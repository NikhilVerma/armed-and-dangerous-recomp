// The original's far clip, shared by the game material (gamematerial.wesl), which drops its pixels beyond it, and the
// sky (graphics/farclip.wesl), which fills exactly those pixels where a depth prepass hides it: both decide the same.

// Where the dissolve starts, as a share of the far clip.
const FADE_START: f32 = 0.85;

// Interleaved gradient noise (Jimenez 2014), moving each frame so TAA averages the dissolve into a fade.
fn dither(pixel: vec2<f32>, frame: u32) -> f32 {
    let q = pixel + 5.588238 * f32(frame % 64u);
    return fract(52.9829189 * fract(dot(q, vec2(0.06711056, 0.00583715))));
}

// Whether a pixel at this view depth is beyond the far clip `far` (the original camera's far plane; 0: none): a hard
// cut, or (fade) a dissolve over the last 15 % before it.
fn far_clipped(depth: f32, far: f32, fade: bool, pixel: vec2<f32>, frame: u32) -> bool {
    if (far <= 0.0) {
        return false;
    }
    if (fade) {
        let t = smoothstep(far * FADE_START, far, depth);
        return t >= 1.0 || (t > 0.0 && dither(pixel, frame) < t);
    }
    return depth > far;
}
