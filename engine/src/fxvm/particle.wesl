// Fx particles: texture x vertex colour, nothing else (the original draws a textured, coloured quad with
// additive or alpha blending). Much cheaper per pixel than the PBR material at 4K.
// The quads' corners come from a storage buffer written whole each frame (particles.rs draw_particles: three vec4s
// per corner: position and u, v and rgb, alpha); the pool mesh only numbers the corners (position.x) and keeps its
// size, so no mesh is re-uploaded per frame.
// Remaster (graphics.rs soft_particles): with the depth prepass, a particle fades out over params.y game units
// where it meets geometry, instead of the original's hard line where a quad cuts into the ground.
import bevy_pbr::render::mesh_view_bindings::view;
@if(DEPTH_PREPASS)
import bevy_pbr::prepass::utils as prepass_utils;

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var bank: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var bank_sampler: sampler;
// x: the alpha exponent; y: the soft-particle depth (0: hard, the original)
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var<uniform> params: vec4<f32>;
// three per corner: (x, y, z, u), (v, r, g, b), (a, -, -, -); engine (world) space
@group(constants::MATERIAL_BIND_GROUP) @binding(3) var<storage, read> corners: array<vec4<f32>>;

struct CornerIn {
    // x: the corner's index
    @location(0) position: vec3<f32>,
}

struct ParticleOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
}

@vertex
fn vertex(v: CornerIn) -> ParticleOut {
    let i = u32(v.position.x) * 3u;
    let a = corners[i];
    let b = corners[i + 1u];
    let c = corners[i + 2u];
    var out: ParticleOut;
    out.position = view.clip_from_world * vec4(a.xyz, 1.0);
    out.uv = vec2(a.w, b.x);
    out.color = vec4(b.yzw, c.x);
    return out;
}

@fragment
fn fragment(in: ParticleOut) -> @location(0) vec4<f32> {
    var c = textureSample(bank, bank_sampler, in.uv) * in.color;
    // alpha-blended pools (params.x > 1): the original blends in gamma space, the target here is linear. A gamma-space
    // blend of a black source leaves (1 - a)^2.2 of the scene's linear light whatever the scene, so a dark source takes
    // 1 - (1 - a)^2.2; a white source over black shows a^2.2 (the intro's white fade, measured); between the two by the
    // source's gamma-space brightness. (alpha ^ 2.2 alone drew dark smoke, the keg's trail, at a fraction of its
    // opacity: a = 0.5 hid 22% of the scene where the original hid 78%.)
    let a = clamp(c.a, 0.0, 1.0);
    if (params.x > 1.0) {
        let lum = pow(clamp(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0), 1.0 / params.x);
        c.a = mix(1.0 - pow(1.0 - a, params.x), pow(a, params.x), lum);
    } else {
        c.a = a;
    }
@if(DEPTH_PREPASS)
    {
        if (params.y > 0.0) {
            // (sample 0 under MSAA: the fade is smooth anyway)
            // reverse-z, infinite far: view distance = near / ndc depth (clip_from_view[3][2] is the near plane)
            let scene = prepass_utils::prepass_depth(in.position, 0u);
            let near = view.clip_from_view[3][2];
            let gap = near / max(scene, 1e-7) - near / max(in.position.z, 1e-7);
            let fade = clamp(gap / params.y, 0.0, 1.0);
            // additive pools add colour (alpha exponent 1), blended pools blend by alpha: fade both
            c = vec4(c.rgb * select(1.0, fade, params.x == 1.0), c.a * fade);
        }
    }
    return c;
}
