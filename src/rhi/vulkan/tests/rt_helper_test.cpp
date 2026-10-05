// End-to-end test of ray-traced shadows through the 64-bit helper, without the game and without a display
// (rt_ipc.h, vk_rt_client.h, rthelper/). Plays the game's part with a known scene: a floor (z = 0) and a box
// (2 x 2 x 4) at the origin, a camera 30 units above looking straight down, the sun from +x (elevation ~63 deg).
// The box must shadow the floor on its -x side; open floor and the box top must be lit.
//
// Usage: rt_helper_test.exe (rt_helper.exe next to it). Writes rt_helper_test.pgm (the visibility) and prints PASS
// or FAIL with the measured visibilities. Environment: RT_DEBUG=1 raw one-ray result, 2 no history; RT_PERF=1 a
// 1920 x 1080 frame with a 400 x 400 floor grid and 400 extra boxes (~330k triangles), printing the helper's GPU time.
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../vk_core.h"
#include "../vk_rt_client.h"

// The game's log and configuration, stubbed.
void Log(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    std::printf("\n");
    va_end(a);
}
int ConfigInt(const char*, const char*, int fallback) { return fallback; }

using namespace rhi::vulkan;

namespace
{
uint32_t W = 256, H = 256;
constexpr float kNear = 1.0f, kFar = 1000.0f, kFocal = 1.5f, kCamZ = 30.0f;
// D3D right-handed projection terms (camera looks along -z): depth = (z _33 + _43) / (z _34).
const float p33 = kFar / (kNear - kFar), p43 = kNear * kFar / (kNear - kFar), p34 = -1.0f;

void Upload(VkBuffer dst, const void* data, VkDeviceSize size)
{
    Context& c = Ctx();
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging;
    vkCreateBuffer(c.device, &bi, nullptr, &staging);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, staging, &req);
    VkDeviceMemory mem = c.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(c.device, staging, mem, 0);
    void* p;
    vkMapMemory(c.device, mem, 0, size, 0, &p);
    std::memcpy(p, data, size);
    vkUnmapMemory(c.device, mem);
    VkCommandBuffer cb = c.BeginOneShot();
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cb, staging, dst, 1, &region);
    c.EndOneShot(cb);
    vkDestroyBuffer(c.device, staging, nullptr);
    vkFreeMemory(c.device, mem, nullptr);
}

std::vector<float> Download(VkBuffer src, VkDeviceSize size)
{
    Context& c = Ctx();
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer staging;
    vkCreateBuffer(c.device, &bi, nullptr, &staging);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, staging, &req);
    VkDeviceMemory mem = c.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(c.device, staging, mem, 0);
    VkCommandBuffer cb = c.BeginOneShot();
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cb, src, staging, 1, &region);
    c.EndOneShot(cb);
    std::vector<float> out(size / 4);
    void* p;
    vkMapMemory(c.device, mem, 0, size, 0, &p);
    std::memcpy(out.data(), p, size);
    vkUnmapMemory(c.device, mem);
    vkDestroyBuffer(c.device, staging, nullptr);
    vkFreeMemory(c.device, mem, nullptr);
    return out;
}

// Depth of the scene at pixel (x, y): the floor, or the box top inside its footprint (the camera looks down).
float SceneDepth(uint32_t x, uint32_t y)
{
    float ndcX = (x + 0.5f) / W * 2.0f - 1.0f, ndcY = 1.0f - (y + 0.5f) / H * 2.0f;
    ndcX *= float(W) / float(H);  // square pixels (_11 = _22)
    // A camera-space ray (ndc.x / _11, ndc.y / _22, -1) hits world height h at camera z = -(kCamZ - h).
    for (float h : {4.0f, 0.0f})
    {
        float z = -(kCamZ - h);
        float wx = ndcX * -z / kFocal, wy = ndcY * -z / kFocal;
        if (h == 0.0f || (std::fabs(wx) <= 1.0f && std::fabs(wy) <= 1.0f))
            return (z * p33 + p43) / (z * p34);
    }
    return 1.0f;
}
}  // namespace

int main()
{
    const bool perf = GetEnvironmentVariableA("RT_PERF", nullptr, 0) != 0;
    if (perf)
        W = 1920, H = 1080;
    if (!ContextInit())
        return std::printf("FAIL: no Vulkan\n"), 1;
    RtClient& rc = Rt();
    if (!rc.Start())
        return std::printf("FAIL: helper did not start\n"), 1;
    for (int i = 0; i < 5000 && !rc.Ready(); ++i)
        Sleep(1);
    if (!rc.Ready())
        return std::printf("FAIL: helper not ready\n"), 1;

    // Geometry: floor (2 triangles) and the box (5 faces, 10 triangles), world space, w = 1.
    std::vector<float> v;
    auto vert = [&](float x, float y, float z) { v.insert(v.end(), {x, y, z, 1.0f}); };
    std::vector<uint32_t> idx;
    auto quad = [&](float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz,
                    float dx, float dy, float dz) {
        uint32_t base = static_cast<uint32_t>(v.size() / 4);
        vert(ax, ay, az), vert(bx, by, bz), vert(cx, cy, cz), vert(dx, dy, dz);
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    if (perf)
    {
        // The floor as a 400 x 400 grid and a 20 x 20 field of boxes (tops only matter little: all faces).
        const int N = 400;
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i)
            {
                float x0 = -50 + 100.0f * i / N, x1 = -50 + 100.0f * (i + 1) / N;
                float y0 = -50 + 100.0f * j / N, y1 = -50 + 100.0f * (j + 1) / N;
                quad(x0, y0, 0, x1, y0, 0, x1, y1, 0, x0, y1, 0);
            }
        for (int j = 0; j < 20; ++j)
            for (int i = 0; i < 20; ++i)
            {
                float cx = -45 + 4.7f * i, cy = -45 + 4.7f * j;
                if (std::fabs(cx) < 6 && std::fabs(cy) < 6)
                    continue;
                float a = cx - 0.3f, b = cx + 0.3f, c = cy - 0.3f, d = cy + 0.3f, h = 6.0f;
                quad(a, c, h, b, c, h, b, d, h, a, d, h);
                quad(a, c, 0, b, c, 0, b, c, h, a, c, h);
                quad(b, c, 0, b, d, 0, b, d, h, b, c, h);
                quad(b, d, 0, a, d, 0, a, d, h, b, d, h);
                quad(a, d, 0, a, c, 0, a, c, h, a, d, h);
            }
    }
    else
        quad(-50, -50, 0, 50, -50, 0, 50, 50, 0, -50, 50, 0);  // floor
    quad(-1, -1, 4, 1, -1, 4, 1, 1, 4, -1, 1, 4);          // top
    quad(-1, -1, 0, 1, -1, 0, 1, -1, 4, -1, -1, 4);        // sides
    quad(1, -1, 0, 1, 1, 0, 1, 1, 4, 1, -1, 4);
    quad(1, 1, 0, -1, 1, 0, -1, 1, 4, 1, 1, 4);
    quad(-1, 1, 0, -1, -1, 0, -1, -1, 4, -1, 1, 4);
    std::vector<float> depth(W * H);
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x)
            depth[y * W + x] = SceneDepth(x, y);
    if (!rc.Ensure(v.size() / 4, idx.size(), W, H))
        return std::printf("FAIL: shared buffers\n"), 1;
    Upload(rc.Buffer(rtipc::kVertices), v.data(), v.size() * 4);
    std::memcpy(rc.IndicesMapped(), idx.data(), idx.size() * 4);
    Upload(rc.Buffer(rtipc::kDepth), depth.data(), depth.size() * 4);

    // Frames: signal the helper, wait for its result (the denoiser accumulates over frames).
    Context& c = Ctx();
    std::vector<float> vis;
    const float sun[3] = {1.0f, 0.0f, 2.0f};
    const float len = std::sqrt(5.0f);
    const uint64_t frames = perf ? 96 : 24;
    uint64_t micros = 0;
    for (uint64_t frame = 1; frame <= frames; ++frame)
    {
        VkTimelineSemaphoreSubmitInfo ts{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        ts.signalSemaphoreValueCount = 1;
        ts.pSignalSemaphoreValues = &frame;
        VkSemaphore to = rc.ToHelper();
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.pNext = &ts;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &to;
        vkQueueSubmit(c.queue, 1, &si, VK_NULL_HANDLE);
        rtipc::FrameParams fp{};
        fp.frame = frame;
        fp.vertexCount = static_cast<uint32_t>(v.size() / 4);
        fp.indexCount = static_cast<uint32_t>(idx.size());
        fp.width = W, fp.height = H;
        fp.rays = 1;
        char dbg[8] = {};
        fp.debug = GetEnvironmentVariableA("RT_DEBUG", dbg, sizeof(dbg)) ? static_cast<uint32_t>(atoi(dbg)) : 0;
        const float c2w[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, kCamZ};
        std::memcpy(fp.camToWorld, c2w, sizeof(c2w));
        fp.proj[0] = kFocal * float(H) / float(W), fp.proj[1] = kFocal, fp.proj[2] = p33, fp.proj[3] = p34;
        fp.proj43 = p43;
        fp.sun[0] = sun[0] / len, fp.sun[1] = sun[1] / len, fp.sun[2] = sun[2] / len;
        fp.sun[3] = std::tan(0.6f * 3.14159265f / 180.0f);
        rc.Request(fp);
        if (!rc.Accepted(frame, 2000))
            return std::printf("FAIL: frame %llu not accepted\n", static_cast<unsigned long long>(frame)), 1;
        VkSemaphore from = rc.FromHelper();
        VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wi.semaphoreCount = 1;
        wi.pSemaphores = &from;
        wi.pValues = &frame;
        if (vkWaitSemaphores(c.device, &wi, 2000000000ull) != VK_SUCCESS)
            return std::printf("FAIL: helper result timed out (frame %llu)\n", static_cast<unsigned long long>(frame)), 1;
        if (frame > frames - 32)
            micros += rc.GpuMicros();  // the previous frame's, measured by the helper
    }
    if (perf)
        std::printf("helper GPU time: %.2f ms per frame (%u x %u, %zu triangles)\n", micros / 32.0 / 1000.0, W, H, idx.size() / 3);
    vis = Download(rc.Buffer(rtipc::kVisibility), uint64_t(W) * H * 4);

    // Probes (world -> pixel: x = (wx * focal / 30 + 1) / 2 * W, y = (1 - wy * focal / 30) / 2 * H on the floor).
    auto pixel = [](float wx, float wy) {
        uint32_t x = static_cast<uint32_t>((wx * kFocal / kCamZ * float(H) / float(W) + 1.0f) * 0.5f * W);
        uint32_t y = static_cast<uint32_t>((1.0f - wy * kFocal / kCamZ) * 0.5f * H);
        return y * W + x;
    };
    float shadow = vis[pixel(-2.5f, 0.0f)];  // the box's shadow: 4 / 2 = 2 units long on the -x side
    float open = vis[pixel(6.0f, 6.0f)];
    float lee = vis[pixel(4.0f, 0.0f)];       // the sunny side
    if (FILE* f = std::fopen("rt_helper_test.pgm", "wb"))
    {
        std::fprintf(f, "P5\n%u %u\n255\n", W, H);
        for (float x : vis)
            std::fputc(static_cast<int>(std::fmax(0.0f, std::fmin(1.0f, x)) * 255.0f), f);
        std::fclose(f);
    }
    bool ok = shadow < 0.25f && open > 0.75f && lee > 0.75f;
    std::printf("%s: shadow %.2f, open floor %.2f, sunny side %.2f\n", ok ? "PASS" : "FAIL", shadow, open, lee);
    rc.Shutdown();
    return ok ? 0 : 1;
}
