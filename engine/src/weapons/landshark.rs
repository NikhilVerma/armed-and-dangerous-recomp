//! The Land Shark gun (player_sharkgun, PlayerWeapon vtable 0x005a4858) and its shark (SharkGunProjectile: load
//! 0x0047f6d0, launch 0x0047f7e0, update 0x0047fc90, bite 0x0047f600, target search 0x0047fa70, the scare 0x0047f920,
//! remove 0x0047f980; template sharkgun_projectile, p_sharkgun_projectile; "armed jaws" is its loop sound).
//!
//! - The gun fires only with a target picked (vt+0x138 0x004a2910): the HUD's shark pass (targeting.rs: AIs that can
//!   be bitten, inside the view narrowed by autoaim_sharkgun_factor_* 0.4 / 0.6, within sharkgun_range 500, in sight).
//!   The shot (0x004a2950): wpn_shark_fire (2D), the muzzle flash, the shark launched at the target (0x0047dbf0 with its
//!   target, ref_hit_00). One shark a clip, one spare (sharkgun_max_clip / _max_ammo).
//! - Launch (0x0047f7e0): lobbed (speed 50 across, under gravity: 0x00471a00) at a point initial_arc_distance (20)
//!   ahead along the shot, x / y jittered by up to 5; its target is told (0x0047f920: the 0x2000 message: the AI stands
//!   still, frozen, on guard against it: ai_04.cpp:274); the loop wpn_armed_jaws_loop runs on it.
//! - Into the ground (0x0047fc90, not dived yet): when its path from the last frame meets the world (or it falls below
//!   -800) it dives there: the missile hidden, 10 below the hit, the fin (landshark_fin) attached, depth -10,
//!   wpn_shark_dive.
//! - Swimming: kept on the ground under it (Object_ForceGround) at its depth; with a target it heads for it at 50 a
//!   second (the pull (1 - d / d0) x 100 + 0.5, d0 the distance at the dive), rising to the fin's depth (-3, 16 a second)
//!   while farther than submerge_attack_distance (40) and sinking under (-10, 20 a second) for the attack; within 3
//!   (across) it bites: a landshark rises at the victim playing sharkbite_grunt / sharkbite_captain (its notes the
//!   fx_shark_attack sounds), the victim is killed outright (the 0x1000 message: anim 0x58 grunt_sharkbite /
//!   captain_sharkbite, its scream notes), the loop stops, the shark waits hidden under the bite until the bite is
//!   over, then dives again (depth -10, the fin back, the loop again while bites < max_kills (6), else its time is up).
//!   Without a target (and time left, lifetime 15 s) it looks for the nearest AI that can be bitten within the range of
//!   the player and in his sight (0x0047fa70), and meanwhile circles him at 15..30 (pi / 2 a second, either way) or
//!   swims to that circle from farther than 30. Its time up and no target: it sinks (7 a second) and is gone below -10.
//!   The fin shows above the ground only near the surface depth.

use super::targeting::can_be_bitten;
use super::{GunFired, WeaponId};
use crate::physics::Physics;
use bevy::prelude::*;

/// The sharks in the world (+0x588 .. +0x5b0 of the projectile).
#[derive(Component, Debug)]
pub struct LandShark {
    pub phase: Phase,
    pub vel: Vec3,
    /// +0x544: the character it hunts (frozen while hunted)
    pub target: Option<Entity>,
    /// +0x568: the distance to the target when it was taken (the pull's scale)
    initial: f32,
    /// +0x58c: how far below the ground it swims (-10 dived, -3 the fin out)
    pub depth: f32,
    /// +0x589: it fell below the world (-800) before diving: it stays at that height
    below_world: bool,
    /// +0x464: lifetime left (counts only while swimming)
    pub life: f32,
    /// +0x59c / +0x598: bites so far, max_kills
    pub bites: u32,
    max_kills: u32,
    /// +0x594 submerge_attack_distance
    submerge: f32,
    /// +0x5a8 angle, +0x5ac radius (15..30), +0x590 direction (> 1: one way) of the circling
    circle: (f32, f32, f32),
    /// the fin (+0x5a0) and the missile's own model
    fin: Option<Entity>,
    model: Option<Entity>,
    /// its loop sound (+0x5b0)
    jaws: Option<crate::audio::SoundId>,
    /// the shark gun's range (vt+0x144: sharkgun_range), its search reach
    range: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Phase {
    /// lobbed, not in the ground yet
    Arc,
    Swim,
    /// under a bite (+0x5a4): the landshark playing, seconds left
    Bite { bite: Entity, left: f32 },
}

/// A character a shark hunts (the 0x2000 message: frozen on guard, physics off, its target the shark).
#[derive(Component, Debug)]
pub struct Hunted {
    pub shark: Entity,
}

/// The landshark bite object rising at its victim: the anim it plays once.
#[derive(Component)]
struct Bite {
    anim: &'static str,
    started: bool,
}

/// The shark's constants (0x0047fc90; the floats read from the exe).
mod k {
    /// the dive below the hit point and the depth then (0x005bcb68, -10.0)
    pub const DIVE: f32 = 10.0;
    pub const DEEP: f32 = -10.0;
    /// the fin's depth (0x005bcf44)
    pub const SURFACE: f32 = -3.0;
    /// below this it counts as fallen out of the world (0x005bcf50)
    pub const FLOOR: f32 = -800.0;
    /// the bite's reach across (0x005bcb7c)
    pub const BITE: f32 = 3.0;
    /// the circle's reach squared (0x005bcf4c) and its turn rate (0x005bce6c)
    pub const CIRCLE_SQ: f32 = 900.0;
    pub const CIRCLE_RATE: f32 = std::f32::consts::FRAC_PI_2;
    /// rising, sinking for the attack, sinking away (0x005bce84, 0x005bcbe8, 0x005bcde8)
    pub const RISE: f32 = 16.0;
    pub const ATTACK_SINK: f32 = 20.0;
    pub const AWAY_SINK: f32 = 7.0;
    /// the swim speed (+0x460 = 50 at the launch, 0x0047f7e0)
    pub const SPEED: f32 = 50.0;
}

pub struct LandSharkPlugin;

impl Plugin for LandSharkPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (fire, swim, bites, hunted).chain().after(crate::player::PlayerSet::Weapons).run_if(crate::playing));
    }
}

/// The pull toward the target (0x0047fc90): (1 - min(d, d0) / d0) x 100 + 0.5.
pub fn pull(dist: f32, initial: f32) -> f32 {
    let d0 = initial.max(1e-3);
    (1.0 - dist.min(d0) / d0) * 100.0 + 0.5
}

/// The new depth for a frame (0x0047fc90's height block): sinking away when its time is up without a target (gone
/// below -10: None), rising to the surface while far (or without a target), sinking for the attack when close (back to
/// -10 under it).
pub fn next_depth(depth: f32, dt: f32, out_of_time: bool, dist: f32, submerge: f32) -> Option<f32> {
    if out_of_time {
        let d = depth - dt * k::AWAY_SINK;
        return (d >= k::DEEP).then_some(d);
    }
    if dist > submerge || dist.is_nan() {
        return Some((depth + dt * k::RISE).min(k::SURFACE));
    }
    let d = depth - dt * k::ATTACK_SINK;
    Some(if d < k::DEEP { k::DEEP } else { d })
}

#[allow(clippy::too_many_arguments)]
fn fire(
    mut commands: Commands,
    mut fired: MessageReader<GunFired>,
    data: Res<crate::gamedata::GameData>,
    targets: Res<super::targeting::Targets>,
    mut kit: crate::army::ModelKit,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut rng: ResMut<crate::fx::FxStats>,
    (roman, mut grunts): (Query<&crate::player::Roman>, Query<&mut crate::ai::Grunt>),
) {
    for f in fired.read().filter(|f| f.id == WeaponId::Sharkgun) {
        let Some(gun) = roman.single().ok().and_then(|r| r.arsenal.gun(WeaponId::Sharkgun)) else { continue };
        let target = targets.first();
        sounds.write(crate::audio::PlaySound::ui("wpn_shark_fire"));
        let p = data.props(&gun.spec.projectile);
        // the arc's point: 20 ahead along the shot (to the target's point), jittered by up to 5 across
        let aim = target.map_or(f.aim, |t| t.point);
        let dir = (aim - f.muzzle).normalize_or(Vec3::NEG_Z);
        let arc = p.f("initial_arc_distance", 20.0);
        let to = f.muzzle + dir * arc + Vec3::new(rng.range(-5.0, 5.0), 0.0, rng.range(-5.0, 5.0));
        let vel = crate::vehicles::rockets::lob(f.muzzle, to, k::SPEED);
        let tr = Transform::from_translation(f.muzzle).with_rotation(crate::vehicles::rockets::facing(vel));
        let template = gun.spec.gun.projectile_name.clone();
        let model = data.templates.get(&template).and_then(|t| t.model.clone()).and_then(|m| kit.spawn(&mut commands, &m, Transform::IDENTITY, true));
        let jaws = crate::audio::SoundId::new();
        let e = commands
            .spawn((
                tr,
                Visibility::default(),
                LandShark {
                    phase: Phase::Arc,
                    vel,
                    target: target.map(|t| t.entity),
                    initial: 0.0,
                    // the load (0x0047f6d0): -3 until the dive sets -10; the circle's radius 15..30
                    depth: k::SURFACE,
                    below_world: false,
                    life: p.f("lifetime", 15.0),
                    bites: 0,
                    max_kills: p.i("max_kills", 0).max(0) as u32,
                    submerge: p.f("submerge_attack_distance", 40.0),
                    circle: (0.0, rng.range(15.0, 30.0), 0.0),
                    fin: None,
                    model,
                    jaws: Some(jaws),
                    range: gun.spec.range,
                },
            ))
            .id();
        if let Some(m) = model {
            commands.entity(e).add_child(m);
        }
        sounds.write(crate::audio::PlaySound { id: Some(jaws), ..crate::audio::PlaySound::on("wpn_armed_jaws_loop", e).looping(true) });
        // 0x0047f920: the target is told (frozen on guard)
        if let Some(t) = target {
            hunt(&mut commands, &mut grunts, t.entity, e);
        }
        info!("EVENT shark: launched at {:?}", target.map(|t| t.entity));
    }
}

/// The 0x2000 message (GenericAIObject_Slot52 0x00430340): the AI stands still on guard (0x0042d0f0, physics frozen)
/// with the shark as its target.
fn hunt(commands: &mut Commands, grunts: &mut Query<&mut crate::ai::Grunt>, victim: Entity, shark: Entity) {
    if let Ok(mut g) = grunts.get_mut(victim) {
        g.target = None;
        g.mind = crate::ai::Mind::WaitTrigger;
        commands.entity(victim).insert(Hunted { shark });
    }
}

/// The shark's update (0x0047fc90).
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn swim(
    mut commands: Commands,
    time: Res<Time>,
    physics: NonSend<Physics>,
    mut sharks: Query<(Entity, &mut LandShark, &mut Transform)>,
    mut grunts: Query<(Entity, &mut crate::ai::Grunt, &GlobalTransform, &mut crate::fx::Health, Option<&crate::physics::Body>), Without<LandShark>>,
    roman: Query<&GlobalTransform, With<crate::player::Roman>>,
    mut kit: crate::army::ModelKit,
    data: Res<crate::gamedata::GameData>,
    (mut sounds, mut stop): (MessageWriter<crate::audio::PlaySound>, MessageWriter<crate::audio::StopVoice>),
    (mut rng, mut stats, mut log): (ResMut<crate::fx::FxStats>, ResMut<crate::player::PlayerStats>, ResMut<super::WeaponStats>),
    (gone, carried): (Query<(), With<Bite>>, Query<(), With<super::blackhole::Carried>>),
) {
    let dt = time.delta_secs();
    if dt <= 0.0 {
        return;
    }
    let player = roman.single().ok().map(|t| t.translation());
    for (e, mut s, mut t) in &mut sharks {
        // under a bite: nothing until it is over; then down again (0x0047fc90's first block)
        if let Phase::Bite { bite, left } = s.phase {
            if left - dt > 0.0 && gone.contains(bite) {
                s.phase = Phase::Bite { bite, left: left - dt };
                continue;
            }
            commands.entity(bite).try_despawn();
            s.phase = Phase::Swim;
            s.depth = k::DEEP;
            s.vel = Vec3::ZERO;
            if let Some(fin) = s.fin {
                commands.entity(fin).insert(Visibility::Inherited);
            }
            if s.bites < s.max_kills {
                let id = crate::audio::SoundId::new();
                s.jaws = Some(id);
                sounds.write(crate::audio::PlaySound { id: Some(id), ..crate::audio::PlaySound::on("wpn_armed_jaws_loop", e).looping(true) });
            } else {
                s.life = 0.0;
            }
        }
        // the target dropped when it is dead (Object_IsDead 0x00454780)
        // (or carried off by a black hole meanwhile)
        if s.target.is_some_and(|g| carried.contains(g) || !grunts.get(g).is_ok_and(|x| x.1.mind != crate::ai::Mind::Dead)) {
            s.target = None;
        }
        let pos = t.translation;
        if s.phase == Phase::Arc {
            // lobbed under gravity until its path meets the world (or it is below the floor)
            s.vel.y -= crate::vehicles::rockets::GRAVITY * dt;
            let step = s.vel * dt;
            let hit = physics.raycast(pos, step.normalize_or(Vec3::NEG_Y), step.length().max(0.01)).filter(|(b, _, _)| !grunts.iter().any(|g| g.4.is_some_and(|gb| gb.id == *b)));
            let below = pos.y + step.y < k::FLOOR;
            if hit.is_none() && !below {
                t.translation = pos + step;
                t.rotation = crate::vehicles::rockets::facing(s.vel);
                continue;
            }
            // the dive: at the hit, the missile hidden, 10 under, the fin on it, depth -10, wpn_shark_dive
            let at = hit.map_or(pos + step, |h| h.1);
            s.below_world = below;
            s.phase = Phase::Swim;
            s.vel = Vec3::ZERO;
            s.depth = k::DEEP;
            t.translation = at - Vec3::Y * k::DIVE;
            // (its frame as the projectile's: the model's -Y along its heading, game Z up; the fin rides on it)
            t.rotation = crate::vehicles::rockets::facing(s.vel.with_y(0.0).normalize_or(Vec3::NEG_Z));
            if let Some(m) = s.model.take() {
                commands.entity(m).despawn();
            }
            if s.fin.is_none() {
                if let Some(fin) = data.templates.get("landshark_fin").and_then(|t| t.model.clone()).and_then(|m| kit.spawn(&mut commands, &m, Transform::IDENTITY, true)) {
                    commands.entity(fin).insert(crate::level::DefaultAnim::new("player_landsharkfin_0"));
                    commands.entity(e).add_child(fin);
                    s.fin = Some(fin);
                }
            }
            sounds.write(crate::audio::PlaySound::at("wpn_shark_dive", at));
            log.shark_dives += 1;
            info!("EVENT shark: dived");
            continue;
        }
        // no target and time left: look for one (0x0047fa70)
        if s.target.is_none() && s.life > 0.0 {
            if let Some(p) = player {
                let eye = p + Vec3::Y * 6.0;
                let best = grunts
                    .iter()
                    .filter(|(ge, g, _, _, _)| g.mind != crate::ai::Mind::Dead && can_be_bitten(g) && !carried.contains(*ge))
                    .map(|(ge, _, gt, _, _)| (ge, gt.translation() + Vec3::Y * 5.0))
                    .filter(|(_, c)| c.distance(eye) <= s.range)
                    .filter(|(ge, c)| super::targeting::in_sight(&physics, eye, *c, *ge, &|_| false))
                    .min_by(|a, b| a.1.distance(eye).total_cmp(&b.1.distance(eye)));
                if let Some((ge, _)) = best {
                    s.target = Some(ge);
                    s.initial = 0.0;
                    if let Ok((_, mut g, _, _, _)) = grunts.get_mut(ge) {
                        g.target = None;
                        g.mind = crate::ai::Mind::WaitTrigger;
                        commands.entity(ge).insert(Hunted { shark: e });
                    }
                    info!("EVENT shark: found {ge:?}");
                }
            }
        }
        // where it heads: its target (bites within 3 across), else round the player
        let (goal, dist, pull_k, min_speed) = match s.target.and_then(|g| grunts.get(g).ok().map(|x| (g, x.2.translation()))) {
            Some((ge, gp)) => {
                let across = (gp - pos).with_y(0.0).length();
                if s.initial <= 0.0 {
                    s.initial = across.max(1e-3);
                }
                if across < k::BITE {
                    bite(&mut commands, &mut s, e, ge, &mut grunts, &mut kit, &data, &mut stop, &mut stats);
                    log.shark_bites += 1;
                    t.translation = gp;
                    continue;
                }
                (gp, across, pull(across, s.initial), k::SPEED)
            }
            None => {
                let Some(p) = player else { continue };
                let d = (pos - p).with_y(0.0);
                let (mut a, r, way) = s.circle;
                if d.length_squared() > k::CIRCLE_SQ {
                    // from afar: to the circle's nearest point, a new way round picked
                    a = d.z.atan2(d.x);
                    s.circle = (a, r, rng.rand() * 2.0);
                } else {
                    a += if way > 1.0 { dt } else { -dt } * k::CIRCLE_RATE;
                    s.circle = (a, r, way);
                }
                (p + Vec3::new(a.cos(), 0.0, a.sin()) * r, f32::INFINITY, 10.0, 5.0)
            }
        };
        // steering across (0x00471970 / 0x00471750 / 0x00455a30): the wanted velocity toward the goal, its length kept
        // between the speeds, pulled in at pull_k a second
        let to = (goal - pos).with_y(0.0);
        let want = to.normalize_or_zero() * to.length().clamp(min_speed, k::SPEED);
        let dv = (want - s.vel.with_y(0.0)) * (pull_k * dt).min(1.0);
        s.vel = (s.vel + dv).with_y(0.0);
        let mut next = pos + s.vel * dt;
        // the height: the ground under it (Object_ForceGround 0x00455860) plus its depth; out of the world: -800
        s.life -= dt;
        let out_of_time = s.target.is_none() && s.life <= 0.0;
        let Some(depth) = next_depth(s.depth, dt, out_of_time, dist, s.submerge) else {
            remove(&mut commands, e, &s, &mut stop);
            log.sharks_gone += 1;
            info!("EVENT shark: gone ({} bites)", s.bites);
            continue;
        };
        s.depth = depth;
        let ground = if s.below_world { Some(k::FLOOR) } else { ground_under(&physics, next, pos.y - s.depth) };
        if let Some(g) = ground {
            next.y = g + s.depth;
        }
        if s.vel.length_squared() > 1e-4 {
            t.rotation = crate::vehicles::rockets::facing(s.vel.with_y(0.0));
        }
        t.translation = next;
    }
}

/// The ground under a point (a ray down from above the last ground height), as Object_ForceGround's.
fn ground_under(physics: &Physics, at: Vec3, last_ground: f32) -> Option<f32> {
    let from = Vec3::new(at.x, last_ground + 20.0, at.z);
    physics.raycast(from, Vec3::NEG_Y, 60.0).map(|h| h.1.y)
}

/// The bite (0x0047fc90 -> 0x0047f600): bites + 1, the landshark at the victim (its matrix) playing the bite for the
/// victim's kind, the victim killed outright (0x1000: anim 0x58), the loop stopped, the shark hidden under it.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn bite(
    commands: &mut Commands,
    s: &mut LandShark,
    shark: Entity,
    victim: Entity,
    grunts: &mut Query<(Entity, &mut crate::ai::Grunt, &GlobalTransform, &mut crate::fx::Health, Option<&crate::physics::Body>), Without<LandShark>>,
    kit: &mut crate::army::ModelKit,
    data: &crate::gamedata::GameData,
    stop: &mut MessageWriter<crate::audio::StopVoice>,
    stats: &mut crate::player::PlayerStats,
) {
    s.bites += 1;
    s.target = None;
    if let Some(id) = s.jaws.take() {
        stop.write(crate::audio::StopVoice { id, immediate: false });
    }
    if let Some(fin) = s.fin {
        commands.entity(fin).insert(Visibility::Hidden);
    }
    let Ok((_, mut g, gt, mut h, _)) = grunts.get_mut(victim) else { return };
    // "sharkbite_grunt" when the victim's name holds "grunt", else "sharkbite_captain"; the victim's own anim 0x58
    let captain = g.kind.captain_like();
    let (bite_anim, death_anim) = if captain { ("sharkbite_captain", "captain_sharkbite") } else { ("sharkbite_grunt", "grunt_sharkbite") };
    let len = kit.anim(bite_anim).map_or(3.3, |a| a.duration);
    let (_, rot, at) = gt.to_scale_rotation_translation();
    let bite = data
        .templates
        .get("landshark")
        .and_then(|t| t.model.clone())
        .and_then(|m| kit.spawn(commands, &m, Transform::from_translation(at).with_rotation(rot), true))
        .unwrap_or_else(|| commands.spawn((Transform::from_translation(at), Visibility::default())).id());
    commands.entity(bite).insert(Bite { anim: bite_anim, started: false });
    s.phase = Phase::Bite { bite, left: len };
    // killed outright (no ragdoll, the bite's own death anim)
    commands.entity(victim).remove::<Hunted>().insert(crate::ai::DeathAnim(death_anim));
    g.cartoon = false;
    h.0 = h.0.min(0.0) - 1.0;
    stats.kills += 1;
    info!("EVENT shark: bite {} on {victim:?} by {shark:?}", s.bites);
}

/// Removed (0x0047f980): the fin and any bite gone, the loop stopped.
fn remove(commands: &mut Commands, e: Entity, s: &LandShark, stop: &mut MessageWriter<crate::audio::StopVoice>) {
    if let Some(id) = s.jaws {
        stop.write(crate::audio::StopVoice { id, immediate: false });
    }
    if let Phase::Bite { bite, .. } = s.phase {
        commands.entity(bite).try_despawn();
    }
    commands.entity(e).despawn();
}

/// The landshark bite plays its anim once from its first frame.
fn bites(mut q: Query<(&mut Bite, &mut crate::skin::Animator)>) {
    for (mut b, mut a) in &mut q {
        if !b.started {
            b.started = true;
            a.play(0, b.anim, false, crate::skin::Mask::All);
        }
    }
}

/// The hunted stand still facing their shark until it bites or is gone.
fn hunted(mut commands: Commands, mut q: Query<(Entity, &Hunted, &mut crate::ai::Grunt, &GlobalTransform)>, sharks: Query<(&LandShark, &GlobalTransform)>) {
    for (e, h, mut g, gt) in &mut q {
        // still its shark's target?
        let Some(st) = sharks.get(h.shark).ok().filter(|(s, _)| s.target == Some(e)).map(|(_, t)| t.translation()) else {
            commands.entity(e).remove::<Hunted>();
            if g.mind == crate::ai::Mind::WaitTrigger {
                g.mind = crate::ai::Mind::Chase;
            }
            continue;
        };
        g.pause = 1.0;
        g.mind = crate::ai::Mind::WaitTrigger;
        let d = (st - gt.translation()).with_y(0.0);
        if d.length_squared() > 1.0 {
            g.yaw = d.x.atan2(d.z);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The pull: 0.5 at the start, 100.5 at the target.
    #[test]
    fn pull_grows_as_it_closes() {
        assert!((pull(100.0, 100.0) - 0.5).abs() < 1e-5);
        assert!((pull(0.0, 100.0) - 100.5).abs() < 1e-4);
        assert!((pull(50.0, 100.0) - 50.5).abs() < 1e-4);
    }

    /// The depth: rising to -3 while far, sinking to -10 (and held) when close, sinking away and gone when its time is
    /// up.
    #[test]
    fn depth_rules() {
        assert_eq!(next_depth(-10.0, 0.1, false, 100.0, 40.0), Some(-8.4));
        assert_eq!(next_depth(-3.5, 0.1, false, 100.0, 40.0), Some(-3.0));
        assert_eq!(next_depth(-3.0, 0.1, false, 10.0, 40.0), Some(-5.0));
        assert_eq!(next_depth(-9.5, 0.1, false, 10.0, 40.0), Some(-10.0));
        assert_eq!(next_depth(-9.5, 0.1, true, 10.0, 40.0), None);
        assert!(next_depth(-3.0, 0.1, true, 10.0, 40.0).is_some_and(|d| (d + 3.7).abs() < 1e-5));
    }
}
