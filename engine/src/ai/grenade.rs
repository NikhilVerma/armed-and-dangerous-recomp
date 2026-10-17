//! The captain's grenade (template captain_grenade, behaviour proj_grenade: GrenadeProjectile, vtable 0x005a5010, on the
//! Projectile base; its slot table also at 0x005a6608 / 0x005adc78). docs/subsystems/grunt-ai.md section 13.
//!
//! - The throw: the captain's engage roll (0x00427910: rand x 200 >> 15 < 30 with flag 1, anim 0x27
//!   captain_grenade_throw, state 0x10, ai.rs) and the anim's "throw" note at 1.333 s (0x0043d180 -> 0x00428340: the
//!   grenade from the left hand at the target's ref_hit_00): [`throw`].
//! - Load (0x00481c90): the base Projectile_Load 0x0047e0c0 (the property set below, speed x 3) and off the list
//!   World+0xf1c (it is not a pickup-able projectile).
//! - Launch (GrenadeProjectile_Launch 0x00482870): the target point pulled back toward the thrower by 20 (K 0x005bcbe8)
//!   when farther, the velocity of Ballistic_LaunchVelocity 0x00471a00 with the speed and gravity -64.348 (keep 0: the
//!   later root of the arc slows the horizontal speed), a random spin (rand() x K 0x005bce50 per axis, 0..0.05).
//! - Flight (slot 0x2f 0x00481ce0): +0x424 bit 0x20 cleared, then the generic update Projectile_Update 0x0047e580: under
//!   gravity, its path ray-cast; the thrower is in its ignore chain (+0x574).
//! - Hit (slot 0x4e 0x00481d10): at the hit point (+0x514), the area damage AreaDamage_Apply 0x0047f020 with the
//!   record's flags 0x0047eba0 | 0x10 (slot 0x4f 0x00481d00), then the direct damage to what it hit (vt+0x158) and the
//!   end (vt+0xd4): fx_end. Its lifetime (15 s) ends it unexploded (guess: Projectile_Update's expiry).

use bevy::prelude::*;
use serde::Deserialize;

/// K(0x005bcd64) negated: the world's gravity (game units / s^2).
const GRAVITY: f32 = 64.348;

/// The captain grenade's property set (p_captain_grenade), read once per throw from the level's data (Projectile_Load
/// 0x0047e0c0 defaults where the set has none).
#[derive(Deserialize, Clone, Debug)]
#[serde(default)]
pub struct GrenadeProps {
    pub speed: f32,
    pub lifetime: f32,
    pub damage: f32,
    pub area_damage: f32,
    pub damage_radius_inner: f32,
    pub damage_radius_outer: f32,
    pub impact_force: f32,
    pub fx_end: Option<String>,
}

impl Default for GrenadeProps {
    fn default() -> Self {
        GrenadeProps { speed: 35.0, lifetime: 15.0, damage: 50.0, area_damage: 30.0, damage_radius_inner: 10.0, damage_radius_outer: 30.0, impact_force: 15000.0, fx_end: None }
    }
}

impl GrenadeProps {
    pub fn read(data: &crate::gamedata::GameData) -> Self {
        serde_json::from_value(serde_json::Value::Object(data.props("p_captain_grenade").0)).unwrap_or_default()
    }
}

/// The grenade model (template captain_grenade: grunt_grenade_1), loaded with the level (level.rs).
#[derive(Resource, Default)]
pub struct GrenadeAssets(pub Vec<(Handle<Mesh>, Handle<StandardMaterial>)>);

/// Grenades thrown, ended, and ended on or near Roman (within the outer radius): the playtest state's `grenade`.
#[derive(Resource, Default, Debug)]
pub struct GrenadeStats {
    pub thrown: u32,
    pub ended: u32,
    pub hit_roman: u32,
    pub near_roman: u32,
}

/// A thrown grenade in flight.
#[derive(Component)]
pub struct Grenade {
    vel: Vec3,
    spin: Vec3,
    life: f32,
    owner: Entity,
    props: GrenadeProps,
}

/// Ballistic_LaunchVelocity 0x00471a00 in engine axes (y up), `g` the (positive) gravity: across at `speed`, the
/// vertical speed that reaches `to` at t = across distance / speed; unless `keep`, when the arc's later root of the
/// height equation lies beyond t the horizontal speed is slowed to land there.
pub fn launch_velocity(from: Vec3, to: Vec3, speed: f32, g: f32, keep: bool) -> Vec3 {
    let across = (to - from).with_y(0.0);
    let h = across.length();
    let dir = across.normalize_or_zero();
    if h < 1e-4 || speed <= 0.0 {
        return Vec3::Y * speed;
    }
    let t = h / speed;
    let dz = to.y - from.y;
    // the game's gravity is negative (-64.348): vz = (dz - t^2 g / 2) / t
    let gs = -g;
    let vz = (dz - t * t * gs * 0.5) / t;
    let mut vel = dir * speed + Vec3::Y * vz;
    if keep {
        return vel;
    }
    // the roots of dz = vz t + gs t^2 / 2 (K 0x005bcc40 = -2: disc = sqrt(vz^2 + 2 gs dz)), 0 when 2 gs - dz >= vz^2
    let disc = if gs + gs - dz < vz * vz { (vz * vz + 2.0 * gs * dz).max(0.0).sqrt() } else { 0.0 };
    let (r1, r2) = ((disc - vz) / gs, (-vz - disc) / gs);
    let tt = if r1 > 0.0 && r1 > r2 {
        r1
    } else if r2 > 0.0 && r2 > r1 {
        r2
    } else {
        t
    };
    if tt > t {
        vel = dir * (h / tt) + Vec3::Y * vz;
    }
    vel
}

/// The throw (0x00428340 -> GrenadeProjectile_Launch 0x00482870): from the hand at the target point.
pub fn throw(commands: &mut Commands, assets: Option<&GrenadeAssets>, data: &crate::gamedata::GameData, rng: &mut crate::fx::FxStats, from: Vec3, target: Vec3, owner: Entity) {
    let props = GrenadeProps::read(data);
    // the target point pulled back toward the thrower by 20 when farther than that (K 0x005bcbe8)
    let d = target - from;
    let to = if d.length() > 20.0 { from + d.normalize() * (d.length() - 20.0) } else { target };
    let vel = launch_velocity(from, to, props.speed * 3.0, GRAVITY, false);
    // the random spin: rand() x K(0x005bce50) (1.526e-6) per axis, 0..0.05 (0x004583d0: the body's angular velocity)
    let spin = Vec3::new(rng.rand(), rng.rand(), rng.rand()) * 0.05;
    let life = props.lifetime;
    let e = commands
        .spawn((Transform::from_translation(from).with_rotation(crate::level::game_to_engine()), Visibility::default(), Grenade { vel, spin, life, owner, props }))
        .id();
    if let Some(a) = assets {
        for (mesh, mat) in &a.0 {
            let c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone()))).id();
            commands.entity(e).add_child(c);
        }
    }
    commands.queue(|w: &mut World| {
        if let Some(mut s) = w.get_resource_mut::<GrenadeStats>() {
            s.thrown += 1;
        }
    });
    info!("EVENT ai: captain threw a grenade");
}

/// The grenades' flight and end (Projectile_Update 0x0047e580, the hit 0x00481d10).
#[allow(clippy::too_many_arguments)]
pub fn fly(
    mut commands: Commands,
    time: Res<Time>,
    physics: NonSend<crate::physics::Physics>,
    mut q: Query<(Entity, &mut Grenade, &mut Transform)>,
    mut health: Query<&mut crate::fx::Health>,
    mut blasts: MessageWriter<crate::fx::Explode>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    roman: Query<(Entity, &Transform, &crate::player::Roman), Without<Grenade>>,
    mut stats: ResMut<GrenadeStats>,
) {
    let dt = time.delta_secs();
    // Roman is no physics body (a walker): the path passing within 4.5 of his centre hits him (as the bullets'
    // fallback without his collision parts, player.rs)
    let roman = roman.single().ok().map(|(e, t, r)| (e, t.translation + Vec3::Y * r.walker.lift));
    for (e, mut gr, mut t) in &mut q {
        gr.life -= dt;
        gr.vel.y -= GRAVITY * dt;
        let spin = gr.spin * dt;
        t.rotate_local(Quat::from_euler(EulerRot::XYZ, spin.x, spin.y, spin.z));
        let step = gr.vel * dt;
        let from = t.translation;
        let mut hit = None;
        let mut start = from;
        let mut rest = step.length();
        let dir = step.normalize_or(Vec3::NEG_Y);
        while rest > 0.0 {
            let Some((b, at, _)) = physics.raycast(start, dir, rest) else { break };
            let obj = physics.bodies.get(&b).copied();
            // the thrower's own body is in the projectile's ignore chain (+0x574)
            if obj == Some(gr.owner) {
                rest -= at.distance(start) + 0.05;
                start = at + dir * 0.05;
                continue;
            }
            hit = Some((at, obj));
            break;
        }
        if let Some((re, c)) = roman {
            let u = ((c - from).dot(step) / step.length_squared().max(1e-6)).clamp(0.0, 1.0);
            let at = from + step * u;
            if at.distance(c) < 4.5 && hit.is_none_or(|(h, _)| h.distance(from) > at.distance(from)) {
                hit = Some((at, Some(re)));
            }
        }
        if hit.is_none() && gr.life > 0.0 {
            t.translation = from + step;
            continue;
        }
        let (at, obj) = hit.unwrap_or((from, None));
        let p = &gr.props;
        stats.ended += 1;
        if let Some((re, c)) = roman {
            if obj == Some(re) {
                stats.hit_roman += 1;
            } else if c.distance(at) <= p.damage_radius_outer {
                stats.near_roman += 1;
            }
        }
        blasts.write(crate::fx::Explode {
            at,
            area_damage: p.area_damage,
            radius_inner: p.damage_radius_inner,
            radius_outer: p.damage_radius_outer,
            impact_force: p.impact_force,
            source: obj,
            delay: 0.0,
            by: crate::fx::Src::Badguy,
            cartoon: false,
        });
        if let Some(mut h) = obj.and_then(|o| health.get_mut(o).ok()) {
            h.0 -= p.damage;
        }
        if let Some(n) = &p.fx_end {
            fx.write(crate::fx::StartFx { name: n.clone(), at, rotation: crate::level::game_to_engine(), follow: None, position_only: false });
        }
        commands.entity(e).despawn();
    }
}

/// The playtest state's `grenade` (GrenadeStats).
pub fn log(stats: Res<GrenadeStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert("grenade".into(), serde_json::json!({"thrown": stats.thrown, "ended": stats.ended, "hit_roman": stats.hit_roman, "near_roman": stats.near_roman}));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lands_on_target() {
        let from = Vec3::new(0.0, 10.0, 0.0);
        for to in [Vec3::new(80.0, 0.0, 30.0), Vec3::new(-40.0, 25.0, 10.0)] {
            let v = launch_velocity(from, to, 105.0, GRAVITY, false);
            // fly it: at the time it covers the horizontal distance it is at the target's height
            let h = (to - from).with_y(0.0).length();
            let t = h / v.with_y(0.0).length();
            let y = from.y + v.y * t - 0.5 * GRAVITY * t * t;
            assert!((y - to.y).abs() < 0.5, "{y} vs {}", to.y);
        }
    }

    #[test]
    fn props_from_the_set() {
        let p: GrenadeProps = serde_json::from_value(serde_json::json!({"speed": 35, "damage": 50.0, "fx_end": "fx_imp_sticky", "behavior": "proj_grenade"})).unwrap();
        assert_eq!(p.speed, 35.0);
        assert_eq!(p.area_damage, 30.0);
        assert_eq!(p.fx_end.as_deref(), Some("fx_imp_sticky"));
    }
}
