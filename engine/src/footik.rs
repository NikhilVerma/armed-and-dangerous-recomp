//! Foot placement and leg IK (the player's: 0x00477570 each frame after the anims, and the leg bone callback
//! 0x00472cc0). Game-neutral: any character with a `FootPlacement` and a skeleton whose legs are hip -> knee ->
//! ankle chains gets it; the player has one (PlayerObject_Load binds the leg callback, table 0x005fb760).
//!
//! What the original does (archive/src/anim/anim_01.cpp):
//! - For each ankle ("l_ankle", "r_ankle": 0x005ce328) of the animated pose, a ray from 6 above it (0x005bccf0) straight
//!   down finds the ground. The ray is 12 long (0x41400000) for the first foot and doubles after each hit; a miss
//!   gives height 0 and length 0 (the next foot casts no ray: reproduced). The foot's ground height relative to the
//!   body's base is hit + 0.1 (0x005bcbfc); the foot's height and its ground normal move toward the new ones by
//!   min(dt x 10, 1) (0x005bcb68), the normal renormalised.
//! - The lowest foot (no lower than -2.5, 0x005bce78) sets the body: it drops by lowest x 0.75 (0x005bc15c) - 0.1
//!   (only downwards: lowest is first raised by the highest when both are below, then kept <= 0) (0x00457820, the
//!   model's offset); each foot's target is its height above the lowest (+0x684), never negative.
//! - The leg callback (its per-leg record found by 0x00472c30, the lift read by 0x00472c90) solves each leg with the
//!   law of cosines: the knee bends and the hip turns so the ankle reaches
//!   its animated position raised by that height; the ankle keeps its orientation and the foot turns onto the
//!   ground normal (flag 0x10).
//! Not ported: the landing bob (+0x648, the turbo's), the fall flags it sets when the lowest foot is below -4 (the
//! player's fall code decides falls in the remaster), and the body's pull onto the ground (the walker keeps him on it).

use crate::skin::Animator;
use bevy::prelude::*;

pub struct FootIkPlugin;

impl Plugin for FootIkPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, attach_player)
            .add_systems(PostUpdate, place_feet.after(crate::skin::AnimateSet).after(crate::charanim::twist).before(bevy::transform::TransformSystems::Propagate));
    }
}

/// A character's foot placement: its ankles and the state that eases between frames.
#[derive(Component, Clone, Debug)]
pub struct FootPlacement {
    /// the ankle bones (each the end of hip -> knee -> ankle)
    pub ankles: [String; 2],
    /// each foot's ground height relative to the body's base (+0x664), eased
    pub height: [f32; 2],
    /// each foot's ground normal in the character's frame (game axes, z up: +0x64c), eased
    pub normal: [Vec3; 2],
    /// the body's drop this frame (the model offset)
    pub drop: f32,
    /// each foot's lift this frame (+0x684)
    pub lift: [f32; 2],
    pub enabled: bool,
}

impl Default for FootPlacement {
    fn default() -> Self {
        FootPlacement { ankles: ["l_ankle".into(), "r_ankle".into()], height: [0.0; 2], normal: [Vec3::Z; 2], drop: 0.0, lift: [0.0; 2], enabled: true }
    }
}

/// The ray's start above the ankle, its first length, the ease rate, the lowest foot height, the body's share of it
/// and its sink (0x005bccf0, 0x41400000, 0x005bcb68, 0x005bce78, 0x005bc15c, 0x005bcbfc).
const RAY_UP: f32 = 6.0;
const RAY_LEN: f32 = 12.0;
const EASE_RATE: f32 = 10.0;
const LOWEST_MIN: f32 = -2.5;
const BODY_SHARE: f32 = 0.75;
const SINK: f32 = 0.1;

fn attach_player(mut commands: Commands, q: Query<Entity, (With<crate::player::Roman>, With<Animator>, Without<FootPlacement>)>) {
    for e in &q {
        commands.entity(e).insert(FootPlacement::default());
    }
}

/// The body drop and the feet's lifts from the feet's ground heights (0x00477570's second half).
pub fn body_and_lifts(height: [f32; 2]) -> (f32, [f32; 2]) {
    let mut lowest = height[0].min(height[1]);
    let highest = height[0].max(height[1]);
    if lowest < LOWEST_MIN {
        lowest = LOWEST_MIN;
    }
    let lift = [(height[0] - lowest).max(0.0), (height[1] - lowest).max(0.0)];
    let m = lowest - highest;
    if lowest < m {
        lowest = m;
    }
    let lowest = lowest.min(0.0);
    (lowest * BODY_SHARE - SINK, lift)
}

/// The two-bone solve (0x00472cc0's hip branch): the knee bend and the hip turn, as rotations in the chain's frame,
/// that bring the ankle from `a` to `t` with the hip at `h` and the knee at `k` (the bend stays in the leg's plane).
pub fn two_bone(h: Vec3, k: Vec3, a: Vec3, t: Vec3) -> Option<(Quat, Quat)> {
    let (u, v) = (h - k, a - k);
    let (la, lb) = (u.length(), v.length());
    if la < 1e-4 || lb < 1e-4 {
        return None;
    }
    let n = u.cross(v);
    if n.length_squared() < 1e-10 {
        return None;
    }
    let n = n.normalize();
    let lc = (t - h).length().clamp((la - lb).abs() + 1e-3, la + lb - 1e-3);
    let cur = (u.dot(v) / (la * lb)).clamp(-1.0, 1.0).acos();
    let want = ((la * la + lb * lb - lc * lc) / (2.0 * la * lb)).clamp(-1.0, 1.0).acos();
    let knee = Quat::from_axis_angle(n, want - cur);
    let a2 = k + knee * v;
    let (from, to) = ((a2 - h).normalize_or_zero(), (t - h).normalize_or_zero());
    if from == Vec3::ZERO || to == Vec3::ZERO {
        return None;
    }
    Some((knee, Quat::from_rotation_arc(from, to)))
}

#[allow(clippy::type_complexity)]
fn place_feet(
    time: Res<Time>,
    physics: NonSend<crate::physics::Physics>,
    mut q: Query<(Entity, &Transform, &Animator, &mut FootPlacement, Option<&crate::player::Roman>)>,
    mut joints: Query<&mut Transform, Without<FootPlacement>>,
    mut state: Option<ResMut<crate::playtest::PlayState>>,
) {
    let ease = (time.delta_secs() * EASE_RATE).min(1.0);
    for (owner, tf, a, mut fp, roman) in &mut q {
        if !fp.enabled || roman.is_some_and(|r| r.dead) {
            continue;
        }
        let skel = &a.skeleton;
        let n = skel.names.len();
        let local: Vec<Transform> = a.joints.iter().map(|j| joints.get(*j).map(|t| *t).unwrap_or_default()).collect();
        // the bones in the character's frame (game axes)
        let model = |local: &[Transform]| -> Vec<Transform> {
            let mut out: Vec<Option<Transform>> = vec![None; n];
            fn get(i: usize, skel: &crate::skin::Skeleton, local: &[Transform], out: &mut Vec<Option<Transform>>) -> Transform {
                if let Some(t) = out[i] {
                    return t;
                }
                let p = skel.parents[i];
                let t = if p >= 0 { get(p as usize, skel, local, out).mul_transform(local[i]) } else { local[i] };
                out[i] = Some(t);
                t
            }
            (0..n).map(|i| get(i, skel, local, &mut out)).collect()
        };
        let idx = |name: &str| skel.index.get(name).copied();
        let legs: Vec<(usize, usize, usize)> = fp
            .ankles
            .iter()
            .filter_map(|an| {
                let a = idx(an)?;
                let k = usize::try_from(skel.parents[a]).ok()?;
                let h = usize::try_from(skel.parents[k]).ok()?;
                Some((h, k, a))
            })
            .collect();
        if legs.len() != 2 {
            continue;
        }
        let m = model(&local);
        // the ground under each ankle (rays through the character's own body)
        let mut len = RAY_LEN;
        let up_w = Vec3::Y;
        for (i, &(_, _, ai)) in legs.iter().enumerate() {
            let ankle_w = tf.transform_point(m[ai].translation);
            let mut target = (0.0, Vec3::Z);
            if len > 0.0 {
                let from = ankle_w + up_w * RAY_UP;
                let mut start = from;
                let mut left = len;
                let mut hit = None;
                while let Some((body, at, normal)) = physics.raycast(start, -up_w, left) {
                    if physics.bodies.get(&body) == Some(&owner) {
                        let step = at.distance(start) + 0.05;
                        start = at - up_w * 0.05;
                        left -= step;
                        if left <= 0.0 {
                            break;
                        }
                        continue;
                    }
                    hit = Some((at, normal));
                    break;
                }
                match hit {
                    Some((at, normal)) => {
                        target = (at.y + SINK - tf.translation.y, (tf.rotation.inverse() * normal).normalize_or(Vec3::Z));
                        len += len;
                    }
                    None => len = 0.0,
                }
            }
            fp.height[i] += (target.0 - fp.height[i]) * ease;
            let nn = fp.normal[i] + (target.1 - fp.normal[i]) * ease;
            fp.normal[i] = nn.normalize_or(Vec3::Z);
        }
        let (drop, lift) = body_and_lifts(fp.height);
        fp.drop = drop;
        fp.lift = lift;
        // the body's drop on the root of the legs' chain (the bone the animation rewrites every frame: the original's
        // model offset 0x00457820 is not accumulated; a root nothing rewrites would sink by the drop each frame),
        // then each leg reaches its lifted ankle
        let mut local = local;
        let mut root = legs[0].0;
        while let Ok(p) = usize::try_from(skel.parents[root]) {
            root = p;
        }
        local[root].translation.z += drop;
        for (leg, &(hi, ki, ai)) in legs.iter().enumerate() {
            let m = model(&local);
            let (h, k, an) = (m[hi].translation, m[ki].translation, m[ai].translation);
            let t = an + Vec3::Z * lift[leg];
            let Some((dk, dh)) = two_bone(h, k, an, t) else { continue };
            let parent_rot = |i: usize| usize::try_from(skel.parents[i]).map_or(Quat::IDENTITY, |p| m[p].rotation);
            let (wh, wk, wa) = (m[hi].rotation, m[ki].rotation, m[ai].rotation);
            // hip: turned by dh in the character's frame; knee: bent by dk (its parent turned with it); ankle: its
            // old orientation (the ankle branch undoes the hip and knee turns)
            local[hi].rotation = (parent_rot(hi).inverse() * dh * parent_rot(hi) * local[hi].rotation).normalize();
            local[ki].rotation = (wh.inverse() * dk * wh * local[ki].rotation).normalize();
            local[ai].rotation = ((dh * dk * wk).inverse() * wa).normalize();
            // the foot below the ankle (the callback's "other leg bones" branch, leg flag 0x10) turns onto the ground
            // normal about the knee's hinge only: the normal less its part along the hinge (+0x18 of the leg record,
            // the bend's axis turned with the hip; 0x004f1830's arc). A skeleton without a foot bone tilts the ankle.
            let hinge = (dh * (h - k).cross(an - k)).normalize_or_zero();
            let normal = (fp.normal[leg] - hinge * fp.normal[leg].dot(hinge)).normalize_or(Vec3::Z);
            let tilt = Quat::from_rotation_arc(Vec3::Z, normal);
            match (0..n).find(|&j| skel.parents[j] == ai as i32) {
                Some(fi) => local[fi].rotation = (wa.inverse() * tilt * m[fi].rotation).normalize(),
                None => local[ai].rotation = ((dh * dk * wk).inverse() * tilt * wa).normalize(),
            }
        }
        for (j, t) in a.joints.iter().zip(&local) {
            if let Ok(mut jt) = joints.get_mut(*j) {
                if *jt != *t {
                    *jt = *t;
                }
            }
        }
        if let (Some(s), true) = (state.as_mut(), roman.is_some()) {
            let r2 = |v: f32| (v * 100.0).round() / 100.0;
            s.0.insert("foot_l".into(), r2(fp.height[0]).into());
            s.0.insert("foot_r".into(), r2(fp.height[1]).into());
            s.0.insert("foot_drop".into(), r2(fp.drop).into());
            s.0.insert("foot_lift".into(), r2(fp.lift[0].max(fp.lift[1])).into());
            // a root bone nothing animates (ref_q_00): its height must not follow the drop
            if let Some(&q) = skel.index.get("ref_q_00") {
                s.0.insert("ref_q_z".into(), r2(local[q].translation.z).into());
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn flat_ground_sinks_the_body_a_little() {
        // both feet on the base's ground (hit + 0.1): no lift, the body 0.1 down
        let (drop, lift) = body_and_lifts([0.1, 0.1]);
        assert!((drop + 0.1).abs() < 1e-6, "{drop}");
        assert_eq!(lift, [0.0, 0.0]);
    }

    #[test]
    fn a_step_lifts_the_upper_foot_and_drops_the_body() {
        // one foot 2 lower than the base, the other on it: the body drops 0.75 x 2 + 0.1, the upper foot lifts 2.1
        let (drop, lift) = body_and_lifts([-2.0, 0.1]);
        assert!((drop - (-2.0 * 0.75 - 0.1)).abs() < 1e-5, "{drop}");
        assert!((lift[1] - 2.1).abs() < 1e-5 && lift[0] == 0.0);
        // no further than -2.5
        let (drop, _) = body_and_lifts([-9.0, 0.1]);
        assert!((drop - (-2.5 * 0.75 - 0.1)).abs() < 1e-5);
    }

    #[test]
    fn two_bone_reaches_the_target() {
        // a leg along -z with a slight bend, the ankle raised by 1
        let (h, k, a) = (Vec3::new(0.0, 0.0, 4.0), Vec3::new(0.0, 0.2, 2.0), Vec3::new(0.0, 0.0, 0.0));
        let t = a + Vec3::Z * 1.0;
        let (dk, dh) = two_bone(h, k, a, t).unwrap();
        let k2 = h + dh * (k - h);
        let a2 = k2 + dh * dk * (a - k);
        assert!(a2.distance(t) < 1e-3, "{a2}");
        // the bones keep their lengths
        assert!(((k2 - h).length() - (k - h).length()).abs() < 1e-4);
        assert!(((a2 - k2).length() - (a - k).length()).abs() < 1e-4);
    }
}
