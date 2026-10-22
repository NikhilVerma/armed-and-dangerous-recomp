//! Squads (SquadObject, vtable 0x005ab3e8): a moving anchor that carries captains and grunts (and a goliath) in a
//! formation along a marker spline, halts when it meets the player, and sends some of its members to charge.
//! docs/subsystems/infantry.md section 4.
//! - Load (Squad_Init 0x00433220, ai_04.cpp:1318-1464): `Squad::from_props`; the formation is the template's
//!   `<template>_ref` anim (static bone offsets ref_captain_0N / ref_regular_0N / ref_grunt_0N / ref_goliath_00).
//! - The trigger (SquadObject_Slot14 0x00433a10): the spline run (0x0042d1b0), members follow their bones (state 0xdc,
//!   GenericAI_Update ai_06.cpp:3062-3089: `member_goal`).
//! - The squad's own target scan (0x0042be80 in states 0x96 / 0x97: AI_FindTarget over the player and his companions,
//!   `find_target`) and its alert (SquadObject_Slot143 0x00433c70, `alert`): halt (state 0x98) for resume_delay,
//!   charge_count members leave and charge every charge_delay, the group hears, the squads of its squad_tag halt too.
//! - Update (SquadObject_Update 0x00446ba0, ai_07.cpp:384-453): the halt timer re-armed while a member still fights
//!   (passed on to the tag's squads, no charge); the goliath dead -> every member charges; nothing left -> the squad
//!   is gone. (Its guard 0x0044a600 on the level record Game+0xa64 holds while a level loads: here the Playing state.)
//! - The goliath (+0xb7c, at ref_goliath_00): made through goliath.rs (`SpawnGoliath`), which follows the squad as a
//!   member; it takes over the squad's receive_trigger_kill / send_trigger_on_death / tag (the squad keeps none).

use super::{Grunt, Mind, Player};
use crate::fx::{FxStats, Health};
use crate::physics::Physics;
use bevy::prelude::*;

/// The squad's states (+0x460).
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum SquadPhase {
    /// 0x8c: waiting for receive_trigger_activate (members idle, 0xde)
    Waiting,
    /// 0x96 / 0x97: on the marker spline (members follow, 0xdc)
    Moving,
    /// 0x98: halted by a target (members fight in place, 0xdd)
    Halted,
    /// 1: the end of a non-looping spline
    Done,
}

/// A member's place in its squad (+0x9bc the squad, +0x9c0 the bone: here its offset in the squad's frame).
#[derive(Clone, Copy, Debug)]
pub struct SquadLink {
    pub squad: Entity,
    pub offset: Vec3,
}

#[derive(Component)]
pub struct Squad {
    pub phase: SquadPhase,
    /// the anchor: its position (engine space) and heading
    pub pos: Vec3,
    pub yaw: f32,
    /// receive_trigger_activate +0x52c, marker_tag_min / max +0x558 / +0x55c, marker_looping
    pub trigger: i64,
    pub tags: (i64, i64),
    pub looping: bool,
    /// the spline run: its tag and the marker index
    pub run: Option<(i64, usize)>,
    /// the speed on a spline of markers without their own speed (fn_004297a0 in states 0x96 / 0x97: the marker's speed
    /// 0x0044b4f0, else patrol_speed +0x954 with the patrol flag 1 of +0x488, else move_speed +0x948); the flag is set
    /// by marker_patrol and by marker_strict_patrol (Ai_InitGeneric ai_01.cpp:2517-2528). Oracle (ticker_l3m1): the
    /// strict-patrol goliath squads of lev_3_m1 walk at about 9 units / s, their patrol_speed 10 (move_speed 40)
    pub speed: f32,
    /// the speed now: the original's anchor is a body pushed toward `speed` (Squad_Init's mass setting 0x00458270), so
    /// it gathers pace from a halt (`PACE_TIME`)
    pub pace: f32,
    /// the spline run's route planning before the first step (state 0x96, ai.rs ROUTE_PLAN)
    pub route_wait: f32,
    /// scan_distance +0x5dc (default 300): the squad's own target scan
    pub scan: f32,
    /// hear_distance +0x5e0 (Ai_InitGeneric's default 20, ai_01.cpp:2391; no squad sets it): the player this close is
    /// taken at once, and seen all round
    pub hear: f32,
    scan_timer: f32,
    /// group id +0x574 = -squad_tag: the members' alert group
    pub group: i64,
    /// resume_delay, charge_delay (floats), charge_count (ints)
    pub resume: (f32, f32),
    pub charge_delay: (f32, f32),
    pub charge_count: (i64, i64),
    /// +0x490 the halt timer, +0x494 the charge timer, +0x4ac the state to resume
    pub halt: f32,
    pub charge: f32,
    resume_phase: SquadPhase,
    pub target: Option<Entity>,
    /// +0xb78 the members, their health last frame (a hit or a death alerts the squad)
    pub members: Vec<Entity>,
    health: std::collections::HashMap<Entity, f32>,
    /// +0xb7c the goliath (made at `goliath_slot` on the first frame by goliath.rs; the slot is then cleared), with a cage and peasant at its
    /// chest when "goliath_cage" is set
    pub goliath: Option<Entity>,
    pub goliath_slot: Option<Vec3>,
    pub goliath_cage: bool,
    /// the squad's receive_trigger_kill (+0x530), send_trigger_on_death (+0x528) and tag (+0x390): the goliath's
    /// (0x0042de60 a1..a3); Squad_Init clears its own afterwards, with or without a goliath
    pub handed_over: (i64, i64, i64),
    /// the members still to make: (template, offset), made on the first frame (spawn_grunts links them)
    pub pending: Vec<(&'static str, Vec3)>,
    /// squad_tag +0xb58 and squad_sequence +0xb5c (0x00433b90 / 0x00433b80): squads of one tag share a halt down the
    /// sequence (0x00433bc0)
    pub squad_tag: i64,
    pub sequence: i64,
}

impl Squad {
    /// Squad_Init 0x00433220 over the property set and the formation anim `<template>_ref` of the level `dir`.
    pub fn from_props(p: &crate::gamedata::Props, dir: &std::path::Path, template: &str, pos: Vec3, yaw: f32) -> Squad {
        let anim = format!("{template}_ref");
        let bone = |n: &str| crate::mission::pub_bone(dir, &anim, n).map(|v| crate::level::game_to_engine() * v);
        let mut pending = Vec::new();
        // ref_grunt_0N "grunt", ref_regular_0N "grunt_regular", ref_captain_0N "captain": each list stops at its first
        // missing index
        for (prefix, tpl) in [("ref_grunt_", "grunt"), ("ref_regular_", "grunt_regular"), ("ref_captain_", "captain")] {
            for i in 0.. {
                let Some(o) = bone(&format!("{prefix}{i:02}")) else { break };
                pending.push((tpl, o));
            }
        }
        let tag = p.i("squad_tag", 0);
        Squad {
            phase: SquadPhase::Waiting,
            pos,
            yaw,
            trigger: p.i("receive_trigger_activate", 0),
            tags: (p.i("marker_tag_min", 0), p.i("marker_tag_max", 0)),
            looping: p.i("marker_looping", 0) != 0,
            run: None,
            speed: if p.i("marker_patrol", 0) != 0 || p.i("marker_strict_patrol", 0) != 0 { p.f("patrol_speed", 10.0) } else { p.f("move_speed", 40.0) },
            pace: 0.0,
            route_wait: 0.0,
            scan: p.f("scan_distance", 300.0),
            hear: p.f("hear_distance", 20.0),
            scan_timer: 0.0,
            // (0 would be no group: the original logs and uses 0xfffe7e53)
            group: if tag != 0 { -tag } else { -0x181ad },
            resume: (p.f("resume_delay_min", 10.0), p.f("resume_delay_max", 20.0)),
            charge_delay: (p.f("charge_delay_min", 5.0), p.f("charge_delay_max", 10.0)),
            charge_count: (p.i("charge_count_min", 1), p.i("charge_count_max", 1)),
            halt: 0.0,
            charge: 0.0,
            resume_phase: SquadPhase::Waiting,
            target: None,
            members: Vec::new(),
            health: default(),
            goliath: None,
            goliath_slot: bone("ref_goliath_00"),
            goliath_cage: p.i("goliath_cage", 0) != 0,
            handed_over: (p.i("receive_trigger_kill", 0), p.i("send_trigger_on_death", 0), p.i("tag", 0)),
            pending,
            squad_tag: tag,
            sequence: p.i("squad_sequence", 0),
        }
    }

    /// Where a member's bone is now (fn_00453c50 on the squad's matrix).
    pub fn point(&self, offset: Vec3) -> Vec3 {
        self.pos + Quat::from_rotation_y(self.yaw) * offset
    }

    /// Members are busy (0x0042f8d0: states 0xdc / 0xde) unless the squad is halted.
    pub fn members_engage(&self) -> bool {
        self.phase == SquadPhase::Halted
    }
}

/// Seconds the anchor takes to gather ~63 % of its speed from a halt: fitted to the original (oracle lev_bonus_m3: the
/// squad's members' centre at 0.5, 1.4, 3.9, 5.5, 6.9, 7.8 .. 9.3 units / s over its first 3 s at patrol_speed 10; the
/// force model behind it is not traced).
pub const PACE_TIME: f32 = 1.3;

/// The member's goal in state 0xdc: its bone, at most twice its own move_speed (0x00471970).
pub fn member_goal(sq: &Squad, link: SquadLink, pos: Vec3, move_speed: f32) -> (Vec3, f32) {
    let at = sq.point(link.offset);
    let d = (at - pos).with_y(0.0).length();
    // (the original steers by force toward the point; here the speed eases off within a few units)
    (at, (d * 4.0).min(2.0 * move_speed))
}

/// A halt passed on to the other squads of the tag (0x00433bc0): from which squad, its tag and sequence, the target
/// (none when the re-arm's fighting member is in state 10 without one: the receivers then drop theirs) and the
/// sender's halt timer.
#[derive(Clone, Copy, Debug)]
struct Share {
    from: Entity,
    tag: i64,
    sequence: i64,
    target: Option<Entity>,
    halt: f32,
}

impl Share {
    fn from(se: Entity, sq: &Squad, target: Option<Entity>) -> Self {
        Share { from: se, tag: sq.squad_tag, sequence: sq.sequence, target, halt: sq.halt }
    }
}

/// 0x00433bc0's choice: another squad of the same squad_tag whose squad_sequence is not below the sender's (signed).
/// Tag 0 (no squad_tag) is a tag like any other: the original has no test for it (no shipped level has two squads
/// of one tag, so no halt is ever shared in the shipped data).
pub fn shares_halt(from: (i64, i64), other: (i64, i64)) -> bool {
    other.0 == from.0 && other.1 >= from.1
}

/// The members a charge sends: charge_count_min + (rand * (max - min + 1)) >> 15 (ai_04.cpp:1672), `r` in [0, 1).
fn charge_count(range: (i64, i64), r: f32) -> usize {
    let span = (range.1 - range.0 + 1) as f32;
    (range.0 + (r * span).floor() as i64).max(0) as usize
}

/// The grunts as the squads see them (their members).
type Members<'w, 's> = Query<'w, 's, (Entity, &'static mut Grunt, &'static Health)>;

/// What the alert works on besides the squad: the members, the random numbers, the stats and the halts to pass on.
struct AlertCx<'a, 'w, 's> {
    grunts: &'a mut Members<'w, 's>,
    rng: &'a mut FxStats,
    stats: &'a mut super::AiStats,
    shares: &'a mut Vec<Share>,
    /// the Guy Fawkes traitors (+0x488 0x4000), whom a group alert passes over
    traitors: &'a Query<'a, 'a, (), With<crate::weapons::traitor::Traitor>>,
}

/// SquadObject_Slot143 0x00433c70 (ai_04.cpp:1640-1706) with the pass-on flag: a resume delay is rolled and only when
/// it outlasts the halt timer does anything but the target change: the timer takes it, with the flag the tag's
/// squads hear (0x00433bc0, after the frame: `Share`), the squad halts (state 0x98, its charge timer cleared so a new
/// halt may charge at once) and, with the flag and the charge timer run out, charge_count members charge. The base
/// SetTarget (0x0042cc70) always: the target, and with the flag the group (+0x574) told.
fn alert(sq: &mut Squad, se: Entity, target: Option<Entity>, pass_on: bool, cx: &mut AlertCx) {
    let t = cx.rng.range(sq.resume.0, sq.resume.1);
    if t > sq.halt {
        sq.halt = t;
        if pass_on {
            cx.shares.push(Share::from(se, sq, target));
        }
        if sq.phase != SquadPhase::Halted {
            sq.resume_phase = sq.phase;
            sq.phase = SquadPhase::Halted;
            sq.charge = 0.0;
            cx.stats.squad_halts += 1;
            info!("EVENT ai: squad halts ({} members)", sq.members.len());
        }
        if let (true, true, Some(target)) = (pass_on, sq.charge <= 0.0, target) {
            charge(sq, target, cx);
        }
    }
    set_target(sq, se, target, pass_on, cx);
}

/// The charge (ai_04.cpp:1667-1703): dead members met are unlinked, then live ones leave the squad and attack the
/// target until `charge_count` have gone; the charge timer restarts (charge_delay).
fn charge(sq: &mut Squad, target: Entity, cx: &mut AlertCx) {
    let n = charge_count(sq.charge_count, cx.rng.rand());
    let mut sent = 0;
    let (grunts, rng) = (&mut *cx.grunts, &mut *cx.rng);
    sq.members.retain(|&m| {
        let Ok((_, mut g, h)) = grunts.get_mut(m) else { return false };
        if h.0 <= 0.0 || g.mind == Mind::Dead {
            return false;
        }
        if sent >= n {
            return true;
        }
        // 0x0042cc10 (the squad and the group released), 0x0042cff0 (on the target), 0x0042ce70 (the charge: ai.rs
        // `charge`, which keeps the target it is given and marks him charged, +0x958)
        g.squad = None;
        g.alert_group = 0;
        g.guard = false;
        g.target = Some(target);
        g.mind = super::charge(&mut g, None, rng);
        sent += 1;
        false
    });
    cx.stats.squad_charged += sent;
    if sent > 0 {
        info!("EVENT ai: squad sends {sent} to charge");
    }
    sq.charge = cx.rng.range(sq.charge_delay.0, sq.charge_delay.1);
}

/// The base SetTarget 0x0042cc70 (ecx = squad; target, flag): the squad's target; with the flag every AI of the
/// squad's group (+0x574, the members' alert group) takes part in the alert as a grunt's search tells its group
/// (0x0042c700, ai.rs `alerted`: whether he holds a target does not matter) and takes the target after 0.25..1 s.
fn set_target(sq: &mut Squad, se: Entity, target: Option<Entity>, tell_group: bool, cx: &mut AlertCx) {
    sq.target = target;
    let Some(target) = target.filter(|_| tell_group) else { return };
    for (ge, mut g, _) in cx.grunts.iter_mut() {
        if super::alerted(&g, ge, (sq.group, target, se), cx.traitors.contains(ge)) {
            g.pending = Some((target, cx.rng.range(0.25, 1.0)));
        }
    }
}

/// The squad's own target scan (0x0042be80 in states 0x96 / 0x97): AI_FindTarget 0x00426920 (ai.rs `find_target`)
/// from the anchor over its scan_distance, without a model (its own ref_hit_00 is the anchor), the gun and the
/// barricade (no squad has either).
fn find_target(sq: &Squad, cands: &[super::TargetCandidate], heard: bool, tagged: bool, physics: &Physics) -> Option<Entity> {
    let looker = super::Looker {
        pos: sq.pos,
        eye: sq.pos,
        fwd: Vec3::new(sq.yaw.sin(), 0.0, sq.yaw.cos()),
        scan: sq.scan,
        hear: sq.hear,
        at_barricade: false,
        gun: None,
        // (its sight passes through the AI characters, its own goliath among them: sight_ray as 0x00426fd0, which
        // excludes the AI list World+0xe7c; a plain ray from the anchor started inside the goliath's hit capsule and
        // never saw Roman beside it: oracle ticker_l3m1, the original's squad halts 16 frames after Roman appears there)
        skip: &[],
        heard,
        tagged,
    };
    super::find_target(&looker, cands, physics).map(|b| b.1)
}

/// The squads' frame: members made, the trigger, the spline, the scan, the halt and the charges.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
pub fn update_squads(
    mut commands: Commands,
    time: Res<Time>,
    mut squads: Query<(Entity, &mut Squad, &mut Transform)>,
    mut grunts: Members,
    objects: Query<(Has<crate::level::Placed>, Option<&crate::fx::Destructible>)>,
    player: Query<(Entity, &GlobalTransform), With<Player>>,
    markers: Res<crate::alarm::Markers>,
    mut triggers: MessageReader<crate::mission::SendTrigger>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
    (mut rng, physics, mut stats, mut goliaths): (ResMut<FxStats>, NonSend<Physics>, ResMut<super::AiStats>, MessageWriter<crate::goliath::SpawnGoliath>),
    (companions, noise, mission, army, traitors): (
        Query<(Entity, &GlobalTransform, &super::Companion), Without<Player>>,
        Res<super::Interests>,
        Option<Res<crate::mission::Mission>>,
        Query<(), With<super::ArmyTarget>>,
        Query<(), With<crate::weapons::traitor::Traitor>>,
    ),
) {
    let dt = time.delta_secs();
    let got: Vec<i64> = triggers.read().map(|t| t.0).collect();
    let (pe, pp) = match player.single() {
        Ok((e, t)) => (Some(e), t.translation()),
        Err(_) => (None, Vec3::ZERO),
    };
    let global_tag = mission.as_ref().is_some_and(|m| m.ai_target_tag != 0);
    // the list +0x604 (Squad_Init: Roman and his companions), each aimed at 7 above the feet (no rig here)
    let cands: Vec<super::TargetCandidate> = pe
        .map(|e| super::TargetCandidate { entity: e, pos: pp, hit: pp + Vec3::Y * 7.0, player: true, army: army.contains(e) })
        .into_iter()
        .chain(companions.iter().filter(|(_, _, c)| c.dead.is_none()).map(|(e, t, _)| {
            let pos = t.translation();
            super::TargetCandidate { entity: e, pos, hit: pos + Vec3::Y * 7.0, player: false, army: army.contains(e) }
        }))
        .collect();
    let mut shares: Vec<Share> = Vec::new();
    for (se, mut sq, mut tr) in &mut squads {
        // the members, made at their bones (fn_0042de60: state 0xde, idle), then the goliath at ref_goliath_00
        for (tpl, offset) in std::mem::take(&mut sq.pending) {
            spawn.write(crate::level::SpawnGrunt {
                at: sq.point(offset),
                template: tpl.to_owned(),
                alarm: false,
                barracks: None,
                exit: None,
                marker_tags: None,
                alert_group: sq.group,
                squad: Some(SquadLink { squad: se, offset }),
                ..default()
            });
        }
        let made_now = sq.goliath_slot.is_some();
        if let Some(offset) = sq.goliath_slot.take() {
            let (kill, on_death, tag) = sq.handed_over;
            let entity = commands.spawn_empty().id();
            sq.goliath = Some(entity);
            goliaths.write(crate::goliath::SpawnGoliath {
                entity,
                at: sq.point(offset),
                yaw: sq.yaw,
                squad: Some(SquadLink { squad: se, offset }),
                group: Some(sq.group as u32),
                cage: sq.goliath_cage,
                kill_trigger: kill,
                death_trigger: on_death,
                tag,
            });
        }
        // the members spawn_grunts made for this squad join its list
        for (ge, g, h) in &grunts {
            if g.squad.is_some_and(|l| l.squad == se) && !sq.members.contains(&ge) {
                sq.members.push(ge);
                sq.health.insert(ge, h.0);
            }
        }
        // a member hit or killed alerts the squad (ai_04.cpp:397-410, 605-611): the player as its target
        let mut hurt = false;
        let s = &mut *sq;
        for &m in &s.members {
            let hp = grunts.get(m).map(|(_, _, h)| h.0).unwrap_or(0.0);
            if s.health.insert(m, hp).is_some_and(|last| hp < last) {
                hurt = true;
            }
        }
        let live = |g: &Grunt, h: &Health| g.mind != Mind::Dead && h.0 > 0.0;
        // dead members leave the list
        sq.members.retain(|&m| grunts.get(m).is_ok_and(|(_, g, h)| live(g, h) && g.squad.is_some()));
        let mut cx = AlertCx { grunts: &mut grunts, rng: &mut rng, stats: &mut stats, shares: &mut shares, traitors: &traitors };
        // the goliath dead: every member charges the player (charge_count = the list's length, the timers cleared)
        // (its entity is filled the frame after it is reserved: alive until then)
        if let Some(gl) = sq.goliath.filter(|_| !made_now) {
            // dead: gone, its Destructible taken by its death, or dying
            let dead = match objects.get(gl) {
                Err(_) => true,
                Ok((made, d)) => made && d.is_none_or(|d| d.health <= 0.0 || d.dying.is_some()),
            };
            if dead {
                sq.goliath = None;
                let n = sq.members.len() as i64;
                sq.charge_count = (n, n);
                sq.halt = 0.0;
                sq.charge = 0.0;
                if pe.is_some() {
                    alert(&mut sq, se, pe, true, &mut cx);
                }
            }
        }
        if sq.members.is_empty() && sq.goliath.is_none() && sq.pending.is_empty() && sq.phase != SquadPhase::Waiting {
            info!("EVENT ai: squad {se:?} gone");
            commands.entity(se).despawn();
            continue;
        }
        // the trigger: the spline (0x0042d1b0: a tag picked in the range; a missing spline destroys the squad)
        if sq.phase == SquadPhase::Waiting && sq.trigger != 0 && got.contains(&sq.trigger) {
            match markers.pick_spline(sq.tags, cx.rng.rand()) {
                Some(tag) => {
                    sq.run = Some((tag, 0));
                    sq.phase = SquadPhase::Moving;
                    sq.route_wait = super::ROUTE_PLAN;
                    info!("EVENT ai: squad moves on spline {tag}");
                }
                None => warn!("ai: squad {se:?}: no spline in {:?}", sq.tags),
            }
        }
        if hurt && pe.is_some() {
            alert(&mut sq, se, pe, true, &mut cx);
        }
        match sq.phase {
            SquadPhase::Moving => {
                // the squad's own scan (0x0042be80, range +0x5dc): AI_FindTarget's timer
                sq.scan_timer -= dt;
                if sq.scan_timer <= 0.0 {
                    sq.scan_timer = cx.rng.range(0.75, 1.25);
                    let heard = noise.list.iter().any(|i| i.pos.distance(sq.pos) <= i.radius);
                    if let Some(t) = find_target(&sq, &cands, heard, global_tag, &physics) {
                        alert(&mut sq, se, Some(t), true, &mut cx);
                    }
                }
                if let Some((tag, i)) = sq.run.filter(|_| sq.phase == SquadPhase::Moving) {
                    match markers.spline_marker(tag, i) {
                        Some(mk) => {
                            let to = (mk.pos - sq.pos).with_y(0.0);
                            if to.length() < 8.0 {
                                // Marker_ProcessArrival 0x0044ba30: its trigger
                                if mk.trigger != 0 {
                                    // (through the commands: this system reads the same messages)
                                    commands.write_message(crate::mission::SendTrigger(mk.trigger));
                                }
                                let n = markers.spline_len(tag);
                                sq.run = if i + 1 < n {
                                    Some((tag, i + 1))
                                } else if sq.looping {
                                    Some((tag, 0))
                                } else {
                                    None
                                };
                                if sq.run.is_none() {
                                    sq.phase = SquadPhase::Done;
                                    info!("EVENT ai: squad at the end of its spline");
                                }
                            } else if sq.route_wait > 0.0 {
                                sq.route_wait -= dt;
                            } else {
                                sq.pace += (sq.speed - sq.pace) * (1.0 - (-dt / PACE_TIME).exp());
                                let step = to.normalize_or_zero() * (sq.pace * dt).min(to.length());
                                sq.pos += step;
                                // on the ground (force_ground): the height follows the landscape under the anchor
                                // (Object_ForceGround 0x00455860 keeps only ownerless bodies: physics.rs
                                // raycast_landscape). A ray that stops at any body met its own goliath's hit capsule
                                // and the anchor climbed on it frame after frame (oracle ticker_l3m1: the goliaths
                                // reached z 24000 while the original's walk at z 838..846)
                                if let Some(hit) = physics.raycast_landscape(sq.pos + Vec3::Y * 200.0, -Vec3::Y, 1000.0) {
                                    sq.pos.y = hit.y;
                                }
                                sq.yaw = to.x.atan2(to.z);
                            }
                        }
                        None => sq.phase = SquadPhase::Done,
                    }
                }
            }
            SquadPhase::Halted => {
                sq.pace = 0.0;
                // state 0x98: the timers count down; at 0 the target is dropped and the spline resumes, unless a member
                // still fights (SquadObject_Update 0x00446ba0, ai_07.cpp:413-448): the first live member with a target
                // or in state 10 re-arms the halt timer, and only that: the timer and that member's target go to the
                // tag's squads (0x00433ba0 / 0x00433bc0); no charge, no new target of its own
                sq.halt -= dt;
                sq.charge -= dt;
                if sq.halt <= 0.0 {
                    let fighter = sq.members.iter().find_map(|&m| {
                        cx.grunts.get(m).ok().filter(|(_, g, _)| g.target.is_some() || g.mind == Mind::Fire).map(|(_, g, _)| g.target)
                    });
                    match fighter {
                        Some(target) => {
                            sq.halt = cx.rng.range(sq.resume.0, sq.resume.1);
                            cx.shares.push(Share::from(se, &sq, target));
                        }
                        None => {
                            sq.target = None;
                            sq.phase = sq.resume_phase;
                            info!("EVENT ai: squad resumes");
                        }
                    }
                }
            }
            _ => {}
        }
        tr.translation = sq.pos;
        tr.rotation = Quat::from_rotation_y(sq.yaw) * crate::level::game_to_engine();
    }
    // 0x00433bc0 on each squad of the tag down the sequence: vt+0x23c(target, 0) (the halt without pass-on or
    // charge), the base SetTarget(target, 1), and the sender's halt timer (0x00433ba0)
    let mut none = Vec::new();
    let mut cx = AlertCx { grunts: &mut grunts, rng: &mut rng, stats: &mut stats, shares: &mut none, traitors: &traitors };
    for s in shares {
        for (se, mut sq, _) in &mut squads {
            if se == s.from || !shares_halt((s.tag, s.sequence), (sq.squad_tag, sq.sequence)) {
                continue;
            }
            alert(&mut sq, se, s.target, false, &mut cx);
            set_target(&mut sq, se, s.target, true, &mut cx);
            sq.halt = s.halt;
            info!("EVENT ai: squad {se:?} halts with squad {:?} (tag {})", s.from, s.tag);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn halt_passes_down_the_sequence() {
        // lev_3_m1's 601 sequence 1 tells a same-tag sequence 2, not sequence 0 nor another tag
        assert!(shares_halt((601, 1), (601, 2)));
        assert!(shares_halt((601, 1), (601, 1)));
        assert!(!shares_halt((601, 1), (601, 0)));
        assert!(!shares_halt((601, 1), (602, 5)));
    }

    #[test]
    fn charge_count_is_an_even_integer_draw() {
        // (rand * (max - min + 1)) >> 15 + min: lev_2_m1's 4..10, each of the 7 counts equally likely
        assert_eq!(charge_count((4, 10), 0.0), 4);
        assert_eq!(charge_count((4, 10), 0.999), 10);
        assert_eq!(charge_count((4, 10), 3.0 / 7.0), 7);
        assert_eq!(charge_count((1, 1), 0.5), 1);
    }
}

/// The playtest state's `squad`: squads, the first's phase, members in squads, halts and charges so far, members
/// following (state 0xdc, walking), members fighting in place (0xdd).
pub fn log_squads(squads: Query<&Squad>, grunts: Query<&Grunt>, state: Option<ResMut<crate::playtest::PlayState>>, stats: Res<super::AiStats>) {
    let Some(mut state) = state else { return };
    let first = squads.iter().next();
    state.0.insert(
        "squad".into(),
        serde_json::json!({
            "count": squads.iter().count(),
            "phase": first.map(|s| format!("{:?}", s.phase)).unwrap_or_default(),
            "members": squads.iter().map(|s| s.members.len()).sum::<usize>(),
            "halts": stats.squad_halts,
            "charged": stats.squad_charged,
            "marching": grunts.iter().filter(|g| g.squad.is_some() && g.walking).count(),
            "fighting": grunts.iter().filter(|g| g.squad.is_some() && g.mind == Mind::Fire).count(),
        }),
    );
}
