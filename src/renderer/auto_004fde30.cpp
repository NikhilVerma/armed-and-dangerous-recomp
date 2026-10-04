// Recovered: renderer core (Direct3D 9 layer), 0x004fde30..0x004ffd10: primitive-interface draws, renderer
// virtuals around matrices, viewport and lights (renderer vtable 0x00589990, slots 9, 10, 12, 23-26, 28-30), and
// a few dynamic-buffer helpers. S2, Approach B. None of these is executed in the comparison scenarios.
//   0x004fde30 fn_004fde30
//   0x004fdf30 fn_004fdf30
//   0x004fe1b0 fn_004fe1b0
//   0x004fe210 fn_004fe210
//   0x004fe910 Renderer_FlushVertexBufferSets
//   0x004feb90 fn_004feb90
//   0x004fec40 fn_004fec40
//   0x004fec50 fn_004fec50
//   0x004fec60 Renderer_GetProjection
//   0x004fed00 Renderer_SetViewport
//   0x004fedd0 Renderer_SetCenteredViewport
//   0x004fee40 Renderer_SetViewportRect
//   0x004fefb0 fn_004fefb0
//   0x004fefe0 fn_004fefe0
//   0x004ff260 Renderer_EvictManagedResources
//   0x004ff270 fn_004ff270
//   0x004ffbc0 Renderer_SetLight
//   0x004ffd10 Renderer_GetLight
#include <cstring>

#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

uint32_t Renderer_PrepareDraw(uint8_t* item);                                      // auto_004fbe00.cpp
uint32_t Renderer_ApplyMaterial(Renderer* r, uint8_t* material);                   // loader/auto_00502cb0.cpp
uint32_t Renderer_UpdateMaterialConstants(Renderer* r, const uint8_t* material);   // render_states.cpp
uint32_t VertexBufferSet_ReleaseBuffers(VertexBufferSet* set);                     // auto_004fb170.cpp

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
// _ftol (0x0056a7c8): truncate toward zero to 64 bits; callers keep the low dword.
uint32_t Ftol(float f)
{
    return static_cast<uint32_t>(static_cast<int64_t>(f));
}
// The virtual base of a sub-object whose vbptr sits at `vbptrOffset` (vbtable entry 1 = offset from the vbptr).
uint8_t* VirtualBase(void* sub, int32_t vbptrOffset)
{
    uint8_t* vbptr = reinterpret_cast<uint8_t*>(sub) + vbptrOffset;
    return vbptr + (*reinterpret_cast<int32_t**>(vbptr))[1];
}
constexpr uint32_t kGameHeap = 0x0063a210;  // ecx of 0x004e08d0
const char* const kStrDXError = reinterpret_cast<const char*>(0x0058b33c);  // "Error %s in DX function %s\n"
const char* const kStrDrawIndexedPrimitive =
    reinterpret_cast<const char*>(0x00589c58);  // "IDirect3DDevice9::DrawIndexedPrimitive"
const float& kHalf = GameGlobal<const float>(0x005bcb4c);  // 0.5f
}  // namespace

// 0x004fde30 (ecx = primitive interface; vbptr at -4; [a0, a1, count, a3, indices], callee pops) (not executed in
// the comparison scenarios): the 16-bit sibling of fn_004fdeb0. Copies count * 2 bytes of indices (rep movsd +
// movsb) into the renderer's shared index buffer (virtual base +0x0c = renderer, renderer +0x68; its virtual 3
// locks with count, virtual 5 unlocks), then returns the interface's own virtual 8 (+0x20) with
// (a0, a1, count, a3, index buffer).
uint32_t fn_004fde30(void* iface, uint32_t a0, uint32_t a1, uint32_t count, uint32_t a3, const uint16_t* indices)
{
    auto sharedIndexBuffer = [iface]() {
        return Field<void*>(Field<uint8_t*>(VirtualBase(iface, -4), 0xc), 0x68);
    };
    void* out = reinterpret_cast<void*>(VCall<uint32_t>(sharedIndexBuffer(), 0xc / 4, count));
    memcpy(out, indices, count * 2u);
    VCall<uint32_t>(sharedIndexBuffer(), 0x14 / 4);
    return VCall<uint32_t>(iface, 0x20 / 4, a0, a1, count, a3, sharedIndexBuffer());
}

// 0x004fdf30 (ecx = primitive interface; vbptr at -4, virtual base +0x0c = renderer; [mesh, material], callee
// pops) (not executed in the comparison scenarios): draw a mesh as an indexed triangle strip. Nothing for a null
// mesh or one marked failed (+8 bit 2). Otherwise Renderer_PrepareDraw(virtual base), SetIndices(mesh +0x18), and
// DrawIndexedPrimitive(TRIANGLESTRIP, 0, vertex block (+4) +0x14, +0x18, 0, mesh +0x10 - 2) for the renderer's
// material table (+0x3ac: byte count, 0x50-byte entries; current index +0x3b0, applied with Renderer_ApplyMaterial
// on change, then Renderer_UpdateMaterialConstants). material = -1 draws once per material and always returns
// false (the result byte is never set on that path, faithfully); otherwise one material, returning whether more
// follow (material + 1 < count, signed). A failed draw logs ("Error %s in DX function %s" / 0x004e0ee0), marks
// the mesh failed and returns false.
uint32_t fn_004fdf30(void* iface, uint8_t* mesh, uint32_t material)
{
    auto renderer = [iface]() { return Field<Renderer*>(VirtualBase(iface, -4), 0xc); };
    auto draw = [&]() {
        uint8_t* vertices = Field<uint8_t*>(mesh, 4);
        return renderer()->device->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(5), 0,
                                                        Field<UINT>(vertices, 0x14), Field<UINT>(vertices, 0x18), 0,
                                                        Field<UINT>(mesh, 0x10) - 2);
    };
    if (!mesh || (mesh[8] & 4))
        return 0;
    Renderer_PrepareDraw(VirtualBase(iface, -4));
    renderer()->device->SetIndices(Field<IDirect3DIndexBuffer9*>(mesh, 0x18));

    if (material == 0xffffffff)
    {
        for (uint32_t i = 0, offset = 0;; ++i, offset += 0x50)
        {
            Renderer* r = renderer();
            uint8_t* table = Field<uint8_t*>(r, 0x3ac);
            if (!table || i >= table[0])
                return 0;
            if (i != Field<uint32_t>(r, 0x3b0))
            {
                Field<uint32_t>(r, 0x3b0) = i;
                if (static_cast<uint8_t>(Renderer_ApplyMaterial(r, table + offset)) == 0)
                    return 0;
            }
            Renderer_UpdateMaterialConstants(r, Field<uint8_t*>(r, 0x3ac) + offset);
            HRESULT hr = draw();
            if (hr != 0)
            {
                auto name = reinterpret_cast<const char*>(orig::fn_004e0d20(static_cast<uint32_t>(hr)));
                GameLog(kStrDXError, name, kStrDrawIndexedPrimitive);
                Field<uint32_t>(mesh, 8) |= 4;
                return 0;
            }
        }
    }

    Renderer* r = renderer();
    uint8_t* table = Field<uint8_t*>(r, 0x3ac);
    if (!table || material >= table[0])
        return 0;
    if (material != Field<uint32_t>(r, 0x3b0))
    {
        Field<uint32_t>(r, 0x3b0) = material;
        if (static_cast<uint8_t>(Renderer_ApplyMaterial(r, table + material * 0x50)) == 0)
            return 0;
    }
    Renderer_UpdateMaterialConstants(r, Field<uint8_t*>(r, 0x3ac) + material * 0x50);
    bool more = static_cast<int32_t>(material + 1) < static_cast<int32_t>(Field<uint8_t*>(renderer(), 0x3ac)[0]);
    HRESULT hr = draw();
    if (hr != 0)
    {
        orig::fn_004e0ee0(u(kStrDrawIndexedPrimitive), static_cast<uint32_t>(hr));
        Field<uint32_t>(mesh, 8) |= 4;
        return 0;
    }
    return more ? 1 : 0;
}

// 0x004fe1b0 (ecx = vertex buffer set object) (not executed in the comparison scenarios): teardown: its virtual 3
// (+0xc) with 0, VertexBufferSet_ReleaseBuffers, unlink its node (+4) from the renderer's list (self-linked
// after), free the shadow index copy (+0x4c, "gfx_primbuffer", see 0x004fe3f0) if any and clear it, then free +0x84
// (no null test) on the game heap (0x004e08d0). Returns the last free's result.
uint32_t fn_004fe1b0(uint8_t* set)
{
    VCall<uint32_t>(set, 0xc / 4, 0u);
    VertexBufferSet_ReleaseBuffers(reinterpret_cast<VertexBufferSet*>(set));
    ListNode* node = &Field<ListNode>(set, 4);
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
    if (uint32_t shadow = Field<uint32_t>(set, 0x4c))
    {
        orig::fn_004e08d0(kGameHeap, shadow);
        Field<uint32_t>(set, 0x4c) = 0;
    }
    return orig::fn_004e08d0(kGameHeap, Field<uint32_t>(set, 0x84));
}

// 0x004fe210 (ecx = vertex buffer set base, as in VertexBufferSet_MakeRoom) (not executed in the comparison
// scenarios): lock flags +0x6c lose D3DLOCK_NOOVERWRITE (0x1000) and gain D3DLOCK_DISCARD (0x2000); the counters
// +0x40/+0x44 restart at 0. Returns 0.
uint32_t fn_004fe210(uint8_t* base)
{
    Field<uint32_t>(base, 0x6c) = (Field<uint32_t>(base, 0x6c) & 0xffffefff) | 0x2000;
    Field<uint32_t>(base, 0x40) = 0;
    Field<uint32_t>(base, 0x44) = 0;
    return 0;
}

// 0x004fe910 (ecx = renderer) (not executed in the comparison scenarios): renderer virtual 9, called first by the
// matrix, viewport and light setters. Unless already running (re-entrancy byte 0x0064a708), calls virtual 8 (+0x20,
// the batch end used by fn_004fd390/fn_004fd410) of every vertex buffer set in the list +0x50 (object = node - 4;
// the next node is read before the call). Returns 0; 1 when it was already running (the original leaves the
// guard byte in al).
uint32_t Renderer_FlushVertexBufferSets(Renderer* r)
{
    uint8_t& busy = GameGlobal<uint8_t>(0x0064a708);
    if (busy != 0)
        return 1;
    ListNode* head = &r->vertexBufferSets;
    ListNode* node = head->next;
    ListNode* next = node->next;
    busy = 1;
    while (node != head)
    {
        VCall<uint32_t>(reinterpret_cast<uint8_t*>(node) - 4, 0x20 / 4);
        node = next;
        next = next->next;
    }
    busy = 0;
    return 0;
}

// 0x004feb90 (ecx = renderer; [block A, block B, matrix pointers, count], callee pops) (not executed in the
// comparison scenarios): renderer virtual 23. Only with vertex shaders (+0x33c): virtual 20 (+0x50) with A and
// virtual 21 (+0x54) with B, then base = 0x004e2910(eax = B, out, A); for each of count entries, upload either
// 0x004e2910(eax = entry, out2, base) or base itself (null entry) as 3 registers with SetVertexShaderConstantF from
// c45, 3 registers apart (a matrix palette, guessed). Returns the last HRESULT (0 when nothing was uploaded).
uint32_t fn_004feb90(Renderer* r, uint32_t a, uint32_t b, const uint32_t* entries, uint32_t count)
{
    if (Field<uint32_t>(r, 0x33c) == 0)
        return 0;
    alignas(16) uint8_t base[0x48];
    alignas(16) uint8_t combined[0x40];
    VCall<uint32_t>(r, 0x50 / 4, a);
    VCall<uint32_t>(r, 0x54 / 4, b);
    orig::fn_004e2910(b, u(base), a);
    uint32_t result = 0;
    for (uint32_t reg = 0x2d; count != 0; --count, ++entries, reg += 3)
    {
        const void* data = base;
        if (*entries != 0)
        {
            orig::fn_004e2910(*entries, u(combined), u(base));
            data = combined;
        }
        result = static_cast<uint32_t>(
            r->device->SetVertexShaderConstantF(reg, reinterpret_cast<const float*>(data), 3));
    }
    return result;
}

// 0x004fec40 (ecx = renderer) (not executed in the comparison scenarios): renderer virtual 26, the address of the
// 0x44-byte matrix block at +0x6c (the one fn_004fe970, virtual 21, stores).
uint32_t fn_004fec40(Renderer* r)
{
    return u(r) + 0x6c;
}

// 0x004fec50 (ecx = renderer) (not executed in the comparison scenarios): renderer virtual 25, the address of the
// 0x44-byte matrix block at +0xb0 (the one fn_004fe9b0, virtual 20, stores).
uint32_t fn_004fec50(Renderer* r)
{
    return u(r) + 0xb0;
}

// 0x004fec60 (ecx = renderer) (not executed in the comparison scenarios): renderer virtual 24, the projection block
// stored by Renderer_SetProjection (+0x138, 17 dwords). When +0x14dc bit 0x20 is set (SetProjection negated the
// first two columns), returns a copy in the static block 0x0064a7c8 with _11,_21,_31,_41 and _12,_22,_32,_42
// negated back (fchs).
uint32_t Renderer_GetProjection(Renderer* r)
{
    if (!(Field<uint8_t>(r, 0x14dc) & 0x20))
        return u(r) + 0x138;
    uint32_t* copy = &GameGlobal<uint32_t>(0x0064a7c8);
    memcpy(copy, &Field<uint32_t>(r, 0x138), 0x11 * 4);
    float* m = reinterpret_cast<float*>(copy);
    m[0] = -m[0];
    m[4] = -m[4];
    m[8] = -m[8];
    m[12] = -m[12];
    m[1] = -m[1];
    m[5] = -m[5];
    m[9] = -m[9];
    m[13] = -m[13];
    return u(copy);
}

// 0x004fed00 (ecx = renderer; [view or null], callee pops) (not executed in the comparison scenarios): renderer
// virtual 29. Virtual 9 first, then stores the view (4 floats: half width, half height, centre x, centre y; null =
// the whole screen, from the screen size ints +0x1634/+0x1638 times 0.5 at 0x005bcb4c) at +0x40, and calls
// SetViewport with X = cx - hw, Y = H - (cy - hh) - 2 hh (y up), Width = 2 hw, Height = 2 hh (each _ftol),
// MinZ 0, MaxZ 1. Returns SetViewport's HRESULT.
uint32_t Renderer_SetViewport(Renderer* r, const uint32_t* view)
{
    VCall<uint32_t>(r, 0x24 / 4);
    float* v = &Field<float>(r, 0x40);
    if (view)
    {
        memcpy(v, view, 16);
    }
    else
    {
        float x = static_cast<float>(Field<int32_t>(r, 0x1634)) * kHalf;
        float y = static_cast<float>(Field<int32_t>(r, 0x1638)) * kHalf;
        v[0] = x;
        v[1] = y;
        v[2] = x;
        v[3] = y;
    }
    D3DVIEWPORT9 vp;
    vp.X = Ftol(v[2] - v[0]);
    float twiceHalfHeight = v[1] + v[1];
    float screenHeight = static_cast<float>(Field<int32_t>(r, 0x1638));
    float top = screenHeight - (v[3] - v[1]);
    vp.Y = Ftol(top - twiceHalfHeight);
    vp.Width = Ftol(v[0] + v[0]);
    vp.Height = Ftol(twiceHalfHeight);
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    return static_cast<uint32_t>(r->device->SetViewport(&vp));
}

// 0x004fedd0 (ecx = renderer; [scale], callee pops) (not executed in the comparison scenarios): renderer virtual
// 30. A viewport centred on the screen (+0x1634/+0x1638 ints) covering `scale` of it: Renderer_SetViewport (virtual
// 29) with half extents W * (scale * 0.5), H * (scale * 0.5) and centre W * 0.5, H * 0.5. Returns its result.
uint32_t Renderer_SetCenteredViewport(Renderer* r, float scale)
{
    float s = scale * kHalf;
    float w = static_cast<float>(Field<int32_t>(r, 0x1634));
    float h = static_cast<float>(Field<int32_t>(r, 0x1638));
    float view[4];
    view[0] = w * s;
    view[1] = h * s;
    view[2] = w * kHalf;
    view[3] = h * kHalf;
    return VCall<uint32_t>(r, 0x74 / 4, view);
}

// 0x004fee40 (ecx = renderer; [x, y, width, height] floats, callee pops) (not executed in the comparison
// scenarios): renderer virtual 28. Renderer_SetViewport (virtual 29) with half extents width * 0.5, height * 0.5
// and centre (x + half width, y + half height). Returns its result.
uint32_t Renderer_SetViewportRect(Renderer* r, float x, float y, float width, float height)
{
    float view[4];
    view[0] = width * kHalf;
    view[1] = height * kHalf;
    view[2] = view[0] + x;
    view[3] = view[1] + y;
    return VCall<uint32_t>(r, 0x74 / 4, view);
}

// 0x004fefb0 (ecx = renderer; [a0, a1, a2], callee pops) (not executed in the comparison scenarios): store the
// three dwords at +0x165c/+0x1660/+0x1664. Returns a2 (left in eax).
uint32_t fn_004fefb0(Renderer* r, uint32_t a0, uint32_t a1, uint32_t a2)
{
    Field<uint32_t>(r, 0x165c) = a0;
    Field<uint32_t>(r, 0x1660) = a1;
    Field<uint32_t>(r, 0x1664) = a2;
    return a2;
}

// 0x004fefe0 (ecx = renderer; [index, a1, a2 (float), a3, a4], callee pops) (not executed in the comparison
// scenarios): store a 16-byte record {a1, a2, a3, a4} at +0x1acc + index * 16 (a2 through fld/fstp). Returns
// renderer + index * 16 (left in eax).
uint32_t fn_004fefe0(Renderer* r, uint32_t index, uint32_t a1, float a2, uint32_t a3, uint32_t a4)
{
    uint32_t entry = u(r) + (index << 4);
    *reinterpret_cast<float*>(u(r) + ((index + 0x1ad) << 4)) = a2;
    *reinterpret_cast<uint32_t*>(entry + 0x1acc) = a1;
    *reinterpret_cast<uint32_t*>(entry + 0x1ad4) = a3;
    *reinterpret_cast<uint32_t*>(entry + 0x1ad8) = a4;
    return entry;
}

// 0x004ff260 (ecx = renderer) (not executed in the comparison scenarios): renderer virtual 10,
// IDirect3DDevice9::EvictManagedResources (+0x14). Returns its HRESULT.
uint32_t Renderer_EvictManagedResources(Renderer* r)
{
    return static_cast<uint32_t>(r->device->EvictManagedResources());
}

// 0x004ff270 (ecx = renderer) (not executed in the comparison scenarios): renderer virtual 12. Calls virtual 1
// (+4) of every index buffer (list +0x58), then of every vertex buffer set (list +0x50); object = node - 4, the next
// node is read before each call. Returns 0.
uint32_t fn_004ff270(Renderer* r)
{
    ListNode* heads[2] = {&r->indexBuffers, &r->vertexBufferSets};
    for (ListNode* head : heads)
    {
        ListNode* node = head->next;
        ListNode* next = node->next;
        while (node != head)
        {
            VCall<uint32_t>(reinterpret_cast<uint8_t*>(node) - 4, 4 / 4);
            node = next;
            next = next->next;
        }
    }
    return 0;
}

// 0x004ffbc0 (ecx = renderer; [index, enable (byte), light or null], callee pops) (not executed in the comparison
// scenarios): set a light. Virtual 9 first. Fixed function (+0x33c == 0): SetLight(index, a copy of the light)
// when given, then LightEnable(index, enable); with vertex shaders only marks the light constants dirty (+0x1680 = 1).
// Then records the enable byte at +0x1a44 + index and the D3DLIGHT9 (0x68 bytes) at +0x169c + index * 0x68.
// Returns LightEnable's HRESULT, or the +0x33c value on the shader path (left in eax).
uint32_t Renderer_SetLight(Renderer* r, uint32_t index, uint8_t enable, const D3DLIGHT9* light)
{
    VCall<uint32_t>(r, 0x24 / 4);
    uint32_t result = Field<uint32_t>(r, 0x33c);
    if (result == 0)
    {
        if (light)
        {
            D3DLIGHT9 copy;
            memcpy(&copy, light, sizeof(copy));
            r->device->SetLight(index, &copy);
        }
        result = static_cast<uint32_t>(r->device->LightEnable(index, enable));
    }
    else
    {
        Field<uint8_t>(r, 0x1680) = 1;
    }
    Field<uint8_t>(r, 0x1a44 + index) = enable;
    if (light)
        memcpy(&Field<uint8_t>(r, 0x169c + index * 0x68), light, 0x68);
    return result;
}

// 0x004ffd10 (ecx = renderer; [index, out], callee pops) (not executed in the comparison scenarios): the getter
// for Renderer_SetLight: copies the recorded D3DLIGHT9 (+0x169c + index * 0x68) to out and returns the enable byte
// (+0x1a44 + index) in al; the rest of eax holds the renderer pointer, as in the original.
uint32_t Renderer_GetLight(Renderer* r, uint32_t index, D3DLIGHT9* out)
{
    memcpy(out, &Field<uint8_t>(r, 0x169c + index * 0x68), 0x68);
    return (u(r) & 0xffffff00u) | Field<uint8_t>(r, 0x1a44 + index);
}

// ---- ABI adapters and hooks

static uint32_t fn_004fde30_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t count, uint32_t a3, uint32_t indices)
{
    return fn_004fde30(reinterpret_cast<void*>(ecx), a0, a1, count, a3, reinterpret_cast<const uint16_t*>(indices));
}
static uint32_t fn_004fdf30_Abi(uint32_t ecx, uint32_t mesh, uint32_t material)
{
    return fn_004fdf30(reinterpret_cast<void*>(ecx), reinterpret_cast<uint8_t*>(mesh), material);
}
static uint32_t fn_004fe1b0_Abi(uint32_t ecx)
{
    return fn_004fe1b0(reinterpret_cast<uint8_t*>(ecx));
}
static uint32_t fn_004fe210_Abi(uint32_t ecx)
{
    return fn_004fe210(reinterpret_cast<uint8_t*>(ecx));
}
static uint32_t Renderer_FlushVertexBufferSets_Abi(uint32_t ecx)
{
    return Renderer_FlushVertexBufferSets(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t fn_004feb90_Abi(uint32_t ecx, uint32_t a, uint32_t b, uint32_t entries, uint32_t count)
{
    return fn_004feb90(reinterpret_cast<Renderer*>(ecx), a, b, reinterpret_cast<const uint32_t*>(entries), count);
}
static uint32_t fn_004fec40_Abi(uint32_t ecx)
{
    return fn_004fec40(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t fn_004fec50_Abi(uint32_t ecx)
{
    return fn_004fec50(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t Renderer_GetProjection_Abi(uint32_t ecx)
{
    return Renderer_GetProjection(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t Renderer_SetViewport_Abi(uint32_t ecx, uint32_t view)
{
    return Renderer_SetViewport(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint32_t*>(view));
}
static uint32_t Renderer_SetCenteredViewport_Abi(uint32_t ecx, uint32_t scale)
{
    return Renderer_SetCenteredViewport(reinterpret_cast<Renderer*>(ecx), u2f(scale));
}
static uint32_t Renderer_SetViewportRect_Abi(uint32_t ecx, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    return Renderer_SetViewportRect(reinterpret_cast<Renderer*>(ecx), u2f(x), u2f(y), u2f(width), u2f(height));
}
static uint32_t fn_004fefb0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004fefb0(reinterpret_cast<Renderer*>(ecx), a0, a1, a2);
}
static uint32_t fn_004fefe0_Abi(uint32_t ecx, uint32_t index, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004fefe0(reinterpret_cast<Renderer*>(ecx), index, a1, u2f(a2), a3, a4);
}
static uint32_t Renderer_EvictManagedResources_Abi(uint32_t ecx)
{
    return Renderer_EvictManagedResources(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t fn_004ff270_Abi(uint32_t ecx)
{
    return fn_004ff270(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t Renderer_SetLight_Abi(uint32_t ecx, uint32_t index, uint32_t enable, uint32_t light)
{
    return Renderer_SetLight(reinterpret_cast<Renderer*>(ecx), index, static_cast<uint8_t>(enable),
                             reinterpret_cast<const D3DLIGHT9*>(light));
}
static uint32_t Renderer_GetLight_Abi(uint32_t ecx, uint32_t index, uint32_t out)
{
    return Renderer_GetLight(reinterpret_cast<Renderer*>(ecx), index, reinterpret_cast<D3DLIGHT9*>(out));
}

ABI_ENTRY_004fde30(fn_004fde30_Entry, fn_004fde30_Abi)
ABI_ENTRY_004fdf30(fn_004fdf30_Entry, fn_004fdf30_Abi)
ABI_ENTRY_004fe1b0(fn_004fe1b0_Entry, fn_004fe1b0_Abi)
ABI_ENTRY_004fe210(fn_004fe210_Entry, fn_004fe210_Abi)
ABI_ENTRY_004fe910(Renderer_FlushVertexBufferSets_Entry, Renderer_FlushVertexBufferSets_Abi)
ABI_ENTRY_004feb90(fn_004feb90_Entry, fn_004feb90_Abi)
ABI_ENTRY_004fec40(fn_004fec40_Entry, fn_004fec40_Abi)
ABI_ENTRY_004fec50(fn_004fec50_Entry, fn_004fec50_Abi)
ABI_ENTRY_004fec60(Renderer_GetProjection_Entry, Renderer_GetProjection_Abi)
ABI_ENTRY_004fed00(Renderer_SetViewport_Entry, Renderer_SetViewport_Abi)
ABI_ENTRY_004fedd0(Renderer_SetCenteredViewport_Entry, Renderer_SetCenteredViewport_Abi)
ABI_ENTRY_004fee40(Renderer_SetViewportRect_Entry, Renderer_SetViewportRect_Abi)
ABI_ENTRY_004fefb0(fn_004fefb0_Entry, fn_004fefb0_Abi)
ABI_ENTRY_004fefe0(fn_004fefe0_Entry, fn_004fefe0_Abi)
ABI_ENTRY_004ff260(Renderer_EvictManagedResources_Entry, Renderer_EvictManagedResources_Abi)
ABI_ENTRY_004ff270(fn_004ff270_Entry, fn_004ff270_Abi)
ABI_ENTRY_004ffbc0(Renderer_SetLight_Entry, Renderer_SetLight_Abi)
ABI_ENTRY_004ffd10(Renderer_GetLight_Entry, Renderer_GetLight_Abi)

HOOK(0x004fde30, "fn_004fde30", fn_004fde30_Entry, nullptr);
HOOK(0x004fdf30, "fn_004fdf30", fn_004fdf30_Entry, nullptr);
HOOK(0x004fe1b0, "fn_004fe1b0", fn_004fe1b0_Entry, nullptr);
HOOK(0x004fe210, "fn_004fe210", fn_004fe210_Entry, nullptr);
HOOK(0x004fe910, "Renderer_FlushVertexBufferSets", Renderer_FlushVertexBufferSets_Entry, nullptr);
HOOK(0x004feb90, "fn_004feb90", fn_004feb90_Entry, nullptr);
HOOK(0x004fec40, "fn_004fec40", fn_004fec40_Entry, nullptr);
HOOK(0x004fec50, "fn_004fec50", fn_004fec50_Entry, nullptr);
HOOK(0x004fec60, "Renderer_GetProjection", Renderer_GetProjection_Entry, nullptr);
HOOK(0x004fed00, "Renderer_SetViewport", Renderer_SetViewport_Entry, nullptr);
HOOK(0x004fedd0, "Renderer_SetCenteredViewport", Renderer_SetCenteredViewport_Entry, nullptr);
HOOK(0x004fee40, "Renderer_SetViewportRect", Renderer_SetViewportRect_Entry, nullptr);
HOOK(0x004fefb0, "fn_004fefb0", fn_004fefb0_Entry, nullptr);
HOOK(0x004fefe0, "fn_004fefe0", fn_004fefe0_Entry, nullptr);
HOOK(0x004ff260, "Renderer_EvictManagedResources", Renderer_EvictManagedResources_Entry, nullptr);
HOOK(0x004ff270, "fn_004ff270", fn_004ff270_Entry, nullptr);
HOOK(0x004ffbc0, "Renderer_SetLight", Renderer_SetLight_Entry, nullptr);
HOOK(0x004ffd10, "Renderer_GetLight", Renderer_GetLight_Entry, nullptr);
