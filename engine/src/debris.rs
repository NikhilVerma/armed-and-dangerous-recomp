//! Explosion debris that stays: the old remaster's `[persistence] debris_keep` (hook/enhance/persistence.cpp
//! `KeepDebris`, called from the fx object update fn_0041a350 0x0041a350, src/renderer/render_r1.cpp:1065), as typed
//! settings ([`DebrisSettings`], part of the graphics presets).
//!
//! The original gives every debris piece a lifespan and a fade-out (1-12 s, docs/engine-limits.md): a building's
//! rubble is gone seconds after it fell. A kept piece loses both, once, and stays opaque. Kept: fx objects of a
//! `fxo_debri*` template that fall and land, i.e. have full gravity (opcode 0x11 Fx_SetGravity 0x0041de10, scale
//! >= 0.99; 0 = weightless, the fx default) and the ground-hit test (Fx_SetHitGround: `HitActions::ground`). The rest
//! drift or sink in the original and fade within seconds; kept, they hung in the air (the player, 2026-10-12:
//! "building particles are floating in the air"). At most `debris_max` pieces per level are kept (the old remaster's
//! 1000); later ones go as in the original.
//!
//! Remaster addition: `debris_time` above 0 keeps a piece that many seconds from its start (never less than the
//! original's lifespan), then fades it over [`DEBRIS_FADE`] seconds, as corpses.rs does for bodies; 0 keeps it until
//! the level ends (the old remaster's).
//!
//! API: [`DebrisSettings`]; [`DebrisKeeper`] (the level's count, a per-level resource); the fx object update calls
//! [`DebrisKeeper::consider`] on each object every update until it is kept.

use crate::fxvm::object::FxObject;
use crate::levelreset::LevelAppExt;
use bevy::prelude::*;
use serde::{Deserialize, Serialize};

/// The fade at the end of a timed piece (seconds; corpses.rs's Remaster `corpse_fade`).
pub const DEBRIS_FADE: f32 = 1.5;

#[derive(Clone, Copy, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct DebrisSettings {
    /// explosion debris that lands stays (off: it goes after its lifespan, the original's)
    pub debris_keep: bool,
    /// seconds a kept piece stays from its start, then fades (0 = until the level ends)
    pub debris_time: f32,
    /// pieces kept per level; later ones go as in the original
    pub debris_max: u32,
}

impl DebrisSettings {
    pub fn original() -> Self {
        DebrisSettings { debris_keep: false, debris_time: 0.0, debris_max: 1000 }
    }

    /// The old remaster's: kept until the level ends, at most 1000 (and.ini `debris_max`).
    pub fn remaster() -> Self {
        DebrisSettings { debris_keep: true, ..Self::original() }
    }
}

impl Default for DebrisSettings {
    fn default() -> Self {
        Self::remaster()
    }
}

/// The level's kept pieces (KeepDebris's g_debrisKept, reset when the level changes).
#[derive(Resource, Default, Debug)]
pub struct DebrisKeeper {
    pub kept: u32,
}

impl DebrisKeeper {
    /// Whether an object is debris that falls and lands (KeepDebris's tests).
    pub fn is_landing_debris(o: &FxObject) -> bool {
        o.template.get(..9).is_some_and(|p| p.eq_ignore_ascii_case("fxo_debri")) && o.gravity >= 0.99 && o.hit.ground
    }

    /// Keeps `o` when the settings say so and it is landing debris with an end: its lifespan and fade-out go (or,
    /// with `debris_time`, move to that time). Returns whether it was kept now.
    pub fn consider(&mut self, s: &DebrisSettings, o: &mut FxObject) -> bool {
        if !s.debris_keep || o.kept || (o.lifespan.is_none() && o.fade_out.is_none()) || !Self::is_landing_debris(o) {
            return false;
        }
        if s.debris_max > 0 && self.kept >= s.debris_max {
            return false;
        }
        if s.debris_time > 0.0 {
            let life = (s.debris_time + DEBRIS_FADE).max(o.lifespan.unwrap_or(0.0));
            o.lifespan = Some(life);
            o.fade_out = Some(life - DEBRIS_FADE);
        } else {
            o.lifespan = None;
            o.fade_out = None;
        }
        o.kept = true;
        self.kept += 1;
        true
    }
}

pub struct DebrisPlugin;

impl Plugin for DebrisPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<DebrisKeeper>().add_systems(Update, playtest_state.run_if(resource_exists::<crate::playtest::PlayState>));
    }
}

/// The playtest state "debris": pieces kept this level.
fn playtest_state(keeper: Res<DebrisKeeper>, mut state: ResMut<crate::playtest::PlayState>) {
    if keeper.is_changed() {
        state.0.insert("debris".into(), serde_json::json!({ "kept": keeper.kept }));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn piece(template: &str, gravity: f32, ground: bool) -> FxObject {
        let mut o = FxObject::new(template, None, Mat4::IDENTITY, Vec::new(), Vec::new());
        o.lifespan = Some(4.0);
        o.fade_out = Some(3.0);
        o.gravity = gravity;
        o.hit.ground = ground;
        o
    }

    /// Only landing debris is kept: the template, full gravity and the ground test (KeepDebris).
    #[test]
    fn keeps_landing_debris_only() {
        let s = DebrisSettings::remaster();
        let mut k = DebrisKeeper::default();
        assert!(!k.consider(&s, &mut piece("fx_smoke", 1.0, true)));
        assert!(!k.consider(&s, &mut piece("fxo_debris_wood", 0.5, true)));
        assert!(!k.consider(&s, &mut piece("fxo_debris_wood", 1.0, false)));
        let mut o = piece("FXO_Debris_Wood", 1.0, true);
        assert!(k.consider(&s, &mut o));
        assert_eq!((o.lifespan, o.fade_out, k.kept), (None, None, 1));
        // once only
        assert!(!k.consider(&s, &mut o));
        // the Original keeps nothing
        let mut o = piece("fxo_debris_wood", 1.0, true);
        assert!(!DebrisKeeper::default().consider(&DebrisSettings::original(), &mut o));
        assert_eq!(o.lifespan, Some(4.0));
    }

    /// At most debris_max per level.
    #[test]
    fn at_most_max() {
        let s = DebrisSettings { debris_max: 2, ..DebrisSettings::remaster() };
        let mut k = DebrisKeeper::default();
        let kept = (0..5).filter(|_| k.consider(&s, &mut piece("fxo_debris_stone", 1.0, true))).count();
        assert_eq!(kept, 2);
    }

    /// debris_time: the piece stays that long from its start, then fades over DEBRIS_FADE; never shorter than the
    /// original's lifespan.
    #[test]
    fn stays_the_set_time() {
        let s = DebrisSettings { debris_time: 30.0, ..DebrisSettings::remaster() };
        let mut o = piece("fxo_debris_stone", 1.0, true);
        assert!(DebrisKeeper::default().consider(&s, &mut o));
        assert_eq!((o.fade_out, o.lifespan), (Some(30.0), Some(30.0 + DEBRIS_FADE)));
        let short = DebrisSettings { debris_time: 0.5, ..s };
        let mut o = piece("fxo_debris_stone", 1.0, true);
        DebrisKeeper::default().consider(&short, &mut o);
        assert_eq!(o.lifespan, Some(4.0));
    }
}
