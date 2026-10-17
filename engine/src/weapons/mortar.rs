//! The Gurner personal mortar (player_rlgurner, PlayerWeapon vtable 0x005ae210): each shot (vt+0x140 0x004a23e0 ->
//! 0x0047dbf0, no target: vt+0x16c is 0x00435250, false) lobs a gurner_rocket (GurnerProjectile, vtable 0x005a4c38)
//! from the muzzle at the crosshair's point (gurner_range 600 when nothing is under it): the forge cannon's launch
//! 0x00480ec0 (the horizontal speed the distance + 5, about a second's flight, under gravity: vehicles/rockets.rs),
//! the standard hit 0x0047eae0 (area damage 100 within 40 / 60, the direct damage, fx_explosion_turret_mortar).
//! One round a clip, 6 spare (gurner_max_clip / _max_ammo); Roman's recoil set and kick as every launcher (player.rs).

use super::{GunFired, WeaponId};
use bevy::prelude::*;

pub struct MortarPlugin;

impl Plugin for MortarPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, fire.after(crate::player::PlayerSet::Weapons).run_if(crate::playing));
    }
}

fn fire(
    mut commands: Commands,
    mut fired: MessageReader<GunFired>,
    data: Res<crate::gamedata::GameData>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut rng: ResMut<crate::vehicles::VehicleRng>,
    roman: Query<&crate::player::Roman>,
) {
    for f in fired.read().filter(|f| f.id == WeaponId::Gurner) {
        let Some(template) = roman.single().ok().and_then(|r| r.arsenal.gun(WeaponId::Gurner)).map(|g| g.spec.gun.projectile_name.clone()) else { continue };
        let launch = crate::vehicles::rockets::Launch { template: &template, from: f.muzzle, to: f.aim, along: None, target: None, owner: Some(f.owner), by: crate::fx::Src::Player };
        crate::vehicles::rockets::launch(&mut commands, &mut fx, &data, &mut rng, launch);
    }
}
