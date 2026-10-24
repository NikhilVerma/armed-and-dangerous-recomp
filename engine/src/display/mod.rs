//! Display, camera-view and UI-layout settings ported from the old remaster (the D3D9 hook's `archive/hook/enhance/camera.cpp`,
//! `display.cpp` and `ui_aspect.cpp`, and.ini sections [camera], [display] and [ui]). Each setting is a field of
//! [`DisplaySettings`], flattened into the presentation settings (graphics/mod.rs `GraphicsSettings`), so the two
//! presets, the options file's `graphics` changes, `--graphics key=value` and the menus treat them like every other
//! presentation setting. The **Original** preset has the original game's behaviour (the old remaster's `original`
//! register defaults), the **Remaster** preset the old remaster's defaults (its `remaster` register defaults, see the
//! table on [`DisplaySettings`]).
//!
//! One module per mechanism, each a plugin with its own systems:
//! - [`view`]: field of view, Hor+ widescreen and the original's fixed letterbox (camera.cpp `Camera_Adjust`,
//!   `SetLetterbox`): the projection shown, never the game's own lens.
//! - [`frame_limit`]: the frame rate cap (display.cpp `DisplayBeforePresent`).
//! - [`render_scale`]: the 3D view rendered at a scale of the window and resampled to it (display.cpp `render_scale`,
//!   and `dpi_aware`: an application Windows does not know to be DPI aware is drawn at the desktop's scaled size).
//! - [`window`]: the display mode (fullscreen / windowed / borderless) and the resolutions offered (`all_modes`).
//! - [`brightness`]: the brightness option as the original's gamma ramp (`brightness_shader`, display.cpp
//!   `Display_Gamma` / vk_present.cpp), or the engine's exposure step.
//! - [`ui`]: the UI layout on wide screens (`ui.anchor`), the text size (`text_scale`, `text_relative`).
//! - [`menu`]: the Display options page's steppers (gamemenu.rs draws and drives the page).
//!
//! API: [`DisplayPlugin`]; [`DisplaySettings`] (read through `Res<GraphicsSettings>`'s `display`); [`GameView`] (the
//! camera that draws the world to the screen); [`ui::UiLayout`] (640 x 480 layout <-> window pixels).

pub mod brightness;
pub mod frame_limit;
pub mod menu;
pub mod render_scale;
pub mod ui;
pub mod view;
pub mod window;

use bevy::prelude::*;
use serde::{Deserialize, Serialize};

/// How fullscreen is done when the options' Fullscreen is on (display.cpp `window_mode` 0 / 2; 1 is Windowed, the
/// options' Fullscreen off).
#[derive(Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Debug, Default)]
#[serde(rename_all = "lowercase")]
pub enum FullscreenMode {
    /// exclusive fullscreen at the chosen resolution (the display changes mode)
    Exclusive,
    /// a borderless window covering the monitor at its own resolution (the old remaster's Vulkan "Fullscreen", the
    /// engine's fullscreen since 2026-10-14)
    #[default]
    Borderless,
}

/// The display, view and UI settings. Defaults (old remaster `RemasterRegister(setting, original, remaster)`):
///
/// | Setting | Original | Remaster | Old remaster |
/// |---|---|---|---|
/// | fov | 1.0 | 1.0 | camera.cpp fov (0.75..1.75) |
/// | fov_zoom | off | off | camera.cpp fov_zoom |
/// | hor_plus | off | on | camera.cpp hor_plus (0, 1) |
/// | letterbox | on | off | camera.cpp letterbox (1, 0) |
/// | fps_limit | 0 | 0 | display.cpp fps_limit (0..240) |
/// | render_scale | 1.0 | 1.0 | display.cpp render_scale (0.5..2.0) |
/// | fullscreen_mode | borderless | borderless | display.cpp window_mode 0 (Vulkan: a window covering the monitor) |
/// | dpi_aware | off | on | display.cpp dpi_aware (0; the Remaster deviates: sharp, as the engine was) |
/// | all_modes | off | on | display.cpp all_modes (0, 1) |
/// | brightness_shader | off | on | display.cpp brightness_shader (0, 1) |
/// | ui_anchor | off | on | ui_aspect.cpp anchor (0, 1) |
/// | text_scale | 1.0 | 1.0 | ui_aspect.cpp text_scale (1.0..2.5) |
/// | text_relative | off | on | ui_aspect.cpp text_relative (0, 1) |
#[derive(Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct DisplaySettings {
    /// field-of-view scale: tan(fov / 2) is multiplied by it (1.15 is about +10 degrees at 60)
    pub fov: f32,
    /// the field-of-view scale also applies to zoomed views (sniper, rifle and other weapon cameras)
    pub fov_zoom: bool,
    /// keep the vertical field of view and widen the horizontal one to the screen's aspect; off, the picture is the
    /// original's 4:3 projection stretched over the screen
    pub hor_plus: bool,
    /// the original's fixed letterbox: the 3D view drawn in the band from 11.25 % to 88.75 % of the screen height
    /// (0x005bcf24 / 0x005bcf28), black above and below, in play and in cutscenes alike
    pub letterbox: bool,
    /// frames per second at most (0 = no limit)
    pub fps_limit: u32,
    /// the 3D view's resolution as a multiple of the window's (2.0 = 2x supersampling, 0.5 = half), resampled
    pub render_scale: f32,
    pub fullscreen_mode: FullscreenMode,
    /// sharp on high-DPI screens; off, the game is drawn at the desktop's scaled size and enlarged, as Windows shows
    /// an application that does not declare itself DPI aware (the original)
    pub dpi_aware: bool,
    /// offer every resolution the monitor has from 640 wide; off, the original's list: 640..1600 wide in 16:9, 4:3
    /// or 5:4 (archive/src/renderer/display_modes.cpp)
    pub all_modes: bool,
    /// the brightness option as the original's gamma ramp (out = in^(1 / brightness), Renderer_SetGamma 0x005091c0),
    /// applied to the picture so it works in a window; off, the engine's exposure step (1.5 = unchanged)
    pub brightness_shader: bool,
    /// the HUD and menus keep their 4:3 proportions on wide screens (HUD pinned to the edges, menus centred); off,
    /// the original stretches the whole 640 x 480 layout across the screen
    pub ui_anchor: bool,
    /// text size multiplier (texts laid out on the menus' fixed grid stay at most 1, so lines keep their places)
    pub text_scale: f32,
    /// text keeps its 640 x 480 size at every resolution; off, the original's glyphs are sized in screen pixels (a
    /// quarter of the size at 3840 x 2160 as at 640 x 480)
    pub text_relative: bool,
}

impl DisplaySettings {
    /// The original game's behaviour (the old remaster's `original` defaults).
    pub fn original() -> Self {
        DisplaySettings {
            fov: 1.0,
            fov_zoom: false,
            hor_plus: false,
            letterbox: true,
            fps_limit: 0,
            render_scale: 1.0,
            fullscreen_mode: FullscreenMode::Borderless,
            dpi_aware: false,
            all_modes: false,
            brightness_shader: false,
            ui_anchor: false,
            text_scale: 1.0,
            text_relative: false,
        }
    }

    /// The old remaster's defaults.
    pub fn remaster() -> Self {
        DisplaySettings {
            hor_plus: true,
            letterbox: false,
            dpi_aware: true,
            all_modes: true,
            brightness_shader: true,
            ui_anchor: true,
            text_relative: true,
            ..Self::original()
        }
    }
}

impl Default for DisplaySettings {
    fn default() -> Self {
        Self::remaster()
    }
}

/// The camera that draws the world to the screen (tagged when a 3D camera targeting the window appears; a save's
/// thumbnail camera renders to its own image and is not one): the view settings, the letterbox and the render scale
/// apply to it.
#[derive(Component, Clone, Copy, Debug)]
pub struct GameView {
    /// the camera's unzoomed field of view (camera +0xf0 in the original): a lens narrower than 95 % of it is a zoom
    pub base_fov: f32,
}

/// The settings' plugins; the Display page itself is gamemenu.rs's.
pub struct DisplayPlugin;

impl Plugin for DisplayPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(PostUpdate, tag_game_views)
            .add_plugins((view::ViewPlugin, frame_limit::FrameLimitPlugin, render_scale::RenderScalePlugin, window::WindowSettingsPlugin, brightness::BrightnessPlugin))
            .add_systems(Last, log_display.run_if(resource_exists::<crate::playtest::PlayState>));
    }
}

/// The state log's "display" (playtest expect steps): the projection the game view really uses (`vfov` / `hfov` in
/// degrees, from its matrix, and its `aspect`), the game's own lens (`game_fov`), the 3D view's band in its target
/// (`viewport_y`, `viewport_h`; null for the whole target) and the letterbox `bars`, the game view's picture size
/// (`render_w`, `render_h`), the frame interval (`frame_ms`, the median of the last 31), and an in-game HUD text's scale
/// (`hud_text_scale`: the ammo count's, window pixels per layout unit).
#[allow(clippy::type_complexity)]
fn log_display(
    views: Query<(&Camera, &Projection), With<GameView>>,
    times: Res<frame_limit::FrameTimes>,
    texts: Query<(&crate::hud::HudElement, &Transform), With<crate::hud::HudText>>,
    bars: Query<&Visibility, With<view::LetterboxBar>>,
    mut state: ResMut<crate::playtest::PlayState>,
) {
    let r2 = |v: f32| (v * 100.0).round() / 100.0;
    let Some((cam, proj)) = views.iter().next() else { return };
    let m = cam.clip_from_view();
    let (vfov, hfov) = (2.0 * (1.0 / m.y_axis.y).atan(), 2.0 * (1.0 / m.x_axis.x).atan());
    let game_fov = match proj {
        Projection::Perspective(p) => r2(p.fov.to_degrees()),
        _ => 0.0,
    };
    let viewport = cam.viewport.as_ref().map(|v| (v.physical_position.y, v.physical_size.y));
    let render = cam.physical_target_size().unwrap_or_default();
    let text = texts.iter().find(|(e, _)| e.name == "ui_ammo_count").map(|(_, t)| (t.scale.y * 1000.0).round() / 1000.0);
    state.0.insert(
        "display".into(),
        serde_json::json!({
            "vfov": r2(vfov.to_degrees()), "hfov": r2(hfov.to_degrees()), "aspect": (m.y_axis.y / m.x_axis.x * 1000.0).round() / 1000.0,
            "game_fov": game_fov, "viewport_y": viewport.map(|v| v.0), "viewport_h": viewport.map(|v| v.1),
            "bars": bars.iter().any(|v| *v != Visibility::Hidden), "render_w": render.x, "render_h": render.y,
            "frame_ms": r2(times.median_ms()), "hud_text_scale": text,
        }),
    );
}

/// A new 3D camera drawing to the window is the game's view; its lens at that moment is its unzoomed one.
fn tag_game_views(mut commands: Commands, cams: Query<(Entity, &Projection, &bevy::camera::RenderTarget), (Added<Camera3d>, Without<GameView>)>) {
    for (e, p, target) in &cams {
        if !matches!(target, bevy::camera::RenderTarget::Window(_)) {
            continue;
        }
        let base_fov = match p {
            Projection::Perspective(pp) => pp.fov,
            _ => continue,
        };
        commands.entity(e).insert(GameView { base_fov });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The presets carry the old remaster's two sets of defaults.
    #[test]
    fn presets() {
        let (o, r) = (DisplaySettings::original(), DisplaySettings::remaster());
        assert!(o.letterbox && !o.hor_plus && !o.ui_anchor && !o.text_relative && !o.brightness_shader && !o.all_modes);
        assert!(!r.letterbox && r.hor_plus && r.ui_anchor && r.text_relative && r.brightness_shader && r.all_modes);
        assert_eq!((o.fov, o.fps_limit, o.render_scale, o.text_scale), (1.0, 0, 1.0, 1.0));
        assert_eq!((r.fov, r.fps_limit, r.render_scale, r.text_scale), (1.0, 0, 1.0, 1.0));
    }

    /// Flattened into the presentation settings: one key per setting in the options' `graphics` changes.
    #[test]
    fn flattened_keys() {
        let mut m = serde_json::Map::new();
        m.insert("fov".into(), 1.25.into());
        m.insert("letterbox".into(), true.into());
        let s = crate::graphics::resolve(crate::graphics::Preset::Remaster, &m);
        assert_eq!((s.display.fov, s.display.letterbox, s.display.hor_plus), (1.25, true, true));
        let v = serde_json::to_value(&s).unwrap();
        assert_eq!(v.get("text_scale"), Some(&serde_json::Value::from(1.0)));
    }
}
