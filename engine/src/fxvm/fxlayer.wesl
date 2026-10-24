// Layered fx meshes (the snow domes fxo_snow / fxo_snow_2): the original's fixed-function stages, read from its
// material log ([test] log_materials, docs/remaster-engine.md):
//   stage 0: TEMP    = t0 * diffuse            (t0: the base texture, uv scroll layer 1)
//   stage 1: CURRENT = t1 * diffuse            (t1: the second texture, uv scroll layer 2)
//   stage 2: colour  = LERP(t2.rgb, CURRENT, TEMP)  (t2: the mask, op 26 = D3DTOP_LERP), alpha = t2.a * factor
//   blend:   SRCALPHA, ONE (table 0x0057d9e0 entry 1): additive
// Gamma space as the original (samples back to gamma); the result summed and composited by additive.rs (see the end).
import bevy_pbr::render::forward_io::VertexOutput;

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

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let uv = vec3(in.uv, 1.0);
    let s0 = textureSample(t0, s0, vec2(dot(p.uv1_x.xyz, uv), dot(p.uv1_y.xyz, uv)));
    let s1 = textureSample(t1, s1, vec2(dot(p.uv2_x.xyz, uv), dot(p.uv2_y.xyz, uv)));
    let m = textureSample(t2, s2, in.uv);
    let a = colour_of(s0, p.flags.x) * p.colour.rgb;
    let b = colour_of(s1, p.flags.y) * p.colour.rgb;
    // stage 2 LERP(mask, CURRENT (t1), TEMP (t0)) by the mask's colour
    let col = mix(a, b, colour_of(m, p.flags.z));
    // stage 1 alpha SELECTARG1 TEXTURE (t1's alpha), stage 2 alpha MODULATE(CURRENT, TEXTURE): t1 x mask
    let alpha = s1.a * m.a * p.colour.w;
    // additive (SRCALPHA, ONE): the stage colour times the stage alpha. Textures stored premultiplied (DXT2: the
    // snow flakes, the blast ring) are un-premultiplied first: the flakes' colour is white where they show.
    // the stage maths run in gamma space (as the original), and so does the addition: the material draws in the
    // transmissive pass, the blend sums these gamma-space colours and additive.rs composites the sum over the scene as
    // the original adds it ((dst + sum)^2.2 in linear light; until 2026-10-25 the gamma value was added as linear
    // light, within a factor ~2 over the snow)
    return vec4(max(col * alpha, vec3(0.0)), 0.0);
}
