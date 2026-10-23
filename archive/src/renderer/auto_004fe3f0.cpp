// Recovered: renderer core, Direct3D 9 layer (renderer object vtable 0x00589990, dynamic buffers). S2, Approach B.
//   0x004fe3f0 Renderer_CreateVertexBufferSet
//   0x004fe650 VertexBufferSet_Construct
//   0x004fe750 Renderer_CreateIndexBuffer
//   0x004fe7d0 Renderer_ReleaseDynamicBuffers
//   0x004fe870 Renderer_RestoreDynamicBuffers
//   0x004fe970 fn_004fe970
//   0x004fe9b0 fn_004fe9b0
//   0x004fea60 Renderer_SetProjection
//   0x004fef90 fn_004fef90
//   0x004ff020 Renderer_ResetDeviceState
//   0x004ffd40 Renderer_Destroy
//   0x004fffe0 Renderer_Create
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
constexpr uint32_t kHeap = 0x0063a210;      // the game's allocator object (0x004e07f0 alloc, 0x004e08d0 free)
constexpr uint32_t kStrNew = 0x0058b5e8;    // "new"
constexpr uint32_t kStrPrimBuffer = 0x00589bf8;  // "gfx_primbuffer"
const char* const kStrIbLocked = reinterpret_cast<const char*>(0x00589c08);  // "Error releasing IB while it is locked!"

// Insert `node` at the tail of the intrusive list `head` (the inlined sequence in 0x004fe3f0 and 0x004fe750).
void ListInsertTail(ListNode* head, ListNode* node)
{
    node->next = node;
    node->prev = node->next;
    node->prev = head->prev;
    node->next = head;
    head->prev = node;
    node->prev->next = node;
}
}  // namespace

uint32_t VertexBufferSet_Restore(VertexBufferSet* set);  // buffers.cpp
uint32_t IndexBuffer_Restore(IndexBuffer* ib);           // buffers.cpp
uint32_t VertexFormats_Init(Renderer* r);                // auto_004fbe00.cpp
uint32_t Renderer_InitDefaults(Renderer* r);             // auto_004fbe00.cpp
bool Renderer_Init(HWND window, uint32_t options, Renderer* r, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2,
                   uint32_t a3);                         // renderer_init.cpp

// 0x004fe650 (eax = object, ecx = most-derived flag): constructor of the 0xac-byte vertex buffer set object, which
// uses virtual inheritance (virtual base pointers at +0x04/+0x10/+0x1c, offset to the base at [vbptr + 4]). With
// the flag set it first writes the vbptr tables 0x00589940/0x00589938/0x00589930 and +0x24; then it installs the
// vtables of each construction stage, ending with 0x00589988 at +0. Only caller: 0x004fe3f0.
void VertexBufferSet_Construct(uint8_t* o, uint32_t mostDerived)
{
    auto vb = [o](uint32_t vbptrOffset) -> uint32_t& {
        return Field<uint32_t>(o, Field<int32_t>(Field<uint8_t*>(o, vbptrOffset), 4) + vbptrOffset);
    };
    if (mostDerived)
    {
        Field<uint32_t>(o, 0x04) = 0x00589940;
        Field<uint32_t>(o, 0x10) = 0x00589938;
        Field<uint32_t>(o, 0x1c) = 0x00589930;
        Field<uint32_t>(o, 0x24) = 0x005898e4;
    }
    Field<uint32_t>(o, 0x00) = 0x00589928;
    vb(0x04) = 0x005898e4;
    Field<uint32_t>(o, 0x08) = 0x00589bbc;
    Field<uint32_t>(o, 0x00) = 0x00589988;
    Field<uint32_t>(o, 0x08) = 0x0058991c;
    vb(0x04) = 0x005898e4;
    Field<uint32_t>(o, 0x0c) = 0x00589928;
    vb(0x10) = 0x005898e4;
    Field<uint32_t>(o, 0x14) = 0x00589bbc;
    Field<uint32_t>(o, 0x0c) = 0x00589914;
    Field<uint32_t>(o, 0x14) = 0x00589908;
    vb(0x10) = 0x005898e4;
    Field<uint32_t>(o, 0x18) = 0x00589928;
    vb(0x1c) = 0x005898e4;
    Field<uint32_t>(o, 0x20) = 0x00589b94;
    Field<uint32_t>(o, 0x18) = 0x005898dc;
    Field<uint32_t>(o, 0x20) = 0x005898b4;
    vb(0x1c) = 0x005898e4;
    Field<uint32_t>(o, 0x00) = 0x00589988;
    Field<uint32_t>(o, 0x08) = 0x0058997c;
    Field<uint32_t>(o, 0x0c) = 0x00589914;
    Field<uint32_t>(o, 0x14) = 0x00589970;
    Field<uint32_t>(o, 0x18) = 0x005898dc;
    Field<uint32_t>(o, 0x20) = 0x00589948;
    vb(0x04) = 0x005898e4;
}

// 0x004fe3f0 (ecx = renderer; [arg0, count, withShadow], callee pops): renderer virtual 8. Allocate (0xac bytes,
// "new") and construct (0x004fe650) a vertex buffer set; its VertexBufferSet part sits 4 bytes into the virtual
// base. With withShadow, allocate a count * 2 byte "gfx_primbuffer" (category 1) into set +0x4c and +0x50. Link the
// set at the tail of renderer +0x50, set renderer (+0x0c), an empty list (+0x10), renderer +4 (+0x18), defaults
// (+0x2c = 0x7fffffff, +0x30 = 1, count = 3 buffers at +0x60, +0x3c = count, +0x28 = 0xd, +0x84 = the object),
// call 0x004fe2a0 (ecx = set, ebx = arg0) and create the buffers (VertexBufferSet_Restore). On failure call the
// set's virtual 0 and return null; else return the set.
VertexBufferSet* Renderer_CreateVertexBufferSet(Renderer* r, uint32_t arg0, uint32_t count, uint32_t withShadow)
{
    auto* obj = reinterpret_cast<uint8_t*>(orig::fn_004e07f0(kHeap, 0x10, 0xac, 2, kStrNew));
    if (!obj)
        return nullptr;
    VertexBufferSet_Construct(obj, 1);
    uint8_t* b = obj + Field<int32_t>(Field<uint8_t*>(obj, 4), 4);  // the virtual base; the set is b + 4
    Field<uint32_t>(b, 0x50) = 0;
    Field<uint32_t>(b, 0x54) = 0;
    if (withShadow)
    {
        Field<uint32_t>(b, 0x50) = orig::fn_004e07f0(kHeap, 1, count * 2, 2, kStrPrimBuffer);
        Field<uint32_t>(b, 0x54) = Field<uint32_t>(b, 0x50);
    }
    ListInsertTail(&r->vertexBufferSets, &Field<ListNode>(b, 8));
    Field<Renderer*>(b, 0x10) = r;
    ListNode* inner = &Field<ListNode>(b, 0x14);
    inner->next = inner;
    inner->prev = inner->next;
    Field<uint32_t>(b, 0x1c) = Field<uint32_t>(r, 4);
    Field<uint32_t>(b, 0x30) = 0x7fffffff;
    Field<uint32_t>(b, 0x34) = 1;
    Field<uint32_t>(b, 0x38) = 0;
    Field<uint32_t>(b, 0x3c) = 0;
    Field<uint32_t>(b, 0x44) = 0;
    Field<uint32_t>(b, 0x48) = 0;
    Field<uint32_t>(b, 0x5c) = 0;
    Field<uint32_t>(b, 0x58) = 0;
    Field<uint32_t>(b, 0x4c) = 0;
    Field<uint32_t>(b, 0x60) = 0;
    Field<uint32_t>(b, 0x68) = 0;
    Field<uint32_t>(b, 0x6c) = 0;
    Field<uint32_t>(b, 0x70) = 0;
    Field<uint32_t>(b, 0x74) = 0;
    Field<uint32_t>(b, 0x78) = 0;
    Field<uint32_t>(b, 0x7c) = 0;
    Field<uint32_t>(b, 0x80) = 0;
    Field<uint32_t>(b, 0x64) = 3;
    Field<uint32_t>(b, 0x40) = count;
    Field<uint32_t>(b, 0x48) = 0;
    Field<uint32_t>(b, 0x84) = 0;
    Field<uint8_t*>(b, 0x88) = obj;
    Field<uint32_t>(b, 0x2c) = 0xd;
    auto* set = reinterpret_cast<VertexBufferSet*>(b + 4);
    orig::fn_004fe2a0(u(set), arg0);
    if (VertexBufferSet_Restore(set) == 0)
        return set;
    VCall<void>(set, 0);
    return nullptr;
}

// 0x004fe750 (edi = renderer; [indexCount], callee pops): allocate a 0x28-byte index buffer ("new", vtable
// 0x00589b60), link it at the tail of renderer +0x58, set renderer/indexCount, clear the rest and create the D3D
// buffer (IndexBuffer_Restore). On failure call its virtual 0 and return null.
IndexBuffer* Renderer_CreateIndexBuffer(Renderer* r, uint32_t indexCount)
{
    auto* ib = reinterpret_cast<IndexBuffer*>(orig::fn_004e07f0(kHeap, 0x10, 0x28, 2, kStrNew));
    if (!ib)
        return nullptr;
    ib->field_00 = 0x00589b60;
    ListInsertTail(&r->indexBuffers, &ib->node);
    ib->renderer = r;
    ib->locked = 0;
    ib->indexCount = indexCount;
    Field<uint32_t>(ib, 0x18) = 0;
    Field<uint32_t>(ib, 0x14) = 0;
    ib->buffer = nullptr;
    Field<uint32_t>(ib, 0x24) = 0;
    if (IndexBuffer_Restore(ib) == 0)
        return ib;
    VCall<void>(ib, 0);
    return nullptr;
}

// 0x004fe7d0 ([renderer], callee pops): before a device reset. Every vertex buffer set (+0x50) goes through
// 0x004fe130 (edi = set); every index buffer (+0x58) still locked logs "Error releasing IB while it is locked!",
// is unlocked (IDirect3DIndexBuffer9::Unlock), then its buffer is released and cleared. Returns the list head
// +0x58 (what EAX holds at the end). (not executed in the comparison scenarios)
uint32_t Renderer_ReleaseDynamicBuffers(Renderer* r)
{
    ListNode* head = &r->vertexBufferSets;
    for (ListNode *n = head->next, *next = n->next; n != head; n = next, next = next->next)
        orig::fn_004fe130(u(n) - 4);
    head = &r->indexBuffers;
    for (ListNode *n = head->next, *next = n->next; n != head; n = next, next = next->next)
    {
        auto* ib = reinterpret_cast<IndexBuffer*>(reinterpret_cast<uint8_t*>(n) - 4);
        if (ib->locked)
        {
            GameLog(kStrIbLocked);
            ib->locked = 0;
            if (ib->buffer)
                ib->buffer->Unlock();
        }
        if (ib->buffer)
        {
            ib->buffer->Release();
            ib->buffer = nullptr;
        }
    }
    return u(head);
}

// 0x004fe870 ([renderer], callee pops): after a device reset. Restore every vertex buffer set (+0x50,
// VertexBufferSet_Restore) and create every missing index buffer (+0x58): CreateIndexBuffer(indexCount * 2,
// usage +0x148c | DYNAMIC, INDEX16, pool +0x1488); results are ignored. Returns the list head +0x58.
// (not executed in the comparison scenarios)
uint32_t Renderer_RestoreDynamicBuffers(Renderer* r)
{
    ListNode* head = &r->vertexBufferSets;
    for (ListNode *n = head->next, *next = n->next; n != head; n = next, next = next->next)
        VertexBufferSet_Restore(reinterpret_cast<VertexBufferSet*>(reinterpret_cast<uint8_t*>(n) - 4));
    head = &r->indexBuffers;
    for (ListNode *n = head->next, *next = n->next; n != head; n = next, next = next->next)
    {
        auto* ib = reinterpret_cast<IndexBuffer*>(reinterpret_cast<uint8_t*>(n) - 4);
        if (!ib->buffer)
        {
            Renderer* owner = ib->renderer;
            owner->device->CreateIndexBuffer(ib->indexCount * 2, owner->bufferUsage | D3DUSAGE_DYNAMIC,
                                             D3DFMT_INDEX16, owner->bufferPool, &ib->buffer, nullptr);
        }
    }
    return u(head);
}

// 0x004fe970 (ecx = renderer; [matrix block, 0x44 bytes]): renderer virtual 21. Unless the argument is the
// renderer's own copy, call virtual 9, copy the 17 dwords to +0x6c and set dirty bits 0x61 in +0x2d0. Renderer_Create
// passes an identity matrix. Returns the new flags (0 when nothing changed; the original leaves EAX as it was).
uint32_t fn_004fe970(Renderer* r, const uint32_t* block)
{
    uint32_t* dst = &Field<uint32_t>(r, 0x6c);
    if (block == dst)
        return 0;
    VCall<void>(r, 9);
    uint32_t flags = Field<uint32_t>(r, 0x2d0) | 0x61;
    for (int i = 0; i < 0x11; i++)
        dst[i] = block[i];
    Field<uint32_t>(r, 0x2d0) = flags;
    return flags;
}

// 0x004fe9b0 (ecx = renderer; [matrix block, 0x44 bytes]): renderer virtual 20. Unless the argument is the
// renderer's own copy, call virtual 9, copy the 17 dwords to +0xb0, derive +0xf4 from it with 0x004e2b40
// (eax = source, ecx = destination), set dirty bits 0x10072 in +0x2d0, give the four 0x50-byte records at +0x740
// fresh serial numbers from the counter +0x2d4 and set +0x3b0 = -1. Returns the last serial number given (0 when
// nothing changed; the original leaves EAX as it was).
uint32_t fn_004fe9b0(Renderer* r, const uint32_t* block)
{
    uint32_t* dst = &Field<uint32_t>(r, 0xb0);
    if (block == dst)
        return 0;
    VCall<void>(r, 9);
    for (int i = 0; i < 0x11; i++)
        dst[i] = block[i];
    orig::fn_004e2b40(u(dst), u(r) + 0xf4);
    Field<uint32_t>(r, 0x2d0) |= 0x10072;
    uint32_t& serial = Field<uint32_t>(r, 0x2d4);
    Field<uint32_t>(r, 0x740) = serial;
    Field<uint32_t>(r, 0x790) = ++serial;
    Field<uint32_t>(r, 0x7e0) = ++serial;
    uint32_t last = ++serial;
    Field<uint32_t>(r, 0x830) = last;
    serial++;
    Field<uint32_t>(r, 0x3b0) = 0xffffffff;
    return last;
}

// 0x004fea60 (ecx = renderer; [matrix block, 0x44 bytes]): renderer virtual 19, the projection matrix (Renderer_Create
// passes the matrix built by 0x004f0100 from 0x3f860a92 = 60 degrees, 1.0, 1000.0). Unless the argument is the
// renderer's own copy: call virtual 9, copy the 17 dwords to +0x138, negate its first two columns when +0x14dc bit
// 0x20 is set, set dirty bits 0x5c in +0x2d0. Then, from the argument's _33/_34/_43/_44, store
// _44 - _34 / _33 * _43 at +0x1674 and (_44 - _34) / (_33 - _43) * _43 + _44 at +0x1678, or 1.0 / 2.0 when
// _43 * _33 or (_33 - _43) * _43 + _44 is zero (0x0056c05c = 0.0). Returns 0 (the original returns FPU status bits).
uint32_t Renderer_SetProjection(Renderer* r, const float* m)
{
    float* dst = &Field<float>(r, 0x138);
    if (m == dst)
        return 0;
    VCall<void>(r, 9);
    bool flip = (Field<uint8_t>(r, 0x14dc) & 0x20) != 0;
    for (int i = 0; i < 0x11; i++)
        Field<uint32_t>(dst, i * 4) = reinterpret_cast<const uint32_t*>(m)[i];
    if (flip)
    {
        dst[0] = -dst[0];
        dst[4] = -dst[4];
        dst[8] = -dst[8];
        dst[12] = -dst[12];
        dst[1] = -dst[1];
        dst[5] = -dst[5];
        dst[9] = -dst[9];
        dst[13] = -dst[13];
    }
    Field<uint32_t>(r, 0x2d0) |= 0x5c;
    const float zero = Field<float>(reinterpret_cast<void*>(0x0056c05c), 0);
    float product = m[14] * m[10];
    if (!(product == zero))
    {
        float denom = (m[10] - m[14]) * m[14] + m[15];
        if (!(denom == zero))
        {
            float a = m[15] - m[11] / m[10] * m[14];
            Field<float>(r, 0x1674) = a;
            float b = (m[15] - m[11]) / (m[10] - m[14]) * m[14] + m[15];
            Field<float>(r, 0x1678) = b;
            return 0;
        }
    }
    Field<uint32_t>(r, 0x1674) = 0x3f800000;
    Field<uint32_t>(r, 0x1678) = 0x40000000;
    return 0;
}

// 0x004fef90 (ecx = renderer; [value]): renderer virtual 42. Call virtual 9, then store the value at +0x1658.
// Returns what virtual 9 returned. Renderer_Create passes 0.
uint32_t fn_004fef90(Renderer* r, uint32_t value)
{
    uint32_t result = VCall<uint32_t>(r, 9);
    Field<uint32_t>(r, 0x1658) = value;
    return result;
}

// 0x004ff020 (ecx = renderer): renderer virtual 11. With a device (+0x14cc): reset the renderer's cached device
// state. Clears +0x3a4, +0x3a8 = 0xfff0000f, byte +0x1680 = 1, +0x1684 = 0, +0x738 = 0x80000000; poisons the four
// 0x50-byte stage records at +0x73c (fields +0x04/+0x4c) and the 12-byte entries at +0x87c with 0xcdcdcdcd, sets
// +0x8ac..+0x8b8 and +0x8d0..+0x8dc to -1, gives each record (+0x740) a serial number from +0x2d4; unbinds both
// stream sources that are set (SetStreamSource(i, 0, 0, 0), cached at +0x3c0), every texture stage 0-3
// (SetTexture, records +0x73c and +0x8d0 cleared, nibble mask in +0x3a8), the indices (SetIndices(0)), the vertex
// shader if +0x33c (SetVertexShader(0) and constant c0 = 0x00589be8), the pixel shader if +0x3a0. Finally copies 14
// dwords from 0x005ca818 to +0x08. Returns the last HRESULT (SetPixelShader) or 0.
uint32_t Renderer_ResetDeviceState(Renderer* r)
{
    if (!r->device)
        return 0;
    Field<uint32_t>(r, 0x3a4) = 0;
    Field<uint32_t>(r, 0x3a8) = 0xfff0000f;
    Field<uint8_t>(r, 0x1680) = 1;
    Field<uint32_t>(r, 0x1684) = 0;
    Field<uint32_t>(r, 0x738) = 0x80000000;
    for (uint32_t i = 0; i < 4; i++)
    {
        Field<uint32_t>(r, 0x73c + i * 0x50) = 0;
        Field<uint32_t>(r, 0x740 + i * 0x50) = 0xcdcdcdcd;
        Field<uint32_t>(r, 0x788 + i * 0x50) = 0xcdcdcdcd;
        Field<uint32_t>(r, 0x884 + i * 0xc) = 0xcdcdcdcd;
        Field<uint32_t>(r, 0x87c + i * 0xc) = 0xcdcdcdcd;
        Field<uint32_t>(r, 0x880 + i * 0xc) = 0xcdcdcdcd;
        Field<uint32_t>(r, 0x8d0 + i * 4) = 0xffffffff;
    }
    Field<uint32_t>(r, 0x8c8) = 0;
    Field<uint32_t>(r, 0x8c4) = 0;
    Field<uint32_t>(r, 0x8bc) = 0;
    Field<uint32_t>(r, 0x8c0) = 0;
    Field<uint32_t>(r, 0x8cc) = 0x80000000;
    Field<uint32_t>(r, 0x8ac) = 0xffffffff;
    Field<uint32_t>(r, 0x8b0) = 0xffffffff;
    Field<uint32_t>(r, 0x8b4) = 0xffffffff;
    Field<uint32_t>(r, 0x8b8) = 0xffffffff;
    for (uint32_t i = 0; i < 4; i++)
    {
        Field<uint32_t>(r, 0x740 + i * 0x50) = Field<uint32_t>(r, 0x2d4);
        Field<uint32_t>(r, 0x2d4) = Field<uint32_t>(r, 0x2d4) + 1;
    }
    Field<uint32_t>(r, 0x3b0) = 0xffffffff;
    Field<uint32_t>(r, 0x3b4) = Field<uint32_t>(r, 0x3b8);
    Field<uint32_t>(r, 0x3bc) = 0;
    for (uint32_t i = 0; i < 2; i++)
    {
        uint32_t* cached = &Field<uint32_t>(r, 0x3c0 + i * 8);
        if (cached[0] != 0 || cached[1] != 0)
        {
            r->device->SetStreamSource(i, nullptr, 0, 0);
            cached[0] = 0;
            cached[1] = 0;
        }
    }
    Field<uint32_t>(r, 0x2d8) = 0;
    for (uint32_t i = 0; i < 4; i++)
    {
        Field<uint32_t>(r, 0x73c + i * 0x50) = 0;
        r->device->SetTexture(i, nullptr);
        Field<uint32_t>(r, 0x8d0 + i * 4) = 0;
        Field<uint32_t>(r, 0x3a8) &= ~(0xf0u << (i * 4));
    }
    r->device->SetIndices(nullptr);
    uint32_t result = Field<uint32_t>(r, 0x33c);
    if (result != 0)
    {
        r->device->SetVertexShader(nullptr);
        r->device->SetVertexShaderConstantF(0, reinterpret_cast<const float*>(0x00589be8), 1);
    }
    result = Field<uint32_t>(r, 0x3a0);
    if (result != 0)
        result = static_cast<uint32_t>(r->device->SetPixelShader(nullptr));
    const uint32_t* defaults = reinterpret_cast<const uint32_t*>(0x005ca818);
    for (int i = 0; i < 0xe; i++)
        Field<uint32_t>(r, 8 + i * 4) = defaults[i];
    return result;
}

// 0x004ffd40 (ecx = renderer): renderer virtual 0, destroy and free. Call virtual 11 (Renderer_ResetDeviceState),
// 0x00506f30 (esi = renderer), destroy (virtual 0) the default index buffer (+0x68), then every index buffer (+0x58,
// each first stored at +0x68), vertex buffer set (+0x50) and the objects in list +0x60; then 0x00509170
// (eax = renderer) and free the renderer (0x004e08d0). Returns the free's result. (not executed in the comparison
// scenarios)
uint32_t Renderer_Destroy(Renderer* r)
{
    VCall<void>(r, 11);
    orig::fn_00506f30(u(r));
    if (void* defaultIb = Field<void*>(r, 0x68))
        VCall<void>(defaultIb, 0);
    ListNode* head = &r->indexBuffers;
    for (ListNode *n = head->next, *next = n->next; n != head; n = next, next = next->next)
    {
        void* object = reinterpret_cast<uint8_t*>(n) - 4;
        Field<void*>(r, 0x68) = object;
        VCall<void>(object, 0);
    }
    head = &r->vertexBufferSets;
    for (ListNode *n = head->next, *next = n->next; n != head; n = next, next = next->next)
        VCall<void>(reinterpret_cast<uint8_t*>(n) - 4, 0);
    head = &Field<ListNode>(r, 0x60);
    for (ListNode *n = head->next, *next = n->next; n != head; n = next, next = next->next)
        VCall<void>(reinterpret_cast<uint8_t*>(n) - 4, 0);
    orig::fn_00509170(u(r));
    return orig::fn_004e08d0(kHeap, u(r));
}

// 0x004fffe0 ([options, window, a2, a3, a4, a5, a6], caller pops): create the renderer. Allocate 0x1adc bytes
// ("new"), vtable 0x00589990, clear +0x340/+0x344/+0x34c/+0x350, Renderer_InitDefaults, then Renderer_Init
// (eax = window, ecx = options, edi = a5; [a2, a3, a4, a6]); on failure destroy it (virtual 0) and return null.
// Then VertexFormats_Init, +4 = 0x004fd000(eax = renderer), virtual 11 (reset state), identity matrices to virtuals
// 21 and 20, a projection from 0x004f0100 (60 degrees, 1.0, 1000.0) to virtual 19, identity to virtual 22 for
// stages 0-3, 0 to virtuals 35 (Renderer_SetZEnable), 36 (Renderer_SetDepthBias) and 42, virtual 41 (0, 0, 0, 0, i)
// for i = 0-3, and a 0x30000-index default index buffer at +0x68. Returns the renderer.
Renderer* Renderer_Create(uint32_t options, uint32_t window, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                          uint32_t a6)
{
    auto* r = reinterpret_cast<Renderer*>(orig::fn_004e07f0(kHeap, 0x10, 0x1adc, 2, kStrNew));
    if (!r)
        return nullptr;
    r->vtbl = reinterpret_cast<void**>(0x00589990);
    Field<uint32_t>(r, 0x340) = 0;
    Field<uint32_t>(r, 0x344) = 0;
    Field<uint32_t>(r, 0x34c) = 0;
    Field<uint32_t>(r, 0x350) = 0;
    Renderer_InitDefaults(r);
    if (!Renderer_Init(reinterpret_cast<HWND>(window), options, r, a5, a2, a3, a4, a6))
    {
        VCall<void>(r, 0);
        return nullptr;
    }
    VertexFormats_Init(r);
    Field<uint32_t>(r, 4) = orig::fn_004fd000(u(r));
    VCall<void>(r, 11);
    uint32_t identity[0x11] = {};
    identity[0] = 0x3f800000;
    identity[5] = 0x3f800000;
    identity[10] = 0x3f800000;
    identity[15] = 0x3f800000;
    VCall<void>(r, 21, identity);
    VCall<void>(r, 20, identity);
    uint32_t projection[0x11];
    orig::fn_004f0100(u(r), u(projection), 0x3f860a92, 0x3f800000, 0x447a0000, 0);
    VCall<void>(r, 19, projection);
    for (uint32_t i = 0; i < 4; i++)
        VCall<void>(r, 22, identity, i);
    VCall<void>(r, 35, 0u);
    VCall<void>(r, 36, 0u);
    VCall<void>(r, 42, 0u);
    for (uint32_t i = 0; i < 4; i++)
        VCall<void>(r, 41, 0u, 0u, 0u, 0u, i);
    Field<IndexBuffer*>(r, 0x68) = Renderer_CreateIndexBuffer(r, 0x30000);
    return r;
}

static uint32_t Renderer_CreateVertexBufferSet_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return u(Renderer_CreateVertexBufferSet(reinterpret_cast<Renderer*>(ecx), a0, a1, a2));
}
static void VertexBufferSet_Construct_Abi(uint32_t eax, uint32_t ecx)
{
    VertexBufferSet_Construct(reinterpret_cast<uint8_t*>(eax), ecx);
}
static uint32_t Renderer_CreateIndexBuffer_Abi(uint32_t edi, uint32_t a0)
{
    return u(Renderer_CreateIndexBuffer(reinterpret_cast<Renderer*>(edi), a0));
}
static uint32_t Renderer_ReleaseDynamicBuffers_Abi(uint32_t a0)
{
    return Renderer_ReleaseDynamicBuffers(reinterpret_cast<Renderer*>(a0));
}
static uint32_t Renderer_RestoreDynamicBuffers_Abi(uint32_t a0)
{
    return Renderer_RestoreDynamicBuffers(reinterpret_cast<Renderer*>(a0));
}
static uint32_t fn_004fe970_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fe970(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint32_t*>(a0));
}
static uint32_t fn_004fe9b0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fe9b0(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint32_t*>(a0));
}
static uint32_t Renderer_SetProjection_Abi(uint32_t ecx, uint32_t a0)
{
    return Renderer_SetProjection(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const float*>(a0));
}
static uint32_t fn_004fef90_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fef90(reinterpret_cast<Renderer*>(ecx), a0);
}
static uint32_t Renderer_ResetDeviceState_Abi(uint32_t ecx)
{
    return Renderer_ResetDeviceState(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t Renderer_Destroy_Abi(uint32_t ecx)
{
    return Renderer_Destroy(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t Renderer_Create_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                    uint32_t a6)
{
    return u(Renderer_Create(a0, a1, a2, a3, a4, a5, a6));
}
ABI_ENTRY_004fe3f0(Renderer_CreateVertexBufferSet_Entry, Renderer_CreateVertexBufferSet_Abi)
ABI_ENTRY_004fe650(VertexBufferSet_Construct_Entry, VertexBufferSet_Construct_Abi)
ABI_ENTRY_004fe750(Renderer_CreateIndexBuffer_Entry, Renderer_CreateIndexBuffer_Abi)
ABI_ENTRY_004fe7d0(Renderer_ReleaseDynamicBuffers_Entry, Renderer_ReleaseDynamicBuffers_Abi)
ABI_ENTRY_004fe870(Renderer_RestoreDynamicBuffers_Entry, Renderer_RestoreDynamicBuffers_Abi)
ABI_ENTRY_004fe970(fn_004fe970_Entry, fn_004fe970_Abi)
ABI_ENTRY_004fe9b0(fn_004fe9b0_Entry, fn_004fe9b0_Abi)
ABI_ENTRY_004fea60(Renderer_SetProjection_Entry, Renderer_SetProjection_Abi)
ABI_ENTRY_004fef90(fn_004fef90_Entry, fn_004fef90_Abi)
ABI_ENTRY_004ff020(Renderer_ResetDeviceState_Entry, Renderer_ResetDeviceState_Abi)
ABI_ENTRY_004ffd40(Renderer_Destroy_Entry, Renderer_Destroy_Abi)
ABI_ENTRY_004fffe0(Renderer_Create_Entry, Renderer_Create_Abi)
HOOK(0x004fe3f0, "Renderer_CreateVertexBufferSet", Renderer_CreateVertexBufferSet_Entry, nullptr);
HOOK(0x004fe650, "VertexBufferSet_Construct", VertexBufferSet_Construct_Entry, nullptr);
HOOK(0x004fe750, "Renderer_CreateIndexBuffer", Renderer_CreateIndexBuffer_Entry, nullptr);
HOOK(0x004fe7d0, "Renderer_ReleaseDynamicBuffers", Renderer_ReleaseDynamicBuffers_Entry, nullptr);
HOOK(0x004fe870, "Renderer_RestoreDynamicBuffers", Renderer_RestoreDynamicBuffers_Entry, nullptr);
HOOK(0x004fe970, "fn_004fe970", fn_004fe970_Entry, nullptr);
HOOK(0x004fe9b0, "fn_004fe9b0", fn_004fe9b0_Entry, nullptr);
HOOK(0x004fea60, "Renderer_SetProjection", Renderer_SetProjection_Entry, nullptr);
HOOK(0x004fef90, "fn_004fef90", fn_004fef90_Entry, nullptr);
HOOK(0x004ff020, "Renderer_ResetDeviceState", Renderer_ResetDeviceState_Entry, nullptr);
HOOK(0x004ffd40, "Renderer_Destroy", Renderer_Destroy_Entry, nullptr);
HOOK(0x004fffe0, "Renderer_Create", Renderer_Create_Entry, nullptr);
