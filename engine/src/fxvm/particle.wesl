// Fx particles: texture x vertex colour, nothing else (the original draws a textured, coloured quad with
// additive or alpha blending). Much cheaper per pixel than the PBR material at 4K.
import bevy_pbr::render::forward_io::VertexOutput;

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var bank: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var bank_sampler: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var<uniform> params: vec4<f32>;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    var c = textureSample(bank, bank_sampler, in.uv);
    @if(VERTEX_COLORS) {
        c = c * in.color;
    }
    // alpha-blended pools: alpha ^ 2.2 (the original blends in gamma space, the target here is linear)
    c.a = pow(max(c.a, 0.0), params.x);
    return c;
}
