//! Monks (MonkObject, vtable 0x005aac28): grunt-class AI whose pursuit and hiding-spot search are a teleport flight
//! (MonkObject_Bamf 0x00436bb0, state 0xf0) and whose engage sometimes casts a basket rain (state 0xf1).
//! docs/subsystems/infantry.md section 6.
//! - `Monk::roll`: the special counter +0xb58 (rand 1..3 at init 0x004369a0) and the combat roll vt+0x220 0x00437130.
//! - `bamf_point`: the teleport's destination (five headings 72 degrees apart round the target, 30..200 out, on the
//!   ground).
//! - `baskets`: the rain's five monk_basket projectiles (MonkBasketProjectile, game_07.cpp:2140-2185).
//! Not ported: the twiglet summon (state 0xf2 / 0xf3: the engine has no twiglets; that roll fires instead), the
//! knock-over hang (0xf4), the smoke fx.

use crate::fx::FxStats;
use crate::physics::Physics;
use bevy::prelude::*;

/// What the combat roll chose.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum MonkAct {
    Fire,
    /// n < 200 of 500: cast_spell_1 (anim 0x26), the baskets round the target (state 0xf1)
    Baskets,
    /// n < 50 of 500 (+0x484 0x4000 clear): the twiglets (0xf2): not ported, fires instead
    Twiglets,
}

/// The monk's own state.
#[derive(Clone, Copy, Debug)]
pub struct Monk {
    /// +0xb58: engages left before the next special roll; 0 = the next hiding-spot search teleports
    pub special: u32,
    /// the teleport flight's time (+0x490 ramps 1 s, +0x494 5 s at most)
    pub flight_t: f32,
    /// the cast's baskets sent
    pub cast: bool,
}

impl Monk {
    pub fn new(rng: &mut crate::fxvm::CrtRand) -> Monk {
        Monk { special: 1 + (rng.rand() * 3 >> 15) as u32, flight_t: 0.0, cast: false }
    }

    /// vt+0x220 0x00437130: count down, at 0 re-roll 1..3 and n = rand 500: < 50 twiglets, < 200 baskets, else fire.
    pub fn roll(&mut self, rng: &mut FxStats) -> MonkAct {
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
/// target point, each from start_height above it, falling at velocity_min..max.
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
        let mut m = Monk { special: 2, flight_t: 0.0, cast: false };
        let mut rng = FxStats::default();
        assert_eq!(m.roll(&mut rng), MonkAct::Fire);
        assert_eq!(m.roll(&mut rng), MonkAct::Fire);
        assert_eq!(m.special, 0);
        let _ = m.roll(&mut rng);
        assert!((1..=3).contains(&m.special), "re-rolled 1..3: {}", m.special);
    }

    #[test]
    fn the_flight_ramps_and_slows() {
        let v0 = bamf_velocity(Vec3::ZERO, Vec3::X * 100.0, 0.1, 80.0).length();
        let v1 = bamf_velocity(Vec3::ZERO, Vec3::X * 100.0, 1.5, 80.0).length();
        let v2 = bamf_velocity(Vec3::X * 95.0, Vec3::X * 100.0, 1.5, 80.0).length();
        assert!(v0 < v1 && v2 < v1, "{v0} {v1} {v2}");
    }
}
