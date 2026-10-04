// GPU snowfall: shaders (vk_snow.h). The device side is VulkanDevice::Snow in vk_device.cpp.
#include "vk_snow.h"

namespace rhi::vulkan
{
namespace
{
const char* kBlock = R"(layout(std140, set = 0, binding = 1) uniform Params
{
    mat4 view;      // world -> camera
    vec4 proj;      // _11 _22 _33 _34
    vec4 proj2;     // _43, time, radius, box
    vec4 camPos;
    vec4 motion;    // wind x, wind z, fall, turbulence
    vec4 viewport;  // x y w h
    vec4 target;    // w h
} p;
)";
}  // namespace

std::string SnowVertexGlsl()
{
    return std::string("#version 450\n") + kBlock + R"(
layout(location = 0) out vec2 corner;
layout(location = 1) out float flakeZ;   // camera-space z of the flake (negative in front)
layout(location = 2) out float alpha;

vec3 Hash3(uint n)
{
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    uvec3 k = n * uvec3(n, n * 16807u, n * 48271u);
    return vec3(k & uvec3(0x7fffffffu)) / float(0x7fffffff);
}

void main()
{
    uint flake = uint(gl_InstanceIndex);
    vec3 h = Hash3(flake * 3u + 1u), g = Hash3(flake * 7u + 5u);
    float box = p.proj2.w, t = p.proj2.y;
    // Home position in a 2*box cube; each flake has its own fall speed and wobble.
    float speed = p.motion.z * (0.7 + 0.6 * g.x);
    vec3 w = vec3(h.x, h.y, h.z) * 2.0 * box;
    w.y -= speed * t;
    w.xz += vec2(p.motion.x, p.motion.y) * t * (0.8 + 0.4 * g.y);
    float phase = g.z * 6.2831853;
    w.x += sin(t * (0.9 + g.x) + phase) * p.motion.w;
    w.z += cos(t * (0.7 + g.y) + phase * 1.3) * p.motion.w;
    // Wrap into the box centred on the camera.
    vec3 rel = mod(w - p.camPos.xyz + box, 2.0 * box) - box;
    vec3 world = p.camPos.xyz + rel;
    vec3 c = (p.view * vec4(world, 1.0)).xyz;
    // Camera-facing quad in camera space.
    vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    corner = corners[gl_VertexIndex];
    float radius = p.proj2.z * (0.6 + 0.8 * g.x);
    c.xy += corner * radius;
    flakeZ = c.z;
    // Fade near the camera (no flakes stuck to the lens) and towards the box edge (no popping when wrapping).
    float dist = length(rel);
    alpha = smoothstep(1.5, 4.0, -c.z) * (1.0 - smoothstep(0.75 * box, box, dist));
    // D3D9 projection (row vectors): clip = (x _11, y _22, z _33 + _43, z _34); Vulkan y points down.
    vec4 clip = vec4(c.x * p.proj.x, c.y * p.proj.y, c.z * p.proj.z + p.proj2.x, c.z * p.proj.w);
    clip.y = -clip.y;
    gl_Position = c.z < -0.05 ? clip : vec4(0.0, 0.0, -1.0, 1.0);
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
    float shape = 1.0 - smoothstep(0.35, 1.0, r);
    o = vec4(vec3(0.95, 0.97, 1.0), 0.85 * shape * soft * alpha);
}
)";
}
}  // namespace rhi::vulkan
