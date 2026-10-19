//! The brightness option on the picture. The original hands it to Renderer_SetGamma 0x005091c0 as the gamma of the
//! device's ramp (clamped to 0..2; entry i = (i / 255)^(1 / gamma)), which a window does not get. The old remaster's
//! `brightness_shader` (hook/enhance/display.cpp `Display_Gamma`) applied the same curve in its present pass
//! (src/rhi/vulkan/vk_present.cpp: `c = pow(c, 1 / gamma)`). Here it is Bevy's colour grading gamma on the game view
//! (tonemapping.wesl: `pow(color, 1 / gamma)` in linear light, which for the display's power-law transfer is the same
//! curve as the ramp on the encoded value). Without it the engine's exposure step applies (one stop per 1.0 of the
//! option, 1.5 = unchanged; options.rs before 2026-10-19). The HUD is drawn by its own camera after the grading, so
//! the curve does not reach it (the device ramp did).

use super::GameView;
use bevy::prelude::*;
use bevy::render::view::ColorGrading;

pub struct BrightnessPlugin;

impl Plugin for BrightnessPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(PostUpdate, apply);
    }
}

/// The brightness option (1.0..2.0, default 1.5) as (exposure in stops, gamma).
pub fn grading(brightness: f32, gamma_ramp: bool) -> (f32, f32) {
    if gamma_ramp {
        // Renderer_SetGamma's clamp (0x005bcb80 = 2.0, else at least 0); 0 would divide by zero in the shader
        (0.0, brightness.clamp(0.01, 2.0))
    } else {
        (brightness - 1.5, 1.0)
    }
}

/// Sets the exposure and the three sections' gamma of the game view's colour grading (other grading fields are left
/// to whoever sets them).
fn apply(
    mut commands: Commands,
    o: Res<crate::options::GameOptions>,
    settings: Res<crate::graphics::GraphicsSettings>,
    mut views: Query<(Entity, Option<&mut ColorGrading>), With<GameView>>,
    new_views: Query<(), Added<GameView>>,
) {
    if !o.is_changed() && !settings.is_changed() && new_views.is_empty() {
        return;
    }
    let (exposure, gamma) = grading(o.brightness, settings.display.brightness_shader);
    for (e, grading) in &mut views {
        let set = |g: &mut ColorGrading| {
            g.global.exposure = exposure;
            g.shadows.gamma = gamma;
            g.midtones.gamma = gamma;
            g.highlights.gamma = gamma;
        };
        match grading {
            Some(mut g) => set(&mut g),
            None => {
                let mut g = ColorGrading::default();
                set(&mut g);
                commands.entity(e).insert(g);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn curves() {
        assert_eq!(grading(1.5, false), (0.0, 1.0));
        assert_eq!(grading(2.0, false), (0.5, 1.0));
        assert_eq!(grading(1.5, true), (0.0, 1.5));
        assert_eq!(grading(3.0, true), (0.0, 2.0));
    }
}
