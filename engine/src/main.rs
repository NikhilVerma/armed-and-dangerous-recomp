//! Armed and Dangerous on the remaster engine (MVP): a level from the retail data (tools/remaster_export.py),
//! Bevy 0.20 rendering, Jolt Physics 5.6, grunt AI, explosions, bevy_egui menus and a benchmark mode.
//!
//!   and_remaster [--scene DIR] [--size WxH] [--vsync] [--no-ai] [--bench SECONDS --out FILE]
//!
//! Game-neutral modules: scene (format), physics (Jolt), fx (explosions), flycam, bench, menu.
//! Game-specific: level (A&D templates, axes, units, lights), ai (grunts), player.

mod ai;
mod bench;
mod flycam;
mod fx;
mod gamedata;
mod level;
mod menu;
mod physics;
mod player;
mod scene;

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
}

fn args() -> Args {
    let mut a = Args { scene: PathBuf::from("../work/remaster/lev_1_m1"), size: (1920, 1080), bench: None, vsync: false, no_ai: false };
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
            other => panic!("unknown argument {other}"),
        }
        i += used;
    }
    a.bench = bench_secs.map(|s| (s, out));
    a
}

#[derive(Resource)]
struct Launch {
    scene: PathBuf,
    bench: Option<(f32, PathBuf)>,
    size: (u32, u32),
    no_ai: bool,
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
                    ..default()
                }),
                ..default()
            })
            .set(AssetPlugin { file_path: scene_dir.to_string_lossy().into_owned(), ..default() }),
    )
    .add_plugins(EguiPlugin::default())
    .add_plugins((FrameTimeDiagnosticsPlugin::default(), flycam::FlyCamPlugin, bench::BenchPlugin))
    .add_plugins((physics::PhysicsPlugin, fx::FxPlugin, ai::AiPlugin, player::PlayerPlugin, menu::MenuPlugin, level::LevelPlugin))
    .init_state::<AppState>()
    .insert_non_send(physics::Physics::new(level::METERS_PER_UNIT))
    .insert_resource(ClearColor(Color::srgb(0.62, 0.70, 0.80)))
    .insert_resource(Launch { scene: scene_dir, bench: a.bench, size: a.size, no_ai: a.no_ai })
    .add_systems(Startup, setup)
    .add_systems(Update, (bench_scenario, bench_record).run_if(resource_exists::<bench::Bench>));
    if bench {
        app.insert_state(AppState::Playing);
    }
    app.run();
}

#[allow(clippy::too_many_arguments)]
fn setup(
    mut commands: Commands,
    launch: Res<Launch>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut physics: NonSendMut<physics::Physics>,
    mut ai_stats: ResMut<ai::AiStats>,
    mut menu: ResMut<menu::MenuState>,
) {
    let t = std::time::Instant::now();
    let scene = scene::read_scene(&launch.scene);
    let mut lib = scene::Library { dir: launch.scene.clone(), ..default() };
    let data = gamedata::GameData::load(&launch.scene);
    let info = level::spawn(&mut commands, &scene, &data, &mut lib, &mut physics, &assets, &mut meshes, &mut materials);
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

    let start = info.center + Vec3::new(0.0, 120.0, info.size.y * 0.35);
    let cam = commands.spawn((
        Camera3d::default(),
        Transform::from_translation(start).looking_at(info.center, Vec3::Y),
        flycam::FlyCam { yaw: 0.0, pitch: -0.35, speed: 60.0 },
        AmbientLight { color: Color::srgb(0.75, 0.8, 0.9), brightness: 700.0, ..default() },
        ai::Player,
    )).id();
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
    commands.insert_resource(info);
    commands.insert_resource(lib);
    commands.insert_resource(data);
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

fn bench_record(mut bench: ResMut<bench::Bench>, physics: NonSend<physics::Physics>, ai: Res<ai::AiStats>, fx: Res<fx::FxStats>) {
    bench.record("physics_ms", physics.step_ms);
    bench.record("dynamic_bodies", physics.bodies.len() as f32);
    bench.record("ai_ms", ai.ms);
    bench.record("grunts_alive", ai.alive as f32);
    bench.record("explosions", fx.explosions as f32);
    bench.record("deaths", fx.deaths as f32);
}
