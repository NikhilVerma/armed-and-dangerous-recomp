//! The AI's predicted aim point (0x004580a0, anim_01.cpp:2425-2457): where a moving target will be when a projectile of
//! a given speed reaches it. Used by a mounted-gun gunner whose counters ran out together (+0x48c flag 1, ai.rs
//! `Gun::snap`): his aim (0x00428fc0, ai_02.cpp:2056-2066) and his shots (0x004284e0, ai_02.cpp:1733-1745) go at the
//! point led for a bullet speed of 360 (0x43b40000) from the gun's muzzle (the turret's vt+0x148, 0x00497f60), the lead
//! starting from the target's ref_hit_00 (0x004580a0's use-bone flag, 0x00458040), without the aim error.
//!
//! - [`AimBlend`]: the aim weights 0x00428fc0 steps toward the target (the gunner's, and a grunt's on foot, whose
//!   off-target result starts his turn, 0x0042df70).
//! - [`lead_point`]: the target's point plus its velocity x t, t solving the 2D intercept (game x / y, engine x / z):
//!   only when the target moves (horizontal speed > 0.01, K 0x005bcbec), the speed is above 0.01 and the target is not on
//!   the shooter (|d|^2 >= 0.001, the double at 0x005bcef0): b = (d . v) x -2 (K 0x005bcc40), disc = b^2 - (|v|^2 -
//!   speed^2) |d|^2 x 4 (K 0x005bcbe0; 1 when negative), t = 2 |d|^2 / (sqrt(disc) - b).
//! - The aim's height (0x00428fc0): (20 x the target's position height + the led point's height) / 21 (K 0x005bcbe8 =
//!   20, K 0x005bcf14 = 1/21): [`aim_height`].
//! - [`TargetMotion`]: the targets' velocities (0x00458000 reads the body's), measured from their positions frame to
//!   frame for whatever a grunt holds as target.

use super::Grunt;
use bevy::prelude::*;
use std::collections::HashMap;

/// The bullet speed a gunner's lead assumes (0x00428fc0 / 0x004284e0 pass 360.0).
pub const GUN_LEAD_SPEED: f32 = 360.0;

/// 0x004580a0: `target` led by its velocity `vel` for a projectile of `speed` from `from` (engine axes, the lead in the
/// horizontal plane only).
pub fn lead_point(target: Vec3, vel: Vec3, from: Vec3, speed: f32) -> Vec3 {
    let v = Vec2::new(vel.x, vel.z);
    let sq = v.length_squared();
    if sq.sqrt() <= 0.01 || speed <= 0.01 {
        return target;
    }
    let d = Vec2::new(from.x - target.x, from.z - target.z);
    let d2 = d.length_squared();
    if (d2 as f64) < 0.001 {
        return target;
    }
    let b = d.dot(v) * -2.0;
    let mut disc = b * b - (sq - speed * speed) * d2 * 4.0;
    if disc < 0.0 {
        disc = 1.0;
    }
    let r = disc.sqrt() - b;
    let t = 1.0 / (r / (d2 + d2));
    target + Vec3::new(v.x * t, 0.0, v.y * t)
}

/// The led aim's height (0x00428fc0): mostly the target's own position's height.
pub fn aim_height(target_pos_y: f32, led_y: f32) -> f32 {
    (target_pos_y * 20.0 + led_y) * (1.0 / 21.0)
}

/// The aim blend weights (+0x580 horizontal: 0 left .. 1 right, +0x584 vertical: 0 bottom .. 1 top as stored; the
/// four-pose sets read 1 - v) that 0x00428fc0 (vt+0x190) moves toward a target: the gunner's on his gun, a grunt's on
/// foot in the fire cycle and the slot's stand (through the turn 0x0042df70, window 45).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AimBlend {
    pub h: f32,
    pub v: f32,
}

impl Default for AimBlend {
    /// the centre (0x00428980 resets it without a target to aim at; 0x004266b0 off a gun)
    fn default() -> Self {
        AimBlend { h: 0.5, v: 0.5 }
    }
}

impl AimBlend {
    /// 0x00428fc0 (window in degrees): the horizontal weight from the angle between `fwd` and the flat direction from
    /// `from` to the target (inside +-window: 1 - (angle + window) / (2 window), on target; beyond: 1 right or 0 left),
    /// the vertical from the elevation acos(dz / len) - 90 (inside +-26.5: 1 - (e + 26.5) / 53; above: 1, below: 0, off
    /// target). `snap` (+0x48c flag 1) and on target sets them at once, else they step by dt / (2 window x 0.0037037)
    /// without overshooting. No target: toward (0.5, 0.5), on target. Returns whether on target.
    pub fn track(&mut self, fwd: Vec3, from: Vec3, target: Option<Vec3>, window: f32, snap: bool, dt: f32) -> bool {
        let (th, tv, ok) = match target {
            None => (0.5, 0.5, true),
            Some(p) => {
                let d = p - from;
                let flat = d.with_y(0.0).normalize_or_zero();
                let ang = fwd.cross(flat).y.atan2(fwd.dot(flat)).to_degrees();
                let (th, mut ok) = if ang < -window {
                    (1.0, false)
                } else if ang > window {
                    (0.0, false)
                } else {
                    (1.0 - (ang + window) / (2.0 * window), true)
                };
                let e = (d.y / d.length().max(1e-3)).clamp(-1.0, 1.0).acos().to_degrees() - 90.0;
                let tv = if e > 26.5 {
                    ok = false;
                    0.0
                } else if e < -26.5 {
                    ok = false;
                    1.0
                } else {
                    1.0 - (e + 26.5) / 53.0
                };
                (th, tv, ok)
            }
        };
        if snap && ok {
            *self = AimBlend { h: th, v: tv };
            return ok;
        }
        let r = dt / (2.0 * window * 0.003_703_7);
        let step = |c: f32, t: f32| if t < c { (c - r).max(t) } else { (c + r).min(t) };
        self.h = step(self.h, th);
        self.v = step(self.v, tv);
        ok
    }
}

/// The velocities of the objects grunts target, from their positions frame to frame.
#[derive(Resource, Default)]
pub struct TargetMotion {
    last: HashMap<Entity, Vec3>,
    pub vel: HashMap<Entity, Vec3>,
}

impl TargetMotion {
    pub fn velocity(&self, e: Entity) -> Vec3 {
        self.vel.get(&e).copied().unwrap_or(Vec3::ZERO)
    }
}

/// Measures the velocity of every grunt's target.
pub fn track(time: Res<Time>, grunts: Query<&Grunt>, positions: Query<&GlobalTransform>, mut motion: ResMut<TargetMotion>) {
    let dt = time.delta_secs();
    if dt <= 0.0 {
        return;
    }
    let mut seen: HashMap<Entity, Vec3> = HashMap::new();
    for t in grunts.iter().filter_map(|g| g.target) {
        if let Ok(p) = positions.get(t) {
            seen.insert(t, p.translation());
        }
    }
    let mut vel = HashMap::new();
    for (&e, &p) in &seen {
        if let Some(&q) = motion.last.get(&e) {
            vel.insert(e, (p - q) / dt);
        }
    }
    motion.last = seen;
    motion.vel = vel;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn still_target_is_not_led() {
        let t = Vec3::new(100.0, 5.0, 0.0);
        assert_eq!(lead_point(t, Vec3::ZERO, Vec3::ZERO, 360.0), t);
    }

    #[test]
    fn crossing_target_is_met() {
        // a target 300 away crossing at 40 / s: the bullet at 360 / s meets it where it will be
        let (from, t, v) = (Vec3::ZERO, Vec3::new(300.0, 0.0, 0.0), Vec3::new(0.0, 0.0, 40.0));
        let p = lead_point(t, v, from, 360.0);
        let time_target = (p - t).length() / 40.0;
        let time_bullet = (p - from).length() / 360.0;
        assert!((time_target - time_bullet).abs() < 0.01, "{time_target} {time_bullet}");
        assert!(p.z > 0.0);
    }

    #[test]
    fn height_mostly_the_target_s() {
        assert!((aim_height(0.0, 21.0) - 1.0).abs() < 1e-5);
    }
}
