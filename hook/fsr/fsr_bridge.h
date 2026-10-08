#pragma once
// AMD FSR 3.1 upscaler at native resolution (temporal anti-aliasing) inside the hook DLL (docs/subsystems/
// remaster-aa.md). Compiled into the static library and_fsr with the FidelityFX SDK v1.1.4 sources; the rest of the
// project only sees this header. Vulkan through volk (the device's function pointers).

#include <volk.h>

#include <cstdint>

struct FsrImage
{
    VkImage image;
    VkFormat format;
    uint32_t width, height;
    VkImageUsageFlags usage;
};

struct FsrFrame
{
    VkCommandBuffer cb;
    FsrImage color;   // linear HDR scene, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    FsrImage depth;   // R32 float 0..1 (D3D convention), SHADER_READ_ONLY_OPTIMAL
    FsrImage motion;  // .rg = prevUv - curUv (0..1, y down), SHADER_READ_ONLY_OPTIMAL
    FsrImage output;  // storage image, VK_IMAGE_LAYOUT_GENERAL
    float jitter[2];  // pixels, +x right, +y down
    float cameraNear, cameraFar, fovY;
    float frameTimeMs;
    float sharpness;  // 0..1 (0: no RCAS)
    bool reset;
    bool debugView;
};

// Creates the FSR context for a width x height scene (again when the size changes). False: `log` says why.
bool FsrEnsure(VkDevice device, VkPhysicalDevice physical, PFN_vkGetDeviceProcAddr getDeviceProcAddr, uint32_t width,
               uint32_t height, char* log, size_t logSize);
// Records FSR into frame.cb. The output image is left in VK_IMAGE_LAYOUT_GENERAL.
bool FsrDispatch(const FsrFrame& frame, char* log, size_t logSize);
void FsrShutdown();
