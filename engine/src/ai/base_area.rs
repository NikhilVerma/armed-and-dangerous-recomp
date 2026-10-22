//! The defence levels' cull of AIs outside the base (GenericAI_Update 0x0043ee90, ai_06.cpp:403-429). In a level that
//! starts with Roman in a turret (0x0044e600: a player and start_in_turret_tag), each AI's timer +0xb34 runs down, not
//! while he is dead (0x00454780), in an IC move (0x00426630: Mind::Scripted), waiting in state 0x8c with +0x488
//! 0x8000000 (0x00426610: a hidden IC grunt) or attached to an object (0x004539e0: a turret boarder). At 0 it is rolled
//! again (rand 3..5 s) and his position (game x / y) is tested against the mission's two rectangles base_x_min_N ..
//! base_y_max_N (Game+0xbd0..0xbec, `Mission::base_areas`); inside neither, vt+0xd4(0): his death. The timer starts at 0
//! (the object's zeroed memory), so the first test comes on his first update.

use super::{Grunt, Mind};
use bevy::prelude::*;

/// The timer +0xb34 of one AI (absent: 0, due now).
#[derive(Component, Clone, Copy, Debug, Default)]
pub struct BaseCheck {
    pub left: f32,
}

/// AIs culled (the playtest state's `base_culled`).
#[derive(Resource, Default, Debug)]
pub struct BaseAreaStats {
    pub culled: u32,
}

/// Whether game-space `(x, y)` lies within `[x_min, y_min, x_max, y_max]` (bounds included, as the original's `<` / `>`
/// tests leave them in).
pub fn inside(r: [f32; 4], x: f32, y: f32) -> bool {
    !(x < r[0] || y < r[1] || x > r[2] || y > r[3])
}

/// The cull: every AI's timer, and the test when it runs out.
#[allow(clippy::type_complexity)]
pub fn cull(
    mut commands: Commands,
    time: Res<Time>,
    mission: Option<Res<crate::mission::Mission>>,
    player: Query<(), With<super::Player>>,
    mut q: Query<(Entity, &Grunt, &GlobalTransform, Option<&mut BaseCheck>, Has<super::ic::IcEntry>), Without<crate::defence::TurretBoarder>>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut ends: MessageWriter<super::notes::ScriptEnd>,
    mut stats: ResMut<BaseAreaStats>,
) {
    let Some(m) = mission.filter(|m| m.start_in_turret) else { return };
    if player.is_empty() {
        return;
    }
    let dt = time.delta_secs();
    for (e, g, gt, check, hidden) in &mut q {
        if matches!(g.mind, Mind::Dead | Mind::Scripted) || hidden {
            continue;
        }
        let left = check.as_ref().map_or(0.0, |c| c.left) - dt;
        let due = left <= 0.0;
        let next = if due { rng.range(3.0, 5.0) } else { left };
        match check {
            Some(mut c) => c.left = next,
            None => {
                commands.entity(e).insert(BaseCheck { left: next });
            }
        }
        if !due {
            continue;
        }
        // engine (x, y, z) is game (x, -z, y)
        let p = gt.translation();
        let (x, y) = (p.x, -p.z);
        if !m.base_areas.iter().any(|&r| inside(r, x, y)) {
            stats.culled += 1;
            ends.write(super::notes::ScriptEnd { entity: e, end: super::notes::End::Kill });
            info!("EVENT ai: {e:?} outside the base at ({x:.0}, {y:.0}): culled");
        }
    }
}

/// The playtest state's `base_culled`.
pub fn log(stats: Res<BaseAreaStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    if let Some(mut s) = state {
        s.0.insert("base_culled".into(), stats.culled.into());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// lev_1_m4's rectangles: the gate area and the yard; a point between them is in the second.
    #[test]
    fn rectangles() {
        let a = [702.0, -2968.0, 1083.0, -2612.0];
        let b = [600.0, -2621.0, 1210.0, -1001.0];
        assert!(inside(a, 900.0, -2700.0));
        assert!(!inside(a, 900.0, -2000.0) && inside(b, 900.0, -2000.0));
        assert!(!inside(a, 0.0, 0.0) && !inside(b, 0.0, 0.0));
        assert!(inside(a, 702.0, -2612.0));
        // the zero rectangle of a level without one holds only the origin
        assert!(!inside([0.0; 4], 1.0, 0.0));
    }
}
