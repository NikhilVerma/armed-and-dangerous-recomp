#pragma once
// Ray-traced shadows in the helper: one jittered ray per pixel towards the sun's disc, denoised by AMD's FidelityFX
// Shadow Denoiser (third_party/FidelityFX-ShadowDenoiser, MIT, unmodified; dispatch sequence, resources and
// constants as the SDK's ffx_denoiser.cpp denoiserDispatchShadows). Our own passes only produce what the denoiser
// takes as input: depth, world-space normals and motion vectors (G-buffer pass) and the per-tile hit mask (trace).

#include <volk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rth
{
struct Image
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0, height = 0;
};

struct Buffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

// Per frame: the camera of this and the previous frame, the sun, the grid.
struct DenoiseFrame
{
    float proj[4];        // _11 _22 _33 _34 (D3D projection, row vectors)
    float proj43;         // _43
    float camToWorld[12]; // rows
    float prevView[16];   // previous world -> camera (row-major 4x4, column vectors)
    float prevProj[5];    // previous _11 _22 _33 _34 _43
    float sun[4];         // towards the sun, w: tan(angular radius)
    uint32_t width, height;
    uint64_t frame;
    bool first;           // no usable history (first frame, resize, camera cut)
    bool raw;             // debug: the undenoised one-ray result
};

class ShadowDenoiser
{
public:
    // Compiles every pass (glslang; FFX sources embedded). `log` receives errors.
    bool Init(VkDevice device, const VkPhysicalDeviceMemoryProperties& mem, std::string& log);
    // Records the whole chain into `cb`: G-buffer from `depth` (raw 0..1 depth per pixel, float, row-major), the
    // trace against `tlas`, the five FidelityFX passes, then the result (visibility 0..1, 1 = lit) into `visibility`.
    bool Record(VkCommandBuffer cb, const DenoiseFrame& f, VkBuffer depth, VkAccelerationStructureKHR tlas,
                VkBuffer visibility, std::string& log);

private:
    bool Resize(VkCommandBuffer cb, uint32_t w, uint32_t h);
    VkPipeline Pipeline(const std::string& source, VkPipelineLayout& layoutOut, VkDescriptorSetLayout& setOut,
                        const std::vector<VkDescriptorSetLayoutBinding>& bindings, std::string& log);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    struct Pass
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout set = VK_NULL_HANDLE;
    };
    Pass gbuffer_, trace_, prepare_, classify_, filter_[3], decode_;
    uint32_t w_ = 0, h_ = 0;
    Image depth_, prevDepth_, normal_, velocity_, hitMask_, moments_[2], scratch_[2], output_;
    Buffer hitBits_, tiles_, metadata_, constants_;
    bool fresh_ = true;  // history must be cleared
};

// Compiles every shader without a device (rt_helper.exe --selftest).
bool CompileAll(std::string& log);

// The embedded FidelityFX sources by file name (generated header, rthelper/CMakeLists.txt).
const char* FfxSource(const std::string& name);
}  // namespace rth
