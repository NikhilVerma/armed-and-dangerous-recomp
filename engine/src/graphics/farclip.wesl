// The sky (Bevy's standard material, unlit) where a depth prepass ran (graphics/farclip.rs): drawn without a depth
// test, it fills its own pixels and the ones the game material clips at the far plane, whose depth the prepass holds
// (gamematerial_shared.wesl decides both the same way); everywhere else it steps aside. Without a prepass (the
// Original preset) it is Bevy's standard fragment shader at the far end of the depth range (depth 0, reverse Z), so the
// world always covers it, as the original draws the sky first (ScenePass_Render 0x00489070).
import bevy_pbr::render::pbr_fragment::pbr_input_from_standard_material;
import bevy_pbr::render::pbr_functions::{alpha_discard, apply_pbr_lighting, main_pass_post_lighting_processing};
import bevy_pbr::render::forward_io::VertexOutput;
import bevy_pbr::render::pbr_types::STANDARD_MATERIAL_FLAGS_UNLIT_BIT;
import bevy_pbr::render::mesh_view_bindings::globals;
import bevy_pbr::render::view_transformations::depth_ndc_to_view_z;
@if(DEPTH_PREPASS)
import bevy_pbr::prepass::utils::prepass_depth;
import and_remaster::gamematerial_shared::far_clipped;

struct SkyClip {
    // the game material's far clip (its fog_range.z), 0: none
    far: f32,
    // 1: the Remaster's dissolve before it
    fade: f32,
}

@group(constants::MATERIAL_BIND_GROUP) @binding(100) var<uniform> clip: SkyClip;

struct SkyOut {
    @location(0) color: vec4<f32>,
    @builtin(frag_depth) depth: f32,
}

@fragment
fn fragment(in: VertexOutput, @builtin(front_facing) is_front: bool) -> SkyOut {
@if(DEPTH_PREPASS) {
    // reverse Z, cleared to 0: the sky has no prepass, so any depth there is the world in front of it; keep that unless
    // the game material clipped it
    let d = prepass_depth(in.position, 0u);
    if (d > 0.0) {
        let depth = -depth_ndc_to_view_z(d);
        if (!far_clipped(depth, clip.far, clip.fade > 0.0, in.position.xy, globals.frame_count)) {
            discard;
        }
    }
}
    // Bevy's standard fragment (pbr.wesl, forward)
    var pbr_input = pbr_input_from_standard_material(in, is_front);
    pbr_input.material.base_color = alpha_discard(pbr_input.material.flags, pbr_input.material.alpha_cutoff, pbr_input.material.base_color);
    var out: SkyOut;
    out.depth = 0.0;
    if ((pbr_input.material.flags & STANDARD_MATERIAL_FLAGS_UNLIT_BIT) == 0u) {
        out.color = apply_pbr_lighting(pbr_input);
    } else {
        out.color = pbr_input.material.base_color;
    }
    out.color = main_pass_post_lighting_processing(pbr_input, out.color);
    return out;
}
