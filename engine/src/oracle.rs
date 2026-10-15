//! Lockstep comparison with the original game (tools/oracle.py, docs/remaster-engine.md "Oracle runs"): a playtest
//! script with an `"oracle"` section gets the same scripted input the hook DLL feeds the original
//! (hook/src/testoracle.cpp, input_hooks.cpp) and writes the same per-frame state trace, so the two runs can be
//! compared field by field.
//!
//! ```json
//! {"oracle": {"keys": "0:ENTER*3,40:17*300,200:MOUSE1", "frames": 600, "trace_every": 6, "play_delay": 30,
//!             "warp": [x, y, z]}}
//! ```
//! - Play frames: frame 0 is `play_delay` frames after the level's first paused message (the briefing) appears, as in
//!   the hook; a scenario dismisses it with its own frame-0 key.
//! - `keys`: the hook's [test] keys_frames form, `frame:KEY` or `frame:KEY*held_frames` (3 by default): KEY is a
//!   DirectInput scan code (17 W, 30 A, 31 S, 32 D, 57 Space ...), ENTER / ESC / SPACE / UP / DOWN / LEFT / RIGHT,
//!   MOUSE1..3, or MOUSEX<n> / MOUSEY<n> (mouse counts per frame while held).
//! - `warp`: Roman is put there (game space) at play frame -10, keeping his heading, as [test] play_warp.
//! - The trace (`<script>.trace.jsonl`): one line every `trace_every` frames from frame 0, the schema of
//!   hook/src/testoracle.cpp in game space (Z up); the run quits after `frames`.

use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::prelude::*;
use serde::Deserialize;
use std::io::Write;

#[derive(Deserialize, Clone, Default)]
pub struct OracleConfig {
    #[serde(default)]
    pub keys: String,
    #[serde(default)]
    pub frames: i64,
    #[serde(default)]
    pub trace_every: i64,
    #[serde(default = "default_delay")]
    pub play_delay: i64,
    #[serde(default)]
    pub warp: Option<[f32; 3]>,
}

fn default_delay() -> i64 {
    30
}

#[derive(Deserialize)]
struct ScriptOracle {
    #[serde(default)]
    oracle: Option<OracleConfig>,
}

/// One scripted input: down during play frames [at, at + hold).
#[derive(Clone, Debug, PartialEq)]
pub enum Input {
    Key(KeyCode),
    Mouse(MouseButton),
    Motion(Vec2),
}

#[derive(Clone, Debug)]
pub struct Scripted {
    pub at: i64,
    pub hold: i64,
    pub input: Input,
}

/// A DirectInput scan code (as the hook's keys take them) as a key.
fn dik(code: u32) -> Option<KeyCode> {
    use KeyCode::*;
    let digits = [Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9, Digit0];
    Some(match code {
        1 => Escape,
        2..=11 => digits[(code - 2) as usize],
        15 => Tab,
        16 => KeyQ,
        17 => KeyW,
        18 => KeyE,
        19 => KeyR,
        20 => KeyT,
        28 => Enter,
        29 => ControlLeft,
        30 => KeyA,
        31 => KeyS,
        32 => KeyD,
        33 => KeyF,
        34 => KeyG,
        42 => ShiftLeft,
        44 => KeyZ,
        45 => KeyX,
        46 => KeyC,
        47 => KeyV,
        57 => Space,
        200 => ArrowUp,
        203 => ArrowLeft,
        205 => ArrowRight,
        208 => ArrowDown,
        _ => return None,
    })
}

/// Parses the hook's keys_frames form (hook/src/input_hooks.cpp InputHooksInit).
pub fn parse_keys(text: &str) -> Vec<Scripted> {
    let mut out = Vec::new();
    for tok in text.split(',').map(str::trim).filter(|t| !t.is_empty()) {
        let Some((at, rest)) = tok.split_once(':') else { continue };
        let Ok(at) = at.trim().parse::<f64>() else { continue };
        let (name, hold) = match rest.split_once('*') {
            Some((n, h)) => (n, h.trim().parse::<f64>().unwrap_or(3.0)),
            None => (rest, 3.0),
        };
        let name = name.trim().to_uppercase();
        let input = if let Some(n) = name.strip_prefix("MOUSEX") {
            Input::Motion(Vec2::new(n.parse().unwrap_or(0.0), 0.0))
        } else if let Some(n) = name.strip_prefix("MOUSEY") {
            Input::Motion(Vec2::new(0.0, n.parse().unwrap_or(0.0)))
        } else if let Some(n) = name.strip_prefix("MOUSE") {
            Input::Mouse(match n {
                "2" => MouseButton::Right,
                "3" => MouseButton::Middle,
                _ => MouseButton::Left,
            })
        } else {
            let k = match name.as_str() {
                "ESC" => Some(KeyCode::Escape),
                "ENTER" => Some(KeyCode::Enter),
                "SPACE" => Some(KeyCode::Space),
                "UP" => Some(KeyCode::ArrowUp),
                "DOWN" => Some(KeyCode::ArrowDown),
                "LEFT" => Some(KeyCode::ArrowLeft),
                "RIGHT" => Some(KeyCode::ArrowRight),
                n => n.parse::<u32>().ok().and_then(dik),
            };
            match k {
                Some(k) => Input::Key(k),
                None => {
                    warn!("oracle: unknown key {name}");
                    continue;
                }
            }
        };
        out.push(Scripted { at: at as i64, hold: hold.round() as i64, input });
    }
    out
}

#[derive(Resource)]
pub struct Oracle {
    cfg: OracleConfig,
    keys: Vec<Scripted>,
    /// frames counted from the first frame of play
    frame: i64,
    /// the frame the briefing appeared
    brief: Option<i64>,
    clock0: f32,
    out: Option<std::fs::File>,
    log_seen: usize,
    warped: bool,
    /// inputs down last frame (released when their span ends)
    down: Vec<Input>,
    /// placed destructible objects alive last frame, and the ones gone since the last trace line
    alive: std::collections::HashSet<u32>,
    deaths: Vec<u32>,
}

impl Oracle {
    /// Play frames (see the module docs); None before the briefing appeared.
    pub fn play_frame(&self) -> Option<i64> {
        self.brief.map(|b| self.frame - (b + self.cfg.play_delay))
    }
}

pub struct OraclePlugin;

impl Plugin for OraclePlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Startup, setup.run_if(resource_exists::<crate::playtest::Playtest>))
            .add_systems(First, count.run_if(resource_exists::<Oracle>))
            .add_systems(PreUpdate, drive.after(crate::playtest::drive).before(crate::input::InputDevices).run_if(resource_exists::<Oracle>))
            .add_systems(Last, trace.run_if(resource_exists::<Oracle>));
    }
}

fn setup(mut commands: Commands, pt: Res<crate::playtest::Playtest>) {
    let Some(cfg) = std::fs::read_to_string(&pt.path).ok().and_then(|t| serde_json::from_str::<ScriptOracle>(&t).ok()).and_then(|s| s.oracle) else { return };
    let keys = parse_keys(&cfg.keys);
    let out = (cfg.trace_every > 0).then(|| std::fs::File::create(pt.path.with_extension("trace.jsonl")).ok()).flatten();
    info!("EVENT oracle: {} scripted inputs, {} frames, trace every {}", keys.len(), cfg.frames, cfg.trace_every);
    commands.insert_resource(Oracle { cfg, keys, frame: 0, brief: None, clock0: 0.0, out, log_seen: 0, warped: false, down: Vec::new(), alive: Default::default(), deaths: Vec::new() });
}

fn count(mut o: ResMut<Oracle>, mission: Option<Res<crate::mission::Mission>>) {
    o.frame += 1;
    // the briefing: the first paused message once the level plays (none after 600 frames: the clock starts anyway)
    if o.brief.is_none() && (mission.is_some_and(|m| m.showing.is_some()) || o.frame >= 600) {
        o.brief = Some(o.frame);
        info!("EVENT oracle: briefing at frame {}", o.frame);
    }
}

/// The scripted inputs for this play frame (after the playtest's own drive, which clears the mouse motion).
fn drive(
    mut o: ResMut<Oracle>,
    mut keys: ResMut<ButtonInput<KeyCode>>,
    mut buttons: ResMut<ButtonInput<MouseButton>>,
    mut motion: ResMut<AccumulatedMouseMotion>,
    mut pt: ResMut<crate::playtest::Playtest>,
    roman: Query<&crate::player::Roman>,
    mut exit: MessageWriter<AppExit>,
) {
    let Some(f) = o.play_frame() else { return };
    if let (Some(w), false, Ok(r)) = (o.cfg.warp, o.warped, roman.single()) {
        if f >= -10 {
            o.warped = true;
            pt.request_warp([w[0], w[1], w[2], r.yaw.to_degrees()]);
        }
    }
    let now: Vec<Input> = o.keys.iter().filter(|k| f >= k.at && f < k.at + k.hold).map(|k| k.input.clone()).collect();
    for i in &now {
        match i {
            Input::Key(k) => keys.press(*k),
            Input::Mouse(b) => buttons.press(*b),
            Input::Motion(d) => motion.delta += *d,
        }
    }
    for i in std::mem::take(&mut o.down) {
        if !now.contains(&i) {
            match i {
                Input::Key(k) => keys.release(k),
                Input::Mouse(b) => buttons.release(b),
                Input::Motion(_) => {}
            }
        }
    }
    o.down = now;
    if o.cfg.frames > 0 && f > o.cfg.frames {
        info!("EVENT oracle: done at play frame {f}");
        exit.write(AppExit::Success);
    }
}

#[allow(clippy::type_complexity)]
fn trace(
    mut o: ResMut<Oracle>,
    vtime: Res<Time<Virtual>>,
    p: Res<crate::player::PlayerStats>,
    mission: Option<Res<crate::mission::Mission>>,
    mut bank: Option<ResMut<crate::audio::SoundBank>>,
    roman: Query<(Entity, &GlobalTransform), With<crate::player::Roman>>,
    grunts: Query<(Entity, &crate::ai::Grunt, &crate::fx::Health, &GlobalTransform, Option<&crate::level::InstanceId>)>,
    companions: Query<(&crate::ai::Companion, &crate::fx::Health, &GlobalTransform)>,
    ids: Query<&crate::level::InstanceId>,
    objects: Query<(&crate::level::InstanceId, &crate::fx::Destructible)>,
) {
    let Some(f) = o.play_frame() else { return };
    if let Some(b) = bank.as_deref_mut() {
        b.log_started = f >= 0 && o.out.is_some();
    }
    if f < 0 || o.out.is_none() {
        return;
    }
    // deaths: a placed destructible gone (despawned, or replaced by its husk) or out of health with no fuse burning, as
    // the original's DefaultGameObject_OnDeath 0x00450ee0 / FxExplosion_Die 0x0041c570 (the hook's "deaths")
    let alive: std::collections::HashSet<u32> = objects.iter().filter(|(_, d)| d.health > 0.0 || d.dying.is_some_and(|t| t > 0.0)).map(|(i, _)| i.0).collect();
    if f > 0 {
        let gone: Vec<u32> = o.alive.difference(&alive).copied().collect();
        o.deaths.extend(gone);
    }
    o.alive = alive;
    if f == 0 {
        o.clock0 = vtime.elapsed_secs();
        o.log_seen = mission.as_ref().map_or(0, |m| m.log.len());
    }
    if f % o.cfg.trace_every.max(1) != 0 {
        return;
    }
    let to_game = crate::level::game_to_engine().inverse();
    let g = |v: Vec3| {
        let v = to_game * v;
        [r2(v.x), r2(v.y), r2(v.z)]
    };
    let roman_e = roman.single().ok();
    let mut line = serde_json::Map::new();
    line.insert("f".into(), f.into());
    line.insert("t".into(), r2(vtime.elapsed_secs() - o.clock0).into());
    line.insert("paused".into(), (vtime.is_paused() as i32).into());
    if let Some((_, t)) = roman_e {
        // the model's facing (its -Y axis in game space) as the hook reports it
        let facing = to_game * (t.rotation() * (crate::level::game_to_engine() * Vec3::NEG_Y));
        line.insert(
            "roman".into(),
            serde_json::json!({"pos": g(p.position), "heading": r2(facing.x.atan2(facing.y).to_degrees()), "health": r2(p.health),
                "max_health": r2(p.max_health), "weapon": p.weapon, "clip": p.clip, "ammo": p.ammo, "stickies": p.stickies}),
        );
    }
    let mut comps: Vec<(bool, serde_json::Value)> = companions.iter().map(|(c, h, t)| {
        let v = g(t.translation());
        (c.q, serde_json::json!([v[0], v[1], v[2], r2(h.0)]))
    }).collect();
    comps.sort_by_key(|c| c.0); // Jonesy, then Q (the hook's order: player +0xad0, +0xad4)
    line.insert("companions".into(), comps.into_iter().map(|c| c.1).collect::<Vec<_>>().into());
    let gs: Vec<serde_json::Value> = grunts
        .iter()
        .map(|(_, gr, h, t, id)| {
            let target = match gr.target {
                Some(e) if roman_e.is_some_and(|r| r.0 == e) => serde_json::json!("roman"),
                Some(e) => ids.get(e).map(|i| serde_json::json!(i.0)).unwrap_or(serde_json::Value::Null),
                None => serde_json::Value::Null,
            };
            serde_json::json!({"id": id.map_or(0, |i| i.0), "pos": g(t.translation()), "health": r2(h.0), "mind": format!("{:?}", gr.mind), "target": target})
        })
        .collect();
    line.insert("grunts".into(), gs.into());
    let mut triggers = Vec::new();
    if let Some(m) = &mission {
        line.insert("mission".into(), serde_json::json!({"state": format!("{:?}", m.outcome), "objective": m.compass_tag}));
        for l in m.log.iter().skip(o.log_seen) {
            // mission.rs dispatch: "trigger {t}: {n} receivers"
            if let Some(t) = l.strip_prefix("trigger ").and_then(|r| r.split(':').next()).and_then(|t| t.parse::<i64>().ok()) {
                triggers.push(t);
            }
        }
        o.log_seen = m.log.len();
    }
    line.insert("triggers".into(), triggers.into());
    let mut deaths = std::mem::take(&mut o.deaths);
    deaths.sort_unstable();
    line.insert("deaths".into(), deaths.into());
    let sounds: Vec<String> = bank.as_deref_mut().map(|b| std::mem::take(&mut b.started_keys)).unwrap_or_default();
    line.insert("sounds".into(), sounds.into());
    if let Some(out) = o.out.as_mut() {
        let _ = writeln!(out, "{}", serde_json::Value::Object(line));
    }
}

fn r2(v: f32) -> f32 {
    (v * 100.0).round() / 100.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keys_parse_like_the_hook() {
        let k = parse_keys("0:ENTER,40:17*300,200:MOUSE1,210:MOUSEX-20*10,5:57");
        assert_eq!(k.len(), 5);
        assert_eq!((k[0].at, k[0].hold, k[0].input.clone()), (0, 3, Input::Key(KeyCode::Enter)));
        assert_eq!((k[1].at, k[1].hold, k[1].input.clone()), (40, 300, Input::Key(KeyCode::KeyW)));
        assert_eq!(k[2].input, Input::Mouse(MouseButton::Left));
        assert_eq!((k[3].hold, k[3].input.clone()), (10, Input::Motion(Vec2::new(-20.0, 0.0))));
        assert_eq!(k[4].input, Input::Key(KeyCode::Space));
    }
}
