//! Paratroopers: the troops a vehicle's anim drops (vehicles/boss.rs: "create_object_ic captain ref_repel_0N
//! mover_captain_rappel_attack", "create_rocket_captain_IC ref_repel_0N mover_captain_attack_village"; lev_2_m1's mini
//! zeppelin 255, lev_5_m2's village zeppelins). docs/subsystems/infantry.md section 10.
//!
//! - Made at the vehicle's ref_repel_0N node (0x00456180-like: the node's matrix) and handed the IC anim (0x0042c410:
//!   physics off, state 0x64): he slides down his rope by the mover's root ([`Rappel`]).
//! - Its "freefall_*" note (at 0.933 s; GenericAIObject_HandleEvent ai_05.cpp:1513-1550): out of the IC anim (vt+0x1ac(0)),
//!   physics on, the landing orders as the mode to take (+0x4ac: "freefall_attack" 0x13, the player; "freefall_attack_village
//!   LO HI" 1, the objects of tags LO..HI as priority targets +0x858 / +0x85c; ai.rs `drop_orders`), 5000 s to live
//!   (+0x890), anim 0x61, the parachute allowed (+0x48c 0x8000), state 0x82: ai/fall.rs opens the parachute once he sinks
//!   faster than 25 / s, holds the descent there and lands him into his orders.
//! - "freefall_defend_radius" / "freefall_marker_range" (modes 0x14 / 0x12) and the Q112 boss drops (0x0042da00 /
//!   0x0042daa0, modes 0x11a / 0x119) are dead in the shipped data: only the unused test movers (mover_zep_small,
//!   mover_lev_4_test, mover_zeppelin_test_tim_2) carry them; here such a note is a plain free fall.

use super::{Grunt, Mind};
use crate::mover::{Mover, MoverNote};
use bevy::prelude::*;

/// The time to live of a free fall (+0x890 = 5000.0, the freefall events).
pub const FREEFALL_TIME: f32 = 5000.0;

/// A dropped trooper on his rope: the state his landing orders give (+0x4ac).
#[derive(Component, Clone, Copy, Debug)]
pub struct Rappel {
    pub resume: Mind,
}

/// Troopers dropped, out of their rope into the free fall (playtest log `paratroop`).
#[derive(Resource, Default, Debug)]
pub struct ParatroopStats {
    pub dropped: u32,
    pub freefalls: u32,
}

/// The drop (level.rs, a troop with a mover): on the rope at `at` (engine space), the mover playing.
pub fn start(commands: &mut Commands, e: Entity, g: &mut Grunt, at: Vec3, mover: &str) {
    let resume = g.mind;
    g.mind = Mind::Scripted;
    let origin = Mat4::from_translation(crate::level::game_to_engine().inverse() * at);
    commands.entity(e).insert((Rappel { resume }, Mover::new(mover, origin)));
    commands.queue(|w: &mut World| {
        if let Some(mut s) = w.get_resource_mut::<ParatroopStats>() {
            s.dropped += 1;
        }
    });
}

/// The rope (physics off: the body follows the mover) and its end: the "freefall_*" note or the anim's end.
#[allow(clippy::type_complexity)]
pub fn rappel(
    mut commands: Commands,
    mut notes: MessageReader<MoverNote>,
    mut q: Query<(Entity, &Rappel, &mut Grunt, &crate::physics::Body, &Transform, Option<&Mover>, Option<&mut crate::skin::Animator>)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut stats: ResMut<ParatroopStats>,
) {
    let freefall: Vec<Entity> = notes.read().filter(|n| n.text.trim_start().to_lowercase().starts_with("freefall")).map(|n| n.entity).collect();
    let k = physics.meters_per_unit;
    for (e, r, mut g, body, tr, mover, animator) in &mut q {
        if g.mind == Mind::Dead {
            commands.entity(e).remove::<(Mover, Rappel)>();
            continue;
        }
        let pos = tr.translation;
        if freefall.contains(&e) || mover.is_none_or(|m| m.ended) {
            commands.entity(e).remove::<(Mover, Rappel)>();
            g.yaw = super::ic::yaw_of(tr.rotation);
            g.home = pos;
            g.goal = pos;
            if let Ok(mut b) = physics.world.body_mut(body.id) {
                let _ = b.set_position(crate::physics::jr((pos + body.offset) * k), oxijolt::prelude::Activation::Activate);
                let _ = b.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
            }
            g.mind = r.resume;
            super::fall::start(&mut g, FREEFALL_TIME, true);
            stats.freefalls += 1;
            info!("EVENT ai: paratrooper {e:?} free falls");
            continue;
        }
        if let Ok(mut b) = physics.world.body_mut(body.id) {
            let _ = b.set_position(crate::physics::jr((pos + body.offset) * k), oxijolt::prelude::Activation::Activate);
            let _ = b.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
        }
        if let (Some(m), Some(mut a)) = (mover, animator) {
            if a.layers.first().is_none_or(|l| l.anim != m.anim) {
                a.play_at(0, &m.anim, false, crate::skin::Mask::All, m.t);
                a.stop(1);
            }
            if let Some(l) = a.layers.get_mut(0) {
                l.time = m.t;
                l.speed = 0.0;
            }
        }
    }
}

/// The playtest state's `paratroop`.
pub fn log(stats: Res<ParatroopStats>, q: Query<(), With<Rappel>>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert("paratroop".into(), serde_json::json!({"dropped": stats.dropped, "freefalls": stats.freefalls, "on_rope": q.iter().count()}));
}
