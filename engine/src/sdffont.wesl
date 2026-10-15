// Text from the font's signed distance field (sdffont.rs): the glyph's edge is the field's 0.5 level, anti-aliased
// over one screen pixel at any magnification, so the original's 25-pixel glyphs stay sharp at 4K.
import bevy_sprite_render::mesh2d::vertex_output::VertexOutput;
import bevy_sprite_render::mesh2d::view_bindings::view;

@if(TONEMAP_IN_SHADER)
import bevy_core_pipeline::tonemapping;
@if(SRGB_OUTPUT)
import bevy_render::color_operations::linear_to_srgb;
@if(OKLAB_OUTPUT)
import bevy_render::color_operations::linear_rgb_to_oklab;

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var<uniform> color: vec4<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var sdf: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var sdf_sampler: sampler;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    var out = color;
@if(VERTEX_COLORS)
    out = out * in.color;
    let d = textureSample(sdf, sdf_sampler, in.uv).r;
    // the field's change over one screen pixel: the edge's anti-aliasing width wherever the text is scaled
    let w = max(fwidth(d), 1.0e-4);
    out.a = out.a * clamp((d - 0.5) / w + 0.5, 0.0, 1.0);
@if(TONEMAP_IN_SHADER)
    out = tonemapping::tone_mapping(out, view.color_grading);
@if(SRGB_OUTPUT)
    out = vec4(linear_to_srgb(out.rgb), out.a);
@if(OKLAB_OUTPUT)
    out = vec4(linear_rgb_to_oklab(out.rgb), out.a);
    return out;
}
