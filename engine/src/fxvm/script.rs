//! The fx script interpreter (Fx_RunScript 0x00420770, render_r2b.cpp; opcode table in its header) and fx object
//! creation (Fx_CreateObject 0x004234d0, Fx_AttachMesh 0x00423680: game/fx.cpp). One instance runs from its
//! instruction pointer until it ends (0x04), pauses (0x12) or waits (0x15) (Fx_Step 0x00423360).
//!
//! Locals of a run (render_r2b.cpp:211): the current object `cur` (set by 0x05 / 0x13 / 0x37), the push stack (0x06 /
//! 0x07; setters write to its top object, so an object must be created and pushed), the name lists list1 / list2
//! (0x104). Operands and random numbers are consumed whether or not there is a current object, so the random
//! sequence never depends on it.

use super::attach::point_world;
use super::body::{add_impulse, add_spin, make_dynamic, set_rotation, wake};
use super::materials::{FxLayerMaterial, FxLayerParams, premultiplied};
use super::object::{EmitMode, FxObject, HitCreate, LightDef, MeshEmitter, ScaleCurve, UvRotate, UvScale, UvScroll};
use super::particles::{Emitter, build_ramp};
use super::screen::{ScreenKind, ScreenRequest};
use super::{CrtRand, FxEntity, FxVm};
use crate::gamedata::GameData;
use crate::level::game_to_engine;
use crate::physics::Physics;
use crate::scene::Library;
use bevy::prelude::*;
use serde_json::Value;

/// One running fx (the original's fx instance, 0x128 bytes: FxInstance_Alloc 0x0041cc20).
pub struct Instance {
    /// the script's name in the bank (the material variant resolved)
    pub name: String,
    pub script: Vec<Value>,
    /// fx+4 instruction pointer
    pub ip: usize,
    /// fx+8 pause timer (state 1)
    pub paused: f32,
    /// state 4 (0x15): idle until released
    pub waiting: bool,
    /// Placement of the objects it creates (game space).
    pub place: Mat4,
    /// The object it runs on (fx+0x6c / its owner fx+0xb4): a parent fx object, or the entity a StartFx follows.
    pub parent: Option<Entity>,
    /// Its objects follow the parent (0x2f children, fx started on a moving entity).
    pub follow: bool,
    pub follow_pos_only: bool,
    /// The parent's attach point it starts at (fx+0x64) and the point of its own objects put there (fx+0x68).
    pub parent_point: Option<String>,
    pub child_point: Option<String>,
    /// 0x14 "set logobject" (Fx_SetLogObject 0x0041df20: fx+0x14 |= 0x20, object list fx+0x124): objects ended when the
    /// instance is released (Fx_Release 0x0041d030 -> FxInstance_DestroyEmitters 0x0041cfb0 -> 0x00419290)
    pub logged: Vec<Entity>,
    /// started as a saved game restores it (crate::fx::RestoreFx): its objects with an end are already gone
    pub restored: bool,
}

impl Instance {
    pub fn new(name: String, script: Vec<Value>, place: Mat4) -> Self {
        Instance { name, script, ip: 0, paused: 0.0, waiting: false, place, parent: None, follow: false, follow_pos_only: false, parent_point: None, child_point: None, logged: Vec::new(), restored: false }
    }
}

/// 0x3f force feedback request (Fx_PlayForce 0x0041ff60): rumble sample name, looping (edi), player only (a3: the
/// object or an ancestor must be the player, 0x0045a480), gain distances min / max (+0x4e4 / +0x4e8: gain 1 within
/// min, falling linearly to 0 at max, 1 when max is 0; render_r1.cpp:1467), the object it follows (None: one-shot).
pub struct FxForce {
    pub name: String,
    pub looping: bool,
    pub player_only: bool,
    pub min: f32,
    pub max: f32,
    pub owner: Option<Entity>,
}

/// FxBank_Find 0x0041cb60: a script by name; a material variant the bank lacks falls back to the base fx
/// (Fx_Create's "%s_%s": fx_imp_arrow_grunt -> fx_imp_arrow).
pub fn find(data: &GameData, name: &str) -> Option<(String, Vec<Value>)> {
    let name = name.to_lowercase();
    if let Some(s) = data.fx.get(&name) {
        return Some((name, s.clone()));
    }
    let base = name.rsplit_once('_').map(|(b, _)| b.to_owned()).unwrap_or_default();
    data.fx.get(&base).map(|s| (base, s.clone()))
}

fn f(v: &Value) -> f32 {
    v.as_f64().unwrap_or(0.0) as f32
}

fn names(v: &Value) -> Vec<String> {
    v.as_array().map(|a| a.iter().filter_map(|s| s.as_str().map(str::to_owned)).collect()).unwrap_or_default()
}

/// FxNameList_PickRandom 0x0041cba0: entry (rand() * count) >> 15 (one rand), None for "null" (_stricmp with
/// 0x005bba94) or no list (no rand).
pub fn pick_name(rng: &mut CrtRand, l: Option<&Vec<String>>) -> Option<String> {
    let l = l?;
    if l.is_empty() {
        return None;
    }
    let i = rng.index(l.len());
    l.get(i).filter(|n| !n.eq_ignore_ascii_case("null")).cloned()
}

/// Matrix_FromEulerDeg 0x004e3db0 (angles A, B, C in degrees, x [0x005bcba0] = pi/180), rows (column vectors, as the
/// game's matrices: translation in m3 / m7 / m11): (cC cB, -sA cC sB - sC cA, sC sA - cC sB cA),
/// (cB sC, cC cA - sC sB sA, -sC sB cA - cC sA), (sB, cB sA, cB cA) = Rz(C) Ry(-B) Rx(A).
pub fn euler_deg(a: f32, b: f32, c: f32) -> Mat3 {
    let (sa, ca) = a.to_radians().sin_cos();
    let (sb, cb) = b.to_radians().sin_cos();
    let (sc, cc) = c.to_radians().sin_cos();
    Mat3::from_cols(
        Vec3::new(cc * cb, cb * sc, sb),
        Vec3::new(-(sa * (cc * sb)) - sc * ca, cc * ca - (sc * sb) * sa, cb * sa),
        Vec3::new(sc * sa - (cc * sb) * ca, -((sc * sb) * ca) - cc * sa, cb * ca),
    )
}

/// Everything a script run needs from the world.
pub struct Ctx<'a, 'cw, 'cs> {
    pub commands: &'a mut Commands<'cw, 'cs>,
    pub data: &'a GameData,
    pub lib: &'a mut Library,
    pub assets: &'a AssetServer,
    pub meshes: &'a mut Assets<Mesh>,
    pub materials: &'a mut Assets<StandardMaterial>,
    pub layer_materials: &'a mut Assets<FxLayerMaterial>,
    pub physics: &'a mut Physics,
    /// the 3D camera: fx locked to it (the intro overlays) draw over the scene
    pub camera: Option<Entity>,
}

/// Where an instance's new object goes (game space) and what it follows: the parent fx object's attach point
/// (fx+0x64) with the object's own point (fx+0x68) put on it; a 0x0f child is placed there once, a 0x2f child and an
/// fx started on a moving entity follow (game/fx.cpp:132-169).
fn placement(vm: &mut FxVm, inst_idx: usize, template_anim: Option<&str>) -> (Mat4, Option<(Entity, Mat4)>) {
    let inst = &vm.instances[inst_idx];
    let (parent, follow) = (inst.parent, inst.follow);
    let (pp, cp) = (inst.parent_point.clone(), inst.child_point.clone());
    let mut place = inst.place;
    if let Some(p) = parent.and_then(|p| vm.objects.get(&p)) {
        let (pplace, scale, anim) = (p.place, p.scale_now, p.anim.clone());
        let point = pp.as_deref().and_then(|n| vm.attach.point(anim.as_deref(), n));
        place = point_world(pplace, scale, point);
    }
    if let Some(c) = cp.as_deref().and_then(|n| vm.attach.point(template_anim, n)) {
        place *= c.inverse();
    }
    let follows = match (follow, parent) {
        (true, Some(p)) => {
            // the offset from the parent's placement (identity for an fx started on a non-fx entity)
            let offset = vm.objects.get(&p).map(|o| o.place.inverse() * place).unwrap_or(Mat4::IDENTITY);
            Some((p, offset))
        }
        _ => None,
    };
    (place, follows)
}

/// Fx_CreateObject 0x004234d0: the template's mesh as an fx object (class `fx`, FxObject_Load 0x00418f60 clears its
/// script state) at the instance's placement.
fn create_object(vm: &mut FxVm, ctx: &mut Ctx<'_, '_, '_>, idx: usize, template: &str, attach: Option<(Entity, Mat4)>) -> Option<Entity> {
    let t = ctx.data.templates.get(&template.to_lowercase())?;
    let anim = t.default_anim.clone().or_else(|| t.model.clone());
    let (place, mut follows) = placement(vm, idx, anim.as_deref());
    let (place, follows) = match attach {
        // 0x13: on the owner at its point, following it
        Some((owner, offset)) => {
            let base = vm.objects.get(&owner).map(|o| o.place).unwrap_or(vm.instances[idx].place);
            follows = Some((owner, offset));
            (base * offset, follows)
        }
        None => (place, follows),
    };
    let inst = &vm.instances[idx];
    let world = Mat4::from_quat(game_to_engine()) * place;
    let mut e = ctx.commands.spawn((Transform::from_matrix(world), Visibility::default(), Name::new(template.to_owned()), FxEntity));
    let mut mats = Vec::new();
    let mut layers = Vec::new();
    let mut body_def = super::body::BodyDef::default();
    if let Some(model) = t.model.as_ref().and_then(|m| ctx.lib.load_model(m, false, ctx.assets, ctx.meshes, ctx.materials)) {
        if model.max.cmpge(model.min).all() && !model.vertices.is_empty() {
            // once per template (the hull of a 900-vertex roof is not free)
            let k = ctx.physics.meters_per_unit;
            body_def = vm
                .body_defs
                .entry(template.to_lowercase())
                .or_insert_with(|| {
                    let p = ctx.data.template_props(&template.to_lowercase());
                    // the hull's volume only matters to collide_poly templates (debris)
                    let volume = if p.f("collide_poly", 0.0) != 0.0 { super::body::hull_volume(&model.vertices) } else { 0.0 };
                    super::body::BodyDef::new(model.min, model.max, volume, &p).with_hull(&model.vertices, k)
                })
                .clone();
        }
        let parts = model.parts.clone();
        let locked = follows.is_some() && !inst.follow_pos_only && inst.parent.is_some() && inst.parent == ctx.camera;
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
                    let tex2 = def.texture2.as_deref().unwrap_or(&tex).to_owned();
                    let mask = def.mask.as_deref().unwrap_or(&tex).to_owned();
                    let lm = ctx.layer_materials.add(FxLayerMaterial {
                        t0: load(&tex),
                        t1: load(&tex2),
                        // the mask is data (linear): an uncompressed BGRA DDS fails to load as sRGB (black fallback)
                        t2: crate::gamematerial::load_tex(ctx.assets, &mask, false),
                        params: FxLayerParams {
                            offsets: Vec4::ZERO,
                            colour: Vec4::new(def.emissive[0], def.emissive[1], def.emissive[2], 1.0),
                            flags: Vec4::new(premultiplied(&ctx.lib.dir, &tex) as u8 as f32, premultiplied(&ctx.lib.dir, &tex2) as u8 as f32, premultiplied(&ctx.lib.dir, &mask) as u8 as f32, 0.0),
                        },
                    });
                    layers.push(lm.clone());
                    c.spawn((Mesh3d(mesh), MeshMaterial3d(lm), bevy::light::NotShadowCaster));
                    continue;
                }
                // textured fx draw texture x the material's diffuse (stage MODULATE(texture, diffuse), the fx objects'
                // material colour 0.588 on fxo_flash): measured on the sticky blast's flash, the original adds ~0.6 of
                // what texture x white added (work/shots/stickY vs work/remaster/play/stickX)
                let (def, alpha_tex) = ctx.lib.game_defs.get(&mat.id()).map_or((None, false), |(d, _, a)| (Some(d.clone()), *a));
                // the blend (0x004897d0): a translucent material (kind bit 0x10) takes its blend byte +0x37: 0 -> state
                // 0x120, additive (SRCALPHA / ONE: fx_glow_red, the flashes, the snow), 2 -> 0x320, alpha-blended (the
                // glass shards fxo_debri_glass_*, fx_smoke_snow, fx_impact_snow_small: they drew as additive white
                // squares); without the bit an alpha texture blends (SRCALPHA / INVSRCALPHA: fx_white, fx_true_story),
                // except that one whose blend byte is 0 adds too (guess: which path gives these kind-1 records the
                // additive state is not traced): the flames, muzzle flashes and the flying keg's fire trail
                // (fx_fire_trail), which the original's capture of a keg (work/shots/orig_keg, 2026-10-18) shows as a
                // pale streak over the snow where an alpha-blended trail drew an opaque orange-red column
                let additive = def.as_ref().map_or(textured, |d| d.blend == 0 && (d.kind & 0x10 != 0 || alpha_tex));
                // alpha-blended fx (lighting on, kind bit 1): the lit vertex colour, emissive + diffuse clamped (fx_white:
                // white; the original's white fade peaks at 252 / 255: intro capture); additive fx keep the diffuse (the keg's trail
                // then shows as the original's pale streak)
                let tint = def.as_ref().map(|d| {
                    if !additive {
                        Color::srgb((d.emissive[0] + d.diffuse[0]).min(1.0), (d.emissive[1] + d.diffuse[1]).min(1.0), (d.emissive[2] + d.diffuse[2]).min(1.0))
                    } else {
                        Color::srgb(d.diffuse[0], d.diffuse[1], d.diffuse[2])
                    }
                });
                // an fx locked to the camera (the intro's white fade fxo_white: a quad 1.26 in front of the eye, near plane
                // 1) is drawn over the scene: the camera path passes within 1.26 of the glacier walls, which showed in
                // front of the white (the player's report, 2026-10-14; the original's capture is all white there)
                let m = ctx.materials.add(StandardMaterial {
                    depth_bias: if locked { 1.0e9 } else { base.depth_bias },
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
    let id = e.id();
    *vm.made.entry(template.to_lowercase()).or_default() += 1;
    let mut o = FxObject::new(template, anim, place, mats, layers);
    o.follow = follows;
    o.body_def = body_def;
    o.follow_pos_only = inst.follow_pos_only;
    o.restored = inst.restored;
    vm.objects.insert(id, o);
    Some(id)
}

/// A child fx on an object (Fx_StartOnObject 0x0041dc80 / Fx_Attach 0x0041dd50): placed at a random point of list1 on
/// the parent and a random point of list2 of its own objects (one rand each when given); detached children (0x2f,
/// 0x30's trails) follow the parent, others are placed once.
pub fn child_instance(vm: &mut FxVm, name: String, script: Vec<Value>, parent: Entity, follow: bool, l1: Option<&Vec<String>>, l2: Option<&Vec<String>>) -> Instance {
    let parent_point = pick_name(&mut vm.rng, l1);
    let child_point = pick_name(&mut vm.rng, l2);
    let place = vm.objects.get(&parent).map(|o| o.place).unwrap_or(Mat4::IDENTITY);
    Instance { parent: Some(parent), follow, parent_point, child_point, ..Instance::new(name, script, place) }
}

/// Run one instance from its instruction pointer until end (true), pause or wait (false).
pub fn run_script(vm: &mut FxVm, ctx: &mut Ctx<'_, '_, '_>, idx: usize, spawned: &mut Vec<Instance>) -> bool {
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
        let w = |i: usize| f(&a.get(i).cloned().unwrap_or(Value::Null));
        let top = stack.last().copied();
        // the object setters reach: the pushed top, when there is a current object
        let target = if cur.is_some() { top } else { None };
        macro_rules! obj {
            () => {
                target.and_then(|t| vm.objects.get_mut(&t))
            };
        }
        macro_rules! emitter {
            () => {
                obj!().and_then(|o| o.emitters.last_mut())
            };
        }
        match op.as_str() {
            "0x04" => return true,
            // 0x05 create, 0x37 create and clear list1 (0x004234d0)
            "0x05" | "0x37" => {
                cur = create_object(vm, ctx, idx, arg(1).as_str().unwrap_or(""), None);
                if op == "0x37" {
                    list1 = None;
                }
            }
            // 0x13 Fx_AttachMesh 0x00423680: on the fx's owner at a point of list1, following it. The original needs an
            // owner (fx+0xb4); the remaster's callers may start these without one: placed at the fx then (engine)
            "0x13" => {
                let owner = vm.instances[idx].parent;
                let attach = owner.map(|o| {
                    let point = pick_name(&mut vm.rng, list1.as_ref());
                    let offset = match vm.objects.get(&o) {
                        Some(p) => {
                            let anim = p.anim.clone();
                            let (pl, s) = (p.place, p.scale_now);
                            let pt = point.as_deref().and_then(|n| vm.attach.point(anim.as_deref(), n));
                            pl.inverse() * point_world(pl, s, pt)
                        }
                        None => Mat4::IDENTITY,
                    };
                    (o, offset)
                });
                list1 = None;
                cur = create_object(vm, ctx, idx, arg(1).as_str().unwrap_or(""), attach);
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
            // Fx_SetLifespan 0x0041d110 (max < min: max = min)
            "0x08" => {
                let (lo, hi) = (w(1), w(2));
                let v = vm.rng.r(lo, hi.max(lo));
                if let Some(o) = obj!() {
                    o.lifespan = Some(v);
                    o.age = 0.0;
                }
            }
            // Fx_SetFadeIn 0x0041dba0, then vt+0x100(0): invisible until the fade starts
            "0x09" => {
                let s = vm.rng.r(w(1), w(2));
                let e = vm.rng.r(w(3), w(4));
                if let Some(o) = obj!() {
                    o.fade_in = Some((s, 1.0 / (e - s).max(1e-4), true));
                    o.set_alpha(ctx.materials, 0.0);
                }
            }
            // Fx_SetFadeOut 0x0041dc30: the fade-out start age
            "0x0a" => {
                let v = vm.rng.r(w(1), w(2));
                if let Some(o) = obj!() {
                    o.fade_out = Some(v);
                }
            }
            // 0x0b: Fx_SetScaleLinear 0x0041d170 with from = to: a constant scale
            "0x0b" => {
                let v = vm.rng.r(w(1), w(2));
                if let Some(o) = obj!() {
                    o.scale = Some(ScaleCurve::new(v, v, 0));
                }
            }
            // Fx_SetScaleLinear / Exp / Log 0x0041d170 / 0x0041d210 / 0x0041d2c0
            "0x0c" | "0x0d" | "0x0e" => {
                let from = vm.rng.r(w(1), w(2));
                let to = vm.rng.r(w(3), w(4));
                let mode = match op.as_str() {
                    "0x0c" => 0,
                    "0x0d" => 1,
                    _ => 2,
                };
                if let Some(o) = obj!() {
                    o.scale = Some(ScaleCurve::new(from, to, mode));
                }
            }
            // Fx_Attach 0x0041dd50: a child fx on the pushed object (0x0f placed once, 0x2f following); both lists cleared
            "0x0f" | "0x2f" => {
                let name = arg(1).as_str().unwrap_or("").to_lowercase();
                if let (Some(_), Some(t)) = (cur, top) {
                    if let Some(script) = ctx.data.fx.get(&name).cloned() {
                        let child = child_instance(vm, name.clone(), script, t, op == "0x2f", list1.as_ref(), list2.as_ref());
                        spawned.push(child);
                    }
                }
                list1 = None;
                list2 = None;
            }
            "0x104" => {
                let l = names(&arg(1));
                if list1.is_none() {
                    list1 = Some(l);
                } else {
                    list2 = Some(l);
                }
            }
            // 0x10 (0x00421379) impulse / 0x1e drift: s = r(w0, w1) always; if cur: a reference point from list1 (one
            // rand when set); Fx_SetImpulse 0x0041e0c0 / Fx_SetDrift 0x0041dfb0: zero s or no push: nothing; the force is
            // minus column 1 (impulse) or column 0 (drift) of the point's matrix (0x00453c50) or the object's own
            // (0x00453150 / 0x004531b0) times s, at its centre (the point's translation in the original: the engine
            // pushes at the centre). The impulse waits for the body when there is none yet (0x11 follows in the scripts)
            "0x10" | "0x1e" => {
                let s = vm.rng.r(w(1), w(2));
                if cur.is_some() {
                    let reference = pick_name(&mut vm.rng, list1.as_ref());
                    if let Some(t) = top {
                        if let Some(o) = vm.objects.get(&t) {
                            let (pl, sc, anim) = (o.place, o.scale_now, o.anim.clone());
                            let pt = reference.as_deref().and_then(|n| vm.attach.point(anim.as_deref(), n));
                            let m = point_world(pl, sc, pt);
                            let axis = if op == "0x10" { -m.y_axis.truncate() } else { m.x_axis.truncate() };
                            let o = vm.objects.get_mut(&t).unwrap();
                            if s != 0.0 {
                                o.impulse += axis * s;
                                if let Some(id) = o.body {
                                    add_impulse(ctx.physics, id, std::mem::take(&mut o.impulse));
                                }
                            }
                        }
                    }
                }
                list1 = None;
            }
            // Fx_SetGravity 0x0041de10: the gravity scale; the body made dynamic (Object_setPhysicsEnabled(obj, 0))
            "0x11" => {
                let g = vm.rng.r(w(1), w(2));
                if let Some(t) = target {
                    if let Some(o) = vm.objects.get_mut(&t) {
                        make_dynamic(ctx.physics, ctx.commands, o, t, g);
                    }
                }
            }
            // Fx_Pause 0x0041dea0: only with an empty push stack; the run stops in every case
            "0x12" => {
                let t = vm.rng.r(w(1), w(2));
                if stack.is_empty() {
                    vm.instances[idx].paused = t;
                }
                return false;
            }
            "0x14" => {
                if let Some(t) = target {
                    vm.instances[idx].logged.push(t);
                }
            }
            // Fx_Wait 0x0041dee0
            "0x15" => {
                if stack.is_empty() {
                    vm.instances[idx].waiting = true;
                }
                return false;
            }
            // Fx_SetInvisible 0x0041e280: the mesh is not drawn; emitters, physics and lifespan still run
            "0x16" => {
                if let Some(t) = target {
                    ctx.commands.entity(t).insert(Visibility::Hidden);
                }
            }
            // 0x17 / 0x18 / 0x19 / 0x30: Fx_Emit 0x0041e4d0 (FxObject_AllocEmitter 0x0041e420, 10 per object): an fx `s`
            // spawned every r(w0, w1) s: 0x17 along the object's path (0x80), 0x18 at a point of list1 (0x40), 0x19 at a
            // point of list1 with its motion interpolated (2; list2 the spawned object's point), 0x30 as a child following
            // the object (1, at points of list1 / list2). 0x36 Fx_EmitRandom 0x0041e610: as 0x19 with a random rotation
            // in the box w2..w7 (object flag 0x4000)
            "0x17" | "0x18" | "0x19" | "0x30" | "0x36" => {
                let name = arg(1).as_str().unwrap_or("").to_lowercase();
                let interval = vm.rng.r(w(2), w(3));
                let (l1, l2) = (list1.take(), list2.take());
                if let Some(t) = target {
                    let mode = match op.as_str() {
                        "0x17" => EmitMode::Along,
                        "0x18" => EmitMode::Points,
                        "0x30" => EmitMode::Children,
                        _ => EmitMode::Path,
                    };
                    let random_rot = (op == "0x36").then(|| (Vec3::new(w(4), w(6), w(8)), Vec3::new(w(5), w(7), w(9))));
                    let fx_ok = ctx.data.fx.contains_key(&name);
                    let at_setup = matches!(mode, EmitMode::Points | EmitMode::Path).then(|| pick_name(&mut vm.rng, l1.as_ref()));
                    if let Some(o) = vm.objects.get(&t) {
                        let (pl, sc, anim) = (o.place, o.scale_now, o.anim.clone());
                        let start = point_world(pl, sc, at_setup.flatten().as_deref().and_then(|n| vm.attach.point(anim.as_deref(), n)));
                        let o = vm.objects.get_mut(&t).unwrap();
                        if fx_ok && o.mesh_emitters.len() < 10 {
                            o.mesh_emitters.push(MeshEmitter::new(name, interval, mode, l1.unwrap_or_default(), l2.unwrap_or_default(), start, random_rot));
                        }
                    }
                }
            }
            // 0x1a Fx_RotateObject 0x0041f8f0: r() x3, a spin (body.rs)
            "0x1a" => {
                let v = Vec3::new(vm.rng.r(w(1), w(2)), vm.rng.r(w(3), w(4)), vm.rng.r(w(5), w(6)));
                if let Some(o) = obj!() {
                    match o.body {
                        Some(id) => add_spin(ctx.physics, id, v),
                        None => o.spin += v,
                    }
                }
            }
            // 0x1b: an fx at (r, r, r) in the world (always, Fx_Create flags 0x28)
            "0x1b" => {
                let p = Vec3::new(vm.rng.r(w(2), w(3)), vm.rng.r(w(4), w(5)), vm.rng.r(w(6), w(7)));
                if let Some((n, script)) = find(ctx.data, arg(1).as_str().unwrap_or("")) {
                    spawned.push(Instance::new(n, script, Mat4::from_translation(p)));
                }
            }
            // 0x78: an fx at the current object's position (flags 0x28)
            "0x78" => {
                if let Some(o) = cur.and_then(|c| vm.objects.get(&c)) {
                    let p = o.place.w_axis.truncate();
                    if let Some((n, script)) = find(ctx.data, arg(1).as_str().unwrap_or("")) {
                        spawned.push(Instance::new(n, script, Mat4::from_translation(p)));
                    }
                }
            }
            // Fx_SetHeightOffset 0x0041f970: z += offset, once
            "0x1c" => {
                let v = vm.rng.r(w(1), w(2));
                if let Some(o) = obj!() {
                    o.place.w_axis.z += v;
                }
            }
            // 0x1d Fx_SetOnDeath 0x0041fd10: the fx started where the object ends (0x00419290 with al = 1)
            "0x1d" => {
                let name = arg(1).as_str().unwrap_or("").to_lowercase();
                if ctx.data.fx.contains_key(&name) {
                    if let Some(o) = obj!() {
                        o.on_death = Some(name);
                    }
                }
            }
            // particle emitter setters on the current emitter (FxObject_CurrentParticleEmitter 0x0041e730): blur
            // 0x0041e9f0, size 0x0041ea40, scale (growth) 0x0041ea90, damping 0x0041f340, speed 0x0041ee50, lifespan
            // 0x0041eae0, cone angle 0x0041f2f0, gravity 0x0041eea0
            "0x20" | "0x21" | "0x22" | "0x23" | "0x24" | "0x25" | "0x26" | "0x48" => {
                let (lo, hi) = (w(1), w(2));
                if let Some(e) = emitter!() {
                    match op.as_str() {
                        "0x20" => e.stretch = (lo, hi),
                        "0x21" => e.size = (lo, hi),
                        "0x22" => e.growth = (lo, hi),
                        // Fx_SetParticleDamping 0x0041f340: a clamped below to 1e-6 ([0x005bcc68])
                        "0x23" => e.drag = (lo.max(1e-6), hi),
                        "0x24" => e.speed = (lo, hi),
                        "0x25" => e.life = (lo, hi),
                        "0x26" => e.cone = (lo, hi),
                        _ => e.gravity = (lo, hi),
                    }
                }
            }
            // 0x27 Fx_EmitParticle 0x0041e760 (flags 0x20): an emitter following the object; the interval drawn always
            "0x27" => {
                let pool = arg(1).as_i64().unwrap_or(0).clamp(0, 2) as usize;
                let interval = vm.rng.r(w(2), w(3));
                if let Some(o) = obj!() {
                    if o.emitters.len() < 10 {
                        // direction: -column 1 of the object's matrix (0x00453150), origin its position
                        let dir = -o.place.y_axis.truncate();
                        o.emitters.push(Emitter::new(pool, interval, dir, o.place.w_axis.truncate()));
                    }
                }
            }
            // 0x2a Fx_StopEmitter 0x0041e3b0: the last active mesh emitter stops after `value` seconds
            "0x2a" => {
                let v = vm.rng.r(w(1), w(2));
                if let Some(e) = obj!().and_then(|o| o.mesh_emitters.last_mut()) {
                    e.timeout = Some(v);
                }
            }
            // Fx_SetParticleUvs 0x0041f3b0
            "0x2b" => {
                let uv = [w(1), w(2), w(3), w(4)];
                if let Some(e) = emitter!() {
                    e.uv = uv;
                }
            }
            // Fx_SetParticleColour 0x0041f410: alpha (unused by the draw), r, g, b (0..255)
            "0x2c" => {
                let c = Vec3::new(w(2), w(3), w(4)) / 255.0;
                if let Some(e) = emitter!() {
                    e.colour = c;
                }
            }
            // Fx_SetKite 0x0041fd70 (0x400): a camera card
            "0x2e" => {
                if let Some(o) = obj!() {
                    o.kite = true;
                }
            }
            // Fx_SetInitialRotation 0x0041f850: a1 = r(w0, w1), a2 = r(w2, w3), a3 = r(w4, w5) always; X = W R
            // (Matrix_FromEulerDeg 0x004e3db0(a1, a3, a2), Matrix_Multiply 0x004e2910), Object_SetTransform 0x00457990
            "0x31" => {
                let a1 = vm.rng.r(w(1), w(2));
                let a2 = vm.rng.r(w(3), w(4));
                let a3 = vm.rng.r(w(5), w(6));
                if let Some(o) = obj!() {
                    o.place *= Mat4::from_mat3(euler_deg(a1, a3, a2));
                    if let Some(id) = o.body {
                        set_rotation(ctx.physics, id, o.place);
                    }
                }
            }
            // hit ground (0x32 via 0x0041faf0, 0x3d Fx_SetHitGround 0x0041f9d0, 0x3c 0x0041fa60: collision mask bits,
            // stored), on hit death (0x34, 0x0041fbd0), on hit delete (0x33, 0x0041fc20), on hit create (0x35,
            // 0x0041fc70: fx `s`, at most w0 times, w1 s apart), on hit area damage (0x76, 0x0041fb00); each wakes the
            // object (0x004577b0)
            "0x32" | "0x3c" | "0x3d" | "0x33" | "0x34" | "0x35" | "0x76" => {
                let fx_name = arg(1).as_str().map(str::to_lowercase);
                if let Some(o) = obj!() {
                    o.hit.ground = true;
                    match op.as_str() {
                        "0x33" => o.hit.delete = true,
                        "0x34" => o.hit.death = true,
                        "0x35" => {
                            if let Some(n) = fx_name.filter(|n| ctx.data.fx.contains_key(n)) {
                                o.hit.create = Some(HitCreate { fx: n, count: w(2) as i32, spacing: w(3), last: 0.0 });
                            }
                        }
                        "0x76" => {
                            // +0x540 = a0 (the impact force), +0x530 = a1 (the damage), +0x534 / +0x538 inner / outer
                            o.hit.aoe = Some((w(1), w(2), w(3), w(4)));
                        }
                        _ => {}
                    }
                    if let Some(id) = o.body {
                        wake(ctx.physics, id);
                    }
                }
            }
            // 0x38 / 0x39 / 0x3a: a random name from the inline list (one rand always), the looping word; Fx_PlaySound
            // 0x00420080: 0x38 in 2D (list1 kept); 0x39 / 0x3a with cur: at a point of list1 (one rand when set) of the
            // object (0x3a in 2D on the player: audio.rs ListenerBody); list1 cleared
            "0x38" | "0x39" | "0x3a" => {
                let l = names(&arg(1));
                let pick = if l.is_empty() { None } else { l.get(vm.rng.index(l.len())).cloned() };
                let looping = arg(2).as_i64().unwrap_or(0) != 0;
                if op == "0x38" {
                    if let Some(name) = pick {
                        vm.out.sounds.push(crate::audio::PlaySound::ui(name).looping(looping));
                    }
                } else {
                    if cur.is_some() {
                        let point = pick_name(&mut vm.rng, list1.as_ref());
                        if let (Some(name), Some(t)) = (pick, top) {
                            let at = vm.objects.get(&t).map(|o| (o.place, o.scale_now, o.anim.clone())).map(|(pl, sc, anim)| {
                                let pt = point.as_deref().and_then(|n| vm.attach.point(anim.as_deref(), n));
                                game_to_engine() * point_world(pl, sc, pt).w_axis.truncate()
                            });
                            vm.out.sounds.push(crate::audio::PlaySound { at, ..crate::audio::PlaySound::on(name, t).looping(looping) });
                        }
                    }
                    list1 = None;
                }
            }
            // FxScreenShake_Set 0x004201f0: time, radius, amplitude (object.rs fires it)
            "0x3b" => {
                if let Some(o) = obj!() {
                    o.shake = Some((w(1), w(2), w(3)));
                }
            }
            // 0x3f (0x00422407, no cur test): Fx_PlayForce 0x0041ff60: min and max <= 0 and not looping: a one-shot rumble
            // whatever top is; else nothing without top. Queued only (input.rs owns the remaster's rumble)
            "0x3f" => {
                let name = arg(1).as_str().unwrap_or("").to_owned();
                let (looping, player_only, min, max) = (w(2) != 0.0, w(3) != 0.0, w(4), w(5));
                let one_shot = !(min > 0.0) && !(max > 0.0) && !looping;
                if one_shot || top.is_some() {
                    vm.out.forces.push(FxForce { name, looping, player_only, min, max, owner: if one_shot { None } else { top } });
                }
            }
            // Fx_SetUvScroll 0x00420670: layer, angle (deg), speed, duration, ramp in, ramp out (the ramps only with a
            // duration); direction from the sine table (cos, sin of the angle: guess at the table's phase)
            "0x40" => {
                let layer = arg(1).as_i64().unwrap_or(1);
                let (ang, speed, dur, rin, rout) = (w(2).to_radians(), w(3), w(4), w(5), w(6));
                if let Some(o) = obj!() {
                    if let Some(l) = o.uv_layer(layer) {
                        let (rin, rout) = if dur > 0.0 { (rin, rout) } else { (0.0, 0.0) };
                        l.scroll = Some(UvScroll { dir: Vec2::new(ang.cos(), ang.sin()), speed, end: dur, ramp_in: rin, ramp_out: rout });
                    }
                }
            }
            // 0x41 .. 0x44 Fx_SetUvScale 0x004202b0: layer, from (u, v), to (u, v), duration; mode 0 a random scale in
            // the box (0x41), 1 linear (0x42), 3 eased (0x43), 2 exp (0x44)
            "0x41" | "0x42" | "0x43" | "0x44" => {
                let layer = arg(1).as_i64().unwrap_or(1);
                let (from, to, dur) = if op == "0x41" { (Vec2::new(w(2), w(4)), Vec2::new(w(3), w(5)), 0.0) } else { (Vec2::new(w(2), w(3)), Vec2::new(w(4), w(5)), w(6)) };
                let mode = match op.as_str() {
                    "0x41" => 0,
                    "0x42" => 1,
                    "0x43" => 3,
                    _ => 2,
                };
                // mode 0 draws its scale now (rand per axis)
                let random = (mode == 0).then(|| Vec2::new(from.x + vm.rng.rand() as f32 * (to.x - from.x) / 32768.0, from.y + vm.rng.rand() as f32 * (to.y - from.y) / 32768.0));
                if let Some(o) = obj!() {
                    if let Some(l) = o.uv_layer(layer) {
                        l.scale = Some(UvScale { mode, from, to, duration: dur, fixed: random });
                    }
                }
            }
            // 0x45 Fx_SetUvRotate 0x00420450: layer, speed (deg/s), end, fade-in end, fade-out length
            "0x45" => {
                let layer = arg(1).as_i64().unwrap_or(1);
                let (speed, end, fin, fout) = (w(2), w(3), w(4), w(5));
                if let Some(o) = obj!() {
                    if let Some(l) = o.uv_layer(layer) {
                        l.rotate = Some(UvRotate { speed, end, ramp_in: fin, ramp_out: if end > 0.0 { fout } else { 0.0 } });
                    }
                }
            }
            // Fx_SetParticleAxis / AxisRelative 0x0041eef0 / 0x0041f0f0: M = Rx Ry Rz (degrees); the original's
            // v * (Rx Ry Rz) with D3D row matrices is (Rz Ry Rx) v with column matrices ((0, -1, 0) by (-90, 0, 0) gives
            // +z: particles go up). 0x46 turns the direction once and fixes it; 0x47 re-applies M every step
            "0x46" | "0x47" => {
                let (x, y, z) = (w(1).to_radians(), w(2).to_radians(), w(3).to_radians());
                if let Some(e) = emitter!() {
                    let m = Mat3::from_rotation_z(z) * Mat3::from_rotation_y(y) * Mat3::from_rotation_x(x);
                    e.dir = m * e.dir;
                    e.fixed_dir = op == "0x46";
                    e.rel = (op == "0x47").then_some(m);
                }
            }
            "0x49" => {
                let (in0, in1, out0) = (w(1), w(2), w(3));
                if let Some(e) = emitter!() {
                    e.ramp = Some(build_ramp(in0, in1, out0, e.life));
                }
            }
            // 0x4a Fx_SetLight 0x0041da90: a point light (diffuse rgb / 255, attenuation 1 + 0.1 d) at a point of list1
            // (one rand when set, with a push); list1 cleared. 0x58 / 0x59 Fx_SetLightSpecular / Ambient 0x0041d8f0 /
            // 0x0041d980 and 0x5a Fx_SetLightHalfRange 0x0041da10 (attenuation1 = 1 / half range, 0 for 0) change it
            "0x4a" => {
                let colour = Vec3::new(w(1), w(2), w(3)) / 255.0;
                if let Some(t) = target {
                    let point = pick_name(&mut vm.rng, list1.as_ref());
                    if let Some(o) = vm.objects.get_mut(&t) {
                        o.light = Some(LightDef { diffuse: colour, specular: Vec3::ZERO, ambient: Vec3::ZERO, attenuation1: 0.1, point });
                    }
                }
                list1 = None;
            }
            "0x58" | "0x59" | "0x5a" => {
                let c = Vec3::new(w(1), w(2), w(3)) / 255.0;
                let half = w(1);
                if let Some(l) = obj!().and_then(|o| o.light.as_mut()) {
                    match op.as_str() {
                        "0x58" => l.specular = c,
                        "0x59" => l.ambient = c,
                        _ => l.attenuation1 = if half == 0.0 { 0.0 } else { 1.0 / half },
                    }
                }
            }
            // Fx_SetTextureAnimation 0x0041e310: layer, frames, rate (frames/s), loop; the string operand is not read
            "0x63" => {
                let (frames, rate, looping) = (arg(2).as_i64().unwrap_or(1) as u32, w(3), arg(4).as_i64().unwrap_or(0) != 0);
                if let Some(o) = obj!() {
                    let base = o.materials.first().and_then(|m| ctx.materials.get(m)).and_then(|m| m.base_color_texture.as_ref()).and_then(|h| h.path()).map(|p| p.path().file_stem().unwrap().to_string_lossy().into_owned()).unwrap_or_default();
                    o.texanim = Some(super::object::TexAnim { rate, frames: frames.max(1), looping, acc: 0.0, frame: 0, base });
                }
            }
            // Fx_SetAbsoluteImpulse 0x0041e1e0: r() x3 always; the body made dynamic and pushed (world frame)
            "0x64" => {
                let v = Vec3::new(vm.rng.r(w(1), w(2)), vm.rng.r(w(3), w(4)), vm.rng.r(w(5), w(6)));
                if let Some(t) = target {
                    if let Some(o) = vm.objects.get_mut(&t) {
                        if let Some(id) = make_dynamic(ctx.physics, ctx.commands, o, t, 1.0) {
                            add_impulse(ctx.physics, id, v);
                        }
                    }
                }
            }
            // 0x65: the object's position and a texture looked up, the result unused (nothing to do)
            "0x65" => {}
            // 0x4d / 0x4e Fx_AddStreamer 0x0041f500: a streamer on the object (8 at most), 0x4e at a point of list1
            "0x4d" | "0x4e" => {
                if let (Some(_), Some(t)) = (cur, top) {
                    let point = if op == "0x4e" { pick_name(&mut vm.rng, list1.as_ref()) } else { None };
                    let anim = vm.objects.get(&t).and_then(|o| o.anim.clone());
                    let pt = point.as_deref().and_then(|n| vm.attach.point(anim.as_deref(), n));
                    if vm.streamers.iter().filter(|s| s.owner == t).count() < 8 {
                        vm.streamers.push(super::streamers::Streamer::new(t, pt));
                    }
                }
                if op == "0x4e" {
                    list1 = None;
                }
            }
            // the current streamer's settings (FxObject_CurrentStreamer 0x0041f560: the last of the object's)
            "0x4f" | "0x50" | "0x51" | "0x52" | "0x53" | "0x54" | "0x55" | "0x56" | "0x57" => {
                if let (Some(_), Some(t)) = (cur, top) {
                    if let Some(st) = vm.streamers.iter_mut().rev().find(|s| s.owner == t) {
                        st.set(&op, &arg(1), [w(1), w(2), w(3), w(4)]);
                    }
                }
            }
            // post-process effects on the object (PostEffects, Game+0xa9c; obj+0x634 = the object as the effect id):
            // 0x5b colour (0x0047be00), 0x5c blur (0x0047be60), 0x70 screen texture (0x0047bec0), 0x5d / 0x71 rings
            // (0x0047bf80: 0x71 always), 0x77 fade (0x0047bda0: colour w0..w2, alpha w3), 0x5e phase durations
            // (0x0047c1a0), 0x5f distance fade (0x0047c0a0), 0x61 edge fade (0x0047c140), 0x60 release (0x0041bd30 /
            // 0x0047bfe0)
            "0x5b" | "0x5c" | "0x5d" | "0x5e" | "0x5f" | "0x60" | "0x61" | "0x70" | "0x71" | "0x77" => {
                if let Some(t) = target.filter(|t| vm.objects.contains_key(t)) {
                    let id = t.to_bits();
                    let req = match op.as_str() {
                        "0x5b" => Some(ScreenRequest::Start(id, ScreenKind::Colour([w(1), w(2), w(3), w(4)]))),
                        "0x5c" => Some(ScreenRequest::Start(id, ScreenKind::Blur(w(1)))),
                        "0x70" => Some(ScreenRequest::Start(id, ScreenKind::Overlay(w(1)))),
                        "0x5d" | "0x71" => Some(ScreenRequest::Start(id, ScreenKind::OverlayRings([0.5, 0.5, w(1), w(2), 0.5, 0.5, w(3), w(4)]))),
                        "0x77" => Some(ScreenRequest::Start(id, ScreenKind::Fade(Vec3::new(w(1), w(2), w(3)) / 255.0, w(4)))),
                        "0x5e" => Some(ScreenRequest::Phases(id, w(1), w(2), w(3))),
                        "0x5f" => Some(ScreenRequest::DistanceFade(id, w(1), w(2))),
                        "0x61" => Some(ScreenRequest::EdgeFade(id, w(1), w(2))),
                        _ => Some(ScreenRequest::Stop(id)),
                    };
                    if let Some(r) = req {
                        if matches!(r, ScreenRequest::Start(..)) {
                            if let Some(o) = vm.objects.get_mut(&t) {
                                o.screen = true;
                            }
                        }
                        vm.out.screen.push(r);
                    }
                }
            }
            // 0x73 / 0x74 Camera_StartPath 0x0040ff50 mode 1 / 2 (not guarded by the object; weather.md)
            "0x73" | "0x74" => {
                if let Some(name) = arg(1).as_str() {
                    vm.out.camera_paths.push(name.to_owned());
                }
            }
            // 0x6a: a decal (Decals_Spawn 0x004165c0) in the box of +-size around the object (decals.rs)
            "0x6a" => {
                if let (Some(_), Some(t)) = (cur, top) {
                    if let (Some(o), Some(tex)) = (vm.objects.get(&t), arg(2).as_str()) {
                        vm.out.decals.push(crate::decals::SpawnDecal { place: o.place, size: w(1), texture: tex.to_owned() });
                    }
                }
            }
            // 0x79 Fx_SetInert 0x0041e230 (+0x424 |= 0x20000): no collision shape, never in the physics world
            "0x79" => {
                if let Some(o) = obj!() {
                    o.inert = true;
                }
            }
            other => {
                vm.unported.insert(other.to_owned());
            }
        }
    }
}

/// Runs every live instance (Fx_UpdateAll 0x004237b0 -> Fx_Step 0x00423360 per instance): waiting ones are released
/// when their owner is gone, paused ones count down (resuming in the frame the timer runs out), the rest run.
#[allow(clippy::too_many_arguments)]
pub fn run(
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
    cams: Query<Entity, With<Camera3d>>,
    alive: Query<()>,
) {
    let t0 = std::time::Instant::now();
    let dt = time.delta_secs();
    let vm = &mut *vm;
    vm.time += dt;
    let mut ctx = Ctx { commands: &mut commands, data: &data, lib: &mut lib, assets: &assets, meshes: &mut meshes, materials: &mut materials, layer_materials: &mut layer_materials, physics: &mut physics, camera: cams.iter().next() };
    let mut i = 0;
    while i < vm.instances.len() {
        let inst = &mut vm.instances[i];
        if inst.waiting {
            // the owner of a waiting fx releases it when it goes (Fx_Release 0x0041d030 from the owner; the engine:
            // its parent entity is gone)
            if inst.parent.is_some_and(|p| !vm.objects.contains_key(&p) && alive.get(p).is_err()) {
                release(vm, &mut ctx, i);
                vm.instances.swap_remove(i);
                continue;
            }
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
        if inst.ip == 0 {
            let n = inst.name.clone();
            *vm.started.entry(n).or_default() += 1;
        }
        let mut spawned = Vec::new();
        let done = run_script(vm, &mut ctx, i, &mut spawned);
        // 0x0f children run at once (nested Fx_Step), 0x2f children too (appended to the list being walked)
        vm.instances.extend(spawned);
        if done {
            // Fx_Step: an ended instance is released (Fx_Release 0x0041d030), its 0x14 objects with it
            release(vm, &mut ctx, i);
            vm.instances.swap_remove(i);
        } else {
            i += 1;
        }
    }
    vm.stats.instances = vm.instances.len();
    vm.stats.ms = t0.elapsed().as_secs_f32() * 1000.0;
}

/// Fx_Release 0x0041d030 with fx+0x14 bit 0x20: FxInstance_DestroyEmitters 0x0041cfb0 ends each logged object
/// (0x00419290 with al = 1: its on-death fx runs).
fn release(vm: &mut FxVm, ctx: &mut Ctx<'_, '_, '_>, idx: usize) {
    for e in std::mem::take(&mut vm.instances[idx].logged) {
        if let Some(o) = vm.objects.remove(&e) {
            if let Some(next) = super::object::end_object(&o, ctx.data, ctx.physics, ctx.commands, e, true) {
                vm.instances.push(next);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn euler_matches_rows() {
        // 180 degrees about the object's z (fx_breath_f: (0, 0, 0, 0, 180, 180) -> a1 = 0, a3 = 180, a2 = 0)
        let m = euler_deg(0.0, 0.0, 180.0);
        assert!((m * Vec3::X - Vec3::NEG_X).length() < 1e-5);
        // A turns about x: y -> z
        let m = euler_deg(90.0, 0.0, 0.0);
        assert!((m * Vec3::Y - Vec3::Z).length() < 1e-5);
    }

    #[test]
    fn pick_name_draws_once_and_null_is_none() {
        let l = vec!["null".to_owned()];
        let mut r = CrtRand::seeded(1);
        assert_eq!(pick_name(&mut r, Some(&l)), None);
        assert_eq!(r.rand(), { let mut q = CrtRand::seeded(1); q.rand(); q.rand() });
        let mut r = CrtRand::seeded(1);
        assert_eq!(pick_name(&mut r, None), None);
        assert_eq!(r.rand(), CrtRand::seeded(1).rand());
        let l = vec!["a".to_owned(), "b".to_owned(), "c".to_owned()];
        let mut r = CrtRand::seeded(1);
        // rand 41 -> index 0
        assert_eq!(pick_name(&mut r, Some(&l)).as_deref(), Some("a"));
    }
}
