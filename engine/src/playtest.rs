//! Playtest harness: `--play SCRIPT.json` feeds timed input into the game as if a player pressed it, takes
//! named screenshots and logs the game state once a second, so a run can be played and checked unattended.
//!
//! Script: {"events": [{"t": 1.0, "down": "KeyW"}, {"t": 2.5, "up": "KeyW"}, {"t": 3, "mouse": [40, 0]},
//!          {"t": 3.2, "click": "Left"}, {"t": 4, "shot": "after_fire"}, {"t": 20, "quit": true}]}
//! Keys use Bevy's KeyCode names (KeyW, Space, ShiftLeft, KeyE, Escape, Digit1, ...). Output goes next to the
//! script: <script>.log.jsonl (state lines) and <script>.<shot>.png.

use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::prelude::*;
use bevy::render::view::screenshot::{Screenshot, ScreenshotCaptured, save_to_disk};
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
    /// a screenshot measured into the state log's "screen" once captured (a few frames later: wait before an expect;
    /// also saved as <script>.<name>.png): {"name", "side_dark": the near-black fraction of the strips beside the
    /// 4:3 centre (x < 12 % and > 88 %, y 74..83 %: the title menu's black backdrop covers them, the world never
    /// does), "luma": the frame's mean luminance 0..1}
    #[serde(default)]
    pub screen: Option<String>,
    /// steps only: take this step's shot this many seconds into the step (e.g. mid hold_use)
    #[serde(default)]
    pub shot_after: Option<f32>,
    #[serde(default)]
    pub quit: bool,
    /// write the game state to `states/NAME.json` next to the script; a script with `"load": "states/NAME.json"`
    /// starts from it (savegame.rs: Roman, mission, dead grunts, destroyed objects; the intro skipped)
    #[serde(default)]
    pub save: Option<String>,
    /// check state-log fields now: `{"health": [270, 290], "won": false}` (an array of two numbers is an inclusive
    /// range, null means absent, anything else must be equal). Prints EXPECT PASS / FAIL; any failure makes the exit
    /// code 1
    /// An expect value may also compare with a noted state: {"as": "fresh", "margin": 2, "pct": 10} passes when the
    /// field is within margin + pct % of what it was at the `note` step "fresh" (both default 0: equal)
    #[serde(default)]
    pub expect: Option<serde_json::Map<String, serde_json::Value>>,
    /// keep the state log as it is now under this name, for later expects' {"as": name}
    #[serde(default)]
    pub note: Option<String>,
    /// delete every save in the build's saves/ (savegame.rs): the save / load screens start empty
    #[serde(default)]
    pub clear_saves: bool,
    /// debug: take this much damage at once (to test losing)
    #[serde(default)]
    pub hurt: Option<f32>,
    /// debug: deal this hit to the nearest live grunt (tests the death paths)
    #[serde(default)]
    pub kill: Option<f32>,
    /// debug: deal this hit to every live grunt that holds, is going to or just left a mounted gun (a gunner test drives his
    /// death instead of waiting for the bot's bullets to find him)
    #[serde(default)]
    pub kill_gunner: Option<f32>,
    /// debug: destroy the placed object with this instance id (as a lethal hit)
    #[serde(default)]
    pub destroy: Option<u32>,
    /// debug: deal [instance id, amount] to that placed object's health as a hit (Object_TakeDamage 0x004555e0: a
    /// killing blow takes the death path), to test a unit's damage and death without waiting for shots to land
    #[serde(default)]
    pub damage: Option<[f32; 2]>,
    /// debug: the same hit on every live object of a template: ["v_rockettower_turret", 100]
    #[serde(default)]
    pub damage_template: Option<(String, f32)>,
    /// debug: damage a companion: [0 Jonesy / 1 Q, amount]
    #[serde(default)]
    pub hurt_companion: Option<[f32; 2]>,
    /// debug: a badguy blast of this area damage at Roman's feet (through the blast damage path, fx.rs)
    #[serde(default)]
    pub blast: Option<f32>,
    /// debug: send this mission trigger (e.g. 123, lev_1_m1's win)
    #[serde(default)]
    pub send: Option<i64>,
    /// debug: a base-defence command (defence.rs DefenceCommand): {"aim": "army"} keeps the base turret on the nearest
    /// army grunt (or marching platoon), {"aim": false} stops; {"mortar": "platoon"} fires a shell at the nearest
    /// platoon's members; {"frame": F} moves the turret along its wall track
    #[serde(default)]
    pub defence: Option<serde_json::Value>,
    /// debug: spawn a grunt_regular at this game-space point (as a barracks does)
    #[serde(default)]
    pub spawn: Option<[f32; 3]>,
    /// debug: with spawn, the template instead of grunt_regular (grunt: the archer, captain)
    #[serde(default)]
    pub template: Option<String>,
    /// debug: a noise of Roman's at this game-space point (as his bullet's impact: ai.rs Interests, radius 200, 2 s)
    #[serde(default)]
    pub noise: Option<[f32; 3]>,
    /// debug: the grunts' shared bark delays (death / vo / taunt) count as passed (the taunt after a clip at once)
    #[serde(default)]
    pub barks_ready: Option<bool>,
    /// debug: the live grunt nearest Roman runs the hiding-spot search now (as at a reload's end; ai.rs
    /// find_hiding_spot), so a trench's use can be checked without waiting for its rolls
    #[serde(default)]
    pub cover: Option<bool>,
    /// debug: put Roman's feet at this game-space point facing this yaw (degrees, game space: 0 = +y), to look at
    /// one spot without walking there
    #[serde(default)]
    pub warp: Option<[f32; 4]>,
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
    /// steps only: wait until the game shows a paused message (the briefing): "message", max 60 s; or until no movie
    /// plays: "movie_done", max 600 s; or until a restarted level has left its loading screen: "loaded", max 30 s
    #[serde(default)]
    pub until: Option<String>,
    /// steps only: wait this many seconds
    #[serde(default)]
    pub wait: Option<f32>,
    /// steps only: hold Use for this many seconds
    #[serde(default)]
    pub hold_use: Option<f32>,
    /// a virtual gamepad (connected on first use; "PlayStation" in the first command makes it a DualSense): "South=1,
    /// LeftStickY=0.8" sets buttons / axes (Bevy names)
    #[serde(default)]
    pub pad: Option<String>,
    /// debug: make a pickup of this template at Roman's feet (pickup.rs)
    #[serde(default)]
    pub pickup: Option<String>,
    /// debug: set Roman's loadout: {"stickies", "topsy", "mg" (spare ammo; gives the gun), "mg_clip", "use_mg",
    /// "rifle_clip", "health"} (pickup.rs)
    #[serde(default)]
    pub loadout: Option<serde_json::Map<String, serde_json::Value>>,
    /// debug: a sound-system command (gameaudio.rs playtest_audio: play / stop / stop_all_but_music / mixer / music /
    /// pause)
    #[serde(default)]
    pub audio: Option<serde_json::Map<String, serde_json::Value>>,
    /// a movie command (cutscenes.rs playtest_movies): "a,b" plays a skippable list; {"play", "at" seconds into the
    /// first, "skippable", "loop", "behind" z}; {"stop": true}
    #[serde(default)]
    pub movie: Option<serde_json::Value>,
    /// debug: start an fx script (fxvm playtest_fx): {"name", "at": [game x, y, z] (default: Roman's feet), "yaw"
    /// degrees, "clear": true ends every live fx first}
    #[serde(default)]
    pub fx: Option<serde_json::Map<String, serde_json::Value>>,
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
    /// with title: play the boot intro movies first
    #[serde(default)]
    intro: bool,
    /// start from this saved state (relative to the script; written by a 'save' step)
    #[serde(default)]
    load: Option<PathBuf>,
    /// play with this graphics preset as a normal launch does ("remaster": the player's default): the options
    /// choose it (not pinned), so the Graphics page applies live, as for the player (main.rs)
    #[serde(default)]
    preset: Option<crate::graphics::Preset>,
    /// game time follows the wall clock, as in the player's game (default: exactly 1/60 s a frame, main.rs): a slow
    /// frame is then a long game step, as the player gets it (restart_parity: the rebuild's frame)
    #[serde(default)]
    real_time: bool,
}

/// The graphics preset a script plays with as a normal launch (its "preset").
pub fn script_preset(script: &std::path::Path) -> Option<crate::graphics::Preset> {
    serde_json::from_str::<Script>(&std::fs::read_to_string(script).ok()?).ok()?.preset
}

/// A title script that asks for the boot intro movies ("title" and "intro").
impl Playtest {
    pub fn load_intro(script: &std::path::Path) -> bool {
        std::fs::read_to_string(script).ok().and_then(|t| serde_json::from_str::<Script>(&t).ok()).is_some_and(|s| s.title && s.intro)
    }
}

/// The state file a script starts from (its "load"), resolved against the script's folder.
pub fn script_load(script: &std::path::Path) -> Option<PathBuf> {
    let s: Script = serde_json::from_str(&std::fs::read_to_string(script).ok()?).ok()?;
    s.load.map(|l| script.parent().map(|d| d.join(&l)).unwrap_or(l))
}

struct StepView<'a>(&'a Event);

impl serde::Serialize for StepView<'_> {
    fn serialize<S: serde::Serializer>(&self, s: S) -> Result<S::Ok, S::Error> {
        let e = self.0;
        let v = serde_json::json!({"goto": e.goto, "wait": e.wait, "hold_use": e.hold_use, "tap": e.tap, "shot": e.shot, "screen": e.screen, "fight": e.fight});
        v.serialize(s)
    }
}

#[derive(Resource)]
pub struct Playtest {
    /// pickup / loadout steps for pickup.rs
    pub pickups: Vec<String>,
    pub loadouts: Vec<serde_json::Map<String, serde_json::Value>>,
    /// audio steps for gameaudio.rs
    pub audio: Vec<serde_json::Map<String, serde_json::Value>>,
    /// fx steps for fxvm.rs
    pub fx: Vec<serde_json::Map<String, serde_json::Value>>,
    /// movie steps for cutscenes.rs
    pub movies: Vec<serde_json::Value>,
    /// play the boot intro movies before the title menu (a title script skips them unless it sets "intro")
    pub intro: bool,
    pub path: PathBuf,
    pub title: bool,
    /// the script's "real_time"
    pub real_time: bool,
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
    /// expect results so far
    passed: u32,
    failed: u32,
    step: usize,
    step_t: f32,
    step_started: bool,
    /// a warp step waiting for the warp system
    warp: Option<[f32; 4]>,
    /// virtual gamepad input for pad_drive
    pad: Vec<String>,
    /// companion damage for hurt_companions
    hurt_c: Vec<[f32; 2]>,
    /// blast steps for hurt_companions
    blasts: Vec<f32>,
    /// the virtual gamepad, once connected: scripted runs read only it (input.rs), never a pad on the desk
    pad_e: Option<Entity>,
    /// the states kept by `note` steps
    notes: std::collections::HashMap<String, serde_json::Map<String, serde_json::Value>>,
}

/// A state-log field (a dotted key reads into an object field: "window.width").
fn field<'a>(state: &'a serde_json::Map<String, serde_json::Value>, key: &str) -> Option<&'a serde_json::Value> {
    let mut parts = key.split('.');
    parts.next().and_then(|h| state.get(h)).and_then(|v| parts.try_fold(v, |v, p| v.get(p)))
}

/// Whether an expect step's value `want` holds for `got` (see Event::expect); `notes` for {"as": name}.
fn expect_holds(key: &str, want: &serde_json::Value, got: Option<&serde_json::Value>, notes: &std::collections::HashMap<String, serde_json::Map<String, serde_json::Value>>) -> bool {
    if let Some(name) = want.get("as").and_then(|v| v.as_str()) {
        let Some(noted) = notes.get(name).and_then(|n| field(n, key)) else { return false };
        let Some(got) = got else { return false };
        return match (got.as_f64(), noted.as_f64()) {
            (Some(g), Some(n)) => {
                let margin = want.get("margin").and_then(|v| v.as_f64()).unwrap_or(0.0);
                let pct = want.get("pct").and_then(|v| v.as_f64()).unwrap_or(0.0);
                (g - n).abs() <= margin + pct / 100.0 * n.abs()
            }
            _ => got == noted,
        };
    }
    match (want.as_array(), got) {
        (Some(r), Some(g)) if r.len() == 2 && r.iter().all(|v| v.is_number()) => g.as_f64().is_some_and(|v| v >= r[0].as_f64().unwrap() && v <= r[1].as_f64().unwrap()),
        (_, Some(g)) => g == want || (g.is_number() && want.is_number() && g.as_f64() == want.as_f64()),
        // null: the field is absent
        (_, None) => want.is_null(),
    }
}

impl Playtest {
    pub fn pad_entity(&self) -> Option<Entity> {
        self.pad_e
    }

    /// A warp as the script's warp step: Roman's feet to game-space [x, y, z] facing yaw degrees (oracle.rs).
    pub fn request_warp(&mut self, w: [f32; 4]) {
        self.warp = Some(w);
    }
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
        Playtest { pickups: vec![], loadouts: vec![], audio: vec![], fx: vec![], movies: vec![], intro: s.intro, passed: 0, failed: 0, hurt_c: vec![], blasts: vec![], pad_e: None, notes: Default::default(), pad: vec![], warp: None, path, title: s.title, real_time: s.real_time, events, turns: s.turns, next: 0, t: 0.0, taps: vec![], clicks: vec![], log, last_log: -1.0, steps: s.steps, step_shot: None, step: 0, step_t: 0.0, step_started: false }
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
            .init_resource::<FrameClock>()
            .add_systems(First, frame_begin.run_if(resource_exists::<Playtest>))
            .add_systems(Last, log_frames.before(log_render).run_if(resource_exists::<Playtest>))
            .add_systems(PreUpdate, drive.after(bevy::input::InputSystems).before(crate::input::InputDevices).run_if(resource_exists::<Playtest>))
            .add_systems(PreUpdate, pad_drive.after(drive).before(crate::input::InputDevices).run_if(resource_exists::<Playtest>))
            .add_systems(Update, hurt_companions.run_if(resource_exists::<Playtest>))
            .add_systems(Last, (log_render, log_state).chain().run_if(resource_exists::<Playtest>))
            .add_systems(Update, warp.run_if(resource_exists::<Playtest>));
    }
}

/// The warp step: Roman's walker and transform moved, his yaw set (the bot's goto is cleared).
fn warp(
    mut pt: ResMut<Playtest>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut bot: ResMut<crate::bot::Bot>,
    mut roman: Query<(&mut crate::player::Roman, &mut Transform)>,
) {
    let Some(w) = pt.warp else { return };
    let Ok((mut r, mut t)) = roman.single_mut() else { return };
    pt.warp = None;
    let feet = crate::level::game_to_engine() * Vec3::new(w[0], w[1], w[2]);
    physics.place_walker(&r.walker, feet);
    t.translation = feet;
    r.yaw = w[3].to_radians();
    r.twist_reset = true;
    r.free_trail.clear();
    r.free_trail.push_back(feet);
    // a new ground reference (else a warp down a slope reads as a fall)
    r.ground_ref = feet.y;
    r.grace = 0.5;
    r.falling = false;
    r.fall_fast_t = 0.0;
    r.peak_vz = 0.0;
    bot.goto = None;
}

pub fn key(name: &str) -> Option<KeyCode> {
    let k = match name {
        "Space" => KeyCode::Space,
        "Escape" => KeyCode::Escape,
        "F3" => KeyCode::F3,
        "Enter" => KeyCode::Enter,
        "Delete" => KeyCode::Delete,
        "Backspace" => KeyCode::Backspace,
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
pub(crate) fn drive(
    mut commands: Commands,
    time: Res<Time<Real>>,
    mut pt: ResMut<Playtest>,
    mut keys: ResMut<ButtonInput<KeyCode>>,
    mut buttons: ResMut<ButtonInput<MouseButton>>,
    mut motion: ResMut<AccumulatedMouseMotion>,
    mut exit: MessageWriter<AppExit>,
    mut bot: ResMut<crate::bot::Bot>,
    mission: Option<Res<crate::mission::Mission>>,
    mut roman: Query<&mut crate::fx::Health, With<crate::player::Roman>>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
    (mut send, state, mut save_req, mut noise, mut barks, mut defence): (
        MessageWriter<crate::mission::SendTrigger>,
        Res<PlayState>,
        ResMut<crate::savegame::SaveRequest>,
        ResMut<crate::ai::Interests>,
        ResMut<crate::ai::Barks>,
        MessageWriter<crate::defence::DefenceCommand>,
    ),
    mut objects: Query<(Entity, Option<&crate::level::InstanceId>, Option<&crate::level::Placed>, &mut crate::fx::Destructible)>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    player: Query<&GlobalTransform, With<crate::player::Roman>>,
    mut grunts: Query<(&mut crate::ai::Grunt, &mut crate::fx::Health, &GlobalTransform), Without<crate::player::Roman>>,
) {
    pt.t += time.delta_secs();
    // the mission over (won or failed): play stops, so a pending goto counts as done
    let ended = mission.as_ref().is_some_and(|m| m.outcome != crate::mission::Outcome::Playing);
    let t = pt.t;
    let released: Vec<KeyCode> = std::mem::take(&mut pt.taps);
    for &k in &released {
        keys.release(k);
    }
    for b in std::mem::take(&mut pt.clicks) {
        buttons.release(b);
    }
    let turn: Vec2 = pt.turns.iter().filter(|(t0, t1, _, _)| t >= *t0 && t < *t1).map(|(_, _, x, y)| Vec2::new(*x, *y)).sum();
    // scripted runs ignore the real mouse (it moves the camera under the cursor grab): only scripted turns
    motion.delta = turn;
    // a paused mission message (the mid-mission briefings) is dismissed as a player would while the bot walks,
    // fights or holds Use (those steps would never end on a paused game); other steps leave it to the script
    let waits_for_message = pt.steps.get(pt.step).is_none_or(|st| st.goto.is_none() && st.fight.is_none() && st.hold_use.is_none());
    // (a frame up between taps, as a finger gives: input.rs needs a release before a held key counts again)
    if mission.as_ref().is_some_and(|m| m.showing.is_some()) && !waits_for_message && !pt.taps.contains(&KeyCode::Enter) && !released.contains(&KeyCode::Enter) {
        keys.press(KeyCode::Enter);
        pt.taps.push(KeyCode::Enter);
    }
    // sequential steps
    let mut due: Vec<Event> = Vec::new();
    if pt.step < pt.steps.len() {
        let st = pt.steps[pt.step].clone();
        if !pt.step_started {
            pt.step_started = true;
            pt.step_t = 0.0;
            info!("playtest step {} at {:.3}: {}", pt.step, time.elapsed_secs(), serde_json::to_string(&StepView(&st)).unwrap_or_default());
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
        // a paused message (a token found on the way) during a walk: continue as a player would, else the walk
        // never ends (the game is paused)
        if st.goto.is_some() && mission.as_ref().is_some_and(|m| m.showing.is_some()) && pt.step_t.fract() < time.delta_secs() {
            keys.press(KeyCode::Enter);
            pt.taps.push(KeyCode::Enter);
        }
        let done = if st.until.as_deref() == Some("message") {
            mission.as_ref().is_some_and(|m| m.showing.is_some()) || pt.step_t > 60.0
        } else if st.until.as_deref() == Some("movie_done") {
            // no movie in the state log (movie.rs log_state) once the step had a frame to start one; max 600 s
            (pt.step_t > 0.1 && !state.0.contains_key("movie")) || pt.step_t > 600.0
        } else if st.until.as_deref() == Some("loaded") {
            // a restarted level has left its loading screen (levelreset.rs "loading"); max 30 s
            (pt.step_t > 0.1 && state.0.get("loading").and_then(|v| v.as_bool()) != Some(true)) || pt.step_t > 30.0
        } else if let Some(g) = st.goto {
            // a bot that has not arrived after 240 s is stuck (its route-finding is a test aid, not the game): it is
            // warped to the goal and the run goes on, logged so a run that needed it is not mistaken for a clean one
            if !bot.arrived && !ended && pt.step_t > 240.0 {
                warn!("EVENT playtest: bot stuck on the way to {g:?}: warped there");
                pt.warp = Some([g[0], g[1], g[2] + 3.0, 0.0]);
            }
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
        if let Some(t) = e.send {
            send.write(crate::mission::SendTrigger(t));
        }
        if let Some(v) = &e.defence {
            defence.write(crate::defence::DefenceCommand(v.clone()));
        }
        if let Some(id) = e.destroy {
            for (_, i, _, mut d) in &mut objects {
                if i.is_some_and(|i| i.0 == id) && d.dying.is_none() {
                    d.health = 0.0;
                    d.dying = Some(0.0);
                }
            }
        }
        // a hit on placed objects by instance id, or on every live object of a template (parts made at run time,
        // such as a rocket tower's turret)
        let hits = e.damage.map(|[id, amount]| (Some(id as u32), None, amount)).into_iter().chain(e.damage_template.clone().map(|(t, amount)| (None, Some(t), amount)));
        for (id, template, amount) in hits {
            let mut n = 0;
            for (_, i, pl, mut d) in &mut objects {
                let hit = id.is_some_and(|id| i.is_some_and(|i| i.0 == id)) || template.as_ref().is_some_and(|t| pl.is_some_and(|pl| &pl.template == t));
                if !hit {
                    continue;
                }
                n += 1;
                if d.take_damage(amount) && d.dying.is_none() {
                    d.chain_delay = 0.0;
                    d.dying = Some(0.0);
                }
            }
            info!("EVENT playtest: damage {amount} to {n} objects ({})", template.unwrap_or_else(|| format!("id {}", id.unwrap_or(0))));
        }
        if let Some(p) = e.pad {
            pt.pad.push(p);
        }
        if let Some(p) = e.pickup {
            pt.pickups.push(p);
        }
        if let Some(l) = e.loadout {
            pt.loadouts.push(l);
        }
        if let Some(a) = e.audio {
            pt.audio.push(a);
        }
        if let Some(f) = e.fx {
            pt.fx.push(f);
        }
        if let Some(m) = e.movie {
            pt.movies.push(m);
        }
        if let Some(h) = e.hurt_companion {
            pt.hurt_c.push(h);
        }
        if let Some(d) = e.blast {
            pt.blasts.push(d);
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
                // the grunts (game space; the hook's dump lists the original's in "ai"), for movement comparisons
                let gs: Vec<[f32; 3]> = grunts.iter().filter(|(g, _, _)| g.mind != crate::ai::Mind::Dead).map(|(_, _, t)| (to_game * t.translation()).to_array()).collect();
                let j = serde_json::json!({"time": pt.t, "position": pos.to_array(), "forward": fwd.to_array(), "up": up.to_array(), "roman": roman.map(|r| r.to_array()), "grunts": gs});
                let _ = std::fs::write(pt.path.with_extension(format!("{name}.camera.json")), j.to_string());
            }
        }
        if let Some(name) = e.screen {
            let path = pt.path.with_extension(format!("{name}.png"));
            commands.spawn(Screenshot::primary_window()).observe(save_to_disk(path)).observe(measure_screen(name));
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
        if let Some(name) = &e.save {
            let dir = pt.path.parent().map(|d| d.join("states")).unwrap_or_else(|| "states".into());
            save_req.save = true;
            save_req.path = Some(dir.join(format!("{name}.json")));
            save_req.source = crate::savegame::SaveSource::Script;
            info!("EVENT playtest: state saved as {name}");
        }
        if e.clear_saves {
            crate::savegame::clear_saves();
            info!("EVENT playtest: saves cleared");
        }
        if let Some(name) = &e.note {
            pt.notes.insert(name.clone(), state.0.clone());
            info!("EVENT playtest: state noted as {name}");
        }
        if let Some(want) = &e.expect {
            for (k, w) in want {
                let got = field(&state.0, k);
                let ok = expect_holds(k, w, got, &pt.notes);
                if ok { pt.passed += 1 } else { pt.failed += 1 }
                let got = got.map(|g| g.to_string()).unwrap_or_else(|| "(missing)".into());
                let noted = w.get("as").and_then(|n| n.as_str()).and_then(|n| pt.notes.get(n)).and_then(|s| field(s, k)).map(|v| format!(", noted {v}")).unwrap_or_default();
                info!("EXPECT {} {k} = {got} (want {w}{noted}) at {:.2} s", if ok { "PASS" } else { "FAIL" }, pt.t);
            }
        }
        if e.quit {
            info!("PLAYTEST RESULT: {} passed, {} failed", pt.passed, pt.failed);
            exit.write(if pt.failed > 0 { AppExit::error() } else { AppExit::Success });
        }
        if let (Some(d), Ok(mut h)) = (e.hurt, roman.single_mut()) {
            h.0 -= d;
        }
        if let (Some(d), Ok(p)) = (e.kill, player.single()) {
            let me = p.translation();
            if let Some((mut g, mut h, _)) = grunts.iter_mut().filter(|(g, _, _)| g.mind != crate::ai::Mind::Dead).min_by(|a, b| a.2.translation().distance(me).total_cmp(&b.2.translation().distance(me))) {
                g.last_hit = d;
                h.0 -= d;
            }
        }
        if let Some(d) = e.kill_gunner {
            for (mut g, mut h, _) in grunts.iter_mut().filter(|(g, _, _)| g.mind != crate::ai::Mind::Dead && g.gun.is_some()) {
                g.last_hit = d;
                h.0 -= d;
            }
        }
        if let Some(w) = e.warp {
            pt.warp = Some(w);
        }
        if let (Some(true), Ok(p)) = (e.cover, player.single()) {
            let me = p.translation();
            if let Some((mut g, _, _)) = grunts.iter_mut().filter(|(g, _, _)| g.mind != crate::ai::Mind::Dead).min_by(|a, b| a.2.translation().distance(me).total_cmp(&b.2.translation().distance(me))) {
                g.force_hide = true;
            }
        }
        if let Some(g) = e.spawn {
            let template = e.template.clone().unwrap_or_else(|| "grunt_regular".into());
            spawn.write(crate::level::SpawnGrunt { at: Vec3::new(g[0], g[2], -g[1]), template, ..default() });
        }
        if let Some(g) = e.noise {
            noise.post(Vec3::new(g[0], g[2], -g[1]), 200.0, 2.0);
        }
        if e.barks_ready == Some(true) {
            barks.all_ready();
        }
    }
}

/// The `screen` step's measures of a captured frame (see Event::screen).
fn measure_screen(name: String) -> impl FnMut(On<ScreenshotCaptured>, ResMut<PlayState>) {
    move |shot, mut state| {
        let img = &shot.image;
        let (w, h) = (img.width(), img.height());
        let px = |x: u32, y: u32| img.get_color_at(x, y).map(|c| c.to_srgba()).ok();
        let (mut side, mut dark) = (0u32, 0u32);
        for y in (h * 74 / 100)..(h * 83 / 100) {
            for x in (0..w * 12 / 100).chain(w * 88 / 100..w) {
                if let Some(c) = px(x, y) {
                    side += 1;
                    dark += (c.red.max(c.green).max(c.blue) < 16.0 / 255.0) as u32;
                }
            }
        }
        let (mut n, mut luma) = (0u32, 0.0f64);
        for y in (0..h).step_by(4) {
            for x in (0..w).step_by(4) {
                if let Some(c) = px(x, y) {
                    n += 1;
                    luma += (0.2126 * c.red + 0.7152 * c.green + 0.0722 * c.blue) as f64;
                }
            }
        }
        let r = |v: f64| (v * 1000.0).round() / 1000.0;
        let side_dark = r(dark as f64 / side.max(1) as f64);
        let luma = r(luma / n.max(1) as f64);
        info!("EVENT playtest: screen {name}: side_dark {side_dark}, luma {luma}");
        state.0.insert("screen".into(), serde_json::json!({"name": name, "side_dark": side_dark, "luma": luma}));
    }
}

/// The state log's "render": the active cameras and whether they draw into one target texture (the same MSAA and
/// HDR; else Bevy gives a camera a texture of its own, and the HUD camera, which never clears, kept the title
/// menu's art: docs/player-feedback.md 22), and whether a 3D camera is among them.
fn log_render(cams: Query<(&Camera, Option<&Msaa>, Has<bevy::camera::Hdr>, Has<Camera3d>)>, mut state: ResMut<PlayState>) {
    let active: Vec<_> = cams.iter().filter(|c| c.0.is_active).map(|(_, m, hdr, c3)| (m.map_or(1, |m| m.samples()), hdr, c3)).collect();
    let shared = active.windows(2).all(|w| w[0].0 == w[1].0 && w[0].1 == w[1].1);
    state.0.insert("render".into(), serde_json::json!({"cameras": active.len(), "camera3d": active.iter().any(|c| c.2), "shared_target": shared}));
}

/// The wall-clock frame measures (scripts advance game time by 1/60 s a frame, so Time says nothing of how long a
/// frame really took): when this frame began, and the last FRAMES frames' wall time, main-world time and game step.
#[derive(Resource, Default)]
pub struct FrameClock {
    began: Option<std::time::Instant>,
    frames: std::collections::VecDeque<(f32, f32, f32)>,
}

impl FrameClock {
    const FRAMES: usize = 60;
}

fn frame_begin(mut clock: ResMut<FrameClock>) {
    clock.began = Some(std::time::Instant::now());
}

/// The state log's frame measures, over the last 60 frames: "frame_ms" (mean wall time from one frame's start to the
/// next), "frame_ms_max", "main_ms" (mean main-world time, First to Last: the CPU the game's systems take),
/// "dt_max_ms" (the longest game time step), and the level's size: "entities", "romans", "companions" (alive)
/// (restart_parity compares them after a restart with a fresh level's).
#[allow(clippy::type_complexity)]
fn log_frames(
    mut clock: ResMut<FrameClock>,
    mut state: ResMut<PlayState>,
    vtime: Res<Time<Virtual>>,
    entities: Query<()>,
    romans: Query<(), With<crate::player::Roman>>,
    companions: Query<&crate::ai::Companion>,
    mut last_begin: Local<Option<std::time::Instant>>,
) {
    let now = std::time::Instant::now();
    let Some(began) = clock.began else { return };
    let wall = last_begin.map_or(0.0, |l| (began - l).as_secs_f32() * 1000.0);
    *last_begin = Some(began);
    let main = (now - began).as_secs_f32() * 1000.0;
    clock.frames.push_back((wall, main, vtime.delta_secs() * 1000.0));
    while clock.frames.len() > FrameClock::FRAMES {
        clock.frames.pop_front();
    }
    let n = clock.frames.len().max(1) as f32;
    let r = |v: f32| (v * 10.0).round() / 10.0;
    let (sum_wall, max_wall, sum_main, max_dt) = clock.frames.iter().fold((0.0, 0.0f32, 0.0, 0.0f32), |a, f| (a.0 + f.0, a.1.max(f.0), a.2 + f.1, a.3.max(f.2)));
    state.0.insert("frame_ms".into(), r(sum_wall / n).into());
    state.0.insert("frame_ms_max".into(), r(max_wall).into());
    state.0.insert("main_ms".into(), r(sum_main / n).into());
    state.0.insert("dt_max_ms".into(), r(max_dt).into());
    state.0.insert("entities".into(), entities.iter().count().into());
    state.0.insert("romans".into(), romans.iter().count().into());
    state.0.insert("companions".into(), companions.iter().filter(|c| c.dead.is_none()).count().into());
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

/// The script's virtual gamepad: raw button / axis changes, processed by Bevy's gamepad systems next frame.
fn pad_drive(
    mut commands: Commands,
    mut pt: ResMut<Playtest>,
    mut conn: MessageWriter<bevy::input::gamepad::GamepadConnectionEvent>,
    mut raw: MessageWriter<bevy::input::gamepad::RawGamepadEvent>,
) {
    use bevy::input::gamepad::{GamepadAxis as A, GamepadButton as B};
    for cmd in std::mem::take(&mut pt.pad) {
        // "PlayStation" in the first command makes it a DualSense (Sony vendor id), else an Xbox pad
        let sony = cmd.contains("PlayStation");
        let e = *pt.pad_e.get_or_insert_with(|| {
            let e = commands.spawn_empty().id();
            conn.write(bevy::input::gamepad::GamepadConnectionEvent::new(
                e,
                bevy::input::gamepad::GamepadConnection::Connected { name: "playtest pad".into(), vendor_id: Some(if sony { 0x054c } else { 0x045e }), product_id: None },
            ));
            e
        });
        for part in cmd.split(',') {
            let Some((n, v)) = part.split_once('=') else { continue };
            let v: f32 = v.trim().parse().unwrap_or(0.0);
            let b = match n.trim() {
                "South" => Some(B::South),
                "East" => Some(B::East),
                "West" => Some(B::West),
                "North" => Some(B::North),
                "LeftTrigger" => Some(B::LeftTrigger),
                "RightTrigger" => Some(B::RightTrigger),
                "LeftTrigger2" => Some(B::LeftTrigger2),
                "RightTrigger2" => Some(B::RightTrigger2),
                "Start" => Some(B::Start),
                "Select" => Some(B::Select),
                "RightThumb" => Some(B::RightThumb),
                "DPadUp" => Some(B::DPadUp),
                "DPadDown" => Some(B::DPadDown),
                "DPadLeft" => Some(B::DPadLeft),
                "DPadRight" => Some(B::DPadRight),
                _ => None,
            };
            let a = match n.trim() {
                "LeftStickX" => Some(A::LeftStickX),
                "LeftStickY" => Some(A::LeftStickY),
                "RightStickX" => Some(A::RightStickX),
                "RightStickY" => Some(A::RightStickY),
                _ => None,
            };
            if let Some(b) = b {
                raw.write(bevy::input::gamepad::RawGamepadEvent::Button(bevy::input::gamepad::RawGamepadButtonChangedEvent::new(e, b, v)));
            }
            if let Some(a) = a {
                raw.write(bevy::input::gamepad::RawGamepadEvent::Axis(bevy::input::gamepad::RawGamepadAxisChangedEvent::new(e, a, v)));
            }
        }
    }
}

/// The hurt_companion step: damage straight to Jonesy (0) or Q (1). The blast step: an explosion at Roman.
fn hurt_companions(
    mut pt: ResMut<Playtest>,
    mut q: Query<(&crate::ai::Companion, &mut crate::fx::Health)>,
    roman: Query<(&GlobalTransform, &crate::fx::Health), (With<crate::player::Roman>, Without<crate::ai::Companion>)>,
    mut boom: MessageWriter<crate::fx::Explode>,
) {
    for d in std::mem::take(&mut pt.blasts) {
        if let Ok((t, h)) = roman.single() {
            info!("EVENT playtest: blast {d} at Roman (health {:.0})", h.0);
            boom.write(crate::fx::Explode { at: t.translation(), area_damage: d, radius_inner: 10.0, radius_outer: 20.0, impact_force: 0.0, source: None, delay: 0.0, by: crate::fx::Src::Badguy, cartoon: false });
        }
    }
    for [who, amount] in std::mem::take(&mut pt.hurt_c) {
        for (c, mut h) in &mut q {
            if c.q == (who >= 0.5) {
                h.0 -= amount;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    /// Expect values: equal, a range, null for absent, and {"as": note} within margin + pct of the noted value.
    #[test]
    fn expects() {
        let fresh = json!({"grunts_alive": 20, "frame_ms": 10.0, "cam_mode": "follow", "render": {"cameras": 3}});
        let notes: std::collections::HashMap<String, _> = [("fresh".to_string(), fresh.as_object().unwrap().clone())].into();
        let holds = |k: &str, w: serde_json::Value, g: Option<serde_json::Value>| expect_holds(k, &w, g.as_ref(), &notes);
        assert!(holds("clip", json!(8), Some(json!(8.0))));
        assert!(holds("health", json!([399, 401]), Some(json!(400))));
        assert!(holds("menu.art", json!(null), None));
        assert!(holds("grunts_alive", json!({"as": "fresh"}), Some(json!(20))));
        assert!(!holds("grunts_alive", json!({"as": "fresh"}), Some(json!(19))));
        assert!(holds("grunts_alive", json!({"as": "fresh", "margin": 1}), Some(json!(19))));
        assert!(holds("frame_ms", json!({"as": "fresh", "pct": 50, "margin": 3}), Some(json!(18.0))));
        assert!(!holds("frame_ms", json!({"as": "fresh", "pct": 50, "margin": 3}), Some(json!(300.0))), "the 3 fps of feedback 28");
        assert!(holds("cam_mode", json!({"as": "fresh"}), Some(json!("follow"))));
        assert!(holds("render.cameras", json!({"as": "fresh"}), Some(json!(3))));
        assert!(!holds("grunts_alive", json!({"as": "nothing"}), Some(json!(20))), "no such note");
        assert!(!holds("grunts_alive", json!({"as": "fresh"}), None), "missing now");
    }
}
