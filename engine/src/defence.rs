//! The base turret of the defence levels (BaseDefendTurret, vtable 0x005a9f58 / 0x005a9bb0, behaviour base_turret;
//! base_turret_l5_m5 on lev_1_m4, lev_2_m4, lev_3_m4_bd, lev_4_m5, lev_5_m5). Roman starts in it (the mission's
//! start_in_turret_tag: MissionObject slot 8 0x0044d150 -> vt+0x178 mount 0x0049bcc0) and cannot leave (the Use
//! action skips the exit while Game+0x26cc is set, game_06.cpp:2224). The spec, with every address, is
//! docs/subsystems/defence.md. In short:
//! - Mount 0x0049bcc0: the shared mount 0x00498a80 (Roman hidden, his weapons put away), the wall with the turret's tag
//!   (base_defend_wall, its reference anim base_defend_wall_ref, the track node "ref_base_turret"), the position on the
//!   track +0x7a0 = frame 250 (the middle of 500), the intro anim (intro_anim_name, a camera fly-in; Game+0xdce while it
//!   plays).
//! - Update 0x0049b030: side movement along the wall (the strafe axis, side_accel 1000 / side_stop_accel 500 / clamped
//!   to side_max_speed frames per second, stopping at the track's ends; "wpn_base_turret_track_lp" above 5); the aim
//!   (turn/pitch_max_speed, zoom_* while zoomed, clamped to yaw/pitch_range; "wpn_base_turret_aim_lp"); the turret's
//!   matrix = wall * T(track node at the frame) * Rz(yaw) * Rx(pitch) (the node's rotation dropped); the zoom toggle;
//!   the crowd ambience every 10 s by the army's size; fire (button 8 held: the machine gun, recycle_time, alternating
//!   emitter_bone_name / emitter_bone_name_2, "base_turret_l5_m5_fire") and the mortar (button 9: 0x0049be70 ready after
//!   mortar_recycle_time, mortar_special_recycle_time with the ammo upgrade; 0x0049bf20 a ballistic shell to the
//!   crosshair point, Projectile_Launch 0x00480ec0).
//! - Damage 0x0049aeb0: the glass window (base_turret_l5_m5_window, 40 health) takes the hits while it lasts; then
//!   the player does (0x00498400), halved while mounted (player.rs).
//! - Overrun (the turret's "overrun" trigger, 0x0049bc90): every 45..75 s a grunt_regular comes for the turret
//!   (0x0049b030 -> 0x0042d8f0).
//!
//! Hooks outside this file: turret.rs (`Mounted` kept for a mount that is not a StigTurret), main.rs (plugin), and the
//! player / camera / hud / bot modules already follow `turret::Mounted`.

use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::prelude::*;
use bevy::transform::TransformSystems;

use crate::input::Act;
use crate::levelreset::LevelAppExt;

/// A camera node's keys from an anim's "cameras" table (model space, game axes): time, translation, rotation.
#[derive(Clone, Debug, Default)]
pub struct CameraTrack {
    keys: Vec<(f32, Vec3, Quat)>,
    pub duration: f32,
}

impl CameraTrack {
    /// The named camera of `anim` (anims/<anim>.json "cameras"), case-insensitive.
    pub fn load(dir: &std::path::Path, anim: &str, camera: &str) -> Option<Self> {
        let text = std::fs::read_to_string(dir.join("anims").join(format!("{}.json", anim.to_lowercase()))).ok()?;
        let v: serde_json::Value = serde_json::from_str(&text).ok()?;
        let cams = v["cameras"].as_object()?;
        let (_, c) = cams.iter().find(|(k, _)| k.eq_ignore_ascii_case(camera))?;
        let f = |x: &serde_json::Value| x.as_f64().unwrap_or(0.0) as f32;
        let ts = c["t"].as_array()?;
        let rs = c["r"].as_array()?;
        let mut keys = Vec::new();
        for (i, t) in ts.iter().enumerate() {
            let t = t.as_array()?;
            let r = rs.get(i.min(rs.len().saturating_sub(1)))?.as_array()?;
            keys.push((f(&t[0]), Vec3::new(f(&t[1]), f(&t[2]), f(&t[3])), Quat::from_xyzw(f(&r[1]), f(&r[2]), f(&r[3]), f(&r[4])).normalize()));
        }
        let duration = v["duration"].as_f64().unwrap_or(0.0) as f32;
        (!keys.is_empty()).then_some(Self { keys, duration })
    }

    /// The node's model-space matrix at `t` (held at the ends).
    pub fn sample(&self, t: f32) -> Mat4 {
        let i = self.keys.partition_point(|k| k.0 <= t);
        let (a, b) = (self.keys[i.saturating_sub(1)], self.keys[i.min(self.keys.len() - 1)]);
        let u = if b.0 > a.0 { ((t - a.0) / (b.0 - a.0)).clamp(0.0, 1.0) } else { 0.0 };
        Mat4::from_rotation_translation(a.2.slerp(b.2, u), a.1.lerp(b.1, u))
    }
}

/// One base turret's state and property values (0x0049a6a0).
#[derive(Component)]
pub struct BaseTurret {
    pub tag: i64,
    /// +0x620 (about z) and +0x61c (about x), radians
    pub yaw: f32,
    pub pitch: f32,
    turn_speed: (f32, f32),
    pitch_speed: (f32, f32),
    yaw_range: (f32, f32),
    pitch_range: (f32, f32),
    /// the wall's matrix (game space, no scale) and the track node's positions by time (wall reference anim)
    wall: Mat4,
    track_anim: String,
    track_frames: f32,
    /// +0x7a0 frame on the track, +0x7a4 side speed (frames / s); side_max_speed, side_accel, side_stop_accel
    pub frame: f32,
    side_speed: f32,
    side: (f32, f32, f32),
    /// skeleton bones: seat (player_bone_name), emitters (machine gun 1, 2; mortar), muzzle bones
    seat: Option<usize>,
    emitters: [Option<usize>; 3],
    muzzles: [Option<usize>; 3],
    /// byte +0x62c: which machine gun fires next
    left: bool,
    recycle: f32,
    mortar_recycle: (f32, f32),
    projectile: String,
    mortar: (String, String),
    muzzle_fx: Option<String>,
    mortar_fx: Option<String>,
    last_fire: Option<f32>,
    /// +0x640: the last mortar
    last_mortar: Option<f32>,
    /// +0x634: the ammo upgrade's time left (ammo_upgrade_duration +0x630)
    pub upgrade: f32,
    upgrade_duration: f32,
    /// zoom (camera_zoom node)
    pub zoomed: bool,
    cameras: (Mat4, Option<Mat4>),
    /// the intro (+0x62d): its anim and camera, the time into it
    intro: Option<(String, Option<CameraTrack>, f32)>,
    /// the glass window's model (base_turret_l5_m5_window, attached at ref_window) and its health: the
    /// fx::DamageShield put on the player while mounted
    window: Option<Entity>,
    window_health: f32,
    /// intro_anim_name (+0x644, default base_turret_enter)
    intro_anim: String,
    /// the overrun trigger +0x834, set +0x62f, and the next overrun grunt (+0x838)
    overrun_trigger: i64,
    pub overrun: bool,
    overrun_next: f32,
    /// the held machine-gun fx (rapid fire: 0x00497fd0 / 0x00497ed0) and its flags 0x40 / 0x80
    fx_anchor: Option<Entity>,
    fx_flags: u8,
    sounds: (bool, bool),
    /// the crowd ambience (+0x840 level, +0x844 countdown)
    crowd: (i32, f32),
}

impl BaseTurret {
    /// The mount's intro (intro_anim_name) is playing: a cinematic (Game byte +0xdce while it plays), which holds the
    /// mission's start text (MissionObject_ShowStart 0x0044daf0).
    pub fn in_intro(&self) -> bool {
        self.intro.is_some()
    }
}

#[derive(Resource, Default)]
struct DefenceState {
    /// start_in_turret_tag (Game+0x26cc) and whether the start mount was done
    start_tag: i64,
    started: bool,
    /// the counter of the mission's grunt_wall_counter_tag (Game+0x26fc): the HUD's grunts over the wall
    wall_counter: Option<u32>,
}

/// A playtest's debug command (playtest.rs `defence`): {"aim": "army"} keeps the turret on the nearest army grunt
/// (else marching platoon), {"aim": false} stops; {"mortar": "platoon"} fires a shell at the nearest platoon; {"frame":
/// F} puts the turret at that track frame.
#[derive(Message, Clone, Debug)]
pub struct DefenceCommand(pub serde_json::Value);

#[derive(Resource, Default)]
struct DefenceDebug {
    aim_army: bool,
}

pub struct DefencePlugin;

impl Plugin for DefencePlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<DefenceState>()
            .init_level_resource::<DefenceDebug>()
            .add_message::<DefenceCommand>()
            .add_systems(Update, (setup, start, debug_commands, (ride, overrun, log).chain().after(crate::turret::ride).after(crate::hud_state)).chain().run_if(crate::playing))
            .add_systems(PostUpdate, pose.after(crate::skin::AnimateSet).before(TransformSystems::Propagate));
    }
}

fn read_json(path: std::path::PathBuf) -> Option<serde_json::Value> {
    std::fs::read_to_string(path).ok().and_then(|s| serde_json::from_str(&s).ok())
}

/// The camera node of the turret's reference anim (camera_bone_name / zoom_camera_bone_name).
fn reference_camera(dir: &std::path::Path, model: &str, name: &str) -> Option<Mat4> {
    let reference = read_json(dir.join("skeletons").join(format!("{model}.json")))?["reference_anim"].as_str()?.to_owned();
    CameraTrack::load(dir, &reference, name).map(|c| c.sample(0.0))
}

/// Base-turret components for the placed turrets (once each), with their wall.
#[allow(clippy::too_many_arguments)]
fn setup(
    mut commands: Commands,
    mut state: ResMut<DefenceState>,
    launch: Res<crate::Launch>,
    data: Res<crate::gamedata::GameData>,
    q: Query<(Entity, &crate::level::InstanceId, &crate::skin::Animator, Option<&crate::level::Placed>), Without<BaseTurret>>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mission: Option<Res<crate::mission::Mission>>,
    mut done: crate::levelreset::PerLevel<(Option<crate::scene::SceneFile>, std::collections::HashSet<Entity>)>,
    mut kit: crate::army::ModelKit,
) {
    let (scene, seen) = done.get();
    if mission.is_none() || q.iter().all(|(e, ..)| seen.contains(&e)) {
        return;
    }
    let scene = scene.get_or_insert_with(|| crate::scene::read_scene(&launch.scene));
    let dir = kit.dir();
    if state.start_tag == 0 {
        if let Some(m) = scene.logic.iter().find(|l| l.template == "mission") {
            state.start_tag = m.props.get("start_in_turret_tag").and_then(|v| v.as_i64()).unwrap_or(0);
            let tag = m.props.get("grunt_wall_counter_tag").and_then(|v| v.as_i64()).unwrap_or(0);
            if tag != 0 {
                state.wall_counter = scene
                    .instances
                    .iter()
                    .find(|i| {
                        let p = data.instance_props(&i.template, &i.props);
                        i.template == "counter" && p.i("tag", 0) == tag
                    })
                    .map(|i| i.id);
            }
        }
    }
    for (e, id, a, placed) in &q {
        if !seen.insert(e) {
            continue;
        }
        let Some(inst) = scene.instances.iter().find(|i| i.id == id.0) else { continue };
        let p = data.instance_props(&inst.template, &inst.props);
        if p.s("behavior") != Some("base_turret") {
            continue;
        }
        let tag = p.i("tag", 0);
        // the wall: the base_defend_wall object of the same tag (0x004592e0), else any
        let walls: Vec<_> = scene.instances.iter().filter(|w| data.templates.get(&w.template).is_some_and(|t| t.property == "p_base_defend_wall")).collect();
        let Some(wall) = walls.iter().find(|w| data.instance_props(&w.template, &w.props).i("tag", 0) == tag).or(walls.first()) else {
            warn!("base turret {}: no wall", id.0);
            continue;
        };
        let (_, wr, wt) = crate::scene::game_matrix(&wall.transform).to_scale_rotation_translation();
        let track_anim = data.templates.get(&wall.template).and_then(|t| t.default_anim.clone()).unwrap_or_else(|| "base_defend_wall_ref".into());
        let track_frames = read_json(dir.join("anims").join(format!("{track_anim}.json"))).and_then(|v| v["duration"].as_f64()).unwrap_or(500.0 / 30.0) as f32 * 30.0;
        let bone = |k: &str| p.s(k).and_then(|n| a.skeleton.index.get(&n.to_lowercase()).copied());
        let model = &inst.model;
        let normal = reference_camera(&dir, model, p.s("camera_bone_name").unwrap_or("camera_normal")).unwrap_or(Mat4::from_translation(Vec3::new(0.0, -2.0, 13.0)));
        let zoom = p.s("zoom_camera_bone_name").and_then(|n| reference_camera(&dir, model, n));
        let deg = |k: &str, d: f32| p.f(k, d).to_radians();
        // the turret moves along the wall: its placed collider would stay behind (removed; the player takes the hits)
        if let Some(c) = placed.and_then(|pl| pl.collider) {
            physics.remove_static(c);
        }
        // the glass window at ref_window (0x0049a6a0: base_turret_l5_m5_window attached)
        let window_model = data.templates.get("base_turret_l5_m5_window").and_then(|t| t.model.clone());
        let window_at = a.skeleton.index.get("ref_window").map(|&i| a.skeleton.rest[i].translation);
        let window = match (window_model, window_at) {
            (Some(m), Some(at)) => kit.spawn(&mut commands, &m, Transform::from_translation(at), false),
            _ => None,
        };
        if let Some(w) = window {
            commands.entity(e).add_child(w);
        }
        commands.entity(e).insert(BaseTurret {
            tag,
            yaw: 0.0,
            pitch: 0.0,
            turn_speed: (p.f("turn_max_speed", 2.0), p.f("zoom_turn_max_speed", 0.025)),
            pitch_speed: (p.f("pitch_max_speed", 2.0), p.f("zoom_pitch_max_speed", 0.1)),
            yaw_range: (deg("yaw_range_min", -45.0), deg("yaw_range_max", 45.0)),
            pitch_range: (deg("pitch_range_min", -45.0), deg("pitch_range_max", 45.0)),
            wall: Mat4::from_rotation_translation(wr, wt),
            track_anim,
            track_frames,
            frame: 250.0,
            side_speed: 0.0,
            side: (p.f("side_max_speed", 400.0), p.f("side_accel", 1000.0), p.f("side_stop_accel", 500.0)),
            seat: bone("player_bone_name"),
            emitters: [bone("emitter_bone_name"), bone("emitter_bone_name_2"), bone("mortar_emitter_bone_name")],
            muzzles: [bone("muzzle_bone_name"), bone("muzzle_bone_name_2"), bone("mortar_muzzle_bone_name")],
            left: true,
            recycle: p.f("recycle_time", 1.0),
            mortar_recycle: (p.f("mortar_recycle_time", 4.0), p.f("mortar_special_recycle_time", 2.0)),
            projectile: format!("p_{}", p.s("projectile_name").unwrap_or("base_turret_mgun_bullet")),
            mortar: (p.s("mortar_projectile_name").unwrap_or("base_mortar").to_owned(), p.s("mortar_special_projectile_name").unwrap_or("base_homing").to_owned()),
            muzzle_fx: p.s("muzzle_fx_name").map(str::to_lowercase),
            mortar_fx: p.s("mortar_muzzle_fx_name").map(str::to_lowercase),
            last_fire: None,
            last_mortar: None,
            upgrade: 0.0,
            upgrade_duration: p.f("ammo_upgrade_duration", 10.0),
            zoomed: false,
            cameras: (normal, zoom),
            intro: None,
            window,
            window_health: data.props("p_base_turret_l5_m5_window").f("max_health", 40.0),
            intro_anim: p.s("intro_anim_name").unwrap_or("base_turret_enter").to_lowercase(),
            overrun_trigger: p.i("overrun", 0),
            overrun: false,
            overrun_next: 0.0,
            fx_anchor: None,
            fx_flags: 0,
            sounds: (false, false),
            crowd: (-1, 0.0),
        });
        info!("EVENT defence: base turret {} (tag {tag}) on wall {} track {}", id.0, wall.id, data.templates.get(&wall.template).and_then(|t| t.default_anim.clone()).unwrap_or_default());
    }
}

/// The level's start in the turret (0x0044d150 -> mount 0x0049bcc0).
#[allow(clippy::too_many_arguments)]
fn start(
    mut commands: Commands,
    mut state: ResMut<DefenceState>,
    mut mounted: ResMut<crate::turret::Mounted>,
    mut turrets: Query<(Entity, &mut BaseTurret, &mut crate::skin::Animator)>,
    mut roman: Query<(Entity, &mut crate::player::Roman, &mut Visibility)>,
    lib: Res<crate::skin::AnimLibrary>,
) {
    if state.started || state.start_tag == 0 {
        return;
    }
    let Some((e, mut t, mut a)) = turrets.iter_mut().find(|(_, t, _)| t.tag == state.start_tag) else { return };
    let Ok((re, mut r, mut vis)) = roman.single_mut() else { return };
    state.started = true;
    mounted.0 = Some(e);
    *vis = Visibility::Hidden;
    r.vel = Vec3::ZERO;
    r.zoom = 0.0;
    // the window in front of him takes the hits first (0x0049aeb0)
    if t.window.is_some() {
        commands.entity(re).insert(crate::fx::DamageShield { health: t.window_health });
    }
    // the intro (intro_anim_name) with its camera Camera_normal
    let intro = t.intro_anim.clone();
    let cam = CameraTrack::load(&lib.dir, &intro, "camera_normal");
    a.play(0, &intro, false, crate::skin::Mask::All);
    t.intro = Some((intro.clone(), cam, 0.0));
    // silent: the base turret's mount 0x0049bcc0 plays no sound (the stig gun's 0x00498f60 plays wpn_stig_mount;
    // the oracle's lev_1_m4_start: the original mounts without it)
    info!("EVENT defence: Roman starts in the base turret (tag {}), intro {intro}", t.tag);
}

/// The playtest's defence commands (DefenceCommand) and the debug aim, before the turret's update.
#[allow(clippy::too_many_arguments)]
fn debug_commands(
    mut commands: Commands,
    mut cmds: MessageReader<DefenceCommand>,
    mut dbg: ResMut<DefenceDebug>,
    mounted: Res<crate::turret::Mounted>,
    mut turrets: Query<(&mut BaseTurret, &GlobalTransform, &crate::skin::Animator)>,
    platoons: Query<(&crate::army::Platoon, &crate::mover::Mover)>,
    grunts: Query<(&crate::ai::Grunt, &GlobalTransform), With<crate::army::ArmyGrunt>>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    let Some(me) = mounted.0 else {
        cmds.clear();
        return;
    };
    let Ok((mut t, gt, a)) = turrets.get_mut(me) else { return };
    let from = gt.translation();
    let nearest_platoon = || {
        platoons.iter().flat_map(|(p, m)| p.member_points(m)).min_by(|x, y| x.distance(from).total_cmp(&y.distance(from)))
    };
    for c in cmds.read() {
        let v = &c.0;
        if let Some(aim) = v.get("aim") {
            dbg.aim_army = aim.as_str() == Some("army");
        }
        if let Some(f) = v.get("frame").and_then(|f| f.as_f64()) {
            t.frame = f as f32;
        }
        if v.get("mortar").is_some() {
            if let (Some(target), Some(em)) = (nearest_platoon(), t.emitters[2]) {
                let start = (gt.to_matrix() * bone_model(&a.skeleton, em)).w_axis.truncate();
                let template = t.mortar.0.clone();
                crate::shell::launch(&mut commands, &data, &mut kit, &mut fx, &template, start, target, crate::shell::Lob { by: crate::fx::Src::Player, flight: None });
                info!("EVENT defence: debug mortar at the nearest platoon, {:.0} away", (target - start).length());
            }
        }
    }
    if dbg.aim_army {
        // the nearest live army grunt, else the nearest marching member; aimed through the turret's centre (the camera
        // sits on its axis)
        let target = grunts
            .iter()
            .filter(|(g, _)| g.mind != crate::ai::Mind::Dead)
            .map(|(_, g)| g.translation() + Vec3::Y * 6.0)
            .min_by(|x, y| x.distance(from).total_cmp(&y.distance(from)))
            .or_else(nearest_platoon);
        if let Some(target) = target {
            // engine -> game: (x, y, z) -> (x, -z, y); then into the wall's frame
            let d = target - (from + gt.rotation() * Vec3::Z * 10.0);
            let dg = Vec3::new(d.x, -d.z, d.y);
            let local = t.wall.inverse().transform_vector3(dg).normalize_or(Vec3::NEG_Y);
            t.pitch = clamp(-local.z.asin(), t.pitch_range);
            t.yaw = clamp(local.x.atan2(-local.y), t.yaw_range);
        }
    }
}

/// The turret's matrix (game space): wall * T(track node at the frame) * Rz(yaw) * Rx(pitch) (0x0049b030).
fn turret_world(t: &BaseTurret, track: Option<&crate::skin::Anim>) -> Mat4 {
    let node = track.and_then(|a| a.translation("ref_base_turret", t.frame / 30.0)).unwrap_or(Vec3::ZERO);
    t.wall * Mat4::from_translation(node) * Mat4::from_rotation_z(t.yaw) * Mat4::from_rotation_x(t.pitch)
}

/// A bone's model-space matrix (rest pose, game axes).
fn bone_model(sk: &crate::skin::Skeleton, i: usize) -> Mat4 {
    let mut m = Mat4::IDENTITY;
    let mut j = i as i32;
    while j >= 0 {
        m = sk.rest[j as usize].to_matrix() * m;
        j = sk.parents[j as usize];
    }
    m
}

/// The clamp of a turret angle, skipped when either limit is 0 (0x004990a0).
fn clamp(v: f32, (lo, hi): (f32, f32)) -> f32 {
    if lo == 0.0 || hi == 0.0 { v } else { v.clamp(lo, hi) }
}

/// The mounted turret's update (0x0049b030), the window's shield and the start-in-turret hold on Roman.
#[allow(clippy::too_many_arguments)]
fn ride(
    mut commands: Commands,
    time: Res<Time>,
    (ctl, motion, look, playtest, opts): (Res<crate::input::Controls>, Res<AccumulatedMouseMotion>, Res<crate::player::LookDelta>, Option<Res<crate::playtest::Playtest>>, Res<crate::options::GameOptions>),
    mounted: Res<crate::turret::Mounted>,
    mut turrets: Query<(Entity, &mut BaseTurret, &mut crate::skin::Animator, &mut Transform)>,
    mut roman: Query<(Entity, &mut crate::player::Roman, &mut Transform, &crate::fx::Health, &mut Visibility, Option<&crate::fx::DamageShield>), Without<BaseTurret>>,
    (mut physics, data, xhair): (NonSendMut<crate::physics::Physics>, Res<crate::gamedata::GameData>, Res<crate::hud::Crosshair>),
    (mut sounds, mut stop, mut fx, mut triggers): (MessageWriter<crate::audio::PlaySound>, MessageWriter<crate::audio::StopSound>, MessageWriter<crate::fx::StartFx>, MessageWriter<crate::mission::SendTrigger>),
    (mut stats, mut hud, army): (ResMut<crate::player::PlayerStats>, ResMut<crate::hud::HudState>, Res<crate::army::Army>),
    (joints, mut upgrades): (Query<&GlobalTransform>, MessageReader<crate::icobject::TurretUpgrade>),
    mut kit: crate::army::ModelKit,
) {
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let Some(me) = mounted.0 else { return };
    let Ok((te, mut t, mut a, mut tt)) = turrets.get_mut(me) else { return };
    let Ok((re, mut r, mut rt, health, mut vis, shield)) = roman.single_mut() else { return };
    let track = kit.anim(&t.track_anim);
    let skeleton = a.skeleton.clone();
    let to_engine = Mat4::from_quat(crate::level::game_to_engine());
    // the window shot out (its shield used up): it bursts (fx_death of p_base_turret_l5_m5_window)
    if let (Some(w), Some(s)) = (t.window, shield) {
        if s.health <= 0.0 {
            let at = joints.get(w).map(|g| g.translation()).unwrap_or(tt.translation);
            fx.write(crate::fx::StartFx { name: "fx_explosion_turret_window".into(), at, rotation: Quat::IDENTITY, follow: None, position_only: false });
            commands.entity(w).despawn();
            commands.entity(re).remove::<crate::fx::DamageShield>();
            t.window = None;
            info!("EVENT defence: the turret's window is shot out");
        }
    }
    let dead = r.dead || health.0 <= 0.0;
    // the intro: the turret's anim plays, the camera follows its node; no control until it ends
    let mut intro_on = false;
    if let Some((name, _, ti)) = t.intro.as_mut() {
        let t0 = *ti;
        *ti += dt;
        let ti = *ti;
        let name = name.clone();
        if let Some(anim) = kit.anim(&name) {
            for line in crate::mover::notes_between(&anim, t0, ti) {
                let mut w = line.split_whitespace();
                match w.next().unwrap_or("").to_lowercase().as_str() {
                    "trigger" => {
                        if let Some(n) = w.next().and_then(|x| x.parse::<i64>().ok()) {
                            triggers.write(crate::mission::SendTrigger(n));
                        }
                    }
                    "fx" => {
                        if let Some(n) = w.next() {
                            fx.write(crate::fx::StartFx { name: n.to_lowercase(), at: tt.translation, rotation: tt.rotation, follow: Some(te), position_only: false });
                        }
                    }
                    _ => {}
                }
            }
            if ti >= anim.duration {
                t.intro = None;
                a.play(0, "base_turret_l5_m5_ref", true, crate::skin::Mask::All);
                info!("EVENT defence: intro over");
            } else {
                intro_on = true;
            }
        } else {
            t.intro = None;
        }
    }
    hud.cinematic |= intro_on;
    // the muzzle fx step (0x00498710 -> 0x00497ed0)
    if t.fx_flags & 0x40 != 0 {
        t.fx_flags &= !0x40;
    } else if t.fx_flags & 0x80 == 0 {
        t.fx_flags |= 0x80;
    } else if let Some(anchor) = t.fx_anchor.take() {
        commands.entity(anchor).despawn();
    }
    let can = !intro_on && !dead;
    // side movement along the wall (input axes 2 / 3)
    let side_in = if can { ctl.axis(Act::Move).x } else { 0.0 };
    let (smax, sacc, sstop) = t.side;
    if side_in.abs() > 0.1 {
        t.side_speed = (t.side_speed + side_in.signum() * sacc * dt).clamp(-smax, smax);
    } else {
        let s = t.side_speed;
        t.side_speed = if s > 0.0 { (s - sstop * dt).max(0.0) } else { (s + sstop * dt).min(0.0) };
    }
    let frames = t.track_frames;
    t.frame += t.side_speed * dt;
    if t.frame <= 0.0 || t.frame >= frames {
        t.frame = t.frame.clamp(0.0, frames);
        t.side_speed = 0.0;
    }
    let tracking = t.side_speed.abs() > 5.0;
    if tracking != t.sounds.0 {
        t.sounds.0 = tracking;
        if tracking {
            sounds.write(crate::audio::PlaySound { looped: Some(true), follow: Some(te), ..crate::audio::PlaySound::at("wpn_base_turret_track_lp", tt.translation) });
        } else {
            stop.write(crate::audio::StopSound("wpn_base_turret_track_lp".into()));
        }
    }
    // aim: the mouse at the player's rates scaled to the turret's speeds (as turret.rs); keys and stick in rad/s
    if can {
        let d = if playtest.is_some() { motion.delta } else { look.0 };
        let k = opts.mouse_sensitivity + 0.5;
        let inv = if opts.invert_mouse { -1.0 } else { 1.0 };
        let stick = ctl.axis(Act::Look);
        let (ts, ps) = if t.zoomed { (t.turn_speed.1, t.pitch_speed.1) } else { (t.turn_speed.0, t.pitch_speed.0) };
        let (y0, p0) = (t.yaw, t.pitch);
        let yaw = t.yaw - d.x * 0.0100 / 3.0 * ts * k - stick.x * ts * dt;
        let pitch = t.pitch + d.y * 0.0057 / 0.95 * ps * k * inv - stick.y * ps * dt * inv;
        t.yaw = clamp(yaw, t.yaw_range);
        t.pitch = clamp(pitch, t.pitch_range);
        let aiming = (t.yaw - y0).abs() > 0.0 || (t.pitch - p0).abs() > 0.0;
        if aiming != t.sounds.1 {
            t.sounds.1 = aiming;
            if aiming {
                sounds.write(crate::audio::PlaySound { looped: Some(true), follow: Some(te), ..crate::audio::PlaySound::at("wpn_base_turret_aim_lp", tt.translation) });
            } else {
                stop.write(crate::audio::StopSound("wpn_base_turret_aim_lp".into()));
            }
        }
        // the zoom toggle (button 10)
        if ctl.pressed(Act::Aim) && t.cameras.1.is_some() {
            t.zoomed = !t.zoomed;
        }
    }
    // the turret's matrix
    let world = turret_world(&t, track.as_deref());
    *tt = crate::mover::engine_transform(world);
    let engine = to_engine * world;
    let bone_world = |i: usize| engine * bone_model(&skeleton, i);
    // Roman held at the seat (Object_Attach at player_bone_name), hidden
    let seat = t.seat.map(|i| bone_world(i).w_axis.truncate()).unwrap_or(tt.translation);
    physics.place_walker(&r.walker, seat);
    rt.translation = seat;
    // (the player update skips a mounted Roman: his position for the HUD / state log is the seat's)
    stats.position = seat;
    r.vel = Vec3::ZERO;
    r.falling = false;
    r.fall_fast_t = 0.0;
    r.grace = 0.5;
    *vis = Visibility::Hidden;
    // the ammo upgrade (Game+0xdd1, a shot turret_upgrade: icobject.rs) starts, counts down
    if upgrades.read().count() > 0 {
        t.upgrade = t.upgrade_duration;
        info!("EVENT defence: ammo upgrade for {} s", t.upgrade_duration);
    }
    t.upgrade = (t.upgrade - dt).max(0.0);
    // the crowd ambience every 10 s by the army's live grunts (<15, <30, <60, more)
    t.crowd.1 -= dt;
    if t.crowd.1 <= 0.0 {
        t.crowd.1 = 10.0;
        let n = army.stats.live + army.stats.members;
        let level = if n < 15 { 0 } else if n < 30 { 1 } else if n < 60 { 2 } else { 3 };
        if level != t.crowd.0 {
            let names = ["", "amb_base_crowd_small", "amb_base_crowd_med", "amb_base_crowd_large"];
            if t.crowd.0 > 0 {
                stop.write(crate::audio::StopSound(names[t.crowd.0 as usize].into()));
            }
            if level > 0 {
                sounds.write(crate::audio::PlaySound { looped: Some(true), ..crate::audio::PlaySound::ui(names[level as usize]) });
            }
            t.crowd.0 = level;
        }
    }
    // fire: the machine gun (button 8 held), the mortar (button 9)
    if can && (ctl.down(Act::Fire) || ctl.pressed(Act::Fire)) {
        t.fx_flags = (t.fx_flags | 0x40) & !0x80;
        a.play(0, "base_turret_l5_m5_fire", true, crate::skin::Mask::All);
        if t.last_fire.is_none_or(|l| now - l > t.recycle) {
            let gun = if t.left { 0 } else { 1 };
            if let Some(em) = t.emitters[gun].or(t.emitters[0]) {
                let m = bone_world(em);
                let from = m.w_axis.truncate();
                let fwd = engine.transform_vector3(Vec3::NEG_Y).normalize_or(Vec3::NEG_Z);
                let dir = xhair.point.map(|q| (q - from).normalize_or(fwd)).filter(|d| d.dot(fwd) > 0.0).unwrap_or(fwd);
                let bullet = data.props(&t.projectile);
                crate::player::spawn_projectile(&mut commands, &mut fx, &bullet, from, dir, true, Some(re));
                stats.shots += 1;
            }
            t.left = !t.left;
            t.last_fire = Some(now);
            if t.fx_anchor.is_none() {
                if let (Some(name), Some(&joint)) = (t.muzzle_fx.clone(), t.muzzles[0].or(t.emitters[0]).and_then(|i| a.joints.get(i))) {
                    let anchor = commands.spawn((Transform::IDENTITY, Visibility::default())).id();
                    commands.entity(joint).add_child(anchor);
                    let jm = joints.get(joint).map(|j| j.compute_transform()).unwrap_or_default();
                    fx.write(crate::fx::StartFx { name, at: jm.translation, rotation: jm.rotation, follow: Some(anchor), position_only: false });
                    t.fx_anchor = Some(anchor);
                }
            }
        }
    } else if t.intro.is_none() && a.layers.first().is_some_and(|l| l.anim == "base_turret_l5_m5_fire") {
        a.play(0, "base_turret_l5_m5_ref", true, crate::skin::Mask::All);
    }
    if can && ctl.pressed(Act::Special) {
        // ready (0x0049be70): mortar_special_recycle_time while the upgrade lasts, else mortar_recycle_time
        let wait = if t.upgrade > 0.0 { t.mortar_recycle.1 } else { t.mortar_recycle.0 };
        if t.last_mortar.is_none_or(|l| now - l > wait) {
            if let Some(em) = t.emitters[2] {
                let from = bone_world(em).w_axis.truncate();
                let fwd = engine.transform_vector3(Vec3::NEG_Y);
                let target = xhair.point.unwrap_or(from + fwd * 500.0);
                let template = if t.upgrade > 0.0 { t.mortar.1.clone() } else { t.mortar.0.clone() };
                crate::shell::launch(&mut commands, &data, &mut kit, &mut fx, &template, from, target, crate::shell::Lob { by: crate::fx::Src::Player, flight: None });
                if let Some(name) = t.mortar_fx.clone() {
                    let (_, rot, at) = bone_world(t.muzzles[2].unwrap_or(em)).to_scale_rotation_translation();
                    fx.write(crate::fx::StartFx { name, at, rotation: rot, follow: None, position_only: false });
                }
                stats.shots += 1;
                t.last_mortar = Some(now);
                info!("EVENT defence: mortar {template} at {:.0}", (target - from).length());
            }
        }
    }
}

/// The turret's overrun (its "overrun" trigger, 0x0049bc90): every 45..75 s a grunt_regular comes for it (0x0042d8f0
/// mounts him on the turret with grunt_turret_jump_on; here he is spawned behind the turret and charges: guess).
fn overrun(
    time: Res<Time>,
    mut events: MessageReader<crate::mission::SendTrigger>,
    mut q: Query<(&mut BaseTurret, &GlobalTransform)>,
    mounted: Res<crate::turret::Mounted>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let ids: Vec<i64> = events.read().map(|e| e.0).collect();
    let now = time.elapsed_secs();
    for (mut t, gt) in &mut q {
        if ids.iter().any(|&i| i != 0 && i == t.overrun_trigger) && !t.overrun {
            t.overrun = true;
            t.overrun_next = now;
            info!("EVENT defence: the base is overrun");
        }
        if !t.overrun || mounted.0.is_none() || now < t.overrun_next {
            continue;
        }
        t.overrun_next = now + rng.range(45.0, 75.0);
        let back = gt.rotation().mul_vec3(Vec3::Z).normalize_or(Vec3::Z) * 25.0;
        spawn.write(crate::level::SpawnGrunt { at: gt.translation() + back, template: "grunt_regular".into(), ..default() });
    }
}

/// The camera at the turret's camera node (normal, zoom or the intro's), set after the anims.
fn pose(
    mounted: Res<crate::turret::Mounted>,
    turrets: Query<(&BaseTurret, &GlobalTransform)>,
    mut cam: Query<&mut Transform, With<crate::player::FollowCam>>,
) {
    let Some(me) = mounted.0 else { return };
    let Ok((t, gt)) = turrets.get(me) else { return };
    let node = match &t.intro {
        Some((_, Some(c), ti)) => c.sample(*ti),
        _ => match (t.zoomed, t.cameras.1) {
            (true, Some(z)) => z,
            _ => t.cameras.0,
        },
    };
    if let Ok(mut c) = cam.single_mut() {
        let (_, rot, pos) = (gt.to_matrix() * node).to_scale_rotation_translation();
        c.translation = pos;
        c.rotation = rot;
    }
}

/// The turret in the playtest state log; the HUD's grunts over the wall.
fn log(
    mounted: Res<crate::turret::Mounted>,
    q: Query<(Entity, &BaseTurret, &GlobalTransform)>,
    state: Option<ResMut<crate::playtest::PlayState>>,
    defence: Res<DefenceState>,
    mission: Option<Res<crate::mission::Mission>>,
    mut hud: ResMut<crate::hud::HudState>,
) {
    hud.over_wall = defence.wall_counter.and_then(|id| mission.as_ref()?.counter(id));
    let Some(mut state) = state else { return };
    let Some((e, t, gt)) = q.iter().next() else { return };
    let p = gt.translation();
    state.0.insert("base_turret".into(), serde_json::json!({
        "mounted": mounted.0 == Some(e),
        "intro": t.intro.is_some(),
        "frame": t.frame,
        "yaw": t.yaw.to_degrees(),
        "pitch": t.pitch.to_degrees(),
        "zoomed": t.zoomed,
        "window": t.window.is_some(),
        "overrun": t.overrun,
        "upgrade": t.upgrade,
        "pos": [p.x, -p.z, p.y],
        "over_wall": hud.over_wall.map(|c| c.0),
    }));
}
