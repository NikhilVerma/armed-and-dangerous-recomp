// A UI part drawn additively (hudblend.rs): the texture times the material's colour; the pipeline adds it to what is
// behind in proportion to its alpha (SRCALPHA / ONE, the original's blend mode 0).
import bevy_sprite_render::mesh2d::vertex_output::VertexOutput;
import bevy_sprite_render::mesh2d::view_bindings::view;

@if(TONEMAP_IN_SHADER)
import bevy_core_pipeline::tonemapping;
@if(SRGB_OUTPUT)
import bevy_render::color_operations::linear_to_srgb;
@if(OKLAB_OUTPUT)
import bevy_render::color_operations::linear_rgb_to_oklab;

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var<uniform> color: vec4<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var tex: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var tex_sampler: sampler;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    var out = color * textureSample(tex, tex_sampler, in.uv);
@if(TONEMAP_IN_SHADER)
    out = tonemapping::tone_mapping(out, view.color_grading);
@if(SRGB_OUTPUT)
    out = vec4(linear_to_srgb(out.rgb), out.a);
@if(OKLAB_OUTPUT)
    out = vec4(linear_rgb_to_oklab(out.rgb), out.a);
    return out;
}
