// Ray-traced sun shadows: shaders (vk_rt.h). The device side is VulkanDevice::RtShadows in vk_device.cpp.
#include "vk_rt.h"

namespace rhi::vulkan
{
std::string RtShadowVertexGlsl()
{
    return R"(#version 460
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
}

std::string RtShadowFragmentGlsl(bool remote)
{
    std::string head = remote ? "#version 460\n#define RT_REMOTE 1\n" : "#version 460\n#extension GL_EXT_ray_query : require\n";
    return head + "struct ModernFog\n{\n" + ModernFogMembers() + "};\n" + R"(
layout(set = 0, binding = 0) uniform sampler2D depthTex;
#ifdef RT_REMOTE
layout(std430, set = 0, binding = 1) readonly buffer Vis { float v[]; } vis;  // the helper's (rt_ipc.h)
#else
layout(set = 0, binding = 1) uniform accelerationStructureEXT scene;
#endif
layout(std140, set = 0, binding = 2) uniform Params
{
    vec4 camToWorld[3];
    vec4 proj;      // _11 _22 _33 _34
    vec4 misc;      // _43, strength, rays, frame
    vec4 sun;       // world direction towards the sun, w: tan(angular radius)
    vec4 viewport;
    vec4 fog;       // start, end, enabled, debug
    vec4 fogColor;
    ModernFog mf;
} p;
// Dual-source output (as the shadow-map pass): colour = dst * o1 + o.
layout(location = 0, index = 0) out vec4 o;
layout(location = 0, index = 1) out vec4 o1;

vec3 CamPos(vec2 pixel, float d)
{
    vec2 ndc = vec2((pixel.x - p.viewport.x) / p.viewport.z * 2.0 - 1.0,
                    1.0 - (pixel.y - p.viewport.y) / p.viewport.w * 2.0);
    float den = d * p.proj.w - p.proj.z;
    float z = p.misc.x / (abs(den) < 1e-7 ? 1e-7 : den);
    return vec3(ndc.x * z * p.proj.w / p.proj.x, ndc.y * z * p.proj.w / p.proj.y, z);
}

vec3 ToWorld(vec3 c)
{
    return vec3(dot(p.camToWorld[0], vec4(c, 1.0)), dot(p.camToWorld[1], vec4(c, 1.0)),
                dot(p.camToWorld[2], vec4(c, 1.0)));
}

float Hash(vec2 v)
{
    return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec2 pixel = gl_FragCoord.xy;
    float d = texelFetch(depthTex, ivec2(pixel), 0).r;
    if (d >= 0.99999)
        discard;  // sky
    vec3 c = CamPos(pixel, d);
    vec3 w = ToWorld(c);
    // Normal from the depth buffer (world space).
    vec3 n = normalize(cross(dFdx(w), dFdy(w)));
    vec3 toCam = ToWorld(vec3(0.0)) - w;
    if (dot(n, toCam) < 0.0)
        n = -n;
    vec3 L = normalize(p.sun.xyz);
    float ndl = dot(n, L);
    if (ndl <= 0.02)
        discard;  // facing away from the sun: the game's lighting has it dark already
    // Origin pushed off the surface, proportional to the distance (depth precision) and the slope.
    float dist = length(c);
    vec3 origin = w + n * (0.02 + dist * 0.0015) + L * 0.05;
    // Cone of directions over the sun's disc: an orthonormal basis around L, per-pixel rotated spiral.
    vec3 t = normalize(abs(L.z) < 0.9 ? cross(L, vec3(0.0, 0.0, 1.0)) : cross(L, vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(L, t);
    int rays = int(p.misc.z);
    float rot = Hash(pixel + p.misc.w * 7.31) * 6.2831853;
    float blocked = 0.0;
#ifdef RT_REMOTE
    ivec2 local = ivec2(pixel - p.viewport.xy);
    blocked = (1.0 - vis.v[local.y * int(p.viewport.z) + local.x]) * float(rays);
    rays = max(rays, 1);
    if (false)
#endif
    for (int i = 0; i < rays; ++i)
    {
        float r = sqrt((float(i) + 0.5) / float(rays)) * p.sun.w;
        float a = float(i) * 2.39996323 + rot;
        vec3 dir = normalize(L + (t * cos(a) + b * sin(a)) * r);
#ifndef RT_REMOTE
        rayQueryEXT q;
        rayQueryInitializeEXT(q, scene, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xff, origin, 0.0,
                              dir, 1e5);
        while (rayQueryProceedEXT(q))
        {
        }
        if (rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT)
            blocked += 1.0;
#endif
    }
    float shadow = blocked / float(rays) * p.misc.y * smoothstep(0.02, 0.2, ndl);
    if (p.fog.w > 0.5)
    {
        o1 = vec4(0.0);
        o = vec4(vec3(1.0 - blocked / float(rays)), 0.0);  // [render] shadow_debug=1: visibility
        return;
    }
    if (shadow <= 0.0)
        discard;
    // Under the fog, sky-tinted (vk_shadows.cpp).
    float f = 1.0;
    vec3 fc = p.fogColor.rgb;
    if (p.fog.z > 0.5)
    {
        if (p.mf.on.x > 0.5)
            )" + ModernFogGlsl("p.mf", "d", "f", "fc", "p.fogColor.rgb") + R"(
        else
            f = clamp(p.fog.y != p.fog.x ? (p.fog.y - abs(c.z)) / (p.fog.y - p.fog.x) : 1.0, 0.0, 1.0);
    }
    vec3 skyLit = clamp((1.0 - p.misc.y) * vec3(0.82, 0.92, 1.18), 0.0, 1.0);
    vec3 k = mix(vec3(1.0), skyLit, shadow / max(p.misc.y, 1e-3));
    o1 = vec4(k, 1.0);
    o = vec4(fc * (1.0 - f) * (1.0 - k), 0.0);
}
)";
}
std::string RtDepthComputeGlsl()
{
    return R"(#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D depthTex;
layout(std430, set = 0, binding = 1) writeonly buffer Z { float z[]; } outz;
layout(std140, set = 0, binding = 2) uniform Params
{
    vec4 viewport;  // x y w h
    vec4 proj;      // _33 _34 _43
} p;
void main()
{
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(p.viewport.z) || px.y >= int(p.viewport.w))
        return;
    float d = texelFetch(depthTex, px + ivec2(p.viewport.xy), 0).r;
    outz.z[px.y * int(p.viewport.z) + px.x] = d;  // raw depth (rt_ipc.h kDepth)
}
)";
}
}  // namespace rhi::vulkan
