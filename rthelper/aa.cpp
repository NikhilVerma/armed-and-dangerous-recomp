// Temporal anti-aliasing in the helper (aa.h, docs/subsystems/remaster-aa.md).
#define NOMINMAX
#include "aa.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>

namespace rth
{
namespace
{
uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& mem, uint32_t bits, VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    return 0;
}

// The library's messages go to rt_helper_aa.log next to the helper (warnings and errors only).
void FfxMessage(uint32_t type, const wchar_t* message)
{
    static FILE* f = nullptr;
    if (!f)
        f = std::fopen("rt_helper_aa.log", "w");
    if (f)
        std::fprintf(f, "%s %ls\n", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", message),
            std::fflush(f);
}

ffxFunctions& Ffx(void* p)
{
    return *static_cast<ffxFunctions*>(p);
}

FfxApiResource Resource(VkImage image, const VkImageCreateInfo& info, uint32_t usage, uint32_t state)
{
    return ffxApiGetResourceVK(reinterpret_cast<void*>(image), ffxApiGetImageResourceDescriptionVK(image, info, usage),
                               state);
}

void Barrier(VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage,
             VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = srcStage, b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage, b.dstAccessMask = dstAccess;
    b.oldLayout = from, b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cb, &di);
}
}  // namespace

bool TemporalAa::Init(VkDevice device, VkPhysicalDevice physical, const VkPhysicalDeviceMemoryProperties& mem,
                      std::string& log)
{
    device_ = device, physical_ = physical, mem_ = mem;
    // AMD FidelityFX API: amd_fidelityfx_vk.dll next to rt_helper.exe.
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = std::strrchr(path, '\\');
    std::strcpy(slash ? slash + 1 : path, "amd_fidelityfx_vk.dll");
    HMODULE m = LoadLibraryA(path);
    if (!m)
    {
        log = "amd_fidelityfx_vk.dll not found next to rt_helper.exe";
        return false;
    }
    ffxModule_ = m;
    auto* f = new ffxFunctions{};
    ffxLoadFunctions(f, m);
    ffxFunctions_ = f;
    if (!f->CreateContext || !f->Dispatch || !f->DestroyContext)
    {
        log = "amd_fidelityfx_vk.dll lacks the FidelityFX API entry points";
        return false;
    }
    mode_ = rtipc::kAaFsr;
    return true;
}

bool TemporalAa::CreateImage(Img& img, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage)
{
    VkImageCreateInfo& ci = img.info;
    ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {width, height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ci, nullptr, &img.image) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, img.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(mem_, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &ai, nullptr, &img.memory) != VK_SUCCESS)
        return false;
    vkBindImageMemory(device_, img.image, img.memory, 0);
    img.format = format;
    return true;
}

void TemporalAa::DestroyImages()
{
    for (Img* i : {&color_, &depth_, &motion_, &output_})
    {
        if (i->image)
            vkDestroyImage(device_, i->image, nullptr);
        if (i->memory)
            vkFreeMemory(device_, i->memory, nullptr);
        *i = Img{};
    }
    width_ = height_ = 0;
}

bool TemporalAa::EnsureImages(uint32_t width, uint32_t height, std::string& log)
{
    if (width_ == width && height_ == height && color_.image)
        return true;
    vkDeviceWaitIdle(device_);
    DestroyImages();
    const VkImageUsageFlags in = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!CreateImage(color_, VK_FORMAT_R16G16B16A16_SFLOAT, width, height, in) ||
        !CreateImage(depth_, VK_FORMAT_R32_SFLOAT, width, height, in) ||
        !CreateImage(motion_, VK_FORMAT_R16G16B16A16_SFLOAT, width, height, in) ||
        !CreateImage(output_, VK_FORMAT_R16G16B16A16_SFLOAT, width, height,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
    {
        log = "AA images failed";
        DestroyImages();
        return false;
    }
    width_ = width, height_ = height;
    return true;
}

void TemporalAa::DestroyFsr()
{
    if (fsrContext_ && ffxFunctions_)
    {
        vkDeviceWaitIdle(device_);
        ffxContext c = static_cast<ffxContext>(fsrContext_);
        Ffx(ffxFunctions_).DestroyContext(&c, nullptr);
    }
    fsrContext_ = nullptr;
    fsrWidth_ = fsrHeight_ = 0;
}

bool TemporalAa::EnsureFsr(uint32_t width, uint32_t height, std::string& log)
{
    if (fsrContext_ && fsrWidth_ == width && fsrHeight_ == height)
        return true;
    DestroyFsr();
    ffxCreateBackendVKDesc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend.vkDevice = device_;
    backend.vkPhysicalDevice = physical_;
    backend.vkDeviceProcAddr = vkGetDeviceProcAddr;
    ffxCreateContextDescUpscale desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    desc.header.pNext = &backend.header;
    // The scene is linear HDR (the RGBA16F back buffer), not tone-mapped yet; native size (DLAA-like).
    desc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    desc.maxRenderSize = {width, height};
    desc.maxUpscaleSize = {width, height};
    desc.fpMessage = FfxMessage;
    ffxContext c = nullptr;
    const ffxReturnCode_t r = Ffx(ffxFunctions_).CreateContext(&c, &desc.header, nullptr);
    if (r != FFX_API_RETURN_OK || !c)
    {
        char b[96];
        std::snprintf(b, sizeof(b), "FSR context creation failed (%u)", r);
        log = b;
        return false;
    }
    fsrContext_ = c;
    fsrWidth_ = width, fsrHeight_ = height;
    return true;
}

bool TemporalAa::Record(VkCommandBuffer cb, const rtipc::AaParams& p, VkBuffer color, VkBuffer depth,
                        VkBuffer motion, VkBuffer output, std::string& log)
{
    if (mode_ == rtipc::kAaOff || !EnsureImages(p.width, p.height, log) || !EnsureFsr(p.width, p.height, log))
        return false;

    // Shared buffers (tightly packed rows) -> images.
    const VkPipelineStageFlags2 all = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    for (Img* i : {&color_, &depth_, &motion_})
        Barrier(cb, i->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, all,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {p.width, p.height, 1};
    vkCmdCopyBufferToImage(cb, color, color_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vkCmdCopyBufferToImage(cb, depth, depth_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vkCmdCopyBufferToImage(cb, motion, motion_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    for (Img* i : {&color_, &depth_, &motion_})
        Barrier(cb, i->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT);
    Barrier(cb, output_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, all, 0,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);

    ffxDispatchDescUpscale d{};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = cb;
    d.color = Resource(color_.image, color_.info, FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.depth = Resource(depth_.image, depth_.info, FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.motionVectors =
        Resource(motion_.image, motion_.info, FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.output = Resource(output_.image, output_.info, FFX_API_RESOURCE_USAGE_UAV, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    // FSR's jitter convention is in pixels with +y up in its sample (it negates the projection offset); ours is +y
    // down, matching the motion vectors (uv, y down) scaled to pixels.
    d.jitterOffset = {p.jitter[0], p.jitter[1]};
    // Motion: prevUv - curUv in 0..1 texture coordinates -> pixels.
    d.motionVectorScale = {float(p.width), float(p.height)};
    d.renderSize = {p.width, p.height};
    d.upscaleSize = {p.width, p.height};
    d.enableSharpening = p.sharpness > 0.0f;
    d.sharpness = p.sharpness;
    d.frameTimeDelta = p.frameTimeMs;
    d.preExposure = 1.0f;
    d.reset = p.reset != 0;
    d.cameraNear = p.cameraNear;
    d.cameraFar = p.cameraFar;
    d.cameraFovAngleVertical = p.fovY;
    d.viewSpaceToMetersFactor = 0.3048f;  // the game's units are feet
    d.flags = p.debug ? FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW : 0;
    ffxContext c = static_cast<ffxContext>(fsrContext_);
    const ffxReturnCode_t r = Ffx(ffxFunctions_).Dispatch(&c, &d.header);
    if (r != FFX_API_RETURN_OK)
    {
        char b[64];
        std::snprintf(b, sizeof(b), "FSR dispatch failed (%u)", r);
        log = b;
        return false;
    }

    // Output image -> shared buffer.
    Barrier(cb, output_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
    vkCmdCopyImageToBuffer(cb, output_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output, 1, &region);
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = all, mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cb, &di);
    return true;
}

void TemporalAa::Shutdown()
{
    DestroyFsr();
    DestroyImages();
    delete static_cast<ffxFunctions*>(ffxFunctions_);
    ffxFunctions_ = nullptr;
    if (ffxModule_)
        FreeLibrary(static_cast<HMODULE>(ffxModule_)), ffxModule_ = nullptr;
}
}  // namespace rth
