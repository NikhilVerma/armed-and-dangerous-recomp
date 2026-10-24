// GPU snowfall (graphics/snow.rs): one compute thread per flake moves it (state in a storage buffer), then each flake
// is drawn as a soft sprite stretched along its motion over the shutter time. Ported from the old remaster's Vulkan
// snow (archive/src/rhi/vulkan/vk_snow.cpp), in Bevy's world space: Y is up, the ground plane is x, z.
//
// Simulation, per flake:
//   - born in the sky above the camera, it falls at its terminal speed (larger flakes a little faster)
//   - the air moves: a base wind whose strength gusts in waves travelling downwind, plus swirling turbulence; the flake
//     follows the air with a short lag (drag), so it drifts, eddies and tumbles instead of sliding along a line
//   - a flake that falls into a visible surface (the depth prepass) has landed, one that leaves the bottom of the volume
//     has fallen out: both are born again in the sky; sideways the volume wraps around the camera
// The first frame, and after a camera jump (a level start, a teleport), the whole volume is filled at once.
//
// Shader defs: SIM (the compute pipeline: the flakes are writable), MULTISAMPLED (the depth prepass has samples).

struct Snow {
    view_from_world: mat4x4<f32>,
    clip_from_view: mat4x4<f32>,
    // camera position (world), w: 1 = fill the volume this frame
    cam: vec4<f32>,
    // base wind x, z (units / s), terminal fall speed, turbulence (swirl speed)
    air: vec4<f32>,
    // horizontal half size of the volume, sky height above the camera, depth below it, drag time (s)
    volume: vec4<f32>,
    // time (game seconds), time step (0 while paused), flake count, streak exposure (s)
    clock: vec4<f32>,
    // viewport x, y, width, height (pixels of the target)
    viewport: vec4<f32>,
    // flake radius, soft depth (units), longest streak (pixels), 1 = debug (red, no depth test)
    look: vec4<f32>,
    // flake colour as lit in the level (gamma), opacity
    color: vec4<f32>,
    // fog colour (gamma), w: 1 = fog on
    fog: vec4<f32>,
    // fog start, fog end (view depth), unused, unused
    fog_range: vec4<f32>,
}

struct Flake {
    // xyz world, w: size 0..1
    pos: vec4<f32>,
    // xyz world units per second, w: per-flake seed 0..1
    vel: vec4<f32>,
}

@group(0) @binding(0) var<uniform> p: Snow;
@if(SIM)
@group(0) @binding(1) var<storage, read_write> flakes: array<Flake>;
@else
@group(0) @binding(1) var<storage, read> flakes: array<Flake>;
@if(MULTISAMPLED)
@group(0) @binding(2) var depth_tex: texture_depth_multisampled_2d;
@else
@group(0) @binding(2) var depth_tex: texture_depth_2d;

// The depth prepass at a pixel (reverse Z: 0 = nothing drawn there, the sky).
// (the third argument is the sample with MULTISAMPLED, else the mip level)
fn scene_depth(px: vec2<i32>) -> f32 {
    return textureLoad(depth_tex, px, 0);
}

// The view-space z (negative in front) of a depth value: depth = (P22 z + P32) / -z.
fn view_z(d: f32) -> f32 {
    return -p.clip_from_view[3].z / (d + p.clip_from_view[2].z);
}

fn hash3(n0: u32) -> vec3<f32> {
    var n = (n0 << 13u) ^ n0;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    let k = vec3<u32>(n, n, n) * vec3<u32>(n, n * 16807u, n * 48271u);
    return vec3<f32>(k & vec3<u32>(0x7fffffffu)) / 2147483647.0;
}

// The air's velocity at x: base wind with travelling gusts, plus swirls (sums of sines: cheap and smooth).
fn air(x: vec3<f32>, t: f32) -> vec3<f32> {
    let wind = p.air.xy;
    let dir = normalize(wind + vec2(1e-4));
    let along = dot(x.xz, dir);
    let gust = 1.0 + 0.7 * sin(t * 0.31 - along * 0.06) + 0.3 * sin(t * 0.83 - along * 0.17 + x.y * 0.1);
    var a = vec3(wind.x, 0.0, wind.y) * max(gust, 0.1);
    let k = p.air.w;
    let q = x * 0.23;
    a.x += k * (sin(q.y * 2.1 + t * 0.7) + 0.6 * cos(q.z * 1.7 - t * 0.5));
    a.z += k * (cos(q.y * 1.9 - t * 0.6) + 0.6 * sin(q.x * 1.5 + t * 0.4));
    a.y += k * 0.45 * sin(q.x * 1.3 + q.z * 1.1 + t * 0.9);
    return a;
}

@if(SIM)
fn born(i: u32, anywhere: bool, t: f32, size: f32, seed: f32) {
    let h = hash3(i * 3u + u32(t * 997.0) * 7919u + 1u);
    let r = p.volume.x;
    var pos = vec3(0.0);
    pos.x = p.cam.x + (h.x * 2.0 - 1.0) * r;
    pos.z = p.cam.z + (h.y * 2.0 - 1.0) * r;
    pos.y = select(p.cam.y + p.volume.y * (0.85 + 0.15 * h.z), p.cam.y - p.volume.z + h.z * (p.volume.y + p.volume.z), anywhere);
    flakes[i].pos = vec4(pos, size);
    flakes[i].vel = vec4(air(pos, t) - vec3(0.0, p.air.z, 0.0), seed);
}

@if(SIM)
@compute @workgroup_size(64)
fn simulate(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (f32(i) >= p.clock.z) {
        return;
    }
    let t = p.clock.x;
    let dt = p.clock.y;
    if (p.cam.w > 0.5) {
        let s = hash3(i * 11u + 3u);
        // sizes skewed small, a few large aggregates
        born(i, true, t, s.x * s.x * s.x, s.y);
        return;
    }
    if (dt <= 0.0) {
        return;  // paused
    }
    let f = flakes[i];
    var pos = f.pos.xyz;
    var vel = f.vel.xyz;
    let size = f.pos.w;
    let seed = f.vel.w;
    let terminal = p.air.z * (0.8 + 0.25 * seed + 0.5 * size);
    let want = air(pos, t) - vec3(0.0, terminal, 0.0);
    // drag: the flake relaxes towards the air's velocity (minus its fall) with time constant tau
    let tau = p.volume.w * (0.6 + 0.8 * size);
    vel += (want - vel) * (1.0 - exp(-dt / tau));
    // tumbling: a small spin around the fall line, per flake
    let spin = t * (1.5 + 2.0 * seed) + seed * 40.0;
    let flutter = vec3(cos(spin), 0.0, sin(spin)) * 0.25 * (1.0 - size);
    pos += (vel + flutter) * dt;

    // landed: crossed into a visible surface (camera space, against the depth prepass)
    let c = (p.view_from_world * vec4(pos, 1.0)).xyz;
    if (c.z < -0.1) {
        let ndc = vec2(p.clip_from_view[0].x * c.x, p.clip_from_view[1].y * c.y) / -c.z;
        let px = p.viewport.xy + vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * p.viewport.zw;
        if (all(px >= p.viewport.xy) && all(px < p.viewport.xy + p.viewport.zw)) {
            let d = scene_depth(vec2<i32>(px));
            if (d > 0.0) {
                let gap = c.z - view_z(d);  // < 0: behind the surface
                if (gap < 0.0 && gap > -0.6) {
                    born(i, false, t, size, seed);
                    return;
                }
            }
        }
    }
    // fell out of the bottom, or carried far above the sky: born again
    if (pos.y < p.cam.y - p.volume.z || pos.y > p.cam.y + p.volume.y + 4.0) {
        born(i, false, t, size, seed);
        return;
    }
    // sideways the volume wraps around the camera
    let r = p.volume.x;
    let rel = pos.xz - p.cam.xz;
    let wrapped = pos.xz - 2.0 * r * floor((rel + r) / (2.0 * r));
    flakes[i].pos = vec4(wrapped.x, pos.y, wrapped.y, size);
    flakes[i].vel = vec4(vel, seed);
}

struct FlakeOut {
    @builtin(position) position: vec4<f32>,
    @location(0) corner: vec2<f32>,
    // the flake's camera-space z (negative in front)
    @location(1) flake_z: f32,
    // linear colour, opacity
    @location(2) color: vec4<f32>,
}

fn to_linear(c: vec3<f32>) -> vec3<f32> {
    // the sRGB curve, as gamematerial.wesl (a 2.2 power darkened the darkest values)
    let x = max(c, vec3(0.0));
    return select(pow((x + 0.055) / 1.055, vec3(2.4)), x / 12.92, x <= vec3(0.04045));
}

@if(!SIM)
@vertex
fn vertex(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> FlakeOut {
    var out: FlakeOut;
    let f = flakes[ii];
    let world = f.pos.xyz;
    let size = f.pos.w;
    let c = (p.view_from_world * vec4(world, 1.0)).xyz;
    // where the flake was a moment ago: the quad stretches between the two (a motion streak over the shutter time).
    // The flake's own motion only: camera pans never streak (the player: "I don't want camera pans to blur the screen")
    var c0 = (p.view_from_world * vec4(world - f.vel.xyz * p.clock.w, 1.0)).xyz;
    var radius = p.look.x * (0.5 + 2.0 * size);
    // the apparent size is capped only for flakes very close to the lens (a disc covering the screen): near flakes are
    // visibly larger than far ones, so the snow has depth (the player, 2026-10-11)
    radius = min(radius, max(-c.z, 0.0) * 0.012);
    let focal = p.clip_from_view[0].x * 0.5 * p.viewport.z;  // pixels per unit at distance 1
    var d = c.xy - c0.xy;
    var len = length(d);
    // a fast flake's streak is capped (pixels)
    let max_len = p.look.z * max(-c.z, 0.1) / focal;
    if (len > max_len) {
        c0 = vec3(c.xy - d * (max_len / len), c0.z);
        d = c.xy - c0.xy;
        len = max_len;
    }
    let ax = select(vec2(0.0, 1.0), d / max(len, 1e-6), len > 1e-5);
    let ay = vec2(-ax.y, ax.x);
    var corners = array<vec2<f32>, 6>(vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    let corner = corners[vi];
    let mid = 0.5 * (c + c0);
    // fade near the camera, at the sides of the volume (where it wraps) and just below the sky (birth)
    let rel = world.xz - p.cam.xz;
    let edge = max(abs(rel.x), abs(rel.y));
    var alpha = smoothstep(1.0, 2.5, -c.z) * (1.0 - smoothstep(0.8 * p.volume.x, p.volume.x, edge))
        * (1.0 - smoothstep(p.cam.y + 0.7 * p.volume.y, p.cam.y + 0.85 * p.volume.y, world.y));
    // a streak spreads the flake's light over its length
    var half_len = radius + 0.5 * len;
    alpha *= radius / half_len * 0.5 + 0.5;
    // distant flakes stay at least ~1.2 pixels wide (sub-pixel sprites flicker), their light scaled down to the true
    // covered area
    let px = radius * focal / max(-c.z, 0.1);
    if (px < 1.2) {
        alpha *= px / 1.2;
        radius = 1.2 * max(-c.z, 0.1) / focal;
        half_len = radius + 0.5 * len;
    }
    // aerial fade with distance
    alpha *= mix(1.0, 0.7, smoothstep(10.0, 140.0, -c.z));
    let q = vec3(mid.xy + ax * corner.x * half_len + ay * corner.y * radius, mid.z);
    // lit as the level lights it, then the level's linear fog (as the game material: gamma space, by view depth)
    var col = p.color.rgb * (0.82 + 0.18 * f.vel.w);
    if (p.fog.w > 0.5) {
        let fog = clamp((p.fog_range.y + c.z) / max(p.fog_range.y - p.fog_range.x, 1e-3), 0.0, 1.0);
        col = mix(p.fog.rgb, col, fog);
    }
    out.color = vec4(to_linear(col), p.color.a * alpha);
    out.corner = corner;
    out.flake_z = c.z;
    let visible = c.z < -0.05 && c0.z < -0.05 && alpha > 0.0;
    out.position = select(vec4(0.0, 0.0, -1.0, 1.0), p.clip_from_view * vec4(q, 1.0), visible);
    return out;
}

@if(!SIM)
@fragment
fn fragment(in: FlakeOut) -> @location(0) vec4<f32> {
    let r = dot(in.corner, in.corner);
    if (r > 1.0) {
        discard;
    }
    // behind a surface: hidden; just in front of it: fading (soft against the scene, flakes landing)
    var soft = 1.0;
    let d = scene_depth(vec2<i32>(in.position.xy));
    if (d > 0.0 && p.look.w < 0.5) {
        let gap = in.flake_z - view_z(d);  // > 0 when the flake is in front
        if (gap <= 0.0) {
            discard;
        }
        soft = smoothstep(0.0, p.look.y, gap);
    }
    let shape = 1.0 - smoothstep(0.25, 1.0, r);
    return vec4(in.color.rgb, in.color.a * shape * soft);
}
