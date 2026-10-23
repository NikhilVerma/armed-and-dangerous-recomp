// The old remaster's present pass (archive/src/rhi/vulkan/vk_present.cpp kFragment), last in the 3D camera's post-processing
// (graphics/present.rs): the filmic ACES curve ([render] tonemap_aces) and triangular dither noise of +-1 step of
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

// Narkowicz 2015 fit (vk_present.cpp Aces)
fn aces(x: vec3<f32>) -> vec3<f32> {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3(0.0), vec3(1.0));
}

fn to_gamma(c: vec3<f32>) -> vec3<f32> {
    return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}

fn to_linear(c: vec3<f32>) -> vec3<f32> {
    return pow(max(c, vec3(0.0)), vec3(2.2));
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
        c = to_linear(aces(to_gamma(c)));
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
