//! Armed and Dangerous on the remaster engine (MVP): a level from the retail data (tools/remaster_export.py),
//! Bevy 0.20 rendering, Jolt Physics 5.6, grunt AI, explosions, bevy_egui menus and a benchmark mode.
//!
//!   and_remaster [--scene DIR] [--size WxH] [--vsync] [--no-ai] [--bench SECONDS --out FILE]
//!
//! Game-neutral modules: scene (format), physics (Jolt), fx (explosions), flycam, bench, menu.
//! Game-specific: level (A&D templates, axes, units, lights), ai (grunts), player.

mod ai;
mod audio;
mod bench;
mod campath;
mod charanim;
mod flycam;
mod fx;
mod fxvm;
mod gamedata;
mod camdump;
mod gamemenu;
mod gamematerial;
mod options;
mod gameaudio;
mod hud;
mod level;
mod menu;
mod mission;
mod nav;
mod physics;
mod pickup;
mod pubscreen;
mod topsy;
mod player;
mod playtest;
mod scene;
mod skin;

use bevy::diagnostic::FrameTimeDiagnosticsPlugin;
use bevy::prelude::*;
use bevy::window::{PresentMode, WindowResolution};
use bevy_egui::EguiPlugin;
use std::path::PathBuf;

#[derive(States, Default, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AppState {
    #[default]
    Title,
    Playing,
    Paused,
}

pub fn playing(state: Res<State<AppState>>) -> bool {
    *state.get() == AppState::Playing
}

struct Args {
    scene: PathBuf,
    size: (u32, u32),
    bench: Option<(f32, PathBuf)>,
    vsync: bool,
    no_ai: bool,
    fx_test: Option<PathBuf>,
    snap: Option<PathBuf>,
    play: Option<PathBuf>,
    /// start in the level (Restart the level: the menus start the game again with this)
    skip_title: bool,
    /// render from a camera dumped from the original (camdump.rs)
    camera: Option<PathBuf>,
    /// no HUD or menus (render comparisons)
    no_hud: bool,
    /// test runs: the window opens off-screen, unfocused and behind everything, so it does not take over the desktop
    background: bool,
    /// the landscape with Bevy's standard material instead of the original's (A/B comparisons)
    std_materials: bool,
    /// debug: hide "landscape" or the placed objects whose template contains this text (render comparisons)
    debug_hide: Option<String>,
}

fn args() -> Args {
    let mut a = Args { scene: PathBuf::from("../work/remaster/lev_1_m1"), size: (1920, 1080), bench: None, vsync: false, no_ai: false, fx_test: None, snap: None, play: None, skip_title: false, camera: None, no_hud: false, background: false, std_materials: false, debug_hide: None };
    let v: Vec<String> = std::env::args().collect();
    let mut bench_secs = None;
    let mut out = PathBuf::from("../work/remaster/bench/latest.json");
    let mut i = 1;
    while i < v.len() {
        let next = v.get(i + 1).cloned().unwrap_or_default();
        let mut used = 2;
        match v[i].as_str() {
            "--scene" => a.scene = PathBuf::from(next),
            "--size" => {
                let (w, h) = next.split_once('x').expect("--size WxH");
                a.size = (w.parse().unwrap(), h.parse().unwrap());
            }
            "--bench" => bench_secs = Some(next.parse::<f32>().expect("--bench SECONDS")),
            "--out" => out = PathBuf::from(next),
            "--vsync" => (a.vsync, used) = (true, 1),
            "--no-ai" => (a.no_ai, used) = (true, 1),
            "--fx-test" => a.fx_test = Some(PathBuf::from(next)),
            "--snap" => a.snap = Some(PathBuf::from(next)),
            "--play" => a.play = Some(PathBuf::from(next)),
            "--skip-title" => (a.skip_title, used) = (true, 1),
            "--camera" => a.camera = Some(PathBuf::from(next)),
            "--no-hud" => (a.no_hud, used) = (true, 1),
            "--background" => (a.background, used) = (true, 1),
            "--std-materials" => (a.std_materials, used) = (true, 1),
            "--debug-hide" => a.debug_hide = Some(next),
            other => panic!("unknown argument {other}"),
        }
        i += used;
    }
    a.bench = bench_secs.map(|s| (s, out));
    a
}

#[derive(Resource)]
pub struct Launch {
    fx_test: bool,
    pub scene: PathBuf,
    bench: Option<(f32, PathBuf)>,
    size: (u32, u32),
    no_ai: bool,
    no_hud: bool,
    std_materials: bool,
}

/// `--fx-test DIR`: camera parked by the powder kegs nearest the level centre; at 1 s the nearest keg is shot
/// (killed directly, as a bullet would); screenshots every 0.1 s to DIR for 3 s, then exit.
#[derive(Resource)]
struct FxTest {
    dir: PathBuf,
    t: f32,
    shot: usize,
    target: Option<Entity>,
}

fn main() {
    let a = args();
    let scene_dir = std::fs::canonicalize(&a.scene).unwrap_or_else(|_| panic!("scene folder {}", a.scene.display()));
    let bench = a.bench.is_some();
    let mut app = App::new();
    app.add_plugins(
        DefaultPlugins
            .set(WindowPlugin {
                primary_window: Some(Window {
                    title: "Armed and Dangerous - remaster engine".into(),
                    resolution: WindowResolution::new(a.size.0, a.size.1).with_scale_factor_override(1.0),
                    present_mode: if a.vsync { PresentMode::AutoVsync } else { PresentMode::AutoNoVsync },
                    position: if a.background { bevy::window::WindowPosition::At(IVec2::new(-20000, -20000)) } else { bevy::window::WindowPosition::Automatic },
                    focused: !a.background,
                    window_level: if a.background { bevy::window::WindowLevel::AlwaysOnBottom } else { bevy::window::WindowLevel::Normal },
                    skip_taskbar: a.background,
                    ..default()
                }),
                ..default()
            })
            .set(AssetPlugin { file_path: scene_dir.to_string_lossy().into_owned(), ..default() })
            // Bevy's spatial audio only pans: game units are scaled down so its own distance fall-off is negligible;
            // audio.rs attenuates by the game's min/max distances
            .set(bevy::audio::AudioPlugin { default_spatial_scale: bevy::audio::SpatialScale::new(0.002), ..default() }),
    )
    .add_plugins(EguiPlugin::default())
    .add_plugins(playtest::PlaytestPlugin)
    .add_plugins(audio::AudioBankPlugin)
    .add_plugins(pickup::PickupPlugin)
    .add_plugins((FrameTimeDiagnosticsPlugin::default(), flycam::FlyCamPlugin, bench::BenchPlugin))
    .add_plugins((physics::PhysicsPlugin, fx::FxPlugin, ai::AiPlugin, player::PlayerPlugin, menu::MenuPlugin, level::LevelPlugin, fxvm::FxVmPlugin, ai::CompanionPlugin, skin::SkinPlugin, charanim::CharAnimPlugin, hud::HudPlugin, mission::MissionPlugin, gameaudio::GameAudioPlugin, campath::CameraPathPlugin, gamemenu::GameMenuPlugin))
    .add_plugins(options::OptionsPlugin)
    .add_plugins(gamematerial::GameMaterialPlugin)
    .add_plugins(pubscreen::PubScreenPlugin)
    .add_plugins(topsy::TopsyPlugin)
    .init_state::<AppState>()
    .insert_non_send(physics::Physics::new(level::METERS_PER_UNIT))
    .insert_resource(ClearColor(Color::srgb(0.62, 0.70, 0.80)))
    .insert_resource(Launch { fx_test: a.fx_test.is_some(), scene: scene_dir, bench: a.bench, size: a.size, no_ai: a.no_ai, no_hud: a.no_hud, std_materials: a.std_materials })
    .add_systems(Update, fx_test.run_if(resource_exists::<FxTest>))
    .add_systems(Startup, setup)
    .add_systems(Update, (play_state, hud_state))
    .add_systems(Update, start_camera_fx.run_if(playing))
    .add_systems(Update, (bench_scenario, bench_record).run_if(resource_exists::<bench::Bench>))
    .add_systems(Update, bench_ground.after(bench::BenchSet).run_if(resource_exists::<bench::Bench>));
    if let Some(dir) = a.fx_test {
        let _ = std::fs::create_dir_all(&dir);
        app.insert_resource(FxTest { dir, t: 0.0, shot: 0, target: None });
        app.insert_state(AppState::Playing);
    }
    if bench {
        app.insert_state(AppState::Playing);
    }
    if a.skip_title {
        app.insert_state(AppState::Playing);
    }
    app.add_plugins(camdump::CamDumpPlugin);
    if let Some(h) = a.debug_hide.clone() {
        app.add_systems(Update, move |mut q: Query<(&Name, &mut Visibility)>| {
            for (n, mut v) in &mut q {
                if n.as_str().contains(h.as_str()) {
                    *v = Visibility::Hidden;
                }
            }
        });
    }
    if let Some(c) = &a.camera {
        app.insert_resource(camdump::CameraDump::load(c).fixed());
    }
    // --play SCRIPT: scripted input from the start of play (the title menu is skipped)
    if let Some(p) = a.play {
        let pt = playtest::Playtest::load(p);
        if !pt.title {
            app.insert_state(AppState::Playing);
        }
        app.insert_resource(pt);
    }
    // --snap FILE: a screenshot of whatever shows after 4 s (the title menu without other options), then exit
    if let Some(path) = a.snap {
        app.add_systems(Update, move |mut commands: Commands, time: Res<Time<Real>>, mut done: Local<u8>, mut exit: MessageWriter<AppExit>| {
            let t = time.elapsed_secs();
            if t > 4.0 && *done == 0 {
                commands.spawn(bevy::render::view::screenshot::Screenshot::primary_window()).observe(bevy::render::view::screenshot::save_to_disk(path.clone()));
                *done = 1;
            } else if t > 5.5 {
                exit.write(AppExit::Success);
            }
        });
    }
    app.run();
}

#[allow(clippy::too_many_arguments)]
/// The vertical FOV of the original's follow camera (player.json camera node Camera_end, horizontal radians).
fn original_vertical_fov(scene: &std::path::Path) -> f32 {
    let h = std::fs::read_to_string(scene.join("player.json"))
        .ok()
        .and_then(|s| serde_json::from_str::<serde_json::Value>(&s).ok())
        .and_then(|v| v["camera"]["nodes"]["Camera_end"]["fov"].as_f64())
        .unwrap_or(std::f64::consts::FRAC_PI_3) as f32;
    2.0 * ((h * 0.5).tan() / 1.702_128).atan()
}

fn setup(
    mut commands: Commands,
    launch: Res<Launch>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut physics: NonSendMut<physics::Physics>,
    mut ai_stats: ResMut<ai::AiStats>,
    mut menu: ResMut<menu::MenuState>,
    mut anim_lib: ResMut<skin::AnimLibrary>,
    mut color_materials: ResMut<Assets<ColorMaterial>>,
    mut binds: ResMut<Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>>,
    mut images: ResMut<Assets<Image>>,
    mut game_materials: ResMut<Assets<gamematerial::GameMaterial>>,
) {
    let t = std::time::Instant::now();
    let scene = scene::read_scene(&launch.scene);
    let mut lib = scene::Library { dir: launch.scene.clone(), ..default() };
    let data = gamedata::GameData::load(&launch.scene);
    anim_lib.dir = launch.scene.clone();
    let mut skin_ctx = level::SkinCtx { lib: &mut anim_lib, binds: &mut binds };
    lib.skip_landscape_visual = !launch.std_materials;
    let info = level::spawn(&mut commands, &scene, &data, &mut lib, &mut physics, &assets, &mut meshes, &mut materials, &mut skin_ctx);
    // the landscape with the original's materials (rendering piece 1, docs/remaster-engine.md)
    if !launch.std_materials {
        let mut lights = gamematerial::GameLights::from_scene(&scene);
        let white = images.add(Image::new_fill(
            bevy::render::render_resource::Extent3d { width: 1, height: 1, depth_or_array_layers: 1 },
            bevy::render::render_resource::TextureDimension::D2,
            &[255, 255, 255, 255],
            bevy::render::render_resource::TextureFormat::Rgba8UnormSrgb,
            bevy::asset::RenderAssetUsages::RENDER_WORLD,
        ));
        lights.white = white.clone();
        let parts = lib.load_game_parts(&scene.landscape, &lights, &assets, &mut meshes, &mut game_materials, &white);
        let e = commands.spawn((Transform::from_matrix(Mat4::from_quat(level::game_to_engine())), Visibility::default(), Name::new("landscape"))).id();
        for (i, (mesh, mat)) in parts.into_iter().enumerate() {
            let c = commands.spawn((Mesh3d(mesh), MeshMaterial3d(mat), Name::new(format!("lsub{i}_")))).id();
            commands.entity(e).add_child(c);
        }
        commands.insert_resource(lights);
    }
    // every model an fx script can create (0x05 / 0x13 by template name), loaded now rather than on first use
    let mut fx_models = 0;
    for script in data.fx.values() {
        for ins in script {
            let a = ins.as_array();
            let op = a.and_then(|a| a.first()).and_then(|v| v.as_str());
            if matches!(op, Some("0x05") | Some("0x13")) {
                let model = a.and_then(|a| a.get(1)).and_then(|v| v.as_str()).and_then(|t| data.templates.get(&t.to_lowercase())).and_then(|t| t.model.clone());
                if let Some(m) = model {
                    if !lib.models.contains_key(&m) && lib.load_model(&m, false, &assets, &mut meshes, &mut materials).is_some() {
                        fx_models += 1;
                    }
                }
            }
        }
    }
    info!("fx: {fx_models} effect models preloaded");
    info!(
        "{}: {} objects of {} models, {} draws before batching, {} triangles; Jolt: {} static bodies ({} triangles), {} dynamic; {} grunts, {} destructibles; {:.0} ms",
        info.name,
        info.instances,
        info.models,
        info.draws,
        info.triangles,
        info.static_bodies,
        info.static_triangles,
        physics.bodies.len(),
        info.grunts,
        info.destructibles,
        t.elapsed().as_secs_f32() * 1000.0
    );
    ai_stats.enabled = !launch.no_ai;

    // start at gameplay height: 45 units above the ground, looking across the level
    let mut start = info.center + Vec3::new(0.0, 0.0, info.size.y * 0.25);
    start.y = physics.raycast(start.with_y(3000.0), -Vec3::Y, 6000.0).map(|h| h.1.y).unwrap_or(info.center.y) + 45.0;
    let cam = commands.spawn((
        Camera3d::default(),
        // the original's follow camera: Camera_end's horizontal FOV (player.json, 60 degrees) made vertical for its
        // letterboxed 1.702 view, 2 * atan(tan(h / 2) / 1.702128) (docs/subsystems/camera.md), 0.654 rad; the
        // remaster keeps that vertical FOV (wider screens see a little more at the sides)
        Projection::Perspective(PerspectiveProjection { fov: original_vertical_fov(&launch.scene), ..default() }),
        Transform::from_translation(start).looking_at(info.center.with_y(start.y - 20.0), Vec3::Y),
        {
            let d = (info.center.with_y(start.y - 20.0) - start).normalize();
            flycam::FlyCam { yaw: (-d.x).atan2(-d.z), pitch: d.y.asin(), speed: 60.0 }
        },
        AmbientLight { color: Color::srgb(0.75, 0.8, 0.9), brightness: 700.0, ..default() },
        SpatialListener::new(2.0),
    )).id();
    // playing as Roman: the camera follows him; the free camera stays for the benchmark and fx tests
    if info.player.is_some() && launch.bench.is_none() && !launch.fx_test {
        commands.entity(cam).insert(player::FollowCam).remove::<flycam::FlyCam>();
    }
    // The level's own linear fog; the sky (behaviour background, fog 0) is drawn without it.
    if let Some((near, far, color)) = info.fog {
        commands.insert_resource(ClearColor(color));
        commands.entity(cam).insert(bevy::pbr::DistanceFog { color, falloff: bevy::pbr::FogFalloff::Linear { start: near, end: far }, ..default() });
    }
    if let Some((secs, out)) = &launch.bench {
        menu.stats = true;
        let mut b = bench::Bench::new(*secs, out.clone(), info.center, info.size.x.min(info.size.y) * 0.3, 45.0);
        b.events = vec![(0.30, "airstrike"), (0.45, "airstrike"), (0.60, "kegs"), (0.75, "airstrike"), (0.85, "airstrike")];
        for (k, v) in [
            ("engine", serde_json::Value::from("bevy 0.20 + jolt 5.6")),
            ("level", info.name.clone().into()),
            ("resolution", format!("{}x{}", launch.size.0, launch.size.1).into()),
            ("instances", info.instances.into()),
            ("models", info.models.into()),
            ("draws_before_batching", info.draws.into()),
            ("triangles", info.triangles.into()),
            ("static_bodies", info.static_bodies.into()),
            ("static_triangles", info.static_triangles.into()),
            ("grunts", info.grunts.into()),
            ("destructibles", info.destructibles.into()),
            ("ai", (!launch.no_ai).into()),
        ] {
            b.info.insert(k.into(), v);
        }
        commands.insert_resource(b);
    }
    let has_player = info.player.is_some();
    commands.insert_resource(CameraFxPending(info.camera_fx.clone(), cam, start));
    commands.insert_resource(info);
    commands.insert_resource(lib);
    commands.insert_resource(data);
    commands.insert_resource(audio::SoundBank::load(&launch.scene));
    if launch.no_hud {
        menu.stats = false;
    }
    if has_player && launch.bench.is_none() && !launch.fx_test && !launch.no_hud {
        if hud::spawn(&mut commands, &launch.scene, &assets, &mut meshes, &mut color_materials).is_none() {
            warn!("hud: ui/layout.json missing (run tools/remaster_export_ui.py)");
        } else {
            gamemenu::spawn(&mut commands, &assets, &mut meshes, &mut color_materials, &launch.scene);
        }
        menu.stats = false;
        match mission::Mission::load(&launch.scene) {
            Some(m) => commands.insert_resource(m),
            None => warn!("mission: mission.json missing (run tools/remaster_export_mission.py)"),
        }
    }
}

/// Benchmark scenario: airstrikes on the point the camera looks at, and every powder keg set off.
fn bench_scenario(
    mut commands: Commands,
    mut events: MessageReader<bench::BenchEvent>,
    bench: Res<bench::Bench>,
    mut physics: NonSendMut<physics::Physics>,
    bombs: Res<player::BombAssets>,
    data: Res<gamedata::GameData>,
    mut rng: ResMut<fx::FxStats>,
    mut objects: Query<(&mut fx::Destructible, &GlobalTransform)>,
) {
    for ev in events.read() {
        match ev.0 {
            "airstrike" => {
                let target = physics.raycast(bench.look + Vec3::Y * 400.0, -Vec3::Y, 1000.0).map(|h| h.1).unwrap_or(bench.look);
                for i in 0..10 {
                    let a = i as f32 / 10.0 * std::f32::consts::TAU + rng.rand();
                    let at = target + Vec3::new(a.cos(), 0.0, a.sin()) * (10.0 + 60.0 * rng.rand()) + Vec3::Y * (100.0 + 40.0 * rng.rand());
                    player::spawn_bomb(&mut commands, &mut physics, &bombs, &data, at, Vec3::Y * -40.0, 2.6 + 0.1 * i as f32);
                }
            }
            // set off the exploding prop nearest the camera's target: the rest follows by the game's chain rules
            "kegs" => {
                let near = objects
                    .iter_mut()
                    .filter(|(d, _)| d.area_damage > 0.0 && d.dying.is_none())
                    .min_by(|a, b| a.1.translation().distance(bench.look).total_cmp(&b.1.translation().distance(bench.look)));
                if let Some((mut d, _)) = near {
                    d.health = 0.0;
                    d.dying = Some(d.start_delay);
                }
            }
            _ => {}
        }
    }
}

/// The benchmark flies at gameplay height: `height` above the ground under the camera (smoothed), looking
/// slightly down along its path.
fn bench_ground(physics: NonSend<physics::Physics>, mut bench: ResMut<bench::Bench>, mut cam: Query<&mut Transform, With<Camera3d>>, mut ground: Local<Option<f32>>) {
    let Ok(mut t) = cam.single_mut() else { return };
    let probe = Vec3::new(t.translation.x, 2000.0, t.translation.z);
    let Some(hit) = physics.raycast(probe, -Vec3::Y, 4000.0).map(|h| h.1.y) else { return };
    let g = ground.get_or_insert(hit);
    *g += (hit - *g) * 0.05;
    t.translation.y = *g + bench.height;
    let look = Vec3::new(bench.look.x, *g + bench.height * 0.5, bench.look.z);
    *t = t.looking_at(look, Vec3::Y);
    bench.look = look;
}

/// The playtest log's state line: player, AI, fx.
fn play_state(skin: Res<skin::SkinCheck>, mut bank: Option<ResMut<audio::SoundBank>>, bot: Res<playtest::Bot>, mut st: ResMut<playtest::PlayState>, p: Res<player::PlayerStats>, ai: Res<ai::AiStats>, fx: Res<fx::FxStats>, mission: Option<ResMut<mission::Mission>>, hud: Res<hud::HudState>) {
    let m = &mut st.0;
    if let Some(mut ms) = mission {
        m.insert("mission".into(), format!("{:?}", ms.outcome).into());
        m.insert("tickers".into(), ms.tickers.into());
        m.insert("compass_tag".into(), ms.compass_tag.into());
        let log: Vec<String> = ms.log.drain(..).collect();
        if !log.is_empty() {
            let prev = m.get("events").and_then(|v| v.as_array()).cloned().unwrap_or_default();
            let mut all: Vec<serde_json::Value> = prev;
            all.extend(log.into_iter().map(serde_json::Value::from));
            m.insert("events".into(), all.into());
        }
        m.insert("message".into(), ms.showing.clone().unwrap_or_default().chars().take(80).collect::<String>().into());
    }
    m.insert("prompt".into(), hud.prompt.clone().unwrap_or_default().into());
    m.insert("skin".into(), serde_json::json!({"meshes": skin.meshes, "vertices": skin.vertices, "max_stretch": skin.max_stretch, "max_gap": skin.max_gap, "motion": skin.mean_motion}));
    m.insert("subtitle".into(), hud.subtitle.as_ref().map(|s| s.0.clone()).unwrap_or_default().into());
    if let Some(b) = bank.as_deref_mut() {
        m.insert("sounds_played".into(), b.played.into());
        let mut missing: Vec<String> = b.missing.iter().cloned().collect();
        missing.sort();
        m.insert("sounds_missing".into(), missing.into());
        let recent: Vec<String> = b.recent.drain(..).collect();
        if !recent.is_empty() {
            m.insert("sounds".into(), recent.into());
        }
    }
    m.insert("bot".into(), format!("route {} veer {:.1} goto {:?}", bot.route.len(), bot.veer, bot.goto.map(|g| [g.x as i32, g.z as i32])).into());
    let r = |v: f32| (v * 10.0).round() / 10.0;
    m.insert("pos".into(), serde_json::json!([r(p.position.x), r(p.position.y), r(p.position.z)]));
    m.insert("health".into(), r(p.health).into());
    m.insert("clip".into(), p.clip.into());
    m.insert("ammo".into(), p.ammo.into());
    m.insert("stickies".into(), p.stickies.into());
    m.insert("shots".into(), p.shots.into());
    m.insert("kills".into(), p.kills.into());
    m.insert("grunts_alive".into(), ai.alive.into());
    m.insert("grunt_shots".into(), ai.shots.into());
    m.insert("explosions".into(), fx.explosions.into());
}

/// camera_fx scripts start when play begins (the intro path and snow), not behind the title menu.
#[derive(Resource)]
struct CameraFxPending(Vec<(String, bool)>, Entity, Vec3);

fn start_camera_fx(mut commands: Commands, pending: Option<Res<CameraFxPending>>, mut fx: MessageWriter<fx::StartFx>) {
    let Some(p) = pending else { return };
    for (name, pos_only) in &p.0 {
        fx.write(fx::StartFx { name: name.clone(), at: p.2, rotation: Quat::IDENTITY, follow: Some(p.1), position_only: *pos_only });
    }
    commands.remove_resource::<CameraFxPending>();
}

/// The HUD's values from the game state.
fn hud_state(
    path: Res<campath::CameraPath>,
    mut hud: ResMut<hud::HudState>,
    state: Res<State<AppState>>,
    p: Res<player::PlayerStats>,
    roman: Query<&player::Roman>,
    menu: Res<gamemenu::GameMenu>,
    companions: Query<(&Name, &fx::Health), With<ai::Companion>>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
) {
    // cinematic (a camera path): the HUD meters are hidden (hud.md 6)
    // the pause menu keeps the HUD on screen (capture orig_ui_play-720.png)
    let shown = *state.get() == AppState::Playing || (*state.get() == AppState::Paused && menu.hud_under());
    hud.visible = shown && roman.single().is_ok() && !path.active();
    hud.health = if p.max_health > 0.0 { p.health / p.max_health } else { 0.0 };
    hud.clip = p.clip;
    hud.ammo = p.ammo;
    hud.weapon = p.weapon;
    hud.unlimited = p.unlimited;
    hud.stickies = p.stickies;
    hud.special = p.special;
    hud.topsy = p.topsy;
    for (n, h) in &companions {
        if n.as_str() == "molmer" {
            hud.jonesy_health = h.0 / 500.0;
        } else {
            hud.q_health = h.0 / 500.0;
        }
    }
    if let Ok(c) = cam.single() {
        let f = c.forward();
        hud.heading = f.x.atan2(-f.z);
    }
}

fn bench_record(mut bench: ResMut<bench::Bench>, physics: NonSend<physics::Physics>, ai: Res<ai::AiStats>, fx: Res<fx::FxStats>, vm: Res<fxvm::FxVm>) {
    bench.record("physics_ms", physics.step_ms);
    bench.record("dynamic_bodies", physics.bodies.len() as f32);
    bench.record("ai_ms", ai.ms);
    bench.record("grunts_alive", ai.alive as f32);
    bench.record("explosions", fx.explosions as f32);
    bench.record("deaths", fx.deaths as f32);
    bench.record("particles", vm.stats.particles as f32);
    bench.record("fx_objects", vm.stats.objects as f32);
    bench.record("fx_vm_ms", vm.stats.ms);
}

fn fx_test(
    mut commands: Commands,
    time: Res<Time<Real>>,
    mut test: ResMut<FxTest>,
    mut objects: Query<(Entity, &mut fx::Destructible, &GlobalTransform)>,
    mut cam: Query<&mut Transform, With<Camera3d>>,
    info: Res<level::LevelInfo>,
    mut exit: MessageWriter<AppExit>,
    mut menu: ResMut<menu::MenuState>,
    physics: NonSend<physics::Physics>,
) {
    menu.stats = true;
    if test.target.is_none() {
        // the keg (chain explosive) with the most chain neighbours
        let kegs: Vec<(Entity, Vec3)> = objects.iter().filter(|(_, d, _)| d.chain).map(|(e, _, t)| (e, t.translation())).collect();
        let best = kegs.iter().max_by_key(|(_, p)| kegs.iter().filter(|(_, q)| q.distance(*p) < 60.0).count());
        let Some(&(e, p)) = best else {
            exit.write(AppExit::Success);
            return;
        };
        test.target = Some(e);
        if let Ok(mut c) = cam.single_mut() {
            // the direction (of 16) with the longest clear line of sight from the keg, 90 units out, 30 up
            let look = p + Vec3::Y * 10.0;
            let mut best = (0.0f32, p + Vec3::Y * 120.0);
            for i in 0..16 {
                let a = i as f32 / 16.0 * std::f32::consts::TAU;
                let dir = Vec3::new(a.cos(), 0.35, a.sin()).normalize();
                let clear = physics.raycast(look, dir, 50.0).map(|h| h.1.distance(look)).unwrap_or(50.0);
                if clear > best.0 {
                    best = (clear, look + dir * (clear - 5.0).max(10.0));
                }
            }
            let _ = &info;
            info!("fx test: keg at {p}, camera at {}", best.1);
            *c = Transform::from_translation(best.1).looking_at(look, Vec3::Y);
        }
    }
    test.t += time.delta_secs();
    if test.t > 1.0 && test.t - time.delta_secs() <= 1.0 {
        if let Some(Ok((_, mut d, _))) = test.target.map(|e| objects.get_mut(e)) {
            d.health = 0.0;
            d.chain_delay = 0.0;
            d.dying = Some(0.0);
        }
    }
    let due = 0.9 + test.shot as f32 * 0.1;
    if test.t >= due && test.shot < 32 {
        let path = test.dir.join(format!("fx_{:02}.png", test.shot));
        commands.spawn(bevy::render::view::screenshot::Screenshot::primary_window()).observe(bevy::render::view::screenshot::save_to_disk(path));
        test.shot += 1;
    }
    if test.t > 5.0 {
        exit.write(AppExit::Success);
    }
}
