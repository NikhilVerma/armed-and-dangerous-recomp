//! `--no-window` draws the game into an image in memory, for snapshots that must not touch the desktop.
//!
//! `--background` isn't enough for this on macOS. Metal gives a fully hidden window nothing to draw into, so the test
//! window has to stay partly on screen and above the others (platform::BACKGROUND_WINDOW_NEEDS_VISIBLE_EDGE).
//!
//! This mode doesn't start the window system, so no window can open and nothing takes focus. A `Window` record stays
//! in the world without a real window behind it, because the view code reads the picture's size from it. Each frame,
//! cameras and screenshots aimed at that window are pointed at the image.
//!
//! It doesn't cover real input, presentation or the frame limiter, which all need a real window.

use bevy::camera::{CameraUpdateSystems, ImageRenderTarget, RenderTarget};
use bevy::prelude::*;
use bevy::render::render_resource::TextureFormat;
use bevy::render::view::screenshot::Screenshot;

/// The image that stands in for the window.
#[derive(Resource)]
struct OffscreenTarget(Handle<Image>);

impl OffscreenTarget {
    fn render_target(&self) -> RenderTarget {
        RenderTarget::Image(ImageRenderTarget { handle: self.0.clone(), scale_factor: 1.0 })
    }
}

/// Added after DefaultPlugins with the window system disabled. `size` is the picture's size in pixels.
pub struct OffscreenPlugin {
    pub size: (u32, u32),
}

impl Plugin for OffscreenPlugin {
    fn build(&self, app: &mut App) {
        let image = Image::new_target_texture(self.size.0, self.size.1, TextureFormat::Rgba8UnormSrgb, None);
        let handle = app.world_mut().resource_mut::<Assets<Image>>().add(image);
        app.insert_resource(OffscreenTarget(handle))
            // Runs after the steps that only act on window cameras or set the game view's target.
            .add_systems(
                PostUpdate,
                (aim_cameras, aim_screenshots)
                    .after(super::tag_game_views)
                    .after(super::render_scale::apply)
                    .before(CameraUpdateSystems),
            );
    }
}

fn aim_cameras(target: Res<OffscreenTarget>, mut cameras: Query<&mut RenderTarget, With<Camera>>) {
    for mut camera_target in &mut cameras {
        if matches!(*camera_target, RenderTarget::Window(_)) {
            *camera_target = target.render_target();
        }
    }
}

fn aim_screenshots(target: Res<OffscreenTarget>, mut shots: Query<&mut Screenshot, Added<Screenshot>>) {
    for mut shot in &mut shots {
        if matches!(shot.0, RenderTarget::Window(_)) {
            shot.0 = target.render_target();
        }
    }
}
