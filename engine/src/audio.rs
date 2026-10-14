//! Sounds by the game's names (audio/sounds.json from tools/remaster_export_audio.py), with the original's volume
//! model (docs/subsystems/audio.md): volume_db per sound, positional sounds get the distance curve (cubic Bezier
//! through 0, -5, -10, -40 dB from min_distance to max_distance, 0x004f4ec0) and are muted at -40 dB; pan from
//! Bevy's spatial audio. Speech sounds raise a Subtitle with their text-bank line (0x00495e00).

use bevy::audio::{PlaybackMode, Volume};
use bevy::prelude::*;
use serde::Deserialize;
use std::collections::HashMap;
use std::path::Path;

#[derive(Deserialize, Clone, Default)]
pub struct SoundDef {
    pub file: String,
    #[serde(default)]
    pub r#loop: bool,
    #[serde(default)]
    pub volume_db: f32,
    #[serde(default)]
    pub min_distance: Option<f32>,
    #[serde(default)]
    pub max_distance: Option<f32>,
    #[serde(default)]
    pub category: Option<String>,
    /// music: the segments that may follow (sounds.json)
    #[serde(default)]
    pub next: Vec<String>,
    #[serde(default)]
    pub duration: f32,
}

#[derive(Deserialize)]
struct SoundsFile {
    sounds: HashMap<String, SoundDef>,
}

#[derive(Deserialize)]
struct VoiceFile {
    lines: HashMap<String, VoiceLine>,
}

#[derive(Deserialize, Clone)]
pub struct VoiceLine {
    #[serde(default)]
    pub speaker: Option<String>,
    #[serde(default)]
    pub subtitle: Option<String>,
    /// the line's length in seconds (the wav)
    #[serde(default)]
    pub duration: f32,
}

/// A subtitle to show (speech started): speaker and line.
#[derive(Message, Clone)]
pub struct Subtitle {
    pub speaker: Option<String>,
    pub text: String,
    /// seconds to show it: the line's length
    pub duration: f32,
}

/// The distance curve in dB at t in [0, 1] between min and max distance.
pub fn distance_db(t: f32) -> f32 {
    let t = t.clamp(0.0, 1.0);
    let u = 1.0 - t;
    3.0 * u * u * t * -5.0 + 3.0 * u * t * t * -10.0 + t * t * t * -40.0
}

fn db_to_gain(db: f32) -> f32 {
    if db <= -40.0 { 0.0 } else { 10f32.powf(db / 20.0) }
}

#[derive(Resource, Default)]
pub struct SoundBank {
    pub sounds: HashMap<String, SoundDef>,
    handles: HashMap<String, Handle<AudioSource>>,
    pub played: usize,
    pub missing: std::collections::HashSet<String>,
    pub voice: HashMap<String, VoiceLine>,
    /// the last sounds started (for the playtest log)
    pub recent: Vec<String>,
}

impl SoundBank {
    pub fn load(dir: &Path) -> Self {
        let sounds: HashMap<String, SoundDef> = std::fs::read_to_string(dir.join("audio").join("sounds.json"))
            .ok()
            .and_then(|t| serde_json::from_str::<SoundsFile>(&t).ok())
            .map(|f| f.sounds.into_iter().map(|(k, v)| (k.to_lowercase(), v)).collect())
            .unwrap_or_default();
        let voice = std::fs::read_to_string(dir.join("audio").join("voice.json"))
            .ok()
            .and_then(|t| serde_json::from_str::<VoiceFile>(&t).ok())
            .map(|f| f.lines)
            .unwrap_or_default();
        info!("audio: {} sounds, {} voice lines", sounds.len(), voice.len());
        SoundBank { sounds, voice, ..default() }
    }

    /// "@grunt:<Category>:<set>": a random line of that category from the voice set gr1/gr2/gr3 (any set when the
    /// set has none), as the grunt voice methods pick them (audio_01.cpp).
    pub fn resolve(&self, name: &str, pick: u32) -> Option<String> {
        let Some(rest) = name.strip_prefix("@grunt:") else { return Some(name.to_lowercase()) };
        let mut it = rest.split(':');
        let cat = it.next()?.to_lowercase();
        let set: u8 = it.next().and_then(|s| s.parse().ok()).unwrap_or(0);
        // an optional pool: "last<N>" the category's last N lines (by number), "butlast<N>" the others (the voice
        // functions pick from fixed pools: Hurt 46..49 vs HurtShort 29..32, Dead 26..28 vs DeadAtPosition 21..25)
        let pool = it.next().map(str::to_owned);
        let suffix = format!("_{cat}");
        let of = |prefix: &str| {
            let mut v: Vec<&String> = self.sounds.keys().filter(|k| k.starts_with(prefix) && k.ends_with(&suffix)).collect();
            v.sort();
            v
        };
        let mut v = of(&format!("gr{}", set + 1));
        if v.is_empty() {
            v = of("gr");
        }
        if let Some(p) = pool.as_deref() {
            if let Some(n) = p.strip_prefix("butlast").and_then(|n| n.parse::<usize>().ok()) {
                v.truncate(v.len().saturating_sub(n).max(1));
            } else if let Some(n) = p.strip_prefix("last").and_then(|n| n.parse::<usize>().ok()) {
                let k = v.len().saturating_sub(n);
                v.drain(..k);
            }
        }
        if v.is_empty() {
            return None;
        }
        Some(v[pick as usize % v.len()].clone())
    }
}

/// Play a sound by its game name. `at` (engine space) makes it positional; `follow` keeps it on an entity.
#[derive(Message, Clone)]
pub struct PlaySound {
    pub name: String,
    pub at: Option<Vec3>,
    pub follow: Option<Entity>,
    pub looped: Option<bool>,
    pub volume: f32,
}

impl PlaySound {
    pub fn at(name: impl Into<String>, at: Vec3) -> Self {
        PlaySound { name: name.into(), at: Some(at), follow: None, looped: None, volume: 1.0 }
    }
    pub fn ui(name: impl Into<String>) -> Self {
        PlaySound { name: name.into(), at: None, follow: None, looped: None, volume: 1.0 }
    }
}

/// A playing positional sound: volume from the distance to the listener, every frame.
#[derive(Component)]
pub struct Positional {
    pub base: f32,
    pub min: f32,
    pub max: f32,
    pub follow: Option<Entity>,
    /// a loop on an object stops with it (FxObject_Remove 0x00418db0 stops the object's sound)
    pub looped: bool,
}

/// Stops every playing instance of a sound (looped sounds: the machine gun's fire loop, the ticker wind-up), as
/// the game stops a sound by its handle ((g+0x2c) virtual +0xd8).
#[derive(Message, Clone)]
pub struct StopSound(pub String);

/// The bank name of a playing sound.
#[derive(Component)]
pub struct SoundName(pub String);

/// A playing sound's bank category (music, voice, ...).
#[derive(Component)]
pub struct SoundCategory(pub String);

/// StopSound name that stops every sound but the music (the win, 0x0044a390: audio vt+0xa8(0) 0x004f24e0)
pub const ALL_BUT_MUSIC: &str = "*all_but_music";

/// Category volumes (settings menu).
#[derive(Resource)]
pub struct Mixer {
    pub master: f32,
    pub sfx: f32,
    pub voice: f32,
    pub music: f32,
}

pub struct AudioBankPlugin;

impl Plugin for AudioBankPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<PlaySound>()
            .add_message::<Subtitle>()
            .add_message::<StopSound>()
            .insert_resource(Mixer { master: 1.0, sfx: 1.0, voice: 1.0, music: 0.8 })
            .init_resource::<AudioCheck>()
            .add_systems(Update, (stop, play, attenuate).chain())
            .add_systems(Update, audio_check.run_if(resource_exists::<crate::playtest::Playtest>));
    }
}

fn play(
    mut commands: Commands,
    mut events: MessageReader<PlaySound>,
    bank: Option<ResMut<SoundBank>>,
    assets: Res<AssetServer>,
    mixer: Res<Mixer>,
    mut subs: MessageWriter<Subtitle>,
    mut pick: Local<u32>,
    time: Res<Time>,
    mut log: Local<Option<std::fs::File>>,
) {
    let Some(mut bank) = bank else { return };
    // debug: AND_SOUND_LOG=<file> writes every sound started (game seconds, name, volume_db), to compare with the
    // original's [test] log_sounds list on the same inputs (docs/remaster-engine.md)
    if log.is_none() {
        if let Ok(path) = std::env::var("AND_SOUND_LOG") {
            *log = std::fs::File::create(path).ok();
        }
    }
    for ev in events.read() {
        *pick = pick.wrapping_mul(1103515245).wrapping_add(12345);
        let Some(key) = bank.resolve(&ev.name, *pick >> 8) else { continue };
        let Some(def) = bank.sounds.get(&key).cloned() else {
            if bank.missing.insert(key.clone()) {
                info!("audio: no sound {key} in the bank");
            }
            continue;
        };
        let handle = bank.handles.entry(key.clone()).or_insert_with(|| assets.load(format!("audio/{}", def.file))).clone();
        let looped = ev.looped.unwrap_or(def.r#loop);
        let cat = match def.category.as_deref() {
            Some("voice") => mixer.voice,
            Some("music") => mixer.music,
            _ => mixer.sfx,
        };
        let base = db_to_gain(def.volume_db) * ev.volume * cat * mixer.master;
        if let Some(f) = log.as_mut() {
            use std::io::Write;
            let _ = writeln!(f, "{:.3} {key} {:.1} {}", time.elapsed_secs(), def.volume_db, if ev.at.is_some() || ev.follow.is_some() { "3d" } else { "2d" });
        }
        if def.category.as_deref() == Some("voice") {
            if let Some(line) = bank.voice.get(&key).filter(|l| l.subtitle.is_some()) {
                subs.write(Subtitle { speaker: line.speaker.clone(), text: line.subtitle.clone().unwrap_or_default(), duration: line.duration.max(2.0) });
            }
        }
        let settings = PlaybackSettings {
            mode: if looped { PlaybackMode::Loop } else { PlaybackMode::Despawn },
            volume: Volume::Linear(base),
            spatial: ev.at.is_some() || ev.follow.is_some(),
            ..default()
        };
        let mut e = commands.spawn((AudioPlayer(handle), settings, Transform::from_translation(ev.at.unwrap_or_default()), SoundName(ev.name.clone()), SoundCategory(def.category.clone().unwrap_or_default())));
        if ev.at.is_some() || ev.follow.is_some() {
            e.insert(Positional {
                base,
                min: def.min_distance.unwrap_or(10.0),
                max: def.max_distance.unwrap_or(500.0),
                follow: ev.follow,
                looped,
            });
        }
        bank.played += 1;
        bank.recent.push(key.clone());
        if bank.recent.len() > 40 {
            bank.recent.remove(0);
        }
    }
}

fn stop(mut commands: Commands, mut events: MessageReader<StopSound>, playing: Query<(Entity, &SoundName, Option<&SoundCategory>)>) {
    for ev in events.read() {
        for (e, n, c) in &playing {
            if n.0 == ev.0 || (ev.0 == ALL_BUT_MUSIC && c.is_none_or(|c| c.0 != "music")) {
                commands.entity(e).despawn();
            }
        }
    }
}

/// The game's distance curve; streamed and positional sounds are silent beyond max distance.
fn attenuate(
    mut commands: Commands,
    listener: Query<&GlobalTransform, With<SpatialListener>>,
    mut q: Query<(Entity, &Positional, &mut Transform, Option<&mut AudioSink>)>,
    followed: Query<&GlobalTransform, Without<Positional>>,
) {
    let Ok(l) = listener.single() else { return };
    for (e, p, mut t, sink) in &mut q {
        if p.looped && p.follow.is_some_and(|f| followed.get(f).is_err()) {
            commands.entity(e).despawn();
            continue;
        }
        if let Some(f) = p.follow.and_then(|e| followed.get(e).ok()) {
            t.translation = f.translation();
        }
        let d = t.translation.distance(l.translation());
        let g = if d <= p.min { 1.0 } else if d >= p.max { 0.0 } else { db_to_gain(distance_db((d - p.min) / (p.max - p.min))) };
        if let Some(mut s) = sink {
            s.set_volume(Volume::Linear(p.base * g));
        }
    }
}

/// Playback check (playtest evidence that sounds are heard, not just started): every 0.5 s each playing sound's
/// position on the output device is compared with the last sample; advancing, paused and silent sinks are counted,
/// and every sound name that advanced at least once is kept.
#[derive(Resource, Default)]
pub struct AudioCheck {
    pub advancing: usize,
    pub stalled: usize,
    pub silent: usize,
    pub heard: std::collections::BTreeSet<String>,
    pub voice_heard: usize,
    last: std::collections::HashMap<Entity, f32>,
    timer: f32,
}

pub fn audio_check(
    time: Res<Time<Real>>,
    mut chk: ResMut<AudioCheck>,
    sinks: Query<(Entity, &AudioSink, &SoundName)>,
    spatial: Query<(Entity, &bevy::audio::SpatialAudioSink, &SoundName)>,
    bank: Option<Res<SoundBank>>,
) {
    use bevy::audio::AudioSinkPlayback;
    chk.timer -= time.delta_secs();
    if chk.timer > 0.0 {
        return;
    }
    chk.timer = 0.1;
    let (mut adv, mut stalled, mut silent) = (0, 0, 0);
    let mut seen = std::collections::HashMap::new();
    let mut heard = Vec::new();
    let mut check = |e: Entity, pos: f32, paused: bool, vol: f32, name: &str, last: &std::collections::HashMap<Entity, f32>| {
        seen.insert(e, pos);
        if vol <= 1e-4 {
            silent += 1;
        }
        match last.get(&e) {
            Some(p) if pos > *p && !paused => {
                adv += 1;
                heard.push(name.to_owned());
            }
            Some(_) => stalled += 1,
            None => {}
        }
    };
    let last = std::mem::take(&mut chk.last);
    for (e, s, n) in &sinks {
        check(e, s.position().as_secs_f32(), s.is_paused(), s.volume().to_linear(), &n.0, &last);
    }
    for (e, s, n) in &spatial {
        check(e, s.position().as_secs_f32(), s.is_paused(), s.volume().to_linear(), &n.0, &last);
    }
    chk.last = seen;
    chk.advancing = adv;
    chk.stalled = stalled;
    chk.silent = silent;
    for n in heard {
        if bank.as_ref().is_some_and(|b| b.sounds.get(&n.to_lowercase()).is_some_and(|d| d.category.as_deref() == Some("voice"))) || n.starts_with('@') {
            chk.voice_heard += 1;
        }
        chk.heard.insert(n);
    }
}
