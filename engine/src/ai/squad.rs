//! Squads (SquadObject, vtable 0x005ab3e8): a moving anchor that carries captains and grunts (and a goliath) in a
//! formation along a marker spline, halts when it meets the player, and sends some of its members to charge.
//! docs/subsystems/infantry.md section 4.
//! - Load (Squad_Init 0x00433220, ai_04.cpp:1318-1464): `Squad::from_props`; the formation is the template's
//!   `<template>_ref` anim (static bone offsets ref_captain_0N / ref_regular_0N / ref_grunt_0N / ref_goliath_00).
//! - The trigger (SquadObject_Slot14 0x00433a10): the spline run (0x0042d1b0), members follow their bones (state 0xdc,
//!   GenericAI_Update ai_06.cpp:3062-3089: `member_goal`).
//! - The squad's own target scan (0x0042be80 in states 0x96 / 0x97) and its alert (SquadObject_Slot143 0x00433c70):
//!   halt (state 0x98) for resume_delay, charge_count members leave and charge every charge_delay, the group hears.
//! - Update (SquadObject_Update 0x00446ba0, ai_07.cpp:384-453): the halt re-armed while a member still fights; the
//!   goliath dead -> every member charges; nothing left -> the squad is gone.
//! The goliath member itself (+0xb7c) is another module's (vehicles / defence): `Squad::goliath` is the hook it fills.

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
    /// move_speed, or patrol_speed with marker_patrol (guess at which the speed getter ai_02.cpp:2358 returns)
    pub speed: f32,
    /// scan_distance +0x5dc (default 300): the squad's own target scan
    pub scan: f32,
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
    /// +0xb7c the goliath (set by the goliath's module when it spawns one at `goliath_slot`)
    pub goliath: Option<Entity>,
    pub goliath_slot: Option<Vec3>,
    /// the members still to make: (template, offset), made on the first frame (spawn_grunts links them)
    pub pending: Vec<(&'static str, Vec3)>,
    /// playtest log: halts, charges
    pub halts: usize,
    pub charged: usize,
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
            speed: if p.i("marker_patrol", 0) != 0 { p.f("patrol_speed", 10.0) } else { p.f("move_speed", 40.0) },
            scan: p.f("scan_distance", 300.0),
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
            pending,
            halts: 0,
            charged: 0,
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

/// The member's goal in state 0xdc: its bone, at most twice its own move_speed (0x00471970).
pub fn member_goal(sq: &Squad, link: SquadLink, pos: Vec3, move_speed: f32) -> (Vec3, f32) {
    let at = sq.point(link.offset);
    let d = (at - pos).with_y(0.0).length();
    // (the original steers by force toward the point; here the speed eases off within a few units)
    (at, (d * 4.0).min(2.0 * move_speed))
}

/// SquadObject_Slot143 0x00433c70: halt, charge, tell the group.
fn alert(sq: &mut Squad, target: Entity, grunts: &mut Query<(Entity, &mut Grunt, &Health)>, rng: &mut FxStats) {
    let t = rng.range(sq.resume.0, sq.resume.1);
    if t > sq.halt {
        sq.halt = t;
    }
    if sq.phase != SquadPhase::Halted {
        sq.resume_phase = sq.phase;
        sq.phase = SquadPhase::Halted;
        sq.halts += 1;
        info!("EVENT ai: squad halts ({} members)", sq.members.len());
    }
    if sq.charge <= 0.0 {
        let n = rng.range(sq.charge_count.0 as f32, sq.charge_count.1 as f32 + 0.999) as usize;
        let mut sent = 0;
        sq.members.retain(|&m| {
            if sent >= n {
                return true;
            }
            let Ok((_, mut g, h)) = grunts.get_mut(m) else { return false };
            if h.0 <= 0.0 || g.mind == Mind::Dead {
                return false;
            }
            // 0x0042cc10 (the squad and the group released), 0x0042cff0, 0x0042ce70 (attack the target)
            g.squad = None;
            g.alert_group = 0;
            g.guard = false;
            g.target = Some(target);
            g.mind = Mind::Chase;
            sent += 1;
            false
        });
        sq.charged += sent;
        if sent > 0 {
            info!("EVENT ai: squad sends {sent} to charge");
        }
        sq.charge = rng.range(sq.charge_delay.0, sq.charge_delay.1);
    }
    // the base SetTarget 0x0042cc70: the group (+0x574) takes the target after 0.25..1 s (0x0042c700)
    sq.target = Some(target);
    for &m in &sq.members {
        if let Ok((_, mut g, _)) = grunts.get_mut(m) {
            if g.target.is_none() && g.pending.is_none() {
                g.pending = Some((target, rng.range(0.25, 1.0)));
            }
        }
    }
}

/// The squads' frame: members made, the trigger, the spline, the scan, the halt and the charges.
#[allow(clippy::too_many_arguments)]
pub fn update_squads(
    mut commands: Commands,
    time: Res<Time>,
    mut squads: Query<(Entity, &mut Squad, &mut Transform)>,
    mut grunts: Query<(Entity, &mut Grunt, &Health)>,
    healths: Query<&Health, Without<Grunt>>,
    player: Query<(Entity, &GlobalTransform), With<Player>>,
    markers: Res<crate::alarm::Markers>,
    mut triggers: MessageReader<crate::mission::SendTrigger>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
    (mut rng, physics): (ResMut<FxStats>, NonSend<Physics>),
) {
    let dt = time.delta_secs();
    let got: Vec<i64> = triggers.read().map(|t| t.0).collect();
    let (pe, pp) = match player.single() {
        Ok((e, t)) => (Some(e), t.translation()),
        Err(_) => (None, Vec3::ZERO),
    };
    for (se, mut sq, mut tr) in &mut squads {
        // the members, made at their bones (fn_0042de60: state 0xde, idle)
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
        // the goliath dead: every member charges the player (charge_count = the list's length, the timers cleared)
        if let Some(gl) = sq.goliath {
            if healths.get(gl).map_or(true, |h| h.0 <= 0.0) {
                sq.goliath = None;
                let n = sq.members.len() as i64;
                sq.charge_count = (n, n);
                sq.halt = 0.0;
                sq.charge = 0.0;
                if let Some(p) = pe {
                    alert(&mut sq, p, &mut grunts, &mut rng);
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
            match markers.pick_spline(sq.tags, rng.rand()) {
                Some(tag) => {
                    sq.run = Some((tag, 0));
                    sq.phase = SquadPhase::Moving;
                    info!("EVENT ai: squad moves on spline {tag}");
                }
                None => warn!("ai: squad {se:?}: no spline in {:?}", sq.tags),
            }
        }
        if hurt {
            if let Some(p) = pe {
                alert(&mut sq, p, &mut grunts, &mut rng);
            }
        }
        match sq.phase {
            SquadPhase::Moving => {
                // the squad's own scan (0x0042be80, range +0x5dc): the player within it and in sight
                sq.scan_timer -= dt;
                if sq.scan_timer <= 0.0 {
                    sq.scan_timer = rng.range(0.75, 1.25);
                    if let Some(p) = pe {
                        if (pp - sq.pos).with_y(0.0).length() <= sq.scan && super::line_of_sight(&physics, sq.pos, pp) {
                            alert(&mut sq, p, &mut grunts, &mut rng);
                        }
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
                            } else {
                                let step = to.normalize_or_zero() * (sq.speed * dt).min(to.length());
                                sq.pos += step;
                                // on the ground (force_ground): the height follows the terrain under the anchor
                                if let Some((_, hit, _)) = physics.raycast(sq.pos + Vec3::Y * 200.0, -Vec3::Y, 1000.0) {
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
                // state 0x98: the timers count down; at 0 (no member still fighting) the target is dropped and the
                // spline resumes
                sq.halt -= dt;
                sq.charge -= dt;
                if sq.halt <= 0.0 {
                    let fighting = sq.members.iter().any(|&m| grunts.get(m).is_ok_and(|(_, g, _)| g.target.is_some() || g.mind == Mind::Fire));
                    match (fighting, sq.target.or(pe)) {
                        (true, Some(t)) => {
                            sq.halt = rng.range(sq.resume.0, sq.resume.1);
                            alert(&mut sq, t, &mut grunts, &mut rng);
                        }
                        _ => {
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
}

/// The playtest state's `squad`: squads, the first's phase, members in squads, halts and charges so far, members
/// following (state 0xdc, walking), members fighting in place (0xdd).
pub fn log_squads(squads: Query<&Squad>, grunts: Query<&Grunt>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let first = squads.iter().next();
    state.0.insert(
        "squad".into(),
        serde_json::json!({
            "count": squads.iter().count(),
            "phase": first.map(|s| format!("{:?}", s.phase)).unwrap_or_default(),
            "members": squads.iter().map(|s| s.members.len()).sum::<usize>(),
            "halts": squads.iter().map(|s| s.halts).sum::<usize>(),
            "charged": squads.iter().map(|s| s.charged).sum::<usize>(),
            "marching": grunts.iter().filter(|g| g.squad.is_some() && g.walking).count(),
            "fighting": grunts.iter().filter(|g| g.squad.is_some() && g.mind == Mind::Fire).count(),
        }),
    );
}
