// Recovered (parallel batch): texture format fallback, texture upload from memory, TGA decoding, vertex shader
// selection. S2, Approach B.
//   0x0050ab40 Renderer_PickSupportedTextureClass
//   0x0050b040 Texture_FillLevelsFromMemory
//   0x0050b390 Tga_Decode
//   0x0050ca70 Renderer_SelectVertexShader
#include <cstring>

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

constexpr uint32_t kHeap = 0x0063a210;  // the game's allocator object (0x004e07f0 alloc, 0x004e08d0 free)
constexpr uint32_t kStrNew = 0x0058b5e0;  // "new[]"
constexpr uint32_t kEFail = 0x80004005;
constexpr uint32_t kEOutOfMemory = 0x8007000e;

const char* const kStrDXError = reinterpret_cast<const char*>(0x0058b33c);  // "Error %s in DX function %s\n"
const char* const kStrD3DXLoadSurfaceFromMemory = reinterpret_cast<const char*>(0x005893f8);
constexpr uint32_t kStrGetDescError = 0x005893dc;  // "error: texsurface->GetDisc"

uint8_t* GameAlloc(uint32_t bytes)
{
    return reinterpret_cast<uint8_t*>(orig::fn_004e07f0(kHeap, 0x10, bytes, 2, kStrNew));
}

uint16_t Word(const uint8_t* p, uint32_t offset)
{
    uint16_t w;
    memcpy(&w, p + offset, 2);
    return w;
}

// Fallback orders of 0x0050ab40, by requested class (0-14); -1 ends a list.
const int8_t kClassFallbacks[15][10] = {
    {0, 3, 4, 14, 12, 10, -1},
    {2, 1, 0, 5, 4, 3, 10, 12, 14, -1},
    {2, 1, 0, 5, 4, 3, 10, 12, 14, -1},
    {3, 4, 0, 12, 14, 10, -1},
    {4, 3, 0, 10, 12, 14, -1},
    {5, 4, 2, 1, 0, 10, 12, 14, -1},
    {6, 0, 3, 4, 14, 12, 10, -1},
    {7, 3, 4, 0, 12, 14, 10, -1},
    {8, 2, 0, 3, 4, 10, 12, 14, -1},  // class 2 present answers 0 (see below)
    {9, 0, 3, 4, 14, 12, 10, -1},
    {10, 12, 14, 4, 3, 0, -1},
    {11, 13, 14, 10, 3, 4, 0, -1},
    {12, 14, 3, 0, 4, 10, -1},
    {13, 11, 3, 0, 4, 10, -1},
    {14, 12, 3, 0, 10, 4, -1},
};
}  // namespace

// 0x0050ab40 (eax = wanted texture class, cl = use second column, edx = renderer): the first class in a fixed
// per-class fallback order whose entry in the renderer's support table (+0x14f4, 16 bytes per class, column
// 0 or 2 by cl; the same table 0x0050b2b0 reads at column 2) is nonzero. 15 when none or the class is unknown.
// Quirk kept: for class 8, a supported class 2 answers 0.
uint32_t Renderer_PickSupportedTextureClass(int32_t wanted, bool secondColumn, Renderer* r)
{
    uint32_t column = secondColumn ? 2 : 0;
    if (wanted < 0 || wanted > 14)
        return 15;
    for (const int8_t* c = kClassFallbacks[wanted]; *c >= 0; ++c)
    {
        if (Field<int32_t>(r, 0x14f4 + *c * 16 + column * 4) != 0)
            return (wanted == 8 && *c == 2) ? 0 : static_cast<uint32_t>(*c);
    }
    return 15;
}

// 0x0050b040 (eax = source; [renderer, texture slot]): upload system-memory pixels into every needed level of
// the slot's texture (+0x10) or cube texture (+0x14, face from 0x005ca780) with D3DXLoadSurfaceFromMemory
// (0x00545c10, default filter). With texture word +0x22 bit 0x800 the source is an image record (pitch +0x0c,
// format class +0x14, pixels +0x1c); otherwise it is the pixels, pitch = (1 << width log +0x10) * class bytes
// (0x005cab60) negated for flag 4, class +0x18. Level count from renderer virtuals 0x130/0x12c when flag 0x100
// and caps +0x15fd bit 0x10, else 1. Returns the last surface Release() (0 if a surface is missing).
uint32_t Texture_FillLevelsFromMemory(const uint8_t* source, Renderer* r, uint8_t* slot)
{
    uint8_t* tex = Field<uint8_t*>(slot, 8);
    uint16_t flags = Field<uint16_t>(tex, 0x22);
    int32_t pitch;
    uint32_t pixels;
    uint32_t formatClass;
    if (flags & 0x800)
    {
        pitch = Field<int32_t>(source, 0xc);
        formatClass = Field<uint32_t>(source, 0x14);
        pixels = Field<uint32_t>(source, 0x1c);
    }
    else
    {
        int32_t sizeLog = Field<int32_t>(tex, 0x10);
        int32_t texels = (sizeLog < 0 || static_cast<uint32_t>(sizeLog) >= 0x11)
                             ? 0
                             : GameGlobal<int32_t>(0x005caaa0 + sizeLog * 4);
        formatClass = Field<uint32_t>(tex, 0x18);
        pixels = u(source);
        pitch = GameGlobal<int32_t>(0x005cab60 + formatClass * 4) * texels;
        if (flags & 4)
            pitch = -pitch;
    }

    int32_t levels;
    if ((flags & 0x100) && (Field<uint8_t>(r, 0x15fd) & 0x10))
    {
        uint32_t first = 0;
        uint32_t second = u(r);  // the original passes its own argument slot as the second output
        VCall<void>(r, 0x130 / 4, tex, &first, &second);
        int32_t last = VCall<int32_t>(r, 0x12c / 4, first, second);
        levels = last + 1;
        if (levels <= 0)
            return static_cast<uint32_t>(last);
    }
    else
    {
        levels = 1;
    }

    IDirect3DSurface9* surface = nullptr;
    int32_t level = 0;
    uint32_t result;
    do
    {
        auto* cube = Field<IDirect3DCubeTexture9*>(slot, 0x14);
        if (cube)
            cube->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(GameGlobal<uint32_t>(0x005ca780)), level,
                                    &surface);
        else
            Field<IDirect3DTexture9*>(slot, 0x10)->GetSurfaceLevel(level, &surface);
        if (!surface)
            return 0;
        D3DSURFACE_DESC desc;
        HRESULT hr = surface->GetDesc(&desc);
        if (hr == 0)
        {
            RECT rect = {0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
            uint32_t format = GameGlobal<uint32_t>(0x005ca600 + formatClass * 8);
            uint32_t loadResult = orig::fn_00545c10(u(surface), 0, 0, pixels, format, static_cast<uint32_t>(pitch),
                                                    0, u(&rect), 0xffffffff /* D3DX_DEFAULT */, 0);
            if (loadResult != 0)
            {
                auto name = reinterpret_cast<const char*>(orig::fn_004e0d20(loadResult));
                GameLog(kStrDXError, name, kStrD3DXLoadSurfaceFromMemory);
            }
        }
        else
        {
            orig::fn_00508210(kStrGetDescError, u(r), static_cast<uint32_t>(hr), 0);
        }
        ++level;
        result = surface->Release();
    } while (level < levels);
    return result;
}

// 0x0050b390 ([image record, TGA file data, size]): decode a TGA (types 1-3, optionally RLE bit 8) into the
// record: +0 width, +4 height, +0x14 texture class (8-bit 9, 15-bit 5, 16-bit 4, 24-bit 2, 32-bit 0), +0x1c
// pixels allocated with the game heap ("new[]"), rows stored bottom-up. Colour-mapped files build a palette that
// is then discarded. Returns 0, E_FAIL (malformed) or E_OUTOFMEMORY.
uint32_t Tga_Decode(uint32_t* out, const uint8_t* tga, uint32_t size)
{
    if (size < 0x12)
        return kEFail;
    if (tga[1] & 0xfe)
        return kEFail;
    uint32_t imageType = tga[2];
    if (imageType & 0xfffffff4)
        return kEFail;
    if (Word(tga, 0xc) == 0 || Word(tga, 0xe) == 0)
        return kEFail;
    uint8_t bpp = tga[0x10];
    uint32_t cmapEntryBytes = (tga[7] + 7u) >> 3;
    uint32_t pixelBytes = (bpp + 7u) >> 3;
    bool colorMapped = false;
    uint32_t formatClass;
    switch (imageType & 3)
    {
    case 1:
        if (tga[1] == 0 || bpp != 8)
            return kEFail;
        colorMapped = true;
        formatClass = 9;
        break;
    case 2:
        switch (bpp)
        {
        case 15: formatClass = 5; break;
        case 16: formatClass = 4; break;
        case 24: formatClass = 2; break;
        case 32: formatClass = 0; break;
        default: return kEFail;
        }
        break;
    case 3:
        if (bpp != 8)
            return kEFail;
        formatClass = 9;
        break;
    default:
        return kEFail;
    }

    uint32_t rle = imageType & 8;
    bool bottomUp = (tga[0x11] & 0x20) != 0x20;
    bool leftToRight = (tga[0x11] & 0x10) != 0x10;
    uint32_t remaining = size - 0x12;
    uint32_t idLength = tga[0];
    if (remaining < idLength)
        return kEFail;
    remaining -= idLength;
    const uint8_t* cmap = tga + idLength + 0x12;
    uint32_t cmapLength = Word(tga, 5);
    uint32_t cmapBytes = cmapLength * cmapEntryBytes;
    if (remaining < cmapBytes)
        return kEFail;

    if (colorMapped)
    {
        if (static_cast<int32_t>(Word(tga, 3) + cmapLength) > 0x100)
            return kEFail;
        uint8_t* palette = GameAlloc(0x400);
        if (!palette)
            return kEOutOfMemory;
        memset(palette, 0xff, 0x400);
        uint8_t* dst = palette + Word(tga, 3) * 4;
        uint8_t* end = dst + cmapLength * 4;
        const uint8_t* src = cmap;
        if (tga[1] != 0)
        {
            for (; dst < end; dst += 4)
            {
                uint8_t c0, c1, c2, a;
                switch (tga[7])
                {
                case 15:
                case 16:
                {
                    uint16_t w = Word(src, 0);
                    a = tga[7] == 15 ? 0xff : static_cast<uint8_t>((w >> 15) * 0xff);
                    uint32_t r5 = (w >> 10) & 0x1f, g5 = (w >> 5) & 0x1f, b5 = w & 0x1f;
                    c0 = static_cast<uint8_t>((r5 >> 2) | (r5 << 3));
                    c1 = static_cast<uint8_t>((g5 >> 2) | (g5 << 3));
                    c2 = static_cast<uint8_t>((b5 >> 2) | (b5 << 3));
                    src += 2;
                    break;
                }
                case 24:
                    c0 = src[2];
                    c1 = src[1];
                    c2 = src[0];
                    a = 0xff;
                    src += 3;
                    break;
                case 32:
                    c0 = src[2];
                    c1 = src[1];
                    c2 = src[0];
                    a = src[3];
                    src += 4;
                    break;
                default:
                    return kEFail;  // the palette is not freed here in the original
                }
                dst[0] = c0;
                dst[1] = c1;
                dst[2] = c2;
                dst[3] = a;
            }
        }
        orig::fn_004e08d0(kHeap, u(palette));
    }

    remaining -= cmapBytes;
    const uint8_t* src = cmap + cmapBytes;
    uint32_t width = Word(tga, 0xc);
    uint32_t height = Word(tga, 0xe);
    uint32_t imageBytes = height * width * pixelBytes;
    uint32_t rowBytes = width * pixelBytes;
    out[5] = formatClass;
    out[7] = u(src);
    out[0] = width;
    out[1] = height;

    if (!rle && bottomUp && leftToRight)
    {
        if (remaining < imageBytes)
            return kEFail;
        uint8_t* pixels = GameAlloc(height * rowBytes);
        out[7] = u(pixels);
        memcpy(pixels, src, out[1] * rowBytes);
        return 0;
    }

    uint8_t* pixels = GameAlloc(imageBytes);
    out[7] = u(pixels);
    if (!pixels)
        return kEOutOfMemory;
    uint8_t* row = bottomUp ? pixels : pixels + (Word(tga, 0xe) - 1) * rowBytes;
    uint8_t* rowLast = row - pixelBytes + rowBytes;
    for (uint32_t y = 0; y < Word(tga, 0xe); ++y)
    {
        uint8_t* dst = leftToRight ? row : rowLast;
        uint32_t x = 0;
        while (x < Word(tga, 0xc))
        {
            uint32_t count = Word(tga, 0xc);
            uint32_t repeat = 0;
            if (rle)
            {
                if (remaining < 1)
                    return kEFail;
                uint8_t header = *src++;
                repeat = header & 0x80;
                count = (header & 0x7f) + 1u;
                --remaining;
            }
            x += count;
            while (count != 0)
            {
                --count;
                if (remaining < pixelBytes)
                    return kEFail;
                memcpy(dst, src, pixelBytes);
                if (!repeat)
                {
                    src += pixelBytes;
                    remaining -= pixelBytes;
                }
                if (leftToRight)
                    dst += pixelBytes;
                else
                    dst -= pixelBytes;
            }
            if (repeat)
            {
                src += pixelBytes;
                remaining -= pixelBytes;
            }
        }
        if (bottomUp)
        {
            row += rowBytes;
            rowLast += rowBytes;
        }
        else
        {
            row -= rowBytes;
            rowLast -= rowBytes;
        }
    }
    return 0;
}

// 0x0050ca70 (eax = renderer): bind the vertex shader for the current feature key ([+0x3a4] & [+0x3a8]) from
// the shader table (+0x33c; cached key +0x8c0, bound shader +0x8c4). Without a table it binds [[+0x3bc] + 8].
// A key the table lacks (0x00461fc0) is created (0x0048c4c0) or logged as "Missing vshader" and binds null.
// SetVertexShader is called only on change. Returns true in AL unless the lookup failed.
uint32_t Renderer_SelectVertexShader(Renderer* r)
{
    uint32_t table = Field<uint32_t>(r, 0x33c);
    uint8_t ok = 1;
    uint32_t shader;
    uint32_t eax;  // the value the original leaves in EAX's upper bytes
    if (table == 0)
    {
        Field<uint32_t>(r, 0x8c0) = 0;
        shader = Field<uint32_t>(Field<uint8_t*>(r, 0x3bc), 8);
        eax = 0;
    }
    else
    {
        uint32_t key = Field<uint32_t>(r, 0x3a8) & Field<uint32_t>(r, 0x3a4);
        if (Field<uint32_t>(r, 0x8c0) == key)
        {
            shader = Field<uint32_t>(r, 0x8c4);
            eax = shader;
        }
        else
        {
            Field<uint32_t>(r, 0x8c4) = 0;
            Field<uint32_t>(r, 0x8c0) = key;
            shader = orig::fn_00461fc0(table, key);
            eax = shader;
            if (shader == 0)
            {
                eax = orig::fn_0048c4c0(table, key);
                if (eax == 0)
                {
                    orig::fn_00461ff0(key, 0, table);
                    eax = GameFunction<uint32_t(__cdecl*)(const char*, const char*, ...)>(0x004e0c60)(
                        reinterpret_cast<const char*>(0x005891d8) /* "vshader.err" */,
                        reinterpret_cast<const char*>(0x005891e4) /* "Missing vshader %x\r\n" */, key);
                }
                ok = 0;
            }
        }
    }
    if (Field<uint32_t>(r, 0x8c4) != shader)
    {
        Field<uint32_t>(r, 0x8c4) = shader;
        eax = static_cast<uint32_t>(r->device->SetVertexShader(reinterpret_cast<IDirect3DVertexShader9*>(shader)));
    }
    return (eax & 0xffffff00u) | ok;
}

static uint32_t Renderer_PickSupportedTextureClass_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return Renderer_PickSupportedTextureClass(static_cast<int32_t>(eax), (ecx & 0xff) != 0,
                                              reinterpret_cast<Renderer*>(edx));
}
static uint32_t Texture_FillLevelsFromMemory_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Texture_FillLevelsFromMemory(reinterpret_cast<const uint8_t*>(eax), reinterpret_cast<Renderer*>(a0),
                                        reinterpret_cast<uint8_t*>(a1));
}
static uint32_t Tga_Decode_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Tga_Decode(reinterpret_cast<uint32_t*>(a0), reinterpret_cast<const uint8_t*>(a1), a2);
}
static uint32_t Renderer_SelectVertexShader_Abi(uint32_t eax)
{
    return Renderer_SelectVertexShader(reinterpret_cast<Renderer*>(eax));
}
ABI_ENTRY_0050ab40(Renderer_PickSupportedTextureClass_Entry, Renderer_PickSupportedTextureClass_Abi)
ABI_ENTRY_0050b040(Texture_FillLevelsFromMemory_Entry, Texture_FillLevelsFromMemory_Abi)
ABI_ENTRY_0050b390(Tga_Decode_Entry, Tga_Decode_Abi)
ABI_ENTRY_0050ca70(Renderer_SelectVertexShader_Entry, Renderer_SelectVertexShader_Abi)
HOOK(0x0050ab40, "Renderer_PickSupportedTextureClass", Renderer_PickSupportedTextureClass_Entry, nullptr);
HOOK(0x0050b040, "Texture_FillLevelsFromMemory", Texture_FillLevelsFromMemory_Entry, nullptr);
HOOK(0x0050b390, "Tga_Decode", Tga_Decode_Entry, nullptr);
HOOK(0x0050ca70, "Renderer_SelectVertexShader", Renderer_SelectVertexShader_Entry, nullptr);
