//! Ground detail adds small tiling textures to the landscape, so floors stay sharp close to the camera.
//!
//! The game paints each floor once and stretches it over a large area. Up close, one texel covers several screen
//! pixels, so the floor looks soft at any texture resolution. Modern games keep a painted map like that for the broad
//! colour and add a small, sharp, repeating material for the close-up detail. This module does the same, behind the
//! `ground_detail` setting.
//!
//! - The detail textures are generated from noise when the game starts, so there are no image files to ship.
//! - Each layer of one array texture is one kind of surface, such as rock, grass or snow. Its red and green channels
//!   hold a bump as a normal's x and y. Its blue channel holds a brightness change, where 0.5 leaves the colour alone.
//! - ground_detail.json says which surface each game texture is. Textures it doesn't list get no detail.
//! - The shader function `ground_detail` in gamematerial.wesl places the detail by world position. It keeps the same
//!   size however the floor's own texture is stretched, mixes two sizes to hide the repeats, and fades with distance.
//!
//! The detail only changes brightness and bumps, so the game's colours stay as painted.

use bevy::asset::{uuid_handle, RenderAssetUsages};
use bevy::image::{ImageAddressMode, ImageSampler};
use bevy::prelude::*;
use bevy::render::render_resource::{Extent3d, TextureDimension, TextureFormat, TextureViewDescriptor, TextureViewDimension};
use std::collections::HashMap;
use std::sync::LazyLock;

/// The array texture every game material binds. It is made at startup, so a material can name it before it exists.
pub const DETAIL_TEXTURES: Handle<Image> = uuid_handle!("6d0f6f1e-3b7c-4d55-9a3e-2f1c8b9e4a71");

/// Pixels per side of one detail layer.
const SIZE: usize = 512;

/// How much the detail brightens and darkens the colour (0.5 would go from half to one and a half times).
pub const ALBEDO_STRENGTH: f32 = 0.6;
/// How strongly the detail's bumps tilt the surface's normal.
pub const NORMAL_STRENGTH: f32 = 1.3;

/// The kinds of surface, in the order of the array texture's layers.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Surface {
    Snow,
    Ice,
    Rock,
    Dirt,
    Grass,
    Moss,
    Sand,
    Stone,
    Wood,
    Metal,
}

const SURFACES: [(Surface, &str); 10] = [
    (Surface::Snow, "snow"),
    (Surface::Ice, "ice"),
    (Surface::Rock, "rock"),
    (Surface::Dirt, "dirt"),
    (Surface::Grass, "grass"),
    (Surface::Moss, "moss"),
    (Surface::Sand, "sand"),
    (Surface::Stone, "stone"),
    (Surface::Wood, "wood"),
    (Surface::Metal, "metal"),
];

/// Texture name (lower case) to its layer, counted from 1 so that 0 can mean "no detail" in the shader.
static LAYERS: LazyLock<HashMap<String, u32>> = LazyLock::new(|| {
    #[derive(serde::Deserialize)]
    struct Table {
        surfaces: HashMap<String, Vec<String>>,
    }
    let table: Table = serde_json::from_str(include_str!("ground_detail.json")).expect("ground_detail.json");
    let mut layers = HashMap::new();
    for (i, (_, key)) in SURFACES.iter().enumerate() {
        for name in table.surfaces.get(*key).into_iter().flatten() {
            layers.insert(name.to_lowercase(), i as u32 + 1);
        }
    }
    layers
});

fn layer_of(texture: Option<&str>) -> f32 {
    texture.and_then(|t| LAYERS.get(&t.to_lowercase())).copied().unwrap_or(0) as f32
}

/// Returns the detail layers of a material's base texture and second texture, where 0 means none.
///
/// A floor that blends two textures, where only one of them is listed, uses that one's detail for both.
pub fn layers(base: Option<&str>, texture2: Option<&str>) -> (f32, f32) {
    let (a, b) = (layer_of(base), layer_of(texture2));
    (if a > 0.0 { a } else { b }, if b > 0.0 { b } else { a })
}

/// The shader's strengths for the setting: (brightness, bumps), both zero when it is off.
pub fn strengths(on: bool) -> (f32, f32) {
    if on { (ALBEDO_STRENGTH, NORMAL_STRENGTH) } else { (0.0, 0.0) }
}

pub struct GroundDetailPlugin;

impl Plugin for GroundDetailPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Startup, create_detail_textures);
    }
}

fn create_detail_textures(mut images: ResMut<Assets<Image>>) {
    let started = std::time::Instant::now();
    let layers: Vec<Vec<u8>> = std::thread::scope(|scope| {
        let jobs: Vec<_> = SURFACES.iter().map(|(s, _)| scope.spawn(move || with_mips(&generate(*s)))).collect();
        jobs.into_iter().map(|j| j.join().expect("detail layer")).collect()
    });
    let mips = SIZE.ilog2() + 1;
    let mut image = Image::new_uninit(
        Extent3d { width: SIZE as u32, height: SIZE as u32, depth_or_array_layers: SURFACES.len() as u32 },
        TextureDimension::D2,
        TextureFormat::Rgba8Unorm,
        RenderAssetUsages::RENDER_WORLD,
    );
    // Layer-major order (Bevy's default): each layer's whole mip chain, then the next layer.
    image.data = Some(layers.concat());
    image.texture_descriptor.mip_level_count = mips;
    image.texture_view_descriptor = Some(TextureViewDescriptor { dimension: Some(TextureViewDimension::D2Array), ..default() });
    image.sampler = ImageSampler::Descriptor(super::world_sampler(ImageAddressMode::Repeat, false));
    let _ = images.insert(DETAIL_TEXTURES.id(), image);
    info!("ground detail: {} layers of {SIZE} px made in {} ms", SURFACES.len(), started.elapsed().as_millis());
}

// Noise that repeats seamlessly: every lattice wraps at a whole number of cells across the texture.

fn hash(x: i32, y: i32, seed: u32) -> u32 {
    let mut h = (x as u32).wrapping_mul(0x8da6_b343) ^ (y as u32).wrapping_mul(0xd816_3841) ^ seed.wrapping_mul(0xcb1a_b31f);
    h ^= h >> 13;
    h = h.wrapping_mul(0x5bd1_e995);
    h ^ (h >> 15)
}

fn rand(x: i32, y: i32, seed: u32) -> f32 {
    (hash(x, y, seed) & 0xff_ffff) as f32 / 16_777_216.0
}

/// Smooth value noise in 0..1, `period` cells across the texture.
fn value_noise(u: f32, v: f32, period: i32, seed: u32) -> f32 {
    let (x, y) = (u * period as f32, v * period as f32);
    let (xi, yi) = (x.floor() as i32, y.floor() as i32);
    let (fx, fy) = (x - xi as f32, y - yi as f32);
    let (sx, sy) = (fx * fx * (3.0 - 2.0 * fx), fy * fy * (3.0 - 2.0 * fy));
    let at = |dx: i32, dy: i32| rand((xi + dx).rem_euclid(period), (yi + dy).rem_euclid(period), seed);
    let top = at(0, 0) + (at(1, 0) - at(0, 0)) * sx;
    let bottom = at(0, 1) + (at(1, 1) - at(0, 1)) * sx;
    top + (bottom - top) * sy
}

/// Layers of value noise, each twice as fine and half as strong, in 0..1.
fn fbm(u: f32, v: f32, period: i32, octaves: u32, seed: u32) -> f32 {
    let (mut sum, mut amp, mut total, mut p) = (0.0, 1.0, 0.0, period);
    for o in 0..octaves {
        sum += value_noise(u, v, p, seed + o) * amp;
        total += amp;
        amp *= 0.5;
        p *= 2;
    }
    sum / total
}

/// Cellular noise: distances (in cells) to the nearest and second-nearest scattered point, and a random value of the
/// nearest point's cell. `cells` across the texture.
fn worley(u: f32, v: f32, cells: i32, seed: u32) -> (f32, f32, f32) {
    let (x, y) = (u * cells as f32, v * cells as f32);
    let (xi, yi) = (x.floor() as i32, y.floor() as i32);
    let (mut f1, mut f2, mut id) = (9.0f32, 9.0f32, 0.0);
    for dy in -1..=1 {
        for dx in -1..=1 {
            let (cx, cy) = ((xi + dx).rem_euclid(cells), (yi + dy).rem_euclid(cells));
            let px = (xi + dx) as f32 + rand(cx, cy, seed);
            let py = (yi + dy) as f32 + rand(cx, cy, seed + 1);
            let d = ((px - x).powi(2) + (py - y).powi(2)).sqrt();
            if d < f1 {
                f2 = f1;
                f1 = d;
                id = rand(cx, cy, seed + 2);
            } else if d < f2 {
                f2 = d;
            }
        }
    }
    (f1, f2, id)
}

fn smoothstep(e0: f32, e1: f32, x: f32) -> f32 {
    let t = ((x - e0) / (e1 - e0)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

/// Short tapering strokes scattered at random angles, for grass: the tallest stroke under the point, 0..1.
fn blades(u: f32, v: f32, cells: i32, seed: u32) -> f32 {
    let (x, y) = (u * cells as f32, v * cells as f32);
    let (xi, yi) = (x.floor() as i32, y.floor() as i32);
    let mut best = 0.0f32;
    for dy in -2..=2 {
        for dx in -2..=2 {
            let (cx, cy) = ((xi + dx).rem_euclid(cells), (yi + dy).rem_euclid(cells));
            for k in 0..3u32 {
                let s = seed + k * 7;
                let (ox, oy) = ((xi + dx) as f32 + rand(cx, cy, s), (yi + dy) as f32 + rand(cx, cy, s + 1));
                let angle = rand(cx, cy, s + 2) * std::f32::consts::TAU;
                let length = 0.7 + 0.8 * rand(cx, cy, s + 3);
                let (dirx, diry) = (angle.cos(), angle.sin());
                let t = (((x - ox) * dirx + (y - oy) * diry) / length).clamp(0.0, 1.0);
                let (qx, qy) = (ox + dirx * length * t, oy + diry * length * t);
                let d = ((x - qx).powi(2) + (y - qy).powi(2)).sqrt();
                let width = 0.09 * (1.0 - 0.7 * t);
                let h = (1.0 - d / width).max(0.0) * (0.55 + 0.45 * rand(cx, cy, s + 4)) * (1.0 - 0.4 * t);
                best = best.max(h);
            }
        }
    }
    best
}

/// One surface's height (0..1) and brightness change (0.5 = none) at a point, and how steep its bumps are.
fn sample(s: Surface, u: f32, v: f32) -> (f32, f32) {
    match s {
        // soft drifts and fine crystals, nearly white throughout
        Surface::Snow => {
            let h = 0.7 * fbm(u, v, 8, 4, 11) + 0.3 * fbm(u, v, 64, 2, 12);
            (h, 0.5 + 0.2 * (h - 0.5) + 0.15 * (fbm(u, v, 96, 2, 13) - 0.5))
        }
        // smooth, with thin pale cracks that wander and break off (the same trick as rock)
        Surface::Ice => {
            let (wu, wv) = (u + 0.1 * (fbm(u, v, 4, 3, 23) - 0.5), v + 0.1 * (fbm(u, v, 4, 3, 24) - 0.5));
            let (f1, f2, _) = worley(wu, wv, 6, 21);
            let crack = (1.0 - smoothstep(0.0, 0.02, f2 - f1)) * smoothstep(0.45, 0.6, fbm(u, v, 5, 3, 25));
            (0.5 + 0.15 * fbm(u, v, 4, 3, 22) - 0.25 * crack, 0.5 + 0.35 * crack)
        }
        // weathered stone: ridges, uneven facets, and cracks that wander and break off. The cell pattern is bent by
        // noise and most of its edges are left out, so it doesn't read as paving.
        Surface::Rock => {
            let ridged = 1.0 - (2.0 * fbm(u, v, 6, 5, 31) - 1.0).abs();
            let (wu, wv) = (u + 0.12 * (fbm(u, v, 4, 3, 34) - 0.5), v + 0.12 * (fbm(u, v, 4, 3, 35) - 0.5));
            let (f1, f2, id) = worley(wu, wv, 7, 32);
            let some_edges = smoothstep(0.5, 0.62, fbm(u, v, 5, 3, 36));
            let crack = (1.0 - smoothstep(0.0, 0.025, f2 - f1)) * some_edges;
            let facet = (id - 0.5) * 0.3 * (1.0 - f1).clamp(0.0, 1.0);
            let h = 0.6 * ridged + facet + 0.25 * fbm(u, v, 48, 3, 33) - 0.3 * crack;
            (h, 0.5 + 0.3 * (h - 0.5) - 0.15 * crack)
        }
        // crumbly earth with scattered pebbles
        Surface::Dirt => {
            let (f1, _, id) = worley(u, v, 24, 41);
            let pebble = if id > 0.55 { smoothstep(0.45, 0.1, f1) } else { 0.0 };
            let h = 0.6 * fbm(u, v, 16, 4, 42) + 0.25 * fbm(u, v, 96, 2, 43) + 0.45 * pebble;
            (h, 0.5 + 0.3 * (fbm(u, v, 48, 3, 44) - 0.5) + 0.12 * pebble)
        }
        // blades over darker gaps
        Surface::Grass => {
            let b = blades(u, v, 40, 51);
            let h = 0.75 * b + 0.25 * fbm(u, v, 32, 3, 52);
            (h, 0.38 + 0.3 * b)
        }
        // soft clumps with fine fuzz
        Surface::Moss => {
            let (f1, _, id) = worley(u, v, 28, 61);
            let clump = smoothstep(0.9, 0.0, f1) * (0.6 + 0.4 * id);
            let h = 0.6 * clump + 0.4 * fbm(u, v, 128, 2, 62);
            (h, 0.42 + 0.22 * clump)
        }
        // wind ripples and grains; whole numbers of ripples across so they tile
        Surface::Sand => {
            let phase = std::f32::consts::TAU * (7.0 * u + 3.0 * v) + 4.0 * fbm(u, v, 4, 3, 71);
            let ripple = 0.5 + 0.5 * phase.sin();
            let h = 0.7 * ripple + 0.3 * fbm(u, v, 128, 2, 72);
            (h, 0.5 + 0.1 * (ripple - 0.5) + 0.12 * (fbm(u, v, 128, 1, 73) - 0.5))
        }
        // worked stone, plaster and brick faces: fine pitting
        Surface::Stone => {
            let (f1, _, id) = worley(u, v, 48, 81);
            let pit = if id > 0.6 { smoothstep(0.25, 0.0, f1) } else { 0.0 };
            let h = 0.6 * fbm(u, v, 24, 4, 82) + 0.2 * fbm(u, v, 128, 1, 83) - 0.35 * pit;
            (h, 0.5 + 0.2 * (h - 0.5) - 0.15 * pit)
        }
        // fibres in no particular direction, since the detail doesn't know which way a plank runs
        Surface::Wood => {
            let h = 0.5 * fbm(u, v, 48, 3, 91) + 0.5 * fbm(u, v, 160, 2, 92);
            (h, 0.5 + 0.18 * (h - 0.5))
        }
        // pitting and flaking rust
        Surface::Metal => {
            let (f1, _, id) = worley(u, v, 64, 101);
            let pit = if id > 0.5 { smoothstep(0.3, 0.0, f1) } else { 0.0 };
            let flake = smoothstep(0.55, 0.65, fbm(u, v, 16, 4, 102));
            let h = 0.3 * fbm(u, v, 96, 2, 103) - 0.4 * pit + 0.25 * flake;
            (h, 0.5 + 0.15 * (flake - 0.5) - 0.1 * pit)
        }
    }
}

/// How steep a surface's bumps are: the height difference across one pixel is multiplied by this.
fn bumpiness(s: Surface) -> f32 {
    match s {
        Surface::Snow => 9.0,
        Surface::Ice => 2.0,
        Surface::Rock => 7.0,
        Surface::Dirt => 6.0,
        Surface::Grass => 6.0,
        Surface::Moss => 5.0,
        Surface::Sand => 4.0,
        Surface::Stone => 5.0,
        Surface::Wood => 3.0,
        Surface::Metal => 3.0,
    }
}

/// One layer at full size: RGBA, a normal's x and y in red and green, the brightness change in blue.
fn generate(s: Surface) -> Vec<u8> {
    let mut height = vec![0.0f32; SIZE * SIZE];
    let mut albedo = vec![0.0f32; SIZE * SIZE];
    for y in 0..SIZE {
        for x in 0..SIZE {
            let (h, a) = sample(s, x as f32 / SIZE as f32, y as f32 / SIZE as f32);
            height[y * SIZE + x] = h;
            albedo[y * SIZE + x] = a;
        }
    }
    let k = bumpiness(s);
    let at = |x: usize, y: usize| height[(y % SIZE) * SIZE + (x % SIZE)];
    let mut out = vec![0u8; SIZE * SIZE * 4];
    for y in 0..SIZE {
        for x in 0..SIZE {
            let dx = (at(x + 1, y) - at(x + SIZE - 1, y)) * 0.5 * k;
            let dy = (at(x, y + 1) - at(x, y + SIZE - 1)) * 0.5 * k;
            let n = Vec3::new(-dx, -dy, 1.0).normalize();
            let i = (y * SIZE + x) * 4;
            out[i] = ((n.x * 0.5 + 0.5) * 255.0).round() as u8;
            out[i + 1] = ((n.y * 0.5 + 0.5) * 255.0).round() as u8;
            out[i + 2] = (albedo[y * SIZE + x].clamp(0.0, 1.0) * 255.0).round() as u8;
            out[i + 3] = 255;
        }
    }
    out
}

/// The layer followed by its smaller copies down to 1 pixel, each the average of 2 x 2 pixels of the one before.
/// Averaged normals grow shorter where bumps cancel out, so the detail fades as it gets smaller.
fn with_mips(full: &[u8]) -> Vec<u8> {
    let mut all = full.to_vec();
    let mut level = full.to_vec();
    let mut size = SIZE;
    while size > 1 {
        let half = size / 2;
        let mut next = vec![0u8; half * half * 4];
        for y in 0..half {
            for x in 0..half {
                for c in 0..4 {
                    let at = |xx: usize, yy: usize| level[(yy * size + xx) * 4 + c] as u32;
                    let sum = at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1);
                    next[(y * half + x) * 4 + c] = ((sum + 2) / 4) as u8;
                }
            }
        }
        all.extend_from_slice(&next);
        level = next;
        size = half;
    }
    all
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_listed_texture_has_one_surface() {
        let table: serde_json::Value = serde_json::from_str(include_str!("ground_detail.json")).unwrap();
        let mut seen = std::collections::HashSet::new();
        for (key, names) in table["surfaces"].as_object().unwrap() {
            assert!(SURFACES.iter().any(|(_, k)| k == key), "unknown surface {key}");
            for n in names.as_array().unwrap() {
                assert!(seen.insert(n.as_str().unwrap().to_owned()), "{n} listed twice");
            }
        }
        assert_eq!(layers(Some("B_LEVEL1_GROUND"), None), (1.0, 1.0));
        assert_eq!(layers(Some("fx_black"), Some("b_plains_ground")), (5.0, 5.0));
        assert_eq!(layers(None, None), (0.0, 0.0));
    }

    #[test]
    fn layers_tile_seamlessly() {
        // the last column must continue into the first as smoothly as neighbouring columns do
        for (s, _) in SURFACES {
            let (h0, _) = sample(s, 0.0, 0.37);
            let (h1, _) = sample(s, 1.0, 0.37);
            assert!((h0 - h1).abs() < 1e-3, "{s:?} doesn't wrap: {h0} vs {h1}");
        }
    }

    /// Writes each layer as raw RGBA to $DETAIL_DUMP/NAME.rgba, to look at while tuning: `DETAIL_DUMP=dir cargo test
    /// dump_layers -- --ignored`.
    #[test]
    #[ignore]
    fn dump_layers() {
        let dir = std::env::var("DETAIL_DUMP").expect("DETAIL_DUMP");
        for (s, name) in SURFACES {
            std::fs::write(format!("{dir}/{name}.rgba"), generate(s)).unwrap();
        }
    }

    #[test]
    fn mip_chain_has_every_level() {
        let full = vec![128u8; SIZE * SIZE * 4];
        let expected: usize = (0..=SIZE.ilog2()).map(|l| (SIZE >> l) * (SIZE >> l) * 4).sum();
        assert_eq!(with_mips(&full).len(), expected);
    }
}
