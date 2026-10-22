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
//! - Overrun (the turret's "overrun" trigger, 0x0049bc90): every 45..75 s a grunt_regular jumps onto the turret
//!   (0x0049b030 -> 0x0042d8f0: grunt_turret_jump_on / _1 alternately) and is gone when the anim ends (state 0x67).
//! - The failure mob (0x0049ad60, from MissionObject_OnTrigger 0x0044d600): a failure trigger while Roman is in the
//!   base turret (Game+0xac8 = 1) does not fail at once: six grunt_regulars board the turret (0x0042d780:
//!   grunt_turret_attack, _1 .. _5 by a running count), Game+0xdcd is set (the success trigger is ignored meanwhile),
//!   and failure_mode_duration (+0x830, 5 s) later the turret is removed (vt+0xdc 0x0049ac60): level state 3.
//!
//! Hooks outside this file: turret.rs (`Mounted` kept for a mount that is not a StigTurret), main.rs (plugin), army.rs
//! (`ArmyGrunt::in_script`: the grunts the turret runs over), and the player / camera / hud / bot modules already follow
//! `turret::Mounted`.

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
    /// the machine gun's muzzle (muzzle_bone_name +0x500) in the world: the turret's vt+0x148 (0x00497f60), the HUD
    /// passes' eye while mounted (0x004067e0; weapons/targeting.rs)
    pub eye: Option<Vec3>,
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
    /// failure_mode_duration (+0x830): the failure mob's time left before the turret is removed
    failure_left: f32,
    /// the held machine-gun fx (rapid fire: 0x00497fd0 / 0x00497ed0) and its flags 0x40 / 0x80
    fx_anchor: Option<Entity>,
    fx_flags: u8,
    sounds: (bool, bool),
    /// the crowd ambience (+0x840 level, +0x844 countdown)
    crowd: (i32, f32),
    /// the turret's helpers (0x0049a6a0): the fake mortar launcher +0x808 (fake_base_mortar, shown) and the fake homing
    /// launcher +0x80c (fake_base_homing, hidden), attached at its origin, and the trolley +0x804
    /// (base_turret_trolley) that rides the track without the aim; `launchers` draws them
    launchers: Option<(Entity, Entity)>,
    trolley: Option<Entity>,
    /// the homing launcher shown instead of the mortar (the ammo upgrade's swap, 0x0049b030 / 0x0049bf20)
    pub homing_shown: bool,
    /// a reload anim to play on a launcher (true: the homing launcher) after a shell (0x0049bf20)
    reload: Option<bool>,
    /// the trolley's matrix (game space): wall * T(track node), no aim (0x0049b030: base * +0x7a8)
    trolley_world: Mat4,
    /// min_non_locking (+0x820, 0) and max_non_locking (+0x81c, 5): how many machine-gun rounds go to the crosshair
    /// point while lock-on candidates are in view before the gun locks onto one of them; the count left (+0x818)
    non_locking: (i32, i32),
    lock_countdown: i32,
    /// the targets' positions last frame and their velocities (0x00458000), for the machine gun's lead (0x004580a0)
    target_motion: crate::ai::aim::TargetMotion,
    /// the window shield's health last seen: a drop is a glass hit (0x0049aeb0: fx_glass_bullet_hole)
    shield_seen: Option<f32>,
    /// the glass hits so far (the playtest log)
    glass_hits: u32,
    /// the fake launchers' reload anims set at load (0x0049a6a0) are still to play
    load_reloads: bool,
    /// removed (vt+0xdc 0x0049ac60): the helpers released, the player dismounted
    removed: bool,
}

impl DefenceState {
    /// The level starts Roman in a turret (0x0044e600: start_in_turret_tag, Game+0x26cc, set).
    pub(crate) fn starts_in_turret(&self) -> bool {
        self.start_tag != 0
    }
}

impl BaseTurret {
    /// The mount's intro (intro_anim_name) is playing: a cinematic (Game byte +0xdce while it plays), which holds the
    /// mission's start text (MissionObject_ShowStart 0x0044daf0).
    pub fn in_intro(&self) -> bool {
        self.intro.is_some()
    }
}

#[derive(Resource, Default)]
pub(crate) struct DefenceState {
    /// start_in_turret_tag (Game+0x26cc) and whether the start mount was done
    start_tag: i64,
    started: bool,
    /// the counter of the mission's grunt_wall_counter_tag (Game+0x26fc): the HUD's grunts over the wall
    wall_counter: Option<u32>,
    /// the boarders' anim counts: 0x0064a75c (grunt_turret_attack, _1 .. _5) and the byte 0x0064a758 (jump_on / _1)
    attack_count: u32,
    jump_toggle: bool,
}

/// A grunt boarding the base turret (AI state 0x67, set by 0x0042d780 / 0x0042d8f0): attached to the turret
/// (Object_Attach 0x004539f0) and taken off every AI list, he plays his anim, authored in the turret's frame (its root
/// "center" carries him over the turret), and is removed when it ends (state 0x67: vt+0xdc). The anim's notes run as
/// a mover's script ("fx NAME", "trigger N").
#[derive(Component)]
pub struct TurretBoarder {
    pub anim: String,
    t: f32,
    len: f32,
    started: bool,
}

/// A playtest's debug command (playtest.rs `defence`): {"aim": "army"} keeps the turret on the nearest army grunt
/// (else marching platoon in sight), {"aim": "platoon"} on the nearest marching platoon in sight only, {"aim": false}
/// stops; {"fire": true / false} both guns fire as if their buttons were held (the mortar each time it is ready);
/// {"mortar": "platoon"} fires a shell at the nearest platoon, {"mortar": "auto"} one a second until {"mortar": false}
/// (a completion run's alt-fire); {"frame": F} puts the turret at that track frame.
#[derive(Message, Clone, Debug)]
pub struct DefenceCommand(pub serde_json::Value);

#[derive(Resource, Default)]
struct DefenceDebug {
    aim_army: bool,
    /// only the marching platoons (their members die to the mortar and count towards the win: trigger 27)
    aim_platoon: bool,
    /// both guns fire as if their buttons were held: the machine gun, and the mortar each time it is ready
    fire: bool,
    /// the auto mortar: seconds to the next shell
    auto_mortar: Option<f32>,
}

pub struct DefencePlugin;

impl Plugin for DefencePlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<DefenceState>()
            .init_level_resource::<DefenceDebug>()
            .add_message::<DefenceCommand>()
            .add_systems(Update, (setup, start, debug_commands, (window, ride, run_over, launchers, overrun, failure_mob, boarders, log).chain().after(crate::turret::ride).after(crate::hud_state)).chain().run_if(crate::playing))
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
    mut rng: ResMut<crate::fx::FxStats>,
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
        // the fake launchers at the turret's origin (attached, 0x0049a6a0: the homing one hidden) and the trolley
        // (0x0049abf0 multiplies the launchers' LOD cutoffs by 5 so the far wall's turret keeps them: the engine draws
        // its models at every distance)
        let helper = |kit: &mut crate::army::ModelKit, commands: &mut Commands, template: &str| {
            let m = data.templates.get(template).and_then(|t| t.model.clone())?;
            kit.spawn(commands, &m, Transform::IDENTITY, false)
        };
        let launchers = match (helper(&mut kit, &mut commands, "fake_base_mortar"), helper(&mut kit, &mut commands, "fake_base_homing")) {
            (Some(m), Some(h)) => {
                commands.entity(e).add_children(&[m, h]);
                commands.entity(h).insert(Visibility::Hidden);
                Some((m, h))
            }
            _ => None,
        };
        let trolley = helper(&mut kit, &mut commands, "base_turret_trolley");
        let non_locking = (p.i("min_non_locking", 0) as i32, p.i("max_non_locking", 5) as i32);
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
            eye: None,
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
            failure_left: p.f("failure_mode_duration", 5.0),
            fx_anchor: None,
            fx_flags: 0,
            sounds: (false, false),
            crowd: (-1, 0.0),
            launchers,
            trolley,
            homing_shown: false,
            reload: None,
            trolley_world: Mat4::IDENTITY,
            non_locking,
            // +0x818 = min + ((rand * (max - min + 1)) >> 15)
            lock_countdown: non_locking.0 + (rng.rand() * (non_locking.1 - non_locking.0 + 1) as f32) as i32,
            target_motion: default(),
            shield_seen: None,
            glass_hits: 0,
            load_reloads: true,
            removed: false,
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
    (physics, time): (NonSend<crate::physics::Physics>, Res<Time>),
) {
    let Some(me) = mounted.0 else {
        cmds.clear();
        return;
    };
    let Ok((mut t, gt, a)) = turrets.get_mut(me) else { return };
    let from = gt.translation();
    // the nearest marching member the turret can see (a bullet's ray from the turret reaches its chest: one behind a
    // ridge is no target)
    let in_sight = |p: Vec3| {
        let to = p + Vec3::Y * 6.0;
        let start = from + (to - from).normalize_or(Vec3::Y) * 15.0;
        physics.raycast_bullet(start, to - start, (to - start).length()).is_none_or(|h| h.1.distance(to) < 12.0)
    };
    let nearest_platoon = || {
        platoons
            .iter()
            .flat_map(|(p, m)| p.member_points(m))
            .filter(|p| in_sight(*p))
            .min_by(|x, y| x.distance(from).total_cmp(&y.distance(from)))
    };
    for c in cmds.read() {
        let v = &c.0;
        if let Some(aim) = v.get("aim") {
            dbg.aim_army = aim.as_str() == Some("army");
            dbg.aim_platoon = aim.as_str() == Some("platoon");
        }
        if let Some(f) = v.get("fire").and_then(|f| f.as_bool()) {
            dbg.fire = f;
        }
        if let Some(f) = v.get("frame").and_then(|f| f.as_f64()) {
            t.frame = f as f32;
        }
        // {"upgrade": true}: the ammo upgrade as a shot turret_upgrade starts it (Game+0xdd1)
        if v.get("upgrade").and_then(|u| u.as_bool()) == Some(true) {
            t.upgrade = t.upgrade_duration;
            info!("EVENT defence: debug ammo upgrade for {} s", t.upgrade_duration);
        }
        let mut fire = false;
        match v.get("mortar") {
            Some(m) if m.as_str() == Some("auto") => dbg.auto_mortar = Some(0.0),
            Some(m) if m.as_bool() == Some(false) => dbg.auto_mortar = None,
            Some(_) => fire = true,
            None => {}
        }
        if fire {
            if let (Some(target), Some(em)) = (nearest_platoon(), t.emitters[2]) {
                let start = (gt.to_matrix() * bone_model(&a.skeleton, em)).w_axis.truncate();
                let template = t.mortar.0.clone();
                crate::shell::launch(&mut commands, &data, &mut kit, &mut fx, &template, start, target, crate::shell::Lob { by: crate::fx::Src::Player, flight: None });
                info!("EVENT defence: debug mortar at the nearest platoon, {:.0} away", (target - start).length());
            }
        }
    }
    if let Some(left) = dbg.auto_mortar.as_mut() {
        *left -= time.delta_secs();
        if *left <= 0.0 {
            *left = 1.0;
            if let (Some(target), Some(em)) = (nearest_platoon(), t.emitters[2]) {
                let start = (gt.to_matrix() * bone_model(&a.skeleton, em)).w_axis.truncate();
                let template = t.mortar.0.clone();
                crate::shell::launch(&mut commands, &data, &mut kit, &mut fx, &template, start, target, crate::shell::Lob { by: crate::fx::Src::Player, flight: None });
            }
        }
    }
    if dbg.aim_army || dbg.aim_platoon {
        // the nearest live army grunt (not with "platoon"), else the nearest marching member; aimed through the
        // turret's centre (the camera sits on its axis)
        let target = grunts
            .iter()
            .filter(|(g, _)| dbg.aim_army && g.mind != crate::ai::Mind::Dead)
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

/// The turret's glass window (0x0049aeb0): the shield it puts on Roman takes the hits while it stands; each hit puts
/// fx_glass_bullet_hole on a random one of its 32 hit nodes (sprintf "Ref_hit_%d", (rand << 5) >> 15: the nodes of
/// its anim base_turret_l5_m5_window); used up, it bursts (fx_death of p_base_turret_l5_m5_window) and is released.
fn window(
    mut commands: Commands,
    mounted: Res<crate::turret::Mounted>,
    mut turrets: Query<&mut BaseTurret>,
    roman: Query<(Entity, Option<&crate::fx::DamageShield>), With<crate::player::Roman>>,
    windows: Query<&GlobalTransform>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut kit: crate::army::ModelKit,
) {
    let Some(mut t) = mounted.0.and_then(|m| turrets.get_mut(m).ok()) else { return };
    let (Ok((re, Some(shield))), Some(w)) = (roman.single(), t.window) else { return };
    let Ok(wt) = windows.get(w) else { return };
    let seen = t.shield_seen.replace(shield.health).unwrap_or(shield.health);
    if shield.health < seen {
        let node = format!("ref_hit_{}", (rng.rand() * 32.0) as u32);
        let at = kit.anim("base_turret_l5_m5_window").and_then(|a| a.translation(&node, 0.0)).unwrap_or(Vec3::ZERO);
        let (_, rotation, _) = wt.to_scale_rotation_translation();
        fx.write(crate::fx::StartFx { name: "fx_glass_bullet_hole".into(), at: wt.transform_point(at), rotation, follow: Some(w), position_only: false });
        t.glass_hits += 1;
    }
    if shield.health <= 0.0 {
        fx.write(crate::fx::StartFx { name: "fx_explosion_turret_window".into(), at: wt.translation(), rotation: Quat::IDENTITY, follow: None, position_only: false });
        commands.entity(w).despawn();
        commands.entity(re).remove::<crate::fx::DamageShield>();
        t.window = None;
        info!("EVENT defence: the turret's window is shot out");
    }
}

/// The base turret's machine-gun aim (0x0049bf20): the object under the crosshair when it is a character (+0x424 bit
/// 0x40, set by the AI classes' slot 7 0x00425df0); else, with lock-on candidates in view (the turret's multi-lock pass,
/// weapons/targeting.rs), the crosshair point for a random min..max_non_locking rounds (+0x818), then a random one of
/// the candidates on every round until none is in view (which re-arms the count). A moving character is led for the
/// bullet's speed (0x004580a0). Returns the aim point.
fn mgun_aim(t: &mut BaseTurret, xhair: &crate::hud::Crosshair, targets: &crate::weapons::targeting::Targets, is_character: impl Fn(Entity) -> bool, muzzle: Vec3, speed: f32, rng: &mut crate::fx::FxStats) -> Option<Vec3> {
    let cands = &targets.list[..targets.list.len().min(4)];
    let target = match xhair.object.filter(|&o| is_character(o)) {
        Some(o) => Some((o, cands.iter().find(|c| c.entity == o).map(|c| c.point).or(xhair.point)?)),
        None if cands.is_empty() => {
            t.lock_countdown = t.non_locking.0 + (rng.rand() * (t.non_locking.1 - t.non_locking.0 + 1) as f32) as i32;
            None
        }
        None => {
            t.lock_countdown -= 1;
            (t.lock_countdown < 0).then(|| cands[((rng.rand() * cands.len() as f32) as usize).min(cands.len() - 1)]).map(|c| (c.entity, c.point))
        }
    };
    match target {
        Some((e, point)) => {
            let vel = t.target_motion.velocity(e);
            Some(crate::ai::aim::lead_point(point, vel, muzzle, speed))
        }
        None => xhair.point,
    }
}

/// The targets' velocities (0x00458000) from their positions frame to frame: the crosshair's object and the lock-on
/// candidates.
fn track_targets(t: &mut BaseTurret, xhair: &crate::hud::Crosshair, targets: &crate::weapons::targeting::Targets, positions: &Query<&GlobalTransform>, dt: f32) {
    let seen = targets.list.iter().map(|c| c.entity).chain(xhair.object).filter_map(|e| positions.get(e).ok().map(|p| (e, p.translation()))).collect();
    t.target_motion.observe(seen, dt);
}

/// The mounted turret's update (0x0049b030) and the start-in-turret hold on Roman.
#[allow(clippy::too_many_arguments)]
fn ride(
    mut commands: Commands,
    time: Res<Time>,
    (ctl, motion, look, playtest, opts): (Res<crate::input::Controls>, Res<AccumulatedMouseMotion>, Res<crate::player::LookDelta>, Option<Res<crate::playtest::Playtest>>, Res<crate::options::GameOptions>),
    mounted: Res<crate::turret::Mounted>,
    mut turrets: Query<(Entity, &mut BaseTurret, &mut crate::skin::Animator, &mut Transform)>,
    mut roman: Query<(Entity, &mut crate::player::Roman, &mut Transform, &crate::fx::Health, &mut Visibility), Without<BaseTurret>>,
    (mut physics, data, xhair): (NonSendMut<crate::physics::Physics>, Res<crate::gamedata::GameData>, Res<crate::hud::Crosshair>),
    (mut sounds, mut stop, mut fx, mut triggers): (MessageWriter<crate::audio::PlaySound>, MessageWriter<crate::audio::StopSound>, MessageWriter<crate::fx::StartFx>, MessageWriter<crate::mission::SendTrigger>),
    (mut stats, mut hud, army): (ResMut<crate::player::PlayerStats>, ResMut<crate::hud::HudState>, Res<crate::army::Army>),
    (joints, mut upgrades): (Query<&GlobalTransform>, MessageReader<crate::icobject::TurretUpgrade>),
    mut kit: crate::army::ModelKit,
    dbg: Res<DefenceDebug>,
    (targets, characters, mut rng): (Res<crate::weapons::targeting::Targets>, Query<(), With<crate::ai::Grunt>>, ResMut<crate::fx::FxStats>),
) {
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let Some(me) = mounted.0 else { return };
    let Ok((te, mut t, mut a, mut tt)) = turrets.get_mut(me) else { return };
    let Ok((re, mut r, mut rt, health, mut vis)) = roman.single_mut() else { return };
    track_targets(&mut t, &xhair, &targets, &joints, dt);
    let track = kit.anim(&t.track_anim);
    let skeleton = a.skeleton.clone();
    let to_engine = Mat4::from_quat(crate::level::game_to_engine());
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
    // side movement along the wall (input axes 2 / 3, Controls_AxisMaxCapped 0x00414aa0 scaled by side_accel: the
    // stick's deflection past its dead zone (Controls) sets the acceleration; keys give full)
    let side_in = if can { ctl.axis(Act::Move).x.clamp(-1.0, 1.0) } else { 0.0 };
    let (smax, sacc, sstop) = t.side;
    if side_in != 0.0 {
        t.side_speed = (t.side_speed + side_in * sacc * dt).clamp(-smax, smax);
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
    t.trolley_world = t.wall * Mat4::from_translation(track.as_deref().and_then(|a| a.translation("ref_base_turret", t.frame / 30.0)).unwrap_or(Vec3::ZERO));
    *tt = crate::mover::engine_transform(world);
    let engine = to_engine * world;
    let bone_world = |i: usize| engine * bone_model(&skeleton, i);
    // Roman held at the seat (Object_Attach at player_bone_name), hidden
    let seat = t.seat.map(|i| bone_world(i).w_axis.truncate()).unwrap_or(tt.translation);
    t.eye = Some(t.muzzles[0].map_or(seat + Vec3::Y * 4.0, |i| bone_world(i).w_axis.truncate()));
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
    // while the upgrade lasts the homing launcher takes the mortar's place (0x0049b030: base_homing_reload)
    if t.upgrade > 0.0 && !t.homing_shown {
        t.homing_shown = true;
        t.reload = Some(true);
    }
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
    if can && (ctl.down(Act::Fire) || ctl.pressed(Act::Fire) || dbg.fire) {
        t.fx_flags = (t.fx_flags | 0x40) & !0x80;
        a.play(0, "base_turret_l5_m5_fire", true, crate::skin::Mask::All);
        if t.last_fire.is_none_or(|l| now - l > t.recycle) {
            let gun = if t.left { 0 } else { 1 };
            if let Some(em) = t.emitters[gun].or(t.emitters[0]) {
                let m = bone_world(em);
                let from = m.w_axis.truncate();
                let fwd = engine.transform_vector3(Vec3::NEG_Y).normalize_or(Vec3::NEG_Z);
                let bullet = data.props(&t.projectile);
                // the bullet's speed as Projectile_Load gives it (0x0047e360; player.rs spawn_projectile)
                let speed = bullet.f("speed", 300.0) * 3.0;
                let aim = mgun_aim(&mut t, &xhair, &targets, |e| characters.contains(e), from, speed, &mut rng);
                let dir = aim.map_or(fwd, |q| (q - from).normalize_or(fwd));
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
    if can && (ctl.pressed(Act::Special) || dbg.fire) {
        // ready (0x0049be70): every check holds the muzzle fx (+0x530 |= 0x40, &= ~0x80); mortar_special_recycle_time
        // while the homing launcher shows, else mortar_recycle_time
        t.fx_flags = (t.fx_flags | 0x40) & !0x80;
        let wait = if t.homing_shown { t.mortar_recycle.1 } else { t.mortar_recycle.0 };
        if t.last_mortar.is_none_or(|l| now - l > wait) {
            if let Some(em) = t.emitters[2] {
                let from = bone_world(em).w_axis.truncate();
                let fwd = engine.transform_vector3(Vec3::NEG_Y);
                let target = xhair.point.unwrap_or(from + fwd * 500.0);
                // 0x0049bf20: the special shell during the upgrade (the homing launcher reloads); after it, the first shell
                // while the homing launcher still shows is a special one too, and the mortar launcher comes back
                let template = if t.upgrade > 0.0 || t.homing_shown { t.mortar.1.clone() } else { t.mortar.0.clone() };
                if t.upgrade <= 0.0 {
                    t.homing_shown = false;
                }
                t.reload = Some(t.upgrade > 0.0);
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

/// The turret runs over the grunts scaling the wall (0x0049b030; meant for every third frame, but the count +0x824 is
/// set to count + 1 and then back to 0 whenever count % 3 == 0, so it stays 0 and the test runs every frame): an AI of the AI list
/// (World+0xe7c; vt+0x10c) in an IC move (0x00426630: +0xa18 bit 0; the army's ic_* ladder and wall-scale scripts)
/// within 15 of the turret across (squared < 225) and 10 up or down (30 while the turret stands still) turns the side
/// speed round, shakes the camera (0x0040fac0: 0.5 for 1 s) and takes the turret's hit of 300000 (flags 0x200, impact
/// 1000 along the turret's facing): dead.
fn run_over(
    mounted: Res<crate::turret::Mounted>,
    mut turrets: Query<(&mut BaseTurret, &Transform)>,
    mut grunts: Query<(&mut crate::ai::Grunt, &mut crate::fx::Health, &GlobalTransform, Option<&crate::army::ArmyGrunt>), Without<crate::player::Roman>>,
    mut shake: MessageWriter<crate::camera::ShakeCamera>,
) {
    let Some((mut t, tt)) = mounted.0.and_then(|m| turrets.get_mut(m).ok()) else { return };
    let reach_z = if t.side_speed == 0.0 { 30.0 } else { 10.0 };
    let at = tt.translation;
    for (mut g, mut h, gt, army) in &mut grunts {
        let scripted = g.mind == crate::ai::Mind::Scripted || army.is_some_and(|a| a.in_script());
        if !scripted || g.mind == crate::ai::Mind::Dead || h.0 <= 0.0 {
            continue;
        }
        let d = gt.translation() - at;
        if d.x * d.x + d.z * d.z >= 225.0 || d.y.abs() >= reach_z {
            continue;
        }
        t.side_speed = -t.side_speed;
        shake.write(crate::camera::ShakeCamera { amplitude: 0.5, secs: 1.0 });
        h.0 -= 300000.0;
        g.cartoon = false;
        info!("EVENT defence: the turret runs over a grunt on the wall");
    }
}

/// The fake launchers and the trolley as the turret's state has them: the shown launcher, its reload anim after a
/// shell, the trolley on the track.
fn launchers(
    mut q: Query<&mut BaseTurret>,
    mut parts: Query<(&mut Visibility, &mut Transform, Option<&mut crate::skin::Animator>), Without<BaseTurret>>,
) {
    for mut t in &mut q {
        if let Some(tr) = t.trolley {
            if let Ok((_, mut tt, _)) = parts.get_mut(tr) {
                *tt = crate::mover::engine_transform(t.trolley_world);
            }
        }
        let Some((mortar, homing)) = t.launchers else { continue };
        // at load (0x0049a6a0) each launcher is set to its reload anim, played once and held (the homing one hidden)
        if t.load_reloads {
            let mut done = true;
            for (e, anim) in [(mortar, "base_mortar_reload"), (homing, "base_homing_reload")] {
                match parts.get_mut(e) {
                    Ok((_, _, Some(mut a))) => a.play(0, anim, false, crate::skin::Mask::All),
                    _ => done = false,
                }
            }
            t.load_reloads = !done;
        }
        let shown = t.homing_shown;
        for (e, on) in [(mortar, !shown), (homing, shown)] {
            if let Ok((mut v, ..)) = parts.get_mut(e) {
                *v = if on { Visibility::Inherited } else { Visibility::Hidden };
            }
        }
        if let Some(h) = t.reload.take() {
            let (e, anim) = if h { (homing, "base_homing_reload") } else { (mortar, "base_mortar_reload") };
            if let Ok((_, _, Some(mut a))) = parts.get_mut(e) {
                a.play(0, anim, false, crate::skin::Mask::All);
                a.restart(0);
            }
        }
    }
}

/// The turret's overrun (its "overrun" trigger, 0x0049bc90 sets +0x62f): while Roman rides it and no failure mob is on
/// (Game+0xdcd), every rand(45, 75) s (+0x838, 0x0046e2c0) a grunt_regular jumps onto it (0x0042d8f0).
#[allow(clippy::too_many_arguments)]
fn overrun(
    mut commands: Commands,
    time: Res<Time>,
    mut events: MessageReader<crate::mission::SendTrigger>,
    mut q: Query<(Entity, &mut BaseTurret)>,
    mounted: Res<crate::turret::Mounted>,
    mut state: ResMut<DefenceState>,
    mut rng: ResMut<crate::fx::FxStats>,
    mission: Option<Res<crate::mission::Mission>>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
) {
    let ids: Vec<i64> = events.read().map(|e| e.0).collect();
    let now = time.elapsed_secs();
    let mob = mission.as_ref().is_some_and(|m| m.failure_mob);
    for (e, mut t) in &mut q {
        if ids.iter().any(|&i| i != 0 && i == t.overrun_trigger) && !t.overrun {
            t.overrun = true;
            t.overrun_next = now;
            info!("EVENT defence: the base is overrun");
        }
        if !t.overrun || mob || mounted.0 != Some(e) || now < t.overrun_next {
            continue;
        }
        t.overrun_next = now + rng.range(45.0, 75.0);
        // 0x0042d8f0: "grunt_turret_jump_on" while the byte is set, else "grunt_turret_jump_on_1"; then it flips
        let anim = if state.jump_toggle { "grunt_turret_jump_on" } else { "grunt_turret_jump_on_1" };
        state.jump_toggle = !state.jump_toggle;
        board(&mut commands, &mut kit, &data, e, anim);
        info!("EVENT defence: an overrun grunt jumps onto the turret ({anim})");
    }
}

/// A grunt_regular made (ObjectSystem_CreateObject 0x0045f020) and put on `turret` with `anim` (state 0x67).
fn board(commands: &mut Commands, kit: &mut crate::army::ModelKit, data: &crate::gamedata::GameData, turret: Entity, anim: &str) {
    let Some(model) = data.templates.get("grunt_regular").and_then(|t| t.model.clone()) else { return };
    let len = kit.anim(anim).map(|a| a.duration).unwrap_or(0.0);
    if let Some(g) = kit.spawn(commands, &model, Transform::IDENTITY, true) {
        commands.entity(g).insert(TurretBoarder { anim: anim.to_owned(), t: 0.0, len, started: false });
        commands.entity(turret).add_child(g);
    }
}

/// The failure mob (0x0049ad60) and its countdown (0x0049b030: while Game+0xdcd is set +0x830 counts down, then the
/// turret's removal 0x0049ac60 sets level state 3). The mission learns whether Roman rides a base turret (Game+0xac8 =
/// 1), which turns its failure triggers into the mob (MissionObject_OnTrigger 0x0044d600).
#[allow(clippy::too_many_arguments)]
fn failure_mob(
    mut commands: Commands,
    time: Res<Time>,
    mut mounted: ResMut<crate::turret::Mounted>,
    mut q: Query<(Entity, &mut BaseTurret)>,
    mut state: ResMut<DefenceState>,
    mission: Option<ResMut<crate::mission::Mission>>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    roman: Query<(Entity, &mut Visibility), With<crate::player::Roman>>,
) {
    let Some(mut mission) = mission else { return };
    let Some((e, mut t)) = mounted.0.and_then(|m| q.get_mut(m).ok()) else {
        mission.base_turret_mounted = false;
        return;
    };
    mission.base_turret_mounted = true;
    while mission.failure_mob_calls > 0 {
        mission.failure_mob_calls -= 1;
        for _ in 0..6 {
            // 0x0042d780: "grunt_turret_attack" at the count 0, "_1" .. "_4" at 1 .. 4, "_5" after
            let anim = match state.attack_count {
                0 => "grunt_turret_attack".to_owned(),
                n @ 1..=4 => format!("grunt_turret_attack_{n}"),
                _ => "grunt_turret_attack_5".to_owned(),
            };
            state.attack_count += 1;
            board(&mut commands, &mut kit, &data, e, &anim);
        }
        info!("EVENT defence: the failure mob boards the turret ({:.1} s to the loss)", t.failure_left);
    }
    if mission.failure_mob {
        t.failure_left -= time.delta_secs();
        if t.failure_left <= 0.0 {
            mission.failure_mob = false;
            remove(&mut commands, &mut t, &mut mounted, roman);
            mission.fail_state(3, "the failure mob took the base turret");
            info!("EVENT defence: the turret is lost to the failure mob");
        }
    }
}

/// The turret's removal (vt+0xdc 0x0049ac60): the player dismounted (vt+0x17c 0x0049bd80: zoomed out, detached where he
/// sits, Game+0xac8 cleared, shown again: player vt+0x134(1)), the window, the trolley and the fake launchers released
/// (gone); the turret's model stays. The caller sets level state 3.
fn remove(commands: &mut Commands, t: &mut BaseTurret, mounted: &mut crate::turret::Mounted, roman: Query<(Entity, &mut Visibility), With<crate::player::Roman>>) {
    t.removed = true;
    t.zoomed = false;
    let launchers = t.launchers.take().map(|(m, h)| [m, h]).into_iter().flatten();
    for e in t.window.take().into_iter().chain(t.trolley.take()).chain(launchers) {
        commands.entity(e).despawn();
    }
    if let Some(a) = t.fx_anchor.take() {
        commands.entity(a).despawn();
    }
    mounted.0 = None;
    for (re, mut vis) in roman {
        commands.entity(re).remove::<crate::fx::DamageShield>();
        *vis = Visibility::Inherited;
    }
}

/// The boarders' anims (state 0x67): played once in the turret's frame, their notes run, removed at the end.
fn boarders(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut TurretBoarder, &GlobalTransform, Option<&mut crate::skin::Animator>)>,
    mut kit: crate::army::ModelKit,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut triggers: MessageWriter<crate::mission::SendTrigger>,
) {
    for (e, mut b, gt, animator) in &mut q {
        let Some(mut a) = animator else { continue };
        if !b.started {
            b.started = true;
            a.play(0, &b.anim, false, crate::skin::Mask::All);
        }
        let t0 = b.t;
        b.t += time.delta_secs();
        if let Some(anim) = kit.anim(&b.anim) {
            // a note before the start (fx_zeppelin_reload at -1 / 30 s) runs on the first frame
            for line in crate::mover::notes_between(&anim, t0, b.t) {
                let mut w = line.split_whitespace();
                match (w.next().map(str::to_lowercase).as_deref(), w.next()) {
                    (Some("fx"), Some(name)) => {
                        let (_, rot, at) = gt.to_scale_rotation_translation();
                        fx.write(crate::fx::StartFx { name: name.to_lowercase(), at, rotation: rot, follow: None, position_only: false });
                    }
                    (Some("trigger"), Some(n)) => {
                        if let Ok(n) = n.parse() {
                            triggers.write(crate::mission::SendTrigger(n));
                        }
                    }
                    _ => {}
                }
            }
        }
        if b.t >= b.len {
            commands.entity(e).despawn();
        }
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
    boarders: Query<&TurretBoarder>,
) {
    hud.over_wall = defence.wall_counter.and_then(|id| mission.as_ref()?.counter(id));
    let Some(mut state) = state else { return };
    let Some((e, t, gt)) = q.iter().next() else { return };
    let p = gt.translation();
    state.0.insert("base_turret".into(), serde_json::json!({
        "mounted": mounted.0 == Some(e),
        "boarders": boarders.iter().count(),
        "failure_mob": mission.as_ref().is_some_and(|m| m.failure_mob),
        "failure_left": t.failure_left,
        "intro": t.intro.is_some(),
        "frame": t.frame,
        "yaw": t.yaw.to_degrees(),
        "pitch": t.pitch.to_degrees(),
        "zoomed": t.zoomed,
        "window": t.window.is_some(),
        "removed": t.removed,
        "glass_hits": t.glass_hits,
        "lock_countdown": t.lock_countdown,
        "overrun": t.overrun,
        "upgrade": t.upgrade,
        "homing_shown": t.homing_shown,
        "launchers": t.launchers.is_some(),
        "trolley": t.trolley.is_some(),
        "pos": [p.x, -p.z, p.y],
        "over_wall": hud.over_wall.map(|c| c.0),
    }));
}
