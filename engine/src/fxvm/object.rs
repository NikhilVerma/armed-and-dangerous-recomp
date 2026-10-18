//! Fx objects (class `fx`, vtable 0x005a52d8; FxObject_Load 0x00418f60) and their per-frame update (fn_0041a350
//! 0x0041a350, render_r1.cpp:974): ground hits, life, fades, size over life, lights, uv transforms, camera cards,
//! mesh emitters (fx spawned by the object), particle emitters, texture frames. docs/subsystems/fx-vm.md 2-3.

use super::attach::point_world;
use super::body::{make_dynamic, touching};
use super::particles::{Emitter, step_emitter};
use super::script::{Instance, child_instance, find, pick_name};
use super::{FxEntity, FxLight, FxLayerMaterial, FxVm};
use crate::gamedata::GameData;
use crate::level::game_to_engine;
use crate::physics::Physics;
use bevy::prelude::*;

/// Size over life (Fx_SetScaleLinear / Exp / Log 0x0041d170 / 0x0041d210 / 0x0041d2c0; +0x480 from, +0x484 to,
/// +0x488 rate; mode flags 0x20 / 0x40 / 0x80 of +0x460, only when from != to).
#[derive(Clone, Copy, Debug)]
pub struct ScaleCurve {
    pub from: f32,
    pub to: f32,
    /// 0 linear (Fx_Lerp 0x0041a2d0), 1 exp (fn_0041a2f0 0x0041a2f0: from + (rate t / life)^2, rate = sqrt(to - from)),
    /// 2 eased out (fn_0041a310 0x0041a310: from + p (2 - t / life), p = (to - from) t / life)
    pub mode: u8,
}

impl ScaleCurve {
    pub fn new(from: f32, to: f32, mode: u8) -> Self {
        ScaleCurve { from, to, mode }
    }
    /// The scale at `age` of a `life` (no lifespan: the age stays 0 in the original's terms, the scale stays `from`).
    pub fn at(&self, age: f32, life: Option<f32>) -> f32 {
        let Some(life) = life.filter(|l| *l > 0.0) else { return self.from };
        if self.from == self.to {
            return self.from;
        }
        let (d, t) = (self.to - self.from, age);
        match self.mode {
            0 => d / life * t + self.from,
            1 => {
                let q = d.max(0.0).sqrt() / life * t;
                q * q + self.from
            }
            _ => {
                let r = 1.0 / life;
                let p = d * r * t;
                (1.0 - r * t) * p + p + self.from
            }
        }
    }
}

/// TexAnim_Fade (TexAnim_Step 0x00419d30): 0 past the end (when there is one), t / in during the ramp in, the ramp out
/// over the last `out` seconds dividing by (end - out) as the original does (render_r1.cpp:790, a possible original
/// bug: it reaches 0 only when out = end / 2), else 1.
pub fn ramp(t: f32, ramp_in: f32, ramp_out: f32, end: f32) -> f32 {
    if end > 0.0 && t > end {
        0.0
    } else if ramp_in > 0.0 && t < ramp_in {
        t / ramp_in
    } else if ramp_out > 0.0 && end - ramp_out < t {
        1.0 - (t - (end - ramp_out)) / (end - ramp_out)
    } else {
        1.0
    }
}

/// Fx_SetUvScroll 0x00420670 (opcode 0x40): uv offset += dir * speed * ramp * dt.
#[derive(Clone, Copy, Debug)]
pub struct UvScroll {
    pub dir: Vec2,
    pub speed: f32,
    pub end: f32,
    pub ramp_in: f32,
    pub ramp_out: f32,
}

/// Fx_SetUvScale 0x004202b0 (opcodes 0x41..0x44): the texture scale from -> to over `duration`.
#[derive(Clone, Copy, Debug)]
pub struct UvScale {
    /// 0 a random fixed scale, 1 linear, 2 exp (from + (sqrt(to - from) t / d)^2), 3 eased
    pub mode: u8,
    pub from: Vec2,
    pub to: Vec2,
    pub duration: f32,
    pub fixed: Option<Vec2>,
}

impl UvScale {
    /// TexAnim_Step 0x00419d30's path block: the end value once t >= duration.
    pub fn at(&self, t: f32) -> Vec2 {
        if let Some(f) = self.fixed {
            return f;
        }
        if t >= self.duration {
            return self.to;
        }
        let r = 1.0 / self.duration;
        match self.mode {
            1 => (self.to - self.from) * r * t + self.from,
            2 => {
                let q = (self.to - self.from).max(Vec2::ZERO).map(f32::sqrt) * r * t;
                q * q + self.from
            }
            _ => {
                let p = (self.to - self.from) * r * t;
                p * (1.0 - r * t) + self.from + p
            }
        }
    }
}

/// Fx_SetUvRotate 0x00420450 (opcode 0x45): angle += speed (deg/s) * ramp * dt.
#[derive(Clone, Copy, Debug)]
pub struct UvRotate {
    pub speed: f32,
    pub end: f32,
    pub ramp_in: f32,
    pub ramp_out: f32,
}

/// One texture stage's uv transform (the uv block +0x514, FxObject_EnsureUvScroll 0x00420520; the stage matrix
/// TexAnim_BuildStageMatrix 0x004195e0 = rotation x scale, then the scroll offset (mod 64), set by TexAnim_SetStages
/// 0x004196f0 for stages whose bit 0x100000 << s is on).
#[derive(Clone, Copy, Debug, Default)]
pub struct UvLayer {
    pub scroll: Option<UvScroll>,
    pub scale: Option<UvScale>,
    pub rotate: Option<UvRotate>,
    pub offset: Vec2,
    pub angle: f32,
}

impl UvLayer {
    pub fn active(&self) -> bool {
        self.scroll.is_some() || self.scale.is_some() || self.rotate.is_some()
    }
    /// TexAnim_Step 0x00419d30 at time t (the block's age) by dt.
    pub fn step(&mut self, t: f32, dt: f32) {
        if let Some(s) = self.scroll {
            let f = ramp(t, s.ramp_in, s.ramp_out, s.end);
            self.offset += s.dir * s.speed * f * dt;
            self.offset = Vec2::new(self.offset.x.rem_euclid(64.0), self.offset.y.rem_euclid(64.0));
        }
        if let Some(r) = self.rotate {
            self.angle += ramp(t, r.ramp_in, r.ramp_out, r.end) * r.speed * dt;
        }
    }
    /// The stage matrix: rotation (degrees) x scale, then the offset.
    pub fn matrix(&self, t: f32) -> bevy::math::Affine2 {
        let s = self.scale.map(|s| s.at(t)).unwrap_or(Vec2::ONE);
        let m = Mat2::from_angle(self.angle.to_radians()) * Mat2::from_diagonal(s);
        // the offset wrapped to 0..1 (the same image with repeat addressing; a clamped sampler would smear the edge)
        bevy::math::Affine2::from_mat2_translation(m, Vec2::new(self.offset.x.rem_euclid(1.0), self.offset.y.rem_euclid(1.0)))
    }
}

/// Fx_SetTextureAnimation 0x0041e310 (opcode 0x63; its block FxObject_EnsureTexAnim 0x0041e2c0): the stage texture
/// replaced by "<name>_<frame>"
/// (TexAnim_FindFrameTexture 0x004198c0, TexAnim_ResolveTextures 0x00419940), frame 0 = the base texture.
#[derive(Clone, Debug)]
pub struct TexAnim {
    pub rate: f32,
    pub frames: u32,
    pub looping: bool,
    pub acc: f32,
    pub frame: u32,
    pub base: String,
}

/// The object's point light (opcode 0x4a and 0x58..0x5a).
#[derive(Clone, Debug)]
pub struct LightDef {
    pub diffuse: Vec3,
    pub specular: Vec3,
    pub ambient: Vec3,
    pub attenuation1: f32,
    pub point: Option<String>,
}

/// 0x35 Fx_SetOnHitCreate 0x0041fc70: fx `<fx>_<surface>` at the contact (Fx_Create flags 0x818: the material variant
/// of the body hit), at most `count` times, `spacing` seconds apart (+0x52c, +0x524, +0x528).
#[derive(Clone, Debug)]
pub struct HitCreate {
    pub fx: String,
    pub count: i32,
    pub spacing: f32,
    pub last: f32,
}

/// What a ground hit does (+0x460 bits 0x10000 hit ground, 0x800 death, 0x1000 delete, 0x2000 create; +0x464 bit
/// 0x1000 area damage), run at the update after the contact (fn_0041a350's first block).
#[derive(Clone, Debug, Default)]
pub struct HitActions {
    pub ground: bool,
    pub death: bool,
    pub delete: bool,
    pub create: Option<HitCreate>,
    /// 0x76 Fx_SetOnHitAoe 0x0041fb00: (impact force, damage, inner, outer), applied once (AreaDamage_Apply 0x0047f020)
    pub aoe: Option<(f32, f32, f32, f32)>,
    /// in contact last update (a contact counts once, when it starts)
    pub touching: bool,
}

/// A mesh emitter (0x98 bytes, 10 per object at +0x4ec): spawns fx `fx` every `interval` seconds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EmitMode {
    /// 0x30 (flag 1): a child fx following the object, at points of list1 / list2, when the timer runs out
    Children,
    /// 0x19 / 0x36 (flag 2): at a point of list1, its matrix interpolated over the frame (type 0x32: the matrix given)
    Path,
    /// 0x17 (flag 0x80): positions along the object's path (type 0x38)
    Along,
    /// 0x18 (flag 0x40): positions between the last and the current point of list1 (type 0x38)
    Points,
}

#[derive(Clone, Debug)]
pub struct MeshEmitter {
    pub fx: String,
    pub interval: f32,
    /// +0x2c: starts at -interval (the first spawns at once)
    pub timer: f32,
    pub mode: EmitMode,
    pub list1: Vec<String>,
    pub list2: Vec<String>,
    /// +0x38 (0x2a): seconds left; at 0 the emitter stops
    pub timeout: Option<f32>,
    pub stopped: bool,
    /// the last matrix / position (+0x3c, +0x48/+0x58/+0x68), game space
    pub last: Mat4,
    /// 0x36: the random rotation box (degrees, min / max per axis; object flag 0x4000)
    pub random_rot: Option<(Vec3, Vec3)>,
}

impl MeshEmitter {
    /// Fx_Emit 0x0041e4d0: +0x28 interval, +0x2c = -interval, +0x38 = -1; the matrix of the start point.
    pub fn new(fx: String, interval: f32, mode: EmitMode, list1: Vec<String>, list2: Vec<String>, start: Mat4, random_rot: Option<(Vec3, Vec3)>) -> Self {
        let interval = interval.max(1e-3);
        MeshEmitter { fx, interval, timer: -interval, mode, list1, list2, timeout: None, stopped: false, last: start, random_rot }
    }
}

/// A live fx object: a template's mesh with the script's timers, fades, scaling and emitters.
pub struct FxObject {
    pub template: String,
    /// the anim whose nodes are its attach points (the template's default anim)
    pub anim: Option<String>,
    /// Game-space placement (rotation + position) without the scale animation.
    pub place: Mat4,
    /// the uniform scale this frame
    pub scale_now: f32,
    pub age: f32,
    pub lifespan: Option<f32>,
    /// (delay or time, 1 / duration, still delayed)
    pub fade_in: Option<(f32, f32, bool)>,
    /// the fade-out start age
    pub fade_out: Option<f32>,
    pub scale: Option<ScaleCurve>,
    pub kite: bool,
    pub emitters: Vec<Emitter>,
    pub mesh_emitters: Vec<MeshEmitter>,
    pub texanim: Option<TexAnim>,
    /// uv transforms of texture layers 1 and 2 (the meshes' one stage, the snow domes' second)
    pub uv: [UvLayer; 2],
    pub shake: Option<(f32, f32, f32)>,
    /// the entity it follows and its offset from that entity's placement (0x2f children, 0x13 objects, fx started on
    /// a moving entity)
    pub follow: Option<(Entity, Mat4)>,
    /// follow only the parent's position (camera_fx position_only)
    pub follow_pos_only: bool,
    pub materials: Vec<Handle<StandardMaterial>>,
    pub layer_mats: Vec<Handle<FxLayerMaterial>>,
    pub alpha: f32,
    pub body: Option<oxijolt::prelude::BodyId>,
    /// its body when it gets one: the template's mass and surface, the mesh's bounding box
    pub body_def: super::body::BodyDef,
    /// opcode 0x79 "set inert" (Fx_SetInert 0x0041e230: +0x424 |= 0x20000): no collision shape
    pub inert: bool,
    /// 0x10 / 0x1e impulses and 0x1a spins given before the object has a body: applied when it gets one
    pub impulse: Vec3,
    pub spin: Vec3,
    pub hit: HitActions,
    /// 0x1d: the fx started where the object ends (+0x51c, +0x460 bit 0)
    pub on_death: Option<String>,
    pub light: Option<LightDef>,
    /// it carries a post-process effect (obj+0x634): its position goes to the effect each update (0x0041bd30)
    pub screen: bool,
}

impl FxObject {
    pub fn new(template: &str, anim: Option<String>, place: Mat4, materials: Vec<Handle<StandardMaterial>>, layer_mats: Vec<Handle<FxLayerMaterial>>) -> Self {
        FxObject {
            template: template.to_owned(),
            anim,
            place,
            scale_now: 1.0,
            age: 0.0,
            lifespan: None,
            fade_in: None,
            fade_out: None,
            scale: None,
            kite: false,
            emitters: Vec::new(),
            mesh_emitters: Vec::new(),
            texanim: None,
            uv: [UvLayer::default(); 2],
            shake: None,
            follow: None,
            follow_pos_only: false,
            materials,
            layer_mats,
            alpha: 1.0,
            body: None,
            body_def: super::body::BodyDef::default(),
            inert: false,
            impulse: Vec3::ZERO,
            spin: Vec3::ZERO,
            hit: HitActions::default(),
            on_death: None,
            light: None,
            screen: false,
        }
    }

    pub fn has_body(&self) -> bool {
        self.body.is_some()
    }

    /// The scale it has now, a 0x0b..0x0e set this frame included (scale_now follows at the next update).
    pub fn scale_at_now(&self) -> f32 {
        self.scale.map(|s| s.at(self.age, self.lifespan)).unwrap_or(self.scale_now)
    }

    /// Texture layer 1..2 (layers 3 and 4 have no stage on these meshes).
    pub fn uv_layer(&mut self, layer: i64) -> Option<&mut UvLayer> {
        match layer {
            1 => Some(&mut self.uv[0]),
            2 => Some(&mut self.uv[1]),
            _ => None,
        }
    }

    /// vt+0x100: the object's alpha on every part (the original blends in gamma space, Bevy in linear: alpha^2.2 gives
    /// the same look for an alpha-blended fade: the intro's white, a straight line from 252 to the scene over 0.5..3 s).
    pub fn set_alpha(&mut self, materials: &mut Assets<StandardMaterial>, alpha: f32) {
        self.alpha = alpha;
        for m in &self.materials {
            if let Some(mut m) = materials.get_mut(m) {
                if m.alpha_mode == AlphaMode::Opaque {
                    m.alpha_mode = AlphaMode::Blend;
                }
                let a = if m.alpha_mode == AlphaMode::Blend { alpha.powf(2.2) } else { alpha };
                m.base_color.set_alpha(a);
            }
        }
    }

    /// The alpha the fades give at this update (render_r1.cpp:1080-1113): a delayed fade-in counts its delay down,
    /// then alpha = time / duration until 1 (0.999 then, done); otherwise past the fade-out start alpha falls linearly
    /// to 0 at the lifespan; else 0.999.
    fn fade(&mut self, dt: f32) -> Option<f32> {
        if let Some((mut t, rate, mut delayed)) = self.fade_in {
            if delayed {
                t -= dt;
                if t <= 0.0 {
                    delayed = false;
                    t = -t;
                } else {
                    self.fade_in = Some((t, rate, true));
                    return None;
                }
            } else {
                t += dt;
            }
            let a = t * rate;
            self.fade_in = if a >= 1.0 { None } else { Some((t, rate, delayed)) };
            return Some(a.min(1.0));
        }
        let (start, life) = (self.fade_out?, self.lifespan?);
        Some(if self.age > start { (1.0 - (self.age - start) / (life - start).max(1e-4)).max(0.0) } else { 1.0 })
    }
}

/// The end of an object (fn_00419290 0x00419290): with `death` and an on-death fx, that fx at the object's position
/// (Fx_Create params 0x38: position only); its body removed and its entity despawned. Returns the new instance.
pub fn end_object(o: &FxObject, data: &GameData, physics: &mut Physics, commands: &mut Commands, e: Entity, death: bool) -> Option<Instance> {
    if let Some(b) = o.body {
        super::body::remove(physics, b);
    }
    commands.entity(e).despawn();
    let name = o.on_death.as_ref().filter(|_| death)?;
    let (n, script) = find(data, name)?;
    Some(Instance::new(n, script, Mat4::from_translation(o.place.w_axis.truncate())))
}


/// The fx object update (fn_0041a350 0x0041a350, render_r1.cpp:974), once per frame for every live object, in the
/// original's order: the ground hit's actions (+0x464 bit 0x2000), the camera shake (moved before the life check:
/// the shakes sit on objects that live 0.01 s, which the engine's first step of at least 1/60 s ends at once),
/// age and lifespan (ending runs the on-death fx), fades, size over life, the light, uv transforms and texture
/// frames, the placement (camera card 0x00419a60), mesh emitters, particle emitters (0x0041a050).
#[allow(clippy::too_many_arguments)]
pub fn objects(
    mut commands: Commands,
    time: Res<Time>,
    mut vm: ResMut<FxVm>,
    data: Res<GameData>,
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
    let vm = &mut *vm;
    vm.lights.clear();
    let now = vm.time;
    let mut objs = std::mem::take(&mut vm.objects);
    let mut gone = Vec::new();
    let mut spawned: Vec<Instance> = Vec::new();
    for (&e, o) in objs.iter_mut() {
        let Ok(mut t) = q.get_mut(e) else { continue }; // spawned this frame: starts next frame
        // a 0x10 impulse on an object no 0x11 / 0x64 made dynamic (fx_trash): its body now, normal gravity (guess:
        // 0x64's; whether fx bodies are dynamic from creation is not traced)
        if (o.impulse != Vec3::ZERO || o.spin != Vec3::ZERO) && o.body.is_none() && !o.inert {
            make_dynamic(&mut physics, &mut commands, o, e, 1.0);
        }
        let body = o.body;
        // physics-driven objects: the placement read back from the body's pose
        if body.is_some() {
            o.place = Mat4::from_quat(axes.inverse()) * Mat4::from_rotation_translation(t.rotation, t.translation);
        }
        // ground hits (the contact callback 0x00419330 sets +0x464 bit 0x2000; acted on here)
        if let (true, Some(b)) = (o.hit.ground, body) {
            let contact = touching(&physics, b, o, dt);
            let new_hit = contact.is_some() && !o.hit.touching;
            o.hit.touching = contact.is_some();
            if let (true, Some((at, other))) = (new_hit, contact) {
                vm.stats.hits += 1;
                if let Some(c) = o.hit.create.as_mut() {
                    if c.count != 0 && now - c.spacing > c.last {
                        // the variant of the surface it landed on (surface.rs)
                        let name = physics.surface(other, at).variant(&c.fx, |n| data.fx.contains_key(n));
                        if let Some((n, script)) = find(&data, &name) {
                            spawned.push(Instance::new(n, script, Mat4::from_translation(at)));
                        }
                        c.last = now;
                        if c.count > 0 {
                            c.count -= 1;
                        }
                    }
                }
                if let Some((impact, damage, inner, outer)) = o.hit.aoe.take() {
                    vm.stats.blasts += 1;
                    vm.out.explosions.push(crate::fx::Explode {
                        at: axes * o.place.w_axis.truncate(),
                        area_damage: damage,
                        radius_inner: inner,
                        radius_outer: outer,
                        impact_force: impact,
                        source: None,
                        delay: 0.0,
                        by: crate::fx::Src::Object,
                        cartoon: false,
                    });
                }
                if o.hit.delete || o.hit.death {
                    spawned.extend(end_object(o, &data, &mut physics, &mut commands, e, o.hit.death));
                    gone.push(e);
                    continue;
                }
            }
        }
        // shake: one-shot when the camera is within the radius (camera.rs)
        if let Some((time_s, radius, amp)) = o.shake.take() {
            if let Some(c) = cam_t {
                if c.translation.distance(axes * o.place.w_axis.truncate()) <= radius {
                    vm.out.shakes.push(crate::camera::ShakeCamera { amplitude: amp, secs: time_s });
                }
            }
        }
        // the original ran at 60 fps: an object's first update steps at least 1/60 s, so scripts with lifespans
        // under a frame (Roman's snow footsteps: 0.01) end before their emitters run, as there (render_r1.cpp:1071)
        let step = if o.age == 0.0 { dt.max(1.0 / 60.0) } else { dt };
        o.age += step;
        if let Some(life) = o.lifespan {
            if o.age >= life {
                spawned.extend(end_object(o, &data, &mut physics, &mut commands, e, true));
                gone.push(e);
                continue;
            }
        }
        // fades (vt+0x100: object alpha)
        if let Some(alpha) = o.fade(dt) {
            if (alpha - o.alpha).abs() > 1e-3 {
                o.set_alpha(&mut materials, alpha);
            }
        }
        // size over life (Object_SetScale 0x004535d0)
        o.scale_now = o.scale.map(|s| s.at(o.age, o.lifespan)).unwrap_or(1.0);
        // the light at its attach point (re-added every update: LightSlot_Acquire 0x0048a4b0)
        if let Some(l) = &o.light {
            let pt = l.point.as_deref().and_then(|n| vm.attach.point(o.anim.as_deref(), n));
            let at = point_world(o.place, o.scale_now, pt).w_axis.truncate();
            vm.lights.push(FxLight { position: axes * at, diffuse: l.diffuse, specular: l.specular, ambient: l.ambient, attenuation1: l.attenuation1 });
        }
        // uv transforms (TexAnim_Step 0x00419d30 per active stage; TexAnim_SetStages 0x004196f0 applies the matrix)
        if o.uv[0].active() {
            o.uv[0].step(o.age, dt);
            let m = o.uv[0].matrix(o.age);
            for h in &o.materials {
                if let Some(mut mat) = materials.get_mut(h) {
                    mat.uv_transform = m;
                }
            }
        }
        if o.uv[1].active() {
            o.uv[1].step(o.age, dt);
        }
        if !o.layer_mats.is_empty() {
            let (o1, o2) = (o.uv[0].offset, o.uv[1].offset);
            for m in &o.layer_mats {
                if let Some(mut m) = layer_materials.get_mut(m) {
                    m.params.offsets = Vec4::new(o1.x.rem_euclid(1.0), o1.y.rem_euclid(1.0), o2.x.rem_euclid(1.0), o2.y.rem_euclid(1.0));
                    m.params.colour.w = o.alpha;
                }
            }
        }
        // texture frames: frame += int(acc * rate), the fraction kept; looping wraps, else the last frame holds
        if let Some(ta) = o.texanim.as_mut() {
            ta.acc += dt;
            let whole = (ta.acc * ta.rate) as u32;
            if whole > 0 {
                ta.acc -= whole as f32 / ta.rate;
                ta.frame = if ta.looping { (ta.frame + whole) % ta.frames } else { (ta.frame + whole).min(ta.frames - 1) };
                let name = if ta.frame == 0 { ta.base.clone() } else { format!("{}_{}", ta.base, ta.frame) };
                let h: Handle<Image> = assets.load(crate::graphics::texture_path(&name));
                for m in &o.materials {
                    if let Some(mut m) = materials.get_mut(m) {
                        m.base_color_texture = Some(h.clone());
                    }
                }
            }
        }
        // placement: kite (0x00419a60 with +0x460 bit 0x400: the camera's rotation, the object's position), scale
        if body.is_none() {
            let world = Mat4::from_quat(axes) * o.place;
            let (_, mut rot, pos) = world.to_scale_rotation_translation();
            if o.kite {
                if let Some(c) = cam_t {
                    // the card's mesh faces -Y in game space: turn it to the camera
                    rot = c.rotation * axes;
                }
            }
            *t = Transform { translation: pos, rotation: rot, scale: Vec3::splat(o.scale_now) };
        } else {
            t.scale = Vec3::splat(o.scale_now);
        }
        // mesh emitters (+0x460 bit 0x100), then particle emitters (bit 0x200)
        if !o.mesh_emitters.is_empty() {
            let mut ems = std::mem::take(&mut o.mesh_emitters);
            for em in &mut ems {
                step_mesh_emitter(vm, &data, e, o, em, dt, &mut spawned);
            }
            o.mesh_emitters = ems;
        }
        let pos = o.place.w_axis.truncate();
        let obj_dir = -o.place.y_axis.truncate();
        for em in &mut o.emitters {
            step_emitter(&mut vm.pools, &mut vm.rng, em, pos, obj_dir, dt);
        }
        if o.screen {
            vm.out.screen.push(super::screen::ScreenRequest::Position(e.to_bits(), axes * pos));
        }
    }
    for e in gone {
        objs.remove(&e);
    }
    vm.stats.objects = objs.len();
    vm.objects = objs;
    vm.instances.extend(spawned);
}

/// One mesh emitter's frame (fn_0041a350's sub-emitter block, render_r1.cpp:1233): the timer counts the frame down
/// and the emitter spawns while it is due, each spawn adding the interval (the pass repeats with dt = 0 while the
/// timer is still negative). Path / points / along spawn at the interpolated place of the due time; children spawn
/// one child fx following the object when the timer runs out, the timer reset to the interval.
fn step_mesh_emitter(vm: &mut FxVm, data: &GameData, e: Entity, o: &FxObject, em: &mut MeshEmitter, dt: f32, spawned: &mut Vec<Instance>) {
    if em.stopped {
        return;
    }
    if let Some(t) = em.timeout.as_mut() {
        *t -= dt;
        if *t <= 0.0 {
            em.stopped = true;
            return;
        }
    }
    let Some(script) = data.fx.get(&em.fx).cloned() else { return };
    em.timer -= dt;
    let pos = o.place.w_axis.truncate();
    match em.mode {
        EmitMode::Children => {
            if em.timer < 0.0 {
                let l1 = (!em.list1.is_empty()).then_some(&em.list1);
                let l2 = (!em.list2.is_empty()).then_some(&em.list2);
                let child = child_instance(vm, em.fx.clone(), script, e, true, l1, l2);
                spawned.push(child);
                em.timer = em.interval;
            }
        }
        _ => {
            let mut last_now = em.last;
            while -em.interval >= em.timer {
                // how far into the frame the spawn was due: the share of the current place (k / timer * interval with
                // [0x005bcb5c] = -1: guess)
                let f = (em.interval / -em.timer).clamp(0.0, 1.0);
                let l1 = (!em.list1.is_empty()).then_some(&em.list1);
                let point = pick_name(&mut vm.rng, l1);
                let now = match em.mode {
                    EmitMode::Along => Mat4::from_translation(pos),
                    _ => point_world(o.place, o.scale_now, point.as_deref().and_then(|n| vm.attach.point(o.anim.as_deref(), n))),
                };
                let (_, r0, t0) = em.last.to_scale_rotation_translation();
                let (_, r1, t1) = now.to_scale_rotation_translation();
                let mut m = Mat4::from_rotation_translation(r0.slerp(r1, f), t0.lerp(t1, f));
                if let Some((lo, hi)) = em.random_rot {
                    // the random rotation (object flag 0x4000): three rand() in the box, Matrix_FromEulerDeg, turned in
                    // the world frame, the position kept
                    let x = vm.rng.rand() as f32 * (hi.x - lo.x) / 32768.0 + lo.x;
                    let y = vm.rng.rand() as f32 * (hi.y - lo.y) / 32768.0 + lo.y;
                    let z = vm.rng.rand() as f32 * (hi.z - lo.z) / 32768.0 + lo.z;
                    let r = Mat4::from_mat3(super::script::euler_deg(x, y, z));
                    let tr = m.w_axis;
                    m = r * m;
                    m.w_axis = tr;
                }
                let mut inst = Instance::new(em.fx.clone(), script.clone(), match em.mode {
                    EmitMode::Path => m,
                    _ => Mat4::from_translation(m.w_axis.truncate()),
                });
                if em.mode == EmitMode::Path {
                    let l2 = (!em.list2.is_empty()).then_some(&em.list2);
                    inst.child_point = pick_name(&mut vm.rng, l2);
                }
                spawned.push(inst);
                last_now = now;
                em.timer += em.interval;
            }
            em.last = match em.mode {
                EmitMode::Along => Mat4::from_translation(pos),
                _ => last_now,
            };
        }
    }
}

/// 0x2f children, 0x13 objects and fx started on a moving entity follow it (its world transform times their offset);
/// a gone parent ends them (FxObject_Remove: attached objects go with it).
pub fn follow(mut commands: Commands, mut vm: ResMut<FxVm>, parents: Query<&GlobalTransform>, mut q: Query<&mut Transform, With<FxEntity>>, mut physics: NonSendMut<Physics>) {
    let to_game = Mat4::from_quat(game_to_engine().inverse());
    let pairs: Vec<(Entity, Entity, Mat4)> = vm.objects.iter().filter_map(|(&e, o)| o.follow.map(|(p, off)| (e, p, off))).collect();
    for (e, p, offset) in pairs {
        let Ok(pt) = parents.get(p).map(|t| t.compute_transform()) else {
            if let Some(o) = vm.objects.remove(&e) {
                if let Some(b) = o.body {
                    super::body::remove(&mut physics, b);
                }
            }
            commands.entity(e).despawn();
            continue;
        };
        // the parent's placement in game space: an fx object's own (without its scale animation), else its transform
        let parent_place = match vm.objects.get(&p) {
            Some(po) => po.place,
            None => to_game * Mat4::from_rotation_translation(pt.rotation, pt.translation),
        };
        let Some(o) = vm.objects.get_mut(&e) else { continue };
        if o.follow_pos_only {
            o.place.w_axis = (parent_place.w_axis.truncate() + offset.w_axis.truncate()).extend(1.0);
        } else {
            o.place = parent_place * offset;
        }
        if o.body.is_none() {
            if let Ok(mut t) = q.get_mut(e) {
                let (_, rot, pos) = (Mat4::from_quat(game_to_engine()) * o.place).to_scale_rotation_translation();
                t.translation = pos;
                if !o.follow_pos_only && !o.kite {
                    t.rotation = rot;
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scale_curves() {
        // the keg's blast ring: eased 4 -> 50 over 0.3 s
        let s = ScaleCurve::new(4.0, 50.0, 2);
        assert!((s.at(0.0, Some(0.3)) - 4.0).abs() < 1e-5);
        assert!((s.at(0.3, Some(0.3)) - 50.0).abs() < 1e-3);
        assert!(s.at(0.15, Some(0.3)) > 4.0 + 23.0); // ahead of linear
        let l = ScaleCurve::new(5.0, 20.0, 0);
        assert!((l.at(0.1, Some(0.2)) - 12.5).abs() < 1e-4);
        let x = ScaleCurve::new(1.0, 5.0, 1);
        assert!((x.at(1.0, Some(1.0)) - 5.0).abs() < 1e-4);
        assert!(x.at(0.5, Some(1.0)) < 3.0); // eased in
        assert_eq!(l.at(1.0, None), 5.0);
    }

    #[test]
    fn uv_ramp_and_scroll() {
        assert_eq!(ramp(5.0, 0.0, 0.0, 0.0), 1.0); // no end: forever
        assert_eq!(ramp(5.0, 0.0, 0.0, 4.0), 0.0);
        assert!((ramp(0.5, 1.0, 0.0, 4.0) - 0.5).abs() < 1e-6);
        // the ramp out divides by (end - out), as the original
        assert!((ramp(3.5, 0.0, 1.0, 4.0) - (1.0 - 0.5 / 3.0)).abs() < 1e-6);
        let mut l = UvLayer { scroll: Some(UvScroll { dir: Vec2::X, speed: 2.0, end: 0.0, ramp_in: 0.0, ramp_out: 0.0 }), ..default() };
        l.step(1.0, 0.5);
        assert!((l.offset.x - 1.0).abs() < 1e-6);
        let s = UvScale { mode: 1, from: Vec2::splat(0.25), to: Vec2::splat(5.0), duration: 8.0, fixed: None };
        assert!((s.at(4.0).x - 2.625).abs() < 1e-5);
        assert_eq!(s.at(9.0), Vec2::splat(5.0));
    }

    #[test]
    fn mesh_emitter_timer() {
        // spawns at once, then every interval
        let mut em = MeshEmitter::new("x".into(), 0.1, EmitMode::Along, vec![], vec![], Mat4::IDENTITY, None);
        let mut n = 0;
        for _ in 0..30 {
            em.timer -= 1.0 / 60.0;
            while -em.interval >= em.timer {
                n += 1;
                em.timer += em.interval;
            }
        }
        // 0.5 s at 0.1: 5 or 6
        assert!((5..=6).contains(&n), "{n}");
    }
}
