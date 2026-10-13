//! Grunt AI ported from the recovered state machine (GenericAI_Update 0x0043ee90 and its helpers, documented in
//! docs/subsystems/grunt-ai.md sections 3-5 and 11), on the game's own per-instance parameters. Reduced to the
//! states lev_1_m1 uses: idle/roaming (1), guard (0x79), wait for trigger (0x8c), chase (0x3c), firing (0xa) with the
//! fire cycle (clips x bursts x rounds, 180-degree cone + line of sight, aim error 0x004284e0), reload (0x32),
//! flee from a stuck sticky bomb (0xc8), limping, death and ragdoll. Hit-reaction states, cover/peek, taunts,
//! barricades and the alarm controller are not ported yet (see the doc).

use crate::fx::{FxStats, Health};
use crate::physics::{Body, Physics, bv, jv};
use bevy::prelude::*;
use oxijolt::prelude::*;
use std::time::Instant;

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Mind {
    /// state 1
    Roam,
    /// states 0x79 / 0xdd
    Guard,
    /// state 0x8c
    WaitTrigger,
    /// state 0x3c (and 2 / 0x50: waiting, going home)
    Chase,
    /// state 0xa with the fire cycle
    Fire,
    /// state 0x32
    Reload,
    /// state 0xc8
    Flee,
    Dead,
}

#[derive(Component)]
pub struct Grunt {
    pub mind: Mind,
    pub home: Vec3,
    pub goal: Vec3,
    pub timer: f32,
    pub base: Quat,
    pub yaw: f32,
    pub move_speed: f32,
    pub patrol_speed: f32,
    pub limp_speed: f32,
    pub limp_pct: f32,
    pub max_health: f32,
    pub scan_distance: f32,
    pub hear_distance: f32,
    /// engagement range +0x5d8, drawn once from fire_range_min..max
    pub range: f32,
    pub alert_group: i64,
    pub guard: bool,
    pub trigger_los: bool,
    pub search_timer: f32,
    pub target: Option<Entity>,
    /// alert-group hand-over (+0x578 / +0x57c)
    pub pending: Option<(Entity, f32)>,
    pub clips: (i64, i64),
    pub rounds: (i64, i64),
    pub bursts: (i64, i64),
    pub burst_delay: (f32, f32),
    pub clip_delay: (f32, f32),
    pub inaccuracy_angle: f32,
    pub inaccuracy_height: f32,
    pub accuracy: (f32, f32),
    pub infinite_accuracy: (f32, f32),
    pub infinite: bool,
    pub left_rounds: i64,
    pub left_bursts: i64,
    pub left_clips: i64,
    pub pause: f32,
    pub bullet_speed: f32,
    pub bullet_damage: f32,
    /// the weapon's and its projectile's property sets (p_grunt_rifle / p_grunt_bow and their projectiles)
    pub weapon: crate::gamedata::Props,
    pub bullet: crate::gamedata::Props,
    pub bow: bool,
    pub hit_ragdoll: f32,
    pub last_health: f32,
    pub last_hit: f32,
    pub hurt_voice_timer: f32,
    pub dead_said: bool,
    pub voice_set: u8,
    pub ray_timer: f32,
    /// died by a hit of at least hit_ragdoll (blown away as a body) rather than a death anim
    pub ragdoll: bool,
    /// which death anim (grunt_die_1..6) when not ragdolled
    pub death_anim: u8,
    /// chance in % of a pickup on death (pickup_drop_percent, default 100)
    pub drop_percent: f32,
}

/// What the AI hunts: Roman.
#[derive(Component)]
pub struct Player;

#[derive(Resource, Default)]
pub struct AiStats {
    pub ms: f32,
    pub alive: usize,
    pub dead: usize,
    pub shots: usize,
    pub enabled: bool,
    pub states: [usize; 8],
}

/// Sticky bomb stuck at a point (game_07.cpp:1240: AI within 25 units are scared).
#[derive(Message, Clone, Copy)]
pub struct StickyStuck(pub Vec3);

pub struct AiPlugin;

impl Plugin for AiPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(AiStats { enabled: true, ..default() })
            .add_message::<StickyStuck>()
            .add_systems(Update, (search, scare, think, die).chain().after(crate::fx::FxSet));
    }
}

/// Capsule of a character with the game's `collision_radius`, as tall as a grunt (9.8 units).
pub fn capsule(physics: &Physics, radius: f32) -> Shape {
    let k = physics.meters_per_unit;
    let r = radius.min(4.5) * 0.55;
    Shape::new_capsule((4.9 - r).max(0.5) * k, r * k).expect("capsule")
}

/// Height of the capsule's centre above the feet.
pub fn capsule_lift(_radius: f32) -> f32 {
    4.9 + 0.3
}

/// Ai_InitGeneric 0x004247f0 + the grunt init: parameters with the code's defaults, the mode's start state.
pub fn make_grunt(p: &crate::gamedata::Props, data: &crate::gamedata::GameData, home: Vec3, yaw: f32, rng: &mut crate::fxvm::CrtRand) -> Grunt {
    let mode = p.s("mode").unwrap_or("attention_roaming").to_owned();
    let (rmin, rmax) = (p.f("fire_range_min", 200.0), p.f("fire_range_max", 350.0));
    // grunt_regular carries the rifle, grunt the bow (guess, grunt-ai.md 1); projectile speed x3 (Projectile_Load)
    let bow = !p.has("max_health");
    let weapon = data.props(if bow { "p_grunt_bow" } else { "p_grunt_rifle" });
    let proj = data.props(&format!("p_{}", weapon.s("projectile_name").unwrap_or("grunt_rifle_bullet")));
    let move_speed = p.f("move_speed", 46.0);
    let guard = mode.contains("guard") || mode.contains("window") || mode.contains("tower");
    let mind = if mode.starts_with("trigger") { Mind::WaitTrigger } else if guard { Mind::Guard } else { Mind::Roam };
    let max_health = p.f("max_health", 100.0);
    let pick = rng.rand() * 300 >> 15;
    Grunt {
        mind,
        home,
        goal: home,
        timer: rng.r(15.0, 24.0),
        base: crate::level::game_to_engine(),
        yaw,
        move_speed,
        patrol_speed: p.f("patrol_speed", move_speed * 0.25),
        limp_speed: p.f("limp_speed", move_speed * 0.35),
        limp_pct: p.f("limp_pct", 0.0),
        max_health,
        scan_distance: p.f("scan_distance", 300.0),
        hear_distance: p.f("hear_distance", 20.0),
        range: rmin + rng.rand() as f32 * (rmax - rmin) / 32768.0,
        alert_group: p.i("alert_group", 0),
        guard,
        trigger_los: p.i("trigger_los", 1) != 0,
        search_timer: rng.r(0.0, 1.0),
        target: None,
        pending: None,
        clips: (p.i("fire_num_clips_min", 1), p.i("fire_num_clips_max", 1)),
        rounds: (p.i("fire_num_rounds_min", 1), p.i("fire_num_rounds_max", 1)),
        bursts: (p.i("fire_num_bursts_min", 1), p.i("fire_num_bursts_max", 1)),
        burst_delay: (p.f("fire_burst_delay_min", 0.0), p.f("fire_burst_delay_max", 0.0)),
        clip_delay: (p.f("fire_clip_delay_min", 0.0), p.f("fire_clip_delay_max", 0.0)),
        // the exe reads fire_inaccuracy_angle (default 15), not the data's fire_inaccuracy_xy
        inaccuracy_angle: p.f("fire_inaccuracy_angle", 15.0),
        inaccuracy_height: p.f("fire_inaccuracy_height", 10.0),
        accuracy: (p.f("fire_accuracy_min", 50.0), p.f("fire_accuracy_max", 150.0)),
        infinite_accuracy: (p.f("fire_infinite_accuracy_min", 400.0), p.f("fire_infinite_accuracy_max", 800.0)),
        infinite: false,
        left_rounds: 0,
        left_bursts: 0,
        left_clips: 0,
        pause: 0.0,
        bullet_speed: proj.f("speed", if bow { 25.0 } else { 150.0 }) * 3.0,
        bullet_damage: proj.f("damage", if bow { 10.0 } else { 15.0 }),
        bullet: proj.clone(),
        weapon,
        bow,
        hit_ragdoll: p.f("hit_ragdoll", 50.0),
        last_health: max_health,
        last_hit: 0.0,
        hurt_voice_timer: 0.0,
        dead_said: false,
        voice_set: if pick < 100 { 0 } else if pick < 200 { 1 } else { 2 },
        ray_timer: 0.0,
        ragdoll: false,
        death_anim: (rng.rand() % 6) as u8 + 1,
        drop_percent: p.f("pickup_drop_percent", 100.0),
    }
}

/// A grunt bark of a category (Fight, Hurt, Dead, Taunt, Help, Sticky_Scream, ...): audio.rs picks a line of
/// that category and voice set from the bank (audio_01.cpp; index = ((rand*range)>>15)/100).
pub fn voice(category: &str, set: u8) -> String {
    format!("@grunt:{category}:{set}")
}

fn bark(sounds: &mut MessageWriter<crate::audio::PlaySound>, category: &str, set: u8, at: Vec3) {
    sounds.write(crate::audio::PlaySound { name: voice(category, set), at: Some(at), follow: None, looped: Some(false), volume: 1.0 });
}

/// AI_FindTarget 0x00426920 and the alert-group hand-over: every 0.75..1.25 s; the player inside hear_distance is
/// taken at once; otherwise within scan_distance, inside the 190-degree cone, with line of sight.
fn search(
    time: Res<Time>,
    mut rng: ResMut<FxStats>,
    physics: NonSend<Physics>,
    player: Query<(Entity, &GlobalTransform), With<Player>>,
    mut grunts: Query<(Entity, &mut Grunt, &GlobalTransform)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let dt = time.delta_secs();
    let Ok((pe, pt)) = player.single() else { return };
    let ppos = pt.translation();
    let mut spotted: Vec<(i64, Entity, Entity)> = Vec::new();
    for (ge, mut g, t) in &mut grunts {
        if g.mind == Mind::Dead {
            continue;
        }
        if let Some((tgt, left)) = g.pending {
            if left - dt <= 0.0 {
                g.pending = None;
                if g.target.is_none() {
                    g.target = Some(tgt);
                    if g.mind == Mind::WaitTrigger {
                        g.mind = Mind::Roam;
                    }
                }
            } else {
                g.pending = Some((tgt, left - dt));
            }
        }
        g.search_timer -= dt;
        if g.search_timer > 0.0 || g.target.is_some() {
            continue;
        }
        // waiting for a trigger: only trigger_los grunts look (state 0x8c)
        if g.mind == Mind::WaitTrigger && !g.trigger_los {
            continue;
        }
        g.search_timer = rng.range(0.75, 1.25);
        let me = t.translation();
        let to = ppos - me;
        let d2 = to.with_y(0.0).length();
        if d2 > 2.0 * g.scan_distance {
            continue;
        }
        let heard = d2 < g.hear_distance;
        let seen = d2 < g.scan_distance && {
            let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
            let cone = to.with_y(0.0).normalize_or_zero().dot(fwd) >= 95f32.to_radians().cos();
            cone && line_of_sight(&physics, me, ppos)
        };
        if heard || seen {
            g.target = Some(pe);
            if g.mind == Mind::WaitTrigger {
                g.mind = Mind::Roam; // activate 0x0042f5c0 (charge/marker flags: not ported)
            }
            bark(&mut sounds, "Fight", g.voice_set, me);
            if g.alert_group != 0 {
                spotted.push((g.alert_group, pe, ge));
            }
        }
    }
    for (group, tgt, who) in spotted {
        for (ge, mut g, _) in &mut grunts {
            if ge != who && g.alert_group == group && g.target.is_none() && g.pending.is_none() && g.mind != Mind::Dead {
                g.pending = Some((tgt, rng.range(0.25, 1.0)));
            }
        }
    }
}

/// Line of sight from a grunt's eye to the target's ref_hit_00 (+5 z, 0x00426fd0): nothing static in between.
pub fn line_of_sight(physics: &Physics, from_feet: Vec3, to_feet: Vec3) -> bool {
    let eye = from_feet + Vec3::Y * 8.0;
    let aim = to_feet + Vec3::Y * 7.0;
    let v = aim - eye;
    physics.raycast(eye + v.normalize_or(Vec3::Y) * 4.0, v.normalize_or(Vec3::Y), (v.length() - 8.0).max(0.1)).is_none()
}

/// Scare 0x0042fb60: a sticky bomb stuck within 25 units makes a grunt flee 100 units from the player for 3-5 s
/// (run_scared), then charge him; a guard plays grenade_stuck and the Sticky_Scream bark instead of fleeing.
fn scare(
    mut stuck: MessageReader<StickyStuck>,
    mut rng: ResMut<FxStats>,
    player: Query<&GlobalTransform, With<Player>>,
    mut grunts: Query<(&mut Grunt, &GlobalTransform)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let ppos = player.single().map(|p| p.translation()).unwrap_or_default();
    for s in stuck.read() {
        for (mut g, t) in &mut grunts {
            if g.mind == Mind::Dead || t.translation().distance(s.0) >= 25.0 {
                continue;
            }
            bark(&mut sounds, "Sticky_Scream", g.voice_set, t.translation());
            if g.guard {
                continue;
            }
            let away = (t.translation() - ppos).with_y(0.0).normalize_or(Vec3::X);
            g.goal = t.translation() + away * 100.0;
            g.timer = rng.range(3.0, 5.0);
            g.mind = Mind::Flee;
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn think(
    mut commands: Commands,
    time: Res<Time>,
    mut stats: ResMut<AiStats>,
    mut rng: ResMut<FxStats>,
    mut physics: NonSendMut<Physics>,
    targets: Query<&GlobalTransform>,
    mut grunts: Query<(&mut Grunt, &Body, &mut Transform, &Health)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    let t0 = Instant::now();
    let dt = time.delta_secs();
    let k = physics.meters_per_unit;
    let (mut alive, mut dead) = (0, 0);
    let mut states = [0usize; 8];
    for (mut g, body, mut tr, h) in &mut grunts {
        if g.mind == Mind::Dead {
            dead += 1;
            continue;
        }
        alive += 1;
        states[g.mind as usize] += 1;
        // hurt: bark at most every 4 s (0x004394c0); a hurt grunt waiting for a trigger activates (ai_04.cpp:420)
        g.hurt_voice_timer -= dt;
        if h.0 < g.last_health && h.0 > 0.0 {
            g.last_hit = g.last_health - h.0;
            if g.hurt_voice_timer <= 0.0 {
                g.hurt_voice_timer = 4.0;
                bark(&mut sounds, "Hurt", g.voice_set, tr.translation);
            }
            if g.mind == Mind::WaitTrigger {
                g.mind = Mind::Roam;
            }
        }
        g.last_health = h.0;
        if !stats.enabled {
            continue;
        }
        let pos = tr.translation;
        let flat = |v: Vec3| Vec3::new(v.x, 0.0, v.z);
        let limping = g.limp_pct > 0.0 && h.0 < g.max_health * g.limp_pct / 100.0;
        let run_speed = if limping { g.limp_speed } else { g.move_speed };
        let target = g.target.and_then(|e| targets.get(e).ok()).map(|t| t.translation());
        g.timer -= dt;
        g.ray_timer -= dt;
        // a target farther than 2 x scan_distance is dropped (states 0x3c, 0xa)
        if let Some(p) = target {
            if flat(p - pos).length() > 2.0 * g.scan_distance && !g.guard {
                g.target = None;
            }
        }
        let target = g.target.and_then(|e| targets.get(e).ok()).map(|t| t.translation());
        let in_range = target.is_some_and(|p| flat(p - pos).length() <= g.range);
        // a guard whose target is beyond its range fires with the infinite accuracy ranges (ai_03.cpp:1383)
        g.infinite = g.guard && target.is_some() && !in_range;
        // engage 0x00427910
        g.mind = match (g.mind, target) {
            (Mind::Flee, _) if g.timer > 0.0 => Mind::Flee,
            (Mind::Flee, _) => {
                g.target = g.target.or(None);
                Mind::Chase
            }
            (Mind::Reload, _) if g.pause > 0.0 => Mind::Reload,
            (Mind::WaitTrigger, None) => Mind::WaitTrigger,
            (_, Some(_)) if in_range || g.guard => Mind::Fire,
            (_, Some(_)) => Mind::Chase,
            (Mind::Guard, None) | (Mind::Fire, None) if g.guard => Mind::Guard,
            _ => Mind::Roam,
        };
        let mut goal = pos;
        let mut speed = 0.0;
        match g.mind {
            Mind::Roam => {
                // stand-in patrol around home at patrol_speed (the original fidgets in place or follows markers)
                if g.timer <= 0.0 || flat(g.goal - pos).length() < 4.0 {
                    let a = rng.rand() * std::f32::consts::TAU;
                    g.goal = g.home + Vec3::new(a.cos(), 0.0, a.sin()) * rng.range(8.0, 40.0);
                    g.timer = rng.range(3.0, 7.0);
                }
                goal = g.goal;
                speed = g.patrol_speed;
            }
            Mind::Chase => {
                // pursue 0x004295d0: a point within +-20 of the target; every 0.2-0.4 s a ray decides to engage
                if let Some(p) = target {
                    if g.ray_timer <= 0.0 {
                        g.ray_timer = if limping { rng.range(0.27, 0.54) } else { rng.range(0.2, 0.4) };
                        g.goal = p + Vec3::new(rng.range(-20.0, 20.0), 0.0, rng.range(-20.0, 20.0));
                    }
                    goal = g.goal;
                    speed = run_speed;
                }
            }
            Mind::Flee => {
                goal = g.goal;
                speed = run_speed;
            }
            Mind::Fire => {
                if let Some(p) = target {
                    fire_cycle(&mut commands, &mut g, &physics, &mut rng, pos, p, dt, &mut stats, &mut sounds, &mut fx);
                }
            }
            Mind::Reload => g.pause -= dt,
            _ => {}
        }
        let dir = flat(goal - pos).normalize_or_zero();
        let face = match (g.mind, target) {
            (Mind::Fire, Some(p)) | (Mind::Chase, Some(p)) => flat(p - pos).normalize_or_zero(),
            _ => dir,
        };
        if face != Vec3::ZERO {
            let want = face.x.atan2(face.z);
            let d = (want - g.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
            g.yaw += d * (dt * 8.0).min(1.0);
        }
        tr.rotation = Quat::from_rotation_y(g.yaw) * g.base;
        if let Ok(mut b) = physics.world.body_mut(body.id) {
            let v = bv(b.linear_velocity());
            let want = dir * speed * k;
            let _ = b.set_linear_velocity(jv(Vec3::new(want.x, v.y, want.z)));
            if speed > 0.0 {
                b.activate();
            }
        }
    }
    stats.alive = alive;
    stats.dead = dead;
    stats.states = states;
    stats.ms = t0.elapsed().as_secs_f32() * 1000.0;
}

/// Fire cycle 0x00428980 with the counters of 0x00427600 and the aim error of 0x004284e0.
#[allow(clippy::too_many_arguments)]
fn fire_cycle(
    commands: &mut Commands,
    g: &mut Grunt,
    physics: &Physics,
    rng: &mut FxStats,
    pos: Vec3,
    p: Vec3,
    dt: f32,
    stats: &mut AiStats,
    sounds: &mut MessageWriter<crate::audio::PlaySound>,
    fx: &mut MessageWriter<crate::fx::StartFx>,
) {
    g.pause -= dt;
    if g.pause > 0.0 {
        return;
    }
    if g.left_clips <= 0 {
        g.left_clips = rng.range(g.clips.0 as f32, g.clips.1 as f32 + 0.999) as i64;
        g.left_bursts = 0;
    }
    if g.left_bursts <= 0 {
        g.left_bursts = rng.range(g.bursts.0 as f32, g.bursts.1 as f32 + 0.999) as i64;
        g.left_rounds = 0;
    }
    if g.left_rounds <= 0 {
        g.left_rounds = rng.range(g.rounds.0 as f32, g.rounds.1 as f32 + 0.999) as i64;
    }
    // fires only inside the 180-degree cone with line of sight
    let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
    let to = (p - pos).with_y(0.0);
    if to.normalize_or_zero().dot(fwd) < 0.0 || !line_of_sight(physics, pos, p) {
        g.pause = 0.25;
        return;
    }
    // aim error 0x004284e0: sideways L in [-angle, angle], up V in [0, height], scaled by s from the accuracy ranges
    let d = to.length();
    let (lo, hi) = if g.infinite { g.infinite_accuracy } else { g.accuracy };
    let s = if d <= lo { 0.1 } else if d >= hi { 1.0 } else { ((d * d - lo * lo) / (hi * hi - lo * lo)).max(0.1) };
    let side = Vec3::new(-to.z, 0.0, to.x).normalize_or_zero();
    let aim = p + Vec3::Y * 7.0 + side * rng.range(-g.inaccuracy_angle, g.inaccuracy_angle) * s + Vec3::Y * rng.range(0.0, g.inaccuracy_height) * s;
    let muzzle = pos + Vec3::Y * 7.0;
    let dir = (aim - muzzle).normalize_or(Vec3::Z);
    crate::player::spawn_projectile(commands, fx, &g.bullet, muzzle + dir * 4.0, dir, false, None);
    if let Some(m) = g.weapon.s("muzzle_fx_name") {
        fx.write(crate::fx::StartFx { name: m.to_owned(), at: muzzle + dir * 4.0, rotation: Transform::IDENTITY.looking_to(dir, Vec3::Y).rotation, follow: None, position_only: false });
    }
    sounds.write(crate::audio::PlaySound::at(if g.bow { "wpn_bow_fire_1" } else { "wpn_grunt_rifle_fire" }, pos));
    stats.shots += 1;
    g.left_rounds -= 1;
    g.pause = 0.15; // round spacing (per-shot timing not traced: weapon recycle)
    if g.left_rounds <= 0 {
        g.left_bursts -= 1;
        g.pause = rng.range(g.burst_delay.0, g.burst_delay.1).max(0.3);
        if g.left_bursts <= 0 {
            g.left_clips -= 1;
            g.pause = rng.range(g.clip_delay.0, g.clip_delay.1).max(0.6);
            if g.left_clips <= 0 {
                // clip cycle done: reload (state 0x32, grunt_reload_*), counters re-rolled after
                g.mind = Mind::Reload;
                g.pause = 1.5;
            }
        }
    }
}

/// Character_Die: Dead bark once; ragdoll (a tumbling rigid body here) when the killing hit was at least
/// hit_ragdoll, GoRagdoll impulse = direction x amount x 0.15. Death anims (grunt_die_*) come with skinning.
fn die(
    mut commands: Commands,
    mut physics: NonSendMut<Physics>,
    mut grunts: Query<(Entity, &mut Grunt, &Body, &Transform, &Health, Option<&mut crate::skin::Animator>)>,
    joints: Query<&GlobalTransform>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    for (e, mut g, body, tr, h, animator) in &mut grunts {
        if g.mind == Mind::Dead || h.0 > 0.0 {
            continue;
        }
        g.mind = Mind::Dead;
        if !g.dead_said {
            g.dead_said = true;
            bark(&mut sounds, "Dead", g.voice_set, tr.translation);
        }
        let push = physics.world.body(body.id).map(|b| bv(b.linear_velocity())).unwrap_or(Vec3::ZERO) / physics.meters_per_unit;
        physics.remove(body.id);
        // a hit below hit_ragdoll plays a death anim where he stands (Character_Die); harder hits ragdoll
        g.ragdoll = g.last_hit >= g.hit_ragdoll || push.length() > 30.0;
        if !g.ragdoll {
            commands.entity(e).remove::<Body>();
            continue;
        }
        // an articulated ragdoll from the pose at death (ragdoll.rs); the single box below when that fails
        if let Some(mut a) = animator {
            if let Some(rd) = crate::ragdoll::start(&mut physics, &a, &joints, push) {
                a.layers.clear();
                commands.entity(e).remove::<Body>().insert(rd);
                continue;
            }
        }
        let k = physics.meters_per_unit;
        let half = Vec3::new(2.6, 1.6, 4.8) * tr.scale * k;
        let Ok(shape) = Shape::new_box(jv(half)) else { continue };
        let center_local = Vec3::new(0.0, 0.0, 4.8);
        let origin = tr.translation + tr.rotation * (center_local * tr.scale);
        let settings = BodySettings::new_dynamic().mass(80.0).friction(0.8).angular_damping(0.4);
        if let Some(id) = physics.add_dynamic(e, &shape, origin, tr.rotation, push, settings) {
            commands.entity(e).insert(Body { id, offset: center_local, sync_rotation: true });
        }
    }
}

/// Roman's companions (Q = q111, Jonesy = molmer), STAND-IN: they keep formation behind him using their property
/// sets (formation_angle, formation_distance, delay_distance: they stay put while Roman is within delay_distance,
/// scan_distance, move_speed) and fire at the nearest grunt in range. The recovered companion AI replaces this.
#[derive(Component)]
pub struct Companion {
    pub walker: crate::physics::Walker,
    pub angle: f32,
    pub distance: f32,
    pub delay: f32,
    pub scan: f32,
    pub speed: f32,
    pub fire_timer: f32,
    pub base: Quat,
    pub yaw: f32,
    pub vy: f32,
    pub moving: bool,
}

pub struct CompanionPlugin;

impl Plugin for CompanionPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, companions.run_if(crate::playing));
    }
}

fn companions(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<Physics>,
    mut rng: ResMut<FxStats>,
    roman: Query<(&Transform, &crate::player::Roman), Without<Companion>>,
    mut q: Query<(&mut Companion, &mut Transform), Without<crate::player::Roman>>,
    grunts: Query<(&Grunt, &GlobalTransform)>,
) {
    let dt = time.delta_secs();
    let Ok((rt, r)) = roman.single() else { return };
    for (mut c, mut t) in &mut q {
        let slot_yaw = r.yaw + std::f32::consts::PI + c.angle.to_radians();
        let slot = rt.translation + Quat::from_rotation_y(slot_yaw) * Vec3::NEG_Z * c.distance;
        let to = Vec3::new(slot.x - t.translation.x, 0.0, slot.z - t.translation.z);
        let d = to.length();
        let from_roman = (rt.translation - t.translation).with_y(0.0).length();
        if from_roman > c.delay {
            c.moving = true;
        }
        if d < 6.0 {
            c.moving = false;
        }
        let speed = if c.moving { c.speed } else { 0.0 };
        let dir = to.normalize_or_zero();
        let vel = dir * speed + Vec3::Y * c.vy;
        if let Some((feet, on)) = physics.move_walker(&c.walker, vel, dt) {
            t.translation = feet;
            c.vy = if on { -5.0 } else { (c.vy - 160.0 * dt).max(-200.0) };
        }
        let target = grunts
            .iter()
            .filter(|(g, _)| g.mind != Mind::Dead)
            .map(|(_, gt)| gt.translation())
            .filter(|p| p.distance(t.translation) < c.scan)
            .min_by(|a, b| a.distance(t.translation).total_cmp(&b.distance(t.translation)));
        let face = target.map(|p| (p - t.translation).with_y(0.0)).filter(|v| v.length() > 0.1).unwrap_or(dir);
        if face != Vec3::ZERO {
            c.yaw = face.x.atan2(face.z);
        }
        t.rotation = Quat::from_rotation_y(c.yaw) * c.base;
        c.fire_timer -= dt;
        if let Some(p) = target {
            if c.fire_timer <= 0.0 {
                c.fire_timer = rng.range(0.15, 0.4);
                let muzzle = t.translation + Vec3::Y * 6.0;
                let dir = (p + Vec3::Y * 5.0 + Vec3::new(rng.rand() - 0.5, 0.0, rng.rand() - 0.5) * 4.0 - muzzle).normalize_or(Vec3::Z);
                commands.spawn((
                    Transform::from_translation(muzzle + dir * 4.0),
                    crate::player::Projectile { vel: dir * 900.0, life: 0.4, damage: 10.0, from_player: true, owner: None, fx_end: Some("fx_imp_flemming".into()) },
                ));
            }
        }
    }
}
