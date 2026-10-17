//! The companions' close kill: Q or Jonesy grabs the grunt he fights and kills him with his hands, a pair of IC anims
//! (src/ai/ai_02.cpp 0x00427e80, ai_04.cpp GruntObject_Slot66 0x00434ea0 / BowGruntObject_Slot66 0x004346a0, ai_06.cpp
//! states 0xc..0xf, ai_03.cpp 0x0042c670; reached in the original on lev_1_m3: Q's vt+0x138 and the grunt's Slot66 ran).
//!
//! - **The try** (0x00427e80, from the engage 0x00427910, the fire cycle's burst end 0x00428980 and the attack order
//!   0x00484100): only a class with vt+0x1e4 (Q111Object, MolmerObject) at a target with vt+0x104 (GruntObject,
//!   BowGruntObject: not the captains, the Q112 or the monk), alive, not already in one (states 0xc..0xe), passing a
//!   coin (rand * 100 >> 15 >= 50), nearer than 50 (2500 squared, 3D), facing each other (the headings 135 degrees or
//!   more apart, K 0x005bcea8 / 0x005bceac), the grunt's "ref_fight_00" within 0.5 of his feet's height and a route to it
//!   (0x0046f6b0). Then the grunt is asked (Slot66): he refuses when dead, already held, on a gun or in a trench's slot,
//!   flying, in an IC move; else he stands still holding his aim set, state 0xf (`Mind::Held`), the companion takes him
//!   as his target (+0xab0) and runs to the point (state 0xc).
//! - **The approach** (states 0xc / 0xd / 0xe): along a route to "ref_fight_00" at his run speed, then straight to
//!   within 1 (K 0x005bcb2c), turning to face him (0x00454e60). A grunt killed meanwhile, or a companion stunned or
//!   killed, ends it (the grunt's state 0xf lets go when its holder leaves 0xc..0xe: back to its fight, 0x0042bc60).
//! - **The kill** (0x0042c670 on both): each plays an IC anim from where he stands, physics off: the companion his
//!   vt+0x1a4(vt+0x138()) (Q: ids 0x81 / 0x82, even odds, mover_q_kill_grunt_1 / _2; Jonesy: 0x7f / 0x80,
//!   mover_molmer_kill_grunt_1 / _2), the grunt the same id of his own table (mover_grunt_qdie_1 / _2,
//!   mover_grunt_mdie_1 / _2), the companion into state 0x32 after it, the grunt 0x65. The grunt's anim's "kill" note
//!   kills him (an enemy killed, the score), his body left as the anim ends.
//!
//! Guesses: the facing at the point (the original turns to the negated y axis of "ref_fight_00"; here toward the grunt,
//! which the bone's axis points at in the pairs' anims); a route that cannot be walked ends the try after 10 s (engine
//! guard: the original's route step fails it at once).

use super::{Companion, Grunt, Kind, Mind};
use crate::fx::{FxStats, Health};
use crate::mover::{Mover, MoverNote};
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
/// Waypoints count as passed within this (the companions' route following, ai.rs).
const WAYPOINT: f32 = 4.0;
/// Turning toward the grunt at the point (0x00454e60's rate argument 16: degrees per frame at 30 fps, guess).
const TURN_RATE: f32 = 16.0 * 30.0;
/// Engine guard: an approach that has not arrived after this gives up.
const GIVE_UP: f32 = 10.0;

/// A companion's try that passed (0x00427e80 up to the grunt's answer): the grabbed grunt and his "ref_fight_00".
#[derive(Message, Clone, Copy, Debug)]
pub struct Grab {
    pub companion: Entity,
    pub grunt: Entity,
    pub point: Vec3,
}

/// Debug (playtest `close_kill`): this companion grabs the live grunt or archer nearest him, without the try's coin,
/// reach and facing tests (the grunt's answer and the route to his "ref_fight_00" still decide).
#[derive(Message, Clone, Copy, Debug)]
pub struct DebugGrab {
    pub q: bool,
}

fn debug_grabs(
    mut asks: MessageReader<DebugGrab>,
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
        let Some(point) = ga.skeleton.index.get("ref_fight_00").and_then(|&i| joints.get(ga.joints[i]).ok()).map(|j| j.translation()) else { continue };
        info!("EVENT playtest: {} grabs the nearest grunt {ge:?}", if a.q { "Q" } else { "Jonesy" });
        out.write(Grab { companion: ce, grunt: ge, point });
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
    /// states 0xc..0xe: to the fight point
    Approach { point: Vec3, route: Vec<Vec3> },
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
            .add_systems(Update, (debug_grabs, grab, approach, kill, orphans, log).chain().run_if(crate::playing));
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

/// Whether a grunt can be grabbed now (GruntObject_Slot66's refusals, as the engine models the states).
fn grabbable(g: &Grunt) -> bool {
    matches!(g.kind, Kind::Grunt | Kind::Bow)
        && !matches!(g.mind, Mind::Dead | Mind::Scripted | Mind::Held | Mind::Jump | Mind::Gunner | Mind::InSquad)
        && g.gun.is_none()
        && g.cover.is_none()
        && g.fixed.is_none()
}

/// The heading test of 0x00427e80: the two facings at least 135 degrees apart.
pub fn facing_each_other(yaw_a: f32, yaw_b: f32) -> bool {
    let d = (yaw_a - yaw_b + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
    d.abs().to_degrees() >= FACING
}

/// 0x00427e80's tests for a companion `c` (entity `me_e` at `me`) at his target, up to the grunt's answer; None when
/// any fails. Uses one rand (the coin) once the target qualifies.
#[allow(clippy::type_complexity)]
pub fn attempt(
    c: &Companion,
    me_e: Entity,
    me: Vec3,
    grunts: &Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    joints: &Query<&GlobalTransform, Without<Companion>>,
    rng: &mut FxStats,
) -> Option<Grab> {
    let t = c.target()?;
    let (_, g, gt, ga) = grunts.get(t).ok()?;
    if !grabbable(g) {
        return None;
    }
    // (rand * 100) >> 15 < 50: no
    if rng.rand() < 0.5 {
        return None;
    }
    let gp = gt.translation();
    if me.distance_squared(gp) >= REACH * REACH || !facing_each_other(c.yaw, g.yaw) {
        return None;
    }
    let a = ga?;
    let point = a.skeleton.index.get("ref_fight_00").and_then(|&i| joints.get(a.joints[i]).ok()).map(|j| j.translation())?;
    ((point.y - gp.y).abs() <= HEIGHT).then_some(Grab { companion: me_e, grunt: t, point })
}

/// The grunt's answer (Slot66) and the start of the approach; also the debug grab of a playtest (`force`).
#[allow(clippy::type_complexity)]
fn grab(
    mut commands: Commands,
    mut grabs: MessageReader<Grab>,
    mut companions: Query<(&mut Companion, &Transform), (Without<Killing>, Without<super::carried::Knocked>, Without<super::tower::OnTask>)>,
    mut grunts: Query<&mut Grunt, Without<Victim>>,
    physics: NonSend<crate::physics::Physics>,
    mut rng: ResMut<FxStats>,
    mut stats: ResMut<CloseKills>,
) {
    for gr in grabs.read() {
        let Ok((mut c, t)) = companions.get_mut(gr.companion) else { continue };
        let Ok(mut g) = grunts.get_mut(gr.grunt) else { continue };
        if c.dead.is_some() || !grabbable(&g) {
            continue;
        }
        // the route to the point (0x0046f6b0 must accept it)
        let Some(route) = crate::nav::path(&physics, t.translation, gr.point, &[]).filter(|r| r.last().is_none_or(|w| (*w - gr.point).with_y(0.0).length() <= WAYPOINT)) else { continue };
        g.mind = Mind::Held;
        g.walking = false;
        let anims = pair(c.q, rng.rand());
        c.engage(gr.grunt, &mut rng);
        c.moving = true;
        c.firing = false;
        commands.entity(gr.grunt).insert(Victim { by: gr.companion });
        commands.entity(gr.companion).insert(Killing { grunt: gr.grunt, phase: Phase::Approach { point: gr.point, route }, anims, t: 0.0 });
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
        let Phase::Approach { point, route } = &mut kl.phase else { continue };
        let point = *point;
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
            c.walk(&mut physics, &mut t, dir, speed, dt);
            continue;
        }
        // at the point: turn to the grunt, then the pair's IC anims from where each stands
        c.moving = false;
        let face = (gp - me).with_y(0.0).normalize_or_zero();
        let want = face.x.atan2(face.z);
        let d = (want - c.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
        let step = TURN_RATE.to_radians() * dt;
        c.yaw += d.clamp(-step, step);
        t.rotation = Quat::from_rotation_y(c.yaw) * c.base;
        if d.abs() > step {
            continue;
        }
        let axes = Mat4::from_quat(crate::level::game_to_engine().inverse());
        let origin = |tr: &Transform| axes * Mat4::from_rotation_translation(tr.rotation, tr.translation);
        let mut mine = Mover::new(kl.anims.0, origin(&t));
        mine.generic = true;
        commands.entity(e).insert(mine);
        commands.entity(grunt).insert(Mover::new(kl.anims.1, origin(gt)));
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
    commands.entity(grunt).remove::<(Victim, Mover)>();
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
    mut notes: MessageReader<MoverNote>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut q: Query<(Entity, &Killing, &mut Companion, &Transform, Option<&Mover>, Option<&mut crate::skin::Animator>), Without<Grunt>>,
    mut grunts: Query<(&mut Grunt, &Transform, Option<&crate::physics::Body>, &mut Health, Option<&Mover>, Option<&mut crate::skin::Animator>), (Without<Companion>, With<Victim>)>,
    mut stats: ResMut<CloseKills>,
) {
    let k = physics.meters_per_unit;
    let killed: Vec<Entity> = notes.read().filter(|n| n.text.split_whitespace().next().is_some_and(|w| w.eq_ignore_ascii_case("kill"))).map(|n| n.entity).collect();
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
        if let Ok((mut g, gt, body, mut h, gm, ga)) = grunts.get_mut(grunt) {
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
            if killed.contains(&grunt) && h.0 > 0.0 {
                // the "kill" note: dead where the anim leaves him (no ragdoll, no other death anim: ai.rs die)
                h.0 = 0.0;
                g.last_health = 0.0;
                g.last_hit = 0.0;
                g.attacker = Some(e);
                g.death_name = Some(kl.anims.1);
                stats.kills += 1;
                info!("EVENT companions: grunt {grunt:?} killed by {}", if c.q { "Q" } else { "Jonesy" });
            }
        }
        if mover.is_none_or(|m| m.ended) {
            // state 0x32 after the IC anim: he stands where it left him, back to his update (his target dead: the
            // fight's end, ai.rs)
            c.ic = false;
            c.yaw = super::ic::yaw_of(t.rotation);
            physics.place_walker(&c.walker, t.translation);
            commands.entity(e).remove::<(Killing, Mover)>();
            commands.entity(grunt).remove::<Victim>();
            if let Ok((mut g, ..)) = grunts.get_mut(grunt) {
                if g.mind == Mind::Scripted {
                    // (the anim over without its kill: he stands where it left him)
                    g.mind = Mind::Roam;
                    commands.entity(grunt).remove::<Mover>();
                }
            }
        }
    }
}

/// A held grunt whose companion left the kill another way (stunned: carried.rs; removed) is let go (state 0xf's test).
fn orphans(mut commands: Commands, mut grunts: Query<(Entity, &Victim, &mut Grunt)>, holders: Query<(), With<Killing>>) {
    for (e, v, mut g) in &mut grunts {
        if holders.contains(v.by) {
            continue;
        }
        commands.entity(e).remove::<(Victim, Mover)>();
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
