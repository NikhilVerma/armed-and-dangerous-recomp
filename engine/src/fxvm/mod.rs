//! The fx system: the game's fx script interpreter (Fx_RunScript 0x00420770), its fx objects, particles,
//! streamers, attach points, decals, lights and screen effects, ported from the recovered C++ per
//! docs/subsystems/fx-vm.md. Scripts come unchanged from the level's fx bank (fx.json); game space (Z up, game units)
//! throughout, drawing converts to engine axes.
//!
//! Public API: send `crate::fx::StartFx` to start a script; read `FxVm::stats` / `FxVm::lights`; `ScreenEffects`
//! (screen.rs) for the post-process effects. Modules:
//! - `script`: the interpreter (one instance at a time, opcode by opcode) and fx object creation
//! - `object`: the fx object update (life, fades, scale, texture animation, uv transforms, emitters, hits, death)
//! - `particles`: particle emitters, the three pools, simulation and drawing
//! - `streamers`: ribbon trails
//! - `attach`: attach points (named nodes of an object's anim) for the 0x104 name lists
//! - `body`: the physics glue (dynamic bodies, impulses, spins, ground hits)
//! - `screen`: the post-process effect manager (fades, colour, blur) started by opcodes 0x5b..0x77
//! - `materials`: the particle and layered fx materials; `rng`: the game's rand()
//!
//! The fx system's own plumbing is replaced by Rust ownership: FxSystem_Construct / Destruct / ResetPools / Unload
//! (0x0041c700 / 0x0041c760 / 0x0041c9a0 / 0x0041c9d0) by this resource (made at start-up, its objects despawned with
//! the level), FxInstance_Alloc 0x0041cc20 by `Instance`, the instance list walk Fx_UpdateAll 0x004237b0 by `run`, the
//! push stack FxStack_Push / Pop 0x00423430 / 0x00423470 by a Vec in `script::run_script`.

mod attach;
mod body;
mod materials;
mod object;
mod particles;
mod rng;
pub mod screen;
mod script;
mod streamers;

pub use body::IMPULSE_SCALE;
pub use materials::{FxLayerMaterial, ParticleMaterial};
pub use rng::CrtRand;

use crate::fx::StartFx;
use crate::levelreset::LevelAppExt;
use crate::gamedata::GameData;
use crate::level::game_to_engine;
use bevy::prelude::*;
use std::collections::{BTreeMap, HashMap, HashSet};

/// Marks the entity of an fx object (its state lives in `FxVm::objects`, so a script can set it up in the same
/// frame it creates it).
#[derive(Component)]
pub struct FxEntity;

/// A point light an fx object carries (Fx_SetLight 0x0041da90, 0x58 / 0x59 / 0x5a), where it is this frame: the
/// original's D3D point light (diffuse, specular, attenuation 1 + a1 d, range 1e6) re-placed at its attach point
/// every update (render_r1.cpp:1526). Published for the renderer; the original shows no visible effect from them
/// (docs/engine-limits.md R4: only the third shader light slot takes them), so nothing draws them yet.
#[derive(Clone, Copy, Debug)]
pub struct FxLight {
    /// engine space
    pub position: Vec3,
    pub diffuse: Vec3,
    pub specular: Vec3,
    pub ambient: Vec3,
    /// Attenuation1: the light is at half strength at 1 / a1 units
    pub attenuation1: f32,
}

#[derive(Resource)]
pub struct FxVm {
    instances: Vec<script::Instance>,
    /// by entity (ordered: the per-frame update draws from the shared random stream and plays sounds object by
    /// object, so a hash map's per-process order made runs differ, docs/remaster-engine.md "Traps")
    objects: BTreeMap<Entity, object::FxObject>,
    pools: particles::Pools,
    streamers: Vec<streamers::Streamer>,
    streamer_meshes: streamers::StreamerMeshes,
    attach: attach::AttachPoints,
    pub rng: CrtRand,
    unported: HashSet<String>,
    pub stats: FxVmStats,
    /// the lights of the live fx objects, this frame
    pub lights: Vec<FxLight>,
    /// objects made per template since start (or the last playtest clear), for playtests
    pub made: HashMap<String, u32>,
    /// instances started per script name (the material variant resolved) since start or the last playtest clear
    pub started: HashMap<String, u32>,
    /// game time (seconds since start): the on-hit spawn spacing (0x0041fc70, [0x00604a98])
    time: f32,
    out: Outbox,
}

/// What a script run or an object update asks of other systems this frame (sent by `run` / `objects`).
#[derive(Default)]
struct Outbox {
    sounds: Vec<crate::audio::PlaySound>,
    camera_paths: Vec<String>,
    decals: Vec<crate::decals::SpawnDecal>,
    explosions: Vec<crate::fx::Explode>,
    forces: Vec<script::FxForce>,
    shakes: Vec<crate::camera::ShakeCamera>,
    screen: Vec<screen::ScreenRequest>,
}

#[derive(Default, Clone, Copy)]
pub struct FxVmStats {
    pub instances: usize,
    pub objects: usize,
    pub particles: usize,
    pub dropped: usize,
    /// ground hits of fx bodies so far (the on-hit opcodes' trigger)
    pub hits: u32,
    /// on-hit area damages sent (0x76)
    pub blasts: u32,
    pub ms: f32,
}

pub struct FxVmPlugin;

/// The fx update, after the object model's damage and deaths (crate::fx::FxSet).
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct FxVmSet;

impl Plugin for FxVmPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "particle.wesl");
        bevy::asset::embedded_asset!(app, "fxlayer.wesl");
        app.add_plugins(MaterialPlugin::<FxLayerMaterial>::default());
        app.add_plugins(MaterialPlugin::<ParticleMaterial>::default());
        app.add_plugins(screen::ScreenFxPlugin);
        app.add_level_spawner(setup)
            .add_systems(
                Update,
                (start, script::run, object::objects, particles_step, send_outbox, playtest_state).chain().in_set(FxVmSet).after(crate::fx::FxSet),
            )
            .add_systems(Update, playtest_fx.before(start).run_if(resource_exists::<crate::playtest::Playtest>))
            .add_systems(PostUpdate, (object::follow, particles::draw_particles).before(TransformSystems::Propagate))
            .add_systems(PostUpdate, streamers::streamers.after(TransformSystems::Propagate));
    }
}

fn setup(mut commands: Commands, assets: Res<AssetServer>, mut meshes: ResMut<Assets<Mesh>>, mut materials: ResMut<Assets<ParticleMaterial>>, mut std_mats: ResMut<Assets<StandardMaterial>>, launch: Option<Res<crate::Launch>>) {
    // the untextured streamers' two meshes (alpha-blended, additive)
    let streamer_meshes = streamers::StreamerMeshes([false, true].into_iter().map(|additive| ((None, additive), streamers::streamer_mesh(&mut commands, &mut meshes, &mut std_mats, &assets, None, additive))).collect());
    let pools = particles::setup_pools(&mut commands, &assets, &mut meshes, &mut materials);
    let dir = launch.map(|l| l.scene.clone()).unwrap_or_default();
    commands.insert_resource(FxVm {
        instances: Vec::new(),
        objects: BTreeMap::new(),
        pools,
        streamers: Vec::new(),
        streamer_meshes,
        attach: attach::AttachPoints::new(dir),
        rng: CrtRand::seeded(1),
        unported: HashSet::new(),
        stats: FxVmStats::default(),
        lights: Vec::new(),
        made: HashMap::new(),
        started: HashMap::new(),
        time: 0.0,
        out: Outbox::default(),
    });
}

fn engine_to_game() -> Quat {
    game_to_engine().inverse()
}

/// StartFx -> a new instance (Fx_Create 0x0041cc60; FxBank_Find 0x0041cb60 by name). A material variant the bank
/// lacks falls back to the base fx (fx_imp_arrow_grunt -> fx_imp_arrow: Fx_Create's "%s_%s" with the material).
/// `Quat::IDENTITY` is an unrotated placement in game space (the original's position-only start, params flag 8),
/// as every caller giving no rotation means it; any other rotation is the engine-space rotation of the placement.
/// Placed fx entities (fn_004102e0 0x004102e0: an fx entity's init starts its effect) and the fx ears' toggles
/// (FxOwner_Toggle 0x0041bf60) come here through level.rs and mission.rs.
fn start(mut vm: ResMut<FxVm>, data: Res<GameData>, mut events: MessageReader<StartFx>) {
    for ev in events.read() {
        let name = ev.name.to_lowercase();
        // debug: FXSKIP=name1,name2 does not start those scripts
        if std::env::var("FXSKIP").is_ok_and(|v| v.split(',').any(|n| n == name)) {
            continue;
        }
        if std::env::var("FXLOG").is_ok() {
            info!("FXLOG start {name} at {:?}", ev.at);
        }
        let Some((resolved, script)) = script::find(&data, &name) else {
            warn!("fx {} not in the level's fx bank", ev.name);
            continue;
        };
        let rot = if ev.rotation == Quat::IDENTITY { game_to_engine() } else { ev.rotation };
        let place = Mat4::from_quat(engine_to_game()) * Mat4::from_rotation_translation(rot, ev.at);
        let mut inst = script::Instance::new(resolved, script, place);
        inst.parent = ev.follow;
        inst.follow = ev.follow.is_some();
        inst.follow_pos_only = ev.position_only;
        vm.instances.push(inst);
    }
}

/// FxSystem_KillAll 0x0041cab0: every fx object destroyed (vt+0xdc) and every instance released, except those on
/// `keep` (the camera's: the weather domes).
pub fn kill_all(vm: &mut FxVm, commands: &mut Commands, physics: &mut crate::physics::Physics, keep: Option<Entity>) {
    let kept = |p: Option<Entity>| keep.is_some() && p == keep;
    let gone: Vec<Entity> = vm.objects.iter().filter(|(_, o)| !kept(o.follow.map(|f| f.0))).map(|(e, _)| *e).collect();
    for e in gone {
        if let Some(b) = vm.objects.remove(&e).and_then(|o| o.body) {
            body::remove(physics, b);
        }
        commands.entity(e).despawn();
    }
    vm.instances.retain(|i| kept(i.parent));
    vm.streamers.clear();
    for p in &mut vm.pools.live {
        p.clear();
    }
}

/// The playtest `fx` step: start a script at a game-space point (default Roman's feet) facing `yaw`; `clear` first
/// ends every live fx but the camera's (the snow domes) and resets the "fx" state's maxima.
fn playtest_fx(
    mut commands: Commands,
    mut pt: ResMut<crate::playtest::Playtest>,
    mut vm: ResMut<FxVm>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut out: MessageWriter<StartFx>,
    mut state: Option<ResMut<crate::playtest::PlayState>>,
    roman: Query<&GlobalTransform, With<crate::player::Roman>>,
    cams: Query<Entity, With<Camera3d>>,
) {
    for f in std::mem::take(&mut pt.fx) {
        if f.get("clear").and_then(|v| v.as_bool()).unwrap_or(false) {
            kill_all(&mut vm, &mut commands, &mut physics, cams.iter().next());
            vm.made.clear();
            vm.started.clear();
            if let Some(s) = state.as_mut() {
                s.0.remove("fx");
            }
        }
        let Some(name) = f.get("name").and_then(|v| v.as_str()) else { continue };
        let at = f
            .get("at")
            .and_then(|v| v.as_array())
            .map(|a| Vec3::new(a[0].as_f64().unwrap_or(0.0) as f32, a[1].as_f64().unwrap_or(0.0) as f32, a[2].as_f64().unwrap_or(0.0) as f32))
            .map(|g| game_to_engine() * g)
            .or_else(|| roman.single().ok().map(|t| t.translation()));
        let Some(at) = at else { continue };
        let yaw = f.get("yaw").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
        // a game-space turn about z, as an engine-space rotation
        let rotation = game_to_engine() * Quat::from_rotation_z(yaw.to_radians());
        out.write(StartFx { name: name.to_owned(), at, rotation, follow: None, position_only: false });
    }
}

/// The particle update (Particles_Draw 0x0046d9f0's simulation; dt = 0 while paused: Fx_DrawParticles 0x00423820).
fn particles_step(time: Res<Time>, mut vm: ResMut<FxVm>) {
    particles::simulate(&mut vm.pools, time.delta_secs());
    vm.stats.particles = vm.pools.count();
    vm.stats.dropped = vm.pools.dropped;
}

/// Sends what scripts and objects asked for this frame.
#[allow(clippy::too_many_arguments)]
fn send_outbox(
    mut vm: ResMut<FxVm>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut paths: MessageWriter<crate::campath::StartCameraPath>,
    mut decals: MessageWriter<crate::decals::SpawnDecal>,
    mut blasts: MessageWriter<crate::fx::Explode>,
    mut shakes: MessageWriter<crate::camera::ShakeCamera>,
    mut screen: ResMut<screen::ScreenEffects>,
) {
    let out = std::mem::take(&mut vm.out);
    sounds.write_batch(out.sounds);
    paths.write_batch(out.camera_paths.into_iter().map(crate::campath::StartCameraPath));
    decals.write_batch(out.decals);
    blasts.write_batch(out.explosions);
    shakes.write_batch(out.shakes);
    for r in out.screen {
        screen.apply(r);
    }
    for fo in out.forces {
        // 0x3f force feedback: not consumed (the remaster's rumble lives in input.rs)
        if std::env::var("FXLOG").is_ok() {
            info!("FXLOG force {} looping {} player only {} distance {}..{} on {:?}", fo.name, fo.looping, fo.player_only, fo.min, fo.max, fo.owner);
        }
    }
    if !vm.unported.is_empty() {
        let list: Vec<_> = vm.unported.drain().collect();
        for u in list {
            info!("fx vm: not ported: {u}");
        }
    }
}

/// The playtest state "fx": live instances / objects / particles per pool / streamers / lights and the dropped count,
/// so playtests can check what an effect makes.
fn playtest_state(vm: Res<FxVm>, mut state: Option<ResMut<crate::playtest::PlayState>>, screen: Res<screen::ScreenEffects>) {
    let Some(state) = state.as_mut() else { return };
    let pools: Vec<usize> = vm.pools.live.iter().map(Vec::len).collect();
    let bodies = vm.objects.values().filter(|o| o.has_body()).count();
    let top = vm.objects.values().map(|o| o.place.w_axis.z).fold(f32::MIN, f32::max);
    // live objects per template, and the horizontal extent of the live objects (where debris starts: attach points)
    let mut by_template = serde_json::Map::new();
    let (mut lo, mut hi) = (Vec2::splat(f32::MAX), Vec2::splat(f32::MIN));
    for o in vm.objects.values() {
        let n = by_template.get(&o.template).and_then(|v| v.as_u64()).unwrap_or(0);
        by_template.insert(o.template.clone(), (n + 1).into());
        let p = o.place.w_axis.truncate().truncate();
        lo = lo.min(p);
        hi = hi.max(p);
    }
    let extent = if vm.objects.is_empty() { 0.0 } else { (hi - lo).length() };
    let acc = screen.accum;
    let streamer_points: usize = vm.streamers.iter().map(|s| s.points()).sum();
    let prev = state.0.get("fx").cloned().unwrap_or_default();
    let max = |k: &str, v: usize| prev.get(k).and_then(|x| x.as_u64()).map_or(v, |m| (m as usize).max(v));
    state.0.insert(
        "fx".into(),
        serde_json::json!({
            "instances": vm.instances.len(),
            "objects": vm.objects.len(),
            "bodies": bodies,
            "particles": vm.pools.count(),
            "pool0": pools[0], "pool1": pools[1], "pool2": pools[2],
            "max_particles": max("max_particles", vm.pools.count()),
            "max_objects": max("max_objects", vm.objects.len()),
            "max_bodies": max("max_bodies", bodies),
            "max_lights": max("max_lights", vm.lights.len()),
            "streamers": vm.streamers.len(),
            "streamer_points": streamer_points,
            "lights": vm.lights.len(),
            "light": vm.lights.first().map(|l| serde_json::json!({"x": l.position.x, "y": l.position.y, "z": l.position.z, "r": l.diffuse.x, "g": l.diffuse.y, "b": l.diffuse.z, "specular": l.specular.x + l.specular.y + l.specular.z, "ambient": l.ambient.x + l.ambient.y + l.ambient.z, "attenuation1": l.attenuation1})),
            "by_template": by_template,
            "made": vm.made,
            "started": vm.started,
            "blasts": vm.stats.blasts,
            "extent": extent,
            "max_extent": prev.get("max_extent").and_then(|x| x.as_f64()).map_or(extent, |m| (m as f32).max(extent)),
            "screen_fade": acc.fade.1,
            "screen_glow": acc.colour[2],
            "max_screen_fade": prev.get("max_screen_fade").and_then(|x| x.as_f64()).map_or(acc.fade.1, |m| (m as f32).max(acc.fade.1)),
            "dropped": vm.pools.dropped,
            "top_z": if vm.objects.is_empty() { serde_json::Value::Null } else { serde_json::json!(top) },
            "hits": vm.stats.hits,
            "screen": screen.live(),
            "max_screen": max("max_screen", screen.live()),
        }),
    );
}

