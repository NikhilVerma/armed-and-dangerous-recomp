//! The companions' hand-to-hand takedown (states 0xc..0xe and the paired IC anims): Q or Jonesy grabs a grunt in front
//! of him and kills him with a kung-fu move. docs/subsystems/companions.md, docs/subsystems/grunt-ai.md section 15.
//!
//! - Who: a companion (vt+0x1e4 true only for Q111Object and MolmerObject) against a grunt or an archer (vt+0x104 true
//!   only for GruntObject and BowGruntObject).
//! - When (0x00427e80, ai_02.cpp:1329-1418): tried at a burst's end that is not the last and at a clip's end (the fire
//!   cycle 0x00428980, ai_02.cpp:1942 / 1988), first thing in the engage (0x00427910) and in the attack order
//!   (0x00484100). It needs a live target, the companion not already fighting (0xc..0xe), +0x484 0x2000000 clear, a
//!   50 % roll ((rand() x 100) >> 15 >= 50), the target within 50 (3D, K 0x005bccb8 = 2500 squared), the two headings
//!   apart by 135 degrees or more (K 0x005bcea8 / 0x005bceac: facing each other within 45), the target's ref_fight_00
//!   (its point, the facing minus its column 1) within 0.5 of the target's height (K 0x005bcb4c), a route there
//!   (0x0046f6b0); and the grunt's consent (vt+0x108: GruntObject_Slot66 0x00434ea0 / BowGruntObject_Slot66 0x004346a0:
//!   not dead, not in a trench or on a gun, not a window / tower grunt, not held, not in the air): he stops, holds still
//!   in his stand (state 0xf: until his partner lets go), +0x484 0x2000.
//! - The approach (states 0xc / 0xd / 0xe, ai_06.cpp:926-994): along the route to the point, then a push onto it (2D
//!   within 1, K 0x005bcb2c), turned to the facing (0x00454e60), then the pairing.
//! - The pairing (ai_06.cpp:986-993): a selector from the companion's vt+0x138 (Q111Object_Slot78 0x00487040: 0x81 /
//!   0x82; MolmerObject 0x0044ea40: 0x7f / 0x80, even odds), both played as IC anims from where they stand
//!   (0x0042c670 -> GenericAI_PlayICAnim 0x0042c410: physics off, the root track carries them): Q mover_q_kill_grunt_1
//!   / _2 with mover_grunt_qdie_1 / _2 (3.13 / 5.97 s), Jonesy mover_molmer_kill_grunt_1 / _2 with mover_grunt_mdie_1 /
//!   _2 (2.8 / 6.8 s). The grunt's anim ends in "kill" (3.10 / 5.93 / 2.77 / 6.77 s): his death (ai/notes.rs). The
//!   companion's end (its wake 0x0042c0e0) drops the partner as target and goes to state 0x32 (his reload).
//! - A companion that dies in it wakes the grunt (Character_Die -> vt+0x1ac(1) -> the partner's wake): state 0x65, the
//!   stand, state 1. A grunt that dies before the pairing lets the companion go (ai_06.cpp:995-1019).
//!
//! The grunts' own close attack (`CloseAttack`, state 0xb): a grunt, an archer or a Q112 (vt+0x1dc) whose target is
//! within his hand-to-hand reach m_h2hMin (+0x5fc: 20, the Q112 25; GenericAIObject_Construct game_02.cpp:1638 /
//! Q112Object_Slot6 ai_04.cpp:2369) at a clip's end (0x00428980 -> 0x00428160) swings at it: anim 0x5f / 0x60 (even
//! odds; grunt_bow_hand_to_hand / _2, the Q112's q112_gun_hit) looped, facing it; each "hit" note (0x0043d180 ->
//! 0x0043eae0) hurts the target within m_h2hMax (+0x600: 10, the Q112 35) across: 50 (the Q112: 100, by its name),
//! impulse 6500 / 13000 along the way to it, 0.7..0.9 up. At each loop's end (ai_06.cpp:889-925) a target that has
//! got m_h2hMax away or more ends it: in a trench the duck, else the fire state.

use super::{Companion, Grunt, Kind, Mind};
use crate::mover::Mover;
use bevy::prelude::*;

/// 0x00427e80's limits: the target within 50 (3D), the headings apart by 135 degrees or more.
pub const REACH: f32 = 50.0;
pub const FACING: f32 = 135.0;

/// A companion's fight: its grunt and the phase.
#[derive(Component, Clone, Debug)]
pub struct Fighting {
    pub grunt: Entity,
    /// the point beside the grunt and the facing there (ref_fight_00)
    pub point: Vec3,
    pub facing: Vec3,
    /// states 0xc..0xe: the time on the way (the engine gives up after APPROACH_LIMIT)
    pub approach: f32,
    pub paired: bool,
}

/// A grunt held by a companion's fight (state 0xf, then his half of the paired anim).
#[derive(Component, Clone, Copy, Debug)]
pub struct Fought {
    pub by: Entity,
}

/// The engine's limit on the approach (the original has none: a companion stuck on the way would hold the grunt for
/// good).
const APPROACH_LIMIT: f32 = 8.0;

/// Fights started, paired, grunts killed (playtest log `melee`).
#[derive(Resource, Default, Debug)]
pub struct MeleeStats {
    pub tries: u32,
    pub started: u32,
    pub paired: u32,
    pub kills: u32,
}

/// The paired anims of a selector (the companion's, the grunt's).
pub fn anims(selector: u32) -> (&'static str, &'static str) {
    match selector {
        0x81 => ("mover_q_kill_grunt_1", "mover_grunt_qdie_1"),
        0x82 => ("mover_q_kill_grunt_2", "mover_grunt_qdie_2"),
        0x7f => ("mover_molmer_kill_grunt_1", "mover_grunt_mdie_1"),
        _ => ("mover_molmer_kill_grunt_2", "mover_grunt_mdie_2"),
    }
}

/// The selector (Q111Object_Slot78 0x00487040 / MolmerObject vt+0x138 0x0044ea40): even odds of the two.
pub fn selector(q: bool, r: f32) -> u32 {
    match (q, r < 0.5) {
        (true, true) => 0x81,
        (true, false) => 0x82,
        (false, true) => 0x7f,
        (false, false) => 0x80,
    }
}

/// The signed angle (degrees) from heading `a` to heading `b` (flat unit vectors).
fn heading_diff(a: Vec3, b: Vec3) -> f32 {
    a.cross(b).y.atan2(a.dot(b)).to_degrees()
}

/// 0x00427e80's geometry: within reach and facing each other.
pub fn can_reach(me: Vec3, my_fwd: Vec3, them: Vec3, their_fwd: Vec3) -> bool {
    me.distance_squared(them) < REACH * REACH && heading_diff(their_fwd, my_fwd).abs() >= FACING
}

/// The grunt's consent (GruntObject_Slot66 0x00434ea0).
fn consents(g: &Grunt) -> bool {
    matches!(g.kind, Kind::Grunt | Kind::Bow)
        && !matches!(g.mind, Mind::Dead | Mind::Scripted | Mind::Falling | Mind::Jump | Mind::Cover | Mind::Gunner | Mind::Flee)
        && g.cover.is_none()
        && g.gun.is_none()
        && !g.window
        && g.fixed.is_none()
}

fn game_matrix(t: &Transform) -> Mat4 {
    Mat4::from_quat(crate::level::game_to_engine()).inverse() * t.to_matrix()
}

/// The tries (a companion's burst or clip end, `Companion::fight_try`) and the start.
#[allow(clippy::type_complexity)]
pub fn start(
    mut commands: Commands,
    mut companions: Query<(Entity, &mut Companion, &Transform), Without<Fighting>>,
    mut grunts: Query<(&mut Grunt, &Transform, Option<&crate::skin::Animator>), (Without<Fought>, Without<Companion>)>,
    joints: Query<&GlobalTransform>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut force: ResMut<super::AiForce>,
    mut stats: ResMut<MeleeStats>,
) {
    for (ce, mut c, ct) in &mut companions {
        if !std::mem::take(&mut c.fight_try) || c.dead.is_some() {
            continue;
        }
        let Some(te) = c.target else { continue };
        let Ok((mut g, gt, ga)) = grunts.get_mut(te) else { continue };
        if !consents(&g) {
            continue;
        }
        stats.tries += 1;
        // the 50 % roll ((rand() x 100) >> 15 < 50 fails), or forced (playtest)
        if !(force.take("fight") || rng.rand() >= 0.5) {
            continue;
        }
        let my_fwd = Vec3::new(c.yaw.sin(), 0.0, c.yaw.cos());
        let their_fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
        if !can_reach(ct.translation, my_fwd, gt.translation, their_fwd) {
            debug!("melee: {ce:?} out of reach of {te:?}: {:.0} apart, headings {:.0}", ct.translation.distance(gt.translation), heading_diff(their_fwd, my_fwd));
            continue;
        }
        // the target's ref_fight_00: the point and the facing (minus its y axis), its height within 0.5 of his
        let Some(j) = ga.and_then(|a| a.skeleton.index.get("ref_fight_00").and_then(|&i| joints.get(a.joints[i]).ok())) else {
            debug!("melee: {te:?} has no ref_fight_00");
            continue;
        };
        let point = j.translation();
        if (point.y - gt.translation.y).abs() > 0.5 {
            debug!("melee: {te:?}'s ref_fight_00 {:.1} off his height", point.y - gt.translation.y);
            continue;
        }
        let facing = (-Vec3::from(j.affine().matrix3.y_axis)).with_y(0.0).normalize_or(-their_fwd);
        // his consent: stopped, holding still in his stand (state 0xf)
        g.mind = Mind::Scripted;
        g.target = None;
        commands.entity(te).insert(Fought { by: ce });
        commands.entity(ce).insert(Fighting { grunt: te, point, facing, approach: 0.0, paired: false });
        c.moving = false;
        c.firing = false;
        stats.started += 1;
        info!("EVENT ai: companion {ce:?} goes for grunt {te:?} hand to hand");
    }
}

/// The approach, the pairing and the ends.
#[allow(clippy::type_complexity)]
pub fn run(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut companions: Query<(Entity, &mut Companion, &mut Transform, &mut Fighting, Option<&Mover>, Option<&mut crate::skin::Animator>)>,
    mut grunts: Query<(Entity, &mut Grunt, &crate::physics::Body, &Transform, Option<&Mover>, Option<&mut crate::skin::Animator>, &Fought), Without<Companion>>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut stats: ResMut<MeleeStats>,
) {
    let dt = time.delta_secs();
    let k = physics.meters_per_unit;
    // the grunts first: his partner gone or dead wakes him (state 0x65: the stand, state 1); else held where he is
    for (ge, mut g, body, gt, mover, animator, f) in &mut grunts {
        let partner = companions.get(f.by).ok().filter(|(_, c, _, fi, ..)| c.dead.is_none() && fi.grunt == ge);
        if g.mind == Mind::Dead {
            commands.entity(ge).remove::<(Fought, Mover)>();
            continue;
        }
        if partner.is_none() {
            commands.entity(ge).remove::<(Fought, Mover)>();
            g.mind = Mind::Roam;
            g.home = gt.translation;
            g.goal = gt.translation;
            info!("EVENT ai: grunt {ge:?} let go (hand to hand)");
            continue;
        }
        if let Ok(mut b) = physics.world.body_mut(body.id) {
            let _ = b.set_position(crate::physics::jr((gt.translation + body.offset) * k), oxijolt::prelude::Activation::Activate);
            let _ = b.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
        }
        if let Some(mut a) = animator {
            match mover {
                // his half of the paired anim on the whole skeleton at the mover's time
                Some(m) => {
                    if a.layers.first().is_none_or(|l| l.anim != m.anim) {
                        a.play_at(0, &m.anim, false, crate::skin::Mask::All, m.t);
                        a.stop(1);
                    }
                    if let Some(l) = a.layers.get_mut(0) {
                        l.time = m.t;
                        l.speed = 0.0;
                    }
                }
                // state 0xf: the stand
                None => {
                    a.play(0, g.anim(2, 0.0), true, crate::skin::Mask::All);
                    a.stop(1);
                }
            }
        }
    }
    for (ce, mut c, mut t, mut f, mover, animator) in &mut companions {
        let grunt_ok = grunts.get(f.grunt).is_ok_and(|(_, g, ..)| g.mind != Mind::Dead);
        if c.dead.is_some() || (!grunt_ok && !f.paired) {
            // dead (the grunt wakes above) or the grunt gone before the pairing: let go (ai_06.cpp:995-1019)
            commands.entity(ce).remove::<(Fighting, Mover)>();
            c.target = None;
            c.engaged = false;
            continue;
        }
        if f.paired {
            // the paired IC anim carries him (physics off); its end wakes him into his reload (state 0x32)
            physics.place_walker(&c.walker, t.translation);
            if let (Some(m), Some(mut a)) = (mover, animator) {
                if a.layers.first().is_none_or(|l| l.anim != m.anim) {
                    a.play_at(0, &m.anim, false, crate::skin::Mask::All, m.t);
                    a.stop(1);
                }
                if let Some(l) = a.layers.get_mut(0) {
                    l.time = m.t;
                    l.speed = 0.0;
                }
            }
            if mover.is_none_or(|m| m.ended) {
                commands.entity(ce).remove::<(Fighting, Mover)>();
                c.yaw = super::ic::yaw_of(t.rotation);
                t.rotation = Quat::from_rotation_y(c.yaw) * c.base;
                c.target = None;
                c.engaged = false;
                c.reloading = if c.q { 1.967 } else { 3.467 };
                info!("EVENT ai: companion {ce:?} done hand to hand");
            }
            continue;
        }
        // states 0xc..0xe: to the point, then turned to the facing
        f.approach += dt;
        let to = (f.point - t.translation).with_y(0.0);
        let speed = c.run_speed();
        if to.length() > 1.0 && f.approach < APPROACH_LIMIT {
            let dir = to.normalize_or_zero();
            c.yaw = dir.x.atan2(dir.z);
            t.rotation = Quat::from_rotation_y(c.yaw) * c.base;
            let step = speed.min(to.length() / dt.max(1e-4));
            c.walk(&mut physics, &mut t, dir, step, dt);
            if let Some(mut a) = animator {
                let who = if c.q { "q111" } else { "molmer" };
                a.play(0, &format!("{who}_run"), true, crate::skin::Mask::All);
                a.stop(1);
            }
            continue;
        }
        if to.length() > 1.0 {
            // the engine's give-up (see APPROACH_LIMIT): the grunt wakes
            commands.entity(ce).remove::<Fighting>();
            continue;
        }
        c.yaw = f.facing.x.atan2(f.facing.z);
        t.rotation = Quat::from_rotation_y(c.yaw) * c.base;
        let sel = selector(c.q, rng.rand());
        let (mine, theirs) = anims(sel);
        commands.entity(ce).insert(Mover::new(mine, game_matrix(&t)));
        if let Ok((ge, _, _, gt, ..)) = grunts.get(f.grunt) {
            commands.entity(ge).insert(Mover::new(theirs, game_matrix(gt)));
        }
        f.paired = true;
        stats.paired += 1;
        info!("EVENT ai: companion {ce:?} takes grunt {:?} down ({mine} / {theirs})", f.grunt);
    }
}

/// A grunt's hand-to-hand reach (vt+0x1dc and +0x5fc / +0x600): (swing within, hits within).
pub fn h2h(kind: Kind) -> Option<(f32, f32)> {
    match kind {
        Kind::Grunt | Kind::Bow => Some((20.0, 10.0)),
        Kind::Q112 => Some((25.0, 35.0)),
        _ => None,
    }
}

/// 0x00428160: swing at a target within reach (3D): the anim id (0x5f / 0x60 at even odds).
pub fn close_attack(kind: Kind, me: Vec3, target: Vec3, r: f32) -> Option<u32> {
    let (min, _) = h2h(kind)?;
    (me.distance_squared(target) < min * min).then_some(if r < 0.5 { 0x5f } else { 0x60 })
}

/// The "hit" notes of the swing anims (grunt_bow_hand_to_hand 0.367; _2 0.367 and 0.9; the Q112's q112_gun_hit: guess,
/// the same as the first).
pub fn hit_notes(id: u32) -> &'static [f32] {
    if id == 0x60 { &[0.367, 0.9] } else { &[0.367] }
}

/// One "hit" (0x0043eae0) of a close attack, applied by `close_hits`.
#[derive(Message, Clone, Copy, Debug)]
pub struct CloseHit {
    pub by: Entity,
    pub target: Entity,
    pub damage: f32,
}

/// Swings landed (playtest log).
#[derive(Resource, Default, Debug)]
pub struct CloseStats {
    pub swings: u32,
    pub hits: u32,
}

/// The hits' damage to Roman, a companion or another object with health (the damage record 0x40000 with the AI as
/// source; Roman's knock-back from the impulse is not modelled).
pub fn close_hits(mut hits: MessageReader<CloseHit>, mut health: Query<&mut crate::fx::Health>, mut companions: Query<&mut Companion>, mut stats: ResMut<CloseStats>) {
    for h in hits.read() {
        if let Ok(mut hp) = health.get_mut(h.target) {
            hp.0 -= h.damage;
            stats.hits += 1;
            if let Ok(mut c) = companions.get_mut(h.target) {
                c.attacker = Some(h.by);
            }
        }
    }
}

/// Counts the takedowns' kills (the grunt's "kill" note: ai/notes.rs).
pub fn count_kills(mut ends: MessageReader<super::notes::ScriptEnd>, fought: Query<(), With<Fought>>, mut stats: ResMut<MeleeStats>) {
    for e in ends.read() {
        if e.kill && fought.contains(e.entity) {
            stats.kills += 1;
        }
    }
}

/// The playtest state's `melee`.
pub fn log(stats: Res<MeleeStats>, close: Res<CloseStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert(
        "melee".into(),
        serde_json::json!({"tries": stats.tries, "started": stats.started, "paired": stats.paired, "kills": stats.kills, "swings": close.swings, "close_hits": close.hits}),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn facing_each_other_within_reach() {
        let me = Vec3::ZERO;
        let them = Vec3::new(0.0, 0.0, 30.0);
        // facing each other: in
        assert!(can_reach(me, Vec3::Z, them, -Vec3::Z));
        // 40 degrees off still in (135 apart or more)
        let off = Quat::from_rotation_y(40f32.to_radians()) * -Vec3::Z;
        assert!(can_reach(me, Vec3::Z, them, off));
        // side by side: out; too far: out
        assert!(!can_reach(me, Vec3::Z, them, Vec3::Z));
        assert!(!can_reach(me, Vec3::Z, Vec3::new(0.0, 0.0, 60.0), -Vec3::Z));
    }

    #[test]
    fn selectors_pair_the_anims() {
        assert_eq!(anims(selector(true, 0.2)), ("mover_q_kill_grunt_1", "mover_grunt_qdie_1"));
        assert_eq!(anims(selector(false, 0.7)), ("mover_molmer_kill_grunt_2", "mover_grunt_mdie_2"));
    }
}
