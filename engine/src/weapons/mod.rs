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
            .init_level_resource::<WeaponStats>()
            .init_level_resource::<targeting::Targets>()
            .add_message::<GunFired>()
            .add_message::<ThrowSpecial>()
            .add_systems(Update, release_throws.in_set(WeaponSet::Throws).after(crate::player::PlayerSet::Weapons).run_if(crate::playing))
            .add_systems(Update, (count_shots.after(crate::player::PlayerSet::Weapons), log_weapons).run_if(crate::playing))
            .add_plugins((targeting::TargetingPlugin, sniper::SniperPlugin, mortar::MortarPlugin, vindaloo::VindalooPlugin, landshark::LandSharkPlugin, blackhole::BlackHolePlugin, traitor::TraitorPlugin));
    }
}

/// What the weapons did this level (the state log's "weapons": playtest expect steps).
#[derive(Resource, Default, Debug, Clone)]
pub struct WeaponStats {
    /// shots of the guns with their own projectiles, by mission name
    pub fired: std::collections::BTreeMap<&'static str, u32>,
    /// the sniper: the most characters one bullet went through, health pickups it left
    pub pierced: u32,
    pub health_drops: u32,
    pub vindaloo_rockets: u32,
    pub shark_dives: u32,
    pub shark_bites: u32,
    pub sharks_gone: u32,
    pub holes: u32,
    pub carried: u32,
    pub swallowed: u32,
    pub turned: u32,
}

fn count_shots(mut fired: MessageReader<GunFired>, mut stats: ResMut<WeaponStats>) {
    for f in fired.read() {
        *stats.fired.entry(f.id.name()).or_default() += 1;
    }
}

/// The state log's "weapons".
#[allow(clippy::type_complexity)]
fn log_weapons(
    stats: Res<WeaponStats>,
    state: Option<ResMut<crate::playtest::PlayState>>,
    (targets, upgrade, queue): (Res<targeting::Targets>, Res<VindalooUpgrade>, Res<ThrowQueue>),
    sharks: Query<&landshark::LandShark>,
    (boxes, holes, suction): (Query<(), With<blackhole::HoleBox>>, Query<&blackhole::BlackHole>, Res<blackhole::Suction>),
    (traitors, grunts): (Query<&crate::ai::Grunt, With<traitor::Traitor>>, Query<(Entity, &crate::ai::Grunt), Without<traitor::Traitor>>),
    hunted: Query<(), With<landshark::Hunted>>,
) {
    let Some(mut state) = state else { return };
    let s = &*stats;
    let shark = sharks.iter().next();
    state.0.insert(
        "weapons".into(),
        serde_json::json!({
            "fired": s.fired, "pierced": s.pierced, "health_drops": s.health_drops, "vindaloo_rockets": s.vindaloo_rockets,
            "targets": targets.list.len(), "pass": targets.pass.map(|p| format!("{p:?}")).unwrap_or_default(), "upgraded": upgrade.0,
            "throw_pending": queue.pending.map(|p| p.0.name()).unwrap_or_default(), "fawkes_wait": (queue.fawkes_wait * 10.0).round() / 10.0,
            "sharks": sharks.iter().count(), "shark_phase": shark.map(|s| match s.phase { landshark::Phase::Arc => "arc", landshark::Phase::Swim => "swim", landshark::Phase::Bite { .. } => "bite" }).unwrap_or_default(),
            "shark_depth": shark.map(|s| (s.depth * 10.0).round() / 10.0), "shark_target": shark.is_some_and(|s| s.target.is_some()),
            "shark_dives": s.shark_dives, "shark_bites": s.shark_bites, "sharks_gone": s.sharks_gone, "hunted": hunted.iter().count(),
            "boxes": boxes.iter().count(), "holes": holes.iter().count(), "sucking": suction.active, "holes_opened": s.holes,
            "carried": s.carried, "swallowed": s.swallowed,
            "traitors": traitors.iter().filter(|g| g.mind != crate::ai::Mind::Dead).count(), "turned": s.turned,
            // the two sides at war: traitors hunting a loyal grunt, loyal grunts hunting a traitor
            "traitors_hunting": traitors.iter().filter(|g| g.mind != crate::ai::Mind::Dead && g.target.is_some_and(|t| grunts.contains(t))).count(),
            "loyal_hunting_traitors": grunts.iter().filter(|(_, g)| g.mind != crate::ai::Mind::Dead && g.target.is_some_and(|t| traitors.contains(t))).count(),
        }),
    );
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
