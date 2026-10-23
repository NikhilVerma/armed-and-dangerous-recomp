//! Height fog: the old remaster's modern fog ([render] fog_modern / fog_density / fog_height / fog_sun, registered
//! in archive/hook/enhance/shadows.cpp; the maths in archive/src/rhi/vulkan/vk_fog.cpp `ModernFogGlsl` and vk_device.cpp
//! `ModernFogUniforms`). Exponential height fog per pixel in place of the level's linear fog, on every surface the
//! level's fog reaches (the game material; the sky stays unfogged as in the original), with the level's own fog
//! colour and range: level with the camera, half the light gets through half way through the fog range. Valleys get
//! thicker, peaks thinner, and the fog glows towards the sun.
//!
//! The maths is in height_fog.wesl (`height_fog`, called by gamematerial.wesl). Per material: the settings and the
//! level's reference height (`GameMaterialParams::height_fog`); the reference is the camera's height on the level's
//! first frame, as the old renderer took it when the level finished loading (`GameLights::fog_reference`).

use crate::gamematerial::{Coverage, GameLights, GameMaterial};
use bevy::prelude::*;
use serde::{Deserialize, Serialize};

/// The height fog's settings (options.json keys as the old and.ini [render] keys).
#[derive(Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct HeightFogSettings {
    /// exponential height fog in place of the level's linear fog
    pub fog_modern: bool,
    /// density scale: 1 = half the light through half way through the level's fog range (old slider 0.25..3)
    pub fog_density: f32,
    /// density falloff per unit of height above the reference (old slider 0..0.01)
    pub fog_height: f32,
    /// forward-scattering glow towards the sun (old slider 0..2)
    pub fog_sun: f32,
}

impl HeightFogSettings {
    /// The level's linear fog (the old remaster's defaults kept for when it is switched on).
    pub fn original() -> Self {
        HeightFogSettings { fog_modern: false, fog_density: 1.0, fog_height: 0.002, fog_sun: 0.6 }
    }

    /// The old remaster's defaults: on.
    pub fn remaster() -> Self {
        HeightFogSettings { fog_modern: true, ..Self::original() }
    }

    /// The game material's `height_fog` parameters (x 0 = the level's linear fog), at the level's reference height
    /// (0 until the level's first frame).
    pub fn material_params(&self, reference: Option<f32>) -> Vec4 {
        if !self.fog_modern {
            return Vec4::ZERO;
        }
        Vec4::new(self.fog_density.max(1e-3), self.fog_height.max(0.0), self.fog_sun.max(0.0), reference.unwrap_or(0.0))
    }
}

impl Default for HeightFogSettings {
    fn default() -> Self {
        Self::remaster()
    }
}

pub struct HeightFogPlugin;

impl Plugin for HeightFogPlugin {
    fn build(&self, app: &mut App) {
        bevy::shader::load_shader_library!(app, "height_fog.wesl");
        app.add_systems(
            PostUpdate,
            (
                record_reference.after(bevy::transform::TransformSystems::Propagate),
                (apply_to_materials::<crate::gamematerial::Solid>, apply_to_materials::<crate::gamematerial::AlphaTested>),
            )
                .chain(),
        );
    }
}

/// The level's reference height: the 3D camera's on the level's first frame (GameLights is per level).
fn record_reference(lights: Option<ResMut<GameLights>>, cams: Query<(&Camera, &GlobalTransform), With<Camera3d>>) {
    let Some(mut lights) = lights else { return };
    if lights.fog_reference.is_some() {
        return;
    }
    if let Some((_, t)) = cams.iter().find(|(c, _)| c.is_active) {
        lights.fog_reference = Some(t.translation().y);
    }
}

fn apply_to_materials<C: Coverage>(
    s: Res<super::GraphicsSettings>,
    lights: Option<Res<GameLights>>,
    mut game: ResMut<Assets<GameMaterial<C>>>,
    mut events: MessageReader<AssetEvent<GameMaterial<C>>>,
) {
    let reference = lights.as_ref().and_then(|l| l.fog_reference);
    let changed = s.is_changed() || lights.as_ref().is_some_and(|l| l.is_changed());
    let want = s.height_fog.material_params(reference);
    super::set_game_material_param(changed, &mut game, &mut events, |p| &mut p.height_fog, |_| want);
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Off: the level's linear fog; on: the settings and the reference height reach the material.
    #[test]
    fn material_params() {
        assert_eq!(HeightFogSettings::original().material_params(Some(50.0)), Vec4::ZERO);
        let p = HeightFogSettings::remaster().material_params(Some(50.0));
        assert_eq!(p, Vec4::new(1.0, 0.002, 0.6, 50.0));
    }
}
