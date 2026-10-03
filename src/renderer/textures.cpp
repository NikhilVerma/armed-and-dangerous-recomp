// Recovered: texture helpers (gfx\d3d\Tex.cpp area). S2, Approach B.
//   0x00509e80 Texture_ComputeSize
//   0x0050b330 TextureBuckets_Acquire
//   0x0050c1b0 TextureImage_LoadIntoSurface
//   0x0050b2b0 Renderer_ChooseTextureFormat
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
const char* const kStrNotEnoughBuckets = reinterpret_cast<const char*>(0x005893c0);
const char* const kStrD3DXLoadSurfaceFromMemory = reinterpret_cast<const char*>(0x005893f8);
const char* const kStrDXError = reinterpret_cast<const char*>(0x0058b33c);  // "Error %s in DX function %s\n"
}  // namespace

// 0x00509e80 (eax = bits per pixel, ecx = extra mip levels; [width, height, faces]): bytes for a texture
// with ecx + 1 levels, each a quarter of the previous (signed division), times the face count.
int32_t Texture_ComputeSize(int32_t bitsPerPixel, int32_t extraLevels, int32_t width, int32_t height, int32_t faces)
{
    int32_t level = (bitsPerPixel >> 3) * width * height;
    int32_t total = 0;
    int32_t remaining = extraLevels;
    do
    {
        total += level;
        level /= 4;
    } while (remaining-- != 0);
    return total * faces;
}

// 0x0050b330 (edi = texture manager): move the first free bucket (list at +0x48c) to the used list (+0x494).
// Logs and returns null when none is free.
ListNode* TextureBuckets_Acquire(void* manager)
{
    ListNode* freeList = &Field<ListNode>(manager, 0x48c);
    ListNode* used = &Field<ListNode>(manager, 0x494);
    ListNode* node = freeList->next == freeList ? nullptr : freeList->next;
    if (!node)
    {
        GameLog(kStrNotEnoughBuckets);
        return nullptr;
    }
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = used;
    node->next = used->next;
    used->next = node;
    node->next->prev = node;
    return node;
}

// 0x0050c1b0 (esi = image; [unused, destination surface]): copy the image's pixels (+0x10, format +0x04,
// pitch word +0x0a, size words +0x00/+0x02) into the surface with D3DXLoadSurfaceFromMemory (statically linked
// at 0x00545c10), linear filter. Logs failures. Returns the image's field at +0x0c.
uint32_t TextureImage_LoadIntoSurface(uint8_t* image, uint32_t /*unused*/, IDirect3DSurface9* surface)
{
    RECT source = {0, 0, *reinterpret_cast<uint16_t*>(image), *reinterpret_cast<uint16_t*>(image + 2)};
    uint32_t hr = orig::fn_00545c10(u(surface), 0, 0, u(image + 0x10), Field<uint32_t>(image, 4),
                                    *reinterpret_cast<uint16_t*>(image + 0xa), 0, u(&source),
                                    3 /* D3DX_FILTER_LINEAR */, 0);
    if (hr != 0)
    {
        auto name = reinterpret_cast<const char*>(orig::fn_004e0d20(hr));
        GameLog(kStrDXError, name, kStrD3DXLoadSurfaceFromMemory);
    }
    return Field<uint32_t>(image, 0xc);
}

// 0x0050b2b0 (eax = texture class, edi = renderer; [flags]): the back buffer's format (A8R8G8B8 if it cannot
// be read), unless flags has 0x2000 clear and the renderer's per-class format table (+0x14fc, 16-byte entries;
// classes 6-9 share entry 5) has a format for this class.
D3DFORMAT Renderer_ChooseTextureFormat(int32_t textureClass, Renderer* r, uint32_t flags)
{
    IDirect3DSurface9* back = nullptr;
    D3DSURFACE_DESC desc;
    D3DFORMAT format;
    if (r->device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back) == 0)
    {
        back->GetDesc(&desc);
        back->Release();
        format = desc.Format;
    }
    else
    {
        format = D3DFMT_A8R8G8B8;
    }
    if (!(flags & 0x2000))
    {
        if (textureClass >= 6 && textureClass <= 9)
            textureClass = 5;
        D3DFORMAT preferred = Field<D3DFORMAT>(r, 0x14fc + textureClass * 16);
        if (preferred)
            format = preferred;
    }
    return format;
}

// 0x00508750 (eax = format): bits per pixel of a D3D format (0 if unknown). EDX is preserved: callers keep
// a value in it across the call.
uint32_t Format_BitsPerPixel(uint32_t format)
{
    switch (format)
    {
    case 0x14: return 24;  // R8G8B8
    case 0x15: case 0x16: case 0x3e: case 0x3f: case 0x40: case 0x47:  // 32-bit colour and bump formats
    case 0x4b: case 0x4d: case 0x4f: case 0x66:                         // D24S8, D24X8, D24X4S4, INDEX32
        return 32;
    case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1d: case 0x1e: case 0x28: case 0x33: case 0x3c:
    case 0x3d: case 0x46: case 0x49: case 0x50: case 0x65:              // 16-bit colour, bump, D16, INDEX16
        return 16;
    case 0x1b: case 0x1c: case 0x29: case 0x32: case 0x34:              // 8-bit
        return 8;
    case MAKEFOURCC('D', 'X', 'T', '1'): case MAKEFOURCC('D', 'X', 'T', '2'): case MAKEFOURCC('D', 'X', 'T', '3'):
    case MAKEFOURCC('D', 'X', 'T', '4'): case MAKEFOURCC('D', 'X', 'T', '5'): case MAKEFOURCC('Y', 'U', 'Y', '2'):
    case MAKEFOURCC('U', 'Y', 'V', 'Y'):
        return 16;
    default:
        return 0;
    }
}

// 0x0050a510 (ecx = renderer; [bucket or null]): texture LOD. Null: evict managed resources, then update every
// used bucket (+0x494). A bucket's texture (+8, created and not locked: word +0x22 bits 0 and 7 clear) with a
// D3D texture (+0xc) larger than 4 texels in its smaller dimension gets
// LOD = 0 for textures that must stay sharp (+0x23 bit 1), else min(size - 4, renderer max LOD +0x1668,
// levels - 1); SetLOD is called only when it changes. Returns the last value computed (unused by callers).
uint32_t Renderer_UpdateTextureLod(Renderer* r, uint8_t* bucket)
{
    if (!bucket)
    {
        r->device->EvictManagedResources();
        ListNode* used = &Field<ListNode>(r, 0x494);
        for (ListNode* n = used->next; n != used;)
        {
            ListNode* next = n->next;
            Renderer_UpdateTextureLod(r, reinterpret_cast<uint8_t*>(n));
            n = next;
        }
        return 0;
    }
    uint8_t* tex = Field<uint8_t*>(bucket, 8);
    if (!tex || (tex[0x22] & 0x81))
        return reinterpret_cast<uint32_t>(tex);
    auto* d3dTex = Field<IDirect3DBaseTexture9*>(bucket, 0xc);
    if (!d3dTex)
        return reinterpret_cast<uint32_t>(tex);
    int32_t w = Field<int32_t>(tex, 0x10), h = Field<int32_t>(tex, 0x14);
    uint32_t size = static_cast<uint32_t>(w < h ? w : h);
    uint32_t maxLevel = d3dTex->GetLevelCount() - 1;
    if (size <= 4 || maxLevel == 0)
        return maxLevel;
    uint32_t lod;
    if (Field<uint8_t*>(bucket, 8)[0x23] & 2)
    {
        lod = 0;
    }
    else
    {
        lod = size - 4;
        uint32_t rendererMax = Field<uint32_t>(r, 0x1668);
        if (lod >= rendererMax)
            lod = rendererMax;
        if (lod >= maxLevel)
            lod = maxLevel;
    }
    uint32_t current = Field<IDirect3DBaseTexture9*>(bucket, 0xc)->GetLOD();
    if (lod == current)
        return current;
    return Field<IDirect3DBaseTexture9*>(bucket, 0xc)->SetLOD(lod);
}

static uint32_t Format_BitsPerPixel_Abi(uint32_t eax)
{
    return Format_BitsPerPixel(eax);
}
static uint32_t Renderer_UpdateTextureLod_Abi(uint32_t ecx, uint32_t bucket)
{
    return Renderer_UpdateTextureLod(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(bucket));
}
ABI_ENTRY_00508750(Format_BitsPerPixel_Entry, Format_BitsPerPixel_Abi)
ABI_ENTRY_0050a510(Renderer_UpdateTextureLod_Entry, Renderer_UpdateTextureLod_Abi)
HOOK(0x00508750, "Format_BitsPerPixel", Format_BitsPerPixel_Entry, nullptr);
HOOK(0x0050a510, "Renderer_UpdateTextureLod", Renderer_UpdateTextureLod_Entry, nullptr);

// 0x0050b1f0 (ecx = format; [unused]): the renderer's texture format class. 15 for anything unknown.
uint32_t TextureFormat_Class(D3DFORMAT format)
{
    switch (format)
    {
    case D3DFMT_A8R8G8B8: return 0;
    case D3DFMT_X8R8G8B8: return 1;
    case D3DFMT_R8G8B8: return 2;
    case D3DFMT_A1R5G5B5: return 3;
    case D3DFMT_X1R5G5B5: return 4;
    case D3DFMT_R5G6B5: return 5;
    case D3DFMT_DXT1: return 10;
    case D3DFMT_DXT2: return 11;
    case D3DFMT_DXT3: return 12;
    case D3DFMT_DXT4: return 13;
    case D3DFMT_DXT5: return 14;
    default: return 15;
    }
}

// 0x0050be80 (eax = texture, ecx = renderer; [source surface, cube face]): make sure the texture is created
// (word +0x22 bit 15; else the renderer's virtual 73 creates it), then copy the source surface into level 0
// (cube textures: the face from the table at 0x005ca780) with D3DXLoadSurfaceFromSurface (statically linked at
// 0x00545cf8, default filter). Returns true on success.
bool Texture_LoadFromSurface(uint8_t* texture, Renderer* r, IDirect3DSurface9* source, int32_t face)
{
    if (!(Field<uint16_t>(texture, 0x22) & 0x8000))
    {
        VCall<void>(r, 0x124 / 4, texture);
        if (!(Field<uint16_t>(texture, 0x22) & 0x8000))
            return false;
    }
    IDirect3DSurface9* surface;
    HRESULT hr;
    uint8_t* d3d = Field<uint8_t*>(texture, 0xc);
    if (Field<uint16_t>(texture, 0x22) & 0x20)
    {
        auto faces = reinterpret_cast<const D3DCUBEMAP_FACES*>(0x005ca780);
        hr = Field<IDirect3DCubeTexture9*>(d3d, 0x14)->GetCubeMapSurface(faces[face], 0, &surface);
    }
    else
    {
        hr = Field<IDirect3DTexture9*>(d3d, 0x10)->GetSurfaceLevel(0, &surface);
    }
    if (hr != 0)
        return false;
    uint32_t loadResult = orig::fn_00545cf8(u(surface), 0, 0, u(source), 0, 0, 0xffffffff /* D3DX_DEFAULT */, 0);
    if (loadResult != 0)
    {
        auto name = reinterpret_cast<const char*>(orig::fn_004e0d20(loadResult));
        GameLog(kStrDXError, name, reinterpret_cast<const char*>(0x0058933c) /* D3DXLoadSurfaceFromSurface */);
    }
    bool ok = loadResult == 0;
    surface->Release();
    return ok;
}

static uint32_t TextureFormat_Class_Abi(uint32_t ecx, uint32_t /*unused*/)
{
    return TextureFormat_Class(static_cast<D3DFORMAT>(ecx));
}
static uint32_t Texture_LoadFromSurface_Abi(uint32_t eax, uint32_t ecx, uint32_t source, uint32_t face)
{
    return Texture_LoadFromSurface(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<Renderer*>(ecx),
                                   reinterpret_cast<IDirect3DSurface9*>(source), face);
}
ABI_ENTRY_0050b1f0(TextureFormat_Class_Entry, TextureFormat_Class_Abi)
ABI_ENTRY_0050be80(Texture_LoadFromSurface_Entry, Texture_LoadFromSurface_Abi)
HOOK(0x0050b1f0, "TextureFormat_Class", TextureFormat_Class_Entry, nullptr);
HOOK(0x0050be80, "Texture_LoadFromSurface", Texture_LoadFromSurface_Entry, nullptr);

static uint32_t Texture_ComputeSize_Abi(uint32_t eax, uint32_t ecx, uint32_t w, uint32_t h, uint32_t faces)
{
    return Texture_ComputeSize(eax, ecx, w, h, faces);
}
static uint32_t TextureBuckets_Acquire_Abi(uint32_t edi)
{
    return u(TextureBuckets_Acquire(reinterpret_cast<void*>(edi)));
}
static uint32_t TextureImage_LoadIntoSurface_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return TextureImage_LoadIntoSurface(reinterpret_cast<uint8_t*>(esi), a0, reinterpret_cast<IDirect3DSurface9*>(a1));
}
static uint32_t Renderer_ChooseTextureFormat_Abi(uint32_t eax, uint32_t edi, uint32_t flags)
{
    return Renderer_ChooseTextureFormat(eax, reinterpret_cast<Renderer*>(edi), flags);
}
ABI_ENTRY_00509e80(Texture_ComputeSize_Entry, Texture_ComputeSize_Abi)
ABI_ENTRY_0050b330(TextureBuckets_Acquire_Entry, TextureBuckets_Acquire_Abi)
ABI_ENTRY_0050c1b0(TextureImage_LoadIntoSurface_Entry, TextureImage_LoadIntoSurface_Abi)
ABI_ENTRY_0050b2b0(Renderer_ChooseTextureFormat_Entry, Renderer_ChooseTextureFormat_Abi)
HOOK(0x00509e80, "Texture_ComputeSize", Texture_ComputeSize_Entry, nullptr);
HOOK(0x0050b330, "TextureBuckets_Acquire", TextureBuckets_Acquire_Entry, nullptr);
HOOK(0x0050c1b0, "TextureImage_LoadIntoSurface", TextureImage_LoadIntoSurface_Entry, nullptr);
HOOK(0x0050b2b0, "Renderer_ChooseTextureFormat", Renderer_ChooseTextureFormat_Entry, nullptr);
