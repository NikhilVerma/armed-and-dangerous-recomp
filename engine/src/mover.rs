//! Movers: an object carried along a "mover" anim, the generic script text of the original (gen_text / generic_txt,
//! GenericAIObject vt+0x238; anims named mover_*). The anim's root track (the track named as the anim) moves the
//! object relative to where it started, and the anim's notes are its script (GenericAIObject_HandleEvent 0x0043d180:
//! "fx NAME", "trigger N", "deactivate", "release_platoon", "powderkeg", "token_or_health" ...).
//!
//! Users: the army's platoons (army.rs: FakeGruntObject, mover_army_path_*), the bombers flying over the base
//! (object_ic: ICObject, mover_sb_*), the grunts' ladder and wall scripts (mover_grunt_ladder_*, played as an anim
//! overlay, army.rs). This module only moves and reads the script; each owner acts on the notes it understands
//! ([`MoverNote`]); the generic ones (fx, trigger, deactivate) are done here when the mover asks for it
//! ([`Mover::generic`]).
//!
//! Game space: the origin and the track are in the game's axes (z up); the entity's Transform is the engine's
//! (`level::game_to_engine`).

use bevy::prelude::*;

/// An object moved by a mover anim.
#[derive(Component, Clone, Debug)]
pub struct Mover {
    /// the mover anim (its root track has the anim's own name)
    pub anim: String,
    /// seconds into the anim
    pub t: f32,
    /// where the mover started, game space (the object's matrix when the script began)
    pub origin: Mat4,
    /// paused movers neither move nor read notes (FakeGrunt state 0x66: the army's grunt limit reached)
    pub paused: bool,
    /// "anim_loop NAME" on itself or a looping script: wraps at the end
    pub looping: bool,
    /// the "orient" flag (0x2000000 of +0x488): faces along its path (guess: model -y along the horizontal velocity)
    pub orient: bool,
    /// fx, trigger and deactivate notes done here (the generic handler); false: the owner reads every note
    pub generic: bool,
    /// the script reached its end (no more notes; a non-looping mover rests at its last key)
    pub ended: bool,
    /// the game-space matrix last applied (the owner's view of where it is)
    pub world: Mat4,
    last_pos: Option<Vec3>,
}

impl Mover {
    pub fn new(anim: &str, origin: Mat4) -> Self {
        Self { anim: anim.to_lowercase(), t: 0.0, origin, paused: false, looping: false, orient: false, generic: true, ended: false, world: origin, last_pos: None }
    }

    /// The game-space matrix at the mover's time: origin * T(track) * R(track).
    pub fn sample(&self, anim: &crate::skin::Anim) -> Mat4 {
        let t = anim.translation(&self.anim, self.t).unwrap_or(Vec3::ZERO);
        let r = anim.rotation(&self.anim, self.t).unwrap_or(Quat::IDENTITY);
        self.origin * Mat4::from_rotation_translation(r, t)
    }
}

/// One line of a mover's note reached (a note may hold several lines: "trigger 5000\r\ndeactivate").
#[derive(Message, Clone, Debug)]
pub struct MoverNote {
    pub entity: Entity,
    pub text: String,
}

pub struct MoverPlugin;

impl Plugin for MoverPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<MoverNote>().add_systems(Update, (advance, generic_notes).chain().run_if(crate::playing));
    }
}

/// The note lines of `anim` in (a, b] (and those at or before 0 when a is the start).
pub fn notes_between(anim: &crate::skin::Anim, a: f32, b: f32) -> Vec<String> {
    let mut out = Vec::new();
    for (nt, text) in &anim.notes {
        let hit = if a <= 0.0 { *nt <= b } else { *nt > a && *nt <= b };
        if hit {
            out.extend(text.split(['\r', '\n']).map(str::trim).filter(|l| !l.is_empty()).map(str::to_owned));
        }
    }
    out
}

/// The game-space matrix as the engine's transform.
pub fn engine_transform(game: Mat4) -> Transform {
    let axes = Mat4::from_quat(crate::level::game_to_engine());
    let (_, r, t) = (axes * game).to_scale_rotation_translation();
    Transform { translation: t, rotation: r, scale: Vec3::ONE }
}

/// Moves every mover along its track and passes on the notes it crosses.
fn advance(time: Res<Time>, mut lib: ResMut<crate::skin::AnimLibrary>, mut q: Query<(Entity, &mut Mover, &mut Transform)>, mut notes: MessageWriter<MoverNote>) {
    let dt = time.delta_secs();
    for (e, mut m, mut tr) in &mut q {
        if m.paused || m.ended && !m.looping {
            continue;
        }
        let Some(anim) = lib.anim(&m.anim) else {
            m.ended = true;
            continue;
        };
        let t0 = m.t;
        let mut t1 = t0 + dt;
        for text in notes_between(&anim, t0, t1.min(anim.duration)) {
            match text.split_whitespace().next().unwrap_or("") {
                "orient" | "orient2d" => m.orient = true,
                "orient_off" => m.orient = false,
                "anim_loop" if text.split_whitespace().nth(1).is_some_and(|n| n.eq_ignore_ascii_case(&m.anim)) => m.looping = true,
                _ => {}
            }
            notes.write(MoverNote { entity: e, text });
        }
        if t1 >= anim.duration {
            if m.looping {
                t1 -= anim.duration;
            } else {
                t1 = anim.duration;
                m.ended = true;
            }
        }
        m.t = t1;
        let mut world = m.sample(&anim);
        let pos = world.w_axis.truncate();
        if m.orient {
            // face along the path: the model's -y along the horizontal velocity (guess at 0x2000000's facing)
            if let Some(v) = m.last_pos.map(|p| (pos - p).with_z(0.0)).filter(|v| v.length() > 1e-3) {
                let yaw = (-v.x).atan2(v.y) + std::f32::consts::PI;
                world = Mat4::from_rotation_translation(Quat::from_rotation_z(yaw), pos);
            }
        }
        m.last_pos = Some(pos);
        m.world = world;
        *tr = engine_transform(world);
    }
}

/// The generic notes of movers that ask for them: "fx NAME" (Fx_Create at the object), "trigger N"
/// (Object_SendTrigger), "deactivate" (vt+0xd4: the object removed).
fn generic_notes(
    mut commands: Commands,
    mut notes: MessageReader<MoverNote>,
    q: Query<(&Mover, &GlobalTransform)>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut triggers: MessageWriter<crate::mission::SendTrigger>,
) {
    for n in notes.read() {
        let Ok((m, gt)) = q.get(n.entity) else { continue };
        if !m.generic {
            continue;
        }
        let mut words = n.text.split_whitespace();
        match words.next().unwrap_or("").to_lowercase().as_str() {
            "fx" => {
                if let Some(name) = words.next() {
                    let (_, rot, at) = gt.to_scale_rotation_translation();
                    fx.write(crate::fx::StartFx { name: name.to_lowercase(), at, rotation: rot, follow: None, position_only: false });
                }
            }
            "trigger" => {
                if let Some(t) = words.next().and_then(|w| w.parse::<i64>().ok()) {
                    triggers.write(crate::mission::SendTrigger(t));
                }
            }
            "deactivate" => {
                commands.entity(n.entity).despawn();
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn engine_transform_maps_game_up_to_engine_up() {
        let t = engine_transform(Mat4::from_translation(Vec3::new(1.0, 2.0, 3.0)));
        assert!((t.translation - Vec3::new(1.0, 3.0, -2.0)).length() < 1e-5);
    }
}
