//! The army of the base-defence levels (lev_1_m4, lev_2_m4, lev_3_m4_bd, lev_4_m5, lev_5_m5): army timers send
//! platoons of fake grunts marching along mover paths; at the path's "release_platoon" note each platoon turns its
//! members into real grunts one by one, while the number of live army grunts stays under the grunt limit; the real
//! grunts run to marker splines near the wall or carry ladders to it and climb over (trigger 5000, counted by the
//! level's counters towards "overrun" and failure). Fake members die to a blast or a hit within the platoon's
//! kill_range (trigger 27, counted towards the win).
//!
//! The original (docs/subsystems/defence.md):
//! - ArmyTimerObject (vtable 0x005a2800, behaviour army_timer; ArmyTimerObject_ReadProps 0x004968b0, the update
//!   0x00496da0, ArmyTimerObject_Fire 0x00496c60): a TimerObject (0x00496160) with up to 50 "create_platoon_N" =
//!   "time/marker set/ladder set" and up to 10 "grunt_limits_N" = "time/max/unpause", each list advanced by
//!   Timer_Advance 0x00496690 (entries fire when the elapsed time passes them; after the last the timer removes itself).
//!   A platoon is ObjectSystem_CreateObject(platoon_name) at the timer's matrix, its marker and ladder sets from the
//!   global settings' army_marker{1..3}_tag_min/max and army_ladder{1..3}_tag_min/max (0x004331f0; GlobalSettings
//!   load render_r6.cpp:1001-1024: army_max_grunts 16 -> Game+0xbf4 = +0xbf0, army_unpause_count 14 -> +0xbf8), given
//!   gen_text as its mover (vt+0x238) and the player as target.
//! - FakeGruntObject (vtable 0x005a4da8, behaviour grunt_fake; init 0x00432cb0, update FakeGrunt_Update 0x00446460,
//!   damage 0x00433000, event "release_platoon" 0x0043ce30): one member per ref_<kind>_0N point of its model (kinds
//!   captain, grunt, regular, torch: 0x00432ef0), drawn as particles in the original (pool 2 of Game+0xa90) and as
//!   low-detail grunts running here. While marching: live army grunts (World+0xe84, 0x0043ce10) >= Game+0xbf0 ->
//!   +0xbf0 = +0xbf8 and the platoon halts (state 0x66) until the count drops below +0xbf0 (then +0xbf0 = +0xbf4).
//!   Released: every 0.1..0.2 s (rand * 3.05e-6 + 0.1) while under the limit the first member left becomes a
//!   "grunt" / "grunt_regular" / "captain" (by kind) that takes a free ladder of its ladder set (0x00429900: one per
//!   army_ladder_delay_min..max seconds, a random ladder spline of the tag range not in use; AI_ClimbLadder
//!   0x0042d710) or follows its marker set's spline (AI_FollowMarkerSpline 0x0042d1b0); the last one removes the
//!   platoon. A damage record at a point kills every member within kill_range (60 default, p_platoon 50) of it:
//!   fx "FX_platoon_<kind>_die", trigger 0x1b (27).
//! - The ladder grunts (Marker_ProcessArrival 0x0044ba30 "ic_*" actions -> BowGruntObject vt+0x248 0x0043ab40):
//!   at ic_plant_ladder (spline flag 0x10 clear) "mover_grunt_ladder_plant", at ic_climb_ladder (flag set)
//!   "mover_grunt_ladder_climb", whose notes send trigger 5000 and deactivate the grunt; at ic_scale_base_wall (the
//!   marker sets' splines) one of "mover_grunt_scale" .. "_4" at random (GruntObject's 0x0043bcf0 likewise), the same
//!   way over the wall.
//!
//! Hooks outside this file: level.rs SpawnGrunt::army (the orders inserted on the grunt), main.rs (plugin).

use std::collections::HashSet;

use bevy::ecs::system::SystemParam;
use bevy::prelude::*;

use crate::ai::{Grunt, Mind, SplineRun};
use crate::mover::{Mover, MoverNote};
use crate::levelreset::LevelAppExt;

impl ArmyGrunt {
    /// Running an ic_* script or the breach: not free for the assault's search (ai/assault.rs)
    pub fn busy(&self) -> bool {
        self.script.is_some() || self.breach
    }
}

/// A real grunt released by a platoon: its orders (+0x558 / +0x55c marker tags, the ladder spline) and the ladder
/// script it runs.
#[derive(Component, Clone, Debug, Default)]
pub struct ArmyGrunt {
    /// the ladder spline it carries a ladder along (AI_ClimbLadder 0x0042d710)
    pub ladder: Option<i64>,
    /// sent through the open gate (the army breach, 0x00429a50)
    breach: bool,
    /// the ic_* script running (an anim overlay on slot 4) and the time into it
    script: Option<IcScript>,
}

#[derive(Clone, Debug)]
struct IcScript {
    anim: String,
    t: f32,
    /// the spline index to go on from when the script ends without removing the grunt
    resume: Option<SplineRun>,
    plant: bool,
}

/// The overlay slot the ic_* scripts play on (above the AI's slots 0 and 1).
const SCRIPT_SLOT: usize = 4;

/// One "create_platoon_N" / "grunt_limits_N" entry: time and two numbers.
#[derive(Clone, Copy, Debug, PartialEq)]
struct Entry {
    time: f32,
    a: i64,
    b: i64,
}

/// An army timer (ArmyTimerObject): its two lists, advanced separately.
#[derive(Clone, Debug)]
struct ArmyTimer {
    id: u32,
    activate: i64,
    running: bool,
    removed: bool,
    platoons: Vec<Entry>,
    limits: Vec<Entry>,
    /// elapsed times +0x788 (platoons) / +0x790 (limits)
    elapsed: (f32, f32),
    origin: Mat4,
    platoon: String,
    gen_text: String,
    /// anim_name (+0x464, +0x4aa set): an animated timer plays it on itself; its notes are commands
    /// ("create_platoon NAME A B": TimerObject_Command 0x004964d0); it ends with the anim (TimerObject_Update 0x00496750)
    anim: Option<String>,
    anim_t: f32,
}

/// How big the army is, as a setting (a later difficulty selector scales the waves; 1.0 is the original): each platoon
/// the level's timers create becomes `wave_scale` platoons (fractions carried over: 1.5 adds one every second
/// platoon, beside it), and the grunt limits (max and unpause counts) are multiplied by `limit_scale`.
#[derive(Resource, Clone, Copy, Debug)]
pub struct ArmyTuning {
    pub wave_scale: f32,
    pub limit_scale: f32,
}

impl Default for ArmyTuning {
    fn default() -> Self {
        Self { wave_scale: 1.0, limit_scale: 1.0 }
    }
}

impl ArmyTuning {
    fn limit(&self, n: i64) -> i64 {
        (n as f32 * self.limit_scale).round() as i64
    }
}

/// The army's shared state (the Game fields +0xbf0..+0xc38, +0xaec..+0xaf8).
#[derive(Resource, Default)]
pub struct Army {
    loaded: bool,
    timers: Vec<ArmyTimer>,
    /// Game+0xbf0 (the limit now), +0xbf4 (max), +0xbf8 (unpause)
    limit: i64,
    max: i64,
    unpause: i64,
    /// army_marker{1..3}_tag_min/max and army_ladder{1..3}_tag_min/max by set index (0: none)
    marker_sets: [(i64, i64); 4],
    ladder_sets: [(i64, i64); 4],
    /// army_ladder_delay_min / max (mission, 1.0 / 1.0) and the time the next ladder may be taken (+0xaf8 + +0xaf4)
    ladder_delay: (f32, f32),
    ladder_next: f32,
    /// ladder splines with a ladder planted (spline flag 0x10)
    planted: HashSet<i64>,
    /// ladder splines handed out this frame or earlier and not yet seen on a grunt (time handed out)
    pending_ladders: Vec<(i64, f32)>,
    /// receive_trigger_pre_success of the mission: the army stops (MissionObject_OnTrigger 0x0044d600)
    pre_success: i64,
    stopped: bool,
    /// the last crowd charge sound (Game+0xb38, every 20 s at most)
    crowd_at: f32,
    /// the fraction of a platoon owed by wave_scale (ArmyTuning)
    wave_carry: f32,
    /// the army breach (mission army_breach_marker_tag Game+0xc44, army_breach_run_max +0xc4c, the runners sent so
    /// far +0xc48): while the gate stands open (Mission::breach_open, Game+0xcbe) grunts run its spline into the base
    breach_tag: i64,
    breach_max: u32,
    breach_runners: u32,
    pub stats: ArmyStats,
}

impl Army {
    /// A ladder stands on this ladder spline (spline bit 4, 0x0044b7b0)
    pub fn is_planted(&self, tag: i64) -> bool {
        self.planted.contains(&tag)
    }
}

#[derive(Default, Clone, Copy, Debug, serde::Serialize)]
pub struct ArmyStats {
    pub platoons: u32,
    pub marching: u32,
    pub members: u32,
    pub released: u32,
    pub fake_killed: u32,
    pub over_wall: u32,
    pub live: u32,
}

/// A platoon of fake grunts (FakeGruntObject).
#[derive(Component, Debug)]
pub struct Platoon {
    members: Vec<Member>,
    kill_range: f32,
    /// the platoon model's box (platoon_1: 50 x 4 x 14 round its row of members; game axes, the platoon's frame): a
    /// player's bullet that enters it is a damage record at the entry point (the object's collision; the record then
    /// kills every member within kill_range, 0x00433000)
    hit_box: Option<(Vec3, Vec3)>,
    markers: (i64, i64),
    ladders: (i64, i64),
    /// state 0x66: halted at the grunt limit
    halted: bool,
    /// "release_platoon" received (+0x484 bit 30) and the release countdown +0x490
    releasing: bool,
    release_timer: f32,
}

impl Platoon {
    /// Where its live members are (engine space), given its mover.
    pub fn member_points(&self, m: &Mover) -> Vec<Vec3> {
        let world = Mat4::from_quat(crate::level::game_to_engine()) * m.world;
        self.members.iter().filter(|x| x.alive).map(|x| world.transform_point3(x.local)).collect()
    }

    /// Still marching (not yet releasing its members).
    pub fn marching(&self) -> bool {
        !self.releasing
    }
}

#[derive(Debug, Clone)]
struct Member {
    /// 0 grunt (bow), 1 regular, 2 captain, 3 torch (0x00432ef0)
    kind: u8,
    /// the ref point in the platoon's frame (game axes)
    local: Vec3,
    visual: Option<Entity>,
    alive: bool,
}

impl Member {
    fn template(&self) -> &'static str {
        match self.kind {
            1 => "grunt_regular",
            2 => "captain",
            _ => "grunt",
        }
    }
    fn die_fx(&self) -> &'static str {
        ["fx_platoon_grunt_die2", "fx_platoon_regular_die", "fx_platoon_captain_die", "fx_platoon_torch_die"][self.kind.min(3) as usize]
    }
}

pub struct ArmyPlugin;

impl Plugin for ArmyPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<ArmyTuning>()
            .init_level_resource::<Army>()
            .add_systems(Update, (load, triggers, timers, platoons, fake_hits, army_grunts, breach, log).chain().before(crate::fx::FxSet).run_if(crate::playing));
    }
}

/// Parses "a/b/c" entries (ArmyTimerObject_ReadProps: atof then atol twice; time <= 0 skipped), up to `last`, stopping at
/// the first missing name; sorted by time.
fn entries(p: &crate::gamedata::Props, fmt: &str, last: usize) -> Vec<Entry> {
    let mut out = Vec::new();
    for i in 1..=last {
        let Some(v) = p.s(&format!("{fmt}{i}")) else { break };
        let parts: Vec<&str> = v.split('/').collect();
        if parts.len() != 3 {
            warn!("Army Timer has invalid property format {v}");
            continue;
        }
        let time = parts[0].trim().parse::<f32>().unwrap_or(0.0);
        if time <= 0.0 {
            continue;
        }
        out.push(Entry { time, a: parts[1].trim().parse().unwrap_or(0), b: parts[2].trim().parse().unwrap_or(0) });
    }
    out.sort_by(|x, y| x.time.total_cmp(&y.time));
    out
}

/// Timer_Advance 0x00496690: the entries passed between `prev` and `prev + dt` (prev <= time < now), and whether the
/// last one fired (the timer ends then).
fn advance(list: &[Entry], elapsed: &mut f32, dt: f32) -> (Vec<Entry>, bool) {
    let prev = *elapsed;
    *elapsed += dt;
    let mut fired = Vec::new();
    let mut ended = false;
    for (i, e) in list.iter().enumerate() {
        if prev > e.time {
            continue;
        }
        if !(*elapsed > e.time) {
            break;
        }
        fired.push(*e);
        if i + 1 == list.len() {
            ended = true;
        }
    }
    (fired, ended)
}

/// The army's data from the level (once per level).
fn load(mut army: ResMut<Army>, tuning: Res<ArmyTuning>, launch: Res<crate::Launch>, data: Res<crate::gamedata::GameData>, mission: Option<Res<crate::mission::Mission>>) {
    if army.loaded || mission.is_none() {
        return;
    }
    army.loaded = true;
    let scene = crate::scene::read_scene(&launch.scene);
    let mut gs = data.props("p_globalsettings");
    for (k, v) in &scene.settings {
        gs.0.insert(k.clone(), v.clone());
    }
    let mut mp = data.props("p_mission");
    if let Some(m) = scene.logic.iter().find(|l| l.template == "mission") {
        for (k, v) in &m.props {
            mp.0.insert(k.clone(), v.clone());
        }
    }
    army.max = tuning.limit(gs.i("army_max_grunts", 16));
    army.limit = army.max;
    army.unpause = tuning.limit(gs.i("army_unpause_count", 14));
    for i in 1..=3 {
        army.marker_sets[i] = (gs.i(&format!("army_marker{i}_tag_min"), 0), gs.i(&format!("army_marker{i}_tag_max"), 0));
        army.ladder_sets[i] = (gs.i(&format!("army_ladder{i}_tag_min"), 0), gs.i(&format!("army_ladder{i}_tag_max"), 0));
    }
    army.ladder_delay = (mp.f("army_ladder_delay_min", 1.0), mp.f("army_ladder_delay_max", 1.0));
    army.pre_success = mp.i("receive_trigger_pre_success", 0);
    army.breach_tag = mp.i("army_breach_marker_tag", 0);
    army.breach_max = mp.i("army_breach_run_max", 5).max(0) as u32;
    for inst in &scene.instances {
        let p = data.instance_props(&inst.template, &inst.props);
        if p.s("behavior") != Some("army_timer") {
            continue;
        }
        let (_, r, t) = crate::scene::game_matrix(&inst.transform).to_scale_rotation_translation();
        let activate = p.i("receive_trigger_activate", 0);
        army.timers.push(ArmyTimer {
            id: inst.id,
            activate,
            running: activate == 0,
            removed: false,
            platoons: entries(&p, "create_platoon_", 50),
            limits: entries(&p, "grunt_limits_", 10),
            elapsed: (0.0, 0.0),
            origin: Mat4::from_rotation_translation(r, t),
            platoon: p.s("platoon_name").unwrap_or("noPlatoonNameGive").to_owned(),
            gen_text: p.s("gen_text").unwrap_or("none").to_owned(),
            anim: p.s("anim_name").filter(|a| !a.eq_ignore_ascii_case("none")).map(str::to_lowercase),
            anim_t: 0.0,
        });
    }
    if !army.timers.is_empty() {
        let n: usize = army.timers.iter().map(|t| t.platoons.len()).sum();
        let animated = army.timers.iter().filter(|t| t.anim.is_some()).count();
        info!("EVENT army: {} timers ({animated} animated), {} platoons listed; grunt limit {} (unpause {})", army.timers.len(), n, army.max, army.unpause);
    }
}

/// Army timers' activation (TimerObject_OnTrigger 0x004965d0) and the mission's pre-success (the army stops:
/// Game+0xbf0 = +0xbf8 = 0, every army grunt retreats, MissionObject_OnTrigger 0x0044d600).
fn triggers(
    mut commands: Commands,
    mut army: ResMut<Army>,
    mut events: MessageReader<crate::mission::SendTrigger>,
    grunts: Query<(Entity, &Grunt)>,
    markers: Option<Res<crate::alarm::Markers>>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut retreat: ResMut<crate::ai::retreat::RetreatStats>,
) {
    let ids: Vec<i64> = events.read().map(|e| e.0).collect();
    for t in ids {
        for timer in army.timers.iter_mut().filter(|x| x.activate != 0 && x.activate == t) {
            timer.running = true;
            timer.activate = 0;
            info!("EVENT army: timer {} started", timer.id);
        }
        if t != 0 && t == army.pre_success && !army.stopped {
            army.stopped = true;
            army.limit = 0;
            army.unpause = 0;
            // the retreat (vt+0x234(&dir) on each AI, the direction of the marker tagged -1: ai/retreat.rs)
            let dir = crate::ai::retreat::direction(markers.as_deref());
            crate::ai::retreat::hand_out(&mut commands, grunts.iter().map(|(e, g)| (e, g.mind)), dir, &mut rng, &mut retreat);
            info!("EVENT army: pre-success: the army retreats");
        }
    }
}

/// Spawning models at run time (platoon members).
#[derive(SystemParam)]
pub struct ModelKit<'w> {
    lib: ResMut<'w, crate::scene::Library>,
    anims: ResMut<'w, crate::skin::AnimLibrary>,
    binds: ResMut<'w, Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>>,
    assets: Res<'w, AssetServer>,
    meshes: ResMut<'w, Assets<Mesh>>,
    materials: ResMut<'w, Assets<StandardMaterial>>,
}

impl ModelKit<'_> {
    /// One model under a new entity at `tr` (skinned models get their rig and an Animator).
    pub fn spawn(&mut self, commands: &mut Commands, model: &str, tr: Transform, shadows: bool) -> Option<Entity> {
        let e = commands.spawn((tr, Visibility::default(), Name::new(model.to_owned()))).id();
        if self.attach(commands, e, model, shadows).is_none() {
            commands.entity(e).despawn();
            return None;
        }
        Some(e)
    }

    /// One model's parts (and rig) under an existing entity; its bounds (model space), or None when the model is
    /// missing.
    pub fn attach(&mut self, commands: &mut Commands, e: Entity, model: &str, shadows: bool) -> Option<crate::fx::Bounds> {
        use crate::scene::ClonedParts;
        let m = self.lib.load_model(model, false, &self.assets, &mut self.meshes, &mut self.materials).cloned_parts()?;
        let mut ctx = crate::level::SkinCtx { lib: &mut self.anims, binds: &mut self.binds };
        crate::level::attach_model(commands, e, &m, shadows, Some(&mut ctx), "ref_rhand");
        Some(crate::fx::Bounds { center: (m.min + m.max) * 0.5, radius: (m.max - m.min).length() * 0.5 })
    }

    /// A model's vertices (model space, game axes): a collision model's points for a convex hull (movable.rs).
    pub fn model_points(&mut self, model: &str) -> Option<Vec<Vec3>> {
        self.lib.load_model(model, false, &self.assets, &mut self.meshes, &mut self.materials).map(|m| m.vertices.clone())
    }

    pub fn anim(&mut self, name: &str) -> Option<std::sync::Arc<crate::skin::Anim>> {
        self.anims.anim(name)
    }

    /// The level's export folder (anims/, skeletons/ ...).
    pub fn dir(&self) -> std::path::PathBuf {
        self.anims.dir.clone()
    }

    /// The model and anim libraries for baking a crowd clip (crowd.rs).
    pub fn bake_input(&mut self) -> crate::crowd::BakeInput<'_> {
        crate::crowd::BakeInput { models: &mut self.lib, anims: &mut self.anims, binds: &mut self.binds, assets: &self.assets, meshes: &mut self.meshes, materials: &mut self.materials }
    }
}

/// The member ref points of a platoon template (FakeGrunt_Update's first call: ref_captain_0N, ref_grunt_0N,
/// ref_regular_0N, ref_torch_0N probed from 0 until one is missing), read from its reference anim; game axes.
/// A template's model bounds (models/<name>.json "min" / "max": game axes, model space).
fn model_box(kit: &ModelKit, data: &crate::gamedata::GameData, template: &str) -> Option<(Vec3, Vec3)> {
    let model = data.templates.get(template)?.model.clone()?;
    let v: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(kit.lib.dir.join("models").join(format!("{model}.json"))).ok()?).ok()?;
    let p = |k: &str| -> Option<Vec3> {
        let a = v.get(k)?.as_array()?;
        Some(Vec3::new(a.first()?.as_f64()? as f32, a.get(1)?.as_f64()? as f32, a.get(2)?.as_f64()? as f32))
    };
    Some((p("min")?, p("max")?))
}

/// The distance from point c to the segment a..b.
fn segment_distance(a: Vec3, b: Vec3, c: Vec3) -> f32 {
    let ab = b - a;
    let u = ((c - a).dot(ab) / ab.length_squared().max(1e-6)).clamp(0.0, 1.0);
    (a + ab * u).distance(c)
}

/// Where the segment a..b first enters the box (slab test), if it does.
fn segment_enters_box(a: Vec3, b: Vec3, (lo, hi): (Vec3, Vec3)) -> Option<Vec3> {
    let d = b - a;
    let (mut t0, mut t1) = (0.0f32, 1.0f32);
    for i in 0..3 {
        if d[i].abs() < 1e-6 {
            if a[i] < lo[i] || a[i] > hi[i] {
                return None;
            }
            continue;
        }
        let (u, v) = ((lo[i] - a[i]) / d[i], (hi[i] - a[i]) / d[i]);
        t0 = t0.max(u.min(v));
        t1 = t1.min(u.max(v));
        if t0 > t1 {
            return None;
        }
    }
    Some(a + d * t0)
}

fn member_points(kit: &mut ModelKit, data: &crate::gamedata::GameData, template: &str) -> Vec<(u8, Vec3)> {
    let Some(reference) = data.templates.get(template).and_then(|t| t.default_anim.clone()) else { return Vec::new() };
    let Some(anim) = kit.anim(&reference) else { return Vec::new() };
    let mut out = Vec::new();
    // member order (0x00432ef0): grunts, regulars, captains, torches
    for (kind, name) in [(0u8, "grunt"), (1, "regular"), (2, "captain"), (3, "torch")] {
        let mut n = 0;
        while let Some(p) = anim.translation(&format!("ref_{name}_0{n}"), 0.0) {
            out.push((kind, p));
            n += 1;
        }
    }
    out
}

/// The army timers' lists (ArmyTimerObject update 0x00496da0): platoons created, grunt limits changed.
fn timers(
    mut commands: Commands,
    time: Res<Time>,
    mut army: ResMut<Army>,
    tuning: Res<ArmyTuning>,
    data: Res<crate::gamedata::GameData>,
    mut kit: ModelKit,
    mut crowds: ResMut<crate::crowd::CrowdLibrary>,
) {
    let dt = time.delta_secs();
    let army = &mut *army;
    let mut made = Vec::new();
    let mut limits = Vec::new();
    for t in army.timers.iter_mut().filter(|t| t.running && !t.removed) {
        // an animated timer: its anim's notes (TimerObject_Command 0x004964d0), the end with the anim
        if let Some(name) = t.anim.clone() {
            let Some(anim) = kit.anim(&name) else {
                t.removed = true;
                continue;
            };
            let t0 = t.anim_t;
            t.anim_t += dt;
            for line in crate::mover::notes_between(&anim, t0, t.anim_t) {
                let w: Vec<&str> = line.split_whitespace().collect();
                if w.first().is_some_and(|c| c.eq_ignore_ascii_case("create_platoon")) && w.len() >= 2 {
                    let num = |i: usize| w.get(i).and_then(|x| x.parse::<i64>().ok()).unwrap_or(0);
                    made.push((t.id, t.origin, w[1].to_lowercase(), t.gen_text.clone(), Entry { time: t.anim_t, a: num(2), b: num(3) }));
                }
            }
            // the anim's end removes the timer (TimerObject_Update 0x00496750); until then its lists run too (0x00496da0)
            if t.anim_t >= anim.duration {
                t.removed = true;
                continue;
            }
        }
        let (fired, end_a) = advance(&t.platoons, &mut t.elapsed.0, dt);
        for e in fired {
            made.push((t.id, t.origin, t.platoon.clone(), t.gen_text.clone(), e));
        }
        let (fired, end_b) = advance(&t.limits, &mut t.elapsed.1, dt);
        limits.extend(fired);
        if end_a || end_b {
            t.removed = true;
        }
    }
    // grunt limits (ArmyTimerObject_Fire with +0x78d): +0xbf0 = max when it was at the max, else the unpause count
    for e in limits {
        let (max, unpause) = (tuning.limit(e.a), tuning.limit(e.b));
        army.limit = if army.limit == army.max { max } else { unpause };
        army.max = max;
        army.unpause = unpause;
        info!("EVENT army: grunt limit {} (unpause {})", army.max, army.unpause);
    }
    // the wave scale: each platoon listed becomes wave_scale platoons, the extra ones beside it
    let made: Vec<_> = made
        .into_iter()
        .flat_map(|m| {
            army.wave_carry += tuning.wave_scale;
            let n = army.wave_carry.floor().max(0.0) as usize;
            army.wave_carry -= n as f32;
            (0..n).map(move |k| {
                let (id, origin, template, gen_text, e) = m.clone();
                let side = if k % 2 == 1 { 1.0 } else { -1.0 } * (k as f32 / 2.0).ceil() * 60.0;
                (id, origin * Mat4::from_translation(Vec3::X * side), template, gen_text, e)
            })
        })
        .collect();
    for (id, origin, template, gen_text, e) in made {
        let p = data.template_props(&template);
        let points = member_points(&mut kit, &data, &template);
        let set = |sets: &[(i64, i64); 4], i: i64| sets.get(i as usize).copied().unwrap_or((0, 0));
        let pe = commands
            .spawn((crate::mover::engine_transform(origin), Visibility::default(), Name::new(template.clone())))
            .id();
        let mut mover = Mover::new(&gen_text, origin);
        mover.generic = false;
        let mut members = Vec::new();
        for (kind, local) in points {
            let member = Member { kind, local, visual: None, alive: true };
            // the member drawn as a grunt of its kind running, one instanced mesh each (crowd.rs; the original's
            // sprite: a particle of pool 2 of Game+0xa90, FakeGrunt_Update 0x00446460); its pace is set by
            // `platoons` once it exists
            let model = data.templates.get(member.template()).and_then(|t| t.model.clone());
            let clip = model.and_then(|m| crowds.clip(kit.bake_input(), &m, MEMBER_ANIM));
            let visual = clip.map(|c| crate::crowd::CrowdLibrary::spawn(&mut commands, &c, Transform::from_translation(local), member_phase(members.len(), c.duration), 1.0, false));
            if let Some(v) = visual {
                commands.entity(pe).add_child(v);
                commands.entity(v).insert(MemberPace);
            }
            members.push(Member { visual, ..member });
        }
        army.stats.platoons += 1;
        debug!("army: timer {id} created a platoon ({} members, path {gen_text}, markers {:?}, ladders {:?}) at {:.0}", members.len(), set(&army.marker_sets, e.a), set(&army.ladder_sets, e.b), e.time);
        commands.entity(pe).insert((
            mover,
            Platoon {
                members,
                kill_range: p.f("kill_range", 60.0),
                hit_box: model_box(&kit, &data, &template),
                markers: set(&army.marker_sets, e.a),
                ladders: set(&army.ladder_sets, e.b),
                halted: false,
                releasing: false,
                release_timer: 0.0,
            },
        ));
    }
}

/// The members' run anim (the regular's template default).
const MEMBER_ANIM: &str = "grunt_run_no_sound";

/// A member whose pace (its rate through the run cycle) is still to be drawn.
#[derive(Component)]
struct MemberPace;

/// Where in the run cycle member `i` of a platoon starts (spread by the golden ratio, so neighbours are out of step
/// without drawing random numbers).
fn member_phase(i: usize, duration: f32) -> f32 {
    (i as f32 * 0.618_034).fract() * duration
}

/// FakeGrunt_Update 0x00446460 and the "release_platoon" event 0x0043ce30.
#[allow(clippy::too_many_arguments)]
fn platoons(
    mut commands: Commands,
    time: Res<Time>,
    mut army: ResMut<Army>,
    mut q: Query<(Entity, &mut Platoon, &mut Mover)>,
    mut notes: MessageReader<MoverNote>,
    live: Query<&Grunt, With<ArmyGrunt>>,
    markers: Option<Res<crate::alarm::Markers>>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut runners: Query<(Entity, &mut crate::crowd::CrowdMember), With<MemberPace>>,
) {
    let now = time.elapsed_secs();
    let dt = time.delta_secs();
    let army = &mut *army;
    // the members' run cycle (each a little out of step)
    for (e, mut m) in &mut runners {
        m.speed = rng.range(0.85, 1.15);
        commands.entity(e).remove::<MemberPace>();
    }
    for n in notes.read() {
        if n.text.eq_ignore_ascii_case("release_platoon") {
            if let Ok((_, mut p, mut m)) = q.get_mut(n.entity) {
                p.releasing = true;
                p.release_timer = 0.0;
                m.paused = true;
                // the crowd's charge, at most every 20 s (Game+0xb38, K 0x005bcbe8)
                if now - army.crowd_at >= 20.0 || army.crowd_at == 0.0 {
                    army.crowd_at = now;
                    sounds.write(crate::audio::PlaySound { name: "amb_base_crowd_charge_lp".into(), at: Some(m.world.w_axis.truncate()), looped: Some(false), ..default() });
                }
            }
        }
    }
    let mut live_n = live.iter().filter(|g| g.mind != Mind::Dead).count() as i64;
    army.pending_ladders.retain(|(_, t)| now - *t < 1.0);
    let busy: HashSet<i64> = army.pending_ladders.iter().map(|(t, _)| *t).collect();
    let mut stats = ArmyStats { platoons: army.stats.platoons, released: army.stats.released, fake_killed: army.stats.fake_killed, over_wall: army.stats.over_wall, ..default() };
    for (pe, mut p, mut m) in &mut q {
        stats.members += p.members.iter().filter(|x| x.alive).count() as u32;
        if p.releasing {
            if live_n >= army.limit {
                continue;
            }
            p.release_timer -= dt;
            if p.release_timer > 0.0 {
                continue;
            }
            let left = p.members.iter().filter(|x| x.alive).count();
            if let Some(i) = p.members.iter().position(|x| x.alive) {
                let member = p.members[i].clone();
                p.members[i].alive = false;
                if let Some(v) = member.visual {
                    commands.entity(v).despawn();
                }
                let world = m.world * Mat4::from_translation(member.local);
                let fwd_game = m.world.transform_vector3(Vec3::NEG_Y).with_z(0.0).normalize_or(Vec3::NEG_Y);
                let at = crate::mover::engine_transform(world).translation;
                let fwd = Mat4::from_quat(crate::level::game_to_engine()).transform_vector3(fwd_game);
                // a ladder (0x00429900, captains never: kind 2) else the marker set
                let mut ladder = None;
                if member.kind != 2 && p.ladders.0 != 0 && now >= army.ladder_next {
                    if let Some(mk) = markers.as_deref() {
                        let taken: HashSet<i64> = live.iter().filter(|g| g.mind != Mind::Dead).filter_map(|g| g.spline.map(|s| s.tag)).chain(busy.iter().copied()).collect();
                        let free: Vec<i64> = (p.ladders.0..=p.ladders.1).filter(|t| mk.spline_len(*t) > 0 && !taken.contains(t)).collect();
                        if !free.is_empty() {
                            let tag = free[((rng.rand() * free.len() as f32) as usize).min(free.len() - 1)];
                            ladder = Some(tag);
                            army.pending_ladders.push((tag, now));
                            army.ladder_next = now + rng.range(army.ladder_delay.0, army.ladder_delay.1);
                        }
                    }
                }
                let tags = ladder.map(|t| (t, t)).or((p.markers.0 != 0).then_some(p.markers));
                spawn.write(crate::level::SpawnGrunt {
                    at,
                    template: member.template().into(),
                    exit: Some(at + fwd),
                    marker_tags: tags,
                    army: Some(ArmyGrunt { ladder, ..default() }),
                    ..default()
                });
                live_n += 1;
                army.stats.released += 1;
                stats.released += 1;
                p.release_timer = rng.rand() * 0.1 + 0.1;
                if left <= 1 {
                    commands.entity(pe).despawn();
                }
            } else {
                commands.entity(pe).despawn();
            }
            continue;
        }
        stats.marching += 1;
        if p.halted {
            if live_n < army.limit {
                army.limit = army.max;
                p.halted = false;
                m.paused = false;
            }
        } else if live_n >= army.limit {
            army.limit = army.unpause;
            p.halted = true;
            m.paused = true;
        }
    }
    stats.live = live_n.max(0) as u32;
    army.stats = stats;
}

/// FakeGruntObject_Slot52 0x00433000: a damage record at a point kills every member within kill_range of it. Blasts
/// (fx::Explode) and the player's projectiles passing through the platoon (its collision box: the members' points,
/// 4 units round, 14 high: guess at the platoon's box) are the records here.
#[allow(clippy::too_many_arguments)]
fn fake_hits(
    mut commands: Commands,
    time: Res<Time>,
    mut army: ResMut<Army>,
    mut blasts: MessageReader<crate::fx::Explode>,
    mut q: Query<(Entity, &mut Platoon, &Mover)>,
    shots: Query<(Entity, &Transform, &crate::player::Projectile)>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut triggers: MessageWriter<crate::mission::SendTrigger>,
) {
    let dt = time.delta_secs();
    let to_engine = Mat4::from_quat(crate::level::game_to_engine());
    let mut points: Vec<Vec3> = blasts.read().map(|b| b.at).collect();
    // the player's shots this frame: the segment each crossed
    let segments: Vec<(Entity, Vec3, Vec3)> = shots.iter().filter(|(_, _, sp)| sp.from_player).map(|(se, st, sp)| (se, st.translation - sp.vel * dt, st.translation)).collect();
    let mut used = HashSet::new();
    for (pe, mut p, m) in &mut q {
        let world = to_engine * m.world;
        let alive: Vec<(usize, Vec3)> = p.members.iter().enumerate().filter(|(_, x)| x.alive).map(|(i, x)| (i, world.transform_point3(x.local))).collect();
        if alive.is_empty() {
            continue;
        }
        let mut hits = points.clone();
        // a sphere round everything a shot can hit here (the box's corners, the members' chests and their 5-unit
        // reach): a segment that misses it misses both tests below
        let inv = world.inverse();
        let corners = p.hit_box.map(|(lo, hi)| (0..8).map(move |k| world.transform_point3(Vec3::new([lo.x, hi.x][k & 1], [lo.y, hi.y][(k >> 1) & 1], [lo.z, hi.z][k >> 2]))));
        let reach: Vec<Vec3> = alive.iter().map(|(_, c)| *c + Vec3::Y * 6.0).chain(corners.into_iter().flatten()).collect();
        let centre = reach.iter().copied().sum::<Vec3>() / reach.len() as f32;
        let radius = reach.iter().map(|r| r.distance(centre)).fold(0.0, f32::max) + 5.1;
        for &(se, a, b) in &segments {
            if used.contains(&se) || segment_distance(a, b, centre) > radius {
                continue;
            }
            // the platoon's box (its collision), in the platoon's frame
            if let Some(bx) = p.hit_box {
                if let Some(at) = segment_enters_box(inv.transform_point3(a), inv.transform_point3(b), bx) {
                    hits.push(world.transform_point3(at));
                    used.insert(se);
                    commands.entity(se).despawn();
                    continue;
                }
            }
            for (_, c) in &alive {
                let c = *c + Vec3::Y * 6.0;
                let ab = b - a;
                let u = ((c - a).dot(ab) / ab.length_squared().max(1e-6)).clamp(0.0, 1.0);
                let near = a + ab * u;
                if near.distance(c) < 5.0 {
                    hits.push(near);
                    used.insert(se);
                    commands.entity(se).despawn();
                    break;
                }
            }
        }
        for h in hits {
            for (i, c) in &alive {
                if !p.members[*i].alive || c.distance(h) >= p.kill_range {
                    continue;
                }
                p.members[*i].alive = false;
                if let Some(v) = p.members[*i].visual.take() {
                    commands.entity(v).despawn();
                }
                fx.write(crate::fx::StartFx { name: p.members[*i].die_fx().into(), at: *c, rotation: Quat::IDENTITY, follow: None, position_only: false });
                triggers.write(crate::mission::SendTrigger(0x1b));
                army.stats.fake_killed += 1;
            }
        }
        if p.members.iter().all(|x| !x.alive) {
            commands.entity(pe).despawn();
        }
    }
    points.clear();
}

/// The released grunts: their death trigger, and the ladder scripts (Marker_ProcessArrival 0x0044ba30's ic_* branch
/// with BowGruntObject vt+0x248 0x0043ab40): ai.rs only stands at an ic_* marker, so the arrival is taken over here
/// before the AI's update sees it.
#[allow(clippy::too_many_arguments)]
fn army_grunts(
    time: Res<Time>,
    mut army: ResMut<Army>,
    markers: Option<Res<crate::alarm::Markers>>,
    mut q: Query<(Entity, &mut ArmyGrunt, &mut Grunt, &GlobalTransform, Option<&mut crate::skin::Animator>)>,
    mut anims: ResMut<crate::skin::AnimLibrary>,
    mut triggers: MessageWriter<crate::mission::SendTrigger>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut done: crate::levelreset::PerLevel<HashSet<Entity>>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut ends: MessageWriter<crate::ai::notes::ScriptEnd>,
    mut cues: MessageWriter<crate::voices::VoiceCue>,
) {
    let dt = time.delta_secs();
    let seen = done.get();
    for (e, mut ag, mut g, gt, animator) in &mut q {
        if seen.insert(e) {
            if let Some(tag) = ag.ladder {
                army.pending_ladders.retain(|(t, _)| *t != tag);
            }
        }
        // dead (its send_trigger_on_death: mission.rs DeathTrigger): a script it was running ends
        if g.mind == Mind::Dead {
            if ag.script.take().is_some() {
                if let Some(mut a) = animator {
                    a.stop(SCRIPT_SLOT);
                }
                g.fixed = None;
            }
            continue;
        }
        let pos = gt.translation();
        // a running script: the overlay anim, its notes; woken by a hit (no longer waiting) it ends
        if let Some(mut s) = ag.script.take() {
            let Some(anim) = anims.anim(&s.anim) else { continue };
            if g.mind != Mind::WaitTrigger {
                if let Some(mut a) = animator {
                    a.stop(SCRIPT_SLOT);
                }
                g.fixed = None;
                continue;
            }
            let t0 = s.t;
            s.t += dt;
            let mut gone: Option<bool> = None;
            for line in crate::mover::notes_between(&anim, t0, s.t) {
                let mut w = line.split_whitespace();
                match w.next().unwrap_or("").to_lowercase().as_str() {
                    "trigger" => {
                        if let Some(t) = w.next().and_then(|x| x.parse::<i64>().ok()) {
                            if t == 5000 {
                                army.stats.over_wall += 1;
                            }
                            triggers.write(crate::mission::SendTrigger(t));
                        }
                    }
                    "fx" => {
                        if let Some(n) = w.next() {
                            fx.write(crate::fx::StartFx { name: n.to_lowercase(), at: pos, rotation: gt.compute_transform().rotation, follow: None, position_only: false });
                        }
                    }
                    // over the wall: "deactivate" (0x00409110: the death bookkeeping, his death trigger, removed
                    // without a body) or "kill" (vt+0xd4: a death; mover_grunt_scale_2..4's notes past their end)
                    "deactivate" => gone = gone.or(Some(false)),
                    "kill" => gone = gone.or(Some(true)),
                    _ => {}
                }
            }
            if let Some(kill) = gone {
                info!("EVENT army: grunt {e:?} over the wall ({})", s.anim);
                ends.write(crate::ai::notes::ScriptEnd { entity: e, kill });
                continue;
            }
            if s.t >= anim.duration {
                if let Some(mut a) = animator {
                    a.stop(SCRIPT_SLOT);
                }
                g.fixed = None;
                if s.plant {
                    if let Some(tag) = ag.ladder {
                        army.planted.insert(tag);
                        info!("EVENT army: ladder {tag} planted");
                        // the planter's LadderPlaced (state 0xab, vt+0x170: voices.rs)
                        cues.write(crate::voices::VoiceCue { who: e, cue: crate::voices::Cue::LadderPlaced });
                    }
                }
                if let Some(run) = s.resume {
                    g.spline = Some(run);
                    g.mind = Mind::ToMarker;
                }
                continue;
            }
            ag.script = Some(s);
            continue;
        }
        // arriving at an ic_* marker of its spline (within the AI's arrival distance, 8): its ladder spline, or the
        // marker set's spline with the wall scales
        let (Some(run), Some(mk)) = (g.spline, markers.as_deref()) else { continue };
        let tag = run.tag;
        let Some(m) = mk.spline_marker(tag, run.index) else { continue };
        if m.flags & crate::alarm::Marker::IC == 0 || (m.pos - pos).with_y(0.0).length() >= 8.0 {
            continue;
        }
        let on_ladder = ag.ladder == Some(tag);
        let planted = army.planted.contains(&tag);
        let next = (run.index + 1 < mk.spline_len(tag)).then_some(SplineRun { tag, index: run.index + 1, started: true });
        // the interaction types of GruntObject / BowGruntObject vt+0x248 (0x0043bcf0 / 0x0043ab40): 0 plant, 1 climb,
        // 2 one of the four wall scales by (rand() << 2) >> 15 (their last note: trigger 5000 and deactivate)
        const SCALES: [&str; 4] = ["mover_grunt_scale", "mover_grunt_scale_2", "mover_grunt_scale_3", "mover_grunt_scale_4"];
        let script = match m.action.to_lowercase().as_str() {
            "ic_plant_ladder" if on_ladder && !planted => Some(("mover_grunt_ladder_plant", true)),
            "ic_climb_ladder" if on_ladder && planted => Some(("mover_grunt_ladder_climb", false)),
            "ic_scale_base_wall" => Some((SCALES[((rng.rand() * 4.0) as usize).min(3)], false)),
            _ => None,
        };
        match script {
            Some((anim, plant)) => {
                // placed at the marker (Arrival_PlaceAndAct: the marker's matrix), facing its direction, still
                g.spline = None;
                g.target = None;
                g.pending = None;
                g.trigger_los = false;
                g.mind = Mind::WaitTrigger;
                g.fixed = Some(m.pos);
                if m.face_dir {
                    g.yaw = m.dir.x.atan2(m.dir.z);
                }
                if let Some(mut a) = animator {
                    a.play(SCRIPT_SLOT, anim, false, crate::skin::Mask::All);
                }
                // arrived at his ladder or wall scale (states 0xaa / 0xac): the ClimbTaunt (vt+0x16c: voices.rs)
                if !plant {
                    cues.write(crate::voices::VoiceCue { who: e, cue: crate::voices::Cue::ClimbTaunt });
                }
                ag.script = Some(IcScript { anim: anim.into(), t: 0.0, resume: next, plant });
                info!("EVENT army: grunt {e:?} at {} on ladder {tag}: {anim}", m.action);
            }
            None => {
                // the ladder already stands (or none yet to climb): on along the spline
                if let Some(n) = next {
                    g.spline = Some(n);
                }
            }
        }
    }
}

/// The army breach (0x00429a50, from the AI's update while Game+0xcbe is set and fewer than army_breach_run_max have
/// gone): an army grunt drops what it does and runs the breach spline (AI_FollowMarkerSpline 0x0042d1b0 with
/// army_breach_marker_tag) through the open gate; its last marker sends 5000 (ai.rs, the marker's trigger) and its
/// "deactivate" flag (Marker_ProcessArrival 0x0044ba30: vt+0xd4) takes the grunt out of the game, here.
fn breach(
    mut commands: Commands,
    mut army: ResMut<Army>,
    mission: Option<Res<crate::mission::Mission>>,
    markers: Option<Res<crate::alarm::Markers>>,
    mut q: Query<(Entity, &mut ArmyGrunt, &mut Grunt, &GlobalTransform)>,
    mut cues: MessageWriter<crate::voices::VoiceCue>,
) {
    let (Some(mission), Some(mk)) = (mission, markers) else { return };
    let tag = army.breach_tag;
    let len = mk.spline_len(tag);
    if tag == 0 || len == 0 {
        return;
    }
    let end = mk.spline_marker(tag, len - 1).map(|m| m.pos);
    let mut sent = false;
    for (e, mut ag, mut g, gt) in &mut q {
        if g.mind == Mind::Dead {
            continue;
        }
        if ag.breach {
            // through: at the last marker the spline ends (ai.rs stands it there)
            if g.spline.is_none() && end.is_some_and(|p| (p - gt.translation()).with_y(0.0).length() < 10.0) {
                army.stats.over_wall += 1;
                commands.entity(e).despawn();
            }
            continue;
        }
        if sent || !mission.breach_open || army.breach_runners >= army.breach_max || ag.script.is_some() || ag.ladder.is_some() || g.mind == Mind::Flee {
            continue;
        }
        ag.breach = true;
        g.target = None;
        g.fixed = None;
        g.spline = Some(SplineRun { tag, index: 0, started: false });
        g.mind = Mind::ToMarker;
        army.breach_runners += 1;
        sent = true;
        // the runner's OpenGate (0x00429a50 -> vt+0x174: voices.rs)
        cues.write(crate::voices::VoiceCue { who: e, cue: crate::voices::Cue::OpenGate });
        info!("EVENT army: grunt {e:?} runs the breach ({} of {})", army.breach_runners, army.breach_max);
    }
}

/// The army in the playtest state log.
fn log(army: Res<Army>, state: Option<ResMut<crate::playtest::PlayState>>) {
    if !army.loaded || army.timers.is_empty() {
        return;
    }
    let Some(mut state) = state else { return };
    state.0.insert("army".into(), serde_json::json!({
        "platoons": army.stats.platoons,
        "marching": army.stats.marching,
        "members": army.stats.members,
        "released": army.stats.released,
        "live": army.stats.live,
        "fake_killed": army.stats.fake_killed,
        "over_wall": army.stats.over_wall,
        "limit": army.limit,
        "planted": army.planted.len(),
        "breach_runners": army.breach_runners,
        "stopped": army.stopped,
    }));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn props(v: serde_json::Value) -> crate::gamedata::Props {
        crate::gamedata::Props(v.as_object().cloned().unwrap_or_default())
    }

    #[test]
    fn entries_skip_zero_times_and_sort() {
        let p = props(serde_json::json!({ "create_platoon_1": "20/2/2", "create_platoon_2": "1/3/3", "create_platoon_3": "0.0/0/0", "create_platoon_4": "5/1/1" }));
        let e = entries(&p, "create_platoon_", 50);
        assert_eq!(e.iter().map(|x| x.time).collect::<Vec<_>>(), vec![1.0, 5.0, 20.0]);
        assert_eq!((e[0].a, e[0].b), (3, 3));
    }

    #[test]
    fn advance_fires_each_entry_once_and_ends_after_the_last() {
        let list = vec![Entry { time: 1.0, a: 0, b: 0 }, Entry { time: 2.0, a: 0, b: 0 }];
        let mut el = 0.0;
        let (f, end) = advance(&list, &mut el, 0.5);
        assert!(f.is_empty() && !end);
        let (f, end) = advance(&list, &mut el, 0.6);
        assert_eq!(f.len(), 1);
        assert!(!end);
        let (f, end) = advance(&list, &mut el, 1.0);
        assert_eq!(f.len(), 1);
        assert!(end);
    }
}
