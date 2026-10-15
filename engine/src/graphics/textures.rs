//! Which texture file a material loads, and how it is sampled: the Remaster's AI retextures (prepared by
//! tools/remaster_textures.py into `<scene>/textures_remaster/<name>.dds`: colour-locked to the original, same alpha,
//! mips, BC1 / BC3) replace `<scene>/textures/<name>.dds` where one exists. The settings are chosen once at start-up;
//! the list of retextures is the loaded level's (listed again on a level change, campaign.rs): a level's materials
//! load their textures as it spawns.

use bevy::image::{ImageAddressMode, ImageFilterMode, ImageSamplerDescriptor};
use std::collections::HashSet;
use std::sync::RwLock;

struct TextureSet {
    /// the settings' remaster_textures and remaster_textures_exclude
    use_remaster: bool,
    exclude: Vec<String>,
    /// the loaded level's retextures (lower-case names)
    remaster: HashSet<String>,
    anisotropy: u16,
    anisotropic_fx: bool,
}

impl TextureSet {
    /// Lists the scene's remaster textures when the settings use them.
    fn list(&mut self, scene: &std::path::Path) {
        self.remaster = if self.use_remaster {
            std::fs::read_dir(scene.join("textures_remaster"))
                .map(|d| {
                    d.filter_map(|e| e.ok())
                        .filter_map(|e| e.file_name().to_str().and_then(|n| n.strip_suffix(".dds")).map(str::to_lowercase))
                        .filter(|n| !self.exclude.iter().any(|x| !x.is_empty() && n.contains(&x.to_lowercase())))
                        .collect()
                })
                .unwrap_or_default()
        } else {
            HashSet::new()
        };
        if self.use_remaster && self.remaster.is_empty() {
            bevy::log::warn!("graphics: no remaster textures in {} (run tools/remaster_textures.py)", scene.join("textures_remaster").display());
        }
    }
}

static SET: RwLock<Option<TextureSet>> = RwLock::new(None);

/// The settings and the launch level's remaster textures (once per run).
pub(super) fn init(scene: &std::path::Path, s: &super::GraphicsSettings) {
    let Ok(mut set) = SET.write() else { return };
    if set.is_some() {
        return;
    }
    let mut t = TextureSet { use_remaster: s.remaster_textures, exclude: s.remaster_textures_exclude.clone(), remaster: HashSet::new(), anisotropy: s.anisotropy.max(1), anisotropic_fx: s.anisotropic_fx };
    t.list(scene);
    *set = Some(t);
}

/// Another level is loaded (campaign.rs): its remaster textures, with the same settings.
pub fn set_level(scene: &std::path::Path) {
    if let Ok(mut set) = SET.write() {
        if let Some(t) = set.as_mut() {
            t.list(scene);
        }
    }
}

pub(super) fn count() -> usize {
    SET.read().ok().and_then(|s| s.as_ref().map(|s| s.remaster.len())).unwrap_or(0)
}

/// The asset path of a texture by its game name: the remaster's version when the preset uses it and one exists.
pub fn texture_path(name: &str) -> String {
    let lower = name.to_lowercase();
    if SET.read().is_ok_and(|s| s.as_ref().is_some_and(|s| s.remaster.contains(&lower))) {
        format!("textures_remaster/{lower}.dds")
    } else {
        format!("textures/{name}.dds")
    }
}

/// The world's texture sampler: repeat, trilinear, anisotropic as the settings say (16x in both presets: the
/// engine's look since 2026-10-13). `fx` samplers (decals, effects) are anisotropic only with `anisotropic_fx`.
pub fn world_sampler(address: ImageAddressMode, fx: bool) -> ImageSamplerDescriptor {
    let (aniso, on_fx) = SET.read().ok().and_then(|s| s.as_ref().map(|s| (s.anisotropy, s.anisotropic_fx))).unwrap_or((16, false));
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
