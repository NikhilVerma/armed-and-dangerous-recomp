//! Script commands that end an AI (the anim-note handler 0x00409140, "parsed note track", run by AnimTrack_FireEvents
//! 0x004eb480 on the object whose anim plays): "kill" (0x004090f0: vt+0xd4(0), a real death: the death anim or
//! ragdoll, the kill counted, the corpse) and "deactivate" (0x00409110: for an AI, the death bookkeeping 0x00431720 with
//! no damage record: +0x48c dead flag, the kill counted only when held or ragdolled, its send_trigger_on_death +0x528
//! sent; then vt+0xdc, the silent removal: no death anim, no corpse). "kill_tag N" / "deactivate_tag N" (the same on
//! every object of a tag, 0x00459390) appear in no level's data.
//!
//! Who uses them on AIs: the army's ladder climb and wall scales (mover_grunt_ladder_climb 6.83 s, mover_grunt_scale_2..4
//! "deactivate"; army.rs), the companions' takedowns (mover_grunt_qdie_* / mover_grunt_mdie_* "kill"; ai/close_kill.rs) and
//! any IC mover of a grunt (mover.rs notes).

use super::{Grunt, Mind};
use bevy::prelude::*;

/// An AI's script ending it: "kill" (a death) or "deactivate" (removed, its death trigger sent).
#[derive(Message, Clone, Copy, Debug)]
pub struct ScriptEnd {
    pub entity: Entity,
    pub kill: bool,
}

/// Deactivated this frame: dead to the AI's bookkeeping (troop_deaths sends its death trigger), removed next frame.
#[derive(Component)]
pub struct Deactivated;

/// Kills and deactivations by script (playtest log `script_ends`).
#[derive(Resource, Default, Debug)]
pub struct ScriptEndStats {
    pub kills: u32,
    pub deactivations: u32,
}

/// The mover notes "kill" / "deactivate" on an AI (mover.rs leaves an AI's to this module).
pub fn from_movers(mut notes: MessageReader<crate::mover::MoverNote>, grunts: Query<(), With<Grunt>>, mut out: MessageWriter<ScriptEnd>) {
    for n in notes.read() {
        if !grunts.contains(n.entity) {
            continue;
        }
        match n.text.split_whitespace().next().unwrap_or("").to_lowercase().as_str() {
            "kill" => {
                out.write(ScriptEnd { entity: n.entity, kill: true });
            }
            "deactivate" => {
                out.write(ScriptEnd { entity: n.entity, kill: false });
            }
            _ => {}
        }
    }
}

/// Removes last frame's deactivated AIs, then applies this frame's script ends.
pub fn apply(
    mut commands: Commands,
    mut ends: MessageReader<ScriptEnd>,
    gone: Query<Entity, With<Deactivated>>,
    mut grunts: Query<(&mut Grunt, &mut crate::fx::Health)>,
    mut stats: ResMut<ScriptEndStats>,
) {
    for e in &gone {
        commands.entity(e).despawn();
    }
    for end in ends.read() {
        let Ok((mut g, mut h)) = grunts.get_mut(end.entity) else { continue };
        if g.mind == Mind::Dead {
            continue;
        }
        if end.kill {
            // vt+0xd4(0): ai.rs die (the death anim, the corpse)
            h.0 = 0.0;
            stats.kills += 1;
            info!("EVENT ai: {:?} killed by its script", end.entity);
        } else {
            g.mind = Mind::Dead;
            stats.deactivations += 1;
            commands.entity(end.entity).insert(Deactivated);
            info!("EVENT ai: {:?} deactivated by its script", end.entity);
        }
    }
}

/// The playtest state's `script_ends`.
pub fn log(stats: Res<ScriptEndStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert("script_ends".into(), serde_json::json!({"kills": stats.kills, "deactivations": stats.deactivations}));
}
