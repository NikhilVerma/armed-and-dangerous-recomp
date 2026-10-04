#pragma once
// Conventions shared by the Vulkan backend's shader generators (vk_shader_translate, vk_fixed_function) and its
// pipeline/draw code (vk_pipeline, vk_device). Every generated GLSL shader follows exactly this layout.
//
// Descriptor set 0 (push descriptors, VK_KHR_push_descriptor), one layout for every pipeline:
//   binding 0  uniform block VsConstants { vec4 c[256]; }      vertex shader float constants (c0..c255)
//   binding 1  uniform block PsConstants { vec4 c[32]; }       pixel shader float constants (c0..c31)
//   binding 2  uniform block FixedFunction { FFUniforms }      fixed-function state (below), both stages
//   binding 8+i  sampler2D / samplerCube s<i>, i = 0..7          texture stage / sampler i
// All three uniform blocks are std140 and always bound (a zero-filled range when unused).
//
// Vertex attribute locations: by D3DDECLUSAGE and usage index (VertexLocation below). The vertex input state is
// built from the D3DVERTEXELEMENT9 array with the same function, so shaders and pipelines always agree.
//
// Clip space: shaders write D3D9 clip coordinates. The backend renders with a negative-height viewport (y flip)
// and depth range 0..1 (same as D3D9). D3D9's half-pixel offset is applied by every vertex shader through
// FFUniforms::halfPixel: gl_Position.xy += halfPixel.xy * gl_Position.w, with halfPixel = (+1/width, -1/height):
// D3D9 pixel centres are at integer coordinates and Vulkan's at +0.5, so geometry moves half a pixel right and down.
#include <d3d9.h>

#include <cstdint>

namespace rhi::vulkan
{
constexpr uint32_t kBindingVsConstants = 0;
constexpr uint32_t kBindingPsConstants = 1;
constexpr uint32_t kBindingFixedFunction = 2;
constexpr uint32_t kBindingSampler0 = 8;
constexpr uint32_t kMaxSamplers = 8;
constexpr uint32_t kVsConstantCount = 256;
constexpr uint32_t kPsConstantCount = 32;

// Location of a vertex attribute (D3DDECLUSAGE, usage index); 0xff if unsupported.
inline uint32_t VertexLocation(BYTE usage, BYTE index)
{
    switch (usage)
    {
    case D3DDECLUSAGE_POSITION: return index == 0 ? 0 : 0xff;
    case D3DDECLUSAGE_BLENDWEIGHT: return index == 0 ? 1 : 0xff;
    case D3DDECLUSAGE_BLENDINDICES: return index == 0 ? 2 : 0xff;
    case D3DDECLUSAGE_NORMAL: return index == 0 ? 3 : 0xff;
    case D3DDECLUSAGE_PSIZE: return index == 0 ? 4 : 0xff;
    case D3DDECLUSAGE_TEXCOORD: return index < 8 ? 5 + index : 0xff;  // 5..12
    case D3DDECLUSAGE_TANGENT: return index == 0 ? 13 : 0xff;
    case D3DDECLUSAGE_BINORMAL: return index == 0 ? 14 : 0xff;
    case D3DDECLUSAGE_COLOR: return index < 2 ? 15 + index : 0xff;  // 15, 16
    case D3DDECLUSAGE_POSITIONT: return index == 0 ? 17 : 0xff;     // pre-transformed (screen-space) position
    case D3DDECLUSAGE_FOG: return index == 0 ? 18 : 0xff;
    default: return 0xff;
    }
}

// Fixed-function uniform block (std140; mirrored field by field in the generated GLSL `FixedFunction` block).
// Filled by the device from the D3D9 state at draw time.
struct alignas(16) FFUniforms
{
    float world[4][16];        // D3DTS_WORLDMATRIX(0..3), row-major as D3D9 stores them (vectors are rows)
    float view[16];
    float projection[16];
    float texture[8][16];      // D3DTS_TEXTURE0..7
    float halfPixel[4];        // xy: (+1/viewportWidth, -1/viewportHeight) in clip units; zw unused
    float viewportSize[4];     // xy: width, height (for POSITIONT); zw: minZ, maxZ
    float textureFactor[4];    // D3DRS_TEXTUREFACTOR as RGBA 0..1
    float fogColor[4];         // D3DRS_FOGCOLOR as RGBA 0..1
    float fogParams[4];        // start, end, density, w: 1 = W-based table fog (WFOG cap + W-friendly projection)
    float alphaRef[4];         // x: D3DRS_ALPHAREF / 255
    float ambient[4];          // D3DRS_AMBIENT as RGBA 0..1
    float materialDiffuse[4];  // D3DMATERIAL9
    float materialAmbient[4];
    float materialSpecular[4];
    float materialEmissive[4];  // w: power
    float lightDiffuse[8][4];   // D3DLIGHT9, enabled lights packed first (FFKey::lightCount)
    float lightSpecular[8][4];
    float lightAmbient[8][4];
    float lightPosition[8][4];   // w: range
    float lightDirection[8][4];  // w: type (1 point, 2 spot, 3 directional)
    float lightAttenuation[8][4];  // attenuation0..2, falloff
    float lightSpot[8][4];         // cos(theta/2), cos(phi/2)
    float textureStageConstant[8][4];  // D3DTSS_CONSTANT (unused by D3D9 drivers of this era; kept for layout)
    float bumpEnvMat[8][4];            // D3DTSS_BUMPENVMAT00, 01, 10, 11
    float bumpEnvLum[8][4];            // D3DTSS_BUMPENVLSCALE, LOFFSET
};

// Everything that selects a fixed-function shader pair. Values are the D3D9 enums; unused fields are zero so the
// key can be hashed and compared bytewise.
struct FFStage
{
    uint8_t colorOp, colorArg1, colorArg2, colorArg0;  // D3DTOP_*, D3DTA_* (with modifier bits)
    uint8_t alphaOp, alphaArg1, alphaArg2, alphaArg0;
    uint8_t resultArg;     // D3DTA_CURRENT or D3DTA_TEMP
    uint8_t texCoordIndex; // D3DTSS_TEXCOORDINDEX low byte
    uint8_t texCoordGen;   // D3DTSS_TEXCOORDINDEX high bits >> 16 (D3DTSS_TCI_*)
    uint8_t transformFlags;  // D3DTSS_TEXTURETRANSFORMFLAGS: count in bits 0-2, D3DTTFF_PROJECTED as 0x80
    uint8_t textureType;     // 0 none bound, 1 2D, 2 cube
    uint8_t pad[3];
};

struct FFKey
{
    FFStage stage[8];        // up to the first stage whose colorOp is D3DTOP_DISABLE
    uint32_t vertexMask;     // bit per VertexLocation present in the vertex layout
    uint8_t vertexTexCoordSize[8];  // components (1..4) of each TEXCOORD in the layout
    uint8_t lighting;        // D3DRS_LIGHTING
    uint8_t lightCount;      // enabled lights
    uint8_t specularEnable;  // D3DRS_SPECULARENABLE
    uint8_t normalizeNormals;
    uint8_t localViewer;
    uint8_t colorVertex;     // D3DRS_COLORVERTEX
    uint8_t diffuseSource, ambientSource, specularSource, emissiveSource;  // D3DMCS_*
    uint8_t fogTableMode;    // D3DRS_FOGTABLEMODE (pixel fog)
    uint8_t fogVertexMode;   // D3DRS_FOGVERTEXMODE
    uint8_t fogEnable;
    uint8_t rangeFogEnable;
    uint8_t alphaTestEnable;
    uint8_t alphaFunc;       // D3DCMP_*
    uint8_t vertexBlend;     // D3DRS_VERTEXBLEND
    uint8_t indexedVertexBlend;
    uint8_t shadeFlat;       // D3DRS_SHADEMODE == D3DSHADE_FLAT
    uint8_t vsIsShader;      // 1: a vertex shader is bound (only the pixel part is fixed function)
    uint8_t psIsShader;      // 1: a pixel shader is bound (only the vertex part is fixed function)
    uint8_t pad[3];
};
}  // namespace rhi::vulkan
