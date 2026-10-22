//! The world's smallest black hole (special 9: the box player_blackhole_box, BlackHoleBox vtable 0x005a2940, and the
//! hole player_blackhole, BlackHoleProjectile 0x005ad858).
//!
//! - Fire Special with it in hand while no box or hole is live (Game+0xadc, 0x004788a0's case 9) queues the throw; at
//!   the throw note the box leaves the hand (0x004a1970: one less, "wpn_black_hole_fire").
//! - The box (0x004821b0 launch, 0x004822b0 update): lobbed at 50 across toward the camera's facing x
//!   initial_arc_distance (20) ahead, showing the first frame of player_blackhole_box_open (Anim_Set flags 0x90: 0x10
//!   holds the slot's time); where its path meets the world it lands (put on the ground) and opens (the anim restarted,
//!   0x80); when the opening is over the hole appears at it. lifetime 14 s.
//! - The hole (0x00481db0 load, 0x00481ef0 update): rises final_height (40) above where it appeared, easing 5 % of the
//!   way a frame (hidden when it gets there; its fx_explosion_black_hole runs on); start_delay (2 s) later every AI
//!   whose bounding sphere comes within damage_radius_outer (200) of it is carried off (AreaDamage_Apply 0x0047f020
//!   with record flag 0x800, the hole as the holder: GenericAIObject_Slot52 0x00430340) and the hole joins the
//!   suction list World+0xf00 (from the next frame on it counts down suction_lifetime, 6 s, and leaves the list at 0).
//!   lifetime 14 s, then gone (0x004820b0: its own area damage record, area_damage / damage_radius_inner / _outer /
//!   impact_force, when area_damage is not 0). Only grunt-class characters are carried off here: the 0x800 record
//!   reaches every object of the list and each one's vt+0xd0 decides (not decoded for the other classes).
//! - The carried off (ai_04.cpp:179, ai_06.cpp:3108-3170): first they run from the hole (state 0x19c: 250 away along
//!   the line from it, 1..1.75 s), then they are pulled in (0x19d): across at 1.5 x their speed (pulled at 5 a second),
//!   after 1..2 s lifted too, flailing (anims 0x49 / 0x4a looped); within 5 of the hole, or when the hole is gone,
//!   they are gone (vt+0xd4).
//! - While it sucks: Roman is pulled toward it across, (1 - d / 5000) x 200 a second (5000: its vt+0x144), turning to it
//!   unless he turns himself (0x004788a0 at 0x00479bef), and leans against it standing (trotter_blackhole_pull,
//!   charanim.rs); every projectile is steered at it at 70 a second (Projectile_Update 0x0047e580; player.rs bullets,
//!   vehicles/rockets.rs rockets), and the vindaloo's rockets stop homing (0x004805f0).

use super::{SpecialId, ThrowSpecial};
use crate::physics::Physics;
use bevy::prelude::*;

/// A box or hole in the world (Game+0xadc counts them): no other black hole while one is live.
#[derive(Component)]
pub struct LiveHole;

/// The suction (World+0xf00 not empty): where the first sucking hole is.
#[derive(Resource, Default, Debug, Clone, Copy)]
pub struct Suction {
    pub active: bool,
    pub at: Vec3,
}

#[derive(Component, Debug)]
pub struct HoleBox {
    vel: Vec3,
    life: f32,
    /// opened: seconds since, and whether the hole is out
    open: Option<(f32, bool)>,
    /// in flight showing the open anim's first frame (Anim_Set 0x90 at the launch)
    posed: bool,
    /// the opening's length (player_blackhole_box_open)
    open_len: f32,
}

#[derive(Component, Debug)]
pub struct BlackHole {
    /// where it rises to (+0x598)
    pub target: Vec3,
    /// +0x594 start_delay, +0x590 the suction left, lifetime +0x464
    pub delay: f32,
    pub suction: f32,
    pub life: f32,
    arrived: bool,
    /// its area damage record (+0x474): the carry-off's reach (damage_radius_outer) and the last blast
    blast: AreaRecord,
    model: Option<Entity>,
}

/// The carry-off as other objects take it (AreaDamage_Apply 0x0047f020 with record flag 0x800, the hole the holder
/// +0x30, from the player): every object whose bounding sphere is within `radius` of `at` (the twiglets: twiglet.rs).
#[derive(Message, Clone, Copy, Debug)]
pub struct CarryOff {
    pub hole: Entity,
    pub at: Vec3,
    pub radius: f32,
}

/// A projectile's area damage record (Projectile_Load 0x0047e0c0: +0x474 area_damage, +0x478 / +0x47c the radii,
/// +0x470 impact_force).
#[derive(Clone, Copy, Debug)]
struct AreaRecord {
    damage: f32,
    inner: f32,
    outer: f32,
    impact: f32,
}

/// A character the hole carried off (0x800 message, its holder +0x870).
#[derive(Component, Debug)]
pub struct Carried {
    pub hole: Entity,
    phase: Carry,
    vel: Vec3,
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum Carry {
    /// state 0x19c: running away, seconds left
    Flee(f32),
    /// state 0x19d: pulled in; seconds before the lift
    Pull(f32),
}

/// The constants (read from the exe).
mod k {
    /// the box's throw speed across (0x004821b0: +0x460 = 50)
    pub const THROW_SPEED: f32 = 50.0;
    /// the rise: 5 % of the way each frame (0x005bcc34 / 0x005bce54), at the original's frame rate (guess: 60)
    pub const EASE: f32 = 0.05;
    pub const FRAME_RATE: f32 = 60.0;
    /// the run from the hole (0x0046f6d0 at 250, 0x005bcf00) and its time (0x0046e2c0(1.0, 1.75))
    pub const FLEE: f32 = 250.0;
    /// the pull: 1.5 x the speed (0x005bce20), at 5 a second (0x005bcbdc), swallowed within 5
    pub const PULL_SPEED: f32 = 1.5;
    pub const PULL_RATE: f32 = 5.0;
    pub const SWALLOW: f32 = 5.0;
    /// Roman's pull: reach (0x00461ec0: 5000) and strength (0x005bcda8: 200)
    pub const REACH: f32 = 5000.0;
    pub const STRENGTH: f32 = 200.0;
    /// the projectiles' pull toward it (0x0047e580: 70 a second at a rate of 1)
    pub const PROJECTILE_SPEED: f32 = 70.0;
}

pub struct BlackHolePlugin;

impl Plugin for BlackHolePlugin {
    fn build(&self, app: &mut App) {
        use crate::levelreset::LevelAppExt;
        app.init_level_resource::<Suction>().add_message::<CarryOff>().add_systems(
            Update,
            (throw.after(super::WeaponSet::Throws), boxes, holes, carried, pull_roman, pull_projectiles).chain().after(crate::player::PlayerSet::Weapons).run_if(crate::playing),
        );
    }
}

/// The box leaves the hand (0x004a1970 with 9 -> 0x004821b0).
#[allow(clippy::too_many_arguments)]
fn throw(
    mut commands: Commands,
    mut thrown: MessageReader<ThrowSpecial>,
    mut roman: Query<(&Transform, Option<&crate::skin::Animator>, &mut crate::player::Roman)>,
    joints: Query<&GlobalTransform>,
    cam: Query<&GlobalTransform, With<crate::camera::FollowCam>>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    (mut sounds, mut stats, cheats): (MessageWriter<crate::audio::PlaySound>, ResMut<crate::player::PlayerStats>, Res<crate::cheats::Cheats>),
) {
    if !thrown.read().fold(false, |any, t| any || t.0 == SpecialId::BlackHole) {
        return;
    }
    let (Ok((rt, ra, mut r)), Ok(c)) = (roman.single_mut(), cam.single()) else { return };
    if r.arsenal.count(SpecialId::BlackHole) == 0 {
        return;
    }
    if !cheats.on(crate::cheats::INFINITE_AMMO) {
        r.arsenal.spend(SpecialId::BlackHole);
    }
    stats.shots += 1;
    sounds.write(crate::audio::PlaySound::ui("wpn_black_hole_fire"));
    let from = ra.and_then(|a| a.skeleton.index.get("l_hand").and_then(|&i| joints.get(a.joints[i]).ok())).map_or(rt.translation + Vec3::Y * 8.0, |j| j.translation());
    let p = data.props("p_player_blackhole_box");
    let to = from + c.forward().as_vec3() * p.f("initial_arc_distance", 20.0);
    let vel = crate::vehicles::rockets::lob(from, to, k::THROW_SPEED);
    let tr = Transform::from_translation(from).with_rotation(crate::level::game_to_engine());
    let e = data
        .templates
        .get("player_blackhole_box")
        .and_then(|t| t.model.clone())
        .and_then(|m| kit.spawn(&mut commands, &m, tr, true))
        .unwrap_or_else(|| commands.spawn((tr, Visibility::default())).id());
    let open_len = kit.anim("player_blackhole_box_open").map_or(1.0, |a| a.duration);
    commands.entity(e).insert((LiveHole, HoleBox { vel, life: p.f("lifetime", 14.0), open: None, posed: false, open_len }));
    info!("EVENT blackhole: box thrown");
}

/// The box: in flight, landed and opening, the hole out (0x004822b0).
fn boxes(
    mut commands: Commands,
    time: Res<Time>,
    physics: NonSend<Physics>,
    mut q: Query<(Entity, &mut HoleBox, &mut Transform, Option<&mut crate::skin::Animator>)>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    let dt = time.delta_secs();
    for (e, mut b, mut t, mut anim) in &mut q {
        b.life -= dt;
        if b.life <= 0.0 {
            commands.entity(e).despawn();
            continue;
        }
        match b.open {
            None => {
                // the launch's Anim_Set (0x90): the open anim held at its first frame while it flies
                if let (false, Some(a)) = (b.posed, anim.as_deref_mut()) {
                    a.play(0, "player_blackhole_box_open", false, crate::skin::Mask::All);
                    if let Some(l) = a.layers.get_mut(0) {
                        l.speed = 0.0;
                    }
                    b.posed = true;
                }
                b.vel.y -= crate::vehicles::rockets::GRAVITY * dt;
                let step = b.vel * dt;
                let from = t.translation;
                match physics.raycast(from, step.normalize_or(Vec3::NEG_Y), step.length().max(0.01)) {
                    Some((_, at, _)) => {
                        // landed: on the ground (Object_ForceGround 5, 10), opening (Anim_Set 0x80: once)
                        t.translation = physics.raycast(at + Vec3::Y * 5.0, Vec3::NEG_Y, 15.0).map_or(at, |h| h.1);
                        b.vel = Vec3::ZERO;
                        b.open = Some((0.0, false));
                        if let Some(mut a) = anim {
                            a.play(0, "player_blackhole_box_open", false, crate::skin::Mask::All);
                            a.restart(0);
                            if let Some(l) = a.layers.get_mut(0) {
                                l.speed = 1.0;
                            }
                        }
                        info!("EVENT blackhole: box landed");
                    }
                    None => t.translation = from + step,
                }
            }
            Some((tt, out)) => {
                let tt = tt + dt;
                b.open = Some((tt, out));
                if !out && tt >= b.open_len {
                    b.open = Some((tt, true));
                    spawn_hole(&mut commands, &data, &mut kit, &mut fx, *t);
                }
            }
        }
    }
}

/// The hole at the box (0x00481db0): its target final_height above.
fn spawn_hole(commands: &mut Commands, data: &crate::gamedata::GameData, kit: &mut crate::army::ModelKit, fx: &mut MessageWriter<crate::fx::StartFx>, at: Transform) {
    let p = data.props("p_player_blackhole");
    let model = data.templates.get("player_blackhole").and_then(|t| t.model.clone()).and_then(|m| kit.spawn(commands, &m, Transform::IDENTITY, false));
    let e = commands
        .spawn((
            Transform::from_translation(at.translation).with_rotation(at.rotation),
            Visibility::default(),
            LiveHole,
            BlackHole {
                target: at.translation + Vec3::Y * p.f("final_height", 40.0),
                delay: p.f("start_delay", 2.0),
                suction: p.f("suction_lifetime", 10.0),
                life: p.f("lifetime", 4.0),
                arrived: false,
                blast: AreaRecord {
                    damage: p.f("area_damage", 0.0),
                    inner: p.f("damage_radius_inner", 0.0),
                    outer: p.f("damage_radius_outer", 0.0),
                    impact: p.f("impact_force", 0.0),
                },
                model,
            },
        ))
        .id();
    if let Some(m) = model {
        commands.entity(m).insert(crate::level::DefaultAnim::new("player_blackhole_ref"));
        commands.entity(e).add_child(m);
    }
    if let Some(name) = p.s("fx_start") {
        fx.write(crate::fx::StartFx { name: name.to_owned(), at: at.translation, rotation: Quat::IDENTITY, follow: Some(e), position_only: true });
    }
    info!("EVENT blackhole: the hole is open");
}

/// The hole (0x00481ef0): the rise, the start delay and the carry-off, the suction's time, the end.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn holes(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut BlackHole, &mut Transform)>,
    mut grunts: Query<(Entity, &mut crate::ai::Grunt, &GlobalTransform, Option<&super::targeting::HudSphere>), Without<Carried>>,
    mut suction: ResMut<Suction>,
    mut rng: ResMut<crate::fx::FxStats>,
    (mut blasts, mut carry): (MessageWriter<crate::fx::Explode>, MessageWriter<CarryOff>),
    (mut log, mut stats): (Option<ResMut<crate::mission::Mission>>, ResMut<super::WeaponStats>),
) {
    let dt = time.delta_secs();
    *suction = Suction::default();
    for (e, mut h, mut t) in &mut q {
        h.life -= dt;
        if h.life <= 0.0 {
            // 0x004820b0: its area damage record as it goes, unless area_damage is 0
            let b = h.blast;
            if b.damage != 0.0 {
                blasts.write(crate::fx::Explode { at: t.translation, area_damage: b.damage, radius_inner: b.inner, radius_outer: b.outer, impact_force: b.impact, source: Some(e), delay: 0.0, by: crate::fx::Src::Player, cartoon: false, kind: crate::player::DamageKind::Plain });
            }
            commands.entity(e).despawn();
            info!("EVENT blackhole: closed");
            continue;
        }
        let was_near = t.translation.distance_squared(h.target) < 4.0;
        let k = 1.0 - (1.0 - k::EASE).powf(dt * k::FRAME_RATE);
        t.translation = t.translation.lerp(h.target, k);
        // arrived: the hole's own model hidden (vt+0xf4(0)); its fx runs on
        if !h.arrived && !was_near && t.translation.distance_squared(h.target) < 4.0 {
            h.arrived = true;
            if let Some(m) = h.model {
                commands.entity(m).insert(Visibility::Hidden);
            }
        }
        if h.delay > 0.0 {
            h.delay -= dt;
            if h.delay > 0.0 {
                continue;
            }
            // the carry-off (AreaDamage_Apply 0x0047f020 with flag 0x800, the hole the holder): the AIs whose bounding
            // sphere (vt+0x94: targeting::bounds) reaches within the outer radius of +0x598 and that
            // can be carried (vt+0x254: the ones on their feet; guess: as the land shark's prey)
            let mut n = 0;
            for (ge, mut g, gt, sphere) in &mut grunts {
                let (centre, slack) = super::targeting::bounds(gt, sphere);
                let reach = centre.distance(h.target) - slack;
                if g.mind == crate::ai::Mind::Dead || !super::targeting::on_their_feet(&g) || reach > h.blast.outer {
                    continue;
                }
                let away = (gt.translation() - h.target).with_y(0.0).normalize_or(Vec3::X);
                let speed = g.move_speed;
                crate::ai::wander_to(&mut g, gt.translation(), gt.translation() + away * k::FLEE, speed);
                g.target = None;
                commands.entity(ge).insert(Carried { hole: e, phase: Carry::Flee(rng.range(1.0, 1.75)), vel: Vec3::ZERO });
                n += 1;
            }
            carry.write(CarryOff { hole: e, at: h.target, radius: h.blast.outer });
            if let Some(m) = log.as_deref_mut() {
                m.log.push(format!("black hole: {n} carried off"));
            }
            stats.holes += 1;
            stats.carried += n;
            info!("EVENT blackhole: sucking, {n} carried off");
            // it joins the suction list and returns: the countdown starts next frame
            if !suction.active {
                *suction = Suction { active: true, at: t.translation };
            }
            continue;
        }
        // in the list while the suction left after this frame's countdown is above 0
        if h.suction > 0.0 {
            h.suction -= dt;
            if h.suction > 0.0 && !suction.active {
                *suction = Suction { active: true, at: t.translation };
            }
        }
    }
}

/// The carried off: the run, the pull, swallowed (ai_06.cpp:3108-3170).
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn carried(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<Physics>,
    mut q: Query<(Entity, &mut Carried, &mut crate::ai::Grunt, &mut Transform, &mut crate::fx::Health, Option<&crate::physics::Body>)>,
    holes: Query<&Transform, (With<BlackHole>, Without<Carried>)>,
    mut rng: ResMut<crate::fx::FxStats>,
    (mut stats, mut log): (ResMut<crate::player::PlayerStats>, ResMut<super::WeaponStats>),
) {
    let dt = time.delta_secs();
    for (e, mut c, mut g, mut t, mut h, body) in &mut q {
        // died meanwhile (a shark's bite, a bullet): no longer the hole's
        if g.mind == crate::ai::Mind::Dead {
            commands.entity(e).remove::<(Carried, crate::ai::AnimOverride)>();
            continue;
        }
        let Ok(hole) = holes.get(c.hole).map(|h| h.translation) else {
            // the hole is gone: so is he
            swallow(&mut commands, e, &mut g, &mut h, &mut stats);
            log.swallowed += 1;
            continue;
        };
        match c.phase {
            Carry::Flee(left) => {
                let left = left - dt;
                c.phase = if left > 0.0 { Carry::Flee(left) } else { Carry::Pull(rng.range(1.0, 2.0)) };
                if left <= 0.0 {
                    // the pull: out of the AI's hands, off the physics
                    if let Some(b) = body {
                        physics.remove(b.id);
                        commands.entity(e).remove::<crate::physics::Body>();
                    }
                    g.mind = crate::ai::Mind::WaitTrigger;
                }
            }
            Carry::Pull(lift) => {
                g.pause = 1.0;
                g.mind = crate::ai::Mind::WaitTrigger;
                let lift = lift - dt;
                let to = hole - t.translation;
                if to.length() < k::SWALLOW {
                    swallow(&mut commands, e, &mut g, &mut h, &mut stats);
                    log.swallowed += 1;
                    continue;
                }
                let dir = to.normalize_or_zero();
                let speed = g.move_speed * k::PULL_SPEED;
                // across from the start, up too once the lift's time is over (and the flailing then)
                let mut want = Vec3::new(dir.x, 0.0, dir.z) * speed;
                if lift <= 0.0 {
                    want.y = dir.y * speed;
                    if lift + dt > 0.0 {
                        let id = if rng.rand() < 0.5 { 0x49 } else { 0x4a };
                        commands.entity(e).insert(crate::ai::AnimOverride { id, looping: true });
                    }
                }
                let dv = (want - c.vel) * (k::PULL_RATE * dt).min(1.0);
                c.vel += dv;
                t.translation += c.vel * dt;
                c.phase = Carry::Pull(lift);
            }
        }
    }
}

/// Gone into the hole (vt+0xd4): dead, removed with what he holds.
fn swallow(commands: &mut Commands, e: Entity, g: &mut crate::ai::Grunt, h: &mut crate::fx::Health, stats: &mut crate::player::PlayerStats) {
    h.0 = 0.0;
    g.mind = crate::ai::Mind::Dead;
    g.dead_said = true;
    stats.kills += 1;
    commands.entity(e).remove::<(Carried, crate::ai::AnimOverride)>().insert((Visibility::Hidden, crate::corpses::Corpse::gone()));
    info!("EVENT blackhole: swallowed {e:?}");
}

/// Roman pulled toward the sucking hole (0x004788a0 at 0x00479bef): (1 - d / 5000) x 200 a second across, and turned
/// toward it when he is not turning himself and faces away (dot < 0.9: (dot - 0.9) x 0.0625 as his turn input).
fn pull_roman(time: Res<Time>, suction: Res<Suction>, mut q: Query<(&mut crate::player::Roman, &Transform)>, (ctl, look): (Res<crate::input::Controls>, Res<crate::player::LookDelta>)) {
    if !suction.active {
        return;
    }
    let dt = time.delta_secs();
    for (mut r, t) in &mut q {
        if r.dead {
            continue;
        }
        let to = suction.at - t.translation;
        let d = to.length();
        if d > k::REACH || d <= 0.0 {
            continue;
        }
        let across = to.with_y(0.0).normalize_or_zero();
        let push = across * (1.0 - d / k::REACH) * k::STRENGTH * dt;
        r.vel += push;
        if ctl.get(crate::input::Act::Look).value.x.abs() < 1e-3 && look.0.x.abs() < 1e-3 {
            let facing = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
            let dot = facing.dot(across);
            if dot < 0.9 {
                // which way: the side the hole is on
                let side = facing.cross(across).y.signum();
                r.yaw += side * (0.9 - dot) * 0.0625 * 10.0 * dt;
            }
        }
    }
}

/// Every projectile steered at the sucking hole (Projectile_Update 0x0047e580: toward it at 70, rate 1).
fn pull_projectiles(time: Res<Time>, suction: Res<Suction>, mut bullets: Query<(&mut crate::player::Projectile, &Transform)>, mut rockets: Query<(&mut crate::vehicles::rockets::Rocket, &Transform), Without<crate::player::Projectile>>) {
    if !suction.active {
        return;
    }
    let k = time.delta_secs().min(1.0);
    let want = |at: Vec3| (suction.at - at).normalize_or_zero() * k::PROJECTILE_SPEED;
    for (mut p, t) in &mut bullets {
        let dv = (want(t.translation) - p.vel) * k;
        p.vel += dv;
    }
    for (mut r, t) in &mut rockets {
        let dv = (want(t.translation) - r.vel) * k;
        r.vel += dv;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The rise: 5 % a frame at 60 frames a second, whatever the step: a second of 60 steps and of 30 double steps end
    /// at the same point, 95.4 % of the way.
    #[test]
    fn rise_is_frame_rate_independent() {
        let run = |steps: usize, dt: f32| {
            let mut a = 0.0f32;
            for _ in 0..steps {
                a += (40.0 - a) * (1.0 - (1.0 - k::EASE).powf(dt * k::FRAME_RATE));
            }
            a
        };
        let (fine, coarse) = (run(60, 1.0 / 60.0), run(30, 1.0 / 30.0));
        assert!((fine - coarse).abs() < 1e-3, "{fine} {coarse}");
        assert!((fine - 40.0 * (1.0 - 0.95f32.powi(60))).abs() < 1e-3, "{fine}");
    }
}
