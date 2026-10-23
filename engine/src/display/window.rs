//! The window: the display mode and the resolutions the Graphics page offers (old remaster archive/hook/enhance/display.cpp
//! `window_mode` and `all_modes`; the original's mode list archive/src/renderer/display_modes.cpp).
//!
//! - Display mode: the options' Fullscreen (F11 / Alt+Enter, the Graphics page's checkbox) and the setting
//!   `fullscreen_mode` together give display.cpp's three choices: Fullscreen (exclusive, at the chosen resolution),
//!   Windowed, Borderless (a window covering the monitor). Tool runs (options::Windowed) always keep their window.
//! - Resolutions: the original offers the adapter's modes 640..1600 wide in 16:9, 4:3 or 5:4; `all_modes` offers every
//!   mode from 640 wide (display.cpp `g_displayModeFilter`: min 640, max 16384, any aspect).

use super::FullscreenMode;
use bevy::prelude::*;
use bevy::window::{MonitorSelection, PrimaryMonitor, VideoModeSelection, WindowMode};

pub struct WindowSettingsPlugin;

impl Plugin for WindowSettingsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<DisplayModes>().add_systems(PostUpdate, (monitor_modes, apply).chain());
    }
}

/// The resolutions of the primary monitor (every video mode, refresh rates merged), sorted by width then height.
#[derive(Resource, Default, Debug, Clone)]
pub struct DisplayModes(pub Vec<UVec2>);

/// Offered when no monitor is known (an off-screen tool run before the monitors are listed): the engine's list before
/// the setting.
const FALLBACK: [(u32, u32); 6] = [(1280, 720), (1600, 900), (1920, 1080), (2560, 1440), (3440, 1440), (3840, 2160)];

impl DisplayModes {
    /// The resolutions the Graphics page offers: the original's filter or every mode from 640 wide (`all_modes`), and
    /// the current window size when it is not among them (a window can have any size).
    pub fn offered(&self, all_modes: bool, current: (u32, u32)) -> Vec<(u32, u32)> {
        let source: Vec<(u32, u32)> = if self.0.is_empty() { FALLBACK.to_vec() } else { self.0.iter().map(|m| (m.x, m.y)).collect() };
        let mut list: Vec<(u32, u32)> = source.into_iter().filter(|&(w, h)| offered(all_modes, w, h)).collect();
        if !list.contains(&current) && current.0 > 0 && current.1 > 0 {
            list.push(current);
        }
        list.sort();
        list.dedup();
        list
    }
}

/// display_modes.cpp's filter (640..1600 wide, 16:9 / 4:3 / 5:4), or display.cpp's with all_modes (640..16384, any
/// aspect).
pub fn offered(all_modes: bool, w: u32, h: u32) -> bool {
    if all_modes {
        return (640..=16384).contains(&w) && h > 0;
    }
    let aspect_ok = w * 9 == h * 16 || w * 3 == h * 4 || w * 4 == h * 5;
    (640..=1600).contains(&w) && aspect_ok
}

/// The primary monitor's modes, read when the monitors change.
fn monitor_modes(monitors: Query<&bevy::window::Monitor, With<PrimaryMonitor>>, changed: Query<(), Changed<bevy::window::Monitor>>, mut modes: ResMut<DisplayModes>) {
    if changed.is_empty() && !modes.0.is_empty() {
        return;
    }
    let Some(m) = monitors.iter().next() else { return };
    let mut list: Vec<UVec2> = m.video_modes.iter().map(|v| v.physical_size).collect();
    list.push(UVec2::new(m.physical_width, m.physical_height));
    list.sort_by_key(|s| (s.x, s.y));
    list.dedup();
    if modes.0 != list {
        modes.0 = list;
    }
}

/// The window mode for the options: windowed, a borderless window over the current monitor, or exclusive fullscreen
/// at the chosen resolution (the monitor's mode of that size with the highest refresh rate; else the current mode).
pub fn window_mode(fullscreen: bool, kind: FullscreenMode, resolution: (u32, u32), monitor: Option<&bevy::window::Monitor>) -> WindowMode {
    if !fullscreen {
        return WindowMode::Windowed;
    }
    match kind {
        FullscreenMode::Borderless => WindowMode::BorderlessFullscreen(MonitorSelection::Current),
        FullscreenMode::Exclusive => {
            let mode = monitor
                .and_then(|m| m.video_modes.iter().filter(|v| (v.physical_size.x, v.physical_size.y) == resolution).max_by_key(|v| v.refresh_rate_millihertz))
                .copied()
                .map(VideoModeSelection::Specific)
                .unwrap_or(VideoModeSelection::Current);
            WindowMode::Fullscreen(MonitorSelection::Current, mode)
        }
    }
}

/// The window follows the options and the settings: its mode, and its size when windowed (or exclusive); the state
/// log's "window" for the scripts.
#[allow(clippy::too_many_arguments)]
fn apply(
    o: Res<crate::options::GameOptions>,
    settings: Res<crate::graphics::GraphicsSettings>,
    mut windows: Query<&mut Window>,
    monitors: Query<&bevy::window::Monitor, With<PrimaryMonitor>>,
    windowed: Option<Res<crate::options::Windowed>>,
    state: Option<ResMut<crate::playtest::PlayState>>,
    mut new_window: Local<bool>,
) {
    let first = !*new_window;
    if !o.is_changed() && !settings.is_changed() && !first {
        return;
    }
    *new_window = true;
    // tool runs stay windowed (main.rs inserts Windowed); the option is still logged so a script can check it
    let full = o.fullscreen && windowed.is_none();
    let kind = settings.display.fullscreen_mode;
    let mut state = state;
    for mut w in &mut windows {
        let mode = window_mode(full, kind, o.resolution, monitors.iter().next());
        if w.mode != mode {
            w.mode = mode;
        }
        let sized = !full || kind == FullscreenMode::Exclusive;
        if sized && o.resolution != (0, 0) && (w.resolution.width() as u32, w.resolution.height() as u32) != o.resolution {
            w.resolution.set(o.resolution.0 as f32, o.resolution.1 as f32);
        }
        if let Some(s) = state.as_mut() {
            let size = if sized && o.resolution != (0, 0) { o.resolution } else { (w.resolution.width() as u32, w.resolution.height() as u32) };
            let mode_name = if !o.fullscreen { "windowed" } else if kind == FullscreenMode::Exclusive { "fullscreen" } else { "borderless" };
            s.0.insert("window".into(), serde_json::json!({ "fullscreen": o.fullscreen, "mode": mode_name, "width": size.0, "height": size.1, "brightness": o.brightness, "shadows": o.shadows, "draw_distance": o.draw_distance, "graphics_preset": o.graphics_preset, "graphics": o.graphics }));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The original's list: 640..1600 wide in 16:9, 4:3, 5:4; all_modes: every mode from 640 wide.
    #[test]
    fn mode_filter() {
        let modes = DisplayModes(vec![UVec2::new(640, 480), UVec2::new(800, 600), UVec2::new(1280, 1024), UVec2::new(1600, 900), UVec2::new(1680, 1050), UVec2::new(2560, 1440), UVec2::new(3840, 2160), UVec2::new(1366, 768)]);
        assert_eq!(modes.offered(false, (640, 480)), vec![(640, 480), (800, 600), (1280, 1024), (1600, 900)]);
        let all = modes.offered(true, (640, 372));
        assert_eq!(all.len(), 9, "{all:?}");
        assert!(all.contains(&(640, 372)) && all.contains(&(3840, 2160)) && all.contains(&(1366, 768)));
    }

    #[test]
    fn modes() {
        assert_eq!(window_mode(false, FullscreenMode::Borderless, (0, 0), None), WindowMode::Windowed);
        assert_eq!(window_mode(true, FullscreenMode::Borderless, (0, 0), None), WindowMode::BorderlessFullscreen(MonitorSelection::Current));
        assert_eq!(window_mode(true, FullscreenMode::Exclusive, (1280, 720), None), WindowMode::Fullscreen(MonitorSelection::Current, VideoModeSelection::Current));
    }
}
