// rhi::vulkan present pass: the HDR back buffer (RGBA16F) to the 8-bit present image, through a tonemap operator
// or a debug view. Remaster hooks (docs/goals/modern-renderer.md step 5):
//   [render] tonemap=none|aces (default none: the game's values pass unchanged, so frames match the D3D9 backend)
//   [render] exposure=1.0      (multiplies before the operator)
//   [render] debug_view=none|depth|normals|motion
//     depth    linear eye depth from the depth buffer (white = 300 units)
//     normals  surface normals reconstructed from depth (view space, n * 0.5 + 0.5)
//     motion   the motion-vector attachment (x red, y green, 0.5 = no motion, scaled x50 x exposure)
#include "vk_present.h"

#include <cstring>
#include <string>

#include "config.h"
#include "log.h"

namespace rhi::vulkan
{
namespace
{
const char* kVertex = R"(#version 450
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);  // full-screen triangle
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kFragment = R"(#version 450
layout(location = 0) out vec4 o;
layout(set = 0, binding = 0) uniform sampler2D hdr;
layout(set = 0, binding = 1) uniform sampler2D depthTex;
layout(set = 0, binding = 2) uniform sampler2D motionTex;
layout(push_constant) uniform Params
{
    int view;       // 0 colour, 1 depth, 2 normals, 3 motion
    int tonemap;    // 0 none, 1 ACES
    float exposure;
    float p11, p22, p33, p34, p43;  // D3D9 projection _11 _22 _33 _34 _43 (depth linearisation)
    int hasDepth, hasMotion;
    float gammaR, gammaG, gammaB;  // rhi::Output().gamma: out = in^(1 / gamma)
} pc;

// Eye-space position from the 0..1 depth: depth = (z * _33 + _43) / (z * _34) for a perspective projection with
// _44 = 0 (the game's is right-handed: _34 = -1), so z = _43 / (depth * _34 - _33).
vec3 EyePos(vec2 uv)
{
    float d = texture(depthTex, uv).r;
    float den = d * pc.p34 - pc.p33;
    float z = pc.p43 / (abs(den) < 1e-7 ? 1e-7 : den);
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return vec3(ndc.x * z * pc.p34 / pc.p11, ndc.y * z * pc.p34 / pc.p22, z);
}

vec3 Aces(vec3 x)  // Narkowicz 2015 fit
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec2 size = vec2(textureSize(hdr, 0));
    vec2 uv = gl_FragCoord.xy / size;
    if (pc.view == 1 && pc.hasDepth != 0)
    {
        o = vec4(vec3(clamp(abs(EyePos(uv).z) / 300.0, 0.0, 1.0)), 1.0);
        return;
    }
    if (pc.view == 2 && pc.hasDepth != 0)
    {
        vec2 t = 1.0 / size;
        vec3 p = EyePos(uv), px = EyePos(uv + vec2(t.x, 0.0)), py = EyePos(uv + vec2(0.0, t.y));
        vec3 n = normalize(cross(px - p, py - p));
        if (n.z * sign(pc.p34) > 0.0)
            n = -n;  // face the camera
        o = vec4(n * 0.5 + 0.5, 1.0);
        return;
    }
    if (pc.view == 3 && pc.hasMotion != 0)
    {
        vec2 m = texture(motionTex, uv).rg;
        o = vec4(clamp(m * 50.0 * pc.exposure + 0.5, 0.0, 1.0), 0.5, 1.0);
        return;
    }
    vec3 c = texture(hdr, uv).rgb * pc.exposure;
    if (pc.tonemap == 1)
        c = Aces(c);
    c = clamp(c, 0.0, 1.0);
    if (pc.gammaR != 1.0 || pc.gammaG != 1.0 || pc.gammaB != 1.0)
        c = pow(c, vec3(1.0 / pc.gammaR, 1.0 / pc.gammaG, 1.0 / pc.gammaB));
    o = vec4(c, 1.0);
}
)";

struct Params
{
    int view, tonemap;
    float exposure, p11, p22, p33, p34, p43;
    int hasDepth, hasMotion;
    float gamma[3];
};
}  // namespace

bool PresentPass::Init()
{
    Context& c = Ctx();
    char buf[32];
    ConfigString("render", "debug_view", "none", buf, sizeof(buf));
    view_ = !_stricmp(buf, "depth") ? 1 : !_stricmp(buf, "normals") ? 2 : !_stricmp(buf, "motion") ? 3 : 0;
    ConfigString("render", "tonemap", "none", buf, sizeof(buf));
    tonemap_ = !_stricmp(buf, "aces") || ConfigInt("render", "tonemap_aces", 0) ? 1 : 0;  // tonemap_aces: Remaster page
    exposure_ = ConfigFloat("render", "exposure", 1.0f);
    if (view_ || tonemap_)
        Log("vulkan: present pass: debug_view %d, tonemap %d, exposure %.2f", view_, tonemap_, exposure_);

    VkDescriptorSetLayoutBinding b[3];
    for (uint32_t i = 0; i < 3; ++i)
        b[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dl.bindingCount = 3;
    dl.pBindings = b;
    vkCreateDescriptorSetLayout(c.device, &dl, nullptr, &setLayout_);
    VkPushConstantRange pr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Params)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pr;
    vkCreatePipelineLayout(c.device, &pl, nullptr, &layout_);

    VkShaderModule vs = MakeModule(CompileGlsl(kVertex, false)), fs = MakeModule(CompileGlsl(kFragment, true));
    if (!vs || !fs)
        return false;
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT, st[0].module = vs, st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT, st[1].module = fs, st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkFormat ldr = VK_FORMAT_B8G8R8A8_UNORM;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &ldr;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext = &ri;
    ci.stageCount = 2;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = layout_;
    VkResult r = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_);
    vkDestroyShaderModule(c.device, vs, nullptr);
    vkDestroyShaderModule(c.device, fs, nullptr);
    VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sc.magFilter = sc.minFilter = VK_FILTER_NEAREST;
    sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(c.device, &sc, nullptr, &sampler_);
    return r == VK_SUCCESS;
}

void PresentPass::Record(VkCommandBuffer cb, Image& hdr, Image* depth, Image* motion, Image& ldr, const float proj[16])
{
    hdr.Transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (depth)
        depth->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    if (motion)
        motion->Transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ldr.Transition(cb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = ldr.AttachmentView(0, 0);
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {ldr.width, ldr.height}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRendering(cb, &ri);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport v{0, 0, float(ldr.width), float(ldr.height), 0, 1};
    VkRect2D s{{0, 0}, {ldr.width, ldr.height}};
    vkCmdSetViewport(cb, 0, 1, &v);
    vkCmdSetScissor(cb, 0, 1, &s);
    VkDescriptorImageInfo img[3] = {
        {sampler_, hdr.sampledView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {sampler_, depth ? depth->sampledView : hdr.sampledView,
         depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {sampler_, motion ? motion->sampledView : hdr.sampledView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet w[3];
    for (uint32_t i = 0; i < 3; ++i)
    {
        w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &img[i];
    }
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 3, w);
    const float* g = rhi::Output().gamma;
    Params p{view_,    tonemap_,         exposure_,  proj[0], proj[5], proj[10], proj[11],
             proj[14], depth ? 1 : 0,    motion ? 1 : 0, {g[0] > 0.0f ? g[0] : 1.0f, g[1] > 0.0f ? g[1] : 1.0f,
                                                          g[2] > 0.0f ? g[2] : 1.0f}};
    vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(p), &p);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRendering(cb);
}
}  // namespace rhi::vulkan

rhi::OutputSettings& rhi::Output()
{
    static OutputSettings s;
    return s;
}
