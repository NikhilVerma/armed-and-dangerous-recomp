//! Playtest harness: `--play SCRIPT.json` feeds timed input into the game as if a player pressed it, takes
//! named screenshots and logs the game state once a second, so a run can be played and checked unattended.
//!
//! Script: {"events": [{"t": 1.0, "down": "KeyW"}, {"t": 2.5, "up": "KeyW"}, {"t": 3, "mouse": [40, 0]},
//!          {"t": 3.2, "click": "Left"}, {"t": 4, "shot": "after_fire"}, {"t": 20, "quit": true}]}
//! Keys use Bevy's KeyCode names (KeyW, Space, ShiftLeft, KeyE, Escape, Digit1, ...). Output goes next to the
//! script: <script>.log.jsonl (state lines) and <script>.<shot>.png.

use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::prelude::*;
use bevy::render::view::screenshot::{Screenshot, save_to_disk};
use serde::Deserialize;
use std::io::Write;
use std::path::PathBuf;

#[derive(Deserialize, Clone, Default)]
pub struct Event {
    pub t: f32,
    #[serde(default)]
    pub down: Option<String>,
    #[serde(default)]
    pub up: Option<String>,
    /// press and release next frame
    #[serde(default)]
    pub tap: Option<String>,
    #[serde(default)]
    pub mouse: Option<[f32; 2]>,
    #[serde(default)]
    pub click: Option<String>,
    #[serde(default)]
    pub hold_mouse: Option<String>,
    #[serde(default)]
    pub release_mouse: Option<String>,
    #[serde(default)]
    pub shot: Option<String>,
    #[serde(default)]
    pub quit: bool,
}

#[derive(Deserialize)]
struct Script {
    events: Vec<Event>,
    /// turn the camera by this many mouse counts per frame while time is in [t0, t1)
    #[serde(default)]
    turns: Vec<(f32, f32, f32, f32)>,
}

#[derive(Resource)]
pub struct Playtest {
    pub path: PathBuf,
    events: Vec<Event>,
    turns: Vec<(f32, f32, f32, f32)>,
    next: usize,
    t: f32,
    taps: Vec<KeyCode>,
    clicks: Vec<MouseButton>,
    log: Option<std::fs::File>,
    last_log: f32,
}

/// Lines the game adds to the once-a-second state log (player position, health, mission step, ...).
#[derive(Resource, Default)]
pub struct PlayState(pub serde_json::Map<String, serde_json::Value>);

impl Playtest {
    pub fn load(path: PathBuf) -> Self {
        let text = std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
        let s: Script = serde_json::from_str(&text).expect("playtest script");
        let mut events = s.events;
        events.sort_by(|a, b| a.t.total_cmp(&b.t));
        let log = std::fs::File::create(path.with_extension("log.jsonl")).ok();
        Playtest { path, events, turns: s.turns, next: 0, t: 0.0, taps: vec![], clicks: vec![], log, last_log: -1.0 }
    }
}

pub struct PlaytestPlugin;

impl Plugin for PlaytestPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<PlayState>()
            .add_systems(PreUpdate, drive.after(bevy::input::InputSystems).run_if(resource_exists::<Playtest>))
            .add_systems(Last, log_state.run_if(resource_exists::<Playtest>));
    }
}

pub fn key(name: &str) -> Option<KeyCode> {
    let k = match name {
        "Space" => KeyCode::Space,
        "Escape" => KeyCode::Escape,
        "Enter" => KeyCode::Enter,
        "Tab" => KeyCode::Tab,
        "ShiftLeft" => KeyCode::ShiftLeft,
        "ControlLeft" => KeyCode::ControlLeft,
        "AltLeft" => KeyCode::AltLeft,
        "ArrowUp" => KeyCode::ArrowUp,
        "ArrowDown" => KeyCode::ArrowDown,
        "ArrowLeft" => KeyCode::ArrowLeft,
        "ArrowRight" => KeyCode::ArrowRight,
        s if s.starts_with("Key") && s.len() == 4 => {
            let c = s.as_bytes()[3];
            [
                KeyCode::KeyA, KeyCode::KeyB, KeyCode::KeyC, KeyCode::KeyD, KeyCode::KeyE, KeyCode::KeyF, KeyCode::KeyG,
                KeyCode::KeyH, KeyCode::KeyI, KeyCode::KeyJ, KeyCode::KeyK, KeyCode::KeyL, KeyCode::KeyM, KeyCode::KeyN,
                KeyCode::KeyO, KeyCode::KeyP, KeyCode::KeyQ, KeyCode::KeyR, KeyCode::KeyS, KeyCode::KeyT, KeyCode::KeyU,
                KeyCode::KeyV, KeyCode::KeyW, KeyCode::KeyX, KeyCode::KeyY, KeyCode::KeyZ,
            ]
            .get((c.wrapping_sub(b'A')) as usize)
            .copied()?
        }
        s if s.starts_with("Digit") && s.len() == 6 => {
            [
                KeyCode::Digit0, KeyCode::Digit1, KeyCode::Digit2, KeyCode::Digit3, KeyCode::Digit4, KeyCode::Digit5,
                KeyCode::Digit6, KeyCode::Digit7, KeyCode::Digit8, KeyCode::Digit9,
            ]
            .get((s.as_bytes()[5].wrapping_sub(b'0')) as usize)
            .copied()?
        }
        _ => return None,
    };
    Some(k)
}

fn button(name: &str) -> MouseButton {
    match name {
        "Right" => MouseButton::Right,
        "Middle" => MouseButton::Middle,
        _ => MouseButton::Left,
    }
}

#[allow(clippy::too_many_arguments)]
fn drive(
    mut commands: Commands,
    time: Res<Time<Real>>,
    mut pt: ResMut<Playtest>,
    mut keys: ResMut<ButtonInput<KeyCode>>,
    mut buttons: ResMut<ButtonInput<MouseButton>>,
    mut motion: ResMut<AccumulatedMouseMotion>,
    mut exit: MessageWriter<AppExit>,
) {
    pt.t += time.delta_secs();
    let t = pt.t;
    for k in std::mem::take(&mut pt.taps) {
        keys.release(k);
    }
    for b in std::mem::take(&mut pt.clicks) {
        buttons.release(b);
    }
    let turn: Vec2 = pt.turns.iter().filter(|(t0, t1, _, _)| t >= *t0 && t < *t1).map(|(_, _, x, y)| Vec2::new(*x, *y)).sum();
    motion.delta += turn;
    while pt.next < pt.events.len() && pt.events[pt.next].t <= t {
        let e = pt.events[pt.next].clone();
        pt.next += 1;
        if let Some(k) = e.down.as_deref().and_then(key) {
            keys.press(k);
        }
        if let Some(k) = e.up.as_deref().and_then(key) {
            keys.release(k);
        }
        if let Some(k) = e.tap.as_deref().and_then(key) {
            keys.press(k);
            pt.taps.push(k);
        }
        if let Some(m) = e.mouse {
            motion.delta += Vec2::from(m);
        }
        if let Some(b) = e.click.as_deref().map(button) {
            buttons.press(b);
            pt.clicks.push(b);
        }
        if let Some(b) = e.hold_mouse.as_deref().map(button) {
            buttons.press(b);
        }
        if let Some(b) = e.release_mouse.as_deref().map(button) {
            buttons.release(b);
        }
        if let Some(name) = e.shot {
            let path = pt.path.with_extension(format!("{name}.png"));
            commands.spawn(Screenshot::primary_window()).observe(save_to_disk(path));
        }
        if e.quit {
            exit.write(AppExit::Success);
        }
    }
}

fn log_state(mut pt: ResMut<Playtest>, state: Res<PlayState>) {
    if pt.t - pt.last_log < 1.0 {
        return;
    }
    pt.last_log = pt.t;
    let mut line = state.0.clone();
    line.insert("t".into(), ((pt.t * 100.0).round() / 100.0).into());
    if let Some(f) = pt.log.as_mut() {
        let _ = writeln!(f, "{}", serde_json::Value::Object(line));
    }
}
