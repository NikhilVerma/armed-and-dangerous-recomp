// Layered fx meshes (the snow domes fxo_snow / fxo_snow_2): the original's fixed-function stages, read from its
// material log ([test] log_materials, docs/remaster-engine.md):
//   stage 0: TEMP    = t0 * diffuse            (t0: the base texture, uv scroll layer 1)
//   stage 1: CURRENT = t1 * diffuse            (t1: the second texture, uv scroll layer 2)
//   stage 2: colour  = LERP(t2.rgb, CURRENT, TEMP)  (t2: the mask, op 26 = D3DTOP_LERP), alpha = t2.a * factor
//   blend:   SRCALPHA, ONE (table 0x0057d9e0 entry 1): additive
// Gamma space as the original (samples back to gamma, the result to linear).
import bevy_pbr::render::forward_io::VertexOutput;

struct FxLayerParams {
    // xy: layer 1 offset, zw: layer 2 offset
    offsets: vec4<f32>,
    // rgb: the material colour (emissive: the domes are unlit), w: the fade / texture factor
    colour: vec4<f32>,
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

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let a = g(textureSample(t0, s0, in.uv + p.offsets.xy).rgb) * p.colour.rgb;
    let b = g(textureSample(t1, s1, in.uv + p.offsets.zw).rgb) * p.colour.rgb;
    let m = textureSample(t2, s2, in.uv);
    let col = mix(a, b, g(m.rgb));
    // stage 1 alpha SELECTARG1 TEXTURE (the flakes' alpha), stage 2 alpha MODULATE(CURRENT, TEXTURE): flakes x mask
    let fa = textureSample(t1, s1, in.uv + p.offsets.zw).a;
    let alpha = fa * m.a * p.colour.w;
    // Additive (SRCALPHA, ONE) as the original: the stage colour times the stage alpha. The flake texture is stored
    // premultiplied (DXT2), so the flake profile is in effect squared: faint specks vanish and the larger flakes
    // stay, as in the original's sparse round flakes (synced walk frame 520, work/remaster/render/snow_520i.png).
    return vec4(col * alpha, 0.0);
}
