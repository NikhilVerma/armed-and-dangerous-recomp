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
    /// a guard's first fire cycle is dry (no shot: the counters are empty), then the reload: measured in the original
    /// (hook AI dump, second encounter: the guards play fire (anim 0x23) at 740..760, reload (0x2a) by 780..820, and
    /// their first shots come at 800..837; the sound log has none in between), as fire-timing note #7 predicted
    pub dry_first: bool,
    pub last_hit: f32,
    /// hit_level_1 / hit_level_2 (+0x9e8 / +0x9ec): the damage limits of the small and medium hit reactions
    pub hit_level_1: f32,
    pub hit_level_2: f32,
    /// a window / tower grunt's place (physics off there: 0x00425e20), held each frame
    pub fixed: Option<Vec3>,
    /// the hit reaction playing (states 0x12f small, 0x130 medium, 0x131 big): its anim, and its kind 1..3; the grunt
    /// neither moves nor fires meanwhile; charanim.rs clears it when the anim ends
    pub hit: Option<(String, u8)>,
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
    /// grunts holding a target: the player, a companion (playtest log, for comparisons with the hook's AI dump)
    pub targets: [usize; 2],
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
        // +0x490 starts at 0: the first search runs on the first frame
        search_timer: 0.0,
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
        dry_first: guard,
        last_hit: 0.0,
        hit_level_1: p.f("hit_level_1", 10.0),
        hit_level_2: p.f("hit_level_2", 25.0),
        hit: None,
        fixed: None,
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
    player: Query<(Entity, &GlobalTransform, Option<&crate::skin::Animator>), With<Player>>,
    companions: Query<(Entity, &GlobalTransform, Option<&crate::skin::Animator>), (With<Companion>, Without<Player>)>,
    mut grunts: Query<(Entity, &mut Grunt, &GlobalTransform, &Body, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform, Without<Grunt>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut stats: ResMut<AiStats>,
) {
    let dt = time.delta_secs();
    let Ok((pe, _, _)) = player.single() else { return };
    stats.targets = [0, 0];
    for (_, g, _, _, _) in &grunts {
        if let Some(t) = g.target.filter(|_| g.mind != Mind::Dead) {
            stats.targets[if t == pe { 0 } else { 1 }] += 1;
        }
    }
    // the candidates (the AI's list +0x604: the player and his companions; the original's grunts take Jonesy and Q
    // too, hook AI dump fightT): each with its position and its ref_hit_00 (the point the sight ray aims at)
    let hit_point = |t: &GlobalTransform, a: Option<&crate::skin::Animator>| {
        a.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| joints.get(a.joints[i]).ok())).map(|j| j.translation()).unwrap_or(t.translation() + Vec3::Y * 7.0)
    };
    let cands: Vec<(Entity, Vec3, Vec3)> =
        player.iter().chain(companions.iter()).map(|(e, t, a)| (e, t.translation(), hit_point(t, a))).collect();
    if std::env::var("AIDBG").is_ok() && time.elapsed_secs() < 0.5 {
        info!("AIDBG candidates player {:?} companions {:?}", pe, companions.iter().map(|c| c.0).collect::<Vec<_>>());
    }
    let mut spotted: Vec<(i64, Entity, Entity)> = Vec::new();
    for (ge, mut g, t, gbody, ga) in &mut grunts {
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
        if g.search_timer > 0.0 {
            continue;
        }
        // a guard (0x0042ec20, every frame of state 0x79) releases its target each time the search timer runs out and
        // searches again: one that loses the cone or the line of sight loses its target within 0.75..1.25 s
        if g.guard && g.target.is_some() {
            g.target = None;
        }
        if g.target.is_some() {
            continue;
        }
        // waiting for a trigger: only trigger_los grunts look (state 0x8c)
        if g.mind == Mind::WaitTrigger && !g.trigger_los {
            continue;
        }
        g.search_timer = rng.range(0.75, 1.25);
        let me = t.translation();
        // AI_FindTarget's walk of +0x604: within scan_distance (2D), the closest that passes; the player inside
        // hear_distance is taken at once; otherwise inside the 190-degree cone (or within hear_distance) with line of
        // sight
        let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
        // its own ref_hit_00 (0x00428280 with "ref_hit_00"): where the sight ray starts (raised by 5)
        let own_hit = ga.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| joints.get(a.joints[i]).ok())).map(|j| j.translation()).unwrap_or(me + Vec3::Y * 6.0);
        let mut best: Option<(f32, Entity, bool)> = None;
        for &(ce, cpos, chit) in &cands {
            let to = (cpos - me).with_y(0.0);
            let d2 = to.length();
            if d2 > g.scan_distance || best.is_some_and(|b| d2 > b.0) {
                continue;
            }
            if ce == pe && d2 < g.hear_distance {
                best = Some((d2, ce, true));
                break;
            }
            let cone = to.normalize_or_zero().dot(fwd) >= 95f32.to_radians().cos() || d2 < g.hear_distance;
            if cone && sight_ray(&physics, own_hit, chit, &[gbody.id]) {
                best = Some((d2, ce, false));
            }
        }
        if let Some((d2, tgt, heard)) = best {
            g.target = Some(tgt);
            if g.mind == Mind::WaitTrigger {
                g.mind = Mind::Roam; // activate 0x0042f5c0 (charge/marker flags: not ported)
            }
            if std::env::var("AIDBG").is_ok() { info!("AIDBG spotted {:?} by {:?} at {:?} d {:.0} heard {} mode {:?}", tgt, ge, me, d2, heard, g.mind); }
            // no bark on spotting: the original's grunts engage and shoot without one (sound log of the first fight:
            // bow shots from frame 696, no _fight line); the Fight bark (vt+0x140) is called from other paths
            if g.alert_group != 0 {
                spotted.push((g.alert_group, tgt, ge));
            }
        }
    }
    for (group, tgt, who) in spotted {
        for (ge, mut g, _, _, _) in &mut grunts {
            if ge != who && g.alert_group == group && g.target.is_none() && g.pending.is_none() && g.mind != Mind::Dead {
                g.pending = Some((tgt, rng.range(0.25, 1.0)));
            }
        }
    }
}

/// Line of sight from a grunt's eye to the target's ref_hit_00 (+5 z, 0x00426fd0): nothing static in between.
/// Line of sight as 0x00426fd0: from the source raised by 5 (0x005bcbdc) to the target point (its ref_hit_00), true
/// when nothing is hit on the way; bodies in `skip` (the looker's own) are passed through.
pub fn sight_ray(physics: &Physics, from_hit: Vec3, target: Vec3, skip: &[BodyId]) -> bool {
    let mut from = from_hit + Vec3::Y * 5.0;
    let dir = (target - from).normalize_or(Vec3::Y);
    let mut left = (target - from).length();
    while let Some((b, at, _)) = physics.raycast(from, dir, left) {
        if !skip.contains(&b) {
            return false;
        }
        let step = at.distance(from) + 0.05;
        from = at + dir * 0.05;
        left -= step;
        if left <= 0.0 {
            break;
        }
    }
    true
}

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
    anims: Query<&crate::skin::Animator, Without<Grunt>>,
    mut grunts: Query<(&mut Grunt, &Body, &mut Transform, &Health, Option<&crate::skin::Animator>)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    rtime: Res<Time<Real>>,
) {
    let t0 = Instant::now();
    let dt = time.delta_secs();
    let k = physics.meters_per_unit;
    let (mut alive, mut dead) = (0, 0);
    let mut states = [0usize; 8];
    for (mut g, body, mut tr, h, ga) in &mut grunts {
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
            // the hit reaction (GenericAIObject_Slot52 0x00430340, anims from GruntObject 0x0043bd70): damage <=
            // hit_level_1 a small hit (one of ids 0x3f..0x42 by rand * 400 >> 15: grunt_bow_hit_1, grunt_hit_mgun_2,
            // grunt_hit_mgun_3, grunt_hit_4; not again while one plays), <= hit_level_2 a medium hit (0x43 / 0x44
            // alternating, the first by rand: grunt_hit_1 / _2), else a big hit (0x45 / 0x46: grunt_hit_3)
            let d = g.last_hit;
            if d <= g.hit_level_1 {
                if g.hit.as_ref().is_none_or(|h| h.1 != 1) {
                    let q = (rng.rand() * 400.0) as u32;
                    let n = ["grunt_bow_hit_1", "grunt_hit_mgun_2", "grunt_hit_mgun_3", "grunt_hit_4"][(q / 100).min(3) as usize];
                    g.hit = Some((n.to_owned(), 1));
                }
            } else if d <= g.hit_level_2 {
                let prev = g.hit.as_ref().map(|h| h.0.clone());
                let n = match prev.as_deref() {
                    Some("grunt_hit_1") => "grunt_hit_2",
                    Some("grunt_hit_2") => "grunt_hit_1",
                    _ if rng.rand() < 0.5 => "grunt_hit_1",
                    _ => "grunt_hit_2",
                };
                g.hit = Some((n.to_owned(), 2));
            } else {
                g.hit = Some(("grunt_hit_3".to_owned(), 3));
            }
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
        // a hit reaction holds the grunt (states 0x12f..0x131)
        let reacting = g.hit.is_some();
        match if reacting { Mind::Dead } else { g.mind } {
            Mind::Roam => {
                // idle / roaming (state 1, grunt-ai.md): with no target the grunt stands at attention where it is and
                // fidgets (charanim.rs); it only moves along a patrol spline, which no lev_1_m1 grunt has. (The
                // engine's earlier stand-in wandered round home, marching with footsteps the original never plays.)
                goal = pos;
                speed = 0.0;
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
                    // the target's ref_hit_00 (0x00428280): the aim point of 0x004284e0 and the sight ray's end
                    let hit = g
                        .target
                        .and_then(|e| anims.get(e).ok())
                        .and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| targets.get(a.joints[i]).ok()))
                        .map(|j| j.translation())
                        .unwrap_or(p + Vec3::Y * 7.0);
                    let own_hit = ga.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| targets.get(a.joints[i]).ok())).map(|j| j.translation()).unwrap_or(pos + Vec3::Y * 6.0);
                    let before = stats.shots;
                    fire_cycle(&mut commands, &mut g, &physics, &mut rng, pos, p, hit, own_hit, body.id, dt, &mut stats, &mut sounds, &mut fx);
                    if stats.shots > before && std::env::var("AIDBG").is_ok() {
                        info!("AIDBG gruntshot t {:.3}", rtime.elapsed_secs());
                    }
                }
            }
            Mind::Reload => g.pause -= dt,
            _ => {}
        }
        // a window / tower grunt stays at its place
        if let Some(f) = g.fixed {
            goal = pos;
            speed = 0.0;
            let k = physics.meters_per_unit;
            if let Ok(mut b) = physics.world.body_mut(body.id) {
                if (pos - f).length() > 0.5 {
                    let _ = b.set_position(crate::physics::jr((f + body.offset) * k), Activation::Activate);
                }
                let _ = b.set_linear_velocity(jv(Vec3::ZERO));
            }
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
    hit: Vec3,
    own_hit: Vec3,
    own: BodyId,
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
    if to.normalize_or_zero().dot(fwd) < 0.0 || !sight_ray(physics, own_hit, hit, &[own]) {
        // no shot this frame (a guard: state 0x7a and back, the target dropped at the next search tick)
        return;
    }
    // aim error 0x004284e0: sideways L in [-angle, angle], up V in [0, height], scaled by s from the accuracy ranges
    let d = to.length();
    let (lo, hi) = if g.infinite { g.infinite_accuracy } else { g.accuracy };
    let s = if d <= lo { 0.1 } else if d >= hi { 1.0 } else { ((d * d - lo * lo) / (hi * hi - lo * lo)).max(0.1) };
    let side = Vec3::new(-to.z, 0.0, to.x).normalize_or_zero();
    let aim = hit + side * rng.range(-g.inaccuracy_angle, g.inaccuracy_angle) * s + Vec3::Y * rng.range(0.0, g.inaccuracy_height) * s;
    let muzzle = pos + Vec3::Y * 7.0;
    let dir = (aim - muzzle).normalize_or(Vec3::Z);
    if g.dry_first {
        // the dry first cycle: the fire anim without a shot, then the reload as after an empty clip
        g.dry_first = false;
        g.left_clips = 0;
        g.mind = Mind::Reload;
        g.pause = 0.467 + if g.bow { 1.3 } else { 1.133 } + 2.0 / 60.0;
        return;
    }
    crate::player::spawn_projectile(commands, fx, &g.bullet, muzzle + dir * 4.0, dir, false, None);
    if let Some(m) = g.weapon.s("muzzle_fx_name") {
        fx.write(crate::fx::StartFx { name: m.to_owned(), at: muzzle + dir * 4.0, rotation: Transform::IDENTITY.looking_to(dir, Vec3::Y).rotation, follow: None, position_only: false });
    }
    // (the shot sound comes from the weapon's muzzle fx: the original's sound log has one per shot)
    let _ = &sounds;
    stats.shots += 1;
    // the fire cycle (0x00428980, counters 0x00427600, as the companions'): a shot on the fire anim's note (0.033 s
    // into its 0.467 s loop; here at its start), fire_num_rounds per clip. An empty clip plays the reload anim
    // (grunt_bow_reload_* 1.3 s, grunt_reload_br 1.133 s); a guard (state 0x79, ai_06.cpp:2656) then re-rolls the
    // counters on the first-call path, without fire_clip_delay, and fires the next frame: anim + 1 frame + reload +
    // 1 frame = 108 frames for the archer (the original's 837 -> 945); a moving grunt (state 0x32) waits the clip delay
    g.left_rounds -= 1;
    g.pause = 0.467;
    if g.left_rounds <= 0 {
        g.left_bursts -= 1;
        g.pause = 0.467 + rng.range(g.burst_delay.0, g.burst_delay.1);
        if g.left_bursts <= 0 {
            g.left_clips -= 1;
            g.mind = Mind::Reload;
            let reload = if g.bow { 1.3 } else { 1.133 };
            g.pause = 0.467 + reload + if g.guard { 2.0 / 60.0 } else { rng.range(g.clip_delay.0, g.clip_delay.1) };
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

/// Roman's companions, Q (q111, Q111Object 0x005a6df8) and Jonesy (molmer, MolmerObject 0x005ae5f8): their shared
/// update MolmerObject_Slot47 0x00485910 (ai_08.cpp).
/// - Formation: the slot is Roman's bone ref_q_00 / ref_molmer_00 (0x00483d60); idle (mode 1) until the 2D distance
///   to Roman exceeds formation_distance + 2.5 (0x004860e2), then run to the slot (mode 2) at speed (d / delay) x
///   move_speed below 2 x delay_distance, else move_speed (0x00486cb0), d the distance to Roman; idle again there.
///   (The leader swap of the two slots, 0x00483810, is not ported.)
/// - Targets (0x00485490 -> AI_FindTarget 0x00426920): the nearest live grunt within scan_distance, in the 190 degree
///   facing cone and in line of sight; refused when farther than scan_distance from Roman (0x004853a0). Lost or dead:
///   back to formation after 3..7 s.
/// - Support fire (0x00485580): following, no target, Roman's Fire held and his heading within 45 degrees of the
///   companion's: the fire cycle at Roman's aim point (Game+0xa6c), turning toward Roman's heading.
/// - Fire cycle (0x00428980, counters 0x00427600): bursts of fire_num_bursts shots on the fire anims' notes (Jonesy:
///   the 4 blended 0.3 s anims each fire at 0: 4 at once; Q: 6 notes per 0.633 s loop), fire_burst_delay between
///   bursts, fire_num_rounds per clip, then the reload anim and fire_clip_delay. Shots: the weapon's projectile at the
///   aim point (+ scatter up to fire_inaccuracy_height); the muzzle fx carries the sound (Q's is one fx per burst, its
///   machine-gun loop).
/// Not ported: the slot swap, defend / attack orders, retaliation, Q's healthy tea, barks.
#[derive(Component)]
pub struct Companion {
    pub walker: crate::physics::Walker,
    pub q: bool,
    pub distance: f32,
    pub delay: f32,
    pub scan: f32,
    pub speed: f32,
    pub base: Quat,
    pub yaw: f32,
    pub vy: f32,
    pub moving: bool,
    /// the fire anim playing (anims), reloading (seconds left of the reload anim)
    pub firing: bool,
    pub reloading: f32,
    /// Jonesy's TNT throw (anim 39 molmer_throw_tnt, 2.3 s): the time into it
    pub throwing: Option<f32>,
    /// +0x484 flag 1: a throw is allowed; set when the fire counters are rolled after a full clip cycle (0x00427600,
    /// at spawn and at the reload's end), cleared by the throw (0x00428340)
    throw_armed: bool,
    /// the engagement range +0x5d8, rolled once in [fire_range_min, fire_range_max] (0x00425d30); negative: not yet
    range: f32,
    fire_range: (f32, f32),
    /// in range of the target (engaged, state 0xa); before that it walks toward it (state 0x3c)
    engaged: bool,
    /// aim elevation weight 0 (down) .. 1 (up), for the anim blend
    pub aim_v: f32,
    target: Option<Entity>,
    lost: f32,
    /// AI_FindTarget's timer +0x490: a search every 0.75..1.25 s
    search: f32,
    rounds: u32,
    burst: u32,
    loop_t: f32,
    delay_t: f32,
    muzzle: Option<Entity>,
    p: CompanionFire,
    /// the route to the slot (0x0046f6d0, re-planned every 0.25 s while moving: nav.rs A*)
    route: Vec<Vec3>,
    replan: f32,
    /// Q's healthy tea (0x004857e0, mode 9): the drop anim's time left, and given once (+0xbcc 0x20000)
    pub tea: f32,
    tea_given: bool,
    /// placed on its formation point beside Roman at the start (0x004836b0 from the player's reset 0x00475650)
    spawned: bool,
}

#[derive(Clone)]
struct CompanionFire {
    rounds: (u32, u32),
    bursts: (u32, u32),
    burst_delay: (f32, f32),
    clip_delay: (f32, f32),
    height: f32,
    /// fire_inaccuracy_angle (the exe's key, default 15), fire_accuracy_min / max (50 / 150)
    angle: f32,
    accuracy: (f32, f32),
    bullet: String,
    muzzle_fx: String,
}

impl Companion {
    pub fn new(walker: crate::physics::Walker, p: &crate::gamedata::Props, data: &crate::gamedata::GameData, q: bool, yaw: f32, base: Quat) -> Self {
        let weapon = data.props(if q { "p_q_chaingun" } else { "p_molmer_pistol" });
        let fire = CompanionFire {
            rounds: (p.i("fire_num_rounds_min", 30) as u32, p.i("fire_num_rounds_max", 30) as u32),
            bursts: (p.i("fire_num_bursts_min", 4) as u32, p.i("fire_num_bursts_max", 4) as u32),
            burst_delay: (p.f("fire_burst_delay_min", 0.2), p.f("fire_burst_delay_max", 0.5)),
            clip_delay: (p.f("fire_clip_delay_min", 0.5), p.f("fire_clip_delay_max", 1.0)),
            height: p.f("fire_inaccuracy_height", 1.0),
            angle: p.f("fire_inaccuracy_angle", 15.0),
            accuracy: (p.f("fire_accuracy_min", 50.0), p.f("fire_accuracy_max", 150.0)),
            bullet: format!("p_{}", weapon.s("projectile_name").unwrap_or("molmer_bullet")),
            muzzle_fx: weapon.s("muzzle_fx_name").unwrap_or("").to_owned(),
        };
        Companion {
            walker,
            q,
            distance: p.f("formation_distance", 30.0),
            delay: p.f("delay_distance", 50.0),
            scan: p.f("scan_distance", 300.0),
            speed: p.f("move_speed", 46.0),
            base,
            yaw,
            vy: -5.0,
            moving: false,
            firing: false,
            reloading: 0.0,
            throwing: None,
            throw_armed: true,
            range: -1.0,
            engaged: false,
            fire_range: (p.f("fire_range_min", 200.0), p.f("fire_range_max", 350.0)),
            aim_v: 0.5,
            target: None,
            lost: 0.0,
            search: 0.0,
            rounds: fire.rounds.0,
            burst: fire.bursts.0,
            loop_t: 0.0,
            delay_t: 0.0,
            muzzle: None,
            p: fire,
            route: Vec::new(),
            replan: 0.0,
            tea: 0.0,
            tea_given: false,
            spawned: false,
        }
    }
}

pub struct CompanionPlugin;

impl Plugin for CompanionPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, companions.run_if(crate::playing));
    }
}

/// The fire notes of one loop of the blended fire anims (seconds into the loop) and the loop's length: Jonesy's
/// molmer_fire_tl/tr/bl/br (0.3 s) each fire at 0; Q's q111_fire_bl (0.033, 0.467) and _br (0, 0.133, 0.3, 0.467).
const JONESY_NOTES: (&[f32], f32) = (&[0.0, 0.0, 0.0, 0.0], 0.3);
const Q_NOTES: (&[f32], f32) = (&[0.0, 0.033, 0.133, 0.3, 0.467, 0.467], 0.633);

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn companions(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<Physics>,
    mut rng: ResMut<FxStats>,
    data: Res<crate::gamedata::GameData>,
    buttons: Res<ButtonInput<MouseButton>>,
    bot: Res<crate::playtest::Bot>,
    roman: Query<(&Transform, &crate::player::Roman, &crate::skin::Animator, &Health), Without<Companion>>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    mut q: Query<(Entity, &mut Companion, &mut Transform, Option<&crate::skin::Animator>, Option<&Health>), Without<crate::player::Roman>>,
    grunts: Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform, Without<Companion>>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut log: ResMut<crate::playtest::PlayState>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    (mut pickups, mut dynamite): (MessageWriter<crate::pickup::SpawnPickup>, MessageWriter<crate::sticky::ThrowDynamite>),
) {
    let dt = time.delta_secs();
    let Ok((rt, r, ra, rh)) = roman.single() else { return };
    let bone = |a: &crate::skin::Animator, n: &str| a.skeleton.index.get(n).and_then(|&i| joints.get(a.joints[i]).ok()).map(|t| t.translation());
    // Roman's aim point (Game+0xa6c): where the crosshair ray meets the world, else far along it
    let aim_point = cam.single().ok().map(|c| {
        let (o, d) = (c.translation(), c.forward().as_vec3());
        physics.raycast(o + d * 20.0, d, 500.0).map(|h| h.1).unwrap_or(o + d * 520.0)
    });
    let fire_held = (buttons.pressed(MouseButton::Left) || bot.fire_ok) && !r.dead;
    for (e, mut c, mut t, ca, ch) in &mut q {
        if c.range < 0.0 {
            c.range = rng.range(c.fire_range.0, c.fire_range.1);
        }
        let c = &mut *c;
        // at the start: on the formation point (Roman's ref_q_00 / ref_molmer_00), grounded (0x00455860)
        if !c.spawned {
            if let Some(slot) = bone(ra, if c.q { "ref_q_00" } else { "ref_molmer_00" }) {
                let feet = physics.raycast(slot + Vec3::Y * 1000.0, -Vec3::Y, 10000.0).map(|h| h.1).unwrap_or(slot);
                physics.place_walker(&c.walker, feet);
                t.translation = feet;
                c.yaw = r.yaw + std::f32::consts::PI;
                c.spawned = true;
            }
        }
        let me = t.translation;
        // Q's healthy tea (Q11Voice_PlayAmbient 0x004857e0): once, when Roman's health is at most a quarter of its
        // maximum and he is within 50 units: a line (50 %: q1150174_Ambient_Spec, else q1150026_Ambient at Q), the
        // drop_tea anim (id 0x5a, 7.63 s), then (mode 9) "pickup_healthytea" thrown from Q + 4 up toward Roman at
        // normalise(2D direction, 0.7 up) x 40
        if c.q && c.tea > 0.0 {
            c.tea -= dt;
            if c.tea <= 0.0 {
                let to = (rt.translation - me).with_y(0.0).normalize_or_zero();
                let v = Vec3::new(to.x, 0.7, to.z).normalize() * 40.0;
                pickups.write(crate::pickup::SpawnPickup { name: "pickup_healthytea".into(), at: me + Vec3::Y * 4.0, vel: v });
            }
            continue;
        }
        if c.q && !c.tea_given && !r.dead && rh.0 <= r.max_health * 0.25 && rt.translation.distance(me) < 50.0 {
            c.tea_given = true;
            c.tea = 7.633;
            c.moving = false;
            c.firing = false;
            if rng.rand() < 0.5 {
                sounds.write(crate::audio::PlaySound::ui("q1150174_Ambient_Spec"));
            } else {
                sounds.write(crate::audio::PlaySound::at("q1150026_Ambient", me));
            }
            continue;
        }
        // targets
        if let Some(tg) = c.target {
            if grunts.get(tg).map_or(true, |(_, g, _, _)| g.mind == Mind::Dead) {
                c.target = None;
                c.lost = rng.range(3.0, 7.0);
            }
        }
        c.search -= dt;
        // at each search tick a target farther (2D) than scan_distance from Roman is refused (0x004853a0 in formation)
        // and dropped: the original's companions stand and fight until Roman has moved on (fightT: Jonesy lets go at
        // ~1350, Q at ~1440, then they run after him)
        if c.search <= 0.0 {
            if let Some((_, _, gt, _)) = c.target.and_then(|t| grunts.get(t).ok()) {
                if (gt.translation() - rt.translation).with_y(0.0).length() > c.scan {
                    c.target = None;
                }
            }
            if c.target.is_some() {
                c.search = rng.range(0.75, 1.25);
            }
        }
        if c.target.is_none() && c.search <= 0.0 {
            c.search = rng.range(0.75, 1.25);
            let face = Quat::from_rotation_y(c.yaw) * Vec3::Z;
            c.target = grunts
                .iter()
                .filter(|(_, g, _, _)| g.mind != Mind::Dead)
                .map(|(ge, _, gt, _)| (ge, gt.translation()))
                .filter(|(_, p)| (*p - me).with_y(0.0).length() < c.scan && (*p - rt.translation).with_y(0.0).length() < c.scan)
                .filter(|(_, p)| (*p - me).with_y(0.0).normalize_or_zero().dot(face) > (95.0f32).to_radians().cos())
                .filter(|(ge, p)| {
                    let eye = me + Vec3::Y * 8.0;
                    let to = *p + Vec3::Y * 6.0 - eye;
                    physics.raycast(eye, to.normalize_or(Vec3::Y), to.length()).is_none_or(|(b, _, _)| physics.bodies.get(&b) == Some(ge))
                })
                .min_by(|a, b| a.1.distance(me).total_cmp(&b.1.distance(me)))
                .map(|(ge, _)| ge);
            if std::env::var("AIDBG").is_ok() {
                if let Some((_, _, gt, _)) = c.target.and_then(|t| grunts.get(t).ok()) {
                    info!("AIDBG companion q={} targets grunt at {:?} d {:.0} me {:?} roman {:?}", c.q, gt.translation(), gt.translation().distance(me), me, rt.translation);
                }
            }
        }
        // engage 0x00427910: a target beyond the rolled range +0x5d8 is approached (state 0x3c: every 0.2..0.4 s, in
        // range when within 0.9 x +0x5d8 with a clear ray); in range, the engage roll with allow 1: a class that can
        // throw (Molmer's vt+0x1e0; not Q) with flag 1 set throws TNT when rand 0..89 < 30, else fires (state 0xa).
        // The original's dump (fightT): Jonesy walks from 181 to 130 units and Q to 153 before firing
        let tdist = c.target.and_then(|tg| grunts.get(tg).ok()).map(|(_, _, gt, _)| (gt.translation() - me).with_y(0.0).length());
        let in_range = |c: &Companion| tdist.is_some_and(|d| d < c.range);
        match tdist {
            None => c.engaged = false,
            Some(d) if !c.engaged && d <= 0.9 * c.range => {
                c.engaged = true;
                if !c.q && c.throw_armed && c.throwing.is_none() && rng.rand() * 90.0 < 30.0 {
                    c.throwing = Some(0.0);
                }
            }
            _ => {}
        }
        let approach = c.target.filter(|_| !c.engaged).and_then(|tg| grunts.get(tg).ok()).map(|(_, _, gt, _)| (gt.translation() - me).with_y(0.0));
        if c.lost > 0.0 {
            c.lost -= dt;
        }
        // formation: Roman's bone for this companion, regroup beyond formation_distance + 2.5
        let slot = bone(ra, if c.q { "ref_q_00" } else { "ref_molmer_00" }).unwrap_or(rt.translation);
        let from_roman = (rt.translation - me).with_y(0.0).length();
        // playtest log: how far each companion is from Roman
        log.0.insert(if c.q { "q_dist".into() } else { "jonesy_dist".into() }, (from_roman.round() as i64).into());
        log.0.insert(if c.q { "q_pos".into() } else { "jonesy_pos".into() }, serde_json::json!([(me.x * 10.0).round() / 10.0, (-me.z * 10.0).round() / 10.0]));
        log.0.insert(if c.q { "q_health".into() } else { "jonesy_health".into() }, ch.map(|h| h.0).unwrap_or(0.0).into());
        // measured in the original's intro (hook dumps of World+0xe94): both set off within 0.5 s of Roman and run at
        // ~38 units/s about 32 units behind him; so they regroup on the distance to their slot, not to Roman
        let _ = from_roman;
        if c.target.is_none() && c.lost <= 0.0 && (slot - me).with_y(0.0).length() > 15.0 {
            c.moving = true;
        }
        // with a target they stand and fight (the original's dump, fightT: Q fires from (-232, 884) and Jonesy from
        // (-221, 865) without moving until they let go of the target)
        if c.target.is_some() && c.moving {
            c.moving = false;
            c.route.clear();
        }
        let to_slot = (slot - me).with_y(0.0);
        if to_slot.length() < 4.0 {
            c.moving = false;
            c.route.clear();
        }
        if c.moving {
            c.replan -= dt;
            if c.replan <= 0.0 {
                c.replan = 0.25;
                c.route = crate::nav::path(&physics, me, slot, &[]).unwrap_or_default();
            }
            while c.route.first().is_some_and(|w| (*w - me).with_y(0.0).length() < 4.0) {
                c.route.remove(0);
            }
        }
        // the run anim while approaching (moving is cleared again next frame while it has a target)
        if approach.is_some() {
            c.moving = true;
        }
        let to = approach.unwrap_or_else(|| c.route.first().map(|w| (*w - me).with_y(0.0)).unwrap_or(to_slot));
        // full move_speed while moving (the original's intro: 38 of 40 units/s at 32 units from Roman)
        // fitted: the original's companions cover 37.3 units/s at move_speed 40 (its drive, move_power 100, not ported)
        let speed = if c.moving || approach.is_some() { c.speed * 0.933 } else { 0.0 };
        let dir = to.normalize_or_zero();
        let vel = dir * speed + Vec3::Y * c.vy;
        if let Some((feet, on)) = physics.move_walker(&c.walker, vel, dt) {
            t.translation = feet;
            c.vy = if on { -5.0 } else { (c.vy - 160.0 * dt).max(-200.0) };
        }
        // what to shoot at: the target's ref_hit_00, or (support fire) Roman's aim point
        let target_pos = c.target.and_then(|tg| grunts.get(tg).ok()).map(|(_, _, gt, a)| a.and_then(|a| bone(a, "ref_hit_00")).unwrap_or(gt.translation() + Vec3::Y * 6.0));
        let roman_heading = r.yaw + std::f32::consts::PI;
        let heading_ok = {
            let d = (roman_heading - c.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
            d.abs() <= 45f32.to_radians()
        };
        let support = c.target.is_none() && !c.moving && fire_held && heading_ok;
        let shoot_at = if approach.is_some() { None } else { target_pos.or(if support { aim_point } else { None }) };
        // facing: the target, else (support fire / following) Roman's heading, else the way it runs
        let face = target_pos.map(|p| (p - me).with_y(0.0)).filter(|v| v.length() > 0.1);
        if let Some(f) = face {
            c.yaw = f.x.atan2(f.z);
        } else if c.moving && dir != Vec3::ZERO {
            c.yaw = dir.x.atan2(dir.z);
        } else if fire_held {
            let d = (roman_heading - c.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
            c.yaw += d * (8.0 * dt).min(1.0);
        }
        t.rotation = Quat::from_rotation_y(c.yaw) * c.base;
        // the fire cycle
        let (notes, loop_len) = if c.q { Q_NOTES } else { JONESY_NOTES };
        if let Some(tt) = c.throwing {
            // molmer_throw_tnt (2.3 s): make_tnt 0.467 (the pistol hidden: not shown), FX fx_wpn_grenade_throw at 1.667
            // (its sound wpn_grenade_throw; started by the anim note, gameaudio.rs), Fire_tnt at 1.733: the dynamite from the hand (0x00428340); then the fire
            // cycle again (state 0x10's end: 50% fire, 50% close in first, not ported)
            c.firing = false;
            let now = tt + dt;
            let crossed = |n: f32| tt < n && now >= n;
            let hand = ca.and_then(|a| bone(a, "ref_hand_00")).unwrap_or(me + Vec3::Y * 8.0);
            if crossed(1.733) {
                c.throw_armed = false;
                if let Some((ge, _, gt, ga)) = c.target.and_then(|tg| grunts.get(tg).ok()) {
                    let point = ga.and_then(|a| bone(a, "ref_hit_00")).unwrap_or(gt.translation() + Vec3::Y * 6.0);
                    let n = (rng.rand() * 7.0) as usize;
                    let joint = ga.and_then(|a| a.skeleton.index.get(&format!("ref_hit_0{n}")).map(|&i| a.joints[i]));
                    dynamite.write(crate::sticky::ThrowDynamite { from: hand, target: ge, joint, point });
                }
            }
            c.throwing = if now >= 2.3 || (c.target.is_none() && tt < 1.733) { None } else { Some(now) };
        } else if c.reloading > 0.0 {
            c.reloading -= dt;
            c.firing = false;
            if c.reloading <= 0.0 {
                c.rounds = rng.range(c.p.rounds.0 as f32, c.p.rounds.1 as f32 + 0.999) as u32;
                c.burst = rng.range(c.p.bursts.0 as f32, c.p.bursts.1 as f32 + 0.999) as u32;
                // the counters rolled after the clip cycle set flag 1 again; then the engage roll (state 0x32's end)
                c.throw_armed = true;
                if !c.q && in_range(&*c) && rng.rand() * 90.0 < 30.0 {
                    c.throwing = Some(0.0);
                }
            }
        } else if let Some(at) = shoot_at.filter(|_| std::env::var("NOCOMPFIRE").is_err()) {
            let muzzle = me + Vec3::Y * 7.0 + Quat::from_rotation_y(c.yaw) * Vec3::Z * 3.0;
            let dz = at - muzzle;
            let deg = (dz.y / dz.length().max(1e-3)).clamp(-1.0, 1.0).acos().to_degrees() - 90.0;
            c.aim_v = (deg.clamp(-26.5, 26.5) + 26.5) / 53.0;
            if c.delay_t > 0.0 {
                c.delay_t -= dt;
                c.firing = false;
            } else {
                c.firing = true;
                let prev = c.loop_t;
                c.loop_t += dt;
                for &n in notes {
                    let hit = (prev == 0.0 && n == 0.0) || (n > prev && n <= c.loop_t);
                    if hit && c.burst > 0 && c.rounds > 0 {
                        c.burst -= 1;
                        // Jonesy: each molmer_fire_* anim has "fire" at 0 and "fire_alt" at 0.067; the four fire notes
                        // use up the burst of 4, then each fire_alt finds the burst empty and only wastes a round
                        // (0x0043d180, ai_05.cpp:1264): a clip of 24 rounds is 3 bursts of 4 shots, as the original's
                        // sound log and hook AI dump show (fightT)
                        c.rounds = c.rounds.saturating_sub(if c.q { 1 } else { 2 });
                        // aim error 0x004284e0 (as the grunts'): sideways in [-angle, angle], up in [0, height], scaled
                        // by the distance between fire_accuracy_min and max
                        let to = (at - muzzle).with_y(0.0);
                        let d = to.length();
                        let (lo, hi) = c.p.accuracy;
                        let sc = if d <= lo { 0.1 } else if d >= hi { 1.0 } else { ((d * d - lo * lo) / (hi * hi - lo * lo)).max(0.1) };
                        let side = Vec3::new(-to.z, 0.0, to.x).normalize_or_zero();
                        let off = side * rng.range(-c.p.angle, c.p.angle) * sc + Vec3::Y * rng.range(0.0, c.p.height) * sc;
                        let aim = (at + off - muzzle).normalize_or(Vec3::Z);
                        let bullet = data.props(&c.p.bullet);
                        crate::player::spawn_projectile(&mut commands, &mut fx, &bullet, muzzle, aim, true, Some(e));
                        if c.q {
                            // Q's muzzle fx runs for the burst (its machine-gun loop): one per burst
                            if c.muzzle.is_none() {
                                let m = commands.spawn((Transform::from_translation(muzzle), Visibility::default())).id();
                                c.muzzle = Some(m);
                                fx.write(crate::fx::StartFx { name: c.p.muzzle_fx.clone(), at: muzzle, rotation: Quat::IDENTITY, follow: Some(m), position_only: true });
                            }
                        } else if !c.p.muzzle_fx.is_empty() {
                            fx.write(crate::fx::StartFx { name: c.p.muzzle_fx.clone(), at: muzzle, rotation: Transform::IDENTITY.looking_to(aim, Vec3::Y).rotation, follow: None, position_only: false });
                        }
                    }
                }
                if c.loop_t >= loop_len {
                    c.loop_t = 0.0;
                    if c.burst == 0 || c.rounds == 0 {
                        c.burst = rng.range(c.p.bursts.0 as f32, c.p.bursts.1 as f32 + 0.999) as u32;
                        c.delay_t = rng.range(c.p.burst_delay.0, c.p.burst_delay.1);
                        if let Some(m) = c.muzzle.take() {
                            commands.entity(m).despawn();
                        }
                    }
                    if c.rounds == 0 {
                        // the reload anim (molmer_reload_br 3.47 s, q111_reload_br 1.97 s) and the clip delay
                        c.reloading = if c.q { 1.967 } else { 3.467 } + rng.range(c.p.clip_delay.0, c.p.clip_delay.1);
                    }
                }
            }
            if let (Some(m), true) = (c.muzzle, c.q) {
                commands.entity(m).insert(Transform::from_translation(muzzle));
            }
        } else {
            c.firing = false;
            c.loop_t = 0.0;
            if let Some(m) = c.muzzle.take() {
                commands.entity(m).despawn();
            }
        }
    }
}
