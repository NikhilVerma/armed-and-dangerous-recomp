//! Jolt Physics (5.6, through oxijolt) for the engine: a world stepped at a fixed 60 Hz, static triangle-mesh
//! colliders for the level, dynamic bodies whose poses drive their entities' transforms.
//! Game-neutral: positions in the engine are game units; Jolt works in metres (`Physics::meters_per_unit`).

use bevy::prelude::*;
use oxijolt::prelude::math as j;
use oxijolt::prelude::*;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Instant;

pub use crate::contacts::{ContactRule, Touch};


pub struct Physics {
    pub world: PhysicsWorld,
    pub meters_per_unit: f32,
    /// Dynamic bodies and the entity each one moves.
    pub bodies: HashMap<BodyId, Entity>,
    pub static_bodies: usize,
    /// bodies projectiles pass through: objects not in the projectile list (World +0xe90) and invisible_material
    pub no_bullets: std::collections::HashSet<BodyId>,
    /// the game material of a body (property `material`: snow, stone, wood, metal, grunt, ...) for impact fx
    pub materials: HashMap<BodyId, String>,
    /// a static mesh body's surface names per triangle (surface.rs)
    pub surfaces: HashMap<BodyId, crate::surface::SurfaceMap>,
    pub static_triangles: usize,
    /// the landscape's collision body (the follow camera's rays stop on it alone, camfade.rs)
    pub landscape: Option<BodyId>,
    /// the AI characters' bodies (the AI list World+0xe7c): the AI's sight rays pass through them (0x00426fd0 excludes
    /// the looker's list +0x608, which is World+0xe7c for a grunt)
    pub ai_bodies: std::collections::HashSet<BodyId>,
    /// the volumes bullets hit instead of a body (a character's collision model round its physics sphere), by body
    pub hit_volumes: HashMap<BodyId, HitVolume>,
    /// the game's contact rules and the contacts of the ruled bodies (contacts.rs), shared with the solver
    pub contacts: Arc<crate::contacts::Contacts>,
    /// CPU time of the last steps (ms), for the benchmark.
    pub step_ms: f32,
    poses: Vec<BodyPose>,
}

pub fn jv(v: Vec3) -> j::Vec3 {
    j::Vec3::new(v.x, v.y, v.z)
}
pub fn jr(v: Vec3) -> j::RVec3 {
    j::RVec3::new(v.x as _, v.y as _, v.z as _)
}
pub fn jq(q: Quat) -> j::Quat {
    j::Quat { x: q.x, y: q.y, z: q.z, w: q.w }
}
pub fn bv(v: j::Vec3) -> Vec3 {
    Vec3::new(v.x, v.y, v.z)
}
pub fn br(v: j::RVec3) -> Vec3 {
    Vec3::new(v.x as f32, v.y as f32, v.z as f32)
}
pub fn bq(q: j::Quat) -> Quat {
    Quat::from_xyzw(q.x, q.y, q.z, q.w)
}

impl Physics {
    pub fn new(meters_per_unit: f32) -> Self {
        let mut world = PhysicsWorld::new(WorldSettings::default().gravity(j::Vec3::new(0.0, -9.81, 0.0)))
            .expect("Jolt world");
        let contacts = Arc::new(crate::contacts::Contacts::default());
        world.set_contact_listener(Some(Arc::new(crate::contacts::ContactListenerImpl(contacts.clone()))));
        Physics {
            world,
            meters_per_unit,
            bodies: HashMap::new(),
            static_bodies: 0,
            no_bullets: Default::default(),
            materials: HashMap::new(),
            surfaces: HashMap::new(),
            static_triangles: 0,
            landscape: None,
            ai_bodies: Default::default(),
            hit_volumes: HashMap::new(),
            contacts,
            step_ms: 0.0,
            poses: Vec::new(),
        }
    }

    pub fn to_m(&self, p: Vec3) -> j::RVec3 {
        jr(p * self.meters_per_unit)
    }
    pub fn from_m(&self, p: j::RVec3) -> Vec3 {
        br(p) / self.meters_per_unit
    }

    /// A static triangle mesh, vertices already in engine space (game units). Friction 1.0: the original's default
    /// material (PhysMaterial_Load 0x00412140: "%s_friction", else default_friction, 1.0 on every level; wood's 20 is
    /// not modelled). Jolt's solver collides with triangles from the front only, Havok's MOPP meshes from both sides:
    /// a turned-round copy of the landscape's downward-facing triangles was tried for B3b (docs/journal/2026-10-16-charbody.md)
    /// and dropped: it did not hold the grunts and made lev_1_m1's frames ~12% slower.
    pub fn add_static_mesh(&mut self, vertices: &[Vec3], triangles: &[[u32; 3]]) -> Option<BodyId> {
        let k = self.meters_per_unit;
        let v: Vec<j::Vec3> = vertices.iter().map(|p| jv(*p * k)).collect();
        let (shape, _dropped) = Shape::new_mesh(&v, triangles).ok()?;
        let id = self.world.create_body(&shape, &BodySettings::new_static().friction(1.0)).ok()?;
        self.static_bodies += 1;
        self.static_triangles += triangles.len();
        Some(id)
    }

    /// A dynamic body for `entity`. `shape` is in metres around the body origin.
    pub fn add_dynamic(
        &mut self,
        entity: Entity,
        shape: &Shape,
        position: Vec3,
        rotation: Quat,
        velocity: Vec3,
        settings: BodySettings,
    ) -> Option<BodyId> {
        let s = settings
            .position(self.to_m(position))
            .rotation(jq(rotation))
            .linear_velocity(jv(velocity * self.meters_per_unit));
        let id = self.world.create_body(shape, &s).ok()?;
        self.bodies.insert(id, entity);
        Some(id)
    }

    pub fn remove_static(&mut self, id: BodyId) {
        self.surfaces.remove(&id);
        if self.world.remove_body(id).is_ok() {
            self.static_bodies = self.static_bodies.saturating_sub(1);
        }
    }

    pub fn remove(&mut self, id: BodyId) {
        self.bodies.remove(&id);
        self.ai_bodies.remove(&id);
        self.contacts.remove(id);
        self.hit_volumes.remove(&id);
        let _ = self.world.remove_body(id);
    }

    /// Radial impulse from `center` (game units): bodies within `radius` get `strength` (N s at the centre,
    /// falling off linearly), pushed away and upwards.
    pub fn explode(&mut self, center: Vec3, radius: f32, strength: f32) -> Vec<(BodyId, f32)> {
        let mut hit = Vec::new();
        let ids: Vec<BodyId> = self.bodies.keys().copied().collect();
        for id in ids {
            let Ok(b) = self.world.body(id) else { continue };
            let p = self.from_m(b.position());
            let d = p - center;
            let dist = d.length();
            if dist > radius {
                continue;
            }
            let falloff = 1.0 - dist / radius;
            let mass = b.mass().unwrap_or(1.0);
            drop(b);
            let dir = (d.normalize_or(Vec3::Y) + Vec3::Y * 0.6).normalize();
            if let Ok(mut m) = self.world.body_mut(id) {
                m.activate();
                let _ = m.add_impulse(jv(dir * strength * falloff * mass.min(200.0) / 50.0));
            }
            hit.push((id, falloff));
        }
        hit
    }

    /// A projectile's ray (Projectile_Update 0x0047e580): as raycast, through the bodies bullets pass, and into the
    /// hit volumes (the nearer of the two).
    pub fn raycast_bullet(&self, from: Vec3, dir: Vec3, length: f32) -> Option<(BodyId, Vec3, Vec3)> {
        let d = dir.normalize_or(Vec3::Y);
        let world = self.raycast_bullet_bodies(from, d, length);
        let reach = world.map_or(length, |h| h.1.distance(from));
        let volume = self
            .hit_volumes
            .iter()
            .filter_map(|(&id, v)| {
                let centre = self.from_m(self.world.body(id).ok()?.position());
                let (t, at, normal) = v.ray(centre, from, d, reach)?;
                Some((t, id, at, normal))
            })
            .min_by(|a, b| a.0.total_cmp(&b.0));
        volume.map(|(_, id, at, n)| (id, at, n)).or(world)
    }

    fn raycast_bullet_bodies(&self, from: Vec3, d: Vec3, length: f32) -> Option<(BodyId, Vec3, Vec3)> {
        let (mut from, mut left) = (from, length);
        for _ in 0..16 {
            let h = self.raycast(from, d, left)?;
            if !self.no_bullets.contains(&h.0) {
                return Some(h);
            }
            left -= h.1.distance(from) + 0.05;
            if left <= 0.0 {
                return None;
            }
            from = h.1 + d * 0.05;
        }
        None
    }

    /// The ground under a point as Object_ForceGround (0x00455860) finds it, the hit point: its ray (0x004137e0 ->
    /// Ray_CastQuery 0x00414110) has a fresh include stamp and no include list, so the collector (0x00413a90) keeps
    /// only bodies without an owner object (+0x78 set): the landscape. Objects over the point are passed through (grunt
    /// 181 stands under the archway v_archway_a: the original puts him on the ground, not on the arch at z 108). Without
    /// a landscape body, the first hit.
    pub fn raycast_landscape(&self, from: Vec3, dir: Vec3, length: f32) -> Option<Vec3> {
        let d = dir.normalize_or(Vec3::NEG_Y);
        let (mut from, mut left) = (from, length);
        for _ in 0..64 {
            let (body, at, _) = self.raycast(from, d, left)?;
            if self.landscape.is_none_or(|l| l == body) {
                return Some(at);
            }
            left -= at.distance(from) + 0.05;
            if left <= 0.0 {
                return None;
            }
            from = at + d * 0.05;
        }
        None
    }

    /// What a point of a body is made of (surface.rs): its mesh surface there and its material property.
    pub fn surface(&self, body: BodyId, at: Vec3) -> crate::surface::Surface {
        crate::surface::Surface {
            mesh: self.surfaces.get(&body).and_then(|m| m.at(at)).map(str::to_owned),
            property: self.materials.get(&body).cloned(),
        }
    }

    /// The surface under a point (a foot): the first static body a ray down from 2 units above meets within `depth`
    /// below, past the dynamic bodies (the character's own and others': the original asks the character's own
    /// ground contact, 0x00454b20). None over nothing.
    pub fn surface_below(&self, at: Vec3, depth: f32) -> Option<crate::surface::Surface> {
        let k = self.meters_per_unit;
        let (mut from, mut left, mut skip) = (at + Vec3::Y * 2.0, depth + 2.0, None);
        for _ in 0..8 {
            // past the last dynamic body met (a ray that starts inside one hits it at once)
            let filter = skip.map_or(QueryFilter::new(), |b| QueryFilter::new().exclude_body(b));
            let hit = self.world.cast_ray(&RayCast::new(jr(from * k), jv(Vec3::NEG_Y * left * k)), &filter).ok()??;
            let point = from + Vec3::NEG_Y * hit.distance / k;
            if !self.bodies.contains_key(&hit.body) {
                return Some(self.surface(hit.body, point));
            }
            left -= from.y - point.y;
            from = point;
            skip = Some(hit.body);
        }
        None
    }

    /// Object_ForceGround (0x00455860): drops a placement onto the landscape. A ray from `lift` above `at`, `range`
    /// long, that sees only the landscape (`raycast_landscape`; like Havok's it hits triangles from behind too, so a
    /// point under an overhang lands on the overhang's underside first); the point goes 0.1 above the hit. Then, when
    /// the object's collision sphere (`sphere`: its radius, the centre that far above the point: a character's body,
    /// 0x004527d0) touches the landscape (0x00455720: the closest distance within the collision tolerance), a
    /// vertical bisection lifts it out: up by `step` (the object's bounding radius, vt+0x90) while touching, down while
    /// free, the step halved at each change, until the eleventh change; it ends at the last free height + 0.1
    /// (lev_5_m1 captain 66: the cliff's underside at 230, out beside its lip at 298). Without a sphere only the ray
    /// applies.
    pub fn force_ground(&self, at: Vec3, lift: f32, range: f32, sphere: Option<f32>, step: f32) -> Option<Vec3> {
        let hit = self.raycast_landscape(at + Vec3::Y * lift, -Vec3::Y, range)?;
        let mut p = at.with_y(hit.y + 0.1);
        let Some(radius) = sphere.filter(|r| *r > 0.0) else { return Some(p) };
        let Ok(shape) = Shape::new_sphere(radius * self.meters_per_unit) else { return Some(p) };
        let touches = |p: Vec3| self.touches_landscape(&shape, p + Vec3::Y * radius);
        let mut now = touches(p);
        if !now {
            return Some(p);
        }
        let (mut step, mut changes, mut previous, mut free) = (step, 0, now, p.y);
        loop {
            if now {
                p.y += step;
            } else {
                free = p.y;
                p.y -= step;
            }
            if previous != now {
                step *= 0.5;
                changes += 1;
            }
            if changes > 10 {
                break;
            }
            previous = now;
            now = touches(p);
        }
        Some(p.with_y(free + 0.1))
    }

    /// The shape at `centre` (unrotated) touches the landscape: overlaps it or comes within the collision tolerance,
    /// from either side of its triangles. The tolerance is 0.3: the game's world tolerance 0.2 (world cinfo +0x40, Havok's
    /// default 0.1) and the mesh's own radius 0.1 (mesh shape +0x20): the original lifts lev_5_m3 grunt 44 / lev_5_m1
    /// captain 66 to 0.31 / 0.32 off the overhang (their traced z 315.99 / 297.89; 0.1 gave 0.11 / 0.12, z 1.4 / 0.9 lower).
    fn touches_landscape(&self, shape: &Shape, centre: Vec3) -> bool {
        let Some(landscape) = self.landscape else { return false };
        let k = self.meters_per_unit;
        let q = CollideShape::new(shape, self.to_m(centre), j::Quat::IDENTITY).max_separation_distance(0.3 * k).collide_with_back_faces(true);
        self.world.collide_shape(&q, &QueryFilter::new()).is_ok_and(|hits| hits.iter().any(|h| h.body == landscape))
    }

    /// Sets a body's contact rule (contacts.rs); a changed rule drops the body's cached contacts, so the solver asks
    /// again.
    pub fn set_contact_rule(&mut self, id: BodyId, rule: ContactRule) {
        if self.contacts.set_rule(id, rule) {
            if let Ok(mut b) = self.world.body_mut(id) {
                b.invalidate_contact_cache();
            }
        }
    }

    /// The contacts a ruled body made in the last step (contacts.rs).
    pub fn touches(&self, id: BodyId) -> Vec<Touch> {
        self.contacts.touches(id)
    }

    pub fn raycast(&self, from: Vec3, dir: Vec3, length: f32) -> Option<(BodyId, Vec3, Vec3)> {
        let k = self.meters_per_unit;
        let ray = RayCast::new(jr(from * k), jv(dir.normalize() * length * k));
        let hit = self.world.cast_ray(&ray, &QueryFilter::new()).ok()??;
        Some((hit.body, from + dir.normalize() * hit.distance / k, bv(hit.normal)))
    }
}

/// What bullets hit instead of a body: a vertical capsule round the body's centre (game units, engine axes): its
/// radius and its segment's ends above the centre.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct HitVolume {
    pub radius: f32,
    pub bottom: f32,
    pub top: f32,
}

impl HitVolume {
    /// Where a ray from `from` along the unit `d` first enters the capsule round `centre`, within `reach`: the distance,
    /// the point and the outward normal there. None from inside it or past `reach`.
    pub fn ray(&self, centre: Vec3, from: Vec3, d: Vec3, reach: f32) -> Option<(f32, Vec3, Vec3)> {
        let (a, b) = (centre + Vec3::Y * self.bottom, centre + Vec3::Y * self.top);
        let r2 = self.radius * self.radius;
        let axis_point = |p: Vec3| a.with_y(p.y.clamp(a.y, b.y));
        if (from - axis_point(from)).length_squared() <= r2 {
            return None;
        }
        // the side (an infinite vertical cylinder), then the two end spheres: the first entry that lies on the capsule
        let mut best: Option<f32> = None;
        let mut take = |t: f32| {
            if t >= 0.0 && t <= reach && best.is_none_or(|b| t < b) {
                best = Some(t);
            }
        };
        let (o, dd) = ((from - a).with_y(0.0), d.with_y(0.0));
        let (qa, qb, qc) = (dd.length_squared(), 2.0 * o.dot(dd), o.length_squared() - r2);
        if qa > 1e-9 {
            let disc = qb * qb - 4.0 * qa * qc;
            if disc >= 0.0 {
                let t = (-qb - disc.sqrt()) / (2.0 * qa);
                let y = from.y + d.y * t;
                if (a.y..=b.y).contains(&y) {
                    take(t);
                }
            }
        }
        for c in [a, b] {
            let oc = from - c;
            let (hb, hc) = (oc.dot(d), oc.length_squared() - r2);
            let disc = hb * hb - hc;
            if disc >= 0.0 {
                take(-hb - disc.sqrt());
            }
        }
        let t = best?;
        let at = from + d * t;
        Some((t, at, (at - axis_point(at)).normalize_or(-d)))
    }
}

/// An entity whose transform follows a Jolt body. With `sync_rotation`, the entity takes the body's rotation
/// and `offset` is the body origin in the entity's own space (game units); without it (upright characters,
/// whose facing the game sets), only the position follows and `offset` is in engine space.
#[derive(Component)]
pub struct Body {
    pub id: BodyId,
    pub offset: Vec3,
    pub sync_rotation: bool,
}

impl Body {
    pub fn rigid(id: BodyId) -> Self {
        Body { id, offset: Vec3::ZERO, sync_rotation: true }
    }
}

pub struct PhysicsPlugin;

impl Plugin for PhysicsPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(Time::<Fixed>::from_hz(60.0))
            .add_systems(FixedUpdate, step)
            .add_systems(PostUpdate, sync.before(TransformSystems::Propagate));
    }
}

fn step(mut physics: NonSendMut<Physics>, time: Res<Time<Fixed>>) {
    let t = Instant::now();
    // the contact notes are the last step's (contacts.rs)
    physics.contacts.clear();
    let _ = physics.world.step(time.delta_secs());
    physics.step_ms = t.elapsed().as_secs_f32() * 1000.0;
}

fn sync(mut physics: NonSendMut<Physics>, mut q: Query<(&Body, &mut Transform)>) {
    let mut poses = std::mem::take(&mut physics.poses);
    physics.world.active_body_poses_into(&mut poses);
    let k = physics.meters_per_unit;
    for pose in &poses {
        let Some(&e) = physics.bodies.get(&pose.id) else { continue };
        if let Ok((body, mut t)) = q.get_mut(e) {
            if body.sync_rotation {
                let rot = bq(pose.rotation);
                t.rotation = rot;
                t.translation = br(pose.position) / k - rot * (body.offset * t.scale);
            } else {
                t.translation = br(pose.position) / k - body.offset;
            }
        }
    }
    physics.poses = poses;
}

/// A walking character (Jolt CharacterVirtual): a capsule moved by velocity that climbs steps, sticks to
/// floors and slides along walls. Feet at the entity origin; positions in game units, engine axes.
pub struct Walker {
    pub id: CharacterId,
    /// capsule centre above the feet (game units)
    pub lift: f32,
}

impl Physics {
    /// `max_slope`: the steepest walkable ground (radians); steeper ground slides the walker down.
    pub fn add_walker(&mut self, feet: Vec3, radius: f32, height: f32, max_slope: f32) -> Option<Walker> {
        let k = self.meters_per_unit;
        let r = radius * k;
        let half = ((height * k) * 0.5 - r).max(0.05);
        let shape = Shape::new_capsule(half, r).ok()?;
        let lift = height * 0.5;
        let settings = CharacterSettings::new(&shape)
            .shape_offset(j::Vec3::new(0.0, lift * k, 0.0))
            .max_slope_angle(max_slope)
            .mass(80.0);
        let id = self.world.create_character(&settings, self.to_m(feet), j::Quat { x: 0.0, y: 0.0, z: 0.0, w: 1.0 }).ok()?;
        let _ = self.world.refresh_character_contacts(id, &QueryFilter::new());
        Some(Walker { id, lift })
    }

    /// Moves a walker with `velocity` (game units/s, engine axes) for `dt`; gravity is the caller's (it is part
    /// of the velocity). Returns the new feet position and whether it stands on ground.
    pub fn move_walker(&mut self, w: &Walker, velocity: Vec3, dt: f32) -> Option<(Vec3, bool)> {
        let k = self.meters_per_unit;
        // no time, no move (a paused game: the update used to run 1e-4 s anyway, and Roman crept on through a
        // briefing at 0.004 units a frame)
        if dt > 0.0 {
            self.world.character_mut(w.id).ok()?.set_linear_velocity(jv(velocity * k)).ok()?;
            let g = j::Vec3::new(0.0, -9.81, 0.0);
            self.world.update_character(w.id, dt.min(0.1), g, &ExtendedUpdateSettings::default(), &QueryFilter::new()).ok()?;
        }
        let c = self.world.character(w.id).ok()?;
        let on = matches!(c.ground_state(), oxijolt::GroundState::OnGround);
        Some((br(c.position()) / k, on))
    }

    /// Whether a walker touches nothing below it (not even ground too steep to stand on).
    pub fn walker_in_air(&self, w: &Walker) -> bool {
        self.world.character(w.id).map(|c| matches!(c.ground_state(), oxijolt::GroundState::InAir)).unwrap_or(false)
    }

    /// Puts a walker's feet at `feet` (game units, engine axes): a teleport (leaving the pub at its exit).
    pub fn place_walker(&mut self, w: &Walker, feet: Vec3) {
        let p = self.to_m(feet);
        if let Ok(mut c) = self.world.character_mut(w.id) {
            let _ = c.set_position(p);
        }
    }

    pub fn remove_walker(&mut self, w: &Walker) {
        let _ = self.world.remove_character(w.id);
    }
}
