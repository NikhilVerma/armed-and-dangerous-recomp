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
    /// Scenario: (path position 0..1, name) fired once each as a BenchEvent, for the game to act on.
    pub events: Vec<(f32, &'static str)>,
    /// Per-frame samples the game records while measuring (physics ms, bodies, ...): averages, maxima and
    /// the series go to the JSON.
    pub samples: std::collections::BTreeMap<String, Vec<f32>>,
    pub measuring: bool,
    pub look: Vec3,
    fired: usize,
    /// Frames left that a screenshot capture slows down: not counted (the capture is the tool, not the game).
    skip_until: f32,
    pub skipped: u32,
    started: Option<f32>,
    frames: Vec<f32>,
    shots_taken: usize,
}

#[derive(Message, Clone, Copy)]
pub struct BenchEvent(pub &'static str);

impl Bench {
    pub fn record(&mut self, name: &str, v: f32) {
        if self.measuring {
            self.samples.entry(name.to_owned()).or_default().push(v);
        }
    }
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
            events: Vec::new(),
            samples: Default::default(),
            measuring: false,
            look: center,
            fired: 0,
            skip_until: 0.0,
            skipped: 0,
            started: None,
            frames: Vec::new(),
            shots_taken: 0,
        }
    }
}

/// The benchmark's camera system, so a game can adjust the camera after it.
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct BenchSet;

pub struct BenchPlugin;

impl Plugin for BenchPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<BenchEvent>().add_systems(Update, run_bench.in_set(BenchSet).run_if(resource_exists::<Bench>));
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
    mut events: MessageWriter<BenchEvent>,
) {
    let now = time.elapsed_secs();
    let t0 = *bench.started.get_or_insert(now);
    let since = now - t0;
    let u = ((since - bench.warmup).max(0.0) / bench.seconds).min(1.0);
    let (pos, look) = path(&bench, u);
    for mut t in &mut cams {
        *t = Transform::from_translation(pos).looking_at(look, Vec3::Y);
    }
    bench.look = look;
    if since < bench.warmup {
        return;
    }
    bench.measuring = true;
    while bench.fired < bench.events.len() && u >= bench.events[bench.fired].0 {
        events.write(BenchEvent(bench.events[bench.fired].1));
        bench.fired += 1;
    }
    if bench.shots_taken < SHOTS.len() && u >= SHOTS[bench.shots_taken] {
        if let Some(dir) = bench.out.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        let path = bench.out.with_extension(format!("shot{}.png", bench.shots_taken));
        commands.spawn(Screenshot::primary_window()).observe(save_to_disk(path));
        bench.shots_taken += 1;
        bench.skip_until = since + 0.3;
    }
    if since < bench.skip_until {
        bench.skipped += 1;
    } else {
        bench.frames.push(time.delta_secs() * 1000.0);
    }
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
    put("frames_skipped_for_screenshots", bench.skipped.into());
    put("seconds", (total / 1000.0).into());
    put("fps_avg", (n as f32 * 1000.0 / total).into());
    put("fps_1pct_low", low1.into());
    put("ms_median", pct(0.5).into());
    put("ms_p99", pct(0.99).into());
    put("ms_max", ms[n - 1].into());
    for (k, v) in &bench.samples {
        let n = v.len().max(1) as f32;
        out.insert(format!("{k}_avg"), (v.iter().sum::<f32>() / n).into());
        out.insert(format!("{k}_max"), v.iter().cloned().fold(0.0, f32::max).into());
        out.insert(format!("{k}_series"), serde_json::Value::from(v.iter().map(|x| (x * 100.0).round() / 100.0).collect::<Vec<_>>()));
    }
    out.insert("events".into(), serde_json::Value::from(bench.events.iter().map(|(u, n)| serde_json::json!({"at": u, "name": n})).collect::<Vec<_>>()));
    let mut put = |k: &str, v: serde_json::Value| {
        out.insert(k.into(), v);
    };
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
