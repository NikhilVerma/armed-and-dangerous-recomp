// The screen effects' draw (fxvm/screen_post.rs; PostProcess_Pass 0x0047c600): the blur chain's steps and the
// composite of blur and fade on the game's values (the scene to them by a 2.2 power, as gamematerial.wesl writes
// them; back to linear light at the end).
import bevy_core_pipeline::fullscreen_vertex_shader::FullscreenVertexOutput;

@group(0) @binding(0) var source: texture_2d<f32>;
@group(0) @binding(1) var linear_sampler: sampler;

struct ScreenPost {
    // the fade's game colour and alpha
    fade: vec4<f32>,
    // the blur's chain level
    level: u32,
    // 1: level - 1 drawn opaque first
    opaque_below: u32,
    // the level drawn over at this alpha
    fraction: f32,
    // 1: the blur draws
    blur: u32,
    // 1: the small targets are drawn back through the cubic B-spline filter
    soft: u32,
}

@group(0) @binding(2) var below: texture_2d<f32>;
@group(0) @binding(3) var level: texture_2d<f32>;
@group(0) @binding(4) var<uniform> post: ScreenPost;

fn to_game(c: vec3<f32>) -> vec3<f32> {
    return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}

fn to_linear(c: vec3<f32>) -> vec3<f32> {
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

// The source averaged over this target texel's area: n x n bilinear taps (each the mean of 2 x 2 texels), n from
// the footprint; a halving step is the original's single tap between four texels (D3D9's half-texel offset).
fn box_filter(uv: vec2<f32>, from_screen: bool) -> vec3<f32> {
    let size = vec2<f32>(textureDimensions(source));
    let footprint = size * vec2(abs(dpdx(uv.x)), abs(dpdy(uv.y)));
    let n = clamp(ceil(footprint * 0.5), vec2(1.0), vec2(8.0));
    let step = footprint / n / size;
    var sum = vec3(0.0);
    for (var y = 0.0; y < n.y; y += 1.0) {
        for (var x = 0.0; x < n.x; x += 1.0) {
            let at = uv + (vec2(x, y) + 0.5 - n * 0.5) * step;
            let c = textureSampleLevel(source, linear_sampler, at, 0.0).rgb;
            sum += select(c, to_game(c), from_screen);
        }
    }
    return sum / (n.x * n.y);
}

@fragment
fn downsample_screen(in: FullscreenVertexOutput) -> @location(0) vec4<f32> {
    return vec4(box_filter(in.uv, true), 1.0);
}

@fragment
fn downsample(in: FullscreenVertexOutput) -> @location(0) vec4<f32> {
    return vec4(box_filter(in.uv, false), 1.0);
}

// A small target drawn back to the screen: the original's bilinear tap, or (smooth) a cubic B-spline over its 4 x 4
// nearest texels, taken as four bilinear taps. The spline has no creases at the texel edges, so a 64 x 32 target
// comes back as an even, Gaussian-like blur with no blocks.
fn blur_tap(t: texture_2d<f32>, uv: vec2<f32>) -> vec3<f32> {
    if post.soft == 0u {
        return textureSampleLevel(t, linear_sampler, uv, 0.0).rgb;
    }
    let size = vec2<f32>(textureDimensions(t));
    let p = uv * size - 0.5;
    let i = floor(p);
    let f = p - i;
    let f2 = f * f;
    let f3 = f2 * f;
    let w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
    let w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
    let w3 = f3 / 6.0;
    let w2 = 1.0 - w0 - w1 - w3;
    let g0 = w0 + w1;
    let g1 = w2 + w3;
    let h0 = (i - 0.5 + w1 / g0) / size;
    let h1 = (i + 1.5 + w3 / g1) / size;
    return textureSampleLevel(t, linear_sampler, vec2(h0.x, h0.y), 0.0).rgb * g0.x * g0.y
        + textureSampleLevel(t, linear_sampler, vec2(h1.x, h0.y), 0.0).rgb * g1.x * g0.y
        + textureSampleLevel(t, linear_sampler, vec2(h0.x, h1.y), 0.0).rgb * g0.x * g1.y
        + textureSampleLevel(t, linear_sampler, vec2(h1.x, h1.y), 0.0).rgb * g1.x * g1.y;
}

@fragment
fn composite(in: FullscreenVertexOutput) -> @location(0) vec4<f32> {
    let scene = textureSample(source, linear_sampler, in.uv);
    var c = to_game(scene.rgb);
    // Post_DrawBlur 0x0047d0f0
    if post.blur != 0u {
        if post.opaque_below != 0u {
            c = blur_tap(below, in.uv);
        }
        c = mix(c, blur_tap(level, in.uv), post.fraction);
    }
    // Post_DrawFade 0x0047cff0
    c = mix(c, post.fade.rgb, post.fade.a);
    return vec4(to_linear(c), scene.a);
}
