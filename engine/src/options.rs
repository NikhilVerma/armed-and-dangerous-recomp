//! The options the menus edit: the Game object's option block (docs/subsystems/menus.md 1, options.opt), kept in
//! options.json next to the executable. The original registers each option with its default (Options_Register
//! 0x00401a80 through 0x00462160 bool / 0x00462190 int / 0x004621c0 float / 0x004621f0 blob, the registry
//! 0x00462110, reset 0x00462250 and its callback 0x00462220), loads options.opt record by record (0x00462660 ->
//! 0x00462310: a record of the wrong type is skipped with "Option is incorrect type", the others still load) and
//! writes it when changed (0x00401e60 -> 0x00462610 / 0x004625d0 / 0x00462450). Here serde is the registry: the
//! fields and their defaults, loaded key by key the same way (`from_json`), saved whenever a menu changes one.
//! Not kept (the remaster's renderer decides them): enablePostFx, enableBumpMapping, enableDecals,
//! enablePixelShaders, modelLod, textureLod, textureQuality, skinHQ, maxTextures. The input bindings
//! (m_control->m_inputActions) are `bindings` (bindings.rs). Defaults are the original's (0x00401a80): volumes 0.8, brightness 1.5, draw
//! distance 1.0, shadows on, mouse sensitivity 0.5; both subtitle options on (guess: 0x00447630 picks the default
//! by language). Applied to the engine whenever they change: the mixer, the subtitle line, the shadow-casting sun,
//! the far plane (draw distance), mouse look (player.rs); the brightness and the window by display/brightness.rs and
//! display/window.rs.

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
    /// Auto-Aim (Game+0x4c2 m_autoAim, default off; the Controls page): the rifle's and the machine gun's shots go to
    /// the character nearest the crosshair inside a narrow view (weapons/targeting.rs)
    pub auto_aim: bool,
    /// Extras tokens found (Game+0x200, saved like the original's options), and the placed tokens already taken
    /// ("level:instance id", pickup.rs `token_key`), so each is found once
    pub tokens: u32,
    pub tokens_taken: Vec<String>,
    /// `tokens_taken` names the level of each token. Files from before (2026-10-17) keyed every level's tokens as
    /// "lev_1_m1:<id>": `from_json` turns those into "any:<id>" (taken on some level with that id), as they then
    /// counted, so no token is found twice
    pub tokens_keyed_by_level: bool,
    /// the profile's unlock records (Unlock_Add 0x004a07a0): category 1, the levels whose mission start was shown, and
    /// category 0, the story movies seen (extras.rs: the Replay Level and Cutscenes pages)
    pub levels_reached: Vec<String>,
    pub movies_seen: Vec<String>,
    /// the money carried from level to level (Game+0x570; the Stats screen counts it up at a win)
    pub money: f32,
    /// the pub's one-time intro line was heard (Game+0x574, banked with the money by 0x00401ef0 and reset by a new game:
    /// once per campaign, pubscreen.rs)
    pub pub_intro_heard: bool,
    /// cheats whose code was entered once (Game+0x48c)
    pub cheats_known: Vec<u32>,
    /// the original's save rule: the game saves itself at the checkpoints only (the mission start, leaving the pub;
    /// savegame.rs) and the pause menu cannot save. Off: save anywhere (the remaster's)
    pub checkpoint_saves: bool,
    /// remaster: the presentation preset (graphics.rs: Remaster, or Original = the faithful look) and any of its
    /// settings changed by name ({"ambient_occlusion": false, ...}); tool runs ignore both (Original)
    pub graphics_preset: crate::graphics::Preset,
    pub graphics: serde_json::Map<String, serde_json::Value>,
    /// remaster: the developer menu's toggles (devtools: god mode, infinite ammo, the overlays, ...)
    pub dev: crate::devtools::DevSettings,
    /// the key, mouse and pad bindings the Controls page edits (bindings.rs; the original's m_control->m_inputActions)
    pub bindings: crate::bindings::KeyBindings,
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
            auto_aim: false,
            tokens: 0,
            tokens_taken: Vec::new(),
            tokens_keyed_by_level: true,
            levels_reached: Vec::new(),
            movies_seen: Vec::new(),
            money: 0.0,
            pub_intro_heard: false,
            cheats_known: Vec::new(),
            checkpoint_saves: false,
            graphics_preset: crate::graphics::Preset::Remaster,
            graphics: serde_json::Map::new(),
            dev: crate::devtools::DevSettings::default(),
            bindings: crate::bindings::KeyBindings::default(),
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
        std::fs::read_to_string(Self::path()).map(|s| Self::from_json(&s)).unwrap_or_default()
    }

    /// The options in a file, key by key as 0x00462310 reads records: a known key whose value has the wrong type is
    /// skipped (logged), unknown keys are ignored, missing ones keep their defaults; a file that is not a JSON object
    /// gives the defaults.
    pub fn from_json(text: &str) -> Self {
        let Ok(serde_json::Value::Object(file)) = serde_json::from_str::<serde_json::Value>(text) else { return Self::default() };
        let legacy_tokens = !file.contains_key("tokens_keyed_by_level");
        let Ok(serde_json::Value::Object(mut merged)) = serde_json::to_value(Self::default()) else { return Self::default() };
        for (k, v) in file {
            if !merged.contains_key(&k) {
                continue;
            }
            let mut trial = merged.clone();
            trial.insert(k.clone(), v.clone());
            if serde_json::from_value::<Self>(serde_json::Value::Object(trial)).is_ok() {
                merged.insert(k, v);
            } else {
                warn!("options: {k} is of the wrong type, kept its default");
            }
        }
        let mut o: Self = serde_json::from_value(serde_json::Value::Object(merged)).unwrap_or_default();
        if legacy_tokens {
            for k in &mut o.tokens_taken {
                if let Some(id) = k.strip_prefix("lev_1_m1:") {
                    *k = format!("{}:{id}", crate::pickup::ANY_LEVEL);
                }
            }
            o.tokens_keyed_by_level = true;
        }
        o.bindings = o.bindings.normalized();
        o
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

    /// These options with some changed by name, as `from_json` reads a file (a value of the wrong type keeps the
    /// option); an object value (`graphics`) is merged key by key into the option's own. Playtests' "options" steps.
    pub fn with_changes(&self, changes: &serde_json::Map<String, serde_json::Value>) -> Self {
        let Ok(serde_json::Value::Object(mut v)) = serde_json::to_value(self) else { return self.clone() };
        for (k, c) in changes {
            match (v.get_mut(k), c) {
                (Some(serde_json::Value::Object(cur)), serde_json::Value::Object(add)) => cur.extend(add.clone()),
                _ => {
                    v.insert(k.clone(), c.clone());
                }
            }
        }
        Self::from_json(&serde_json::Value::Object(v).to_string())
    }

    /// Mouse look factor relative to the default (sensitivity 0.5 = 1.0).
    pub fn mouse_factor(&self) -> f32 {
        (self.mouse_sensitivity / 0.5).max(0.05)
    }
}

/// The light whose shadows the "Enable Shadows" option switches (the level's first directional light).
#[derive(Component)]
pub struct ShadowLight;

/// The draw distance the camera uses: 0x0040f8c0 (the option Game+0x4b0, applied at level start by Game_StartLevel
/// 0x00403690 and after a load) keeps a value in (0.25, 1.0), takes 1.0 at or above 1.0 ([0x005bcb2c]) and 0.25 at
/// or below 0.25 ([0x005bcbe4]), and scales the fog range by it. (It also takes 1.0 when 0x0044e600 answers true:
/// not traced.)
pub fn draw_distance_scale(v: f32) -> f32 {
    if v > 0.25 { v.min(1.0) } else { 0.25 }
}

/// The far plane at draw distance 1.0 as a multiple of the level's fog_far: the original's camera dumps on lev_1_m1
/// give far 1712.132 with fog_far 1500 (camera +0xd4 * +0xd0; how the game computes it is open, docs/subsystems/
/// camera.md): guess, the same ratio on every level. Beyond fog_far everything is the fog colour anyway.
const FAR_OVER_FOG: f32 = 1712.132 / 1500.0;

/// Where the world ends for the camera: the original's far plane at this draw distance option (the game material
/// clips there; graphics.rs fits the shadow cascades to it).
pub fn world_far(fog_far: f32, draw_distance: f32) -> f32 {
    fog_far * FAR_OVER_FOG * draw_distance_scale(draw_distance)
}

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
    mut seen_cams: crate::levelreset::PerLevel<usize>,
    game_lights: Option<Res<crate::gamematerial::GameLights>>,
) {
    // a camera spawned after the last change (level start) gets the options too
    let n = cams.iter().count();
    let seen_cams = seen_cams.get();
    if !o.is_changed() && n == *seen_cams {
        return;
    }
    *seen_cams = n;
    // the option values; audio.rs turns them into group levels (the original's curve, 0 dB at the 0.8 default)
    mixer.sfx = o.sound_volume;
    mixer.voice = o.voice_volume;
    mixer.music = o.music_volume;
    hud.subtitles_off = !o.subtitles_game;
    if !o.subtitles_game {
        hud.subtitle = None;
    }
    for mut l in &mut lights {
        l.shadow_maps_enabled = o.shadows;
    }
    // brightness: display/brightness.rs (the exposure step, or the original's gamma ramp); the window: display/window.rs
    for (e, p) in &cams {
        if let Projection::Perspective(pp) = p {
            let mut pp = pp.clone();
            let fog_far = game_lights.as_ref().and_then(|l| l.fog.map(|f| f.1)).unwrap_or(1500.0);
            // the original's far plane: objects wholly beyond it are culled (the game material also clips each pixel
            // there, so nothing visible changes); the sky dome (radius 20000) is never culled (level.rs). Culled
            // objects also stay out of the remaster's depth prepass, where their clipped depth would hide the sky
            pp.far = world_far(fog_far, o.draw_distance);
            commands.entity(e).insert(Projection::Perspective(pp));
        }
    }
}

/// A tool run (--background, --size, --snap, --bench, --camera, --play): never fullscreen.
#[derive(Resource)]
pub struct Windowed;

/// F11 or Alt+Enter: fullscreen on / off, saved at once.
fn toggle_fullscreen(keys: Res<ButtonInput<KeyCode>>, mut o: ResMut<GameOptions>) {
    let alt = keys.any_pressed([KeyCode::AltLeft, KeyCode::AltRight]);
    if keys.just_pressed(KeyCode::F11) || (alt && keys.just_pressed(KeyCode::Enter)) {
        o.fullscreen = !o.fullscreen;
        o.save();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The options file round trip: written and read back equal; the original's defaults (0x00401a80).
    #[test]
    fn round_trip() {
        let d = GameOptions::default();
        assert_eq!((d.sound_volume, d.brightness, d.draw_distance, d.mouse_sensitivity, d.shadows), (0.8, 1.5, 1.0, 0.5, true));
        let mut o = d.clone();
        o.music_volume = 0.3;
        o.invert_mouse = true;
        o.resolution = (1280, 720);
        o.tokens_taken = vec!["lev_1_m1:12".into()];
        o.checkpoint_saves = true;
        o.graphics_preset = crate::graphics::Preset::Original;
        o.graphics.insert("bloom".into(), 0.2.into());
        assert_eq!(GameOptions::from_json(&serde_json::to_string(&o).unwrap()), o);
    }

    /// An options file from before the tokens were keyed by level: its "lev_1_m1:<id>" keys stand for any level.
    #[test]
    fn legacy_token_keys() {
        let o = GameOptions::from_json(r#"{"tokens": 2, "tokens_taken": ["lev_1_m1:12", "lev_1_m1:40"]}"#);
        assert_eq!(o.tokens_taken, vec!["any:12".to_string(), "any:40".to_string()]);
        assert!(o.tokens_keyed_by_level);
        let n = GameOptions::from_json(r#"{"tokens_keyed_by_level": true, "tokens_taken": ["lev_1_m1:12"]}"#);
        assert_eq!(n.tokens_taken, vec!["lev_1_m1:12".to_string()]);
    }

    /// Record by record (0x00462310): a wrong type keeps that option's default only; unknown and missing keys.
    #[test]
    fn partial_files() {
        let o = GameOptions::from_json(r#"{"music_volume": 0.25, "brightness": "bright", "no_such_option": 3}"#);
        assert_eq!((o.music_volume, o.brightness), (0.25, 1.5));
        assert_eq!(o.sound_volume, 0.8);
        assert_eq!(GameOptions::from_json("not json"), GameOptions::default());
    }

    #[test]
    fn draw_distance() {
        assert_eq!(draw_distance_scale(0.1), 0.25);
        assert_eq!(draw_distance_scale(0.25), 0.25);
        assert_eq!(draw_distance_scale(0.6), 0.6);
        assert_eq!(draw_distance_scale(3.0), 1.0);
    }
}
