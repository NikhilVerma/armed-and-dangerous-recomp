//! Articulated ragdolls for dying characters (the original's GoRagdoll: a Havok ragdoll, its parameters not
//! recovered): one capsule per main bone (pelvis, chest, head, upper and lower arms, thighs, shins) joined by
//! swing-twist joints, built from the character's pose at the moment of death, launched with the hit's push. The
//! physics bodies then drive those bones; the other bones follow their parents. Limits and sizes are guesses,
//! judged by eye against the original's dying grunts.

use crate::physics::{Physics, bq, br, jq, jr, jv};
use crate::skin::Animator;
use bevy::prelude::*;
use oxijolt::prelude::*;
use oxijolt::{RagdollJoint, RagdollPart, SkeletonJoint, SwingTwistConstraintSettings};

/// (bone, parent part index, the bone its capsule reaches to, radius in game units, swing half-angle and twist
/// half-angle in degrees)
const PARTS: [(&str, Option<u32>, &str, f32, f32, f32); 11] = [
    ("center", None, "belly", 1.3, 0.0, 0.0),
    ("chest", Some(0), "neck", 1.4, 30.0, 25.0),
    ("head", Some(1), "mouth", 0.9, 40.0, 40.0),
    ("l_shoulder", Some(1), "l_arm", 0.55, 80.0, 45.0),
    ("l_arm", Some(3), "l_wrist", 0.5, 70.0, 20.0),
    ("r_shoulder", Some(1), "r_arm", 0.55, 80.0, 45.0),
    ("r_arm", Some(5), "r_wrist", 0.5, 70.0, 20.0),
    ("l_leg", Some(0), "l_shin", 0.7, 60.0, 20.0),
    ("l_shin", Some(7), "l_ankle", 0.55, 70.0, 10.0),
    ("r_leg", Some(0), "r_shin", 0.7, 60.0, 20.0),
    ("r_shin", Some(9), "r_ankle", 0.55, 70.0, 10.0),
];

/// A character turned ragdoll: the Jolt ragdoll and, per part, its bone index.
#[derive(Component)]
pub struct Ragdoll {
    pub id: RagdollId,
    pub bones: Vec<usize>,
}

pub struct RagdollPlugin;

impl Plugin for RagdollPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(PostUpdate, drive.after(crate::skin::AnimateSet).before(bevy::transform::TransformSystems::Propagate));
    }
}

/// Builds and starts the ragdoll of a character from its joints' current world transforms. Returns None when the
/// skeleton lacks a bone of the set (the caller keeps its simpler fallback).
pub fn start(physics: &mut Physics, a: &Animator, joints: &Query<&GlobalTransform>, push: Vec3) -> Option<Ragdoll> {
    let k = physics.meters_per_unit;
    let idx = |n: &str| a.skeleton.index.get(n).copied();
    let mut bones = Vec::new();
    let mut shapes = Vec::new();
    let mut poses = Vec::new();
    for (bone, _, to, r, _, _) in PARTS {
        let (bi, ti) = (idx(bone)?, idx(to)?);
        let w = joints.get(a.joints[bi]).ok()?.compute_transform();
        let wt = joints.get(a.joints[ti]).ok()?.translation();
        // the capsule from the bone to the next one, in the bone's frame
        let local = w.rotation.inverse() * (wt - w.translation);
        let len = local.length().max(0.2);
        let dir = local / len;
        let half = ((len * 0.5 - r).max(0.05)) * k;
        let cap = Shape::new_capsule(half, r * k).ok()?;
        let child = CompoundChild { shape: &cap, position: jv(dir * len * 0.5 * k), rotation: jq(Quat::from_rotation_arc(Vec3::Y, dir)), user_data: 0 };
        let shape = Shape::new_compound(&[child]).ok()?;
        bones.push(bi);
        shapes.push(shape);
        poses.push((w.translation, w.rotation.normalize(), dir));
    }
    let names: Vec<SkeletonJoint> = PARTS.iter().map(|p| SkeletonJoint { name: p.0, parent: p.1 }).collect();
    let skeleton = oxijolt::Skeleton::new(&names).ok()?;
    let parts: Vec<RagdollPart> = PARTS
        .iter()
        .enumerate()
        .map(|(i, p)| {
            let (pos, rot, dir) = poses[i];
            let body = BodySettings::new_dynamic().position(jr(pos * k)).rotation(jq(rot)).linear_velocity(jv(push * k)).friction(0.8).angular_damping(0.6);
            let joint = p.1.map(|_| {
                let twist = (rot * dir).normalize();
                let plane = twist.any_orthonormal_vector();
                RagdollJoint::SwingTwist(
                    SwingTwistConstraintSettings::new(jr(pos * k), jv(twist), jv(plane))
                        .half_cone_angles(p.4.to_radians(), p.4.to_radians())
                        .twist_limits(-p.5.to_radians(), p.5.to_radians()),
                )
            });
            RagdollPart { shape: &shapes[i], body, joint }
        })
        .collect();
    let settings = RagdollSettings::new_stabilized(&skeleton, &parts).ok()?;
    let id = physics.world.create_ragdoll(&settings, None, Activation::Activate).ok()?;
    Some(Ragdoll { id, bones })
}

/// The ragdoll's bodies drive their bones: each part's world transform, made local to its parent bone (whose
/// model-space transform is rebuilt along the skeleton each frame).
fn drive(physics: NonSend<Physics>, rigs: Query<(&Ragdoll, &Animator, &GlobalTransform)>, mut joints: Query<&mut Transform>) {
    let k = physics.meters_per_unit;
    for (rd, a, root) in &rigs {
        let Ok(r) = physics.world.ragdoll(rd.id) else { continue };
        let ids = r.body_ids().to_vec();
        let root_inv = root.affine().inverse();
        let n = a.skeleton.names.len();
        let mut model: Vec<Mat4> = Vec::with_capacity(n);
        for i in 0..n {
            let parent = a.skeleton.parents[i];
            let local = joints.get(a.joints[i]).map(|t| t.to_matrix()).unwrap_or(Mat4::IDENTITY);
            let pm = if parent >= 0 { model[parent as usize] } else { Mat4::IDENTITY };
            let mut m = pm * local;
            if let Some(p) = rd.bones.iter().position(|&b| b == i) {
                if let Some(b) = ids.get(p).and_then(|id| physics.world.body(*id).ok()) {
                    let w = Mat4::from_rotation_translation(bq(b.rotation()), br(b.position()) / k);
                    m = Mat4::from(root_inv) * w;
                    if let Ok(mut t) = joints.get_mut(a.joints[i]) {
                        let l = pm.inverse() * m;
                        let (_, rot, tr) = l.to_scale_rotation_translation();
                        t.translation = tr;
                        t.rotation = rot;
                    }
                }
            }
            model.push(m);
        }
    }
}
