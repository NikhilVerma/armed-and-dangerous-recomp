// Additive fx meshes (SRCALPHA / ONE: the flames, the muzzle flashes, the flying keg's fire trail, the glows): the
// texture times the material colour times the alpha, in the original's gamma space. The original adds that to its
// display-encoded back buffer; here the material draws in the transmissive pass, the blend sums these gamma-space
// colours over the scene, and additive.rs composites the sum as the original's addition (until 2026-10-25 each draw
// matched it over a fixed scene brightness, 0.6: far too bright over dark scenes).
import bevy_pbr::render::forward_io::VertexOutput;

struct FxAddParams {
    // rgb: the material colour (gamma), a: the object's alpha (fades)
    colour: vec4<f32>,
    // the uv transform's rows: u' = x.x u + x.y v + x.z, v' = y.x u + y.y v + y.z (the 0x40..0x45 transforms)
    uv_x: vec4<f32>,
    uv_y: vec4<f32>,
    // x: the texture stored premultiplied
    flags: vec4<f32>,
}

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var base: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var base_sampler: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var<uniform> p: FxAddParams;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let uv = vec2(dot(p.uv_x.xyz, vec3(in.uv, 1.0)), dot(p.uv_y.xyz, vec3(in.uv, 1.0)));
    let s = textureSample(base, base_sampler, uv);
    var c = pow(max(s.rgb, vec3(0.0)), vec3(1.0 / 2.2));
    if (p.flags.x > 0.5) {
        c = min(c / max(s.a, 1.0 / 255.0), vec3(1.0));
    }
    // premultiplied blend with alpha 0: the colour is added
    return vec4(c * p.colour.rgb * clamp(s.a * p.colour.a, 0.0, 1.0), 0.0);
}
