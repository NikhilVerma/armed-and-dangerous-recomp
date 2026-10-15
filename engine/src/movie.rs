//! Movies: full-window cut scenes and looping background movies, with subtitles, skipping and audio kept in step
//! with the picture. Game-neutral: a game asks for a play list with `PlayMovies` and hears back through
//! `MovieEvent`; where the files are, the volume and whether subtitles show are `MovieSettings`.
//!
//! The original (src/movie/movie_01.cpp, the movie screens of src/ui/ui_03.cpp) plays DivX AVI files through
//! DivxMediaLib.dll. The remaster plays what tools/remaster_export_movies.py made from them, one folder per movie
//! under `MovieSettings::dir`: `video.rmjv` (every frame a baseline JPEG, decoded with zune-jpeg: pure Rust, no
//! codec shipped), `audio.wav` (the soundtrack as stored) and `movie.json` (size, rate, subtitle lines with their
//! speakers' styles). The format and the choice are described in that tool.
//!
//! What is ported, with the original's addresses:
//! - the clock (Movie_Update 0x0050e110): the real frame time is added up; while at least 0.9 of a frame
//!   ([0x005bcc38]) is due, one frame is consumed and decoded (Movie_DecodeFrame 0x0050e410); the end of the file
//!   ends the movie, or goes back to frame 0 when it loops (flag 0x10). The remaster decodes only the last frame
//!   due (every frame is a key frame), the original each one;
//! - the video time (0x0050e740: the current frame / fps) drives the subtitles (0x00495c50: the line with
//!   start <= t <= end, a binary search) drawn in the speaker's style (0x00495cb0, the table 0x005cadd8: colour,
//!   always shown or only with the movie subtitle option Game+0x176, size fitted to 576 units, position);
//! - the audio follows the picture (Movie_StreamAudio 0x0050e1a0): d = audio time - video time; outside +-0.05 s
//!   the playback rate aims at 1 / (1 + 0.05 min(((d - 0.05) 2.2222)^2, 1)) (audio ahead) or
//!   1 + 0.05 min(((d + 0.05) 2.2222)^2, 1) (behind), moving 2.5 % of the way each update. Remaster addition: a
//!   gap over 0.5 s (a stall, a test run's fixed time step) is closed by seeking the audio;
//! - the play list (0x00493270: names separated by ',' or ';'), skipping to the next movie on the menu's select
//!   or back buttons (the movie screen 0x004933e0, input bits 0x30; "PlayMovieNoSkip", flags 0x4000, cannot be
//!   skipped), and the screen ending when the list is done;
//! - the picture's place (0x00493440 with 0x004938e0 / 0x004937a0): centred, square pixels, 0.8 of the original's
//!   640-wide screen for the movie screens (flag 0x8000: width * 5 / 4), the whole width for the pub's and the
//!   credits' background movies. The remaster keeps that 640 x 480 frame and scales it to the window (the
//!   original's capture work/shots/origmovA: the 512 x 308 picture at x 64..575, rows 86..393, subtitles below).
//!
//! Remaster additions: a skip prompt with the active device's button (`MovieSettings::skip_prompt`); a mouse click
//! (a trackpad tap) also skips.

use crate::gamemenu::MenuOwned;
use crate::hud::{Anchor, HudElement, HudText, TextAlign};
use bevy::asset::RenderAssetUsages;
use bevy::audio::{AudioSinkPlayback, PlaybackMode, Volume};
use bevy::camera::visibility::RenderLayers;
use bevy::prelude::*;
use bevy::render::render_resource::{Extent3d, TextureDimension, TextureFormat};
use serde::Deserialize;
use std::collections::VecDeque;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

/// The render layer of full-window movies (drawn by their own camera over everything else).
pub const MOVIE_LAYER: usize = 6;
/// That camera's order (the HUD's is 1).
const MOVIE_CAMERA_ORDER: isize = 10;

// ---------------------------------------------------------------------------------------------------------------
// Public API

/// Where and how movies play.
#[derive(Resource, Clone)]
pub struct MovieSettings {
    /// the exported movies (one folder per movie, lower-case names)
    pub dir: PathBuf,
    /// show the subtitle lines that are not always shown (the original's movie subtitle option, Game+0x176)
    pub subtitles: bool,
    /// linear volume of the soundtrack
    pub volume: f32,
    /// show "<button> Skip" on skippable full-window movies (remaster addition)
    pub skip_prompt: bool,
    /// the skip prompt's text after the button
    pub skip_text: String,
}

impl Default for MovieSettings {
    fn default() -> Self {
        MovieSettings { dir: PathBuf::from("movies"), subtitles: true, volume: 1.0, skip_prompt: true, skip_text: "Skip".into() }
    }
}

/// How a play list is shown.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum MovieView {
    /// over everything, on black, the picture `width` units wide in the original's 640 x 480 screen (the movie
    /// screens: 512, 0.8 of it)
    FullWindow { width: f32 },
    /// behind a page's art on the HUD layer at this z, `width` units wide (the pub's and the credits' movies: 640)
    Behind { width: f32, z: f32 },
}

impl MovieView {
    /// The movie screens ("PlayMovie", flag 0x8000: the quad fitted to width * 5 / 4, 0x00493440).
    pub const SCREEN: MovieView = MovieView::FullWindow { width: 512.0 };
}

/// Play these movies one after another (a list as the original's "a,b;c" strings: `PlayMovies::list`).
#[derive(Message, Clone, Debug)]
pub struct PlayMovies {
    pub names: Vec<String>,
    /// the menu's select / back buttons (or a click) skip to the next movie
    pub skippable: bool,
    /// the last movie starts again at its end (until `StopMovies`)
    pub looped: bool,
    pub view: MovieView,
    /// start the first movie this many seconds in (tests, chapter starts)
    pub start_at: f32,
}

impl PlayMovies {
    /// A skippable full-window play list from the original's string form ("LucasAnim,pm_logo,pre_1_1_opening").
    pub fn list(list: &str) -> Self {
        PlayMovies {
            names: list.split([',', ';']).map(str::trim).filter(|s| !s.is_empty()).map(str::to_owned).collect(),
            skippable: true,
            looped: false,
            view: MovieView::SCREEN,
            start_at: 0.0,
        }
    }
}

/// Stop whatever plays (the list ends; `MovieEvent::ListDone` follows).
#[derive(Message, Clone, Debug)]
pub struct StopMovies;

/// What the player did.
#[derive(Message, Clone, Debug, PartialEq)]
pub enum MovieEvent {
    Started(String),
    /// a movie ended: played out, skipped, or missing (not exported: `missing`)
    Ended { name: String, skipped: bool, missing: bool },
    /// the play list is done
    ListDone,
}

/// The player's state (read it; change it through the messages).
#[derive(Resource, Default)]
pub struct MoviePlayer {
    list: VecDeque<String>,
    request: Option<PlayMovies>,
    current: Option<Playing>,
    /// frames left during which the menus still ignore input after a list ended (the press that skipped the last
    /// movie must not also act on the page under it)
    input_hold: u8,
}

impl MoviePlayer {
    /// A play list is running.
    pub fn busy(&self) -> bool {
        self.current.is_some() || !self.list.is_empty()
    }
    /// A full-window movie is on screen (or the press that ended one is still being released): the game's input
    /// belongs to the movie.
    pub fn takes_input(&self) -> bool {
        self.input_hold > 0 || self.current.as_ref().is_some_and(|p| matches!(p.view, MovieView::FullWindow { .. }))
    }
}

/// Run condition: no full-window movie holds the input (menus, cheat entry).
pub fn no_movie_input(p: Option<Res<MoviePlayer>>) -> bool {
    p.is_none_or(|p| !p.takes_input())
}

pub struct MoviePlugin;

impl Plugin for MoviePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<MoviePlayer>()
            .init_resource::<MovieSettings>()
            .add_message::<PlayMovies>()
            .add_message::<StopMovies>()
            .add_message::<MovieEvent>()
            .add_systems(First, release_input_hold)
            .add_systems(Update, (requests, start_next, advance, skip, sync_audio, subtitles).chain().in_set(MovieSystems))
            .add_systems(PostUpdate, layout.before(bevy::transform::TransformSystems::Propagate));
    }
}

/// The movie systems (game glue that reacts to `MovieEvent` runs after them).
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct MovieSystems;

// ---------------------------------------------------------------------------------------------------------------
// Files

/// `movie.json` (tools/remaster_export_movies.py).
#[derive(Deserialize, Clone, Debug)]
pub struct MovieMeta {
    pub name: String,
    pub fps: f64,
    pub frames: u32,
    #[serde(default)]
    pub audio: Option<AudioFormat>,
    #[serde(default)]
    pub subtitles: Vec<SubtitleLine>,
}

#[derive(Deserialize, Clone, Copy, Debug)]
pub struct AudioFormat {
    pub rate: u32,
    pub channels: u32,
}

/// One subtitle line of the UI bank's tables (big-ui.md: start, end, speaker, text) with the speaker's style
/// (0x005cadd8).
#[derive(Deserialize, Clone, Debug)]
pub struct SubtitleLine {
    pub start: f32,
    pub end: f32,
    pub speaker: String,
    pub text_id: String,
    /// null: the id is not in the text bank (the original draws nothing, Text_Lookup 0x00495f00 quiet)
    pub text: Option<String>,
    pub colour: String,
    pub always_shown: bool,
    pub size: f32,
    pub x: f32,
    pub y: f32,
}

/// The line at `t` (0x00495c50: binary search over lines sorted by start; start <= t <= end).
pub fn line_at(lines: &[SubtitleLine], t: f32) -> Option<&SubtitleLine> {
    let (mut lo, mut hi) = (0i32, lines.len() as i32 - 1);
    while lo <= hi {
        let mid = (lo + hi) / 2;
        let e = &lines[mid as usize];
        if e.start > t {
            hi = mid - 1;
        } else if !(e.end < t) {
            return Some(e);
        } else {
            lo = mid + 1;
        }
    }
    None
}

/// `video.rmjv`: frames as JPEGs with an index (format: tools/remaster_export_movies.py).
pub struct VideoFile {
    file: std::fs::File,
    pub width: u32,
    pub height: u32,
    pub fps: f64,
    index: Vec<(u64, u32)>,
    buf: Vec<u8>,
}

impl VideoFile {
    pub fn open(path: &Path) -> Result<Self, String> {
        let mut file = std::fs::File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let mut head = [0u8; 32];
        file.read_exact(&mut head).map_err(|e| format!("{}: {e}", path.display()))?;
        let u = |o: usize| u32::from_le_bytes(head[o..o + 4].try_into().unwrap());
        if &head[0..4] != b"RMJV" || u(4) != 1 {
            return Err(format!("{}: not an RMJV version 1 file", path.display()));
        }
        let (width, height, num, den, n) = (u(8), u(12), u(16), u(20).max(1), u(24) as usize);
        let mut raw = vec![0u8; n * 12];
        file.read_exact(&mut raw).map_err(|e| format!("{}: index: {e}", path.display()))?;
        let index = raw.chunks_exact(12).map(|c| (u64::from_le_bytes(c[0..8].try_into().unwrap()), u32::from_le_bytes(c[8..12].try_into().unwrap()))).collect();
        Ok(VideoFile { file, width, height, fps: num as f64 / den as f64, index, buf: Vec::new() })
    }

    pub fn frames(&self) -> usize {
        self.index.len()
    }

    /// Decodes frame `k` into `out` (width * height RGBA bytes).
    pub fn decode(&mut self, k: usize, out: &mut [u8]) -> Result<(), String> {
        let (off, len) = *self.index.get(k).ok_or_else(|| format!("frame {k} out of range"))?;
        self.buf.resize(len as usize, 0);
        self.file.seek(SeekFrom::Start(off)).and_then(|_| self.file.read_exact(&mut self.buf)).map_err(|e| e.to_string())?;
        let options = zune_core::options::DecoderOptions::default().jpeg_set_out_colorspace(zune_core::colorspace::ColorSpace::RGBA);
        let mut d = zune_jpeg::JpegDecoder::new_with_options(zune_core::bytestream::ZCursor::new(&self.buf[..]), options);
        d.decode_headers().map_err(|e| format!("frame {k}: {e:?}"))?;
        d.decode_into(out).map_err(|e| format!("frame {k}: {e:?}"))
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Playback

struct Playing {
    meta: MovieMeta,
    video: VideoFile,
    view: MovieView,
    skippable: bool,
    looped: bool,
    /// Movie_Update's accumulator +0x218 and seconds per frame +0x21c
    acc: f64,
    spf: f64,
    /// frames consumed since the start (or the last loop): the next frame to decode (+0x23c, guess)
    decoded: i64,
    /// the frame on screen (-1: none yet, black)
    shown: i64,
    /// seconds played (for the prompt)
    age: f32,
    /// the first update after opening adds no time (opening read the files); the clock waits for the audio
    fresh: bool,
    audio: Option<Entity>,
    /// the audio playback-rate factor +0x220
    rate: f32,
    /// last audio time read (the playtest log)
    audio_time: Option<f64>,
    pending_skip: bool,
    image: Handle<Image>,
    rgba: Vec<u8>,
    entities: Vec<Entity>,
    subtitle: Entity,
    prompt: Entity,
    frame: Entity,
    shown_line: Option<String>,
    prompt_until: f32,
    wait: f32,
}

impl Playing {
    fn video_time(&self) -> f64 {
        self.decoded as f64 * self.spf
    }
}

/// The soundtrack's entity.
#[derive(Component)]
pub struct MovieSound;

/// The picture (layout sizes it).
#[derive(Component)]
struct MovieFrame {
    width: f32,
    aspect: f32,
}

/// The black backdrop of a full-window movie.
#[derive(Component)]
struct MovieBackdrop;

fn release_input_hold(mut p: ResMut<MoviePlayer>) {
    if p.current.is_none() && p.input_hold > 0 {
        p.input_hold -= 1;
    }
}

fn requests(mut p: ResMut<MoviePlayer>, mut play: MessageReader<PlayMovies>, mut stop: MessageReader<StopMovies>, mut commands: Commands, mut events: MessageWriter<MovieEvent>) {
    let stop = stop.read().count() > 0;
    let req = play.read().last().cloned();
    if stop || req.is_some() {
        p.list.clear();
        if let Some(cur) = p.current.take() {
            let full = matches!(cur.view, MovieView::FullWindow { .. });
            close(&mut commands, cur, false, &mut events);
            if full {
                p.input_hold = 2;
            }
            if req.is_none() {
                events.write(MovieEvent::ListDone);
            }
        }
    }
    if let Some(r) = req {
        p.list = r.names.iter().cloned().collect();
        p.request = Some(r);
    }
}

fn close(commands: &mut Commands, cur: Playing, skipped: bool, events: &mut MessageWriter<MovieEvent>) {
    for e in &cur.entities {
        commands.entity(*e).despawn();
    }
    if let Some(a) = cur.audio {
        commands.entity(a).despawn();
    }
    info!("EVENT movie: {} ended at {:.2} s{}", cur.meta.name, cur.video_time(), if skipped { " (skipped)" } else { "" });
    events.write(MovieEvent::Ended { name: cur.meta.name, skipped, missing: false });
}

#[allow(clippy::too_many_arguments)]
fn start_next(
    mut p: ResMut<MoviePlayer>,
    settings: Res<MovieSettings>,
    mut commands: Commands,
    mut images: ResMut<Assets<Image>>,
    mut sounds: ResMut<Assets<AudioSource>>,
    mut events: MessageWriter<MovieEvent>,
) {
    if p.current.is_some() {
        return;
    }
    let Some(req) = p.request.clone() else { return };
    while let Some(name) = p.list.pop_front() {
        let first = p.request.as_ref().is_some_and(|r| r.names.len() == p.list.len() + 1);
        match open(&settings, &name) {
            Ok((meta, video)) => {
                let start_at = if first { req.start_at as f64 } else { 0.0 };
                let looped = req.looped && p.list.is_empty();
                let playing = spawn(&mut commands, &mut images, &mut sounds, &settings, meta, video, &req, looped, start_at);
                info!("EVENT movie: {} started ({} frames at {} fps, {} subtitle lines)", playing.meta.name, playing.meta.frames, playing.meta.fps, playing.meta.subtitles.len());
                events.write(MovieEvent::Started(playing.meta.name.clone()));
                p.current = Some(playing);
                return;
            }
            Err(e) => {
                // a movie that was not exported is passed over, as the original's failed open (flags |= 4 at
                // 0x0050de70) makes the screen try the next one (0x004933e0)
                warn!("EVENT movie: {name} missing: {e}");
                events.write(MovieEvent::Ended { name: name.to_lowercase(), skipped: false, missing: true });
            }
        }
    }
    p.request = None;
    events.write(MovieEvent::ListDone);
}

fn open(settings: &MovieSettings, name: &str) -> Result<(MovieMeta, VideoFile), String> {
    let dir = settings.dir.join(name.to_lowercase());
    let text = std::fs::read_to_string(dir.join("movie.json")).map_err(|e| format!("{}: {e}", dir.join("movie.json").display()))?;
    let meta: MovieMeta = serde_json::from_str(&text).map_err(|e| format!("movie.json: {e}"))?;
    let video = VideoFile::open(&dir.join("video.rmjv"))?;
    Ok((meta, video))
}

#[allow(clippy::too_many_arguments)]
fn spawn(
    commands: &mut Commands,
    images: &mut Assets<Image>,
    sounds: &mut Assets<AudioSource>,
    settings: &MovieSettings,
    meta: MovieMeta,
    video: VideoFile,
    req: &PlayMovies,
    looped: bool,
    start_at: f64,
) -> Playing {
    let (w, h) = (video.width, video.height);
    let image = images.add(Image::new_fill(
        Extent3d { width: w, height: h, depth_or_array_layers: 1 },
        TextureDimension::D2,
        &[0, 0, 0, 255],
        TextureFormat::Rgba8UnormSrgb,
        RenderAssetUsages::MAIN_WORLD | RenderAssetUsages::RENDER_WORLD,
    ));
    let mut entities = Vec::new();
    let (layer, z, width) = match req.view {
        MovieView::FullWindow { width } => {
            entities.push(commands.spawn((Camera2d, Camera { order: MOVIE_CAMERA_ORDER, clear_color: ClearColorConfig::None, ..default() }, RenderLayers::layer(MOVIE_LAYER))).id());
            entities.push(commands.spawn((Sprite::from_color(Color::BLACK, Vec2::ONE), MovieBackdrop, Transform::from_xyz(0.0, 0.0, 0.0), RenderLayers::layer(MOVIE_LAYER))).id());
            (MOVIE_LAYER, 1.0, width)
        }
        MovieView::Behind { width, z } => (crate::hud::HUD_LAYER, z, width),
    };
    let frame = commands
        .spawn((
            Sprite { image: image.clone(), custom_size: Some(Vec2::ONE), ..default() },
            MovieFrame { width, aspect: h as f32 / w.max(1) as f32 },
            Transform::from_xyz(0.0, 0.0, z),
            RenderLayers::layer(layer),
        ))
        .id();
    entities.push(frame);
    // the subtitle line and the skip prompt: HUD texts on the movie's layer (hud.rs builds and places them)
    let text = |name: &str, pos: Vec2, anchor: Anchor, scale: f32, fit: f32| {
        (
            HudElement { name: name.into(), pos, anchor, full: true },
            HudText { text: String::new(), built: None, scale, colour: Color::WHITE, fit },
            MenuOwned,
            Transform::from_xyz(0.0, 0.0, z + 10.0),
            Visibility::Hidden,
            RenderLayers::layer(layer),
        )
    };
    let subtitle = commands.spawn(text("movie_subtitle", Vec2::new(320.0, 72.0), Anchor::Centre, 1.1, 576.0)).id();
    let prompt = commands.spawn((text("movie_skip", Vec2::new(628.0, 16.0), Anchor::Right, 0.8, 0.0), TextAlign(1.0))).id();
    entities.push(subtitle);
    entities.push(prompt);
    let audio = meta.audio.and_then(|_| {
        let path = settings.dir.join(meta.name.to_lowercase()).join("audio.wav");
        match std::fs::read(&path) {
            Ok(bytes) => {
                let handle = sounds.add(AudioSource { bytes: bytes.into() });
                let s = PlaybackSettings { mode: PlaybackMode::Once, volume: Volume::Linear(settings.volume), paused: true, ..default() };
                Some(commands.spawn((AudioPlayer(handle), s, MovieSound)).id())
            }
            Err(e) => {
                warn!("movie: {}: {e} (plays silent)", path.display());
                None
            }
        }
    });
    let spf = 1.0 / video.fps.max(1.0);
    let rgba = vec![0u8; (w * h * 4) as usize];
    let decoded = (start_at / spf).floor() as i64;
    Playing {
        meta,
        video,
        view: req.view,
        skippable: req.skippable,
        looped,
        acc: start_at - decoded as f64 * spf,
        spf,
        decoded,
        shown: -1,
        age: 0.0,
        fresh: true,
        audio,
        rate: 1.0,
        audio_time: None,
        pending_skip: false,
        image,
        rgba,
        entities,
        subtitle,
        prompt,
        frame,
        shown_line: None,
        prompt_until: 4.0,
        wait: 0.0,
    }
}

/// The clock and the picture (Movie_Update 0x0050e110, Movie_DecodeFrame 0x0050e410, Movie_UploadFrame 0x0050e620).
fn advance(
    time: Res<Time<Real>>,
    mut p: ResMut<MoviePlayer>,
    mut commands: Commands,
    mut images: ResMut<Assets<Image>>,
    sinks: Query<&AudioSink, With<MovieSound>>,
    mut events: MessageWriter<MovieEvent>,
) {
    let Some(cur) = p.current.as_mut() else { return };
    let dt = time.delta_secs_f64();
    if cur.fresh {
        // the clock starts with the soundtrack (its sink exists once the source is decoded), at most 0.5 s later
        let sink = cur.audio.and_then(|a| sinks.get(a).ok());
        cur.wait += dt as f32;
        if cur.audio.is_some() && sink.is_none() && cur.wait < 0.5 {
            return;
        }
        if let Some(s) = sink {
            if cur.video_time() > 0.0 {
                let _ = s.try_seek(std::time::Duration::from_secs_f64(cur.video_time()));
            }
            s.play();
        }
        cur.fresh = false;
    } else {
        cur.acc += dt;
        cur.age += dt as f32;
    }
    let frames = cur.video.frames() as i64;
    let mut ended = false;
    // 0.9 of a frame due consumes one ([0x005bcc38])
    while cur.acc >= cur.spf * 0.9 {
        cur.acc -= cur.spf;
        if cur.decoded >= frames {
            if cur.looped {
                // DivxGotoFrame(h, 0), flags |= 8: this pass decodes nothing
                cur.decoded = 0;
                if let Some(s) = cur.audio.and_then(|a| sinks.get(a).ok()) {
                    let _ = s.try_seek(std::time::Duration::ZERO);
                }
            } else {
                ended = true;
                break;
            }
        } else {
            cur.decoded += 1;
        }
    }
    let want = cur.decoded - 1;
    if want >= 0 && want != cur.shown && want < frames {
        let mut rgba = std::mem::take(&mut cur.rgba);
        match cur.video.decode(want as usize, &mut rgba) {
            Ok(()) => {
                if let Some(mut img) = images.get_mut(&cur.image) {
                    img.data = Some(rgba.clone());
                }
                cur.shown = want;
            }
            Err(e) => {
                warn!("movie: {}: {e}", cur.meta.name);
                ended = true;
            }
        }
        cur.rgba = rgba;
    }
    if ended || cur.pending_skip {
        let skipped = cur.pending_skip && !ended;
        let cur = p.current.take().unwrap();
        let full = matches!(cur.view, MovieView::FullWindow { .. });
        close(&mut commands, cur, skipped, &mut events);
        if full {
            p.input_hold = 2;
        }
    }
}

/// Skipping (the movie screen's update 0x004933e0: input bits 0x30, the menu's select / back): to the next movie
/// of the list.
fn skip(
    mut p: ResMut<MoviePlayer>,
    controls: Res<crate::input::Controls>,
    mouse: Res<ButtonInput<MouseButton>>,
    motion: Res<bevy::input::mouse::AccumulatedMouseMotion>,
) {
    let Some(cur) = p.current.as_mut() else { return };
    if !cur.skippable || !matches!(cur.view, MovieView::FullWindow { .. }) {
        return;
    }
    use crate::input::Act;
    if controls.pressed(Act::Select) || controls.pressed(Act::Back) || mouse.just_pressed(MouseButton::Left) {
        cur.pending_skip = true;
    }
    // the prompt shows for the first seconds and again when the mouse moves (remaster addition)
    if motion.delta.length_squared() > 4.0 {
        cur.prompt_until = cur.age + 3.0;
    }
}

/// The soundtrack follows the picture (Movie_StreamAudio 0x0050e1a0).
fn sync_audio(mut p: ResMut<MoviePlayer>, mut sinks: Query<&mut AudioSink, With<MovieSound>>, settings: Res<MovieSettings>) {
    let Some(cur) = p.current.as_mut() else { return };
    let Some(mut sink) = cur.audio.and_then(|a| sinks.get_mut(a).ok()) else { return };
    sink.set_volume(Volume::Linear(settings.volume));
    if cur.fresh {
        return;
    }
    let audio = sink.position().as_secs_f64();
    cur.audio_time = Some(audio);
    let video = cur.video_time();
    let d = (audio - video) as f32;
    if d.abs() > 0.5 && !sink.empty() {
        // remaster addition: a large gap (a stall, a fixed-step test run) is closed at once
        let _ = sink.try_seek(std::time::Duration::from_secs_f64(video));
        cur.rate = 1.0;
        sink.set_speed(1.0);
        return;
    }
    let target = if d > 0.05 {
        1.0 / (1.0 + 0.05 * (((d - 0.05) * 2.2222) * ((d - 0.05) * 2.2222)).min(1.0))
    } else if d < -0.05 {
        1.0 + 0.05 * (((d + 0.05) * -2.2222) * ((d + 0.05) * -2.2222)).min(1.0)
    } else {
        1.0
    };
    if target != cur.rate {
        cur.rate += (target - cur.rate) * 0.025;
        sink.set_speed(cur.rate);
    }
}

/// The subtitle line and the skip prompt (0x00495cb0's draw through hud.rs texts).
fn subtitles(
    mut p: ResMut<MoviePlayer>,
    settings: Res<MovieSettings>,
    mut texts: Query<(&mut HudText, &mut HudElement, &mut Visibility)>,
) {
    let Some(cur) = p.current.as_mut() else { return };
    let t = cur.video_time() as f32;
    let line = line_at(&cur.meta.subtitles, t).filter(|l| l.text.is_some() && (l.always_shown || settings.subtitles)).cloned();
    if let Ok((mut text, mut el, mut vis)) = texts.get_mut(cur.subtitle) {
        match &line {
            Some(l) => {
                let colour = parse_colour(&l.colour);
                let s = l.text.clone().unwrap_or_default();
                if text.text != s || text.colour != colour || text.scale != l.size {
                    text.text = s;
                    text.colour = colour;
                    text.scale = l.size;
                    text.built = None;
                }
                // x, y: fractions of the screen, y up (0x00495cb0 passes them to Text_Draw)
                el.pos = Vec2::new(l.x * 640.0, l.y * 480.0);
                *vis = Visibility::Inherited;
            }
            None => *vis = Visibility::Hidden,
        }
    }
    cur.shown_line = line.map(|l| l.text_id);
    let show_prompt = settings.skip_prompt && cur.skippable && matches!(cur.view, MovieView::FullWindow { .. }) && cur.age < cur.prompt_until;
    if let Ok((mut text, _, mut vis)) = texts.get_mut(cur.prompt) {
        let s = format!("{} {}", crate::input::key(crate::input::Act::Select), settings.skip_text);
        if text.text != s {
            text.text = s;
            text.colour = Color::srgb(0.75, 0.75, 0.75);
        }
        *vis = if show_prompt { Visibility::Inherited } else { Visibility::Hidden };
    }
}

fn parse_colour(s: &str) -> Color {
    u32::from_str_radix(s.trim_start_matches('#'), 16).map(|v| Color::srgb_u8((v >> 16) as u8, (v >> 8) as u8, v as u8)).unwrap_or(Color::WHITE)
}

/// Sizes the picture and the backdrop to the window: the original's 640 x 480 screen scaled to fit, centred.
fn layout(windows: Query<&Window>, mut frames: Query<(&MovieFrame, &mut Sprite), Without<MovieBackdrop>>, mut backs: Query<&mut Sprite, With<MovieBackdrop>>) {
    let size = windows.single().map(|w| Vec2::new(w.physical_width() as f32, w.physical_height() as f32)).unwrap_or(Vec2::new(1920.0, 1080.0));
    let s = (size.y / 480.0).min(size.x / 640.0);
    for (f, mut sprite) in &mut frames {
        let want = Vec2::new(f.width * s, f.width * f.aspect * s);
        if sprite.custom_size != Some(want) {
            sprite.custom_size = Some(want);
        }
    }
    for mut b in &mut backs {
        if b.custom_size != Some(size) {
            b.custom_size = Some(size);
        }
    }
}

/// The playtest log's "movie" (absent when none plays): name, video time, frame on screen, audio time and its
/// drift from the picture, the subtitle line's text id, the prompt.
pub fn log_state(p: Res<MoviePlayer>, play: Option<ResMut<crate::playtest::PlayState>>, texts: Query<&Visibility, With<HudText>>) {
    let Some(mut play) = play else { return };
    match p.current.as_ref() {
        Some(c) => {
            let r = |v: f64| (v * 1000.0).round() / 1000.0;
            let prompt = texts.get(c.prompt).is_ok_and(|v| *v != Visibility::Hidden);
            play.0.insert(
                "movie".into(),
                serde_json::json!({
                    "name": c.meta.name,
                    "time": r(c.video_time()),
                    "frame": c.shown,
                    "frames": c.meta.frames,
                    "audio_time": c.audio_time.map(r),
                    "drift": c.audio_time.map(|a| r(a - c.video_time())),
                    "rate": r(c.rate as f64),
                    "subtitle": c.shown_line.clone().unwrap_or_default(),
                    "prompt": prompt,
                    "left": p.list.len(),
                }),
            );
        }
        None => {
            play.0.remove("movie");
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn line(start: f32, end: f32, id: &str) -> SubtitleLine {
        SubtitleLine { start, end, speaker: "rome".into(), text_id: id.into(), text: Some(id.into()), colour: "#c0c040".into(), always_shown: false, size: 1.1, x: 0.5, y: 0.15 }
    }

    #[test]
    fn line_lookup_is_inclusive_and_skips_gaps() {
        let lines = vec![line(0.1, 9.5, "a"), line(13.5, 15.1, "b"), line(15.2, 22.1, "c")];
        assert_eq!(line_at(&lines, 0.1).map(|l| l.text_id.as_str()), Some("a"));
        assert_eq!(line_at(&lines, 9.5).map(|l| l.text_id.as_str()), Some("a"));
        assert!(line_at(&lines, 10.0).is_none());
        assert!(line_at(&lines, 0.05).is_none());
        assert_eq!(line_at(&lines, 15.15), None);
        assert_eq!(line_at(&lines, 20.0).map(|l| l.text_id.as_str()), Some("c"));
        assert!(line_at(&[], 1.0).is_none());
    }

    #[test]
    fn play_list_splits_like_the_original() {
        let p = PlayMovies::list("LucasAnim,pm_logo;pre_1_1_opening");
        assert_eq!(p.names, vec!["LucasAnim", "pm_logo", "pre_1_1_opening"]);
        assert!(p.skippable && !p.looped);
    }
}
