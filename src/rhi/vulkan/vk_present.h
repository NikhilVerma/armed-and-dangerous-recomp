#pragma once
// rhi::vulkan present pass (vk_present.cpp): HDR back buffer -> tonemap or debug view -> 8-bit present image.
#include "vk_resources.h"

namespace rhi::vulkan
{
class PresentPass
{
public:
    bool Init();
    // Records the pass into `cb` (outside rendering). `proj`: the D3D9 projection matrix (row-major, 16 floats).
    void Record(VkCommandBuffer cb, Image& hdr, Image* depth, Image* motion, Image& ldr, const float proj[16]);
    bool Identity() const { return view_ == 0 && tonemap_ == 0 && exposure_ == 1.0f; }

private:
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    int view_ = 0, tonemap_ = 0;
    float exposure_ = 1.0f;
};
}  // namespace rhi::vulkan
