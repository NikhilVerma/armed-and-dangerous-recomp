//! The Q1-12 (`q112`, Q112Object, vtable 0x005a2440): a heavy chaingun robot through the shared grunt states (ai.rs,
//! `Kind::Q112`), with its own parts here (src/ai/ai_02.cpp, ai_04.cpp, ai_05.cpp, ai_06.cpp, audio/audio_01.cpp):
//!
//! - **The swing** (0x00428160, state 0xb; init Q112Object_Slot6 0x00435090: +0x5fc = 25, +0x600 = 35). From the fire
//!   cycle at a burst's or the clip's end (0x00428980, ai_02.cpp:1919 / 1986) and the guard's fire (state 0x79), a
//!   target nearer than 25 (3D) gets the gun's butt:
//!   q112_gun_hit (anim 0x5f / 0x60) looped, facing the target. Its "hit" note (GenericAIObject_HandleEvent 0x0043d180
//!   -> 0x0043eae0 with radius +0x600) hurts a target within 35 (2D) by 100 (the damage of an AI named "q112"; others
//!   50) with an impact of 13000 along the line to it, lifted 0.7..0.9. At each loop's end a target still nearer than 25
//!   is swung at again; else the fire cycle's end (0x0042b8e0, the hiding-spot search) goes on. Only the Q112 sets
//!   +0x5fc: the grunts and archers share the slot (vt+0x1dc) with a range of 0.
//! - **The trail** (the point list +0x958 / pool +0x95c / last point +0x960, made by 0x004246c0 at its init: no other
//!   class has one; 0x00424610 / 0x00424630 / 0x004246a0 / 0x004247a0). While it chases (state 0x3c, ai_06.cpp:1501-1546)
//!   it keeps the target's position each time it is more than 120 (14400 squared, K 0x005bcfac) from the last one kept,
//!   the list cleared past 100 points. When no route to the target can be planned it plans one to the oldest point
//!   kept and drops that point; with no list (any other class) the chase would give up into a guard (0x0042d0f0).
//!   Guess: the list's head (0x004e7340) is its oldest point (it is filled by vt+0x18, an append in the other lists).
//!   The list is cleared when a new target is taken (0x0042bc60 -> 0x004246a0).
//! - **The eye loop** (vt+0xd0 0x00435170): after a hit leaves it below a quarter of its health, once, a loop on it,
//!   fol_q112_eye_1_lp or _2_lp (even odds); its death (Q112Object_Slot53 0x00435220) stops it.
//! - **Bullets** do bullet_scale of their damage (0x00435170 on record flag 0x400; ai.rs `Grunt::bullet_scale`).
//! - **Voice** (its vtable's voice slots): Fight +0x13c 0x0043a200 Q1250006_Fight (the only entry of table 0x005ce630)
//!   under the shared vo_delay, kept as its line; Hurt +0x14c 0x0043c7b0 -> AIVoice_Q12Hurt 0x0043a100 one of seven
//!   q1250012..21_Hurt under vo_delay, kept as its line; Dead +0x154 0x0043a300 -> AIVoice_Q12Dead 0x0043a2b0
//!   q1250015 / 16_Dead, always; its HurtShort, Spotted, Taunt and DeadAtPosition slots are empty (0x00407ff0).

use bevy::prelude::*;
use std::collections::VecDeque;

/// +0x5fc: a target nearer than this (3D) is swung at.
pub const SWING_START: f32 = 25.0;
/// +0x600: the swing's "hit" reaches a target within this (2D), and the swing goes on while it is that near.
pub const SWING_REACH: f32 = 35.0;
/// 0x0043eae0: the damage and impact of a swing by an AI named "q112".
pub const SWING_DAMAGE: f32 = 100.0;
pub const SWING_IMPACT: f32 = 13000.0;
/// anims 0x5f / 0x60 of its table (0x0043c330)
pub const SWING_ANIM: &str = "q112_gun_hit";
/// The "hit" note's time in q112_gun_hit when the scene's anim does not say (its export: 0.667 s).
const SWING_HIT_DEFAULT: f32 = 0.667;
/// A trail point is kept when the target is farther than this from the last one (K 0x005bcfac = 14400, squared).
const TRAIL_STEP: f32 = 120.0;
/// More points than this and the list is cleared first (0x00424630).
const TRAIL_MAX: usize = 100;
/// The eye loop starts below this share of its health (K 0x005bcbe4).
const EYE_BELOW: f32 = 0.25;

pub const FIGHT_LINE: &str = "Q1250006_Fight";
/// 0x0043a100's table, in its order (Pick(700): each a seventh).
pub const HURT_LINES: [&str; 7] = ["q1250014_Hurt", "q1250017_Hurt", "q1250018_Hurt", "q1250019_Hurt", "q1250020_Hurt", "q1250021_Hurt", "q1250012_Hurt"];
/// 0x0043a2b0's table (Pick(200)).
pub const DEAD_LINES: [&str; 2] = ["q1250015_Dead", "q1250016_Dead"];
const EYE_LOOPS: [&str; 2] = ["fol_q112_eye_1_lp", "fol_q112_eye_2_lp"];

/// A swing's "hit" on its target (0x0043eae0's damage message): `dir` the knock's direction (engine axes).
#[derive(Message, Clone, Copy, Debug)]
pub struct SwingHit {
    pub target: Entity,
    pub by: Entity,
    pub dir: Vec3,
}

pub struct Q112Plugin;

impl Plugin for Q112Plugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SwingHit>().add_systems(Update, (hits, log).run_if(crate::playing));
    }
}

/// The swing's damage (0x0043eae0 -> the target's vt+0xd0): Roman takes it through his hit handler (the damage, the
/// hit anim and the knock: PlayerObject_OnHit 0x0047afa0), a companion as a hit by an enemy (his Health; his hit
/// reaction and lines are ai.rs companion_damage's), the attacker offered to him as a target.
fn hits(
    mut swings: MessageReader<SwingHit>,
    mut roman: Query<(&mut crate::player::Roman, &mut crate::fx::Health), Without<super::Companion>>,
    mut companions: Query<(&mut super::Companion, &mut crate::fx::Health), Without<crate::player::Roman>>,
) {
    for s in swings.read() {
        if let Ok((mut r, mut h)) = roman.get_mut(s.target) {
            if h.0 > 0.0 {
                h.0 -= SWING_DAMAGE;
                r.knock(SWING_DAMAGE, SWING_IMPACT, s.dir);
            }
        } else if let Ok((mut c, mut h)) = companions.get_mut(s.target) {
            if h.0 > 0.0 && c.dead.is_none() {
                h.0 -= SWING_DAMAGE;
                c.attacker = Some(s.by);
            }
        }
    }
}

/// The playtest state's `q112s`: per live Q112 its state, swings that hit, trail points kept and used, the eye loop.
fn log(q: Query<(Entity, &super::Grunt)>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let mut out = serde_json::Map::new();
    for (e, g) in &q {
        let Some(x) = g.q112.as_ref() else { continue };
        out.insert(
            format!("{}", e.index()),
            serde_json::json!({"mind": format!("{:?}", g.mind), "rounds_left": g.left_rounds, "bursts_left": g.left_bursts, "pause": g.pause, "hits": x.hits, "trail": x.trail_len(), "crumbs_used": x.crumbs_used, "eye": x.eye.is_some()}),
        );
    }
    if !out.is_empty() || state.0.contains_key("q112s") {
        let any = |k: &str| out.values().any(|v| v[k].as_bool() == Some(true));
        let sum = |k: &str| out.values().map(|v| v[k].as_u64().unwrap_or(0)).sum::<u64>();
        let summary = serde_json::json!({"count": out.len(), "hits": sum("hits"), "crumbs_used": sum("crumbs_used"), "eye": any("eye"), "melee": out.values().any(|v| v["mind"] == "Melee"), "each": out});
        state.0.insert("q112s".into(), summary);
    }
}

/// The Q112's own state beside the shared grunt's.
#[derive(Clone, Debug, Default)]
pub struct Q112 {
    trail: VecDeque<Vec3>,
    last_kept: Option<Vec3>,
    /// the swing: seconds into the current loop of q112_gun_hit
    swing_t: f32,
    /// the eye loop playing (+0xb5c), to stop at its death
    pub eye: Option<crate::audio::SoundId>,
    /// swings that hit (the playtest log)
    pub hits: u32,
    /// routes planned to a trail point (the playtest log)
    pub crumbs_used: u32,
    /// the target the trail is of
    seen: Option<Entity>,
    /// on a route to a trail point: no new plan until it is walked (the chase plans only without a route,
    /// 0x00470c30)
    pub on_crumb: bool,
    /// a burst (or the clip) of its fire cycle just ended: the moment the swing is tried (0x00428980)
    pub burst_over: bool,
}

impl Q112 {
    /// The trail is of one target: another one (or none) clears it.
    pub fn follow_target(&mut self, target: Option<Entity>) {
        if self.seen != target {
            self.seen = target;
            self.forget();
        }
    }

    /// The chase keeps the target's position (0x00424630 from state 0x3c).
    pub fn keep(&mut self, target: Vec3) {
        if self.last_kept.is_some_and(|l| l.distance_squared(target) <= TRAIL_STEP * TRAIL_STEP) {
            return;
        }
        if self.trail.len() > TRAIL_MAX {
            self.trail.clear();
        }
        self.trail.push_back(target);
        self.last_kept = Some(target);
    }

    /// No route to the target: the oldest point kept, taken off the list (0x00424610).
    pub fn next_point(&mut self) -> Option<Vec3> {
        let p = self.trail.pop_front();
        self.crumbs_used += p.is_some() as u32;
        p
    }

    /// A new target: the list cleared (0x004246a0).
    pub fn forget(&mut self) {
        self.trail.clear();
        self.on_crumb = false;
    }

    pub fn trail_len(&self) -> usize {
        self.trail.len()
    }

    /// The swing starts (0x00428160 true).
    pub fn start_swing(&mut self) {
        self.swing_t = 0.0;
    }

    /// One frame of the swing: whether its "hit" note was passed this frame (at `hit_at` into the loop) and whether
    /// the loop ended (`len`, the anim's length).
    pub fn swing(&mut self, dt: f32, hit_at: f32, len: f32) -> (bool, bool) {
        let t0 = self.swing_t;
        self.swing_t += dt;
        let hit = t0 < hit_at && self.swing_t >= hit_at;
        let ended = self.swing_t >= len;
        if ended {
            self.swing_t = 0.0;
        }
        (hit, ended)
    }

    /// After a hit (0x00435170): the eye loop's name to start, once, below a quarter of the health. `r` in [0, 1).
    pub fn eye_after_hit(&self, health: f32, max: f32, r: f32) -> Option<&'static str> {
        (self.eye.is_none() && health > 0.0 && health < max * EYE_BELOW).then(|| EYE_LOOPS[(r >= 0.5) as usize])
    }
}

/// 0x00428160: the swing's test (the class's +0x5fc: only the Q112's is set): the target strictly nearer than 25 (3D).
pub fn reaches(kind: super::Kind, from: Vec3, to: Vec3) -> bool {
    kind == super::Kind::Q112 && from.distance_squared(to) < SWING_START * SWING_START
}

/// Whether a planned route (nav.rs: the nearest reachable point when the goal cannot be reached) gets to the chase's
/// goal: its end within 25 of it (the goal itself is a point within 20 of the target, 0x004295d0). No route at all: no.
pub fn route_reaches(route: Option<&[Vec3]>, goal: Vec3) -> bool {
    route.is_some_and(|r| r.last().is_none_or(|w| (*w - goal).with_y(0.0).length() <= 25.0))
}

/// The "hit" note's time in the swing anim (its export), and the anim's length.
pub fn swing_times(lib: &mut crate::skin::AnimLibrary) -> (f32, f32) {
    match lib.anim(SWING_ANIM) {
        Some(a) => {
            let hit = a.notes.iter().find(|(_, n)| n.split_whitespace().next().is_some_and(|w| w.eq_ignore_ascii_case("hit"))).map(|(t, _)| *t);
            (hit.unwrap_or(SWING_HIT_DEFAULT), a.duration.max(0.1))
        }
        None => (SWING_HIT_DEFAULT, 1.633),
    }
}

/// The swing's knock (0x0043eae0's message): along the line from the Q112 to the target, its height replaced by
/// 0.7..0.9 (`r` in [0, 1)) before normalising. Engine axes (y up).
pub fn knock_dir(from: Vec3, to: Vec3, r: f32) -> Vec3 {
    let flat = (to - from).with_y(0.0);
    Vec3::new(flat.x, 0.7 + 0.2 * r, flat.z).normalize_or(Vec3::Y)
}

/// A line of `table` the way Pick(range) does (each entry 100 of the range): `r` in [0, 1).
pub fn pick(table: &[&'static str], r: f32) -> &'static str {
    table[((r * table.len() as f32) as usize).min(table.len() - 1)]
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 0x00424630: a point every 120 units of the target's way; the oldest is used first; past 100 the list restarts.
    #[test]
    fn the_trail_keeps_a_point_every_120_units() {
        let mut q = Q112::default();
        for x in 0..50 {
            q.keep(Vec3::new(x as f32 * 10.0, 0.0, 0.0));
        }
        // 0, 130 (> 120 from 0), 260, 390
        assert_eq!(q.trail_len(), 4);
        assert_eq!(q.next_point(), Some(Vec3::ZERO));
        assert_eq!(q.next_point(), Some(Vec3::new(130.0, 0.0, 0.0)));
        assert_eq!(q.crumbs_used, 2);
        let mut q = Q112::default();
        for i in 0..=TRAIL_MAX + 1 {
            q.keep(Vec3::new(i as f32 * 200.0, 0.0, 0.0));
        }
        assert_eq!(q.trail_len(), 1);
    }

    /// 0x00428160: only the Q112, strictly nearer than 25 in 3D.
    #[test]
    fn the_swing_starts_within_25() {
        assert!(reaches(super::super::Kind::Q112, Vec3::ZERO, Vec3::new(20.0, 10.0, 0.0)));
        assert!(!reaches(super::super::Kind::Q112, Vec3::ZERO, Vec3::new(25.0, 0.0, 0.0)));
        assert!(!reaches(super::super::Kind::Grunt, Vec3::ZERO, Vec3::new(5.0, 0.0, 0.0)));
        let mut q = Q112::default();
        q.start_swing();
        assert_eq!(q.swing(0.6, 0.667, 1.633), (false, false));
        assert_eq!(q.swing(0.1, 0.667, 1.633), (true, false));
        assert_eq!(q.swing(1.0, 0.667, 1.633), (false, true));
    }

    /// 0x00435170: below a quarter, once.
    #[test]
    fn the_eye_loop_starts_below_a_quarter() {
        let q = Q112::default();
        assert_eq!(q.eye_after_hit(300.0, 1000.0, 0.2), None);
        assert_eq!(q.eye_after_hit(200.0, 1000.0, 0.2), Some("fol_q112_eye_1_lp"));
        assert_eq!(q.eye_after_hit(200.0, 1000.0, 0.7), Some("fol_q112_eye_2_lp"));
        let q = Q112 { eye: Some(crate::audio::SoundId::new()), ..default() };
        assert_eq!(q.eye_after_hit(200.0, 1000.0, 0.2), None);
    }
}
