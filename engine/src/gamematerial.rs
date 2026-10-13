//! The original's materials (gamematerial.wesl): kinds, extra maps and colours from the exported material records
//! (tools/remaster_export.py, docs/formats/big-skin.md 3.5), the level's lights as the original lights them
//! (the sun per pixel through the normal map, the ambient-only light and the fill per vertex), the level's linear
//! fog. Rendering piece 1 (docs/remaster-engine.md): the landscape first, compared with tools/render_compare.py.

use bevy::image::{ImageAddressMode, ImageFilterMode, ImageLoaderSettings, ImageSampler, ImageSamplerDescriptor};
use bevy::prelude::*;
use bevy::render::render_resource::{AsBindGroup, ShaderType};
use serde::Deserialize;

#[derive(ShaderType, Debug, Clone, Default)]
pub struct GameMaterialParams {
    pub kind: u32,
    pub has_normal: u32,
    pub has_texture2: u32,
    pub has_mask: u32,
    pub emissive: Vec4,
    pub ambient: Vec4,
    pub diffuse: Vec4,
    pub specular: Vec4,
    pub env: Vec4,
    pub sun_dir: Vec4,
    pub sun_col: Vec4,
    pub fill_dir: Vec4,
    pub fill_col: Vec4,
    pub ambient_light: Vec4,
    pub fog_col: Vec4,
    pub fog_range: Vec4,
}

#[derive(AsBindGroup, Debug, Clone, Asset, TypePath)]
pub struct GameMaterial {
    #[texture(0)]
    #[sampler(1)]
    pub base: Handle<Image>,
    #[texture(2)]
    #[sampler(3)]
    pub normal: Handle<Image>,
    #[texture(4)]
    #[sampler(5)]
    pub texture2: Handle<Image>,
    #[texture(6)]
    #[sampler(7)]
    pub mask: Handle<Image>,
    #[uniform(8)]
    pub params: GameMaterialParams,
    pub alpha: bool,
}

impl Material for GameMaterial {
    fn fragment_shader() -> bevy::shader::ShaderRef {
        bevy::shader::ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("gamematerial.wesl")).with_source("embedded"))
    }
    fn alpha_mode(&self) -> AlphaMode {
        if self.alpha { AlphaMode::Mask(0.5) } else { AlphaMode::Opaque }
    }
}

pub struct GameMaterialPlugin;

impl Plugin for GameMaterialPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "gamematerial.wesl");
        app.add_plugins(MaterialPlugin::<GameMaterial>::default());
    }
}

/// A material record as exported.
#[derive(Deserialize, Clone, Default)]
pub struct MaterialDef {
    #[serde(default)]
    pub kind: u32,
    #[serde(default)]
    pub normal_map: Option<String>,
    #[serde(default)]
    pub texture2: Option<String>,
    #[serde(default)]
    pub mask: Option<String>,
    #[serde(default)]
    pub emissive: [f32; 3],
    #[serde(default)]
    pub ambient: [f32; 3],
    #[serde(default)]
    pub diffuse: [f32; 3],
    #[serde(default)]
    pub specular: [f32; 3],
    #[serde(default)]
    pub power: f32,
}

/// The level's lighting as the original applies it (scene lights): the first directional light that is not
/// ambient-only is the sun, the next the fill; the ambient-only light gives the ambient term. Directions point
/// toward the light, engine space. Fog from global_settings.
#[derive(Resource, Clone, Default)]
pub struct GameLights {
    pub sun_dir: Vec3,
    pub sun_col: Vec3,
    pub fill_dir: Vec3,
    pub fill_col: Vec3,
    pub ambient: Vec3,
    pub fog: Option<(f32, f32, Vec3)>,
    /// the environment cube's colour (approximation: the fog colour, the sky at the horizon)
    pub env: Vec3,
}

impl GameLights {
    pub fn from_scene(scene: &crate::scene::SceneFile) -> Self {
        let mut l = GameLights::default();
        let mut dirs = Vec::new();
        for light in &scene.lights {
            let p = |k: &str| light.props.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
            if light.props.get("lighttype").and_then(|v| v.as_str()) != Some("dir") || p("lighton") == 0.0 {
                continue;
            }
            let col = Vec3::new(p("redvalue"), p("greenvalue"), p("bluevalue")) / 255.0 * p("intensity").max(0.0);
            if p("ambientonly") != 0.0 {
                l.ambient += col;
                continue;
            }
            let m = crate::scene::game_matrix(&light.transform);
            // the light shines down its local -Z: toward it is +Z
            let to = crate::level::game_to_engine() * m.z_axis.truncate().normalize();
            dirs.push((to, col));
        }
        if let Some((d, c)) = dirs.first() {
            (l.sun_dir, l.sun_col) = (*d, *c);
        }
        if let Some((d, c)) = dirs.get(1) {
            (l.fill_dir, l.fill_col) = (*d, *c);
        }
        let g = |k: &str| scene.settings.get(k).and_then(|v| v.as_f64()).map(|v| v as f32);
        if let (Some(near), Some(far)) = (g("fog_near"), g("fog_far")) {
            let c = Vec3::new(g("fog_red").unwrap_or(200.0), g("fog_green").unwrap_or(200.0), g("fog_blue").unwrap_or(200.0)) / 255.0;
            l.fog = Some((near, far, c));
            l.env = c;
        }
        l
    }
}

fn load_tex(assets: &AssetServer, name: &str, srgb: bool) -> Handle<Image> {
    assets
        .load_builder()
        .with_settings(move |s: &mut ImageLoaderSettings| {
            s.is_srgb = srgb;
            s.sampler = ImageSampler::Descriptor(ImageSamplerDescriptor {
                address_mode_u: ImageAddressMode::Repeat,
                address_mode_v: ImageAddressMode::Repeat,
                mag_filter: ImageFilterMode::Linear,
                min_filter: ImageFilterMode::Linear,
                mipmap_filter: ImageFilterMode::Linear,
                anisotropy_clamp: 16,
                ..default()
            });
        })
        .load(format!("textures/{name}.dds"))
}

/// A game material for one submesh.
pub fn make(def: &MaterialDef, texture: Option<&str>, alpha: bool, lights: &GameLights, assets: &AssetServer, white: &Handle<Image>) -> GameMaterial {
    let tex = |n: Option<&str>, srgb: bool| n.map(|n| load_tex(assets, n, srgb)).unwrap_or_else(|| white.clone());
    let v4 = |c: [f32; 3], w: f32| Vec4::new(c[0], c[1], c[2], w);
    let l3 = |v: Vec3, w: f32| v.extend(w);
    let (fog_col, fog_range) = match lights.fog {
        Some((near, far, c)) => (c.extend(1.0), Vec4::new(near, far, 0.0, 0.0)),
        None => (Vec4::ZERO, Vec4::ZERO),
    };
    GameMaterial {
        base: tex(texture, true),
        normal: tex(def.normal_map.as_deref(), false),
        texture2: tex(def.texture2.as_deref(), true),
        mask: tex(def.mask.as_deref(), true),
        params: GameMaterialParams {
            kind: def.kind,
            has_normal: def.normal_map.is_some() as u32,
            has_texture2: def.texture2.is_some() as u32,
            has_mask: def.mask.is_some() as u32,
            emissive: v4(def.emissive, 0.0),
            ambient: v4(def.ambient, 0.0),
            diffuse: v4(def.diffuse, 0.0),
            specular: v4(def.specular, if def.specular.iter().any(|c| *c > 0.0) { def.power } else { 0.0 }),
            // guess: TFACTOR 0.3 (the environment materials' floats +0x38.. hold 0.3; the render state was not traced)
            env: l3(lights.env, 0.3),
            sun_dir: l3(lights.sun_dir, 0.0),
            sun_col: l3(lights.sun_col, 0.0),
            fill_dir: l3(lights.fill_dir, 0.0),
            fill_col: l3(lights.fill_col, 0.0),
            ambient_light: l3(lights.ambient, 0.0),
            fog_col,
            fog_range,
        },
        alpha,
    }
}
