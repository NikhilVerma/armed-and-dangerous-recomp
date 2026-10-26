//! The AI navigation grid (Game+0xaa4) and its router: the original's 10-unit cell grid over the landscape, built at
//! level load from the landscape's triangles (PathGrid_Build 0x0046ed30) and the footprints of the objects that block
//! paths (Object_MarkGrid 0x004552f0, DefaultGameObject_MarkGrid 0x004509a0), searched by a 4-neighbour A* whose path
//! is smoothed by straight grid lines (Grid_FindPath 0x0046fd20, Path_Smooth 0x00470100, Grid_LineClear 0x00470310).
//!
//! Layout (ai_07.cpp "The AI navigation grid"): cells of 10 units from the landscape's minimum snapped down to a
//! multiple of 10 (SnapTen), two bits per cell: 1 the landscape's blocks (steep or `nonpath` surfaces) and footprints
//! marked with mode 1, 2 the footprints marked with mode 2 (default objects, and the places a companion keeps out of
//! its route search: 0x00483d60). A cell is free when both bits are clear (Grid_IsFree 0x0046f2e0).
//!
//! Positions here are game space (x, y on the ground plane, z up); the router ignores height, as the original's
//! route nodes do (z 0, AIRoute_Build 0x0046f6d0).
//!
//! The companions route with it (ai.rs `formation_route`, 0x00483d60). The grunts' routes still use nav.rs, the
//! engine's stand-in over the physics world.

use bevy::prelude::*;
use std::collections::HashMap;

/// A cell's side (0x005bcb68 = 10; 0x005bcbfc = 0.1 its inverse).
pub const CELL: f32 = 10.0;
const INV_CELL: f32 = 0.1;
/// The A* node array's capacity in the original (AStar_Add 0x00470600: 0x7d0; the heap +0x34 has the same).
pub const ORIGINAL_NODES: usize = 2000;
/// The most path points a search keeps (Grid_FindPath's frame: 400 24-byte points).
const MAX_PATH_POINTS: usize = 400;
/// The most points one plan puts on a route (AIRoute_Plan 0x0046fa80: Grid_FindPathWorld(.., 5, ..)).
pub const PLAN_POINTS: usize = 5;
/// A surface steeper than this blocks the cells it covers: the plane normal's z below 0.64278 (0x005bcd0c, cos 50
/// degrees), PathGrid_Build 0x0046ed30.
const STEEP_Z: f32 = 0.642_78;

/// The bit a mark sets (Grid_SetCell 0x0046ece0's 2-bit value).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mark {
    /// bit 1: the landscape's blocks, and the footprints of objects marked with mode 1 (buildings, trees, default
    /// objects without object_block)
    Static = 1,
    /// bit 2: footprints marked with mode 2 (default objects with object_block, the pub, kegs, turrets) and the
    /// places a companion keeps out of its route search
    Dynamic = 2,
}

/// A path point (Grid_FindPath's 24-byte record: position, cell; the direction byte is 0 for the start and goal).
#[derive(Clone, Copy, Debug, PartialEq)]
struct PathPoint {
    pos: Vec2,
    cell: IVec2,
}

/// The directions (Grid_Step 0x004704f0): 1 x+1, 2 x-1, 3 y-1, 4 y+1.
fn step(c: IVec2, dir: u8) -> IVec2 {
    match dir {
        1 => c + IVec2::X,
        2 => c - IVec2::X,
        3 => c - IVec2::Y,
        4 => c + IVec2::Y,
        _ => c,
    }
}

/// The steps the search takes out of a node by the direction it was reached in: all four from the start, never back
/// the way it came (the tables at 0x005ce42c, read from the executable's data: {1, 2, 3, 4}, {1, 3, 4}, {2, 3, 4},
/// {3, 1, 2}, {4, 1, 2}).
fn onward(dir: u8) -> &'static [u8] {
    match dir {
        1 => &[1, 3, 4],
        2 => &[2, 3, 4],
        3 => &[3, 1, 2],
        4 => &[4, 1, 2],
        _ => &[1, 2, 3, 4],
    }
}

/// The 28-bit cell part of a search key (x << 14 | y, 14 bits each), which identifies a node and ends the search.
fn key(c: IVec2) -> u32 {
    (((c.x as u32) & 0x3fff) << 14) | ((c.y as u32) & 0x3fff)
}

/// A search node (24 bytes in the original: key with the arrival direction and the open bit, f, g, h, parent).
struct Node {
    cell: IVec2,
    dir: u8,
    f: f32,
    g: f32,
    h: f32,
    parent: Option<usize>,
    open: bool,
}

/// The A* search state (Game+0xaa4 +0x20..+0x38): the node array, its cell index (the 251-bucket hash table) and the
/// 1-based binary heap ordered by f.
struct Search {
    nodes: Vec<Node>,
    index: HashMap<u32, usize>,
    heap: Vec<usize>,
    capacity: usize,
}

impl Search {
    fn new(capacity: usize) -> Self {
        Search { nodes: Vec::new(), index: HashMap::new(), heap: vec![usize::MAX], capacity }
    }

    /// AStar_HeapUp 0x004705c0: an item rises while its f is not above its parent's.
    fn heap_up(&mut self, mut i: usize) {
        let item = self.heap[i];
        while i > 1 {
            let p = i >> 1;
            if self.nodes[item].f > self.nodes[self.heap[p]].f {
                break;
            }
            self.heap[i] = self.heap[p];
            i = p;
        }
        self.heap[i] = item;
    }

    /// AStar_HeapDown 0x00470560: the top sinks to the smaller child (the right one only when strictly smaller) while
    /// its f is above it.
    fn heap_down(&mut self) {
        let n = self.heap.len() - 1;
        let item = self.heap[1];
        let (mut i, mut c) = (1, 2);
        while c <= n {
            if c < n && self.nodes[self.heap[c + 1]].f < self.nodes[self.heap[c]].f {
                c += 1;
            }
            if self.nodes[item].f <= self.nodes[self.heap[c]].f {
                break;
            }
            self.heap[i] = self.heap[c];
            i = c;
            c *= 2;
        }
        self.heap[i] = item;
    }

    /// AStar_Push 0x00470760: onto the open heap while it has room, marked open.
    fn push(&mut self, node: usize) {
        if self.heap.len() - 1 >= self.capacity {
            return;
        }
        self.heap.push(node);
        self.heap_up(self.heap.len() - 1);
        self.nodes[node].open = true;
    }

    /// AStar_Add 0x00470600: a new node, indexed and pushed; refused once the array is full.
    fn add(&mut self, node: Node) {
        if self.nodes.len() >= self.capacity {
            return;
        }
        let i = self.nodes.len();
        self.index.insert(key(node.cell), i);
        self.nodes.push(node);
        self.push(i);
    }

    /// AStar_PopMin 0x00470690: the lowest f off the heap, no longer open.
    fn pop(&mut self) -> Option<usize> {
        let n = self.heap.len() - 1;
        if n == 0 {
            return None;
        }
        let top = self.heap[1];
        self.heap[1] = self.heap[n];
        self.heap.pop();
        if self.heap.len() > 1 {
            self.heap_down();
        }
        self.nodes[top].open = false;
        Some(top)
    }

    /// AStar_Visit 0x00470240: a neighbour reached at cost `g`. A node already reached as cheaply keeps its cost; an
    /// open one takes the new f / g / h but keeps its parent and its heap place (AStar_CopyCost 0x004706d0 copies the
    /// costs only, nothing re-sorts it); a closed one goes back on the heap as it was.
    fn visit(&mut self, dir: u8, cell: IVec2, g: f32, parent: usize, goal: IVec2) {
        let existing = self.index.get(&key(cell)).copied();
        if let Some(n) = existing {
            if self.nodes[n].g <= g {
                return;
            }
        }
        let h = heuristic(cell, goal);
        let f = h + g;
        match existing {
            None => self.add(Node { cell, dir, f, g, h, parent: Some(parent), open: false }),
            Some(n) if self.nodes[n].open => {
                let node = &mut self.nodes[n];
                (node.f, node.g, node.h) = (f, g, h);
            }
            Some(n) => self.push(n),
        }
    }

    /// AStar_Closest 0x004701e0: of the nodes off the heap, the one nearest the goal (smallest h, the first of
    /// equals, below 999999).
    fn closest(&self) -> Option<usize> {
        let mut best = (999_999.0, None);
        for (i, n) in self.nodes.iter().enumerate() {
            if !n.open && best.0 > n.h {
                best = (n.h, Some(i));
            }
        }
        best.1
    }
}

/// AStar_Heuristic 0x00470700: the straight cell distance to the goal (each step costs 1, AStar_StepCost 0x004706f0).
fn heuristic(c: IVec2, goal: IVec2) -> f32 {
    let (dx, dy) = ((c.x & 0x3fff) as f32 - goal.x as f32, (c.y & 0x3fff) as f32 - goal.y as f32);
    (dy * dy + dx * dx).sqrt()
}

/// A path search's outcome (Grid_FindPath's result): the points after the start, and whether it reached the goal.
#[derive(Debug, Clone, PartialEq)]
pub struct Found {
    pub points: Vec<Vec2>,
    /// 1: the goal (or a straight line to it), 2: as near as it got (the open heap ran dry, or the straight line
    /// ended in front of the goal's blocked cell)
    pub result: u8,
}

/// A planned route (AIRoute_Build 0x0046f6d0 with AIRoute_Plan 0x0046fa80): the points to walk, and whether the last
/// one is the target node still to be planned (a plan puts at most five points in front of it; the target is planned
/// again from where the walker is once it is next, AIRoute_Update 0x00470aa0).
#[derive(Debug, Clone, PartialEq)]
pub struct Route {
    pub points: Vec<Vec2>,
    pub target_unplanned: bool,
}

/// The cells an object's footprint marked, kept to unmark them (DefaultGameObject_MarkGrid's +0x4ec / +0x4f0 for the
/// one-cell kind, the set rebuilt from the mesh for the other).
struct Footprint {
    cells: Vec<IVec2>,
    mark: Mark,
    /// never cleared: a default object marked with mode 1 (+0x4d0 bit 0x4000) ignores the unmark
    /// (DefaultGameObject_MarkGrid returns at once when not enabling)
    permanent: bool,
}

/// The level's navigation grid (Game+0xaa4), kept in the physics world (physics.rs `nav_grid`).
pub struct NavGrid {
    min: Vec2,
    max: Vec2,
    size: IVec2,
    /// two bits per cell (the original packs four cells a byte)
    bits: Vec<u8>,
    footprints: HashMap<Entity, Footprint>,
}

/// SnapTen (loader_01.cpp, written by 0x0046ed30): down to a multiple of 10 by the integer part's remainder; a
/// negative coordinate always moves by 10 - remainder (so -3710.0 becomes -3720.0).
fn snap_ten(x: f32) -> f32 {
    if x >= 0.0 {
        x - ((x as i64 as u32) % 10) as f32
    } else {
        x - (CELL - ((-x as i64 as u32) % 10) as f32)
    }
}

/// The plane normal of a triangle as Plane_FromPoints 0x004e80b0 makes it for PathGrid_Build (points v2, v0, v1):
/// (v1 - v0) x (v2 - v0), normalised when longer than 0.
fn normal(t: [Vec3; 3]) -> Vec3 {
    let n = (t[1] - t[0]).cross(t[2] - t[0]);
    let len = n.length();
    if len > 0.0 {
        n / len
    } else {
        n
    }
}

/// Poly_ClipToPlane 0x0046e830 against an axis line in the ground plane: keeps the side where `sign * (v - at) >= 0`
/// on that axis, inserting the crossings. Returns the clipped polygon.
fn clip(poly: &[Vec3], axis: usize, sign: f32, at: f32) -> Vec<Vec3> {
    let mut out = Vec::with_capacity(poly.len() + 2);
    let Some(&last) = poly.last() else { return out };
    let dist = |v: Vec3| sign * (v[axis] - at);
    let (mut prev, mut d_prev) = (last, dist(last));
    for &cur in poly {
        let d_cur = dist(cur);
        if (d_prev < 0.0) != (d_cur < 0.0) {
            let t = d_prev / (d_prev - d_cur);
            out.push(prev * (1.0 - t) + cur * t);
        }
        if d_cur >= 0.0 {
            out.push(cur);
        }
        (prev, d_prev) = (cur, d_cur);
    }
    out
}

impl Default for NavGrid {
    /// A grid of one free cell, until a level makes its own.
    fn default() -> Self {
        NavGrid::new(Vec2::ZERO, Vec2::ZERO)
    }
}

impl NavGrid {
    /// An open grid over `min`..`max` (game x / y): the landscape's bounds, the minimum snapped down to a multiple
    /// of 10 (0x0046ed30: +0xc / +0x10 snapped, +0x14 / +0x18 as they are, size 1 - trunc((max - min) * -0.1)).
    pub fn new(min: Vec2, max: Vec2) -> Self {
        let min = Vec2::new(snap_ten(min.x), snap_ten(min.y));
        let size = IVec2::new(1 - ((max.x - min.x) * -INV_CELL) as i32, 1 - ((max.y - min.y) * -INV_CELL) as i32).max(IVec2::ONE);
        NavGrid { min, max, size, bits: vec![0; (size.x * size.y) as usize], footprints: HashMap::new() }
    }

    /// PathGrid_Build 0x0046ed30: the grid of the landscape (game space triangles, their surface names by triangle
    /// range). A surface whose name has `nonpath_` / `_nonpath` blocks every cell it covers for good; one with
    /// `path_` / `_path` keeps its cells open whatever else covers them; any other triangle steeper than 50 degrees
    /// (its normal's z below 0.64278, including the ones facing down) blocks its cells unless a path surface opened
    /// them. (The original marks in the triangles' order with a scratch grid of those two overrides; the outcome
    /// does not depend on the order.)
    pub fn from_landscape(min: Vec2, max: Vec2, vertices: &[Vec3], tris: &[[u32; 3]], surfaces: &[(std::ops::Range<u32>, String)]) -> Self {
        let mut grid = NavGrid::new(min, max);
        let n = (grid.size.x * grid.size.y) as usize;
        let (mut steep, mut forced, mut opened) = (vec![false; n], vec![false; n], vec![false; n]);
        let mut names = vec![""; tris.len()];
        for (range, name) in surfaces {
            for t in range.start as usize..(range.end as usize).min(tris.len()) {
                names[t] = name;
            }
        }
        for (t, name) in tris.iter().zip(names) {
            let tri = t.map(|i| vertices[i as usize]);
            let target = if name.contains("nonpath_") || name.contains("_nonpath") {
                &mut forced
            } else if name.contains("path_") || name.contains("_path") {
                &mut opened
            } else {
                // (a degenerate triangle has no unit normal: 0.99 <= |n| <= 1.001, 0x005bccf8 / 0x005bcd10)
                let nrm = normal(tri);
                if !(0.99..=1.001).contains(&nrm.length()) || nrm.z >= STEEP_Z {
                    continue;
                }
                &mut steep
            };
            for c in grid.raster(tri) {
                target[(c.y * grid.size.x + c.x) as usize] = true;
            }
        }
        for i in 0..n {
            if forced[i] || (steep[i] && !opened[i]) {
                grid.bits[i] |= Mark::Static as u8;
            }
        }
        grid
    }

    /// The cells a triangle overlaps (PathGrid_RasterizeTriangle 0x0046ea00, Grid_RasterTriangle 0x00455000): the
    /// cells of its vertices' box; one cell is taken as it is, otherwise each cell whose square clips the triangle to
    /// more than two points. Cells outside the grid are left out.
    fn raster(&self, tri: [Vec3; 3]) -> Vec<IVec2> {
        let cell_of = |v: Vec3| IVec2::new(((v.x - self.min.x) * INV_CELL) as i32, ((v.y - self.min.y) * INV_CELL) as i32);
        let cs = tri.map(cell_of);
        let lo = cs[0].min(cs[1]).min(cs[2]);
        let hi = cs[0].max(cs[1]).max(cs[2]);
        if lo == hi {
            return if self.inside(lo) { vec![lo] } else { Vec::new() };
        }
        let mut out = Vec::new();
        for x in lo.x.max(0)..=hi.x.min(self.size.x - 1) {
            for y in lo.y.max(0)..=hi.y.min(self.size.y - 1) {
                let corner = self.corner(IVec2::new(x, y));
                let mut p = clip(&tri, 0, 1.0, corner.x);
                p = clip(&p, 0, -1.0, corner.x + CELL);
                p = clip(&p, 1, 1.0, corner.y);
                p = clip(&p, 1, -1.0, corner.y + CELL);
                if p.len() > 2 {
                    out.push(IVec2::new(x, y));
                }
            }
        }
        out
    }

    fn inside(&self, c: IVec2) -> bool {
        c.x >= 0 && c.y >= 0 && c.x < self.size.x && c.y < self.size.y
    }

    /// Grid_CellToWorld 0x0046f3a0: the cell's corner.
    fn corner(&self, c: IVec2) -> Vec2 {
        Vec2::new(c.x as f32 * CELL + self.min.x, c.y as f32 * CELL + self.min.y)
    }

    /// The cell's centre (Grid_FindPath's path points: cell * 10 + min + 5).
    pub fn centre(&self, c: IVec2) -> Vec2 {
        self.corner(c) + Vec2::splat(CELL * 0.5)
    }

    /// The cell of a point by truncation, unchecked (Grid_FindPathWorld 0x0046fc60).
    fn cell_unchecked(&self, p: Vec2) -> IVec2 {
        IVec2::new(((p.x - self.min.x) * INV_CELL) as i32, ((p.y - self.min.y) * INV_CELL) as i32)
    }

    /// Grid_WorldToCell 0x0046f320: the cell of a point strictly inside the grid's bounds (x in (min, max]).
    pub fn cell(&self, p: Vec2) -> Option<IVec2> {
        (p.x > self.min.x && p.x <= self.max.x && p.y > self.min.y && p.y <= self.max.y).then(|| self.cell_unchecked(p))
    }

    /// Grid_IsFree 0x0046f2e0: inside the grid and neither bit set.
    pub fn is_free(&self, c: IVec2) -> bool {
        self.inside(c) && self.bits[(c.y * self.size.x + c.x) as usize] == 0
    }

    /// Grid_SetCell 0x0046ece0: sets or clears a mark's bit of a cell inside the grid.
    pub fn set(&mut self, c: IVec2, mark: Mark, on: bool) {
        if self.inside(c) {
            let b = &mut self.bits[(c.y * self.size.x + c.x) as usize];
            if on {
                *b |= mark as u8;
            } else {
                *b &= !(mark as u8);
            }
        }
    }

    /// Object_MarkGrid 0x004552f0: an object's footprint, the cells under the triangles of its collision model that
    /// reach below 10 units in the model's own height (`vertices` in model space, game axes; `place` the object's
    /// game matrix). Marked with `mark` and kept to unmark when the object goes, unless `permanent`.
    pub fn mark_mesh(&mut self, owner: Entity, vertices: &[Vec3], tris: &[[u32; 3]], place: Mat4, mark: Mark, permanent: bool) {
        let mut cells: Vec<IVec2> = Vec::new();
        for t in tris {
            let tri = t.map(|i| vertices[i as usize]);
            if tri.iter().map(|v| v.z).fold(f32::INFINITY, f32::min) >= CELL {
                continue;
            }
            // (Grid_RasterTriangle ignores the vertices outside the grid for its box)
            cells.extend(self.raster(tri.map(|v| place.transform_point3(v))));
        }
        cells.sort_by_key(|c| (c.x, c.y));
        cells.dedup();
        for &c in &cells {
            self.set(c, mark, true);
        }
        self.footprints.insert(owner, Footprint { cells, mark, permanent });
    }

    /// DefaultGameObject_MarkGrid 0x004509a0 for an object with a body (0x004531e0): only the cell under it, and only
    /// when that cell is free.
    pub fn mark_cell(&mut self, owner: Entity, at: Vec2, mark: Mark) {
        let Some(c) = self.cell(at).filter(|c| self.is_free(*c)) else { return };
        self.set(c, mark, true);
        self.footprints.insert(owner, Footprint { cells: vec![c], mark, permanent: false });
    }

    /// vt+0xfc(0, ..): the object's footprint cleared (its death, Object_Kill; the cells' bits go whoever else marked
    /// them, as in the original).
    pub fn unmark(&mut self, owner: Entity) {
        if let Some(Footprint { cells, mark, permanent: false }) = self.footprints.remove(&owner) {
            for c in cells {
                self.set(c, mark, false);
            }
        }
    }

    /// Grid_Step 0x004704f0: the neighbour in a direction, and whether it is blocked (outside or not free).
    fn step(&self, c: IVec2, dir: u8) -> (IVec2, bool) {
        let n = step(c, dir);
        (n, !self.is_free(n))
    }

    /// Grid_LineClear 0x00470310: the cells from one point's to the other's by Bresenham's steps (x and y one at a
    /// time): 1 when all are free; 2 when the first blocked one is the destination's cell, which then moves to the
    /// cell before it (its centre); 0 when blocked before.
    fn line_clear(&self, from: &PathPoint, to: &mut PathPoint) -> u8 {
        let mut cur = from.cell;
        let (mut dx, mut dy) = (to.cell.x - cur.x, to.cell.y - cur.y);
        let (mut xdir, mut ydir) = (1, 4);
        if dy < 0 {
            dy = -dy;
            ydir = 3;
        }
        if dx < 0 {
            dx = -dx;
            xdir = 2;
        }
        // (major axis, minor axis, steps along the major one)
        let (major, minor, n, d_major, d_minor) = if dx > dy { (xdir, ydir, dx, dx, dy) } else { (ydir, xdir, dy, dy, dx) };
        let (inc_straight, inc_diag) = (2 * d_minor, 2 * d_minor - 2 * d_major);
        let mut err = 2 * d_minor - d_major;
        let mut blocked = None;
        'walk: for _ in 0..n {
            let (next, b) = self.step(cur, major);
            if b {
                blocked = Some(next);
                break 'walk;
            }
            cur = next;
            if err >= 0 {
                let (next, b) = self.step(cur, minor);
                if b {
                    blocked = Some(next);
                    break 'walk;
                }
                cur = next;
                err += inc_diag;
            } else {
                err += inc_straight;
            }
        }
        match blocked {
            None => 1,
            Some(b) if b == to.cell => {
                to.cell = cur;
                if cur.x >= 0 && cur.y >= 0 && cur.x < self.size.x && cur.y < self.size.y {
                    to.pos = self.corner(cur);
                }
                to.pos += Vec2::splat(CELL * 0.5);
                2
            }
            Some(_) => 0,
        }
    }

    /// Path_Smooth 0x00470100: point i - 1 goes when the line from point i - 2 to point i is clear, the scan going on
    /// from i + 1 without testing the shifted point again; passes repeat until one removes nothing or two points are
    /// left.
    fn smooth(&self, pts: &mut Vec<PathPoint>) {
        loop {
            let mut changed = false;
            if pts.len() <= 2 {
                break;
            }
            let mut i = 2;
            while i < pts.len() {
                let from = pts[i - 2];
                if self.line_clear(&from, &mut pts[i]) != 0 {
                    pts.remove(i - 1);
                    changed = true;
                }
                i += 1;
            }
            if !changed {
                break;
            }
        }
    }

    /// Grid_FindPathWorld 0x0046fc60 / Grid_FindPath 0x0046fd20: up to `max` points from `start` toward `goal`. A
    /// clear straight line gives the goal alone (moved to the cell before its own when that one is blocked). Otherwise
    /// the A* (4-neighbour steps, never straight back; step cost 1; the straight cell distance as the heuristic; at
    /// most `capacity` nodes): a blocked neighbour that is the goal's cell ends it at the current node's parent; when
    /// the heap runs dry the node nearest the goal is taken (result 2). The node chain's cell centres (at most 400)
    /// are smoothed, the start dropped, and a last point in the goal's cell takes the goal's own position. None when
    /// no point is left.
    pub fn find_path(&self, start: Vec2, goal: Vec2, max: usize, capacity: usize) -> Option<Found> {
        let from = PathPoint { pos: start, cell: self.cell_unchecked(start) };
        let mut to = PathPoint { pos: goal, cell: self.cell_unchecked(goal) };
        let direct = self.line_clear(&from, &mut to);
        if direct != 0 {
            return Some(Found { points: vec![to.pos], result: direct });
        }
        let mut s = Search::new(capacity.max(1));
        let h = heuristic(from.cell, to.cell);
        s.add(Node { cell: from.cell, dir: 0, f: h, g: 0.0, h, parent: None, open: false });
        let goal_key = key(to.cell);
        // (a closed node can go back on the heap, AStar_Visit: the pops are bounded as a guard only)
        let mut pops = 0usize;
        let mut end = None;
        'search: while let Some(n) = s.pop() {
            pops += 1;
            if key(s.nodes[n].cell) == goal_key {
                end = Some(n);
                break;
            }
            if pops > capacity.saturating_mul(64) {
                break;
            }
            let (cell, g) = (s.nodes[n].cell, s.nodes[n].g);
            for &dir in onward(s.nodes[n].dir) {
                let (next, blocked) = self.step(cell, dir);
                if !blocked {
                    s.visit(dir, next, g + 1.0, n, to.cell);
                } else if key(next) == goal_key {
                    end = s.nodes[n].parent;
                    break 'search;
                }
            }
        }
        let (end, partial) = match end {
            Some(e) => (e, false),
            None => (s.closest()?, true),
        };
        let mut chain = vec![end];
        while let Some(p) = s.nodes[*chain.last().unwrap()].parent {
            chain.push(p);
        }
        chain.reverse();
        chain.truncate(MAX_PATH_POINTS);
        let mut pts: Vec<PathPoint> = chain.iter().map(|&i| PathPoint { pos: self.centre(s.nodes[i].cell), cell: s.nodes[i].cell }).collect();
        self.smooth(&mut pts);
        if pts.len() < 2 {
            return None;
        }
        let mut out: Vec<PathPoint> = pts[1..].iter().take(max).copied().collect();
        if let Some(last) = out.last_mut().filter(|l| l.cell == to.cell) {
            last.pos = to.pos;
        }
        Some(Found { points: out.into_iter().map(|p| p.pos).collect(), result: if partial { 2 } else { 1 } })
    }

    /// AIRoute_Build 0x0046f6d0 (no start node, not direct) with its plan (AIRoute_Update 0x00470aa0 ->
    /// AIRoute_Plan 0x0046fa80): one point found becomes the target itself; several go in front of the target, which
    /// stays to be planned when it is next. None when no path is found (the route cleared).
    pub fn plan(&self, start: Vec2, target: Vec2, capacity: usize) -> Option<Route> {
        let found = self.find_path(start, target, PLAN_POINTS, capacity)?;
        if found.points.len() < 2 {
            return Some(Route { points: found.points, target_unplanned: false });
        }
        let mut points = found.points;
        points.push(target);
        Some(Route { points, target_unplanned: true })
    }

    /// A plan with some places kept out of the search (0x00483d60: Roman's and the partner's cells set with mode 2
    /// when free, released after).
    pub fn plan_avoiding(&mut self, start: Vec2, target: Vec2, avoid: &[Vec2], capacity: usize) -> Option<Route> {
        let mut held = Vec::new();
        for &a in avoid {
            if let Some(c) = self.cell(a).filter(|c| self.is_free(*c)) {
                self.set(c, Mark::Dynamic, true);
                held.push(c);
            }
        }
        let route = self.plan(start, target, capacity);
        for c in held {
            self.set(c, Mark::Dynamic, false);
        }
        route
    }

    /// Debug: the cells round a point (game x / y) as text, rows of +y first: '.' free, '1' / '2' / '3' the bits set,
    /// '@' the point's own cell.
    pub fn ascii(&self, at: Vec2, radius: i32) -> String {
        let c = self.cell_unchecked(at);
        let mut out = format!("nav grid round {at} (cell {c}, rows +y first, x to the right)\n");
        for y in (c.y - radius..=c.y + radius).rev() {
            for x in c.x - radius..=c.x + radius {
                let p = IVec2::new(x, y);
                out.push(match (p == c, self.inside(p).then(|| self.bits[(y * self.size.x + x) as usize])) {
                    (true, _) => '@',
                    (_, None) => '#',
                    (_, Some(0)) => '.',
                    (_, Some(b)) => char::from(b'0' + b),
                });
            }
            out.push('\n');
        }
        out
    }

    /// The grid's size in cells and how many are blocked (for the level's log).
    pub fn stats(&self) -> (IVec2, usize) {
        (self.size, self.bits.iter().filter(|b| **b != 0).count())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A 100 x 100 open grid from (0, 0) with a wall of blocked cells at x cell 5, y cells 0..8.
    fn walled() -> NavGrid {
        let mut g = NavGrid::new(Vec2::new(0.5, 0.5), Vec2::new(100.0, 100.0));
        for y in 0..8 {
            g.set(IVec2::new(5, y), Mark::Static, true);
        }
        g
    }

    #[test]
    fn snap_ten_rounds_down() {
        assert_eq!(snap_ten(-3713.41), -3720.41);
        assert_eq!(snap_ten(-224.55), -230.55);
        assert_eq!(snap_ten(17.5), 10.5);
        assert_eq!(snap_ten(-3710.0), -3720.0);
    }

    #[test]
    fn straight_line_gives_the_goal() {
        let g = walled();
        let f = g.find_path(Vec2::new(5.0, 95.0), Vec2::new(95.0, 95.0), 5, ORIGINAL_NODES).unwrap();
        assert_eq!(f, Found { points: vec![Vec2::new(95.0, 95.0)], result: 1 });
    }

    #[test]
    fn a_blocked_goal_cell_moves_the_goal_to_the_cell_before() {
        let g = walled();
        let f = g.find_path(Vec2::new(5.0, 5.0), Vec2::new(55.0, 5.0), 5, ORIGINAL_NODES).unwrap();
        assert_eq!(f.result, 2);
        assert_eq!(f.points, vec![g.centre(IVec2::new(4, 0))]);
    }

    #[test]
    fn the_route_goes_round_the_wall_by_cell_centres() {
        let g = walled();
        let f = g.find_path(Vec2::new(25.0, 15.0), Vec2::new(85.0, 15.0), 5, ORIGINAL_NODES).unwrap();
        assert_eq!(f.result, 1);
        assert_eq!(*f.points.last().unwrap(), Vec2::new(85.0, 15.0));
        // every leg is a clear grid line
        let mut from = PathPoint { pos: Vec2::new(25.0, 15.0), cell: g.cell_unchecked(Vec2::new(25.0, 15.0)) };
        for p in &f.points {
            let mut to = PathPoint { pos: *p, cell: g.cell_unchecked(*p) };
            assert_eq!(g.line_clear(&from, &mut to), 1, "{from:?} -> {p}");
            from = to;
        }
        // and it passes the wall's end (y cell 8)
        assert!(f.points.iter().any(|p| g.cell_unchecked(*p).y >= 8));
    }

    #[test]
    fn a_closed_off_goal_ends_nearest() {
        let mut g = walled();
        for y in 8..10 {
            g.set(IVec2::new(5, y), Mark::Static, true);
        }
        let f = g.find_path(Vec2::new(25.0, 15.0), Vec2::new(85.0, 15.0), 5, ORIGINAL_NODES).unwrap();
        assert_eq!(f.result, 2);
        assert_eq!(g.cell_unchecked(*f.points.last().unwrap()).x, 4);
    }

    #[test]
    fn steep_and_path_surfaces() {
        // a wall (vertical) on the line x = 50 and a ramp named *_path_* over its middle
        let v = [
            Vec3::new(50.0, 1.0, 0.0),
            Vec3::new(50.0, 99.0, 0.0),
            Vec3::new(50.0, 1.0, 20.0),
            Vec3::new(45.0, 40.0, 0.0),
            Vec3::new(56.0, 40.0, 0.0),
            Vec3::new(45.0, 60.0, 5.0),
        ];
        let tris = [[0, 1, 2], [3, 4, 5]];
        let g = NavGrid::from_landscape(Vec2::new(0.5, 0.5), Vec2::new(100.0, 100.0), &v, &tris, &[(1..2, "Wood_path_planks".into())]);
        assert!(!g.is_free(IVec2::new(4, 0)) || !g.is_free(IVec2::new(5, 0)));
        assert!(g.is_free(IVec2::new(4, 4)) && g.is_free(IVec2::new(5, 4)));
        let forced = NavGrid::from_landscape(Vec2::new(0.5, 0.5), Vec2::new(100.0, 100.0), &v, &tris, &[(1..2, "Stone_nonpath_cliffs2".into())]);
        assert!(!forced.is_free(IVec2::new(4, 4)));
    }

    #[test]
    fn avoided_places_are_released() {
        let mut g = walled();
        let r = g.plan_avoiding(Vec2::new(25.0, 95.0), Vec2::new(85.0, 95.0), &[Vec2::new(55.0, 95.0)], ORIGINAL_NODES).unwrap();
        assert!(r.points.len() >= 2 && r.target_unplanned);
        assert!(g.is_free(IVec2::new(5, 9)));
    }
}
