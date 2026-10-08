#pragma once
// Temporal anti-aliasing in the helper (docs/subsystems/remaster-aa.md, protocol src/rhi/vulkan/rt_ipc.h v4): the
// game's scene (before the UI) arrives in shared buffers; this copies them into images, runs the library and copies
// the result to the shared output buffer. FSR 3.1 through AMD's FidelityFX API (amd_fidelityfx_vk.dll next to
// rt_helper.exe, third_party/ffx-api headers); NVIDIA DLAA through the NGX SDK (nvngx_dlss.dll next to rt_helper.exe,
// AND_HAVE_DLSS); Intel XeSS in its native-resolution AA mode (libxess.dll next to rt_helper.exe, loaded at run time).
// Each backend is optional: a mode whose library is missing runs FSR instead (ActiveMode says which).

#include <volk.h>

#include <cstdint>
#include <string>
#include <vector>

#include "rt_ipc.h"

namespace rth
{
class TemporalAa
{
public:
    // Before the helper creates its instance / device: extensions and features the optional backends need.
    // Names the instance or device does not offer are dropped by the caller (the backend then stays off).
    static void InstanceExtensions(std::vector<const char*>& out);
    static void DeviceExtensions(VkInstance instance, VkPhysicalDevice physical, std::vector<const char*>& out);
    // XeSS patches the caller's feature chain (VkPhysicalDeviceFeatures2 and its pNext structures).
    static void DeviceFeatures(VkInstance instance, VkPhysicalDevice physical, VkPhysicalDeviceFeatures2& features);
    // device / physical: the helper's; mem: its memory properties. False when no backend can run (log says why).
    bool Init(VkInstance instance, VkDevice device, VkPhysicalDevice physical,
              const VkPhysicalDeviceMemoryProperties& mem, std::string& log);
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
        VkImageView view = VK_NULL_HANDLE;
    };
    bool EnsureImages(uint32_t width, uint32_t height, std::string& log);
    bool CreateImage(Img& img, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage);
    void DestroyImages();
    bool EnsureFsr(uint32_t width, uint32_t height, std::string& log);
    void DestroyFsr();
    bool RecordFsr(VkCommandBuffer cb, const rtipc::AaParams& p, std::string& log);
    bool InitDlss(std::string& log);
    bool EnsureDlss(VkCommandBuffer cb, uint32_t width, uint32_t height, std::string& log);
    bool RecordDlss(VkCommandBuffer cb, const rtipc::AaParams& p, std::string& log);
    void DestroyDlss();
    bool InitXess(std::string& log);
    bool EnsureXess(uint32_t width, uint32_t height, std::string& log);
    bool RecordXess(VkCommandBuffer cb, const rtipc::AaParams& p, std::string& log);
    void DestroyXess();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_{};
    uint32_t width_ = 0, height_ = 0;
    Img color_, depth_, motion_, output_;
    uint32_t mode_ = rtipc::kAaOff;  // the mode last run
    bool haveFsr_ = false, haveDlss_ = false, haveXess_ = false;
    std::string pendingLog_;  // a backend that failed: reported once, then FSR runs

    // FidelityFX API (loaded at run time).
    void* ffxModule_ = nullptr;
    void* ffxFunctions_ = nullptr;  // ffxFunctions
    void* fsrContext_ = nullptr;    // ffxContext
    uint32_t fsrWidth_ = 0, fsrHeight_ = 0;

    // NVIDIA NGX (DLSS in DLAA mode).
    bool ngxInitialised_ = false;
    void* ngxParams_ = nullptr;  // NVSDK_NGX_Parameter
    void* dlssHandle_ = nullptr; // NVSDK_NGX_Handle
    uint32_t dlssWidth_ = 0, dlssHeight_ = 0;

    // Intel XeSS (libxess.dll, entry points resolved at run time).
    void* xessModule_ = nullptr;
    void* xessContext_ = nullptr;  // xess_context_handle_t
    uint32_t xessWidth_ = 0, xessHeight_ = 0;
    float xessMvSign_ = 1.0f;
};
}  // namespace rth
