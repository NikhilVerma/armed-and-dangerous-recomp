// The old remaster's present pass (archive/src/rhi/vulkan/vk_present.cpp kFragment), last in the 3D camera's post-processing
// (graphics/present.rs): the filmic tone curve ([render] tonemap_aces; a highlight roll-off since 2026-10-23, the
// old pass's ACES fit washed the image out) and triangular dither noise of +-1 step of
// the 8-bit output ([render] dither), so the smooth gradients of sky, fog and snow shading do not band.
//
// The old pass read the game's values as they were (D3D9 colours: display-encoded, the game computes in gamma
// space) and wrote the 8-bit present image. Here the scene is linear light in a float target and the window's sRGB
// encoding comes after: the curve runs on the game's values (to_gamma below, as gamematerial.wesl writes them) and
// the noise is added in the display encoding (sRGB), each turned back to linear for the store.
import bevy_core_pipeline::fullscreen_vertex_shader::FullscreenVertexOutput;

@group(0) @binding(0) var screen: texture_2d<f32>;
@group(0) @binding(1) var screen_sampler: sampler;
struct PresentPass {
    // 1: the ACES curve
    aces: u32,
    // 1: dither
    dither: u32,
    // varies the noise per frame (temporal AA averages it away)
    frame: u32,
    _pad: u32,
}
@group(0) @binding(2) var<uniform> settings: PresentPass;

// The filmic tone curve as a highlight roll-off (graphics/present.rs `PresentSettings::tonemap_aces`): the game's own
// range up to the knee unchanged, the brightest channel above it eased towards white (a Reinhard shoulder, the same
// slope at the knee: no visible seam) and the other channels scaled with it, so hues and saturation stay. The old
// pass's Narkowicz ACES fit on these display values lifted the shadows and dimmed white to 0.8 (lev_1_m1 start: luma
// spread 45.6 -> 31.1, mean saturation 0.142 -> 0.088, the original 47.7 / 0.157), a wash over the whole image; ACES
// is made for scene-referred HDR, the game's values are already display-referred.
const KNEE: f32 = 0.9;

fn highlight_rolloff(x: vec3<f32>) -> vec3<f32> {
    let peak = max(max(x.r, x.g), x.b);
    if (peak <= KNEE) {
        return x;
    }
    let e = (peak - KNEE) / (1.0 - KNEE);
    let eased = KNEE + (1.0 - KNEE) * e / (1.0 + e);
    return x * (eased / peak);
}

// sRGB encode without the clamp at white: values above it (additive effects, bloom) keep their excess for the curve
fn srgb_encode_unclamped(c: vec3<f32>) -> vec3<f32> {
    let x = max(c, vec3(0.0));
    return select(1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, x * 12.92, x <= vec3(0.0031308));
}

fn srgb_encode(c: vec3<f32>) -> vec3<f32> {
    let x = clamp(c, vec3(0.0), vec3(1.0));
    return select(1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, x * 12.92, x <= vec3(0.0031308));
}

fn srgb_decode(c: vec3<f32>) -> vec3<f32> {
    let x = max(c, vec3(0.0));
    return select(pow((x + 0.055) / 1.055, vec3(2.4)), x / 12.92, x <= vec3(0.04045));
}

@fragment
fn fragment(in: FullscreenVertexOutput) -> @location(0) vec4<f32> {
    let src = textureSample(screen, screen_sampler, in.uv);
    var c = src.rgb;
    if (settings.aces != 0u) {
        // the game's values, as gamematerial.wesl writes them (the sRGB curve, both ways)
        c = srgb_decode(highlight_rolloff(srgb_encode_unclamped(c)));
    }
    if (settings.dither != 0u) {
        // two uniform hashes summed: triangular noise in (-1, 1) steps, which hides banding without the noise level
        // depending on the signal (rectangular noise of the same width leaves faint bands)
        let p = in.position.xy + f32(settings.frame) * vec2(47.0, 17.0);
        let a = fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
        let b = fract(sin(dot(p, vec2(39.3468, 11.135))) * 24634.6345);
        c = srgb_decode(srgb_encode(c) + (a + b - 1.0) / 255.0);
    }
    return vec4(c, src.a);
}
