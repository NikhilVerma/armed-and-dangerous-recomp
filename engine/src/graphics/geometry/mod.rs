//! Geometry, surface detail and shadow tone: the old remaster's options that the Bevy engine lacked, ported (the old
//! D3D9 hook's Vulkan renderer: hook/enhance/render.cpp and shadows.cpp register them, src/rhi/vulkan implements
//! them). Each is a small piece with its provenance:
//!
//! - `tessellation*`: [`tessellation`] (vk_tessellation.cpp): Phong-tessellated characters, refined once at load.
//! - `terrain_detail*`: the terrain normal map sampled again at a finer tiling (vk_shader_translate.cpp), in
//!   gamematerial.wesl `terrain_detail`.
//! - `lod_max`, `lod_scale`: [`lod`] (Object_SelectLod 0x00452f50 with the old switches).
//! - `alpha_coverage`: alpha textures cover by their alpha with MSAA's alpha-to-coverage instead of blending (the old's
//!   vk_fixed_function.cpp FinalBody gave alpha-tested edges a coverage under its AA). Under TAA the engine's foliage
//!   blends (the original's SRCALPHA / INVSRCALPHA), which already has soft edges: the old's per-frame noise there was
//!   tried and dropped (2026-10-19, work/shots/port-geom/cmp_alpha_coverage_zoom.png: on the semi-transparent icicles
//!   and twigs Bevy's TAA clamps the noise instead of averaging it, a visible screen door; the old measured no gain
//!   under its TAA either, docs/subsystems/remaster-aa.md).
//! - `aa_mip_bias`: the texture mip bias while TAA runs (vk_pipeline.cpp), Bevy's `MipBias` read by the game material.
//! - `shadow_strength`: how dark a full shadow is, tinted by the sky (vk_shadows.cpp), in place of the level's flat
//!   shadow colour.
//!
//! Not ported: `cull_radius` (shadows.cpp, "Shadow caster range"). The old shadow maps replayed the main scene's draws,
//! so a caster the camera's frustum culled lost its shadow; the setting kept casters within that range. Bevy culls
//! the sun's casters against each cascade's own frustum (bevy_light check_dir_light_mesh_visibility, no near plane),
//! never against the camera's, so casters behind and beside the view always cast, as the original's projected shadows
//! do (shadows.md: every shadow-list object): the remaster's 400 holds at any range, with nothing to set.
//!
//! The values reach the game materials through `GameMaterialParams::geometry` ([`material_values`]), the cameras
//! through `MipBias` ([`apply_mip_bias`]); tessellation and LOD are plugins of their own.

pub mod lod;
pub mod tessellation;

use bevy::prelude::*;
use bevy::render::camera::MipBias;
use serde::{Deserialize, Serialize};

use super::{AntiAliasing, GraphicsSettings};

/// The settings (flattened into [`GraphicsSettings`]: options.json `graphics` names them directly).
#[derive(Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct GeometrySettings {
    /// Phong-tessellated skinned meshes (characters), refined once at load ([`tessellation`])
    pub tessellation: bool,
    /// segments per edge (each triangle becomes level^2; the old: screen-adaptive up to 8)
    pub tessellation_level: u32,
    /// 0 flat .. 1 the full curved surface (the old tess_shape)
    pub tessellation_shape: f32,
    /// edges within this angle (degrees) curve fully (the old tess_smooth_angle)
    pub tessellation_smooth_angle: f32,
    /// edges beyond this angle stay straight (the old tess_sharp_angle); both 0 = every edge curves
    pub tessellation_sharp_angle: f32,
    /// the terrain's normal map sampled again at this many times its tiling, its slope added (0 = the original's)
    pub terrain_detail: f32,
    /// how much of the detail layer's slope is added (the old terrain_detail_strength)
    pub terrain_detail_strength: f32,
    /// every object draws its finest LOD at all distances
    pub lod_max: bool,
    /// the LOD distances multiplied by this (finer models kept further out; 1 = the original's)
    pub lod_scale: f32,
    /// alpha textures cover by their alpha with MSAA (alpha-to-coverage) instead of blending; no effect under TAA / SMAA
    pub alpha_coverage: bool,
    /// texture mip bias while TAA runs (negative = sharper; 0 = none)
    pub aa_mip_bias: f32,
    /// how dark a full shadow is, the sky's blue left in it (0 = the level's shadow colour and blend, the original's)
    pub shadow_strength: f32,
}

impl GeometrySettings {
    /// The original's look: none of these.
    pub fn original() -> Self {
        GeometrySettings {
            tessellation: false,
            tessellation_level: 3,
            tessellation_shape: 0.75,
            tessellation_smooth_angle: 25.0,
            tessellation_sharp_angle: 50.0,
            terrain_detail: 0.0,
            terrain_detail_strength: 0.6,
            lod_max: false,
            lod_scale: 1.0,
            alpha_coverage: false,
            aa_mip_bias: 0.0,
            shadow_strength: 0.0,
        }
    }

    /// The old remaster's defaults (RemasterRegister's remaster values), except: tessellation on (the old was off
    /// for its seam cracks, which this port's welded surface does not have: docs/player-feedback.md 1-3), and
    /// lod_max on (the human's decision of 2026-10-13: the finest everywhere; the old's was off for its CPU cost).
    pub fn remaster() -> Self {
        GeometrySettings {
            tessellation: true,
            terrain_detail: 8.0,
            lod_max: true,
            lod_scale: 2.0,
            alpha_coverage: true,
            aa_mip_bias: -1.0,
            shadow_strength: 0.55,
            ..Self::original()
        }
    }
}

impl Default for GeometrySettings {
    fn default() -> Self {
        Self::remaster()
    }
}

/// Whether alpha textures cover by MSAA's alpha-to-coverage (`alpha_coverage` with MSAA).
pub fn alpha_to_coverage(s: &GraphicsSettings) -> bool {
    s.geometry.alpha_coverage && s.anti_aliasing == AntiAliasing::Msaa
}

/// The game materials' `geometry` parameters: x the terrain detail scale (0 off), y its strength, z 1 = alpha-to-
/// coverage, w the shadow strength (0: the level's shadow colour).
pub fn material_values(s: &GraphicsSettings) -> Vec4 {
    let g = &s.geometry;
    let detail = if g.terrain_detail > 1.0 { g.terrain_detail } else { 0.0 };
    Vec4::new(detail, g.terrain_detail_strength.max(0.0), alpha_to_coverage(s) as u32 as f32, g.shadow_strength.clamp(0.0, 1.0))
}

/// [`material_values`] for one material: the alpha cover only on an alpha texture (not a translucent record, whose
/// opacity < 1 keeps it blended), zero elsewhere.
pub fn for_material(values: Vec4, alpha_textured: bool, opacity: f32) -> Vec4 {
    if alpha_textured && opacity >= 1.0 { values } else { values.with_z(0.0) }
}

/// The numeric settings' steppers on the Graphics page's "Detail and Shadows" (gamemenu.rs): setting, first and last
/// value, positions. The old remaster's slider ranges and steps (RemasterRegister): terrain_detail 0..16 by 2,
/// lod_scale 1..8 by 0.5, aa_mip_bias -2..0 by 0.25, shadow_strength 0.1..1 by 0.05 (0 here: the level's colour);
/// tessellation_level replaces the old's screen-space level (up to 8).
pub const STEPPERS: [(&str, f32, f32, usize); 5] = [
    ("tessellation_level", 2.0, 6.0, 5),
    ("terrain_detail", 0.0, 16.0, 9),
    ("lod_scale", 1.0, 8.0, 15),
    ("aa_mip_bias", -2.0, 0.0, 9),
    ("shadow_strength", 0.0, 1.0, 21),
];

pub struct GeometryPlugin;

impl Plugin for GeometryPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins((tessellation::TessellationPlugin, lod::LodPlugin))
            .add_systems(PostUpdate, apply_mip_bias.after(super::apply_cameras))
            .add_systems(Update, report.run_if(resource_exists::<crate::playtest::PlayState>));
    }
}

/// For playtests (`geometry.*` expect keys): what reached the renderer. Tessellated parts, objects switching LOD and
/// those drawing a coarser one, the camera's mip bias (none: null), the alpha textures with alpha-to-coverage,
/// and the largest terrain detail and shadow strength over the game materials.
#[allow(clippy::type_complexity)]
fn report(
    mut state: ResMut<crate::playtest::PlayState>,
    tess: Query<(), With<tessellation::Tessellated>>,
    lods: Query<&lod::LodParts>,
    cams: Query<Option<&MipBias>, With<Camera3d>>,
    (solid, alpha): (Res<Assets<crate::gamematerial::GameMaterial>>, Res<Assets<crate::gamematerial::AlphaTestedGameMaterial>>),
) {
    let cover = alpha.iter().filter(|(_, m)| m.alpha_mode() == AlphaMode::AlphaToCoverage).count();
    let params = solid.iter().map(|(_, m)| m.params.geometry).chain(alpha.iter().map(|(_, m)| m.params.geometry));
    let (detail, shadow) = params.fold((0.0f32, 0.0f32), |(d, s), g| (d.max(g.x), s.max(g.w)));
    let bias = cams.iter().next().flatten().map(|b| b.0);
    state.0.insert(
        "geometry".into(),
        serde_json::json!({
            "tessellated": tess.iter().count(),
            "lod_objects": lods.iter().count(),
            "lod_coarser": lods.iter().filter(|l| l.coarser()).count(),
            "mip_bias": bias,
            "alpha_to_coverage": cover,
            "terrain_detail": detail,
            "shadow_strength": (shadow as f64 * 100.0).round() / 100.0,
        }),
    );
}

/// The cameras' mip bias: `aa_mip_bias` while TAA runs (Bevy's TAA requires a MipBias, -1 by default; the game
/// material samples with it, gamematerial.wesl), none otherwise.
fn apply_mip_bias(
    mut commands: Commands,
    s: Res<GraphicsSettings>,
    cams: Query<(Entity, Option<&MipBias>), With<Camera3d>>,
    new_cams: Query<(), Added<Camera>>,
) {
    if !s.is_changed() && new_cams.is_empty() {
        return;
    }
    for (e, bias) in &cams {
        if s.anti_aliasing == AntiAliasing::Taa {
            if bias.is_none_or(|b| b.0 != s.geometry.aa_mip_bias) {
                commands.entity(e).insert(MipBias(s.geometry.aa_mip_bias));
            }
        } else if bias.is_some() {
            commands.entity(e).remove::<MipBias>();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn presets_and_values() {
        let o = GraphicsSettings::original();
        assert_eq!(material_values(&o), Vec4::new(0.0, 0.6, 0.0, 0.0));
        let r = GraphicsSettings::remaster();
        assert_eq!(material_values(&r), Vec4::new(8.0, 0.6, 0.0, 0.55));
        let msaa = GraphicsSettings { anti_aliasing: AntiAliasing::Msaa, ..GraphicsSettings::remaster() };
        assert_eq!(material_values(&msaa).z, 1.0);
        assert!(r.geometry.tessellation && r.geometry.lod_max && !o.geometry.tessellation && !o.geometry.lod_max);
        // flattened: the options name the settings directly, and a change applies over the preset
        let mut m = serde_json::Map::new();
        m.insert("terrain_detail".into(), 4.into());
        m.insert("alpha_coverage".into(), false.into());
        let s = crate::graphics::resolve(crate::graphics::Preset::Remaster, &m);
        assert_eq!((s.geometry.terrain_detail, s.geometry.alpha_coverage, s.geometry.shadow_strength), (4.0, false, 0.55));
        assert!(!alpha_to_coverage(&s));
    }
}
