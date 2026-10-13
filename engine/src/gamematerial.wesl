// The original's shading (docs/remaster-engine.md "Rendering"; the three pixel shaders disassembled with
// tools/ps_disasm.py, the fixed-function stages from the material log, [test] log_materials):
//   lighting  v0 = emissive + ambient light * material ambient + fill light * material diffuse * N.L (per vertex in the
//             original: the vertex shader / D3D vertex lighting), c1 = sun * material diffuse,
//             light = sat(sat(Nmap . Lsun) * c1 + v0)
//   kind 3    bump diffuse (ps eac78aec9a00d1cf): base * light, the normal from the normal map
//   kind 15   terrain (ps 98a72838035bad82): sat(base * (1 - mask) + texture2 * mask) * light, mask on uv set 1
//   kind 5    environment (stages: cube * TFACTOR, then MULTIPLYADD texture * diffuse): env * factor + base * light
//   other     fixed function: base * light, plus specular * pow(N.H, power) with the sun
// then the level's linear fog (global_settings fog_near / fog_far / colour).
import bevy_pbr::render::forward_io::VertexOutput;
import bevy_pbr::render::mesh_view_bindings::view;

struct GameMaterialParams {
    kind: u32,
    has_normal: u32,
    has_texture2: u32,
    has_mask: u32,
    emissive: vec4<f32>,
    ambient: vec4<f32>,
    diffuse: vec4<f32>,
    // rgb specular colour, w power
    specular: vec4<f32>,
    // the environment cube's colour (approximation: the sky; the original renders a cube map) and its factor in w
    env: vec4<f32>,
    // toward the sun (engine space), w unused; sun colour * intensity
    sun_dir: vec4<f32>,
    sun_col: vec4<f32>,
    fill_dir: vec4<f32>,
    fill_col: vec4<f32>,
    ambient_light: vec4<f32>,
    // fog colour rgb; near, far in fog_range.xy (0 0: no fog)
    fog_col: vec4<f32>,
    fog_range: vec4<f32>,
}

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var base_tex: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var base_smp: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var normal_tex: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(3) var normal_smp: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(4) var tex2: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(5) var tex2_smp: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(6) var mask_tex: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(7) var mask_smp: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(8) var<uniform> p: GameMaterialParams;

// The original computes in gamma space (D3D9: texture bytes times light bytes, no sRGB). Bevy samples sRGB textures
// as linear and writes linear to an sRGB target, so samples go back to gamma here and only the result to linear.
fn to_gamma(c: vec3<f32>) -> vec3<f32> {
    return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}
fn to_linear(c: vec3<f32>) -> vec3<f32> {
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let base_s = textureSample(base_tex, base_smp, in.uv);
    let base = vec4(to_gamma(base_s.rgb), base_s.a);
    let n_geo = normalize(in.world_normal);
    var n = n_geo;
    @if(VERTEX_TANGENTS) {
        if (p.has_normal != 0u) {
            // the normal map in tangent space (_bx2: 2 * t - 1)
            let tn = textureSample(normal_tex, normal_smp, in.uv).xyz * 2.0 - 1.0;
            let t = normalize(in.world_tangent.xyz - n_geo * dot(n_geo, in.world_tangent.xyz));
            let b = cross(n_geo, t) * in.world_tangent.w;
            n = normalize(t * tn.x + b * tn.y + n_geo * tn.z);
        }
    }
    let to_sun = normalize(p.sun_dir.xyz);
    let v0 = p.emissive.rgb + p.ambient_light.rgb * p.ambient.rgb
        + p.fill_col.rgb * p.diffuse.rgb * clamp(dot(n_geo, normalize(p.fill_dir.xyz)), 0.0, 1.0);
    let c1 = p.sun_col.rgb * p.diffuse.rgb;
    let light = clamp(clamp(dot(n, to_sun), 0.0, 1.0) * c1 + v0, vec3(0.0), vec3(1.0));
    var albedo = base.rgb;
    var alpha = base.a;
    if (p.kind == 15u) {
        var uv1 = in.uv;
        @if(VERTEX_UVS_B) {
            uv1 = in.uv_b;
        }
        let m = to_gamma(textureSample(mask_tex, mask_smp, uv1).rgb);
        let t2 = to_gamma(textureSample(tex2, tex2_smp, in.uv).rgb);
        albedo = clamp(base.rgb * (vec3(1.0) - m) + t2 * m, vec3(0.0), vec3(1.0));
        alpha = 1.0;
    }
    var col = albedo * light;
    if (p.kind == 5u) {
        col = p.env.rgb * p.env.w + albedo * light;
    } else if (p.kind != 3u && p.kind != 15u && p.specular.w > 0.0) {
        let to_eye = normalize(view.world_position.xyz - in.world_position.xyz);
        let h = normalize(to_sun + to_eye);
        col = col + p.specular.rgb * p.sun_col.rgb * pow(clamp(dot(n, h), 0.0, 1.0), p.specular.w);
    }
    if (p.fog_range.y > p.fog_range.x) {
        let d = distance(view.world_position.xyz, in.world_position.xyz);
        let f = clamp((d - p.fog_range.x) / (p.fog_range.y - p.fog_range.x), 0.0, 1.0);
        col = mix(col, p.fog_col.rgb, f);
    }
    return vec4(to_linear(col), alpha);
}
