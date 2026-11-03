//! A flung grunt is one thrown through the air by a blast or a launch.
//!
//! In the original the body flails along a ballistic arc and only goes limp when it lands. The ragdoll's joints hold
//! their pose, so going limp at the moment of death would show a stiff figure sailing through the air.
//!
//! The original's 8 s limit removes the body. Here the limit starts the ragdoll, so a body that meets nothing still
//! lies down and fades on the corpse timer.
//!
//! The flight comes from Character_Launch 0x00431020 and state 0x195, which play anims 0x49 and 0x4a. The landing
//! comes from the fall block of 0x0042e1c0 and GoRagdoll 0x00431310.

use super::Grunt;
use crate::physics::Physics;
use bevy::prelude::*;

/// The flight's limit: the countdown +0x490 of state 0x195 (GenericAI_Update, ai_06.cpp:3336).
const FLIGHT_SECONDS: f32 = 8.0;

/// A dead grunt in flight, animated by charanim.rs until [`fly`] turns it into a ragdoll.
#[derive(Component)]
pub struct Flung {
    pub velocity: Vec3,
    pub left: f32,
}

impl Flung {
    pub fn new(velocity: Vec3) -> Self {
        Flung { velocity, left: FLIGHT_SECONDS }
    }
}

/// Moves each flung grunt along its arc, and lets it go limp when it meets the world or the time runs out.
pub fn fly(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<Physics>,
    mut q: Query<(Entity, &mut Flung, &mut Transform, &mut Grunt, &mut crate::skin::Animator)>,
    joints: Query<&GlobalTransform>,
) {
    let dt = time.delta_secs();
    for (e, mut flung, mut t, mut g, mut a) in &mut q {
        flung.left -= dt;
        let step = flung.velocity * dt;
        let met = physics.raycast(t.translation + Vec3::Y, step.normalize_or(Vec3::NEG_Y), step.length() + 1.0).is_some_and(|(b, _, _)| !physics.ai_bodies.contains(&b));
        if (met && flung.velocity.y <= 0.0) || flung.left <= 0.0 {
            if let Some(rd) = crate::ragdoll::start(&mut physics, &a, &joints, flung.velocity) {
                a.layers.clear();
                commands.entity(e).insert(rd);
            }
            g.death_name = None;
            commands.entity(e).remove::<Flung>().insert(crate::corpses::Corpse::ragdoll());
            continue;
        }
        t.translation += step;
        flung.velocity.y -= super::launch::GRAVITY * dt;
    }
}
