// rt_helper.exe: the 64-bit ray-tracing helper for the game's Vulkan backend (protocol: src/rhi/vulkan/rt_ipc.h).
//
// Started by the game with its process id. Opens the shared control block, creates a Vulkan device with ray queries
// on the same GPU (matched by LUID), imports the game's timeline semaphores and buffers, then per frame: copies the
// world-space caster triangles into acceleration-structure inputs, builds one BLAS and one TLAS, and traces shadow
// rays from every pixel (the game's camera-space depth) towards the sun's disc, writing the visible fraction.
// Exits with the game.
#define NOMINMAX
#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <windows.h>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "rt_ipc.h"
#include "shadow_denoiser.h"

namespace
{
rtipc::Control* g_ctl;

void Fail(const char* fmt, ...)
{
    if (!g_ctl)
        return;
    va_list a;
    va_start(a, fmt);
    vsnprintf(g_ctl->log, sizeof(g_ctl->log), fmt, a);
    va_end(a);
    g_ctl->helperState = 2;
}

struct Buf
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDeviceAddress address = 0;
    void* mapped = nullptr;
};

VkInstance g_instance;
VkPhysicalDevice g_physical;
VkDevice g_device;
VkQueue g_queue;
uint32_t g_family;
VkPhysicalDeviceMemoryProperties g_mem;
VkDeviceSize g_scratchAlign = 256;

uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < g_mem.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (g_mem.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    return 0;
}

void Free(Buf& b)
{
    if (b.buffer)
        vkDestroyBuffer(g_device, b.buffer, nullptr);
    if (b.memory)
        vkFreeMemory(g_device, b.memory, nullptr);
    b = Buf{};
}

bool Create(Buf& b, VkDeviceSize size, VkBufferUsageFlags usage, bool host)
{
    Free(b);
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (vkCreateBuffer(g_device, &bi, nullptr, &b.buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_device, b.buffer, &req);
    VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.pNext = &fi;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = MemoryType(req.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                             : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(g_device, &ai, nullptr, &b.memory) != VK_SUCCESS)
        return false;
    vkBindBufferMemory(g_device, b.buffer, b.memory, 0);
    if (host)
        vkMapMemory(g_device, b.memory, 0, size, 0, &b.mapped);
    VkBufferDeviceAddressInfo da{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    da.buffer = b.buffer;
    b.address = vkGetBufferDeviceAddress(g_device, &da);
    b.size = size;
    return true;
}

bool Grow(Buf& b, VkDeviceSize size, VkBufferUsageFlags usage, bool host = false)
{
    if (b.buffer && b.size >= size)
        return true;
    vkDeviceWaitIdle(g_device);
    return Create(b, size + size / 2 + 65536, usage, host);
}

// One of the game's buffers, imported from its opaque Win32 handle.
bool Import(Buf& b, const rtipc::SharedBuffer& s)
{
    Free(b);
    VkExternalMemoryBufferCreateInfo eb{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    eb.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.pNext = &eb;
    bi.size = s.size;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(g_device, &bi, nullptr, &b.buffer) != VK_SUCCESS)
        return false;
    VkImportMemoryWin32HandleInfoKHR im{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
    im.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    im.handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(s.handle));
    VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    ded.buffer = b.buffer;
    im.pNext = &ded;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.pNext = &im;
    ai.allocationSize = s.size;
    ai.memoryTypeIndex = s.memoryType;
    VkResult r = vkAllocateMemory(g_device, &ai, nullptr, &b.memory);
    if (r != VK_SUCCESS)
    {
        Fail("import of a shared buffer failed (VkResult %d, memory type %u)", int(r), s.memoryType);
        return false;
    }
    vkBindBufferMemory(g_device, b.buffer, b.memory, 0);
    b.size = s.size;
    return true;
}

bool ImportSemaphore(VkSemaphore& s, uint64_t handle)
{
    VkSemaphoreTypeCreateInfo ty{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    ty.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    ci.pNext = &ty;
    if (vkCreateSemaphore(g_device, &ci, nullptr, &s) != VK_SUCCESS)
        return false;
    VkImportSemaphoreWin32HandleInfoKHR im{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
    im.semaphore = s;
    im.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    im.handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle));
    return vkImportSemaphoreWin32HandleKHR(g_device, &im) == VK_SUCCESS;
}

std::vector<uint32_t> Compile(const char* glsl)
{
    glslang::InitializeProcess();
    glslang::TShader shader(EShLangCompute);
    shader.setStrings(&glsl, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
    if (!shader.parse(GetDefaultResources(), 460, false, EShMsgDefault))
    {
        Fail("shader: %s", shader.getInfoLog());
        return {};
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault))
    {
        Fail("shader link: %s", program.getInfoLog());
        return {};
    }
    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), spirv);
    return spirv;
}


struct alignas(16) TraceParams
{
    float camToWorld[12];
    float proj[4];
    float sun[4];
    uint32_t dims[4];
};

bool CreateDevice()
{
    if (volkInitialize() != VK_SUCCESS)
        return Fail("no Vulkan loader"), false;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, nullptr, &g_instance) != VK_SUCCESS)
        return Fail("vkCreateInstance failed"), false;
    volkLoadInstance(g_instance);
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g_instance, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(g_instance, &n, devs.data());
    for (VkPhysicalDevice d : devs)
    {
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        p2.pNext = &id;
        vkGetPhysicalDeviceProperties2(d, &p2);
        if (id.deviceLUIDValid && !std::memcmp(id.deviceLUID, g_ctl->gpuLuid, VK_LUID_SIZE))
            g_physical = d;
    }
    if (!g_physical)
        return Fail("the game's GPU was not found (LUID)"), false;
    vkGetPhysicalDeviceMemoryProperties(g_physical, &g_mem);
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 pp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    pp.pNext = &asp;
    vkGetPhysicalDeviceProperties2(g_physical, &pp);
    g_scratchAlign = std::max<VkDeviceSize>(asp.minAccelerationStructureScratchOffsetAlignment, 1);
    vkGetPhysicalDeviceQueueFamilyProperties(g_physical, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fams(n);
    vkGetPhysicalDeviceQueueFamilyProperties(g_physical, &n, fams.data());
    for (uint32_t i = 0; i < n; ++i)
        if (fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            g_family = i;
            break;
        }
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = g_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    rq.rayQuery = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    as.accelerationStructure = VK_TRUE;
    as.pNext = &rq;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.synchronization2 = VK_TRUE;
    f13.pNext = &as;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.bufferDeviceAddress = VK_TRUE;
    f12.timelineSemaphore = VK_TRUE;
    f12.shaderFloat16 = VK_TRUE;  // FidelityFX FFX_HALF (its filter has no 32-bit path)
    f12.pNext = &f13;
    VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    f11.storageBuffer16BitAccess = VK_TRUE;
    f11.uniformAndStorageBuffer16BitAccess = VK_TRUE;
    f11.pNext = &f12;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f11;
    f2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;  // FidelityFX filter output
    f2.features.shaderInt16 = VK_TRUE;
    const char* ext[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                         VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
                         VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 6;
    dci.ppEnabledExtensionNames = ext;
    VkResult r = vkCreateDevice(g_physical, &dci, nullptr, &g_device);
    if (r != VK_SUCCESS)
        return Fail("vkCreateDevice with ray queries failed (VkResult %d)", int(r)), false;
    volkLoadDevice(g_device);
    vkGetDeviceQueue(g_device, g_family, 0, &g_queue);
    return true;
}
}  // namespace

// world -> camera (row-major 4x4, column vectors) from camera -> world rows (rotation + translation).
void WorldToCamera(const float c2w[12], float out[16])
{
    // The rotation is orthonormal: its transpose; translation -R^T t.
    for (int r = 0; r < 3; ++r)
    {
        float t = 0;
        for (int c = 0; c < 3; ++c)
            out[r * 4 + c] = c2w[c * 4 + r], t += c2w[c * 4 + r] * c2w[c * 4 + 3];
        out[r * 4 + 3] = -t;
    }
    out[12] = out[13] = out[14] = 0.0f, out[15] = 1.0f;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR cmd, int)
{
    // rt_helper.exe --selftest: compile every shader (no device, no game); result in rt_helper_selftest.txt.
    if (std::strstr(cmd, "--selftest"))
    {
        std::string log;
        bool ok = rth::CompileAll(log);
        if (FILE* f = std::fopen("rt_helper_selftest.txt", "w"))
            std::fprintf(f, "%s\n%s", ok ? "OK" : "FAILED", log.c_str()), std::fclose(f);
        return ok ? 0 : 1;
    }
    unsigned long pid = std::strtoul(cmd, nullptr, 10);
    char name[64];
    std::snprintf(name, sizeof(name), "Local\\AndRtHelper-%lu", pid);
    HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    std::snprintf(name, sizeof(name), "Local\\AndRtHelperWake-%lu", pid);
    HANDLE wake = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!mapping || !wake || !game)
        return 1;
    g_ctl = static_cast<rtipc::Control*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rtipc::Control)));
    if (!g_ctl || g_ctl->magic != rtipc::kMagic || g_ctl->version != rtipc::kVersion)
        return 1;
    if (!CreateDevice())
        return 1;

    // Semaphore handles are written by the game right after it starts us.
    for (int i = 0; i < 5000 && (!g_ctl->semToHelper || !g_ctl->semFromHelper); ++i)
        Sleep(1);
    VkSemaphore toHelper = VK_NULL_HANDLE, fromHelper = VK_NULL_HANDLE;
    if (!ImportSemaphore(toHelper, g_ctl->semToHelper) || !ImportSemaphore(fromHelper, g_ctl->semFromHelper))
        return Fail("semaphore import failed"), 1;

    rth::ShadowDenoiser denoiser;
    {
        std::string log;
        if (!denoiser.Init(g_device, g_mem, log))
            return Fail("denoiser: %s", log.c_str()), 1;
    }

    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp.queueFamilyIndex = g_family;
    VkCommandPool pool;
    vkCreateCommandPool(g_device, &cp, nullptr, &pool);
    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(g_device, &cai, &cb);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence;
    vkCreateFence(g_device, &fci, nullptr, &fence);

    Buf shared[rtipc::kBufferCount];
    Buf verts, indices, blasBuf, tlasBuf, scratch, instances, params;
    VkAccelerationStructureKHR blas = VK_NULL_HANDLE, tlas = VK_NULL_HANDLE;
    Create(instances, sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
    Create(params, sizeof(TraceParams) * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    g_ctl->helperState = 1;

    // GPU time per frame (two timestamps around the frame's work), reported in the control block.
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g_physical, &props);
    VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpi.queryCount = 2;
    VkQueryPool timer = VK_NULL_HANDLE;
    vkCreateQueryPool(g_device, &qpi, nullptr, &timer);
    bool timed = false;

    uint64_t done = 0;
    rtipc::FrameParams prev{};
    bool havePrev = false;
    while (!g_ctl->quit)
    {
        HANDLE waits[2] = {wake, game};
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, 100);
        if (w == WAIT_OBJECT_0 + 1)
            break;  // the game exited
        if (g_ctl->importedGeneration != g_ctl->generation)
        {
            vkWaitForFences(g_device, 1, &fence, VK_TRUE, UINT64_MAX);
            uint32_t gen = g_ctl->generation;
            for (uint32_t i = 0; i < rtipc::kBufferCount; ++i)
                if (!Import(shared[i], g_ctl->buffers[i]))
                    return 1;
            g_ctl->importedGeneration = gen;
        }
        uint64_t frame = g_ctl->requested;
        if (frame <= done || !shared[0].buffer)
            continue;
        const rtipc::FrameParams fp = g_ctl->params[frame % rtipc::kRing];
        if (fp.frame != frame || !fp.indexCount || !fp.vertexCount || !fp.width || !fp.height)
        {
            // Nothing to trace: pass the semaphore on so the game never waits in vain.
            VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
            wait.semaphore = toHelper, wait.value = frame, wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            VkSemaphoreSubmitInfo sig{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
            sig.semaphore = fromHelper, sig.value = frame, sig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
            si.waitSemaphoreInfoCount = 1, si.pWaitSemaphoreInfos = &wait;
            si.signalSemaphoreInfoCount = 1, si.pSignalSemaphoreInfos = &sig;
            vkQueueSubmit2(g_queue, 1, &si, VK_NULL_HANDLE);
            done = frame;
            g_ctl->accepted = frame;
            continue;
        }
        vkWaitForFences(g_device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(g_device, 1, &fence);
        uint64_t stamps[2];
        if (timed && vkGetQueryPoolResults(g_device, timer, 0, 2, sizeof(stamps), stamps, 8,
                                           VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            g_ctl->gpuMicros = static_cast<uint32_t>(double(stamps[1] - stamps[0]) * props.limits.timestampPeriod / 1000.0);
        const VkBufferUsageFlags asIn = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (!Grow(verts, VkDeviceSize(fp.vertexCount) * 16, asIn) ||
            !Grow(indices, VkDeviceSize(fp.indexCount) * 4, asIn))
            return Fail("local buffers failed"), 1;

        // Sizes, AS objects.
        VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        auto& tr = geo.geometry.triangles;
        tr.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        tr.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        tr.vertexData.deviceAddress = verts.address;
        tr.vertexStride = 16;
        tr.maxVertex = fp.vertexCount - 1;
        tr.indexType = VK_INDEX_TYPE_UINT32;
        tr.indexData.deviceAddress = indices.address;
        VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        // Rebuilt every frame: prefer fast build (measured, rt_helper_test RT_PERF, 1080p, 324k triangles, whole
        // helper frame: 0.94 ms fast build, 1.16 ms fast trace).
        bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        bi.geometryCount = 1;
        bi.pGeometries = &geo;
        uint32_t triangles = fp.indexCount / 3;
        VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(g_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi,
                                                &triangles, &sz);
        auto ensureAs = [&](Buf& buf, VkAccelerationStructureKHR& as, VkDeviceSize size,
                            VkAccelerationStructureTypeKHR type) {
            if (as && buf.size >= size)
                return true;
            vkDeviceWaitIdle(g_device);
            if (as)
                vkDestroyAccelerationStructureKHR(g_device, as, nullptr), as = VK_NULL_HANDLE;
            if (!Create(buf, size + size / 2, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false))
                return false;
            VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
            ci.buffer = buf.buffer;
            ci.size = buf.size;
            ci.type = type;
            return vkCreateAccelerationStructureKHR(g_device, &ci, nullptr, &as) == VK_SUCCESS;
        };
        VkAccelerationStructureGeometryKHR tgeo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        tgeo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        tgeo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        tgeo.geometry.instances.data.deviceAddress = instances.address;
        VkAccelerationStructureBuildGeometryInfoKHR tbi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        tbi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        tbi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        tbi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        tbi.geometryCount = 1;
        tbi.pGeometries = &tgeo;
        const uint32_t one = 1;
        VkAccelerationStructureBuildSizesInfoKHR tsz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(g_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tbi, &one,
                                                &tsz);
        if (!ensureAs(blasBuf, blas, sz.accelerationStructureSize, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR) ||
            !ensureAs(tlasBuf, tlas, tsz.accelerationStructureSize, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR) ||
            !Grow(scratch, std::max(sz.buildScratchSize, tsz.buildScratchSize) + g_scratchAlign,
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
            return Fail("acceleration structures failed"), 1;
        VkDeviceAddress scratchAddr = (scratch.address + g_scratchAlign - 1) / g_scratchAlign * g_scratchAlign;
        VkAccelerationStructureDeviceAddressInfoKHR dai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        dai.accelerationStructure = blas;
        VkAccelerationStructureInstanceKHR inst{};
        inst.transform.matrix[0][0] = inst.transform.matrix[1][1] = inst.transform.matrix[2][2] = 1.0f;
        inst.mask = 0xff;
        inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(g_device, &dai);
        std::memcpy(instances.mapped, &inst, sizeof(inst));
        (void)params;

        // Record: copy inputs, build, trace.
        vkResetCommandBuffer(cb, 0);
        VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &cbi);
        if (timer)
        {
            vkCmdResetQueryPool(cb, timer, 0, 2);
            vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, timer, 0);
        }
        VkBufferCopy vc{0, 0, VkDeviceSize(fp.vertexCount) * 16};
        vkCmdCopyBuffer(cb, shared[rtipc::kVertices].buffer, verts.buffer, 1, &vc);
        VkBufferCopy ic{0, 0, VkDeviceSize(fp.indexCount) * 4};
        vkCmdCopyBuffer(cb, shared[rtipc::kIndices].buffer, indices.buffer, 1, &ic);
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        di.memoryBarrierCount = 1;
        di.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cb, &di);
        bi.dstAccelerationStructure = blas;
        bi.scratchData.deviceAddress = scratchAddr;
        VkAccelerationStructureBuildRangeInfoKHR range{triangles, 0, 0, 0};
        const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
        vkCmdBuildAccelerationStructuresKHR(cb, 1, &bi, &ranges);
        mb.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        mb.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        mb.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier2(cb, &di);
        tbi.dstAccelerationStructure = tlas;
        tbi.scratchData.deviceAddress = scratchAddr;
        VkAccelerationStructureBuildRangeInfoKHR trange{1, 0, 0, 0};
        const VkAccelerationStructureBuildRangeInfoKHR* tranges = &trange;
        vkCmdBuildAccelerationStructuresKHR(cb, 1, &tbi, &tranges);
        mb.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        mb.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier2(cb, &di);
        rth::DenoiseFrame df{};
        std::memcpy(df.proj, fp.proj, sizeof(df.proj));
        df.proj43 = fp.proj43;
        std::memcpy(df.camToWorld, fp.camToWorld, sizeof(df.camToWorld));
        // The previous frame's camera (world -> camera = inverse of camera -> world) and projection.
        const rtipc::FrameParams& pv = havePrev ? prev : fp;
        WorldToCamera(pv.camToWorld, df.prevView);
        std::memcpy(df.prevProj, pv.proj, sizeof(pv.proj));
        df.prevProj[4] = pv.proj43;
        std::memcpy(df.sun, fp.sun, sizeof(df.sun));
        df.width = fp.width, df.height = fp.height;
        df.frame = frame;
        df.raw = fp.debug == 1;
        df.first = fp.debug == 2 || !havePrev || frame != prev.frame + 1 || prev.width != fp.width || prev.height != fp.height;
        std::string dlog;
        if (!denoiser.Record(cb, df, shared[rtipc::kDepth].buffer, tlas, shared[rtipc::kVisibility].buffer, dlog))
            return Fail("denoiser: %s", dlog.c_str()), 1;
        prev = fp;
        havePrev = true;
        if (timer)
            vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, timer, 1), timed = true;
        vkEndCommandBuffer(cb);

        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait.semaphore = toHelper, wait.value = frame, wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSemaphoreSubmitInfo sig{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        sig.semaphore = fromHelper, sig.value = frame, sig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkCommandBufferSubmitInfo cbs{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cbs.commandBuffer = cb;
        VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        si.waitSemaphoreInfoCount = 1, si.pWaitSemaphoreInfos = &wait;
        si.commandBufferInfoCount = 1, si.pCommandBufferInfos = &cbs;
        si.signalSemaphoreInfoCount = 1, si.pSignalSemaphoreInfos = &sig;
        if (vkQueueSubmit2(g_queue, 1, &si, fence) != VK_SUCCESS)
            return Fail("submit failed"), 1;
        done = frame;
        g_ctl->accepted = frame;
    }
    vkDeviceWaitIdle(g_device);
    return 0;
}
