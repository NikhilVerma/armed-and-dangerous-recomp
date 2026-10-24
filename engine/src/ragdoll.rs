//! Articulated ragdolls for dying characters (the original's GoRagdoll: a Havok ragdoll, its parameters not
//! recovered): one capsule per main bone (pelvis, chest, head, upper and lower arms, thighs, shins) joined by
//! swing-twist joints, built from the character's pose at the moment of death, launched with the hit's push. The
//! physics bodies then drive those bones; the other bones follow their parents. Limits and sizes are guesses,
//! judged by eye against the original's dying grunts. The joints' hold is the original's (`JOINT_FRICTION`).
//!
//! Debt (audit 2026-10-22, deferred): the original builds one body per record of the model's part list (obj+0x1bc,
//! 0x4c bytes each, pose from record +8: Object_SetRagdoll 0x00459000) and reads each joint from the ragdoll
//! definition (Body_addConstraints 0x00456570: type 2 limited hinges with optional limits, type 0 ragdoll joints with
//! the cone cos +0x4c, plane sines +0x50/+0x54 and twist sines +0x40/+0x44). Decoding those records from the model
//! files would replace `PARTS`.

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

/// The character whose ragdoll `body` is a part of (its bodies are not in Physics::bodies: they drive bones, not the
/// entity's transform).
pub fn owner_of(physics: &Physics, rigs: &Query<(Entity, &Ragdoll)>, body: oxijolt::prelude::BodyId) -> Option<Entity> {
    rigs.iter().find(|(_, rd)| physics.world.ragdoll(rd.id).is_ok_and(|r| r.body_ids().contains(&body))).map(|(e, _)| e)
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
    start_with(physics, a, joints, push, Build::default())
}

/// How a ragdoll is built beyond the pose: for a character of another build than a grunt's.
#[derive(Clone, Copy, Debug)]
pub struct Build {
    /// the parts' radii (sized for a grunt, ~9.8 units tall) times this (the peasants, 5.3 units tall: ~0.54)
    pub size: f32,
    /// each joint's friction torque per kilogram of the two parts it joins (N m / kg; 0: limp). Havok's limited hinge and
    /// ragdoll constraints take a "strength" (Body_addConstraints 0x00456570: (mass A + mass B) x 20): a ragdoll whose
    /// joints hold its pose until a strong pull bends them ([`JOINT_FRICTION`], the default)
    pub joint_friction: f32,
    /// every part's radius in game units instead of the set's (the tied peasant: 0x00405d10 sets each part's convex
    /// radius, shape +0xc, to 1.0)
    pub radius: Option<f32>,
}

/// The joints' hold (Body_addConstraints 0x00456570): each joint's strength is (mass A + mass B) x 20 in the original's
/// units, where gravity is 64.35 units / s^2; as a friction torque here with the same ratio to the weight under the
/// world's gravity (the game's, physics.rs: 64.35 units / s^2 = 12.87 m/s^2): 20 x 12.87 x 0.2 / 64.35 = 20 x 0.2^2 =
/// 0.8 N m per kg. Every ragdoll the original builds goes through it: the tied peasant (0x00405d10), the AI's
/// (GenericAI_Update 0x0043ee90) and Roman's (PlayerObject 0x004788a0), so corpses keep their pose until pulled hard.
pub const JOINT_FRICTION: f32 = 20.0 * crate::level::METERS_PER_UNIT * crate::level::METERS_PER_UNIT;

impl Default for Build {
    fn default() -> Self {
        Build { size: 1.0, joint_friction: JOINT_FRICTION, radius: None }
    }
}

/// As [`start`] with a [`Build`].
pub fn start_with(physics: &mut Physics, a: &Animator, joints: &Query<&GlobalTransform>, push: Vec3, build: Build) -> Option<Ragdoll> {
    let size = build.size;
    let k = physics.meters_per_unit;
    let idx = |n: &str| a.skeleton.index.get(n).copied();
    let mut bones = Vec::new();
    let mut shapes = Vec::new();
    let mut poses = Vec::new();
    let mut masses = Vec::new();
    for (bone, _, to, r, _, _) in PARTS {
        let r = build.radius.unwrap_or(r * size);
        let bi = idx(bone)?;
        let w = joints.get(a.joints[bi]).ok()?.compute_transform();
        // the end bone; a skeleton without one (Roman's has no "mouth") extends the bone from its parent's joint
        let wt = match idx(to) {
            Some(ti) => joints.get(a.joints[ti]).ok()?.translation(),
            None => {
                let up = idx("neck").and_then(|n| joints.get(a.joints[n]).ok()).map(|n| w.translation - n.translation()).unwrap_or(Vec3::Y * 2.0);
                w.translation + up * 0.8
            }
        };
        // the capsule from the bone to the next one, in the bone's frame
        let local = w.rotation.inverse() * (wt - w.translation);
        let len = local.length().max(0.2 * size);
        let dir = local / len;
        let half = ((len * 0.5 - r).max(0.05 * size)) * k;
        let cap = Shape::new_capsule(half, r * k).ok()?;
        // its mass at Jolt's default density (1000 kg / m^3)
        let rm = r * k;
        masses.push(1000.0 * std::f32::consts::PI * rm * rm * (2.0 * half + 4.0 / 3.0 * rm));
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
                        .twist_limits(-p.5.to_radians(), p.5.to_radians())
                        .max_friction_torque(p.1.map_or(0.0, |parent| build.joint_friction * (masses[i] + masses[parent as usize]))),
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
/// model-space transform is rebuilt along the skeleton each frame). A ragdoll's entity is top-level, so its own
/// Transform is its world placement: this frame's, also when its owner moved it this frame (a dragged peasant's root
/// follows its pelvis, peasant.rs), where the GlobalTransform would still be last frame's.
fn drive(physics: NonSend<Physics>, rigs: Query<(Entity, &Ragdoll, &Animator)>, mut joints: Query<&mut Transform>) {
    let k = physics.meters_per_unit;
    for (e, rd, a) in &rigs {
        let Ok(r) = physics.world.ragdoll(rd.id) else { continue };
        let ids = r.body_ids().to_vec();
        let Ok(root) = joints.get(e).map(|t| t.to_matrix()) else { continue };
        let root_inv = root.inverse();
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
                    m = root_inv * w;
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
