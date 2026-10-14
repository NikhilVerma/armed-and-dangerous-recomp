//! Armed and Dangerous specifics: the game's axes and units, and how a placed object's property set
//! (`behavior`, `collision_lod_index`, health and damage keys) decides what it becomes in the engine.

use crate::ai;
use crate::fx::{Bounds, DClass, Destructible, Died, Health};
use crate::gamedata::{GameData, Props};
use crate::physics::{Body, Physics};
use crate::player::BombAssets;
use crate::scene::{self, ClonedParts, Library, SceneFile};
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
    pub player: Option<Entity>,
    /// camera_fx scripts to start on the camera: (fx name, position only)
    pub camera_fx: Vec<(String, bool)>,
}

/// The placed instance's 1-based id in the level's inst bank (what triggers and tags refer to).
#[derive(Component, Clone, Copy)]
pub struct InstanceId(pub u32);

/// `behavior = background` (the sky dome): drawn around the camera, unlit, no fog, no collision.
#[derive(Component)]
pub struct Background;

/// The model parts (children) and static collider of a placed object, so its husk can replace them.
#[derive(Component)]
pub struct Placed {
    pub template: String,
    pub collider: Option<BodyId>,
}

/// A troop to spawn (barracks / alarm generators): a grunt template at a point, awake and hunting the player.
#[derive(Message, Clone)]
pub struct SpawnGrunt {
    pub at: Vec3,
    pub template: String,
    /// a troop of the alarm controller (alarm.rs)
    pub alarm: bool,
}

/// The `Center` translation track of a rigid (unskinned) model's own anim, played in a loop on its placement.
#[derive(Component)]
pub struct RigidAnim {
    keys: Vec<(f32, Vec3)>,
    duration: f32,
    t: f32,
    base: Option<Vec3>,
}

impl RigidAnim {
    fn load(dir: &std::path::Path, model: &str) -> Option<Self> {
        let v: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(dir.join("anims").join(format!("{model}.json"))).ok()?).ok()?;
        let track = v.get("tracks")?.get("Center")?.get("t")?.as_array()?;
        let keys: Vec<(f32, Vec3)> = track
            .iter()
            .filter_map(|k| {
                let k = k.as_array()?;
                let f = |i: usize| k.get(i).and_then(|x| x.as_f64()).map(|x| x as f32);
                Some((f(0)?, Vec3::new(f(1)?, f(2)?, f(3)?)))
            })
            .collect();
        if keys.len() < 2 {
            return None;
        }
        Some(RigidAnim { duration: v.get("duration")?.as_f64()? as f32, keys, t: 0.0, base: None })
    }
}

fn rigid_anims(time: Res<Time>, mut q: Query<(&mut RigidAnim, &mut Transform)>) {
    for (mut a, mut t) in &mut q {
        let base = *a.base.get_or_insert(t.translation);
        a.t = (a.t + time.delta_secs()).rem_euclid(a.duration.max(1e-3));
        let i = a.keys.iter().rposition(|k| k.0 <= a.t).unwrap_or(0);
        let (t0, p0) = a.keys[i];
        let (t1, p1) = a.keys[(i + 1).min(a.keys.len() - 1)];
        let p = if t1 > t0 { p0.lerp(p1, (a.t - t0) / (t1 - t0)) } else { p0 };
        // game space offset (z up) on the placement's own axes
        t.translation = base + t.rotation * p;
    }
}

pub struct LevelPlugin;

impl Plugin for LevelPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SpawnGrunt>()
            .add_systems(Update, (husks.after(crate::fx::FxSet), spawn_grunts, rigid_anims, audio_loops))
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

/// What skinned models need while spawning.
pub struct SkinCtx<'a> {
    pub lib: &'a mut crate::skin::AnimLibrary,
    pub binds: &'a mut Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>,
}

/// Spawns one model's parts under `e`; a skinned model also gets its joint entities and an Animator on `e`
/// (upper/lower body split at "belly", the bone the _t / _b anim halves divide at). Returns (draws, triangles).
fn attach_model(commands: &mut Commands, e: Entity, model: &scene::Model, shadows: bool, skin: Option<&mut SkinCtx>) -> (usize, usize, Option<Entity>) {
    let rig = match (model.skeleton.as_deref(), skin) {
        (Some(name), Some(ctx)) => ctx.lib.skeleton(name, ctx.binds).map(|sk| crate::skin::build_rig(commands, e, sk, Some("belly"))),
        _ => None,
    };
    for (mesh, mat) in &model.parts {
        let mut c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone())));
        if !shadows {
            c.insert(NotShadowCaster);
        }
        if let Some(r) = &rig {
            c.insert((crate::skin::skinned_mesh(r), bevy::camera::visibility::NoFrustumCulling));
        }
        let c = c.id();
        commands.entity(e).add_child(c);
    }
    // the right-hand weapon bone (characters carry their weapon at "ref_rhand": grunt init 0x00434fb0)
    let hand = rig.as_ref().and_then(|r| r.skeleton.index.get("ref_rhand").map(|&i| r.joints[i]));
    if let Some(r) = rig {
        commands.entity(e).insert(r);
    }
    (model.parts.len(), model.triangles, hand)
}

/// A weapon model in a character's hand: its ref_hand_00 bone is at the weapon's origin, so the model hangs
/// straight off the hand joint.
fn attach_weapon(commands: &mut Commands, hand: Entity, model: &scene::Model, fix: Transform) -> Entity {
    let holder = commands.spawn((Transform::IDENTITY, Visibility::default())).id();
    commands.entity(hand).add_child(holder);
    for (mesh, mat) in &model.parts {
        let c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone()), fix)).id();
        commands.entity(holder).add_child(c);
    }
    holder
}

/// A penguin (PenguinObject, vtable 0x005a1768): any hit kills it (damage 0x0048fa20, no amount threshold, no team
/// filter), score_value -10 to Accidents; `dead` counts the time since (pngn_blown_up, then pngn_land_dead, the
/// body lingering 5 s and fading 0.5 s: GoRagdoll 0x00431310's corpse timer).
#[derive(Component, Default)]
pub struct Penguin {
    pub dead: Option<f32>,
}

/// One of Roman's weapon models in his hand (player.rs shows the one in use).
#[derive(Component)]
pub struct HandWeapon(pub &'static str);

fn static_collider(physics: &mut Physics, model: &scene::Model, world: Mat4) -> Option<BodyId> {
    let v: Vec<Vec3> = model.vertices.iter().map(|p| world.transform_point3(*p)).collect();
    physics.add_static_mesh(&v, &model.tris)
}

fn destructible(p: &Props) -> Option<Destructible> {
    // Objects with a death fx or a husk and finite health die from damage (behaviour fx_explosion, building, tree).
    // (an object that takes damage and has a death fx but no max_health, the guard alarm: the base default health,
    // 100 (guess: Object_Load's default is not traced))
    let takes = p.f("takes_damage", 0.0) != 0.0 && p.s("fx_death").is_some();
    if !((p.has("max_health") || takes) && (p.s("fx_death").is_some() || p.s("husk").is_some())) || p.f("max_health", 0.0) >= 100000.0 {
        return None;
    }
    Some(Destructible {
        health: p.f("max_health", 100.0),
        fx_death: p.s("fx_death").map(str::to_owned),
        husk: p.s("husk").map(str::to_owned),
        area_damage: p.f("area_damage", 0.0),
        radius_inner: p.f("damage_radius_inner", 0.0),
        radius_outer: p.f("damage_radius_outer", 0.0),
        impact_force: p.f("impact_force", 0.0),
        chain: p.s("behavior") == Some("fx_explosion"),
        score: p.f("score_value", 0.0),
        class: match p.s("behavior").unwrap_or("") {
            "building" => DClass::Building,
            "fx_explosion" => DClass::Keg,
            "tree" | "rocket_tower_turret" => DClass::Open,
            _ => DClass::Default,
        },
        // defaults by class: DefaultGameObject_Load 0x0044fd20, BuildingObject 0x0040ad80 (ai_01.cpp:961),
        // FxExplosion_Load 0x0041c180
        takes_damage: p.f("takes_damage", if p.s("behavior") == Some("building") { 1.0 } else { 0.0 }) != 0.0,
        takes_bullet: p.f("takes_bullet_damage", 0.0) != 0.0,
        player_damage: p.f("player_damage", 1.0) != 0.0,
        teammate_damage: p.f("teammate_damage", if p.s("behavior") == Some("building") { 1.0 } else { 0.0 }) != 0.0,
        badguy_damage: p.f("badguy_damage", 0.0) != 0.0,
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
    skin: &mut SkinCtx,
) -> LevelInfo {
    let axes = Mat4::from_quat(game_to_engine());
    let mut rng = crate::fxvm::CrtRand::default();
    let mut info = LevelInfo { name: scene.level.clone(), ..default() };

    // landscape: its own mesh is its collision
    let skip_visual = lib.skip_landscape_visual;
    if let Some(model) = lib.load_model(&scene.landscape, false, assets, meshes, materials) {
        if !skip_visual {
            let e = commands.spawn((Transform::from_matrix(axes), Visibility::default(), Name::new("landscape"))).id();
            let (d, t, _) = attach_model(commands, e, model, true, None);
            info.draws += d;
            info.triangles += t;
        }
        physics.landscape = static_collider(physics, model, axes);
        commands.insert_resource(crate::decals::DecalWorld::new(&model.vertices, &model.tris));
    }

    for inst in &scene.instances {
        let p = data.instance_props(&inst.template, &inst.props);
        let tpl = data.templates.get(&inst.template);
        let behavior = p.s("behavior").unwrap_or("");
        // markers, trigger volumes, cameras, sound emitters: logic only, never drawn
        // camera_fx (weather domes, intro overlays): their fx starts at load attached to the camera (trigger_start 0;
        // CameraFXObject_Load 0x004101a0), position-only when position_only = 1 (docs/subsystems/weather.md)
        if behavior == "camera_fx" {
            if p.f("trigger_start", 0.0) == 0.0 {
                if let Some(fx) = p.s("fx_name") {
                    info.camera_fx.push((fx.to_owned(), p.f("position_only", 1.0) != 0.0));
                }
            }
            continue;
        }
        // (the big red arrow over the pub is drawn: the original shows it until trigger 900 removes it)
        if matches!(behavior, "broadcast" | "fx_ear") || inst.template == "marker" {
            continue;
        }
        // the placement's scale is not applied: the original draws scaled placements (the archways' 0.5, pine trees'
        // 0.55..0.88) at full size; measured against its captures (frame 580 of the synced walk: the arch and the
        // trees' tops land where the unscaled models project, docs/remaster-engine.md). Only rotation and position.
        let placed = scene::game_matrix(&inst.transform);
        let (_, r, tr) = placed.to_scale_rotation_translation();
        let mut world = axes * Mat4::from_rotation_translation(r, tr);
        // force_ground (Object_ForceGround 0x00455860 with (1000, 10000)): raised 1000 units, a ray cast down
        // 10000, placed at the hit + 0.1 (the penetration bisection that follows is not ported)
        if p.f("force_ground", 0.0) != 0.0 {
            let at = world.w_axis.truncate();
            if let Some((_, hit, _)) = physics.raycast(at + Vec3::Y * 1000.0, -Vec3::Y, 10000.0) {
                world.w_axis.y = hit.y + 0.1;
            }
        }
        let (_, rot, pos) = world.to_scale_rotation_translation();
        let background = behavior == "background";
        let invisible = inst.property.contains("invisible") || p.f("invisible", 0.0) != 0.0;
        let lib_dir = lib.dir.clone();
        let Some(model) = lib.load_model(&inst.model, background, assets, meshes, materials) else { continue };
        let (scale, _, _) = world.to_scale_rotation_translation();
        let bounds = Bounds { center: (model.min + model.max) * 0.5, radius: (model.max - model.min).length() * 0.5 * scale.max_element() };
        let mut e = commands.spawn((Transform::from_matrix(world), Visibility::default(), Name::new(inst.template.clone()), InstanceId(inst.id)));
        if background {
            e.insert(Background);
        }
        let e = e.id();
        // a rigid model's own anim moving its root (the big red arrow bobs: Center z 20..46, anim bigredarrow_0)
        if model.skeleton.is_none() {
            if let Some(ra) = RigidAnim::load(&lib_dir, &inst.model) {
                commands.entity(e).insert(ra);
            }
        }
        if !invisible {
            // shadow casters: objects whose property set has shadow_enable (shadows.md, object +0x3ec bits 1-2)
            let casts = !background && p.f("shadow_enable", 0.0) != 0.0;
            let (d, t, hand) = attach_model(commands, e, model, casts, Some(&mut *skin));
            // weapons: Roman's rifle (the level's loadout: player_add_rifle), grunts' rifle or bow
            let weapon = match inst.template.as_str() {
                "trotter" => Some("player_rifle"),
                "grunt_regular" => Some("grunt_rifle"),
                "grunt" => Some("grunt_bow"),
                _ => None,
            };
            if let (Some(h), Some(w)) = (hand, weapon.and_then(|w| data.templates.get(w)).and_then(|t| t.model.clone())) {
                if let Some(m) = lib.load_model(&w, false, assets, meshes, materials) {
                    // the weapon's ref_hand_00 = the hand joint (identity; checked by CPU skinning: the rifle points
                    // ahead in trotter_idle_1 and about 45 degrees up in the aim / run_aim poses, as authored)
                    let held = attach_weapon(commands, h, m, Transform::IDENTITY);
                    if inst.template == "trotter" {
                        commands.entity(held).insert(HandWeapon("rifle"));
                    }
                }
                // Roman's machine gun (taken in the pub) hangs in the same hand, hidden until it is in use
                if inst.template == "trotter" {
                    if let Some(m) = data.templates.get("player_machinegun").and_then(|t| t.model.clone()).and_then(|n| lib.load_model(&n, false, assets, meshes, materials)) {
                        let held = attach_weapon(commands, h, m, Transform::IDENTITY);
                        commands.entity(held).insert((HandWeapon("machinegun"), Visibility::Hidden));
                    }
                }
            }
            info.instances += 1;
            info.draws += d;
            info.triangles += t;
        }
        if background {
            continue;
        }

        // Roman, the player: the placed trotter
        if inst.template == "trotter" {
            let (yaw, _, _) = (rot * game_to_engine().inverse()).to_euler(EulerRot::YXZ);
            // PlayerObject_Load 0x00473da0 puts him on the ground (Object_ForceGround 0x00455860: from 1000 above,
            // 10000 down): he does not drop from the placement's height at the start
            let pos = physics.raycast(pos + Vec3::Y * 1000.0, -Vec3::Y, 10000.0).map(|h| pos.with_y(h.1.y)).unwrap_or(pos);
            if let Some(r) = crate::player::make_roman(physics, &p, data, pos, yaw - std::f32::consts::PI) {
                commands.entity(e).insert((Health(r.max_health), r, bounds, crate::ai::Player));
                info.player = Some(e);
            }
            continue;
        }
        // Q and Jonesy follow Roman
        if inst.template == "q111" || inst.template == "molmer" {
            if let Some(walker) = physics.add_walker(pos, p.f("collision_radius", 4.0).clamp(2.0, 5.0), 10.0, 50f32.to_radians()) {
                let (yaw, _, _) = (rot * game_to_engine().inverse()).to_euler(EulerRot::YXZ);
                commands.entity(e).insert((
                    crate::ai::Companion::new(walker, &p, data, inst.template == "q111", yaw, game_to_engine()),
                    Health(p.f("max_health", 500.0)),
                    bounds,
                ));
            }
            continue;
        }
        // characters: property sets with an AI mode (grunts). Their AI here is a stand-in driven by the game's
        // own parameters (speeds, ranges); the recovered AI (src/ai) replaces it later.
        if p.has("mode") && inst.model.starts_with("grunt_") {
            // window / tower grunts (AI_ResolveStartObjects 0x00425e20): placed at the generic_txt bone of the object
            // whose tag is obj_tag (the house), physics off: they stay there. Facing: away from the object's centre
            // (guess: the original takes the bone's whole matrix)
            let mode = p.s("mode").unwrap_or("").to_owned();
            let mut pos = pos;
            let mut window_yaw = None;
            if mode.contains("window") || mode.contains("tower") {
                let tag = p.f("obj_tag", 0.0) as i64;
                let house = scene.instances.iter().find(|h| h.props.get("tag").and_then(|v| v.as_i64()) == Some(tag) && tag != 0);
                if let (Some(h), Some(bone)) = (house, p.s("generic_txt")) {
                    if let Some(b) = crate::mission::pub_bone(&lib.dir, &h.model, bone) {
                        let m = crate::scene::game_matrix(&h.transform);
                        let at = game_to_engine() * m.transform_point3(b);
                        let centre = game_to_engine() * m.w_axis.truncate();
                        let out = (at - centre).with_y(0.0).normalize_or_zero();
                        pos = at;
                        window_yaw = (out != Vec3::ZERO).then(|| out.x.atan2(out.z));
                    }
                }
            }
            let shape = ai::capsule(physics, p.f("collision_radius", 4.0));
            let lift = Vec3::Y * ai::capsule_lift(p.f("collision_radius", 4.0));
            let s = BodySettings::new_dynamic()
                .mass(80.0)
                .friction(0.0)
                .allowed_dofs(AllowedDofs::TRANSLATION_X | AllowedDofs::TRANSLATION_Y | AllowedDofs::TRANSLATION_Z);
            if let Some(id) = physics.add_dynamic(e, &shape, pos + lift, Quat::IDENTITY, Vec3::ZERO, s) {
                let (yaw, _, _) = (rot * game_to_engine().inverse()).to_euler(EulerRot::YXZ);
                physics.materials.insert(id, p.s("material").unwrap_or("grunt").to_owned());
                let mut g = ai::make_grunt(&p, data, pos, window_yaw.unwrap_or(yaw), &mut rng);
                if window_yaw.is_some() {
                    g.fixed = Some(pos);
                }
                commands.entity(e).insert((
                    Body { id, offset: lift, sync_rotation: false },
                    Health(p.f("max_health", 100.0)),
                    bounds,
                    g,
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
        if let (Some(c), Some(m)) = (collider, p.s("material")) {
            physics.materials.insert(c, m.to_owned());
        }
        // bullets hit only objects in the projectile list World +0xe90 (Projectile_Update 0x0047e580, filter
        // 0x00413a90): buildings (unless base_defend or invisible), trees, kegs, turrets always; objects without a
        // behaviour only when they take damage or block projectiles (game_03.cpp:560); invisible_material never
        // (0x004138c0). Invisible walls (blocks_projectiles 0) let bullets through.
        if let Some(c) = collider {
            let flag = |k: &str| p.f(k, 0.0) != 0.0;
            let listed = match behavior {
                "building" => !(invisible || flag("base_defend")),
                "tree" | "fx_explosion" | "rocket_tower_turret" => true,
                "" => (flag("takes_damage") || flag("blocks_projectiles")) && !flag("no_collide"),
                _ => true,
            };
            if !listed || p.s("material") == Some("invisible_material") {
                physics.no_bullets.insert(c);
            }
        }
        commands.entity(e).insert((Placed { template: inst.template.clone(), collider }, bounds, crate::hud::AimClass::of(&p)));
        // attach_audio_loop (the oil pumps' amb_oil_pump_lp): a loop on the object while it lives
        if let Some(name) = p.s("attach_audio_loop").filter(|n| !n.is_empty() && *n != "none") {
            commands.entity(e).insert(AudioLoop(name.to_owned()));
        } else if behavior == "pub" {
            // the pub's own loop (Pub_StartExteriorLoop 0x0046a790: "amb_pub_exterior_lp", attached, once)
            commands.entity(e).insert(AudioLoop("amb_pub_exterior_lp".into()));
        }
        if let Some(d) = destructible(&p) {
            commands.entity(e).insert(d);
            info.destructibles += 1;
        } else if inst.template == "penguin" {
            // in the projectile and area damage lists (+0xe90, +0xe94): 1 health, no filter
            commands.entity(e).insert((
                Destructible {
                    health: 1.0,
                    fx_death: None,
                    husk: None,
                    area_damage: 0.0,
                    radius_inner: 0.0,
                    radius_outer: 0.0,
                    impact_force: 0.0,
                    chain: false,
                    start_delay: 0.0,
                    min_delay: 0.0,
                    acceleration: 1.0,
                    dying: None,
                    chain_delay: 0.0,
                    class: DClass::Open,
                    score: p.f("score_value", -10.0),
                    takes_damage: true,
                    takes_bullet: true,
                    player_damage: true,
                    teammate_damage: true,
                    badguy_damage: true,
                },
                Penguin::default(),
            ));
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

    // The sticky bomb (template player_sticky_bomb).
    let parts = lib.load_model("player_stickybomb_0", false, assets, meshes, materials).map(|m| m.parts.clone()).unwrap_or_default();
    let dynamite = lib.load_model("molmer_dynamite_0", false, assets, meshes, materials).map(|m| m.parts.clone()).unwrap_or_default();
    // the grunts' collision model (templates grunt / grunt_regular: collision_lod_index 3 = grunt_coll_9), for bullet hits
    commands.insert_resource(crate::hitparts::HitHulls {
        grunt: crate::hitparts::load(&lib.dir, "grunt_coll_9"),
        roman: crate::hitparts::load(&lib.dir, "trotter_coll_0"),
        molmer: crate::hitparts::load(&lib.dir, "molmer_coll_0"),
        q111: crate::hitparts::load(&lib.dir, "q111_coll_0"),
    });
    commands.insert_resource(crate::sticky::StickyAssets { parts, dynamite, base: game_to_engine() });

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
        let mut light = commands.spawn((
            DirectionalLight {
                color: Color::srgb(p("redvalue") / 255.0, p("greenvalue") / 255.0, p("bluevalue") / 255.0),
                illuminance: 9000.0 * p("intensity").max(0.1),
                shadow_maps_enabled: first,
                ..default()
            },
            Transform::default().looking_to(dir, Vec3::Y),
            CascadeShadowConfigBuilder { maximum_distance: extent * 0.5, first_cascade_far_bound: 60.0, ..default() }.build(),
        ));
        if first {
            light.insert(crate::options::ShadowLight);
        }
        first = false;
    }
    if first {
        commands.spawn((
            DirectionalLight { illuminance: 9000.0, shadow_maps_enabled: true, ..default() },
            Transform::default().looking_to(Vec3::new(-0.4, -1.0, -0.3), Vec3::Y),
            crate::options::ShadowLight,
        ));
    }
    info
}

/// An object's attach_audio_loop sound (0x00450490 reads it into +0x4f4 with flag 0x20000; the per-frame update
/// 0x004505a0 starts it as a positional loop on the object, and its death stops it).
#[derive(Component)]
pub struct AudioLoop(pub String);

fn audio_loops(
    new: Query<(Entity, &AudioLoop), Added<AudioLoop>>,
    mut died: MessageReader<Died>,
    sounds: Query<(Entity, &crate::audio::Positional)>,
    mut play: MessageWriter<crate::audio::PlaySound>,
    mut commands: Commands,
) {
    for (e, l) in &new {
        play.write(crate::audio::PlaySound { name: l.0.clone(), at: None, follow: Some(e), looped: Some(true), volume: 1.0 });
    }
    for d in died.read() {
        for (s, p) in &sounds {
            if p.follow == Some(d.0) {
                commands.entity(s).despawn();
            }
        }
    }
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
    mut penguins: Query<&mut Penguin>,
) {
    for d in died.read() {
        let Ok((p, t, children)) = placed.get(d.0) else { continue };
        if let Some(c) = p.collider {
            physics.remove_static(c);
        }
        // a penguin keeps its model: its death anims play (charanim.rs)
        if let Ok(mut pg) = penguins.get_mut(d.0) {
            pg.dead = Some(0.0);
            continue;
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
            let _ = attach_model(&mut commands, d.0, m, true, None);
        }
        let collider = ht
            .collision
            .as_ref()
            .and_then(|n| lib.load_model(n, false, &assets, &mut meshes, &mut materials).map(|m| (m.vertices.clone(), m.tris.clone())))
            .and_then(|(v, tr)| {
                let v: Vec<Vec3> = v.iter().map(|x| world.transform_point3(*x)).collect();
                physics.add_static_mesh(&v, &tr)
            });
        let aim = crate::hud::AimClass::of(&data.template_props(husk.as_deref().unwrap_or_default()));
        commands.entity(d.0).insert((Placed { template: husk.unwrap_or_default(), collider }, aim));
    }
}

#[allow(clippy::too_many_arguments)]
fn spawn_grunts(
    mut commands: Commands,
    mut events: MessageReader<SpawnGrunt>,
    data: Res<GameData>,
    mut lib: ResMut<Library>,
    mut anim_lib: ResMut<crate::skin::AnimLibrary>,
    mut binds: ResMut<Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>>,
    mut physics: NonSendMut<Physics>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    player: Query<Entity, With<crate::ai::Player>>,
    mut rng: Local<crate::fxvm::CrtRand>,
) {
    for ev in events.read() {
        let p = data.template_props(&ev.template);
        let Some(model_name) = data.templates.get(&ev.template).and_then(|t| t.model.clone()) else { continue };
        let Some(model) = lib.load_model(&model_name, false, &assets, &mut meshes, &mut materials).cloned_parts() else { continue };
        let world = Mat4::from_rotation_translation(game_to_engine(), ev.at);
        let e = commands.spawn((Transform::from_matrix(world), Visibility::default(), Name::new(ev.template.clone()))).id();
        if ev.alarm {
            commands.entity(e).insert(crate::alarm::AlarmTroop);
        }
        let mut ctx = SkinCtx { lib: &mut anim_lib, binds: &mut binds };
        let (_, _, hand) = attach_model(&mut commands, e, &model, true, Some(&mut ctx));
        if let (Some(h), Some(w)) = (hand, data.templates.get("grunt_rifle").and_then(|t| t.model.clone())) {
            if let Some(m) = lib.load_model(&w, false, &assets, &mut meshes, &mut materials) {
                attach_weapon(&mut commands, h, m, Transform::IDENTITY);
            }
        }
        let shape = ai::capsule(&physics, p.f("collision_radius", 4.0));
        let lift = Vec3::Y * ai::capsule_lift(p.f("collision_radius", 4.0));
        let s = BodySettings::new_dynamic()
            .mass(80.0)
            .friction(0.0)
            .allowed_dofs(AllowedDofs::TRANSLATION_X | AllowedDofs::TRANSLATION_Y | AllowedDofs::TRANSLATION_Z);
        if let Some(id) = physics.add_dynamic(e, &shape, ev.at + lift, Quat::IDENTITY, Vec3::ZERO, s) {
            physics.materials.insert(id, "grunt".into());
            let mut g = ai::make_grunt(&p, &data, ev.at, 0.0, &mut rng);
            // spawned troops charge the player (0x0042dd50 -> state 0x5b -> 0x0042ce70)
            g.target = player.single().ok();
            g.mind = ai::Mind::Chase;
            let bounds = Bounds { center: (model.min + model.max) * 0.5, radius: (model.max - model.min).length() * 0.5 };
            commands.entity(e).insert((Body { id, offset: lift, sync_rotation: false }, Health(p.f("max_health", 100.0)), bounds, g));
        }
    }
}
