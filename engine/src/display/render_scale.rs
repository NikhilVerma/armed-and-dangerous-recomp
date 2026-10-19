//! The 3D view's resolution as a multiple of the window's (old remaster hook/enhance/display.cpp `render_scale`: the
//! game rendered its back buffer at window size x scale and Present stretched it to the window, so 2.0 is 2x
//! supersampling and 0.5 half resolution), and `dpi_aware` (off: Windows draws an application that does not declare
//! itself DPI aware at the desktop's scaled size and enlarges it; the old remaster called
//! SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2) only when it was on). Bevy's window is always DPI aware, so
//! the off case is reproduced the same way: a further scale of 1 / the monitor's scale factor.
//!
//! At an effective scale of 1 nothing changes (the game view draws to the window). Otherwise the game view draws into
//! an image of the scaled size and the HUD camera shows that image under the HUD, filtered bilinearly (as Present's
//! stretch), over the whole window.

use super::GameView;
use bevy::camera::visibility::RenderLayers;
use bevy::camera::{ImageRenderTarget, RenderTarget};
use bevy::image::ImageSampler;
use bevy::prelude::*;
use bevy::render::render_resource::TextureFormat;

pub struct RenderScalePlugin;

impl Plugin for RenderScalePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<ScaledView>().add_systems(PostUpdate, apply.before(bevy::camera::CameraUpdateSystems));
    }
}

/// The image the game view draws into while the effective scale is not 1, and its size.
#[derive(Resource, Default)]
pub struct ScaledView {
    pub image: Option<Handle<Image>>,
    pub size: UVec2,
}

/// The game view's image on the screen (on the HUD layer, under everything else).
#[derive(Component)]
struct ScaledPicture;

/// The render scale in effect: the setting (clamped as display.cpp clamps the and.ini value, 0.25..4) and, when not
/// DPI aware, the desktop's scaling undone (a 150 % desktop draws at 1 / 1.5).
pub fn effective_scale(render_scale: f32, dpi_aware: bool, os_scale_factor: f32) -> f32 {
    let s = if (0.25..=4.0).contains(&render_scale) { render_scale } else { 1.0 };
    if dpi_aware || os_scale_factor <= 0.0 { s } else { s / os_scale_factor }
}

/// The game view's picture size for a window of `window` physical pixels at `scale` (at least 1, at most the
/// largest texture wgpu allows on every adapter we target).
pub fn scaled_size(window: UVec2, scale: f32) -> UVec2 {
    (window.as_vec2() * scale).round().as_uvec2().clamp(UVec2::ONE, UVec2::splat(16384))
}

#[allow(clippy::too_many_arguments)]
fn apply(
    mut commands: Commands,
    settings: Res<crate::graphics::GraphicsSettings>,
    windows: Query<&Window>,
    tool: Option<Res<crate::options::Windowed>>,
    mut state: ResMut<ScaledView>,
    mut images: ResMut<Assets<Image>>,
    mut views: Query<(&Camera, &mut RenderTarget, &mut Projection), With<GameView>>,
    mut pictures: Query<(Entity, &mut Transform, &MeshMaterial2d<ColorMaterial>, &mut Visibility), With<ScaledPicture>>,
    (mut meshes, mut materials): (ResMut<Assets<Mesh>>, ResMut<Assets<ColorMaterial>>),
) {
    let Some(window) = windows.iter().next() else { return };
    let win = UVec2::new(window.physical_width(), window.physical_height());
    if win.x == 0 || win.y == 0 {
        return;
    }
    // a tool's window is in physical pixels by request: DPI aware (mod.rs GameView docs)
    let dpi_aware = settings.display.dpi_aware || tool.is_some();
    let scale = effective_scale(settings.display.render_scale, dpi_aware, window.resolution.base_scale_factor());
    let want = if (scale - 1.0).abs() < 1e-3 { None } else { Some(scaled_size(win, scale)) };

    // the image: made (or remade at a new size) when needed, dropped at scale 1
    match want {
        Some(size) if state.image.is_none() || state.size != size => {
            let mut img = Image::new_target_texture(size.x, size.y, TextureFormat::Rgba8UnormSrgb, None);
            img.sampler = ImageSampler::linear();
            state.image = Some(images.add(img));
            state.size = size;
            info!("display: game view drawn at {}x{} for a {}x{} window (scale {scale:.2})", size.x, size.y, win.x, win.y);
        }
        None if state.image.is_some() => {
            state.image = None;
            state.size = UVec2::ZERO;
            info!("display: game view drawn at the window's size");
        }
        _ => {}
    }

    // the game view's target
    let target = match &state.image {
        Some(h) => RenderTarget::Image(ImageRenderTarget { handle: h.clone(), scale_factor: 1.0 }),
        None => RenderTarget::Window(bevy::window::WindowRef::Primary),
    };
    let mut active = false;
    for (cam, mut t, mut proj) in &mut views {
        active |= cam.is_active;
        let same = match (&*t, &target) {
            (RenderTarget::Window(_), RenderTarget::Window(_)) => true,
            (RenderTarget::Image(a), RenderTarget::Image(b)) => a.handle == b.handle,
            _ => false,
        };
        if !same {
            *t = target.clone();
            // camera_system recomputes the target's size when the projection changed (a target switch alone is
            // not one of its triggers)
            proj.set_changed();
        }
    }

    // the picture on the screen
    match (&state.image, pictures.iter_mut().next()) {
        (Some(image), None) => {
            let material = materials.add(ColorMaterial { texture: Some(image.clone()), ..default() });
            commands.spawn((
                ScaledPicture,
                Mesh2d(meshes.add(Rectangle::new(1.0, 1.0))),
                MeshMaterial2d(material),
                Transform::from_xyz(0.0, 0.0, -990.0).with_scale(Vec3::new(win.x as f32, win.y as f32, 1.0)),
                Visibility::Inherited,
                RenderLayers::layer(crate::hud::HUD_LAYER),
            ));
        }
        (Some(image), Some((_, mut tr, mat, mut vis))) => {
            if let Some(mut m) = materials.get_mut(&mat.0) {
                if m.texture.as_ref() != Some(image) {
                    m.texture = Some(image.clone());
                }
            }
            let scale = Vec3::new(win.x as f32, win.y as f32, 1.0);
            if tr.scale != scale {
                tr.scale = scale;
            }
            let v = if active { Visibility::Inherited } else { Visibility::Hidden };
            if *vis != v {
                *vis = v;
            }
        }
        (None, Some((e, ..))) => commands.entity(e).despawn(),
        (None, None) => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scale() {
        assert_eq!(effective_scale(1.0, true, 1.5), 1.0);
        assert!((effective_scale(1.0, false, 1.5) - 1.0 / 1.5).abs() < 1e-6);
        assert_eq!(effective_scale(9.0, true, 1.0), 1.0, "out of range: the default, as display.cpp");
        assert_eq!(scaled_size(UVec2::new(1280, 720), 0.5), UVec2::new(640, 360));
        assert_eq!(scaled_size(UVec2::new(3840, 2160), 2.0), UVec2::new(7680, 4320));
    }
}
