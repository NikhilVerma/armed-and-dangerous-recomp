// Per-object motion blur, the compute passes (graphics/motion_blur.rs), ported from the old remaster's
// src/rhi/vulkan/vk_motionblur.cpp (kVelocity, kTileMax, kNeighbourMax; McGuire et al. 2012, "A Reconstruction Filter
// for Plausible Motion Blur"). One layout for the three: each reads `src` (or the prepass) and writes `dst`.
import and_remaster::graphics::motion_blur_shared::{MotionBlurParams, TILE};

@group(0) @binding(0) var<uniform> params: MotionBlurParams;
// the depth prepass (reversed Z: 0 = infinitely far)
@group(0) @binding(1) var depth: texture_depth_2d;
// Bevy's motion vectors: this frame's uv - last frame's, everything that moved, the camera included
@group(0) @binding(2) var motion: texture_2d<f32>;
@group(0) @binding(3) var src: texture_2d<f32>;
@group(0) @binding(4) var dst: texture_storage_2d<rgba16float, write>;

// Pass 0: object velocity = full motion - camera motion, in pixels, scaled by the shutter and clamped.
@compute @workgroup_size(8, 8, 1)
fn velocity(@builtin(global_invocation_id) id: vec3<u32>) {
    let size = vec2<i32>(textureDimensions(dst));
    let px = vec2<i32>(id.xy);
    if (any(px >= size)) {
        return;
    }
    let m = -textureLoad(motion, px, 0).xy;
    let d = textureLoad(depth, px, 0);
    var v = vec2(0.0);
    // nothing drew motion here (cleared to 0) or sky: no object motion
    if (d > 0.0 && any(m != vec2(0.0))) {
        let sizef = vec2<f32>(size);
        let uv = (vec2<f32>(px) + 0.5) / sizef;
        let ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        let prev = params.reproject * vec4(ndc, d, 1.0);
        let pndc = prev.xy / select(prev.w, 1e-7, abs(prev.w) < 1e-7);
        let prev_uv = vec2(pndc.x * 0.5 + 0.5, 0.5 - pndc.y * 0.5);
        let camera = prev_uv - uv;
        // Only surfaces that move in the world blur (static scenery stays sharp while the camera pans), and they blur
        // by their motion ON SCREEN, as a real camera sees it: a runner the camera follows stays sharp except for his
        // swinging limbs (blurring by the world motion smeared the tracked player, 2026-10-12).
        let world = (m - camera) * sizef;
        let screen = m * sizef;
        let world_px = length(world);
        let moving = smoothstep(0.75, 2.0, world_px);
        // The shorter of the two: a runner the camera follows (small on screen) and a character while the camera
        // turns (its screen motion is the camera's) both stay sharp; only real object motion blurs (the player:
        // "the character still blurs when moving the camera on their edges").
        v = select(world, screen, dot(screen, screen) < dot(world, world)) * params.exposure * moving;
        let len = length(v);
        if (!(len >= 0.5) || len > 1e6) {
            v = vec2(0.0);
        } else if (len > params.max_radius) {
            v *= params.max_radius / len;
        }
    }
    textureStore(dst, px, vec4(v, 0.0, 0.0));
}

// Pass 1: the longest velocity in each tile.
@compute @workgroup_size(8, 8, 1)
fn tile_max(@builtin(global_invocation_id) id: vec3<u32>) {
    let t = vec2<i32>(id.xy);
    if (any(t >= vec2<i32>(textureDimensions(dst)))) {
        return;
    }
    let lim = vec2<i32>(textureDimensions(src)) - 1;
    var best = vec2(0.0);
    var best_len = 0.0;
    for (var y = 0; y < TILE; y++) {
        for (var x = 0; x < TILE; x++) {
            let v = textureLoad(src, min(t * TILE + vec2(x, y), lim), 0).xy;
            let l = dot(v, v);
            if (l > best_len) {
                best = v;
                best_len = l;
            }
        }
    }
    textureStore(dst, t, vec4(best, 0.0, 0.0));
}

// Pass 2: the longest tile velocity in the 3x3 neighbourhood (motion reaching into this tile from next to it).
@compute @workgroup_size(8, 8, 1)
fn neighbour_max(@builtin(global_invocation_id) id: vec3<u32>) {
    let t = vec2<i32>(id.xy);
    let lim = vec2<i32>(textureDimensions(src)) - 1;
    if (any(t > lim)) {
        return;
    }
    var best = vec2(0.0);
    var best_len = 0.0;
    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            let v = textureLoad(src, clamp(t + vec2(x, y), vec2(0), lim), 0).xy;
            let l = dot(v, v);
            if (l > best_len) {
                best = v;
                best_len = l;
            }
        }
    }
    textureStore(dst, t, vec4(best, 0.0, 0.0));
}
