//! Armed and Dangerous on the remaster engine (MVP): a level from the retail data (tools/remaster_export.py),
//! Bevy 0.20 rendering, Jolt Physics 5.6, grunt AI, explosions, bevy_egui menus and a benchmark mode.
//!
//!   and_remaster [--scene DIR] [--size WxH] [--vsync] [--no-ai] [--bench SECONDS --out FILE] [--preset remaster|original]
//!
//! Presentation: graphics.rs (the Remaster preset for play; tool runs use the Original unless --preset says).
//!
//! Game-neutral modules: scene (format), physics (Jolt), fx (explosions), flycam, bench, menu.
//! Game-specific: level (A&D templates, axes, units, lights), ai (grunts), player.

mod ai;
mod alarm;
mod audio;
mod bot;
mod bench;
mod camera;
mod campath;
mod charanim;
mod flycam;
mod footik;
mod fx;
mod fxvm;
mod gamedata;
mod camdump;
mod camfade;
mod cheats;
mod cutscenes;
mod decals;
mod sticky;
mod gamemenu;
mod hitparts;
mod gamematerial;
mod graphics;
mod options;
mod gameaudio;
mod hud;
mod input;
mod stats;
mod statscreen;
mod level;
mod levelreset;
mod menu;
mod movie;
mod mission;
mod nav;
mod physics;
mod pickup;
mod pubscreen;
mod ragdoll;
mod savegame;
mod topsy;
mod turret;
mod weapontable;
mod player;
mod playtest;
mod scene;
mod sdffont;
mod skin;

use bevy::diagnostic::FrameTimeDiagnosticsPlugin;
use bevy::prelude::*;
use bevy::window::{PresentMode, WindowResolution};
use bevy_egui::EguiPlugin;
use levelreset::LevelAppExt;
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
    /// --size given: the run wants that window (tools, captures), never the fullscreen option
    size_given: bool,
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
    /// a saved game to continue (savegame.rs)
    load: Option<PathBuf>,
    /// test runs: the window opens off-screen, unfocused and behind everything, so it does not take over the desktop
    background: bool,
    /// the landscape with Bevy's standard material instead of the original's (A/B comparisons)
    std_materials: bool,
    /// debug: hide "landscape" or the placed objects whose template contains this text (render comparisons)
    debug_hide: Option<String>,
    /// no boot intro movies (a relaunch within a session: the original plays them on the first front-end load only)
    no_intro: bool,
    /// the exported movies (default: ../movies next to the scene folder; tools/remaster_export_movies.py)
    movies: Option<PathBuf>,
    /// the presentation preset for this run (graphics.rs; tool runs default to the Original)
    preset: Option<graphics::Preset>,
    /// --graphics key=value,...: single presentation settings over the preset (A/B comparisons, benches)
    graphics: Option<String>,
}

fn args() -> Args {
    let mut a = Args { scene: PathBuf::from("../work/remaster/lev_1_m1"), size: (1920, 1080), bench: None, vsync: false, no_ai: false, fx_test: None, snap: None, play: None, skip_title: false, camera: None, no_hud: false, background: false, size_given: false, load: None, std_materials: false, debug_hide: None, no_intro: false, movies: None, preset: None, graphics: None };
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
                a.size_given = true;
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
            "--load" => a.load = Some(PathBuf::from(next)),
            "--std-materials" => (a.std_materials, used) = (true, 1),
            "--debug-hide" => a.debug_hide = Some(next),
            "--no-intro" => (a.no_intro, used) = (true, 1),
            "--movies" => a.movies = Some(PathBuf::from(next)),
            "--preset" => a.preset = Some(graphics::Preset::parse(&next).expect("--preset remaster|original")),
            "--graphics" => a.graphics = Some(next),
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

fn resize_tolerant(e: &bevy::render::error_handler::RenderError, _: &mut World, _: &mut World) -> bevy::render::error_handler::RenderErrorPolicy {
    use std::sync::atomic::{AtomicU32, Ordering};
    static SEEN: AtomicU32 = AtomicU32::new(0);
    if e.description.contains("differing sizes") && SEEN.fetch_add(1, Ordering::Relaxed) < 120 {
        warn!("render: attachment size mismatch during a resize, frame skipped");
        return bevy::render::error_handler::RenderErrorPolicy::Ignore;
    }
    error!("Quitting the application due to {:?} RenderError", e.ty);
    std::process::exit(1);
}

fn main() {
    let a = args();
    let scene_dir = std::fs::canonicalize(&a.scene).unwrap_or_else(|_| panic!("scene folder {}", a.scene.display()));
    let bench = a.bench.is_some();
    let tool_run = a.background || a.size_given || a.snap.is_some() || bench || a.camera.is_some() || a.play.is_some();
    // presentation: chosen before the app is built (the default sampler and the texture set depend on it)
    // a script's "preset" (playtest.rs) plays as the player's launch: chosen by the options, not pinned (--preset still
    // pins)
    let script_preset = if a.preset.is_none() { a.play.as_deref().and_then(playtest::script_preset) } else { None };
    let mut start_options = options::GameOptions::load();
    if let Some(p) = script_preset {
        start_options.graphics_preset = p;
        start_options.graphics.clear();
    }
    let gfx = graphics::GraphicsPlugin::new(graphics::GraphicsLaunch::new(a.preset, tool_run && script_preset.is_none(), a.graphics.as_deref()), &start_options, &scene_dir);
    let mut app = App::new();
    app.add_plugins(
        DefaultPlugins
            .set(gfx.image_plugin())
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
    // a window resize (the Resolution / Fullscreen options) leaves the HUD camera's depth one frame behind its
    // colour target: wgpu reports "Attachments have differing sizes" for that frame. Bevy's default handler quits
    // on any render error; this one renders on through that transient (at most 120 times, then it quits as before)
    .insert_resource(bevy::render::error_handler::RenderErrorHandler(resize_tolerant))
    .add_plugins(levelreset::LevelResetPlugin)
    .add_plugins((playtest::PlaytestPlugin, bot::BotPlugin))
    // (bot.rs is another agent's: its state is registered as the level's here)
    .init_level_resource::<bot::Bot>()
    .add_plugins(audio::AudioBankPlugin)
    .add_plugins(pickup::PickupPlugin)
    .add_plugins((FrameTimeDiagnosticsPlugin::default(), flycam::FlyCamPlugin, bench::BenchPlugin))
    .add_plugins((physics::PhysicsPlugin, fx::FxPlugin, ai::AiPlugin, player::PlayerPlugin, menu::MenuPlugin, level::LevelPlugin, fxvm::FxVmPlugin, ai::CompanionPlugin, skin::SkinPlugin, charanim::CharAnimPlugin, hud::HudPlugin, mission::MissionPlugin, gameaudio::GameAudioPlugin, campath::CameraPathPlugin, gamemenu::GameMenuPlugin))
    .add_plugins(options::OptionsPlugin)
    .add_plugins(gfx)
    .add_plugins(input::InputPlugin)
    .add_plugins(alarm::AlarmPlugin)
    .add_plugins(stats::StatsPlugin)
    .add_plugins(statscreen::StatScreenPlugin)
    .add_plugins(gamematerial::GameMaterialPlugin)
    .add_plugins(sdffont::SdfFontPlugin)
    .add_plugins(pubscreen::PubScreenPlugin)
    .add_plugins(topsy::TopsyPlugin)
    .add_plugins(turret::TurretPlugin)
    .init_state::<AppState>()
    .insert_non_send(physics::Physics::new(level::METERS_PER_UNIT))
    .insert_resource(ClearColor(Color::srgb(0.62, 0.70, 0.80)))
    .insert_resource(Launch { fx_test: a.fx_test.is_some(), scene: scene_dir.clone(), bench: a.bench, size: a.size, no_ai: a.no_ai, no_hud: a.no_hud, std_materials: a.std_materials })
    .add_systems(Update, fx_test.run_if(resource_exists::<FxTest>))
    .add_level_spawner(setup)
    .add_systems(Update, (play_state, hud_state))
    .add_systems(Update, start_camera_fx.run_if(playing))
    .add_systems(Update, (bench_scenario, bench_record).run_if(resource_exists::<bench::Bench>))
    .add_systems(Update, bench_ground.after(bench::BenchSet).run_if(resource_exists::<bench::Bench>));
    if let Some(p) = script_preset {
        app.add_systems(PreStartup, move |mut o: ResMut<options::GameOptions>| {
            o.graphics_preset = p;
            o.graphics.clear();
        });
    }
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
    app.add_plugins(camera::CameraPlugin);
    app.add_plugins(footik::FootIkPlugin);
    app.add_plugins(savegame::SaveGamePlugin);
    app.add_plugins(cheats::CheatsPlugin);
    app.add_plugins(decals::DecalPlugin);
    app.add_plugins(sticky::StickyPlugin);
    app.add_plugins(camfade::CameraFadePlugin);
    app.add_plugins(ragdoll::RagdollPlugin);
    // movies (movie.rs) and when the game plays them (cutscenes.rs): the boot intro before the title menu, unless a
    // tool run or a relaunch; a title script asks for it with "intro"
    app.add_plugins((movie::MoviePlugin, cutscenes::CutscenesPlugin));
    let movies_dir = a.movies.clone().unwrap_or_else(|| scene_dir.parent().map(|p| p.join("movies")).unwrap_or_else(|| PathBuf::from("movies")));
    app.insert_resource(movie::MovieSettings { dir: movies_dir, ..default() });
    let script_intro = a.play.as_deref().is_some_and(|p| playtest::Playtest::load_intro(p));
    let mut cut = cutscenes::Cutscenes::from_scene(&scene_dir, (!tool_run && !a.no_intro) || script_intro);
    cut.from_save = a.load.is_some() || a.play.as_deref().and_then(playtest::script_load).is_some();
    app.insert_resource(cut);
    // a script's own "load" (a state a 'save' step wrote) when --load is not given
    let load = a.load.clone().or_else(|| a.play.as_deref().and_then(playtest::script_load));
    if let Some(save) = load.as_ref().and_then(|p| savegame::read(p)) {
        app.insert_resource(savegame::PendingLoad(save, 0));
        app.insert_resource(campath::CameraPath::skipping());
        app.insert_state(AppState::Playing);
    }
    // tool runs (off-screen, an explicit --size, snaps, benches, camera dumps, scripts) keep their window: the
    // player's fullscreen option is for playing
    if tool_run {
        app.insert_resource(options::Windowed);
    }
    // background test runs are silent (playback still runs: positions advance, the audio check counts them)
    if a.background {
        app.add_systems(PostStartup, |mut m: ResMut<audio::Mixer>| m.master = 0.0);
    }
    // debug: NEAR_DUMP=1 prints the named entities within 45 units of the camera once, 5 s into play
    if std::env::var("NEAR_DUMP").is_ok() {
        app.add_systems(Update, |time: Res<Time>, mut done: Local<bool>, cam: Query<&GlobalTransform, With<Camera3d>>, q: Query<(&Name, &GlobalTransform)>, meshes: Query<(Entity, &GlobalTransform, Option<&ChildOf>, Has<MeshMaterial3d<StandardMaterial>>, Has<MeshMaterial3d<gamematerial::GameMaterial>>, Has<MeshMaterial3d<fxvm::FxLayerMaterial>>), With<Mesh3d>>, gm: Query<&MeshMaterial3d<gamematerial::GameMaterial>>, gma: Res<Assets<gamematerial::GameMaterial>>| {
            if *done || time.elapsed_secs() < std::env::var("NEAR_DUMP").ok().and_then(|v| v.parse().ok()).unwrap_or(9.0) { return; }
            *done = true;
            let Ok(c) = cam.single() else { return };
            for (n, t) in &q { let d = t.translation().distance(c.translation()); if d < 45.0 { info!("NEAR {} {:.1} {:?} {:?}", n, d, t.translation(), t.compute_transform().scale); } } for (e, t, parent, std, game, layer) in &meshes { let d = t.translation().distance(c.translation()); if d < 20.0 { let pn = parent.and_then(|p| q.get(p.parent()).ok()).map(|(n, _)| n.to_string()).unwrap_or_default(); let gp = gm.get(e).ok().and_then(|h| gma.get(&h.0)).map(|m| format!("kind {} em {:?} diff {:?} fog {:?} alpha {}", m.params.kind, m.params.emissive, m.params.diffuse, m.params.fog_range, m.alpha)).unwrap_or_default(); info!("NEARMESH {e:?} {:.1} parent {} std {} game {} layer {} {}", d, pn, std, game, layer, gp); } }
        });
    }
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
        // game time advances exactly 1/60 s per frame (real and virtual): a slow frame (a screenshot, loading) no
        // longer jumps every timer at once, and runs compare frame for frame with the original's captures, which
        // advance one 60 Hz step per frame (fight2E: grunts fired in lockstep after each screenshot's stall)
        app.insert_resource(bevy::time::TimeUpdateStrategy::ManualDuration(std::time::Duration::from_secs_f64(1.0 / 60.0)));
        // the same system order every run: many unordered systems draw from the shared random stream (FxStats), so
        // the multi-threaded executor's run-to-run order changed which grunt shot when (player_move, 2026-10-15)
        app.edit_schedule(Update, |s| {
            s.set_executor(bevy::ecs::schedule::SingleThreadedExecutor::new());
        });
    }
    // --snap FILE: a screenshot of whatever shows after 4 s (the title menu without other options), then exit.
    // AND_SNAP_DELAY=SECONDS waits longer (a busy machine loads the level's objects later than 4 s)
    if let Some(path) = a.snap {
        let delay: f32 = std::env::var("AND_SNAP_DELAY").ok().and_then(|v| v.parse().ok()).unwrap_or(4.0);
        app.add_systems(Update, move |mut commands: Commands, time: Res<Time<Real>>, mut done: Local<u8>, mut exit: MessageWriter<AppExit>| {
            let t = time.elapsed_secs();
            if t > delay && *done == 0 {
                commands.spawn(bevy::render::view::screenshot::Screenshot::primary_window()).observe(bevy::render::view::screenshot::save_to_disk(path.clone()));
                *done = 1;
            } else if t > delay + 1.5 {
                exit.write(AppExit::Success);
            }
        });
    }
    // a failed playtest expect exits with its code (tools/playtest.py reads it)
    if let AppExit::Error(c) = app.run() {
        std::process::exit(c.get() as i32);
    }
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
    mut font_materials: ResMut<Assets<sdffont::FontMaterial>>,
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
            // the landscape casts no shadow (shadows.md: only objects with shadow_enable cast, projected silhouettes)
            let c = commands.spawn((Mesh3d(mesh), MeshMaterial3d(mat), Name::new(format!("lsub{i}_")), bevy::light::NotShadowCaster)).id();
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
        // remaster keeps that vertical FOV (wider screens see a little more at the sides); near 1 as the original's
        // (camera dump: near 1), which also clips the radius-1 fxo_snow dome as there
        Projection::Perspective(PerspectiveProjection { fov: original_vertical_fov(&launch.scene), near: 1.0, ..default() }),
        Transform::from_translation(start).looking_at(info.center.with_y(start.y - 20.0), Vec3::Y),
        {
            let d = (info.center.with_y(start.y - 20.0) - start).normalize();
            flycam::FlyCam { yaw: (-d.x).atan2(-d.z), pitch: d.y.asin(), speed: 60.0 }
        },
        AmbientLight { color: Color::srgb(0.75, 0.8, 0.9), brightness: 700.0, ..default() },
        SpatialListener::new(2.0),
        // no tonemapping: the original writes its colours as they are (Bevy's default curve darkened the pale sky
        // near the horizon from 216 to ~175 and the horizon band from 212 to 179; synced walk frame 520)
        bevy::core_pipeline::tonemapping::Tonemapping::None,
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
        if hud::spawn(&mut commands, &launch.scene, &assets, &mut meshes, &mut color_materials, (&mut images, &mut font_materials)).is_none() {
            warn!("hud: ui/layout.json missing (run tools/remaster_export_ui.py)");
        } else {
            gamemenu::spawn(&mut commands, &assets, &mut meshes, &mut color_materials, &launch.scene);
        }
        menu.stats = false;
        commands.insert_resource(alarm::Markers::load(&launch.scene));
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
fn play_state(audio_chk: Res<audio::AudioCheck>, skin: Res<skin::SkinCheck>, mut bank: Option<ResMut<audio::SoundBank>>, bot: Res<bot::Bot>, mut st: ResMut<playtest::PlayState>, p: Res<player::PlayerStats>, ai: Res<ai::AiStats>, fx: Res<fx::FxStats>, mission: Option<ResMut<mission::Mission>>, hud: Res<hud::HudState>, mut seen: levelreset::PerLevel<usize>) {
    let m = &mut st.0;
    if let Some(mut ms) = mission {
        m.insert("mission".into(), format!("{:?}", ms.outcome).into());
        m.insert("tickers".into(), ms.tickers.into());
        m.insert("compass_tag".into(), ms.compass_tag.into());
        // (read, not drained: the session event log reads the same list)
        let seen = seen.get();
        let log: Vec<String> = ms.log.iter().skip(*seen).cloned().collect();
        *seen = ms.log.len();
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
    m.insert("audio_check".into(), serde_json::json!({"advancing": audio_chk.advancing, "stalled": audio_chk.stalled, "silent": audio_chk.silent, "heard": audio_chk.heard.len(), "voice_heard": audio_chk.voice_heard}));
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
    m.insert("bot".into(), format!("{} seen {} route {} veer {:.1} goto {:?}", bot.mode, bot.threats.len(), bot.route.len(), bot.veer, bot.goto.map(|g| [g.x as i32, -g.z as i32])).into());
    let r = |v: f32| (v * 10.0).round() / 10.0;
    m.insert("pos".into(), serde_json::json!([r(p.position.x), r(p.position.y), r(p.position.z)]));
    m.insert("health".into(), r(p.health).into());
    m.insert("clip".into(), p.clip.into());
    m.insert("ammo".into(), p.ammo.into());
    m.insert("stickies".into(), p.stickies.into());
    m.insert("shots".into(), p.shots.into());
    m.insert("kills".into(), p.kills.into());
    m.insert("head_shots".into(), p.head_shots.into());
    m.insert("grunts_alive".into(), ai.alive.into());
    m.insert("grunt_targets".into(), serde_json::json!(ai.targets));
    m.insert("grunt_shots".into(), ai.shots.into()); m.insert("ai_ms".into(), serde_json::json!(ai.ms));
    m.insert("explosions".into(), fx.explosions.into());
}

/// camera_fx scripts start when play begins (the intro path and snow), not behind the title menu.
#[derive(Resource)]
struct CameraFxPending(Vec<(String, bool)>, Entity, Vec3);

fn start_camera_fx(mut commands: Commands, pending: Option<Res<CameraFxPending>>, mut fx: MessageWriter<fx::StartFx>, load: Option<Res<savegame::PendingLoad>>) {
    let Some(p) = pending else { return };
    for (name, pos_only) in &p.0 {
        // a loaded game skips the intro overlay (the camera fx locked to the full camera transform: weather.md)
        if load.is_some() && !*pos_only {
            continue;
        }
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
    hud.visible = shown && roman.single().is_ok();
    hud.cinematic = path.active();
    hud.health = if p.max_health > 0.0 { p.health / p.max_health } else { 0.0 };
    hud.clip = p.clip;
    hud.ammo = p.ammo;
    hud.weapon = p.weapon;
    hud.unlimited = p.unlimited;
    hud.stickies = p.stickies;
    hud.special = p.special;
    hud.topsy = p.topsy;
    // the weapon-cycle arrows (hud.rs, 0x00463fa0): the rifle and the machine gun once carried; the sticky bombs and
    // the Topsy Turvy bomb while any are left (player.rs cycles to it only then)
    if let Ok(r) = roman.single() {
        hud.weapons = 1 + r.mg.is_some() as u32;
        hud.specials = 1 + (r.topsy > 0) as u32;
    }
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
