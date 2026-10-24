// Recovered: loader helpers around 0x004e6570 (file streaming thread, resource pointer fix-up / byte-swap
// passes, skin and animation binding). S2, parallel batch.
//   0x004e6570 FileStream_ReadChunk          0x004e6740 FileStream_ThreadLoop
//   0x004e7570 fn_004e7570                   0x004e80b0 Plane_FromPoints
//   0x004eaaa0 Reloc_FixupElementList        0x004ed290 SkinBinding_Create
//   0x004ed410 AnimSkinBinding_Create        0x004ee830 fn_004ee830
//   0x004ee8f0 Reloc_SwapVec2                0x004ee9c0 Reloc_SwapVec3
//   0x004eea40 Reloc_SwapVec4                0x004eebe0 Reloc_SwapDword
//   0x004eec50 Reloc_SwapMatrixAndDword      0x004eeda0 Reloc_FixupTypedBlock
#include <windows.h>

#include <cmath>
#include <cstring>

#include "game.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

// Imports the game calls through its IAT (call dword ptr [slot]); going through the slot keeps import hooks.
template <class F>
F Auto4e6570_Import(uintptr_t slot)
{
    return *reinterpret_cast<F*>(slot);
}
auto Auto4e6570_EnterCS() { return Auto4e6570_Import<decltype(&EnterCriticalSection)>(0x0056b0b0); }
auto Auto4e6570_LeaveCS() { return Auto4e6570_Import<decltype(&LeaveCriticalSection)>(0x0056b0b4); }
auto Auto4e6570_Sleep() { return Auto4e6570_Import<decltype(&Sleep)>(0x0056b0b8); }
auto Auto4e6570_SetEvent() { return Auto4e6570_Import<decltype(&SetEvent)>(0x0056b0bc); }
auto Auto4e6570_ReadFile() { return Auto4e6570_Import<decltype(&ReadFile)>(0x0056b0c8); }
auto Auto4e6570_WriteFile() { return Auto4e6570_Import<decltype(&WriteFile)>(0x0056b0cc); }
auto Auto4e6570_SetFilePointer() { return Auto4e6570_Import<decltype(&SetFilePointer)>(0x0056b0d0); }

// The game's allocator hook (function pointer at 0x005cacc0, cdecl (size, tag)).
void* Auto4e6570_Alloc(uint32_t size, uint32_t tag)
{
    return GameGlobal<void*(__cdecl*)(uint32_t, uint32_t)>(0x005cacc0)(size, tag);
}

const char* const kStrSknMissingBone = reinterpret_cast<const char*>(0x0058af20);
const char* const kStrCantFindTree = reinterpret_cast<const char*>(0x0058af68);
const char* const kStrBindUnderTree = reinterpret_cast<const char*>(0x0058aee4);
const char* const kStrBindDefaultTree = reinterpret_cast<const char*>(0x0058aea0);

// Pointer fix-up context passed to the loader's relocation passes (0x004e2040 etc.): +0 the file (whose +8 is
// the relocation table), +4 nonzero once fixed up (the passes are skipped), +8 nonzero = byte-swap fields.
struct RelocCtx
{
    uint8_t* file;
    uint32_t fixed;
    uint32_t swap;
};

// Binary search of a sorted table (count +0x1c, +0x20 must exceed minEntries, searcher state at
// [+0x24] + searcherOffset: +0 count, +8 index, +0xc compare result, +0x10 entries, +0x18 key) with 0x004e0f40.
// Returns the matching entry or null. Not inlined so the null result is never folded into later reads.
__declspec(noinline) uint8_t* Auto4e6570_TableSearch(uint8_t* table, uint32_t searcherOffset, uint32_t minEntries,
                                                     const void* key)
{
    uint32_t count = Field<uint32_t>(table, 0x1c);
    if (count == 0 || Field<uint32_t>(table, 0x20) <= minEntries)
        return nullptr;
    uint8_t* s = Field<uint8_t*>(table, 0x24) + searcherOffset;
    Field<uint32_t>(s, 0) = count;
    Field<const void*>(s, 0x18) = key;
    orig::fn_004e0f40(u(s));
    if (Field<uint32_t>(s, 0xc) != 0)
        return nullptr;
    return Field<uint8_t**>(s, 0x10)[Field<uint32_t>(s, 8)];
}

// The relocation entry for the field at `field`, and where the field really lives:
// field + (entry dword at word[+2] + 4) - (entry dword at word[+2]).
uint32_t* Auto4e6570_Locate(RelocCtx* ctx, uintptr_t field)
{
    uintptr_t key = field;
    uint8_t* entry = Auto4e6570_TableSearch(Field<uint8_t*>(ctx->file, 8), 0, 0, &key);
    uint32_t w = Field<uint16_t>(entry, 2);
    return reinterpret_cast<uint32_t*>(field + (Field<uint32_t>(entry, w + 4) - Field<uint32_t>(entry, w)));
}

// Locate the field and, when swapping, byte-swap it with 0x004f60b0. Returns EAX as the original leaves it:
// 0 (the swap flag) when not swapping, else 0x004f60b0's result.
uint32_t Auto4e6570_SwapCall(RelocCtx* ctx, uintptr_t field)
{
    uint32_t* target = Auto4e6570_Locate(ctx, field);
    if (ctx->swap == 0)
        return 0;
    return orig::fn_004f60b0(u(target), 0, 0, 0);
}

// Same, with the byte swap written inline (as some callers do). Returns the located field.
uint32_t* Auto4e6570_SwapInline(RelocCtx* ctx, uintptr_t field)
{
    uint32_t* target = Auto4e6570_Locate(ctx, field);
    if (ctx->swap != 0)
    {
        uint32_t v = *target;
        *target = (v << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
    }
    return target;
}

// 0x004e2160 (edi = ctx; [field, size]): locate a field and swap it per its size (reads two stack arguments).
void Auto4e6570_SwapSized(RelocCtx* ctx, uintptr_t field, uint32_t size)
{
    orig::fn_004e2160(u(ctx), field, size, 0, 0);
}
void Auto4e6570_SwapDwords(RelocCtx* ctx, uintptr_t base, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        Auto4e6570_SwapSized(ctx, base + i * 4, 4);
}
}  // namespace

uint32_t Reloc_SwapVec2(RelocCtx* ctx, uintptr_t field);
uint32_t Reloc_SwapVec3(RelocCtx* ctx, uintptr_t field);
uint32_t Reloc_SwapVec4(RelocCtx* ctx, uintptr_t field);
uint32_t Reloc_SwapDword(uintptr_t field, RelocCtx* ctx);
uint32_t Reloc_SwapMatrixAndDword(uintptr_t field, RelocCtx* ctx);

// 0x004e6570 (eax = stream): one read step of the file streaming thread. Prepares (0x004e6190 with +0x550), takes
// up to 0x8000 bytes of buffer space (0x004e5cf0) under the lock at +0x520 and ReadFile()s handle +0xf0 into it.
// On failure sets flag 0x40 (+0x10) and calls 0x004e62c0; on success mirrors the data to handle +0x304 when
// mode +0x300 is 2. Reports the byte count to virtual 11, then 0x004e6250. AL = 1 on failure or short read.
uint32_t FileStream_ReadChunk(uint8_t* stream)
{
    uint8_t failed = 0;
    orig::fn_004e6190(u(stream), Field<uint32_t>(stream, 0x550));
    auto* lock = reinterpret_cast<CRITICAL_SECTION*>(stream + 0x520);
    Auto4e6570_EnterCS()(lock);
    void* buffer = nullptr;
    uint32_t wanted = orig::fn_004e5cf0(u(stream), u(&buffer), 0x8000);
    Auto4e6570_LeaveCS()(lock);
    if (wanted != 0)
    {
        DWORD got = 0;
        BOOL ok = Auto4e6570_ReadFile()(Field<HANDLE>(stream, 0xf0), buffer, wanted, &got, nullptr);
        failed = ok == 0 ? 1 : 0;
        if (failed)
        {
            Auto4e6570_EnterCS()(lock);
            Field<uint32_t>(stream, 0x10) |= 0x40;
            Auto4e6570_LeaveCS()(lock);
            orig::fn_004e62c0(u(stream));
        }
        else
        {
            if (Field<int32_t>(stream, 0x300) == 2 && got != 0)
            {
                DWORD written;
                Auto4e6570_WriteFile()(Field<HANDLE>(stream, 0x304), buffer, got, &written, nullptr);
            }
            failed = got != wanted ? 1 : 0;
        }
        VCall<void>(stream, 0x2c / 4, static_cast<uint32_t>(got));
    }
    uint32_t r = orig::fn_004e6250(u(stream));
    return (r & 0xffffff00u) | failed;
}

// 0x004e6740 (ecx unused, esi = stream): the streaming thread's loop. Until flag 2 (+0x10) is set: wait for
// virtual 15 to say there is work, read a chunk (0x004e6570); on failure/short read or flag 0x80, rewind the file
// (flag 8 without 0x84) or finish the stream (0x004e62c0). On exit, flag 0x100 finishes and signals event +0x53c;
// clears flag 0x10 under the lock and returns 0.
uint32_t FileStream_ThreadLoop(uint8_t* stream)
{
    auto* lock = reinterpret_cast<CRITICAL_SECTION*>(stream + 0x520);
    for (;;)
    {
        if ((Field<uint32_t>(stream, 0x10) >> 1) & 1)
            break;
        if (!VCall<bool>(stream, 0x3c / 4))
            continue;
        uint8_t failed = static_cast<uint8_t>(FileStream_ReadChunk(stream));
        Auto4e6570_EnterCS()(lock);
        uint32_t flags = Field<uint32_t>(stream, 0x10);
        Auto4e6570_LeaveCS()(lock);
        uint8_t low = static_cast<uint8_t>(flags);
        if (failed != 0 || (low & 0x80))
        {
            if (!(low & 0x84) && (low & 8))
            {
                Auto4e6570_SetFilePointer()(Field<HANDLE>(stream, 0xf0), 0, nullptr, 0);
                Auto4e6570_Sleep()(0);
                continue;
            }
            orig::fn_004e62c0(u(stream));
        }
        Auto4e6570_Sleep()(0);
    }
    if ((Field<uint32_t>(stream, 0x10) >> 8) & 1)
    {
        orig::fn_004e62c0(u(stream));
        Auto4e6570_SetEvent()(Field<HANDLE>(stream, 0x53c));
    }
    Auto4e6570_EnterCS()(lock);
    Field<uint32_t>(stream, 0x10) &= 0xffffffefu;
    Auto4e6570_LeaveCS()(lock);
    return 0;
}

// 0x004e7570 (eax = object, ecx, edx): constructor: vtable 0x0058b0d0, +4 = ecx, +0x14 = edx, +8/+0xc/+0x10 = 0.
void fn_004e7570(uint8_t* object, uint32_t a, uint32_t b)
{
    Field<uint32_t>(object, 4) = a;
    Field<uint32_t>(object, 0x10) = 0;
    Field<uint32_t>(object, 8) = 0;
    Field<uint32_t>(object, 0xc) = 0;
    Field<uint32_t>(object, 0x14) = b;
    Field<uint32_t>(object, 0) = 0x0058b0d0;
}

// 0x004e80b0 (eax = p0, ecx = p1, edx = p2, esi = plane out): plane through three points: normal
// (p2 - p1) x (p0 - p1), normalised (1.0 at 0x005bcb2c / length) when longer than 0x0056c05c, and
// d = -(n . p1). Leaves nothing on the x87 stack (the caller does not pop a result); EAX = the normal's z bits.
uint32_t Plane_FromPoints(const float* p0, const float* p1, const float* p2, float* plane)
{
    float e1 = p2[1] - p1[1];
    float a2 = p0[2] - p1[2];
    float e2 = p2[2] - p1[2];
    float a1 = p0[1] - p1[1];
    float x = a2 * e1 - a1 * e2;
    float a0 = p0[0] - p1[0];
    float e0 = p2[0] - p1[0];
    float y = a0 * e2 - e0 * a2;
    float z = e0 * a1 - a0 * e1;
    float length = sqrtf(z * z + y * y + x * x);
    if (length > GameGlobal<float>(0x0056c05c))
    {
        float inv = GameGlobal<float>(0x005bcb2c) / length;
        x = inv * x;
        y = inv * y;
        z = inv * z;
    }
    float d = -(x * p1[0] + z * p1[2] + y * p1[1]);
    plane[2] = z;
    plane[3] = d;
    plane[0] = x;
    plane[1] = y;
    uint32_t zBits;
    std::memcpy(&zBits, &z, 4);
    return zBits;
}

// 0x004ee8f0 (edi = ctx; [field]): relocate/byte-swap two consecutive dwords (a 2-vector). Skipped once fixed.
uint32_t Reloc_SwapVec2(RelocCtx* ctx, uintptr_t field)
{
    if (ctx->fixed == 0)
        Auto4e6570_SwapCall(ctx, field);
    if (ctx->fixed != 0)
        return ctx->fixed;
    return Auto4e6570_SwapCall(ctx, field + 4);
}

// 0x004ee9c0 (eax = ctx, ecx = field): three dwords (a 3-vector): 0x004ee8f0 for the first two, then +8.
uint32_t Reloc_SwapVec3(RelocCtx* ctx, uintptr_t field)
{
    Reloc_SwapVec2(ctx, field);
    if (ctx->fixed != 0)
        return ctx->fixed;
    return Auto4e6570_SwapCall(ctx, field + 8);
}

// 0x004eea40 (edi = ctx; [field]): four dwords (+0..+0xc). Reads one stack argument (the ABI lists 15).
uint32_t Reloc_SwapVec4(RelocCtx* ctx, uintptr_t field)
{
    for (uint32_t off = 0; off < 0xc; off += 4)
    {
        if (ctx->fixed == 0)
            Auto4e6570_SwapCall(ctx, field + off);
    }
    if (ctx->fixed != 0)
        return ctx->fixed;
    return Auto4e6570_SwapCall(ctx, field + 0xc);
}

// 0x004eebe0 (ebx = field, edi = ctx): one dword.
uint32_t Reloc_SwapDword(uintptr_t field, RelocCtx* ctx)
{
    if (ctx->fixed != 0)
        return ctx->fixed;
    return Auto4e6570_SwapCall(ctx, field);
}

// 0x004eec50 ([field, ctx]): 17 dwords (a 4x4 matrix and one more at +0x40), swap written inline.
// Returns ctx+4 when fixed, else the located address of the last dword.
uint32_t Reloc_SwapMatrixAndDword(uintptr_t field, RelocCtx* ctx)
{
    uintptr_t at = field;
    for (int row = 4; row != 0; --row)
    {
        for (int col = 4; col != 0; --col)
        {
            if (ctx->fixed == 0)
                Auto4e6570_SwapInline(ctx, at);
            at += 4;
        }
    }
    if (ctx->fixed != 0)
        return ctx->fixed;
    return u(Auto4e6570_SwapInline(ctx, field + 0x40));
}

// 0x004eaaa0 (eax = ctx; [list]): fix up a list object: pointers +4 and +0x18 (0x004e2040), dwords +0x14 (count)
// and +0x10, then each element of the array at +0x18: dwords +4 (type) and +8, pointers +0x14/+0x18, and per type
// 0: three 3-vectors (+0x1c) and three 2-vectors (+0x40); 1: two matrices (+0x1c, +0x60) and three 2-vectors
// (+0xa4); 2: two 3-vectors, dwords +0x34/+0x38 and a 2-vector (+0x3c). Returns the count pointer if empty,
// else the count.
uint32_t Reloc_FixupElementList(RelocCtx* ctx, uint8_t* list)
{
    orig::fn_004e2040(u(list + 4), u(ctx));
    orig::fn_004e2040(u(list + 0x18), u(ctx));
    if (ctx->fixed == 0)
        Auto4e6570_SwapCall(ctx, u(list + 0x14));
    if (ctx->fixed == 0)
        Auto4e6570_SwapCall(ctx, u(list + 0x10));
    int32_t* count = reinterpret_cast<int32_t*>(list + 0x14);
    if (*count <= 0)
        return u(count);
    int32_t i = 0;
    do
    {
        orig::fn_004e2040(Field<uint32_t>(list, 0x18) + i * 4, u(ctx));
        uint8_t* e = Field<uint8_t**>(list, 0x18)[i];
        if (ctx->fixed == 0)
            Auto4e6570_SwapInline(ctx, u(e + 4));
        if (ctx->fixed == 0)
            Auto4e6570_SwapInline(ctx, u(e + 8));
        orig::fn_004e2040(u(e + 0x14), u(ctx));
        orig::fn_004e2040(u(e + 0x18), u(ctx));
        switch (Field<int32_t>(e, 4))
        {
        case 0:
            Reloc_SwapVec3(ctx, u(e + 0x1c));
            Reloc_SwapVec3(ctx, u(e + 0x28));
            Reloc_SwapVec3(ctx, u(e + 0x34));
            Reloc_SwapVec2(ctx, u(e + 0x40));
            Reloc_SwapVec2(ctx, u(e + 0x48));
            Reloc_SwapVec2(ctx, u(e + 0x50));
            break;
        case 1:
            Reloc_SwapMatrixAndDword(u(e + 0x1c), ctx);
            Reloc_SwapMatrixAndDword(u(e + 0x60), ctx);
            Reloc_SwapVec2(ctx, u(e + 0xa4));
            Reloc_SwapVec2(ctx, u(e + 0xac));
            Reloc_SwapVec2(ctx, u(e + 0xb4));
            break;
        case 2:
            Reloc_SwapVec3(ctx, u(e + 0x1c));
            Reloc_SwapVec3(ctx, u(e + 0x28));
            if (ctx->fixed == 0)
                Auto4e6570_SwapCall(ctx, u(e + 0x34));
            if (ctx->fixed == 0)
                Auto4e6570_SwapCall(ctx, u(e + 0x38));
            Reloc_SwapVec2(ctx, u(e + 0x3c));
            break;
        default:
            break;
        }
        ++i;
    } while (i < *count);
    return static_cast<uint32_t>(i);
}

// 0x004ed290 ([skeleton, skin, unused]): allocate (allocator 0x005cacc0, tag 2) and zero a binding of
// skin+0x88 bones: {skeleton, skin, bound count, bone pointers...}. Each skin bone (0x58-byte records at +0x8c,
// id word +8) is looked up by id in the skin's name table, then by name in the skeleton (+4); a missing bone
// gets null and logs "Error binding : The SKN ... references (but can't find) the bone ...".
uint32_t* SkinBinding_Create(uint8_t* skeleton, uint8_t* skin, uint32_t /*unused*/)
{
    uint32_t size = Field<uint32_t>(skin, 0x88) * 4 + 0xc;
    uint32_t* binding;
    if (size == 0)
    {
        binding = nullptr;
    }
    else
    {
        binding = static_cast<uint32_t*>(Auto4e6570_Alloc(size, 2));
        std::memset(binding, 0, size);
    }
    binding[0] = u(skeleton);
    binding[1] = u(skin);
    binding[2] = 0;
    if (!skeleton)
        return binding;
    uint32_t* out = binding + 3;
    int32_t offset = 0;
    for (int32_t i = 0; i < Field<int32_t>(skin, 0x88); ++i, offset += 0x58)
    {
        uint32_t key = Field<uint16_t>(Field<uint8_t*>(skin, 0x8c), offset + 8);
        uint8_t* names = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(skin, 4), 0x18), 4);
        uint8_t* bone = Auto4e6570_TableSearch(names, 0x1c, 1, &key);
        const uint8_t* boneName = bone + Field<uint16_t>(bone, 2) + 4;
        uint8_t* found = Auto4e6570_TableSearch(Field<uint8_t*>(skeleton, 4), 0, 0, boneName);
        if (found)
        {
            *out = Field<uint32_t>(found, Field<uint16_t>(found, 2));
        }
        else
        {
            *out = 0;
            GameLog(kStrSknMissingBone, Field<uint8_t*>(skin, 4) + 0x34, bone + Field<uint16_t>(bone, 2) + 4);
        }
        ++out;
        ++binding[2];
    }
    return binding;
}

// 0x004ed410 ([anim, tree name or null, model, a3]): allocate and zero a binding for every skin in the model's
// list (+0x20 -> +4): +4 the anim's tree (default +0x2c, or the named one via 0x004f0c90 on +0x28; logs
// "Can't find tree"), +8 anim, +0xc model, +0x10 total bone count, +0x14 skin count, +0x18 the skin bindings
// (0x004ed290). Logs "Error binding" for a skin with an unbound bone. Null when the model has no skins.
uint32_t* AnimSkinBinding_Create(uint8_t* anim, const char* treeName, uint8_t* model, uint32_t a3)
{
    uint32_t skins = Field<uint32_t>(Field<uint8_t*>(Field<uint8_t*>(model, 0x20), 4), 0x1c);
    if (skins == 0)
        return nullptr;
    uint32_t size = skins * 4 + 0x18;
    uint32_t* b;
    if (size == 0)
    {
        b = nullptr;
    }
    else
    {
        b = static_cast<uint32_t*>(Auto4e6570_Alloc(size, 2));
        std::memset(b, 0, size);
    }
    b[2] = u(anim);
    b[3] = u(model);
    b[4] = 0;
    uint32_t tree = 0;
    if (anim)
    {
        tree = Field<uint32_t>(anim, 0x2c);
        if (treeName)
        {
            auto* e = reinterpret_cast<uint8_t*>(orig::fn_004f0c90(Field<uint32_t>(anim, 0x28), u(treeName)));
            if (e)
                tree = Field<uint32_t>(e, Field<uint16_t>(e, 2));
            else
                GameLog(kStrCantFindTree, treeName, anim + 0x34);
        }
    }
    b[1] = tree;
    uint32_t* out = b + 6;
    uint32_t index = 0;
    for (;;)
    {
        uint8_t* list = Field<uint8_t*>(Field<uint8_t*>(model, 0x20), 4);
        uint8_t* skin = nullptr;
        if (index < Field<uint32_t>(list, 0x1c) && Field<uint32_t>(list, 0x20) != 0)
        {
            uint8_t* e = Field<uint8_t**>(Field<uint8_t*>(list, 0x24), 0x10)[index];
            if (e)
                skin = Field<uint8_t*>(e, Field<uint16_t>(e, 2));
        }
        ++index;
        if (!skin)
            break;
        uint32_t* sb = SkinBinding_Create(reinterpret_cast<uint8_t*>(b[1]), skin, a3);
        *out = u(sb);
        if (sb)
        {
            b[4] += Field<uint32_t>(reinterpret_cast<uint8_t*>(sb[1]), 0x10);
            for (int32_t j = 0; j < static_cast<int32_t>(sb[2]); ++j)
            {
                if (sb[3 + j] == 0)
                {
                    if (treeName)
                        GameLog(kStrBindUnderTree, model + 0x34, anim + 0x34, treeName);
                    else
                        GameLog(kStrBindDefaultTree, model + 0x34, anim + 0x34);
                    break;
                }
            }
        }
        ++out;
    }
    b[5] = static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<uint8_t*>(out) -
                                                      reinterpret_cast<uint8_t*>(b) - 0x18) >> 2);
    return b;
}

// 0x004ee830 (eax = object): for the sub-objects at +0x48/+0x4c/+0x50 (kind word +6 = 2 or 3; only +0x4c for
// kind 1) and +0xa8/+0xac/+0xb0 (word +4 = 1), set their +4 from the 12-byte table at 0x005cabe0 indexed by
// their first byte. Returns the object, null, or the +0xb0 sub-object (EAX as the original leaves it).
uint32_t fn_004ee830(uint8_t* object)
{
    if (!object)
        return 0;
    auto resolve = [](uint8_t* sub) {
        if (sub)
            Field<uint32_t>(sub, 4) = GameGlobal<uint32_t>(0x005cabe0 + sub[0] * 12);
    };
    int16_t kind = Field<int16_t>(object, 6);
    if (kind == 2 || kind == 3)
    {
        resolve(Field<uint8_t*>(object, 0x48));
        resolve(Field<uint8_t*>(object, 0x4c));
        resolve(Field<uint8_t*>(object, 0x50));
    }
    else if (kind == 1)
    {
        resolve(Field<uint8_t*>(object, 0x4c));
    }
    if (Field<int16_t>(object, 4) != 1)
        return u(object);
    resolve(Field<uint8_t*>(object, 0xa8));
    resolve(Field<uint8_t*>(object, 0xac));
    uint8_t* last = Field<uint8_t*>(object, 0xb0);
    resolve(last);
    return u(last);
}

// 0x004eeda0 (eax = ctx, ecx = slot): fix up the pointer in *slot (0x004e2040) and the typed block it points to:
// type byte +0, count +8, item pointers at +0xc (types 1-3 also swap the dword after them). Each item gets its
// type's dword fields swapped (0x004e2160) plus 3-vectors (types 4-6), 4-vectors (7-9), single dwords (12-14) or
// pointer pairs (11). Finishes with the four header bytes and the count. Returns 0x004e2040's result if the
// slot is null, ctx+4 when fixed, else the count's located address.
uint32_t Reloc_FixupTypedBlock(RelocCtx* ctx, uint32_t* slot)
{
    uint32_t r = orig::fn_004e2040(u(slot), u(ctx));
    auto* block = reinterpret_cast<uint8_t*>(*slot);
    if (!block)
        return r;
    if (block[0] != 0 && block[0] <= 3)
        Auto4e6570_SwapSized(ctx, u(block + Field<uint32_t>(block, 8) * 4 + 0xc), 4);
    for (uint32_t i = 0; i < Field<uint32_t>(block, 8); ++i)
    {
        uint32_t* item = reinterpret_cast<uint32_t*>(block + 0xc) + i;
        orig::fn_004e2040(u(item), u(ctx));
        uintptr_t p = *item;
        switch (block[0])
        {
        case 1:
            Auto4e6570_SwapDwords(ctx, p, 9);
            break;
        case 2:
            Auto4e6570_SwapDwords(ctx, p, 5);
            break;
        case 3:
            Auto4e6570_SwapDwords(ctx, p, 2);
            break;
        case 4:
            Auto4e6570_SwapDwords(ctx, p, 6);
            Reloc_SwapVec3(ctx, p + 0x18);
            Reloc_SwapVec3(ctx, p + 0x24);
            Reloc_SwapVec3(ctx, p + 0x30);
            break;
        case 5:
            Auto4e6570_SwapDwords(ctx, p, 2);
            Reloc_SwapVec3(ctx, p + 8);
            Reloc_SwapVec3(ctx, p + 0x14);
            Reloc_SwapVec3(ctx, p + 0x20);
            break;
        case 6:
            Auto4e6570_SwapDwords(ctx, p, 1);
            Reloc_SwapVec3(ctx, p + 4);
            break;
        case 7:
            Auto4e6570_SwapDwords(ctx, p, 6);
            Reloc_SwapVec4(ctx, p + 0x18);
            Reloc_SwapVec4(ctx, p + 0x28);
            Reloc_SwapVec4(ctx, p + 0x38);
            break;
        case 8:
            Auto4e6570_SwapDwords(ctx, p, 2);
            Reloc_SwapVec4(ctx, p + 8);
            Reloc_SwapVec4(ctx, p + 0x18);
            break;
        case 9:
            Auto4e6570_SwapDwords(ctx, p, 1);
            Reloc_SwapVec4(ctx, p + 4);
            break;
        case 11:
        {
            Auto4e6570_SwapDwords(ctx, p, 3);
            uint32_t* n = reinterpret_cast<uint32_t*>(p + 8);
            uintptr_t at = p + 0x14;
            for (uint32_t j = 0; j < *n; ++j, at += 0xc)
            {
                orig::fn_004e2040(at - 4, u(ctx));
                orig::fn_004e2040(at, u(ctx));
            }
            break;
        }
        case 12:
            Auto4e6570_SwapDwords(ctx, p, 6);
            Reloc_SwapDword(p + 0x18, ctx);
            Reloc_SwapDword(p + 0x1c, ctx);
            Reloc_SwapDword(p + 0x20, ctx);
            break;
        case 13:
            Auto4e6570_SwapDwords(ctx, p, 2);
            Reloc_SwapDword(p + 8, ctx);
            Reloc_SwapDword(p + 0xc, ctx);
            break;
        case 14:
            Auto4e6570_SwapDwords(ctx, p, 1);
            Reloc_SwapDword(p + 4, ctx);
            break;
        default:
            break;
        }
    }
    for (uint32_t k = 0; k < 4; ++k)
        Auto4e6570_SwapSized(ctx, u(block + k), 1);
    if (ctx->fixed != 0)
        return ctx->fixed;
    return u(Auto4e6570_SwapInline(ctx, u(block + 8)));
}

// --- ABI adapters and hooks ------------------------------------------------------------------------------------

static uint32_t FileStream_ReadChunk_Abi(uint32_t eax)
{
    return FileStream_ReadChunk(reinterpret_cast<uint8_t*>(eax));
}
static uint32_t FileStream_ThreadLoop_Abi(uint32_t /*ecx*/, uint32_t esi)
{
    return FileStream_ThreadLoop(reinterpret_cast<uint8_t*>(esi));
}
static void fn_004e7570_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    fn_004e7570(reinterpret_cast<uint8_t*>(eax), ecx, edx);
}
// recover.py lists st0 as the result, but the function leaves the x87 stack empty and its caller (0x0046ed30)
// pops nothing: returning a float here would leak an x87 register. EAX is returned instead.
static uint32_t Plane_FromPoints_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi)
{
    return Plane_FromPoints(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx),
                            reinterpret_cast<const float*>(edx), reinterpret_cast<float*>(esi));
}
static uint32_t Reloc_FixupElementList_Abi(uint32_t eax, uint32_t list)
{
    return Reloc_FixupElementList(reinterpret_cast<RelocCtx*>(eax), reinterpret_cast<uint8_t*>(list));
}
static uint32_t SkinBinding_Create_Abi(uint32_t skeleton, uint32_t skin, uint32_t a2)
{
    return u(SkinBinding_Create(reinterpret_cast<uint8_t*>(skeleton), reinterpret_cast<uint8_t*>(skin), a2));
}
static uint32_t AnimSkinBinding_Create_Abi(uint32_t anim, uint32_t tree, uint32_t model, uint32_t a3)
{
    return u(AnimSkinBinding_Create(reinterpret_cast<uint8_t*>(anim), reinterpret_cast<const char*>(tree),
                                    reinterpret_cast<uint8_t*>(model), a3));
}
static uint32_t fn_004ee830_Abi(uint32_t eax)
{
    return fn_004ee830(reinterpret_cast<uint8_t*>(eax));
}
static uint32_t Reloc_SwapVec2_Abi(uint32_t edi, uint32_t field)
{
    return Reloc_SwapVec2(reinterpret_cast<RelocCtx*>(edi), field);
}
static uint32_t Reloc_SwapVec3_Abi(uint32_t eax, uint32_t ecx)
{
    return Reloc_SwapVec3(reinterpret_cast<RelocCtx*>(eax), ecx);
}
static uint32_t Reloc_SwapVec4_Abi(uint32_t edi, uint32_t field, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/,
                                   uint32_t /*a4*/, uint32_t /*a5*/, uint32_t /*a6*/, uint32_t /*a7*/,
                                   uint32_t /*a8*/, uint32_t /*a9*/, uint32_t /*a10*/, uint32_t /*a11*/,
                                   uint32_t /*a12*/, uint32_t /*a13*/, uint32_t /*a14*/)
{
    return Reloc_SwapVec4(reinterpret_cast<RelocCtx*>(edi), field);
}
static uint32_t Reloc_SwapDword_Abi(uint32_t ebx, uint32_t edi)
{
    return Reloc_SwapDword(ebx, reinterpret_cast<RelocCtx*>(edi));
}
static uint32_t Reloc_SwapMatrixAndDword_Abi(uint32_t field, uint32_t ctx)
{
    return Reloc_SwapMatrixAndDword(field, reinterpret_cast<RelocCtx*>(ctx));
}
static uint32_t Reloc_FixupTypedBlock_Abi(uint32_t eax, uint32_t ecx)
{
    return Reloc_FixupTypedBlock(reinterpret_cast<RelocCtx*>(eax), reinterpret_cast<uint32_t*>(ecx));
}

ABI_ENTRY_004e6570(FileStream_ReadChunk_Entry, FileStream_ReadChunk_Abi)
ABI_ENTRY_004e6740(FileStream_ThreadLoop_Entry, FileStream_ThreadLoop_Abi)
ABI_ENTRY_004e7570(fn_004e7570_Entry, fn_004e7570_Abi)
ABI_ENTRY_004e80b0(Plane_FromPoints_Entry, Plane_FromPoints_Abi)
ABI_ENTRY_004eaaa0(Reloc_FixupElementList_Entry, Reloc_FixupElementList_Abi)
ABI_ENTRY_004ed290(SkinBinding_Create_Entry, SkinBinding_Create_Abi)
ABI_ENTRY_004ed410(AnimSkinBinding_Create_Entry, AnimSkinBinding_Create_Abi)
ABI_ENTRY_004ee830(fn_004ee830_Entry, fn_004ee830_Abi)
ABI_ENTRY_004ee8f0(Reloc_SwapVec2_Entry, Reloc_SwapVec2_Abi)
ABI_ENTRY_004ee9c0(Reloc_SwapVec3_Entry, Reloc_SwapVec3_Abi)
ABI_ENTRY_004eea40(Reloc_SwapVec4_Entry, Reloc_SwapVec4_Abi)
ABI_ENTRY_004eebe0(Reloc_SwapDword_Entry, Reloc_SwapDword_Abi)
ABI_ENTRY_004eec50(Reloc_SwapMatrixAndDword_Entry, Reloc_SwapMatrixAndDword_Abi)
ABI_ENTRY_004eeda0(Reloc_FixupTypedBlock_Entry, Reloc_FixupTypedBlock_Abi)

HOOK(0x004e6570, "FileStream_ReadChunk", FileStream_ReadChunk_Entry, nullptr);
HOOK(0x004e6740, "FileStream_ThreadLoop", FileStream_ThreadLoop_Entry, nullptr);
HOOK(0x004e7570, "fn_004e7570", fn_004e7570_Entry, nullptr);
HOOK(0x004e80b0, "Plane_FromPoints", Plane_FromPoints_Entry, nullptr);
HOOK(0x004eaaa0, "Reloc_FixupElementList", Reloc_FixupElementList_Entry, nullptr);
HOOK(0x004ed290, "SkinBinding_Create", SkinBinding_Create_Entry, nullptr);
HOOK(0x004ed410, "AnimSkinBinding_Create", AnimSkinBinding_Create_Entry, nullptr);
HOOK(0x004ee830, "fn_004ee830", fn_004ee830_Entry, nullptr);
HOOK(0x004ee8f0, "Reloc_SwapVec2", Reloc_SwapVec2_Entry, nullptr);
HOOK(0x004ee9c0, "Reloc_SwapVec3", Reloc_SwapVec3_Entry, nullptr);
HOOK(0x004eea40, "Reloc_SwapVec4", Reloc_SwapVec4_Entry, nullptr);
HOOK(0x004eebe0, "Reloc_SwapDword", Reloc_SwapDword_Entry, nullptr);
HOOK(0x004eec50, "Reloc_SwapMatrixAndDword", Reloc_SwapMatrixAndDword_Entry, nullptr);
HOOK(0x004eeda0, "Reloc_FixupTypedBlock", Reloc_FixupTypedBlock_Entry, nullptr);
