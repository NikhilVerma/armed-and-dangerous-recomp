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
        // ref_grunt_0%d "grunt", ref_regular_0%d "grunt_regular", ref_captain_0%d "captain": each list stops at its
        // first missing index (the "0" is literal: the 11th is ref_grunt_010)
        for (prefix, tpl) in [("ref_grunt_0", "grunt"), ("ref_regular_0", "grunt_regular"), ("ref_captain_0", "captain")] {
            for i in 0.. {
                let Some(o) = bone(&format!("{prefix}{i}")) else { break };
                pending.push((tpl, o));
            }
        }
        let goliath_slot = bone("ref_goliath_00");
        // no reference at all: "Squad has no valid ref points" and the squad is deactivated (vt+0xdc): left with
        // nothing, it is gone on its first update
        let phase = if pending.is_empty() && goliath_slot.is_none() {
            warn!("ai: squad {template} at {pos:.0} has no valid ref points");
            SquadPhase::Done
        } else {
            SquadPhase::Waiting
        };
        // a min above its max is logged and lowered to it (Squad_Init: resume / charge delay, charge count)
        let range = |name: &str, lo: f32, hi: f32| {
            let (lo, hi) = (p.f(&format!("{name}_min"), lo), p.f(&format!("{name}_max"), hi));
            if lo > hi {
                warn!("ai: squad {template}: {name}_min {lo} > {name}_max {hi}");
            }
            (lo.min(hi), hi)
        };
        let (count_lo, count_hi) = (p.i("charge_count_min", 1), p.i("charge_count_max", 1));
        let tag = p.i("squad_tag", 0);
        Squad {
            phase,
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
            scan_timer: 0.0,
            // (0 would be no group: the original logs and uses 0xfffe7e53)
            group: if tag != 0 { -tag } else { -0x181ad },
            resume: range("resume_delay", 10.0, 20.0),
            charge_delay: range("charge_delay", 5.0, 10.0),
            charge_count: (count_lo.min(count_hi), count_hi),
            halt: 0.0,
            charge: 0.0,
            resume_phase: SquadPhase::Waiting,
            target: None,
            members: Vec::new(),
            health: default(),
            goliath: None,
            goliath_slot,
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

/// A halt passed on to the other squads of the tag (0x00433bc0): from which squad, its tag and sequence, the target and
/// its halt timer.
#[derive(Clone, Copy, Debug)]
struct Share {
    from: Entity,
    tag: i64,
    sequence: i64,
    target: Entity,
    halt: f32,
}

/// 0x00433bc0's choice: another squad of the same squad_tag whose squad_sequence is not below the sender's (signed).
pub fn shares_halt(from: (i64, i64), other: (i64, i64)) -> bool {
    other.0 == from.0 && other.1 >= from.1
}

/// SquadObject_Slot143 0x00433c70: halt, charge, tell the group; the halt passed on down the tag's sequence
/// (0x00433bc0, after the frame: `shares`).
fn alert(sq: &mut Squad, target: Entity, grunts: &mut Query<(Entity, &mut Grunt, &Health)>, rng: &mut FxStats, stats: &mut super::AiStats) {
    let t = rng.range(sq.resume.0, sq.resume.1);
    if t > sq.halt {
        sq.halt = t;
    }
    if sq.phase != SquadPhase::Halted {
        sq.resume_phase = sq.phase;
        sq.phase = SquadPhase::Halted;
        stats.squad_halts += 1;
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
            g.charged = true;
            sent += 1;
            false
        });
        stats.squad_charged += sent;
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
    objects: Query<(Has<crate::level::Placed>, Option<&crate::fx::Destructible>)>,
    player: Query<(Entity, &GlobalTransform), With<Player>>,
    markers: Res<crate::alarm::Markers>,
    mut triggers: MessageReader<crate::mission::SendTrigger>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
    (mut rng, physics, mut stats, mut goliaths): (ResMut<FxStats>, NonSend<Physics>, ResMut<super::AiStats>, MessageWriter<crate::goliath::SpawnGoliath>),
) {
    let dt = time.delta_secs();
    let got: Vec<i64> = triggers.read().map(|t| t.0).collect();
    let (pe, pp) = match player.single() {
        Ok((e, t)) => (Some(e), t.translation()),
        Err(_) => (None, Vec3::ZERO),
    };
    let mut shares: Vec<Share> = Vec::new();
    for (se, mut sq, mut tr) in &mut squads {
        let halts_before = stats.squad_halts;
        let target_before = sq.target;
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
                if let Some(p) = pe {
                    alert(&mut sq, p, &mut grunts, &mut rng, &mut stats);
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
                    sq.route_wait = super::ROUTE_PLAN;
                    info!("EVENT ai: squad moves on spline {tag}");
                }
                None => warn!("ai: squad {se:?}: no spline in {:?}", sq.tags),
            }
        }
        if hurt {
            if let Some(p) = pe {
                alert(&mut sq, p, &mut grunts, &mut rng, &mut stats);
            }
        }
        match sq.phase {
            SquadPhase::Moving => {
                // the squad's own scan (0x0042be80, range +0x5dc): the player within it and in sight
                sq.scan_timer -= dt;
                if sq.scan_timer <= 0.0 {
                    sq.scan_timer = rng.range(0.75, 1.25);
                    if let Some(p) = pe {
                        // (its sight passes through the AI characters, its own goliath among them: sight_ray as
                        // 0x00426fd0, which excludes the AI list World+0xe7c; a plain ray from the anchor started inside
                        // the goliath's hit capsule and never saw Roman beside it: oracle ticker_l3m1, the original's
                        // squad halts 16 frames after Roman appears there)
                        if (pp - sq.pos).with_y(0.0).length() <= sq.scan && super::sight_ray(&physics, sq.pos, pp + Vec3::Y * 7.0, &[]) {
                            alert(&mut sq, p, &mut grunts, &mut rng, &mut stats);
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
                // state 0x98: the timers count down; at 0 (no member still fighting) the target is dropped and the
                // spline resumes
                sq.halt -= dt;
                sq.charge -= dt;
                if sq.halt <= 0.0 {
                    let fighting = sq.members.iter().any(|&m| grunts.get(m).is_ok_and(|(_, g, _)| g.target.is_some() || g.mind == Mind::Fire));
                    match (fighting, sq.target.or(pe)) {
                        (true, Some(t)) => {
                            sq.halt = rng.range(sq.resume.0, sq.resume.1);
                            alert(&mut sq, t, &mut grunts, &mut rng, &mut stats);
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
        // alerted this frame (a new halt or a new target): passed on to the tag's squads (0x00433bc0)
        if let (Some(t), true) = (sq.target, stats.squad_halts != halts_before || sq.target != target_before) {
            if sq.squad_tag != 0 {
                shares.push(Share { from: se, tag: sq.squad_tag, sequence: sq.sequence, target: t, halt: sq.halt });
            }
        }
    }
    // 0x00433bc0: vt+0x23c(target, 0) on each (the halt), the base SetTarget, and the sender's halt timer
    for s in shares {
        for (se, mut sq, _) in &mut squads {
            if se == s.from || !shares_halt((s.tag, s.sequence), (sq.squad_tag, sq.sequence)) {
                continue;
            }
            alert(&mut sq, s.target, &mut grunts, &mut rng, &mut stats);
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
