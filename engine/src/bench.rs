//! Benchmark: a scripted camera flight of fixed length; frame times are recorded and written as JSON for the
//! benchmark page (tools/bench_page.py). Game-neutral: the game supplies the path's centre and size.

use bevy::prelude::*;
use bevy::render::view::screenshot::{Screenshot, save_to_disk};
use std::path::PathBuf;

#[derive(Resource)]
pub struct Bench {
    pub seconds: f32,
    pub warmup: f32,
    pub out: PathBuf,
    pub center: Vec3,
    pub radius: f32,
    pub height: f32,
    pub info: serde_json::Map<String, serde_json::Value>,
    started: Option<f32>,
    frames: Vec<f32>,
    shots_taken: usize,
}

impl Bench {
    pub fn new(seconds: f32, out: PathBuf, center: Vec3, radius: f32, height: f32) -> Self {
        Bench {
            seconds,
            warmup: 3.0,
            out,
            center,
            radius,
            height,
            info: Default::default(),
            started: None,
            frames: Vec::new(),
            shots_taken: 0,
        }
    }
}

pub struct BenchPlugin;

impl Plugin for BenchPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, run_bench.run_if(resource_exists::<Bench>));
    }
}

/// Camera position at path parameter u in [0, 1): one loop around the centre, dipping low on the far side.
pub fn path(b: &Bench, u: f32) -> (Vec3, Vec3) {
    let a = u * std::f32::consts::TAU;
    let r = b.radius * (0.55 + 0.45 * (a * 2.0).cos().abs());
    let h = b.height * (0.35 + 0.65 * (0.5 + 0.5 * (a + 1.0).cos()));
    let pos = b.center + Vec3::new(a.cos() * r, h, a.sin() * r);
    let look = b.center + Vec3::new((a + 0.6).cos() * r * 0.3, 0.0, (a + 0.6).sin() * r * 0.3);
    (pos, look)
}

/// Path positions where a screenshot is saved next to the JSON (<out>.shotN.png), for checking the image.
const SHOTS: [f32; 4] = [0.02, 0.27, 0.52, 0.77];

fn run_bench(
    mut commands: Commands,
    time: Res<Time<Real>>,
    mut bench: ResMut<Bench>,
    mut cams: Query<&mut Transform, With<Camera3d>>,
    mut exit: MessageWriter<AppExit>,
) {
    let now = time.elapsed_secs();
    let t0 = *bench.started.get_or_insert(now);
    let since = now - t0;
    let u = ((since - bench.warmup).max(0.0) / bench.seconds).min(1.0);
    let (pos, look) = path(&bench, u);
    for mut t in &mut cams {
        *t = Transform::from_translation(pos).looking_at(look, Vec3::Y);
    }
    if since < bench.warmup {
        return;
    }
    if bench.shots_taken < SHOTS.len() && u >= SHOTS[bench.shots_taken] {
        if let Some(dir) = bench.out.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        let path = bench.out.with_extension(format!("shot{}.png", bench.shots_taken));
        commands.spawn(Screenshot::primary_window()).observe(save_to_disk(path));
        bench.shots_taken += 1;
    }
    bench.frames.push(time.delta_secs() * 1000.0);
    if since < bench.warmup + bench.seconds {
        return;
    }
    let mut ms = bench.frames.clone();
    ms.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let n = ms.len().max(1);
    let total: f32 = ms.iter().sum();
    let pct = |p: f32| ms[((n as f32 * p) as usize).min(n - 1)];
    // "1% low": the average fps of the slowest 1% of frames
    let worst = &ms[n - (n / 100).max(1)..];
    let low1 = 1000.0 / (worst.iter().sum::<f32>() / worst.len() as f32);
    let mut out = bench.info.clone();
    let mut put = |k: &str, v: serde_json::Value| {
        out.insert(k.into(), v);
    };
    put("frames", n.into());
    put("seconds", (total / 1000.0).into());
    put("fps_avg", (n as f32 * 1000.0 / total).into());
    put("fps_1pct_low", low1.into());
    put("ms_median", pct(0.5).into());
    put("ms_p99", pct(0.99).into());
    put("ms_max", ms[n - 1].into());
    put("frame_ms", serde_json::Value::from(bench.frames.iter().map(|x| (x * 100.0).round() / 100.0).collect::<Vec<_>>()));
    if let Some(dir) = bench.out.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    std::fs::write(&bench.out, serde_json::to_string_pretty(&out).unwrap()).expect("bench output");
    info!(
        "bench: {} frames, {:.1} fps average, {:.1} fps 1% low -> {}",
        n,
        n as f32 * 1000.0 / total,
        low1,
        bench.out.display()
    );
    exit.write(AppExit::Success);
}
