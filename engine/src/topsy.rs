//! The Topsy Turvy bomb (player_topsyturvy, from the pub): PlayerObject's topsy states (game_05.cpp 1790-1820,
//! game_06.cpp 2330) and the AI's reaction (fn_0042fe50, ai_03.cpp 2977):
//! - Fire Special with it selected (Tab picks the special): flag 0x800000, trotter_topsy_into (its notes: the
//!   fx_trotter_topsy fx, topsy_create attaches the bomb to the hand); then trotter_topsy_world_turn while the view
//!   turns over (the Camera_topsy track); the weapon fires (0x004a1aa0): every AI within topsy_turvy_radius (100) of
//!   Roman is thrown; the timer Game+0xc80 = topsy_turvy_duration (5 s) runs with Roman hanging
//!   (trotter_topsy_hanging), unable to turn; at 0 the bomb is removed and the world turns back.
//! - A thrown grunt rolls rand * 100: at or above topsy_turvy_fall_pct (50), with more than 3 s of the effect left,
//!   it hangs on (grunt_hang_into, grunt_hanging) until the world turns back; otherwise it lets go and falls into
//!   the sky (grunt_blown_up_into, grunt_blown_up, physics off) with a topsy_scream, and is dead when the world
//!   turns back.
//! Simplified, labelled: the view rolls 180 degrees about its axis over the turn instead of following the
//! Camera_topsy track; the world turns back over the same 0.97 s (the original's anim 0x23 is not identified).

use crate::ai::{Grunt, Mind};
use crate::fx::FxStats;
use crate::player::Roman;
use bevy::prelude::*;

#[derive(Clone, Copy, PartialEq, Debug, Default)]
pub enum Phase {
    #[default]
    Off,
    Into,
    Turn,
    Hang,
    Back,
}

/// The effect (Game+0xc80 and the player's topsy flags).
#[derive(Resource, Default)]
pub struct Topsy {
    pub phase: Phase,
    pub t: f32,
    /// topsy_turvy_duration
    pub duration: f32,
    /// the camera's roll, radians (0 upright, PI upside down)
    pub roll: f32,
    pub start: bool,
}

impl Topsy {
    pub fn active(&self) -> bool {
        self.phase != Phase::Off
    }
}

/// A grunt the bomb threw: hanging on, or falling into the sky.
#[derive(Component)]
pub struct Thrown {
    pub hangs: bool,
    pub t: f32,
    pub rise: f32,
}

const INTO: f32 = 2.47;
const TURN: f32 = 0.97;

pub struct TopsyPlugin;

impl Plugin for TopsyPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Topsy>().add_systems(Update, (run, thrown).chain().run_if(crate::playing));
    }
}

#[allow(clippy::too_many_arguments)]
fn run(
    mut commands: Commands,
    time: Res<Time>,
    mut topsy: ResMut<Topsy>,
    data: Res<crate::gamedata::GameData>,
    roman: Query<(&Roman, &GlobalTransform)>,
    mut grunts: Query<(Entity, &mut Grunt, &GlobalTransform, Option<&crate::physics::Body>), Without<Thrown>>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut rng: ResMut<FxStats>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut log: Option<ResMut<crate::mission::Mission>>,
) {
    let dt = time.delta_secs();
    let g = data.props("p_globalsettings");
    let Ok((r, rt)) = roman.single() else { return };
    if topsy.start {
        topsy.start = false;
        if !topsy.active() {
            topsy.phase = Phase::Into;
            topsy.t = 0.0;
            topsy.duration = g.f("topsy_turvy_duration", 5.0);
            fx.write(crate::fx::StartFx { name: "fx_trotter_topsy".into(), at: rt.translation(), rotation: Quat::IDENTITY, follow: None, position_only: true });
            if let Some(m) = log.as_deref_mut() {
                m.log.push("topsy turvy: armed".into());
            }
        }
    }
    if !topsy.active() {
        return;
    }
    let _ = r;
    topsy.t += dt;
    match topsy.phase {
        Phase::Into if topsy.t >= INTO => {
            topsy.phase = Phase::Turn;
            topsy.t = 0.0;
        }
        Phase::Turn => {
            topsy.roll = std::f32::consts::PI * (topsy.t / TURN).min(1.0);
            if topsy.t >= TURN {
                // the weapon fires: every AI within the radius is thrown
                let radius = g.f("topsy_turvy_radius", 100.0);
                let fall_pct = g.f("topsy_turvy_fall_pct", 50.0);
                let me = rt.translation();
                let (mut hang, mut fall) = (0, 0);
                for (e, mut gr, gt, body) in &mut grunts {
                    if gr.mind == Mind::Dead || gt.translation().distance(me) > radius {
                        continue;
                    }
                    let roll = rng.rand() * 100.0;
                    let hangs = roll >= fall_pct && topsy.duration > 3.0;
                    if !hangs {
                        if let Some(b) = body {
                            physics.remove(b.id);
                            commands.entity(e).remove::<crate::physics::Body>();
                        }
                        sounds.write(crate::audio::PlaySound { name: crate::ai::voice("Topsy_Scream", gr.voice_set), at: Some(gt.translation()), follow: None, looped: Some(false), volume: 1.0 });
                        fall += 1;
                    } else {
                        hang += 1;
                    }
                    gr.mind = Mind::WaitTrigger;
                    commands.entity(e).insert(Thrown { hangs, t: 0.0, rise: 0.0 });
                }
                if let Some(m) = log.as_deref_mut() {
                    m.log.push(format!("topsy turvy: {fall} fell, {hang} hung on"));
                }
                topsy.phase = Phase::Hang;
                topsy.t = 0.0;
            }
        }
        Phase::Hang if topsy.t >= topsy.duration => {
            topsy.phase = Phase::Back;
            topsy.t = 0.0;
        }
        Phase::Back => {
            topsy.roll = std::f32::consts::PI * (1.0 - (topsy.t / TURN).min(1.0));
            if topsy.t >= TURN {
                topsy.phase = Phase::Off;
                topsy.roll = 0.0;
            }
        }
        _ => {}
    }
}

/// Thrown grunts: the falling ones rise into the sky (the world is upside down) and are dead when it turns back;
/// the ones that hung on carry on.
#[allow(clippy::too_many_arguments)]
fn thrown(
    mut commands: Commands,
    time: Res<Time>,
    topsy: Res<Topsy>,
    mut q: Query<(Entity, &mut Grunt, &mut Transform, &mut Thrown, &mut crate::fx::Health)>,
    mut stats: ResMut<crate::player::PlayerStats>,
) {
    let dt = time.delta_secs();
    for (e, mut g, mut t, mut th, mut h) in &mut q {
        th.t += dt;
        if th.hangs {
            g.pause = 1.0;
            if !topsy.active() {
                commands.entity(e).remove::<Thrown>();
                g.mind = Mind::Chase;
            }
            continue;
        }
        // falling "up": the game's gravity (64.348 units/s^2) toward the sky
        th.rise += 64.348 * dt;
        t.translation.y += th.rise * dt;
        if !topsy.active() || topsy.phase == Phase::Back {
            h.0 = 0.0;
            g.mind = Mind::Dead;
            g.dead_said = true;
            stats.kills += 1;
            commands.entity(e).remove::<Thrown>();
            commands.entity(e).insert(Visibility::Hidden);
        }
    }
}
