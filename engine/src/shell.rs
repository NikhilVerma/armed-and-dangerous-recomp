//! Lobbed shells: a projectile thrown at a point under the world's gravity that explodes where it lands (area damage
//! and fx_end of its property set) or when its lifetime runs out. The base turret's mortars (base_mortar /
//! base_homing: Projectile vtables 0x005adc78 / 0x005a8100, launch Projectile_Launch 0x00480ec0 with the velocity
//! solver 0x00471a00, update 0x00483210 -> 0x0047e580) and the goliath's fireballs (GoliathObject_HandleEvent
//! 0x0043cef0 "fire": the same solver).

use bevy::prelude::*;

/// The world's gravity in game units / s^2 (the arrows' and shells' solver, 0x00471a00).
pub const GRAVITY: f32 = 64.348;

/// A shell in flight.
#[derive(Component)]
pub struct Shell {
    vel: Vec3,
    life: f32,
    by: crate::fx::Src,
    props: crate::gamedata::Props,
}

/// How a shell is thrown.
#[derive(Clone, Copy, Debug)]
pub struct Lob {
    /// who the blast's damage record names (the turret's player, the goliath a bad guy)
    pub by: crate::fx::Src,
    /// the flight time; None: the mortar's, horizontal speed = the horizontal distance + 5 (0x00480ec0: about 1 s)
    pub flight: Option<f32>,
}

pub struct ShellPlugin;

impl Plugin for ShellPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, fly.run_if(crate::playing));
    }
}

/// The launch velocity reaching `target` from `from` in `flight` seconds under GRAVITY (engine axes, Y up).
pub fn velocity(from: Vec3, target: Vec3, flight: Option<f32>) -> Vec3 {
    let d = target - from;
    let flat = d.with_y(0.0);
    let time = flight.unwrap_or_else(|| flat.length() / (flat.length() + 5.0)).max(0.05);
    flat / time + Vec3::Y * (d.y / time + 0.5 * GRAVITY * time)
}

/// Throws a shell of `template` (its model, fx_start held on it) at `target`. Returns its entity.
#[allow(clippy::too_many_arguments)]
pub fn launch(commands: &mut Commands, data: &crate::gamedata::GameData, kit: &mut crate::army::ModelKit, fx: &mut MessageWriter<crate::fx::StartFx>, template: &str, from: Vec3, target: Vec3, lob: Lob) -> Entity {
    let p = data.template_props(template);
    let vel = velocity(from, target, lob.flight);
    let rot = facing(vel);
    let tr = Transform::from_translation(from).with_rotation(rot);
    let model = data.templates.get(template).and_then(|t| t.model.clone());
    let e = model.and_then(|m| kit.spawn(commands, &m, tr, true)).unwrap_or_else(|| commands.spawn((tr, Visibility::default())).id());
    commands.entity(e).insert(Shell { vel, life: p.f("lifetime", 15.0), by: lob.by, props: p.clone() });
    if let Some(name) = p.s("fx_start") {
        fx.write(crate::fx::StartFx { name: name.to_lowercase(), at: from, rotation: rot, follow: Some(e), position_only: false });
    }
    e
}

/// The model's game frame along the flight (+y ahead, +z up), as projectiles carry it.
fn facing(vel: Vec3) -> Quat {
    Transform::IDENTITY.looking_to(vel.normalize_or(Vec3::NEG_Z), Vec3::Y).rotation * crate::level::game_to_engine()
}

/// Shells fly and land: the first hit along the frame's step explodes it.
fn fly(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut Shell, &mut Transform)>,
    physics: NonSend<crate::physics::Physics>,
    mut blasts: MessageWriter<crate::fx::Explode>,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    let dt = time.delta_secs();
    for (e, mut s, mut tr) in &mut q {
        s.life -= dt;
        let from = tr.translation;
        s.vel.y -= GRAVITY * dt;
        let step = s.vel * dt;
        let hit = physics.raycast(from, step.normalize_or(Vec3::NEG_Y), step.length()).map(|h| h.1);
        if hit.is_none() && s.life > 0.0 {
            tr.translation = from + step;
            tr.rotation = facing(s.vel);
            continue;
        }
        let at = hit.unwrap_or(from);
        let p = &s.props;
        blasts.write(crate::fx::Explode {
            at,
            area_damage: p.f("area_damage", 0.0),
            radius_inner: p.f("damage_radius_inner", 0.0),
            radius_outer: p.f("damage_radius_outer", 0.0),
            impact_force: p.f("impact_force", 2010.0),
            source: None,
            delay: 0.0,
            by: s.by,
            cartoon: false,
            kind: crate::player::DamageKind::Plain,
        });
        if let Some(name) = p.s("fx_end") {
            fx.write(crate::fx::StartFx { name: name.to_lowercase(), at, rotation: Quat::IDENTITY, follow: None, position_only: false });
        }
        commands.entity(e).despawn();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The solved velocity lands the shell on the target after the flight time.
    #[test]
    fn velocity_reaches_the_target() {
        let (from, target) = (Vec3::new(0.0, 50.0, 0.0), Vec3::new(300.0, 10.0, -400.0));
        for flight in [None, Some(1.5)] {
            let v = velocity(from, target, flight);
            let t = (target - from).with_y(0.0).length() / v.with_y(0.0).length();
            let at = from + v * t - Vec3::Y * 0.5 * GRAVITY * t * t;
            assert!(at.distance(target) < 0.01, "{at} vs {target}");
        }
    }
}
