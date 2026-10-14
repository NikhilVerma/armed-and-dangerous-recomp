//! Grunt AI ported from the recovered state machine (GenericAI_Update 0x0043ee90 and its helpers, documented in
//! docs/subsystems/grunt-ai.md sections 3-5 and 11), on the game's own per-instance parameters. Reduced to the
//! states lev_1_m1 uses: idle/roaming (1), guard (0x79), wait for trigger (0x8c), chase (0x3c), firing (0xa) with the
//! fire cycle (clips x bursts x rounds, 180-degree cone + line of sight, aim error 0x004284e0), reload (0x32),
//! flee from a stuck sticky bomb (0xc8), limping, death and ragdoll, and the trenches as cover (the hiding-spot search
//! 0x0042a7b0 / 0x00429f00 and the barricade states 0xa0..0xa5: `find_hiding_spot`, `CoverPhase`). Hit-reaction
//! states, the generic cover slots, taunts, AI gunners on the mounted gun and the alarm controller are not ported yet
//! (see the doc).

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
    /// state 0x96: running to a marker (trigger_marker mode, alarm.rs)
    ToMarker,
    /// states 0xa0..0xa5: going to and using a trench's slot (`Grunt::cover`, its phase)
    Cover,
    Dead,
}

/// A trench (BarricadeObject, vtable 0x005a7068, behavior "barracade" = class 141, game_03.cpp:3067; lev_1_m1's
/// `trench` 37..41). Its type (vt+0x138 0x004563c0) is 3, so its load (ai_01.cpp:1019) gives it one ground slot with
/// limit 1: one grunt at a time. vt+0x14c 0x0040dcb0 gives its ref_exit, vt+0x150 0x0040dd10 its ref_barracade (point
/// = the node's translation, direction = -(its y axis)); vt+0x148 0x0040c100 (free: no occupant, count < limit),
/// vt+0x158 0x0040c400 (occupy), vt+0x15c 0x0040c4a0 (free the slot).
#[derive(Component)]
pub struct Barricade {
    /// the object's position (vt+0x54: the placement), ref_exit, ref_barracade and its facing (horizontal)
    pub origin: Vec3,
    pub exit: Vec3,
    pub spot: Vec3,
    pub dir: Vec3,
    /// the slot's occupant (+0x4b0[0]); no code path frees it when the occupant dies (static reading: Character_Die
    /// 0x004319e0 / 0x00427100 call neither vt+0x188 nor vt+0x18c), so a dead grunt keeps his trench
    pub occupant: Option<Entity>,
    /// its collision mesh: the cover's own line-of-sight test passes through it (0x00426fd0's `exclude`)
    pub collider: Option<BodyId>,
}

/// The barricade states (GenericAI_Update 0x0043ee90, ai_06.cpp:2055-2218; anim ids by GruntObject 0x0043bd70).
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum CoverPhase {
    /// 0xa0: running to ref_exit (the path of 0x0046f6d0)
    ToExit,
    /// 0xa1: from there to ref_barracade; on arrival +0x484 |= 0x20000, anim 0x66
    ToSpot,
    /// 0xa2: grunt_barricade_reload (0x66, 1.333 s) facing the slot's direction; at its end the fire counters are
    /// rolled (0x00427600) and 0xa3 follows
    Duck,
    /// 0xa3: grunt_barricade_breathe (0x65, looped) for 1..6 s (0 when the target is outside 80 degrees of the slot's
    /// direction); then 90 % stand up (0xa4), else peek (0xa5)
    Breathe,
    /// 0xa5: grunt_barricade_peek (0x67, 2.633 s, no fire notes), then back to 0xa3 (counters left) or 0xa2
    Peek,
    /// 0xa4: the stand set (ids 2, 3, 0, 1: grunt_stand_*, 1.967 s) turning to the target; at its end engage
    /// (0x00427910): in range the fire state 0xa (keeping the trench), else 0xa2; no target 0xa2
    Stand,
}

/// A grunt's trench (+0x904 with its slot +0x914 = 0, ref_barracade +0x934, the slot direction +0x908).
#[derive(Clone, Copy, Debug)]
pub struct Cover {
    pub trench: Entity,
    pub phase: CoverPhase,
    pub timer: f32,
    pub exit: Vec3,
    pub spot: Vec3,
    pub dir: Vec3,
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
    /// trigger_marker mode (ai_01.cpp:2498): marker_tag_min..max, the marker run (marker index, goal) and done
    pub marker_tags: Option<(i64, i64)>,
    pub marker: Option<(usize, Vec3)>,
    pub marker_done: bool,
    /// a barracks troop walking out to the exit slot's point (state 0x5a, set by 0x0042db50); then his marker run
    /// (marker tags set: 0x0042d1b0) or 20..50 ahead within 45 degrees (state 0x5b) and idle
    pub exit: Option<Vec3>,
    /// a route to the goal over the ground when the straight line is blocked (the original walks the nav grid,
    /// 0x0046f6d0; here nav.rs A*, re-planned every second)
    route: Vec<Vec3>,
    replan: f32,
    /// his own entity: his bullets' owner (the damage record's source +0x2c)
    pub me: Option<Entity>,
    /// score_value: to the Kills money when he dies (Game+0x2724)
    pub score: f32,
    /// the source of the last damage taken (the hit message's +0x2c), offered as a target (0x0042c820)
    pub attacker: Option<Entity>,
    /// game time a target was last adopted by an offer (+0x514)
    pub offer_t: f32,
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
    /// a guard's first fire cycle after a turn toward its target is dry (no shot: the counters are empty), then the
    /// reload: measured in the original
    /// (hook AI dump, second encounter: the guards play fire (anim 0x23) at 740..760, reload (0x2a) by 780..820, and
    /// their first shots come at 800..837; the sound log has none in between), as fire-timing note #7 predicted
    pub dry_first: bool,
    /// a turn toward the target before firing (0x0042e050: target more than ~45 degrees off: grunt_turn_180 0.6 s
    /// behind (dot < -0.7), grunt_turn_left / right_90 0.5 s, the archers' grunt_bow_turn 0.633 s): the anim, time left
    pub turn: Option<(&'static str, f32)>,
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
    /// the death anim Character_Die picked instead of grunt_die_N (window, ledge)
    pub death_name: Option<&'static str>,
    /// ledge_death (+0x484 bit 31): falls off his ledge when he dies
    pub ledge_death: bool,
    /// a window or tower grunt (+0x488 & 0x200080: modes *_window / *_tower)
    pub window: bool,
    /// the last damage record had flag 0x80 (allow_cartoon_death: the sticky bomb's)
    pub cartoon: bool,
    /// chance in % of a pickup on death (pickup_drop_percent, default 100)
    pub drop_percent: f32,
    /// the trench held (+0x904) and its phase (states 0xa0..0xa5)
    pub cover: Option<Cover>,
    /// +0x484 0x20000: in the trench's slot (set on arrival, 0xa1; cleared on leaving, 0x004273a0 / 0x004274e0): the
    /// target search sees all round (AI_FindTarget ai_02.cpp:771), the clip's end and a lost line of sight duck
    /// (0xa2) instead of reloading or pursuing, and the death anim is 0x56 grunt_barricade_death (0x00427100)
    pub at_barricade: bool,
    /// +0x918 / +0x91c / +0x920: the covers left last (0x004273a0); the search skips the first two
    pub cover_history: [Option<Entity>; 3],
    /// debug (playtest `cover` step): run the hiding-spot search now, as at a reload's end (flag 4 set)
    pub force_hide: bool,
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
    pub states: [usize; 10],
    /// grunts holding a target: the player, a companion (playtest log, for comparisons with the hook's AI dump)
    pub targets: [usize; 2],
}

/// Sticky bomb stuck at a point (game_07.cpp:1240: AI within 25 units are scared).
#[derive(Message, Clone, Copy)]
pub struct StickyStuck(pub Vec3, pub Option<Entity>);

pub struct AiPlugin;

/// The kill meter and the "dismay" banter (MissionObject_Update, game_02.cpp:2473-2494): every bad guy killed adds 1
/// (Game+0xbb8, ai_04.cpp:594), the meter drains 0.5 / s (+0xbb4); when the cooldown (+0xbc8) has run out and the
/// meter is at 3 (+0xbbc) or more, one line of table 0x005ce570 plays (Roman, Jonesy, Q: "Ha ha, did you see that?!"
/// ...; 0x00493b70), the meter is cleared and the cooldown restarts at 30..45 s (+0xbc0 / +0xbc4).
#[derive(Resource, Default)]
pub struct KillMeter {
    pub meter: f32,
    pub cooldown: f32,
}

const DISMAY: [&str; 16] = [
    "rom50039_dismay", "rom50040_dismay", "rom50062_dismay", "rom50174_dismay", "rom50202_dismay", "rom50210_dismay",
    "rom50045_ambient", "jon50023_dismay", "jon50048_dismay", "jon50049_dismay", "jon50117_dismay", "jon50118_dismay",
    "jon50119_dismay", "jon50174_dismay", "q1150168_dismay", "q1150115_dismay",
];

fn dismay(
    time: Res<Time>,
    mut km: ResMut<KillMeter>,
    mut rng: ResMut<FxStats>,
    companions: Query<(&Companion, &Transform, Option<&Health>)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let dt = time.delta_secs();
    km.cooldown -= dt;
    if km.cooldown < 0.0 && km.meter >= 3.0 {
        km.cooldown = rng.range(30.0, 45.0);
        km.meter = 0.0;
        // a line whose speaker is there (Roman always; Jonesy / Q while alive): guess at 0x00493b70's choice
        let alive = |q: bool| companions.iter().find(|(c, _, h)| c.q == q && h.is_none_or(|h| h.0 > 0.0)).map(|(_, t, _)| t.translation);
        let pool: Vec<(&str, Option<Vec3>, bool)> = DISMAY
            .iter()
            .filter_map(|n| match &n[..3] {
                "rom" => Some((*n, None, true)),
                "jon" => alive(false).map(|p| (*n, Some(p), true)),
                _ => alive(true).map(|p| (*n, Some(p), true)),
            })
            .collect();
        if !pool.is_empty() {
            let (name, at, _) = pool[((rng.rand() * pool.len() as f32) as usize).min(pool.len() - 1)];
            sounds.write(match at {
                Some(p) => crate::audio::PlaySound::at(name, p),
                None => crate::audio::PlaySound::ui(name),
            });
        }
    }
    km.meter = (km.meter - dt * 0.5).max(0.0);
}

impl Plugin for AiPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(AiStats { enabled: true, ..default() })
            .init_resource::<KillMeter>()
            .init_resource::<Barks>()
            .add_message::<StickyStuck>()
            .add_systems(Update, (search, scare, think, die, dismay, log_cover).chain().after(crate::fx::FxSet));
    }
}

/// The playtest state's `cover`: trenches held, grunts in a slot, the first held cover's phase, deaths in a trench.
fn log_cover(grunts: Query<&Grunt>, trenches: Query<(&Barricade, &crate::level::InstanceId)>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let held = trenches.iter().filter(|(b, _)| b.occupant.is_some()).count();
    // each trench by instance id: held or not ("cover.slots.37")
    let slots: serde_json::Map<String, serde_json::Value> = trenches.iter().map(|(b, i)| (i.0.to_string(), b.occupant.is_some().into())).collect();
    let live = grunts.iter().filter(|g| g.mind != Mind::Dead);
    let in_slot = live.clone().filter(|g| g.at_barricade).count();
    let going = live.clone().filter(|g| g.cover.is_some() && !g.at_barricade).count();
    let phase = live.clone().find_map(|g| g.cover.map(|c| format!("{:?}", c.phase))).unwrap_or_default();
    let firing = live.filter(|g| g.at_barricade && g.mind == Mind::Fire).count();
    let deaths = grunts.iter().filter(|g| g.mind == Mind::Dead && g.death_name == Some("grunt_barricade_death")).count();
    state.0.insert("cover".into(), serde_json::json!({"held": held, "in_slot": in_slot, "going": going, "phase": phase, "firing": firing, "deaths": deaths, "slots": slots}));
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
        score: p.f("score_value", 0.0),
        me: None,
        marker_tags: (p.s("mode") == Some("trigger_marker")).then(|| (p.i("marker_tag_min", 0), p.i("marker_tag_max", 0))),
        marker: None,
        marker_done: false,
        exit: None,
        route: Vec::new(),
        replan: 0.0,
        attacker: None,
        offer_t: f32::NEG_INFINITY,
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
        dry_first: false,
        turn: None,
        last_hit: 0.0,
        hit_level_1: p.f("hit_level_1", 10.0),
        hit_level_2: p.f("hit_level_2", 25.0),
        hit: None,
        fixed: None,
        hurt_voice_timer: 0.0,
        dead_said: false,
        // GruntObject_Slot6 (ai_04.cpp:2340): < 100 GR1 (+0x484 bit 21), < 200 GR3 (+0x488 bit 29), else GR2
        voice_set: if pick < 100 { 0 } else if pick < 200 { 2 } else { 1 },
        ray_timer: 0.0,
        ragdoll: false,
        death_anim: (rng.rand() % 6) as u8 + 1,
        death_name: None,
        ledge_death: p.i("ledge_death", 0) != 0,
        window: mode.contains("window") || mode.contains("tower"),
        cartoon: false,
        drop_percent: p.f("pickup_drop_percent", 100.0),
        cover: None,
        at_barricade: false,
        cover_history: [None; 3],
        force_hide: false,
    }
}

/// A grunt bark of a category (Fight, Hurt, Dead, Taunt, Help, Sticky_Scream, ...): audio.rs picks a line of
/// that category and voice set from the bank (audio_01.cpp; index = ((rand*range)>>15)/100).
pub fn voice(category: &str, set: u8) -> String {
    // "Cat:{}:pool" puts the set before a pool selector (audio.rs resolve)
    if category.contains("{}") {
        return format!("@grunt:{}", category.replace("{}", &set.to_string()));
    }
    format!("@grunt:{category}:{set}")
}

/// The barks' shared timers (game_02.cpp:2057-2110), all started at level load and re-rolled when used:
/// death_delay Game+0xb64 30..45 s (hurt / dead / sticky), vo_delay Game+0xb44 30..45 s (spotted / fight),
/// taunt_delay Game+0xb84 300..360 s.
#[derive(Resource, Default)]
pub struct Barks {
    /// (last time, interval) each; an interval of 0 is rolled on first use
    death: (f32, f32),
    vo: (f32, f32),
    taunt: (f32, f32),
}

#[derive(Clone, Copy)]
pub enum Delay {
    Death,
    Vo,
    Taunt,
}

impl Barks {
    /// Whether the delay has passed (then it restarts with a new interval).
    pub fn take(&mut self, d: Delay, now: f32, rng: &mut FxStats) -> bool {
        let (t, range) = match d {
            Delay::Death => (&mut self.death, (30.0, 45.0)),
            Delay::Vo => (&mut self.vo, (30.0, 45.0)),
            Delay::Taunt => (&mut self.taunt, (300.0, 360.0)),
        };
        if t.1 == 0.0 {
            t.1 = rng.range(range.0, range.1);
        }
        if now - t.0 < t.1 {
            return false;
        }
        *t = (now, rng.range(range.0, range.1));
        true
    }
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
    mut barks: ResMut<Barks>,
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
            // at a barricade (+0x484 0x20000) the cone test is skipped (ai_02.cpp:771)
            let cone = to.normalize_or_zero().dot(fwd) >= 95f32.to_radians().cos() || d2 < g.hear_distance || g.at_barricade;
            if cone && sight_ray(&physics, own_hit, chit, &[gbody.id]) {
                best = Some((d2, ce, false));
            }
        }
        if let Some((d2, tgt, heard)) = best {
            // the Spotted cue: a new target when there was none (SetTarget 0x0042cc70 -> vt+0x144), under vo_delay
            // (30..45 s, from level load: the first fight has no line)
            if g.target.is_none() && barks.take(Delay::Vo, time.elapsed_secs(), &mut rng) {
                bark(&mut sounds, "Spotted", g.voice_set, me);
            }
            g.target = Some(tgt);
            if g.mind == Mind::WaitTrigger {
                g.mind = Mind::Roam; // activate 0x0042f5c0 (charge/marker flags: not ported)
            }
            if std::env::var("AIDBG").is_ok() { info!("AIDBG spotted {:?} by {:?} at {:?} d {:.0} heard {} mode {:?}", tgt, ge, me, d2, heard, g.mind); }
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
    mut grunts: Query<(Entity, &mut Grunt, &GlobalTransform)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut barks: ResMut<Barks>,
    time: Res<Time>,
) {
    let ppos = player.single().map(|p| p.translation()).unwrap_or_default();
    for s in stuck.read() {
        for (ge, mut g, t) in &mut grunts {
            if g.mind == Mind::Dead || t.translation().distance(s.0) >= 25.0 {
                continue;
            }
            // Scare 0x0042fb60: the grunt the bomb is stuck to says Sticky (death_delay; else General_Scream), the others
            // General_Scream, a guard without the bomb on him nothing (ai_03.cpp:2930-2943)
            if s.1 == Some(ge) {
                let cat = if barks.take(Delay::Death, time.elapsed_secs(), &mut rng) { "Sticky_Scream" } else { "General_Scream" };
                bark(&mut sounds, cat, g.voice_set, t.translation());
            } else if !g.guard {
                bark(&mut sounds, "General_Scream", g.voice_set, t.translation());
            }
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

/// 0x0042b860: `point` lies within `angle` degrees of `facing` seen from `from` (2D; both directions normalised, a
/// zero vector giving 90 degrees).
pub fn within_arc(point: Vec3, from: Vec3, facing: Vec3, angle: f32) -> bool {
    let a = (point - from).with_y(0.0).normalize_or_zero();
    let f = facing.with_y(0.0).normalize_or_zero();
    a.dot(f).clamp(-1.0, 1.0).acos().to_degrees() < angle
}

/// A trench as the frame's searches see it (occupancy written back after the grunts' loop).
struct TrenchView {
    e: Entity,
    origin: Vec3,
    exit: Vec3,
    spot: Vec3,
    dir: Vec3,
    occupant: Option<Entity>,
    collider: Option<BodyId>,
}

/// The barricade candidate of the hiding-spot search, 0x00429f00 (needs vt+0x1f4: true for grunts, 0x00433fa0): the
/// trench (vt+0x138 == 3) nearest the grunt (2D, from its origin) whose slot 0 is free (vt+0x148), that is not one of
/// the last two left (+0x918 / +0x91c), within scan_distance (+0x5dc) of the grunt; with a target also within the
/// engagement range (+0x5d8) of the target, the target within 80 degrees of ref_barracade's direction seen from
/// ref_barracade (0x0042b860), and a clear line from ref_barracade + 5 (and + 5 again in 0x00426fd0) to the target's
/// ref_hit_00, the trench itself passed through. No target with need_target (every caller here): none.
fn pick_trench(g: &Grunt, me: Entity, pos: Vec3, target: Option<(Vec3, Vec3)>, trenches: &[TrenchView], physics: &Physics, own: BodyId) -> Option<usize> {
    let (tpos, thit) = target?;
    let mut best: Option<(usize, f32)> = None;
    for (i, t) in trenches.iter().enumerate() {
        if t.occupant.is_some_and(|o| o != me) || g.cover_history[..2].contains(&Some(t.e)) {
            continue;
        }
        let d = (t.origin - pos).with_y(0.0).length_squared();
        if d > g.scan_distance * g.scan_distance || best.is_some_and(|b| !(b.1 > d)) {
            continue;
        }
        if (t.origin - tpos).with_y(0.0).length_squared() > g.range * g.range {
            continue;
        }
        if !within_arc(tpos, t.spot, t.dir, 80.0) {
            continue;
        }
        let skip: Vec<BodyId> = t.collider.into_iter().chain([own]).collect();
        if !sight_ray(physics, t.spot + Vec3::Y * 5.0, thit, &skip) {
            continue;
        }
        best = Some((i, d));
    }
    best.map(|b| b.0)
}

/// What the hiding-spot search chose.
enum Hide {
    /// a trench taken (state 0xa0 next)
    Cover,
    /// the reload in place (anims 0x2a/0x2b/0x28/0x29, state 0x32)
    Reload,
    /// pursue (vt+0x184 0x004295d0)
    Pursue,
}

/// Leaves a held trench: vt+0x188 0x004273a0 (history: +0x918 = the trench, the older two cleared, as without flag
/// 0x10, which only the generic cover slots set) or vt+0x18c 0x004274e0 (history cleared); the slot is freed (vt+0x15c)
/// and +0x484 0x20000 cleared.
fn leave_cover(g: &mut Grunt, me: Entity, trenches: &mut [TrenchView], history: bool) {
    if let Some(c) = g.cover.take() {
        if let Some(t) = trenches.iter_mut().find(|t| t.e == c.trench && t.occupant == Some(me)) {
            t.occupant = None;
        }
        g.cover_history = if history { [Some(c.trench), None, None] } else { [None; 3] };
    } else if !history {
        g.cover_history = [None; 3];
    }
    g.at_barricade = false;
}

/// GenericAI_FindHidingSpot 0x0042a7b0 (vt+0x240) as lev_1_m1's grunts reach it, with need_target (a0 = 1: at the clip's
/// end through 0x0042b8e0, and at a reload's end, state 0x32):
/// - a clip's end (+0x484 flag 4 clear) with a target: 90 % (0x2d0000) the reload in place, the search skipped
///   (0x004531e0(target), true for an airborne target, taken false: guess);
/// - pickup_grab_pct (+0x86c, default 1) % 0x0042a500, a grab of a pickup: not ported;
/// - a held cover is left (vt+0x188); no target: nothing found;
/// - the candidates: the nearest free mounted gun (0x0042a100: not ported here, see docs/subsystems/turret.md) and the
///   nearest trench (0x00429f00, `pick_trench`); a trench nearer than the gun wins; then the generic cover slots of the
///   list World+0xed4 (kinds 1 ground / 6 enter of objects other than type 3), which lev_1_m1 has none of (its
///   barracks have only ref_exit / ref_spawn nodes; the trenches are type 3, skipped there);
/// - a trench: the path to its ref_exit must exist (0x0046f6d0 == 1: a straight line or a nav route here), then the slot
///   is taken (vt+0x158) and state 0xa0 starts;
/// - nothing: flag 4 set -> pursue (vt+0x184), else the reload in place (state 0x32).
#[allow(clippy::too_many_arguments)]
fn find_hiding_spot(g: &mut Grunt, me: Entity, flag4: bool, pos: Vec3, target: Option<(Vec3, Vec3)>, trenches: &mut [TrenchView], physics: &Physics, own: BodyId, rng: &mut FxStats) -> Hide {
    if !flag4 && target.is_some() && rng.rand() < 0.9 {
        return Hide::Reload;
    }
    leave_cover(g, me, trenches, true);
    let fail = if flag4 { Hide::Pursue } else { Hide::Reload };
    let Some(i) = pick_trench(g, me, pos, target, trenches, physics, own) else { return fail };
    let exit = trenches[i].exit;
    if !line_of_sight(physics, pos, exit) && crate::nav::path(physics, pos, exit, &[]).is_none() {
        return fail;
    }
    trenches[i].occupant = Some(me);
    let t = &trenches[i];
    g.cover = Some(Cover { trench: t.e, phase: CoverPhase::ToExit, timer: 0.0, exit: t.exit, spot: t.spot, dir: t.dir });
    g.route.clear();
    Hide::Cover
}

/// The barricade anims' lengths (lev_1_m1_big03_anim): grunt_barricade_reload, _peek, the stand set.
const DUCK_TIME: f32 = 1.333;
const PEEK_TIME: f32 = 2.633;
const STAND_TIME: f32 = 1.967;

/// Duck at the trench: anim 0x66 and state 0xa2 (0x00427910 out of range, 0x00428980 line of sight lost, 0x0042b8e0
/// target within the slot's 80 degrees).
fn duck(g: &mut Grunt) {
    if let Some(c) = g.cover.as_mut() {
        c.phase = CoverPhase::Duck;
        c.timer = DUCK_TIME;
        g.mind = Mind::Cover;
        g.turn = None;
    }
}

/// What ended the fire cycle's frame.
#[derive(PartialEq)]
enum FireEnd {
    None,
    /// the last burst of the clip (0x00428980 -> 0x0042b8e0)
    Clip,
    /// no line of sight to the target (los_lost)
    Sight,
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
    mut grunts: Query<(Entity, &mut Grunt, &Body, &mut Transform, &Health, Option<&crate::skin::Animator>)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    rtime: Res<Time<Real>>,
    mut barks: ResMut<Barks>,
    (markers, mut arrive, mut trenches): (Res<crate::alarm::Markers>, MessageWriter<crate::alarm::MarkerArrival>, Query<(Entity, &mut Barricade)>),
) {
    let t0 = Instant::now();
    let dt = time.delta_secs();
    let k = physics.meters_per_unit;
    let (mut alive, mut dead) = (0, 0);
    let mut states = [0usize; 10];
    let mut views: Vec<TrenchView> = trenches
        .iter()
        .map(|(e, b)| TrenchView { e, origin: b.origin, exit: b.exit, spot: b.spot, dir: b.dir, occupant: b.occupant, collider: b.collider })
        .collect();
    for (ge, mut g, body, mut tr, h, ga) in &mut grunts {
        g.me = Some(ge);
        if g.mind == Mind::Dead {
            dead += 1;
            continue;
        }
        alive += 1;
        states[g.mind as usize] += 1;
        // a trench held by a grunt another system took elsewhere (a scare, a throw): let it go (guess: as vt+0x18c)
        if g.cover.is_some() && !matches!(g.mind, Mind::Cover | Mind::Fire | Mind::Reload) {
            leave_cover(&mut g, ge, &mut views, false);
        }
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
            // Hurt (0x00439e00, shared pool, death_delay) first; when refused HurtShort (0x00439e30) at most every 4 s
            // per grunt; the shared one restarts the 4 s too (+0xb48)
            if barks.take(Delay::Death, time.elapsed_secs(), &mut rng) {
                g.hurt_voice_timer = 4.0;
                bark(&mut sounds, "Hurt:{}:last4", g.voice_set, tr.translation);
            } else if g.hurt_voice_timer <= 0.0 {
                g.hurt_voice_timer = 4.0;
                bark(&mut sounds, "Hurt:{}:butlast4", g.voice_set, tr.translation);
            }
            if g.mind == Mind::WaitTrigger {
                g.mind = Mind::Roam;
            }
            // a hit reaction lets a trench go unless the grunt is in its slot (0x00430340's stopMove: vt+0x18c without
            // +0x484 0x20000); in the slot the reaction's end ducks again (state 0x132 -> anim 0x66, 0xa2). Let go:
            // pursue (guess: the reaction's end with flag 4 set, vt+0x184)
            if g.cover.is_some() {
                if g.at_barricade {
                    duck(&mut g);
                } else {
                    leave_cover(&mut g, ge, &mut views, false);
                    g.mind = Mind::Chase;
                }
            }
            // retaliation: the end of GenericAIObject_Slot52 0x00430340 offers the damage source (when it is one of
            // the grunt's enemies, list +0x604: Roman or a companion) as a target, GenericAI_OfferTarget 0x0042c820:
            // ignored while a target adopted this way is less than 10 s old (K 0x005bcb68 against +0x514); taken when
            // there is no target or the source is nearer (3D) than the current one
            if let Some(a) = g.attacker.take() {
                let now = time.elapsed_secs();
                let ap = targets.get(a).ok().map(|t| t.translation());
                let cur = g.target.and_then(|e| targets.get(e).ok()).map(|t| t.translation());
                // (ignored too on the way to a cover: +0x904 held without +0x484 0x20000, 0x0042c820)
                let recent = (g.target.is_some() && now - g.offer_t < 10.0) || (g.cover.is_some() && !g.at_barricade);
                if let (Some(ap), false) = (ap, recent) {
                    let me = tr.translation;
                    if cur.is_none_or(|c| (ap - me).length_squared() <= (c - me).length_squared()) {
                        if std::env::var("AIDBG").is_ok() {
                            info!("AIDBG retaliate {:?} -> {:?} (had {:?})", body.id, a, g.target);
                        }
                        g.target = Some(a);
                        g.offer_t = now;
                    }
                }
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
        // a barracks troop first walks out of the door (state 0x5a 0x00442407: following its path, nothing else)
        if let Some(x) = g.exit {
            if flat(x - pos).length() < 4.0 {
                g.exit = None;
                if g.marker_tags.is_none() {
                    // no marker tag: a point 20..50 ahead, within +-45 degrees of its heading (state 0x5b), then idle
                    let a = g.yaw + rng.range(-std::f32::consts::FRAC_PI_4, std::f32::consts::FRAC_PI_4);
                    let d = rng.range(20.0, 50.0);
                    g.goal = pos + Vec3::new(a.sin(), 0.0, a.cos()) * d;
                    g.timer = d / run_speed.max(1.0);
                    g.mind = Mind::Flee;
                }
            } else {
                g.mind = Mind::ToMarker;
            }
        }
        // a trigger_marker grunt, once awake (its trigger, a target found, or hurt), runs to his marker first
        if g.exit.is_none() && g.marker.is_none() && !g.marker_done && g.mind != Mind::WaitTrigger {
            if let Some(i) = g.marker_tags.and_then(|tags| markers.find(tags, rng.rand())) {
                g.marker = Some((i, markers.markers[i].pos));
                g.mind = Mind::ToMarker;
            } else {
                g.marker_done = true;
            }
        }
        if let Some((i, goal_m)) = g.marker {
            if flat(goal_m - pos).length() < 8.0 {
                // Marker_ProcessArrival 0x0044ba30: the alarm (alarm.rs), then the action
                g.marker = None;
                g.marker_done = true;
                arrive.write(crate::alarm::MarkerArrival { marker: i, at: pos, voice_set: g.voice_set });
                let mk = &markers.markers[i];
                let (lo, hi) = match mk.action.as_str() {
                    "hide" => (60.0, 100.0),
                    "trigger_charge" => (0.0, 0.0),
                    _ => mk.scatter,
                };
                if hi > 0.0 {
                    let a = rng.range(0.0, std::f32::consts::TAU);
                    g.goal = pos + Vec3::new(a.cos(), 0.0, a.sin()) * rng.range(lo, hi);
                    g.timer = 3.0;
                    g.mind = Mind::Flee;
                } else {
                    g.mind = Mind::Chase;
                }
                info!("EVENT ai: grunt reached marker {} ({})", mk.id, mk.action);
            } else {
                g.mind = Mind::ToMarker;
            }
        }
        // the target's ref_hit_00 (0x00428280) and the grunt's own: the sight rays' ends
        let target_hit = target.map(|p| {
            g.target
                .and_then(|e| anims.get(e).ok())
                .and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| targets.get(a.joints[i]).ok()))
                .map(|j| j.translation())
                .unwrap_or(p + Vec3::Y * 7.0)
        });
        let own_hit = ga.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| targets.get(a.joints[i]).ok())).map(|j| j.translation()).unwrap_or(pos + Vec3::Y * 6.0);
        let tgt = target.zip(target_hit);
        // debug: the search as at a reload's end (playtest `cover` step)
        if g.force_hide {
            g.force_hide = false;
            g.mind = match find_hiding_spot(&mut g, ge, true, pos, tgt, &mut views, &physics, body.id, &mut rng) {
                Hide::Cover => Mind::Cover,
                Hide::Pursue => Mind::Chase,
                Hide::Reload => Mind::Reload,
            };
            info!("EVENT ai: cover search -> {:?}", g.mind);
        }
        g.mind = match (g.mind, target) {
            (Mind::ToMarker, _) => Mind::ToMarker,
            (Mind::Flee, _) if g.timer > 0.0 => Mind::Flee,
            (Mind::Flee, _) => {
                g.target = g.target.or(None);
                Mind::Chase
            }
            // the trench's states run their own course (below)
            (Mind::Cover, _) if g.cover.is_some() => Mind::Cover,
            // in the slot the fire state 0xa keeps firing while the target is held (its exits: the clip's end, a lost
            // line of sight); no target: duck (guess: 0x0042bc60's path is not traced for a barricade grunt)
            (Mind::Fire, Some(_)) if g.at_barricade => Mind::Fire,
            (Mind::Fire, None) if g.at_barricade => {
                duck(&mut g);
                Mind::Cover
            }
            (Mind::Reload, _) if g.pause > 0.0 => Mind::Reload,
            // the reload's end (state 0x32, ai_06.cpp:1223): in the slot engage (0x00427910: in range fire, else duck);
            // otherwise in range (2D, +0x5d8) with a line of sight fire again (vt+0x230), else leave any cover and 50 %
            // search for a hiding spot (vt+0x240(1, 0, 0) with flag 4 set: nothing found -> pursue), 50 % pursue
            (Mind::Reload, Some(_)) if g.at_barricade => {
                if in_range {
                    Mind::Fire
                } else {
                    duck(&mut g);
                    Mind::Cover
                }
            }
            (Mind::Reload, Some(_)) if !g.guard => {
                let (_, thit) = tgt.unwrap_or((pos, pos));
                if in_range && sight_ray(&physics, own_hit, thit, &[body.id]) {
                    Mind::Fire
                } else if rng.rand() < 0.5 {
                    match find_hiding_spot(&mut g, ge, true, pos, tgt, &mut views, &physics, body.id, &mut rng) {
                        Hide::Cover => Mind::Cover,
                        _ => Mind::Chase,
                    }
                } else {
                    leave_cover(&mut g, ge, &mut views, true);
                    Mind::Chase
                }
            }
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
            Mind::ToMarker => {
                goal = g.exit.or(g.marker.map(|m| m.1)).unwrap_or(pos);
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
                    // the turn toward the target first (no fire while it plays; the yaw keeps turning below)
                    let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
                    let to = flat(p - pos).normalize_or_zero();
                    if let Some((_, left)) = g.turn.as_mut() {
                        *left -= dt;
                        if *left <= 0.0 {
                            g.turn = None;
                        }
                    } else if to.dot(fwd) < 0.7 {
                        let right = fwd.cross(to).y < 0.0;
                        // after a turn a guard's first fire cycle is dry (measured: the original's courtyard guards,
                        // most of which turn, fire anim then reload and first shots ~80 frames later, two that faced
                        // the player shooting at once; the first fight's archer, facing, shoots at once). Guess at the
                        // path: engage -> state 0xa -> back to the guard cycle with empty counters (note #7)
                        g.dry_first = g.guard;
                        g.turn = Some(if g.bow {
                            ("grunt_bow_turn", 0.633)
                        } else if to.dot(fwd) < -0.7 {
                            ("grunt_turn_180", 0.6)
                        } else if right {
                            ("grunt_turn_right_90", 0.5)
                        } else {
                            ("grunt_turn_left_90", 0.5)
                        });
                    }
                    let before = stats.shots;
                    let mut end = FireEnd::None;
                    if g.turn.is_none() {
                        // the weapon in its right hand (ref_rhand, grunt init 0x00434fb0): where the shot leaves
                        let hand = ga.and_then(|a| a.skeleton.index.get("ref_rhand").and_then(|&i| targets.get(a.joints[i]).ok())).map(|j| j.translation());
                        end = fire_cycle(&mut commands, &mut g, &physics, &mut rng, pos, p, hit, own_hit, hand, body.id, dt, &mut stats, &mut sounds, &mut fx, (&mut barks, time.elapsed_secs()));
                    }
                    if stats.shots > before && std::env::var("AIDBG").is_ok() {
                        info!("AIDBG gruntshot t {:.3}", rtime.elapsed_secs());
                    }
                    match end {
                        // in the slot a lost line of sight ducks (0x00428980: anim 0x66, state 0xa2)
                        FireEnd::Sight if g.at_barricade => duck(&mut g),
                        // the clip's end (0x0042b8e0): in the slot, the target within 80 degrees of its direction ->
                        // duck, else leave it (vt+0x188) and search again (vt+0x240(1, 1, 0)); elsewhere a non-guard
                        // searches (vt+0x240(1, 0, 0)): 90 % the reload in place first
                        FireEnd::Clip if g.at_barricade => {
                            let c = g.cover.expect("at a barricade without a cover");
                            if within_arc(p, c.spot, c.dir, 80.0) {
                                duck(&mut g);
                            } else {
                                leave_cover(&mut g, ge, &mut views, true);
                                if let Hide::Cover = find_hiding_spot(&mut g, ge, false, pos, tgt, &mut views, &physics, body.id, &mut rng) {
                                    g.mind = Mind::Cover;
                                }
                            }
                        }
                        FireEnd::Clip if !g.guard => {
                            if let Hide::Cover = find_hiding_spot(&mut g, ge, false, pos, tgt, &mut views, &physics, body.id, &mut rng) {
                                g.mind = Mind::Cover;
                            }
                        }
                        _ => {}
                    }
                    if g.mind == Mind::Cover {
                        info!("EVENT ai: grunt {ge:?} cover {:?}", g.cover.map(|c| c.phase));
                    }
                }
            }
            Mind::Cover => {
                // the barricade states 0xa0..0xa5 (ai_06.cpp:2055-2218)
                let mut c = g.cover.expect("cover state without a cover");
                c.timer -= dt;
                match c.phase {
                    CoverPhase::ToExit => {
                        goal = c.exit;
                        speed = run_speed;
                        if flat(c.exit - pos).length() < 3.0 {
                            c.phase = CoverPhase::ToSpot;
                        }
                    }
                    CoverPhase::ToSpot => {
                        goal = c.spot;
                        speed = run_speed;
                        if flat(c.spot - pos).length() < 1.5 {
                            // in the slot: +0x484 0x20000, anim 0x66, state 0xa2 (with no target one is searched for,
                            // AI_FindTarget: the search system's next tick, all round now)
                            g.at_barricade = true;
                            c.phase = CoverPhase::Duck;
                            c.timer = DUCK_TIME;
                            info!("EVENT ai: grunt {ge:?} in trench at {:.0} {:.0}", c.spot.x, -c.spot.z);
                        }
                    }
                    CoverPhase::Duck => {
                        if c.timer <= 0.0 {
                            // the fire counters rolled afresh (0x00427600), 1..6 s of breathing (0xa3)
                            g.left_clips = 0;
                            c.phase = CoverPhase::Breathe;
                            c.timer = rng.range(1.0, 6.0);
                        }
                    }
                    CoverPhase::Breathe => {
                        // the target outside the slot's 80 degrees (or none): the timer is cut to 0
                        if target.is_none_or(|p| !within_arc(p, c.spot, c.dir, 80.0)) {
                            c.timer = 0.0;
                        }
                        if c.timer <= 0.0 {
                            // rand(1000) >= 100: stand up (0xa4); else peek (0xa5: the fire-spray branch needs vt+0x1fc,
                            // false for grunts 0x00435250)
                            if rng.rand() >= 0.1 {
                                c.phase = CoverPhase::Stand;
                                c.timer = STAND_TIME;
                            } else {
                                c.phase = CoverPhase::Peek;
                                c.timer = PEEK_TIME;
                            }
                        }
                    }
                    CoverPhase::Peek => {
                        // its end: rounds left (+0x5bc > 0 after the fresh roll) -> breathe again
                        if c.timer <= 0.0 {
                            c.phase = CoverPhase::Breathe;
                            c.timer = rng.range(1.0, 6.0);
                        }
                    }
                    CoverPhase::Stand => {
                        if c.timer <= 0.0 {
                            // engage (0x00427910): in range the fire state 0xa, keeping the trench; else (or no
                            // target, L_4459e4) duck
                            if target.is_some() && in_range {
                                g.mind = Mind::Fire;
                                g.pause = 0.0;
                            } else {
                                c.phase = CoverPhase::Duck;
                                c.timer = DUCK_TIME;
                            }
                        }
                    }
                }
                if g.cover.is_some() {
                    g.cover = Some(c);
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
        let mut goal = goal;
        g.replan -= dt;
        if speed > 0.0 && flat(goal - pos).length() > 15.0 && !line_of_sight(&physics, pos, goal) {
            if g.replan <= 0.0 || g.route.is_empty() {
                g.replan = 1.0;
                g.route = crate::nav::path(&physics, pos, goal, &[]).unwrap_or_default();
            }
            while g.route.first().is_some_and(|w| flat(*w - pos).length() < 4.0) {
                g.route.remove(0);
            }
            if let Some(w) = g.route.first() {
                goal = *w;
            }
        } else {
            g.route.clear();
        }
        let dir = flat(goal - pos).normalize_or_zero();
        let face = match (g.mind, target, g.cover.map(|c| c.phase)) {
            (Mind::Fire, Some(p), _) | (Mind::Chase, Some(p), _) => flat(p - pos).normalize_or_zero(),
            // in the slot: its direction +0x908 (0x00454e60), standing up: the target (0x0042df70)
            (Mind::Cover, Some(p), Some(CoverPhase::Stand)) => flat(p - pos).normalize_or_zero(),
            (Mind::Cover, _, Some(CoverPhase::Duck | CoverPhase::Breathe | CoverPhase::Peek | CoverPhase::Stand)) => {
                g.cover.map(|c| flat(c.dir).normalize_or_zero()).unwrap_or(dir)
            }
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
    for v in &views {
        if let Ok((_, mut b)) = trenches.get_mut(v.e) {
            if b.occupant != v.occupant {
                b.occupant = v.occupant;
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
    hand: Option<Vec3>,
    own: BodyId,
    dt: f32,
    stats: &mut AiStats,
    sounds: &mut MessageWriter<crate::audio::PlaySound>,
    fx: &mut MessageWriter<crate::fx::StartFx>,
    (barks, now): (&mut Barks, f32),
) -> FireEnd {
    g.pause -= dt;
    if g.pause > 0.0 {
        return FireEnd::None;
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
    if to.normalize_or_zero().dot(fwd) < 0.0 {
        return FireEnd::None;
    }
    if !sight_ray(physics, own_hit, hit, &[own]) {
        // no shot this frame (a guard: state 0x7a and back, the target dropped at the next search tick; in a trench:
        // duck, the caller)
        return FireEnd::Sight;
    }
    // aim error 0x004284e0: sideways L in [-angle, angle], up V in [0, height], scaled by s from the accuracy ranges
    let d = to.length();
    let (lo, hi) = if g.infinite { g.infinite_accuracy } else { g.accuracy };
    let s = if d <= lo { 0.1 } else if d >= hi { 1.0 } else { ((d * d - lo * lo) / (hi * hi - lo * lo)).max(0.1) };
    let side = Vec3::new(-to.z, 0.0, to.x).normalize_or_zero();
    let aim = hit + side * rng.range(-g.inaccuracy_angle, g.inaccuracy_angle) * s + Vec3::Y * rng.range(0.0, g.inaccuracy_height) * s;
    // the shot leaves the weapon's emitter (0x004a2010): measured in the original (hook log_hits' and-shots.txt,
    // second encounter, eight grunts facing their target: the muzzle minus the grunt's position) ~6.9 ahead, 1.3..1.5
    // to its right (the rifle at the right shoulder) and 8.4 up; the bullets then pass where the original's do (a
    // grunt standing in front of a shooter is hit)
    let _ = hand;
    let right = Vec3::new(-fwd.z, 0.0, fwd.x);
    let emitter = pos + fwd * 6.9 + right * 1.4 + Vec3::Y * 8.4;
    let muzzle = emitter - (aim - emitter).normalize_or(Vec3::Z) * 4.0;
    let dir = (aim - muzzle).normalize_or(Vec3::Z);
    if std::env::var("AIDBG").is_ok() {
        let o = crate::level::game_to_engine().inverse() * (muzzle + dir * 4.0 - pos);
        let tg = crate::level::game_to_engine().inverse() * (p - pos);
        let gp = crate::level::game_to_engine().inverse() * pos;
        info!("AIDBG muzzle {:.1} {:.1} off {:.2} {:.2} {:.2}", gp.x, gp.y, o.x, o.y, o.z);
    }
    if g.dry_first {
        // the dry first cycle: the fire anim without a shot, then the reload as after an empty clip
        g.dry_first = false;
        g.left_clips = 0;
        g.mind = Mind::Reload;
        g.pause = 0.467 + if g.bow { 1.3 } else { 1.133 } + 2.0 / 60.0;
        return FireEnd::None;
    }
    crate::player::spawn_projectile(commands, fx, &g.bullet, muzzle + dir * 4.0, dir, false, g.me);
    if let Some(m) = g.weapon.s("muzzle_fx_name") {
        fx.write(crate::fx::StartFx { name: m.to_owned(), at: muzzle + dir * 4.0, rotation: Transform::IDENTITY.looking_to(dir, Vec3::Y).rotation, follow: None, position_only: false });
    }
    // (the shot sound comes from the weapon's muzzle fx: the original's sound log has one per shot)
    // the Fight cue as the fire anim starts with a target (0x00428980 -> vt+0x13c), under vo_delay
    if barks.take(Delay::Vo, now, rng) {
        bark(sounds, "Fight", g.voice_set, pos);
    }
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
            // a Taunt after the last burst (ai_02.cpp:1956-1981) under taunt_delay (300..360 s, game-wide); the taunt
            // anims 0x32..0x34 and state 0x6e are not played (guess: the reload follows)
            // (not in a trench: +0x484 0x20000, ai_02.cpp:1959)
            if !g.at_barricade && barks.take(Delay::Taunt, now, rng) {
                bark(sounds, "Taunt", g.voice_set, pos);
            }
            g.left_clips -= 1;
            g.mind = Mind::Reload;
            let reload = if g.bow { 1.3 } else { 1.133 };
            g.pause = 0.467 + reload + if g.guard { 2.0 / 60.0 } else { rng.range(g.clip_delay.0, g.clip_delay.1) };
            return FireEnd::Clip;
        }
    }
    FireEnd::None
}

/// Character_Die: Dead bark once; ragdoll (a tumbling rigid body here) when the killing hit was at least
/// hit_ragdoll, GoRagdoll impulse = direction x amount x 0.15. Death anims (grunt_die_*) come with skinning.
fn die(
    mut commands: Commands,
    mut physics: NonSendMut<Physics>,
    mut grunts: Query<(Entity, &mut Grunt, &Body, &Transform, &Health, Option<&mut crate::skin::Animator>)>,
    joints: Query<&GlobalTransform>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut km: ResMut<KillMeter>,
    mut ms: ResMut<crate::stats::MissionStats>,
    mut rng: ResMut<FxStats>,
    alive: Query<&Health>,
    (mut barks, time): (ResMut<Barks>, Res<Time>),
) {
    let mut alerts: Vec<(i64, Entity, Entity)> = Vec::new();
    for (e, mut g, body, tr, h, animator) in &mut grunts {
        if g.mind == Mind::Dead || h.0 > 0.0 {
            continue;
        }
        g.mind = Mind::Dead;
        km.meter += 1.0;
        // his alert group hears of it (0x00431720): with a group id (+0x574) and a living killer, every other living
        // member takes the killer as a target after 0.25..1 s (0x0042c700 -> the +0x578 hand-over)
        if let Some(k) = g.attacker.take().filter(|&k| alive.get(k).is_ok_and(|h| h.0 > 0.0)) {
            if g.alert_group != 0 {
                alerts.push((g.alert_group, k, e));
            }
        }
        // an enemy killed, any cause (0x00431720), and his score_value
        ms.enemies_killed += 1;
        ms.killed(g.score);
        if !g.dead_said {
            g.dead_said = true;
            // Dead (0x00439e60, pool 26..28) under death_delay, else DeadAtPosition (0x00439ea0, pool 21..25): once
            let cat = if barks.take(Delay::Death, time.elapsed_secs(), &mut rng) { "Dead:{}:last3" } else { "Dead:{}:butlast3" };
            bark(&mut sounds, cat, g.voice_set, tr.translation);
        }
        let push = physics.world.body(body.id).map(|b| bv(b.linear_velocity())).unwrap_or(Vec3::ZERO) / physics.meters_per_unit;
        physics.remove(body.id);
        // the death (Character_Die 0x004319e0, in its order): a window / tower grunt killed by a record with flag
        // 0x80 (the sticky bomb) is blown out of the window (anim 0x3e); a hit of at least hit_ragdoll ragdolls; a
        // ledge_death grunt falls (0x39 + rand*4: grunt_hit_fall_1 / _2 / _1 / _2); a window / tower grunt
        // grunt_hit_window (0x3d); else 0x00427100: grunt_barricade_death (0x56) in a trench's slot, else grunt_die_1..6.
        // Not reached here: the 0x57 flag variant (in back: no lev_1_m1 grunt sets it) and record flag 2's launch.
        let blown_out = g.window && g.cartoon;
        g.ragdoll = !blown_out && (g.last_hit >= g.hit_ragdoll || push.length() > 30.0);
        g.death_name = if blown_out {
            Some("grunt_window_blown_out")
        } else if g.ragdoll {
            None
        } else if g.ledge_death {
            Some(if rng.rand() < 0.5 { "grunt_hit_fall_1" } else { "grunt_hit_fall_2" })
        } else if g.window {
            Some("grunt_hit_window")
        } else if g.at_barricade {
            // 0x00427100: +0x484 0x20000 -> anim 0x56 (GruntObject 0x0043bd70: grunt_barricade_death)
            Some("grunt_barricade_death")
        } else {
            None
        };
        if std::env::var("AIDBG").is_ok() {
            info!("AIDBG death {e:?}: {:?} ragdoll {} (hit {:.0}, cartoon {})", g.death_name, g.ragdoll, g.last_hit, g.cartoon);
        }
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
    for (group, killer, who) in alerts {
        for (ge, mut g, _, _, _, _) in &mut grunts {
            if ge != who && g.alert_group == group && g.mind != Mind::Dead && g.pending.is_none() {
                if std::env::var("AIDBG").is_ok() {
                    info!("AIDBG group {group} alerted by a death: {ge:?} -> {killer:?}");
                }
                g.pending = Some((killer, rng.range(0.25, 1.0)));
            }
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
/// - Team orders (fn team_orders): Defend Me / Return To Me / Defend Area / Attack, their lines and HUD texts; the
///   Fight cue at the fire anim's start (docs/subsystems/companions.md).
/// Not ported: the slot swap (leader flag 0x10, 0x00483810), the order marker fx (Game+0xd34), object types 0x51 /
/// 0x53 as attack targets.
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
    /// hits and death (GenericAIObject_Slot52 0x00430340 and Character_Die 0x004319e0, no companion overrides):
    /// health last frame, seconds since the last Hurt bark (+0xb48, 10 s), the reaction anim playing (name, time
    /// left), a player / teammate shot that hit (FriendlyFire bark, no damage), the attacker to offer as a target,
    /// dead (permanent: *_die, then the body is removed)
    pub last_health: f32,
    pub hurt_t: f32,
    pub hit: Option<(&'static str, f32)>,
    pub friendly: bool,
    pub attacker: Option<Entity>,
    pub dead: Option<f32>,
    /// the standing order (flags +0xbcc 1 / 4 / 8; docs/subsystems/companions.md)
    pub order: Order,
    /// formation_angle (+0xbc0, default 20): the spread of the formation points round an order's anchor (0x00483c30)
    angle: f32,
    /// +0xb68 after an attack order: seconds idle before the pair returns silently (3..7 s, 0x00484100)
    linger: f32,
}

/// The pair's standing order (+0xbcc flags; 0x004844c0, 0x00484290, 0x00485120).
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Order {
    /// flags 1 | 8, the start (0x004836b0): "Defend Me". Formation on Roman's ref bones; targets within scan_distance
    /// of Roman (0x004853a0); regroup when away from the slot
    Guard,
    /// no flag 1 (a second "return" toggles to it): "Return To Me". Generic targeting (0x0043ee90, no anchor limit);
    /// regroup only when Roman is farther than delay_distance (mode 1, 0x0048631c)
    Loose,
    /// flag 1 without 8: "Defend Area" at the aimed point, facing Roman's direction at the order (0x00484ca0)
    Area { at: Vec3, dir: Vec3 },
    /// flags 1 | 4: "Attack" the grunt under the crosshair, anchored where it stood (0x00484100)
    Attack { at: Vec3, dir: Vec3 },
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
        last_health: f32::NAN,
        hurt_t: 99.0,
        hit: None,
        friendly: false,
        attacker: None,
        dead: None,
        order: Order::Guard,
        angle: p.f("formation_angle", 20.0),
        linger: 0.0,
        }
    }
}

impl Companion {
    /// Roman was hit (0x0047afa0 -> 0x00486df0): unless engaged, the attacker becomes the target.
    pub fn roman_attacked(&mut self, by: Entity) {
        if self.dead.is_none() && !self.engaged {
            self.target = Some(by);
        }
    }
}

pub struct CompanionPlugin;

impl Plugin for CompanionPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (companion_damage, companions).chain().run_if(crate::playing));
    }
}

/// The companions' hits and death (no overrides of the generic handlers: GenericAIObject_Slot52 0x00430340,
/// Character_Die 0x004319e0; barks audio_02.cpp:197-267 (Q), audio_01.cpp:1545-1610 (Jonesy)):
/// - a player or teammate shot does no damage (same team) but draws the FriendlyFire line (shared delay);
/// - damage: the Damaged line under the shared delay Game+0xb44, else Hurt at most every 10 s (+0xb48); a reaction
///   anim by the damage (<= hit_level_1 20 *_hit_mgun, <= hit_level_2 51 *_hit_small, else *_hit_med); the attacker
///   offered as a target (Slot109 0x00485360: ignored while a live target is held);
/// - death (permanent): the Dead line, *_die, score_value -500 to Accidents, the body removed after the anim and the
///   corpse timer (0.5 s + 0.5 s fade: guess on the fade's look).
/// The shared delay is vo_delay Game+0xb44 (range Game+0xb48 / +0xb4c = 30..45 s from level load, game_02.cpp:2057),
/// the one the grunts' Spotted / Fight cues use (Barks, Delay::Vo).
#[allow(clippy::too_many_arguments)]
fn companion_damage(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<Physics>,
    mut q: Query<(Entity, &mut Companion, &Health, &Transform, &mut Visibility)>,
    alive: Query<&Health, Without<Companion>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut ms: ResMut<crate::stats::MissionStats>,
    mut rng: ResMut<FxStats>,
    mut barks: ResMut<Barks>,
) {
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let mut say = |sounds: &mut MessageWriter<crate::audio::PlaySound>, rng: &mut FxStats, list: &[&str], at: Vec3| {
        let n = list[((rng.rand() * list.len() as f32) as usize).min(list.len() - 1)];
        sounds.write(crate::audio::PlaySound::at(n, at));
    };
    for (e, mut c, h, t, mut vis) in &mut q {
        let at = t.translation;
        let len = if c.q { 2.533 } else { 2.0 };
        if let Some(d) = c.dead.as_mut() {
            *d += dt;
            if *d >= len + 1.0 && *vis != Visibility::Hidden {
                *vis = Visibility::Hidden;
                physics.remove_walker(&c.walker);
                commands.entity(e).remove::<Companion>();
            }
            continue;
        }
        c.hurt_t += dt;
        if let Some((n, left)) = c.hit {
            c.hit = (left - dt > 0.0).then_some((n, left - dt));
        }
        if std::mem::take(&mut c.friendly) && barks.take(Delay::Vo, now, &mut rng) {
            let l: &[&str] = if c.q { &["q1150068_friendlyfire", "q1150069_friendlyfire", "q1150070_friendlyfire", "q1150071_friendlyfire", "q1150072_friendlyfire", "q1150073_friendlyfire"] } else { &["jon50076_friendlyfire", "jon50077_friendlyfire", "jon50078_friendlyfire", "jon50079_friendlyfire", "jon50080_friendlyfire"] };
            say(&mut sounds, &mut rng, l, at);
        }
        if c.last_health.is_nan() {
            c.last_health = h.0;
        }
        if h.0 <= 0.0 {
            c.dead = Some(0.0);
            c.hit = None;
            let l: &[&str] = if c.q { &["q1150051_dead", "q1150052_dead", "q1150055_dead", "q1150056_dead", "q1150057_dead", "q1150058_dead", "q1150059_dead"] } else { &["jon50062_dead", "jon50063_dead", "jon50064_dead", "jon50065_dead", "jon50066_dead"] };
            say(&mut sounds, &mut rng, l, at);
            // not a kill (vt+0xe8 false); score_value -500 to Accidents (0x00431720)
            ms.killed(-500.0);
            info!("EVENT {} died", if c.q { "Q" } else { "Jonesy" });
            continue;
        }
        if h.0 < c.last_health {
            let d = c.last_health - h.0;
            if barks.take(Delay::Vo, now, &mut rng) {
                let l: &[&str] = if c.q { &["q1150050_help", "q1150053_help", "q1150054_help", "q1150064_hurt", "q1150065_hurt", "q1150066_hurt", "q1150067_hurt"] } else { &["jon50060_help", "jon50061_help", "jon50070_hurt", "jon50071_hurt", "jon50072_hurt", "jon50073_hurt"] };
                say(&mut sounds, &mut rng, l, at);
            } else if c.hurt_t >= 10.0 {
                c.hurt_t = 0.0;
                let l: &[&str] = if c.q { &["q1150060_hurt", "q1150061_hurt", "q1150062_hurt"] } else { &["jon50067_hurt", "jon50068_hurt"] };
                say(&mut sounds, &mut rng, l, at);
            }
            let (n, len) = if d <= 20.0 {
                ("hit_mgun", 0.3)
            } else if d <= 51.0 {
                ("hit_small", if c.q { 2.133 } else { 0.633 })
            } else {
                ("hit_med", if c.q { 2.8 } else { 1.967 })
            };
            // a small hit does not restart one already playing (states 0x12f / 0x132)
            if !(n == "hit_mgun" && c.hit.is_some()) {
                c.hit = Some((n, len));
            }
            if let Some(a) = c.attacker.take().filter(|&a| alive.get(a).is_ok_and(|h| h.0 > 0.0)) {
                if c.target.is_none_or(|t| alive.get(t).is_err()) {
                    c.target = Some(a);
                }
            }
        }
        c.last_health = h.0;
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
    ctl: Res<crate::input::Controls>,
    bot: Res<crate::playtest::Bot>,
    roman: Query<(&Transform, &crate::player::Roman, &crate::skin::Animator, &Health), Without<Companion>>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    mut q: Query<(Entity, &mut Companion, &mut Transform, Option<&crate::skin::Animator>, Option<&Health>), Without<crate::player::Roman>>,
    grunts: Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform, Without<Companion>>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut log: ResMut<crate::playtest::PlayState>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    (mut pickups, mut dynamite, mut barks, mut hud, mut pending): (
        MessageWriter<crate::pickup::SpawnPickup>,
        MessageWriter<crate::sticky::ThrowDynamite>,
        ResMut<Barks>,
        Option<ResMut<crate::hud::HudState>>,
        Local<(bool, bool)>,
    ),
) {
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let Ok((rt, r, ra, rh)) = roman.single() else { return };
    let bone = |a: &crate::skin::Animator, n: &str| a.skeleton.index.get(n).and_then(|&i| joints.get(a.joints[i]).ok()).map(|t| t.translation());
    // Roman's aim point (Game+0xa6c): where the crosshair ray meets the world, else far along it
    let aim_point = cam.single().ok().map(|c| {
        let (o, d) = (c.translation(), c.forward().as_vec3());
        physics.raycast(o + d * 20.0, d, 500.0).map(|h| h.1).unwrap_or(o + d * 520.0)
    });
    let fire_held = (ctl.down(crate::input::Act::Fire) || ctl.pressed(crate::input::Act::Fire) || bot.fire_ok) && !r.dead;
    // Roman's direction (0x00453150), flat
    let roman_dir = (Quat::from_rotation_y(r.yaw + std::f32::consts::PI) * Vec3::Z).with_y(0.0).normalize_or(Vec3::Z);
    let may_act = !r.dead && !hud.as_ref().is_some_and(|h| h.cinematic);
    team_orders(
        &mut q,
        &mut pending,
        &mut barks,
        hud.as_deref_mut(),
        &mut sounds,
        &mut rng,
        &physics,
        &grunts,
        (ctl.pressed(crate::input::Act::TeamSend), ctl.pressed(crate::input::Act::TeamRecall)),
        may_act,
        cam.single().ok().map(|c| (c.translation(), c.forward().as_vec3())),
        aim_point,
        roman_dir,
        now,
    );
    // who is fighting (the attack order's linger waits for the partner: 0x0042c6f0, guess "has a target")
    let fighting: Vec<(bool, bool)> = q.iter().filter(|(_, c, ..)| c.dead.is_none()).map(|(_, c, ..)| (c.q, c.target.is_some())).collect();
    let mut silent_return = false;
    for (e, mut c, mut t, ca, ch) in &mut q {
        // a dead companion does nothing; a hit reaction holds him (states 0x12f..0x131: movement stopped)
        if c.dead.is_some() || c.hit.is_some() {
            c.moving = false;
            c.firing = false;
            continue;
        }
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
                pickups.write(crate::pickup::SpawnPickup { name: "pickup_healthytea".into(), at: me + Vec3::Y * 4.0, vel: v, hover: false });
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
        // the anchor targets must stay near (0x004853a0 / 0x004852e0, flag 1): Roman (Defend Me: the anchor follows
        // him, flag 8), the order's point (Defend Area / Attack); none in Return To Me (flag 1 clear)
        let anchor = match c.order {
            Order::Guard => Some(rt.translation),
            Order::Loose => None,
            Order::Area { at, .. } | Order::Attack { at, .. } => Some(at),
        };
        // at each search tick a target farther (2D) than scan_distance from the anchor is refused (0x004853a0 in
        // formation) and dropped: the original's companions stand and fight until Roman has moved on (fightT: Jonesy
        // lets go at ~1350, Q at ~1440, then they run after him). The attack order's own target is kept (+0xbb0)
        if c.search <= 0.0 {
            if let (Some((_, _, gt, _)), Some(a)) = (c.target.and_then(|t| grunts.get(t).ok()), anchor.filter(|_| !matches!(c.order, Order::Attack { .. }))) {
                if (gt.translation() - a).with_y(0.0).length() > c.scan {
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
                .filter(|(_, p)| (*p - me).with_y(0.0).length() < c.scan && anchor.is_none_or(|a| (*p - a).with_y(0.0).length() < c.scan))
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
        // formation: Roman's bone for this companion; under an area / attack order the point beside the anchor
        // (0x00483c30: the reversed direction turned by +-formation_angle, x formation_distance; the side is the one
        // of the companion's bone, guess for the leader flag 0x10 of 0x00483810)
        let bone_slot = bone(ra, if c.q { "ref_q_00" } else { "ref_molmer_00" }).unwrap_or(rt.translation);
        let slot = match c.order {
            Order::Area { at, dir } | Order::Attack { at, dir } => {
                let side = |d: Vec3, v: Vec3| (d.x * v.z - d.z * v.x).signum();
                let want = side(roman_dir, bone_slot - rt.translation);
                let back = -dir;
                let a = c.angle.to_radians();
                let p1 = at + Quat::from_rotation_y(a) * back * c.distance;
                let p2 = at + Quat::from_rotation_y(-a) * back * c.distance;
                if side(dir, p1 - at) == want { p1 } else { p2 }
            }
            _ => bone_slot,
        };
        let from_roman = (rt.translation - me).with_y(0.0).length();
        // playtest log: how far each companion is from Roman
        log.0.insert(if c.q { "q_dist".into() } else { "jonesy_dist".into() }, (from_roman.round() as i64).into());
        log.0.insert(if c.q { "q_pos".into() } else { "jonesy_pos".into() }, serde_json::json!([(me.x * 10.0).round() / 10.0, (-me.z * 10.0).round() / 10.0]));
        log.0.insert(if c.q { "q_health".into() } else { "jonesy_health".into() }, ch.map(|h| h.0).unwrap_or(0.0).into());
        let order = match c.order {
            Order::Guard => "defend_me",
            Order::Loose => "return",
            Order::Area { .. } => "defend_area",
            Order::Attack { .. } => "attack",
        };
        log.0.insert(if c.q { "q_order".into() } else { "jonesy_order".into() }, order.into());
        // measured in the original's intro (hook dumps of World+0xe94): both set off within 0.5 s of Roman and run at
        // ~38 units/s about 32 units behind him; so they regroup on the distance to their slot, not to Roman
        // regroup (mode 1, 0x0048631c): Defend Me on the distance to the slot (as measured); Return To Me only when
        // Roman is farther than delay_distance; an anchored order beyond formation_distance + 2.5 of the anchor
        let regroup = match c.order {
            Order::Guard => (slot - me).with_y(0.0).length() > 15.0,
            Order::Loose => from_roman > c.delay,
            Order::Area { at, .. } | Order::Attack { at, .. } => (at - me).with_y(0.0).length() > c.distance + 2.5,
        };
        if c.target.is_none() && c.lost <= 0.0 && regroup {
            c.moving = true;
        }
        // the attack order (flag 4) idle in formation with the partner not fighting: +0xb68 runs down, then the pair
        // returns to Defend Me without a line or a HUD text (0x004845b0(0), mode 1 at 0x004862d2)
        if matches!(c.order, Order::Attack { .. }) && c.target.is_none() && !c.moving && !fighting.iter().any(|&(q, f)| q != c.q && f) {
            c.linger -= dt;
            if c.linger <= 0.0 {
                silent_return = true;
            }
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
        let support = c.target.is_none() && !c.moving && fire_held && heading_ok && matches!(c.order, Order::Guard | Order::Loose);
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
                // the fire anim starting with a target (0x00428980 -> vt+0x13c, ai_02.cpp:1860): the Fight cue under
                // vo_delay (Q11Voice 0x004875d0: Q1150038 / Q1150040_Fight; AIVoice 0x0044eab0: JON50026_Fight,
                // JON50028 / JON50029_Taunt; equal odds)
                if !c.firing && c.target.is_some() && barks.take(Delay::Vo, now, &mut rng) {
                    let l: &[&str] = if c.q { &["Q1150038_Fight", "Q1150040_Fight"] } else { &["JON50026_Fight", "JON50028_Taunt", "JON50029_Taunt"] };
                    let n = l[((rng.rand() * l.len() as f32) as usize).min(l.len() - 1)];
                    sounds.write(crate::audio::PlaySound::at(n, me));
                }
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
    if silent_return {
        for (_, mut c, ..) in &mut q {
            if c.dead.is_none() {
                c.order = Order::Guard;
                c.route.clear();
            }
        }
    }
}

/// Picks one of `n` entries the way the voice code does: (rand() * range) >> 15, here range x a uniform [0, 1).
fn roll(rng: &mut FxStats, range: f32) -> u32 {
    (rng.rand() * range) as u32
}

/// Team orders (0x0047a9a0 -> 0x004852b0 -> 0x00485120; docs/subsystems/companions.md). Key 1 / D-pad up
/// (TeammatesSend 0x12) and key 3 / D-pad down (TeammatesReturn 0x13) set the pending flags 0x2000 / 0x4000 of both
/// companions while Roman may act (0x0047a950: alive, not in a cinematic). The first of the pair to update handles
/// them for both, then clears both flags ("return" wins when both are set), unless one of them is busy with an
/// animation (modes 5 / 10: here Q's tea); a dead companion is dropped from the pair.
/// - Return (0x004845b0(1), 0x00484290 on each): Defend Me <-> Return To Me (an area / attack order -> Defend Me);
///   targets dropped, back to formation; a line under vo_delay; the HUD text "ui_teammate_defend_player" /
///   "ui_teammate_return".
/// - Send: a live grunt under the crosshair (0x00408020 of the aim) -> attack it (0x00484830 / 0x00484100: anchored
///   at it, its target, +0xb68 = 3..7 s), "ui_teammate_attack", the attack lines; else defend the aimed point
///   (0x00484ca0), "ui_teammate_defend_loc", the defend lines.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn team_orders(
    q: &mut Query<(Entity, &mut Companion, &mut Transform, Option<&crate::skin::Animator>, Option<&Health>), Without<crate::player::Roman>>,
    pending: &mut (bool, bool),
    barks: &mut Barks,
    hud: Option<&mut crate::hud::HudState>,
    sounds: &mut MessageWriter<crate::audio::PlaySound>,
    rng: &mut FxStats,
    physics: &Physics,
    grunts: &Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    (send, ret): (bool, bool),
    may_act: bool,
    ray: Option<(Vec3, Vec3)>,
    aim_point: Option<Vec3>,
    roman_dir: Vec3,
    now: f32,
) {
    if may_act {
        pending.0 |= send;
        pending.1 |= ret;
    }
    if !pending.0 && !pending.1 {
        return;
    }
    // (entity, is Q, position) of the live pair; the first is "self" (+0xbcc flag 0x200: Q)
    let live: Vec<(Entity, bool, Vec3)> = q.iter().filter(|(_, c, ..)| c.dead.is_none()).map(|(e, c, t, ..)| (e, c.q, t.translation)).collect();
    if live.is_empty() {
        *pending = (false, false);
        return;
    }
    if q.iter().any(|(_, c, ..)| c.dead.is_none() && c.tea > 0.0) {
        return;
    }
    let (send, ret) = std::mem::take(pending);
    let pair = live.len() == 2;
    let self_q = live[0].1;
    let at_of = |is_q: bool| live.iter().find(|l| l.1 == is_q).map(|l| l.2);
    let say = |sounds: &mut MessageWriter<crate::audio::PlaySound>, n: &str, who: Option<bool>| {
        sounds.write(match who.and_then(at_of) {
            Some(p) => crate::audio::PlaySound::at(n, p),
            // Roman's lines play without a speaker (audio +0xd0)
            None => crate::audio::PlaySound::ui(n),
        });
    };
    let text: &'static str;
    if ret {
        let mut guard = false;
        for &(e, ..) in &live {
            if let Ok((_, mut c, ..)) = q.get_mut(e) {
                c.order = if c.order == Order::Guard { Order::Loose } else { Order::Guard };
                guard = c.order == Order::Guard;
                c.target = None;
                c.engaged = false;
                c.lost = 0.0;
                c.route.clear();
            }
        }
        if barks.take(Delay::Vo, now, rng) {
            let generic = if pair {
                let v = roll(rng, 1000.0);
                if v < 800 {
                    true
                } else {
                    // 0x004845b0: Jonesy's or Q's line, each said by its own speaker
                    if v < 900 {
                        say(sounds, "jon25002_B_Return", Some(false))
                    } else {
                        say(sounds, "q1111007_B_Return", Some(true))
                    }
                    false
                }
            } else if roll(rng, 500.0) < 400 {
                true
            } else {
                say(sounds, if self_q { "q1111007_B_Return" } else { "jon25002_B_Return" }, Some(self_q));
                false
            };
            if generic {
                let l = ["rom50030_Return", "rom50031_Return", "rom50057_Return", "rom50058_Return"];
                say(sounds, l[(roll(rng, 400.0) / 100).min(3) as usize], None);
            }
        }
        text = if guard { "ui_teammate_defend_player" } else { "ui_teammate_return" };
    } else {
        debug_assert!(send);
        // the object under the crosshair: a live grunt (the enemy list +0x604; object types 0x51 / 0x53 not ported)
        let target = ray
            .and_then(|(o, d)| physics.raycast_bullet(o + d * 20.0, d, 1000.0))
            .and_then(|(b, _, _)| physics.bodies.get(&b).copied())
            .and_then(|ge| grunts.get(ge).ok())
            .filter(|(_, g, _, _)| g.mind != Mind::Dead)
            .map(|(ge, _, gt, _)| (ge, gt.translation()));
        let (order, attack) = match (target, aim_point) {
            (Some((_, at)), _) => (Order::Attack { at, dir: roman_dir }, true),
            (None, Some(at)) => (Order::Area { at, dir: roman_dir }, false),
            (None, None) => return,
        };
        for &(e, ..) in &live {
            if let Ok((_, mut c, ..)) = q.get_mut(e) {
                c.order = order;
                c.route.clear();
                c.lost = 0.0;
                if let Some((ge, _)) = target {
                    if c.target != Some(ge) {
                        c.engaged = false;
                    }
                    c.target = Some(ge);
                    c.linger = rng.range(3.0, 7.0);
                }
            }
        }
        if barks.take(Delay::Vo, now, rng) {
            // pair: half generic, a quarter Jonesy's, a quarter Q's; alone 70 % generic, else his own
            let who = if pair {
                match roll(rng, 400.0) {
                    v if v < 200 => None,
                    v if v < 300 => Some(false),
                    _ => Some(true),
                }
            } else if roll(rng, 1000.0) < 700 {
                None
            } else {
                Some(self_q)
            };
            match who {
                None if attack => {
                    // ROM50117..119 (with Q alive) are not in lev_1_m1's bank: silent there, as in the original
                    let m = if at_of(true).is_some() { 600.0 } else { 299.0 };
                    let l = ["rom50027_Attack", "rom50032_Attack", "rom50034_Attack", "ROM50117_Attack", "ROM50118_Attack", "ROM50119_Attack"];
                    say(sounds, l[(roll(rng, m) / 100).min(5) as usize], None);
                }
                None => {
                    let l = ["rom50028_Defend", "rom50029_Defend", "rom50114_Defend", "rom50116_Defend"];
                    say(sounds, l[(roll(rng, 400.0) / 100).min(3) as usize], None);
                }
                // the same lines for both orders (0x004849b5 / 0x00484c5a, 0x00484a5a / 0x00484be8)
                Some(false) => {
                    let l = ["jon50025_Send", "jon50131_Attack", "JON50033_Defend", "JON50130_Defend"];
                    say(sounds, l[(roll(rng, 400.0) / 100).min(3) as usize], Some(false));
                }
                Some(true) => {
                    let v = roll(rng, 400.0);
                    say(sounds, if v < 100 { "q1150039_Send" } else if v < 200 { "Q1150116_Defend" } else { "Q1150118_Defend" }, Some(true));
                }
            }
        }
        text = if attack { "ui_teammate_attack" } else { "ui_teammate_defend_loc" };
    }
    // 0x00463890 + flag 4: the order's text for 3.0 s, hiding the others (0x00467890)
    if let Some(h) = hud {
        h.team = Some((text, 3.0));
    }
    info!("EVENT team order {text}");
}
