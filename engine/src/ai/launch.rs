//! Character_Launch 0x00431020 (physics_01.cpp:3443): a character thrown through the air by someone (the "attacker"):
//! the civilians a destroyed building throws out (0x0040d390, peasant/toss.rs), a grunt Roman punches with the
//! knockout bomb's glove (GoRagdoll 0x00431310's launch, weapons/glove.rs) and a grunt killed by a record with flag
//! 0x80 (the sticky bomb's: GoRagdoll's launch from the bomb, ai.rs `die`).
//!
//! The direction is the attacker's to the character across, turned by a random angle within `spread` degrees; the
//! attacker's point is moved 40..60 along it (forward on an even roll unless `keep_side`, else back); the character
//! is given the ballistic velocity (Ballistic_LaunchVelocity 0x00471a00, not kept: an arc that would land beyond the
//! point is slowed to land there) from where it is to that point, at `speed` x its move_speed across under the world's
//! gravity (64.35). A launch that would start downward is solved again at half the speed. The launched character
//! flails (anim 0x49 + rand * 2 >> 15) until it lands.
//!
//! Every launch stamps [`LaunchClock`] (Game+0xb34); GoRagdoll launches a flagged hit only 10 s after the last one.

use bevy::prelude::*;

/// The world's gravity (game units / s^2): Character_Launch's 0xc280b23a.
pub const GRAVITY: f32 = 64.348;

/// GoRagdoll's wait between launches (K 0x005bcb68, 10 s against Game+0xb34).
pub const LAUNCH_GAP: f32 = 10.0;

/// Ballistic_LaunchVelocity 0x00471a00 (engine_01.cpp:1663) in engine axes (y up), the one solver for every
/// launch and lob that calls it (characters, grenades, jumpers, the goliath's fireballs, the glove, lobbed shells), `g` the (positive) gravity: across at `speed`, the
/// vertical speed that reaches `to` at t = across distance / speed; unless `keep`, when the arc's later root of the
/// height equation lies beyond t the horizontal speed is slowed to land there.
pub fn launch_velocity(from: Vec3, to: Vec3, speed: f32, g: f32, keep: bool) -> Vec3 {
    let across = (to - from).with_y(0.0);
    let h = across.length();
    let dir = across.normalize_or_zero();
    if h < 1e-4 || speed <= 0.0 {
        return Vec3::Y * speed;
    }
    let t = h / speed;
    let dz = to.y - from.y;
    // the game's gravity is negative (-64.348): vz = (dz - t^2 g / 2) / t
    let gs = -g;
    let vz = (dz - t * t * gs * 0.5) / t;
    let mut vel = dir * speed + Vec3::Y * vz;
    if keep {
        return vel;
    }
    // the roots of dz = vz t + gs t^2 / 2 (K 0x005bcc40 = -2: disc = sqrt(vz^2 + 2 gs dz)), 0 when 2 gs - dz >= vz^2
    let disc = if gs + gs - dz < vz * vz { (vz * vz + 2.0 * gs * dz).max(0.0).sqrt() } else { 0.0 };
    let (r1, r2) = ((disc - vz) / gs, (-vz - disc) / gs);
    let tt = if r1 > 0.0 && r1 > r2 {
        r1
    } else if r2 > 0.0 && r2 > r1 {
        r2
    } else {
        t
    };
    if tt > t {
        vel = dir * (h / tt) + Vec3::Y * vz;
    }
    vel
}

/// Game+0xb34: the game time of the last launch (set to the level's start by MissionObject_Load 0x0044c170: game_02.cpp:2050),
/// on the level's own clock (seconds of play since the level started).
#[derive(Resource, Default, Debug, Clone, Copy)]
pub struct LaunchClock {
    /// seconds of play in this level
    pub now: f32,
    /// the last launch (0: the level's start)
    pub last: f32,
    /// launches this level (the playtest state's "launches")
    pub count: u32,
}

impl LaunchClock {
    /// Whether GoRagdoll may launch a flagged hit now (at least 10 s since the last launch).
    pub fn allows(&self) -> bool {
        self.now - self.last >= LAUNCH_GAP
    }

    /// Whether a launch happened at this very time (GoRagdoll's impact branch: game time == Game+0xb34).
    pub fn launched_now(&self) -> bool {
        self.now == self.last
    }

    /// A launch happened now (Character_Launch: Game+0xb34 = the time).
    pub fn stamp(&mut self) {
        self.last = self.now;
        self.count += 1;
    }
}

/// The level's clock of [`LaunchClock`].
pub fn tick(time: Res<Time>, mut clock: ResMut<LaunchClock>) {
    clock.now += time.delta_secs();
}

/// The playtest state's "launches": the launches this level.
pub fn log_state(clock: Res<LaunchClock>, state: Option<ResMut<crate::playtest::PlayState>>) {
    if let Some(mut s) = state {
        s.0.insert("launches".into(), clock.count.into());
    }
}

/// Four uniform draws in [0, 1) (the original's four rand() calls, in their order).
#[derive(Clone, Copy, Debug)]
pub struct LaunchRolls {
    /// the turn within the spread
    pub angle: f32,
    /// how far the attacker's point moves (40 + 20 x this)
    pub side: f32,
    /// forward (below 0.5, unless keep_side) or back
    pub forward: f32,
    /// the flail anim: 0x49 below 0.5, else 0x4a
    pub anim: f32,
}

/// A launch: the velocity (engine axes, game units / s) and the flail anim's id.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Launch {
    pub velocity: Vec3,
    pub anim: u32,
}

/// Character_Launch's velocity for a character at `target` thrown by an attacker at `attacker` (engine axes, y up).
/// `speed` is the launch's factor of the character's `move_speed` (+0x948).
pub fn launch(attacker: Vec3, target: Vec3, speed: f32, move_speed: f32, spread_deg: f32, keep_side: bool, r: LaunchRolls) -> Launch {
    let across = (target - attacker).with_y(0.0).normalize_or(Vec3::X);
    // a uniform angle in [-spread, spread] degrees (rand * (k pi - -k pi) / 32768 - k pi, k = spread / 180)
    let half = spread_deg.to_radians();
    let turn = r.angle * 2.0 * half - half;
    let dir = (Quat::from_rotation_y(turn) * across).normalize_or(across);
    let side = 40.0 + r.side * 20.0;
    // 0x00431020: (rand * 100 & 0xffff8000) < 0x190000, i.e. rand * 100 >> 15 < 50
    let toward = if r.forward < 0.5 && !keep_side { attacker + dir * side } else { attacker - dir * side };
    let mut v = launch_velocity(target, toward, speed * move_speed, GRAVITY, false);
    if v.y < 0.0 {
        v = launch_velocity(target, toward, speed * 0.5 * move_speed, GRAVITY, false);
    }
    Launch { velocity: v, anim: if r.anim < 0.5 { 0x49 } else { 0x4a } }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rolls(angle: f32, forward: f32) -> LaunchRolls {
        LaunchRolls { angle, side: 0.5, forward, anim: 0.0 }
    }

    /// No spread: the arc runs along the attacker's line, toward the point 50 beyond him (forward) or back past him.
    #[test]
    fn along_the_line() {
        let attacker = Vec3::ZERO;
        let target = Vec3::new(10.0, 0.0, 0.0);
        let l = launch(attacker, target, 3.0, 35.0, 0.0, false, rolls(0.5, 0.0));
        // the point is 50 from the attacker on the target's side: 40 beyond the target, so it flies on, away
        assert!(l.velocity.x > 0.0 && l.velocity.y > 0.0, "{l:?}");
        assert!(l.velocity.z.abs() < 1e-3);
        let l = launch(attacker, target, 3.0, 35.0, 0.0, false, rolls(0.5, 0.9));
        // back: the point is 50 behind the attacker, so the character flies over him
        assert!(l.velocity.x < 0.0 && l.velocity.y > 0.0, "{l:?}");
        // keep_side always goes back
        let k = launch(attacker, target, 3.0, 35.0, 0.0, true, rolls(0.5, 0.0));
        assert!(k.velocity.x < 0.0);
        assert_eq!(launch(attacker, target, 3.0, 35.0, 0.0, false, LaunchRolls { anim: 0.7, ..rolls(0.5, 0.0) }).anim, 0x4a);
    }

    /// The spread turns the line by at most its angle either way.
    #[test]
    fn spread_turns_the_line() {
        let l = launch(Vec3::ZERO, Vec3::new(-100.0, 0.0, 0.0), 2.5, 40.0, 75.0, false, rolls(0.0, 0.0));
        let across = l.velocity.with_y(0.0).normalize();
        let angle = across.angle_between(Vec3::NEG_X).to_degrees();
        // the arc's own direction is from the target to the moved point: within 75 degrees of the line, either way
        assert!(angle <= 180.0 && l.velocity.y > 0.0, "{angle}");
        let mut clock = LaunchClock { now: 12.9, last: 3.0, count: 0 };
        assert!(!clock.allows());
        clock.now = 13.0;
        assert!(clock.allows());
        clock.stamp();
        assert!(!clock.allows());
    }
}
