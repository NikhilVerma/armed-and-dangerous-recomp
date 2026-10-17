//! The pre-success retreat of the base defences (MissionObject_OnTrigger 0x0044d600's receive_trigger_pre_success with
//! the level started in a turret, 0x0044e600; army.rs stops the army): every AI of the AI list (World+0xe7c, vt+0x10c)
//! is handed the retreat direction (vt+0x234 0x0042db00, ai_03.cpp:2108-2119) and runs off. docs/subsystems/defence.md.
//!
//! - The direction: the facing of the head of the marker spline tagged -1 (Marker_FindSpline 0x0044c030 with -1,
//!   0x00453150); none: "Pre-Success Marker not found" and (0, 1, 0).
//! - 0x0042db00: a delay +0x4a8 = rand() x 3.0212e-5 + 0.01 (0.01 .. 1.0 s), the direction into +0x908, +0x48c 0x2000.
//! - The delay's end (GenericAI_Update ai_06.cpp:373-399): not when dead, falling (state 0x82) or a teammate (vt+0xe4);
//!   +0x488 0x100000, a route to his position + direction x 2000 (K 0x005bcc08), 20 % ((rand(100)) < 20) the scared
//!   run (+0x484 0x20: anim 0xb run_scared) with his scream (vt+0x160), state 0xae (along the route; it does not end).

use super::{Grunt, Mind};
use bevy::prelude::*;

/// How far the retreat runs (K 0x005bcc08).
pub const DISTANCE: f32 = 2000.0;

/// An AI handed the retreat: the delay left and the direction (engine axes).
#[derive(Component, Clone, Copy, Debug)]
pub struct Retreat {
    pub delay: f32,
    pub dir: Vec3,
}

/// Retreats started (playtest log `retreat`).
#[derive(Resource, Default, Debug)]
pub struct RetreatStats {
    pub handed: u32,
    pub running: u32,
    pub scared: u32,
}

/// The direction of the retreat: the head of spline -1's facing, else game (0, 1, 0).
pub fn direction(markers: Option<&crate::alarm::Markers>) -> Vec3 {
    markers
        .and_then(|m| m.spline_marker(-1, 0))
        .map(|m| m.dir.with_y(0.0).normalize_or_zero())
        .filter(|d| *d != Vec3::ZERO)
        .unwrap_or_else(|| {
            warn!("Pre-Success Marker not found");
            crate::level::game_to_engine() * Vec3::Y
        })
}

/// vt+0x234 on every live AI: the retreat with its delay (0x0042db00).
pub fn hand_out(commands: &mut Commands, grunts: impl Iterator<Item = (Entity, Mind)>, dir: Vec3, rng: &mut crate::fx::FxStats, stats: &mut RetreatStats) {
    for (e, mind) in grunts {
        if mind == Mind::Dead {
            continue;
        }
        let delay = rng.rand() * 32768.0 * 3.0212e-5 + 0.01;
        commands.entity(e).insert(Retreat { delay, dir });
        stats.handed += 1;
    }
}

/// The delays and the run.
pub fn run(mut commands: Commands, time: Res<Time>, mut q: Query<(Entity, &mut Retreat, &mut Grunt, &GlobalTransform)>, mut rng: ResMut<crate::fx::FxStats>, mut stats: ResMut<RetreatStats>, mut sounds: MessageWriter<crate::audio::PlaySound>) {
    let dt = time.delta_secs();
    for (e, mut r, mut g, gt) in &mut q {
        r.delay -= dt;
        if r.delay > 0.0 {
            continue;
        }
        commands.entity(e).remove::<Retreat>();
        if matches!(g.mind, Mind::Dead | Mind::Falling) {
            continue;
        }
        let pos = gt.translation();
        g.spline = None;
        g.fixed = None;
        g.target = None;
        g.cover = None;
        g.goal = pos + r.dir * DISTANCE;
        g.timer = 1.0e6;
        stats.running += 1;
        // 20 %: the scared run (anim 0xb) and his scream
        if rng.rand() * 100.0 < 20.0 {
            g.mind = Mind::Flee;
            stats.scared += 1;
            sounds.write(crate::audio::PlaySound::at(&super::voice("General_Scream", g.voice_set), pos));
        } else {
            g.mind = Mind::Wander;
        }
    }
}

/// The playtest state's `retreat`.
pub fn log(stats: Res<RetreatStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert("retreat".into(), serde_json::json!({"handed": stats.handed, "running": stats.running, "scared": stats.scared}));
}
