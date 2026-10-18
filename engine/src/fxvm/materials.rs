//! The fx materials: particles (bank texture x vertex colour, particle.wesl) and the layered fx meshes (the snow
//! domes' three texture stages, fxlayer.wesl). Fx meshes with one stage use Bevy's StandardMaterial (object.rs).

use bevy::prelude::*;
use serde_json::Value;

/// Layered fx meshes (the snow domes): the original's three-stage set-up (fxlayer.wesl), additive.
#[derive(bevy::render::render_resource::AsBindGroup, Debug, Clone, Asset, bevy::reflect::TypePath)]
pub struct FxLayerMaterial {
    #[texture(0)]
    #[sampler(1)]
    pub t0: Handle<Image>,
    #[texture(2)]
    #[sampler(3)]
    pub t1: Handle<Image>,
    #[texture(4)]
    #[sampler(5)]
    pub t2: Handle<Image>,
    #[uniform(6)]
    pub params: FxLayerParams,
}

#[derive(bevy::render::render_resource::ShaderType, Debug, Clone, Default)]
pub struct FxLayerParams {
    /// uv offsets of layers 1 (xy) and 2 (zw): the 0x40 scrolls
    pub offsets: Vec4,
    pub colour: Vec4,
    /// t0 / t1 / t2 stored premultiplied (1) or not (0)
    pub flags: Vec4,
}

impl Material for FxLayerMaterial {
    fn fragment_shader() -> bevy::shader::ShaderRef {
        bevy::shader::ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("fxlayer.wesl")).with_source("embedded"))
    }
    fn alpha_mode(&self) -> AlphaMode {
        AlphaMode::Add
    }
}

/// Additive fx meshes with one stage (fxadd.wesl): the texture times the material colour, added as the original adds
/// in gamma space (matched over a scene of brightness `scene.x`).
#[derive(bevy::render::render_resource::AsBindGroup, Debug, Clone, Asset, bevy::reflect::TypePath)]
pub struct FxAddMaterial {
    #[texture(0)]
    #[sampler(1)]
    pub base: Handle<Image>,
    #[uniform(2)]
    pub params: FxAddParams,
}

#[derive(bevy::render::render_resource::ShaderType, Debug, Clone, Default)]
pub struct FxAddParams {
    /// rgb: the material colour (gamma space), a: the object's alpha
    pub colour: Vec4,
    /// the uv transform's rows (u' = x.x u + x.y v + x.z; v' likewise with y)
    pub uv_x: Vec4,
    pub uv_y: Vec4,
    /// x: the scene brightness the addition is matched over; y: the texture stored premultiplied (1)
    pub scene: Vec4,
}

impl FxAddParams {
    pub fn new(colour: Vec3, scene: f32, premultiplied: bool) -> Self {
        let mut p = FxAddParams { colour: colour.extend(1.0), scene: Vec4::new(scene, premultiplied as u8 as f32, 0.0, 0.0), ..default() };
        p.set_uv(bevy::math::Affine2::IDENTITY);
        p
    }
    pub fn set_uv(&mut self, m: bevy::math::Affine2) {
        self.uv_x = Vec4::new(m.matrix2.x_axis.x, m.matrix2.y_axis.x, m.translation.x, 0.0);
        self.uv_y = Vec4::new(m.matrix2.x_axis.y, m.matrix2.y_axis.y, m.translation.y, 0.0);
    }
}

impl Material for FxAddMaterial {
    fn fragment_shader() -> bevy::shader::ShaderRef {
        bevy::shader::ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("fxadd.wesl")).with_source("embedded"))
    }
    fn alpha_mode(&self) -> AlphaMode {
        AlphaMode::Add
    }
}

/// The particle material: bank texture x vertex colour, additive or alpha-blended (particle.wesl).
#[derive(bevy::render::render_resource::AsBindGroup, Debug, Clone, Asset, bevy::reflect::TypePath)]
pub struct ParticleMaterial {
    #[texture(0)]
    #[sampler(1)]
    pub bank: Handle<Image>,
    pub additive: bool,
    /// x: the gamma of the original's blend (alpha-blended pools: 2.2, the original blends in gamma space, emulated
    /// in particle.wesl; additive: 1, alpha as it is); y: the soft-particle depth (graphics.rs); z: the scene
    /// brightness a white alpha-blended source is matched over (particles.rs WHITE_OVER)
    #[uniform(2)]
    pub params: Vec4,
    /// the pool's quad corners, written each frame (particles.rs draw_particles)
    #[storage(3, read_only, visibility(vertex))]
    pub corners: Handle<bevy::render::storage::ShaderBuffer>,
}

impl Material for ParticleMaterial {
    fn vertex_shader() -> bevy::shader::ShaderRef {
        Self::fragment_shader()
    }
    fn fragment_shader() -> bevy::shader::ShaderRef {
        bevy::shader::ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("particle.wesl")).with_source("embedded"))
    }
    fn alpha_mode(&self) -> AlphaMode {
        if self.additive { AlphaMode::Add } else { AlphaMode::Blend }
    }
}

/// The export's per-texture `premultiplied` flag (scene.json textures: DXT2 in the bank), read once.
pub(super) fn premultiplied(dir: &std::path::Path, name: &str) -> bool {
    static FLAGS: std::sync::OnceLock<std::collections::HashMap<String, bool>> = std::sync::OnceLock::new();
    FLAGS
        .get_or_init(|| {
            let text = std::fs::read_to_string(dir.join("scene.json")).unwrap_or_default();
            let v: Value = serde_json::from_str(&text).unwrap_or(Value::Null);
            v.get("textures")
                .and_then(|t| t.as_object())
                .map(|t| t.iter().map(|(k, x)| (k.to_lowercase(), x.get("premultiplied").and_then(|p| p.as_bool()).unwrap_or(false))).collect())
                .unwrap_or_default()
        })
        .get(&name.to_lowercase())
        .copied()
        .unwrap_or(false)
}
