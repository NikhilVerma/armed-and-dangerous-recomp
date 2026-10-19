// The old remaster's ambient occlusion apply (src/rhi/vulkan/vk_gtao.cpp GtaoApplyFragmentGlsl) on the SSAO term in
// the game material (graphics/ao_extras.rs; the term itself is XeGTAO's, with its power: graphics/xegtao.wesl):
//   1. multi-bounce with the surface's albedo (Jimenez et al. 2016, eq. 10): bright surfaces lose less light
//   2. strength, and only ao_snow of it on snow (the snow surfaces by name, ao_exempt, and snow-white albedo)
//   3. contact: where the term is clearly occluded (feet on the ground, objects resting on snow) ao_contact of it
//      darkens the whole light, the sun's share too, whatever step 2 left: characters sit on sunlit snow
// The old pass darkened the finished colour by the ambient share; here the occlusion splits the same way: `ambient`
// multiplies the ambient and fill light, `whole` caps the whole light.

struct Occlusion {
    ambient: f32,
    whole: f32,
}

// `visibility`: the SSAO term (1 = open); `albedo`: the surface colour's luminance (0..1, the game's gamma values);
// `strength`: ao_strength (0 on blended surfaces); `extras`: x ao_contact, y ao_snow, z 1 = a snow surface by name
// (GameMaterialParams::ao_extras).
fn remaster_occlusion(visibility: f32, albedo: f32, strength: f32, extras: vec4<f32>) -> Occlusion {
    let raw = clamp(visibility, 0.0, 1.0);
    let a = clamp(albedo, 0.0, 1.0);
    let ma = 2.0404 * a - 0.3324;
    let mb = -4.7951 * a + 0.6417;
    let mc = 2.7552 * a + 0.6903;
    var ao = max(raw, ((raw * ma + mb) * raw + mc) * raw);
    // snow scatters light through itself and is lit by the bright snow around it: occlusion mostly disappears
    let snow = max(extras.z, smoothstep(0.65, 0.8, a));
    ao = mix(1.0, ao, strength * mix(1.0, extras.y, snow));
    let contact = extras.x * strength * smoothstep(0.9, 0.45, raw);
    return Occlusion(ao, mix(1.0, raw, contact));
}
