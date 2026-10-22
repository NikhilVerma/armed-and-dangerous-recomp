//! The mouse pointer in the menus and whether the mouse stays in the window: the old remaster's `[input]
//! hardware_cursor` and `clip_cursor` (hook/enhance/input.cpp `InputInit`, `CursorSprite`, `Clip`), as typed settings
//! ([`CursorSettings`], part of the graphics presets).
//!
//! - **The menu pointer** (`hardware_cursor`). The original hides the Windows pointer once at window creation
//!   (0x004e0300) and draws its own sprite, the layout's `ui_mouse_cursor` (model ui_multi_lock), last in the UI pass
//!   (0x004646e2: ui +0x54), placed at the pointer every frame (0x00491380 -> 0x00463ac0). Off (the Original preset):
//!   that sprite over a hidden pointer. On (the Remaster): the Windows pointer (the sprite trailed the pointer a frame
//!   or more in the original and felt slow; the player, 2026-10-09). Either way the pointer is hidden in play.
//! - **Keeping the mouse in the window** (`clip_cursor`). The original opens the mouse non-exclusive and clips only in
//!   exclusive mode (0x004f82d0), so in a window or borderless the hidden pointer wandered onto other monitors and a
//!   click there took the focus away. On: the pointer is confined to the window while it has the focus, in play and in
//!   the menus (winit's `CursorGrabMode::Confined`, released with the focus as the old remaster's WndProc did). Off:
//!   no clip; in play the pointer is still put back to the window's centre every frame (player.rs `grab_cursor`).
//!   An absolute pointer (a remote desktop's mouse mode: Moonlight's) is held at the window's edges by the clip, which
//!   mouselook.rs already reads as movement into that edge, so the look keeps turning there.
//!
//! API: [`CursorPlugin`] (the sprite); [`grab`] (the grab mode and pointer visibility player.rs `grab_cursor` sets).

use crate::graphics::GraphicsSettings;
use bevy::prelude::*;
use bevy::window::{CursorGrabMode, PrimaryWindow};
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct CursorSettings {
    /// the Windows pointer in the menus (off: the game's own pointer sprite)
    pub hardware_cursor: bool,
    /// keep the pointer inside the window while it has the focus
    pub clip_cursor: bool,
}

impl CursorSettings {
    pub fn original() -> Self {
        CursorSettings { hardware_cursor: false, clip_cursor: false }
    }

    pub fn remaster() -> Self {
        CursorSettings { hardware_cursor: true, clip_cursor: true }
    }
}

impl Default for CursorSettings {
    fn default() -> Self {
        Self::remaster()
    }
}

/// The grab mode and whether the Windows pointer shows: in play (hidden; confined with `clip_cursor`) or in a menu
/// (shown with `hardware_cursor`; confined with `clip_cursor` while the window has the focus).
pub fn grab(s: &CursorSettings, play: bool, focused: bool) -> (CursorGrabMode, bool) {
    let mode = |on: bool| if on { CursorGrabMode::Confined } else { CursorGrabMode::None };
    if play { (mode(s.clip_cursor), false) } else { (mode(s.clip_cursor && focused), s.hardware_cursor) }
}

/// Whether the game is in play for the pointer (not a menu, not the pub screen, which takes the pointer too).
pub fn in_play(state: crate::AppState, page: crate::gamemenu::Page) -> bool {
    state == crate::AppState::Playing && page != crate::gamemenu::Page::Pub
}

/// The settings in effect (the Original's before the graphics settings exist).
pub fn settings(s: Option<&GraphicsSettings>) -> CursorSettings {
    s.map_or_else(CursorSettings::original, |s| s.cursor)
}

/// The game's pointer sprite (ui_mouse_cursor).
#[derive(Component)]
pub struct PointerSprite;

pub struct CursorPlugin;

impl Plugin for CursorPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(PostUpdate, pointer_sprite.before(bevy::transform::TransformSystems::Propagate));
    }
}

/// The pointer's place in the menus' 640 x 480 layout (y up), as gamemenu.rs hit-tests it (scaled by height / 480
/// about the window's centre).
pub fn layout_point(cursor: Vec2, window: Vec2) -> Vec2 {
    let s = window.y / 480.0;
    Vec2::new(320.0 + (cursor.x - window.x / 2.0) / s, 240.0 - (cursor.y - window.y / 2.0) / s)
}

/// Where a pointer that left the window shows: its last point inside, put on the border it was nearest (it crossed
/// that one between two frames).
fn clamp_to_border(c: Vec2, size: Vec2) -> Vec2 {
    let c = c.clamp(Vec2::ZERO, size);
    let (dx, dy) = (c.x.min(size.x - c.x), c.y.min(size.y - c.y));
    if dx <= dy {
        Vec2::new(if c.x < size.x - c.x { 0.0 } else { size.x }, c.y)
    } else {
        Vec2::new(c.x, if c.y < size.y - c.y { 0.0 } else { size.y })
    }
}

/// Shows the sprite at the pointer in the menus while the Windows pointer is off; made from the HUD's layout once
/// it is loaded (again after a level change despawns it).
#[allow(clippy::too_many_arguments)]
fn pointer_sprite(
    mut commands: Commands,
    gfx: Option<Res<GraphicsSettings>>,
    hud: Option<Res<crate::hud::HudAssets>>,
    state: Res<State<crate::AppState>>,
    menu: Res<crate::gamemenu::GameMenu>,
    windows: Query<&Window, With<PrimaryWindow>>,
    mut sprite: Query<(&mut crate::hud::HudElement, &mut Transform, &mut Visibility), With<PointerSprite>>,
    (mut meshes, mut materials): (ResMut<Assets<Mesh>>, ResMut<Assets<ColorMaterial>>),
    mut last: Local<Option<Vec2>>,
) {
    let s = settings(gfx.as_deref());
    let Ok((mut el, mut t, mut vis)) = sprite.single_mut() else {
        if let Some(hud) = hud.as_ref().filter(|_| !s.hardware_cursor) {
            if let Some(e) = crate::hud::spawn_element(&mut commands, &mut meshes, &mut materials, hud, "ui_mouse_cursor", true) {
                // drawn last (UiPass_Draw draws it after every other element), over the menu's fade
                commands.entity(e).insert((PointerSprite, crate::gamemenu::MenuOwned, Transform::from_xyz(0.0, 0.0, 95.0)));
            }
        }
        return;
    };
    let play = in_play(*state.get(), menu.page);
    // 0x00491380 clamps the pointer to the client area (+-w/2, +-h/2): a pointer that left the (focused) window keeps
    // the sprite at the edge it crossed, here its last place inside, pinned to the border
    let at = windows.single().ok().filter(|w| w.focused).and_then(|w| {
        let size = Vec2::new(w.width(), w.height());
        let c = match w.cursor_position() {
            Some(c) => {
                *last = Some(c);
                c
            }
            None => clamp_to_border((*last)?, size),
        };
        Some(layout_point(c, size))
    });
    let want = match at {
        Some(p) if !s.hardware_cursor && !play => {
            el.pos = p;
            t.translation.z = 95.0;
            Visibility::Visible
        }
        _ => Visibility::Hidden,
    };
    if *vis != want {
        *vis = want;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The Original: the game's sprite (no Windows pointer) and no clip; the Remaster: the Windows pointer in the
    /// menus, confined while focused, and confined and hidden in play.
    #[test]
    fn grab_modes() {
        let o = CursorSettings::original();
        assert_eq!(grab(&o, false, true), (CursorGrabMode::None, false));
        assert_eq!(grab(&o, true, true), (CursorGrabMode::None, false));
        let r = CursorSettings::remaster();
        assert_eq!(grab(&r, false, true), (CursorGrabMode::Confined, true));
        assert_eq!(grab(&r, false, false), (CursorGrabMode::None, true));
        assert_eq!(grab(&r, true, true), (CursorGrabMode::Confined, false));
    }

    #[test]
    fn layout_points() {
        let w = Vec2::new(1920.0, 1080.0);
        assert_eq!(layout_point(w / 2.0, w), Vec2::new(320.0, 240.0));
        assert_eq!(layout_point(Vec2::new(960.0, 0.0), w), Vec2::new(320.0, 480.0));
    }

    #[test]
    fn a_pointer_that_left_sits_on_the_nearest_border() {
        let w = Vec2::new(1920.0, 1080.0);
        assert_eq!(clamp_to_border(Vec2::new(1915.0, 500.0), w), Vec2::new(1920.0, 500.0));
        assert_eq!(clamp_to_border(Vec2::new(700.0, 3.0), w), Vec2::new(700.0, 0.0));
    }
}
