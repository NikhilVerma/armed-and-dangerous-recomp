//! The fx objects' physics: a dynamic body when a script asks for gravity or an impulse (Object_setPhysicsEnabled
//! (obj, 0)), linear impulses (0x10 / 0x1e / 0x64: Body_ApplyImpulse 0x00458440), spins (0x1a: a force action,
//! ForceActionPool_Add 0x00403fb0 with an angular impulse) and the ground-hit test that drives the on-hit opcodes
//! (the Havok contact callback FxObject slot 0x2a, 0x00419330: +0x464 |= 0x2000, the contact point and the other body).
//!
//! The body (`BodyDef`) follows the original's build (the fx body set-up fn_00419020 0x00419020 ->
//! Object_BuildRadiusCollision 0x004527d0): its mass is the game's, in the game's units (Havok runs in game units, the
//! world's gravity 64.348 units/s^2), so a script's impulse gives the original's speed (impulse / mass) and its gravity
//! scale the original's weight. Fx bodies do not stop bullets (they are not in the projectile list World +0xe90; their
//! collision mask 0x20000 from the hit-ground opcodes, Fx_SetHitGround 0x0041f9d0, is taken as "the ground only": guess).

use super::object::FxObject;
use crate::gamedata::Props;
use crate::level::game_to_engine;
use crate::physics::{Body, Physics, jq, jv};
use bevy::prelude::*;
use oxijolt::prelude::*;

/// Game impulse units to Jolt for the blast pushes on bodies whose mass is not the game's (fx.rs: the area damage's
/// impact force on props; debt: how non-character receivers take impact_force is not traced). Fx bodies do not use
/// it: their mass is the game's.
pub const IMPULSE_SCALE: f32 = 1.0 / 66.0;
/// 0x1a spins: the vector (degrees, per the scripts' 0..360 ranges) taken as an angular velocity change in degrees per
/// second (guess: the original passes it to a force action as an angular impulse of vector / 120 in Havok units),
/// capped so a script's 100000 (fx_debri_oilpump_02) stays a fast tumble.
const MAX_SPIN: f32 = 20.0;
/// Object_BuildRadiusCollision's floor on a body's mass (0.05, K 0x005bcc34).
const MIN_MASS: f32 = 0.05;

/// An fx template's body: its collision box round the model (model space, game axes, at scale 1), its mass in game
/// units and its surface (the template's property set: p_fx, p_fx_collide, p_fx_collide_friction, ...).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BodyDef {
    /// the model's bounding box: centre and half size (game units, scale 1)
    pub centre: Vec3,
    pub half: Vec3,
    /// game mass (density x volume, game units)
    pub mass: f32,
    pub friction: f32,
    pub restitution: f32,
}

impl Default for BodyDef {
    /// No model (an empty reference mesh): a 0.25 unit box of density 1.
    fn default() -> Self {
        let half = Vec3::splat(0.25);
        BodyDef { centre: Vec3::ZERO, half, mass: sphere_mass(half.length(), 1.0), friction: 0.2, restitution: 0.2 }
    }
}

/// A sphere's mass, r^3 x density x 4/3 pi (Object_BuildRadiusCollision 0x004527d0: K 0x005bcfc8), at least 0.05.
fn sphere_mass(radius: f32, density: f32) -> f32 {
    (radius.powi(3) * density * 4.18879).max(MIN_MASS)
}

/// The volume a closed triangle mesh encloses (the sum of its triangles' signed tetrahedra about the origin).
pub fn enclosed_volume(vertices: &[Vec3], tris: &[[u32; 3]]) -> f32 {
    let v = |i: u32| vertices.get(i as usize).copied().unwrap_or(Vec3::ZERO);
    tris.iter().map(|t| v(t[0]).dot(v(t[1]).cross(v(t[2])))).sum::<f32>().abs() / 6.0
}

impl BodyDef {
    /// The body of a template with this model's bounds (`min`, `max`) and enclosed volume, and its property set.
    /// Mass (Object_BuildRadiusCollision 0x004527d0; fx objects build with cinfo +0xc set):
    /// - a `collide_poly` template (scale-table flag 1): the compound of its collision model's parts (0x00456c40 ->
    ///   Body_BuildShape 0x00413450), whose Havok mass properties come from the shape with the object's density;
    ///   here density x the model's enclosed volume (guess for Havok's computation), at most the sphere's;
    /// - otherwise a sphere of the bounding radius (vt+0x90; the bounding box's half diagonal here) round the bounding
    ///   centre: r^3 x density x 4/3 pi;
    /// - an explicit `mass` (p_fx_zero_mass 0.01) wins (guess: the property's name).
    ///
    /// The original builds the body when the object is made (the factory 0x0045f020, before any 0x0b scale) and
    /// never rescales it, so the mass is the scale-1 one; the collision box is scaled with the object instead
    /// (`make_dynamic`), so a small piece rests on its own faces rather than hovering on a full-size shape.
    pub fn new(min: Vec3, max: Vec3, volume: f32, p: &Props) -> Self {
        let half = ((max - min) * 0.5).max(Vec3::splat(0.1));
        let centre = (max + min) * 0.5;
        let density = p.f("density", 1.0);
        let sphere = sphere_mass(half.length(), density);
        let mass = if p.has("mass") {
            p.f("mass", 1.0).max(1e-3)
        } else if p.f("collide_poly", 0.0) != 0.0 && volume > 1e-3 {
            (volume * density).clamp(MIN_MASS, sphere)
        } else {
            sphere
        };
        BodyDef { centre, half, mass, friction: p.f("friction", 0.2), restitution: p.f("restitution", 0.2) }
    }

    /// The collision box at `scale` (game units): centre and half size in model space.
    pub fn scaled(&self, scale: f32) -> (Vec3, Vec3) {
        (self.centre * scale, self.half * scale)
    }
}

/// Body_ApplyImpulse 0x00458440 with point null: a linear impulse (game units: game mass x units/s, world frame) at
/// the centre of mass, the body woken (0x004577b0). The body's Jolt mass is the game mass, so only the length converts.
pub fn add_impulse(physics: &mut Physics, id: BodyId, v: Vec3) {
    let k = physics.meters_per_unit;
    if let Ok(mut b) = physics.world.body_mut(id) {
        b.activate();
        let _ = b.add_impulse(jv(game_to_engine() * v * k));
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

/// Object_setPhysicsEnabled(obj, 0): the fx object gets a dynamic body (`BodyDef`: the template's mass, friction and
/// restitution; the model's box at the object's scale), its gravity scale (0x11: +0x48c; Physics_ApplyGravityScale
/// 0x00403c70 gives listed objects g * (scale - 1), so 1 is normal gravity and an fx object's default 0 would float:
/// FxObject_Load 0x00418f60 clears +0x48c, and every fx body is listed by its body set-up fn_00419020 0x00419020
/// (Game+0xa84 +0xf1c); 0x11 with exactly 1.0 unlinks it, read back by fn_00419390 0x00419390) of the game's gravity.
/// Inert objects (0x79) never get one. Pending impulses and spins are applied now. Fast pieces (a flying keg leaves at
/// ~180 units/s) sweep their motion, so they do not tunnel through a roof or the ground.
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
    let def = o.body_def;
    let (centre, half) = def.scaled(o.scale_at_now());
    let cube = Shape::new_box(jv(half.max(Vec3::splat(0.02)) * k)).ok()?;
    let shape = Shape::new_compound(&[CompoundChild { shape: &cube, position: jv(centre * k), rotation: jq(Quat::IDENTITY), user_data: 0 }]).ok()?;
    let s = BodySettings::new_dynamic()
        .mass(def.mass)
        .friction(def.friction)
        .restitution(def.restitution)
        .gravity_factor(physics.game_gravity_factor(gravity.max(0.0)))
        .motion_quality(MotionQuality::LinearCast);
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

/// The body's collision box centre (engine space, game units) and its velocity (engine axes, units/s).
fn centre_and_velocity(physics: &Physics, id: BodyId, o: &FxObject) -> Option<(Vec3, Vec3)> {
    let b = physics.world.body(id).ok()?;
    let k = physics.meters_per_unit;
    let origin = physics.from_m(b.position());
    let rot = crate::physics::bq(b.rotation());
    let v = crate::physics::bv(b.linear_velocity()) / k;
    let (centre, _) = o.body_def.scaled(o.scale_now);
    Some((origin + rot * centre, v))
}

/// A new contact of a moving fx body (engine-side stand-in for the Havok contact callback): a ray from the box's
/// centre along its velocity (straight down when it rests), as long as the box's half size plus this frame's travel,
/// the body itself excluded. Returns (game-space contact point, the body hit).
pub fn touching(physics: &Physics, id: BodyId, o: &FxObject, dt: f32) -> Option<(Vec3, BodyId)> {
    let (centre, v) = centre_and_velocity(physics, id, o)?;
    let k = physics.meters_per_unit;
    let (_, half) = o.body_def.scaled(o.scale_now);
    let dir = if v.length() > 0.5 { v.normalize() } else { Vec3::NEG_Y };
    let len = half.max_element() * 1.2 + v.length() * dt;
    let ray = RayCast::new(crate::physics::jr(centre * k), jv(dir * len * k));
    let hit = physics.world.cast_ray(&ray, &QueryFilter::new().exclude_body(id)).ok()??;
    Some((game_to_engine().inverse() * (centre + dir * hit.distance / k), hit.body))
}

/// How an fx body sits (the playtests' check that debris lands and settles): its velocity (game axes, units/s) and,
/// once it rests (under 3 units/s), the gap between the bottom of its visible box and whatever is below it (game
/// units; None when nothing is within 200 units).
pub fn rest(physics: &Physics, id: BodyId, o: &FxObject) -> Option<(Vec3, Option<f32>)> {
    let (centre, v) = centre_and_velocity(physics, id, o)?;
    let vg = game_to_engine().inverse() * v;
    if v.length() >= 3.0 {
        return Some((vg, None));
    }
    let k = physics.meters_per_unit;
    let rot = physics.world.body(id).ok().map(|b| crate::physics::bq(b.rotation()))?;
    let (_, half) = o.body_def.scaled(o.scale_now);
    // the box's extent below its centre: sum over its axes of |axis . down| x half
    let below = (rot * Vec3::X).y.abs() * half.x + (rot * Vec3::Y).y.abs() * half.y + (rot * Vec3::Z).y.abs() * half.z;
    let ray = RayCast::new(crate::physics::jr(centre * k), jv(Vec3::NEG_Y * 200.0 * k));
    let gap = physics.world.cast_ray(&ray, &QueryFilter::new().exclude_body(id)).ok().flatten().map(|h| h.distance / k - below);
    Some((vg, gap))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn props(v: serde_json::Value) -> Props {
        Props(v.as_object().cloned().unwrap_or_default())
    }

    /// Object_BuildRadiusCollision 0x004527d0: a sphere of the bounding radius, r^3 x density x 4/3 pi; collide_poly:
    /// density x the enclosed volume; an explicit mass wins.
    #[test]
    fn masses() {
        // the flying keg (fxo_powderkeg, p_fx): bounds 8.2 x 8.6 x 5.0 round (-0.17, 0.04, 3.07)
        let p_fx = props(serde_json::json!({ "density": 1.0, "friction": 0.2, "restitution": 0.2 }));
        let keg = BodyDef::new(Vec3::new(-4.27, -4.26, 0.58), Vec3::new(3.93, 4.33, 5.57), 45.0, &p_fx);
        assert!((keg.mass - 1118.0).abs() < 20.0, "{}", keg.mass);
        assert!((keg.centre.z - 3.07).abs() < 0.01);
        // its launch: impulse 200000 -> about 180 units/s
        assert!((200000.0 / keg.mass - 179.0).abs() < 4.0);
        let poly = props(serde_json::json!({ "density": 1.0, "collide_poly": 1, "friction": 0.001, "restitution": 0.001 }));
        let piece = BodyDef::new(Vec3::splat(-10.0), Vec3::splat(10.0), 2165.0, &poly);
        assert_eq!(piece.mass, 2165.0);
        assert_eq!((piece.friction, piece.restitution), (0.001, 0.001));
        let zero = props(serde_json::json!({ "mass": 0.01 }));
        assert_eq!(BodyDef::new(Vec3::splat(-1.0), Vec3::ONE, 0.0, &zero).mass, 0.01);
    }

    #[test]
    fn closed_cube_volume() {
        let v: Vec<Vec3> = (0..8).map(|i| Vec3::new((i & 1) as f32, ((i >> 1) & 1) as f32, ((i >> 2) & 1) as f32) * 2.0).collect();
        // 12 triangles of a 2-unit cube, outward
        let t = [[0, 2, 1], [1, 2, 3], [4, 5, 6], [5, 7, 6], [0, 1, 4], [1, 5, 4], [2, 6, 3], [3, 6, 7], [0, 4, 2], [2, 4, 6], [1, 3, 5], [3, 7, 5]];
        assert!((enclosed_volume(&v, &t) - 8.0).abs() < 1e-4);
    }
}
