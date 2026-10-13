//! Pickups dropped by dying grunts (Drop on death, cleanup_01.cpp:96-130: chance pickup_drop_percent / 100; the
//! auto pick gives the player what he needs: health when hurt, else sticky bombs, guess at the rule), from the
//! pickup templates' property sets: num_health / num_ammo, sphere_radius, linger_time, upward_force (the pop),
//! text_lookup (a message line on collection). Collected by walking into them.

use crate::ai::{Grunt, Mind, Player};
use crate::fx::Health;
use crate::gamedata::GameData;
use crate::level::game_to_engine;
use crate::scene::Library;
use bevy::prelude::*;

#[derive(Component)]
pub struct Pickup {
    pub health: f32,
    pub stickies: u32,
    pub radius: f32,
    pub life: f32,
    pub vel: f32,
    pub ground: f32,
    pub text: Option<String>,
}

pub struct PickupPlugin;

impl Plugin for PickupPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (drop_on_death, collect).run_if(crate::playing));
    }
}

#[allow(clippy::too_many_arguments)]
fn drop_on_death(
    mut commands: Commands,
    mut seen: Local<std::collections::HashSet<Entity>>,
    grunts: Query<(Entity, &Grunt, &GlobalTransform)>,
    player: Query<&Health, With<Player>>,
    roman: Query<&crate::player::Roman>,
    data: Res<GameData>,
    mut lib: ResMut<Library>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut rng: ResMut<crate::fx::FxStats>,
    physics: NonSend<crate::physics::Physics>,
) {
    let hurt = match (player.single(), roman.single()) {
        (Ok(h), Ok(r)) => h.0 < r.max_health * 0.75,
        _ => true,
    };
    for (e, g, t) in &grunts {
        if g.mind != Mind::Dead || !seen.insert(e) {
            continue;
        }
        if rng.rand() >= g.drop_percent / 100.0 {
            continue;
        }
        let name = if hurt { "pickup_health_1" } else { "pickup_sticky_bombs" };
        let Some(tpl) = data.templates.get(name) else { continue };
        let p = data.template_props(name);
        let at = t.translation() + Vec3::Y * 4.0;
        let ground = physics.raycast(at + Vec3::Y * 20.0, -Vec3::Y, 200.0).map(|h| h.1.y).unwrap_or(at.y - 4.0);
        let pe = commands
            .spawn((
                Transform::from_translation(at).with_rotation(game_to_engine()),
                Visibility::default(),
                Pickup {
                    health: p.f("num_health", 0.0),
                    stickies: p.f("num_ammo", 0.0) as u32 * (name == "pickup_sticky_bombs") as u32,
                    radius: p.f("sphere_radius", 4.0) + 4.0,
                    life: p.f("linger_time", 300.0),
                    vel: p.f("upward_force", 70.0) * 0.6,
                    ground,
                    text: p.s("text_lookup").map(str::to_owned),
                },
            ))
            .id();
        if let Some(m) = tpl.model.as_ref().and_then(|m| lib.load_model(m, false, &assets, &mut meshes, &mut materials)) {
            for (mesh, mat) in &m.parts {
                let c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone()))).id();
                commands.entity(pe).add_child(c);
            }
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn collect(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut Pickup, &mut Transform)>,
    mut player: Query<(&GlobalTransform, &mut Health, &mut crate::player::Roman), With<Player>>,
    mission: Option<ResMut<crate::mission::Mission>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let dt = time.delta_secs();
    let Ok((pt, mut h, mut r)) = player.single_mut() else { return };
    let mut mission = mission;
    for (e, mut p, mut t) in &mut q {
        // the pop: up with upward_force, back down to the ground; spin
        p.vel -= 160.0 * dt;
        t.translation.y = (t.translation.y + p.vel * dt).max(p.ground + 1.0);
        t.rotate_y(dt * 2.0);
        p.life -= dt;
        if p.life <= 0.0 {
            commands.entity(e).despawn();
            continue;
        }
        if t.translation.distance(pt.translation() + Vec3::Y * 4.0) > p.radius {
            continue;
        }
        if p.health > 0.0 && h.0 >= r.max_health {
            continue; // full health: leave it
        }
        h.0 = (h.0 + p.health).min(r.max_health);
        r.stickies += p.stickies;
        if let (Some(m), Some(text)) = (mission.as_deref_mut(), p.text.as_ref()) {
            m.feed(text);
        }
        sounds.write(crate::audio::PlaySound::ui(if p.health > 0.0 { "ui_pickup_health" } else { "ui_pickup_ammo_1" }));
        commands.entity(e).despawn();
    }
}
