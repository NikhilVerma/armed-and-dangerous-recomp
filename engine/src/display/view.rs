//! The view the game's camera shows: the field-of-view scale, Hor+ widescreen and the original's fixed letterbox
//! (old remaster hook/enhance/camera.cpp `Camera_Adjust` and `SetLetterbox`; evidence docs/subsystems/camera.md).
//!
//! The original builds its projection (0x0040f6e0 via 0x004f0100 -> 0x004e3490) after the scene render 0x00403050 has
//! set the letterbox viewport `(0, H * 0.1125, W, H * 0.775)` (0x005bcf24 / 0x005bcf28, unconditional), with the
//! aspect `viewport aspect / Renderer_GetAspect` (`W / H * 0.75`): every screen gets the same 1.72 projection,
//! stretched over the band. The old remaster adjusted only that final projection, never the camera's own lens; so
//! does this module: the game's lens stays in `Projection` (the follow camera, camera paths and zoom read and blend
//! it), and the matrix and frustum the renderer uses are replaced after Bevy computed them from it.

use super::{DisplaySettings, GameView};
use bevy::camera::visibility::VisibilitySystems;
use bevy::camera::{CameraProjection, CameraUpdateSystems, RenderTarget, Viewport};
use bevy::camera::primitives::Frustum;
use bevy::prelude::*;

/// The letterbox band: its top at 11.25 % of the screen height, its height 77.5 % (0x005bcf24, 0x005bcf28).
pub const LETTERBOX_TOP: f32 = 0.1125;
pub const LETTERBOX_HEIGHT: f32 = 0.775;
/// The original's screen aspect the layout is made for.
const LAYOUT_ASPECT: f32 = 4.0 / 3.0;

pub struct ViewPlugin;

impl Plugin for ViewPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(PostUpdate, letterbox_viewport.before(CameraUpdateSystems))
            .add_systems(
                PostUpdate,
                shown_projection.after(CameraUpdateSystems).after(VisibilitySystems::UpdateFrusta).before(VisibilitySystems::CheckVisibility),
            )
            .add_systems(Update, letterbox_bars);
    }
}

/// The projection shown for the game's lens (vertical `fov`), its unzoomed lens `base_fov` and the aspect of the
/// viewport it is drawn in: (vertical fov, projection aspect). camera.cpp `Camera_Adjust`:
/// - the scale multiplies tan(fov / 2), except on a zoomed lens (narrower than 95 % of the base) unless `fov_zoom`;
/// - Hor+ takes the viewport's own aspect; without the letterbox a view narrower than the letterboxed 4:3 view (1.72)
///   keeps that view's horizontal extent by widening the vertical one (no Vert- crop);
/// - otherwise the original's aspect, 4:3 over the band's height share (1.72 letterboxed, 4:3 without), stretched.
pub fn shown_lens(fov: f32, base_fov: f32, viewport_aspect: f32, s: &DisplaySettings) -> (f32, f32) {
    let zoomed = base_fov > 0.0 && fov < base_fov * 0.95;
    let mut t = (fov * 0.5).tan();
    if s.fov != 1.0 && (!zoomed || s.fov_zoom) {
        t *= s.fov.clamp(0.5, 2.0);
    }
    let band = if s.letterbox { LETTERBOX_HEIGHT } else { 1.0 };
    let aspect = if s.hor_plus && viewport_aspect > 0.0 {
        let design = LAYOUT_ASPECT / LETTERBOX_HEIGHT;
        if !s.letterbox && viewport_aspect < design {
            t *= design / viewport_aspect;
        }
        viewport_aspect
    } else {
        LAYOUT_ASPECT / band
    };
    (2.0 * t.atan(), aspect)
}

/// The 3D view's rectangle in its target (physical pixels): the letterbox band, or None for the whole target.
pub fn letterbox_rect(target: UVec2, letterbox: bool) -> Option<(UVec2, UVec2)> {
    if !letterbox || target.x == 0 || target.y == 0 {
        return None;
    }
    let top = (target.y as f32 * LETTERBOX_TOP).round() as u32;
    let height = ((target.y as f32 * LETTERBOX_HEIGHT).round() as u32).max(1);
    Some((UVec2::new(0, top), UVec2::new(target.x, height)))
}

/// The size of a camera's target in physical pixels (a window or an image: render_scale.rs draws into one).
pub fn target_size(target: &RenderTarget, windows: &Query<&Window>, images: &Assets<Image>) -> Option<UVec2> {
    match target {
        RenderTarget::Window(_) => windows.iter().next().map(|w| UVec2::new(w.physical_width(), w.physical_height())),
        RenderTarget::Image(i) => images.get(&i.handle).map(|img| img.size()),
        _ => None,
    }
}

/// The letterbox: the game view's viewport is the band (SetLetterbox: the scene render reads the band every frame,
/// so a change shows on the next frame).
fn letterbox_viewport(
    settings: Res<crate::graphics::GraphicsSettings>,
    windows: Query<&Window>,
    images: Res<Assets<Image>>,
    mut cams: Query<(&mut Camera, &RenderTarget), With<GameView>>,
) {
    for (mut cam, target) in &mut cams {
        let Some(size) = target_size(target, &windows, &images) else { continue };
        let want = letterbox_rect(size, settings.display.letterbox).map(|(physical_position, physical_size)| Viewport { physical_position, physical_size, ..default() });
        let same = match (&cam.viewport, &want) {
            (None, None) => true,
            (Some(a), Some(b)) => a.physical_position == b.physical_position && a.physical_size == b.physical_size,
            _ => false,
        };
        if !same {
            cam.viewport = want;
        }
    }
}

/// The shown projection: Bevy has computed the matrix and the frustum from the game's lens (its aspect the
/// viewport's); both are replaced by the adjusted lens's, so drawing and culling agree.
fn shown_projection(settings: Res<crate::graphics::GraphicsSettings>, mut cams: Query<(&mut Camera, &Projection, &GlobalTransform, &mut Frustum, &GameView)>) {
    for (mut cam, proj, transform, mut frustum, view) in &mut cams {
        let Projection::Perspective(game) = proj else { continue };
        let Some(size) = cam.logical_viewport_size() else { continue };
        if size.y <= 0.0 {
            continue;
        }
        let (fov, aspect_ratio) = shown_lens(game.fov, view.base_fov, size.x / size.y, &settings.display);
        let shown = PerspectiveProjection { fov, aspect_ratio, ..game.clone() };
        let m = shown.get_clip_from_view();
        if cam.computed.clip_from_view != m {
            cam.computed.clip_from_view = m;
        }
        *frustum = shown.compute_frustum(transform);
    }
}

/// The black above and below the band (the original's cleared back buffer; Bevy clears the whole target with the fog
/// colour, so the bars are drawn): two quads on the HUD layer under every HUD element, shown while the world is.
#[derive(Component)]
pub(super) struct LetterboxBar {
    top: bool,
}

fn letterbox_bars(
    mut commands: Commands,
    settings: Res<crate::graphics::GraphicsSettings>,
    windows: Query<&Window>,
    views: Query<&Camera, With<GameView>>,
    mut bars: Query<(&LetterboxBar, &mut Transform, &mut Visibility)>,
    (mut meshes, mut materials): (ResMut<Assets<Mesh>>, ResMut<Assets<ColorMaterial>>),
) {
    let on = settings.display.letterbox && views.iter().any(|c| c.is_active);
    if bars.is_empty() {
        if !on {
            return;
        }
        let mesh = meshes.add(Rectangle::new(1.0, 1.0));
        let black = materials.add(ColorMaterial::from(Color::BLACK));
        for top in [true, false] {
            commands.spawn((
                LetterboxBar { top },
                Mesh2d(mesh.clone()),
                MeshMaterial2d(black.clone()),
                Transform::default(),
                Visibility::Hidden,
                bevy::camera::visibility::RenderLayers::layer(crate::hud::HUD_LAYER),
            ));
        }
        return;
    }
    let size = windows.iter().next().map(|w| Vec2::new(w.physical_width() as f32, w.physical_height() as f32)).unwrap_or(Vec2::ONE);
    let bar = size.y * LETTERBOX_TOP;
    for (b, mut t, mut v) in &mut bars {
        let want = if on { Visibility::Inherited } else { Visibility::Hidden };
        if *v != want {
            *v = want;
        }
        // HUD space: origin at the window's centre, y up, one unit a pixel; under the HUD's parts (z 10 + their own)
        let y = (size.y - bar) * 0.5 * if b.top { 1.0 } else { -1.0 };
        let wanted = Transform::from_xyz(0.0, y, -500.0).with_scale(Vec3::new(size.x + 2.0, bar + 1.0, 1.0));
        if *t != wanted {
            *t = wanted;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn deg(r: f32) -> f32 {
        r.to_degrees()
    }

    /// The original lens: 0.654 rad vertical (60 degrees horizontal at 1.702, camera.rs vertical_fov).
    const V: f32 = 0.654;

    /// Original: the letterboxed 1.72 projection whatever the screen; without the letterbox, 4:3.
    #[test]
    fn original_is_stretched() {
        let o = DisplaySettings::original();
        let (f, a) = shown_lens(V, V, 1280.0 / 558.0, &o);
        assert_eq!(f, V);
        assert!((a - 1.7204).abs() < 1e-3);
        let (_, a) = shown_lens(V, V, 16.0 / 9.0, &DisplaySettings { letterbox: false, ..o });
        assert!((a - 4.0 / 3.0).abs() < 1e-5);
    }

    /// Hor+ at 16:9 keeps the vertical lens and widens the horizontal one (60 -> 62.2 degrees); at 4:3 without the
    /// letterbox the vertical one opens so the horizontal stays the letterboxed view's 60.
    #[test]
    fn hor_plus() {
        let r = DisplaySettings::remaster();
        let (f, a) = shown_lens(V, V, 16.0 / 9.0, &r);
        assert!((f - V).abs() < 1e-6 && (a - 16.0 / 9.0).abs() < 1e-6);
        let h = 2.0 * ((f * 0.5).tan() * a).atan();
        assert!((deg(h) - 62.2).abs() < 0.2, "{}", deg(h));
        let (f, a) = shown_lens(V, V, 4.0 / 3.0, &r);
        let h = 2.0 * ((f * 0.5).tan() * a).atan();
        assert!(f > V && (deg(h) - 60.0).abs() < 0.2, "{}", deg(h));
    }

    /// The scale multiplies tan(fov / 2); a zoomed lens keeps its own unless fov_zoom.
    #[test]
    fn fov_scale_and_zoom() {
        let s = DisplaySettings { fov: 1.25, ..DisplaySettings::remaster() };
        let (f, _) = shown_lens(V, V, 16.0 / 9.0, &s);
        assert!(((f * 0.5).tan() - (V * 0.5).tan() * 1.25).abs() < 1e-5);
        let zoom = 0.26;
        assert_eq!(shown_lens(zoom, V, 16.0 / 9.0, &s).0, zoom);
        let (fz, _) = shown_lens(zoom, V, 16.0 / 9.0, &DisplaySettings { fov_zoom: true, ..s });
        assert!(fz > zoom);
    }

    #[test]
    fn band() {
        assert_eq!(letterbox_rect(UVec2::new(640, 480), true), Some((UVec2::new(0, 54), UVec2::new(640, 372))));
        assert_eq!(letterbox_rect(UVec2::new(1280, 720), true), Some((UVec2::new(0, 81), UVec2::new(1280, 558))));
        assert_eq!(letterbox_rect(UVec2::new(1280, 720), false), None);
    }
}
