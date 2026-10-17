//! The wall assault of the base-defence levels (army_max_scale 40 on lev_1_m4, lev_2_m4, lev_3_m4_bd, lev_4_m5,
//! lev_5_m5): an army grunt with nothing to do takes a planted ladder or a wall-scale spline near him and goes over the
//! wall. docs/subsystems/defence.md, docs/subsystems/grunt-ai.md section 15.
//!
//! - When (GenericAI_FindHidingSpot 0x0042a7b0, ai_03.cpp:219-224): with the player present and the level started in a
//!   turret (0x0044e600: every defence level), the hiding-spot search first tries the breach (0x00429a50: army.rs
//!   `breach`), then a planted ladder (0x00429ce0), then a wall scale (0x00429ac0). Here an idle army grunt (standing at
//!   the end of his marker set's spline, where the original's hide markers search) asks once a second (guess at the
//!   rate of the original's searches).
//! - Both (ai_02.cpp:2603-2771): vt+0x1c8 (the three grunt classes) and fewer than army_max_scale grunts in the assault
//!   list (World+0xf14, Game+0xc54); the spline heads of the marker list (+0xee4) not in use (spline bit 6, 0x0044b7e0):
//!   0x00429ce0 those with a ladder planted (bit 4, 0x0044b7b0), 0x00429ac0 those whose last marker's action is
//!   ic_scale_base_wall (Spline_EndsAtScaleWall 0x0044b760); the nearest end point (Spline_GetEndPoint 0x0044b710, 2D)
//!   within scan_distance (+0x5dc). Taken: the spline marked in use (0x0044b7f0), +0x484 0x80000, the grunt in the list
//!   with +0x488 0x40000, a path to the end (states 0xaa / 0xac); there the ladder's climb (0x0042bf70) or the scale
//!   (state 0xad, one of mover_grunt_scale / _2 / _3 / _4): army.rs's ic_* scripts, whose last notes send 5000 and
//!   deactivate him (ai/notes.rs).
//! - Leaving (the teardown 0x00432730 with +0x488 0x40000 -> 0x0042ba30, ai_03.cpp:849-877): out of the list, the
//!   spline free again (0x0044b7f0 with cl = 0). Replaced: 0x0042ba30's hand-over of the place to the first waiting AI
//!   (0x0042b9e0) is the next idle grunt's own search here.
//! - The voices (ai_06.cpp:2014-2024: "ClimbTaunt" on arrival, vt+0x16c) are not played (the army's grunts have no bark
//!   set here yet).

use super::{Grunt, Mind, SplineRun};
use crate::army::ArmyGrunt;
use bevy::prelude::*;
use std::collections::{HashMap, HashSet};

/// How often an idle army grunt looks for a way over (guess).
const RETRY: f32 = 1.0;

/// The assault list (World+0xf14) and the splines in use (bit 6).
#[derive(Resource, Default, Debug)]
pub struct Assault {
    /// army_max_scale (Game+0xc54): 0 no assault
    pub max: u32,
    /// the grunts on their way over and their spline
    pub climbers: Vec<(Entity, i64)>,
    pub in_use: HashSet<i64>,
    /// started: by ladder, by scale
    pub ladders: u32,
    pub scales: u32,
    next_try: HashMap<Entity, f32>,
}

/// What a grunt may take.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Way {
    /// a planted ladder (0x00429ce0, state 0xaa)
    Ladder,
    /// a wall scale (0x00429ac0, state 0xac)
    Scale,
}

/// The candidates' choice (0x00429ce0 then 0x00429ac0): the nearest end point (2D) within `scan` of `from` among the
/// free splines of each way, a ladder first. `ends`: (tag, way, end point).
pub fn choose(ends: &[(i64, Way, Vec3)], from: Vec3, scan: f32, in_use: &HashSet<i64>) -> Option<(i64, Way)> {
    for way in [Way::Ladder, Way::Scale] {
        let best = ends
            .iter()
            .filter(|(t, w, _)| *w == way && !in_use.contains(t))
            .map(|(t, _, p)| (*t, (*p - from).with_y(0.0).length()))
            .filter(|(_, d)| *d <= scan)
            .min_by(|a, b| a.1.total_cmp(&b.1));
        if let Some((t, _)) = best {
            return Some((t, way));
        }
    }
    None
}

/// The mission's army_max_scale.
pub fn sync(mission: Option<Res<crate::mission::Mission>>, mut a: ResMut<Assault>) {
    let max = mission.map_or(0, |m| m.army_max_scale);
    if a.max != max {
        a.max = max;
    }
}

/// The search's assault branch for idle army grunts, and the list's upkeep.
#[allow(clippy::type_complexity)]
pub fn recruit(
    time: Res<Time>,
    markers: Option<Res<crate::alarm::Markers>>,
    army: Res<crate::army::Army>,
    mut a: ResMut<Assault>,
    mut q: Query<(Entity, &mut Grunt, &mut ArmyGrunt, &GlobalTransform)>,
) {
    // the dead and the gone leave the list (0x0042ba30) and free their spline
    let mut freed = Vec::new();
    a.climbers.retain(|(e, t)| {
        let keep = q.get(*e).is_ok_and(|(_, g, ..)| g.mind != Mind::Dead);
        if !keep {
            freed.push(*t);
        }
        keep
    });
    for t in freed {
        a.in_use.remove(&t);
    }
    let (Some(mk), true) = (markers, a.max > 0) else { return };
    let now = time.elapsed_secs();
    // the ways over: planted ladders and wall-scale splines, with their end points
    let mut ends: Vec<(i64, Way, Vec3)> = Vec::new();
    for (&tag, s) in &mk.splines {
        let Some(&last) = s.last() else { continue };
        let m = &mk.markers[last];
        let way = if army.is_planted(tag) {
            Way::Ladder
        } else if m.action.eq_ignore_ascii_case("ic_scale_base_wall") {
            Way::Scale
        } else {
            continue;
        };
        ends.push((tag, way, m.pos));
    }
    if ends.is_empty() {
        return;
    }
    for (e, mut g, mut ag, gt) in &mut q {
        if (a.climbers.len() as u32) >= a.max {
            break;
        }
        if !matches!(g.mind, Mind::Roam | Mind::Guard) || g.spline.is_some() || ag.busy() || a.climbers.iter().any(|(c, _)| *c == e) {
            continue;
        }
        let t = a.next_try.entry(e).or_insert(now);
        if now < *t {
            continue;
        }
        *t = now + RETRY;
        let Some((tag, way)) = choose(&ends, gt.translation(), g.scan_distance, &a.in_use) else { continue };
        a.in_use.insert(tag);
        a.climbers.push((e, tag));
        let last = mk.spline_len(tag).saturating_sub(1);
        g.spline = Some(SplineRun { tag, index: last, started: true });
        g.target = None;
        g.guard = false;
        g.mind = Mind::ToMarker;
        if way == Way::Ladder {
            ag.ladder = Some(tag);
            a.ladders += 1;
        } else {
            a.scales += 1;
        }
        info!("EVENT ai: army grunt {e:?} goes over the wall by {way:?} {tag}");
    }
    let live: HashSet<Entity> = q.iter().map(|(e, ..)| e).collect();
    a.next_try.retain(|e, _| live.contains(e));
}

/// The playtest state's `assault`.
pub fn log(a: Res<Assault>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert("assault".into(), serde_json::json!({"max": a.max, "climbers": a.climbers.len(), "ladders": a.ladders, "scales": a.scales}));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ladder_first_then_nearest_scale_in_range() {
        let ends = [(300, Way::Scale, Vec3::new(50.0, 0.0, 0.0)), (301, Way::Scale, Vec3::new(20.0, 0.0, 0.0)), (100, Way::Ladder, Vec3::new(250.0, 0.0, 0.0))];
        let none = HashSet::new();
        assert_eq!(choose(&ends, Vec3::ZERO, 300.0, &none), Some((100, Way::Ladder)));
        assert_eq!(choose(&ends, Vec3::ZERO, 200.0, &none), Some((301, Way::Scale)));
        let used: HashSet<i64> = [301].into();
        assert_eq!(choose(&ends, Vec3::ZERO, 200.0, &used), Some((300, Way::Scale)));
        assert_eq!(choose(&ends, Vec3::ZERO, 10.0, &none), None);
    }
}
