// The game material's parameters and maps (gamematerial.rs GameMaterial, its AsBindGroup layout), shared by the main
// pass (gamematerial.wesl) and the alpha-tested depth-only passes (gamematerial_prepass.wesl).

struct GameMaterialParams {
    kind: u32,
    has_normal: u32,
    has_texture2: u32,
    has_mask: u32,
    emissive: vec4<f32>,
    ambient: vec4<f32>,
    diffuse: vec4<f32>,
    // rgb specular colour, w power
    specular: vec4<f32>,
    // the environment cube's colour (approximation: the sky; the original renders a cube map) and its factor in w
    env: vec4<f32>,
    // toward the sun (engine space), w unused; sun colour * intensity
    sun_dir: vec4<f32>,
    sun_col: vec4<f32>,
    fill_dir: vec4<f32>,
    fill_col: vec4<f32>,
    ambient_light: vec4<f32>,
    // fog colour rgb; near, far in fog_range.xy (0 0: no fog); fog_range.z the far plane (0: none)
    fog_col: vec4<f32>,
    fog_range: vec4<f32>,
    // presentation (graphics.rs; zero in the Original): x the ambient occlusion's weight (0: exempt, as snow),
    // y 1 = dissolve over the last 15 % before the far clip instead of a hard cut, z the alpha below which an
    // alpha-tested surface casts no shadow (0: the whole card casts)
    remaster: vec4<f32>,
    // the old remaster's height fog (graphics/height_fog.rs; x 0 = the level's linear fog): x density scale, y height
    // falloff per unit, z sun glow, w the level's reference height
    height_fog: vec4<f32>,
    // the old remaster's AO shaping (graphics/ao_extras.rs): x ao_contact, y ao_snow, z 1 = snow surface
    ao_extras: vec4<f32>,
    // the old remaster's ported settings (graphics/geometry/mod.rs material_values): x terrain detail scale (0 off),
    // y its strength, z alpha cover (0 blend, 1 MSAA alpha-to-coverage), w shadow strength (0: the
    // level's shadow colour)
    geometry: vec4<f32>,
    // the environment cube map (graphics/env_map.rs): x 1 = env_tex holds the level's baked cube (3 x 2 faces)
    env_map: vec4<f32>,
    // the uv set each map is sampled with (gamematerial.rs MaterialDef::uv_sets): x base, y normal map, z second
    // texture, w mask; 0 the first set, 1 the second (no world record uses a third)
    uv_sets: vec4<u32>,
    // x 1 = added to the scene (SRCALPHA / ONE): gamma-space colour times alpha, composited by fxvm/additive.rs
    blend: vec4<u32>,
}

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var base_tex: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var base_smp: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var normal_tex: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(3) var normal_smp: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(4) var tex2: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(5) var tex2_smp: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(6) var mask_tex: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(7) var mask_smp: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(8) var<uniform> p: GameMaterialParams;
@group(constants::MATERIAL_BIND_GROUP) @binding(9) var env_tex: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(10) var env_smp: sampler;
