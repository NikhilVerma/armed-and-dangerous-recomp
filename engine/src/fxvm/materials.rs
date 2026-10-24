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
    /// the record's opacity (+0x48): the texture factor (0x004897d0's constant alpha) is the object's alpha x this
    /// (the snow domes fxo_snow / fxo_snow_2: 0.8); `params.colour.w` holds the product
    pub opacity: f32,
    /// set by opcode 0x62 (Fx_SetBackground 0x0041fe00, script.rs): drawn as part of the sky, opaque, behind the world
    pub background: Option<Background>,
}

/// An fx object drawn with the sky (opcode 0x62: lev_5's sea and cloud dome). The original draws the second sky list
/// in ScenePass_Render (0x00489070, flag 0x10) before the world, so the world always covers it; here, as the level's
/// sky (graphics/farclip.rs), it is depth-tested without a prepass and, with one, fills only the pixels nothing is in
/// front of (or what is in front was far-clipped). Its record is opaque (kind 13: no translucent bit, 0x004897d0
/// gives no blend), so the layered stages' colour is written as it is.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Background {
    /// the material fog bit 0x10 (the opcode's second word): under the level's fog, as the game material is
    pub fog: bool,
}

#[derive(bevy::render::render_resource::ShaderType, Debug, Clone, Default)]
pub struct FxLayerParams {
    /// the uv transforms of layers 1 (t0) and 2 (t1), as rows (u' = x.x u + x.y v + x.z; v' likewise with y): the
    /// 0x40 scrolls, 0x41..0x44 scales and 0x45 rotations (TexAnim_SetStages 0x004196f0, a matrix per stage)
    pub uv1_x: Vec4,
    pub uv1_y: Vec4,
    pub uv2_x: Vec4,
    pub uv2_y: Vec4,
    pub colour: Vec4,
    /// t0 / t1 / t2 stored premultiplied (1) or not (0); w: 1 = a background (opaque, see [`Background`])
    pub flags: Vec4,
    /// a fogged background: the level's fog colour (rgb, gamma) and w 1 (0: no fog)
    pub fog_col: Vec4,
    /// a background: fog start, end; the far clip and its dissolve (1) of what may be in front (the game material's)
    pub fog_range: Vec4,
    /// a fogged background: the height fog's parameters (GameMaterialParams::height_fog; x 0 = the linear fog)
    pub height_fog: Vec4,
    /// toward the sun (the height fog's glow)
    pub sun_dir: Vec4,
}

impl FxLayerParams {
    pub fn new(colour: Vec4, flags: Vec4) -> Self {
        let mut p = FxLayerParams { colour, flags, ..default() };
        p.set_uv(0, bevy::math::Affine2::IDENTITY);
        p.set_uv(1, bevy::math::Affine2::IDENTITY);
        p
    }
    /// Layer `stage` (0 or 1)'s uv transform.
    pub fn set_uv(&mut self, stage: usize, m: bevy::math::Affine2) {
        let x = Vec4::new(m.matrix2.x_axis.x, m.matrix2.y_axis.x, m.translation.x, 0.0);
        let y = Vec4::new(m.matrix2.x_axis.y, m.matrix2.y_axis.y, m.translation.y, 0.0);
        if stage == 0 {
            (self.uv1_x, self.uv1_y) = (x, y);
        } else {
            (self.uv2_x, self.uv2_y) = (x, y);
        }
    }
}

impl Material for FxLayerMaterial {
    fn fragment_shader() -> bevy::shader::ShaderRef {
        bevy::shader::ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("fxlayer.wesl")).with_source("embedded"))
    }
    fn alpha_mode(&self) -> AlphaMode {
        if self.background.is_some() { AlphaMode::Opaque } else { AlphaMode::Add }
    }
    /// additive: drawn in the transmissive pass, composited in gamma space (additive.rs)
    fn reads_view_transmission_texture(&self) -> bool {
        self.background.is_none()
    }
    /// A background's colour pass behind a depth prepass draws as the sky does (graphics/farclip.rs): no depth test
    /// or writes, the shader keeps what nothing covers. Its prepass stays depth-tested.
    fn specialize(
        _pipeline: &bevy::pbr::MaterialPipeline,
        descriptor: &mut bevy::render::render_resource::RenderPipelineDescriptor,
        _layout: &bevy::mesh::MeshVertexBufferLayoutRef,
        key: bevy::pbr::MaterialPipelineKey<Self>,
    ) -> Result<(), bevy::render::render_resource::SpecializedMeshPipelineError> {
        use bevy::pbr::MeshPipelineKey;
        let opaque = key.mesh_key & MeshPipelineKey::BLEND_RESERVED_BITS == MeshPipelineKey::BLEND_OPAQUE;
        crate::graphics::farclip::behind_prepass(descriptor, opaque && key.mesh_key.contains(MeshPipelineKey::DEPTH_PREPASS));
        Ok(())
    }
}

/// The fog and far clip a background layered material ([`Background`]) needs, from the level and the settings:
/// kept current as they change (the Graphics page, the height fog's reference set on the level's first frame).
pub fn background_fog(
    lights: Option<Res<crate::gamematerial::GameLights>>,
    settings: Res<crate::graphics::GraphicsSettings>,
    mut layers: ResMut<Assets<FxLayerMaterial>>,
) {
    let Some(lights) = lights else { return };
    let ids: Vec<_> = layers.iter().filter(|(_, m)| m.background.is_some()).map(|(id, _)| id).collect();
    for id in ids {
        let Some(m) = layers.get(id) else { continue };
        let fog = m.background.is_some_and(|b| b.fog);
        let mut p = m.params.clone();
        p.flags.w = 1.0;
        let clip = crate::options::world_far(lights.clip_far, 1.0);
        let fade = if settings.far_fade { 1.0 } else { 0.0 };
        match lights.fog.filter(|_| fog) {
            Some((near, far, c)) => {
                p.fog_col = c.extend(1.0);
                p.fog_range = Vec4::new(near, far, clip, fade);
                p.height_fog = settings.height_fog.material_params(lights.fog_reference);
            }
            None => {
                p.fog_col = Vec4::ZERO;
                p.fog_range = Vec4::new(0.0, 0.0, if lights.fog.is_some() { clip } else { 0.0 }, fade);
                p.height_fog = Vec4::ZERO;
            }
        }
        p.sun_dir = lights.sun_dir.extend(0.0);
        if (p.flags, p.fog_col, p.fog_range, p.height_fog, p.sun_dir) != (m.params.flags, m.params.fog_col, m.params.fog_range, m.params.height_fog, m.params.sun_dir) {
            if let Some(mut m) = layers.get_mut(id) {
                m.params = p;
            }
        }
    }
}

/// Additive fx meshes with one stage (fxadd.wesl): the texture times the material colour, added as the original adds,
/// in gamma space (additive.rs).
#[derive(bevy::render::render_resource::AsBindGroup, Debug, Clone, Asset, bevy::reflect::TypePath)]
pub struct FxAddMaterial {
    #[texture(0)]
    #[sampler(1)]
    pub base: Handle<Image>,
    #[uniform(2)]
    pub params: FxAddParams,
    /// the record's opacity (+0x48): the constant alpha the draw state 0x004897d0 gives an additive record (state
    /// 0x120, SRCALPHA / ONE) is the object's alpha x this (with 0x40000 the stage's alpha is the texture's x that
    /// constant: fxo_glow_red 0.1, fxo_trail_bullet 0.15); `params.colour.w` holds the product
    pub opacity: f32,
}

#[derive(bevy::render::render_resource::ShaderType, Debug, Clone, Default)]
pub struct FxAddParams {
    /// rgb: the material colour (gamma space), a: the object's alpha
    pub colour: Vec4,
    /// the uv transform's rows (u' = x.x u + x.y v + x.z; v' likewise with y)
    pub uv_x: Vec4,
    pub uv_y: Vec4,
    /// x: the texture stored premultiplied (1)
    pub flags: Vec4,
}

impl FxAddParams {
    pub fn new(colour: Vec3, premultiplied: bool) -> Self {
        let mut p = FxAddParams { colour: colour.extend(1.0), flags: Vec4::new(premultiplied as u8 as f32, 0.0, 0.0, 0.0), ..default() };
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
    /// drawn in the transmissive pass, composited in gamma space (additive.rs)
    fn reads_view_transmission_texture(&self) -> bool {
        true
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
    /// in particle.wesl; additive: 1, the colour summed in gamma space and composited by additive.rs); y: the
    /// soft-particle depth (graphics.rs); z: the scene brightness a white alpha-blended source is matched over
    /// (particles.rs WHITE_OVER)
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
    /// the additive pool draws in the transmissive pass, composited in gamma space (additive.rs)
    fn reads_view_transmission_texture(&self) -> bool {
        self.additive
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
