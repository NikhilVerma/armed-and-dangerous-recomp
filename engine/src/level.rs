//! Armed and Dangerous specifics: the game's axes and units, and how a placed object's property set
//! (`behavior`, `collision_lod_index`, health and damage keys) decides what it becomes in the engine.

use crate::ai::{self, Grunt, Mind};
use crate::fx::{Destructible, Died, Health};
use crate::gamedata::{GameData, Props};
use crate::physics::{Body, Physics};
use crate::player::BombAssets;
use crate::scene::{self, Library, SceneFile};
use bevy::light::{CascadeShadowConfigBuilder, NotShadowCaster};
use bevy::prelude::*;
use oxijolt::prelude::*;

/// 1 game unit in metres: a grunt is about 9.8 units tall (guess: 2 m), a pine 144 units (29 m).
pub const METERS_PER_UNIT: f32 = 0.2;

/// Game axes (Z up, as authored in 3ds Max) to the engine's (Y up): a rotation, so handedness is kept.
pub fn game_to_engine() -> Quat {
    Quat::from_rotation_x(-std::f32::consts::FRAC_PI_2)
}

#[derive(Resource, Default, Clone)]
pub struct LevelInfo {
    pub name: String,
    pub center: Vec3,
    pub size: Vec3,
    pub instances: usize,
    pub models: usize,
    pub draws: usize,
    pub triangles: usize,
    pub static_bodies: usize,
    pub static_triangles: usize,
    pub grunts: usize,
    pub destructibles: usize,
    /// The level's fog (global_settings): near, far (game units), colour.
    pub fog: Option<(f32, f32, Color)>,
}

/// `behavior = background` (the sky dome): drawn around the camera, unlit, no fog, no collision.
#[derive(Component)]
pub struct Background;

/// The model parts (children) and static collider of a placed object, so its husk can replace them.
#[derive(Component)]
pub struct Placed {
    pub template: String,
    pub collider: Option<BodyId>,
}

pub struct LevelPlugin;

impl Plugin for LevelPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, husks.after(crate::fx::FxSet))
            .add_systems(PostUpdate, follow_camera.before(TransformSystems::Propagate));
    }
}

fn follow_camera(cam: Query<&Transform, (With<Camera3d>, Without<Background>)>, mut sky: Query<&mut Transform, With<Background>>) {
    let Ok(c) = cam.single() else { return };
    // horizontally only: the dome keeps its authored height, so its rim stays at the ground's horizon
    for mut t in &mut sky {
        t.translation.x = c.translation.x;
        t.translation.z = c.translation.z;
    }
}

/// Spawns one model's parts under `e`; returns (draws, triangles).
fn attach_model(commands: &mut Commands, e: Entity, model: &scene::Model, shadows: bool) -> (usize, usize) {
    for (mesh, mat) in &model.parts {
        let mut c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone())));
        if !shadows {
            c.insert(NotShadowCaster);
        }
        let c = c.id();
        commands.entity(e).add_child(c);
    }
    (model.parts.len(), model.triangles)
}

fn static_collider(physics: &mut Physics, model: &scene::Model, world: Mat4) -> Option<BodyId> {
    let v: Vec<Vec3> = model.vertices.iter().map(|p| world.transform_point3(*p)).collect();
    physics.add_static_mesh(&v, &model.tris)
}

fn destructible(p: &Props) -> Option<Destructible> {
    // Objects with a death fx or a husk and finite health die from damage (behaviour fx_explosion, building, tree).
    if !(p.has("max_health") && (p.s("fx_death").is_some() || p.s("husk").is_some())) || p.f("max_health", 0.0) >= 100000.0 {
        return None;
    }
    Some(Destructible {
        health: p.f("max_health", 1.0),
        fx_death: p.s("fx_death").map(str::to_owned),
        husk: p.s("husk").map(str::to_owned),
        area_damage: p.f("area_damage", 0.0),
        radius_inner: p.f("damage_radius_inner", 0.0),
        radius_outer: p.f("damage_radius_outer", 0.0),
        impact_force: p.f("impact_force", 0.0),
        start_delay: p.f("start_delay", 0.0),
        min_delay: p.f("min_delay", 0.0),
        acceleration: p.f("acceleration_rate", 1.0),
        dying: None,
        chain_delay: 0.0,
    })
}

#[allow(clippy::too_many_arguments)]
pub fn spawn(
    commands: &mut Commands,
    scene: &SceneFile,
    data: &GameData,
    lib: &mut Library,
    physics: &mut Physics,
    assets: &AssetServer,
    meshes: &mut Assets<Mesh>,
    materials: &mut Assets<StandardMaterial>,
) -> LevelInfo {
    let axes = Mat4::from_quat(game_to_engine());
    let mut info = LevelInfo { name: scene.level.clone(), ..default() };

    // landscape: its own mesh is its collision
    if let Some(model) = lib.load_model(&scene.landscape, false, assets, meshes, materials) {
        let e = commands.spawn((Transform::from_matrix(axes), Visibility::default(), Name::new("landscape"))).id();
        let (d, t) = attach_model(commands, e, model, true);
        info.draws += d;
        info.triangles += t;
        static_collider(physics, model, axes);
    }

    for inst in &scene.instances {
        let p = data.template_props(&inst.template);
        let tpl = data.templates.get(&inst.template);
        let behavior = p.s("behavior").unwrap_or("");
        // markers, trigger volumes, cameras, sound emitters: logic only, never drawn
        if matches!(behavior, "broadcast" | "camera_fx" | "fx_ear") || inst.template == "marker" || inst.template == "bigredarrow" {
            continue;
        }
        let world = axes * scene::game_matrix(&inst.transform);
        let (_, rot, pos) = world.to_scale_rotation_translation();
        let background = behavior == "background";
        let invisible = inst.property.contains("invisible") || p.f("invisible", 0.0) != 0.0;
        let Some(model) = lib.load_model(&inst.model, background, assets, meshes, materials) else { continue };
        let mut e = commands.spawn((Transform::from_matrix(world), Visibility::default(), Name::new(inst.template.clone())));
        if background {
            e.insert(Background);
        }
        let e = e.id();
        if !invisible {
            let (d, t) = attach_model(commands, e, model, !background);
            info.instances += 1;
            info.draws += d;
            info.triangles += t;
        }
        if background {
            continue;
        }

        // characters: property sets with an AI mode (grunts). Their AI here is a stand-in driven by the game's
        // own parameters (speeds, ranges); the recovered AI (src/ai) replaces it later.
        if p.has("mode") && inst.model.starts_with("grunt_") {
            let shape = ai::capsule(physics, p.f("collision_radius", 4.0));
            let lift = Vec3::Y * ai::capsule_lift(p.f("collision_radius", 4.0));
            let s = BodySettings::new_dynamic()
                .mass(80.0)
                .friction(0.0)
                .allowed_dofs(AllowedDofs::TRANSLATION_X | AllowedDofs::TRANSLATION_Y | AllowedDofs::TRANSLATION_Z);
            if let Some(id) = physics.add_dynamic(e, &shape, pos + lift, Quat::IDENTITY, Vec3::ZERO, s) {
                let (yaw, _, _) = (rot * game_to_engine().inverse()).to_euler(EulerRot::YXZ);
                commands.entity(e).insert((
                    Body { id, offset: lift, sync_rotation: false },
                    Health(p.f("max_health", 100.0)),
                    Grunt {
                        mind: Mind::Patrol,
                        home: pos,
                        goal: pos,
                        timer: 0.0,
                        fire_timer: 1.0,
                        base: game_to_engine(),
                        yaw,
                        move_speed: p.f("move_speed", 40.0),
                        patrol_speed: p.f("patrol_speed", 7.0),
                        scan_distance: p.f("scan_distance", 350.0),
                        fire_range: (p.f("fire_range_min", 35.0), p.f("fire_range_max", 500.0)),
                        scare_distance: p.f("sticky_scare_dist", 25.0),
                    },
                ));
                info.grunts += 1;
            }
            continue;
        }

        // everything else stands still and collides with its collision model (the render model when the
        // template names none, e.g. invisible walls)
        let coll_name = tpl.and_then(|t| t.collision.clone()).or_else(|| invisible.then(|| inst.model.clone()));
        let collider = coll_name
            .and_then(|n| lib.load_model(&n, false, assets, meshes, materials).map(|m| (m.vertices.clone(), m.tris.clone())))
            .and_then(|(v, t)| {
                let v: Vec<Vec3> = v.iter().map(|x| world.transform_point3(*x)).collect();
                physics.add_static_mesh(&v, &t)
            });
        commands.entity(e).insert(Placed { template: inst.template.clone(), collider });
        if let Some(d) = destructible(&p) {
            commands.entity(e).insert(d);
            info.destructibles += 1;
        }
    }
    let g = |k: &str| scene.settings.get(k).and_then(|v| v.as_f64()).map(|v| v as f32);
    if let (Some(near), Some(far)) = (g("fog_near"), g("fog_far")) {
        let c = Color::srgb_u8(g("fog_red").unwrap_or(200.0) as u8, g("fog_green").unwrap_or(200.0) as u8, g("fog_blue").unwrap_or(200.0) as u8);
        info.fog = Some((near, far, c));
    }
    info.models = lib.models.len();
    info.static_bodies = physics.static_bodies;
    info.static_triangles = physics.static_triangles;

    // The player's tickerbomb, for throwing.
    let parts = lib.load_model("tickerbomb_0", false, assets, meshes, materials).map(|m| m.parts.clone()).unwrap_or_default();
    commands.insert_resource(BombAssets {
        parts,
        shape: Shape::new_sphere(1.4 * physics.meters_per_unit).expect("bomb shape"),
        base: game_to_engine(),
    });

    // The level's directional lights (lighttype "dir"): each shines down its local -Z axis.
    let (lo, hi) = (Vec3::from(scene.bounds[0]), Vec3::from(scene.bounds[1]));
    info.center = game_to_engine() * ((lo + hi) * 0.5);
    info.size = hi - lo;
    let extent = (hi - lo).length();
    let mut first = true;
    for l in &scene.lights {
        let p = |k: &str| l.props.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
        if l.props.get("lighttype").and_then(|v| v.as_str()) != Some("dir") || p("lighton") == 0.0 || p("ambientonly") != 0.0 {
            continue;
        }
        let m = scene::game_matrix(&l.transform);
        let dir = game_to_engine() * -m.z_axis.truncate().normalize();
        commands.spawn((
            DirectionalLight {
                color: Color::srgb(p("redvalue") / 255.0, p("greenvalue") / 255.0, p("bluevalue") / 255.0),
                illuminance: 9000.0 * p("intensity").max(0.1),
                shadow_maps_enabled: first,
                ..default()
            },
            Transform::default().looking_to(dir, Vec3::Y),
            CascadeShadowConfigBuilder { maximum_distance: extent * 0.5, first_cascade_far_bound: 60.0, ..default() }.build(),
        ));
        first = false;
    }
    if first {
        commands.spawn((
            DirectionalLight { illuminance: 9000.0, shadow_maps_enabled: true, ..default() },
            Transform::default().looking_to(Vec3::new(-0.4, -1.0, -0.3), Vec3::Y),
        ));
    }
    info
}

/// A dead object is replaced by its husk template (felled tree, ruined barracks) or removed, with its collider.
#[allow(clippy::too_many_arguments)]
fn husks(
    mut commands: Commands,
    mut died: MessageReader<Died>,
    data: Res<GameData>,
    mut lib: ResMut<Library>,
    mut physics: NonSendMut<Physics>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    placed: Query<(&Placed, &Transform, &Children)>,
) {
    for d in died.read() {
        let Ok((p, t, children)) = placed.get(d.0) else { continue };
        if let Some(c) = p.collider {
            physics.remove_static(c);
        }
        for c in children.iter() {
            commands.entity(c).despawn();
        }
        let husk = data.template_props(&p.template).s("husk").map(str::to_owned);
        let husk_tpl = husk.as_ref().and_then(|h| data.templates.get(h)).cloned();
        let Some(ht) = husk_tpl else {
            commands.entity(d.0).despawn();
            continue;
        };
        let world = t.to_matrix();
        if let Some(m) = ht.model.as_ref().and_then(|m| lib.load_model(m, false, &assets, &mut meshes, &mut materials)) {
            attach_model(&mut commands, d.0, m, true);
        }
        let collider = ht
            .collision
            .as_ref()
            .and_then(|n| lib.load_model(n, false, &assets, &mut meshes, &mut materials).map(|m| (m.vertices.clone(), m.tris.clone())))
            .and_then(|(v, tr)| {
                let v: Vec<Vec3> = v.iter().map(|x| world.transform_point3(*x)).collect();
                physics.add_static_mesh(&v, &tr)
            });
        commands.entity(d.0).insert(Placed { template: husk.unwrap_or_default(), collider });
    }
}
