// Per-object motion blur, the reconstruction (graphics/motion_blur.rs), ported from the old remaster's
// src/rhi/vulkan/vk_motionblur.cpp kReconstruct (McGuire et al. 2012): samples along the neighbourhood's dominant
// velocity, each weighted by whether it is in front of / behind this pixel and whether its own (or this pixel's) blur
// reaches here.
import bevy_core_pipeline::fullscreen_vertex_shader::FullscreenVertexOutput;
import and_remaster::graphics::motion_blur_shared::{MotionBlurParams, TILE, view_distance};

@group(0) @binding(0) var<uniform> params: MotionBlurParams;
@group(0) @binding(1) var depth: texture_depth_2d;
@group(0) @binding(2) var color: texture_2d<f32>;
// object velocity in pixels (motion_blur_tiles.wesl velocity)
@group(0) @binding(3) var velocity: texture_2d<f32>;
// the neighbourhood's longest velocity per tile (motion_blur_tiles.wesl neighbour_max)
@group(0) @binding(4) var neighbour: texture_2d<f32>;

fn cone(dist: f32, v: f32) -> f32 {
    return clamp(1.0 - dist / max(v, 1e-3), 0.0, 1.0);
}

fn cylinder(dist: f32, v: f32) -> f32 {
    return 1.0 - smoothstep(0.95 * v, 1.05 * v, dist);
}

// 1 where b is in front of a (by more than 2 % of the nearer distance), easing over that
fn soft_depth(za: f32, zb: f32) -> f32 {
    return clamp(1.0 - (za - zb) / max(0.02 * min(za, zb), 0.05), 0.0, 1.0);
}

fn hash(v: vec2<f32>) -> f32 {
    return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.5453);
}

@fragment
fn fragment(in: FullscreenVertexOutput) -> @location(0) vec4<f32> {
    let px = vec2<i32>(in.position.xy);
    let cx = textureLoad(color, px, 0);
    let tiles = vec2<i32>(textureDimensions(neighbour));
    let vn = textureLoad(neighbour, min(px / TILE, tiles - 1), 0).xy;
    if (length(vn) < 0.5) {
        return cx;
    }
    let lim = vec2<i32>(textureDimensions(color)) - 1;
    let vx = textureLoad(velocity, px, 0).xy;
    let len_x = max(length(vx), 0.5);
    let zx = view_distance(params, textureLoad(depth, px, 0));
    var wsum = 1.0 / len_x;
    var sum = cx.rgb * wsum;
    const N = 15;
    let j = hash(vec2<f32>(px)) - 0.5;
    for (var i = 0; i < N; i++) {
        if (i == N / 2) {
            continue;
        }
        let t = mix(-1.0, 1.0, (f32(i) + j * 0.95 + 0.5) / f32(N)) * 0.5;
        let py = clamp(vec2<i32>(vec2<f32>(px) + vn * t + 0.5), vec2(0), lim);
        let dist = length(vec2<f32>(py - px));
        let zy = view_distance(params, textureLoad(depth, py, 0));
        let len_y = max(length(textureLoad(velocity, py, 0).xy), 0.5);
        let f = soft_depth(zx, zy);  // y in front of x
        let b = soft_depth(zy, zx);  // y behind x
        let w = f * cone(dist, len_y) + b * cone(dist, len_x) + cylinder(dist, len_y) * cylinder(dist, len_x) * 2.0;
        sum += textureLoad(color, py, 0).rgb * w;
        wsum += w;
    }
    return vec4(sum / wsum, cx.a);
}
