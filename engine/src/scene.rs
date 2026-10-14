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
    #[serde(default)]
    pub logic: Vec<LogicDef>,
    pub lights: Vec<LightDef>,
    /// The level's global_settings object (fog_near, fog_far, fog_red/green/blue, ...).
    #[serde(default)]
    pub settings: HashMap<String, serde_json::Value>,
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
    /// The instance's own property values (override the template's set).
    #[serde(default)]
    pub props: serde_json::Map<String, serde_json::Value>,
}

/// A logic object without a model (mission, marker, sound/ui ear, generator, camera path).
#[derive(Deserialize, Clone)]
pub struct LogicDef {
    pub id: u32,
    pub template: String,
    pub transform: [[f32; 4]; 3],
    #[serde(default)]
    pub property: Option<String>,
    #[serde(default)]
    pub props: serde_json::Map<String, serde_json::Value>,
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
    #[serde(default)]
    skeleton: Option<String>,
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
    #[serde(default)]
    joints: Option<usize>,
    #[serde(default)]
    weights: Option<usize>,
    /// the material record as the original shades it (gamematerial.rs)
    #[serde(default)]
    material: Option<crate::gamematerial::MaterialDef>,
    /// the second uv set (terrain blend masks)
    #[serde(default)]
    uvs1: Option<usize>,
    /// the skin bone carrying this part (a rigid part of a multi-bone mesh, or a one-bone mesh)
    #[serde(default)]
    bone: Option<String>,
}

/// How a model part follows its skeleton (when the model has one and is spawned with its rig).
#[derive(Clone, Debug, PartialEq)]
pub enum PartBind {
    /// fixed in model space
    None,
    /// GPU skinned (joints and weights)
    Skinned,
    /// rigid on one bone (oilpump_1's arm and struts: UBYTE4 blend indices without weights, tools/remaster_export.py)
    Bone(String),
}

/// One loaded model: a mesh and material per submesh, plus its bounds (model space, game axes).
pub struct Model {
    pub parts: Vec<(Handle<Mesh>, Handle<StandardMaterial>)>,
    pub min: Vec3,
    pub max: Vec3,
    pub triangles: usize,
    /// CPU copy for physics: every submesh's vertices (model space, game axes) and triangles.
    pub vertices: Vec<Vec3>,
    pub tris: Vec<[u32; 3]>,
    /// skeletons/<name>.json when the model is skinned
    pub skeleton: Option<String>,
    /// per part: how it follows the skeleton
    pub binds: Vec<PartBind>,
}

impl Model {
    /// A copy without the CPU collision data (for spawning while the library stays borrowed).
    pub fn light_clone(&self) -> Model {
        Model {
            parts: self.parts.clone(),
            min: self.min,
            max: self.max,
            triangles: self.triangles,
            vertices: Vec::new(),
            tris: Vec::new(),
            skeleton: self.skeleton.clone(),
            binds: self.binds.clone(),
        }
    }
}

pub trait ClonedParts {
    fn cloned_parts(self) -> Option<Model>;
}

impl ClonedParts for Option<&Model> {
    fn cloned_parts(self) -> Option<Model> {
        self.map(|m| m.light_clone())
    }
}

#[derive(Resource, Default)]
pub struct Library {
    pub dir: PathBuf,
    pub models: HashMap<String, Model>,
    pub materials: HashMap<(Option<String>, bool, bool, [u8; 3]), Handle<StandardMaterial>>,
    /// the landscape is drawn with the original's materials (main.rs), not here
    pub skip_landscape_visual: bool,
    /// each standard material's original record (texture, alpha): the game material swap (gamematerial.rs)
    pub game_defs: HashMap<AssetId<StandardMaterial>, (crate::gamematerial::MaterialDef, Option<String>, bool)>,
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
    /// A model's parts with the original's materials (gamematerial.rs): uv set 1 and tangents for the normal maps.
    #[allow(clippy::too_many_arguments)]
    pub fn load_game_parts(
        &self,
        name: &str,
        lights: &crate::gamematerial::GameLights,
        assets: &AssetServer,
        meshes: &mut Assets<Mesh>,
        materials: &mut Assets<crate::gamematerial::GameMaterial>,
        white: &Handle<Image>,
    ) -> Vec<(Handle<Mesh>, Handle<crate::gamematerial::GameMaterial>)> {
        let Some(json) = std::fs::read_to_string(self.dir.join("models").join(format!("{name}.json"))).ok() else { return Vec::new() };
        let Ok(file) = serde_json::from_str::<ModelFile>(&json) else { return Vec::new() };
        let Ok(bin) = std::fs::read(self.dir.join("models").join(format!("{name}.bin"))) else { return Vec::new() };
        let mut out = Vec::new();
        for s in &file.submeshes {
            let pos = floats(&bin, s.positions, s.vertices * 3);
            let nrm = floats(&bin, s.normals, s.vertices * 3);
            let uv = floats(&bin, s.uvs, s.vertices * 2);
            let idx: Vec<u32> = bin[s.index_offset..s.index_offset + s.indices * 4].chunks_exact(4).map(|c| u32::from_le_bytes([c[0], c[1], c[2], c[3]])).collect();
            let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::RENDER_WORLD);
            mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, pos.chunks_exact(3).map(|v| [v[0], v[1], v[2]]).collect::<Vec<_>>());
            mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, nrm.chunks_exact(3).map(|v| [v[0], v[1], v[2]]).collect::<Vec<_>>());
            mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, uv.chunks_exact(2).map(|v| [v[0], v[1]]).collect::<Vec<_>>());
            if let Some(o) = s.uvs1 {
                mesh.insert_attribute(Mesh::ATTRIBUTE_UV_1, floats(&bin, o, s.vertices * 2).chunks_exact(2).map(|v| [v[0], v[1]]).collect::<Vec<_>>());
            }
            mesh.insert_indices(Indices::U32(idx));
            let def = s.material.clone().unwrap_or_default();
            // tangents for the normal map (generated, MikkTSpace; the original stores its own: guess that they agree)
            if def.normal_map.is_some() {
                if let Err(e) = mesh.generate_tangents() {
                    warn!("{name}: no tangents: {e}");
                }
            }
            let m = crate::gamematerial::make(&def, s.texture.as_deref(), s.alpha, lights, assets, white);
            out.push((meshes.add(mesh), materials.add(m)));
        }
        out
    }

    #[allow(clippy::too_many_arguments)]
    pub fn material(
        &mut self,
        texture: Option<&str>,
        alpha: bool,
        unlit: bool,
        color: [f32; 3],
        def: Option<&crate::gamematerial::MaterialDef>,
        assets: &AssetServer,
        materials: &mut Assets<StandardMaterial>,
    ) -> Handle<StandardMaterial> {
        let color = if texture.is_some() { [1.0; 3] } else { color };
        // one material per distinct original record (its colours and maps differ between parts sharing a texture)
        let tag = def.map(|d| format!("{d:?}")).unwrap_or_default();
        let key = (Some(format!("{}|{tag}", texture.unwrap_or(""))), alpha, unlit, color.map(|c| (c * 255.0) as u8));
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
        // the original's record, for the swap to the game material (gamematerial.rs swap); untextured parts of
        // models (colour only) too, the sky (unlit) never
        if !unlit {
            let mut d = def.cloned().unwrap_or_default();
            if texture.is_none() {
                d.diffuse = color;
            }
            self.game_defs.insert(h.id(), (d, texture.map(str::to_owned), alpha));
        }
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
            let mut binds = Vec::new();
            let mut triangles = 0;
            let mut cpu_v: Vec<Vec3> = Vec::new();
            let mut cpu_t: Vec<[u32; 3]> = Vec::new();
            for s in &file.submeshes {
                // a sky's untextured parts (l1_bob_m1_sky_0: the disk under the horizon, emissive (212, 218, 212), and the
                // cap at the top) draw in their emissive colour: the disk is the original's pale grey horizon band where
                // the land is clipped (measured ~(205, 213, 208) in its captures; the fog colour behind it is pure white)
                let colour = if unlit && s.texture.is_none() { s.material.as_ref().map(|m| m.emissive).or(s.color) } else { s.color };
                let pos = floats(&bin, s.positions, s.vertices * 3);
                let nrm = floats(&bin, s.normals, s.vertices * 3);
                let uv = floats(&bin, s.uvs, s.vertices * 2);
                let idx: Vec<u32> = bin[s.index_offset..s.index_offset + s.indices * 4]
                    .chunks_exact(4)
                    .map(|c| u32::from_le_bytes([c[0], c[1], c[2], c[3]]))
                    .collect();
                triangles += idx.len() / 3;
                let base = cpu_v.len() as u32;
                cpu_v.extend(pos.chunks_exact(3).map(|v| Vec3::new(v[0], v[1], v[2])));
                cpu_t.extend(idx.chunks_exact(3).map(|t| [t[0] + base, t[1] + base, t[2] + base]));
                // skinned meshes keep a main-world copy too (the playtest skinning check skins them on the CPU)
                let usage = if s.joints.is_some() { RenderAssetUsages::all() } else { RenderAssetUsages::RENDER_WORLD };
                let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, usage);
                mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, pos.chunks_exact(3).map(|v| [v[0], v[1], v[2]]).collect::<Vec<_>>());
                mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, nrm.chunks_exact(3).map(|v| [v[0], v[1], v[2]]).collect::<Vec<_>>());
                mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, uv.chunks_exact(2).map(|v| [v[0], v[1]]).collect::<Vec<_>>());
                mesh.insert_indices(Indices::U32(idx));
                if let (Some(jo), Some(wo)) = (s.joints, s.weights) {
                    let j: Vec<[u16; 4]> = bin[jo..jo + s.vertices * 8]
                        .chunks_exact(8)
                        .map(|c| [u16::from_le_bytes([c[0], c[1]]), u16::from_le_bytes([c[2], c[3]]), u16::from_le_bytes([c[4], c[5]]), u16::from_le_bytes([c[6], c[7]])])
                        .collect();
                    let w = floats(&bin, wo, s.vertices * 4);
                    mesh.insert_attribute(Mesh::ATTRIBUTE_JOINT_INDEX, bevy::mesh::VertexAttributeValues::Uint16x4(j));
                    mesh.insert_attribute(Mesh::ATTRIBUTE_JOINT_WEIGHT, w.chunks_exact(4).map(|v| [v[0], v[1], v[2], v[3]]).collect::<Vec<_>>());
                }
                if let Some(o) = s.uvs1 {
                    mesh.insert_attribute(Mesh::ATTRIBUTE_UV_1, floats(&bin, o, s.vertices * 2).chunks_exact(2).map(|v| [v[0], v[1]]).collect::<Vec<_>>());
                }
                if s.material.as_ref().is_some_and(|m| m.normal_map.is_some()) {
                    let _ = mesh.generate_tangents();
                }
                let material = self.material(s.texture.as_deref(), s.alpha, unlit, colour.unwrap_or([1.0; 3]), s.material.as_ref(), assets, materials);
                parts.push((meshes.add(mesh), material));
                binds.push(match (s.joints, &s.bone) {
                    (Some(_), _) => PartBind::Skinned,
                    (None, Some(b)) => PartBind::Bone(b.to_lowercase()),
                    (None, None) => PartBind::None,
                });
            }
            self.models.insert(
                name.to_owned(),
                Model { parts, min: Vec3::from(file.min), max: Vec3::from(file.max), triangles, vertices: cpu_v, tris: cpu_t, skeleton: file.skeleton.clone(), binds },
            );
        }
        self.models.get(name)
    }
}
