//! Sounds by the game's names (audio/sounds.json from tools/remaster_export_audio.py): one-shots and loops,
//! positional ones attenuated by distance between their min and max distance (game units) and panned by Bevy's
//! spatial audio. Game-neutral: a game supplies the bank and calls PlaySound.

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
    #[serde(default = "one")]
    pub volume: f32,
    #[serde(default)]
    pub min_distance: Option<f32>,
    #[serde(default)]
    pub max_distance: Option<f32>,
    #[serde(default)]
    pub category: Option<String>,
}

fn one() -> f32 {
    1.0
}

#[derive(Resource, Default)]
pub struct SoundBank {
    pub sounds: HashMap<String, SoundDef>,
    handles: HashMap<String, Handle<AudioSource>>,
    pub played: usize,
    pub missing: std::collections::HashSet<String>,
}

impl SoundBank {
    pub fn load(dir: &Path) -> Self {
        let sounds = std::fs::read_to_string(dir.join("audio").join("sounds.json"))
            .ok()
            .and_then(|t| serde_json::from_str::<HashMap<String, SoundDef>>(&t).ok())
            .map(|m| m.into_iter().map(|(k, v)| (k.to_lowercase(), v)).collect())
            .unwrap_or_default();
        SoundBank { sounds, ..default() }
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
}

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
            .insert_resource(Mixer { master: 1.0, sfx: 1.0, voice: 1.0, music: 0.8 })
            .add_systems(Update, (play, attenuate).chain());
    }
}

fn play(
    mut commands: Commands,
    mut events: MessageReader<PlaySound>,
    bank: Option<ResMut<SoundBank>>,
    assets: Res<AssetServer>,
    mixer: Res<Mixer>,
) {
    let Some(mut bank) = bank else { return };
    for ev in events.read() {
        let key = ev.name.to_lowercase();
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
        let base = def.volume * ev.volume * cat * mixer.master;
        let settings = PlaybackSettings {
            mode: if looped { PlaybackMode::Loop } else { PlaybackMode::Despawn },
            volume: Volume::Linear(base),
            spatial: ev.at.is_some() || ev.follow.is_some(),
            ..default()
        };
        let mut e = commands.spawn((AudioPlayer(handle), settings, Transform::from_translation(ev.at.unwrap_or_default())));
        if ev.at.is_some() || ev.follow.is_some() {
            e.insert(Positional {
                base,
                min: def.min_distance.unwrap_or(20.0),
                max: def.max_distance.unwrap_or(400.0),
                follow: ev.follow,
            });
        }
        bank.played += 1;
    }
}

/// Linear fall-off between min and max distance (guess until docs/subsystems/audio.md says the game's model).
fn attenuate(
    listener: Query<&GlobalTransform, With<SpatialListener>>,
    mut q: Query<(&Positional, &mut Transform, Option<&mut AudioSink>)>,
    followed: Query<&GlobalTransform, Without<Positional>>,
) {
    let Ok(l) = listener.single() else { return };
    for (p, mut t, sink) in &mut q {
        if let Some(f) = p.follow.and_then(|e| followed.get(e).ok()) {
            t.translation = f.translation();
        }
        let d = t.translation.distance(l.translation());
        let g = if d <= p.min { 1.0 } else if d >= p.max { 0.0 } else { 1.0 - (d - p.min) / (p.max - p.min) };
        if let Some(mut s) = sink {
            s.set_volume(Volume::Linear(p.base * g));
        }
    }
}
