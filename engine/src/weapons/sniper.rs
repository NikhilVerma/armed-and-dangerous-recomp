//! The Cyclops sniper rifle (player_snipergun, PlayerWeapon vtable 0x005ac210; its bullet SniperGunBullet, vtable
//! 0x005a9620).
//!
//! - Fire (vt+0x140 0x004a2480 -> 0x004a2a70): not from the muzzle toward the crosshair as the other guns, but along the
//!   camera's ray: the start is the camera's position moved along its direction to the muzzle's depth (t = dir . muzzle
//!   - dir . camera), the target that start + direction x snipergun_range (Game+0x2a08, 4000); 0x0047de40 launches
//!   p_player_snipergun_bullet at it (speed x 3: 4500 units a second, lifetime 2 s). Its muzzle fx
//!   (fx_muzzle_flash_cyclops) plays the shot's sound (wpn_rome_sniper_fire); recycle 1.5 s, one round a clip, a 2 s
//!   reload (p_player_snipergun; snipergun_max_clip 1, _max_ammo 7).
//! - The bullet goes through characters (its hit 0x0047f2a0): an object of the character list (World +0xe8c) joins the
//!   bullet's hit list (its SetOwner 0x0047f210 put the shooter there first) and the bullet flies on; each such hit
//!   takes the direct damage (100, x5 on the head) and counts when it is an AI (vt+0x10c, +0x588). The first other hit
//!   stops it, and then a count above 2 drops "pickup_health_2", above 1 "pickup_health_1", 15 in front of the player
//!   and 4 up (0x00457870 then the pickup's own fall). player.rs `projectiles` runs the flight with [`Pierce`].
//! - Zoom: the weapon's camera node Camera_sniper (camera.rs).

use super::{GunFired, WeaponId};
use crate::physics::Physics;
use bevy::prelude::*;
use oxijolt::prelude::BodyId;

/// A bullet that goes through characters: the ones already hit (each once) and how many it went through.
#[derive(Component, Default, Debug)]
pub struct Pierce {
    pub hit: Vec<Entity>,
    pub count: u32,
}

/// The pickup a stopped piercing bullet leaves for its count (0x0047f2a0).
pub fn reward(count: u32) -> Option<&'static str> {
    match count {
        n if n > 2 => Some("pickup_health_2"),
        2 => Some("pickup_health_1"),
        _ => None,
    }
}

/// The first hit along a segment past the bodies `skip` names (the grunts' capsules for a piercing bullet).
pub fn ray_past(physics: &Physics, from: Vec3, dir: Vec3, len: f32, skip: impl Fn(BodyId) -> bool) -> Option<(BodyId, Vec3, Vec3)> {
    let (mut at, mut left) = (from, len);
    for _ in 0..32 {
        let h = physics.raycast_bullet(at, dir, left)?;
        if !skip(h.0) {
            return Some(h);
        }
        left -= h.1.distance(at) + 0.05;
        if left <= 0.0 {
            return None;
        }
        at = h.1 + dir * 0.05;
    }
    None
}

/// The shot's start and target along the camera ray (0x004a2a70): the camera point at the muzzle's depth, and that
/// point plus the direction times the range.
pub fn camera_ray_shot(cam: Vec3, dir: Vec3, muzzle: Vec3, range: f32) -> (Vec3, Vec3) {
    let t = dir.dot(muzzle) - dir.dot(cam);
    let start = cam + dir * t;
    (start, start + dir * range)
}

pub struct SniperPlugin;

impl Plugin for SniperPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, fire.after(crate::player::PlayerSet::Weapons).run_if(crate::playing));
    }
}

fn fire(
    mut commands: Commands,
    mut fired: MessageReader<GunFired>,
    cam: Query<&GlobalTransform, With<crate::camera::FollowCam>>,
    data: Res<crate::gamedata::GameData>,
    roman: Query<&crate::player::Roman>,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    for f in fired.read().filter(|f| f.id == WeaponId::Snipergun) {
        let (Ok(c), Ok(r)) = (cam.single(), roman.single()) else { continue };
        let Some(gun) = r.arsenal.gun(WeaponId::Snipergun) else { continue };
        let (start, target) = camera_ray_shot(c.translation(), c.forward().as_vec3(), f.muzzle, gun.spec.range);
        let bullet = data.props(&gun.spec.projectile);
        let e = crate::player::spawn_projectile_to(&mut commands, &mut fx, &bullet, start, target, true, Some(f.owner));
        commands.entity(e).insert(Pierce::default());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 0x004a2a70: the start on the camera ray at the muzzle's depth along it.
    #[test]
    fn shot_along_the_camera_ray() {
        let (s, t) = camera_ray_shot(Vec3::new(0.0, 10.0, 0.0), Vec3::NEG_Z, Vec3::new(3.0, 8.0, -12.0), 4000.0);
        assert_eq!(s, Vec3::new(0.0, 10.0, -12.0));
        assert_eq!(t, Vec3::new(0.0, 10.0, -4012.0));
    }

    /// The reward by the count: 2 -> the small health, 3 and more -> the big one.
    #[test]
    fn rewards() {
        assert_eq!(reward(1), None);
        assert_eq!(reward(2), Some("pickup_health_1"));
        assert_eq!(reward(5), Some("pickup_health_2"));
    }
}
