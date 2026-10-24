//! The turret bosses: the saucer (behaviour saucer, SaucerObject 0x005a5e98), the zeppelin (zeppelin, ZeppelinObject
//! 0x005af500: the mini_zeppelin) and the colossus (colossus, ColossusObject 0x005aa238). AI characters with a weapon on
//! their "ref_turret" node that pick a target, turn the weapon to it and fire volleys of missiles. Recovered code:
//! archive/src/ai/ai_04.cpp:2420-3100 (init 0x00435420, targeting 0x00435b50, volley 0x00435940, damage 0x00435fa0 /
//! 0x00436600, death 0x00436010), ai_03.cpp:2523-2645 (the turret gunner 0x0042ecf0), ai_06.cpp:2700-2745 (the
//! colossus's walk states), ai_07.cpp:672 (the update 0x00447400).
//!
//! Behaviour:
//! - **Waking.** A saucer or zeppelin (mode trigger_ic) waits until its receive_trigger_activate arrives, or with
//!   trigger_los until it has a target; then it flies its generic_txt path (mover.rs). A colossus (mode marker_boss)
//!   sets off walking on its trigger or once it has a target.
//! - **Targets** (0x00435b50). Each target tag names one object, the first made with it (Q112Boss_Slot8 0x00435770).
//!   The priority target (target_tag_priority, Roman on most levels) when it is within range in the plane, alive and
//!   in the line of fire; else the nearest such object among target_tag_1..10. Checked every 1..2 s, not during a
//!   volley nor a hit reaction; a target that leaves the range (or a priority target that leaves the line of fire) is
//!   dropped. The range: a random value in fire_range_min..max (the colossus: 400). A hit makes the attacker the
//!   target unless the priority target is held (0x00435fa0).
//! - **Firing** (0x0042ecf0, 0x00435940; gunnery.rs FireCycle). The weapon turns toward its aim point (a slerp of
//!   4 x dt a frame from its facing with the aim's pitch; "wpn_colossus_turret_lp" while it turns, target or not): the
//!   target's ref_hit_00, or for a moving target late in a volley the point led for a 450 projectile (0x004580a0);
//!   volleys of fire_num_bursts shots, fire_delay apart, fire_burst_delay before each, a rest of fire_burst_delay after
//!   each unless walking or standing; each shot the weapon's projectile from its two emitters in turn, straight along
//!   the weapon's facing (0x004284e0 without a target, 0x004a2010): no aim error, no homing.
//! - **The stop to fire** (0x00435b50's tail). With a target and stop_volley_count volleys gone at it (at once with
//!   stop_and_fire), a boss on its path stops and holds in the air until the target is gone, then flies on (the
//!   saucers: 3 volleys); the colossus stands, facing the target.
//! - **The colossus's walk** (states 0x10e / 0x10f / 0x110). To a random marker of its tag range (marker_tag_min..max,
//!   sequence 0..marker_sequence_max, not one of its last two: 0x0042a660), on to the next on arrival; it stands to
//!   fire as above. A medium or large hit plays its reaction (no targeting or firing meanwhile), then it walks.
//! - **Init.** SaucerObject_Slot6 0x00436090 / ZeppelinObject_Slot6 0x00436180 (TurretBossInit) and ColossusObject_Slot6
//!   0x00436390 over the shared 0x00435420; the target tags resolve as Q112Boss_Slot8 0x00435770 does.
//! - **Damage and death.** max_health times the difficulty's bad-guy factor (Ai_InitGeneric); bad guys' shots do
//!   nothing (same side); bullets are scaled by bullet_scale (the colossus: 0.08). Death: send_trigger_on_death, the
//!   score, fx_death (fx_zeppelin_crash drops the wreck, fx_exp_saucer, fx_explosion_colossus) and the husk
//!   (colossus_d), through the engine's death path (fx.rs, mission.rs, level.rs). Each boss counts on the Stats screen
//!   (enemies, and killed: 0x004247f0, 0x00436010).
//!
//! Debt (docs/remaster-engine.md "Vehicles"): the colossus walks straight to its markers (the original follows a route
//! on its grid, 0x00470aa0: plan, nav.rs routes); a hit's attacker is taken to be Roman (the engine's damage path does
//! not carry the source yet: plan, a source on the damage record). The dropped captains slide down their ropes and
//! parachute to the ground (ai/paratroop.rs, ai/fall.rs). Not ported yet: the colossus's death animation
//! (colossus_blow_up, ids 0x50..0x55 through 0x00427100; 0x00447400 kills it when the anim ends: plan, a hold on the
//! engine's death path, the trigger sent at its start); the Guy Fawkes turn of a boss and its turning back after
//! Game+0xb30 (10 s) in 0x0042ecf0 (weapons/traitor.rs turns grunts only). Unreachable in the shipped data: the
//! vt+0x228 mode of 0x00435940 / 0x00435b50 (false for all three classes), the scan / run_fire anims (the table
//! 0x0043c6e0 names "colussus_scan" / "colussus_run_fire", which do not exist).

use super::gunnery::{FireCycle, Span};
use super::mover::{Mover, MoverNote};
use super::nodes::Nodes;
use super::rockets::{self, Launch};
use super::{VehicleRng, VehicleTuning};
use crate::fx::{DClass, Destructible, Died, Src};
use crate::level::{InstanceId, Placed};
use bevy::prelude::*;
use oxijolt::prelude::BodyId;
use std::collections::HashMap;

pub struct BossPlugin;

impl Plugin for BossPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (setup, wake, targeting, halt, fire, walk, rigid_notes, notes, log).chain().after(crate::fx::FxSet).run_if(crate::playing))
            // before the husk swap removes the dead (level.rs husks runs after mission.rs deaths)
            .add_systems(Update, deaths.after(crate::fx::FxSet).before(crate::mission::deaths));
    }
}

/// The scale a bullet's damage takes on an object (Q112Boss_Slot52 0x00436600: projectiles without damages_buildings
/// times "bullet_scale").
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
    /// Its weapon (the attachment types 0x14 / 0x15 / 0x16 of the weapon table, game_01.cpp:604-606).
    pub fn weapon(self) -> &'static str {
        match self {
            BossKind::Saucer => "saucer_missile_launcher",
            BossKind::Zeppelin => "mini_zeppelin_turret",
            BossKind::Colossus => "colossus_turret",
        }
    }
    pub fn name(self) -> &'static str {
        match self {
            BossKind::Saucer => "saucer",
            BossKind::Zeppelin => "zeppelin",
            BossKind::Colossus => "colossus",
        }
    }
}

/// What a boss is doing.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Phase {
    /// waiting to be woken (state 0x8c; the colossus 0x10e)
    Waiting,
    /// flying its path (state 0x64)
    Flying,
    /// its path over without a loop: it stays where it ended (state 0x65)
    Holding,
    /// stopped on its path to fire, held in the air until its target is gone (state 0x66, 0x00435b50)
    Halted,
    /// walking its markers (state 0x10f)
    Walking,
    /// standing, facing its target (state 0x110)
    Standing,
}

/// How a boss wakes.
#[derive(Clone, Debug)]
pub struct Wake {
    /// receive_trigger_activate
    pub trigger: i64,
    /// trigger_los: a target also wakes it
    pub by_sight: bool,
    /// the path it flies (mode trigger_ic: generic_txt)
    pub path: Option<String>,
}

/// Whom a boss shoots at.
#[derive(Clone, Debug)]
pub struct Targeting {
    pub tags: Vec<i64>,
    pub priority_tag: i64,
    pub current: Option<Entity>,
    /// in the plane
    pub range: f32,
    may_retarget: bool,
    next_check: f32,
    last_health: f32,
}

/// The weapon on the boss's ref_turret (an "aiweapon" object).
pub struct Weapon {
    pub entity: Entity,
    pub projectile: String,
    pub emitters: Vec<String>,
    pub muzzle_fx: Option<String>,
    /// the weapon model's nodes (its emitters)
    pub nodes: Nodes,
    /// which emitter fires next
    next_emitter: usize,
    /// its facing in the world (the turn's state)
    facing: Option<Quat>,
    turning: bool,
    /// its target's velocity for the lead (0x00458000; ai/aim.rs, as the base turret keeps its own)
    motion: crate::ai::aim::TargetMotion,
}

/// When a walking boss stops to shoot.
#[derive(Clone, Copy, Debug)]
pub struct Halt {
    /// stop_and_fire
    pub at_once: bool,
    /// stop_volley_count (0: never)
    pub after_volleys: u32,
}

impl Halt {
    /// Time to stop and fire, after `volleys` volleys at the target (0x00435b50: +0xb8c, or +0xb8f reached by +0x5c0).
    pub fn reached(&self, volleys: u32) -> bool {
        self.at_once || (self.after_volleys != 0 && volleys >= self.after_volleys)
    }
}

/// The colossus's marker walk.
#[derive(Clone, Debug)]
pub struct MarkerWalk {
    pub tags: Span<i64>,
    pub sequences: Span<i64>,
    /// the last two sequences picked (0x0042a660 avoids them)
    recent: (i64, i64),
    pub goal: Option<Vec3>,
    pub speed: f32,
    /// hit_level_1 / hit_level_2 (+0x9e8 / +0x9ec) and the health last frame (a hit's size)
    hit_levels: (f32, f32),
    last_health: Option<f32>,
    /// the hit reaction playing (GenericAIObject_Slot52 0x00430340 states 0x130 / 0x131; the colossus's table
    /// 0x0043c6e0: ids 0x43 / 0x44 colossus_hit_med, 0x45..0x48 colossus_hit_large): its anim and time left; it stops
    /// meanwhile
    pub hit: Option<&'static str>,
}

/// A turret boss (see the module doc).
#[derive(Component)]
pub struct Boss {
    pub kind: BossKind,
    pub id: u32,
    pub phase: Phase,
    pub wake: Wake,
    pub targeting: Targeting,
    pub weapon: Option<Weapon>,
    pub cycle: FireCycle,
    pub halt: Halt,
    pub walk: Option<MarkerWalk>,
    pub shots: u32,
}

/// The weapon entity's marker.
#[derive(Component)]
pub struct BossWeapon;

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn setup(
    mut commands: Commands,
    data: Res<crate::gamedata::GameData>,
    q: Query<(Entity, &InstanceId, &Placed, &Transform, Option<&crate::skin::Animator>), Without<Boss>>,
    mut done: crate::levelreset::PerLevel<Option<std::collections::HashSet<Entity>>>,
    mut props: crate::levelreset::PerLevel<Option<HashMap<u32, crate::gamedata::Props>>>,
    (mut lib, anim_lib, assets, mut meshes, mut materials): (ResMut<crate::scene::Library>, Res<crate::skin::AnimLibrary>, Res<AssetServer>, ResMut<Assets<Mesh>>, ResMut<Assets<StandardMaterial>>),
    (mut rng, tuning, difficulty): (ResMut<VehicleRng>, Res<VehicleTuning>, Option<Res<crate::gamedata::Difficulty>>),
    mut stats: ResMut<crate::stats::MissionStats>,
) {
    let done = done.get().get_or_insert_with(Default::default);
    let placed = props.get().get_or_insert_with(|| super::placements(&anim_lib.dir, &data));
    let badguy_health = difficulty.map_or(1.0, |d| d.factors().badguy_health);
    for (e, id, pl, t, anim) in &q {
        if !done.insert(e) {
            continue;
        }
        let Some(p) = placed.get(&id.0) else { continue };
        let Some(kind) = BossKind::of(super::class_of(p, &pl.template)) else { continue };
        let model = data.templates.get(&pl.template).and_then(|t| t.model.clone()).unwrap_or_default();
        let own = Nodes::of_model(&anim_lib.dir, &model, anim.map(|a| &*a.skeleton));
        let weapon = spawn_weapon(&mut commands, e, kind, &own, &data, &anim_lib, (&mut lib, &assets, &mut meshes, &mut materials));
        let range = if kind == BossKind::Colossus { tuning.colossus_range } else { rng.range(p.f("fire_range_min", 200.0), p.f("fire_range_max", 350.0)) };
        let pause = |k: &str, d: f32| p.f(k, d) * tuning.fire_pause_scale;
        let cycle = FireCycle::new(
            Span { min: pause("fire_delay_min", 0.25), max: pause("fire_delay_max", 0.5) },
            Span { min: pause("fire_burst_delay_min", 0.0), max: pause("fire_burst_delay_max", 0.0) },
            Span { min: p.i("fire_num_bursts_min", 1), max: p.i("fire_num_bursts_max", 1) },
            &mut rng,
        );
        let health = super::unit_health(p, DClass::Building, pl.husk.clone(), badguy_health * tuning.health_scale);
        let walk = (p.s("mode") == Some("marker_boss")).then(|| {
            let last = p.i("marker_sequence_max", 0);
            MarkerWalk {
                tags: Span { min: p.i("marker_tag_min", 0), max: p.i("marker_tag_max", 0) },
                sequences: Span { min: 0, max: last },
                // both start one past the last sequence (Ai_InitGeneric, marker_boss)
                recent: (last + 1, last + 1),
                goal: None,
                speed: p.f("move_speed", 46.0),
                hit_levels: (p.f("hit_level_1", 10.0), p.f("hit_level_2", 25.0)),
                last_health: None,
                hit: None,
            }
        });
        let boss = Boss {
            kind,
            id: id.0,
            phase: Phase::Waiting,
            wake: Wake {
                trigger: p.i("receive_trigger_activate", 0),
                by_sight: p.i("trigger_los", 1) != 0,
                path: (p.s("mode") == Some("trigger_ic")).then(|| p.s("generic_txt").filter(|g| !g.is_empty() && *g != "none").map(str::to_owned)).flatten(),
            },
            targeting: Targeting {
                tags: (1..=10).map(|i| p.i(&format!("target_tag_{i}"), 0)).filter(|t| *t != 0).collect(),
                priority_tag: p.i("target_tag_priority", 0),
                current: None,
                range,
                may_retarget: true,
                next_check: 0.0,
                last_health: health.health,
            },
            weapon,
            cycle,
            // stop_volley_count's low byte (0x00435420: +0xb8f)
            halt: Halt { at_once: p.i("stop_and_fire", 1) != 0, after_volleys: u32::from(p.i("stop_volley_count", 3) as u8) },
            walk,
            shots: 0,
        };
        // a placed bad guy counts toward the Stats screen's enemies (Ai_InitGeneric 0x004247f0: Game+0x2710)
        stats.enemies_total += 1;
        let mut ec = commands.entity(e);
        ec.insert((boss, health, BulletScale(if kind == BossKind::Colossus { p.f("bullet_scale", 0.5) } else { 1.0 })));
        if let Some(c) = pl.collider {
            ec.insert(super::MovingCollider::new(c, t.to_matrix()));
        }
        // (fx_origin_start_lock, the zeppelin's propeller and hum, the saucer's loop: every placed object's, mod.rs)
    }
}

/// The weapon object at the boss's ref_turret, facing as the boss (TurretBossInit / PlaceTurret, ai_04.cpp).
fn spawn_weapon(
    commands: &mut Commands,
    boss: Entity,
    kind: BossKind,
    own: &Nodes,
    data: &crate::gamedata::GameData,
    anim_lib: &crate::skin::AnimLibrary,
    (lib, assets, meshes, materials): (&mut crate::scene::Library, &AssetServer, &mut Assets<Mesh>, &mut Assets<StandardMaterial>),
) -> Option<Weapon> {
    let wt = data.templates.get(kind.weapon())?;
    let wp = data.template_props(kind.weapon());
    let at = own.point("ref_turret").unwrap_or(Vec3::ZERO);
    let w = commands.spawn((Transform::from_translation(at), Visibility::default(), Name::new(kind.weapon()), BossWeapon)).id();
    commands.entity(boss).add_child(w);
    if let Some(m) = wt.model.as_ref().and_then(|m| lib.load_model(m, false, assets, meshes, materials)) {
        for (mesh, mat) in m.parts.clone() {
            let c = commands.spawn((Mesh3d(mesh), MeshMaterial3d(mat))).id();
            commands.entity(w).add_child(c);
        }
    }
    Some(Weapon {
        entity: w,
        projectile: wp.s("projectile_name").unwrap_or("").to_owned(),
        emitters: ["emitter_bone_name", "emitter_bone_name_2"].iter().filter_map(|k| wp.s(k)).filter(|s| !s.is_empty()).map(str::to_owned).collect(),
        muzzle_fx: wp.s("muzzle_fx_name").map(str::to_owned),
        nodes: wt.default_anim.as_deref().and_then(|a| Nodes::from_anim(&anim_lib.dir, a)).unwrap_or_default(),
        next_emitter: 0,
        facing: None,
        turning: false,
        motion: Default::default(),
    })
}

/// Wakes a waiting boss (the trigger handler 0x0042f870 -> 0x0042f5c0; with trigger_los a target, state 0x8c): a
/// flying boss sets off on its path, a colossus walks.
fn wake(
    mut commands: Commands,
    mut triggers: MessageReader<crate::mission::SendTrigger>,
    mut q: Query<(Entity, &mut Boss, &Transform)>,
    mut rng: ResMut<VehicleRng>,
    markers: Option<Res<crate::alarm::Markers>>,
) {
    let sent: Vec<i64> = triggers.read().map(|t| t.0).filter(|t| *t != 0).collect();
    for (e, mut b, t) in &mut q {
        let b = &mut *b;
        let woken = sent.contains(&b.wake.trigger) || (b.wake.by_sight && b.targeting.current.is_some());
        if b.phase != Phase::Waiting || !woken {
            continue;
        }
        if let Some(walk) = b.walk.as_mut() {
            b.phase = Phase::Walking;
            walk.goal = markers.as_deref().and_then(|m| next_marker(walk, &mut rng, m));
        } else if let Some(path) = &b.wake.path {
            commands.entity(e).insert(Mover::new(path, t.to_matrix()));
            b.phase = Phase::Flying;
        }
    }
}

/// 0x0042a660: a random tag of the walk's range and a random sequence that is not one of the last two picks.
fn next_marker(w: &mut MarkerWalk, rng: &mut VehicleRng, m: &crate::alarm::Markers) -> Option<Vec3> {
    let tag = w.tags.roll(rng);
    let (lo, hi) = (w.sequences.min, w.sequences.max);
    let (last, before) = w.recent;
    let mut i;
    if last <= hi {
        if before < hi {
            i = rng.count(lo, hi - 2);
            if i >= last {
                i += 1;
            }
            if i >= before {
                i += 1;
            }
        } else {
            i = rng.count(lo, hi - 1);
            if i >= last {
                i += 1;
            }
        }
    } else {
        i = rng.count(lo, hi);
    }
    w.recent = (i, last);
    m.spline_marker(tag, i.max(0) as usize).map(|mk| mk.pos)
}

/// A target's aim point: its ref_hit_00 (0x00428280), else the centre of its bounds.
fn hit_point(e: Entity, globals: &Query<&GlobalTransform>, anims: &Query<&crate::skin::Animator>, bounds: &Query<&crate::fx::Bounds>) -> Option<Vec3> {
    let gt = globals.get(e).ok()?;
    if let Ok(a) = anims.get(e) {
        if let Some(j) = a.skeleton.index.get("ref_hit_00").and_then(|&i| a.joints.get(i)).and_then(|&j| globals.get(j).ok()) {
            return Some(j.translation());
        }
    }
    Some(bounds.get(e).map_or(gt.translation(), |b| gt.transform_point(b.center)))
}

/// The objects by tag in the order they were made (0x004592e0 walks the object list from the first made).
fn objects_by_tag(dir: &std::path::Path, data: &crate::gamedata::GameData) -> HashMap<i64, Vec<u32>> {
    let mut list: Vec<(u32, i64)> = super::placements(dir, data).into_iter().map(|(id, p)| (id, p.i("tag", 0))).filter(|(_, t)| *t != 0).collect();
    list.sort();
    let mut m: HashMap<i64, Vec<u32>> = HashMap::new();
    for (id, t) in list {
        m.entry(t).or_default().push(id);
    }
    m
}

/// The targeting 0x00435b50 and the reaction to a hit 0x00435fa0 (see the module doc).
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn targeting(
    time: Res<Time>,
    (data, anim_lib, tuning): (Res<crate::gamedata::GameData>, Res<crate::skin::AnimLibrary>, Res<VehicleTuning>),
    (physics, blind): (NonSend<crate::physics::Physics>, Res<super::SightBlind>),
    mut q: Query<(&mut Boss, &GlobalTransform, &Destructible, Option<&Placed>)>,
    ids: Query<(Entity, &InstanceId, Option<&Placed>)>,
    alive: Query<(), Or<(With<Destructible>, With<crate::fx::Health>)>>,
    healths: Query<&crate::fx::Health>,
    roman: Query<Entity, With<crate::player::Roman>>,
    (globals, anims, bounds): (Query<&GlobalTransform>, Query<&crate::skin::Animator>, Query<&crate::fx::Bounds>),
    mut tags: crate::levelreset::PerLevel<Option<HashMap<i64, Vec<u32>>>>,
    mut rng: ResMut<VehicleRng>,
) {
    let dt = time.delta_secs();
    let tags = tags.get().get_or_insert_with(|| objects_by_tag(&anim_lib.dir, &data));
    let by_id: HashMap<u32, (Entity, Option<BodyId>)> = ids.iter().map(|(e, i, p)| (i.0, (e, p.and_then(|p| p.collider)))).collect();
    let roman = roman.single().ok();
    let is_alive = |e: Entity| alive.contains(e) && healths.get(e).map_or(true, |h| h.0 > 0.0);
    for (mut b, gt, health, pl) in &mut q {
        let b = &mut *b;
        let pos = gt.translation();
        let own = pl.and_then(|p| p.collider);
        let range = b.targeting.range;
        // alive and within range in the plane (0x0043ecf0)
        let acceptable = |e: Entity| is_alive(e) && globals.get(e).is_ok_and(|g| (g.translation() - pos).with_y(0.0).length() <= range);
        // the line of fire (0x00426fd0: from 5 above the boss to the target's hit point, past both)
        let in_sight = |e: Entity| {
            let Some(p) = hit_point(e, &globals, &anims, &bounds) else { return false };
            let target_body = by_id.values().find(|(te, _)| *te == e).and_then(|(_, b)| *b);
            let skip: Vec<BodyId> = own.into_iter().chain(target_body).collect();
            super::sight(&physics, &blind, pos + Vec3::Y * 5.0, p, &skip)
        };
        let own_id = b.id;
        let t = &mut b.targeting;
        // each tag names one object: the first made with it, not the boss itself (Q112Boss_Slot8 0x00435770 resolves
        // each through Object_FindByTag 0x004592e0)
        let tagged = |tag: i64| tags.get(&tag).and_then(|ids| ids.iter().find(|id| **id != own_id)).and_then(|id| by_id.get(id)).map(|(e, _)| *e);
        let priority = tagged(t.priority_tag);
        // a hit: the attacker becomes the target unless the priority target is held (SaucerObject_Slot52 0x00435fa0:
        // taken as it is, the next check drops it when out of range; debt: the attacker is taken to be Roman, see the
        // module doc)
        if health.health < t.last_health && t.current != priority {
            if let Some(r) = roman {
                t.current = Some(r);
            }
        }
        t.last_health = health.health;
        // a hit reaction plays (states 0x12c..0x132): no targeting meanwhile (Q112Boss_Update 0x00447400)
        if b.walk.as_ref().is_some_and(|w| w.hit.is_some()) {
            continue;
        }
        t.next_check -= dt;
        if t.next_check > 0.0 || (t.current.is_some() && b.cycle.mid_volley()) {
            continue;
        }
        t.next_check = tuning.retarget_every + rng.unit() * tuning.retarget_jitter;
        // (+0xb8e, or the colossus waiting in state 0x10e: vt+0x228 is false for all three classes, so the state 0x110
        // clause never applies)
        if !(t.may_retarget || (b.phase == Phase::Waiting && b.walk.is_some())) {
            // the check after a pick is skipped once (0x00435b50)
            t.may_retarget = true;
            continue;
        }
        if let Some(cur) = t.current {
            if !acceptable(cur) || (Some(cur) == priority && !in_sight(cur)) {
                t.current = None;
                t.may_retarget = true;
                b.cycle.new_target(&mut rng);
            }
        }
        if priority.is_some() && t.current == priority {
            continue;
        }
        let nearest = || {
            t.tags
                .iter()
                .filter_map(|tag| tagged(*tag))
                .filter(|e| acceptable(*e) && in_sight(*e))
                .min_by(|a, c| {
                    let d = |e: &Entity| globals.get(*e).map_or(f32::MAX, |g| (g.translation() - pos).with_y(0.0).length_squared());
                    d(a).total_cmp(&d(c))
                })
        };
        if let Some(p) = priority.filter(|p| acceptable(*p) && in_sight(*p)).or_else(nearest) {
            t.current = Some(p);
            t.may_retarget = false;
        }
    }
}

/// The stop to fire of a boss on its path (0x00435b50's tail; the colossus's own stop is `walk`'s): with a target, once
/// stop_and_fire is set or stop_volley_count volleys have gone at it, the boss stops where it is and holds there, its
/// path's clock stopped (an IC anim plays, 0x00426630: the body keyframed with no velocity, state 0x66, +0xb5c = 3);
/// without a target it goes on along its path (physics as before, state 0x64, +0xb5c = 2). The shipped saucers stop
/// after 3 volleys (p_saucer has no stop_volley_count); the zeppelins never (stop_volley_count 0).
fn halt(mut q: Query<(&mut Boss, &mut Mover)>) {
    for (mut b, mut m) in &mut q {
        let has_target = b.targeting.current.is_some();
        if b.phase == Phase::Flying && has_target && b.halt.reached(b.cycle.volleys) {
            b.phase = Phase::Halted;
            m.hold(true);
        } else if b.phase == Phase::Halted && !has_target {
            b.phase = Phase::Flying;
            m.hold(false);
        }
    }
}

/// The turret gunner 0x0042ecf0: the weapon turns to its aim point, the fire cycle runs (Q112Boss_Slot102 0x00435940),
/// each shot a missile from the next emitter along the weapon's facing (0x004284e0 without a target, 0x004a2010).
/// The aim point: the target's ref_hit_00 (0x00428280); a movable target (0x004531e0) is led (0x004580a0 from its
/// position, for a projectile of `boss_lead_speed`) once the shots left in the volley are no more than the volleys fired
/// (+0x5b8 - +0x5c0 <= 0).
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn fire(
    mut commands: Commands,
    time: Res<Time>,
    (data, tuning): (Res<crate::gamedata::GameData>, Res<VehicleTuning>),
    mut q: Query<(Entity, &mut Boss, &GlobalTransform)>,
    mut weapons: Query<(&mut Transform, &GlobalTransform), With<BossWeapon>>,
    (globals, anims, bounds): (Query<&GlobalTransform>, Query<&crate::skin::Animator>, Query<&crate::fx::Bounds>),
    movable: Query<(), Or<(With<crate::player::Roman>, With<crate::physics::Body>)>>,
    (mut fx, mut sounds, mut stop): (MessageWriter<crate::fx::StartFx>, MessageWriter<crate::audio::PlaySound>, MessageWriter<crate::audio::StopSoundsOn>),
    mut rng: ResMut<VehicleRng>,
) {
    let dt = time.delta_secs();
    for (e, mut b, gt) in &mut q {
        let b = &mut *b;
        // a hit reaction plays (states 0x12c..0x132): GenericAI_Update, and with it the gunner, does not run
        if b.walk.as_ref().is_some_and(|w| w.hit.is_some()) {
            continue;
        }
        let target = b.targeting.current;
        let leads = b.cycle.shots_left() <= i64::from(b.cycle.volleys);
        let Some(w) = b.weapon.as_mut() else { continue };
        let Ok((mut wt, wgt)) = weapons.get_mut(w.entity) else { continue };
        let here = wgt.translation();
        // the target's velocity (0x00458000: its body's; TargetMotion measures it from its positions frame to frame)
        let target_at = target.and_then(|t| globals.get(t).ok()).map(|g| g.translation());
        w.motion.observe(target.zip(target_at).into_iter().collect(), dt);
        let velocity = target.map_or(Vec3::ZERO, |t| w.motion.velocity(t));
        let aim_point = target.and_then(|t| match target_at {
            Some(p) if leads && movable.contains(t) => Some(crate::ai::aim::lead_point(p, velocity, here, tuning.boss_lead_speed)),
            _ => hit_point(t, &globals, &anims, &bounds),
        });
        // turn toward the aim point, or back to the boss's own facing: from the facing it has with the aim's pitch (the
        // facing's vertical part replaced by the aim direction's) toward the aim, by 4 x dt
        let current = w.facing.unwrap_or_else(|| wgt.to_scale_rotation_translation().1);
        let aim_dir = aim_point.map(|p| p - here).filter(|d| d.length_squared() > 1e-4);
        let (from, want_dir) = match aim_dir {
            Some(d) => {
                let d = d.normalize();
                let facing = current * Vec3::NEG_Y;
                let pitched = facing.with_y(d.y).normalize_or(facing);
                (rockets::facing(pitched), d)
            }
            None => (current, gt.to_matrix().transform_vector3(Vec3::NEG_Y)),
        };
        let next = from.slerp(rockets::facing(want_dir), (dt * tuning.weapon_turn_rate).min(1.0)).normalize();
        // the turn loop runs while the facing moves, target or not (the old facing against the new)
        let turning = (current * Vec3::NEG_Y).dot(next * Vec3::NEG_Y) < 0.99999;
        if turning != w.turning {
            if turning {
                sounds.write(crate::audio::PlaySound::on("wpn_colossus_turret_lp", w.entity).looping(true));
            } else {
                stop.write(crate::audio::StopSoundsOn(w.entity));
            }
            w.turning = turning;
        }
        w.facing = Some(next);
        let (_, boss_rot, _) = gt.to_scale_rotation_translation();
        wt.rotation = boss_rot.inverse() * next;
        if target.is_none() {
            continue;
        }
        let rests = !matches!(b.phase, Phase::Walking | Phase::Standing);
        if !b.cycle.step(dt, rests, &mut rng).shoot {
            continue;
        }
        // the shot: from the emitter along its -Y (the weapon's slot 0x4f 0x004a1e90: the emitter's position and
        // direction, aimed 1000 along it), with no target to home on and no aim error
        let weapon_world = Mat4::from_rotation_translation(next, here);
        let muzzle = w.emitters.get(w.next_emitter).and_then(|n| w.nodes.get(n)).map_or(weapon_world, |m| weapon_world * m);
        w.next_emitter = (w.next_emitter + 1) % w.emitters.len().max(1);
        let from = muzzle.w_axis.truncate();
        let along = (-muzzle.y_axis.truncate()).normalize_or(next * Vec3::NEG_Y);
        rockets::launch(&mut commands, &mut fx, &data, &mut rng, Launch { template: &w.projectile, from, to: from + along * 1000.0, along: Some(along), target: None, owner: Some(e), by: Src::Badguy });
        if let Some(name) = w.muzzle_fx.clone() {
            fx.write(crate::fx::StartFx { name, at: from, rotation: next, follow: None, position_only: false });
        }
        b.shots += 1;
    }
}

/// The colossus's walk and stand (states 0x10e / 0x10f / 0x110; the stop is 0x00435b50's tail).
#[allow(clippy::type_complexity)]
fn walk(
    time: Res<Time>,
    tuning: Res<VehicleTuning>,
    physics: NonSend<crate::physics::Physics>,
    mut q: Query<(&mut Boss, &mut Transform, Option<&mut crate::skin::Animator>, Option<&Placed>, Option<&Destructible>), Without<Mover>>,
    globals: Query<&GlobalTransform>,
    markers: Option<Res<crate::alarm::Markers>>,
    mut rng: ResMut<VehicleRng>,
    lib: Res<crate::skin::AnimLibrary>,
) {
    let dt = time.delta_secs();
    for (mut b, mut t, mut anim, pl, d) in &mut q {
        let b = &mut *b;
        let Some(w) = b.walk.as_mut() else { continue };
        // a hit's reaction (0x00430340 with the colossus's anim table): <= hit_level_1 the small reaction (its table
        // gives colossus_stand: no anim of its own), <= hit_level_2 colossus_hit_med, more colossus_hit_large; it
        // stands while it plays
        if let Some(d) = d {
            let lost = w.last_health.map_or(0.0, |h| h - d.health);
            w.last_health = Some(d.health);
            if lost > w.hit_levels.0 && d.health > 0.0 {
                let name = if lost <= w.hit_levels.1 { "colossus_hit_med" } else { "colossus_hit_large" };
                w.hit = Some(name);
                if let Some(a) = anim.as_mut() {
                    a.play(0, name, false, crate::skin::Mask::All);
                    a.restart(0);
                }
            }
        }
        if let Some(name) = w.hit {
            let playing = anim.as_ref().and_then(|a| a.layers.first()).is_some_and(|l| l.anim == name);
            if playing && !anim.as_ref().is_some_and(|a| a.finished(0, &lib)) {
                continue;
            }
            // the reaction over, it walks (0x00447400: vt+0x19c(8), state 0x10f), even from waiting; a stand to fire
            // comes back at once below
            w.hit = None;
            b.phase = Phase::Walking;
            if w.goal.is_none() {
                w.goal = markers.as_deref().and_then(|m| next_marker(w, &mut rng, m));
            }
        }
        let target = b.targeting.current;
        if b.phase == Phase::Waiting && target.is_some() {
            b.phase = Phase::Walking;
            w.goal = markers.as_deref().and_then(|m| next_marker(w, &mut rng, m));
        }
        if b.phase == Phase::Walking && target.is_some() && b.halt.reached(b.cycle.volleys) {
            b.phase = Phase::Standing;
        }
        if b.phase == Phase::Standing && target.is_none() {
            b.phase = Phase::Walking;
        }
        let pos = t.translation;
        let face = match b.phase {
            Phase::Walking => w.goal,
            Phase::Standing => target.and_then(|e| globals.get(e).ok()).map(|g| g.translation()),
            _ => None,
        };
        if let Some(f) = face {
            // the model faces -Y; turned at the boss turn speed (0x0042df70)
            let d = (f - pos).with_y(0.0);
            if d.length_squared() > 1e-4 {
                let want = rockets::facing(d);
                let angle = t.rotation.angle_between(want);
                let k = if angle > 1e-4 { (tuning.boss_turn_speed * dt / angle).min(1.0) } else { 1.0 };
                t.rotation = t.rotation.slerp(want, k).normalize();
            }
        }
        let mut walking = false;
        if let (Phase::Walking, Some(g)) = (b.phase, w.goal) {
            let d = (g - pos).with_y(0.0);
            if d.length() < tuning.marker_reached {
                w.goal = markers.as_deref().and_then(|m| next_marker(w, &mut rng, m));
            } else {
                walking = true;
                let mut next = pos + d.normalize() * w.speed * dt;
                let skip: Vec<BodyId> = pl.and_then(|p| p.collider).into_iter().collect();
                if let Some(y) = ground(&physics, next, &skip) {
                    next.y = y;
                }
                t.translation = next;
            }
        }
        if let Some(mut a) = anim {
            a.play(0, if walking { "colossus_walk" } else { "colossus_stand" }, true, crate::skin::Mask::All);
        }
    }
}

/// The ground under a point (a ray down from 60 above, past the walker's own collision).
fn ground(physics: &crate::physics::Physics, at: Vec3, skip: &[BodyId]) -> Option<f32> {
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

/// A body anim played on a unit without a rig (the mini zeppelin's model is rigid): its clock and notes, as the rig's
/// anim pass runs them (skin.rs animate), so the notes it carries still fire (mini_zeppelin_attack_village: the
/// rocket captains' drops at ref_repel_00..03); `anim` names it for its nodes' positions.
#[derive(Component)]
struct RigidAnimNotes {
    anim: String,
    notes: Vec<(f32, String)>,
    duration: f32,
    t: f32,
}

/// Advances the rigid units' anim clocks and sends the notes passed (crate::skin::AnimNote, read by `notes`).
fn rigid_notes(mut commands: Commands, time: Res<Time>, mut q: Query<(Entity, &mut RigidAnimNotes)>, mut out: MessageWriter<crate::skin::AnimNote>) {
    for (e, mut r) in &mut q {
        let from = r.t;
        r.t += time.delta_secs();
        for (_, text) in r.notes.iter().filter(|(at, _)| *at >= from && *at < r.t) {
            out.write(crate::skin::AnimNote { entity: e, text: text.clone() });
        }
        if r.t >= r.duration {
            commands.entity(e).remove::<RigidAnimNotes>();
        }
    }
}

/// The notes of a boss's path and of its body's anims (GenericAIObject_HandleEvent 0x0043d180 and the object's own):
/// deactivate (removal: no death, fx, trigger or score), the body anims, the zeppelins' captain drops; and the path's
/// end (state 0x65).
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn notes(
    mut commands: Commands,
    (mut mover_notes, mut anim_notes): (MessageReader<MoverNote>, MessageReader<crate::skin::AnimNote>),
    mut q: Query<(&mut Boss, Option<&mut crate::skin::Animator>, Option<&Placed>, &GlobalTransform, Option<&Mover>)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
    data: Res<crate::gamedata::GameData>,
    mut log: ResMut<super::VehicleLog>,
    joints: Query<&GlobalTransform, Without<Boss>>,
    (dropped, in_turret): (Query<&crate::ai::Grunt, With<crate::level::Dropped>>, Query<(), With<crate::defence::BaseTurret>>),
    (mut anim_lib, rigid): (ResMut<crate::skin::AnimLibrary>, Query<&RigidAnimNotes>),
) {
    // the drops stop while 12 dropped troops live, except on a level that starts in the base turret (0x00409b0e:
    // the list +0xf24's count against 0xc, then 0x0044e600)
    let mut alive = dropped.iter().filter(|g| g.mind != crate::ai::Mind::Dead).count();
    let all: Vec<(Entity, String)> = mover_notes.read().map(|n| (n.entity, n.text.clone())).chain(anim_notes.read().map(|n| (n.entity, n.text.clone()))).collect();
    for (e, text) in all {
        let Ok((b, mut anim, pl, gt, _)) = q.get_mut(e) else { continue };
        for line in text.split(['\r', '\n']).map(str::trim).filter(|s| !s.is_empty()) {
            let words: Vec<&str> = line.split_whitespace().collect();
            let cmd = words[0].to_lowercase();
            match cmd.as_str() {
                "deactivate" => {
                    if let Some(c) = pl.and_then(|p| p.collider) {
                        physics.remove_static(c);
                    }
                    log.0.insert(b.id.to_string(), serde_json::json!({"kind": b.kind.name(), "deactivated": true, "shots": b.shots}));
                    commands.entity(e).despawn();
                    break;
                }
                "anim_loop" | "animloop" => {
                    if let (Some(a), Some(name)) = (anim.as_deref_mut(), words.get(1)) {
                        a.play(0, name, true, crate::skin::Mask::All);
                    }
                }
                "anim_intro" => {
                    if let (Some(a), Some(name)) = (anim.as_deref_mut(), words.get(1)) {
                        a.play(0, name, false, crate::skin::Mask::All);
                        a.restart(0);
                    } else if let Some(a) = words.get(1).and_then(|n| anim_lib.anim(n)) {
                        commands.entity(e).insert(RigidAnimNotes { anim: words[1].to_lowercase(), notes: a.notes.clone(), duration: a.duration, t: 0.0 });
                    }
                }
                // create_object_ic TYPE REF MOVER / create_rocket_captain_IC REF MOVER: a captain at a ref node, down his
                // rope and his parachute (ai/paratroop.rs)
                "create_object_ic" | "create_rocket_captain_ic" => {
                    let (template, node) = if cmd == "create_object_ic" { (words.get(1).copied().unwrap_or("captain"), words.get(2)) } else { ("captain", words.get(1)) };
                    let (Some(node), true) = (node, data.templates.contains_key(template)) else { continue };
                    if alive >= 12 && in_turret.is_empty() {
                        continue;
                    }
                    alive += 1;
                    let at = anim
                        .as_deref()
                        .and_then(|a| a.skeleton.index.get(&node.to_lowercase()).and_then(|&i| a.joints.get(i)).and_then(|&j| joints.get(j).ok()).map(|j| j.translation()))
                        .or_else(|| {
                            // a rigid unit: the node in the playing anim's node table (model space)
                            let r = rigid.get(e).ok()?;
                            Nodes::from_anim(&anim_lib.dir, &r.anim)?.point(node).map(|p| gt.transform_point(p))
                        })
                        .unwrap_or(gt.translation());
                    let mover = if cmd == "create_object_ic" { words.get(3) } else { words.get(2) }.map(|m| m.to_string());
                    // at the node with a rope anim (ai/paratroop.rs: the rope, the free fall, the parachute); without
                    // one, on the ground under it
                    let skip: Vec<BodyId> = pl.and_then(|p| p.collider).into_iter().collect();
                    let feet = if mover.as_deref().is_some_and(|m| anim_lib.anim(m).is_some()) { at } else { ground(&physics, at - Vec3::Y * 50.0, &skip).map_or(at, |y| at.with_y(y)) };
                    info!("EVENT vehicles: {} drops a {template} at {node} ({})", b.id, mover.as_deref().unwrap_or("no mover"));
                    let dropped = crate::level::DroppedTroop { rocket: cmd == "create_rocket_captain_ic", mover };
                    spawn.write(crate::level::SpawnGrunt { at: feet, template: template.to_owned(), dropped: Some(dropped), ..default() });
                }
                _ => {}
            }
        }
    }
    for (mut b, _, _, _, m) in &mut q {
        if b.phase == Phase::Flying && m.is_some_and(|m| m.done) {
            b.phase = Phase::Holding;
        }
    }
}

/// The state log's entry of each live boss, and "dropped": the live troops the vehicles dropped (position, mind,
/// whether they hold a target, their priority tags).
fn log(q: Query<(&Boss, &GlobalTransform, &Destructible)>, mut log: ResMut<super::VehicleLog>, dropped: Query<(&crate::ai::Grunt, &GlobalTransform), With<crate::level::Dropped>>) {
    let troops: Vec<serde_json::Value> = dropped
        .iter()
        .filter(|(g, _)| g.mind != crate::ai::Mind::Dead)
        .map(|(g, gt)| serde_json::json!({"pos": super::game_pos(gt.translation()), "mind": format!("{:?}", g.mind), "target": g.target.is_some(), "tags": [g.priority_tags.0, g.priority_tags.1]}))
        .collect();
    if !troops.is_empty() || log.0.contains_key("dropped") {
        log.0.insert("dropped".into(), serde_json::Value::Array(troops));
    }
    for (b, gt, d) in &q {
        log.0.insert(
            b.id.to_string(),
            serde_json::json!({
                "kind": b.kind.name(),
                "phase": format!("{:?}", b.phase).to_lowercase(),
                "health": d.health,
                "target": b.targeting.current.is_some(),
                "shots": b.shots,
                "volleys": b.cycle.volleys,
                "hit": b.walk.as_ref().and_then(|w| w.hit),
                "pos": super::game_pos(gt.translation()),
            }),
        );
    }
}

/// A dead boss: its weapon goes with it (the destroy Q112Boss_Slot55 0x00435860), its log entry says so (the fx,
/// score, trigger and husk are the engine's death path: fx.rs `dying`, mission.rs `deaths`, level.rs `husks`). A
/// zeppelin's death has Roman remark on it under the shared voice delay (AIVoice_PlayZeppelin 0x00436260:
/// ROM50206 / 50208 / 50209_Zeppelin at equal odds, ((rand() x 300) >> 15) / 100).
#[allow(clippy::too_many_arguments)]
fn deaths(
    mut commands: Commands,
    mut died: MessageReader<Died>,
    q: Query<&Boss>,
    mut log: ResMut<super::VehicleLog>,
    (mut stop, mut sounds): (MessageWriter<crate::audio::StopSoundsOn>, MessageWriter<crate::audio::PlaySound>),
    (time, mut barks, mut fxrng): (Res<Time>, ResMut<crate::ai::Barks>, ResMut<crate::fx::FxStats>),
    roman: Query<Entity, With<crate::player::Roman>>,
    mut rng: ResMut<VehicleRng>,
    mut stats: ResMut<crate::stats::MissionStats>,
) {
    for d in died.read() {
        let Ok(b) = q.get(d.0) else { continue };
        // Game+0x270c, the Stats screen's enemies killed (Q112Boss_Slot53 0x00436010)
        stats.enemies_killed += 1;
        if b.kind == BossKind::Zeppelin && barks.take(crate::ai::Delay::Vo, time.elapsed_secs(), &mut fxrng) {
            let line = ["ROM50206_Zeppelin", "ROM50208_Zeppelin", "ROM50209_Zeppelin"][((rng.0.rand() * 300) >> 15).min(299) as usize / 100];
            if let Ok(r) = roman.single() {
                sounds.write(crate::audio::PlaySound::on(line, r));
            }
        }
        log.0.insert(b.id.to_string(), serde_json::json!({"kind": b.kind.name(), "dead": true, "shots": b.shots, "volleys": b.cycle.volleys}));
        if let Some(w) = &b.weapon {
            stop.write(crate::audio::StopSoundsOn(w.entity));
            commands.entity(w.entity).despawn();
        }
        stop.write(crate::audio::StopSoundsOn(d.0));
        commands.entity(d.0).remove::<(Boss, Mover, super::MovingCollider, BulletScale)>();
    }
}
