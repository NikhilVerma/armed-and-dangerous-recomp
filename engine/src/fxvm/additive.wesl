// The additive fx's composite (fxvm/additive.rs): the original adds them (SRCALPHA / ONE) to its 8-bit back buffer,
// in display (gamma) space, each add saturating at 1. The fx drew their gamma-space colours, summed, onto the scene
// in the transmissive pass; `before` is the scene as it was (Bevy's copy for that pass), `after` the scene with the
// sums on it. The difference is the sum S; the result is the original's: (scene's gamma value + S, at most 1) back to
// linear light, the scene's gamma value as gamematerial.wesl writes it (a 2.2 power). A channel already above 1 (the
// remaster's HDR) keeps its value: the original had saturated there.
import bevy_core_pipeline::fullscreen_vertex_shader::FullscreenVertexOutput;

@group(0) @binding(0) var before: texture_2d<f32>;
@group(0) @binding(1) var after: texture_2d<f32>;

@fragment
fn composite(in: FullscreenVertexOutput) -> @location(0) vec4<f32> {
    let p = vec2<i32>(in.position.xy);
    let l0 = textureLoad(before, p, 0);
    let l1 = textureLoad(after, p, 0);
    let sum = max(l1.rgb - l0.rgb, vec3(0.0));
    let g0 = pow(clamp(l0.rgb, vec3(0.0), vec3(1.0)), vec3(1.0 / 2.2));
    let lin = pow(min(g0 + sum, vec3(1.0)), vec3(2.2));
    return vec4(max(l0.rgb, lin), l1.a);
}
