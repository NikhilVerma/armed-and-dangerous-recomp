//! The turret bosses: SaucerObject (behaviour saucer, class 86, vtable 0x005a5e98), ZeppelinObject (zeppelin, class
//! 85, vtable 0x005af500: the mini_zeppelin) and ColossusObject (colossus, class 71, vtable 0x005aa238). Generic AI
//! characters (Ai_InitGeneric 0x004247f0) with a weapon object on their "ref_turret" node; src/ai/ai_04.cpp:2420-3100,
//! ai_03.cpp:2523-2645 (the turret gunner), ai_06.cpp:2700-2745 (the boss states), ai_07.cpp:672 (the update).
//!
//! - **Init** (0x00435420 with TurretBossInit / ColossusObject_Slot6 0x00436390): target_tag_1..10 (the target list,
//!   resolved by 0x00435770) and target_tag_priority (the priority target: the first object with that tag), stop_and_fire
//!   (default 1) and stop_volley_count (3); fire_range_min / max (200 / 350) give the range +0x5d8 (a random value
//!   between; the colossus 400); the shot delay fire_delay_min / max (0.25 / 0.5), the volley delay fire_burst_delay_min /
//!   max (moved to +0x594 / +0x598) and the shots per volley fire_num_bursts_min / max; the weapon at "ref_turret":
//!   saucer_missile_launcher (attachment type 0x14), mini_zeppelin_turret (0x15), colossus_turret (0x16) (game_01.cpp
//!   weapon table), placed with the boss's matrix. The colossus also reads bullet_scale (0.5; p_colossus 0.08).
//! - **Targeting** (0x00435b50, every update outside the death states): every 1..2 s (1 + rand / 32768) a target out of
//!   range (vt+0x180 0x0043ecf0: alive and within the range in the plane) is dropped; a priority target without a line
//!   of fire from ref_hit_00 (0x00426fd0) too; then the priority target when acceptable, in the weapon's 180-degree
//!   view and in the line of fire, else the nearest acceptable target of the list in sight (AI_FindTarget 0x00426920
//!   on the boss's list +0x604 = the target list). A hit from the other side makes its source the target unless the
//!   priority target is held (SaucerObject_Slot52 0x00435fa0).
//! - **Firing** (the turret gunner 0x0042ecf0 with Q112Boss_Slot102 0x00435940): the weapon turns toward the target's
//!   ref_hit_00 by a slerp of min(4 dt, 1) a frame (the loop "wpn_colossus_turret_lp" while it turns); the fire timer
//!   +0x494 run out, the volley: the volley delay +0x5c8 counts down, then the shot delay +0x5cc, then a shot (vt+0x194
//!   0x004284e0: the aim error at the target's ref_hit_00, the weapon's projectile from its emitters in turn, 0x004a2010)
//!   until the volley's count is spent: a new volley delay, a new count, and (outside the walk / stand states 0x10f /
//!   0x110) a new fire timer random(+0x594, +0x598).
//! - **Moving**: the saucers and zeppelins fly their generic_txt IC anim once triggered (mover.rs); the colossus
//!   (mode marker_boss, state 0x10e) walks once triggered or once it has a target: to a random marker of its tag range
//!   (0x0042a660: sequence 0..marker_sequence_max, not one of the last two), on arrival the next (state 0x10f); after
//!   stop_volley_count volleys at a target it stands and turns to face it (state 0x110, 0x0042df70 at +0x860 = 360 degrees
//!   a second) until the target is gone. The remaster walks straight at move_speed (the original's grid route 0x00470aa0
//!   is not ported: a colossus has open ground between its markers on lev_1_m3 and lev_5_m4).
//! - **Damage and death**: the health is the property set's max_health; bad guys' shots do nothing (same side);
//!   bullet-flagged records (damages_buildings off) are scaled by bullet_scale (Q112Boss_Slot52 0x00436600, the
//!   colossus). Death (Q112Boss_Slot53 0x00436010 / AIVoice_PlayZeppelin 0x00436260): send_trigger_on_death, the score,
//!   fx_death (fx_zeppelin_crash drops the wreck, fx_exp_saucer, fx_explosion_colossus) and the husk (colossus_d).

use super::mover::{Mover, MoverNote};
use super::nodes::Nodes;
use super::rockets::{self, Launch};
use crate::fx::{DClass, Destructible, Died, Src};
use crate::level::{InstanceId, Placed};
use bevy::prelude::*;
use std::collections::HashMap;

pub struct BossPlugin;

impl Plugin for BossPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (setup, activate, targeting, fire, walk, notes, deaths).chain().after(crate::fx::FxSet).run_if(crate::playing));
    }
}

/// The scale a bullet's damage takes on this object (Q112Boss_Slot52 0x00436600: records with 0x400, i.e. projectiles
/// without damages_buildings, times "bullet_scale").
#[derive(Component, Clone, Copy)]
pub struct BulletScale(pub f32);

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BossKind {
    Saucer,
    Zeppelin,
    Colossus,
}

impl BossKind {
    pub fn of(class: &str) -> Option<BossKind> {
        match class {
            "saucer" => Some(BossKind::Saucer),
            "zeppelin" => Some(BossKind::Zeppelin),
            "colossus" => Some(BossKind::Colossus),
            _ => None,
        }
    }
    /// The weapon of the attachment type (0x14 / 0x15 / 0x16, the weapon table game_01.cpp:604-606).
    pub fn weapon(self) -> &'static str {
        match self {
            BossKind::Saucer => "saucer_missile_launcher",
            BossKind::Zeppelin => "mini_zeppelin_turret",
            BossKind::Colossus => "colossus_turret",
        }
    }
    fn name(self) -> &'static str {
        match self {
            BossKind::Saucer => "saucer",
            BossKind::Zeppelin => "zeppelin",
            BossKind::Colossus => "colossus",
        }
    }
}

/// What a boss is doing (its AI state +0x460).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Phase {
    /// 0x8c: waiting for its trigger (trigger_ic) / 0x10e (marker_boss)
    Waiting,
    /// 0x64: flying its IC anim
    Flying,
    /// 0x65 and on: the IC anim over, holding where it ended
    Holding,
    /// 0x10f: walking its markers
    Walking,
    /// 0x110: standing, facing its target
    Standing,
}

/// The weapon object on the boss's ref_turret (the attachment +0x61c; weapon behaviour "aiweapon").
pub struct Weapon {
    pub entity: Entity,
    pub projectile: String,
    pub emitters: Vec<String>,
    pub muzzle_fx: Option<String>,
    pub nodes: Nodes,
    /// flag 0x20 (alternating emitters, 0x004a2010)
    pub flip: bool,
    /// the turret's world facing (the slerp's state)
    pub facing: Option<Quat>,
}

/// A turret boss (see the module doc).
#[derive(Component)]
pub struct Boss {
    pub kind: BossKind,
    pub id: u32,
    pub phase: Phase,
    /// receive_trigger_activate +0x52c, trigger_los (+0x488 0x40), the IC anim generic_txt
    pub activate: i64,
    pub trigger_los: bool,
    pub ic_anim: Option<String>,
    /// target_tag_1..10 and target_tag_priority
    pub target_tags: Vec<i64>,
    pub priority_tag: i64,
    pub target: Option<Entity>,
    /// +0xb8e: may pick a new target
    pub may_retarget: bool,
    /// +0x490
    pub retarget: f32,
    /// +0x5d8
    pub range: f32,
    pub stop_and_fire: bool,
    pub stop_volleys: u32,
    /// +0x494 fire timer, +0x5c8 volley delay, +0x5cc shot delay, +0x5b8 shots left, +0x5c0 volleys
    pub fire_wait: f32,
    pub clip: f32,
    pub burst: f32,
    pub left: i64,
    pub volleys: u32,
    pub shot_delay: (f32, f32),
    pub clip_delay: (f32, f32),
    pub counts: (i64, i64),
    /// the aim error (0x004284e0): fire_inaccuracy_angle +0x5e8, fire_inaccuracy_height +0x5e4, fire_accuracy_min /
    /// max +0x5ec / +0x5f0
    pub inaccuracy: (f32, f32),
    pub accuracy: (f32, f32),
    pub weapon: Option<Weapon>,
    /// marker_boss: the tag range, the sequence range and the last two picks (+0x558.. +0x56c), the marker walked to
    pub marker_tags: (i64, i64),
    pub marker_seq: (i64, i64),
    pub last_markers: (i64, i64),
    pub goal: Option<Vec3>,
    pub speed: f32,
    pub turn: f32,
    /// the health last frame (a drop is a hit)
    pub last_health: f32,
    pub turning: bool,
    pub shots: u32,
}

/// The weapon attachment's marker.
#[derive(Component)]
pub struct BossWeapon(pub Entity);

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn setup(
    mut commands: Commands,
    data: Res<crate::gamedata::GameData>,
    q: Query<(Entity, &InstanceId, &Placed, &Transform, Option<&crate::skin::Animator>), Without<Boss>>,
    mut done: crate::levelreset::PerLevel<Option<std::collections::HashSet<Entity>>>,
    mut props: crate::levelreset::PerLevel<Option<HashMap<u32, crate::gamedata::Props>>>,
    (mut lib, anim_lib, assets, mut meshes, mut materials): (ResMut<crate::scene::Library>, Res<crate::skin::AnimLibrary>, Res<AssetServer>, ResMut<Assets<Mesh>>, ResMut<Assets<StandardMaterial>>),
    mut rng: ResMut<super::VehicleRng>,
) {
    let done = done.get().get_or_insert_with(Default::default);
    let placed = props.get().get_or_insert_with(|| super::placements(&anim_lib.dir, &data));
    for (e, id, pl, t, anim) in &q {
        if !done.insert(e) {
            continue;
        }
        let Some(p) = placed.get(&id.0) else { continue };
        let Some(kind) = BossKind::of(super::class_of(p, &pl.template)) else { continue };
        let model = data.templates.get(&pl.template).and_then(|t| t.model.clone()).unwrap_or_default();
        let own = Nodes::of_model(&anim_lib.dir, &model, anim.map(|a| &*a.skeleton));
        // the weapon at ref_turret, with the boss's own facing (PlaceTurret)
        let weapon = data.templates.get(kind.weapon()).cloned().and_then(|wt| {
            let wp = data.template_props(kind.weapon());
            let at = own.point("ref_turret").unwrap_or(Vec3::ZERO);
            let w = commands.spawn((Transform::from_translation(at), Visibility::default(), Name::new(kind.weapon()), BossWeapon(e))).id();
            commands.entity(e).add_child(w);
            if let Some(m) = wt.model.as_ref().and_then(|m| lib.load_model(m, false, &assets, &mut meshes, &mut materials)) {
                for (mesh, mat) in m.parts.clone() {
                    let c = commands.spawn((Mesh3d(mesh), MeshMaterial3d(mat))).id();
                    commands.entity(w).add_child(c);
                }
            }
            let wn = wt.default_anim.as_deref().and_then(|a| Nodes::from_anim(&anim_lib.dir, a)).unwrap_or_default();
            let emitters = ["emitter_bone_name", "emitter_bone_name_2"].iter().filter_map(|k| wp.s(k)).filter(|s| !s.is_empty()).map(str::to_owned).collect();
            Some(Weapon { entity: w, projectile: wp.s("projectile_name").unwrap_or("").to_owned(), emitters, muzzle_fx: wp.s("muzzle_fx_name").map(str::to_owned), nodes: wn, flip: false, facing: None })
        });
        // 0x00435420: the range, the delays and the first count
        let (rmin, rmax) = (p.f("fire_range_min", 200.0), p.f("fire_range_max", 350.0));
        let range = if kind == BossKind::Colossus { 400.0 } else { rng.range(rmin, rmax) };
        let clip_delay = (p.f("fire_burst_delay_min", 0.0), p.f("fire_burst_delay_max", 0.0));
        let counts = (p.i("fire_num_bursts_min", 1), p.i("fire_num_bursts_max", 1));
        let mode = p.s("mode").unwrap_or("attention_roaming");
        let health = super::unit_health(p, DClass::Building, pl.husk.clone());
        let mut boss = Boss {
            kind,
            id: id.0,
            phase: Phase::Waiting,
            activate: p.i("receive_trigger_activate", 0),
            trigger_los: p.i("trigger_los", 1) != 0,
            ic_anim: (mode == "trigger_ic").then(|| p.s("generic_txt").filter(|g| !g.is_empty() && *g != "none").map(str::to_owned)).flatten(),
            target_tags: (1..=10).map(|i| p.i(&format!("target_tag_{i}"), 0)).filter(|t| *t != 0).collect(),
            priority_tag: p.i("target_tag_priority", 0),
            target: None,
            may_retarget: true,
            retarget: 0.0,
            range,
            stop_and_fire: p.i("stop_and_fire", 1) != 0,
            stop_volleys: p.i("stop_volley_count", 3).clamp(0, 255) as u32,
            fire_wait: 0.0,
            clip: rng.range(clip_delay.0, clip_delay.1),
            burst: 0.0,
            left: rng.count(counts.0, counts.1),
            volleys: 0,
            shot_delay: (p.f("fire_delay_min", 0.25), p.f("fire_delay_max", 0.5)),
            clip_delay,
            counts,
            inaccuracy: (p.f("fire_inaccuracy_angle", 15.0), p.f("fire_inaccuracy_height", 10.0)),
            accuracy: (p.f("fire_accuracy_min", 50.0), p.f("fire_accuracy_max", 150.0)),
            weapon,
            marker_tags: (p.i("marker_tag_min", 0), p.i("marker_tag_max", 0)),
            marker_seq: (0, p.i("marker_sequence_max", 0)),
            // +0x568 / +0x56c start one past the last sequence (Ai_InitGeneric marker_boss)
            last_markers: (p.i("marker_sequence_max", 0) + 1, p.i("marker_sequence_max", 0) + 1),
            goal: None,
            speed: p.f("move_speed", 46.0),
            turn: 360f32.to_radians(),
            last_health: health.health,
            turning: false,
            shots: 0,
        };
        if kind == BossKind::Colossus {
            boss.phase = Phase::Waiting;
        }
        let mut ec = commands.entity(e);
        ec.insert((boss, health, BulletScale(if kind == BossKind::Colossus { p.f("bullet_scale", 0.5) } else { 1.0 })));
        if let Some(c) = pl.collider {
            ec.insert(super::MovingCollider::new(c, t.to_matrix()));
        }
        // fx_origin_start_lock (the propeller, the saucer's loop)
        if let Some(fx) = p.s("fx_origin_start_lock").filter(|s| !s.is_empty() && *s != "null" && *s != "none") {
            let at = p.s("fx_origin_start_lock_parent").filter(|s| *s != "null").and_then(|n| own.point(n)).unwrap_or(Vec3::ZERO);
            ec.insert(super::StartLockFx { name: fx.to_owned(), at });
        }
        info!("boss {} {}: range {range:.0}, weapon {:?}", kind.name(), id.0, kind.weapon());
    }
}

/// The trigger that starts a waiting boss (the handler 0x0042f870 -> 0x0042f5c0): a trigger_ic boss plays its IC anim
/// (vt+0x238 with state 0x65 after), a marker_boss sets off to a marker (state 0x10f).
fn activate(mut commands: Commands, mut triggers: MessageReader<crate::mission::SendTrigger>, mut q: Query<(Entity, &mut Boss, &Transform)>, mut rng: ResMut<super::VehicleRng>, markers: Option<Res<crate::alarm::Markers>>) {
    let sent: Vec<i64> = triggers.read().map(|t| t.0).filter(|t| *t != 0).collect();
    for (e, mut b, t) in &mut q {
        if b.phase != Phase::Waiting {
            continue;
        }
        // trigger_los: a target in range also starts it (state 0x8c, ai_06.cpp:1658: AI_FindTarget then 0x0042f5c0)
        let by_sight = b.trigger_los && b.target.is_some();
        if !(sent.contains(&b.activate) || by_sight) {
            continue;
        }
        start(&mut commands, e, &mut b, t, &mut rng, markers.as_deref());
    }
}

fn start(commands: &mut Commands, e: Entity, b: &mut Boss, t: &Transform, rng: &mut super::VehicleRng, markers: Option<&crate::alarm::Markers>) {
    if b.kind == BossKind::Colossus {
        b.phase = Phase::Walking;
        b.goal = markers.and_then(|m| next_marker(b, rng, m));
    } else if let Some(anim) = b.ic_anim.clone() {
        commands.entity(e).insert(Mover::new(&anim, t.to_matrix()));
        b.phase = Phase::Flying;
    }
}

/// 0x0042a660: a random tag of [+0x558, +0x55c] and a random sequence of [+0x560, +0x564] that is not one of the last
/// two picks (+0x568, +0x56c), the history shifted.
fn next_marker(b: &mut Boss, rng: &mut super::VehicleRng, m: &crate::alarm::Markers) -> Option<Vec3> {
    let tag = rng.count(b.marker_tags.0, b.marker_tags.1);
    let (lo, hi) = b.marker_seq;
    let (l0, l1) = b.last_markers;
    let mut i;
    if l0 <= hi {
        if l1 < hi {
            i = rng.count(lo, hi - 2);
            if i >= l0 {
                i += 1;
            }
            if i >= l1 {
                i += 1;
            }
        } else {
            i = rng.count(lo, hi - 1);
            if i >= l0 {
                i += 1;
            }
        }
    } else {
        i = rng.count(lo, hi);
    }
    b.last_markers = (i, l0);
    m.spline_marker(tag, i.max(0) as usize).map(|mk| mk.pos)
}

/// A target's aim point: its ref_hit_00 (0x00428280), else its bounds' centre.
fn hit_point(e: Entity, globals: &Query<&GlobalTransform>, anims: &Query<&crate::skin::Animator>, bounds: &Query<&crate::fx::Bounds>) -> Option<Vec3> {
    let gt = globals.get(e).ok()?;
    if let Ok(a) = anims.get(e) {
        if let Some(j) = a.skeleton.index.get("ref_hit_00").and_then(|&i| a.joints.get(i)).and_then(|&j| globals.get(j).ok()) {
            return Some(j.translation());
        }
    }
    Some(bounds.get(e).map_or(gt.translation(), |b| gt.transform_point(b.center)))
}

/// The targeting update 0x00435b50 (see the module doc), and the reaction to a hit (0x00435fa0).
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn targeting(
    time: Res<Time>,
    data: Res<crate::gamedata::GameData>,
    anim_lib: Res<crate::skin::AnimLibrary>,
    (physics, blind): (NonSend<crate::physics::Physics>, Res<super::SightBlind>),
    mut q: Query<(Entity, &mut Boss, &GlobalTransform, Option<&Destructible>, Option<&Placed>)>,
    ids: Query<(Entity, &InstanceId, Option<&Placed>)>,
    alive: Query<(), Or<(With<Destructible>, With<crate::fx::Health>)>>,
    healths: Query<&crate::fx::Health>,
    roman: Query<Entity, With<crate::player::Roman>>,
    (globals, anims, bounds): (Query<&GlobalTransform>, Query<&crate::skin::Animator>, Query<&crate::fx::Bounds>),
    mut tags: crate::levelreset::PerLevel<Option<HashMap<i64, Vec<u32>>>>,
    mut rng: ResMut<super::VehicleRng>,
) {
    let dt = time.delta_secs();
    // the objects by tag, in creation order (0x004592e0 walks World +0xe70 from the first made)
    let tags = tags.get().get_or_insert_with(|| {
        let mut m: HashMap<i64, Vec<u32>> = HashMap::new();
        let mut list: Vec<(u32, i64)> = super::placements(&anim_lib.dir, &data).into_iter().map(|(id, p)| (id, p.i("tag", 0))).filter(|(_, t)| *t != 0).collect();
        list.sort();
        for (id, t) in list {
            m.entry(t).or_default().push(id);
        }
        m
    });
    let by_id: HashMap<u32, (Entity, Option<oxijolt::prelude::BodyId>)> = ids.iter().map(|(e, i, p)| (i.0, (e, p.and_then(|p| p.collider)))).collect();
    let roman = roman.single().ok();
    let is_alive = |e: Entity| alive.contains(e) && healths.get(e).map_or(true, |h| h.0 > 0.0);
    for (_, mut b, gt, d, pl) in &mut q {
        let Some(d) = d else { continue };
        let pos = gt.translation();
        let own = pl.and_then(|p| p.collider);
        let range = b.range;
        // in range in the plane (0x0043ecf0)
        let acceptable = |e: Entity| is_alive(e) && globals.get(e).is_ok_and(|g| (g.translation() - pos).with_y(0.0).length() <= range);
        // the line of fire from the boss (0x00426fd0 from its ref_hit_00: here its origin)
        let eye = pos;
        let sight = |e: Entity| {
            let Some(p) = hit_point(e, &globals, &anims, &bounds) else { return false };
            let target_body = ids.get(e).ok().and_then(|(_, _, pl)| pl.and_then(|p| p.collider));
            let skip: Vec<_> = own.into_iter().chain(target_body).collect();
            super::sight(&physics, &blind, eye + Vec3::Y * 5.0, p, &skip)
        };
        let priority = tags.get(&b.priority_tag).and_then(|ids| ids.first()).and_then(|id| by_id.get(id)).map(|(e, _)| *e);
        // a hit from the other side: its source becomes the target unless the priority target is held (0x00435fa0;
        // the source here: Roman, who deals the damage the bosses take in the remaster's levels)
        if d.health < b.last_health && b.target != priority && b.target != roman {
            if let Some(r) = roman.filter(|r| acceptable(*r)) {
                b.target = Some(r);
                b.may_retarget = false;
            }
        }
        b.last_health = d.health;
        b.retarget -= dt;
        if b.retarget > 0.0 {
            continue;
        }
        if b.clip <= 0.0 && b.target.is_some() {
            // a volley under way: no retarget
            continue;
        }
        if b.may_retarget || b.phase == Phase::Standing || b.phase == Phase::Waiting && b.kind == BossKind::Colossus {
            if let Some(t) = b.target {
                if !acceptable(t) || (Some(t) == priority && !sight(t)) {
                    b.may_retarget = true;
                    b.volleys = 0;
                    b.left = rng.count(b.counts.0, b.counts.1);
                    b.target = None;
                }
            }
            if priority.is_none() || b.target != priority {
                let pick = priority.filter(|p| acceptable(*p) && sight(*p)).or_else(|| {
                    // AI_FindTarget on the target list: the nearest acceptable one in sight (2D)
                    b.target_tags
                        .iter()
                        .filter_map(|t| tags.get(t))
                        .flatten()
                        .filter_map(|id| by_id.get(id).map(|(e, _)| *e))
                        .filter(|e| acceptable(*e) && sight(*e))
                        .min_by(|a, c| {
                            let da = globals.get(*a).map_or(f32::MAX, |g| (g.translation() - pos).with_y(0.0).length_squared());
                            let dc = globals.get(*c).map_or(f32::MAX, |g| (g.translation() - pos).with_y(0.0).length_squared());
                            da.total_cmp(&dc)
                        })
                });
                if let Some(p) = pick {
                    b.may_retarget = false;
                    b.target = Some(p);
                }
            }
        } else {
            b.may_retarget = true;
        }
        b.retarget = 1.0 + rng.unit();
    }
}

/// The turret gunner update 0x0042ecf0 with the volley Q112Boss_Slot102 0x00435940 and the shot 0x004284e0.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn fire(
    mut commands: Commands,
    time: Res<Time>,
    data: Res<crate::gamedata::GameData>,
    mut q: Query<(Entity, &mut Boss, &GlobalTransform)>,
    mut weapons: Query<(&mut Transform, &GlobalTransform), With<BossWeapon>>,
    (globals, anims, bounds): (Query<&GlobalTransform, Without<BossWeapon>>, Query<&crate::skin::Animator>, Query<&crate::fx::Bounds>),
    (mut fx, mut sounds, mut stop): (MessageWriter<crate::fx::StartFx>, MessageWriter<crate::audio::PlaySound>, MessageWriter<crate::audio::StopSoundsOn>),
    (mut rng, mut log): (ResMut<super::VehicleRng>, ResMut<super::VehicleLog>),
    gq: Query<&GlobalTransform>,
) {
    let dt = time.delta_secs();
    for (e, mut b, gt) in &mut q {
        let b = &mut *b;
        let target_point = b.target.and_then(|t| {
            let g = globals.get(t).ok()?;
            let joint = anims.get(t).ok().and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| a.joints.get(i)).and_then(|&j| gq.get(j).ok()).map(|j| j.translation()));
            Some(joint.unwrap_or_else(|| bounds.get(t).map_or(g.translation(), |bd| g.transform_point(bd.center))))
        });
        // the weapon turns toward the target (or the boss's own facing) by a slerp of min(4 dt, 1)
        let mut muzzle_world: Option<Mat4> = None;
        if let Some(w) = b.weapon.as_mut() {
            if let Ok((mut wt, wgt)) = weapons.get_mut(w.entity) {
                let here = wgt.translation();
                let want_dir = target_point.map(|p| p - here).filter(|d| d.length_squared() > 1e-4).unwrap_or_else(|| gt.to_matrix().transform_vector3(Vec3::NEG_Y));
                let want = rockets::facing(want_dir);
                let current = w.facing.unwrap_or_else(|| wgt.to_scale_rotation_translation().1);
                let next = current.slerp(want, (dt * 4.0).min(1.0)).normalize();
                // the loop sound while it turns (the old facing against the new one below 0.99999)
                let turning = (current * Vec3::Y).dot(next * Vec3::Y) < 0.99999 && b.target.is_some();
                if turning && !b.turning {
                    sounds.write(crate::audio::PlaySound::on("wpn_colossus_turret_lp", w.entity).looping(true));
                } else if !turning && b.turning {
                    stop.write(crate::audio::StopSoundsOn(w.entity));
                }
                b.turning = turning;
                w.facing = Some(next);
                // local = the parent's rotation undone (the boss's)
                let (_, prot, _) = gt.to_scale_rotation_translation();
                wt.rotation = prot.inverse() * next;
                let wm = Mat4::from_rotation_translation(next, here);
                let emitter = w.emitters.get(w.flip as usize).or(w.emitters.first()).cloned();
                muzzle_world = Some(emitter.and_then(|n| w.nodes.get(&n)).map_or(wm, |m| wm * m));
            }
        }
        // the fire timer +0x494
        if b.fire_wait > 0.0 {
            b.fire_wait -= dt;
            if b.fire_wait <= 0.0 {
                // 0x00427600: the counters rolled again (+0x484 bit 2 cleared)
                b.left = rng.count(b.counts.0, b.counts.1);
            }
        } else if b.target.is_some() {
            let (finished, shot) = volley(b, &mut rng, dt);
            if shot {
                if let (Some(tp), Some(m)) = (target_point, muzzle_world) {
                    let from = m.w_axis.truncate();
                    let aim = aim_error(b, &mut rng, from, tp);
                    if let Some(w) = b.weapon.as_mut() {
                        rockets::launch(&mut commands, &mut fx, &data, &mut rng, Launch { template: &w.projectile, from, to: aim, along: None, target: b.target.map(|t| (t, Some("ref_hit_00"))), owner: Some(e), by: Src::Badguy });
                        w.flip = !w.flip;
                        if let Some(name) = w.muzzle_fx.clone() {
                            let (_, rot, _) = m.to_scale_rotation_translation();
                            fx.write(crate::fx::StartFx { name, at: from, rotation: rot, follow: None, position_only: false });
                        }
                    }
                    b.shots += 1;
                }
            }
            if finished && !matches!(b.phase, Phase::Walking | Phase::Standing) {
                b.fire_wait = rng.range(b.clip_delay.0, b.clip_delay.1);
            }
        }
        log.0.insert(
            b.id.to_string(),
            serde_json::json!({
                "kind": b.kind.name(),
                "phase": format!("{:?}", b.phase).to_lowercase(),
                "target": b.target.is_some(),
                "shots": b.shots,
                "volleys": b.volleys,
                "pos": super::game_pos(gt.translation()),
            }),
        );
    }
}

/// Q112Boss_Slot102 0x00435940 for a boss without the landing mode (+0x228 false): (the volley finished, a shot now).
pub fn volley(b: &mut Boss, rng: &mut super::VehicleRng, dt: f32) -> (bool, bool) {
    if b.target.is_none() {
        return (true, false);
    }
    if b.clip > 0.0 {
        b.clip -= dt;
        return (false, false);
    }
    if b.burst > 0.0 {
        b.burst -= dt;
        return (false, false);
    }
    b.left -= 1;
    if b.left > 0 {
        b.burst = rng.range(b.shot_delay.0, b.shot_delay.1);
        return (false, true);
    }
    b.clip = rng.range(b.clip_delay.0, b.clip_delay.1);
    b.volleys += 1;
    b.left = rng.count(b.counts.0, b.counts.1);
    (true, true)
}

/// The aim error of 0x004284e0 at a target point: sideways in [-fire_inaccuracy_angle, +angle], up in [0,
/// fire_inaccuracy_height], both scaled by s from the accuracy range (d <= min: 0.1; beyond max: 1; between:
/// max(0.1, (d^2 - min^2) / (max^2 - min^2))), d the distance in the plane.
pub fn aim_error(b: &Boss, rng: &mut super::VehicleRng, from: Vec3, hit: Vec3) -> Vec3 {
    let to = (hit - from).with_y(0.0);
    let d = to.length();
    let (lo, hi) = b.accuracy;
    let s = if d <= lo { 0.1 } else if d >= hi { 1.0 } else { ((d * d - lo * lo) / (hi * hi - lo * lo)).max(0.1) };
    let side = Vec3::new(-to.z, 0.0, to.x).normalize_or_zero();
    hit + side * rng.range(-b.inaccuracy.0, b.inaccuracy.0) * s + Vec3::Y * rng.range(0.0, b.inaccuracy.1) * s
}

/// The colossus's walk (states 0x10e / 0x10f / 0x110) and the stop after stop_volley_count volleys (0x00435b50's tail).
#[allow(clippy::type_complexity)]
fn walk(
    time: Res<Time>,
    physics: NonSend<crate::physics::Physics>,
    mut q: Query<(&mut Boss, &mut Transform, Option<&mut crate::skin::Animator>, Option<&Placed>), Without<Mover>>,
    globals: Query<&GlobalTransform>,
    markers: Option<Res<crate::alarm::Markers>>,
    mut rng: ResMut<super::VehicleRng>,
) {
    let dt = time.delta_secs();
    for (mut b, mut t, anim, pl) in &mut q {
        if b.kind != BossKind::Colossus {
            continue;
        }
        // a target sets a waiting colossus off (state 0x10e)
        if b.phase == Phase::Waiting && b.target.is_some() {
            b.phase = Phase::Walking;
            b.goal = markers.as_deref().and_then(|m| next_marker(&mut b, &mut rng, m));
        }
        // the stop (stop_and_fire, or stop_volley_count volleys at the target): stand, face it
        if b.phase == Phase::Walking && b.target.is_some() && (b.stop_and_fire || (b.stop_volleys != 0 && b.volleys >= b.stop_volleys)) {
            b.phase = Phase::Standing;
        }
        if b.phase == Phase::Standing && b.target.is_none() {
            b.phase = Phase::Walking;
        }
        let pos = t.translation;
        let face = match b.phase {
            Phase::Walking => b.goal,
            Phase::Standing => b.target.and_then(|e| globals.get(e).ok()).map(|g| g.translation()),
            _ => None,
        };
        if let Some(f) = face {
            // the model faces -Y: its yaw toward the point, turned at +0x860 a second (0x0042df70)
            let d = (f - pos).with_y(0.0);
            if d.length_squared() > 1e-4 {
                let want = Transform::IDENTITY.looking_to(d.normalize(), Vec3::Y).rotation * crate::level::game_to_engine() * Quat::from_rotation_z(std::f32::consts::PI);
                let angle = t.rotation.angle_between(want);
                let k = if angle > 1e-4 { (b.turn * dt / angle).min(1.0) } else { 1.0 };
                t.rotation = t.rotation.slerp(want, k).normalize();
            }
        }
        if b.phase == Phase::Walking {
            if let Some(g) = b.goal {
                let d = (g - pos).with_y(0.0);
                if d.length() < 15.0 {
                    // arrived: the next marker (state 0x10f, +0x228 false)
                    b.goal = markers.as_deref().and_then(|m| next_marker(&mut b, &mut rng, m));
                } else {
                    let step = d.normalize() * b.speed * dt;
                    let mut next = pos + step;
                    // on the ground (force_ground; the body's contact in the original)
                    let skip: Vec<_> = pl.and_then(|p| p.collider).into_iter().collect();
                    if let Some(y) = ground(&physics, next, &skip) {
                        next.y = y;
                    }
                    t.translation = next;
                }
            }
        }
        if let Some(mut a) = anim {
            let name = if b.phase == Phase::Walking && b.goal.is_some() { "colossus_walk" } else { "colossus_stand" };
            a.play(0, name, true, crate::skin::Mask::All);
        }
    }
}

/// The ground under a point (a ray down from 60 above, past the walker's own collision).
fn ground(physics: &crate::physics::Physics, at: Vec3, skip: &[oxijolt::prelude::BodyId]) -> Option<f32> {
    let (mut from, mut left) = (at + Vec3::Y * 60.0, 200.0);
    for _ in 0..8 {
        let (b, hit, _) = physics.raycast(from, Vec3::NEG_Y, left)?;
        if !skip.contains(&b) {
            return Some(hit.y);
        }
        left -= from.y - hit.y + 0.05;
        from = hit - Vec3::Y * 0.05;
    }
    None
}

/// The notes of a boss's IC anim and of its body's anims (GenericAIObject_HandleEvent 0x0043d180 and the object's
/// own): the IC end (state 0x65), deactivate (removal), the body anims, the zeppelins' captain drops.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn notes(
    mut commands: Commands,
    mut mover_notes: MessageReader<MoverNote>,
    mut anim_notes: MessageReader<crate::skin::AnimNote>,
    mut q: Query<(&mut Boss, Option<&mut crate::skin::Animator>, Option<&Placed>, &GlobalTransform, Option<&Mover>)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
    data: Res<crate::gamedata::GameData>,
    mut log: ResMut<super::VehicleLog>,
    joints: Query<&GlobalTransform, Without<Boss>>,
) {
    let mut all: Vec<(Entity, String)> = mover_notes.read().map(|n| (n.entity, n.text.clone())).collect();
    all.extend(anim_notes.read().map(|n| (n.entity, n.text.clone())));
    for (e, text) in all {
        let Ok((b, mut anim, pl, gt, _)) = q.get_mut(e) else { continue };
        for line in text.split(['\r', '\n']).map(str::trim).filter(|s| !s.is_empty()) {
            let words: Vec<&str> = line.split_whitespace().collect();
            let cmd = words.first().map(|w| w.to_lowercase()).unwrap_or_default();
            match cmd.as_str() {
                // removal (vt+0xdc: no death, no fx, no trigger, no score)
                "deactivate" => {
                    if let Some(c) = pl.and_then(|p| p.collider) {
                        physics.remove_static(c);
                    }
                    log.0.insert(b.id.to_string(), serde_json::json!({"kind": b.kind.name(), "deactivated": true, "shots": b.shots}));
                    if let Some(w) = &b.weapon {
                        commands.entity(w.entity).despawn();
                    }
                    commands.entity(e).despawn();
                    break;
                }
                // the body's anims (anim_loop: 0x81, looped; anim_intro: the first once)
                "anim_loop" | "animloop" => {
                    if let (Some(a), Some(name)) = (anim.as_deref_mut(), words.get(1)) {
                        a.play(0, name, true, crate::skin::Mask::All);
                    }
                }
                "anim_intro" => {
                    if let (Some(a), Some(name)) = (anim.as_deref_mut(), words.get(1)) {
                        a.play(0, name, false, crate::skin::Mask::All);
                        a.restart(0);
                    }
                }
                // a captain dropped at a ref node (create_object_ic TYPE REF MOVER / create_rocket_captain_IC REF MOVER):
                // the IC rappel anims are not ported: he is made on the ground under the node
                "create_object_ic" | "create_rocket_captain_ic" => {
                    let (template, node) = if cmd == "create_object_ic" { (words.get(1).copied().unwrap_or("captain"), words.get(2)) } else { ("captain", words.get(1)) };
                    let Some(node) = node else { continue };
                    if !data.templates.contains_key(template) {
                        continue;
                    }
                    let at = anim
                        .as_deref()
                        .and_then(|a| a.skeleton.index.get(&node.to_lowercase()).and_then(|&i| a.joints.get(i)).and_then(|&j| joints.get(j).ok()).map(|j| j.translation()))
                        .unwrap_or(gt.translation());
                    let skip: Vec<_> = pl.and_then(|p| p.collider).into_iter().collect();
                    let feet = ground(&physics, at - Vec3::Y * 50.0, &skip).map_or(at, |y| at.with_y(y));
                    spawn.write(crate::level::SpawnGrunt { at: feet, template: template.to_owned(), alarm: false, barracks: None, exit: None, marker_tags: None, alert_group: 0 });
                }
                _ => {}
            }
        }
    }
    // the IC anim over without a loop: state 0x65 (hold where it ended)
    for (mut b, _, _, _, m) in &mut q {
        if b.phase == Phase::Flying && m.is_some_and(|m| m.done) {
            b.phase = Phase::Holding;
        }
    }
}

/// A dead boss: its weapon goes with it; its last log entry says dead (the fx, score, trigger and husk are the death
/// path's: fx.rs `dying`, mission.rs `deaths`, level.rs `husks`).
fn deaths(mut commands: Commands, mut died: MessageReader<Died>, q: Query<&Boss>, mut log: ResMut<super::VehicleLog>, mut stop: MessageWriter<crate::audio::StopSoundsOn>) {
    for d in died.read() {
        let Ok(b) = q.get(d.0) else { continue };
        log.0.insert(b.id.to_string(), serde_json::json!({"kind": b.kind.name(), "dead": true, "shots": b.shots, "volleys": b.volleys}));
        if let Some(w) = &b.weapon {
            stop.write(crate::audio::StopSoundsOn(w.entity));
            commands.entity(w.entity).despawn();
        }
        stop.write(crate::audio::StopSoundsOn(d.0));
        commands.entity(d.0).remove::<(Boss, Mover, super::MovingCollider, BulletScale)>();
    }
}
