//! How an AI gunner paces and aims its shots: the volley rhythm of the turret bosses (Q112Boss_Slot102 0x00435940
//! with the turret gunner 0x0042ecf0) and the aim error of the AI's shot (0x004284e0). Pure logic, no ECS: the units
//! own one of each and step it; the numbers come from the unit's property set.

use super::VehicleRng;
use bevy::math::Vec3;

/// A range to roll in (inclusive), as the property sets give them (`*_min` / `*_max`).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Span<T> {
    pub min: T,
    pub max: T,
}

impl Span<f32> {
    pub fn roll(self, rng: &mut VehicleRng) -> f32 {
        rng.range(self.min, self.max)
    }
}

impl Span<i64> {
    pub fn roll(self, rng: &mut VehicleRng) -> i64 {
        rng.count(self.min, self.max)
    }
}

/// What a step of the fire cycle asks for.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Step {
    /// fire one shot now
    pub shoot: bool,
    /// this shot ended a volley
    pub volley_done: bool,
}

/// The volley rhythm: a pause before each volley, a pause between its shots, a rolled number of shots per volley.
/// Q112Boss_Slot102 0x00435940: the volley pause counts down, then the shot pause, then a shot; a spent volley rolls a
/// new pause and a new count. The turret gunner 0x0042ecf0 adds a rest after each volley (random over the same range
/// as the volley pause) unless the gunner is walking or standing to fire (the colossus).
#[derive(Clone, Debug)]
pub struct FireCycle {
    /// fire_delay_min / max: between the shots of a volley
    pub shot_pause: Span<f32>,
    /// fire_burst_delay_min / max: before a volley (0x00435420 moves it into the volley slot)
    pub volley_pause: Span<f32>,
    /// fire_num_bursts_min / max: shots per volley
    pub shots: Span<i64>,
    rest: f32,
    volley_wait: f32,
    shot_wait: f32,
    left: i64,
    /// volleys fired at the current target (stop_volley_count compares with it)
    pub volleys: u32,
}

impl FireCycle {
    /// A new cycle: the first volley pause and count rolled (0x00435420).
    pub fn new(shot_pause: Span<f32>, volley_pause: Span<f32>, shots: Span<i64>, rng: &mut VehicleRng) -> Self {
        let volley_wait = volley_pause.roll(rng);
        let left = shots.roll(rng);
        FireCycle { shot_pause, volley_pause, shots, rest: 0.0, volley_wait, shot_wait: 0.0, left, volleys: 0 }
    }

    /// The cycle starts over for a new target (0x00435b50 on dropping one: the volley count reset, a new shot count).
    pub fn new_target(&mut self, rng: &mut VehicleRng) {
        self.volleys = 0;
        self.left = self.shots.roll(rng);
    }

    /// One frame with a target. `rests` is false while the gunner walks or stands to fire (states 0x10f / 0x110).
    pub fn step(&mut self, dt: f32, rests: bool, rng: &mut VehicleRng) -> Step {
        let idle = Step { shoot: false, volley_done: false };
        if self.rest > 0.0 {
            self.rest -= dt;
            if self.rest <= 0.0 {
                // 0x00427600: the counts rolled again
                self.left = self.shots.roll(rng);
            }
            return idle;
        }
        if self.volley_wait > 0.0 {
            self.volley_wait -= dt;
            return idle;
        }
        if self.shot_wait > 0.0 {
            self.shot_wait -= dt;
            return idle;
        }
        self.left -= 1;
        if self.left > 0 {
            self.shot_wait = self.shot_pause.roll(rng);
            return Step { shoot: true, volley_done: false };
        }
        self.volley_wait = self.volley_pause.roll(rng);
        self.volleys += 1;
        self.left = self.shots.roll(rng);
        if rests {
            self.rest = self.volley_pause.roll(rng);
        }
        Step { shoot: true, volley_done: true }
    }

    /// A volley is under way (no retargeting then: 0x00435b50).
    pub fn mid_volley(&self) -> bool {
        self.volley_wait <= 0.0 && self.rest <= 0.0
    }
}

/// The AI's aim error (0x004284e0): the target's hit point moved sideways by up to `spread` and up by up to `rise`,
/// both scaled by the distance in the plane: 0.1 within `accurate_within`, 1 from `wild_beyond` on, between them
/// max(0.1, (d^2 - near^2) / (far^2 - near^2)). Properties fire_inaccuracy_angle (15), fire_inaccuracy_height (10),
/// fire_accuracy_min / max (50 / 150).
#[derive(Clone, Copy, Debug)]
pub struct AimError {
    pub spread: f32,
    pub rise: f32,
    pub accurate_within: f32,
    pub wild_beyond: f32,
}

impl AimError {
    pub fn of(p: &crate::gamedata::Props) -> Self {
        AimError {
            spread: p.f("fire_inaccuracy_angle", 15.0),
            rise: p.f("fire_inaccuracy_height", 10.0),
            accurate_within: p.f("fire_accuracy_min", 50.0),
            wild_beyond: p.f("fire_accuracy_max", 150.0),
        }
    }

    /// The scale of the error at a distance in the plane.
    pub fn scale(&self, d: f32) -> f32 {
        let (lo, hi) = (self.accurate_within, self.wild_beyond);
        if d <= lo {
            0.1
        } else if d >= hi {
            1.0
        } else {
            ((d * d - lo * lo) / (hi * hi - lo * lo)).max(0.1)
        }
    }

    /// The point a shot from `from` at `hit` is aimed at (engine axes, Y up).
    pub fn aim(&self, from: Vec3, hit: Vec3, rng: &mut VehicleRng) -> Vec3 {
        let to = (hit - from).with_y(0.0);
        let s = self.scale(to.length());
        let side = Vec3::new(-to.z, 0.0, to.x).normalize_or_zero();
        hit + side * rng.range(-self.spread, self.spread) * s + Vec3::Y * rng.range(0.0, self.rise) * s
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rng() -> VehicleRng {
        VehicleRng(crate::fxvm::CrtRand::seeded(1))
    }

    /// Three shots a volley, 1 s apart, 4 s before each volley: the shots land at 4, 5, 6 then 10, 11, 12 (plus the
    /// rest when the gunner rests: another 4 s).
    #[test]
    fn volley_rhythm() {
        let mut r = rng();
        let mut c = FireCycle::new(Span { min: 1.0, max: 1.0 }, Span { min: 4.0, max: 4.0 }, Span { min: 3, max: 3 }, &mut r);
        let dt = 0.01;
        let mut shots = Vec::new();
        for i in 0..1300 {
            let s = c.step(dt, false, &mut r);
            if s.shoot {
                shots.push(((i as f32 * dt * 10.0).round() / 10.0, s.volley_done));
            }
        }
        let times: Vec<f32> = shots.iter().map(|s| s.0).collect();
        let want = [4.0, 5.0, 6.0, 10.0, 11.0, 12.0];
        assert!(times.len() == want.len() && times.iter().zip(want).all(|(t, w)| (t - w).abs() <= 0.15), "{shots:?}");
        assert!(shots[2].1 && !shots[1].1);
        assert_eq!(c.volleys, 2);
        // resting: a second pause after each volley
        let mut c = FireCycle::new(Span { min: 1.0, max: 1.0 }, Span { min: 4.0, max: 4.0 }, Span { min: 3, max: 3 }, &mut r);
        let n = (0..1300).filter(|_| c.step(dt, true, &mut r).shoot).count();
        assert_eq!(n, 3, "at 4, 5, 6 s, then the rest (4) and the pause (4): the next at 14 s");
    }

    #[test]
    fn aim_error_scales_with_distance() {
        let a = AimError { spread: 15.0, rise: 10.0, accurate_within: 50.0, wild_beyond: 150.0 };
        assert_eq!(a.scale(10.0), 0.1);
        assert_eq!(a.scale(500.0), 1.0);
        assert!((a.scale(100.0) - 0.375).abs() < 1e-6);
    }
}
