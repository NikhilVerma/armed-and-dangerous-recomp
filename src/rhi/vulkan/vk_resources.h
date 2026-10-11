#pragma once
// rhi::vulkan resources: images (textures, render targets, depth), surfaces (a level/face of an image, or a CPU
// surface), buffers (vertex/index), vertex layouts and shaders. Device-side work (frames, draws) is vk_device.cpp.
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "vk_core.h"
#include "vk_shaders.h"

namespace rhi::vulkan
{
class VulkanDevice;

// Reference counting shared by all backend objects (COM-like, as rhi::Object requires).
template <class Base>
class Counted : public Base
{
public:
    ULONG AddRef() override { return ++refs_; }
    ULONG Release() override
    {
        ULONG r = --refs_;
        if (r == 0)
            delete this;
        return r;
    }
    void* Native() override { return nullptr; }

protected:
    std::atomic<ULONG> refs_{1};
};

// A VkImage with one tracked layout for all its subresources.
struct Image
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView sampledView = VK_NULL_HANDLE;         // all levels/faces, with the format's swizzle
    std::vector<VkImageView> lodViews;               // sampled views starting at mip level i (D3D9 SetLOD)
    std::vector<VkImageView> attachmentViews;        // per level x face, for rendering
    VkFormat format = VK_FORMAT_UNDEFINED;
    D3DFORMAT d3dFormat = D3DFMT_UNKNOWN;
    uint32_t width = 0, height = 0, levels = 1, faces = 1;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    bool renderTarget = false;

    // `storage`: a different Vulkan format for the same D3D9 format (the back buffer is stored as RGBA16F, the HDR
    // target, while the game still sees its own format).
    bool Create(D3DFORMAT f, uint32_t w, uint32_t h, uint32_t levels, uint32_t faces, bool rt, bool depth,
                VkFormat storage = VK_FORMAT_UNDEFINED);
    void Destroy();  // deferred until the frames using it are complete
    void Transition(VkCommandBuffer cb, VkImageLayout to);
    VkImageView AttachmentView(uint32_t level, uint32_t face);
    VkImageView SampledView(uint32_t baseLevel);  // levels baseLevel..end (D3D9 texture LOD)
};

class VulkanTexture;

// A level (and face) of a texture, a back buffer / depth / render-target image, or CPU-only memory.
class VulkanSurface final : public Counted<Surface>
{
public:
    VulkanSurface(VulkanTexture* owner, uint32_t level, uint32_t face);       // texture level (owned by it)
    VulkanSurface(std::shared_ptr<Image> image);                              // standalone image (back buffer, depth)
    VulkanSurface(D3DFORMAT f, uint32_t w, uint32_t h);                        // CPU-only (D3DPOOL_SYSTEMMEM/SCRATCH)
    ~VulkanSurface() override;

    // A texture's surface is owned by it (rhi.h): references act on the texture.
    ULONG AddRef() override;
    ULONG Release() override;
    Texture* Container() override;
    HRESULT GetDesc(D3DSURFACE_DESC* desc) override;
    HRESULT LockRect(D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) override;
    HRESULT UnlockRect() override;

    Image* GetImage() const;  // null for CPU-only surfaces
    uint32_t level = 0, face = 0;
    std::vector<uint8_t> cpu;  // CPU-only surfaces: the pixels; texture levels: see VulkanTexture::shadow
    D3DFORMAT cpuFormat = D3DFMT_UNKNOWN;
    uint32_t cpuWidth = 0, cpuHeight = 0;

private:
    VulkanTexture* owner_ = nullptr;
    std::shared_ptr<Image> image_;
    bool lockedReadOnly_ = false;
};

class VulkanTexture final : public Counted<Texture>
{
public:
    VulkanTexture(D3DFORMAT f, uint32_t w, uint32_t h, uint32_t levels, uint32_t faces, DWORD usage, D3DPOOL pool);
    ~VulkanTexture() override;
    bool ok = false;

    D3DRESOURCETYPE Type() override { return image->faces == 6 ? D3DRTYPE_CUBETEXTURE : D3DRTYPE_TEXTURE; }
    DWORD GetLevelCount() override { return image->levels; }
    DWORD GetLOD() override { return lod_; }
    DWORD Lod() const { return lod_; }
    DWORD SetLOD(DWORD lod) override
    {
        DWORD old = lod_;
        lod_ = lod < image->levels ? lod : image->levels - 1;
        return old;
    }
    HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) override;
    HRESULT GetSurfaceLevel(UINT level, Surface** out) override;
    HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, Surface** out) override;
    HRESULT LockRect(UINT face, UINT level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) override;
    HRESULT UnlockRect(UINT face, UINT level) override;

    std::shared_ptr<Image> image;
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_MANAGED;
    // CPU copy per level x face (allocated on first lock), uploaded on unlock.
    std::vector<std::vector<uint8_t>> shadow;
    std::vector<VulkanSurface*> surfaces;  // per level x face, created on first request, deleted with the texture
    std::vector<uint8_t>& Shadow(uint32_t level, uint32_t face);
    void Upload(uint32_t level, uint32_t face);

private:
    DWORD lod_ = 0;
    bool lockedDiscard_ = false;
    bool lockedReadOnly_ = false;
};

// Vertex or index buffer: host-visible, persistently mapped versions; DISCARD switches to a version the GPU is not
// using (D3D9 "renaming"), NOOVERWRITE writes the current one.
class VulkanBuffer final : public Counted<Buffer>
{
public:
    VulkanBuffer(uint32_t length, DWORD usage, bool index, D3DFORMAT indexFormat);
    ~VulkanBuffer() override;
    bool ok = false;

    HRESULT Lock(UINT offset, UINT size, void** data, DWORD flags) override;
    HRESULT Unlock() override { return D3D_OK; }

    struct Version
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint8_t* mapped = nullptr;
        uint64_t lastUse = 0;  // frame serial of the last draw that used it
    };
    Version& Current() { return versions_[current_]; }
    // The current contents (host-cached memory when available, see AddVersion).
    const uint8_t* Contents() const { return versions_[current_].mapped; }
    void MarkUsed(uint64_t serial) { versions_[current_].lastUse = serial; }
    uint32_t length = 0;
    bool index = false;
    VkIndexType indexType = VK_INDEX_TYPE_UINT16;

private:
    bool AddVersion();
    std::vector<Version> versions_;
    size_t current_ = 0;
};

class VulkanVertexLayout final : public Counted<VertexLayout>
{
public:
    explicit VulkanVertexLayout(const D3DVERTEXELEMENT9* e);
    std::vector<D3DVERTEXELEMENT9> elements;  // including D3DDECL_END
    uint64_t hash = 0;
};

// A D3D9 shader: bytecode translated and compiled once.
class VulkanShader final : public Counted<VertexShader>
{
public:
    VulkanShader(const DWORD* code, bool pixel);
    ~VulkanShader() override;
    TranslatedShader translated;
    VkShaderModule module = VK_NULL_HANDLE;  // vertex shaders (pixel shaders depend on D3D9_FINAL: per variant)
    std::vector<std::pair<uint64_t, VkShaderModule>> variants;  // pixel: FinalPixelDefine hash -> module
    VkShaderModule PixelVariant(const FFKey& key);
    VkShaderModule CaptureVariant();  // vertex: also writes clip positions (vk_abi.h kBindingCapture)
    VkShaderModule TessVariant();     // vertex: also writes the tessellation varyings (vk_abi.h "Tessellation")
    bool pixel = false;
};


class VulkanPixelShader final : public Counted<PixelShader>
{
public:
    VulkanPixelShader(const DWORD* code) : shader(new VulkanShader(code, true)) {}
    ~VulkanPixelShader() override { shader->Release(); }
    VulkanShader* shader;
};

VkShaderModule MakeModule(const std::vector<uint32_t>& spirv);

// Counters for the verbose per-frame log (vk_device.cpp): texture uploads (count, QueryPerformanceCounter ticks) and
// new buffer versions (renames that found no free version).
struct ResourceCounters
{
    long long uploadTicks = 0;
    uint32_t uploads = 0, bufferVersions = 0;
};
ResourceCounters& Counters();
// Frame serials: work recorded in frame `serial` is complete once CompletedSerial() >= serial.
uint64_t CurrentSerial();
uint64_t CompletedSerial();
// Destroy a Vulkan object once the GPU no longer uses it.
void DeferDestroy(std::function<void()> destroy);
// Run a transfer recorded into the current frame's command buffer (outside rendering) if a frame is open, else
// a one-shot submission. Used for uploads, so they land in order with the draws around them.
void RecordTransfer(const std::function<void(VkCommandBuffer)>& record);
}  // namespace rhi::vulkan
