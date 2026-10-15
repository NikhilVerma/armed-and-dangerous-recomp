// The game material's prepass fragment (depth, normals, motion vectors): Bevy's default prepass output, plus the
// far clip of gamematerial.wesl, so the prepass depth (SSAO, TAA, soft particles: graphics.rs) never keeps land
// that the main pass discards (it would hide the sky behind it). Only remaster settings run a prepass.
import bevy_pbr::prepass::io::VertexOutput;
@if(PREPASS_FRAGMENT)
import bevy_pbr::prepass::io::FragmentOutput;
import bevy_pbr::prepass::bindings as prepass_bindings;
import bevy_pbr::render::mesh_view_bindings::view;

// as gamematerial.wesl (the same uniform at binding 8; only fog_range and remaster are read here)
struct GameMaterialParams {
    kind: u32,
    has_normal: u32,
    has_texture2: u32,
    has_mask: u32,
    emissive: vec4<f32>,
    ambient: vec4<f32>,
    diffuse: vec4<f32>,
    specular: vec4<f32>,
    env: vec4<f32>,
    sun_dir: vec4<f32>,
    sun_col: vec4<f32>,
    fill_dir: vec4<f32>,
    fill_col: vec4<f32>,
    ambient_light: vec4<f32>,
    fog_col: vec4<f32>,
    fog_range: vec4<f32>,
    remaster: vec4<f32>,
}

@group(constants::MATERIAL_BIND_GROUP) @binding(8) var<uniform> p: GameMaterialParams;
// the prepass view layout has the globals at binding 1 (bevy_pbr prepass_view_layout)
@group(0) @binding(1) var<uniform> prepass_globals: bevy_render::globals::Globals;

fn dither(pixel: vec2<f32>) -> f32 {
    let q = pixel + 5.588238 * f32(prepass_globals.frame_count % 64u);
    return fract(52.9829189 * fract(dot(q, vec2(0.06711056, 0.00583715))));
}

fn far_clipped(depth: f32, pixel: vec2<f32>) -> bool {
    if (p.remaster.y > 0.0) {
        let t = smoothstep(p.fog_range.z * 0.85, p.fog_range.z, depth);
        return t >= 1.0 || (t > 0.0 && dither(pixel) < t);
    }
    return depth > p.fog_range.z;
}

fn clip(in: VertexOutput) {
    if (p.fog_range.z > 0.0) {
        let depth = -(view.view_from_world * vec4(in.world_position.xyz, 1.0)).z;
        if (far_clipped(depth, in.position.xy)) {
            discard;
        }
    }
}

// without colour targets this runs only in the sun's shadow pass for blended casters (MAY_DISCARD): it does
// nothing, as Bevy's prepass did without a fragment shader (the main view's prepass always has targets: graphics.rs
// adds normals with the depth prepass), so the Original's shadows are unchanged
@if(!PREPASS_FRAGMENT)
@fragment
fn fragment(in: VertexOutput) {
}

@if(PREPASS_FRAGMENT)
@fragment
fn fragment(in: VertexOutput) -> FragmentOutput {
    // (a shadow view with unclipped-depth emulation runs this too: no far clip from the light's view)
@if(!UNCLIPPED_DEPTH_ORTHO_EMULATION)
    clip(in);
    var out: FragmentOutput;

@if(NORMAL_PREPASS)
    out.normal = vec4(normalize(in.world_normal) * 0.5 + vec3(0.5), 1.0);

@if(UNCLIPPED_DEPTH_ORTHO_EMULATION)
    out.frag_depth = in.unclipped_depth;

@if(MOTION_VECTOR_PREPASS) {
    let clip_position_t = view.unjittered_clip_from_world * in.world_position;
    let clip_position = clip_position_t.xy / clip_position_t.w;
    let previous_clip_position_t = prepass_bindings::previous_view_uniforms.unjittered_clip_from_world * in.previous_world_position;
    let previous_clip_position = previous_clip_position_t.xy / previous_clip_position_t.w;
    out.motion_vector = (clip_position - previous_clip_position) * vec2(0.5, -0.5);
}

    return out;
}
