//! The old remaster's ambient occlusion in Bevy's SSAO pass: XeGTAO's main pass (Intel, MIT;
//! third_party/XeGTAO/XeGTAO.hlsli `XeGTAO_MainPass`, unmodified there) with the old remaster's settings (vk_device.cpp
//! `Ambient`: GTAOSettings Radius = ao_radius, FinalValuePower = ao_power, FalloffRange = ao_falloff,
//! ThinOccluderCompensation 0.7 ([render] ao_thin), and vk_gtao.cpp's thickness limit ([render] ao_thickness)).
//!
//! graphics/ao_extras.rs puts this source in place of Bevy 0.20's ssao.wesl (bevy_pbr/src/ssao, MIT / Apache-2.0),
//! with the settings' values in the constants below; everything around the main pass is Bevy's and kept from its
//! file: the bindings, the noise, the edges its spatial denoiser reads, the view-space helpers. Bevy's own estimator
//! (visibility bitmasks) leaves flat ground about 17 % occluded, so the old remaster's power of 3 darkened all the
//! snow; XeGTAO's horizon integral gives flat ground 1, as the old renderer had. When Bevy is upgraded, take its new
//! ssao.wesl's bindings and helpers and keep `xegtao_main` and `ssao` below.

import bevy_render::{
    view::View,
    globals::Globals,
    maths::{PI, HALF_PI},
};

@group(0) @binding(0) var preprocessed_depth: texture_2d<f32>;
@group(0) @binding(1) var normals: texture_2d<f32>;
@group(0) @binding(2) var hilbert_index_lut: texture_2d<u32>;
@if(USE_R16FLOAT)
@group(0) @binding(3) var ambient_occlusion: texture_storage_2d<r16float, write>;
@else
@group(0) @binding(3) var ambient_occlusion: texture_storage_2d<r32float, write>;
@group(0) @binding(4) var depth_differences: texture_storage_2d<r32uint, write>;
@group(0) @binding(5) var<uniform> globals: Globals;
struct SsaoSettings {
    // XeGTAO's Radius (world units; the effect radius is this times RADIUS_MULTIPLIER)
    radius: f32,
    // the old remaster's thickness limit (world units; 0 = off)
    thickness: f32,
}
@group(0) @binding(6) var<uniform> settings: SsaoSettings;
@group(1) @binding(0) var point_clamp_sampler: sampler;
@group(1) @binding(1) var linear_clamp_sampler: sampler;
@group(1) @binding(2) var<uniform> view: View;

// XeGTAO's constants: the settings' (written by graphics/ao_extras.rs) and its defaults (XeGTAO.h)
const FALLOFF_RANGE: f32 = 0.5;
const FINAL_VALUE_POWER: f32 = 3.0;
const THIN_OCCLUDER_COMPENSATION: f32 = 0.7;
const RADIUS_MULTIPLIER: f32 = 1.457;
const SAMPLE_DISTRIBUTION_POWER: f32 = 2.0;
const DEPTH_MIP_SAMPLING_OFFSET: f32 = 3.30;
const DEPTH_MIP_LEVELS: f32 = 4.0;

fn load_noise(pixel_coordinates: vec2<i32>) -> vec2<f32> {
    var index = textureLoad(hilbert_index_lut, pixel_coordinates % 64, 0).r;

@if(TEMPORAL_JITTER)
    index += 288u * (globals.frame_count % 64u);

    // R2 sequence - http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences
    return fract(0.5 + f32(index) * vec2<f32>(0.75487766624669276005, 0.5698402909980532659114));
}

/// Calculate differences in depth between neighbor pixels (later used by the spatial denoiser pass to preserve object edges)
fn calculate_neighboring_depth_differences(pixel_coordinates: vec2<i32>) -> f32 {
    // Sample the pixel's depth and 4 depths around it
    let uv = vec2<f32>(pixel_coordinates) / view.viewport.zw;
    let depths_upper_left = textureGather(0, preprocessed_depth, point_clamp_sampler, uv);
    let depths_bottom_right = textureGather(0, preprocessed_depth, point_clamp_sampler, uv, vec2<i32>(1i, 1i));
    let depth_center = depths_upper_left.y;
    let depth_left = depths_upper_left.x;
    let depth_top = depths_upper_left.z;
    let depth_bottom = depths_bottom_right.x;
    let depth_right = depths_bottom_right.z;

    // Calculate the depth differences (large differences represent object edges)
    var edge_info = vec4<f32>(depth_left, depth_right, depth_top, depth_bottom) - depth_center;
    let slope_left_right = (edge_info.y - edge_info.x) * 0.5;
    let slope_top_bottom = (edge_info.w - edge_info.z) * 0.5;
    let edge_info_slope_adjusted = edge_info + vec4<f32>(slope_left_right, -slope_left_right, slope_top_bottom, -slope_top_bottom);
    edge_info = min(abs(edge_info), abs(edge_info_slope_adjusted));
    let bias = 0.25; // Using the bias and then saturating nudges the values a bit
    let scale = depth_center * 0.011; // Weight the edges by their distance from the camera
    edge_info = saturate((1.0 + bias) - edge_info / scale); // Apply the bias and scale, and invert edge_info so that small values become large, and vice versa

    // Pack the edge info into the texture
    let edge_info_packed = vec4<u32>(pack4x8unorm(edge_info), 0u, 0u, 0u);
    textureStore(depth_differences, pixel_coordinates, edge_info_packed);

    return depth_center;
}

fn load_normal_view_space(uv_normals: vec2<f32>) -> vec3<f32> {
    var world_normal = textureSampleLevel(normals, point_clamp_sampler, uv_normals, 0.0).xyz;
    world_normal = (world_normal * 2.0) - 1.0;
    let view_from_world = mat3x3<f32>(
        view.view_from_world[0].xyz,
        view.view_from_world[1].xyz,
        view.view_from_world[2].xyz,
    );
    return view_from_world * world_normal;
}

fn reconstruct_view_space_position(depth: f32, uv: vec2<f32>) -> vec3<f32> {
    let clip_xy = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - 2.0 * uv.y);
    let t = view.view_from_clip * vec4<f32>(clip_xy, depth, 1.0);
    let view_xyz = t.xyz / t.w;
    return view_xyz;
}

// XeGTAO_FastACos
fn xe_fast_acos(x_in: f32) -> f32 {
    let x = abs(x_in);
    let res = (-0.156583 * x + HALF_PI) * sqrt(1.0 - x);
    return select(PI - res, res, x_in >= 0.0);
}

// The sample's weight: XeGTAO's falloff with its thin-occluder heuristic, then the old remaster's thickness limit (a
// sample far in front of the centre, a character before the ground behind it, fades out over another `thickness`;
// Bevy's view space looks down -Z, so "in front" is +z).
fn sample_weight(delta: vec3<f32>, falloff_mul: f32, falloff_add: f32) -> f32 {
    let falloff_base = length(vec3(delta.x, delta.y, delta.z * (1.0 + THIN_OCCLUDER_COMPENSATION)));
    var weight = saturate(falloff_base * falloff_mul + falloff_add);
    if (settings.thickness > 0.0) {
        weight *= saturate(2.0 - delta.z / settings.thickness);
    }
    return weight;
}

// XeGTAO_MainPass (Algorithm 1 of Jimenez et al. 2016 "Practical Real-Time Strategies for Accurate Indirect
// Occlusion"), in Bevy's view space; the visibility before the denoiser.
fn xegtao_main(pixel_coordinates: vec2<i32>, pixel_depth: f32) -> f32 {
    let slice_count = f32(constants::SLICE_COUNT);
    let steps_per_slice = f32(constants::SAMPLES_PER_SLICE_SIDE);
    let pixel_size = 1.0 / view.viewport.zw;
    let uv = (vec2<f32>(pixel_coordinates) + 0.5) * pixel_size;
    // move the centre slightly towards the camera against depth imprecision (XeGTAO: FP32 depths)
    let pix_center_pos = reconstruct_view_space_position(pixel_depth, uv) * 0.99999;
    let normals_size = vec2<f32>(textureDimensions(normals));
    let viewspace_normal = load_normal_view_space((vec2<f32>(pixel_coordinates) + 0.5 + view.viewport.xy) / normals_size);
    let view_vec = normalize(-pix_center_pos);
    let noise = load_noise(pixel_coordinates);

    let effect_radius = settings.radius * RADIUS_MULTIPLIER;
    let falloff_range = FALLOFF_RANGE * effect_radius;
    let falloff_from = effect_radius * (1.0 - FALLOFF_RANGE);
    let falloff_mul = -1.0 / falloff_range;
    let falloff_add = falloff_from / falloff_range + 1.0;

    var visibility = 0.0;
    // the size of a pixel in view space at the centre's distance
    let pixel_view_size = -pix_center_pos.z * 2.0 / (view.clip_from_view[0][0] * view.viewport.z);
    let screenspace_radius = effect_radius / pixel_view_size;
    // fade out for small screen radii
    visibility += saturate((10.0 - screenspace_radius) / 100.0) * 0.5;
    // the min distance to start sampling from (no useful data at the centre pixel)
    let min_s = 1.3 / screenspace_radius;

    for (var slice = 0.0; slice < slice_count; slice += 1.0) {
        let phi = (slice + noise.x) / slice_count * PI;
        let cos_phi = cos(phi);
        let sin_phi = sin(phi);
        let omega = vec2(cos_phi, -sin_phi) * screenspace_radius;
        let direction_vec = vec3(cos_phi, sin_phi, 0.0);
        let ortho_direction_vec = direction_vec - dot(direction_vec, view_vec) * view_vec;
        let axis_vec = normalize(cross(ortho_direction_vec, view_vec));
        let projected_normal_vec = viewspace_normal - axis_vec * dot(viewspace_normal, axis_vec);
        let sign_norm = sign(dot(ortho_direction_vec, projected_normal_vec));
        var projected_normal_vec_length = length(projected_normal_vec);
        let cos_norm = saturate(dot(projected_normal_vec, view_vec) / projected_normal_vec_length);
        let n = sign_norm * xe_fast_acos(cos_norm);
        let low_horizon_cos0 = cos(n + HALF_PI);
        let low_horizon_cos1 = cos(n - HALF_PI);
        var horizon_cos0 = low_horizon_cos0;
        var horizon_cos1 = low_horizon_cos1;

        for (var step_i = 0.0; step_i < steps_per_slice; step_i += 1.0) {
            // R1 sequence
            let step_noise = fract(noise.y + (slice + step_i * steps_per_slice) * 0.6180339887498948482);
            var s = (step_i + step_noise) / steps_per_slice;
            s = pow(s, SAMPLE_DISTRIBUTION_POWER) + min_s;
            var sample_offset = s * omega;
            let mip_level = clamp(log2(length(sample_offset)) - DEPTH_MIP_SAMPLING_OFFSET, 0.0, DEPTH_MIP_LEVELS);
            // snap to the pixel centre
            sample_offset = round(sample_offset) * pixel_size;

            let sample_uv0 = uv + sample_offset;
            let sample_pos0 = reconstruct_view_space_position(textureSampleLevel(preprocessed_depth, point_clamp_sampler, sample_uv0, mip_level).r, sample_uv0);
            let sample_uv1 = uv - sample_offset;
            let sample_pos1 = reconstruct_view_space_position(textureSampleLevel(preprocessed_depth, point_clamp_sampler, sample_uv1, mip_level).r, sample_uv1);
            let sample_delta0 = sample_pos0 - pix_center_pos;
            let sample_delta1 = sample_pos1 - pix_center_pos;
            let weight0 = sample_weight(sample_delta0, falloff_mul, falloff_add);
            let weight1 = sample_weight(sample_delta1, falloff_mul, falloff_add);
            // discard unwanted samples
            let shc0 = mix(low_horizon_cos0, dot(normalize(sample_delta0), view_vec), weight0);
            let shc1 = mix(low_horizon_cos1, dot(normalize(sample_delta1), view_vec), weight1);
            horizon_cos0 = max(horizon_cos0, shc0);
            horizon_cos1 = max(horizon_cos1, shc1);
        }

        // XeGTAO's fudge against slight overdarkening on high slopes
        projected_normal_vec_length = mix(projected_normal_vec_length, 1.0, 0.05);
        let h0 = -xe_fast_acos(horizon_cos1);
        let h1 = xe_fast_acos(horizon_cos0);
        let iarc0 = (cos_norm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
        let iarc1 = (cos_norm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
        visibility += projected_normal_vec_length * (iarc0 + iarc1);
    }
    visibility /= slice_count;
    visibility = pow(max(visibility, 0.0), FINAL_VALUE_POWER);
    // disallow total occlusion
    return max(0.03, visibility);
}

@compute
@workgroup_size(8, 8, 1)
fn ssao(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let pixel_coordinates = vec2<i32>(global_id.xy);
    let pixel_depth = calculate_neighboring_depth_differences(pixel_coordinates);
    // nothing drawn (reversed Z: 0 is infinitely far): open
    var visibility = 1.0;
    if (pixel_depth > 0.0) {
        visibility = xegtao_main(pixel_coordinates, pixel_depth);
    }
    textureStore(ambient_occlusion, pixel_coordinates, vec4<f32>(visibility, 0.0, 0.0, 0.0));
}
