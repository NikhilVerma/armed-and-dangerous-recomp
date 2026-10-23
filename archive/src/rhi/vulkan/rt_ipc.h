#pragma once
// Ray-traced shadows across processes: the game is 32-bit, and NVIDIA's 32-bit Vulkan driver has no ray tracing
// (measured: 270 device extensions, none of VK_KHR_acceleration_structure / ray_query). A 64-bit helper process
// (rthelper/, rt_helper.exe next to the game) uses the RT cores on the same GPU; this header is the protocol, shared
// by both sides (fixed-size types only: the same layout in 32 and 64 bits).
//
// Shared GPU memory (exported by the game, VK_KHR_external_memory_win32, opaque Win32 handles duplicated into the
// helper): world-space caster vertices (vec4), triangle indices (uint32), the scene depth as camera-space z
// (float per pixel), and the visibility the helper writes (float per pixel, 1 = lit). Two timeline semaphores
// (VK_KHR_external_semaphore_win32): the game signals `toHelper` = frame N when its inputs for N are written; the
// helper's work waits for it and signals `fromHelper` = N; the game's second half of frame N waits for that before
// applying the visibility (VulkanDevice::RtShadows). The game waits only for frames the helper has acknowledged
// (`accepted`), so a missing or stalled helper can never hang the GPU: the game falls back to shadow maps.
//
// Control block: a named file mapping "Local\\AndRtHelper-<game pid>" (RtControl), plus an auto-reset event
// "Local\\AndRtHelperWake-<game pid>" the game sets after each request.

#include <cstdint>

namespace rtipc
{
constexpr uint32_t kMagic = 0x52544831;  // "RTH1"
constexpr uint32_t kVersion = 4;  // 4: temporal anti-aliasing (docs/subsystems/remaster-aa.md)
constexpr uint32_t kRing = 4;  // frame parameter slots (frame N uses N % kRing)

enum Buffer : uint32_t
{
    kVertices,    // vec4 world positions (w = 1)
    kIndices,     // uint32, three per triangle
    kDepth,       // float depth 0..1 per pixel (D3D convention, row-major, viewport size); 1 = sky
    kVisibility,  // float per pixel, written by the helper
    // Temporal anti-aliasing (AaParams; allocated only while AA is on, handle 0 otherwise):
    kAaColor,     // RGBA16F per pixel: the scene before the UI (row-major, width x height)
    kAaDepth,     // float depth 0..1 per pixel (D3D convention)
    kAaMotion,    // RGBA16F per pixel: xy = prevUv - curUv (0..1 texture coordinates, y down), zw unused
    kAaOutput,    // RGBA16F per pixel, written by the helper
    kBufferCount
};

struct SharedBuffer
{
    uint64_t handle;      // Win32 handle valid in the helper process
    uint64_t size;        // bytes
    uint32_t memoryType;  // memory type index (the same physical device: the same types in both drivers)
    uint32_t pad;
};

struct FrameParams
{
    uint64_t frame;          // N
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t width, height;  // the depth / visibility grid
    uint32_t rays;           // per pixel
    uint32_t debug;          // 1: the raw one-ray result, without the denoiser
    float camToWorld[12];    // rows
    float proj[4];           // scene projection _11 _22 _33 _34 (D3D9, row vectors)
    float proj43;            // _43
    float pad2[3];
    float sun[4];            // world direction towards the sun, w: tan(angular radius)
};

// One anti-aliasing frame (the game's scene end, before the UI).
enum AaMode : uint32_t
{
    kAaOff,
    kAaFsr,   // AMD FSR 3.1 (FidelityFX API, amd_fidelityfx_vk.dll)
    kAaDlss,  // NVIDIA DLSS / DLAA (nvngx_dlss.dll)
    kAaXess,  // Intel XeSS
};
struct AaParams
{
    uint64_t frame;          // N (its own counter: aaRequested / aaAccepted)
    uint32_t width, height;  // render = output size (native anti-aliasing)
    uint32_t mode;           // AaMode
    uint32_t reset;          // 1: camera cut / first frame (no history)
    float jitter[2];         // the subpixel offset applied to the projection, pixels (+x right, +y down)
    float cameraNear, cameraFar, fovY;  // from the scene projection
    float frameTimeMs;
    float sharpness;         // 0..1
    uint32_t debug;          // 1: the library's debug view
};

struct Control
{
    uint32_t magic, version;
    uint32_t helperPid;
    volatile uint32_t helperState;  // 0 starting, 1 ready (device created, buffers imported), 2 failed
    uint8_t gpuLuid[8];             // the game's device (VkPhysicalDeviceIDProperties::deviceLUID)
    volatile uint32_t generation;   // incremented by the game when buffers are reallocated (helper re-imports)
    uint32_t importedGeneration;    // the helper's
    SharedBuffer buffers[kBufferCount];
    uint64_t semToHelper, semFromHelper;  // timeline semaphore handles (helper process)
    volatile uint64_t requested;          // last frame the game submitted inputs for
    volatile uint64_t accepted;           // last frame the helper submitted its work for
    volatile uint32_t quit;               // the game is shutting down
    uint32_t pad;
    FrameParams params[kRing];
    volatile uint32_t gpuMicros;          // the helper's GPU time for its last finished frame (timestamps)
    uint32_t pad3;
    char log[256];                        // the helper's last error, for the game's log
    // v4: temporal anti-aliasing (buffers kAaColor..kAaOutput). Independent of the ray tracing above.
    uint64_t semAaToHelper, semAaFromHelper;  // timeline semaphores: the game signals inputs N, the helper output N
    volatile uint64_t aaRequested, aaAccepted;
    AaParams aaParams[kRing];
    volatile uint32_t aaState;            // 0 not started, 1 working, 2 failed (aaLog says why)
    volatile uint32_t aaActiveMode;       // the AaMode the helper actually runs
    volatile uint32_t aaGpuMicros;
    uint32_t pad4;
    char aaLog[256];
};
}  // namespace rtipc
