// The game's side of the ray-tracing helper (vk_rt_client.h, rt_ipc.h).
#include "vk_rt_client.h"

#include <cstdio>
#include <cstring>

#include "log.h"

namespace rhi::vulkan
{
RtClient& Rt()
{
    static RtClient c;
    return c;
}

bool RtClient::Start()
{
    if (started_ || failed_)
        return started_ && !failed_;
    started_ = true;
    Context& c = Ctx();
    if (!c.externalSharing)
    {
        Log("rt: the device cannot share memory and semaphores with another process; no ray-traced shadows");
        failed_ = true;
        return false;
    }
    DWORD pid = GetCurrentProcessId();
    char name[64];
    std::snprintf(name, sizeof(name), "Local\\AndRtHelper-%lu", pid);
    mapping_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(rtipc::Control), name);
    if (!mapping_)
    {
        failed_ = true;
        return false;
    }
    control_ = static_cast<rtipc::Control*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rtipc::Control)));
    std::snprintf(name, sizeof(name), "Local\\AndRtHelperWake-%lu", pid);
    wake_ = CreateEventA(nullptr, FALSE, FALSE, name);
    if (!control_ || !wake_)
    {
        failed_ = true;
        return false;
    }
    std::memset(control_, 0, sizeof(*control_));
    control_->magic = rtipc::kMagic;
    control_->version = rtipc::kVersion;
    VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    p2.pNext = &id;
    vkGetPhysicalDeviceProperties2(c.physical, &p2);
    std::memcpy(control_->gpuLuid, id.deviceLUID, sizeof(control_->gpuLuid));

    // The helper: rt_helper.exe next to the game executable, given our pid.
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char* slash = std::strrchr(exe, '\\');
    std::strcpy(slash ? slash + 1 : exe, "rt_helper.exe");
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES)
    {
        Log("rt: %s not found; no ray-traced shadows", exe);
        failed_ = true;
        return false;
    }
    char cmd[MAX_PATH + 32];
    std::snprintf(cmd, sizeof(cmd), "\"%s\" %lu", exe, pid);
    STARTUPINFOA si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(exe, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        Log("rt: could not start %s (error %lu)", exe, GetLastError());
        failed_ = true;
        return false;
    }
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    control_->helperPid = pi.dwProcessId;
    if (!ExportSemaphore(toHelper_, control_->semToHelper) || !ExportSemaphore(fromHelper_, control_->semFromHelper) ||
        !ExportSemaphore(aaToHelper_, control_->semAaToHelper) ||
        !ExportSemaphore(aaFromHelper_, control_->semAaFromHelper))
    {
        Log("rt: could not create shared semaphores");
        failed_ = true;
        return false;
    }
    Log("rt: helper started (pid %lu)", pi.dwProcessId);
    return true;
}

bool RtClient::Ready() const
{
    if (!started_ || failed_ || !control_)
        return false;
    if (control_->helperState == 2)
    {
        static bool logged;
        if (!logged)
            Log("rt: helper failed: %s", control_->log), logged = true;
        return false;
    }
    if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0)
        return false;  // the helper exited
    // Ready, and caught up: it has submitted every frame requested so far (the shared input buffers are reused, and
    // the semaphores order its GPU work only once it has submitted). It usually runs about a frame behind, so wait
    // briefly for that instead of failing: failing made about one frame in nine fall back to shadow maps at 1080p,
    // and shadows flickered between the two looks while moving.
    if (control_->helperState != 1)
        return false;
    return control_->accepted >= control_->requested || Accepted(control_->requested, 6);
}

uint64_t RtClient::Duplicate(HANDLE h) const
{
    HANDLE out = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), h, process_, &out, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return 0;
    return reinterpret_cast<uintptr_t>(out);
}

bool RtClient::ExportSemaphore(VkSemaphore& s, uint64_t& handleOut)
{
    Context& c = Ctx();
    VkExportSemaphoreCreateInfo ex{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    ex.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkSemaphoreTypeCreateInfo ty{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    ty.pNext = &ex;
    ty.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    ty.initialValue = 0;
    VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    ci.pNext = &ty;
    if (vkCreateSemaphore(c.device, &ci, nullptr, &s) != VK_SUCCESS)
        return false;
    VkSemaphoreGetWin32HandleInfoKHR gi{VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR};
    gi.semaphore = s;
    gi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE h = nullptr;
    if (vkGetSemaphoreWin32HandleKHR(c.device, &gi, &h) != VK_SUCCESS || !h)
        return false;
    handleOut = Duplicate(h);
    CloseHandle(h);
    return handleOut != 0;
}

bool RtClient::Alloc(rtipc::Buffer b, uint64_t size, VkBufferUsageFlags usage, bool host)
{
    Context& c = Ctx();
    Shared& s = buffers_[b];
    if (s.buffer)
    {
        // The previous frames may still use it: release after the GPU is idle (reallocation is rare).
        vkDeviceWaitIdle(c.device);
        vkDestroyBuffer(c.device, s.buffer, nullptr);
        vkFreeMemory(c.device, s.memory, nullptr);
        if (s.handle)
            CloseHandle(s.handle);
        s = Shared{};
    }
    VkExternalMemoryBufferCreateInfo eb{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    eb.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.pNext = &eb;
    bi.size = size;
    bi.usage = usage;
    if (vkCreateBuffer(c.device, &bi, nullptr, &s.buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, s.buffer, &req);
    VkExportMemoryAllocateInfo ex{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
    ex.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    ded.buffer = s.buffer;
    ex.pNext = &ded;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.pNext = &ex;
    ai.allocationSize = req.size;
    s.memoryType = c.MemoryType(req.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                         : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ai.memoryTypeIndex = s.memoryType;
    if (vkAllocateMemory(c.device, &ai, nullptr, &s.memory) != VK_SUCCESS)
        return false;
    vkBindBufferMemory(c.device, s.buffer, s.memory, 0);
    if (host)
        vkMapMemory(c.device, s.memory, 0, req.size, 0, &s.mapped);
    VkMemoryGetWin32HandleInfoKHR gi{VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR};
    gi.memory = s.memory;
    gi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    if (vkGetMemoryWin32HandleKHR(c.device, &gi, &s.handle) != VK_SUCCESS || !s.handle)
        return false;
    s.size = req.size;
    rtipc::SharedBuffer& sb = control_->buffers[b];
    sb.handle = Duplicate(s.handle);
    sb.size = req.size;
    sb.memoryType = s.memoryType;
    return sb.handle != 0;
}

bool RtClient::Grow(uint32_t first, uint32_t last, const uint64_t* need)
{
    if (!started_ || failed_)
        return false;
    bool grew = false;
    for (uint32_t b = first; b <= last; ++b)
    {
        if (buffers_[b].buffer && buffers_[b].size >= need[b - first])
            continue;
        uint64_t size = need[b - first] + need[b - first] / 2 + 65536;
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (!Alloc(static_cast<rtipc::Buffer>(b), size, usage, b == rtipc::kIndices))
        {
            Log("rt: shared buffer %u (%llu bytes) failed", b, static_cast<unsigned long long>(size));
            failed_ = true;
            return false;
        }
        grew = true;
    }
    if (grew)
    {
        MemoryBarrier();
        control_->generation++;
        SetEvent(wake_);
    }
    // Wait (briefly) for the helper to import the new buffers before they are used.
    for (int i = 0; i < 2000 && control_->importedGeneration != control_->generation; ++i)
    {
        if (control_->helperState == 2)
            return false;
        Sleep(1);
    }
    return control_->importedGeneration == control_->generation;
}

bool RtClient::Ensure(uint64_t vertices, uint64_t indices, uint32_t width, uint32_t height)
{
    const uint64_t need[] = {vertices * 16, indices * 4, uint64_t(width) * height * 4, uint64_t(width) * height * 4};
    return Grow(rtipc::kVertices, rtipc::kVisibility, need);
}

bool RtClient::EnsureAa(uint32_t width, uint32_t height)
{
    const uint64_t px = uint64_t(width) * height;
    const uint64_t need[] = {px * 8, px * 4, px * 8, px * 8};
    return Grow(rtipc::kAaColor, rtipc::kAaOutput, need);
}

bool RtClient::AaReady() const
{
    if (!started_ || failed_ || !control_ || control_->helperState != 1 || control_->aaState == 2)
        return false;
    // Working is enough: each frame's handoff waits (briefly) for the previous one to be accepted (TakeHelperWaits).
    // Requiring it to be caught up here turned AA off every other frame, and each off frame reset the history.
    return WaitForSingleObject(process_, 0) != WAIT_OBJECT_0 && control_->aaState == 1;
}

void RtClient::RequestAa(const rtipc::AaParams& p)
{
    control_->aaParams[p.frame % rtipc::kRing] = p;
    MemoryBarrier();
    control_->aaRequested = p.frame;
    SetEvent(wake_);
}

bool RtClient::AaAccepted(uint64_t frame, int ms) const
{
    if (!control_)
        return false;
    LARGE_INTEGER f, start, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&start);
    while (control_->aaAccepted < frame)
    {
        if (control_->helperState != 1 || control_->aaState == 2)
            return false;
        QueryPerformanceCounter(&now);
        if ((now.QuadPart - start.QuadPart) * 1000 / f.QuadPart >= ms)
            return false;
        Sleep(0);
    }
    return true;
}

void RtClient::Request(const rtipc::FrameParams& p)
{
    control_->params[p.frame % rtipc::kRing] = p;
    MemoryBarrier();
    control_->requested = p.frame;
    SetEvent(wake_);
}

bool RtClient::Accepted(uint64_t frame, int ms) const
{
    if (!control_)
        return false;
    LARGE_INTEGER f, start, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&start);
    while (control_->accepted < frame)
    {
        if (control_->helperState != 1)
            return false;
        QueryPerformanceCounter(&now);
        if ((now.QuadPart - start.QuadPart) * 1000 / f.QuadPart >= ms)
            return false;
        Sleep(0);
    }
    return true;
}

void RtClient::Shutdown()
{
    if (control_)
    {
        control_->quit = 1;
        SetEvent(wake_);
    }
    if (process_)
    {
        WaitForSingleObject(process_, 1000);
        CloseHandle(process_);
        process_ = nullptr;
    }
}
}  // namespace rhi::vulkan
