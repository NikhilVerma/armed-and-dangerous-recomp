//! The player's weapon roster beyond the bullets: what Roman carries ([`arsenal`]), each weapon's typed data
//! ([`spec`]), the HUD's target passes ([`targeting`]: auto-aim, the shark gun's prey, the vindaloo's lock-ons), and one
//! module per weapon with its own projectile and its effect on the world:
//!
//! | weapon | module | original |
//! |---|---|---|
//! | sniper rifle | [`sniper`] | fire along the camera ray 0x004a2480 / 0x004a2a70, the piercing SniperGunBullet 0x0047f2a0 |
//! | Gurner mortar | [`mortar`] | PlayerWeapon fire 0x004a23e0, GurnerProjectile (launch 0x00480ec0, update 0x00483210) |
//! | Vindaloo launcher | [`vindaloo`] | the fan 0x004a2550, VindalooProjectile 0x00480510 / 0x004805f0 |
//! | Land Shark gun | [`landshark`] | fire 0x004a2910 / 0x004a2950, SharkGunProjectile 0x0047f6d0..0x0047fc90 |
//! | black hole | [`blackhole`] | BlackHoleBox 0x00482120..0x004822b0, BlackHoleProjectile 0x00481db0 / 0x00481ef0 |
//! | Guy Fawkes traitor bomb | [`traitor`] | the bomb (Projectile vtable 0x005a6608), the AI's turn 0x00430140 |
//!
//! Roman's own handling (switching, reloading, the recycle gate, the anims) stays in player.rs, which writes a
//! [`GunFired`] for each shot of a launcher or the sniper rifle and a [`ThrowSpecial`] when a queued special leaves his
//! hand; the bullets of the rifle and the machine gun are player.rs's projectiles.

pub mod arsenal;
pub mod blackhole;
pub mod landshark;
pub mod mortar;
pub mod sniper;
pub mod spec;
pub mod targeting;
pub mod traitor;
pub mod vindaloo;

pub use arsenal::{Arsenal, Gun, SpecialId, WeaponId};
pub use spec::GunSpec;

use crate::levelreset::LevelAppExt;
use bevy::prelude::*;

/// A shot of a gun whose projectile its own module makes (player.rs's fire, vt+0x140 of the gun's class): from the
/// muzzle toward the aim point (the crosshair's, hud +0x88).
#[derive(Message, Clone, Copy, Debug)]
pub struct GunFired {
    pub id: WeaponId,
    pub owner: Entity,
    /// the emitter bone's point (ref_shoot_00)
    pub muzzle: Vec3,
    /// where the crosshair's ray meets the world (or far along it)
    pub aim: Vec3,
}

/// A queued special leaves Roman's hand (the throw anim's "fire" note: 0x00475ff0 state 5 -> 0x004a1970).
#[derive(Message, Clone, Copy, Debug)]
pub struct ThrowSpecial(pub SpecialId);

/// The special weapon queue (+0x1c4 id, +0x230 cooldown: 0x004a1930 queues with 0.4 s, 0x004a1890 counts it down,
/// 0x004a1970 fires): Fire Special queues the thrown specials (sticky bomb, black hole, Guy Fawkes); they leave the
/// hand on the throw anim's note, 0.267 s in (trotter_throw / trotter_run_throw).
#[derive(Resource, Default)]
pub struct ThrowQueue {
    pub pending: Option<(SpecialId, f32)>,
    /// Game+0xae0: the Guy Fawkes bomb's 10 s between uses (0x004788a0's case 6)
    pub fawkes_wait: f32,
}

impl ThrowQueue {
    pub fn busy(&self) -> bool {
        self.pending.is_some()
    }
}

/// When a queued throw leaves the hand: the throw anim's "fire" note (game_05.cpp:1579; the 0.4 s queue of 0x004a1930
/// is only a fallback).
pub const THROW_NOTE: f32 = 0.267;

/// Game+0xdd0 vindaloo_is_upgraded: the weapon upgrade pickup (0x00472770) doubles the vindaloo's fan to four
/// rockets and widens its lock-on view (HudFrustum_Build 0x00408390: x 1.2); the mission resets it
/// (MissionObject_Load: vindaloo_is_upgraded).
#[derive(Resource, Default, Clone, Copy, Debug)]
pub struct VindalooUpgrade(pub bool);

/// The order other modules hook into: the queued throws released (their `ThrowSpecial`s readable after it).
#[derive(SystemSet, Clone, Debug, PartialEq, Eq, Hash)]
pub enum WeaponSet {
    Throws,
}

pub struct WeaponsPlugin;

impl Plugin for WeaponsPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<ThrowQueue>()
            .init_level_resource::<VindalooUpgrade>()
            .init_level_resource::<targeting::Targets>()
            .add_message::<GunFired>()
            .add_message::<ThrowSpecial>()
            .add_systems(Update, release_throws.in_set(WeaponSet::Throws).after(crate::player::PlayerSet::Weapons).run_if(crate::playing))
            .add_plugins((targeting::TargetingPlugin, sniper::SniperPlugin, mortar::MortarPlugin, vindaloo::VindalooPlugin, landshark::LandSharkPlugin, blackhole::BlackHolePlugin, traitor::TraitorPlugin));
    }
}

/// Counts the queue down; at the note the special leaves the hand (each special's module makes it and spends one).
fn release_throws(time: Res<Time>, mut q: ResMut<ThrowQueue>, mut out: MessageWriter<ThrowSpecial>) {
    let dt = time.delta_secs();
    q.fawkes_wait = (q.fawkes_wait - dt).max(0.0);
    let Some((s, left)) = q.pending.as_mut() else { return };
    *left -= dt;
    if *left <= 0.0 {
        let s = *s;
        q.pending = None;
        out.write(ThrowSpecial(s));
    }
}
