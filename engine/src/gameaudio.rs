//! Sound the way lev_1_m1 plays it, from the game's data: anim note tracks (FX <fx> / fx_material <fx> + the
//! surface material: footsteps, foley, weapon handling, run through the fx VM), ambient emitters (ambient.json:
//! a world loop in 2D, positional one-shots within max_distance after oneshot_delay as the game runs it), and the
//! dynamic music (Music_Update 0x0044f580: the state from enemy counts and timers, victory / defeat requests, the
//! segment graph walked by the stream's resolver 0x00500a00; docs/subsystems/audio.md section 4).

use crate::audio::{PlaySound, SoundBank};
use crate::fx::StartFx;
use crate::skin::AnimNote;
use bevy::audio::Volume;
use bevy::prelude::*;
use serde::Deserialize;

pub struct GameAudioPlugin;

impl Plugin for GameAudioPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Music>().add_systems(Update, (notes, ambient, (music_state, music_voice).chain()));
    }
}

/// FX <name>: the fx at the character; fx_material <name>: + "_" + the surface (lev_1_m1 ground: snow, guess:
/// the material lookup under the feet is not ported).
fn notes(mut ev: MessageReader<AnimNote>, q: Query<&GlobalTransform>, mut fx: MessageWriter<StartFx>) {
    for n in ev.read() {
        let mut it = n.text.split_whitespace();
        let (Some(kind), Some(name)) = (it.next(), it.next()) else { continue };
        let Ok(t) = q.get(n.entity) else { continue };
        let name = name.to_lowercase();
        let name = match kind.to_lowercase().as_str() {
            "fx" | "fxlocked" => name,
            "fx_material" => format!("{name}_snow"),
            _ => continue,
        };
        fx.write(StartFx { name, at: t.translation(), rotation: Quat::IDENTITY, follow: None, position_only: false });
    }
}

#[derive(Deserialize, Clone)]
struct AmbientDef {
    position: [f32; 3],
    world: bool,
    #[serde(default)]
    loops: Vec<String>,
    #[serde(default)]
    oneshots: Vec<String>,
    #[serde(default)]
    max_distance: Option<f32>,
    #[serde(default)]
    oneshot_delay: Option<(f32, f32)>,
}

#[derive(Deserialize)]
struct AmbientFile {
    ambients: Vec<AmbientDef>,
}

struct Emitter {
    def: AmbientDef,
    pos: Vec3,
    timer: f32,
    looping: bool,
}

fn ambient(
    mut started: Local<Option<Vec<Emitter>>>,
    launch: Option<Res<crate::Launch>>,
    time: Res<Time>,
    listener: Query<&GlobalTransform, With<SpatialListener>>,
    mut sounds: MessageWriter<PlaySound>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let Some(launch) = launch else { return };
    let ems = started.get_or_insert_with(|| {
        std::fs::read_to_string(launch.scene.join("audio").join("ambient.json"))
            .ok()
            .and_then(|t| serde_json::from_str::<AmbientFile>(&t).ok())
            .map(|f| {
                f.ambients
                    .into_iter()
                    .map(|d| Emitter { pos: crate::level::game_to_engine() * Vec3::from(d.position), timer: 1.0, looping: false, def: d })
                    .collect()
            })
            .unwrap_or_default()
    });
    let Ok(l) = listener.single() else { return };
    for e in ems.iter_mut() {
        let near = e.def.world || e.def.max_distance.is_none_or(|m| e.pos.distance(l.translation()) < m);
        // one random loop is kept running (world emitters in 2D)
        if !e.looping && !e.def.loops.is_empty() && near {
            let i = (rng.rand() * e.def.loops.len() as f32) as usize % e.def.loops.len();
            let mut s = if e.def.world { PlaySound::ui(e.def.loops[i].clone()) } else { PlaySound::at(e.def.loops[i].clone(), e.pos) };
            s.looped = Some(true);
            sounds.write(s);
            e.looping = true;
        }
        if e.def.oneshots.is_empty() || !near {
            continue;
        }
        e.timer -= time.delta_secs();
        if e.timer <= 0.0 {
            let (a, b) = e.def.oneshot_delay.unwrap_or((5.0, 10.0));
            e.timer = rng.range(a, b.max(a));
            let i = (rng.rand() * e.def.oneshots.len() as f32) as usize % e.def.oneshots.len();
            // positional ambients get the distance curve but play 2D, no pan (0x00500520)
            sounds.write(PlaySound::ui(e.def.oneshots[i].clone()));
        }
    }
}

/// The level's music table (0x005ce4e0 + 0x1c * (Game+0x2708 - 1), level 1's row): states 0..6, -1 = silence.
const TRACKS: [&str; 7] = ["l1amb", "stealth", "skirm2", "l1bat", "skirm1", "l1vic", "l1def"];
/// Battle / stealth re-arm time (0x005bcbb8 = 180.0).
const REARM: f32 = 180.0;
/// Music fade-out on stop: the music definitions' envelope `fade_out` (0x004fb360); offset (1 - level) * -40 dB.
const FADE_OUT: f32 = 10.0;

/// Dynamic music: the record Game+0xa88 updated by Music_Update 0x0044f580 (audio_01.cpp), plus the music voice of
/// the sound system (audio +0x90 / +0x94 / +0x98) that walks the segment graph. docs/subsystems/audio.md section 4.
#[derive(Resource)]
pub struct Music {
    /// +0 re-evaluation timer, +4 time the music has played, +8 hold timer, +0x1c state
    timer: f32,
    played: f32,
    hold: f32,
    state: i32,
    /// +0x10 victory request, +0x11 (cl of 0x0044f560), +0x12 defeat request. The play call's flag byte is
    /// !+0x11 for a victory, 1 for a defeat: it sets voice flag 0x8000000 (0x004f3b10), whose reader is not found,
    /// so it is kept but unused here.
    victory: bool,
    #[allow(dead_code)]
    no_flag: bool,
    defeat: bool,
    /// +0x14 / +0x18 enemy counts (near / far)
    pub near: u32,
    pub far: u32,
    /// Game+0xd78 / +0xd7c: game time of the last battle / stealth track (0 = never)
    last_battle: f32,
    last_stealth: f32,
    /// the music voice: the segment playing and its time left, the queued track (audio +0x94), the stop mark
    /// (voice +0x64 bit 0x800), a fade-out in progress (seconds into it), a fresh start pending
    segment: Option<String>,
    left: f32,
    queue: Option<String>,
    stopping: bool,
    fade: Option<f32>,
    start: Option<String>,
    /// edges: the mission won, Roman dead
    won: bool,
    dead: bool,
}

impl Default for Music {
    fn default() -> Self {
        Music {
            timer: 0.0,
            played: 0.0,
            hold: 1.0,
            state: -1,
            victory: false,
            no_flag: false,
            defeat: false,
            near: 0,
            far: 0,
            last_battle: 0.0,
            last_stealth: 0.0,
            segment: None,
            left: 0.0,
            queue: None,
            stopping: false,
            fade: None,
            start: None,
            won: false,
            dead: false,
        }
    }
}

impl Music {
    /// 0x0044f560 (eax = Game+0xa88, cl): victory request, hold cleared. cl = 1 from an objective marked with its
    /// text (ui_ear 0x0046afa0: the "objective jingle"), cl = 0 from the mission's success (0x0044a390 state 2).
    pub fn request_victory(&mut self, no_flag: bool) {
        self.victory = true;
        self.no_flag = no_flag;
        self.hold = 0.0;
    }
    /// 0x0044f570: defeat request, hold cleared (the player's death vt+0xd4 0x0047ae40; mission failure
    /// 0x0044a390 states 3..8).
    pub fn request_defeat(&mut self) {
        self.defeat = true;
        self.hold = 0.0;
    }
    /// Audio +0xdc on the music handle +0x98.
    fn playing(&self) -> bool {
        self.segment.is_some() || self.start.is_some()
    }
    /// Audio +0xd4 (0x004f3b10): while the music plays the track is queued (+0x94) for a segment boundary, else it
    /// starts now with its first segment (the definition named like the track).
    fn play(&mut self, name: &str) {
        if self.segment.is_some() {
            self.queue = Some(name.to_string());
        } else {
            self.start = Some(name.to_string());
        }
    }
    /// Audio +0xd8 with immediate = 1 (0x004f30e0 -> 0x004f26a0): the music voice gets +0x64 |= 0x800, the queue
    /// is cleared and the voice fades out over the envelope's 10 s.
    fn stop(&mut self) {
        self.start = None;
        if self.segment.is_some() {
            self.stopping = true;
            self.queue = None;
            self.fade.get_or_insert(0.0);
        }
    }
    /// 0x0044f4c0 (level start / restart): state -1, timers 0, hold 1 s, requests cleared, Game+0xd78 / +0xd7c = 0,
    /// and the music stopped.
    fn reset(&mut self) {
        self.state = -1;
        self.timer = 0.0;
        self.played = 0.0;
        self.hold = 1.0;
        self.near = 0;
        self.far = 0;
        self.victory = false;
        self.defeat = false;
        self.last_battle = 0.0;
        self.last_stealth = 0.0;
        self.won = false;
        self.dead = false;
        self.stop();
    }
}

/// The name cut at its last '_' (0x005009b0): l1bat_044 -> l1bat, tran3 -> tran3.
fn base(name: &str) -> &str {
    name.rfind('_').map(|i| &name[..i]).unwrap_or(name)
}

/// Audio_ResolveSound 0x00500a00 at the end of segment `ended`: the segment that follows, or None (the music ends).
/// With a queued track: the first `next` entry whose base is the track, else the first entry whose own `next` has
/// one, else the first entry. The queue is cleared only when a segment of the queued track itself ends, and that
/// segment is then chosen again (0x00500c99..0x00500ce7). A queued track equal to the segment that ended clears
/// the queue and ends the music (0x00500ae6). Without a queue: the first entry ("none" ends), or the second (the
/// `*_loopend`) when the voice was stopped (+0x64 0x800) and the segment has flag 0x8000000 (0x00500c55).
fn resolve(bank: &SoundBank, ended: &str, queue: &mut Option<String>, stopping: bool) -> Option<String> {
    let def = bank.sounds.get(ended)?;
    if def.next.is_empty() {
        return None;
    }
    let mut chosen: Option<String> = None;
    let mut was_current = false;
    if queue.as_deref() == Some(ended) {
        *queue = None;
        was_current = true;
    }
    if let Some(q) = queue.clone() {
        let mut found: Option<&String> = None;
        let mut matched = false;
        for v in &def.next {
            if base(v) == q {
                if bank.sounds.contains_key(v) {
                    chosen = Some(v.clone());
                    matched = true;
                    break;
                }
            } else if found.is_none() && bank.sounds.get(v).is_some_and(|o| o.next.iter().any(|w| base(w) == q)) {
                found = Some(v);
            }
        }
        if !matched {
            chosen = found.or(def.next.first()).filter(|v| bank.sounds.contains_key(*v)).cloned();
        }
    } else if !was_current {
        if def.next[0].starts_with("none") {
            return None;
        }
        let mut v = &def.next[0];
        if stopping && def.flag_names.iter().any(|f| f == "music_last_segment") {
            v = def.next.get(1)?;
        }
        chosen = Some(v.clone()).filter(|v| bank.sounds.contains_key(v));
    }
    if chosen.is_some() {
        if let Some(q) = queue.as_deref() {
            if base(q) == base(ended) {
                *queue = None;
                chosen = Some(ended.to_string());
            }
        }
    }
    chosen
}

/// Music_Update 0x0044f580, once per game frame while playing (it returns while paused, Game+0x2684). Level 1's
/// values (p_globalsettings, LevelProperties_Load 0x00424530; code defaults in brackets): music_nearby_suspense 2
/// (+0xd6c), music_agro_battle 4 (+0xd70), music_nearby_range 100 (+0xd80), music_agro_range 500000 (+0xd74),
/// music_play_time 600 (+0xd84), music_play_delay_min / max 60 / 300 [30 / 60] (+0xd88 / +0xd8c).
#[allow(clippy::too_many_arguments)]
fn music_state(
    mut music: ResMut<Music>,
    time: Res<Time>,
    state: Res<State<crate::AppState>>,
    data: Option<Res<crate::gamedata::GameData>>,
    mission: Option<Res<crate::mission::Mission>>,
    grunts: Query<(&crate::ai::Grunt, &GlobalTransform)>,
    player: Query<&GlobalTransform, With<crate::ai::Player>>,
    roman: Query<&crate::player::Roman>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let m: &mut Music = &mut music;
    let title = *state.get() == crate::AppState::Title;
    if title || mission.as_ref().is_some_and(|mi| mi.is_added()) {
        if m.state != -1 || m.timer != 0.0 || m.played != 0.0 || m.hold != 1.0 || m.won || m.dead || m.start.is_some() || (m.segment.is_some() && m.fade.is_none()) {
            m.reset();
        }
        if title {
            return;
        }
    }
    if *state.get() != crate::AppState::Playing {
        return;
    }
    // the requests from the mission's success (0x0044a390 state 2: 0x0044f560 cl = 0) and the player's death
    // (vt+0xd4 0x0047ae40: 0x0044f570)
    if mission.as_ref().is_some_and(|mi| mi.outcome == crate::mission::Outcome::Won) && !m.won {
        m.won = true;
        m.request_victory(false);
    }
    if let Ok(r) = roman.single() {
        if r.dead && !m.dead {
            m.request_defeat();
        }
        m.dead = r.dead;
    }
    let p = data.as_ref().map(|d| d.props("p_globalsettings")).unwrap_or_default();
    let far_limit = p.i("music_nearby_suspense", 2) as u32;
    let near_limit = p.i("music_agro_battle", 4) as u32;
    let far_range = p.f("music_nearby_range", 100.0);
    let agro_range = p.f("music_agro_range", 500000.0);
    let play_time = p.f("music_play_time", 600.0);
    let delay_min = p.f("music_play_delay_min", 30.0);
    let delay_max = p.f("music_play_delay_max", 60.0);

    let dt = time.delta_secs();
    let playing = m.playing();
    if m.state == -1 && playing {
        return;
    }
    m.timer -= dt;
    if playing {
        m.played += dt;
    } else {
        m.played = 0.0;
    }
    if m.hold > 0.0 {
        m.hold -= dt;
        return;
    }
    // a track still queued for the next segment boundary: wait 10 s more
    if playing && m.queue.is_some() {
        m.hold = 10.0;
        return;
    }
    let mut next = -1;
    if m.played >= play_time && play_time > 0.0 {
        m.timer = 1.0;
    } else {
        next = m.state;
    }
    let mut decided = false;
    // victory / defeat: only while music plays and not already in them (and not in base defend, 0x0044e600, which
    // level 1 never sets)
    if playing && m.state != 5 && m.state != 6 {
        if m.defeat {
            next = 6;
            m.timer = 1.0;
            decided = true;
        } else if m.victory {
            next = 5;
            m.timer = 1.0;
            decided = true;
        }
    }
    if !decided && m.timer <= 0.0 {
        m.timer = rng.range(0.5, 1.0);
        let Ok(pp) = player.single() else { return };
        let pp = pp.translation();
        let (mut near, mut far) = (0u32, 0u32);
        // World+0xe7c (the grunts, vtables 0x5a9cf0 / 0x5abb88): vt+0xe4 false, vt+0xe8 true, alive (0x00454780);
        // vt+0x10c true (AI): near only with a target (+0x588, 0x0042c6f0) within music_agro_range; any within
        // music_nearby_range counts as far
        for (g, t) in &grunts {
            if g.mind == crate::ai::Mind::Dead {
                continue;
            }
            let d = t.translation().distance_squared(pp);
            if g.target.is_some() && d <= agro_range * agro_range {
                near += 1;
            }
            if d <= far_range * far_range {
                far += 1;
            }
        }
        m.near = near;
        m.far = far;
        let now = time.elapsed_secs();
        let many_near = near as f64 >= near_limit as f64 * 0.5;
        // 0x0044fa8f: skirm2 when the last battle track is 180 s old (re-armed), else lNbat (armed when unset)
        let battle = |m: &mut Music| {
            if m.last_battle > 0.0 && now - REARM >= m.last_battle {
                m.last_battle = now;
                return 2;
            }
            if m.last_battle == 0.0 {
                m.last_battle = now;
            }
            3
        };
        // 0x0044fa10: stealth when the last stealth track is 180 s old (re-armed); else silence when nobody is near
        let stealth_or_silence = |m: &mut Music, cur: i32| {
            if m.last_stealth > 0.0 && now - REARM >= m.last_stealth {
                m.last_stealth = now;
                return 1;
            }
            if far == 0 && near == 0 {
                return -1;
            }
            cur
        };
        next = match m.state {
            0 => {
                if near >= near_limit {
                    battle(&mut *m)
                } else if far >= far_limit && now - REARM >= m.last_stealth {
                    m.last_stealth = now;
                    1
                } else {
                    next
                }
            }
            1 => {
                if near >= near_limit {
                    battle(&mut *m)
                } else if far == 0 {
                    -1
                } else {
                    next
                }
            }
            2..=4 => {
                if many_near {
                    if now - REARM >= m.last_battle {
                        m.last_battle = now;
                        if m.state == 3 { 4 } else { 3 }
                    } else {
                        next
                    }
                } else {
                    stealth_or_silence(&mut *m, next)
                }
            }
            5 | 6 => {
                if !playing {
                    m.victory = false;
                    m.defeat = false;
                    m.hold = 5.0;
                    -1
                } else {
                    next
                }
            }
            -1 => {
                if near >= near_limit {
                    battle(&mut *m)
                } else if far >= far_limit {
                    if m.last_stealth > 0.0 && now - REARM >= m.last_stealth {
                        m.last_stealth = now;
                        1
                    } else {
                        if m.last_stealth == 0.0 {
                            m.last_stealth = now;
                        }
                        0
                    }
                } else {
                    // hold <= 0 here
                    0
                }
            }
            _ => next,
        };
    }
    if next == m.state {
        return;
    }
    info!("music: state {} -> {} ({}) at {:.1} s, near {} far {}", m.state, next, TRACKS.get(next as usize).unwrap_or(&"silence"), time.elapsed_secs(), m.near, m.far);
    m.state = next;
    if next == -1 {
        // silence for the time played, clamped to music_play_delay_min..max (played is 0 after a jingle ended)
        m.hold = m.played;
        if m.played < delay_min {
            m.hold = delay_min;
        } else if m.played > delay_max {
            m.hold = delay_max;
        }
        m.stop();
        return;
    }
    m.hold = 10.0;
    m.play(TRACKS[next as usize]);
}

/// The music voice (the driver's stream, 0x00500890 / 0x00500a00): plays the segments one after the other,
/// resolving the next one at each boundary; a stop fades out over 10 s. It runs while paused too (real time).
fn music_voice(
    mut music: ResMut<Music>,
    time: Res<Time<Real>>,
    bank: Option<Res<SoundBank>>,
    mut sounds: MessageWriter<PlaySound>,
    mut stop: MessageWriter<crate::audio::StopSound>,
    mut sinks: Query<(&crate::audio::SoundCategory, &PlaybackSettings, &mut AudioSink)>,
) {
    let Some(bank) = bank else { return };
    let m: &mut Music = &mut music;
    let dt = time.delta_secs();
    if let Some(name) = m.start.take() {
        if let Some(seg) = m.segment.take() {
            stop.write(crate::audio::StopSound(seg));
        }
        m.queue = None;
        m.stopping = false;
        m.fade = None;
        m.left = bank.sounds.get(&name).map(|d| d.duration).unwrap_or(4.0);
        sounds.write(PlaySound::ui(name.clone()));
        m.segment = Some(name);
        return;
    }
    let Some(cur) = m.segment.clone() else { return };
    if let Some(f) = m.fade.as_mut() {
        *f += dt;
        let t = *f;
        if t >= FADE_OUT {
            stop.write(crate::audio::StopSound(cur));
            m.segment = None;
            m.fade = None;
            m.queue = None;
            return;
        }
        let gain = 10f32.powf(-40.0 * (t / FADE_OUT) / 20.0);
        for (c, s, mut sink) in &mut sinks {
            if c.0 == "music" {
                sink.set_volume(Volume::Linear(s.volume.to_linear() * gain));
            }
        }
    }
    m.left -= dt;
    if m.left > 0.0 {
        return;
    }
    let mut queue = m.queue.take();
    let next = resolve(&bank, &cur, &mut queue, m.stopping);
    m.queue = queue;
    let Some(seg) = next else {
        m.segment = None;
        m.fade = None;
        m.queue = None;
        return;
    };
    m.left = bank.sounds.get(&seg).map(|d| d.duration).unwrap_or(4.0);
    sounds.write(PlaySound::ui(seg.clone()));
    m.segment = Some(seg);
}
