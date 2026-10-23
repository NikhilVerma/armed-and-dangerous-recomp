// Recovered: renderer core (Direct3D 9 layer), 0x004fcda0..0x004fdcf0: draw state teardown, vertex buffer set
// helpers and the primitive interface methods that draw from it (gfx\d3d\buffer.cpp area). S2, Approach B.
// None of these run in the comparison scenarios yet.
//   0x004fcda0 fn_004fcda0
//   0x004fce00 DrawState_Destroy
//   0x004fd350 fn_004fd350
//   0x004fd360 fn_004fd360
//   0x004fd3e0 fn_004fd3e0
//   0x004fd450 fn_004fd450
//   0x004fd5d0 fn_004fd5d0
//   0x004fd880 fn_004fd880
//   0x004fd940 fn_004fd940
//   0x004fd980 fn_004fd980
//   0x004fd9b0 fn_004fd9b0
//   0x004fdaf0 fn_004fdaf0
//   0x004fdb20 fn_004fdb20
//   0x004fdb90 fn_004fdb90
//   0x004fdca0 IndexBuffer_Unreserve
//   0x004fdcf0 fn_004fdcf0
//
// Layout notes (evidence: vtables in .rdata). Vertex buffer set vtable appears to start at 0x005898e4 (slot 3 =
// 0x004fcda0, slot 5 = VertexBufferSet_Begin, slots 6-8 = 0x004fd3e0/0x004fd350/0x004fd360; virtual 8 is what
// DrawState_NotifyListeners and VertexBufferSet_MakeRoom call). The primitive interfaces reach the set as an MSVC
// virtual base: two interface vtables at 0x005898b8 and 0x0058994c (vbptr at -4: 0x004fd980..0x004fdcf0) and the
// interfaces with vbptr at +4 (0x004fd390, 0x004fd450, 0x004fd5d0). Index buffer vtable 0x00589b60 (slot 4 =
// 0x004fdca0). Draw state vtable 0x00589b78 (slot 0 = 0x004fce00).
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

uint32_t Renderer_PrepareDraw(uint8_t* item);                                     // auto_004fbe00.cpp
uint32_t VertexBufferSet_SetFormat(VertexBufferSet* set, uint32_t format);        // auto_004fb170.cpp
uint32_t Renderer_ApplyMaterial(Renderer* r, uint8_t* material);                  // loader\auto_00502cb0.cpp
uint32_t Renderer_UpdateMaterialConstants(Renderer* r, const uint8_t* material);  // render_states.cpp

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

constexpr uint32_t kGameHeap = 0x0063a210;  // ecx of 0x004e08d0
const char* const kStrVbAlreadyLocked = reinterpret_cast<const char*>(0x00589d38);
const char* const kStrHwVerticesInUse = reinterpret_cast<const char*>(0x00589d0c);
const char* const kStrUnreserveVbUnlocked = reinterpret_cast<const char*>(0x00589cdc);
const char* const kStrUnreserveIbUnlocked = reinterpret_cast<const char*>(0x00589c80);

constexpr uint32_t kLockNoOverwrite = 0x1000;  // D3DLOCK_NOOVERWRITE

// MSVC virtual inheritance: the subobject's vbptr sits at `sub + vbptrOffset`, and entry 1 of the vbtable is the
// offset from the vbptr to the virtual base (the vertex buffer set).
VertexBufferSet* VirtualBase(void* sub, int32_t vbptrOffset)
{
    uint8_t* vbptr = reinterpret_cast<uint8_t*>(sub) + vbptrOffset;
    return reinterpret_cast<VertexBufferSet*>(vbptr + (*reinterpret_cast<int32_t**>(vbptr))[1]);
}

// The set's flush callback (+0x38, 0x004fd220 once VertexBufferSet_Begin has run), called with ecx = thisArg.
uint32_t CallFlush(VertexBufferSet* set, void* thisArg)
{
    auto fn = reinterpret_cast<uint32_t(__fastcall*)(void*, void*)>(Field<uintptr_t>(set, 0x38));
    return fn(thisArg, nullptr);
}

// Unless the lock flags (+0x6c) already have NOOVERWRITE, advance to the next buffer (signed wrap at count), then
// Lock it whole with the flags into +0x48 and add NOOVERWRITE (same sequence as VertexBufferSet_Begin).
void LockNextBuffer(VertexBufferSet* set)
{
    if (!(set->field_6c & kLockNoOverwrite))
    {
        int32_t next = static_cast<int32_t>(set->current) + 1;
        set->current = static_cast<uint32_t>(next);
        if (next >= static_cast<int32_t>(set->count))
            set->current = 0;
    }
    uint32_t flags = set->field_6c;
    set->buffers[set->current]->Lock(0, 0, reinterpret_cast<void**>(&set->locked), flags);
    set->field_6c |= kLockNoOverwrite;
}

// The material pass loop shared by the draw methods: for each pass of the renderer's (set +0x0c) current material
// list (+0x3ac: byte count, 0x50-byte entries), apply the pass when it is not the current one (+0x3b0; stop when
// Renderer_ApplyMaterial returns false in al), update its constants and call draw(device). Returns what the
// original leaves in eax at the exit (unused by the callers).
template <class Draw>
uint32_t ForEachMaterialPass(VertexBufferSet* set, Draw draw)
{
    uint32_t offset = 0;
    for (uint32_t pass = 0;; ++pass, offset += 0x50)
    {
        Renderer* r = set->renderer;
        uint8_t* list = Field<uint8_t*>(r, 0x3ac);
        if (!list || pass >= list[0])
            return u(list);
        if (pass != Field<uint32_t>(r, 0x3b0))
        {
            Field<uint32_t>(r, 0x3b0) = pass;
            uint32_t ok = Renderer_ApplyMaterial(r, list + offset);
            if (!(ok & 0xff))
                return ok;
        }
        Renderer_UpdateMaterialConstants(r, Field<uint8_t*>(r, 0x3ac) + offset);
        draw(set->renderer->device);
    }
}
}  // namespace

// 0x004fcda0 (ecx = object; [draw state or null]) (not executed in the comparison scenarios): vertex buffer set
// virtual 3 (vtable 0x005898e4 slot 3), and what DrawState_Destroy calls with null on each listener. Unlinks the
// object's node (+0x10) from its list; with a draw state, appends it to the state's listener list (+0x48, the list
// DrawState_NotifyListeners walks with object = node - 0x10) and stores the state at +0x18. With null, leaves the
// node self-linked and stores [[object +0x0c] + 4] (the renderer's +4) at +0x18. Returns the node (with a state) or
// object +0x0c (null), as left in eax.
uint32_t fn_004fcda0(uint8_t* object, uint8_t* state)
{
    ListNode* node = &Field<ListNode>(object, 0x10);
    node->prev->next = node->next;
    node->next->prev = node->prev;
    if (state)
    {
        ListNode* list = &Field<ListNode>(state, 0x48);
        node->prev = list->prev;
        node->next = list;
        list->prev = node;
        node->prev->next = node;
        Field<uint8_t*>(object, 0x18) = state;
        return u(node);
    }
    node->next = node;
    node->prev = node;
    uint8_t* owner = Field<uint8_t*>(object, 0xc);
    Field<uint32_t>(object, 0x18) = Field<uint32_t>(owner, 4);
    return u(owner);
}

// 0x004fce00 (ecx = draw state) (not executed in the comparison scenarios): draw state virtual 0 (vtable
// 0x00589b78 slot 0), destroy. Calls virtual 3 (+0xc) with 0 on every listener in the list at +0x48 (object = node
// - 0x10; the next node is read before the call, which unlinks), unlinks the state's node (+4) from the renderer's
// list (Renderer_CreateDrawState links it at renderer +0x60) and frees the state with 0x004e08d0 on the game heap
// (0x0063a210). Returns that call's result.
uint32_t DrawState_Destroy(uint8_t* state)
{
    ListNode* head = &Field<ListNode>(state, 0x48);
    ListNode* node = head->next;
    ListNode* next = node->next;
    while (node != head)
    {
        VCall<uint32_t>(reinterpret_cast<uint8_t*>(node) - 0x10, 0xc / 4, 0u);
        node = next;
        next = next->next;
    }
    ListNode* own = &Field<ListNode>(state, 4);
    own->prev->next = own->next;
    own->next->prev = own->prev;
    own->next = own;
    own->prev = own;
    return orig::fn_004e08d0(kGameHeap, u(state));
}

// 0x004fd350 (ecx = vertex buffer set) (not executed in the comparison scenarios): vertex buffer set virtual 7.
// When an interface is in use (+0x80), tail-calls its virtual 0. Returns its result; otherwise the original returns
// the caller's eax untouched, here 0.
uint32_t fn_004fd350(VertexBufferSet* set)
{
    void* iface = Field<void*>(set, 0x80);
    if (!iface)
        return 0;
    return VCall<uint32_t>(iface, 0);
}

// 0x004fd360 (ecx = vertex buffer set) (not executed in the comparison scenarios): vertex buffer set virtual 8 (the
// draw-state listener notification). With a re-entrancy guard (byte 0x0064a709), calls the in-use interface's
// (+0x80) virtual 1 (+4). Returns the guard byte when re-entrant (left in al), the call's result, or 0.
uint32_t fn_004fd360(VertexBufferSet* set)
{
    uint8_t& guard = GameGlobal<uint8_t>(0x0064a709);
    if (guard != 0)
        return guard;
    void* iface = Field<void*>(set, 0x80);
    guard = 1;
    uint32_t result = 0;
    if (iface)
        result = VCall<uint32_t>(iface, 4 / 4);
    guard = 0;
    return result;
}

// 0x004fd3e0 (ecx = vertex buffer set) (not executed in the comparison scenarios): vertex buffer set virtual 6.
// Makes the interface object (+0x84) in use through its +0x18 part (+0x80 = object + 0x18, or null) and returns
// that part + 8 (object + 0x20), or null. Same pattern as the end of VertexBufferSet_Begin.
uint32_t fn_004fd3e0(VertexBufferSet* set)
{
    uint32_t iface = Field<uint32_t>(set, 0x84);
    Field<uint32_t>(set, 0x80) = iface ? iface + 0x18 : 0;
    if (iface == 0 || iface + 0x18 == 0)
        return 0;
    return iface + 0x20;
}

// 0x004fd450 (ecx = primitive interface; vbptr at +4, virtual base = vertex buffer set) (not executed in the
// comparison scenarios): last slot of the interface vtable at 0x00589948. Draw the vertices written since the last
// draw: start (+0x54) = +0x40, count (+0x58) = +0x44 - +0x40, run the flush callback (+0x38, ecx = set), and when
// count is non-zero draw them with DrawPrimitive(type +0x2c, start, count / +0x30 + +0x34) in every material pass.
// Then relock: logs "Error locking vertex buffer, already locked!" when locked (+0x48), else locks the next buffer
// (see LockNextBuffer). Finally +0x40 = +0x44. Returns set - 4 (left in eax).
uint32_t fn_004fd450(void* iface)
{
    VertexBufferSet* set = VirtualBase(iface, 4);
    Field<uint32_t>(set, 0x54) = Field<uint32_t>(set, 0x40);
    Field<int32_t>(set, 0x58) = Field<int32_t>(set, 0x44) - Field<int32_t>(set, 0x40);
    CallFlush(set, set);
    if (Field<int32_t>(set, 0x58) != 0)
    {
        ForEachMaterialPass(set, [set](IDirect3DDevice9* device) {
            int32_t primitives = Field<int32_t>(set, 0x58) / Field<int32_t>(set, 0x30) + Field<int32_t>(set, 0x34);
            device->DrawPrimitive(static_cast<D3DPRIMITIVETYPE>(Field<uint32_t>(set, 0x2c)),
                                  Field<uint32_t>(set, 0x54), static_cast<UINT>(primitives));
        });
    }
    if (set->locked != 0)
        GameLog(kStrVbAlreadyLocked);
    else
        LockNextBuffer(set);
    Field<uint32_t>(set, 0x40) = Field<uint32_t>(set, 0x44);
    return u(set) - 4;
}

// 0x004fd5d0 (ecx = primitive interface; vbptr at +4, virtual base = vertex buffer set) (not executed in the
// comparison scenarios): vtable entry at 0x00589918, next to 0x004fd8a0/0x004fd940. Like 0x004fd450, but the batch is a
// run of strips: the 16-bit vertex counts appended by 0x004fd8a0 between +0x4c and the write pointer +0x50. For each
// count (n = (end - start) >> 1, signed), draws DrawPrimitive(type +0x2c, start, count / +0x30 + +0x34) in every
// material pass and advances start by count. After relocking, resets the write pointer (+0x50 = +0x4c) and +0x40 =
// +0x44. Returns set - 4 (left in eax).
uint32_t fn_004fd5d0(void* iface)
{
    VertexBufferSet* set = VirtualBase(iface, 4);
    Field<uint32_t>(set, 0x54) = Field<uint32_t>(set, 0x40);
    Field<int32_t>(set, 0x58) = Field<int32_t>(set, 0x44) - Field<int32_t>(set, 0x40);
    CallFlush(set, set);
    if (Field<int32_t>(set, 0x58) != 0)
    {
        const uint16_t* counts = Field<const uint16_t*>(set, 0x4c);
        int32_t n = static_cast<int32_t>(Field<uint32_t>(set, 0x50) - u(counts)) >> 1;
        uint32_t start = Field<uint32_t>(set, 0x54);
        for (; n != 0; --n)
        {
            ForEachMaterialPass(set, [set, counts, start](IDirect3DDevice9* device) {
                int32_t primitives = static_cast<int32_t>(*counts) / Field<int32_t>(set, 0x30) +
                                     Field<int32_t>(set, 0x34);
                device->DrawPrimitive(static_cast<D3DPRIMITIVETYPE>(Field<uint32_t>(set, 0x2c)), start,
                                      static_cast<UINT>(primitives));
            });
            start += *counts++;
        }
    }
    if (set->locked != 0)
        GameLog(kStrVbAlreadyLocked);
    else
        LockNextBuffer(set);
    Field<uint32_t>(set, 0x50) = Field<uint32_t>(set, 0x4c);
    Field<uint32_t>(set, 0x40) = Field<uint32_t>(set, 0x44);
    return u(set) - 4;
}

// 0x004fd880 (ecx = primitive interface; vbptr at -4; [count]) (not executed in the comparison scenarios): give back
// `count` vertices reserved by 0x004fd7f0: the set's used count (+0x44) -= count, unchecked. Returns &set->+0x44
// (left in eax).
uint32_t fn_004fd880(void* iface, int32_t count)
{
    VertexBufferSet* set = VirtualBase(iface, -4);
    Field<int32_t>(set, 0x44) -= count;
    return u(set) + 0x44;
}

// 0x004fd940 (ecx = primitive interface; vbptr at -4; [count]) (not executed in the comparison scenarios): give back
// `count` vertices reserved by 0x004fd8a0: used count (+0x44) -= count and the last strip count (word before the
// write pointer +0x50) -= count; when that word becomes 0 it is dropped (write pointer -= 2). Returns &set->+0x50.
uint32_t fn_004fd940(void* iface, int32_t count)
{
    VertexBufferSet* set = VirtualBase(iface, -4);
    Field<int32_t>(set, 0x44) -= count;
    Field<uint16_t*>(set, 0x50)[-1] -= static_cast<uint16_t>(count);
    uint16_t* last = Field<uint16_t*>(set, 0x50) - 1;
    if (*last == 0)
        Field<uint16_t*>(set, 0x50) = last;
    return u(set) + 0x50;
}

// 0x004fd980 (ecx = primitive interface; vbptr at -4; [vertices]) (not executed in the comparison scenarios):
// interface slot 0 (vtables 0x005898b8 and 0x0058994c). Logs "Error setting HW vertices, already in use!" when the
// set is locked (+0x48); otherwise stores the argument at +0x7c (cleared by 0x004fd410 and 0x004fd9b0). Returns
// set + 4 (left in eax); after the log the original returns the log function's result, here 0.
uint32_t fn_004fd980(void* iface, uint32_t vertices)
{
    VertexBufferSet* set = VirtualBase(iface, -4);
    if (set->locked != 0)
    {
        GameLog(kStrHwVerticesInUse);
        return 0;
    }
    Field<uint32_t>(set, 0x7c) = vertices;
    return u(set) + 4;
}

// 0x004fd9b0 (ecx = primitive interface; vbptr at -4; [vertex format, count]) (not executed in the comparison
// scenarios): interface slot 1. Lock `count` vertices of a format for writing. Logs "Error locking vertex buffer,
// already locked!" and returns null when locked (+0x48). Clears +0x7c, sets the format (VertexBufferSet_SetFormat);
// when used (+0x44) + count exceeds capacity (+0x3c, signed) drops NOOVERWRITE (+0x6c), calls the set's virtual 8
// (+0x20) and restarts at 0 (+0x40/+0x44), returning null if it still does not fit. Locks the next buffer (see
// LockNextBuffer), records the start (+0x40 = used), advances used by count and returns the locked data (+0x48) +
// stride (+0x5c) * start.
uint32_t fn_004fd9b0(void* iface, uint32_t format, int32_t count)
{
    VertexBufferSet* set = VirtualBase(iface, -4);
    if (set->locked != 0)
    {
        GameLog(kStrVbAlreadyLocked);
        return 0;
    }
    Field<uint32_t>(set, 0x7c) = 0;
    VertexBufferSet_SetFormat(set, format);
    if (Field<int32_t>(set, 0x44) + count > Field<int32_t>(set, 0x3c))
    {
        set->field_6c &= ~kLockNoOverwrite;
        VCall<uint32_t>(set, 0x20 / 4);
        Field<uint32_t>(set, 0x40) = 0;
        Field<uint32_t>(set, 0x44) = 0;
        if (Field<int32_t>(set, 0x44) + count > Field<int32_t>(set, 0x3c))
            return 0;
    }
    LockNextBuffer(set);
    int32_t used = Field<int32_t>(set, 0x44);
    uint32_t stride = Field<uint32_t>(set, 0x5c);
    uint32_t data = set->locked;
    Field<int32_t>(set, 0x40) = used;
    Field<int32_t>(set, 0x44) += count;
    return stride * static_cast<uint32_t>(used) + data;
}

// 0x004fdaf0 (ecx = primitive interface; vbptr at -4; [count]) (not executed in the comparison scenarios):
// interface slot 2, unreserve. When the set is locked (+0x48), used (+0x44) -= count and returns set + 4 (left in
// eax); otherwise logs "Error unreserve from a unlocked vertex buffer!" (original returns the log result, here 0).
uint32_t fn_004fdaf0(void* iface, int32_t count)
{
    VertexBufferSet* set = VirtualBase(iface, -4);
    if (set->locked == 0)
    {
        GameLog(kStrUnreserveVbUnlocked);
        return 0;
    }
    Field<int32_t>(set, 0x44) -= count;
    return u(set) + 4;
}

namespace
{
// Tail of 0x004fdb20/0x004fdb90: when the flush argument differs from +0x24, store it and the flush callback
// 0x004fd220 (+0x38); then call the callback. The original passes ecx = null when the interface pointer is 8
// (the null check of the derived-to-base conversion, complete object = iface - 8).
uint32_t SetFlushTargetAndFlush(void* iface, VertexBufferSet* set, uint32_t target)
{
    if (Field<uint32_t>(set, 0x24) != target)
    {
        Field<uint32_t>(set, 0x24) = target;
        Field<uint32_t>(set, 0x38) = 0x004fd220;
    }
    void* thisArg = u(iface) == 8 ? nullptr : static_cast<void*>(set);
    return CallFlush(set, thisArg);
}
}  // namespace

// 0x004fdb20 (ecx = primitive interface; vbptr at -4; [flush argument]) (not executed in the comparison scenarios):
// interface slot 3. Marks everything written since the last draw as pending: start (+0x54) = +0x40, count (+0x58) =
// +0x44 - +0x40; then sets the flush argument (+0x24, as VertexBufferSet_Begin's last argument) and runs the flush
// callback (+0x38). Returns the callback's result.
uint32_t fn_004fdb20(void* iface, uint32_t target)
{
    VertexBufferSet* set = VirtualBase(iface, -4);
    Field<uint32_t>(set, 0x54) = Field<uint32_t>(set, 0x40);
    Field<int32_t>(set, 0x58) = Field<int32_t>(set, 0x44) - Field<int32_t>(set, 0x40);
    return SetFlushTargetAndFlush(iface, set, target);
}

// 0x004fdb90 (ecx = primitive interface; vbptr at -4; [first, count, flush argument]) (not executed in the comparison
// scenarios): interface slot 4. Like 0x004fdb20 for an explicit range: start (+0x54) = +0x40 + first, count (+0x58)
// = count. Returns the flush callback's result.
uint32_t fn_004fdb90(void* iface, int32_t first, uint32_t count, uint32_t target)
{
    VertexBufferSet* set = VirtualBase(iface, -4);
    Field<int32_t>(set, 0x54) = Field<int32_t>(set, 0x40) + first;
    Field<uint32_t>(set, 0x58) = count;
    return SetFlushTargetAndFlush(iface, set, target);
}

// 0x004fdca0 (ecx = index buffer; [count]) (not executed in the comparison scenarios): index buffer virtual 4
// (vtable 0x00589b60, between IndexBuffer_Lock and IndexBuffer_Unlock). When locked (+0x1c), used (+0x18) -= count
// and returns count (left in eax); otherwise logs "Error unreserve from a unlocked index buffer!" (original returns
// the log result, here 0).
uint32_t IndexBuffer_Unreserve(IndexBuffer* ib, int32_t count)
{
    if (ib->locked == 0)
    {
        GameLog(kStrUnreserveIbUnlocked);
        return 0;
    }
    Field<int32_t>(ib, 0x18) -= count;
    return static_cast<uint32_t>(count);
}

// 0x004fdcf0 (ecx = primitive interface; vbptr at -4; [primitive class, primitive kind, index count, base vertex,
// index buffer]) (not executed in the comparison scenarios): interface slot 7, indexed draw (0x004fdeb0 calls it
// through virtual 8 with the renderer's shared index buffer). Base vertex = +0x40 + argument; Renderer_PrepareDraw
// (set); then +0x1c/+0x20 = class/kind and +0x2c/+0x30/+0x34 from the tables 0x005fb868/0x005fb828/0x005fb7e8 at
// [kind + class * 4] (as VertexBufferSet_Begin), SetIndices(index buffer +0x20), and in every material pass
// DrawIndexedPrimitive(type +0x2c, base vertex, 0, +0x44 - base vertex, index buffer start +0x14,
// index count / +0x30 + +0x34). Returns what the pass loop leaves in eax.
uint32_t fn_004fdcf0(void* iface, int32_t primClass, int32_t primKind, int32_t indexCount, int32_t baseVertexOffset,
                     IndexBuffer* ib)
{
    VertexBufferSet* set = VirtualBase(iface, -4);
    int32_t baseVertex = Field<int32_t>(set, 0x40) + baseVertexOffset;
    Renderer_PrepareDraw(reinterpret_cast<uint8_t*>(set));
    Field<int32_t>(set, 0x20) = primKind;
    Field<int32_t>(set, 0x1c) = primClass;
    int32_t index = primKind + primClass * 4;
    Field<uint32_t>(set, 0x2c) = reinterpret_cast<const uint32_t*>(0x005fb868)[index];
    Field<uint32_t>(set, 0x30) = reinterpret_cast<const uint32_t*>(0x005fb828)[index];
    Field<uint32_t>(set, 0x34) = reinterpret_cast<const uint32_t*>(0x005fb7e8)[index];
    set->renderer->device->SetIndices(ib->buffer);
    return ForEachMaterialPass(set, [set, ib, baseVertex, indexCount](IDirect3DDevice9* device) {
        int32_t primitives = indexCount / Field<int32_t>(set, 0x30) + Field<int32_t>(set, 0x34);
        device->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(Field<uint32_t>(set, 0x2c)), baseVertex, 0,
                                     static_cast<UINT>(Field<int32_t>(set, 0x44) - baseVertex),
                                     Field<uint32_t>(ib, 0x14), static_cast<UINT>(primitives));
    });
}

// ---- ABI adapters and hooks ----

static uint32_t fn_004fcda0_Abi(uint32_t ecx, uint32_t state)
{
    return fn_004fcda0(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(state));
}
static uint32_t DrawState_Destroy_Abi(uint32_t ecx)
{
    return DrawState_Destroy(reinterpret_cast<uint8_t*>(ecx));
}
static uint32_t fn_004fd350_Abi(uint32_t ecx)
{
    return fn_004fd350(reinterpret_cast<VertexBufferSet*>(ecx));
}
static uint32_t fn_004fd360_Abi(uint32_t ecx)
{
    return fn_004fd360(reinterpret_cast<VertexBufferSet*>(ecx));
}
static uint32_t fn_004fd3e0_Abi(uint32_t ecx)
{
    return fn_004fd3e0(reinterpret_cast<VertexBufferSet*>(ecx));
}
static uint32_t fn_004fd450_Abi(uint32_t ecx)
{
    return fn_004fd450(reinterpret_cast<void*>(ecx));
}
static uint32_t fn_004fd5d0_Abi(uint32_t ecx)
{
    return fn_004fd5d0(reinterpret_cast<void*>(ecx));
}
static uint32_t fn_004fd880_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fd880(reinterpret_cast<void*>(ecx), static_cast<int32_t>(a0));
}
static uint32_t fn_004fd940_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fd940(reinterpret_cast<void*>(ecx), static_cast<int32_t>(a0));
}
static uint32_t fn_004fd980_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fd980(reinterpret_cast<void*>(ecx), a0);
}
static uint32_t fn_004fd9b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004fd9b0(reinterpret_cast<void*>(ecx), a0, static_cast<int32_t>(a1));
}
static uint32_t fn_004fdaf0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fdaf0(reinterpret_cast<void*>(ecx), static_cast<int32_t>(a0));
}
static uint32_t fn_004fdb20_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fdb20(reinterpret_cast<void*>(ecx), a0);
}
static uint32_t fn_004fdb90_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004fdb90(reinterpret_cast<void*>(ecx), static_cast<int32_t>(a0), a1, a2);
}
static uint32_t IndexBuffer_Unreserve_Abi(uint32_t ecx, uint32_t a0)
{
    return IndexBuffer_Unreserve(reinterpret_cast<IndexBuffer*>(ecx), static_cast<int32_t>(a0));
}
static uint32_t fn_004fdcf0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004fdcf0(reinterpret_cast<void*>(ecx), static_cast<int32_t>(a0), static_cast<int32_t>(a1),
                       static_cast<int32_t>(a2), static_cast<int32_t>(a3), reinterpret_cast<IndexBuffer*>(a4));
}

ABI_ENTRY_004fcda0(fn_004fcda0_Entry, fn_004fcda0_Abi)
ABI_ENTRY_004fce00(DrawState_Destroy_Entry, DrawState_Destroy_Abi)
ABI_ENTRY_004fd350(fn_004fd350_Entry, fn_004fd350_Abi)
ABI_ENTRY_004fd360(fn_004fd360_Entry, fn_004fd360_Abi)
ABI_ENTRY_004fd3e0(fn_004fd3e0_Entry, fn_004fd3e0_Abi)
ABI_ENTRY_004fd450(fn_004fd450_Entry, fn_004fd450_Abi)
ABI_ENTRY_004fd5d0(fn_004fd5d0_Entry, fn_004fd5d0_Abi)
ABI_ENTRY_004fd880(fn_004fd880_Entry, fn_004fd880_Abi)
ABI_ENTRY_004fd940(fn_004fd940_Entry, fn_004fd940_Abi)
ABI_ENTRY_004fd980(fn_004fd980_Entry, fn_004fd980_Abi)
ABI_ENTRY_004fd9b0(fn_004fd9b0_Entry, fn_004fd9b0_Abi)
ABI_ENTRY_004fdaf0(fn_004fdaf0_Entry, fn_004fdaf0_Abi)
ABI_ENTRY_004fdb20(fn_004fdb20_Entry, fn_004fdb20_Abi)
ABI_ENTRY_004fdb90(fn_004fdb90_Entry, fn_004fdb90_Abi)
ABI_ENTRY_004fdca0(IndexBuffer_Unreserve_Entry, IndexBuffer_Unreserve_Abi)
ABI_ENTRY_004fdcf0(fn_004fdcf0_Entry, fn_004fdcf0_Abi)

HOOK(0x004fcda0, "fn_004fcda0", fn_004fcda0_Entry, nullptr);
HOOK(0x004fce00, "DrawState_Destroy", DrawState_Destroy_Entry, nullptr);
HOOK(0x004fd350, "fn_004fd350", fn_004fd350_Entry, nullptr);
HOOK(0x004fd360, "fn_004fd360", fn_004fd360_Entry, nullptr);
HOOK(0x004fd3e0, "fn_004fd3e0", fn_004fd3e0_Entry, nullptr);
HOOK(0x004fd450, "fn_004fd450", fn_004fd450_Entry, nullptr);
HOOK(0x004fd5d0, "fn_004fd5d0", fn_004fd5d0_Entry, nullptr);
HOOK(0x004fd880, "fn_004fd880", fn_004fd880_Entry, nullptr);
HOOK(0x004fd940, "fn_004fd940", fn_004fd940_Entry, nullptr);
HOOK(0x004fd980, "fn_004fd980", fn_004fd980_Entry, nullptr);
HOOK(0x004fd9b0, "fn_004fd9b0", fn_004fd9b0_Entry, nullptr);
HOOK(0x004fdaf0, "fn_004fdaf0", fn_004fdaf0_Entry, nullptr);
HOOK(0x004fdb20, "fn_004fdb20", fn_004fdb20_Entry, nullptr);
HOOK(0x004fdb90, "fn_004fdb90", fn_004fdb90_Entry, nullptr);
HOOK(0x004fdca0, "IndexBuffer_Unreserve", IndexBuffer_Unreserve_Entry, nullptr);
HOOK(0x004fdcf0, "fn_004fdcf0", fn_004fdcf0_Entry, nullptr);
