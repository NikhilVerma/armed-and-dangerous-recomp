//! The fx objects' physics: a dynamic body when a script asks for gravity or an impulse (Object_setPhysicsEnabled
//! (obj, 0)), linear impulses (0x10 / 0x1e / 0x64: Body_ApplyImpulse 0x00458440), spins (0x1a: a force action,
//! ForceActionPool_Add 0x00403fb0 with an angular impulse) and the ground-hit test that drives the on-hit opcodes
//! (the Havok contact callback FxObject slot 0x2a, 0x00419330: +0x464 |= 0x2000, the contact point and the other body).
//!
//! The scales between the game's Havok units and Jolt are guesses (labelled below); the fx meshes' collision volumes
//! are not exported, so an fx body is its mesh's bounding box (0.25 units for an empty reference mesh). Fx bodies do
//! not stop bullets (they are not in the projectile list World +0xe90; their collision mask 0x20000 from the hit-ground
//! opcodes, Fx_SetHitGround 0x0041f9d0, is taken as "the ground only": guess).

use super::object::FxObject;
use crate::level::game_to_engine;
use crate::physics::{Body, Physics, jq, jv};
use bevy::prelude::*;
use oxijolt::prelude::*;

/// Game impulse units to Jolt (kg m/s): unknown in the original (Havok mass from density x collision volume);
/// chosen so the flying keg of fx_explosion_powder_keg (impulse 200000 on a 30 kg body) leaves at about 20 m/s
/// (100 game units/s). Guess, to be measured against the original.
pub const IMPULSE_SCALE: f32 = 1.0 / 66.0;
/// 0x1a spins: the vector (degrees, per the scripts' 0..360 ranges) taken as an angular velocity change in degrees per
/// second (guess: the original passes it to a force action as an angular impulse of vector / 120 in Havok units),
/// capped so a script's 100000 (fx_debri_oilpump_02) stays a fast tumble.
const MAX_SPIN: f32 = 20.0;

/// Body_ApplyImpulse 0x00458440 with point null: a linear impulse (game units, world frame) at the centre of mass,
/// the body woken (0x004577b0); scaled by IMPULSE_SCALE (the fixed fx body mass of make_dynamic).
pub fn add_impulse(physics: &mut Physics, id: BodyId, v: Vec3) {
    let k = physics.meters_per_unit;
    if let Ok(mut b) = physics.world.body_mut(id) {
        b.activate();
        let _ = b.add_impulse(jv(game_to_engine() * v * k * IMPULSE_SCALE));
    }
}

/// 0x1a (Fx_RotateObject 0x0041f8f0 -> 0x00455b80): wakes the object and spins it (game-space axes, degrees).
pub fn add_spin(physics: &mut Physics, id: BodyId, deg: Vec3) {
    let w = game_to_engine() * (deg * std::f32::consts::PI / 180.0);
    let w = w.clamp_length_max(MAX_SPIN);
    let now = physics.world.body(id).map(|b| crate::physics::bv(b.angular_velocity())).unwrap_or(Vec3::ZERO);
    if let Ok(mut b) = physics.world.body_mut(id) {
        b.activate();
        let _ = b.set_angular_velocity(jv(now + w));
    }
}

/// Object_setPhysicsEnabled(obj, 0): the fx object gets a dynamic body (a box), its gravity scale (0x11: +0x48c;
/// Physics_ApplyGravityScale 0x00403c70 gives listed objects g * (scale - 1), so 1 is normal gravity and an fx
/// object's default 0 would float: FxObject_Load 0x00418f60 clears +0x48c, and every fx body is listed by its body
/// set-up fn_00419020 0x00419020 (Game+0xa84 +0xf1c); 0x11 with exactly 1.0 unlinks it, read back by fn_00419390
/// 0x00419390). Inert objects (0x79) never get one. Pending impulses and spins are applied now.
pub fn make_dynamic(physics: &mut Physics, commands: &mut Commands, o: &mut FxObject, e: Entity, gravity: f32) -> Option<BodyId> {
    if let Some(b) = o.body {
        return Some(b);
    }
    if o.inert {
        return None;
    }
    let k = physics.meters_per_unit;
    let world = Mat4::from_quat(game_to_engine()) * o.place;
    let (_, rot, pos) = world.to_scale_rotation_translation();
    let shape = Shape::new_box(jv(o.half * k)).ok()?;
    let s = BodySettings::new_dynamic().mass(30.0).friction(0.2).restitution(0.2).gravity_factor(gravity.max(0.0));
    let id = physics.add_dynamic(e, &shape, pos, rot, Vec3::ZERO, s)?;
    commands.entity(e).insert(Body::rigid(id));
    o.body = Some(id);
    physics.no_bullets.insert(id);
    if o.impulse != Vec3::ZERO {
        add_impulse(physics, id, std::mem::take(&mut o.impulse));
    }
    if o.spin != Vec3::ZERO {
        add_spin(physics, id, std::mem::take(&mut o.spin));
    }
    Some(id)
}

/// Turns a body to a new placement's rotation (0x31 after the body exists).
pub fn set_rotation(physics: &mut Physics, id: BodyId, place: Mat4) {
    let (_, rot, _) = (Mat4::from_quat(game_to_engine()) * place).to_scale_rotation_translation();
    if let Ok(mut b) = physics.world.body_mut(id) {
        let _ = b.set_rotation(jq(rot), Activation::Activate);
    }
}

/// The body gone with its object.
pub fn remove(physics: &mut Physics, id: BodyId) {
    physics.no_bullets.remove(&id);
    physics.remove(id);
}

pub fn wake(physics: &mut Physics, id: BodyId) {
    if let Ok(mut b) = physics.world.body_mut(id) {
        b.activate();
    }
}

/// A new contact of a moving fx body (engine-side stand-in for the Havok contact callback): a ray from the body's
/// centre along its velocity (straight down when it rests), as long as the half size plus this frame's travel, the
/// body itself excluded. Returns (game-space contact point, the body hit).
pub fn touching(physics: &Physics, id: BodyId, half: Vec3, dt: f32) -> Option<(Vec3, BodyId)> {
    let b = physics.world.body(id).ok()?;
    let k = physics.meters_per_unit;
    let centre = physics.from_m(b.position());
    let v = crate::physics::bv(b.linear_velocity()) / k;
    drop(b);
    let dir = if v.length() > 0.5 { v.normalize() } else { Vec3::NEG_Y };
    let len = half.max_element() * 1.2 + v.length() * dt;
    let ray = RayCast::new(crate::physics::jr(centre * k), jv(dir * len * k));
    let hit = physics.world.cast_ray(&ray, &crate::physics::Physics::query().exclude_body(id)).ok()??;
    Some((game_to_engine().inverse() * (centre + dir * hit.distance / k), hit.body))
}
