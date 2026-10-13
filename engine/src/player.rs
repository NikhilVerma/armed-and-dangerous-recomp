//! The player, Roman (template `trotter`, property set `p_trotter`), and his weapons, from the game's data:
//! movement speeds, jump, health (p_trotter), the rifle and its projectile (p_player_rifle / proj_rifle_bullet
//! values via docs/subsystems/player.md), sticky bombs (p_player_sticky*), the level's starting loadout (the
//! mission instance: player_add_rifle). Controls and the camera follow are STAND-INS until the original's
//! input map and camera nodes are ported (docs/subsystems/player.md, camera.md).

use crate::fx::{Destructible, Fuse, Health};
use crate::gamedata::{GameData, Props};
use crate::level::game_to_engine;
use crate::physics::{Body, Physics, Walker};
use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::prelude::*;
use bevy::window::{CursorGrabMode, CursorOptions, PrimaryWindow};
use oxijolt::prelude::*;

#[derive(Resource)]
pub struct BombAssets {
    pub parts: Vec<(Handle<Mesh>, Handle<StandardMaterial>)>,
    pub shape: Shape,
    /// The model's rest rotation (game axes to engine axes).
    pub base: Quat,
}

/// Roman. Values from p_trotter (instance values over the template's).
#[derive(Component)]
pub struct Roman {
    pub walker: Walker,
    pub yaw: f32,
    pub pitch: f32,
    pub vel: Vec3,
    pub on_ground: bool,
    pub fwd_speed: f32,
    pub side_speed: f32,
    pub jump_impulse: f32,
    pub fall_speed: f32,
    pub anti_gravity: f32,
    pub max_health: f32,
    pub rifle: Weapon,
    pub stickies: u32,
    pub dead: bool,
    pub base: Quat,
    /// seconds since the last shot, and of reload left (for the anims)
    pub since_fire: f32,
    pub reloading: f32,
    pub move_local: Vec2,
}

#[derive(Clone)]
pub struct Weapon {
    pub name: &'static str,
    pub recycle: f32,
    pub reload_time: f32,
    pub clip_size: u32,
    pub clip: u32,
    pub ammo: u32,
    pub speed: f32,
    pub damage: f32,
    pub range: f32,
    pub impact_force: f32,
    pub timer: f32,
}

/// A projectile in flight (the game's bullets are projectiles, not hitscan): moves `vel` per second, hits the
/// first thing on its path each frame.
#[derive(Component)]
pub struct Projectile {
    pub vel: Vec3,
    pub life: f32,
    pub damage: f32,
    pub from_player: bool,
    pub owner: Option<Entity>,
    /// impact fx base name (fx_end_material + "_" + the hit material)
    pub fx_end: Option<String>,
}

/// A projectile of a property set (speed x3 as Projectile_Load 0x0047e0c0; damage, lifetime, fx_start trail
/// following it, fx_end_material on impact).
pub fn spawn_projectile(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, p: &Props, from: Vec3, dir: Vec3, from_player: bool, owner: Option<Entity>) {
    let speed = p.f("speed", 300.0) * 3.0;
    let e = commands
        .spawn((
            Transform::from_translation(from).looking_to(dir, Vec3::Y),
            Projectile {
                vel: dir * speed,
                life: p.f("lifetime", 4.0).min(3.0),
                damage: p.f("damage", 10.0),
                from_player,
                owner,
                fx_end: p.s("fx_end_material").map(str::to_owned),
            },
        ))
        .id();
    if let Some(name) = p.s("fx_start") {
        fx.write(crate::fx::StartFx { name: name.to_owned(), at: from, rotation: Quat::IDENTITY, follow: Some(e) });
    }
}

/// The third-person camera rig around Roman.
#[derive(Component)]
pub struct FollowCam;

#[derive(Resource, Default)]
pub struct PlayerStats {
    pub health: f32,
    pub max_health: f32,
    pub clip: u32,
    pub ammo: u32,
    pub stickies: u32,
    pub position: Vec3,
    pub kills: u32,
    pub shots: u32,
}

pub struct PlayerPlugin;

impl Plugin for PlayerPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<PlayerStats>()
            .add_systems(Update, (grab_cursor, control, weapons, projectiles).chain().run_if(crate::playing))
            .add_systems(PostUpdate, follow_camera.before(TransformSystems::Propagate).run_if(crate::playing));
    }
}

/// Builds Roman from his property set at a placed trotter instance (feet at `feet`, engine space).
pub fn make_roman(physics: &mut Physics, p: &Props, data: &GameData, feet: Vec3, yaw: f32) -> Option<Roman> {
    let radius = p.f("collision_radius", 4.0).clamp(2.0, 5.0);
    let walker = physics.add_walker(feet, radius, 11.0)?;
    let r = data.props("p_player_rifle");
    let b = data.props("proj_rifle_bullet");
    Some(Roman {
        walker,
        yaw,
        pitch: -0.15,
        vel: Vec3::ZERO,
        on_ground: false,
        fwd_speed: p.f("fwd_max_speed", 40.0),
        side_speed: p.f("side_max_speed", 40.0),
        jump_impulse: p.f("jump_impulse", 70.0),
        fall_speed: p.f("fall_speed", -50.0),
        anti_gravity: p.f("jump_anti_gravity", 5.0),
        max_health: p.f("max_health", 400.0),
        rifle: Weapon {
            name: "rifle",
            recycle: r.f("recycle_time", 0.5),
            reload_time: r.f("clip_reload_time", 1.0),
            clip_size: r.i("clip_size", 8) as u32,
            clip: r.i("clip_size", 8) as u32,
            // weapon type 2: unlimited reserve (the HUD shows "8/" and the infinity sign; hud.md)
            ammo: u32::MAX,
            speed: b.f("speed", 300.0),
            damage: b.f("damage", 50.0),
            range: r.f("range", 500.0),
            impact_force: b.f("impact_force", 7370.0),
            timer: 0.0,
        },
        stickies: 5,
        dead: false,
        base: game_to_engine(),
        since_fire: 10.0,
        reloading: 0.0,
        move_local: Vec2::ZERO,
    })
}

fn grab_cursor(mut cursor: Query<&mut CursorOptions, With<PrimaryWindow>>, mut done: Local<bool>) {
    if *done {
        return;
    }
    if let Ok(mut c) = cursor.single_mut() {
        c.grab_mode = CursorGrabMode::Locked;
        c.visible = false;
        *done = true;
    }
}

/// WASD relative to the camera yaw, Space jump, mouse look (stand-in bindings).
#[allow(clippy::too_many_arguments)]
fn control(
    time: Res<Time>,
    mut bot: ResMut<crate::playtest::Bot>,
    grunts: Query<(&crate::ai::Grunt, &GlobalTransform)>,
    keys: Res<ButtonInput<KeyCode>>,
    motion: Res<AccumulatedMouseMotion>,
    mut physics: NonSendMut<Physics>,
    mut q: Query<(&mut Roman, &mut Transform, &Health)>,
    mut stats: ResMut<PlayerStats>,
) {
    let dt = time.delta_secs();
    for (mut r, mut t, h) in &mut q {
        stats.health = h.0.max(0.0);
        stats.max_health = r.max_health;
        if h.0 <= 0.0 {
            r.dead = true;
        }
        r.yaw -= motion.delta.x * 0.0025;
        r.pitch = (r.pitch - motion.delta.y * 0.0025).clamp(-1.1, 0.7);
        let mut wish = Vec3::ZERO;
        // playtest bot: turn toward the goal and walk; when fighting, face the nearest live grunt
        let mut bot_walk: Option<Vec3> = None;
        // plan a route when the goal changes (A* over the ground, nav.rs), then follow its waypoints
        // (re)plan: a new goal, or the partial route ran out short of the goal
        let route_done = bot.route.is_empty() && bot.goto.is_some_and(|g| (g - t.translation).with_y(0.0).length() > 30.0);
        bot.replan_timer -= dt;
        if bot.goto.is_some() && (bot.planned_for != bot.goto || (route_done && bot.replan_timer <= 0.0)) {
            bot.planned_for = bot.goto;
            bot.replan_timer = 1.5;
            let blocked = bot.blocked.clone();
            bot.route = crate::nav::path(&physics, t.translation, bot.goto.unwrap(), &blocked).unwrap_or_default();
            info!("bot: route to {:?}: {} waypoints {:?}", bot.goto, bot.route.len(), bot.route.iter().map(|p| [p.x as i32, p.y as i32, p.z as i32]).collect::<Vec<_>>());
        }
        while bot.route.first().is_some_and(|w| (*w - t.translation).with_y(0.0).length() < 8.0) {
            bot.route.remove(0);
        }
        if let Some(g) = bot.goto {
            let to = (bot.route.first().copied().unwrap_or(g) - t.translation).with_y(0.0);
            let to = if (g - t.translation).with_y(0.0).length() < 12.0 { (g - t.translation).with_y(0.0) } else { to };
            if (g - t.translation).with_y(0.0).length() < 12.0 {
                bot.goto = None;
                bot.arrived = true;
            } else {
                // stuck (under 4 units of progress in a second): jump and veer for a moment
                bot.stuck_timer += dt;
                if bot.stuck_timer > 1.0 {
                    let moved = t.translation.with_y(0.0).distance(bot.last_pos.with_y(0.0));
                    bot.veer = if moved < 4.0 { 1.2 } else { (bot.veer - 1.0).max(0.0) };
                    bot.no_progress = if moved < 6.0 { bot.no_progress + 1.0 } else { 0.0 };
                    bot.last_pos = t.translation;
                    bot.stuck_timer = 0.0;
                    // stuck 3 s: block the waypoint ahead and plan again around it
                    if bot.no_progress >= 3.0 {
                        let spot = bot.route.first().copied().unwrap_or(t.translation);
                        bot.blocked.push(spot);
                        bot.route.clear();
                        bot.replan_timer = 0.0;
                        bot.planned_for = None;
                        bot.no_progress = 0.0;
                        info!("bot: stuck near {:?}, replanning", [spot.x as i32, spot.z as i32]);
                    }
                }
                let dir = to.normalize();
                let dir = if bot.veer > 0.0 { Quat::from_rotation_y(0.9 * if (bot.veer * 10.0) as i32 % 2 == 0 { 1.0 } else { -1.0 }) * dir } else { dir };
                if !bot.fight {
                    r.yaw = (-dir.x).atan2(-dir.z);
                }
                bot_walk = Some(dir);
            }
        }
        if bot.fight {
            let me = t.translation;
            if let Some(p) = grunts
                .iter()
                .filter(|(g, _)| g.mind != crate::ai::Mind::Dead)
                .map(|(_, gt)| gt.translation())
                .filter(|p| p.distance(me) < 260.0)
                .min_by(|a, b| a.distance(me).total_cmp(&b.distance(me)))
            {
                let to = p + Vec3::Y * 5.0 - (me + Vec3::Y * 8.5);
                r.yaw = (-to.x).atan2(-to.z);
                r.pitch = (to.y / to.with_y(0.0).length().max(1.0)).atan() - 0.08;
            }
        }
        if !r.dead {
            if let Some(dir) = bot_walk {
                wish += dir * r.fwd_speed;
            }
            let f = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
            let s = Quat::from_rotation_y(r.yaw) * Vec3::X;
            if keys.pressed(KeyCode::KeyW) {
                wish += f * r.fwd_speed;
            }
            if keys.pressed(KeyCode::KeyS) {
                wish -= f * r.fwd_speed * 0.7;
            }
            if keys.pressed(KeyCode::KeyD) {
                wish += s * r.side_speed;
            }
            if keys.pressed(KeyCode::KeyA) {
                wish -= s * r.side_speed;
            }
        }
        // horizontal velocity follows the input quickly, vertical integrates gravity (49 units/s^2 = 9.8 m/s^2)
        let k = (dt * 12.0).min(1.0);
        r.vel.x += (wish.x - r.vel.x) * k;
        r.vel.z += (wish.z - r.vel.z) * k;
        if r.on_ground {
            r.vel.y = r.vel.y.max(-5.0);
            if (keys.just_pressed(KeyCode::Space) || (bot.veer > 0.0 && bot.goto.is_some())) && !r.dead {
                r.vel.y = r.jump_impulse;
            }
        } else {
            // world gravity -64.348 x anti_gravity x 0.5 (0x005bcb4c): -160.9 with jump_anti_gravity 5 in lev_1_m1
            // (docs/subsystems/player.md); jump impulse 70 -> about 15 units high
            r.vel.y = (r.vel.y - 64.348 * r.anti_gravity * 0.5 * dt).max(r.fall_speed.min(-50.0) * 4.0);
        }
        let vel = r.vel;
        let fwd = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
        let right = Quat::from_rotation_y(r.yaw) * Vec3::X;
        r.move_local = Vec2::new(vel.dot(right), vel.dot(fwd));
        if let Some((feet, on)) = physics.move_walker(&r.walker, vel, dt) {
            if on && r.vel.y <= 0.0 {
                r.vel.y = 0.0;
            }
            r.on_ground = on;
            t.translation = feet;
        }
        // Roman faces where the camera looks (his model faces -Y in game space = +Z engine: turn by pi)
        t.rotation = Quat::from_rotation_y(r.yaw + std::f32::consts::PI) * r.base;
        stats.position = t.translation;
        stats.clip = r.rifle.clip;
        stats.ammo = r.rifle.ammo;
        stats.stickies = r.stickies;
    }
}

fn aim(r: &Roman, t: &Transform) -> (Vec3, Vec3) {
    let dir = Quat::from_euler(EulerRot::YXZ, r.yaw, r.pitch + 0.08, 0.0) * Vec3::NEG_Z;
    (t.translation + Vec3::Y * 8.5 + dir * 3.0, dir)
}

#[allow(clippy::too_many_arguments)]
fn weapons(
    mut commands: Commands,
    time: Res<Time>,
    buttons: Res<ButtonInput<MouseButton>>,
    keys: Res<ButtonInput<KeyCode>>,
    mut physics: NonSendMut<Physics>,
    bombs: Res<BombAssets>,
    data: Res<GameData>,
    bot: Res<crate::playtest::Bot>,
    mut q: Query<(Entity, &mut Roman, &Transform)>,
    mut stats: ResMut<PlayerStats>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    let dt = time.delta_secs();
    for (e, mut r, t) in &mut q {
        if r.dead {
            continue;
        }
        r.since_fire += dt;
        r.reloading -= dt;
        let mut reloaded = false;
        let w = &mut r.rifle;
        w.timer -= dt;
        if (keys.just_pressed(KeyCode::KeyR) && w.clip < w.clip_size && w.ammo > 0) || (w.clip == 0 && w.ammo > 0 && w.timer <= 0.0) {
            let n = (w.clip_size - w.clip).min(w.ammo);
            w.clip += n;
            w.ammo -= n;
            w.timer = w.reload_time;
            reloaded = true;
            sounds.write(crate::audio::PlaySound::at("wpn_rome_rifle_load", t.translation));
        }
        let fire = buttons.pressed(MouseButton::Left) || bot.fight;
        let shoot = fire && w.timer <= 0.0 && w.clip > 0;
        if reloaded {
            r.reloading = r.rifle.reload_time;
        }
        if shoot {
            r.since_fire = 0.0;
            let w = &mut r.rifle;
            w.clip -= 1;
            w.timer = w.recycle;
            stats.shots += 1;
            let (from, dir) = aim(&r, t);
            let rifle = data.props("p_player_rifle");
            let bullet = data.props(&format!("p_{}", rifle.s("projectile_name").unwrap_or("player_rifle_bullet")));
            spawn_projectile(&mut commands, &mut fx, &bullet, from, dir, true, Some(e));
            if let Some(m) = rifle.s("muzzle_fx_name") {
                fx.write(crate::fx::StartFx { name: m.to_owned(), at: from, rotation: Transform::IDENTITY.looking_to(dir, Vec3::Y).rotation, follow: None });
            }
            sounds.write(crate::audio::PlaySound::at("wpn_rome_rifle_fire", t.translation));
        }
        // sticky bomb: thrown along the aim with the game's sticky bomb values (stand-in throw strength)
        if (keys.just_pressed(KeyCode::KeyF) || buttons.just_pressed(MouseButton::Right)) && r.stickies > 0 {
            r.stickies -= 1;
            let (from, dir) = aim(&r, t);
            spawn_bomb(&mut commands, &mut physics, &bombs, &data, from, dir * 90.0 + Vec3::Y * 20.0, 3.0);
        }
    }
}

/// Projectiles: one ray per frame along the path; damage on the first body hit (characters' Health, objects'
/// Destructible), a tracer line for the path.
#[allow(clippy::too_many_arguments)]
fn projectiles(
    mut commands: Commands,
    time: Res<Time>,
    physics: NonSend<Physics>,
    mut q: Query<(Entity, &mut Projectile, &mut Transform)>,
    mut chars: Query<(Entity, &crate::physics::Body, &mut Health), Without<Projectile>>,
    mut objects: Query<(&crate::level::Placed, &mut Destructible)>,
    roman: Query<(Entity, &Roman, &Transform), Without<Projectile>>,
    mut roman_health: Query<&mut Health, (With<Roman>, Without<crate::physics::Body>, Without<Projectile>)>,
    mut stats: ResMut<PlayerStats>,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    let dt = time.delta_secs();
    for (e, mut p, mut t) in &mut q {
        let step = p.vel * dt;
        let from = t.translation;
        p.life -= dt;
        let hit = physics.raycast(from, step.normalize_or(Vec3::Y), step.length().max(0.01));
        // enemy bullets hit Roman by distance to his capsule (the walker is not a body)
        let mut hit_roman = false;
        if !p.from_player {
            if let Ok((_, r, rt)) = roman.single() {
                let c = rt.translation + Vec3::Y * r.walker.lift;
                let to = from + step;
                let seg = to - from;
                let u = ((c - from).dot(seg) / seg.length_squared().max(1e-6)).clamp(0.0, 1.0);
                if (from + seg * u).distance(c) < 4.5 {
                    hit_roman = true;
                }
            }
        }
        // impact fx: fx_end_material + the surface's material (landscape: snow, guess)
        let impact = |body: Option<BodyId>| -> Option<String> {
            let base = p.fx_end.clone()?;
            let mat = body.and_then(|b| physics.materials.get(&b).cloned()).unwrap_or_else(|| "snow".into());
            Some(format!("{base}_{mat}"))
        };
        if hit_roman {
            if let Some(n) = impact(None).map(|n| n.replace("_snow", "_grunt")) {
                fx.write(crate::fx::StartFx { name: n, at: from + step, rotation: Quat::IDENTITY, follow: None });
            }
            if let Ok(mut h) = roman_health.single_mut() {
                h.0 -= p.damage;
            }
            commands.entity(e).despawn();
            continue;
        }
        if let Some((body, at, normal)) = hit {
            if let Some(n) = impact(Some(body)) {
                let rot = Quat::from_rotation_arc(Vec3::Y, normal.normalize_or(Vec3::Y));
                fx.write(crate::fx::StartFx { name: n, at, rotation: rot, follow: None });
            }
            if let Some((_, _, mut h)) = chars.iter_mut().find(|(ce, b, _)| b.id == body && Some(*ce) != p.owner) {
                let was_alive = h.0 > 0.0;
                h.0 -= p.damage;
                if p.from_player && was_alive && h.0 <= 0.0 {
                    stats.kills += 1;
                }
            } else if let Some((_, mut d)) = objects.iter_mut().find(|(pl, _)| pl.collider == Some(body)) {
                // a direct hit (not a blast): kills exploding props at once (FxExplosion_Die, non-chain record)
                d.health -= p.damage;
                if d.health <= 0.0 && d.dying.is_none() {
                    d.chain_delay = 0.0;
                    d.dying = Some(0.0);
                }
            }
            commands.entity(e).despawn();
            continue;
        }
        t.translation = from + step;
        if p.life <= 0.0 {
            commands.entity(e).despawn();
        }
    }
}

/// Third-person camera behind Roman's shoulder at his yaw and pitch, pulled in when geometry is in the way.
fn follow_camera(
    physics: NonSend<Physics>,
    roman: Query<(&Roman, &Transform), Without<FollowCam>>,
    mut cam: Query<&mut Transform, With<FollowCam>>,
) {
    let (Ok((r, rt)), Ok(mut c)) = (roman.single(), cam.single_mut()) else { return };
    let pivot = rt.translation + Vec3::Y * 10.0;
    let rot = Quat::from_euler(EulerRot::YXZ, r.yaw, r.pitch, 0.0);
    let back = rot * Vec3::new(4.0, 2.0, 30.0);
    let len = back.length();
    let dist = physics.raycast(pivot, back / len, len).map(|h| h.1.distance(pivot) - 1.5).unwrap_or(len).max(3.0);
    c.translation = pivot + back / len * dist;
    c.rotation = rot;
}

pub fn spawn_bomb(commands: &mut Commands, physics: &mut Physics, bombs: &BombAssets, data: &GameData, at: Vec3, velocity: Vec3, fuse: f32) {
    let p = data.props("p_player_tickerbomb");
    let d = Destructible {
        health: 1.0,
        fx_death: Some(TICKERBOMB_FX.into()),
        husk: None,
        area_damage: p.f("area_damage", 100.0),
        radius_inner: p.f("damage_radius_inner", 40.0),
        radius_outer: p.f("damage_radius_outer", 60.0),
        impact_force: p.f("impact_force", 25000.0),
        chain: false,
        start_delay: 0.0,
        min_delay: 0.0,
        acceleration: 1.0,
        dying: None,
        chain_delay: 0.0,
    };
    let e = commands.spawn((Transform::from_translation(at).with_rotation(bombs.base), Visibility::default(), d, Fuse(fuse))).id();
    for (m, mat) in &bombs.parts {
        let c = commands.spawn((Mesh3d(m.clone()), MeshMaterial3d(mat.clone()))).id();
        commands.entity(e).add_child(c);
    }
    let settings = BodySettings::new_dynamic().mass(6.0).friction(0.6).restitution(0.3).angular_damping(0.5);
    if let Some(id) = physics.add_dynamic(e, &bombs.shape, at, bombs.base, velocity, settings) {
        commands.entity(e).insert(Body::rigid(id));
    }
}

/// The tickerbomb's explosion fx (guess: the sticky bomb's snow variant; the behaviour picks it in the game).
const TICKERBOMB_FX: &str = "fx_exp_sticky_snow";
