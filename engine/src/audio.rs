//! The sound system: the original's audio object (Game+0x2c; src/audio/audio_02.cpp and audio_03.cpp, its
//! DirectSound driver in src/renderer/auto_004fb170.cpp / auto_004fb1f0.cpp; docs/subsystems/audio.md) on Bevy audio.
//! A self-contained module: a game hands it a bank of definitions and sends it messages.
//!
//! - `SoundBank`: the definitions by name (`audio/sounds.json` and `audio/voice.json` of an export).
//! - `PlaySound`: start a sound 2D, at a point or on an entity; a `SoundId` addresses it later.
//! - `StopSound` (every voice of a name), `StopVoice` (one id), `StopSoundsOn` (an entity's), `ALL_BUT_MUSIC`.
//! - `SetVoiceOffset`: a voice's extra dB (the original's per-voice offset, slot +0xf8).
//! - `Mixer`: the options' volume per group (sfx / speech / music) and a master mute.
//! - `ListenerBody`: the listener's own body (the player): positional sounds on it play 2D.
//! - `Subtitle`: raised when a speech sound starts (the driver's start callback, 0x00495e00).
//!
//! What a definition says (data, not code): its dB, the distance curve between min and max distance, a pitch
//! variation, a priority on an exclusive channel, an intro / loop / outro sequence, a follow-on sound, an envelope
//! (the fade-out on stop) and whether it is streamed. The volume of a voice is recomputed every frame, as the
//! original refreshes every voice when a level changes (0x004f3000): group + definition + offset + envelope +
//! distance, in dB, silent at or below -40 dB (0x004fb170, 0x004fa1d0).

use crate::levelreset::LevelAppExt;
use bevy::audio::{AudioSinkPlayback, PlaybackMode, SpatialAudioSink, SpatialScale, Volume};
use bevy::prelude::*;
use serde::Deserialize;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::Path;

/// The driver mutes a voice at or below -40 dB (K 0x005bcb64; SetVolume(DSBVOLUME_MIN), 0x004fa1d0).
pub const SILENT_DB: f32 = -40.0;
/// Far below silence: a group switched off.
const MUTED_DB: f32 = -1000.0;
/// Exclusive channels: definition flag bits 11..15, the handles at audio +0x38 + 4i (0x004f35e0).
pub const CHANNELS: usize = 5;
/// The option value the remaster plays at 0 dB. The original's setters give the default 0.8 -3.2 dB; the remaster
/// keeps its default loudness and the original's curve around it (a value of 0 is still silent).
pub const OPTION_DEFAULT: f32 = 0.8;
/// Bevy's spatial sink also scales a voice by 1 / (distance x scale)^2; at this scale that factor stays 1 within
/// 10000 units, so only its pan is used: the distance volume is the original's curve.
const SPATIAL_SCALE: f32 = 1.0e-4;
/// Definition defaults (0x004f4fe0): min / max distance 20 / 500, priority 15.
const DEFAULT_RANGE: (f32, f32) = (20.0, 500.0);

#[derive(Deserialize, Clone, Copy, Default, Debug, PartialEq)]
pub struct Envelope {
    #[serde(default)]
    pub delay: f32,
    #[serde(default)]
    pub attack: f32,
    #[serde(default)]
    pub hold: f32,
    #[serde(default)]
    pub release: f32,
    #[serde(default)]
    pub fade_out: f32,
    #[serde(default)]
    pub level: f32,
}

impl Envelope {
    /// The level (0..1) `t` seconds into the voice (0x004f2730): 0 until `delay`, up over `attack`, 1 for `hold`,
    /// then over `release` the original's 1 - (1 - (c - t) / release) x level, and `level` after.
    pub fn level_at(&self, t: f32) -> f32 {
        if t <= self.delay {
            return 0.0;
        }
        let a = self.delay + self.attack;
        if t < a {
            return 1.0 - (a - t) / self.attack;
        }
        let h = a + self.hold;
        if h > t {
            return 1.0;
        }
        let c = h + self.release;
        if t < c {
            return 1.0 - (1.0 - (c - t) / self.release) * self.level;
        }
        self.level
    }

    /// The level `t` seconds into a stop's fade-out (0x004fb360 sets +0x8c = fade_out, 0x004f2730 counts it down:
    /// remaining / fade_out x level); None once it is over (the voice stops).
    pub fn fade_level(&self, t: f32) -> Option<f32> {
        let rest = self.fade_out - t;
        (rest > 0.0).then(|| rest / self.fade_out * self.level)
    }

    /// A level as a dB offset: (1 - level) x -40 (voice +0x90).
    pub fn level_db(level: f32) -> f32 {
        (1.0 - level) * SILENT_DB
    }
}

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
    /// definition flags by name (sounds.json; docs/subsystems/audio.md section 2)
    #[serde(default)]
    pub flag_names: Vec<String>,
    /// +0x24: a busy exclusive channel keeps its voice when that one's priority is at least this (0x004f35e0)
    #[serde(default = "default_priority")]
    pub priority: u32,
    /// the lowest of flag bits 11..15
    #[serde(default)]
    pub exclusive_channel: Option<usize>,
    /// +0x2c: the rate is uniform in [(1 - v / 8), (1 + v / 4)] x the file's (0x004f3a80)
    #[serde(default)]
    pub pitch_variation: f32,
    #[serde(default = "default_channels")]
    pub channels: u32,
    /// +0x7c / +0x9c (flag intro_outro_loop): played once before the loop body / on a stop that is not immediate
    #[serde(default)]
    pub intro: Option<String>,
    #[serde(default)]
    pub outro: Option<String>,
    /// +0xbc (flag follow_on): played on the same place when this one ends (0x004f2a80)
    #[serde(default)]
    pub follow_on: Option<String>,
    /// +0x38..+0x4c (flag envelope)
    #[serde(default)]
    pub envelope: Option<Envelope>,
}

fn default_priority() -> u32 {
    15
}

fn default_channels() -> u32 {
    1
}

impl SoundDef {
    pub fn has(&self, flag: &str) -> bool {
        self.flag_names.iter().any(|f| f == flag)
    }

    /// The level group by flags (0x004fb170): music 0x2000000, speech 0x600, none 0x40000000, else sfx.
    pub fn group(&self) -> VolumeGroup {
        if self.has("music") {
            VolumeGroup::Music
        } else if self.has("speech") || self.has("speech_alt_colour") {
            VolumeGroup::Speech
        } else if self.has("no_group_level") {
            VolumeGroup::Fixed
        } else {
            VolumeGroup::Sfx
        }
    }

    /// The distance curve's range (+0x30 / +0x34).
    pub fn range(&self) -> (f32, f32) {
        (self.min_distance.unwrap_or(DEFAULT_RANGE.0), self.max_distance.unwrap_or(DEFAULT_RANGE.1))
    }

    /// Flag 0x40: streamed from GameData\streams (a streamed positional voice stops or pauses beyond max distance).
    pub fn streamed(&self) -> bool {
        self.has("streamed")
    }
}

/// A sequence piece by name: "none" or empty means there is none.
fn piece(name: &Option<String>) -> Option<&str> {
    name.as_deref().filter(|n| !n.is_empty() && !n.eq_ignore_ascii_case("none"))
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

/// The cubic Bezier through 0, -5, -10, -40 dB at t in [0, 1] (the basis 0x0057e7b0, Spline_Coefficients 0x0046d630).
pub fn bezier_db(t: f32) -> f32 {
    let t = t.clamp(0.0, 1.0);
    let u = 1.0 - t;
    3.0 * u * u * t * -5.0 + 3.0 * u * t * t * -10.0 + t * t * t * SILENT_DB
}

/// The level curve (0x004f4ec0, slot +0xac): 0 dB at or below `lo`, -40 dB at or beyond `hi`, the Bezier between.
/// Used for distances (min / max distance) and for the option volumes (1 - value over 0..1).
pub fn level_curve(x: f32, lo: f32, hi: f32) -> f32 {
    if x <= lo {
        0.0
    } else if x >= hi {
        SILENT_DB
    } else {
        bezier_db((x - lo) / (hi - lo))
    }
}

pub fn db_to_gain(db: f32) -> f32 {
    if db <= SILENT_DB { 0.0 } else { 10f32.powf(db / 20.0) }
}

/// The playback rate factor for a definition's pitch variation and a uniform draw `u` in [0, 1) (0x004f3a80:
/// K 0x005bcbe4 = 0.25 up, K 0x005bccdc = 0.125 down).
pub fn rate_factor(variation: f32, u: f32) -> f32 {
    if variation <= 0.0 {
        return 1.0;
    }
    let (lo, hi) = (1.0 - variation * 0.125, 1.0 + variation * 0.25);
    lo + (hi - lo) * u
}

#[derive(Resource, Default)]
pub struct SoundBank {
    pub sounds: HashMap<String, SoundDef>,
    handles: HashMap<String, Handle<AudioSource>>,
    pub played: usize,
    pub missing: HashSet<String>,
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

    fn handle(&mut self, key: &str, assets: &AssetServer) -> Option<Handle<AudioSource>> {
        let file = self.sounds.get(key)?.file.clone();
        Some(self.handles.entry(key.to_owned()).or_insert_with(|| assets.load(format!("audio/{file}"))).clone())
    }

    /// The bank name a request plays (the definition lookup by name, 0x004f2470, is the map's). "@grunt:<Category>:<set>[:<pool>]": a line of that category from the voice set
    /// gr1/gr2/gr3 (any set when the set has none), as the grunt voice methods pick them (audio_01.cpp: one of a fixed
    /// list, (rand() x range) >> 15); `pick` chooses among them.
    pub fn resolve(&self, name: &str, pick: u32) -> Option<String> {
        let Some(rest) = name.strip_prefix("@grunt:") else { return Some(name.to_lowercase()) };
        let mut it = rest.split(':');
        let cat = it.next()?.to_lowercase();
        let set: u8 = it.next().and_then(|s| s.parse().ok()).unwrap_or(0);
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
        let v = pool(v, it.next());
        if v.is_empty() {
            return None;
        }
        Some(v[pick as usize % v.len()].clone())
    }
}

/// A category's lines narrowed to a pool: "last<N>" the last N lines (by number), "butlast<N>" the others (at least
/// one). The voice functions pick from fixed lists, e.g. Hurt 46..49 vs HurtShort 29..32, Dead 26..28 vs
/// DeadAtPosition 21..25 (audio_01.cpp).
fn pool<'a>(mut v: Vec<&'a String>, pool: Option<&str>) -> Vec<&'a String> {
    let Some(p) = pool else { return v };
    if let Some(n) = p.strip_prefix("butlast").and_then(|n| n.parse::<usize>().ok()) {
        v.truncate(v.len().saturating_sub(n).max(1));
    } else if let Some(n) = p.strip_prefix("last").and_then(|n| n.parse::<usize>().ok()) {
        let k = v.len().saturating_sub(n);
        v.drain(..k);
    }
    v
}

/// A level group (the option volumes; 0x004fb170).
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum VolumeGroup {
    #[default]
    Sfx,
    Speech,
    Music,
    /// 0 dB whatever the options (menu sounds: UiSound_PlayRandom 0x00491760 offsets them by -sfx level)
    Fixed,
}

impl VolumeGroup {
    pub fn name(self) -> &'static str {
        match self {
            VolumeGroup::Sfx => "sfx",
            VolumeGroup::Speech => "speech",
            VolumeGroup::Music => "music",
            VolumeGroup::Fixed => "fixed",
        }
    }
}

/// A handle to one playing sound, chosen by the caller (`SoundId::new()`).
#[derive(Component, Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct SoundId(u64);

impl SoundId {
    pub fn new() -> Self {
        static NEXT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);
        SoundId(NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed))
    }
}

impl Default for SoundId {
    fn default() -> Self {
        SoundId::new()
    }
}

/// Which part of a definition a request plays.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum Part {
    /// the definition as a sound: an intro / loop / outro sequence starts with its intro
    #[default]
    Whole,
    /// the loop body after its intro
    Body,
    /// exactly this definition (an outro, a follow-on), on no exclusive channel
    Piece,
}

/// Play a sound by its bank name. `at` (engine space) makes it positional; `follow` keeps it on an entity.
#[derive(Message, Clone)]
pub struct PlaySound {
    pub name: String,
    pub at: Option<Vec3>,
    pub follow: Option<Entity>,
    /// None: the definition's default (sounds.json `loop`)
    pub looped: Option<bool>,
    /// a linear factor on top of the dB levels
    pub volume: f32,
    /// the voice's extra dB from the start (SetVoiceOffset changes it later)
    pub offset_db: f32,
    /// play at this group's level instead of the definition's
    pub group: Option<VolumeGroup>,
    pub id: Option<SoundId>,
    /// keeps playing while the game is paused (music voice flag 0x8000000, which the pause 0x004f2d60 skips)
    pub through_pause: bool,
    /// start this far into the envelope's stop fade (a music segment after one that was fading)
    pub fade_elapsed: Option<f32>,
    pub part: Part,
}

impl Default for PlaySound {
    fn default() -> Self {
        PlaySound { name: String::new(), at: None, follow: None, looped: None, volume: 1.0, offset_db: 0.0, group: None, id: None, through_pause: false, fade_elapsed: None, part: Part::Whole }
    }
}

impl PlaySound {
    /// At a fixed point (+0xcc 0x004f39a0 -> +0x5c 0x004f3740).
    pub fn at(name: impl Into<String>, at: Vec3) -> Self {
        PlaySound { name: name.into(), at: Some(at), ..default() }
    }
    /// 2D (+0xd0 0x004f4030 -> +0x60 0x004f3c60).
    pub fn ui(name: impl Into<String>) -> Self {
        PlaySound { name: name.into(), ..default() }
    }
    /// On an entity, following it (+0xc4 0x004f3670 -> +0x58 0x004f32f0).
    pub fn on(name: impl Into<String>, entity: Entity) -> Self {
        PlaySound { name: name.into(), follow: Some(entity), ..default() }
    }
    /// A menu sound: 2D at 0 dB whatever the sfx volume (UiSound_PlayRandom 0x00491760 with group 0, as the menus'
    /// select / select set / cancel / move 0x00491830 / 0x00491850 / 0x00491870 / 0x00491890 call it).
    pub fn menu(name: impl Into<String>) -> Self {
        PlaySound { name: name.into(), group: Some(VolumeGroup::Fixed), ..default() }
    }
    pub fn looping(mut self, looped: bool) -> Self {
        self.looped = Some(looped);
        self
    }
    pub fn with_id(mut self, id: SoundId) -> Self {
        self.id = Some(id);
        self
    }
}

/// Stops every playing voice of a sound name, not immediately (0x004f30e0 with immediate 0: a looping sequence
/// plays its outro, an envelope fades out). `ALL_BUT_MUSIC` stops every voice but the music, immediately.
#[derive(Message, Clone)]
pub struct StopSound(pub String);

/// StopSound name that stops every sound but the music (the win, 0x0044a390: audio vt+0xa8(0) 0x004f24e0)
pub const ALL_BUT_MUSIC: &str = "*all_but_music";

/// Stops one voice (slot +0xd8 by handle); `immediate` skips the outro and the fade-out.
#[derive(Message, Clone, Copy)]
pub struct StopVoice {
    pub id: SoundId,
    pub immediate: bool,
}

/// Stops every voice on an entity, not immediately (0x004f3170, slot +0x120: an object removed).
#[derive(Message, Clone, Copy)]
pub struct StopSoundsOn(pub Entity);

/// Sets a voice's extra dB (slot +0xf8, 0x004f4540: voice +0x14).
#[derive(Message, Clone, Copy)]
pub struct SetVoiceOffset {
    pub id: SoundId,
    pub db: f32,
}

/// The bank name of a playing sound (as requested).
#[derive(Component)]
pub struct SoundName(pub String);

/// The listener's own body (the player): positional sounds on it, or starting within `radius` of it, play 2D. The
/// original plays 2D what is on the player (0x004f32a0 through its callback +0x64; Fx_PlaySound 0x00420080 for fx
/// opcode 0x3a with 0x0045a480). The remaster's callers give positions, so the radius stands in for "on it"; every
/// lev_1_m1 sound's min_distance is at least 10, so within the radius only the pan differs.
#[derive(Component, Clone, Copy)]
pub struct ListenerBody {
    pub radius: f32,
}

/// The options' volumes per group (menu values 0..1, Game+0x490 / +0x494 / +0x498) and a master factor (the
/// remaster's mute for background test runs).
#[derive(Resource, Clone)]
pub struct Mixer {
    pub master: f32,
    pub sfx: f32,
    pub voice: f32,
    pub music: f32,
}

impl Default for Mixer {
    fn default() -> Self {
        Mixer { master: 1.0, sfx: OPTION_DEFAULT, voice: OPTION_DEFAULT, music: OPTION_DEFAULT }
    }
}

impl Mixer {
    /// A group's level in dB (the getters 0x004f4340 / 0x004f4320 / 0x004f4330). The setters 0x004f42e0 (sfx +0x18), 0x004f4260 (speech +0x20) and 0x004f42a0 (music
    /// +0x1c) store level_curve(1 - value, 0, 1); the remaster's 0 dB is the default value's level.
    pub fn group_db(&self, group: VolumeGroup) -> f32 {
        let v = match group {
            VolumeGroup::Sfx => self.sfx,
            VolumeGroup::Speech => self.voice,
            VolumeGroup::Music => self.music,
            VolumeGroup::Fixed => return 0.0,
        };
        let db = level_curve(1.0 - v, 0.0, 1.0);
        if db <= SILENT_DB {
            return MUTED_DB;
        }
        db - level_curve(1.0 - OPTION_DEFAULT, 0.0, 1.0)
    }
}

/// The game's pause as the sound system sees it (Game_SetPaused 0x004031e0 pauses every voice, audio +0xe0 with
/// all = 1, and resumes them, +0xe4; voices started during the pause play).
#[derive(Resource, Default)]
pub struct GamePause {
    pub paused: bool,
    /// counts pauses, so a voice knows whether it started during the current one
    pub epoch: u32,
}

/// The exclusive channels' voices and their priorities (audio +0x38 + 4i).
#[derive(Resource, Default)]
pub struct Channels(pub [Option<(Entity, u32)>; CHANNELS]);

/// Counters for tests and the playtest log.
#[derive(Resource, Default)]
pub struct AudioStats {
    /// voices started, by bank name
    pub started: BTreeMap<String, u32>,
    /// voices started, by level group
    pub started_by_group: BTreeMap<String, u32>,
    /// requests not played, by bank name ("busy", "stereo_3d", "out_of_range")
    pub refused: BTreeMap<String, BTreeMap<String, u32>>,
    /// the text of the last subtitle raised
    pub last_subtitle: String,
}

impl AudioStats {
    fn refuse(&mut self, reason: &str, key: &str) {
        *self.refused.entry(reason.to_owned()).or_default().entry(key.to_owned()).or_default() += 1;
    }
}

/// A playing voice (the original's 0x98-byte voice record).
#[derive(Component)]
pub struct Voice {
    /// the bank definition playing (an intro, body or outro piece of a sequence included)
    pub key: String,
    pub group: VolumeGroup,
    /// definition volume_db (+0x10), extra offset (+0x14), the request's linear factor
    pub def_db: f32,
    pub offset_db: f32,
    pub factor: f32,
    pub envelope: Option<Envelope>,
    /// seconds played (paused time not counted); seconds into the stop's fade-out
    pub age: f32,
    pub fading: Option<f32>,
    /// positional: the curve's range; the place (followed entity or fixed point)
    pub range: Option<(f32, f32)>,
    pub follow: Option<Entity>,
    pub at: Vec3,
    pub looped: bool,
    pub streamed: bool,
    pub through_pause: bool,
    /// the pause it started in (a voice started during a pause is not paused by it)
    started_in_pause: Option<u32>,
    /// the loop body still to come after this intro
    then_body: Option<PlaySound>,
    /// the outro a non-immediate stop plays
    outro: Option<String>,
    follow_on: Option<String>,
    request: PlaySound,
    /// last values, for the log
    pub distance: f32,
    pub gain_db: f32,
    pub paused: bool,
    /// seconds without a sink (the file still loading; one that never loads is dropped)
    waiting: f32,
}

/// A voice whose file has not loaded after this long is dropped (it would hold its channel for ever).
const LOAD_TIMEOUT: f32 = 5.0;

impl Voice {
    /// The voice's level in dB without the master factor (0x004fb170): group, definition, offset, envelope and, for
    /// positional voices, the distance curve.
    pub fn level_db(&self, mixer: &Mixer) -> f32 {
        let env = match (self.fading, self.envelope) {
            (Some(t), Some(e)) => e.fade_level(t).map_or(MUTED_DB, Envelope::level_db),
            (None, Some(e)) => Envelope::level_db(e.level_at(self.age.max(1.0e-6))),
            _ => 0.0,
        };
        let dist = self.range.map_or(0.0, |(lo, hi)| level_curve(self.distance, lo, hi));
        mixer.group_db(self.group) + self.def_db + self.offset_db + env + dist
    }

    fn gain(&self, mixer: &Mixer) -> f32 {
        db_to_gain(self.level_db(mixer)) * self.factor * mixer.master
    }
}

pub struct AudioBankPlugin;

impl Plugin for AudioBankPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<PlaySound>()
            .add_message::<Subtitle>()
            .add_message::<StopSound>()
            .add_message::<StopVoice>()
            .add_message::<StopSoundsOn>()
            .add_message::<SetVoiceOffset>()
            .init_resource::<Mixer>()
            .init_level_resource::<GamePause>()
            .init_level_resource::<Channels>()
            .init_resource::<AudioStats>()
            .init_resource::<AudioCheck>()
            .add_systems(Update, (track_pause, stop, play, update_voices).chain())
            .add_systems(Update, audio_check.run_if(resource_exists::<crate::playtest::Playtest>));
    }
}

fn track_pause(vtime: Res<Time<Virtual>>, mut pause: ResMut<GamePause>) {
    let p = vtime.is_paused();
    if p != pause.paused {
        pause.paused = p;
        if p {
            pause.epoch += 1;
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn play(
    mut commands: Commands,
    mut events: MessageReader<PlaySound>,
    bank: Option<ResMut<SoundBank>>,
    assets: Res<AssetServer>,
    mixer: Res<Mixer>,
    mut subs: MessageWriter<Subtitle>,
    (mut rng, time, mut log): (Local<u32>, Res<Time>, Local<Option<std::fs::File>>),
    (pause, mut channels, mut stats): (Res<GamePause>, ResMut<Channels>, ResMut<AudioStats>),
    voices: Query<(), With<Voice>>,
    listener: Query<&GlobalTransform, With<SpatialListener>>,
    bodies: Query<(Entity, &GlobalTransform, &ListenerBody)>,
    places: Query<&GlobalTransform>,
) {
    let Some(mut bank) = bank else { return };
    // debug: AND_SOUND_LOG=<file> writes every sound started (game seconds, name, volume_db, 2d / 3d), to compare
    // with the original's [test] log_sounds list on the same inputs (docs/remaster-engine.md)
    if log.is_none() {
        if let Ok(path) = std::env::var("AND_SOUND_LOG") {
            *log = std::fs::File::create(path).ok();
        }
    }
    let ear = listener.single().ok().map(|l| l.translation());
    let mut spawned: HashSet<Entity> = HashSet::new();
    for ev in events.read() {
        let mut next = || {
            *rng = rng.wrapping_mul(1103515245).wrapping_add(12345);
            *rng >> 8
        };
        let Some(key) = bank.resolve(&ev.name, next()) else { continue };
        let Some(def) = bank.sounds.get(&key).cloned() else {
            if bank.missing.insert(key.clone()) {
                info!("audio: no sound {key} in the bank");
            }
            continue;
        };
        // an exclusive channel (0x004f35e0, through the play slots +0xc4 / +0xcc / +0xd0; "plays" is slot +0xdc
        // 0x004f4380): while its voice plays with
        // a priority at least this one's, the request is dropped; otherwise that voice stops
        let channel = def.exclusive_channel.filter(|c| *c < CHANNELS && ev.part == Part::Whole);
        if let Some(ch) = channel {
            if let Some((e, pri)) = channels.0[ch] {
                let playing = voices.contains(e) || spawned.contains(&e);
                if playing && pri >= def.priority {
                    stats.refuse("busy", &key);
                    continue;
                }
                if playing {
                    commands.entity(e).despawn();
                }
                channels.0[ch] = None;
            }
        }
        // the given point first: an entity spawned this frame has no GlobalTransform yet (the voice follows it from the
        // next frame on)
        let place = ev.at.or_else(|| ev.follow.and_then(|e| places.get(e).ok()).map(|t| t.translation()));
        // the listener's own body plays 2D
        let on_body = place.is_some_and(|p| bodies.iter().any(|(e, t, b)| ev.follow == Some(e) || t.translation().distance(p) <= b.radius));
        let positional = place.is_some() && !on_body;
        let (lo, hi) = def.range();
        let distance = match (place, ear) {
            (Some(p), Some(l)) if positional => p.distance(l),
            _ => 0.0,
        };
        let looped = ev.looped.unwrap_or(def.r#loop);
        // the driver's start (0x004fa210): a stereo sound cannot play positionally; a streamed one-shot beyond its
        // max distance does not start
        if positional && def.channels >= 2 {
            stats.refuse("stereo_3d", &key);
            info!("audio: {key} is stereo, not played in 3d (0x004fa210)");
            continue;
        }
        if positional && def.streamed() && !looped && distance >= hi {
            stats.refuse("out_of_range", &key);
            continue;
        }
        // an intro / loop / outro sequence (flag 0x1000000, 0x004fa340): the intro first; looping, the body follows
        // it and a stop plays the outro; not looping, the intro (or the body without one) plays once
        let sequence = ev.part != Part::Piece && def.has("intro_outro_loop");
        let mut file = key.clone();
        let mut then_body = None;
        if sequence && ev.part == Part::Whole {
            if let Some(intro) = piece(&def.intro).map(str::to_lowercase).filter(|i| bank.sounds.contains_key(i)) {
                file = intro;
                then_body = looped.then(|| PlaySound { part: Part::Body, ..ev.clone() });
            }
        }
        let outro = (sequence && looped).then(|| piece(&def.outro).map(str::to_lowercase)).flatten().filter(|o| bank.sounds.contains_key(o));
        let Some(fdef) = bank.sounds.get(&file).cloned() else { continue };
        let Some(handle) = bank.handle(&file, &assets) else { continue };
        let loops_now = looped && then_body.is_none();
        let group = ev.group.unwrap_or(def.group());
        let id = ev.id;
        let mut voice = Voice {
            key: file.clone(),
            group,
            def_db: fdef.volume_db,
            offset_db: ev.offset_db,
            factor: ev.volume.max(0.0),
            envelope: def.envelope.filter(|_| def.has("envelope")),
            age: 0.0,
            fading: ev.fade_elapsed,
            range: positional.then_some((lo, hi)),
            follow: ev.follow,
            at: place.unwrap_or_default(),
            looped,
            streamed: def.streamed(),
            through_pause: ev.through_pause,
            started_in_pause: pause.paused.then_some(pause.epoch),
            then_body,
            outro,
            follow_on: piece(&fdef.follow_on).map(str::to_lowercase).filter(|_| fdef.has("follow_on")),
            request: ev.clone(),
            distance,
            gain_db: 0.0,
            paused: false,
            waiting: 0.0,
        };
        voice.gain_db = voice.level_db(&mixer);
        if let Some(f) = log.as_mut() {
            use std::io::Write;
            let _ = writeln!(f, "{:.3} {file} {:.1} {}", time.elapsed_secs(), fdef.volume_db, if positional { "3d" } else { "2d" });
        }
        // the driver's start callback for speech (0x004fa100 -> 0x00495e00): the subtitle
        if group == VolumeGroup::Speech || def.group() == VolumeGroup::Speech {
            if let Some(line) = bank.voice.get(&file).filter(|l| l.subtitle.is_some()) {
                let text = line.subtitle.clone().unwrap_or_default();
                stats.last_subtitle = text.clone();
                subs.write(Subtitle { speaker: line.speaker.clone(), text, duration: line.duration.max(2.0) });
            }
        }
        let u = (next() & 0xffff) as f32 / 65536.0;
        let settings = PlaybackSettings {
            mode: if loops_now { PlaybackMode::Loop } else { PlaybackMode::Once },
            volume: Volume::Linear(voice.gain(&mixer)),
            speed: if group == VolumeGroup::Music { 1.0 } else { rate_factor(fdef.pitch_variation, u) },
            spatial: positional,
            spatial_scale: Some(SpatialScale::new(SPATIAL_SCALE)),
            ..default()
        };
        let mut e = commands.spawn((AudioPlayer(handle), settings, Transform::from_translation(voice.at), SoundName(ev.name.clone()), voice));
        if let Some(id) = id {
            e.insert(id);
        }
        let e = e.id();
        spawned.insert(e);
        if let Some(ch) = channel {
            channels.0[ch] = Some((e, def.priority));
        }
        *stats.started.entry(file.clone()).or_default() += 1;
        *stats.started_by_group.entry(group.name().to_owned()).or_default() += 1;
        bank.played += 1;
        bank.recent.push(file);
        if bank.recent.len() > 40 {
            bank.recent.remove(0);
        }
    }
}

/// Ends a voice. Not immediate (0x004fb360): a looping sequence plays its outro, an envelope fades out over its
/// fade_out (the voice is removed when the fade ends).
fn end_voice(commands: &mut Commands, e: Entity, v: &mut Voice, immediate: bool, replay: &mut MessageWriter<PlaySound>) {
    if !immediate {
        if let Some(outro) = v.outro.take() {
            replay.write(PlaySound { name: outro, at: Some(v.at), follow: v.follow, looped: Some(false), part: Part::Piece, ..v.request.clone() });
            commands.entity(e).despawn();
            return;
        }
        if v.envelope.is_some_and(|env| env.fade_out > 0.0) {
            v.fading.get_or_insert(0.0);
            return;
        }
    }
    commands.entity(e).despawn();
}

fn stop(
    mut commands: Commands,
    (mut names, mut ids, mut on): (MessageReader<StopSound>, MessageReader<StopVoice>, MessageReader<StopSoundsOn>),
    mut voices: Query<(Entity, &SoundName, &mut Voice, Option<&SoundId>)>,
    mut replay: MessageWriter<PlaySound>,
) {
    let names: Vec<String> = names.read().map(|s| s.0.clone()).collect();
    let ids: Vec<StopVoice> = ids.read().copied().collect();
    let on: Vec<Entity> = on.read().map(|s| s.0).collect();
    if names.is_empty() && ids.is_empty() && on.is_empty() {
        return;
    }
    for (e, n, mut v, id) in &mut voices {
        if names.iter().any(|s| s == ALL_BUT_MUSIC) && v.group != VolumeGroup::Music {
            commands.entity(e).despawn();
            continue;
        }
        let by_id = id.and_then(|id| ids.iter().find(|s| s.id == *id));
        if let Some(s) = by_id {
            end_voice(&mut commands, e, &mut v, s.immediate, &mut replay);
        } else if names.iter().any(|s| *s == n.0) || v.follow.is_some_and(|f| on.contains(&f)) {
            end_voice(&mut commands, e, &mut v, false, &mut replay);
        }
    }
}

/// Per frame (the voice update 0x004f2a80 / 0x004f29d0 and the driver's 0x004fb1f0): follow, distance, pause
/// (0x004f2d60 / resume 0x004f2e40), envelope, volume;
/// a finished voice starts what comes after it (the loop body after an intro, a follow-on).
#[allow(clippy::too_many_arguments)]
fn update_voices(
    mut commands: Commands,
    time: Res<Time<Real>>,
    (mixer, pause): (Res<Mixer>, Res<GamePause>),
    listener: Query<&GlobalTransform, With<SpatialListener>>,
    followed: Query<&GlobalTransform, Without<Voice>>,
    mut voices: Query<(Entity, &mut Voice, &mut Transform, Option<&SoundId>, Option<&mut AudioSink>, Option<&mut SpatialAudioSink>)>,
    mut offsets: MessageReader<SetVoiceOffset>,
    mut replay: MessageWriter<PlaySound>,
) {
    let offsets: HashMap<SoundId, f32> = offsets.read().map(|o| (o.id, o.db)).collect();
    let ear = listener.single().ok().map(|l| l.translation());
    let dt = time.delta_secs();
    for (e, mut v, mut t, id, mut sink, mut spatial) in &mut voices {
        if let Some(db) = id.and_then(|id| offsets.get(id)) {
            v.offset_db = *db;
        }
        if let Some(f) = v.follow {
            match followed.get(f) {
                Ok(ft) => v.at = ft.translation(),
                // FxObject_Remove (0x00418db0) stops its object's loop
                Err(_) if v.looped => {
                    end_voice(&mut commands, e, &mut v, false, &mut replay);
                    continue;
                }
                Err(_) => {}
            }
        }
        if t.translation != v.at {
            t.translation = v.at;
        }
        if let (Some(_), Some(l)) = (v.range, ear) {
            v.distance = v.at.distance(l);
        }
        // a streamed positional voice beyond its max distance (0x004fb1f0): a loop pauses until it is back in range,
        // a one-shot stops
        let far = v.streamed && v.range.is_some_and(|(_, hi)| v.distance >= hi);
        if far && !v.looped {
            commands.entity(e).despawn();
            continue;
        }
        let game_paused = pause.paused && !v.through_pause && v.started_in_pause != Some(pause.epoch);
        let want_paused = game_paused || far;
        let s: &mut dyn AudioSinkPlayback = if let Some(s) = sink.as_deref_mut() {
            s
        } else if let Some(s) = spatial.as_deref_mut() {
            s
        } else {
            v.waiting += dt;
            if v.waiting > LOAD_TIMEOUT {
                warn!("audio: {} did not load, dropped", v.key);
                commands.entity(e).despawn();
            }
            continue;
        };
        if s.is_paused() != want_paused {
            if want_paused { s.pause() } else { s.play() }
        }
        v.paused = want_paused;
        if !want_paused {
            v.age += dt;
            if let Some(f) = v.fading.as_mut() {
                *f += dt;
            }
        }
        if let (Some(f), Some(env)) = (v.fading, v.envelope) {
            if env.fade_level(f).is_none() {
                commands.entity(e).despawn();
                continue;
            }
        }
        v.gain_db = v.level_db(&mixer);
        s.set_volume(Volume::Linear(v.gain(&mixer)));
        // the end of a one-shot: the body after an intro, else the follow-on (0x004f2a80, on the same place, taking
        // over the handle)
        if !v.looped || v.then_body.is_some() {
            if s.empty() {
                if let Some(body) = v.then_body.take() {
                    replay.write(PlaySound { at: Some(v.at), ..body });
                } else if let Some(next) = v.follow_on.take() {
                    replay.write(PlaySound { name: next, at: Some(v.at), follow: v.follow, looped: Some(false), part: Part::Whole, fade_elapsed: None, through_pause: false, ..v.request.clone() });
                }
                commands.entity(e).despawn();
            }
        }
    }
}

/// A summary of the sound system for the playtest log: voices started and refused, what plays now (count, level in
/// dB without the master factor, paused) by bank name, the group levels.
pub fn summary(stats: &AudioStats, mixer: &Mixer, voices: &Query<&Voice>, channels: &Channels) -> serde_json::Value {
    let mut playing: BTreeMap<&str, u32> = BTreeMap::new();
    let mut by_group: BTreeMap<&str, u32> = [VolumeGroup::Sfx, VolumeGroup::Speech, VolumeGroup::Music, VolumeGroup::Fixed].iter().map(|g| (g.name(), 0)).collect();
    let mut level: BTreeMap<&str, f32> = BTreeMap::new();
    let mut paused = 0;
    let mut paused_by_name: BTreeMap<&str, u32> = BTreeMap::new();
    for v in voices.iter() {
        *playing.entry(&v.key).or_default() += 1;
        *by_group.entry(v.group.name()).or_default() += 1;
        let db = (v.gain_db.max(-100.0) * 10.0).round() / 10.0;
        level.entry(&v.key).and_modify(|d| *d = d.max(db)).or_insert(db);
        paused += usize::from(v.paused);
        if v.paused {
            *paused_by_name.entry(&v.key).or_default() += 1;
        }
    }
    let r = |v: f32| (v.max(-100.0) * 10.0).round() / 10.0;
    let ch: serde_json::Map<String, serde_json::Value> =
        channels.0.iter().enumerate().map(|(i, c)| (i.to_string(), c.and_then(|(e, _)| voices.get(e).ok()).map(|v| v.key.clone()).unwrap_or_default().into())).collect();
    serde_json::json!({
        "started": stats.started,
        "started_by_group": stats.started_by_group,
        "subtitle": stats.last_subtitle,
        "refused": stats.refused,
        "playing": playing,
        "playing_by_group": by_group,
        "level_db": level,
        "paused": paused,
        "paused_by_name": paused_by_name,
        "channels": ch,
        "groups_db": {"sfx": r(mixer.group_db(VolumeGroup::Sfx)), "speech": r(mixer.group_db(VolumeGroup::Speech)), "music": r(mixer.group_db(VolumeGroup::Music))},
    })
}

/// Playback check (playtest evidence that sounds are heard, not just started): every 0.1 s each playing sound's
/// position on the output device is compared with the last sample; advancing, paused and silent sinks are counted,
/// and every sound name that advanced at least once is kept.
#[derive(Resource, Default)]
pub struct AudioCheck {
    pub advancing: usize,
    pub stalled: usize,
    pub silent: usize,
    pub heard: std::collections::BTreeSet<String>,
    pub voice_heard: usize,
    last: HashMap<Entity, f32>,
    timer: f32,
}

pub fn audio_check(
    time: Res<Time<Real>>,
    mut chk: ResMut<AudioCheck>,
    sinks: Query<(Entity, &AudioSink, &SoundName)>,
    spatial: Query<(Entity, &SpatialAudioSink, &SoundName)>,
    bank: Option<Res<SoundBank>>,
) {
    chk.timer -= time.delta_secs();
    if chk.timer > 0.0 {
        return;
    }
    chk.timer = 0.1;
    let (mut adv, mut stalled, mut silent) = (0, 0, 0);
    let mut seen = HashMap::new();
    let mut heard = Vec::new();
    let mut check = |e: Entity, pos: f32, paused: bool, vol: f32, name: &str, last: &HashMap<Entity, f32>| {
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
        if bank.as_ref().is_some_and(|b| b.sounds.get(&n.to_lowercase()).is_some_and(|d| d.group() == VolumeGroup::Speech)) || n.starts_with('@') {
            chk.voice_heard += 1;
        }
        chk.heard.insert(n);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn level_curve_matches_the_bezier() {
        assert_eq!(level_curve(5.0, 10.0, 80.0), 0.0);
        assert_eq!(level_curve(80.0, 10.0, 80.0), SILENT_DB);
        assert!((level_curve(45.0, 10.0, 80.0) - -10.625).abs() < 1e-4);
        // the default option 0.8: -3.2 dB in the original
        assert!((level_curve(0.2, 0.0, 1.0) - -3.2).abs() < 1e-4);
    }

    #[test]
    fn group_levels_follow_the_options() {
        let mut m = Mixer::default();
        assert!(m.group_db(VolumeGroup::Sfx).abs() < 1e-5);
        m.sfx = 0.5;
        assert!((m.group_db(VolumeGroup::Sfx) - (-10.625 + 3.2)).abs() < 1e-4);
        m.music = 0.0;
        assert!(m.group_db(VolumeGroup::Music) <= -1000.0);
        assert_eq!(m.group_db(VolumeGroup::Fixed), 0.0);
        m.voice = 1.0;
        assert!((m.group_db(VolumeGroup::Speech) - 3.2).abs() < 1e-4);
    }

    #[test]
    fn envelope_levels() {
        // the music envelope: level 1 from the first moment, a 10 s fade-out from 0 to -40 dB
        let music = Envelope { fade_out: 10.0, level: 1.0, ..default() };
        assert_eq!(music.level_at(0.01), 1.0);
        assert_eq!(music.fade_level(0.0), Some(1.0));
        assert!((Envelope::level_db(music.fade_level(5.0).unwrap()) - -20.0).abs() < 1e-4);
        assert_eq!(music.fade_level(10.0), None);
        let e = Envelope { delay: 1.0, attack: 2.0, hold: 1.0, release: 2.0, fade_out: 0.0, level: 0.5 };
        assert_eq!(e.level_at(0.5), 0.0);
        assert!((e.level_at(2.0) - 0.5).abs() < 1e-6);
        assert_eq!(e.level_at(3.5), 1.0);
        assert_eq!(e.level_at(7.0), 0.5);
    }

    #[test]
    fn pitch_variation_range() {
        assert_eq!(rate_factor(0.0, 0.7), 1.0);
        assert!((rate_factor(1.0, 0.0) - 0.875).abs() < 1e-6);
        assert!((rate_factor(1.0, 1.0) - 1.25).abs() < 1e-6);
    }

    fn bank(names: &[&str]) -> SoundBank {
        let mut b = SoundBank::default();
        for n in names {
            b.sounds.insert(n.to_string(), SoundDef::default());
        }
        b
    }

    #[test]
    fn grunt_pools_never_cross() {
        let b = bank(&["gr150026_dead", "gr150027_dead", "gr150028_dead", "gr150021_dead", "gr150022_dead", "gr250026_dead"]);
        for pick in 0..50 {
            let last = b.resolve("@grunt:Dead:0:last3", pick).unwrap();
            assert!(["gr150026_dead", "gr150027_dead", "gr150028_dead"].contains(&last.as_str()), "{last}");
            let rest = b.resolve("@grunt:Dead:0:butlast3", pick).unwrap();
            assert!(["gr150021_dead", "gr150022_dead"].contains(&rest.as_str()), "{rest}");
            // set 1 has one line; set 2 (none) falls back to every set
            assert_eq!(b.resolve("@grunt:Dead:1", pick).unwrap(), "gr250026_dead");
            assert!(b.resolve("@grunt:Dead:2", pick).unwrap().starts_with("gr"));
        }
        // every line of the pool comes up
        let seen: HashSet<String> = (0..30).map(|p| b.resolve("@grunt:Dead:0:last3", p).unwrap()).collect();
        assert_eq!(seen.len(), 3);
        assert_eq!(b.resolve("Plain_Name", 0).as_deref(), Some("plain_name"));
    }
}
