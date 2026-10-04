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
#include "vk_fog.h"
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
    VkCommandBuffer cb = VK_NULL_HANDLE;
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
        vkAllocateCommandBuffers(c.device, &ai, &s.cb);
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(c.device, &fi, nullptr, &s.fence);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(c.device, &si, nullptr, &s.acquired);
        vkCreateSemaphore(c.device, &si, nullptr, &s.rendered);
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = kUniformRing;
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;  // + tessellation lists
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
    // format). Next to it: the motion-vector attachment (RG16F, vk_abi.h) and the 8-bit image the present pass
    // (tonemap / debug view) writes and the swapchain is filled from.
    backBuffer_ = std::make_shared<Image>();
    D3DFORMAT bbFormat = params_.BackBufferFormat == D3DFMT_UNKNOWN ? D3DFMT_X8R8G8B8 : params_.BackBufferFormat;
    if (!backBuffer_->Create(bbFormat, params_.BackBufferWidth, params_.BackBufferHeight, 1, 1, true, false,
                             VK_FORMAT_R16G16B16A16_SFLOAT))
        return false;
    motion_ = std::make_shared<Image>();
    if (!motion_->Create(D3DFMT_A8R8G8B8, params_.BackBufferWidth, params_.BackBufferHeight, 1, 1, true, false,
                         VK_FORMAT_R16G16_SFLOAT))
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
    ffDirty_ = true;  // per-frame uniforms (modern fog: camera)
    scene_.serial = serial;
    captureFrame_ = Ctx().vertexStores && DumpSceneFrame() == serial;
    captureUsed_ = 0;
    motionCleared_ = false;
    vkResetCommandBuffer(s.cb, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(s.cb, &bi);
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
    if (depth)
        depth->Transition(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = color->AttachmentView(rt_->level, rt_->face);
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (depth)
    {
        da.imageView = depth->AttachmentView(0, 0);
        da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
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
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = acquired ? 1 : 0;
    si.pWaitSemaphores = &s.acquired;
    si.pWaitDstStageMask = &waitStage;
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
    if (ps_)
    {
        fsm = ps_->shader->PixelVariant(key);
        samplerMask = ps_->shader->translated.samplerMask;
        cubeMask = ps_->shader->translated.samplerCubeMask;
    }
    else
    {
        GeneratedShader g = FixedFunctionPixel(key);  // cheap; the module is cached
        VkShaderModule& m = ffPixel_[keyHash];
        if (!m)
            m = MakeModule(CompileGlsl(g.glsl, true));
        fsm = m;
        samplerMask = g.samplerMask;
        cubeMask = g.samplerCubeMask;
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
    pk.motionFormat = rt_->GetImage() == backBuffer_.get() ? VK_FORMAT_R16G16_SFLOAT : VK_FORMAT_UNDEFINED;
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
        u.halfPixel[2] = captureDraw_ ? static_cast<float>(captureFirst_) : 0.0f;  // capture base vertex (vk_abi.h)
        u.tessParams[0] = tessNormalDecode_, u.tessParams[1] = Tess().shape;
        u.tessParams[2] = Tess().maxLevel, u.tessParams[3] = Tess().pixels;
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
    VkWriteDescriptorSet writes[5 + kMaxSamplers];
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
    uint32_t n = t == D3DPT_TRIANGLELIST ? count * 3 : count + 2;
    std::vector<uint32_t> v(n), list;
    for (uint32_t i = 0; i < n; ++i)
    {
        if (!indices)
        {
            v[i] = start + i;
            continue;
        }
        uint32_t size = index32 ? 4 : 2, at = (start + i) * size, idx = 0;
        if (at + size > indexLength)
            return false;
        std::memcpy(&idx, indices + at, size);
        v[i] = idx;
    }
    list.reserve(count * 3);
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
    if (list.empty())
        return true;
    VkDeviceSize at = PushUniform(list.data(), list.size() * 4);
    if (at == VkDeviceSize(-1))
        return false;
    VkCommandBuffer cb = Slot().cb;
    vkCmdBindIndexBuffer(cb, Slot().uniforms, at, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, static_cast<uint32_t>(list.size()), 1, 0, indices ? base : 0, 0);
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
    p.on[1] = std::log(2.0f) / (0.5f * range) * h.fogDensity;
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
    const std::string hlsl = GtaoHlsl();
    const char* entries[4] = {"CSPrefilterDepths16x16", "CSGTAOHigh", "CSDenoisePass", "CSDenoiseLastPass"};
    VkDescriptorSetLayoutBinding b[14];
    const VkDescriptorType types[14] = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLER,       VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    for (uint32_t i = 0; i < 14; ++i)
        b[i] = {i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dl.bindingCount = 14;
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
    gtaoApplyVs_ = MakeModule(CompileGlsl(GtaoApplyVertexGlsl(), false));
    gtaoApplyFs_ = MakeModule(CompileGlsl(GtaoApplyFragmentGlsl(), true));
    VkDescriptorSetLayoutBinding ab[3] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    dl.bindingCount = 3;
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
    Log("vulkan: ambient occlusion: XeGTAO ready");
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
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE, att.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
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
        for (GtaoImage* gi : {&gtaoDepth_, &gtaoTerm_, &gtaoEdges_, &gtaoFinal_})
            GtaoImageDestroy(*gi);
        gtaoWidth_ = svp.Width, gtaoHeight_ = svp.Height;
        EndRendering();
        if (!GtaoImageCreate(gtaoDepth_, VK_FORMAT_R32_SFLOAT, XE_GTAO_DEPTH_MIP_LEVELS) ||
            !GtaoImageCreate(gtaoTerm_, VK_FORMAT_R8_UINT, 1) || !GtaoImageCreate(gtaoEdges_, VK_FORMAT_R8_UNORM, 1) ||
            !GtaoImageCreate(gtaoFinal_, VK_FORMAT_R8_UINT, 1))
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
    VkDescriptorImageInfo img[12];
    img[0] = {VK_NULL_HANDLE, depth->sampledView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};  // 2
    for (int l = 0; l < 5; ++l)
        img[1 + l] = {VK_NULL_HANDLE, gtaoDepth_.mips[l], VK_IMAGE_LAYOUT_GENERAL};                  // 3..7
    img[6] = {VK_NULL_HANDLE, gtaoDepth_.view, VK_IMAGE_LAYOUT_GENERAL};                             // 8
    img[7] = {VK_NULL_HANDLE, gtaoTerm_.view, VK_IMAGE_LAYOUT_GENERAL};                              // 9
    img[8] = {VK_NULL_HANDLE, gtaoEdges_.view, VK_IMAGE_LAYOUT_GENERAL};                             // 10
    img[9] = {VK_NULL_HANDLE, gtaoTerm_.view, VK_IMAGE_LAYOUT_GENERAL};                              // 11
    img[10] = {VK_NULL_HANDLE, gtaoEdges_.view, VK_IMAGE_LAYOUT_GENERAL};                            // 12
    img[11] = {VK_NULL_HANDLE, gtaoFinal_.view, VK_IMAGE_LAYOUT_GENERAL};                            // 13
    VkWriteDescriptorSet w[14];
    for (uint32_t i = 0; i < 14; ++i)
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

    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    auto pass = [&](int i, uint32_t gx, uint32_t gy) {
        vkCmdPipelineBarrier2(cb, &di);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, gtaoPipe_[i]);
        vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_COMPUTE, gtaoLayout_, 0, 14, w);
        vkCmdDispatch(cb, gx, gy, 1);
        mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    };
    const uint32_t W = svp.Width, H = svp.Height;
    pass(0, (W + 15) / 16, (H + 15) / 16);                                                   // prefilter depths
    pass(1, (W + XE_GTAO_NUMTHREADS_X - 1) / XE_GTAO_NUMTHREADS_X,                         // main pass
         (H + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y);
    // Denoise (2 px per thread): term -> final, then final -> term (bindings 11 and 13 swapped); apply reads term.
    const uint32_t dx = (W + XE_GTAO_NUMTHREADS_X * 2 - 1) / (XE_GTAO_NUMTHREADS_X * 2);
    const uint32_t dy = (H + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y;
    pass(2, dx, dy);
    std::swap(img[9], img[11]);
    pass(3, dx, dy);
    mb.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    vkCmdPipelineBarrier2(cb, &di);

    // Apply.
    GtaoApplyParams ap{};
    ap.viewport[0] = float(svp.X), ap.viewport[1] = float(svp.Y), ap.viewport[2] = float(W), ap.viewport[3] = float(H);
    ap.fog[0] = scene_.fogStart, ap.fog[1] = scene_.fogEnd, ap.fog[2] = scene_.fog ? 1.0f : 0.0f;
    ap.fog[3] = h.aoStrength;
    std::memcpy(ap.fogColor, scene_.fogColor, sizeof(scene_.fogColor));
    if (scene_.fog)
        ModernFogUniforms(ap.mf, h, sceneProjection_, svp, scene_.fogStart, scene_.fogEnd, false);
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
    VkWriteDescriptorSet aw[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    aw[0].dstBinding = 0, aw[0].descriptorCount = 1, aw[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aw[0].pImageInfo = &aoImg;
    aw[1].dstBinding = 1, aw[1].descriptorCount = 1, aw[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aw[1].pImageInfo = &dImg;
    aw[2].dstBinding = 2, aw[2].descriptorCount = 1, aw[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    aw[2].pBufferInfo = &apb;
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gtaoApplyLayout_, 0, 3, aw);
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
    };
    std::vector<Job> jobs;
    jobs.reserve(scene_.draws.size() + casters_.size());
    for (const SceneDraw& d : scene_.draws)
    {
        if (!caster(d))
            continue;
        const float* c = &scene_.vsConstants[d.vsConstantsOffset];
        jobs.push_back({&d, c, camToLight});
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
        if (useCache && e.lastSeen != shadowFrame_ && !e.moving)
            jobs.push_back({&e.draw, e.constants.data(), Multiply(LightMatrix(centre, h.sun, range, h.shadowSize), e.toWorld)});
        ++it;
    }

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
    p.soft[2] = 256.0f;  // a sanity limit on the penumbra (texels), not a style choice
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
    p.sim[0] = h.snowSky, p.sim[1] = 12.0f, p.sim[2] = 0.5f, p.sim[3] = 1.0f / 60.0f;
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
    Ambient();
    Shadows();
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
            rhi::vulkan::g_device->Ambient();
            rhi::vulkan::g_device->Shadows();
            rhi::vulkan::g_device->lightingDone_ = true;
        }
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
