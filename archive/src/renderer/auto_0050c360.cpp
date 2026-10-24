// Recovered: renderer core, Direct3D 9 layer (renderer virtuals 15, 39, 40, 46, 84). S2, Approach B.
//   0x0050c360 Renderer_CreateTextureFromImages
//   0x0050c4a0 Renderer_SetMaxTextures
//   0x0050c500 Renderer_GetMaxTextures
//   0x0050c990 Renderer_ComputeMaterialRenderFlags
//   0x0050cba0 Renderer_UpdateWindowedSize
// None of these has a direct caller; all five are slots of the renderer vtable 0x00589990 (tools\vtable.py).
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

uint32_t Renderer_CreateTexture(Renderer* r, uint8_t* tex, uint32_t width, uint32_t height, uint32_t maxLevels,
                                uint32_t format);                                         // auto_00506210.cpp
uint32_t TextureImage_LoadIntoSurface(uint8_t* image, uint32_t unused, IDirect3DSurface9* surface);  // textures.cpp
uint32_t Renderer_ReleaseDevice(Renderer* r);                                             // auto_00508210.cpp
int32_t Renderer_RestoreDevice(Renderer* r);                                              // auto_00508210.cpp

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
}  // namespace

// 0x0050c360 (ecx = renderer; [texture record, image chain, unused], callee pops 12): renderer virtual 84 (+0x150),
// which Renderer_RegisterTexture (0x0050bb30) calls for records with word +0x22 bit 3 as (record, data, 0). The
// data is an image count (+0) followed by image records in the TextureImage_LoadIntoSurface layout (word +0
// width, word +2 height, +4 format, +0xc pixel bytes, pixels at +0x10; the next image follows the pixels).
// Unless the record is already created (+0x23 bit 7) it is created with Renderer_CreateTexture (0x00509eb0) from
// the first image's size and format, with the image count as max levels. Then for every level of the D3D texture
// (bucket +0x0c: base texture GetLevelCount, +0x10: GetLevelDesc / GetSurfaceLevel) it advances along the chain
// while the image is smaller than the level in either dimension and more than one image remains (the position
// carries over between levels), uploads that image into the level surface (0x0050c1b0, with the renderer as its
// unused argument) and releases the surface. Returns AL = 1, or 0 without a bucket or texture.
// (not executed in the comparison scenarios)
uint32_t Renderer_CreateTextureFromImages(Renderer* r, uint8_t* tex, const uint8_t* data, uint32_t /*unused*/)
{
    uint32_t remaining = Field<uint32_t>(data, 0);
    uint8_t* image = const_cast<uint8_t*>(data) + 4;
    uint32_t size = Field<uint32_t>(image, 0);  // width | height << 16
    if (!(tex[0x23] & 0x80))
        Renderer_CreateTexture(r, tex, size & 0xffff, size >> 16, remaining, Field<uint32_t>(image, 4));
    uint8_t* bucket = Field<uint8_t*>(tex, 0xc);
    if (!bucket || !Field<IDirect3DTexture9*>(bucket, 0x10))
        return 0;
    uint32_t levels = Field<IDirect3DBaseTexture9*>(bucket, 0xc)->GetLevelCount();
    for (uint32_t level = 0; level < levels; level++)
    {
        D3DSURFACE_DESC desc;
        if (Field<IDirect3DTexture9*>(bucket, 0x10)->GetLevelDesc(level, &desc) != 0)
            continue;
        while ((desc.Width < (size & 0xffff) || desc.Height < (size >> 16)) && remaining != 1)
        {
            image += 0x10 + Field<uint32_t>(image, 0xc);
            size = Field<uint32_t>(image, 0);
            remaining--;
        }
        IDirect3DSurface9* surface;
        if (Field<IDirect3DTexture9*>(bucket, 0x10)->GetSurfaceLevel(level, &surface) != 0)
            continue;
        TextureImage_LoadIntoSurface(image, u(r), surface);
        surface->Release();
    }
    return (levels & 0xffffff00) | 1;
}

static uint32_t Renderer_CreateTextureFromImages_Abi(uint32_t ecx, uint32_t tex, uint32_t data, uint32_t unused)
{
    return Renderer_CreateTextureFromImages(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(tex),
                                            reinterpret_cast<const uint8_t*>(data), unused);
}
ABI_ENTRY_0050c360(Renderer_CreateTextureFromImages_Entry, Renderer_CreateTextureFromImages_Abi)
HOOK(0x0050c360, "Renderer_CreateTextureFromImages", Renderer_CreateTextureFromImages_Entry, nullptr);

// 0x0050c4a0 (ecx = renderer; [count], callee pops 4): renderer virtual 39. Clamp the count to 1..3 (unsigned)
// and store it as the configured texture limit +0x3b8 (default 3, set in 0x004fbe00); lower the active limit
// +0x3b4 (which Renderer_BindMaterialDeclaration 0x004fc7a0 applies) to it, and the texture count in bits 2-3 of
// the render flags +0x3a4 to that. (not executed in the comparison scenarios)
void Renderer_SetMaxTextures(Renderer* r, uint32_t count)
{
    if (count <= 1)
        count = 1;
    else if (count >= 3)
        count = 3;
    Field<uint32_t>(r, 0x3b8) = count;
    uint32_t active = Field<uint32_t>(r, 0x3b4);
    if (active >= count)
        active = count;
    uint32_t textures = (Field<uint32_t>(r, 0x3a4) >> 2) & 3;
    Field<uint32_t>(r, 0x3b4) = active;
    if (textures >= active)
        textures = active;
    Field<uint32_t>(r, 0x3a4) = (Field<uint32_t>(r, 0x3a4) & 0xfffffff3) | (textures << 2);
}

static uint32_t Renderer_SetMaxTextures_Abi(uint32_t ecx, uint32_t count)
{
    Renderer_SetMaxTextures(reinterpret_cast<Renderer*>(ecx), count);
    return (Field<uint32_t>(reinterpret_cast<Renderer*>(ecx), 0x3a4) >> 2 & 3) << 2;  // EAX as the original leaves it
}
ABI_ENTRY_0050c4a0(Renderer_SetMaxTextures_Entry, Renderer_SetMaxTextures_Abi)
HOOK(0x0050c4a0, "Renderer_SetMaxTextures", Renderer_SetMaxTextures_Entry, nullptr);

// 0x0050c500 (ecx = renderer): renderer virtual 40, the getter of 0x0050c4a0: returns +0x3b8.
// (not executed in the comparison scenarios)
uint32_t Renderer_GetMaxTextures(Renderer* r)
{
    return Field<uint32_t>(r, 0x3b8);
}

static uint32_t Renderer_GetMaxTextures_Abi(uint32_t ecx)
{
    return Renderer_GetMaxTextures(reinterpret_cast<Renderer*>(ecx));
}
ABI_ENTRY_0050c500(Renderer_GetMaxTextures_Entry, Renderer_GetMaxTextures_Abi)
HOOK(0x0050c500, "Renderer_GetMaxTextures", Renderer_GetMaxTextures_Entry, nullptr);

// 0x0050c990 (ecx = renderer; [material, state or null, count, texcoord indices or null], callee pops 16):
// renderer virtual 15. Builds a render-flags word in the layout of +0x3a4 (the vertex shader key used by
// Renderer_SelectVertexShader 0x0050ca70) without touching the renderer: bits 2+ = material texture count (byte
// +0x10, not clamped); bit 23 unless material flag (+8) bit 4; bit 24 = flag bit 6. With a state word: bit 20 =
// its bit 19, bit 22 = its bit 1, and when its bit 0 is set the count is ORed in (-1 = the renderer's +0x1a4c).
// With indices: four 3-bit fields at bits 4, 8, 12, 16 = index + 1 (ORed unmasked, in this order).
// (not executed in the comparison scenarios)
uint32_t Renderer_ComputeMaterialRenderFlags(Renderer* r, const uint8_t* material, const uint32_t* state,
                                             uint32_t count, const int32_t* texcoords)
{
    uint32_t flags = static_cast<uint32_t>(material[0x10]) << 2;
    uint32_t materialFlags = Field<uint32_t>(material, 8);
    flags = (materialFlags >> 4) & 1 ? flags & 0xff7fffff : flags | 0x800000;
    flags = (materialFlags >> 6) & 1 ? flags | 0x1000000 : flags & 0xfeffffff;
    if (state)
    {
        if (count == 0xffffffff)
            count = Field<uint32_t>(r, 0x1a4c);
        uint32_t s = *state;
        flags = (s >> 19) & 1 ? flags | 0x100000 : flags & 0xffefffff;
        flags = (s >> 1) & 1 ? flags | 0x400000 : flags & 0xffbfffff;
        flags = (flags & 0xfffffffc) | ((s & 1) ? count : 0);
    }
    if (texcoords)
    {
        flags = (static_cast<uint32_t>(texcoords[0] + 1) << 4) | (flags & 0xffffff8f);
        flags = (flags & 0xfffff8ff) | (static_cast<uint32_t>(texcoords[1] + 1) << 8);
        flags = (flags & 0xffff8fff) | (static_cast<uint32_t>(texcoords[2] + 1) << 12);
        flags = (flags & 0xfff8ffff) | (static_cast<uint32_t>(texcoords[3] + 1) << 16);
    }
    return flags;
}

static uint32_t Renderer_ComputeMaterialRenderFlags_Abi(uint32_t ecx, uint32_t material, uint32_t state,
                                                        uint32_t count, uint32_t texcoords)
{
    return Renderer_ComputeMaterialRenderFlags(reinterpret_cast<Renderer*>(ecx),
                                               reinterpret_cast<const uint8_t*>(material),
                                               reinterpret_cast<const uint32_t*>(state), count,
                                               reinterpret_cast<const int32_t*>(texcoords));
}
ABI_ENTRY_0050c990(Renderer_ComputeMaterialRenderFlags_Entry, Renderer_ComputeMaterialRenderFlags_Abi)
HOOK(0x0050c990, "Renderer_ComputeMaterialRenderFlags", Renderer_ComputeMaterialRenderFlags_Entry, nullptr);

// 0x0050cba0 (ecx = renderer): renderer virtual 46. Only when the renderer is ready (+0x1628 bit 0) and windowed
// (bit 5): re-read the device window's rectangles (+0x1484; GetWindowRect into +0x9c0, GetClientRect into +0x9d0,
// through the import table 0x0056b254 / 0x0056b250). If the client size changed, store it as the back buffer size
// (present parameters +0x1490/+0x1494), clear the ready bit and release the device (0x00509920). Unless that
// failed (negative): with a zero size nothing more happens; otherwise width/height/depth default to 800/600/32,
// are stored in +0x14d0..+0x14d8 and +0x1634..+0x163c, +0x14dc bit 0 is set and the device is restored
// (0x005099b0). The ready bit is set again. Returns AL = 0 only when the device was restored; the upper bytes
// are the last value in EAX (the restore or release result, the new client width, or the entry EAX, here 0).
// (not executed in the comparison scenarios)
uint32_t Renderer_UpdateWindowedSize(Renderer* r)
{
    uint8_t& state = Field<uint8_t>(r, 0x1628);
    if (!(state & 1) || !(state & 0x20))
        return 1;
    RECT old = Field<RECT>(r, 0x9d0);
    auto getWindowRect = *reinterpret_cast<BOOL(WINAPI**)(HWND, LPRECT)>(0x0056b254);
    auto getClientRect = *reinterpret_cast<BOOL(WINAPI**)(HWND, LPRECT)>(0x0056b250);
    getWindowRect(r->deviceWindow, &Field<RECT>(r, 0x9c0));
    getClientRect(r->deviceWindow, &Field<RECT>(r, 0x9d0));
    const RECT& client = Field<RECT>(r, 0x9d0);
    int32_t width = client.right - client.left;
    if (old.right - old.left == width && old.bottom - old.top == client.bottom - client.top)
        return (static_cast<uint32_t>(width) & 0xffffff00) | 1;

    uint8_t ok = 1;
    Field<int32_t>(r, 0x1494) = client.bottom - client.top;
    Field<int32_t>(r, 0x1490) = width;
    state &= 0xfe;
    uint32_t eax = Renderer_ReleaseDevice(r);
    if (static_cast<int32_t>(eax) >= 0)
    {
        int32_t w = Field<int32_t>(r, 0x1490);
        int32_t depth = Field<int32_t>(r, 0x163c);
        int32_t h = Field<int32_t>(r, 0x1494);
        eax = static_cast<uint32_t>(h);
        if (w == 0 && h == 0)
        {
            ok = 1;
        }
        else
        {
            if (w == 0)
                w = 800;
            if (h == 0)
                h = 600;
            if (depth == 0)
                depth = 32;
            Field<int32_t>(r, 0x14d4) = h;
            Field<int32_t>(r, 0x1638) = h;
            Field<int32_t>(r, 0x14d0) = w;
            Field<int32_t>(r, 0x1634) = w;
            Field<int32_t>(r, 0x14d8) = depth;
            Field<int32_t>(r, 0x163c) = depth;
            Field<uint8_t>(r, 0x14dc) |= 1;
            ok = 0;
            eax = static_cast<uint32_t>(Renderer_RestoreDevice(r));
        }
    }
    state |= 1;
    return (eax & 0xffffff00) | ok;
}

static uint32_t Renderer_UpdateWindowedSize_Abi(uint32_t ecx)
{
    return Renderer_UpdateWindowedSize(reinterpret_cast<Renderer*>(ecx));
}
ABI_ENTRY_0050cba0(Renderer_UpdateWindowedSize_Entry, Renderer_UpdateWindowedSize_Abi)
HOOK(0x0050cba0, "Renderer_UpdateWindowedSize", Renderer_UpdateWindowedSize_Entry, nullptr);
