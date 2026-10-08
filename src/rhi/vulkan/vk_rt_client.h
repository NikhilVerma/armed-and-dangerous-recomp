#pragma once
// The game's side of the ray-tracing helper (rt_ipc.h): launches rt_helper.exe, creates the shared control block,
// the exported GPU buffers and timeline semaphores, and hands frames over. Used by VulkanDevice::RtShadows.

#include <cstdint>

#include "rt_ipc.h"
#include "vk_core.h"

namespace rhi::vulkan
{
class RtClient
{
public:
    // Starts the helper once (rt_helper.exe next to the game executable). False when it cannot run.
    bool Start();
    // The helper is ready and has accepted every frame requested so far (else: shadow maps this frame).
    bool Ready() const;
    // Shared buffers of at least these sizes (vertices, indices, depth/visibility grid). Reallocates (and tells the
    // helper to re-import) when they grow.
    bool Ensure(uint64_t vertices, uint64_t indices, uint32_t width, uint32_t height);
    VkBuffer Buffer(rtipc::Buffer b) const { return buffers_[b].buffer; }
    void* IndicesMapped() const { return buffers_[rtipc::kIndices].mapped; }
    VkSemaphore ToHelper() const { return toHelper_; }
    VkSemaphore FromHelper() const { return fromHelper_; }
    // After submitting the inputs of frame `p.frame` (signalling ToHelper = frame): wake the helper.
    void Request(const rtipc::FrameParams& p);
    // Whether the helper has submitted its work for `frame` (waits up to `ms` milliseconds).
    bool Accepted(uint64_t frame, int ms) const;
    // Temporal anti-aliasing (docs/subsystems/remaster-aa.md): shared colour / depth / motion / output buffers for a
    // width x height scene; its own frames and semaphores, independent of the ray tracing.
    bool EnsureAa(uint32_t width, uint32_t height);
    bool AaReady() const;  // the helper is running and its AA backend works
    VkSemaphore AaToHelper() const { return aaToHelper_; }
    VkSemaphore AaFromHelper() const { return aaFromHelper_; }
    void RequestAa(const rtipc::AaParams& p);
    bool AaAccepted(uint64_t frame, int ms) const;
    uint32_t AaActiveMode() const { return control_ ? control_->aaActiveMode : 0; }
    const char* AaLog() const { return control_ ? control_->aaLog : ""; }
    // The helper's GPU time for its last finished frame, microseconds (0 = not measured yet).
    uint32_t GpuMicros() const { return control_ ? control_->gpuMicros : 0; }
    void Shutdown();

private:
    struct Shared
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint64_t size = 0;
        uint32_t memoryType = 0;
        void* mapped = nullptr;
        HANDLE handle = nullptr;  // in this process
    };
    bool Alloc(rtipc::Buffer b, uint64_t size, VkBufferUsageFlags usage, bool host);
    bool Grow(uint32_t first, uint32_t last, const uint64_t* need);  // buffers first..last to at least need[]
    bool ExportSemaphore(VkSemaphore& s, uint64_t& handleOut);
    uint64_t Duplicate(HANDLE h) const;

    bool started_ = false, failed_ = false;
    HANDLE process_ = nullptr, mapping_ = nullptr, wake_ = nullptr;
    rtipc::Control* control_ = nullptr;
    Shared buffers_[rtipc::kBufferCount];
    VkSemaphore toHelper_ = VK_NULL_HANDLE, fromHelper_ = VK_NULL_HANDLE;
    VkSemaphore aaToHelper_ = VK_NULL_HANDLE, aaFromHelper_ = VK_NULL_HANDLE;
};

RtClient& Rt();
}  // namespace rhi::vulkan
