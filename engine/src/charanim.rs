//! Which animation each Armed and Dangerous character plays (names from the anim bank and the template slots in
//! anims/index.json). Roman's anims come in bottom / top halves (`_b` drives the legs, `_t` the body above
//! "belly"); grunts, Q and Jonesy play whole-body anims. The original picks grunt anims by id through
//! vt+0x1a4 (docs/subsystems/grunt-ai.md 11); the state -> anim pairs here follow the states there.

use crate::levelreset::LevelAppExt;
use crate::ai::{Companion, CoverPhase, GunPhase, Grunt, Mind};
use crate::player::Roman;
use crate::skin::{Animator, Mask};
use bevy::prelude::*;

pub struct CharAnimPlugin;

impl Plugin for CharAnimPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<Twist>().init_level_resource::<PlayerAnimTable>().add_systems(Update, (roman, grunts, companions, critters));
        app.add_systems(PostUpdate, twist.after(crate::skin::AnimateSet).before(bevy::transform::TransformSystems::Propagate));
        app.add_systems(PostUpdate, count_notes.after(crate::skin::AnimateSet).run_if(resource_exists::<crate::playtest::PlayState>));
    }
}

/// Playtest evidence: Roman's anim notes so far (`notes`) and his footsteps among them (`notes_step`: the
/// fx_material Fx_fs_roman_* notes of the walk / run anims).
fn count_notes(mut ev: MessageReader<crate::skin::AnimNote>, roman: Query<Entity, With<Roman>>, mut ps: ResMut<crate::playtest::PlayState>) {
    for k in ["notes", "notes_step"] {
        if !ps.0.contains_key(k) {
            ps.0.insert(k.into(), 0.into());
        }
    }
    let Ok(me) = roman.single() else {
        ev.clear();
        return;
    };
    for n in ev.read().filter(|n| n.entity == me) {
        let bump = |ps: &mut crate::playtest::PlayState, k: &str| {
            let v = ps.0.get(k).and_then(|v| v.as_u64()).unwrap_or(0) + 1;
            ps.0.insert(k.into(), v.into());
        };
        bump(&mut ps, "notes");
        if n.text.to_lowercase().starts_with("fx_material fx_fs_roman") {
            bump(&mut ps, "notes_step");
        }
    }
}

/// The AI characters' cross-fade: GenericAI_PlayAnim 0x004262d0 and PlayAnim4 0x00426480 pass 0.25 s, except for a
/// character's first anim (flag 0x4000000 of +0x484: 0; the engine's first play fades from nothing, so it cuts too).
const AI_BLEND: f32 = 0.25;

/// One entry of the player's anim table: what a (mode, state) plays.
#[derive(Clone, Debug, PartialEq)]
pub struct ModeEntry {
    /// the anim (type 1) or the prefix of its `_b` / `_t` aim-pitch pair (type 2), lower case (0x00475f50 joins
    /// prefix and suffix: "", "_b" / "_t", "_bl" / "_tl" / "_br" / "_tr")
    pub name: String,
    /// 1 one anim, 2 a pair blended by the aim pitch, 3 four corners (not on the player in lev_1_m1)
    pub kind_type: u32,
    /// slot flags: 1 loops, 0x40 plays backwards
    pub flags: u32,
    /// the cross-fade class (0x005cb3a8's row / column)
    pub kind: usize,
    /// the speed-driven rate block (movement modes)
    pub rate: Option<String>,
}

/// The player's animation table (player_anims.json, tools/remaster_export_player_anims.py from the exe): what each
/// movement mode and action state plays (0x005cb650, index mode * 7 + state; a missing state links to another
/// state of the mode), the cross-fade times between entry kinds (0x005cb3a8, [previous][next]) and the speed-driven
/// rate blocks of the movement anims (directions from PlayerObject_Load 0x00473da0, speeds from p_trotter's anim_*
/// keys in player.json `movement.anim_speed_tables`).
#[derive(Resource, Default)]
pub struct PlayerAnimTable {
    loaded: bool,
    entries: std::collections::HashMap<(u32, u32), ModeEntry>,
    links: std::collections::HashMap<(u32, u32), u32>,
    blend: Vec<Vec<f32>>,
    rates: std::collections::HashMap<String, crate::skin::RateBlock>,
}

/// The player's movement modes (+0x554) used here (the table's rows; player.md 8).
pub mod mode {
    pub const STANCE: u32 = 0;
    pub const IDLE_1: u32 = 1;
    pub const FALL_LOOP: u32 = 7;
    pub const LAND_HARD: u32 = 8;
    pub const RUN: u32 = 9;
    pub const RUN_BACK: u32 = 10;
    pub const RUN_LEFT: u32 = 11;
    pub const RUN_RIGHT: u32 = 12;
    pub const WALK: u32 = 13;
    pub const WALK_BACK: u32 = 14;
    pub const WALK_LEFT: u32 = 15;
    pub const WALK_RIGHT: u32 = 16;
    pub const JUMP_INTO: u32 = 17;
    pub const JUMP_IDLE: u32 = 18;
    pub const JUMP_LAND: u32 = 19;
    pub const LAND_SOFT: u32 = 20;
    pub const TURN_LEFT: u32 = 28;
    pub const TURN_RIGHT: u32 = 29;
    pub const HIT_SMALL: u32 = 30;
    pub const HIT_MED: u32 = 31;
    pub const TOPSY_INTO: u32 = 32;
    pub const TOPSY_TURN: u32 = 33;
    pub const TOPSY_HANG: u32 = 34;
    pub const TOPSY_BACK: u32 = 35;
}

/// The action states (+0x558) of 0x00475ff0: 1 firing (rifle / machine gun / ...: weapons 0..2), 3 reloading, 4
/// changing weapon, 5 throwing.
pub mod state {
    pub const NONE: u32 = 0;
    pub const FIRE: u32 = 1;
    pub const RELOAD: u32 = 3;
    pub const WCHANGE: u32 = 4;
    pub const THROW: u32 = 5;
}

impl PlayerAnimTable {
    fn load(&mut self, dir: &std::path::Path) {
        self.loaded = true;
        let read = |f: &str| std::fs::read_to_string(dir.join(f)).ok().and_then(|t| serde_json::from_str::<serde_json::Value>(&t).ok());
        let Some(t) = read("player_anims.json") else {
            warn!("player_anims.json missing (tools/remaster_export_player_anims.py): Roman plays no anims");
            return;
        };
        let u = |v: &serde_json::Value| v.as_u64().unwrap_or(0) as u32;
        for m in t["modes"].as_array().into_iter().flatten() {
            self.entries.insert(
                (u(&m["mode"]), u(&m["state"])),
                ModeEntry {
                    name: m["name"].as_str().unwrap_or_default().to_owned(),
                    kind_type: u(&m["type"]),
                    flags: u(&m["flags"]),
                    kind: u(&m["kind"]) as usize,
                    rate: m["rate"].as_str().map(str::to_owned),
                },
            );
        }
        for (k, v) in t["links"].as_object().into_iter().flatten() {
            let mut it = k.split(',').filter_map(|x| x.parse().ok());
            if let (Some(m), Some(s)) = (it.next(), it.next()) {
                self.links.insert((m, s), u(v));
            }
        }
        self.blend = t["blend"].as_array().into_iter().flatten().map(|r| r.as_array().into_iter().flatten().map(|v| v.as_f64().unwrap_or(0.0) as f32).collect()).collect();
        let speeds = read("player.json").map(|p| p["movement"]["anim_speed_tables"].clone()).unwrap_or_default();
        for (block, dir) in t["rate_dirs"].as_object().into_iter().flatten() {
            // run_fwd / run_back read anim_run_*, walk_* anim_walk_*, side_run_* anim_side_run_*, side_walk_* anim_side_walk_*
            let key = ["_fwd", "_back", "_left", "_right"].iter().fold(block.as_str(), |k, s| k.trim_end_matches(s));
            let s = &speeds[key];
            let f = |k: &str, d: f32| s[k].as_f64().map_or(d, |v| v as f32);
            let d: Vec<f32> = dir.as_array().into_iter().flatten().map(|v| v.as_f64().unwrap_or(0.0) as f32).collect();
            // code defaults where a key is missing (PlayerObject_Load: min 0, max 100, factors 0 / 1, high speed 17)
            self.rates.insert(
                block.clone(),
                crate::skin::RateBlock {
                    dir: Vec3::new(d.first().copied().unwrap_or(0.0), d.get(1).copied().unwrap_or(0.0), d.get(2).copied().unwrap_or(0.0)),
                    min: f("min", 0.0),
                    max: f("max", 100.0),
                    low_factor: f("low_factor", 0.0),
                    low_speed: f("low_speed", 0.0),
                    high_factor: f("high_factor", 1.0),
                    high_speed: f("high_speed", 17.0),
                },
            );
        }
    }

    /// The entry of (mode, state), following the state links (0x00475ff0: `fallback` when a link was followed).
    pub fn entry(&self, mode: u32, state: u32) -> Option<(&ModeEntry, bool)> {
        let mut s = state;
        for hops in 0..8 {
            if let Some(e) = self.entries.get(&(mode, s)) {
                return Some((e, hops > 0));
            }
            s = *self.links.get(&(mode, s))?;
        }
        None
    }

    /// The cross-fade time from an entry of kind `from` to one of kind `to` (0x005cb3a8[from * 13 + to]).
    pub fn blend(&self, from: usize, to: usize) -> f32 {
        self.blend.get(from).and_then(|r| r.get(to)).copied().unwrap_or(0.0)
    }

    pub fn rate(&self, block: &str) -> Option<crate::skin::RateBlock> {
        self.rates.get(block).copied()
    }
}

/// Roman's anim state between frames (the player's +0x554 mode, +0x558 state and the entry playing).
#[derive(Default)]
pub struct RomanAnim {
    mode: Option<u32>,
    state: u32,
    entry: Option<ModeEntry>,
}

/// Starts (mode, state) on Roman as 0x00475ff0 does: the table's entry; when the mode or state changed and the
/// entry's anims differ, a cross-fade of 0x005cb3a8[previous kind][kind] and the start: a mode change within the same
/// state (other than 0, no link followed) keeps the fraction (flags 0xa00 with 0x00475fc0), two movement entries
/// (kind 4) of the same type in state 0 / 6 carry the time on, anything else restarts (at the end when backwards).
/// Type 1 plays the one anim on layer 0; type 2 the `_b` / `_t` pair on layers 0 / 1, the top weighted by the aim
/// pitch (`aim`). Movement entries play at their rate block's speed for `velocity` (the model frame), backwards with
/// flag 0x40. Returns whether the entry changed.
pub fn play_mode(a: &mut Animator, table: &PlayerAnimTable, st: &mut RomanAnim, mode: u32, state: u32, aim: f32, velocity: Vec3) -> bool {
    let Some((e, fallback)) = table.entry(mode, state) else { return false };
    let e = e.clone();
    let changed_mode = st.mode != Some(mode);
    let same = !changed_mode && st.state == state;
    let prev = st.entry.clone();
    let changed = !same && prev.as_ref() != Some(&e);
    if changed {
        a.blend = prev.as_ref().map_or(0.0, |p| table.blend(p.kind, e.kind));
    }
    let looping = e.flags & 1 != 0;
    let backwards = e.flags & 0x40 != 0;
    let start = if !changed {
        Start::Keep
    } else if changed_mode && st.state == state && state != 0 && !fallback {
        Start::Fraction
    } else if prev.as_ref().is_some_and(|p| p.kind == 4 && e.kind == 4 && p.kind_type == e.kind_type) && (state == 0 || state == 6) {
        Start::Time
    } else {
        Start::Restart
    };
    let rate = e.rate.as_deref().and_then(|b| table.rate(b)).map_or(1.0, |rb| rb.rate(velocity));
    let speed = if backwards { -rate } else { rate };
    match e.kind_type {
        1 => {
            start.play(a, 0, &e.name, looping);
            a.stop(1);
        }
        _ => {
            start.play(a, 0, &format!("{}_b", e.name), looping);
            start.play(a, 1, &format!("{}_t", e.name), looping);
            a.set_weight(1, aim);
            a.sync(1, 0);
        }
    }
    for l in a.layers.iter_mut().take(2) {
        l.speed = speed;
    }
    st.mode = Some(mode);
    st.state = state;
    st.entry = Some(e);
    changed
}

/// How a changed entry starts (0x00475ff0's flags).
#[derive(Clone, Copy)]
enum Start {
    Keep,
    Restart,
    Fraction,
    Time,
}

impl Start {
    fn play(self, a: &mut Animator, slot: usize, name: &str, looping: bool) {
        match self {
            Start::Keep => a.play(slot, name, looping, Mask::All),
            Start::Restart => {
                let same = a.layers.get(slot).is_some_and(|l| l.anim == name);
                a.play(slot, name, looping, Mask::All);
                // the same anim as a new entry (the topsy turn back): from its start again
                if same {
                    a.restart(slot);
                }
            }
            Start::Fraction => a.play_synced(slot, name, looping, Mask::All),
            Start::Time => a.play_continued(slot, name, looping, Mask::All),
        }
    }
}

/// The movement mode of a gait (0x00476a10, game_05.cpp:1901-1914): walk or run by the speed against fwd_max_speed
/// x anim_walk_run_switch (40 x 0.3 = 12), the side modes when the sideways speed beats the forward speed (forwards
/// x 1.5, 0x005bcf94; backwards x 1), else forwards or backwards. guess: the local velocity (+0x494 / +0x498) decides.
pub fn gait_mode(local: Vec2) -> u32 {
    let (side, fwd) = (local.x, local.y);
    let sideways = if fwd >= 0.0 { side.abs() > fwd * 1.5 } else { side.abs() > -fwd };
    let run = local.length() >= 12.0;
    match (sideways, run, side > 0.0, fwd < 0.0) {
        (true, true, true, _) => mode::RUN_RIGHT,
        (true, true, false, _) => mode::RUN_LEFT,
        (true, false, true, _) => mode::WALK_RIGHT,
        (true, false, false, _) => mode::WALK_LEFT,
        (false, true, _, true) => mode::RUN_BACK,
        (false, true, _, false) => mode::RUN,
        (false, false, _, true) => mode::WALK_BACK,
        (false, false, _, false) => mode::WALK,
    }
}

/// Roman's standing torso twist and turn in place. The player's bone callbacks (PlayerObject_Load binds table
/// 0x005fb760: 0x004735f0 to "center" kind 0 and "belly" kind 1, 0x00472cc0 to "l_foot") read the look-at record
/// actor+0x68c, which 0x00475ff0 rebuilds at each mode change: anim-table kinds 2 (mode 0, the aim stance) and 3
/// (modes 0x1c / 0x1d, Trotter_turn_left / right) set flag 2 (kind 3 also flag 8), with the limit +0x24 =
/// normal_twist_limit (+0x560); the stored direction +8 (the legs' facing) is the previous record's for kinds
/// 1/2/3/5/8 and the actor's facing (0x00453150) otherwise (the fidgets are kind 12: no flag, no twist). Each frame
/// 0x004735f0 takes the angle between the facing and +8, drags +8 to within the limit (less 0.001) and, outside the
/// turn modes and the record's first frame, raises +0xa34 0x2000 with +0x980 = (+8 x facing).z; flag 8 scales the
/// limit by the anim's progress left, so the legs reach the facing as the turn anim ends. "center" turns by the
/// angle (the legs keep their facing), "belly" by minus it (the torso follows the view); actor+0x978 / +0x97c are
/// 0 for the player (game_05.cpp:831, no other writer found). The anim update 0x00476a10 (game_05.cpp:1915) plays
/// mode 0x1c when +0x980 >= 0, else 0x1d, standing, until the anim ends.
#[derive(Resource, Default)]
pub struct Twist {
    /// the stance's record has flag 2 this frame (the aim stance or a turn anim)
    pub active: bool,
    /// the legs' facing (record +8) as a yaw; None: the body's
    pub leg_yaw: Option<f32>,
    /// the torso's twist from the legs (record +0x28; + = the view left of the legs)
    pub angle: f32,
    /// the limit hit (+0xa34 0x2000) and its side (+0x980 > 0: left), for the next frame's anim update
    pub raised: Option<bool>,
    /// the turn anim playing: (left, seconds into it)
    pub turn: Option<(bool, f32)>,
    /// the record's first frame (record byte 0): no 0x2000
    pub fresh: bool,
    /// turn anims started (playtest evidence)
    pub turns: u32,
}

/// Trotter_turn_left / right_aim_b / _t: 29 frames at 30 fps
const TURN_LEN: f32 = 0.9667;

fn wrap(a: f32) -> f32 {
    (a + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI
}

/// The bone callback 0x004735f0 (flag 2) on Roman's "center" and "belly" after the anims set the joints.
pub(crate) fn twist(mut tw: ResMut<Twist>, mut q: Query<(&mut Roman, &Animator)>, mut joints: Query<&mut Transform>, mut state: Option<ResMut<crate::playtest::PlayState>>) {
    for (mut r, a) in &mut q {
        if std::mem::take(&mut r.twist_reset) {
            tw.leg_yaw = None;
            tw.turn = None;
            tw.raised = None;
            tw.fresh = true;
        }
        if !tw.active || r.dead {
            tw.leg_yaw = None;
            tw.angle = 0.0;
            tw.raised = None;
            tw.fresh = true;
        } else {
            // flag 8 (the turn modes): the limit times the anim's progress left (end - t) / (end - start)
            let lim = r.twist_limit * tw.turn.map_or(1.0, |(_, t)| (1.0 - t / TURN_LEN).max(0.0));
            let leg = *tw.leg_yaw.get_or_insert(r.yaw);
            let mut ang = wrap(r.yaw - leg);
            if ang.abs() > lim {
                ang -= (ang.abs() - lim + 0.001) * ang.signum();
                tw.leg_yaw = Some(r.yaw - ang);
                if !tw.fresh && tw.turn.is_none() {
                    tw.raised = Some(ang > 0.0);
                }
            }
            tw.fresh = false;
            tw.angle = ang;
            // the world's up in the model's space (the rig's parent turns by yaw + pi about it); "center" turns by
            // -angle about it (legs at leg_yaw), "belly" back by +angle about the same axis in center's frame
            let up = r.base.inverse() * Vec3::Y;
            let idx = |n: &str| a.skeleton.index.get(n).and_then(|&i| a.joints.get(i).copied());
            if let (Some(c), Some(b)) = (idx("center"), idx("belly")) {
                let mut crot = Quat::IDENTITY;
                if let Ok(mut jt) = joints.get_mut(c) {
                    jt.rotation = Quat::from_axis_angle(up, -ang) * jt.rotation;
                    crot = jt.rotation;
                }
                if let Ok(mut jt) = joints.get_mut(b) {
                    jt.rotation = Quat::from_axis_angle((crot.inverse() * up).normalize(), ang) * jt.rotation;
                }
            }
        }
        if let Some(s) = state.as_mut() {
            let d = |v: f32| (wrap(v).to_degrees() * 10.0).round() / 10.0;
            s.0.insert("twist_deg".into(), d(tw.angle).into());
            s.0.insert("yaw_deg".into(), d(r.yaw).into());
            s.0.insert("leg_yaw_deg".into(), d(tw.leg_yaw.unwrap_or(r.yaw)).into());
            s.0.insert("turn_anim".into(), tw.turn.map_or("", |(l, _)| if l { "left" } else { "right" }).into());
            s.0.insert("turns".into(), tw.turns.into());
        }
        break;
    }
}

/// Roman's idle (0x00476a10, game_05.cpp:1932-1948): standing, the idle time +0xc04 counts; past 5 s (0x005bcbdc),
/// each time the playing anim ends (the aim stance's 1.97 s loop or a fidget) a rand picks the stance (one half) or
/// one of trotter_idle_1..5 (mode 1 + rand * 5 >> 15); any other mode clears the time.
#[derive(Default)]
struct Idle {
    t: f32,
    left: f32,
    fidget: Option<u32>,
}

/// Roman's anims: the engine's player state picks the movement mode and the action state (as 0x00476a10 does from
/// +0xa34), the table plays them (`play_mode`).
#[allow(clippy::too_many_arguments)]
fn roman(
    mut q: Query<(&Roman, &mut Animator)>,
    topsy: Res<crate::topsy::Topsy>,
    time: Res<Time>,
    mut idle: crate::levelreset::PerLevel<Idle>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut tw: ResMut<Twist>,
    (mut table, lib, mut st, ps): (ResMut<PlayerAnimTable>, Res<crate::skin::AnimLibrary>, crate::levelreset::PerLevel<RomanAnim>, Option<ResMut<crate::playtest::PlayState>>),
) {
    if !table.loaded {
        table.load(&lib.dir);
    }
    let table = &*table;
    let st = st.get();
    let idle = idle.get();
    if let (Some(mut ps), Some((_, a))) = (ps, q.iter().next()) {
        // playtest evidence: the mode / state / entry playing, layer 0's time and rate, the cross-fade
        let l0 = a.layers.first();
        ps.0.insert("anim_mode".into(), st.mode.map_or(-1, |m| m as i64).into());
        ps.0.insert("anim_state".into(), st.state.into());
        ps.0.insert("anim_entry".into(), st.entry.as_ref().map_or("", |e| e.name.as_str()).into());
        ps.0.insert("anim_l0".into(), l0.map_or("", |l| l.anim.as_str()).into());
        ps.0.insert("anim_time".into(), l0.map_or(0.0, |l| (l.time * 1000.0).round() / 1000.0).into());
        ps.0.insert("anim_speed".into(), l0.map_or(0.0, |l| (l.speed * 1000.0).round() / 1000.0).into());
        ps.0.insert("anim_fade".into(), ((a.fade_weight() * 1000.0).round() / 1000.0).into());
        ps.0.insert("anim_blend".into(), ((a.blend * 1000.0).round() / 1000.0).into());
    }
    for (r, mut a) in &mut q {
        let a = &mut *a;
        // the twist's record flag 2 (set below for the aim stance and the turn anims)
        tw.active = false;
        // the velocity in the model's frame (game axes, facing -Y, +X its left) for the movement anims' rates
        let v = Vec3::new(-r.move_local.x, -r.move_local.y, 0.0);
        // `_b` / `_t` are the bottom and top of an aim-pitch blend: the rifle points ~50 degrees down in `_b` and ~47
        // up in `_t` (measured on the skeleton, docs/remaster-engine.md). The weight (0x00475ff0, game_05.cpp:1499):
        // (clamp(2 x pitch, -1, 1) + 1) / 2 of the look pitch, 0 at rest (the engine's rest pitch is -0.15)
        let w = ((2.0 * (r.pitch + 0.15)).clamp(-1.0, 1.0) + 1.0) * 0.5;
        let mut play = |a: &mut Animator, m: u32, s: u32| play_mode(a, table, st, m, s, w, v);
        // the Topsy Turvy states (modes 0x20..0x23; 0x23 turns the world back: the turn anim backwards)
        let t = match topsy.phase {
            crate::topsy::Phase::Into => Some(mode::TOPSY_INTO),
            crate::topsy::Phase::Turn => Some(mode::TOPSY_TURN),
            crate::topsy::Phase::Back => Some(mode::TOPSY_BACK),
            crate::topsy::Phase::Hang => Some(mode::TOPSY_HANG),
            crate::topsy::Phase::Off => None,
        };
        if let (Some(m), false) = (t, r.dead) {
            play(a, m, state::NONE);
            continue;
        }
        if r.dead {
            // the ragdoll drives the bones (ragdoll.rs); without one, the fall pose
            if !r.ragdolled {
                play(a, mode::FALL_LOOP, state::NONE);
            }
            continue;
        }
        // the hit reaction (modes 0x1e / 0x1f: Trotter_hit_small / _med, full body, once)
        if r.hit_anim > 0.0 {
            play(a, if r.hit_med { mode::HIT_MED } else { mode::HIT_SMALL }, state::NONE);
            continue;
        }
        // landing (modes 8 land_hard / 0x13 jump_land / 0x14 land_soft, once); falling (mode 7 fall_loop)
        if let Some((n, _, _)) = r.landing {
            let m = match n {
                "trotter_land_hard" => mode::LAND_HARD,
                "trotter_land_soft" => mode::LAND_SOFT,
                _ => mode::JUMP_LAND,
            };
            play(a, m, state::NONE);
            continue;
        }
        if r.falling && !r.on_ground && !r.jumping {
            play(a, mode::FALL_LOOP, state::NONE);
            continue;
        }
        // a standing jump's wind-up (mode 0x11 trotter_jump_into; the take-off at its end, 0x0047aaf0)
        if r.jump_wind.is_some() {
            *idle = Idle::default();
            play(a, mode::JUMP_INTO, state::NONE);
            continue;
        }
        let speed = r.move_local.length();
        // the movement mode: in the air the jump idle (mode 0x12), standing the stance, else the gait's
        let m = if !r.on_ground {
            mode::JUMP_IDLE
        } else if speed < 3.0 {
            mode::STANCE
        } else {
            gait_mode(r.move_local)
        };
        // the action state over it (0x00475ff0 from the request bits): the sticky bomb throw (state 5: trotter_throw
        // 0.633 s standing, trotter_run_throw 0.767 s running), the weapon change, the reload, firing (the fire
        // anims' length 0.3 s; state 1 for the rifle and the machine gun, weapons 0..2)
        let throw_len = if m == mode::RUN || m == mode::RUN_BACK { 0.767 } else { 0.633 };
        let s = if r.since_throw < throw_len {
            state::THROW
        } else if r.wchange.is_some() {
            state::WCHANGE
        } else if r.reloading > 0.0 {
            state::RELOAD
        } else if r.since_fire < 0.3 {
            state::FIRE
        } else {
            state::NONE
        };
        // turning in place (0x00476a10, game_05.cpp:1915): standing with the twist limit hit last frame, the turn
        // anim (mode 0x1c Trotter_turn_left / 0x1d _right; state 1 the _fire pair, other states link to _aim) plays
        // once, the legs catching up (Twist); any mode outside 0..5 clears the idle time
        let stance = m == mode::STANCE;
        if !stance {
            tw.turn = None;
            tw.raised = None;
        } else if tw.turn.is_none() {
            if let Some(left) = tw.raised.take() {
                tw.turn = Some((left, 0.0));
                tw.fresh = true;
                tw.turns += 1;
            }
        }
        if let Some((left, t)) = tw.turn {
            *idle = Idle::default();
            play(a, if left { mode::TURN_LEFT } else { mode::TURN_RIGHT }, if r.since_fire < 0.3 { state::FIRE } else { state::NONE });
            a.layers[0].time = t;
            a.sync(1, 0);
            tw.active = true;
            let t = t + time.delta_secs();
            if t >= TURN_LEN {
                // the anim ended: +0xa34 0x2000 cleared, the aim stance's record from this one (the legs kept)
                tw.turn = None;
                tw.fresh = true;
            } else {
                tw.turn = Some((left, t));
            }
            continue;
        }
        // the idle fidgets (modes 1..5)
        if stance && s == state::NONE {
            let dt = time.delta_secs();
            idle.t += dt;
            idle.left -= dt;
            if idle.left <= 0.0 {
                idle.fidget = None;
                idle.left = 1.967;
                if idle.t > 5.0 && rng.rand() < 0.5 {
                    let k = ((rng.rand() * 5.0) as u32).min(4) + 1;
                    idle.fidget = Some(k);
                    idle.left = [3.3, 5.467, 4.8, 5.467, 3.3][k as usize - 1];
                }
            }
            if let Some(k) = idle.fidget {
                play(a, mode::IDLE_1 + k - 1, state::NONE);
                continue;
            }
        } else {
            *idle = Idle::default();
        }
        play(a, m, s);
        // the aim stance (mode 0, kind 2) twists; the fidgets (kind 12) and the moving modes do not
        tw.active = stance;
    }
}

#[allow(clippy::type_complexity)]
fn grunts(
    mut q: Query<(Entity, &mut Grunt, &mut Animator, &Transform, Option<&crate::topsy::Thrown>), Without<crate::ragdoll::Ragdoll>>,
    targets: Query<&GlobalTransform>,
    time: Res<Time>,
    lib: Res<crate::skin::AnimLibrary>,
    // per grunt: the fidget timer and anim, the run anim; the anim random stream (a level starts them over)
    mut per_level: crate::levelreset::PerLevel<(std::collections::HashMap<Entity, (f32, Option<String>)>, std::collections::HashMap<Entity, u32>, u32)>,
) {
    let (fidgets, runs, seed) = per_level.get();
    let dt = time.delta_secs();
    let mut rand = || {
        // MSVC rand(): 0..32767
        *seed = seed.wrapping_mul(214013).wrapping_add(2531011);
        (*seed >> 16) & 0x7fff
    };
    // every name by the class's anim table (vt+0x1a4: GruntObject 0x0043bd70, BowGruntObject 0x0043abc0,
    // CaptainObject 0x0043b610; ai.rs anim_name_with, by the grunt's state: Grunt::anim)
    let name = |g: &Grunt, id: u32, r: f32| g.anim(id, r);
    // the four-pose sets (PlayAnim4 vt+0x1a0, layers 0 / 1 blended by the aim weights): ids tl, tr, bl, br
    let four = |a: &mut Animator, g: &Grunt, ids: [u32; 4], looping: bool, h: f32, v: f32| {
        a.play(0, name(g, ids[0], 0.0), looping, Mask::All);
        a.set_pair(0, name(g, ids[1], 0.0), h);
        a.play(1, name(g, ids[2], 0.0), looping, Mask::All);
        a.set_pair(1, name(g, ids[3], 0.0), h);
        a.set_weight(1, v);
        a.sync(1, 0);
    };
    for (e, mut g, mut a, t, thrown) in &mut q {
        a.blend = AI_BLEND;
        // a hit reaction: its anim once (the all-fours breathing 0x36 looped until its timer ends, ai.rs), then the
        // state's anims again
        if g.mind != Mind::Dead {
            if let Some((n, kind)) = g.hit.clone() {
                let playing = a.layers.first().is_some_and(|l| l.anim == n);
                if playing && a.finished(0, &lib) {
                    g.hit = None;
                } else {
                    if !playing {
                        a.play(0, &n, kind == 6, Mask::All);
                        a.layers[0].time = 0.0;
                    }
                    a.stop(1);
                    continue;
                }
            }
        } else {
            g.hit = None;
        }
        // thrown by the Topsy Turvy bomb: hanging on, or falling into the sky (ids 113 / 114, 115 / 73)
        if let Some(th) = thrown {
            let (id, looping) = match (th.hangs, th.t < 1.0, th.t < 0.3) {
                (true, true, _) => (113, false),
                (true, false, _) => (114, true),
                (false, _, true) => (115, false),
                (false, _, false) => (73, true),
            };
            a.play(0, name(&g, id, 0.0), looping, Mask::All);
            a.stop(1);
            continue;
        }
        // a pose that holds the grunt (the sniper's attach / detach, state 0x104): once
        if let Some((n, _)) = g.pose {
            let playing = a.layers.first().is_some_and(|l| l.anim == n);
            a.play(0, n, false, Mask::All);
            if !playing {
                a.layers[0].time = 0.0;
            }
            a.stop(1);
            continue;
        }
        // a bully scaring his peasant (grunt_scare, anim 0x7d: bully.rs)
        if g.bully.is_some_and(|b| b.scaring.is_some()) {
            a.play(0, name(&g, 0x7d, 0.0), false, Mask::All);
            a.stop(1);
            continue;
        }
        // turning toward the target before firing (0x0042e050)
        if let (Some((n, _)), Mind::Fire | Mind::Guard) = (g.turn, g.mind) {
            a.play(0, n, false, Mask::All);
            a.stop(1);
            continue;
        }
        let limping = g.limp_pct > 0.0 && g.last_health < g.max_health * g.limp_pct / 100.0;
        // the move anim (0x00427210): limping 0x59; else 8 + (rand * 2 >> 15), drawn once per grunt here (the original
        // keeps a run 8..0xa already playing)
        let run = *runs.entry(e).or_insert_with(|| 8 + (rand() * 2 >> 15));
        let mover = |g: &Grunt| if limping { name(g, 0x59, 0.0) } else { name(g, run, if run == 9 { 0.75 } else { 0.25 }) };
        // a guard firing from his guard state: the reload set at a clip's end (0x29 here, as Reload), else the fire set
        let guard_fire = g.mind == Mind::Guard && g.guard_firing;
        if guard_fire && g.reloading && g.shot_age > g.fire_len {
            a.play(0, name(&g, 0x29, 0.0), false, Mask::All);
            a.stop(1);
            continue;
        }
        // (a guard firing shows what the fire state shows)
        let mind = if guard_fire { Mind::Fire } else { g.mind };
        let (n, looping): (String, bool) = match mind {
            Mind::Dead if g.ragdoll => (name(&g, 73, 0.0).to_owned(), false),
            Mind::Dead => {
                // the death anim Character_Die picked (window, ledge, trench: ai.rs death_name) or 0x50 + n
                let r = (g.death_anim.saturating_sub(1)) as f32 / 6.0;
                (g.death_name.map(str::to_owned).unwrap_or_else(|| name(&g, 0x4f + g.death_anim as u32, r).to_owned()), false)
            }
            Mind::Fire => {
                // the four aim poses blended by the aim weights (fn_00428980, PlayAnim4 vt+0x1a0 with ids
                // 0x23/0x24/0x21/0x22): horizontal h (the grunt turns to face the target: on target 0.5), vertical
                // from the elevation to the target, acos(dz / len) * 57.3 - 90 degrees clamped to +-26.5 (0x005bcf0c /
                // 0x005bcf10): target above -> the top poses
                let v = g
                    .target
                    .and_then(|e| targets.get(e).ok())
                    .map(|tg| {
                        let d = tg.translation() + Vec3::Y * 5.0 - (t.translation + Vec3::Y * 7.0);
                        let deg = (d.y / d.length().max(1e-3)).clamp(-1.0, 1.0).acos().to_degrees() - 90.0;
                        (deg.clamp(-26.5, 26.5) + 26.5) / 53.0
                    })
                    .unwrap_or(0.5);
                // a one-shot fire anim (the sniper, the rocket captain: no +0x484 0x1000) plays once a shot, then the
                // aim set (ids 0..3: the sniper's raised-gun poses) until the next (guess at what state 0xa shows
                // between shots)
                let one_shot = g.kind == crate::ai::Kind::Sniper || g.rocket;
                if one_shot && g.shot_age > g.fire_len {
                    four(&mut a, &g, [0, 1, 2, 3], true, 0.5, v);
                } else {
                    let fresh = one_shot && g.shot_age <= 0.0;
                    four(&mut a, &g, [33, 34, 35, 36], !one_shot, 0.5, v);
                    if fresh {
                        a.layers[0].time = 0.0;
                        a.sync(1, 0);
                    }
                }
                continue;
            }
            // a trench (ai.rs CoverPhase): the run there (the move anim of 0x00427210), 0x66 the duck, 0x65 the
            // breathing (looped), 0x67 the peek, and standing up the stand set 2, 3, 0, 1 at the aim weights 0.5
            Mind::Cover => match g.cover.map(|c| c.phase) {
                Some(CoverPhase::Duck) => (name(&g, 0x66, 0.0).to_owned(), false),
                Some(CoverPhase::Breathe) => (name(&g, 0x65, 0.0).to_owned(), true),
                Some(CoverPhase::Peek) => (name(&g, 0x67, 0.0).to_owned(), false),
                Some(CoverPhase::Spray) => (name(&g, 0x68, 0.0).to_owned(), false),
                Some(CoverPhase::Stand) => {
                    four(&mut a, &g, [0, 1, 2, 3], false, 0.5, 0.5);
                    continue;
                }
                _ => (mover(&g).to_owned(), true),
            },
            // a mounted gun (ai.rs GunPhase): the run there and back, on it the gunner's four corner sets (ids 0x83..0x86
            // fire, 0x87..0x8a idle; PlayAnim4 vt+0x1a0 by the weights +0x580 (0 left .. 1 right) / +0x584 (1 top))
            Mind::Gunner => match g.gun {
                Some(gn) if gn.phase == GunPhase::On => {
                    let base = if gn.firing { 0x83 } else { 0x87 };
                    four(&mut a, &g, [base, base + 1, base + 2, base + 3], true, gn.h, 1.0 - gn.v);
                    continue;
                }
                _ => (mover(&g).to_owned(), true),
            },
            // the reload set (0x2a, 0x2b, 0x28, 0x29 by PlayAnim4): the engine plays one of them (0x29, tr)
            Mind::Reload => (name(&g, 0x29, 0.0).to_owned(), false),
            Mind::Flee => (name(&g, 11, 0.0).to_owned(), true),
            Mind::Chase | Mind::ToMarker | Mind::Wander => (mover(&g).to_owned(), true),
            // a monk's teleport (anim 0x6b monk_turbo) and his cast (0x26)
            Mind::Teleport => (name(&g, 0x6b, 0.0).to_owned(), true),
            Mind::Cast => (name(&g, 0x26, 0.0).to_owned(), false),
            // a squad member: the march (0xe) while the squad moves, else attention (0xf)
            Mind::InSquad => (name(&g, if g.walking { 0xe } else { 0xf }, 0.0).to_owned(), true),
            // a jumper's flight (0xc4 / 0xc5: captain_jumper_forward 0x6b; the glide 0xbd: its set by the aim weights)
            Mind::Jump => match g.jumper.and_then(|j| j.flight) {
                Some(crate::ai::jumper::Flight::Glide) => {
                    four(&mut a, &g, [0x6d, 0x6e, 0x6f, 0x70], true, 0.5, 0.5);
                    continue;
                }
                _ => (name(&g, 0x6b, 0.0).to_owned(), true),
            },
            // state 0x50 walks home with anim 0xa (run_scan)
            Mind::Home => (if limping { name(&g, 0x59, 0.0) } else { name(&g, 0xa, 0.0) }.to_owned(), true),
            // the taunt (ids 0x32..0x34) and the throw (0x27): once
            Mind::Taunt | Mind::Throw => {
                let id = if g.mind == Mind::Throw { 0x27 } else { g.anim_id };
                (name(&g, id, 0.0).to_owned(), false)
            }
            Mind::Guard | Mind::WaitTrigger | Mind::Roam if g.attention => {
                // attention (anim 0xf) and the fidgets (0x0042be80 / 0x00427330): with no target the timer +0x494
                // (15 + rand * 0.000275 s, also at load) counts down; on expiry, standing at attention, one random fidget
                // 0x12..0x1a plays once (18 shiver on a cold level, Game+0x2708 == 1; 19..26 the class's idles and stand
                // scans); when it ends (or the timer runs out again) attention resumes and the timer is redrawn. The
                // cough and shiver anims carry the fx notes that play the grunts' _ic sounds.
                let f = fidgets.entry(e).or_insert_with(|| (15.0 + rand() as f32 * 0.000275, None));
                f.0 -= dt;
                let done = f.1.is_some() && a.finished(0, &lib);
                if f.0 <= 0.0 || done {
                    f.0 = 15.0 + rand() as f32 * 0.000275;
                    f.1 = if f.1.is_some() {
                        None
                    } else {
                        let id = ((rand() * 9) >> 15) + 0x12;
                        let r = rand() as f32 / 32768.0;
                        Some(name(&g, id, r).to_owned())
                    };
                }
                match &f.1 {
                    Some(n) => (n.clone(), false),
                    None => (name(&g, 0xf, 0.0).to_owned(), true),
                }
            }
            // ready: the stand set (2, 3, 0, 1 at 0.5, 0.5); no fidgets (0x00427330 needs anim 0xf playing)
            Mind::Guard | Mind::WaitTrigger | Mind::Roam => {
                four(&mut a, &g, [0, 1, 2, 3], true, 0.5, 0.5);
                continue;
            }
        };
        a.play(0, &n, looping, Mask::All);
        a.stop(1);
    }
}

/// Critters, penguins and sheep (PenguinObject_Update 0x0048f690, shared by both classes; level.rs `Critter`). At load
/// (the property read 0x0048f140) a roaming critter (+0xb60) starts walking one time in two ((rand() x 100) <
/// 0x190000, 5..10 s), else every critter grazes (20..35 s). Walking (state 1): the walk anim (8) until the timer
/// runs out, then grazing with a fresh timer. Grazing (state 2): anim 0x17 at first; each time the anim ends a new
/// one by r = rand() * 100 >> 15: 0x18 below 33, 0x17 below 66, else 0x19; when the timer has run out by then, a
/// roaming critter walks again, the others draw a fresh timer after the pick. Names by species (Slot105): the
/// penguin's 0x17 / 0x19 pngn_scan_chat (its note at 0, `fx fx_penguin`, is the call), 0x18 pngn_scan_chat1; the
/// sheep's 0x17 sheep_graze, 0x18 sheep_scan, 0x19 sheep_bleat (its note `fx FX_Sheep_bleat`), 8 sheep_walk (notes
/// `FX FX_ctr_fs_sheep`, the hoof steps). Timers: the load's rand() x 5 / 32768 + 5 and rand() x 15 / 32768 + 20; the
/// update's random(5, 10) / random(20, 35) (0x0046e2c0, taken as lo + rand() x (hi - lo) / 32768: guess). Any hit
/// kills (0x0048fa20, level.rs) with the death line below.
///
/// Not ported: what GenericAI_Update does while a critter walks (its generic_txt, mover_sheep_roam, played by
/// vt+0x238 at load; the mover's root does not move, so the sheep walk on the spot) and the load's "stamp" (0x004ec410
/// on the mover anim: the sheep placed at a point of it); physics on while grazing (knocked about by blasts); the bomb
/// carrier (a sticky bomb stuck on a penguin: PenguinObject_Grab 0x0048f9c0 holds it 300 s, salutes (0xd), then runs
/// to the nearest visible bad guy within 500 (0x0048f4c0, 0x0042cf40 state 0x3e) and sets it off within 15 of him,
/// else lets it go in 3..6 s and flees; needs sticky.rs to hand over the bomb's linger); the screams of vt+0x160..0x168
/// / 0x268 (Slot88 0x0048fd80 / 0x0048fbc0: scare, sticky, topsy, which reach critters only through the carrier: the
/// scare walks list World+0xe7c, which critters are not in).
fn critters(
    mut q: Query<(&mut Animator, &mut crate::level::Critter, &mut Visibility)>,
    lib: Res<crate::skin::AnimLibrary>,
    // the random stream and the death lines used per species (a level starts them over)
    mut per_level: crate::levelreset::PerLevel<(u32, [Vec<usize>; 2])>,
    time: Res<Time>,
    (mut sounds, tf): (MessageWriter<crate::audio::PlaySound>, Query<&GlobalTransform>),
) {
    use crate::level::CritterState::{Grazing, Walking};
    let (seed, used) = per_level.get();
    let dt = time.delta_secs();
    let mut rand = || {
        *seed = seed.wrapping_mul(214013).wrapping_add(2531011);
        (*seed >> 16) & 0x7fff
    };
    for (mut a, mut c, mut vis) in &mut q {
        a.blend = AI_BLEND;
        let species = c.species;
        let play = |a: &mut Animator, id: u32, looping: bool| {
            a.play(0, species.anim(id), looping, Mask::All);
            a.layers[0].time = 0.0;
        };
        // dead (0x0048fa20 -> GoRagdoll 0x00431310): blown_up (0.97 s, both species; its note screams for the
        // penguin, fx_penguin_blow_up), then land_dead; the body lingers 5 s, then goes (the 0.5 s fade not drawn:
        // guess)
        if let Some(t) = c.dead.as_mut() {
            let first = *t == 0.0;
            *t += dt;
            let t = *t;
            if first {
                play(&mut a, 0x49, false);
                // GoRagdoll's death line: the Dead slot (vt+0x154) is a no, so DeadAtPosition (vt+0x158): one of the
                // species' lines by Audio_PlayRandomSound 0x00493b70, which deals them without repeats until all
                // have played
                let lines = species.death_lines();
                let used = &mut used[species as usize];
                if used.len() >= lines.len() {
                    used.clear();
                }
                let free: Vec<usize> = (0..lines.len()).filter(|i| !used.contains(i)).collect();
                let k = free[(rand() as usize * free.len()) >> 15];
                used.push(k);
                let at = a.joints.first().and_then(|j| tf.get(*j).ok()).map(|j| j.translation()).unwrap_or_default();
                sounds.write(crate::audio::PlaySound::at(lines[k], at));
            } else if t > BLOWN_UP && a.layers.first().is_some_and(|l| l.anim == species.anim(0x49)) {
                play(&mut a, 0x5d, false);
            }
            if t > BLOWN_UP + 5.5 {
                *vis = Visibility::Hidden;
            }
            continue;
        }
        let state = match c.state {
            // the load (0x0048f140)
            None => {
                if c.roams && rand() * 100 < 0x190000 {
                    play(&mut a, 8, true);
                    Walking(rand() as f32 * 5.0 / 32768.0 + 5.0)
                } else {
                    play(&mut a, 0x17, false);
                    Grazing(rand() as f32 * 15.0 / 32768.0 + 20.0)
                }
            }
            Some(Walking(t)) => {
                let t = t - dt;
                if t > 0.0 {
                    Walking(t)
                } else {
                    play(&mut a, 0x17, false);
                    Grazing(random(&mut rand, 20.0, 35.0))
                }
            }
            Some(Grazing(t)) => {
                let t = t - dt;
                if !a.finished(0, &lib) {
                    Grazing(t)
                } else if t < 0.0 && c.roams {
                    let walk = random(&mut rand, 5.0, 10.0);
                    play(&mut a, 8, true);
                    Walking(walk)
                } else {
                    let r = rand() * 100 >> 15;
                    play(&mut a, if r < 33 { 0x18 } else if r < 66 { 0x17 } else { 0x19 }, false);
                    Grazing(if t < 0.0 { random(&mut rand, 20.0, 35.0) } else { t })
                }
            }
        };
        c.state = Some(state);
    }
}

/// The critters' blown-up anim length (pngn_blown_up, sheep_blown_up: 29 frames at 30 per second).
const BLOWN_UP: f32 = 29.0 / 30.0;

/// random(lo, hi) of 0x0046e2c0 (one rand(), guess at the scaling).
fn random(rand: &mut impl FnMut() -> u32, lo: f32, hi: f32) -> f32 {
    lo + rand() as f32 * (hi - lo) / 32768.0
}

/// The companions' anims (their tables 0x004876c0 / 0x0044ef20): run (run_reload while reloading on the move),
/// reload_br, the four fire poses blended by the aim elevation (as the grunts'), else the idle poses.
fn companions(mut q: Query<(&Companion, &mut Animator, &Name)>) {
    for (c, mut a, name) in &mut q {
        a.blend = AI_BLEND;
        let who = if name.as_str() == "molmer" { "molmer" } else { "q111" };
        if c.dead.is_some() {
            a.play(0, &format!("{who}_die"), false, Mask::All);
            a.stop(1);
        } else if let Some((n, _)) = c.hit {
            a.play(0, &format!("{who}_{n}"), false, Mask::All);
            a.stop(1);
        } else if c.tea > 0.0 {
            a.play(0, "q111_drop_tea", false, Mask::All);
            a.stop(1);
        } else if c.throwing.is_some() {
            a.play(0, "molmer_throw_tnt", false, Mask::All);
            a.stop(1);
        } else if c.moving {
            a.play(0, &format!("{who}_{}", if c.reloading > 0.0 { "run_reload" } else { "run" }), true, Mask::All);
            a.stop(1);
        } else if c.reloading > 0.0 {
            a.play(0, &format!("{who}_reload_br"), false, Mask::All);
            a.stop(1);
        } else {
            let kind = if c.firing { "fire" } else { "idle" };
            a.play(0, &format!("{who}_{kind}_tl"), true, Mask::All);
            a.set_pair(0, &format!("{who}_{kind}_tr"), 0.5);
            a.play(1, &format!("{who}_{kind}_bl"), true, Mask::All);
            a.set_pair(1, &format!("{who}_{kind}_br"), 0.5);
            a.set_weight(1, 1.0 - c.aim_v);
            a.sync(1, 0);
        }
    }
}
