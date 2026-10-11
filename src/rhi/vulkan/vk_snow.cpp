// GPU snowfall: shaders (vk_snow.h). The device side is VulkanDevice::Snow in vk_device.cpp.
//
// A particle simulation, one compute thread per flake (state in a storage buffer: position, velocity):
// The game's world is Z-up (camera up axis measured in world space: (0, 0, 1)); the ground plane is x, y.
//   - flakes are born in the sky above the camera and fall at their terminal speed (larger flakes a little faster)
//   - the air moves: a base wind whose strength gusts in waves travelling downwind, plus swirling turbulence; a
//     flake follows the air with a short lag (drag relaxation), so it drifts, eddies and tumbles instead of
//     sliding along a straight line
//   - a flake that falls into a visible surface (the scene's depth buffer) has landed, one that leaves the bottom
//     of the volume has fallen out: both are born again in the sky; sideways the volume wraps around the camera
//     (what the wind carries out on one side, it carries in on the other)
// At the first frame and after a camera jump (level start, teleport) the volume is filled at once.
#include "vk_snow.h"

namespace rhi::vulkan
{
namespace
{
const char* kBlock = R"(layout(std140, set = 0, binding = 1) uniform Params
{
    mat4 view;      // world -> camera
    vec4 proj;      // _11 _22 _33 _34
    vec4 proj2;     // _43, time, flake radius, horizontal radius of the volume
    vec4 camPos;    // camera position, w: 1 = fill the volume this frame
    vec4 motion;    // wind x, wind z, terminal fall speed, turbulence
    vec4 viewport;  // x y w h
    vec4 target;    // w h, flake count, frame time (s)
    vec4 color;     // rgb, opacity
    vec4 sim;       // sky height above the camera, depth below it, drag time (s), streak exposure (s)
} p;
struct Flake
{
    vec4 pos;  // xyz world, w: size (0..1)
    vec4 vel;  // xyz world units per second, w: per-flake seed
};
)";

const char* kHash = R"(
vec3 Hash3(uint n)
{
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    uvec3 k = n * uvec3(n, n * 16807u, n * 48271u);
    return vec3(k & uvec3(0x7fffffffu)) / float(0x7fffffff);
}
)";
}  // namespace

std::string SnowComputeGlsl()
{
    return std::string("#version 450\n") + kBlock + kHash + R"(
layout(local_size_x = 64) in;
layout(std430, set = 0, binding = 2) buffer Flakes { Flake f[]; };
layout(set = 0, binding = 0) uniform sampler2D depthTex;

// The air's velocity at x: base wind with travelling gusts, plus swirls (sums of sines, cheap and smooth).
vec3 Air(vec3 x, float t)
{
    vec2 wind = p.motion.xy;
    vec2 dir = normalize(wind + vec2(1e-4));
    float along = dot(x.xy, dir);
    float gust = 1.0 + 0.7 * sin(t * 0.31 - along * 0.06) + 0.3 * sin(t * 0.83 - along * 0.17 + x.z * 0.1);
    vec3 a = vec3(wind.x, wind.y, 0.0) * max(gust, 0.1);
    float k = p.motion.w;
    vec3 q = x * 0.23;
    a.x += k * (sin(q.z * 2.1 + t * 0.7) + 0.6 * cos(q.y * 1.7 - t * 0.5));
    a.y += k * (cos(q.z * 1.9 - t * 0.6) + 0.6 * sin(q.x * 1.5 + t * 0.4));
    a.z += k * 0.45 * (sin(q.x * 1.3 + q.y * 1.1 + t * 0.9));
    return a;
}

void Born(uint i, bool anywhere, float t)
{
    vec3 h = Hash3(i * 3u + uint(t * 997.0) * 7919u + 1u);
    float R = p.proj2.w;
    vec3 pos;
    pos.xy = p.camPos.xy + (h.xy * 2.0 - 1.0) * R;
    pos.z = anywhere ? p.camPos.z - p.sim.y + h.z * (p.sim.x + p.sim.y) : p.camPos.z + p.sim.x * (0.85 + 0.15 * h.z);
    float size = f[i].pos.w;
    f[i].pos = vec4(pos, size);
    f[i].vel.xyz = Air(pos, t) - vec3(0.0, 0.0, p.motion.z);
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (float(i) >= p.target.z)
        return;
    float t = p.proj2.y, dt = p.target.w;
    if (p.camPos.w > 0.5)
    {
        vec3 s = Hash3(i * 11u + 3u);
        f[i].pos.w = s.x * s.x * s.x;  // sizes skewed small, a few large aggregates
        f[i].vel.w = s.y;
        Born(i, true, t);
        return;
    }
    if (dt <= 0.0)
        return;  // paused
    vec3 pos = f[i].pos.xyz, vel = f[i].vel.xyz;
    float size = f[i].pos.w, seed = f[i].vel.w;
    float terminal = p.motion.z * (0.8 + 0.25 * seed + 0.5 * size);
    vec3 want = Air(pos, t) - vec3(0.0, 0.0, terminal);
    // Drag: the flake relaxes towards the air's velocity (minus its fall) with time constant tau.
    float tau = p.sim.z * (0.6 + 0.8 * size);
    vel += (want - vel) * (1.0 - exp(-dt / tau));
    // Tumbling: a small spin around the fall line, per flake.
    float spin = t * (1.5 + 2.0 * seed) + seed * 40.0;
    vec3 flutter = vec3(cos(spin), sin(spin), 0.0) * 0.25 * (1.0 - size);
    pos += (vel + flutter) * dt;

    // Landed: crossed into a visible surface (camera space, against the depth buffer).
    vec3 c = (p.view * vec4(pos, 1.0)).xyz;
    if (c.z < -0.1)
    {
        vec2 ndc = vec2(c.x * p.proj.x, -c.y * p.proj.y) / (c.z * p.proj.w);
        vec2 px = p.viewport.xy + (ndc * 0.5 + 0.5) * p.viewport.zw;
        if (all(greaterThanEqual(px, p.viewport.xy)) && all(lessThan(px, p.viewport.xy + p.viewport.zw)))
        {
            float d = texelFetch(depthTex, ivec2(px), 0).r;
            if (d < 0.99999)
            {
                float den = d * p.proj.w - p.proj.z;
                float sceneZ = p.proj2.x / (abs(den) < 1e-7 ? 1e-7 : den);
                float gap = c.z - sceneZ;  // < 0: behind the surface
                if (gap < 0.0 && gap > -0.6)
                {
                    f[i].pos.w = size;
                    Born(i, false, t);
                    return;
                }
            }
        }
    }
    // Fell out of the bottom, or carried far above the sky: born again.
    if (pos.z < p.camPos.z - p.sim.y || pos.z > p.camPos.z + p.sim.x + 4.0)
    {
        Born(i, false, t);
        return;
    }
    // Sideways the volume wraps around the camera.
    float R = p.proj2.w;
    vec2 rel = pos.xy - p.camPos.xy;
    pos.xy -= 2.0 * R * floor((rel + R) / (2.0 * R));
    f[i].pos.xyz = pos;
    f[i].vel.xyz = vel;
}
)";
}

std::string SnowVertexGlsl()
{
    return std::string("#version 450\n") + kBlock + R"(
layout(std430, set = 0, binding = 2) readonly buffer Flakes { Flake f[]; };
layout(location = 0) out vec2 corner;
layout(location = 1) out float flakeZ;   // camera-space z of the flake (negative in front)
layout(location = 2) out float alpha;
layout(location = 3) out float shade;

void main()
{
    Flake fl = f[gl_InstanceIndex];
    vec3 world = fl.pos.xyz;
    float size = fl.pos.w;
    vec3 c = (p.view * vec4(world, 1.0)).xyz;
    // Where the flake was a moment ago: the quad stretches between the two (motion streak).
    vec3 c0 = (p.view * vec4(world - fl.vel.xyz * p.sim.w, 1.0)).xyz;
    float radius = p.proj2.z * (0.5 + 2.0 * size);
    // Cap the apparent size only for flakes very close to the lens (a disc covering the screen). The old cap of
    // 0.0022 x distance gave every flake within ~20 units the same ~2-pixel size, so the snow read as a flat
    // screen-space overlay with no depth (the player, 2026-10-11); now near flakes are visibly larger.
    radius = min(radius, max(-c.z, 0.0) * 0.012);
    vec2 d = c.xy - c0.xy;
    float len = length(d);
    vec2 ax = len > 1e-5 ? d / len : vec2(0.0, 1.0);
    vec2 ay = vec2(-ax.y, ax.x);
    vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    corner = corners[gl_VertexIndex];
    vec3 mid = 0.5 * (c + c0);
    float half_len = radius + 0.5 * len;
    vec3 q = vec3(mid.xy + ax * corner.x * half_len + ay * corner.y * radius, mid.z);
    flakeZ = c.z;
    // Fade near the camera, at the sides of the volume (wrapping) and just below the sky (birth).
    vec2 rel = world.xy - p.camPos.xy;
    float edge = max(abs(rel.x), abs(rel.y));
    alpha = smoothstep(1.0, 2.5, -c.z) * (1.0 - smoothstep(0.8 * p.proj2.w, p.proj2.w, edge)) *
            (1.0 - smoothstep(p.camPos.z + 0.7 * p.sim.x, p.camPos.z + 0.85 * p.sim.x, world.z));
    // A streak spreads the flake's light over its length.
    alpha *= radius / half_len * 0.5 + 0.5;
    // Distant flakes: at least ~0.8 pixel wide (sub-pixel sprites flicker), with their light scaled down to the true
    // covered area, and an aerial fade with distance.
    float focal = p.proj.x * 0.5 * p.viewport.z;  // pixels per unit at distance 1
    float px = radius * focal / max(-c.z, 0.1);
    if (px < 1.2)
    {
        alpha *= px / 1.2;
        radius = 1.2 * max(-c.z, 0.1) / focal;
        half_len = radius + 0.5 * len;
        q = vec3(mid.xy + ax * corner.x * half_len + ay * corner.y * radius, mid.z);
    }
    alpha *= mix(1.0, 0.7, smoothstep(10.0, 140.0, -c.z));
    shade = 0.82 + 0.18 * fl.vel.w;
    vec4 clip = vec4(q.x * p.proj.x, q.y * p.proj.y, q.z * p.proj.z + p.proj2.x, q.z * p.proj.w);
    clip.y = -clip.y;
    gl_Position = c.z < -0.05 && c0.z < -0.05 ? clip : vec4(0.0, 0.0, -1.0, 1.0);
}
)";
}

std::string SnowFragmentGlsl()
{
    return std::string("#version 450\n") + kBlock + R"(
layout(set = 0, binding = 0) uniform sampler2D depthTex;
layout(location = 0) in vec2 corner;
layout(location = 1) in float flakeZ;
layout(location = 2) in float alpha;
layout(location = 3) in float shade;
layout(location = 0) out vec4 o;

void main()
{
    float r = dot(corner, corner);
    if (r > 1.0 || alpha <= 0.0)
        discard;
    // Scene depth at this pixel as camera-space z (depth = (z _33 + _43) / (z _34)).
    float d = texelFetch(depthTex, ivec2(gl_FragCoord.xy), 0).r;
    float sceneZ = -1e9;
    if (d < 0.99999)
    {
        float den = d * p.proj.w - p.proj.z;
        sceneZ = p.proj2.x / (abs(den) < 1e-7 ? 1e-7 : den);
    }
    // Behind a surface: hidden; just in front of it: fade (soft particles, flakes landing).
    float gap = flakeZ - sceneZ;  // > 0 when the flake is in front
    if (gap <= 0.0)
        discard;
    float soft = smoothstep(0.0, 0.3, gap);
    float shape = 1.0 - smoothstep(0.25, 1.0, r);
    o = vec4(p.color.rgb * shade, p.color.a * shape * soft * alpha);
}
)";
}
}  // namespace rhi::vulkan
