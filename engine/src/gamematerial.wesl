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
import and_remaster::gamematerial_bindings::{p, base_tex, base_smp, normal_tex, normal_smp, tex2, tex2_smp, mask_tex, mask_smp, env_tex, env_smp, detail_tex, detail_smp};

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
    // the fixed-function specular (fixed_specular), lit per vertex as the original lights it
    @location(8) specular: vec3<f32>,
}

// The fixed-function specular of a material without a normal map (D3DRS_SPECULARENABLE: 0x00505320 sets renderer
// flags +0x48 bit 1 exactly for the materials with a specular colour, material log), as the original computes it: per
// vertex, D3D9's lighting for the static meshes and the same sum in the skinned meshes' vertex shaders (shaders.dat
// vs_0002cde5b3229591: per light `lit` of N.L and N.H with the power c6, times c13/c14 = the material's specular x
// the light's), Blinn's half vector with a local viewer, only where N.L > 0; both directional lights affect specular
// (LightObject_Load 0x0044add0: AffectSpecular), interpolated across the triangle and added after the texture stages.
// Per vertex is what makes the jump pack's and Q's chrome read as chrome: a coarse dome's vertex highlights spread
// over its large triangles (a per-pixel highlight was a small dot on a dark rust dome, lev_3_m1).
fn fixed_specular(n: vec3<f32>, world_position: vec3<f32>) -> vec3<f32> {
    if (p.has_normal != 0u || (p.kind & 16u) != 0u || p.specular.w <= 0.0 || dot(p.specular.rgb, vec3(1.0)) <= 0.0 || dot(n, n) <= 0.0) {
        return vec3(0.0);
    }
    let to_eye = normalize(view.world_position.xyz - world_position);
    let to_sun = normalize(p.sun_dir.xyz);
    let to_fill = normalize(p.fill_dir.xyz);
    let s_sun = select(0.0, pow(clamp(dot(n, normalize(to_sun + to_eye)), 0.0, 1.0), p.specular.w), dot(n, to_sun) > 0.0);
    let s_fill = select(0.0, pow(clamp(dot(n, normalize(to_fill + to_eye)), 0.0, 1.0), p.specular.w), dot(n, to_fill) > 0.0);
    // a vertex colour output saturates (oD1)
    return clamp(p.specular.rgb * (p.sun_col.rgb * s_sun + p.fill_col.rgb * s_fill), vec3(0.0), vec3(1.0));
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
    out.specular = fixed_specular(unit_or_zero(out.world_normal), out.world_position.xyz);
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
/// Fine grain for a surface seen close up, to add to its colour.
///
/// Close up, a surface has more screen pixels than texels, so it looks soft. The texture's own fine grain, repeated at
/// four times the tiling, fills that gap. It is only the grain (a fine sample minus a blurrier one), so colours stay
/// the same, and it fades out as the surface stops being magnified, so distant views are unchanged.
fn fine_grain(t: texture_2d<f32>, s: sampler, uv: vec2<f32>, bias: f32) -> vec3<f32> {
    let duv = uv * vec2<f32>(textureDimensions(t));
    let texels_per_pixel = max(length(dpdx(duv)), length(dpdy(duv)));
    let magnified = 1.0 - smoothstep(0.3, 1.0, texels_per_pixel);
    let fine = textureSampleBias(t, s, uv * 4.0, bias).rgb;
    let soft = textureSampleBias(t, s, uv * 4.0, bias + 3.0).rgb;
    return (to_gamma(fine) - to_gamma(soft)) * 0.45 * magnified;
}

/// The ground detail tile's size in world units (a grunt is about 10 tall). graphics/ground_detail.rs makes the tiles.
const DETAIL_TILE: f32 = 4.0;

/// Close-up detail for a landscape surface: xyz bends its normal (world space), w multiplies its colour.
///
/// The detail is projected from the three world axes and blended by how much the surface faces each one, so it keeps
/// the same size on any slope whatever the surface's own texture coordinates do. Each projection mixes the tile with a
/// larger, turned copy, so the repeats are harder to spot. It fades out once a tile texel gets smaller than a few
/// screen pixels, where it would only shimmer.
fn ground_detail(layer: f32, pos: vec3<f32>, n_geo: vec3<f32>) -> vec4<f32> {
    let index = i32(layer) - 1;
    let footprint = length(fwidth(pos));
    let texel = DETAIL_TILE / 512.0;
    let fade = 1.0 - smoothstep(texel * 2.0, texel * 24.0, footprint);
    var w = pow(abs(n_geo), vec3(4.0));
    w = w / max(w.x + w.y + w.z, 1e-5);
    let turn = mat2x2<f32>(0.54, 0.84, -0.84, 0.54);
    let planes = array<vec2<f32>, 3>(pos.yz, pos.xz, pos.xy);
    // screen-space rates of change of the position, taken here because the loop below skips planes per pixel
    let dx = dpdx(pos) / DETAIL_TILE;
    let dy = dpdy(pos) / DETAIL_TILE;
    let dx_planes = array<vec2<f32>, 3>(dx.yz, dx.xz, dx.xy);
    let dy_planes = array<vec2<f32>, 3>(dy.yz, dy.xz, dy.xy);
    var bumps = vec3(0.0);
    var brightness = 0.0;
    for (var k = 0; k < 3; k++) {
        // a floor or wall faces mostly one axis, so most pixels need one plane, not three
        if (w[k] < 0.02 || fade <= 0.0) { continue; }
        let uv = planes[k] / DETAIL_TILE;
        let a = textureSampleGrad(detail_tex, detail_smp, uv, index, dx_planes[k], dy_planes[k]);
        let b = textureSampleGrad(detail_tex, detail_smp, turn * uv * 0.38 + vec2(0.31, 0.67), index,
            turn * dx_planes[k] * 0.38, turn * dy_planes[k] * 0.38);
        let t = a * 0.65 + b * 0.35;
        let slope = (t.xy * 2.0 - 1.0) * w[k];
        // the slope lies in the projection's plane: x and y of the plane's two world axes
        if (k == 0) { bumps += vec3(0.0, slope.x, slope.y); }
        else if (k == 1) { bumps += vec3(slope.x, 0.0, slope.y); }
        else { bumps += vec3(slope.x, slope.y, 0.0); }
        brightness += (t.z - 0.5) * w[k];
    }
    // only the part of the bend that lies along the surface tilts it
    bumps = (bumps - n_geo * dot(bumps, n_geo)) * fade;
    return vec4(bumps, 1.0 + brightness * 2.0 * p.detail.z * fade);
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

// The Remaster's snow surface. The original lights its snow with a 512 px bump map, which reads as big soft blobs at
// today's resolutions. This replaces the map with light behaviour worked out per pixel from the world position:
//
// - Base: clean snow replaces the texture's painted cracks, which don't respond to light. The texture still decides
//   where dark ground shows through.
// - Drifts and crust: wind-shaped ridges and fine relief, so no pattern repeats across a floor.
// - Scattering: part of the light bounces off the surface and shows every detail of the crust. The rest travels inside
//   the snow first, so it only follows the broad drifts, and ice absorbs red on the way, so it comes back blue. That
//   keeps shaded sides bright and blue.
// - Glitter: tiny ice crystals, about a pixel wide, flash when they face the point halfway between the sun and the eye.
//   They flash as the camera moves, because that point moves with it.
//
// Only the relief's slope changes the light, so a flat floor keeps the original's overall brightness.
fn snow_random(cell: vec3<i32>, salt: u32) -> f32 {
    var h = (bitcast<u32>(cell.x) * 73856093u) ^ (bitcast<u32>(cell.y) * 19349663u) ^ (bitcast<u32>(cell.z) * 2654435761u) ^ (salt * 83492791u);
    h = (h ^ (h >> 15u)) * 2246822519u;
    h = (h ^ (h >> 13u)) * 3266489917u;
    return f32(h ^ (h >> 16u)) / 4294967295.0;
}

// Smooth noise over 3D space, returned as (value, gradient). It's worked out from a point's position rather than
// projected onto the surface, so it can't stretch on a slope. The gradient comes from the same formula, which saves
// looking the noise up three more times.
fn snow_noise(q: vec3<f32>) -> vec4<f32> {
    let c = vec3<i32>(floor(q));
    let f = fract(q);
    let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    let du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    let a = snow_random(c, 1u);
    let b = snow_random(c + vec3(1, 0, 0), 1u) - a;
    let d = snow_random(c + vec3(0, 1, 0), 1u) - a;
    let e = snow_random(c + vec3(0, 0, 1), 1u) - a;
    let g = snow_random(c + vec3(1, 1, 0), 1u) - a - b - d;
    let h = snow_random(c + vec3(1, 0, 1), 1u) - a - b - e;
    let i = snow_random(c + vec3(0, 1, 1), 1u) - a - d - e;
    let j = snow_random(c + vec3(1, 1, 1), 1u) - a - b - d - e - g - h - i;
    return vec4(
        a + b * u.x + d * u.y + e * u.z + g * u.x * u.y + h * u.x * u.z + i * u.y * u.z + j * u.x * u.y * u.z,
        du * vec3(
            b + g * u.y + h * u.z + j * u.y * u.z,
            d + g * u.x + i * u.z + j * u.x * u.z,
            e + h * u.x + i * u.y + j * u.x * u.y));
}

// The crust's slope at one size. Sizes much smaller than a pixel fade out, or they would shimmer.
fn snow_crust(p: vec3<f32>, size: f32, height: f32, pixel: f32) -> vec3<f32> {
    return snow_noise(p / size).yzw * height * (1.0 - smoothstep(0.25, 0.8, pixel / size));
}

// The slope of one band of wind-shaped snow, with a rounded crest. A plain abs() fold would leave a hard line along
// the top. `bend` shifts the band sideways, and `bend_slope` is how that shift changes across space.
fn snow_ridge(along: f32, across: f32, height: f32, length: f32, width: f32, wind: vec3<f32>, side: vec3<f32>, bend_slope: vec3<f32>) -> vec3<f32> {
    let noise = snow_noise(vec3(along / length, across / width, height / width));
    let centred = 2.0 * noise.x - 1.0;
    let crest = -2.0 * centred / sqrt(centred * centred + 0.04);
    return crest * (noise.y * wind / length + noise.z * (side + bend_slope) / width + noise.w * vec3(0.0, 1.0, 0.0) / width);
}

// The slope of the drifts. Noise stretched along the wind gives long shapes. Several sizes, each bent by its own slow
// noise, stop any one band from reading as a track.
fn snow_drift(p: vec3<f32>) -> vec3<f32> {
    let wind = vec3(0.8, 0.0, 0.6);
    let side = vec3(-0.6, 0.0, 0.8);
    let along = dot(p, wind);
    let across = dot(p, side);
    let slow = snow_noise(p / 45.0);
    let quick = snow_noise(p / 13.0 + vec3(7.0, 3.0, 5.0));
    let bend = slow.x * 22.0 + quick.x * 5.0;
    let bend_slope = slow.yzw * 22.0 / 45.0 + quick.yzw * 5.0 / 13.0;
    return snow_ridge(along, across + bend, p.y, 110.0, 55.0, wind, side, bend_slope) * 3.5
        + snow_ridge(along + 550.0, across - bend, p.y, 50.0, 19.0, wind, side, -bend_slope) * 1.3
        + snow_ridge(along + 506.0, across + bend * 0.5, p.y, 22.0, 9.0, wind, side, bend_slope * 0.5) * 0.12;
}

// One layer of crystals, following Wang and Bowles: a seed point in each cell of a 3D grid, shown where the surface
// passes by. Each has its own random tilt, and most never line up with the light.
//
// A crystal stays fixed in the world, but its size is measured in screen pixels using the two screen-to-world steps.
// Otherwise it would stretch into an oval at low camera angles, or be small enough for TAA to average it away.
fn snow_crystals(p: vec3<f32>, cell: f32, n: vec3<f32>, h: vec3<f32>, step_x: vec3<f32>, step_y: vec3<f32>, salt: u32) -> vec3<f32> {
    let g = p / cell;
    let c = vec3<i32>(floor(g));
    let tilt = vec3(snow_random(c, salt + 2u), snow_random(c, salt + 3u), snow_random(c, salt + 8u)) * 2.0 - 1.0;
    let facet = normalize(n + tilt * 1.1);
    let facing = smoothstep(0.94, 0.975, dot(facet, h));
    let centre = vec3(snow_random(c, salt + 4u), snow_random(c, salt + 5u), snow_random(c, salt + 9u)) * 0.5 + 0.25;
    let d = (fract(g) - centre) * cell;
    let xx = dot(step_x, step_x);
    let xy = dot(step_x, step_y);
    let yy = dot(step_y, step_y);
    let bx = dot(d, step_x);
    let by = dot(d, step_y);
    let in_pixels = vec2(yy * bx - xy * by, xx * by - xy * bx) / max(xx * yy - xy * xy, 1e-8);
    let dot_shape = 1.0 - smoothstep(0.25, 0.9, length(in_pixels));
    // Raised to a high power so that most crystals are a faint grain and only a few are bright.
    let strength = pow(snow_random(c, salt + 6u), 8.0);
    // Ice splits light like a prism, so each crystal gets a slight tint. That also sets glints apart from white flakes.
    let tint = vec3(1.0) + 0.3 * cos(6.283 * (snow_random(c, salt + 7u) + vec3(0.0, 0.33, 0.67)));
    return tint * facing * dot_shape * (0.1 + 0.9 * strength);
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
    var base = vec4(to_gamma(base_s.rgb), base_s.a);
    // Remaster: grain on surfaces seen close up (fine_grain). Floors that blend two textures get it on both, below.
    // `terrain_detail` 0 turns it off.
    if (p.geometry.x > 1.0) {
        base = vec4(clamp(base.rgb + fine_grain(base_tex, base_smp, uv_of(in, p.uv_sets.x), bias), vec3(0.0), vec3(1.0)), base.a);
    }
    let n_geo = unit_or_zero(in.world_normal);
    var n = n_geo;
    if (p.remaster.w < 0.5) {
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
    let snow_surface = p.remaster.w > 0.5 && dot(n_geo, n_geo) > 0.0;
    let snow_position = in.world_position.xyz;
    var n_broad = n_geo;
    var snow_cover = 0.0;
    if (snow_surface) {
        // A very blurred sample keeps where the ground is dark and drops the painted cracks. Dark ground stays as it is.
        // These floors can blend two textures with a mask, so the blurred sample has to blend them as well.
        var broad_colour = to_gamma(textureSampleBias(base_tex, base_smp, uv_of(in, p.uv_sets.x), bias + 5.0).rgb);
        if (p.has_texture2 != 0u && p.has_mask != 0u) {
            let broad_mask = to_gamma(textureSampleBias(mask_tex, mask_smp, uv_of(in, p.uv_sets.w), bias + 5.0).rgb);
            let broad_second = to_gamma(textureSampleBias(tex2, tex2_smp, uv_of(in, p.uv_sets.z), bias + 5.0).rgb);
            broad_colour = broad_colour * (vec3(1.0) - broad_mask) + broad_second * broad_mask;
        }
        snow_cover = smoothstep(0.3, 0.55, dot(broad_colour, vec3(0.2126, 0.7152, 0.0722)));
        let pixel = max(length(dpdx(snow_position)), length(dpdy(snow_position)));
        let drift = snow_drift(snow_position);
        let crust = snow_crust(snow_position, 2.0, 0.1, pixel) + snow_crust(snow_position, 0.5, 0.14, pixel)
            + snow_crust(snow_position, 0.15, 0.08, pixel);
        // Mikkelsen's surface gradient: only the part of a slope that lies along the surface can bend its normal.
        let along_surface = (drift + crust) - n_geo * dot(n_geo, drift + crust);
        let broad_along_surface = drift - n_geo * dot(n_geo, drift);
        n = normalize(n_geo - along_surface * snow_cover);
        n_broad = normalize(n_geo - broad_along_surface * snow_cover);
    }
    let blend = p.has_texture2 != 0u && p.has_mask != 0u;
    // Remaster ground detail (ground_detail below): brightness factors for the base and second texture, and bumps.
    // A floor that blends two textures takes each one's bumps where its texture shows.
    var detail_brightness = vec2(1.0);
    if (p.detail.x > 0.0 && (p.detail.z > 0.0 || p.detail.w > 0.0)) {
        let first = ground_detail(p.detail.x, in.world_position.xyz, n_geo);
        var bumps = first.xyz;
        detail_brightness = vec2(first.w);
        if (blend && p.detail.y != p.detail.x) {
            let second = ground_detail(p.detail.y, in.world_position.xyz, n_geo);
            let m = dot(to_gamma(textureSampleBias(mask_tex, mask_smp, uv_of(in, p.uv_sets.w), bias).rgb), vec3(1.0 / 3.0));
            bumps = mix(bumps, second.xyz, m);
            detail_brightness.y = second.w;
        }
        n = normalize(n + bumps * p.detail.w);
    }
    var albedo = clamp(base.rgb * detail_brightness.x, vec3(0.0), vec3(1.0));
    var alpha = base.a;
    let env = p.has_texture2 != 0u && p.has_mask == 0u;
    if (blend) {
        let m = to_gamma(textureSampleBias(mask_tex, mask_smp, uv_of(in, p.uv_sets.w), bias).rgb);
        var t2 = to_gamma(textureSampleBias(tex2, tex2_smp, uv_of(in, p.uv_sets.z), bias).rgb);
        if (p.geometry.x > 1.0) {
            t2 = clamp(t2 + fine_grain(tex2, tex2_smp, uv_of(in, p.uv_sets.z), bias), vec3(0.0), vec3(1.0));
        }
        albedo = clamp(base.rgb * detail_brightness.x * (vec3(1.0) - m) + t2 * detail_brightness.y * m, vec3(0.0), vec3(1.0));
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
    var sun = clamp(dot(n, to_sun), 0.0, 1.0) * c1;
    var snow_shade = vec3(1.0);
    if (snow_surface) {
        // Each part of the light is divided by a flat floor's value, so the broad brightness stays the same.
        let flat_light = max(dot(n_geo, to_sun), 0.05);
        let surface = clamp(dot(n, to_sun) / flat_light, 0.0, 1.25);
        let wrap = 0.6;
        let inside = clamp((dot(n_broad, to_sun) + wrap) / (flat_light + wrap), 0.0, 1.15);
        // The original's light clips at full brightness, and these floors are past that in sun, so relief added to
        // the sun term would be cut off. It multiplies the clipped light instead.
        sun = clamp(dot(n_geo, to_sun), 0.0, 1.0) * c1;
        // Red fades fastest as the light inside weakens, so flat snow stays neutral and only shaded sides turn blue.
        snow_shade = surface * 0.5 + pow(vec3(inside), vec3(2.4, 1.3, 0.6)) * 0.5;
    }
    let light = min(clamp(sun + v0, vec3(0.0), vec3(1.0)), clamp(sun + v0_open, vec3(0.0), vec3(1.0)) * ao_whole);
    if (snow_surface) {
        // Clean snow is slightly blue rather than pure white. Only a trace of the painted detail stays.
        albedo = mix(albedo, vec3(0.84, 0.88, 0.93), snow_cover * 0.92);
    }
    var col = albedo * light * snow_shade;
    let to_eye = normalize(view.world_position.xyz - in.world_position.xyz);
    var sparkle = vec3(0.0);
    if (snow_surface) {
        // Cell size follows the pixel size in steps, so each cell has room for its crystal. Two neighbouring steps are
        // blended so that crystals fade in and out with distance instead of popping. They fade out far away, where
        // whole fields of them would only look like grain.
        let step_x = dpdx(snow_position);
        let step_y = dpdy(snow_position);
        let pixel = max(length(step_x), length(step_y));
        let level = max(log2(pixel * 2.2 / 0.06), 0.0);
        let cell = 0.06 * pow(2.0, floor(level));
        let coarser = fract(level);
        let h = normalize(to_sun + to_eye);
        let white = smoothstep(0.35, 0.6, min(albedo.r, min(albedo.g, albedo.b)));
        sparkle = (mix(snow_crystals(snow_position, cell, n, h, step_x, step_y, 10u),
                       snow_crystals(snow_position, cell * 2.0, n, h, step_x, step_y, 20u), coarser)
            + mix(snow_crystals(snow_position + 0.37 * cell, cell * 1.3, n, h, step_x, step_y, 30u),
                  snow_crystals(snow_position + 0.37 * cell, cell * 2.6, n, h, step_x, step_y, 40u), coarser))
            * white * (1.0 - smoothstep(100.0, 280.0, depth));
        // Snow reflects more of the sky at low viewing angles, which gives it a soft sheen.
        let grazing = pow(1.0 - clamp(dot(n_broad, to_eye), 0.0, 1.0), 5.0);
        col = col + vec3(0.75, 0.85, 1.0) * grazing * 0.15;
    }
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
    } else {
        // fixed function with D3DRS_SPECULARENABLE: the per-vertex specular (fixed_specular; zero for the other
        // materials), added after the texture stages, before fog
        col = col + in.specular;
    }
    // shadows (shadows.md: each caster's silhouette projected along the sun onto the world, drawn in the level's
    // shadow colour (mission shadow_color_red/green/blue/blend: (0, 0.125, 0.25) at 0.3), alpha-blended); here the
    // sun's shadow map. Characters receive none (the original projects onto world geometry only), nor do objects
    // whose set clears shadow_receive (the river surfaces, grass: object +0x3ec bit 0x10, 0x00451360)
    // crystals flash only in direct sun
    var sun_visible = 1.0;
@if(!SKINNED)
    if (in.shadow_receiver != 0u) {
        let vz = (view.view_from_world * vec4(in.world_position.xyz, 1.0)).z;
        var lit = 1.0;
        for (var i = 0u; i < view_bindings::lights.n_directional_lights; i = i + 1u) {
            if ((view_bindings::lights.directional_lights[i].flags & mesh_view_types::DIRECTIONAL_LIGHT_FLAGS_SHADOWS_ENABLED_BIT) != 0u) {
                lit = min(lit, fetch_directional_shadow(i, in.world_position, n_geo, vz, in.position.xy));
            }
        }
        sun_visible = lit;
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
    col = col + sparkle * sun_visible * 3.0;
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
