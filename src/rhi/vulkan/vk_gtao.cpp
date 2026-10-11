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

std::string GtaoHlsl(float thickness, int slices)
{
    // XeGTAO options: 32-bit depths (no fp16 path), normals generated from depth inside the main pass, no bent
    // normals, tuning constants from GTAOSettings ([render] ao_radius / ao_power / ao_falloff).
    std::string s =
        "#define XE_GTAO_USE_HALF_FLOAT_PRECISION 0\n"
        "#define XE_GTAO_FP32_DEPTHS 1\n"
        "#define XE_GTAO_GENERATE_NORMALS_INPLACE 1\n"
        "#define XE_GTAO_USE_DEFAULT_CONSTANTS 0\n"  // radius, falloff and power from GTAOSettings
        // The two macros XeGTAO.hlsli takes from its sample framework (vaShaderCore.h, vaShared.hlsl).
        "#define VA_SATURATE saturate\n"
        "#define VA_PI (3.1415926535897932384626433832795)\n";
    s += kXeGtaoH;
    s += "\n";
    std::string hlsli = DropInclude(DropInclude(kXeGtaoHlsli, "vaShared.hlsl"), "XeGTAO.h");
    // Remaster thickness limit ([render] ao_thickness, world units; 0 = XeGTAO as shipped). The depth buffer holds
    // only front surfaces, so a character reads as an infinitely thick board and darkens the ground well behind it
    // (the player: "renders the player as if standing in front of a board"). A sample more than `thickness` closer
    // to the camera than the centre is faded out over another `thickness`: the ground at a character's feet still
    // sees the feet (small depth step), the ground behind the character no longer sees the character.
    if (thickness > 0.0f)
    {
        const std::string anchor = "lpfloat weight1         = saturate( falloffBase1 * falloffMul + falloffAdd );";
        size_t at = hlsli.find(anchor);
        if (at != std::string::npos)
        {
            std::string t = std::to_string(thickness);
            hlsli.insert(at + anchor.size(),
                         "\n                weight0 *= saturate(2.0 - (-sampleDelta0.z) / " + t + ");"
                         "\n                weight1 *= saturate(2.0 - (-sampleDelta1.z) / " + t + ");");
        }
    }
    std::string entries = std::string(kGtaoEntries) + "\n" + kAfterlightEntries;  // afterlight.hlsl ([render] afterlight)
    const std::string marker = "#include \"XeGTAO.hlsli\"";
    size_t at = entries.find(marker);
    if (at != std::string::npos)
        entries.replace(at, marker.size(), hlsli);
    // glslang's HLSL front end does not take swizzles of numeric literals ("2.xx", "0.5.xxxx"), which XeGTAO uses;
    // they become constructors ("float2(2, 2)"). The vendored files stay unmodified.
    // [render] ao_slices: directions per pixel in the main pass (the sample's "high" is 3, "ultra" 9): fewer
    // directions leave more noise for the denoiser and the temporal AA to remove (the player: "so pixellated").
    {
        const std::string call = "XeGTAO_MainPass(pixCoord, 3, 3,";
        size_t at = entries.find(call);
        if (at != std::string::npos)
            entries.replace(at, call.size(), "XeGTAO_MainPass(pixCoord, " + std::to_string(slices) + ", 3,");
    }
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

std::string GtaoApplyFragmentGlsl(bool afterlight)
{
    return std::string("#version 450\n") + (afterlight ? "#define AFTERLIGHT 1\n" : "") +
           "struct ModernFog\n{\n" + ModernFogMembers() + "};\n" + R"(
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
    vec4 material;  // x: share of the occlusion kept on snow-white surfaces, y: debug view, z: contact share
    vec4 gi;        // Afterlight: x reality gain (bounce gain), y max relight stops, z ambient fill
} p;
layout(std430, set = 0, binding = 3) readonly buffer Vis { float v[]; } vis;  // RT sun visibility (rt_ipc.h)
layout(set = 0, binding = 4) uniform sampler2D matTex;  // motion target: z AO weight, w albedo (vk_abi.h)
#ifdef AFTERLIGHT
layout(set = 0, binding = 5) uniform sampler2D bounceTex;  // gathered radiance (rgb) and its solid-angle weight (a)
layout(set = 0, binding = 6) uniform sampler2D radTex;     // source radiance, linear, 5 MIPs; a = emitter energy
// Dual-source output: colour = dst * o1 + o (per channel: the bounce tints).
layout(location = 0, index = 0) out vec4 o;
layout(location = 0, index = 1) out vec4 o1;
vec3 LinearToSrgb(vec3 c)
{
    return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
#else
layout(location = 0) out vec4 o;
#endif

// Output blends as  dst * o.a + o.rgb  (src ONE, dst SRC_ALPHA); with AFTERLIGHT  dst * o1 + o.
void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    ivec2 local = px - ivec2(p.viewport.xy);
    float d = texelFetch(depthTex, px, 0).r;
    if (d >= 0.99999 || any(lessThan(local, ivec2(0))) || any(greaterThanEqual(local, ivec2(p.viewport.zw))))
    {
        o = vec4(0.0, 0.0, 0.0, 1.0);  // sky / outside the scene: unchanged
#ifdef AFTERLIGHT
        o = vec4(0.0);
        o1 = vec4(1.0);
#endif
        return;
    }
    float ao = float(texelFetch(aoTex, local, 0).r) / 255.0;
    float rawAo = ao;
    // The opaque surface's material (vk_abi.h): weight = the share of its colour lit by ambient light, albedo
    // (-1 when the shader is not known). Pixels no opaque draw wrote (weight 0) are left alone.
    vec4 mat = texelFetch(matTex, px, 0);
    float weight = clamp(mat.z, 0.0, 1.0);
    bool known = mat.w >= 0.0;
    // Multi-bounce (Jimenez et al. 2016, eq. 10) with the surface's albedo (0.6 when unknown).
    float albedo = known ? clamp(mat.w, 0.0, 1.0) : 0.6;
    float ma = 2.0404 * albedo - 0.3324, mb = -4.7951 * albedo + 0.6417, mc = 2.7552 * albedo + 0.6903;
    ao = max(ao, ((ao * ma + mb) * ao + mc) * ao);
    // Snow scatters light through itself and is lit by the bright snow around it: occlusion mostly disappears on
    // snow-white surfaces (albedo 0.65 .. 0.8 keeps p.material.x of it).
    float snow = known ? smoothstep(0.65, 0.8, albedo) : 0.0;
    ao = mix(1.0, ao, p.fog.w * mix(1.0, p.material.x, snow));
    if (known)
        ao = mix(1.0, ao, weight);  // the ambient share only: the sun term, glow and highlights are not occluded
    else if (p.sun.w > 0.0)
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
    // Contact occlusion (remaster, [render] ao_contact): where the raw term is clearly occluded (feet on the ground,
    // objects resting on snow) keep that share of it whatever the snow and sunlight limits above leave, so
    // characters sit on sunlit snow instead of floating (the player: "their feet should darken the ground").
    float contact = p.material.z * p.fog.w * smoothstep(0.9, 0.45, rawAo);
    ao = min(ao, mix(1.0, rawAo, contact));
#ifdef AFTERLIGHT
    // Afterlight composite (the player's SSLD GTAO, SSLD_RelightDebug in export/SSLD_GTAO.fx), in linear light: the
    // gathered bounce is compared with the local lighting prior; light above the prior is new transport and
    // becomes a bounded positive fill (stops), AO is relieved where the gather agrees, the bounce's chroma tints
    // neutral receivers, and detected light sources are protected. Omitted: exposure state (authored-light relief),
    // history. The modulation is then turned into a factor on the stored (display-encoded) colour.
    vec3 giK = vec3(1.0);
    {
        const vec3 L = vec3(0.2126, 0.7152, 0.0722);
        const float lf = 0.01;
        vec2 uv = (vec2(local) + 0.5) / p.viewport.zw;
        vec4 bounce = textureLod(bounceTex, uv, 0.0);
        vec3 average = bounce.rgb / max(bounce.a, 1e-6);
        vec2 reach = 64.0 / p.viewport.zw;
        vec3 prior = textureLod(radTex, uv, 4.0).rgb * 4.0;
        prior += (textureLod(radTex, uv + vec2(-reach.x, 0.0), 4.0).rgb + textureLod(radTex, uv + vec2(reach.x, 0.0), 4.0).rgb +
                  textureLod(radTex, uv + vec2(0.0, -reach.y), 4.0).rgb + textureLod(radTex, uv + vec2(0.0, reach.y), 4.0).rgb) * 2.0;
        prior += textureLod(radTex, uv - reach, 4.0).rgb + textureLod(radTex, uv + reach, 4.0).rgb +
                 textureLod(radTex, uv + vec2(reach.x, -reach.y), 4.0).rgb + textureLod(radTex, uv + vec2(-reach.x, reach.y), 4.0).rgb;
        prior *= 1.0 / 16.0;
        float realityGain = min(p.gi.x, 4.0);
        float trust = bounce.a / (bounce.a + 0.02);
        vec3 diffuse = mix(prior, average, trust);
        float averageLuma = dot(diffuse, L), priorLuma = dot(prior, L);
        float priorRelativeStops = log2(max(averageLuma, lf) / max(priorLuma, lf));
        vec4 r0 = texelFetch(radTex, local, 0);
        vec3 sceneLinear = r0.rgb;
        float sceneLuma = dot(sceneLinear, L);
        float receiverLightingLuma = dot(textureLod(radTex, uv, 2.0).rgb, L);
        float transportEvidence = smoothstep(0.0, 0.25, clamp(priorRelativeStops - 0.05, 0.0, 1.0));
        float evidenceConfidence = bounce.a / (bounce.a + 0.01);
        float lightAgreement = smoothstep(-1.0, 0.0, priorRelativeStops);
        float aoSuppression = evidenceConfidence * lightAgreement * transportEvidence;
        float gatheredMean = dot(bounce.rgb, L) / max(bounce.a, 1e-6);
        float transportedRatio = bounce.a / (bounce.a + 1.0) * max(gatheredMean - priorLuma, 0.0) / max(priorLuma, lf);
        float authoredDeficit = max(priorLuma - receiverLightingLuma, 0.0) / max(priorLuma, lf) * p.gi.z;
        float deficitGain = min(realityGain, 1.0);
        float resolverEvidence = deficitGain == realityGain ? realityGain * (8.0 * transportedRatio + authoredDeficit)
                                                            : realityGain * 8.0 * transportedRatio + deficitGain * authoredDeficit;
        float relax = clamp(max(realityGain, 1.0) - 1.0, 0.0, 1.0);
        float support = pow(clamp(evidenceConfidence, 0.0, 1.0), 0.25 * (1.0 - relax)) *
                        mix(1.0, clamp(evidenceConfidence / 1e-6, 0.0, 1.0), relax);
        float headroom = 1.0 - smoothstep(0.2, 1.0, sceneLuma);
        float dramaticT = clamp((realityGain - 2.0) / 2.0, 0.0, 1.0);
        float ceiling = min(p.gi.y * realityGain, mix(1.0, 2.0, dramaticT));
        float residualStops = min(log2(1.0 + resolverEvidence) * support, ceiling) * headroom;
        float reliefStops = min(0.5 * realityGain, mix(1.0, 1.5, dramaticT));
        ao = min(ao * exp2(reliefStops * aoSuppression), 1.0);
        vec3 chromaStops = clamp(log2(max(diffuse, vec3(lf)) / max(averageLuma, lf)), -1.0, 1.0);
        float rmax = max(sceneLinear.r, max(sceneLinear.g, sceneLinear.b));
        float sat = (rmax - min(sceneLinear.r, min(sceneLinear.g, sceneLinear.b))) / max(rmax, lf);
        float chromaAmount = min(realityGain * 0.5, 1.0) * evidenceConfidence * lightAgreement *
                             (1.0 - smoothstep(0.10, 0.35, sat)) * transportEvidence;
        vec3 tint = exp2(chromaStops * chromaAmount);
        tint *= sceneLuma / max(dot(sceneLinear * tint, L), 1e-6);
        vec3 modulation = tint * exp2(residualStops);
        float protect = clamp(r0.a / max(sceneLuma, 1e-6), 0.0, 1.0);
        modulation = mix(modulation, vec3(1.0), protect);
        giK = LinearToSrgb(sceneLinear * modulation) / max(LinearToSrgb(sceneLinear), vec3(1e-4));
        // [render] ao_debug 5: the gathered bounce (trusted share); 6: fill stops, AO relief, protection
        int gdbg = int(p.material.y + 0.5);
        if (gdbg == 5 || gdbg == 6)
        {
            o = vec4(gdbg == 5 ? LinearToSrgb(average * trust) : vec3(residualStops, aoSuppression, protect), 0.0);
            o1 = vec4(0.0);
            return;
        }
    }
#endif
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
#ifdef AFTERLIGHT
    // The bounce lights the surface under the fog as the occlusion darkens it: C k f + Fog (1 - f).
    o1 = vec4(mix(vec3(1.0), giK, f) * ao, 0.0);
    o.a = 0.0;
#endif
    // [render] ao_debug: 1 XeGTAO's term, 2 the factor applied, 3 the AO weight, 4 the albedo (grey, replaces the
    // frame).
    int dbg = int(p.material.y + 0.5);
    if (dbg != 0)
    {
        o = vec4(vec3(dbg == 1 ? rawAo : dbg == 2 ? ao : dbg == 3 ? weight : (known ? albedo : 0.0)), 0.0);
#ifdef AFTERLIGHT
        o1 = vec4(0.0);
#endif
    }
}
)";
}
}  // namespace rhi::vulkan
