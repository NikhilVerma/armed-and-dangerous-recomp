//! How the physics world steps and how its bodies are drawn between steps: the old remaster's `[limits]`
//! physics options (hook/enhance/limits.cpp `LimitsInit`, `DrawMatrix`; src/physics/physics_01.cpp
//! `PhysicsManager_Step`), as typed settings ([`PhysicsStepSettings`], part of the graphics presets).
//!
//! - **Stepping** (`physics_fixed_step`). The original splits a frame into 1 + 20 dt substeps (at most 5) of at most
//!   0.05 s each (Game_Update 0x00402e80: Game+0x4c34 / +0x4c30) and raises a step under 1/60 s to 1/60 s
//!   (PhysicsManager_Step 0x00411d40: `if (dt < 1/60) dt = 1/60`), so above 60 fps the world runs fast (x2.41 measured
//!   at 144 Hz, docs/engine-limits.md S1) and a frame over 0.25 s simulates only 0.25 s. Off (the Original preset):
//!   that frame-tied stepping, [`frame_tied_steps`]. On (the Remaster): whole 1/60 s steps from Bevy's fixed timestep, the accumulator the old
//!   remaster added to PhysicsManager_Step, so the world runs at real speed at any frame rate.
//! - **Interpolation** (`physics_interpolation`). With fixed steps above 60 fps a body moves only every few frames.
//!   As the old remaster's `DrawMatrix`: each body's drawn pose is blended between its poses after the last two
//!   steps by how far the frame is into the next step (one step of display lag), only while frames clearly outnumber
//!   steps (more than 6 frames per 5 steps over 30 frames; at 60 fps it would only add lag), never for a body that
//!   moved between steps (teleported, set by the game) in the last 60 frames. Drawing only: the true pose is put back
//!   in `First`, before any game system runs ([`restore_true_poses`]). The old one blended the position only; this one
//!   also turns bodies whose rotation the physics drives (better: spinning debris no longer ticks at 60 Hz).
//! - **Solver iterations** (`physics_solver_iterations`): the Havok world's +0x3c (4, raised to 8 by the old remaster,
//!   WorldCinfo 0x00411960) becomes Jolt's velocity steps per step (`WorldSettings::velocity_steps`; Jolt's default
//!   10, which the engine's physics was tuned with). Read when a level's world is made.
//! - **Each step's side work** (PhysicsManager_Step, per substep): the gravity scales (Physics_ApplyGravityScale
//!   0x00403c70) are each body's Jolt gravity factor (fxvm/body.rs, movable.rs); the force actions (ForceActionPool_Update
//!   0x00404140: impulses and forces spread over a delay and a duration) are applied by their users straight to the Jolt
//!   bodies (fx.rs, fxvm/body.rs, jetpack.rs); the deferred whole-world refilter (0x004ac910 when the manager's +0x60 is
//!   set, after a save is loaded) has no counterpart, Jolt's filters being asked on every query; the step counter
//!   Game+0x700 is [`PhysicsClock::steps`].
//! - **The solver** (PhysicsManager_Init 0x00411960): Havok's tau 0.6 and damping 1.0 and its broad phase's box
//!   (-5000, -5000, -1100)..(5000, 5000, 2000) have no Jolt counterpart (Jolt's solver is its own, and its broad phase
//!   is unbounded); its iterations are `physics_solver_iterations`, its gravity the world's (physics.rs `Physics::new`).
//! - **Body capacity** (`physics_body_capacity`): the old remaster's `force_pool` (ForceActionPool_Init 0x00403ef0:
//!   512 force actions) has no counterpart: the engine applies impulses and forces to Jolt bodies directly, with no
//!   pool. Its intent (the physics' fixed capacity) maps to the one fixed-size table the Jolt world has: its body
//!   count (Jolt's default 10240), with the broad phase's pair buffer and the step's scratch memory grown with it.
//!   Read when a level's world is made (docs/engine-limits.md).
//!
//! API: [`PhysicsStepPlugin`] (added by physics.rs); [`PhysicsClock`] (steps taken, how far into the next step).

use crate::graphics::GraphicsSettings;
use crate::physics::{Body, Physics};
use bevy::prelude::*;
use bevy::time::{Fixed, Virtual};
use serde::{Deserialize, Serialize};

/// The physics step (PhysicsManager_Step's 1/60 s, K 0x005bcc98 / 0x3c888889).
pub const STEP: f32 = 1.0 / 60.0;

/// The physics settings of a preset (graphics.rs presets; options.json `graphics` changes them by name).
#[derive(Clone, Copy, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct PhysicsStepSettings {
    /// whole 1/60 s steps whatever the frame rate (off: one step per frame of at least 1/60 s, the original's)
    pub physics_fixed_step: bool,
    /// draw bodies between their last two steps above 60 fps (needs `physics_fixed_step`)
    pub physics_interpolation: bool,
    /// Jolt's velocity iterations per step (2..255); applies when the next level starts
    pub physics_solver_iterations: u32,
    /// the Jolt world's body capacity; applies when the next level starts
    pub physics_body_capacity: u32,
}

impl PhysicsStepSettings {
    /// Jolt's own default iterations and capacity (`WorldSettings::default`): the engine's physics before the
    /// presets, which the playtests were tuned with.
    pub const JOLT_ITERATIONS: u32 = 10;
    pub const JOLT_BODIES: u32 = 10240;

    /// The original's frame-tied stepping (the Original preset).
    pub fn original() -> Self {
        PhysicsStepSettings {
            physics_fixed_step: false,
            physics_interpolation: false,
            physics_solver_iterations: Self::JOLT_ITERATIONS,
            physics_body_capacity: Self::JOLT_BODIES,
        }
    }

    /// The old remaster's defaults: fixed steps, interpolation, twice the solver iterations (Havok 4 -> 8, here Jolt
    /// 10 -> 20), and a larger world (its force pool went 512 -> 8192; here bodies 10240 -> 32768).
    pub fn remaster() -> Self {
        PhysicsStepSettings {
            physics_fixed_step: true,
            physics_interpolation: true,
            physics_solver_iterations: 2 * Self::JOLT_ITERATIONS,
            physics_body_capacity: 32768,
        }
    }

    /// The Jolt world's settings for a new level's world.
    pub fn world(&self) -> crate::physics::WorldConfig {
        let bodies = self.physics_body_capacity.clamp(1024, 65536);
        let scale = bodies.div_ceil(Self::JOLT_BODIES).max(1);
        crate::physics::WorldConfig {
            velocity_steps: self.physics_solver_iterations.clamp(2, 255),
            max_bodies: bodies,
            // Jolt's defaults (65536 pairs, 10 MiB scratch) per 10240 bodies
            max_body_pairs: 65536 * scale,
            temp_allocator_size: 10 * 1024 * 1024 * scale,
        }
    }
}

impl Default for PhysicsStepSettings {
    fn default() -> Self {
        Self::remaster()
    }
}

/// The most substeps of a frame (Game_Update 0x00402e80: Game+0x4c34 capped at 5).
pub const MAX_SUBSTEPS: u32 = 5;
/// The longest substep (Game_Update: Game+0x4c30 capped at K 0x005bcc34 = 0.05 s).
pub const MAX_SUBSTEP: f32 = 0.05;

/// The original's steps for a frame of `dt` seconds: how many and how long each. Game_Update 0x00402e80 splits the
/// frame into 1 + (int)(20 dt) substeps, at most 5, of dt / n each, at most 0.05 s; PhysicsManager_Step 0x00411d40
/// raises a step under 1/60 s to 1/60 s and steps the world n times by it. Steps within 0.1 % of 1/60 s are exactly
/// 1/60 s (the old remaster's tolerance: frame times hover round 1/60 s at 60 fps; the scripted runs' 16.6667 ms frame
/// then steps as the fixed timestep does).
pub fn frame_tied_steps(dt: f32) -> (u32, f32) {
    let n = (1 + (dt * 20.0) as u32).min(MAX_SUBSTEPS);
    let step = (dt / n as f32).min(MAX_SUBSTEP);
    (n, if step < STEP * 1.001 { STEP } else { step })
}

/// The world's steps so far and how far the frame is into the next one (0..1; 1 when the frame stepped its whole
/// time: the frame-tied mode). The old remaster's g_physicsStepId / g_physicsAlpha.
#[derive(Resource, Default, Debug)]
pub struct PhysicsClock {
    pub steps: u64,
    pub alpha: f32,
    /// frames drawn (counts every Update)
    pub frames: u64,
}

pub struct PhysicsStepPlugin;

impl Plugin for PhysicsStepPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(Time::<Fixed>::from_seconds(STEP as f64))
            .init_resource::<PhysicsClock>()
            .init_resource::<InterpolationWindow>()
            .add_systems(First, restore_true_poses)
            .add_systems(FixedUpdate, step_fixed.run_if(fixed_step))
            .add_systems(RunFixedMainLoop, step_per_frame.run_if(not(fixed_step)).in_set(RunFixedMainLoopSystems::BeforeFixedMainLoop))
            .add_systems(RunFixedMainLoop, measure_alpha.in_set(RunFixedMainLoopSystems::AfterFixedMainLoop))
            .add_systems(
                PostUpdate,
                interpolate.after(crate::physics::PhysicsSync).before(bevy::transform::TransformSystems::Propagate),
            );
    }
}

fn settings(s: &Option<Res<GraphicsSettings>>) -> PhysicsStepSettings {
    s.as_ref().map_or_else(PhysicsStepSettings::original, |s| s.physics)
}

fn fixed_step(s: Option<Res<GraphicsSettings>>) -> bool {
    settings(&s).physics_fixed_step
}

fn step_fixed(mut physics: NonSendMut<Physics>, time: Res<Time<Fixed>>, mut clock: ResMut<PhysicsClock>) {
    physics.step(time.delta_secs());
    clock.steps += 1;
}

/// The frame's substeps, each at least 1/60 s ([`frame_tied_steps`]); none while the game is paused (no virtual time
/// passes: Game_Update steps only when dt > 0).
fn step_per_frame(mut physics: NonSendMut<Physics>, time: Res<Time<Virtual>>, mut clock: ResMut<PhysicsClock>) {
    let dt = time.delta_secs();
    if dt > 0.0 {
        let (n, step) = frame_tied_steps(dt);
        for _ in 0..n {
            physics.step(step);
            clock.steps += 1;
        }
    }
}

fn measure_alpha(s: Option<Res<GraphicsSettings>>, fixed: Res<Time<Fixed>>, mut clock: ResMut<PhysicsClock>) {
    clock.frames += 1;
    clock.alpha = if settings(&s).physics_fixed_step { fixed.overstep_fraction().clamp(0.0, 1.0) } else { 1.0 };
}

/// Whether frames clearly outnumber steps: measured over 30-frame windows, active above 6 frames per 5 steps
/// (about 72 fps), as the old remaster's DrawMatrix.
#[derive(Resource, Default, Debug)]
pub struct InterpolationWindow {
    start_frame: u64,
    start_steps: u64,
    pub active: bool,
}

impl InterpolationWindow {
    pub fn tick(&mut self, frame: u64, steps: u64) {
        if frame - self.start_frame >= 30 {
            let n = steps - self.start_steps;
            self.active = n > 0 && (frame - self.start_frame) * 5 > n * 6;
            self.start_frame = frame;
            self.start_steps = steps;
        }
    }
}

/// A body's pose as drawn: the body's centre (its entity's origin plus the rotated offset) and rotation.
#[derive(Clone, Copy, PartialEq, Debug, Default)]
pub struct Pose {
    pub centre: Vec3,
    pub rotation: Quat,
}

impl Pose {
    pub fn lerp(self, to: Pose, a: f32) -> Pose {
        Pose { centre: self.centre.lerp(to.centre, a), rotation: self.rotation.slerp(to.rotation, a) }
    }
}

/// A body's poses after the last two steps (the old remaster's `Track`).
#[derive(Component, Clone, Copy, Debug, Default)]
pub struct PoseTrack {
    prev: Pose,
    cur: Pose,
    step: u64,
    frame: u64,
    moved_off_step: Option<u64>,
}

impl PoseTrack {
    /// Notes the true pose at this frame and step; returns the pose to draw when it differs from the true one.
    /// `rotation_driven`: the physics turns the body (else the game sets its facing every frame, which is not a move).
    pub fn update(&mut self, pose: Pose, step: u64, frame: u64, alpha: f32, rotation_driven: bool) -> Option<Pose> {
        let seen = self.frame + 1 == frame || self.frame == frame;
        let moved = |a: &Pose, b: &Pose| a.centre != b.centre || (rotation_driven && a.rotation != b.rotation);
        if self.step != step {
            // the next step after a frame that saw the last one: blend from it; new, unseen for a while, or several
            // steps at once: snap
            self.prev = if seen && self.step + 1 == step { self.cur } else { pose };
            self.cur = pose;
            self.step = step;
        } else if !seen || moved(&pose, &self.cur) {
            // moved without a step: teleported or set by the game
            self.prev = pose;
            self.cur = pose;
            self.moved_off_step = Some(frame);
        }
        self.frame = frame;
        if alpha >= 1.0 || self.moved_off_step.is_some_and(|f| frame - f < 60) || !moved(&self.prev, &self.cur) {
            return None;
        }
        let mut drawn = self.prev.lerp(self.cur, alpha.max(0.0));
        if !rotation_driven {
            drawn.rotation = pose.rotation;
        }
        Some(drawn)
    }
}

/// The true transform of an entity drawn interpolated, and the drawn one (put back in `First` unless something else
/// wrote the transform since).
#[derive(Component, Clone, Copy)]
pub struct DrawnBetweenSteps {
    truth: Transform,
    drawn: Transform,
}

#[allow(clippy::type_complexity)]
fn interpolate(
    mut commands: Commands,
    s: Option<Res<GraphicsSettings>>,
    clock: Res<PhysicsClock>,
    mut window: ResMut<InterpolationWindow>,
    mut q: Query<(Entity, &Body, &mut Transform, Option<&mut PoseTrack>)>,
) {
    let s = settings(&s);
    if !(s.physics_fixed_step && s.physics_interpolation) {
        return;
    }
    window.tick(clock.frames, clock.steps);
    for (e, body, mut t, track) in &mut q {
        let offset = if body.sync_rotation { t.rotation * (body.offset * t.scale) } else { body.offset };
        let pose = Pose { centre: t.translation + offset, rotation: t.rotation };
        let Some(mut track) = track else {
            commands.entity(e).insert(PoseTrack { prev: pose, cur: pose, step: clock.steps, frame: clock.frames, moved_off_step: None });
            continue;
        };
        let Some(drawn) = track.update(pose, clock.steps, clock.frames, clock.alpha, body.sync_rotation).filter(|_| window.active) else { continue };
        let truth = *t;
        t.rotation = drawn.rotation;
        t.translation = drawn.centre - if body.sync_rotation { drawn.rotation * (body.offset * t.scale) } else { body.offset };
        commands.entity(e).insert(DrawnBetweenSteps { truth, drawn: *t });
    }
}

/// Puts the true transforms back before the frame's game systems run, and the root bodies' global transforms with
/// them (the game reads both; children keep the drawn pose until the next propagation, which no game code reads).
fn restore_true_poses(
    mut commands: Commands,
    mut q: Query<(Entity, &DrawnBetweenSteps, &mut Transform, &mut GlobalTransform, Option<&ChildOf>)>,
    parents: Query<&GlobalTransform, Without<DrawnBetweenSteps>>,
) {
    for (e, d, mut t, mut gt, parent) in &mut q {
        // something else set the transform since it was drawn: that value stands
        if *t == d.drawn {
            *t = d.truth;
            *gt = match parent.and_then(|p| parents.get(p.parent()).ok()) {
                Some(p) => p.mul_transform(d.truth),
                None => GlobalTransform::from(d.truth),
            };
        }
        commands.entity(e).remove::<DrawnBetweenSteps>();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Game_Update / PhysicsManager_Step: short frames step 1/60 s once; frames round 1/60 s step it exactly; longer
    /// ones in 1 + 20 dt substeps of at most 0.05 s, at most 5 (a frame over 0.25 s simulates 0.25 s).
    #[test]
    fn frame_tied() {
        assert_eq!(frame_tied_steps(1.0 / 144.0), (1, STEP));
        assert_eq!(frame_tied_steps(0.016_666_7), (1, STEP));
        assert_eq!(frame_tied_steps(0.04), (1, 0.04));
        assert_eq!(frame_tied_steps(0.05), (2, 0.025));
        assert_eq!(frame_tied_steps(0.12), (3, 0.04));
        assert_eq!(frame_tied_steps(0.3), (5, 0.05));
        assert_eq!(frame_tied_steps(2.0), (5, 0.05));
    }

    /// The presets: the Original's frame-tied world as before; the Remaster's fixed, interpolated, stiffer, larger.
    #[test]
    fn presets() {
        let o = PhysicsStepSettings::original();
        assert!(!o.physics_fixed_step && !o.physics_interpolation);
        assert_eq!((o.world().velocity_steps, o.world().max_bodies, o.world().temp_allocator_size), (10, 10240, 10 * 1024 * 1024));
        let r = PhysicsStepSettings::remaster();
        assert!(r.physics_fixed_step && r.physics_interpolation);
        let w = r.world();
        assert_eq!((w.velocity_steps, w.max_bodies, w.max_body_pairs), (20, 32768, 65536 * 4));
    }

    /// Active above 6 frames per 5 steps over a 30-frame window: at 144 fps, not at 60.
    #[test]
    fn window() {
        let mut w = InterpolationWindow::default();
        // 60 fps: a step per frame
        w.tick(30, 30);
        assert!(!w.active);
        // 144 fps: 30 frames, 12 or 13 steps
        w.tick(60, 43);
        assert!(w.active);
    }

    /// Rendered poses are smoothed between steps: at 144 fps a body moving 1 unit per step is drawn at the blend
    /// of its last two step poses by the frame's alpha, and snaps after a move between steps.
    #[test]
    fn smooths_between_steps() {
        let at = |x: f32| Pose { centre: Vec3::new(x, 0.0, 0.0), rotation: Quat::IDENTITY };
        let mut t = PoseTrack::default();
        // steps 1 and 2 seen on consecutive frames: the body went 0 -> 1
        assert_eq!(t.update(at(0.0), 1, 1, 0.0, true), None);
        let d = t.update(at(1.0), 2, 2, 0.25, true).expect("blended");
        assert!((d.centre.x - 0.25).abs() < 1e-6);
        // the next frames before step 3: further in, the same two poses
        let d = t.update(at(1.0), 2, 3, 0.75, true).expect("blended");
        assert!((d.centre.x - 0.75).abs() < 1e-6);
        // the drawn positions only go forward from frame to frame (no jumps back)
        let d3 = t.update(at(2.0), 3, 4, 0.1, true).expect("blended");
        assert!(d3.centre.x > 0.75 && d3.centre.x < 2.0);
        // moved without a step (a teleport): drawn true, and for 60 frames after
        assert_eq!(t.update(at(50.0), 3, 5, 0.5, true), None);
        assert_eq!(t.update(at(51.0), 4, 6, 0.5, true), None);
        // the rotation of a body the game turns is its own, and is no move
        let mut c = PoseTrack::default();
        let turned = Pose { centre: Vec3::X, rotation: Quat::from_rotation_y(1.0) };
        c.update(at(0.0), 1, 1, 0.0, false);
        let d = c.update(turned, 2, 2, 0.5, false).expect("blended");
        assert_eq!(d.rotation, turned.rotation);
        assert!((d.centre.x - 0.5).abs() < 1e-6);
    }
}
