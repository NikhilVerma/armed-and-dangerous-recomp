// The depth-only passes of the alpha-tested game materials (gamematerial.rs AlphaTestedGameMaterial): the sun's shadow
// cascades, and the depth prepass should such a surface ever be drawn there. A branch card or a leaf cluster covers
// only where its texture's alpha does, so its shadow has the leaves' shape, not the card's rectangle (player feedback
// 30). Same texture and uv set as the main pass (gamematerial.wesl: base_tex at uv set 0, no texture transform).
// The cutoff is the material's remaster.z (graphics.rs shadow_alpha_cutoff): 0 keeps every texel, the whole card.
// Bevy's own prepass vertex shader feeds this (the material has none of its own).
import bevy_pbr::prepass::io::VertexOutput;
import and_remaster::gamematerial_bindings::{p, base_tex, base_smp};
@if(PREPASS_FRAGMENT)
import bevy_pbr::prepass::io::FragmentOutput;
@if(PREPASS_FRAGMENT && MOTION_VECTOR_PREPASS)
import bevy_pbr::prepass::bindings::previous_view_uniforms;
@if(PREPASS_FRAGMENT && MOTION_VECTOR_PREPASS)
import bevy_pbr::render::mesh_view_bindings::view;

// Drops the texels the alpha test removes.
fn alpha_test(in: VertexOutput) {
@if(VERTEX_UVS_A)
    {
        if (textureSample(base_tex, base_smp, in.uv).a < p.remaster.z) {
            discard;
        }
    }
}

// Shadow cascades (no targets: depth only).
@if(!PREPASS_FRAGMENT)
@fragment
fn fragment(in: VertexOutput) {
    alpha_test(in);
}

// With targets (a normal or motion vector prepass) or the emulated unclipped depth of the cascades: Bevy's default
// prepass outputs, after the test.
@if(PREPASS_FRAGMENT)
@fragment
fn fragment(in: VertexOutput) -> FragmentOutput {
    alpha_test(in);
    var out: FragmentOutput;
@if(NORMAL_PREPASS)
    out.normal = vec4(in.world_normal * 0.5 + vec3(0.5), 1.0);
@if(UNCLIPPED_DEPTH_ORTHO_EMULATION)
    out.frag_depth = in.unclipped_depth;
@if(MOTION_VECTOR_PREPASS)
    {
        let clip_t = view.unjittered_clip_from_world * in.world_position;
        let prev_t = previous_view_uniforms.unjittered_clip_from_world * in.previous_world_position;
        out.motion_vector = (clip_t.xy / clip_t.w - prev_t.xy / prev_t.w) * vec2(0.5, -0.5);
    }
    return out;
}
