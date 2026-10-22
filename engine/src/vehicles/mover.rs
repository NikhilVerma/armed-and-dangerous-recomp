//! Scripted flight paths: an object driven by an IC ("in context") anim's root track, the mode trigger_ic of the
//! zeppelins, saucers and the lev_5_m4 colossus (Ai_InitGeneric 0x004247f0: generic_txt is the anim,
//! receive_trigger_activate the trigger that starts it, trigger_los also starts it when a target is in range).
//!
//! - Set up 0x0042d000 (state 0x8c, waiting): the trigger handler 0x0042f870 (receive_trigger_activate) or, with trigger_los, a target
//!   found within range (ai_06.cpp:1658-1668) calls 0x0042f5c0, which plays the anim (GenericAI_PlayICAnim 0x0042c410):
//!   the object's matrix then is the start, physics off, state 0x64.
//! - State 0x64 (ai_06.cpp:1900-1958): the time goes on by dt; the root node's transform at that time times the start
//!   is the path's pose (0x004e2910). The object takes the pose's rotation at once, but not its position: the matrix
//!   handed to Object_SetTransform 0x00457990 keeps the object's current position, and the gap to the pose is set as
//!   the body's velocity (Body_setLinearVelocity 0x00458510), which physics integrates on the next step. The gap itself
//!   per second means the object chases the path with a time constant of one second: it starts from rest and runs
//!   about one second of path behind (lev_2_m1 saucer 285: 11 units in its first second; this model reproduces the
//!   original's trace within 0.03 units over 9 s, the velocity applied one frame later).
//! - The end test 0x00406430: past the end with ic_loop the time wraps (by the length) and the path starts over from
//!   the same start (ic_loop_snap also puts the object back at the start: Object_SetTransform with the start matrix);
//!   without, it wakes (0x0042c0e0 with 1: the body stopped), so the object rests where the chase left it.
//! - The anim's notes along the way (GenericAIObject_HandleEvent 0x0043d180): ic_loop / ic_loop_snap, flying (the
//!   collision filter 0x100, nothing here), anim_loop / anim_intro / animloop (the body's own anim), deactivate
//!   (removal, vt+0xdc: no death, no fx, no trigger), and the zeppelins' drops (create_object_ic, create_rocket_captain_IC:
//!   a captain at a ref node).
//!
//! The root track's rotation turns the model: mover_mini_zeppelin_l2m1_01 starts at -105.8 degrees about z and its
//! first step runs along the model's -Y turned by that (the travel direction), so the track is used as exported.

use bevy::prelude::*;

pub struct MoverPlugin;

impl Plugin for MoverPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<MoverNote>().add_systems(Update, play.run_if(crate::playing));
    }
}

/// An IC anim driving an object (see the module doc).
#[derive(Component, Clone, Debug)]
pub struct Mover {
    pub anim: String,
    /// the object's matrix when the anim started, engine space (the entity's frame is the game's model space)
    pub start: Mat4,
    /// seconds into the anim
    pub t: f32,
    /// an ic_loop note was passed: the path loops
    pub looping: bool,
    /// an ic_loop_snap note was passed: each loop puts the object back at the start
    pub snap: bool,
    /// ended without a loop (0x00406430 returned true)
    pub done: bool,
    /// the body's velocity, units per second: the gap to the path's pose (Body_setLinearVelocity 0x00458510)
    pub velocity: Vec3,
    /// held where it is, its clock stopped (a boss stopped to fire: state 0x66, the body keyframed and still, 0x00435b50;
    /// the clock +0xa20 only runs in state 0x64)
    pub paused: bool,
}

impl Mover {
    pub fn new(anim: &str, start: Mat4) -> Self {
        Mover { anim: anim.to_lowercase(), start, t: 0.0, looping: false, snap: false, done: false, velocity: Vec3::ZERO, paused: false }
    }

    /// Holds the object where it is (its velocity zeroed) or lets it go on along its path from there.
    pub fn hold(&mut self, held: bool) {
        self.paused = held;
        self.velocity = Vec3::ZERO;
    }

    /// The object's matrix at time `t` (engine space): the start times the root node's local transform.
    pub fn pose(&self, anim: &crate::skin::Anim, t: f32) -> Mat4 {
        let tr = anim.translation(&self.anim, t).unwrap_or(Vec3::ZERO);
        let r = anim.rotation(&self.anim, t).unwrap_or(Quat::IDENTITY);
        self.start * Mat4::from_rotation_translation(r, tr)
    }
}

/// A note of a playing IC anim, for the unit that owns the object (one command per line of the note's text).
#[derive(Message, Clone, Debug)]
pub struct MoverNote {
    pub entity: Entity,
    pub text: String,
}

/// Advances every mover (state 0x64 with the end test 0x00406430) and sends the notes passed this frame.
fn play(
    time: Res<Time>,
    mut lib: ResMut<crate::skin::AnimLibrary>,
    mut q: Query<(Entity, &mut Mover, &mut Transform)>,
    mut notes: MessageWriter<MoverNote>,
) {
    let dt = time.delta_secs();
    for (e, mut m, mut t) in &mut q {
        if m.done || m.paused {
            continue;
        }
        let Some(anim) = lib.anim(&m.anim) else {
            m.done = true;
            m.velocity = Vec3::ZERO;
            continue;
        };
        // the physics step: the body carries the velocity the last update set
        t.translation += m.velocity * dt;
        let before = m.t;
        let mut now = before + dt;
        let mut passed = notes_between(&anim, before, now.min(anim.duration), before == 0.0);
        // the loop flags can be set by a note of this very frame (the IC_loop notes sit just before the end)
        for n in &passed {
            let n = n.to_lowercase();
            if n.starts_with("ic_loop") {
                m.looping = true;
                m.snap |= n.starts_with("ic_loop_snap");
            }
        }
        if now >= anim.duration {
            if m.looping {
                now -= anim.duration;
                passed.extend(notes_between(&anim, -1.0, now, false));
                if m.snap {
                    t.translation = m.start.w_axis.truncate();
                }
            } else {
                // the end: the AI wakes with its body stopped (0x0042c0e0 with 1), where the chase left it
                m.t = anim.duration;
                m.done = true;
                m.velocity = Vec3::ZERO;
                for n in passed {
                    notes.write(MoverNote { entity: e, text: n });
                }
                continue;
            }
        }
        for n in passed {
            notes.write(MoverNote { entity: e, text: n });
        }
        m.t = now;
        let (_, rot, target) = m.pose(&anim, now).to_scale_rotation_translation();
        m.velocity = chase_velocity(t.translation, target);
        t.rotation = rot;
    }
}

/// The velocity that chases the path (0x00442670 block: the gap to the pose, set as units per second, so the object
/// closes it with a one-second time constant).
fn chase_velocity(at: Vec3, target: Vec3) -> Vec3 {
    target - at
}

/// The notes in (from, to] (and a note at 0 on the first frame: the range callback 0x004eb480 from the start), one
/// command per line of their text.
fn notes_between(anim: &crate::skin::Anim, from: f32, to: f32, first: bool) -> Vec<String> {
    let mut out = Vec::new();
    for (nt, text) in &anim.notes {
        if (*nt > from || (first && *nt == 0.0)) && *nt <= to {
            out.extend(text.split(['\r', '\n']).map(str::trim).filter(|s| !s.is_empty()).map(str::to_owned));
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The start matrix carries the root track: a root step along +x in the anim moves the object along its own
    /// turned x.
    #[test]
    fn pose_is_start_times_root() {
        let m = Mover::new("x", Mat4::from_rotation_translation(Quat::from_rotation_y(std::f32::consts::FRAC_PI_2), Vec3::new(10.0, 0.0, 0.0)));
        let local = Mat4::from_translation(Vec3::new(1.0, 0.0, 0.0));
        let p = (m.start * local).w_axis.truncate();
        assert!((p - Vec3::new(10.0, 0.0, -1.0)).length() < 1e-5, "{p:?}");
    }

    /// Chasing a path at a steady speed from rest: slow at first, then one second of path behind (the original's
    /// saucers and mini zeppelins).
    #[test]
    fn chase_starts_from_rest_and_lags_one_second() {
        let (dt, speed) = (1.0 / 60.0, 30.0);
        let (mut at, mut v) = (Vec3::ZERO, Vec3::ZERO);
        let mut first_second = 0.0;
        for frame in 1..=600 {
            at += v * dt;
            let target = Vec3::X * speed * frame as f32 * dt;
            v = chase_velocity(at, target);
            if frame == 60 {
                first_second = at.x;
            }
        }
        // x(T) = v (T - (1 - e^-T)): 11.0 units after one second at 30 units/s
        assert!((first_second - 11.0).abs() < 0.6, "{first_second}");
        let lag = speed * 10.0 - at.x;
        assert!((lag - speed).abs() < 1.0, "{lag}");
    }
}
