//! The Vindaloo rocket launcher (player_rlvindaloo, PlayerVindaloo vtable 0x005a90a8; its rockets VindalooProjectile,
//! vtable 0x005afc80, template vindaloo_missile).
//!
//! - Lock-on: with the vindaloo in hand the HUD's multi-lock pass always runs (vt+0x16c 0x00433fa0; hud +0x152): up to
//!   four characters inside the view narrowed by autoaim_multitarget_factor_* (x 1.2 upgraded), within vindaloo_range
//!   and in sight, each marked by a ui_multi_lock (targeting.rs, hud.rs `lock_markers`).
//! - Fire (vt+0x140 0x004a2550): a fan of two rockets, four with the upgrade (Game+0xdd0, pickup_upgrade_weapon), from the
//!   muzzle along the camera's facing turned by yaw {+a, -a, +a, -a} and pitch {s + b, s + b, s - b/2, s - b/2}
//!   (a = start_angle_yaw, b = start_angle_pitch, degrees; s = the camera's downward tilt, so the fan leaves level
//!   ground), each launched at start_speed (0x0047e050). Rocket i homes on lock-on i at its ref_hit_00; empty slots are
//!   filled from the earlier ones (slot 3 from 0, 2 from 1 or else 0, 1 from 0), so one lock-on draws every rocket;
//!   with none they fly to the crosshair's point. The sound wpn_rome_vindaloo_fire_2 / _4 (2D) and the shots counted
//!   (Game+0x2718 + 1 / + 3 over the fire's own).
//! - The rocket (0x004805f0, vehicles/rockets.rs `Flight::Vindaloo`): straight for homing_delay_duration (0.4 s), then
//!   steered at its target, faster as it closes (start_speed to max_speed x 3 over full_speed_distance), and when it
//!   starts to move away from its target (passed it) it goes off there: the target takes the direct damage and the
//!   blast (area_damage 50 within 10 / 20) reaches all round; a hit on anything on the way goes off as usual.
//!   fx_trail_vindaloo while it flies, fx_explosion_vindaloo where it ends.
//! - One rocket fan a clip (vindaloo_max_clip 1), 12 spare (pickup_vindaloo_mortars).

use super::targeting::Target;
use super::{GunFired, VindalooUpgrade, WeaponId};
use bevy::prelude::*;

pub struct VindalooPlugin;

impl Plugin for VindalooPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, fire.after(crate::player::PlayerSet::Weapons).run_if(crate::playing));
    }
}

/// The lock-on each rocket takes (0x004a2550's fill of the empty candidate slots).
pub fn assign(locks: &[Target]) -> [Option<Target>; 4] {
    let mut c: [Option<Target>; 4] = std::array::from_fn(|i| locks.get(i).copied());
    if c[3].is_none() {
        c[3] = c[0];
    }
    if c[2].is_none() {
        c[2] = if c[1].is_some() { c[1] } else { c[0] };
    }
    if c[1].is_none() {
        c[1] = c[0];
    }
    c
}

/// Rocket i's direction: the camera's facing turned by its yaw (about the camera's up) and pitch (about its right,
/// up positive), radians.
pub fn fan_direction(fwd: Vec3, right: Vec3, up: Vec3, yaw: f32, pitch: f32) -> Vec3 {
    let d = Quat::from_axis_angle(up, yaw) * fwd;
    let r = Quat::from_axis_angle(up, yaw) * right;
    (Quat::from_axis_angle(r, pitch) * d).normalize_or(fwd)
}

/// The fan's yaw and pitch per rocket (0x004a2550): yaw {a, -a, a, -a}, pitch {s + b, s + b, s - b/2, s - b/2}.
pub fn fan_angles(a: f32, b: f32, s: f32) -> [(f32, f32); 4] {
    [(a, s + b), (-a, s + b), (a, s - b * 0.5), (-a, s - b * 0.5)]
}

#[allow(clippy::too_many_arguments)]
fn fire(
    mut commands: Commands,
    mut fired: MessageReader<GunFired>,
    data: Res<crate::gamedata::GameData>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut rng: ResMut<crate::vehicles::VehicleRng>,
    (roman, cam, targets, upgrade): (Query<&crate::player::Roman>, Query<&GlobalTransform, With<crate::camera::FollowCam>>, Res<super::targeting::Targets>, Res<VindalooUpgrade>),
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    (mut stats, mut log): (ResMut<crate::player::PlayerStats>, ResMut<super::WeaponStats>),
) {
    for f in fired.read().filter(|f| f.id == WeaponId::Vindaloo) {
        let (Ok(r), Ok(c)) = (roman.single(), cam.single()) else { continue };
        let Some(gun) = r.arsenal.gun(WeaponId::Vindaloo) else { continue };
        let (fwd, right, up) = (c.forward().as_vec3(), c.right().as_vec3(), c.up().as_vec3());
        // s: the camera's downward tilt (-m[10] clamped at 0: the sine of the look down)
        let s = (-fwd.y).max(0.0);
        let angles = fan_angles(gun.spec.gun.start_angle_yaw.to_radians(), gun.spec.gun.start_angle_pitch.to_radians(), s);
        let count = if upgrade.0 { 4 } else { 2 };
        // Game+0x2718: 3 more with four rockets, 1 more with two
        stats.shots += if upgrade.0 { 3 } else { 1 };
        sounds.write(crate::audio::PlaySound::ui(if upgrade.0 { "wpn_rome_vindaloo_fire_4" } else { "wpn_rome_vindaloo_fire_2" }));
        let locks = assign(&targets.list);
        for (i, &(yaw, pitch)) in angles.iter().enumerate().take(count) {
            let dir = fan_direction(fwd, right, up, yaw, pitch);
            let lock = locks[i];
            let launch = crate::vehicles::rockets::Launch {
                template: &gun.spec.gun.projectile_name,
                from: f.muzzle,
                // the target point: the lock-on's, else the crosshair's (vt+0x150)
                to: lock.map_or(f.aim, |t| t.point),
                along: Some(dir),
                target: lock.map(|t| (t.entity, Some("ref_hit_00"))),
                owner: Some(f.owner),
                by: crate::fx::Src::Player,
            };
            crate::vehicles::rockets::launch(&mut commands, &mut fx, &data, &mut rng, launch);
            log.vindaloo_rockets += 1;
        }
        info!("EVENT vindaloo: fired {count} rockets, {} lock-ons", targets.list.len());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn t(n: u32) -> Target {
        Target { entity: Entity::from_raw_u32(n).unwrap(), point: Vec3::splat(n as f32), joint: None }
    }

    /// 0x004a2550's fill: one lock-on draws all four rockets; two: 0, 1, 1, 0; three: 0, 1, 2, 0.
    #[test]
    fn rockets_share_the_lock_ons() {
        let e = |c: [Option<Target>; 4]| c.map(|x| x.map(|t| t.point.x as u32));
        assert_eq!(e(assign(&[])), [None; 4]);
        assert_eq!(e(assign(&[t(1)])), [Some(1); 4]);
        assert_eq!(e(assign(&[t(1), t(2)])), [Some(1), Some(2), Some(2), Some(1)]);
        assert_eq!(e(assign(&[t(1), t(2), t(3)])), [Some(1), Some(2), Some(3), Some(1)]);
    }

    /// The fan: rockets 0 / 1 to either side and up by the pitch, 2 / 3 lower; level when the camera looks level.
    #[test]
    fn the_fan_spreads() {
        let a = fan_angles(10f32.to_radians(), 10f32.to_radians(), 0.0);
        let d0 = fan_direction(Vec3::NEG_Z, Vec3::X, Vec3::Y, a[0].0, a[0].1);
        let d1 = fan_direction(Vec3::NEG_Z, Vec3::X, Vec3::Y, a[1].0, a[1].1);
        let d2 = fan_direction(Vec3::NEG_Z, Vec3::X, Vec3::Y, a[2].0, a[2].1);
        assert!(d0.x < 0.0 && d1.x > 0.0, "{d0} {d1}");
        assert!(d0.y > 0.15 && (d0.y - d1.y).abs() < 1e-5);
        assert!(d2.y < 0.0, "the lower pair dips by half the pitch: {d2}");
    }
}
