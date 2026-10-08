// FSR 3.1 through the FidelityFX SDK's native API and its Vulkan backend (fsr_bridge.h).
#include "fsr_bridge.h"

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Frame generation's swapchain (FrameInterpolationSwapchainVK.cpp) is not built: only the upscaler is used. The
// backend's interface table still names this function.
FFX_API FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*)
{
    return FFX_ERROR_INVALID_ARGUMENT;
}

namespace
{
FfxFsr3UpscalerContext g_context;
bool g_haveContext = false;
void* g_scratch = nullptr;
uint32_t g_width = 0, g_height = 0;
VkDevice g_device = VK_NULL_HANDLE;
// The backend keeps a pointer to this (ffxGetDeviceVK): it must outlive FsrEnsure.
VkDeviceContext g_deviceContext{};
// The backend interface and the three "shared" resources the native FSR3 API expects the caller to own (dilated
// depth, dilated motion vectors, reconstructed previous nearest depth), created as AMD's FidelityFX API provider does
// (ffx-api/src/ffx_provider_fsr3upscale.cpp). Without them FSR clears an unallocated resource slot and the driver
// crashes (seen in game: vkCmdFillBuffer on handle 0).
FfxInterface g_interface{};
FfxResourceInternal g_shared[3]{};
bool g_haveShared = false;
PFN_vkGetDeviceProcAddr g_realGetDeviceProcAddr = nullptr;

// The SDK asks for extension names (vkGetBufferMemoryRequirements2KHR, ...) that are core in Vulkan 1.1+ and not
// enabled as extensions on the game's device: fall back to the core name when the suffixed one is not there.
PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddrCoreFallback(VkDevice device, const char* name)
{
    PFN_vkVoidFunction f = g_realGetDeviceProcAddr(device, name);
    const size_t n = std::strlen(name);
    if (!f && n > 3 && n < 120 && std::strcmp(name + n - 3, "KHR") == 0)
    {
        char core[128];
        std::memcpy(core, name, n - 3);
        core[n - 3] = 0;
        f = g_realGetDeviceProcAddr(device, core);
    }
    return f;
}

FfxResource Resource(const FsrImage& i, FfxResourceStates state, const wchar_t* name)
{
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = i.format;
    ci.extent = {i.width, i.height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = i.usage;
    const FfxResourceUsage extra =
        (i.usage & VK_IMAGE_USAGE_STORAGE_BIT) ? FFX_RESOURCE_USAGE_UAV : FFX_RESOURCE_USAGE_READ_ONLY;
    return ffxGetResourceVK(reinterpret_cast<void*>(i.image), ffxGetImageResourceDescriptionVK(i.image, ci, extra),
                            name, state);
}

void Message(FfxMsgType type, const wchar_t* message)
{
    static FILE* f = nullptr;
    if (!f)
        f = std::fopen("and-fsr.log", "w");
    if (f)
        std::fprintf(f, "%s %ls\n", type == FFX_MESSAGE_TYPE_ERROR ? "error" : "warning", message), std::fflush(f);
}
}  // namespace

void FsrShutdown()
{
    if (g_haveContext)
    {
        vkDeviceWaitIdle(g_device);
        if (g_haveShared)
            for (FfxResourceInternal& r : g_shared)
                g_interface.fpDestroyResource(&g_interface, r, 0);
        g_haveShared = false;
        ffxFsr3UpscalerContextDestroy(&g_context);
        g_haveContext = false;
    }
    std::free(g_scratch);
    g_scratch = nullptr;
    g_width = g_height = 0;
}

bool FsrEnsure(VkDevice device, VkPhysicalDevice physical, PFN_vkGetDeviceProcAddr getDeviceProcAddr, uint32_t width,
               uint32_t height, char* log, size_t logSize)
{
    if (g_haveContext && width == g_width && height == g_height)
        return true;
    FsrShutdown();
    g_device = device;
    const size_t scratchSize = ffxGetScratchMemorySizeVK(physical, FFX_FSR3UPSCALER_CONTEXT_COUNT);
    g_scratch = std::calloc(1, scratchSize);
    g_realGetDeviceProcAddr = getDeviceProcAddr;
    g_deviceContext = {device, physical, GetDeviceProcAddrCoreFallback};
    FfxFsr3UpscalerContextDescription desc{};
    FfxErrorCode e = ffxGetInterfaceVK(&desc.backendInterface, ffxGetDeviceVK(&g_deviceContext), g_scratch, scratchSize,
                                       FFX_FSR3UPSCALER_CONTEXT_COUNT);
    if (e != FFX_OK)
        return std::snprintf(log, logSize, "FSR: Vulkan backend interface failed (%d)", int(e)), false;
    // The scene is the linear HDR back buffer before tone mapping; native resolution.
    desc.flags = FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE;  // exposure: our 1x1 texture (1.0); the scene is display-referred
    desc.maxRenderSize = {width, height};
    desc.maxUpscaleSize = {width, height};
    desc.fpMessage = Message;
    ffxFsr3UpscalerSetGlobalDebugMessage(
        [](uint32_t type, const wchar_t* message) { Message(static_cast<FfxMsgType>(type), message); }, 1);
    e = ffxFsr3UpscalerContextCreate(&g_context, &desc);
    if (e != FFX_OK)
        return std::snprintf(log, logSize, "FSR: context creation failed (%d)", int(e)), false;
    g_haveContext = true;
    g_interface = desc.backendInterface;
    FfxFsr3UpscalerSharedResourceDescriptions shared{};
    e = ffxFsr3UpscalerGetSharedResourceDescriptions(&g_context, &shared);
    if (e == FFX_OK)
        e = g_interface.fpCreateResource(&g_interface, &shared.dilatedDepth, 0, &g_shared[0]);
    if (e == FFX_OK)
        e = g_interface.fpCreateResource(&g_interface, &shared.dilatedMotionVectors, 0, &g_shared[1]);
    if (e == FFX_OK)
        e = g_interface.fpCreateResource(&g_interface, &shared.reconstructedPrevNearestDepth, 0, &g_shared[2]);
    if (e != FFX_OK)
        return std::snprintf(log, logSize, "FSR: shared resources failed (%d)", int(e)), false;
    g_haveShared = true;
    g_width = width, g_height = height;
    return true;
}

bool FsrDispatch(const FsrFrame& f, char* log, size_t logSize)
{
    if (!g_haveContext)
        return std::snprintf(log, logSize, "FSR: no context"), false;
    FfxFsr3UpscalerDispatchDescription d{};
    d.commandList = ffxGetCommandListVK(f.cb);
    d.color = Resource(f.color, FFX_RESOURCE_STATE_COMPUTE_READ, L"and_color");
    d.depth = Resource(f.depth, FFX_RESOURCE_STATE_COMPUTE_READ, L"and_depth");
    d.motionVectors = Resource(f.motion, FFX_RESOURCE_STATE_COMPUTE_READ, L"and_motion");
    d.output = Resource(f.output, FFX_RESOURCE_STATE_UNORDERED_ACCESS, L"and_output");
    if (f.exposure.image)
        d.exposure = Resource(f.exposure, FFX_RESOURCE_STATE_COMPUTE_READ, L"and_exposure");
    d.dilatedDepth = g_interface.fpGetResource(&g_interface, g_shared[0]);
    d.dilatedMotionVectors = g_interface.fpGetResource(&g_interface, g_shared[1]);
    d.reconstructedPrevNearestDepth = g_interface.fpGetResource(&g_interface, g_shared[2]);
    // Jitter in pixels, +y down (the projection offset is -2 jy / h in D3D clip space, as AMD's samples).
    d.jitterOffset = {f.jitter[0], f.jitter[1]};
    // Motion: prevUv - curUv in 0..1 texture coordinates (y down) -> pixels.
    d.motionVectorScale = {float(f.color.width), float(f.color.height)};
    d.renderSize = {f.color.width, f.color.height};
    d.upscaleSize = {f.output.width, f.output.height};
    d.enableSharpening = f.sharpness > 0.0f;
    d.sharpness = f.sharpness;
    d.frameTimeDelta = f.frameTimeMs;
    d.preExposure = 1.0f;
    d.reset = f.reset;
    d.cameraNear = f.cameraNear;
    d.cameraFar = f.cameraFar;
    d.cameraFovAngleVertical = f.fovY;
    d.viewSpaceToMetersFactor = 0.3048f;  // the game's units are feet
    d.flags = f.debugView ? FFX_FSR3UPSCALER_DISPATCH_DRAW_DEBUG_VIEW : 0;
    const FfxErrorCode e = ffxFsr3UpscalerContextDispatch(&g_context, &d);
    if (e != FFX_OK)
        return std::snprintf(log, logSize, "FSR: dispatch failed (%d)", int(e)), false;
    return true;
}
