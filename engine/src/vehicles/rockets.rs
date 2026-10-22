//! Rockets and shells: the projectile classes the vehicles fire (src/game/game_07.cpp), on the Projectile base
//! (src/game/game_06.cpp: Projectile_Load 0x0047e0c0, Projectile_Update 0x0047e580, the hit 0x0047eae0, the end
//! effect 0x0047ed40, the direct damage 0x0047eec0, AreaDamage_Apply 0x0047f020).
//!
//! | behaviour | class | flight |
//! |---|---|---|
//! | proj_captain_rocket | CaptainRocket 0x005a11d8 | max_speed x 3 along the aim; homes on its target (min / max speed, turn_power) unless dumb_fire (every vehicle's missile is dumb_fire) |
//! | proj_rocket_tower | RocketTowerProjectile 0x005aba18 | start_speed x 3 along the nozzle; after homing_delay_duration it steers at the target, up to max_speed over full_speed_distance, and lets go inside stop_homing_proximity when heading at it |
//! | proj_rocket_bud | RocketBudProjectile 0x005a7958 | lobbed: speed x 3 across, the vertical speed that lands it on the target under gravity (0x00471a00) |
//! | proj_forge_mortar | ForgeCannonProjectile 0x005a33b8 | lobbed as the bud's, its horizontal speed the distance + 5 (0x00480ec0): about a second's flight |
//! | proj_gurner_rocket | GurnerProjectile 0x005a4c38 | the player's Gurner mortar: lobbed as the forge cannon's (the same launch 0x00480ec0; update 0x00483210) |
//! | proj_vindaloo_missile | VindalooProjectile 0x005afc80 | the player's vindaloo: start_speed x 3 along its fan direction; after homing_delay_duration it steers at its target (0x004805f0, `Flight::Vindaloo`) and goes off when it starts to move away from it |
//!
//! A hit (0x0047eae0): the rocket stops at the point, area_damage (radius inner / outer) to everything near but the
//! object hit, the direct damage (damage + area_damage, 0x0047eec0) to the object hit, fx_end at the point and
//! fx_end_material with the surface's material. Pass-by sounds within 50 / 70 units of Roman, once.
//!
//! API: [`is_rocket`] (does a property set fly as one of these), [`launch`] (spawn one), the [`Rocket`] component.

use crate::fx::{Destructible, Explode, Src};
use crate::gamedata::Props;
use crate::levelreset::LevelAppExt;
use crate::physics::Physics;
use bevy::prelude::*;
use oxijolt::prelude::BodyId;

/// The world's gravity for the lobbed classes (the launch solves with -64.348, 0x00471a00's caller 0x00480db0).
pub const GRAVITY: f32 = 64.348;

pub struct RocketsPlugin;

impl Plugin for RocketsPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<RocketStats>()
            .add_systems(Update, (dress_rockets, fly, log_rockets).chain().after(crate::fx::FxSet).run_if(crate::playing));
    }
}

/// Counts for the state log's `vehicles.rockets` (playtest expects): rockets launched by bad guys, how many ended,
/// how many hit Roman directly and how many blew up within their outer radius of him.
#[derive(Resource, Default, Clone, Copy, Debug)]
pub struct RocketStats {
    pub fired: u32,
    pub ended: u32,
    pub hit_roman: u32,
    pub near_roman: u32,
    /// the player's shells (the forge cannon): launched, ended
    pub player_fired: u32,
    pub player_ended: u32,
}

fn log_rockets(stats: Res<RocketStats>, mut log: ResMut<super::VehicleLog>) {
    if stats.fired > 0 || stats.player_fired > 0 {
        log.0.insert(
            "rockets".into(),
            serde_json::json!({"fired": stats.fired, "ended": stats.ended, "hit_roman": stats.hit_roman, "near_roman": stats.near_roman, "player_fired": stats.player_fired, "player_ended": stats.player_ended}),
        );
    }
}

/// How a rocket flies.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Flight {
    /// straight on (CaptainRocket with dumb_fire, or without a target)
    Straight,
    /// CaptainRocket_Update 0x00482b40: the velocity pulled toward (T - p) clamped to [min, max] at turn_power per s
    Captain { min: f32, max: f32, turn: f32 },
    /// RocketTowerProjectile_Update 0x00480a20 (see the module table); `initial` is the distance to the target at the
    /// launch, `factor` the pull, which grows by 4 a second
    Tower { start: f32, max: f32, delay: f32, full: f32, stop: f32, initial: f32, factor: f32 },
    /// a lobbed shell under GRAVITY
    Ballistic,
    /// VindalooProjectile_Update 0x004805f0: straight at `start` until `delay` has run out, then steered at the target
    /// (its ref_hit_00, or the aim point `to`) with a pull of (1 - min(d, initial) / initial) x 100 + 0.5 a second and
    /// a speed from `start` up to `max` over the first `full` units closed; `last` is the distance a frame before
    /// (+0x56c): growing by more than 1 sets it off (it has passed its target)
    Vindaloo { start: f32, max: f32, delay: f32, full: f32, initial: f32, last: f32, to: Vec3 },
}

/// A rocket or shell in flight.
#[derive(Component)]
pub struct Rocket {
    pub vel: Vec3,
    pub flight: Flight,
    /// what it homes on: an entity and its node ("chest" for the rocket towers, 0x0049c610)
    pub target: Option<(Entity, Option<String>)>,
    /// seconds left (lifetime, default 4)
    pub life: f32,
    /// damage (default 10), area_damage and its radii, impact_force (2010): Projectile_Load 0x0047e0c0
    pub damage: f32,
    pub area: f32,
    pub inner: f32,
    pub outer: f32,
    pub impact: f32,
    pub owner: Option<Entity>,
    pub by: Src,
    /// damages_buildings off: the record's 0x400 (0x0047eba0)
    pub no_buildings: bool,
    pub fx_end: Option<String>,
    pub fx_end_material: Option<String>,
    /// the pass-by sound and its squared radius; played once
    pub passby: Option<(&'static str, f32)>,
    /// the rocket's template (its model is attached by `dress_rockets`)
    pub template: String,
    /// its damage records' class (the forge cannon's shell: ForgeCannonProjectile_Flags 0x00481000 adds 0x100000)
    pub kind: crate::player::DamageKind,
}

/// The behaviours this module flies.
pub fn is_rocket(p: &Props) -> bool {
    matches!(p.s("behavior"), Some("proj_captain_rocket" | "proj_rocket_tower" | "proj_rocket_bud" | "proj_forge_mortar" | "proj_gurner_rocket" | "proj_vindaloo_missile"))
}

/// What to launch: the projectile's template, from where, toward what.
pub struct Launch<'a> {
    pub template: &'a str,
    pub from: Vec3,
    /// the aim point (0x0047de40's target point, the crosshair's point, or the target's hit point)
    pub to: Vec3,
    /// launch along this direction instead of toward `to` (0x0047dfd0: the nozzle's facing)
    pub along: Option<Vec3>,
    /// the object to home on and its node (vt+0x14c)
    pub target: Option<(Entity, Option<&'a str>)>,
    pub owner: Option<Entity>,
    pub by: Src,
}

/// Creates the projectile and launches it (the factory with id 0x400, the class's launch vt+0x140, its start effect
/// vt+0x154 0x0047ee30: fx_start on fx_start_bone_name, both needed). Returns its entity.
pub fn launch(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, data: &crate::gamedata::GameData, rng: &mut super::VehicleRng, l: Launch) -> Option<Entity> {
    let p = data.template_props(l.template);
    let aim = (l.to - l.from).normalize_or(Vec3::NEG_Z);
    let dist = l.from.distance(l.to);
    let k3 = |v: f32| v * 3.0;
    let (vel, flight, passby) = match p.s("behavior").unwrap_or("") {
        "proj_captain_rocket" => {
            // CaptainRocket_ReadProps 0x004829e0: min_speed 10, max_speed 30, turn_power 30 (both speeds x 3); the
            // launch speed is the max speed; dumb_fire flies straight (0x00482b40 returns at once)
            let (min, max) = (k3(p.f("min_speed", 10.0)), k3(p.f("max_speed", 30.0)));
            let flight = if p.i("dumb_fire", 0) != 0 { Flight::Straight } else { Flight::Captain { min, max, turn: p.f("turn_power", 30.0) } };
            (l.along.unwrap_or(aim) * max, flight, Some(("wpn_rocket_passby_med_1", 4900.0)))
        }
        "proj_rocket_tower" => {
            // RocketTowerProjectile_ReadProps 0x00480920: start_speed 7, max_speed 70 (x 3), homing_delay_duration
            // 0.7, full_speed_distance 50 (at least 1), stop_homing_proximity 200; launched at the start speed
            let start = k3(p.f("start_speed", 7.0));
            let flight = Flight::Tower {
                start,
                max: k3(p.f("max_speed", 70.0)),
                delay: p.f("homing_delay_duration", 0.7),
                full: p.f("full_speed_distance", 50.0).max(1.0),
                stop: p.f("stop_homing_proximity", 200.0),
                initial: dist,
                factor: 1.0,
            };
            // its pass-by: "_1" or "_2" at even odds ((rand() x 100) & ~0x7fff < 0x190000), within 50
            let name = if rng.0.rand() * 100 < 0x190000 { "wpn_rocket_passby_med_1" } else { "wpn_rocket_passby_med_2" };
            (l.along.unwrap_or(aim) * start, flight, Some((name, 2500.0)))
        }
        "proj_rocket_bud" => (lob(l.from, l.to, k3(p.f("speed", 10.0))), Flight::Ballistic, Some(("wpn_rocket_passby_med_3", 4900.0))),
        // VindalooProjectile_ReadProps 0x00480510: start_speed 7, max_speed 70 (both x 3), homing_delay_duration 0.7,
        // full_speed_distance 50 (at least 1); launched along its fan direction at the start speed (0x0047e050 ->
        // 0x0047e400); the distance to the target taken at its first update (0x0047e580 -> 0x0047e4d0)
        "proj_vindaloo_missile" => {
            let start = k3(p.f("start_speed", 7.0));
            let flight = Flight::Vindaloo { start, max: k3(p.f("max_speed", 70.0)), delay: p.f("homing_delay_duration", 0.7), full: p.f("full_speed_distance", 50.0).max(1.0), initial: 0.0, last: f32::MAX, to: l.to };
            (l.along.unwrap_or(aim) * start, flight, None)
        }
        // Projectile_Launch_00480ec0: the speed is the horizontal distance to the target + 5 (the forge cannon's
        // shell and the Gurner's mortar); only the forge cannon's update (0x00480f60) adds the pass-by sound, the
        // Gurner's (0x00483210) has none
        "proj_forge_mortar" | "proj_gurner_rocket" => {
            let h = (l.to - l.from).with_y(0.0).length();
            let passby = (p.s("behavior") == Some("proj_forge_mortar")).then_some(("wpn_rocket_passby_med_2", 4900.0));
            (lob(l.from, l.to, h + 5.0), Flight::Ballistic, passby)
        }
        _ => return None,
    };
    let e = commands
        .spawn((
            Transform::from_translation(l.from).with_rotation(facing(vel)),
            Visibility::default(),
            Rocket {
                vel,
                flight,
                target: l.target.map(|(e, b)| (e, b.map(str::to_owned))),
                life: p.f("lifetime", 4.0),
                damage: p.f("damage", 10.0),
                area: p.f("area_damage", 0.0),
                inner: p.f("damage_radius_inner", 0.0),
                outer: p.f("damage_radius_outer", 0.0),
                impact: p.f("impact_force", 2010.0),
                owner: l.owner,
                by: l.by,
                no_buildings: p.f("damages_buildings", 0.0) == 0.0,
                fx_end: p.s("fx_end").filter(|s| !s.is_empty() && *s != "none").map(str::to_owned),
                fx_end_material: p.s("fx_end_material").filter(|s| !s.is_empty() && *s != "none").map(str::to_owned),
                passby,
                template: l.template.to_owned(),
                kind: crate::player::DamageKind::of_behavior(p.s("behavior").unwrap_or("")),
            },
        ))
        .id();
    if let (Some(name), Some(_bone)) = (p.s("fx_start"), p.s("fx_start_bone_name")) {
        // (the bone's offset on the rocket is applied by dress_rockets' anchor once the model is known; the trail
        // starts at the rocket's origin and follows it)
        fx.write(crate::fx::StartFx { name: name.to_owned(), at: l.from, rotation: Quat::IDENTITY, follow: Some(e), position_only: false });
    }
    Some(e)
}

/// One shot of a projectile property set (`p_<template>`) from a gun: a rocket or shell flies here, a bullet is the
/// player's projectile (player.rs). For guns shared with other modules (an AI gunner on a forge cannon, ai.rs).
#[allow(clippy::too_many_arguments)]
pub fn shoot(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, data: &crate::gamedata::GameData, set: &str, from: Vec3, to: Vec3, owner: Option<Entity>, by: Src) {
    let p = data.props(set);
    if is_rocket(&p) {
        // (only a rocket tower's missile rolls at launch: a fixed stream is enough for these)
        let mut rng = super::VehicleRng(crate::fxvm::CrtRand::seeded(0x5a5e));
        launch(commands, fx, data, &mut rng, Launch { template: set.trim_start_matches("p_"), from, to, along: None, target: None, owner, by });
    } else {
        crate::player::spawn_projectile_to(commands, fx, &p, from, to, by == Src::Player, owner);
    }
}

/// The launch velocity of a lobbed shell (Ballistic_LaunchVelocity 0x00471a00 with keep = 1): `speed` across toward
/// the target, the time t = across distance / speed, the vertical speed (dz + t^2 g / 2) / t. Engine axes (Y up).
pub fn lob(from: Vec3, to: Vec3, speed: f32) -> Vec3 {
    let d = to - from;
    let across = d.with_y(0.0);
    let h = across.length();
    if h < 1e-3 || speed <= 0.0 {
        return Vec3::Y * speed;
    }
    let t = h / speed;
    across / h * speed + Vec3::Y * ((d.y + t * t * GRAVITY * 0.5) / t)
}

/// The rocket's matrix (0x00455e70 rebuilds it along the velocity with Matrix_LookAtNeg 0x004e3070: the model's -Y
/// along the flight, game Z up), as an engine rotation.
pub(crate) fn facing(vel: Vec3) -> Quat {
    Transform::IDENTITY.looking_to(vel.normalize_or(Vec3::NEG_Z), Vec3::Y).rotation * crate::level::game_to_engine() * Quat::from_rotation_z(std::f32::consts::PI)
}

/// The template's model on a new rocket.
#[allow(clippy::too_many_arguments)]
fn dress_rockets(
    mut commands: Commands,
    // (a rocket captain's rockets, ai/rocket.rs, are the infantry's: not counted here)
    q: Query<(Entity, &Rocket, Has<crate::ai::rocket::RocketHull>), Added<Rocket>>,
    mut stats: ResMut<RocketStats>,
    data: Res<crate::gamedata::GameData>,
    mut lib: ResMut<crate::scene::Library>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
) {
    for (e, r, captains) in &q {
        if r.by == Src::Player {
            stats.player_fired += 1;
        } else if !captains {
            stats.fired += 1;
        }
        let Some(model) = data.templates.get(&r.template).and_then(|t| t.model.clone()) else { continue };
        let Some(m) = lib.load_model(&model, false, &assets, &mut meshes, &mut materials) else { continue };
        for (mesh, mat) in m.parts.clone() {
            let c = commands.spawn((Mesh3d(mesh), MeshMaterial3d(mat))).id();
            commands.entity(e).add_child(c);
        }
    }
}

/// One frame of every rocket: steering, gravity, the pass-by, the swept segment's first hit, the lifetime.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn fly(
    mut commands: Commands,
    time: Res<Time>,
    physics: NonSend<Physics>,
    mut q: Query<(Entity, &mut Rocket, &mut Transform, Has<crate::ai::rocket::RocketHull>)>,
    globals: Query<&GlobalTransform>,
    anims: Query<&crate::skin::Animator>,
    mut roman: Query<(Entity, &mut crate::player::Roman, &Transform, &mut crate::fx::Health), Without<Rocket>>,
    mut companions: Query<(Entity, &mut crate::ai::Companion, &Transform, &mut crate::fx::Health), (Without<crate::player::Roman>, Without<Rocket>)>,
    chars: Query<(Entity, &crate::physics::Body, Has<crate::ai::Grunt>), Without<Rocket>>,
    mut grunt_health: Query<&mut crate::fx::Health, (With<crate::ai::Grunt>, Without<crate::player::Roman>, Without<crate::ai::Companion>, Without<Rocket>)>,
    mut objects: Query<(Entity, &crate::level::Placed, &mut Destructible, Option<&crate::vehicles::boss::BulletScale>)>,
    (data, mounted, topsy, mut stats, tuning, suction): (Res<crate::gamedata::GameData>, Res<crate::turret::Mounted>, Res<crate::topsy::Topsy>, ResMut<RocketStats>, Res<super::VehicleTuning>, Res<crate::weapons::blackhole::Suction>),
    (mut fx, mut blasts, mut sounds, mut player_stats, mut noise): (MessageWriter<crate::fx::StartFx>, MessageWriter<Explode>, MessageWriter<crate::audio::PlaySound>, ResMut<crate::player::PlayerStats>, Option<ResMut<crate::ai::Interests>>),
    hits_ignored: Query<(), With<crate::mission::HitsIgnored>>,
) {
    let dt = time.delta_secs();
    if dt <= 0.0 {
        return;
    }
    let roman_at = roman.single().ok().map(|(e, r, t, _)| (e, t.translation + Vec3::Y * r.walker.lift));
    for (e, mut r, mut t, captains) in &mut q {
        let from = t.translation;
        // the target dropped once it is gone (Projectile_Update 0x0047e580: vt+0xd8)
        if r.target.as_ref().is_some_and(|(te, _)| globals.get(*te).is_err()) {
            r.target = None;
        }
        let target_point = r.target.as_ref().and_then(|(te, bone)| {
            let gt = globals.get(*te).ok()?;
            let joint = bone.as_ref().and_then(|b| {
                let a = anims.get(*te).ok()?;
                let j = *a.joints.get(*a.skeleton.index.get(&b.to_lowercase())?)?;
                globals.get(j).ok().map(|g| g.translation())
            });
            Some(joint.unwrap_or(gt.translation()))
        });
        // the vindaloo's: it has passed its target (0x004805f0)
        let mut proximity = false;
        match r.flight {
            Flight::Straight => {}
            Flight::Ballistic => r.vel.y -= GRAVITY * dt,
            Flight::Captain { min, max, turn } => {
                if let Some(tp) = target_point {
                    // 0x00471970 (min, max, 1, 0): toward the target, its length clamped; 0x00471750 x mass x turn_power
                    let d = tp - from;
                    let want = d.normalize_or_zero() * d.length().clamp(min, max);
                    let pull = (want - r.vel) * (turn * dt).min(1.0);
                    r.vel += pull;
                }
            }
            Flight::Tower { start, max, mut delay, full, stop, initial, mut factor } => {
                delay -= dt;
                if delay <= 0.0 {
                    if let Some(tp) = target_point {
                        let d = tp - from;
                        let dist = d.length();
                        let dir = d.normalize_or_zero();
                        // heading at the target (dot > 0.7) inside stop_homing_proximity: let go (flies on straight)
                        if r.vel.normalize_or_zero().dot(dir) > 0.7 && dist < stop {
                            r.target = None;
                        } else {
                            factor += 4.0 * dt;
                            let gained = (initial - dist.min(initial)).min(full);
                            let speed = start + (max - start) * gained / full;
                            let pull = (dir * speed - r.vel) * (factor * dt).min(1.0);
                            r.vel += pull;
                        }
                    }
                }
                r.flight = Flight::Tower { start, max, delay, full, stop, initial, factor };
            }
            Flight::Vindaloo { start, max, mut delay, full, mut initial, mut last, to } => {
                let tp = target_point.unwrap_or(to);
                let dist = tp.distance(from);
                if initial <= 0.0 {
                    initial = dist.max(1e-3);
                }
                delay -= dt;
                // (while a black hole sucks, list +0xf00 not empty, the homing does nothing: weapons/blackhole.rs)
                if delay <= 0.0 && !suction.active {
                    if dist > last + 1.0 {
                        proximity = true;
                    } else {
                        last = dist;
                        let d = dist.min(initial);
                        let pull = (1.0 - d / initial) * 100.0 + 0.5;
                        let speed = start + (max - start) * (initial - d).min(full) / full;
                        let want = (tp - from).normalize_or_zero() * speed;
                        let dv = (want - r.vel) * (pull * dt).min(1.0);
                        r.vel += dv;
                    }
                }
                r.flight = Flight::Vindaloo { start, max, delay, full, initial, last, to };
            }
        }
        let step = r.vel * dt;
        if step.length_squared() > 1e-8 {
            t.rotation = facing(r.vel);
        }
        // the pass-by sound, once, near Roman
        if let (Some((name, r2)), Some((_, rp))) = (r.passby, roman_at) {
            if from.distance_squared(rp) < r2 {
                sounds.write(crate::audio::PlaySound::at(name, from));
                r.passby = None;
            }
        }
        r.life -= dt;
        // the swept segment: the world (skipping the owner's own collision and what bullets pass), Roman and the
        // companions by distance to their bodies (4.5 units, player.rs's fallback for characters without parts)
        let len = step.length().max(0.01);
        let dir = step.normalize_or(Vec3::NEG_Z);
        let skip: Vec<BodyId> = r.owner.and_then(|o| objects.get(o).ok()).and_then(|(_, p, _, _)| p.collider).into_iter().collect();
        let hit = ray(&physics, from, dir, len, &skip);
        let reach = hit.map_or(len, |(_, at, _)| at.distance(from));
        let near = |c: Vec3| {
            let u = ((c - from).dot(step) / step.length_squared().max(1e-6)).clamp(0.0, 1.0);
            ((from + step * u).distance(c) < 4.5 && (c - from).length() <= reach + 4.5).then_some(from + step * u)
        };
        let mut victim: Option<(Option<Entity>, Vec3, Option<BodyId>)> = None;
        // gone off past its target: at its place, the target (if any) takes the direct damage and the blast reaches
        // everything, the target too (0x004805f0: 0x0047eec0 on the target, then 0x0047f020 with no object left out)
        if proximity {
            victim = Some((r.target.as_ref().map(|t| t.0), from, None));
        }
        if victim.is_none() && r.by != Src::Player {
            if let Some((re, rp)) = roman_at.filter(|(re, _)| Some(*re) != r.owner) {
                if let Some(at) = near(rp) {
                    victim = Some((Some(re), at, None));
                }
            }
        }
        if victim.is_none() {
            for (ce, _, ct, _) in companions.iter() {
                if Some(ce) == r.owner || r.by == Src::Player {
                    continue;
                }
                if let Some(at) = near(ct.translation + Vec3::Y * 5.0) {
                    victim = Some((Some(ce), at, None));
                    break;
                }
            }
        }
        if victim.is_none() {
            if let Some((body, at, _)) = hit {
                let owner = physics.bodies.get(&body).copied().or_else(|| objects.iter().find(|(_, p, _, _)| p.collider == Some(physics.main_body(body))).map(|(oe, ..)| oe));
                victim = Some((owner, at, Some(body)));
            }
        }
        let Some((obj, at, body)) = victim else {
            t.translation = from + step;
            if r.life <= 0.0 {
                commands.entity(e).despawn();
            }
            continue;
        };
        // the hit 0x0047eae0: at the point; area damage to all but the object hit; the direct damage (damage +
        // area_damage, 0x0047eec0); the end effects (0x0047ed40); removed
        // (the vehicles' damage scale applies to the bad guys' rockets; the player's shells are his weapon's)
        let scale = if r.by == Src::Player { 1.0 } else { tuning.damage_scale };
        let direct = (r.damage + r.area) * scale;
        if r.by == Src::Player {
            stats.player_ended += 1;
            let target = obj.and_then(|o| objects.get(o).ok()).map(|(_, p, _, _)| p.template.clone()).unwrap_or_default();
            info!("EVENT vehicles: the player's shell ended at game ({:.0}, {:.0}, {:.0}) on {target:?}", at.x, -at.z, at.y);
        } else if !captains {
            stats.ended += 1;
            if obj.is_some() && obj == roman_at.map(|(re, _)| re) {
                stats.hit_roman += 1;
            } else if roman_at.is_some_and(|(_, rp)| rp.distance(at) <= r.outer) {
                stats.near_roman += 1;
            }
        }
        if r.area != 0.0 {
            let source = if proximity { None } else { obj };
            blasts.write(Explode { at, area_damage: r.area * scale, radius_inner: r.inner, radius_outer: r.outer, impact_force: r.impact, source, delay: 0.0, by: r.by, cartoon: false, kind: r.kind });
        }
        if let Some(o) = obj {
            if let Ok((_, mut rm, _, mut h)) = roman.get_mut(o) {
                // PlayerObject_OnHit 0x0047afa0: nothing in the topsy states or after the pre-success (mission.rs
                // HitsIgnored); half while on a mounted gun
                if !topsy.active() && !hits_ignored.contains(o) {
                    h.0 -= if mounted.0.is_some() { direct * 0.5 } else { direct };
                    rm.hit_from = Some(r.vel.normalize_or(Vec3::Z));
                }
            } else if let Ok((_, mut c, _, mut h)) = companions.get_mut(o) {
                // (a forge shell's record: x forge_damage_multiplier, 0.1 for Q and Jonesy)
                h.0 -= direct * c.damage_scale(r.kind);
                c.attacker = r.owner;
            } else if let Ok((_, _, grunt)) = chars.get(o) {
                // a bad guy's rocket does nothing to a grunt (same team; player.rs); the player's hurts
                if !(grunt && r.by != Src::Player) {
                    if let Ok(mut h) = grunt_health.get_mut(o) {
                        let alive = h.0 > 0.0;
                        h.0 -= direct;
                        // the player's rocket killed him (his kills, as the bullets count them: player.rs)
                        if r.by == Src::Player && alive && h.0 <= 0.0 {
                            player_stats.kills += 1;
                        }
                    }
                }
            } else if let Ok((_, _, mut d, scale)) = objects.get_mut(o) {
                if d.accepts(r.by, r.no_buildings) {
                    let amount = if r.no_buildings { direct * scale.map_or(1.0, |s| s.0) } else { direct };
                    if d.take_damage(amount) && d.dying.is_none() {
                        d.chain_delay = 0.0;
                        d.dying = Some(0.0);
                    }
                }
            }
        }
        if let Some(n) = &r.fx_end {
            fx.write(crate::fx::StartFx { name: n.clone(), at, rotation: Quat::IDENTITY, follow: None, position_only: false });
        }
        if let Some(base) = &r.fx_end_material {
            // the variant of the surface it hit (surface.rs)
            let name = body.map(|b| physics.surface(b, at)).unwrap_or_default().variant(base, |n| data.fx.contains_key(n));
            fx.write(crate::fx::StartFx { name, at, rotation: Quat::IDENTITY, follow: None, position_only: false });
        }
        // the end effect's AI noise for Roman's own (0x0047ed40 -> 0x00404810: radius 200, 2 s; ai.rs Interests)
        if let (true, Some(n)) = (r.owner.is_some() && r.owner == roman_at.map(|(re, _)| re), noise.as_mut()) {
            n.end_at(e, at);
        }
        commands.entity(e).despawn();
    }
}

/// The first hit along a segment, past the bodies bullets pass and the ones in `skip`.
fn ray(physics: &Physics, from: Vec3, dir: Vec3, length: f32, skip: &[BodyId]) -> Option<(BodyId, Vec3, Vec3)> {
    let (mut at, mut left) = (from, length);
    for _ in 0..16 {
        let h = physics.raycast_bullet(at, dir, left)?;
        if !skip.contains(&h.0) {
            return Some(h);
        }
        left -= h.1.distance(at) + 0.05;
        if left <= 0.0 {
            return None;
        }
        at = h.1 + dir * 0.05;
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 0x00471a00 with keep: the shell lands on the target under gravity.
    #[test]
    fn lob_lands_on_target() {
        let from = Vec3::new(0.0, 10.0, 0.0);
        let to = Vec3::new(120.0, -5.0, 50.0);
        let v = lob(from, to, 130.0);
        let (mut p, mut vel, dt) = (from, v, 1.0 / 600.0);
        let t = (to - from).with_y(0.0).length() / 130.0;
        let n = (t / dt).round() as i32;
        for _ in 0..n {
            vel.y -= GRAVITY * dt;
            p += vel * dt;
        }
        assert!(p.distance(to) < 1.0, "{p:?}");
    }
}
