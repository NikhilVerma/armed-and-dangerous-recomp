// rhi::vulkan: instance, device, memory helpers and format table.
#define VOLK_IMPLEMENTATION
#include "vk_core.h"

#include <cstring>
#include <vector>

#include "config.h"
#include "log.h"

namespace rhi::vulkan
{
namespace
{
Context g_ctx;
}

Context& Ctx()
{
    return g_ctx;
}

void LogVk(const char* what, VkResult r)
{
    Log("vulkan: %s failed (VkResult %d)", what, static_cast<int>(r));
}

bool ContextInit()
{
    Context& c = g_ctx;
    if (c.device)
        return true;
    if (volkInitialize() != VK_SUCCESS)
    {
        Log("vulkan: no Vulkan loader (vulkan-1.dll)");
        return false;
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Armed and Dangerous (and-decomp)";
    app.apiVersion = VK_API_VERSION_1_3;
    const char* instanceExt[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = instanceExt;
    VkResult r = vkCreateInstance(&ici, nullptr, &c.instance);
    if (r != VK_SUCCESS)
        return LogVk("vkCreateInstance", r), false;
    volkLoadInstance(c.instance);

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(c.instance, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(c.instance, &n, devs.data());
    for (VkPhysicalDevice d : devs)  // prefer a discrete GPU with Vulkan 1.3
    {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_3)
            continue;
        if (!c.physical || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            c.physical = d, c.properties = p;
    }
    if (!c.physical)
    {
        Log("vulkan: no Vulkan 1.3 device");
        return false;
    }
    vkGetPhysicalDeviceMemoryProperties(c.physical, &c.memory);

    vkGetPhysicalDeviceQueueFamilyProperties(c.physical, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fams(n);
    vkGetPhysicalDeviceQueueFamilyProperties(c.physical, &n, fams.data());
    for (uint32_t i = 0; i < n; ++i)
        if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            c.queueFamily = i;
            break;
        }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = c.queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    f2.features.samplerAnisotropy = VK_TRUE;
    f2.features.fillModeNonSolid = VK_TRUE;  // D3DFILL_WIREFRAME ([render] wireframe)
    f2.features.textureCompressionBC = VK_TRUE;
    f2.features.depthBiasClamp = VK_TRUE;
    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(c.physical, &supported);
    f2.features.tessellationShader = supported.tessellationShader;  // remaster: tessellation ([render] tessellation)
    c.tessellation = supported.tessellationShader == VK_TRUE;
    f2.features.vertexPipelineStoresAndAtomics = supported.vertexPipelineStoresAndAtomics;  // scene vertex capture
    c.vertexStores = supported.vertexPipelineStoresAndAtomics == VK_TRUE;
    f2.features.dualSrcBlend = supported.dualSrcBlend;  // remaster: sky-tinted shadows (vk_shadows.cpp)
    // AMD FSR 3.1 (hook/fsr/fsr_bridge.h): its backend picks 16-bit shader variants from what the GPU supports, so
    // the device must enable what they use (else its passes write nothing: the scene came out black).
    f2.features.shaderInt16 = supported.shaderInt16;
    f2.features.shaderStorageImageWriteWithoutFormat = supported.shaderStorageImageWriteWithoutFormat;
    // Ray queries for ray-traced shadows, when the device has them (vk_rt.h).
    std::vector<const char*> deviceExt = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    {
        uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(c.physical, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> exts(count);
        vkEnumerateDeviceExtensionProperties(c.physical, nullptr, &count, exts.data());
        auto has = [&](const char* name) {
            for (const VkExtensionProperties& e : exts)
                if (!std::strcmp(e.extensionName, name))
                    return true;
            return false;
        };
        for (const VkExtensionProperties& e : exts)
            if (std::strstr(e.extensionName, "external_") || std::strstr(e.extensionName, "timeline"))
                Log("vulkan: extension %s", e.extensionName);
        Log("vulkan: %u device extensions; acceleration_structure %d ray_query %d deferred_host_operations %d", count,
            has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME), has(VK_KHR_RAY_QUERY_EXTENSION_NAME),
            has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME));
        c.rayQuery = has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) && has(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
                     has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        c.externalSharing =
            has(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) && has(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
    }
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR fas{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR frq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceVulkan12Features s12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features s11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan13Features s13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 sf2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    sf2.pNext = &s12;
    s12.pNext = &s11;
    s11.pNext = &s13;
    vkGetPhysicalDeviceFeatures2(c.physical, &sf2);
    // FSR 3.1's 16-bit variants and subgroup control (see shaderInt16 above).
    VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    f11.storageBuffer16BitAccess = s11.storageBuffer16BitAccess;
    f11.uniformAndStorageBuffer16BitAccess = s11.uniformAndStorageBuffer16BitAccess;
    f12.shaderFloat16 = s12.shaderFloat16;
    f13.subgroupSizeControl = s13.subgroupSizeControl;
    f13.computeFullSubgroups = s13.computeFullSubgroups;
    c.externalSharing = c.externalSharing && s12.timelineSemaphore && ConfigInt("render", "rt_share", 1) != 0;
    if (c.externalSharing)
    {
        deviceExt.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
        deviceExt.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
        f12.timelineSemaphore = VK_TRUE;
    }
    f13.pNext = &f11;
    f11.pNext = &f12;
    if (c.rayQuery)
    {
        deviceExt.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExt.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        deviceExt.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        f12.bufferDeviceAddress = VK_TRUE;
        fas.accelerationStructure = VK_TRUE;
        frq.rayQuery = VK_TRUE;
        f12.pNext = &fas;
        fas.pNext = &frq;
    }
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(deviceExt.size());
    dci.ppEnabledExtensionNames = deviceExt.data();
    r = vkCreateDevice(c.physical, &dci, nullptr, &c.device);
    if (r != VK_SUCCESS)
        return LogVk("vkCreateDevice", r), false;
    volkLoadDevice(c.device);
    Log("vulkan: ray queries %s; sharing with other processes %s", c.rayQuery ? "available" : "not available",
        c.externalSharing ? "available (ray-tracing helper)" : "not available");
    vkGetDeviceQueue(c.device, c.queueFamily, 0, &c.queue);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = c.queueFamily;
    vkCreateCommandPool(c.device, &pci, nullptr, &c.commandPool);
    Log("vulkan: %s (driver %u.%u), Vulkan %u.%u", c.properties.deviceName, c.properties.driverVersion >> 22,
        (c.properties.driverVersion >> 14) & 0xff, VK_API_VERSION_MAJOR(c.properties.apiVersion),
        VK_API_VERSION_MINOR(c.properties.apiVersion));
    return true;
}

uint32_t Context::MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const
{
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    return UINT32_MAX;
}

VkDeviceMemory Context::Allocate(VkMemoryRequirements req, VkMemoryPropertyFlags flags) const
{
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = MemoryType(req.memoryTypeBits, flags);
    VkDeviceMemory m = VK_NULL_HANDLE;
    VkResult r = vkAllocateMemory(device, &ai, nullptr, &m);
    if (r != VK_SUCCESS)
        LogVk("vkAllocateMemory", r);
    return m;
}

VkDeviceMemory Context::AllocateAddressable(VkMemoryRequirements req, VkMemoryPropertyFlags flags) const
{
    VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.pNext = &fi;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = MemoryType(req.memoryTypeBits, flags);
    VkDeviceMemory m = VK_NULL_HANDLE;
    VkResult r = vkAllocateMemory(device, &ai, nullptr, &m);
    if (r != VK_SUCCESS)
        LogVk("vkAllocateMemory (device address)", r);
    return m;
}

VkCommandBuffer Context::BeginOneShot()
{
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(device, &ai, &cb);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    return cb;
}

void Context::EndOneShot(VkCommandBuffer cb)
{
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &cb);
}

FormatInfo Format(D3DFORMAT f)
{
    constexpr VkComponentSwizzle I = VK_COMPONENT_SWIZZLE_IDENTITY, R = VK_COMPONENT_SWIZZLE_R,
                                 G = VK_COMPONENT_SWIZZLE_G, ONE = VK_COMPONENT_SWIZZLE_ONE,
                                 ZERO = VK_COMPONENT_SWIZZLE_ZERO;
    FormatInfo i;
    i.swizzle = {I, I, I, I};
    switch (f)
    {
    case D3DFMT_A8R8G8B8: i.format = VK_FORMAT_B8G8R8A8_UNORM, i.blockBytes = 4; break;
    case D3DFMT_X8R8G8B8: i.format = VK_FORMAT_B8G8R8A8_UNORM, i.blockBytes = 4, i.swizzle.a = ONE; break;
    case D3DFMT_R5G6B5: i.format = VK_FORMAT_R5G6B5_UNORM_PACK16, i.blockBytes = 2; break;
    case D3DFMT_A1R5G5B5: i.format = VK_FORMAT_A1R5G5B5_UNORM_PACK16, i.blockBytes = 2; break;
    case D3DFMT_X1R5G5B5: i.format = VK_FORMAT_A1R5G5B5_UNORM_PACK16, i.blockBytes = 2, i.swizzle.a = ONE; break;
    case D3DFMT_A4R4G4B4: i.format = VK_FORMAT_A4R4G4B4_UNORM_PACK16, i.blockBytes = 2; break;
    case D3DFMT_L8: i.format = VK_FORMAT_R8_UNORM, i.blockBytes = 1, i.swizzle = {R, R, R, ONE}; break;
    case D3DFMT_A8: i.format = VK_FORMAT_R8_UNORM, i.blockBytes = 1, i.swizzle = {ZERO, ZERO, ZERO, R}; break;
    case D3DFMT_A8L8: i.format = VK_FORMAT_R8G8_UNORM, i.blockBytes = 2, i.swizzle = {R, R, R, G}; break;
    case D3DFMT_V8U8: i.format = VK_FORMAT_R8G8_SNORM, i.blockBytes = 2; break;
    case D3DFMT_DXT1: i.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK, i.blockBytes = 8, i.compressed = true; break;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3: i.format = VK_FORMAT_BC2_UNORM_BLOCK, i.blockBytes = 16, i.compressed = true; break;
    case D3DFMT_DXT4:
    case D3DFMT_DXT5: i.format = VK_FORMAT_BC3_UNORM_BLOCK, i.blockBytes = 16, i.compressed = true; break;
    case D3DFMT_D16: i.format = VK_FORMAT_D16_UNORM, i.blockBytes = 2, i.depth = true; break;
    case D3DFMT_D24X8:
    case D3DFMT_D24S8:
    case D3DFMT_D24X4S4: i.format = VK_FORMAT_D24_UNORM_S8_UINT, i.blockBytes = 4, i.depth = i.stencil = true; break;
    case D3DFMT_D32: i.format = VK_FORMAT_D32_SFLOAT, i.blockBytes = 4, i.depth = true; break;
    default: break;
    }
    return i;
}

void LevelLayout(D3DFORMAT f, uint32_t width, uint32_t height, uint32_t* pitch, uint32_t* rows)
{
    FormatInfo i = Format(f);
    if (i.compressed)
    {
        *pitch = ((width + 3) / 4) * i.blockBytes;
        *rows = (height + 3) / 4;
    }
    else
    {
        *pitch = width * (i.blockBytes ? i.blockBytes : 4);
        *rows = height;
    }
}
}  // namespace rhi::vulkan
