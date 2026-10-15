// Fx particles: texture x vertex colour, nothing else (the original draws a textured, coloured quad with
// additive or alpha blending). Much cheaper per pixel than the PBR material at 4K.
// Remaster (graphics.rs soft_particles): with the depth prepass, a particle fades out over params.y game units
// where it meets geometry, instead of the original's hard line where a quad cuts into the ground.
import bevy_pbr::render::forward_io::VertexOutput;
import bevy_pbr::render::mesh_view_bindings::view;
@if(DEPTH_PREPASS)
import bevy_pbr::prepass::utils as prepass_utils;

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var bank: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var bank_sampler: sampler;
// x: the alpha exponent; y: the soft-particle depth (0: hard, the original)
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var<uniform> params: vec4<f32>;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    var c = textureSample(bank, bank_sampler, in.uv);
    @if(VERTEX_COLORS) {
        c = c * in.color;
    }
    // alpha-blended pools: alpha ^ 2.2 (the original blends in gamma space, the target here is linear)
    c.a = pow(max(c.a, 0.0), params.x);
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
