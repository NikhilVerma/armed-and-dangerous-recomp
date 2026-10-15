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
    /// Another level's folder (campaign.rs): the caches start over (a level can lack an anim another has, and a
    /// skeleton of the same name can differ: player_rlvindaloomissile_0 in lev_3_m1).
    pub fn set_dir(&mut self, dir: &std::path::Path) {
        if self.dir != dir {
            self.dir = dir.to_path_buf();
            self.skeletons.clear();
            self.anims.clear();
        }
    }

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

impl Anim {
    /// A bone's translation key track sampled at `t` seconds (held at the ends); None without one.
    pub fn translation(&self, bone: &str, t: f32) -> Option<Vec3> {
        sample3(&self.tracks.get(&bone.to_lowercase())?.t, t)
    }

    /// A bone's rotation key track sampled at `t` seconds (held at the ends); None without one (hud.rs: the
    /// objective pointer's arms).
    pub fn rotation(&self, bone: &str, t: f32) -> Option<Quat> {
        sample_q(&self.tracks.get(&bone.to_lowercase())?.r, t)
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
    /// seconds into the anim, kept in [0, duration] (wrapped when looping, held at the ends otherwise: 0x00409ec0)
    pub time: f32,
    /// playback rate; negative plays backwards (slot flag 0x40, e.g. Roman's backward run: 0x00409d90 negates the
    /// rate). Set it each frame for speed-driven anims (`RateBlock`).
    pub speed: f32,
    pub looping: bool,
    pub mask: Mask,
    /// 1 replaces what the layers below set; less blends toward this layer (lerp / slerp)
    pub weight: f32,
    /// a second anim sampled at the same time and mixed in first: the layer is lerp(anim, pair, pair weight)
    pub pair: Option<(String, f32)>,
    /// the anim's length once sampled (0 before), for `play_synced`
    pub duration: f32,
    /// where a new anim starts (resolved when it is first sampled, once the speed is known)
    start: Option<StartAt>,
}

/// Where a newly played anim starts.
#[derive(Clone, Copy, Debug)]
enum StartAt {
    /// its start, or its end when it plays backwards (0x00475ff0: user 1.0 with flag 0x40)
    Begin,
    /// a fraction of its length (flags 0xa00)
    Fraction(f32),
    /// the time the layer had (the slot's +4 left as it was: no 0x800)
    Time(f32),
}

impl Layer {
    pub fn new(anim: &str, looping: bool) -> Self {
        Layer { anim: anim.to_lowercase(), time: 0.0, speed: 1.0, looping, mask: Mask::All, weight: 1.0, pair: None, duration: 0.0, start: Some(StartAt::Begin) }
    }
    /// The position as a fraction of the length (0x00475fc0: slot time / entry length; 0 unknown).
    pub fn fraction(&self) -> f32 {
        if self.duration > 0.0 { (self.time / self.duration).clamp(0.0, 1.0) } else { 0.0 }
    }
}

/// The cosine ease of the original's blends: (1 - cos(w pi)) / 2 (0x00408ee0 / 0x00409ec0 at 0x00408fa3, 0x0040a158).
pub fn cos_blend(w: f32) -> f32 {
    (1.0 - (w.clamp(0.0, 1.0) * std::f32::consts::PI).cos()) * 0.5
}

/// What a cross-fade fades out from.
#[derive(Clone, Debug)]
enum FadeFrom {
    /// the layers that played before the change, still advancing (their notes silent): the original copies slots
    /// 0 / 1 into 2 / 3 with flag 0x10000 (AnimSlot_Set 0x00408a70 at 0x00408b41)
    Layers(Vec<Layer>),
    /// a frozen copy of the pose shown before the change (the change came while another fade ran: 0x00408820 caches
    /// the blended pose, skeleton +0x20, and 0x004ec2f0 blends from it); None until `animate` captures it
    Pose(Option<Vec<Transform>>),
}

/// A cross-fade in progress: the pose before the change weighs cos_blend(left / secs), from 1 down to 0.
#[derive(Clone, Debug)]
struct Fade {
    from: FadeFrom,
    secs: f32,
    left: f32,
}

/// Plays layers of animation on a skinned character's joints.
#[derive(Component)]
pub struct Animator {
    pub skeleton: Arc<Skeleton>,
    pub joints: Vec<Entity>,
    pub layers: Vec<Layer>,
    /// per bone: below the split bone (upper body)
    pub upper: Vec<bool>,
    /// The cross-fade time (seconds) for anim changes made from now on; 0 cuts. The original passes it with each
    /// change (AnimSlot_Set's weight, counted down by dt / +0x14 in 0x00409ec0): 0.25 for the AI's PlayAnim
    /// (0x004262d0), the player's per-kind table 0x005cb3a8 (charanim.rs).
    pub blend: f32,
    fade: Option<Fade>,
    /// a change already started the fade this frame (Roman changes both halves of a pair at once)
    fade_started: bool,
}

impl Animator {
    /// The weight of the pose before the last anim change (1 just after it, 0 when no cross-fade runs).
    pub fn fade_weight(&self) -> f32 {
        self.fade.as_ref().map_or(0.0, |f| cos_blend(f.left / f.secs.max(1e-6)))
    }

    /// Starts the cross-fade for a change about to be made (AnimSlot_Set 0x00408a70): none with a zero blend time
    /// (and a running one ends: the weight-0 path calls 0x00408820 with 0), none from nothing (the AI's first
    /// anim); from the playing layers, or from a frozen pose when another fade still runs.
    fn begin_fade(&mut self) {
        if self.fade_started {
            return;
        }
        self.fade_started = true;
        if self.blend <= 0.0 {
            self.fade = None;
            return;
        }
        if self.layers.iter().all(|l| l.anim.is_empty()) {
            return;
        }
        let from = if self.fade.is_some() { FadeFrom::Pose(None) } else { FadeFrom::Layers(self.layers.clone()) };
        self.fade = Some(Fade { from, secs: self.blend, left: self.blend });
    }

    /// Plays `anim` on a layer index (0 = base). Restarts it only when the name changes (then cross-fading over
    /// `blend`). Anim_Set 0x00408d90 (one anim: slot 0, slot 1 cleared) and 0x00408e30 (nothing when it already
    /// plays) in one.
    pub fn play(&mut self, slot: usize, anim: &str, looping: bool, mask: Mask) {
        self.play_from(slot, anim, looping, mask, StartAt::Begin);
    }

    /// As `play`, but a changed anim starts at the fraction of its length the layer's old one had reached (0x00475ff0
    /// flags 0xa00 with the slot's fraction 0x00475fc0: the player's mode changes within a state other than 0, e.g.
    /// reloading on the move from a walk into a run).
    pub fn play_synced(&mut self, slot: usize, anim: &str, looping: bool, mask: Mask) {
        let s = self.layers.get(slot).filter(|l| !l.anim.is_empty()).map_or(StartAt::Begin, |l| StartAt::Fraction(l.fraction()));
        self.play_from(slot, anim, looping, mask, s);
    }

    /// As `play`, but a changed anim carries on from the layer's time (0x00475ff0 without 0x800: the slot's time is
    /// left as it was; the player's walk / run modes in state 0 switch this way).
    pub fn play_continued(&mut self, slot: usize, anim: &str, looping: bool, mask: Mask) {
        let s = self.layers.get(slot).filter(|l| !l.anim.is_empty()).map_or(StartAt::Begin, |l| StartAt::Time(l.time));
        self.play_from(slot, anim, looping, mask, s);
    }

    /// As `play`, starting `time` seconds into the anim (an IC anim taken up part way: "anim_full", mover.rs).
    pub fn play_at(&mut self, slot: usize, anim: &str, looping: bool, mask: Mask, time: f32) {
        self.play_from(slot, anim, looping, mask, StartAt::Time(time));
    }

    fn play_from(&mut self, slot: usize, anim: &str, looping: bool, mask: Mask, start: StartAt) {
        let anim = anim.to_lowercase();
        while self.layers.len() <= slot {
            self.layers.push(Layer::new("", true));
        }
        if self.layers[slot].anim != anim {
            self.begin_fade();
            let l = &mut self.layers[slot];
            *l = Layer { anim, looping, mask, start: Some(start), ..Layer::new("", looping) };
        } else {
            let l = &mut self.layers[slot];
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
        if let Some((t, s)) = self.layers.get(to).map(|l| (l.time, l.start)) {
            if let Some(l) = self.layers.get_mut(slot) {
                l.time = t;
                l.start = s;
            }
        }
    }
    /// Clears a layer (cross-fading out of it over `blend`).
    pub fn stop(&mut self, slot: usize) {
        if self.layers.get(slot).is_some_and(|l| !l.anim.is_empty()) {
            self.begin_fade();
            self.layers[slot].anim.clear();
        }
    }
    /// Restarts a layer's anim from its start (or end when it plays backwards): the original's request flag 0x80
    /// (0x00409ec0); the notes from the start fire as the layer plays on (0x0040a3f0 fires them for a reset slot).
    pub fn restart(&mut self, slot: usize) {
        if let Some(l) = self.layers.get_mut(slot) {
            l.start = Some(StartAt::Begin);
        }
    }
    /// true when a non-looping layer reached its end
    pub fn finished(&self, slot: usize, lib: &AnimLibrary) -> bool {
        self.layers.get(slot).is_some_and(|l| {
            !l.looping && lib.anims.get(&l.anim).and_then(|a| a.as_ref()).is_some_and(|a| if l.speed < 0.0 { l.time <= 0.0 } else { l.time >= a.duration })
        })
    }
}

/// The speed-driven playback rate of a movement anim (0x00409d90 with the rate block of the anim table entry, +0xc):
/// the body's velocity along `dir` (model space: game axes, the model faces -Y), clamped to [min, max], mapped from
/// [low_speed, high_speed] onto [low_factor, high_factor]. lev_1_m1's p_trotter: run high 23, walk high 7, min 2.5,
/// max 32 (player.json `movement.anim_speed_tables`).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RateBlock {
    pub dir: Vec3,
    pub min: f32,
    pub max: f32,
    pub low_factor: f32,
    pub low_speed: f32,
    pub high_factor: f32,
    pub high_speed: f32,
}

impl RateBlock {
    /// The rate for a velocity in model space (flags 0: v . dir; 0x005bcd3c = 0.001 the smallest range).
    pub fn rate(&self, velocity: Vec3) -> f32 {
        let s = velocity.dot(self.dir).clamp(self.min, self.max);
        let range = self.high_speed - self.low_speed;
        let t = if range > 0.001 { (s - self.low_speed) / range } else { 1.0 };
        (1.0 - t) * self.low_factor + t * self.high_factor
    }
}

/// One layer's step: the new time and the note-time spans it played over (the original's events fire for times in
/// [from, to) going forwards, (to, from] backwards: AnimTrack_FireEvents 0x004eb480 over the intervals of 0x0040a200,
/// split where the time wrapped). Times stay in [0, duration]: looping wraps, otherwise they hold at the end reached.
fn step_time(time: f32, speed: f32, dt: f32, duration: f32, looping: bool) -> (f32, [Option<(f32, f32)>; 2]) {
    let d = dt * speed;
    if d == 0.0 || duration <= 0.0 {
        return (time, [None, None]);
    }
    let t = time + d;
    if d > 0.0 {
        if t < duration {
            return (t, [Some((time, t)), None]);
        }
        let wrapped = if looping { t.rem_euclid(duration) } else { duration };
        (wrapped, [Some((time, duration)), looping.then_some((0.0, wrapped))])
    } else {
        if t >= 0.0 {
            return (t, [Some((time, t)), None]);
        }
        let wrapped = if looping { t.rem_euclid(duration) } else { 0.0 };
        (wrapped, [Some((time, 0.0)), looping.then_some((duration, wrapped))])
    }
}

/// Whether a note at `nt` fires over a span (from, to): [from, to) forwards, (to, from] backwards (0x004eb480).
fn note_in(nt: f32, (from, to): (f32, f32)) -> bool {
    if to >= from { nt >= from && nt < to } else { nt <= from && nt > to }
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
    Animator { skeleton, joints, layers: Vec::new(), upper, blend: 0.0, fade: None, fade_started: false }
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

/// Blends one joint transform toward another: position and scale lerped, rotation slerped (0x004e3510).
fn mix(a: &Transform, b: &Transform, w: f32) -> Transform {
    Transform { translation: a.translation.lerp(b.translation, w), rotation: a.rotation.slerp(b.rotation, w), scale: a.scale.lerp(b.scale, w) }
}

/// Advances `layers` by `dt` and poses them onto `pose` (one transform per bone, holding what the layers below set),
/// layer over layer by each layer's weight. The notes passed go to `notes` when given (the fading copies of a
/// cross-fade are silent: flag 0x10000 in 0x0040a200).
fn play_layers(layers: &mut [Layer], lib: &mut AnimLibrary, skel: &Skeleton, upper: &[bool], pose: &mut [Transform], dt: f32, mut notes: Option<&mut Vec<String>>) {
    for l in layers.iter_mut() {
        if l.anim.is_empty() {
            continue;
        }
        let Some(anim) = lib.anim(&l.anim) else { continue };
        let pair = l.pair.as_ref().and_then(|(n, w)| lib.anim(n).map(|a| (a, *w)));
        l.duration = anim.duration;
        match l.start.take() {
            Some(StartAt::Begin) => l.time = if l.speed < 0.0 { anim.duration } else { 0.0 },
            Some(StartAt::Fraction(f)) => l.time = f * anim.duration,
            Some(StartAt::Time(t)) => l.time = t,
            None => {}
        }
        l.time = l.time.clamp(0.0, anim.duration);
        let (t, spans) = step_time(l.time, l.speed, dt, anim.duration, l.looping);
        l.time = t;
        if let Some(out) = notes.as_mut() {
            for span in spans.into_iter().flatten() {
                for (nt, text) in &anim.notes {
                    if note_in(*nt, span) {
                        out.push(text.trim().to_owned());
                    }
                }
            }
        }
        for (i, name) in skel.names.iter().enumerate() {
            let drive = match l.mask {
                Mask::All => true,
                Mask::Upper => upper[i],
                Mask::Lower => !upper[i],
            };
            if !drive {
                continue;
            }
            let Some(track) = anim.tracks.get(name) else { continue };
            let jt = &mut pose[i];
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

/// The anim update (0x00409ec0 Anim_Update, 0x0040a200 the notes, the pose blends 0x004ec600 / 0x004ec800 /
/// 0x004ec2f0): each layer advances and poses the joints; during a cross-fade the pose before the change (its old
/// layers playing on, or a frozen pose) is blended over the result by the fade weight, which first drops by dt.
fn animate(time: Res<Time>, mut lib: ResMut<AnimLibrary>, mut q: Query<(Entity, &mut Animator)>, mut joints: Query<&mut Transform>, mut notes: MessageWriter<AnimNote>) {
    let dt = time.delta_secs();
    let mut passed = Vec::new();
    for (e, mut a) in &mut q {
        let a = &mut *a;
        a.fade_started = false;
        let mut pose: Vec<Transform> = a.joints.iter().map(|j| joints.get(*j).copied().unwrap_or_default()).collect();
        // a frozen fade source: the pose shown until now
        if let Some(Fade { from: FadeFrom::Pose(p @ None), .. }) = a.fade.as_mut() {
            *p = Some(pose.clone());
        }
        let base = pose.clone();
        passed.clear();
        play_layers(&mut a.layers, &mut lib, &a.skeleton, &a.upper, &mut pose, dt, Some(&mut passed));
        for text in passed.drain(..) {
            notes.write(AnimNote { entity: e, text });
        }
        if let Some(f) = a.fade.as_mut() {
            f.left -= dt;
            let w = cos_blend(f.left / f.secs.max(1e-6));
            let from = match &mut f.from {
                FadeFrom::Layers(old) => {
                    let mut p = base;
                    play_layers(old, &mut lib, &a.skeleton, &a.upper, &mut p, dt, None);
                    p
                }
                FadeFrom::Pose(p) => p.clone().unwrap_or(base),
            };
            if w > 0.0 {
                for (to, from) in pose.iter_mut().zip(&from) {
                    *to = mix(to, from, w);
                }
            }
            if f.left <= 0.0 {
                a.fade = None;
            }
        }
        for (j, t) in a.joints.iter().zip(&pose) {
            if let Ok(mut jt) = joints.get_mut(*j) {
                if *jt != *t {
                    *jt = *t;
                }
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

#[cfg(test)]
mod tests {
    use super::*;

    fn fired(notes: &[f32], spans: [Option<(f32, f32)>; 2]) -> Vec<f32> {
        spans.into_iter().flatten().flat_map(|s| notes.iter().copied().filter(move |n| note_in(*n, s))).collect()
    }

    #[test]
    fn notes_fire_once_per_pass_forwards() {
        // trotter_run_aim_b: footsteps at 0.1667 and 0.5333 of 0.767 s
        let notes = [0.0, 0.16666667, 0.53333336, 0.767];
        let (mut t, mut all) = (0.0, Vec::new());
        for _ in 0..92 {
            // 92 frames of 1/60 s = two loops of 0.767 at rate 1
            let (nt, spans) = step_time(t, 1.0, 1.0 / 60.0, 0.767, true);
            all.extend(fired(&notes, spans));
            t = nt;
        }
        // the note at 0 at the start and once per wrap, the two footsteps each loop; the one at the very end never
        // (the original's [from, to) intervals end at the length)
        assert_eq!(all.iter().filter(|n| **n == 0.0).count(), 2, "{all:?}");
        assert_eq!(all.iter().filter(|n| **n > 0.1 && **n < 0.6).count(), 4, "{all:?}");
        assert!(!all.contains(&0.767));
    }

    #[test]
    fn notes_fire_backwards_and_hold_at_the_ends() {
        let notes = [0.2, 0.5];
        // backwards over both notes: (to, from]
        let (t, spans) = step_time(0.6, -1.0, 0.5, 0.767, true);
        assert!((t - 0.1).abs() < 1e-5);
        assert_eq!(fired(&notes, spans), vec![0.2, 0.5]);
        // backwards across the start: (0, from] then (t, length] after the wrap
        let (t, spans) = step_time(0.3, -1.0, 0.4, 0.767, true);
        assert!((t - 0.667).abs() < 1e-5, "{t}");
        assert_eq!(fired(&notes, spans), vec![0.2]);
        // a one-shot anim holds at its end, its time no further
        let (t, spans) = step_time(0.7, 1.0, 0.5, 0.767, false);
        assert_eq!(t, 0.767);
        assert_eq!(spans[1], None);
        // no time passed: nothing fires
        assert_eq!(step_time(0.3, 1.0, 0.0, 0.767, true).1, [None, None]);
    }

    #[test]
    fn cross_fade_eases_by_cosine() {
        assert_eq!(cos_blend(0.0), 0.0);
        assert!((cos_blend(1.0) - 1.0).abs() < 1e-6);
        assert!((cos_blend(0.5) - 0.5).abs() < 1e-6);
        // 0.05 s into a 0.125 s fade: 0.6 of it left -> 0.654
        assert!((cos_blend(0.6) - 0.6545).abs() < 1e-3);
    }

    #[test]
    fn movement_rate_follows_the_speed() {
        // p_trotter's run block: 40 units/s forwards plays trotter_run_aim at 40 / 23
        let run = RateBlock { dir: Vec3::new(0.0, -1.0, 0.0), min: 2.5, max: 32.0, low_factor: 0.0, low_speed: 0.0, high_factor: 1.0, high_speed: 23.0 };
        // the speed is clamped to [min, max] first: 40 -> 32
        assert!((run.rate(Vec3::new(0.0, -40.0, 0.0)) - 32.0 / 23.0).abs() < 1e-5);
        assert!((run.rate(Vec3::new(0.0, -11.5, 0.0)) - 0.5).abs() < 1e-5);
        // moving the other way: the floor (min 2.5)
        assert!((run.rate(Vec3::new(0.0, 10.0, 0.0)) - 2.5 / 23.0).abs() < 1e-5);
    }
}
