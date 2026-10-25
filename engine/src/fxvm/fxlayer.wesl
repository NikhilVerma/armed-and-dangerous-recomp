// Layered fx meshes (the snow domes fxo_snow / fxo_snow_2): the original's fixed-function stages, read from its
// material log ([test] log_materials, docs/remaster-engine.md):
//   stage 0: TEMP    = t0 * diffuse            (t0: the base texture, uv scroll layer 1)
//   stage 1: CURRENT = t1 * diffuse            (t1: the second texture, uv scroll layer 2)
//   stage 2: colour  = LERP(t2.rgb, CURRENT, TEMP)  (t2: the mask, op 26 = D3DTOP_LERP), alpha = t2.a * factor
//   blend:   SRCALPHA, ONE (table 0x0057d9e0 entry 1): additive
// Gamma space as the original (samples back to gamma); the result summed and composited by additive.rs (see the end).
// A background (opcode 0x62, Fx_SetBackground 0x0041fe00: lev_5's sea and cloud dome; entry point fragment_background)
// is an opaque record: the same stages, no blend, the level's fog (fixed-function table fog: per pixel on view depth), drawn with the sky.
// Each stage reads the uv set its record names (p.uv_sets, the record's byte +0x32; the original's vertex shader key
// holds set + 1 per stage): the snow domes' t0 on set 0 (0..12 x 0..6 over the dome), t1 on set 1 (fxo_snow 0..18 x
// 0..9, fxo_snow_2 0..24 x 0..14: smaller, denser flakes than the first layer's) and the wind-spray mask on set 2
// (0..1: once over the whole dome). Hence a vertex stage of its own: Bevy's mesh vertex passes on two sets only.
import bevy_pbr::render::mesh_functions;
import bevy_pbr::render::view_transformations::position_world_to_clip;
import bevy_pbr::render::mesh_view_bindings::{view, globals};
import bevy_pbr::render::view_transformations::depth_ndc_to_view_z;
@if(DEPTH_PREPASS)
import bevy_pbr::prepass::utils::prepass_depth;
import and_remaster::gamematerial_shared::far_clipped;
import and_remaster::graphics::height_fog::height_fog;

struct FxLayerParams {
    // the uv transforms' rows of layers 1 and 2: u' = x.x u + x.y v + x.z, v' = y.x u + y.y v + y.z
    uv1_x: vec4<f32>,
    uv1_y: vec4<f32>,
    uv2_x: vec4<f32>,
    uv2_y: vec4<f32>,
    // rgb: the material colour (emissive: the domes are unlit), w: the fade / texture factor
    colour: vec4<f32>,
    // x, y, z: t0, t1, t2 stored premultiplied (the export's `premultiplied`: DXT2 in the bank)
    flags: vec4<f32>,
    // a background: the fog colour (gamma), w 1 = fogged; start, end, the far clip and its dissolve; the height fog's
    // parameters (x 0 = the linear fog); toward the sun
    fog_col: vec4<f32>,
    fog_range: vec4<f32>,
    height_fog: vec4<f32>,
    sun_dir: vec4<f32>,
    // the uv set of t0, t1, t2 (0..2)
    uv_sets: vec4<u32>,
}

// The mesh's uv sets (materials.rs specialize binds the ones it has: a set it lacks reads the first)
struct LayerIn {
    @builtin(instance_index) instance_index: u32,
    @location(0) position: vec3<f32>,
    @location(1) uv: vec2<f32>,
@if(LAYER_UV_1)
    @location(2) uv_1: vec2<f32>,
@if(LAYER_UV_2)
    @location(3) uv_2: vec2<f32>,
}

struct LayerOut {
    @builtin(position) position: vec4<f32>,
    @location(0) world_position: vec4<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) uv_1: vec2<f32>,
    @location(3) uv_2: vec2<f32>,
}

@vertex
fn vertex(v: LayerIn) -> LayerOut {
    var out: LayerOut;
    let world_from_local = mesh_functions::get_world_from_local(v.instance_index);
    out.world_position = mesh_functions::mesh_position_local_to_world(world_from_local, vec4(v.position, 1.0));
    out.position = position_world_to_clip(out.world_position.xyz);
    out.uv = v.uv;
    out.uv_1 = v.uv;
    out.uv_2 = v.uv;
@if(LAYER_UV_1)
    out.uv_1 = v.uv_1;
@if(LAYER_UV_2)
    out.uv_2 = v.uv_2;
    return out;
}

// The texture coordinates of uv set `uv_set`
fn uv_of(in: LayerOut, uv_set: u32) -> vec2<f32> {
    return select(select(in.uv, in.uv_1, uv_set == 1u), in.uv_2, uv_set >= 2u);
}

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var t0: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var s0: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var t1: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(3) var s1: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(4) var t2: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(5) var s2: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(6) var<uniform> p: FxLayerParams;

fn g(c: vec3<f32>) -> vec3<f32> {
    return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}

// A texel's colour in the original's (gamma) space, un-premultiplied when the texture is stored premultiplied
fn colour_of(s: vec4<f32>, premultiplied: f32) -> vec3<f32> {
    let c = g(s.rgb);
    return select(c, min(c / max(s.a, 1.0 / 255.0), vec3(1.0)), premultiplied > 0.5);
}

// The three stages' colour (gamma) and alpha
fn stages(in: LayerOut) -> vec4<f32> {
    // the stage transforms (texture animation) apply to each stage's own set
    let uv_a = vec3(uv_of(in, p.uv_sets.x), 1.0);
    let uv_b = vec3(uv_of(in, p.uv_sets.y), 1.0);
    let s0 = textureSample(t0, s0, vec2(dot(p.uv1_x.xyz, uv_a), dot(p.uv1_y.xyz, uv_a)));
    let s1 = textureSample(t1, s1, vec2(dot(p.uv2_x.xyz, uv_b), dot(p.uv2_y.xyz, uv_b)));
    let m = textureSample(t2, s2, uv_of(in, p.uv_sets.z));
    let a = colour_of(s0, p.flags.x) * p.colour.rgb;
    let b = colour_of(s1, p.flags.y) * p.colour.rgb;
    // stage 2 LERP(mask, CURRENT (t1), TEMP (t0)) by the mask's colour
    let col = mix(a, b, colour_of(m, p.flags.z));
    // stage 1 alpha SELECTARG1 TEXTURE (t1's alpha), stage 2 alpha MODULATE(CURRENT, TEXTURE): t1 x mask
    return vec4(col, s1.a * m.a);
}

@fragment
fn fragment(in: LayerOut) -> @location(0) vec4<f32> {
    let s = stages(in);
    let col = s.rgb;
    let alpha = s.a * p.colour.w;
    // additive (SRCALPHA, ONE): the stage colour times the stage alpha. Textures stored premultiplied (DXT2: the
    // snow flakes, the blast ring) are un-premultiplied first: the flakes' colour is white where they show.
    // the stage maths run in gamma space (as the original), and so does the addition: the material draws in the
    // transmissive pass, the blend sums these gamma-space colours and additive.rs composites the sum over the scene as
    // the original adds it ((dst + sum)^2.2 in linear light; until 2026-10-25 the gamma value was added as linear
    // light, within a factor ~2 over the snow)
    return vec4(max(col * alpha, vec3(0.0)), 0.0);
}

// sRGB curve (the game material's, gamematerial.wesl): a fully fogged pixel is exactly the level's clear colour
fn srgb_to_linear(c: vec3<f32>) -> vec3<f32> {
    let x = max(c, vec3(0.0));
    return select(pow((x + 0.055) / 1.055, vec3(2.4)), x / 12.92, x <= vec3(0.04045));
}

struct BackgroundOut {
    @location(0) color: vec4<f32>,
    @builtin(frag_depth) depth: f32,
}

// A background's pixel (the entry point materials.rs picks for a background material, which is opaque): the stages'
// colour as it is (no blend), behind everything as the sky (graphics/farclip.wesl): behind a depth prepass it keeps
// only what nothing is in front of, or what is in front was far-clipped; without one it lies at the far end of the
// depth range (depth 0, reverse Z). Then the level's fog.
@fragment
fn fragment_background(in: LayerOut) -> BackgroundOut {
@if(DEPTH_PREPASS) {
    // reverse Z, cleared to 0: a background has no prepass, so any depth there is the world in front of it
    let d = prepass_depth(in.position, 0u);
    if (d > 0.0) {
        if (!far_clipped(-depth_ndc_to_view_z(d), p.fog_range.z, p.fog_range.w > 0.5, in.position.xy, globals.frame_count)) {
            discard;
        }
    }
}
    var col = stages(in).rgb;
    if (p.fog_col.w > 0.5 && p.fog_range.y > p.fog_range.x) {
        if (p.height_fog.x > 0.0) {
            // the Remaster's height fog, as the game material applies it
            let fog = height_fog(in.world_position.xyz - view.world_position.xyz, view.world_position.y, p.fog_col.rgb, p.fog_range.xy, p.height_fog, normalize(p.sun_dir.xyz));
            col = mix(fog.rgb, col, fog.a);
        } else {
            let depth = -(view.view_from_world * vec4(in.world_position.xyz, 1.0)).z;
            col = mix(p.fog_col.rgb, col, clamp((p.fog_range.y - depth) / (p.fog_range.y - p.fog_range.x), 0.0, 1.0));
        }
    }
    var out: BackgroundOut;
    out.color = vec4(srgb_to_linear(col), 1.0);
    out.depth = 0.0;
    return out;
}
