//! Grunts in mode "bully" (+0x488 0x10000000): they hold a peasant (the PeasantObject whose tag is their obj_tag, list
//! +0xef8) and scare him now and then until they see the player or the peasant dies. docs/subsystems/infantry.md
//! section 5.
//! - Start (AI_ResolveStartObjects 0x00425e20, ai_02.cpp:181-202): the peasant found: timer 10..30 s, the stand set,
//!   the peasant's peasant_man_stand, state 0x118 (`Mind::Bully`); not found: ready roaming (state 1).
//! - State 0x118 (ai_06.cpp:2546-2642): the timer out -> under the game-wide bully delay (bully_delay_min / max 60..75 s)
//!   a line (70 % the peasant's, 30 % the grunt's), then grunt_scare (anim 0x7d) with the peasant's Peasant_man_scare;
//!   its end: a new 10..30 s and the stand set again.
//! - The end (0x0042cc70 with a target; the peasant dead or ragdolled): the peasant released to peasant_man_stand, the
//!   target pursued (0x0042ce70 for a dead peasant: the player).

use super::{Grunt, Mind, Player};
use crate::fx::{Destructible, FxStats};
use crate::level::InstanceId;
use bevy::prelude::*;

/// A bully's hold on his peasant (+0x88c).
#[derive(Clone, Copy, Debug)]
pub struct Bully {
    /// the peasant's instance id (level.rs: by obj_tag among the placed peasants)
    pub peasant_id: u32,
    pub peasant: Option<Entity>,
    /// +0x494: the time to the next scare
    pub timer: f32,
    /// grunt_scare playing: its time left
    pub scaring: Option<f32>,
    /// scares so far (playtest log)
    pub scares: u32,
}

impl Bully {
    pub fn new(peasant_id: u32, rng: &mut crate::fxvm::CrtRand) -> Bully {
        // rand x 0.00061035 + 10: 10..30 s
        Bully { peasant_id, peasant: None, timer: rng.rand() as f32 * 0.000_610_35 + 10.0, scaring: None, scares: 0 }
    }
}

/// The game-wide bully line delay (Game+0xb74 / +0xb80, re-rolled from bully_delay_min / max, game_02.cpp:2099-2104).
#[derive(Resource, Default)]
pub struct BullyLines {
    last: Option<f32>,
    delay: f32,
}

const PEASANT_LINES: [&str; 7] =
    ["olm50003_mission", "olm50007_bullied", "olw50002_bullied", "olw50004_mission", "pe150038_peasantcaptured", "pe250004_bullied", "pe250032_peasantcaptured"];

/// The bullies' frame (after think): the peasant found, the scares and their lines, the peasant's anims, the end.
#[allow(clippy::too_many_arguments)]
pub fn bullies(
    time: Res<Time>,
    mut grunts: Query<(Entity, &mut Grunt, &Transform)>,
    mut peasants: Query<(Entity, &InstanceId, &Transform, Option<&Destructible>, Option<&mut crate::skin::Animator>, Option<&crate::peasant::Peasant>), Without<Grunt>>,
    player: Query<Entity, With<Player>>,
    mut lines: ResMut<BullyLines>,
    mut rng: ResMut<FxStats>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut lib: ResMut<crate::skin::AnimLibrary>,
) {
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    for (ge, mut g, tr) in &mut grunts {
        let Some(mut b) = g.bully else { continue };
        if b.peasant.is_none() {
            b.peasant = peasants.iter().find(|p| p.1 .0 == b.peasant_id).map(|p| p.0);
            if b.peasant.is_none() {
                // "Can't find bully target ... switching to ready roaming"
                warn!("ai: bully {ge:?} finds no peasant {}", b.peasant_id);
                g.bully = None;
                g.attention = false;
                continue;
            }
        }
        let pe = b.peasant.expect("found above");
        // dead, or ragdolled (+0x421: tied on Roman's rope, peasant.rs)
        let dead = peasants.get(pe).map_or(true, |p| p.3.is_some_and(|d| d.health <= 0.0) || p.5.is_some_and(|x| x.rope.is_some()));
        let set = |peasants: &mut Query<(Entity, &InstanceId, &Transform, Option<&Destructible>, Option<&mut crate::skin::Animator>, Option<&crate::peasant::Peasant>), Without<Grunt>>, n: &str, looping: bool| {
            if let Ok((_, _, _, _, Some(mut a), _)) = peasants.get_mut(pe) {
                a.play(0, n, looping, crate::skin::Mask::All);
            }
        };
        // the end: a target (the player seen, a hit) or the peasant dead
        if g.mind == Mind::Dead || g.target.is_some() || dead {
            if !dead {
                set(&mut peasants, "peasant_man_stand", true);
            }
            if dead && g.target.is_none() && g.mind != Mind::Dead {
                g.target = player.single().ok();
                g.mind = Mind::Chase;
            }
            g.bully = None;
            info!("EVENT ai: bully {ge:?} lets his peasant go");
            continue;
        }
        // state 0x118 (a hurt or alerted bully has a target: the end above)
        g.mind = Mind::Bully;
        match b.scaring {
            Some(left) if left - dt > 0.0 => b.scaring = Some(left - dt),
            Some(_) => {
                // the scare's end: 10..30 s, the stand set, the peasant standing
                b.scaring = None;
                b.timer = rng.range(10.0, 30.0);
                set(&mut peasants, "peasant_man_stand", true);
            }
            None => {
                b.timer -= dt;
                if b.timer <= 0.0 {
                    // a line under the shared delay: 70 % the peasant's, 30 % the grunt's
                    if lines.last.is_none_or(|t| now - t >= lines.delay) {
                        lines.last = Some(now);
                        lines.delay = rng.range(60.0, 75.0);
                        let at = peasants.get(pe).map(|p| p.2.translation).unwrap_or(tr.translation);
                        if rng.rand() < 0.7 {
                            let n = PEASANT_LINES[((rng.rand() * 7.0) as usize).min(6)];
                            sounds.write(crate::audio::PlaySound::at(n, at));
                        } else {
                            let n = match g.voice_set {
                                0 => "gr150015_ambient",
                                2 => "gr350016_taunt",
                                _ if rng.rand() < 0.5 => "gr250013_ambient",
                                _ => "gr250019_ambient",
                            };
                            sounds.write(crate::audio::PlaySound::at(n, tr.translation));
                        }
                    }
                    // grunt_scare (0x7d) and the peasant's Peasant_man_scare (both sexes)
                    let n = g.anim(0x7d, 0.0);
                    b.scaring = Some(lib.anim(n).map(|a| a.duration).unwrap_or(1.0));
                    b.scares += 1;
                    set(&mut peasants, "peasant_man_scare", false);
                    info!("EVENT ai: bully {ge:?} scares his peasant");
                }
            }
        }
        g.bully = Some(b);
    }
}
