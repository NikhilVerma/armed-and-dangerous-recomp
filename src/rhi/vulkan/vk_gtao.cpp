// Ambient occlusion: shader sources (vk_gtao.h). The device side is VulkanDevice::Ambient in vk_device.cpp.
#include "vk_gtao.h"

#include <regex>

#include "xegtao_embed.h"  // kXeGtaoH, kXeGtaoHlsli, kGtaoEntries (generated, hook/CMakeLists.txt)

namespace rhi::vulkan
{
namespace
{
// Removes every line that starts with `#include "name"` (the files are pasted in by GtaoHlsl instead).
std::string DropInclude(std::string s, const char* name)
{
    const std::string line = std::string("#include \"") + name + "\"";
    for (size_t at; (at = s.find(line)) != std::string::npos;)
        s.erase(at, line.size());
    return s;
}
}  // namespace

std::string GtaoHlsl()
{
    // XeGTAO options: 32-bit depths (no fp16 path), normals generated from depth inside the main pass, no bent
    // normals, the default (compiled-in) tuning constants.
    std::string s =
        "#define XE_GTAO_USE_HALF_FLOAT_PRECISION 0\n"
        "#define XE_GTAO_FP32_DEPTHS 1\n"
        "#define XE_GTAO_GENERATE_NORMALS_INPLACE 1\n"
        "#define XE_GTAO_USE_DEFAULT_CONSTANTS 1\n"
        // The two macros XeGTAO.hlsli takes from its sample framework (vaShaderCore.h, vaShared.hlsl).
        "#define VA_SATURATE saturate\n"
        "#define VA_PI (3.1415926535897932384626433832795)\n";
    s += kXeGtaoH;
    s += "\n";
    std::string hlsli = DropInclude(DropInclude(kXeGtaoHlsli, "vaShared.hlsl"), "XeGTAO.h");
    std::string entries = kGtaoEntries;
    const std::string marker = "#include \"XeGTAO.hlsli\"";
    size_t at = entries.find(marker);
    if (at != std::string::npos)
        entries.replace(at, marker.size(), hlsli);
    // glslang's HLSL front end does not take swizzles of numeric literals ("2.xx", "0.5.xxxx"), which XeGTAO uses;
    // they become constructors ("float2(2, 2)"). The vendored files stay unmodified.
    std::string out = s + entries, fixed;
    static const std::regex literal(R"((^|[^\w.])(\d+(?:\.\d+)?)\.(xxxx|xxx|xx)\b)");
    std::sregex_iterator it(out.begin(), out.end(), literal), end;
    size_t last = 0;
    for (; it != end; ++it)
    {
        const std::smatch& m = *it;
        size_t n = m[3].length();
        std::string v = m[2].str(), args = v;
        for (size_t i = 1; i < n; ++i)
            args += ", " + v;
        fixed += out.substr(last, m.position(0) - last) + m[1].str() + "float" + std::to_string(n) + "(" + args + ")";
        last = m.position(0) + m.length(0);
    }
    fixed += out.substr(last);
    return fixed;
}

std::string GtaoApplyVertexGlsl()
{
    return R"(#version 450
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
}

std::string GtaoApplyFragmentGlsl()
{
    return std::string("#version 450\nstruct ModernFog\n{\n") + ModernFogMembers() + "};\n" + R"(
layout(set = 0, binding = 0) uniform usampler2D aoTex;     // XeGTAO final term: visibility * 255
layout(set = 0, binding = 1) uniform sampler2D depthTex;   // scene depth 0..1
layout(std140, set = 0, binding = 2) uniform Params
{
    vec4 viewport;
    vec4 fog;       // start, end, enabled, AO strength
    vec4 fogColor;
    ModernFog mf;
    vec4 sun;       // camera space, w: sun share s (0: no ray-traced visibility)
    vec4 proj;      // _11 _22 _33 _34
    vec4 proj43;
} p;
layout(std430, set = 0, binding = 3) readonly buffer Vis { float v[]; } vis;  // RT sun visibility (rt_ipc.h)
layout(location = 0) out vec4 o;

// Output blends as  dst * o.a + o.rgb  (src ONE, dst SRC_ALPHA).
void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    ivec2 local = px - ivec2(p.viewport.xy);
    float d = texelFetch(depthTex, px, 0).r;
    if (d >= 0.99999 || any(lessThan(local, ivec2(0))) || any(greaterThanEqual(local, ivec2(p.viewport.zw))))
    {
        o = vec4(0.0, 0.0, 0.0, 1.0);  // sky / outside the scene: unchanged
        return;
    }
    float ao = float(texelFetch(aoTex, local, 0).r) / 255.0;
    // Multi-bounce (Jimenez et al. 2016, eq. 10), albedo 0.6 (snow and pale rock dominate the game's levels).
    const float albedo = 0.6;
    float ma = 2.0404 * albedo - 0.3324, mb = -4.7951 * albedo + 0.6417, mc = 2.7552 * albedo + 0.6903;
    ao = max(ao, ((ao * ma + mb) * ao + mc) * ao);
    ao = mix(1.0, ao, p.fog.w);
    if (p.sun.w > 0.0)
    {
        // Occlusion of the ambient light only. The shadow pass (vk_rt.cpp) scales the colour by (1 - s') + s' vis,
        // s' = s smoothstep(0.02, 0.2, n.l) the sun's share here; the light should be (1 - s') ao + s' vis, so this
        // pass scales by their ratio: full AO in shade and on surfaces facing away, the ambient share in sunlight.
        float den = d * p.proj.w - p.proj.z;
        float z = p.proj43.x / (abs(den) < 1e-7 ? 1e-7 : den);
        vec2 ndc = vec2(float(local.x) + 0.5, float(local.y) + 0.5) / p.viewport.zw * 2.0 - 1.0;
        vec3 c = vec3(ndc.x * z * p.proj.w / p.proj.x, -ndc.y * z * p.proj.w / p.proj.y, z);
        vec3 n = normalize(cross(dFdx(c), dFdy(c)));
        if (dot(n, -c) < 0.0)
            n = -n;
        float s = p.sun.w * smoothstep(0.02, 0.2, dot(n, normalize(p.sun.xyz)));
        float v = clamp(vis.v[local.y * int(p.viewport.z) + local.x], 0.0, 1.0);
        float lit = (1.0 - s) + s * v;
        ao = lit > 1e-4 ? ((1.0 - s) * ao + s * v) / lit : ao;
    }
    // The game's fog (D3D9 linear table fog on depth): F = C f + Fog (1 - f). Occlusion applies to C only:
    // C ao f + Fog (1 - f) = F ao + Fog (1 - f)(1 - ao).
    float f = 1.0;
    vec3 fc = p.fogColor.rgb;
    if (p.fog.z > 0.5)
    {
        if (p.mf.on.x > 0.5)
            )" + ModernFogGlsl("p.mf", "d", "f", "fc", "p.fogColor.rgb") + R"(
        else
        {
            // The game's linear fog on camera distance (z), from the depth through the projection.
            float den = d * p.mf.proj2.x - p.mf.proj.z;
            float z = abs(p.mf.proj.w / (abs(den) < 1e-7 ? 1e-7 : den));
            f = clamp(p.fog.y != p.fog.x ? (p.fog.y - z) / (p.fog.y - p.fog.x) : (z < p.fog.y ? 1.0 : 0.0), 0.0, 1.0);
        }
    }
    o = vec4(fc * (1.0 - f) * (1.0 - ao), ao);
}
)";
}
}  // namespace rhi::vulkan
