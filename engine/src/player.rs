//! The player's actions, STAND-IN until the game's player (Roman, recovered in src/game) is linked in:
//! F throws the game's tickerbomb (its model and its property set `p_player_tickerbomb`: area damage, radii,
//! impact force), left mouse fires a hitscan shot, G drops six tickerbombs around the crosshair (test only).

use crate::fx::{Destructible, Fuse, Health, Tracer};
use crate::gamedata::GameData;
use crate::physics::{Body, Physics};
use bevy::prelude::*;
use oxijolt::prelude::*;

#[derive(Resource)]
pub struct BombAssets {
    pub parts: Vec<(Handle<Mesh>, Handle<StandardMaterial>)>,
    pub shape: Shape,
    /// The model's rest rotation (game axes to engine axes).
    pub base: Quat,
}

/// Damage of one hitscan shot (stand-in value; the game's weapons are not ported yet).
const SHOT_DAMAGE: f32 = 35.0;
/// The tickerbomb's explosion fx (guess: the sticky bomb's snow variant; the behaviour picks it in the game).
const TICKERBOMB_FX: &str = "fx_exp_sticky_snow";

pub struct PlayerPlugin;

impl Plugin for PlayerPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, act.run_if(crate::playing));
    }
}

pub fn spawn_bomb(commands: &mut Commands, physics: &mut Physics, bombs: &BombAssets, data: &GameData, at: Vec3, velocity: Vec3, fuse: f32) {
    let p = data.props("p_player_tickerbomb");
    let d = Destructible {
        health: 1.0,
        fx_death: Some(TICKERBOMB_FX.into()),
        husk: None,
        area_damage: p.f("area_damage", 100.0),
        radius_inner: p.f("damage_radius_inner", 40.0),
        radius_outer: p.f("damage_radius_outer", 60.0),
        impact_force: p.f("impact_force", 25000.0),
        start_delay: 0.0,
        min_delay: 0.0,
        acceleration: 1.0,
        dying: None,
        chain_delay: 0.0,
    };
    let e = commands.spawn((Transform::from_translation(at).with_rotation(bombs.base), Visibility::default(), d, Fuse(fuse))).id();
    for (m, mat) in &bombs.parts {
        let c = commands.spawn((Mesh3d(m.clone()), MeshMaterial3d(mat.clone()))).id();
        commands.entity(e).add_child(c);
    }
    let settings = BodySettings::new_dynamic().mass(6.0).friction(0.6).restitution(0.3).angular_damping(0.5);
    if let Some(id) = physics.add_dynamic(e, &bombs.shape, at, bombs.base, velocity, settings) {
        commands.entity(e).insert(Body::rigid(id));
    }
}

#[allow(clippy::too_many_arguments)]
fn act(
    mut commands: Commands,
    buttons: Res<ButtonInput<MouseButton>>,
    keys: Res<ButtonInput<KeyCode>>,
    mut physics: NonSendMut<Physics>,
    bombs: Res<BombAssets>,
    data: Res<GameData>,
    mut rng: ResMut<crate::fx::FxStats>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    mut chars: Query<(&Body, &mut Health)>,
) {
    let Ok(cam) = cam.single() else { return };
    let from = cam.translation();
    let dir = *cam.forward();
    if buttons.just_pressed(MouseButton::Left) {
        let hit = physics.raycast(from + dir * 2.0, dir, 1500.0);
        let to = hit.map(|h| h.1).unwrap_or(from + dir * 1500.0);
        commands.spawn(Tracer { from: from - Vec3::Y * 1.5 + *cam.right() * 1.5, to, age: 0.0 });
        if let Some((body, _, _)) = hit {
            if let Some((_, mut h)) = chars.iter_mut().find(|(b, _)| b.id == body) {
                h.0 -= SHOT_DAMAGE;
            }
        }
    }
    if keys.just_pressed(KeyCode::KeyF) {
        spawn_bomb(&mut commands, &mut physics, &bombs, &data, from + dir * 6.0, dir * 90.0 + Vec3::Y * 25.0, 2.2);
    }
    if keys.just_pressed(KeyCode::KeyG) {
        let target = physics.raycast(from, dir, 3000.0).map(|h| h.1).unwrap_or(from + dir * 300.0);
        for i in 0..6 {
            let a = i as f32 / 6.0 * std::f32::consts::TAU + rng.rand();
            let at = target + Vec3::new(a.cos(), 0.0, a.sin()) * rng.range(10.0, 50.0) + Vec3::Y * rng.range(120.0, 160.0);
            spawn_bomb(&mut commands, &mut physics, &bombs, &data, at, Vec3::Y * -40.0, 3.0 + 0.15 * i as f32);
        }
    }
}
