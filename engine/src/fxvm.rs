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
    instances: Vec<Instance>,
    objects: std::collections::HashMap<Entity, FxObject>,
    pools: Vec<Vec<Particle>>,
    pool_meshes: Vec<Handle<Mesh>>,
    pub rng: CrtRand,
    unported: HashSet<String>,
    pub stats: FxVmStats,
    /// Camera shake (game units, decays x -0.75 per 1/60 s: the original's per-frame decay at 60 fps).
    pub shake: (f32, f32),
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
        app.add_systems(Startup, setup)
            .add_systems(Update, (start, run, objects, particles).chain().after(crate::fx::FxSet))
            .add_systems(PostUpdate, (follow, draw_particles, camera_shake).before(TransformSystems::Propagate));
    }
}

fn setup(mut commands: Commands, assets: Res<AssetServer>, mut meshes: ResMut<Assets<Mesh>>, mut materials: ResMut<Assets<StandardMaterial>>) {
    let mut pool_meshes = Vec::new();
    for (_, tex, additive) in POOLS {
        let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, Vec::<[f32; 3]>::new());
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, Vec::<[f32; 2]>::new());
        mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, Vec::<[f32; 4]>::new());
        mesh.insert_indices(Indices::U32(Vec::new()));
        let h = meshes.add(mesh);
        // Pool 0 draws additive (state 0x100), pools 1 and 2 alpha-blended (0x310): docs/subsystems/fx-vm.md 4
        let mat = materials.add(StandardMaterial {
            base_color_texture: Some(assets.load(format!("textures/{tex}.dds"))),
            unlit: true,
            alpha_mode: if additive { AlphaMode::Add } else { AlphaMode::Blend },
            cull_mode: None,
            double_sided: true,
            ..default()
        });
        commands.spawn((Mesh3d(h.clone()), MeshMaterial3d(mat), Transform::IDENTITY, bevy::camera::visibility::NoFrustumCulling, bevy::light::NotShadowCaster));
        pool_meshes.push(h);
    }
    commands.insert_resource(FxVm {
        instances: Vec::new(),
        objects: Default::default(),
        pools: POOLS.iter().map(|(n, _, _)| Vec::with_capacity(n * POOL_SCALE / 4)).collect(),
        pool_meshes,
        rng: CrtRand(1),
        unported: HashSet::new(),
        stats: FxVmStats::default(),
        shake: (0.0, 0.0),
    });
}

fn engine_to_game() -> Quat {
    game_to_engine().inverse()
}

fn start(mut vm: ResMut<FxVm>, data: Res<GameData>, mut events: MessageReader<StartFx>) {
    for ev in events.read() {
        let Some(script) = data.fx.get(&ev.name.to_lowercase()) else {
            warn!("fx {} not in the level's fx bank", ev.name);
            continue;
        };
        // engine placement -> game space
        let place = Mat4::from_quat(engine_to_game()) * Mat4::from_rotation_translation(ev.rotation, ev.at);
        vm.instances.push(Instance { script: script.clone(), ip: 0, paused: 0.0, waiting: false, place, parent: None, follow: false });
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
    if let Some(model) = t.model.as_ref().and_then(|m| ctx.lib.load_model(m, false, ctx.assets, ctx.meshes, ctx.materials)) {
        let parts = model.parts.clone();
        e.with_children(|c| {
            for (mesh, mat) in parts {
                // own material per part: fades and texture animation change it (fx meshes draw unlit, additive
                // when textured: guess from the material flags, docs/subsystems/fx-vm.md 3)
                let base = ctx.materials.get(&mat).cloned().unwrap_or_default();
                let textured = base.base_color_texture.is_some();
                let m = ctx.materials.add(StandardMaterial {
                    unlit: true,
                    alpha_mode: if textured { AlphaMode::Add } else { base.alpha_mode },
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
            "0x05" => {
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
                        let child = Instance { script: script.clone(), ip: 0, paused: 0.0, waiting: false, place, parent: Some(t), follow: op == "0x2f" };
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
            "0x39" => {
                let l = names(&arg(1));
                let _pick = if l.is_empty() { 0 } else { (vm.rng.rand() as usize * l.len()) >> 15 };
                list1 = None;
                vm.unported.insert("0x39 sound".into());
            }
            "0x3b" => {
                if let Some(o) = obj!() {
                    o.shake = Some((f(&arg(1)), f(&arg(2)), f(&arg(3))));
                }
            }
            "0x46" | "0x47" => {
                let (x, y, z) = (f(&arg(1)).to_radians(), f(&arg(2)).to_radians(), f(&arg(3)).to_radians());
                if let Some(o) = emitter!() {
                    let m = Mat3::from_rotation_x(x) * Mat3::from_rotation_y(y) * Mat3::from_rotation_z(z);
                    let e = o.emitters.last_mut().unwrap();
                    // row-vector convention of the original: v * M
                    e.dir = (m.transpose() * e.dir).normalize_or(Vec3::Z);
                    e.fixed_dir = op == "0x46";
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
            "0x6a" => vm.unported.insert("0x6a decal".into()).then_some(()).unwrap_or(()),
            "0x40" => vm.unported.insert("0x40 uv scroll".into()).then_some(()).unwrap_or(()),
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
    mut physics: NonSendMut<Physics>,
) {
    let t0 = std::time::Instant::now();
    let dt = time.delta_secs();
    let vm = &mut *vm;
    let mut ctx = Ctx { commands: &mut commands, data: &data, lib: &mut lib, assets: &assets, meshes: &mut meshes, materials: &mut materials, physics: &mut physics, _q: std::marker::PhantomData };
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
        o.age += dt;
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
                    m.base_color.set_alpha(alpha);
                    if m.alpha_mode == AlphaMode::Opaque {
                        m.alpha_mode = AlphaMode::Blend;
                    }
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
                em.dir = obj_dir;
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
            idx.extend_from_slice(&[base, base + 1, base + 2, base, base + 2, base + 3]);
        }
        if pos.is_empty() {
            // an empty mesh is refused by the renderer: one zero-area triangle
            pos.extend([[0.0f32; 3]; 3]);
            uv.extend([[0.0f32; 2]; 3]);
            col.extend([[0.0f32; 4]; 3]);
            idx.extend([0, 1, 2]);
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

/// 0x2f children follow their parent object.
fn follow(mut vm: ResMut<FxVm>, mut q: Query<&mut Transform, With<FxEntity>>) {
    let pairs: Vec<(Entity, Entity)> = vm.objects.iter().filter_map(|(&e, o)| o.follow.map(|p| (e, p))).collect();
    for (e, p) in pairs {
        let Ok(pt) = q.get(p).map(|t| *t) else { continue };
        if let Ok(mut t) = q.get_mut(e) {
            t.translation = pt.translation;
            t.rotation = pt.rotation;
        }
        if let Some(o) = vm.objects.get_mut(&e) {
            o.place = Mat4::from_quat(engine_to_game()) * Mat4::from_rotation_translation(pt.rotation, pt.translation);
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
