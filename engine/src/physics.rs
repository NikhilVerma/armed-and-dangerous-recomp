//! Jolt Physics (5.6, through oxijolt) for the engine: a world stepped at a fixed 60 Hz, static triangle-mesh
//! colliders for the level, dynamic bodies whose poses drive their entities' transforms.
//! Game-neutral: positions in the engine are game units; Jolt works in metres (`Physics::meters_per_unit`).

use bevy::prelude::*;
use oxijolt::prelude::math as j;
use oxijolt::prelude::*;
use std::collections::HashMap;
use std::time::Instant;

pub struct Physics {
    pub world: PhysicsWorld,
    pub meters_per_unit: f32,
    /// Dynamic bodies and the entity each one moves.
    pub bodies: HashMap<BodyId, Entity>,
    pub static_bodies: usize,
    /// the game material of a body (property `material`: snow, stone, wood, metal, grunt, ...) for impact fx
    pub materials: HashMap<BodyId, String>,
    pub static_triangles: usize,
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
        let world = PhysicsWorld::new(WorldSettings::default().gravity(j::Vec3::new(0.0, -9.81, 0.0)))
            .expect("Jolt world");
        Physics {
            world,
            meters_per_unit,
            bodies: HashMap::new(),
            static_bodies: 0,
            materials: HashMap::new(),
            static_triangles: 0,
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

    /// A static triangle mesh, vertices already in engine space (game units).
    pub fn add_static_mesh(&mut self, vertices: &[Vec3], triangles: &[[u32; 3]]) -> Option<BodyId> {
        let k = self.meters_per_unit;
        let v: Vec<j::Vec3> = vertices.iter().map(|p| jv(*p * k)).collect();
        let (shape, _dropped) = Shape::new_mesh(&v, triangles).ok()?;
        let id = self.world.create_body(&shape, &BodySettings::new_static()).ok()?;
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
        if self.world.remove_body(id).is_ok() {
            self.static_bodies = self.static_bodies.saturating_sub(1);
        }
    }

    pub fn remove(&mut self, id: BodyId) {
        self.bodies.remove(&id);
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

    pub fn raycast(&self, from: Vec3, dir: Vec3, length: f32) -> Option<(BodyId, Vec3, Vec3)> {
        let k = self.meters_per_unit;
        let ray = RayCast::new(jr(from * k), jv(dir.normalize() * length * k));
        let hit = self.world.cast_ray(&ray, &QueryFilter::new()).ok()??;
        Some((hit.body, from + dir.normalize() * hit.distance / k, bv(hit.normal)))
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
    pub fn add_walker(&mut self, feet: Vec3, radius: f32, height: f32) -> Option<Walker> {
        let k = self.meters_per_unit;
        let r = radius * k;
        let half = ((height * k) * 0.5 - r).max(0.05);
        let shape = Shape::new_capsule(half, r).ok()?;
        let lift = height * 0.5;
        let settings = CharacterSettings::new(&shape)
            .shape_offset(j::Vec3::new(0.0, lift * k, 0.0))
            .max_slope_angle(50f32.to_radians())
            .mass(80.0);
        let id = self.world.create_character(&settings, self.to_m(feet), j::Quat { x: 0.0, y: 0.0, z: 0.0, w: 1.0 }).ok()?;
        let _ = self.world.refresh_character_contacts(id, &QueryFilter::new());
        Some(Walker { id, lift })
    }

    /// Moves a walker with `velocity` (game units/s, engine axes) for `dt`; gravity is the caller's (it is part
    /// of the velocity). Returns the new feet position and whether it stands on ground.
    pub fn move_walker(&mut self, w: &Walker, velocity: Vec3, dt: f32) -> Option<(Vec3, bool)> {
        let k = self.meters_per_unit;
        self.world.character_mut(w.id).ok()?.set_linear_velocity(jv(velocity * k)).ok()?;
        let g = j::Vec3::new(0.0, -9.81, 0.0);
        self.world.update_character(w.id, dt.clamp(1e-4, 0.1), g, &ExtendedUpdateSettings::default(), &QueryFilter::new()).ok()?;
        let c = self.world.character(w.id).ok()?;
        let on = matches!(c.ground_state(), oxijolt::GroundState::OnGround);
        Some((br(c.position()) / k, on))
    }

    pub fn remove_walker(&mut self, w: &Walker) {
        let _ = self.world.remove_character(w.id);
    }
}
