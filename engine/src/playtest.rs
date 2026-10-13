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
    #[serde(default)]
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
    /// steps only: take this step's shot this many seconds into the step (e.g. mid hold_use)
    #[serde(default)]
    pub shot_after: Option<f32>,
    #[serde(default)]
    pub quit: bool,
    /// debug: take this much damage at once (to test losing)
    #[serde(default)]
    pub hurt: Option<f32>,
    /// debug: spawn a grunt_regular at this game-space point (as a barracks does)
    #[serde(default)]
    pub spawn: Option<[f32; 3]>,
    /// bot: walk to this point (game space x, y, z: as the scene files give positions)
    #[serde(default)]
    pub goto: Option<[f32; 3]>,
    /// bot: aim at the nearest live grunt within range and fire (true) / stop (false)
    #[serde(default)]
    pub fight: Option<bool>,
    /// bot: hold the Use key (true) / release (false)
    #[serde(default)]
    pub r#use: Option<bool>,
    /// bot: with goto, arrive within this many units (default 12)
    #[serde(default)]
    pub near: Option<f32>,
    /// steps only: wait this many seconds
    #[serde(default)]
    pub wait: Option<f32>,
    /// steps only: hold Use for this many seconds
    #[serde(default)]
    pub hold_use: Option<f32>,
}

/// The playtest bot's orders, read by the player's control and weapon systems.
#[derive(Resource, Default)]
pub struct Bot {
    pub goto: Option<Vec3>,
    pub fight: bool,
    pub arrived: bool,
    pub use_key: bool,
    pub stuck_timer: f32,
    pub last_pos: Vec3,
    pub veer: f32,
    /// planned waypoints toward `goto` (nav.rs)
    pub route: Vec<Vec3>,
    pub planned_for: Option<Vec3>,
    pub replan_timer: f32,
    /// places the bot got stuck at (avoided by later plans)
    pub blocked: Vec<Vec3>,
    pub no_progress: f32,
    pub last_dist: f32,
    /// seconds spent holding position on the current engagement
    pub hold: f32,
    /// seconds without a target (the hold resets after 5)
    pub no_target: f32,
    /// the bot has something worth shooting and no explosive next to it
    pub fire_ok: bool,
    /// the current goto's arrival distance (0: the default 12)
    pub near: f32,
    /// seconds on the current pickup detour; pickups given up on (unreachable)
    pub detour_t: f32,
    pub skip_pickups: Vec<Vec3>,
    /// throw a sticky bomb this frame; seconds until the next may be thrown
    pub throw: bool,
    pub throw_timer: f32,
}

#[derive(Deserialize)]
struct Script {
    /// turn the camera by this many mouse counts per frame while time is in [t0, t1)
    #[serde(default)]
    events: Vec<Event>,
    #[serde(default)]
    turns: Vec<(f32, f32, f32, f32)>,
    /// sequential steps: each runs when the previous finished (goto: on arrival, max 240 s; wait / hold_use: after
    /// their time); the same fields as events, without `t`
    #[serde(default)]
    steps: Vec<Event>,
    /// start on the title screen (menu tests) instead of in the level
    #[serde(default)]
    title: bool,
}

struct StepView<'a>(&'a Event);

impl serde::Serialize for StepView<'_> {
    fn serialize<S: serde::Serializer>(&self, s: S) -> Result<S::Ok, S::Error> {
        let e = self.0;
        let v = serde_json::json!({"goto": e.goto, "wait": e.wait, "hold_use": e.hold_use, "tap": e.tap, "shot": e.shot, "fight": e.fight});
        v.serialize(s)
    }
}

#[derive(Resource)]
pub struct Playtest {
    pub path: PathBuf,
    pub title: bool,
    events: Vec<Event>,
    turns: Vec<(f32, f32, f32, f32)>,
    next: usize,
    t: f32,
    taps: Vec<KeyCode>,
    clicks: Vec<MouseButton>,
    log: Option<std::fs::File>,
    last_log: f32,
    steps: Vec<Event>,
    /// a step's deferred shot (shot_after)
    step_shot: Option<String>,
    step: usize,
    step_t: f32,
    step_started: bool,
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
        Playtest { path, title: s.title, events, turns: s.turns, next: 0, t: 0.0, taps: vec![], clicks: vec![], log, last_log: -1.0, steps: s.steps, step_shot: None, step: 0, step_t: 0.0, step_started: false }
    }
}

impl Event {
    /// The same event with nothing to do (a base for one deferred action).
    fn cleared(self) -> Event {
        serde_json::from_str::<Event>(&format!("{{\"t\": {}}}", self.t)).expect("empty event")
    }
}

pub struct PlaytestPlugin;

impl Plugin for PlaytestPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<PlayState>()
            .init_resource::<Bot>()
            .add_systems(PreUpdate, drive.after(bevy::input::InputSystems).run_if(resource_exists::<Playtest>))
            .add_systems(Last, log_state.run_if(resource_exists::<Playtest>));
    }
}

pub fn key(name: &str) -> Option<KeyCode> {
    let k = match name {
        "Space" => KeyCode::Space,
        "Escape" => KeyCode::Escape,
        "F3" => KeyCode::F3,
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
    mut bot: ResMut<Bot>,
    mission: Option<Res<crate::mission::Mission>>,
    mut roman: Query<&mut crate::fx::Health, With<crate::player::Roman>>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    player: Query<&GlobalTransform, With<crate::player::Roman>>,
) {
    pt.t += time.delta_secs();
    // the mission over (won or failed): play stops, so a pending goto counts as done
    let ended = mission.is_some_and(|m| m.outcome != crate::mission::Outcome::Playing);
    let t = pt.t;
    for k in std::mem::take(&mut pt.taps) {
        keys.release(k);
    }
    for b in std::mem::take(&mut pt.clicks) {
        buttons.release(b);
    }
    let turn: Vec2 = pt.turns.iter().filter(|(t0, t1, _, _)| t >= *t0 && t < *t1).map(|(_, _, x, y)| Vec2::new(*x, *y)).sum();
    motion.delta += turn;
    // sequential steps
    let mut due: Vec<Event> = Vec::new();
    if pt.step < pt.steps.len() {
        let st = pt.steps[pt.step].clone();
        if !pt.step_started {
            pt.step_started = true;
            pt.step_t = 0.0;
            info!("playtest step {}: {}", pt.step, serde_json::to_string(&StepView(&st)).unwrap_or_default());
            pt.step_shot = st.shot_after.is_some().then(|| st.shot.clone()).flatten();
            let mut now = st.clone();
            if pt.step_shot.is_some() {
                now.shot = None;
            }
            due.push(now);
            if st.hold_use.is_some() {
                bot.use_key = true;
            }
            if st.goto.is_some() {
                bot.arrived = false;
            }
        }
        pt.step_t += time.delta_secs();
        if st.shot_after.is_some_and(|a| pt.step_t >= a) {
            if let Some(s) = pt.step_shot.take() {
                due.push(Event { shot: Some(s), ..st.clone().cleared() });
            }
        }
        let done = if st.goto.is_some() {
            bot.arrived || ended || pt.step_t > 240.0
        } else if let Some(w) = st.wait {
            pt.step_t >= w
        } else if let Some(h) = st.hold_use {
            if pt.step_t >= h {
                bot.use_key = false;
                true
            } else {
                false
            }
        } else {
            true
        };
        if done {
            pt.step += 1;
            pt.step_started = false;
        }
    }
    while pt.next < pt.events.len() && pt.events[pt.next].t <= t {
        due.push(pt.events[pt.next].clone());
        pt.next += 1;
    }
    for e in due {
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
            // the camera and Roman at this shot, in game space (Z up), to compare with the original's camera dumps
            let to_game = crate::level::game_to_engine().inverse();
            if let Ok(c) = cam.single() {
                let fwd = to_game * c.forward().as_vec3();
                let up = to_game * c.up().as_vec3();
                let pos = to_game * c.translation();
                let roman = player.single().map(|r| to_game * r.translation()).ok();
                let j = serde_json::json!({"time": pt.t, "position": pos.to_array(), "forward": fwd.to_array(), "up": up.to_array(), "roman": roman.map(|r| r.to_array())});
                let _ = std::fs::write(pt.path.with_extension(format!("{name}.camera.json")), j.to_string());
            }
        }
        if let Some(g) = e.goto {
            // game space (Z up) -> engine (Y up)
            bot.goto = Some(Vec3::new(g[0], g[2], -g[1]));
            bot.arrived = false;
            bot.near = e.near.unwrap_or(0.0);
        }
        if let Some(f) = e.fight {
            bot.fight = f;
        }
        if let Some(u) = e.r#use {
            bot.use_key = u;
        }
        if e.quit {
            exit.write(AppExit::Success);
        }
        if let (Some(d), Ok(mut h)) = (e.hurt, roman.single_mut()) {
            h.0 -= d;
        }
        if let Some(g) = e.spawn {
            spawn.write(crate::level::SpawnGrunt { at: Vec3::new(g[0], g[2], -g[1]), template: "grunt_regular".into() });
        }
    }
}

fn log_state(mut pt: ResMut<Playtest>, mut state: ResMut<PlayState>) {
    if pt.t - pt.last_log < 1.0 {
        return;
    }
    pt.last_log = pt.t;
    let mut line = state.0.clone();
    state.0.remove("events");
    line.insert("t".into(), ((pt.t * 100.0).round() / 100.0).into());
    if let Some(f) = pt.log.as_mut() {
        let _ = writeln!(f, "{}", serde_json::Value::Object(line));
    }
}
