// Recovered: renderer core, Direct3D 9 layer (0x00509e30..0x0050c220). S2, Approach B.
//   0x00509e30 GameHeap_FreeNonNull
//   0x00509e50 fn_00509e50
//   0x0050a420 Renderer_SetTextureFormatLevel
//   0x0050a4b0 Renderer_SetMaxTextureLod
//   0x0050a680 Renderer_GetBackBufferAsTexture
//   0x0050a6a0 Renderer_GetPersistantTexture
//   0x0050a770 Renderer_SetRenderTargetTexture
//   0x0050a950 fn_0050a950
//   0x0050a970 fn_0050a970
//   0x0050a990 Renderer_GetTextureSize
//   0x0050aa00 fn_0050aa00
//   0x0050bd10 Renderer_ReleaseTexture
//   0x0050bd30 Renderer_LockTexture
//   0x0050bda0 Renderer_LockTextureReadOnly
//   0x0050be10 Texture_Unlock
//   0x0050bf40 Renderer_CopyTexture
//   0x0050c220 Renderer_GetTextureRawData
// None of these is executed in the comparison scenarios, so none can be verified by compare.py yet.
#include <cstring>

#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

// Recovered elsewhere.
uint32_t Renderer_ProbeTextureFormats(Renderer* r);                                          // auto_00506210.cpp
uint32_t Renderer_UpdateTextureLod(Renderer* r, uint8_t* bucket);                            // textures.cpp
uint32_t Format_BitsPerPixel(uint32_t format);                                               // textures.cpp
bool Texture_LoadFromSurface(uint8_t* texture, Renderer* r, IDirect3DSurface9* source, int32_t face);  // textures.cpp
HRESULT Renderer_SetRenderTarget(IDirect3DSurface9* depth, bool useDefaultDepth, IDirect3DSurface9* target,
                                 Renderer* r);                                               // render_targets.cpp
uint32_t TextureBucket_Release(uint32_t notifyLost, uint8_t* bucket, Renderer* r);           // auto_00508210.cpp
uint32_t Renderer_CopySurfaceToImage(Renderer* r, IDirect3DSurface9* source, uint8_t* image,
                                     uint32_t capacity);                                     // auto_0050c040.cpp

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
constexpr uint32_t kHeap = 0x0063a210;  // the game's allocator object (0x004e07f0 alloc, 0x004e08d0 free)
const char* const kStrGetPersistantTexture = reinterpret_cast<const char*>(0x00589414);  // "GetPersistantTexture"
const char* const kStrTexGetRawData = reinterpret_cast<const char*>(0x0058932c);         // "TexGetRawData"
const D3DCUBEMAP_FACES* const kCubeFaces = reinterpret_cast<const D3DCUBEMAP_FACES*>(0x005ca780);

// The used texture buckets (+0x494), walked as the original does: the next node is read before the current one
// is processed, and the head means the end.
ListNode* UsedBuckets(Renderer* r)
{
    return &Field<ListNode>(r, 0x494);
}
}  // namespace

// 0x00509e30 ([pointer], callee pops 4): free a block on the game heap (0x004e08d0, ecx = 0x0063a210) unless it
// is null (tail jump). Returns the free's result, or 0 for null. (not executed in the comparison scenarios)
uint32_t GameHeap_FreeNonNull(uint32_t p)
{
    if (p == 0)
        return 0;
    return orig::fn_004e08d0(kHeap, p);
}

// 0x00509e50 (ecx = object, probably the renderer; [a, b, c], callee pops 12): store three values at +0x9b4,
// +0x9b8 and +0x9bc. No other evidence for their meaning. Returns c (last value loaded into EAX). (not executed in
// the comparison scenarios)
uint32_t fn_00509e50(uint8_t* self, uint32_t a, uint32_t b, uint32_t c)
{
    Field<uint32_t>(self, 0x9b4) = a;
    Field<uint32_t>(self, 0x9b8) = b;
    Field<uint32_t>(self, 0x9bc) = c;
    return c;
}

// 0x0050a420 (ecx = renderer; [level], callee pops 4): set the texture format level +0x166c (clamped to 0..2,
// unsigned) that Renderer_ProbeTextureFormats (0x0050a290) uses to drop 32-bit (< 2) and 16-bit (< 1) formats.
// On a change, every texture record (+8) in the used buckets (+0x494) that is not a render target (byte +0x22
// bit 7 clear) goes through renderer virtual 74 (+0x128, record, 0), then the formats are probed again (the
// original passes ecx = 0, unused). Returns the probe's result, or the clamped level when nothing changed.
// (not executed in the comparison scenarios)
uint32_t Renderer_SetTextureFormatLevel(Renderer* r, uint32_t level)
{
    uint32_t clamped = level > 2 ? 2 : level;
    if (clamped == Field<uint32_t>(r, 0x166c))
        return clamped;
    ListNode* head = UsedBuckets(r);
    ListNode* n = head->next;
    ListNode* next = n->next;
    while (n != head)
    {
        uint8_t* record = Field<uint8_t*>(n, 8);
        if (record && !(record[0x22] & 0x80))
            VCall<uint32_t>(r, 0x128 / 4, record, 0u);
        n = next;
        next = n->next;
    }
    Field<uint32_t>(r, 0x166c) = clamped;
    return Renderer_ProbeTextureFormats(r);
}

// 0x0050a4b0 (ecx = renderer; [max LOD], callee pops 4): set the renderer's maximum texture LOD (+0x1668, read by
// Renderer_UpdateTextureLod). On a change: EvictManagedResources (device +0x14), then Renderer_UpdateTextureLod
// for every used bucket (+0x494). Returns the argument when unchanged, else 0 (EAX after the loop).
// (not executed in the comparison scenarios)
uint32_t Renderer_SetMaxTextureLod(Renderer* r, uint32_t maxLod)
{
    if (maxLod == Field<uint32_t>(r, 0x1668))
        return maxLod;
    Field<uint32_t>(r, 0x1668) = maxLod;
    r->device->EvictManagedResources();
    ListNode* head = UsedBuckets(r);
    ListNode* n = head->next;
    ListNode* next = n->next;
    while (n != head)
    {
        Renderer_UpdateTextureLod(r, reinterpret_cast<uint8_t*>(n));
        n = next;
        next = n->next;
    }
    return 0;
}

// 0x0050a680 (ecx = renderer, caller pops): copy the back buffer (+0x3d0) into the "GetBackBufferAsTexture"
// texture record at +0x420 (described by 0x0050a5f0) with Texture_LoadFromSurface (face 0). Returns the record,
// or null when the copy failed. (not executed in the comparison scenarios)
uint8_t* Renderer_GetBackBufferAsTexture(Renderer* r)
{
    uint8_t* tex = &Field<uint8_t>(r, 0x420);
    return Texture_LoadFromSurface(tex, r, Field<IDirect3DSurface9*>(r, 0x3d0), 0) ? tex : nullptr;
}

// 0x0050a6a0 (ecx = renderer; [texture record, copy back buffer], callee pops 8): release the record through
// renderer virtual 74 (+0x128, record, 0) and zero its 9 dwords. When the flag is set: GetBackBuffer (device
// +0x48), describe the record as "GetPersistantTexture" (0x00589414) of the back buffer's size (GetDesc +0x30),
// level byte +0x20 = 0x7f, flags word +0x22 = 0x2081, and copy the back buffer into it (Texture_LoadFromSurface);
// on failure virtual 74 releases it again. Releases the back buffer. Returns the record, or null on any failure
// or without the flag. (not executed in the comparison scenarios)
uint8_t* Renderer_GetPersistantTexture(Renderer* r, uint8_t* tex, uint8_t copyBackBuffer)
{
    VCall<uint32_t>(r, 0x128 / 4, tex, 0u);
    std::memset(tex, 0, 9 * 4);
    if (copyBackBuffer == 0)
        return nullptr;
    IDirect3DSurface9* back = nullptr;
    if (r->device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back) != 0)
        return nullptr;
    D3DSURFACE_DESC desc;
    back->GetDesc(&desc);
    Field<UINT>(tex, 0x10) = desc.Width;
    Field<UINT>(tex, 0x14) = desc.Height;
    Field<const char*>(tex, 0) = kStrGetPersistantTexture;
    Field<uint32_t>(tex, 0x1c) = 0;
    Field<uint32_t>(tex, 0x18) = 0;
    tex[0x21] = 0;
    tex[0x20] = 0x7f;
    Field<uint16_t>(tex, 0x22) = 0x2081;
    uint8_t* result = tex;
    if (!Texture_LoadFromSurface(tex, r, back, 0))
    {
        VCall<uint32_t>(r, 0x128 / 4, tex, 0u);
        result = nullptr;
    }
    back->Release();
    return result;
}

// 0x0050a770 (ecx = renderer; [texture record or null, mip level, cube face, use the record's depth surface],
// callee pops 16): make a texture the render target. Needs caps bit +0x14e0 & 0x4000 (render targets; without it
// returns record == null). Null record: bind the back buffer (+0x3d0) and depth surface (+0x3d4) when they differ
// from the current pair (+0x3d8/+0x3dc): virtual 9 (flush), SetDepthStencilSurface (+0x9c), SetRenderTarget
// (+0x94), then cache the target's width, height and bits per pixel (+0x1634/+0x1638/+0x163c); a positive non-zero
// result counts as success. Record: register it (virtual 73, +0x124) if not created (word +0x22 bit 15), needs a
// bucket record (bucket +8); a non-zero level must be below GetLevelCount. Cube maps (bucket record +0x22 bit
// 0x20) take the face (0..6 accepted, table 0x005ca780 - 6 is one past the cube faces in the original) through
// GetCubeMapSurface; the back buffer texture (+0x420) binds the back buffer with the bucket's depth surface
// (+0x18); others use GetSurfaceLevel. The surface results are not checked. Renderer_SetRenderTarget (0x0050a360)
// binds it with the bucket's depth surface when the flag is set; on success the record's +0x23 bit 2 is set.
// Releases the surface. Returns true on success. (not executed in the comparison scenarios)
bool Renderer_SetRenderTargetTexture(Renderer* r, uint8_t* tex, uint32_t level, int32_t face, uint8_t useDepth)
{
    if (!(Field<uint32_t>(r, 0x14e0) & 0x4000))
        return tex == nullptr;
    IDirect3DSurface9* surface = nullptr;
    bool ok = false;
    if (!tex)
    {
        IDirect3DSurface9* back = Field<IDirect3DSurface9*>(r, 0x3d0);
        IDirect3DSurface9* depth = Field<IDirect3DSurface9*>(r, 0x3d4);
        HRESULT hr = 0;
        if (back != Field<IDirect3DSurface9*>(r, 0x3d8) || depth != Field<IDirect3DSurface9*>(r, 0x3dc))
        {
            VCall<void>(r, 0x24 / 4);
            Field<IDirect3DSurface9*>(r, 0x3d8) = back;
            Field<IDirect3DSurface9*>(r, 0x3dc) = depth;
            hr = r->device->SetDepthStencilSurface(depth);
            if (hr == 0)
            {
                hr = r->device->SetRenderTarget(0, Field<IDirect3DSurface9*>(r, 0x3d8));
                if (hr == 0)
                {
                    D3DSURFACE_DESC desc;
                    Field<IDirect3DSurface9*>(r, 0x3d8)->GetDesc(&desc);
                    Field<UINT>(r, 0x1638) = desc.Height;
                    Field<UINT>(r, 0x1634) = desc.Width;
                    Field<uint32_t>(r, 0x163c) = Format_BitsPerPixel(static_cast<uint32_t>(desc.Format));
                }
            }
        }
        ok = hr >= 0;
    }
    else
    {
        bool usable = true;
        if (!(Field<uint16_t>(tex, 0x22) & 0x8000))
        {
            VCall<uint32_t>(r, 0x124 / 4, tex);
            usable = (Field<uint16_t>(tex, 0x22) & 0x8000) != 0;
        }
        uint8_t* bucket = usable ? Field<uint8_t*>(tex, 0xc) : nullptr;
        if (bucket && Field<uint8_t*>(bucket, 8))
        {
            if (level != 0)
            {
                if (static_cast<int32_t>(level) < 0 ||
                    level >= Field<IDirect3DBaseTexture9*>(bucket, 0xc)->GetLevelCount())
                    usable = false;
            }
            if (usable)
            {
                IDirect3DSurface9* depth = nullptr;
                bool defaultDepth = useDepth != 0;
                if (Field<uint8_t*>(bucket, 8)[0x22] & 0x20)
                {
                    if (face < 0 || face > 6)
                        usable = false;
                    else
                        Field<IDirect3DCubeTexture9*>(bucket, 0x14)->GetCubeMapSurface(kCubeFaces[face], level,
                                                                                         &surface);
                }
                else if (tex == &Field<uint8_t>(r, 0x420))
                {
                    surface = nullptr;
                    defaultDepth = true;
                    depth = Field<IDirect3DSurface9*>(bucket, 0x18);
                }
                else
                {
                    Field<IDirect3DTexture9*>(bucket, 0x10)->GetSurfaceLevel(level, &surface);
                }
                if (usable)
                {
                    if (defaultDepth)
                        depth = Field<IDirect3DSurface9*>(bucket, 0x18);
                    if (Renderer_SetRenderTarget(depth, defaultDepth, surface, r) >= 0)
                    {
                        tex[0x23] |= 4;
                        ok = true;
                    }
                }
            }
        }
    }
    if (surface)
        surface->Release();
    return ok;
}

// 0x0050a950 ([a, b], callee pops 8): min(a, b) - 1 (signed), at least 0. No evidence for its purpose.
// (not executed in the comparison scenarios)
int32_t fn_0050a950(int32_t a, int32_t b)
{
    int32_t m = a < b ? a : b;
    int32_t result = static_cast<int32_t>(static_cast<uint32_t>(m) - 1u);
    return result < 0 ? 0 : result;
}

// 0x0050a970 ([unused, float* x, float* y], callee pops 12): store 1.0f in both outputs. Returns the x pointer
// (EAX). No evidence for its purpose (probably a renderer callback returning a unit texel scale; guess).
// (not executed in the comparison scenarios)
uint32_t fn_0050a970(uint32_t /*unused*/, float* x, float* y)
{
    *x = 1.0f;
    *y = 1.0f;
    return u(x);
}

// 0x0050a990 (ecx = renderer; [texture record or null, int* width, int* height], callee pops 12): a texture's size
// in pixels. Null: the current render target's size (+0x1634/+0x1638, cached by Renderer_SetRenderTarget). Records
// with word +0x22 bit 0 store pixels at +0x10/+0x14 (back buffer textures, see 0x0050a5f0); others store log2
// sizes there (1 << value). Returns the height (null) or the height pointer, as EAX ends.
// (not executed in the comparison scenarios)
uint32_t Renderer_GetTextureSize(Renderer* r, uint8_t* tex, int32_t* width, int32_t* height)
{
    if (!tex)
    {
        *width = Field<int32_t>(r, 0x1634);
        *height = Field<int32_t>(r, 0x1638);
        return static_cast<uint32_t>(*height);
    }
    if (tex[0x22] & 1)
    {
        *width = Field<int32_t>(tex, 0x10);
        *height = Field<int32_t>(tex, 0x14);
        return u(height);
    }
    *width = static_cast<int32_t>(1u << (Field<uint32_t>(tex, 0x10) & 31));
    *height = static_cast<int32_t>(1u << (Field<uint32_t>(tex, 0x14) & 31));
    return u(height);
}

namespace
{
// Inlined twice in 0x0050aa00: the number of doublings of 1 needed to reach value, stopping once the power
// passes the global limit at 0x005caae0 (signed comparisons; 0 when the limit is below 1).
int32_t Log2Capped(int32_t value, int32_t limit)
{
    int32_t n = 0;
    int32_t p = 1;
    if (limit < 1)
        return 0;
    while (value > p)
    {
        p = static_cast<int32_t>(static_cast<uint32_t>(p) * 2u);
        n++;
        if (p > limit)
            break;
    }
    return n;
}
}  // namespace

// 0x0050aa00 (ecx = renderer; [texture record, int* log2 width or null, int* log2 height or null], callee pops 12):
// for log2-sized records (word +0x22 bit 0 clear; otherwise both outputs are set to 0 and it returns false):
// width/height = record +0x10/+0x14 minus the skipped levels byte +0x21 (at least 0). The renderer limits
// +0x14e8/+0x14f0 (probably maximum texture width and height; guess) become log2 values capped by the global at
// 0x005caae0. If the smaller excess over the limits is positive, +0x21 grows by it. The sizes are clamped to the
// limits and, when +0x15e4 >= 0, their difference (aspect, in log2) is clamped to it. Writes the sizes to the
// non-null outputs; returns true when the aspect changed. (not executed in the comparison scenarios)
bool fn_0050aa00(Renderer* r, uint8_t* tex, int32_t* outWidth, int32_t* outHeight)
{
    if (tex[0x22] & 1)
    {
        *outHeight = 0;
        *outWidth = 0;
        return false;
    }
    int32_t limit = *reinterpret_cast<const int32_t*>(0x005caae0);
    int32_t maxW = Log2Capped(Field<int32_t>(r, 0x14e8), limit);
    int32_t maxH = Log2Capped(Field<int32_t>(r, 0x14f0), limit);
    uint8_t skip = tex[0x21];
    int32_t w = Field<int32_t>(tex, 0x10) - skip;
    if (w < 0)
        w = 0;
    int32_t h = Field<int32_t>(tex, 0x14) - skip;
    if (h < 0)
        h = 0;
    int32_t excessW = w - maxW;
    int32_t excessH = h - maxH;
    if (excessW < excessH)
    {
        if (excessW > 0)
            tex[0x21] = static_cast<uint8_t>(skip + excessW);
    }
    else if (excessH > 0)
    {
        tex[0x21] = static_cast<uint8_t>(skip + excessH);
    }
    int32_t aspectBefore = w - h < 0 ? h - w : w - h;
    if (w > maxW)
        w = maxW;
    if (h > maxH)
        h = maxH;
    int32_t maxAspect = Field<int32_t>(r, 0x15e4);
    if (maxAspect >= 0)
    {
        if (w >= h)
        {
            if (w - h > maxAspect)
                w = maxAspect + h;
        }
        else if (h - w > maxAspect)
        {
            h = maxAspect + w;
        }
    }
    if (outWidth)
        *outWidth = w;
    if (outHeight)
        *outHeight = h;
    int32_t aspectAfter = w - h < 0 ? h - w : w - h;
    return aspectBefore != aspectAfter;
}

// 0x0050bd10 (ecx = renderer; [texture record or null, notify lost], callee pops 8): give the record's bucket
// (+0x0c) back with TextureBucket_Release (0x0050bcc0: eax = flag byte, ecx = bucket, edx = renderer). Returns its
// result; for a null record the original returns the caller's EAX, here 0. (not executed in the comparison
// scenarios)
uint32_t Renderer_ReleaseTexture(Renderer* r, uint8_t* tex, uint8_t notifyLost)
{
    if (!tex)
        return 0;
    return TextureBucket_Release(notifyLost, Field<uint8_t*>(tex, 0xc), r);
}

namespace
{
// 0x0050bd30 and 0x0050bda0 differ only in the LockRect flags.
bool LockTextureLevel0(Renderer* r, uint8_t* tex, void** bits, INT* pitch, DWORD flags)
{
    uint16_t word = Field<uint16_t>(tex, 0x22);
    if ((word & 0x20) || !(word & 0x40))
        return false;
    if (!(word & 0x8000))
    {
        VCall<uint32_t>(r, 0x124 / 4, tex);
        if (!(tex[0x23] & 0x80))
            return false;
    }
    D3DLOCKED_RECT locked;
    if (Field<IDirect3DTexture9*>(Field<uint8_t*>(tex, 0xc), 0x10)->LockRect(0, &locked, nullptr, flags) != 0)
        return false;
    *bits = locked.pBits;
    *pitch = locked.Pitch;
    return true;
}
}  // namespace

// 0x0050bd30 (ecx = renderer; [texture record, void** bits, int* pitch], callee pops 12): lock level 0 of a
// non-cube texture with word +0x22 bit 0x40, registering it first (virtual 73, +0x124) if not created. LockRect
// (+0x4c) flags 0x800 (D3DLOCK_NOSYSLOCK). Returns true and the bits and pitch on success.
// (not executed in the comparison scenarios)
bool Renderer_LockTexture(Renderer* r, uint8_t* tex, void** bits, INT* pitch)
{
    return LockTextureLevel0(r, tex, bits, pitch, 0x800);
}

// 0x0050bda0: as 0x0050bd30 with LockRect flags 0x810 (NOSYSLOCK | READONLY). (not executed in the comparison
// scenarios)
bool Renderer_LockTextureReadOnly(Renderer* r, uint8_t* tex, void** bits, INT* pitch)
{
    return LockTextureLevel0(r, tex, bits, pitch, 0x810);
}

// 0x0050be10 ([texture record], callee pops 4): the matching unlock: for a created (word +0x22 bit 15) non-cube
// texture with bit 0x40, UnlockRect(0) (+0x50) on bucket (+0x0c) texture +0x10. Returns its result, else the
// flags word (EAX's low half in the original). (not executed in the comparison scenarios)
uint32_t Texture_Unlock(uint8_t* tex)
{
    uint16_t word = Field<uint16_t>(tex, 0x22);
    if ((word & 0x20) || !(word & 0x40) || !(word & 0x8000))
        return word;
    return static_cast<uint32_t>(Field<IDirect3DTexture9*>(Field<uint8_t*>(tex, 0xc), 0x10)->UnlockRect(0));
}

// 0x0050bf40 (ecx = renderer; [source record or null, destination record, source face, destination face], callee
// pops 16): copy level 0 of a texture into another. Null source: renderer virtual 55 (+0xdc) supplies it.
// Otherwise a source not yet created (+0x23 bit 7) is registered (virtual 73, +0x124) and must then be created.
// The source surface comes from GetCubeMapSurface (face from table 0x005ca780, not range checked) for cube maps
// (+0x22 bit 0x20) or GetSurfaceLevel(0); Texture_LoadFromSurface (0x0050be80) copies it into the destination
// face. Releases the surface. Returns the copy's result, false otherwise. (not executed in the comparison
// scenarios)
bool Renderer_CopyTexture(Renderer* r, uint8_t* source, uint8_t* dest, int32_t sourceFace, int32_t destFace)
{
    if (!source)
    {
        source = VCall<uint8_t*>(r, 0xdc / 4);
    }
    else if (!(source[0x23] & 0x80))
    {
        VCall<uint32_t>(r, 0x124 / 4, source);
        if (!(source[0x23] & 0x80))
            return false;
    }
    IDirect3DSurface9* surface = nullptr;  // the original reuses the source argument's stack slot
    uint8_t* bucket = Field<uint8_t*>(source, 0xc);
    HRESULT hr;
    if (source[0x22] & 0x20)
        hr = Field<IDirect3DCubeTexture9*>(bucket, 0x14)->GetCubeMapSurface(kCubeFaces[sourceFace], 0, &surface);
    else
        hr = Field<IDirect3DTexture9*>(bucket, 0x10)->GetSurfaceLevel(0, &surface);
    if (hr != 0)
        return false;
    bool ok = Texture_LoadFromSurface(dest, r, surface, destFace);
    surface->Release();
    return ok;
}

// 0x0050c220 (ecx = renderer; [texture record, void** data, uint32_t* size], callee pops 12): "TexGetRawData".
// Non-cube textures only; registers the record (virtual 73, +0x124) if not created (word +0x22 bit 15). Sums the
// image sizes of every level (Renderer_CopySurfaceToImage with no image, GetSurfaceLevel +0x48) plus 4, stores
// total + 8 in *size and allocates that much on the game heap (0x004e07f0: tag 0x1a, align 2, "TexGetRawData"
// 0x0058932c) into *data. Writes the level count at +0, then each level's image after it. Original bug,
// reproduced: the capacity passed per level is the running remainder, which starts at 0, so
// Renderer_CopySurfaceToImage refuses every level (returns 0), nothing is copied and the result is still true.
// A non-zero remainder would free the block (0x004e08d0) and clear *data. Returns false on a failed
// GetSurfaceLevel in the sizing pass or allocation. (not executed in the comparison scenarios)
bool Renderer_GetTextureRawData(Renderer* r, uint8_t* tex, uint32_t** data, uint32_t* size)
{
    if (Field<uint16_t>(tex, 0x22) & 0x20)
        return false;
    if (!(Field<uint16_t>(tex, 0x22) & 0x8000))
    {
        VCall<uint32_t>(r, 0x124 / 4, tex);
        if (!(tex[0x23] & 0x80))
            return false;
    }
    uint32_t levels = Field<IDirect3DTexture9*>(Field<uint8_t*>(tex, 0xc), 0x10)->GetLevelCount();
    uint32_t total = 4;
    for (uint32_t level = 0; level < levels; ++level)
    {
        IDirect3DSurface9* surface = nullptr;
        if (Field<IDirect3DTexture9*>(Field<uint8_t*>(tex, 0xc), 0x10)->GetSurfaceLevel(level, &surface) != 0)
            return false;
        total += Renderer_CopySurfaceToImage(r, surface, nullptr, 0);
        surface->Release();
    }
    *size = total + 8;
    auto* block = reinterpret_cast<uint32_t*>(orig::fn_004e07f0(kHeap, 0x1a, total + 8, 2, u(kStrTexGetRawData)));
    *data = block;
    if (!block)
        return false;
    uint32_t remaining = 0;
    block[0] = levels;
    uint8_t* out = reinterpret_cast<uint8_t*>(block + 1);
    if (levels == 0)
        return true;
    for (uint32_t level = 0; level < levels; ++level)
    {
        IDirect3DSurface9* surface = nullptr;
        if (Field<IDirect3DTexture9*>(Field<uint8_t*>(tex, 0xc), 0x10)->GetSurfaceLevel(level, &surface) == 0)
        {
            uint32_t written = Renderer_CopySurfaceToImage(r, surface, out, remaining);
            remaining -= written;
            out += written;
            surface->Release();
        }
    }
    if (remaining != 0)
    {
        orig::fn_004e08d0(kHeap, u(*data));
        *data = nullptr;
        return false;
    }
    return true;
}

// ---- ABI adapters and hooks ----
static uint32_t GameHeap_FreeNonNull_Abi(uint32_t p)
{
    return GameHeap_FreeNonNull(p);
}
static uint32_t fn_00509e50_Abi(uint32_t ecx, uint32_t a, uint32_t b, uint32_t c)
{
    return fn_00509e50(reinterpret_cast<uint8_t*>(ecx), a, b, c);
}
static uint32_t Renderer_SetTextureFormatLevel_Abi(uint32_t ecx, uint32_t level)
{
    return Renderer_SetTextureFormatLevel(reinterpret_cast<Renderer*>(ecx), level);
}
static uint32_t Renderer_SetMaxTextureLod_Abi(uint32_t ecx, uint32_t maxLod)
{
    return Renderer_SetMaxTextureLod(reinterpret_cast<Renderer*>(ecx), maxLod);
}
static uint32_t Renderer_GetBackBufferAsTexture_Abi(uint32_t ecx)
{
    return u(Renderer_GetBackBufferAsTexture(reinterpret_cast<Renderer*>(ecx)));
}
static uint32_t Renderer_GetPersistantTexture_Abi(uint32_t ecx, uint32_t tex, uint32_t copyBackBuffer)
{
    return u(Renderer_GetPersistantTexture(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(tex),
                                           static_cast<uint8_t>(copyBackBuffer)));
}
static uint32_t Renderer_SetRenderTargetTexture_Abi(uint32_t ecx, uint32_t tex, uint32_t level, uint32_t face,
                                                    uint32_t useDepth)
{
    return Renderer_SetRenderTargetTexture(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(tex), level,
                                           static_cast<int32_t>(face), static_cast<uint8_t>(useDepth));
}
static uint32_t fn_0050a950_Abi(uint32_t a, uint32_t b)
{
    return static_cast<uint32_t>(fn_0050a950(static_cast<int32_t>(a), static_cast<int32_t>(b)));
}
static uint32_t fn_0050a970_Abi(uint32_t unused, uint32_t x, uint32_t y)
{
    return fn_0050a970(unused, reinterpret_cast<float*>(x), reinterpret_cast<float*>(y));
}
static uint32_t Renderer_GetTextureSize_Abi(uint32_t ecx, uint32_t tex, uint32_t width, uint32_t height)
{
    return Renderer_GetTextureSize(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(tex),
                                   reinterpret_cast<int32_t*>(width), reinterpret_cast<int32_t*>(height));
}
static uint32_t fn_0050aa00_Abi(uint32_t ecx, uint32_t tex, uint32_t outWidth, uint32_t outHeight)
{
    return fn_0050aa00(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(tex),
                       reinterpret_cast<int32_t*>(outWidth), reinterpret_cast<int32_t*>(outHeight));
}
static uint32_t Renderer_ReleaseTexture_Abi(uint32_t ecx, uint32_t tex, uint32_t notifyLost)
{
    return Renderer_ReleaseTexture(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(tex),
                                   static_cast<uint8_t>(notifyLost));
}
static uint32_t Renderer_LockTexture_Abi(uint32_t ecx, uint32_t tex, uint32_t bits, uint32_t pitch)
{
    return Renderer_LockTexture(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(tex),
                                reinterpret_cast<void**>(bits), reinterpret_cast<INT*>(pitch));
}
static uint32_t Renderer_LockTextureReadOnly_Abi(uint32_t ecx, uint32_t tex, uint32_t bits, uint32_t pitch)
{
    return Renderer_LockTextureReadOnly(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(tex),
                                        reinterpret_cast<void**>(bits), reinterpret_cast<INT*>(pitch));
}
static uint32_t Texture_Unlock_Abi(uint32_t tex)
{
    return Texture_Unlock(reinterpret_cast<uint8_t*>(tex));
}
static uint32_t Renderer_CopyTexture_Abi(uint32_t ecx, uint32_t source, uint32_t dest, uint32_t sourceFace,
                                         uint32_t destFace)
{
    return Renderer_CopyTexture(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(source),
                                reinterpret_cast<uint8_t*>(dest), static_cast<int32_t>(sourceFace),
                                static_cast<int32_t>(destFace));
}
static uint32_t Renderer_GetTextureRawData_Abi(uint32_t ecx, uint32_t tex, uint32_t data, uint32_t size)
{
    return Renderer_GetTextureRawData(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(tex),
                                      reinterpret_cast<uint32_t**>(data), reinterpret_cast<uint32_t*>(size));
}

ABI_ENTRY_00509e30(GameHeap_FreeNonNull_Entry, GameHeap_FreeNonNull_Abi)
ABI_ENTRY_00509e50(fn_00509e50_Entry, fn_00509e50_Abi)
ABI_ENTRY_0050a420(Renderer_SetTextureFormatLevel_Entry, Renderer_SetTextureFormatLevel_Abi)
ABI_ENTRY_0050a4b0(Renderer_SetMaxTextureLod_Entry, Renderer_SetMaxTextureLod_Abi)
ABI_ENTRY_0050a680(Renderer_GetBackBufferAsTexture_Entry, Renderer_GetBackBufferAsTexture_Abi)
ABI_ENTRY_0050a6a0(Renderer_GetPersistantTexture_Entry, Renderer_GetPersistantTexture_Abi)
ABI_ENTRY_0050a770(Renderer_SetRenderTargetTexture_Entry, Renderer_SetRenderTargetTexture_Abi)
ABI_ENTRY_0050a950(fn_0050a950_Entry, fn_0050a950_Abi)
ABI_ENTRY_0050a970(fn_0050a970_Entry, fn_0050a970_Abi)
ABI_ENTRY_0050a990(Renderer_GetTextureSize_Entry, Renderer_GetTextureSize_Abi)
ABI_ENTRY_0050aa00(fn_0050aa00_Entry, fn_0050aa00_Abi)
ABI_ENTRY_0050bd10(Renderer_ReleaseTexture_Entry, Renderer_ReleaseTexture_Abi)
ABI_ENTRY_0050bd30(Renderer_LockTexture_Entry, Renderer_LockTexture_Abi)
ABI_ENTRY_0050bda0(Renderer_LockTextureReadOnly_Entry, Renderer_LockTextureReadOnly_Abi)
ABI_ENTRY_0050be10(Texture_Unlock_Entry, Texture_Unlock_Abi)
ABI_ENTRY_0050bf40(Renderer_CopyTexture_Entry, Renderer_CopyTexture_Abi)
ABI_ENTRY_0050c220(Renderer_GetTextureRawData_Entry, Renderer_GetTextureRawData_Abi)

HOOK(0x00509e30, "GameHeap_FreeNonNull", GameHeap_FreeNonNull_Entry, nullptr);
HOOK(0x00509e50, "fn_00509e50", fn_00509e50_Entry, nullptr);
HOOK(0x0050a420, "Renderer_SetTextureFormatLevel", Renderer_SetTextureFormatLevel_Entry, nullptr);
HOOK(0x0050a4b0, "Renderer_SetMaxTextureLod", Renderer_SetMaxTextureLod_Entry, nullptr);
HOOK(0x0050a680, "Renderer_GetBackBufferAsTexture", Renderer_GetBackBufferAsTexture_Entry, nullptr);
HOOK(0x0050a6a0, "Renderer_GetPersistantTexture", Renderer_GetPersistantTexture_Entry, nullptr);
HOOK(0x0050a770, "Renderer_SetRenderTargetTexture", Renderer_SetRenderTargetTexture_Entry, nullptr);
HOOK(0x0050a950, "fn_0050a950", fn_0050a950_Entry, nullptr);
HOOK(0x0050a970, "fn_0050a970", fn_0050a970_Entry, nullptr);
HOOK(0x0050a990, "Renderer_GetTextureSize", Renderer_GetTextureSize_Entry, nullptr);
HOOK(0x0050aa00, "fn_0050aa00", fn_0050aa00_Entry, nullptr);
HOOK(0x0050bd10, "Renderer_ReleaseTexture", Renderer_ReleaseTexture_Entry, nullptr);
HOOK(0x0050bd30, "Renderer_LockTexture", Renderer_LockTexture_Entry, nullptr);
HOOK(0x0050bda0, "Renderer_LockTextureReadOnly", Renderer_LockTextureReadOnly_Entry, nullptr);
HOOK(0x0050be10, "Texture_Unlock", Texture_Unlock_Entry, nullptr);
HOOK(0x0050bf40, "Renderer_CopyTexture", Renderer_CopyTexture_Entry, nullptr);
HOOK(0x0050c220, "Renderer_GetTextureRawData", Renderer_GetTextureRawData_Entry, nullptr);
