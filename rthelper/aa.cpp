// Temporal anti-aliasing in the helper (aa.h, docs/subsystems/remaster-aa.md).
#define NOMINMAX
#include "aa.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <type_traits>

#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>

#if AND_HAVE_DLSS
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_vk.h>
#endif
#if AND_HAVE_XESS
#include <xess/xess_vk.h>
#endif

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

// The libraries' messages go to rt_helper_aa.log next to the helper (warnings and errors only).
void FfxMessage(uint32_t type, const wchar_t* message)
{
    static FILE* f = nullptr;
    if (!f)
        f = std::fopen("rt_helper_aa.log", "w");
    if (f)
        std::fprintf(f, "%s %ls\n", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", message),
            std::fflush(f);
}

void Message(uint32_t type, const std::string& s)
{
    const std::wstring w(s.begin(), s.end());
    FfxMessage(type, w.c_str());
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

// Path of a file next to rt_helper.exe (an empty name gives the directory, with its trailing backslash).
std::string Beside(const char* name)
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = std::strrchr(path, '\\');
    std::strcpy(slash ? slash + 1 : path, name);
    return path;
}

#if AND_HAVE_XESS
// Intel XeSS entry points (libxess.dll next to rt_helper.exe, resolved at run time so the helper starts without it).
struct XessApi
{
    HMODULE module = nullptr;
    decltype(&xessVKGetRequiredInstanceExtensions) instanceExtensions = nullptr;
    decltype(&xessVKGetRequiredDeviceExtensions) deviceExtensions = nullptr;
    decltype(&xessVKGetRequiredDeviceFeatures) deviceFeatures = nullptr;
    decltype(&xessVKCreateContext) createContext = nullptr;
    decltype(&xessVKInit) init = nullptr;
    decltype(&xessVKExecute) execute = nullptr;
    decltype(&xessSetVelocityScale) setVelocityScale = nullptr;
    decltype(&xessDestroyContext) destroyContext = nullptr;
    bool Ok() const
    {
        return instanceExtensions && deviceExtensions && deviceFeatures && createContext && init && execute &&
               setVelocityScale && destroyContext;
    }
};

const XessApi& Xess()
{
    static const XessApi api = [] {
        XessApi a;
        a.module = LoadLibraryA(Beside("libxess.dll").c_str());
        if (!a.module)
            return a;
        auto get = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(a.module, name));
        };
        get(a.instanceExtensions, "xessVKGetRequiredInstanceExtensions");
        get(a.deviceExtensions, "xessVKGetRequiredDeviceExtensions");
        get(a.deviceFeatures, "xessVKGetRequiredDeviceFeatures");
        get(a.createContext, "xessVKCreateContext");
        get(a.init, "xessVKInit");
        get(a.execute, "xessVKExecute");
        get(a.setVelocityScale, "xessSetVelocityScale");
        get(a.destroyContext, "xessDestroyContext");
        return a;
    }();
    return api;
}
#endif

#if AND_HAVE_DLSS
// NGX application identity: a project ID (the helper is not an application registered with NVIDIA).
constexpr const char* kNgxProjectId = "5b3c7a52-0e4f-4c1e-9a8d-2f6e1b7c4d90";
#endif

// aa_debug bits 8 / 16: flip the jitter's x / y sign before it goes to the library; 32: negate the motion vectors
// (checking conventions).
float JitterX(const rtipc::AaParams& p)
{
    return (p.debug & 8) ? -p.jitter[0] : p.jitter[0];
}
float JitterY(const rtipc::AaParams& p)
{
    return (p.debug & 16) ? -p.jitter[1] : p.jitter[1];
}
}  // namespace

void TemporalAa::InstanceExtensions(std::vector<const char*>& out)
{
#if AND_HAVE_DLSS
    unsigned int ni = 0, nd = 0;
    const char** ie = nullptr;
    const char** de = nullptr;
    if (NVSDK_NGX_VULKAN_RequiredExtensions(&ni, &ie, &nd, &de) == NVSDK_NGX_Result_Success && ie)
        out.insert(out.end(), ie, ie + ni);
#endif
#if AND_HAVE_XESS
    const XessApi& x = Xess();
    uint32_t n = 0, version = 0;
    const char* const* e = nullptr;
    if (x.Ok() && x.instanceExtensions(&n, &e, &version) == XESS_RESULT_SUCCESS && e)
        out.insert(out.end(), e, e + n);
#endif
    (void)out;
}

void TemporalAa::DeviceExtensions(VkInstance instance, VkPhysicalDevice physical, std::vector<const char*>& out)
{
#if AND_HAVE_DLSS
    unsigned int ni = 0, nd = 0;
    const char** ie = nullptr;
    const char** de = nullptr;
    if (NVSDK_NGX_VULKAN_RequiredExtensions(&ni, &ie, &nd, &de) == NVSDK_NGX_Result_Success && de)
        out.insert(out.end(), de, de + nd);
#endif
#if AND_HAVE_XESS
    const XessApi& x = Xess();
    uint32_t n = 0;
    const char* const* e = nullptr;
    if (x.Ok() && x.deviceExtensions(instance, physical, &n, &e) == XESS_RESULT_SUCCESS && e)
        out.insert(out.end(), e, e + n);
#endif
    (void)instance, (void)physical, (void)out;
}

void TemporalAa::DeviceFeatures(VkInstance instance, VkPhysicalDevice physical, VkPhysicalDeviceFeatures2& features)
{
#if AND_HAVE_XESS
    const XessApi& x = Xess();
    void* chain = &features;
    if (x.Ok())
        x.deviceFeatures(instance, physical, &chain);
#endif
    (void)instance, (void)physical, (void)features;
}

bool TemporalAa::Init(VkInstance instance, VkDevice device, VkPhysicalDevice physical,
                      const VkPhysicalDeviceMemoryProperties& mem, std::string& log)
{
    instance_ = instance, device_ = device, physical_ = physical, mem_ = mem;
    // AMD FidelityFX API: amd_fidelityfx_vk.dll next to rt_helper.exe (also the fallback for the other modes).
    if (HMODULE m = LoadLibraryA(Beside("amd_fidelityfx_vk.dll").c_str()))
    {
        ffxModule_ = m;
        auto* f = new ffxFunctions{};
        ffxLoadFunctions(f, m);
        ffxFunctions_ = f;
        haveFsr_ = f->CreateContext && f->Dispatch && f->DestroyContext;
    }
    std::string dlssLog, xessLog;
    haveDlss_ = InitDlss(dlssLog);
    haveXess_ = InitXess(xessLog);
    Message(FFX_API_MESSAGE_TYPE_WARNING, std::string("FSR ") + (haveFsr_ ? "available" : "unavailable") + ", DLSS " +
                                              (haveDlss_ ? "available" : dlssLog) + ", XeSS " +
                                              (haveXess_ ? "available" : xessLog));
    if (!haveFsr_ && !haveDlss_ && !haveXess_)
    {
        log = "no anti-aliasing library: amd_fidelityfx_vk.dll missing; " + dlssLog + "; " + xessLog;
        return false;
    }
    mode_ = haveFsr_ ? rtipc::kAaFsr : haveDlss_ ? rtipc::kAaDlss : rtipc::kAaXess;
    return true;
}

bool TemporalAa::InitDlss(std::string& log)
{
#if AND_HAVE_DLSS
    // nvngx_dlss.dll is looked up next to rt_helper.exe; NGX writes its logs there too.
    const std::string dir = Beside("");
    const std::wstring wdir(dir.begin(), dir.end());
    const wchar_t* paths[] = {wdir.c_str()};
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;
    const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID(
        kNgxProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", wdir.c_str(), instance_, physical_, device_,
        vkGetInstanceProcAddr, vkGetDeviceProcAddr, &info);
    if (NVSDK_NGX_FAILED(r))
    {
        char b[96];
        std::snprintf(b, sizeof(b), "NGX initialisation failed (0x%08x)", unsigned(r));
        log = b;
        return false;
    }
    ngxInitialised_ = true;
    NVSDK_NGX_Parameter* params = nullptr;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetCapabilityParameters(&params)) || !params)
    {
        log = "NGX capability parameters unavailable";
        return false;
    }
    ngxParams_ = params;
    int available = 0;
    params->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    if (!available)
    {
        int result = 0;
        params->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &result);
        char b[96];
        std::snprintf(b, sizeof(b), "not available on this GPU / driver (0x%08x)", unsigned(result));
        log = b;
        return false;
    }
    return true;
#else
    log = "not built (no DLSS SDK)";
    return false;
#endif
}

bool TemporalAa::InitXess(std::string& log)
{
#if AND_HAVE_XESS
    if (!Xess().Ok())
    {
        log = "libxess.dll not found next to rt_helper.exe";
        return false;
    }
    return true;
#else
    log = "not built (no XeSS SDK)";
    return false;
#endif
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
    // Views for DLSS and XeSS (FSR takes the images).
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(device_, &vi, nullptr, &img.view) == VK_SUCCESS;
}

void TemporalAa::DestroyImages()
{
    for (Img* i : {&color_, &depth_, &motion_, &output_})
    {
        if (i->view)
            vkDestroyImageView(device_, i->view, nullptr);
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
    if (!EnsureImages(p.width, p.height, log))
        return false;
    // The requested backend, or FSR when its library is missing (or failed earlier).
    uint32_t mode = p.mode;
    if ((mode == rtipc::kAaDlss && !haveDlss_) || (mode == rtipc::kAaXess && !haveXess_) || mode == rtipc::kAaOff ||
        mode > rtipc::kAaXess)
        mode = rtipc::kAaFsr;
    if (mode == rtipc::kAaFsr && !haveFsr_)
        mode = haveDlss_ ? rtipc::kAaDlss : rtipc::kAaXess;

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

    bool ok = false;
    std::string blog;
    if (mode == rtipc::kAaDlss)
        ok = RecordDlss(cb, p, blog);
    else if (mode == rtipc::kAaXess)
        ok = RecordXess(cb, p, blog);
    if (!ok && mode != rtipc::kAaFsr && haveFsr_)
    {
        // The backend failed: FSR from now on, the reason in rt_helper_aa.log.
        Message(FFX_API_MESSAGE_TYPE_ERROR, blog + "; FSR from now on");
        (mode == rtipc::kAaDlss ? haveDlss_ : haveXess_) = false;
        mode = rtipc::kAaFsr;
    }
    if (mode == rtipc::kAaFsr)
        ok = RecordFsr(cb, p, blog);
    if (!ok)
    {
        log = blog;
        return false;
    }
    mode_ = mode;

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

bool TemporalAa::RecordFsr(VkCommandBuffer cb, const rtipc::AaParams& p, std::string& log)
{
    if (!EnsureFsr(p.width, p.height, log))
        return false;
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
    d.jitterOffset = {JitterX(p), JitterY(p)};
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
    d.flags = (p.debug & 1) ? FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW : 0;
    ffxContext c = static_cast<ffxContext>(fsrContext_);
    const ffxReturnCode_t r = Ffx(ffxFunctions_).Dispatch(&c, &d.header);
    if (r != FFX_API_RETURN_OK)
    {
        char b[64];
        std::snprintf(b, sizeof(b), "FSR dispatch failed (%u)", r);
        log = b;
        return false;
    }
    return true;
}

bool TemporalAa::EnsureDlss(VkCommandBuffer cb, uint32_t width, uint32_t height, std::string& log)
{
#if AND_HAVE_DLSS
    if (dlssHandle_ && dlssWidth_ == width && dlssHeight_ == height)
        return true;
    DestroyDlss();
    NVSDK_NGX_DLSS_Create_Params c{};
    c.Feature.InWidth = width, c.Feature.InHeight = height;
    c.Feature.InTargetWidth = width, c.Feature.InTargetHeight = height;
    c.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;  // native resolution: anti-aliasing only
    // Linear HDR scene colour, motion at render resolution, the library's own exposure, D3D depth (0 near, 1 far).
    c.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                             NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    NVSDK_NGX_Handle* h = nullptr;
    const NVSDK_NGX_Result r =
        NGX_VULKAN_CREATE_DLSS_EXT1(device_, cb, 1, 1, &h, static_cast<NVSDK_NGX_Parameter*>(ngxParams_), &c);
    if (NVSDK_NGX_FAILED(r) || !h)
    {
        char b[96];
        std::snprintf(b, sizeof(b), "DLSS feature creation failed (0x%08x)", unsigned(r));
        log = b;
        return false;
    }
    dlssHandle_ = h;
    dlssWidth_ = width, dlssHeight_ = height;
    return true;
#else
    (void)cb, (void)width, (void)height;
    log = "DLSS not built";
    return false;
#endif
}

bool TemporalAa::RecordDlss(VkCommandBuffer cb, const rtipc::AaParams& p, std::string& log)
{
#if AND_HAVE_DLSS
    if (!EnsureDlss(cb, p.width, p.height, log))
        return false;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto res = [&](const Img& i, bool rw) {
        return NVSDK_NGX_Create_ImageView_Resource_VK(i.view, i.image, range, i.format, p.width, p.height, rw);
    };
    NVSDK_NGX_Resource_VK color = res(color_, false), depth = res(depth_, false), motion = res(motion_, false),
                          output = res(output_, true);
    NVSDK_NGX_VK_DLSS_Eval_Params e{};
    e.Feature.pInColor = &color;
    e.Feature.pInOutput = &output;
    e.pInDepth = &depth;
    e.pInMotionVectors = &motion;
    e.InJitterOffsetX = JitterX(p), e.InJitterOffsetY = JitterY(p);
    e.InRenderSubrectDimensions = {p.width, p.height};
    e.InReset = p.reset ? 1 : 0;
    // Motion: prevUv - curUv in 0..1 texture coordinates -> pixels (DLSS, like FSR, wants prev - cur in pixels).
    const float mvSign = (p.debug & 32) ? -1.0f : 1.0f;
    e.InMVScaleX = mvSign * float(p.width), e.InMVScaleY = mvSign * float(p.height);
    e.InPreExposure = 1.0f, e.InExposureScale = 1.0f;
    e.InFrameTimeDeltaInMsec = p.frameTimeMs;
    const NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSS_EXT(cb, static_cast<NVSDK_NGX_Handle*>(dlssHandle_),
                                                            static_cast<NVSDK_NGX_Parameter*>(ngxParams_), &e);
    if (NVSDK_NGX_FAILED(r))
    {
        char b[96];
        std::snprintf(b, sizeof(b), "DLSS evaluation failed (0x%08x)", unsigned(r));
        log = b;
        return false;
    }
    return true;
#else
    (void)cb, (void)p;
    log = "DLSS not built";
    return false;
#endif
}

void TemporalAa::DestroyDlss()
{
#if AND_HAVE_DLSS
    if (dlssHandle_)
    {
        vkDeviceWaitIdle(device_);
        NVSDK_NGX_VULKAN_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(dlssHandle_));
    }
#endif
    dlssHandle_ = nullptr;
    dlssWidth_ = dlssHeight_ = 0;
}

bool TemporalAa::EnsureXess(uint32_t width, uint32_t height, std::string& log)
{
#if AND_HAVE_XESS
    if (xessContext_ && xessWidth_ == width && xessHeight_ == height)
        return true;
    DestroyXess();
    const XessApi& x = Xess();
    xess_context_handle_t c = nullptr;
    xess_result_t r = x.createContext(instance_, physical_, device_, &c);
    if (r != XESS_RESULT_SUCCESS || !c)
    {
        char b[96];
        std::snprintf(b, sizeof(b), "XeSS context creation failed (%d)", int(r));
        log = b;
        return false;
    }
    xessContext_ = c;
    xess_vk_init_params_t ip{};
    ip.outputResolution = {width, height};
    ip.qualitySetting = XESS_QUALITY_SETTING_AA;  // native resolution: anti-aliasing only
    // Linear HDR colour, motion at render resolution with the depth, the library's own exposure.
    ip.initFlags = XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
    ip.creationNodeMask = 1, ip.visibleNodeMask = 1;
    r = x.init(c, &ip);
    if (r != XESS_RESULT_SUCCESS)
    {
        char b[96];
        std::snprintf(b, sizeof(b), "XeSS initialisation failed (%d)", int(r));
        log = b;
        DestroyXess();
        return false;
    }
    // Motion: prevUv - curUv in 0..1 texture coordinates -> pixels.
    x.setVelocityScale(c, float(width), float(height));
    xessWidth_ = width, xessHeight_ = height;
    xessMvSign_ = 1.0f;
    return true;
#else
    (void)width, (void)height;
    log = "XeSS not built";
    return false;
#endif
}

bool TemporalAa::RecordXess(VkCommandBuffer cb, const rtipc::AaParams& p, std::string& log)
{
#if AND_HAVE_XESS
    if (!EnsureXess(p.width, p.height, log))
        return false;
    const float mvSign = (p.debug & 32) ? -1.0f : 1.0f;
    if (mvSign != xessMvSign_)
        Xess().setVelocityScale(static_cast<xess_context_handle_t>(xessContext_), mvSign * float(p.width),
                                mvSign * float(p.height)),
            xessMvSign_ = mvSign;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto view = [&](const Img& i) {
        xess_vk_image_view_info v{};
        v.imageView = i.view, v.image = i.image, v.subresourceRange = range, v.format = i.format;
        v.width = p.width, v.height = p.height;
        return v;
    };
    xess_vk_execute_params_t e{};
    e.colorTexture = view(color_);
    e.velocityTexture = view(motion_);
    e.depthTexture = view(depth_);
    e.outputTexture = view(output_);
    e.jitterOffsetX = JitterX(p), e.jitterOffsetY = JitterY(p);
    e.exposureScale = 1.0f;
    e.resetHistory = p.reset;
    e.inputWidth = p.width, e.inputHeight = p.height;
    const xess_result_t r = Xess().execute(static_cast<xess_context_handle_t>(xessContext_), cb, &e);
    if (r != XESS_RESULT_SUCCESS)
    {
        char b[96];
        std::snprintf(b, sizeof(b), "XeSS execution failed (%d)", int(r));
        log = b;
        return false;
    }
    return true;
#else
    (void)cb, (void)p;
    log = "XeSS not built";
    return false;
#endif
}

void TemporalAa::DestroyXess()
{
#if AND_HAVE_XESS
    if (xessContext_)
    {
        vkDeviceWaitIdle(device_);
        Xess().destroyContext(static_cast<xess_context_handle_t>(xessContext_));
    }
#endif
    xessContext_ = nullptr;
    xessWidth_ = xessHeight_ = 0;
}

void TemporalAa::Shutdown()
{
    DestroyFsr();
    DestroyDlss();
    DestroyXess();
#if AND_HAVE_DLSS
    if (ngxParams_)
        NVSDK_NGX_VULKAN_DestroyParameters(static_cast<NVSDK_NGX_Parameter*>(ngxParams_)), ngxParams_ = nullptr;
    if (ngxInitialised_)
        NVSDK_NGX_VULKAN_Shutdown1(device_), ngxInitialised_ = false;
#endif
    DestroyImages();
    delete static_cast<ffxFunctions*>(ffxFunctions_);
    ffxFunctions_ = nullptr;
    if (ffxModule_)
        FreeLibrary(static_cast<HMODULE>(ffxModule_)), ffxModule_ = nullptr;
}
}  // namespace rth
