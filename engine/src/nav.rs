//! Path planning for the playtest bot (debug tooling, not game logic): A* over a grid sampled from the physics
//! world: ground height by a ray down per cell from just above the cell it is reached from (so the floor under an
//! archway or overhang, not its roof), an edge is walkable when the step is under 50 degrees and nothing static
//! blocks a knee-high ray between the cells. Built on demand around the start and goal.

use crate::physics::Physics;
use bevy::prelude::*;
use std::cmp::Ordering;
use std::collections::{BinaryHeap, HashMap};

const STEP: f32 = 6.0;

#[derive(Copy, Clone, PartialEq)]
struct Node {
    f: f32,
    i: (i32, i32),
}
impl Eq for Node {}
impl Ord for Node {
    fn cmp(&self, o: &Self) -> Ordering {
        o.f.partial_cmp(&self.f).unwrap_or(Ordering::Equal)
    }
}
impl PartialOrd for Node {
    fn partial_cmp(&self, o: &Self) -> Option<Ordering> {
        Some(self.cmp(o))
    }
}

/// Waypoints (engine space, on the ground) from `from` to `to`, or None when no path is found within the search
/// limits.
pub fn path(physics: &Physics, from: Vec3, to: Vec3, blocked: &[Vec3]) -> Option<Vec<Vec3>> {
    let cell = |p: Vec3| ((p.x / STEP).round() as i32, (p.z / STEP).round() as i32);
    let pos = |c: (i32, i32)| Vec2::new(c.0 as f32 * STEP, c.1 as f32 * STEP);
    let mut heights: HashMap<(i32, i32), Option<f32>> = HashMap::new();
    // the first sample of a cell decides it: a ray down from CLIMB above the height it is reached from
    const CLIMB: f32 = 20.0;
    let ground = |heights: &mut HashMap<(i32, i32), Option<f32>>, c: (i32, i32), from_h: f32| -> Option<f32> {
        *heights.entry(c).or_insert_with(|| {
            let p = pos(c);
            physics.raycast(Vec3::new(p.x, from_h + CLIMB, p.y), -Vec3::Y, 400.0).filter(|h| h.2.y > 0.55).map(|h| h.1.y)
        })
    };
    // the start: where the walker stands (a ray from above may meet a steep object it is against)
    if ground(&mut heights, cell(from), from.y).is_none() {
        heights.insert(cell(from), Some(from.y));
    }
    let (s, g) = (cell(from), cell(to));
    let h = |c: (i32, i32)| pos(c).distance(pos(g));
    let mut open = BinaryHeap::new();
    let mut came: HashMap<(i32, i32), (i32, i32)> = HashMap::new();
    let mut cost: HashMap<(i32, i32), f32> = HashMap::new();
    cost.insert(s, 0.0);
    open.push(Node { f: h(s), i: s });
    let mut best = s;
    let mut expanded = 0;
    while let Some(Node { i: c, .. }) = open.pop() {
        if h(c) < h(best) {
            best = c;
        }
        if c == g || h(c) < STEP * 1.5 {
            best = c;
            break;
        }
        expanded += 1;
        if expanded > 150000 {
            break;
        }
        let Some(hc) = heights.get(&c).copied().flatten() else { continue };
        let cc = cost[&c];
        for (dx, dz) in [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)] {
            let n = (c.0 + dx, c.1 + dz);
            let Some(hn) = ground(&mut heights, n, hc) else { continue };
            // spots where the bot got stuck before are avoided
            let np = pos(n);
            if blocked.iter().any(|b| Vec2::new(b.x, b.z).distance(np) < 10.0) {
                continue;
            }
            let d = if dx != 0 && dz != 0 { STEP * 1.414 } else { STEP };
            if (hn - hc).abs() > d * 1.2 {
                continue;
            }
            let (a, b) = (pos(c), pos(n));
            let a3 = Vec3::new(a.x, hc + 5.0, a.y);
            let b3 = Vec3::new(b.x, hn + 5.0, b.y);
            if physics.raycast(a3, (b3 - a3).normalize_or(Vec3::X), (b3 - a3).length()).is_some() {
                continue;
            }
            let nc = cc + d + (hn - hc).max(0.0) * 0.5;
            if cost.get(&n).is_none_or(|&old| nc < old) {
                cost.insert(n, nc);
                came.insert(n, c);
                open.push(Node { f: nc + h(n), i: n });
            }
        }
    }
    if best == s {
        return None;
    }
    let mut cells = vec![best];
    while let Some(&p) = came.get(cells.last().unwrap()) {
        cells.push(p);
    }
    cells.reverse();
    // keep every 4th cell as a waypoint
    let pts: Vec<Vec3> = cells
        .iter()
        .enumerate()
        .filter(|(i, _)| i % 4 == 0 || *i == cells.len() - 1)
        .map(|(_, &c)| {
            let p = pos(c);
            Vec3::new(p.x, heights.get(&c).copied().flatten().unwrap_or(0.0), p.y)
        })
        .collect();
    Some(pts)
}
