// The original's shading (docs/remaster-engine.md "Rendering"; the three pixel shaders disassembled with
// tools/ps_disasm.py, the fixed-function stages from the material log, [test] log_materials):
//   lighting  v0 = emissive + ambient light * material ambient + fill light * material diffuse * N.L (per vertex in the
//             original: the vertex shader / D3D vertex lighting), c1 = sun * material diffuse,
//             light = sat(sat(Nmap . Lsun) * c1 + v0)
// The kind byte (material +0x34) is a set of flags: 1 textured, 2 normal map, 4 second texture, 8 blend mask
// (16 and 32 not identified):
//   normal map           bump (ps eac78aec9a00d1cf): base * light, the normal from the normal map
//   texture2 + mask      terrain (ps 98a72838035bad82) / shore blend: sat(base * (1 - mask) + texture2 * mask) * light,
//                        (the terrain's mask on uv set 1)
// Each map is sampled with the uv set its record names (p.uv_sets: the record's byte +0x32).
//   texture2, no mask    environment (stages: cube * TFACTOR, then MULTIPLYADD texture * diffuse): env * factor +
//                        base * light (texture2 is the cube's slot; the game binds its own cube map there)
//   otherwise            fixed function: base * light (plus a specular highlight with flag 16: guess)
// then the level's linear fog (global_settings fog_near / fog_far / colour).
import bevy_pbr::render::forward_io::{Vertex, decompress_vertex};
import bevy_pbr::render::mesh_functions;
import bevy_pbr::render::mesh_bindings::mesh;
import bevy_pbr::render::mesh_types::MESH_FLAGS_SHADOW_RECEIVER_BIT;
import bevy_pbr::render::skinning;
import bevy_pbr::render::view_transformations::position_world_to_clip;
import bevy_pbr::render::mesh_view_bindings::view;
import bevy_pbr::render::shadows::fetch_directional_shadow;
import bevy_pbr::render::mesh_view_bindings as view_bindings;
import bevy_pbr::render::mesh_view_types;
import and_remaster::gamematerial_shared::far_clipped;
import and_remaster::graphics::height_fog::height_fog;
import and_remaster::graphics::ao_extras::remaster_occlusion;
import and_remaster::gamematerial_bindings::{p, base_tex, base_smp, normal_tex, normal_smp, tex2, tex2_smp, mask_tex, mask_smp, env_tex, env_smp};

// The level's environment cube along `d` (engine space), from its six faces in a 3 x 2 atlas (graphics/env_map.rs
// FACES: the face of the largest axis, the basis Bevy's looking_to gives its camera, a pi/2 view). Linear colour.
// Each face's samples stay half a texel inside it, so the filter never reads the neighbouring face.
fn env_lookup(d: vec3<f32>) -> vec3<f32> {
    let a = abs(d);
    var face = 0u;
    var f = vec3(1.0, 0.0, 0.0);
    var u = vec3(0.0, 1.0, 0.0);
    if (a.x >= a.y && a.x >= a.z) {
        face = select(1u, 0u, d.x > 0.0);
        f = vec3(sign(d.x), 0.0, 0.0);
    } else if (a.y >= a.z) {
        face = select(3u, 2u, d.y > 0.0);
        f = vec3(0.0, sign(d.y), 0.0);
        u = vec3(0.0, 0.0, sign(d.y));
    } else {
        face = select(5u, 4u, d.z > 0.0);
        f = vec3(0.0, 0.0, sign(d.z));
    }
    let right = normalize(cross(f, u));
    let up = cross(right, f);
    let z = max(dot(d, f), 1e-6);
    let inset = 0.5 / max(f32(textureDimensions(env_tex).y) * 0.5, 1.0);
    let in_face = clamp(vec2(0.5 + 0.5 * dot(d, right) / z, 0.5 - 0.5 * dot(d, up) / z), vec2(inset), vec2(1.0 - inset));
    let cell = vec2(f32(face % 3u), f32(face / 3u));
    return textureSampleLevel(env_tex, env_smp, (cell + in_face) / vec2(3.0, 2.0), 0.0).rgb;
}

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
    @location(6) uv_c: vec2<f32>,
    // 1 when the object receives the sun's shadows (NotShadowReceiver clears Bevy's flag: level.rs Shadowing)
    @location(7) @interpolate(flat) shadow_receiver: u32,
}

@vertex
// uv_c: the mesh's third uv set (gamematerial.rs specialize binds it at location 10; its first set when it has none)
fn vertex(vertex_in: Vertex, @location(10) uv_c: vec2<f32>) -> GameOut {
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
    out.uv_c = uv_c;
    out.shadow_receiver = select(0u, 1u, (mesh[vertex_in.instance_index].flags & MESH_FLAGS_SHADOW_RECEIVER_BIT) != 0u);
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
// Both with the sRGB curve itself (the exact inverse of the GPU's decode and encode), so a texture byte and a result
// byte are the original's: a 2.2 power instead (until 2026-10-24) crushed everything below ~0.05 to near black (the
// sRGB curve's linear toe), e.g. the village's dark wood under the eaves at 2 / 255 where the original shows 9-10.
fn to_gamma(c: vec3<f32>) -> vec3<f32> {
    let x = max(c, vec3(0.0));
    return select(1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, x * 12.92, x <= vec3(0.0031308));
}
fn to_linear(c: vec3<f32>) -> vec3<f32> {
    let x = max(c, vec3(0.0));
    return select(pow((x + 0.055) / 1.055, vec3(2.4)), x / 12.92, x <= vec3(0.04045));
}

// The texture coordinates of uv set `uv_set` (MaterialDef::uv_sets): the first, the second (uv_b) or the third (uv_c;
// a set the mesh lacks reads its first)
fn uv_of(in: GameOut, uv_set: u32) -> vec2<f32> {
    return select(select(in.uv, in.uv_b, uv_set == 1u), in.uv_c, uv_set >= 2u);
}

// The unit vertex normal, or zero for a mesh authored without normals (lev_4_m3's reeds: every normal (0, 0, 0)).
// The original's lighting vertex shader takes N . L of the normal as stored (Renderer_UpdateLightConstants 0x004ff2e0
// loads the light directions and colours as constants, c3 = ambient light * material ambient + emissive), so a zero
// normal gets no sun, no fill and no specular, only the emissive + ambient term. normalize(0) is NaN on the GPU and
// turned such surfaces black.
fn unit_or_zero(v: vec3<f32>) -> vec3<f32> {
    let len2 = dot(v, v);
    return select(vec3(0.0), v * inverseSqrt(len2), len2 > 1e-12);
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
    let base_s = textureSampleBias(base_tex, base_smp, uv_of(in, p.uv_sets.x), bias);
    let base = vec4(to_gamma(base_s.rgb), base_s.a);
    let n_geo = unit_or_zero(in.world_normal);
    var n = n_geo;
    {
        if (p.has_normal != 0u && dot(in.world_tangent.xyz, in.world_tangent.xyz) > 0.0 && dot(n_geo, n_geo) > 0.0) {
            // the normal map in tangent space (_bx2: 2 * t - 1), on its own uv set (the tangents follow that set:
            // scene.rs add_tangents)
            let uv_n = uv_of(in, p.uv_sets.y);
            var tn = textureSampleBias(normal_tex, normal_smp, uv_n, bias).xyz * 2.0 - 1.0;
            // remaster terrain_detail (graphics/geometry; the old remaster's vk_shader_translate.cpp, terrain pixel
            // shader 98a72838035bad82 only): the terrain's normal map sampled again at geometry.x times its tiling,
            // its slope added at geometry.y; mip filtering fades it with distance
            if (p.geometry.x > 1.0 && p.has_texture2 != 0u && p.has_mask != 0u) {
                let tdd = textureSampleBias(normal_tex, normal_smp, uv_n * p.geometry.x, bias).xyz * 2.0 - 1.0;
                tn = normalize(vec3(tn.xy + tdd.xy * p.geometry.y, tn.z));
            }
            let t = normalize(in.world_tangent.xyz - n_geo * dot(n_geo, in.world_tangent.xyz));
            let b = cross(n_geo, t) * in.world_tangent.w;
            n = normalize(t * tn.x + b * tn.y + n_geo * tn.z);
        }
    }
    let to_sun = normalize(p.sun_dir.xyz);
    var albedo = base.rgb;
    var alpha = base.a;
    let blend = p.has_texture2 != 0u && p.has_mask != 0u;
    let env = p.has_texture2 != 0u && p.has_mask == 0u;
    if (blend) {
        let m = to_gamma(textureSampleBias(mask_tex, mask_smp, uv_of(in, p.uv_sets.w), bias).rgb);
        let t2 = to_gamma(textureSampleBias(tex2, tex2_smp, uv_of(in, p.uv_sets.z), bias).rgb);
        albedo = clamp(base.rgb * (vec3(1.0) - m) + t2 * m, vec3(0.0), vec3(1.0));
        alpha = 1.0;
    }
    // remaster: screen-space ambient occlusion (Bevy's SSAO, graphics.rs) on the ambient and fill light, the
    // light that comes from all around, shaped as the old remaster did (graphics/ao_extras.wesl): the sun and
    // emissive stay as they are except under the contact occlusion; snow keeps only ao_snow of it
    var ao = 1.0;
    var ao_whole = 1.0;
@if(SCREEN_SPACE_AMBIENT_OCCLUSION)
    {
        let ssao = textureLoad(view_bindings::screen_space_ambient_occlusion_texture, vec2<i32>(in.position.xy - view.viewport.xy), 0i).r;
        let occ = remaster_occlusion(ssao, dot(albedo, vec3(0.2126, 0.7152, 0.0722)), p.remaster.x, p.ao_extras);
        ao = occ.ambient;
        ao_whole = occ.whole;
    }
    let v0_open = p.emissive.rgb + p.ambient_light.rgb * p.ambient.rgb
        + p.fill_col.rgb * p.diffuse.rgb * clamp(dot(n_geo, normalize(p.fill_dir.xyz)), 0.0, 1.0);
    let v0 = mix(p.emissive.rgb, v0_open, ao);
    let c1 = p.sun_col.rgb * p.diffuse.rgb;
    let sun = clamp(dot(n, to_sun), 0.0, 1.0) * c1;
    let light = min(clamp(sun + v0, vec3(0.0), vec3(1.0)), clamp(sun + v0_open, vec3(0.0), vec3(1.0)) * ao_whole);
    var col = albedo * light;
    let to_eye = normalize(view.world_position.xyz - in.world_position.xyz);
    if (env) {
        // stage 0 cube * TFACTOR, stage 1 MULTIPLYADD current + texture * diffuse (graphics/env_map.rs); the stages'
        // results saturate. The cube along the reflected view: per pixel here (the original's fixed function made the
        // reflection vector per vertex); without the level's cube, the fog colour
        var reflected = p.env.rgb;
        if (p.env_map.x > 0.5) {
            reflected = to_gamma(env_lookup(reflect(-to_eye, n)));
        }
        col = min(reflected * p.env.w + albedo * light, vec3(1.0));
    }
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
    // sun's shadow map. Characters receive none (the original projects onto world geometry only), nor do objects
    // whose set clears shadow_receive (the river surfaces, grass: object +0x3ec bit 0x10, 0x00451360)
@if(!SKINNED)
    if (in.shadow_receiver != 0u) {
        let vz = (view.view_from_world * vec4(in.world_position.xyz, 1.0)).z;
        var lit = 1.0;
        for (var i = 0u; i < view_bindings::lights.n_directional_lights; i = i + 1u) {
            if ((view_bindings::lights.directional_lights[i].flags & mesh_view_types::DIRECTIONAL_LIGHT_FLAGS_SHADOWS_ENABLED_BIT) != 0u) {
                lit = min(lit, fetch_directional_shadow(i, in.world_position, n_geo, vz, in.position.xy));
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
        if (p.height_fog.x > 0.0) {
            // remaster: the old remaster's height fog per pixel (graphics/height_fog.wesl) in place of the linear fog
            let fog = height_fog(in.world_position.xyz - view.world_position.xyz, view.world_position.y, p.fog_col.rgb, p.fog_range.xy, p.height_fog, to_sun);
            col = mix(fog.rgb, col, fog.a);
        } else {
            col = mix(p.fog_col.rgb, col, in.fog);
        }
    }
    // emissive.w: the camera fade (camfade.rs; 0 = opaque), objects between the camera and Roman
    // diffuse.w: the record's opacity (a translucent record, kind bit 0x10: the draw state's constant alpha that the
    // texture's alpha is multiplied by, fn_004897d0; 1 otherwise)
    // (remaster alpha_coverage, graphics/geometry: with MSAA the pipeline turns this alpha into the samples' coverage)
    let a = alpha * p.diffuse.w * (1.0 - p.emissive.w);
    if (p.blend.x != 0u) {
        // added (SRCALPHA / ONE): the gamma-space colour times alpha, summed in the transmissive pass and composited in
        // gamma space as the original adds to its back buffer (fxvm/additive.rs)
        return vec4(col * a, 0.0);
    }
    return vec4(to_linear(col), a);
}
