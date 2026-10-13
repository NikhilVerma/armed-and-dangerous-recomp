//! Skinned characters and their animations, from tools/remaster_export_anim.py (docs/formats/big-anim.md):
//! skeletons/<name>.json (bones with local rest transforms, column-major inverse bind matrices) and
//! anims/<name>.json (per-bone local tracks: t / r (x,y,z,w) / s keyed in seconds, sampled with the game's own
//! interpolation at every frame, so linear / slerp between samples is exact enough). Game space throughout: the
//! joint entities hang under the character entity, whose transform turns game axes into engine axes.
//! Bone names match case-insensitively. Game-neutral apart from the upper/lower body split rule (a layer's mask).

use bevy::mesh::skinning::{SkinnedMesh, SkinnedMeshInverseBindposes};
use bevy::prelude::*;
use serde::Deserialize;
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;

#[derive(Deserialize)]
struct BoneDef {
    name: String,
    parent: i32,
    t: [f32; 3],
    r: [f32; 4],
    s: [f32; 3],
}

#[derive(Deserialize)]
struct SkeletonFile {
    bones: Vec<BoneDef>,
    inverse_bind: Vec<[f32; 16]>,
}

pub struct Skeleton {
    pub names: Vec<String>,
    pub parents: Vec<i32>,
    pub rest: Vec<Transform>,
    pub inverse_bind: Handle<SkinnedMeshInverseBindposes>,
    pub index: HashMap<String, usize>,
}

#[derive(Deserialize)]
struct AnimFile {
    duration: f32,
    tracks: HashMap<String, TrackDef>,
    #[serde(default)]
    notes: Vec<(f32, String)>,
}

/// A note track event reached by a playing anim (FX / fx_material / fire / ...): docs/formats/big-anim.md.
#[derive(Message, Clone)]
pub struct AnimNote {
    pub entity: Entity,
    pub text: String,
}

#[derive(Deserialize, Default)]
struct TrackDef {
    #[serde(default)]
    t: Vec<[f32; 4]>,
    #[serde(default)]
    r: Vec<[f32; 5]>,
    #[serde(default)]
    s: Vec<[f32; 4]>,
}

pub struct Anim {
    pub duration: f32,
    tracks: HashMap<String, TrackDef>,
    pub notes: Vec<(f32, String)>,
}

#[derive(Resource, Default)]
pub struct AnimLibrary {
    pub dir: PathBuf,
    skeletons: HashMap<String, Arc<Skeleton>>,
    anims: HashMap<String, Option<Arc<Anim>>>,
}

impl AnimLibrary {
    pub fn skeleton(&mut self, name: &str, binds: &mut Assets<SkinnedMeshInverseBindposes>) -> Option<Arc<Skeleton>> {
        if let Some(s) = self.skeletons.get(name) {
            return Some(s.clone());
        }
        let text = std::fs::read_to_string(self.dir.join("skeletons").join(format!("{name}.json"))).ok()?;
        let f: SkeletonFile = serde_json::from_str(&text).ok()?;
        let rest = f
            .bones
            .iter()
            .map(|b| Transform { translation: Vec3::from(b.t), rotation: Quat::from_array(b.r).normalize(), scale: Vec3::from(b.s) })
            .collect();
        let inv: Vec<Mat4> = f.inverse_bind.iter().map(Mat4::from_cols_array).collect();
        let s = Arc::new(Skeleton {
            names: f.bones.iter().map(|b| b.name.to_lowercase()).collect(),
            parents: f.bones.iter().map(|b| b.parent).collect(),
            rest,
            inverse_bind: binds.add(SkinnedMeshInverseBindposes::from(inv)),
            index: f.bones.iter().enumerate().map(|(i, b)| (b.name.to_lowercase(), i)).collect(),
        });
        self.skeletons.insert(name.to_owned(), s.clone());
        Some(s)
    }

    pub fn anim(&mut self, name: &str) -> Option<Arc<Anim>> {
        let key = name.to_lowercase();
        if let Some(a) = self.anims.get(&key) {
            return a.clone();
        }
        let a = std::fs::read_to_string(self.dir.join("anims").join(format!("{key}.json")))
            .ok()
            .and_then(|t| serde_json::from_str::<AnimFile>(&t).ok())
            .map(|f| Arc::new(Anim { duration: f.duration.max(1e-3), tracks: f.tracks.into_iter().map(|(k, v)| (k.to_lowercase(), v)).collect(), notes: f.notes }));
        if a.is_none() {
            warn!("anim {key} not found");
        }
        self.anims.insert(key, a.clone());
        a
    }
}

fn sample3(keys: &[[f32; 4]], t: f32) -> Option<Vec3> {
    let (a, b, u) = bracket(keys.len(), |i| keys[i][0], t)?;
    let va = Vec3::new(keys[a][1], keys[a][2], keys[a][3]);
    let vb = Vec3::new(keys[b][1], keys[b][2], keys[b][3]);
    Some(va.lerp(vb, u))
}

fn sample_q(keys: &[[f32; 5]], t: f32) -> Option<Quat> {
    let (a, b, u) = bracket(keys.len(), |i| keys[i][0], t)?;
    let qa = Quat::from_xyzw(keys[a][1], keys[a][2], keys[a][3], keys[a][4]);
    let qb = Quat::from_xyzw(keys[b][1], keys[b][2], keys[b][3], keys[b][4]);
    Some(qa.slerp(qb, u).normalize())
}

fn bracket(n: usize, time: impl Fn(usize) -> f32, t: f32) -> Option<(usize, usize, f32)> {
    if n == 0 {
        return None;
    }
    if n == 1 || t <= time(0) {
        return Some((0, 0, 0.0));
    }
    if t >= time(n - 1) {
        return Some((n - 1, n - 1, 0.0));
    }
    // binary search for the last key <= t
    let (mut lo, mut hi) = (0, n - 1);
    while hi - lo > 1 {
        let mid = (lo + hi) / 2;
        if time(mid) <= t {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    let (ta, tb) = (time(lo), time(hi));
    Some((lo, hi, if tb > ta { (t - ta) / (tb - ta) } else { 0.0 }))
}

/// Which bones a layer drives.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Mask {
    All,
    /// the bone named in `Animator::split` and everything below it (upper body)
    Upper,
    /// everything else (lower body)
    Lower,
}

#[derive(Clone, Debug)]
pub struct Layer {
    pub anim: String,
    pub time: f32,
    pub speed: f32,
    pub looping: bool,
    pub mask: Mask,
    /// 1 replaces what the layers below set; less blends toward this layer (lerp / slerp)
    pub weight: f32,
    /// a second anim sampled at the same time and mixed in first: the layer is lerp(anim, pair, pair weight)
    pub pair: Option<(String, f32)>,
}

impl Layer {
    pub fn new(anim: &str, looping: bool) -> Self {
        Layer { anim: anim.to_lowercase(), time: 0.0, speed: 1.0, looping, mask: Mask::All, weight: 1.0, pair: None }
    }
}

/// Plays layers of animation on a skinned character's joints.
#[derive(Component)]
pub struct Animator {
    pub skeleton: Arc<Skeleton>,
    pub joints: Vec<Entity>,
    pub layers: Vec<Layer>,
    /// per bone: below the split bone (upper body)
    pub upper: Vec<bool>,
}

impl Animator {
    /// Plays `anim` on a layer index (0 = base). Restarts it only when the name changes.
    pub fn play(&mut self, slot: usize, anim: &str, looping: bool, mask: Mask) {
        let anim = anim.to_lowercase();
        while self.layers.len() <= slot {
            self.layers.push(Layer::new("", true));
        }
        let l = &mut self.layers[slot];
        if l.anim != anim {
            *l = Layer { anim, time: 0.0, speed: 1.0, looping, mask, weight: 1.0, pair: None };
        } else {
            l.looping = looping;
            l.mask = mask;
        }
    }
    /// Mixes a second anim into a layer (play first): the layer samples lerp(anim, pair, w).
    pub fn set_pair(&mut self, slot: usize, pair: &str, w: f32) {
        if let Some(l) = self.layers.get_mut(slot) {
            l.pair = Some((pair.to_lowercase(), w.clamp(0.0, 1.0)));
        }
    }
    /// The layer's blend weight over the layers below (play first).
    pub fn set_weight(&mut self, slot: usize, weight: f32) {
        if let Some(l) = self.layers.get_mut(slot) {
            l.weight = weight.clamp(0.0, 1.0);
        }
    }
    /// Keeps a layer's time equal to another's (two halves of one blend).
    pub fn sync(&mut self, slot: usize, to: usize) {
        if let Some(t) = self.layers.get(to).map(|l| l.time) {
            if let Some(l) = self.layers.get_mut(slot) {
                l.time = t;
            }
        }
    }
    pub fn stop(&mut self, slot: usize) {
        if let Some(l) = self.layers.get_mut(slot) {
            l.anim.clear();
        }
    }
    /// true when a non-looping layer reached its end
    pub fn finished(&self, slot: usize, lib: &AnimLibrary) -> bool {
        self.layers.get(slot).is_some_and(|l| !l.looping && lib.anims.get(&l.anim).and_then(|a| a.as_ref()).is_some_and(|a| l.time >= a.duration))
    }
}

/// Builds the joint entities of `skeleton` under `parent` (rest pose) and returns the animator.
pub fn build_rig(commands: &mut Commands, parent: Entity, skeleton: Arc<Skeleton>, split: Option<&str>) -> Animator {
    let n = skeleton.names.len();
    let mut joints = Vec::with_capacity(n);
    for i in 0..n {
        let e = commands.spawn((skeleton.rest[i], Visibility::default())).id();
        joints.push(e);
    }
    for i in 0..n {
        let p = skeleton.parents[i];
        let owner = if p >= 0 { joints[p as usize] } else { parent };
        commands.entity(owner).add_child(joints[i]);
    }
    let split_idx = split.and_then(|s| skeleton.index.get(&s.to_lowercase()).copied());
    let upper = (0..n)
        .map(|mut i| loop {
            if Some(i) == split_idx {
                break true;
            }
            let p = skeleton.parents[i];
            if p < 0 {
                break false;
            }
            i = p as usize;
        })
        .collect();
    Animator { skeleton, joints, layers: Vec::new(), upper }
}

pub fn skinned_mesh(a: &Animator) -> SkinnedMesh {
    SkinnedMesh { inverse_bindposes: a.skeleton.inverse_bind.clone(), joints: a.joints.clone() }
}

/// The joint animation (systems that change joints afterwards order after it).
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct AnimateSet;

pub struct SkinPlugin;

impl Plugin for SkinPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<SkinCheck>().add_systems(Update, skin_check.run_if(resource_exists::<crate::playtest::Playtest>));
        app.init_resource::<AnimLibrary>().add_message::<AnimNote>().add_systems(PostUpdate, animate.in_set(AnimateSet).before(TransformSystems::Propagate));
    }
}

fn animate(time: Res<Time>, mut lib: ResMut<AnimLibrary>, mut q: Query<(Entity, &mut Animator)>, mut joints: Query<&mut Transform>, mut notes: MessageWriter<AnimNote>) {
    let dt = time.delta_secs();
    for (e, mut a) in &mut q {
        let a = &mut *a;
        for l in &mut a.layers {
            if l.anim.is_empty() {
                continue;
            }
            let Some(anim) = lib.anim(&l.anim) else { continue };
            let pair = l.pair.as_ref().and_then(|(n, w)| lib.anim(n).map(|a| (a, *w)));
            let prev = if l.looping { l.time.rem_euclid(anim.duration) } else { l.time.min(anim.duration) };
            // a layer starting from 0 also passes a note at 0 (pngn_scan_chat's call at load)
            let fresh = l.time == 0.0;
            l.time += dt * l.speed;
            let t = if l.looping { l.time.rem_euclid(anim.duration) } else { l.time.min(anim.duration) };
            // notes passed this frame (wrapping at the loop point)
            for (nt, text) in &anim.notes {
                let hit = (fresh && *nt == 0.0) || if t >= prev { *nt > prev && *nt <= t } else { *nt > prev || *nt <= t };
                if hit && dt > 0.0 {
                    notes.write(AnimNote { entity: e, text: text.trim().to_owned() });
                }
            }
            for (i, name) in a.skeleton.names.iter().enumerate() {
                let drive = match l.mask {
                    Mask::All => true,
                    Mask::Upper => a.upper[i],
                    Mask::Lower => !a.upper[i],
                };
                if !drive {
                    continue;
                }
                let Some(track) = anim.tracks.get(name) else { continue };
                let Ok(mut jt) = joints.get_mut(a.joints[i]) else { continue };
                let w = l.weight;
                // the paired anim, sampled at the same phase of its own length
                let ptrack = pair.as_ref().and_then(|(pa, pw)| {
                    let pt = if pa.duration > 0.0 { t / anim.duration.max(1e-6) * pa.duration } else { 0.0 };
                    pa.tracks.get(name).map(|tr| (tr, pt, *pw))
                });
                let mut tv = sample3(&track.t, t);
                let mut rq = sample_q(&track.r, t);
                if let Some((ptr, pt, pw)) = ptrack {
                    if let (Some(a0), Some(b0)) = (tv, sample3(&ptr.t, pt)) {
                        tv = Some(a0.lerp(b0, pw));
                    }
                    if let (Some(a0), Some(b0)) = (rq, sample_q(&ptr.r, pt)) {
                        rq = Some(a0.slerp(b0, pw));
                    }
                }
                if let Some(v) = tv {
                    jt.translation = if w >= 1.0 { v } else { jt.translation.lerp(v, w) };
                }
                if let Some(q) = rq {
                    jt.rotation = if w >= 1.0 { q } else { jt.rotation.slerp(q, w) };
                }
                let sc = sample3(&track.s, t).unwrap_or(Vec3::ONE);
                jt.scale = if w >= 1.0 { sc } else { jt.scale.lerp(sc, w) };
            }
        }
    }
}

/// Skinning check (playtest evidence): once a second every skinned mesh is skinned on the CPU from its joints'
/// current transforms, as the GPU does. Each triangle edge is compared with its bind-pose length: the worst
/// stretch relative to the mesh's median (broken weights or a wrong joint order show as exploded triangles, far
/// above 1), and the mean vertex motion since the last sample (animations playing, not frozen in bind pose).
#[derive(Resource, Default)]
pub struct SkinCheck {
    pub meshes: usize,
    pub vertices: usize,
    pub max_stretch: f32,
    /// the largest change of an edge's length, as a fraction of its character's size (broken skinning: tens of %)
    pub max_gap: f32,
    pub mean_motion: f32,
    last: HashMap<Entity, Vec<Vec3>>,
    timer: f32,
    reported: Vec<Entity>,
}

fn skin_check(
    time: Res<Time<Real>>,
    mut chk: ResMut<SkinCheck>,
    q: Query<(Entity, &SkinnedMesh, &Mesh3d, &InheritedVisibility)>,
    meshes: Res<Assets<Mesh>>,
    binds: Res<Assets<SkinnedMeshInverseBindposes>>,
    globals: Query<&GlobalTransform>,
    parents: Query<&ChildOf>,
    names: Query<&Name>,
) {
    use bevy::mesh::VertexAttributeValues as V;
    chk.timer -= time.delta_secs();
    if chk.timer > 0.0 {
        return;
    }
    chk.timer = 1.0;
    let (mut n, mut verts, mut worst, mut motion, mut moved_n, mut gap) = (0usize, 0usize, 1.0f32, 0.0f32, 0usize, 0.0f32);
    let mut seen = Vec::new();
    for (e, sm, m, vis) in &q {
        if !vis.get() {
            continue;
        }
        let (Some(mesh), Some(ib)) = (meshes.get(&m.0), binds.get(&sm.inverse_bindposes)) else { continue };
        let attr = |id| mesh.try_attribute_option(id).ok().flatten();
        let (Some(V::Float32x3(pos)), Some(V::Uint16x4(ji)), Some(V::Float32x4(jw))) =
            (attr(Mesh::ATTRIBUTE_POSITION), attr(Mesh::ATTRIBUTE_JOINT_INDEX), attr(Mesh::ATTRIBUTE_JOINT_WEIGHT))
        else {
            continue;
        };
        let Some(idx) = mesh.try_indices_option().ok().flatten() else { continue };
        let mats: Vec<Mat4> = sm.joints.iter().zip(ib.iter()).map(|(j, inv)| globals.get(*j).map(|g| Mat4::from(g.affine()) * *inv).unwrap_or(Mat4::IDENTITY)).collect();
        let skinned: Vec<Vec3> = pos
            .iter()
            .zip(ji.iter().zip(jw.iter()))
            .map(|(p, (j, w))| {
                let p = Vec3::from(*p);
                (0..4).filter(|&k| w[k] > 0.0).map(|k| mats.get(j[k] as usize).copied().unwrap_or(Mat4::IDENTITY).transform_point3(p) * w[k]).sum()
            })
            .collect();
        let tri: Vec<usize> = idx.iter().collect();
        // the character's size: the extent of its skeleton (a sleeve or collar sub-mesh is far smaller)
        let (lo, hi) = sm.joints.iter().filter_map(|j| globals.get(*j).ok()).fold((Vec3::splat(f32::MAX), Vec3::splat(f32::MIN)), |(l, h), g| (l.min(g.translation()), h.max(g.translation())));
        let size = (hi - lo).length().max(1e-3);
        let mut ratios: Vec<f32> = Vec::with_capacity(tri.len());
        for t in tri.chunks_exact(3) {
            for (a, b) in [(t[0], t[1]), (t[1], t[2]), (t[2], t[0])] {
                let rest = Vec3::from(pos[a]).distance(Vec3::from(pos[b]));
                if rest > 1e-3 {
                    let now = skinned[a].distance(skinned[b]);
                    ratios.push(now / rest);
                    gap = gap.max((now - rest).abs() / size);
                }
            }
        }
        if ratios.is_empty() {
            continue;
        }
        let mut sorted = ratios.clone();
        sorted.sort_by(|a, b| a.total_cmp(b));
        let median = sorted[sorted.len() / 2].max(1e-6);
        let top = sorted[sorted.len() - 1] / median;
        let mesh_gap = ratios.iter().zip(tri.chunks_exact(3).flat_map(|t| [(t[0], t[1]), (t[1], t[2]), (t[2], t[0])]).filter(|(a, b)| Vec3::from(pos[*a]).distance(Vec3::from(pos[*b])) > 1e-3)).map(|(_, (a, b))| {
            let rest = Vec3::from(pos[a]).distance(Vec3::from(pos[b]));
            ((skinned[a].distance(skinned[b]) - rest).abs() / size, a, b)
        }).fold((0.0f32, 0, 0), |m, x| if x.0 > m.0 { x } else { m });
        if mesh_gap.0 > 0.1 && !chk.reported.contains(&e) {
            let (_, a, b) = mesh_gap;
            let owner = parents.iter_ancestors(e).find_map(|p| names.get(p).ok()).map(|n| n.as_str().to_owned()).unwrap_or_default();
            info!("skin check: {owner} mesh {e} ({} verts, size {size:.1}) gap {:.2}: v{a} joints {:?} w {:?} / v{b} joints {:?} w {:?}, rest {:.2} -> {:.2}",
                skinned.len(), mesh_gap.0, ji[a], jw[a], ji[b], jw[b], Vec3::from(pos[a]).distance(Vec3::from(pos[b])), skinned[a].distance(skinned[b]));
            chk.reported.push(e);
        }
        if false {
            // which edge: its vertices' joints and weights, its rest and skinned length
            let k = ratios.iter().position(|r| *r == sorted[sorted.len() - 1]).unwrap_or(0);
            let t = &tri[(k / 3) * 3..(k / 3) * 3 + 3];
            let (a, b) = [(t[0], t[1]), (t[1], t[2]), (t[2], t[0])][k % 3];
            info!(
                "skin check: mesh {e} ({} verts, {} joints) stretch {top:.2}: v{a} joints {:?} w {:?} / v{b} joints {:?} w {:?}, rest {:.3} -> {:.3}",
                skinned.len(), sm.joints.len(), ji[a], jw[a], ji[b], jw[b],
                Vec3::from(pos[a]).distance(Vec3::from(pos[b])), skinned[a].distance(skinned[b])
            );
            chk.reported.push(e);
        }
        worst = worst.max(top);
        if let Some(prev) = chk.last.get(&e).filter(|p| p.len() == skinned.len()) {
            motion += prev.iter().zip(&skinned).map(|(a, b)| a.distance(*b)).sum::<f32>() / skinned.len() as f32;
            moved_n += 1;
        }
        n += 1;
        verts += skinned.len();
        seen.push((e, skinned));
    }
    chk.last = seen.into_iter().collect();
    chk.meshes = n;
    chk.vertices = verts;
    chk.max_stretch = worst;
    chk.max_gap = gap;
    chk.mean_motion = if moved_n > 0 { motion / moved_n as f32 } else { 0.0 };
}
