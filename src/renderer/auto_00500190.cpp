// Recovered: 0x00500190-0x00501e00. Small renderer vtable methods (vtable 0x00589990), two MSVC virtual-base
// adjustors, and the rest of zlib 1.1.4 trees.c (deflate Huffman trees; see auto_00500d60.cpp and
// auto_00501d90.cpp for the deflate_state layout). S2, Approach B.
//   0x00500190 fn_00500190
//   0x005001a0 fn_005001a0
//   0x005001d0 Renderer_VCreateIndexBuffer      (renderer vtable slot 98, +0x188)
//   0x005001f0 Renderer_GetDevice               (renderer vtable slot 92, +0x170)
//   0x00500200 Renderer_SetTextureCallbacks
//   0x00500220 Renderer_VCreateDrawState        (renderer vtable slot 6, +0x018)
//   0x00501510 Zlib_GenCodes
//   0x005017a0 Zlib_ScanTree
//   0x00501880 Zlib_SendTree
//   0x00501d90 Zlib_BuildBlTree
//   0x00501e00 Zlib_SendAllTrees
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

uint8_t* Renderer_CreateDrawState(uint8_t* r);                       // auto_004fb170.cpp
IndexBuffer* Renderer_CreateIndexBuffer(Renderer* r, uint32_t indexCount);  // auto_004fe3f0.cpp
uint32_t Zlib_BuildTree(uint8_t* s, uint8_t* desc);                   // auto_00500d60.cpp

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

const uint8_t* const kBlOrder = reinterpret_cast<const uint8_t*>(0x0057dc3c);  // zlib bl_order[19]

// zlib send_bits (inlined everywhere in the original): append `length` bits of `value` to bi_buf (+0x16b0);
// when bi_valid (+0x16b4) exceeds 16 - length (signed, jle), flush the 16-bit buffer to pending_buf (+0x08) at
// pending (+0x14), low byte first, and keep the bits that did not fit.
void TreesSendBits(uint8_t* s, uint32_t value, int32_t length)
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

// zlib send_code with s->bl_tree (+0xa74: code word, +0xa76: length word, 4-byte entries).
void TreesSendBlCode(uint8_t* s, uint32_t symbol)
{
    TreesSendBits(s, Field<uint16_t>(s, 0xa74 + symbol * 4), Field<uint16_t>(s, 0xa76 + symbol * 4));
}

// MSVC virtual-base conversion shared by 0x00500190 and 0x005001a0 (tail at 0x005001b0, not a function of its
// own): `adjusted` = ecx after the thunk's subtraction. Null when the full object (adjusted - 8) is null, else the
// vbptr (at adjusted - 4) plus the virtual base offset in vbtable entry 1.
uint32_t VirtualBaseFromVbptr(uint32_t adjusted)
{
    if (adjusted - 8 == 0)
        return 0;
    uint32_t vbptrAt = adjusted - 4;
    uint32_t vbtable = *reinterpret_cast<const uint32_t*>(vbptrAt);
    return *reinterpret_cast<const uint32_t*>(vbtable + 4) + vbptrAt;
}
}  // namespace

// 0x00500190 (ecx = subobject pointer): `sub ecx, 0xc` then the shared tail 0x005001b0. Returns null when the
// full object (ecx - 0x14) is null, else the vbptr at ecx - 0x10 plus vbtable[1]: the virtual base of the object
// that owns this subobject. No callers found by the export (indirect only). (not executed in the comparison
// scenarios)
uint32_t fn_00500190(uint32_t self)
{
    return VirtualBaseFromVbptr(self - 0xc);
}

// 0x005001a0 (ecx = subobject pointer): same as 0x00500190 with `sub ecx, 0x18`: full object ecx - 0x20, vbptr
// at ecx - 0x1c. (not executed in the comparison scenarios)
uint32_t fn_005001a0(uint32_t self)
{
    return VirtualBaseFromVbptr(self - 0x18);
}

// 0x005001d0 (ecx = renderer; [index count], callee pops): renderer vtable slot 98 (+0x188, tools/vtable.py
// 0x00589990). Moves ecx into EDI and calls Renderer_CreateIndexBuffer (0x004fe750); returns the new index
// buffer. (not executed in the comparison scenarios)
IndexBuffer* Renderer_VCreateIndexBuffer(Renderer* r, uint32_t indexCount)
{
    return Renderer_CreateIndexBuffer(r, indexCount);
}

// 0x005001f0 (ecx = renderer): renderer vtable slot 92 (+0x170). Returns the IDirect3DDevice9 at +0x14cc.
// (not executed in the comparison scenarios)
IDirect3DDevice9* Renderer_GetDevice(Renderer* r)
{
    return r->device;
}

// 0x00500200 (ecx = renderer; [load, release], callee pops): stores the two texture callbacks that
// Renderer_RegisterTexture (0x0050bb30) calls: load at +0x4a0, release at +0x4a4. Returns the load callback (left
// in EAX). Not in the renderer vtable; the owner of ECX is inferred from those offsets only. (not executed in the
// comparison scenarios)
uint32_t Renderer_SetTextureCallbacks(Renderer* r, uint32_t load, uint32_t release)
{
    Field<uint32_t>(r, 0x4a0) = load;
    Field<uint32_t>(r, 0x4a4) = release;
    return load;
}

// 0x00500220 (ecx = renderer): renderer vtable slot 6 (+0x018). `mov eax, ecx` and tail-jumps to
// Renderer_CreateDrawState (0x004fd000, eax = renderer); returns its result. (not executed in the comparison
// scenarios)
uint8_t* Renderer_VCreateDrawState(uint8_t* r)
{
    return Renderer_CreateDrawState(r);
}

// 0x00501510 (edx = bl_count, ebx = max_code, edi = tree; EBP is only saved; /GS cookie): zlib gen_codes():
// next_code[bits] = (next_code[bits - 1] + bl_count[bits - 1]) << 1 in 16 bits for bits 1..15, then each code
// n <= max_code (signed) with a nonzero Len (+2) gets next_code[len]++ bit-reversed over len bits in Code (+0).
// Same algorithm as the unhooked C++ copy in auto_00500d60.cpp (used by Zlib_BuildTree).
void Zlib_GenCodes(uint16_t* tree, int32_t maxCode, const uint16_t* blCount)
{
    uint16_t nextCode[16];
    uint16_t code = 0;
    for (int bits = 1; bits <= 15; ++bits)
    {
        code = static_cast<uint16_t>((code + blCount[bits - 1]) << 1);
        nextCode[bits] = code;
    }
    for (int32_t n = 0; n <= maxCode; ++n)
    {
        int32_t len = tree[n * 2 + 1];
        if (len == 0)
            continue;
        uint32_t c = nextCode[len]++;
        uint32_t res = 0;
        do
        {
            res |= c & 1;
            c >>= 1;
            res <<= 1;
        } while (--len > 0);
        tree[n * 2] = static_cast<uint16_t>(res >> 1);
    }
}

// 0x005017a0 (eax = tree, ecx = max_code, EBP is only overwritten; [deflate state], caller pops): zlib
// scan_tree(). Sets tree[max_code + 1].Len = 0xffff as a guard, then counts runs of equal code lengths into
// bl_tree frequencies (+0xa74): short runs add the run length to bl_tree[curlen], repeats of a nonzero length
// count REP_3_6 (+0xab4, plus one bl_tree[curlen] when it differs from the previous length), zero runs count
// REPZ_3_10 (+0xab8, run <= 10) or REPZ_11_138 (+0xabc). Run limits 138/3 (next zero), 6/3 (same), 7/4.
void Zlib_ScanTree(uint16_t* tree, int32_t maxCode, uint8_t* s)
{
    int32_t prevlen = -1;
    uint32_t nextlen = tree[1];
    int32_t count = 0;
    int32_t maxCount = 7;
    int32_t minCount = 4;
    if (nextlen == 0)
    {
        maxCount = 138;
        minCount = 3;
    }
    tree[(maxCode + 1) * 2 + 1] = 0xffff;
    if (maxCode < 0)
        return;
    for (int32_t n = 0; n <= maxCode; ++n)
    {
        uint32_t curlen = nextlen;
        nextlen = tree[(n + 1) * 2 + 1];
        if (++count < maxCount && curlen == nextlen)
            continue;
        if (count < minCount)
        {
            Field<uint16_t>(s, 0xa74 + curlen * 4) = static_cast<uint16_t>(Field<uint16_t>(s, 0xa74 + curlen * 4) + count);
        }
        else if (curlen != 0)
        {
            if (curlen != static_cast<uint32_t>(prevlen))
                Field<uint16_t>(s, 0xa74 + curlen * 4)++;
            Field<uint16_t>(s, 0xab4)++;
        }
        else if (count <= 10)
        {
            Field<uint16_t>(s, 0xab8)++;
        }
        else
        {
            Field<uint16_t>(s, 0xabc)++;
        }
        count = 0;
        prevlen = static_cast<int32_t>(curlen);
        if (nextlen == 0)
        {
            maxCount = 138;
            minCount = 3;
        }
        else if (curlen == nextlen)
        {
            maxCount = 6;
            minCount = 3;
        }
        else
        {
            maxCount = 7;
            minCount = 4;
        }
    }
}

// 0x00501880 (eax = deflate state, ecx = tree, EBX is only overwritten; [max_code], caller pops): zlib
// send_tree(). Same run detection as scan_tree (without the guard write), but emits the bl_tree codes: a short
// run sends bl_tree[curlen] count times; a repeat of a nonzero length sends bl_tree[curlen] once if it differs from
// the previous length (count - 1), then REP_3_6 (16) and count - 3 in 2 bits; zero runs send REPZ_3_10 (17) and
// count - 3 in 3 bits, or REPZ_11_138 (18) and count - 11 in 7 bits. (not executed in the comparison scenarios)
void Zlib_SendTree(uint8_t* s, const uint16_t* tree, int32_t maxCode)
{
    int32_t prevlen = -1;
    uint32_t nextlen = tree[1];
    int32_t count = 0;
    int32_t maxCount = 7;
    int32_t minCount = 4;
    if (nextlen == 0)
    {
        maxCount = 138;
        minCount = 3;
    }
    if (maxCode < 0)
        return;
    for (int32_t n = 0; n <= maxCode; ++n)
    {
        uint32_t curlen = nextlen;
        nextlen = tree[(n + 1) * 2 + 1];
        if (++count < maxCount && curlen == nextlen)
            continue;
        if (count < minCount)
        {
            do
            {
                TreesSendBlCode(s, curlen);
            } while (--count != 0);
        }
        else if (curlen != 0)
        {
            if (curlen != static_cast<uint32_t>(prevlen))
            {
                TreesSendBlCode(s, curlen);
                count--;
            }
            TreesSendBlCode(s, 16);
            TreesSendBits(s, static_cast<uint32_t>(count - 3), 2);
        }
        else if (count <= 10)
        {
            TreesSendBlCode(s, 17);
            TreesSendBits(s, static_cast<uint32_t>(count - 3), 3);
        }
        else
        {
            TreesSendBlCode(s, 18);
            TreesSendBits(s, static_cast<uint32_t>(count - 11), 7);
        }
        count = 0;
        prevlen = static_cast<int32_t>(curlen);
        if (nextlen == 0)
        {
            maxCount = 138;
            minCount = 3;
        }
        else if (curlen == nextlen)
        {
            maxCount = 6;
            minCount = 3;
        }
        else
        {
            maxCount = 7;
            minCount = 4;
        }
    }
}

// 0x00501d90 (eax = deflate state, EBP passed through untouched to its callees): zlib build_bl_tree(). Scans
// dyn_ltree (+0x8c, max_code l_desc +0xb14) and dyn_dtree (+0x980, max_code d_desc +0xb20), builds bl_tree from
// bl_desc (+0xb28) with Zlib_BuildTree, finds the last nonzero bl_tree length in bl_order (0x0057dc3c) from 18
// down to 3 (2 when none), adds 3 * (max + 1) + 14 to opt_len (+0x16a0) and returns max_blindex.
int32_t Zlib_BuildBlTree(uint8_t* s)
{
    Zlib_ScanTree(&Field<uint16_t>(s, 0x8c), Field<int32_t>(s, 0xb14), s);
    Zlib_ScanTree(&Field<uint16_t>(s, 0x980), Field<int32_t>(s, 0xb20), s);
    Zlib_BuildTree(s, s + 0xb28);
    int32_t maxBlIndex = 18;
    do
    {
        if (Field<uint16_t>(s, 0xa76 + kBlOrder[maxBlIndex] * 4) != 0)
            break;
        --maxBlIndex;
    } while (maxBlIndex >= 3);
    Field<uint32_t>(s, 0x16a0) += static_cast<uint32_t>(maxBlIndex * 3 + 0x11);
    return maxBlIndex;
}

// 0x00501e00 (eax = deflate state; [lcodes, dcodes, blcodes], caller pops): zlib send_all_trees(). Sends
// lcodes - 257 (5 bits), dcodes - 1 (5 bits), blcodes - 4 (4 bits), the bl_tree lengths (+0xa76) in bl_order for
// the first blcodes ranks (3 bits each, signed loop), then the literal tree (+0x8c, lcodes - 1) and the distance tree
// (+0x980, dcodes - 1) with send_tree. (not executed in the comparison scenarios)
void Zlib_SendAllTrees(uint8_t* s, int32_t lcodes, int32_t dcodes, int32_t blcodes)
{
    TreesSendBits(s, static_cast<uint32_t>(lcodes - 257), 5);
    TreesSendBits(s, static_cast<uint32_t>(dcodes - 1), 5);
    TreesSendBits(s, static_cast<uint32_t>(blcodes - 4), 4);
    for (int32_t rank = 0; rank < blcodes; ++rank)
        TreesSendBits(s, Field<uint16_t>(s, 0xa76 + kBlOrder[rank] * 4), 3);
    Zlib_SendTree(s, &Field<uint16_t>(s, 0x8c), lcodes - 1);
    Zlib_SendTree(s, &Field<uint16_t>(s, 0x980), dcodes - 1);
}

static uint32_t fn_00500190_Abi(uint32_t ecx)
{
    return fn_00500190(ecx);
}
static uint32_t fn_005001a0_Abi(uint32_t ecx)
{
    return fn_005001a0(ecx);
}
static uint32_t Renderer_VCreateIndexBuffer_Abi(uint32_t ecx, uint32_t indexCount)
{
    return u(Renderer_VCreateIndexBuffer(reinterpret_cast<Renderer*>(ecx), indexCount));
}
static uint32_t Renderer_GetDevice_Abi(uint32_t ecx)
{
    return u(Renderer_GetDevice(reinterpret_cast<Renderer*>(ecx)));
}
static uint32_t Renderer_SetTextureCallbacks_Abi(uint32_t ecx, uint32_t load, uint32_t release)
{
    return Renderer_SetTextureCallbacks(reinterpret_cast<Renderer*>(ecx), load, release);
}
static uint32_t Renderer_VCreateDrawState_Abi(uint32_t ecx)
{
    return u(Renderer_VCreateDrawState(reinterpret_cast<uint8_t*>(ecx)));
}
static uint32_t Zlib_GenCodes_Abi(uint32_t edx, uint32_t ebx, uint32_t edi, uint32_t /*ebp*/)
{
    Zlib_GenCodes(reinterpret_cast<uint16_t*>(edi), static_cast<int32_t>(ebx), reinterpret_cast<const uint16_t*>(edx));
    return 0;
}
static uint32_t Zlib_ScanTree_Abi(uint32_t eax, uint32_t ecx, uint32_t /*ebp*/, uint32_t s)
{
    Zlib_ScanTree(reinterpret_cast<uint16_t*>(eax), static_cast<int32_t>(ecx), reinterpret_cast<uint8_t*>(s));
    return 0;
}
static void Zlib_SendTree_Abi(uint32_t eax, uint32_t ecx, uint32_t /*ebx*/, uint32_t maxCode)
{
    Zlib_SendTree(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<const uint16_t*>(ecx), static_cast<int32_t>(maxCode));
}
static uint32_t Zlib_BuildBlTree_Abi(uint32_t eax, uint32_t /*ebp*/)
{
    return static_cast<uint32_t>(Zlib_BuildBlTree(reinterpret_cast<uint8_t*>(eax)));
}
static void Zlib_SendAllTrees_Abi(uint32_t eax, uint32_t lcodes, uint32_t dcodes, uint32_t blcodes)
{
    Zlib_SendAllTrees(reinterpret_cast<uint8_t*>(eax), static_cast<int32_t>(lcodes), static_cast<int32_t>(dcodes),
                      static_cast<int32_t>(blcodes));
}
ABI_ENTRY_00500190(fn_00500190_Entry, fn_00500190_Abi)
ABI_ENTRY_005001a0(fn_005001a0_Entry, fn_005001a0_Abi)
ABI_ENTRY_005001d0(Renderer_VCreateIndexBuffer_Entry, Renderer_VCreateIndexBuffer_Abi)
ABI_ENTRY_005001f0(Renderer_GetDevice_Entry, Renderer_GetDevice_Abi)
ABI_ENTRY_00500200(Renderer_SetTextureCallbacks_Entry, Renderer_SetTextureCallbacks_Abi)
ABI_ENTRY_00500220(Renderer_VCreateDrawState_Entry, Renderer_VCreateDrawState_Abi)
ABI_ENTRY_00501510(Zlib_GenCodes_Entry, Zlib_GenCodes_Abi)
ABI_ENTRY_005017a0(Zlib_ScanTree_Entry, Zlib_ScanTree_Abi)
ABI_ENTRY_00501880(Zlib_SendTree_Entry, Zlib_SendTree_Abi)
ABI_ENTRY_00501d90(Zlib_BuildBlTree_Entry, Zlib_BuildBlTree_Abi)
ABI_ENTRY_00501e00(Zlib_SendAllTrees_Entry, Zlib_SendAllTrees_Abi)
HOOK(0x00500190, "fn_00500190", fn_00500190_Entry, nullptr);
HOOK(0x005001a0, "fn_005001a0", fn_005001a0_Entry, nullptr);
HOOK(0x005001d0, "Renderer_VCreateIndexBuffer", Renderer_VCreateIndexBuffer_Entry, nullptr);
HOOK(0x005001f0, "Renderer_GetDevice", Renderer_GetDevice_Entry, nullptr);
HOOK(0x00500200, "Renderer_SetTextureCallbacks", Renderer_SetTextureCallbacks_Entry, nullptr);
HOOK(0x00500220, "Renderer_VCreateDrawState", Renderer_VCreateDrawState_Entry, nullptr);
HOOK(0x00501510, "Zlib_GenCodes", Zlib_GenCodes_Entry, nullptr);
HOOK(0x005017a0, "Zlib_ScanTree", Zlib_ScanTree_Entry, nullptr);
HOOK(0x00501880, "Zlib_SendTree", Zlib_SendTree_Entry, nullptr);
HOOK(0x00501d90, "Zlib_BuildBlTree", Zlib_BuildBlTree_Entry, nullptr);
HOOK(0x00501e00, "Zlib_SendAllTrees", Zlib_SendAllTrees_Entry, nullptr);
