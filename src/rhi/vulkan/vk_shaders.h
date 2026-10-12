#pragma once
// Shader generation for the Vulkan backend. Conventions: vk_abi.h.
#include <cstdint>
#include <string>
#include <vector>

#include "vk_abi.h"

namespace rhi::vulkan
{
// --- vk_shader_translate.cpp ---------------------------------------------------------------------------------
// D3D9 shader bytecode (vs_1_1 .. vs_2_0, ps_1_1 .. ps_2_0, the versions the game uses) to GLSL 450.
struct TranslatedShader
{
    bool ok = false;
    bool isPixel = false;
    std::string glsl;          // a complete shader following vk_abi.h
    std::string error;         // why translation failed
    uint32_t inputMask = 0;    // vertex shaders: bit per VertexLocation read (dcl_* inputs)
    uint32_t samplerMask = 0;  // pixel shaders: bit per sampler s<i> used
    uint32_t samplerCubeMask = 0;  // of those, declared/used as cube maps (ps_2_0 dcl_cube, or texm3x3 forms)
    bool relativeConstants = false;  // vertex shaders: reads c[a0 + n] (the game's skinned meshes: bone palettes)
    bool hasNormal = false;          // vertex shaders: reads a NORMAL input (tessellation needs it)
    // Interface between the stages: vertex shaders write and pixel shaders read these varyings.
    // Locations: 0..7 texcoord t0..t7 (vec4), 8 color0 (vec4), 9 color1 (vec4), 10 fog (float).
};
TranslatedShader TranslateShader(const uint32_t* code);

// --- vk_fixed_function.cpp -------------------------------------------------------------------------------------
// GLSL for the fixed-function pipeline, from the state key. Vertex part: transforms (incl. vertex blending),
// lighting, texture coordinate generation/transform, vertex fog. Pixel part: texture stage combiners, specular
// add, fog, alpha test. The same varying locations as TranslatedShader, so either half can pair with a
// translated shader (FFKey::vsIsShader / psIsShader).
struct GeneratedShader
{
    std::string glsl;
    uint32_t samplerMask = 0;
    uint32_t samplerCubeMask = 0;
};
GeneratedShader FixedFunctionVertex(const FFKey& key);
GeneratedShader FixedFunctionPixel(const FFKey& key);
// What D3D9 does after a shader-model 1-2 pixel shader: fog blending (FFKey fog fields; the fog factor comes from
// varying location 10, or from depth for table fog) and the alpha test. Translated pixel shaders end main() with
// `D3D9_FINAL(result);` (result: the vec4 written to oC0) and write `fragColor = result;` after it; the backend
// inserts this function's output (a `#define D3D9_FINAL(c) ...` line) right after the `#version` line.
std::string FinalPixelDefine(const FFKey& key);

// --- vk_glsl.cpp (backend core) -------------------------------------------------------------------------------
// GLSL to SPIR-V with glslang; empty and an error logged on failure.
std::vector<uint32_t> CompileGlsl(const std::string& glsl, bool pixel);
enum class GlslStage { Vertex, Fragment, TessControl, TessEval, Compute };
std::vector<uint32_t> CompileGlslStage(const std::string& glsl, GlslStage stage);
// An HLSL compute shader (glslang's HLSL front end; third-party effects such as XeGTAO), entry point `entry`.
std::vector<uint32_t> CompileHlslCompute(const std::string& hlsl, const char* entry);
// `glsl` with the line `define` inserted after its #version line (shader variants: D3D9_FINAL, capture, tessellation).
std::string WithDefine(const std::string& glsl, const std::string& define);

// --- vk_tessellation.cpp --------------------------------------------------------------------------------------
// The generic tessellation control and evaluation shaders (vk_abi.h "Tessellation"). `ccw`: the evaluation
// shader's output winding.
std::string TessControlGlsl();
std::string TessEvalGlsl(bool ccw);
extern float g_tessSmoothCos, g_tessSharpCos;  // crease rule (vk_tessellation.cpp), set before compiling
// The VsConstants (vsc), FixedFunction (ff) and PrevFrame (pf) uniform block declarations of translated vertex
// shaders (vk_shader_translate.cpp), for the tessellation stages.
std::string VertexUniformBlocks();
}  // namespace rhi::vulkan
