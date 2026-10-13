//! Grunt AI, STAND-IN: the original AI is recovered C++ (src/ai/*) and replaces this when it is linked in.
//! Until then this drives grunts with the game's own parameters from their property sets: `patrol_speed`,
//! `move_speed`, `scan_distance`, `fire_range_min/max`, `sticky_scare_dist`, `collision_radius`, `max_health`.
//! Bodies are Jolt capsules that only translate; a grunt whose health runs out tumbles as a rigid body.

use crate::fx::{Explode, FxStats, Health};
use crate::physics::{Body, Physics, bv, jv};
use bevy::prelude::*;
use oxijolt::prelude::*;
use std::time::Instant;

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Mind {
    Patrol,
    Chase,
    Attack,
    Flee,
    Dead,
}

#[derive(Component)]
pub struct Grunt {
    pub mind: Mind,
    pub home: Vec3,
    pub goal: Vec3,
    pub timer: f32,
    pub fire_timer: f32,
    /// The model's rest rotation (game axes to engine axes) under the facing yaw.
    pub base: Quat,
    pub yaw: f32,
    pub move_speed: f32,
    pub patrol_speed: f32,
    pub scan_distance: f32,
    pub fire_range: (f32, f32),
    pub scare_distance: f32,
}

/// What the AI targets: the camera (the player) for the MVP.
#[derive(Component)]
pub struct Player;

#[derive(Resource, Default)]
pub struct AiStats {
    pub ms: f32,
    pub alive: usize,
    pub dead: usize,
    pub shots: usize,
    pub enabled: bool,
}

pub struct AiPlugin;

impl Plugin for AiPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(AiStats { enabled: true, ..default() })
            .add_systems(Update, (scare, think, die).chain().after(crate::fx::FxSet));
    }
}

/// Capsule of a character with the game's `collision_radius`, as tall as a grunt (9.8 units).
pub fn capsule(physics: &Physics, radius: f32) -> Shape {
    let k = physics.meters_per_unit;
    let r = radius.min(4.5) * 0.55;
    Shape::new_capsule((4.9 - r).max(0.5) * k, r * k).expect("capsule")
}

/// Height of the capsule's centre above the feet.
pub fn capsule_lift(_radius: f32) -> f32 {
    4.9 + 0.3
}

/// Grunts run from blasts near them (sticky_scare_dist; guess: scaled by the blast's outer radius).
fn scare(mut blasts: MessageReader<Explode>, mut grunts: Query<(&mut Grunt, &GlobalTransform)>) {
    for b in blasts.read() {
        for (mut g, t) in &mut grunts {
            if g.mind == Mind::Dead {
                continue;
            }
            let d = t.translation() - b.at;
            if d.length() < b.radius_outer + g.scare_distance * 2.0 {
                g.mind = Mind::Flee;
                g.goal = t.translation() + d.normalize_or(Vec3::X) * 80.0;
                g.timer = 2.5;
            }
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn think(
    mut commands: Commands,
    time: Res<Time>,
    mut stats: ResMut<AiStats>,
    mut rng: ResMut<FxStats>,
    mut physics: NonSendMut<Physics>,
    player: Query<&GlobalTransform, With<Player>>,
    mut grunts: Query<(&mut Grunt, &Body, &mut Transform)>,
) {
    let t0 = Instant::now();
    let dt = time.delta_secs();
    let target = player.single().map(|p| p.translation()).unwrap_or(Vec3::ZERO);
    let k = physics.meters_per_unit;
    let (mut alive, mut dead) = (0, 0);
    for (mut g, body, mut tr) in &mut grunts {
        if g.mind == Mind::Dead {
            dead += 1;
            continue;
        }
        alive += 1;
        if !stats.enabled {
            continue;
        }
        let pos = tr.translation;
        let flat = |v: Vec3| Vec3::new(v.x, 0.0, v.z);
        let to_player = flat(target - pos);
        let dist = to_player.length();
        g.timer -= dt;
        g.fire_timer -= dt;
        g.mind = match g.mind {
            Mind::Flee if g.timer > 0.0 => Mind::Flee,
            _ if dist < g.fire_range.0.max(55.0) => Mind::Attack,
            _ if dist < g.scan_distance => Mind::Chase,
            _ => Mind::Patrol,
        };
        let (goal, speed) = match g.mind {
            Mind::Patrol => {
                if g.timer <= 0.0 || flat(g.goal - pos).length() < 4.0 {
                    let a = rng.rand() * std::f32::consts::TAU;
                    g.goal = g.home + Vec3::new(a.cos(), 0.0, a.sin()) * rng.range(8.0, 40.0);
                    g.timer = rng.range(3.0, 7.0);
                }
                (g.goal, g.patrol_speed)
            }
            Mind::Chase => (target, g.move_speed * 0.6),
            Mind::Flee => (g.goal, g.move_speed),
            Mind::Attack => {
                if g.fire_timer <= 0.0 {
                    g.fire_timer = rng.range(0.7, 1.6);
                    // the grunt rifle's projectile (speed 150, damage 15: proj values, docs/subsystems/player.md);
                    // aim error grows with distance (stand-in for the fire_accuracy rules)
                    let muzzle = pos + Vec3::Y * 7.0;
                    let aim_at = target + Vec3::Y * 6.0;
                    let err = (dist / 60.0).max(0.5);
                    let spread = Vec3::new(rng.rand() - 0.5, (rng.rand() - 0.5) * 0.5, rng.rand() - 0.5) * err * 4.0;
                    let dir = (aim_at + spread - muzzle).normalize_or(Vec3::Z);
                    commands.spawn((
                        Transform::from_translation(muzzle + dir * 4.0),
                        crate::player::Projectile { vel: dir * 150.0, life: 500.0 / 150.0, damage: 15.0, from_player: false, owner: None },
                    ));
                    stats.shots += 1;
                }
                (pos, 0.0)
            }
            Mind::Dead => unreachable!(),
        };
        let dir = flat(goal - pos).normalize_or_zero();
        let face = if g.mind == Mind::Attack { to_player.normalize_or_zero() } else { dir };
        if face != Vec3::ZERO {
            let want = face.x.atan2(face.z);
            let d = (want - g.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
            g.yaw += d * (dt * 8.0).min(1.0);
        }
        tr.rotation = Quat::from_rotation_y(g.yaw) * g.base;
        if let Ok(mut b) = physics.world.body_mut(body.id) {
            let v = bv(b.linear_velocity());
            let want = dir * speed * k;
            let _ = b.set_linear_velocity(jv(Vec3::new(want.x, v.y, want.z)));
            if speed > 0.0 {
                b.activate();
            }
        }
    }
    stats.alive = alive;
    stats.dead = dead;
    stats.ms = t0.elapsed().as_secs_f32() * 1000.0;
}

/// A grunt out of health drops its upright capsule and becomes a tumbling rigid body (a box around the model)
/// that later blasts push. (The original plays death animations and ragdolls; not ported yet.)
fn die(mut commands: Commands, mut physics: NonSendMut<Physics>, mut grunts: Query<(Entity, &mut Grunt, &Body, &Transform, &Health)>) {
    for (e, mut g, body, tr, h) in &mut grunts {
        if g.mind == Mind::Dead || h.0 > 0.0 {
            continue;
        }
        g.mind = Mind::Dead;
        let push = physics.world.body(body.id).map(|b| bv(b.linear_velocity())).unwrap_or(Vec3::ZERO) / physics.meters_per_unit;
        physics.remove(body.id);
        let k = physics.meters_per_unit;
        let half = Vec3::new(2.6, 1.6, 4.8) * tr.scale * k;
        let Ok(shape) = Shape::new_box(jv(half)) else { continue };
        let center_local = Vec3::new(0.0, 0.0, 4.8);
        let origin = tr.translation + tr.rotation * (center_local * tr.scale);
        let settings = BodySettings::new_dynamic().mass(80.0).friction(0.8).angular_damping(0.4);
        if let Some(id) = physics.add_dynamic(e, &shape, origin, tr.rotation, push, settings) {
            commands.entity(e).insert(Body { id, offset: center_local, sync_rotation: true });
        }
    }
}
