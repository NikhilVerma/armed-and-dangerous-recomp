//! Path planning for the grunts' routes (ai.rs: the route of any walk whose goal is out of sight, the reachability
//! tests of the hiding and gun searches; ai/tower.rs, ai/close_kill.rs: the engine's stand-in for the original's grid
//! router Grid_FindPath 0x0046fd20, pools.rs) and for the playtest bot: A* over a grid sampled from the physics
//! world: ground height by a ray down per cell from just above the cell it is reached from (so the floor under an
//! archway or overhang, not its roof), an edge is walkable when the step is under 50 degrees and nothing static
//! blocks a knee-high ray between the cells. Built on demand around the start and goal.

use crate::physics::Physics;
use bevy::prelude::*;
use std::cmp::Ordering;
use std::collections::{BinaryHeap, HashMap};
use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};

const STEP: f32 = 6.0;
/// the highest drop a route may take (Roman's landing hurts from much higher: fallO, 100 units cost 168.7)
const DROP: f32 = 30.0;
/// the side rays' offset from a walk's middle (Roman's radius 4, less a little: gates and doors stay open)
const CLEARANCE: f32 = 3.0;
/// the extra cost of a step with less room than that
const TIGHT_COST: f32 = 12.0;

/// Nodes a search expands before it gives up with the nearest cell reached (the engine's budget; the original's A*
/// holds 2000 nodes of its own grid: AStar_Add 0x00470600, pools.rs).
pub const DEFAULT_NODE_BUDGET: usize = 150_000;
static NODE_BUDGET: AtomicUsize = AtomicUsize::new(DEFAULT_NODE_BUDGET);

/// The search budget (pools.rs `path_nodes`); shared by every search (they run from many systems with only the
/// physics world at hand).
pub fn set_node_budget(nodes: usize) {
    NODE_BUDGET.store(nodes.max(1), AtomicOrdering::Relaxed);
}

pub fn node_budget() -> usize {
    NODE_BUDGET.load(AtomicOrdering::Relaxed)
}

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
            let down = |top: f32| physics.raycast(Vec3::new(p.x, top, p.y), -Vec3::Y, 400.0).filter(|h| h.2.y > 0.55).map(|h| h.1.y);
            // a wall's or roof's top well above the reaching cell: look again from just above that cell's height,
            // for the floor beside it (the start in a walled yard otherwise sees only wall tops around it)
            match down(from_h + CLIMB) {
                Some(h) if h > from_h + 8.0 => down(from_h + 6.0).or(Some(h)),
                h => h,
            }
        })
    };
    // the start cell is where the walker stands (a ray from above can land on a wall's top next to him)
    heights.insert(cell(from), Some(from.y));
    let (s, g) = (cell(from), cell(to));
    let h = |c: (i32, i32)| pos(c).distance(pos(g));
    let mut open = BinaryHeap::new();
    let mut came: HashMap<(i32, i32), (i32, i32)> = HashMap::new();
    let mut cost: HashMap<(i32, i32), f32> = HashMap::new();
    cost.insert(s, 0.0);
    open.push(Node { f: h(s), i: s });
    let mut best = s;
    let mut expanded = 0;
    let budget = node_budget();
    while let Some(Node { i: c, .. }) = open.pop() {
        if h(c) < h(best) {
            best = c;
        }
        if c == g || h(c) < STEP * 1.5 {
            best = c;
            break;
        }
        expanded += 1;
        if expanded > budget {
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
            // up: a step under 50 degrees; down: a drop of up to DROP (a bot that jumped onto a roof or a wall's top found
            // no way back down and stood there for minutes)
            if hn - hc > d * 1.2 || hc - hn > DROP.max(d * 1.2) {
                continue;
            }
            // from the start cell, rays leave from where the walker actually stands (the cell centre may be inside
            // a wall's collider: the bot then found no route at all)
            let (a, b) = (if c == s { Vec2::new(from.x, from.z) } else { pos(c) }, pos(n));
            let a3 = Vec3::new(a.x, hc + 5.0, a.y);
            // (a drop is tested at the upper level: over the edge, then down)
            let b3 = Vec3::new(b.x, hn.max(hc) + 5.0, b.y);
            // (a middle ray: side rays here closed doorways the 6-unit grid does not line up with)
            if physics.raycast(a3, (b3 - a3).normalize_or(Vec3::X), (b3 - a3).length()).is_some() {
                continue;
            }
            // a step that brushes something (no room for Roman's width) costs more: routes keep off walls and trees
            // where there is room (hugging a wall's end next to a tree wedged him for good)
            let tight = if walkable(physics, a3, b3) { 0.0 } else { TIGHT_COST };
            let nc = cc + d + (hn - hc).max(0.0) * 0.5 + tight;
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
    let at = |c: (i32, i32)| {
        let p = pos(c);
        Vec3::new(p.x, heights.get(&c).copied().flatten().unwrap_or(0.0), p.y)
    };
    // waypoints: every 4th cell, or sooner where the straight walk from the last waypoint to the cell after next
    // would clip something (cutting the corner round a tree or a wall's end snagged the walker)
    let mut pts = vec![at(cells[0])];
    let mut last = 0;
    for i in 1..cells.len() {
        let next = cells[(i + 1).min(cells.len() - 1)];
        let (a, b) = (pts[pts.len() - 1] + Vec3::Y * 5.0, at(next) + Vec3::Y * 5.0);
        if i == cells.len() - 1 || i - last >= 4 || !walkable(physics, a.with_y(a.y.max(b.y)), b.with_y(a.y.max(b.y))) {
            pts.push(at(cells[i]));
            last = i;
        }
    }
    Some(pts)
}

/// Debug: the planner's view of the ground round `center` (engine space) as text, one character per cell, rows
/// north (game +y) first: '.' open, '|' the way from the west neighbour blocked, '-' from the north one, '+' both,
/// 'x' no walkable ground, '^' / 'v' ground over 8 units above / below the centre. For the bot's NAVMAP variable.
pub fn ascii_map(physics: &Physics, center: Vec3, cells: i32) -> String {
    let ground = |x: f32, z: f32| physics.raycast(Vec3::new(x, center.y + 20.0, z), -Vec3::Y, 400.0).filter(|h| h.2.y > 0.55).map(|h| h.1.y);
    let open = |a: Vec3, b: Vec3| physics.raycast(a, (b - a).normalize_or(Vec3::X), (b - a).length()).is_none();
    let mut out = String::new();
    for j in -cells..=cells {
        for i in -cells..=cells {
            let (x, z) = (center.x + i as f32 * STEP, center.z + j as f32 * STEP);
            let c = match ground(x, z) {
                None => 'x',
                Some(h) if h > center.y + 8.0 => '^',
                Some(h) if h < center.y - 8.0 => 'v',
                Some(h) => {
                    let p = Vec3::new(x, h + 5.0, z);
                    let w = !open(p, p - Vec3::X * STEP);
                    let n = !open(p, p - Vec3::Z * STEP);
                    match (w, n) {
                        (true, true) => '+',
                        (true, false) => '|',
                        (false, true) => '-',
                        _ => '.',
                    }
                }
            };
            out.push(c);
        }
        out.push('\n');
    }
    out
}

/// Whether the walker fits along a straight walk from `a` to `b` (knee height), for the waypoints: a ray down the
/// middle and one each side at CLEARANCE (Roman's collision radius is 4: with a middle ray only, walks between
/// waypoints four cells apart cut corners and snagged on trees and wall ends).
fn walkable(physics: &Physics, a: Vec3, b: Vec3) -> bool {
    let v = b - a;
    let dir = v.normalize_or(Vec3::X);
    let side = Vec3::new(-dir.z, 0.0, dir.x).normalize_or_zero() * CLEARANCE;
    [Vec3::ZERO, side, -side].iter().all(|o| physics.raycast(a + *o, dir, v.length()).is_none())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The node budget (pools.rs `path_nodes`) bounds a search: across open ground 300 units off, a budget of 20
    /// nodes ends near the start, the default reaches the goal.
    #[test]
    fn node_budget_bounds_the_search() {
        let mut physics = Physics::new(0.2, crate::physicsstep::PhysicsStepSettings::original().world());
        let v = [Vec3::new(-400.0, 0.0, -400.0), Vec3::new(-400.0, 0.0, 400.0), Vec3::new(400.0, 0.0, 400.0), Vec3::new(400.0, 0.0, -400.0)];
        physics.add_static_mesh(&v, &[[0, 1, 2], [0, 2, 3]]).expect("ground");
        let (from, to) = (Vec3::new(0.0, 0.0, 0.0), Vec3::new(300.0, 0.0, 0.0));
        set_node_budget(20);
        let short = path(&physics, from, to, &[]).map_or(0.0, |r| r.last().map_or(0.0, |w| w.x));
        set_node_budget(DEFAULT_NODE_BUDGET);
        let full = path(&physics, from, to, &[]).expect("a route").last().copied().expect("a waypoint");
        assert!(short < 150.0, "{short}");
        assert!(full.distance(to) < STEP * 2.0, "{full}");
    }
}
