#pragma once
// rhi::vulkan internals: the Vulkan context, memory helpers and the D3D9 state shadow shared by the backend's
// source files. See docs/renderer-interface.md ("Vulkan backend").
#include <windows.h>
#include <d3d9.h>
#include <volk.h>

#include <cstdint>
#include <mutex>
#include <vector>

#include "rhi/rhi.h"

namespace rhi::vulkan
{
constexpr uint32_t kFramesInFlight = 2;

struct Context
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::recursive_mutex lock;  // the game calls D3D9 from its loader thread too (texture uploads)
    bool vertexStores = false;  // vertexPipelineStoresAndAtomics: scene vertex capture (vk_scene.h)
    bool tessellation = false;  // tessellationShader (vk_abi.h "Tessellation")

    uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;
    VkDeviceMemory Allocate(VkMemoryRequirements req, VkMemoryPropertyFlags flags) const;
    // One-off command buffer, submitted and waited for (uploads outside a frame).
    VkCommandBuffer BeginOneShot();
    void EndOneShot(VkCommandBuffer cb);
};

Context& Ctx();
bool ContextInit();  // instance + device (no window); false if Vulkan is unavailable

// Vulkan format for a D3D9 format, and how many bytes a texel or a 4x4 block takes. VK_FORMAT_UNDEFINED if
// unsupported. `swizzleRgba` tells uploads to convert (e.g. 16-bit formats Vulkan lacks).
struct FormatInfo
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t blockBytes = 0;  // per texel, or per 4x4 block for compressed formats
    bool compressed = false;
    bool depth = false;
    bool stencil = false;
    VkComponentMapping swizzle{};  // view swizzle (X8R8G8B8 alpha = 1, L8 = rrr1, A8L8 = rrrg, A8 = 000r)
};
FormatInfo Format(D3DFORMAT f);
// Bytes per row and rows of a level as D3D9 lays them out in LockRect.
void LevelLayout(D3DFORMAT f, uint32_t width, uint32_t height, uint32_t* pitch, uint32_t* rows);

void LogVk(const char* what, VkResult r);
}  // namespace rhi::vulkan
