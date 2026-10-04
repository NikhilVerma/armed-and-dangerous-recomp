#pragma once
// Graphics pipelines for the Vulkan backend: D3D9 render state + vertex layout + shaders -> VkPipeline.
// Conventions: vk_abi.h.
#include <cstdint>
#include <vector>

#include "vk_core.h"

namespace rhi::vulkan
{
// The D3D9 render states that end up in a VkPipeline (others are dynamic state or uniforms). Values are D3D9's.
struct RasterKey
{
    uint8_t fillMode, cullMode, zEnable, zWriteEnable;
    uint8_t zFunc, alphaBlendEnable, srcBlend, destBlend;
    uint8_t blendOp, separateAlphaBlend, srcBlendAlpha, destBlendAlpha;
    uint8_t blendOpAlpha, stencilEnable, stencilFunc, stencilFail;
    uint8_t stencilZFail, stencilPass, twoSidedStencil, colorWriteEnable;  // D3DRS_COLORWRITEENABLE mask
    uint8_t stencilMask, stencilWriteMask, depthBiasEnable, scissorTestEnable;
    uint8_t topology;  // D3DPRIMITIVETYPE
    uint8_t pad[3];
};

struct PipelineKey
{
    RasterKey raster;
    uint64_t layoutHash;    // hash of the vertex elements + strides (VertexInput below)
    uint64_t shaderHash;    // hash of the vertex + fragment SPIR-V module pair
    uint32_t colorFormat;   // VkFormat of the render target (VK_FORMAT_UNDEFINED: none)
    uint32_t depthFormat;   // VkFormat of the depth-stencil target (VK_FORMAT_UNDEFINED: none)
};

// Vertex input state from a D3DVERTEXELEMENT9 array (ends with D3DDECL_END) and the stream strides set with
// SetStreamSource. Attribute locations: VertexLocation(); formats: D3DDECLTYPE -> VkFormat (SHORT4 -> R16G16B16A16
// SSCALED, D3DCOLOR -> B8G8R8A8 UNORM, UBYTE4 -> R8G8B8A8 USCALED, ...). Elements with an unsupported usage are
// dropped. `mask` gets a bit per location present; `texCoordSize` the component count per TEXCOORD index.
struct VertexInput
{
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    uint32_t mask = 0;
    uint8_t texCoordSize[8] = {};
};
VertexInput BuildVertexInput(const D3DVERTEXELEMENT9* elements, const uint32_t strides[16]);

// The single pipeline layout (set 0 = push descriptors per vk_abi.h); created once.
VkPipelineLayout PipelineLayout();
VkDescriptorSetLayout DescriptorSetLayout();

// A pipeline for these states (cached by key; dynamic state: viewport, scissor, depth bias, blend constants,
// stencil reference). Null on failure (logged once per key).
VkPipeline GetPipeline(const PipelineKey& key, const VertexInput& input, VkShaderModule vs, VkShaderModule fs);

// Sampler for D3D9 sampler states of one stage (cached). `states` indexed by D3DSAMPLERSTATETYPE (1..13).
VkSampler GetSampler(const uint32_t states[14]);

uint64_t Hash(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);
}  // namespace rhi::vulkan
