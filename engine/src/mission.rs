//! The level's mission logic as the original runs it: numbered triggers between placed objects
//! (docs/subsystems/mission.md; data: tools/remaster_export_mission.py -> mission.json). Every logic object compares
//! a broadcast trigger (Object_SendTrigger 0x00461870) with its own fields, as the original's handlers do:
//! - the mission object (MissionObject_Load 0x0044c170, _OnTrigger 0x0044d600, _Update 0x0044db80, _ShowStart
//!   0x0044daf0): start trigger and text, success, pre-success, failures 1..4, the alarm switch, score;
//! - trigger volumes (TriggerObject 0x00497460 load, 0x004976a0 enter / leave, 0x00497640 send, 0x00497720 enable /
//!   disable): on_enter / on_exit, one_shot, door_mat, enable / disable, ai_can_trigger;
//! - ticker bomb sites (PlayerTickerBomb 0x0044de90 load, 0x0044e150 update, 0x0044e5e0 enable, 0x0044e0c0 set,
//!   0x0044e0e0 not enabled), counters (CounterObject 0x00415770 / 0x00415860), camera fx (CameraFXObject 0x004101a0:
//!   trigger_start / receive_trigger_kill), the pub (PubObject 0x0046a200, 0x0046a770 open, 0x0046a6c0..0x0046a720),
//!   the barracks (0x0040d1b0);
//! - from the export's edges: ui_ears (objective text as a paused message + compass target), sound_ears (voice
//!   line), AI (wake up), objects (destroyed), fx ears, the win.
//! Prompts ("Hold down %s to set the ticker bomb!", "Press %s to enter pub") come from the hint texts with the
//! bound key's name.

use crate::ai::{Grunt, Mind, Player};
use crate::fx::{Destructible, Explode, StartFx};
use crate::level::{InstanceId, game_to_engine};
use crate::levelreset::LevelAppExt;
use bevy::prelude::*;
use serde::Deserialize;
use serde_json::{Map, Value};
use std::collections::{HashMap, HashSet};

#[derive(Deserialize)]
struct EdgeEnd {
    id: Option<u32>,
    #[serde(default)]
    #[allow(dead_code)]
    template: Option<String>,
    #[serde(default)]
    event: Option<String>,
    #[serde(default)]
    action: Option<String>,
    /// the receiver's parameters (text id, fx name, voice lines...): the export stores them here, under `to`
    #[serde(default)]
    detail: Option<Map<String, Value>>,
}

#[derive(Deserialize)]
struct Edge {
    trigger: i64,
    from: EdgeEnd,
    to: EdgeEnd,
    #[serde(default)]
    detail: Option<Map<String, Value>>,
}

#[derive(Deserialize)]
struct InstanceDef {
    id: u32,
    template: String,
    #[serde(default)]
    position: Option<[f32; 3]>,
    #[serde(default)]
    transform: Option<[[f32; 4]; 3]>,
    #[serde(default)]
    model: Option<String>,
    #[serde(default)]
    resolved: Map<String, Value>,
    /// the placement's own values (a sound_ear's line counts only when the placement sets it: 0x004533a0 returns 1)
    #[serde(default)]
    props: Map<String, Value>,
    #[serde(default)]
    volume: Option<Map<String, Value>>,
}

#[derive(Deserialize)]
struct Hint {
    text: Option<String>,
}

#[derive(Deserialize)]
struct MissionFile {
    #[serde(default)]
    level: String,
    mission: MissionInst,
    edges: Vec<Edge>,
    instances: Vec<InstanceDef>,
    text: HashMap<String, String>,
    hints: HashMap<String, Hint>,
}

#[derive(Deserialize)]
struct MissionInst {
    resolved: Map<String, Value>,
}

#[derive(Clone, Debug)]
enum Action {
    WakeAi(u32),
    /// the text id, and whether the same ui_ear marked an objective on this trigger (objective_trigger_receive_0N
    /// with a non-zero objective_tag_0N: 0x0046afa0 then requests the victory music, 0x0044f560 cl = 1)
    ShowText(String, bool),
    Compass(i64),
    /// a sound_ear's line (SoundEar 0x00497750 / 0x00497900): the placement's q_sound / jonsey_sound / roman_sound
    /// (ai_sound + ai_tag: no lev_1_m1 ear has one, not ported), max_distance; it removes itself after playing unless
    /// `reapeat` (sic) is set
    Voice { ear: u32, repeat: bool, max_distance: f32, q: Option<String>, jonesy: Option<String>, roman: Option<String> },
    Fx(String, Vec3),
    /// (the barracks match the trigger ids themselves in dispatch, 0x0040d1b0; kept for the receiver count)
    Barracks(#[allow(dead_code)] u32),
    Destroy(u32),
    Win,
}

/// A trigger volume's shape.
#[derive(Clone, Copy, Debug)]
pub(crate) enum Volume {
    Sphere(Vec3, f32),
    /// the model's box in its own space (game axes, z up) and the map from engine space into it: the placement's
    /// rotation is kept, so a slab turned about z is tested as the slab, not its world aabb (trigger 269, a 20-unit
    /// bb_300x300 turned ~13 degrees, reached Roman's start point through its aabb)
    Box { to_local: Mat4, min: Vec3, max: Vec3 },
}

impl Volume {
    /// The box volume of a model placed by `placement` (the instance's game-space matrix), `min` / `max` its box.
    fn oriented_box(placement: Mat4, min: Vec3, max: Vec3) -> Self {
        let to_engine = Mat4::from_quat(game_to_engine()) * placement;
        Volume::Box { to_local: to_engine.inverse(), min: min.min(max), max: min.max(max) }
    }

    /// `p` is a point 5 units above the feet (the body's centre, guess for the collision proxy's reach); a box is
    /// reached 2 units outside its faces and 20 above its top (the body's height over a thin floor slab).
    pub(crate) fn contains(&self, p: Vec3) -> bool {
        match *self {
            Volume::Sphere(c, r) => c.distance(p) <= r,
            Volume::Box { to_local, min, max } => {
                let l = to_local.transform_point3(p);
                l.cmpge(min - Vec3::splat(2.0)).all() && l.cmple(max + Vec3::new(2.0, 2.0, 20.0)).all()
            }
        }
    }
}

/// A TriggerObject (vtable 0x005a7c20, behaviour "broadcast"): TriggerObject_Load 0x00497460 reads on_enter (+0x460),
/// on_exit (+0x464), enable (+0x468; none: flag 8, enabled at once), disable (+0x46c), sphere_radius (+0x470) and the
/// flags +0x474: 1 one_shot, 2 ai_can_trigger, 4 pending enter, 8 enabled, 0x10 door_mat. Its collision group is 8, or
/// 0x18 with ai_can_trigger (TriggerObject_BuildCollision 0x004975d0: the player, and AI with the flag).
#[derive(Clone, Debug)]
pub(crate) struct Trigger {
    pub id: u32,
    pub on_enter: i64,
    pub on_exit: i64,
    pub enable: i64,
    pub disable: i64,
    pub one_shot: bool,
    pub door_mat: bool,
    pub ai: bool,
    pub enabled: bool,
    pub pending: bool,
    pub volume: Volume,
    /// the objects in contact now (player, AI)
    inside: HashSet<Entity>,
    /// on_enter was used up by a one-shot send (the save keeps it)
    pub fired: bool,
}

impl Trigger {
    fn new(id: u32, r: &Map<String, Value>, volume: Volume) -> Self {
        let n = |k: &str| r.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0);
        let enable = n("enable") as i64;
        Trigger {
            id,
            on_enter: n("on_enter") as i64,
            on_exit: n("on_exit") as i64,
            enable,
            disable: n("disable") as i64,
            one_shot: n("one_shot") != 0.0,
            door_mat: n("door_mat") != 0.0,
            ai: n("ai_can_trigger") != 0.0,
            enabled: enable == 0,
            pending: false,
            volume,
            inside: HashSet::new(),
            fired: false,
        }
    }

    /// The contact callback 0x004976a0: nothing without an on_enter or on_exit trigger; entering sets "pending"
    /// (flag 4, sent by the update); leaving clears it and, enabled, sends on_exit (a one-shot volume clears it first).
    pub fn contact(&mut self, entering: bool) -> Option<i64> {
        if self.on_enter == 0 && self.on_exit == 0 {
            return None;
        }
        if entering {
            self.pending = true;
            return None;
        }
        self.pending = false;
        let t = self.on_exit;
        if t == 0 || !self.enabled {
            return None;
        }
        if self.one_shot {
            self.on_exit = 0;
        }
        Some(t)
    }

    /// The update 0x00497640: a pending enter with no on_enter is dropped; enabled, on_enter is sent and the
    /// pending flag cleared, a one-shot volume without door_mat then clearing on_enter. Disabled, it stays pending.
    pub fn update(&mut self) -> Option<i64> {
        if !self.pending {
            return None;
        }
        if self.on_enter == 0 {
            self.pending = false;
            return None;
        }
        if !self.enabled {
            return None;
        }
        let t = self.on_enter;
        self.pending = false;
        if self.one_shot && !self.door_mat {
            self.on_enter = 0;
            self.fired = true;
        }
        Some(t)
    }

    /// The trigger handler 0x00497720: the enable message sets flag 8, the disable message clears it.
    pub fn message(&mut self, t: i64) {
        if t == self.enable {
            self.enabled = true;
        } else if t == self.disable {
            self.enabled = false;
        }
    }
}

/// A ticker bomb site (PlayerTickerBomb, vtable 0x005ae380; PlayerTickerBomb_Load 0x0044de90).
struct Site {
    id: u32,
    pos: Vec3,
    /// message_radius (+0x498, default 10)
    radius: f32,
    /// receive_trigger_enable (+0x474): non-zero = not enabled yet (PlayerTickerBomb_IsEnabled 0x0044e380); the
    /// trigger clears it (PlayerTickerBomb_OnTrigger 0x0044e5e0)
    enable_trigger: i64,
    /// send_trigger_explode (+0x478; explode2 / 3 +0x47c / +0x480 are read but never sent), send_trigger_set (+0x46c),
    /// send_trigger_non_enabled (+0x470)
    explode: i64,
    set: i64,
    non_enabled: i64,
    /// arm_time (+0x468, 2.0), unarmed_falloff (vt+0x100: the unarmed bomb's alpha, 0.3)
    arm_time: f32,
    falloff: f32,
    /// impact_force +0x484 (2010), area_damage +0x488, damage_radius_inner / outer +0x48c / +0x490
    impact_force: f32,
    area_damage: f32,
    radius_inner: f32,
    radius_outer: f32,
    /// lock_parent_tag (+0x49c) and lock_parent_bone (+0x4a0, p_player_tickerbomb "ref_ticker"): the bomb rides that
    /// bone of the tagged object (PlayerTickerBomb_AttachToParent 0x0044e120; `lock_sites`)
    lock_parent_tag: i64,
    lock_parent_bone: String,
    /// the parent it is attached to (Object_Attach 0x004539f0: the parent referenced at +0x34)
    locked_to: Option<Entity>,
    /// armed (+0x460, PlayerTickerBomb_IsArmed 0x0044e390) with the fuse left (Game+0x25e4)
    armed: Option<f32>,
    hold: f32,
    done: bool,
    /// the ticker prompt runs every 6th frame (0x00467450 counter +0x4f8)
    nag: u32,
    /// the bomb object took its alpha (unarmed_falloff, then 1.0 when armed)
    shown: Option<f32>,
}

impl Site {
    fn enabled(&self) -> bool {
        self.enable_trigger == 0
    }
}

/// A CounterObject (vtable 0x005a4b00, behaviour "counter"; Counter_Init 0x00415770, CounterObject_OnTrigger
/// 0x00415860): counts receive_trigger_activate (+0x460); on reaching count (+0x470) it sends send_trigger (+0x468)
/// and restarts with infinite (+0x474), else it is removed (vt+0xdc). receive_trigger_deactivate (+0x464) removes it.
#[derive(Clone, Debug)]
pub(crate) struct Counter {
    pub id: u32,
    pub activate: i64,
    pub deactivate: i64,
    pub send: i64,
    /// count (+0x470; its getter 0x00415900)
    pub count: i64,
    pub infinite: bool,
    pub current: i64,
    /// +0x475: deactivated (removed)
    pub off: bool,
}

impl Counter {
    pub fn message(&mut self, t: i64) -> Option<i64> {
        if self.off {
            return None;
        }
        if t == self.activate {
            self.current += 1;
            if self.current != self.count {
                return None;
            }
            if self.infinite {
                self.current = 0;
            } else {
                self.off = true;
            }
            return (self.send != 0).then_some(self.send);
        }
        if t == self.deactivate {
            self.off = true;
        }
        None
    }
}

/// A TimerObject (vtable 0x005a7d58, behaviour "timer"; TimerObject_ReadProps 0x00496160): runs from load, or from
/// receive_trigger_activate (+0x460; TimerObject_OnTrigger 0x004965d0); its "send_trigger_time_N" entries
/// ("trigger/time", N = 1.., up to the first missing; trigger 0 dropped; sorted by time) fire as the elapsed time
/// passes them (Timer_Advance 0x00496690, TimerObject_Fire 0x00496660); after the last a looping timer wraps, else it
/// sends send_end_trigger (+0x4a4) and is removed. "display" shows ui_mission_timer with the countdown_time left (or
/// the time elapsed) (TimerObject_Update 0x00496750). The animated variant (anim_name) and the army timer
/// (ArmyTimerObject, create_platoon 0x004964d0) are not ported: none on lev_1_m1.
#[derive(Clone, Debug)]
pub(crate) struct Timer {
    pub id: u32,
    pub activate: i64,
    pub looped: bool,
    pub end: i64,
    pub display: bool,
    pub countdown: f32,
    /// (time, trigger), sorted by time
    pub entries: Vec<(f32, i64)>,
    pub running: bool,
    pub elapsed: f32,
    pub gone: bool,
}

impl Timer {
    fn new(id: u32, r: &Map<String, Value>) -> Self {
        let n = |k: &str| r.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0);
        let mut entries = Vec::new();
        for k in 1..=10 {
            let Some(v) = r.get(&format!("send_trigger_time_{k}")).and_then(|v| v.as_str()) else { break };
            // "Timer has invalid property format" without a '/': skipped
            let Some((t, time)) = v.split_once('/') else { continue };
            let t = t.trim().parse::<i64>().unwrap_or(0);
            if t != 0 {
                entries.push((time.trim().parse::<f32>().unwrap_or(0.0), t));
            }
        }
        entries.sort_by(|a, b| a.0.total_cmp(&b.0));
        let activate = n("receive_trigger_activate") as i64;
        Timer { id, activate, looped: n("loop") != 0.0, end: n("send_end_trigger") as i64, display: n("display") != 0.0, countdown: n("countdown_time") as f32, entries, running: activate == 0, elapsed: 0.0, gone: false }
    }

    /// Timer_Advance 0x00496690: the elapsed time moves on by `dt`; every entry from the previous time to the new one
    /// fires; past the last a loop wraps (previous 0, elapsed less that entry's time), else the end trigger and removal.
    pub fn advance(&mut self, dt: f32) -> Vec<i64> {
        let mut out = Vec::new();
        if !self.running || self.gone {
            return out;
        }
        let mut prev = self.elapsed;
        self.elapsed += dt;
        let n = self.entries.len();
        let mut i = 0;
        while i < n {
            let (t, trig) = self.entries[i];
            if prev > t {
                i += 1;
                continue;
            }
            if self.elapsed <= t {
                return out;
            }
            out.push(trig);
            if i + 1 == n {
                if self.looped && t > 0.0 {
                    prev = 0.0;
                    self.elapsed -= t;
                    i = 0;
                    continue;
                }
                if self.end != 0 {
                    out.push(self.end);
                }
                self.gone = true;
                return out;
            }
            i += 1;
        }
        out
    }

    /// What ui_mission_timer shows: countdown_time less the elapsed time (not below 0), or the elapsed time.
    pub fn shown(&self) -> f32 {
        if self.countdown == 0.0 { self.elapsed } else { (self.countdown - self.elapsed).max(0.0) }
    }
}

/// A CameraFXObject (vtable 0x005ad058, behaviour "camera_fx"; Load 0x004101a0): its fx starts at load when
/// trigger_start (+0x480) is 0 (level.rs / main.rs start those), else when that trigger arrives; receive_trigger_kill
/// (+0x484) removes it and its fx.
struct CamFx {
    id: u32,
    start: i64,
    kill: i64,
    fx: String,
    position_only: bool,
    /// the running fx's owner (a child of the camera): despawning it ends the fx (fxvm.rs releases an fx whose
    /// owner is gone, Fx_Release 0x0041d030)
    holder: Option<Entity>,
    gone: bool,
}

/// A troop generator (TwigletGenerator / BarricadeObject, set-up 0x0040ad80, update 0x0040cb10, release 0x0040c910,
/// triggers 0x0040d1b0; docs/subsystems/mission.md "Barracks troop release").
struct Barracks {
    id: u32,
    /// the troops still to release by type (+0x53c: bow, regular, jumper captain, captain, rocket captain, monk) and
    /// the counts as loaded (+0x554, the reset trigger's refill)
    counts: [i64; 6],
    initial: [i64; 6],
    /// the batch still to come (+0x59c), release_amount (+0x598), release_count (+0x594)
    batch: u32,
    amount: u32,
    count: u32,
    /// the release delay +0x574, rolled in release_time_min..max (+0x578 / +0x57c)
    timer: f32,
    release: (f32, f32),
    /// +0x460 bit 3: releasing
    active: bool,
    /// +0x460 bit 0x10 (base_defend): the base generators release one troop every delay, no batches (0x0040cb10's
    /// paced path -> 0x0040c710: spawn, and once empty the counts refilled and the release stops)
    paced: bool,
    /// receive_trigger_activate (+0x534; cleared once used, 0x0040d1b0), deactivate (+0x538), trigger_reset (+0x5a4)
    activate: i64,
    deactivate: i64,
    reset: i64,
    /// alert_group (+0x530), marker_tag_min/max (+0x580 / +0x584): handed to every troop (0x0042db50)
    alert_group: i64,
    marker_tags: (i64, i64),
    /// priority_target_tag_min / max (+0x56c / +0x570): handed to every troop as his priority targets (0x0040c590 ->
    /// 0x0042cc60)
    priority_tags: (i64, i64),
    /// the exit slot's "ref_spawn_00" and "ref_exit_00" points (engine space); none: no exit slot, no troops
    spawn: Option<(Vec3, Vec3)>,
    /// health when last seen: damage while `activate` is set starts the release (BarricadeObject_Slot52 0x0040ce40)
    health: Option<f32>,
    /// a troop asked for and not yet spawned (the original creates it at once and it joins the list +0x5a0 there;
    /// here the spawn is a message handled a frame later: it counts as alive and walking out meanwhile)
    pending: bool,
}

impl Barracks {
    fn left(&self) -> i64 {
        self.counts.iter().sum()
    }
    /// the troop type with the most remaining, the first of equals (0x0040c910)
    fn most(&self) -> usize {
        (1..6).fold(0, |best, k| if self.counts[k] > self.counts[best] { k } else { best })
    }
}

/// A troop released by barracks `0` (counted in its list +0x5a0 while alive).
#[derive(Component)]
pub struct BarracksTroop(pub u32);

/// The troop templates by type (0x0040c590's table: 0 "grunt", 1 "grunt_regular", 2..4 "captain", 5 "monk").
const TROOP_TEMPLATES: [&str; 6] = ["grunt", "grunt_regular", "captain", "captain", "captain", "monk"];

/// A reference bone of a model (its reference anim's first key: anims/<model>.json), in the model's game space.
pub(crate) fn pub_bone(dir: &std::path::Path, model: &str, bone: &str) -> Option<Vec3> {
    let v: Value = serde_json::from_str(&std::fs::read_to_string(dir.join("anims").join(format!("{model}.json"))).ok()?).ok()?;
    let (_, track) = v.get("tracks")?.as_object()?.iter().find(|(k, _)| k.eq_ignore_ascii_case(bone))?;
    let k = track.get("t")?.as_array()?.first()?.as_array()?;
    let f = |i: usize| k.get(i).and_then(|x| x.as_f64()).unwrap_or(0.0) as f32;
    Some(Vec3::new(f(1), f(2), f(3)))
}

/// What a pub has (p_pub with the instance's overrides; PubObject_Load 0x0046a200).
#[derive(Clone, Debug)]
pub struct PubInfo {
    /// the weapons it sells, in the weapon table's order
    pub sells: Vec<&'static str>,
    /// its special weapon
    pub special: Option<&'static str>,
    pub has_tickerbomb: bool,
    /// health left in its pool (+0x46c "health_pool", get / set 0x0046a6a0 / 0x0046a6b0)
    pub pool: f32,
    /// the pool at load (+0x470, a copy of health_pool): the pool bar's full scale
    pub pool_full: f32,
    /// the pub screen's bars move by this fraction of their full scale per second (+0x474: health_bar_rate x 0.01,
    /// default 50)
    pub bar_rate: f32,
    /// should_refill_ammo (+0x467, default 1, 0x0046a5e0): the guns Roman holds are topped up when he enters
    pub refill: bool,
}

/// The pub's trigger fields (PubObject_Load 0x0046a200): listen_trigger_open +0x4c8 (closed until it arrives,
/// 0x0046a6c0; PubObject_OnTrigger 0x0046a770 clears it), send_trigger_entry +0x4c0 (once, 0x0046a6f0),
/// send_trigger_entry_failure +0x4bc (0x0046a6d0), send_trigger_exit +0x4c4 (once, 0x0046a720).
#[derive(Clone, Debug, Default)]
struct PubTriggers {
    open: i64,
    entry: i64,
    entry_failure: i64,
    /// the player was inside the doorway radius last frame (the failure is sent on stepping in: guess, the prompt
    /// 0x00467b60 compares two player points, +0xc0c inside and +0xc18 outside the radius)
    was_inside: bool,
}

/// One pub (PubObject 0x0046a200): its doorway, trigger fields, whether Roman has been in, and what it has. A level
/// may place several (lev_1_m3: 119 with the ticker bombs, opened by 804, and 283 by the second village).
#[derive(Clone, Debug)]
pub struct Pub {
    pub id: u32,
    /// the Ref_entrance bone's position (engine space) and doorway_entry_radius
    door: Vec3,
    radius: f32,
    /// send_trigger_exit, sent when the pub screen is left (0x0046a720)
    exit: i64,
    triggers: PubTriggers,
    /// entered once: the doorway takes no more entries (0x0046a6f0)
    done: bool,
    pub info: PubInfo,
}

/// The level state (Game+0xa64 +0xc, set by 0x0044a580, "over" when > 1: 0x0044a590): playing, won (2), failed (3..8).
#[derive(Clone, Copy, PartialEq, Debug, Default)]
pub enum Outcome {
    #[default]
    Playing,
    Won,
    Failed,
}

/// The player's weapons by id (the weapon state's slots, game_09.cpp; names of MissionObject_Load's properties).
const WEAPONS: [&str; 6] = ["machinegun", "snipergun", "rifle", "vindaloo", "gurner", "sharkgun"];
/// The specials by id 6..10.
const SPECIALS: [&str; 5] = ["guyfawkes", "bunchoffives", "topsyturvy", "blackhole", "stickybomb"];

/// The player's loadout at the level's start. MissionObject_Load 0x0044c170 reads player_add_* (Game+0xd93 + id, default
/// 1 for the machine gun, 0 for the others), player_remove_* (+0xdaf + id), player_set_ammo_* (+0x25e8 + 4 id, default
/// -2) and player_set_num_* (+0x2600.., default -1); the player's reset 0x00475650 applies them to the player object the
/// level builds (so nothing is carried from the last level): the removes (on its empty list: no effect), the adds in id
/// order (0x004a0ef0; the primary list holds 3), the amounts (0x004a14e0: -2 fills clip and spare to the maximum, -1
/// leaves the record, n sets it; a special with n > 0 joins the special list), then it equips slot 0 (0x004a10e0): the
/// first weapon by id is in hand. The topsy count is read as player_set_num_topsyturvy (0x005b1e04), a name the
/// mission template does not use (it says player_set_num_topsy): the template's value never applies (original data quirk).
#[derive(Clone, Debug, PartialEq)]
pub struct Loadout {
    /// the weapons held, in slot order (the first in hand)
    pub weapons: Vec<&'static str>,
    /// player_set_ammo_* of each weapon (-2 full, -1 unchanged, n rounds)
    pub ammo: HashMap<&'static str, i64>,
    /// player_set_num_* of each special (-1 unchanged, so none on a new player)
    pub specials: HashMap<&'static str, i64>,
    /// player_has_jetpack (Game byte +0xd92, game_02.cpp:1999): the reset gives the jumper pack (jetpack.rs)
    pub jetpack: bool,
    /// vindaloo_is_upgraded (Game byte +0xdd0, game_02.cpp:2006, default 0): the vindaloo fires four rockets
    pub vindaloo_upgraded: bool,
}

impl Loadout {
    /// From the mission object's resolved properties.
    pub fn from_props(m: &Map<String, Value>) -> Self {
        let n = |k: String, default: i64| m.get(&k).and_then(|v| v.as_f64()).map_or(default, |v| v as i64);
        let weapons = WEAPONS.iter().copied().filter(|w| n(format!("player_add_{w}"), (*w == "machinegun") as i64) != 0).take(3).collect();
        let ammo = WEAPONS.iter().map(|w| (*w, n(format!("player_set_ammo_{w}"), -2))).collect();
        let specials = SPECIALS.iter().map(|s| (*s, n(format!("player_set_num_{s}"), -1))).collect();
        Loadout { weapons, ammo, specials, jetpack: n("player_has_jetpack".into(), 0) != 0, vindaloo_upgraded: n("vindaloo_is_upgraded".into(), 0) != 0 }
    }

    pub fn has(&self, weapon: &str) -> bool {
        self.weapons.contains(&weapon)
    }

    /// A special's count on a new player (-1 leaves it at 0).
    pub fn special(&self, name: &str) -> u32 {
        self.specials.get(name).copied().unwrap_or(-1).max(0) as u32
    }
}

#[derive(Resource)]
pub struct Mission {
    /// the level's name (0x0044a660 on Game+0xa64: the success bonus of lev_1_m1_b / lev_2_m1)
    level: String,
    receivers: HashMap<i64, Vec<Action>>,
    /// objects that send a trigger when they die (send_trigger_on_death: DefaultGameObject_OnDeath 0x00450ee0,
    /// FxExplosion_Die game_02.cpp:797): instance id -> trigger (pipe 96 -> 711, boom_island 131 -> 301)
    death_senders: HashMap<u32, i64>,
    triggers: Vec<Trigger>,
    sites: Vec<Site>,
    counters: Vec<Counter>,
    timers: Vec<Timer>,
    /// a displayed timer's value (ui_mission_timer, drawn by hud.rs mission_time_text)
    pub timer_display: Option<f32>,
    camfx: Vec<CamFx>,
    /// the level's pubs, in instance order
    pubs: Vec<Pub>,
    /// the pub Roman went into last (its screen, its exit trigger)
    entered_pub: Option<usize>,
    /// the player entered a pub: the pub screen opens (gamemenu.rs)
    pub pub_request: bool,
    /// the pub screen was left: send its exit trigger
    pub pub_exit_pending: bool,
    barracks: Vec<Barracks>,
    texts: HashMap<String, String>,
    hints: HashMap<String, String>,
    /// objects by tag (+0x390): instance id and position; `untagged` ids lost their tag or died (an armed ticker
    /// bomb's +0x390 = 0, 0x0044e4a0; a removed object leaves the lists the compass walks)
    tags: HashMap<i64, Vec<(u32, Vec3)>>,
    untagged: HashSet<u32>,
    pub compass_tag: i64,
    /// ai_target_tag (Game+0xae8): while set, a bad guy's target search takes only objects of this tag, at any range,
    /// without the view cone or the line of sight (AI_FindTarget 0x00426920, ai.rs `search`): lev_bonus_m1's Roman
    /// (the whole army has him as target from the first frame); the defence levels' base wall and turret
    pub ai_target_tag: i64,
    /// ai_max_fire (Game+0xc50, MissionObject_Load game_02.cpp:2024): bad guys firing at once, 0 no limit (ai/max_fire.rs)
    pub ai_max_fire: u32,
    /// army_max_scale (Game+0xc54, game_02.cpp:2025): grunts at once in the ladder / scale-wall assault (World+0xf14)
    pub army_max_scale: u32,
    start_trigger: i64,
    start_text: Option<String>,
    success_text: Option<String>,
    /// receive_trigger_success (+0x46c), receive_trigger_pre_success (+0x470), receive_trigger_failure[_2.._4]
    /// (+0x474..+0x480) and their texts (Game+0x2664..+0x2670)
    success: i64,
    pre_success: i64,
    failures: [i64; 4],
    failure_texts: [Option<String>; 4],
    /// alarm_trigger_activate / _deactivate (Game+0x26d8 / +0x26dc: Game+0xdcc, the alarm forced on),
    /// army_breach_open / close_trigger (Game+0xc3c / +0xc40: Game+0xcbe), peasant_saved_trigger (Game+0x26f0)
    alarm_triggers: (i64, i64),
    breach_triggers: (i64, i64),
    /// the rescue rules (Game+0x26e0..+0x26f8), read by peasant.rs
    pub peasant_rules: PeasantRules,
    /// Game+0x2674: the text of failure state 8 (timeout_text, default "ui_timeout": the peasant casualties' failure
    /// 0x004062a0 shows it; 0x0044a390 takes Game+0x2654 + state x 4)
    timeout_text: Option<String>,
    /// Game+0xdcc: the alarm forced by the mission (the alarm controller is alarm.rs; no lev_1_m1 trigger sets it)
    pub alarm_forced: bool,
    /// Game+0xcbe: the army breach open (no army on lev_1_m1)
    pub breach_open: bool,
    /// start_in_turret_tag (Game+0x26cc): the level starts with Roman in a turret (0x0044e600)
    pub start_in_turret: bool,
    /// the base's two rectangles base_x_min_N / base_y_min_N / base_x_max_N / base_y_max_N (Game+0xbd0..0xbec, loaded by
    /// 0x0045a4e0: game_02.cpp:2033-2040), game x / y: [x_min, y_min, x_max, y_max] (ai/base_area.rs)
    pub base_areas: [[f32; 4]; 2],
    /// Game+0xdd2: finished (pre-success; read by 0x0044e620): failure triggers are ignored from then on
    pub finished: bool,
    /// Game+0xac8 = 1: Roman rides a base turret (defence.rs keeps it): a failure trigger then calls the failure mob
    /// (0x0049ad60) instead of failing; `failure_mob_calls` counts the calls defence.rs has still to make
    pub base_turret_mounted: bool,
    pub failure_mob_calls: u32,
    /// Game+0xdcd: the failure mob is on the turret (the success trigger is ignored; defence.rs fails the level when
    /// failure_mode_duration has passed)
    pub failure_mob: bool,
    /// score_value (Loot at the win, game_02.cpp:2262) and next_level_name (Game+0x268c)
    pub score_value: f32,
    pub next_level: Option<String>,
    /// the player's weapons and specials at the start (player.rs applies it)
    pub loadout: Loadout,
    pub failure_text: Option<String>,
    started: bool,
    /// send_trigger_start sent (the first update)
    start_sent: bool,
    /// paused messages waiting for "continue", and the one shown
    queue: Vec<(f32, String)>,
    pub showing: Option<String>,
    pub tickers: u32,
    pub outcome: Outcome,
    dead_timer: f32,
    pub log: Vec<String>,
    /// camera_startup_path_anim: the camera path the level starts on (MissionObject slot 8 0x0044d150 ->
    /// Camera_StartPath mode 2; campath.rs `startup`); no shipped level sets one (all 24 mission.json: empty)
    pub startup_path: Option<String>,
    /// the "patience" text's time left (ui_tickerbomb_patience: raised by Use at a site while another bomb ticks,
    /// 0x0044e3a0; shown 2.0 s by its timed text 0x004677c0)
    patience: f32,
    pub objectives: String,
    /// the objectives text with both forms (the pause menu shows the short one), and its text id
    pub objectives_raw: String,
    pub objectives_id: String,
    /// sound_ears that played and removed themselves
    pub ears_done: std::collections::HashSet<u32>,
    /// new messages for the feed: (chat colour index, text), handed to hud.rs's Chat each frame
    pub feed: Vec<(u8, String)>,
    /// triggers a handler sends while handling another (a counter reaching its count): sent next frame
    resend: Vec<i64>,
}

#[derive(Message, Clone, Copy)]
pub struct SendTrigger(pub i64);

/// A trigger sent by a volume (Object_SendTrigger 0x00461870 passes the sender to every handler): the handlers that
/// act on their sender read this beside [`SendTrigger`] (the mission's peasant_saved_trigger banks peasants at the
/// door mat that sent it, MissionObject_OnTrigger 0x0044d600).
#[derive(Message, Clone, Copy, Debug)]
pub struct VolumeSent {
    /// the volume's instance id
    pub volume: u32,
    pub trigger: i64,
}

/// The mission's peasant rules (MissionObject_Load 0x0044c170, game_02.cpp:2042-2045): peasant_max_casualties
/// (Game+0x26e0), peasant_max_saved (+0x26ec; non-zero shows the peasant counters, 0x00465be0..0x00465d30),
/// peasant_saved_trigger (+0x26f0: a door mat banks the carried peasants) and peasant_success_trigger (+0x26f8: sent
/// once when the saved reach max_saved).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PeasantRules {
    pub max_casualties: u32,
    pub max_saved: u32,
    pub saved_trigger: i64,
    pub success_trigger: i64,
}

/// The compass's first list (World+0xefc, ui_compass_objective 0x00466790): while it holds anything the arrow points
/// at its nearest member, and a tagged objective (Game+0x2658) only when strictly nearer. The peasants not yet picked
/// up are on it (PeasantObject_Load 0x004056b0; the pick-up 0x00405d10 takes them off): peasant.rs fills it each frame.
#[derive(Resource, Default, Debug)]
pub struct CompassList(pub Vec<Vec3>);

/// A trigger volume's sphere_radius as the original reads it: TriggerObject_Load 0x00497460 reads the property as a
/// dword (Object_ReadValueProp 0x00453400) and BuildCollision 0x004975d0 converts it as an unsigned integer. An integer
/// in the data is its value; a float (lev_1_m1_b's spheres 376 / 387: 100.0) is its bit pattern, 0x42c80000 =
/// 1.12e9 units, a sphere over the whole level: both fire at the load when Roman appears inside (original bug,
/// docs/original-bugs.md; the oracle's trace_calls of 0x004976a0: their enter callback has Roman, 1200 units away).
fn sphere_radius(r: &Map<String, Value>) -> f32 {
    match r.get("sphere_radius") {
        Some(Value::Number(v)) if v.is_f64() => (v.as_f64().unwrap_or(0.0) as f32).to_bits() as f32,
        Some(v) => v.as_f64().unwrap_or(0.0) as f32,
        None => 0.0,
    }
}

/// The send_trigger_on_death of a grunt: a placed one's from its instance (lev_5_m1's Q1-12s, lev_4_m1's monks), one made
/// during play (barracks troops, the army's released grunts, alarm troops) from its template
/// (DefaultGameObject_OnDeath 0x00450ee0 sends
/// +0x488 for any object). Sent once, when it dies (`troop_deaths`).
#[derive(Component, Clone, Copy, Debug)]
pub struct DeathTrigger(pub i64);

fn game_pos(p: [f32; 3]) -> Vec3 {
    game_to_engine() * Vec3::from(p)
}

/// The text as shown in game: the long form (<long> kept, <short> dropped: Renderer_TextSkipTaggedBlock).
pub fn long_form(t: &str) -> String {
    let mut s = t.to_string();
    while let (Some(a), Some(b)) = (s.find("<short>"), s.find("</short>")) {
        if b < a {
            break;
        }
        s.replace_range(a..b + "</short>".len(), "");
    }
    s.replace("<long>", "").replace("</long>", "").replace("<BR>", "<br>")
}

/// The short form (<short> kept, <long> dropped: render flag 8), as the pause menu's objectives show it.
pub fn short_form(t: &str) -> String {
    let mut s = t.to_string();
    while let (Some(a), Some(b)) = (s.find("<long>"), s.find("</long>")) {
        if b < a {
            break;
        }
        s.replace_range(a..b + "</long>".len(), "");
    }
    s.replace("<short>", "").replace("</short>", "").replace("<BR>", "<br>")
}

/// The mission's state in a saved game (savegame.rs).
#[derive(serde::Serialize, serde::Deserialize, Default, Clone, Debug, PartialEq)]
pub struct MissionSave {
    pub triggers_done: Vec<u32>,
    /// (id, enabled, done)
    pub sites: Vec<(u32, bool, bool)>,
    /// (id, active, left)
    pub barracks: Vec<(u32, bool, i64)>,
    /// the first pub's state (saves from before `pubs`)
    pub pub_done: bool,
    pub pub_pool: Option<f32>,
    pub compass_tag: i64,
    pub tickers: u32,
    pub objectives: String,
    pub objectives_raw: String,
    pub ears_done: Vec<u32>,
    /// (id, enabled, on_exit cleared) of each volume; counters (id, current, off); objects without their tag; the
    /// pub opened; the mission finished; the alarm forced
    #[serde(default)]
    pub volumes: Vec<(u32, bool, bool)>,
    #[serde(default)]
    pub counters: Vec<(u32, i64, bool)>,
    #[serde(default)]
    pub untagged: Vec<u32>,
    #[serde(default)]
    pub pub_open: bool,
    #[serde(default)]
    pub finished: bool,
    #[serde(default)]
    pub alarm_forced: bool,
    /// every pub: (id, entered, health pool left, opened)
    #[serde(default)]
    pub pubs: Vec<(u32, bool, f32, bool)>,
}

impl Mission {
    /// A counter's count so far and its target, by instance id (defence.rs: the grunts-over-the-wall counter,
    /// grunt_wall_counter_tag, ui_grunt_over_wall_count)
    pub fn counter(&self, id: u32) -> Option<(i64, i64)> {
        self.counters.iter().find(|c| c.id == id && !c.off).map(|c| (c.current, c.count))
    }

    /// What the pub Roman last went into has (pubscreen.rs: its offers and health pool).
    pub fn entered_pub_info(&mut self) -> Option<&mut PubInfo> {
        let i = self.entered_pub?;
        self.pubs.get_mut(i).map(|p| &mut p.info)
    }

    /// The mission start has been shown (MissionObject_ShowStart 0x0044daf0; savegame.rs's first checkpoint).
    pub fn started(&self) -> bool {
        self.started
    }

    pub fn snapshot(&self) -> MissionSave {
        MissionSave {
            triggers_done: self.triggers.iter().filter(|t| t.fired).map(|t| t.id).collect(),
            sites: self.sites.iter().map(|s| (s.id, s.enabled(), s.done || s.armed.is_some())).collect(),
            barracks: self.barracks.iter().map(|b| (b.id, b.active, b.left())).collect(),
            pub_done: self.pubs.first().is_some_and(|p| p.done),
            pub_pool: self.pubs.first().map(|p| p.info.pool),
            compass_tag: self.compass_tag,
            tickers: self.tickers,
            objectives: self.objectives.clone(),
            objectives_raw: self.objectives_raw.clone(),
            ears_done: self.ears_done.iter().copied().collect(),
            volumes: self.triggers.iter().map(|t| (t.id, t.enabled, t.on_exit == 0)).collect(),
            counters: self.counters.iter().map(|c| (c.id, c.current, c.off)).collect(),
            untagged: self.untagged.iter().copied().collect(),
            pub_open: self.pubs.first().is_some_and(|p| p.triggers.open == 0),
            finished: self.finished,
            alarm_forced: self.alarm_forced,
            pubs: self.pubs.iter().map(|p| (p.id, p.done, p.info.pool, p.triggers.open == 0)).collect(),
        }
    }

    /// Back to a saved state; the start trigger and message are not replayed.
    pub fn restore(&mut self, s: &MissionSave) {
        for t in &mut self.triggers {
            if s.triggers_done.contains(&t.id) {
                t.fired = true;
                t.on_enter = 0;
            }
            if let Some(&(_, en, exit_gone)) = s.volumes.iter().find(|x| x.0 == t.id) {
                t.enabled = en;
                if exit_gone {
                    t.on_exit = 0;
                }
            }
        }
        for st in &mut self.sites {
            if let Some(&(_, en, done)) = s.sites.iter().find(|x| x.0 == st.id) {
                if en {
                    st.enable_trigger = 0;
                }
                st.done = done;
            }
        }
        for c in &mut self.counters {
            if let Some(&(_, cur, off)) = s.counters.iter().find(|x| x.0 == c.id) {
                c.current = cur;
                c.off = off;
            }
        }
        for b in &mut self.barracks {
            if let Some(&(_, active, left)) = s.barracks.iter().find(|x| x.0 == b.id) {
                b.active = active;
                // the save keeps the total: take the rest off the types with the most remaining (0x0040c910's order)
                while b.left() > left.max(0) {
                    let t = b.most();
                    b.counts[t] -= 1;
                }
            }
        }
        if s.pubs.is_empty() {
            // a save from before every pub was kept: its fields are the (only) first pub's
            if let Some(p) = self.pubs.first_mut() {
                p.done = s.pub_done;
                if let Some(pool) = s.pub_pool {
                    p.info.pool = pool;
                }
                if s.pub_open {
                    p.triggers.open = 0;
                }
            }
        }
        for &(id, done, pool, open) in &s.pubs {
            if let Some(p) = self.pubs.iter_mut().find(|p| p.id == id) {
                p.done = done;
                p.info.pool = pool;
                if open {
                    p.triggers.open = 0;
                }
            }
        }
        self.untagged.extend(s.untagged.iter().copied());
        // (sites saved as done or armed have lost their tag: 0x0044e4a0 clears it when arming)
        for st in &self.sites {
            if st.done {
                self.untagged.insert(st.id);
            }
        }
        self.finished = s.finished;
        self.alarm_forced = s.alarm_forced;
        self.compass_tag = s.compass_tag;
        self.tickers = s.tickers;
        self.objectives = s.objectives.clone();
        self.objectives_raw = s.objectives_raw.clone();
        self.ears_done = s.ears_done.iter().copied().collect();
        self.started = true;
        self.start_sent = true;
        self.start_text = None;
        self.queue.clear();
        self.showing = None;
    }

    pub fn load(dir: &std::path::Path) -> Option<Self> {
        let f: MissionFile = serde_json::from_str(&std::fs::read_to_string(dir.join("mission.json")).ok()?).map_err(|e| warn!("mission.json: {e}")).ok()?;
        let inst: HashMap<u32, &InstanceDef> = f.instances.iter().map(|i| (i.id, i)).collect();
        let templates: HashMap<String, crate::gamedata::Template> =
            std::fs::read_to_string(dir.join("templates.json")).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default();
        let s = |m: &Map<String, Value>, k: &str| m.get(k).and_then(|v| v.as_str()).filter(|v| !v.is_empty() && *v != "none").map(str::to_owned);
        let n = |m: &Map<String, Value>, k: &str| m.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0);
        let mut receivers: HashMap<i64, Vec<Action>> = HashMap::new();
        let mut death_senders = HashMap::new();
        for e in &f.edges {
            if let (Some(fid), Some(ev)) = (e.from.id, e.from.event.as_deref()) {
                if ev.contains("dies") {
                    death_senders.insert(fid, e.trigger);
                }
            }
            // receivers
            let Some(to) = e.to.id else { continue };
            let action = e.to.action.as_deref().unwrap_or("");
            // (read from `to`: the edge-level field was always empty, so texts, the compass retarget and fx ears were
            // dropped: no l1m1_sub2 / sub3 briefings, the compass pointing nowhere after the pub)
            let d = e.to.detail.clone().or_else(|| e.detail.clone()).unwrap_or_default();
            let a = if action.starts_with("AI wakes up") {
                Action::WakeAi(to)
            } else if action.starts_with("show text_display") {
                let r = inst.get(&to).map(|i| &i.resolved);
                // the ui_ear's fields (its Load 0x0046acf0: text_trigger_receive_0N, text_display_0N,
                // objective_trigger_receive_0N, objective_tag_0N)
                let objective = r.is_some_and(|r| {
                    (1..=5).any(|k| n(r, &format!("objective_trigger_receive_{k:02}")) as i64 == e.trigger && n(r, &format!("objective_tag_{k:02}")) != 0.0)
                });
                match s(&d, "text_id") {
                    Some(t) => Action::ShowText(t, objective),
                    None => continue,
                }
            } else if action.starts_with("compass target") {
                Action::Compass(d.get("objective_tag").and_then(|v| v.as_i64()).unwrap_or(0))
            } else if action.starts_with("play one voice line") {
                let r = inst.get(&to).map(|i| &i.resolved);
                let placed = |k: &str| inst.get(&to).and_then(|i| i.props.get(k)).and_then(|v| v.as_str()).map(str::to_owned);
                Action::Voice {
                    ear: to,
                    repeat: r.and_then(|r| r.get("reapeat")).and_then(|v| v.as_f64()).unwrap_or(0.0) != 0.0,
                    max_distance: r.and_then(|r| r.get("max_distance")).and_then(|v| v.as_f64()).unwrap_or(300.0) as f32,
                    q: placed("q_sound"),
                    jonesy: placed("jonsey_sound"),
                    roman: placed("roman_sound"),
                }
            } else if action.starts_with("toggle the fx") {
                match (s(&d, "fx_name"), inst.get(&to).and_then(|i| i.position)) {
                    (Some(fx), Some(p)) => Action::Fx(fx, game_pos(p)),
                    _ => continue,
                }
            } else if action.starts_with("barracks") {
                Action::Barracks(to)
            } else if action.starts_with("destroyed") {
                Action::Destroy(to)
            } else if action.starts_with("WIN") {
                Action::Win
            } else {
                // (bomb site enable, volume enable / disable, counters, camera fx, the pub's open trigger: the
                // objects' own trigger fields, compared in dispatch)
                continue;
            };
            receivers.entry(e.trigger).or_default().push(a);
        }
        let mut triggers = Vec::new();
        let mut sites = Vec::new();
        let mut counters = Vec::new();
        let mut camfx = Vec::new();
        let mut timers = Vec::new();
        let mut barracks = Vec::new();
        // the set-up's rand() (0x0040ad80: the first release delay)
        let mut crt = crate::fxvm::CrtRand::default();
        let mut pubs = Vec::new();
        let mut tags: HashMap<i64, Vec<(u32, Vec3)>> = HashMap::new();
        for i in &f.instances {
            let r = &i.resolved;
            let pos = game_pos(i.position.unwrap_or_default());
            let tag = n(r, "tag") as i64;
            if tag != 0 {
                tags.entry(tag).or_default().push((i.id, pos));
            }
            // the class: the resolved behaviour, else the template name (ObjectSystem_CreateObject 0x0045f020)
            let behavior = s(r, "behavior").unwrap_or_else(|| i.template.clone());
            match behavior.as_str() {
                "broadcast" => {
                    // the shape: sphere_radius > 0 a sphere (TriggerObject_BuildCollision 0x004975d0), else the
                    // model's own collision (the export's box of its vertices: guess, mission.md)
                    let vol = i.volume.as_ref();
                    let kind = vol.and_then(|v| v.get("kind")).and_then(|v| v.as_str()).unwrap_or("sphere");
                    let volume = if kind == "box" {
                        // the model's box (local_min / local_max) under the placement's transform
                        let corner = |k: &str| -> Vec3 {
                            let a = vol.and_then(|v| v.get(k)).and_then(|v| v.as_array());
                            let c = |j: usize| a.and_then(|a| a.get(j)).and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
                            Vec3::new(c(0), c(1), c(2))
                        };
                        let placement = i.transform.as_ref().map(crate::scene::game_matrix).unwrap_or(Mat4::from_translation(Vec3::from(i.position.unwrap_or_default())));
                        Volume::oriented_box(placement, corner("local_min"), corner("local_max"))
                    } else {
                        Volume::Sphere(pos, sphere_radius(r))
                    };
                    triggers.push(Trigger::new(i.id, r, volume));
                }
                "player_tickerbomb" => sites.push(Site {
                    id: i.id,
                    pos,
                    radius: r.get("message_radius").and_then(|v| v.as_f64()).unwrap_or(10.0) as f32,
                    enable_trigger: n(r, "receive_trigger_enable") as i64,
                    explode: n(r, "send_trigger_explode") as i64,
                    set: n(r, "send_trigger_set") as i64,
                    non_enabled: n(r, "send_trigger_non_enabled") as i64,
                    arm_time: r.get("arm_time").and_then(|v| v.as_f64()).unwrap_or(2.0) as f32,
                    falloff: r.get("unarmed_falloff").and_then(|v| v.as_f64()).unwrap_or(0.3) as f32,
                    impact_force: r.get("impact_force").and_then(|v| v.as_f64()).unwrap_or(2010.0) as f32,
                    area_damage: n(r, "area_damage") as f32,
                    radius_inner: n(r, "damage_radius_inner") as f32,
                    radius_outer: n(r, "damage_radius_outer") as f32,
                    lock_parent_tag: n(r, "lock_parent_tag") as i64,
                    lock_parent_bone: s(r, "lock_parent_bone").unwrap_or_default(),
                    locked_to: None,
                    armed: None,
                    hold: 0.0,
                    done: false,
                    nag: 0,
                    shown: None,
                }),
                "counter" => counters.push(Counter {
                    id: i.id,
                    activate: n(r, "receive_trigger_activate") as i64,
                    deactivate: n(r, "receive_trigger_deactivate") as i64,
                    send: n(r, "send_trigger") as i64,
                    count: r.get("count").and_then(|v| v.as_f64()).unwrap_or(1.0) as i64,
                    infinite: n(r, "infinite") != 0.0,
                    current: 0,
                    off: false,
                }),
                "timer" => {
                    if s(r, "anim_name").is_some() {
                        warn!("timer {}: anim_name timers not ported", i.id);
                    }
                    timers.push(Timer::new(i.id, r));
                }
                "camera_fx" => {
                    if let Some(fx) = s(r, "fx_name") {
                        camfx.push(CamFx {
                            id: i.id,
                            start: n(r, "trigger_start") as i64,
                            kill: n(r, "receive_trigger_kill") as i64,
                            fx,
                            position_only: r.get("position_only").and_then(|v| v.as_f64()).unwrap_or(1.0) != 0.0,
                            holder: None,
                            gone: false,
                        });
                    }
                }
                "pub" => {
                    // the doorway: the model's Ref_entrance bone (doorway_bone_name) and doorway_entry_radius
                    // (0x00467b60: player position within the radius; 0x0046a5f0, 0x0046a640); the exit sends
                    // send_trigger_exit (0x0046a720). The leave (0x004690c0) also takes the Ref_exit bone's position
                    // (player_exit_bone_name, 0x0046a650) but never uses it: Roman stays at the door (oracle pub: he
                    // is at the warp point before and after the screen)
                    let door = i.model.as_ref().zip(i.transform.as_ref()).and_then(|(model, t)| pub_bone(dir, model, "ref_entrance").map(|l| game_to_engine() * crate::scene::game_matrix(t).transform_point3(l)));
                    let triggers = PubTriggers {
                        open: n(r, "listen_trigger_open") as i64,
                        entry: n(r, "send_trigger_entry") as i64,
                        entry_failure: n(r, "send_trigger_entry_failure") as i64,
                        was_inside: false,
                    };
                    // weapons 0..5 in the weapon table's order, the one special (+0x468), ticker bombs, the health pool
                    let sells = [("has_rifle", "rifle"), ("has_snipergun", "snipergun"), ("has_machinegun", "machinegun"), ("has_gurner", "rlgurner"), ("has_sharkgun", "sharkgun"), ("has_vindaloo", "rlvindaloo")]
                        .iter()
                        .filter(|(k, _)| n(r, k) != 0.0)
                        .map(|(_, w)| *w)
                        .collect();
                    let special = [("has_topsyturvy", "topsy"), ("has_guyfawkes", "guyfawkes"), ("has_bunchoffives", "bof"), ("has_blackhole", "blackhole"), ("has_stickybomb", "stickybomb")]
                        .iter()
                        .find(|(k, _)| n(r, k) != 0.0)
                        .map(|(_, s)| *s);
                    let info = PubInfo {
                        sells,
                        special,
                        has_tickerbomb: n(r, "has_tickerbomb") != 0.0,
                        pool: n(r, "health_pool") as f32,
                        pool_full: n(r, "health_pool") as f32,
                        bar_rate: if n(r, "health_bar_rate") > 0.0 { n(r, "health_bar_rate") as f32 * 0.01 } else { 0.5 },
                        refill: r.get("should_refill_ammo").and_then(|v| v.as_f64()).is_none_or(|v| v != 0.0),
                    };
                    pubs.push(Pub {
                        id: i.id,
                        door: door.unwrap_or(pos),
                        radius: n(r, "doorway_entry_radius").max(1.0) as f32,
                        exit: n(r, "send_trigger_exit") as i64,
                        triggers,
                        done: false,
                        info,
                    });
                }
                // a troop generator: a building whose set has troop counts (BuildingObject 0x0040ad80: grunt_barracks_*,
                // the defence levels' grunt_generator_base)
                _ if i.template.starts_with("grunt_barracks") || r.get("num_regular").is_some() => {
                    // 0x0040ad80: the slots from the model's matrices; a barracks with an exit slot ("ref_exit_0%d")
                    // releases troops created at "ref_spawn_0%d" (lev_1_m1's mini barracks have one of each)
                    // the matrices are the template's anim slot's (invisible_barracks: model "nothing", its spawn and
                    // exit in grunt_generator_base; lev_4_m2 284 releases from there)
                    let reference = templates.get(&i.template).and_then(|t| t.default_anim.clone()).or_else(|| i.model.clone());
                    let spawn = reference.as_ref().zip(i.transform.as_ref()).and_then(|(anim, t)| {
                        let m = crate::scene::game_matrix(t);
                        let w = |b: &str| pub_bone(dir, anim, b).map(|l| game_to_engine() * m.transform_point3(l));
                        w("ref_spawn_00").zip(w("ref_exit_00"))
                    });
                    let counts = ["num_bow", "num_regular", "num_jumper_captain", "num_captain", "num_rocket_captain", "num_monk"].map(|k| n(r, k) as i64);
                    // release times: defaults 1 / 2, also when either is <= 0 ("%s has bad release times, setting
                    // defaults"); the first delay is rolled at set-up
                    let (mut lo, mut hi) = (r.get("release_time_min").and_then(|v| v.as_f64()).unwrap_or(1.0) as f32, r.get("release_time_max").and_then(|v| v.as_f64()).unwrap_or(2.0) as f32);
                    if lo <= 0.0 || hi <= 0.0 {
                        (lo, hi) = (1.0, 2.0);
                    }
                    let int = |k: &str, d: i64| r.get(k).and_then(|v| v.as_f64()).map(|v| v as i64).unwrap_or(d);
                    barracks.push(Barracks {
                        id: i.id,
                        counts,
                        initial: counts,
                        batch: 0,
                        amount: int("release_amount", 3) as u32,
                        count: int("release_count", 0) as u32,
                        timer: crt.r(lo, hi),
                        release: (lo, hi),
                        active: false,
                        paced: n(r, "base_defend") != 0.0,
                        activate: n(r, "receive_trigger_activate") as i64,
                        deactivate: n(r, "receive_trigger_deactivate") as i64,
                        reset: n(r, "trigger_reset") as i64,
                        alert_group: n(r, "alert_group") as i64,
                        marker_tags: (n(r, "marker_tag_min") as i64, n(r, "marker_tag_max") as i64),
                        priority_tags: (n(r, "priority_target_tag_min") as i64, n(r, "priority_target_tag_max") as i64),
                        spawn,
                        health: None,
                        pending: false,
                    })
                }
                _ => {}
            }
        }
        let m = &f.mission.resolved;
        let text = |k: &str| s(m, k).and_then(|t| f.text.get(&t).cloned());
        // MissionObject_Load 0x0044c170: start_in_turret_tag starts the level in a turret (defence.rs) and
        // camera_startup_path_anim on a camera path (slot 8 0x0044d150, Camera_StartPath mode 2: campath.rs startup).
        // (Slot 9 0x0044d1b0 resets the mission timer +0x464 and the start flags: `start` below, from the level's start.)
        let log = Vec::new();
        let startup_path = s(m, "camera_startup_path_anim");
        Some(Mission {
            startup_path,
            patience: 0.0,
            level: f.level.clone(),
            receivers,
            death_senders,
            triggers,
            sites,
            counters,
            timers,
            timer_display: None,
            camfx,
            pubs,
            entered_pub: None,
            pub_request: false,
            pub_exit_pending: false,
            barracks,
            hints: f.hints.into_iter().filter_map(|(k, h)| h.text.map(|t| (k, t))).collect(),
            tags,
            untagged: HashSet::new(),
            compass_tag: n(m, "compass_objective_tag") as i64,
            ai_target_tag: n(m, "ai_target_tag") as i64,
            ai_max_fire: n(m, "ai_max_fire").max(0.0) as u32,
            army_max_scale: n(m, "army_max_scale").max(0.0) as u32,
            start_trigger: n(m, "send_trigger_start") as i64,
            start_text: text("start_text"),
            success_text: text("success_text"),
            success: n(m, "receive_trigger_success") as i64,
            pre_success: n(m, "receive_trigger_pre_success") as i64,
            failures: ["receive_trigger_failure", "receive_trigger_failure_2", "receive_trigger_failure_3", "receive_trigger_failure_4"].map(|k| n(m, k) as i64),
            failure_texts: ["failure_text", "failure_text_2", "failure_text_3", "failure_text_4"].map(text),
            alarm_triggers: (n(m, "alarm_trigger_activate") as i64, n(m, "alarm_trigger_deactivate") as i64),
            breach_triggers: (n(m, "army_breach_open_trigger") as i64, n(m, "army_breach_close_trigger") as i64),
            peasant_rules: PeasantRules {
                max_casualties: n(m, "peasant_max_casualties").max(0.0) as u32,
                max_saved: n(m, "peasant_max_saved").max(0.0) as u32,
                saved_trigger: n(m, "peasant_saved_trigger") as i64,
                success_trigger: n(m, "peasant_success_trigger") as i64,
            },
            timeout_text: f.text.get(&s(m, "timeout_text").unwrap_or_else(|| "ui_timeout".into())).cloned(),
            alarm_forced: false,
            breach_open: false,
            start_in_turret: n(m, "start_in_turret_tag") != 0.0,
            base_areas: ["1", "2"].map(|i| ["base_x_min_", "base_y_min_", "base_x_max_", "base_y_max_"].map(|k| n(m, &format!("{k}{i}")) as f32)),
            finished: false,
            base_turret_mounted: false,
            failure_mob_calls: 0,
            failure_mob: false,
            score_value: n(m, "score_value") as f32,
            next_level: s(m, "next_level_name"),
            loadout: Loadout::from_props(m),
            failure_text: text("failure_text"),
            texts: f.text,
            started: false,
            start_sent: false,
            queue: Vec::new(),
            showing: None,
            // player_has_tickerbomb (MissionObject_Load 0x0044c170, game_02.cpp:1994: Game byte +0xd90, the flag the
            // ticker prompt 0x00467450 and the sites test; the pub's award sets the same byte): lev_3_m1 / lev_4_m1
            // start with ticker bombs. The remaster counts them as the pub's award does (pubscreen.rs: 4)
            tickers: if n(m, "player_has_tickerbomb") != 0.0 { 4 } else { 0 },
            outcome: Outcome::Playing,
            dead_timer: 0.0,
            log,
            objectives: String::new(),
            objectives_raw: String::new(),
            objectives_id: String::new(),
            ears_done: Default::default(),
            feed: Vec::new(),
            resend: Vec::new(),
        })
    }

    /// A message for the in-level feed (Chat_AddMessage 0x0046aa60: hud.rs wraps it into the three chat lines, the
    /// oldest scrolling off after 6 s), from a text id or a literal.
    pub fn feed(&mut self, text_or_id: &str) {
        let t = self.texts.get(text_or_id).cloned().unwrap_or_else(|| text_or_id.to_owned());
        // the chat colour table's entry 1, #1fffff (0x0046a7e0; pickups use it, 0x00472310; measured on the original's
        // capture, frame 815)
        self.feed.push((1, long_form(&t)));
    }

    pub(crate) fn hint(&self, key: &str) -> String {
        self.hints.get(key).cloned().unwrap_or_default().replace("%s", &crate::input::key(crate::input::Act::Use))
    }

    pub fn message(&mut self, text: &str, delay: f32) {
        self.queue.push((delay, long_form(text)));
    }

    /// The nearest object of the compass tag (engine space; ui_compass_objective 0x00466790 walks the live objects
    /// whose tag +0x390 equals Game+0x2658).
    pub fn compass_target(&self, from: Vec3) -> Option<Vec3> {
        self.tags.get(&self.compass_tag)?.iter().filter(|(id, _)| !self.untagged.contains(id)).map(|(_, p)| *p).min_by(|a, b| a.distance(from).total_cmp(&b.distance(from)))
    }

    /// The placed objects that carry `tag` and have not lost it (dead, armed): the object list's tag search
    /// (0x004592e0) as the AI's priority targets walk it.
    pub fn tagged(&self, tag: i64) -> impl Iterator<Item = u32> + '_ {
        self.tags.get(&tag).into_iter().flatten().map(|(i, _)| *i).filter(|i| !self.untagged.contains(i))
    }

    /// Whether the placed object `id` has died or lost its tag (the lists the compass and the AI's tag searches walk).
    pub fn lost(&self, id: u32) -> bool {
        self.untagged.contains(&id)
    }

    /// A pub's doorway (engine space), by the pub's instance id (the playtest's land_by stands there).
    pub fn pub_door(&self, id: u32) -> Option<Vec3> {
        self.pubs.iter().find(|p| p.id == id).map(|p| p.door)
    }

    /// Whether the placed object `id` carries `tag` (+0x390, Object_GetTag 0x00453290) and has not lost it.
    pub fn has_tag(&self, id: u32, tag: i64) -> bool {
        !self.untagged.contains(&id) && self.tags.get(&tag).is_some_and(|l| l.iter().any(|(i, _)| *i == id))
    }

    /// The trigger volume of instance `id`, while it lives.
    pub(crate) fn volume(&self, id: u32) -> Option<&Trigger> {
        self.triggers.iter().find(|t| t.id == id)
    }

    /// A volume's object is gone (killed: Object_Kill 0x00455640 removes it, vt+0xdc): it sends nothing more.
    pub(crate) fn remove_volume(&mut self, id: u32) {
        self.triggers.retain(|t| t.id != id);
    }

    /// The level lost through 0x0044a580(Game+0xa64, `state`) from an object's own code (the peasant casualties:
    /// state 8, 0x004062a0): failed while playing, with the state's text (0x0044a390: Game+0x2654 + state x 4; state 8
    /// is Game+0x2674, timeout_text).
    pub fn fail_state(&mut self, state: u32, why: &str) {
        if self.outcome != Outcome::Playing {
            return;
        }
        self.outcome = Outcome::Failed;
        if state == 8 {
            self.failure_text = self.timeout_text.clone();
        }
        self.log.push(format!("FAILED ({why}, state {state})"));
    }

    /// The mission object's trigger handler MissionObject_OnTrigger 0x0044d600 for the triggers it reads into the Game
    /// object: pre-success (+0x470: Game+0xdd2 = 1; the teammates' retreat runs only in start_in_turret levels,
    /// 0x0044e600), failures (+0x474..+0x480, level state 4..7 while not finished), the army breach (Game+0xc3c /
    /// +0xc40), the alarm switch (Game+0x26d8 / +0x26dc). The success trigger is the Win edge (dispatch).
    fn mission_trigger(&mut self, t: i64) {
        if t == 0 {
            return;
        }
        if t == self.pre_success {
            self.finished = true;
            self.log.push("pre-success: mission finished (failure triggers ignored)".into());
            return;
        }
        if let Some(k) = self.failures.iter().position(|&f| f == t) {
            if self.finished || self.outcome != Outcome::Playing {
                return;
            }
            // in the base turret (Game+0xac8 = 1) the failure mob comes instead (0x0049ad60: defence.rs), which sets
            // Game+0xdcd; the turret's loss then fails the level
            if self.base_turret_mounted {
                self.failure_mob_calls += 1;
                self.failure_mob = true;
                self.log.push(format!("failure trigger {t}: the failure mob boards the base turret"));
                return;
            }
            // 0x0044a580(Game+0xa64, 4 + k); 0x0044a390 then pushes "MissionFailed" with failure_text(_N)
            self.outcome = Outcome::Failed;
            self.failure_text = self.failure_texts[k].clone().or_else(|| self.failure_text.clone());
            self.log.push(format!("FAILED (failure trigger {t}, state {})", 4 + k));
            return;
        }
        // (peasant_saved_trigger, Game+0x26f0: peasant.rs banks the peasants at the mat that sent it)
        if t == self.breach_triggers.0 && !self.finished {
            self.breach_open = true;
        } else if t == self.breach_triggers.1 && !self.finished {
            self.breach_open = false;
        }
        if t == self.alarm_triggers.0 {
            self.alarm_forced = true;
            self.log.push("alarm forced on".into());
        } else if t == self.alarm_triggers.1 {
            self.alarm_forced = false;
            self.log.push("alarm forced off".into());
        }
    }
}

pub struct MissionPlugin;

impl Plugin for MissionPlugin {
    fn build(&self, app: &mut App) {
        // the mission runs once play begins (not behind the title menu); paused messages keep the state Playing
        app.add_message::<SendTrigger>()
            .add_message::<VolumeSent>()
            .init_level_resource::<CompassList>()
            .add_systems(Update, (start, volumes, lock_sites, sites, pub_door, barracks, timers, dispatch, messages, outcome).chain().run_if(in_state(crate::AppState::Playing)))
            .add_systems(Update, (event_log, state_log))
            // after the deaths of the frame (fx.rs), before the husk swap (level.rs husks: the Husk marker)
            .add_systems(Update, deaths.after(crate::fx::FxSet).run_if(in_state(crate::AppState::Playing)))
            .add_systems(Update, troop_deaths.run_if(in_state(crate::AppState::Playing)));
    }
}

/// An object with send_trigger_on_death died: its trigger goes out.
/// A grunt died (placed or made during play): its DeathTrigger goes out (once: the component is removed).
pub(crate) fn troop_deaths(mut commands: Commands, q: Query<(Entity, &DeathTrigger, &Grunt)>, mut out: MessageWriter<SendTrigger>) {
    for (e, t, g) in &q {
        if g.mind == crate::ai::Mind::Dead {
            out.write(SendTrigger(t.0));
            commands.entity(e).remove::<DeathTrigger>();
        }
    }
}

pub(crate) fn deaths(mut m: Option<ResMut<Mission>>, mut died: MessageReader<crate::fx::Died>, ids: Query<&InstanceId>, husks: Query<(), With<crate::level::Husk>>, mut out: MessageWriter<SendTrigger>) {
    let Some(m) = m.as_deref_mut() else {
        died.clear();
        return;
    };
    for d in died.read() {
        // a husk dying (the houses' later stages) is a new object, not the placed one (level.rs Husk)
        if husks.contains(d.0) {
            continue;
        }
        if let Ok(i) = ids.get(d.0) {
            // a dead object leaves the lists the compass walks
            m.untagged.insert(i.0);
            // a dead volume (a door mat banked its peasant) sends nothing more
            m.remove_volume(i.0);
            // a destroyed barracks releases no more troops (the building is replaced by its husk)
            let mut stopped = false;
            for b in m.barracks.iter_mut().filter(|b| b.id == i.0 && (b.left() > 0 || b.active)) {
                b.counts = [0; 6];
                b.active = false;
                stopped = true;
            }
            if stopped {
                m.log.push(format!("barracks {} destroyed: no more troops", i.0));
            }
        }
        if let Some(t) = ids.get(d.0).ok().and_then(|i| m.death_senders.get(&i.0).copied()) {
            m.log.push(format!("object {} died -> trigger {t}", ids.get(d.0).map(|i| i.0).unwrap_or(0)));
            out.write(SendTrigger(t));
        }
    }
}

/// The session event log (play.bat keeps the output): every mission event as it happens, with the game time.
fn event_log(m: Option<Res<Mission>>, time: Res<Time>, mut shown: crate::levelreset::PerLevel<usize>) {
    let Some(m) = m else { return };
    let shown = shown.get();
    for e in m.log.iter().skip(*shown) {
        info!("EVENT {:.2} mission: {}", time.elapsed_secs(), e);
    }
    *shown = m.log.len();
}

/// The playtest state log's "mission_obj": the logic objects' state (volumes, sites, counters, camera fx, the pub,
/// the mission's flags), for expect steps.
fn state_log(m: Option<Res<Mission>>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let (Some(m), Some(mut state)) = (m, state) else { return };
    let vols: Map<String, Value> = m.triggers.iter().map(|t| (t.id.to_string(), serde_json::json!({"on_enter": t.on_enter, "on_exit": t.on_exit, "enabled": t.enabled, "pending": t.pending, "inside": t.inside.len()}))).collect();
    let sites: Map<String, Value> = m
        .sites
        .iter()
        .map(|s| {
            // the bomb's position in game space (it moves with a lock parent) and whether it rides one
            let g = crate::level::game_to_engine().inverse() * s.pos;
            let pos = [g.x, g.y, g.z].map(|v| (v * 10.0).round() / 10.0);
            (s.id.to_string(), serde_json::json!({"enabled": s.enabled(), "armed": s.armed.is_some(), "done": s.done, "tagged": !m.untagged.contains(&s.id), "alpha": s.shown, "pos": pos, "locked": s.locked_to.is_some()}))
        })
        .collect();
    let counters: Map<String, Value> = m.counters.iter().map(|c| (c.id.to_string(), serde_json::json!({"current": c.current, "off": c.off}))).collect();
    let camfx: Map<String, Value> = m.camfx.iter().map(|c| (c.id.to_string(), serde_json::json!({"running": c.holder.is_some(), "gone": c.gone}))).collect();
    state.0.insert(
        "mission_obj".into(),
        serde_json::json!({
            "volumes": vols, "sites": sites, "counters": counters, "camfx": camfx,
            // the first pub (scripts written for one), then every pub by id: open, entered, doorway (game x, y) and its
            // entry radius
            "pub_open": m.pubs.first().is_some_and(|p| p.triggers.open == 0), "pub_done": m.pubs.first().is_some_and(|p| p.done),
            "pubs": m.pubs.iter().map(|p| (p.id.to_string(), serde_json::json!({"open": p.triggers.open == 0, "done": p.done, "door": [p.door.x.round(), (-p.door.z).round(), p.radius]}))).collect::<Map<String, Value>>(),
            "finished": m.finished, "alarm_forced": m.alarm_forced, "ears_done": m.ears_done.len(), "compass_target": m.tags.get(&m.compass_tag).map(|v| v.iter().filter(|(id, _)| !m.untagged.contains(id)).map(|(id, _)| id.to_string()).collect::<Vec<String>>().join(",")), "objectives_id": m.objectives_id,
            "queued": m.queue.len(), "showing": m.showing.is_some(), "failure_text": m.failure_text.clone().unwrap_or_default().chars().take(40).collect::<String>(),
        }),
    );
}

/// Frames the remaster's pipeline takes from play start until a camera path that the level's camera fx start at load
/// is playing: start_camera_fx -> the fx VM's first step (op 0x73) -> campath, one frame each at most. The original
/// runs that inside its first frame, so its start text shows on the load frame unless a cinematic plays (oracle B10).
const START_PATH_LATENCY: u32 = 2;

fn start(
    mut m: Option<ResMut<Mission>>,
    mut out: MessageWriter<SendTrigger>,
    path: Res<crate::campath::CameraPath>,
    turrets: Query<&crate::defence::BaseTurret>,
    mut waited: crate::levelreset::PerLevel<u32>,
) {
    let Some(m) = m.as_deref_mut() else { return };
    let waited = waited.get();
    if m.started {
        return;
    }
    // MissionObject_Update 0x0044db80: send_trigger_start is the pending trigger +0x460, sent on its first update
    if !m.start_sent {
        m.start_sent = true;
        if m.start_trigger != 0 {
            out.write(SendTrigger(m.start_trigger));
        }
    }
    // MissionObject_ShowStart 0x0044daf0, tried every update until it runs: once no cinematic plays (Game byte +0xdce:
    // a camera path, or the base turret's mount intro on the defence levels, 279-399 frames), the start text as a
    // paused message
    *waited += 1;
    let cinematic = path.active() || turrets.iter().any(|t| t.in_intro());
    if cinematic || *waited <= START_PATH_LATENCY {
        return;
    }
    m.started = true;
    if let Some(t) = m.start_text.clone() {
        m.objectives = long_form(&t);
        m.objectives_raw = t.clone();
        m.objectives_id = "start_text".into();
        m.message(&t, 0.0);
    }
    m.log.push("start".into());
}

/// Trigger volumes: the player (and, with ai_can_trigger, AI) entering or leaving (the contact callback 0x004976a0),
/// then each volume's update (0x00497640) sends a pending on_enter. Neither runs while the game is paused (a paused
/// message, the pub): the contacts come from the physics step and the update from the object update, which a pause
/// stops (the original's trace: trigger 98 at the courtyard warp comes 2 frames after the briefing is dismissed).
fn volumes(
    vtime: Res<Time<Virtual>>,
    mut m: Option<ResMut<Mission>>,
    player: Query<(Entity, &GlobalTransform), With<Player>>,
    grunts: Query<(Entity, &GlobalTransform, &Grunt)>,
    companions: Query<(Entity, &GlobalTransform, &crate::ai::Companion)>,
    mut out: MessageWriter<SendTrigger>,
    mut from: MessageWriter<VolumeSent>,
) {
    let Some(m) = m.as_deref_mut() else { return };
    let mut bodies: Vec<(Entity, Vec3, bool)> = player.iter().map(|(e, t)| (e, t.translation(), false)).collect();
    bodies.extend(grunts.iter().filter(|(_, _, g)| g.mind != Mind::Dead).map(|(e, t, _)| (e, t.translation(), true)));
    bodies.extend(companions.iter().filter(|(_, _, c)| c.dead.is_none()).map(|(e, t, _)| (e, t.translation(), true)));
    let mut logs = Vec::new();
    for t in std::mem::take(&mut m.resend) {
        out.write(SendTrigger(t));
    }
    if vtime.is_paused() {
        return;
    }
    for t in &mut m.triggers {
        let mut sends = Vec::new();
        let present: HashSet<Entity> = bodies.iter().map(|b| b.0).collect();
        // gone (died, removed) while inside: it leaves
        let left: Vec<Entity> = t.inside.iter().copied().filter(|e| !present.contains(e)).collect();
        for e in left {
            t.inside.remove(&e);
            sends.extend(t.contact(false));
        }
        for &(e, p, ai) in &bodies {
            // collision group 8 (the player) or 0x18 with ai_can_trigger (0x004975d0)
            if ai && !t.ai {
                continue;
            }
            let inside = t.volume.contains(p + Vec3::Y * 5.0);
            let was = t.inside.contains(&e);
            if inside != was {
                if inside {
                    t.inside.insert(e);
                } else {
                    t.inside.remove(&e);
                }
                sends.extend(t.contact(inside));
            }
        }
        sends.extend(t.update());
        for s in sends {
            logs.push(format!("volume {} -> trigger {s}", t.id));
            out.write(SendTrigger(s));
            from.write(VolumeSent { volume: t.id, trigger: s });
        }
    }
    m.log.extend(logs);
}

/// Ticker bomb sites locked to a moving object (PlayerTickerBomb_AttachToParent 0x0044e120, run for every object
/// after the level's load: 0x0045d9ef): a site with a lock_parent_tag rides the lock_parent_bone of the object of that
/// tag (Object_FindByTag 0x004592e0, then Object_Attach 0x004539f0 with the bomb's own origin on the bone: no offset,
/// no child bone). lev_3_m1's sites 173..175 and lev_4_m1's 330 / 367 / 368 ride the "ref_ticker" bone of their
/// squads' goliaths (tags 2001..2003, 2110..2112): the oracle's trace of Object_Attach on lev_3_m1 shows a parent
/// without an instance id, the goliath the squad made, not squad #170 of the same tag (ticker_l3m1 scenario). The
/// goliath is made after the load (goliath.rs), so a site looks for it until it is there. Once attached, the site's
/// position is the bomb's (the prompt's range, the blast). A parent that dies leaves the bomb where it was (guess: the
/// parent object stops updating; not traced).
pub(crate) fn lock_sites(
    mut commands: Commands,
    mut m: Option<ResMut<Mission>>,
    bombs: Query<(Entity, &InstanceId, &GlobalTransform)>,
    parents: Query<(Entity, &crate::level::ObjectTag, &crate::skin::Animator, Option<&crate::fx::Destructible>)>,
    globals: Query<&GlobalTransform>,
) {
    let Some(m) = m.as_deref_mut() else { return };
    // alive: not dying (a goliath's death starts its husk swap, which takes its bones and what hangs on them)
    let alive = |d: Option<&crate::fx::Destructible>| d.is_some_and(|d| d.dying.is_none() && d.health > 0.0);
    let mut logs = Vec::new();
    for s in m.sites.iter_mut().filter(|s| s.lock_parent_tag != 0 && !s.done) {
        let Some((bomb, _, at)) = bombs.iter().find(|(_, i, _)| i.0 == s.id) else { continue };
        match s.locked_to {
            None => {
                let bone = parents.iter().filter(|(_, t, _, d)| t.0 == s.lock_parent_tag && alive(*d)).find_map(|(e, _, a, _)| {
                    let joint = a.skeleton.index.get(s.lock_parent_bone.as_str()).and_then(|&i| a.joints.get(i).copied())?;
                    Some((e, joint))
                });
                let Some((parent, joint)) = bone else { continue };
                commands.entity(bomb).insert(Transform::IDENTITY);
                commands.entity(joint).add_child(bomb);
                s.locked_to = Some(parent);
                logs.push(format!("ticker site {} locked to tag {} at {}", s.id, s.lock_parent_tag, s.lock_parent_bone));
            }
            Some(parent) => {
                if parents.get(parent).is_ok_and(|(_, _, _, d)| alive(d)) {
                    s.pos = at.translation();
                } else {
                    // the parent died: the bomb stays where it was
                    let here = globals.get(bomb).map(|g| g.compute_transform()).unwrap_or(Transform::from_translation(s.pos));
                    commands.entity(bomb).remove_parent_in_place().insert(here);
                    s.lock_parent_tag = 0;
                    logs.push(format!("ticker site {}: its parent died, the bomb stays", s.id));
                }
            }
        }
    }
    m.log.extend(logs);
}

/// Ticker bomb sites (PlayerTickerBomb_Update 0x0044e150 and the prompt 0x00467450): in range with ticker bombs,
/// "Hold down %s to set the ticker bomb!"; Use held for arm_time arms it (0x0044e4a0: send_trigger_set, its tag
/// cleared, alpha 1, a 6 s fuse Game+0x25e4), then it explodes (area damage at the bomb), sends send_trigger_explode
/// and is removed (vt+0xd4).
#[allow(clippy::too_many_arguments)]
pub(crate) fn sites(
    time: Res<Time>,
    ctl: Res<crate::input::Controls>,
    bot: Res<crate::bot::Bot>,
    mut m: Option<ResMut<Mission>>,
    player: Query<&GlobalTransform, With<Player>>,
    (mut out, mut blast, mut fx): (MessageWriter<SendTrigger>, MessageWriter<Explode>, MessageWriter<StartFx>),
    mut hud: ResMut<crate::hud::HudState>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut stop: MessageWriter<crate::audio::StopSound>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut commands: Commands,
    placed: Query<(Entity, &InstanceId, Option<&crate::level::Placed>)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    stats: Res<crate::player::PlayerStats>,
) {
    let (Some(m), Ok(p)) = (m.as_deref_mut(), player.single()) else { return };
    let dt = time.delta_secs();
    let p = p.translation();
    let mut prompt = None;
    // (the Use handler waits for PlayerCanAct, 0x0047a950: player.rs can_act)
    let holding = (ctl.down(crate::input::Act::Use) || bot.use_key) && stats.can_act;
    let tickers = m.tickers;
    // Game+0xdcf: a ticker bomb is armed and ticking (one at a time)
    let any_armed = m.sites.iter().any(|s| s.armed.is_some() && !s.done);
    // Use acts on one site: the nearest enabled, unarmed one in range (the Use handler 0x004788a0, game_06.cpp:2259-2300,
    // keeps the closest bomb of the list Game+0xa84 +0xeb0 and starts only its wind-up, 0x0044e3a0). Two goliaths
    // walking side by side (lev_3_m1 squads 171 / 172) put two sites in range at once
    let chosen = m.sites.iter().filter(|s| !s.done && s.armed.is_none() && s.enabled() && p.distance(s.pos) <= s.radius).min_by(|a, b| p.distance(a.pos).total_cmp(&p.distance(b.pos))).map(|s| s.id);
    let mut sends = Vec::new();
    let mut logs = Vec::new();
    let mut untag = Vec::new();
    let mut arming = None;
    let mut clock = None;
    let mut patience = false;
    // the wind-up (0x0044e3a0 starts wpn_ticker_windup_lp, 0x0044e430 stops it when Use is let go)
    // (the bank's sequence plays wpn_ticker_windup_start before the loop and wpn_ticker_windup_end on its stop)
    let wind_down = |stop: &mut MessageWriter<crate::audio::StopSound>, _sounds: &mut MessageWriter<crate::audio::PlaySound>, _at: Vec3| {
        stop.write(crate::audio::StopSound("wpn_ticker_windup_lp".into()));
    };
    for s in &mut m.sites {
        if s.done {
            continue;
        }
        // the bomb object's alpha (vt+0x100): unarmed_falloff at load (0x0044de90), 1.0 once armed (0x0044e4a0)
        let alpha = if s.armed.is_some() { 1.0 } else { s.falloff };
        if s.shown != Some(alpha) {
            if let Some((e, _, _)) = placed.iter().find(|(_, i, _)| i.0 == s.id) {
                commands.entity(e).insert(crate::camfade::ObjectAlpha(alpha));
                s.shown = Some(alpha);
            }
        }
        if let Some(fuse) = s.armed.as_mut() {
            *fuse -= dt;
            // ui_clock plays from the arming (g+0x25e4 = 6 s)
            clock = Some(6.0 - fuse.max(0.0));
            if *fuse <= 0.0 {
                s.done = true;
                clock = None;
                blast.write(Explode {
                    at: s.pos,
                    area_damage: s.area_damage,
                    radius_inner: s.radius_inner,
                    radius_outer: s.radius_outer,
                    impact_force: s.impact_force,
                    source: None,
                    delay: 0.0,
                    // 0x0047f020 with no source object (stack: 0, &pos, 0, ...); planted by the player (guess: the
                    // record's flags are not traced)
                    by: crate::fx::Src::Player,
                    cartoon: false,
                    kind: crate::player::DamageKind::Plain,
                });
                fx.write(StartFx { name: "fx_ticker_bomb".into(), at: s.pos, rotation: Quat::IDENTITY, follow: None, position_only: false });
                if s.explode != 0 {
                    sends.push(s.explode);
                }
                // vt+0xd4(0): the bomb object goes
                for (e, i, pl) in &placed {
                    if i.0 == s.id {
                        if let Some(c) = pl.and_then(|pl| pl.collider) {
                            physics.remove_static(c);
                        }
                        commands.entity(e).despawn();
                    }
                }
                untag.push(s.id);
                logs.push(format!("ticker site {} exploded -> trigger {}", s.id, s.explode));
            }
            continue;
        }
        if p.distance(s.pos) > s.radius {
            if s.hold > 0.0 {
                wind_down(&mut stop, &mut sounds, s.pos);
            }
            s.hold = 0.0;
            continue;
        }
        if !s.enabled() {
            // the prompt (every 6th frame, 0x00467450) sends send_trigger_non_enabled while the player, holding ticker
            // bombs, stands at a disabled site (PlayerTickerBomb_SendNonEnabled 0x0044e0e0; not a one-shot)
            if tickers > 0 && s.non_enabled != 0 && dt > 0.0 {
                if s.nag % 6 == 0 {
                    sends.push(s.non_enabled);
                }
                s.nag += 1;
            }
            continue;
        }
        if Some(s.id) != chosen {
            if s.hold > 0.0 {
                wind_down(&mut stop, &mut sounds, s.pos);
            }
            s.hold = 0.0;
            continue;
        }
        if tickers == 0 {
            prompt = Some("ui_tickerbomb_none".to_string());
            continue;
        }
        // another bomb ticking: the hint as at any site, and Use raises "Wait for the other Ticker Bomb to explode!"
        // (0x0044e3a0 from the Use handler 0x004788a0)
        if any_armed {
            prompt = Some("ui_tickerbomb_hint".to_string());
            patience |= ctl.pressed(crate::input::Act::Use);
            continue;
        }
        if holding {
            if s.hold == 0.0 {
                sounds.write(crate::audio::PlaySound::at("wpn_ticker_windup_lp", s.pos).looping(true));
            }
            s.hold += dt;
            // the arming bar ui_tickerbomb_set fills while Use is held (it takes the prompt's place): (T - +0x464) /
            // arm_time
            arming = Some((s.hold / s.arm_time.max(0.01)).min(1.0));
            if s.hold >= s.arm_time {
                // 0x0044e4a0: wind-up stops, wpn_ticker and one of Roman's two "armed" lines (50 % each), send_trigger_set
                // (0x0044e0c0), its tag cleared (+0x390 = 0), the clock
                s.armed = Some(6.0);
                arming = None;
                clock = Some(0.0);
                stop.write(crate::audio::StopSound("wpn_ticker_windup_lp".into()));
                sounds.write(crate::audio::PlaySound::at("wpn_ticker", s.pos));
                let line = if rng.rand() < 0.5 { "rom50211_tickerarmed" } else { "rom50212_tickerarmed" };
                sounds.write(crate::audio::PlaySound::at(line, p));
                if s.set != 0 {
                    sends.push(s.set);
                }
                untag.push(s.id);
                logs.push(format!("ticker site {} armed", s.id));
            }
        } else {
            if s.hold > 0.0 {
                wind_down(&mut stop, &mut sounds, s.pos);
            }
            s.hold = 0.0;
            prompt = Some("ui_tickerbomb_hint".to_string());
        }
    }
    for t in sends {
        out.write(SendTrigger(t));
    }
    m.untagged.extend(untag);
    m.log.extend(logs);
    // the patience text (its timed update 0x004677c0: shown 2.0 s from the first visible frame, then hidden; while it
    // shows it holds the other prompts back, Game+0xdd3)
    if patience {
        m.patience = 2.0;
    }
    if m.patience > 0.0 {
        m.patience -= dt;
        prompt = Some("ui_tickerbomb_patience".to_string());
    }
    hud.ticker_arming = arming;
    hud.ticker_clock = clock;
    // the ticker bombs icon (ui_tickerbomb, 0x00467220): while the player holds ticker bombs (Game+0xd90) and sites are
    // left (the list World+0xeb4)
    hud.tickers_left = tickers > 0 && m.sites.iter().any(|s| !s.done);
    hud.prompt = prompt.map(|k| if k.starts_with("ui_") { m.hint(&k) } else { k });
}

/// The pub: near the doorway "Press %s to enter pub" while open, "CLOSED until troops are destroyed" (ui_pub_fail)
/// while its listen_trigger_open has not arrived (0x00467b60, 0x0046a6c0; stepping in sends send_trigger_entry_failure,
/// 0x0046a6d0); Use opens the pub screen (gamemenu.rs) and sends send_trigger_entry once (0x0046a6f0); leaving the
/// screen sends send_trigger_exit (0x0046a720).
fn pub_door(ctl: Res<crate::input::Controls>, bot: Res<crate::bot::Bot>, mut m: Option<ResMut<Mission>>, player: Query<&GlobalTransform, With<Player>>, mut out: MessageWriter<SendTrigger>, mut hud: ResMut<crate::hud::HudState>, stats: Res<crate::player::PlayerStats>) {
    let (Some(m), Ok(p)) = (m.as_deref_mut(), player.single()) else { return };
    if m.pub_exit_pending {
        m.pub_exit_pending = false;
        if let Some(p) = m.entered_pub.and_then(|i| m.pubs.get(i)) {
            let exit = p.exit;
            m.log.push(format!("pub {} screen left -> trigger {exit}", p.id));
            if exit != 0 {
                out.write(SendTrigger(exit));
            }
        }
    }
    let at = p.translation().with_y(0.0);
    // each pub's doorway (0x00467b60 per pub object); Roman stands in at most one
    let mut log = Vec::new();
    let mut prompt = None;
    let mut entered = None;
    for (i, pb) in m.pubs.iter_mut().enumerate() {
        let inside = at.distance(pb.door.with_y(0.0)) <= pb.radius;
        let stepped_in = inside && !pb.triggers.was_inside;
        pb.triggers.was_inside = inside;
        if pb.done || !inside {
            continue;
        }
        if pb.triggers.open != 0 {
            if stepped_in && pb.triggers.entry_failure != 0 {
                log.push(format!("pub {} closed -> trigger {}", pb.id, pb.triggers.entry_failure));
                out.write(SendTrigger(pb.triggers.entry_failure));
            }
            prompt.get_or_insert("ui_pub_fail");
            continue;
        }
        prompt.get_or_insert("ui_pub_entry_hint");
        // (the Use handler waits for PlayerCanAct, 0x0047a950: player.rs can_act)
        if (ctl.pressed(crate::input::Act::Use) || bot.use_key) && stats.can_act {
            pb.done = true;
            entered = Some(i);
            log.push(format!("pub {} entered", pb.id));
            let entry = std::mem::take(&mut pb.triggers.entry);
            if entry != 0 {
                out.write(SendTrigger(entry));
            }
            break;
        }
    }
    m.log.extend(log);
    if let Some(i) = entered {
        m.entered_pub = Some(i);
        m.pub_request = true;
        hud.prompt = None;
    } else if let (Some(k), true) = (prompt, hud.prompt.is_none()) {
        hud.prompt = Some(m.hint(k));
    }
}

/// Barracks release their troops: the generator update 0x0040cb10 (its non-window branch) and the release 0x0040c910.
/// While releasing (+0x460 bit 3): with no batch under way (+0x59c == 0) and at least release_count troops of its list
/// alive the delay is re-rolled (it waits); otherwise it counts down, and a new batch of release_amount starts when
/// fewer than release_count are alive. When the delay runs out one troop of the type with the most remaining comes
/// out of the first free exit slot (created at "ref_spawn_0%d", walking out to "ref_exit_0%d", then to its marker),
/// the batch drops by one, and the delay is re-rolled; none left: it stops (bit 3 cleared). Damage while the
/// activate trigger is still armed starts it too (0x0040ce40). release_max (+0x590) is read but not used here.
fn barracks(
    time: Res<Time>,
    mut m: Option<ResMut<Mission>>,
    // the release delays' random stream: each level starts it over
    mut crt: crate::levelreset::PerLevel<crate::fxvm::CrtRand>,
    troops: Query<(&BarracksTroop, &Grunt)>,
    objects: Query<(&InstanceId, &Destructible)>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
) {
    let Some(m) = m.as_deref_mut() else { return };
    let crt = crt.get();
    let dt = time.delta_secs();
    let mut logs = Vec::new();
    for b in &mut m.barracks {
        if let Some(h) = objects.iter().find(|(i, _)| i.0 == b.id).map(|(_, d)| d.health) {
            if b.activate != 0 && b.health.is_some_and(|o| h < o) {
                b.active = true;
                logs.push(format!("barracks {} damaged: releasing", b.id));
            }
            b.health = Some(h);
        }
        if !b.active {
            continue;
        }
        // the list +0x5a0: its released troops still alive (guess: a dead troop leaves the list)
        if b.pending && troops.iter().any(|(t, g)| t.0 == b.id && g.exit.is_some()) {
            b.pending = false;
        }
        let alive = troops.iter().filter(|(t, g)| t.0 == b.id && g.mind != Mind::Dead).count() as u32 + b.pending as u32;
        if b.paced {
            // 0x0040cb10 with bits 0x14: the delay counts down, whatever is alive
            b.timer -= dt;
        } else {
            if b.batch == 0 && alive >= b.count {
                b.timer = crt.r(b.release.0, b.release.1);
            } else {
                b.timer -= dt;
            }
            if b.batch == 0 && alive < b.count {
                b.batch = b.amount;
            }
        }
        if b.timer > 0.0 {
            continue;
        }
        let ty = b.most();
        if b.counts[ty] <= 0 {
            b.active = false;
            // a base generator refills for its next activation (0x0040c710)
            if b.paced {
                b.counts = b.initial;
            }
            logs.push(format!("barracks {} empty: stops", b.id));
            continue;
        }
        // the first free exit slot: taken while a troop walks out through it (guess at when it is freed)
        let busy = b.pending || troops.iter().any(|(t, g)| t.0 == b.id && g.exit.is_some() && g.mind != Mind::Dead);
        if let (Some((at, exit)), false) = (b.spawn, busy) {
            spawn.write(crate::level::SpawnGrunt {
                at,
                template: TROOP_TEMPLATES[ty].into(),
                alarm: false,
                // type 2 is a jumper captain (0x0040c590 -> 0x00425d30(1))
                jumper: ty == 2,
                barracks: Some(b.id),
                exit: Some(exit),
                marker_tags: (b.marker_tags.0 != 0).then_some(b.marker_tags),
                alert_group: b.alert_group,
                priority_tags: b.priority_tags,
                ..default()
            });
            b.batch = b.batch.wrapping_sub(1);
            b.pending = true;
            b.counts[ty] -= 1;
            logs.push(format!("barracks {} released a {} ({} left, {} of its troops were alive, batch {})", b.id, TROOP_TEMPLATES[ty], b.left(), alive, b.batch));
        }
        b.timer = crt.r(b.release.0, b.release.1);
    }
    m.log.extend(logs);
}

/// Running timers advance (TimerObject_Update 0x00496750) and send their triggers.
impl Mission {
    /// The running timers jump `dt` ahead (the playtest's skip_timers step): the triggers of the entries passed, in
    /// order, as each timer's own advance (Timer_Advance 0x00496690) gives them.
    pub fn skip_timers(&mut self, dt: f32) -> Vec<i64> {
        let mut out = Vec::new();
        for tm in &mut self.timers {
            for t in tm.advance(dt) {
                self.log.push(format!("timer {} -> trigger {t} (skipped ahead)", tm.id));
                out.push(t);
            }
        }
        out
    }
}

fn timers(time: Res<Time>, mut m: Option<ResMut<Mission>>, mut out: MessageWriter<SendTrigger>) {
    let Some(m) = m.as_deref_mut() else { return };
    let dt = time.delta_secs();
    let mut display = None;
    let mut logs = Vec::new();
    for tm in &mut m.timers {
        for t in tm.advance(dt) {
            logs.push(format!("timer {} -> trigger {t}", tm.id));
            out.write(SendTrigger(t));
        }
        if tm.running && !tm.gone && tm.display {
            display = Some(tm.shown());
        }
    }
    m.timer_display = display;
    m.log.extend(logs);
}

#[allow(clippy::too_many_arguments)]
fn dispatch(
    mut m: Option<ResMut<Mission>>,
    mut events: MessageReader<SendTrigger>,
    mut grunts: Query<(&InstanceId, &mut Grunt, Has<crate::ai::ic::IcEntry>)>,
    mut objects: Query<(&InstanceId, &mut Destructible)>,
    // (a squad hands its receive_trigger_kill to its goliath and keeps none: Squad_Init 0x00433220, goliath.rs)
    plain: Query<(Entity, &InstanceId, Option<&crate::level::Placed>), (Without<Destructible>, Without<Grunt>, Without<crate::ai::squad::Squad>)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut commands: Commands,
    player: Query<(Entity, &GlobalTransform), With<Player>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut fx: MessageWriter<StartFx>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut ms: ResMut<crate::stats::MissionStats>,
    mut stop: MessageWriter<crate::audio::StopSound>,
    companions: Query<(Entity, &crate::ai::Companion, &GlobalTransform)>,
    mut music: ResMut<crate::gameaudio::Music>,
    cam: Query<(Entity, &GlobalTransform), With<Camera3d>>,
) {
    let Some(m) = m.as_deref_mut() else { return };
    let pe = player.single().ok().map(|p| p.0);
    let pt = player.single().ok().map(|p| p.1.translation());
    // Object_SendTrigger 0x00461870: every object's trigger handler is offered the id; all lev_1_m1 handlers return
    // false, so it is a broadcast (docs/subsystems/objects.md). Id 0 is never sent ("sent 0 trigger BAD BAD BAD").
    let ids: Vec<i64> = events.read().map(|e| e.0).filter(|t| *t != 0).collect();
    for t in ids {
        let t = &t;
        let actions = m.receivers.get(t).cloned().unwrap_or_default();
        m.log.push(format!("trigger {t}: {} receivers", actions.len()));
        // the barracks' trigger handler 0x0040d1b0: activate starts the release and is used up (+0x534 = 0),
        // deactivate stops it, reset starts it with the troop counts refilled
        let mut blogs = Vec::new();
        let mut m_resend = Vec::new();
        for b in &mut m.barracks {
            if *t == b.activate {
                b.active = true;
                b.activate = 0;
                blogs.push(format!("barracks {} activated", b.id));
            } else if *t == b.deactivate {
                b.active = false;
                blogs.push(format!("barracks {} deactivated", b.id));
            } else if *t == b.reset {
                b.active = true;
                b.counts = b.initial;
                blogs.push(format!("barracks {} reset", b.id));
            }
        }
        // volumes' enable / disable (0x00497720)
        for v in &mut m.triggers {
            v.message(*t);
        }
        // bomb sites: receive_trigger_enable clears itself (PlayerTickerBomb_OnTrigger 0x0044e5e0)
        for s in &mut m.sites {
            if *t == s.enable_trigger {
                s.enable_trigger = 0;
                blogs.push(format!("ticker site {} enabled", s.id));
            }
        }
        // counters (CounterObject_OnTrigger 0x00415860): the count reached sends its trigger at once (re-entrant)
        for c in &mut m.counters {
            if let Some(s) = c.message(*t) {
                blogs.push(format!("counter {} reached {} -> trigger {s}", c.id, c.count));
                m_resend.push(s);
            }
        }
        // timers: the activation trigger starts one (TimerObject_OnTrigger 0x004965d0)
        for tm in &mut m.timers {
            if *t == tm.activate {
                tm.running = true;
                tm.activate = 0;
                blogs.push(format!("timer {} started", tm.id));
            }
        }
        // camera fx (CameraFXObject 0x004101a0 / 0x004102b0): trigger_start starts the fx on the camera,
        // receive_trigger_kill removes the object and its fx
        if let Ok((ce, ct)) = cam.single() {
            for c in &mut m.camfx {
                if c.gone {
                    continue;
                }
                if *t == c.start && c.holder.is_none() {
                    let h = commands.spawn((Transform::default(), Visibility::default(), ChildOf(ce))).id();
                    fx.write(StartFx { name: c.fx.clone(), at: ct.translation(), rotation: Quat::IDENTITY, follow: Some(h), position_only: c.position_only });
                    c.holder = Some(h);
                    blogs.push(format!("camera fx {} started {}", c.id, c.fx));
                }
                if *t == c.kill {
                    if let Some(h) = c.holder.take() {
                        commands.entity(h).despawn();
                    }
                    c.gone = true;
                    blogs.push(format!("camera fx {} killed", c.id));
                }
            }
        }
        // the pub's open trigger (PubObject_OnTrigger 0x0046a770 clears listen_trigger_open)
        for p in m.pubs.iter_mut().filter(|p| p.triggers.open != 0 && p.triggers.open == *t) {
            p.triggers.open = 0;
            blogs.push(format!("pub {} opened", p.id));
        }
        m.log.extend(blogs);
        m.resend.extend(m_resend);
        m.mission_trigger(*t);
        for a in actions {
            match a {
                Action::WakeAi(id) => {
                    for (i, mut g, ic) in &mut grunts {
                        if i.0 == id {
                            crate::ai::activate(&mut g, ic, pe);
                        }
                    }
                }
                Action::ShowText(t, objective) => {
                    // the objective jingle: the victory music request (0x0046afa0 -> 0x0044f560, cl = 1)
                    if objective {
                        music.request_victory(true);
                        m.log.push("music: victory request (objective)".into());
                    }
                    let text = m.texts.get(&t).cloned().unwrap_or(t.clone());
                    m.objectives = long_form(&text);
                    m.objectives_raw = text.clone();
                    m.objectives_id = t;
                    // 0x00403410 flags 3 / 0x13: queued (shown 1 s later), paused, "Press %s to continue"
                    m.message(&text, 1.0);
                }
                Action::Compass(tag) => m.compass_tag = tag,
                Action::Voice { ear, repeat, max_distance, q, jonesy, roman } => {
                    // a removed ear receives nothing
                    if m.ears_done.contains(&ear) {
                        continue;
                    }
                    let Some(pp) = pt else { continue };
                    let r2 = max_distance * max_distance;
                    // candidates (0x00497900): Roman's line always counts; Q (0x00475d40) and Jonesy (0x00475d30)
                    // when present and alive, counted when nearer than max_distance (squared distances)
                    let speaker = |want_q: bool| companions.iter().find(|(_, c, _)| c.q == want_q && c.dead.is_none()).map(|(e, _, t)| (e, t.translation().distance_squared(pp)));
                    let me = roman.is_some();
                    let mut count = u32::from(me);
                    let (mut d1, mut d2, d3) = (1.0e8f32, 1.0e8f32, 1.0e8f32);
                    let (mut t1, mut t2) = (None, None);
                    let (mut near1, mut near2) = (false, false);
                    if q.is_some() {
                        if let Some((e, d)) = speaker(true) {
                            t1 = Some(e);
                            d1 = d;
                            if d1 < r2 {
                                count += 1;
                                near1 = true;
                            }
                        }
                    }
                    if jonesy.is_some() {
                        if let Some((e, d)) = speaker(false) {
                            t2 = Some(e);
                            d2 = d;
                            if d2 < r2 {
                                count += 1;
                                near2 = true;
                            }
                        }
                    }
                    if count == 0 {
                        // nobody at all: the ear removes itself (vt+0xdc, even with reapeat)
                        if t1.is_none() && t2.is_none() {
                            m.ears_done.insert(ear);
                            m.log.push(format!("sound ear {ear}: no speaker, removed"));
                            continue;
                        }
                        // else the nearest one
                        if d1 < d2 && d1 < d3 {
                            near1 = true;
                        } else if d2 < d1 && d2 < d3 {
                            near2 = true;
                        }
                        count = 1;
                    }
                    // (rand() * count) >> 15, the original's branches 0x00497bdc..0x00497cc3 literally
                    let r15 = ((rng.rand() * 32768.0) as i32).min(32767);
                    let pick = (r15 * count as i32) >> 15;
                    let first = if near1 { 1 } else if near2 { 2 } else { 3 };
                    let line = match pick {
                        0 if me => 0,
                        0 => first,
                        1 if me => first,
                        1 => if near1 && near2 { 2 } else { 3 },
                        2 if me => if near1 && near2 { 2 } else { 3 },
                        _ => 3,
                    };
                    // Roman's line in 2D (+0xd0); the others on the speaker (+0xc4, his audio position)
                    let play = match line {
                        0 => roman.map(crate::audio::PlaySound::ui),
                        1 => q.zip(t1).map(|(n, e)| crate::audio::PlaySound { follow: Some(e), at: companions.get(e).ok().map(|c| c.2.translation()), ..crate::audio::PlaySound::ui(n) }),
                        2 => jonesy.zip(t2).map(|(n, e)| crate::audio::PlaySound { follow: Some(e), at: companions.get(e).ok().map(|c| c.2.translation()), ..crate::audio::PlaySound::ui(n) }),
                        _ => None,
                    };
                    if let Some(p) = play.filter(|p| p.name != "none") {
                        m.log.push(format!("sound ear {ear}: {} ({})", p.name, if p.follow.is_some() { "on the speaker" } else { "2d" }));
                        sounds.write(p);
                    }
                    if !repeat {
                        m.ears_done.insert(ear);
                    }
                }
                Action::Fx(name, at) => {
                    m.log.push(format!("fx ear: {name}"));
                    fx.write(StartFx { name, at, rotation: Quat::IDENTITY, follow: None, position_only: false });
                }
                // handled above by the barracks' own trigger ids
                Action::Barracks(_) => {}
                // receive_trigger_kill (DefaultGameObject_OnTrigger 0x00450fb0 -> vt+0xd4, the death slot)
                Action::Destroy(id) => {
                    m.untagged.insert(id);
                    let mut killed = false;
                    for (i, mut d) in &mut objects {
                        if i.0 == id && d.dying.is_none() {
                            d.health = 0.0;
                            d.dying = Some(0.0);
                            // an explosive (FxExplosion_OnTrigger 0x0041c6e0 -> FxExplosion_Die 0x0041c570 with no
                            // record) passes acceleration_rate x start_delay on (fx.rs `dying` uses the chain delay)
                            if d.chain {
                                d.chain_delay = d.start_delay;
                            }
                            killed = true;
                        }
                    }
                    // objects without health (the big red arrow): receive_trigger_kill removes them
                    if !killed {
                        for (e, i, pl) in &plain {
                            if i.0 == id {
                                m.log.push(format!("object {id} removed"));
                                // with its collision (the arrow's body stayed behind, blocking the crosshair)
                                if let Some(c) = pl.and_then(|pl| pl.collider) {
                                    physics.remove_static(c);
                                }
                                commands.entity(e).despawn();
                            }
                        }
                    }
                }
                Action::Win => {
                    // a start-in-turret level ignores the success while the failure mob is on (Game+0xdcd, 0x0044d600)
                    if m.outcome == Outcome::Playing && *t == m.success && !m.failure_mob {
                        m.outcome = Outcome::Won;
                        // MissionObject_OnTrigger 0x0044d600 +0x46c: on lev_1_m1_b and lev_2_m1 Rescues (Game+0x272c)
                        // += 1000; score_value to Loot (+0x2730, game_02.cpp:2262); level state 2
                        if m.level.eq_ignore_ascii_case("lev_1_m1_b") || m.level.eq_ignore_ascii_case("lev_2_m1") {
                            ms.rescues_money += 1000.0;
                        }
                        ms.loot_money += m.score_value;
                        // every sound but the music stops (0x0044a390: audio vt+0xa8(0), 0x004f24e0)
                        stop.write(crate::audio::StopSound(crate::audio::ALL_BUT_MUSIC.into()));
                        let t = m.success_text.clone().unwrap_or("Well Done!".into());
                        m.message(&t, 0.0);
                        m.log.push("WIN".into());
                    }
                }
            }
        }
    }
}

/// Paused messages (0x00403410 flag 1): the game pauses on the message with "Press %s to continue".
fn messages(
    time: Res<Time<Real>>,
    ctl: Res<crate::input::Controls>,
    buttons: Res<ButtonInput<MouseButton>>,
    mut m: Option<ResMut<Mission>>,
    mut hud: ResMut<crate::hud::HudState>,
    mut vtime: ResMut<Time<Virtual>>,
    mut subs: MessageReader<crate::audio::Subtitle>,
    opts: Res<crate::options::GameOptions>,
) {
    let Some(m) = m.as_deref_mut() else { return };
    // speech lines go to the feed as "Speaker: line" (the original adds them to the chat feed Game+0xaac,
    // docs/subsystems/audio.md; seen as "Roman: Oh, I'm freezing." in its captures), with the in-game subtitles
    // option
    for s in subs.read() {
        if !opts.subtitles_game || s.text.is_empty() {
            continue;
        }
        let line = match &s.speaker {
            Some(sp) if !s.text.starts_with(&format!("{sp}:")) => format!("{sp}: {}", s.text),
            _ => s.text.clone(),
        };
        // speech in colour 1 (#1fffff); the lines without a speaker are player.rs's weapon changes
        // ("ui_change_weapon" / "ui_change_special_weapon"), which the original adds in colour 2 (#f0f01f,
        // game_09.cpp:1730)
        if s.speaker.is_none() {
            m.feed.push((2, crate::mission::long_form(&line)));
        } else {
            m.feed(&line);
        }
    }
    // the feed's new messages go to the HUD's chat (hud.rs: wrap, three lines, 6 s scroll)
    hud.chat_in.extend(m.feed.drain(..).map(|(c, t)| (t, c)));
    // "Press %s to continue" names the MenuSelect binding of the device in use (input.rs), kept current while shown
    let page = |m: &Mission, text: &str| {
        let cont = m.hints.get("ui_stats_ok").cloned().unwrap_or("Press %s to continue".into()).replace("%s", &crate::input::key(crate::input::Act::Continue));
        format!("{text}<br><br>{cont}")
    };
    if let Some(text) = m.showing.clone() {
        // (a trackpad tap arrives as press and release in one frame: the raw click counts too)
        if ctl.pressed(crate::input::Act::Continue) || buttons.just_pressed(MouseButton::Left) {
            m.showing = None;
            hud.briefing = None;
            vtime.unpause();
        } else {
            let b = page(m, &text);
            if hud.briefing.as_ref() != Some(&b) {
                hud.briefing = Some(b);
            }
        }
        return;
    }
    let dt = time.delta_secs();
    for q in &mut m.queue {
        q.0 -= dt;
    }
    if let Some(i) = m.queue.iter().position(|q| q.0 <= 0.0) {
        let (_, text) = m.queue.remove(i);
        hud.briefing = Some(page(m, &text));
        m.showing = Some(text);
        vtime.pause();
    }
}

/// Lose: the player dead for 5 s (level state 3, "MissionFailed").
fn outcome(
    time: Res<Time>,
    mut m: Option<ResMut<Mission>>,
    roman: Query<&crate::player::Roman>,
    mut hud: ResMut<crate::hud::HudState>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    player: Query<&GlobalTransform, With<Player>>,
    first: Res<CompassList>,
) {
    let Some(m) = m.as_deref_mut() else { return };
    if let Ok(r) = roman.single() {
        if r.dead && m.outcome == Outcome::Playing {
            m.dead_timer += time.delta_secs();
            if m.dead_timer > 5.0 {
                // the mission failed page (gamemenu.rs) shows the failure text
                m.outcome = Outcome::Failed;
                m.log.push("FAILED".into());
            }
        }
    }
    // compass objective: bearing of the target relative to the camera, and its distance
    if let (Ok(c), Ok(p)) = (cam.single(), player.single()) {
        // 0x00466790: from the camera (Camera_GetPosition), the distance in 3D (the pointer's anim frame)
        // the first list's nearest (World+0xefc: the peasants still to pick up), the tagged objective only when
        // strictly nearer
        let from = p.translation();
        let listed = first.0.iter().copied().min_by(|a, b| a.distance(from).total_cmp(&b.distance(from)));
        let target = match (listed, m.compass_target(from)) {
            (Some(l), Some(t)) if t.distance(from) < l.distance(from) => Some(t),
            (Some(l), _) => Some(l),
            (None, t) => t,
        };
        hud.objective = target.map(|t| {
            let to = (t - c.translation()).with_y(0.0);
            let f = c.forward();
            let rel = to.x.atan2(-to.z) - f.x.atan2(-f.z);
            (-rel, (t - c.translation()).length())
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The loadout rules of MissionObject_Load / 0x00475650: adds in weapon-id order (the machine gun first, in hand),
    /// the machine gun added by default, at most 3 weapons, set_num -1 gives none, and the topsy count read under
    /// player_set_num_topsyturvy (the template's player_set_num_topsy never applies).
    #[test]
    fn loadout_rules() {
        let m = |v: serde_json::Value| Loadout::from_props(v.as_object().unwrap());
        // lev_1_m1_b
        let l = m(serde_json::json!({"player_add_machinegun": 1, "player_add_rifle": 1, "player_add_snipergun": 0, "player_set_num_stickybomb": 5, "player_set_num_topsy": 2}));
        assert_eq!(l.weapons, vec!["machinegun", "rifle"]);
        assert_eq!((l.special("stickybomb"), l.special("topsyturvy"), l.special("blackhole")), (5, 0, 0));
        assert_eq!(l.ammo["machinegun"], -2);
        // lev_1_m1: the rifle only; nothing named: the machine gun by default
        assert_eq!(m(serde_json::json!({"player_add_machinegun": 0, "player_add_rifle": 1})).weapons, vec!["rifle"]);
        assert_eq!(m(serde_json::json!({})).weapons, vec!["machinegun"]);
        // the primary list holds three
        let all = m(serde_json::json!({"player_add_snipergun": 1, "player_add_rifle": 1, "player_add_vindaloo": 1, "player_add_sharkgun": 1}));
        assert_eq!(all.weapons, vec!["machinegun", "snipergun", "rifle"]);
        assert_eq!(m(serde_json::json!({"player_set_num_topsyturvy": 2})).special("topsyturvy"), 2);
    }

    fn volume(props: serde_json::Value) -> Trigger {
        Trigger::new(1, props.as_object().unwrap(), Volume::Sphere(Vec3::ZERO, 10.0))
    }

    #[test]
    fn one_shot_enter_sends_once() {
        let mut t = volume(serde_json::json!({"on_enter": 5, "one_shot": 1}));
        assert_eq!(t.contact(true), None);
        assert_eq!(t.update(), Some(5));
        assert_eq!(t.update(), None);
        t.contact(false);
        t.contact(true);
        assert_eq!(t.update(), None, "one_shot clears on_enter");
        assert!(t.fired);
    }

    #[test]
    fn door_mat_keeps_on_enter() {
        let mut t = volume(serde_json::json!({"on_enter": 5, "one_shot": 1, "door_mat": 1}));
        t.contact(true);
        assert_eq!(t.update(), Some(5));
        t.contact(false);
        t.contact(true);
        assert_eq!(t.update(), Some(5));
    }

    #[test]
    fn disabled_volume_waits_and_on_exit() {
        let mut t = volume(serde_json::json!({"on_enter": 5, "on_exit": 6, "enable": 9, "disable": 8, "one_shot": 1}));
        assert!(!t.enabled, "an enable trigger means it starts disabled");
        t.contact(true);
        assert_eq!(t.update(), None);
        assert!(t.pending, "stays pending while disabled");
        t.message(9);
        assert_eq!(t.update(), Some(5));
        assert_eq!(t.contact(false), Some(6));
        t.contact(true);
        assert_eq!(t.contact(false), None, "one_shot clears on_exit");
        t.message(8);
        assert!(!t.enabled);
    }

    #[test]
    fn leaving_clears_pending() {
        let mut t = volume(serde_json::json!({"on_enter": 5, "enable": 9}));
        t.contact(true);
        t.contact(false);
        t.message(9);
        assert_eq!(t.update(), None);
    }

    #[test]
    fn failure_triggers_and_pre_success() {
        let dir = std::env::temp_dir().join(format!("and_mission_test_{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let j = serde_json::json!({
            "level": "lev_test",
            "mission": {"resolved": {"receive_trigger_failure_2": 77, "failure_text": "f1", "failure_text_2": "f2", "receive_trigger_pre_success": 55, "alarm_trigger_activate": 5, "alarm_trigger_deactivate": 6}},
            "edges": [], "instances": [], "text": {"f1": "First", "f2": "Second"}, "hints": {}
        });
        std::fs::write(dir.join("mission.json"), j.to_string()).unwrap();
        let mut m = Mission::load(&dir).unwrap();
        m.mission_trigger(5);
        assert!(m.alarm_forced);
        m.mission_trigger(6);
        assert!(!m.alarm_forced);
        m.mission_trigger(77);
        assert_eq!(m.outcome, Outcome::Failed);
        assert_eq!(m.failure_text.as_deref(), Some("Second"));
        let mut f = Mission::load(&dir).unwrap();
        f.mission_trigger(55);
        assert!(f.finished);
        f.mission_trigger(77);
        assert_eq!(f.outcome, Outcome::Playing, "failures are ignored once finished");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn timer_fires_in_order_and_ends() {
        let r = serde_json::json!({"send_trigger_time_1": "11/2.0", "send_trigger_time_2": "10/1.0", "send_trigger_time_3": "0/1.5", "send_end_trigger": 12});
        let mut t = Timer::new(1, r.as_object().unwrap());
        assert_eq!(t.entries, vec![(1.0, 10), (2.0, 11)]);
        assert!(t.running);
        assert_eq!(t.advance(0.5), Vec::<i64>::new());
        assert_eq!(t.advance(0.6), vec![10]);
        assert_eq!(t.advance(1.0), vec![11, 12]);
        assert!(t.gone);
        let r = serde_json::json!({"send_trigger_time_1": "5/1.0", "loop": 1, "receive_trigger_activate": 3, "countdown_time": 10.0});
        let mut l = Timer::new(2, r.as_object().unwrap());
        assert_eq!(l.advance(5.0), Vec::<i64>::new(), "waits for its trigger");
        l.running = true;
        assert_eq!(l.advance(1.2), vec![5]);
        assert!((l.elapsed - 0.2).abs() < 1e-4, "wraps");
        assert!(!l.gone);
        assert!((l.shown() - 9.8).abs() < 1e-4);
    }

    #[test]
    fn counter_counts_and_finishes() {
        let mut c = Counter { id: 1, activate: 3, deactivate: 4, send: 7, count: 2, infinite: false, current: 0, off: false };
        assert_eq!(c.message(3), None);
        assert_eq!(c.message(3), Some(7));
        assert_eq!(c.message(3), None, "removed after sending");
        let mut i = Counter { infinite: true, off: false, current: 0, ..c.clone() };
        assert_eq!(i.message(3), None);
        assert_eq!(i.message(3), Some(7));
        assert_eq!(i.message(3), None);
        assert_eq!(i.message(3), Some(7), "infinite restarts");
        i.message(4);
        assert_eq!(i.message(3), None);
    }
}
