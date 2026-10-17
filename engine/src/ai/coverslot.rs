//! The generic cover slots (the cover-object list World+0xed4 and its interface vt+0x138..+0x160): the trees and the
//! valid buildings a grunt hides by, beside the trenches and the mounted guns (ai.rs). docs/subsystems/grunt-ai.md.
//!
//! - Trees (behaviour tree, vtable 0x005a2ab0; load 0x00496e90 joins World+0xed4): one slot (capacity 1, +0x468), the
//!   ground kind (0x00497200: 1). Its point (0x00497210): with a target, 20 (K 0x005bcbe8) from the trunk on the side
//!   away from it (tree.xy + normalize(tree - target).xy x 20, the tree's height); without, the trunk minus its facing x
//!   its radius (radius x |scale| x sqrt(3) for a uniform scale: +0x464 x |the scale vector|). Take 0x004973b0, release
//!   0x004973c0, removal 0x00497430 (the occupant freed), type 0x004971f0 / 0x004971d0 / 0x004971e0. A valid tree
//!   failing the nav-grid test 0x00497110 leaves the list (0x004970a0): no tree sets valid.
//! - Buildings (BuildingObject / TwigletGenerator, the load 0x0040ad80 with "valid" and more than one part): their
//!   ground slots ref_ground_0N (at most 2 in use), the point the node's position and its facing -(node y)
//!   (0x0040c280); the other kinds (window 3, roof 4, exit 5, enter 6) are not taken by the search (roofs skipped,
//!   windows and exits not ground; enter needs vt+0x1f8: not traced, labelled). A destroyed one's slots go with it
//!   (0x0040cd40); its ruin leaves the list unless valid (0x0040cf40).
//!
//! The search (GenericAI_FindHidingSpot 0x0042a7b0, ai_03.cpp:298-520) is `pick`; the run to the slot (states 0x1e /
//! 0x1f, ai_06.cpp:1070-1180) is ai.rs Mind::ToSlot.

use bevy::prelude::*;

/// One object of the cover list.
#[derive(Component, Clone, Debug)]
pub struct CoverObject {
    pub kind: CoverKind,
    /// the slots' nodes (model matrices, game axes; a tree's one slot has none: its point is computed)
    pub slots: Vec<Option<Mat4>>,
    pub occupants: Vec<Option<Entity>>,
    /// +0x140: how many may be in use at once
    pub capacity: usize,
}

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum CoverKind {
    /// a tree and its radius (game units, scaled)
    Tree { radius: f32 },
    /// a building (type 2: the slot itself is the cover, no covering test)
    Building,
}

/// A cover object as the frame's search sees it (occupants written back after the grunts' loop).
pub struct SlotView {
    pub e: Entity,
    pub kind: CoverKind,
    /// world (engine space): the object's position and facing, each slot's point and direction (None: computed)
    pub at: Vec3,
    pub facing: Vec3,
    pub slots: Vec<Option<(Vec3, Vec3)>>,
    pub occupants: Vec<Option<Entity>>,
    pub capacity: usize,
    pub collider: Option<oxijolt::prelude::BodyId>,
}

impl SlotView {
    pub fn of(e: Entity, c: &CoverObject, gt: &GlobalTransform, collider: Option<oxijolt::prelude::BodyId>) -> Self {
        let m = gt.to_matrix();
        SlotView {
            e,
            kind: c.kind,
            at: gt.translation(),
            facing: gt.rotation() * crate::level::game_to_engine().inverse() * Vec3::NEG_Y,
            slots: c
                .slots
                .iter()
                .map(|n| {
                    n.map(|n| {
                        let w = m * n;
                        // -(the node's y axis), the game's facing (0x0040c280)
                        (w.w_axis.truncate(), -w.y_axis.truncate().normalize_or_zero())
                    })
                })
                .collect(),
            occupants: c.occupants.clone(),
            capacity: c.capacity,
            collider,
        }
    }

    /// The slot's point and direction (vt+0x14c): the building's node, or the tree's spot away from `target`.
    pub fn point(&self, slot: usize, target: Option<Vec3>) -> (Vec3, Vec3) {
        match (self.kind, self.slots.get(slot).copied().flatten()) {
            (CoverKind::Building, Some(p)) => p,
            (kind, _) => {
                let r = if let CoverKind::Tree { radius } = kind { radius } else { 5.0 };
                match target {
                    Some(t) => {
                        let away = (self.at - t).with_y(0.0).normalize_or(Vec3::Z);
                        (self.at + away * 20.0, -away)
                    }
                    None => (self.at - self.facing.with_y(0.0).normalize_or(Vec3::Z) * r, self.facing),
                }
            }
        }
    }

    pub fn used(&self) -> usize {
        self.occupants.iter().filter(|o| o.is_some()).count()
    }
}

/// What the search needs of the grunt.
pub struct Seeker<'a> {
    pub me: Entity,
    pub pos: Vec3,
    /// the target (or the player when there is none: +0x588 else Game+0x748), its position and hit point
    pub target: Option<(Vec3, Vec3)>,
    /// +0x918 / +0x91c: the last objects left; with +0x484 bit 4 both are skipped, else the last one a quarter of the time
    pub history: [Option<Entity>; 2],
    pub skip_last_both: bool,
    pub physics: &'a crate::physics::Physics,
}

/// The generic part of 0x0042a7b0: the free ground slot nearest the grunt (3D) within `limit2` (squared: twice
/// scan_distance, or a quarter of the gun's / trench's distance when one was found), not behind it from the target
/// (dot < -0.65 and farther than the target), and, for a tree, one the tree covers from the target (0x00426ee0: the
/// ray from the slot to the target's hit point meets the tree). Returns (object index, slot, point, direction).
pub fn pick(s: &Seeker, views: &[SlotView], mut limit2: f32, skip_last: bool) -> Option<(usize, usize, Vec3, Vec3)> {
    let mut best = None;
    let tpos = s.target.map(|t| t.0);
    for (i, v) in views.iter().enumerate() {
        if s.history[0] == Some(v.e) && (s.skip_last_both || skip_last) || (s.skip_last_both && s.history[1] == Some(v.e)) {
            continue;
        }
        if v.used() >= v.capacity {
            continue;
        }
        for slot in 0..v.slots.len() {
            if v.occupants.get(slot).copied().flatten().is_some_and(|o| o != s.me) {
                continue;
            }
            let (point, dir) = v.point(slot, tpos);
            if let Some(t) = tpos {
                let a = (s.pos - t).with_y(0.0).normalize_or_zero();
                let b = (point - s.pos).with_y(0.0).normalize_or_zero();
                if a.dot(b) < -0.65 && s.pos.distance_squared(point) > (s.pos - t).with_y(0.0).length_squared() {
                    continue;
                }
            }
            let d2 = s.pos.distance_squared(point);
            if d2 >= limit2 {
                continue;
            }
            if let (CoverKind::Tree { .. }, Some((_, thit))) = (v.kind, s.target) {
                // 0x00426ee0: from the slot's point toward the target's hit point, the first object met is the tree
                let from = point + Vec3::Y * 5.0;
                let to = thit - from;
                let hit = s.physics.raycast(from, to.normalize_or(Vec3::Y), to.length());
                if !hit.is_some_and(|h| Some(h.0) == v.collider) {
                    continue;
                }
            }
            limit2 = d2;
            best = Some((i, slot, point, dir));
        }
    }
    best
}

pub struct CoverSlotPlugin;

impl Plugin for CoverSlotPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (setup, gone, log).run_if(crate::playing));
    }
}

/// The cover list's objects, once each: the trees (radius x |scale|) and the valid buildings' ground slots.
fn setup(
    mut commands: Commands,
    q: Query<(Entity, &crate::level::InstanceId, &crate::level::Placed, Option<&crate::fx::Destructible>), Without<CoverObject>>,
    physics: NonSend<crate::physics::Physics>,
    data: Res<crate::gamedata::GameData>,
    lib: Res<crate::skin::AnimLibrary>,
    launch: Res<crate::Launch>,
    mut done: crate::levelreset::PerLevel<(Option<crate::scene::SceneFile>, std::collections::HashSet<Entity>)>,
) {
    let (scene, seen) = done.get();
    if q.iter().all(|(e, ..)| seen.contains(&e)) {
        return;
    }
    let scene = scene.get_or_insert_with(|| crate::scene::read_scene(&launch.scene));
    for (e, id, placed, d) in &q {
        if !seen.insert(e) || d.is_some_and(|d| d.health <= 0.0) {
            continue;
        }
        let Some(inst) = scene.instances.iter().find(|i| i.id == id.0) else { continue };
        let p = data.instance_props(&inst.template, &inst.props);
        match p.s("behavior").unwrap_or("") {
            "tree" => {
                // +0x464 radius (5) x |the placement's scale vector| (Object_GetScale 0x004535b0)
                let m = crate::scene::game_matrix(&inst.transform);
                let scale = Vec3::new(m.x_axis.truncate().length(), m.y_axis.truncate().length(), m.z_axis.truncate().length());
                let radius = p.f("radius", 5.0) * scale.length();
                // a valid tree (its instance's "valid": 30 of lev_1_m1's, one on 1_m1_b and 1_m3) stays in the list only
                // when the nav grid is free round it (0x004970a0 -> 0x00497110: the ring of cells round the trunk's
                // cell); here the ring of the engine's nav cells: 8 knee-high rays from the trunk, 9 out (nav.rs
                // STEP 6 x 1.5), none meeting another static object, and ground under each end (labelled approximation
                // of the original's grid of rasterised objects)
                if p.f("valid", 0.0) != 0.0 && !ring_free(&physics, crate::level::game_to_engine() * m.w_axis.truncate(), placed.collider) {
                    continue;
                }
                commands.entity(e).insert(CoverObject { kind: CoverKind::Tree { radius }, slots: vec![None], occupants: vec![None], capacity: 1 });
            }
            "building" if p.f("valid", 0.0) != 0.0 => {
                let anim = p.s("ref_anim_name").map(str::to_owned).or_else(|| data.templates.get(&inst.template).and_then(|t| t.default_anim.clone())).unwrap_or_else(|| inst.model.clone());
                let mut slots = Vec::new();
                while let Some(n) = crate::level::ref_node(&lib.dir, &anim, &format!("ref_ground_{:02}", slots.len())) {
                    slots.push(Some(n));
                }
                if !slots.is_empty() {
                    let capacity = slots.len().min(2);
                    let occupants = vec![None; slots.len()];
                    commands.entity(e).insert(CoverObject { kind: CoverKind::Building, slots, occupants, capacity });
                }
            }
            _ => {}
        }
    }
}

/// The ring round a tree free (see `setup`): no static object within 9 of the trunk at knee height in 8 directions, and
/// ground under each end.
fn ring_free(physics: &crate::physics::Physics, at: Vec3, own: Option<oxijolt::prelude::BodyId>) -> bool {
    (0..8).all(|i| {
        let a = i as f32 * std::f32::consts::FRAC_PI_4;
        let dir = Vec3::new(a.sin(), 0.0, a.cos());
        let from = at + Vec3::Y * 3.0;
        let blocked = physics.raycast(from, dir, 9.0).is_some_and(|h| Some(h.0) != own);
        let ground = physics.raycast(from + dir * 9.0 + Vec3::Y * 10.0, Vec3::NEG_Y, 40.0).is_some();
        !blocked && ground
    })
}

/// A destroyed cover object leaves the list with its occupants freed (0x0040cd40 / 0x00497430); its ruin is a new object.
/// An occupant dead or gone frees his slot too (the object's release, vt+0x15c, from his death 0x00431720 / removal).
fn gone(
    mut commands: Commands,
    mut died: MessageReader<crate::fx::Died>,
    mut q: Query<(Entity, &mut CoverObject)>,
    mut grunts: Query<&mut super::Grunt>,
) {
    for (_, mut c) in &mut q {
        if c.occupants.iter().any(|o| o.is_some_and(|o| grunts.get(o).map_or(true, |g| g.mind == super::Mind::Dead))) {
            for o in c.occupants.iter_mut() {
                if o.is_some_and(|e| grunts.get(e).map_or(true, |g| g.mind == super::Mind::Dead)) {
                    *o = None;
                }
            }
        }
    }
    let q = q.transmute_lens::<Entity>();
    let q = q.query();
    for d in died.read() {
        if q.get(d.0).is_ok() {
            commands.entity(d.0).remove::<CoverObject>();
            for mut g in &mut grunts {
                if g.slot.is_some_and(|s| s.obj == d.0) {
                    g.slot = None;
                }
            }
        }
    }
}

/// The cover object's occupants after the grunts' loop.
pub fn write_back<F: bevy::ecs::query::QueryFilter>(views: &[SlotView], q: &mut Query<(Entity, &mut CoverObject, &GlobalTransform, Option<&crate::level::Placed>), F>) {
    for v in views {
        if let Ok((_, mut c, ..)) = q.get_mut(v.e) {
            if c.occupants != v.occupants {
                c.occupants = v.occupants.clone();
            }
        }
    }
}

/// A grunt's hold of a slot (+0x904 the object, +0x914 the slot, +0x908 the point) and the run's timers (+0x490 the
/// next look, +0x494 the time left before he gives up and reloads where he stands).
#[derive(Clone, Copy, Debug)]
pub struct SlotHold {
    pub obj: Entity,
    pub slot: usize,
    pub point: Vec3,
    pub dir: Vec3,
    pub look: f32,
    pub give_up: f32,
    pub arrived: bool,
}

/// Lets go of a held slot (vt+0x15c).
pub fn release(slot: &mut Option<SlotHold>, me: Entity, views: &mut [SlotView]) {
    if let Some(h) = slot.take() {
        if let Some(v) = views.iter_mut().find(|v| v.e == h.obj) {
            if v.occupants.get(h.slot).copied().flatten() == Some(me) {
                v.occupants[h.slot] = None;
            }
        }
    }
}

/// The cover list in the playtest state log: its trees and buildings, the slots held.
fn log(
    q: Query<&CoverObject>,
    grunts: Query<(Entity, &super::Grunt)>,
    state: Option<ResMut<crate::playtest::PlayState>>,
    mut seen: crate::levelreset::PerLevel<(std::collections::HashSet<Entity>, std::collections::HashSet<Entity>)>,
) {
    // the grunts that ever took a slot and that ever reached one
    let (took, reached) = seen.get();
    for (e, g) in &grunts {
        if let Some(s) = g.slot {
            took.insert(e);
            if s.arrived {
                reached.insert(e);
            }
        }
    }
    let (took, reached) = (took.len(), reached.len());
    let Some(mut state) = state else { return };
    let trees = q.iter().filter(|c| matches!(c.kind, CoverKind::Tree { .. })).count();
    let held: usize = q.iter().map(|c| c.occupants.iter().filter(|o| o.is_some()).count()).sum();
    state.0.insert("coverslots".into(), serde_json::json!({ "trees": trees, "buildings": q.iter().count() - trees, "held": held, "took": took, "reached": reached }));
}
