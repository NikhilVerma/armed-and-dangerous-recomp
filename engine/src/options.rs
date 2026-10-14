//! The options the menus edit: the Game object's option block (docs/subsystems/menus.md 1, options.opt), kept in
//! options.json next to the executable. Defaults are the original's (0x00401a80): volumes 0.8, brightness 1.5, draw
//! distance 1.0, shadows on, mouse sensitivity 0.5; both subtitle options on (guess: 0x00447630 picks the default
//! by language). Applied to the engine whenever they change: the mixer, the subtitle line, the shadow-casting sun,
//! camera exposure (brightness), the far plane (draw distance), the window size, mouse look (player.rs).

use bevy::prelude::*;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Resource, Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct GameOptions {
    pub sound_volume: f32,
    pub voice_volume: f32,
    pub music_volume: f32,
    pub subtitles_movie: bool,
    pub subtitles_game: bool,
    pub brightness: f32,
    pub draw_distance: f32,
    pub shadows: bool,
    /// window size; (0, 0) = the size the game was started with (windowed only)
    pub resolution: (u32, u32),
    /// borderless fullscreen on the current monitor at its own resolution (remaster; on by default, F11 / Alt+Enter)
    pub fullscreen: bool,
    pub mouse_sensitivity: f32,
    pub invert_mouse: bool,
    /// Extras tokens found (Game+0x200, saved like the original's options), and the placed tokens already taken
    /// ("level:instance id"), so each is found once
    pub tokens: u32,
    pub tokens_taken: Vec<String>,
    /// the money carried from level to level (Game+0x570; the Stats screen counts it up at a win)
    pub money: f32,
    /// cheats whose code was entered once (Game+0x48c)
    pub cheats_known: Vec<u32>,
}

impl Default for GameOptions {
    fn default() -> Self {
        GameOptions {
            sound_volume: 0.8,
            voice_volume: 0.8,
            music_volume: 0.8,
            subtitles_movie: true,
            subtitles_game: true,
            brightness: 1.5,
            draw_distance: 1.0,
            shadows: true,
            resolution: (0, 0),
            fullscreen: true,
            mouse_sensitivity: 0.5,
            invert_mouse: false,
            tokens: 0,
            tokens_taken: Vec::new(),
            money: 0.0,
            cheats_known: Vec::new(),
        }
    }
}

impl GameOptions {
    /// Scripted runs (--play) use their own file, so the bot's progress (tokens taken, cheats) never reaches the
    /// player's options.
    fn path() -> PathBuf {
        let name = if std::env::args().any(|a| a == "--play") { "options_playtest.json" } else { "options.json" };
        std::env::current_exe().ok().and_then(|p| p.parent().map(|d| d.join(name))).unwrap_or_else(|| PathBuf::from(name))
    }

    pub fn load() -> Self {
        // scripted runs start from the defaults every time and never save (options_playtest.json is not used)
        if std::env::args().any(|a| a == "--play") {
            return Self::default();
        }
        std::fs::read_to_string(Self::path()).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_default()
    }

    pub fn save(&self) {
        if std::env::args().any(|a| a == "--play") {
            return;
        }
        if let Ok(s) = serde_json::to_string_pretty(self) {
            if let Err(e) = std::fs::write(Self::path(), s) {
                warn!("options: not saved: {e}");
            }
        }
    }

    /// Mouse look factor relative to the default (sensitivity 0.5 = 1.0).
    pub fn mouse_factor(&self) -> f32 {
        (self.mouse_sensitivity / 0.5).max(0.05)
    }
}

/// The light whose shadows the "Enable Shadows" option switches (the level's first directional light).
#[derive(Component)]
pub struct ShadowLight;

/// The far plane at draw distance 1.0 as a multiple of the level's fog_far: the original's camera dumps on lev_1_m1
/// give far 1712.132 with fog_far 1500 (camera +0xd4 * +0xd0; how the game computes it is open, docs/subsystems/
/// camera.md): guess, the same ratio on every level. Beyond fog_far everything is the fog colour anyway.
const FAR_OVER_FOG: f32 = 1712.132 / 1500.0;

pub struct OptionsPlugin;

impl Plugin for OptionsPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(GameOptions::load()).add_systems(Update, (toggle_fullscreen, apply).chain());
    }
}

#[allow(clippy::too_many_arguments)]
fn apply(
    mut commands: Commands,
    o: Res<GameOptions>,
    mut mixer: ResMut<crate::audio::Mixer>,
    mut hud: ResMut<crate::hud::HudState>,
    mut lights: Query<&mut DirectionalLight, With<ShadowLight>>,
    cams: Query<(Entity, &Projection), With<Camera3d>>,
    mut windows: Query<&mut Window>,
    mut seen_cams: Local<usize>,
    game_lights: Option<Res<crate::gamematerial::GameLights>>,
    (scripted, mut state): (Option<Res<crate::playtest::Playtest>>, Option<ResMut<crate::playtest::PlayState>>),
) {
    // a camera spawned after the last change (level start) gets the options too
    let n = cams.iter().count();
    if !o.is_changed() && n == *seen_cams {
        return;
    }
    *seen_cams = n;
    // the original's 0.8 defaults are the remaster's tuned levels
    mixer.sfx = o.sound_volume / 0.8;
    mixer.voice = o.voice_volume / 0.8;
    mixer.music = o.music_volume;
    hud.subtitles_off = !o.subtitles_game;
    if !o.subtitles_game {
        hud.subtitle = None;
    }
    for mut l in &mut lights {
        l.shadow_maps_enabled = o.shadows;
    }
    for (e, p) in &cams {
        // brightness: the original's gamma 1.5 is exposure 0; one step of 1.0 is one stop
        commands.entity(e).insert(bevy::render::view::ColorGrading {
            global: bevy::render::view::ColorGradingGlobal { exposure: o.brightness - 1.5, ..default() },
            ..default()
        });
        if let Projection::Perspective(pp) = p {
            let mut pp = pp.clone();
            let fog_far = game_lights.as_ref().and_then(|l| l.fog.map(|f| f.1)).unwrap_or(1500.0);
            // the sky dome (radius 20000) must fit: the game material clips the world at the original far plane itself
            pp.far = (fog_far * FAR_OVER_FOG * o.draw_distance.clamp(0.25, 1.0)).max(50000.0);
            commands.entity(e).insert(Projection::Perspective(pp));
        }
    }
    // scripted (headless) runs stay windowed off-screen; the option is still logged so a script can check it
    let full = o.fullscreen && scripted.is_none();
    for mut w in &mut windows {
        let mode = if full { bevy::window::WindowMode::BorderlessFullscreen(bevy::window::MonitorSelection::Current) } else { bevy::window::WindowMode::Windowed };
        if w.mode != mode {
            w.mode = mode;
        }
        if !full && o.resolution != (0, 0) && (w.resolution.width() as u32, w.resolution.height() as u32) != o.resolution {
            w.resolution.set(o.resolution.0 as f32, o.resolution.1 as f32);
        }
        if let Some(s) = state.as_mut() {
            let size = if !full && o.resolution != (0, 0) { o.resolution } else { (w.resolution.width() as u32, w.resolution.height() as u32) };
            s.0.insert("window".into(), serde_json::json!({ "fullscreen": o.fullscreen, "width": size.0, "height": size.1, "brightness": o.brightness, "shadows": o.shadows, "draw_distance": o.draw_distance }));
        }
    }
}

/// F11 or Alt+Enter: fullscreen on / off, saved at once.
fn toggle_fullscreen(keys: Res<ButtonInput<KeyCode>>, mut o: ResMut<GameOptions>) {
    let alt = keys.any_pressed([KeyCode::AltLeft, KeyCode::AltRight]);
    if keys.just_pressed(KeyCode::F11) || (alt && keys.just_pressed(KeyCode::Enter)) {
        o.fullscreen = !o.fullscreen;
        o.save();
    }
}
