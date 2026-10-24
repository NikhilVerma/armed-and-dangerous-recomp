// Additive fx meshes (SRCALPHA / ONE: the flames, the muzzle flashes, the flying keg's fire trail, the glows): the
// texture times the material colour, added to the scene. The original adds in gamma space; this target is linear,
// where adding the same colour reddens and darkens it (an orange (0.79, 0.38, 0.12) over a grey 0.6 scene gives
// (1.0, 0.98, 0.72) in the original, a pale yellow, and (0.92, 0.60, 0.33) added in linear: the keg's trail drew as a
// red column, capture 2026-10-18). So the addition is done as the original's over a scene of gamma brightness
// p.scene.x (the snow levels' 0.6, as the white particles' match: particles.rs WHITE_OVER): the linear light added is
// (g + c)^2.2 - g^2.2 per channel, c the gamma-space colour times the alpha.
import bevy_pbr::render::forward_io::VertexOutput;
import and_remaster::fxvm::over_scene::added_light;

struct FxAddParams {
    // rgb: the material colour (gamma), a: the object's alpha (fades)
    colour: vec4<f32>,
    // the uv transform's rows: u' = x.x u + x.y v + x.z, v' = y.x u + y.y v + y.z (the 0x40..0x45 transforms)
    uv_x: vec4<f32>,
    uv_y: vec4<f32>,
    // x: the scene brightness the addition is matched over (gamma); y: the texture stored premultiplied; z: added over
    // the scene's own pixel (the camera's weather domes, over_scene.wesl)
    scene: vec4<f32>,
}

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var base: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var base_sampler: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var<uniform> p: FxAddParams;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let uv = vec2(dot(p.uv_x.xyz, vec3(in.uv, 1.0)), dot(p.uv_y.xyz, vec3(in.uv, 1.0)));
    let s = textureSample(base, base_sampler, uv);
    var c = pow(max(s.rgb, vec3(0.0)), vec3(1.0 / 2.2));
    if (p.scene.y > 0.5) {
        c = min(c / max(s.a, 1.0 / 255.0), vec3(1.0));
    }
    let added = c * p.colour.rgb * clamp(s.a * p.colour.a, 0.0, 1.0);
    // premultiplied blend with alpha 0: the colour is added
    return vec4(added_light(in.position, added, p.scene.z > 0.5, p.scene.x), 0.0);
}
