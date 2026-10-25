//! The original's materials (gamematerial.wesl): kinds, extra maps and colours from the exported material records
//! (tools/remaster_export.py, docs/formats/big-skin.md 3.5), the level's lights as the original lights them
//! (the sun per pixel through the normal map, the ambient-only light and the fill per vertex), the level's linear
//! fog. Rendering piece 1 (docs/remaster-engine.md): the landscape first, compared with tools/render_compare.py.
//!
//! Two material types share the data and the main pass: [`GameMaterial`] (solid: the depth-only passes draw the whole
//! triangle, with no fragment work) and [`AlphaTestedGameMaterial`] (alpha textures: the sun's shadow maps keep only
//! the texels whose alpha passes, gamematerial_prepass.wesl). [`Coverage`] says why this is a type, not a field.

use bevy::image::{ImageAddressMode, ImageLoaderSettings, ImageSampler};
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
    /// presentation settings (graphics.rs; zero in the Original): x the ambient occlusion's strength (0 on blended
    /// surfaces), y 1 = dissolve at the far clip instead of a hard cut, z the alpha below which an alpha-tested
    /// surface casts no shadow (0: the whole card casts, as the original's silhouettes)
    pub remaster: Vec4,
    /// the old remaster's height fog (graphics/height_fog.rs `HeightFogSettings::material_params`; x 0 = the level's
    /// linear fog)
    pub height_fog: Vec4,
    /// the old remaster's AO shaping (graphics/ao_extras.rs `AoExtras::material_params`)
    pub ao_extras: Vec4,
    /// the old remaster's ported settings (graphics/geometry/mod.rs `material_values`; zero in the Original but
    /// y): x terrain detail scale, y its strength, z the alpha cover (0 blend, 1 MSAA alpha-to-coverage), w shadow strength (0: the level's shadow colour)
    pub geometry: Vec4,
    /// the environment cube map (graphics/env_map.rs): x 1 = the atlas holds the level's baked cube (else `env` rgb
    /// is reflected)
    pub env_map: Vec4,
    /// the uv set each map is sampled with (MaterialDef::uv_set): x base, y normal map, z second texture, w mask
    pub uv_sets: UVec4,
}

/// How a game material covers what lies behind it in the depth-only passes (the sun's shadow cascades): the whole
/// triangle ([`Solid`]) or only where the base texture's alpha passes the test ([`AlphaTested`]: tree branch cards,
/// leaves). A type parameter rather than a field: Bevy chooses prepass shaders per material type, and a type with a
/// prepass shader of its own is bound in every depth-only pass, which breaks the solid casters' batching (the sun's
/// four cascades: 4K 113 -> 64 fps, 2026-10-15). So only the alpha-tested surfaces, which the shadow pass binds
/// anyway (their alpha mode may discard), run a fragment shader there.
pub trait Coverage: TypePath + Clone + Default + std::fmt::Debug + Send + Sync + 'static {
    /// whether the depth-only passes run the alpha test (gamematerial_prepass.wesl)
    const ALPHA_TESTED: bool;
}

/// Opaque surfaces: depth only, no fragment shader in the shadow and depth prepasses.
#[derive(TypePath, Clone, Copy, Default, Debug)]
pub struct Solid;

/// Alpha-textured surfaces: the shadow pass discards where the base texture's alpha is below the cutoff.
#[derive(TypePath, Clone, Copy, Default, Debug)]
pub struct AlphaTested;

impl Coverage for Solid {
    const ALPHA_TESTED: bool = false;
}

impl Coverage for AlphaTested {
    const ALPHA_TESTED: bool = true;
}

/// A game material with an alpha texture (its record's `alpha`): alpha-tested shadows.
pub type AlphaTestedGameMaterial = GameMaterial<AlphaTested>;

#[derive(AsBindGroup, Debug, Clone, Asset, TypePath)]
pub struct GameMaterial<C: Coverage = Solid> {
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
    /// the level's environment cube map, its six faces in a 3 x 2 atlas (graphics/env_map.rs)
    #[texture(9)]
    #[sampler(10)]
    pub env_atlas: Handle<Image>,
    pub alpha: bool,
    /// a snow surface by name (graphics.rs `ao_exempt`): keeps only `ao_snow` of the ambient occlusion
    pub ao_snow_surface: bool,
    pub coverage: std::marker::PhantomData<C>,
}

/// The vertex location of the third uv set in the main pass (past Bevy's mesh attributes, 0..7).
const UV_C_LOCATION: u32 = 10;

fn embedded_shader(path: std::path::PathBuf) -> bevy::shader::ShaderRef {
    bevy::shader::ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(path).with_source("embedded"))
}

impl<C: Coverage> Material for GameMaterial<C> {
    fn vertex_shader() -> bevy::shader::ShaderRef {
        embedded_shader(bevy::asset::embedded_path!("gamematerial.wesl"))
    }
    fn fragment_shader() -> bevy::shader::ShaderRef {
        embedded_shader(bevy::asset::embedded_path!("gamematerial.wesl"))
    }
    // Solid: none of its own (see Coverage). Where the depth prepass holds the depth of pixels this material clips at
    // the far plane, the sky fills them (graphics/farclip.rs).
    fn prepass_fragment_shader() -> bevy::shader::ShaderRef {
        if C::ALPHA_TESTED { embedded_shader(bevy::asset::embedded_path!("gamematerial_prepass.wesl")) } else { bevy::shader::ShaderRef::Default }
    }
    /// With a depth prepass, the colour pass of a solid game material tests against the depth that Bevy's own prepass
    /// shader wrote for the same triangles. Two shader programs computing the same clip position need not round it
    /// alike, and where this one came out a hair farther the GreaterEqual test failed: whole triangles of distant walls
    /// stayed the clear (fog) colour (lev_bonus_m1's orange trapezoids, findings B4) and thin pale slivers showed the
    /// sky through lev_4_m3's mill walls (B3), in every preset with a prepass (TAA, SSAO, SMAA + AO), never without.
    /// A depth bias toward the camera on the colour pass of the prepassed (opaque) pipelines takes the rounding back;
    /// the depth it writes is the prepass's anyway.
    fn specialize(
        _pipeline: &bevy::pbr::MaterialPipeline,
        descriptor: &mut bevy::render::render_resource::RenderPipelineDescriptor,
        layout: &bevy::mesh::MeshVertexBufferLayoutRef,
        key: bevy::pbr::MaterialPipelineKey<Self>,
    ) -> Result<(), bevy::render::render_resource::SpecializedMeshPipelineError> {
        let prepass_pipeline = descriptor.vertex.shader_defs.iter().any(|d| matches!(d, bevy::shader::ShaderDefVal::Bool(n, true) if n == "PREPASS_PIPELINE"));
        if !prepass_pipeline {
            // the third uv set (gamematerial.wesl `uv_c`, location UV_C_LOCATION) from the mesh's own interleaved buffer:
            // lev_3_m3's / lev_bonus_m1's terrain masks (uv_sets 0x90) span the level once on it; a mesh without one
            // passes its first set there (no record of it reads the third)
            let third = if layout.0.contains(crate::scene::ATTRIBUTE_UV_2.id) { crate::scene::ATTRIBUTE_UV_2 } else { Mesh::ATTRIBUTE_UV_0 };
            let extra = layout.0.get_layout(&[third.at_shader_location(UV_C_LOCATION)])?;
            if let Some(buffer) = descriptor.vertex.buffers.first_mut() {
                buffer.attributes.extend(extra.attributes);
            }
        }
        if key.mesh_key.contains(bevy::pbr::MeshPipelineKey::DEPTH_PREPASS)
            && !prepass_pipeline
            && let Some(ds) = descriptor.depth_stencil.as_mut()
            && ds.depth_write_enabled == Some(true)
        {
            // reverse Z: a positive bias is nearer
            ds.bias = bevy::render::render_resource::DepthBiasState { constant: 4, slope_scale: 1.0, clamp: 0.0 };
        }
        Ok(())
    }
    fn alpha_mode(&self) -> AlphaMode {
        // alpha textures blend (the material log: flags bits 8-11 = 3 -> blend table 0x0057d9e0 entry 3 = SRCALPHA,
        // INVSRCALPHA, ADD, with alpha test on), not a hard 0.5 cut: thin soft twigs as in the original
        // objects the camera fades (camfade.rs: emissive.w > 0) blend too
        // remaster alpha_coverage (graphics/geometry): an alpha texture (not a translucent record, not fading) covers
        // by its alpha with MSAA's alpha-to-coverage instead
        if self.params.emissive.w > 0.0 || (self.alpha && self.params.diffuse.w < 1.0) {
            return AlphaMode::Blend;
        }
        if !self.alpha {
            return AlphaMode::Opaque;
        }
        if self.params.geometry.z > 0.5 { AlphaMode::AlphaToCoverage } else { AlphaMode::Blend }
    }
}

pub struct GameMaterialPlugin;

impl Plugin for GameMaterialPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "gamematerial.wesl");
        bevy::asset::embedded_asset!(app, "gamematerial_prepass.wesl");
        // the far clip, shared with the sky (graphics/farclip.rs)
        bevy::shader::load_shader_library!(app, "gamematerial_shared.wesl");
        // the parameters and maps, shared by the main and the depth-only passes
        bevy::shader::load_shader_library!(app, "gamematerial_bindings.wesl");
        app.add_plugins((MaterialPlugin::<GameMaterial>::default(), MaterialPlugin::<AlphaTestedGameMaterial>::default()))
            .add_systems(PostUpdate, swap.run_if(resource_exists::<GameLights>));
    }
}

/// One original material record's game material, of the type its alpha texture calls for.
#[derive(Clone)]
enum MadeMaterial {
    Solid(Handle<GameMaterial>),
    AlphaTested(Handle<AlphaTestedGameMaterial>),
}

/// Every part spawned with a standard material that came from an original material record gets the game material
/// instead (one game material per standard one).
fn swap(
    mut commands: Commands,
    q: Query<(Entity, &MeshMaterial3d<StandardMaterial>), Added<MeshMaterial3d<StandardMaterial>>>,
    lib: Option<Res<crate::scene::Library>>,
    lights: Res<GameLights>,
    assets: Res<AssetServer>,
    mut materials: ResMut<Assets<GameMaterial>>,
    mut alpha_materials: ResMut<Assets<AlphaTestedGameMaterial>>,
    // per level: the next level's library has standard materials of its own (the old ones' game materials go with it)
    mut made: crate::levelreset::PerLevel<std::collections::HashMap<AssetId<StandardMaterial>, MadeMaterial>>,
) {
    let Some(lib) = lib else { return };
    let made = made.get();
    for (e, m) in &q {
        let Some((def, texture, alpha)) = lib.game_defs.get(&m.0.id()) else { continue };
        let made = made.entry(m.0.id()).or_insert_with(|| {
            let (texture, white) = (texture.as_deref(), &lights.white);
            if *alpha {
                MadeMaterial::AlphaTested(alpha_materials.add(make(def, texture, true, &lights, &assets, white)))
            } else {
                MadeMaterial::Solid(materials.add(make(def, texture, false, &lights, &assets, white)))
            }
        });
        let mut e = commands.entity(e);
        e.remove::<MeshMaterial3d<StandardMaterial>>();
        match made {
            MadeMaterial::Solid(h) => e.insert(MeshMaterial3d(h.clone())),
            MadeMaterial::AlphaTested(h) => e.insert(MeshMaterial3d(h.clone())),
        };
    }
}

/// A material record as exported.
#[derive(Deserialize, Clone, Default, Debug)]
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
    /// translucency (kind bit 0x10; the draw state 0x004897d0): the blend mode (byte +0x37: 2 = SRCALPHA /
    /// INVSRCALPHA, the only one world models use) and the opacity +0x48, the constant alpha the texture's is
    /// multiplied by (glass: the forge cannon's sight 0.35, the bomber's canopy 0.54)
    #[serde(default)]
    pub blend: u8,
    #[serde(default = "opaque")]
    pub opacity: f32,
    /// an environment material's (kind bit 4 without a mask) cube map strength: the record's float +0x40, which the
    /// material set-up makes the texture factor's grey (archive/src/loader/loader_01.cpp, the 0x7000 colour mix;
    /// graphics/env_map.rs). Exports before 2026-10-23 lack it: 0.3, the value of every environment record in the
    /// shipped levels checked (lev_1_m1's ice and water, the bronze pipes)
    #[serde(default = "env_factor")]
    pub env_factor: f32,
    /// the uv set each map is sampled with, two bits apiece (the record's byte +0x32): base, normal map, second
    /// texture, mask ([`MapSlot`]). RenderModes_Load (0x00489b40) gives them to the stages with their textures and the
    /// stage addressing (0x00504ef0) packs set + 1 into the vertex shader's key, whose shader passes TEXCOORD<set> on
    /// (shaders.dat "2100212": bump, base on set 0, normal map on set 1). The village walls' and the boulders' plaster
    /// normal map runs on set 1 at ~5x the base's tiling (0x04); the terrain's mask on set 1 (0x40); the snow domes'
    /// second flake layer on set 1 and their mask on set 2 (0x90). Exports before 2026-10-25 lack it: 0x40, the rule
    /// the engine had (the mask on set 1)
    #[serde(default = "legacy_uv_sets")]
    pub uv_sets: u8,
}

/// A material's maps, in the order of their uv set bits ([`MaterialDef::uv_sets`]).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MapSlot {
    Base = 0,
    Normal = 1,
    Texture2 = 2,
    Mask = 3,
}

fn opaque() -> f32 {
    1.0
}

fn legacy_uv_sets() -> u8 {
    0x40
}

fn env_factor() -> f32 {
    0.3
}

impl MaterialDef {
    /// The opacity the surface is drawn with: below 1 only for a translucent record (kind bit 0x10).
    pub fn alpha(&self) -> f32 {
        if self.kind & 0x10 != 0 { self.opacity.clamp(0.0, 1.0) } else { 1.0 }
    }

    /// The uv set (0..3) `map` is sampled with.
    pub fn uv_set(&self, map: MapSlot) -> u32 {
        (self.uv_sets as u32 >> (2 * map as u32)) & 3
    }

    /// The four maps' uv sets (x base, y normal map, z second texture, w mask), for a shader.
    pub fn uv_set_vec(&self) -> UVec4 {
        UVec4::new(self.uv_set(MapSlot::Base), self.uv_set(MapSlot::Normal), self.uv_set(MapSlot::Texture2), self.uv_set(MapSlot::Mask))
    }
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
    /// the level's far plane at draw distance 1 (level_clip_far); options.rs world_far scales it by the option
    pub clip_far: f32,
    /// what the environment materials reflect where the level has no env map marker (or env_maps is off): the fog
    /// colour, the sky at the horizon (approximation; the original then has no cube texture at all, see
    /// graphics/env_map.rs)
    pub env: Vec3,
    /// the level's env map marker (graphics/env_map.rs), where a bake renders the cube from
    pub env_probe: Option<crate::graphics::env_map::EnvProbe>,
    /// the level's baked cube, its faces 3 x 2 in one image (graphics/env_map.rs), when `env_baked`
    pub env_atlas: Handle<Image>,
    pub env_baked: bool,
    /// the height the old remaster's height fog is calibrated at: the camera's when the level started
    /// (graphics/height_fog.rs; None until the level's first frame)
    pub fog_reference: Option<f32>,
    /// a white texture for the unused maps
    pub white: Handle<Image>,
}

impl GameLights {
    pub fn from_scene(scene: &crate::scene::SceneFile, data: &crate::gamedata::GameData) -> Self {
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
        let (near, far, c) = level_fog(scene, data);
        l.fog = Some((near, far, c));
        l.clip_far = level_clip_far(scene, data, far);
        l.env = c;
        l
    }
}

/// The level's linear fog: start, end, colour (0..1). LevelProperties_Load (archive/src/renderer/render_r6.cpp,
/// the global_settings object) reads each value through the property reader: the level's own global_settings
/// value, else its property set's (p_globalsettings: fog_near 10, fog_far 6000, colour 255), else the reader's
/// default (fog_near 10, fog_far 3000, colour 255), and always sets the fog. Nine levels give fog_far and the colour
/// but not fog_near (lev_3_m*, lev_5_m1/2/3/5, lev_bonus_m1/2): the engine took that for "no fog" until 2026-10-23,
/// so lev_3_m3's town through the arch showed dark instead of in its orange haze and lev_5_m3's night had no fog.
pub fn level_fog(scene: &crate::scene::SceneFile, data: &crate::gamedata::GameData) -> (f32, f32, Vec3) {
    let set = data.template_props("global_settings");
    let g = |k: &str, default: f32| scene.settings.get(k).and_then(|v| v.as_f64()).map(|v| v as f32).unwrap_or_else(|| set.f(k, default));
    (g("fog_near", 10.0), g("fog_far", 3000.0), Vec3::new(g("fog_red", 255.0), g("fog_green", 255.0), g("fog_blue", 255.0)) / 255.0)
}

/// The level's far plane (camera +0xd4, Camera_SetFar 0x0040f8b0 from LevelProperties_Load 0x00423900): the level's
/// clip_far (global_settings, its property set: p_globalsettings 0), or when that is 0 fog_far plus the diagonal of
/// one landscape grid cell (sqrt(s * s + s * s), s = landscape +0x2c, 0x00449440; the world is culled by whole cells).
/// lev_1_m1: 1500 + 150 * sqrt 2 = 1712.132, the far plane of its camera dumps. lev_bonus_m1 / lev_3_m3 give 5000,
/// lev_4_m5 / lev_5_m5 12000: their distant walls and towers stood beyond the old fog_far-based guess. Scenes exported
/// before `cell_size` was written fall back to that guess (fog_far * 1712.132 / 1500).
pub fn level_clip_far(scene: &crate::scene::SceneFile, data: &crate::gamedata::GameData, fog_far: f32) -> f32 {
    let set = data.template_props("global_settings");
    let clip = scene.settings.get("clip_far").and_then(|v| v.as_f64()).map(|v| v as f32).unwrap_or_else(|| set.f("clip_far", 0.0));
    if clip != 0.0 {
        return clip;
    }
    match scene.cell_size {
        Some(s) => (s * s + s * s).sqrt() + fog_far,
        None => fog_far * crate::options::DEFAULT_CLIP_FAR / 1500.0,
    }
}

/// A texture by its game name (the remaster's version when the preset uses one: graphics.rs), with the world
/// sampler (repeat, trilinear, anisotropic).
pub fn load_tex(assets: &AssetServer, name: &str, srgb: bool) -> Handle<Image> {
    let sampler = crate::graphics::world_sampler(ImageAddressMode::Repeat, false);
    assets
        .load_builder()
        .with_settings(move |s: &mut ImageLoaderSettings| {
            s.is_srgb = srgb;
            s.sampler = ImageSampler::Descriptor(sampler.clone());
        })
        .load(crate::graphics::texture_path(name))
}

/// A game material for one submesh.
pub fn make<C: Coverage>(def: &MaterialDef, texture: Option<&str>, alpha: bool, lights: &GameLights, assets: &AssetServer, white: &Handle<Image>) -> GameMaterial<C> {
    let tex = |n: Option<&str>, srgb: bool| n.map(|n| load_tex(assets, n, srgb)).unwrap_or_else(|| white.clone());
    let v4 = |c: [f32; 3], w: f32| Vec4::new(c[0], c[1], c[2], w);
    let l3 = |v: Vec3, w: f32| v.extend(w);
    let (fog_col, fog_range) = match lights.fog {
        // far clip at the original camera's far plane (level_clip_far): beyond it the sky dome shows, as in its
        // captures (without the clip a hard pale band of fully fogged land showed)
        Some((near, far, c)) => (c.extend(1.0), Vec4::new(near, far, crate::options::world_far(lights.clip_far, 1.0), 0.0)),
        None => (Vec4::ZERO, Vec4::ZERO),
    };
    // debug (render investigations): AND_DEBUG_NOFOG=1 draws without fog and far clip
    let (fog_col, fog_range) = if std::env::var("AND_DEBUG_NOFOG").is_ok() { (Vec4::ZERO, Vec4::ZERO) } else { (fog_col, fog_range) };
    // a translucent record blends as an alpha texture does (fn_004897d0: kind bit 0x10, blend mode 2)
    let alpha = alpha || def.alpha() < 1.0;
    let ao_snow_surface = crate::graphics::ao_exempt(&[texture, def.texture2.as_deref(), def.normal_map.as_deref()]);
    let g = crate::graphics::current();
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
            // w: the record's opacity (translucent records; 1 otherwise)
            diffuse: v4(def.diffuse, def.alpha()),
            specular: v4(def.specular, if def.specular.iter().any(|c| *c > 0.0) { def.power } else { 0.0 }),
            // the cube * TFACTOR stage: the record's +0x40 grey (MaterialDef::env_factor)
            // a level without an env map marker binds no cube at all (fn_00487e30: EnvMap_FindNearest finds none,
            // the stage's texture is 0) and the stage adds black: lev_2_m2's bronze pipes are rust, not pale cream
            env: l3(if lights.env_probe.is_some() { lights.env } else { Vec3::ZERO }, def.env_factor.clamp(0.0, 1.0)),
            sun_dir: l3(lights.sun_dir, 0.0),
            sun_col: l3(lights.sun_col, 0.0),
            fill_dir: l3(lights.fill_dir, 0.0),
            fill_col: l3(lights.fill_col, 0.0),
            ambient_light: l3(lights.ambient, 0.0),
            fog_col,
            fog_range,
            remaster: Vec4::new(crate::graphics::ao_weight(&g, alpha), if g.far_fade { 1.0 } else { 0.0 }, g.shadow_alpha_cutoff, 0.0),
            height_fog: g.height_fog.material_params(lights.fog_reference),
            ao_extras: g.ao_extras.material_params(ao_snow_surface),
            geometry: crate::graphics::geometry::for_material(crate::graphics::geometry::material_values(&g), alpha, def.alpha()),
            env_map: Vec4::new(if lights.env_baked { 1.0 } else { 0.0 }, 0.0, 0.0, 0.0),
            uv_sets: def.uv_set_vec(),
        },
        env_atlas: if lights.env_baked { lights.env_atlas.clone() } else { white.clone() },
        alpha,
        ao_snow_surface,
        coverage: std::marker::PhantomData,
    }
}
