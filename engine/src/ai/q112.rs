//! The Q1-12 (`q112`, Q112Object, vtable 0x005a2440): a heavy chaingun robot through the shared grunt states (ai.rs,
//! `Kind::Q112`), with its own parts here (archive/src/ai/ai_01.cpp, ai_04.cpp, ai_06.cpp, audio/audio_01.cpp):
//!
//! - **The swing** (0x00428160, state 0xb; Q112Object_Slot6 0x00435090: +0x5fc 25, +0x600 35; 100 damage and 13000
//!   impact by its name, 0x0043eae0) is the AI's close attack it shares with the grunts and archers: ai/melee.rs.
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

pub struct Q112Plugin;

impl Plugin for Q112Plugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, log.run_if(crate::playing));
    }
}

/// The playtest state's `q112s`: per Q112 its state and fire counters, trail points kept and used, the eye loop.
fn log(q: Query<(Entity, &super::Grunt)>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let mut out = serde_json::Map::new();
    for (e, g) in &q {
        let Some(x) = g.q112.as_ref() else { continue };
        out.insert(
            format!("{}", e.index()),
            serde_json::json!({"mind": format!("{:?}", g.mind), "rounds_left": g.left_rounds, "burst_left": g.left_burst, "pause": g.pause, "trail": x.trail_len(), "crumbs_used": x.crumbs_used, "eye": x.eye.is_some()}),
        );
    }
    if !out.is_empty() || state.0.contains_key("q112s") {
        let any = |k: &str| out.values().any(|v| v[k].as_bool() == Some(true));
        let sum = |k: &str| out.values().map(|v| v[k].as_u64().unwrap_or(0)).sum::<u64>();
        let summary = serde_json::json!({"count": out.len(), "crumbs_used": sum("crumbs_used"), "eye": any("eye"), "each": out});
        state.0.insert("q112s".into(), summary);
    }
}

/// The Q112's own state beside the shared grunt's.
#[derive(Clone, Debug, Default)]
pub struct Q112 {
    trail: VecDeque<Vec3>,
    last_kept: Option<Vec3>,
    /// the eye loop playing (+0xb5c), to stop at its death
    pub eye: Option<crate::audio::SoundId>,
    /// routes planned to a trail point (the playtest log)
    pub crumbs_used: u32,
    /// the target the trail is of
    seen: Option<Entity>,
    /// on a route to a trail point: no new plan until it is walked (the chase plans only without a route,
    /// 0x00470c30)
    pub on_crumb: bool,
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

    /// After a hit (0x00435170): the eye loop's name to start, once, below a quarter of the health. `r` in [0, 1).
    pub fn eye_after_hit(&self, health: f32, max: f32, r: f32) -> Option<&'static str> {
        (self.eye.is_none() && health > 0.0 && health < max * EYE_BELOW).then(|| EYE_LOOPS[(r >= 0.5) as usize])
    }
}

/// Whether a planned route (nav.rs: the nearest reachable point when the goal cannot be reached) gets to the chase's
/// goal: its end within 25 of it (the goal itself is a point within 20 of the target, 0x004295d0). No route at all: no.
pub fn route_reaches(route: Option<&[Vec3]>, goal: Vec3) -> bool {
    route.is_some_and(|r| r.last().is_none_or(|w| (*w - goal).with_y(0.0).length() <= 25.0))
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
