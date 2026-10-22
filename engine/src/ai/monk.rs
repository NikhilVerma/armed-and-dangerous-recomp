//! Monks (MonkObject, vtable 0x005aac28): grunt-class AI whose pursuit and hiding-spot search are a teleport flight
//! (MonkObject_Bamf 0x00436bb0, state 0xf0) and whose engage sometimes casts a basket rain (state 0xf1).
//! docs/subsystems/infantry.md section 6.
//! - `Monk::roll`: the special counter +0xb58 (rand 1..3 at init 0x004369a0) and the combat roll vt+0x220 0x00437130.
//! - `bamf_point`: the teleport's destination (five headings 72 degrees apart round the target, 30..200 out, on the
//!   ground).
//! - `baskets`: the rain's five monk_basket projectiles (MonkBasketProjectile, game_07.cpp:2140-2185).
//! - `summon_point` / `twiglet_spots`: the twiglet summon (states 0xf2 / 0xf3, ai_06.cpp:2906-2972): he turns to a point
//!   30..40 ahead, casts cast_spell_2 and, at its end, five idle twiglets stand round him (twiglet.rs `SpawnTwiglet`).
//! - the knock-over hang (0xf4: topsy.rs `monk_hang`), then `bamf`: the teleport away.
//! - the five smoke fx on his sleeves and body (MonkObject_Init 0x004369a0): ai.rs `monk_smoke`.

use crate::fx::FxStats;
use crate::physics::Physics;
use bevy::prelude::*;

/// What the combat roll chose.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum MonkAct {
    Fire,
    /// n < 200 of 500: cast_spell_1 (anim 0x26), the baskets round the target (state 0xf1)
    Baskets,
    /// n < 50 of 500 (+0x484 0x4000 clear): the twiglet summon (0xf2 / 0xf3: cast_spell_2, anim 0x25)
    Twiglets,
}

/// What a cast (`Mind::Cast`) brings: the basket rain (state 0xf1) or the twiglet summon (0xf2 / 0xf3).
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum Spell {
    #[default]
    Baskets,
    Twiglets,
}

/// The monk's own state.
#[derive(Clone, Copy, Debug)]
pub struct Monk {
    /// +0xb58: engages left before the next special roll; 0 = the next hiding-spot search teleports
    pub special: u32,
    /// the teleport flight's time (+0x490 ramps 1 s, +0x494 5 s at most)
    pub flight_t: f32,
    /// the cast's baskets sent (or twiglets made)
    pub cast: bool,
    /// the cast under way and, for the summon, the point he turns to (+0x934, engine space)
    pub spell: Spell,
    pub summon_at: Vec3,
    /// debug (playtest `monk_spell`): the next roll's outcome, whatever the counter
    pub forced: Option<MonkAct>,
    /// MonkObject_Bamf 0x00436bb0 asked for (vt+0x240: the knock-over hang's end, topsy.rs): he teleports away now
    pub bamf: bool,
}

impl Monk {
    pub fn new(rng: &mut crate::fxvm::CrtRand) -> Monk {
        Monk { special: 1 + (rng.rand() * 3 >> 15) as u32, flight_t: 0.0, cast: false, spell: Spell::Baskets, summon_at: Vec3::ZERO, forced: None, bamf: false }
    }

    /// vt+0x220 0x00437130: count down, at 0 re-roll 1..3 and n = rand 500: < 50 twiglets, < 200 baskets, else fire.
    pub fn roll(&mut self, rng: &mut FxStats) -> MonkAct {
        if let Some(act) = self.forced.take() {
            return act;
        }
        if self.special > 0 {
            self.special -= 1;
            return MonkAct::Fire;
        }
        self.special = 1 + (rng.rand() * 3.0) as u32 % 3;
        let n = (rng.rand() * 500.0) as u32;
        if n < 50 {
            MonkAct::Twiglets
        } else if n < 200 {
            MonkAct::Baskets
        } else {
            MonkAct::Fire
        }
    }
}

/// MonkObject_Bamf's destination: from a random heading, five headings 72 degrees apart round `around`, each 30..200
/// out; the first with ground under it (the nav check 0x0046f280 here: a ray down), snapped to it.
pub fn bamf_point(around: Vec3, physics: &Physics, rng: &mut FxStats) -> Option<Vec3> {
    let a0 = rng.rand() * std::f32::consts::TAU;
    (0..5).find_map(|i| {
        let a = a0 + i as f32 * 72f32.to_radians();
        let d = rng.range(30.0, 200.0);
        let p = around + Vec3::new(a.sin(), 0.0, a.cos()) * d;
        physics.raycast(p + Vec3::Y * 300.0, -Vec3::Y, 1000.0).map(|(_, hit, _)| hit)
    })
}

/// State 0xf2: the point the summoner turns to, his facing turned by -90..90 degrees (0x0046e2c0(-pi/2, pi/2)), 30..40
/// out (each axis its own roll, as the original scales the direction's x, y, z apart). `forward`: his facing, engine
/// space, level.
pub fn summon_point(at: Vec3, forward: Vec3, rng: &mut FxStats) -> Vec3 {
    let dir = Quat::from_rotation_y(rng.range(-std::f32::consts::FRAC_PI_2, std::f32::consts::FRAC_PI_2)) * forward.normalize_or(Vec3::Z);
    at + Vec3::new(dir.x * rng.range(30.0, 40.0), dir.y * rng.range(30.0, 40.0), dir.z * rng.range(30.0, 40.0))
}

/// State 0xf3's end: where the five twiglets stand (engine space, on the landscape). The game's +y turned by i x 72 degrees
/// about z, scaled by two radii of 7..20 (one for x, one for y, K 0x005bcd2c x 2 pi), from the summoner's feet; a point
/// with no ground (the map test 0x0046f280: here no landscape under it) is skipped; the rest are dropped onto the
/// landscape (Object_ForceGround 0x00455860 with 1000, 10000).
pub fn twiglet_spots(at: Vec3, physics: &Physics, rng: &mut FxStats) -> Vec<Vec3> {
    let (a, b) = (rng.range(7.0, 20.0), rng.range(7.0, 20.0));
    (0..5)
        .filter_map(|i| {
            let t = i as f32 * 0.2 * std::f32::consts::TAU;
            // game (-sin t, cos t, 0): engine x = game x, engine z = -game y
            let (gx, gy) = (-t.sin(), t.cos());
            let p = at + Vec3::new(gx * a, 0.0, -gy * b);
            physics.raycast_landscape(p + Vec3::Y * 1000.0, Vec3::NEG_Y, 10000.0)
        })
        .collect()
}

/// The teleport flight's velocity toward `goal` (state 0xf0, ai_06.cpp:2980-3049): the speed ramps over the first second,
/// slows within 20 units; `speed` the monk's move_speed x 2 (guess at the force's result).
pub fn bamf_velocity(pos: Vec3, goal: Vec3, t: f32, speed: f32) -> Vec3 {
    let to = goal - pos;
    let d = to.length();
    let ramp = t.min(1.0);
    let slow = (d / 20.0).min(1.0);
    to.normalize_or_zero() * speed * ramp.max(0.2) * slow.max(0.2)
}

/// The basket rain (state 0xf1's event, fn_0047df70): five monk_baskets 72 degrees apart on a circle of 40 round the
/// target point, each from start_height above it, falling at velocity_min..max (MonkBasketProjectile_ReadProps
/// 0x004830e0: +0x460 = rand(velocity_min, velocity_max), start_height +0x588; MonkBasketProjectile_Launch 0x00483180:
/// raised by start_height, velocity (0, 0, -speed)); the hit is the base projectile's (1 damage, impact_force 4690).
pub fn baskets(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, data: &crate::gamedata::GameData, at: Vec3, owner: Entity, rng: &mut FxStats) {
    let p = data.template_props("monk_basket");
    let h = p.f("start_height", 450.0);
    let a0 = rng.rand() * std::f32::consts::TAU;
    for i in 0..5 {
        let a = a0 + i as f32 * 72f32.to_radians();
        let spot = at + Vec3::new(a.sin(), 0.0, a.cos()) * 40.0;
        let mut q = p.clone();
        // (spawn_projectile multiplies the speed by 3, as Projectile_Load)
        q.0.insert("speed".into(), (rng.range(p.f("velocity_min", 150.0), p.f("velocity_max", 200.0)) / 3.0).into());
        crate::player::spawn_projectile(commands, fx, &q, spot + Vec3::Y * h, -Vec3::Y, false, Some(owner));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_counter_runs_down_before_a_special() {
        let mut m = Monk { special: 2, flight_t: 0.0, cast: false, spell: Spell::Baskets, summon_at: Vec3::ZERO, forced: None, bamf: false };
        let mut rng = FxStats::default();
        assert_eq!(m.roll(&mut rng), MonkAct::Fire);
        assert_eq!(m.roll(&mut rng), MonkAct::Fire);
        assert_eq!(m.special, 0);
        let _ = m.roll(&mut rng);
        assert!((1..=3).contains(&m.special), "re-rolled 1..3: {}", m.special);
    }

    /// State 0xf2's point: within 90 degrees of his facing, 30..40 out on each axis.
    #[test]
    fn the_summon_point_is_ahead() {
        let mut rng = FxStats::default();
        for _ in 0..50 {
            let p = summon_point(Vec3::ZERO, Vec3::Z, &mut rng);
            assert!(p.z >= -1e-3 && p.length() <= 40.0 * 1.5 && p.y == 0.0, "{p:?}");
        }
    }

    #[test]
    fn the_flight_ramps_and_slows() {
        let v0 = bamf_velocity(Vec3::ZERO, Vec3::X * 100.0, 0.1, 80.0).length();
        let v1 = bamf_velocity(Vec3::ZERO, Vec3::X * 100.0, 1.5, 80.0).length();
        let v2 = bamf_velocity(Vec3::X * 95.0, Vec3::X * 100.0, 1.5, 80.0).length();
        assert!(v0 < v1 && v2 < v1, "{v0} {v1} {v2}");
    }
}
