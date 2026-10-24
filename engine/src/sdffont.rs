//! Crisp text at any resolution in the original's typeface (docs/player-feedback.md 24, 27).
//!
//! The game's only font is a 256 x 512 atlas of 25-pixel glyphs (ui/font.png, exported by
//! tools/remaster_export_ui.py from txtr mainfontid), drawn at 640 x 480 in the original. Sampled as a plain texture
//! at 4K it is magnified 4.5 times and the bilinear filter smears every edge (feedback 24, "blurry"); a distance
//! field of that atlas (the first fix) made the edges sharp but the shapes came from 25-pixel bitmaps: thick, rounded
//! glyphs (feedback 27, "bold and unsharp").
//!
//! The typeface is Arial (regular): every glyph of the atlas matches Arial's shapes (G without a spur, the slanted
//! t, the two-storey a, R's straight leg, Q's tail, &, %: compared drawn at the same cap height, 2026-10-15), at an
//! em of about 20.9 atlas pixels (cap height 15, Arial's 0.716 em). So the atlas is drawn again from Arial's
//! outlines at [`OUTLINE_UPSCALE`] times its resolution, glyph by glyph into the same cells: each glyph at the em
//! that gives the atlas' cap height, on the atlas' baseline, its ink centred on the atlas glyph's ink. The text
//! layout (font.json: advances, line height, uv rects) and the shadow (a second mesh, hud.rs) are unchanged; only
//! what the cells hold is finer. Arial is not shipped or committed: it is read from the system's fonts folder
//! (C:\Windows\Fonts\arial.ttf) at start-up; without it (or for a glyph Arial lacks) the atlas' own pixels are used,
//! as before.
//!
//! The drawn atlas becomes a signed distance field once per process ([`font_field`]); the shader (sdffont.wesl)
//! draws the 0.5 level anti-aliased over one screen pixel, so the glyphs stay sharp at any size; mip levels keep
//! small text (a 640 x 372 test window) from shimmering.
//!
//! API: [`SdfFontPlugin`]; [`FontMaterial`] (the text's colour times its vertex colours); [`font_field`] (the field
//! image for a UI folder, computed once per process).

use bevy::asset::RenderAssetUsages;
use bevy::image::{CompressedImageFormats, ImageFilterMode, ImageSampler, ImageSamplerDescriptor, ImageType};
use bevy::prelude::*;
use bevy::render::render_resource::{AsBindGroup, Extent3d, TextureDimension, TextureFormat};
use bevy::shader::ShaderRef;
use bevy::sprite_render::{AlphaMode2d, Material2d, Material2dPlugin};
use std::path::Path;
use std::sync::OnceLock;

/// Field pixels per atlas pixel when the atlas' own pixels are the source (no typeface).
pub const UPSCALE: usize = 4;
/// Field pixels per atlas pixel when the glyphs are drawn from the typeface's outlines.
pub const OUTLINE_UPSCALE: usize = 8;
/// The typeface's file in the system's fonts folder.
pub const TYPEFACE_FILE: &str = "arial.ttf";
/// Mip levels of the field (the full size and three halvings).
const MIPS: u32 = 4;

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

/// A glyph's cell in the atlas (font.json `chars`: x, y, width, height in atlas pixels).
#[derive(Clone, Copy, Debug)]
pub struct Cell {
    pub ch: char,
    pub x: usize,
    pub y: usize,
    pub w: usize,
    pub h: usize,
}

/// The cells of font.json.
fn cells(json: &Path) -> Vec<Cell> {
    let Some(v) = std::fs::read_to_string(json).ok().and_then(|s| serde_json::from_str::<serde_json::Value>(&s).ok()) else { return Vec::new() };
    let Some(chars) = v["chars"].as_object() else { return Vec::new() };
    chars
        .iter()
        .filter_map(|(k, g)| {
            let ch = atlas_char(k.parse().ok()?)?;
            let n = |f: &str| g[f].as_u64().map(|v| v as usize);
            Some(Cell { ch, x: n("x")?, y: n("y")?, w: n("width")?, h: n("height")? })
        })
        .collect()
}

/// The character an atlas code stands for (font.json keys are byte codes): Latin-1, the same in Unicode, except 127..159,
/// whose cells all hold the original font's missing-glyph box (measured: the same 11 x 14 ink box in every one), kept
/// as it is: NUL, which no typeface draws.
fn atlas_char(code: u32) -> Option<char> {
    match code {
        0x7f..=0x9f => Some('\0'),
        _ => char::from_u32(code),
    }
}

/// The system's copy of the typeface, if any (Windows: Fonts\arial.ttf; macOS: Supplemental/Arial.ttf).
fn typeface() -> Option<Vec<u8>> {
    std::fs::read(crate::platform::find_system_font(TYPEFACE_FILE)?).ok()
}

/// The distance field of the UI folder's font (font.png, font.json): R8, 0.5 on the outline, more inside, linear
/// sampled with mips. Computed once per process (a level restart reuses it); None when the atlas cannot be read.
pub fn font_field(ui: &Path) -> Option<Image> {
    static FIELD: OnceLock<Option<(u32, u32, Vec<u8>)>> = OnceLock::new();
    let (w, h, data) = FIELD
        .get_or_init(|| {
            let t = std::time::Instant::now();
            let png = ui.join("font.png");
            let bytes = std::fs::read(&png).map_err(|e| warn!("font: {}: {e}", png.display())).ok()?;
            let img = Image::from_buffer(&bytes, ImageType::Extension("png"), CompressedImageFormats::NONE, false, ImageSampler::Default, RenderAssetUsages::MAIN_WORLD)
                .map_err(|e| warn!("font: {}: {e}", png.display()))
                .ok()?;
            let (w, h) = (img.width() as usize, img.height() as usize);
            let rgba = img.data.as_deref().filter(|d| d.len() == w * h * 4)?;
            let alpha: Vec<f32> = rgba.chunks_exact(4).map(|p| p[3] as f32 / 255.0).collect();
            let atlas = Atlas { alpha: &alpha, w, h };
            let drawn = typeface().and_then(|font| redraw(&font, &cells(&ui.join("font.json")), &atlas, OUTLINE_UPSCALE));
            let (field, k, source) = match drawn {
                Some(d) => (field_of_mask(&d.inside, w * OUTLINE_UPSCALE, h * OUTLINE_UPSCALE, OUTLINE_UPSCALE), OUTLINE_UPSCALE, format!("{TYPEFACE_FILE}, {} glyphs, {} from the atlas", d.drawn, d.kept)),
                None => (distance_field(&alpha, w, h), UPSCALE, "the atlas (no typeface)".to_owned()),
            };
            let (fw, fh) = (w * k, h * k);
            let mut data = field;
            data.extend(mips(&data, fw, fh, MIPS));
            info!("font: distance field {fw}x{fh} from {source} in {:.0} ms", t.elapsed().as_secs_f32() * 1000.0);
            Some((fw as u32, fh as u32, data))
        })
        .clone()?;
    let mut image = Image::new_uninit(Extent3d { width: w, height: h, depth_or_array_layers: 1 }, TextureDimension::D2, TextureFormat::R8Unorm, RenderAssetUsages::RENDER_WORLD);
    image.data = Some(data);
    image.texture_descriptor.mip_level_count = MIPS;
    image.sampler = ImageSampler::Descriptor(ImageSamplerDescriptor { mipmap_filter: ImageFilterMode::Linear, ..ImageSamplerDescriptor::linear() });
    Some(image)
}

/// The original atlas' coverage (alpha 0..1, w x h).
pub struct Atlas<'a> {
    pub alpha: &'a [f32],
    pub w: usize,
    pub h: usize,
}

impl Atlas<'_> {
    fn at(&self, x: usize, y: usize) -> f32 {
        self.alpha[y * self.w + x]
    }

    /// A cell's ink box (atlas pixels, cell-relative, end exclusive): the columns and rows with coverage of 0.5 or
    /// more. None for an empty cell.
    pub fn ink(&self, c: &Cell) -> Option<(usize, usize, usize, usize)> {
        let (mut x0, mut y0, mut x1, mut y1) = (usize::MAX, usize::MAX, 0, 0);
        for y in 0..c.h.min(self.h.saturating_sub(c.y)) {
            for x in 0..c.w.min(self.w.saturating_sub(c.x)) {
                if self.at(c.x + x, c.y + y) >= 0.5 {
                    (x0, y0, x1, y1) = (x0.min(x), y0.min(y), x1.max(x + 1), y1.max(y + 1));
                }
            }
        }
        (x0 != usize::MAX).then_some((x0, y0, x1, y1))
    }
}

/// The atlas drawn again at `k` times its size: which field pixels are inside a glyph, and how many cells came from
/// the typeface and how many kept the atlas' pixels.
pub struct Drawn {
    pub inside: Vec<bool>,
    pub drawn: usize,
    pub kept: usize,
}

/// The cells of `atlas` drawn from the typeface `font` at `k` times the atlas' resolution (module doc). None when
/// the font cannot be read or the atlas has no 'H' to measure.
pub fn redraw(font: &[u8], cells: &[Cell], atlas: &Atlas, k: usize) -> Option<Drawn> {
    use swash::scale::{Render, ScaleContext, Source};
    use swash::zeno::Format;
    let face = swash::FontRef::from_index(font, 0)?;
    // the atlas' cap height and baseline (in its cell) from 'H'
    let h_cell = cells.iter().find(|c| c.ch == 'H')?;
    let (_, cap_top, _, baseline) = atlas.ink(h_cell)?;
    let cap_px = (baseline - cap_top) as f32;
    let mut ctx = ScaleContext::new();
    let cap_ratio = {
        let mut s = ctx.builder(face).size(1000.0).hint(false).build();
        s.scale_outline(face.charmap().map('H'))?.bounds().max.y / 1000.0
    };
    if cap_ratio <= 0.0 {
        return None;
    }
    let em = cap_px / cap_ratio;
    let mut scaler = ctx.builder(face).size(em * k as f32).hint(false).build();
    let (fw, fh) = (atlas.w * k, atlas.h * k);
    let mut inside = vec![false; fw * fh];
    let (mut drawn, mut kept) = (0, 0);
    for c in cells {
        let Some((ix0, _, ix1, _)) = atlas.ink(c) else { continue };
        let gid = face.charmap().map(c.ch);
        let img = (gid != 0).then(|| Render::new(&[Source::Outline]).format(Format::Alpha).render(&mut scaler, gid)).flatten();
        // the glyph's ink columns in its image (coverage of half or more)
        let ink_cols = img.as_ref().and_then(|g| {
            let (w, h) = (g.placement.width as usize, g.placement.height as usize);
            let cols: Vec<usize> = (0..w).filter(|&x| (0..h).any(|y| g.data[y * w + x] >= 128)).collect();
            Some((*cols.first()?, *cols.last()? + 1))
        });
        let (Some(g), Some((gx0, gx1))) = (img, ink_cols) else {
            // a glyph the typeface lacks: the atlas' own pixels, magnified
            kept += 1;
            for y in 0..c.h * k {
                for x in 0..c.w * k {
                    let (fx, fy) = (c.x * k + x, c.y * k + y);
                    if fx < fw && fy < fh {
                        inside[fy * fw + fx] = atlas.at((c.x + x / k).min(atlas.w - 1), (c.y + y / k).min(atlas.h - 1)) >= 0.5;
                    }
                }
            }
            continue;
        };
        drawn += 1;
        // the image's left column in field pixels: its ink centre on the atlas glyph's ink centre
        let atlas_centre = (c.x as f32 + (ix0 + ix1) as f32 / 2.0) * k as f32;
        let left = (atlas_centre - (gx0 + gx1) as f32 / 2.0).round() as isize;
        // its top row: the baseline on the atlas' ('H' measured), placement.top above it
        let top = ((c.y + baseline) * k) as isize - g.placement.top as isize;
        let (gw, gh) = (g.placement.width as usize, g.placement.height as usize);
        let (cx0, cy0, cx1, cy1) = (c.x * k, c.y * k, (c.x + c.w) * k, (c.y + c.h) * k);
        for y in 0..gh {
            for x in 0..gw {
                let (fx, fy) = (left + x as isize, top + y as isize);
                // within the glyph's own cell only (the uv rect: neighbours never bleed in)
                if fx < cx0 as isize || fy < cy0 as isize || fx >= cx1.min(fw) as isize || fy >= cy1.min(fh) as isize {
                    continue;
                }
                if g.data[y * gw + x] >= 128 {
                    inside[fy as usize * fw + fx as usize] = true;
                }
            }
        }
    }
    Some(Drawn { inside, drawn, kept })
}

/// The signed distance field of an inside mask (fw x fh field pixels, `k` per atlas pixel), as bytes: 0.5 * 255 on
/// the outline, larger inside, 2 atlas pixels to either end of the range (enough for the anti-aliasing when the text
/// is drawn smaller than the atlas).
fn field_of_mask(inside: &[bool], fw: usize, fh: usize, k: usize) -> Vec<u8> {
    let spread = 2.0 * k as f32;
    let to_inside = squared_distances(inside, fw, fh, true);
    let to_outside = squared_distances(inside, fw, fh, false);
    // half a pixel puts the outline between the two kinds
    (0..fw * fh)
        .map(|i| {
            let d = if inside[i] { to_outside[i].sqrt() - 0.5 } else { -(to_inside[i].sqrt() - 0.5) };
            ((0.5 + d / (2.0 * spread)).clamp(0.0, 1.0) * 255.0).round() as u8
        })
        .collect()
}

/// The mip levels below a w x h R8 image (2 x 2 box filter), concatenated, `levels - 1` of them.
fn mips(base: &[u8], w: usize, h: usize, levels: u32) -> Vec<u8> {
    let mut out = Vec::new();
    let (mut prev, mut pw, mut ph) = (base.to_vec(), w, h);
    for _ in 1..levels {
        let (nw, nh) = ((pw / 2).max(1), (ph / 2).max(1));
        let at = |x: usize, y: usize| prev[y.min(ph - 1) * pw + x.min(pw - 1)] as u32;
        let next: Vec<u8> = (0..nw * nh)
            .map(|i| {
                let (x, y) = (i % nw * 2, i / nw * 2);
                ((at(x, y) + at(x + 1, y) + at(x, y + 1) + at(x + 1, y + 1) + 2) / 4) as u8
            })
            .collect();
        out.extend_from_slice(&next);
        (prev, pw, ph) = (next, nw, nh);
    }
    out
}

/// Coverage `alpha` (w x h, 0..1) -> the signed distance field at UPSCALE times the size (the source when the typeface
/// is missing): the coverage's 0.5 level upsampled smoothly (Catmull-Rom) as the outline.
fn distance_field(alpha: &[f32], w: usize, h: usize) -> Vec<u8> {
    let (fw, fh) = (w * UPSCALE, h * UPSCALE);
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
    field_of_mask(&inside, fw, fh, UPSCALE)
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

    /// Mips halve each level with a box filter.
    #[test]
    fn mip_levels() {
        let base: Vec<u8> = (0..16).map(|i| if i % 4 < 2 { 200 } else { 0 }).collect();
        let m = mips(&base, 4, 4, 3);
        assert_eq!(m.len(), 4 + 1);
        assert_eq!(&m[..4], &[200, 0, 200, 0]);
        assert_eq!(m[4], 100);
    }

    /// Feedback 27: the glyphs drawn from Arial keep the original's metrics, so no text moves: every glyph's ink box,
    /// brought back to atlas pixels, lies within a pixel of the original atlas glyph's (left, right, top, bottom), and
    /// nearly every cell is drawn from the typeface. Needs the exported UI (work/remaster/lev_1_m1/ui) and the
    /// system's Arial; skipped (passes) without them.
    #[test]
    fn typeface_keeps_the_metrics() {
        // AND_UI_DIR: the exported ui folder when this checkout has no work/ (a worktree)
        let ui = std::env::var_os("AND_UI_DIR").map(std::path::PathBuf::from).unwrap_or_else(|| Path::new(env!("CARGO_MANIFEST_DIR")).join("../work/remaster/lev_1_m1/ui"));
        let (Some(font), Ok(bytes)) = (typeface(), std::fs::read(ui.join("font.png"))) else {
            eprintln!("skipped: no {TYPEFACE_FILE} or no exported font.png");
            return;
        };
        let img = Image::from_buffer(&bytes, ImageType::Extension("png"), CompressedImageFormats::NONE, false, ImageSampler::Default, RenderAssetUsages::MAIN_WORLD).unwrap();
        let (w, h) = (img.width() as usize, img.height() as usize);
        let alpha: Vec<f32> = img.data.as_deref().unwrap().chunks_exact(4).map(|p| p[3] as f32 / 255.0).collect();
        let atlas = Atlas { alpha: &alpha, w, h };
        let cells = cells(&ui.join("font.json"));
        let k = OUTLINE_UPSCALE;
        let d = redraw(&font, &cells, &atlas, k).expect("the typeface draws the atlas");
        let with_ink: Vec<&Cell> = cells.iter().filter(|c| atlas.ink(c).is_some()).collect();
        // every glyph but the missing-glyph boxes (codes 127..159) comes from the typeface
        let boxes = with_ink.iter().filter(|c| c.ch == '\0').count();
        assert!(d.drawn + d.kept == with_ink.len() && d.kept == boxes && d.drawn > 150, "drawn {} kept {} boxes {boxes}", d.drawn, d.kept);
        let with_ink: Vec<&Cell> = with_ink.into_iter().filter(|c| c.ch != '\0').collect();
        let (fw, _) = (w * k, h * k);
        let mut worst = (0.0f32, ' ');
        let (mut sum, mut n) = (0.0f32, 0usize);
        for c in with_ink {
            let (ax0, ay0, ax1, ay1) = atlas.ink(c).unwrap();
            // the drawn ink box in atlas pixels
            let (mut x0, mut y0, mut x1, mut y1) = (usize::MAX, usize::MAX, 0, 0);
            for y in c.y * k..(c.y + c.h) * k {
                for x in c.x * k..(c.x + c.w) * k {
                    if d.inside[y * fw + x] {
                        (x0, y0, x1, y1) = (x0.min(x), y0.min(y), x1.max(x + 1), y1.max(y + 1));
                    }
                }
            }
            if x0 == usize::MAX {
                panic!("{:?} drew nothing", c.ch);
            }
            let px = |v: usize, o: usize| v as f32 / k as f32 - o as f32;
            let err = [px(x0, c.x) - ax0 as f32, px(x1, c.x) - ax1 as f32, px(y0, c.y) - ay0 as f32, px(y1, c.y) - ay1 as f32].iter().fold(0.0f32, |m, e| m.max(e.abs()));
            if err > worst.0 {
                worst = (err, c.ch);
            }
            (sum, n) = (sum + err, n + 1);
        }
        // hinting moved the original's stems and accents by up to a pixel; on average the boxes agree to half one
        let mean = sum / n.max(1) as f32;
        eprintln!("ink box error: mean {mean:.2}, worst {:.2} ({:?}) atlas pixels over {n} glyphs", worst.0, worst.1);
        assert!(mean <= 0.5, "the glyphs' ink moved {mean:.2} atlas pixels on average");
        assert!(worst.0 <= 2.0, "the ink of {:?} moved {:.2} atlas pixels", worst.1, worst.0);
    }
}
