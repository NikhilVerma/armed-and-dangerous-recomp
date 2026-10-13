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
}

impl Layer {
    pub fn new(anim: &str, looping: bool) -> Self {
        Layer { anim: anim.to_lowercase(), time: 0.0, speed: 1.0, looping, mask: Mask::All }
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
            *l = Layer { anim, time: 0.0, speed: 1.0, looping, mask };
        } else {
            l.looping = looping;
            l.mask = mask;
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

pub struct SkinPlugin;

impl Plugin for SkinPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<AnimLibrary>().add_message::<AnimNote>().add_systems(PostUpdate, animate.before(TransformSystems::Propagate));
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
            let prev = if l.looping { l.time.rem_euclid(anim.duration) } else { l.time.min(anim.duration) };
            l.time += dt * l.speed;
            let t = if l.looping { l.time.rem_euclid(anim.duration) } else { l.time.min(anim.duration) };
            // notes passed this frame (wrapping at the loop point)
            for (nt, text) in &anim.notes {
                let hit = if t >= prev { *nt > prev && *nt <= t } else { *nt > prev || *nt <= t };
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
                if let Some(v) = sample3(&track.t, t) {
                    jt.translation = v;
                }
                if let Some(q) = sample_q(&track.r, t) {
                    jt.rotation = q;
                }
                jt.scale = sample3(&track.s, t).unwrap_or(Vec3::ONE);
            }
        }
    }
}
