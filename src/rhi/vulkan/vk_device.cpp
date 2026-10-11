// rhi::vulkan device: frames, presentation, D3D9 state shadow and draws. See docs/renderer-interface.md.
//
// Frame model: a command buffer is always open for the current frame (kFramesInFlight slots). D3D9's immediate
// calls record into it; rendering (dynamic rendering) starts lazily at the first clear/draw after the targets
// change and ends for transfers. Present blits our back buffer to the swapchain, submits and opens the next slot.
// The adapter answers caps/mode/format queries through a real IDirect3D9 (no D3D9 device), so the game makes the
// same rendering-path decisions as with the D3D9 backend.
#include <cfloat>
#include <cmath>
#include <cstring>
#include <deque>
#include <unordered_map>

#include "config.h"
#include "display.h"
#include "log.h"
#include "vk_pipeline.h"
#include "vk_present.h"
#include "vk_resources.h"
#include "vk_scene.h"
#include "vk_shadows.h"
#include "vk_snow.h"
#include "vk_gtao.h"
#include "fsr/fsr_bridge.h"
#include "vk_fog.h"
#include "vk_rt.h"
#include "vk_rt_client.h"
#include "../../../third_party/XeGTAO/XeGTAO.h"

bool TestFrameDump(unsigned* frame);                                                          // hook/src/testshot.cpp
void TestSaveBgra(unsigned frame, const void* pixels, unsigned w, unsigned h, unsigned pitch);  // hook/src/testshot.cpp
void HarnessOnFrame();                                                                         // hook/src/d3d9_hooks.cpp

namespace rhi::vulkan
{
namespace
{
template <class T>
void SetRef(T*& slot, T* value)
{
    if (value)
        value->AddRef();
    if (slot)
        slot->Release();
    slot = value;
}

float U2F(DWORD v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void ColorToFloat(D3DCOLOR c, float out[4])
{
    out[0] = ((c >> 16) & 0xff) / 255.0f;
    out[1] = ((c >> 8) & 0xff) / 255.0f;
    out[2] = (c & 0xff) / 255.0f;
    out[3] = ((c >> 24) & 0xff) / 255.0f;
}

struct FrameSlot
{
    VkCommandBuffer cb = VK_NULL_HANDLE;  // the one being recorded (cbs[part])
    VkCommandBuffer cbs[3] = {};       // a frame can be submitted in up to three parts (the helper's ray tracing and
                                       // anti-aliasing work between them, rt_ipc.h)
    uint32_t part = 0;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore rendered = VK_NULL_HANDLE;
    uint64_t serial = 0;
    std::vector<std::function<void()>> destroy;
    // Uniform ring: FFUniforms + VS/PS constants per draw (when changed).
    VkBuffer uniforms = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
    uint8_t* uniformMapped = nullptr;
    VkDeviceSize uniformUsed = 0;
};
struct TickScope
{
    long long& total;
    LARGE_INTEGER start;
    explicit TickScope(long long& t) : total(t) { QueryPerformanceCounter(&start); }
    ~TickScope()
    {
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        total += end.QuadPart - start.QuadPart;
    }
};

constexpr VkDeviceSize kUniformRing = 32u << 20;

struct TessConfig
{
    bool on = false, props = false, ccw = false;
    float maxLevel = 8.0f, pixels = 6.0f, shape = 0.75f;
};
}  // namespace
const TessConfig& Tess();  // [render] tessellation settings (below)

VulkanDevice* g_device;  // the game creates one device

class VulkanDevice final : public Counted<Device>
{
public:
    VulkanDevice(IDirect3D9* queries, HWND window, D3DPRESENT_PARAMETERS* params);
    ~VulkanDevice() override;
    bool ok = false;

    // --- frame ---------------------------------------------------------------------------------------------
    HRESULT TestCooperativeLevel() override { return D3D_OK; }
    // What D3D9 reports on GPUs with 4 GB or more (measured on the D3D9 backend: 0xffb00000). The game reads it once
    // and sizes things by it, so a different value changes what it draws (tools/compare_backends.py).
    UINT GetAvailableTextureMem() override { return 0xFFB00000u; }
    HRESULT EvictManagedResources() override { return D3D_OK; }
    HRESULT GetDeviceCaps(D3DCAPS9* caps) override { return queries_->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, caps); }
    HRESULT Reset(D3DPRESENT_PARAMETERS* params) override;
    HRESULT Present(const RECT*, const RECT*, HWND, const RGNDATA*) override;
    HRESULT GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, Surface** out) override
    {
        *out = new VulkanSurface(backBuffer_);
        return D3D_OK;
    }
    void SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP*) override {}
    HRESULT BeginScene() override { return D3D_OK; }
    HRESULT EndScene() override { return D3D_OK; }
    HRESULT Clear(DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override;

    // --- resources -----------------------------------------------------------------------------------------
    HRESULT CreateTexture(UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, Texture** out) override
    {
        static const int verbose = ConfigInt("render", "vulkan_verbose", 0);
        if (verbose)
            Log("vulkan: CreateTexture %ux%u levels %u usage 0x%lx format %d pool %d", w, h, l, u, static_cast<int>(f), p);
        auto* t = new VulkanTexture(f, w, h, l, 1, u, p);
        if (!t->ok)
            return t->Release(), *out = nullptr, D3DERR_INVALIDCALL;
        *out = t;
        return D3D_OK;
    }
    HRESULT CreateCubeTexture(UINT e, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, Texture** out) override
    {
        auto* t = new VulkanTexture(f, e, e, l, 6, u, p);
        if (!t->ok)
            return t->Release(), *out = nullptr, D3DERR_INVALIDCALL;
        *out = t;
        return D3D_OK;
    }
    HRESULT CreateVertexBuffer(UINT len, DWORD u, DWORD, D3DPOOL, Buffer** out) override
    {
        auto* b = new VulkanBuffer(len, u, false, D3DFMT_UNKNOWN);
        *out = b;
        return b->ok ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
    }
    HRESULT CreateIndexBuffer(UINT len, DWORD u, D3DFORMAT f, D3DPOOL, Buffer** out) override
    {
        auto* b = new VulkanBuffer(len, u, true, f);
        *out = b;
        return b->ok ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
    }
    HRESULT CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT f, D3DPOOL, Surface** out) override
    {
        *out = new VulkanSurface(f, w, h);
        return D3D_OK;
    }
    HRESULT CreateVertexLayout(const D3DVERTEXELEMENT9* e, VertexLayout** out) override
    {
        *out = new VulkanVertexLayout(e);
        return D3D_OK;
    }
    HRESULT CreateVertexShader(const DWORD* code, VertexShader** out) override
    {
        auto* s = new VulkanShader(code, false);
        *out = s;
        return s->translated.ok ? D3D_OK : D3DERR_INVALIDCALL;
    }
    HRESULT CreatePixelShader(const DWORD* code, PixelShader** out) override
    {
        auto* s = new VulkanPixelShader(code);
        *out = s;
        return s->shader->translated.ok ? D3D_OK : D3DERR_INVALIDCALL;
    }
    HRESULT StretchRect(Surface* src, const RECT* sr, Surface* dst, const RECT* dr, D3DTEXTUREFILTERTYPE f) override;

    // --- targets and state ---------------------------------------------------------------------------------
    HRESULT SetRenderTarget(DWORD index, Surface* s) override;
    HRESULT SetDepthStencilSurface(Surface* s) override;
    HRESULT GetDepthStencilSurface(Surface** out) override
    {
        if (!depth_)
            return *out = nullptr, D3DERR_NOTFOUND;
        depth_->AddRef();
        *out = depth_;
        return D3D_OK;
    }
    HRESULT SetViewport(const D3DVIEWPORT9* v) override
    {
        viewport_ = *v;
        ffDirty_ = true;
        return D3D_OK;
    }
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m) override;
    HRESULT SetMaterial(const D3DMATERIAL9* m) override
    {
        material_ = *m;
        ffDirty_ = true;
        return D3D_OK;
    }
    HRESULT SetLight(DWORD i, const D3DLIGHT9* l) override
    {
        if (i < 8)
            lights_[i] = *l, ffDirty_ = true;
        return D3D_OK;
    }
    HRESULT LightEnable(DWORD i, BOOL e) override
    {
        if (i < 8)
            lightOn_[i] = e != 0, ffDirty_ = true;
        return D3D_OK;
    }
    HRESULT SetRenderState(D3DRENDERSTATETYPE s, DWORD v) override
    {
        if (s < 256)
            rs_[s] = v, ffDirty_ = true;
        return D3D_OK;
    }
    HRESULT SetTextureStageState(DWORD st, D3DTEXTURESTAGESTATETYPE t, DWORD v) override
    {
        if (st < 8 && t < 33)
            tss_[st][t] = v, ffDirty_ = true;
        return D3D_OK;
    }
    HRESULT SetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) override
    {
        if (s < 8 && t < 14)
            samp_[s][t] = v;
        return D3D_OK;
    }
    HRESULT SetTexture(DWORD st, Texture* t) override
    {
        if (st < 8)
            SetRef(textures_[st], static_cast<VulkanTexture*>(t));
        return D3D_OK;
    }
    HRESULT SetVertexLayout(VertexLayout* l) override
    {
        SetRef(layout_, static_cast<VulkanVertexLayout*>(l));
        return D3D_OK;
    }
    HRESULT SetVertexShader(VertexShader* s) override
    {
        if ((vs_ == nullptr) != (s == nullptr))
            ffDirty_ = true;  // fogParams.w depends on fixed-function vs shader vertex processing
        SetRef(vs_, static_cast<VulkanShader*>(s));
        return D3D_OK;
    }
    HRESULT SetVertexShaderConstantF(UINT start, const float* d, UINT count) override
    {
        if (start + count <= kVsConstantCount)
            std::memcpy(vsConst_ + start * 4, d, count * 16), vsDirty_ = true;
        return D3D_OK;
    }
    HRESULT SetPixelShader(PixelShader* s) override
    {
        SetRef(ps_, static_cast<VulkanPixelShader*>(s));
        return D3D_OK;
    }
    HRESULT SetPixelShaderConstantF(UINT start, const float* d, UINT count) override
    {
        if (start + count <= kPsConstantCount)
            std::memcpy(psConst_ + start * 4, d, count * 16), psDirty_ = true;
        return D3D_OK;
    }
    HRESULT SetStreamSource(UINT s, Buffer* b, UINT o, UINT stride) override
    {
        if (s < 16)
        {
            SetRef(streams_[s], static_cast<VulkanBuffer*>(b));
            streamOffset_[s] = o, streamStride_[s] = stride;
        }
        return D3D_OK;
    }
    HRESULT SetIndices(Buffer* b) override
    {
        SetRef(indices_, static_cast<VulkanBuffer*>(b));
        return D3D_OK;
    }
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE t, UINT start, UINT count) override;
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT base, UINT mi, UINT nv, UINT si, UINT count) override;

    // --- used by resources ----------------------------------------------------------------------------------
    uint64_t serial = 1;      // frame being recorded
    uint64_t completed = 0;   // last frame known finished
    FrameSlot& Slot() { return slots_[serial % kFramesInFlight]; }
    void Transfer(const std::function<void(VkCommandBuffer)>& record)
    {
        EndRendering();
        record(Slot().cb);
    }

private:
    bool CreateSwapchain();
    void DestroySwapchain();
    bool CreateTargets();
    void BeginFrame();
    void BeginRendering();
    void EndRendering();
    void ResetState();
    bool PrepareDraw(D3DPRIMITIVETYPE type, uint64_t drawKey);
    FFKey BuildFFKey(uint32_t vertexMask, const uint8_t texCoordSize[8]);
    VkDeviceSize PushUniform(const void* data, VkDeviceSize size);
    uint32_t VertexCount(D3DPRIMITIVETYPE t, UINT primitives) const;

    IDirect3D9* queries_;
    HWND window_;
    D3DPRESENT_PARAMETERS params_{};
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapImages_;
    VkExtent2D swapExtent_{};
    FrameSlot slots_[kFramesInFlight];

    std::shared_ptr<Image> backBuffer_, autoDepth_, motion_, present_;
    PresentPass presentPass_;
    bool motionCleared_ = false;  // the motion attachment is cleared at the frame's first rendering into it
    // Previous-frame transforms per draw (motion vectors): draw id -> snapshot. `prevSnaps_` is last frame's.
    std::unordered_map<uint64_t, PrevUniforms> prevSnaps_, curSnaps_;
    std::unordered_map<uint64_t, uint32_t> drawOccurrence_;
    uint64_t DrawId(uint64_t base);
    SceneFrame scene_;  // this frame's 3D draws into the back buffer (vk_scene.h)
// Shadow caster cache (Shadows): draws seen earlier in the level, by draw id.
struct CachedCaster
{
    SceneDraw draw;
    std::vector<float> constants;  // its vertex shader constants when last seen
    Mat4 toWorld;                  // camera -> world of that frame (the constants hold world * view)
    float worldKey[12];            // its world transform (c42..c44 taken to world space): moving if it changes
    uint64_t lastSeen = 0;
    bool moving = false;
};
std::unordered_map<uint64_t, CachedCaster> casters_;
uint64_t shadowFrame_ = 0;
void DropCaster(CachedCaster& c);
    // Shadow maps (vk_shadows.h): at SceneEnd.
    const VertexInput* lastInput_ = nullptr;  // the current draw's vertex input and plain vertex shader (PrepareDraw)
    VkShaderModule lastVertex_ = VK_NULL_HANDLE;
    std::shared_ptr<Image> shadowMap_;
    VkShaderModule shadowDepthFs_ = VK_NULL_HANDLE, shadowApplyVs_ = VK_NULL_HANDLE, shadowApplyFs_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadowSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowLayout_ = VK_NULL_HANDLE;
    VkPipeline shadowApply_ = VK_NULL_HANDLE;
    VkFormat shadowApplyFormat_ = VK_FORMAT_UNDEFINED;
    VkSampler shadowDepthSampler_ = VK_NULL_HANDLE, shadowCompareSampler_ = VK_NULL_HANDLE;
    bool shadowFailed_ = false;
    bool ShadowInit(uint32_t size);
    VkPipeline ShadowApplyPipeline(VkFormat target);

    // GPU snowfall (vk_snow.h): at SceneEnd, after the shadows.
    VkShaderModule snowVs_ = VK_NULL_HANDLE, snowFs_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout snowSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout snowLayout_ = VK_NULL_HANDLE;
    VkPipeline snowPipeline_ = VK_NULL_HANDLE;
    VkFormat snowFormat_ = VK_FORMAT_UNDEFINED;
    bool snowFailed_ = false;
// The flakes' state (vk_snow.cpp: position, velocity; 32 bytes each), simulated by a compute pass.
VkShaderModule snowCs_ = VK_NULL_HANDLE;
VkPipeline snowCompute_ = VK_NULL_HANDLE;
VkBuffer snowBuffer_ = VK_NULL_HANDLE;
VkDeviceMemory snowMemory_ = VK_NULL_HANDLE;
uint32_t snowCapacity_ = 0;
bool snowFilled_ = false;
float snowLastCam_[3] = {};
    VkPipeline SnowPipeline(VkFormat target);

public:
    void Shadows();  // rhi::SceneEnd
    void Snow();     // rhi::SceneEnd
void Ambient();  // rhi::SceneEnd: ambient occlusion (vk_gtao.h)
// Ray-traced shadows (vk_rt.h).
struct RtCaster
{
    const SceneDraw* d;
    const float* c;  // its vertex shader constants
    Mat4 toWorld;    // camera -> world of the frame they come from
};
struct RtBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
    VkDeviceAddress address = 0;
};
struct RtFrame
{
    RtBuffer verts, indices, blasBuffer, tlasBuffer, instances, scratch;
    VkAccelerationStructureKHR blas = VK_NULL_HANDLE, tlas = VK_NULL_HANDLE;
};
bool RtShadows(const std::vector<RtCaster>& casters, const Mat4& toWorld);
bool RtShadowsRemote(const std::vector<RtCaster>& casters, const Mat4& toWorld);  // via the 64-bit helper
uint64_t RtGather(const std::vector<RtCaster>& casters, std::vector<uint32_t>& base);
void RtReplay(const std::vector<RtCaster>& casters, const std::vector<uint32_t>& base, VkBuffer dst);
void SplitSubmit(VkSemaphore signal, uint64_t value);
bool RtRemoteInit();
uint64_t rtWaitFrame_ = 0;  // this frame's ray-tracing helper work (its visibility is read after the split)
// Helper work the next submission waits for (taken by SplitSubmit and the final submit). Only frames the helper has
// accepted are waited for, so a stalled or missing helper cannot hang the GPU.
struct HelperWait
{
    VkSemaphore semaphore;
    uint64_t value;
    bool aa;  // anti-aliasing (else ray tracing)
};
std::vector<HelperWait> helperWaits_;
uint32_t TakeHelperWaits(VkSemaphore* waits, VkPipelineStageFlags* stages, uint64_t* values, uint32_t n);
// Temporal anti-aliasing (docs/subsystems/remaster-aa.md): decided per frame, jitter in pixels (+x right, +y down).
bool aaActive_ = false;
float aaJitter_[2] = {0.0f, 0.0f};
uint64_t aaFrame_ = 0;
uint32_t aaWidth_ = 0, aaHeight_ = 0;
bool aaReset_ = true;
LARGE_INTEGER aaLastTime_{};
void AaBeginFrame();
void TemporalAa();
void TemporalAaHelper();  // modes 2 / 3 (DLSS, XeSS): the 64-bit helper (rt_ipc.h)
// FSR 3.1 in process (hook/fsr/fsr_bridge.h): its own depth copy (R32F) and output (storage) images.
struct AaImage
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
AaImage aaDepth_, aaOutput_, aaExposure_;
VkBuffer aaDepthBuffer_ = VK_NULL_HANDLE;
VkDeviceMemory aaDepthBufferMemory_ = VK_NULL_HANDLE;
bool aaFsrFailed_ = false;
bool AaEnsureImages(uint32_t width, uint32_t height);
void AaBarrier(VkCommandBuffer cb, AaImage& img, VkImageLayout to);
VkShaderModule rtRemoteFs_ = VK_NULL_HANDLE, rtDepthCs_ = VK_NULL_HANDLE;
VkDescriptorSetLayout rtRemoteSetLayout_ = VK_NULL_HANDLE, rtDepthSetLayout_ = VK_NULL_HANDLE;
VkPipelineLayout rtRemoteLayout_ = VK_NULL_HANDLE, rtDepthLayout_ = VK_NULL_HANDLE;
VkPipeline rtRemoteApply_ = VK_NULL_HANDLE, rtDepthPipe_ = VK_NULL_HANDLE;
VkFormat rtRemoteFormat_ = VK_FORMAT_UNDEFINED;
bool rtRemoteFailed_ = false;
bool RtInit();
bool RtEnsure(RtBuffer& b, VkDeviceSize size, VkBufferUsageFlags usage, bool host);
void RtFree(RtBuffer& b);
VkPipeline RtApplyPipeline(VkFormat target);
RtFrame rtFrames_[kFramesInFlight];
std::vector<uint32_t> rtIndices_;
bool rtFailed_ = false;
VkDeviceSize rtScratchAlign_ = 256;
VkShaderModule rtNullFs_ = VK_NULL_HANDLE, rtVs_ = VK_NULL_HANDLE, rtFs_ = VK_NULL_HANDLE;
VkDescriptorSetLayout rtSetLayout_ = VK_NULL_HANDLE;
VkPipelineLayout rtApplyLayout_ = VK_NULL_HANDLE;
VkPipeline rtApply_ = VK_NULL_HANDLE;
VkFormat rtApplyFormat_ = VK_FORMAT_UNDEFINED;
bool sceneEnded_ = false;  // SceneEnd ran this frame: the draws that follow are the UI
bool lightingDone_ = false;  // ambient occlusion and shadows were applied this frame
void BeforeDraw();          // runs them before the first translucent scene draw
// XeGTAO state (Ambient): compute pipelines, working images (sized to the target), the apply pass.
struct GtaoImage
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;   // all levels (sampled)
    VkImageView mips[5] = {};            // one level each (storage)
};
bool GtaoImageCreate(GtaoImage& g, VkFormat format, uint32_t levels);
void GtaoImageDestroy(GtaoImage& g);
bool GtaoInit();
VkPipeline GtaoApplyPipeline(VkFormat target);
bool gtaoFailed_ = false, gtaoLogged_ = false;
VkPipeline gtaoPipe_[4] = {};
VkDescriptorSetLayout gtaoSetLayout_ = VK_NULL_HANDLE;
VkPipelineLayout gtaoLayout_ = VK_NULL_HANDLE;
GtaoImage gtaoDepth_, gtaoTerm_, gtaoEdges_, gtaoFinal_;
uint32_t gtaoWidth_ = 0, gtaoHeight_ = 0;
uint32_t gtaoFrame_ = 0;
VkShaderModule gtaoApplyVs_ = VK_NULL_HANDLE, gtaoApplyFs_ = VK_NULL_HANDLE;
VkDescriptorSetLayout gtaoApplySetLayout_ = VK_NULL_HANDLE;
VkPipelineLayout gtaoApplyLayout_ = VK_NULL_HANDLE;
VkPipeline gtaoApply_ = VK_NULL_HANDLE;
VkFormat gtaoApplyFormat_ = VK_FORMAT_UNDEFINED;
// Afterlight unified AO + GI ([render] afterlight; shaders/afterlight.hlsl): replaces XeGTAO's main pass with the
// player's one-pass occlusion + bounce gather, and adds its normals, source radiance and bounce blur passes.
bool alOn_ = false;
VkPipeline alPipe_[6] = {};  // normals, radiance, radiance MIP, unified main pass, bounce blur H, bounce blur V
GtaoImage alNormals_, alRadiance_, alBounce_, alPong_;
VkSampler alLinear_ = VK_NULL_HANDLE;

private:
    // Scene vertex capture (vk_abi.h kBindingCapture): clip positions of the scene draws, on capture frames.
    bool captureFrame_ = false, captureDraw_ = false;
    int64_t captureFirst_ = 0;     // vertex index range of the next draw (set by the draw calls)
    uint32_t captureCount_ = 0;
    VkBuffer captureBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory captureMemory_ = VK_NULL_HANDLE;
    uint8_t* captureMapped_ = nullptr;
    VkDeviceSize captureSize_ = 0, captureUsed_ = 0, captureOffset_ = 0;
    bool SceneDrawTarget() const { return rt_->GetImage() == backBuffer_.get() && depth_ && rs_[D3DRS_ZENABLE]; }
    // Tessellation (vk_abi.h "Tessellation"; [render] tessellation=1): this draw goes through the tessellation
    // stages as a patch list built by TessIndices.
    bool tessDraw_ = false;
    float tessNormalDecode_ = 0.0f;
    // [render] vulkan_verbose: CPU time per frame in draw preparation, in Present, and waiting for the GPU.
    long long prepareTicks_ = 0, presentTicks_ = 0, waitTicks_ = 0, oneShotTicks_ = 0;
    uint32_t drawsCounted_ = 0;
    uint32_t tessDraws_ = 0, tessCandidates_ = 0, tessSkinned_ = 0, tessWithNormal_ = 0;  // this frame (verbose log)
    VkShaderModule tessControl_ = VK_NULL_HANDLE, tessEval_ = VK_NULL_HANDLE;
    bool TessModules();
    bool DrawTessellated(D3DPRIMITIVETYPE t, const uint8_t* indices, bool index32, uint32_t indexLength, INT base,
                         UINT start, UINT count);
    bool EnsureCapture();
    void RecordScene(D3DPRIMITIVETYPE t, bool indexed, INT base, UINT start, UINT count, uint64_t id);
    VulkanSurface* rt_ = nullptr;
    VulkanSurface* depth_ = nullptr;
    bool rendering_ = false;
    // Soft particles (vk_abi.h): the depth attachment's layout for the next BeginRendering. Soft draws sample the
    // depth they are tested against, so it goes read-only (DEPTH_STENCIL_READ_ONLY_OPTIMAL) for them and stays so
    // until a draw or clear writes depth or stencil. `renderingReadOnly_`: the layout of the rendering in progress.
    bool depthReadOnly_ = false, renderingReadOnly_ = false;
    void DepthReadOnly(bool readOnly);
    uint8_t SoftParticleMode() const;
    float softParams_[4] = {};  // FFUniforms::soft of the last uniform upload
    uint32_t softDraws_ = 0;    // this frame (verbose log)

    // D3D9 state shadow
    DWORD rs_[256] = {};
    DWORD tss_[8][33] = {};
    uint32_t samp_[8][14] = {};
    VulkanTexture* textures_[8] = {};
    VulkanBuffer* streams_[16] = {};
    UINT streamOffset_[16] = {}, streamStride_[16] = {};
    VulkanBuffer* indices_ = nullptr;
    VulkanVertexLayout* layout_ = nullptr;
    VulkanShader* vs_ = nullptr;
    VulkanPixelShader* ps_ = nullptr;
    float vsConst_[kVsConstantCount * 4] = {};
    float psConst_[kPsConstantCount * 4] = {};
    D3DMATRIX world_[4]{}, view_{}, projection_{}, texture_[8]{};
    D3DMATRIX sceneProjection_{};  // projection of the last depth-writing draw into the back buffer
    D3DVIEWPORT9 viewport_{};
    D3DMATERIAL9 material_{};
    D3DLIGHT9 lights_[8]{};
    bool lightOn_[8] = {};
    bool ffDirty_ = true, vsDirty_ = true, psDirty_ = true;
    bool wFog_ = false;  // D3DPRASTERCAPS_WFOG of the adapter
    VkDeviceSize ffOffset_ = 0, vsOffset_ = 0, psOffset_ = 0;

    std::unordered_map<uint64_t, VertexInput> inputs_;
    std::unordered_map<uint64_t, VkShaderModule> ffVertex_, ffPixel_;
    // Per fixed-function pixel key: the sampler masks (generating the GLSL text per draw just to read them cost
    // string building on every draw; 1300 draws per frame).
    struct FFPixelMasks
    {
        uint32_t sampler = 0, cube = 0;
        bool known = false;
    };
    std::unordered_map<uint64_t, FFPixelMasks> ffPixelMasks_;
    // Tessellated draws' triangle lists (DrawTessellated), converted once and kept in a persistent buffer: the meshes
    // are static, and rebuilding and re-uploading ~360 lists per frame was the largest CPU cost at 4K (2026-10-11).
    // Key: hash of the source index bytes, range and primitive type -> {offset, count}.
    struct TessList
    {
        VkDeviceSize offset;
        uint32_t count;
    };
    std::unordered_map<uint64_t, TessList> tessLists_;
    VkBuffer tessBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory tessMemory_ = VK_NULL_HANDLE;
    uint8_t* tessMapped_ = nullptr;
    VkDeviceSize tessUsed_ = 0;
    static constexpr VkDeviceSize kTessBuffer = 64u << 20;
    std::shared_ptr<Image> white_;  // bound when a shader samples a stage with no texture
};

// --- construction --------------------------------------------------------------------------------------------

VulkanDevice::VulkanDevice(IDirect3D9* queries, HWND window, D3DPRESENT_PARAMETERS* params)
    : queries_(queries), window_(window)
{
    queries_->AddRef();
    g_device = this;
    Context& c = Ctx();
    DisplayAdjustParams(params);  // [display] options (window, size, vsync) as with D3D9
    DisplayAdjustWindow(window, params);
    // D3D9 fills in what the caller left open and writes it back: the game reads BackBufferFormat afterwards (as the
    // adapter format of its CheckDeviceFormat queries) and the size.
    if (params->BackBufferFormat == D3DFMT_UNKNOWN)
    {
        D3DDISPLAYMODE mode{};
        queries_->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);
        params->BackBufferFormat = mode.Format != D3DFMT_UNKNOWN ? mode.Format : D3DFMT_X8R8G8B8;
    }
    if (params->BackBufferWidth == 0 || params->BackBufferHeight == 0)
    {
        RECT rc{};
        GetClientRect(params->hDeviceWindow ? params->hDeviceWindow : window, &rc);
        if (params->BackBufferWidth == 0)
            params->BackBufferWidth = rc.right > 0 ? rc.right : 640;
        if (params->BackBufferHeight == 0)
            params->BackBufferHeight = rc.bottom > 0 ? rc.bottom : 480;
    }
    if (params->BackBufferCount == 0)
        params->BackBufferCount = 1;
    params_ = *params;
    if (!params_.hDeviceWindow)
        params_.hDeviceWindow = window;
    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    sci.hinstance = GetModuleHandleA(nullptr);
    sci.hwnd = window_;
    if (vkCreateWin32SurfaceKHR(c.instance, &sci, nullptr, &surface_) != VK_SUCCESS)
        return;
    for (FrameSlot& s : slots_)
    {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = c.commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        ai.commandBufferCount = 3;
    vkAllocateCommandBuffers(c.device, &ai, s.cbs);
    s.cb = s.cbs[0];
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(c.device, &fi, nullptr, &s.fence);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(c.device, &si, nullptr, &s.acquired);
        vkCreateSemaphore(c.device, &si, nullptr, &s.rendered);
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = kUniformRing;
        // + tessellation lists; storage: a placeholder for optional storage bindings (the AO pass without RT).
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        vkCreateBuffer(c.device, &bi, nullptr, &s.uniforms);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(c.device, s.uniforms, &req);
        s.uniformMemory = c.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(c.device, s.uniforms, s.uniformMemory, 0);
        vkMapMemory(c.device, s.uniformMemory, 0, kUniformRing, 0, reinterpret_cast<void**>(&s.uniformMapped));
    }
    BeginFrame();
    if (!CreateSwapchain() || !CreateTargets())
        return;
    white_ = std::make_shared<Image>();
    white_->Create(D3DFMT_A8R8G8B8, 1, 1, 1, 1, false, false);
    std::shared_ptr<Image> w = white_;
    Transfer([w](VkCommandBuffer cb) {
        w->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearColorValue one{{1, 1, 1, 1}};
        VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, w->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1, &r);
        w->Transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    ResetState();
    if (!presentPass_.Init())
        return;
    D3DCAPS9 caps{};
    queries_->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
    wFog_ = (caps.RasterCaps & D3DPRASTERCAPS_WFOG) != 0 && ConfigInt("render", "wfog", 0) != 0;
    ok = true;
    Log("vulkan: device ready, %ux%u back buffer, swapchain %ux%u", params_.BackBufferWidth, params_.BackBufferHeight,
        swapExtent_.width, swapExtent_.height);
}

VulkanDevice::~VulkanDevice()
{
    Context& c = Ctx();
    vkDeviceWaitIdle(c.device);
    for (auto*& t : textures_)
        SetRef(t, static_cast<VulkanTexture*>(nullptr));
    for (auto*& s : streams_)
        SetRef(s, static_cast<VulkanBuffer*>(nullptr));
    SetRef(indices_, static_cast<VulkanBuffer*>(nullptr));
    SetRef(layout_, static_cast<VulkanVertexLayout*>(nullptr));
    SetRef(vs_, static_cast<VulkanShader*>(nullptr));
    SetRef(ps_, static_cast<VulkanPixelShader*>(nullptr));
    SetRef(rt_, static_cast<VulkanSurface*>(nullptr));
    SetRef(depth_, static_cast<VulkanSurface*>(nullptr));
    DestroySwapchain();
    for (FrameSlot& s : slots_)
        for (auto& d : s.destroy)
            d();
    if (captureBuffer_)
    {
        vkDestroyBuffer(c.device, captureBuffer_, nullptr);
        vkFreeMemory(c.device, captureMemory_, nullptr);
    }
    queries_->Release();
    if (g_device == this)
        g_device = nullptr;
}

// D3D9's initial state (the values the D3D9 documentation lists as defaults).
void VulkanDevice::ResetState()
{
    std::memset(rs_, 0, sizeof(rs_));
    rs_[D3DRS_ZENABLE] = params_.EnableAutoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE;
    rs_[D3DRS_FILLMODE] = D3DFILL_SOLID;
    rs_[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    rs_[D3DRS_ZWRITEENABLE] = TRUE;
    rs_[D3DRS_LASTPIXEL] = TRUE;
    rs_[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    rs_[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    rs_[D3DRS_CULLMODE] = D3DCULL_CCW;
    rs_[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    rs_[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs_[D3DRS_FOGSTART] = 0;
    rs_[D3DRS_FOGEND] = 0x3f800000;
    rs_[D3DRS_FOGDENSITY] = 0x3f800000;
    rs_[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    rs_[D3DRS_STENCILFAIL] = rs_[D3DRS_STENCILZFAIL] = rs_[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs_[D3DRS_STENCILMASK] = rs_[D3DRS_STENCILWRITEMASK] = 0xffffffff;
    rs_[D3DRS_TEXTUREFACTOR] = 0xffffffff;
    rs_[D3DRS_LIGHTING] = TRUE;
    rs_[D3DRS_COLORVERTEX] = TRUE;
    rs_[D3DRS_LOCALVIEWER] = TRUE;
    rs_[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    rs_[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    rs_[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs_[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs_[D3DRS_COLORWRITEENABLE] = 0xf;
    rs_[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
    rs_[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;
    rs_[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    rs_[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    rs_[D3DRS_BLENDFACTOR] = 0xffffffff;
    rs_[D3DRS_POINTSIZE] = 0x3f800000;
    rs_[D3DRS_MULTISAMPLEMASK] = 0xffffffff;
    for (int s = 0; s < 8; ++s)
    {
        std::memset(tss_[s], 0, sizeof(tss_[s]));
        tss_[s][D3DTSS_COLOROP] = s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        tss_[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        tss_[s][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        tss_[s][D3DTSS_ALPHAOP] = s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        tss_[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        tss_[s][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        tss_[s][D3DTSS_TEXCOORDINDEX] = s;
        tss_[s][D3DTSS_COLORARG0] = tss_[s][D3DTSS_ALPHAARG0] = tss_[s][D3DTSS_RESULTARG] = D3DTA_CURRENT;
        std::memset(samp_[s], 0, sizeof(samp_[s]));
        samp_[s][D3DSAMP_ADDRESSU] = samp_[s][D3DSAMP_ADDRESSV] = samp_[s][D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        samp_[s][D3DSAMP_MAGFILTER] = samp_[s][D3DSAMP_MINFILTER] = D3DTEXF_POINT;
        samp_[s][D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
        samp_[s][D3DSAMP_MAXANISOTROPY] = 1;
    }
    D3DMATRIX identity{};
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    for (auto& m : world_)
        m = identity;
    view_ = projection_ = identity;
    for (auto& m : texture_)
        m = identity;
    viewport_ = {0, 0, params_.BackBufferWidth, params_.BackBufferHeight, 0, 1};
    ffDirty_ = vsDirty_ = psDirty_ = true;
}

// --- swapchain and targets -------------------------------------------------------------------------------------

bool VulkanDevice::CreateSwapchain()
{
    Context& c = Ctx();
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c.physical, surface_, &caps);
    swapExtent_ = caps.currentExtent;
    if (swapExtent_.width == UINT32_MAX || swapExtent_.width == 0)
        swapExtent_ = {params_.BackBufferWidth, params_.BackBufferHeight};
    bool vsync = params_.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE;
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = caps.minImageCount + 1 <= caps.maxImageCount || caps.maxImageCount == 0 ? caps.minImageCount + 1
                                                                                                : caps.minImageCount;
    ci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = swapExtent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = swapchain_;
    VkSwapchainKHR sc;
    VkResult r = vkCreateSwapchainKHR(c.device, &ci, nullptr, &sc);
    if (r != VK_SUCCESS)
        return LogVk("vkCreateSwapchainKHR", r), false;
    if (swapchain_)
        vkDestroySwapchainKHR(c.device, swapchain_, nullptr);
    swapchain_ = sc;
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(c.device, sc, &n, nullptr);
    swapImages_.resize(n);
    vkGetSwapchainImagesKHR(c.device, sc, &n, swapImages_.data());
    return true;
}

void VulkanDevice::DestroySwapchain()
{
    Context& c = Ctx();
    if (swapchain_)
        vkDestroySwapchainKHR(c.device, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    if (surface_)
        vkDestroySurfaceKHR(c.instance, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
}

bool VulkanDevice::CreateTargets()
{
    // The back buffer is stored as RGBA16F (the linear HDR target of the remaster; the game still sees its own
    // format). Next to it: the motion-vector attachment (RGBA16F: motion, AO weight, albedo; vk_abi.h) and the 8-bit image the present pass
    // (tonemap / debug view) writes and the swapchain is filled from.
    backBuffer_ = std::make_shared<Image>();
    D3DFORMAT bbFormat = params_.BackBufferFormat == D3DFMT_UNKNOWN ? D3DFMT_X8R8G8B8 : params_.BackBufferFormat;
    if (!backBuffer_->Create(bbFormat, params_.BackBufferWidth, params_.BackBufferHeight, 1, 1, true, false,
                             VK_FORMAT_R16G16B16A16_SFLOAT))
        return false;
    motion_ = std::make_shared<Image>();
    if (!motion_->Create(D3DFMT_A8R8G8B8, params_.BackBufferWidth, params_.BackBufferHeight, 1, 1, true, false,
                         VK_FORMAT_R16G16B16A16_SFLOAT))
        return false;
    present_ = std::make_shared<Image>();
    if (!present_->Create(D3DFMT_A8R8G8B8, params_.BackBufferWidth, params_.BackBufferHeight, 1, 1, true, false))
        return false;
    SetRef(rt_, static_cast<VulkanSurface*>(nullptr));
    rt_ = new VulkanSurface(backBuffer_);
    SetRef(depth_, static_cast<VulkanSurface*>(nullptr));
    if (params_.EnableAutoDepthStencil)
    {
        autoDepth_ = std::make_shared<Image>();
        if (!autoDepth_->Create(params_.AutoDepthStencilFormat, params_.BackBufferWidth, params_.BackBufferHeight, 1,
                                1, true, true))
            return false;
        depth_ = new VulkanSurface(autoDepth_);
    }
    return true;
}

HRESULT VulkanDevice::Reset(D3DPRESENT_PARAMETERS* params)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    EndRendering();
    vkDeviceWaitIdle(Ctx().device);
    DisplayAdjustParams(params);
    DisplayAdjustWindow(window_, params);  // a new size from the Graphics page: resize / fit the window as at start
    params_ = *params;
    if (backBuffer_)
        backBuffer_->Destroy();
    if (autoDepth_)
        autoDepth_->Destroy();
    if (!CreateSwapchain() || !CreateTargets())
        return D3DERR_DEVICELOST;
    ResetState();
    return D3D_OK;
}

// --- frames ----------------------------------------------------------------------------------------------------

void VulkanDevice::BeginFrame()
{
    Context& c = Ctx();
    FrameSlot& s = Slot();
    {
        TickScope wait(waitTicks_);
        vkWaitForFences(c.device, 1, &s.fence, VK_TRUE, UINT64_MAX);
    }
    if (s.serial > completed)
        completed = s.serial;
    for (auto& d : s.destroy)
        d();
    s.destroy.clear();
    s.serial = serial;
    s.uniformUsed = 0;
    ffDirty_ = vsDirty_ = psDirty_ = true;  // new ring: re-upload uniforms
    prevSnaps_.swap(curSnaps_);  // this frame's draws find last frame's transforms there
    curSnaps_.clear();
    drawOccurrence_.clear();
    scene_.Clear();
    sceneEnded_ = false;
    lightingDone_ = false;
    depthReadOnly_ = false;  // the frame starts with a writable depth (soft particles switch it per draw)
    ffDirty_ = true;  // per-frame uniforms (modern fog: camera)
    scene_.serial = serial;
    captureFrame_ = Ctx().vertexStores && DumpSceneFrame() == serial;
    captureUsed_ = 0;
    motionCleared_ = false;
    s.cb = s.cbs[0];
    s.part = 0;
    rtWaitFrame_ = 0;
    helperWaits_.clear();
    AaBeginFrame();
    for (VkCommandBuffer b : s.cbs)
        vkResetCommandBuffer(b, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(s.cb, &bi);
}

// Takes the pending helper waits (helperWaits_) that the helper has accepted into the arrays after `n` entries;
// returns the new count. Binary semaphores in the arrays (value 0) are ignored by the timeline values.
uint32_t VulkanDevice::TakeHelperWaits(VkSemaphore* waits, VkPipelineStageFlags* stages, uint64_t* values, uint32_t n)
{
    for (const HelperWait& w : helperWaits_)
        if (n < 4 && (w.aa ? Rt().AaAccepted(w.value, 4) : Rt().Accepted(w.value, 4)))
            waits[n] = w.semaphore, stages[n] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, values[n++] = w.value;
    helperWaits_.clear();
    return n;
}

// Submits the frame so far (signalling `signal` = value, a timeline semaphore, and waiting for pending helper work)
// and continues recording in the slot's next command buffer (the helper works between the parts, rt_ipc.h).
void VulkanDevice::SplitSubmit(VkSemaphore signal, uint64_t value)
{
    EndRendering();
    FrameSlot& s = Slot();
    vkEndCommandBuffer(s.cb);
    VkSemaphore waits[4];
    VkPipelineStageFlags stages[4];
    uint64_t waitValues[4];
    const uint32_t nWaits = TakeHelperWaits(waits, stages, waitValues, 0);
    VkTimelineSemaphoreSubmitInfo ts{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    ts.signalSemaphoreValueCount = 1;
    ts.pSignalSemaphoreValues = &value;
    ts.waitSemaphoreValueCount = nWaits;
    ts.pWaitSemaphoreValues = waitValues;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.pNext = &ts;
    si.waitSemaphoreCount = nWaits;
    si.pWaitSemaphores = waits;
    si.pWaitDstStageMask = stages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s.cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &signal;
    vkQueueSubmit(Ctx().queue, 1, &si, VK_NULL_HANDLE);
    s.part = s.part < 2 ? s.part + 1 : 2;
    s.cb = s.cbs[s.part];
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(s.cb, &bi);
    ffDirty_ = vsDirty_ = psDirty_ = true;
}

void VulkanDevice::BeginRendering()
{
    if (rendering_)
        return;
    VkCommandBuffer cb = Slot().cb;
    Image* color = rt_ ? rt_->GetImage() : nullptr;
    Image* depth = depth_ ? depth_->GetImage() : nullptr;
    if (!color)
        return;
    color->Transition(cb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    // Soft particles: a read-only depth attachment that the draws may also sample (DepthReadOnly).
    const bool readOnly = depth && depthReadOnly_;
    const VkImageLayout depthLayout =
        readOnly ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (depth)
        depth->Transition(cb, depthLayout);
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = color->AttachmentView(rt_->level, rt_->face);
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (depth)
    {
        da.imageView = depth->AttachmentView(0, 0);
        da.imageLayout = depthLayout;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        da.storeOp = readOnly ? VK_ATTACHMENT_STORE_OP_NONE : VK_ATTACHMENT_STORE_OP_STORE;
    }
    renderingReadOnly_ = readOnly;
    // Into the back buffer: also the motion-vector attachment (cleared at the frame's first use).
    VkRenderingAttachmentInfo cas[2] = {ca, {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}};
    bool withMotion = color == backBuffer_.get() && motion_;
    if (withMotion)
    {
        motion_->Transition(cb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        cas[1].imageView = motion_->AttachmentView(0, 0);
        cas[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        cas[1].loadOp = motionCleared_ ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        cas[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        motionCleared_ = true;
    }
    uint32_t w = color->width >> rt_->level, h = color->height >> rt_->level;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {w ? w : 1, h ? h : 1}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = withMotion ? 2 : 1;
    ri.pColorAttachments = cas;
    ri.pDepthAttachment = depth ? &da : nullptr;
    ri.pStencilAttachment = depth && (depth->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) ? &da : nullptr;
    vkCmdBeginRendering(cb, &ri);
    rendering_ = true;
}

void VulkanDevice::EndRendering()
{
    if (!rendering_)
        return;
    vkCmdEndRendering(Slot().cb);
    rendering_ = false;
}

// Soft particles: the depth layout the following draws need. A rendering in the other layout ends here; the next
// BeginRendering transitions the depth image.
void VulkanDevice::DepthReadOnly(bool readOnly)
{
    depthReadOnly_ = readOnly;
    if (rendering_ && renderingReadOnly_ != (readOnly && depth_))
        EndRendering();
}

// The soft particle variant (vk_abi.h kSoft*) for the current state, kSoftNone when this draw is not one: a draw of
// the game's particle phase (rhi::Hints().particlePass, hook/enhance/render.cpp) into the 3D scene, depth tested
// without depth or stencil writes, before the UI, blended with a mode whose contribution can be faded out.
uint8_t VulkanDevice::SoftParticleMode() const
{
    const rhi::SceneHints& h = rhi::Hints();
    if (!h.softParticles || !h.particlePass || sceneEnded_ || !SceneDrawTarget() || rs_[D3DRS_ZWRITEENABLE] ||
        !rs_[D3DRS_ALPHABLENDENABLE] || rs_[D3DRS_ZFUNC] == D3DCMP_ALWAYS || rs_[D3DRS_STENCILENABLE] ||
        !(h.softParticlesRange > 0.0f))
        return kSoftNone;
    if (rs_[D3DRS_BLENDOP] != D3DBLENDOP_ADD)
        return kSoftNone;
    // The scene's camera depth needs a perspective projection (the last depth-writing scene draw's).
    if (sceneProjection_._34 == 0.0f || sceneProjection_._43 == 0.0f)
        return kSoftNone;
    const DWORD src = rs_[D3DRS_SRCBLEND], dst = rs_[D3DRS_DESTBLEND];
    if (src == D3DBLEND_SRCALPHA && (dst == D3DBLEND_INVSRCALPHA || dst == D3DBLEND_ONE))
        return kSoftAlpha;
    if (src == D3DBLEND_ONE && (dst == D3DBLEND_ONE || dst == D3DBLEND_INVSRCALPHA || dst == D3DBLEND_INVSRCCOLOR))
        return kSoftScale;
    if (src == D3DBLEND_ZERO && dst == D3DBLEND_INVSRCCOLOR)
        return kSoftScale;
    if ((src == D3DBLEND_DESTCOLOR && dst == D3DBLEND_ZERO) || (src == D3DBLEND_ZERO && dst == D3DBLEND_SRCCOLOR))
        return kSoftModulate;
    if (src == D3DBLEND_DESTCOLOR && dst == D3DBLEND_SRCCOLOR)
        return kSoftModulate2;
    return kSoftNone;
}

HRESULT VulkanDevice::Present(const RECT*, const RECT*, HWND, const RGNDATA*)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    TickScope presentTiming(presentTicks_);
    Context& c = Ctx();
    static const int verbose = ConfigInt("render", "vulkan_verbose", 0);
    if (verbose && serial % 120 == 0)
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        auto ms = [&](long long ticks) { return ticks * 1000.0 / f.QuadPart / 120; };
        Log("vulkan: per frame (120 frames): %u draws, %.2f ms preparing them, %.2f ms in Present, %.2f ms waiting "
            "for the GPU (frame fence), %.1f texture uploads in %.2f ms, %.1f new buffer versions",
            drawsCounted_ / 120, ms(prepareTicks_), ms(presentTicks_), ms(waitTicks_), Counters().uploads / 120.0,
            ms(Counters().uploadTicks), Counters().bufferVersions / 120.0);
        Counters() = ResourceCounters{};
        prepareTicks_ = presentTicks_ = waitTicks_ = 0;
        drawsCounted_ = 0;
        Log("vulkan: per frame (120 frames): %.1f soft particle draws", softDraws_ / 120.0);
        softDraws_ = 0;
    }
    if (verbose && (serial == 1 || serial % 100 == 0))
        Log("vulkan: present %llu", static_cast<unsigned long long>(serial));
    EndRendering();
    FrameSlot& s = Slot();
    uint32_t index = 0;
    VkResult r = vkAcquireNextImageKHR(c.device, swapchain_, UINT64_MAX, s.acquired, VK_NULL_HANDLE, &index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        vkDeviceWaitIdle(c.device);
        CreateSwapchain();
        r = vkAcquireNextImageKHR(c.device, swapchain_, UINT64_MAX, s.acquired, VK_NULL_HANDLE, &index);
    }
    bool acquired = r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
    VkCommandBuffer cb = s.cb;
    // HDR back buffer -> present pass (tonemap or debug view) -> 8-bit present image -> swapchain.
    if (!motionCleared_)  // nothing drew into the back buffer this frame: clear motion for the debug view
    {
        motion_->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearColorValue zero{};
        VkImageSubresourceRange rr{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, motion_->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &rr);
    }
    if (verbose && serial % 100 == 0 && Tess().on)
        Log("vulkan: frame %llu: %u of %u scene shader draws tessellated (%u skinned, %u with normals)",
            static_cast<unsigned long long>(serial), tessDraws_, tessCandidates_, tessSkinned_, tessWithNormal_);
    tessDraws_ = tessCandidates_ = tessSkinned_ = tessWithNormal_ = 0;
    if (verbose && serial % 100 == 0)
        Log("vulkan: scene projection _11 %g _22 %g _33 %g _34 %g _43 %g _44 %g", sceneProjection_._11,
            sceneProjection_._22, sceneProjection_._33, sceneProjection_._34, sceneProjection_._43, sceneProjection_._44);
    presentPass_.Record(cb, *backBuffer_, autoDepth_.get(), motion_.get(), *present_, &sceneProjection_._11);
    if (acquired)
    {
        present_->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = swapImages_[index];
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cb, &di);
        // The image keeps its proportions in the window (black bars), as a monitor scaling a mode would.
        VkClearColorValue black{};
        VkImageSubresourceRange whole{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, swapImages_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &whole);
        double sx = double(swapExtent_.width) / present_->width, sy = double(swapExtent_.height) / present_->height;
        double fit = sx < sy ? sx : sy;
        int32_t w = static_cast<int32_t>(present_->width * fit + 0.5), h = static_cast<int32_t>(present_->height * fit + 0.5);
        int32_t x = (static_cast<int32_t>(swapExtent_.width) - w) / 2, y = (static_cast<int32_t>(swapExtent_.height) - h) / 2;
        VkImageBlit blit{};
        blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {static_cast<int32_t>(present_->width), static_cast<int32_t>(present_->height), 1};
        blit.dstOffsets[0] = {x, y, 0};
        blit.dstOffsets[1] = {x + w, y + h, 1};
        vkCmdBlitImage(cb, present_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapImages_[index],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dstAccessMask = 0;
        vkCmdPipelineBarrier2(cb, &di);
    }
    // [test] hash_frames + dump_hashed: the back buffer to and-frame-N.bmp, as the D3D9 path does.
    unsigned dumpFrame = 0;
    VkBuffer dumpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory dumpMemory = VK_NULL_HANDLE;
    if (TestFrameDump(&dumpFrame))
    {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = VkDeviceSize(present_->width) * present_->height * 4;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkCreateBuffer(c.device, &bi, nullptr, &dumpBuffer);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(c.device, dumpBuffer, &req);
        dumpMemory = c.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(c.device, dumpBuffer, dumpMemory, 0);
        present_->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {present_->width, present_->height, 1};
        vkCmdCopyImageToBuffer(cb, present_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dumpBuffer, 1, &region);
    }
    if (captureFrame_)  // scene vertex capture: vertex-shader writes visible to the host after the fence
    {
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mb.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        mb.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        mb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        di.memoryBarrierCount = 1;
        di.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cb, &di);
    }
    vkEndCommandBuffer(cb);
    vkResetFences(c.device, 1, &s.fence);
    // Waits: the swapchain image, and the ray-tracing helper's work for this frame when it accepted it (the second
    // half of the frame reads its visibility; a frame it did not accept is not waited for, so it cannot hang us).
    VkSemaphore waits[4];
    VkPipelineStageFlags stages[4];
    uint64_t values[4] = {0, 0, 0, 0};
    uint32_t nWaits = 0;
    if (acquired)
        waits[nWaits] = s.acquired, stages[nWaits++] = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const uint32_t binaryWaits = nWaits;
    nWaits = TakeHelperWaits(waits, stages, values, nWaits);
    VkTimelineSemaphoreSubmitInfo ts{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    ts.waitSemaphoreValueCount = nWaits;
    ts.pWaitSemaphoreValues = values;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    if (nWaits > binaryWaits)
        si.pNext = &ts;
    si.waitSemaphoreCount = nWaits;
    si.pWaitSemaphores = waits;
    si.pWaitDstStageMask = stages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = acquired ? 1 : 0;
    si.pSignalSemaphores = &s.rendered;
    vkQueueSubmit(c.queue, 1, &si, s.fence);
    if (acquired)
    {
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &s.rendered;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain_;
        pi.pImageIndices = &index;
        vkQueuePresentKHR(c.queue, &pi);
    }
    if (DumpSceneFrame() == serial)
    {
        vkWaitForFences(c.device, 1, &s.fence, VK_TRUE, UINT64_MAX);  // captured vertices are written
        DumpScene(scene_);
    }
    if (dumpBuffer)
    {
        vkWaitForFences(c.device, 1, &s.fence, VK_TRUE, UINT64_MAX);
        void* p;
        vkMapMemory(c.device, dumpMemory, 0, VK_WHOLE_SIZE, 0, &p);
        TestSaveBgra(dumpFrame, p, present_->width, present_->height, present_->width * 4);
        vkUnmapMemory(c.device, dumpMemory);
        vkDestroyBuffer(c.device, dumpBuffer, nullptr);
        vkFreeMemory(c.device, dumpMemory, nullptr);
    }
    ++serial;
    BeginFrame();
    HarnessOnFrame();
    return D3D_OK;
}

HRESULT VulkanDevice::Clear(DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    if (flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL))
        DepthReadOnly(false);  // the clear writes the depth attachment
    BeginRendering();
    if (!rendering_)
        return D3D_OK;
    VkClearAttachment att[2];
    uint32_t n = 0;
    if (flags & D3DCLEAR_TARGET)
    {
        att[n] = {VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
        ColorToFloat(color, att[n].clearValue.color.float32);
        D3DFORMAT f = rt_ && rt_->GetImage() ? rt_->GetImage()->d3dFormat : D3DFMT_UNKNOWN;
        if (f == D3DFMT_X8R8G8B8 || f == D3DFMT_X1R5G5B5)
            att[n].clearValue.color.float32[3] = 1.0f;  // X formats read alpha as 1 (the float target stores it)
        ++n;
    }
    Image* depth = depth_ ? depth_->GetImage() : nullptr;
    VkImageAspectFlags da = 0;
    if (depth && (flags & D3DCLEAR_ZBUFFER))
        da |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (depth && (flags & D3DCLEAR_STENCIL) && (depth->aspect & VK_IMAGE_ASPECT_STENCIL_BIT))
        da |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if (da)
    {
        att[n] = {da, 0, {}};
        att[n].clearValue.depthStencil = {z, stencil};
        ++n;
    }
    if (!n)
        return D3D_OK;
    // D3D9 clears the viewport (intersected with the rectangles, if any).
    std::vector<VkClearRect> cr;
    VkRect2D vp{{static_cast<int32_t>(viewport_.X), static_cast<int32_t>(viewport_.Y)}, {viewport_.Width, viewport_.Height}};
    if (count && rects)
        for (DWORD i = 0; i < count; ++i)
        {
            LONG x1 = rects[i].x1 > LONG(viewport_.X) ? rects[i].x1 : LONG(viewport_.X);
            LONG y1 = rects[i].y1 > LONG(viewport_.Y) ? rects[i].y1 : LONG(viewport_.Y);
            LONG x2 = rects[i].x2 < LONG(viewport_.X + viewport_.Width) ? rects[i].x2 : LONG(viewport_.X + viewport_.Width);
            LONG y2 = rects[i].y2 < LONG(viewport_.Y + viewport_.Height) ? rects[i].y2 : LONG(viewport_.Y + viewport_.Height);
            if (x2 > x1 && y2 > y1)
                cr.push_back({{{x1, y1}, {static_cast<uint32_t>(x2 - x1), static_cast<uint32_t>(y2 - y1)}}, 0, 1});
        }
    else
        cr.push_back({vp, 0, 1});
    if (!cr.empty())
        vkCmdClearAttachments(Slot().cb, n, att, static_cast<uint32_t>(cr.size()), cr.data());
    return D3D_OK;
}

HRESULT VulkanDevice::StretchRect(Surface* src, const RECT* sr, Surface* dst, const RECT* dr, D3DTEXTUREFILTERTYPE f)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    auto* s = static_cast<VulkanSurface*>(src);
    auto* d = static_cast<VulkanSurface*>(dst);
    Image* si = s ? s->GetImage() : nullptr;
    Image* di = d ? d->GetImage() : nullptr;
    if (!si || !di)
        return D3DERR_INVALIDCALL;
    static const int verbose = ConfigInt("render", "vulkan_verbose", 0);
    if (verbose >= 2)
        Log("vulkan: StretchRect %ux%u f%d face%u -> %ux%u f%d face%u faces%u (frame %llu)", si->width, si->height,
            static_cast<int>(si->d3dFormat), s->face, di->width, di->height, static_cast<int>(di->d3dFormat), d->face,
            di->faces, static_cast<unsigned long long>(serial));
    auto full = [](Image* img, uint32_t level) {
        return RECT{0, 0, LONG(img->width >> level ? img->width >> level : 1), LONG(img->height >> level ? img->height >> level : 1)};
    };
    RECT a = sr ? *sr : full(si, s->level), b = dr ? *dr : full(di, d->level);
    Transfer([&](VkCommandBuffer cb) {
        VkImageLayout siOld = si->layout, diOld = di->layout;
        si->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        di->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit blit{};
        blit.srcSubresource = {si->aspect, s->level, s->face, 1};
        blit.dstSubresource = {di->aspect, d->level, d->face, 1};
        blit.srcOffsets[0] = {a.left, a.top, 0};
        blit.srcOffsets[1] = {a.right, a.bottom, 1};
        blit.dstOffsets[0] = {b.left, b.top, 0};
        blit.dstOffsets[1] = {b.right, b.bottom, 1};
        vkCmdBlitImage(cb, si->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, di->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, f == D3DTEXF_POINT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
        si->Transition(cb, siOld == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : siOld);
        di->Transition(cb, di->renderTarget ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        (void)diOld;
    });
    return D3D_OK;
}

HRESULT VulkanDevice::SetRenderTarget(DWORD index, Surface* s)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    if (index != 0)
        return s ? D3DERR_INVALIDCALL : D3D_OK;  // one target (the game's caps path)
    EndRendering();
    static const int dumpTargets = ConfigInt("render", "vk_dump_targets", 0);
    if (dumpTargets && s)
    {
        auto* vs = static_cast<VulkanSurface*>(s);
        Image* img = vs->GetImage();
        Log("vulkan: SetRenderTarget surface %p image %p face %u cube %d (frame %llu)", static_cast<void*>(vs),
            static_cast<void*>(img), vs->face, img && img->faces == 6 ? 1 : 0, static_cast<unsigned long long>(serial));
    }
    SetRef(rt_, static_cast<VulkanSurface*>(s));
    if (Image* img = rt_ ? rt_->GetImage() : nullptr)  // D3D9 resets the viewport to the new target
        viewport_ = {0, 0, img->width >> rt_->level, img->height >> rt_->level, 0, 1};
    ffDirty_ = true;
    return D3D_OK;
}

HRESULT VulkanDevice::SetDepthStencilSurface(Surface* s)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    EndRendering();
    SetRef(depth_, static_cast<VulkanSurface*>(s));
    return D3D_OK;
}

HRESULT VulkanDevice::SetTransform(D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m)
{
    if (s == D3DTS_VIEW)
        view_ = *m;
    else if (s == D3DTS_PROJECTION)
        projection_ = *m;
    else if (s >= D3DTS_TEXTURE0 && s <= D3DTS_TEXTURE7)
        texture_[s - D3DTS_TEXTURE0] = *m;
    else if (s >= D3DTS_WORLDMATRIX(0) && s <= D3DTS_WORLDMATRIX(3))
        world_[s - D3DTS_WORLDMATRIX(0)] = *m;
    ffDirty_ = true;
    return D3D_OK;
}

// --- draws -----------------------------------------------------------------------------------------------------

uint32_t VulkanDevice::VertexCount(D3DPRIMITIVETYPE t, UINT n) const
{
    switch (t)
    {
    case D3DPT_POINTLIST: return n;
    case D3DPT_LINELIST: return n * 2;
    case D3DPT_LINESTRIP: return n + 1;
    case D3DPT_TRIANGLELIST: return n * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN: return n + 2;
    default: return 0;
    }
}

VkDeviceSize VulkanDevice::PushUniform(const void* data, VkDeviceSize size)
{
    FrameSlot& s = Slot();
    VkDeviceSize align = Ctx().properties.limits.minUniformBufferOffsetAlignment;
    VkDeviceSize at = (s.uniformUsed + align - 1) / align * align;
    if (at + size > kUniformRing)
    {
        Log("vulkan: uniform ring full this frame");
        at = 0;
    }
    std::memcpy(s.uniformMapped + at, data, static_cast<size_t>(size));
    s.uniformUsed = at + size;
    return at;
}

FFKey VulkanDevice::BuildFFKey(uint32_t vertexMask, const uint8_t texCoordSize[8])
{
    FFKey k{};
    for (int i = 0; i < 8; ++i)
    {
        FFStage& st = k.stage[i];
        st.colorOp = static_cast<uint8_t>(tss_[i][D3DTSS_COLOROP]);
        if (st.colorOp == D3DTOP_DISABLE)
        {
            for (int j = i; j < 8; ++j)
                k.stage[j].colorOp = k.stage[j].alphaOp = D3DTOP_DISABLE;
            break;
        }
        st.colorArg1 = static_cast<uint8_t>(tss_[i][D3DTSS_COLORARG1]);
        st.colorArg2 = static_cast<uint8_t>(tss_[i][D3DTSS_COLORARG2]);
        st.colorArg0 = static_cast<uint8_t>(tss_[i][D3DTSS_COLORARG0]);
        st.alphaOp = static_cast<uint8_t>(tss_[i][D3DTSS_ALPHAOP]);
        st.alphaArg1 = static_cast<uint8_t>(tss_[i][D3DTSS_ALPHAARG1]);
        st.alphaArg2 = static_cast<uint8_t>(tss_[i][D3DTSS_ALPHAARG2]);
        st.alphaArg0 = static_cast<uint8_t>(tss_[i][D3DTSS_ALPHAARG0]);
        st.resultArg = static_cast<uint8_t>(tss_[i][D3DTSS_RESULTARG]);
        st.texCoordIndex = static_cast<uint8_t>(tss_[i][D3DTSS_TEXCOORDINDEX] & 0xff);
        st.texCoordGen = static_cast<uint8_t>(tss_[i][D3DTSS_TEXCOORDINDEX] >> 16);
        DWORD ttf = tss_[i][D3DTSS_TEXTURETRANSFORMFLAGS];  // D3DTTFF_PROJECTED (0x100) is stored as bit 0x80
        st.transformFlags = static_cast<uint8_t>((ttf & 0x7f) | (ttf & D3DTTFF_PROJECTED ? 0x80 : 0));
        st.textureType = textures_[i] ? (textures_[i]->image->faces == 6 ? 2 : 1) : 0;
    }
    k.vertexMask = vertexMask;
    std::memcpy(k.vertexTexCoordSize, texCoordSize, 8);
    k.lighting = rs_[D3DRS_LIGHTING] ? 1 : 0;
    for (int i = 0; i < 8; ++i)
        k.lightCount += lightOn_[i] ? 1 : 0;
    k.specularEnable = rs_[D3DRS_SPECULARENABLE] ? 1 : 0;
    k.normalizeNormals = rs_[D3DRS_NORMALIZENORMALS] ? 1 : 0;
    k.localViewer = rs_[D3DRS_LOCALVIEWER] ? 1 : 0;
    k.colorVertex = rs_[D3DRS_COLORVERTEX] ? 1 : 0;
    k.diffuseSource = static_cast<uint8_t>(rs_[D3DRS_DIFFUSEMATERIALSOURCE]);
    k.ambientSource = static_cast<uint8_t>(rs_[D3DRS_AMBIENTMATERIALSOURCE]);
    k.specularSource = static_cast<uint8_t>(rs_[D3DRS_SPECULARMATERIALSOURCE]);
    k.emissiveSource = static_cast<uint8_t>(rs_[D3DRS_EMISSIVEMATERIALSOURCE]);
    k.fogEnable = rs_[D3DRS_FOGENABLE] ? 1 : 0;
    k.fogTableMode = static_cast<uint8_t>(rs_[D3DRS_FOGTABLEMODE]);
    k.fogVertexMode = static_cast<uint8_t>(rs_[D3DRS_FOGVERTEXMODE]);
    k.rangeFogEnable = rs_[D3DRS_RANGEFOGENABLE] ? 1 : 0;
    k.alphaTestEnable = rs_[D3DRS_ALPHATESTENABLE] ? 1 : 0;
    k.alphaFunc = static_cast<uint8_t>(rs_[D3DRS_ALPHAFUNC]);
    k.vertexBlend = static_cast<uint8_t>(rs_[D3DRS_VERTEXBLEND]);
    k.indexedVertexBlend = rs_[D3DRS_INDEXEDVERTEXBLENDENABLE] ? 1 : 0;
    k.shadeFlat = rs_[D3DRS_SHADEMODE] == D3DSHADE_FLAT ? 1 : 0;
    k.vsIsShader = vs_ ? 1 : 0;
    k.psIsShader = ps_ ? 1 : 0;
    if (!k.fogEnable)
        k.fogTableMode = k.fogVertexMode = 0;
    if (!k.alphaTestEnable)
        k.alphaFunc = 0;
    return k;
}

bool VulkanDevice::PrepareDraw(D3DPRIMITIVETYPE type, uint64_t drawKey)
{
    TickScope timing(prepareTicks_);
    ++drawsCounted_;
    if (!layout_ || !rt_)
        return false;
    // Vertex input
    uint32_t strides[16];
    for (int i = 0; i < 16; ++i)
        strides[i] = streamStride_[i];
    uint64_t inputKey = Hash(strides, sizeof(strides), layout_->hash);
    auto it = inputs_.find(inputKey);
    if (it == inputs_.end())
        it = inputs_.emplace(inputKey, BuildVertexInput(layout_->elements.data(), strides)).first;
    const VertexInput& input = it->second;

    // Shaders: translated where the game bound shaders, generated fixed function otherwise.
    FFKey key = BuildFFKey(input.mask, input.texCoordSize);
    uint64_t keyHash = Hash(&key, sizeof(key));
    VkShaderModule vsm, fsm;
    uint32_t samplerMask, cubeMask;
    if (vs_)
        vsm = vs_->module;
    else
    {
        VkShaderModule& m = ffVertex_[keyHash];
        if (!m)
            m = MakeModule(CompileGlsl(FixedFunctionVertex(key).glsl, false));
        vsm = m;
    }
    // Soft particles (vk_abi.h): a pixel shader variant only (the vertex shader above was chosen without it).
    const uint8_t soft = SoftParticleMode();
    key.softParticle = soft;
    uint64_t pixelHash = keyHash;
    // [render] soft_log=N (diagnosis): every blended draw without depth writes of frame N, and whether it is soft.
    static const int softLog = ConfigInt("render", "soft_log", -1);
    if (softLog >= 0 && serial == uint64_t(softLog) && rs_[D3DRS_ALPHABLENDENABLE] && !rs_[D3DRS_ZWRITEENABLE])
        Log("softlog: phase %d ended %d scene %d z %d zf %d st %d src %d dst %d op %d vs %d ps %d at %d posT %d soft %d "
            "p33 %g p34 %g p43 %g",
            rhi::Hints().particlePass, sceneEnded_, SceneDrawTarget(), rs_[D3DRS_ZENABLE], rs_[D3DRS_ZFUNC],
            rs_[D3DRS_STENCILENABLE], rs_[D3DRS_SRCBLEND], rs_[D3DRS_DESTBLEND], rs_[D3DRS_BLENDOP], vs_ ? 1 : 0,
            ps_ ? 1 : 0, rs_[D3DRS_ALPHATESTENABLE], (key.vertexMask >> 17) & 1, soft, sceneProjection_._33,
            sceneProjection_._34, sceneProjection_._43);
    if (soft)
    {
        pixelHash = Hash(&key, sizeof(key));
        const float params[4] = {sceneProjection_._33, sceneProjection_._43, sceneProjection_._34,
                                 1.0f / rhi::Hints().softParticlesRange};
        if (std::memcmp(params, softParams_, sizeof(params)))
            std::memcpy(softParams_, params, sizeof(params)), ffDirty_ = true;
        ++softDraws_;
    }
    if (ps_)
    {
        fsm = ps_->shader->PixelVariant(key);
        samplerMask = ps_->shader->translated.samplerMask;
        cubeMask = ps_->shader->translated.samplerCubeMask;
    }
    else
    {
        VkShaderModule& m = ffPixel_[pixelHash];
        FFPixelMasks& masks = ffPixelMasks_[pixelHash];
        if (!m || !masks.known)
        {
            GeneratedShader g = FixedFunctionPixel(key);
            if (!m)
                m = MakeModule(CompileGlsl(g.glsl, true));
            masks = {g.samplerMask, g.samplerCubeMask, true};
        }
        fsm = m;
        samplerMask = masks.sampler;
        cubeMask = masks.cube;
    }
    if (!vsm || !fsm)
        return false;
    (void)cubeMask;
    lastInput_ = &input;  // for the scene record (shadow map replay): before any variant replaces vsm
    lastVertex_ = vsm;

    // Tessellation of character (and with tess_props=1 prop) meshes: translated vertex shaders with normals, in the
    // 3D scene, triangle primitives. Off on capture frames (the capture variant has no tessellation outputs).
    tessDraw_ = false;
    const TessConfig& tc = Tess();
    if (tc.on && vs_ && SceneDrawTarget())
    {
        ++tessCandidates_;
        tessSkinned_ += vs_->translated.relativeConstants ? 1 : 0;
        tessWithNormal_ += vs_->translated.hasNormal ? 1 : 0;
    }
    if (tc.on && vs_ && vs_->translated.hasNormal && (vs_->translated.relativeConstants || tc.props) &&
        SceneDrawTarget() && !captureFrame_ && !key.shadeFlat &&
        (type == D3DPT_TRIANGLELIST || type == D3DPT_TRIANGLESTRIP || type == D3DPT_TRIANGLEFAN) && TessModules())
    {
        if (VkShaderModule tm = vs_->TessVariant())
        {
            vsm = tm;
            tessDraw_ = true;
            ++tessDraws_;
            float decode = 0.0f;  // the NORMAL input's decoding (vk_abi.h FFUniforms::tessParams.x)
            for (const D3DVERTEXELEMENT9& e : layout_->elements)
                if (e.Stream != 0xff && e.Usage == D3DDECLUSAGE_NORMAL && e.UsageIndex == 0)
                    decode = e.Type == D3DDECLTYPE_UBYTE4 ? 1.0f
                             : (e.Type == D3DDECLTYPE_D3DCOLOR || e.Type == D3DDECLTYPE_UBYTE4N) ? 2.0f
                                                                                                 : 0.0f;
            if (decode != tessNormalDecode_)
                tessNormalDecode_ = decode, ffDirty_ = true;
        }
    }

    // Scene vertex capture: this draw's vertices [captureFirst_, +captureCount_) to the capture buffer.
    captureDraw_ = false;
    if (captureFrame_ && captureCount_ && captureFirst_ >= 0 && SceneDrawTarget() && EnsureCapture())
    {
        VkShaderModule cm = VK_NULL_HANDLE;
        if (vs_)
            cm = vs_->CaptureVariant();
        else
        {
            VkShaderModule& m = ffVertex_[keyHash ^ 1];  // capture variant of the fixed-function vertex shader
            if (!m)
                m = MakeModule(CompileGlsl(WithDefine(FixedFunctionVertex(key).glsl, kCaptureDefine), false));
            cm = m;
        }
        VkDeviceSize align = Ctx().properties.limits.minStorageBufferOffsetAlignment;
        VkDeviceSize at = (captureUsed_ + align - 1) / align * align, need = VkDeviceSize(captureCount_) * 16;
        if (cm && at + need <= captureSize_)
        {
            vsm = cm;
            captureOffset_ = at;
            captureUsed_ = at + need;
            captureDraw_ = true;
            ffDirty_ = true;  // halfPixel.z carries the base vertex
        }
    }

    // Textures must be readable: leave rendering to transition any that are not.
    VkCommandBuffer cb = Slot().cb;
    for (uint32_t i = 0; i < kMaxSamplers; ++i)
        if ((samplerMask & (1u << i)) && textures_[i] && textures_[i]->image->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            EndRendering();
            textures_[i]->image->Transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    // Soft particles sample the depth: read-only depth from the first one until something writes depth or stencil.
    if (soft)
        DepthReadOnly(true);
    else if (depthReadOnly_ && ((rs_[D3DRS_ZENABLE] && rs_[D3DRS_ZWRITEENABLE]) || rs_[D3DRS_STENCILENABLE]))
        DepthReadOnly(false);
    BeginRendering();
    if (!rendering_)
        return false;

    // Pipeline
    PipelineKey pk{};
    RasterKey& r = pk.raster;
    r.fillMode = static_cast<uint8_t>(rs_[D3DRS_FILLMODE]);
    r.cullMode = static_cast<uint8_t>(rs_[D3DRS_CULLMODE]);
    Image* depth = depth_ ? depth_->GetImage() : nullptr;
    r.zEnable = depth ? static_cast<uint8_t>(rs_[D3DRS_ZENABLE]) : 0;
    r.zWriteEnable = rs_[D3DRS_ZWRITEENABLE] ? 1 : 0;
    r.zFunc = static_cast<uint8_t>(rs_[D3DRS_ZFUNC]);
    r.alphaBlendEnable = rs_[D3DRS_ALPHABLENDENABLE] ? 1 : 0;
    r.srcBlend = static_cast<uint8_t>(rs_[D3DRS_SRCBLEND]);
    r.destBlend = static_cast<uint8_t>(rs_[D3DRS_DESTBLEND]);
    r.blendOp = static_cast<uint8_t>(rs_[D3DRS_BLENDOP]);
    r.separateAlphaBlend = rs_[D3DRS_SEPARATEALPHABLENDENABLE] ? 1 : 0;
    r.srcBlendAlpha = static_cast<uint8_t>(rs_[D3DRS_SRCBLENDALPHA]);
    r.destBlendAlpha = static_cast<uint8_t>(rs_[D3DRS_DESTBLENDALPHA]);
    r.blendOpAlpha = static_cast<uint8_t>(rs_[D3DRS_BLENDOPALPHA]);
    r.stencilEnable = depth && rs_[D3DRS_STENCILENABLE] ? 1 : 0;
    r.stencilFunc = static_cast<uint8_t>(rs_[D3DRS_STENCILFUNC]);
    r.stencilFail = static_cast<uint8_t>(rs_[D3DRS_STENCILFAIL]);
    r.stencilZFail = static_cast<uint8_t>(rs_[D3DRS_STENCILZFAIL]);
    r.stencilPass = static_cast<uint8_t>(rs_[D3DRS_STENCILPASS]);
    r.colorWriteEnable = static_cast<uint8_t>(rs_[D3DRS_COLORWRITEENABLE] & 0xf);
    D3DFORMAT rtFormat = rt_->GetImage()->d3dFormat;
    if (rtFormat == D3DFMT_X8R8G8B8 || rtFormat == D3DFMT_X1R5G5B5)
        r.colorWriteEnable &= 0x7;  // X formats have no alpha to write: D3D9 reads their alpha as 1
    r.stencilMask = static_cast<uint8_t>(rs_[D3DRS_STENCILMASK]);
    r.stencilWriteMask = static_cast<uint8_t>(rs_[D3DRS_STENCILWRITEMASK]);
    r.depthBiasEnable = (rs_[D3DRS_DEPTHBIAS] || rs_[D3DRS_SLOPESCALEDEPTHBIAS]) ? 1 : 0;
    r.topology = static_cast<uint8_t>(type);
    pk.layoutHash = inputKey;
    VkShaderModule pair[2] = {vsm, fsm};
    pk.shaderHash = Hash(pair, sizeof(pair));
    pk.colorFormat = rt_->GetImage()->format;
    pk.depthFormat = depth ? depth->format : VK_FORMAT_UNDEFINED;
    pk.motionFormat = rt_->GetImage() == backBuffer_.get() ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_UNDEFINED;
    if (pk.motionFormat && r.zEnable && r.zWriteEnable)
        sceneProjection_ = projection_;  // the 3D camera, for the depth/normal views (the HUD sets its own last)
    VkPipeline pipeline = tessDraw_ ? GetPipeline(pk, input, vsm, fsm, tessControl_, tessEval_)
                                    : GetPipeline(pk, input, vsm, fsm);
    if (!pipeline)
        return false;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Uniforms
    if (ffDirty_)
    {
        FFUniforms u{};
        std::memcpy(u.world, world_, sizeof(world_));
        std::memcpy(u.view, &view_, 64);
        std::memcpy(u.projection, &projection_, 64);
        std::memcpy(u.texture, texture_, sizeof(texture_));
        float vw = viewport_.Width ? float(viewport_.Width) : 1.0f, vh = viewport_.Height ? float(viewport_.Height) : 1.0f;
        // D3D9 pixel centres are at integer coordinates, Vulkan's at +0.5: shift +0.5 px right and down, which with
        // the flipped (negative-height) viewport is (+1/w, -1/h) in clip units.
        u.halfPixel[0] = 1.0f / vw;
        u.halfPixel[1] = -1.0f / vh;
        // Temporal anti-aliasing: the scene's subpixel jitter (pixels, +y down) on top, scene draws only (the UI comes
        // after SceneEnd). The motion vectors are taken before this offset, so they carry no jitter.
        if (aaActive_ && !sceneEnded_ && rt_ && rt_->GetImage() == backBuffer_.get())
            u.halfPixel[0] += 2.0f * aaJitter_[0] / vw, u.halfPixel[1] -= 2.0f * aaJitter_[1] / vh;
        u.halfPixel[2] = captureDraw_ ? static_cast<float>(captureFirst_) : 0.0f;  // capture base vertex (vk_abi.h)
        u.tessParams[0] = tessNormalDecode_, u.tessParams[1] = Tess().shape;
        u.tessParams[2] = Tess().maxLevel, u.tessParams[3] = Tess().pixels;
        std::memcpy(u.soft, softParams_, sizeof(u.soft));  // soft particles (vk_abi.h)
        u.viewportSize[0] = vw, u.viewportSize[1] = vh, u.viewportSize[2] = viewport_.MinZ, u.viewportSize[3] = viewport_.MaxZ;
        ColorToFloat(rs_[D3DRS_TEXTUREFACTOR], u.textureFactor);
        ColorToFloat(rs_[D3DRS_FOGCOLOR], u.fogColor);
        u.fogParams[0] = U2F(rs_[D3DRS_FOGSTART]), u.fogParams[1] = U2F(rs_[D3DRS_FOGEND]), u.fogParams[2] = U2F(rs_[D3DRS_FOGDENSITY]);
        // Table fog distance. The D3D9 reference (NVIDIA driver, which reports D3DPRASTERCAPS_WFOG) fogs this game
        // by depth z, also for fixed-function draws with a W-friendly projection: measured with
        // tools/compare_backends.py, where only depth z matches (lev_1_m1 rocks at frame 200). [render] wfog=1 uses
        // W-based fog instead (WFOG cap + W-friendly projection), for drivers that do.
        bool wFriendly = projection_._34 == 1.0f && projection_._14 == 0.0f && projection_._24 == 0.0f && projection_._44 == 0.0f;
        u.fogParams[3] = (wFog_ && wFriendly && !vs_) ? 1.0f : 0.0f;
        if (rs_[D3DRS_FOGENABLE] && !sceneEnded_)  // the HUD and menus (after SceneEnd) keep the game's fog
            ModernFogUniforms(u.mf, rhi::Hints(), vs_ ? sceneProjection_ : projection_, viewport_,
                              vs_ ? vsConst_[4] : u.fogParams[0], vs_ ? vsConst_[5] : u.fogParams[1],
                              !vs_ && rs_[D3DRS_FOGTABLEMODE] != D3DFOG_NONE);
        else
            std::memset(&u.mf, 0, sizeof(u.mf));
        u.alphaRef[0] = (rs_[D3DRS_ALPHAREF] & 0xff) / 255.0f;
        // Remaster alpha coverage (vk_fixed_function.cpp FinalBody): scene draws under temporal AA only.
        static const bool alphaCoverage = ConfigIntDefault("render", "alpha_coverage", 0, 1) != 0;
        u.alphaRef[1] = static_cast<float>(aaFrame_ & 15);
        u.alphaRef[2] = alphaCoverage && aaActive_ && !sceneEnded_ ? 1.0f : 0.0f;
        u.alphaRef[3] = 0.0f;
        ColorToFloat(rs_[D3DRS_AMBIENT], u.ambient);
        std::memcpy(u.materialDiffuse, &material_.Diffuse, 16);
        std::memcpy(u.materialAmbient, &material_.Ambient, 16);
        std::memcpy(u.materialSpecular, &material_.Specular, 16);
        std::memcpy(u.materialEmissive, &material_.Emissive, 12);
        u.materialEmissive[3] = material_.Power;
        int n = 0;
        for (int i = 0; i < 8; ++i)
        {
            if (!lightOn_[i])
                continue;
            const D3DLIGHT9& l = lights_[i];
            std::memcpy(u.lightDiffuse[n], &l.Diffuse, 16);
            std::memcpy(u.lightSpecular[n], &l.Specular, 16);
            std::memcpy(u.lightAmbient[n], &l.Ambient, 16);
            std::memcpy(u.lightPosition[n], &l.Position, 12);
            u.lightPosition[n][3] = l.Range;
            std::memcpy(u.lightDirection[n], &l.Direction, 12);
            u.lightDirection[n][3] = float(l.Type);
            u.lightAttenuation[n][0] = l.Attenuation0, u.lightAttenuation[n][1] = l.Attenuation1;
            u.lightAttenuation[n][2] = l.Attenuation2, u.lightAttenuation[n][3] = l.Falloff;
            u.lightSpot[n][0] = std::cos(l.Theta * 0.5f), u.lightSpot[n][1] = std::cos(l.Phi * 0.5f);
            ++n;
        }
        for (int i = 0; i < 8; ++i)
        {
            u.bumpEnvMat[i][0] = U2F(tss_[i][D3DTSS_BUMPENVMAT00]), u.bumpEnvMat[i][1] = U2F(tss_[i][D3DTSS_BUMPENVMAT01]);
            u.bumpEnvMat[i][2] = U2F(tss_[i][D3DTSS_BUMPENVMAT10]), u.bumpEnvMat[i][3] = U2F(tss_[i][D3DTSS_BUMPENVMAT11]);
            u.bumpEnvLum[i][0] = U2F(tss_[i][D3DTSS_BUMPENVLSCALE]), u.bumpEnvLum[i][1] = U2F(tss_[i][D3DTSS_BUMPENVLOFFSET]);
        }
        ffOffset_ = PushUniform(&u, sizeof(u));
        ffDirty_ = false;
    }
    if (vsDirty_)
        vsOffset_ = PushUniform(vsConst_, sizeof(vsConst_)), vsDirty_ = false;
    if (psDirty_)
        psOffset_ = PushUniform(psConst_, sizeof(psConst_)), psDirty_ = false;

    // [render] vk_dump_draws=N: one line per draw of presented frame N (state that decides what a draw looks
    // like), to find run-to-run differences: and-vk-draws.txt.
    static const int dumpFrame = ConfigInt("render", "vk_dump_draws", -1);
    if (dumpFrame >= 0 && serial == static_cast<uint64_t>(dumpFrame))
    {
        static FILE* f = fopen("and-vk-draws.txt", "w");
        static int index = 0;
        if (f)
        {
            fprintf(f, "%d vs=%d ps=%d key=%016llx rt=%ux%u face%u", index++, vs_ ? 1 : 0, ps_ ? 1 : 0,
                    static_cast<unsigned long long>(keyHash), rt_->GetImage()->width, rt_->GetImage()->height,
                    rt_->face);
            for (uint32_t i = 0; i < kMaxSamplers; ++i)
            {
                if (!(samplerMask & (1u << i)) || !textures_[i])
                    continue;
                VulkanTexture* tx = textures_[i];
                uint64_t sum = 0;
                if (!tx->shadow.empty() && !tx->shadow[0].empty())
                    sum = Hash(tx->shadow[0].data(), tx->shadow[0].size());
                fprintf(f, " t%u=%ux%u f%d lod%lu lv%u cube%d rt%d sum%016llx", i, tx->image->width, tx->image->height,
                        static_cast<int>(tx->image->d3dFormat), tx->GetLOD(), tx->image->levels,
                        tx->image->faces == 6 ? 1 : 0, tx->image->renderTarget ? 1 : 0,
                        static_cast<unsigned long long>(sum));
            }
            fprintf(f, "\n");
            fflush(f);
        }
    }

    // [render] vk_dump_targets=1: frame, target and face of every draw into an off-screen target (and-vk-targets.txt)
    static const int dumpTargets = ConfigInt("render", "vk_dump_targets", 0);
    if (dumpTargets && rt_->GetImage() != backBuffer_.get())
    {
        static FILE* tf = fopen("and-vk-targets.txt", "w");
        if (tf)
            fprintf(tf, "%llu %ux%u f%d face%u cube%d\n", static_cast<unsigned long long>(serial), rt_->GetImage()->width,
                    rt_->GetImage()->height, static_cast<int>(rt_->GetImage()->d3dFormat), rt_->face,
                    rt_->GetImage()->faces == 6 ? 1 : 0);
    }

    // Previous-frame transforms of this draw (motion vectors, vk_abi.h): last frame's snapshot under the same draw
    // id, or the current transforms when it is new (no motion).
    PrevUniforms& cur = curSnaps_[drawKey];
    std::memcpy(cur.world, world_, sizeof(cur.world));
    std::memcpy(cur.view, &view_, 64);
    std::memcpy(cur.projection, &projection_, 64);
    std::memcpy(cur.vsConstants, vsConst_, sizeof(cur.vsConstants));
    auto prev = prevSnaps_.find(drawKey);
    VkDeviceSize prevOffset = PushUniform(prev != prevSnaps_.end() ? &prev->second : &cur, sizeof(PrevUniforms));

    // Descriptors
    FrameSlot& s = Slot();
    VkDescriptorBufferInfo bufs[4] = {{s.uniforms, vsOffset_, sizeof(vsConst_)},
                                      {s.uniforms, psOffset_, sizeof(psConst_)},
                                      {s.uniforms, ffOffset_, sizeof(FFUniforms)},
                                      {s.uniforms, prevOffset, sizeof(PrevUniforms)}};
    VkDescriptorImageInfo imgs[kMaxSamplers];
    VkWriteDescriptorSet writes[6 + kMaxSamplers];
    uint32_t nw = 0;
    for (uint32_t b = 0; b < 4; ++b)
    {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstBinding = b;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bufs[b];
        writes[nw++] = w;
    }
    for (uint32_t i = 0; i < kMaxSamplers; ++i)
    {
        if (!(samplerMask & (1u << i)))
            continue;
        Image* img = textures_[i] ? textures_[i]->image.get() : white_.get();
        // D3D9 SetLOD (managed textures): the most detailed level used; D3DSAMP_MAXMIPLEVEL does the same per stage.
        uint32_t base = textures_[i] ? textures_[i]->Lod() : 0;
        imgs[i] = {GetSampler(samp_[i]), img->SampledView(base), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstBinding = kBindingSampler0 + i;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &imgs[i];
        writes[nw++] = w;
    }
    VkDescriptorBufferInfo captureInfo{captureBuffer_, captureOffset_, VkDeviceSize(captureCount_) * 16};
    if (captureDraw_)
    {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstBinding = kBindingCapture;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo = &captureInfo;
        writes[nw++] = w;
    }
    VkDescriptorImageInfo sceneDepthInfo{};
    if (soft)  // the scene depth (vk_abi.h kBindingSceneDepth), also this rendering's read-only depth attachment
    {
        if (!shadowDepthSampler_)
        {
            VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            si.magFilter = si.minFilter = VK_FILTER_NEAREST;
            si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            vkCreateSampler(Ctx().device, &si, nullptr, &shadowDepthSampler_);
        }
        sceneDepthInfo = {shadowDepthSampler_, depth->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstBinding = kBindingSceneDepth;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &sceneDepthInfo;
        writes[nw++] = w;
    }
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout(), 0, nw, writes);

    // Vertex buffers
    for (const VkVertexInputBindingDescription& b : input.bindings)
    {
        VulkanBuffer* vb = streams_[b.binding];
        if (!vb)
            return false;
        VkDeviceSize off = streamOffset_[b.binding];
        vkCmdBindVertexBuffers(cb, b.binding, 1, &vb->Current().buffer, &off);
        vb->MarkUsed(serial);
    }

    // Dynamic state
    Image* target = rt_->GetImage();
    VkViewport v{float(viewport_.X), float(viewport_.Y + viewport_.Height), float(viewport_.Width),
                 -float(viewport_.Height), viewport_.MinZ, viewport_.MaxZ};
    vkCmdSetViewport(cb, 0, 1, &v);
    VkRect2D sc{{0, 0}, {target->width >> rt_->level, target->height >> rt_->level}};
    vkCmdSetScissor(cb, 0, 1, &sc);
    float depthUnits = depth && depth->format == VK_FORMAT_D16_UNORM ? 65535.0f : 16777215.0f;
    vkCmdSetDepthBias(cb, U2F(rs_[D3DRS_DEPTHBIAS]) * depthUnits, 0.0f, U2F(rs_[D3DRS_SLOPESCALEDEPTHBIAS]));
    float bf[4];
    ColorToFloat(rs_[D3DRS_BLENDFACTOR], bf);
    vkCmdSetBlendConstants(cb, bf);
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, rs_[D3DRS_STENCILREF] & 0xff);
    return true;
}

// Matches a draw with its previous-frame self for motion vectors: what it draws (buffers, range, shader, first
// texture) and the how-many-th such draw it is this frame, so repeated meshes (trees, characters) stay apart.
// [render] tessellation=1 and its parameters (docs/goals/remaster-basics.md item 5). Off by default.
//   tess_max=8         maximum tessellation level per edge
//   tess_pixels=6      target edge length in pixels (smaller: finer)
//   tess_shape=0.75    Phong shape factor: 0 flat, 1 fully curved
//   tess_props=0       1: also props and level meshes with normals (default: skinned meshes, i.e. characters)
//   tess_ccw=0         output winding of the evaluation stage (a debugging switch)
const TessConfig& Tess()
{
    static const TessConfig c = [] {
        TessConfig t;
        t.on = ConfigIntDefault("render", "tessellation", 0, 1) != 0 && Ctx().tessellation;
        t.props = ConfigInt("render", "tess_props", 0) != 0;
        t.maxLevel = ConfigFloat("render", "tess_max", 8.0f);
        t.pixels = ConfigFloat("render", "tess_pixels", 6.0f);
        t.shape = ConfigFloat("render", "tess_shape", 0.75f);
        t.ccw = ConfigInt("render", "tess_ccw", 0) != 0;
        if (t.on)
            Log("vulkan: tessellation on (max %.0f, %.0f px per segment, shape %.2f, props %d)", t.maxLevel, t.pixels,
                t.shape, t.props ? 1 : 0);
        return t;
    }();
    return c;
}

bool VulkanDevice::TessModules()
{
    if (tessControl_ && tessEval_)
        return true;
    static bool failed = false;
    if (failed)
        return false;
    tessControl_ = MakeModule(CompileGlslStage(TessControlGlsl(), GlslStage::TessControl));
    tessEval_ = MakeModule(CompileGlslStage(TessEvalGlsl(Tess().ccw), GlslStage::TessEval));
    failed = !tessControl_ || !tessEval_;
    if (failed)
        Log("vulkan: tessellation shaders failed to build; tessellation off");
    return !failed;
}

// A draw as a patch list: the triangle-list indices of its primitives (strips: odd triangles reordered so they keep
// D3D9's winding; fans around their first vertex; degenerate triangles dropped) in the frame's uniform ring.
bool VulkanDevice::DrawTessellated(D3DPRIMITIVETYPE t, const uint8_t* indices, bool index32, uint32_t indexLength,
                                   INT base, UINT start, UINT count)
{
    const uint32_t n = t == D3DPT_TRIANGLELIST ? count * 3 : count + 2;
    const uint32_t size = index32 ? 4 : 2;
    if (indices && (uint64_t(start) + n) * size > indexLength)
        return false;
    // Cached list: key from the source indices themselves (a rewritten buffer gets a new key).
    uint32_t meta[4] = {static_cast<uint32_t>(t), start, count, index32 ? 1u : 0u};
    uint64_t key = Hash(meta, sizeof(meta), indices ? Hash(indices + size_t(start) * size, size_t(n) * size) : 1);
    if (!tessBuffer_)
    {
        Context& c = Ctx();
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = kTessBuffer;
        bi.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (vkCreateBuffer(c.device, &bi, nullptr, &tessBuffer_) == VK_SUCCESS)
        {
            VkMemoryRequirements req;
            vkGetBufferMemoryRequirements(c.device, tessBuffer_, &req);
            tessMemory_ = c.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkBindBufferMemory(c.device, tessBuffer_, tessMemory_, 0);
            vkMapMemory(c.device, tessMemory_, 0, kTessBuffer, 0, reinterpret_cast<void**>(&tessMapped_));
        }
        if (!tessMapped_)
            return false;
    }
    auto it = tessLists_.find(key);
    if (it == tessLists_.end())
    {
        std::vector<uint32_t> v(n), list;
        for (uint32_t i = 0; i < n; ++i)
        {
            if (!indices)
                v[i] = start + i;
            else if (index32)
                std::memcpy(&v[i], indices + (size_t(start) + i) * 4, 4);
            else
            {
                uint16_t s16;
                std::memcpy(&s16, indices + (size_t(start) + i) * 2, 2);
                v[i] = s16;
            }
        }
        list.reserve(size_t(count) * 3);
        for (uint32_t p = 0; p < count; ++p)
        {
            uint32_t a, b, c;
            if (t == D3DPT_TRIANGLELIST)
                a = v[p * 3], b = v[p * 3 + 1], c = v[p * 3 + 2];
            else if (t == D3DPT_TRIANGLEFAN)
                a = v[0], b = v[p + 1], c = v[p + 2];
            else if (p & 1)
                a = v[p + 1], b = v[p], c = v[p + 2];
            else
                a = v[p], b = v[p + 1], c = v[p + 2];
            if (a == b || b == c || a == c)
                continue;
            list.push_back(a), list.push_back(b), list.push_back(c);
        }
        const VkDeviceSize bytes = VkDeviceSize(list.size()) * 4;
        if (tessUsed_ + bytes > kTessBuffer)
        {
            // Full (many levels' meshes): wait for the GPU, then start over.
            vkDeviceWaitIdle(Ctx().device);
            tessLists_.clear();
            tessUsed_ = 0;
            if (bytes > kTessBuffer)
                return false;
        }
        std::memcpy(tessMapped_ + tessUsed_, list.data(), static_cast<size_t>(bytes));
        it = tessLists_.emplace(key, TessList{tessUsed_, static_cast<uint32_t>(list.size())}).first;
        tessUsed_ += (bytes + 15) & ~VkDeviceSize(15);
    }
    if (!it->second.count)
        return true;
    VkCommandBuffer cb = Slot().cb;
    vkCmdBindIndexBuffer(cb, tessBuffer_, it->second.offset, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, it->second.count, 1, 0, indices ? base : 0, 0);
    return true;
}

// --- shadow maps (vk_shadows.h) ---------------------------------------------------------------------------------

bool VulkanDevice::ShadowInit(uint32_t size)
{
    if (shadowFailed_)
        return false;
    if (shadowMap_ && shadowMap_->width == size)
        return true;
    Context& c = Ctx();
    if (!shadowDepthFs_)
    {
        shadowDepthFs_ = MakeModule(CompileGlsl(ShadowDepthFragmentGlsl(), true));
        shadowApplyVs_ = MakeModule(CompileGlsl(ShadowApplyVertexGlsl(), false));
        shadowApplyFs_ = MakeModule(CompileGlsl(ShadowApplyFragmentGlsl(), true));
        VkDescriptorSetLayoutBinding b[4] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dl.bindingCount = 4;
        dl.pBindings = b;
        vkCreateDescriptorSetLayout(c.device, &dl, nullptr, &shadowSetLayout_);
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &shadowSetLayout_;
        vkCreatePipelineLayout(c.device, &pl, nullptr, &shadowLayout_);
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0.0f;
        vkCreateSampler(c.device, &si, nullptr, &shadowDepthSampler_);
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        si.compareEnable = VK_TRUE;
        si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        vkCreateSampler(c.device, &si, nullptr, &shadowCompareSampler_);
        if (!shadowDepthFs_ || !shadowApplyVs_ || !shadowApplyFs_ || !shadowLayout_)
        {
            Log("vulkan: shadow maps: shaders failed to build; off");
            shadowFailed_ = true;
            return false;
        }
    }
    if (shadowMap_)
        shadowMap_->Destroy();
    shadowMap_ = std::make_shared<Image>();
    if (!shadowMap_->Create(D3DFMT_D32, size, size, 1, 1, true, true))
    {
        Log("vulkan: shadow maps: %ux%u depth image failed; off", size, size);
        shadowFailed_ = true;
        return false;
    }
    Log("vulkan: shadow maps on (%ux%u)", size, size);
    return true;
}

VkPipeline VulkanDevice::ShadowApplyPipeline(VkFormat target)
{
    if (shadowApply_ && shadowApplyFormat_ == target)
        return shadowApply_;
    VkDevice device = Ctx().device;
    if (shadowApply_)
    {
        VkPipeline old = shadowApply_;
        DeferDestroy([device, old] { vkDestroyPipeline(device, old, nullptr); });
    }
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT, st[0].module = shadowApplyVs_, st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT, st[1].module = shadowApplyFs_, st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = VK_TRUE;  // dst * tint + the fog's share (dual source, vk_shadows.cpp), alpha kept
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE, att.dstColorBlendFactor = VK_BLEND_FACTOR_SRC1_COLOR;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &target;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext = &ri;
    ci.stageCount = 2;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = shadowLayout_;
    shadowApply_ = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &shadowApply_) != VK_SUCCESS)
        shadowApply_ = VK_NULL_HANDLE;
    shadowApplyFormat_ = target;
    return shadowApply_;
}

// Modern fog uniforms (vk_fog.h).
void ModernFogUniforms(ModernFogParams& p, const rhi::SceneHints& h, const D3DMATRIX& proj,
                       const D3DVIEWPORT9& viewport, float start, float end, bool depthUnits)
{
    std::memset(&p, 0, sizeof(p));
    p.proj[0] = proj._11, p.proj[1] = proj._22, p.proj[2] = proj._33, p.proj[3] = proj._43;
    p.proj2[0] = proj._34;
    if (!h.fogModern || !h.haveView)
        return;
    Mat4 view, toWorld;
    std::memcpy(view.m, h.view, sizeof(view.m));
    if (!Invert(view, toWorld))
        return;
    // Fog start and end as camera distances (table fog: 0..1 depth, inverted through the projection).
    auto distance = [&](float d) {
        if (!depthUnits)
            return std::fabs(d);
        float den = d * proj._34 - proj._33;
        return std::fabs(proj._43 / (std::fabs(den) < 1e-7f ? 1e-7f : den));
    };
    float s0 = distance(start), s1 = distance(end);
    float range = s1 - s0 > 1.0f ? s1 - s0 : 1.0f;
    static int logged;
    if (logged++ % 2000 == 0)
        Log("fog: start %.4f end %.4f (%s) -> distances %.2f .. %.2f; proj _33 %.4f _34 %.4f _43 %.4f", start, end,
            depthUnits ? "depth" : "distance", s0, s1, proj._33, proj._34, proj._43);
    p.on[0] = 1.0f;
    // Level with the camera, half the light gets through at the middle of the game's fog range, as with its linear
    // fog (and about 6% at its end).
    // Density at the camera: the calibrated density at the level's reference height (the camera height when the
    // level started), falling off with height above it: climbing thins the fog, valleys thicken it.
    static float refHeight;
    static bool haveRef;
    if (h.levelLoading)
        haveRef = false;
    else if (!haveRef)
        refHeight = toWorld.m[11], haveRef = true;
    float above = std::max(-200.0f, std::min(200.0f, toWorld.m[11] - refHeight));
    p.on[1] = std::log(2.0f) / (0.5f * range) * h.fogDensity * std::exp(-h.fogHeightFalloff * above);
    p.on[2] = h.fogHeightFalloff;
    p.on[3] = s0;
    p.proj[0] = proj._11, p.proj[1] = proj._22, p.proj[2] = proj._33, p.proj[3] = proj._43;
    p.proj2[0] = proj._34, p.proj2[1] = h.fogSunGlow;
    p.viewport[0] = float(viewport.X), p.viewport[1] = float(viewport.Y);
    p.viewport[2] = float(viewport.Width ? viewport.Width : 1), p.viewport[3] = float(viewport.Height ? viewport.Height : 1);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            p.toWorld[r][c] = toWorld.m[r * 4 + c];
    std::memcpy(p.sun, h.sun, sizeof(h.sun));
}

// --- Ray-traced sun shadows (vk_rt.h) ----------------------------------------------------------------------------

void VulkanDevice::RtFree(RtBuffer& b)
{
    if (!b.buffer)
        return;
    VkDevice device = Ctx().device;
    RtBuffer old = b;
    DeferDestroy([device, old] {
        vkDestroyBuffer(device, old.buffer, nullptr);
        vkFreeMemory(device, old.memory, nullptr);
    });
    b = RtBuffer{};
}

// A buffer of at least `size` bytes with a device address (host-visible and mapped when `host`); grows by half again.
bool VulkanDevice::RtEnsure(RtBuffer& b, VkDeviceSize size, VkBufferUsageFlags usage, bool host)
{
    if (b.buffer && b.size >= size)
        return true;
    RtFree(b);
    Context& c = Ctx();
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size + size / 2 + 4096;
    bi.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (vkCreateBuffer(c.device, &bi, nullptr, &b.buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, b.buffer, &req);
    b.memory = c.AllocateAddressable(req, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                               : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!b.memory)
        return false;
    vkBindBufferMemory(c.device, b.buffer, b.memory, 0);
    if (host)
        vkMapMemory(c.device, b.memory, 0, bi.size, 0, &b.mapped);
    VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    ai.buffer = b.buffer;
    b.address = vkGetBufferDeviceAddress(c.device, &ai);
    b.size = bi.size;
    return true;
}

bool VulkanDevice::RtInit()
{
    if (rtFailed_)
        return false;
    if (rtApplyLayout_)
        return true;
    VkDevice device = Ctx().device;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    p2.pNext = &asp;
    vkGetPhysicalDeviceProperties2(Ctx().physical, &p2);
    rtScratchAlign_ = asp.minAccelerationStructureScratchOffsetAlignment ? asp.minAccelerationStructureScratchOffsetAlignment : 256;
    rtNullFs_ = MakeModule(CompileGlsl("#version 450\nvoid main() {}\n", true));
    rtVs_ = MakeModule(CompileGlsl(RtShadowVertexGlsl(), false));
    rtFs_ = MakeModule(CompileGlsl(RtShadowFragmentGlsl(), true));
    VkDescriptorSetLayoutBinding b[3] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dl.bindingCount = 3;
    dl.pBindings = b;
    vkCreateDescriptorSetLayout(device, &dl, nullptr, &rtSetLayout_);
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &rtSetLayout_;
    vkCreatePipelineLayout(device, &pl, nullptr, &rtApplyLayout_);
    if (!rtNullFs_ || !rtVs_ || !rtFs_ || !rtApplyLayout_)
    {
        Log("vulkan: ray-traced shadows failed to build; shadow maps instead");
        rtFailed_ = true;
        return false;
    }
    Log("vulkan: ray-traced shadows ready");
    return true;
}

VkPipeline VulkanDevice::RtApplyPipeline(VkFormat target)
{
    if (rtApply_ && rtApplyFormat_ == target)
        return rtApply_;
    VkDevice device = Ctx().device;
    if (rtApply_)
    {
        VkPipeline old = rtApply_;
        DeferDestroy([device, old] { vkDestroyPipeline(device, old, nullptr); });
    }
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT, st[0].module = rtVs_, st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT, st[1].module = rtFs_, st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = VK_TRUE;  // dst * tint + the fog's share (dual source, as the shadow-map pass)
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE, att.dstColorBlendFactor = VK_BLEND_FACTOR_SRC1_COLOR;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbs.attachmentCount = 1;
    cbs.pAttachments = &att;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &target;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext = &ri;
    ci.stageCount = 2;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cbs;
    ci.pDynamicState = &dy;
    ci.layout = rtApplyLayout_;
    rtApply_ = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &rtApply_) != VK_SUCCESS)
        rtApply_ = VK_NULL_HANDLE;
    rtApplyFormat_ = target;
    return rtApply_;
}

// The casters' vertex ranges in one array (base per caster, ~0 = not used) and their triangles as a list
// (rtIndices_; strips and fans converted). Returns the vertex count.
uint64_t VulkanDevice::RtGather(const std::vector<RtCaster>& casters, std::vector<uint32_t>& baseOut)
{
    Context& ctx = Ctx();
    // 1. Vertex ranges and triangle indices (CPU; strips and fans become lists).
    const VkDeviceSize align = std::max<VkDeviceSize>(16, ctx.properties.limits.minStorageBufferOffsetAlignment);
    std::vector<uint32_t>& base = baseOut;
    base.assign(casters.size(), ~0u);
    std::vector<uint32_t>& idx = rtIndices_;
    idx.clear();
    uint64_t total = 0;
    for (size_t k = 0; k < casters.size(); ++k)
    {
        const SceneDraw& d = *casters[k].d;
        if (!d.captureModule || !d.vCount || d.vFirst < 0)
            continue;
        if (d.indexed && !d.indexData)
            continue;
        total = (total * 16 + align - 1) / align * align / 16;
        base[k] = static_cast<uint32_t>(total);
        const uint32_t b = base[k], n = d.vertexCount, count = d.vCount;
        auto vertex = [&](uint32_t i) -> int64_t {
            int64_t v;
            if (d.indexed)
                v = d.index32 ? int64_t(reinterpret_cast<const uint32_t*>(d.indexData)[d.start + i])
                              : int64_t(reinterpret_cast<const uint16_t*>(d.indexData)[d.start + i]);
            else
                v = int64_t(d.start) + i;
            if (d.indexed)
                v += d.baseVertex;
            return v - d.vFirst;
        };
        auto tri = [&](int64_t a, int64_t bb, int64_t c) {
            if (a < 0 || bb < 0 || c < 0 || a >= count || bb >= count || c >= count || a == bb || bb == c || a == c)
                return;
            idx.push_back(b + uint32_t(a)), idx.push_back(b + uint32_t(bb)), idx.push_back(b + uint32_t(c));
        };
        if (d.type == D3DPT_TRIANGLELIST)
            for (uint32_t i = 0; i + 2 < n; i += 3)
                tri(vertex(i), vertex(i + 1), vertex(i + 2));
        else if (d.type == D3DPT_TRIANGLESTRIP)
            for (uint32_t i = 0; i + 2 < n; ++i)
                tri(vertex(i), vertex(i + 1), vertex(i + 2));
        else if (d.type == D3DPT_TRIANGLEFAN)
            for (uint32_t i = 1; i + 1 < n; ++i)
                tri(vertex(0), vertex(i), vertex(i + 1));
        total += count;
    }
    return total;
}

// Each caster's vertex shader (capture variant) writes its world positions into `dst` at its base (c22..c25 =
// camera -> world, so the "clip position" is the world position).
void VulkanDevice::RtReplay(const std::vector<RtCaster>& casters, const std::vector<uint32_t>& base, VkBuffer dst)
{
    VkCommandBuffer cb = Slot().cb;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {1, 1}};
    ri.layerCount = 1;
    vkCmdBeginRendering(cb, &ri);
    VkViewport v1{0, 0, 1, 1, 0, 1};
    VkRect2D sc1{{0, 0}, {1, 1}};
    vkCmdSetViewport(cb, 0, 1, &v1);
    vkCmdSetScissor(cb, 0, 1, &sc1);
    vkCmdSetDepthBias(cb, 0.0f, 0.0f, 0.0f);
    float bf[4] = {};
    vkCmdSetBlendConstants(cb, bf);
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    static PrevUniforms prev{};
    VkDeviceSize prevOffset = PushUniform(&prev, sizeof(prev));
    static std::vector<float> constants(kVsConstantCount * 4);
    for (size_t k = 0; k < casters.size(); ++k)
    {
        if (base[k] == ~0u)
            continue;
        const RtCaster& rc = casters[k];
        const SceneDraw& d = *rc.d;
        std::memcpy(constants.data(), rc.c, constants.size() * sizeof(float));
        // c22..c25: camera -> world rows, then (0, 0, 0, 1): the "clip position" is the world position.
        for (int r = 0; r < 3; ++r)
            std::memcpy(&constants[(kCameraRegister + r) * 4], &rc.toWorld.m[r * 4], 16);
        const float wRow[4] = {0, 0, 0, 1};
        std::memcpy(&constants[(kCameraRegister + 3) * 4], wRow, 16);
        VkDeviceSize vsOffset = PushUniform(constants.data(), constants.size() * sizeof(float));
        FFUniforms ff{};
        ff.halfPixel[2] = static_cast<float>(d.vFirst);  // capture base vertex (vk_abi.h)
        VkDeviceSize ffOffset = PushUniform(&ff, sizeof(ff));
        PipelineKey pk{};
        pk.raster.fillMode = D3DFILL_SOLID;
        pk.raster.cullMode = D3DCULL_NONE;
        pk.raster.topology = static_cast<uint8_t>(d.type);
        pk.layoutHash = reinterpret_cast<uintptr_t>(d.input);
        VkShaderModule pair[2] = {d.captureModule, rtNullFs_};
        pk.shaderHash = Hash(pair, sizeof(pair));
        pk.colorFormat = VK_FORMAT_UNDEFINED;
        pk.depthFormat = VK_FORMAT_UNDEFINED;
        pk.motionFormat = VK_FORMAT_UNDEFINED;
        VkPipeline pipeline = GetPipeline(pk, *d.input, d.captureModule, rtNullFs_);
        if (!pipeline)
            continue;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        FrameSlot& s = Slot();
        VkDescriptorBufferInfo bufs[5] = {{s.uniforms, vsOffset, kVsConstantCount * 16},
                                          {s.uniforms, ffOffset, kPsConstantCount * 16},
                                          {s.uniforms, ffOffset, sizeof(FFUniforms)},
                                          {s.uniforms, prevOffset, sizeof(PrevUniforms)},
                                          {dst, VkDeviceSize(base[k]) * 16, VkDeviceSize(d.vCount) * 16}};
        VkWriteDescriptorSet w[5];
        for (uint32_t i = 0; i < 5; ++i)
        {
            w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w[i].dstBinding = i == 4 ? kBindingCapture : i;
            w[i].descriptorCount = 1;
            w[i].descriptorType = i == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w[i].pBufferInfo = &bufs[i];
        }
        vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout(), 0, 5, w);
        bool bound = true;
        for (const VkVertexInputBindingDescription& vb : d.input->bindings)
        {
            if (vb.binding >= 4 || !d.streams[vb.binding].buffer)
            {
                bound = false;
                break;
            }
            VkDeviceSize off = d.streams[vb.binding].offset;
            vkCmdBindVertexBuffers(cb, vb.binding, 1, &d.streams[vb.binding].buffer, &off);
        }
        if (!bound)
            continue;
        if (d.indexed)
        {
            vkCmdBindIndexBuffer(cb, d.indexBuffer, 0, d.index32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(cb, d.vertexCount, 1, d.start, d.baseVertex, 0);
        }
        else
            vkCmdDraw(cb, d.vertexCount, 1, d.start, 0);
    }
    vkCmdEndRendering(cb);
}

// Pipelines of the helper path: the depth conversion (compute) and the apply pass that reads the helper's visibility.
bool VulkanDevice::RtRemoteInit()
{
    if (rtRemoteFailed_)
        return false;
    if (rtDepthPipe_)
        return true;
    VkDevice device = Ctx().device;
    if (!rtVs_)
        rtVs_ = MakeModule(CompileGlsl(RtShadowVertexGlsl(), false));
    if (!rtNullFs_)
        rtNullFs_ = MakeModule(CompileGlsl("#version 450\nvoid main() {}\n", true));
    rtRemoteFs_ = MakeModule(CompileGlsl(RtShadowFragmentGlsl(true), true));
    rtDepthCs_ = MakeModule(CompileGlslStage(RtDepthComputeGlsl(), GlslStage::Compute));
    VkDescriptorSetLayoutBinding b[3] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dl.bindingCount = 3;
    dl.pBindings = b;
    vkCreateDescriptorSetLayout(device, &dl, nullptr, &rtRemoteSetLayout_);
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &rtRemoteSetLayout_;
    vkCreatePipelineLayout(device, &pl, nullptr, &rtRemoteLayout_);
    for (auto& x : b)
        x.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    vkCreateDescriptorSetLayout(device, &dl, nullptr, &rtDepthSetLayout_);
    pl.pSetLayouts = &rtDepthSetLayout_;
    vkCreatePipelineLayout(device, &pl, nullptr, &rtDepthLayout_);
    if (rtDepthCs_)
    {
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT, ci.stage.module = rtDepthCs_, ci.stage.pName = "main";
        ci.layout = rtDepthLayout_;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &rtDepthPipe_);
    }
    if (!rtVs_ || !rtNullFs_ || !rtRemoteFs_ || !rtDepthPipe_)
    {
        Log("vulkan: ray-traced shadows (helper path) failed to build; shadow maps instead");
        rtRemoteFailed_ = true;
        return false;
    }
    return true;
}

// Ray-traced shadows through the 64-bit helper (rt_ipc.h): the inputs go to shared buffers, the frame is submitted
// up to here (signalling the helper), and the apply pass in the second half reads the visibility it writes.
bool VulkanDevice::RtShadowsRemote(const std::vector<RtCaster>& casters, const Mat4& toWorld)
{
    const rhi::SceneHints& h = rhi::Hints();
    RtClient& rc = Rt();
    if (!rc.Start() || !rc.Ready() || !RtRemoteInit())
        return false;
    Image* target = rt_->GetImage();
    Image* depth = depth_->GetImage();
    if (!depth)
        return false;
    // The apply pipeline (dual-source blend, as the in-process path) with the helper-path fragment shader.
    if (!rtRemoteApply_ || rtRemoteFormat_ != target->format)
    {
        VkShaderModule fs = rtFs_;
        VkPipelineLayout layout = rtApplyLayout_;
        VkPipeline keep = rtApply_;
        VkFormat keepFormat = rtApplyFormat_;
        rtFs_ = rtRemoteFs_, rtApplyLayout_ = rtRemoteLayout_, rtApply_ = VK_NULL_HANDLE;
        rtRemoteApply_ = RtApplyPipeline(target->format);
        rtRemoteFormat_ = target->format;
        rtFs_ = fs, rtApplyLayout_ = layout, rtApply_ = keep, rtApplyFormat_ = keepFormat;
        if (!rtRemoteApply_)
            return false;
    }
    std::vector<uint32_t> base;
    uint64_t total = RtGather(casters, base);
    const std::vector<uint32_t>& idx = rtIndices_;
    if (idx.empty() || total == 0)
        return false;
    const D3DVIEWPORT9& svp = scene_.draws.back().viewport;
    const uint32_t W = svp.Width, H = svp.Height;
    if (!rc.Ensure(total, idx.size(), W, H))
        return false;
    std::memcpy(rc.IndicesMapped(), idx.data(), idx.size() * 4);

    // Inputs: world positions, camera-space depth.
    EndRendering();
    RtReplay(casters, base, rc.Buffer(rtipc::kVertices));
    VkCommandBuffer cb = Slot().cb;
    depth->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    if (!shadowDepthSampler_)
    {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(Ctx().device, &si, nullptr, &shadowDepthSampler_);
    }
    float dp[8] = {float(svp.X), float(svp.Y), float(W), float(H), sceneProjection_._33, sceneProjection_._34,
                   sceneProjection_._43, 0.0f};
    VkDeviceSize dOffset = PushUniform(dp, sizeof(dp));
    VkDescriptorImageInfo dimg{shadowDepthSampler_, depth->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo zb{rc.Buffer(rtipc::kDepth), 0, VkDeviceSize(W) * H * 4};
    VkDescriptorBufferInfo db{Slot().uniforms, dOffset, sizeof(dp)};
    VkWriteDescriptorSet w[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                 {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstBinding = 0, w[0].descriptorCount = 1, w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &dimg;
    w[1].dstBinding = 1, w[1].descriptorCount = 1, w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &zb;
    w[2].dstBinding = 2, w[2].descriptorCount = 1, w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &db;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rtDepthPipe_);
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rtDepthLayout_, 0, 3, w);
    vkCmdDispatch(cb, (W + 7) / 8, (H + 7) / 8, 1);
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cb, &di);

    // Hand over: the first half of the frame signals the helper; recording continues in the second command buffer.
    SplitSubmit(rc.ToHelper(), serial);
    rtipc::FrameParams fp{};
    fp.frame = serial;
    fp.vertexCount = static_cast<uint32_t>(total);
    fp.indexCount = static_cast<uint32_t>(idx.size());
    fp.width = W, fp.height = H;
    fp.rays = h.shadowRays;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            fp.camToWorld[r * 4 + c] = toWorld.m[r * 4 + c];
    fp.proj[0] = sceneProjection_._11, fp.proj[1] = sceneProjection_._22;
    fp.proj[2] = sceneProjection_._33, fp.proj[3] = sceneProjection_._34;
    fp.proj43 = sceneProjection_._43;
    fp.sun[0] = -h.sun[0], fp.sun[1] = -h.sun[1], fp.sun[2] = -h.sun[2];
    fp.sun[3] = std::tan(h.shadowSoftness * 3.14159265f / 180.0f);
    rc.Request(fp);
    rtWaitFrame_ = serial;
    helperWaits_.push_back({rc.FromHelper(), serial, false});

    // Apply (second half; the final submit waits for the helper).
    cb = Slot().cb;
    RtShadowParams p{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            p.camToWorld[r][c] = toWorld.m[r * 4 + c];
    p.proj[0] = sceneProjection_._11, p.proj[1] = sceneProjection_._22;
    p.proj[2] = sceneProjection_._33, p.proj[3] = sceneProjection_._34;
    p.misc[0] = sceneProjection_._43, p.misc[1] = h.shadowStrength;
    p.misc[2] = static_cast<float>(h.shadowRays), p.misc[3] = static_cast<float>(serial % 64);
    p.sun[0] = -h.sun[0], p.sun[1] = -h.sun[1], p.sun[2] = -h.sun[2];
    p.sun[3] = std::tan(h.shadowSoftness * 3.14159265f / 180.0f);
    p.viewport[0] = float(svp.X), p.viewport[1] = float(svp.Y), p.viewport[2] = float(W), p.viewport[3] = float(H);
    p.fog[0] = scene_.fogStart, p.fog[1] = scene_.fogEnd, p.fog[2] = scene_.fog ? 1.0f : 0.0f;
    static const bool debug = ConfigInt("render", "shadow_debug", 0) != 0;
    p.fog[3] = debug ? 1.0f : 0.0f;
    std::memcpy(p.fogColor, scene_.fogColor, sizeof(scene_.fogColor));
    if (scene_.fog)
        ModernFogUniforms(p.mf, h, sceneProjection_, svp, scene_.fogStart, scene_.fogEnd, false);
    VkDeviceSize pOffset = PushUniform(&p, sizeof(p));
    target->Transition(cb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = target->AttachmentView(rt_->level, rt_->face);
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ari{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ari.renderArea = {{0, 0}, {target->width, target->height}};
    ari.layerCount = 1;
    ari.colorAttachmentCount = 1;
    ari.pColorAttachments = &ca;
    vkCmdBeginRendering(cb, &ari);
    VkViewport vv{float(svp.X), float(svp.Y), float(W), float(H), 0, 1};
    VkRect2D vsc{{static_cast<int32_t>(svp.X), static_cast<int32_t>(svp.Y)}, {W, H}};
    vkCmdSetViewport(cb, 0, 1, &vv);
    vkCmdSetScissor(cb, 0, 1, &vsc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, rtRemoteApply_);
    VkDescriptorBufferInfo vb{rc.Buffer(rtipc::kVisibility), 0, VkDeviceSize(W) * H * 4};
    VkDescriptorBufferInfo pb{Slot().uniforms, pOffset, sizeof(p)};
    VkWriteDescriptorSet aw[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    aw[0].dstBinding = 0, aw[0].descriptorCount = 1, aw[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aw[0].pImageInfo = &dimg;
    aw[1].dstBinding = 1, aw[1].descriptorCount = 1, aw[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    aw[1].pBufferInfo = &vb;
    aw[2].dstBinding = 2, aw[2].descriptorCount = 1, aw[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    aw[2].pBufferInfo = &pb;
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, rtRemoteLayout_, 0, 3, aw);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRendering(cb);
    static int logged;
    if (logged++ % 600 == 0)
        Log("vulkan: ray-traced shadows (helper): %zu casters, %llu vertices, %zu triangles", casters.size(),
            static_cast<unsigned long long>(total), idx.size() / 3);
    return true;
}

// The casters' world-space triangles (capture replays with c22..c25 = camera -> world), one BLAS and one TLAS, then
// the apply pass. False: nothing done (the caller falls back to the shadow map).
bool VulkanDevice::RtShadows(const std::vector<RtCaster>& casters, const Mat4& toWorld)
{
    const rhi::SceneHints& h = rhi::Hints();
    if (!RtInit())
        return false;
    Context& ctx = Ctx();
    RtFrame& fr = rtFrames_[serial % kFramesInFlight];
    Image* target = rt_->GetImage();
    Image* depth = depth_->GetImage();
    VkPipeline apply = RtApplyPipeline(target->format);
    if (!apply || !depth)
        return false;

    // 1. Vertex ranges and triangle indices (RtGather).
    std::vector<uint32_t> base;
    uint64_t total = RtGather(casters, base);
    std::vector<uint32_t>& idx = rtIndices_;
    if (idx.empty() || total == 0 || total > (64u << 20) / 16 * 16)
        return false;
    const VkBufferUsageFlags asInput = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!RtEnsure(fr.verts, total * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | asInput, false) ||
        !RtEnsure(fr.indices, idx.size() * 4, asInput, true))
        return false;
    std::memcpy(fr.indices.mapped, idx.data(), idx.size() * 4);

    // 2. Capture replays (RtReplay).
    EndRendering();
    VkCommandBuffer cb = Slot().cb;
    RtReplay(casters, base, fr.verts.buffer);
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cb, &di);

    // 3. Bottom level: all triangles in one geometry.
    VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& tr = geo.geometry.triangles;
    tr.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tr.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    tr.vertexData.deviceAddress = fr.verts.address;
    tr.vertexStride = 16;
    tr.maxVertex = static_cast<uint32_t>(total - 1);
    tr.indexType = VK_INDEX_TYPE_UINT32;
    tr.indexData.deviceAddress = fr.indices.address;
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.geometryCount = 1;
    bi.pGeometries = &geo;
    const uint32_t triangles = static_cast<uint32_t>(idx.size() / 3);
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(ctx.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &triangles,
                                            &sz);
    auto ensureAs = [&](RtBuffer& buf, VkAccelerationStructureKHR& as, VkDeviceSize size,
                        VkAccelerationStructureTypeKHR type) {
        if (as && buf.size >= size)
            return true;
        if (as)
        {
            VkDevice device = ctx.device;
            VkAccelerationStructureKHR old = as;
            DeferDestroy([device, old] { vkDestroyAccelerationStructureKHR(device, old, nullptr); });
            as = VK_NULL_HANDLE;
        }
        if (!RtEnsure(buf, size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false))
            return false;
        VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        ci.buffer = buf.buffer;
        ci.size = buf.size;
        ci.type = type;
        return vkCreateAccelerationStructureKHR(ctx.device, &ci, nullptr, &as) == VK_SUCCESS;
    };
    if (!ensureAs(fr.blasBuffer, fr.blas, sz.accelerationStructureSize, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR))
        return false;
    VkDeviceSize scratchNeed = sz.buildScratchSize + rtScratchAlign_;
    if (!RtEnsure(fr.scratch, std::max<VkDeviceSize>(scratchNeed, 1 << 20), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false))
        return false;
    VkDeviceAddress scratch = (fr.scratch.address + rtScratchAlign_ - 1) / rtScratchAlign_ * rtScratchAlign_;
    bi.dstAccelerationStructure = fr.blas;
    bi.scratchData.deviceAddress = scratch;
    VkAccelerationStructureBuildRangeInfoKHR range{triangles, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(cb, 1, &bi, &ranges);
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    mb.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    vkCmdPipelineBarrier2(cb, &di);

    // 4. Top level: one instance, identity transform.
    if (!RtEnsure(fr.instances, sizeof(VkAccelerationStructureInstanceKHR), asInput, true))
        return false;
    VkAccelerationStructureDeviceAddressInfoKHR dai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    dai.accelerationStructure = fr.blas;
    VkAccelerationStructureInstanceKHR inst{};
    inst.transform.matrix[0][0] = inst.transform.matrix[1][1] = inst.transform.matrix[2][2] = 1.0f;
    inst.mask = 0xff;
    inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    inst.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(ctx.device, &dai);
    std::memcpy(fr.instances.mapped, &inst, sizeof(inst));
    VkAccelerationStructureGeometryKHR tgeo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    tgeo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tgeo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tgeo.geometry.instances.data.deviceAddress = fr.instances.address;
    VkAccelerationStructureBuildGeometryInfoKHR tbi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    tbi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tbi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    tbi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tbi.geometryCount = 1;
    tbi.pGeometries = &tgeo;
    const uint32_t one = 1;
    VkAccelerationStructureBuildSizesInfoKHR tsz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(ctx.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tbi, &one, &tsz);
    if (!ensureAs(fr.tlasBuffer, fr.tlas, tsz.accelerationStructureSize, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR))
        return false;
    if (tsz.buildScratchSize + rtScratchAlign_ > fr.scratch.size)
        return false;
    tbi.dstAccelerationStructure = fr.tlas;
    tbi.scratchData.deviceAddress = scratch;
    VkAccelerationStructureBuildRangeInfoKHR trange{1, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* tranges = &trange;
    vkCmdBuildAccelerationStructuresKHR(cb, 1, &tbi, &tranges);
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    mb.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier2(cb, &di);

    // 5. Apply.
    const D3DVIEWPORT9& svp = scene_.draws.back().viewport;
    RtShadowParams p{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            p.camToWorld[r][c] = toWorld.m[r * 4 + c];
    p.proj[0] = sceneProjection_._11, p.proj[1] = sceneProjection_._22;
    p.proj[2] = sceneProjection_._33, p.proj[3] = sceneProjection_._34;
    p.misc[0] = sceneProjection_._43, p.misc[1] = h.shadowStrength;
    p.misc[2] = static_cast<float>(h.shadowRays), p.misc[3] = static_cast<float>(serial % 64);
    p.sun[0] = -h.sun[0], p.sun[1] = -h.sun[1], p.sun[2] = -h.sun[2];  // towards the sun
    p.sun[3] = std::tan(h.shadowSoftness * 3.14159265f / 180.0f);
    p.viewport[0] = float(svp.X), p.viewport[1] = float(svp.Y), p.viewport[2] = float(svp.Width);
    p.viewport[3] = float(svp.Height);
    p.fog[0] = scene_.fogStart, p.fog[1] = scene_.fogEnd, p.fog[2] = scene_.fog ? 1.0f : 0.0f;
    static const bool debug = ConfigInt("render", "shadow_debug", 0) != 0;
    p.fog[3] = debug ? 1.0f : 0.0f;
    std::memcpy(p.fogColor, scene_.fogColor, sizeof(scene_.fogColor));
    if (scene_.fog)
        ModernFogUniforms(p.mf, h, sceneProjection_, svp, scene_.fogStart, scene_.fogEnd, false);
    VkDeviceSize pOffset = PushUniform(&p, sizeof(p));
    depth->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    target->Transition(cb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = target->AttachmentView(rt_->level, rt_->face);
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ari{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ari.renderArea = {{0, 0}, {target->width, target->height}};
    ari.layerCount = 1;
    ari.colorAttachmentCount = 1;
    ari.pColorAttachments = &ca;
    vkCmdBeginRendering(cb, &ari);
    VkViewport vv{float(svp.X), float(svp.Y), float(svp.Width), float(svp.Height), 0, 1};
    VkRect2D vsc{{static_cast<int32_t>(svp.X), static_cast<int32_t>(svp.Y)}, {svp.Width, svp.Height}};
    vkCmdSetViewport(cb, 0, 1, &vv);
    vkCmdSetScissor(cb, 0, 1, &vsc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, apply);
    if (!shadowDepthSampler_)
    {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(ctx.device, &si, nullptr, &shadowDepthSampler_);
    }
    VkDescriptorImageInfo dimg{shadowDepthSampler_, depth->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSetAccelerationStructureKHR wa{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    wa.accelerationStructureCount = 1;
    wa.pAccelerationStructures = &fr.tlas;
    VkDescriptorBufferInfo pb{Slot().uniforms, pOffset, sizeof(p)};
    VkWriteDescriptorSet aw[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    aw[0].dstBinding = 0, aw[0].descriptorCount = 1, aw[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aw[0].pImageInfo = &dimg;
    aw[1].pNext = &wa;
    aw[1].dstBinding = 1, aw[1].descriptorCount = 1, aw[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    aw[2].dstBinding = 2, aw[2].descriptorCount = 1, aw[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    aw[2].pBufferInfo = &pb;
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, rtApplyLayout_, 0, 3, aw);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRendering(cb);
    static int logged;
    if (logged++ % 600 == 0)
        Log("vulkan: ray-traced shadows: %zu casters, %llu vertices, %u triangles", casters.size(),
            static_cast<unsigned long long>(total), triangles);
    return true;
}

// --- Ambient occlusion (vk_gtao.h): XeGTAO ------------------------------------------------------------------

bool VulkanDevice::GtaoImageCreate(GtaoImage& g, VkFormat format, uint32_t levels)
{
    Context& c = Ctx();
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {gtaoWidth_, gtaoHeight_, 1};
    ii.mipLevels = levels;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(c.device, &ii, nullptr, &g.image) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(c.device, g.image, &req);
    g.memory = c.Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(c.device, g.image, g.memory, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = g.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    vkCreateImageView(c.device, &vi, nullptr, &g.view);
    for (uint32_t l = 0; l < levels && l < 5; ++l)
    {
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, l, 1, 0, 1};
        vkCreateImageView(c.device, &vi, nullptr, &g.mips[l]);
    }
    for (uint32_t l = levels; l < 5; ++l)
        g.mips[l] = g.mips[0];
    // GENERAL for good: storage writes and sampled reads, separated by barriers.
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = g.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(Slot().cb, &di);
    return true;
}

void VulkanDevice::GtaoImageDestroy(GtaoImage& g)
{
    if (!g.image)
        return;
    VkDevice device = Ctx().device;
    GtaoImage old = g;
    DeferDestroy([device, old] {
        VkImageView last = VK_NULL_HANDLE;
        for (VkImageView v : old.mips)
            if (v && v != last)
                vkDestroyImageView(device, v, nullptr), last = v;
        vkDestroyImageView(device, old.view, nullptr);
        vkDestroyImage(device, old.image, nullptr);
        vkFreeMemory(device, old.memory, nullptr);
    });
    g = GtaoImage{};
}

// The three XeGTAO compute pipelines and the apply pass's layout (once).
bool VulkanDevice::GtaoInit()
{
    if (gtaoFailed_)
        return false;
    if (gtaoPipe_[3])
        return true;
    VkDevice device = Ctx().device;
    alOn_ = ConfigIntDefault("render", "afterlight", 0, 1) != 0;  // read once (restart)
    // [render] ao_thickness (world units, vk_gtao.cpp): occluders are this thick, not infinitely deep.
    const std::string hlsl = GtaoHlsl(ConfigFloatDefault("render", "ao_thickness", 0.0f, 3.0f),
                                      ConfigIntDefault("render", "ao_slices", 3, 9));
    const char* entries[4] = {"CSPrefilterDepths16x16", "CSGTAOHigh", "CSDenoisePass", "CSDenoiseLastPass"};
    // Bindings 0..13 XeGTAO (gtao.hlsl), 14..23 Afterlight (afterlight.hlsl).
    VkDescriptorSetLayoutBinding b[24];
    const VkDescriptorType types[24] = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLER,       VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE};
    for (uint32_t i = 0; i < 24; ++i)
        b[i] = {i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dl.bindingCount = 24;
    dl.pBindings = b;
    vkCreateDescriptorSetLayout(device, &dl, nullptr, &gtaoSetLayout_);
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &gtaoSetLayout_;
    vkCreatePipelineLayout(device, &pl, nullptr, &gtaoLayout_);
    for (int i = 0; i < 4; ++i)
    {
        VkShaderModule m = MakeModule(CompileHlslCompute(hlsl, entries[i]));
        if (!m)
            break;
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT, ci.stage.module = m, ci.stage.pName = entries[i];
        ci.layout = gtaoLayout_;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &gtaoPipe_[i]);
        vkDestroyShaderModule(device, m, nullptr);
    }
    if (alOn_)
    {
        const char* alEntries[6] = {"CSAfterlightNormals", "CSAfterlightRadiance", "CSAfterlightRadianceMip",
                                    "CSAfterlightMain",    "CSAfterlightBlurH",    "CSAfterlightBlurV"};
        for (int i = 0; i < 6; ++i)
        {
            VkShaderModule m = MakeModule(CompileHlslCompute(hlsl, alEntries[i]));
            if (!m)
                break;
            VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT, ci.stage.module = m, ci.stage.pName = alEntries[i];
            ci.layout = gtaoLayout_;
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &alPipe_[i]);
            vkDestroyShaderModule(device, m, nullptr);
        }
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 16.0f;
        vkCreateSampler(device, &si, nullptr, &alLinear_);
        for (VkPipeline pp : alPipe_)
            if (!pp)
            {
                Log("vulkan: afterlight (unified AO + GI) failed to build; XeGTAO instead");
                alOn_ = false;
                break;
            }
    }
    gtaoApplyVs_ = MakeModule(CompileGlsl(GtaoApplyVertexGlsl(), false));
    gtaoApplyFs_ = MakeModule(CompileGlsl(GtaoApplyFragmentGlsl(alOn_), true));
    VkDescriptorSetLayoutBinding ab[7] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},   // material
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},   // bounce
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};  // radiance
    dl.bindingCount = 7;
    dl.pBindings = ab;
    vkCreateDescriptorSetLayout(device, &dl, nullptr, &gtaoApplySetLayout_);
    pl.pSetLayouts = &gtaoApplySetLayout_;
    vkCreatePipelineLayout(device, &pl, nullptr, &gtaoApplyLayout_);
    if (!gtaoPipe_[0] || !gtaoPipe_[1] || !gtaoPipe_[2] || !gtaoPipe_[3] || !gtaoApplyVs_ || !gtaoApplyFs_)
    {
        Log("vulkan: ambient occlusion (XeGTAO) failed to build; off");
        gtaoFailed_ = true;
        return false;
    }
    Log("vulkan: ambient occlusion: %s ready", alOn_ ? "Afterlight (unified AO + GI)" : "XeGTAO");
    return true;
}

VkPipeline VulkanDevice::GtaoApplyPipeline(VkFormat target)
{
    if (gtaoApply_ && gtaoApplyFormat_ == target)
        return gtaoApply_;
    VkDevice device = Ctx().device;
    if (gtaoApply_)
    {
        VkPipeline old = gtaoApply_;
        DeferDestroy([device, old] { vkDestroyPipeline(device, old, nullptr); });
    }
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT, st[0].module = gtaoApplyVs_, st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT, st[1].module = gtaoApplyFs_, st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = VK_TRUE;  // dst * ao + fog part (vk_gtao.cpp), alpha kept
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstColorBlendFactor = alOn_ ? VK_BLEND_FACTOR_SRC1_COLOR : VK_BLEND_FACTOR_SRC_ALPHA;  // Afterlight tints
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &target;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext = &ri;
    ci.stageCount = 2;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = gtaoApplyLayout_;
    gtaoApply_ = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &gtaoApply_) != VK_SUCCESS)
        gtaoApply_ = VK_NULL_HANDLE;
    gtaoApplyFormat_ = target;
    return gtaoApply_;
}

// rhi::SceneEnd with [render] ao: XeGTAO on the scene depth (prefilter, main pass, denoise), then the apply pass,
// before the shadows and the snow (both drawn over it).
void VulkanDevice::Ambient()
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    const rhi::SceneHints& h = rhi::Hints();
    if (!h.ao || scene_.draws.empty() || !depth_ || !rt_)
        return;
    if (!GtaoInit())
        return;
    Image* target = rt_->GetImage();
    Image* depth = depth_->GetImage();
    if (!depth)
        return;
    VkPipeline apply = GtaoApplyPipeline(target->format);
    if (!apply)
        return;
    // XeGTAO works on a viewport that starts at the depth image's origin and covers it (its depth reads use 0..1
    // coordinates over the whole image).
    const D3DVIEWPORT9& svp = scene_.draws.back().viewport;
    if (svp.X != 0 || svp.Y != 0 || svp.Width != depth->width || svp.Height != depth->height)
    {
        if (!gtaoLogged_)
            Log("vulkan: ambient occlusion skipped: scene viewport %ux%u at %u,%u in a %ux%u depth buffer", svp.Width,
                svp.Height, svp.X, svp.Y, depth->width, depth->height);
        gtaoLogged_ = true;
        return;
    }
    if (gtaoWidth_ != svp.Width || gtaoHeight_ != svp.Height)
    {
        for (GtaoImage* gi : {&gtaoDepth_, &gtaoTerm_, &gtaoEdges_, &gtaoFinal_, &alNormals_, &alRadiance_, &alBounce_,
                              &alPong_})
            GtaoImageDestroy(*gi);
        gtaoWidth_ = svp.Width, gtaoHeight_ = svp.Height;
        EndRendering();
        if (!GtaoImageCreate(gtaoDepth_, VK_FORMAT_R32_SFLOAT, XE_GTAO_DEPTH_MIP_LEVELS) ||
            !GtaoImageCreate(gtaoTerm_, VK_FORMAT_R8_UINT, 1) || !GtaoImageCreate(gtaoEdges_, VK_FORMAT_R8_UNORM, 1) ||
            !GtaoImageCreate(gtaoFinal_, VK_FORMAT_R8_UINT, 1) ||
            (alOn_ && (!GtaoImageCreate(alNormals_, VK_FORMAT_R16G16B16A16_SFLOAT, 1) ||
                       !GtaoImageCreate(alRadiance_, VK_FORMAT_R16G16B16A16_SFLOAT, 5) ||
                       !GtaoImageCreate(alBounce_, VK_FORMAT_R16G16B16A16_SFLOAT, 1) ||
                       !GtaoImageCreate(alPong_, VK_FORMAT_R16G16B16A16_SFLOAT, 1))))
        {
            Log("vulkan: ambient occlusion: working images failed; off");
            gtaoFailed_ = true;
            return;
        }
    }
    // Constants: XeGTAO's own helper, from the scene projection (D3D row-major, 0..1 depth: XeGTAO's convention).
    XeGTAO::GTAOSettings settings;
    settings.QualityLevel = 2;  // CSGTAOHigh
    settings.DenoisePasses = 2;  // "medium": one intermediate pass, then the last
    settings.Radius = h.aoRadius;
    settings.FinalValuePower = h.aoPower;            // XE_GTAO_USE_DEFAULT_CONSTANTS 0 (vk_gtao.cpp): used
    settings.FalloffRange = h.aoFalloff;
    // [render] ao_thin: occluders are not infinitely thick (XeGTAO's thickness heuristic, 0..0.7): without it a
    // character occludes the ground far behind it like a board standing there (a dark halo around the player).
    static const float aoThin = ConfigFloatDefault("render", "ao_thin", 0.0f, 0.7f);
    settings.ThinOccluderCompensation = aoThin;
    XeGTAO::GTAOConstants consts{};
    XeGTAO::GTAOUpdateConstants(consts, int(svp.Width), int(svp.Height), settings, &sceneProjection_._11, true,
                                gtaoFrame_++);
    consts.NoiseIndex = 0;  // no temporal accumulation

    EndRendering();
    VkCommandBuffer cb = Slot().cb;
    depth->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    if (!shadowDepthSampler_)
    {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(Ctx().device, &si, nullptr, &shadowDepthSampler_);
    }
    VkDeviceSize offset = PushUniform(&consts, sizeof(consts));
    VkDescriptorBufferInfo ub{Slot().uniforms, offset, sizeof(consts)};
    VkDescriptorImageInfo smp{shadowDepthSampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkDescriptorImageInfo img[20] = {};
    img[0] = {VK_NULL_HANDLE, depth->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};  // 2
    for (int l = 0; l < 5; ++l)
        img[1 + l] = {VK_NULL_HANDLE, gtaoDepth_.mips[l], VK_IMAGE_LAYOUT_GENERAL};                  // 3..7
    img[6] = {VK_NULL_HANDLE, gtaoDepth_.view, VK_IMAGE_LAYOUT_GENERAL};                             // 8
    img[7] = {VK_NULL_HANDLE, gtaoTerm_.view, VK_IMAGE_LAYOUT_GENERAL};                              // 9
    img[8] = {VK_NULL_HANDLE, gtaoEdges_.view, VK_IMAGE_LAYOUT_GENERAL};                             // 10
    img[9] = {VK_NULL_HANDLE, gtaoTerm_.view, VK_IMAGE_LAYOUT_GENERAL};                              // 11
    img[10] = {VK_NULL_HANDLE, gtaoEdges_.view, VK_IMAGE_LAYOUT_GENERAL};                            // 12
    img[11] = {VK_NULL_HANDLE, gtaoFinal_.view, VK_IMAGE_LAYOUT_GENERAL};                            // 13
    // Afterlight (bindings 14..23, afterlight.hlsl): constants, the scene colour, radiance MIP in / out (set per
    // pass), radiance (all MIPs), normals out / in, bounce out / in (set per pass).
    AfterlightConstants al{};
    VkDescriptorBufferInfo alub{};
    VkDescriptorImageInfo alSmp{alLinear_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    if (alOn_)
    {
        static const float alGain = ConfigFloat("render", "afterlight_gain", 1.0f);
        static const float alReach = ConfigFloat("render", "afterlight_reach", 2.75f);  // GI ring / AO ring
        al.giGain = alGain;
        al.giEmitterBoost = 4.0f;
        al.giRadiusScale = alReach;
        const float giRadius = std::max(consts.EffectRadius * consts.RadiusMultiplier * alReach, 1e-4f);
        const float giFalloffRange = std::max(consts.EffectFalloffRange * giRadius, 1e-4f);
        al.giFalloffMul = -1.0f / giFalloffRange;
        al.giFalloffAdd = giRadius * (1.0f - consts.EffectFalloffRange) / giFalloffRange + 1.0f;
        al.giRadianceFalloffPower = 1.0f;
        al.giSourceGate = 1.0f;
        al.giSourceGateKnee = 0.2f;
        al.giMaxSampleRadiance = 2.0f;
        // [render] afterlight_emitter_threshold: the light-source detector (0.9 in the FX); 1 = off, the remaster
        // default: on the snow levels sunlit snow is near white and was read as lamps (boosted, then protected from
        // the composite). The sky stays a light source either way.
        static const float alEmitter = ConfigFloat("render", "afterlight_emitter_threshold", 1.0f);
        al.emitterThreshold = alEmitter;
        al.emitterContrast = 1.5f;
        al.sliceCount = float(ConfigIntDefault("render", "ao_slices", 3, 9));
        al.thickness = ConfigFloatDefault("render", "ao_thickness", 0.0f, 3.0f);
        const VkFormat tf = target->format;
        al.sceneIsLinear = (tf == VK_FORMAT_B8G8R8A8_SRGB || tf == VK_FORMAT_R8G8B8A8_SRGB ||
                            tf == VK_FORMAT_R16G16B16A16_SFLOAT)
                               ? 1.0f
                               : 0.0f;
        alub = {Slot().uniforms, PushUniform(&al, sizeof(al)), sizeof(al)};
        img[12] = {VK_NULL_HANDLE, target->sampledView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};  // 15
        img[13] = {VK_NULL_HANDLE, alRadiance_.mips[0], VK_IMAGE_LAYOUT_GENERAL};                   // 17
        img[14] = {VK_NULL_HANDLE, alRadiance_.mips[0], VK_IMAGE_LAYOUT_GENERAL};                   // 18
        img[15] = {VK_NULL_HANDLE, alRadiance_.view, VK_IMAGE_LAYOUT_GENERAL};                      // 19
        img[16] = {VK_NULL_HANDLE, alNormals_.mips[0], VK_IMAGE_LAYOUT_GENERAL};                    // 20
        img[17] = {VK_NULL_HANDLE, alNormals_.view, VK_IMAGE_LAYOUT_GENERAL};                       // 21
        img[18] = {VK_NULL_HANDLE, alBounce_.mips[0], VK_IMAGE_LAYOUT_GENERAL};                     // 22
        img[19] = {VK_NULL_HANDLE, alPong_.view, VK_IMAGE_LAYOUT_GENERAL};                          // 23
    }
    const uint32_t writes = alOn_ ? 24 : 14;
    VkWriteDescriptorSet w[24];
    for (uint32_t i = 0; i < 24; ++i)
    {
        w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, w[0].pBufferInfo = &ub;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, w[1].pImageInfo = &smp;
    const VkDescriptorType t2[12] = {
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    for (int i = 0; i < 12; ++i)
        w[2 + i].descriptorType = t2[i], w[2 + i].pImageInfo = &img[i];
    w[14].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, w[14].pBufferInfo = &alub;
    w[15].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, w[15].pImageInfo = &img[12];
    w[16].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, w[16].pImageInfo = &alSmp;
    const VkDescriptorType t3[7] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE};
    for (int i = 0; i < 7; ++i)
        w[17 + i].descriptorType = t3[i], w[17 + i].pImageInfo = &img[13 + i];

    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    auto passP = [&](VkPipeline pipe, uint32_t gx, uint32_t gy) {
        vkCmdPipelineBarrier2(cb, &di);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_COMPUTE, gtaoLayout_, 0, writes, w);
        vkCmdDispatch(cb, gx, gy, 1);
        mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    };
    auto pass = [&](int i, uint32_t gx, uint32_t gy) { passP(gtaoPipe_[i], gx, gy); };
    const uint32_t W = svp.Width, H = svp.Height;
    const uint32_t mainX = (W + XE_GTAO_NUMTHREADS_X - 1) / XE_GTAO_NUMTHREADS_X;
    const uint32_t mainY = (H + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y;
    pass(0, (W + 15) / 16, (H + 15) / 16);  // prefilter depths
    if (alOn_)
    {
        // Afterlight: normals, source radiance from this frame's colour (MIP 0 then a 2x2 chain), then the unified
        // occlusion + bounce pass in place of XeGTAO's main pass.
        target->Transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        passP(alPipe_[0], (W + 7) / 8, (H + 7) / 8);
        passP(alPipe_[1], (W + 7) / 8, (H + 7) / 8);
        for (uint32_t l = 1; l < 5; ++l)
        {
            img[13].imageView = alRadiance_.mips[l - 1], img[14].imageView = alRadiance_.mips[l];
            passP(alPipe_[2], ((W >> l) + 8) / 8, ((H >> l) + 8) / 8);
        }
        passP(alPipe_[3], mainX, mainY);
    }
    else
        pass(1, mainX, mainY);  // main pass
    // Denoise (2 px per thread): term -> final, then final -> term (bindings 11 and 13 swapped); apply reads term.
    const uint32_t dx = (W + XE_GTAO_NUMTHREADS_X * 2 - 1) / (XE_GTAO_NUMTHREADS_X * 2);
    const uint32_t dy = (H + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y;
    // [render] ao_denoise: denoise passes, even (the result must end in term); the sample's "medium" is 2.
    static const int denoise = std::max(2, ConfigIntDefault("render", "ao_denoise", 2, 4) & ~1);
    for (int i = 0; i < denoise - 1; ++i)
    {
        pass(2, dx, dy);
        std::swap(img[9], img[11]);
    }
    pass(3, dx, dy);
    if (alOn_)
    {
        // Bounce blur, depth- and normal-guided: bounce -> pong (horizontal), pong -> bounce (vertical).
        img[19].imageView = alBounce_.view, img[18].imageView = alPong_.mips[0];
        passP(alPipe_[4], (W + 7) / 8, (H + 7) / 8);
        img[19].imageView = alPong_.view, img[18].imageView = alBounce_.mips[0];
        passP(alPipe_[5], (W + 7) / 8, (H + 7) / 8);
    }
    mb.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    vkCmdPipelineBarrier2(cb, &di);

    // Apply.
    GtaoApplyParams ap{};
    ap.viewport[0] = float(svp.X), ap.viewport[1] = float(svp.Y), ap.viewport[2] = float(W), ap.viewport[3] = float(H);
    ap.fog[0] = scene_.fogStart, ap.fog[1] = scene_.fogEnd, ap.fog[2] = scene_.fog ? 1.0f : 0.0f;
    ap.fog[3] = h.aoStrength;
    ap.material[0] = h.aoSnow;  // occlusion kept on snow-white surfaces (albedo, vk_gtao.cpp)
    static const int aoDebug = ConfigInt("render", "ao_debug", 0);  // diagnosis (vk_gtao.cpp)
    ap.material[1] = float(aoDebug);
    ap.material[2] = h.aoContact;  // contact occlusion kept on sunlit snow (vk_gtao.cpp)
    if (alOn_)
    {
        static const float alGain = ConfigFloat("render", "afterlight_gain", 1.0f);
        static const float alStops = ConfigFloat("render", "afterlight_stops", 0.245f);  // max relight per gain
        static const float alFill = ConfigFloat("render", "afterlight_fill", 0.0f);     // ambient fill (off)
        ap.gi[0] = alGain, ap.gi[1] = alStops, ap.gi[2] = alFill;
    }
    std::memcpy(ap.fogColor, scene_.fogColor, sizeof(scene_.fogColor));
    if (scene_.fog)
        ModernFogUniforms(ap.mf, h, sceneProjection_, svp, scene_.fogStart, scene_.fogEnd, false);
    // This frame's ray-traced sun visibility (the helper path ran in Shadows, before this pass): occlusion of the
    // ambient share only (vk_gtao.cpp).
    RtClient& rcl = Rt();
    const bool haveVis = rtWaitFrame_ == serial && h.haveView && h.haveSun && rcl.Buffer(rtipc::kVisibility);
    if (haveVis)
    {
        for (int i = 0; i < 3; ++i)
            ap.sun[i] = -(h.view[i * 4 + 0] * h.sun[0] + h.view[i * 4 + 1] * h.sun[1] + h.view[i * 4 + 2] * h.sun[2]);
        ap.sun[3] = h.shadowStrength;
    }
    ap.proj[0] = sceneProjection_._11, ap.proj[1] = sceneProjection_._22;
    ap.proj[2] = sceneProjection_._33, ap.proj[3] = sceneProjection_._34;
    ap.proj43[0] = sceneProjection_._43;
    VkDeviceSize apOffset = PushUniform(&ap, sizeof(ap));
    target->Transition(cb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = target->AttachmentView(rt_->level, rt_->face);
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {target->width, target->height}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRendering(cb, &ri);
    VkViewport v{float(svp.X), float(svp.Y), float(W), float(H), 0, 1};
    VkRect2D sc{{static_cast<int32_t>(svp.X), static_cast<int32_t>(svp.Y)}, {W, H}};
    vkCmdSetViewport(cb, 0, 1, &v);
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, apply);
    VkDescriptorImageInfo aoImg{shadowDepthSampler_, gtaoTerm_.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo dImg{shadowDepthSampler_, depth->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo apb{Slot().uniforms, apOffset, sizeof(ap)};
    VkDescriptorBufferInfo visb = haveVis ? VkDescriptorBufferInfo{rcl.Buffer(rtipc::kVisibility), 0, VK_WHOLE_SIZE}
                                          : VkDescriptorBufferInfo{Slot().uniforms, 0, 16};
    // The motion target's z / w: AO weight and albedo of the opaque surface (vk_abi.h).
    motion_->Transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkDescriptorImageInfo matImg{shadowDepthSampler_, motion_->sampledView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet aw[7] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    VkDescriptorImageInfo bounceImg{shadowDepthSampler_, alBounce_.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo radImg{alLinear_, alRadiance_.view, VK_IMAGE_LAYOUT_GENERAL};
    aw[5].dstBinding = 5, aw[5].descriptorCount = 1, aw[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aw[5].pImageInfo = &bounceImg;
    aw[6].dstBinding = 6, aw[6].descriptorCount = 1, aw[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aw[6].pImageInfo = &radImg;
    aw[4].dstBinding = 4, aw[4].descriptorCount = 1, aw[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aw[4].pImageInfo = &matImg;
    aw[3].dstBinding = 3, aw[3].descriptorCount = 1, aw[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    aw[3].pBufferInfo = &visb;
    aw[0].dstBinding = 0, aw[0].descriptorCount = 1, aw[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aw[0].pImageInfo = &aoImg;
    aw[1].dstBinding = 1, aw[1].descriptorCount = 1, aw[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aw[1].pImageInfo = &dImg;
    aw[2].dstBinding = 2, aw[2].descriptorCount = 1, aw[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    aw[2].pBufferInfo = &apb;
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gtaoApplyLayout_, 0, alOn_ ? 7 : 5, aw);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRendering(cb);
}

// rhi::SceneEnd with [render] shadow_maps (rhi::Hints): the sun's shadow map from this frame's opaque shader draws,
// then the apply pass into the current target (the back buffer), before the UI.
void VulkanDevice::DropCaster(CachedCaster& c)
{
    for (VulkanBuffer*& b : c.draw.streamObjects)
        if (b)
            b->Release(), b = nullptr;
    if (c.draw.indexObject)
        c.draw.indexObject->Release(), c.draw.indexObject = nullptr;
}

void VulkanDevice::Shadows()
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    const rhi::SceneHints& h = rhi::Hints();
    if (!h.shadows || !h.haveView || !h.haveSun || scene_.draws.empty() || !depth_ || !rt_)
        return;
    if (!ShadowInit(h.shadowSize))
        return;
    Mat4 view, toWorld;
    std::memcpy(view.m, h.view, sizeof(view.m));
    if (!Invert(view, toWorld))
        return;
    const float range = h.shadowRange;
    // The box: around a point ahead of the camera (camera looks along -z).
    float centre[3];
    for (int i = 0; i < 3; ++i)
        centre[i] = toWorld.m[i * 4 + 3] - toWorld.m[i * 4 + 2] * range * 0.7f;
    Mat4 camToLight = Multiply(LightMatrix(centre, h.sun, range, h.shadowSize), toWorld);

    // Casters: this frame's opaque draws, plus static geometry the game culled from the view since (a hill behind
    // the camera still shadows the road ahead; measured: a large shadow appearing and vanishing as the player
    // stepped back and forth). The game sends world * view (c42..c44), so a cached draw is replayed with the
    // camera of the frame it was seen in. A draw whose world transform changes between sightings (characters,
    // moving objects) is never replayed from the cache. Cached draws keep references on their buffers.
    ++shadowFrame_;
    if (h.levelLoading)
    {
        for (auto& [id, c] : casters_)
            DropCaster(c);
        casters_.clear();
    }
    auto worldKey = [](const Mat4& toWorld, const float* c, float* out) {
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 4; ++k)
            {
                float v = k == 3 ? toWorld.m[j * 4 + 3] : 0.0f;
                for (int i = 0; i < 3; ++i)
                    v += toWorld.m[j * 4 + i] * c[(42 + i) * 4 + k];
                out[j * 4 + k] = v;
            }
    };
    auto caster = [](const SceneDraw& d) {
        static const bool skipAlphaTest = ConfigInt("render", "shadow_skip_alphatest", 0) != 0;  // diagnosis
        return d.zWrite && !d.alphaBlend && !(skipAlphaTest && d.alphaTest) && d.vertexShader && d.vertexModule &&
               d.input && d.vertexCount;
    };
    struct Job
    {
        const SceneDraw* d;
        const float* c;
        Mat4 camToLight;
        Mat4 toWorld;  // camera -> world of the frame its constants come from
    };
    std::vector<Job> jobs;
    jobs.reserve(scene_.draws.size() + casters_.size());
    for (const SceneDraw& d : scene_.draws)
    {
        if (!caster(d))
            continue;
        const float* c = &scene_.vsConstants[d.vsConstantsOffset];
        jobs.push_back({&d, c, camToLight, toWorld});
        float key[12];
        worldKey(toWorld, c, key);
        auto [it, fresh] = casters_.try_emplace(d.id);
        CachedCaster& e = it->second;
        if (fresh)
        {
            for (VulkanBuffer* b : d.streamObjects)
                if (b)
                    b->AddRef();
            if (d.indexObject)
                d.indexObject->AddRef();
        }
        else
        {
            for (int i = 0; i < 12 && !e.moving; ++i)
                e.moving = std::fabs(key[i] - e.worldKey[i]) > 1e-3f * (1.0f + std::fabs(key[i]));
            // The same draw id with other buffers (rare): take the new references.
            for (int s = 0; s < 4; ++s)
                if (e.draw.streamObjects[s] != d.streamObjects[s])
                {
                    if (d.streamObjects[s])
                        d.streamObjects[s]->AddRef();
                    if (e.draw.streamObjects[s])
                        e.draw.streamObjects[s]->Release();
                }
            if (e.draw.indexObject != d.indexObject)
            {
                if (d.indexObject)
                    d.indexObject->AddRef();
                if (e.draw.indexObject)
                    e.draw.indexObject->Release();
            }
        }
        e.draw = d;
        e.draw.capture = nullptr;
        e.constants.assign(c, c + kVsConstantCount * 4);
        e.toWorld = toWorld;
        std::memcpy(e.worldKey, key, sizeof(key));
        e.lastSeen = shadowFrame_;
    }
    const float cam[3] = {toWorld.m[3], toWorld.m[7], toWorld.m[11]};
    const float r2 = h.shadowCasterRadius * h.shadowCasterRadius;
    for (auto it = casters_.begin(); it != casters_.end();)
    {
        CachedCaster& e = it->second;
        float dx = e.worldKey[3] - cam[0], dy = e.worldKey[7] - cam[1], dz = e.worldKey[11] - cam[2];
        bool away = dx * dx + dy * dy + dz * dz > r2;
        // Still the buffer contents it was seen with (a re-filled buffer has a new version).
        bool current = true;
        for (int s = 0; s < 4; ++s)
            if (e.draw.streamObjects[s] && e.draw.streamObjects[s]->Current().buffer != e.draw.streams[s].buffer)
                current = false;
        if (e.draw.indexObject && e.draw.indexObject->Current().buffer != e.draw.indexBuffer)
            current = false;
        if (away || !current || casters_.size() > 16384)
        {
            DropCaster(e);
            it = casters_.erase(it);
            continue;
        }
        static const bool useCache = ConfigInt("render", "shadow_cache", 1) != 0;  // diagnosis
        if (useCache && h.shadowCasterCache && e.lastSeen != shadowFrame_ && !e.moving)
            jobs.push_back({&e.draw, e.constants.data(),
                            Multiply(LightMatrix(centre, h.sun, range, h.shadowSize), e.toWorld), e.toWorld});
        ++it;
    }

    // Ray-traced shadows (vk_rt.h) instead of the shadow map, when on and available.
    if (h.shadowsRt && (Ctx().rayQuery || Ctx().externalSharing))
    {
        std::vector<RtCaster> rc;
        rc.reserve(jobs.size());
        for (const Job& j : jobs)
            rc.push_back({j.d, j.c, j.toWorld});
        const bool traced = Ctx().rayQuery ? RtShadows(rc, toWorld) : RtShadowsRemote(rc, toWorld);
        // Per 300 frames: how many fell back to the shadow map (each one is a visible switch of shadow look).
        static uint32_t rtFrames, rtFallbacks;
        ++rtFrames, rtFallbacks += traced ? 0 : 1;
        if (rtFrames == 300)
        {
            if (rtFallbacks)
                Log("vulkan: ray-traced shadows: %u of 300 frames fell back to the shadow map", rtFallbacks);
            rtFrames = rtFallbacks = 0;
        }
        if (traced)
            return;
    }

    EndRendering();
    VkCommandBuffer cb = Slot().cb;
    // 1. Depth from the sun.
    shadowMap_->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    da.imageView = shadowMap_->AttachmentView(0, 0);
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    da.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {h.shadowSize, h.shadowSize}};
    ri.layerCount = 1;
    ri.pDepthAttachment = &da;
    vkCmdBeginRendering(cb, &ri);
    VkViewport v{0, 0, float(h.shadowSize), float(h.shadowSize), 0, 1};
    VkRect2D sc{{0, 0}, {h.shadowSize, h.shadowSize}};
    vkCmdSetViewport(cb, 0, 1, &v);
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdSetDepthBias(cb, 1.5f, 0.0f, 2.0f);
    float bf[4] = {};
    vkCmdSetBlendConstants(cb, bf);
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    FFUniforms ff{};
    static PrevUniforms prev{};
    VkDeviceSize ffOffset = PushUniform(&ff, sizeof(ff)), prevOffset = PushUniform(&prev, sizeof(prev));
    static std::vector<float> constants(kVsConstantCount * 4);
    uint32_t drawn = 0;

    for (const Job& job : jobs)
    {
        const SceneDraw& d = *job.d;
        std::memcpy(constants.data(), job.c, constants.size() * sizeof(float));
        std::memcpy(&constants[kCameraRegister * 4], job.camToLight.m, sizeof(job.camToLight.m));  // rows -> c22..c25
        VkDeviceSize vsOffset = PushUniform(constants.data(), constants.size() * sizeof(float));
        PipelineKey pk{};
        pk.raster.fillMode = D3DFILL_SOLID;
        pk.raster.cullMode = D3DCULL_NONE;
        pk.raster.zEnable = 1;
        pk.raster.zWriteEnable = 1;
        pk.raster.zFunc = D3DCMP_LESSEQUAL;
        pk.raster.depthBiasEnable = 1;
        pk.raster.topology = static_cast<uint8_t>(d.type);
        pk.layoutHash = reinterpret_cast<uintptr_t>(d.input);
        VkShaderModule pair[2] = {d.vertexModule, shadowDepthFs_};
        pk.shaderHash = Hash(pair, sizeof(pair));
        pk.colorFormat = VK_FORMAT_UNDEFINED;
        pk.depthFormat = shadowMap_->format;
        pk.motionFormat = VK_FORMAT_UNDEFINED;
        VkPipeline pipeline = GetPipeline(pk, *d.input, d.vertexModule, shadowDepthFs_);
        if (!pipeline)
            continue;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        FrameSlot& s = Slot();
        VkDescriptorBufferInfo bufs[4] = {{s.uniforms, vsOffset, kVsConstantCount * 16},
                                          {s.uniforms, ffOffset, kPsConstantCount * 16},
                                          {s.uniforms, ffOffset, sizeof(FFUniforms)},
                                          {s.uniforms, prevOffset, sizeof(PrevUniforms)}};
        VkWriteDescriptorSet w[4];
        for (uint32_t b = 0; b < 4; ++b)
        {
            w[b] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w[b].dstBinding = b;
            w[b].descriptorCount = 1;
            w[b].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w[b].pBufferInfo = &bufs[b];
        }
        vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout(), 0, 4, w);
        bool bound = true;
        for (const VkVertexInputBindingDescription& b : d.input->bindings)
        {
            if (b.binding >= 4 || !d.streams[b.binding].buffer)
            {
                bound = false;
                break;
            }
            VkDeviceSize off = d.streams[b.binding].offset;
            vkCmdBindVertexBuffers(cb, b.binding, 1, &d.streams[b.binding].buffer, &off);
        }
        if (!bound)
            continue;
        if (d.indexed)
        {
            if (!d.indexBuffer)
                continue;
            vkCmdBindIndexBuffer(cb, d.indexBuffer, 0, d.index32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(cb, d.vertexCount, 1, d.start, d.baseVertex, 0);
        }
        else
            vkCmdDraw(cb, d.vertexCount, 1, d.start, 0);
        ++drawn;
    }
    vkCmdEndRendering(cb);

    // 2. Apply to the target.
    Image* target = rt_->GetImage();
    Image* depth = depth_->GetImage();
    VkPipeline apply = ShadowApplyPipeline(target->format);
    if (!apply || !depth)
        return;
    shadowMap_->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    depth->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    target->Transition(cb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = target->AttachmentView(rt_->level, rt_->face);
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ar{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ar.renderArea = {{0, 0}, {target->width, target->height}};
    ar.layerCount = 1;
    ar.colorAttachmentCount = 1;
    ar.pColorAttachments = &ca;
    vkCmdBeginRendering(cb, &ar);
    VkViewport tv{0, 0, float(target->width), float(target->height), 0, 1};
    VkRect2D ts{{0, 0}, {target->width, target->height}};
    vkCmdSetViewport(cb, 0, 1, &tv);
    vkCmdSetScissor(cb, 0, 1, &ts);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, apply);
    ShadowApplyParams p{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            p.camToLight[c * 4 + r] = camToLight.m[r * 4 + c];  // GLSL mat4 is column-major
    p.proj[0] = sceneProjection_._11, p.proj[1] = sceneProjection_._22;
    p.proj[2] = sceneProjection_._33, p.proj[3] = sceneProjection_._34;
    p.misc[0] = sceneProjection_._43, p.misc[1] = h.shadowStrength, p.misc[2] = 1.0f / h.shadowSize;
    // Comparison bias: 0.25 world units, as depth (0.0015 of the depth span was fine with an 8 x range span; with the
    // 44 x range span it grew to ~4 units and cut shadows off below the torso).
    p.misc[3] = 0.25f / (44.0f * range);
    // Towards the sun, in camera space: -(view's 3x3 * sun direction).
    for (int i = 0; i < 3; ++i)
        p.sunCam[i] = -(view.m[i * 4] * h.sun[0] + view.m[i * 4 + 1] * h.sun[1] + view.m[i * 4 + 2] * h.sun[2]);
    const D3DVIEWPORT9& svp = scene_.draws.back().viewport;
    p.viewport[0] = float(svp.X), p.viewport[1] = float(svp.Y), p.viewport[2] = float(svp.Width);
    p.viewport[3] = float(svp.Height);
    p.sunCam[3] = 44.0f * range;  // LightMatrix's depth spans 44 x range
    p.soft[0] = std::tan(h.shadowSoftness * 3.14159265f / 180.0f);
    p.soft[1] = 2.0f * range;
    // Penumbra cap (texels): 16 texels is about 1.6 world units at the default range and size. The blocker-distance
    // estimate over-reads under tall occluders (cliffs), and the old cap of 256 texels (about 25 units) let the
    // filter reach far into lit ground and wash out the shadow's interior.
    static const float softMax = ConfigFloat("render", "shadow_soft_max", 96.0f);  // [render] shadow_soft_max (texels)
    p.soft[2] = softMax;
    static const float softMin = ConfigFloat("render", "shadow_soft_min", 4.0f);  // [render] shadow_soft_min (texels)
    p.soft[3] = softMin;
    p.fog[0] = scene_.fogStart, p.fog[1] = scene_.fogEnd, p.fog[2] = scene_.fog ? 1.0f : 0.0f;
    static const bool shadowDebug = ConfigInt("render", "shadow_debug", 0) != 0;
    p.fog[3] = shadowDebug ? 1.0f : 0.0f;
    std::memcpy(p.fogColor, scene_.fogColor, sizeof(scene_.fogColor));
    if (scene_.fog)
        ModernFogUniforms(p.mf, h, sceneProjection_, scene_.draws.back().viewport, scene_.fogStart, scene_.fogEnd,
                          false);
    VkDeviceSize paramsOffset = PushUniform(&p, sizeof(p));
    VkDescriptorImageInfo imgs[4] = {
        {shadowDepthSampler_, depth->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
        {shadowCompareSampler_, shadowMap_->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
        {},
        {shadowDepthSampler_, shadowMap_->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}};
    VkDescriptorBufferInfo pb{Slot().uniforms, paramsOffset, sizeof(p)};
    VkWriteDescriptorSet w[4];
    for (uint32_t b = 0; b < 4; ++b)
    {
        w[b] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[b].dstBinding = b;
        w[b].descriptorCount = 1;
        w[b].descriptorType = b == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        if (b == 2)
            w[b].pBufferInfo = &pb;
        else
            w[b].pImageInfo = &imgs[b];
    }
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowLayout_, 0, 4, w);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRendering(cb);
    static unsigned logged;
    if (logged++ < 3)
        Log("vulkan: shadow map: %u of %zu scene draws, range %.0f", drawn, scene_.draws.size(), range);
}

// --- GPU snowfall (vk_snow.h) ------------------------------------------------------------------------------------

VkPipeline VulkanDevice::SnowPipeline(VkFormat target)
{
    if (snowFailed_)
        return VK_NULL_HANDLE;
    if (snowPipeline_ && snowFormat_ == target)
        return snowPipeline_;
    VkDevice device = Ctx().device;
    if (!snowVs_)
    {
        snowVs_ = MakeModule(CompileGlsl(SnowVertexGlsl(), false));
        snowFs_ = MakeModule(CompileGlsl(SnowFragmentGlsl(), true));
        snowCs_ = MakeModule(CompileGlslStage(SnowComputeGlsl(), GlslStage::Compute));
        const VkShaderStageFlags all =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutBinding b[3] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, all, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dl.bindingCount = 3;
        dl.pBindings = b;
        vkCreateDescriptorSetLayout(device, &dl, nullptr, &snowSetLayout_);
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &snowSetLayout_;
        vkCreatePipelineLayout(device, &pl, nullptr, &snowLayout_);
        if (snowCs_ && snowLayout_)
        {
            VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT, ci.stage.module = snowCs_, ci.stage.pName = "main";
            ci.layout = snowLayout_;
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &snowCompute_);
        }
        if (!snowVs_ || !snowFs_ || !snowLayout_ || !snowCompute_)
        {
            Log("vulkan: snow: shaders failed to build; off");
            snowFailed_ = true;
            return VK_NULL_HANDLE;
        }
    }
    if (snowPipeline_)
    {
        VkPipeline old = snowPipeline_;
        DeferDestroy([device, old] { vkDestroyPipeline(device, old, nullptr); });
    }
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT, st[0].module = snowVs_, st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT, st[1].module = snowFs_, st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = VK_TRUE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &target;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext = &ri;
    ci.stageCount = 2;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = snowLayout_;
    snowPipeline_ = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &snowPipeline_) != VK_SUCCESS)
        snowPipeline_ = VK_NULL_HANDLE;
    snowFormat_ = target;
    return snowPipeline_;
}

// rhi::SceneEnd with [render] snow (rhi::Hints): the snowfall into the current target, over the scene, before the UI.
// --- Temporal anti-aliasing (docs/subsystems/remaster-aa.md) ---------------------------------------------------

namespace
{
float Halton(uint32_t i, uint32_t base)
{
    float f = 1.0f, r = 0.0f;
    for (; i; i /= base)
        f /= float(base), r += f * float(i % base);
    return r;
}
}  // namespace

// Per frame: whether this frame is anti-aliased (the helper runs AA and is caught up), and its subpixel jitter.
void VulkanDevice::AaBeginFrame()
{
    const rhi::SceneHints& h = rhi::Hints();
    aaActive_ = false;
    const bool inProcess = h.aaMode == rtipc::kAaFsr;
    if (!h.aaMode || (inProcess && aaFsrFailed_) || (!inProcess && (!Rt().Start() || !Rt().AaReady())))
    {
        static bool loggedFailure;
        if (h.aaMode && !loggedFailure && Rt().AaLog()[0])
            loggedFailure = true, Log("vulkan: temporal anti-aliasing unavailable: %s", Rt().AaLog());
        aaReset_ = true;
        return;
    }
    aaActive_ = true;
    ++aaFrame_;
    const uint32_t phase = static_cast<uint32_t>(aaFrame_ % 16) + 1;  // Halton (2, 3), 16 phases, centred
    aaJitter_[0] = Halton(phase, 2) - 0.5f;
    aaJitter_[1] = Halton(phase, 3) - 0.5f;
}

bool VulkanDevice::AaEnsureImages(uint32_t width, uint32_t height)
{
    Context& c = Ctx();
    if (aaOutput_.image && aaWidth_ == width && aaHeight_ == height)
        return true;
    vkDeviceWaitIdle(c.device);
    for (AaImage* i : {&aaDepth_, &aaOutput_, &aaExposure_})
    {
        if (i->image)
            vkDestroyImage(c.device, i->image, nullptr), vkFreeMemory(c.device, i->memory, nullptr);
        *i = AaImage{};
    }
    if (aaDepthBuffer_)
        vkDestroyBuffer(c.device, aaDepthBuffer_, nullptr), vkFreeMemory(c.device, aaDepthBufferMemory_, nullptr);
    aaDepthBuffer_ = VK_NULL_HANDLE;
    auto make = [&](AaImage& img, VkFormat format, VkImageUsageFlags usage, uint32_t w = 0, uint32_t hh = 0) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format;
        ci.extent = {w ? w : width, hh ? hh : height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = usage;
        if (vkCreateImage(c.device, &ci, nullptr, &img.image) != VK_SUCCESS)
            return false;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(c.device, img.image, &req);
        img.memory = c.Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkBindImageMemory(c.device, img.image, img.memory, 0);
        img.format = format, img.usage = usage, img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return img.memory != VK_NULL_HANDLE;
    };
    const bool ok = make(aaDepth_, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) &&
                    make(aaOutput_, VK_FORMAT_R16G16B16A16_SFLOAT,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
                    make(aaExposure_, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         1, 1);
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = VkDeviceSize(width) * height * 4;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (!ok || vkCreateBuffer(c.device, &bi, nullptr, &aaDepthBuffer_) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, aaDepthBuffer_, &req);
    aaDepthBufferMemory_ = c.Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindBufferMemory(c.device, aaDepthBuffer_, aaDepthBufferMemory_, 0);
    aaWidth_ = width, aaHeight_ = height, aaReset_ = true;
    return aaDepthBufferMemory_ != VK_NULL_HANDLE;
}

void VulkanDevice::AaBarrier(VkCommandBuffer cb, AaImage& img, VkImageLayout to)
{
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = img.layout, b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cb, &di);
    img.layout = to;
}

// rhi::SceneEnd, after everything in the scene: AMD FSR 3.1 at native resolution, in this process (mode 1), over the
// back buffer (linear HDR), with the scene depth (as R32F) and the motion vectors; the result is copied back over
// the back buffer and the UI draws on top. Modes 2 / 3 go to the 64-bit helper (TemporalAaHelper).
void VulkanDevice::TemporalAa()
{
    const rhi::SceneHints& h = rhi::Hints();
    if (h.aaMode != rtipc::kAaFsr)
        return TemporalAaHelper();
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    ffDirty_ = true;  // the UI's draws (after SceneEnd) recompute their uniforms without the jitter
    if (!aaActive_ || scene_.draws.empty() || !depth_ || !rt_ || rt_->GetImage() != backBuffer_.get() || !motion_)
        return;
    Image* color = backBuffer_.get();
    Image* depth = depth_->GetImage();
    const uint32_t W = color->width, H = color->height;
    const D3DVIEWPORT9& svp = scene_.draws.back().viewport;
    if (!depth || svp.X != 0 || svp.Y != 0 || svp.Width != W || svp.Height != H || !RtRemoteInit())
        return;
    Context& c = Ctx();
    char log[160] = {};
    if (!AaEnsureImages(W, H) || !FsrEnsure(c.device, c.physical, vkGetDeviceProcAddr, W, H, log, sizeof(log)))
    {
        Log("vulkan: FSR unavailable: %s", log[0] ? log : "images");
        aaFsrFailed_ = true;
        return;
    }
    // 32-bit Vulkan: non-dispatchable handles are 64-bit values; the SDK keeps them in pointers (hook/CMakeLists.txt).
    const uint64_t handles[] = {uint64_t(color->image), uint64_t(motion_->image), uint64_t(aaDepth_.image),
                                uint64_t(aaOutput_.image)};
    for (uint64_t hnd : handles)
        if (hnd > 0xffffffffull)
        {
            Log("vulkan: FSR off: a Vulkan handle does not fit in 32 bits (0x%llx)",
                static_cast<unsigned long long>(hnd));
            aaFsrFailed_ = true;
            return;
        }

    EndRendering();
    VkCommandBuffer cb = Slot().cb;
    // Depth as floats 0..1 (the ray-tracing path's conversion, RtDepthComputeGlsl) -> R32F image.
    depth->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    if (!shadowDepthSampler_)
    {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(c.device, &si, nullptr, &shadowDepthSampler_);
    }
    float dp[8] = {0.0f, 0.0f, float(W), float(H), sceneProjection_._33, sceneProjection_._34, sceneProjection_._43,
                   0.0f};
    VkDeviceSize dOffset = PushUniform(dp, sizeof(dp));
    VkDescriptorImageInfo dimg{shadowDepthSampler_, depth->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo zb{aaDepthBuffer_, 0, VkDeviceSize(W) * H * 4};
    VkDescriptorBufferInfo db{Slot().uniforms, dOffset, sizeof(dp)};
    VkWriteDescriptorSet w[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                 {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstBinding = 0, w[0].descriptorCount = 1, w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &dimg;
    w[1].dstBinding = 1, w[1].descriptorCount = 1, w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &zb;
    w[2].dstBinding = 2, w[2].descriptorCount = 1, w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &db;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rtDepthPipe_);
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rtDepthLayout_, 0, 3, w);
    vkCmdDispatch(cb, (W + 7) / 8, (H + 7) / 8, 1);
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, mb.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cb, &di);
    AaBarrier(cb, aaDepth_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {W, H, 1};
    vkCmdCopyBufferToImage(cb, aaDepthBuffer_, aaDepth_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    AaBarrier(cb, aaDepth_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    color->Transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    motion_->Transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AaBarrier(cb, aaOutput_, VK_IMAGE_LAYOUT_GENERAL);
    // Exposure 1.0 (the scene is display-referred): our own 1x1 texture, cleared each frame.
    AaBarrier(cb, aaExposure_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    {
        VkClearColorValue one{};
        one.float32[0] = one.float32[1] = 1.0f;
        const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, aaExposure_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1, &all);
    }
    AaBarrier(cb, aaExposure_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const VkImageUsageFlags targetUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    FsrFrame f{};
    f.cb = cb;
    f.color = {color->image, color->format, W, H, targetUsage};
    f.depth = {aaDepth_.image, aaDepth_.format, W, H, aaDepth_.usage};
    f.motion = {motion_->image, motion_->format, W, H, targetUsage};
    f.output = {aaOutput_.image, aaOutput_.format, W, H, aaOutput_.usage};
    f.exposure = {aaExposure_.image, aaExposure_.format, 1, 1, aaExposure_.usage};
    f.jitter[0] = aaJitter_[0], f.jitter[1] = aaJitter_[1];
    // D3D projection: |_33| = f / (f - n), |_43| = n |_33| (absolute values: the game's projection is right-handed).
    const float m33 = std::fabs(sceneProjection_._33), m43 = std::fabs(sceneProjection_._43);
    f.cameraNear = m33 > 1e-6f ? m43 / m33 : 1.0f;
    f.cameraFar = m33 > 1.0f + 1e-6f ? m33 * f.cameraNear / (m33 - 1.0f) : 100000.0f;
    f.fovY = sceneProjection_._22 != 0.0f ? 2.0f * std::atan(1.0f / std::fabs(sceneProjection_._22)) : 1.0472f;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    f.frameTimeMs = aaLastTime_.QuadPart
                        ? float(double(now.QuadPart - aaLastTime_.QuadPart) * 1000.0 / double(freq.QuadPart))
                        : 16.7f;
    aaLastTime_ = now;
    f.sharpness = h.aaSharpness;
    f.reset = aaReset_;
    f.debugView = h.aaDebug == 1;
    // [render] aa_debug: 4 = no FSR, the scene copied into the output (tests the copy path); 5 = the output cleared
    // to magenta before FSR (tests whether FSR writes it).
    if (h.aaDebug == 4 || h.aaDebug == 5)
    {
        AaBarrier(cb, aaOutput_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (h.aaDebug == 5)
        {
            VkClearColorValue magenta{};
            magenta.float32[0] = magenta.float32[2] = magenta.float32[3] = 1.0f;
            vkCmdClearColorImage(cb, aaOutput_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &magenta, 1, &all);
        }
        else
        {
            color->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkImageCopy pc{};
            pc.srcSubresource = pc.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            pc.extent = {W, H, 1};
            vkCmdCopyImage(cb, color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, aaOutput_.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &pc);
            color->Transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        AaBarrier(cb, aaOutput_, VK_IMAGE_LAYOUT_GENERAL);
    }
    if (h.aaDebug != 4 && !FsrDispatch(f, log, sizeof(log)))
    {
        Log("vulkan: %s; FSR off", log);
        aaFsrFailed_ = true;
        return;
    }
    aaReset_ = false;
    // Output -> back buffer (FSR leaves its inputs in the states they came in).
    AaBarrier(cb, aaOutput_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    color->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy ic{};
    ic.srcSubresource = ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ic.extent = {W, H, 1};
    vkCmdCopyImage(cb, aaOutput_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, color->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);
    static bool logged;
    if (!logged)
        logged = true, Log("vulkan: temporal anti-aliasing on: AMD FSR 3.1 native AA in process, %ux%u", W, H);
}

// Modes 2 / 3 through the 64-bit helper: the back buffer (linear HDR), its depth and the motion vectors go to it
// (rt_ipc.h kAaColor / kAaDepth / kAaMotion), the frame is split there, and the next part starts by copying the
// helper's output (kAaOutput) back over the back buffer; the UI then draws on top.
void VulkanDevice::TemporalAaHelper()
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    ffDirty_ = true;  // the UI's draws (after SceneEnd) recompute their uniforms without the jitter
    const rhi::SceneHints& h = rhi::Hints();
    if (!aaActive_ || scene_.draws.empty() || !depth_ || !rt_ || rt_->GetImage() != backBuffer_.get() || !motion_)
        return;
    Image* color = backBuffer_.get();
    Image* depth = depth_->GetImage();
    const uint32_t W = color->width, H = color->height;
    const D3DVIEWPORT9& svp = scene_.draws.back().viewport;
    if (!depth || svp.X != 0 || svp.Y != 0 || svp.Width != W || svp.Height != H || !RtRemoteInit())
        return;
    RtClient& rc = Rt();
    if (!rc.EnsureAa(W, H))
        return;
    if (W != aaWidth_ || H != aaHeight_)
        aaReset_ = true, aaWidth_ = W, aaHeight_ = H;

    EndRendering();
    VkCommandBuffer cb = Slot().cb;
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {W, H, 1};
    color->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    motion_->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkCmdCopyImageToBuffer(cb, color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rc.Buffer(rtipc::kAaColor), 1,
                           &region);
    vkCmdCopyImageToBuffer(cb, motion_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rc.Buffer(rtipc::kAaMotion), 1,
                           &region);
    // Depth as floats 0..1 (the ray-tracing path's conversion, RtDepthComputeGlsl).
    depth->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    if (!shadowDepthSampler_)
    {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(Ctx().device, &si, nullptr, &shadowDepthSampler_);
    }
    float dp[8] = {0.0f, 0.0f, float(W), float(H), sceneProjection_._33, sceneProjection_._34, sceneProjection_._43,
                   0.0f};
    VkDeviceSize dOffset = PushUniform(dp, sizeof(dp));
    VkDescriptorImageInfo dimg{shadowDepthSampler_, depth->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo zb{rc.Buffer(rtipc::kAaDepth), 0, VkDeviceSize(W) * H * 4};
    VkDescriptorBufferInfo db{Slot().uniforms, dOffset, sizeof(dp)};
    VkWriteDescriptorSet w[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                 {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstBinding = 0, w[0].descriptorCount = 1, w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &dimg;
    w[1].dstBinding = 1, w[1].descriptorCount = 1, w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &zb;
    w[2].dstBinding = 2, w[2].descriptorCount = 1, w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &db;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rtDepthPipe_);
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rtDepthLayout_, 0, 3, w);
    vkCmdDispatch(cb, (W + 7) / 8, (H + 7) / 8, 1);
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cb, &di);

    // Hand over to the helper; the next part of the frame waits for its output.
    SplitSubmit(rc.AaToHelper(), aaFrame_);
    rtipc::AaParams p{};
    p.frame = aaFrame_;
    p.width = W, p.height = H;
    p.mode = h.aaMode;
    p.reset = aaReset_ ? 1 : 0;
    p.jitter[0] = aaJitter_[0], p.jitter[1] = aaJitter_[1];
    // D3D projection: _33 = f / (f - n), _43 = -n _33, _22 = cot(fovY / 2).
    const float m33 = sceneProjection_._33, m43 = sceneProjection_._43;
    p.cameraNear = m33 != 0.0f ? -m43 / m33 : 1.0f;
    p.cameraFar = m33 != 1.0f ? m33 * p.cameraNear / (m33 - 1.0f) : 100000.0f;
    p.fovY = sceneProjection_._22 != 0.0f ? 2.0f * std::atan(1.0f / sceneProjection_._22) : 1.0472f;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    p.frameTimeMs = aaLastTime_.QuadPart ? float(double(now.QuadPart - aaLastTime_.QuadPart) * 1000.0 / double(freq.QuadPart))
                                         : 16.7f;
    aaLastTime_ = now;
    p.sharpness = h.aaSharpness;
    p.debug = h.aaDebug;
    rc.RequestAa(p);
    helperWaits_.push_back({rc.AaFromHelper(), aaFrame_, true});
    aaReset_ = false;

    cb = Slot().cb;
    VkMemoryBarrier2 hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    hb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, hb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    hb.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, hb.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    VkDependencyInfo hdi{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    hdi.memoryBarrierCount = 1;
    hdi.pMemoryBarriers = &hb;
    vkCmdPipelineBarrier2(cb, &hdi);
    color->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(cb, rc.Buffer(rtipc::kAaOutput), color->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &region);
    // The backend the helper ran (it reports it per frame; FSR stands in for a missing library).
    static uint32_t loggedMode = ~0u;
    if (rc.AaActiveMode() != loggedMode && aaFrame_ > 2)
        loggedMode = rc.AaActiveMode(),
        Log("vulkan: temporal anti-aliasing on (helper mode %u of %u requested, %ux%u)", loggedMode, h.aaMode, W, H);
}

void VulkanDevice::Snow()
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    const rhi::SceneHints& h = rhi::Hints();
    if (h.levelLoading)
        snowFilled_ = false;  // the next snowy frame fills the volume afresh (pauses keep the flakes)
    if (!h.snow || !h.haveView || scene_.draws.empty() || !depth_ || !rt_ || !h.snowCount)
        return;
    Image* target = rt_->GetImage();
    Image* depth = depth_->GetImage();
    VkPipeline pipeline = SnowPipeline(target->format);
    if (!pipeline || !depth)
        return;
    Mat4 view, toWorld;
    std::memcpy(view.m, h.view, sizeof(view.m));
    if (!Invert(view, toWorld))
        return;
    Context& ctx = Ctx();
    // Flake state buffer (device local), recreated when the flake count grows.
    if (h.snowCount > snowCapacity_)
    {
        if (snowBuffer_)
        {
            VkDevice device = ctx.device;
            VkBuffer ob = snowBuffer_;
            VkDeviceMemory om = snowMemory_;
            DeferDestroy([device, ob, om] {
                vkDestroyBuffer(device, ob, nullptr);
                vkFreeMemory(device, om, nullptr);
            });
            snowBuffer_ = VK_NULL_HANDLE, snowMemory_ = VK_NULL_HANDLE;
        }
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = VkDeviceSize(h.snowCount) * 32;
        bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (vkCreateBuffer(ctx.device, &bi, nullptr, &snowBuffer_) != VK_SUCCESS)
            return;
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(ctx.device, snowBuffer_, &req);
        snowMemory_ = ctx.Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkBindBufferMemory(ctx.device, snowBuffer_, snowMemory_, 0);
        snowCapacity_ = h.snowCount;
        snowFilled_ = false;
    }
    float cam[3] = {toWorld.m[3], toWorld.m[7], toWorld.m[11]};
    // A camera jump (level start, teleport, cut) fills the volume around the new position at once.
    float jump = std::fabs(cam[0] - snowLastCam_[0]) + std::fabs(cam[1] - snowLastCam_[1]) +
                 std::fabs(cam[2] - snowLastCam_[2]);
    bool fill = !snowFilled_ || jump > h.snowBox;
    std::memcpy(snowLastCam_, cam, sizeof(cam));
    snowFilled_ = true;

    EndRendering();
    VkCommandBuffer cb = Slot().cb;
    depth->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    const D3DVIEWPORT9& svp = scene_.draws.back().viewport;
    SnowParams p{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            p.view[c * 4 + r] = view.m[r * 4 + c];  // GLSL mat4 is column-major
    p.proj[0] = sceneProjection_._11, p.proj[1] = sceneProjection_._22;
    p.proj[2] = sceneProjection_._33, p.proj[3] = sceneProjection_._34;
    p.proj2[0] = sceneProjection_._43, p.proj2[1] = h.snowTime, p.proj2[2] = h.snowSize, p.proj2[3] = h.snowBox;
    p.camPos[0] = cam[0], p.camPos[1] = cam[1], p.camPos[2] = cam[2], p.camPos[3] = fill ? 1.0f : 0.0f;
    p.motion[0] = h.snowWind[0], p.motion[1] = h.snowWind[1], p.motion[2] = h.snowFall;
    p.motion[3] = h.snowTurbulence;
    p.viewport[0] = float(svp.X), p.viewport[1] = float(svp.Y), p.viewport[2] = float(svp.Width);
    p.viewport[3] = float(svp.Height);
    p.target[0] = float(target->width), p.target[1] = float(target->height);
    p.target[2] = float(h.snowCount), p.target[3] = std::min(h.snowDt, 0.1f);
    if (h.snowDebug)
        p.color[0] = 1.0f, p.color[1] = 0.05f, p.color[2] = 0.05f, p.color[3] = 1.0f;
    else
        p.color[0] = 0.95f, p.color[1] = 0.97f, p.color[2] = 1.0f, p.color[3] = 0.85f;
    // Sky 20 units above the camera, volume 12 below it, drag time 0.5 s, streaks over 1/60 s.
    p.sim[0] = h.snowSky, p.sim[1] = 40.0f, p.sim[2] = 0.5f, p.sim[3] = 1.0f / 60.0f;  // 40: depth below the camera
    VkDeviceSize offset = PushUniform(&p, sizeof(p));
    if (!shadowDepthSampler_)
    {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(ctx.device, &si, nullptr, &shadowDepthSampler_);
    }
    VkDescriptorImageInfo img{shadowDepthSampler_, depth->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo pb{Slot().uniforms, offset, sizeof(p)};
    VkDescriptorBufferInfo fb{snowBuffer_, 0, VkDeviceSize(h.snowCount) * 32};
    VkWriteDescriptorSet w[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                 {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstBinding = 0, w[0].descriptorCount = 1, w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &img;
    w[1].dstBinding = 1, w[1].descriptorCount = 1, w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[1].pBufferInfo = &pb;
    w[2].dstBinding = 2, w[2].descriptorCount = 1, w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[2].pBufferInfo = &fb;

    // Simulate (vk_snow.cpp). Last frame's draw read the flakes in its vertex shader before this pass writes them.
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cb, &di);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, snowCompute_);
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_COMPUTE, snowLayout_, 0, 3, w);
    vkCmdDispatch(cb, (h.snowCount + 63) / 64, 1, 1);
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    vkCmdPipelineBarrier2(cb, &di);

    // Draw.
    target->Transition(cb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = target->AttachmentView(rt_->level, rt_->face);
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {target->width, target->height}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRendering(cb, &ri);
    VkViewport v{float(svp.X), float(svp.Y), float(svp.Width), float(svp.Height), 0, 1};
    VkRect2D sc{{static_cast<int32_t>(svp.X), static_cast<int32_t>(svp.Y)}, {svp.Width, svp.Height}};
    vkCmdSetViewport(cb, 0, 1, &v);
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, snowLayout_, 0, 3, w);
    vkCmdDraw(cb, 6, h.snowCount, 0, 0);
    vkCmdEndRendering(cb);
}

// [render] vk_dump_scene=N: the presented frame whose scene description is dumped (vk_scene.h); -1 none.
uint64_t DumpSceneFrame()
{
    static const int frame = ConfigInt("render", "vk_dump_scene", -1);
    return frame < 0 ? ~0ull : static_cast<uint64_t>(frame);
}

// The capture buffer (host-visible, 64 MB: 4M vertices), created at the first capture frame.
bool VulkanDevice::EnsureCapture()
{
    if (captureBuffer_)
        return true;
    Context& c = Ctx();
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = 64ull << 20;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (vkCreateBuffer(c.device, &bi, nullptr, &captureBuffer_) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, captureBuffer_, &req);
    captureMemory_ = c.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(c.device, captureBuffer_, captureMemory_, 0);
    vkMapMemory(c.device, captureMemory_, 0, bi.size, 0, reinterpret_cast<void**>(&captureMapped_));
    captureSize_ = bi.size;
    return captureMapped_ != nullptr;
}

uint64_t VulkanDevice::DrawId(uint64_t base)
{
    uint32_t n = drawOccurrence_[base]++;
    return Hash(&n, sizeof(n), base);
}

// The opaque scene is complete at the first translucent draw (blended, no depth writes) into the scene after opaque
// draws: ambient occlusion and shadows go in here, under the translucent layers (the intro's white fade, particles,
// glass), which used to get darkened from above when both ran at SceneEnd.
void VulkanDevice::BeforeDraw()
{
    if (lightingDone_ || sceneEnded_ || scene_.draws.empty() || !rs_[D3DRS_ALPHABLENDENABLE] ||
        rs_[D3DRS_ZWRITEENABLE] || !SceneDrawTarget())
        return;
    lightingDone_ = true;
    Shadows();  // first: the ray-traced sun visibility feeds the occlusion pass
    Ambient();
}

HRESULT VulkanDevice::DrawPrimitive(D3DPRIMITIVETYPE t, UINT start, UINT count)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    BeforeDraw();
    const void* what[4] = {streams_[0], vs_, textures_[0], nullptr};
    uint32_t range[3] = {static_cast<uint32_t>(t), start, count};
    uint64_t id = DrawId(Hash(range, sizeof(range), Hash(what, sizeof(what))));
    captureFirst_ = start, captureCount_ = VertexCount(t, count);
    if (PrepareDraw(t, id))
    {
        if (!tessDraw_ || !DrawTessellated(t, nullptr, false, 0, 0, start, count))
            vkCmdDraw(Slot().cb, VertexCount(t, count), 1, start, 0);
        RecordScene(t, false, 0, start, count, id);
    }
    return D3D_OK;
}

HRESULT VulkanDevice::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT base, UINT minIndex, UINT vertices, UINT si,
                                           UINT count)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    BeforeDraw();
    const void* what[4] = {streams_[0], vs_, textures_[0], indices_};
    uint32_t range[4] = {static_cast<uint32_t>(t), static_cast<uint32_t>(base), si, count};
    uint64_t id = DrawId(Hash(range, sizeof(range), Hash(what, sizeof(what))));
    captureFirst_ = int64_t(base) + minIndex, captureCount_ = vertices;  // D3D9: indices + base in this range
    if (!indices_ || !PrepareDraw(t, id))
        return D3D_OK;
    VkCommandBuffer cb = Slot().cb;
    indices_->MarkUsed(serial);
    if (!tessDraw_ || !DrawTessellated(t, indices_->Contents(), indices_->indexType == VK_INDEX_TYPE_UINT32,
                                       indices_->length, base, si, count))
    {
        vkCmdBindIndexBuffer(cb, indices_->Current().buffer, 0, indices_->indexType);
        vkCmdDrawIndexed(cb, VertexCount(t, count), 1, si, base, 0);
    }
    RecordScene(t, true, base, si, count, id);
    return D3D_OK;
}

// Scene description (vk_scene.h): 3D draws into the back buffer, i.e. with depth testing. HUD, menus and
// off-screen passes (render-to-texture) are not part of it.
void VulkanDevice::RecordScene(D3DPRIMITIVETYPE t, bool indexed, INT base, UINT start, UINT count, uint64_t id)
{
    if (!SceneDrawTarget())
        return;
    if (scene_.draws.empty())
    {
        scene_.width = backBuffer_->width, scene_.height = backBuffer_->height;
        scene_.lightCount = 0;
        for (int i = 0; i < 8; ++i)
            if (lightOn_[i])
                scene_.lights[scene_.lightCount++] = lights_[i];
    }
    SceneDraw d{};
    d.viewport = viewport_;
    d.id = id;
    d.type = t;
    d.indexed = indexed;
    d.baseVertex = base;
    d.start = start;
    d.primitives = count;
    for (int s = 0; s < 4; ++s)
        if (streams_[s])
        {
            VulkanBuffer::Version& v = streams_[s]->Current();
            d.streams[s] = {v.mapped, v.buffer, streamOffset_[s], streamStride_[s], streams_[s]->length};
            d.streamObjects[s] = streams_[s];
        }
    if (indexed && indices_)
    {
        d.indexData = indices_->Current().mapped;
        d.indexObject = indices_;
        d.indexBuffer = indices_->Current().buffer;
        d.indexLength = indices_->length;
        d.index32 = indices_->indexType == VK_INDEX_TYPE_UINT32;
    }
    d.layout = layout_;
    std::memcpy(d.world, world_, sizeof(world_));
    d.view = view_;
    d.projection = projection_;
    d.vertexShader = vs_ != nullptr;
    if (vs_)
    {
        d.vsConstantsOffset = static_cast<uint32_t>(scene_.vsConstants.size());
        scene_.vsConstants.insert(scene_.vsConstants.end(), vsConst_, vsConst_ + kVsConstantCount * 4);
    }
    d.vertexBlend = rs_[D3DRS_VERTEXBLEND] != D3DVBF_DISABLE || rs_[D3DRS_INDEXEDVERTEXBLENDENABLE];
    std::memcpy(d.textures, textures_, sizeof(textures_));
    d.material = material_;
    d.alphaBlend = rs_[D3DRS_ALPHABLENDENABLE] != 0;
    d.alphaTest = rs_[D3DRS_ALPHATESTENABLE] != 0;
    d.zWrite = rs_[D3DRS_ZWRITEENABLE] != 0;
    // Fog start and end as camera distances: shader draws take them from c1 (Renderer_SetFog's shader path:
    // {start, end, 1 / (end - start), 0.5}), fixed-function draws with vertex fog from the render states.
    if (d.zWrite && !d.alphaBlend && rs_[D3DRS_FOGENABLE] && (vs_ || rs_[D3DRS_FOGTABLEMODE] == D3DFOG_NONE))
    {
        scene_.fog = true;
        scene_.fogStart = vs_ ? vsConst_[4] : U2F(rs_[D3DRS_FOGSTART]);
        scene_.fogEnd = vs_ ? vsConst_[5] : U2F(rs_[D3DRS_FOGEND]);
        float c[4];
        ColorToFloat(rs_[D3DRS_FOGCOLOR], c);
        std::memcpy(scene_.fogColor, c, sizeof(scene_.fogColor));
    }
    d.vertexModule = lastVertex_;
    d.input = lastInput_;
    d.vertexCount = VertexCount(t, count);
    d.captureModule = vs_ && rhi::Hints().shadowsRt && (Ctx().rayQuery || Ctx().externalSharing)
                          ? vs_->CaptureVariant()
                          : VK_NULL_HANDLE;
    d.vFirst = captureFirst_, d.vCount = captureCount_;
    if (captureDraw_)
    {
        d.capture = reinterpret_cast<const float*>(captureMapped_ + captureOffset_);
        d.captureFirst = captureFirst_;
        d.captureCount = captureCount_;
    }
    scene_.draws.push_back(d);
}

// --- rhi::Hints, rhi::SceneEnd (rhi.h) -------------------------------------------------------------------------

}  // namespace rhi::vulkan

rhi::SceneHints& rhi::Hints()
{
    static SceneHints h;
    return h;
}

void rhi::SceneEnd()
{
    if (rhi::vulkan::g_device)
    {
        if (!rhi::vulkan::g_device->lightingDone_)
        {
            rhi::vulkan::g_device->Shadows();
            rhi::vulkan::g_device->Ambient();
            rhi::vulkan::g_device->lightingDone_ = true;
        }
        // Temporal AA before the GPU snowfall: flakes have no motion vectors, and FSR averaged them away as noise.
        rhi::vulkan::g_device->TemporalAa();
        rhi::vulkan::g_device->Snow();
        rhi::vulkan::g_device->sceneEnded_ = true;
    }
}

namespace rhi::vulkan
{
// --- resource helpers declared in vk_resources.h ---------------------------------------------------------------

uint64_t CurrentSerial()
{
    return g_device ? g_device->serial : 0;
}

uint64_t CompletedSerial()
{
    return g_device ? g_device->completed : 0;
}

void DeferDestroy(std::function<void()> destroy)
{
    if (g_device)
        g_device->Slot().destroy.push_back(std::move(destroy));
    else
        destroy();
}

void RecordTransfer(const std::function<void(VkCommandBuffer)>& record)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    if (g_device)
    {
        g_device->Transfer(record);
        return;
    }
    VkCommandBuffer cb = Ctx().BeginOneShot();
    record(cb);
    Ctx().EndOneShot(cb);
}

// --- adapter ---------------------------------------------------------------------------------------------------

namespace
{
class VulkanAdapter final : public Counted<Adapter>
{
public:
    explicit VulkanAdapter(IDirect3D9* q) : q_(q) {}
    ~VulkanAdapter() override { q_->Release(); }
    UINT GetAdapterModeCount(UINT a, D3DFORMAT f) override { return q_->GetAdapterModeCount(a, f); }
    HRESULT EnumAdapterModes(UINT a, D3DFORMAT f, UINT m, D3DDISPLAYMODE* o) override { return q_->EnumAdapterModes(a, f, m, o); }
    HRESULT GetAdapterDisplayMode(UINT a, D3DDISPLAYMODE* o) override { return q_->GetAdapterDisplayMode(a, o); }
    HRESULT CheckDeviceFormat(UINT a, D3DDEVTYPE t, D3DFORMAT af, DWORD u, D3DRESOURCETYPE r, D3DFORMAT cf) override
    {
        // Same answers as D3D9 for formats this backend can create; no for the rest.
        static const int verbose = ConfigInt("render", "vulkan_verbose", 0);
        HRESULT hr = Format(cf).format == VK_FORMAT_UNDEFINED ? D3DERR_NOTAVAILABLE : q_->CheckDeviceFormat(a, t, af, u, r, cf);
        if (verbose)
            Log("vulkan: CheckDeviceFormat usage 0x%lx type %d format %d -> 0x%08lx", u, r, static_cast<int>(cf), hr);
        return hr;
    }
    HRESULT CheckDepthStencilMatch(UINT a, D3DDEVTYPE t, D3DFORMAT af, D3DFORMAT rf, D3DFORMAT df) override
    {
        return q_->CheckDepthStencilMatch(a, t, af, rf, df);
    }
    HRESULT GetDeviceCaps(UINT a, D3DDEVTYPE t, D3DCAPS9* c) override { return q_->GetDeviceCaps(a, t, c); }
    HRESULT CreateDevice(UINT, D3DDEVTYPE, HWND w, DWORD behavior, D3DPRESENT_PARAMETERS* p, Device** out) override
    {
        // D3D9 switches the calling thread's x87 FPU to single precision (24-bit mantissa) on CreateDevice unless
        // D3DCREATE_FPU_PRESERVE is given; the game's math runs in that mode (docs/recovery-guide.md). Without it the
        // simulation drifts from the D3D9 backend (different draw counts in tools/compare_backends.py).
        if (!(behavior & D3DCREATE_FPU_PRESERVE))
        {
            unsigned int cw;
            _controlfp_s(&cw, _PC_24, _MCW_PC);
        }
        auto* d = new VulkanDevice(q_, w, p);
        if (!d->ok)
        {
            d->Release();
            *out = nullptr;
            return D3DERR_NOTAVAILABLE;
        }
        *out = d;
        return D3D_OK;
    }

private:
    IDirect3D9* q_;
};
}  // namespace

Adapter* CreateAdapter(IDirect3D9* queries)
{
    if (!ContextInit())
        return nullptr;
    return new VulkanAdapter(queries);
}
}  // namespace rhi::vulkan
