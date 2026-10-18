//! Q's cape: a cloth model hung on his shoulders that swings as he moves.
//!
//! The original (src/ai/ai_08.cpp): Q111Object's setup 0x00486f40 creates the template "q111_cape" (when the level has
//! it) as a ClothObject (vtable 0x005aca40, behaviour cloth, property set p_cloth: its model q111_cape_0, the collision
//! LOD q111_cape_coll), keeps it at +0xbd0 (read back by 0x004876b0), gives it the anchor table 0x005fb94c (0x004879c0:
//! the cape's bones chest, l_shoulder and r_shoulder held to Q's bones of the same names) and attaches it to Q
//! (0x00487a40). ClothObject's setup (Slot6 0x00487910: the anchors cleared, +0x598 = 40.0), its physics (Slot7
//! 0x00487990: a body from the radius collision) and its remove (Slot55 0x00487960: detached, anchors cleared) frame
//! a Havok cloth simulation whose parameters are not recovered.
//!
//! The remaster: the cape model's skeleton (q111_cape_0: the three anchor bones and three chains, cape_l_2..3,
//! cape_m_1..3, cape_r_2..3) is posed each frame: the anchors take Q's bones, each chain bone's head is a particle
//! (verlet, under gravity, damped), drawn back towards where the rest pose on the anchors puts it (the cloth's shape)
//! and kept at its rest distance from its parent; each bone turns to point at its child's particle. The constants are
//! guesses judged by eye (no capture of Q's cape in motion). API: [`ClothPlugin`], [`Cape`].

use bevy::prelude::*;

/// The cape's template (0x00486f40: "q111_cape").
const CAPE_TEMPLATE: &str = "q111_cape";
/// The anchor bones (table 0x005fb94c).
const ANCHORS: [&str; 3] = ["chest", "l_shoulder", "r_shoulder"];
/// Per frame (at 60 frames a second): the velocity kept, and the pull back to the rest shape.
const DAMPING: f32 = 0.9;
const SHAPE_PULL: f32 = 0.12;
/// Gravity on the cloth (game units / s^2; the world's 64.348 makes it hang like lead: a cloth's drag halves it).
const GRAVITY: f32 = 32.0;

/// One chain bone's head.
#[derive(Clone, Copy, Default)]
struct Particle {
    pos: Vec3,
    prev: Vec3,
}

/// The cape on its owner (0x00487a40's attachment), its bones' particles (world space) once started.
#[derive(Component)]
pub struct Cape {
    pub owner: Entity,
    particles: Vec<Option<Particle>>,
    /// the last frame's largest swing (a particle's distance from its rest place) and constraint error, for the log
    swing: f32,
    stretch: f32,
    anchor_error: f32,
}

pub struct ClothPlugin;

impl Plugin for ClothPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, hang)
            .add_systems(PostUpdate, simulate.after(crate::skin::AnimateSet).before(bevy::transform::TransformSystems::Propagate))
            .add_systems(Last, log_state);
    }
}

/// Q's setup (0x00486f40): his cape, when the level has its template.
fn hang(
    mut commands: Commands,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    q: Query<(Entity, &crate::ai::Companion, &Transform), Added<crate::ai::Companion>>,
) {
    for (e, c, t) in &q {
        if !c.q {
            continue;
        }
        let Some(model) = data.templates.get(CAPE_TEMPLATE).and_then(|t| t.model.clone()) else { continue };
        if let Some(cape) = kit.spawn(&mut commands, &model, *t, true) {
            commands.entity(cape).insert(Cape { owner: e, particles: Vec::new(), swing: 0.0, stretch: 0.0, anchor_error: 0.0 });
            info!("EVENT cloth: Q's cape {model} hung");
        }
    }
}

/// Model-space matrices of a rig from its joints' local transforms (parents first).
fn model_space(a: &crate::skin::Animator, joints: &Query<&mut Transform, (Without<Cape>, Without<crate::ai::Companion>)>) -> Vec<Mat4> {
    let mut m: Vec<Mat4> = Vec::with_capacity(a.joints.len());
    for i in 0..a.joints.len() {
        let local = joints.get(a.joints[i]).map(|t| t.to_matrix()).unwrap_or(Mat4::IDENTITY);
        let p = a.skeleton.parents[i];
        m.push(if p >= 0 { m[p as usize] * local } else { local });
    }
    m
}

/// The cape posed on Q (after his animation, before the transforms propagate).
#[allow(clippy::type_complexity)]
fn simulate(
    mut commands: Commands,
    time: Res<Time>,
    mut capes: Query<(Entity, &mut Cape, &mut Transform, &crate::skin::Animator)>,
    owners: Query<(&Transform, &crate::skin::Animator), (With<crate::ai::Companion>, Without<Cape>)>,
    mut joints: Query<&mut Transform, (Without<Cape>, Without<crate::ai::Companion>)>,
) {
    let dt = time.delta_secs().min(0.1);
    let frames = dt * 60.0;
    for (e, mut cape, mut root, a) in &mut capes {
        // its owner gone (0x00487960: detached and removed)
        let Ok((ot, oa)) = owners.get(cape.owner) else {
            commands.entity(e).despawn();
            continue;
        };
        *root = *ot;
        let root_m = root.to_matrix();
        let q_model = model_space(oa, &joints);
        let sk = &a.skeleton;
        let n = a.joints.len();
        // the rest pose on the anchors: each anchor bone takes Q's bone (in the cape's model space, which is Q's)
        let mut target: Vec<Mat4> = Vec::with_capacity(n);
        let mut anchored = vec![false; n];
        for i in 0..n {
            let p = sk.parents[i];
            let pm = if p >= 0 { target[p as usize] } else { Mat4::IDENTITY };
            let rest = pm * sk.rest[i].to_matrix();
            let m = match ANCHORS.iter().any(|b| *b == sk.names[i]).then(|| oa.skeleton.index.get(&sk.names[i])).flatten() {
                Some(&qi) => {
                    anchored[i] = true;
                    q_model[qi]
                }
                None => rest,
            };
            target.push(m);
        }
        // the chain bones: a bone under an anchor (directly or through other chain bones), not an anchor itself
        let mut chain = vec![false; n];
        for i in 0..n {
            let p = sk.parents[i];
            chain[i] = !anchored[i] && p >= 0 && (anchored[p as usize] || chain[p as usize]);
        }
        if cape.particles.len() != n {
            cape.particles = (0..n).map(|i| chain[i].then(|| {
                let w = root_m.transform_point3(target[i].w_axis.truncate());
                Particle { pos: w, prev: w }
            })).collect();
        }
        // the particles: inertia, gravity, the pull to the rest shape, the distance to the parent
        let world = |m: &Mat4| root_m.transform_point3(m.w_axis.truncate());
        let mut swing = 0.0f32;
        let mut stretch = 0.0f32;
        let mut heads: Vec<Vec3> = target.iter().map(world).collect();
        for i in 0..n {
            let Some(mut pt) = cape.particles[i] else { continue };
            let rest_at = heads[i];
            let v = (pt.pos - pt.prev) * DAMPING.powf(frames);
            pt.prev = pt.pos;
            pt.pos += v - Vec3::Y * GRAVITY * dt * dt;
            pt.pos += (rest_at - pt.pos) * (1.0 - (1.0 - SHAPE_PULL).powf(frames));
            let parent = heads[sk.parents[i] as usize];
            let len = sk.rest[i].translation.length();
            let d = pt.pos - parent;
            pt.pos = parent + d.normalize_or(rest_at - parent) * len;
            stretch = stretch.max(((pt.pos - parent).length() - len).abs());
            swing = swing.max(pt.pos.distance(rest_at));
            heads[i] = pt.pos;
            cape.particles[i] = Some(pt);
        }
        // the bones: anchors on Q's, chain bones at their particles, turned towards their child's (a leaf as its parent
        // turned), as model-space matrices, then made local to their parents
        let root_inv = root_m.inverse();
        let mut posed: Vec<Mat4> = Vec::with_capacity(n);
        let mut turn: Vec<Quat> = vec![Quat::IDENTITY; n];
        for i in 0..n {
            let p = sk.parents[i];
            let m = if chain[i] {
                let child = (0..n).find(|&c| sk.parents[c] == i as i32 && chain[c]);
                let q = match child {
                    Some(c) => Quat::from_rotation_arc((heads_rest(&target, c, &root_m) - heads_rest(&target, i, &root_m)).normalize_or(Vec3::Y), (heads[c] - heads[i]).normalize_or(Vec3::Y)),
                    None => turn[p as usize],
                };
                turn[i] = q;
                let (s, r, _) = (root_m * target[i]).to_scale_rotation_translation();
                root_inv * Mat4::from_scale_rotation_translation(s, q * r, heads[i])
            } else {
                target[i]
            };
            let pm = if p >= 0 { posed[p as usize] } else { Mat4::IDENTITY };
            if let Ok(mut t) = joints.get_mut(a.joints[i]) {
                let (s, r, tr) = (pm.inverse() * m).to_scale_rotation_translation();
                *t = Transform { translation: tr, rotation: r, scale: s };
            }
            posed.push(m);
        }
        // how close the anchors sit on Q's bones (model space)
        let mut anchor_error = 0.0f32;
        for i in (0..n).filter(|&i| anchored[i]) {
            if let Some(&qi) = oa.skeleton.index.get(&sk.names[i]) {
                anchor_error = anchor_error.max(posed[i].w_axis.truncate().distance(q_model[qi].w_axis.truncate()));
            }
        }
        cape.swing = swing;
        cape.stretch = stretch;
        cape.anchor_error = anchor_error;
    }
}

/// A bone's head in the rest pose on the anchors (world).
fn heads_rest(target: &[Mat4], i: usize, root: &Mat4) -> Vec3 {
    root.transform_point3(target[i].w_axis.truncate())
}

/// The state log's "cape": capes hung, the largest swing from the rest shape, the chains' stretch and the anchors' fit.
fn log_state(capes: Query<&Cape>, play: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut p) = play else { return };
    let r = |v: f32| (v * 1000.0).round() / 1000.0;
    let swing = capes.iter().map(|c| c.swing).fold(0.0f32, f32::max);
    let stretch = capes.iter().map(|c| c.stretch).fold(0.0f32, f32::max);
    let anchor = capes.iter().map(|c| c.anchor_error).fold(0.0f32, f32::max);
    p.0.insert("cape".into(), serde_json::json!({"count": capes.iter().count(), "swing": r(swing), "stretch": r(stretch), "anchor_error": r(anchor)}));
}
