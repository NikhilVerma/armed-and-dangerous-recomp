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
use std::sync::Arc;

/// Game impulse units to Jolt for the blast pushes on bodies whose mass is not the game's (fx.rs: the area damage's
/// impact force on props; debt: how non-character receivers take impact_force is not traced). Fx bodies do not use
/// it: their mass is the game's.
pub const IMPULSE_SCALE: f32 = 1.0 / 66.0;
/// 0x1a spins: the vector (degrees, per the scripts' 0..360 ranges) taken as an angular velocity change in degrees per
/// second (guess: the original passes it to a force action as an angular impulse of vector / 120 in Havok units),
/// capped so a script's 100000 (fx_debri_oilpump_02) stays a fast tumble.
const MAX_SPIN: f32 = 20.0;
/// The contact group of every fx body (contacts.rs: a group's bodies never touch); far from the AI's squad tags.
const FX_BODIES: u32 = 0xF0B0_D1E5;
/// Object_BuildRadiusCollision's floor on a body's mass (0.05, K 0x005bcc34).
const MIN_MASS: f32 = 0.05;

/// An fx template's body: its collision box round the model (model space, game axes, at scale 1), its mass in game
/// units and its surface (the template's property set: p_fx, p_fx_collide, p_fx_collide_friction, ...).
#[derive(Clone, Debug)]
pub struct BodyDef {
    /// the model's bounding box: centre and half size (game units, scale 1)
    pub centre: Vec3,
    pub half: Vec3,
    /// game mass (density x volume, game units)
    pub mass: f32,
    pub friction: f32,
    pub restitution: f32,
    /// the model's convex hull (metres, model space, scale 1; Body_BuildShape's hull), when Jolt could build one; the
    /// bounding box otherwise (a pane of glass too thin for Jolt's hull builder)
    pub hull: Option<Arc<Shape>>,
}

impl Default for BodyDef {
    /// No model (an empty reference mesh): a 0.25 unit box of density 1.
    fn default() -> Self {
        let half = Vec3::splat(0.25);
        BodyDef { centre: Vec3::ZERO, half, mass: sphere_mass(half.length(), 1.0), friction: 0.2, restitution: 0.2, hull: None }
    }
}

/// A sphere's mass, r^3 x density x 4/3 pi (Object_BuildRadiusCollision 0x004527d0: K 0x005bcfc8), at least 0.05.
fn sphere_mass(radius: f32, density: f32) -> f32 {
    (radius.powi(3) * density * 4.18879).max(MIN_MASS)
}

/// The volume of the convex hull of `points` (an incremental hull; 0 for flat or fewer than four points). Body_BuildShape
/// 0x00413450 builds the collision model's shape with its hull flag, and Havok takes a convex shape's mass from its
/// volume: the barracks' roof (hull 36968) then leaves at 216 units/s, rises ~360 units and falls back within its 12 s
/// to burst on landing (its 0x34), where its mesh's enclosed volume (9277) would send it out of sight for good.
pub fn hull_volume(points: &[Vec3]) -> f32 {
    let p: Vec<bevy::math::DVec3> = points.iter().map(|v| v.as_dvec3()).collect();
    if p.len() < 4 {
        return 0.0;
    }
    let (lo, hi) = p.iter().fold((p[0], p[0]), |(a, b), v| (a.min(*v), b.max(*v)));
    let size = (hi - lo).max_element();
    if size <= 0.0 {
        return 0.0;
    }
    let eps = size * 1e-7;
    // a first tetrahedron: two far points, the farthest from their line, the farthest from their plane
    let i0 = 0;
    let i1 = (0..p.len()).max_by(|&a, &b| p[a].distance_squared(p[i0]).total_cmp(&p[b].distance_squared(p[i0]))).unwrap_or(0);
    let line = |k: usize| (p[k] - p[i0]).cross(p[i1] - p[i0]).length();
    let i2 = (0..p.len()).max_by(|&a, &b| line(a).total_cmp(&line(b))).unwrap_or(0);
    let n0 = (p[i1] - p[i0]).cross(p[i2] - p[i0]);
    let plane = |k: usize| n0.dot(p[k] - p[i0]).abs();
    let i3 = (0..p.len()).max_by(|&a, &b| plane(a).total_cmp(&plane(b))).unwrap_or(0);
    if line(i2) <= eps * size || plane(i3) <= eps * n0.length() {
        return 0.0;
    }
    let inside = (p[i0] + p[i1] + p[i2] + p[i3]) / 4.0;
    // faces wound outward (their normal away from `inside`)
    let orient = |f: [usize; 3]| if (p[f[1]] - p[f[0]]).cross(p[f[2]] - p[f[0]]).dot(p[f[0]] - inside) < 0.0 { [f[0], f[2], f[1]] } else { f };
    let mut faces: Vec<[usize; 3]> = [[i0, i1, i2], [i0, i1, i3], [i0, i2, i3], [i1, i2, i3]].into_iter().map(orient).collect();
    for (k, q) in p.iter().enumerate() {
        let sees = |f: &[usize; 3]| (p[f[1]] - p[f[0]]).cross(p[f[2]] - p[f[0]]).dot(*q - p[f[0]]) > eps * (p[f[1]] - p[f[0]]).cross(p[f[2]] - p[f[0]]).length();
        let (seen, kept): (Vec<[usize; 3]>, Vec<[usize; 3]>) = faces.iter().partition(|f| sees(f));
        if seen.is_empty() {
            continue;
        }
        // the horizon: edges of the faces it sees whose other side it does not see
        let edges: std::collections::HashSet<(usize, usize)> = seen.iter().flat_map(|f| [(f[0], f[1]), (f[1], f[2]), (f[2], f[0])]).collect();
        faces = kept;
        for &(a, b) in &edges {
            if !edges.contains(&(b, a)) {
                faces.push([a, b, k]);
            }
        }
    }
    let v: f64 = faces.iter().map(|f| (p[f[0]] - inside).dot((p[f[1]] - inside).cross(p[f[2]] - inside))).sum();
    (v / 6.0).abs() as f32
}

impl BodyDef {
    /// The body of a template with this model's bounds (`min`, `max`) and convex hull volume, and its property set.
    /// Mass (Object_BuildRadiusCollision 0x004527d0; fx objects build with cinfo +0xc set):
    /// - a `collide_poly` template (scale-table flag 1): the compound of its collision model's parts (0x00456c40 ->
    ///   Body_BuildShape 0x00413450 with its hull flag), whose Havok mass properties come from the shape with the
    ///   object's density: density x the model's hull volume (`hull_volume`), at most the sphere's;
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
        BodyDef { centre, half, mass, friction: p.f("friction", 0.2), restitution: p.f("restitution", 0.2), hull: None }
    }

    /// The body's shape is the convex hull of these model-space points (game units).
    pub fn with_hull(mut self, points: &[Vec3], meters_per_unit: f32) -> Self {
        let m: Vec<_> = points.iter().map(|v| jv(*v * meters_per_unit)).collect();
        self.hull = Shape::new_convex_hull(&m).ok().map(Arc::new);
        self
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
    let def = o.body_def.clone();
    let scale = o.scale_at_now();
    // the model's hull at the object's scale, else its bounding box round its centre
    let shape = match def.hull.as_ref().and_then(|h| Shape::new_scaled(h, jv(Vec3::splat(scale))).ok()) {
        Some(s) => s,
        None => {
            let (centre, half) = def.scaled(scale);
            let cube = Shape::new_box(jv(half.max(Vec3::splat(0.02)) * k)).ok()?;
            Shape::new_compound(&[CompoundChild { shape: &cube, position: jv(centre * k), rotation: jq(Quat::IDENTITY), user_data: 0 }]).ok()?
        }
    };
    // friction 1: the contact rule's friction scales the combination (contacts.rs)
    let s = BodySettings::new_dynamic()
        .mass(def.mass)
        .friction(1.0)
        .restitution(def.restitution)
        .gravity_factor(physics.game_gravity_factor(gravity.max(0.0)))
        .motion_quality(MotionQuality::LinearCast);
    let id = physics.add_dynamic(e, &shape, pos, rot, Vec3::ZERO, s)?;
    // fx bodies pass through each other: a flying keg goes straight up through the debris of the house its chain
    // blew up (the player: "they went UP 90 degrees ALWAYS"; whether Havok paired two fx bodies is not traced: guess)
    // an object made inside a collider (the flying keg beside a wall, debris inside its building's ruin) passes through
    // that one rather than being thrown out of it sideways (the landscape excepted: it lifts the piece onto it)
    let inside = {
        let q = CollideShape::new(&shape, physics.to_m(pos), jq(rot));
        let hits = physics.world.collide_shape(&q, &QueryFilter::new().exclude_body(id)).unwrap_or_default();
        hits.iter().map(|h| h.body).find(|b| Some(*b) != physics.landscape && !physics.bodies.contains_key(b))
    };
    physics.set_contact_rule(id, crate::contacts::ContactRule { group: Some(FX_BODIES), ignore: inside, friction: def.friction });
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

/// The extent of the body's box below its centre along `down` (engine axes): sum over the box's axes of |axis . down|
/// x half size.
fn support(rot: Quat, half: Vec3, down: Vec3) -> f32 {
    (rot * Vec3::X).dot(down).abs() * half.x + (rot * Vec3::Y).dot(down).abs() * half.y + (rot * Vec3::Z).dot(down).abs() * half.z
}

/// A contact of the fx body in the last physics step (the Havok contact callback FxObject slot 0x2a, 0x00419330, from
/// the solver's contacts: contacts.rs notes them for every ruled body). Returns (game-space contact point: the box's
/// face towards the other body, the body touched).
pub fn touching(physics: &Physics, id: BodyId, o: &FxObject) -> Option<(Vec3, BodyId)> {
    let t = physics.touches(id).into_iter().next()?;
    let (centre, _) = centre_and_velocity(physics, id, o)?;
    let rot = physics.world.body(id).ok().map(|b| crate::physics::bq(b.rotation()))?;
    let (_, half) = o.body_def.scaled(o.scale_now);
    // the normal points out of the other body, towards this one
    let at = centre - t.normal * support(rot, half, t.normal);
    Some((game_to_engine().inverse() * at, t.other))
}

/// How an fx body sits (the playtests' check that debris lands and settles): its velocity (game axes, units/s) and,
/// once Jolt has put it to sleep (at rest for half a second), the gap between the bottom of its box and whatever is
/// below it (game units; None while awake or when nothing is within 200 units).
pub fn rest(physics: &Physics, id: BodyId, o: &FxObject) -> Option<(Vec3, Option<f32>)> {
    let (centre, v) = centre_and_velocity(physics, id, o)?;
    let vg = game_to_engine().inverse() * v;
    let (asleep, rot) = physics.world.body(id).ok().map(|b| (!b.is_active(), crate::physics::bq(b.rotation())))?;
    if !asleep {
        return Some((vg, None));
    }
    let k = physics.meters_per_unit;
    let (_, half) = o.body_def.scaled(o.scale_now);
    let below = support(rot, half, Vec3::NEG_Y);
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
    /// density x the hull's volume; an explicit mass wins.
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
    fn hull_volumes() {
        // a 2-unit cube's corners, its centre and points on its faces (inside the hull)
        let mut v: Vec<Vec3> = (0..8).map(|i| Vec3::new((i & 1) as f32, ((i >> 1) & 1) as f32, ((i >> 2) & 1) as f32) * 2.0).collect();
        v.extend([Vec3::ONE, Vec3::new(1.0, 1.0, 0.0), Vec3::new(0.5, 2.0, 1.5)]);
        assert!((hull_volume(&v) - 8.0).abs() < 1e-3, "{}", hull_volume(&v));
        // a tetrahedron: 1/6
        assert!((hull_volume(&[Vec3::ZERO, Vec3::X, Vec3::Y, Vec3::Z]) - 1.0 / 6.0).abs() < 1e-5);
        // flat: 0
        assert_eq!(hull_volume(&[Vec3::ZERO, Vec3::X, Vec3::Y, Vec3::ONE.with_z(0.0)]), 0.0);
        // a sphere's sampled points approach 4/3 pi
        let s: Vec<Vec3> = (0..400).map(|i| {
            let (a, b) = (i as f32 * 2.399963, (1.0 - 2.0 * (i as f32 + 0.5) / 400.0).acos());
            Vec3::new(b.sin() * a.cos(), b.sin() * a.sin(), b.cos())
        }).collect();
        assert!((hull_volume(&s) - 4.18879).abs() < 0.1, "{}", hull_volume(&s));
    }
}
