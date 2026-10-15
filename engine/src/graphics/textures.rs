//! Which texture file a material loads, and how it is sampled: the Remaster's AI retextures (prepared by
//! tools/remaster_textures.py into `<scene>/textures_remaster/<name>.dds`: colour-locked to the original, same alpha,
//! mips, BC1 / BC3) replace `<scene>/textures/<name>.dds` where one exists. Chosen once at start-up: a level's
//! materials load their textures as it spawns.

use bevy::image::{ImageAddressMode, ImageFilterMode, ImageSamplerDescriptor};
use std::collections::HashSet;
use std::sync::OnceLock;

struct TextureSet {
    remaster: HashSet<String>,
    anisotropy: u16,
    anisotropic_fx: bool,
}

static SET: OnceLock<TextureSet> = OnceLock::new();

/// Lists the scene's remaster textures when the settings use them (once per run).
pub(super) fn init(scene: &std::path::Path, s: &super::GraphicsSettings) {
    let remaster = if s.remaster_textures {
        std::fs::read_dir(scene.join("textures_remaster"))
            .map(|d| {
                d.filter_map(|e| e.ok())
                    .filter_map(|e| e.file_name().to_str().and_then(|n| n.strip_suffix(".dds")).map(str::to_lowercase))
                    .filter(|n| !s.remaster_textures_exclude.iter().any(|x| !x.is_empty() && n.contains(&x.to_lowercase())))
                    .collect()
            })
            .unwrap_or_default()
    } else {
        HashSet::new()
    };
    if s.remaster_textures && remaster.is_empty() {
        bevy::log::warn!("graphics: no remaster textures in {} (run tools/remaster_textures.py)", scene.join("textures_remaster").display());
    }
    let _ = SET.set(TextureSet { remaster, anisotropy: s.anisotropy.max(1), anisotropic_fx: s.anisotropic_fx });
}

pub(super) fn count() -> usize {
    SET.get().map(|s| s.remaster.len()).unwrap_or(0)
}

/// The asset path of a texture by its game name: the remaster's version when the preset uses it and one exists.
pub fn texture_path(name: &str) -> String {
    let lower = name.to_lowercase();
    if SET.get().is_some_and(|s| s.remaster.contains(&lower)) {
        format!("textures_remaster/{lower}.dds")
    } else {
        format!("textures/{name}.dds")
    }
}

/// The world's texture sampler: repeat, trilinear, anisotropic as the settings say (16x in both presets: the
/// engine's look since 2026-10-13). `fx` samplers (decals, effects) are anisotropic only with `anisotropic_fx`.
pub fn world_sampler(address: ImageAddressMode, fx: bool) -> ImageSamplerDescriptor {
    let (aniso, on_fx) = SET.get().map(|s| (s.anisotropy, s.anisotropic_fx)).unwrap_or((16, false));
    ImageSamplerDescriptor {
        address_mode_u: address,
        address_mode_v: address,
        mag_filter: ImageFilterMode::Linear,
        min_filter: ImageFilterMode::Linear,
        mipmap_filter: ImageFilterMode::Linear,
        anisotropy_clamp: if fx && !on_fx { 1 } else { aniso },
        ..Default::default()
    }
}

/// The app's default sampler with anisotropy (Bevy's default is linear without it).
pub(super) fn fx_sampler(anisotropy: u16) -> ImageSamplerDescriptor {
    ImageSamplerDescriptor { anisotropy_clamp: anisotropy, ..ImageSamplerDescriptor::linear() }
}
