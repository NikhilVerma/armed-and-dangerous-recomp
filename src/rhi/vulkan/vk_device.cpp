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
#include "vk_resources.h"

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
constexpr VkDeviceSize kUniformRing = 32u << 20;
}  // namespace

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
    bool PrepareDraw(D3DPRIMITIVETYPE type);
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

    std::shared_ptr<Image> backBuffer_, autoDepth_;
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
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
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
    backBuffer_ = std::make_shared<Image>();
    D3DFORMAT bbFormat = params_.BackBufferFormat == D3DFMT_UNKNOWN ? D3DFMT_X8R8G8B8 : params_.BackBufferFormat;
    if (!backBuffer_->Create(bbFormat, params_.BackBufferWidth, params_.BackBufferHeight, 1, 1, true, false))
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
    vkWaitForFences(c.device, 1, &s.fence, VK_TRUE, UINT64_MAX);
    if (s.serial > completed)
        completed = s.serial;
    for (auto& d : s.destroy)
        d();
    s.destroy.clear();
    s.serial = serial;
    s.uniformUsed = 0;
    ffDirty_ = vsDirty_ = psDirty_ = true;  // new ring: re-upload uniforms
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
    uint32_t w = color->width >> rt_->level, h = color->height >> rt_->level;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {w ? w : 1, h ? h : 1}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
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
    Context& c = Ctx();
    static const int verbose = ConfigInt("render", "vulkan_verbose", 0);
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
    if (acquired)
    {
        backBuffer_->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
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
        VkImageBlit blit{};
        blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {static_cast<int32_t>(backBuffer_->width), static_cast<int32_t>(backBuffer_->height), 1};
        blit.dstOffsets[1] = {static_cast<int32_t>(swapExtent_.width), static_cast<int32_t>(swapExtent_.height), 1};
        vkCmdBlitImage(cb, backBuffer_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapImages_[index],
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
    if (TestFrameDump(&dumpFrame) && backBuffer_->format == VK_FORMAT_B8G8R8A8_UNORM)
    {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = VkDeviceSize(backBuffer_->width) * backBuffer_->height * 4;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkCreateBuffer(c.device, &bi, nullptr, &dumpBuffer);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(c.device, dumpBuffer, &req);
        dumpMemory = c.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(c.device, dumpBuffer, dumpMemory, 0);
        backBuffer_->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {backBuffer_->width, backBuffer_->height, 1};
        vkCmdCopyImageToBuffer(cb, backBuffer_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dumpBuffer, 1, &region);
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
    if (dumpBuffer)
    {
        vkWaitForFences(c.device, 1, &s.fence, VK_TRUE, UINT64_MAX);
        void* p;
        vkMapMemory(c.device, dumpMemory, 0, VK_WHOLE_SIZE, 0, &p);
        TestSaveBgra(dumpFrame, p, backBuffer_->width, backBuffer_->height, backBuffer_->width * 4);
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

bool VulkanDevice::PrepareDraw(D3DPRIMITIVETYPE type)
{
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
    r.stencilMask = static_cast<uint8_t>(rs_[D3DRS_STENCILMASK]);
    r.stencilWriteMask = static_cast<uint8_t>(rs_[D3DRS_STENCILWRITEMASK]);
    r.depthBiasEnable = (rs_[D3DRS_DEPTHBIAS] || rs_[D3DRS_SLOPESCALEDEPTHBIAS]) ? 1 : 0;
    r.topology = static_cast<uint8_t>(type);
    pk.layoutHash = inputKey;
    VkShaderModule pair[2] = {vsm, fsm};
    pk.shaderHash = Hash(pair, sizeof(pair));
    pk.colorFormat = rt_->GetImage()->format;
    pk.depthFormat = depth ? depth->format : VK_FORMAT_UNDEFINED;
    VkPipeline pipeline = GetPipeline(pk, input, vsm, fsm);
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

    // Descriptors
    FrameSlot& s = Slot();
    VkDescriptorBufferInfo bufs[3] = {{s.uniforms, vsOffset_, sizeof(vsConst_)},
                                      {s.uniforms, psOffset_, sizeof(psConst_)},
                                      {s.uniforms, ffOffset_, sizeof(FFUniforms)}};
    VkDescriptorImageInfo imgs[kMaxSamplers];
    VkWriteDescriptorSet writes[3 + kMaxSamplers];
    uint32_t nw = 0;
    for (uint32_t b = 0; b < 3; ++b)
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

HRESULT VulkanDevice::DrawPrimitive(D3DPRIMITIVETYPE t, UINT start, UINT count)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    if (PrepareDraw(t))
        vkCmdDraw(Slot().cb, VertexCount(t, count), 1, start, 0);
    return D3D_OK;
}

HRESULT VulkanDevice::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT base, UINT, UINT, UINT si, UINT count)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    if (!indices_ || !PrepareDraw(t))
        return D3D_OK;
    VkCommandBuffer cb = Slot().cb;
    vkCmdBindIndexBuffer(cb, indices_->Current().buffer, 0, indices_->indexType);
    indices_->MarkUsed(serial);
    vkCmdDrawIndexed(cb, VertexCount(t, count), 1, si, base, 0);
    return D3D_OK;
}

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
