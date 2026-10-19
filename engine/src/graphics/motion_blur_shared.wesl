// Per-object motion blur (graphics/motion_blur.rs): what its passes share.

// the tile size = the maximum blur radius in pixels (vk_motionblur.h kMotionBlurTile: a 3x3 neighbourhood of tiles
// then reaches every source)
const TILE: i32 = 48;

// graphics/motion_blur.rs MotionBlurParams
struct MotionBlurParams {
    // this frame's clip space -> last frame's clip space (the camera's motion; unjittered, as Bevy's motion vectors)
    reproject: mat4x4<f32>,
    // this frame's clip space -> view space
    view_from_clip: mat4x4<f32>,
    // the share of a frame's motion the shutter is open for: (1 / shutter_speed s) / frame time, at most 4
    exposure: f32,
    // the longest blur in pixels
    max_radius: f32,
}

// The distance in front of the camera of a pixel at this prepass depth (reversed Z).
fn view_distance(params: MotionBlurParams, d: f32) -> f32 {
    let p = params.view_from_clip * vec4(0.0, 0.0, max(d, 1e-7), 1.0);
    return -p.z / p.w;
}
