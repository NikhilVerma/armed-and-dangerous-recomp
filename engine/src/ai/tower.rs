//! The companions' tower task (MolmerObject / Q111Object, src/ai/ai_08.cpp; docs/subsystems/companions.md 8): a
//! companion set up with a trigger and a marker walks off on its own when the trigger comes, takes its place at the
//! marker and blows the doors of the object named by its obj_tag. The shipped data uses it once: lev_4_m2's Jonesy
//! (molmer 147: receive_trigger_activate 7001, tower_marker_tag 30, obj_tag -600) and "Get Jonesy (ALIVE) to the Glass
//! Tower / Guard Jonesy while he blows Tower doors". The mission sends 7001 when the tenth peasant is safe
//! (peasant_success_trigger); marker 356 (tag 30) at the tower sends 802 on his arrival: the alarm, the second
//! objective text and the 120 s countdown whose end (4000) blows the doors (bb_ear 404's fx_exp_glass_tower) and wins
//! the level 5 s later (mission.rs). His death sends 5001: failure 2, "Jonesy just died!" (ai.rs companion_damage).
//!
//! - Set-up (0x00483250): receive_trigger_activate (+0x52c) is kept only with a tower_marker_tag (+0xbc4); such a
//!   companion starts where the level placed it (0x004836b0, ai.rs `starts_in_formation`) and is the ordinary companion
//!   (modes 1..3) until the trigger.
//! - The trigger (Q111Object_Slot14 0x00483680, used up) runs 0x004834f0: the head of the marker spline of the tag
//!   (Marker_FindSpline 0x0044c030) and a route to it (0x0046f6d0; here nav.rs). With both: mode 6, flags 0x8000 (the
//!   team orders 0x00485120 skip it) and 0x40000 (no knock-away, Slot112 / Slot149), unpaired from its partner
//!   (0x00486ba0: the other one is a pair of one from then on), the held target dropped: [`OnTask`]. Without:
//!   "Molmer_Tower_Failure" as a paused message (0x00403410 flag 1), 0x00484290 (the return order's toggle) and flag
//!   0x10000: the same again 15 s later (+0xbc8, counted by 0x00485910's prologue outside modes 6 / 7).
//! - Mode 6 (0x00485d48): along the route at move_speed (the generic speeds 0x004297a0; oracle: 39.7 units/s); once
//!   it ends farther than 10 units (100 squared, K 0x005bcb60) from the marker a new one (failing that, straight on,
//!   0x0046f5b0), within 10 straight on, within 1 (K 0x005bcb2c) arrival: the marker's matrix (0x00457990 with
//!   vt+0x5c), physics stopped, health back to its maximum (+0x454 = +0x458: the oracle shows Jonesy at 405 healed to
//!   500 on the frame he arrives), Marker_ProcessArrival 0x0044ba30
//!   (the marker's trigger, Jonesy as its sender), the door object (Object_FindByTag 0x004592e0 by obj_tag over
//!   World+0xe70) taken out of the collidable / projectile / area-damage / query lists (+0xe9c / +0xe90 / +0xe94 /
//!   +0xea0), "mover_molmer_plant_bomb" as an IC anim (vt+0x238 = 0x0042c410, state 0x64), mode 7.
//! - Mode 7 (0x00485cc6): the IC anim carries him by its own root track (state 0x64: about 29 units toward the doors
//!   and from bomb to bomb; mover.rs `Mover`, as ai/ic.rs), its notes are the script; when it is over (state 0 again)
//!   "mover_molmer_bomb_loop", looping ("ic_loop" note, +0xad8). (Mode 7 hooks the bone callback 0x0042c080 into the
//!   ending plant anim's root channel before the loop starts, holding that root still while the outgoing anim fades;
//!   the loop's own root track carries him as the plant's did. Here the loop replaces the plant at once, so there is
//!   no fade to hold.)
//!   The plant anim's notes (MolmerObject_OnAnimEvent 0x0044e740): "make_bomb" makes a player_tickerbomb at a quarter
//!   size (K 0x005bcbe4) in his hand, taken off the ticker bomb lists (+0xeb0 / +0xeb4: a prop, not a bomb site),
//!   "plant_bomb Ref_bomb_0N" puts it on the door object at that bone: eight over 31.3 s; "fx jones_bomb_plant" / the
//!   loop's "fx fx_jones_bomb_adjust" are the generic notes (mover.rs).
//! - On the task (modes 6 / 7) an attacker is not taken as a target (Q111Object_Slot94 0x00486da0) and, during the IC
//!   anims, hits play no reaction (0x00430340 skips them while 0x00426630: +0xa18 bit 0); on the way none either
//!   (Q111Object_Slot129 0x00486e90: reactions only in modes 1 / 3). Both in ai.rs companion_damage.
//!
//! Not modelled: the door object leaving the AI's sight and the crosshair's ray lists (only the projectile list: its
//! body lets bullets through). Guess: the make_bomb bone (the note names none: the free hand). Debt (nav.rs, not the
//! original's nav mesh): the route's corridor can differ from the original's (lev_4_m2: south of the abbey where the
//! original goes north, the same length), and a walker stuck at a step the grid allowed routes round it (`STUCK_*`).

use super::Companion;
use crate::alarm::Markers;
use crate::mission::SendTrigger;
use crate::physics::Physics;
use bevy::prelude::*;

/// Route waypoints count as passed within this (the companions' own route following, ai.rs).
const WAYPOINT_REACHED: f32 = 4.0;
/// Mode 6: past the route's end a new route is asked for while farther than this from the marker (100 squared,
/// K 0x005bcb60), and the marker is reached within 1 (K 0x005bcb2c, squared).
const REPLAN_BEYOND: f32 = 10.0;
const ARRIVED: f32 = 1.0;
/// A failed start is tried again this long after (+0xbc8 = 15.0).
const RETRY: f32 = 15.0;
/// The original asks for a new route every frame past the route's end; here at most this often (an A* search).
const REPLAN_EVERY: f32 = 0.25;
/// nav.rs's grid can lead over a step the walker cannot climb (the original's nav mesh has no such places): a walker
/// that has not moved this far in STUCK_TIME marks the spot and takes a route round it (nav.rs `blocked`, as the
/// playtest bot does).
const STUCK_MOVE: f32 = 3.0;
const STUCK_TIME: f32 = 1.0;
/// make_bomb's player_tickerbomb is scaled by K 0x005bcbe4.
const BOMB_SCALE: f32 = 0.25;
const PLANT_ANIM: &str = "mover_molmer_plant_bomb";
const LOOP_ANIM: &str = "mover_molmer_bomb_loop";
/// The bone the bomb is made at (guess: make_bomb's note names none; MolmerObject_Load 0x0044e630 gives him the
/// weapon bones "ref_rhand" (the pistol) and "ref_lhand": the free hand).
const HAND_BONE: &str = "ref_lhand";

/// A companion's tower task, as the level set it up (0x00483250) and how far it has got.
#[derive(Component, Debug)]
pub struct TowerTask {
    /// receive_trigger_activate (+0x52c): used up when it comes
    trigger: i64,
    /// tower_marker_tag (+0xbc4): the marker spline whose head is the place
    marker_tag: i64,
    /// obj_tag (+0x534): the object whose doors are blown (lev_4_m2: the glass tower 110, tag -600)
    door_tag: i64,
    pub phase: Phase,
    /// +0xbcc 0x10000 with +0xbc8: a failed start tried again when this runs out
    retry: Option<f32>,
    /// the bomb in the hand (+0xbd0) between a make_bomb and its plant_bomb note
    held: Option<Entity>,
    /// bombs put on the door object
    pub planted: u32,
    /// max_health: restored on arrival
    max_health: f32,
}

/// Where the task is.
#[derive(Debug, Clone, PartialEq)]
pub enum Phase {
    /// waiting for the trigger (or a retry): the ordinary companion, modes 1..3
    Standby,
    /// mode 6: on the route to the marker (engine space); the spots it got stuck at, where it stood at the last
    /// progress check and the time since
    Walking { route: Vec<Vec3>, goal: Vec3, replan: f32, blocked: Vec<Vec3>, check: (Vec3, f32) },
    /// mode 7: on the marker, the plant anim
    Planting,
    /// mode 7: the bomb loop, until the level ends
    BombLoop,
}

impl TowerTask {
    /// The task of a companion with these properties (0x00483250: a trigger without a marker tag is dropped).
    pub fn from_props(p: &crate::gamedata::Props) -> Option<Self> {
        let (trigger, marker_tag) = (p.i("receive_trigger_activate", 0), p.i("tower_marker_tag", 0));
        (trigger != 0 && marker_tag != 0).then(|| TowerTask {
            trigger,
            marker_tag,
            door_tag: p.i("obj_tag", 0),
            phase: Phase::Standby,
            retry: None,
            held: None,
            planted: 0,
            max_health: p.f("max_health", 500.0),
        })
    }

    /// The update's mode +0xb5c while on the task (6 walking, 7 at the marker), None before it.
    pub fn mode(&self) -> Option<u8> {
        match self.phase {
            Phase::Standby => None,
            Phase::Walking { .. } => Some(6),
            Phase::Planting | Phase::BombLoop => Some(7),
        }
    }

    /// The IC anims play (charanim.rs leaves the companion's anims to this module).
    pub fn at_marker(&self) -> bool {
        matches!(self.phase, Phase::Planting | Phase::BombLoop)
    }

    fn phase_name(&self) -> &'static str {
        match self.phase {
            Phase::Standby if self.retry.is_some() => "retry",
            Phase::Standby => "standby",
            Phase::Walking { .. } => "walking",
            Phase::Planting => "planting",
            Phase::BombLoop => "bomb_loop",
        }
    }
}

/// On the task (+0xbcc 0x8000 | 0x40000): the companion's own update and the team orders leave it alone (ai.rs).
#[derive(Component)]
pub struct OnTask;

/// A bomb made by the task (in the hand or on the doors).
#[derive(Component)]
pub struct TowerBomb;

pub struct TowerPlugin;

impl Plugin for TowerPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (receive, walk, plant, log).chain().run_if(crate::playing));
    }
}

/// Q111Object_Slot14 0x00483680 and the retry of 0x00485910's prologue, both into 0x004834f0.
#[allow(clippy::too_many_arguments)]
fn receive(
    mut commands: Commands,
    time: Res<Time>,
    mut triggers: MessageReader<SendTrigger>,
    mut q: Query<(Entity, &mut TowerTask, &mut Companion, &Transform)>,
    markers: Option<Res<Markers>>,
    physics: NonSend<Physics>,
    mut mission: Option<ResMut<crate::mission::Mission>>,
    texts: Option<Res<crate::gamemenu::Texts>>,
) {
    let got: Vec<i64> = triggers.read().map(|t| t.0).filter(|t| *t != 0).collect();
    let dt = time.delta_secs();
    for (e, mut task, mut c, t) in &mut q {
        if c.dead.is_some() || task.phase != Phase::Standby {
            continue;
        }
        let mut start = false;
        if task.trigger != 0 && got.contains(&task.trigger) {
            task.trigger = 0;
            start = true;
        }
        if let Some(left) = task.retry.as_mut() {
            *left -= dt;
            start |= *left <= 0.0;
        }
        if !start {
            continue;
        }
        task.retry = None;
        let marker = markers.as_ref().and_then(|m| m.spline_marker(task.marker_tag, 0));
        // the route must lead to the marker (nav.rs gives the nearest reachable point otherwise)
        let route = marker.and_then(|m| crate::nav::path(&physics, t.translation, m.pos, &[]).filter(|r| r.last().is_some_and(|w| (*w - m.pos).with_y(0.0).length() <= REPLAN_BEYOND)));
        match (marker, route) {
            (Some(m), Some(route)) => {
                info!("EVENT tower: {} sets off for marker {} ({} waypoints, {:.0} units)", if c.q { "Q" } else { "Jonesy" }, m.id, route.len(), (m.pos - t.translation).length());
                if std::env::var("AIDBG").is_ok() {
                    let g: Vec<[i32; 3]> = route.iter().map(|w| { let v = crate::level::game_to_engine().inverse() * *w; [v.x as i32, v.y as i32, v.z as i32] }).collect();
                    info!("AIDBG tower route {g:?}");
                }
                task.phase = Phase::Walking { route, goal: m.pos, replan: 0.0, blocked: Vec::new(), check: (t.translation, 0.0) };
                c.drop_target();
                commands.entity(e).insert(OnTask);
            }
            _ => {
                if marker.is_none() {
                    warn!("{} can not find trigger marker path {}", if c.q { "q111" } else { "molmer" }, task.marker_tag);
                }
                info!("EVENT tower: no way to the marker, retry in {RETRY} s");
                let text = texts.as_ref().map(|t| t.get("molmer_tower_failure")).unwrap_or_else(|| "molmer_tower_failure".into());
                if let Some(m) = mission.as_deref_mut() {
                    m.message(&text, 0.0);
                }
                c.order_return();
                task.retry = Some(RETRY);
            }
        }
    }
}

/// Mode 6: the walk to the marker and the arrival.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn walk(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<Physics>,
    markers: Option<Res<Markers>>,
    mut q: Query<(Entity, &mut TowerTask, &mut Companion, &mut Transform, &mut crate::fx::Health)>,
    doors: Query<(&crate::level::InstanceId, &crate::level::Placed)>,
    mission: Option<Res<crate::mission::Mission>>,
    mut triggers: MessageWriter<SendTrigger>,
) {
    let dt = time.delta_secs();
    for (e, mut task, mut c, mut t, mut health) in &mut q {
        if c.dead.is_some() {
            continue;
        }
        let me = t.translation;
        let door_tag = task.door_tag;
        let marker_tag = task.marker_tag;
        let Phase::Walking { route, goal, replan, blocked, check } = &mut task.phase else { continue };
        let goal = *goal;
        *replan -= dt;
        check.1 += dt;
        if check.1 >= STUCK_TIME {
            if (me - check.0).with_y(0.0).length() < STUCK_MOVE && (goal - me).with_y(0.0).length() > REPLAN_BEYOND {
                blocked.push(me + (route.first().copied().unwrap_or(goal) - me).with_y(0.0).normalize_or_zero() * 6.0);
                info!("EVENT tower: stuck at {:?}, a route round it", (crate::level::game_to_engine().inverse() * me).round());
                if let Some(r) = crate::nav::path(&physics, me, goal, blocked).filter(|r| r.len() > 1) {
                    *route = r;
                }
            }
            *check = (me, 0.0);
        }
        while route.first().is_some_and(|w| (*w - me).with_y(0.0).length() < WAYPOINT_REACHED) {
            route.remove(0);
        }
        let to_goal = (goal - me).with_y(0.0);
        let mut dir = Vec3::ZERO;
        if let Some(w) = route.first() {
            dir = (*w - me).with_y(0.0).normalize_or_zero();
        } else if to_goal.length() > REPLAN_BEYOND {
            // the route ended away from the marker: a new one, else straight on
            if *replan <= 0.0 {
                *replan = REPLAN_EVERY;
                if let Some(r) = crate::nav::path(&physics, me, goal, blocked).filter(|r| r.len() > 1) {
                    *route = r;
                }
            }
            dir = route.first().map(|w| (*w - me).with_y(0.0)).unwrap_or(to_goal).normalize_or_zero();
        } else if to_goal.length() > ARRIVED {
            dir = to_goal.normalize_or_zero();
        }
        if dir != Vec3::ZERO {
            c.moving = true;
            c.yaw = dir.x.atan2(dir.z);
            t.rotation = Quat::from_rotation_y(c.yaw) * c.base;
            let speed = c.run_speed();
            c.walk(&mut physics, &mut t, dir, speed, dt);
            continue;
        }
        // arrival: on the marker's matrix, physics stopped, healed
        let Some(m) = markers.as_ref().and_then(|ms| ms.spline_marker(marker_tag, 0)) else { continue };
        let at = crate::mover::engine_transform(m.matrix);
        t.translation = at.translation;
        t.rotation = at.rotation;
        c.yaw = super::ic::yaw_of(at.rotation);
        c.moving = false;
        // the IC anims from here on (0x0042c410): physics off, so bullets pass him (player.rs; tuning item T1: the
        // original's Jonesy takes no damage while he plants)
        c.ic = true;
        physics.place_walker(&c.walker, at.translation);
        health.0 = task.max_health;
        // the plant anim, an IC anim from here
        commands.entity(e).insert(crate::mover::Mover::new(PLANT_ANIM, m.matrix));
        info!("EVENT tower: {} at marker {}: planting", if c.q { "Q" } else { "Jonesy" }, m.id);
        if m.trigger != 0 {
            triggers.write(SendTrigger(m.trigger));
        }
        // the door object leaves the projectile list: bullets pass it
        if let Some(ms) = mission.as_deref() {
            for (id, placed) in &doors {
                if door_tag != 0 && ms.has_tag(id.0, door_tag) {
                    if let Some(b) = placed.collider {
                        physics.no_bullets.insert(b);
                    }
                }
            }
        }
        task.phase = Phase::Planting;
    }
}

/// Mode 7: the IC anims (the plant, then the bomb loop) and the plant's notes.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn plant(
    mut commands: Commands,
    lib: Res<crate::skin::AnimLibrary>,
    mut notes: MessageReader<crate::mover::MoverNote>,
    mut q: Query<(Entity, &mut TowerTask, &Companion, &Transform, &mut crate::skin::Animator, Option<&mut crate::mover::Mover>)>,
    mut physics: NonSendMut<Physics>,
    bombs: Option<Res<crate::player::BombAssets>>,
    doors: Query<(&crate::level::InstanceId, &crate::level::Placed, &GlobalTransform)>,
    mission: Option<Res<crate::mission::Mission>>,
    data: Res<crate::gamedata::GameData>,
) {
    let notes: Vec<crate::mover::MoverNote> = notes.read().cloned().collect();
    for (e, mut task, c, t, mut a, mover) in &mut q {
        if !task.at_marker() {
            continue;
        }
        let Some(mut m) = mover else { continue };
        if c.dead.is_some() {
            // killed on the marker: the anim stops there (the death is ai.rs's)
            commands.entity(e).remove::<crate::mover::Mover>();
            continue;
        }
        // physics off: the body is put where the IC anim has him
        physics.place_walker(&c.walker, t.translation);
        // the IC anim on the whole skeleton at the mover's time ("anim_full", 0x0042c380), as ai/ic.rs
        if a.layers.first().is_none_or(|l| l.anim != m.anim) {
            a.play_at(0, &m.anim, false, crate::skin::Mask::All, m.t);
            a.stop(1);
        }
        if let Some(l) = a.layers.get_mut(0) {
            l.time = m.t;
            l.speed = 0.0;
        }
        for n in notes.iter().filter(|n| n.entity == e) {
            let mut words = n.text.split_whitespace();
            match words.next().unwrap_or("").to_ascii_lowercase().as_str() {
                // "ic_loop": the IC anim wraps (+0xad8)
                "ic_loop" => m.looping = true,
                // a player_tickerbomb at a quarter size in his hand (once until it is planted)
                "make_bomb" if task.held.is_none() => {
                    let Some(b) = bombs.as_deref() else { continue };
                    let bomb = spawn_bomb(&mut commands, b, Transform::from_scale(Vec3::splat(BOMB_SCALE)));
                    match a.skeleton.index.get(HAND_BONE).map(|&i| a.joints[i]) {
                        Some(hand) => {
                            commands.entity(hand).add_child(bomb);
                        }
                        None => {
                            commands.entity(e).add_child(bomb);
                        }
                    }
                    task.held = Some(bomb);
                }
                // onto the door object at the named bone; with no door object the bomb stays in his hand
                // (game_02.cpp:2840-2847: the lookup comes first)
                "plant_bomb" => {
                    let door = mission.as_deref().and_then(|ms| doors.iter().find(|(id, ..)| task.door_tag != 0 && ms.has_tag(id.0, task.door_tag)));
                    let (Some(bone), Some((_, placed, gt)), Some(b)) = (words.next(), door, bombs.as_deref()) else { continue };
                    let Some(held) = task.held.take() else { continue };
                    commands.entity(held).despawn();
                    let anim = data.templates.get(&placed.template).and_then(|t| t.default_anim.clone()).unwrap_or_else(|| placed.template.clone());
                    let Some(local) = crate::mission::pub_bone(&lib.dir, &anim, bone) else { continue };
                    let (_, rot, _) = gt.to_scale_rotation_translation();
                    spawn_bomb(&mut commands, b, Transform { translation: gt.transform_point(local), rotation: rot, scale: Vec3::splat(BOMB_SCALE) });
                    task.planted += 1;
                    info!("EVENT tower: bomb {} planted at {bone}", task.planted);
                }
                _ => {}
            }
        }
        // the plant over (state 0x64's end, state 0 again): the bomb loop from where it left him
        if task.phase == Phase::Planting && m.ended {
            task.phase = Phase::BombLoop;
            let mut looped = crate::mover::Mover::new(LOOP_ANIM, m.world);
            looped.looping = true;
            commands.entity(e).insert(looped);
            info!("EVENT tower: bombs planted ({}), the bomb loop", task.planted);
        }
    }
}

fn spawn_bomb(commands: &mut Commands, b: &crate::player::BombAssets, at: Transform) -> Entity {
    let e = commands.spawn((at, Visibility::default(), TowerBomb)).id();
    for (m, mat) in &b.parts {
        let c = commands.spawn((Mesh3d(m.clone()), MeshMaterial3d(mat.clone()))).id();
        commands.entity(e).add_child(c);
    }
    e
}

/// The playtest state's `tower`: the task's phase, bombs planted, the distance left to the marker, the companion's
/// health and game-space position, the mission's displayed countdown (the timer the marker's trigger starts).
fn log(q: Query<(&TowerTask, &Transform, &crate::fx::Health)>, mission: Option<Res<crate::mission::Mission>>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let Some((task, t, h)) = q.iter().next() else { return };
    let left = match &task.phase {
        Phase::Walking { goal, .. } => (*goal - t.translation).with_y(0.0).length().round(),
        _ => 0.0,
    };
    let g = crate::level::game_to_engine().inverse() * t.translation;
    let timer = mission.and_then(|m| m.timer_display).map(|s| s.round());
    state.0.insert(
        "tower".into(),
        serde_json::json!({"phase": task.phase_name(), "planted": task.planted, "left": left, "health": h.0.round(), "pos": {"x": g.x.round(), "y": g.y.round(), "z": g.z.round()}, "timer": timer}),
    );
}
