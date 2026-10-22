//! The companions' close kill: Q or Jonesy grabs the grunt he fights and kills him with his hands, a pair of IC anims
//! (src/ai/ai_02.cpp 0x00427e80, ai_04.cpp GruntObject_Slot66 0x00434ea0 / BowGruntObject_Slot66 0x004346a0, ai_06.cpp
//! states 0xc..0xf, ai_03.cpp 0x0042c670; reached in the original on lev_1_m3: Q's vt+0x138 and the grunt's Slot66 ran).
//!
//! - **The try** (0x00427e80, from the engage 0x00427910, a burst's or the clip's end of the fire cycle 0x00428980,
//!   ai_02.cpp:1942 / 1986, and the attack order 0x00484100): only a class with vt+0x1e4 (Q111Object, MolmerObject) at a target with vt+0x104 (GruntObject,
//!   BowGruntObject: not the captains, the Q112 or the monk), alive, not already in one (states 0xc..0xe), passing a
//!   coin (rand * 100 >> 15 >= 50), nearer than 50 (2500 squared, 3D), facing each other (the headings 135 degrees or
//!   more apart, K 0x005bcea8 / 0x005bceac), the grunt's "ref_fight_00" within 0.5 of his feet's height and a route to it
//!   (0x0046f6b0). Then the grunt is asked (Slot66): he refuses when dead, already held, on a gun, in a trench or a
//!   window, flying, falling, fleeing, in an IC move or a squad; else he stands still holding his aim set, state 0xf (`Mind::Held`), the companion takes him
//!   as his target (+0xab0) and runs to the point (state 0xc).
//! - **The approach** (states 0xc / 0xd / 0xe): along a route to "ref_fight_00" at his run speed, then straight to
//!   within 1 (K 0x005bcb2c), turning to ref_fight_00's facing, its negated y axis (0x00454e60). A grunt killed meanwhile, or a companion stunned or
//!   killed, ends it (the grunt's state 0xf lets go when its holder leaves 0xc..0xe: back to its fight, 0x0042bc60).
//! - **The kill** (0x0042c670 on both): each plays an IC anim from where he stands, physics off: the companion his
//!   vt+0x1a4(vt+0x138()) (Q: ids 0x81 / 0x82, even odds, mover_q_kill_grunt_1 / _2; Jonesy: 0x7f / 0x80,
//!   mover_molmer_kill_grunt_1 / _2), the grunt the same id of his own table (mover_grunt_qdie_1 / _2,
//!   mover_grunt_mdie_1 / _2), the companion into state 0x32 after it (his reload), the grunt 0x65. The grunt's anim's
//!   "kill" note kills him (ai/notes.rs: an enemy killed, the score), his body left in the anim's pose (ai.rs DeathAnim).
//!   The grunts' own close attack (0x00428160) is ai/melee.rs.
//!
//! Engine guards: the last 12 units are driven straight through the held grunt's sphere; an approach that has not
//! arrived in 10 s gives up (the original's route step fails at once). A playtest's ai_force "fight" passes the coin;
//! the playtest step close_kill grabs the nearest grunt without the coin, reach and facing tests.

use super::{Companion, Grunt, Kind, Mind};
use crate::fx::FxStats;
use crate::mover::Mover;
use crate::levelreset::LevelAppExt;
use bevy::prelude::*;

/// 0x00427e80: nearer than this (3D).
const REACH: f32 = 50.0;
/// The headings at least this far apart (degrees): facing each other.
const FACING: f32 = 135.0;
/// The fight point's height within this of the grunt's feet (K 0x005bcb4c).
const HEIGHT: f32 = 0.5;
/// The approach ends within this of the point (K 0x005bcb2c, squared).
const ARRIVED: f32 = 1.0;
/// The last stretch to the point is driven straight, through the held grunt's body (his sphere and the companion's
/// capsule would keep them farther apart than the anims' pairing).
const CLOSE_IN: f32 = 12.0;
/// A route to the point must end this near it (nav.rs stops within 1.5 of its 6-unit cells).
const ROUTE_END: f32 = 12.0;
/// Waypoints count as passed within this (the companions' route following, ai.rs).
const WAYPOINT: f32 = 4.0;
/// Turning to ref_fight_00's facing at the point: 0x00454e60's rate argument (16, rad/s; ai/body.rs `turn_toward`).
const TURN_RATE: f32 = 16.0;
/// Engine guard: an approach that has not arrived after this gives up.
const GIVE_UP: f32 = 10.0;

/// The companion's reload (state 0x32) after a close kill or a held grunt knocked out of his hands: its anim's length,
/// Q's and Jonesy's.
fn reload_time(q: bool) -> f32 {
    if q { 1.967 } else { 3.467 }
}

/// A companion's try that passed (0x00427e80 up to the grunt's answer): the grabbed grunt and his "ref_fight_00".
#[derive(Message, Clone, Debug)]
pub struct Grab {
    pub companion: Entity,
    pub grunt: Entity,
    pub point: Vec3,
    /// the way to face there: ref_fight_00's negated y axis (0x00427e80, flat)
    pub facing: Vec3,
    /// the route there (0x0046f6b0 accepted it; empty for a clear walk)
    pub route: Vec<Vec3>,
}

/// The route from `from` to the fight point (0x00427e80's 0x0046f6b0 must accept it): none needed for a clear walk
/// (nav.rs gives none for a goal within its own cell's reach), else nav.rs's ending near the point.
fn route_to(physics: &crate::physics::Physics, from: Vec3, point: Vec3) -> Option<Vec<Vec3>> {
    if super::line_of_sight(physics, from, point) {
        return Some(Vec::new());
    }
    let route = crate::nav::path(physics, from, point, &[]);
    if route.as_ref().is_none_or(|r| r.last().is_some_and(|w| (*w - point).with_y(0.0).length() > ROUTE_END)) {
        let g = |v: Vec3| crate::level::game_to_engine().inverse() * v;
        info!("EVENT companions: no route to the fight point {:?} from {:?} (ends {:?})", g(point).round(), g(from).round(), route.as_ref().and_then(|r| r.last()).map(|e| g(*e).round()));
        return None;
    }
    route
}

/// ref_fight_00 of a grunt: its point and the facing there (the negated y axis of its matrix, flat; toward the grunt
/// when that is level).
fn fight_point(a: &crate::skin::Animator, joints: &Query<&GlobalTransform, Without<Companion>>, grunt_at: Vec3) -> Option<(Vec3, Vec3)> {
    let j = a.skeleton.index.get("ref_fight_00").and_then(|&i| joints.get(a.joints[i]).ok())?;
    let point = j.translation();
    let facing = (-Vec3::from(j.affine().matrix3.y_axis)).with_y(0.0).normalize_or((grunt_at - point).with_y(0.0).normalize_or(Vec3::Z));
    Some((point, facing))
}

/// Debug (playtest `close_kill`): this companion grabs the live grunt or archer nearest him, without the try's coin,
/// reach and facing tests (the grunt's answer and the route to his "ref_fight_00" still decide).
#[derive(Message, Clone, Copy, Debug)]
pub struct DebugGrab {
    pub q: bool,
}

#[allow(clippy::type_complexity)]
fn ga_pos(grunts: &Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>, e: Entity) -> Vec3 {
    grunts.get(e).map(|(_, _, gt, _)| gt.translation()).unwrap_or(Vec3::ZERO)
}

fn debug_grabs(
    mut asks: MessageReader<DebugGrab>,
    physics: NonSend<crate::physics::Physics>,
    companions: Query<(Entity, &Companion, &GlobalTransform)>,
    grunts: Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform, Without<Companion>>,
    mut out: MessageWriter<Grab>,
) {
    for a in asks.read() {
        let Some((ce, _, ct)) = companions.iter().find(|(_, c, _)| c.q == a.q && c.dead.is_none()) else { continue };
        let me = ct.translation();
        let near = grunts.iter().filter(|(_, g, ..)| grabbable(g)).min_by(|x, y| x.2.translation().distance(me).total_cmp(&y.2.translation().distance(me)));
        let Some((ge, _, _, Some(ga))) = near else { continue };
        let Some((point, facing)) = fight_point(ga, &joints, ga_pos(&grunts, ge)) else { continue };
        let Some(route) = route_to(&physics, me, point) else { continue };
        info!("EVENT playtest: {} grabs the nearest grunt {ge:?}", if a.q { "Q" } else { "Jonesy" });
        out.write(Grab { companion: ce, grunt: ge, point, facing, route });
    }
}

/// A companion in a close kill (the formation update leaves him to this module).
#[derive(Component, Debug)]
pub struct Killing {
    grunt: Entity,
    phase: Phase,
    /// the pair's anims: the companion's and the grunt's
    anims: (&'static str, &'static str),
    t: f32,
}

#[derive(Debug, PartialEq)]
enum Phase {
    /// states 0xc..0xe: to the fight point, then turned to its facing
    Approach { point: Vec3, facing: Vec3, route: Vec<Vec3> },
    /// both IC anims playing
    Kill,
}

/// A grunt held for a close kill (state 0xf, then his IC anim): who holds him.
#[derive(Component, Debug)]
pub struct Victim {
    by: Entity,
}

/// The playtest log's counts.
#[derive(Resource, Default)]
pub struct CloseKills {
    /// grabs asked of a grunt (the tries that passed the companion's own tests)
    pub tries: u32,
    pub grabbed: u32,
    pub started: u32,
    pub kills: u32,
    pub aborted: u32,
}

pub struct CloseKillPlugin;

impl Plugin for CloseKillPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<Grab>()
            .add_message::<DebugGrab>()
            .init_level_resource::<CloseKills>()
            .add_systems(Update, (debug_grabs, grab, approach, kill, count_kills, orphans, log).chain().run_if(crate::playing));
    }
}

/// The pair of IC anims for a companion (his vt+0x138 roll, `r` in [0, 1)): his and the grunt's.
pub fn pair(q: bool, r: f32) -> (&'static str, &'static str) {
    match (q, r < 0.5) {
        (true, true) => ("mover_q_kill_grunt_1", "mover_grunt_qdie_1"),
        (true, false) => ("mover_q_kill_grunt_2", "mover_grunt_qdie_2"),
        (false, true) => ("mover_molmer_kill_grunt_1", "mover_grunt_mdie_1"),
        (false, false) => ("mover_molmer_kill_grunt_2", "mover_grunt_mdie_2"),
    }
}

/// Whether a grunt can be grabbed now (GruntObject_Slot66 / BowGruntObject_Slot66 0x004346a0's refusals, as the engine
/// models the states): dead; ledge_death (+0x484 bit 31); a window or tower grunt (+0x488 0x200080); held already
/// (0x2000); on a gun or in a trench (0x8000 / 0x20000); the scared run (0x20); in an IC move, flying, falling, fleeing
/// or in a squad. (+0x48c bit 2, +0xb54 and 0x00454be0 have no engine state.)
fn grabbable(g: &Grunt) -> bool {
    matches!(g.kind, Kind::Grunt | Kind::Bow)
        && !matches!(g.mind, Mind::Dead | Mind::Scripted | Mind::Held | Mind::Falling | Mind::Jump | Mind::Cover | Mind::Gunner | Mind::Flee | Mind::InSquad)
        && g.gun.is_none()
        && g.cover.is_none()
        && !g.window
        && !g.ledge_death
        && !g.scared_run
        && g.fixed.is_none()
}

/// The heading test of 0x00427e80: the two facings at least 135 degrees apart.
pub fn facing_each_other(yaw_a: f32, yaw_b: f32) -> bool {
    let d = (yaw_a - yaw_b + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
    d.abs().to_degrees() >= FACING
}

/// 0x00427e80 for a companion `c` (entity `me_e` at `me`) at his target, in its order: a grunt or an archer (vt+0x104)
/// alive, the coin (one rand; a playtest's `ai_force: "fight"` passes it), the reach and the facing, the fight point's
/// height, the route there (0x0046f6b0), then the grunt's answer (Slot66, `grabbable`). None when any fails: the
/// caller goes on with its own engage or fire.
#[allow(clippy::type_complexity, clippy::too_many_arguments)]
pub fn attempt(
    c: &Companion,
    me_e: Entity,
    me: Vec3,
    grunts: &Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    joints: &Query<&GlobalTransform, Without<Companion>>,
    physics: &crate::physics::Physics,
    rng: &mut FxStats,
    force: &mut super::AiForce,
) -> Option<Grab> {
    let t = c.target()?;
    let (_, g, gt, ga) = grunts.get(t).ok()?;
    if std::env::var("AIDBG").is_ok() {
        info!("AIDBG close kill try: {me_e:?} at {t:?} ({:?}, {:?})", g.kind, g.mind);
    }
    if !matches!(g.kind, Kind::Grunt | Kind::Bow) || g.mind == Mind::Dead {
        return None;
    }
    // (rand * 100) >> 15 < 50: no
    if !(force.take("fight") || rng.rand() >= 0.5) {
        return None;
    }
    let gp = gt.translation();
    if me.distance_squared(gp) >= REACH * REACH || !facing_each_other(c.yaw, g.yaw) {
        if std::env::var("AIDBG").is_ok() {
            info!("AIDBG close kill: {me_e:?} out of reach of {t:?}: {:.0} apart, yaws {:.0} / {:.0}", me.distance(gp), c.yaw.to_degrees(), g.yaw.to_degrees());
        }
        return None;
    }
    let (point, facing) = fight_point(ga?, joints, gp)?;
    if std::env::var("AIDBG").is_ok() {
        info!("AIDBG close kill: {me_e:?} at {t:?}: ref_fight_00 {:.1} off his height", point.y - gp.y);
    }
    if (point.y - gp.y).abs() > HEIGHT {
        return None;
    }
    let route = route_to(physics, me, point)?;
    if !grabbable(g) {
        info!("EVENT companions: grunt {t:?} refuses the grab ({:?})", g.mind);
        return None;
    }
    Some(Grab { companion: me_e, grunt: t, point, facing, route })
}

/// The start of the approach (the grunt still willing: he may have changed since the try); also the debug grab of a
/// playtest.
#[allow(clippy::type_complexity)]
fn grab(
    mut commands: Commands,
    mut grabs: MessageReader<Grab>,
    mut companions: Query<&mut Companion, (Without<Killing>, Without<super::carried::Knocked>, Without<super::tower::OnTask>)>,
    mut grunts: Query<&mut Grunt, Without<Victim>>,
    mut rng: ResMut<FxStats>,
    mut stats: ResMut<CloseKills>,
) {
    // (one grab per companion and per grunt: the components below land after this system)
    let mut taken: Vec<Entity> = Vec::new();
    for gr in grabs.read() {
        if taken.contains(&gr.companion) || taken.contains(&gr.grunt) {
            continue;
        }
        let Ok(mut c) = companions.get_mut(gr.companion) else { continue };
        let Ok(mut g) = grunts.get_mut(gr.grunt) else { continue };
        stats.tries += 1;
        if c.dead.is_some() || !grabbable(&g) {
            info!("EVENT companions: grunt {:?} refuses the grab ({:?})", gr.grunt, g.mind);
            continue;
        }
        g.mind = Mind::Held;
        g.walking = false;
        let anims = pair(c.q, rng.rand());
        c.engage(gr.grunt, &mut rng);
        c.moving = true;
        c.firing = false;
        commands.entity(gr.grunt).insert(Victim { by: gr.companion });
        commands.entity(gr.companion).insert(Killing { grunt: gr.grunt, phase: Phase::Approach { point: gr.point, facing: gr.facing, route: gr.route.clone() }, anims, t: 0.0 });
        taken.extend([gr.companion, gr.grunt]);
        stats.grabbed += 1;
        info!("EVENT companions: {} grabs grunt {:?} ({})", if c.q { "Q" } else { "Jonesy" }, gr.grunt, anims.0);
    }
}

/// States 0xc..0xe: the run to the fight point, the turn, then both IC anims.
#[allow(clippy::type_complexity)]
fn approach(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut q: Query<(Entity, &mut Killing, &mut Companion, &mut Transform), Without<Grunt>>,
    mut grunts: Query<(&mut Grunt, &Transform, &crate::physics::Body), Without<Companion>>,
    mut stats: ResMut<CloseKills>,
) {
    let dt = time.delta_secs();
    let k = physics.meters_per_unit;
    for (e, mut kl, mut c, mut t) in &mut q {
        let grunt = kl.grunt;
        let live = grunts.get(grunt).is_ok_and(|(g, ..)| g.mind != Mind::Dead);
        if kl.phase == Phase::Kill {
            continue;
        }
        kl.t += dt;
        // the held grunt knocked over (a topsy throw, 0x0042fe50 in state 0xf): he lets his holder go, who turns to
        // his target and reloads (state 0x32, the reload set at his aim weights)
        if live && grunts.get(grunt).is_ok_and(|(g, ..)| g.mind != Mind::Held) {
            commands.entity(e).remove::<(Killing, Mover)>();
            commands.entity(grunt).remove::<(Victim, Mover, super::DeathAnim)>();
            c.moving = false;
            c.reloading = reload_time(c.q);
            stats.aborted += 1;
            info!("EVENT companions: close kill on {grunt:?} ends: knocked out of his hands");
            continue;
        }
        if !live || c.dead.is_some() || kl.t > GIVE_UP {
            // the grunt lets go (state 0xf's test) and fights on; the companion back to his update
            release(&mut commands, e, grunt, &mut grunts.get_mut(grunt).ok().map(|(g, ..)| g));
            c.moving = false;
            stats.aborted += 1;
            info!("EVENT companions: close kill on {grunt:?} given up");
            continue;
        }
        let Ok((mut g, gt, body)) = grunts.get_mut(grunt) else { continue };
        // the held grunt stands (its velocity zeroed, Slot66)
        if let Ok(mut b) = physics.world.body_mut(body.id) {
            let _ = b.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
        }
        g.walking = false;
        let gp = gt.translation;
        let me = t.translation;
        let Phase::Approach { point, facing, route } = &mut kl.phase else { continue };
        let (point, facing) = (*point, *facing);
        while route.first().is_some_and(|w| (*w - me).with_y(0.0).length() < WAYPOINT) {
            route.remove(0);
        }
        let to = route.first().copied().unwrap_or(point) - me;
        if to.with_y(0.0).length() > ARRIVED {
            let dir = to.with_y(0.0).normalize_or_zero();
            c.yaw = dir.x.atan2(dir.z);
            t.rotation = Quat::from_rotation_y(c.yaw) * c.base;
            c.moving = true;
            let speed = c.run_speed();
            if route.is_empty() && (point - me).with_y(0.0).length() < CLOSE_IN {
                // the last few units through the grunt's own body (state 0xe's straight drive, 0x00454d80: the held
                // grunt's sphere would stop the walker short of the point)
                let step = (speed * dt).min(to.with_y(0.0).length());
                t.translation += dir * step;
                physics.place_walker(&c.walker, t.translation);
            } else {
                c.walk(&mut physics, &mut t, dir, speed, dt);
            }
            continue;
        }
        // at the point: turned to ref_fight_00's facing (0x00454e60), then the pair's IC anims from where each stands
        c.moving = false;
        let want = facing.x.atan2(facing.z);
        let d = (want - c.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
        // (state 0xe: the kill starts once he faces it, the turn's dot >= 0.98, ai_06.cpp:948)
        let (step, faced) = super::body::turn_toward(d, TURN_RATE, dt);
        c.yaw += step;
        t.rotation = Quat::from_rotation_y(c.yaw) * c.base;
        if !faced {
            continue;
        }
        let axes = Mat4::from_quat(crate::level::game_to_engine().inverse());
        let origin = |tr: &Transform| axes * Mat4::from_rotation_translation(tr.rotation, tr.translation);
        let mut mine = Mover::new(kl.anims.0, origin(&t));
        mine.generic = true;
        commands.entity(e).insert(mine);
        // (his death from here on holds the anim's pose: ai.rs DeathAnim)
        commands.entity(grunt).insert((Mover::new(kl.anims.1, origin(gt)), super::DeathAnim(kl.anims.1)));
        g.mind = Mind::Scripted;
        g.target = Some(e);
        c.ic = true;
        // the grunt's body with physics off: held where his anim has him (`kill`)
        if let Ok(mut b) = physics.world.body_mut(body.id) {
            let _ = b.set_position(crate::physics::jr((gp + body.offset) * k), oxijolt::prelude::Activation::Activate);
        }
        kl.phase = Phase::Kill;
        stats.started += 1;
        info!("EVENT companions: {} kills grunt {grunt:?} ({} / {})", if c.q { "Q" } else { "Jonesy" }, kl.anims.0, kl.anims.1);
    }
}

/// Lets a held grunt go back to his fight (state 0xf's end: 0x0042bc60) and frees the companion.
fn release(commands: &mut Commands, companion: Entity, grunt: Entity, g: &mut Option<Mut<Grunt>>) {
    commands.entity(companion).remove::<(Killing, Mover)>();
    commands.entity(grunt).remove::<(Victim, Mover, super::DeathAnim)>();
    if let Some(g) = g.as_mut() {
        if matches!(g.mind, Mind::Held | Mind::Scripted) {
            g.mind = Mind::Roam;
        }
    }
}

/// Both IC anims: the bodies follow them (physics off), the skeletons play them at the movers' time; the grunt's
/// "kill" note kills him; the companion's anim's end gives him back to his update.
#[allow(clippy::type_complexity)]
fn kill(
    mut commands: Commands,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut q: Query<(Entity, &Killing, &mut Companion, &Transform, Option<&Mover>, Option<&mut crate::skin::Animator>), Without<Grunt>>,
    mut grunts: Query<(&mut Grunt, &Transform, Option<&crate::physics::Body>, Option<&Mover>, Option<&mut crate::skin::Animator>), (Without<Companion>, With<Victim>)>,
) {
    let k = physics.meters_per_unit;
    for (e, kl, mut c, t, mover, animator) in &mut q {
        if kl.phase != Phase::Kill {
            continue;
        }
        let grunt = kl.grunt;
        if c.dead.is_some() {
            // killed during it: the anim stops there (the death is ai.rs's); the grunt carries on with his
            c.ic = false;
            commands.entity(e).remove::<(Killing, Mover)>();
            continue;
        }
        // the companion: his walker where the anim has him, his skeleton at the mover's time ("anim_full")
        physics.place_walker(&c.walker, t.translation);
        if let (Some(m), Some(mut a)) = (mover, animator) {
            play_at(&mut a, m);
        }
        if let Ok((g, gt, body, gm, ga)) = grunts.get_mut(grunt) {
            if let (Some(b), true) = (body, g.mind != Mind::Dead) {
                if let Ok(mut rb) = physics.world.body_mut(b.id) {
                    let _ = rb.set_position(crate::physics::jr((gt.translation + b.offset) * k), oxijolt::prelude::Activation::Activate);
                    let _ = rb.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
                }
            }
            if let (Some(m), Some(mut a)) = (gm, ga) {
                if g.mind != Mind::Dead || !m.ended {
                    play_at(&mut a, m);
                }
            }
        }
        if mover.is_none_or(|m| m.ended) {
            // state 0x32 after the IC anim (its end 0x0042c0e0): his reload where it left him, then back to his update
            // (his target dead: the fight's end, ai.rs)
            c.ic = false;
            c.reloading = reload_time(c.q);
            c.yaw = super::ic::yaw_of(t.rotation);
            physics.place_walker(&c.walker, t.translation);
            commands.entity(e).remove::<(Killing, Mover)>();
            commands.entity(grunt).remove::<Victim>();
            if let Ok((mut g, ..)) = grunts.get_mut(grunt) {
                if g.mind == Mind::Scripted {
                    // (the anim over without its kill: he stands where it left him)
                    g.mind = Mind::Roam;
                    commands.entity(grunt).remove::<(Mover, super::DeathAnim)>();
                }
            }
        }
    }
}

/// The grunt's anim's "kill" note (ai/notes.rs `ScriptEnd`: his death, the anim's last pose kept by ai.rs `DeathAnim`):
/// counted here.
fn count_kills(mut ends: MessageReader<super::notes::ScriptEnd>, victims: Query<&Victim>, mut stats: ResMut<CloseKills>) {
    for e in ends.read() {
        if e.end == super::notes::End::Kill && victims.contains(e.entity) {
            stats.kills += 1;
            info!("EVENT companions: grunt {:?} killed by {:?} hand to hand", e.entity, victims.get(e.entity).map(|v| v.by).ok());
        }
    }
}

/// A held grunt whose companion left the kill another way (stunned: carried.rs; removed) is let go (state 0xf's test).
fn orphans(mut commands: Commands, mut grunts: Query<(Entity, &Victim, &mut Grunt)>, holders: Query<(), With<Killing>>) {
    for (e, v, mut g) in &mut grunts {
        if holders.contains(v.by) {
            continue;
        }
        if g.mind != Mind::Dead {
            commands.entity(e).remove::<(Victim, Mover, super::DeathAnim)>();
        } else {
            commands.entity(e).remove::<(Victim, Mover)>();
        }
        if matches!(g.mind, Mind::Held | Mind::Scripted) {
            g.mind = Mind::Roam;
        }
    }
}

/// An IC anim on the whole skeleton at its mover's time (as ai/ic.rs and ai/tower.rs play theirs).
fn play_at(a: &mut crate::skin::Animator, m: &Mover) {
    if a.layers.first().is_none_or(|l| l.anim != m.anim) {
        a.play_at(0, &m.anim, false, crate::skin::Mask::All, m.t);
        a.stop(1);
    }
    if let Some(l) = a.layers.get_mut(0) {
        l.time = m.t;
        l.speed = 0.0;
    }
}

/// The playtest state's `close_kill`: grabs, kills started and done, given up, and whether one is under way.
fn log(stats: Res<CloseKills>, q: Query<&Killing>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let phase = q.iter().next().map(|k| if k.phase == Phase::Kill { "kill" } else { "approach" }).unwrap_or("none");
    state.0.insert(
        "close_kill".into(),
        serde_json::json!({"grabbed": stats.grabbed, "started": stats.started, "kills": stats.kills, "aborted": stats.aborted, "phase": phase}),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 0x00427e80's heading test: facing each other within 45 degrees.
    #[test]
    fn they_must_face_each_other() {
        let pi = std::f32::consts::PI;
        assert!(facing_each_other(0.0, pi));
        assert!(facing_each_other(0.2, -pi + 0.3));
        assert!(!facing_each_other(0.0, 0.5 * pi));
        assert!(!facing_each_other(0.0, 0.0));
        assert!(facing_each_other(0.0, 136f32.to_radians()));
        assert!(!facing_each_other(0.0, 134f32.to_radians()));
    }

    /// vt+0x138 and the tables: Q's 0x81 / 0x82 with the grunt's qdie, Jonesy's 0x7f / 0x80 with mdie.
    #[test]
    fn each_companion_has_his_pairs() {
        assert_eq!(pair(true, 0.1), ("mover_q_kill_grunt_1", "mover_grunt_qdie_1"));
        assert_eq!(pair(true, 0.9), ("mover_q_kill_grunt_2", "mover_grunt_qdie_2"));
        assert_eq!(pair(false, 0.1), ("mover_molmer_kill_grunt_1", "mover_grunt_mdie_1"));
        assert_eq!(pair(false, 0.6), ("mover_molmer_kill_grunt_2", "mover_grunt_mdie_2"));
    }
}
