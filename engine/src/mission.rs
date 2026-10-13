//! The level's mission logic as the original runs it: numbered triggers between placed objects
//! (docs/subsystems/mission.md; data: tools/remaster_export_mission.py -> mission.json). Senders: trigger volumes
//! (on enter, one-shot or not), the mission (start), ticker bomb sites (explode, not enabled), the pub (exit),
//! deaths. Receivers: ui_ears (objective text as a paused message + compass target), sound_ears (voice line),
//! AI (wake up), barracks (release troops), objects (destroyed), bomb sites (enable), the mission (win).
//! Prompts ("Hold down %s to set the ticker bomb!", "Press %s to enter pub") come from the hint texts with the
//! bound key's name. STAND-INS, labelled where used: the pub screen (a message), the bomb clock (text).

use crate::ai::{Grunt, Mind, Player};
use crate::fx::{Destructible, Explode, StartFx};
use crate::level::{InstanceId, game_to_engine};
use bevy::prelude::*;
use serde::Deserialize;
use serde_json::{Map, Value};
use std::collections::HashMap;

#[derive(Deserialize)]
struct EdgeEnd {
    id: Option<u32>,
    #[serde(default)]
    template: Option<String>,
    #[serde(default)]
    event: Option<String>,
    #[serde(default)]
    action: Option<String>,
}

#[derive(Deserialize)]
struct Edge {
    trigger: i64,
    from: EdgeEnd,
    to: EdgeEnd,
    #[serde(default)]
    detail: Option<Map<String, Value>>,
}

#[derive(Deserialize)]
struct InstanceDef {
    id: u32,
    template: String,
    #[serde(default)]
    position: Option<[f32; 3]>,
    #[serde(default)]
    resolved: Map<String, Value>,
    #[serde(default)]
    volume: Option<Map<String, Value>>,
}

#[derive(Deserialize)]
struct Hint {
    text: Option<String>,
}

#[derive(Deserialize)]
struct MissionFile {
    mission: MissionInst,
    edges: Vec<Edge>,
    instances: Vec<InstanceDef>,
    text: HashMap<String, String>,
    hints: HashMap<String, Hint>,
}

#[derive(Deserialize)]
struct MissionInst {
    resolved: Map<String, Value>,
}

#[derive(Clone, Debug)]
enum Action {
    WakeAi(u32),
    ShowText(String),
    Compass(i64),
    Voice { q: Option<String>, jonesy: Option<String>, roman: Option<String> },
    Fx(String, Vec3),
    Barracks(u32),
    Destroy(u32),
    EnableSite(u32),
    Win,
}

enum Volume {
    Sphere(Vec3, f32),
    /// engine-space aabb
    Box(Vec3, Vec3),
}

struct Trigger {
    id: u32,
    sends: i64,
    volume: Volume,
    one_shot: bool,
    inside: bool,
    done: bool,
}

struct Site {
    id: u32,
    pos: Vec3,
    radius: f32,
    enabled: bool,
    explode: i64,
    non_enabled: i64,
    armed: Option<f32>,
    hold: f32,
    done: bool,
}

struct Barracks {
    id: u32,
    pos: Vec3,
    left: i64,
    amount: i64,
    timer: f32,
    release: (f32, f32),
    active: bool,
}

/// Mission outcome for the state log.
#[derive(Clone, Copy, PartialEq, Debug, Default)]
pub enum Outcome {
    #[default]
    Playing,
    Won,
    Failed,
}

#[derive(Resource)]
pub struct Mission {
    receivers: HashMap<i64, Vec<Action>>,
    triggers: Vec<Trigger>,
    sites: Vec<Site>,
    pub_pos: Option<(Vec3, f32, i64)>,
    pub_done: bool,
    barracks: Vec<Barracks>,
    texts: HashMap<String, String>,
    hints: HashMap<String, String>,
    tags: HashMap<i64, Vec<Vec3>>,
    pub compass_tag: i64,
    start_trigger: i64,
    start_text: Option<String>,
    success_text: Option<String>,
    failure_text: Option<String>,
    started: bool,
    /// paused messages waiting for "continue", and the one shown
    queue: Vec<(f32, String)>,
    pub showing: Option<String>,
    pub tickers: u32,
    pub outcome: Outcome,
    dead_timer: f32,
    pub log: Vec<String>,
    pub objectives: String,
}

#[derive(Message, Clone, Copy)]
pub struct SendTrigger(pub i64);

/// The key names the prompts substitute for %s (the original's default bindings, docs/subsystems/player.md).
pub const KEY_USE: &str = "F";
pub const KEY_CONTINUE: &str = "Enter";

fn game_pos(p: [f32; 3]) -> Vec3 {
    game_to_engine() * Vec3::from(p)
}

/// The text as shown in game: the long form (<long> kept, <short> dropped: Renderer_TextSkipTaggedBlock).
pub fn long_form(t: &str) -> String {
    let mut s = t.to_string();
    while let (Some(a), Some(b)) = (s.find("<short>"), s.find("</short>")) {
        if b < a {
            break;
        }
        s.replace_range(a..b + "</short>".len(), "");
    }
    s.replace("<long>", "").replace("</long>", "").replace("<BR>", "<br>")
}

impl Mission {
    pub fn load(dir: &std::path::Path) -> Option<Self> {
        let f: MissionFile = serde_json::from_str(&std::fs::read_to_string(dir.join("mission.json")).ok()?).map_err(|e| warn!("mission.json: {e}")).ok()?;
        let inst: HashMap<u32, &InstanceDef> = f.instances.iter().map(|i| (i.id, i)).collect();
        let s = |m: &Map<String, Value>, k: &str| m.get(k).and_then(|v| v.as_str()).filter(|v| !v.is_empty() && *v != "none").map(str::to_owned);
        let n = |m: &Map<String, Value>, k: &str| m.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0);
        let mut receivers: HashMap<i64, Vec<Action>> = HashMap::new();
        let mut triggers = Vec::new();
        for e in &f.edges {
            // senders: volumes that fire on enter
            if let (Some(fid), Some(ev)) = (e.from.id, e.from.event.as_deref()) {
                if ev.contains("enters the volume") && !triggers.iter().any(|t: &Trigger| t.id == fid && t.sends == e.trigger) {
                    if let Some(i) = inst.get(&fid) {
                        let vol = i.volume.as_ref();
                        let kind = vol.and_then(|v| v.get("kind")).and_then(|v| v.as_str()).unwrap_or("sphere");
                        let volume = if kind == "box" {
                            let aabb = vol.and_then(|v| v.get("world_aabb")).and_then(|v| v.as_array()).cloned().unwrap_or_default();
                            let p = |i: usize| aabb.get(i).and_then(|v| v.as_array()).map(|a| [a[0].as_f64().unwrap_or(0.0) as f32, a[1].as_f64().unwrap_or(0.0) as f32, a[2].as_f64().unwrap_or(0.0) as f32]).unwrap_or([0.0; 3]);
                            let (a, b) = (game_pos(p(0)), game_pos(p(1)));
                            Volume::Box(a.min(b), a.max(b))
                        } else {
                            let r = vol.and_then(|v| v.get("radius")).and_then(|v| v.as_f64()).unwrap_or(20.0) as f32;
                            Volume::Sphere(game_pos(i.position.unwrap_or_default()), r)
                        };
                        let one_shot = vol.and_then(|v| v.get("one_shot")).and_then(|v| v.as_i64()).unwrap_or(1) != 0;
                        triggers.push(Trigger { id: fid, sends: e.trigger, volume, one_shot, inside: false, done: false });
                    }
                }
            }
            // receivers
            let Some(to) = e.to.id else { continue };
            let action = e.to.action.as_deref().unwrap_or("");
            let d = e.detail.clone().unwrap_or_default();
            let a = if action.starts_with("AI wakes up") {
                Action::WakeAi(to)
            } else if action.starts_with("show text_display") {
                match s(&d, "text_id") {
                    Some(t) => Action::ShowText(t),
                    None => continue,
                }
            } else if action.starts_with("compass target") {
                Action::Compass(d.get("objective_tag").and_then(|v| v.as_i64()).unwrap_or(0))
            } else if action.starts_with("play one voice line") {
                let r = inst.get(&to).map(|i| &i.resolved);
                Action::Voice {
                    q: r.and_then(|r| s(r, "q_sound")),
                    jonesy: r.and_then(|r| s(r, "jonsey_sound")),
                    roman: r.and_then(|r| s(r, "roman_sound")),
                }
            } else if action.starts_with("toggle the fx") {
                match (s(&d, "fx_name"), inst.get(&to).and_then(|i| i.position)) {
                    (Some(fx), Some(p)) => Action::Fx(fx, game_pos(p)),
                    _ => continue,
                }
            } else if action.starts_with("barracks") {
                Action::Barracks(to)
            } else if action.starts_with("destroyed") {
                Action::Destroy(to)
            } else if action.starts_with("enable this bomb site") {
                Action::EnableSite(to)
            } else if action.starts_with("WIN") {
                Action::Win
            } else {
                continue;
            };
            receivers.entry(e.trigger).or_default().push(a);
        }
        let mut sites = Vec::new();
        let mut barracks = Vec::new();
        let mut pub_pos = None;
        let mut tags: HashMap<i64, Vec<Vec3>> = HashMap::new();
        for i in &f.instances {
            let r = &i.resolved;
            let pos = game_pos(i.position.unwrap_or_default());
            let tag = n(r, "tag") as i64;
            if tag != 0 {
                tags.entry(tag).or_default().push(pos);
            }
            match i.template.as_str() {
                "player_tickerbomb" => sites.push(Site {
                    id: i.id,
                    pos,
                    radius: n(r, "message_radius").max(10.0) as f32,
                    enabled: n(r, "receive_trigger_enable") == 0.0,
                    explode: n(r, "send_trigger_explode") as i64,
                    non_enabled: n(r, "send_trigger_non_enabled") as i64,
                    armed: None,
                    hold: 0.0,
                    done: false,
                }),
                "pub" => pub_pos = Some((pos, n(r, "doorway_entry_radius").max(8.0) as f32 + 22.0, n(r, "send_trigger_exit") as i64)),
                t if t.starts_with("grunt_barracks") => barracks.push(Barracks {
                    id: i.id,
                    pos,
                    left: (n(r, "num_regular") + n(r, "num_bow") + n(r, "num_captain")) as i64,
                    amount: n(r, "release_amount").max(1.0) as i64,
                    timer: 0.0,
                    release: (n(r, "release_time_min").max(0.2) as f32, n(r, "release_time_max").max(0.5) as f32),
                    active: false,
                }),
                _ => {}
            }
        }
        let m = &f.mission.resolved;
        Some(Mission {
            receivers,
            triggers,
            sites,
            pub_pos,
            pub_done: false,
            barracks,
            hints: f.hints.into_iter().filter_map(|(k, h)| h.text.map(|t| (k, t))).collect(),
            tags,
            compass_tag: n(m, "compass_objective_tag") as i64,
            start_trigger: n(m, "send_trigger_start") as i64,
            start_text: s(m, "start_text").and_then(|t| f.text.get(&t).cloned()),
            success_text: s(m, "success_text").and_then(|t| f.text.get(&t).cloned()),
            failure_text: s(m, "failure_text").and_then(|t| f.text.get(&t).cloned()),
            texts: f.text,
            started: false,
            queue: Vec::new(),
            showing: None,
            tickers: 0,
            outcome: Outcome::Playing,
            dead_timer: 0.0,
            log: Vec::new(),
            objectives: String::new(),
        })
    }

    fn hint(&self, key: &str) -> String {
        self.hints.get(key).cloned().unwrap_or_default().replace("%s", KEY_USE)
    }

    fn message(&mut self, text: &str, delay: f32) {
        self.queue.push((delay, long_form(text)));
    }

    /// The nearest object of the compass tag (engine space).
    pub fn compass_target(&self, from: Vec3) -> Option<Vec3> {
        self.tags.get(&self.compass_tag)?.iter().copied().min_by(|a, b| a.distance(from).total_cmp(&b.distance(from)))
    }
}

pub struct MissionPlugin;

impl Plugin for MissionPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SendTrigger>().add_systems(Update, (start, volumes, sites, pub_door, barracks, dispatch, messages, outcome).chain());
    }
}

fn start(mut m: Option<ResMut<Mission>>, mut out: MessageWriter<SendTrigger>, path: Res<crate::campath::CameraPath>, time: Res<Time>, mut waited: Local<f32>) {
    let Some(m) = m.as_deref_mut() else { return };
    if m.started {
        return;
    }
    // MissionObject_ShowStart: once no camera path is active (it starts on the first frames)
    *waited += time.delta_secs();
    if path.active() || *waited < 0.3 {
        return;
    }
    m.started = true;
    // MissionObject_Update: the first frame sends send_trigger_start; the start text then shows as a paused message
    if m.start_trigger != 0 {
        out.write(SendTrigger(m.start_trigger));
    }
    if let Some(t) = m.start_text.clone() {
        m.objectives = long_form(&t);
        m.message(&t, 0.0);
    }
    m.log.push("start".into());
}

/// Trigger volumes: the player entering sends the trigger (one-shot volumes once).
fn volumes(mut m: Option<ResMut<Mission>>, player: Query<&GlobalTransform, With<Player>>, mut out: MessageWriter<SendTrigger>) {
    let (Some(m), Ok(p)) = (m.as_deref_mut(), player.single()) else { return };
    let p = p.translation() + Vec3::Y * 5.0;
    for t in &mut m.triggers {
        if t.done {
            continue;
        }
        let inside = match t.volume {
            Volume::Sphere(c, r) => c.distance(p) <= r,
            Volume::Box(a, b) => p.cmpge(a - Vec3::splat(2.0)).all() && p.cmple(b + Vec3::Y * 20.0).all(),
        };
        if inside && !t.inside {
            out.write(SendTrigger(t.sends));
            m.log.push(format!("volume {} -> trigger {}", t.id, t.sends));
            if t.one_shot {
                t.done = true;
            }
        }
        t.inside = inside;
    }
}

/// Ticker bomb sites (PlayerTickerBomb): in range with ticker bombs: "Hold down %s to set the ticker bomb!";
/// held for 2 s it arms with a 6 s fuse (0x0044e4a0), then explodes and sends send_trigger_explode.
#[allow(clippy::too_many_arguments)]
fn sites(
    time: Res<Time>,
    keys: Res<ButtonInput<KeyCode>>,
    bot: Res<crate::playtest::Bot>,
    mut m: Option<ResMut<Mission>>,
    player: Query<&GlobalTransform, With<Player>>,
    mut out: MessageWriter<SendTrigger>,
    mut blast: MessageWriter<Explode>,
    mut fx: MessageWriter<StartFx>,
    mut hud: ResMut<crate::hud::HudState>,
    data: Res<crate::gamedata::GameData>,
) {
    let (Some(m), Ok(p)) = (m.as_deref_mut(), player.single()) else { return };
    let dt = time.delta_secs();
    let p = p.translation();
    let mut prompt = None;
    let holding = keys.pressed(KeyCode::KeyF) || keys.pressed(KeyCode::KeyX) || bot.use_key;
    let tickers = m.tickers;
    let mut sends = Vec::new();
    let mut logs = Vec::new();
    for s in &mut m.sites {
        if s.done {
            continue;
        }
        if let Some(fuse) = s.armed.as_mut() {
            *fuse -= dt;
            prompt = Some(format!("{:.0}", fuse.max(0.0).ceil())); // stand-in for ui_clock
            if *fuse <= 0.0 {
                s.done = true;
                let b = data.props("p_player_tickerbomb");
                blast.write(Explode {
                    at: s.pos,
                    area_damage: b.f("area_damage", 100.0),
                    radius_inner: b.f("damage_radius_inner", 40.0),
                    radius_outer: b.f("damage_radius_outer", 60.0),
                    impact_force: b.f("impact_force", 25000.0),
                    source: None,
                    delay: 0.0,
                });
                fx.write(StartFx { name: "fx_ticker_bomb".into(), at: s.pos, rotation: Quat::IDENTITY, follow: None, position_only: false });
                if s.explode != 0 {
                    sends.push(s.explode);
                }
                logs.push(format!("ticker site {} exploded -> trigger {}", s.id, s.explode));
            }
            continue;
        }
        if p.distance(s.pos) > s.radius {
            s.hold = 0.0;
            continue;
        }
        if !s.enabled {
            if tickers > 0 && s.non_enabled != 0 {
                sends.push(s.non_enabled);
                s.non_enabled = 0;
            }
            continue;
        }
        if tickers == 0 {
            prompt = Some("ui_tickerbomb_none".into());
            continue;
        }
        prompt = Some("ui_tickerbomb_hint".into());
        if holding {
            s.hold += dt;
            if s.hold >= 2.0 {
                s.armed = Some(6.0);
                logs.push(format!("ticker site {} armed", s.id));
            }
        } else {
            s.hold = 0.0;
        }
    }
    for t in sends {
        out.write(SendTrigger(t));
    }
    m.log.extend(logs);
    hud.prompt = prompt.map(|k| if k.starts_with("ui_") { m.hint(&k) } else { k });
}

/// The pub: near the doorway "Press %s to enter pub"; entering gives the Ticker Bombs (STAND-IN for the pub
/// screen) and sends send_trigger_exit on leaving.
fn pub_door(keys: Res<ButtonInput<KeyCode>>, bot: Res<crate::playtest::Bot>, mut m: Option<ResMut<Mission>>, player: Query<&GlobalTransform, With<Player>>, mut out: MessageWriter<SendTrigger>, mut hud: ResMut<crate::hud::HudState>) {
    let (Some(m), Ok(p)) = (m.as_deref_mut(), player.single()) else { return };
    let Some((pos, r, exit)) = m.pub_pos else { return };
    if m.pub_done || p.translation().with_y(0.0).distance(pos.with_y(0.0)) > r {
        return;
    }
    if hud.prompt.is_none() {
        hud.prompt = Some(m.hint("ui_pub_entry_hint"));
    }
    if keys.just_pressed(KeyCode::KeyF) || keys.just_pressed(KeyCode::KeyX) || bot.use_key {
        m.pub_done = true;
        m.tickers = 4;
        m.message("<rgb=ffffff>The Pub</rgb><br><br>You got Ticker Bombs!", 0.0);
        m.log.push(format!("pub entered -> tickers, trigger {exit}"));
        if exit != 0 {
            out.write(SendTrigger(exit));
        }
    }
}

/// Barracks release their troops (num_* grunts, release_amount at a time every release_time_min..max).
fn barracks(time: Res<Time>, mut m: Option<ResMut<Mission>>, mut rng: ResMut<crate::fx::FxStats>, mut spawn: MessageWriter<crate::level::SpawnGrunt>) {
    let Some(m) = m.as_deref_mut() else { return };
    let dt = time.delta_secs();
    let mut logs = Vec::new();
    for b in &mut m.barracks {
        if !b.active || b.left <= 0 {
            continue;
        }
        b.timer -= dt;
        if b.timer > 0.0 {
            continue;
        }
        b.timer = rng.range(b.release.0, b.release.1);
        for _ in 0..b.amount.min(b.left) {
            let at = b.pos + Vec3::new(rng.range(-12.0, 12.0), 2.0, rng.range(-12.0, 12.0));
            spawn.write(crate::level::SpawnGrunt { at, template: "grunt_regular".into() });
            b.left -= 1;
        }
        logs.push(format!("barracks {} released troops ({} left)", b.id, b.left));
    }
    m.log.extend(logs);
}

#[allow(clippy::too_many_arguments)]
fn dispatch(
    mut m: Option<ResMut<Mission>>,
    mut events: MessageReader<SendTrigger>,
    mut grunts: Query<(&InstanceId, &mut Grunt)>,
    mut objects: Query<(&InstanceId, &mut Destructible)>,
    player: Query<Entity, With<Player>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut fx: MessageWriter<StartFx>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let Some(m) = m.as_deref_mut() else { return };
    let pe = player.single().ok();
    for SendTrigger(t) in events.read() {
        let actions = m.receivers.get(t).cloned().unwrap_or_default();
        m.log.push(format!("trigger {t}: {} receivers", actions.len()));
        for a in actions {
            match a {
                Action::WakeAi(id) => {
                    for (i, mut g) in &mut grunts {
                        if i.0 == id && g.mind == Mind::WaitTrigger {
                            g.mind = Mind::Roam;
                            g.target = pe;
                        }
                    }
                }
                Action::ShowText(t) => {
                    let text = m.texts.get(&t).cloned().unwrap_or(t);
                    m.objectives = long_form(&text);
                    m.message(&text, 1.0);
                }
                Action::Compass(tag) => m.compass_tag = tag,
                Action::Voice { q, jonesy, roman } => {
                    let pick: Vec<String> = [q, jonesy, roman].into_iter().flatten().collect();
                    if !pick.is_empty() {
                        let i = (rng.rand() * pick.len() as f32) as usize % pick.len();
                        sounds.write(crate::audio::PlaySound::ui(pick[i].clone()));
                    }
                }
                Action::Fx(name, at) => {
                    fx.write(StartFx { name, at, rotation: Quat::IDENTITY, follow: None, position_only: false });
                }
                Action::Barracks(id) => {
                    for b in &mut m.barracks {
                        if b.id == id {
                            b.active = true;
                        }
                    }
                }
                Action::Destroy(id) => {
                    for (i, mut d) in &mut objects {
                        if i.0 == id && d.dying.is_none() {
                            d.health = 0.0;
                            d.dying = Some(0.0);
                        }
                    }
                }
                Action::EnableSite(id) => {
                    for s in &mut m.sites {
                        if s.id == id {
                            s.enabled = true;
                        }
                    }
                }
                Action::Win => {
                    if m.outcome == Outcome::Playing {
                        m.outcome = Outcome::Won;
                        let t = m.success_text.clone().unwrap_or("Well Done!".into());
                        m.message(&t, 0.0);
                        m.log.push("WIN".into());
                    }
                }
            }
        }
    }
}

/// Paused messages (0x00403410 flag 1): the game pauses on the message with "Press %s to continue".
fn messages(
    time: Res<Time<Real>>,
    keys: Res<ButtonInput<KeyCode>>,
    buttons: Res<ButtonInput<MouseButton>>,
    mut m: Option<ResMut<Mission>>,
    mut hud: ResMut<crate::hud::HudState>,
    mut vtime: ResMut<Time<Virtual>>,
) {
    let Some(m) = m.as_deref_mut() else { return };
    if m.showing.is_some() {
        if keys.just_pressed(KeyCode::Enter) || buttons.just_pressed(MouseButton::Left) {
            m.showing = None;
            hud.briefing = None;
            vtime.unpause();
        }
        return;
    }
    let dt = time.delta_secs();
    for q in &mut m.queue {
        q.0 -= dt;
    }
    if let Some(i) = m.queue.iter().position(|q| q.0 <= 0.0) {
        let (_, text) = m.queue.remove(i);
        let cont = m.hints.get("ui_stats_ok").cloned().unwrap_or("Press %s to continue".into()).replace("%s", KEY_CONTINUE);
        hud.briefing = Some(format!("{text}<br><br>{cont}"));
        m.showing = Some(text);
        vtime.pause();
    }
}

/// Lose: the player dead for 5 s (level state 3, "MissionFailed").
fn outcome(time: Res<Time>, mut m: Option<ResMut<Mission>>, roman: Query<&crate::player::Roman>, mut hud: ResMut<crate::hud::HudState>, cam: Query<&GlobalTransform, With<Camera3d>>, player: Query<&GlobalTransform, With<Player>>) {
    let Some(m) = m.as_deref_mut() else { return };
    if let Ok(r) = roman.single() {
        if r.dead && m.outcome == Outcome::Playing {
            m.dead_timer += time.delta_secs();
            if m.dead_timer > 5.0 {
                m.outcome = Outcome::Failed;
                let t = m.failure_text.clone().unwrap_or("You failed!".into());
                m.message(&t, 0.0);
                m.log.push("FAILED".into());
            }
        }
    }
    // compass objective: bearing of the target relative to the camera, and its distance
    if let (Ok(c), Ok(p)) = (cam.single(), player.single()) {
        hud.objective = m.compass_target(p.translation()).map(|t| {
            let to = (t - p.translation()).with_y(0.0);
            let f = c.forward();
            let rel = to.x.atan2(-to.z) - f.x.atan2(-f.z);
            (-rel, to.length())
        });
    }
}
