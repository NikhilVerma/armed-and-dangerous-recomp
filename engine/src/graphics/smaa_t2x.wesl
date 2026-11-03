// The blend step of SMAA T2x (graphics/smaa_t2x.rs): this frame's SMAA picture with last frame's.
import bevy_core_pipeline::fullscreen_vertex_shader::FullscreenVertexOutput;

@group(0) @binding(0) var current: texture_2d<f32>;
@group(0) @binding(1) var history: texture_2d<f32>;
@group(0) @binding(2) var motion: texture_2d<f32>;
@group(0) @binding(3) var linear_sampler: sampler;

struct Output {
    @location(0) picture: vec4<f32>,
    @location(1) next_history: vec4<f32>,
}

@fragment
fn fragment(in: FullscreenVertexOutput) -> Output {
    let pixel = vec2<i32>(in.position.xy);
    let last = vec2<i32>(textureDimensions(current)) - 1;
    let colour = textureLoad(current, pixel, 0);

    // The range of colours around this pixel. A history colour outside it belongs to something that has since moved
    // or changed, such as a snowflake, which writes no motion vectors. Keeping it inside the range stops ghost trails.
    var low = colour.rgb;
    var high = colour.rgb;
    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            let around = textureLoad(current, clamp(pixel + vec2<i32>(x, y), vec2<i32>(0), last), 0).rgb;
            low = min(low, around);
            high = max(high, around);
        }
    }

    let moved = textureLoad(motion, pixel, 0).rg;
    let was_at = in.uv - moved;
    let on_screen = all(was_at >= vec2<f32>(0.0)) && all(was_at <= vec2<f32>(1.0));

    // An even mix when still, fading to none by 4 pixels of motion, because resampling a moving history softens it.
    let moved_pixels = length(moved * vec2<f32>(textureDimensions(current)));
    var weight = 0.5 * (1.0 - smoothstep(0.5, 4.0, moved_pixels));
    if (!on_screen) {
        weight = 0.0;
    }

    let past = clamp(textureSampleLevel(history, linear_sampler, was_at, 0.0).rgb, low, high);
    return Output(vec4<f32>(mix(colour.rgb, past, weight), colour.a), colour);
}
