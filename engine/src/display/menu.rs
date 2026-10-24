//! The Display page's steppers over the options (gamemenu.rs draws the page; its checkboxes toggle the boolean
//! settings by name, graphics::menu::toggle). The ranges and steps are the old remaster's registered sliders
//! (archive/hook/enhance/settings.h `RemasterSetting` min / max / step); changes are written to the options' `graphics`
//! changes and apply at once, as the old Remaster page's callbacks did.

use super::FullscreenMode;
use crate::graphics::{resolve, set_change};
use crate::options::GameOptions;

/// A stepper on the Display page.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Stepper {
    /// display.cpp window_mode: Fullscreen / Windowed / Borderless
    DisplayMode,
    /// display.cpp render_scale 0.5..2.0 step 0.25
    RenderScale,
    /// display.cpp fps_limit 0..240 step 30 (0 = off)
    FpsLimit,
    /// camera.cpp fov 0.75..1.75 step 0.05
    Fov,
    /// ui_aspect.cpp text_scale 1.0..2.5 step 0.25
    TextScale,
}

/// A numeric setting's slider: (key, min, max, step).
fn slider(s: Stepper) -> Option<(&'static str, f32, f32, f32)> {
    match s {
        Stepper::RenderScale => Some(("render_scale", 0.5, 2.0, 0.25)),
        Stepper::FpsLimit => Some(("fps_limit", 0.0, 240.0, 30.0)),
        Stepper::Fov => Some(("fov", 0.75, 1.75, 0.05)),
        Stepper::TextScale => Some(("text_scale", 1.0, 2.5, 0.25)),
        Stepper::DisplayMode => None,
    }
}

const MODES: [&str; 3] = ["Fullscreen", "Windowed", "Borderless"];

impl Stepper {
    /// The stepper's positions (its ticks).
    pub fn positions(self) -> usize {
        match slider(self) {
            Some((_, lo, hi, step)) => ((hi - lo) / step).round() as usize + 1,
            None => MODES.len(),
        }
    }

    /// The current position.
    pub fn index(self, o: &GameOptions) -> usize {
        let d = resolve(o.graphics_preset, &o.graphics).display;
        match slider(self) {
            Some((_, lo, _, step)) => {
                let v = match self {
                    Stepper::RenderScale => d.render_scale,
                    Stepper::FpsLimit => d.fps_limit as f32,
                    Stepper::Fov => d.fov,
                    _ => d.text_scale,
                };
                (((v - lo) / step).round().max(0.0) as usize).min(self.positions() - 1)
            }
            None => match (o.fullscreen, d.fullscreen_mode) {
                (false, _) => 1,
                (true, FullscreenMode::Exclusive) => 0,
                (true, FullscreenMode::Borderless) => 2,
            },
        }
    }

    /// Sets the position (clamped to the ends).
    pub fn set_index(self, o: &mut GameOptions, i: usize) {
        let i = i.min(self.positions() - 1);
        match slider(self) {
            Some((key, lo, _, step)) => {
                let v = lo + step * i as f32;
                let value: serde_json::Value = if self == Stepper::FpsLimit { (v.round() as u32).into() } else { ((v * 100.0).round() / 100.0).into() };
                set_change(o.graphics_preset, &mut o.graphics, key, value);
            }
            None => {
                o.fullscreen = i != 1;
                if i != 1 {
                    let kind = if i == 0 { FullscreenMode::Exclusive } else { FullscreenMode::Borderless };
                    set_change(o.graphics_preset, &mut o.graphics, "fullscreen_mode", serde_json::to_value(kind).unwrap_or_default());
                }
            }
        }
    }

    /// The value as shown beside the gauge.
    pub fn text(self, o: &GameOptions) -> String {
        let d = resolve(o.graphics_preset, &o.graphics).display;
        match self {
            Stepper::DisplayMode => MODES[self.index(o)].to_owned(),
            Stepper::RenderScale => format!("{:.0}%", d.render_scale * 100.0),
            Stepper::FpsLimit if d.fps_limit == 0 => "Off".to_owned(),
            Stepper::FpsLimit => format!("{}", d.fps_limit),
            Stepper::Fov => format!("{:.2}x", d.fov),
            Stepper::TextScale => format!("{:.2}x", d.text_scale),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn steppers() {
        let mut o = GameOptions::default();
        assert_eq!((Stepper::Fov.positions(), Stepper::FpsLimit.positions(), Stepper::RenderScale.positions(), Stepper::TextScale.positions()), (21, 9, 7, 7));
        assert_eq!(Stepper::Fov.index(&o), 5);
        Stepper::Fov.set_index(&mut o, 10);
        assert_eq!(Stepper::Fov.text(&o), "1.25x");
        Stepper::FpsLimit.set_index(&mut o, 2);
        assert_eq!(Stepper::FpsLimit.text(&o), "60");
        assert_eq!(o.graphics.get("fps_limit"), Some(&serde_json::Value::from(60)));
        Stepper::FpsLimit.set_index(&mut o, 0);
        assert_eq!(o.graphics.get("fps_limit"), None, "the preset's own value is not a change");
        assert_eq!(Stepper::DisplayMode.text(&o), "Borderless");
        Stepper::DisplayMode.set_index(&mut o, 1);
        assert!(!o.fullscreen);
        Stepper::DisplayMode.set_index(&mut o, 0);
        assert_eq!(Stepper::DisplayMode.text(&o), "Fullscreen");
    }
}
