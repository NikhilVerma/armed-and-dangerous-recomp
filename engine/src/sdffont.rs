//! Crisp text at any resolution from the original's bitmap font (docs/player-feedback.md 24).
//!
//! The game's only font is a 256 x 512 atlas of 25-pixel glyphs (ui/font.png, exported by
//! tools/remaster_export_ui.py from txtr mainfontid), drawn at 640 x 480 in the original. Sampled as a plain texture
//! at 4K it is magnified 4.5 times and the bilinear filter smears every edge over ~4 pixels: the player's "fonts are
//! blurry". Here the atlas becomes a signed distance field once, at load: the glyph coverage is upsampled
//! (Catmull-Rom, [`UPSCALE`] times), its 0.5 level taken as the glyph's outline, and the distance to that outline
//! stored per pixel. The shader (sdffont.wesl) draws the outline anti-aliased over one screen pixel, so the same
//! glyph shapes stay sharp at any size, and the layout (font.json metrics, uv rects) is unchanged.
//!
//! API: [`SdfFontPlugin`]; [`FontMaterial`] (the text's colour times its vertex colours); [`font_field`] (the field
//! image from the atlas file, computed once per process).

use bevy::asset::RenderAssetUsages;
use bevy::image::{CompressedImageFormats, ImageSampler, ImageSamplerDescriptor, ImageType};
use bevy::prelude::*;
use bevy::render::render_resource::{AsBindGroup, Extent3d, TextureDimension, TextureFormat};
use bevy::shader::ShaderRef;
use bevy::sprite_render::{AlphaMode2d, Material2d, Material2dPlugin};
use std::path::Path;
use std::sync::OnceLock;

/// Field pixels per atlas pixel.
pub const UPSCALE: usize = 4;
/// The distance (field pixels) that maps to the ends of the stored range: 2 atlas pixels each side of the outline,
/// enough for the edge's anti-aliasing when the text is drawn smaller than the atlas (a 640 x 372 test window).
const SPREAD: f32 = 2.0 * UPSCALE as f32;

pub struct SdfFontPlugin;

impl Plugin for SdfFontPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "sdffont.wesl");
        app.add_plugins(Material2dPlugin::<FontMaterial>::default());
    }
}

/// Text drawn from the font's distance field: `color` times the mesh's vertex colours.
#[derive(Asset, AsBindGroup, TypePath, Debug, Clone)]
pub struct FontMaterial {
    #[uniform(0)]
    pub color: LinearRgba,
    #[texture(1)]
    #[sampler(2)]
    pub field: Handle<Image>,
}

impl Material2d for FontMaterial {
    fn fragment_shader() -> ShaderRef {
        ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("sdffont.wesl")).with_source("embedded"))
    }
    fn alpha_mode(&self) -> AlphaMode2d {
        AlphaMode2d::Blend
    }
}

/// The distance field of the font atlas at `png` (R8: 0.5 on the outline, more inside), linear sampled, no mips.
/// Computed once per process (a level restart reuses it); None when the file cannot be read.
pub fn font_field(png: &Path) -> Option<Image> {
    static FIELD: OnceLock<Option<(u32, u32, Vec<u8>)>> = OnceLock::new();
    let (w, h, data) = FIELD
        .get_or_init(|| {
            let t = std::time::Instant::now();
            let bytes = std::fs::read(png).map_err(|e| warn!("font: {}: {e}", png.display())).ok()?;
            let img = Image::from_buffer(&bytes, ImageType::Extension("png"), CompressedImageFormats::NONE, false, ImageSampler::Default, RenderAssetUsages::MAIN_WORLD)
                .map_err(|e| warn!("font: {}: {e}", png.display()))
                .ok()?;
            let (w, h) = (img.width() as usize, img.height() as usize);
            let rgba = img.data.as_deref().filter(|d| d.len() == w * h * 4)?;
            let alpha: Vec<f32> = rgba.chunks_exact(4).map(|p| p[3] as f32 / 255.0).collect();
            let field = distance_field(&alpha, w, h);
            info!("font: distance field {}x{} in {:.0} ms", w * UPSCALE, h * UPSCALE, t.elapsed().as_secs_f32() * 1000.0);
            Some(((w * UPSCALE) as u32, (h * UPSCALE) as u32, field))
        })
        .clone()?;
    let mut image = Image::new(
        Extent3d { width: w, height: h, depth_or_array_layers: 1 },
        TextureDimension::D2,
        data,
        TextureFormat::R8Unorm,
        RenderAssetUsages::RENDER_WORLD,
    );
    image.sampler = ImageSampler::Descriptor(ImageSamplerDescriptor::linear());
    Some(image)
}

/// Coverage `alpha` (w x h, 0..1) -> the signed distance field at UPSCALE times the size, as bytes (0.5 * 255 on the
/// outline, larger inside, [`SPREAD`] field pixels to either end).
fn distance_field(alpha: &[f32], w: usize, h: usize) -> Vec<u8> {
    let (fw, fh) = (w * UPSCALE, h * UPSCALE);
    // the outline: the coverage's 0.5 level, upsampled smoothly (Catmull-Rom) so diagonals and curves stay round
    let at = |x: isize, y: isize| alpha[y.clamp(0, h as isize - 1) as usize * w + x.clamp(0, w as isize - 1) as usize];
    let weights = |t: f32| {
        let (t2, t3) = (t * t, t * t * t);
        [(-t3 + 2.0 * t2 - t) * 0.5, (3.0 * t3 - 5.0 * t2 + 2.0) * 0.5, (-3.0 * t3 + 4.0 * t2 + t) * 0.5, (t3 - t2) * 0.5]
    };
    let mut inside = vec![false; fw * fh];
    for fy in 0..fh {
        let sy = (fy as f32 + 0.5) / UPSCALE as f32 - 0.5;
        let (y0, wy) = (sy.floor() as isize, weights(sy - sy.floor()));
        for fx in 0..fw {
            let sx = (fx as f32 + 0.5) / UPSCALE as f32 - 0.5;
            let (x0, wx) = (sx.floor() as isize, weights(sx - sx.floor()));
            let mut v = 0.0;
            for (j, wyj) in wy.iter().enumerate() {
                for (i, wxi) in wx.iter().enumerate() {
                    v += wyj * wxi * at(x0 - 1 + i as isize, y0 - 1 + j as isize);
                }
            }
            inside[fy * fw + fx] = v >= 0.5;
        }
    }
    // squared distances to the nearest pixel of the other kind, then signed (half a pixel puts the outline between
    // the two kinds)
    let to_inside = squared_distances(&inside, fw, fh, true);
    let to_outside = squared_distances(&inside, fw, fh, false);
    (0..fw * fh)
        .map(|i| {
            let d = if inside[i] { to_outside[i].sqrt() - 0.5 } else { -(to_inside[i].sqrt() - 0.5) };
            ((0.5 + d / (2.0 * SPREAD)).clamp(0.0, 1.0) * 255.0).round() as u8
        })
        .collect()
}

/// The exact squared Euclidean distance of every pixel to the nearest pixel where `mask == target` (Felzenszwalb and
/// Huttenlocher's separable transform: columns, then rows).
fn squared_distances(mask: &[bool], w: usize, h: usize, target: bool) -> Vec<f32> {
    const FAR: f32 = 1.0e12;
    let mut d: Vec<f32> = mask.iter().map(|&m| if m == target { 0.0 } else { FAR }).collect();
    let mut line = vec![0.0f32; w.max(h)];
    let mut out = vec![0.0f32; w.max(h)];
    for x in 0..w {
        for y in 0..h {
            line[y] = d[y * w + x];
        }
        transform_1d(&line[..h], &mut out[..h]);
        for y in 0..h {
            d[y * w + x] = out[y];
        }
    }
    for y in 0..h {
        line[..w].copy_from_slice(&d[y * w..(y + 1) * w]);
        transform_1d(&line[..w], &mut out[..w]);
        d[y * w..(y + 1) * w].copy_from_slice(&out[..w]);
    }
    d
}

/// One dimension of the transform: the lower envelope of the parabolas (q - p)^2 + f(p).
fn transform_1d(f: &[f32], out: &mut [f32]) {
    let n = f.len();
    let mut v = vec![0usize; n];
    let mut z = vec![0.0f32; n + 1];
    let mut k = 0usize;
    z[0] = f32::NEG_INFINITY;
    z[1] = f32::INFINITY;
    let cross = |q: usize, p: usize| ((f[q] + (q * q) as f32) - (f[p] + (p * p) as f32)) / (2.0 * q as f32 - 2.0 * p as f32);
    for q in 1..n {
        let mut s = cross(q, v[k]);
        while s <= z[k] {
            k -= 1;
            s = cross(q, v[k]);
        }
        k += 1;
        v[k] = q;
        z[k] = s;
        z[k + 1] = f32::INFINITY;
    }
    k = 0;
    for (q, o) in out.iter_mut().enumerate() {
        while z[k + 1] < q as f32 {
            k += 1;
        }
        let p = v[k];
        *o = (q as f32 - p as f32).powi(2) + f[p];
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The transform's distances are exact: a single feature pixel gives the plain squared distance.
    #[test]
    fn squared_distances_exact() {
        let (w, h) = (7, 5);
        let mut mask = vec![false; w * h];
        mask[2 * w + 3] = true;
        let d = squared_distances(&mask, w, h, true);
        for y in 0..h {
            for x in 0..w {
                let want = (x as f32 - 3.0).powi(2) + (y as f32 - 2.0).powi(2);
                assert_eq!(d[y * w + x], want, "({x}, {y})");
            }
        }
    }

    /// A filled square: inside reads above 0.5, outside below, and the outline lands where the coverage is 0.5.
    #[test]
    fn field_of_a_square() {
        let (w, h) = (12, 12);
        let alpha: Vec<f32> = (0..w * h).map(|i| if (4..8).contains(&(i % w)) && (4..8).contains(&(i / w)) { 1.0 } else { 0.0 }).collect();
        let f = distance_field(&alpha, w, h);
        let fw = w * UPSCALE;
        let at = |x: usize, y: usize| f[y * fw + x];
        // the centre of the square (atlas pixel 6) is well inside, a far corner well outside
        assert!(at(6 * UPSCALE, 6 * UPSCALE) > 200);
        assert!(at(0, 0) == 0);
        // the outline is at atlas x = 4 (the edge between the empty and the full pixel): field x = 16
        let row = 6 * UPSCALE;
        assert!(at(4 * UPSCALE - 1, row) < 128 && at(4 * UPSCALE, row) >= 128);
    }
}
