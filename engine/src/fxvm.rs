//! The game's fx script interpreter (Fx_RunScript 0x00420770), ported from the recovered C++ per
//! docs/subsystems/fx-vm.md, and its particle system (Particles_Draw 0x0046d9f0). Scripts come unchanged from
//! the level's fx bank (fx.json). Game space (Z up, game units) throughout; drawing converts to engine axes.
//!
//! Not ported yet (logged once per opcode): sounds (0x39 picks its random name so the RNG order is kept),
//! decals (0x6a), uv scrolling (0x40), the attach points of 0x104 lists (the object origin is used).

use crate::fx::StartFx;
use crate::gamedata::GameData;
use crate::level::game_to_engine;
use crate::physics::{Body, Physics, jv};
use crate::scene::Library;
use bevy::asset::RenderAssetUsages;
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::prelude::*;
use oxijolt::prelude::*;
use serde_json::Value;
use std::collections::HashSet;

/// MSVC CRT rand() (guess for 0x005391cd, docs/subsystems/fx-vm.md), so random draws follow the original.
#[derive(Default)]
pub struct CrtRand(u32);

impl CrtRand {
    pub fn rand(&mut self) -> u32 {
        self.0 = self.0.wrapping_mul(214013).wrapping_add(2531011);
        (self.0 >> 16) & 0x7fff
    }
    /// Rand_FloatRange 0x0046e2c0: (b - a) * rand() / 32768 + a.
    pub fn r(&mut self, a: f32, b: f32) -> f32 {
        (b - a) * self.rand() as f32 * (1.0 / 32768.0) + a
    }
}

/// Layered fx meshes (the snow domes): the original's three-stage set-up (fxlayer.wesl), additive.
#[derive(bevy::render::render_resource::AsBindGroup, Debug, Clone, Asset, bevy::reflect::TypePath)]
pub struct FxLayerMaterial {
    #[texture(0)]
    #[sampler(1)]
    pub t0: Handle<Image>,
    #[texture(2)]
    #[sampler(3)]
    pub t1: Handle<Image>,
    #[texture(4)]
    #[sampler(5)]
    pub t2: Handle<Image>,
    #[uniform(6)]
    pub params: FxLayerParams,
}

#[derive(bevy::render::render_resource::ShaderType, Debug, Clone, Default)]
pub struct FxLayerParams {
    pub offsets: Vec4,
    pub colour: Vec4,
    /// t0 / t1 / t2 stored premultiplied (1) or not (0)
    pub flags: Vec4,
}

/// The export's per-texture `premultiplied` flag (scene.json textures: DXT2 in the bank), read once.
fn premultiplied(dir: &std::path::Path, name: &str) -> bool {
    static FLAGS: std::sync::OnceLock<std::collections::HashMap<String, bool>> = std::sync::OnceLock::new();
    FLAGS
        .get_or_init(|| {
            let text = std::fs::read_to_string(dir.join("scene.json")).unwrap_or_default();
            let v: Value = serde_json::from_str(&text).unwrap_or(Value::Null);
            v.get("textures")
                .and_then(|t| t.as_object())
                .map(|t| t.iter().map(|(k, x)| (k.to_lowercase(), x.get("premultiplied").and_then(|p| p.as_bool()).unwrap_or(false))).collect())
                .unwrap_or_default()
        })
        .get(&name.to_lowercase())
        .copied()
        .unwrap_or(false)
}

impl Material for FxLayerMaterial {
    fn fragment_shader() -> bevy::shader::ShaderRef {
        bevy::shader::ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("fxlayer.wesl")).with_source("embedded"))
    }
    fn alpha_mode(&self) -> AlphaMode {
        AlphaMode::Add
    }
}

/// The particle material: bank texture x vertex colour, additive or alpha-blended (particle.wgsl).
#[derive(bevy::render::render_resource::AsBindGroup, Debug, Clone, Asset, bevy::reflect::TypePath)]
pub struct ParticleMaterial {
    #[texture(0)]
    #[sampler(1)]
    pub bank: Handle<Image>,
    pub additive: bool,
    /// x: the alpha exponent (alpha-blended pools: 2.2, the original blends in gamma space; additive: 1)
    #[uniform(2)]
    pub params: Vec4,
}

impl Material for ParticleMaterial {
    fn fragment_shader() -> bevy::shader::ShaderRef {
        bevy::shader::ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("particle.wesl")).with_source("embedded"))
    }
    fn alpha_mode(&self) -> AlphaMode {
        if self.additive { AlphaMode::Add } else { AlphaMode::Blend }
    }
}

/// Particle pools (Fx_LoadChunk 0x0041c7e0): capacity and bank texture. The remaster multiplies the
/// capacities (the original refuses particles when a pool is full; docs/engine-limits.md R5).
const POOLS: [(usize, &str, bool); 3] = [(2000, "n_fx_bank_1", true), (4000, "n_fx_bank_2", false), (2000, "n_fx_bank_2", false)];
pub const POOL_SCALE: usize = 16;
/// Particle gravity unit ([0x005bcd64]).
const PARTICLE_G: f32 = -64.348_1;

#[derive(Clone)]
struct Emitter {
    pool: usize,
    interval: f32,
    timer: f32,
    size: (f32, f32),
    stretch: (f32, f32),
    growth: (f32, f32),
    drag: (f32, f32),
    speed: (f32, f32),
    life: (f32, f32),
    cone: (f32, f32),
    gravity: (f32, f32),
    uv: [f32; 4],
    /// Cone axis (game space); fixed once 0x46 set it.
    dir: Vec3,
    fixed_dir: bool,
    /// 0x47: the rotation re-applied to the object's direction every step (row vectors: v * M)
    rel: Option<Mat3>,
    prev_pos: Option<Vec3>,
    /// 0x49 alpha ramp: 12 entries.
    ramp: Option<[f32; 12]>,
}

impl Emitter {
    fn new(pool: usize, interval: f32, dir: Vec3) -> Self {
        Emitter {
            pool,
            interval: interval.max(1e-4),
            timer: 0.0,
            size: (0.2, 0.3),
            stretch: (1.0, 1.0),
            growth: (0.0, 0.0),
            drag: (1e-6, 1e-6),
            speed: (8.0, 30.0),
            life: (4.0, 7.0),
            cone: (36.0, 36.0),
            gravity: (0.0, 0.0),
            uv: [0.0, 1.0, 0.0, 1.0],
            dir,
            fixed_dir: false,
            rel: None,
            prev_pos: None,
            ramp: None,
        }
    }
}

/// Marks the entity of an fx object (its state lives in FxVm::objects, so a script can set it up in the
/// same frame it creates it).
#[derive(Component)]
pub struct FxEntity;

/// A live fx object (class `fx`): a template's mesh with the script's timers, fades, scaling and emitters.
pub struct FxObject {
    /// Game-space placement (rotation + position) without the scale animation.
    pub place: Mat4,
    age: f32,
    lifespan: Option<f32>,
    fade_in: Option<(f32, f32, bool)>,
    fade_out: Option<f32>,
    scale: Option<(f32, f32, u8)>,
    kite: bool,
    emitters: Vec<Emitter>,
    texanim: Option<(f32, u32, bool, f32, u32, String)>,
    shake: Option<(f32, f32, f32)>,
    /// 0x2f children follow their parent object.
    follow: Option<Entity>,
    materials: Vec<Handle<StandardMaterial>>,
    alpha: f32,
    body: Option<BodyId>,
    /// 0x40 uv scroll: (direction, speed, duration, ramp in, ramp out, offset) of layer 1 (the meshes' one stage)
    uv_scroll: Option<(Vec2, f32, f32, f32, f32, Vec2)>,
    /// follow only the parent's position (camera_fx position_only)
    follow_pos_only: bool,
    /// layered parts (FxLayerMaterial) and the uv scroll of layer 2
    layer_mats: Vec<Handle<FxLayerMaterial>>,
    uv_scroll2: Option<(Vec2, f32, f32, f32, f32, Vec2)>,
}

/// A streamer (ribbon trail, render_r4.cpp: Streamers 0x00494d90.., Streamer_AddPoint 0x004958a0, Streamer_Update
/// 0x00495300, Streamer_Draw 0x00495380): one point per frame at its fx object while emitting (63 at most), points
/// older than `life` dropped; drawn as a camera-facing strip whose half-width, colour and alpha are Bezier curves of
/// the point's age / life (4 keys each). Defaults 0x00494d40: life 1, width [1, 1, 1, 0], colour white with the last
/// key's alpha 0, alpha blend.
struct Streamer {
    owner: Entity,
    life: f32,
    width: [f32; 4],
    /// per key: r, g, b, a (each quantised to a byte as the original stores them)
    colour: [[f32; 4]; 4],
    additive: bool,
    time: f32,
    emitting: bool,
    /// (position (engine space), direction, birth time, size)
    points: std::collections::VecDeque<(Vec3, Vec3, f32, f32)>,
}

impl Streamer {
    fn new(owner: Entity) -> Self {
        let w = [1.0; 4];
        Streamer {
            owner,
            life: 1.0,
            width: [1.0, 1.0, 1.0, 0.0],
            colour: [w, w, w, [1.0, 1.0, 1.0, 0.0]],
            additive: false,
            time: 0.0,
            emitting: true,
            points: Default::default(),
        }
    }
}

/// A curve argument (render_r2b.cpp:853-881): 1 value constant, 2 a linear ramp, 4 as given, else 1.
fn curve_keys(v: &Value) -> [f32; 4] {
    let n: Vec<f32> = v.as_array().map(|a| a.iter().filter_map(|x| x.as_f64()).map(|x| x as f32).collect()).unwrap_or_default();
    match n.len() {
        1 => [n[0]; 4],
        2 => {
            let k1 = n[0] + (n[1] - n[0]) / 3.0;
            [n[0], k1, (k1 + n[1]) * 0.5, n[1]]
        }
        4 => [n[0], n[1], n[2], n[3]],
        _ => [1.0; 4],
    }
}

fn bezier(k: [f32; 4], t: f32) -> f32 {
    let u = 1.0 - t;
    u * u * u * k[0] + 3.0 * t * u * u * k[1] + 3.0 * t * t * u * k[2] + t * t * t * k[3]
}

struct Instance {
    script: Vec<Value>,
    ip: usize,
    paused: f32,
    waiting: bool,
    /// Placement of objects this fx creates (game space) and, for 0x2f children, the object they follow.
    place: Mat4,
    parent: Option<Entity>,
    follow: bool,
    follow_pos_only: bool,
}

struct Particle {
    pos: Vec3,
    prev: Vec3,
    vel: Vec3,
    life: f32,
    birth_t: f32,
    size: f32,
    growth: f32,
    drag: f32,
    stretch: f32,
    gravity: f32,
    uv: [f32; 4],
    ramp: Option<[f32; 12]>,
    ramp_pos: f32,
    ramp_rate: f32,
}

#[derive(Resource)]
pub struct FxVm {
    /// sounds started by scripts this frame (sent by `run`)
    sounds: Vec<crate::audio::PlaySound>,
    camera_paths: Vec<String>,
    instances: Vec<Instance>,
    objects: std::collections::HashMap<Entity, FxObject>,
    pools: Vec<Vec<Particle>>,
    pool_meshes: Vec<Handle<Mesh>>,
    pub rng: CrtRand,
    unported: HashSet<String>,
    pub stats: FxVmStats,
    /// Camera shake (game units, decays x -0.75 per 1/60 s: the original's per-frame decay at 60 fps).
    pub shake: (f32, f32),
    streamers: Vec<Streamer>,
    /// decals made by scripts this frame (op 0x6a, sent by `run`)
    decals: Vec<crate::decals::SpawnDecal>,
    /// alpha-blended and additive streamer meshes
    streamer_meshes: [Handle<Mesh>; 2],
}

#[derive(Default, Clone, Copy)]
pub struct FxVmStats {
    pub instances: usize,
    pub objects: usize,
    pub particles: usize,
    pub dropped: usize,
    pub ms: f32,
}

pub struct FxVmPlugin;

impl Plugin for FxVmPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "particle.wesl");
        bevy::asset::embedded_asset!(app, "fxlayer.wesl");
        app.add_plugins(MaterialPlugin::<FxLayerMaterial>::default());
        app.add_plugins(MaterialPlugin::<ParticleMaterial>::default());
        app.add_systems(Startup, setup)
            .add_systems(Update, (start, run, objects, particles).chain().after(crate::fx::FxSet))
            .add_systems(PostUpdate, (follow, draw_particles, camera_shake).before(TransformSystems::Propagate))
            .add_systems(PostUpdate, streamers.after(TransformSystems::Propagate));
    }
}

fn setup(mut commands: Commands, assets: Res<AssetServer>, mut meshes: ResMut<Assets<Mesh>>, mut materials: ResMut<Assets<ParticleMaterial>>, mut std_mats: ResMut<Assets<StandardMaterial>>) {
    // the streamers' two meshes (untextured: texture 0x55 not ported), vertex colours, unlit, both sides
    let streamer_meshes = [false, true].map(|additive| {
        let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, vec![[0.0f32; 3]; 3]);
        mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, vec![[0.0f32; 4]; 3]);
        mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, vec![[0.0f32, 1.0, 0.0]; 3]);
        mesh.insert_indices(Indices::U32(vec![0, 1, 2]));
        let h = meshes.add(mesh);
        let mat = std_mats.add(StandardMaterial {
            base_color: Color::WHITE,
            unlit: true,
            double_sided: true,
            cull_mode: None,
            alpha_mode: if additive { AlphaMode::Add } else { AlphaMode::Blend },
            fog_enabled: false,
            ..default()
        });
        commands.spawn((Mesh3d(h.clone()), MeshMaterial3d(mat), Transform::IDENTITY, bevy::camera::visibility::NoFrustumCulling, bevy::light::NotShadowCaster, Name::new("fx_streamers")));
        h
    });
    let mut pool_meshes = Vec::new();
    for (_, tex, additive) in POOLS {
        let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, Vec::<[f32; 3]>::new());
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, Vec::<[f32; 2]>::new());
        mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, Vec::<[f32; 4]>::new());
        mesh.insert_indices(Indices::U32(Vec::new()));
        let h = meshes.add(mesh);
        // Pool 0 draws additive (state 0x100), pools 1 and 2 alpha-blended (0x310): docs/subsystems/fx-vm.md 4
        let mat = materials.add(ParticleMaterial { bank: assets.load(format!("textures/{tex}.dds")), additive, params: Vec4::new(if additive { 1.0 } else { 2.2 }, 0.0, 0.0, 0.0) });
        commands.spawn((Mesh3d(h.clone()), MeshMaterial3d(mat), Transform::IDENTITY, bevy::camera::visibility::NoFrustumCulling, bevy::light::NotShadowCaster, Name::new("fx_pool")));
        pool_meshes.push(h);
    }
    commands.insert_resource(FxVm {
        sounds: Vec::new(),
        camera_paths: Vec::new(),
        instances: Vec::new(),
        objects: Default::default(),
        pools: POOLS.iter().map(|(n, _, _)| Vec::with_capacity(n * POOL_SCALE / 4)).collect(),
        pool_meshes,
        rng: CrtRand(1),
        unported: HashSet::new(),
        stats: FxVmStats::default(),
        shake: (0.0, 0.0),
        streamers: Vec::new(),
        decals: Vec::new(),
        streamer_meshes,
    });
}

fn engine_to_game() -> Quat {
    game_to_engine().inverse()
}

fn start(mut vm: ResMut<FxVm>, data: Res<GameData>, mut events: MessageReader<StartFx>) {
    for ev in events.read() {
        // a material variant the bank lacks falls back to the base fx (fx_imp_arrow_grunt -> fx_imp_arrow)
        let name = ev.name.to_lowercase();
        // debug: FXSKIP=name1,name2 does not start those scripts
        if std::env::var("FXSKIP").is_ok_and(|v| v.split(',').any(|n| n == name)) {
            continue;
        }
        if std::env::var("FXLOG").is_ok() {
            info!("FXLOG start {name} at {:?}", ev.at);
        }
        let base = name.rsplit_once('_').map(|(b, _)| b.to_owned()).unwrap_or_default();
        let Some(script) = data.fx.get(&name).or_else(|| data.fx.get(&base)) else {
            warn!("fx {} not in the level's fx bank", ev.name);
            continue;
        };
        // engine placement -> game space
        let place = Mat4::from_quat(engine_to_game()) * Mat4::from_rotation_translation(ev.rotation, ev.at);
        vm.instances.push(Instance { script: script.clone(), ip: 0, paused: 0.0, waiting: false, place, parent: ev.follow, follow: ev.follow.is_some(), follow_pos_only: ev.position_only });
    }
}

fn f(v: &Value) -> f32 {
    v.as_f64().unwrap_or(0.0) as f32
}

fn names(v: &Value) -> Vec<String> {
    v.as_array().map(|a| a.iter().filter_map(|s| s.as_str().map(str::to_owned)).collect()).unwrap_or_default()
}

/// Everything a script run needs from the world.
struct Ctx<'a, 'cw, 'cs, 'w, 's> {
    commands: &'a mut Commands<'cw, 'cs>,
    data: &'a GameData,
    lib: &'a mut Library,
    assets: &'a AssetServer,
    meshes: &'a mut Assets<Mesh>,
    materials: &'a mut Assets<StandardMaterial>,
    layer_materials: &'a mut Assets<FxLayerMaterial>,
    physics: &'a mut Physics,
    _q: std::marker::PhantomData<(&'w (), &'s ())>,
}

/// Fx_CreateObject 0x004234d0: the template's mesh as an fx object at the instance's placement.
fn create_object(ctx: &mut Ctx<'_, '_, '_, '_, '_>, inst: &Instance, template: &str, dt: f32) -> Option<(Entity, FxObject)> {
    let t = ctx.data.templates.get(&template.to_lowercase())?;
    let place = inst.place;
    let world = Mat4::from_quat(game_to_engine()) * place;
    let mut e = ctx.commands.spawn((Transform::from_matrix(world), Visibility::default(), Name::new(template.to_owned()), FxEntity));
    let mut mats = Vec::new();
    let mut layers = Vec::new();
    if let Some(model) = t.model.as_ref().and_then(|m| ctx.lib.load_model(m, false, ctx.assets, ctx.meshes, ctx.materials)) {
        let parts = model.parts.clone();
        e.with_children(|c| {
            for (mesh, mat) in parts {
                // own material per part: fades and texture animation change it (fx meshes draw unlit, additive
                // when textured: guess from the material flags, docs/subsystems/fx-vm.md 3)
                let base = ctx.materials.get(&mat).cloned().unwrap_or_default();
                let textured = base.base_color_texture.is_some();
                // a part with a second texture and a mask (the snow domes): the layered set-up
                let layered = ctx.lib.game_defs.get(&mat.id()).filter(|(d, t, _)| t.is_some() && d.texture2.is_some() && d.mask.is_some() && d.normal_map.is_none()).cloned();
                if let Some((def, Some(tex), _)) = layered {
                    // repeat addressing: the domes' uvs run 0..12 (the default sampler clamps: no flakes past the first tile)
                    let load = |n: &str| crate::gamematerial::load_tex(ctx.assets, n, true);
                    let lm = ctx.layer_materials.add(FxLayerMaterial {
                        t0: load(&tex),
                        t1: load(def.texture2.as_deref().unwrap_or(&tex)),
                        // the mask is data (linear): an uncompressed BGRA DDS fails to load as sRGB (black fallback)
                        t2: crate::gamematerial::load_tex(ctx.assets, def.mask.as_deref().unwrap_or(&tex), false),
                        params: FxLayerParams {
                            offsets: Vec4::ZERO,
                            colour: Vec4::new(def.emissive[0], def.emissive[1], def.emissive[2], 1.0),
                            flags: Vec4::new(
                                premultiplied(&ctx.lib.dir, &tex) as u8 as f32,
                                premultiplied(&ctx.lib.dir, def.texture2.as_deref().unwrap_or(&tex)) as u8 as f32,
                                premultiplied(&ctx.lib.dir, def.mask.as_deref().unwrap_or(&tex)) as u8 as f32,
                                0.0,
                            ),
                        },
                    });
                    layers.push(lm.clone());
                    c.spawn((Mesh3d(mesh), MeshMaterial3d(lm), bevy::light::NotShadowCaster));
                    continue;
                }
                // textured fx draw texture x the material's diffuse (stage MODULATE(texture, diffuse), the fx objects'
                // material colour 0.588 on fxo_flash): measured on the sticky blast's flash, the original adds ~0.6 of
                // what texture x white added (work/shots/stickY vs work/remaster/play/stickX)
                let def = ctx.lib.game_defs.get(&mat.id()).map(|(d, _, _)| d.clone());
                // alpha-blended fx (lighting on, kind bit 1): the lit vertex colour, emissive + diffuse clamped (fx_white:
                // white; the original's white fade peaks at 252 / 255: intro capture); additive fx keep the diffuse
                let tint = def.as_ref().map(|d| {
                    if d.kind & 0x10 == 0 {
                        Color::srgb((d.emissive[0] + d.diffuse[0]).min(1.0), (d.emissive[1] + d.diffuse[1]).min(1.0), (d.emissive[2] + d.diffuse[2]).min(1.0))
                    } else {
                        Color::srgb(d.diffuse[0], d.diffuse[1], d.diffuse[2])
                    }
                });
                // the blend from the material kind (the original's material log: kind bit 0x10 -> blend 1, SRCALPHA /
                // ONE, additive: fx_glow_red, fx_trail_bullet, the snow; without it blend 3, SRCALPHA / INVSRCALPHA:
                // fx_white (the intro's white fade), fx_true_story (its caption))
                let additive = def.as_ref().map_or(textured, |d| d.kind & 0x10 != 0);
                let m = ctx.materials.add(StandardMaterial {
                    unlit: true,
                    alpha_mode: if additive { AlphaMode::Add } else if textured { AlphaMode::Blend } else { base.alpha_mode },
                    base_color: if textured { tint.unwrap_or(base.base_color) } else { base.base_color },
                    // back faces culled as the original's renderer does (D3D default): fx_true_story's quad is two
                    // faces, the back one mirrored; drawn double-sided the caption showed both
                    double_sided: false,
                    cull_mode: Some(bevy::render::render_resource::Face::Back),
                    ..base
                });
                mats.push(m.clone());
                c.spawn((Mesh3d(mesh), MeshMaterial3d(m), bevy::light::NotShadowCaster));
            }
        });
    }
    let _ = dt;
    let id = e.id();
    Some((id, FxObject {
        place,
        age: 0.0,
        lifespan: None,
        fade_in: None,
        fade_out: None,
        scale: None,
        kite: false,
        emitters: Vec::new(),
        texanim: None,
        shake: None,
        follow: if inst.follow { inst.parent } else { None },
        materials: mats,
        alpha: 1.0,
        body: None,
        uv_scroll: None,
        follow_pos_only: inst.follow_pos_only,
        layer_mats: layers,
        uv_scroll2: None,
    }))
}

/// Run one instance from its instruction pointer until end (true), pause or wait (false).
fn run_script(vm: &mut FxVm, ctx: &mut Ctx<'_, '_, '_, '_, '_>, idx: usize, dt: f32, spawned: &mut Vec<Instance>) -> bool {
    let mut cur: Option<Entity> = None;
    let mut stack: Vec<Entity> = Vec::new();
    let mut list1: Option<Vec<String>> = None;
    let mut list2: Option<Vec<String>> = None;
    loop {
        let inst = &vm.instances[idx];
        let Some(ins) = inst.script.get(inst.ip).cloned() else { return true };
        vm.instances[idx].ip += 1;
        let a = ins.as_array().cloned().unwrap_or_default();
        let op = a.first().and_then(|v| v.as_str()).unwrap_or("0x04").to_owned();
        let arg = |i: usize| a.get(i).cloned().unwrap_or(Value::Null);
        let top = stack.last().copied();
        macro_rules! obj {
            () => {
                if cur.is_some() { top.and_then(|t| vm.objects.get_mut(&t)) } else { None }
            };
        }
        macro_rules! emitter {
            () => {
                obj!().and_then(|o| if o.emitters.is_empty() { None } else { Some(o) })
            };
        }
        match op.as_str() {
            "0x04" => return true,
            "0x05" | "0x13" => {
                cur = create_object(ctx, &vm.instances[idx], arg(1).as_str().unwrap_or(""), dt).map(|(e, o)| {
                    vm.objects.insert(e, o);
                    e
                })
            }
            "0x06" => {
                if let Some(c) = cur {
                    stack.push(c);
                }
            }
            "0x07" => {
                if cur.is_some() {
                    stack.pop();
                }
                cur = None;
            }
            "0x08" => {
                let (lo, hi) = (f(&arg(1)), f(&arg(2)));
                let v = vm.rng.r(lo, hi.max(lo));
                if let Some(o) = obj!() {
                    o.lifespan = Some(v);
                    o.age = 0.0;
                }
            }
            "0x09" => {
                let s = vm.rng.r(f(&arg(1)), f(&arg(2)));
                let e = vm.rng.r(f(&arg(3)), f(&arg(4)));
                if let Some(o) = obj!() {
                    o.fade_in = Some((s, 1.0 / (e - s).max(1e-4), true));
                    o.alpha = 0.0;
                }
            }
            "0x0a" => {
                let v = vm.rng.r(f(&arg(1)), f(&arg(2)));
                if let Some(o) = obj!() {
                    o.fade_out = Some(v);
                }
            }
            "0x0b" => {
                // uniform scale (from weather.md: 0x0b / 0x0c "set scale linear"): a constant scale
                let v = vm.rng.r(f(&arg(1)), f(&arg(2)));
                if let Some(o) = obj!() {
                    o.scale = Some((v, v, 0));
                }
            }
            "0x0c" | "0x0d" | "0x0e" => {
                let from = vm.rng.r(f(&arg(1)), f(&arg(2)));
                let to = vm.rng.r(f(&arg(3)), f(&arg(4)));
                let mode = match op.as_str() { "0x0c" => 0, "0x0d" => 1, _ => 2 };
                if let Some(o) = obj!() {
                    o.scale = Some((from, to, mode));
                }
            }
            "0x0f" | "0x2f" => {
                let name = arg(1).as_str().unwrap_or("").to_lowercase();
                list1 = None;
                list2 = None;
                if let (Some(_), Some(t)) = (cur, top) {
                    if let Some(script) = ctx.data.fx.get(&name) {
                        let place = vm.objects.get(&t).map(|o| o.place).unwrap_or(vm.instances[idx].place);
                        let child = Instance { script: script.clone(), ip: 0, paused: 0.0, waiting: false, place, parent: Some(t), follow: op == "0x2f", follow_pos_only: false };
                        spawned.push(child);
                    }
                }
            }
            "0x104" => {
                let l = names(&arg(1));
                if list1.is_none() {
                    list1 = Some(l);
                } else {
                    list2 = Some(l);
                }
            }
            "0x11" => {
                let g = vm.rng.r(f(&arg(1)), f(&arg(2)));
                if let Some(o) = obj!() {
                    make_dynamic(ctx.physics, ctx.commands, o, top.unwrap(), g);
                }
            }
            "0x12" => {
                let t = vm.rng.r(f(&arg(1)), f(&arg(2)));
                if stack.is_empty() {
                    vm.instances[idx].paused = t;
                }
                return false;
            }
            "0x15" => {
                if stack.is_empty() {
                    vm.instances[idx].waiting = true;
                }
                return false;
            }
            "0x16" => {
                if let Some(t) = top.filter(|_| cur.is_some()) {
                    ctx.commands.entity(t).insert(Visibility::Hidden);
                    // emitters and physics still run; the children inherit Hidden
                }
            }
            "0x1c" => {
                let v = vm.rng.r(f(&arg(1)), f(&arg(2)));
                if let Some(o) = obj!() {
                    o.place.w_axis.z += v;
                }
            }
            "0x20" | "0x21" | "0x22" | "0x23" | "0x24" | "0x25" | "0x26" | "0x48" => {
                let (lo, hi) = (f(&arg(1)), f(&arg(2)));
                if let Some(o) = emitter!() {
                    let e = o.emitters.last_mut().unwrap();
                    match op.as_str() {
                        "0x20" => e.stretch = (lo, hi),
                        "0x21" => e.size = (lo, hi),
                        "0x22" => e.growth = (lo, hi),
                        "0x23" => e.drag = (lo.max(1e-6), hi),
                        "0x24" => e.speed = (lo, hi),
                        "0x25" => e.life = (lo, hi),
                        "0x26" => e.cone = (lo, hi),
                        _ => e.gravity = (lo, hi),
                    }
                }
            }
            "0x27" => {
                let pool = arg(1).as_i64().unwrap_or(0).clamp(0, 2) as usize;
                let interval = vm.rng.r(f(&arg(2)), f(&arg(3)));
                if let Some(o) = obj!() {
                    if o.emitters.len() < 10 {
                        // direction: -column 1 of the object's matrix
                        let dir = -o.place.y_axis.truncate().normalize_or(Vec3::Y);
                        o.emitters.push(Emitter::new(pool, interval, dir));
                    }
                }
            }
            "0x2b" => {
                let uv = [f(&arg(1)), f(&arg(2)), f(&arg(3)), f(&arg(4))];
                if let Some(o) = emitter!() {
                    o.emitters.last_mut().unwrap().uv = uv;
                }
            }
            "0x2e" => {
                if let Some(o) = obj!() {
                    o.kite = true;
                }
            }
            "0x38" | "0x39" | "0x3a" => {
                // a random name from the inline list (one rand), then the looping word; positional at the object
                let l = names(&arg(1));
                let pick = if l.is_empty() { None } else { l.get((vm.rng.rand() as usize * l.len()) >> 15).cloned() };
                let looping = arg(2).as_i64().unwrap_or(0) != 0;
                list1 = None;
                if let (Some(name), Some(t)) = (pick, top.filter(|_| cur.is_some())) {
                    let at = vm.objects.get(&t).map(|o| game_to_engine() * o.place.w_axis.truncate());
                    vm.sounds.push(crate::audio::PlaySound { name, at, follow: Some(t), looped: Some(looping), volume: 1.0 });
                }
            }
            "0x3b" => {
                if let Some(o) = obj!() {
                    o.shake = Some((f(&arg(1)), f(&arg(2)), f(&arg(3))));
                }
            }
            "0x46" | "0x47" => {
                let (x, y, z) = (f(&arg(1)).to_radians(), f(&arg(2)).to_radians(), f(&arg(3)).to_radians());
                if let Some(o) = emitter!() {
                    // the original's v * (Rx Ry Rz) with D3D row matrices is (Rz Ry Rx) v with column matrices (the doc's check:
                    // (0, -1, 0) by (-90, 0, 0) gives +z, particles go up)
                    let m = Mat3::from_rotation_z(z) * Mat3::from_rotation_y(y) * Mat3::from_rotation_x(x);
                    let e = o.emitters.last_mut().unwrap();
                    e.dir = (m * e.dir).normalize_or(Vec3::Z);
                    e.fixed_dir = op == "0x46";
                    e.rel = (op == "0x47").then_some(m);
                }
            }
            "0x49" => {
                let (in0, in1, out0) = (f(&arg(1)), f(&arg(2)), f(&arg(3)));
                if let Some(o) = emitter!() {
                    let e = o.emitters.last_mut().unwrap();
                    e.ramp = Some(build_ramp(in0, in1, out0, e.life));
                }
            }
            "0x63" => {
                let (layer, frames, rate, looping) = (arg(1).as_i64().unwrap_or(1) as u32, arg(2).as_i64().unwrap_or(1) as u32, f(&arg(3)), arg(4).as_i64().unwrap_or(0) != 0);
                if let Some(o) = obj!() {
                    let base = o.materials.first().and_then(|m| ctx.materials.get(m)).and_then(|m| m.base_color_texture.as_ref()).and_then(|h| h.path()).map(|p| p.path().file_stem().unwrap().to_string_lossy().into_owned()).unwrap_or_default();
                    let _ = layer;
                    o.texanim = Some((rate, frames.max(1), looping, 0.0, 0, base));
                }
            }
            "0x64" => {
                let v = Vec3::new(vm.rng.r(f(&arg(1)), f(&arg(2))), vm.rng.r(f(&arg(3)), f(&arg(4))), vm.rng.r(f(&arg(5)), f(&arg(6))));
                if let Some(o) = obj!() {
                    let id = make_dynamic(ctx.physics, ctx.commands, o, top.unwrap(), 1.0);
                    if let Some(id) = id {
                        // Havok impulse in game units: velocity change = impulse / mass (mass unknown, guess below)
                        let k = ctx.physics.meters_per_unit;
                        if let Ok(mut b) = ctx.physics.world.body_mut(id) {
                            b.activate();
                            let _ = b.add_impulse(jv(game_to_engine() * v * k * IMPULSE_SCALE));
                        }
                    }
                }
            }
            // streamers (0x0041f500 ..): 0x4d on the object, 0x4e on a bone of list 1 (the object's position is used)
            "0x4d" | "0x4e" => {
                if let (Some(_), Some(t)) = (cur, top) {
                    if vm.streamers.iter().filter(|s| s.owner == t).count() < 8 {
                        vm.streamers.push(Streamer::new(t));
                    }
                }
                if op == "0x4e" {
                    list1 = None;
                }
            }
            "0x4f" | "0x50" | "0x51" | "0x52" | "0x53" | "0x54" | "0x57" => {
                if let (Some(_), Some(t)) = (cur, top) {
                    if let Some(st) = vm.streamers.iter_mut().rev().find(|s| s.owner == t) {
                        let q = |x: f32| (x * 255.0).trunc() / 255.0;
                        match op.as_str() {
                            "0x4f" => st.width = curve_keys(&arg(1)),
                            "0x54" => st.life = f(&arg(1)).max(1e-3),
                            "0x57" => st.additive = f(&arg(1)) != 0.0,
                            _ => {
                                let ch = match op.as_str() { "0x50" => 3, "0x51" => 0, "0x52" => 1, _ => 2 };
                                let k = curve_keys(&arg(1));
                                for (i, v) in k.iter().enumerate() {
                                    st.colour[i][ch] = q(*v);
                                }
                            }
                        }
                    }
                }
            }
            "0x73" => {
                // Camera_StartPath: not guarded by the created object (weather.md)
                if let Some(name) = arg(1).as_str() {
                    vm.camera_paths.push(name.to_owned());
                }
            }
            // 0x6a: a decal (Decals_Spawn 0x004165c0) in the box of +-size around the object (decals.rs)
            "0x6a" => {
                if let (Some(_), Some(t)) = (cur, top) {
                    if let Some(o) = vm.objects.get(&t) {
                        let place = o.place;
                        if let Some(tex) = arg(2).as_str() {
                            vm.decals.push(crate::decals::SpawnDecal { place, size: f(&arg(1)), texture: tex.to_owned() });
                        }
                    }
                }
            }
            "0x40" => {
                // Fx_SetUvScroll 0x00420670: layer, angle (deg), speed, duration, ramp in, ramp out; direction from the
                // sine table (cos, sin of the angle: guess at the table's phase). Layer 1 only: the fx meshes have one
                // texture stage here
                let (layer, ang, speed, dur, rin, rout) = (arg(1).as_i64().unwrap_or(1), f(&arg(2)).to_radians(), f(&arg(3)), f(&arg(4)), f(&arg(5)), f(&arg(6)));
                if let Some(o) = obj!() {
                    let sc = Some((Vec2::new(ang.cos(), ang.sin()), speed, dur, rin, rout, Vec2::ZERO));
                    match layer {
                        1 => o.uv_scroll = sc,
                        2 => o.uv_scroll2 = sc,
                        _ => {}
                    }
                }
            }
            other => {
                vm.unported.insert(other.to_owned());
            }
        }
    }
}

/// Game impulse units to Jolt (kg m/s): unknown in the original (Havok mass from density x collision volume);
/// chosen so the flying keg of fx_explosion_powder_keg (impulse 200000 on a 30 kg body) leaves at about 20 m/s
/// (100 game units/s). Guess, to be measured against the original.
pub const IMPULSE_SCALE: f32 = 1.0 / 66.0;

/// Object_setPhysicsEnabled(obj, 0): the fx object gets a dynamic body (a box; the fx meshes' collision
/// volume is not exported yet).
fn make_dynamic(physics: &mut Physics, commands: &mut Commands, o: &mut FxObject, e: Entity, gravity: f32) -> Option<BodyId> {
    if let Some(b) = o.body {
        return Some(b);
    }
    let k = physics.meters_per_unit;
    let world = Mat4::from_quat(game_to_engine()) * o.place;
    let (_, rot, pos) = world.to_scale_rotation_translation();
    let shape = Shape::new_box(jv(Vec3::splat(1.5 * k))).ok()?;
    let s = BodySettings::new_dynamic().mass(30.0).friction(0.2).restitution(0.2).gravity_factor(gravity.max(0.0));
    let id = physics.add_dynamic(e, &shape, pos, rot, Vec3::ZERO, s)?;
    commands.entity(e).insert(Body::rigid(id));
    o.body = Some(id);
    Some(id)
}

/// The 0x49 alpha ramp (Fx_SetParticleFadeInOut 0x0041eb70): 12 entries, index 10 at birth, 0 at death.
fn build_ramp(in0: f32, in1: f32, out0: f32, life: (f32, f32)) -> [f32; 12] {
    let t_life = (life.0 + life.1) * 0.5;
    let step = 0.1 * t_life;
    let mut ramp = [0.0f32; 12];
    ramp[11] = 0.0;
    ramp[10] = if in1 == 0.0 { 1.0 } else { 0.0 };
    let (mut n_in, mut n_out) = (0.0f32, 0.0f32);
    for i in 0..10 {
        let t = i as f32 * step;
        let a = if t < in0 {
            0.0
        } else if t < in1 {
            n_in += 1.0;
            (n_in / ((in1 - in0) / step)).min(1.0)
        } else if t < out0 {
            1.0
        } else if t < life.0 {
            n_out += 1.0;
            (1.0 - n_out / ((life.0 - out0) / step)).max(0.0)
        } else {
            0.0
        };
        ramp[9 - i] = a;
    }
    ramp
}

#[allow(clippy::too_many_arguments)]
fn run(
    mut commands: Commands,
    time: Res<Time>,
    mut vm: ResMut<FxVm>,
    data: Res<GameData>,
    mut lib: ResMut<Library>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut layer_materials: ResMut<Assets<FxLayerMaterial>>,
    mut physics: NonSendMut<Physics>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut paths: MessageWriter<crate::campath::StartCameraPath>,
    mut decals: MessageWriter<crate::decals::SpawnDecal>,
) {
    let t0 = std::time::Instant::now();
    let dt = time.delta_secs();
    let vm = &mut *vm;
    let mut ctx = Ctx { commands: &mut commands, data: &data, lib: &mut lib, assets: &assets, meshes: &mut meshes, materials: &mut materials, layer_materials: &mut layer_materials, physics: &mut physics, _q: std::marker::PhantomData };
    let mut i = 0;
    while i < vm.instances.len() {
        let inst = &mut vm.instances[i];
        if inst.waiting {
            i += 1;
            continue;
        }
        if inst.paused > 0.0 {
            inst.paused -= dt;
            if inst.paused > 0.0 {
                i += 1;
                continue;
            }
            inst.paused = 0.0;
        }
        let mut spawned = Vec::new();
        let done = run_script(vm, &mut ctx, i, dt, &mut spawned);
        // 0x0f children run at once (nested Fx_Step), 0x2f children from the next pass: both appended here
        vm.instances.extend(spawned);
        if done {
            vm.instances.swap_remove(i);
        } else {
            i += 1;
        }
    }
    for snd in vm.sounds.drain(..) {
        sounds.write(snd);
    }
    for p in vm.camera_paths.drain(..) {
        paths.write(crate::campath::StartCameraPath(p));
    }
    for d in vm.decals.drain(..) {
        decals.write(d);
    }
    vm.stats.instances = vm.instances.len();
    vm.stats.ms = t0.elapsed().as_secs_f32() * 1000.0;
    if !vm.unported.is_empty() {
        let list: Vec<_> = vm.unported.drain().collect();
        for u in list {
            info!("fx vm: not ported yet: {u}");
        }
    }
}

/// Fx object update (render_r1.cpp:1060ff): age, lifespan, fades, scale, kite, texture animation, shake,
/// and the emitters' particle spawning along the object's movement.
#[allow(clippy::too_many_arguments)]
fn objects(
    mut commands: Commands,
    time: Res<Time>,
    mut vm: ResMut<FxVm>,
    assets: Res<AssetServer>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut layer_materials: ResMut<Assets<FxLayerMaterial>>,
    mut physics: NonSendMut<Physics>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    mut q: Query<&mut Transform, With<FxEntity>>,
) {
    let dt = time.delta_secs();
    let cam_t = cam.single().ok().map(|c| c.compute_transform());
    let axes = game_to_engine();
    let mut objs = std::mem::take(&mut vm.objects);
    let mut gone = Vec::new();
    for (&e, o) in objs.iter_mut() {
        let body = o.body;
        let Ok(mut t) = q.get_mut(e) else { continue }; // spawned this frame: starts next frame
        // physics-driven objects: read the placement back from the body's pose
        if body.is_some() {
            o.place = Mat4::from_quat(axes.inverse()) * Mat4::from_rotation_translation(t.rotation, t.translation);
        }
        // the original ran at 60 fps: an object's first update steps at least 1/60 s, so scripts with lifespans
        // under a frame (Roman's snow footsteps: 0.01) end before their emitters run, as there (render_r1.cpp:1071)
        o.age += if o.age == 0.0 { dt.max(1.0 / 60.0) } else { dt };
        if let Some(life) = o.lifespan {
            if o.age >= life {
                if let Some(b) = body {
                    physics.remove(b);
                }
                commands.entity(e).despawn();
                gone.push(e);
                continue;
            }
        }
        // fades (vt+0x100: object alpha)
        let mut alpha = o.alpha;
        if let Some((mut delay, rate, waiting)) = o.fade_in {
            if waiting {
                delay -= dt;
                if delay <= 0.0 {
                    o.fade_in = Some((-delay, rate, false));
                } else {
                    o.fade_in = Some((delay, rate, true));
                }
            } else {
                let acc = delay + dt;
                alpha = (acc * rate).min(1.0);
                o.fade_in = if alpha >= 1.0 { None } else { Some((acc, rate, false)) };
            }
        } else if let (Some(start), Some(life)) = (o.fade_out, o.lifespan) {
            alpha = if o.age > start { (1.0 - (o.age - start) / (life - start).max(1e-4)).max(0.0) } else { 1.0 };
        }
        if (alpha - o.alpha).abs() > 1e-3 {
            o.alpha = alpha;
            for m in &o.materials {
                if let Some(mut m) = materials.get_mut(m) {
                    // the original blends in gamma space, Bevy in linear: alpha^2.2 gives the same look for an
                    // alpha-blended fade (the intro's white: a straight line from 252 to the scene over 0.5..3 s)
                    let a = if m.alpha_mode == AlphaMode::Blend { alpha.powf(2.2) } else { alpha };
                    m.base_color.set_alpha(a);
                    if m.alpha_mode == AlphaMode::Opaque {
                        m.alpha_mode = AlphaMode::Blend;
                    }
                }
            }
        }
        // uv scroll (TexAnim_Step 0x00419d30): offset += speed * dir * fade * dt
        if let Some((dir, speed, dur, rin, rout, mut off)) = o.uv_scroll {
            let fade = if o.age > dur { 0.0 } else if rin > 0.0 && o.age < rin { o.age / rin } else if rout > 0.0 && o.age > dur - rout { 1.0 - (o.age - (dur - rout)) / (dur - rout).max(1e-3) } else { 1.0 };
            off += dir * speed * fade * dt;
            off = Vec2::new(off.x.rem_euclid(1.0), off.y.rem_euclid(1.0));
            for m in &o.materials {
                if let Some(mut m) = materials.get_mut(m) {
                    m.uv_transform = bevy::math::Affine2::from_translation(off);
                }
            }
            o.uv_scroll = Some((dir, speed, dur, rin, rout, off));
        }
        if let Some((dir, speed, dur, rin, rout, mut off)) = o.uv_scroll2 {
            let fade = if o.age > dur { 0.0 } else if rin > 0.0 && o.age < rin { o.age / rin } else if rout > 0.0 && o.age > dur - rout { 1.0 - (o.age - (dur - rout)) / (dur - rout).max(1e-3) } else { 1.0 };
            off += dir * speed * fade * dt;
            off = Vec2::new(off.x.rem_euclid(1.0), off.y.rem_euclid(1.0));
            o.uv_scroll2 = Some((dir, speed, dur, rin, rout, off));
        }
        if !o.layer_mats.is_empty() {
            let o1 = o.uv_scroll.map(|u| u.5).unwrap_or(Vec2::ZERO);
            let o2 = o.uv_scroll2.map(|u| u.5).unwrap_or(Vec2::ZERO);
            for m in &o.layer_mats {
                if let Some(mut m) = layer_materials.get_mut(m) {
                    m.params.offsets = Vec4::new(o1.x, o1.y, o2.x, o2.y);
                    m.params.colour.w = alpha;
                }
            }
        }
        // texture animation: "<base>_<frame>"
        if let Some((rate, frames, looping, mut acc, mut frame, base)) = o.texanim.clone() {
            acc += dt;
            let step = (acc * rate) as u32;
            if step > 0 {
                acc -= step as f32 / rate;
                frame = if looping { (frame + step) % frames } else { (frame + step).min(frames - 1) };
                let name = if frame == 0 { base.clone() } else { format!("{base}_{frame}") };
                let h: Handle<Image> = assets.load(format!("textures/{name}.dds"));
                for m in &o.materials {
                    if let Some(mut m) = materials.get_mut(m) {
                        m.base_color_texture = Some(h.clone());
                    }
                }
            }
            o.texanim = Some((rate, frames, looping, acc, frame, base));
        }
        // shake: one-shot when the camera is within the radius
        if let Some((time_s, radius, amp)) = o.shake.take() {
            if let Some(c) = cam_t {
                let pos = axes * o.place.w_axis.truncate();
                if c.translation.distance(pos) < radius {
                    vm.shake = (amp, time_s);
                }
            }
        }
        // transform: placement, kite (camera-facing), scale animation
        let s = match (o.scale, o.lifespan) {
            (Some((from, to, mode)), Some(life)) => {
                let u = (o.age / life).clamp(0.0, 1.0);
                match mode {
                    0 => from + (to - from) * u,
                    1 => from + ((to - from).max(0.0).sqrt() * u).powi(2),
                    _ => from + (to - from) * u * (2.0 - u),
                }
            }
            (Some((from, _, _)), None) => from,
            _ => 1.0,
        };
        if body.is_none() {
            let world = Mat4::from_quat(axes) * o.place;
            let (_, mut rot, pos) = world.to_scale_rotation_translation();
            if o.kite {
                if let Some(c) = cam_t {
                    // the card's mesh faces -Y in game space: turn it to the camera
                    rot = c.rotation * axes;
                }
            }
            *t = Transform { translation: pos, rotation: rot, scale: Vec3::splat(s) };
        } else {
            t.scale = Vec3::splat(s);
        }
        // emitters
        let pos = o.place.w_axis.truncate();
        let obj_dir = -o.place.y_axis.truncate().normalize_or(Vec3::Y);
        let mut emitters = std::mem::take(&mut o.emitters);
        for em in &mut emitters {
            if !em.fixed_dir {
                // 0x47 (flag 0x400): the object's direction rotated by M each step (0x0041eef0, render_r1.cpp:870)
                em.dir = match em.rel {
                    Some(m) => (m * obj_dir).normalize_or(obj_dir),
                    None => obj_dir,
                };
            }
            let prev = em.prev_pos.unwrap_or(pos);
            em.timer -= dt;
            while em.timer < 0.0 {
                let frac = ((em.timer + dt) / dt.max(1e-6)).clamp(0.0, 1.0);
                let at = prev.lerp(pos, frac);
                let birth_t = em.timer;
                spawn_particle(&mut vm, em, at, birth_t);
                em.timer += em.interval;
            }
            em.prev_pos = Some(pos);
        }
        o.emitters = emitters;
    }
    for e in gone {
        objs.remove(&e);
    }
    vm.stats.objects = objs.len();
    vm.objects = objs;
}

fn spawn_particle(vm: &mut FxVm, e: &Emitter, at: Vec3, birth_t: f32) {
    let cap = POOLS[e.pool].0 * POOL_SCALE;
    let rng = &mut vm.rng;
    let mut life = rng.r(e.life.0, e.life.1);
    let cone = rng.r(e.cone.0, e.cone.1).to_radians();
    let tilt = rng.r(0.0, cone);
    let spin = rng.r(0.0, std::f32::consts::TAU);
    let speed = rng.r(e.speed.0, e.speed.1);
    let axis = e.dir.normalize_or(Vec3::Z);
    let perp = axis.any_orthonormal_vector();
    let dir = Quat::from_axis_angle(axis, spin) * (Quat::from_axis_angle(perp, tilt) * axis);
    let size = rng.r(e.size.0, e.size.1);
    let growth = if e.growth.0 > e.growth.1 { -(size / life.max(1e-3)) } else { rng.r(e.growth.0, e.growth.1) };
    let drag = rng.r(e.drag.0, e.drag.1);
    let stretch = rng.r(e.stretch.0, e.stretch.1);
    let gravity = rng.r(e.gravity.0, e.gravity.1) * PARTICLE_G;
    let (ramp_pos, ramp_rate) = if e.ramp.is_some() { (10.0, 10.0 / life.max(1e-3)) } else { (1.0, 0.0) };
    if ramp_rate > 0.0 {
        life = life.min(ramp_pos / ramp_rate);
    }
    let pool = &mut vm.pools[e.pool];
    if pool.len() >= cap {
        vm.stats.dropped += 1;
        return;
    }
    pool.push(Particle { pos: at, prev: at, vel: dir * speed, life, birth_t, size, growth, drag, stretch, gravity, uv: e.uv, ramp: e.ramp, ramp_pos, ramp_rate });
}

/// Particles_Draw 0x0046d9f0's simulation: life, growth, ramp, drag, gravity, motion.
fn particles(time: Res<Time>, mut vm: ResMut<FxVm>) {
    let dt = time.delta_secs();
    let mut n = 0;
    for pool in &mut vm.pools {
        pool.retain_mut(|p| {
            let t = dt + p.birth_t;
            p.birth_t = 0.0;
            p.life -= t;
            if p.life <= 0.0 {
                return false;
            }
            p.size += t * p.growth;
            p.ramp_pos -= t * p.ramp_rate;
            let k = t.min(1.0 / p.drag.max(1e-6));
            p.vel -= p.vel * p.drag * k;
            p.vel.z += t * p.gravity;
            p.prev = p.pos;
            p.pos += p.vel * t;
            true
        });
        n += pool.len();
    }
    vm.stats.particles = n;
}

fn alpha_of(p: &Particle) -> f32 {
    match &p.ramp {
        None => 1.0,
        Some(r) => {
            let x = p.ramp_pos.clamp(0.0, 10.999);
            let j = x.floor() as usize;
            let fr = x - j as f32;
            r[j] + (r[(j + 1).min(11)] - r[j]) * fr
        }
    }
}

/// Camera-facing quads per pool (centre = midpoint of the last move, half-extent = size), stretched along the
/// per-frame motion with a faded trailing edge.
fn draw_particles(vm: Res<FxVm>, mut meshes: ResMut<Assets<Mesh>>, cam: Query<&GlobalTransform, With<Camera3d>>) {
    let Ok(c) = cam.single() else { return };
    let axes = game_to_engine();
    let right = *c.right();
    let up = *c.up();
    for (pool, handle) in vm.pools.iter().zip(&vm.pool_meshes) {
        let Some(mut mesh) = meshes.get_mut(handle) else { continue };
        let mut pos = Vec::with_capacity(pool.len() * 4);
        let mut uv = Vec::with_capacity(pool.len() * 4);
        let mut col = Vec::with_capacity(pool.len() * 4);
        let mut idx = Vec::with_capacity(pool.len() * 6);
        for p in pool {
            let centre = axes * ((p.pos + p.prev) * 0.5);
            let motion = axes * (p.pos - p.prev);
            let d = Vec3::new(motion.dot(right), motion.dot(up), 0.0);
            let dl = d.length();
            let a = alpha_of(p);
            let base = pos.len() as u32;
            let s = p.size.max(0.0);
            // corners (-r,-u) (-r,+u) (+r,+u) (+r,-u) with uv (u0,v0) (u0,v1) (u1,v1) (u1,v0)
            for (cx, cy, u, v) in [(-1.0f32, -1.0f32, p.uv[0], p.uv[2]), (-1.0, 1.0, p.uv[0], p.uv[3]), (1.0, 1.0, p.uv[1], p.uv[3]), (1.0, -1.0, p.uv[1], p.uv[2])] {
                let mut w = centre + right * cx * s + up * cy * s;
                let mut alpha = a;
                if p.stretch > 0.0 && dl > 1e-4 {
                    let cdir = cx * d.x / dl + cy * d.y / dl;
                    w += (right * d.x + up * d.y) * cdir * p.stretch;
                    if cdir < 0.0 {
                        alpha *= (1.0 + 0.7071 * cdir).max(0.0);
                    }
                }
                pos.push(w.to_array());
                uv.push([u, v]);
                col.push([1.0, 1.0, 1.0, alpha]);
            }
            // counter-clockwise as seen from the camera (the material culls back faces)
            idx.extend_from_slice(&[base, base + 2, base + 1, base, base + 3, base + 2]);
        }
        if pos.is_empty() {
            // an empty mesh is refused by the renderer: one zero-area triangle
            pos.extend([[0.0f32; 3]; 3]);
            uv.extend([[0.0f32; 2]; 3]);
            col.extend([[0.0f32; 4]; 3]);
            idx.extend([0, 1, 2]);
        }
        if std::env::var("PARTDBG").is_ok() {
            let cp = axes.inverse() * c.translation();
            let near: Vec<&Particle> = pool.iter().filter(|p| p.pos.distance(cp) < 6.0).collect();
            if !near.is_empty() { let d: f32 = near.iter().map(|p| p.pos.distance(cp)).sum::<f32>() / near.len() as f32; let v: f32 = near.iter().map(|p| p.vel.length()).sum::<f32>() / near.len() as f32; let sz: f32 = near.iter().map(|p| p.size).sum::<f32>() / near.len() as f32; info!("PARTDBG pool total {} near {} mean dist {:.2} speed {:.2} size {:.2}", pool.len(), near.len(), d, v, sz); }
        }
        if std::env::var("FX_DEBUG").is_ok() && pos.len() > 3 {
            let (mut lo, mut hi) = (Vec3::splat(f32::MAX), Vec3::splat(f32::MIN));
            for q in &pos {
                lo = lo.min(Vec3::from(*q));
                hi = hi.max(Vec3::from(*q));
            }
            info!("fx debug: pool quads {} bounds {lo} .. {hi}, camera {}", pos.len() / 4, c.translation());
        }
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, pos);
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, uv);
        mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, col);
        mesh.insert_indices(Indices::U32(idx));
    }
}

/// 0x2f children (and fx started on a moving entity) follow their parent; a gone parent ends them.
fn follow(mut commands: Commands, mut vm: ResMut<FxVm>, parents: Query<&GlobalTransform>, mut q: Query<&mut Transform, With<FxEntity>>) {
    let pairs: Vec<(Entity, Entity)> = vm.objects.iter().filter_map(|(&e, o)| o.follow.map(|p| (e, p))).collect();
    for (e, p) in pairs {
        let Ok(pt) = parents.get(p).map(|t| t.compute_transform()) else {
            // the followed object is gone (a projectile hit): its attached fx objects go with it
            if let Some(o) = vm.objects.remove(&e) {
                let _ = o;
            }
            commands.entity(e).despawn();
            continue;
        };
        let pos_only = vm.objects.get(&e).is_some_and(|o| o.follow_pos_only);
        if let Ok(mut t) = q.get_mut(e) {
            t.translation = pt.translation;
            if !pos_only {
                t.rotation = pt.rotation;
            }
        }
        if let Some(o) = vm.objects.get_mut(&e) {
            if pos_only {
                o.place.w_axis = (engine_to_game() * pt.translation).extend(1.0);
            } else {
                o.place = Mat4::from_quat(engine_to_game()) * Mat4::from_rotation_translation(pt.rotation, pt.translation);
            }
        }
    }
}

/// Camera shake: the game moves the camera target up and down, amplitude x -0.75 per frame (at 60 fps; the
/// remaster decays per 1/60 s so the shake lasts the same at any frame rate).
fn camera_shake(time: Res<Time>, mut vm: ResMut<FxVm>, mut cam: Query<&mut Transform, With<Camera3d>>, mut last: Local<f32>) {
    let Ok(mut t) = cam.single_mut() else { return };
    t.translation.y -= *last;
    *last = 0.0;
    let (amp, left) = vm.shake;
    if left <= 0.0 || amp.abs() < 0.01 {
        return;
    }
    let frames = time.delta_secs() * 60.0;
    let a = amp * (-0.75f32).powf(frames.round().max(1.0));
    vm.shake = (a, left - time.delta_secs());
    t.translation.y += a;
    *last = a;
}

/// Streamers: emit one point per frame at the owner while it lives (Streamer_AddPoint 0x004958a0: the width fades in
/// over the first quarter of the life, 3x^2 - 2x^3; directions by central difference), drop points older than the
/// life (0x00495300), stop emitting when the owner is gone (FxObject_Remove -> Streamer_Stop 0x00494f10) and free the
/// streamer when no points remain; then build the strips (Streamer_Draw 0x00495380: side = dir x camera axis (the
/// view's back axis, switching to its up axis while (dir . axis)^2 > 0.6), not normalised; vertices pos +- side * w,
/// w = width(t) * size, colour per channel = curve(t), t = age / life, oldest first).
fn streamers(
    time: Res<Time>,
    mut vm: ResMut<FxVm>,
    owners: Query<&GlobalTransform, With<FxEntity>>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    mut meshes: ResMut<Assets<Mesh>>,
) {
    let dt = time.delta_secs();
    // an fx object spawned this frame has no world transform yet (propagation runs before this): its placement
    let placed: std::collections::HashMap<Entity, Vec3> =
        vm.streamers.iter().filter_map(|s| vm.objects.get(&s.owner).map(|o| (s.owner, game_to_engine() * o.place.w_axis.truncate()))).collect();
    for st in vm.streamers.iter_mut() {
        st.time += dt;
        match owners.get(st.owner) {
            Ok(o) if st.emitting => {
                // (the engine's first point at the world origin drew a huge triangle from there: fight2E, 2026-10-14)
                let p = if o.translation() == Vec3::ZERO { placed.get(&st.owner).copied().unwrap_or(Vec3::ZERO) } else { o.translation() };
                let x = st.time / (st.life * 0.25);
                let size = if x < 1.0 { 3.0 * x * x - 2.0 * x * x * x } else { 1.0 };
                let n = st.points.len();
                let dir = st.points.back().map(|b| (p - b.0).normalize_or_zero()).unwrap_or(Vec3::ZERO);
                if n >= 2 {
                    let before = st.points[n - 2].0;
                    st.points[n - 1].1 = (p - before).normalize_or_zero();
                }
                st.points.push_back((p, dir, st.time, size));
                if st.points.len() > 63 {
                    st.points.pop_front();
                }
            }
            _ => st.emitting = false,
        }
        let cut = st.time - st.life;
        while st.points.front().is_some_and(|q| q.2 < cut) {
            st.points.pop_front();
        }
    }
    vm.streamers.retain(|s| s.emitting || !s.points.is_empty());
    let Ok(c) = cam.single() else { return };
    let (back, up) = (c.back().as_vec3(), c.up().as_vec3());
    for (k, handle) in vm.streamer_meshes.clone().iter().enumerate() {
        let additive = k == 1;
        let mut pos: Vec<[f32; 3]> = Vec::new();
        let mut col: Vec<[f32; 4]> = Vec::new();
        let mut idx: Vec<u32> = Vec::new();
        for st in vm.streamers.iter().filter(|s| s.additive == additive && s.points.len() >= 2) {
            let mut axis = back;
            let base = pos.len() as u32;
            for (i, (p, d, birth, size)) in st.points.iter().enumerate() {
                let t = ((st.time - birth) / st.life).clamp(0.0, 1.0);
                if d.dot(axis).powi(2) > 0.6 {
                    axis = if axis == back { up } else { back };
                }
                let side = d.cross(axis) * bezier(st.width, t) * size;
                let ch = |c: usize| bezier([st.colour[0][c], st.colour[1][c], st.colour[2][c], st.colour[3][c]], t);
                let colour = [ch(0), ch(1), ch(2), ch(3).clamp(0.0, 1.0)];
                pos.push((*p + side).to_array());
                pos.push((*p - side).to_array());
                // vertex colours are linear in Bevy: the original's are gamma-space bytes
                let lin = |v: f32| v.clamp(0.0, 1.0).powf(2.2);
                let cl = [lin(colour[0]), lin(colour[1]), lin(colour[2]), colour[3]];
                col.push(cl);
                col.push(cl);
                if i > 0 {
                    let a = base + (i as u32 - 1) * 2;
                    idx.extend_from_slice(&[a, a + 1, a + 2, a + 1, a + 3, a + 2]);
                }
            }
        }
        if idx.is_empty() {
            pos = vec![[0.0; 3]; 3];
            col = vec![[0.0; 4]; 3];
            idx = vec![0, 1, 2];
        }
        if let Some(mut m) = meshes.get_mut(handle) {
            let n = pos.len();
            m.insert_attribute(Mesh::ATTRIBUTE_POSITION, pos);
            m.insert_attribute(Mesh::ATTRIBUTE_NORMAL, vec![[0.0f32, 1.0, 0.0]; n]);
            m.insert_attribute(Mesh::ATTRIBUTE_COLOR, col);
            m.insert_indices(Indices::U32(idx));
        }
    }
}
