// rhi::vulkan: the descriptor set / pipeline layout, graphics pipelines from D3D9 render state, and samplers from
// D3D9 sampler state. Conventions: vk_abi.h. Vertex input: vk_pipeline_input.cpp.
//
// Winding and culling (the one subtle mapping in this file)
// ---------------------------------------------------------
// D3D9 decides a triangle's facing from its winding *on screen*, after the viewport transform (window y grows
// downwards): D3DCULL_CCW (the default) removes triangles that appear counter-clockwise, D3DCULL_CW removes the
// clockwise ones. Vulkan likewise decides facing in framebuffer coordinates (y grows downwards), and its
// VK_FRONT_FACE_CLOCKWISE / COUNTER_CLOCKWISE names describe the winding as seen in the framebuffer image.
// The backend draws with a negative-height viewport (vk_abi.h), which maps D3D9 clip space y = +1 to the top row,
// exactly as D3D9's own viewport transform does. The rasterised image therefore has the same on-screen winding as
// under D3D9 (the y flip changes the winding in *clip space* relative to a positive-height Vulkan viewport, but
// facing is evaluated after the viewport transform, where both APIs agree). Hence:
//   frontFace = VK_FRONT_FACE_CLOCKWISE (D3D9 treats clockwise as front),
//   D3DCULL_NONE -> VK_CULL_MODE_NONE, D3DCULL_CW -> VK_CULL_MODE_FRONT_BIT, D3DCULL_CCW -> VK_CULL_MODE_BACK_BIT.
// Worked check: clip-space triangle (-1,1), (1,1), (-1,-1) = top-left, top-right, bottom-left on screen in D3D9
// (clockwise on screen). Negative-height viewport: framebuffer (0,0), (W,0), (0,H); Vulkan's signed area
// a = -1/2 * sum(x_i*y_(i+1) - x_(i+1)*y_i) = -1/2 * (W*H) < 0, i.e. clockwise for Vulkan too. Same answer.
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "config.h"
#include "log.h"
#include "vk_abi.h"
#include "vk_pipeline.h"

namespace rhi::vulkan
{
namespace
{
template <typename T>
uint64_t HandleBits(T h)  // non-dispatchable handles are uint64_t on x86 and pointers on x64
{
    uint64_t v = 0;
    std::memcpy(&v, &h, sizeof(h));
    return v;
}

// Pipeline cache key: every input of GetPipeline, packed without implicit padding so it can be hashed and compared
// bytewise (PipelineKey itself has 4 bytes of compiler padding after RasterKey).
#pragma pack(push, 1)
struct PipeCacheKey
{
    RasterKey raster;
    uint64_t layoutHash, shaderHash;
    uint32_t colorFormat, depthFormat, motionFormat;
    uint64_t vs, fs, tcs, tes;
};
#pragma pack(pop)

struct PipeCacheKeyOps
{
    size_t operator()(const PipeCacheKey& k) const { return static_cast<size_t>(Hash(&k, sizeof(k))); }
    bool operator()(const PipeCacheKey& a, const PipeCacheKey& b) const
    {
        return std::memcmp(&a, &b, sizeof(a)) == 0;
    }
};

// Sampler key: D3DSAMP_ADDRESSU .. D3DSAMP_MAXANISOTROPY (1..10). SRGBTEXTURE, ELEMENTINDEX and DMAPOFFSET do not
// affect the VkSampler.
using SamplerCacheKey = std::array<uint32_t, 10>;
struct SamplerCacheKeyOps
{
    size_t operator()(const SamplerCacheKey& k) const { return static_cast<size_t>(Hash(k.data(), sizeof(k))); }
};

struct State
{
    VkDevice device = VK_NULL_HANDLE;  // the device the objects below belong to
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineCache cache = VK_NULL_HANDLE;
    std::unordered_map<PipeCacheKey, VkPipeline, PipeCacheKeyOps, PipeCacheKeyOps> pipelines;
    std::unordered_map<SamplerCacheKey, VkSampler, SamplerCacheKeyOps> samplers;
};
State g_state;

// Caller holds Ctx().lock. Forget objects created for a device that no longer exists (device re-creation; the
// objects died with it).
State& St()
{
    Context& c = Ctx();
    if (g_state.device != c.device)
    {
        g_state = State{};
        g_state.device = c.device;
    }
    return g_state;
}

VkCompareOp CompareOp(uint8_t f)  // D3DCMPFUNC
{
    switch (f)
    {
    case D3DCMP_NEVER: return VK_COMPARE_OP_NEVER;
    case D3DCMP_LESS: return VK_COMPARE_OP_LESS;
    case D3DCMP_EQUAL: return VK_COMPARE_OP_EQUAL;
    case D3DCMP_LESSEQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case D3DCMP_GREATER: return VK_COMPARE_OP_GREATER;
    case D3DCMP_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
    case D3DCMP_GREATEREQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    default: return VK_COMPARE_OP_ALWAYS;  // D3DCMP_ALWAYS and invalid values
    }
}

VkStencilOp StencilOp(uint8_t op)  // D3DSTENCILOP
{
    switch (op)
    {
    case D3DSTENCILOP_ZERO: return VK_STENCIL_OP_ZERO;
    case D3DSTENCILOP_REPLACE: return VK_STENCIL_OP_REPLACE;
    case D3DSTENCILOP_INCRSAT: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case D3DSTENCILOP_DECRSAT: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case D3DSTENCILOP_INVERT: return VK_STENCIL_OP_INVERT;
    case D3DSTENCILOP_INCR: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case D3DSTENCILOP_DECR: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default: return VK_STENCIL_OP_KEEP;  // D3DSTENCILOP_KEEP and invalid values
    }
}

// D3DBLEND -> VkBlendFactor for the plain factors. BOTHSRCALPHA / BOTHINVSRCALPHA are resolved by BlendPair.
VkBlendFactor BlendFactor(uint8_t b, VkBlendFactor fallback)
{
    switch (b)
    {
    case D3DBLEND_ZERO: return VK_BLEND_FACTOR_ZERO;
    case D3DBLEND_ONE: return VK_BLEND_FACTOR_ONE;
    case D3DBLEND_SRCCOLOR: return VK_BLEND_FACTOR_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR: return VK_BLEND_FACTOR_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;  // valid as a destination in Vulkan too
    // D3DRS_BLENDFACTOR: the backend sets it as the dynamic blend constants. Both the colour and the alpha factor
    // use all four components (CONSTANT_COLOR), as D3D9 does.
    case D3DBLEND_BLENDFACTOR: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case D3DBLEND_INVBLENDFACTOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    default: return fallback;
    }
}

// Source and destination factors of one D3D9 blend equation. D3DBLEND_BOTHSRCALPHA as the source means
// (SRCALPHA, INVSRCALPHA) and overrides the destination; BOTHINVSRCALPHA means (INVSRCALPHA, SRCALPHA). D3D9
// documents both as valid only for the source. As a destination (undefined in D3D9) they are treated as the
// destination half of their pair; this is a guess, labelled as such.
void BlendPair(uint8_t src, uint8_t dst, VkBlendFactor* vs, VkBlendFactor* vd)
{
    if (src == D3DBLEND_BOTHSRCALPHA)
    {
        *vs = VK_BLEND_FACTOR_SRC_ALPHA, *vd = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        return;
    }
    if (src == D3DBLEND_BOTHINVSRCALPHA)
    {
        *vs = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, *vd = VK_BLEND_FACTOR_SRC_ALPHA;
        return;
    }
    *vs = BlendFactor(src, VK_BLEND_FACTOR_ONE);  // D3D9 defaults: SRCBLEND ONE, DESTBLEND ZERO
    if (dst == D3DBLEND_BOTHSRCALPHA)
        *vd = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    else if (dst == D3DBLEND_BOTHINVSRCALPHA)
        *vd = VK_BLEND_FACTOR_SRC_ALPHA;
    else
        *vd = BlendFactor(dst, VK_BLEND_FACTOR_ZERO);
}

VkBlendOp BlendOp(uint8_t op)  // D3DBLENDOP; MIN/MAX ignore the factors in both APIs
{
    switch (op)
    {
    case D3DBLENDOP_SUBTRACT: return VK_BLEND_OP_SUBTRACT;
    case D3DBLENDOP_REVSUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case D3DBLENDOP_MIN: return VK_BLEND_OP_MIN;
    case D3DBLENDOP_MAX: return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;  // D3DBLENDOP_ADD and invalid values
    }
}

VkPrimitiveTopology Topology(uint8_t t)  // D3DPRIMITIVETYPE
{
    switch (t)
    {
    case D3DPT_POINTLIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case D3DPT_LINELIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case D3DPT_LINESTRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case D3DPT_TRIANGLESTRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case D3DPT_TRIANGLEFAN: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  // D3DPT_TRIANGLELIST and invalid values
    }
}

bool HasStencil(VkFormat f)
{
    return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT ||
           f == VK_FORMAT_S8_UINT;
}

bool HasDepth(VkFormat f)
{
    return f != VK_FORMAT_UNDEFINED && f != VK_FORMAT_S8_UINT;
}

VkSamplerAddressMode AddressMode(uint32_t a)  // D3DTEXTUREADDRESS
{
    switch (a)
    {
    case D3DTADDRESS_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case D3DTADDRESS_CLAMP: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case D3DTADDRESS_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    // MIRRORONCE would be VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE, but that needs the Vulkan 1.2 feature
    // samplerMirrorClampToEdge, which the device (vk_context.cpp) does not enable. MIRRORED_REPEAT is identical
    // for coordinates in [-1, 1] and differs only outside it (repeats the mirror instead of clamping).
    case D3DTADDRESS_MIRRORONCE: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;  // D3DTADDRESS_WRAP and invalid values
    }
}

// D3DSAMP_BORDERCOLOR (a D3DCOLOR, ARGB) to the nearest of Vulkan's three built-in border colours (custom border
// colours need VK_EXT_custom_border_color, which the device does not enable).
VkBorderColor BorderColor(uint32_t argb)
{
    float a = ((argb >> 24) & 0xff) / 255.0f, r = ((argb >> 16) & 0xff) / 255.0f, g = ((argb >> 8) & 0xff) / 255.0f,
          b = (argb & 0xff) / 255.0f;
    auto dist = [&](float cr, float cg, float cb, float ca) {
        return (r - cr) * (r - cr) + (g - cg) * (g - cg) + (b - cb) * (b - cb) + (a - ca) * (a - ca);
    };
    float transparent = dist(0, 0, 0, 0), black = dist(0, 0, 0, 1), white = dist(1, 1, 1, 1);
    if (transparent < black && transparent < white)
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    return black <= white ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK : VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
}

// MAG/MINFILTER: NONE and POINT -> nearest; LINEAR, ANISOTROPIC and the quad filters (PYRAMIDALQUAD,
// GAUSSIANQUAD, which no hardware of the era implemented beyond linear) -> linear.
VkFilter Filter(uint32_t f)
{
    return f >= D3DTEXF_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}
}  // namespace

VkDescriptorSetLayout DescriptorSetLayout()
{
    std::lock_guard<std::recursive_mutex> guard(Ctx().lock);
    State& s = St();
    if (s.setLayout || !s.device)
        return s.setLayout;
    VkDescriptorSetLayoutBinding b[5 + kMaxSamplers]{};
    uint32_t n = 0;
    for (uint32_t i : {kBindingVsConstants, kBindingPsConstants, kBindingFixedFunction, kBindingPrevFrame})
        b[n++] = {i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                  nullptr};
    b[n++] = {kBindingCapture, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    for (uint32_t i = 0; i < kMaxSamplers; ++i)
        b[n++] = {kBindingSampler0 + i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                  nullptr};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = n;
    ci.pBindings = b;
    VkResult r = vkCreateDescriptorSetLayout(s.device, &ci, nullptr, &s.setLayout);
    if (r != VK_SUCCESS)
        LogVk("vkCreateDescriptorSetLayout", r), s.setLayout = VK_NULL_HANDLE;
    return s.setLayout;
}

VkPipelineLayout PipelineLayout()
{
    std::lock_guard<std::recursive_mutex> guard(Ctx().lock);
    VkDescriptorSetLayout set = DescriptorSetLayout();
    State& s = St();
    if (s.layout || !set)
        return s.layout;
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.setLayoutCount = 1;
    ci.pSetLayouts = &set;
    VkResult r = vkCreatePipelineLayout(s.device, &ci, nullptr, &s.layout);
    if (r != VK_SUCCESS)
        LogVk("vkCreatePipelineLayout", r), s.layout = VK_NULL_HANDLE;
    return s.layout;
}

VkPipeline GetPipeline(const PipelineKey& key, const VertexInput& input, VkShaderModule vs, VkShaderModule fs,
                       VkShaderModule tcs, VkShaderModule tes)
{
    std::lock_guard<std::recursive_mutex> guard(Ctx().lock);
    VkPipelineLayout layout = PipelineLayout();
    State& s = St();
    if (!layout)
        return VK_NULL_HANDLE;

    PipeCacheKey ck;
    std::memset(&ck, 0, sizeof(ck));
    ck.raster = key.raster;
    std::memset(ck.raster.pad, 0, sizeof(ck.raster.pad));  // the caller may leave the padding unset
    ck.layoutHash = key.layoutHash;
    ck.shaderHash = key.shaderHash;
    ck.colorFormat = key.colorFormat;
    ck.depthFormat = key.depthFormat;
    ck.motionFormat = key.motionFormat;
    ck.vs = HandleBits(vs);
    ck.fs = HandleBits(fs);
    ck.tcs = HandleBits(tcs);
    ck.tes = HandleBits(tes);
    if (auto it = s.pipelines.find(ck); it != s.pipelines.end())
        return it->second;  // a failed key stays cached as null, so its failure is logged only once

    const RasterKey& rk = key.raster;
    const auto colorFormat = static_cast<VkFormat>(key.colorFormat);
    const auto depthFormat = static_cast<VkFormat>(key.depthFormat);

    VkPipelineShaderStageCreateInfo stages[4]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    uint32_t stageCount = 2;
    const bool tessellated = tcs && tes;  // vk_abi.h "Tessellation": patch lists of 3
    if (tessellated)
    {
        stages[2] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[2].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        stages[2].module = tcs;
        stages[2].pName = "main";
        stages[3] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[3].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        stages[3].module = tes;
        stages[3].pName = "main";
        stageCount = 4;
    }
    VkPipelineTessellationStateCreateInfo ts{VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
    ts.patchControlPoints = 3;

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(input.bindings.size());
    vi.pVertexBindingDescriptions = input.bindings.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(input.attributes.size());
    vi.pVertexAttributeDescriptions = input.attributes.data();

    // Strips and fans are never cut in D3D9 (no restart index), so primitive restart stays off.
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = tessellated ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : Topology(rk.topology);

    // Viewport and scissor are dynamic. D3DRS_SCISSORTESTENABLE is handled by the backend through the dynamic
    // scissor (the full render target when disabled), so rk.scissorTestEnable does not change the pipeline.
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.depthClampEnable = VK_FALSE;  // D3D9 clips to 0 <= z <= w
    rs.polygonMode = rk.fillMode == D3DFILL_WIREFRAME ? VK_POLYGON_MODE_LINE
                     : rk.fillMode == D3DFILL_POINT   ? VK_POLYGON_MODE_POINT
                                                      : VK_POLYGON_MODE_FILL;  // needs fillModeNonSolid (enabled)
    rs.cullMode = rk.cullMode == D3DCULL_CW    ? VK_CULL_MODE_FRONT_BIT
                  : rk.cullMode == D3DCULL_CCW ? VK_CULL_MODE_BACK_BIT
                                               : VK_CULL_MODE_NONE;  // see the winding note at the top
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    // D3DRS_DEPTHBIAS / SLOPESCALEDEPTHBIAS values are dynamic (vkCmdSetDepthBias); only the enable is baked.
    rs.depthBiasEnable = rk.depthBiasEnable ? VK_TRUE : VK_FALSE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;  // the key carries no sample count: single-sampled targets

    // Depth: D3DZB_TRUE and D3DZB_USEW both use the z buffer (W-buffering is not emulated). A pipeline without a
    // depth target ignores zEnable, as D3D9 does when no depth-stencil surface is set.
    const bool depth = rk.zEnable != D3DZB_FALSE && HasDepth(depthFormat);
    const bool stencil = rk.stencilEnable && HasStencil(depthFormat);
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = depth ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depth && rk.zWriteEnable ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = depth ? CompareOp(rk.zFunc) : VK_COMPARE_OP_ALWAYS;
    ds.stencilTestEnable = stencil ? VK_TRUE : VK_FALSE;
    VkStencilOpState so{};
    so.failOp = StencilOp(rk.stencilFail);
    so.passOp = StencilOp(rk.stencilPass);
    so.depthFailOp = StencilOp(rk.stencilZFail);
    so.compareOp = CompareOp(rk.stencilFunc);
    so.compareMask = rk.stencilMask;      // D3DRS_STENCILMASK (low 8 bits are all an 8-bit stencil uses)
    so.writeMask = rk.stencilWriteMask;   // D3DRS_STENCILWRITEMASK
    so.reference = 0;                     // dynamic (D3DRS_STENCILREF)
    ds.front = so;
    // D3DRS_TWOSIDEDSTENCILMODE: counter-clockwise (= Vulkan back-facing, see above) triangles should use the
    // D3DRS_CCW_STENCIL* states, which RasterKey does not carry. Both faces use the clockwise states for now.
    ds.back = so;

    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = rk.alphaBlendEnable ? VK_TRUE : VK_FALSE;
    BlendPair(rk.srcBlend, rk.destBlend, &att.srcColorBlendFactor, &att.dstColorBlendFactor);
    att.colorBlendOp = BlendOp(rk.blendOp);
    if (rk.separateAlphaBlend)  // D3DRS_SEPARATEALPHABLENDENABLE
    {
        BlendPair(rk.srcBlendAlpha, rk.destBlendAlpha, &att.srcAlphaBlendFactor, &att.dstAlphaBlendFactor);
        att.alphaBlendOp = BlendOp(rk.blendOpAlpha);
    }
    else
    {
        att.srcAlphaBlendFactor = att.srcColorBlendFactor;
        att.dstAlphaBlendFactor = att.dstColorBlendFactor;
        att.alphaBlendOp = att.colorBlendOp;
    }
    // D3DCOLORWRITEENABLE_RED/GREEN/BLUE/ALPHA are bits 0..3, the same as VK_COLOR_COMPONENT_R/G/B/A_BIT.
    att.colorWriteMask = rk.colorWriteEnable & 0xf;
    // Attachment 1: motion vectors (vk_abi.h), written as is (no blending), when the target has one.
    VkPipelineColorBlendAttachmentState atts[2] = {att, {}};
    // Only surfaces that write depth write motion (opaque geometry); overlays, particles and UI keep what is under
    // them, as temporal upscalers expect.
    atts[1].colorWriteMask = depth && rk.zWriteEnable ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT) : 0;
    const VkFormat formats[2] = {colorFormat, static_cast<VkFormat>(key.motionFormat)};
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = colorFormat == VK_FORMAT_UNDEFINED ? 0 : (key.motionFormat != VK_FORMAT_UNDEFINED ? 2 : 1);
    cb.pAttachments = atts;

    const VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS,
                                      VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = static_cast<uint32_t>(std::size(dynamic));
    dy.pDynamicStates = dynamic;

    // Dynamic rendering. A combined depth-stencil format is declared as both the depth and the stencil attachment
    // format, so the backend must bind the image as pDepthAttachment and pStencilAttachment when it has stencil.
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = cb.attachmentCount;
    ri.pColorAttachmentFormats = formats;
    ri.depthAttachmentFormat = HasDepth(depthFormat) ? depthFormat : VK_FORMAT_UNDEFINED;
    ri.stencilAttachmentFormat = HasStencil(depthFormat) ? depthFormat : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext = &ri;
    ci.stageCount = stageCount;
    ci.pTessellationState = tessellated ? &ts : nullptr;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = layout;

    if (!s.cache)
    {
        VkPipelineCacheCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        if (vkCreatePipelineCache(s.device, &pci, nullptr, &s.cache) != VK_SUCCESS)
            s.cache = VK_NULL_HANDLE;  // optional: pipelines still build without it
    }
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(s.device, s.cache, 1, &ci, nullptr, &pipeline);
    if (r != VK_SUCCESS)
    {
        LogVk("vkCreateGraphicsPipelines", r);
        Log("vulkan: pipeline key: layout %016llx shaders %016llx color %u depth %u topology %u",
            static_cast<unsigned long long>(key.layoutHash), static_cast<unsigned long long>(key.shaderHash),
            key.colorFormat, key.depthFormat, static_cast<unsigned>(rk.topology));
        pipeline = VK_NULL_HANDLE;
    }
    s.pipelines.emplace(ck, pipeline);
    return pipeline;
}

VkSampler GetSampler(const uint32_t states[14])
{
    std::lock_guard<std::recursive_mutex> guard(Ctx().lock);
    State& s = St();
    if (!s.device)
        return VK_NULL_HANDLE;
    SamplerCacheKey k;
    for (uint32_t i = 0; i < k.size(); ++i)
        k[i] = states[D3DSAMP_ADDRESSU + i];
    if (auto it = s.samplers.find(k); it != s.samplers.end())
        return it->second;

    const VkPhysicalDeviceLimits& limits = Ctx().properties.limits;
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.addressModeU = AddressMode(states[D3DSAMP_ADDRESSU]);
    ci.addressModeV = AddressMode(states[D3DSAMP_ADDRESSV]);
    ci.addressModeW = AddressMode(states[D3DSAMP_ADDRESSW]);
    ci.borderColor = BorderColor(states[D3DSAMP_BORDERCOLOR]);
    ci.magFilter = Filter(states[D3DSAMP_MAGFILTER]);
    ci.minFilter = Filter(states[D3DSAMP_MINFILTER]);
    // D3D9 turns anisotropy on when MIN or MAG filter is ANISOTROPIC; MAXANISOTROPY gives the degree.
    if (states[D3DSAMP_MINFILTER] == D3DTEXF_ANISOTROPIC || states[D3DSAMP_MAGFILTER] == D3DTEXF_ANISOTROPIC)
    {
        float degree = static_cast<float>(states[D3DSAMP_MAXANISOTROPY]);
        degree = degree < 1.0f ? 1.0f : degree;
        degree = degree > limits.maxSamplerAnisotropy ? limits.maxSamplerAnisotropy : degree;
        ci.anisotropyEnable = degree > 1.0f ? VK_TRUE : VK_FALSE;  // samplerAnisotropy is enabled
        ci.maxAnisotropy = degree;
    }
    else
        ci.maxAnisotropy = 1.0f;
    // MAXMIPLEVEL: index of the largest (most detailed) level D3D9 may use -> minLod.
    const float maxMipLevel = static_cast<float>(states[D3DSAMP_MAXMIPLEVEL]);
    ci.minLod = maxMipLevel;
    if (states[D3DSAMP_MIPFILTER] == D3DTEXF_NONE)
    {
        // No mipmapping: sample the level MAXMIPLEVEL only. maxLod = minLod + 0.25 (not = minLod) keeps the
        // magnification / minification filter choice alive, as the Vulkan spec recommends for this case.
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.maxLod = maxMipLevel + 0.25f;
    }
    else
    {
        ci.mipmapMode = states[D3DSAMP_MIPFILTER] == D3DTEXF_POINT ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                                                   : VK_SAMPLER_MIPMAP_MODE_LINEAR;
        ci.maxLod = VK_LOD_CLAMP_NONE;
    }
    // MIPMAPLODBIAS holds a float's bits.
    float bias;
    std::memcpy(&bias, &states[D3DSAMP_MIPMAPLODBIAS], sizeof(bias));
    if (!std::isfinite(bias))
        bias = 0.0f;
    // Remaster ([render] lod_bias, added; anisotropy, at least this degree on mipmapped linear-filtered samplers).
    // Both off (0) by default.
    static const float extraBias = ConfigFloat("render", "lod_bias", 0.0f);
    static const float forcedAniso = ConfigFloat("render", "anisotropy", 0.0f);
    bias += extraBias;
    if (forcedAniso > 1.0f && states[D3DSAMP_MIPFILTER] != D3DTEXF_NONE && ci.minFilter == VK_FILTER_LINEAR &&
        forcedAniso > ci.maxAnisotropy)
    {
        ci.maxAnisotropy = forcedAniso > limits.maxSamplerAnisotropy ? limits.maxSamplerAnisotropy : forcedAniso;
        ci.anisotropyEnable = VK_TRUE;
    }
    const float maxBias = limits.maxSamplerLodBias;
    ci.mipLodBias = bias > maxBias ? maxBias : bias < -maxBias ? -maxBias : bias;
    // D3DSAMP_SRGBTEXTURE is ignored: sRGB decoding would need an sRGB image view of the texture, not a sampler
    // setting. D3DSAMP_ELEMENTINDEX and D3DSAMP_DMAPOFFSET (multi-element textures, displacement maps) are unused.
    ci.compareEnable = VK_FALSE;
    ci.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler = VK_NULL_HANDLE;
    VkResult r = vkCreateSampler(s.device, &ci, nullptr, &sampler);
    if (r != VK_SUCCESS)
        LogVk("vkCreateSampler", r), sampler = VK_NULL_HANDLE;
    s.samplers.emplace(k, sampler);
    return sampler;
}
}  // namespace rhi::vulkan
