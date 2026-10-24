//! The knockout bomb (special 7, "bunch of fives", player_rabbiter; sold in the pubs, placed on lev_4_m2): a boxing
//! glove on Roman's fist pulls up to six bad guys off their feet one after another and flings them at him, and he
//! punches each one away as it arrives.
//!
//! The original (game_06.cpp 2391-2460 and 2900-2951, game_05.cpp 1825-1850 and 2009-2150, ai_03.cpp 2658-2798):
//! - Use (Fire Special with it in hand, not in a jump: 0x004788a0's case 1 at 0x0047a2bd): the targets are found
//!   (0x00407c20, `targets`); none: nothing happens (nothing is spent). Otherwise wpn_puncher_glove_on and
//!   wpn_puncher_ding (2D), each target grabbed (vt+0x120: GenericAIObject slot 72 0x0042f1d0: the idle anim, state
//!   0x19a, held where it stands; one in an IC move or riding a jump pad's turbo has its move ended (vt+0x1ac (1)) or
//!   its ride stopped (+0x484 bits 28-29 cleared, the pack back to turbo_idle) and is flung at once, in the same call,
//!   vt+0x124 (1)), its fling timer set (Game+0xcc4 + 4i: i + 1 + (d0 - di) / speed, the line-of-sight ray's distances
//!   d to the bodies of the first and this target (the HUD slot's +0x1c) and speed = bof_speed_factor x its
//!   move_speed, at least 0: they arrive about a second apart), the weapons put away
//!   (0x004a0e00 (0)), player_punchingglove attached at bof_glove_player_bone, +0xa34 |= 0x8000000; then the special
//!   is spent and the shot counted (mode_end 0x0047a4e9 -> 0x004a1aa0, Game+0x2718).
//! - While the glove is on (0x00479142, +0xa34 & 0x78000000) Roman's move input is zeroed; each held target's timer
//!   counts down and at 0 the target is flung (vt+0x124 (0): GenericAIObject slot 73 0x0042f2f0, timer 100000); when
//!   none of the targets is alive and held any more the glove comes off and the weapons come back (cbc = 0, cbd = 1).
//! - The punch (game_05.cpp 1825-1850): the windup (anim mode 0x24) while the glove waits; Fire held (0x00414810 (1,
//!   8)) throws one of the three punches (rand * 100 >> 15 < 33 / < 66 / else: modes 0x25 / 0x26 / 0x27, once each),
//!   then the windup again.
//! - A flung target (state 0x19b, ai_06.cpp 3172-3190): physics off, a flail anim (0x49 + rand * 2 >> 15), out of its
//!   squad; while it still touches the landscape (0x00454be0) the ballistic velocity (0x00471a00) to Roman's ref_hit_00
//!   at bof_speed_factor x move_speed across under bof_gravity_factor x the world's gravity is set again each frame;
//!   the first frame it does not, +0x48c |= 0x80 and the velocity is never set again: it flies on that arc. A contact
//!   on the way down (the pending reaction 0x0042e1c0 for state 0x19b: any contact, 0x00454bc0) is its death as a
//!   ragdoll; here a ray along the frame's step, falling (the contact callback is not decoded).
//! - Reaching Roman (his contact callback 0x00477c90, +0xa34 & 0x78000000, the target found in the glove's slots):
//!   punching (+0xa34 & 0x70000000), the target is hit (vt+0x128 (1, the camera's direction, ...): GenericAIObject slot
//!   74 0x0042f470): stopped, the 0x83 record with all its health sent to GoRagdoll 0x00431310 from Roman: launched
//!   (Character_Launch 0x00431020, ai/launch.rs) when 10 s have passed since the last launch, else down as a ragdoll;
//!   wpn_puncher_hit_1 / _2 / _3 (rand * 100 >> 15 < 33 / < 66 / else) and wpn_puncher_cheer (2D). Not punching, the
//!   target goes down where it is (vt+0x128 (0) -> GoRagdoll without a record) and the glove lets go of everyone it
//!   still holds (vt+0x11c: slot 71 0x0042f190: physics back, Roman the target, the pursuit again).
//!
//! The candidates (0x00407c20): the characters of the AI list whose vt+0x118 (0x0043a9b0: grunts, archers, captains,
//! sniper captains) answers true, not held by anything (+0x870), nearest the eye first, inside the view narrowed by
//! autoaim_bof_factor_horz / _vert (0.2 / 0.3), within bof_range (weapon 7's range, 500), in line of sight, and not
//! more than 20 above Roman while standing on the landscape (0x00454be0: tested after the 21-entry cap and the range
//! test, so such a one still counts); at most six.
//!
//! Remaster notes: a flung target flies as a kinematic body (its physics body taken off, as the original turns its
//! physics off) and a punched one goes down as a ragdoll launched with Character_Launch's velocity (the original flails
//! through the arc and goes ragdoll on landing: a tuning item). bof_hit_force_factor and bof_up_force_punch_1..3 are
//! read by the game (render_r6.cpp 1084-1089) but nothing uses them: the 0x83 record's amount is 100.

use super::{SpecialId, targeting};
use crate::ai::{Grunt, Kind, Mind};
use crate::physics::Physics;
use bevy::prelude::*;

/// How many targets the glove holds (the six marker slots of hud +0x204).
pub const MAX_TARGETS: usize = 6;
/// A flung target's timer once flung (Game+0xcc4.. = 100000: it never fires again).
const FLUNG: f32 = 100_000.0;
/// Roman's reach for a flung target (his body against its sphere: radii 4 + 4, and the contact's slack).
const REACH: f32 = 9.0;

/// The glove on Roman's fist (Game+0xcbc, +0xcc0 the count, +0xcc4.. the timers, +0xafc the glove object; +0xa34's
/// 0x78000000 bits).
#[derive(Resource, Default, Debug)]
pub struct Glove {
    phase: Phase,
    /// the targets (hud +0x204's slots): who, and the time to fling it (None: let go)
    slots: Vec<Option<Slot>>,
    /// Fire Special pressed with the knockout bomb in hand (player.rs): the use, this frame
    pub request: bool,
    pub stats: GloveStats,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
enum Phase {
    #[default]
    Off,
    /// +0xa34 0x8000000: the windup, waiting
    Ready,
    /// +0xa34 0x10000000 / 0x20000000 / 0x40000000: punch n (0..3) playing, its time and length
    Punch { n: u8, t: f32, len: f32 },
}

#[derive(Clone, Copy, Debug)]
struct Slot {
    target: Entity,
    timer: f32,
}

/// Counts for the state log's "glove".
#[derive(Clone, Copy, Debug, Default)]
pub struct GloveStats {
    pub uses: u32,
    pub grabbed: u32,
    pub flung: u32,
    pub punched: u32,
    pub launched: u32,
    pub landed: u32,
    pub missed: u32,
    pub let_go: u32,
    pub punches: u32,
}

impl Glove {
    /// The glove is on (+0xa34 & 0x78000000): Roman stands, his weapons put away, Fire punches.
    pub fn active(&self) -> bool {
        self.phase != Phase::Off
    }

    /// Roman's anim mode while the glove is on (0x24 the windup, 0x25..0x27 the punches).
    pub fn anim_mode(&self) -> Option<u32> {
        match self.phase {
            Phase::Off => None,
            Phase::Ready => Some(crate::charanim::mode::WINDUP),
            Phase::Punch { n, .. } => Some(crate::charanim::mode::PUNCH_1 + n as u32),
        }
    }

    /// Punching now (+0xa34 & 0x70000000).
    pub fn punching(&self) -> bool {
        matches!(self.phase, Phase::Punch { .. })
    }

    fn holds(&self, e: Entity) -> Option<usize> {
        self.slots.iter().position(|s| s.is_some_and(|s| s.target == e))
    }
}

/// A character the glove has: held where it stands (state 0x19a), or flung at Roman (0x19b) with its velocity.
#[derive(Component, Clone, Copy, Debug)]
pub enum Gloved {
    Held,
    /// flung at Roman: its speed across, the gravity of its arc, its velocity and whether it still homes (it has not
    /// yet left the landscape: +0x48c 0x80 clear)
    Flung { speed: f32, gravity: f32, vel: Vec3, homing: bool },
}

/// The fling delay of the i-th target (0x004788a0 case 1): i + 1 + (d0 - di) / speed, at least 0.
pub fn fling_delay(i: usize, d0: f32, di: f32, speed: f32) -> f32 {
    let inv = 1.0 / speed.max(1e-3);
    (d0 * inv + i as f32 + 1.0 - inv * di).max(0.0)
}

/// The punch picked by a roll in [0, 1) (rand * 100 >> 15 < 33 / < 66 / else): 0, 1 or 2.
pub fn punch_of(roll: f32) -> u8 {
    let r = (roll * 100.0) as u32;
    if r < 33 {
        0
    } else if r < 66 {
        1
    } else {
        2
    }
}

/// vt+0x118 of the AI classes (0x0043a9b0 for grunts, archers, captains and sniper captains; the other classes' slot
/// answers false): a character the glove can take, not held by anything (+0x870: a black hole's carry-off, a
/// companion's close kill, the glove itself).
pub fn can_be_gloved(g: &Grunt) -> bool {
    matches!(g.kind, Kind::Grunt | Kind::Bow | Kind::Captain | Kind::Sniper) && g.mind != Mind::Dead && g.mind != Mind::Held
}

pub struct GlovePlugin;

impl Plugin for GlovePlugin {
    fn build(&self, app: &mut App) {
        use crate::levelreset::LevelAppExt;
        app.init_level_resource::<Glove>()
            .add_systems(Update, (use_glove, update, flights, log).chain().after(crate::player::PlayerSet::Weapons).run_if(crate::playing));
    }
}

/// The use (0x004788a0 case 1): the targets, the grab, the timers; the special spent only when there was someone.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn use_glove(
    mut commands: Commands,
    mut glove: ResMut<Glove>,
    mut roman: Query<(Entity, &mut crate::player::Roman, &GlobalTransform)>,
    cam: Query<(&GlobalTransform, &Projection), With<crate::camera::FollowCam>>,
    mut grunts: Query<(Entity, &mut Grunt, &GlobalTransform, Option<&crate::skin::Animator>, Option<&crate::physics::Body>, Option<&targeting::HudSphere>)>,
    joints: Query<&GlobalTransform, Without<Grunt>>,
    (taken, ic): (Query<(), Or<(With<super::blackhole::Carried>, With<Gloved>, With<crate::ai::close_kill::Victim>)>>, Query<&crate::ai::ic::IcEntry>),
    mut physics: NonSendMut<Physics>,
    (data, cheats, mut stats, mut sounds, mut rng): (Res<crate::gamedata::GameData>, Res<crate::cheats::Cheats>, ResMut<crate::player::PlayerStats>, MessageWriter<crate::audio::PlaySound>, ResMut<crate::fx::FxStats>),
    (mut log, characters): (Option<ResMut<crate::mission::Mission>>, targeting::Characters),
) {
    if !std::mem::take(&mut glove.request) || glove.active() {
        return;
    }
    let (Ok((_, mut r, rt)), Ok((c, proj))) = (roman.single_mut(), cam.single()) else { return };
    let g = data.props("p_globalsettings");
    let factors = (g.f("autoaim_bof_factor_horz", 0.2), g.f("autoaim_bof_factor_vert", 0.3));
    let range = g.f("bof_range", 500.0);
    let (speed_factor, gravity) = (g.f("bof_speed_factor", 3.0), g.f("bof_gravity_factor", 1.0) * crate::ai::launch::GRAVITY);
    let feet = rt.translation();
    let view = targeting::View::new(c, Some(proj), feet + Vec3::Y * 4.0);
    // the candidates: vt+0x118 true, not already someone's
    let mut cands = Vec::new();
    for (e, g, t, a, _, sphere) in grunts.iter() {
        if !can_be_gloved(g) || taken.contains(e) {
            continue;
        }
        let origin = t.translation();
        let joint = a.and_then(|a| a.skeleton.index.get("ref_hit_00").map(|&i| a.joints[i]));
        let hit = joint.and_then(|j| joints.get(j).ok()).map_or(origin + Vec3::Y * 6.0, |j| j.translation());
        let (centre, slack) = targeting::bounds(t, sphere);
        cands.push(targeting::Candidate { entity: e, dead: false, origin, centre, slack, hit, joint, body: None });
    }
    // one standing on the landscape more than 20 above Roman is passed over (0x00454be0), after the range test
    let passable = |e: Entity| characters.contains(e);
    let high = |c: &targeting::Candidate| c.origin.y > feet.y + 20.0 && targeting::on_landscape(&physics, c.origin);
    let found = targeting::search_with(&view, cands, factors, range, MAX_TARGETS, &physics, passable, high);
    if found.is_empty() {
        info!("EVENT glove: no target");
        return;
    }
    sounds.write(crate::audio::PlaySound::ui("wpn_puncher_glove_on"));
    sounds.write(crate::audio::PlaySound::ui("wpn_puncher_ding"));
    // the line of sight's ray distance to each one's body (0x00414110's, the HUD slot's +0x1c)
    let dists: Vec<f32> = found.iter().map(|t| targeting::hud_sight_distance(&physics, view.eye, t.point, (t.entity, None), &passable).unwrap_or_else(|| t.point.distance(view.eye))).collect();
    glove.slots.clear();
    for (i, t) in found.iter().enumerate() {
        let Ok((e, mut gr, _, _, body, _)) = grunts.get_mut(t.entity) else { continue };
        // 0x0042f1d0: in an IC move (0x00426630) or riding a turbo (+0xb08 with +0x484 bit 28): the move ended (vt+0x1ac
        // (1)) or the ride stopped (bits 28-29 cleared; the pack goes back to turbo_idle, ai.rs jumper_packs), flung
        // at once (vt+0x124 (1))
        let in_ic = ic.get(e).is_ok_and(|i| i.running);
        let riding = gr.jumper.is_some_and(|j| j.flight.is_some());
        let timer = if in_ic || riding {
            if in_ic {
                commands.entity(e).remove::<(crate::mover::Mover, crate::ai::ic::IcEntry)>();
            }
            if let Some(j) = gr.jumper.as_mut() {
                j.flight = None;
                j.vel = Vec3::ZERO;
            }
            fling(&mut commands, e, &mut gr, body, &mut physics, rng.rand(), speed_factor, gravity);
            glove.stats.flung += 1;
            FLUNG
        } else {
            hold(&mut commands, e, &mut gr);
            fling_delay(i, dists[0], dists[i], speed_factor * gr.move_speed)
        };
        glove.slots.push(Some(Slot { target: e, timer }));
        glove.stats.grabbed += 1;
    }
    glove.phase = Phase::Ready;
    glove.stats.uses += 1;
    if !cheats.on(crate::cheats::INFINITE_AMMO) {
        r.arsenal.spend(SpecialId::BunchOfFives);
    }
    stats.shots += 1;
    if let Some(m) = log.as_deref_mut() {
        m.log.push(format!("knockout bomb: {} grabbed", found.len()));
    }
    info!("EVENT glove: on, {} grabbed", found.len());
}

/// The grab (0x0042f1d0): the idle anim, state 0x19a, held where he stands.
fn hold(commands: &mut Commands, e: Entity, g: &mut Grunt) {
    g.mind = Mind::Held;
    g.target = None;
    commands.entity(e).insert(Gloved::Held);
}

/// The glove's timers and Roman's punches (0x00479142, game_05.cpp 1825-1850).
#[allow(clippy::too_many_arguments)]
fn update(
    mut commands: Commands,
    time: Res<Time>,
    mut glove: ResMut<Glove>,
    ctl: Res<crate::input::Controls>,
    mut grunts: Query<(&mut Grunt, Option<&crate::physics::Body>, &Gloved)>,
    mut physics: NonSendMut<Physics>,
    mut rng: ResMut<crate::fx::FxStats>,
    lib: Res<crate::skin::AnimLibrary>,
    data: Res<crate::gamedata::GameData>,
) {
    if !glove.active() {
        return;
    }
    let dt = time.delta_secs();
    // the punches: Fire held at the windup throws one; it plays once, then the windup again
    glove.phase = match glove.phase {
        Phase::Ready if ctl.down(crate::input::Act::Fire) => {
            let n = punch_of(rng.rand());
            glove.stats.punches += 1;
            let len = lib.duration(&format!("trotter_punch_{}", n + 1)).unwrap_or(if n == 1 { 0.667 } else { 0.633 });
            Phase::Punch { n, t: 0.0, len }
        }
        Phase::Punch { n, t, len } if t + dt < len => Phase::Punch { n, t: t + dt, len },
        Phase::Punch { .. } => Phase::Ready,
        p => p,
    };
    // the timers: a live target still the glove's counts down, and is flung at 0
    let g = data.props("p_globalsettings");
    let (speed_factor, gravity) = (g.f("bof_speed_factor", 3.0), g.f("bof_gravity_factor", 1.0) * crate::ai::launch::GRAVITY);
    let (mut live, mut flung) = (0, 0);
    let glove = &mut *glove;
    for slot in glove.slots.iter_mut() {
        let Some(s) = slot.as_mut() else { continue };
        let Ok((mut gr, body, gl)) = grunts.get_mut(s.target) else {
            *slot = None;
            continue;
        };
        if gr.mind == Mind::Dead {
            *slot = None;
            continue;
        }
        live += 1;
        s.timer -= dt;
        if s.timer <= 0.0 && matches!(gl, Gloved::Held) {
            s.timer = FLUNG;
            fling(&mut commands, s.target, &mut gr, body, &mut physics, rng.rand(), speed_factor, gravity);
            flung += 1;
        }
    }
    glove.stats.flung += flung;
    if live == 0 {
        // the glove comes off, the weapons come back (cbc = 0, cbd = 1)
        glove.phase = Phase::Off;
        glove.slots.clear();
        info!("EVENT glove: off");
    }
}

/// The fling (0x0042f2f0): physics off, out of its squad, the flail anim, state 0x19b.
#[allow(clippy::too_many_arguments)]
fn fling(commands: &mut Commands, e: Entity, g: &mut Grunt, body: Option<&crate::physics::Body>, physics: &mut Physics, roll: f32, speed_factor: f32, gravity: f32) {
    if let Some(b) = body {
        physics.remove(b.id);
        commands.entity(e).remove::<crate::physics::Body>();
    }
    g.squad = None;
    g.mind = Mind::Held;
    let id = if roll < 0.5 { 0x49 } else { 0x4a };
    // (its velocity, the arc to Roman, is set by `flights` from this frame on: it homes while on the landscape)
    commands.entity(e).insert((Gloved::Flung { speed: speed_factor * g.move_speed, gravity, vel: Vec3::ZERO, homing: true }, crate::ai::AnimOverride { id, looping: true }));
    info!("EVENT glove: flung {e:?}");
}

/// The flung (state 0x19b): homing on Roman's ref_hit_00; the landing, Roman's punch or his body.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn flights(
    mut commands: Commands,
    time: Res<Time>,
    mut glove: ResMut<Glove>,
    roman: Query<(Entity, &GlobalTransform, Option<&crate::skin::Animator>), With<crate::player::Roman>>,
    cam: Query<&GlobalTransform, With<crate::camera::FollowCam>>,
    mut q: Query<(Entity, &mut Grunt, &mut Transform, &mut Gloved, &crate::ai::body::CharacterBody, &mut crate::fx::Health)>,
    joints: Query<&GlobalTransform, Without<Grunt>>,
    mut physics: NonSendMut<Physics>,
    (mut rng, mut clock, mut stats, mut sounds): (ResMut<crate::fx::FxStats>, ResMut<crate::ai::launch::LaunchClock>, ResMut<crate::player::PlayerStats>, MessageWriter<crate::audio::PlaySound>),
) {
    let dt = time.delta_secs();
    let Ok((re, rt, ra)) = roman.single() else { return };
    let rfeet = rt.translation();
    let aim = ra.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| joints.get(a.joints[i]).ok())).map_or(rfeet + Vec3::Y * 6.0, |j| j.translation());
    let look = cam.single().map_or(Vec3::NEG_Z, |c| c.forward().as_vec3());
    let mut let_go = false;
    for (e, mut g, mut t, mut gl, cb, mut h) in &mut q {
        let Gloved::Flung { speed, gravity, vel, homing } = *gl else { continue };
        if g.mind == Mind::Dead {
            continue;
        }
        let pos = t.translation;
        // while it touches the landscape the velocity again each frame: the arc to Roman's hit point (0x00471a00 at
        // speed across); once off it (+0x48c 0x80) it flies on that arc
        let v = if homing { crate::ai::launch::launch_velocity(pos, aim, speed, gravity, false) } else { vel - Vec3::Y * gravity * dt };
        *gl = Gloved::Flung { speed, gravity, vel: v, homing: homing && targeting::on_landscape(&physics, pos) };
        let centre = pos + Vec3::Y * cb.radius;
        let near = (centre - rfeet).with_y(0.0).length() < REACH && centre.y > rfeet.y - cb.radius && centre.y < rfeet.y + 11.0 + cb.radius;
        if near {
            let slot = glove.holds(e);
            if let (Some(i), true) = (slot, glove.punching()) {
                // the punch (0x0042f470 -> GoRagdoll 0x00431310 with the 0x83 record, all his health, from Roman)
                glove.slots[i] = None;
                let push = if clock.allows() {
                    clock.stamp();
                    glove.stats.launched += 1;
                    let rolls = crate::ai::launch::LaunchRolls { angle: rng.rand(), side: rng.rand(), forward: rng.rand(), anim: rng.rand() };
                    crate::ai::launch::launch(rfeet, pos, rng.range(2.5, 3.0), g.move_speed, 0.0, false, rolls).velocity
                } else {
                    // the record's direction (the camera's) x its amount (100) x 0.15 on his 80 kg
                    look * 100.0 * 0.15 / 80.0
                };
                let hit = (rng.rand() * 100.0) as u32;
                sounds.write(crate::audio::PlaySound::ui(if hit < 33 { "wpn_puncher_hit_1" } else if hit < 66 { "wpn_puncher_hit_2" } else { "wpn_puncher_hit_3" }));
                sounds.write(crate::audio::PlaySound::ui("wpn_puncher_cheer"));
                g.attacker = Some(re);
                down(&mut commands, e, &mut g, &mut h, cb, pos, push, &mut physics);
                stats.kills += 1;
                glove.stats.punched += 1;
                info!("EVENT glove: punched {e:?}");
                continue;
            }
            if slot.is_some() {
                // not punching: he goes down where he is and the glove lets go of the rest (0x00477c90's else)
                glove.slots[slot.unwrap_or_default()] = None;
                let_go = true;
                glove.stats.missed += 1;
                info!("EVENT glove: {e:?} reached Roman unpunched");
            } else {
                glove.stats.landed += 1;
            }
            down(&mut commands, e, &mut g, &mut h, cb, pos, Vec3::ZERO, &mut physics);
            stats.kills += 1;
            continue;
        }
        // the landing: anything in the way of this frame's step, falling (0x0042e1c0 for state 0x19b: a ragdoll death)
        let step = v * dt;
        let landed = physics.raycast(pos + Vec3::Y * 0.5, step.normalize_or(Vec3::NEG_Y), step.length().max(0.6)).is_some_and(|(b, _, _)| physics.bodies.get(&b) != Some(&re));
        if landed && v.y <= 0.0 {
            if let Some(i) = glove.holds(e) {
                glove.slots[i] = None;
            }
            glove.stats.landed += 1;
            down(&mut commands, e, &mut g, &mut h, cb, pos, v, &mut physics);
            stats.kills += 1;
            info!("EVENT glove: {e:?} landed");
            continue;
        }
        t.translation = pos + step;
        if v.with_y(0.0).length_squared() > 1e-4 {
            g.yaw = v.x.atan2(v.z) + std::f32::consts::PI;
        }
    }
    if let_go {
        let glove = &mut *glove;
        // vt+0x11c (0x0042f190) on each one still held: physics back, Roman the target, the pursuit again; the
        // flung ones fly on, no longer the glove's
        for slot in glove.slots.iter_mut() {
            let Some(s) = slot.take() else { continue };
            if let Ok((e, mut g, _, gl, _, _)) = q.get_mut(s.target) {
                if matches!(*gl, Gloved::Held) && g.mind != Mind::Dead {
                    g.mind = Mind::Chase;
                    g.target = Some(re);
                    commands.entity(e).remove::<Gloved>();
                    glove.stats.let_go += 1;
                }
            }
        }
    }
}

/// Down as a ragdoll, dead (GoRagdoll 0x00431310 / the landing 0x0042e1c0): his body back with `push`, his health 0
/// (ai.rs `die`: the kill, the Dead line, the ragdoll from the body's velocity).
#[allow(clippy::too_many_arguments)]
fn down(commands: &mut Commands, e: Entity, g: &mut Grunt, h: &mut crate::fx::Health, cb: &crate::ai::body::CharacterBody, feet: Vec3, push: Vec3, physics: &mut Physics) {
    if let Some((body, character)) = crate::ai::body::add(physics, e, *cb, feet, "grunt") {
        if let Ok(mut b) = physics.world.body_mut(body.id) {
            let _ = b.set_linear_velocity(crate::physics::jv(push * physics.meters_per_unit));
        }
        commands.entity(e).insert((body, character));
    }
    commands.entity(e).remove::<(Gloved, crate::ai::AnimOverride)>();
    g.last_hit = f32::MAX;
    g.fixed = None;
    h.0 = 0.0;
}

/// The state log's "glove".
fn log(glove: Res<Glove>, held: Query<&Gloved>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let s = glove.stats;
    let flying = held.iter().filter(|g| matches!(g, Gloved::Flung { .. })).count();
    state.0.insert(
        "glove".into(),
        serde_json::json!({
            "active": glove.active(), "punching": glove.punching(), "slots": glove.slots.iter().flatten().count(),
            "held": held.iter().count() - flying, "flying": flying,
            "uses": s.uses, "grabbed": s.grabbed, "flung": s.flung, "punched": s.punched, "launched": s.launched,
            "landed": s.landed, "missed": s.missed, "let_go": s.let_go, "punches": s.punches,
        }),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The fling delays (0x004788a0 case 1): one second apart, the farther ones flung earlier by their extra flight.
    #[test]
    fn delays() {
        assert_eq!(fling_delay(0, 100.0, 100.0, 105.0), 1.0);
        let d = fling_delay(1, 100.0, 205.0, 105.0);
        assert!((d - 1.0).abs() < 1e-4, "{d}");
        // arrival = delay + distance / speed: the second one second after the first
        let arrive0 = fling_delay(0, 100.0, 100.0, 105.0) + 100.0 / 105.0;
        let arrive1 = fling_delay(1, 100.0, 300.0, 105.0) + 300.0 / 105.0;
        assert!((arrive1 - arrive0 - 1.0).abs() < 1e-4);
        assert_eq!(fling_delay(0, 100.0, 1000.0, 105.0), 0.0, "never below 0");
    }

    #[test]
    fn punches() {
        assert_eq!((punch_of(0.0), punch_of(0.329), punch_of(0.33), punch_of(0.659), punch_of(0.66), punch_of(0.999)), (0, 0, 1, 1, 2, 2));
    }
}
