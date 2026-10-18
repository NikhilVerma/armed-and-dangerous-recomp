//! Session recording: each launch of the game (normal play) records what happened inside its own window, for bug
//! reports and reproduction. Local files only; nothing leaves the machine. It is the game's own input as its window
//! received it, never system-wide keyboard capture.
//!
//! A session is a folder `<root>/<YYYY-MM-DD_HH-MM-SS>` (UTC), root `%LOCALAPPDATA%/AndRemaster/sessions` (else
//! `sessions/` next to the executable), holding:
//! - `session.json`: when, the command line, the version;
//! - `game.log`: every log line the game writes (info and up: warnings, errors) and panics with their backtrace
//!   (also `panic.txt`);
//! - `events.jsonl`: the log's EVENT lines (levels loaded and restarted, mission events and triggers, saves and loads,
//!   the campaign, developer toggles and actions, bookmarks) plus deaths and mission won / failed, with the time;
//! - `state.jsonl`: the playtest state log (playtest.rs `PlayState`: Roman, health, the mission, the menus, ...) twice
//!   a second;
//! - `inputs.jsonl`: per frame with input, the keys, mouse buttons, mouse motion and wheel, pad buttons, sticks and
//!   triggers the game received, and the app state's changes;
//! - `frames.jsonl`: frame-time statistics every 5 s (mean, p50, p95, p99, max, frames over 33 / 100 ms);
//! - `bookmarks/NNN/` (F8): `screenshot.png`, `save.json` (a quick save made at that moment, loadable with `--load`
//!   or a playtest's "load"), `note.txt`; and `bookmarks.jsonl`, one line each.
//!
//! Retention: the newest 20 sessions are kept, and at most 2 GiB in all (oldest deleted first; the running one never).
//! Tool runs (benchmarks, captures, playtests) record nothing unless asked: a playtest script's "session": true
//! (recorded next to the script), or AND_SESSION_DIR=<root>. `--no-session` turns it off for a launch.
//!
//! Cost: the writes are buffered and flushed once a second; the state line is serialised twice a second (the state
//! itself is built every frame already). tools/session.py lists, summarises and turns bookmarks into playtests.

use super::DevOverlay;
use crate::AppState;
use bevy::input::mouse::{AccumulatedMouseMotion, AccumulatedMouseScroll};
use bevy::log::BoxedLayer;
use bevy::log::tracing::{self, Subscriber, field::Field, field::Visit};
use bevy::log::tracing_subscriber::{Layer, layer::Context};
use bevy::prelude::*;
use bevy::render::view::screenshot::{Screenshot, save_to_disk};
use bevy_egui::{EguiContexts, EguiPrimaryContextPass, egui};
use std::fs::File;
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Mutex, OnceLock};
use std::time::Instant;

/// Sessions kept, and the size they may take together.
pub const KEEP_SESSIONS: usize = 20;
pub const KEEP_BYTES: u64 = 2 << 30;
/// Frame-time statistics period (s), state log period (s).
const FRAMES_PERIOD: f32 = 5.0;
const STATE_PERIOD: f32 = 0.5;

/// The files the log layer and the panic hook write (they run outside the ECS).
struct Shared {
    dir: PathBuf,
    start: Instant,
    log: Mutex<BufWriter<File>>,
    events: Mutex<BufWriter<File>>,
    warnings: AtomicU32,
    errors: AtomicU32,
}

static SHARED: OnceLock<Shared> = OnceLock::new();

/// Where this launch records: AND_SESSION_DIR, a playtest script's own folder ("session": true), nothing for other
/// tool runs or with `--no-session`, else the user's local app data.
pub fn root(tool_run: bool, no_session: bool, script: Option<&Path>) -> Option<PathBuf> {
    if no_session {
        return None;
    }
    if let Some(d) = std::env::var_os("AND_SESSION_DIR") {
        return Some(PathBuf::from(d));
    }
    if let Some(r) = script.and_then(crate::playtest::script_session_root) {
        return Some(r);
    }
    if tool_run {
        return None;
    }
    let base = std::env::var_os("LOCALAPPDATA").map(|d| PathBuf::from(d).join("AndRemaster")).or_else(|| std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)));
    base.map(|b| b.join("sessions"))
}

/// The UTC date and time of a Unix time, as "YYYY-MM-DD_HH-MM-SS" (days to a civil date: Howard Hinnant's algorithm).
pub fn stamp(unix_secs: u64) -> String {
    let days = (unix_secs / 86400) as i64;
    let s = unix_secs % 86400;
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + i64::from(m <= 2);
    format!("{y:04}-{m:02}-{d:02}_{:02}-{:02}-{:02}", s / 3600, s / 60 % 60, s % 60)
}

/// A session folder's name ("YYYY-MM-DD_HH-MM-SS", maybe "_N" after it): only these are ever deleted.
pub fn is_session_name(n: &str) -> bool {
    let b = n.as_bytes();
    b.len() >= 19 && b[..19].iter().enumerate().all(|(i, c)| match i {
        4 | 7 => *c == b'-',
        10 => *c == b'_',
        13 | 16 => *c == b'-',
        _ => c.is_ascii_digit(),
    }) && n[19..].chars().all(|c| c == '_' || c.is_ascii_digit())
}

/// The sessions to delete: newest first, the running one (`current`) always kept, then the others while fewer than
/// `max_n` are kept and their sizes stay within `max_bytes`; everything older than the first one that does not fit
/// goes too. `sessions`: (name, bytes).
pub fn victims(mut sessions: Vec<(String, u64)>, current: &str, max_n: usize, max_bytes: u64) -> Vec<String> {
    sessions.sort_by(|a, b| b.0.cmp(&a.0));
    let (mut kept, mut total) = (0usize, 0u64);
    if let Some(c) = sessions.iter().find(|s| s.0 == current) {
        kept = 1;
        total = c.1;
    }
    let mut over = false;
    let mut out = Vec::new();
    for (name, size) in sessions {
        if name == current {
            continue;
        }
        if over || kept >= max_n || total + size > max_bytes {
            over = true;
            out.push(name);
        } else {
            kept += 1;
            total += size;
        }
    }
    out
}

fn dir_size(p: &Path) -> u64 {
    std::fs::read_dir(p)
        .map(|rd| rd.flatten().map(|e| match e.file_type() {
            Ok(t) if t.is_dir() => dir_size(&e.path()),
            _ => e.metadata().map(|m| m.len()).unwrap_or(0),
        }).sum())
        .unwrap_or(0)
}

/// Deletes the old sessions under `root` (see [`victims`]); returns how many.
pub fn prune(root: &Path, current: &str, max_n: usize, max_bytes: u64) -> usize {
    let Ok(rd) = std::fs::read_dir(root) else { return 0 };
    let sessions: Vec<(String, u64)> = rd
        .flatten()
        .filter(|e| e.file_type().is_ok_and(|t| t.is_dir()))
        .filter_map(|e| e.file_name().to_str().map(str::to_owned))
        .filter(|n| is_session_name(n))
        .map(|n| {
            let size = dir_size(&root.join(&n));
            (n, size)
        })
        .collect();
    let v = victims(sessions, current, max_n, max_bytes);
    for n in &v {
        let _ = std::fs::remove_dir_all(root.join(n));
    }
    v.len()
}

/// Starts a session under `root`: the folder, retention, the log files and the panic hook. Before the app is built
/// (the log layer reads it). None when the folder cannot be made.
pub fn start(root: &Path) -> Option<Session> {
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    let base = stamp(now);
    let mut name = base.clone();
    let mut k = 2;
    while root.join(&name).exists() {
        name = format!("{base}_{k}");
        k += 1;
    }
    let dir = root.join(&name);
    std::fs::create_dir_all(dir.join("bookmarks")).ok()?;
    let pruned = prune(root, &name, KEEP_SESSIONS, KEEP_BYTES);
    let d = dir.clone();
    let open = move |n: &str| File::create(d.join(n)).ok().map(|f| BufWriter::with_capacity(64 * 1024, f));
    let meta = serde_json::json!({ "started_utc": name, "unix_time": now, "args": std::env::args().collect::<Vec<_>>(), "version": env!("CARGO_PKG_VERSION"), "pruned": pruned });
    let _ = std::fs::write(dir.join("session.json"), serde_json::to_string_pretty(&meta).unwrap_or_default());
    let shared = Shared { dir: dir.clone(), start: Instant::now(), log: Mutex::new(open("game.log")?), events: Mutex::new(open("events.jsonl")?), warnings: AtomicU32::new(0), errors: AtomicU32::new(0) };
    if SHARED.set(shared).is_err() {
        return None;
    }
    install_panic_hook();
    Some(Session {
        dir,
        inputs: open("inputs.jsonl")?,
        state: open("state.jsonl")?,
        frames: open("frames.jsonl")?,
        bookmarks: open("bookmarks.jsonl")?,
        marks: Vec::new(),
        window: Vec::new(),
        window_t: 0.0,
        next_state: 0.0,
        next_flush: 1.0,
    })
}

fn elapsed() -> f64 {
    SHARED.get().map_or(0.0, |s| s.start.elapsed().as_secs_f64())
}

fn flush_shared() {
    if let Some(s) = SHARED.get() {
        if let Ok(mut l) = s.log.try_lock() {
            let _ = l.flush();
        }
        if let Ok(mut e) = s.events.try_lock() {
            let _ = e.flush();
        }
    }
}

/// Panics: the message and a backtrace into panic.txt and game.log, then the previous hook (the console).
fn install_panic_hook() {
    let prev = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        if let Some(s) = SHARED.get() {
            let text = format!("PANIC at {:.3} s: {info}\n{}\n", elapsed(), std::backtrace::Backtrace::force_capture());
            let _ = std::fs::write(s.dir.join("panic.txt"), &text);
            // never wait on the lock: the panicking thread may hold it
            if let Ok(mut l) = s.log.try_lock() {
                let _ = l.write_all(text.as_bytes());
                let _ = l.flush();
            }
            if let Ok(mut e) = s.events.try_lock() {
                let _ = writeln!(e, "{}", serde_json::json!({ "t": elapsed(), "text": format!("panic: {info}") }));
                let _ = e.flush();
            }
        }
        prev(info);
    }));
}

/// The log layer (LogPlugin custom_layer): every log line into game.log, the EVENT lines also into events.jsonl.
pub fn log_layer(_app: &mut App) -> Option<BoxedLayer> {
    SHARED.get().map(|_| Box::new(SessionLayer) as BoxedLayer)
}

struct SessionLayer;

#[derive(Default)]
struct Fields {
    message: String,
    rest: String,
}

impl Visit for Fields {
    fn record_str(&mut self, field: &Field, value: &str) {
        if field.name() == "message" {
            self.message.push_str(value);
        } else {
            self.rest.push_str(&format!(" {}={value}", field.name()));
        }
    }

    fn record_debug(&mut self, field: &Field, value: &dyn std::fmt::Debug) {
        if field.name() == "message" {
            self.message.push_str(&format!("{value:?}"));
        } else {
            self.rest.push_str(&format!(" {}={value:?}", field.name()));
        }
    }
}

impl<S: Subscriber> Layer<S> for SessionLayer {
    fn on_event(&self, event: &tracing::Event<'_>, _ctx: Context<'_, S>) {
        let Some(s) = SHARED.get() else { return };
        let mut f = Fields::default();
        event.record(&mut f);
        let meta = event.metadata();
        let level = *meta.level();
        if level == tracing::Level::WARN {
            s.warnings.fetch_add(1, Ordering::Relaxed);
        } else if level == tracing::Level::ERROR {
            s.errors.fetch_add(1, Ordering::Relaxed);
        }
        let t = s.start.elapsed().as_secs_f64();
        if let Ok(mut l) = s.log.lock() {
            let _ = writeln!(l, "{t:10.3} {level:5} {}: {}{}", meta.target(), f.message, f.rest);
        }
        if let Some(e) = f.message.strip_prefix("EVENT ") {
            if let Ok(mut w) = s.events.lock() {
                let _ = writeln!(w, "{}", serde_json::json!({ "t": (t * 1000.0).round() / 1000.0, "text": e }));
            }
        }
    }
}

/// The running session's own files (the ECS side). Present only while recording.
#[derive(Resource)]
pub struct Session {
    pub dir: PathBuf,
    inputs: BufWriter<File>,
    state: BufWriter<File>,
    frames: BufWriter<File>,
    bookmarks: BufWriter<File>,
    /// the bookmarks written: (number, note, screenshot, save)
    marks: Vec<Mark>,
    /// this period's frame times (ms)
    window: Vec<f32>,
    window_t: f32,
    next_state: f32,
    next_flush: f32,
}

struct Mark {
    n: u32,
    dir: PathBuf,
    note: String,
}

/// Ask for a bookmark (F8, the developer menu).
#[derive(Resource, Default)]
pub struct BookmarkRequest(pub bool);

/// A bookmark being made: the screenshot and the save are taken, then the note box opens.
#[derive(Resource, Default)]
enum Bookmarking {
    #[default]
    Idle,
    Capturing { n: u32, dir: PathBuf, info: serde_json::Value, frames: u32 },
    Note { n: u32, dir: PathBuf, info: serde_json::Value, text: String, focus: bool },
}

pub struct SessionPlugin;

impl Plugin for SessionPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<BookmarkRequest>()
            .init_resource::<Bookmarking>()
            .add_systems(Update, (record_input, watch).run_if(resource_exists::<Session>))
            .add_systems(Update, bookmark.after(super::run_commands))
            .add_systems(Last, (frame_stats, state_log, flush).chain().run_if(resource_exists::<Session>))
            .add_systems(EguiPrimaryContextPass, note_box.after(super::overlay::zoom).run_if(resource_exists::<Session>));
    }
}

fn names<T: std::fmt::Debug>(it: impl Iterator<Item = T>) -> Vec<String> {
    it.map(|k| format!("{k:?}")).collect()
}

/// inputs.jsonl: one line per frame with input (and the app state's changes).
#[allow(clippy::too_many_arguments)]
fn record_input(
    mut s: ResMut<Session>,
    frame: Res<bevy::diagnostic::FrameCount>,
    keys: Res<ButtonInput<KeyCode>>,
    mouse: Res<ButtonInput<MouseButton>>,
    (motion, wheel): (Res<AccumulatedMouseMotion>, Res<AccumulatedMouseScroll>),
    pads: Query<(Entity, &Gamepad)>,
    state: Res<State<AppState>>,
    mut last: Local<(Option<AppState>, std::collections::HashMap<(Entity, u8), Vec2>)>,
) {
    let mut line = serde_json::Map::new();
    if last.0 != Some(*state.get()) {
        last.0 = Some(*state.get());
        line.insert("state".into(), format!("{:?}", state.get()).into());
    }
    let mut put = |k: &str, v: Vec<String>| {
        if !v.is_empty() {
            line.insert(k.into(), v.into());
        }
    };
    put("down", names(keys.get_just_pressed()));
    put("up", names(keys.get_just_released()));
    put("mdown", names(mouse.get_just_pressed()));
    put("mup", names(mouse.get_just_released()));
    if motion.delta != Vec2::ZERO {
        line.insert("mouse".into(), serde_json::json!([motion.delta.x, motion.delta.y]));
    }
    if wheel.delta != Vec2::ZERO {
        line.insert("wheel".into(), serde_json::json!([wheel.delta.x, wheel.delta.y]));
    }
    for (e, p) in &pads {
        let mut pad = serde_json::Map::new();
        let down = names(p.get_just_pressed());
        let up = names(p.get_just_released());
        if !down.is_empty() {
            pad.insert("down".into(), down.into());
        }
        if !up.is_empty() {
            pad.insert("up".into(), up.into());
        }
        let lt = p.get(GamepadButton::LeftTrigger2).unwrap_or(0.0);
        let rt = p.get(GamepadButton::RightTrigger2).unwrap_or(0.0);
        for (i, (key, v)) in [("ls", p.left_stick()), ("rs", p.right_stick()), ("triggers", Vec2::new(lt, rt))].into_iter().enumerate() {
            let prev = last.1.entry((e, i as u8)).or_default();
            if prev.distance(v) > 0.05 || (v == Vec2::ZERO && *prev != Vec2::ZERO) {
                *prev = v;
                pad.insert(key.into(), serde_json::json!([(v.x * 100.0).round() / 100.0, (v.y * 100.0).round() / 100.0]));
            }
        }
        if !pad.is_empty() {
            line.insert("pad".into(), pad.into());
        }
    }
    if line.is_empty() {
        return;
    }
    line.insert("f".into(), frame.0.into());
    line.insert("t".into(), ((elapsed() * 1000.0).round() / 1000.0).into());
    let _ = writeln!(s.inputs, "{}", serde_json::Value::Object(line));
}

/// events.jsonl's own lines: a level's mission started, Roman's deaths, the mission won or failed.
fn watch(
    mission: Option<Res<crate::mission::Mission>>,
    levels: Res<crate::campaign::Levels>,
    roman: Query<&crate::fx::Health, With<crate::player::Roman>>,
    generation: Res<crate::levelreset::LevelGeneration>,
    mut seen: crate::levelreset::PerLevel<(bool, bool, Option<crate::mission::Outcome>)>,
) {
    let seen = seen.get();
    let Some(m) = mission else { return };
    if m.started() && !seen.0 {
        seen.0 = true;
        info!("EVENT session: level {} started (generation {})", levels.current_name(), generation.0);
    }
    let dead = roman.single().is_ok_and(|h| h.0 <= 0.0);
    if dead && !seen.1 {
        info!("EVENT session: Roman died on {}", levels.current_name());
    }
    seen.1 = dead;
    if seen.2 != Some(m.outcome) {
        if seen.2.is_some() && m.outcome != crate::mission::Outcome::Playing {
            info!("EVENT session: mission {:?} on {}{}", m.outcome, levels.current_name(), m.failure_text.as_deref().map(|t| format!(": {t}")).unwrap_or_default());
        }
        seen.2 = Some(m.outcome);
    }
}

/// Percentile of sorted values (nearest rank).
fn percentile(sorted: &[f32], p: f32) -> f32 {
    if sorted.is_empty() {
        return 0.0;
    }
    let i = ((p / 100.0) * sorted.len() as f32).ceil() as usize;
    sorted[i.clamp(1, sorted.len()) - 1]
}

/// frames.jsonl: the frame times of each 5 s (wall clock), and an EVENT for a frame over 100 ms outside loading.
fn frame_stats(mut s: ResMut<Session>, time: Res<Time<Real>>, state: Res<State<AppState>>, levels: Res<crate::campaign::Levels>) {
    let ms = time.delta_secs() * 1000.0;
    if ms <= 0.0 {
        return;
    }
    s.window.push(ms);
    s.window_t += time.delta_secs();
    if ms > 100.0 && !matches!(state.get(), AppState::Loading) && time.elapsed_secs() > 5.0 {
        info!("EVENT perf: slow frame {ms:.0} ms ({:?}, {})", state.get(), levels.current_name());
    }
    if s.window_t < FRAMES_PERIOD {
        return;
    }
    let mut v = std::mem::take(&mut s.window);
    s.window_t = 0.0;
    v.sort_by(f32::total_cmp);
    let r = |x: f32| (x * 100.0).round() / 100.0;
    let mean = v.iter().sum::<f32>() / v.len() as f32;
    let line = serde_json::json!({
        "t": r(elapsed() as f32), "state": format!("{:?}", state.get()), "level": levels.current_name(), "frames": v.len(),
        "mean_ms": r(mean), "p50_ms": r(percentile(&v, 50.0)), "p95_ms": r(percentile(&v, 95.0)), "p99_ms": r(percentile(&v, 99.0)),
        "max_ms": r(*v.last().unwrap_or(&0.0)), "over_33ms": v.iter().filter(|x| **x > 33.4).count(), "over_100ms": v.iter().filter(|x| **x > 100.0).count(),
    });
    let _ = writeln!(s.frames, "{line}");
}

/// state.jsonl twice a second; the state log's "session" (playtests: the bookmarks and their files).
fn state_log(mut s: ResMut<Session>, time: Res<Time<Real>>, state: Option<ResMut<crate::playtest::PlayState>>, app_state: Res<State<AppState>>, playtest: Option<Res<crate::playtest::Playtest>>) {
    let Some(mut st) = state else { return };
    let now = time.elapsed_secs();
    if now < s.next_state {
        return;
    }
    s.next_state = now + STATE_PERIOD;
    let last = s.marks.last().map(|m| {
        serde_json::json!({ "n": m.n, "note": m.note, "screenshot": m.dir.join("screenshot.png").is_file(), "save": m.dir.join("save.json").is_file() })
    });
    st.0.insert("session".into(), serde_json::json!({ "dir": s.dir.to_string_lossy(), "bookmarks": s.marks.len(), "last": last }));
    let mut line = st.0.clone();
    line.insert("t".into(), ((elapsed() * 100.0).round() / 100.0).into());
    line.insert("app_state".into(), format!("{:?}", app_state.get()).into());
    let _ = writeln!(s.state, "{}", serde_json::Value::Object(line));
    // in normal play nothing else takes the mission events the state gathers (playtest.rs log_state does in scripts)
    if playtest.is_none() {
        st.0.remove("events");
    }
}

/// Flushes every file once a second, and at exit.
fn flush(mut s: ResMut<Session>, time: Res<Time<Real>>, mut exit: MessageReader<AppExit>) {
    let exiting = exit.read().next().is_some();
    if time.elapsed_secs() < s.next_flush && !exiting {
        return;
    }
    s.next_flush = time.elapsed_secs() + 1.0;
    if exiting {
        info!("EVENT session: exit");
        if let Some(sh) = SHARED.get() {
            info!("session: {} warnings, {} errors", sh.warnings.load(Ordering::Relaxed), sh.errors.load(Ordering::Relaxed));
        }
    }
    let s = &mut *s;
    for f in [&mut s.inputs, &mut s.state, &mut s.frames, &mut s.bookmarks] {
        let _ = f.flush();
    }
    flush_shared();
}

/// F8: the screenshot and a save now, then (three frames on, so the screenshot shows the game) the note box.
#[allow(clippy::too_many_arguments)]
fn bookmark(
    mut commands: Commands,
    mut req: ResMut<BookmarkRequest>,
    mut bm: ResMut<Bookmarking>,
    session: Option<Res<Session>>,
    (mut save, mission, levels): (ResMut<crate::savegame::SaveRequest>, Option<ResMut<crate::mission::Mission>>, Res<crate::campaign::Levels>),
    roman: Query<(&Transform, &crate::fx::Health), With<crate::player::Roman>>,
    (mut overlay, state, mut next, mut vtime, mut menu): (ResMut<DevOverlay>, Res<State<AppState>>, ResMut<NextState<AppState>>, ResMut<Time<Virtual>>, ResMut<crate::gamemenu::GameMenu>),
    frame: Res<bevy::diagnostic::FrameCount>,
) {
    match &mut *bm {
        Bookmarking::Idle => {
            if !std::mem::take(&mut req.0) {
                return;
            }
            let Some(s) = session else {
                if let Some(mut m) = mission {
                    m.feed("Session recording is off: no bookmark");
                }
                return;
            };
            let n = s.marks.len() as u32 + 1;
            let dir = s.dir.join("bookmarks").join(format!("{n:03}"));
            if std::fs::create_dir_all(&dir).is_err() {
                return;
            }
            commands.spawn(Screenshot::primary_window()).observe(save_to_disk(dir.join("screenshot.png")));
            let at = roman.single().ok();
            if at.is_some() && mission.is_some() {
                save.save = true;
                save.path = Some(dir.join("save.json"));
                save.source = crate::savegame::SaveSource::Script;
            }
            let pos = at.map(|(t, _)| {
                let g = crate::level::game_to_engine().inverse() * t.translation;
                [g.x.round(), g.y.round(), g.z.round()]
            });
            let info = serde_json::json!({ "n": n, "t": (elapsed() * 100.0).round() / 100.0, "level": levels.current_name(), "pos": pos, "health": at.map(|(_, h)| h.0.round()), "mission": mission.as_ref().map(|m| format!("{:?}", m.outcome)), "state": format!("{:?}", state.get()) });
            *bm = Bookmarking::Capturing { n, dir, info, frames: 3 };
        }
        Bookmarking::Capturing { n, dir, info, frames } => {
            *frames = frames.saturating_sub(1);
            if *frames > 0 {
                return;
            }
            overlay.note = true;
            overlay.resume = overlay.resume || super::pause_for_overlay(&state, &mut next, &mut vtime, &mut menu);
            overlay.opened_frame = frame.0;
            *bm = Bookmarking::Note { n: *n, dir: std::mem::take(dir), info: info.take(), text: String::new(), focus: true };
        }
        Bookmarking::Note { .. } => {}
    }
}

/// The bookmark's note box: type a note (optional); Enter / A keeps it, Esc / B leaves it empty.
#[allow(clippy::too_many_arguments)]
fn note_box(
    mut contexts: EguiContexts,
    mut bm: ResMut<Bookmarking>,
    mut s: ResMut<Session>,
    keys: Res<ButtonInput<KeyCode>>,
    pads: Query<&Gamepad>,
    (mut overlay, mut next, mut vtime): (ResMut<DevOverlay>, ResMut<NextState<AppState>>, ResMut<Time<Virtual>>),
    frame: Res<bevy::diagnostic::FrameCount>,
) -> Result {
    let Bookmarking::Note { n, dir, info, text, focus } = &mut *bm else { return Ok(()) };
    let ctx = contexts.ctx_mut()?;
    let mut done: Option<bool> = None;
    egui::Window::new(format!("Bookmark {n}")).anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0]).collapsible(false).resizable(false).show(ctx, |ui| {
        ui.set_min_width(420.0);
        ui.label("Screenshot and save taken. Add a note (optional): what is wrong here?");
        let r = ui.add(egui::TextEdit::singleline(text).desired_width(400.0).hint_text("e.g. Q stuck at the gate"));
        if std::mem::take(focus) {
            r.request_focus();
        }
        ui.horizontal(|ui| {
            let pad = crate::input::pad_active();
            if ui.button(if pad { "Keep note (A)" } else { "Keep note (Enter)" }).clicked() {
                done = Some(true);
            }
            if ui.button(if pad { "No note (B)" } else { "No note (Esc)" }).clicked() {
                done = Some(false);
            }
        });
    });
    if frame.0 > overlay.opened_frame {
        if keys.just_pressed(KeyCode::Enter) || keys.just_pressed(KeyCode::NumpadEnter) || pads.iter().any(|p| p.just_pressed(GamepadButton::South)) {
            done = Some(true);
        }
        if keys.just_pressed(KeyCode::Escape) || pads.iter().any(|p| p.just_pressed(GamepadButton::East)) {
            done = Some(false);
        }
    }
    let Some(keep) = done else { return Ok(()) };
    let note = if keep { text.trim().to_owned() } else { String::new() };
    let rel = |p: &Path| p.strip_prefix(&s.dir).unwrap_or(p).to_string_lossy().replace('\\', "/");
    let shot = dir.join("screenshot.png");
    let save = dir.join("save.json");
    let mut entry = info.take();
    entry["note"] = note.clone().into();
    entry["screenshot"] = rel(&shot).into();
    entry["save"] = if save.is_file() { rel(&save).into() } else { serde_json::Value::Null };
    if !note.is_empty() {
        let _ = std::fs::write(dir.join("note.txt"), &note);
    }
    let _ = writeln!(s.bookmarks, "{entry}");
    let _ = s.bookmarks.flush();
    info!("EVENT bookmark {n}: {}", if note.is_empty() { "(no note)" } else { &note });
    s.marks.push(Mark { n: *n, dir: std::mem::take(dir), note });
    *bm = Bookmarking::Idle;
    overlay.note = false;
    super::resume_after_overlay(&mut overlay, &mut next, &mut vtime);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stamps() {
        assert_eq!(stamp(0), "1970-01-01_00-00-00");
        // 2026-10-18 14:03:22 UTC
        assert_eq!(stamp(1_792_332_202), "2026-10-18_14-03-22");
        assert_eq!(stamp(951_782_400), "2000-02-29_00-00-00");
        assert!(is_session_name("2026-10-18_14-03-22") && is_session_name("2026-10-18_14-03-22_2"));
        assert!(!is_session_name("saves") && !is_session_name("2026-10-18") && !is_session_name("2026-10-18_14-03-22x"));
    }

    /// Newest first: the running one always kept, then up to the count and the size; everything older than the first
    /// one that does not fit goes.
    #[test]
    fn retention_rule() {
        let s = |n: &str, b: u64| (n.to_owned(), b);
        let all = vec![s("2026-01-01_00-00-01", 10), s("2026-01-01_00-00-02", 10), s("2026-01-01_00-00-03", 10), s("2026-01-01_00-00-04", 10)];
        assert_eq!(victims(all.clone(), "2026-01-01_00-00-04", 2, 1000), vec!["2026-01-01_00-00-02".to_string(), "2026-01-01_00-00-01".to_string()]);
        assert!(victims(all.clone(), "2026-01-01_00-00-04", 20, 1000).is_empty());
        // the size cap: 4 + 3 fit in 25, 2 does not, so 2 and 1 go
        assert_eq!(victims(all.clone(), "2026-01-01_00-00-04", 20, 25), vec!["2026-01-01_00-00-02".to_string(), "2026-01-01_00-00-01".to_string()]);
        // the running session is kept even when it alone is over the cap
        let big = vec![s("2026-01-01_00-00-01", 10), s("2026-01-01_00-00-09", 5000)];
        assert_eq!(victims(big, "2026-01-01_00-00-09", 20, 100), vec!["2026-01-01_00-00-01".to_string()]);
    }

    /// prune on disk: only session-named folders are touched.
    #[test]
    fn retention_on_disk() {
        let root = std::env::temp_dir().join(format!("and_sessions_test_{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        for n in ["2026-01-01_00-00-01", "2026-01-01_00-00-02", "2026-01-01_00-00-03", "keep_me"] {
            std::fs::create_dir_all(root.join(n)).unwrap();
            std::fs::write(root.join(n).join("game.log"), "x").unwrap();
        }
        assert_eq!(prune(&root, "2026-01-01_00-00-03", 2, KEEP_BYTES), 1);
        assert!(!root.join("2026-01-01_00-00-01").exists());
        assert!(root.join("2026-01-01_00-00-02").exists() && root.join("2026-01-01_00-00-03").exists() && root.join("keep_me").exists());
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn percentiles() {
        let v: Vec<f32> = (1..=100).map(|x| x as f32).collect();
        assert_eq!(percentile(&v, 50.0), 50.0);
        assert_eq!(percentile(&v, 99.0), 99.0);
        assert_eq!(percentile(&[], 50.0), 0.0);
    }
}
