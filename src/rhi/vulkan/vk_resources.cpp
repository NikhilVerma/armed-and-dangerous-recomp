// rhi::vulkan resources. See vk_resources.h.
#include "vk_resources.h"

#include <cstring>

#include "config.h"
#include "log.h"
#include "vk_pipeline.h"

namespace rhi::vulkan
{
// --- Image -----------------------------------------------------------------------------------------------------

bool Image::Create(D3DFORMAT f, uint32_t w, uint32_t h, uint32_t lv, uint32_t fc, bool rt, bool depth, VkFormat storage)
{
    Context& c = Ctx();
    FormatInfo fi = Format(f);
    if (storage != VK_FORMAT_UNDEFINED)
        fi.format = storage;
    if (fi.format == VK_FORMAT_UNDEFINED)
    {
        Log("vulkan: unsupported texture format %d", static_cast<int>(f));
        return false;
    }
    d3dFormat = f, format = fi.format, width = w, height = h, faces = fc, renderTarget = rt;
    uint32_t maxLevels = 1;
    for (uint32_t s = (w > h ? w : h); s > 1; s >>= 1)
        ++maxLevels;
    levels = lv == 0 || lv > maxLevels ? maxLevels : lv;
    aspect = depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | (fi.stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)) : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.flags = fc == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = levels;
    ci.arrayLayers = fc;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (rt)
        ci.usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkResult r = vkCreateImage(c.device, &ci, nullptr, &image);
    if (r != VK_SUCCESS)
        return LogVk("vkCreateImage", r), false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(c.device, image, &req);
    memory = c.Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(c.device, image, memory, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image;
    vi.viewType = fc == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.components = fi.swizzle;
    vi.subresourceRange = {static_cast<VkImageAspectFlags>(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0,
                           levels, 0, fc};
    vkCreateImageView(c.device, &vi, nullptr, &sampledView);
    attachmentViews.assign(levels * fc, VK_NULL_HANDLE);
    return true;
}

VkImageView Image::AttachmentView(uint32_t level, uint32_t face)
{
    VkImageView& v = attachmentViews[level * faces + face];
    if (!v)
    {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {aspect, level, 1, face, 1};
        vkCreateImageView(Ctx().device, &vi, nullptr, &v);
    }
    return v;
}

VkImageView Image::SampledView(uint32_t baseLevel)
{
    if (baseLevel == 0 || baseLevel >= levels)
        return sampledView;
    if (lodViews.size() < levels)
        lodViews.resize(levels, VK_NULL_HANDLE);
    VkImageView& v = lodViews[baseLevel];
    if (!v)
    {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = image;
        vi.viewType = faces == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.components = Format(d3dFormat).swizzle;
        vi.subresourceRange = {static_cast<VkImageAspectFlags>(aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                                                 : VK_IMAGE_ASPECT_COLOR_BIT),
                               baseLevel, levels - baseLevel, 0, faces};
        vkCreateImageView(Ctx().device, &vi, nullptr, &v);
    }
    return v;
}

void Image::Destroy()
{
    VkDevice d = Ctx().device;
    VkImage img = image;
    VkDeviceMemory mem = memory;
    VkImageView sv = sampledView;
    std::vector<VkImageView> av = attachmentViews;
    av.insert(av.end(), lodViews.begin(), lodViews.end());
    DeferDestroy([d, img, mem, sv, av] {
        for (VkImageView v : av)
            if (v)
                vkDestroyImageView(d, v, nullptr);
        if (sv)
            vkDestroyImageView(d, sv, nullptr);
        if (img)
            vkDestroyImage(d, img, nullptr);
        if (mem)
            vkFreeMemory(d, mem, nullptr);
    });
    image = VK_NULL_HANDLE;
}

void Image::Transition(VkCommandBuffer cb, VkImageLayout to)
{
    if (layout == to)
        return;
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = layout;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, levels, 0, faces};
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cb, &di);
    layout = to;
}

// --- Surface ---------------------------------------------------------------------------------------------------

VulkanSurface::VulkanSurface(VulkanTexture* owner, uint32_t lv, uint32_t fc) : owner_(owner)
{
    level = lv, face = fc;
}

ULONG VulkanSurface::AddRef()
{
    return owner_ ? owner_->AddRef() : Counted<Surface>::AddRef();
}

ULONG VulkanSurface::Release()
{
    return owner_ ? owner_->Release() : Counted<Surface>::Release();
}

Texture* VulkanSurface::Container()
{
    return owner_;
}

VulkanSurface::VulkanSurface(std::shared_ptr<Image> image) : image_(std::move(image)) {}

VulkanSurface::VulkanSurface(D3DFORMAT f, uint32_t w, uint32_t h)
{
    uint32_t pitch, rows;
    LevelLayout(f, w, h, &pitch, &rows);
    cpu.assign(size_t(pitch) * rows, 0);
    cpuFormat = f, cpuWidth = w, cpuHeight = h;
}

VulkanSurface::~VulkanSurface() = default;

Image* VulkanSurface::GetImage() const
{
    if (owner_)
        return owner_->image.get();
    return image_.get();
}

HRESULT VulkanSurface::GetDesc(D3DSURFACE_DESC* d)
{
    *d = {};
    if (Image* img = GetImage())
    {
        d->Format = img->d3dFormat;
        d->Type = D3DRTYPE_SURFACE;
        d->Usage = img->renderTarget ? (img->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? D3DUSAGE_DEPTHSTENCIL
                                                                                 : D3DUSAGE_RENDERTARGET)
                                     : 0;
        d->Pool = owner_ ? owner_->pool : D3DPOOL_DEFAULT;
        d->Width = img->width >> level ? img->width >> level : 1;
        d->Height = img->height >> level ? img->height >> level : 1;
        return D3D_OK;
    }
    d->Format = cpuFormat;
    d->Type = D3DRTYPE_SURFACE;
    d->Pool = D3DPOOL_SYSTEMMEM;
    d->Width = cpuWidth;
    d->Height = cpuHeight;
    return D3D_OK;
}

HRESULT VulkanSurface::LockRect(D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags)
{
    uint8_t* base;
    uint32_t pitch, rows;
    D3DFORMAT f;
    if (owner_)
    {
        f = owner_->image->d3dFormat;
        uint32_t w = owner_->image->width >> level ? owner_->image->width >> level : 1;
        uint32_t h = owner_->image->height >> level ? owner_->image->height >> level : 1;
        LevelLayout(f, w, h, &pitch, &rows);
        base = owner_->Shadow(level, face).data();
    }
    else if (!cpu.empty())
    {
        f = cpuFormat;
        LevelLayout(f, cpuWidth, cpuHeight, &pitch, &rows);
        base = cpu.data();
    }
    else
    {
        // Render target / back buffer read-back: not used by the game's measured paths (CaptureBackBuffer goes
        // through StretchRect into a texture). Logged so a new path shows up.
        Log("vulkan: LockRect on a render target is not supported");
        return D3DERR_INVALIDCALL;
    }
    FormatInfo fi = Format(f);
    uint32_t offset = 0;
    if (rect)
    {
        uint32_t bx = fi.compressed ? rect->left / 4 : rect->left;
        uint32_t by = fi.compressed ? rect->top / 4 : rect->top;
        offset = by * pitch + bx * (fi.blockBytes ? fi.blockBytes : 4);
    }
    locked->pBits = base + offset;
    locked->Pitch = static_cast<INT>(pitch);
    lockedReadOnly_ = (flags & D3DLOCK_READONLY) != 0;
    return D3D_OK;
}

HRESULT VulkanSurface::UnlockRect()
{
    if (owner_ && !lockedReadOnly_)
        owner_->Upload(level, face);
    return D3D_OK;
}

// --- Texture ---------------------------------------------------------------------------------------------------

VulkanTexture::VulkanTexture(D3DFORMAT f, uint32_t w, uint32_t h, uint32_t lv, uint32_t fc, DWORD u, D3DPOOL p)
    : image(std::make_shared<Image>()), usage(u), pool(p)
{
    bool depth = (u & D3DUSAGE_DEPTHSTENCIL) != 0;
    ok = image->Create(f, w, h, lv, fc, (u & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) != 0, depth);
    if (ok)
        shadow.resize(size_t(image->levels) * fc);
    if (ok)
    {
        // New contents are zero, as the D3D9 driver hands them out: a render target (the dynamic environment cube)
        // can be sampled before every face is drawn, and Vulkan memory is undefined (it varied per run:
        // tools/compare_backends.py --reference vulkan, lev_1_m1 frame 200). Compressed images are always uploaded
        // whole and cannot be cleared.
        std::shared_ptr<Image> img = image;
        bool compressed = Format(f).compressed;
        RecordTransfer([img, compressed, depth](VkCommandBuffer cb) {
            if (!compressed)
            {
                img->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkImageSubresourceRange r{img->aspect, 0, img->levels, 0, img->faces};
                if (depth)
                {
                    VkClearDepthStencilValue z{1.0f, 0};
                    vkCmdClearDepthStencilImage(cb, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &z, 1, &r);
                }
                else
                {
                    VkClearColorValue zero{};
                    vkCmdClearColorImage(cb, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &r);
                }
            }
            img->Transition(cb, depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
    }
}

VulkanTexture::~VulkanTexture()
{
    for (VulkanSurface* s : surfaces)
        delete s;
    image->Destroy();
}

std::vector<uint8_t>& VulkanTexture::Shadow(uint32_t level, uint32_t face)
{
    auto& s = shadow[size_t(level) * image->faces + face];
    if (s.empty())
    {
        uint32_t w = image->width >> level ? image->width >> level : 1;
        uint32_t h = image->height >> level ? image->height >> level : 1;
        uint32_t pitch, rows;
        LevelLayout(image->d3dFormat, w, h, &pitch, &rows);
        s.assign(size_t(pitch) * rows, 0);
    }
    return s;
}

ResourceCounters& Counters()
{
    static ResourceCounters c;
    return c;
}

void VulkanTexture::Upload(uint32_t level, uint32_t face)
{
    struct Timed
    {
        LARGE_INTEGER t0;
        Timed() { QueryPerformanceCounter(&t0); }
        ~Timed()
        {
            LARGE_INTEGER t1;
            QueryPerformanceCounter(&t1);
            Counters().uploadTicks += t1.QuadPart - t0.QuadPart;
            ++Counters().uploads;
        }
    } timed;
    Context& c = Ctx();
    std::vector<uint8_t>& s = Shadow(level, face);
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = s.size();
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging;
    vkCreateBuffer(c.device, &bi, nullptr, &staging);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, staging, &req);
    VkDeviceMemory mem = c.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(c.device, staging, mem, 0);
    void* p;
    vkMapMemory(c.device, mem, 0, s.size(), 0, &p);
    std::memcpy(p, s.data(), s.size());
    vkUnmapMemory(c.device, mem);
    uint32_t w = image->width >> level ? image->width >> level : 1;
    uint32_t h = image->height >> level ? image->height >> level : 1;
    std::shared_ptr<Image> img = image;
    RecordTransfer([img, staging, w, h, level, face](VkCommandBuffer cb) {
        img->Transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, face, 1};
        region.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(cb, staging, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        img->Transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    VkDevice d = c.device;
    DeferDestroy([d, staging, mem] {
        vkDestroyBuffer(d, staging, nullptr);
        vkFreeMemory(d, mem, nullptr);
    });
}

HRESULT VulkanTexture::GetLevelDesc(UINT level, D3DSURFACE_DESC* d)
{
    if (level >= image->levels)
        return D3DERR_INVALIDCALL;
    *d = {};
    d->Format = image->d3dFormat;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = usage;
    d->Pool = pool;
    d->Width = image->width >> level ? image->width >> level : 1;
    d->Height = image->height >> level ? image->height >> level : 1;
    return D3D_OK;
}

HRESULT VulkanTexture::GetSurfaceLevel(UINT level, Surface** out)
{
    if (level >= image->levels)
        return *out = nullptr, D3DERR_INVALIDCALL;
    if (surfaces.empty())
        surfaces.assign(size_t(image->levels) * image->faces, nullptr);
    VulkanSurface*& s = surfaces[size_t(level) * image->faces];
    if (!s)
        s = new VulkanSurface(this, level, 0);
    AddRef();  // the caller's reference, on the texture
    *out = s;
    return D3D_OK;
}

HRESULT VulkanTexture::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, Surface** out)
{
    if (level >= image->levels || image->faces != 6)
        return *out = nullptr, D3DERR_INVALIDCALL;
    if (surfaces.empty())
        surfaces.assign(size_t(image->levels) * image->faces, nullptr);
    VulkanSurface*& s = surfaces[size_t(level) * image->faces + face];
    if (!s)
        s = new VulkanSurface(this, level, static_cast<uint32_t>(face));
    AddRef();
    *out = s;
    return D3D_OK;
}

// --- Buffer ----------------------------------------------------------------------------------------------------

VulkanBuffer::VulkanBuffer(uint32_t len, DWORD, bool idx, D3DFORMAT indexFormat) : length(len), index(idx)
{
    indexType = indexFormat == D3DFMT_INDEX32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    ok = AddVersion();
}

bool VulkanBuffer::AddVersion()
{
    ++Counters().bufferVersions;
    Context& c = Ctx();
    Version v;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = length ? length : 4;
    bi.usage = index ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(c.device, &bi, nullptr, &v.buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, v.buffer, &req);
    // Host-cached when the device has it (renames and tessellation read the contents back; uncached or BAR memory
    // is very slow to read), else any host-visible coherent memory.
    const VkMemoryPropertyFlags cached =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    v.memory = c.MemoryType(req.memoryTypeBits, cached) != UINT32_MAX
                   ? c.Allocate(req, cached)
                   : c.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(c.device, v.buffer, v.memory, 0);
    vkMapMemory(c.device, v.memory, 0, bi.size, 0, reinterpret_cast<void**>(&v.mapped));
    versions_.push_back(v);
    current_ = versions_.size() - 1;
    return true;
}

VulkanBuffer::~VulkanBuffer()
{
    VkDevice d = Ctx().device;
    std::vector<Version> vs = versions_;
    DeferDestroy([d, vs] {
        for (const Version& v : vs)
        {
            vkDestroyBuffer(d, v.buffer, nullptr);
            vkFreeMemory(d, v.memory, nullptr);
        }
    });
}

HRESULT VulkanBuffer::Lock(UINT offset, UINT size, void** data, DWORD flags)
{
    std::lock_guard<std::recursive_mutex> g(Ctx().lock);
    bool inUse = versions_[current_].lastUse > CompletedSerial();
    if (inUse && !(flags & D3DLOCK_NOOVERWRITE))
    {
        // Rename: a version the GPU has finished with, or a new one. Without DISCARD the contents must survive.
        size_t found = SIZE_MAX;
        for (size_t i = 0; i < versions_.size(); ++i)
            if (versions_[i].lastUse <= CompletedSerial())
                found = i;
        size_t old = current_;
        if (found == SIZE_MAX)
            AddVersion();
        else
            current_ = found;
        // Without DISCARD the contents must survive. The versions live in host-cached memory (AddVersion), so this
        // read is an ordinary memory copy.
        if (!(flags & D3DLOCK_DISCARD))
            std::memcpy(versions_[current_].mapped, versions_[old].mapped, length);
    }
    // The game writes the mapped version directly: its vertex batches lock the whole buffer for a few vertices at a
    // time (VertexBufferSet_Begin), so anything that copies the locked range on unlock costs the whole buffer per
    // batch (measured 2026-10-04: 60% of the frame in memcpy with a CPU copy).
    *data = versions_[current_].mapped + offset;
    (void)size;
    return D3D_OK;
}

// --- Layouts and shaders ---------------------------------------------------------------------------------------

VulkanVertexLayout::VulkanVertexLayout(const D3DVERTEXELEMENT9* e)
{
    for (;; ++e)
    {
        elements.push_back(*e);
        if (e->Stream == 0xff)
            break;
    }
    hash = Hash(elements.data(), elements.size() * sizeof(D3DVERTEXELEMENT9));
}

VkShaderModule MakeModule(const std::vector<uint32_t>& spirv)
{
    if (spirv.empty())
        return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = spirv.size() * 4;
    ci.pCode = spirv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(Ctx().device, &ci, nullptr, &m);
    return m;
}

extern float g_terrainDetail, g_terrainDetailStrength;  // vk_shader_translate.cpp

VulkanShader::VulkanShader(const DWORD* code, bool px) : pixel(px)
{
    // [render] terrain_detail (vk_shader_translate.cpp): read once, before the first terrain shader.
    static const bool detailRead = [] {
        g_terrainDetail = ConfigFloatDefault("render", "terrain_detail", 0.0f, 8.0f);
        g_terrainDetailStrength = ConfigFloat("render", "terrain_detail_strength", 0.6f);
        return true;
    }();
    (void)detailRead;
    translated = TranslateShader(reinterpret_cast<const uint32_t*>(code));
    if (!translated.ok)
        Log("vulkan: shader translation failed: %s", translated.error.c_str());
    else if (!pixel)
        module = MakeModule(CompileGlsl(translated.glsl, false));
}

VulkanShader::~VulkanShader()
{
    VkDevice d = Ctx().device;
    VkShaderModule m = module;
    auto vs = variants;
    DeferDestroy([d, m, vs] {
        if (m)
            vkDestroyShaderModule(d, m, nullptr);
        for (auto& v : vs)
            vkDestroyShaderModule(d, v.second, nullptr);
    });
}

VkShaderModule VulkanShader::CaptureVariant()
{
    if (!translated.ok || pixel)
        return VK_NULL_HANDLE;
    const uint64_t h = Hash(kCaptureDefine, std::strlen(kCaptureDefine));
    for (auto& v : variants)
        if (v.first == h)
            return v.second;
    VkShaderModule m = MakeModule(CompileGlsl(WithDefine(translated.glsl, kCaptureDefine), false));
    variants.push_back({h, m});
    return m;
}

VkShaderModule VulkanShader::TessVariant()
{
    if (!translated.ok || pixel)
        return VK_NULL_HANDLE;
    const uint64_t h = Hash(kTessDefine, std::strlen(kTessDefine));
    for (auto& v : variants)
        if (v.first == h)
            return v.second;
    VkShaderModule m = MakeModule(CompileGlsl(WithDefine(translated.glsl, kTessDefine), false));
    variants.push_back({h, m});
    return m;
}


VkShaderModule VulkanShader::PixelVariant(const FFKey& key)
{
    if (!translated.ok)
        return VK_NULL_HANDLE;
    std::string define = FinalPixelDefine(key);
    uint64_t h = Hash(define.data(), define.size());
    for (auto& v : variants)
        if (v.first == h)
            return v.second;
    VkShaderModule m = MakeModule(CompileGlsl(WithDefine(translated.glsl, define), true));
    variants.push_back({h, m});
    return m;
}
}  // namespace rhi::vulkan

namespace rhi::vulkan
{
HRESULT VulkanTexture::LockRect(UINT face, UINT level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags)
{
    if (level >= image->levels || face >= image->faces)
        return D3DERR_INVALIDCALL;
    uint32_t w = image->width >> level ? image->width >> level : 1;
    uint32_t h = image->height >> level ? image->height >> level : 1;
    uint32_t pitch, rows;
    LevelLayout(image->d3dFormat, w, h, &pitch, &rows);
    FormatInfo fi = Format(image->d3dFormat);
    uint32_t offset = 0;
    if (rect)
    {
        uint32_t bx = fi.compressed ? rect->left / 4 : rect->left;
        uint32_t by = fi.compressed ? rect->top / 4 : rect->top;
        offset = by * pitch + bx * (fi.blockBytes ? fi.blockBytes : 4);
    }
    locked->pBits = Shadow(level, face).data() + offset;
    locked->Pitch = static_cast<INT>(pitch);
    lockedReadOnly_ = (flags & D3DLOCK_READONLY) != 0;
    // D3DLOCK_DISCARD on a managed texture (D3D9 refuses it, so the game never does): a write-once upload from the
    // enhancements (hook/enhance/overrides.cpp). The CPU copy is freed after the upload: 218 override textures of up
    // to 2048x2048 kept ~1-2 GB in the 32-bit process and it ran out of address space after two minutes of play.
    lockedDiscard_ = (flags & D3DLOCK_DISCARD) != 0 && pool == D3DPOOL_MANAGED;
    return D3D_OK;
}

HRESULT VulkanTexture::UnlockRect(UINT face, UINT level)
{
    if (!lockedReadOnly_)
        Upload(level, face);
    if (lockedDiscard_)
    {
        std::vector<uint8_t>().swap(shadow[size_t(level) * image->faces + face]);
        lockedDiscard_ = false;
    }
    return D3D_OK;
}
}  // namespace rhi::vulkan
