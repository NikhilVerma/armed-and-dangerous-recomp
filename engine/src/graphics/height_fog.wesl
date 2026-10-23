// The old remaster's modern fog ([render] fog_modern; archive/src/rhi/vulkan/vk_fog.cpp ModernFogGlsl, the uniforms from
// vk_device.cpp ModernFogUniforms): exponential height fog in place of the level's linear fog, on every surface the
// level's fog reaches (graphics/height_fog.rs).
//
// Density falls off with height, rho(h) = a exp(-b (h - h_cam)). The optical depth from the camera to a point at
// distance s beyond the fog start and height difference dh is  tau = a s (1 - exp(-b dh)) / (b dh),  transmittance
// T = exp(-tau). a: level with the camera, half the light gets through half way through the level's fog range (as
// with its linear fog; about 6 % at its end), times the density setting, falling off with the camera's height above
// the level's reference height (climbing thins the fog, valleys thicken it). In-scattering: the level's fog colour
// plus a forward-scattering glow towards the sun.

// The fog at a point: rgb the in-scattered colour, a the transmittance (1 = clear). `to_point`: camera to the point
// (engine space, Y up); `fog_range`: the level's fog start and end; `params`: x density scale, y height falloff per
// unit, z sun glow, w the reference height (GameMaterialParams::height_fog); `to_sun`: unit vector towards the sun.
fn height_fog(to_point: vec3<f32>, camera_height: f32, fog_col: vec3<f32>, fog_range: vec2<f32>, params: vec4<f32>, to_sun: vec3<f32>) -> vec4<f32> {
    let range = max(fog_range.y - fog_range.x, 1.0);
    let falloff = params.y;
    let above = clamp(camera_height - params.w, -200.0, 200.0);
    let density = log(2.0) / (0.5 * range) * params.x * exp(-falloff * above);
    let s = max(length(to_point) - fog_range.x, 0.0);
    let k = falloff * to_point.y;
    let h = select(1.0, (1.0 - exp(-k)) / k, abs(k) > 1e-4);
    let transmittance = exp(-density * s * h);
    // Henyey-Greenstein phase (g = 0.65, haze) relative to isotropic, (1 - g^2) / (1 + g^2 - 2 g cos)^1.5: its excess
    // over 1 brightens the fog around the sun
    let c = dot(normalize(to_point + vec3(1e-6)), to_sun);
    let hg = 0.5775 / pow(1.4225 - 1.3 * c, 1.5);
    let glow = max(hg - 1.0, 0.0) * 0.08 * params.z;
    // the old remaster's present pass clamped to 1 (vk_present.cpp): no bloom from the fog
    let col = min(fog_col + vec3(1.0, 0.95, 0.85) * glow, vec3(1.0));
    return vec4(col, transmittance);
}
