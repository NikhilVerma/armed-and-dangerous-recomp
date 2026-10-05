// vk_shader_test: offline checks for the Vulkan backend's shader generators (no GPU needed).
//
// Usage: vk_shader_test <corpus dir> [--print <file>]
//   corpus dir: D3D9 shader bytecode files (vs_*.bin / ps_*.bin, written by [test] dump_shaders=1)
// For every corpus file: translate (TranslateShader), compile to SPIR-V (CompileGlsl). Then generate the fixed-
// function vertex and pixel shaders for a set of representative keys (FixedFunctionKeys below) and compile those,
// and pair each translated pixel shader with FinalPixelDefine variants. Prints a summary; exit code 1 on failures.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../vk_gtao.h"
#include "../vk_pipeline.h"
#include "../vk_rt.h"
#include "../vk_shaders.h"

using namespace rhi::vulkan;

namespace
{
std::vector<uint32_t> ReadWords(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<uint32_t> w(bytes.size() / 4);
    std::memcpy(w.data(), bytes.data(), w.size() * 4);
    return w;
}

std::string WithFinal(const std::string& glsl, const FFKey& key)
{
    size_t eol = glsl.find('\n');  // after "#version 450"
    return glsl.substr(0, eol + 1) + FinalPixelDefine(key) + "\n" + glsl.substr(eol + 1);
}

// Representative fixed-function states: the combinations the game's renderer sets up (Material_InitCombiners,
// Renderer_BuildColorMixPass, fog modes from Renderer_SelectFogMode) plus lighting and vertex blending.
std::vector<FFKey> FixedFunctionKeys()
{
    std::vector<FFKey> keys;
    auto base = [] {
        FFKey k{};
        k.vertexMask = (1u << VertexLocation(D3DDECLUSAGE_POSITION, 0)) | (1u << VertexLocation(D3DDECLUSAGE_NORMAL, 0)) |
                       (1u << VertexLocation(D3DDECLUSAGE_TEXCOORD, 0)) | (1u << VertexLocation(D3DDECLUSAGE_COLOR, 0));
        k.vertexTexCoordSize[0] = 2;
        for (auto& s : k.stage)
            s.colorOp = D3DTOP_DISABLE, s.alphaOp = D3DTOP_DISABLE, s.resultArg = D3DTA_CURRENT;
        k.stage[0] = {D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTA_CURRENT, D3DTOP_MODULATE, D3DTA_TEXTURE,
                      D3DTA_DIFFUSE, D3DTA_CURRENT, D3DTA_CURRENT, 0, 0, 0, 1, {}};
        k.colorVertex = 1;
        k.diffuseSource = D3DMCS_COLOR1;
        k.ambientSource = D3DMCS_MATERIAL;
        k.specularSource = D3DMCS_COLOR2;
        k.emissiveSource = D3DMCS_MATERIAL;
        k.alphaFunc = D3DCMP_ALWAYS;
        return k;
    };
    keys.push_back(base());
    {
        FFKey k = base();  // lit, fogged, alpha tested
        k.lighting = 1, k.lightCount = 3, k.specularEnable = 1, k.fogEnable = 1, k.fogVertexMode = D3DFOG_LINEAR;
        k.alphaTestEnable = 1, k.alphaFunc = D3DCMP_GREATEREQUAL;
        keys.push_back(k);
    }
    {
        FFKey k = base();  // two textures: blend by current alpha, table fog
        k.vertexMask |= 1u << VertexLocation(D3DDECLUSAGE_TEXCOORD, 1);
        k.vertexTexCoordSize[1] = 2;
        k.stage[1] = {D3DTOP_BLENDCURRENTALPHA, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTOP_SELECTARG2,
                      D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTA_CURRENT, 1, 0, 0, 1, {}};
        k.fogEnable = 1, k.fogTableMode = D3DFOG_LINEAR;
        keys.push_back(k);
    }
    {
        FFKey k = base();  // environment map: camera-space reflection into a cube map, add
        k.stage[1] = {D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTOP_SELECTARG2, D3DTA_TEXTURE,
                      D3DTA_CURRENT, D3DTA_CURRENT, D3DTA_CURRENT, 0, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR >> 16,
                      D3DTTFF_COUNT3, 2, {}};
        keys.push_back(k);
    }
    {
        FFKey k = base();  // texture factor, bump env map stage, skinned (vertex blend)
        k.stage[0].colorArg2 = D3DTA_TFACTOR;
        k.stage[1] = {D3DTOP_BUMPENVMAP, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTOP_SELECTARG2,
                      D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTA_CURRENT, 0, 0, 0, 1, {}};
        k.stage[2] = {D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTOP_SELECTARG2,
                      D3DTA_TEXTURE, D3DTA_CURRENT, D3DTA_CURRENT, D3DTA_CURRENT, 0, 0, 0, 1, {}};
        k.vertexMask |= (1u << VertexLocation(D3DDECLUSAGE_BLENDWEIGHT, 0)) | (1u << VertexLocation(D3DDECLUSAGE_BLENDINDICES, 0));
        k.vertexBlend = D3DVBF_2WEIGHTS, k.indexedVertexBlend = 1;
        keys.push_back(k);
    }
    {
        FFKey k = base();  // pre-transformed UI quads, no lighting
        k.vertexMask = (1u << VertexLocation(D3DDECLUSAGE_POSITIONT, 0)) | (1u << VertexLocation(D3DDECLUSAGE_COLOR, 0)) |
                       (1u << VertexLocation(D3DDECLUSAGE_TEXCOORD, 0));
        keys.push_back(k);
    }
    return keys;
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: vk_shader_test <corpus dir> [--print <file>]\n");
        return 2;
    }
    if (argc >= 4 && std::strcmp(argv[2], "--print") == 0)
    {
        auto words = ReadWords(std::filesystem::path(argv[1]) / argv[3]);
        TranslatedShader t = TranslateShader(words.data());
        std::printf("%s\n%s\n", t.ok ? "ok" : t.error.c_str(), t.glsl.c_str());
        return t.ok ? 0 : 1;
    }
    int files = 0, translated = 0, compiled = 0, variants = 0, variantsOk = 0;
    std::vector<std::string> failures;
    FFKey finalKey = FixedFunctionKeys()[1];
    for (auto& e : std::filesystem::directory_iterator(argv[1]))
    {
        if (e.path().extension() != ".bin")
            continue;
        ++files;
        auto words = ReadWords(e.path());
        TranslatedShader t = TranslateShader(words.data());
        if (!t.ok)
        {
            failures.push_back(e.path().filename().string() + ": translate: " + t.error);
            continue;
        }
        ++translated;
        std::string glsl = t.isPixel ? WithFinal(t.glsl, finalKey) : t.glsl;
        if (CompileGlsl(glsl, t.isPixel).empty())
            failures.push_back(e.path().filename().string() + ": compile");
        else
            ++compiled;
        if (!t.isPixel)  // the tessellation and capture variants (vk_abi.h)
        {
            ++variants;
            if (CompileGlsl(WithDefine(t.glsl, kTessDefine), false).empty() ||
                CompileGlsl(WithDefine(t.glsl, kCaptureDefine), false).empty())
                failures.push_back(e.path().filename().string() + ": tessellation/capture variant");
            else
                ++variantsOk;
        }
    }
    bool tessOk = !CompileGlslStage(TessControlGlsl(), GlslStage::TessControl).empty() &&
                  !CompileGlslStage(TessEvalGlsl(false), GlslStage::TessEval).empty();
    if (!tessOk)
        failures.push_back("tessellation control/evaluation shaders");
    // Lighting passes (compiled at run time in the game): XeGTAO's entries, the AO apply, ray-traced shadows (both
    // paths) and the helper path's depth conversion.
    int lightOk = 0, lightTotal = 0;
    auto light = [&](const char* name, bool ok) {
        ++lightTotal;
        if (ok)
            ++lightOk;
        else
            failures.push_back(std::string("lighting shader ") + name);
    };
    {
        const std::string hlsl = GtaoHlsl();
        for (const char* e : {"CSPrefilterDepths16x16", "CSGTAOHigh", "CSDenoisePass", "CSDenoiseLastPass"})
            light(e, !CompileHlslCompute(hlsl, e).empty());
        light("GtaoApplyVertexGlsl", !CompileGlsl(GtaoApplyVertexGlsl(), false).empty());
        light("GtaoApplyFragmentGlsl", !CompileGlsl(GtaoApplyFragmentGlsl(), true).empty());
        light("RtShadowVertexGlsl", !CompileGlsl(RtShadowVertexGlsl(), false).empty());
        light("RtShadowFragmentGlsl(remote)", !CompileGlsl(RtShadowFragmentGlsl(true), true).empty());
        light("RtShadowFragmentGlsl(ray query)", !CompileGlsl(RtShadowFragmentGlsl(false), true).empty());
        light("RtDepthComputeGlsl", !CompileGlslStage(RtDepthComputeGlsl(), GlslStage::Compute).empty());
    }
    int ffOk = 0, ffTotal = 0;
    for (const FFKey& k : FixedFunctionKeys())
    {
        ffTotal += 2;
        GeneratedShader v = FixedFunctionVertex(k), p = FixedFunctionPixel(k);
        if (!CompileGlsl(v.glsl, false).empty())
            ++ffOk;
        else
            failures.push_back("fixed-function vertex key " + std::to_string(ffTotal / 2 - 1));
        if (!CompileGlsl(p.glsl, true).empty())
            ++ffOk;
        else
            failures.push_back("fixed-function pixel key " + std::to_string(ffTotal / 2 - 1));
    }
    // Vertex input: the layouts the game uses (docs/formats/big-skin.md 3.3: SHORT4 position, SHORT2 uv, ...).
    D3DVERTEXELEMENT9 skin[] = {{0, 0, D3DDECLTYPE_SHORT4, 0, D3DDECLUSAGE_POSITION, 0},
                                {0, 8, D3DDECLTYPE_SHORT4, 0, D3DDECLUSAGE_BLENDINDICES, 0},
                                {0, 16, D3DDECLTYPE_SHORT4, 0, D3DDECLUSAGE_BLENDWEIGHT, 0},
                                {0, 24, D3DDECLTYPE_SHORT4, 0, D3DDECLUSAGE_NORMAL, 0},
                                {0, 32, D3DDECLTYPE_SHORT2, 0, D3DDECLUSAGE_TEXCOORD, 0},
                                D3DDECL_END()};
    uint32_t strides[16] = {36};
    VertexInput vi = BuildVertexInput(skin, strides);
    bool viOk = vi.attributes.size() == 5 && vi.bindings.size() == 1 && vi.texCoordSize[0] == 2;
    if (!viOk)
        failures.push_back("BuildVertexInput: skin layout");
    for (auto& f : failures)
        std::printf("FAIL %s\n", f.c_str());
    std::printf("corpus: %d files, %d translated, %d compiled; vertex variants %d/%d; tessellation stages %s; "
                "fixed function: %d/%d compiled; lighting %d/%d; vertex input %s\n",
                files, translated, compiled, variantsOk, variants, tessOk ? "ok" : "FAIL", ffOk, ffTotal, lightOk,
                lightTotal, viOk ? "ok" : "FAIL");
    return failures.empty() ? 0 : 1;
}
