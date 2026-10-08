#pragma once
// Temporal anti-aliasing in the helper (docs/subsystems/remaster-aa.md, protocol src/rhi/vulkan/rt_ipc.h v4): the
// game's scene (before the UI) arrives in shared buffers; this copies them into images, runs the library and copies
// the result to the shared output buffer. FSR 3.1 through AMD's FidelityFX API (amd_fidelityfx_vk.dll next to
// rt_helper.exe, third_party/ffx-api headers); DLSS and XeSS slots follow the same shape.

#include <volk.h>

#include <cstdint>
#include <string>

#include "rt_ipc.h"

namespace rth
{
class TemporalAa
{
public:
    // device / physical: the helper's; mem: its memory properties. False when no backend can run (log says why).
    bool Init(VkDevice device, VkPhysicalDevice physical, const VkPhysicalDeviceMemoryProperties& mem,
              std::string& log);
    // Records frame `p` into `cb`: shared colour / depth / motion -> images -> library -> shared output.
    bool Record(VkCommandBuffer cb, const rtipc::AaParams& p, VkBuffer color, VkBuffer depth, VkBuffer motion,
                VkBuffer output, std::string& log);
    uint32_t ActiveMode() const { return mode_; }
    void Shutdown();

private:
    struct Img
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageCreateInfo info{};
    };
    bool EnsureImages(uint32_t width, uint32_t height, std::string& log);
    bool CreateImage(Img& img, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage);
    void DestroyImages();
    bool EnsureFsr(uint32_t width, uint32_t height, std::string& log);
    void DestroyFsr();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_{};
    uint32_t width_ = 0, height_ = 0;
    Img color_, depth_, motion_, output_;
    uint32_t mode_ = rtipc::kAaOff;

    // FidelityFX API (loaded at run time).
    void* ffxModule_ = nullptr;
    void* ffxFunctions_ = nullptr;  // ffxFunctions
    void* fsrContext_ = nullptr;    // ffxContext
    uint32_t fsrWidth_ = 0, fsrHeight_ = 0;
};
}  // namespace rth
