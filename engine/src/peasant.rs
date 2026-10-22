//! Peasants (PeasantObject, vtable 0x005a8550; src/game/game_01.cpp:1153-1385, src/physics/physics_01.cpp:560-765,
//! the voice lines src/audio/audio_01.cpp:76-120 / 1430-1494, the mission's part src/game/game_02.cpp:2187-2348;
//! docs/subsystems/peasants.md): the little villagers Roman rescues. He picks one up by walking up to him, drags him
//! along on a rope behind him (at most three at a time), and banks them on a door mat; the mission counts the saved
//! and the lost.
//!
//! The parts, as in the original:
//! - [`Peasant`]: the placed peasant_man / peasant_woman (behaviour `peasant`, PeasantObject_Load 0x004056b0) or the one
//!   in a squad goliath's cage (Squad_Init 0x00433220). A trigger sphere of `sphere_radius` (20) that Roman alone
//!   reaches (TriggerObject collision group 8); its contact callback (slot +0xb4, 0x00406210) captures him while
//!   fewer than three are carried and sends his `on_enter` once.
//! - The rope (the update slot +0xbc, 0x00405d10): the first update after the capture makes him a ragdoll, plays the
//!   pick-up sound and line (0x004058f0) and counts him as carried; from then on he hangs on the chain (+0x480: 1 at
//!   Roman, 2 at the first, ...), his pelvis pulled to his leader's by a hard key-frame whenever it is farther than
//!   the rope, which reels in at 5 units / s down to 3.
//! - The bank (MissionObject_OnTrigger 0x0044d600, the mission's peasant_saved_trigger from a door mat): while the
//!   mat lives, the last of the chain is saved (0x004063a0), killed and announced (0x0044d1f0), and the mat's own
//!   death slot runs (a one-shot mat dies, so it banks one); then peasant_success_trigger once the saved reach
//!   peasant_max_saved.
//! - A lost peasant (the kill slot 0x004062a0): killed_score_value, the casualties, the level lost (state 8) at
//!   peasant_max_casualties.
//!
//! The civilians a destroyed house throws out (PeasantGenAI, toss_peasants, 0x0040d390) are [`toss`]'s; the house's side
//! (its score, the soldiers' toss) is tossed.rs.

pub mod toss;

use crate::ai::{Companion, Player};
use crate::fx::{DClass, Destructible};
use crate::level::{InstanceId, Placed};
use crate::levelreset::LevelAppExt;
use crate::mission::{Mission, SendTrigger, VolumeSent};
use crate::physics::{Body, Physics, jv};
use crate::skin::Animator;
use bevy::prelude::*;

/// The object-list order of the caged peasants (made by the squads' loads, after the placed objects): this plus the
/// squad's entry.
pub const CAGED_ORDER: u32 = 1_000_000;
/// Fewer than this many carried for a pick-up (0x00406210: Game+0x26f4 < 3).
pub const MAX_CARRIED: u32 = 3;
/// Roman's body as the peasants' trigger spheres meet it (TriggerObject collision group 8): a sphere of this radius this
/// far above his feet. Fitted to the original (oracle peasant_l4m2 and a drop probe, docs/subsystems/peasants.md): on
/// the flat he is caught 17.2..17.4 units from the peasant, falling onto him 37.3..38.7 above his feet; his Havok
/// collision is not traced, so the two numbers are the fit, not his model's.
const ROMAN_BODY_HEIGHT: f32 = 5.5;
const ROMAN_BODY_RADIUS: f32 = 3.3;
/// The rope's shortest length and the pull's dead zone, 3 units (K 0x005bcb7c).
const ROPE_MIN: f32 = 3.0;
/// The rope reels in at 5 units / s while above 5 units (K 0x005bcbdc), and the drop-off lines play 5 above the mat.
const REEL: f32 = 5.0;
/// The hard key-frame's inverse time (0x004df0d0, Havok's applyHardKeyFrame with 10.0): the pelvis is given the
/// velocity that brings it to the pulled position in 0.1 s.
const KEYFRAME_RATE: f32 = 10.0;
/// The pick-up and drop-off voice lines play at most every 10 s (K 0x005bcb68; Game+0xbcc, Game+0xba4).
const LINE_GAP: f32 = 10.0;
/// A grunt's height (grunt_1, 9.8 units): the ragdoll's parts are sized for it (ragdoll.rs); a peasant's (peasant_man_2,
/// 5.3 units: the peasants are small).
const GRUNT_HEIGHT: f32 = 9.8;
const PEASANT_HEIGHT: f32 = 5.3;
/// The tied peasant's ragdoll: his size, and joints that hold his pose (Body_addConstraints 0x00456570: each joint's
/// strength (mass A + mass B) x 20 in the original's units, where gravity is 64.35 units / s^2; as a friction torque here
/// with the same ratio to the weight under the world's gravity, the game's 64.35 units / s^2 = 12.87 m/s^2:
/// 20 x 12.87 x 0.2 / 64.35 = 0.8 N m per kg). The original's tied peasants stand until pulled (oracle peasant_l4m2:
/// the pelvis stays 2.7..3.3 above the ground).
const RAGDOLL: crate::ragdoll::Build = crate::ragdoll::Build {
    size: PEASANT_HEIGHT / GRUNT_HEIGHT,
    joint_friction: 20.0 * crate::level::METERS_PER_UNIT * crate::level::METERS_PER_UNIT,
    radius: Some(1.0),
};

/// One peasant (PeasantObject +0x460..+0x498).
#[derive(Component, Debug, Clone)]
pub struct Peasant {
    /// count +0x484 (default 1): how many he counts for, saved or lost; 0 once banked
    pub count: u32,
    /// saved_score_value +0x490 (p_peasants 500) to the Rescues; killed_score_value +0x494 (-100) to the Accidents
    saved_score: f32,
    killed_score: f32,
    /// on_enter +0x460: sent at the capture, once
    on_capture: i64,
    /// sphere_radius +0x470 (p_peasants 20): his trigger sphere
    reach: f32,
    /// a peasant_woman (Object_GetName 0x00453270 is not "peasant_man": her pick-up has no peasant line)
    pub woman: bool,
    /// +0x48c: captured by Roman (set by the contact, acted on by the next update)
    pub captured: bool,
    /// +0x480: his place on the rope chain (0 off it, 1 at Roman, 2 at the first, ...)
    pub link: i32,
    /// +0x478 / +0x48d: the rope, from the update after the capture
    pub rope: Option<Rope>,
    /// held by the cage on a squad goliath's chest (Object_Attach 0x004539f0 flags 0x14: +0x34 set): no pick-up and no
    /// rope until the cage goes (0x00453750: flag 4 detaches him when his parent dies)
    pub held_by: Option<Entity>,
    /// Roman inside the sphere last frame (the contact callback runs on entering)
    roman_inside: bool,
    /// the order the objects were made in (the object list's walk: the chain is worked out in this order)
    order: u32,
    /// where he was tied: the object's own position from then on (the ragdoll's bodies move, the object matrix stays:
    /// the original's trace shows it so), engine space
    tied_at: Option<Vec3>,
    /// FX_peasant (Fx_Create in the load, +0x498; released at the capture and by PeasantObject_Remove 0x00405850): the
    /// anchor its fx follows
    fx: Option<Entity>,
}

/// The rope of a captured peasant (+0x478 length, +0x48d reeling in).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Rope {
    pub length: f32,
    pub reeling: bool,
}

impl Rope {
    /// One update of the rope (0x00405d10): `distance` from the leader's point to his pelvis, `dt` the frame. A rope of
    /// length 0 (just tied, or the leader changed) takes the distance; while reeling it shortens by 5 / s, keeps
    /// reeling above 5 units and stops at 3. Returns how far to pull the pelvis toward the leader (the distance past
    /// the rope, when beyond 3 units), before the reel of this frame.
    pub fn step(&mut self, distance: f32, dt: f32) -> Option<f32> {
        if self.length == 0.0 {
            self.length = distance;
        }
        let pull = (distance > ROPE_MIN && distance > self.length).then(|| distance - self.length);
        if self.reeling {
            self.length -= dt * REEL;
        }
        if self.length > REEL {
            self.reeling = true;
        }
        if self.length < ROPE_MIN {
            self.length = ROPE_MIN;
            self.reeling = false;
        }
        pull
    }
}

impl Peasant {
    /// PeasantObject_Load 0x004056b0's properties: count (default 1), saved_score_value, killed_score_value, and the
    /// TriggerObject_Load 0x00497460 ones it starts from (on_enter, sphere_radius).
    pub fn from_props(p: &crate::gamedata::Props, template: &str, order: u32) -> Self {
        Peasant {
            count: p.i("count", 1).max(0) as u32,
            saved_score: p.f("saved_score_value", 0.0),
            killed_score: p.f("killed_score_value", 0.0),
            on_capture: p.i("on_enter", 0),
            reach: p.f("sphere_radius", 0.0),
            woman: !template.eq_ignore_ascii_case("peasant_man"),
            captured: false,
            link: 0,
            rope: None,
            held_by: None,
            roman_inside: false,
            order,
            tied_at: None,
            fx: None,
        }
    }

    /// The health record of a peasant (max_health 2000 of p_peasants): the base damage slot with no filter (0x00406280
    /// -> Object_TakeDamage 0x004555e0, except while attached). He is in none of the damage lists (projectiles
    /// World+0xe90, blasts +0xe94: PeasantObject_Load links only +0xe9c, +0xef8, +0xefc), so bullets pass him and blasts
    /// spare him (`BlastProof`, level.rs keeps him out of the bullets' list); only a direct hit reaches the slot. His
    /// death slot is 0x004062a0 (killed_score_value: the record's score, so fx.rs pays it to the Accidents).
    pub fn health(&self, p: &crate::gamedata::Props) -> Destructible {
        let max = p.f("max_health", 2000.0);
        Destructible {
            health: max,
            max_health: max,
            fx_death: None,
            husk: None,
            area_damage: 0.0,
            radius_inner: 0.0,
            radius_outer: 0.0,
            impact_force: 0.0,
            chain: false,
            start_delay: 0.0,
            min_delay: 0.0,
            acceleration: 1.0,
            dying: None,
            chain_delay: 0.0,
            class: DClass::Open,
            score: self.killed_score,
            takes_damage: true,
            takes_bullet: true,
            player_damage: true,
            teammate_damage: true,
            badguy_damage: true,
        }
    }
}

/// The level's peasant counters (Game+0x26e0..+0x26f8, cleared at the mission's start by 0x0044d1b0) and the line
/// timers.
#[derive(Resource, Default, Debug)]
pub struct PeasantTally {
    pub rules: crate::mission::PeasantRules,
    /// +0x26f4 carried, +0x26e8 saved, +0x26e4 lost
    pub carried: u32,
    pub saved: u32,
    pub casualties: u32,
    /// peasant_success_trigger sent (+0x26f8 cleared)
    success_sent: bool,
    /// Game+0xbcc / +0xba4: the game time of the last pick-up / drop-off line (0 at the start: none before 10 s)
    pickup_line: f32,
    dropoff_line: f32,
    rules_read: bool,
}

/// A door mat's health record: the death slot only (nothing damages a trigger volume), its fx_death and husk
/// (p_doormat: fx_doormat_die, doormat_d) for Object_Kill.
pub fn mat_health(p: &crate::gamedata::Props) -> Destructible {
    Destructible {
        health: 1.0,
        max_health: 1.0,
        fx_death: p.s("fx_death").filter(|s| !s.eq_ignore_ascii_case("null")).map(str::to_owned),
        husk: p.s("husk").map(str::to_owned),
        area_damage: 0.0,
        radius_inner: 0.0,
        radius_outer: 0.0,
        impact_force: 0.0,
        chain: false,
        start_delay: 0.0,
        min_delay: 0.0,
        acceleration: 1.0,
        dying: None,
        chain_delay: 0.0,
        class: DClass::Default,
        score: 0.0,
        takes_damage: false,
        takes_bullet: false,
        player_damage: false,
        teammate_damage: false,
        badguy_damage: false,
    }
}

/// The peasants' own rand() stream (the game's CRT form): the voice lines' picks, kept apart from the shared streams so
/// a level without peasants draws as before.
#[derive(Resource, Default)]
struct PeasantRng(crate::fxvm::CrtRand);

pub struct PeasantPlugin;

impl Plugin for PeasantPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins(toss::TossPlugin)
            .init_level_resource::<PeasantTally>()
            .init_level_resource::<PeasantRng>()
            .add_systems(Update, (setup, release_from_cage, pick_up, rope, bank, compass, log).chain().run_if(crate::playing))
            // a death as fx.rs reports it, before level.rs's husks remove the object (and its cage's parent)
            .add_systems(Update, (deaths, cage_parent_died).after(crate::fx::FxSet).before(crate::mission::deaths).run_if(crate::playing));
    }
}

/// The load's remainder (PeasantObject_Load 0x004056b0): FX_peasant on him (Fx_Create with the object to follow), the
/// rules read from the mission once.
fn setup(mut commands: Commands, mut q: Query<(Entity, &mut Peasant), Added<Peasant>>, mut fx: MessageWriter<crate::fx::StartFx>, mut tally: ResMut<PeasantTally>, mission: Option<Res<Mission>>, gt: Query<&GlobalTransform>) {
    if !tally.rules_read {
        if let Some(m) = &mission {
            tally.rules = m.peasant_rules;
            tally.rules_read = true;
        }
    }
    for (e, mut p) in &mut q {
        let anchor = commands.spawn((Transform::IDENTITY, Visibility::default())).id();
        commands.entity(e).add_child(anchor);
        let at = gt.get(e).map(|t| t.translation()).unwrap_or_default();
        fx.write(crate::fx::StartFx { name: "fx_peasant".into(), at, rotation: Quat::IDENTITY, follow: Some(anchor), position_only: false });
        p.fx = Some(anchor);
    }
}

/// The cage's parent died (0x00453750 on the cage: no flag 4, so it is removed; on the peasant: flag 4, so he is
/// detached, and flag 0x10 makes him dynamic, Object_Detach 0x00453b20): he falls free, a rigid body until picked up.
fn cage_parent_died(mut died: MessageReader<crate::fx::Died>, mut q: Query<(Entity, &mut Peasant, &GlobalTransform)>, mut commands: Commands, mut physics: NonSendMut<Physics>) {
    let dead: Vec<Entity> = died.read().map(|d| d.0).collect();
    if dead.is_empty() {
        return;
    }
    for (e, mut p, gt) in &mut q {
        if p.held_by.is_some_and(|h| dead.contains(&h)) {
            p.held_by = None;
            free_fall(&mut commands, &mut physics, e, gt);
            info!("EVENT peasant: freed from the cage");
        }
    }
}

/// A held peasant whose holder is gone (removed without a death) falls free too.
fn release_from_cage(mut q: Query<(Entity, &mut Peasant, &GlobalTransform)>, alive: Query<(), With<Transform>>, mut commands: Commands, mut physics: NonSendMut<Physics>) {
    for (e, mut p, gt) in &mut q {
        if p.held_by.is_some_and(|h| alive.get(h).is_err()) {
            p.held_by = None;
            free_fall(&mut commands, &mut physics, e, gt);
            info!("EVENT peasant: freed (holder gone)");
        }
    }
}

/// Out of the cage at his place in the world, a dynamic body round his model (peasant_coll: 4.2 x 1.3 x 5.3).
fn free_fall(commands: &mut Commands, physics: &mut Physics, e: Entity, gt: &GlobalTransform) {
    let world = gt.compute_transform();
    commands.entity(e).remove::<ChildOf>().insert(Transform::from_translation(world.translation).with_rotation(world.rotation));
    let k = physics.meters_per_unit;
    let centre = Vec3::new(0.0, 0.0, 2.65);
    let Ok(shape) = oxijolt::prelude::Shape::new_box(jv(Vec3::new(1.0, 0.65, 2.65) * k)) else { return };
    let at = world.translation + world.rotation * centre;
    let settings = oxijolt::prelude::BodySettings::new_dynamic().mass(10.0).friction(0.8).angular_damping(0.4);
    if let Some(id) = physics.add_dynamic(e, &shape, at, world.rotation, Vec3::ZERO, settings) {
        commands.entity(e).insert(Body { id, offset: centre, sync_rotation: true });
    }
}

/// The contact callback (TriggerObject collision group 8: the player only; 0x00406210): Roman entering the sphere of a
/// living, free peasant while fewer than three are carried captures him and sends his on_enter, once. Nothing while
/// the game is paused (the contacts come from the physics step).
fn pick_up(
    vtime: Res<Time<Virtual>>,
    mut q: Query<(Entity, &mut Peasant, &GlobalTransform, Option<&Destructible>)>,
    player: Query<&GlobalTransform, With<Player>>,
    tally: Res<PeasantTally>,
    mut out: MessageWriter<SendTrigger>,
) {
    let Ok(roman) = player.single() else { return };
    if vtime.is_paused() {
        return;
    }
    let body = roman.translation() + Vec3::Y * ROMAN_BODY_HEIGHT;
    let mut order: Vec<(u32, Entity)> = q.iter().map(|(e, p, _, _)| (p.order, e)).collect();
    order.sort_unstable();
    for (_, e) in order {
        let Ok((_, mut p, gt, d)) = q.get_mut(e) else { continue };
        // the phantom: a sphere of sphere_radius centred that far above him (0x00405880 -> 0x004574e0: its bottom at
        // his feet), touched by Roman's body
        let centre = gt.translation() + Vec3::Y * p.reach;
        let inside = centre.distance(body) <= p.reach + ROMAN_BODY_RADIUS;
        let entering = inside && !p.roman_inside;
        p.roman_inside = inside;
        let alive = d.is_none_or(|d| d.health > 0.0 && d.dying.is_none());
        if !entering || p.captured || !alive || tally.carried >= MAX_CARRIED || p.held_by.is_some() {
            continue;
        }
        p.captured = true;
        info!("EVENT peasant: captured ({} carried)", tally.carried);
        if p.on_capture != 0 {
            out.write(SendTrigger(p.on_capture));
            p.on_capture = 0;
        }
        // (the carried count rises at his next update, 0x00405d10, so two in one contact step may both pass)
    }
}

/// The update 0x00405d10: a captured peasant's first update ties him (ragdoll, pick-up sound and line, carried); then
/// the chain and the pull, in the object list's order.
#[allow(clippy::too_many_arguments)]
fn rope(
    time: Res<Time>,
    mut commands: Commands,
    mut q: Query<(Entity, &mut Peasant, &mut Transform, Option<&Placed>, Option<&Body>, Option<&crate::ragdoll::Ragdoll>, Option<&mut Animator>)>,
    roman: Query<(Entity, &Animator), (With<Player>, Without<Peasant>)>,
    joints: Query<&GlobalTransform>,
    mut physics: NonSendMut<Physics>,
    mut tally: ResMut<PeasantTally>,
    mut rng: ResMut<PeasantRng>,
    companions: Query<(Entity, &Companion)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let dt = time.delta_secs();
    if dt <= 0.0 {
        return;
    }
    let now = time.elapsed_secs();
    let k = physics.meters_per_unit;
    let roman_e = roman.single().ok().map(|r| r.0);
    // the leader's point for Roman: his "center" bone (vt+0x90 with the name "center")
    let roman_point = roman.single().ok().and_then(|(_, a)| bone_point(a, &joints, "center"));
    let q_alive = companions.iter().find(|(_, c)| c.q && c.dead.is_none()).map(|c| c.0);
    // first updates: tie the newly captured
    for (e, mut p, _, placed, body, rd, anim) in &mut q {
        if !p.captured || p.rope.is_some() || p.held_by.is_some() {
            continue;
        }
        // physics off the standing body (0x004577b0 0 on the peasant: from the key-framed stand to the ragdoll)
        if let Some(c) = placed.and_then(|pl| pl.collider) {
            physics.remove_static(c);
        }
        if let Some(b) = body {
            physics.remove(b.id);
            commands.entity(e).remove::<Body>();
        }
        // the pick-up sound and line (0x004058f0), FX_peasant released, off the compass list, carried
        pickup_lines(&mut sounds, &mut rng.0, &mut tally, now, e, p.woman, roman_e, q_alive);
        if let Some(a) = p.fx.take() {
            commands.entity(a).despawn();
        }
        tally.carried += p.count;
        // a ragdoll from his pose (Object_SetRagdoll 0x00459000, Body_addConstraints 0x00456570), parts sized to him
        if rd.is_none() {
            if let Some(mut a) = anim {
                if let Some(r) = crate::ragdoll::start_with(&mut physics, &a, &joints, Vec3::ZERO, RAGDOLL) {
                    a.layers.clear();
                    commands.entity(e).insert(r);
                }
            }
        }
        p.rope = Some(Rope { length: 0.0, reeling: true });
        p.tied_at = joints.get(e).ok().map(|t| t.translation());
        info!("EVENT peasant: tied ({} carried)", tally.carried);
    }
    // the chain, in the list's order: each takes the largest place below his own as his leader (else, new on the
    // chain, the last one; else Roman)
    let mut all: Vec<(u32, Entity)> = q.iter().map(|(e, p, ..)| (p.order, e)).collect();
    all.sort_unstable();
    let mut links: Vec<(Entity, i32)> = all.iter().filter_map(|(_, e)| q.get(*e).ok().map(|(_, p, ..)| (*e, p.link))).collect();
    for i in 0..links.len() {
        let (e, own) = links[i];
        let Ok((_, p, ..)) = q.get(e) else { continue };
        if p.rope.is_none() {
            continue;
        }
        let others = links.iter().enumerate().filter(|(j, _)| *j != i).map(|(_, l)| *l);
        let leader = choose_leader(own, others);
        let (link, target) = match leader {
            Some((le, l)) => (l + 1, Some(le)),
            None => (1, None),
        };
        links[i].1 = link;
        let goal = match target {
            Some(le) => center_body(&physics, q.get(le).ok().and_then(|x| x.5)),
            None => roman_point,
        };
        let Ok((_, mut p, mut tr, _, _, rd, _)) = q.get_mut(e) else { continue };
        if p.link != link {
            p.link = link;
            if let Some(r) = p.rope.as_mut() {
                r.length = 0.0;
                r.reeling = true;
            }
        }
        let Some(pelvis) = rd.and_then(|r| physics.world.ragdoll(r.id).ok()).and_then(|r| r.body_ids().first().copied()) else { continue };
        let Ok(own_pos) = physics.world.body(pelvis).map(|b| crate::physics::br(b.position()) / k) else { continue };
        // the root follows the pelvis (the object's position is its body's)
        tr.translation = own_pos;
        let Some(goal) = goal else { continue };
        let delta = goal - own_pos;
        let Some(rope) = p.rope.as_mut() else { continue };
        if let Some(step) = rope.step(delta.length(), dt) {
            // applyHardKeyFrame toward own + dir x step, the rotation kept: the velocity to get there in 0.1 s
            let v = delta.normalize_or_zero() * step * KEYFRAME_RATE;
            if let Ok(mut b) = physics.world.body_mut(pelvis) {
                b.activate();
                let _ = b.set_linear_velocity(jv(v * k));
                let _ = b.set_angular_velocity(jv(Vec3::ZERO));
            }
        }
    }
}

/// 0x00405d10's leader: among the others' places, the largest below `own` (first in list order on ties), else (new on
/// the chain, `own` 0) the largest of all; with neither, Roman (None). Returns the leader and its place.
pub fn choose_leader(own: i32, others: impl Iterator<Item = (Entity, i32)>) -> Option<(Entity, i32)> {
    let (mut below, mut highest): (Option<(Entity, i32)>, Option<(Entity, i32)>) = (None, None);
    for (e, v) in others {
        if v < own && below.is_none_or(|b| b.1 < v) && v > 0 {
            below = Some((e, v));
        }
        if v > 0 && highest.is_none_or(|h| h.1 < v) {
            highest = Some((e, v));
        }
    }
    below.or(if own == 0 { highest } else { None })
}

/// A ragdoll's pelvis position (its "center" part: the centre of mass of the body named "center", 0x004586a0).
fn center_body(physics: &Physics, rd: Option<&crate::ragdoll::Ragdoll>) -> Option<Vec3> {
    let id = physics.world.ragdoll(rd?.id).ok()?.body_ids().first().copied()?;
    physics.world.body(id).ok().map(|b| crate::physics::br(b.position()) / physics.meters_per_unit)
}

/// A bone's world position (last frame's propagation).
fn bone_point(a: &Animator, joints: &Query<&GlobalTransform>, name: &str) -> Option<Vec3> {
    let i = *a.skeleton.index.get(name)?;
    joints.get(*a.joints.get(i)?).ok().map(|t| t.translation())
}

/// The pick-up's sound and voice line (0x004058f0): ui_peasant_pickup on him; then, at most every 10 s of game time
/// (Game+0xbcc), r = (rand() x span) >> 15 + base over the lines: a man's 21 PE1 / PE2 lines (0..0x833, each 100 of
/// r), Roman's three (0x834..0x95f, 100 each, unplaced), and with Q two of his (0x960..0xa28). A woman starts at 0x834.
#[allow(clippy::too_many_arguments)]
fn pickup_lines(sounds: &mut MessageWriter<crate::audio::PlaySound>, rng: &mut crate::fxvm::CrtRand, tally: &mut PeasantTally, now: f32, peasant: Entity, woman: bool, roman: Option<Entity>, q: Option<Entity>) {
    sounds.write(crate::audio::PlaySound::on("ui_peasant_pickup", peasant));
    if now - tally.pickup_line < LINE_GAP {
        return;
    }
    tally.pickup_line = now;
    const PEASANT: [&str; 21] = [
        "pe150042_peasantpickup", "pe150044_peasantpickup", "pe150045_peasantpickup", "pe150046_peasantpickup", "pe150047_peasantpickup",
        "pe150048_peasantpickup", "pe150049_peasantpickup", "pe150050_peasantpickup", "pe150053_peasantpickup", "pe150054_peasantpickup",
        "pe150055_peasantpickup", "pe250035_peasantpickup", "pe250037_peasantpickup", "pe250038_peasantpickup", "pe250039_peasantpickup",
        "pe250040_peasantpickup", "pe250041_peasantpickup", "pe250042_peasantpickup", "pe250046_peasantpickup", "pe250047_peasantpickup",
        "pe250048_peasantpickup",
    ];
    let base: u32 = if woman { 0x834 } else { 0 };
    let span = if q.is_some() { 0xc9 } else { 0 } + 0x95f - base + 1;
    let r = ((rng.rand() * span) >> 15) + base;
    let line = match r {
        r if r < 0x834 => crate::audio::PlaySound::on(PEASANT[(r / 100) as usize], peasant),
        r if r < 0x898 => roman_line("rom50195_peasantpickup", roman),
        r if r < 0x8fc => roman_line("rom50196_peasantpickup", roman),
        r if r < 0x960 => roman_line("rom50198_peasantpickup", roman),
        r if r < 0x9c4 => on_or_ui("q1150178_peasantpickup", q),
        _ => on_or_ui("q1150180_peasantpickup", q),
    };
    sounds.write(line);
}

/// Roman's own lines play unplaced (audio +0xd0).
fn roman_line(name: &str, _roman: Option<Entity>) -> crate::audio::PlaySound {
    crate::audio::PlaySound::ui(name)
}

fn on_or_ui(name: &str, who: Option<Entity>) -> crate::audio::PlaySound {
    match who {
        Some(e) => crate::audio::PlaySound::on(name, e),
        None => crate::audio::PlaySound::ui(name),
    }
}

/// The mission's peasant_saved_trigger from a door mat (MissionObject_OnTrigger 0x0044d600, Game+0x26f0): with any
/// carried, while the mat lives, the peasant with the largest place on the chain (0x00406330) is banked (0x004063a0:
/// saved += count, carried -= count, saved_score_value to the Rescues, count 0), killed (his death slot, vt+0xd4:
/// killed_score_value to the Accidents too, the original's quirk, docs/original-bugs.md), announced at the mat
/// (0x0044d1f0), and the mat's death slot runs (0x00497620: a one-shot mat dies, so it banks one; a mat without
/// one_shot, lev_bonus_m2's, banks them all). Then, once the saved reach peasant_max_saved, peasant_success_trigger
/// (once).
#[allow(clippy::too_many_arguments)]
fn bank(
    time: Res<Time>,
    mut from: MessageReader<VolumeSent>,
    mut tally: ResMut<PeasantTally>,
    mut m: Option<ResMut<Mission>>,
    mut q: Query<(Entity, &mut Peasant, &mut Destructible), Without<Mat>>,
    mut mats: Query<(Entity, &InstanceId, &GlobalTransform, &mut Destructible), With<Mat>>,
    mut ms: ResMut<crate::stats::MissionStats>,
    mut out: MessageWriter<SendTrigger>,
    mut rng: ResMut<PeasantRng>,
    companions: Query<(Entity, &Companion)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let Some(m) = m.as_deref_mut() else {
        from.clear();
        return;
    };
    let now = time.elapsed_secs();
    let sent: Vec<VolumeSent> = from.read().copied().collect();
    for s in sent {
        if s.trigger == 0 || s.trigger != tally.rules.saved_trigger || tally.carried == 0 {
            continue;
        }
        let one_shot = m.volume(s.volume).is_some_and(|v| v.one_shot);
        let at = mats.iter().find(|x| x.1 .0 == s.volume).map(|x| x.2.translation());
        let mut mat_alive = at.is_some() || m.volume(s.volume).is_some();
        while mat_alive {
            // 0x00406330: the largest place (> 0), the first on ties
            let mut best: Option<(u32, Entity, i32)> = None;
            for (e, p, d) in &q {
                if p.link > 0 && d.dying.is_none() && best.is_none_or(|b| p.link > b.2 || (p.link == b.2 && p.order < b.0)) {
                    best = Some((p.order, e, p.link));
                }
            }
            let Some((_, e, _)) = best else { break };
            let Ok((_, mut p, mut d)) = q.get_mut(e) else { break };
            tally.saved += p.count;
            tally.carried = tally.carried.saturating_sub(p.count);
            ms.rescues_money += p.saved_score;
            let woman = p.woman;
            p.count = 0;
            p.link = 0;
            // his death slot (0x004062a0 with count 0: no casualty) then Object_Kill: fx.rs pays killed_score_value
            d.health = 0.0;
            d.dying = Some(0.0);
            info!("EVENT peasant: saved at mat {} ({} saved, {} carried)", s.volume, tally.saved, tally.carried);
            let pos = at.unwrap_or_default() + Vec3::Y * REEL;
            dropoff_lines(&mut sounds, &mut rng.0, &mut tally, now, pos, woman, &companions);
            // the mat's death slot: a one-shot mat dies (Object_Kill: removed, its fx_death and husk)
            if one_shot {
                mat_alive = false;
                m.remove_volume(s.volume);
                if let Some((_, _, _, mut md)) = mats.iter_mut().find(|x| x.1 .0 == s.volume) {
                    md.health = 0.0;
                    md.dying = Some(0.0);
                }
            }
        }
        if tally.rules.success_trigger != 0 && !tally.success_sent && tally.saved >= tally.rules.max_saved {
            tally.success_sent = true;
            info!("EVENT peasant: {} saved -> peasant_success_trigger {}", tally.saved, tally.rules.success_trigger);
            out.write(SendTrigger(tally.rules.success_trigger));
        }
    }
}

/// The drop-off's sound and line (0x0044d1f0): ui_peasant_release (on the mission object: unplaced here, guess); then
/// at most every 10 s of game time (Game+0xba4) r = (rand() x (top - base + 1)) >> 15 + base: fourteen peasant lines
/// at the mat (0..0x577, 100 each), Roman's two (0x578..0x63f), Q's three and Jonesy's one when they are with him (top:
/// 0x63f alone, 0x6a3 with Jonesy, 0x76b with Q, 2000 with both). A woman starts at 0x578.
fn dropoff_lines(sounds: &mut MessageWriter<crate::audio::PlaySound>, rng: &mut crate::fxvm::CrtRand, tally: &mut PeasantTally, now: f32, at: Vec3, woman: bool, companions: &Query<(Entity, &Companion)>) {
    sounds.write(crate::audio::PlaySound::ui("ui_peasant_release"));
    if now - tally.dropoff_line < LINE_GAP {
        return;
    }
    tally.dropoff_line = now;
    const PEASANT: [&str; 14] = [
        "pe150051_peasantdropoff", "pe150052_peasantdropoff", "pm154001_peasantdropoff", "pm154003_peasantdropoff", "pm154005_peasantdropoff",
        "pm154009_peasantdropoff", "pm154011_peasantdropoff", "pe250043_peasantdropoff", "pe250044_peasantdropoff", "pe250045_peasantdropoff",
        "pm254002_peasantdropoff", "pm254006_peasantdropoff", "pm254008_peasantdropoff", "pm254010_peasantdropoff",
    ];
    let q = companions.iter().find(|(_, c)| c.q && c.dead.is_none()).map(|c| c.0);
    let jon = companions.iter().find(|(_, c)| !c.q && c.dead.is_none()).map(|c| c.0);
    let base: u32 = if woman { 0x578 } else { 0 };
    let (top, offset): (u32, u32) = match (q, jon) {
        (None, None) => (0x63f, 0),
        (None, Some(_)) => (0x6a3, 300),
        (Some(_), Some(_)) => (2000, 0),
        (Some(_), None) => (0x76b, 0),
    };
    let r = ((rng.rand() * (top - base + 1)) >> 15) + base;
    let line = match r {
        r if r < 0x578 => crate::audio::PlaySound::at(PEASANT[(r / 100) as usize], at),
        r if r < 0x5dc => crate::audio::PlaySound::ui("rom50197_peasantdropoff"),
        r if r < 0x640 => crate::audio::PlaySound::ui("rom50199_peasantdropoff"),
        r if r < 0x6a4 - offset => on_or_ui("q1150173_peasantdropoff", q),
        r if r < 0x708 - offset => on_or_ui("q1150179_peasantdropoff", q),
        r if r < 0x76c - offset => on_or_ui("q1150181_peasantdropoff", q),
        _ => on_or_ui("jon50185_peasantdropoff", jon),
    };
    sounds.write(line);
}

/// A door mat (a visible broadcast volume, p_doormat: door_mat 1): killed by the bank (one_shot).
#[derive(Component)]
pub struct Mat;

/// A peasant's death (the kill slot 0x004062a0), from fx.rs: with a count left (not banked) he is lost: carried less
/// him when he was captured, the casualties up, and at peasant_max_casualties the level is lost (state 8). His ragdoll
/// goes with him.
fn deaths(
    mut commands: Commands,
    mut died: MessageReader<crate::fx::Died>,
    q: Query<(&Peasant, Option<&crate::ragdoll::Ragdoll>, Option<&Body>, Has<Placed>)>,
    mut tally: ResMut<PeasantTally>,
    mut m: Option<ResMut<Mission>>,
    mut physics: NonSendMut<Physics>,
) {
    for d in died.read() {
        let Ok((p, rd, body, placed)) = q.get(d.0) else { continue };
        if let Some(r) = rd {
            let _ = physics.world.remove_ragdoll(r.id);
        }
        if let Some(b) = body {
            physics.remove(b.id);
        }
        // a placed one is removed by level.rs's husks; the cage's is not a placed object
        if !placed {
            commands.entity(d.0).despawn();
        }
        if p.count == 0 {
            continue;
        }
        if p.captured && p.rope.is_some() {
            tally.carried = tally.carried.saturating_sub(p.count);
        }
        tally.casualties += p.count;
        info!("EVENT peasant: lost ({} casualties)", tally.casualties);
        if tally.casualties >= tally.rules.max_casualties {
            if let Some(m) = m.as_deref_mut() {
                m.fail_state(8, "peasant casualties");
            }
        }
    }
}

/// The compass's first list (World+0xefc): the peasants not yet picked up; and the HUD's counters on a rescue level
/// (peasant_max_saved > 0): still to find (0x00465be0: the counts 0x004063f0 on the list less the carried), saved,
/// carried.
fn compass(q: Query<(&Peasant, &GlobalTransform, Option<&Destructible>)>, mut list: ResMut<crate::mission::CompassList>, tally: Res<PeasantTally>, mut hud: ResMut<crate::hud::HudState>) {
    let alive = |d: Option<&Destructible>| d.is_none_or(|d| d.health > 0.0 && d.dying.is_none());
    list.0.clear();
    list.0.extend(q.iter().filter(|(p, _, d)| p.rope.is_none() && alive(*d)).map(|(_, t, _)| t.translation()));
    hud.peasants = (tally.rules.max_saved > 0).then(|| {
        let listed: u32 = q.iter().filter(|(_, _, d)| alive(*d)).map(|(p, _, _)| p.count).sum();
        crate::hud::PeasantCounters { to_find: listed.saturating_sub(tally.carried), saved: tally.saved, carried: tally.carried }
    });
}

/// The state log's "peasants" (playtest expect steps): the counters and each peasant's state by instance id.
fn log(tally: Res<PeasantTally>, hud: Res<crate::hud::HudState>, q: Query<(&Peasant, Option<&InstanceId>, &GlobalTransform)>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let mut list = serde_json::Map::new();
    for (p, id, t) in &q {
        let key = id.map(|i| i.0.to_string()).unwrap_or_else(|| format!("c{}", p.order));
        let g = crate::level::game_to_engine().inverse() * t.translation();
        list.insert(key, serde_json::json!({"captured": p.captured, "tied": p.rope.is_some(), "link": p.link, "held": p.held_by.is_some(), "x": g.x, "y": g.y, "z": g.z}));
    }
    state.0.insert(
        "peasants".into(),
        serde_json::json!({"carried": tally.carried, "saved": tally.saved, "casualties": tally.casualties, "alive": q.iter().count(), "list": list, "hud": hud.peasants.map(|h| serde_json::json!({"to_find": h.to_find, "saved": h.saved, "carried": h.carried}))}),
    );
}

/// The peasants as the oracle's trace lists them (World+0xef8): instance id (0 for a caged one), position (game
/// space), captured, chain place, rope, count, held.
pub fn trace_list(q: &Query<(&Peasant, Option<&InstanceId>, &GlobalTransform, Option<&Destructible>)>) -> Vec<serde_json::Value> {
    let mut v: Vec<(u32, serde_json::Value)> = q
        .iter()
        .filter(|(_, _, _, d)| d.is_none_or(|d| d.dying.is_none() && d.health > 0.0))
        .map(|(p, id, t, _)| {
            let to_game = crate::level::game_to_engine().inverse();
            let r2 = |x: f32| (x * 100.0).round() / 100.0;
            let v = |e: Vec3| {
                let g = to_game * e;
                serde_json::json!([r2(g.x), r2(g.y), r2(g.z)])
            };
            let mut o = serde_json::json!({"id": id.map(|i| i.0).unwrap_or(0), "pos": v(p.tied_at.unwrap_or(t.translation())), "captured": p.captured as i32, "link": p.link,
                "rope": r2(p.rope.map_or(-1.0, |r| r.length)), "count": p.count, "attached": p.held_by.is_some() as i32});
            // a tied one's pelvis (his root follows it, `rope`)
            if p.tied_at.is_some() {
                o["center"] = v(t.translation());
            }
            (p.order, o)
        })
        .collect();
    v.sort_by_key(|x| x.0);
    v.into_iter().map(|x| x.1).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rope_takes_the_distance_then_reels_to_three() {
        let mut r = Rope { length: 0.0, reeling: true };
        // just tied 12 units away: the rope is 12, no pull this frame, reeling starts
        assert_eq!(r.step(12.0, 0.1), None);
        assert!((r.length - 11.5).abs() < 1e-5);
        // the leader moves off: pulled by what is past the rope
        let pull = r.step(14.0, 0.1).unwrap();
        assert!((pull - 2.5).abs() < 1e-5);
        for _ in 0..40 {
            r.step(3.5, 0.1);
        }
        assert_eq!(r.length, 3.0);
        assert!(!r.reeling);
        // within 3 units never pulled
        assert_eq!(r.step(2.9, 0.1), None);
    }

    #[test]
    fn chain_leaders() {
        let e = |i: u32| Entity::from_raw_u32(i).unwrap();
        // the first one tied follows Roman
        assert_eq!(choose_leader(0, [(e(2), 0), (e(3), 0)].into_iter()), None);
        // a new one follows the last of the chain
        assert_eq!(choose_leader(0, [(e(2), 1), (e(3), 2), (e(4), 0)].into_iter()), Some((e(3), 2)));
        // a link follows the largest place below his own (the chain closes up when one goes)
        assert_eq!(choose_leader(3, [(e(2), 1), (e(4), 0)].into_iter()), Some((e(2), 1)));
        assert_eq!(choose_leader(2, [(e(3), 3)].into_iter()), None);
    }
}
