// Direct3D 9 fixed-function pipeline as generated GLSL 450 (Vulkan backend). Conventions: vk_abi.h.
//
// FixedFunctionVertex: transform (incl. vertex blending), lighting, texture coordinate generation and transform,
// vertex fog. FixedFunctionPixel: the texture stage cascade, specular add, then FinalPixelDefine's fog and alpha
// test. Both also carry the motion vectors of vk_abi.h (PrevFrame, varyings 11/12, output location 1); the motion
// output is written by the pixel shader itself, not by D3D9_FINAL. The rules follow the D3D9 documentation ("Fixed Function Vertex Processing", "Mathematics of Lighting",
// "Texture Blending", "Fog", "Alpha Testing State"); each rule is commented where it is implemented. Where the
// documentation is silent, the choice is labelled "guess" with the reason.
//
// Contracts this file adds on top of vk_abi.h (the headers are fixed; see the notes at each use):
//  * FFStage::transformFlags is a uint8_t, but D3DTTFF_PROJECTED is 0x100. The device must store PROJECTED as
//    kFFTransformProjected (0x80) in that byte: transformFlags = (flags & 0xff) | (flags & 0x100 ? 0x80 : 0).
//  * FFUniforms::lightPosition / lightDirection are taken in world space, exactly as D3DLIGHT9 gives them (D3D9
//    lights are world space); the vertex shader moves them to camera space with the view matrix.
//  * D3D9_FINAL (FinalPixelDefine) is one #define line; a macro cannot declare anything, so it uses names the
//    pixel shader must declare: the FixedFunction block with instance name `ff` and the vk_abi.h member names
//    (it reads ff.fogColor, ff.fogParams, ff.alphaRef, ff.projection) and the fog varying
//    `layout(location = 10) in float vFog;`. vk_shader_translate.cpp declares both in every pixel shader, and so
//    does FixedFunctionPixel. Locals it introduces start with d3d9_ and live inside do { } while (false).
#include <cstddef>
#include <string>

#include "vk_shaders.h"

#include "vk_fog.h"

namespace rhi::vulkan
{
// D3DTTFF_PROJECTED as stored in FFStage::transformFlags (the real value, 0x100, does not fit in a byte).
constexpr uint8_t kFFTransformProjected = 0x80;

namespace
{
// The GLSL mirror of FFUniforms below relies on these std140 offsets (mat4 = 64 bytes, vec4 arrays stride 16).
static_assert(offsetof(FFUniforms, view) == 256);
static_assert(offsetof(FFUniforms, projection) == 320);
static_assert(offsetof(FFUniforms, texture) == 384);
static_assert(offsetof(FFUniforms, halfPixel) == 896);
static_assert(offsetof(FFUniforms, alphaRef) == 976);
static_assert(offsetof(FFUniforms, lightDiffuse) == 1072);
static_assert(offsetof(FFUniforms, textureStageConstant) == 1968);
static_assert(offsetof(FFUniforms, mf) == 2368);
static_assert(offsetof(FFUniforms, soft) == 2496);
static_assert(sizeof(FFUniforms) == 2512);

std::string N(int v) { return std::to_string(v); }

// The FixedFunction uniform block, member for member like FFUniforms. D3D9 stores matrices row-major with row
// vectors (v' = v * M). Read into a column-major GLSL mat4 the rows become columns, so the GLSL matrix is M^T and
// `G * v` computes exactly D3D9's `v * M`. Products chain the same way: v * W * V * P == P' * (V' * (W' * v)).
std::string UniformBlock(const char* blockName, const char* instance)
{
    return std::string("#ifndef MODERN_FOG_STRUCT\n#define MODERN_FOG_STRUCT\nstruct ModernFog\n{\n") +
           ModernFogMembers() + "};\n#endif\n" +
           "layout(std140, set = 0, binding = " + N(kBindingFixedFunction) + ") uniform " + blockName +
           "\n{\n"
           "    mat4 world[4];\n    mat4 view;\n    mat4 projection;\n    mat4 texture[8];\n"
           "    vec4 halfPixel;\n    vec4 viewportSize;\n    vec4 textureFactor;\n    vec4 fogColor;\n"
           "    vec4 fogParams;\n    vec4 alphaRef;\n    vec4 ambient;\n    vec4 materialDiffuse;\n"
           "    vec4 materialAmbient;\n    vec4 materialSpecular;\n    vec4 materialEmissive;\n"
           "    vec4 lightDiffuse[8];\n    vec4 lightSpecular[8];\n    vec4 lightAmbient[8];\n"
           "    vec4 lightPosition[8];\n    vec4 lightDirection[8];\n    vec4 lightAttenuation[8];\n"
           "    vec4 lightSpot[8];\n    vec4 textureStageConstant[8];\n    vec4 bumpEnvMat[8];\n"
           "    vec4 bumpEnvLum[8];\n    vec4 tessParams;\n    ModernFog mf;\n    vec4 soft;\n} " +
           instance + ";\n";
}

// The PrevFrame uniform block (vk_abi.h "Motion vectors"), member for member like PrevUniforms. Same matrix
// convention as UniformBlock. Only the fixed-function vertex shader reads it (world, view, projection).
static_assert(offsetof(PrevUniforms, view) == 256);
static_assert(offsetof(PrevUniforms, projection) == 320);
static_assert(offsetof(PrevUniforms, vsConstants) == 384);
static_assert(sizeof(PrevUniforms) == 384 + kVsConstantCount * 16);
std::string PrevFrameBlock()
{
    return "layout(std140, set = 0, binding = " + N(kBindingPrevFrame) +
           ") uniform PrevFrame\n{\n    mat4 world[4];\n    mat4 view;\n    mat4 projection;\n    vec4 vsConstants[" +
           N(kVsConstantCount) + "];\n} prevFrame;\n";
}

// Motion vector varyings and output (vk_abi.h "Motion vectors"): varyings 11 (current clip position before the
// half-pixel offset) and 12 (previous clip position); fragment output 1 = prevUv - curUv with
// uv = (ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5), ndc = clip.xy / clip.w. Not flat: they interpolate like positions.
const char* kMotionPixelDecl = "layout(location = 11) in vec4 vCurClip;\n"
                               "layout(location = 12) in vec4 vPrevClip;\n"
                               "layout(location = 1) out vec4 fragMotion;\n";
const char* kMotionPixelWrite =
    "    {\n"
    "        vec2 curNdc = vCurClip.xy / vCurClip.w, prevNdc = vPrevClip.xy / vPrevClip.w;\n"
    "        vec2 curUv = vec2(curNdc.x * 0.5 + 0.5, 0.5 - curNdc.y * 0.5);\n"
    "        vec2 prevUv = vec2(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5);\n"
    "        fragMotion = vec4(prevUv - curUv, 1.0, -1.0);  // motion; AO weight 1, albedo unknown (vk_abi.h)\n"
    "    }\n";

bool Has(const FFKey& key, BYTE usage, BYTE index)
{
    uint32_t loc = VertexLocation(usage, index);
    return loc < 32 && (key.vertexMask & (1u << loc)) != 0;
}

// Stages in use: up to the first whose colorOp is D3DTOP_DISABLE (D3D9: "the first stage set to DISABLE ends the
// cascade; later stages are ignored"). A zero (unset) op is treated the same way.
int ActiveStages(const FFKey& key)
{
    int n = 0;
    while (n < 8 && key.stage[n].colorOp != 0 && key.stage[n].colorOp != D3DTOP_DISABLE)
        ++n;
    return n;
}

// Fog factor of distance `d` (GLSL expression) for a D3DFOGMODE, per the D3D9 "Fog Formulas":
//   LINEAR f = (end - d) / (end - start),  EXP f = 1 / e^(d * density),  EXP2 f = 1 / e^((d * density)^2),
// clamped to [0, 1]; 1 means no fog. `p` is the fogParams vec4 (start, end, density).
std::string FogFactor(uint8_t mode, const std::string& d, const std::string& p)
{
    switch (mode)
    {
    case D3DFOG_LINEAR:
        // end == start: a step at `end` instead of a division by zero (guess; D3D9 does not define it).
        return "clamp((" + p + ".y - " + p + ".x) != 0.0 ? (" + p + ".y - (" + d + ")) / (" + p + ".y - " + p +
               ".x) : ((" + d + ") < " + p + ".y ? 1.0 : 0.0), 0.0, 1.0)";
    case D3DFOG_EXP: return "clamp(exp(-(" + d + ") * " + p + ".z), 0.0, 1.0)";
    case D3DFOG_EXP2:
        return "clamp(exp(-((" + d + ") * " + p + ".z) * ((" + d + ") * " + p + ".z)), 0.0, 1.0)";
    default: return "1.0";
    }
}

bool UsesVertexFog(const FFKey& key) { return key.fogEnable && key.fogTableMode == D3DFOG_NONE; }
bool UsesTableFog(const FFKey& key)
{
    return key.fogEnable && (key.fogTableMode == D3DFOG_LINEAR || key.fogTableMode == D3DFOG_EXP ||
                             key.fogTableMode == D3DFOG_EXP2);
}

// Statements of D3D9_FINAL (one line, `c` is the macro argument).
std::string FinalBody(const FFKey& key)
{
    std::string s = "do { ";
    if (UsesTableFog(key))
    {
        // Pixel ("table") fog from depth. D3D9 uses eye-space w ("w-based fog") only when the device reports
        // D3DPRASTERCAPS_WFOG and the projection is W-friendly; otherwise the 0..1 depth z. The device decides and
        // passes it as fogParams.w (1 = w); measured: the game's level fog matches D3D9 only with z on this driver.
        // gl_FragCoord.w = 1/w.
        s += "float d3d9_d = ff.fogParams.w > 0.5 ? 1.0 / gl_FragCoord.w : gl_FragCoord.z; ";
        s += "float d3d9_f = " + FogFactor(key.fogTableMode, "d3d9_d", "ff.fogParams") + "; ";
    }
    else if (UsesVertexFog(key))
    {
        // Vertex fog: the per-vertex factor, interpolated (varying 10, written by the vertex stage).
        s += "float d3d9_f = clamp(vFog, 0.0, 1.0); ";
    }
    if (UsesTableFog(key) || UsesVertexFog(key))
    {
        // D3D9 "Fog Blending": C = f * Ci + (1 - f) * Cf on RGB; alpha is not fogged.
        // Remaster: modern height fog (vk_fog.h) when ff.mf.on.x is set.
        s += "vec3 d3d9_fc = ff.fogColor.rgb; if (ff.mf.on.x > 0.5) " +
             ModernFogGlsl("ff.mf", "gl_FragCoord.z", "d3d9_f", "d3d9_fc", "ff.fogColor.rgb") + " ";
        s += "c.rgb = mix(d3d9_fc, c.rgb, d3d9_f); ";
    }
    if (key.alphaTestEnable && key.alphaFunc != D3DCMP_ALWAYS)
    {
        // Alpha test: the pixel is kept when (alpha FUNC ref) holds. D3DRS_ALPHAREF is an 8-bit value and the
        // hardware compares at that precision, so both sides are quantised to 0..255 (D3DCMP_EQUAL works).
        const char* cmp = nullptr;
        switch (key.alphaFunc)
        {
        case D3DCMP_LESS: cmp = "<"; break;
        case D3DCMP_EQUAL: cmp = "=="; break;
        case D3DCMP_LESSEQUAL: cmp = "<="; break;
        case D3DCMP_GREATER: cmp = ">"; break;
        case D3DCMP_NOTEQUAL: cmp = "!="; break;
        case D3DCMP_GREATEREQUAL: cmp = ">="; break;
        default: break;  // D3DCMP_NEVER (and invalid values): nothing passes
        }
        if (cmp)
            s += std::string("float d3d9_a = floor(clamp(c.a, 0.0, 1.0) * 255.0 + 0.5); "
                             "float d3d9_r = floor(clamp(ff.alphaRef.x, 0.0, 1.0) * 255.0 + 0.5); "
                             "if (!(d3d9_a ") + cmp + " d3d9_r)) discard; ";
        else
            s += "discard; ";
    }
    if (key.softParticle)
    {
        // Remaster soft particles (vk_abi.h): after fog and alpha test (the test keeps the game's coverage), the
        // pixel's contribution fades with its camera-depth distance to the opaque surface behind it.
        // Camera depth |z| = |_43 / (d _34 - _33)| (either handedness; the denominator kept off zero).
        s += "float d3d9_sd = texelFetch(d3d9_sceneDepth, ivec2(gl_FragCoord.xy), 0).r; "
             "float d3d9_ds = d3d9_sd * ff.soft.z - ff.soft.x, d3d9_dp = gl_FragCoord.z * ff.soft.z - ff.soft.x; "
             "float d3d9_zs = abs(ff.soft.y / (abs(d3d9_ds) < 1e-7 ? 1e-7 : d3d9_ds)); "
             "float d3d9_zp = abs(ff.soft.y / (abs(d3d9_dp) < 1e-7 ? 1e-7 : d3d9_dp)); "
             "float d3d9_sf = clamp((d3d9_zs - d3d9_zp) * ff.soft.w, 0.0, 1.0); ";
        switch (key.softParticle)
        {
        case kSoftAlpha: s += "c.a *= d3d9_sf; "; break;
        case kSoftScale: s += "c *= d3d9_sf; "; break;
        case kSoftModulate: s += "c.rgb = mix(vec3(1.0), c.rgb, d3d9_sf); "; break;
        case kSoftModulate2: s += "c.rgb = mix(vec3(0.5), c.rgb, d3d9_sf); "; break;
        default: break;
        }
    }
    return s + "} while (false)";
}

}  // namespace

// Soft particle variants also declare the scene depth they read (binding 5, vk_abi.h); the declaration goes on its
// own line before the macro (both are inserted right after #version).
std::string FinalPixelDefine(const FFKey& key)
{
    std::string head = key.softParticle ? "layout(set = 0, binding = " + N(kBindingSceneDepth) +
                                              ") uniform sampler2D d3d9_sceneDepth;\n"
                                        : std::string();
    return head + "#define D3D9_FINAL(c) " + FinalBody(key);
}

// --- Vertex ------------------------------------------------------------------------------------------------------

namespace
{
// Material colour source (D3DRS_*MATERIALSOURCE). D3D9: with D3DRS_COLORVERTEX TRUE, D3DMCS_COLOR1/COLOR2 take the
// vertex diffuse/specular colour; if the vertex lacks that colour, the material colour is used.
std::string MaterialSource(const FFKey& key, uint8_t source, const char* material)
{
    if (key.colorVertex && source == D3DMCS_COLOR1 && Has(key, D3DDECLUSAGE_COLOR, 0))
        return "in_color0";
    if (key.colorVertex && source == D3DMCS_COLOR2 && Has(key, D3DDECLUSAGE_COLOR, 1))
        return "in_color1";
    return std::string("ff.") + material;
}

// D3D9 lighting ("Mathematics of Lighting"), camera space, for lights 0..lightCount-1:
//   Ambient  = Ca * (Ga + sum(Atten_i * Spot_i * La_i))
//   Diffuse  = Cd * sum(Ld_i * max(N.L_i, 0) * Atten_i * Spot_i)
//   Specular = Cs * sum(Ls_i * (N.H_i)^P * Atten_i * Spot_i),  H = norm(L + V), V = norm(-P) with LOCALVIEWER,
//              else the fixed direction (0,0,-1) toward a viewer at infinity (camera looks down +z)
//   Emissive = Ce
//   color0 = saturate(Ambient + Diffuse + Emissive), alpha = Cd.a;  color1 = saturate(Specular), alpha = Cs.a
// Point/spot: Atten = 1 / (att0 + att1 d + att2 d^2), zero beyond the range. Spot: rho = dot(-L, dir); 1 inside
// the umbra (rho > cos(theta/2)), 0 outside the penumbra (rho <= cos(phi/2)), else
// ((rho - cos(phi/2)) / (cos(theta/2) - cos(phi/2)))^falloff. Directional: L = -dir, Atten = Spot = 1.
std::string Lighting(const FFKey& key)
{
    int count = key.lightCount > 8 ? 8 : key.lightCount;
    std::string s;
    s += "    vec4 matDiffuse = " + MaterialSource(key, key.diffuseSource, "materialDiffuse") + ";\n";
    s += "    vec4 matAmbient = " + MaterialSource(key, key.ambientSource, "materialAmbient") + ";\n";
    s += "    vec4 matSpecular = " + MaterialSource(key, key.specularSource, "materialSpecular") + ";\n";
    s += "    vec4 matEmissive = " + MaterialSource(key, key.emissiveSource, "materialEmissive") + ";\n";
    s += "    vec3 sumAmbient = vec3(0.0), sumDiffuse = vec3(0.0), sumSpecular = vec3(0.0);\n";
    s += "    float power = ff.materialEmissive.w;  // D3DMATERIAL9::Power (vk_abi.h)\n";
    s += std::string("    vec3 toViewer = ") +
         (key.localViewer ? "normalize(-eyePos.xyz);\n" : "vec3(0.0, 0.0, -1.0);\n");
    s += "    for (int i = 0; i < " + N(count) + "; ++i)\n    {\n";
    s += "        int type = int(ff.lightDirection[i].w + 0.5);  // 1 point, 2 spot, 3 directional\n"
         "        vec3 L;\n"
         "        float atten = 1.0, spot = 1.0;\n"
         "        if (type == 3)\n"
         "            L = -normalize(mat3(ff.view) * ff.lightDirection[i].xyz);\n"
         "        else\n"
         "        {\n"
         "            vec3 lightPos = (ff.view * vec4(ff.lightPosition[i].xyz, 1.0)).xyz;\n"
         "            vec3 toLight = lightPos - eyePos.xyz;\n"
         "            float d = length(toLight);\n"
         "            L = d > 0.0 ? toLight / d : vec3(0.0, 0.0, -1.0);\n"
         "            vec4 a = ff.lightAttenuation[i];\n"
         "            float denom = a.x + a.y * d + a.z * d * d;\n"
         "            atten = d > ff.lightPosition[i].w ? 0.0 : (denom > 0.0 ? 1.0 / denom : 1.0);\n"
         "            if (type == 2)\n"
         "            {\n"
         "                float rho = dot(-L, normalize(mat3(ff.view) * ff.lightDirection[i].xyz));\n"
         "                float cosTheta = ff.lightSpot[i].x, cosPhi = ff.lightSpot[i].y;\n"
         "                if (rho > cosTheta)\n"
         "                    spot = 1.0;\n"
         "                else if (rho <= cosPhi)\n"
         "                    spot = 0.0;\n"
         "                else\n"
         "                    spot = pow((rho - cosPhi) / (cosTheta - cosPhi), a.w);\n"
         "            }\n"
         "        }\n"
         "        float k = atten * spot;\n"
         "        sumAmbient += ff.lightAmbient[i].rgb * k;\n"
         "        float NdotL = max(dot(normal, L), 0.0);\n"
         "        sumDiffuse += ff.lightDiffuse[i].rgb * NdotL * k;\n";
    if (key.specularEnable)
        // Specular only where the light reaches the front face (N.L > 0), and N.H > 0 so pow() is defined.
        s += "        float NdotH = dot(normal, normalize(L + toViewer));\n"
             "        if (NdotL > 0.0 && NdotH > 0.0)\n"
             "            sumSpecular += ff.lightSpecular[i].rgb * pow(NdotH, power) * k;\n";
    s += "    }\n";
    s += "    vs_color0 = vec4(clamp(matEmissive.rgb + matAmbient.rgb * (ff.ambient.rgb + sumAmbient) + "
         "matDiffuse.rgb * sumDiffuse, 0.0, 1.0), clamp(matDiffuse.a, 0.0, 1.0));\n";
    if (key.specularEnable)
        s += "    vs_color1 = vec4(clamp(matSpecular.rgb * sumSpecular, 0.0, 1.0), clamp(matSpecular.a, 0.0, 1.0));\n";
    else  // D3DRS_SPECULARENABLE FALSE: no specular lighting term
        s += "    vs_color1 = vec4(0.0);\n";
    return s;
}

// Texture coordinates for output i (= texture stage i; D3D9 computes one set per stage).
std::string TexCoord(const FFKey& key, int i, bool pretransformed)
{
    const FFStage& st = key.stage[i];
    std::string out = "vs_tc" + N(i);
    std::string raw, padded;  // padded: the vector the texture matrix multiplies
    switch (st.texCoordGen)
    {
    case 0:  // D3DTSS_TCI_PASSTHRU: the vertex's TEXCOORD<texCoordIndex>
    {
        int n = st.texCoordIndex & 7;
        if (!Has(key, D3DDECLUSAGE_TEXCOORD, (BYTE)n))
        {
            raw = padded = "vec4(0.0, 0.0, 0.0, 1.0)";  // guess: missing coordinates read as Vulkan's default
            break;
        }
        raw = "in_tc" + N(n);
        // D3D9 extends a coordinate with fewer than 4 components as (.., 1, 0, ..): the first missing component
        // is 1 and the rest 0, so the texture matrix row after the last supplied one acts as the translation
        // (a 2D (u, v) multiplies as (u, v, 1, 0)).
        switch (key.vertexTexCoordSize[n])
        {
        case 1: padded = "vec4(" + raw + ".x, 1.0, 0.0, 0.0)"; break;
        case 2: padded = "vec4(" + raw + ".xy, 1.0, 0.0)"; break;
        case 3: padded = "vec4(" + raw + ".xyz, 1.0)"; break;
        default: padded = raw; break;
        }
        break;
    }
    case D3DTSS_TCI_CAMERASPACENORMAL >> 16: raw = padded = "vec4(normal, 1.0)"; break;
    case D3DTSS_TCI_CAMERASPACEPOSITION >> 16: raw = padded = "vec4(eyePos.xyz, 1.0)"; break;
    case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR >> 16:
        // R = 2 (E.N) N - E with E toward the viewer, i.e. reflect(incident, N) with the incident direction from
        // the eye: the vertex direction with LOCALVIEWER, else the view axis (0, 0, 1).
        raw = padded = "vec4(reflected, 1.0)";
        break;
    case D3DTSS_TCI_SPHEREMAP >> 16:
        // Sphere map from the reflection vector (the OpenGL GL_SPHERE_MAP formula, which D3D9 shares; guess).
        raw = padded = "vec4(reflected.xy / (2.0 * length(vec3(reflected.xy, reflected.z + 1.0))) + 0.5, 0.0, 1.0)";
        break;
    default: raw = padded = "vec4(0.0, 0.0, 0.0, 1.0)"; break;
    }
    if (pretransformed && st.texCoordGen != 0)
        raw = padded = "vec4(0.0, 0.0, 0.0, 1.0)";  // no camera space for POSITIONT vertices
    int count = st.transformFlags & 7;  // D3DTTFF_COUNT1..4
    if (count >= 1 && count <= 4)
        // D3DTSS_TEXTURETRANSFORMFLAGS COUNTn: the coordinates are multiplied by D3DTS_TEXTURE<i> (row vector)
        // and the first n components go to the rasterizer (PROJECTED divides by the n-th; done per pixel).
        return "    " + out + " = ff.texture[" + N(i) + "] * " + padded + ";\n";
    return "    " + out + " = " + raw + ";\n";  // D3DTTFF_DISABLE: passed through unchanged
}
}  // namespace

GeneratedShader FixedFunctionVertex(const FFKey& key)
{
    const bool pretransformed = Has(key, D3DDECLUSAGE_POSITIONT, 0);
    const bool hasNormal = Has(key, D3DDECLUSAGE_NORMAL, 0);
    const bool hasColor0 = Has(key, D3DDECLUSAGE_COLOR, 0), hasColor1 = Has(key, D3DDECLUSAGE_COLOR, 1);
    const int stages = ActiveStages(key);
    const char* flat = key.shadeFlat ? "flat " : "";

    std::string s = "#version 450\n// D3D9 fixed-function vertex processing (vk_fixed_function.cpp)\n";
    s += UniformBlock("FixedFunction", "ff");
    s += PrevFrameBlock();
    // Inputs: every attribute in the layout, as vec4. Vulkan fills missing components with (0, 0, 0, 1), which
    // is D3D9's rule for positions and colours too.
    auto input = [&](BYTE usage, BYTE index, const std::string& name) {
        if (Has(key, usage, index))
            s += "layout(location = " + N(VertexLocation(usage, index)) + ") in vec4 " + name + ";\n";
    };
    input(D3DDECLUSAGE_POSITION, 0, "in_position");
    input(D3DDECLUSAGE_POSITIONT, 0, "in_positionT");
    input(D3DDECLUSAGE_BLENDWEIGHT, 0, "in_blendWeight");
    input(D3DDECLUSAGE_BLENDINDICES, 0, "in_blendIndices");
    input(D3DDECLUSAGE_NORMAL, 0, "in_normal");
    input(D3DDECLUSAGE_PSIZE, 0, "in_pointSize");
    for (int n = 0; n < 8; ++n)
        input(D3DDECLUSAGE_TEXCOORD, (BYTE)n, "in_tc" + N(n));
    input(D3DDECLUSAGE_COLOR, 0, "in_color0");
    input(D3DDECLUSAGE_COLOR, 1, "in_color1");
    input(D3DDECLUSAGE_FOG, 0, "in_fog");
    // Outputs: the varying locations of vk_shaders.h (0..7 texcoords, 8/9 colours, 10 fog), always all written so
    // any pixel shader can pair with this one.
    for (int i = 0; i < 8; ++i)
        s += "layout(location = " + N(i) + ") out vec4 vs_tc" + N(i) + ";\n";
    s += std::string("layout(location = 8) ") + flat + "out vec4 vs_color0;\n";
    s += std::string("layout(location = 9) ") + flat + "out vec4 vs_color1;\n";
    s += "layout(location = 10) out float vs_fog;\n";
    s += "layout(location = 11) out vec4 vCurClip;   // motion vectors (vk_abi.h)\n";
    s += "layout(location = 12) out vec4 vPrevClip;\n";
    s += kCaptureGlsl;  // scene vertex capture (vk_abi.h)
    s += "\n// Inverse transpose of a world-view matrix's upper 3x3: D3D9 transforms normals by it.\n"
         "mat3 NormalMatrix(mat4 m) { return transpose(inverse(mat3(m))); }\n\n";
    s += "void main()\n{\n";
    s += "    vec4 eyePos = vec4(0.0, 0.0, 0.0, 1.0);\n    vec3 normal = vec3(0.0);\n";

    if (pretransformed)
    {
        // D3DDECLUSAGE_POSITIONT (D3DFVF_XYZRHW): x, y in render-target pixels, z the viewport depth, w = 1/w
        // (RHW). Back to clip space: ndc.x = 2x/W - 1, ndc.y = 1 - 2y/H, ndc.z = (z - MinZ)/(MaxZ - MinZ), then
        // times w = 1/RHW so interpolation stays perspective-correct. No transform, lighting or clipping applies.
        // Assumes the viewport origin is (0, 0) (FFUniforms has no viewport X/Y).
        s += "    vec4 p = in_positionT;\n"
             "    float w = p.w != 0.0 ? 1.0 / p.w : 1.0;\n"
             "    vec2 size = max(ff.viewportSize.xy, vec2(1.0));\n"
             "    float depthRange = ff.viewportSize.w - ff.viewportSize.z;\n"
             "    float z = depthRange != 0.0 ? (p.z - ff.viewportSize.z) / depthRange : p.z;\n"
             "    gl_Position = vec4((2.0 * p.x / size.x - 1.0) * w, (1.0 - 2.0 * p.y / size.y) * w, z * w, w);\n";
        // Screen-space vertices have no previous transform: no motion (previous = current).
        s += "    vCurClip = gl_Position;\n    vPrevClip = gl_Position;\n";
    }
    else
    {
        s += std::string("    vec4 position = ") +
             (Has(key, D3DDECLUSAGE_POSITION, 0) ? "in_position" : "vec4(0.0, 0.0, 0.0, 1.0)") + ";\n";
        if (hasNormal)
            s += "    vec3 inNormal = in_normal.xyz;\n";
        // Vertex blending (D3DRS_VERTEXBLEND = D3DVBF_1WEIGHTS..3WEIGHTS): n weights from BLENDWEIGHT, n + 1
        // world matrices, the last weight 1 - sum(others). Indexed (D3DRS_INDEXEDVERTEXBLENDENABLE): matrix k is
        // D3DTS_WORLDMATRIX(BLENDINDICES[k]) (only 4 matrices exist in FFUniforms, so indices are clamped to 0..3),
        // else matrix k is D3DTS_WORLDMATRIX(k). Positions blend as sum(w_k * v * W_k * V); normals likewise with
        // the inverse transpose of each W_k * V. D3DVBF_TWEENING and DISABLE use world[0] only.
        int weights = (key.vertexBlend >= 1 && key.vertexBlend <= 3 && Has(key, D3DDECLUSAGE_BLENDWEIGHT, 0))
                          ? key.vertexBlend
                          : 0;
        bool indexed = key.indexedVertexBlend && Has(key, D3DDECLUSAGE_BLENDINDICES, 0);
        if (weights == 0 && !indexed)
        {
            s += "    mat4 worldView = ff.view * ff.world[0];\n"
                 "    eyePos = worldView * position;\n"
                 "    vec4 prevEyePos = (prevFrame.view * prevFrame.world[0]) * position;\n";
            if (hasNormal)
                s += "    normal = NormalMatrix(worldView) * inNormal;\n";
        }
        else
        {
            s += "    float weight[4] = float[4](0.0, 0.0, 0.0, 0.0);\n    float weightSum = 0.0;\n";
            for (int k = 0; k < weights; ++k)
                s += "    weight[" + N(k) + "] = in_blendWeight[" + N(k) + "];\n    weightSum += weight[" + N(k) +
                     "];\n";
            s += "    weight[" + N(weights) + "] = 1.0 - weightSum;\n";
            s += "    eyePos = vec4(0.0);\n    vec4 prevEyePos = vec4(0.0);\n";
            for (int k = 0; k <= weights; ++k)
            {
                // BLENDINDICES as written in the vertex (UBYTE4/SHORT4 arrive as integer-valued floats).
                std::string idx = indexed ? "clamp(int(in_blendIndices[" + N(k) + "] + 0.5), 0, 3)" : N(k);
                std::string m = "ff.world[" + idx + "]";
                s += "    {\n        mat4 worldView = ff.view * " + m + ";\n";
                s += "        eyePos += weight[" + N(k) + "] * (worldView * position);\n";
                // The previous position: same weights and indices, previous world and view matrices.
                s += "        prevEyePos += weight[" + N(k) + "] * ((prevFrame.view * prevFrame.world[" + idx +
                     "]) * position);\n";
                if (hasNormal)
                    s += "        normal += weight[" + N(k) + "] * (NormalMatrix(worldView) * inNormal);\n";
                s += "    }\n";
            }
        }
        // D3DRS_NORMALIZENORMALS: renormalise after the transform (else the transformed length is kept).
        if (hasNormal && key.normalizeNormals)
            s += "    normal = length(normal) > 0.0 ? normalize(normal) : normal;\n";
        s += "    gl_Position = ff.projection * eyePos;\n";
        s += "    vCurClip = gl_Position;\n    vPrevClip = prevFrame.projection * prevEyePos;\n";
    }
    s += "    D3D9_CAPTURE_WRITE(gl_Position);\n";
    // D3D9's half-pixel offset, applied by every vertex shader (vk_abi.h).
    s += "    gl_Position.xy += ff.halfPixel.xy * gl_Position.w;\n";
    s += std::string("    gl_PointSize = ") + (Has(key, D3DDECLUSAGE_PSIZE, 0) ? "in_pointSize.x" : "1.0") + ";\n";

    // Colours. Lighting off (or pre-transformed vertices): the vertex colours pass through; a missing diffuse is
    // opaque white and a missing specular is black (D3D9 defaults).
    if (key.lighting && !pretransformed)
        s += Lighting(key);
    else
    {
        s += std::string("    vs_color0 = ") + (hasColor0 ? "in_color0" : "vec4(1.0)") + ";\n";
        s += std::string("    vs_color1 = ") + (hasColor1 ? "in_color1" : "vec4(0.0)") + ";\n";
    }

    // Texture coordinates for every stage the pixel side may read (all 8 when a pixel shader is bound).
    bool needReflect = false;
    for (int i = 0; i < 8; ++i)
        if ((key.psIsShader || i < stages) &&
            (key.stage[i].texCoordGen == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR >> 16 ||
             key.stage[i].texCoordGen == D3DTSS_TCI_SPHEREMAP >> 16))
            needReflect = true;
    if (needReflect)
        s += std::string("    vec3 reflected = reflect(") +
             (key.localViewer ? "normalize(eyePos.xyz)" : "vec3(0.0, 0.0, 1.0)") + ", normal);\n";
    for (int i = 0; i < 8; ++i)
    {
        if (key.psIsShader || i < stages)
            s += TexCoord(key, i, pretransformed);
        else
            s += "    vs_tc" + N(i) + " = vec4(0.0, 0.0, 0.0, 1.0);\n";
    }

    // Vertex fog factor (varying 10). Pre-transformed vertices carry it in the specular alpha. Otherwise, with
    // D3DRS_FOGVERTEXMODE set, it is computed from the camera-space distance: the depth z, or with
    // D3DRS_RANGEFOGENABLE the true distance length(P). With mode NONE, the application's value is used: the FOG
    // attribute if present, else the specular alpha (D3D9 "Vertex Fog"); with neither, no fog (guess).
    std::string fog;
    if (pretransformed)
        fog = hasColor1 ? "in_color1.a" : "1.0";
    else if (key.fogVertexMode == D3DFOG_LINEAR || key.fogVertexMode == D3DFOG_EXP ||
             key.fogVertexMode == D3DFOG_EXP2)
        fog = FogFactor(key.fogVertexMode, key.rangeFogEnable ? "length(eyePos.xyz)" : "eyePos.z", "ff.fogParams");
    else if (Has(key, D3DDECLUSAGE_FOG, 0))
        fog = "in_fog.x";
    else
        fog = hasColor1 ? "in_color1.a" : "1.0";
    s += "    vs_fog = " + fog + ";\n";
    s += "}\n";
    return {s, 0, 0};
}

// --- Pixel -------------------------------------------------------------------------------------------------------

namespace
{
bool IsTextureArg(uint8_t arg) { return (arg & D3DTA_SELECTMASK) == D3DTA_TEXTURE; }

// Does stage s read its own texture? Through an argument (arg0 only for MULTIPLYADD/LERP), an op that reads the
// texture alpha, or the bump-map ops (which read du/dv from it).
// Arguments a D3DTEXTUREOP reads: bit 0 arg1, bit 1 arg2, bit 2 arg0.
int OpArgs(uint8_t op)
{
    switch (op)
    {
    case 0:
    case D3DTOP_DISABLE:
    case D3DTOP_BUMPENVMAP:
    case D3DTOP_BUMPENVMAPLUMINANCE: return 0;
    case D3DTOP_SELECTARG1:
    case D3DTOP_PREMODULATE: return 1;
    case D3DTOP_SELECTARG2: return 2;
    case D3DTOP_MULTIPLYADD:
    case D3DTOP_LERP: return 7;
    default: return 3;
    }
}

bool StageReadsTexture(const FFStage& s)
{
    auto opUses = [](uint8_t op, uint8_t a1, uint8_t a2, uint8_t a0) {
        if (op == D3DTOP_BLENDTEXTUREALPHA || op == D3DTOP_BLENDTEXTUREALPHAPM || op == D3DTOP_BUMPENVMAP ||
            op == D3DTOP_BUMPENVMAPLUMINANCE)
            return true;
        int used = OpArgs(op);
        return ((used & 1) && IsTextureArg(a1)) || ((used & 2) && IsTextureArg(a2)) || ((used & 4) && IsTextureArg(a0));
    };
    return opUses(s.colorOp, s.colorArg1, s.colorArg2, s.colorArg0) ||
           opUses(s.alphaOp, s.alphaArg1, s.alphaArg2, s.alphaArg0);
}

// An argument (D3DTA_*) of stage i as a vec4 expression. D3DTA_CURRENT in stage 0 is the diffuse colour (the
// shader starts with current = diffuse). D3DTA_TEMP reads 0 until a stage writes it. Modifiers: ALPHAREPLICATE
// copies alpha to all channels, COMPLEMENT gives 1 - x (the two commute).
std::string Arg(uint8_t arg, int i)
{
    std::string x;
    switch (arg & D3DTA_SELECTMASK)
    {
    case D3DTA_DIFFUSE: x = "diffuse"; break;
    case D3DTA_CURRENT: x = "current"; break;
    case D3DTA_TEXTURE: x = "tex" + N(i); break;
    case D3DTA_TFACTOR: x = "ff.textureFactor"; break;
    case D3DTA_SPECULAR: x = "specular"; break;
    case D3DTA_TEMP: x = "temp"; break;
    case D3DTA_CONSTANT: x = "ff.textureStageConstant[" + N(i) + "]"; break;
    default: x = "vec4(0.0)"; break;
    }
    if (arg & D3DTA_ALPHAREPLICATE)
        x = "vec4(" + x + ".a)";
    if (arg & D3DTA_COMPLEMENT)
        x = "(vec4(1.0) - " + x + ")";
    return x;
}

// D3DTEXTUREOP on vec4 arguments a1, a2, a0 (the stage's colour or alpha arguments); the caller keeps .rgb or .a.
// Formulas from the D3D9 D3DTEXTUREOP documentation.
std::string Op(uint8_t op, int i, const std::string& a1, const std::string& a2, const std::string& a0)
{
    std::string tex = "tex" + N(i);
    switch (op)
    {
    case D3DTOP_SELECTARG1: return a1;
    case D3DTOP_SELECTARG2: return a2;
    case D3DTOP_MODULATE: return a1 + " * " + a2;
    case D3DTOP_MODULATE2X: return a1 + " * " + a2 + " * 2.0";
    case D3DTOP_MODULATE4X: return a1 + " * " + a2 + " * 4.0";
    case D3DTOP_ADD: return a1 + " + " + a2;
    case D3DTOP_ADDSIGNED: return a1 + " + " + a2 + " - 0.5";
    case D3DTOP_ADDSIGNED2X: return "(" + a1 + " + " + a2 + " - 0.5) * 2.0";
    case D3DTOP_SUBTRACT: return a1 + " - " + a2;
    case D3DTOP_ADDSMOOTH: return a1 + " + " + a2 + " - " + a1 + " * " + a2;  // a1 + a2 (1 - a1)
    // BLEND*ALPHA: a1 * alpha + a2 * (1 - alpha), alpha from the diffuse colour, this stage's texture, the
    // texture factor or the current colour.
    case D3DTOP_BLENDDIFFUSEALPHA: return "mix(" + a2 + ", " + a1 + ", diffuse.a)";
    case D3DTOP_BLENDTEXTUREALPHA: return "mix(" + a2 + ", " + a1 + ", " + tex + ".a)";
    case D3DTOP_BLENDFACTORALPHA: return "mix(" + a2 + ", " + a1 + ", ff.textureFactor.a)";
    case D3DTOP_BLENDCURRENTALPHA: return "mix(" + a2 + ", " + a1 + ", current.a)";
    case D3DTOP_BLENDTEXTUREALPHAPM: return a1 + " + " + a2 + " * (1.0 - " + tex + ".a)";  // premultiplied
    // PREMODULATE: this stage passes arg1 on, and the next stage's texture is modulated by this stage's result
    // (handled when that texture is sampled; the reference rasterizer's reading of "modulate with the next
    // texture stage").
    case D3DTOP_PREMODULATE: return a1;
    // The colour-only ops (as alpha ops D3D9 leaves them undefined; the same vec4 formula is used).
    case D3DTOP_MODULATEALPHA_ADDCOLOR: return a1 + " + " + a1 + ".a * " + a2;          // a1.rgb + a1.a a2.rgb
    case D3DTOP_MODULATECOLOR_ADDALPHA: return a1 + " * " + a2 + " + " + a1 + ".a";     // a1.rgb a2.rgb + a1.a
    case D3DTOP_MODULATEINVALPHA_ADDCOLOR: return "(1.0 - " + a1 + ".a) * " + a2 + " + " + a1;
    case D3DTOP_MODULATEINVCOLOR_ADDALPHA: return "(vec4(1.0) - " + a1 + ") * " + a2 + " + " + a1 + ".a";
    // BUMPENVMAP(LUMINANCE): the stage's texture holds du, dv (and luminance) that perturb the next stage; its own
    // colour result is the current colour unchanged.
    case D3DTOP_BUMPENVMAP:
    case D3DTOP_BUMPENVMAPLUMINANCE: return "current";
    // DOTPRODUCT3: 4 * sum((a1 - 0.5) * (a2 - 0.5)) over RGB, saturated, replicated to all four channels.
    case D3DTOP_DOTPRODUCT3:
        return "vec4(clamp(4.0 * dot(" + a1 + ".rgb - 0.5, " + a2 + ".rgb - 0.5), 0.0, 1.0))";
    case D3DTOP_MULTIPLYADD: return a0 + " + " + a1 + " * " + a2;
    case D3DTOP_LERP: return "mix(" + a2 + ", " + a1 + ", " + a0 + ")";  // a0 a1 + (1 - a0) a2
    default: return "current";
    }
}
}  // namespace

GeneratedShader FixedFunctionPixel(const FFKey& key)
{
    GeneratedShader g;
    const int stages = ActiveStages(key);
    const char* flat = key.shadeFlat ? "flat " : "";

    std::string s = "#version 450\n";
    s += FinalPixelDefine(key) + "\n";  // fog and alpha test; needs `ff` and `vFog` below
    s += "// D3D9 fixed-function texture stages (vk_fixed_function.cpp)\n";
    s += UniformBlock("FixedFunction", "ff");
    s += "layout(location = 10) in float vFog;  // vertex fog factor\n";
    for (int i = 0; i < stages; ++i)
        s += "layout(location = " + N(i) + ") in vec4 ps_tc" + N(i) + ";\n";
    s += std::string("layout(location = 8) ") + flat + "in vec4 ps_color0;\n";
    s += std::string("layout(location = 9) ") + flat + "in vec4 ps_color1;\n";
    s += kMotionPixelDecl;

    bool reads[8] = {};
    for (int i = 0; i < stages; ++i)
    {
        reads[i] = StageReadsTexture(key.stage[i]);
        // A texture perturbed by a bump stage or modulated by PREMODULATE must be sampled as well.
        if (i > 0 && (key.stage[i - 1].colorOp == D3DTOP_BUMPENVMAP ||
                      key.stage[i - 1].colorOp == D3DTOP_BUMPENVMAPLUMINANCE ||
                      key.stage[i - 1].colorOp == D3DTOP_PREMODULATE))
            reads[i] = true;
        if (reads[i] && key.stage[i].textureType != 0)
        {
            bool cube = key.stage[i].textureType == 2;
            s += "layout(set = 0, binding = " + N(kBindingSampler0 + i) + ") uniform " +
                 (cube ? "samplerCube" : "sampler2D") + " s" + N(i) + ";\n";
            g.samplerMask |= 1u << i;
            if (cube)
                g.samplerCubeMask |= 1u << i;
        }
    }
    s += "layout(location = 0) out vec4 fragColor;\n\nvoid main()\n{\n";
    s += "    vec4 diffuse = ps_color0, specular = ps_color1;\n"
         "    vec4 current = diffuse;  // D3DTA_CURRENT in stage 0 is the diffuse colour\n"
         "    vec4 temp = vec4(0.0);   // D3DTA_TEMP\n"
         "    vec2 bumpOffset = vec2(0.0);\n"
         "    float bumpLuminance = 1.0;\n"
         "    vec4 premodulated = vec4(1.0);  // result of a D3DTOP_PREMODULATE stage, for the next texture\n";

    for (int i = 0; i < stages; ++i)
    {
        const FFStage& st = key.stage[i];
        const std::string n = N(i);
        s += "    // stage " + n + "\n";
        const uint8_t prevOp = i > 0 ? key.stage[i - 1].colorOp : 0;
        if (reads[i])
        {
            if (st.textureType == 0)
                // No texture bound: D3DTA_TEXTURE reads opaque white, so MODULATE/SELECTARG chains behave as if the
                // stage only passed its other argument (guess; D3D9 leaves sampling an unset stage undefined).
                s += "    vec4 tex" + n + " = vec4(1.0);\n";
            else
            {
                // Coordinates: varying i. D3DTTFF_PROJECTED divides by the last of the COUNTn components (by w
                // for COUNT4 or no count). A preceding BUMPENVMAP stage then adds its offset to (u, v).
                std::string tc = "ps_tc" + n;
                int count = st.transformFlags & 7;
                bool projected = (st.transformFlags & kFFTransformProjected) != 0 && st.textureType != 2;
                if (projected)
                {
                    const char* comp = count == 2 ? "y" : count == 3 ? "z" : "w";
                    s += "    vec4 coord" + n + " = " + tc + " / (" + tc + "." + comp + " != 0.0 ? " + tc + "." + comp +
                         " : 1.0);\n";
                }
                else
                    s += "    vec4 coord" + n + " = " + tc + ";\n";
                if (prevOp == D3DTOP_BUMPENVMAP || prevOp == D3DTOP_BUMPENVMAPLUMINANCE)
                    s += "    coord" + n + ".xy += bumpOffset;\n";
                if (st.textureType == 2)
                    s += "    vec4 tex" + n + " = texture(s" + n + ", coord" + n + ".xyz);\n";
                else
                    s += "    vec4 tex" + n + " = texture(s" + n + ", coord" + n + ".xy);\n";
            }
            if (prevOp == D3DTOP_BUMPENVMAPLUMINANCE)
                s += "    tex" + n + ".rgb *= bumpLuminance;\n";
            if (prevOp == D3DTOP_PREMODULATE)
                s += "    tex" + n + " *= premodulated;\n";
        }

        // Arguments the op reads, each evaluated once (an unused argument may name an unsampled texture).
        auto args = [&](const char* prefix, uint8_t op, uint8_t a1, uint8_t a2, uint8_t a0) {
            int used = OpArgs(op);
            std::string p = prefix + std::string("_") + n;
            if (used & 1)
                s += "    vec4 " + p + "1 = " + Arg(a1, i) + ";\n";
            if (used & 2)
                s += "    vec4 " + p + "2 = " + Arg(a2, i) + ";\n";
            if (used & 4)
                s += "    vec4 " + p + "0 = " + Arg(a0, i) + ";\n";
            return Op(op, i, p + "1", p + "2", p + "0");
        };
        s += "    vec4 color" + n + " = clamp(" + args("c", st.colorOp, st.colorArg1, st.colorArg2, st.colorArg0) +
             ", 0.0, 1.0);\n";
        if (st.colorOp == D3DTOP_DOTPRODUCT3)
            // DOTPRODUCT3 as a colour op writes alpha too, whatever the alpha op.
            s += "    vec4 alpha" + n + " = color" + n + ";\n";
        else if (st.alphaOp == D3DTOP_DISABLE || st.alphaOp == 0)
            // Alpha disabled on an enabled colour stage: the alpha passes through (guess; undefined in D3D9).
            s += "    vec4 alpha" + n + " = current;\n";
        else
            s += "    vec4 alpha" + n + " = clamp(" + args("a", st.alphaOp, st.alphaArg1, st.alphaArg2, st.alphaArg0) +
                 ", 0.0, 1.0);\n";
        if (st.colorOp == D3DTOP_BUMPENVMAP || st.colorOp == D3DTOP_BUMPENVMAPLUMINANCE)
        {
            // D3D9 bump environment mapping: du, dv from this stage's texture (signed formats arrive as -1..1 in
            // .r/.g), u' = u + M00 du + M10 dv, v' = v + M01 du + M11 dv (bumpEnvMat = M00, M01, M10, M11).
            // LUMINANCE: L = saturate(texture.b * LSCALE + LOFFSET) modulates the next stage's texture colour.
            s += "    {\n        vec4 m = ff.bumpEnvMat[" + n + "];\n";
            s += "        bumpOffset = vec2(m.x * tex" + n + ".r + m.z * tex" + n + ".g, m.y * tex" + n + ".r + m.w * tex" +
                 n + ".g);\n";
            if (st.colorOp == D3DTOP_BUMPENVMAPLUMINANCE)
                s += "        bumpLuminance = clamp(tex" + n + ".b * ff.bumpEnvLum[" + n + "].x + ff.bumpEnvLum[" +
                     n + "].y, 0.0, 1.0);\n";
            s += "    }\n";
        }
        // D3DTSS_RESULTARG: the stage writes D3DTA_CURRENT (default) or D3DTA_TEMP.
        std::string dst = (st.resultArg & D3DTA_SELECTMASK) == D3DTA_TEMP ? "temp" : "current";
        s += "    " + dst + " = vec4(color" + n + ".rgb, alpha" + n + ".a);\n";
        if (st.colorOp == D3DTOP_PREMODULATE)
            s += "    premodulated = vec4(color" + n + ".rgb, alpha" + n + ".a);\n";
    }
    // D3DRS_SPECULARENABLE: the specular colour is added after the last texture stage (RGB only).
    if (key.specularEnable)
        s += "    current.rgb = clamp(current.rgb + specular.rgb, 0.0, 1.0);\n";
    s += "    vec4 result = current;\n"
         "    D3D9_FINAL(result);  // fog and alpha test (FinalPixelDefine)\n"
         "    fragColor = result;\n";
    s += kMotionPixelWrite;
    s += "}\n";
    g.glsl = s;
    return g;
}

#ifdef RHI_SHADER_TEST
// Extra keys beyond shader_test.cpp's representative set: every D3DTOP, every argument and modifier, TEMP result,
// every texture coordinate generation mode, projected and cube textures, each light type count, every fog mode
// and alpha function, flat shading, 1-3 weight blending. Returns the keys; the caller compiles them.
std::vector<FFKey> FixedFunctionSelfCheckKeys()
{
    std::vector<FFKey> keys;
    auto base = [] {
        FFKey k{};
        k.vertexMask = (1u << VertexLocation(D3DDECLUSAGE_POSITION, 0)) | (1u << VertexLocation(D3DDECLUSAGE_NORMAL, 0)) |
                       (1u << VertexLocation(D3DDECLUSAGE_TEXCOORD, 0)) | (1u << VertexLocation(D3DDECLUSAGE_TEXCOORD, 1)) |
                       (1u << VertexLocation(D3DDECLUSAGE_COLOR, 0)) | (1u << VertexLocation(D3DDECLUSAGE_COLOR, 1));
        k.vertexTexCoordSize[0] = 2, k.vertexTexCoordSize[1] = 3;
        for (auto& s : k.stage)
            s.colorOp = D3DTOP_DISABLE, s.alphaOp = D3DTOP_DISABLE, s.resultArg = D3DTA_CURRENT;
        k.stage[0] = {D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTA_CURRENT, D3DTOP_MODULATE, D3DTA_TEXTURE,
                      D3DTA_DIFFUSE, D3DTA_CURRENT, D3DTA_CURRENT, 0, 0, 0, 1, {}};
        k.colorVertex = 1, k.diffuseSource = D3DMCS_COLOR1, k.specularSource = D3DMCS_COLOR2;
        k.alphaFunc = D3DCMP_ALWAYS;
        return k;
    };
    const uint8_t args[] = {D3DTA_DIFFUSE, D3DTA_CURRENT, D3DTA_TEXTURE, D3DTA_TFACTOR, D3DTA_SPECULAR, D3DTA_TEMP,
                            D3DTA_CONSTANT};
    for (int op = D3DTOP_SELECTARG1; op <= D3DTOP_LERP; ++op)
    {
        FFKey k = base();
        int a = op % 7;
        k.stage[1] = {(uint8_t)op, (uint8_t)(args[a] | D3DTA_COMPLEMENT), args[(a + 1) % 7],
                      (uint8_t)(args[(a + 2) % 7] | D3DTA_ALPHAREPLICATE), (uint8_t)op, args[(a + 3) % 7],
                      D3DTA_CURRENT, D3DTA_TEXTURE, (uint8_t)(op % 2 ? D3DTA_TEMP : D3DTA_CURRENT), 1, 0, 0, 1, {}};
        k.stage[2] = {D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_TEMP, D3DTA_CURRENT, D3DTOP_SELECTARG2, D3DTA_TEXTURE,
                      D3DTA_CURRENT, D3DTA_CURRENT, D3DTA_CURRENT, 0, 0, 0, (uint8_t)(op % 3 == 0 ? 2 : 1), {}};
        k.fogEnable = 1, k.fogTableMode = (uint8_t)(op % 4);  // NONE, EXP, EXP2, LINEAR
        k.fogVertexMode = D3DFOG_EXP2, k.rangeFogEnable = op % 2;
        k.alphaTestEnable = 1, k.alphaFunc = (uint8_t)(1 + op % 8);
        keys.push_back(k);
    }
    for (int gen = 0; gen <= 4; ++gen)
        for (int flags : {0, 2, 3, 4, 3 | kFFTransformProjected, 4 | kFFTransformProjected})
        {
            FFKey k = base();
            k.stage[0].texCoordGen = (uint8_t)gen, k.stage[0].transformFlags = (uint8_t)flags;
            k.stage[0].textureType = gen == 3 && flags == 3 ? 2 : 1;
            k.stage[0].texCoordIndex = 1;
            k.localViewer = gen % 2;
            keys.push_back(k);
        }
    for (int lights = 0; lights <= 8; lights += 4)
        for (int blend = 0; blend <= 3; ++blend)
        {
            FFKey k = base();
            k.lighting = 1, k.lightCount = (uint8_t)lights, k.specularEnable = (uint8_t)(blend % 2);
            k.localViewer = (uint8_t)(blend / 2);
            k.normalizeNormals = 1, k.shadeFlat = blend == 1;
            k.vertexMask |= (1u << VertexLocation(D3DDECLUSAGE_BLENDWEIGHT, 0)) |
                            (1u << VertexLocation(D3DDECLUSAGE_BLENDINDICES, 0)) | (1u << VertexLocation(D3DDECLUSAGE_FOG, 0)) |
                            (1u << VertexLocation(D3DDECLUSAGE_PSIZE, 0));
            k.vertexBlend = (uint8_t)blend, k.indexedVertexBlend = lights == 4;
            k.ambientSource = D3DMCS_COLOR1, k.emissiveSource = D3DMCS_COLOR2;
            k.fogEnable = 1, k.alphaTestEnable = 1, k.alphaFunc = D3DCMP_NEVER;
            keys.push_back(k);
        }
    {
        FFKey k = base();  // pixel shader bound: all 8 texture coordinate sets come from the vertex part
        k.psIsShader = 1;
        k.stage[0].colorOp = D3DTOP_DISABLE;
        k.stage[3].texCoordGen = D3DTSS_TCI_CAMERASPACEPOSITION >> 16, k.stage[3].transformFlags = D3DTTFF_COUNT3;
        keys.push_back(k);
    }
    {
        FFKey k = base();  // no stages at all, no colours
        k.stage[0].colorOp = D3DTOP_DISABLE;
        k.vertexMask = 1u << VertexLocation(D3DDECLUSAGE_POSITION, 0);
        keys.push_back(k);
    }
    {
        FFKey k = base();  // bump luminance into a projected stage, premodulate, unbound texture
        k.stage[0] = {D3DTOP_BUMPENVMAPLUMINANCE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTOP_SELECTARG2,
                      D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTA_CURRENT, D3DTA_CURRENT, 0, 0, 0, 1, {}};
        k.stage[1] = {D3DTOP_PREMODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTOP_DISABLE, D3DTA_TEXTURE,
                      D3DTA_CURRENT, D3DTA_CURRENT, D3DTA_CURRENT, 1, 0, 3 | kFFTransformProjected, 1, {}};
        k.stage[2] = {D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_TEXTURE,
                      D3DTA_CURRENT, D3DTA_CURRENT, D3DTA_CURRENT, 0, 0, 0, 0, {}};
        k.stage[3] = {D3DTOP_PREMODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTOP_SELECTARG1,
                      D3DTA_CURRENT, D3DTA_CURRENT, D3DTA_CURRENT, D3DTA_CURRENT, 0, 0, 0, 1, {}};
        keys.push_back(k);
    }
    return keys;
}
#endif
}  // namespace rhi::vulkan
