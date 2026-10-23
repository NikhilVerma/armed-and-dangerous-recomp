//! Where the 640 x 480 UI layout lands in the window (old remaster archive/hook/enhance/ui_aspect.cpp; docs/subsystems/ui.md,
//! hud.md). The original draws every UI object in a normalised screen space (0x004f01c0: x 0..1 across the viewport),
//! so on a wide screen the whole 4:3 layout is stretched, and sizes its glyphs in screen pixels (g_pixelSizeScale),
//! so text shrinks as the resolution grows.
//!
//! - `ui_anchor` (ui_aspect.cpp `BeginDraw`): the layout keeps its proportions; HUD elements are pinned to the left or
//!   right edge or centred (hud.rs `Anchor`), menu pages are centred (pillarbox). Off: stretched across the window.
//! - `text_relative` (`TextScale`): text keeps its 640 x 480 size at every resolution (one glyph pixel 1/480 of the
//!   height; the in-game HUD band's scale while the band fills the window). Off: glyphs in screen pixels.
//! - `text_scale`: a multiplier on top; text laid out on the menus' fixed grid takes at most 1 (its lines do not move
//!   with the glyph size: objectives were cut off at 1.25, ui_aspect.cpp).
//! - The in-game HUD sits in the letterbox band (rows 54..426 of 480): with the letterbox the band is where the
//!   original drew it; without, the band fills the window (the remaster's mapping since 2026-10-14: the player found
//!   the HUD too far in from the borders).
//!
//! The menus' hit boxes and the cursor (ui_aspect.cpp `CursorToLayout`, the hit scale g_uiHitScale) use the same
//! mapping backwards: `to_layout`, `text_box_scale`.

use super::DisplaySettings;
use crate::hud::Anchor;
use bevy::prelude::*;

/// The layout mapping for a window: built from the settings and the window's physical size.
#[derive(Clone, Copy, Debug)]
pub struct UiLayout {
    pub size: Vec2,
    pub anchor: bool,
    pub letterbox: bool,
    pub text_scale: f32,
    pub text_relative: bool,
}

impl UiLayout {
    pub fn new(s: &DisplaySettings, size: Vec2) -> Self {
        UiLayout { size, anchor: s.ui_anchor, letterbox: s.letterbox, text_scale: s.text_scale.clamp(0.25, 4.0), text_relative: s.text_relative }
    }

    /// Window pixels per layout unit, (x, y). `full`: laid out on the whole 640 x 480 (menus, the pub, the stats);
    /// else the in-game HUD in the letterbox band.
    pub fn scales(&self, full: bool) -> Vec2 {
        let band_fills = !full && !self.letterbox;
        let y = if !band_fills {
            self.size.y / 480.0
        } else if self.anchor {
            // capped by the width (tall windows)
            (self.size.y / 372.0).min(self.size.x / 640.0)
        } else {
            self.size.y / 372.0
        };
        let x = if self.anchor { y } else { self.size.x / 640.0 };
        Vec2::new(x, y)
    }

    /// A layout point (640 x 480, y up) in the window.
    pub fn place(&self, pos: Vec2, anchor: Anchor, full: bool) -> Vec2 {
        let s = self.scales(full);
        let x = if !self.anchor {
            (pos.x - 320.0) * s.x
        } else {
            match anchor {
                Anchor::Left => -self.size.x / 2.0 + pos.x * s.x,
                Anchor::Right => self.size.x / 2.0 - (640.0 - pos.x) * s.x,
                Anchor::Centre => (pos.x - 320.0) * s.x,
            }
        };
        Vec2::new(x, (pos.y - 240.0) * s.y)
    }

    /// A window point (pixels from the top left, y down: a cursor) in the layout, as `place` maps a centred element.
    pub fn to_layout(&self, window: Vec2, full: bool) -> Vec2 {
        let s = self.scales(full);
        Vec2::new(320.0 + (window.x - self.size.x / 2.0) / s.x, 240.0 - (window.y - self.size.y / 2.0) / s.y)
    }

    /// The text multiplier: `text_scale`, at most 1 for text on the menus' fixed grid.
    pub fn text_multiplier(&self, full: bool) -> f32 {
        if full { self.text_scale.min(1.0) } else { self.text_scale }
    }

    /// A text element's scale (x, y): its glyphs keep their proportions; relative, the layout's height scale (the
    /// band's for the HUD), else one glyph unit a screen pixel; times the multiplier.
    pub fn text_scale(&self, full: bool) -> Vec2 {
        let g = if self.text_relative { self.scales(full).y } else { 1.0 };
        Vec2::splat(g * self.text_multiplier(full))
    }

    /// How much larger a text is drawn than its layout size (menu hit boxes measure the layout size: they follow the
    /// drawn text, as g_uiHitScale makes them, in y; x keeps the layout's own x scale).
    pub fn text_box_scale(&self, full: bool) -> Vec2 {
        let s = self.scales(full);
        self.text_scale(full) / s
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn layout(s: &DisplaySettings, w: f32, h: f32) -> UiLayout {
        UiLayout::new(s, Vec2::new(w, h))
    }

    /// Remaster at 16:9: the engine's mapping before the setting (menus h / 480 centred, HUD band over the window,
    /// pinned to the edges).
    #[test]
    fn anchored() {
        let l = layout(&DisplaySettings::remaster(), 1920.0, 1080.0);
        assert_eq!(l.scales(true), Vec2::splat(1080.0 / 480.0));
        assert_eq!(l.scales(false), Vec2::splat(1080.0 / 372.0));
        assert_eq!(l.place(Vec2::new(0.0, 240.0), Anchor::Left, false).x, -960.0);
        assert_eq!(l.place(Vec2::new(640.0, 240.0), Anchor::Right, false).x, 960.0);
        assert_eq!(l.place(Vec2::new(320.0, 480.0), Anchor::Centre, true), Vec2::new(0.0, 540.0));
        assert_eq!(l.text_scale(true), Vec2::splat(1080.0 / 480.0));
    }

    /// Original at 16:9: stretched, the band where the letterbox has it, text in screen pixels.
    #[test]
    fn stretched() {
        let l = layout(&DisplaySettings::original(), 1920.0, 1080.0);
        assert_eq!(l.scales(false), Vec2::new(3.0, 2.25));
        assert_eq!(l.place(Vec2::new(0.0, 426.0), Anchor::Left, false), Vec2::new(-960.0, 186.0 * 2.25));
        assert_eq!(l.text_scale(false), Vec2::ONE);
        // the cursor maps back
        let p = l.place(Vec2::new(100.0, 300.0), Anchor::Centre, true);
        let back = l.to_layout(Vec2::new(p.x + 960.0, 540.0 - p.y), true);
        assert!((back - Vec2::new(100.0, 300.0)).length() < 1e-3);
    }

    /// Text size: the multiplier, capped at 1 on the menus' grid.
    #[test]
    fn text() {
        let s = DisplaySettings { text_scale: 2.0, ..DisplaySettings::remaster() };
        let l = layout(&s, 1280.0, 720.0);
        assert_eq!(l.text_multiplier(false), 2.0);
        assert_eq!(l.text_multiplier(true), 1.0);
        assert!((l.text_scale(false).y - 2.0 * 720.0 / 372.0).abs() < 1e-4);
    }
}
