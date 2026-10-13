//! Scene format written by tools/remaster_export.py: scene.json, models/<name>.{json,bin}, textures/<name>.dds.
//! Game-neutral: a game's exporter writes this format and the engine loads it the same way for every game.

use bevy::asset::RenderAssetUsages;
use bevy::image::{ImageAddressMode, ImageFilterMode, ImageLoaderSettings, ImageSampler, ImageSamplerDescriptor};
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::prelude::*;
use serde::Deserialize;
use std::collections::HashMap;
use std::path::{Path, PathBuf};

#[derive(Deserialize)]
pub struct SceneFile {
    pub level: String,
    pub landscape: String,
    pub bounds: [[f32; 3]; 2],
    pub instances: Vec<InstanceDef>,
    pub lights: Vec<LightDef>,
    pub textures: HashMap<String, TextureInfo>,
}

#[derive(Deserialize, Clone)]
pub struct InstanceDef {
    pub id: u32,
    pub template: String,
    pub model: String,
    /// 3 rows of (3 rotation columns, translation), game axes (Z up).
    pub transform: [[f32; 4]; 3],
    #[serde(default)]
    pub property: String,
}

#[derive(Deserialize)]
pub struct LightDef {
    pub transform: [[f32; 4]; 3],
    pub props: HashMap<String, serde_json::Value>,
}

#[derive(Deserialize)]
pub struct TextureInfo {
    pub alpha: bool,
}

#[derive(Deserialize)]
struct ModelFile {
    submeshes: Vec<SubmeshDef>,
    min: [f32; 3],
    max: [f32; 3],
}

#[derive(Deserialize)]
struct SubmeshDef {
    texture: Option<String>,
    vertices: usize,
    indices: usize,
    alpha: bool,
    #[serde(default)]
    color: Option<[f32; 3]>,
    positions: usize,
    normals: usize,
    uvs: usize,
    index_offset: usize,
}

/// One loaded model: a mesh and material per submesh, plus its bounds (model space, game axes).
pub struct Model {
    pub parts: Vec<(Handle<Mesh>, Handle<StandardMaterial>)>,
    pub min: Vec3,
    pub max: Vec3,
    pub triangles: usize,
}

#[derive(Resource, Default)]
pub struct Library {
    pub dir: PathBuf,
    pub models: HashMap<String, Model>,
    pub materials: HashMap<(Option<String>, bool, bool, [u8; 3]), Handle<StandardMaterial>>,
}

pub fn game_matrix(t: &[[f32; 4]; 3]) -> Mat4 {
    Mat4::from_cols(
        Vec4::new(t[0][0], t[1][0], t[2][0], 0.0),
        Vec4::new(t[0][1], t[1][1], t[2][1], 0.0),
        Vec4::new(t[0][2], t[1][2], t[2][2], 0.0),
        Vec4::new(t[0][3], t[1][3], t[2][3], 1.0),
    )
}

pub fn read_scene(dir: &Path) -> SceneFile {
    let text = std::fs::read_to_string(dir.join("scene.json"))
        .unwrap_or_else(|e| panic!("{}: {e} (run tools/remaster_export.py first)", dir.join("scene.json").display()));
    serde_json::from_str(&text).expect("scene.json")
}

fn floats(bin: &[u8], at: usize, n: usize) -> Vec<f32> {
    bin[at..at + n * 4].chunks_exact(4).map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]])).collect()
}

impl Library {
    pub fn material(
        &mut self,
        texture: Option<&str>,
        alpha: bool,
        unlit: bool,
        color: [f32; 3],
        assets: &AssetServer,
        materials: &mut Assets<StandardMaterial>,
    ) -> Handle<StandardMaterial> {
        let color = if texture.is_some() { [1.0; 3] } else { color };
        let key = (texture.map(str::to_owned), alpha, unlit, color.map(|c| (c * 255.0) as u8));
        if let Some(h) = self.materials.get(&key) {
            return h.clone();
        }
        let image = texture.map(|t| {
            assets.load_builder().with_settings(|s: &mut ImageLoaderSettings| {
                s.sampler = ImageSampler::Descriptor(ImageSamplerDescriptor {
                    address_mode_u: ImageAddressMode::Repeat,
                    address_mode_v: ImageAddressMode::Repeat,
                    mag_filter: ImageFilterMode::Linear,
                    min_filter: ImageFilterMode::Linear,
                    mipmap_filter: ImageFilterMode::Linear,
                    anisotropy_clamp: 16,
                    ..default()
                });
            }).load(format!("textures/{t}.dds"))
        });
        let h = materials.add(StandardMaterial {
            base_color: Color::srgb(color[0], color[1], color[2]),
            base_color_texture: image,
            perceptual_roughness: 0.85,
            reflectance: 0.2,
            alpha_mode: if alpha { AlphaMode::Mask(0.5) } else { AlphaMode::Opaque },
            double_sided: true,
            cull_mode: None,
            unlit,
            fog_enabled: !unlit,
            ..default()
        });
        self.materials.insert(key, h.clone());
        h
    }

    pub fn load_model(
        &mut self,
        name: &str,
        unlit: bool,
        assets: &AssetServer,
        meshes: &mut Assets<Mesh>,
        materials: &mut Assets<StandardMaterial>,
    ) -> Option<&Model> {
        if !self.models.contains_key(name) {
            let json = std::fs::read_to_string(self.dir.join("models").join(format!("{name}.json"))).ok()?;
            let file: ModelFile = serde_json::from_str(&json).ok()?;
            let bin = std::fs::read(self.dir.join("models").join(format!("{name}.bin"))).ok()?;
            let mut parts = Vec::new();
            let mut triangles = 0;
            for s in &file.submeshes {
                // a sky's untextured parts are not drawn (guess: cloud or horizon helpers)
                if unlit && s.texture.is_none() {
                    continue;
                }
                let pos = floats(&bin, s.positions, s.vertices * 3);
                let nrm = floats(&bin, s.normals, s.vertices * 3);
                let uv = floats(&bin, s.uvs, s.vertices * 2);
                let idx: Vec<u32> = bin[s.index_offset..s.index_offset + s.indices * 4]
                    .chunks_exact(4)
                    .map(|c| u32::from_le_bytes([c[0], c[1], c[2], c[3]]))
                    .collect();
                triangles += idx.len() / 3;
                let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::RENDER_WORLD);
                mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, pos.chunks_exact(3).map(|v| [v[0], v[1], v[2]]).collect::<Vec<_>>());
                mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, nrm.chunks_exact(3).map(|v| [v[0], v[1], v[2]]).collect::<Vec<_>>());
                mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, uv.chunks_exact(2).map(|v| [v[0], v[1]]).collect::<Vec<_>>());
                mesh.insert_indices(Indices::U32(idx));
                let material = self.material(s.texture.as_deref(), s.alpha, unlit, s.color.unwrap_or([1.0; 3]), assets, materials);
                parts.push((meshes.add(mesh), material));
            }
            self.models.insert(
                name.to_owned(),
                Model { parts, min: Vec3::from(file.min), max: Vec3::from(file.max), triangles },
            );
        }
        self.models.get(name)
    }
}
