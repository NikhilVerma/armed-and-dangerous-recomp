//! Falling AIs: the falling watchdog (0x0042e910, ai_03.cpp:2387-2447) and the falling state 0x82 (GenericAI_Update
//! ai_06.cpp:1567-1623), with the parachute of the paratroopers' free fall (paratroop.rs starts it).
//! docs/subsystems/grunt-ai.md section 15.
//!
//! - The watchdog (every frame, GenericAI_Update ai_06.cpp:3525): a live AI, not attached (a gun, a window or tower post),
//!   not ragdolled, not already falling, whose body sinks faster than 20 / s (vz < K 0x005bcd44 = -20) arms a 0.2 s timer
//!   (+0x890) unless its body touched something in the last step; a contact or a slower vz disarms it. The timer does
//!   not run in the flight / teleport states (0xb5, 0xb8..0xba, 0xbd..0xbf, 0xc4, 0xc5, 0xe7, 0xe8, 0xea, 0xf0, ...:
//!   here the jumper's flight and the monk's teleport). Run out: the state and anims saved (+0x4ac, +0x4b0..+0x4bc), 5 s
//!   to live (+0x890), anim 0x61 looped (grunt_bow_fall / captain_fall), state 0x82.
//! - State 0x82: a contact lands him: the saved state back (and its anims), the parachute closed ("parachute_close").
//!   Else, in a free fall with the parachute allowed (+0x48c 0x8000: the freefall_* notes) and sinking faster than 25 / s
//!   (K 0x005bcfa8), the velocity pulled toward (vx, vy, -25) at mass x 50 (0x00471750, K 0x005bcd40): the descent held at
//!   25 / s; the first time, the parachute ("captain_parachute") made at the trooper's ref_parachute and opened
//!   ("parachute_open"), anim 0x62 (captain_parachute). The time to live runs down; out: vt+0xd4(0), death (no landing
//!   damage otherwise).
//!
//! What makes a live grunt fall here: walking or being pushed off a ledge, the ground destroyed under him, a drop
//! (paratroop.rs). Blasts that ragdoll him are deaths in this port (ai.rs die). Not ported: state 0x7a's own entry to
//! 0x82 (no 0x7a in the engine) and the jumper's airborne end of a turbo state (0x0042e1c0: 10 s; jumper.rs lands on a
//! contact instead).

use super::{Grunt, Mind};
use crate::physics::{Body, Physics, bv, jv};
use bevy::prelude::*;

/// vz below which the watchdog arms (K 0x005bcd44).
pub const FALL_SPEED: f32 = -20.0;
/// The watchdog's delay (0.2 s) and a fall's time to live (+0x890 = 5.0).
pub const ARM_DELAY: f32 = 0.2;
pub const TIME_TO_LIVE: f32 = 5.0;
/// The parachute's descent (K 0x005bcfa8) and its pull (mass x 50, K 0x005bcd40: the velocity's rate toward it).
pub const CHUTE_SPEED: f32 = -25.0;
pub const CHUTE_PULL: f32 = 50.0;

/// A fall in progress (state 0x82).
#[derive(Clone, Copy, Debug)]
pub struct Fall {
    /// the state to go back to on landing (+0x4ac)
    pub resume: Mind,
    /// time to live (+0x890)
    pub timer: f32,
    /// +0x48c 0x8000: a free fall that may open the parachute
    pub chute_allowed: bool,
    /// the parachute is open (made once)
    pub chute_open: bool,
}

impl Fall {
    pub fn new(resume: Mind, timer: f32, chute_allowed: bool) -> Fall {
        Fall { resume, timer, chute_allowed, chute_open: false }
    }
}

/// The parachute a trooper carries down (captain_parachute at his ref_parachute).
#[derive(Component)]
pub struct Parachute {
    pub owner: Entity,
}

/// Falls started, landings, deaths by falling, parachutes opened (playtest log `fall`).
#[derive(Resource, Default, Debug)]
pub struct FallStats {
    pub falls: u32,
    pub landings: u32,
    pub deaths: u32,
    pub chutes: u32,
}

/// The parachute model (template captain_parachute), loaded with the level when the level has paratroopers (level.rs).
#[derive(Resource, Default)]
pub struct ParachuteAssets(pub Vec<(Handle<Mesh>, Handle<StandardMaterial>)>);

/// Does the watchdog look at this AI (0x0042e910's skips)?
fn watched(g: &Grunt) -> bool {
    !matches!(g.mind, Mind::Dead | Mind::Falling | Mind::Scripted) && g.fixed.is_none() && !g.window && !g.ragdoll
}

/// Does the watchdog's timer run (not in the flight / teleport states)?
fn timer_runs(g: &Grunt) -> bool {
    !(g.mind == Mind::Teleport || g.jumper.is_some_and(|j| j.flight.is_some()))
}

/// The watchdog's step: the timer after this frame, or None when it is disarmed; Some(t <= 0) starts the fall.
pub fn watchdog(armed: Option<f32>, vz: f32, contact: bool, runs: bool, dt: f32) -> Option<f32> {
    if vz >= FALL_SPEED || contact {
        return None;
    }
    match armed {
        None => Some(ARM_DELAY),
        Some(t) if runs => Some(t - dt),
        Some(t) => Some(t),
    }
}

/// The watchdog and state 0x82 for every AI with a body.
#[allow(clippy::too_many_arguments)]
pub fn run(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<Physics>,
    mut q: Query<(Entity, &mut Grunt, &Body, &mut crate::fx::Health, Option<&crate::skin::Animator>)>,
    globals: Query<&GlobalTransform>,
    assets: Option<Res<ParachuteAssets>>,
    mut stats: ResMut<FallStats>,
) {
    let dt = time.delta_secs();
    if dt <= 0.0 {
        return;
    }
    let k = physics.meters_per_unit;
    for (e, mut g, body, mut health, anim) in &mut q {
        if g.mind == Mind::Dead {
            g.fall_watch = None;
            continue;
        }
        let Ok(b) = physics.world.body(body.id) else { continue };
        let vel = bv(b.linear_velocity()) / k;
        drop(b);
        let contact = super::body::touched(&physics, body.id);
        if g.mind == Mind::Falling {
            let Some(mut f) = g.fall else {
                g.mind = Mind::Roam;
                continue;
            };
            if contact {
                // landed: the saved state back, the parachute closed
                g.mind = f.resume;
                g.fall = None;
                stats.landings += 1;
                if f.chute_open {
                    commands.queue(move |w: &mut World| close_parachute(w, e));
                }
                info!("EVENT ai: {e:?} landed (fall)");
                continue;
            }
            if f.chute_allowed && vel.y < CHUTE_SPEED {
                let want = vel.with_y(CHUTE_SPEED);
                let v = vel + (want - vel) * (CHUTE_PULL * dt).min(1.0);
                if let Ok(mut b) = physics.world.body_mut(body.id) {
                    let _ = b.set_linear_velocity(jv(v * k));
                }
                if !f.chute_open {
                    f.chute_open = true;
                    stats.chutes += 1;
                    // made at the trooper's ref_parachute (attached there: follows him)
                    let at = anim.and_then(|a| a.skeleton.index.get("ref_parachute").map(|&i| a.joints[i]));
                    let parent = at.filter(|&j| globals.get(j).is_ok()).unwrap_or(e);
                    let mut c = commands.spawn((Transform::IDENTITY, Visibility::default(), Parachute { owner: e }));
                    if let Some(a) = assets.as_deref() {
                        c.with_children(|p| {
                            for (mesh, mat) in &a.0 {
                                p.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone())));
                            }
                        });
                    }
                    let id = c.id();
                    commands.entity(parent).add_child(id);
                    info!("EVENT ai: {e:?} opens his parachute");
                }
            }
            f.timer -= dt;
            if f.timer < 0.0 {
                // vt+0xd4(0): he dies of the fall
                health.0 = 0.0;
                stats.deaths += 1;
                info!("EVENT ai: {e:?} died falling");
            }
            g.fall = Some(f);
            continue;
        }
        if !watched(&g) {
            // only an attached AI's timer is reset (Object_GetParent, ai_03.cpp:2397); the other skips leave it
            if g.fixed.is_some() || g.window {
                g.fall_watch = None;
            }
            continue;
        }
        g.fall_watch = watchdog(g.fall_watch, vel.y, contact, timer_runs(&g), dt);
        if g.fall_watch.is_some_and(|t| t <= 0.0) {
            start(&mut g, TIME_TO_LIVE, false);
            stats.falls += 1;
            info!("EVENT ai: {e:?} falls (vz {:.0})", vel.y);
        }
    }
}

/// Into state 0x82: the state saved, the time to live, anim 0x61 (charanim.rs).
pub fn start(g: &mut Grunt, timer: f32, chute_allowed: bool) {
    let resume = if g.mind == Mind::Falling { g.fall.map_or(Mind::Roam, |f| f.resume) } else { g.mind };
    g.fall = Some(Fall::new(resume, timer, chute_allowed));
    g.fall_watch = None;
    g.mind = Mind::Falling;
}

/// "parachute_close": the canopy folds away (the close anim is not exported: removed).
fn close_parachute(w: &mut World, owner: Entity) {
    let mut q = w.query::<(Entity, &Parachute)>();
    let gone: Vec<Entity> = q.iter(w).filter(|(_, p)| p.owner == owner).map(|(e, _)| e).collect();
    for e in gone {
        if let Ok(em) = w.get_entity_mut(e) {
            em.despawn();
        }
    }
}

/// A dead trooper's parachute goes with him (+0x894 released on death).
pub fn drop_parachutes(mut commands: Commands, chutes: Query<(Entity, &Parachute)>, grunts: Query<&Grunt>) {
    for (e, p) in &chutes {
        if grunts.get(p.owner).map_or(true, |g| g.mind == Mind::Dead) {
            commands.entity(e).despawn();
        }
    }
}

/// The playtest state's `fall`.
pub fn log(stats: Res<FallStats>, grunts: Query<&Grunt>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let falling = grunts.iter().filter(|g| g.mind == Mind::Falling).count();
    state.0.insert("fall".into(), serde_json::json!({"falls": stats.falls, "landings": stats.landings, "deaths": stats.deaths, "chutes": stats.chutes, "falling": falling}));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn watchdog_arms_and_runs_out() {
        // sinking fast without contact: armed at 0.2, then counting down
        let t = watchdog(None, -30.0, false, true, 0.016);
        assert_eq!(t, Some(ARM_DELAY));
        let mut t = t;
        for _ in 0..13 {
            t = watchdog(t, -30.0, false, true, 0.016);
        }
        assert!(t.unwrap() <= 0.0);
        // a contact or a slow fall disarms it
        assert_eq!(watchdog(Some(0.1), -30.0, true, true, 0.016), None);
        assert_eq!(watchdog(Some(0.1), -10.0, false, true, 0.016), None);
        // in a flight the timer holds
        assert_eq!(watchdog(Some(0.1), -30.0, false, false, 0.016), Some(0.1));
    }
}
