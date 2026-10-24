//! The audio output: plays the entities' sounds on the system's default output device and follows that device.
//!
//! bevy_audio opens the default device once, at startup, and cannot reopen it: switching the sound output in
//! Windows left the game on the old device, and unplugging that device silenced it for good (rodio: "audio stream
//! error: The requested device is no longer available"; player feedback A1, 2026-10-18). This plugin replaces
//! bevy_audio's playback (main.rs disables `AudioPlugin`) and keeps its components, so everything that plays sounds
//! is unchanged: spawn an `AudioPlayer` with `PlaybackSettings`; an `AudioSink` / `SpatialAudioSink` appears once
//! the file has loaded; `SpatialListener`, `GlobalVolume` and `DefaultSpatialScale` work as in Bevy.
//!
//! Following the device:
//! - a watcher thread ([`DeviceFollower`]) checks the default output device once a second and listens for the
//!   stream's "device lost" error; when the default changes, the device goes away or comes back, it opens the new
//!   default and hands it to the game. Opening, polling and closing streams never run on the game's thread, so a
//!   slow or vanished device cannot stall a frame.
//! - the game then moves every playing sound onto the new device: each is re-created at its current place in its
//!   file ([`PlayHead`]: music, ambient loops and speech carry on where they were, never `try_seek`, which blocks
//!   on the audio thread), with its volume, speed, pause and mute.
//! - with no device at all, sounds play into a silent mixer (their sinks exist, so loops keep their channels and
//!   the code reading sinks sees them), and move to the first device that appears.
//!
//! Differences from bevy_audio's playback (all remaster additions): a file that fails to decode is logged and
//! skipped instead of panicking; a loop resumed in the middle wraps to the start of its loop region.

use std::any::Any;
use std::io::Cursor;
use std::num::NonZero;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use bevy::audio::{
    AudioLoader, AudioPlayer, AudioSink, AudioSinkPlayback, AudioSource, DefaultSpatialScale, GlobalVolume,
    PlaybackMode, PlaybackSettings, SpatialAudioSink, SpatialListener,
    SpatialScale, Volume,
};
use bevy::prelude::*;
use bevy::transform::TransformSystems;
use rodio::mixer::Mixer;
use rodio::{Player, Source, SpatialPlayer};

/// How often the watcher looks at the system's default output device.
const POLL: Duration = Duration::from_secs(1);

/// Where sounds are played.
pub struct AudioOutputPlugin {
    pub global_volume: GlobalVolume,
    pub default_spatial_scale: SpatialScale,
    pub devices: DeviceSource,
}

/// Which output devices the plugin follows.
pub enum DeviceSource {
    /// The system's default output device (cpal), watched by a background thread.
    System,
    /// Outputs handed in through the [`OutputSwitches`] resource (tests); starts silent.
    #[cfg_attr(not(test), allow(dead_code))]
    Manual,
}

/// The playback systems, after transform propagation (spatial sounds read their emitters' global transforms).
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct AudioOutputSystems;

impl Plugin for AudioOutputPlugin {
    fn build(&self, app: &mut App) {
        let (to_game, from_watcher) = std::sync::mpsc::channel::<Switch>();
        let (retire, retired) = std::sync::mpsc::channel::<Retired>();
        let mut output = AudioOutput::silent();
        match self.devices {
            DeviceSource::System => {
                // the first device is opened before the first frame, as bevy_audio did; later ones on the watcher
                let mut follower = DeviceFollower::new(SystemDevices);
                if let Some(switch) = follower.poll() {
                    output.apply(switch);
                }
                watch(follower, to_game.clone(), retired);
            }
            DeviceSource::Manual => {
                // retired streams are simply dropped (no thread)
                drop(retired);
            }
        }
        app.insert_resource(self.global_volume)
            .insert_resource(DefaultSpatialScale(self.default_spatial_scale))
            .insert_resource(output)
            .insert_resource(OutputLink { switches: Mutex::new(from_watcher), retire })
            .insert_resource(OutputSwitches(to_game))
            .init_asset::<AudioSource>()
            .init_asset_loader::<AudioLoader>()
            .configure_sets(PostUpdate, AudioOutputSystems.after(TransformSystems::Propagate))
            .add_systems(
                PostUpdate,
                (advance_playheads, follow_device, play_queued, cleanup_finished, update_emitter_positions, update_listener_positions)
                    .chain()
                    .in_set(AudioOutputSystems),
            );
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Devices

/// An open output: the mixer sounds connect to, and the stream that plays it (dropped to close the device).
pub struct OpenOutput {
    pub mixer: Mixer,
    pub stream: Box<dyn Any + Send>,
}

/// The output devices: the system's (cpal) or a test double.
pub trait OutputDevices: Send + 'static {
    /// The id of the current default output device; None when there is none.
    fn default_device(&mut self) -> Option<String>;
    /// Opens the device with this id. The stream sets `lost` when the device goes away.
    fn open(&mut self, id: &str, lost: Arc<AtomicBool>) -> Result<OpenOutput, String>;
}

/// A change of output, from the watcher to the game.
pub enum Switch {
    /// Play on this device from now on.
    To { device: String, output: OpenOutput },
    /// No device left: play silently until one appears.
    Gone,
}

/// Decides when to (re)open the output: the default device changed, its stream reported the device lost, or a
/// device appeared after none. Called by the watcher thread every [`POLL`] (by tests directly).
pub struct DeviceFollower<D: OutputDevices> {
    devices: D,
    /// the device the game plays on
    current: Option<String>,
    /// set by the current stream's error callback when its device goes away
    lost: Arc<AtomicBool>,
    /// the last device that failed to open (logged once, retried every poll)
    failed: Option<String>,
}

impl<D: OutputDevices> DeviceFollower<D> {
    pub fn new(devices: D) -> Self {
        Self { devices, current: None, lost: Arc::new(AtomicBool::new(false)), failed: None }
    }

    /// The output to switch to, if the game should switch.
    pub fn poll(&mut self) -> Option<Switch> {
        let lost = self.lost.load(Ordering::Relaxed);
        let want = self.devices.default_device();
        if want == self.current && !lost {
            return None;
        }
        let Some(id) = want else {
            // no device: drop the dead one (if any) and wait
            return self.current.take().map(|_| Switch::Gone);
        };
        let flag = Arc::new(AtomicBool::new(false));
        match self.devices.open(&id, flag.clone()) {
            Ok(output) => {
                info!("audio: output device {id}");
                self.current = Some(id.clone());
                self.lost = flag;
                self.failed = None;
                Some(Switch::To { device: id, output })
            }
            Err(e) => {
                if self.failed.as_deref() != Some(id.as_str()) {
                    warn!("audio: could not open output device {id}: {e} (retrying)");
                    self.failed = Some(id);
                }
                // a lost stream plays nothing: let it go; a working one keeps playing until the new device opens
                if lost { self.current.take().map(|_| Switch::Gone) } else { None }
            }
        }
    }
}

/// The system's output devices through cpal (WASAPI on Windows).
struct SystemDevices;

impl OutputDevices for SystemDevices {
    fn default_device(&mut self) -> Option<String> {
        use rodio::cpal::traits::{DeviceTrait, HostTrait};
        let device = rodio::cpal::default_host().default_output_device()?;
        Some(device.id().map(|id| id.1).unwrap_or_else(|_| "default".to_owned()))
    }

    fn open(&mut self, id: &str, lost: Arc<AtomicBool>) -> Result<OpenOutput, String> {
        use rodio::cpal::traits::{DeviceTrait, HostTrait};
        let host = rodio::cpal::default_host();
        let matches = |d: &rodio::cpal::Device| d.id().map(|i| i.1 == id).unwrap_or(false);
        let device = host
            .default_output_device()
            .filter(|d| matches(d))
            .or_else(|| host.output_devices().ok()?.find(|d| matches(d)))
            .or_else(|| host.default_output_device())
            .ok_or("no output device")?;
        let on_error = move |e: rodio::cpal::StreamError| match e {
            rodio::cpal::StreamError::DeviceNotAvailable | rodio::cpal::StreamError::StreamInvalidated => {
                lost.store(true, Ordering::Relaxed)
            }
            rodio::cpal::StreamError::BufferUnderrun => {}
            e => warn!("audio: stream error: {e}"),
        };
        let mut stream = rodio::DeviceSinkBuilder::from_device(device)
            .map_err(|e| e.to_string())?
            .with_error_callback(on_error)
            .open_sink_or_fallback()
            .map_err(|e| e.to_string())?;
        stream.log_on_drop(false);
        Ok(OpenOutput { mixer: stream.mixer().clone(), stream: Box::new(stream) })
    }
}

/// Something to drop off the game's thread (a closed stream joins its audio thread).
type Retired = Box<dyn Any + Send>;

/// The watcher thread: polls the follower and drops retired streams. Ends when the game is gone.
fn watch<D: OutputDevices>(mut follower: DeviceFollower<D>, to_game: Sender<Switch>, retired: Receiver<Retired>) {
    let spawned = std::thread::Builder::new().name("audio-device-watch".into()).spawn(move || {
        loop {
            match retired.recv_timeout(POLL) {
                Ok(old) => drop(old),
                Err(RecvTimeoutError::Timeout) => {
                    if let Some(switch) = follower.poll() {
                        if to_game.send(switch).is_err() {
                            return;
                        }
                    }
                }
                Err(RecvTimeoutError::Disconnected) => return,
            }
        }
    });
    if let Err(e) = spawned {
        warn!("audio: no device watcher ({e}): the output stays on the first device");
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The game's side

/// The output sounds play on.
#[derive(Resource)]
pub struct AudioOutput {
    mixer: Mixer,
    /// the device's id; None: silent (no device)
    device: Option<String>,
    /// the open stream (or the silent mixer's unread output)
    stream: Mutex<Option<Box<dyn Any + Send>>>,
    /// how many times the output changed
    pub switches: u32,
}

impl AudioOutput {
    /// A mixer nobody plays: sounds advance in game time and wait for a device.
    fn silent() -> Self {
        let (mixer, source) = rodio::mixer::mixer(NonZero::new(2).unwrap(), NonZero::new(44_100).unwrap());
        Self { mixer, device: None, stream: Mutex::new(Some(Box::new(source))), switches: 0 }
    }

    /// Takes the new output; returns the old stream (to drop off the game's thread).
    fn apply(&mut self, switch: Switch) -> Option<Retired> {
        let (mixer, device, stream): (Mixer, Option<String>, Box<dyn Any + Send>) = match switch {
            Switch::To { device, output } => (output.mixer, Some(device), output.stream),
            Switch::Gone => {
                let s = Self::silent();
                (s.mixer, None, s.stream.into_inner().ok().flatten().expect("silent mixer"))
            }
        };
        self.mixer = mixer;
        self.device = device;
        self.switches += 1;
        self.stream.get_mut().ok()?.replace(stream)
    }

    /// The device sounds play on; None while silent.
    pub fn device(&self) -> Option<&str> {
        self.device.as_deref()
    }
}

/// Where outputs come from: the watcher thread's channel, and where the old streams go.
#[derive(Resource)]
struct OutputLink {
    switches: Mutex<Receiver<Switch>>,
    retire: Sender<Retired>,
}

/// Hands the game a new output (the watcher's channel; tests send their own).
#[cfg_attr(not(test), allow(dead_code))]
#[derive(Resource, Clone)]
pub struct OutputSwitches(pub Sender<Switch>);

/// A sound's place in its file, kept in game time (Time<Real>) so it can be re-created elsewhere: the sink's own
/// position counts from where it was made and stalls with a dead device.
#[derive(Component, Debug, Clone, Copy)]
pub struct PlayHead {
    /// where in the file the sink started (seconds)
    from: f64,
    /// file seconds played since (wall time x speed while not paused)
    played: f64,
    /// a loop's region in the file: start, length (None: not a loop, or its length is unknown)
    looped: Option<(f64, f64)>,
}

impl PlayHead {
    /// The place in the file now.
    pub fn position(&self) -> Duration {
        let at = self.from + self.played;
        let secs = match self.looped {
            Some((start, len)) if len > 0.0 => start + (at - start).max(0.0).rem_euclid(len),
            _ => at,
        };
        Duration::from_secs_f64(secs.max(0.0))
    }
}

/// `PlaybackMode::Despawn`: the entity goes when its sound has played (bevy_audio's marker is private).
#[derive(Component, Default)]
pub struct DespawnWhenDone;

/// `PlaybackMode::Remove`: the sound's components go when it has played.
#[derive(Component, Default)]
pub struct RemoveWhenDone;

/// Play this sound from here (a sound moved to a new device); its settings still say how it loops and ends.
#[derive(Component, Debug, Clone, Copy)]
pub struct ResumeAt(pub Duration);

fn advance_playheads(
    time: Res<Time<Real>>,
    mut heads: Query<(&mut PlayHead, AnyOf<(&AudioSink, &SpatialAudioSink)>)>,
) {
    let dt = time.delta_secs_f64();
    for (mut head, (sink, spatial)) in &mut heads {
        let s: &dyn AudioSinkPlayback = match (sink, spatial) {
            (Some(s), _) => s,
            (None, Some(s)) => s,
            (None, None) => continue,
        };
        if !s.is_paused() {
            head.played += dt * s.speed() as f64;
        }
    }
}

/// A new output from the watcher: every playing sound moves to it at its place, with its volume, speed, pause
/// and mute (re-created by `play_queued` in the same frame).
fn follow_device(
    mut output: ResMut<AudioOutput>,
    link: Res<OutputLink>,
    global: Res<GlobalVolume>,
    mut playing: Query<(Entity, &PlayHead, &mut PlaybackSettings, AnyOf<(&AudioSink, &SpatialAudioSink)>), With<AudioPlayer>>,
    mut commands: Commands,
) {
    let Some(switch) = link.switches.lock().ok().and_then(|rx| {
        // only the newest output matters
        let mut last = None;
        while let Ok(s) = rx.try_recv() {
            last = Some(s);
        }
        last
    }) else {
        return;
    };
    if let Some(old) = output.apply(switch) {
        // dropped on the watcher thread when there is one (else here: a test's silent mixer)
        let _ = link.retire.send(old);
    }
    let mut moved = 0;
    for (e, head, mut settings, (sink, spatial)) in &mut playing {
        let s: &dyn AudioSinkPlayback = match (sink, spatial) {
            (Some(s), _) => s,
            (None, Some(s)) => s,
            (None, None) => continue,
        };
        // a one-shot that has finished stays finished
        if s.empty() {
            continue;
        }
        let g = global.volume.to_linear();
        settings.volume = Volume::Linear(if g > 0.0 { s.volume().to_linear() / g } else { s.volume().to_linear() });
        settings.speed = s.speed();
        settings.paused = s.is_paused();
        settings.muted = s.is_muted();
        commands
            .entity(e)
            .insert(ResumeAt(head.position()))
            .remove::<(AudioSink, SpatialAudioSink, PlayHead, DespawnWhenDone, RemoveWhenDone)>();
        moved += 1;
    }
    info!("audio: output now {}, {moved} sounds moved", output.device().unwrap_or("silent (no device)"));
}

/// Starts the sounds whose files have loaded (bevy_audio's play_queued_audio_system, on [`AudioOutput`]).
#[allow(clippy::type_complexity)]
fn play_queued(
    output: Res<AudioOutput>,
    sources: Res<Assets<AudioSource>>,
    global: Res<GlobalVolume>,
    queued: Query<
        (Entity, &AudioPlayer, &PlaybackSettings, Option<&GlobalTransform>, Option<&ResumeAt>),
        (Without<AudioSink>, Without<SpatialAudioSink>),
    >,
    listeners: Query<(&GlobalTransform, &SpatialListener)>,
    default_scale: Res<DefaultSpatialScale>,
    mut commands: Commands,
) {
    for (e, player, settings, transform, resume) in &queued {
        let Some(file) = sources.get(&player.0) else { continue };
        let (source, head) = match build_source(file, settings, resume.map(|r| r.0)) {
            Ok(built) => built,
            Err(err) => {
                warn!("audio: cannot decode {:?}: {err}", player.0.path());
                commands.entity(e).remove::<(AudioPlayer, PlaybackSettings, ResumeAt)>();
                continue;
            }
        };
        let mut entity = commands.entity(e);
        entity.remove::<ResumeAt>();
        if settings.spatial {
            let (left, right) = ears(&listeners);
            let scale = settings.spatial_scale.unwrap_or(default_scale.0).0;
            let at = transform.map(|t| t.translation()).unwrap_or(Vec3::ZERO) * scale;
            let p = SpatialPlayer::connect_new(&output.mixer, at.into(), (left * scale).into(), (right * scale).into());
            p.append(source);
            let mut sink = SpatialAudioSink::new(p);
            start(&mut sink, settings, &global);
            insert_sink(&mut entity, sink, settings.mode, head);
        } else {
            let p = Player::connect_new(&output.mixer);
            p.append(source);
            let mut sink = AudioSink::new(p);
            start(&mut sink, settings, &global);
            insert_sink(&mut entity, sink, settings.mode, head);
        }
    }
}

fn start(sink: &mut dyn AudioSinkPlayback, settings: &PlaybackSettings, global: &GlobalVolume) {
    if settings.muted {
        sink.mute();
    }
    sink.set_speed(settings.speed);
    sink.set_volume(settings.volume * global.volume);
    if settings.paused {
        sink.pause();
    }
}

fn insert_sink(entity: &mut EntityCommands, sink: impl Component, mode: PlaybackMode, head: PlayHead) {
    match mode {
        PlaybackMode::Loop | PlaybackMode::Once => entity.insert((sink, head)),
        PlaybackMode::Despawn => entity.insert((sink, head, DespawnWhenDone)),
        PlaybackMode::Remove => entity.insert((sink, head, RemoveWhenDone)),
    };
}

/// The first listener's ears (Bevy's default ears without one).
fn ears(listeners: &Query<(&GlobalTransform, &SpatialListener)>) -> (Vec3, Vec3) {
    match listeners.iter().next() {
        Some((t, l)) => (t.transform_point(l.left_ear_offset), t.transform_point(l.right_ear_offset)),
        None => {
            let l = SpatialListener::default();
            (l.left_ear_offset, l.right_ear_offset)
        }
    }
}

type BoxedSource = Box<dyn Source + Send>;

fn decoder(file: &AudioSource) -> Result<rodio::Decoder<Cursor<AudioSource>>, rodio::decoder::DecoderError> {
    rodio::Decoder::builder().with_byte_len(file.bytes.len() as u64).with_data(Cursor::new(file.clone())).build()
}

/// The source to play for these settings, from `resume` (else the settings' start), and its play head.
/// As bevy_audio: `start_position` skips into the file, `duration` limits it, a loop repeats that region; a loop
/// resumed in the middle plays the rest of the region, then repeats the whole region.
fn build_source(
    file: &AudioSource,
    settings: &PlaybackSettings,
    resume: Option<Duration>,
) -> Result<(BoxedSource, PlayHead), rodio::decoder::DecoderError> {
    let first = decoder(file)?;
    let clip = first.total_duration();
    let start = settings.start_position.unwrap_or_default();
    let region = settings.duration.or_else(|| clip.map(|c| c.saturating_sub(start)));
    let from = resume.unwrap_or(start).max(start);
    let head = |looped| PlayHead { from: from.as_secs_f64(), played: 0.0, looped };
    match settings.mode {
        PlaybackMode::Loop => {
            let looped = region.map(|r| (start.as_secs_f64(), r.as_secs_f64()));
            let repeat = |d: rodio::Decoder<Cursor<AudioSource>>| -> BoxedSource {
                match settings.duration {
                    Some(len) => Box::new(d.skip_duration(start).take_duration(len).repeat_infinite()),
                    None => Box::new(d.skip_duration(start).repeat_infinite()),
                }
            };
            if from == start {
                return Ok((repeat(first), head(looped)));
            }
            let rest: BoxedSource = match region {
                Some(r) => Box::new(first.skip_duration(from).take_duration((start + r).saturating_sub(from))),
                None => Box::new(first.skip_duration(from)),
            };
            let whole = repeat(decoder(file)?);
            Ok((Box::new(rodio::source::from_iter([rest, whole])), head(looped)))
        }
        PlaybackMode::Once | PlaybackMode::Despawn | PlaybackMode::Remove => {
            let s: BoxedSource = match settings.duration {
                Some(len) => Box::new(first.skip_duration(from).take_duration((start + len).saturating_sub(from))),
                None => Box::new(first.skip_duration(from)),
            };
            Ok((s, head(None)))
        }
    }
}

/// Despawns / strips finished sounds (bevy_audio's cleanup_finished_audio).
#[allow(clippy::type_complexity)]
fn cleanup_finished(
    mut commands: Commands,
    despawn: Query<(Entity, AnyOf<(&AudioSink, &SpatialAudioSink)>), With<DespawnWhenDone>>,
    remove: Query<(Entity, AnyOf<(&AudioSink, &SpatialAudioSink)>), With<RemoveWhenDone>>,
) {
    let done = |(s, sp): (Option<&AudioSink>, Option<&SpatialAudioSink>)| s.is_some_and(|s| s.empty()) || sp.is_some_and(|s| s.empty());
    for (e, sinks) in &despawn {
        if done(sinks) {
            commands.entity(e).despawn();
        }
    }
    for (e, sinks) in &remove {
        if done(sinks) {
            commands
                .entity(e)
                .remove::<(AudioPlayer, AudioSink, SpatialAudioSink, PlaybackSettings, RemoveWhenDone, PlayHead)>();
        }
    }
}

/// Spatial sounds follow their emitters (bevy_audio's update_emitter_positions).
#[allow(clippy::type_complexity)]
fn update_emitter_positions(
    emitters: Query<(&GlobalTransform, &SpatialAudioSink, &PlaybackSettings), Or<(Changed<GlobalTransform>, Changed<PlaybackSettings>)>>,
    default_scale: Res<DefaultSpatialScale>,
) {
    for (t, sink, settings) in &emitters {
        let scale = settings.spatial_scale.unwrap_or(default_scale.0).0;
        sink.set_emitter_position(t.translation() * scale);
    }
}

/// Spatial sounds hear from the listener (bevy_audio's update_listener_positions).
#[allow(clippy::type_complexity)]
fn update_listener_positions(
    emitters: Query<(&SpatialAudioSink, &PlaybackSettings)>,
    changed: Query<(), (Or<(Changed<SpatialListener>, Changed<GlobalTransform>)>, With<SpatialListener>)>,
    added: Query<(), Added<SpatialAudioSink>>,
    listeners: Query<(&GlobalTransform, &SpatialListener)>,
    default_scale: Res<DefaultSpatialScale>,
) {
    if !default_scale.is_changed() && changed.is_empty() && added.is_empty() {
        return;
    }
    let (left, right) = ears(&listeners);
    for (sink, settings) in &emitters {
        let scale = settings.spatial_scale.unwrap_or(default_scale.0).0;
        sink.set_ears_position(left * scale, right * scale);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Test devices: the default is whatever the test says; opening gives a silent mixer.
    #[derive(Clone, Default)]
    struct FakeDevices {
        state: Arc<Mutex<FakeState>>,
    }

    #[derive(Default)]
    struct FakeState {
        default: Option<String>,
        broken: Vec<String>,
        opened: Vec<String>,
        lost: Option<Arc<AtomicBool>>,
    }

    impl OutputDevices for FakeDevices {
        fn default_device(&mut self) -> Option<String> {
            self.state.lock().unwrap().default.clone()
        }
        fn open(&mut self, id: &str, lost: Arc<AtomicBool>) -> Result<OpenOutput, String> {
            let mut s = self.state.lock().unwrap();
            if s.broken.iter().any(|b| b == id) {
                return Err("broken".into());
            }
            s.opened.push(id.to_owned());
            s.lost = Some(lost);
            let (mixer, source) = rodio::mixer::mixer(NonZero::new(2).unwrap(), NonZero::new(44_100).unwrap());
            Ok(OpenOutput { mixer, stream: Box::new(source) })
        }
    }

    impl FakeDevices {
        fn set_default(&self, id: Option<&str>) {
            self.state.lock().unwrap().default = id.map(str::to_owned);
        }
        fn lose_current(&self) {
            self.state.lock().unwrap().lost.as_ref().unwrap().store(true, Ordering::Relaxed);
        }
        fn opened(&self) -> Vec<String> {
            self.state.lock().unwrap().opened.clone()
        }
    }

    fn device(s: Option<Switch>) -> Option<Option<String>> {
        s.map(|s| match s {
            Switch::To { device, .. } => Some(device),
            Switch::Gone => None,
        })
    }

    #[test]
    fn follower_follows_the_default_device() {
        let fake = FakeDevices::default();
        let mut f = DeviceFollower::new(fake.clone());
        // no device: nothing to do
        assert!(f.poll().is_none());
        fake.set_default(Some("speakers"));
        assert_eq!(device(f.poll()), Some(Some("speakers".into())));
        assert!(f.poll().is_none(), "same device: no reopen");
        // the user switches the output in Windows
        fake.set_default(Some("headset"));
        assert_eq!(device(f.poll()), Some(Some("headset".into())));
        // the headset is unplugged: its stream reports the loss, Windows falls back to the speakers
        fake.lose_current();
        fake.set_default(Some("speakers"));
        assert_eq!(device(f.poll()), Some(Some("speakers".into())));
        // the device disappears and Windows has none for a while
        fake.lose_current();
        fake.set_default(None);
        assert_eq!(device(f.poll()), Some(None));
        assert!(f.poll().is_none());
        // a device comes back
        fake.set_default(Some("speakers"));
        assert_eq!(device(f.poll()), Some(Some("speakers".into())));
        assert_eq!(fake.opened(), ["speakers", "headset", "speakers", "speakers"]);
    }

    #[test]
    fn follower_keeps_a_working_device_when_the_new_one_fails() {
        let fake = FakeDevices::default();
        let mut f = DeviceFollower::new(fake.clone());
        fake.set_default(Some("speakers"));
        assert!(f.poll().is_some());
        fake.state.lock().unwrap().broken.push("hdmi".into());
        fake.set_default(Some("hdmi"));
        assert!(f.poll().is_none(), "speakers keep playing while hdmi will not open");
        // the same device lost and not reopenable: silent until something opens
        fake.lose_current();
        fake.set_default(Some("speakers"));
        fake.state.lock().unwrap().broken.push("speakers".into());
        assert_eq!(device(f.poll()), Some(None));
        fake.state.lock().unwrap().broken.clear();
        assert_eq!(device(f.poll()), Some(Some("speakers".into())));
    }

    /// A 16-bit mono WAV of `secs` seconds of silence.
    fn wav(secs: f32) -> AudioSource {
        let rate = 8000u32;
        let n = (rate as f32 * secs) as u32;
        let mut b = Vec::new();
        b.extend_from_slice(b"RIFF");
        b.extend_from_slice(&(36 + n * 2).to_le_bytes());
        b.extend_from_slice(b"WAVEfmt ");
        b.extend_from_slice(&16u32.to_le_bytes());
        b.extend_from_slice(&1u16.to_le_bytes());
        b.extend_from_slice(&1u16.to_le_bytes());
        b.extend_from_slice(&rate.to_le_bytes());
        b.extend_from_slice(&(rate * 2).to_le_bytes());
        b.extend_from_slice(&2u16.to_le_bytes());
        b.extend_from_slice(&16u16.to_le_bytes());
        b.extend_from_slice(b"data");
        b.extend_from_slice(&(n * 2).to_le_bytes());
        b.resize(b.len() + n as usize * 2, 0);
        AudioSource { bytes: b.into() }
    }

    fn app() -> App {
        let mut app = App::new();
        app.add_plugins((MinimalPlugins, AssetPlugin::default(), TransformPlugin));
        app.add_plugins(AudioOutputPlugin {
            global_volume: GlobalVolume::default(),
            default_spatial_scale: SpatialScale::new(0.002),
            devices: DeviceSource::Manual,
        });
        // the play heads advance by Time<Real>: step it by hand
        app.insert_resource(bevy::time::TimeUpdateStrategy::ManualDuration(Duration::from_millis(100)));
        app
    }

    fn head(app: &mut App, e: Entity) -> f64 {
        app.world().get::<PlayHead>(e).expect("playing").position().as_secs_f64()
    }

    #[test]
    fn sounds_move_to_a_new_device_at_their_positions() {
        let mut app = app();
        let (music, ambient, voice, paused) = {
            let mut files = app.world_mut().resource_mut::<Assets<AudioSource>>();
            (files.add(wav(10.0)), files.add(wav(2.0)), files.add(wav(3.0)), files.add(wav(3.0)))
        };
        let w = app.world_mut();
        let music = w.spawn((AudioPlayer(music), PlaybackSettings::LOOP)).id();
        let ambient = w
            .spawn((AudioPlayer(ambient), PlaybackSettings { spatial: true, ..PlaybackSettings::LOOP }, Transform::default()))
            .id();
        // a voice that loaded late starts 0.5 s in (audio.rs start_position)
        let voice = w
            .spawn((AudioPlayer(voice), PlaybackSettings { start_position: Some(Duration::from_millis(500)), ..PlaybackSettings::ONCE }))
            .id();
        let paused = w.spawn((AudioPlayer(paused), PlaybackSettings { paused: true, ..PlaybackSettings::ONCE })).id();
        app.update();
        app.update();
        assert!(app.world().get::<AudioSink>(music).is_some());
        assert!(app.world().get::<SpatialAudioSink>(ambient).is_some());
        for _ in 0..25 {
            app.update();
        }
        let before = [head(&mut app, music), head(&mut app, ambient), head(&mut app, voice), head(&mut app, paused)];
        app.world_mut().get_mut::<AudioSink>(music).unwrap().set_volume(Volume::Linear(0.25));
        // all started on the same frame: the ambient loop has wrapped (2 s file); the voice runs from its start offset
        assert!(before[0] > 2.0 && before[0] < 3.0, "{before:?}");
        assert!((before[1] - before[0].rem_euclid(2.0)).abs() < 1e-6, "{before:?}");
        assert!((before[2] - (before[0] + 0.5)).abs() < 1e-6, "{before:?}");
        assert_eq!(before[3], 0.0);

        // the device changes
        let (mixer, source) = rodio::mixer::mixer(NonZero::new(2).unwrap(), NonZero::new(48_000).unwrap());
        let tx = app.world().resource::<OutputSwitches>().0.clone();
        tx.send(Switch::To { device: "headset".into(), output: OpenOutput { mixer, stream: Box::new(source) } }).unwrap();
        app.update();
        assert_eq!(app.world().resource::<AudioOutput>().device(), Some("headset"));
        assert_eq!(app.world().resource::<AudioOutput>().switches, 1);
        // every sound has a new sink, made at its place (one 0.1 s frame on top; the paused one did not move)
        let expect = [before[0] + 0.1, (before[1] + 0.1).rem_euclid(2.0), before[2] + 0.1, 0.0];
        for (e, want) in [music, ambient, voice, paused].into_iter().zip(expect) {
            let s = app.world().get::<PlayHead>(e).expect("re-created");
            assert!((s.from - want).abs() < 1e-3 && s.played == 0.0, "{e}: {} vs {want}", s.from);
            assert!(app.world().get::<ResumeAt>(e).is_none());
        }
        assert!(app.world().get::<AudioSink>(music).is_some());
        assert!(app.world().get::<SpatialAudioSink>(ambient).is_some());
        let m = app.world().get::<AudioSink>(music).unwrap();
        assert!((m.volume().to_linear() - 0.25).abs() < 1e-6, "volume carried over");
        assert!(app.world().get::<AudioSink>(paused).unwrap().is_paused(), "pause carried over");
        assert!(!app.world().get::<AudioSink>(voice).unwrap().is_paused());

        // the device goes away: silent, sounds still there and still moving in time
        tx.send(Switch::Gone).unwrap();
        app.update();
        app.update();
        assert_eq!(app.world().resource::<AudioOutput>().device(), None);
        assert!(app.world().get::<AudioSink>(music).is_some());
        assert!(head(&mut app, music) > before[0] + 0.15);
    }

    #[test]
    fn a_resumed_loop_wraps_to_its_region() {
        let file = wav(2.0);
        let settings = PlaybackSettings::LOOP;
        let (_, h) = build_source(&file, &settings, Some(Duration::from_millis(1500))).unwrap();
        let mut h = h;
        h.played = 1.0;
        assert!((h.position().as_secs_f64() - 0.5).abs() < 1e-3);
        // a region: start 0.5 s, 1 s long
        let region = PlaybackSettings { start_position: Some(Duration::from_millis(500)), duration: Some(Duration::from_secs(1)), ..PlaybackSettings::LOOP };
        let (src, mut h) = build_source(&file, &region, Some(Duration::from_millis(1200))).unwrap();
        h.played = 0.5;
        assert!((h.position().as_secs_f64() - 0.7).abs() < 1e-3);
        // the rest of the region (0.3 s) then the region again: 1.3 s of samples before the second repeat ends
        let samples: usize = src.take(8000 * 13 / 10 + 8000).count();
        assert_eq!(samples, 8000 * 13 / 10 + 8000);
    }
}
