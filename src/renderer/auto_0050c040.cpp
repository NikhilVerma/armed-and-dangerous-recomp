// Recovered: renderer core, Direct3D 9 layer (0x0050c040..0x0050cd60). S2, Approach B.
//   0x0050c040 Renderer_CopySurfaceToImage
//   0x0050c6d0 Renderer_LoadShaders
//   0x0050c900 Renderer_ReleaseShaders
//   0x0050ccf0 Renderer_SetDisplayMode
//   0x0050cd60 Zlib_HuftBuild
#include <cstring>

#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

IDirect3DVertexShader9* Renderer_CreateVertexShader(Renderer* r, const char* idText,
                                                    const DWORD* function);  // shaders.cpp
IDirect3DPixelShader9* Renderer_GetNamedPixelShader(const char* name, Renderer* r,
                                                    const DWORD* function);  // shaders.cpp
uint32_t Format_BitsPerPixel(uint32_t format);              // textures.cpp

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
constexpr uint32_t kHeap = 0x0063a210;  // the game's allocator object (0x004e07f0 alloc, 0x004e08d0 free)
const char* const kStrHashTree = reinterpret_cast<const char*>(0x00589258);      // "Vertex shader hash tree"
const char* const kStrStreamBuffer = reinterpret_cast<const char*>(0x0058923c);  // "Vertex shader stream buffer"
const char* const kStrShadersDat = reinterpret_cast<const char*>(0x00589230);    // "shaders.dat"
const char* const kStrNew = reinterpret_cast<const char*>(0x0058b5e8);           // allocation tag of the stream
const char* const kStrErrorLoadingShaders = reinterpret_cast<const char*>(0x005891fc);  // "Error loading shaders\n"
}  // namespace

// 0x0050c040 ([renderer, source surface, image or null, image capacity], callee pops 16): read a surface back
// into an image record with the same layout TextureImage_LoadIntoSurface (0x0050c1b0) uploads: word +0 width,
// word +2 height, +4 format, word +8 rows, word +0xa row bytes, +0xc pixel bytes, pixels at +0x10. GetDesc
// (surface vtable +0x30) gives the size and format; bits per pixel from Format_BitsPerPixel (0x00508750). With no
// image it returns the bytes needed (pixels + 0x10); with too small a capacity (unsigned) 0. Otherwise it fills
// the header, creates an offscreen plain surface (device +0x90, D3DPOOL_DEFAULT) into the capacity argument's
// stack slot, copies the source into it with D3DXLoadSurfaceFromSurface (0x0050be40), locks it read-only
// (0x810 = NOSYSLOCK | READONLY) and copies the rows. Original bug, reproduced: both HRESULT tests are inverted
// (je on success), so a successful CreateOffscreenPlainSurface returns 0x10 at once (the surface leaks) and the
// rows are copied only when LockRect fails. Returns 0x10 + rows * row bytes after a copy attempt, 0x10 when the
// load or lock path stops early. (not executed in the comparison scenarios)
uint32_t Renderer_CopySurfaceToImage(Renderer* r, IDirect3DSurface9* source, uint8_t* image, uint32_t capacity)
{
    D3DSURFACE_DESC desc;
    if (source->GetDesc(&desc) != 0)
        return 0;
    uint32_t bpp = Format_BitsPerPixel(static_cast<uint32_t>(desc.Format));
    uint32_t pixelBytes = (bpp * desc.Height * desc.Width) >> 3;
    if (!image)
        return pixelBytes + 0x10;
    if (capacity < pixelBytes + 0x10)
        return 0;
    Field<uint32_t>(image, 4) = static_cast<uint32_t>(desc.Format);
    Field<uint16_t>(image, 0) = static_cast<uint16_t>(desc.Width);
    uint32_t rowBytes = pixelBytes / desc.Height;
    Field<uint16_t>(image, 2) = static_cast<uint16_t>(desc.Height);
    Field<uint16_t>(image, 8) = static_cast<uint16_t>(desc.Height);
    Field<uint32_t>(image, 0xc) = pixelBytes;
    Field<uint16_t>(image, 0xa) = static_cast<uint16_t>(rowBytes);
    uint8_t* dest = image + 0x10;
    uint32_t result = 0x10;
    // The original reuses the capacity argument's slot for the new surface, so on failure it still holds capacity.
    uint32_t slot = capacity;
    if (r->device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_DEFAULT,
                                               reinterpret_cast<IDirect3DSurface9**>(&slot), nullptr) == 0)
        return result;  // original bug: success leaves without copying or releasing
    auto* offscreen = reinterpret_cast<IDirect3DSurface9*>(slot);
    if (orig::fn_0050be40(u(source), slot, u(r)) & 0xff)
    {
        D3DLOCKED_RECT locked = {0, nullptr};  // uninitialised stack in the original when LockRect fails
        if (offscreen->LockRect(&locked, nullptr, 0x810) != 0)  // original bug: copies only on failure
        {
            uint32_t rows = Field<uint16_t>(image, 8);
            uint32_t width = Field<uint16_t>(image, 0xa);
            result = rows * width + 0x10;
            const uint8_t* src = static_cast<const uint8_t*>(locked.pBits);
            for (uint32_t row = 0; row < Field<uint16_t>(image, 8); row++)
            {
                std::memcpy(dest, src, width);
                src += locked.Pitch;
                width = Field<uint16_t>(image, 0xa);
                dest += width;
            }
            offscreen->UnlockRect();
        }
    }
    offscreen->Release();
    return result;
}

static uint32_t Renderer_CopySurfaceToImage_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Renderer_CopySurfaceToImage(reinterpret_cast<Renderer*>(a0), reinterpret_cast<IDirect3DSurface9*>(a1),
                                       reinterpret_cast<uint8_t*>(a2), a3);
}
ABI_ENTRY_0050c040(Renderer_CopySurfaceToImage_Entry, Renderer_CopySurfaceToImage_Abi)
HOOK(0x0050c040, "Renderer_CopySurfaceToImage", Renderer_CopySurfaceToImage_Entry, nullptr);

// 0x0050c900 (eax = renderer): release the shaders. If the vertex shader trie's node array (+0x348, 256 nodes of
// 0x10 bytes, shader at +0xc; see Renderer_CreateVertexShader 0x0050c510) exists: clear the trie (+0x340, +0x344),
// Release every node's shader (vtable +8), free the array (0x004e08d0) and clear +0x348. Then Release and clear
// the 16 slots at +0x360 (the named pixel shaders start there). Returns the last value left in EAX: the last
// slot's Release result, or 0 when it was empty. (not executed in the comparison scenarios)
uint32_t Renderer_ReleaseShaders(Renderer* r)
{
    if (Field<uint8_t*>(r, 0x348))
    {
        Field<uint32_t>(r, 0x344) = 0;
        Field<uint32_t>(r, 0x340) = 0;
        for (int32_t offset = 0; offset < 0x1000; offset += 0x10)
        {
            if (auto* shader = Field<IUnknown*>(Field<uint8_t*>(r, 0x348), offset + 0xc))
                shader->Release();
        }
        orig::fn_004e08d0(kHeap, Field<uint32_t>(r, 0x348));
        Field<uint32_t>(r, 0x348) = 0;
    }
    uint32_t last = 0;
    IUnknown** slot = &Field<IUnknown*>(r, 0x360);
    for (int i = 0; i < 16; i++, slot++)
    {
        last = u(*slot);
        if (*slot)
        {
            last = (*slot)->Release();
            *slot = nullptr;
        }
    }
    return last;
}

static uint32_t Renderer_ReleaseShaders_Abi(uint32_t eax)
{
    return Renderer_ReleaseShaders(reinterpret_cast<Renderer*>(eax));
}
ABI_ENTRY_0050c900(Renderer_ReleaseShaders_Entry, Renderer_ReleaseShaders_Abi)
HOOK(0x0050c900, "Renderer_ReleaseShaders", Renderer_ReleaseShaders_Entry, nullptr);

// 0x0050c6d0 (ecx = renderer): load "shaders.dat". Nothing to do (returns true) when vertex shaders are off
// (+0x33c == 0) and there is no pixel shader support (+0x15fe bit 6). With vertex shaders on, first allocate the
// trie's node array ("Vertex shader hash tree", 0x1000 bytes, into +0x348) and initialise the trie at +0x340
// (0x00462060, esi = 0x100). Then allocate "Vertex shader stream buffer" (0x11000 bytes: 64K stream buffer, then
// a 4K record buffer), build the path (0x0053859e with the directory at [0x0063a204]), create a 0x55c-byte stream
// object (0x004e5f30, buffer 0x10000/0x8000) and open the file (virtual 4). Records: a 0x28-byte header (tag +0,
// size +4, name +8) followed by size bytes; records over 0x1000 bytes are skipped (read into null). Tag "SxtV"
// (0x56747853) creates a vertex shader (0x0050c510), "SxiP" (0x50697853) a named pixel shader (0x0050c5c0). If the
// stream's error flag (+0x10 bit 6) is set it logs "Error loading shaders", releases them (0x0050c900), clears
// +0x3a0 and +0x33c and returns false. Destroys the stream (virtual 0, argument 0) and frees the buffer. Returns
// false without freeing anything when the stream object cannot be made. The original's SEH frame only covers an
// exception in the stream constructor; it is not reproduced. EBX, ESI and EBP are saved before use: not inputs.
bool Renderer_LoadShaders(Renderer* r)
{
    if (Field<uint32_t>(r, 0x33c) == 0 && !(Field<uint8_t>(r, 0x15fe) & 0x40))
        return true;
    if (Field<uint32_t>(r, 0x33c) != 0 && Field<uint32_t>(r, 0x348) == 0)
    {
        uint32_t nodes = orig::fn_004e07f0(kHeap, 3, 0x1000, 2, u(kStrHashTree));
        Field<uint32_t>(r, 0x348) = nodes;
        orig::fn_00462060(0x100, u(&Field<uint8_t>(r, 0x340)), nodes);
    }
    uint32_t buffer = orig::fn_004e07f0(kHeap, 3, 0x11000, 2, u(kStrStreamBuffer));
    auto* record = reinterpret_cast<const DWORD*>(buffer + 0x10000);
    char path[260];
    orig::fn_0053859e(u(path), 0, *reinterpret_cast<uint32_t*>(0x0063a204), u(kStrShadersDat), 0);
    uint32_t object = orig::fn_004e07f0(kHeap, 0x10, 0x55c, 2, u(kStrNew));
    uint32_t stream = 0;
    if (object != 0)
        stream = orig::fn_004e5f30(object, 0x10000, buffer, 0x8000);
    if (stream == 0)
        return false;
    void* s = reinterpret_cast<void*>(stream);
    bool ok = true;
    VCall<uint32_t>(s, 4, u(path), 0u, 0u, 0u);
    uint32_t header[10];
    while (VCall<uint32_t>(s, 1, 0x28u, u(header)) == 0x28)
    {
        uint32_t size = header[1];
        if (size > 0x1000)
        {
            VCall<uint32_t>(s, 1, size, 0u);
            continue;
        }
        if (VCall<uint32_t>(s, 1, size, u(record)) != header[1])
            continue;
        const char* name = reinterpret_cast<const char*>(&header[2]);
        if (header[0] == 0x56747853)
            Renderer_CreateVertexShader(r, name, record);
        else if (header[0] == 0x50697853)
            Renderer_GetNamedPixelShader(name, r, record);
    }
    if ((Field<uint32_t>(s, 0x10) >> 6) & 1)
    {
        GameLog(kStrErrorLoadingShaders);
        Renderer_ReleaseShaders(r);
        Field<uint32_t>(r, 0x3a0) = 0;
        Field<uint32_t>(r, 0x33c) = 0;
        ok = false;
    }
    VCall<uint32_t>(s, 0, 0u);
    if (buffer != 0)
        orig::fn_004e08d0(kHeap, buffer);
    return ok;
}

static uint32_t Renderer_LoadShaders_Abi(uint32_t ecx, uint32_t /*ebx*/, uint32_t /*esi*/, uint32_t /*ebp*/)
{
    return Renderer_LoadShaders(reinterpret_cast<Renderer*>(ecx)) ? 1 : 0;
}
ABI_ENTRY_0050c6d0(Renderer_LoadShaders_Entry, Renderer_LoadShaders_Abi)
HOOK(0x0050c6d0, "Renderer_LoadShaders", Renderer_LoadShaders_Entry, nullptr);

// 0x0050ccf0 (ecx = renderer; [width, height, depth, flag, a4], callee pops 20): change the display
// mode. Clears the ready bit (+0x1628 bit 0), shuts the device down (0x00509920: releases shaders, buffers and
// the device), then unless that failed (negative) stores the new mode with 0x00509040 (the same call
// Renderer_Init 0x005090e0 makes: eax = width, default 800; ecx = height, 600; edx = depth, 32; edi = flag, copied to +0x14dc bit 0; [renderer,
// a4]) and, when that returns false in AL, recreates the device and restores resources (0x005099b0). Sets the
// ready bit again. Returns AL = false only when 0x00509040 returned true ("nothing to change", in which case the
// device stays released); the upper bytes are the last callee's EAX. Callers: none direct (likely virtual).
// (not executed in the comparison scenarios)
uint32_t Renderer_SetDisplayMode(Renderer* r, uint32_t width, uint32_t height, uint32_t depth, uint32_t flag,
                                 uint32_t a4)
{
    uint8_t ok = 1;
    Field<uint8_t>(r, 0x1628) &= 0xfe;
    uint32_t eax = orig::fn_00509920(u(r));
    if (static_cast<int32_t>(eax) >= 0)
    {
        eax = orig::fn_00509040(width, height, depth, flag, u(r), a4);
        ok = static_cast<uint8_t>(eax);
        if (ok == 0)
            eax = orig::fn_005099b0(u(r));
    }
    Field<uint8_t>(r, 0x1628) |= 1;
    return (eax & 0xffffff00) | ok;
}

static uint32_t Renderer_SetDisplayMode_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                            uint32_t a4)
{
    return Renderer_SetDisplayMode(reinterpret_cast<Renderer*>(ecx), a0, a1, a2, a3, a4);
}
ABI_ENTRY_0050ccf0(Renderer_SetDisplayMode_Entry, Renderer_SetDisplayMode_Abi)
HOOK(0x0050ccf0, "Renderer_SetDisplayMode", Renderer_SetDisplayMode_Entry, nullptr);

// zlib 1.1.x inflate_huft: 8 bytes, exop +0, bits +1, base +4 (offsets from the stores in 0x0050cd60).
struct HuftEntry
{
    uint8_t exop;
    uint8_t bits;
    uint16_t pad;  // never written by the code; the original stores whatever was on its stack
    uint32_t base;
};
static_assert(sizeof(HuftEntry) == 8, "inflate_huft is 8 bytes");

// 0x0050cd60 (eax = bits in/out m, ecx = code lengths b; [n, s, d, e, t, hp, hn, v], caller pops): zlib 1.1.x
// huft_build (inftrees.c), identified from the structure: 16 length counters (BMAX 15), the table limit 0x5a0
// (MANY = 1440), 8-byte entries, exop values 0x60 (32 + 64, end of block), e[] + 0x50 (16 + 64) and 0xc0
// (128 + 64, invalid), returns -3 (Z_DATA_ERROR) and -5 (Z_BUF_ERROR). Callers 0x0050d250/0x0050d2f0 are
// inflate_trees_bits/dynamic. e[] is read as one byte per 4-byte entry (movzx of the low byte), as in the original.
int32_t Zlib_HuftBuild(uint32_t* m, const uint32_t* b, uint32_t n, uint32_t s, const uint32_t* d, const uint32_t* e,
                       HuftEntry** t, HuftEntry* hp, uint32_t* hn, uint32_t* v)
{
    uint32_t c[16] = {};
    uint32_t x[16];
    HuftEntry* uTable[15];
    const uint32_t* p = b;
    uint32_t i = n;
    do
    {
        c[*p++]++;
    } while (--i);
    if (c[0] == n)
    {
        *t = nullptr;
        *m = 0;
        return 0;
    }

    uint32_t l = *m;
    uint32_t j;
    for (j = 1; j <= 15; j++)
        if (c[j])
            break;
    int32_t k = static_cast<int32_t>(j);
    if (l < j)
        l = j;
    for (i = 15; i; i--)
        if (c[i])
            break;
    int32_t g = static_cast<int32_t>(i);
    if (l > i)
        l = i;
    *m = l;

    int32_t y = 1 << j;
    for (; j < i; j++, y <<= 1)
        if ((y -= static_cast<int32_t>(c[j])) < 0)
            return -3;
    if ((y -= static_cast<int32_t>(c[i])) < 0)
        return -3;
    c[i] += static_cast<uint32_t>(y);

    x[1] = j = 0;
    const uint32_t* cp = c + 1;
    uint32_t* xp = x + 2;
    while (--i)
        *xp++ = (j += *cp++);

    p = b;
    i = 0;
    do
    {
        if ((j = *p++) != 0)
            v[x[j]++] = i;
    } while (++i < n);
    n = x[g];

    x[0] = i = 0;
    p = v;
    int32_t h = -1;
    int32_t w = -static_cast<int32_t>(l);
    uTable[0] = nullptr;
    HuftEntry* q = nullptr;
    uint32_t z = 0;
    HuftEntry r = {0, 0, 0, 0};  // base starts as uninitialised stack in the original

    for (; k <= g; k++)
    {
        uint32_t a = c[k];
        while (a--)
        {
            while (k > w + static_cast<int32_t>(l))
            {
                h++;
                w += static_cast<int32_t>(l);
                z = static_cast<uint32_t>(g - w);
                z = z > l ? l : z;
                uint32_t f;
                if ((f = 1u << (j = static_cast<uint32_t>(k - w))) > a + 1)
                {
                    f -= a + 1;
                    const uint32_t* cx = c + k;
                    if (j < z)
                        while (++j < z)
                        {
                            if ((f <<= 1) <= *++cx)
                                break;
                            f -= *cx;
                        }
                }
                z = 1u << j;
                if (*hn + z > 0x5a0)
                    return -3;
                uTable[h] = q = hp + *hn;
                *hn += z;
                if (h)
                {
                    x[h] = i;
                    r.bits = static_cast<uint8_t>(l);
                    r.exop = static_cast<uint8_t>(j);
                    j = i >> (w - static_cast<int32_t>(l));
                    r.base = static_cast<uint32_t>(q - uTable[h - 1]) - j;
                    uTable[h - 1][j] = r;
                }
                else
                {
                    *t = q;
                }
            }

            r.bits = static_cast<uint8_t>(k - w);
            if (p >= v + n)
            {
                r.exop = 128 + 64;
            }
            else if (*p < s)
            {
                r.exop = static_cast<uint8_t>(*p < 256 ? 0 : 32 + 64);
                r.base = *p++;
            }
            else
            {
                r.exop = static_cast<uint8_t>(static_cast<uint8_t>(e[*p - s]) + 16 + 64);
                r.base = d[*p++ - s];
            }

            uint32_t f = 1u << (k - w);
            for (j = i >> w; j < z; j += f)
                q[j] = r;

            for (j = 1u << (k - 1); i & j; j >>= 1)
                i ^= j;
            i ^= j;

            uint32_t mask = (1u << w) - 1;
            while ((i & mask) != x[h])
            {
                h--;
                w -= static_cast<int32_t>(l);
                mask = (1u << w) - 1;
            }
        }
    }
    return y != 0 && g != 1 ? -5 : 0;
}

static uint32_t Zlib_HuftBuild_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                   uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    return static_cast<uint32_t>(Zlib_HuftBuild(
        reinterpret_cast<uint32_t*>(eax), reinterpret_cast<const uint32_t*>(ecx), a0, a1,
        reinterpret_cast<const uint32_t*>(a2), reinterpret_cast<const uint32_t*>(a3), reinterpret_cast<HuftEntry**>(a4),
        reinterpret_cast<HuftEntry*>(a5), reinterpret_cast<uint32_t*>(a6), reinterpret_cast<uint32_t*>(a7)));
}
ABI_ENTRY_0050cd60(Zlib_HuftBuild_Entry, Zlib_HuftBuild_Abi)
HOOK(0x0050cd60, "Zlib_HuftBuild", Zlib_HuftBuild_Entry, nullptr);
