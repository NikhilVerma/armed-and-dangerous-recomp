// Shadow maps: matrices and shaders (vk_shadows.h). The device side is in vk_device.cpp (VulkanDevice::Shadows).
#include "vk_shadows.h"

#include "vk_fog.h"

#include <cmath>

namespace rhi::vulkan
{
Mat4 Multiply(const Mat4& a, const Mat4& b)
{
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            float s = 0;
            for (int k = 0; k < 4; ++k)
                s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

bool Invert(const Mat4& in, Mat4& out)
{
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            a[r][c] = in.m[r * 4 + c], a[r][c + 4] = r == c ? 1.0 : 0.0;
    for (int col = 0; col < 4; ++col)
    {
        int piv = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col]))
                piv = r;
        if (std::fabs(a[piv][col]) < 1e-12)
            return false;
        for (int c = 0; c < 8; ++c)
            std::swap(a[col][c], a[piv][c]);
        double inv = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c)
            a[col][c] *= inv;
        for (int r = 0; r < 4; ++r)
            if (r != col)
            {
                double f = a[r][col];
                for (int c = 0; c < 8; ++c)
                    a[r][c] -= f * a[col][c];
            }
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out.m[r * 4 + c] = static_cast<float>(a[r][c + 4]);
    return true;
}

Mat4 LightMatrix(const float centre[3], const float dir[3], float range, uint32_t size)
{
    // Light basis: forward f along the light, right r and up u perpendicular to it.
    float f[3] = {dir[0], dir[1], dir[2]};
    float len = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len < 1e-6f)
        f[0] = 0, f[1] = -1, f[2] = 0, len = 1;
    for (float& v : f)
        v /= len;
    float helper[3] = {0, 0, 1};
    if (std::fabs(f[2]) > 0.9f)
        helper[0] = 1, helper[2] = 0;
    float r[3] = {helper[1] * f[2] - helper[2] * f[1], helper[2] * f[0] - helper[0] * f[2],
                  helper[0] * f[1] - helper[1] * f[0]};
    float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (float& v : r)
        v /= rl;
    float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    // Light-space coordinates of the centre, snapped to texels (stable as the camera moves).
    float texel = 2.0f * range / static_cast<float>(size);
    float cx = r[0] * centre[0] + r[1] * centre[1] + r[2] * centre[2];
    float cy = u[0] * centre[0] + u[1] * centre[1] + u[2] * centre[2];
    float cz = f[0] * centre[0] + f[1] * centre[1] + f[2] * centre[2];
    cx = std::floor(cx / texel) * texel, cy = std::floor(cy / texel) * texel;
    // Orthographic: x, y in [-range, range] around the centre; depth 0..1 over [cz - 40 range, cz + 4 range]. Far
    // towards the sun so distant casters up-sun (mountains, cliffs) still cast: with 4 range they were clipped and
    // their shadows came and went as the box moved with the camera (measured: a large shadow popping in and out
    // as the player stepped back and forth). kShadowDepthSpan in vk_shadows.h.
    float depthNear = cz - 40.0f * range, depthFar = cz + 4.0f * range;
    Mat4 m{};
    for (int j = 0; j < 3; ++j)
    {
        m.m[0 * 4 + j] = r[j] / range;
        m.m[1 * 4 + j] = u[j] / range;
        m.m[2 * 4 + j] = f[j] / (depthFar - depthNear);
    }
    m.m[0 * 4 + 3] = -cx / range;
    m.m[1 * 4 + 3] = -cy / range;
    m.m[2 * 4 + 3] = -depthNear / (depthFar - depthNear);
    m.m[3 * 4 + 3] = 1.0f;
    return m;
}

std::string ShadowDepthFragmentGlsl()
{
    return "#version 450\n// Shadow map depth pass: depth only.\nvoid main() {}\n";
}

std::string ShadowApplyVertexGlsl()
{
    return R"(#version 450
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);  // full-screen triangle
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
}

std::string ShadowApplyFragmentGlsl()
{
    return std::string("#version 450\nstruct ModernFog\n{\n") + ModernFogMembers() + "};\n" + R"(
// Shadow map apply pass (vk_shadows.h): darkens lit surfaces that the shadow map says are occluded, with
// percentage-closer soft shadows (PCSS): a blocker search finds how far the occluder is above the surface, and
// the filter widens with that distance (hard where an object touches the ground, soft far from the caster),
// as an area light of angular size sunSize would make.
// Dual-source output: colour = dst * o1 + o (o1 per channel: shadows tinted by the sky light that fills them).
layout(location = 0, index = 0) out vec4 o;
layout(location = 0, index = 1) out vec4 o1;
layout(set = 0, binding = 0) uniform sampler2D depthTex;
layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;
layout(std140, set = 0, binding = 2) uniform Params
{
    mat4 camToLight;
    vec4 proj;      // _11 _22 _33 _34
    vec4 misc;      // _43, strength, texel, bias
    vec4 sunCam;    // xyz towards the sun (camera space), w: light-depth range in world units
    vec4 viewport;  // x y w h
    vec4 soft;      // x: tan(sun angular radius), y: shadow-map width in world units, z: max filter in texels
    vec4 fog;       // start, end (camera distances), enabled
    vec4 fogColor;
    ModernFog mf;
} p;
layout(set = 0, binding = 3) uniform sampler2D shadowDepth;  // the same map, raw depth (blocker search)

const vec2 kDisk[16] = vec2[](
    vec2(-0.942, -0.399), vec2(0.946, -0.769), vec2(-0.094, -0.929), vec2(0.345, 0.294),
    vec2(-0.916, 0.458), vec2(-0.815, -0.879), vec2(-0.383, 0.277), vec2(0.975, 0.756),
    vec2(0.443, -0.975), vec2(0.537, -0.474), vec2(-0.265, -0.419), vec2(0.792, 0.191),
    vec2(-0.242, 0.997), vec2(-0.814, 0.914), vec2(0.200, 0.786), vec2(0.144, -0.141));

// Camera-space position from the depth buffer (depth = (z _33 + _43) / (z _34), right-handed: z < 0 in front).
vec3 CamPos(vec2 pixel)
{
    float d = texelFetch(depthTex, ivec2(pixel), 0).r;
    vec2 ndc = vec2((pixel.x - p.viewport.x) / p.viewport.z * 2.0 - 1.0,
                    1.0 - (pixel.y - p.viewport.y) / p.viewport.w * 2.0);
    float den = d * p.proj.w - p.proj.z;
    float z = p.misc.x / (abs(den) < 1e-7 ? 1e-7 : den);
    return vec3(ndc.x * z * p.proj.w / p.proj.x, ndc.y * z * p.proj.w / p.proj.y, z);
}

float Hash(vec2 v)
{
    return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec2 pixel = gl_FragCoord.xy;
    if (pixel.x < p.viewport.x || pixel.y < p.viewport.y || pixel.x >= p.viewport.x + p.viewport.z ||
        pixel.y >= p.viewport.y + p.viewport.w)
        discard;
    float d = texelFetch(depthTex, ivec2(pixel), 0).r;
    if (d >= 0.99999)
        discard;  // sky / nothing drawn
    vec3 c = CamPos(pixel);
    // Surface normal from the depth buffer; faces turned away from the sun are already dark (game lighting).
    vec3 n = normalize(cross(dFdx(c), dFdy(c)));
    if (dot(n, c) > 0.0)
        n = -n;  // towards the camera
    float ndl = dot(n, normalize(p.sunCam.xyz));
    if (ndl <= 0.02)
        discard;
    vec4 l = p.camToLight * vec4(c, 1.0);
    vec3 s = l.xyz / l.w;
    vec2 uv = s.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || s.z <= 0.0 || s.z >= 1.0)
        discard;
    float bias = p.misc.w * (1.0 + 2.0 * (1.0 - ndl));
    float texel = p.misc.z;
    // Rotate the disk per pixel (noise instead of banding).
    float a = Hash(pixel) * 6.2831853;
    mat2 rot = mat2(cos(a), sin(a), -sin(a), cos(a));
    // Physically based penumbra (no fixed ceiling): an occluder at distance g above the receiver, lit by a sun
    // of angular radius r, blurs its shadow over a disc of radius g tan(r). A thin occluder far above (a tree top)
    // spreads over a wide disc and fades, as real shadows do; the old fixed search radius capped the blur and long
    // shadows became evenly blurred tubes. Two blocker searches: the first at a small radius finds nearby
    // occluders, the second over the penumbra their distance implies (largest occluder distances need the widest
    // search). The filter then averages comparison taps over that penumbra.
    float maxUv = p.soft.z * texel;  // a sanity limit only (about 25 world units at the default settings)
    float searchUv = 6.0 * texel;
    float blocker = 0.0;
    bool any = false;
    for (int pass = 0; pass < 2; ++pass)
    {
        float blockers = 0.0, sum = 0.0;
        for (int i = 0; i < 16; ++i)
        {
            float z = texture(shadowDepth, uv + rot * kDisk[i] * searchUv).r;
            if (z < s.z - bias)
            {
                sum += z;
                blockers += 1.0;
            }
        }
        if (blockers < 1.0)
        {
            if (pass == 0)
            {
                searchUv = maxUv * 0.25;  // nothing close by: look for a distant occluder's wide penumbra
                continue;
            }
            break;
        }
        any = true;
        blocker = sum / blockers;
        float g = (s.z - blocker) * p.sunCam.w;
        searchUv = clamp(g * p.soft.x / p.soft.y, 1.5 * texel, maxUv);
    }
    if (!any)
        discard;  // fully lit
    float gap = (s.z - blocker) * p.sunCam.w;
    float radiusUv = clamp(gap * p.soft.x / p.soft.y, texel, maxUv);
    // Filter: 32 taps on a golden-angle spiral (even coverage of the disc), rotated per pixel.
    float lit = 0.0;
    for (int i = 0; i < 32; ++i)
    {
        float rr = sqrt((float(i) + 0.5) / 32.0);
        float th = float(i) * 2.39996323 + a;
        lit += texture(shadowMap, vec3(uv + vec2(cos(th), sin(th)) * rr * radiusUv, s.z - bias));
    }
    lit /= 32.0;
    if (p.fog.w > 0.5)  // [render] shadow_debug=1: penumbra radius (texels / 32) as grey, red where blockers were found
    {
        o1 = vec4(0.0);
        o = vec4(min(radiusUv / texel / 32.0, 1.0), gap / 100.0, 0.0, 0.0);
        return;
    }
    vec2 edge = min(uv, 1.0 - uv);
    // Fade over the outer quarter of the map: its border moves with the camera, and a hard edge there read as
    // shadows popping in and out (measured: a large ground shadow cut off at the map's side).
    float fade = smoothstep(0.0, 0.25, min(edge.x, edge.y));
    float shadow = (1.0 - lit) * p.misc.y * smoothstep(0.02, 0.2, ndl) * fade;
    if (shadow <= 0.0)
        discard;
    // Under the fog (as the ambient occlusion pass): with F = C T + Fog (1 - T), darkening C by k gives
    // F (1 - k) + Fog (1 - T) k. T from the same fog the draws used.
    float f = 1.0;
    vec3 fc = p.fogColor.rgb;
    if (p.fog.z > 0.5)
    {
        if (p.mf.on.x > 0.5)
            )" + ModernFogGlsl("p.mf", "d", "f", "fc", "p.fogColor.rgb") + R"(
        else
            f = clamp(p.fog.y != p.fog.x ? (p.fog.y - abs(c.z)) / (p.fog.y - p.fog.x) : (abs(c.z) < p.fog.y ? 1.0 : 0.0), 0.0, 1.0);
    }
    // A shadow is not black: what reaches a shadowed surface is the light of the sky (blue, on snow) and the
    // scene's bounce. Full shadow multiplies the colour by that sky light, (1 - strength) tinted blue; partial
    // shadow (penumbra) blends towards it.
    vec3 skyLit = clamp((1.0 - p.misc.y) * vec3(0.82, 0.92, 1.18), 0.0, 1.0);
    vec3 k = mix(vec3(1.0), skyLit, shadow / max(p.misc.y, 1e-3));
    o1 = vec4(k, 1.0);
    o = vec4(fc * (1.0 - f) * (1.0 - k), 0.0);
}
)";
}
}  // namespace rhi::vulkan
