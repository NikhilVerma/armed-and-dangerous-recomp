// GLSL to SPIR-V with glslang (linked statically; no external compiler at run time).
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <cstdio>
#include <mutex>

#include "vk_shaders.h"

#ifndef RHI_SHADER_TEST
#include "log.h"
#else
#define Log(...) (std::fprintf(stderr, __VA_ARGS__), std::fprintf(stderr, "\n"))
#endif

namespace rhi::vulkan
{
std::string WithDefine(const std::string& glsl, const std::string& define)
{
    size_t eol = glsl.find('\n');
    return glsl.substr(0, eol + 1) + define + "\n" + glsl.substr(eol + 1);
}
std::vector<uint32_t> CompileGlsl(const std::string& glsl, bool pixel)
{
    return CompileGlslStage(glsl, pixel ? GlslStage::Fragment : GlslStage::Vertex);
}

std::vector<uint32_t> CompileGlslStage(const std::string& glsl, GlslStage which)
{
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
    const EShLanguage stages[] = {EShLangVertex, EShLangFragment, EShLangTessControl, EShLangTessEvaluation};
    const char* names[] = {"vertex", "pixel", "tessellation control", "tessellation evaluation"};
    EShLanguage stage = stages[static_cast<int>(which)];
    glslang::TShader shader(stage);
    const char* src = glsl.c_str();
    shader.setStrings(&src, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
    if (!shader.parse(GetDefaultResources(), 450, false, EShMsgDefault))
    {
        Log("vulkan: GLSL %s shader failed to compile: %s", names[static_cast<int>(which)], shader.getInfoLog());
        return {};
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault))
    {
        Log("vulkan: GLSL link failed: %s", program.getInfoLog());
        return {};
    }
    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
    return spirv;
}
}  // namespace rhi::vulkan
