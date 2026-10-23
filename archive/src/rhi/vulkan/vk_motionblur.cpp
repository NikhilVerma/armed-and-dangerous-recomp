#include "vk_motionblur.h"

namespace rhi::vulkan
{
namespace
{
const char* kHeader = R"(#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(std140, set = 0, binding = 0) uniform Params
{
    mat4 camToPrevCam;
    vec4 proj;      // _11 _22 _33 _34
    vec4 prevProj;
    vec4 misc;      // _43, previous _43, exposure / frame time (shutter speed), max radius (pixels)
    vec4 size;      // width, height, tiles x, tiles y
} p;
layout(set = 0, binding = 1) uniform sampler2D depthTex;   // scene depth 0..1 (D3D)
layout(set = 0, binding = 2) uniform sampler2D motionTex;  // xy = prevUv - curUv (all motion), vk_abi.h
layout(set = 0, binding = 3) uniform sampler2D colorTex;   // the anti-aliased frame
layout(set = 0, binding = 4, rg16f) uniform writeonly image2D velOut;
layout(set = 0, binding = 5) uniform sampler2D velIn;      // object velocity (pixels)
layout(set = 0, binding = 6, rg16f) uniform writeonly image2D tileOut;
layout(set = 0, binding = 7) uniform sampler2D tileIn;
layout(set = 0, binding = 8, rgba16f) uniform writeonly image2D colorOut;

// Camera-space z from depth (right-handed, z < 0 in front; depth = (z _33 + _43) / (z _34)).
float ViewZ(float d)
{
    float den = d * p.proj.w - p.proj.z;
    return p.misc.x / (abs(den) < 1e-7 ? 1e-7 : den);
}
)";

// Pass 0: object velocity = full motion - camera motion, in pixels, scaled by the shutter and clamped.
const char* kVelocity = R"(
void main()
{
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(p.size.x) || px.y >= int(p.size.y))
        return;
    vec4 m = texelFetch(motionTex, px, 0);
    float d = texelFetch(depthTex, px, 0).r;
    vec2 v = vec2(0.0);
    // Nothing drew motion here (cleared to 0) or sky: no object motion.
    if (d < 0.99999 && m != vec4(0.0))
    {
        vec2 uv = (vec2(px) + 0.5) / p.size.xy;
        vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        float z = ViewZ(d);
        vec3 c = vec3(ndc.x * z * p.proj.w / p.proj.x, ndc.y * z * p.proj.w / p.proj.y, z);
        vec3 pc = (p.camToPrevCam * vec4(c, 1.0)).xyz;
        vec4 clip = vec4(pc.x * p.prevProj.x, pc.y * p.prevProj.y, pc.z * p.prevProj.z + p.misc.y, pc.z * p.prevProj.w);
        vec2 pndc = clip.xy / (abs(clip.w) < 1e-7 ? 1e-7 : clip.w);
        vec2 prevUv = vec2(pndc.x * 0.5 + 0.5, 0.5 - pndc.y * 0.5);
        vec2 camMotion = prevUv - uv;
        // Only surfaces that move in the world blur (static scenery stays sharp while the camera pans), and they blur
        // by their motion ON SCREEN, as a real camera sees it: a runner the camera follows stays sharp except for his
        // swinging limbs (blurring by the world motion smeared the tracked player, 2026-10-12).
        vec2 world = (m.xy - camMotion) * p.size.xy;  // pixels per frame from the object's own motion
        vec2 screen = m.xy * p.size.xy;              // on screen (object and camera)
        float worldPx = length(world);
        float moving = (isnan(worldPx) || isinf(worldPx)) ? 0.0 : smoothstep(0.75, 2.0, worldPx);
        // The shorter of the two: a runner the camera follows (small on screen) and a character while the camera
        // turns (its screen motion is the camera's) both stay sharp; only real object motion blurs (the player:
        // "the character still blurs when moving the camera on their edges").
        v = (dot(screen, screen) < dot(world, world) ? screen : world) * p.misc.z * moving;
        if (any(isnan(v)) || any(isinf(v)))
            v = vec2(0.0);
        float len = length(v);
        if (len < 0.5)
            v = vec2(0.0);
        else if (len > p.misc.w)
            v *= p.misc.w / len;
    }
    imageStore(velOut, px, vec4(v, 0.0, 0.0));
}
)";

// Pass 1: the longest velocity in each tile.
const char* kTileMax = R"(
void main()
{
    ivec2 t = ivec2(gl_GlobalInvocationID.xy);
    if (t.x >= int(p.size.z) || t.y >= int(p.size.w))
        return;
    vec2 best = vec2(0.0);
    float bestLen = 0.0;
    ivec2 lim = ivec2(p.size.xy) - 1;
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x)
        {
            vec2 v = texelFetch(velIn, min(t * TILE + ivec2(x, y), lim), 0).xy;
            float l = dot(v, v);
            if (l > bestLen)
                best = v, bestLen = l;
        }
    imageStore(tileOut, t, vec4(best, 0.0, 0.0));
}
)";

// Pass 2: the longest tile velocity in the 3x3 neighbourhood (motion reaching into this tile from next to it).
const char* kNeighbourMax = R"(
void main()
{
    ivec2 t = ivec2(gl_GlobalInvocationID.xy);
    if (t.x >= int(p.size.z) || t.y >= int(p.size.w))
        return;
    vec2 best = vec2(0.0);
    float bestLen = 0.0;
    ivec2 lim = ivec2(p.size.zw) - 1;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            vec2 v = texelFetch(tileIn, clamp(t + ivec2(x, y), ivec2(0), lim), 0).xy;
            float l = dot(v, v);
            if (l > bestLen)
                best = v, bestLen = l;
        }
    imageStore(tileOut, t, vec4(best, 0.0, 0.0));
}
)";

// Pass 3: reconstruction (McGuire et al. 2012): samples along the neighbourhood's dominant velocity, each weighted
// by whether it is in front of / behind this pixel and whether its own (or this pixel's) blur reaches here.
const char* kReconstruct = R"(
float Cone(float dist, float v) { return clamp(1.0 - dist / max(v, 1e-3), 0.0, 1.0); }
float Cylinder(float dist, float v) { return 1.0 - smoothstep(0.95 * v, 1.05 * v, dist); }
float SoftDepth(float za, float zb) { return clamp(1.0 - (za - zb) / max(0.02 * min(za, zb), 0.05), 0.0, 1.0); }
float Hash(vec2 v) { return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.5453); }

void main()
{
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(p.size.x) || px.y >= int(p.size.y))
        return;
    vec4 cx = texelFetch(colorTex, px, 0);
    vec2 vn = texelFetch(tileIn, min(px / TILE, ivec2(p.size.zw) - 1), 0).xy;
    float lenN = length(vn);
    if (lenN < 0.5)
    {
        imageStore(colorOut, px, cx);
        return;
    }
    ivec2 lim = ivec2(p.size.xy) - 1;
    vec2 vx = texelFetch(velIn, px, 0).xy;
    float lenX = max(length(vx), 0.5);
    float zx = -ViewZ(texelFetch(depthTex, px, 0).r);
    float wsum = 1.0 / lenX;
    vec3 sum = cx.rgb * wsum;
    const int N = 15;
    float j = Hash(vec2(px)) - 0.5;
    for (int i = 0; i < N; ++i)
    {
        if (i == N / 2)
            continue;
        float t = mix(-1.0, 1.0, (float(i) + j * 0.95 + 0.5) / float(N)) * 0.5;
        ivec2 py = clamp(ivec2(vec2(px) + vn * t + 0.5), ivec2(0), lim);
        float dist = length(vec2(py - px));
        float zy = -ViewZ(texelFetch(depthTex, py, 0).r);
        float lenY = max(length(texelFetch(velIn, py, 0).xy), 0.5);
        float f = SoftDepth(zx, zy);  // y in front of x
        float b = SoftDepth(zy, zx);  // y behind x
        float w = f * Cone(dist, lenY) + b * Cone(dist, lenX) + Cylinder(dist, lenY) * Cylinder(dist, lenX) * 2.0;
        sum += texelFetch(colorTex, py, 0).rgb * w;
        wsum += w;
    }
    imageStore(colorOut, px, vec4(sum / wsum, cx.a));
}
)";
}  // namespace

std::string MotionBlurGlsl(int pass)
{
    std::string s = std::string(kHeader) + "#define TILE " + std::to_string(kMotionBlurTile) + "\n";
    switch (pass)
    {
    case 0: return s + kVelocity;
    case 1: return s + kTileMax;
    case 2: return s + kNeighbourMax;
    default: return s + kReconstruct;
    }
}
}  // namespace rhi::vulkan
