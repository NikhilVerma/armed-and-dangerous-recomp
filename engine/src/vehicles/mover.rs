//! Scripted flight paths: an object driven by an IC ("in context") anim's root track, the mode trigger_ic of the
//! zeppelins, saucers and the lev_5_m4 colossus (Ai_InitGeneric 0x004247f0: generic_txt is the anim,
//! receive_trigger_activate the trigger that starts it, trigger_los also starts it when a target is in range).
//!
//! - Set up 0x0042d000 (state 0x8c, waiting): the trigger handler 0x0042f870 (id +0x52c) or, with trigger_los, a target
//!   found within range (ai_06.cpp:1658-1668) calls 0x0042f5c0, which plays the anim (GenericAI_PlayICAnim 0x0042c410):
//!   the object's matrix then is the start (+0xa28), physics off, state 0x64.
//! - State 0x64 (ai_06.cpp:1900-1958): the time +0xa20 += dt; the root node's transform at that time times the start
//!   is the object's matrix (0x004e2910, Object_SetTransform 0x00457990), its velocity the step. The end test 0x00406430:
//!   past the end with ic_loop (+0xad8) the time wraps (by the length) and the path starts over from the same start
//!   (ic_loop_snap +0xad9 also puts the object back at the start); without, it holds the end and wakes (0x0042c0e0).
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
    /// the object's matrix when the anim started (+0xa28), engine space (the entity's frame is the game's model space)
    pub start: Mat4,
    /// seconds into the anim (+0xa20)
    pub t: f32,
    /// ic_loop (+0xad8)
    pub looping: bool,
    /// ended without a loop (0x00406430 returned true)
    pub done: bool,
    /// the last frame's step (the object's velocity: Body_setLinearVelocity 0x00458510)
    pub velocity: Vec3,
}

impl Mover {
    pub fn new(anim: &str, start: Mat4) -> Self {
        Mover { anim: anim.to_lowercase(), start, t: 0.0, looping: false, done: false, velocity: Vec3::ZERO }
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
        if m.done {
            m.velocity = Vec3::ZERO;
            continue;
        }
        let Some(anim) = lib.anim(&m.anim) else {
            m.done = true;
            continue;
        };
        let before = m.t;
        let mut now = before + dt;
        let mut passed = notes_between(&anim, before, now.min(anim.duration), before == 0.0);
        // the loop flag can be set by a note of this very frame (the IC_loop notes sit just before the end)
        let loops = m.looping || passed.iter().any(|n| n.to_lowercase().starts_with("ic_loop"));
        if now >= anim.duration {
            if loops {
                now -= anim.duration;
                passed.extend(notes_between(&anim, -1.0, now, false));
            } else {
                now = anim.duration;
                m.done = true;
            }
        }
        for n in &passed {
            let l = n.to_lowercase();
            if l.starts_with("ic_loop") {
                m.looping = true;
            }
            notes.write(MoverNote { entity: e, text: n.clone() });
        }
        m.t = now;
        let pose = m.pose(&anim, now);
        let (_, rot, pos) = pose.to_scale_rotation_translation();
        m.velocity = if dt > 0.0 { (pos - t.translation) / dt } else { Vec3::ZERO };
        t.translation = pos;
        t.rotation = rot;
    }
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
}
