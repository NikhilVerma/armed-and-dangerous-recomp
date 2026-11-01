// Additive fx meshes (SRCALPHA / ONE: the flames, the muzzle flashes, the flying keg's fire trail, the glows): the
// texture times the material colour times the alpha, in the original's gamma space. The original adds that to its
// display-encoded back buffer; here the material draws in the transmissive pass, the blend sums these gamma-space
// colours over the scene, and additive.rs composites the sum as the original's addition (until 2026-10-25 each draw
// matched it over a fixed scene brightness, 0.6: far too bright over dark scenes).
// Remaster (graphics.rs soft_particles, as particle.wesl): with the depth prepass, the card fades out where it meets
// geometry, over a part (flags.y) of its own size, at most the particles' depth (flags.z). The original draws these cards (fx_imp_stig's fireball fxo_spark_anim, the turret
// rockets' and the grenades' blasts) depth-tested with no bias, so a camera-facing card cuts into the ground along a
// hard horizontal line (its captures show it: lev_bonus_m1 walk frame 165).
import bevy_pbr::render::forward_io::VertexOutput;
import bevy_pbr::render::mesh_view_bindings::view;
@if(DEPTH_PREPASS)
import bevy_pbr::prepass::utils as prepass_utils;

struct FxAddParams {
    // rgb: the material colour (gamma), a: the object's alpha (fades)
    colour: vec4<f32>,
    // the uv transform's rows: u' = x.x u + x.y v + x.z, v' = y.x u + y.y v + y.z (the 0x40..0x45 transforms)
    uv_x: vec4<f32>,
    uv_y: vec4<f32>,
    // x: the texture stored premultiplied; y: the soft fade's depth as a part of the card's size (0: hard, the
    // original); z: its most, in game units (the particles' soft depth)
    flags: vec4<f32>,
}

@group(constants::MATERIAL_BIND_GROUP) @binding(0) var base: texture_2d<f32>;
@group(constants::MATERIAL_BIND_GROUP) @binding(1) var base_sampler: sampler;
@group(constants::MATERIAL_BIND_GROUP) @binding(2) var<uniform> p: FxAddParams;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let uv = vec2(dot(p.uv_x.xyz, vec3(in.uv, 1.0)), dot(p.uv_y.xyz, vec3(in.uv, 1.0)));
    let s = textureSample(base, base_sampler, uv);
    // A texture stored premultiplied (DXT2 in the bank: the bullet trails fx_trail_*, the fire trail) is un-premultiplied
    // first. Sampling it as stored (c a^2 added, 2026-10-25 ebdbf8ce, reverted) made the turret trails thin faint lines;
    // the original's captures show them as broad white-hot spindles with orange tails near the camera
    // (work/review/bonus_tracers.jpg, lev_bonus_m1 walk frames 100 and 150), as this draws them.
    var c = pow(max(s.rgb, vec3(0.0)), vec3(1.0 / 2.2));
    if (p.flags.x > 0.5) {
        c = min(c / max(s.a, 1.0 / 255.0), vec3(1.0));
    }
    var a = clamp(s.a * p.colour.a, 0.0, 1.0);
@if(DEPTH_PREPASS)
    {
        if (p.flags.y > 0.0) {
            // the card's size: world units per unit of its mesh uvs (a card's uvs span 0..1). A fixed depth either dimmed
            // the 4-unit fireballs whole (6 units) or left the 20-unit flash fxo_flash_2 a hard line (1.2 units)
            let size = length(dpdx(in.world_position.xyz)) / max(length(dpdx(in.uv)), 1e-6);
            let depth = max(min(p.flags.z, p.flags.y * size), 1e-3);
            // reverse-z, infinite far: view distance = near / ndc depth (clip_from_view[3][2] is the near plane)
            let scene = prepass_utils::prepass_depth(in.position, 0u);
            let near = view.clip_from_view[3][2];
            let gap = near / max(scene, 1e-7) - near / max(in.position.z, 1e-7);
            a *= clamp(gap / depth, 0.0, 1.0);
        }
    }
    // premultiplied blend with alpha 0: the colour is added
    return vec4(c * p.colour.rgb * a, 0.0);
}
