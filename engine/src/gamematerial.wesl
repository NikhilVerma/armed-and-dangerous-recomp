// The original's shading (docs/remaster-engine.md "Rendering"; the three pixel shaders disassembled with
// tools/ps_disasm.py, the fixed-function stages from the material log, [test] log_materials):
//   lighting  v0 = emissive + ambient light * material ambient + fill light * material diffuse * N.L (per vertex in the
//             original: the vertex shader / D3D vertex lighting), c1 = sun * material diffuse,
//             light = sat(sat(Nmap . Lsun) * c1 + v0)
// The kind byte (material +0x34) is a set of flags: 1 textured, 2 normal map, 4 second texture, 8 blend mask
// (16 and 32 not identified):
//   normal map           bump (ps eac78aec9a00d1cf): base * light, the normal from the normal map
//   texture2 + mask      terrain (ps 98a72838035bad82) / shore blend: sat(base * (1 - mask) + texture2 * mask) * light,
//                        mask on uv set 1
//   texture2, no mask    environment (stages: cube * TFACTOR, then MULTIPLYADD texture * diffuse): env * factor +
//                        base * light (texture2 is the cube's slot; the game binds its own cube map there)
//   otherwise            fixed function: base * light (plus a specular highlight with flag 16: guess)
// then the level's linear fog (global_settings fog_near / fog_far / colour).
import bevy_pbr::render::forward_io::{Vertex, decompress_vertex};
import bevy_pbr::render::mesh_functions;
import bevy_pbr::render::skinning;
import bevy_pbr::render::view_transformations::position_world_to_clip;
import bevy_pbr::render::mesh_view_bindings::view;
import bevy_pbr::render::shadows::fetch_directional_shadow;
import bevy_pbr::render::mesh_view_bindings as view_bindings;
import bevy_pbr::render::mesh_view_types;
import and_remaster::gamematerial_shared::far_clipped;
import and_remaster::gamematerial_bindings::{p, base_tex, base_smp, normal_tex, normal_smp, tex2, tex2_smp, mask_tex, mask_smp};

// What the vertex stage hands on: Bevy's mesh outputs plus the fog, computed per vertex as the original's vertex
// shaders do (oFog = clamp((end - view depth) * 1 / (end - start), 0, 1), c1 = {start, end, 1 / (end - start), 0.5},
// Renderer_SetFog 0x00504010) and interpolated: on the landscape's large far triangles that leaves distant hills
// visible through the mist, where a per-pixel fog would wash them out.
struct GameOut {
    @builtin(position) position: vec4<f32>,
    @location(0) world_position: vec4<f32>,
    @location(1) world_normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) uv_b: vec2<f32>,
    @location(4) world_tangent: vec4<f32>,
    @location(5) fog: f32,
}

@vertex
fn vertex(vertex_in: Vertex) -> GameOut {
    var out: GameOut;
    let v = decompress_vertex(vertex_in, vertex_in.instance_index);
@if(SKINNED)
    var world_from_local = skinning::skin_model(v.joint_indices, v.joint_weights, vertex_in.instance_index);
@else
    var world_from_local = mesh_functions::get_world_from_local(vertex_in.instance_index);
@if(SKINNED)
    out.world_normal = skinning::skin_normals(world_from_local, v.normal);
@else
    out.world_normal = mesh_functions::mesh_normal_local_to_world(v.normal, vertex_in.instance_index);
    out.world_position = mesh_functions::mesh_position_local_to_world(world_from_local, vec4<f32>(v.position, 1.0));
    out.position = position_world_to_clip(out.world_position.xyz);
    out.uv = v.uv;
    out.uv_b = v.uv;
@if(VERTEX_UVS_B)
    out.uv_b = v.uv_b;
    out.world_tangent = vec4(0.0);
@if(VERTEX_TANGENTS)
    out.world_tangent = mesh_functions::mesh_tangent_local_to_world(world_from_local, v.tangent, vertex_in.instance_index);
    out.fog = 1.0;
    if (p.fog_range.y > p.fog_range.x) {
        let depth = -(view.view_from_world * vec4(out.world_position.xyz, 1.0)).z;
        out.fog = clamp((p.fog_range.y - depth) / (p.fog_range.y - p.fog_range.x), 0.0, 1.0);
    }
    return out;
}

// The original computes in gamma space (D3D9: texture bytes times light bytes, no sRGB). Bevy samples sRGB textures
// as linear and writes linear to an sRGB target, so samples go back to gamma here and only the result to linear.
fn to_gamma(c: vec3<f32>) -> vec3<f32> {
    return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}
fn to_linear(c: vec3<f32>) -> vec3<f32> {
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

@fragment
fn fragment(in: GameOut) -> @location(0) vec4<f32> {
    // the original's far plane clips (Bevy's projection is infinite): beyond it the sky shows
    let depth = -(view.view_from_world * vec4(in.world_position.xyz, 1.0)).z;
    if (far_clipped(depth, p.fog_range.z, p.remaster.y > 0.0, in.position.xy, view_bindings::globals.frame_count)) {
        discard;
    }
    // every map sampled with the view's mip bias (remaster aa_mip_bias under TAA, graphics/geometry; 0 otherwise),
    // as the old remaster biased every mipmapped sampler (vk_pipeline.cpp)
    let bias = view.mip_bias;
    let base_s = textureSampleBias(base_tex, base_smp, in.uv, bias);
    let base = vec4(to_gamma(base_s.rgb), base_s.a);
    let n_geo = normalize(in.world_normal);
    var n = n_geo;
    {
        if (p.has_normal != 0u && dot(in.world_tangent.xyz, in.world_tangent.xyz) > 0.0) {
            // the normal map in tangent space (_bx2: 2 * t - 1)
            var tn = textureSampleBias(normal_tex, normal_smp, in.uv, bias).xyz * 2.0 - 1.0;
            // remaster terrain_detail (graphics/geometry; the old remaster's vk_shader_translate.cpp, terrain pixel
            // shader 98a72838035bad82 only): the terrain's normal map sampled again at geometry.x times its tiling,
            // its slope added at geometry.y; mip filtering fades it with distance
            if (p.geometry.x > 1.0 && p.has_texture2 != 0u && p.has_mask != 0u) {
                let tdd = textureSampleBias(normal_tex, normal_smp, in.uv * p.geometry.x, bias).xyz * 2.0 - 1.0;
                tn = normalize(vec3(tn.xy + tdd.xy * p.geometry.y, tn.z));
            }
            let t = normalize(in.world_tangent.xyz - n_geo * dot(n_geo, in.world_tangent.xyz));
            let b = cross(n_geo, t) * in.world_tangent.w;
            n = normalize(t * tn.x + b * tn.y + n_geo * tn.z);
        }
    }
    let to_sun = normalize(p.sun_dir.xyz);
    // remaster: screen-space ambient occlusion (Bevy's SSAO, graphics.rs) on the ambient and fill light, the
    // light that comes from all around; the sun and emissive stay as they are, exempt surfaces (snow) keep all
    var ao = 1.0;
@if(SCREEN_SPACE_AMBIENT_OCCLUSION)
    {
        let ssao = textureLoad(view_bindings::screen_space_ambient_occlusion_texture, vec2<i32>(in.position.xy - view.viewport.xy), 0i).r;
        ao = mix(1.0, ssao, p.remaster.x);
    }
    let v0 = p.emissive.rgb + (p.ambient_light.rgb * p.ambient.rgb
        + p.fill_col.rgb * p.diffuse.rgb * clamp(dot(n_geo, normalize(p.fill_dir.xyz)), 0.0, 1.0)) * ao;
    let c1 = p.sun_col.rgb * p.diffuse.rgb;
    let light = clamp(clamp(dot(n, to_sun), 0.0, 1.0) * c1 + v0, vec3(0.0), vec3(1.0));
    var albedo = base.rgb;
    var alpha = base.a;
    let blend = p.has_texture2 != 0u && p.has_mask != 0u;
    let env = p.has_texture2 != 0u && p.has_mask == 0u;
    if (blend) {
        let uv1 = in.uv_b;
        let m = to_gamma(textureSampleBias(mask_tex, mask_smp, uv1, bias).rgb);
        let t2 = to_gamma(textureSampleBias(tex2, tex2_smp, in.uv, bias).rgb);
        albedo = clamp(base.rgb * (vec3(1.0) - m) + t2 * m, vec3(0.0), vec3(1.0));
        alpha = 1.0;
    }
    var col = albedo * light;
    if (env) {
        col = p.env.rgb * p.env.w + albedo * light;
    }
    let to_eye = normalize(view.world_position.xyz - in.world_position.xyz);
    if ((p.kind & 16u) != 0u && p.specular.w > 0.0) {
        // the bump + specular pixel shader (characters): the sun's highlight through the normal map
        let h = normalize(to_sun + to_eye);
        col = col + p.specular.rgb * p.sun_col.rgb * pow(clamp(dot(n, h), 0.0, 1.0), p.specular.w);
    } else if (p.has_normal == 0u && p.specular.w > 0.0 && dot(p.specular.rgb, vec3(1.0)) > 0.0) {
        // fixed function with D3DRS_SPECULARENABLE (0x00505320: renderer flags +0x48 bit 1, set exactly for the
        // materials with a specular colour, material log): each light's specular (both lights affect specular),
        // added after the texture stages, before fog (Blinn half vector, local viewer)
        let hs = normalize(to_sun + to_eye);
        let hf = normalize(normalize(p.fill_dir.xyz) + to_eye);
        let s_sun = select(0.0, pow(clamp(dot(n_geo, hs), 0.0, 1.0), p.specular.w), dot(n_geo, to_sun) > 0.0);
        let s_fill = select(0.0, pow(clamp(dot(n_geo, hf), 0.0, 1.0), p.specular.w), dot(n_geo, p.fill_dir.xyz) > 0.0);
        col = col + p.specular.rgb * (p.sun_col.rgb * s_sun + p.fill_col.rgb * s_fill);
    }
    // shadows (shadows.md: each caster's silhouette projected along the sun onto the world, drawn in the level's
    // shadow colour (mission shadow_color_red/green/blue/blend: (0, 0.125, 0.25) at 0.3), alpha-blended); here the
    // sun's shadow map. Characters receive none (the original projects onto world geometry only)
@if(!SKINNED)
    {
        let vz = (view.view_from_world * vec4(in.world_position.xyz, 1.0)).z;
        var lit = 1.0;
        for (var i = 0u; i < view_bindings::lights.n_directional_lights; i = i + 1u) {
            if ((view_bindings::lights.directional_lights[i].flags & mesh_view_types::DIRECTIONAL_LIGHT_FLAGS_SHADOWS_ENABLED_BIT) != 0u) {
                lit = min(lit, fetch_directional_shadow(i, in.world_position, normalize(in.world_normal), vz, in.position.xy));
            }
        }
        if (p.geometry.w > 0.0) {
            // remaster shadow_strength (graphics/geometry; the old remaster's vk_shadows.cpp apply pass): a shadow is
            // not black, what reaches it is the sky's light; a full shadow multiplies by (1 - strength) tinted blue,
            // the penumbra blends toward it (under the fog, as the old applied it)
            let sky = clamp((1.0 - p.geometry.w) * vec3(0.82, 0.92, 1.18), vec3(0.0), vec3(1.0));
            col = col * mix(vec3(1.0), sky, 1.0 - lit);
        } else {
            col = mix(col, vec3(0.0, 0.125, 0.25), 0.3 * (1.0 - lit));
        }
    }
    if (p.fog_range.y > p.fog_range.x) {
        col = mix(p.fog_col.rgb, col, in.fog);
    }
    // emissive.w: the camera fade (camfade.rs; 0 = opaque), objects between the camera and Roman
    // diffuse.w: the record's opacity (a translucent record, kind bit 0x10: the draw state's constant alpha that the
    // texture's alpha is multiplied by, fn_004897d0; 1 otherwise)
    // (remaster alpha_coverage, graphics/geometry: with MSAA the pipeline turns this alpha into the samples' coverage)
    return vec4(to_linear(col), alpha * p.diffuse.w * (1.0 - p.emissive.w));
}
