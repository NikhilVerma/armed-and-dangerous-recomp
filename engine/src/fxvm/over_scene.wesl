// Gamma-space addition over the scene behind a fragment (the camera's weather domes: fxlayer.wesl, fxadd.wesl).
//
// The original adds its additive fx (SRCALPHA / ONE) to a gamma-space back buffer: a colour c lifts a pixel of gamma
// brightness g to g + c. This target is linear, where the same light is (g + c)^2.2 - g^2.2: a small c over a dark
// pixel adds far less than c, over a bright one more. Added as it is, the rain levels' wind-spray dome (texture up to
// 41 / 255) lifted the dark lev_5_m1 / lev_5_m3 scenes to an even grey veil (the "grey rain dome", review C1, G5).
//
// A material that reads the view's transmission texture (Bevy's copy of the opaque pass, drawn in the transmissive
// phase) gets the scene's own colour per pixel and adds exactly what the original adds; elsewhere the addition is
// matched over a fixed gamma brightness (`fallback`).
@if(VIEW_TRANSMISSION_TEXTURE)
import bevy_pbr::render::mesh_view_bindings::view_transmission_texture;

// The opaque scene's linear colour under the fragment, or a negative value without the copy.
@if(VIEW_TRANSMISSION_TEXTURE)
fn scene_at(frag: vec4<f32>) -> vec3<f32> {
    let size = vec2<i32>(textureDimensions(view_transmission_texture));
    let p = clamp(vec2<i32>(floor(frag.xy)), vec2(0), size - vec2(1));
    return textureLoad(view_transmission_texture, p, 0).rgb;
}

@if(!VIEW_TRANSMISSION_TEXTURE)
fn scene_at(frag: vec4<f32>) -> vec3<f32> {
    return vec3(-1.0);
}

// The linear light to add for a gamma-space addition `c`: over the scene's own pixel when `exact` and the copy
// exists, else over a scene of gamma brightness `fallback`.
fn added_light(frag: vec4<f32>, c: vec3<f32>, exact: bool, fallback: f32) -> vec3<f32> {
    var lin = vec3(pow(fallback, 2.2));
    if (exact) {
        let s = scene_at(frag);
        if (s.r >= 0.0) {
            lin = clamp(s, vec3(0.0), vec3(1.0));
        }
    }
    let g = pow(lin, vec3(1.0 / 2.2));
    return max(pow(min(g + max(c, vec3(0.0)), vec3(1.0)), vec3(2.2)) - lin, vec3(0.0));
}
