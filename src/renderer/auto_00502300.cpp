// Recovered: zlib (trees.c / infblock.c, statically linked next to the renderer) and renderer vtable methods for
// clears, present, fog, texture transforms and capability queries. S2, Approach B.
//   0x00502300 Zlib_TrFlushBlock
//   0x00502cb0 Zlib_InflateBlocksNew
//   0x00503af0 fn_00503af0
//   0x00503b10 fn_00503b10
//   0x00503fd0 Renderer_GetFog                      (renderer vtable 0x00589990 slot 68)
//   0x00504010 Renderer_SetFog                      (slot 69)
//   0x00504190 Renderer_SetTextureTransformFromView (slot 53)
//   0x00504270 fn_00504270                          (slot 52)
//   0x00504440 Renderer_Clear                       (slot 61)
//   0x00504480 Renderer_ClearDepth                  (slot 60)
//   0x005044d0 Renderer_ClearRect                   (slot 63)
//   0x00504570 Renderer_ClearRectColor              (slot 64)
//   0x005045f0 Renderer_ClearRectDepth              (slot 65)
//   0x005046a0 Renderer_Present                     (slot 66)
//   0x00504b20 Renderer_QueryCapability             (slot 87)
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
float u2f(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

// zlib send_bits (inlined in the original): append `length` bits of `value` to bi_buf (+0x16b0); when bi_valid
// (+0x16b4) exceeds 16 - length (signed, jle), flush the 16-bit buffer to pending_buf (+0x08) at pending (+0x14),
// low byte first, and keep the bits that did not fit. Same as the helper in auto_00501d90.cpp.
void SendBits(uint8_t* s, uint32_t value, int32_t length)
{
    int32_t valid = Field<int32_t>(s, 0x16b4);
    uint16_t& buf = Field<uint16_t>(s, 0x16b0);
    buf = static_cast<uint16_t>(buf | static_cast<uint16_t>(value << valid));
    if (valid > 16 - length)
    {
        uint32_t& pending = Field<uint32_t>(s, 0x14);
        Field<uint8_t*>(s, 0x08)[pending] = static_cast<uint8_t>(buf);
        pending++;
        Field<uint8_t*>(s, 0x08)[pending] = static_cast<uint8_t>(buf >> 8);
        pending++;
        buf = static_cast<uint16_t>(static_cast<uint16_t>(value) >> (16 - valid));
        Field<int32_t>(s, 0x16b4) = valid + length - 16;
    }
    else
    {
        Field<int32_t>(s, 0x16b4) = valid + length;
    }
}

// The original's Clear flags choice: D3DCLEAR_TARGET, plus ZBUFFER when the renderer has a depth buffer (+0x3dc),
// plus STENCIL when it also has a stencil (+0x1644).
DWORD ClearAllFlags(Renderer* r)
{
    if (Field<uint32_t>(r, 0x3dc) == 0)
        return 1;
    return Field<uint32_t>(r, 0x1644) != 0 ? 7 : 3;
}

// Rectangle shared by 0x005044d0/0x00504570/0x005045f0: float x, y, width, height in a bottom-up space,
// converted with _ftol (0x0056a7c8, truncation) and flipped with the renderer height (+0x1638).
D3DRECT ClearRectFromFloats(Renderer* r, float x, float y, float w, float h)
{
    int32_t left = static_cast<int32_t>(x);
    int32_t height = static_cast<int32_t>(h);
    int32_t top = Field<int32_t>(r, 0x1638) - static_cast<int32_t>(y) - height;
    int32_t right = static_cast<int32_t>(w) + left;
    return D3DRECT{left, top, right, top + height};
}
}  // namespace

uint32_t Zlib_InitBlock(uint8_t* s);                                                          // auto_00500d60.cpp
uint32_t Zlib_BuildTree(uint8_t* s, uint8_t* desc);                                           // auto_00500d60.cpp
uint32_t Zlib_SetDataType(uint8_t* s);                                                        // auto_00502900.cpp
void Zlib_BiWindup(uint8_t* s);                                                               // auto_00502900.cpp
uint32_t Zlib_InflateBlocksReset(uint32_t* c, uint8_t* s, uint8_t* z);                        // auto_00502900.cpp
uint32_t Zlib_TrStoredBlock(uint8_t* s, uint32_t eof, const uint8_t* buf, uint32_t storedLen);  // auto_00501d90.cpp
void Zlib_CompressBlock(uint8_t* s, const uint16_t* ltree, const uint16_t* dtree);            // auto_00501d90.cpp
uint32_t Renderer_SelectFogMode(Renderer* r);                                                 // materials.cpp

// 0x00502300 (eax = stored_len, ecx = deflate state, edi = eof; [buf]): zlib _tr_flush_block. With level (+0x7c)
// > 0: set_data_type when data_type (+0x1c) is Z_UNKNOWN (2), build_tree for l_desc (+0xb10) and d_desc (+0xb1c),
// build_bl_tree (0x00501d90, original), opt_lenb/static_lenb from opt_len (+0x16a0) and static_len (+0x16a4).
// Then a stored block (unsigned compare, buf non-null), a static-trees block (static_ltree 0x0057dc50, static_dtree
// 0x0057e0d0) or a dynamic block (send_all_trees 0x00501e00 with max_code +0xb14/+0xb20 + 1; dyn_ltree +0x8c,
// dyn_dtree +0x980). Finally init_block and, on eof, bi_windup (tail call). Returns init_block's EAX, or 0 on eof.
uint32_t Zlib_TrFlushBlock(uint8_t* s, const uint8_t* buf, uint32_t storedLen, uint32_t eof)
{
    uint32_t maxBlIndex = 0;
    uint32_t optLenb;
    uint32_t staticLenb;
    if (Field<int32_t>(s, 0x7c) > 0)
    {
        if (Field<uint8_t>(s, 0x1c) == 2)
            Zlib_SetDataType(s);
        Zlib_BuildTree(s, s + 0xb10);
        Zlib_BuildTree(s, s + 0xb1c);
        maxBlIndex = orig::fn_00501d90(u(s), 0 /* ebp = max_blindex, still 0 */);
        optLenb = (Field<uint32_t>(s, 0x16a0) + 10) >> 3;
        staticLenb = (Field<uint32_t>(s, 0x16a4) + 10) >> 3;
        if (staticLenb <= optLenb)
            optLenb = staticLenb;
    }
    else
    {
        optLenb = staticLenb = storedLen + 5;
    }
    if (storedLen + 4 <= optLenb && buf != nullptr)
    {
        Zlib_TrStoredBlock(s, eof, buf, storedLen);
    }
    else if (staticLenb == optLenb)
    {
        SendBits(s, eof + 2, 3);  // (STATIC_TREES << 1) + eof
        Zlib_CompressBlock(s, reinterpret_cast<const uint16_t*>(0x0057dc50),
                           reinterpret_cast<const uint16_t*>(0x0057e0d0));
    }
    else
    {
        SendBits(s, eof + 4, 3);  // (DYN_TREES << 1) + eof
        orig::fn_00501e00(u(s), Field<uint32_t>(s, 0xb14) + 1, Field<uint32_t>(s, 0xb20) + 1, maxBlIndex + 1);
        Zlib_CompressBlock(s, reinterpret_cast<const uint16_t*>(s + 0x8c), reinterpret_cast<const uint16_t*>(s + 0x980));
    }
    uint32_t result = Zlib_InitBlock(s);
    if (eof == 0)
        return result;
    Zlib_BiWindup(s);
    return 0;
}

// 0x00502cb0 (eax = z_stream, ebx = window size w; [check function]): zlib inflate_blocks_new. Allocates the
// 0x40-byte blocks state, the hufts (8 x 0x5a0) and the window through zalloc (+0x20, opaque +0x28), freeing
// what was allocated with zfree (+0x24) on failure. Sets hufts +0x24, window +0x28, end +0x2c, checkfn +0x38,
// mode +0 = TYPE (0), then inflate_blocks_reset with c = null. Returns the state or null.
uint8_t* Zlib_InflateBlocksNew(uint8_t* z, uint32_t w, uint32_t checkFn)
{
    auto zalloc = Field<void*(__cdecl*)(void*, uint32_t, uint32_t)>(z, 0x20);
    auto zfree = Field<void(__cdecl*)(void*, void*)>(z, 0x24);
    auto s = static_cast<uint8_t*>(zalloc(Field<void*>(z, 0x28), 1, 0x40));
    if (!s)
        return nullptr;
    void* hufts = zalloc(Field<void*>(z, 0x28), 8, 0x5a0);
    Field<void*>(s, 0x24) = hufts;
    if (!hufts)
    {
        zfree(Field<void*>(z, 0x28), s);
        return nullptr;
    }
    auto window = static_cast<uint8_t*>(zalloc(Field<void*>(z, 0x28), 1, w));
    Field<uint8_t*>(s, 0x28) = window;
    if (!window)
    {
        zfree(Field<void*>(z, 0x28), Field<void*>(s, 0x24));
        zfree(Field<void*>(z, 0x28), s);
        return nullptr;
    }
    Field<uint8_t*>(s, 0x2c) = window + w;
    Field<uint32_t>(s, 0x38) = checkFn;
    Field<uint32_t>(s, 0) = 0;
    Zlib_InflateBlocksReset(nullptr, s, z);
    return s;
}

// 0x00503af0 ([object], callee pops): if the object is non-null, call its virtual 10 (+0x28) with 1. Returns true
// (AL = 1; the upper bytes of EAX are those of the virtual call's result). Entry 0x0058983c of an unidentified
// vtable. (not executed in the comparison scenarios)
uint32_t fn_00503af0(void* object)
{
    if (!object)
        return 1;
    uint32_t result = VCall<uint32_t>(object, 0x28 / 4, 1u);
    return (result & 0xffffff00u) | 1;
}

// 0x00503b10 (ecx = object): true when the field at +0x10 is non-null (AL = 1 over that field's upper bytes),
// otherwise whether +0x04 is non-null. Entry 0x00589834 of an unidentified vtable.
// (not executed in the comparison scenarios)
uint32_t fn_00503b10(void* object)
{
    uint32_t a = Field<uint32_t>(object, 0x10);
    if (a != 0)
        return (a & 0xffffff00u) | 1;
    return Field<uint32_t>(object, 4) != 0 ? 1 : 0;
}

// 0x00503fd0 (ecx = renderer; [&start, &end, color[3]], callee pops): renderer virtual 68, the getter matching
// Renderer_SetFog: fog start (+0x1694), fog end (+0x1698) and the three colour words (+0x1688). Returns the colour
// pointer (EAX). (not executed in the comparison scenarios)
uint32_t Renderer_GetFog(Renderer* r, uint32_t* start, uint32_t* end, uint32_t* color)
{
    *start = Field<uint32_t>(r, 0x1694);
    *end = Field<uint32_t>(r, 0x1698);
    color[0] = Field<uint32_t>(r, 0x1688);
    color[1] = Field<uint32_t>(r, 0x168c);
    color[2] = Field<uint32_t>(r, 0x1690);
    return u(color);
}

// 0x00504010 (ecx = renderer; [float start, float end, color[3]], callee pops): renderer virtual 69. Stores the
// fog colour (+0x1688), start (+0x1694) and end (+0x1698), calls virtuals 9 and 11, sets D3DRS_RANGEFOGENABLE (48)
// from caps bit +0x15fe & 2, then Renderer_SelectFogMode. When that returns non-zero (never in practice, see
// materials.cpp), start and end are projected through the matrix at +0x138 (z' = z*_33 + _43 over w = z*_34 + _44)
// when w > 0.0 (double 0x005bcb38); if w <= 0 the original keeps whatever its stack slot held: for start the
// colour pointer's bits (its argument slot is reused), for end an uninitialised local, reproduced here as 0.
// Shader path (+0x33c): SetVertexShaderConstantF(1, {start, end, 1.0 / (end - start), 0.5}, 1) with the raw
// arguments; otherwise D3DRS_FOGSTART (36) and D3DRS_FOGEND (37). Returns the last D3D call's HRESULT.
// (not executed in the comparison scenarios)
uint32_t Renderer_SetFog(Renderer* r, float start, float end, const uint32_t* color)
{
    Field<uint32_t>(r, 0x1688) = color[0];
    Field<uint32_t>(r, 0x168c) = color[1];
    Field<uint32_t>(r, 0x1690) = color[2];
    Field<float>(r, 0x1694) = start;
    Field<float>(r, 0x1698) = end;
    VCall<void>(r, 0x24 / 4);
    VCall<void>(r, 0x2c / 4);
    r->device->SetRenderState(D3DRS_RANGEFOGENABLE, (Field<uint8_t>(r, 0x15fe) & 2) ? 1 : 0);

    float fogStart;
    float fogEnd;
    if (Renderer_SelectFogMode(r) != 0)
    {
        fogStart = u2f(u(color));
        fogEnd = 0.0f;
        float w = start * Field<float>(r, 0x164) + Field<float>(r, 0x174);
        if (w > 0.0f)
            fogStart = (start * Field<float>(r, 0x160) + Field<float>(r, 0x170)) / w;
        w = end * Field<float>(r, 0x164) + Field<float>(r, 0x174);
        if (w > 0.0f)
            fogEnd = (end * Field<float>(r, 0x160) + Field<float>(r, 0x170)) / w;
    }
    else
    {
        fogStart = start;
        fogEnd = end;
    }
    if (Field<uint32_t>(r, 0x33c) != 0)
    {
        float constants[4] = {start, end, 1.0f / (end - start), 0.5f};
        return r->device->SetVertexShaderConstantF(1, constants, 1);
    }
    r->device->SetRenderState(D3DRS_FOGSTART, orig::f2u(fogStart));
    return r->device->SetRenderState(D3DRS_FOGEND, orig::f2u(fogEnd));
}

// 0x00504190 (ecx = renderer; [texture stage], callee pops): renderer virtual 53. Copies the matrix at +0xb0
// (17 dwords, as the original's rep movsd), negates its third column (_13, _23, _33) and clears the fourth column
// and the translation row, i.e. the view rotation with z flipped. Fixed-function path (+0x33c == 0):
// SetTransform(D3DTS_TEXTURE0 + stage, table 0x00589800 = {16, 17, 18, 19}); shader path: 0x004e2210 on the
// matrix in place (eax = ebx = matrix), then renderer virtual 22 (+0x58) with (matrix, stage). Then renderer
// virtual 41 (+0xa4) with (1, 2, 6, 2, stage). Returns that call's result.
// (not executed in the comparison scenarios)
uint32_t Renderer_SetTextureTransformFromView(Renderer* r, uint32_t stage)
{
    uint32_t m[17];
    memcpy(m, reinterpret_cast<const uint8_t*>(r) + 0xb0, sizeof(m));
    float* f = reinterpret_cast<float*>(m);
    f[2] = -f[2];
    f[6] = -f[6];
    f[10] = -f[10];
    m[14] = 0;
    m[13] = 0;
    m[12] = 0;
    m[11] = 0;
    m[7] = 0;
    m[3] = 0;
    if (Field<uint32_t>(r, 0x33c) != 0)
    {
        orig::fn_004e2210(u(m), u(m));
        VCall<uint32_t>(r, 0x58 / 4, u(m), stage);
    }
    else
    {
        auto states = reinterpret_cast<const D3DTRANSFORMSTATETYPE*>(0x00589800);
        r->device->SetTransform(states[stage], reinterpret_cast<const D3DMATRIX*>(m));
    }
    return VCall<uint32_t>(r, 0xa4 / 4, 1u, 2u, 6u, 2u, stage);
}

// 0x00504270 ([a0, a1, face], callee pops; this unused): renderer virtual 52. The table at 0x00589770 has six
// 24-byte entries (vec3 direction, vec3 up) along the +-X/+-Y/+-Z axes, which looks like cube-map faces (guess).
// Calls 0x004e2eb0 (eax = &entry.up, ecx = local 4x4 matrix, edx = -entry.direction, edi = a1), then 0x004e2b40
// (eax = matrix, ecx = a0). Returns 0x004e2b40's EAX. (not executed in the comparison scenarios)
uint32_t fn_00504270(uint32_t a0, uint32_t a1, int32_t face)
{
    const uint8_t* entry = reinterpret_cast<const uint8_t*>(0x00589770) + face * 24;
    const float* direction = reinterpret_cast<const float*>(entry);
    float negated[3] = {-direction[0], -direction[1], -direction[2]};
    uint32_t matrix[17];  // game matrix: 16 floats + flags word at +0x40 (0x004e2b40 reads it)
    orig::fn_004e2eb0(u(entry + 0xc), u(matrix), u(negated), a1);
    return orig::fn_004e2b40(u(matrix), a0);
}

// 0x00504440 (ecx = renderer; [colour], callee pops): renderer virtual 61. Clear(0, null, TARGET [| ZBUFFER if
// +0x3dc [| STENCIL if +0x1644]], colour, 1.0, 0). Returns the HRESULT. (not executed in the comparison scenarios)
uint32_t Renderer_Clear(Renderer* r, D3DCOLOR color)
{
    return r->device->Clear(0, nullptr, ClearAllFlags(r), color, 1.0f, 0);
}

// 0x00504480 (ecx = renderer): renderer virtual 60. With a depth buffer (+0x3dc): Clear(0, null, ZBUFFER
// [| STENCIL if +0x1644], 0, 1.0, 0). Returns the HRESULT, or 0 (the +0x3dc value) without a depth buffer.
// (not executed in the comparison scenarios)
uint32_t Renderer_ClearDepth(Renderer* r)
{
    if (Field<uint32_t>(r, 0x3dc) == 0)
        return 0;
    DWORD flags = Field<uint32_t>(r, 0x1644) != 0 ? 6 : 2;
    return r->device->Clear(0, nullptr, flags, 0, 1.0f, 0);
}

// 0x005044d0 (ecx = renderer; [float x, y, width, height, colour], callee pops): renderer virtual 63. Like
// Renderer_Clear for one rectangle: left = x, top = height(+0x1638) - y - h, right = left + w, bottom = top + h
// (each float truncated by _ftol). Returns the HRESULT. (not executed in the comparison scenarios)
uint32_t Renderer_ClearRect(Renderer* r, float x, float y, float w, float h, D3DCOLOR color)
{
    D3DRECT rect = ClearRectFromFloats(r, x, y, w, h);
    return r->device->Clear(1, &rect, ClearAllFlags(r), color, 1.0f, 0);
}

// 0x00504570 (ecx = renderer; [float x, y, width, height, colour], callee pops): renderer virtual 64. Clears only
// the render target (flags 1) in the rectangle of Renderer_ClearRect. Returns the HRESULT.
// (not executed in the comparison scenarios)
uint32_t Renderer_ClearRectColor(Renderer* r, float x, float y, float w, float h, D3DCOLOR color)
{
    D3DRECT rect = ClearRectFromFloats(r, x, y, w, h);
    return r->device->Clear(1, &rect, D3DCLEAR_TARGET, color, 1.0f, 0);
}

// 0x005045f0 (ecx = renderer; [float x, y, width, height], callee pops): renderer virtual 65. Renderer_ClearDepth
// for the rectangle of Renderer_ClearRect. Returns the HRESULT, or 0 without a depth buffer.
// (not executed in the comparison scenarios)
uint32_t Renderer_ClearRectDepth(Renderer* r, float x, float y, float w, float h)
{
    D3DRECT rect = ClearRectFromFloats(r, x, y, w, h);
    if (Field<uint32_t>(r, 0x3dc) == 0)
        return 0;
    DWORD flags = Field<uint32_t>(r, 0x1644) != 0 ? 6 : 2;
    return r->device->Clear(1, &rect, flags, 0, 1.0f, 0);
}

// 0x005046a0 (ecx = renderer): renderer virtual 66. Present(null, null, null, null) unless +0x162c is set.
// Returns the HRESULT, or the +0x162c value when skipped. (not executed in the comparison scenarios)
uint32_t Renderer_Present(Renderer* r)
{
    uint32_t suppressed = Field<uint32_t>(r, 0x162c);
    if (suppressed != 0)
        return suppressed;
    return r->device->Present(nullptr, nullptr, nullptr, nullptr);
}

// 0x00504b20 (ecx = renderer; [query], callee pops): renderer virtual 87, a capability query over the caps flag
// bytes (jump table 0x00504b74): 0 -> +0x15fc bit 0x10 and +0x15fe bit 0x40, 1 -> +0x15fe bit 6, 2 -> +0x15fe
// bit 7, 3 and 4 -> true, anything else false. The meaning of each query is unknown. Byte-sized results leave the
// upper bytes of EAX as the query value had them (0 for 0-4); reproduced. (not executed in the comparison scenarios)
uint32_t Renderer_QueryCapability(Renderer* r, uint32_t query)
{
    uint8_t flags = Field<uint8_t>(r, 0x15fe);
    switch (query)
    {
    case 0: return ((Field<uint8_t>(r, 0x15fc) & 0x10) && (flags & 0x40)) ? 1 : 0;
    case 1: return (flags >> 6) & 1;
    case 2: return flags >> 7;
    case 3:
    case 4: return 1;
    default: return query & 0xffffff00u;
    }
}

static uint32_t Zlib_TrFlushBlock_Abi(uint32_t eax, uint32_t ecx, uint32_t edi, uint32_t buf)
{
    return Zlib_TrFlushBlock(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<const uint8_t*>(buf), eax, edi);
}
static uint32_t Zlib_InflateBlocksNew_Abi(uint32_t eax, uint32_t ebx, uint32_t checkFn)
{
    return u(Zlib_InflateBlocksNew(reinterpret_cast<uint8_t*>(eax), ebx, checkFn));
}
static uint32_t fn_00503af0_Abi(uint32_t object)
{
    return fn_00503af0(reinterpret_cast<void*>(object));
}
static uint32_t fn_00503b10_Abi(uint32_t ecx)
{
    return fn_00503b10(reinterpret_cast<void*>(ecx));
}
static uint32_t Renderer_GetFog_Abi(uint32_t ecx, uint32_t start, uint32_t end, uint32_t color)
{
    return Renderer_GetFog(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint32_t*>(start),
                           reinterpret_cast<uint32_t*>(end), reinterpret_cast<uint32_t*>(color));
}
static uint32_t Renderer_SetFog_Abi(uint32_t ecx, uint32_t start, uint32_t end, uint32_t color)
{
    return Renderer_SetFog(reinterpret_cast<Renderer*>(ecx), u2f(start), u2f(end),
                           reinterpret_cast<const uint32_t*>(color));
}
static uint32_t Renderer_SetTextureTransformFromView_Abi(uint32_t ecx, uint32_t stage)
{
    return Renderer_SetTextureTransformFromView(reinterpret_cast<Renderer*>(ecx), stage);
}
static uint32_t fn_00504270_Abi(uint32_t a0, uint32_t a1, uint32_t face)
{
    return fn_00504270(a0, a1, static_cast<int32_t>(face));
}
static uint32_t Renderer_Clear_Abi(uint32_t ecx, uint32_t color)
{
    return Renderer_Clear(reinterpret_cast<Renderer*>(ecx), color);
}
static uint32_t Renderer_ClearDepth_Abi(uint32_t ecx)
{
    return Renderer_ClearDepth(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t Renderer_ClearRect_Abi(uint32_t ecx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    return Renderer_ClearRect(reinterpret_cast<Renderer*>(ecx), u2f(x), u2f(y), u2f(w), u2f(h), color);
}
static uint32_t Renderer_ClearRectColor_Abi(uint32_t ecx, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                            uint32_t color)
{
    return Renderer_ClearRectColor(reinterpret_cast<Renderer*>(ecx), u2f(x), u2f(y), u2f(w), u2f(h), color);
}
static uint32_t Renderer_ClearRectDepth_Abi(uint32_t ecx, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    return Renderer_ClearRectDepth(reinterpret_cast<Renderer*>(ecx), u2f(x), u2f(y), u2f(w), u2f(h));
}
static uint32_t Renderer_Present_Abi(uint32_t ecx)
{
    return Renderer_Present(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t Renderer_QueryCapability_Abi(uint32_t ecx, uint32_t query)
{
    return Renderer_QueryCapability(reinterpret_cast<Renderer*>(ecx), query);
}

ABI_ENTRY_00502300(Zlib_TrFlushBlock_Entry, Zlib_TrFlushBlock_Abi)
ABI_ENTRY_00502cb0(Zlib_InflateBlocksNew_Entry, Zlib_InflateBlocksNew_Abi)
ABI_ENTRY_00503af0(fn_00503af0_Entry, fn_00503af0_Abi)
ABI_ENTRY_00503b10(fn_00503b10_Entry, fn_00503b10_Abi)
ABI_ENTRY_00503fd0(Renderer_GetFog_Entry, Renderer_GetFog_Abi)
ABI_ENTRY_00504010(Renderer_SetFog_Entry, Renderer_SetFog_Abi)
ABI_ENTRY_00504190(Renderer_SetTextureTransformFromView_Entry, Renderer_SetTextureTransformFromView_Abi)
ABI_ENTRY_00504270(fn_00504270_Entry, fn_00504270_Abi)
ABI_ENTRY_00504440(Renderer_Clear_Entry, Renderer_Clear_Abi)
ABI_ENTRY_00504480(Renderer_ClearDepth_Entry, Renderer_ClearDepth_Abi)
ABI_ENTRY_005044d0(Renderer_ClearRect_Entry, Renderer_ClearRect_Abi)
ABI_ENTRY_00504570(Renderer_ClearRectColor_Entry, Renderer_ClearRectColor_Abi)
ABI_ENTRY_005045f0(Renderer_ClearRectDepth_Entry, Renderer_ClearRectDepth_Abi)
ABI_ENTRY_005046a0(Renderer_Present_Entry, Renderer_Present_Abi)
ABI_ENTRY_00504b20(Renderer_QueryCapability_Entry, Renderer_QueryCapability_Abi)
HOOK(0x00502300, "Zlib_TrFlushBlock", Zlib_TrFlushBlock_Entry, nullptr);
HOOK(0x00502cb0, "Zlib_InflateBlocksNew", Zlib_InflateBlocksNew_Entry, nullptr);
HOOK(0x00503af0, "fn_00503af0", fn_00503af0_Entry, nullptr);
HOOK(0x00503b10, "fn_00503b10", fn_00503b10_Entry, nullptr);
HOOK(0x00503fd0, "Renderer_GetFog", Renderer_GetFog_Entry, nullptr);
HOOK(0x00504010, "Renderer_SetFog", Renderer_SetFog_Entry, nullptr);
HOOK(0x00504190, "Renderer_SetTextureTransformFromView", Renderer_SetTextureTransformFromView_Entry, nullptr);
HOOK(0x00504270, "fn_00504270", fn_00504270_Entry, nullptr);
HOOK(0x00504440, "Renderer_Clear", Renderer_Clear_Entry, nullptr);
HOOK(0x00504480, "Renderer_ClearDepth", Renderer_ClearDepth_Entry, nullptr);
HOOK(0x005044d0, "Renderer_ClearRect", Renderer_ClearRect_Entry, nullptr);
HOOK(0x00504570, "Renderer_ClearRectColor", Renderer_ClearRectColor_Entry, nullptr);
HOOK(0x005045f0, "Renderer_ClearRectDepth", Renderer_ClearRectDepth_Entry, nullptr);
HOOK(0x005046a0, "Renderer_Present", Renderer_Present_Entry, nullptr);
HOOK(0x00504b20, "Renderer_QueryCapability", Renderer_QueryCapability_Entry, nullptr);
