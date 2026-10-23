// Recovered: renderer core (Direct3D 9 layer), 0x004fb170..0x004fe2a0: draw states, dynamic vertex/index
// buffers (gfx\d3d\buffer.cpp area) and a few helpers around them. S2, Approach B.
//   0x004fb170 fn_004fb170
//   0x004fb7c0 fn_004fb7c0
//   0x004fb860 fn_004fb860
//   0x004fb920 fn_004fb920
//   0x004fba30 fn_004fba30
//   0x004fbab0 fn_004fbab0
//   0x004fca00 SkinMesh_CheckTexCoordChannels
//   0x004fce70 DrawState_NotifyListeners
//   0x004fced0 DrawState_Set
//   0x004fcf20 DrawState_SetCullMode
//   0x004fcf50 DrawState_SetClipping
//   0x004fcf80 DrawState_SetWords
//   0x004fcfc0 DrawState_SetWordsAndColors
//   0x004fd000 Renderer_CreateDrawState
//   0x004fd220 VertexBufferSet_Flush
//   0x004fd250 VertexBufferSet_Begin
//   0x004fd390 fn_004fd390
//   0x004fd410 fn_004fd410
//   0x004fd7b0 fn_004fd7b0
//   0x004fd7f0 fn_004fd7f0
//   0x004fd8a0 fn_004fd8a0
//   0x004fdc10 IndexBuffer_Lock
//   0x004fdcd0 IndexBuffer_Unlock
//   0x004fdeb0 fn_004fdeb0
//   0x004fe130 VertexBufferSet_ReleaseBuffers
//   0x004fe230 IndexBuffer_Destroy
//   0x004fe2a0 VertexBufferSet_SetFormat
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

uint32_t Renderer_PrepareDraw(uint8_t* item);  // auto_004fbe00.cpp

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

constexpr uint32_t kGameHeap = 0x0063a210;                                         // ecx of 0x004e07f0/0x004e08d0
const char* const kStrAllocTag = reinterpret_cast<const char*>(0x0058b5e8);       // 4th argument of 0x004e07f0
const char* const kStrPrimBufferInUse = reinterpret_cast<const char*>(0x00589d68);
const char* const kStrVbAlreadyLocked = reinterpret_cast<const char*>(0x00589d38);
const char* const kStrIbAlreadyLocked = reinterpret_cast<const char*>(0x00589cb0);
const char* const kStrReleaseVbLocked = reinterpret_cast<const char*>(0x00589c30);
const char* const kStrReleaseIbLocked = reinterpret_cast<const char*>(0x00589c08);

constexpr uint32_t kLockNoOverwrite = 0x1000;  // D3DLOCK_NOOVERWRITE

// MSVC virtual inheritance: the subobject's vbptr sits at `sub + vbptrOffset`, and entry 1 of the vbtable is the
// offset from the vbptr to the virtual base.
uint8_t* VirtualBase(void* sub, int32_t vbptrOffset)
{
    uint8_t* vbptr = reinterpret_cast<uint8_t*>(sub) + vbptrOffset;
    return vbptr + (*reinterpret_cast<int32_t**>(vbptr))[1];
}
}  // namespace

// 0x004fb170 (ecx = object with vtable; [item]): a float sum. The item's +0x0c points at a record whose flags
// (+0x20) pick the base: bit 25 -> object +0x1c, bits 9-10 -> +0x20, bit 30 clear -> +0x18, else 0.0 (0x0056c05c);
// unless bit 23 is set, object +0x24 is added. Then item +0x10, +0x14 and +0x90 are added, and when item byte +0
// has bit 4, the object's virtual 43 (+0xac, returns float) called with (item +0x60, record +0x30, record +0x34) is
// added too. The result is returned in st0 (fld/fadd before ret), although symbols/abi.csv lists eax: the
// adapter returns float so st0 is set, as the disassembly does.
float fn_004fb170(void* object, uint8_t* item)
{
    uint8_t* record = Field<uint8_t*>(item, 0xc);
    uint32_t flags = Field<uint32_t>(record, 0x20);
    float sum;
    if (flags & 0x2000000)
        sum = Field<float>(object, 0x1c);
    else if (flags & 0x600)
        sum = Field<float>(object, 0x20);
    else if (!(flags & 0x40000000))
        sum = Field<float>(object, 0x18);
    else
        sum = GameGlobal<float>(0x0056c05c);
    if (!(flags & 0x800000))
        sum = sum + Field<float>(object, 0x24);
    sum = sum + Field<float>(item, 0x10);
    sum = sum + Field<float>(item, 0x14);
    sum = sum + Field<float>(item, 0x90);
    if (!(item[0] & 0x10))
        return sum;
    float extra = VCall<float>(object, 0xac / 4, Field<uint32_t>(item, 0x60), Field<uint32_t>(record, 0x30),
                               Field<uint32_t>(record, 0x34));
    return extra + sum;
}

// 0x004fb7c0 (ecx = object; [flags]) (not executed in the comparison scenarios): scalar deleting destructor shape.
// Sets the vtable to 0x0058a204, unlinks the object (+4 = a, +8 = b: a->+8 = b when a is set, then *b = a) and,
// when flags bit 0 is set, frees it with 0x004e08d0 on the game heap (0x0063a210). Returns the object.
void* fn_004fb7c0(uint8_t* object, uint32_t flags)
{
    Field<uint32_t>(object, 0) = 0x0058a204;
    if (Field<uint8_t*>(object, 4))
        Field<uint32_t>(Field<uint8_t*>(object, 4), 8) = Field<uint32_t>(object, 8);
    Field<uint32_t>(Field<uint8_t*>(object, 8), 0) = Field<uint32_t>(object, 4);
    if (flags & 1)
        orig::fn_004e08d0(kGameHeap, u(object));
    return object;
}

// 0x004fb860 (eax = saved-state block, esi = object with vtable, edi = second object; [a0, a1]) (not executed in
// the comparison scenarios): save the four dwords returned by esi's virtual 31 (+0x7c) into block +0xdc, call
// 0x004f0170(esi, block + 0x10), virtual 29 (+0x74) with 0, 0x004f01c0(esi), virtual 41 (+0xa4) with
// (0, 0, s, 2, s) for s = 0..3 and virtual 51 (+0xcc) with (a0, 0, 0, 0). Then edi's virtual 5 (+0x14) with a1
// and returns edi's virtual 3 (+0xc) with 0. 0x004fb920 is its counterpart (restores the block).
uint32_t fn_004fb860(uint8_t* block, void* object, void* other, uint32_t a0, uint32_t a1)
{
    const uint32_t* saved = VCall<const uint32_t*>(object, 0x7c / 4);
    Field<uint32_t>(block, 0xdc) = saved[0];
    Field<uint32_t>(block, 0xe0) = saved[1];
    Field<uint32_t>(block, 0xe4) = saved[2];
    Field<uint32_t>(block, 0xe8) = saved[3];
    orig::fn_004f0170(u(object), u(block) + 0x10);
    VCall<uint32_t>(object, 0x74 / 4, 0u);
    orig::fn_004f01c0(u(object));
    for (uint32_t s = 0; s < 4; ++s)
        VCall<uint32_t>(object, 0xa4 / 4, 0u, 0u, s, 2u, s);
    VCall<uint32_t>(object, 0xcc / 4, a0, 0u, 0u, 0u);
    VCall<uint32_t>(other, 0x14 / 4, a1);
    return VCall<uint32_t>(other, 0xc / 4, 0u);
}

// 0x004fb920 (eax = saved-state block, esi = object with vtable; [a0]) (not executed in the comparison scenarios):
// hand block +0x10, +0x54, +0x98 and +0xdc to esi's virtuals 21 (+0x54), 20 (+0x50), 19 (+0x4c) and 29 (+0x74),
// then virtual 41 (+0xa4) with (0, 0, 0, 0, s) for s = 0..3. When a0 is non-zero returns virtual 51 (+0xcc) with
// (0, 0, 0, 0), else 0 (eax = a0).
uint32_t fn_004fb920(uint8_t* block, void* object, uint32_t a0)
{
    VCall<uint32_t>(object, 0x54 / 4, block + 0x10);
    VCall<uint32_t>(object, 0x50 / 4, block + 0x54);
    VCall<uint32_t>(object, 0x4c / 4, block + 0x98);
    VCall<uint32_t>(object, 0x74 / 4, block + 0xdc);
    for (uint32_t s = 0; s < 4; ++s)
        VCall<uint32_t>(object, 0xa4 / 4, 0u, 0u, 0u, 0u, s);
    if (a0 == 0)
        return 0;
    return VCall<uint32_t>(object, 0xcc / 4, 0u, 0u, 0u, 0u);
}

// 0x004fba30 (no inputs): singleton at 0x0063a0b8. Created on first use: 0x14 bytes from the game heap
// (0x004e07f0), +4..+0x10 zeroed, vtable 0x0058982c. Returns it (null if the allocation failed).
uint32_t fn_004fba30()
{
    uint32_t& instance = GameGlobal<uint32_t>(0x0063a0b8);
    if (instance != 0)
        return instance;
    uint8_t* p = reinterpret_cast<uint8_t*>(orig::fn_004e07f0(kGameHeap, 0x10, 0x14, 2, u(kStrAllocTag)));
    if (!p)
    {
        instance = 0;
        return 0;
    }
    Field<uint32_t>(p, 4) = 0;
    Field<uint32_t>(p, 8) = 0;
    Field<uint32_t>(p, 0xc) = 0;
    Field<uint32_t>(p, 0) = 0x0058982c;
    Field<uint32_t>(p, 0x10) = 0;
    instance = u(p);
    return instance;
}

// 0x004fbab0 (no inputs): reference-counted singleton at 0x0063a0b4 (App +0x60 holds it, 0x004e3fc0). Allocated
// on first use (4 bytes from the game heap, 0x004e07f0); the count at +0 is incremented on every call. Returns it.
uint32_t fn_004fbab0()
{
    uint32_t p = GameGlobal<uint32_t>(0x0063a0b4);
    if (p == 0)
        p = orig::fn_004e07f0(kGameHeap, 0x10, 4, 2, u(kStrAllocTag));
    GameGlobal<uint32_t>(0x0063a0b4) = p;
    ++Field<uint32_t>(reinterpret_cast<void*>(p), 0);
    return p;
}

// 0x004fca00 ([binding, channels in use, expected channels, material name], stdcall; renderer vtable slot 97
// +0x184, ecx unused): check that a skinned mesh has the texture-coordinate channels its stages expect. Null
// binding -> false. If already checked (+8 bit 1) returns whether it passed (bits 1-2 == 2). Otherwise clears bits
// 1-2; needs both arrays and binding +4 -> +4 -> +0x44 (vertex layout), else false. Marks checked, then for each of
// 4 stages whose expected channel (signed) is <= 3 and whose in-use word (+8 + 4i) is set, a missing channel
// (layout bytes +0xd3/+0xd7/+0xdb/+0xdf zero) sets bit 2 and logs to "skinmesh.err" (0x004e0c60) the skin name
// (+4 -> +0x34), mesh name (0x004eb280), material name (or "unknown") and channel. Returns the pass result.
uint32_t SkinMesh_CheckTexCoordChannels(uint8_t* binding, uint8_t* inUse, const int32_t* expected,
                                        const char* materialName)
{
    if (!binding)
        return 0;
    uint32_t& flags = Field<uint32_t>(binding, 8);
    if (!(flags & 2))
    {
        flags &= 0xfffffff9;
        if (!inUse || !expected)
            return 0;
        uint8_t* owner = Field<uint8_t*>(binding, 4);
        if (!owner)
            return 0;
        uint8_t* skin = Field<uint8_t*>(owner, 4);
        if (!skin)
            return 0;
        uint8_t* layout = Field<uint8_t*>(skin, 0x44);
        if (!layout)
            return 0;
        flags |= 2;
        for (uint32_t i = 0; i < 4; ++i)
        {
            int32_t channel = expected[i];
            if (channel > 3 || Field<uint32_t>(inUse, 8 + i * 4) == 0)
                continue;
            bool missing = (channel == 0 && layout[0xd3] == 0) || (channel == 1 && layout[0xd7] == 0) ||
                           (channel == 2 && layout[0xdb] == 0) || (channel == 3 && layout[0xdf] == 0);
            if (!missing)
                continue;
            flags |= 4;
            const char* name = materialName ? materialName : reinterpret_cast<const char*>(0x005b0854);
            uint8_t* owner2 = Field<uint8_t*>(binding, 4);
            uint8_t* skin2 = Field<uint8_t*>(owner2, 4);
            uint32_t meshName = orig::fn_004eb280(u(owner2), u(skin2));
            orig::fn_004e0c60(0x00589df8 /* "skinmesh.err" */, 0x00589e08 /* "(Skin,Mesh,Mat) ..." */,
                              Field<uint32_t>(skin2, 4) + 0x34, meshName, u(name), static_cast<uint32_t>(expected[i]));
        }
    }
    return (flags & 6) == 2 ? 1 : 0;
}

// 0x004fce70 (ecx = draw state): with a re-entrancy guard (byte 0x0064a70a), call virtual 8 (+0x20) of every
// listener in the draw state's list at +0x48 (the object is node - 0x10). Returns 0, or the guard byte when the
// call was re-entrant (the original leaves it in al).
uint32_t DrawState_NotifyListeners(uint8_t* state)
{
    uint8_t& guard = GameGlobal<uint8_t>(0x0064a70a);
    if (guard != 0)
        return guard;
    ListNode* head = &Field<ListNode>(state, 0x48);
    ListNode* node = head->next;
    ListNode* next = node->next;
    guard = 1;
    while (node != head)
    {
        VCall<uint32_t>(reinterpret_cast<uint8_t*>(node) - 0x10, 0x20 / 4);
        node = next;
        next = next->next;
    }
    guard = 0;
    return 0;
}

// The draw state's renderer (+0x0c) remembers the id (+0x10) of the state it applied last at +8 (see
// Renderer_PrepareDraw); a setter that changes the state clears it so the state is applied again.
namespace
{
uint8_t* DrawState_InvalidateIfCurrent(uint8_t* state)
{
    uint8_t* r = Field<uint8_t*>(state, 0xc);
    if (Field<uint32_t>(r, 8) == Field<uint32_t>(state, 0x10))
        Field<uint32_t>(r, 8) = 0;
    return r;
}
}  // namespace

// 0x004fced0 (ecx = draw state; [flags, stage 0 word, texture factor]): draw state virtual 2. Notify listeners,
// then when any differs store flags (+0x14), stage 0 word (+0x1c) and texture factor (+0x18), clear stage words
// 1-3 (+0x20..+0x28) and invalidate the renderer's current state. Field meanings from Renderer_ApplyDrawState
// (0x00504940, which reads them at state + 0x10 + 4/8/0xc..). Returns the renderer.
uint8_t* DrawState_Set(uint8_t* state, uint32_t flags, uint32_t stage0, uint32_t textureFactor)
{
    DrawState_NotifyListeners(state);
    uint8_t* r = Field<uint8_t*>(state, 0xc);
    if (Field<uint32_t>(state, 0x1c) != stage0 || Field<uint32_t>(state, 0x14) != flags ||
        Field<uint32_t>(state, 0x18) != textureFactor)
    {
        Field<uint32_t>(state, 0x1c) = stage0;
        Field<uint32_t>(state, 0x14) = flags;
        Field<uint32_t>(state, 0x28) = 0;
        Field<uint32_t>(state, 0x24) = 0;
        Field<uint32_t>(state, 0x20) = 0;
        Field<uint32_t>(state, 0x18) = textureFactor;
        if (Field<uint32_t>(r, 8) == Field<uint32_t>(state, 0x10))
            Field<uint32_t>(r, 8) = 0;
    }
    return r;
}

// 0x004fcf20 (ecx = draw state; [cull]): draw state virtual 3. Notify listeners; store the cull mode index (+0x40,
// Renderer_ApplyDrawState maps it through table 0x005ca798 to D3DRS_CULLMODE) and invalidate when it changes.
// Returns the renderer.
uint8_t* DrawState_SetCullMode(uint8_t* state, uint32_t cull)
{
    DrawState_NotifyListeners(state);
    uint8_t* r = Field<uint8_t*>(state, 0xc);
    if (Field<uint32_t>(state, 0x40) != cull)
    {
        Field<uint32_t>(state, 0x40) = cull;
        DrawState_InvalidateIfCurrent(state);
    }
    return r;
}

// 0x004fcf50 (ecx = draw state; [value]): draw state virtual 4. Notify listeners; store +0x44 (applied as render
// state 0x88, D3DRS_CLIPPING, by Renderer_ApplyDrawState) and invalidate when it changes. Returns the renderer.
uint8_t* DrawState_SetClipping(uint8_t* state, uint32_t value)
{
    DrawState_NotifyListeners(state);
    uint8_t* r = Field<uint8_t*>(state, 0xc);
    if (Field<uint32_t>(state, 0x44) != value)
    {
        Field<uint32_t>(state, 0x44) = value;
        DrawState_InvalidateIfCurrent(state);
    }
    return r;
}

// 0x004fcf80 (ecx = draw state; [6 dwords]): draw state virtual 5. Notify listeners, copy flags, texture factor
// and the 4 stage words (+0x14..+0x28) and invalidate unconditionally. Returns the renderer.
uint8_t* DrawState_SetWords(uint8_t* state, const uint32_t* words)
{
    DrawState_NotifyListeners(state);
    uint8_t* r = Field<uint8_t*>(state, 0xc);
    for (uint32_t i = 0; i < 6; ++i)
        Field<uint32_t>(state, 0x14 + i * 4) = words[i];
    DrawState_InvalidateIfCurrent(state);
    return r;
}

// 0x004fcfc0 (ecx = draw state; [6 dwords, 5 dwords]): draw state virtual 6. Like 0x004fcf80, plus 5 dwords at
// +0x2c..+0x3c (read by the material colour update when flags bit 0 is set). Returns the renderer.
uint8_t* DrawState_SetWordsAndColors(uint8_t* state, const uint32_t* words, const uint32_t* extra)
{
    DrawState_NotifyListeners(state);
    uint8_t* r = Field<uint8_t*>(state, 0xc);
    for (uint32_t i = 0; i < 6; ++i)
        Field<uint32_t>(state, 0x14 + i * 4) = words[i];
    for (uint32_t i = 0; i < 5; ++i)
        Field<uint32_t>(state, 0x2c + i * 4) = extra[i];
    DrawState_InvalidateIfCurrent(state);
    return r;
}

// 0x004fd000 (eax = renderer; called from 0x004fffe0): create a draw state: 0x50 bytes from the game heap,
// vtable 0x00589b78 (slots 2-6 are the setters above), node +4 appended to the renderer's list at +0x60, empty
// listener list +0x48, renderer +0x0c, 14 default dwords from 0x005ca7e0 at +0x10, then a fresh non-zero id from
// the counter 0x0063a0ac at +0x10. Finally virtual 3 (cull) and virtual 4 (clipping) with 1. Returns the state.
// A failed allocation is not handled (the original writes through null).
uint8_t* Renderer_CreateDrawState(uint8_t* r)
{
    uint8_t* state = reinterpret_cast<uint8_t*>(orig::fn_004e07f0(kGameHeap, 0x10, 0x50, 2, u(kStrAllocTag)));
    if (state)
        Field<uint32_t>(state, 0) = 0x00589b78;
    ListNode* node = &Field<ListNode>(state, 4);
    node->prev = node;
    node->next = node;
    ListNode* list = &Field<ListNode>(r, 0x60);
    node->prev = list->prev;
    node->next = list;
    list->prev = node;
    node->prev->next = node;
    ListNode* listeners = &Field<ListNode>(state, 0x48);
    listeners->next = listeners;
    listeners->prev = listeners;
    Field<uint8_t*>(state, 0xc) = r;
    const uint32_t* defaults = reinterpret_cast<const uint32_t*>(0x005ca7e0);
    for (uint32_t i = 0; i < 14; ++i)
        Field<uint32_t>(state, 0x10 + i * 4) = defaults[i];
    uint32_t& counter = GameGlobal<uint32_t>(0x0063a0ac);
    if (++counter == 0)
        counter = 1;
    Field<uint32_t>(state, 0x10) = counter;
    VCall<uint32_t>(state, 0xc / 4, 1u);
    VCall<uint32_t>(state, 0x10 / 4, 1u);
    return state;
}

// 0x004fd220 (ecx = vertex buffer set): flush callback stored at +0x38 by VertexBufferSet_Begin. When a draw is
// pending (+0x58) calls Renderer_PrepareDraw(set); when locked (+0x48) unlocks the current buffer (+0x70 +
// current * 4) and clears the lock. Returns the Unlock result, or 0.
uint32_t VertexBufferSet_Flush(VertexBufferSet* set)
{
    if (Field<uint32_t>(set, 0x58) != 0)
        Renderer_PrepareDraw(reinterpret_cast<uint8_t*>(set));
    if (set->locked == 0)
        return 0;
    uint32_t hr = static_cast<uint32_t>(set->buffers[set->current]->Unlock());
    set->locked = 0;
    return hr;
}

uint32_t VertexBufferSet_SetFormat(VertexBufferSet* set, uint32_t format);

// 0x004fd250 (ecx = vertex buffer set; [vertex format, a1, a2, a3]): begin a primitive batch. Logs "Error begining
// on primbuffer, already in use..." when an interface is in use (+0x80) and "Error locking vertex buffer, already
// locked!" when locked; otherwise, unless the lock flags (+0x6c) have NOOVERWRITE, advance to the next buffer
// (signed wrap at count), Lock it whole with the flags into +0x48 and add NOOVERWRITE. Then set the format
// (0x004fe2a0), +0x40 = +0x44, +0x1c = a1, +0x20 = a2, +0x2c/+0x30/+0x34 from the tables 0x005fb868/0x005fb828/
// 0x005fb7e8 at [a2 + a1 * 4], and when a3 differs from +0x24 store it and the flush callback 0x004fd220 (+0x38).
// The interface object (+0x84) becomes in use (+0x80: itself when a2 == 0, else its +0x0c part); returns that
// pointer + 8, or null.
uint32_t VertexBufferSet_Begin(VertexBufferSet* set, uint32_t format, int32_t a1, int32_t a2, uint32_t a3)
{
    if (Field<uint32_t>(set, 0x80) != 0)
        GameLog(kStrPrimBufferInUse);
    if (set->locked != 0)
    {
        GameLog(kStrVbAlreadyLocked);
    }
    else
    {
        uint32_t lockFlags = set->field_6c;
        if (!(lockFlags & kLockNoOverwrite))
        {
            int32_t next = static_cast<int32_t>(set->current) + 1;
            set->current = static_cast<uint32_t>(next);
            if (next >= static_cast<int32_t>(set->count))
                set->current = 0;
        }
        set->buffers[set->current]->Lock(0, 0, reinterpret_cast<void**>(&set->locked), lockFlags);
        set->field_6c |= kLockNoOverwrite;
    }
    VertexBufferSet_SetFormat(set, format);
    Field<uint32_t>(set, 0x40) = Field<uint32_t>(set, 0x44);
    Field<int32_t>(set, 0x1c) = a1;
    int32_t index = a2 + a1 * 4;
    Field<int32_t>(set, 0x20) = a2;
    Field<uint32_t>(set, 0x2c) = reinterpret_cast<const uint32_t*>(0x005fb868)[index];
    Field<uint32_t>(set, 0x30) = reinterpret_cast<const uint32_t*>(0x005fb828)[index];
    Field<uint32_t>(set, 0x34) = reinterpret_cast<const uint32_t*>(0x005fb7e8)[index];
    if (Field<uint32_t>(set, 0x24) != a3)
    {
        Field<uint32_t>(set, 0x24) = a3;
        Field<uint32_t>(set, 0x38) = 0x004fd220;
    }
    uint32_t iface = Field<uint32_t>(set, 0x84);
    if (a2 == 0)
    {
        Field<uint32_t>(set, 0x80) = iface;
    }
    else
    {
        Field<uint32_t>(set, 0x80) = iface ? iface + 0xc : 0;
        if (iface == 0)
            return 0;
        iface += 0xc;
    }
    return iface ? iface + 8 : 0;
}

// 0x004fd390 (ecx = primitive interface; vbptr at +4, virtual base = its vertex buffer set): end the batch: the
// set's virtual 8 (+0x20), Unlock the set's current buffer (+0x70 + current * 4), clear the lock (+0x48) and the
// in-use interface (+0x80). Returns 0.
uint32_t fn_004fd390(void* iface)
{
    VCall<uint32_t>(VirtualBase(iface, 4), 0x20 / 4);
    auto* set = reinterpret_cast<VertexBufferSet*>(VirtualBase(iface, 4));
    set->buffers[set->current]->Unlock();
    reinterpret_cast<VertexBufferSet*>(VirtualBase(iface, 4))->locked = 0;
    Field<uint32_t>(VirtualBase(iface, 4), 0x80) = 0;
    return 0;
}

// 0x004fd410 (ecx = primitive interface; vbptr at +4): the virtual base's virtual 8 (+0x20), then clear the base's
// +0x80 (in-use interface) and +0x7c. Returns 0.
uint32_t fn_004fd410(void* iface)
{
    VCall<uint32_t>(VirtualBase(iface, 4), 0x20 / 4);
    Field<uint32_t>(VirtualBase(iface, 4), 0x80) = 0;
    Field<uint32_t>(VirtualBase(iface, 4), 0x7c) = 0;
    return 0;
}

// 0x004fd7b0 (ecx = primitive interface; vbptr at +4): when the virtual base (vertex buffer set) is locked
// (+0x48), Unlock its current buffer and clear the lock. Returns the vbtable's base offset (left in eax).
uint32_t fn_004fd7b0(void* iface)
{
    auto* set = reinterpret_cast<VertexBufferSet*>(VirtualBase(iface, 4));
    if (set->locked != 0)
    {
        set->buffers[set->current]->Unlock();
        reinterpret_cast<VertexBufferSet*>(VirtualBase(iface, 4))->locked = 0;
    }
    uint8_t* vbptr = reinterpret_cast<uint8_t*>(iface) + 4;
    return static_cast<uint32_t>((*reinterpret_cast<int32_t**>(vbptr))[1]);
}

namespace
{
// Shared part of 0x004fd7f0/0x004fd8a0: make room for `count` vertices in the virtual base (vertex buffer set):
// used (+0x44) + count above capacity (+0x3c, signed) drops NOOVERWRITE (+0x6c), calls the set's virtual 8 (+0x20)
// and resets +0x40/+0x44. Returns false when it still does not fit.
bool VertexBufferSet_MakeRoom(void* iface, int32_t count)
{
    uint8_t* base = VirtualBase(iface, -4);
    if (Field<int32_t>(base, 0x44) + count <= Field<int32_t>(base, 0x3c))
        return true;
    Field<uint32_t>(base, 0x6c) &= ~kLockNoOverwrite;
    VCall<uint32_t>(VirtualBase(iface, -4), 0x20 / 4);
    Field<uint32_t>(VirtualBase(iface, -4), 0x40) = 0;
    Field<uint32_t>(VirtualBase(iface, -4), 0x44) = 0;
    base = VirtualBase(iface, -4);
    return Field<int32_t>(base, 0x44) + count <= Field<int32_t>(base, 0x3c);
}
}  // namespace

// 0x004fd7f0 (ecx = primitive interface; vbptr at -4; [count]): reserve `count` vertices in the locked buffer of
// the virtual base (vertex buffer set), flushing when full. Returns locked data (+0x48) + stride (+0x5c) * the
// previous used count (+0x44), which advances by count; null when count does not fit an empty buffer.
uint32_t fn_004fd7f0(void* iface, int32_t count)
{
    if (!VertexBufferSet_MakeRoom(iface, count))
        return 0;
    uint8_t* base = VirtualBase(iface, -4);
    int32_t used = Field<int32_t>(base, 0x44);
    Field<int32_t>(base, 0x44) = used + count;
    return static_cast<uint32_t>(Field<int32_t>(base, 0x5c) * used) + Field<uint32_t>(base, 0x48);
}

// 0x004fd8a0 (ecx = primitive interface; vbptr at -4; [count]): like 0x004fd7f0, and also appends count as a
// 16-bit word at the base's write pointer +0x50 (advanced by 2).
uint32_t fn_004fd8a0(void* iface, int32_t count)
{
    if (!VertexBufferSet_MakeRoom(iface, count))
        return 0;
    uint8_t* base = VirtualBase(iface, -4);
    *Field<uint16_t*>(base, 0x50) = static_cast<uint16_t>(count);
    Field<uint32_t>(VirtualBase(iface, -4), 0x50) += 2;
    base = VirtualBase(iface, -4);
    int32_t used = Field<int32_t>(base, 0x44);
    Field<int32_t>(base, 0x44) = used + count;
    return static_cast<uint32_t>(Field<int32_t>(base, 0x5c) * used) + Field<uint32_t>(base, 0x48);
}

// 0x004fdc10 (ecx = index buffer; [count]): index buffer virtual 3. Logs "Error locking index buffer, already
// locked!" and returns null when locked (+0x1c). When used (+0x18) + count exceeds the capacity (+0x10, signed),
// drops NOOVERWRITE from the lock flags (+0x24) and restarts at 0 (+0x14/+0x18), returning null if count alone is
// too big. Locks the whole buffer (+0x20) with the flags into +0x1c, adds NOOVERWRITE, records the start (+0x14)
// and advances +0x18. Returns the locked data + start * 2.
uint32_t IndexBuffer_Lock(IndexBuffer* ib, int32_t count)
{
    if (ib->locked != 0)
    {
        GameLog(kStrIbAlreadyLocked);
        return 0;
    }
    int32_t capacity = static_cast<int32_t>(ib->indexCount);
    if (Field<int32_t>(ib, 0x18) + count > capacity)
    {
        Field<uint32_t>(ib, 0x24) &= ~kLockNoOverwrite;
        Field<uint32_t>(ib, 0x14) = 0;
        Field<uint32_t>(ib, 0x18) = 0;
        if (count > capacity)
            return 0;
    }
    ib->buffer->Lock(0, 0, reinterpret_cast<void**>(&ib->locked), Field<uint32_t>(ib, 0x24));
    Field<uint32_t>(ib, 0x24) |= kLockNoOverwrite;
    int32_t start = Field<int32_t>(ib, 0x18);
    Field<int32_t>(ib, 0x14) = start;
    Field<int32_t>(ib, 0x18) = start + count;
    return ib->locked + static_cast<uint32_t>(start) * 2;
}

// 0x004fdcd0 (ecx = index buffer): index buffer virtual 5. When locked (+0x1c), Unlock (+0x20) and clear the
// lock. Returns the Unlock result, or 0.
uint32_t IndexBuffer_Unlock(IndexBuffer* ib)
{
    if (ib->locked == 0)
        return 0;
    uint32_t hr = static_cast<uint32_t>(ib->buffer->Unlock());
    ib->locked = 0;
    return hr;
}

// 0x004fdeb0 (ecx = primitive interface; vbptr at -4; [a0, a1, count, a3, indices]) (not executed in the
// comparison scenarios): copy `count` indices, the low words of the dwords at `indices`, into the renderer's shared
// index buffer (virtual base +0x0c = renderer, renderer +0x68; its virtual 3 locks, virtual 5 unlocks), then
// returns the interface's own virtual 8 (+0x20) with (a0, a1, count, a3, index buffer).
uint32_t fn_004fdeb0(void* iface, uint32_t a0, uint32_t a1, uint32_t count, uint32_t a3, const uint32_t* indices)
{
    auto sharedIndexBuffer = [iface]() {
        return Field<void*>(Field<uint8_t*>(VirtualBase(iface, -4), 0xc), 0x68);
    };
    auto* out = reinterpret_cast<uint16_t*>(VCall<uint32_t>(sharedIndexBuffer(), 0xc / 4, count));
    for (uint32_t i = count; i != 0; --i)
    {
        *out++ = static_cast<uint16_t>(*indices);
        indices++;
    }
    VCall<uint32_t>(sharedIndexBuffer(), 0x14 / 4);
    return VCall<uint32_t>(iface, 0x20 / 4, a0, a1, count, a3, sharedIndexBuffer());
}

// 0x004fe130 (edi = vertex buffer set; called from 0x004fe7d0) (not executed in the comparison scenarios): release
// the set's buffers. When still locked (+0x48), logs "Error releasing VB while it is locked!", clears the lock and
// Unlocks the current buffer (if any). Releases every buffer from last to first and clears its slot, then resets
// +0x1c/+0x20/+0x24 to 4/4/3. Returns what the original leaves in eax: the first slot's buffer pointer as it was
// before release (0 when the set has no buffers).
uint32_t VertexBufferSet_ReleaseBuffers(VertexBufferSet* set)
{
    if (set->locked != 0)
    {
        GameLog(kStrReleaseVbLocked);
        set->locked = 0;
        if (IDirect3DVertexBuffer9* vb = set->buffers[set->current])
            vb->Unlock();
    }
    uint32_t last = set->count;
    for (uint32_t i = set->count; i != 0; --i)
    {
        IDirect3DVertexBuffer9* vb = set->buffers[i - 1];
        last = u(vb);
        if (vb)
        {
            vb->Release();
            set->buffers[i - 1] = nullptr;
        }
    }
    set->field_1c = 4;
    set->field_20 = 4;
    set->field_24 = 3;
    return last;
}

// 0x004fe230 (ecx = index buffer; called from 0x004fe750): index buffer virtual 0, destroy. When still locked,
// logs "Error releasing IB while it is locked!", clears the lock and Unlocks. Releases the D3D buffer (+0x20),
// unlinks the node (+4) from the renderer's list and frees the object (0x004e08d0, game heap). Returns its result.
uint32_t IndexBuffer_Destroy(IndexBuffer* ib)
{
    if (ib->locked != 0)
    {
        GameLog(kStrReleaseIbLocked);
        ib->locked = 0;
        if (ib->buffer)
            ib->buffer->Unlock();
    }
    if (ib->buffer)
    {
        ib->buffer->Release();
        ib->buffer = nullptr;
    }
    ListNode* node = &ib->node;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
    return orig::fn_004e08d0(kGameHeap, u(ib));
}

// 0x004fe2a0 (ecx = vertex buffer set, ebx = vertex format; called from 0x004fd250 and 0x004fe3f0): switch the
// set to a vertex format (+0x28) when it changes. The stride comes from the table 0x005caa68; a set with no stride
// yet (+0x5c) takes it and sizes itself (+0x68 = capacity +0x3c * stride). Capacity becomes size / stride
// (unsigned), clamped to 0xffff (signed compare), and the counters +0x40/+0x44 are rescaled to the new stride,
// rounding up (signed). Returns the new +0x44; when the format is unchanged the original returns the caller's eax
// (callers ignore it), here 0.
uint32_t VertexBufferSet_SetFormat(VertexBufferSet* set, uint32_t format)
{
    if (Field<uint32_t>(set, 0x28) == format)
        return 0;
    uint32_t stride = reinterpret_cast<const uint32_t*>(0x005caa68)[format];
    if (Field<uint32_t>(set, 0x5c) == 0)
    {
        Field<uint32_t>(set, 0x5c) = stride;
        set->sizeBytes = Field<uint32_t>(set, 0x3c) * stride;
    }
    uint32_t capacity = set->sizeBytes / stride;
    Field<uint32_t>(set, 0x3c) = capacity;
    if (static_cast<int32_t>(capacity) >= 0xffff)
        Field<uint32_t>(set, 0x3c) = 0xffff;
    int32_t oldStride = Field<int32_t>(set, 0x5c);
    int32_t s = static_cast<int32_t>(stride);
    int32_t c40 = (Field<int32_t>(set, 0x40) * oldStride + s - 1) / s;
    Field<uint32_t>(set, 0x5c) = stride;
    Field<uint32_t>(set, 0x28) = format;
    Field<int32_t>(set, 0x40) = c40;
    int32_t c44 = (Field<int32_t>(set, 0x44) * oldStride + s - 1) / s;
    Field<int32_t>(set, 0x44) = c44;
    return static_cast<uint32_t>(c44);
}

// ---- ABI adapters and hooks ----

static float fn_004fb170_Abi(uint32_t ecx, uint32_t item)
{
    return fn_004fb170(reinterpret_cast<void*>(ecx), reinterpret_cast<uint8_t*>(item));
}
static uint32_t fn_004fb7c0_Abi(uint32_t ecx, uint32_t flags)
{
    return u(fn_004fb7c0(reinterpret_cast<uint8_t*>(ecx), flags));
}
static uint32_t fn_004fb860_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_004fb860(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<void*>(esi), reinterpret_cast<void*>(edi),
                       a0, a1);
}
static uint32_t fn_004fb920_Abi(uint32_t eax, uint32_t esi, uint32_t a0)
{
    return fn_004fb920(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<void*>(esi), a0);
}
static uint32_t fn_004fba30_Abi()
{
    return fn_004fba30();
}
static uint32_t fn_004fbab0_Abi()
{
    return fn_004fbab0();
}
static uint32_t SkinMesh_CheckTexCoordChannels_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return SkinMesh_CheckTexCoordChannels(reinterpret_cast<uint8_t*>(a0), reinterpret_cast<uint8_t*>(a1),
                                          reinterpret_cast<const int32_t*>(a2), reinterpret_cast<const char*>(a3));
}
static uint32_t DrawState_NotifyListeners_Abi(uint32_t ecx)
{
    return DrawState_NotifyListeners(reinterpret_cast<uint8_t*>(ecx));
}
static uint32_t DrawState_Set_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return u(DrawState_Set(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2));
}
static uint32_t DrawState_SetCullMode_Abi(uint32_t ecx, uint32_t a0)
{
    return u(DrawState_SetCullMode(reinterpret_cast<uint8_t*>(ecx), a0));
}
static uint32_t DrawState_SetClipping_Abi(uint32_t ecx, uint32_t a0)
{
    return u(DrawState_SetClipping(reinterpret_cast<uint8_t*>(ecx), a0));
}
static uint32_t DrawState_SetWords_Abi(uint32_t ecx, uint32_t a0)
{
    return u(DrawState_SetWords(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<const uint32_t*>(a0)));
}
static uint32_t DrawState_SetWordsAndColors_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return u(DrawState_SetWordsAndColors(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<const uint32_t*>(a0),
                                         reinterpret_cast<const uint32_t*>(a1)));
}
static uint32_t Renderer_CreateDrawState_Abi(uint32_t eax)
{
    return u(Renderer_CreateDrawState(reinterpret_cast<uint8_t*>(eax)));
}
static uint32_t VertexBufferSet_Flush_Abi(uint32_t ecx)
{
    return VertexBufferSet_Flush(reinterpret_cast<VertexBufferSet*>(ecx));
}
static uint32_t VertexBufferSet_Begin_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return VertexBufferSet_Begin(reinterpret_cast<VertexBufferSet*>(ecx), a0, static_cast<int32_t>(a1),
                                 static_cast<int32_t>(a2), a3);
}
static uint32_t fn_004fd390_Abi(uint32_t ecx)
{
    return fn_004fd390(reinterpret_cast<void*>(ecx));
}
static uint32_t fn_004fd410_Abi(uint32_t ecx)
{
    return fn_004fd410(reinterpret_cast<void*>(ecx));
}
static uint32_t fn_004fd7b0_Abi(uint32_t ecx)
{
    return fn_004fd7b0(reinterpret_cast<void*>(ecx));
}
static uint32_t fn_004fd7f0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fd7f0(reinterpret_cast<void*>(ecx), static_cast<int32_t>(a0));
}
static uint32_t fn_004fd8a0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fd8a0(reinterpret_cast<void*>(ecx), static_cast<int32_t>(a0));
}
static uint32_t IndexBuffer_Lock_Abi(uint32_t ecx, uint32_t a0)
{
    return IndexBuffer_Lock(reinterpret_cast<IndexBuffer*>(ecx), static_cast<int32_t>(a0));
}
static uint32_t IndexBuffer_Unlock_Abi(uint32_t ecx)
{
    return IndexBuffer_Unlock(reinterpret_cast<IndexBuffer*>(ecx));
}
static uint32_t fn_004fdeb0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004fdeb0(reinterpret_cast<void*>(ecx), a0, a1, a2, a3, reinterpret_cast<const uint32_t*>(a4));
}
static uint32_t VertexBufferSet_ReleaseBuffers_Abi(uint32_t edi)
{
    return VertexBufferSet_ReleaseBuffers(reinterpret_cast<VertexBufferSet*>(edi));
}
static uint32_t IndexBuffer_Destroy_Abi(uint32_t ecx)
{
    return IndexBuffer_Destroy(reinterpret_cast<IndexBuffer*>(ecx));
}
static uint32_t VertexBufferSet_SetFormat_Abi(uint32_t ecx, uint32_t ebx)
{
    return VertexBufferSet_SetFormat(reinterpret_cast<VertexBufferSet*>(ecx), ebx);
}

ABI_ENTRY_004fb170(fn_004fb170_Entry, fn_004fb170_Abi)
ABI_ENTRY_004fb7c0(fn_004fb7c0_Entry, fn_004fb7c0_Abi)
ABI_ENTRY_004fb860(fn_004fb860_Entry, fn_004fb860_Abi)
ABI_ENTRY_004fb920(fn_004fb920_Entry, fn_004fb920_Abi)
ABI_ENTRY_004fba30(fn_004fba30_Entry, fn_004fba30_Abi)
ABI_ENTRY_004fbab0(fn_004fbab0_Entry, fn_004fbab0_Abi)
ABI_ENTRY_004fca00(SkinMesh_CheckTexCoordChannels_Entry, SkinMesh_CheckTexCoordChannels_Abi)
ABI_ENTRY_004fce70(DrawState_NotifyListeners_Entry, DrawState_NotifyListeners_Abi)
ABI_ENTRY_004fced0(DrawState_Set_Entry, DrawState_Set_Abi)
ABI_ENTRY_004fcf20(DrawState_SetCullMode_Entry, DrawState_SetCullMode_Abi)
ABI_ENTRY_004fcf50(DrawState_SetClipping_Entry, DrawState_SetClipping_Abi)
ABI_ENTRY_004fcf80(DrawState_SetWords_Entry, DrawState_SetWords_Abi)
ABI_ENTRY_004fcfc0(DrawState_SetWordsAndColors_Entry, DrawState_SetWordsAndColors_Abi)
ABI_ENTRY_004fd000(Renderer_CreateDrawState_Entry, Renderer_CreateDrawState_Abi)
ABI_ENTRY_004fd220(VertexBufferSet_Flush_Entry, VertexBufferSet_Flush_Abi)
ABI_ENTRY_004fd250(VertexBufferSet_Begin_Entry, VertexBufferSet_Begin_Abi)
ABI_ENTRY_004fd390(fn_004fd390_Entry, fn_004fd390_Abi)
ABI_ENTRY_004fd410(fn_004fd410_Entry, fn_004fd410_Abi)
ABI_ENTRY_004fd7b0(fn_004fd7b0_Entry, fn_004fd7b0_Abi)
ABI_ENTRY_004fd7f0(fn_004fd7f0_Entry, fn_004fd7f0_Abi)
ABI_ENTRY_004fd8a0(fn_004fd8a0_Entry, fn_004fd8a0_Abi)
ABI_ENTRY_004fdc10(IndexBuffer_Lock_Entry, IndexBuffer_Lock_Abi)
ABI_ENTRY_004fdcd0(IndexBuffer_Unlock_Entry, IndexBuffer_Unlock_Abi)
ABI_ENTRY_004fdeb0(fn_004fdeb0_Entry, fn_004fdeb0_Abi)
ABI_ENTRY_004fe130(VertexBufferSet_ReleaseBuffers_Entry, VertexBufferSet_ReleaseBuffers_Abi)
ABI_ENTRY_004fe230(IndexBuffer_Destroy_Entry, IndexBuffer_Destroy_Abi)
ABI_ENTRY_004fe2a0(VertexBufferSet_SetFormat_Entry, VertexBufferSet_SetFormat_Abi)

HOOK(0x004fb170, "fn_004fb170", fn_004fb170_Entry, nullptr);
HOOK(0x004fb7c0, "fn_004fb7c0", fn_004fb7c0_Entry, nullptr);
HOOK(0x004fb860, "fn_004fb860", fn_004fb860_Entry, nullptr);
HOOK(0x004fb920, "fn_004fb920", fn_004fb920_Entry, nullptr);
HOOK(0x004fba30, "fn_004fba30", fn_004fba30_Entry, nullptr);
HOOK(0x004fbab0, "fn_004fbab0", fn_004fbab0_Entry, nullptr);
HOOK(0x004fca00, "SkinMesh_CheckTexCoordChannels", SkinMesh_CheckTexCoordChannels_Entry, nullptr);
HOOK(0x004fce70, "DrawState_NotifyListeners", DrawState_NotifyListeners_Entry, nullptr);
HOOK(0x004fced0, "DrawState_Set", DrawState_Set_Entry, nullptr);
HOOK(0x004fcf20, "DrawState_SetCullMode", DrawState_SetCullMode_Entry, nullptr);
HOOK(0x004fcf50, "DrawState_SetClipping", DrawState_SetClipping_Entry, nullptr);
HOOK(0x004fcf80, "DrawState_SetWords", DrawState_SetWords_Entry, nullptr);
HOOK(0x004fcfc0, "DrawState_SetWordsAndColors", DrawState_SetWordsAndColors_Entry, nullptr);
HOOK(0x004fd000, "Renderer_CreateDrawState", Renderer_CreateDrawState_Entry, nullptr);
HOOK(0x004fd220, "VertexBufferSet_Flush", VertexBufferSet_Flush_Entry, nullptr);
HOOK(0x004fd250, "VertexBufferSet_Begin", VertexBufferSet_Begin_Entry, nullptr);
HOOK(0x004fd390, "fn_004fd390", fn_004fd390_Entry, nullptr);
HOOK(0x004fd410, "fn_004fd410", fn_004fd410_Entry, nullptr);
HOOK(0x004fd7b0, "fn_004fd7b0", fn_004fd7b0_Entry, nullptr);
HOOK(0x004fd7f0, "fn_004fd7f0", fn_004fd7f0_Entry, nullptr);
HOOK(0x004fd8a0, "fn_004fd8a0", fn_004fd8a0_Entry, nullptr);
HOOK(0x004fdc10, "IndexBuffer_Lock", IndexBuffer_Lock_Entry, nullptr);
HOOK(0x004fdcd0, "IndexBuffer_Unlock", IndexBuffer_Unlock_Entry, nullptr);
HOOK(0x004fdeb0, "fn_004fdeb0", fn_004fdeb0_Entry, nullptr);
HOOK(0x004fe130, "VertexBufferSet_ReleaseBuffers", VertexBufferSet_ReleaseBuffers_Entry, nullptr);
HOOK(0x004fe230, "IndexBuffer_Destroy", IndexBuffer_Destroy_Entry, nullptr);
HOOK(0x004fe2a0, "VertexBufferSet_SetFormat", VertexBufferSet_SetFormat_Entry, nullptr);
