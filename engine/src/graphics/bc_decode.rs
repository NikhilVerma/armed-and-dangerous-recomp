//! Block-compressed textures on GPUs without BC support: decoded to RGBA8 on load.
//!
//! The exported textures are DDS files in BC1 / BC2 / BC3 (DXT1 / DXT3 / DXT5: the original's banks, the Remaster's
//! retextures). Desktop GPUs sample those directly: every DX12 / Vulkan GPU on Windows, and Metal on macOS (Apple
//! silicon and Intel Macs alike; wgpu reports TEXTURE_COMPRESSION_BC). Bevy's image loader refuses a BC file when the
//! adapter lacks the feature (iPhones, older iPads, some Android GPUs), so there [`BcDecodePlugin`] puts a DDS loader
//! in its place that decodes BC1-3 into RGBA8 (same mip chain, same sRGB flag). Four times the texture memory, which
//! is why it is a fallback and not the rule. `AND_DECODE_BC=1` forces it on any GPU (tests of the fallback).
//!
//! API: [`BcDecodePlugin`]; [`decode`] (one BC image's levels into RGBA8).

use bevy::asset::io::Reader;
use bevy::asset::{AssetApp, AssetLoader, LoadContext};
use bevy::ecs::error::BevyError;
use bevy::image::{CompressedImageFormatSupport, CompressedImageFormats, Image, ImageLoaderSettings, ImageType};
use bevy::prelude::*;
use bevy::reflect::TypePath;
use bevy::render::render_resource::TextureFormat;

/// Registers [`DecodingDdsLoader`] for .dds when the GPU cannot sample BC formats (or `AND_DECODE_BC` is set).
pub struct BcDecodePlugin;

impl Plugin for BcDecodePlugin {
    fn build(&self, _app: &mut App) {}

    /// After the render plugin's `finish` (it adds the adapter's formats and Bevy's image loader; the loader
    /// registered last for an extension is the one used).
    fn finish(&self, app: &mut App) {
        let supported = app.world().get_resource::<CompressedImageFormatSupport>().map(|s| s.0);
        let forced = std::env::var_os("AND_DECODE_BC").is_some();
        let gpu_has_bc = supported.is_some_and(|s| s.contains(CompressedImageFormats::BC));
        // no render plugin (unit tests, a windowless tool): nothing loads images for a GPU
        if supported.is_none() && !forced {
            return;
        }
        if gpu_has_bc && !forced {
            return;
        }
        info!("graphics: BC textures decoded to RGBA8 on load ({})", if gpu_has_bc { "AND_DECODE_BC" } else { "the GPU has no BC support" });
        app.register_asset_loader(DecodingDdsLoader { supported: supported.unwrap_or(CompressedImageFormats::NONE) });
    }
}

/// Bevy's DDS loading, with BC1-3 images decoded to RGBA8.
#[derive(TypePath)]
struct DecodingDdsLoader {
    /// what the GPU samples itself (any other compressed format is refused, as Bevy's loader does)
    supported: CompressedImageFormats,
}

impl AssetLoader for DecodingDdsLoader {
    type Asset = Image;
    type Settings = ImageLoaderSettings;
    type Error = BevyError;

    async fn load(&self, reader: &mut dyn Reader, settings: &ImageLoaderSettings, load_context: &mut LoadContext<'_>) -> Result<Image, BevyError> {
        let mut bytes = Vec::new();
        reader.read_to_end(&mut bytes).await?;
        // parsed as if the GPU took BC, then decoded here
        let mut image = Image::from_buffer(&bytes, ImageType::Extension("dds"), self.supported | CompressedImageFormats::BC, settings.is_srgb, settings.sampler.clone(), settings.asset_usage)
            .map_err(|e| format!("{}: {e}", load_context.path().path().display()))?;
        if let Some(format) = rgba_of(image.texture_descriptor.format) {
            let size = image.texture_descriptor.size;
            let levels = image.texture_descriptor.mip_level_count;
            let data = image.data.take().unwrap_or_default();
            image.data = Some(decode(image.texture_descriptor.format, &data, size.width, size.height, size.depth_or_array_layers, levels).ok_or_else(|| format!("{}: BC data shorter than its mip chain", load_context.path().path().display()))?);
            image.texture_descriptor.format = format;
            static FIRST: std::sync::Once = std::sync::Once::new();
            FIRST.call_once(|| info!("graphics: first BC texture decoded: {} ({levels} levels)", load_context.path().path().display()));
        } else if !self.supported.supports(image.texture_descriptor.format) {
            return Err(format!("{}: {:?} is not supported by this GPU and not decoded", load_context.path().path().display(), image.texture_descriptor.format).into());
        }
        if let Some(format) = settings.texture_format {
            image.texture_descriptor.format = format;
        }
        Ok(image)
    }

    fn extensions(&self) -> &[&str] {
        &["dds"]
    }
}

/// The RGBA8 format a BC1-3 format decodes to (sRGB kept).
fn rgba_of(format: TextureFormat) -> Option<TextureFormat> {
    use TextureFormat::*;
    match format {
        Bc1RgbaUnorm | Bc2RgbaUnorm | Bc3RgbaUnorm => Some(Rgba8Unorm),
        Bc1RgbaUnormSrgb | Bc2RgbaUnormSrgb | Bc3RgbaUnormSrgb => Some(Rgba8UnormSrgb),
        _ => None,
    }
}

/// The BC variant of a format: its block size in bytes and how its alpha is stored.
#[derive(Clone, Copy, PartialEq, Debug)]
enum Bc {
    /// BC1 / DXT1: 8-byte blocks, colour only; the 3-colour mode has a transparent black
    Bc1,
    /// BC2 / DXT3: an explicit 4-bit alpha per pixel, then a BC1 colour block (always 4 colours)
    Bc2,
    /// BC3 / DXT5: an interpolated alpha block, then a BC1 colour block (always 4 colours)
    Bc3,
}

impl Bc {
    fn of(format: TextureFormat) -> Option<Bc> {
        use TextureFormat::*;
        match format {
            Bc1RgbaUnorm | Bc1RgbaUnormSrgb => Some(Bc::Bc1),
            Bc2RgbaUnorm | Bc2RgbaUnormSrgb => Some(Bc::Bc2),
            Bc3RgbaUnorm | Bc3RgbaUnormSrgb => Some(Bc::Bc3),
            _ => None,
        }
    }

    fn block_bytes(self) -> usize {
        if self == Bc::Bc1 { 8 } else { 16 }
    }
}

/// Decodes BC1-3 image data (layer-major: each layer's mips, largest first, as Bevy's DDS loader lays them out) into
/// RGBA8 in the same order. None for a format that is not BC1-3 or data shorter than the chain.
pub fn decode(format: TextureFormat, data: &[u8], width: u32, height: u32, layers: u32, levels: u32) -> Option<Vec<u8>> {
    let bc = Bc::of(format)?;
    let mut out = Vec::new();
    let mut at = 0usize;
    for _ in 0..layers.max(1) {
        for level in 0..levels.max(1) {
            let (w, h) = ((width >> level).max(1) as usize, (height >> level).max(1) as usize);
            let (bw, bh) = (w.div_ceil(4), h.div_ceil(4));
            let len = bw * bh * bc.block_bytes();
            let blocks = data.get(at..at + len)?;
            at += len;
            let start = out.len();
            out.resize(start + w * h * 4, 0);
            let pixels = &mut out[start..];
            for (i, block) in blocks.chunks_exact(bc.block_bytes()).enumerate() {
                let texels = decode_block(bc, block);
                let (bx, by) = (i % bw * 4, i / bw * 4);
                for (t, rgba) in texels.iter().enumerate() {
                    let (x, y) = (bx + t % 4, by + t / 4);
                    if x < w && y < h {
                        pixels[(y * w + x) * 4..][..4].copy_from_slice(rgba);
                    }
                }
            }
        }
    }
    Some(out)
}

/// One 4x4 block's texels, row by row.
fn decode_block(bc: Bc, block: &[u8]) -> [[u8; 4]; 16] {
    let colour = if bc == Bc::Bc1 { block } else { &block[8..] };
    let mut texels = colour_block(colour, bc == Bc::Bc1);
    match bc {
        Bc::Bc1 => {}
        Bc::Bc2 => {
            let bits = u64::from_le_bytes(block[..8].try_into().unwrap());
            for (t, texel) in texels.iter_mut().enumerate() {
                texel[3] = ((bits >> (4 * t)) & 0xf) as u8 * 17;
            }
        }
        Bc::Bc3 => {
            let (a0, a1) = (block[0] as u32, block[1] as u32);
            let mut palette = [a0, a1, 0, 0, 0, 0, 0, 255];
            if a0 > a1 {
                for i in 1..7 {
                    palette[i + 1] = ((7 - i as u32) * a0 + i as u32 * a1 + 3) / 7;
                }
            } else {
                for i in 1..5 {
                    palette[i + 1] = ((5 - i as u32) * a0 + i as u32 * a1 + 2) / 5;
                }
            }
            let mut idx = [0u8; 8];
            idx[..6].copy_from_slice(&block[2..8]);
            let bits = u64::from_le_bytes(idx);
            for (t, texel) in texels.iter_mut().enumerate() {
                texel[3] = palette[((bits >> (3 * t)) & 7) as usize] as u8;
            }
        }
    }
    texels
}

/// A BC1 colour block: two RGB565 end points and a 2-bit index per texel. With `three_colour_mode` (BC1 only), end
/// points in ascending order mean 3 colours and a transparent black; BC2 / BC3 always use 4 colours.
fn colour_block(block: &[u8], three_colour_mode: bool) -> [[u8; 4]; 16] {
    let c0 = u16::from_le_bytes([block[0], block[1]]);
    let c1 = u16::from_le_bytes([block[2], block[3]]);
    let rgb = |c: u16| {
        let (r, g, b) = ((c >> 11) & 31, (c >> 5) & 63, c & 31);
        [(r << 3 | r >> 2) as u32, (g << 2 | g >> 4) as u32, (b << 3 | b >> 2) as u32]
    };
    let (e0, e1) = (rgb(c0), rgb(c1));
    let mix = |a: u32, b: u32, d: u32| -> [u8; 4] {
        let ch = |k: usize| ((a * e0[k] + b * e1[k] + d / 2) / d) as u8;
        [ch(0), ch(1), ch(2), 255]
    };
    let palette = if c0 > c1 || !three_colour_mode {
        [mix(1, 0, 1), mix(0, 1, 1), mix(2, 1, 3), mix(1, 2, 3)]
    } else {
        [mix(1, 0, 1), mix(0, 1, 1), mix(1, 1, 2), [0, 0, 0, 0]]
    };
    let bits = u32::from_le_bytes(block[4..8].try_into().unwrap());
    std::array::from_fn(|t| palette[((bits >> (2 * t)) & 3) as usize])
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A BC1 block of pure red (0xf800) and pure blue (0x001f), indices 0, 1, 2, 3 along the first row.
    fn bc1_red_blue(indices: u32) -> [u8; 8] {
        let mut b = [0u8; 8];
        b[..2].copy_from_slice(&0xf800u16.to_le_bytes());
        b[2..4].copy_from_slice(&0x001fu16.to_le_bytes());
        b[4..].copy_from_slice(&indices.to_le_bytes());
        b
    }

    #[test]
    fn bc1_four_colours() {
        let t = decode_block(Bc::Bc1, &bc1_red_blue(0b11_10_01_00));
        assert_eq!(t[0], [255, 0, 0, 255]);
        assert_eq!(t[1], [0, 0, 255, 255]);
        assert_eq!(t[2], [170, 0, 85, 255]);
        assert_eq!(t[3], [85, 0, 170, 255]);
    }

    #[test]
    fn bc1_three_colours_and_transparent_black() {
        // c0 <= c1: blue then red
        let mut b = bc1_red_blue(0b11_10_01_00);
        b.swap(0, 2);
        b.swap(1, 3);
        let t = decode_block(Bc::Bc1, &b);
        assert_eq!(t[2], [128, 0, 128, 255]);
        assert_eq!(t[3], [0, 0, 0, 0]);
    }

    #[test]
    fn bc2_explicit_alpha() {
        let mut b = [0u8; 16];
        b[0] = 0xf0; // texel 0: 0, texel 1: 15
        b[8..].copy_from_slice(&bc1_red_blue(0));
        let t = decode_block(Bc::Bc2, &b);
        assert_eq!((t[0][3], t[1][3]), (0, 255));
        assert_eq!(t[0][..3], [255, 0, 0]);
    }

    #[test]
    fn bc3_interpolated_alpha() {
        let mut b = [0u8; 16];
        b[0] = 255;
        b[1] = 0;
        // texel 0: index 0 (a0 = 255), texel 1: index 1 (a1 = 0), texel 2: index 7 (1/7 a0 + 6/7 a1, rounded: 36)
        let bits: u64 = 1 << 3 | 7 << 6;
        b[2..8].copy_from_slice(&bits.to_le_bytes()[..6]);
        b[8..].copy_from_slice(&bc1_red_blue(0));
        let t = decode_block(Bc::Bc3, &b);
        assert_eq!((t[0][3], t[1][3], t[2][3]), (255, 0, 36));
    }

    /// A 2x2 image with a 1x1 mip: one block per level, cropped to the level's size.
    #[test]
    fn mip_chain_cropped_to_each_level() {
        let block = bc1_red_blue(0);
        let data = [block, block].concat();
        let out = decode(TextureFormat::Bc1RgbaUnormSrgb, &data, 2, 2, 1, 2).unwrap();
        assert_eq!(out.len(), (2 * 2 + 1) * 4);
        assert!(out.chunks(4).all(|p| p == [255, 0, 0, 255]));
        assert!(decode(TextureFormat::Bc1RgbaUnorm, &data[..8], 2, 2, 1, 2).is_none(), "short data");
        assert_eq!(rgba_of(TextureFormat::Bc3RgbaUnormSrgb), Some(TextureFormat::Rgba8UnormSrgb));
    }
}
