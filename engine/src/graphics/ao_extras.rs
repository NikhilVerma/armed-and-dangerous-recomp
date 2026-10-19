//! Ambient occlusion as the old remaster had it: XeGTAO with its settings, then the old apply pass's shaping
//! ([render] ao_power, ao_falloff, ao_contact, ao_snow, registered in hook/enhance/shadows.cpp; ao_thickness from
//! and.ini; the passes in src/rhi/vulkan/vk_gtao.cpp `GtaoHlsl` / `GtaoApplyFragmentGlsl`, the settings in
//! vk_device.cpp `Ambient`). On top of the existing switches (`ambient_occlusion`, `ao_quality`, `ao_radius`,
//! `ao_strength`, `ao_exempt`, graphics/mod.rs).
//!
//! - The term: xegtao.wesl, XeGTAO's main pass in Bevy's SSAO pass. [`AoExtrasPlugin`] puts that source in place of
//!   Bevy's own ssao.wesl (the same shader asset, so Bevy's SSAO pipeline compiles it, with its depth prefilter and
//!   denoiser), with the settings in its constants, again whenever they change: `ao_power` (XeGTAO's
//!   FinalValuePower: contrast), `ao_falloff` (its FalloffRange: a sample's occlusion fades out over that share of the
//!   radius), the thickness limit through Bevy's `constant_object_thickness` (`ao_thickness`, graphics/mod.rs).
//! - The shaping, in the game material (ao_extras.wesl): the albedo-aware multi-bounce, `ao_snow` (the share of the
//!   occlusion snow keeps: the `ao_exempt` surfaces by name and snow-white albedo), `ao_contact` (where the term is
//!   clearly occluded, that share of it darkens the sunlit colour too: feet on snow).
//!
//! Not ported: half-resolution AO ([render] ao_half) and the separate smoothed-normal pass (Bevy's SSAO reads the
//! prepass normals, which are the meshes' smooth ones).

use crate::gamematerial::{Coverage, GameMaterial};
use bevy::prelude::*;
use serde::{Deserialize, Serialize};

/// The AO settings the old remaster added (options.json keys as the old and.ini [render] keys).
#[derive(Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct AoExtras {
    /// contrast: the occlusion term raised to this power (XeGTAO's FinalValuePower; old slider 1..5)
    pub ao_power: f32,
    /// the share of the AO radius over which a sample's occlusion fades out with its distance (old slider 0.1..1)
    pub ao_falloff: f32,
    /// contact occlusion: the share of a clearly occluded term that darkens the sunlit colour too (old slider 0..1)
    pub ao_contact: f32,
    /// the share of the occlusion kept on snow (old slider 0..1; 0 = none)
    pub ao_snow: f32,
    /// occluders are this thick (game units), not infinitely deep: the ground behind a character is not darkened as if
    /// a board stood there (old and.ini ao_thickness; 0 = off)
    pub ao_thickness: f32,
}

impl AoExtras {
    /// The old remaster's values without the contact term (AO itself is off in the Original preset).
    pub fn original() -> Self {
        AoExtras { ao_power: 3.0, ao_falloff: 0.5, ao_contact: 0.0, ao_snow: 0.25, ao_thickness: 3.0 }
    }

    /// The old remaster's defaults.
    pub fn remaster() -> Self {
        AoExtras { ao_contact: 0.6, ..Self::original() }
    }

    /// The game material's `ao_extras` parameters (ao_extras.wesl `remaster_occlusion`).
    pub fn material_params(&self, snow_surface: bool) -> Vec4 {
        Vec4::new(self.ao_contact.clamp(0.0, 1.0), self.ao_snow.clamp(0.0, 1.0), if snow_surface { 1.0 } else { 0.0 }, 0.0)
    }

    /// XeGTAO's constants in xegtao.wesl.
    fn xegtao(&self) -> XeGtaoConstants {
        XeGtaoConstants { falloff: self.ao_falloff.clamp(0.01, 1.0), power: self.ao_power.max(0.01) }
    }
}

impl Default for AoExtras {
    fn default() -> Self {
        Self::remaster()
    }
}

/// The settings written into xegtao.wesl's constants.
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct XeGtaoConstants {
    pub falloff: f32,
    pub power: f32,
}

/// Bevy's SSAO shader (bevy_pbr ssao/mod.rs `load_embedded_asset!(world, "ssao.wesl")`).
const BEVY_SSAO: &str = "embedded://bevy_pbr/ssao/ssao.wesl";
/// The old remaster's XeGTAO in Bevy's SSAO pass.
const XEGTAO: &str = include_str!("xegtao.wesl");
const FALLOFF_LINE: &str = "const FALLOFF_RANGE: f32 = 0.5;";
const POWER_LINE: &str = "const FINAL_VALUE_POWER: f32 = 3.0;";

pub struct AoExtrasPlugin;

impl Plugin for AoExtrasPlugin {
    fn build(&self, app: &mut App) {
        bevy::shader::load_shader_library!(app, "ao_extras.wesl");
        app.init_resource::<SsaoSource>().add_systems(
            PostUpdate,
            (apply_to_materials::<crate::gamematerial::Solid>, apply_to_materials::<crate::gamematerial::AlphaTested>, apply_xegtao),
        );
    }
}

/// Bevy's SSAO shader asset and the XeGTAO constants its source has.
#[derive(Resource, Default)]
pub struct SsaoSource {
    shader: Option<Handle<Shader>>,
    applied: Option<XeGtaoConstants>,
}

impl SsaoSource {
    /// The XeGTAO constants in Bevy's SSAO pass (None: Bevy's own estimator still).
    pub fn applied(&self) -> Option<XeGtaoConstants> {
        self.applied
    }
}

/// The XeGTAO source with these constants.
fn xegtao_source(c: XeGtaoConstants) -> String {
    XEGTAO
        .replace(FALLOFF_LINE, &format!("const FALLOFF_RANGE: f32 = {:?};", c.falloff))
        .replace(POWER_LINE, &format!("const FINAL_VALUE_POWER: f32 = {:?};", c.power))
}

/// Puts XeGTAO in place of Bevy's SSAO source once Bevy's own has loaded (a later load would overwrite ours) and
/// again whenever the constants change; Bevy recompiles its SSAO pipeline.
fn apply_xegtao(s: Res<super::GraphicsSettings>, mut source: ResMut<SsaoSource>, assets: Res<AssetServer>, mut shaders: ResMut<Assets<Shader>>) {
    if !s.ambient_occlusion {
        return;
    }
    let want = s.ao_extras.xegtao();
    if source.applied == Some(want) {
        return;
    }
    let handle = source.shader.get_or_insert_with(|| assets.load::<Shader>(BEVY_SSAO)).clone();
    if !assets.is_loaded(&handle) {
        return;
    }
    if shaders.insert(&handle, Shader::from_wesl(xegtao_source(want), BEVY_SSAO)).is_ok() {
        info!("graphics: SSAO is XeGTAO (falloff {}, power {})", want.falloff, want.power);
        source.applied = Some(want);
    }
}

fn apply_to_materials<C: Coverage>(
    s: Res<super::GraphicsSettings>,
    mut game: ResMut<Assets<GameMaterial<C>>>,
    mut events: MessageReader<AssetEvent<GameMaterial<C>>>,
) {
    let extras = &s.ao_extras;
    super::set_game_material_param(s.is_changed(), &mut game, &mut events, |p| &mut p.ao_extras, |m| extras.material_params(m.ao_snow_surface));
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The XeGTAO source carries Bevy's entry point and the constants, and the settings replace the defaults.
    #[test]
    fn xegtao_constants() {
        assert!(XEGTAO.contains(FALLOFF_LINE) && XEGTAO.contains(POWER_LINE));
        assert!(XEGTAO.contains("fn ssao("));
        let s = xegtao_source(XeGtaoConstants { falloff: 0.25, power: 2.0 });
        assert!(s.contains("const FALLOFF_RANGE: f32 = 0.25;") && s.contains("const FINAL_VALUE_POWER: f32 = 2.0;"));
        assert!(!s.contains(FALLOFF_LINE) && !s.contains(POWER_LINE));
    }

    #[test]
    fn material_params() {
        assert_eq!(AoExtras::remaster().material_params(true), Vec4::new(0.6, 0.25, 1.0, 0.0));
        assert_eq!(AoExtras::original().material_params(false).x, 0.0);
    }
}
