//! The level's mission logic as the original runs it: numbered triggers between placed objects
//! (docs/subsystems/mission.md; data: tools/remaster_export_mission.py -> mission.json). Senders: trigger volumes
//! (on enter, one-shot or not), the mission (start), ticker bomb sites (explode, not enabled), the pub (exit),
//! deaths. Receivers: ui_ears (objective text as a paused message + compass target), sound_ears (voice line),
//! AI (wake up), barracks (release troops), objects (destroyed), bomb sites (enable), the mission (win).
//! Prompts ("Hold down %s to set the ticker bomb!", "Press %s to enter pub") come from the hint texts with the
//! bound key's name. STAND-INS, labelled where used: the pub screen (a message), the bomb clock (text).

use crate::ai::{Grunt, Mind, Player};
use crate::fx::{Destructible, Explode, StartFx};
use crate::level::{InstanceId, game_to_engine};
use bevy::prelude::*;
use serde::Deserialize;
use serde_json::{Map, Value};
use std::collections::HashMap;

#[derive(Deserialize)]
struct EdgeEnd {
    id: Option<u32>,
    #[serde(default)]
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
    /// the text, and whether the same ui_ear marked an objective on this trigger (objective_trigger_receive_0N with a
    /// non-zero objective_tag_0N: 0x0046afa0 then requests the victory music, 0x0044f560 cl = 1)
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
    EnableSite(u32),
    Win,
}

enum Volume {
    Sphere(Vec3, f32),
    /// engine-space aabb
    Box(Vec3, Vec3),
}

struct Trigger {
    id: u32,
    sends: i64,
    volume: Volume,
    one_shot: bool,
    inside: bool,
    done: bool,
}

struct Site {
    id: u32,
    pos: Vec3,
    radius: f32,
    enabled: bool,
    explode: i64,
    non_enabled: i64,
    armed: Option<f32>,
    hold: f32,
    done: bool,
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
    /// receive_trigger_activate (+0x534; cleared once used, 0x0040d1b0), deactivate (+0x538), trigger_reset (+0x5a4)
    activate: i64,
    deactivate: i64,
    reset: i64,
    /// alert_group (+0x530), marker_tag_min/max (+0x580 / +0x584): handed to every troop (0x0042db50)
    alert_group: i64,
    marker_tags: (i64, i64),
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
    /// health left in its pool, and the pool bar's scale (health_bar_rate)
    pub pool: f32,
    pub bar_rate: f32,
}

/// Mission outcome for the state log.
#[derive(Clone, Copy, PartialEq, Debug, Default)]
pub enum Outcome {
    #[default]
    Playing,
    Won,
    Failed,
}

#[derive(Resource)]
pub struct Mission {
    receivers: HashMap<i64, Vec<Action>>,
    /// objects that send a trigger when they die (send_trigger_on_death: DefaultGameObject_OnDeath 0x00450ee0,
    /// FxExplosion_Die game_02.cpp:797): instance id -> trigger (pipe 96 -> 711, boom_island 131 -> 301)
    death_senders: HashMap<u32, i64>,
    triggers: Vec<Trigger>,
    sites: Vec<Site>,
    pub_pos: Option<(Vec3, f32, i64)>,
    pub_done: bool,
    /// the player entered the pub: the pub screen opens (gamemenu.rs)
    pub pub_request: bool,
    /// the pub screen was left: send its exit trigger
    pub pub_exit_pending: bool,
    /// what the pub has (its property set with the instance's overrides)
    pub pub_info: Option<PubInfo>,
    /// where the player stands after the pub screen (the pub's Ref_exit bone, player_exit_bone_name)
    pub pub_exit_pos: Option<Vec3>,
    barracks: Vec<Barracks>,
    texts: HashMap<String, String>,
    hints: HashMap<String, String>,
    tags: HashMap<i64, Vec<Vec3>>,
    pub compass_tag: i64,
    start_trigger: i64,
    start_text: Option<String>,
    success_text: Option<String>,
    /// score_value (Loot at the win, game_02.cpp:2262) and next_level_name (Game+0x268c)
    pub score_value: f32,
    pub next_level: Option<String>,
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
    pub objectives: String,
    /// the objectives text with both forms (the pause menu shows the short one)
    pub objectives_raw: String,
    /// sound_ears that played and removed themselves
    pub ears_done: std::collections::HashSet<u32>,
    /// the message feed: (seconds left, line)
    pub feed: Vec<(f32, String)>,
}

#[derive(Message, Clone, Copy)]
pub struct SendTrigger(pub i64);

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
#[derive(serde::Serialize, serde::Deserialize, Default, Clone)]
pub struct MissionSave {
    pub triggers_done: Vec<u32>,
    /// (id, enabled, done)
    pub sites: Vec<(u32, bool, bool)>,
    /// (id, active, left)
    pub barracks: Vec<(u32, bool, i64)>,
    pub pub_done: bool,
    pub pub_pool: Option<f32>,
    pub compass_tag: i64,
    pub tickers: u32,
    pub objectives: String,
    pub objectives_raw: String,
    pub ears_done: Vec<u32>,
}

impl Mission {
    pub fn snapshot(&self) -> MissionSave {
        MissionSave {
            triggers_done: self.triggers.iter().filter(|t| t.done).map(|t| t.id).collect(),
            sites: self.sites.iter().map(|s| (s.id, s.enabled, s.done || s.armed.is_some())).collect(),
            barracks: self.barracks.iter().map(|b| (b.id, b.active, b.left())).collect(),
            pub_done: self.pub_done,
            pub_pool: self.pub_info.as_ref().map(|p| p.pool),
            compass_tag: self.compass_tag,
            tickers: self.tickers,
            objectives: self.objectives.clone(),
            objectives_raw: self.objectives_raw.clone(),
            ears_done: self.ears_done.iter().copied().collect(),
        }
    }

    /// Back to a saved state; the start trigger and message are not replayed.
    pub fn restore(&mut self, s: &MissionSave) {
        for t in &mut self.triggers {
            if s.triggers_done.contains(&t.id) {
                t.done = true;
            }
        }
        for st in &mut self.sites {
            if let Some(&(_, en, done)) = s.sites.iter().find(|x| x.0 == st.id) {
                st.enabled = en;
                st.done = done;
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
        self.pub_done = s.pub_done;
        if let (Some(pool), Some(p)) = (s.pub_pool, self.pub_info.as_mut()) {
            p.pool = pool;
        }
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
        let s = |m: &Map<String, Value>, k: &str| m.get(k).and_then(|v| v.as_str()).filter(|v| !v.is_empty() && *v != "none").map(str::to_owned);
        let n = |m: &Map<String, Value>, k: &str| m.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0);
        let mut receivers: HashMap<i64, Vec<Action>> = HashMap::new();
        let mut triggers = Vec::new();
        let mut death_senders = HashMap::new();
        for e in &f.edges {
            if let (Some(fid), Some(ev)) = (e.from.id, e.from.event.as_deref()) {
                if ev.contains("dies") {
                    death_senders.insert(fid, e.trigger);
                }
            }
            // senders: volumes that fire on enter
            if let (Some(fid), Some(ev)) = (e.from.id, e.from.event.as_deref()) {
                if ev.contains("enters the volume") && !triggers.iter().any(|t: &Trigger| t.id == fid && t.sends == e.trigger) {
                    if let Some(i) = inst.get(&fid) {
                        let vol = i.volume.as_ref();
                        let kind = vol.and_then(|v| v.get("kind")).and_then(|v| v.as_str()).unwrap_or("sphere");
                        let volume = if kind == "box" {
                            let aabb = vol.and_then(|v| v.get("world_aabb")).and_then(|v| v.as_array()).cloned().unwrap_or_default();
                            let p = |i: usize| aabb.get(i).and_then(|v| v.as_array()).map(|a| [a[0].as_f64().unwrap_or(0.0) as f32, a[1].as_f64().unwrap_or(0.0) as f32, a[2].as_f64().unwrap_or(0.0) as f32]).unwrap_or([0.0; 3]);
                            let (a, b) = (game_pos(p(0)), game_pos(p(1)));
                            Volume::Box(a.min(b), a.max(b))
                        } else {
                            let r = vol.and_then(|v| v.get("radius")).and_then(|v| v.as_f64()).unwrap_or(20.0) as f32;
                            Volume::Sphere(game_pos(i.position.unwrap_or_default()), r)
                        };
                        let one_shot = vol.and_then(|v| v.get("one_shot")).and_then(|v| v.as_i64()).unwrap_or(1) != 0;
                        triggers.push(Trigger { id: fid, sends: e.trigger, volume, one_shot, inside: false, done: false });
                    }
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
                    ear: to as u32,
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
            } else if action.starts_with("enable this bomb site") {
                Action::EnableSite(to)
            } else if action.starts_with("WIN") {
                Action::Win
            } else {
                continue;
            };
            receivers.entry(e.trigger).or_default().push(a);
        }
        let mut sites = Vec::new();
        let mut barracks = Vec::new();
        // the set-up's rand() (0x0040ad80: the first release delay)
        let mut crt = crate::fxvm::CrtRand::default();
        let mut pub_pos = None;
        let mut pub_info = None;
        let mut pub_exit_pos = None;
        let mut tags: HashMap<i64, Vec<Vec3>> = HashMap::new();
        for i in &f.instances {
            let r = &i.resolved;
            let pos = game_pos(i.position.unwrap_or_default());
            let tag = n(r, "tag") as i64;
            if tag != 0 {
                tags.entry(tag).or_default().push(pos);
            }
            match i.template.as_str() {
                "player_tickerbomb" => sites.push(Site {
                    id: i.id,
                    pos,
                    radius: n(r, "message_radius").max(10.0) as f32,
                    enabled: n(r, "receive_trigger_enable") == 0.0,
                    explode: n(r, "send_trigger_explode") as i64,
                    non_enabled: n(r, "send_trigger_non_enabled") as i64,
                    armed: None,
                    hold: 0.0,
                    done: false,
                }),
                "pub" => {
                    // the doorway: the model's Ref_entrance bone (doorway_bone_name) and doorway_entry_radius
                    // (0x00467b60: player position within the radius); the exit: Ref_exit (player_exit_bone_name)
                    let refs = i.model.as_ref().zip(i.transform.as_ref()).map(|(model, t)| (pub_bone(dir, model, "ref_entrance"), pub_bone(dir, model, "ref_exit"), crate::scene::game_matrix(t)));
                    let world = |b: Option<Vec3>, m: Mat4| b.map(|l| game_to_engine() * m.transform_point3(l));
                    let (door, exit) = refs.map(|(a, b, m)| (world(a, m), world(b, m))).unwrap_or((None, None));
                    pub_exit_pos = exit;
                    pub_pos = Some((door.unwrap_or(pos), n(r, "doorway_entry_radius").max(1.0) as f32, n(r, "send_trigger_exit") as i64));
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
                    pub_info = Some(PubInfo {
                        sells,
                        special,
                        has_tickerbomb: n(r, "has_tickerbomb") != 0.0,
                        pool: n(r, "health_pool") as f32,
                        bar_rate: n(r, "health_bar_rate").max(1.0) as f32,
                    });
                }
                t if t.starts_with("grunt_barracks") => {
                    // 0x0040ad80: the slots from the model's matrices; a barracks with an exit slot ("ref_exit_0%d")
                    // releases troops created at "ref_spawn_0%d" (lev_1_m1's mini barracks have one of each)
                    let spawn = i.model.as_ref().zip(i.transform.as_ref()).and_then(|(model, t)| {
                        let m = crate::scene::game_matrix(t);
                        let w = |b: &str| pub_bone(dir, model, b).map(|l| game_to_engine() * m.transform_point3(l));
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
                        activate: n(r, "receive_trigger_activate") as i64,
                        deactivate: n(r, "receive_trigger_deactivate") as i64,
                        reset: n(r, "trigger_reset") as i64,
                        alert_group: n(r, "alert_group") as i64,
                        marker_tags: (n(r, "marker_tag_min") as i64, n(r, "marker_tag_max") as i64),
                        spawn,
                        health: None,
                        pending: false,
                    })
                }
                _ => {}
            }
        }
        let m = &f.mission.resolved;
        Some(Mission {
            receivers,
            death_senders,
            triggers,
            sites,
            pub_pos,
            pub_done: false,
            pub_request: false,
            pub_exit_pending: false,
            pub_info,
            pub_exit_pos,
            barracks,
            hints: f.hints.into_iter().filter_map(|(k, h)| h.text.map(|t| (k, t))).collect(),
            tags,
            compass_tag: n(m, "compass_objective_tag") as i64,
            start_trigger: n(m, "send_trigger_start") as i64,
            start_text: s(m, "start_text").and_then(|t| f.text.get(&t).cloned()),
            success_text: s(m, "success_text").and_then(|t| f.text.get(&t).cloned()),
            score_value: n(m, "score_value") as f32,
            next_level: s(m, "next_level_name"),
            failure_text: s(m, "failure_text").and_then(|t| f.text.get(&t).cloned()),
            texts: f.text,
            started: false,
            start_sent: false,
            queue: Vec::new(),
            showing: None,
            tickers: 0,
            outcome: Outcome::Playing,
            dead_timer: 0.0,
            log: Vec::new(),
            objectives: String::new(),
            objectives_raw: String::new(),
            ears_done: Default::default(),
            feed: Vec::new(),
        })
    }

    /// A line in the in-level message feed (Chat_AddMessage: 3 lines, the oldest scrolls off after 6 s), from a
    /// text id or a literal.
    pub fn feed(&mut self, text_or_id: &str) {
        let t = self.texts.get(text_or_id).cloned().unwrap_or_else(|| text_or_id.to_owned());
        // the chat colour table's entry 1, #1fffff (0x0046a7e0; measured on the original's capture, frame 815)
        self.feed.push((6.0, format!("<rgb=1fffff>{}", long_form(&t))));
        if self.feed.len() > 3 {
            self.feed.remove(0);
        }
    }

    fn hint(&self, key: &str) -> String {
        self.hints.get(key).cloned().unwrap_or_default().replace("%s", &crate::input::key(crate::input::Act::Use))
    }

    pub fn message(&mut self, text: &str, delay: f32) {
        self.queue.push((delay, long_form(text)));
    }

    /// The nearest object of the compass tag (engine space).
    pub fn compass_target(&self, from: Vec3) -> Option<Vec3> {
        self.tags.get(&self.compass_tag)?.iter().copied().min_by(|a, b| a.distance(from).total_cmp(&b.distance(from)))
    }
}

pub struct MissionPlugin;

impl Plugin for MissionPlugin {
    fn build(&self, app: &mut App) {
        // the mission runs once play begins (not behind the title menu); paused messages keep the state Playing
        app.add_message::<SendTrigger>()
            .add_systems(Update, (start, volumes, sites, pub_door, barracks, dispatch, messages, outcome).chain().run_if(in_state(crate::AppState::Playing)))
            .add_systems(Update, event_log)
            .add_systems(Update, deaths.run_if(in_state(crate::AppState::Playing)));
    }
}

/// An object with send_trigger_on_death died: its trigger goes out.
fn deaths(mut m: Option<ResMut<Mission>>, mut died: MessageReader<crate::fx::Died>, ids: Query<&InstanceId>, mut out: MessageWriter<SendTrigger>) {
    let Some(m) = m.as_deref_mut() else {
        died.clear();
        return;
    };
    for d in died.read() {
        // a destroyed barracks releases no more troops (the building is replaced by its husk)
        if let Ok(i) = ids.get(d.0) {
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
fn event_log(m: Option<Res<Mission>>, time: Res<Time>, mut shown: Local<usize>) {
    let Some(m) = m else { return };
    for e in m.log.iter().skip(*shown) {
        info!("EVENT {:.2} mission: {}", time.elapsed_secs(), e);
    }
    *shown = m.log.len();
}

fn start(mut m: Option<ResMut<Mission>>, mut out: MessageWriter<SendTrigger>, path: Res<crate::campath::CameraPath>, time: Res<Time>, mut waited: Local<f32>) {
    let Some(m) = m.as_deref_mut() else { return };
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
    // MissionObject_ShowStart 0x0044daf0: once no camera path is active (it starts on the first frames), the start
    // text as a paused message
    *waited += time.delta_secs();
    if path.active() || *waited < 0.3 {
        return;
    }
    m.started = true;
    if let Some(t) = m.start_text.clone() {
        m.objectives = long_form(&t);
        m.objectives_raw = t.clone();
        m.message(&t, 0.0);
    }
    m.log.push("start".into());
}

/// Trigger volumes: the player entering sends the trigger (one-shot volumes once).
fn volumes(mut m: Option<ResMut<Mission>>, player: Query<&GlobalTransform, With<Player>>, mut out: MessageWriter<SendTrigger>) {
    let (Some(m), Ok(p)) = (m.as_deref_mut(), player.single()) else { return };
    let p = p.translation() + Vec3::Y * 5.0;
    for t in &mut m.triggers {
        if t.done {
            continue;
        }
        let inside = match t.volume {
            Volume::Sphere(c, r) => c.distance(p) <= r,
            Volume::Box(a, b) => p.cmpge(a - Vec3::splat(2.0)).all() && p.cmple(b + Vec3::Y * 20.0).all(),
        };
        if inside && !t.inside {
            out.write(SendTrigger(t.sends));
            m.log.push(format!("volume {} -> trigger {}", t.id, t.sends));
            if t.one_shot {
                t.done = true;
            }
        }
        t.inside = inside;
    }
}

/// Ticker bomb sites (PlayerTickerBomb): in range with ticker bombs: "Hold down %s to set the ticker bomb!";
/// held for 2 s it arms with a 6 s fuse (0x0044e4a0), then explodes and sends send_trigger_explode.
#[allow(clippy::too_many_arguments)]
fn sites(
    time: Res<Time>,
    ctl: Res<crate::input::Controls>,
    bot: Res<crate::playtest::Bot>,
    mut m: Option<ResMut<Mission>>,
    player: Query<&GlobalTransform, With<Player>>,
    mut out: MessageWriter<SendTrigger>,
    mut blast: MessageWriter<Explode>,
    mut fx: MessageWriter<StartFx>,
    mut hud: ResMut<crate::hud::HudState>,
    data: Res<crate::gamedata::GameData>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut stop: MessageWriter<crate::audio::StopSound>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let (Some(m), Ok(p)) = (m.as_deref_mut(), player.single()) else { return };
    let dt = time.delta_secs();
    let p = p.translation();
    let mut prompt = None;
    let holding = ctl.down(crate::input::Act::Use) || bot.use_key;
    let tickers = m.tickers;
    // Game+0xdcf: a ticker bomb is armed and ticking (one at a time)
    let any_armed = m.sites.iter().any(|s| s.armed.is_some() && !s.done);
    let mut sends = Vec::new();
    let mut logs = Vec::new();
    let mut arming = None;
    let mut clock = None;
    // the wind-up (0x0044e3a0 starts wpn_ticker_windup_lp, 0x0044e430 stops it when Use is let go)
    let mut wind_down = |stop: &mut MessageWriter<crate::audio::StopSound>, sounds: &mut MessageWriter<crate::audio::PlaySound>, at: Vec3| {
        stop.write(crate::audio::StopSound("wpn_ticker_windup_lp".into()));
        sounds.write(crate::audio::PlaySound::at("wpn_ticker_windup_end", at));
    };
    for s in &mut m.sites {
        if s.done {
            continue;
        }
        if let Some(fuse) = s.armed.as_mut() {
            *fuse -= dt;
            // ui_clock plays from the arming (g+0x25e4 = 6 s)
            clock = Some(6.0 - fuse.max(0.0));
            if *fuse <= 0.0 {
                s.done = true;
                clock = None;
                let b = data.props("p_player_tickerbomb");
                blast.write(Explode {
                    at: s.pos,
                    area_damage: b.f("area_damage", 100.0),
                    radius_inner: b.f("damage_radius_inner", 40.0),
                    radius_outer: b.f("damage_radius_outer", 60.0),
                    impact_force: b.f("impact_force", 25000.0),
                    source: None,
                    delay: 0.0,
                    // planted by the player (guess: the ticker's damage record is not traced)
                    by: crate::fx::Src::Player,
                    cartoon: false,
                });
                fx.write(StartFx { name: "fx_ticker_bomb".into(), at: s.pos, rotation: Quat::IDENTITY, follow: None, position_only: false });
                if s.explode != 0 {
                    sends.push(s.explode);
                }
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
        if !s.enabled {
            if tickers > 0 && s.non_enabled != 0 {
                sends.push(s.non_enabled);
                s.non_enabled = 0;
            }
            continue;
        }
        if tickers == 0 {
            prompt = Some("ui_tickerbomb_none".to_string());
            continue;
        }
        // another bomb ticking: "Wait for the other Ticker Bomb to explode!" (0x0044e3a0)
        if any_armed {
            prompt = Some("ui_tickerbomb_patience".to_string());
            continue;
        }
        if holding {
            if s.hold == 0.0 {
                sounds.write(crate::audio::PlaySound::at("wpn_ticker_windup_start", s.pos));
                sounds.write(crate::audio::PlaySound { looped: Some(true), ..crate::audio::PlaySound::at("wpn_ticker_windup_lp", s.pos) });
            }
            s.hold += dt;
            // the arming bar ui_tickerbomb_set fills while Use is held (it takes the prompt's place)
            arming = Some((s.hold / 2.0).min(1.0));
            if s.hold >= 2.0 {
                // 0x0044e4a0: wind-up stops, wpn_ticker and one of Roman's two "armed" lines (50 % each), the clock
                s.armed = Some(6.0);
                arming = None;
                clock = Some(0.0);
                stop.write(crate::audio::StopSound("wpn_ticker_windup_lp".into()));
                sounds.write(crate::audio::PlaySound::at("wpn_ticker", s.pos));
                let line = if rng.rand() < 0.5 { "rom50211_tickerarmed" } else { "rom50212_tickerarmed" };
                sounds.write(crate::audio::PlaySound::at(line, p));
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
    m.log.extend(logs);
    hud.ticker_arming = arming;
    hud.ticker_clock = clock;
    hud.prompt = prompt.map(|k| if k.starts_with("ui_") { m.hint(&k) } else { k });
}

/// The pub: near the doorway "Press %s to enter pub"; Use opens the pub screen (gamemenu.rs: awards, health from
/// the pool, weapons); leaving it sends send_trigger_exit.
fn pub_door(ctl: Res<crate::input::Controls>, bot: Res<crate::playtest::Bot>, mut m: Option<ResMut<Mission>>, player: Query<&GlobalTransform, With<Player>>, mut out: MessageWriter<SendTrigger>, mut hud: ResMut<crate::hud::HudState>) {
    let (Some(m), Ok(p)) = (m.as_deref_mut(), player.single()) else { return };
    let Some((pos, r, exit)) = m.pub_pos else { return };
    if m.pub_exit_pending {
        m.pub_exit_pending = false;
        m.log.push(format!("pub screen left -> trigger {exit}"));
        if exit != 0 {
            out.write(SendTrigger(exit));
        }
    }
    if m.pub_done || p.translation().with_y(0.0).distance(pos.with_y(0.0)) > r {
        return;
    }
    if hud.prompt.is_none() {
        hud.prompt = Some(m.hint("ui_pub_entry_hint"));
    }
    if ctl.pressed(crate::input::Act::Use) || bot.use_key {
        m.pub_done = true;
        m.pub_request = true;
        hud.prompt = None;
        m.log.push("pub entered".into());
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
    mut crt: Local<crate::fxvm::CrtRand>,
    troops: Query<(&BarracksTroop, &Grunt)>,
    objects: Query<(&InstanceId, &Destructible)>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
) {
    let Some(m) = m.as_deref_mut() else { return };
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
        if b.batch == 0 && alive >= b.count {
            b.timer = crt.r(b.release.0, b.release.1);
        } else {
            b.timer -= dt;
        }
        if b.batch == 0 && alive < b.count {
            b.batch = b.amount;
        }
        if b.timer > 0.0 {
            continue;
        }
        let ty = b.most();
        if b.counts[ty] <= 0 {
            b.active = false;
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
                barracks: Some(b.id),
                exit: Some(exit),
                marker_tags: (b.marker_tags.0 != 0).then_some(b.marker_tags),
                alert_group: b.alert_group,
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

#[allow(clippy::too_many_arguments)]
fn dispatch(
    mut m: Option<ResMut<Mission>>,
    mut events: MessageReader<SendTrigger>,
    mut grunts: Query<(&InstanceId, &mut Grunt)>,
    mut objects: Query<(&InstanceId, &mut Destructible)>,
    plain: Query<(Entity, &InstanceId, Option<&crate::level::Placed>), (Without<Destructible>, Without<Grunt>)>,
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
) {
    let Some(m) = m.as_deref_mut() else { return };
    let pe = player.single().ok().map(|p| p.0);
    let pt = player.single().ok().map(|p| p.1.translation());
    for SendTrigger(t) in events.read() {
        let actions = m.receivers.get(t).cloned().unwrap_or_default();
        m.log.push(format!("trigger {t}: {} receivers", actions.len()));
        // the barracks' trigger handler 0x0040d1b0: activate starts the release and is used up (+0x534 = 0),
        // deactivate stops it, reset starts it with the troop counts refilled
        let mut blogs = Vec::new();
        for b in &mut m.barracks {
            if *t == 0 {
                break;
            }
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
        m.log.extend(blogs);
        for a in actions {
            match a {
                Action::WakeAi(id) => {
                    for (i, mut g) in &mut grunts {
                        if i.0 == id && g.mind == Mind::WaitTrigger {
                            g.mind = Mind::Roam;
                            g.target = pe;
                        }
                    }
                }
                Action::ShowText(t, objective) => {
                    // the objective jingle: the victory music request (0x0046afa0 -> 0x0044f560, cl = 1)
                    if objective {
                        music.request_victory(true);
                        m.log.push("music: victory request (objective)".into());
                    }
                    let text = m.texts.get(&t).cloned().unwrap_or(t);
                    m.objectives = long_form(&text);
                    m.objectives_raw = text.clone();
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
                    fx.write(StartFx { name, at, rotation: Quat::IDENTITY, follow: None, position_only: false });
                }
                // handled above by the barracks' own trigger ids
                Action::Barracks(_) => {}
                Action::Destroy(id) => {
                    let mut killed = false;
                    for (i, mut d) in &mut objects {
                        if i.0 == id && d.dying.is_none() {
                            d.health = 0.0;
                            d.dying = Some(0.0);
                            killed = true;
                        }
                    }
                    // objects without health (the big red arrow): receive_trigger_kill removes them
                    if !killed {
                        for (e, i, pl) in &plain {
                            if i.0 == id {
                                // with its collision (the arrow's body stayed behind, blocking the crosshair)
                                if let Some(c) = pl.and_then(|pl| pl.collider) {
                                    physics.remove_static(c);
                                }
                                commands.entity(e).despawn();
                            }
                        }
                    }
                }
                Action::EnableSite(id) => {
                    for s in &mut m.sites {
                        if s.id == id {
                            s.enabled = true;
                        }
                    }
                }
                Action::Win => {
                    if m.outcome == Outcome::Playing {
                        m.outcome = Outcome::Won;
                        // the mission's score_value to Loot (MissionObject_OnTrigger 0x0044d600, game_02.cpp:2262)
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
        m.feed(&line);
    }
    // the feed: the oldest line scrolls off after 6 s
    for f in &mut m.feed {
        f.0 -= time.delta_secs();
    }
    m.feed.retain(|f| f.0 > 0.0);
    hud.messages = m.feed.iter().map(|f| f.1.clone()).collect();
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
fn outcome(time: Res<Time>, mut m: Option<ResMut<Mission>>, roman: Query<&crate::player::Roman>, mut hud: ResMut<crate::hud::HudState>, cam: Query<&GlobalTransform, With<Camera3d>>, player: Query<&GlobalTransform, With<Player>>) {
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
        hud.objective = m.compass_target(p.translation()).map(|t| {
            let to = (t - p.translation()).with_y(0.0);
            let f = c.forward();
            let rel = to.x.atan2(-to.z) - f.x.atan2(-f.z);
            (-rel, to.length())
        });
    }
}
