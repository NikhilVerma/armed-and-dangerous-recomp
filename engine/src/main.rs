//! Armed and Dangerous on the remaster engine (MVP): one level's landscape and placed objects from the retail
//! data (tools/remaster_export.py), lit by the level's own lights, with a free camera and a benchmark flight.
//!
//!   and_remaster [--scene DIR] [--size WxH] [--bench SECONDS --out FILE] [--vsync]
//!
//! Game-neutral pieces: scene.rs (scene format), flycam.rs, bench.rs. Game-specific: this file (axes, lights).

mod bench;
mod flycam;
mod scene;

use bevy::diagnostic::{DiagnosticsStore, FrameTimeDiagnosticsPlugin};
use bevy::light::CascadeShadowConfigBuilder;
use bevy::prelude::*;
use bevy::window::{PresentMode, WindowResolution};
use std::path::PathBuf;

struct Args {
    scene: PathBuf,
    size: (u32, u32),
    bench: Option<(f32, PathBuf)>,
    vsync: bool,
}

fn args() -> Args {
    let mut a = Args {
        scene: PathBuf::from("../work/remaster/lev_1_m1"),
        size: (1920, 1080),
        bench: None,
        vsync: false,
    };
    let v: Vec<String> = std::env::args().collect();
    let mut i = 1;
    let mut bench_secs = None;
    let mut out = PathBuf::from("../work/remaster/bench/latest.json");
    while i < v.len() {
        let next = |i: usize| v.get(i + 1).cloned().unwrap_or_default();
        match v[i].as_str() {
            "--scene" => a.scene = PathBuf::from(next(i)),
            "--size" => {
                let s = next(i);
                let (w, h) = s.split_once('x').expect("--size WxH");
                a.size = (w.parse().unwrap(), h.parse().unwrap());
            }
            "--bench" => bench_secs = Some(next(i).parse::<f32>().expect("--bench SECONDS")),
            "--out" => out = PathBuf::from(next(i)),
            "--vsync" => {
                a.vsync = true;
                i -= 1;
            }
            other => panic!("unknown argument {other}"),
        }
        i += 2;
    }
    a.bench = bench_secs.map(|s| (s, out));
    a
}

/// Game axes (Z up, as authored in 3ds Max) to the engine's (Y up): a rotation, so handedness is kept.
fn game_to_engine() -> Quat {
    Quat::from_rotation_x(-std::f32::consts::FRAC_PI_2)
}

fn to_engine(p: Vec3) -> Vec3 {
    game_to_engine() * p
}

#[derive(Resource)]
struct Launch {
    scene: PathBuf,
    bench: Option<(f32, PathBuf)>,
    size: (u32, u32),
}

#[derive(Component)]
struct FpsText;

const HELPERS: &[&str] = &["marker", "camera_fx", "bb_", "bigredarrow"];

fn main() {
    let a = args();
    let scene_dir = std::fs::canonicalize(&a.scene).unwrap_or_else(|_| panic!("scene folder {}", a.scene.display()));
    App::new()
        .add_plugins(
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
        .add_plugins((FrameTimeDiagnosticsPlugin::default(), flycam::FlyCamPlugin, bench::BenchPlugin))
        .insert_resource(ClearColor(Color::srgb(0.62, 0.70, 0.80)))
        .insert_resource(Launch { scene: scene_dir, bench: a.bench, size: a.size })
        .add_systems(Startup, setup)
        .add_systems(Update, fps_text)
        .run();
}

fn setup(
    mut commands: Commands,
    launch: Res<Launch>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
) {
    let scene = scene::read_scene(&launch.scene);
    let mut lib = scene::Library { dir: launch.scene.clone(), ..default() };

    // Game-axis transforms are turned into engine axes (Z up -> Y up) as they are spawned.
    let axes = Mat4::from_quat(game_to_engine());
    let mut triangles = 0usize;
    let mut parts = 0usize;
    let mut spawn_model = |commands: &mut Commands, lib: &mut scene::Library, name: &str, m: Mat4| -> bool {
        // Editor helpers the game never draws (markers, trigger boxes, camera rigs).
        if HELPERS.iter().any(|h| name.starts_with(h)) {
            return false;
        }
        let unlit = name.contains("_sky");
        let Some(model) = lib.load_model(name, unlit, &assets, &mut meshes, &mut materials) else {
            return false;
        };
        triangles += model.triangles;
        parts += model.parts.len();
        let children: Vec<_> = model
            .parts
            .iter()
            .map(|(mesh, mat)| (Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone())))
            .collect();
        let e = commands.spawn((Transform::from_matrix(m), Visibility::default(), Name::new(name.to_owned()))).id();
        for c in children {
            let child = commands.spawn(c).id();
            commands.entity(e).add_child(child);
        }
        true
    };
    spawn_model(&mut commands, &mut lib, &scene.landscape, axes);
    let mut placed = 0;
    for inst in &scene.instances {
        // Invisible walls only block movement (physics); the game never draws them.
        if inst.property.contains("invisible") {
            continue;
        }
        if spawn_model(&mut commands, &mut lib, &inst.model, axes * scene::game_matrix(&inst.transform)) {
            placed += 1;
        }
    }
    info!(
        "{}: {} instances of {} models, {} draws (before batching), {} triangles",
        scene.level,
        placed,
        lib.models.len(),
        parts,
        triangles
    );

    // The level's directional lights (lighttype "dir"): the light shines down its local -Z axis.
    let (lo, hi) = (Vec3::from(scene.bounds[0]), Vec3::from(scene.bounds[1]));
    let extent = (hi - lo).length();
    let mut first = true;
    for l in &scene.lights {
        let p = |k: &str| l.props.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
        if l.props.get("lighttype").and_then(|v| v.as_str()) != Some("dir") || p("lighton") == 0.0 {
            continue;
        }
        let m = scene::game_matrix(&l.transform);
        let dir = to_engine(-m.z_axis.truncate().normalize());
        let color = Color::srgb(p("redvalue") / 255.0, p("greenvalue") / 255.0, p("bluevalue") / 255.0);
        let ambient_only = p("ambientonly") != 0.0;
        if ambient_only {
            continue;
        }
        commands.spawn((
            DirectionalLight {
                color,
                illuminance: 9000.0 * p("intensity").max(0.1),
                shadow_maps_enabled: first,
                ..default()
            },
            Transform::from_translation(Vec3::ZERO).looking_to(dir, Vec3::Y),
            CascadeShadowConfigBuilder { maximum_distance: extent * 0.5, first_cascade_far_bound: 60.0, ..default() }.build(),
        ));
        first = false;
    }
    if first {
        // no usable light in the level: a default sun
        commands.spawn((
            DirectionalLight { illuminance: 9000.0, shadow_maps_enabled: true, ..default() },
            Transform::from_xyz(0.0, 1.0, 0.0).looking_to(Vec3::new(-0.4, -1.0, -0.3), Vec3::Y),
        ));
    }

    // Camera: over the middle of the level, looking across it.
    let center = to_engine((lo + hi) * 0.5);
    let start = center + Vec3::new(0.0, 120.0, (hi - lo).y * 0.35);
    commands.spawn((
        Camera3d::default(),
        Transform::from_translation(start).looking_at(center, Vec3::Y),
        flycam::FlyCam { yaw: 0.0, pitch: -0.35, speed: 60.0 },
        AmbientLight { color: Color::srgb(0.75, 0.8, 0.9), brightness: 700.0, ..default() },
    ));
    if let Some((secs, out)) = &launch.bench {
        let mut b = bench::Bench::new(*secs, out.clone(), center, (hi - lo).x.min((hi - lo).y) * 0.35, 160.0);
        b.info.insert("engine".into(), "bevy 0.20".into());
        b.info.insert("level".into(), scene.level.clone().into());
        b.info.insert("resolution".into(), format!("{}x{}", launch.size.0, launch.size.1).into());
        b.info.insert("instances".into(), placed.into());
        b.info.insert("models".into(), lib.models.len().into());
        b.info.insert("draws_before_batching".into(), parts.into());
        b.info.insert("triangles".into(), triangles.into());
        commands.insert_resource(b);
    }
    commands.spawn((
        Text::new("fps"),
        TextFont { font_size: FontSize::Px(18.0), ..default() },
        TextColor(Color::WHITE),
        Node { position_type: PositionType::Absolute, top: Val::Px(8.0), left: Val::Px(8.0), ..default() },
        FpsText,
    ));
    commands.insert_resource(lib);
}

fn fps_text(diag: Res<DiagnosticsStore>, mut q: Query<&mut Text, With<FpsText>>) {
    let fps = diag.get(&FrameTimeDiagnosticsPlugin::FPS).and_then(|d| d.smoothed()).unwrap_or(0.0);
    let ms = diag.get(&FrameTimeDiagnosticsPlugin::FRAME_TIME).and_then(|d| d.smoothed()).unwrap_or(0.0);
    for mut t in &mut q {
        t.0 = format!("{fps:.0} fps  {ms:.2} ms   RMB look, WASD move, Q/E down/up, Shift fast");
    }
}
