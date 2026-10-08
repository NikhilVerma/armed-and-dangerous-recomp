// Recovered: loader batch loader_02 (0x004e8620-0x004f60b0: mesh vertex/triangle accessors, vertex-element decoding,
// path helpers and file open/find wrappers over the two search roots 0x0063a204 / 0x0063a208, directory creation,
// chunk-stream helpers). Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle
// (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified from the disassembly. "AL = x" returns keep the upper 24 bits EAX had in the
// original where that is cheap. Stack-cookie checks (__security_check_cookie 0x0053c96a, reached by a tail jump that
// returns to the caller) are not reproduced; they never fire.
#include <windows.h>

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
inline uint8_t* B(uint32_t v)
{
    return reinterpret_cast<uint8_t*>(v);
}
// EAX with AL replaced (setcc / mov al, imm on top of an earlier value).
inline uint32_t Lo8(uint32_t eax, uint32_t al)
{
    return (eax & 0xffffff00u) | (al & 0xffu);
}
// Imports the game calls through its IAT slot (keeps import hooks).
template <class F>
F Imp(uintptr_t slot)
{
    return *reinterpret_cast<F*>(slot);
}
// _makepath (0x0053859e, cdecl, 5 arguments).
inline uint32_t MakePath(uint32_t path, uint32_t drive, uint32_t dir, uint32_t fname, uint32_t ext)
{
    return GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0053859e)(
        path, drive, dir, fname, ext);
}
// _splitpath (0x00538456, cdecl, 5 arguments).
inline void SplitPath(uint32_t path, uint32_t drive, uint32_t dir, uint32_t fname, uint32_t ext)
{
    GameFunction<void(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00538456)(path, drive, dir,
                                                                                               fname, ext);
}
// The two search roots tried in order by the file helpers (globals 0x0063a204 and 0x0063a208, string pointers).
inline uint32_t Root1()
{
    return GameGlobal<uint32_t>(0x0063a204);
}
inline uint32_t Root2()
{
    return GameGlobal<uint32_t>(0x0063a208);
}
}  // namespace

// Entry stub for a function whose real inputs are EBX, EDI and one caller-popped stack argument (symbols/abi.csv
// lists 0x004f2030 as "inputs [] stack 6", so the generated stub would not forward EBX/EDI). Same shape as
// src/gen/entry_stubs.h: save registers, push the inputs, call the cdecl impl, plain ret.
// clang-format off
#define L2_ENTRY_EBX_EDI_S1(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push dword ptr [esp + 28] __asm push edi __asm push ebx __asm call impl __asm add esp, 12 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret }
// clang-format on

// Prototypes (functions of this file call each other directly).
uint32_t fn_004edf80(int32_t* node);
uint32_t fn_004effa0(float* out, const uint8_t* elem, const float* translate, const uint8_t* streams, uint32_t index,
                     const float* scale);
uint32_t fn_004ef990(float* out, uint8_t* mesh, uint32_t index);
uint32_t Path_CopyBackslash(const char* src, char* dst);
uint32_t Path_CreateDirectories(const char* path);
uint32_t File_FindFirstIn(uint8_t* state, const char* pattern, uint32_t outName);
uint32_t File_FindFirst(uint8_t* state, uint32_t outName, const char* pattern);

// ================================================================================================================
// 0x004edf80 - 0x004f0e60: tree extents, chunk blocks, mesh accessors, vertex-element decoding
// ================================================================================================================

// 0x004edf80 (stack: node; caller pops): node {+0 extent, +0xc child count, +0x10 children[]}: when +0 is 0,
// computes every child's extent first (recursively) and sets +0 = max over children of (child+0 + child+4) (signed,
// 0 for no children). Returns +0 when it was already set, else EAX as left by the loops (the last child's sum, or the
// child count when there is none). Callers 0x004ee010 and itself. guess: layout extent of a nested block tree.
uint32_t fn_004edf80(int32_t* node)
{
    int32_t eax = node[0];
    if (eax != 0)
        return static_cast<uint32_t>(eax);
    int32_t best = 0;
    eax = node[3];
    if (eax > 0)
    {
        int32_t i = 0;
        do
        {
            fn_004edf80(reinterpret_cast<int32_t*>(node[4 + i]));
            eax = node[3];
            ++i;
        } while (i < eax);
        if (eax > 0)
        {
            const int32_t* const* child = reinterpret_cast<const int32_t* const*>(node + 4);
            for (int32_t n = eax; n != 0; --n, ++child)
            {
                eax = (*child)[1] + (*child)[0];
                if (best < eax)
                    best = eax;
            }
        }
    }
    node[0] = best;
    return static_cast<uint32_t>(eax);
}

static uint32_t fn_004edf80_Abi(uint32_t a0)
{
    return fn_004edf80(reinterpret_cast<int32_t*>(a0));
}
ABI_ENTRY_004edf80(fn_004edf80_Entry, fn_004edf80_Abi)
HOOK(0x004edf80, "fn_004edf80", fn_004edf80_Entry, nullptr);

// 0x004ef910 (al = flags, ecx = row count, edx = heap; stack: byte type; caller pops): allocates a typed table block
// from the heap (0x004e1e90(heap, (rowSize + 4) * count + 0xc, 1, 2)), rowSize = [0x005cabe4 + type * 12]. Header:
// byte +0 type, +1 = 3, +2 = 3, +3 flags, dword +8 = 0, +4 = handler [0x005cabe0 + type * 12] (type 1: 0x004ebb50,
// type 2: 0x004ebbe0, ...). Unless flags bit 2 is set, +0xc holds `count` row pointers into the rows that follow the
// pointer table. Returns the block. Caller 0x00493f70. symbols/abi.csv lists 3 stack arguments; only the first is read.
uint32_t fn_004ef910(uint32_t flags, uint32_t count, uint32_t heap, uint32_t type)
{
    const uint8_t t = static_cast<uint8_t>(type);
    const uint32_t rowSize = GameGlobal<uint32_t>(0x005cabe4 + t * 12u);
    uint8_t* blk = B(orig::fn_004e1e90(heap, (rowSize + 4) * count + 0xc, 1, 2));
    blk[3] = static_cast<uint8_t>(flags);
    blk[0] = t;
    blk[2] = 3;
    blk[1] = 3;
    Field<uint32_t>(blk, 8) = 0;
    if ((~flags & 4) != 0)
    {
        uint32_t* slot = reinterpret_cast<uint32_t*>(blk + 0xc);
        uint32_t row = u(slot + count);
        for (uint32_t n = count; n != 0; --n)
        {
            *slot++ = row;
            row += rowSize;
        }
    }
    Field<uint32_t>(blk, 4) = GameGlobal<uint32_t>(0x005cabe0 + blk[0] * 12u);
    return u(blk);
}

static uint32_t fn_004ef910_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t /*a1*/,
                                uint32_t /*a2*/)
{
    return fn_004ef910(eax, ecx, edx, a0);
}
ABI_ENTRY_004ef910(fn_004ef910_Entry, fn_004ef910_Abi)
HOOK(0x004ef910, "fn_004ef910", fn_004ef910_Entry, nullptr);

// 0x004ef990 (ecx = out vec3, edx = vertex source, esi = vertex index): reads one vertex position. With an element
// decoder at +0x44 (d): 0x004effa0 (ecx = out, edx = d+0xac element, edi = d+0x20 translate; d+0x70 streams, index,
// d+0x10 scale) and returns its EAX; else copies the 3 dwords at [+0x28] + [[+0x48] + index * 4] * 12 and returns the
// third. Callers 0x00412450, 0x004efb50, 0x004efd00.
uint32_t fn_004ef990(float* out, uint8_t* mesh, uint32_t index)
{
    uint8_t* d = Field<uint8_t*>(mesh, 0x44);
    if (d != nullptr)
        return fn_004effa0(out, d + 0xac, reinterpret_cast<const float*>(d + 0x20), d + 0x70, index,
                           reinterpret_cast<const float*>(d + 0x10));
    const uint32_t* v = reinterpret_cast<const uint32_t*>(Field<uint32_t>(mesh, 0x28) +
                                                          Field<uint32_t*>(mesh, 0x48)[index] * 0xc);
    uint32_t* o = reinterpret_cast<uint32_t*>(out);
    o[0] = v[0];
    o[1] = v[1];
    o[2] = v[2];
    return v[2];
}

static uint32_t fn_004ef990_Abi(uint32_t ecx, uint32_t edx, uint32_t esi)
{
    return fn_004ef990(reinterpret_cast<float*>(ecx), B(edx), esi);
}
ABI_ENTRY_004ef990(fn_004ef990_Entry, fn_004ef990_Abi)
HOOK(0x004ef990, "fn_004ef990", fn_004ef990_Entry, nullptr);

// 0x004efa50 (eax = mesh part): triangle count. Without a strip (+0x24 = 0): [+0x20] / 3 (signed). With a strip s:
// the number of non-degenerate triangles of the word index strip [s+0x3c] (length [s+0x10], unsigned; triangles
// whose three indices all differ). Caller 0x00412450.
uint32_t fn_004efa50(uint8_t* part)
{
    uint8_t* s = Field<uint8_t*>(part, 0x24);
    if (s == nullptr)
        return static_cast<uint32_t>(Field<int32_t>(part, 0x20) / 3);
    const uint16_t* p = Field<uint16_t*>(s, 0x3c);
    const uint32_t len = Field<uint32_t>(s, 0x10);
    uint32_t count = 0;
    if (len > 2)
    {
        for (uint32_t n = len - 2; n != 0; --n, ++p)
        {
            if (p[0] != p[1] && p[1] != p[2] && p[0] != p[2])
                ++count;
        }
    }
    return count;
}

static uint32_t fn_004efa50_Abi(uint32_t eax)
{
    return fn_004efa50(B(eax));
}
ABI_ENTRY_004efa50(fn_004efa50_Entry, fn_004efa50_Abi)
HOOK(0x004efa50, "fn_004efa50", fn_004efa50_Entry, nullptr);

// 0x004efab0 (eax = mesh part, ecx = out 3 words, edx = triangle index): the triangle's three vertex indices. With a
// strip (+0x24) and a decoder ([+4]+0x44): words i, i+1, i+2 of the strip [[+0x24]+0x3c], the last two swapped on odd
// triangles (out[(i&1)+1], out[((i&1)+1)^3]); AL = 0 for a degenerate triangle, else 1. Otherwise the low words of
// the three dwords at [+0x28] + i * 12 and AL = 1. Caller 0x00412450.
uint32_t fn_004efab0(uint8_t* part, uint16_t* out, uint32_t i)
{
    if (Field<uint32_t>(part, 0x24) != 0 && Field<uint32_t>(Field<uint8_t*>(part, 4), 0x44) != 0)
    {
        const uint16_t* s = Field<uint16_t*>(Field<uint8_t*>(part, 0x24), 0x3c);
        out[0] = s[i];
        const uint32_t k = i & 1;
        out[k + 1] = s[i + 1];
        out[(k + 1) ^ 3] = s[i + 2];
        const uint16_t a = out[0];
        const uint16_t b = out[1];
        const uint16_t c = out[2];
        if (a == b || a == c || b == c)
            return a & 0xff00u;
        return Lo8(a, 1);
    }
    const uint32_t t = i * 3;
    const uint8_t* idx = Field<uint8_t*>(part, 0x28);
    out[0] = *reinterpret_cast<const uint16_t*>(idx + t * 4);
    out[1] = *reinterpret_cast<const uint16_t*>(idx + t * 4 + 4);
    const uint16_t w = *reinterpret_cast<const uint16_t*>(idx + (t + 1) * 4 + 4);
    out[2] = w;
    return Lo8(((t + 1) & 0xffff0000u) | w, 1);
}

static uint32_t fn_004efab0_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return fn_004efab0(B(eax), reinterpret_cast<uint16_t*>(ecx), edx);
}
ABI_ENTRY_004efab0(fn_004efab0_Entry, fn_004efab0_Abi)
HOOK(0x004efab0, "fn_004efab0", fn_004efab0_Entry, nullptr);

namespace
{
// One vertex of a triangle for 0x004efb50 / 0x004efd00: through the decoder at [src+0x44] (0x004effa0) or by copying
// 3 dwords of [src+0x28] + [[src+0x48] + index * 4] * 12. Returns EAX as left: the decoder's result, or the copied
// y dword.
uint32_t TriVertex(uint8_t* src, uint32_t index, float* out)
{
    uint8_t* d = Field<uint8_t*>(src, 0x44);
    if (d != nullptr)
        return fn_004effa0(out, d + 0xac, reinterpret_cast<const float*>(d + 0x20), d + 0x70, index,
                           reinterpret_cast<const float*>(d + 0x10));
    const uint32_t* v = reinterpret_cast<const uint32_t*>(Field<uint32_t>(src, 0x28) +
                                                          Field<uint32_t*>(src, 0x48)[index] * 0xc);
    uint32_t* o = reinterpret_cast<uint32_t*>(out);
    o[0] = v[0];
    o[1] = v[1];
    o[2] = v[2];
    return v[1];
}

// The shared body of 0x004efb50 (stride 3 floats) and 0x004efd00 (stride 4 floats): AL = 0 for a degenerate strip
// triangle (EAX = triangle index with AL cleared), else the three positions are written and EAX = the last vertex
// read's EAX with AL = 1 (before 0x004efd00 sets w).
uint32_t TrianglePositions(uint8_t* part, uint32_t tri, float* out, uint32_t stride)
{
    if (Field<uint32_t>(part, 0x24) != 0 && Field<uint32_t>(Field<uint8_t*>(part, 4), 0x44) != 0)
    {
        const uint16_t* s = Field<uint16_t*>(Field<uint8_t*>(part, 0x24), 0x3c);
        const uint16_t b = s[tri + 1];
        const uint16_t a = s[tri];
        const uint16_t c = s[tri + 2];
        if (a == b || a == c || b == c)
            return Lo8(tri, 0);
        const uint32_t k = (tri & 1) + 1;
        fn_004ef990(out, Field<uint8_t*>(part, 4), a);
        fn_004ef990(out + k * stride, Field<uint8_t*>(part, 4), b);
        const uint32_t r = fn_004ef990(out + (k ^ 3) * stride, Field<uint8_t*>(part, 4), c);
        return Lo8(r, 1);
    }
    const uint32_t* idx = reinterpret_cast<const uint32_t*>(Field<uint8_t*>(part, 0x28) + tri * 0xc);
    TriVertex(Field<uint8_t*>(part, 4), idx[0], out);
    TriVertex(Field<uint8_t*>(part, 4), idx[1], out + stride);
    const uint32_t r = TriVertex(Field<uint8_t*>(part, 4), idx[2], out + 2 * stride);
    return Lo8(r, 1);
}
}  // namespace

// 0x004efb50 (stack: mesh part, triangle index, out 9 floats; ret 0xc): the triangle's three vertex positions (vec3
// each), via 0x004efab0's index rules (strip with odd-triangle swap, or the 3-dword index list at +0x28) and
// 0x004ef990 / the decoder 0x004effa0. AL = 0 for a degenerate strip triangle, else 1. Callers 0x004552f0,
// 0x0046ed30.
uint32_t fn_004efb50(uint8_t* part, uint32_t tri, float* out)
{
    return TrianglePositions(part, tri, out, 3);
}

static uint32_t fn_004efb50_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004efb50(B(a0), a1, reinterpret_cast<float*>(a2));
}
ABI_ENTRY_004efb50(fn_004efb50_Entry, fn_004efb50_Abi)
HOOK(0x004efb50, "fn_004efb50", fn_004efb50_Entry, nullptr);

// 0x004efd00 (ecx = mesh part; stack: triangle index, out 3 vec4; ret 8): as 0x004efb50 with 16-byte vertices; on
// success also sets the three w components to 1.0 and returns EAX = 0x3f800001 (1.0f's bits with AL = 1). Caller
// 0x00416930.
uint32_t fn_004efd00(uint8_t* part, uint32_t tri, float* out)
{
    const uint32_t r = TrianglePositions(part, tri, out, 4);
    if (static_cast<uint8_t>(r) == 0)
        return r;
    uint32_t* o = reinterpret_cast<uint32_t*>(out);
    o[0xb] = 0x3f800000;
    o[7] = 0x3f800000;
    o[3] = 0x3f800000;
    return 0x3f800001;
}

static uint32_t fn_004efd00_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004efd00(B(ecx), a0, reinterpret_cast<float*>(a1));
}
ABI_ENTRY_004efd00(fn_004efd00_Entry, fn_004efd00_Abi)
HOOK(0x004efd00, "fn_004efd00", fn_004efd00_Entry, nullptr);

// 0x004effa0 (ecx = out vec3, edx = element {byte stream, byte offset, byte type}, edi = translate or null; stack:
// streams (0x14 bytes each: +0 base, +4 stride), vertex index, scale or null; ret 0xc): decodes one vertex element at
// base + stride * index + offset: types 7/8 three shorts as floats, 0xb/0xc three shorts times [0x005bcb78]
// (3.0518509e-05 = 1/32767), 0xf/0x10 three floats; any other type gives (0, 0, 0). Jump table 0x004f009c with index
// bytes 0x004f00ac. When scale and translate are both given: out = out * scale + translate per component. Returns EAX
// as left: the last short (sign-extended), the third float's bits, or the element address for other types.
// Callers 0x00412450, 0x00448870, 0x00449480, 0x004ef990, 0x004efb50, 0x004efd00.
uint32_t fn_004effa0(float* out, const uint8_t* elem, const float* translate, const uint8_t* streams, uint32_t index,
                     const float* scale)
{
    const uint8_t* s = streams + elem[0] * 0x14u;
    const uint8_t* p = B(Field<uint32_t>(s, 4) * index + elem[1] + Field<uint32_t>(s, 0));
    uint32_t eax = u(p);
    switch (elem[2])
    {
    case 0xf:
    case 0x10:
    {
        const uint32_t* v = reinterpret_cast<const uint32_t*>(p);
        uint32_t* o = reinterpret_cast<uint32_t*>(out);
        o[0] = v[0];
        o[1] = v[1];
        eax = v[2];
        o[2] = eax;
        break;
    }
    case 0xb:
    case 0xc:
    {
        const int16_t* v = reinterpret_cast<const int16_t*>(p);
        const float k = GameGlobal<float>(0x005bcb78);
        out[0] = static_cast<float>(static_cast<int32_t>(v[0])) * k;
        out[1] = static_cast<float>(static_cast<int32_t>(v[1])) * k;
        eax = static_cast<uint32_t>(static_cast<int32_t>(v[2]));
        out[2] = static_cast<float>(static_cast<int32_t>(v[2])) * k;
        break;
    }
    case 7:
    case 8:
    {
        const int16_t* v = reinterpret_cast<const int16_t*>(p);
        out[0] = static_cast<float>(static_cast<int32_t>(v[0]));
        out[1] = static_cast<float>(static_cast<int32_t>(v[1]));
        eax = static_cast<uint32_t>(static_cast<int32_t>(v[2]));
        out[2] = static_cast<float>(static_cast<int32_t>(v[2]));
        break;
    }
    default:
    {
        uint32_t* o = reinterpret_cast<uint32_t*>(out);
        o[2] = 0;
        o[1] = 0;
        o[0] = 0;
        break;
    }
    }
    if (scale != nullptr && translate != nullptr)
    {
        out[0] = out[0] * scale[0] + translate[0];
        out[1] = scale[1] * out[1] + translate[1];
        out[2] = scale[2] * out[2] + translate[2];
    }
    return eax;
}

static uint32_t fn_004effa0_Abi(uint32_t ecx, uint32_t edx, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004effa0(reinterpret_cast<float*>(ecx), B(edx), reinterpret_cast<const float*>(edi), B(a0), a1,
                       reinterpret_cast<const float*>(a2));
}
ABI_ENTRY_004effa0(fn_004effa0_Entry, fn_004effa0_Abi)
HOOK(0x004effa0, "fn_004effa0", fn_004effa0_Entry, nullptr);

// ================================================================================================================
// 0x004f19b0 - 0x004f2030: paths and files over the search roots
// ================================================================================================================

// 0x004f19b0 (eax = name, esi = out path): _makepath(out, 0, root 1 [0x0063a204], name, 0). Returns out. Callers
// 0x004fb360, 0x00500890.
uint32_t fn_004f19b0(uint32_t name, uint32_t out)
{
    MakePath(out, 0, Root1(), name, 0);
    return out;
}

static uint32_t fn_004f19b0_Abi(uint32_t eax, uint32_t esi)
{
    return fn_004f19b0(eax, esi);
}
ABI_ENTRY_004f19b0(fn_004f19b0_Entry, fn_004f19b0_Abi)
HOOK(0x004f19b0, "fn_004f19b0", fn_004f19b0_Entry, nullptr);

// 0x004f19d0 (eax = source, edi = destination; ECX is only a `push ecx` slot): copies the string, then replaces every
// '/' with '\\' (recomputing the length after each character). Returns the final length (0 for an empty string).
// Callers: the file helpers (File_OpenForWrite/Append, 0x004f1ae0, 0x004f1b40, 0x004f1c90, 0x004f1f40, ...).
uint32_t Path_CopyBackslash(const char* src, char* dst)
{
    const char* s = src;
    char* d = dst;
    char c;
    do
    {
        c = *s++;
        *d++ = c;
    } while (c != 0);
    uint32_t len = static_cast<uint32_t>(std::strlen(dst));
    if (len == 0)
        return 0;
    uint32_t i = 0;
    do
    {
        if (dst[i] == '/')
            dst[i] = '\\';
        ++i;
        len = static_cast<uint32_t>(std::strlen(dst));
    } while (i < len);
    return len;
}

static uint32_t Path_CopyBackslash_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t edi)
{
    return Path_CopyBackslash(reinterpret_cast<const char*>(eax), reinterpret_cast<char*>(edi));
}
ABI_ENTRY_004f19d0(Path_CopyBackslash_Entry, Path_CopyBackslash_Abi)
HOOK(0x004f19d0, "Path_CopyBackslash", Path_CopyBackslash_Entry, nullptr);

// 0x004f1ae0 (stack: path; caller pops): DeleteFileA (IAT 0x0056b0c4) on the path with '/' turned into '\\'
// (0x004f19d0, 0x100-byte local). Returns 1 when it succeeded, else 0. ABI lists ecx/ebx/esi/ebp: not read (cookie
// helper inputs). Caller AsyncFile_Close (0x004e62c0). Not executed in recorded runs.
uint32_t File_Delete(const char* path)
{
    char buf[0x100];
    Path_CopyBackslash(path, buf);
    const BOOL ok = Imp<decltype(&DeleteFileA)>(0x0056b0c4)(buf);
    return ok != 0 ? 1u : 0u;
}

static uint32_t File_Delete_Abi(uint32_t /*ecx*/, uint32_t /*ebx*/, uint32_t /*esi*/, uint32_t /*ebp*/, uint32_t a0)
{
    return File_Delete(reinterpret_cast<const char*>(a0));
}
ABI_ENTRY_004f1ae0(File_Delete_Entry, File_Delete_Abi)
HOOK(0x004f1ae0, "File_Delete", File_Delete_Entry, nullptr);

// 0x004f1b40 (stack: path; caller pops; ECX not read): creates every directory along the path: copies it ('/' ->
// '\\', 0x004f19d0), splits it (_splitpath) and rebuilds it without the drive (_makepath(path, 0, dir, fname, ext));
// then, starting from the drive string, appends each component (the last one included) and for each - once any
// component character has been seen - _findfirst (0x0053af29): found -> _findclose (0x0053aea5) and fail unless it is
// a directory (attrib & 0x10); not found -> _mkdir (0x0053b00c), fail when it returns -1. Separators are copied
// through. AL = 1 on success, 0 on failure. Frame 0x620 bytes as in the original (drive +0, path +0x104, fname
// +0x204, ext +0x304, dir +0x404, _finddata_t +0x504). Caller 0x004f1e00.
uint32_t Path_CreateDirectories(const char* path)
{
    uint8_t f[0x620];
    char* const drive = reinterpret_cast<char*>(f);
    char* const full = reinterpret_cast<char*>(f + 0x104);
    uint32_t seen = 0;
    Path_CopyBackslash(path, full);
    SplitPath(u(full), u(drive), u(f + 0x404), u(f + 0x204), u(f + 0x304));
    MakePath(u(full), 0, u(f + 0x404), u(f + 0x204), u(f + 0x304));
    char* d = drive + std::strlen(drive);
    const char* s = full;
    if (*s == 0)
        return 1;
    for (;;)
    {
        char c = *s;
        while (c != 0 && c != '\\' && c != '/')
        {
            ++seen;
            *d++ = c;
            c = *++s;
        }
        *d = 0;
        if (seen != 0)
        {
            const intptr_t h = GameFunction<intptr_t(__cdecl*)(const char*, void*)>(0x0053af29)(drive, f + 0x504);
            if (h != -1)
            {
                GameFunction<int(__cdecl*)(intptr_t)>(0x0053aea5)(h);
                if ((f[0x504] & 0x10) == 0)
                    return 0;
            }
            else if (GameFunction<int(__cdecl*)(const char*)>(0x0053b00c)(drive) == -1)
            {
                return 0;
            }
        }
        while (*s == '\\' || *s == '/')
            *d++ = *s++;
        if (*s == 0)
            return 1;
    }
}

static uint32_t Path_CreateDirectories_Abi(uint32_t /*ecx*/, uint32_t a0)
{
    return Path_CreateDirectories(reinterpret_cast<const char*>(a0));
}
ABI_ENTRY_004f1b40(Path_CreateDirectories_Entry, Path_CreateDirectories_Abi)
HOOK(0x004f1b40, "Path_CreateDirectories", Path_CreateDirectories_Entry, nullptr);

// 0x004f1c90 (esi = file {+0 handle, +4 path}; stack: name; caller pops): opens the file for reading: tries
// root 1 [0x0063a204] + name, root 2 [0x0063a208] + name, then the name as given (each _makepath into a 0x100-byte
// local, copied to file+4 with '/' -> '\\' by 0x004f19d0), CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0,
// OPEN_EXISTING, 0, 0) (IAT 0x0056b0c0); INVALID_HANDLE_VALUE is stored as 0. AL = opened (EAX upper bits = the
// CreateFileA result). Callers 0x00447590, 0x004e84e0, 0x004eb140, 0x004f5010, 0x00500ff0, 0x00506c90, 0x0050b8a0.
uint32_t File_OpenForRead(uint8_t* file, const char* name)
{
    char buf[0x100];
    char* const path = reinterpret_cast<char*>(file + 4);
    const auto open = Imp<decltype(&CreateFileA)>(0x0056b0c0);
    uint32_t h = 0;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (attempt == 0)
        {
            MakePath(u(buf), 0, Root1(), u(name), 0);
            Path_CopyBackslash(buf, path);
        }
        else if (attempt == 1)
        {
            MakePath(u(buf), 0, Root2(), u(name), 0);
            Path_CopyBackslash(buf, path);
        }
        else
        {
            Path_CopyBackslash(name, path);
        }
        h = u(open(path, 0x80000000u, 1, nullptr, 3, 0, nullptr));
        Field<uint32_t>(file, 0) = h;
        if (h == 0xffffffffu)
            Field<uint32_t>(file, 0) = 0;
        if (Field<uint32_t>(file, 0) != 0)
            return Lo8(h, 1);
    }
    return Lo8(h, 0);
}

static uint32_t File_OpenForRead_Abi(uint32_t esi, uint32_t a0)
{
    return File_OpenForRead(B(esi), reinterpret_cast<const char*>(a0));
}
ABI_ENTRY_004f1c90(File_OpenForRead_Entry, File_OpenForRead_Abi)
HOOK(0x004f1c90, "File_OpenForRead", File_OpenForRead_Entry, nullptr);

// 0x004f1da0 (esi = file {+0 handle, +4 path}; stack: name, a1; caller pops): File_OpenForAppend (0x004f1a60, eax =
// root 2 [0x0063a208] + name built by _makepath in a 0x100-byte local). a1 is pushed before that call but
// File_OpenForAppend reads no stack argument. Returns its EAX (AL = opened). ABI lists ebp and 5 stack arguments: only
// the first is read. Caller 0x004e0c60 (debug log).
uint32_t fn_004f1da0(uint8_t* file, const char* name)
{
    char buf[0x100];
    MakePath(u(buf), 0, Root2(), u(name), 0);
    return orig::fn_004f1a60(u(buf), 0, u(file));
}

static uint32_t fn_004f1da0_Abi(uint32_t esi, uint32_t /*ebp*/, uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/,
                                uint32_t /*a3*/, uint32_t /*a4*/)
{
    return fn_004f1da0(B(esi), reinterpret_cast<const char*>(a0));
}
ABI_ENTRY_004f1da0(fn_004f1da0_Entry, fn_004f1da0_Abi)
HOOK(0x004f1da0, "fn_004f1da0", fn_004f1da0_Entry, nullptr);

// 0x004f1e00 (stack: name; caller pops): Path_CreateDirectories (0x004f1b40) on root 2 [0x0063a208] + name
// (_makepath into a 0x100-byte local). Returns its result. ABI lists esi/ebp: not read. Caller 0x0048b260 (save).
uint32_t fn_004f1e00(const char* name)
{
    char buf[0x100];
    MakePath(u(buf), 0, Root2(), u(name), 0);
    return Path_CreateDirectories(buf);
}

static uint32_t fn_004f1e00_Abi(uint32_t /*esi*/, uint32_t /*ebp*/, uint32_t a0)
{
    return fn_004f1e00(reinterpret_cast<const char*>(a0));
}
ABI_ENTRY_004f1e00(fn_004f1e00_Entry, fn_004f1e00_Abi)
HOOK(0x004f1e00, "fn_004f1e00", fn_004f1e00_Entry, nullptr);

// 0x004f1e60 (esi = pattern): AL = 1 when FindFirstFileA finds a match (0x004f1f40 with a 0x248-byte local find state,
// no name output; then FindClose, IAT 0x0056b0dc), else 0x004f1f40's EAX (AL = 0). ABI lists ebx/edi/ebp: not read.
// Callers 0x0048b210, 0x004f59f0.
uint32_t File_Exists(const char* pattern)
{
    uint8_t state[0x248];
    const uint32_t r = File_FindFirstIn(state, pattern, 0);
    if (static_cast<uint8_t>(r) == 0)
        return r;
    const BOOL c = Imp<decltype(&FindClose)>(0x0056b0dc)(Field<HANDLE>(state, 0));
    return Lo8(static_cast<uint32_t>(c), 1);
}

static uint32_t File_Exists_Abi(uint32_t /*ebx*/, uint32_t esi, uint32_t /*edi*/, uint32_t /*ebp*/)
{
    return File_Exists(reinterpret_cast<const char*>(esi));
}
ABI_ENTRY_004f1e60(File_Exists_Entry, File_Exists_Abi)
HOOK(0x004f1e60, "File_Exists", File_Exists_Entry, nullptr);

// 0x004f1f40 (ecx = find state {+0 handle, +4 WIN32_FIND_DATAA, +0x144 directory}, esi = pattern; stack: out name
// pointer or null; caller pops): stores the pattern's drive + directory in state+0x144 (_splitpath, _makepath); when
// the pattern ends in '\\' or '/' it becomes "<pattern>*.*" (sprintf 0x0053814f, format 0x0058ad70 "%s*.*"); the
// result is copied with '/' -> '\\' (0x004f19d0) and passed to FindFirstFileA (IAT 0x0056b0d4, data state+4); +0 =
// the handle (INVALID_HANDLE_VALUE kept). Found: *out = state+0x30 (cFileName) when out is given, AL = 1; else AL = 0.
// Frame 0x308 bytes as in the original (drive +0, copy +4, dir +0x104, sprintf buffer +0x204). ABI lists 6 stack
// arguments: only the first is read.
uint32_t File_FindFirstIn(uint8_t* state, const char* pattern, uint32_t outName)
{
    uint8_t f[0x308];
    SplitPath(u(pattern), u(f), u(f + 0x104), 0, 0);
    MakePath(u(state + 0x144), u(f), u(f + 0x104), 0, 0);
    const char last = pattern[std::strlen(pattern) - 1];
    const char* src = pattern;
    if (last == '\\' || last == '/')
    {
        GameFunction<int(__cdecl*)(uint32_t, uint32_t, const char*)>(0x0053814f)(u(f + 0x204), 0x0058ad70, pattern);
        src = reinterpret_cast<const char*>(f + 0x204);
    }
    Path_CopyBackslash(src, reinterpret_cast<char*>(f + 4));
    const HANDLE h = Imp<decltype(&FindFirstFileA)>(0x0056b0d4)(reinterpret_cast<const char*>(f + 4),
                                                                 reinterpret_cast<WIN32_FIND_DATAA*>(state + 4));
    Field<HANDLE>(state, 0) = h;
    if (h == INVALID_HANDLE_VALUE)
        return Lo8(u(h), 0);
    if (outName != 0)
        *reinterpret_cast<uint32_t*>(outName) = u(state + 0x30);
    return Lo8(u(h), 1);
}

static uint32_t File_FindFirstIn_Abi(uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/,
                                     uint32_t /*a3*/, uint32_t /*a4*/, uint32_t /*a5*/)
{
    return File_FindFirstIn(B(ecx), reinterpret_cast<const char*>(esi), a0);
}
ABI_ENTRY_004f1f40(File_FindFirstIn_Entry, File_FindFirstIn_Abi)
HOOK(0x004f1f40, "File_FindFirstIn", File_FindFirstIn_Entry, nullptr);

// 0x004f2030 (ebx = find state, edi = out name pointer or null; stack: pattern; caller pops): File_FindFirstIn
// (0x004f1f40) on root 1 [0x0063a204] + pattern, then root 2 [0x0063a208] + pattern (_makepath into a 0x100-byte
// local). Original bug: the third attempt was evidently meant to use the pattern as given, but it passes the same local
// buffer again (lea esi, [esp + 0xc] at 0x004f20a4 = the buffer still holding root 2 + pattern), so it repeats the
// second search. Returns the last search's EAX (AL = found). symbols/abi.csv lists "inputs [] stack 6"; the code reads
// EBX (0x004f206c mov ecx, ebx), EDI (pushed as 0x004f1f40's argument) and one stack argument, hence the hand-written
// entry stub. Callers 0x00415a00, 0x0044aa80, 0x0048b370, 0x00492d80.
uint32_t File_FindFirst(uint8_t* state, uint32_t outName, const char* pattern)
{
    char buf[0x100];
    MakePath(u(buf), 0, Root1(), u(pattern), 0);
    uint32_t r = File_FindFirstIn(state, buf, outName);
    if (static_cast<uint8_t>(r) != 0)
        return r;
    MakePath(u(buf), 0, Root2(), u(pattern), 0);
    r = File_FindFirstIn(state, buf, outName);
    if (static_cast<uint8_t>(r) != 0)
        return r;
    return File_FindFirstIn(state, buf, outName);
}

static uint32_t File_FindFirst_Abi(uint32_t ebx, uint32_t edi, uint32_t a0)
{
    return File_FindFirst(B(ebx), edi, reinterpret_cast<const char*>(a0));
}
L2_ENTRY_EBX_EDI_S1(File_FindFirst_Entry, File_FindFirst_Abi)
HOOK(0x004f2030, "File_FindFirst", File_FindFirst_Entry, nullptr);

// ================================================================================================================
// 0x004f5910 - 0x004f60b0: level stream (the *.big file streamer behind the level chunk loader) and byte swap
// ================================================================================================================

// Entry stub for 0x004f59f0, whose real inputs are EBX and EDI (symbols/abi.csv lists "inputs [ecx edi]"; the code
// reads EBX at 0x004f5a54 mov esi, ebx and 0x004f5a84 push ebx, never ECX). Plain ret.
// clang-format off
#define L2_ENTRY_EBX_EDI(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push edi __asm push ebx __asm call impl __asm add esp, 8 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret }
// clang-format on

uint32_t fn_004f5b00(char* fnameOut, const char* path);
uint32_t fn_004f5ba0(uint8_t* stream, const char* dir);
uint32_t fn_004f5dc0(uint32_t* files, uint8_t* stream);
uint32_t fn_004f5f10(uint8_t* stream, uint32_t files);

namespace
{
// Flag bit 6 of the file streamer's +0x10 (set on a read error / end; guess from its use after every read).
inline bool StreamFailed(uint8_t* fs)
{
    return ((Field<uint32_t>(fs, 0x10) >> 6) & 1) != 0;
}
// After a streamer read: when the failure flag is now set and the stream has an error callback (+0), call it with -1
// (cdecl).
inline void ReportFailure(uint8_t* stream)
{
    if (StreamFailed(Field<uint8_t*>(stream, 0x28)))
    {
        const uint32_t cb = Field<uint32_t>(stream, 0);
        if (cb != 0)
            reinterpret_cast<void(__cdecl*)(int32_t)>(cb)(-1);
    }
}
}  // namespace

// 0x004f5910 (esi = level stream): releases it: while it holds a file streamer (+0x28), decrements the open count
// +0x24 and, when that reaches 0, destroys the streamer (virtual slot 0 with 0) and clears +0x28; then decrements the
// shared counter [0x0063a0b4] and frees it (0x004e08d0, heap 0x0063a210) and clears the global when it reaches 0.
// Returns the free's EAX, or the counter pointer when it was not freed. Caller 0x0044ac10.
uint32_t fn_004f5910(uint8_t* stream)
{
    while (Field<uint32_t>(stream, 0x28) != 0)
    {
        if (--Field<int32_t>(stream, 0x24) == 0)
        {
            void* fs = Field<void*>(stream, 0x28);
            if (fs != nullptr)
            {
                VCall<uint32_t>(fs, 0, 0u);
                Field<uint32_t>(stream, 0x28) = 0;
            }
        }
    }
    int32_t* counter = GameGlobal<int32_t*>(0x0063a0b4);
    if (--*counter != 0)
        return u(counter);
    const uint32_t r = orig::fn_004e08d0(0x0063a210u, u(counter));
    GameGlobal<uint32_t>(0x0063a0b4) = 0;
    return r;
}

static uint32_t fn_004f5910_Abi(uint32_t esi)
{
    return fn_004f5910(B(esi));
}
ABI_ENTRY_004f5910(fn_004f5910_Entry, fn_004f5910_Abi)
HOOK(0x004f5910, "fn_004f5910", fn_004f5910_Entry, nullptr);

// 0x004f59a0 (eax = level stream; stack: a0, a1, unused; ret 0xc): reads from the stream's file streamer: 0 when there
// is none or it has failed (+0x10 bit 6); else r = streamer virtual +4 (a0, a1) (the read: (size, buffer) at the
// other call sites), then the error callback (+0, with -1) when the streamer failed, and returns r. Caller
// 0x004fab80.
uint32_t fn_004f59a0(uint8_t* stream, uint32_t a0, uint32_t a1)
{
    uint8_t* fs = Field<uint8_t*>(stream, 0x28);
    if (fs == nullptr || StreamFailed(fs))
        return 0;
    const uint32_t r = VCall<uint32_t>(fs, 1, a0, a1);
    ReportFailure(stream);
    return r;
}

static uint32_t fn_004f59a0_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t /*a2*/)
{
    return fn_004f59a0(B(eax), a0, a1);
}
ABI_ENTRY_004f59a0(fn_004f59a0_Entry, fn_004f59a0_Abi)
HOOK(0x004f59a0, "fn_004f59a0", fn_004f59a0_Entry, nullptr);

// 0x004f59f0 (ebx = level stream, edi = level directory): opens the level's data: first the single file
// "<dir><fname of dir>.big" (_splitpath fname, _makepath with ext 0x0058aa34 ".big"); when it exists (File_Exists
// 0x004f1e60) it is streamed alone (0x004f5dc0 with the list {path, 0}) and AL = 1 when that succeeded. Otherwise (or
// when that failed) 0x004f5ba0(stream, dir) collects the directory's *.big files and returns its result. Frame 0x20c
// bytes as in the original (list +0, path +8, fname +0x10c). Caller 0x0044a110 (through loader_01's CallEbxEdi).
uint32_t fn_004f59f0(uint8_t* stream, const char* dir)
{
    uint8_t f[0x20c];
    SplitPath(u(dir), 0, 0, u(f + 0x10c), 0);
    MakePath(u(f + 8), 0, u(dir), u(f + 0x10c), 0x0058aa34);
    if (static_cast<uint8_t>(File_Exists(reinterpret_cast<const char*>(f + 8))) != 0)
    {
        Field<uint32_t>(f, 0) = u(f + 8);
        Field<uint32_t>(f, 4) = 0;
        const uint32_t r = fn_004f5dc0(reinterpret_cast<uint32_t*>(f), stream);
        if (static_cast<uint8_t>(r) != 0)
            return Lo8(r, 1);
    }
    return fn_004f5ba0(stream, dir);
}

static uint32_t fn_004f59f0_Abi(uint32_t ebx, uint32_t edi)
{
    return fn_004f59f0(B(ebx), reinterpret_cast<const char*>(edi));
}
L2_ENTRY_EBX_EDI(fn_004f59f0_Entry, fn_004f59f0_Abi)
HOOK(0x004f59f0, "fn_004f59f0", fn_004f59f0_Entry, nullptr);

// 0x004f5b00 (ecx = fname out (0x100 bytes) or null for a local; stack: path; caller pops): the language tag of a
// file name: _splitpath(path) fname; when it is longer than 5 characters and has '@' 5 from its end, cuts the name
// there and returns the 4 characters after the '@' as a dword (e.g. "lev@engl"); else returns 'dflt' (0x64666c74,
// the level stream's default language code, see 0x0044ac10). ABI lists ebx/edi/ebp: not read. Callers 0x004f5ba0.
uint32_t fn_004f5b00(char* fnameOut, const char* path)
{
    char local[0x100];
    char* fname = fnameOut != nullptr ? fnameOut : local;
    SplitPath(u(path), 0, 0, u(fname), 0);
    const uint32_t len = static_cast<uint32_t>(std::strlen(fname));
    if (len > 5 && fname[len - 5] == '@')
    {
        fname[len - 5] = 0;
        return *reinterpret_cast<const uint32_t*>(fname + len - 4);
    }
    return 0x64666c74;
}

static uint32_t fn_004f5b00_Abi(uint32_t ecx, uint32_t /*ebx*/, uint32_t /*edi*/, uint32_t /*ebp*/, uint32_t a0)
{
    return fn_004f5b00(reinterpret_cast<char*>(ecx), reinterpret_cast<const char*>(a0));
}
ABI_ENTRY_004f5b00(fn_004f5b00_Entry, fn_004f5b00_Abi)
HOOK(0x004f5b00, "fn_004f5b00", fn_004f5b00_Entry, nullptr);

// 0x004f5ba0 (stack: level stream, level directory; ret 8; ECX is only clobbered by __chkstk): collects the
// directory's "*.big" files (string 0x005b2624; File_FindFirstIn + FindNextFileA): every non-directory entry whose
// extension is ".big" (_stricmp 0x005448ee with 0x0058aa34), up to 31, whose language tag (0x004f5b00) is 'dflt' or
// the stream's language (+0x14) is recorded (full path from the find state's directory + name, and its size). The
// list is sorted (qsort 0x00538f30, comparator 0x004f5ab0); a 'dflt' file directly followed by a localised file of
// the same base name is dropped in favour of it. The kept sizes are summed into stream+0x18 (zeroed first), the list
// is null-terminated and streamed with 0x004f5dc0, whose result is returned; when nothing is found, the find's EAX
// (AL = 0). Frame 0x27d4 bytes as in the original (name pointer +0, list +4, sizes +0x84, find state +0x104, ext
// +0x34c, pattern +0x44c, base names +0x550 / +0x650, path slots of 0x104 from +0x750).
uint32_t fn_004f5ba0(uint8_t* stream, const char* dir)
{
    uint8_t* f = new uint8_t[0x27d4];
    uint32_t* const list = reinterpret_cast<uint32_t*>(f + 4);
    uint32_t* const sizes = reinterpret_cast<uint32_t*>(f + 0x84);
    uint8_t* const state = f + 0x104;
    const auto stricmp_ = GameFunction<int(__cdecl*)(const void*, const void*)>(0x005448ee);
    Field<uint32_t>(f, 0) = 0;
    Field<uint32_t>(stream, 0x18) = 0;
    MakePath(u(f + 0x44c), 0, u(dir), 0x005b2624, 0);
    uint32_t r = File_FindFirstIn(state, reinterpret_cast<const char*>(f + 0x44c), u(f));
    if (static_cast<uint8_t>(r) == 0)
    {
        delete[] f;
        return r;
    }
    std::memset(list, 0, 0x20 * 4);
    uint32_t count = 0;
    uint8_t* slot = f + 0x750;
    uint32_t name = Field<uint32_t>(f, 0);
    for (;;)
    {
        if (((Field<uint32_t>(state, 4) >> 4) & 1) == 0)
        {
            SplitPath(name, 0, 0, 0, u(f + 0x34c));
            if (stricmp_(f + 0x34c, reinterpret_cast<const void*>(0x0058aa34)) == 0 && count < 0x1f)
            {
                MakePath(u(slot), 0, u(state + 0x144), u(state + 0x30), 0);
                const uint32_t lang = fn_004f5b00(nullptr, reinterpret_cast<const char*>(slot));
                if (lang == 0x64666c74 || lang == Field<uint32_t>(stream, 0x14))
                {
                    list[count] = u(slot);
                    sizes[count] = Field<uint32_t>(state, 0x24);
                    ++count;
                    slot += 0x104;
                }
            }
        }
        if (Imp<decltype(&FindNextFileA)>(0x0056b0d8)(Field<HANDLE>(state, 0),
                                                     reinterpret_cast<WIN32_FIND_DATAA*>(state + 4)) == 0)
            break;
        name = u(state + 0x30);
    }
    Imp<decltype(&FindClose)>(0x0056b0dc)(Field<HANDLE>(state, 0));
    GameFunction<void(__cdecl*)(void*, uint32_t, uint32_t, uint32_t)>(0x00538f30)(list, count, 4, 0x004f5ab0);
    uint32_t out = 0;
    for (uint32_t i = 0; i < count;)
    {
        if (fn_004f5b00(reinterpret_cast<char*>(f + 0x550), reinterpret_cast<const char*>(list[i])) == 0x64666c74 &&
            i != count - 1 &&
            fn_004f5b00(reinterpret_cast<char*>(f + 0x650), reinterpret_cast<const char*>(list[i + 1])) !=
                0x64666c74 &&
            stricmp_(f + 0x550, f + 0x650) == 0)
            ++i;
        const uint32_t size = sizes[i];
        const uint32_t path = list[i];
        ++i;
        list[out] = path;
        ++out;
        Field<uint32_t>(stream, 0x18) += size;
    }
    list[out] = 0;
    r = fn_004f5dc0(list, stream);
    delete[] f;
    return r;
}

static uint32_t fn_004f5ba0_Abi(uint32_t /*ecx*/, uint32_t a0, uint32_t a1)
{
    return fn_004f5ba0(B(a0), reinterpret_cast<const char*>(a1));
}
ABI_ENTRY_004f5ba0(fn_004f5ba0_Entry, fn_004f5ba0_Abi)
HOOK(0x004f5ba0, "fn_004f5ba0", fn_004f5ba0_Entry, nullptr);

// 0x004f5dc0 (eax = null-terminated list of file paths, esi = level stream): streams the files: 0x004f5f10 (open /
// share the file streamer with the list), the progress callback +0x10 (cdecl float: [0x0056c05c] / total size +0x18
// as unsigned; that constant is 0.0 in the binary), then reads 8-byte chunk headers {id, size} (streamer virtual +4
// (8, &header)) and hands each to the chunk handler +8 (cdecl (id, size)); a chunk the handler refuses (returns 0) is
// skipped with virtual +4 (size, null). After every read the error callback runs when the streamer failed; the loop
// ends at the first short read, a failure or a missing streamer. Then the open count +0x24 is decremented (the
// streamer is destroyed, virtual slot 0 with 0, when it reaches 0). Returns 1 when the streamer had not failed, else
// 0. Original bug: when no streamer exists (allocation failure in 0x004f5f10) the final check reads [null+0x10].
// Callers 0x004f59f0, 0x004f5ba0.
uint32_t fn_004f5dc0(uint32_t* files, uint8_t* stream)
{
    uint32_t header[2] = {0, 0};
    fn_004f5f10(stream, u(files));
    const uint32_t progress = Field<uint32_t>(stream, 0x10);
    if (progress != 0)
    {
        const int32_t total = Field<int32_t>(stream, 0x18);
        double t = static_cast<double>(total);
        if (total < 0)
            t += static_cast<double>(GameGlobal<float>(0x005bcb28));
        const float value = static_cast<float>(static_cast<double>(GameGlobal<float>(0x0056c05c)) / t);
        reinterpret_cast<void(__cdecl*)(float)>(progress)(value);
    }
    uint8_t* fs = Field<uint8_t*>(stream, 0x28);
    if (fs != nullptr && !StreamFailed(fs))
    {
        uint32_t n = VCall<uint32_t>(fs, 1, 8u, u(header));
        ReportFailure(stream);
        while (n == 8)
        {
            if (reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t)>(Field<uint32_t>(stream, 8))(header[0],
                                                                                                      header[1]) == 0)
            {
                fs = Field<uint8_t*>(stream, 0x28);
                if (fs == nullptr)
                    break;
                if (!StreamFailed(fs))
                {
                    VCall<uint32_t>(fs, 1, header[1], 0u);
                    ReportFailure(stream);
                }
            }
            fs = Field<uint8_t*>(stream, 0x28);
            if (fs == nullptr || StreamFailed(fs))
                break;
            n = VCall<uint32_t>(fs, 1, 8u, u(header));
            ReportFailure(stream);
        }
    }
    uint8_t* last = Field<uint8_t*>(stream, 0x28);
    const bool failed = ((*reinterpret_cast<volatile uint32_t*>(u(last) + 0x10) >> 6) & 1) != 0;
    if (--Field<int32_t>(stream, 0x24) == 0 && last != nullptr)
    {
        VCall<uint32_t>(last, 0, 0u);
        Field<uint32_t>(stream, 0x28) = 0;
    }
    return failed ? 0u : 1u;
}

static uint32_t fn_004f5dc0_Abi(uint32_t eax, uint32_t esi)
{
    return fn_004f5dc0(reinterpret_cast<uint32_t*>(eax), B(esi));
}
ABI_ENTRY_004f5dc0(fn_004f5dc0_Entry, fn_004f5dc0_Abi)
HOOK(0x004f5dc0, "fn_004f5dc0", fn_004f5dc0_Entry, nullptr);

// 0x004f5f10 (ecx = level stream; stack: file list; ret 4): increments the open count +0x24; on the first open
// creates the file streamer: 0x55c bytes from 0x004e07f0 (heap 0x0063a210, tag string 0x0058b5e8), constructed by
// 0x004e5f30 (buffer size 0x400000, no buffer, block size 0x40000) when the allocation succeeded; +0x28 = it (null
// on failure, then the next accesses fault as in the original); under its lock (+0x520, Enter/LeaveCriticalSection)
// sets the completion callback +0xe4 = 0x004f5960 and +0xe8 = the stream; then under the lock again +0x510 = the
// file list, +0x514 = 0 and SetEvent(+0x538) (wakes the streamer thread). Returns the old count when it was nonzero
// (the list is then ignored). The original's SEH frame (unwinding the allocation if the constructor throws) is not
// reproduced. Caller 0x004f5dc0.
uint32_t fn_004f5f10(uint8_t* stream, uint32_t files)
{
    const uint32_t old = Field<uint32_t>(stream, 0x24);
    Field<uint32_t>(stream, 0x24) = old + 1;
    if (old != 0)
        return old;
    const uint32_t mem = orig::fn_004e07f0(0x0063a210u, 0x10, 0x55c, 2, 0x0058b5e8);
    uint32_t fs = 0;
    if (mem != 0)
        fs = orig::fn_004e5f30(mem, 0x400000, 0, 0x40000);
    const auto enter = Imp<decltype(&EnterCriticalSection)>(0x0056b0b0);
    const auto leave = Imp<decltype(&LeaveCriticalSection)>(0x0056b0b4);
    auto* lock = reinterpret_cast<CRITICAL_SECTION*>(fs + 0x520);
    Field<uint32_t>(stream, 0x28) = fs;
    enter(lock);
    Field<uint32_t>(B(fs), 0xe4) = 0x004f5960;
    Field<uint32_t>(B(fs), 0xe8) = u(stream);
    leave(lock);
    uint8_t* s = Field<uint8_t*>(stream, 0x28);
    lock = reinterpret_cast<CRITICAL_SECTION*>(s + 0x520);
    enter(lock);
    Field<uint32_t>(s, 0x510) = files;
    Field<uint32_t>(s, 0x514) = 0;
    Imp<decltype(&SetEvent)>(0x0056b0bc)(Field<HANDLE>(s, 0x538));
    leave(lock);
    return 0;
}

static uint32_t fn_004f5f10_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f5f10(B(ecx), a0);
}
ABI_ENTRY_004f5f10(fn_004f5f10_Entry, fn_004f5f10_Abi)
HOOK(0x004f5f10, "fn_004f5f10", fn_004f5f10_Entry, nullptr);

// 0x004f60b0 (ecx = dword pointer): byte-swaps the dword in place (endian conversion). Returns its original top byte
// (the last byte OR-ed in). ECX is restored (push ecx / pop ecx). ABI lists 3 stack arguments: none is read.
// Callers: the chunk loaders (0x00447660, 0x00447920, 0x004e1320, 0x004e1ad0, 0x004e8620, 0x004eae90, ...). Not
// executed in recorded runs (the PC data is little-endian).
uint32_t Endian_Swap32(uint32_t* p)
{
    const uint32_t v = *p;
    *p = (v << 24) | ((v & 0xff00u) << 8) | ((v >> 8) & 0xff00u) | (v >> 24);
    return v >> 24;
}

static uint32_t Endian_Swap32_Abi(uint32_t ecx, uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/)
{
    return Endian_Swap32(reinterpret_cast<uint32_t*>(ecx));
}
ABI_ENTRY_004f60b0(Endian_Swap32_Entry, Endian_Swap32_Abi)
HOOK(0x004f60b0, "Endian_Swap32", Endian_Swap32_Entry, nullptr);

// ================================================================================================================
// 0x004ef620 / 0x004f0e60: relocation and byte-swapping of loaded chunk tables
// ================================================================================================================
//
// Relocation context (as used here and by Reloc_FixupPointer 0x004e2040): +0 loaded block (its +8 is the sorted
// address-range table), +4 relocate-only flag, +8 byte-swap flag.

namespace
{
// The address-range lookup written inline in 0x004ef620 and 0x004f0e60: binary search (0x004e0f40, searcher
// [table+0x24], count +0x1c, key pointer +0x18 = &addr) of the range table [[ctx]+8]; returns
// record.new - record.old + addr with the record at entry + word[entry+2] ({+0 old base, +4 new base}). The original
// reads through a null entry when nothing is found (it faults); so does this.
uint32_t RelocLocate(uint8_t* ctx, uint32_t addr)
{
    uint32_t key = addr;
    uint8_t* t = Field<uint8_t*>(Field<uint8_t*>(ctx, 0), 8);
    uint8_t* rec = nullptr;
    const uint32_t count = Field<uint32_t>(t, 0x1c);
    if (count != 0 && Field<uint32_t>(t, 0x20) != 0)
    {
        uint8_t* s = Field<uint8_t*>(t, 0x24);
        Field<uint32_t>(s, 0) = count;
        Field<uint32_t*>(s, 0x18) = &key;
        orig::fn_004e0f40(u(s));
        if (Field<uint32_t>(s, 0xc) == 0)
            rec = Field<uint8_t**>(s, 0x10)[Field<uint32_t>(s, 8)];
    }
    volatile const uint16_t* w = reinterpret_cast<volatile const uint16_t*>(u(rec) + 2);
    const uint8_t* q = rec + *w;
    return Field<uint32_t>(q, 4) - Field<uint32_t>(q, 0) + addr;
}
// Swaps the two bytes of the word at p (inline in 0x004f0e60).
inline void Swap16(uint32_t p)
{
    uint8_t* b = B(p);
    const uint8_t lo = b[1];
    const uint8_t hi = b[0];
    b[0] = lo;
    b[1] = hi;
}
// Item i of a chunk table t through the entry array at [t+0x24]+entriesOff (count +0x1c; the table must have more
// than `minParts` parts at +0x20); null when out of range.
uint8_t* TableEntry(uint8_t* t, uint32_t i, uint32_t entriesOff, uint32_t minParts)
{
    if (i >= Field<uint32_t>(t, 0x1c) || Field<uint32_t>(t, 0x20) <= minParts)
        return nullptr;
    return Field<uint8_t**>(Field<uint8_t*>(t, 0x24), entriesOff)[i];
}
}  // namespace

uint32_t Endian_Swap32(uint32_t* p);
uint32_t fn_004e8620(uint8_t* ctx, uint8_t* m);

// 0x004f0e60 (edi = relocation context; stack: table or null, header callback, item callback; caller pops): fixes up a
// loaded chunk table: Reloc_FixupPointer (0x004e2040) on +0 and +4, headerCallback(table+0xc, ctx) (cdecl), sets the
// row fixup hook [0x005caa5c] = 0x004f0c40 when byte +8 has bit 1 else 0x004f0c00 and runs
// Reloc_FixupIndexedTable([+4], 0x005caa58, ctx); then itemCallback(entry + word[entry+2], ctx) for every entry of the
// primary index ([+4]: [[+0x24]+0x10]). When byte +8 bit 0 (secondary index, [[+0x24]+0x2c], needs > 1 parts): for
// each entry p = entry + word[entry+4], unless relocate-only, the words at p+2 and p are located (address-range
// lookup) and byte-swapped when the context swaps. Finally, unless relocate-only, the dword +8 (byte-swapped) and the
// word +0x10. Returns EAX as left (the last located address, or the relocate-only flag). Callers 0x00447920,
// 0x004e8620, 0x004eae90, 0x004ef620.
uint32_t fn_004f0e60(uint8_t* ctx, uint8_t* tbl, uint32_t headerCb, uint32_t itemCb)
{
    if (tbl == nullptr)
        return 0;
    using Cb = void(__cdecl*)(uint32_t, uint32_t);
    orig::fn_004e2040(u(tbl), u(ctx));
    orig::fn_004e2040(u(tbl + 4), u(ctx));
    reinterpret_cast<Cb>(headerCb)(u(tbl + 0xc), u(ctx));
    const uint8_t flags = Field<uint8_t>(tbl, 8);
    GameGlobal<uint32_t>(0x005caa5c) = 0x004f0c40;
    if ((flags & 2) == 0)
        GameGlobal<uint32_t>(0x005caa5c) = 0x004f0c00;
    orig::fn_004e1320(Field<uint32_t>(tbl, 4), 0x005caa58, u(ctx));
    for (uint32_t i = 0;; ++i)
    {
        uint8_t* e = TableEntry(Field<uint8_t*>(tbl, 4), i, 0x10, 0);
        if (e == nullptr)
            break;
        reinterpret_cast<Cb>(itemCb)(u(e) + Field<uint16_t>(e, 2), u(ctx));
    }
    uint32_t eax = 0;
    if ((Field<uint8_t>(tbl, 8) & 1) != 0)
    {
        for (uint32_t i = 0;; ++i)
        {
            uint8_t* e = TableEntry(Field<uint8_t*>(tbl, 4), i, 0x2c, 1);
            if (e == nullptr)
                break;
            const uint32_t p = u(e) + Field<uint16_t>(e, 4);
            eax = Field<uint32_t>(ctx, 4);
            if (eax != 0)
                continue;
            eax = RelocLocate(ctx, p + 2);
            if (Field<uint32_t>(ctx, 8) != 0)
                Swap16(eax);
            if (Field<uint32_t>(ctx, 4) != 0)
            {
                eax = Field<uint32_t>(ctx, 4);
                continue;
            }
            eax = RelocLocate(ctx, p);
            if (Field<uint32_t>(ctx, 8) != 0)
                Swap16(eax);
        }
    }
    eax = Field<uint32_t>(ctx, 4);
    if (eax == 0)
    {
        eax = RelocLocate(ctx, u(tbl + 8));
        if (Field<uint32_t>(ctx, 8) != 0)
        {
            uint32_t* d = reinterpret_cast<uint32_t*>(eax);
            const uint32_t v = *d;
            *d = (v << 24) | ((v & 0xff00u) << 8) | ((v >> 8) & 0xff00u) | (v >> 24);
        }
    }
    eax = Field<uint32_t>(ctx, 4);
    if (eax == 0)
    {
        eax = RelocLocate(ctx, u(tbl + 0x10));
        if (Field<uint32_t>(ctx, 8) != 0)
            Swap16(eax);
    }
    return eax;
}

static uint32_t fn_004f0e60_Abi(uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004f0e60(B(edi), B(a0), a1, a2);
}
ABI_ENTRY_004f0e60(fn_004f0e60_Entry, fn_004f0e60_Abi)
HOOK(0x004f0e60, "fn_004f0e60", fn_004f0e60_Entry, nullptr);

// 0x004ef620 (eax = relocation context; stack: object; caller pops; symbols/abi.csv lists 2 stack arguments, only the
// first is read): fixes up a loaded object: Reloc_FixupPointer on +0xc, +0x28, +0x2c; 0x004f0e60 on the table +0x28
// (callbacks 0x004f0dd0 / 0x004f0e50), then for every item of that table's primary index ([[+0x28]+4]) 0x004f0e60 on
// the item and fn_004ef270 (eax = ctx, ecx = record) on every record of the item's table [item+4]; Reloc_SwapField
// (0x004e2160) on the words +4 and +6; unless relocate-only, the dwords +0x14, +0x18, +0x1c, +0x20, +0x30 are located
// and byte-swapped (0x004f60b0) when the context swaps; finally Reloc_FixupTypedBlock (0x004eeda0, ecx = +0x24),
// whose EAX is returned. Caller 0x004e84e0.
uint32_t fn_004ef620(uint8_t* ctx, uint8_t* obj)
{
    orig::fn_004e2040(u(obj + 0xc), u(ctx));
    orig::fn_004e2040(u(obj + 0x28), u(ctx));
    orig::fn_004e2040(u(obj + 0x2c), u(ctx));
    fn_004f0e60(ctx, Field<uint8_t*>(obj, 0x28), 0x004f0dd0, 0x004f0e50);
    for (uint32_t i = 0;; ++i)
    {
        uint8_t* e = TableEntry(Field<uint8_t*>(Field<uint8_t*>(obj, 0x28), 4), i, 0x10, 0);
        if (e == nullptr)
            break;
        uint8_t* item = Field<uint8_t*>(e, Field<uint16_t>(e, 2));
        fn_004f0e60(ctx, item, 0x004f0dd0, 0x004f0e50);
        for (uint32_t j = 0;; ++j)
        {
            uint8_t* c = TableEntry(Field<uint8_t*>(item, 4), j, 0x10, 0);
            if (c == nullptr)
                break;
            orig::fn_004ef270(u(ctx), Field<uint32_t>(c, Field<uint16_t>(c, 2)));
        }
    }
    orig::fn_004e2160(u(ctx), u(obj + 4), 2, 0, 0);
    orig::fn_004e2160(u(ctx), u(obj + 6), 2, 0, 0);
    static const uint32_t offsets[] = {0x14, 0x18, 0x1c, 0x20, 0x30};
    for (const uint32_t off : offsets)
    {
        if (Field<uint32_t>(ctx, 4) != 0)
            continue;
        const uint32_t a = RelocLocate(ctx, u(obj + off));
        if (Field<uint32_t>(ctx, 8) != 0)
            Endian_Swap32(reinterpret_cast<uint32_t*>(a));
    }
    return orig::fn_004eeda0(u(ctx), u(obj + 0x24));
}

static uint32_t fn_004ef620_Abi(uint32_t eax, uint32_t a0, uint32_t /*a1*/)
{
    return fn_004ef620(B(eax), B(a0));
}
ABI_ENTRY_004ef620(fn_004ef620_Entry, fn_004ef620_Abi)
HOOK(0x004ef620, "fn_004ef620", fn_004ef620_Entry, nullptr);


// 0x004eae90 (eax = relocation context; stack: object; caller pops; symbols/abi.csv lists 2 stack arguments, only the
// first is read): fixes up a loaded object: Reloc_FixupPointer on +0xc, +0x18, +0x1c, +0x20, +0x14; 0x004f0e60 on
// the tables +0x18 (callbacks 0x004f0dd0 / 0x004f0dd0) and +0x1c (0x004f0dd0 / 0x004f0e50); for every item of +0x1c's
// primary index 0x004f0e60 on the item and 0x004e8620 (eax = ctx; stack: record) on every record of the item's table
// [item+4]; Reloc_SwapField on the words +4 and +6; unless relocate-only, the dwords +0x24, +0x28, +0x2c, +0x30 are
// located and byte-swapped (0x004f60b0) when the context swaps; finally, when +0x14 is set,
// Reloc_FixupElementList (0x004eaaa0, eax = ctx; stack: [+0x14]), whose EAX is returned (else 0). The original keeps
// its outer loop counter in the caller's argument slot (the stack word holding `object`); not reproduced. Callers
// 0x0045af30, 0x004eb140.
uint32_t fn_004eae90(uint8_t* ctx, uint8_t* obj)
{
    orig::fn_004e2040(u(obj + 0xc), u(ctx));
    orig::fn_004e2040(u(obj + 0x18), u(ctx));
    orig::fn_004e2040(u(obj + 0x1c), u(ctx));
    orig::fn_004e2040(u(obj + 0x20), u(ctx));
    orig::fn_004e2040(u(obj + 0x14), u(ctx));
    fn_004f0e60(ctx, Field<uint8_t*>(obj, 0x18), 0x004f0dd0, 0x004f0dd0);
    fn_004f0e60(ctx, Field<uint8_t*>(obj, 0x1c), 0x004f0dd0, 0x004f0e50);
    for (uint32_t i = 0;; ++i)
    {
        uint8_t* e = TableEntry(Field<uint8_t*>(Field<uint8_t*>(obj, 0x1c), 4), i, 0x10, 0);
        if (e == nullptr)
            break;
        uint8_t* item = Field<uint8_t*>(e, Field<uint16_t>(e, 2));
        fn_004f0e60(ctx, item, 0x004f0dd0, 0x004f0e50);
        for (uint32_t j = 0;; ++j)
        {
            uint8_t* c = TableEntry(Field<uint8_t*>(item, 4), j, 0x10, 0);
            if (c == nullptr)
                break;
            fn_004e8620(ctx, Field<uint8_t*>(c, Field<uint16_t>(c, 2)));
        }
    }
    orig::fn_004e2160(u(ctx), u(obj + 4), 2, 0, 0);
    orig::fn_004e2160(u(ctx), u(obj + 6), 2, 0, 0);
    static const uint32_t offsets[] = {0x24, 0x28, 0x2c, 0x30};
    for (const uint32_t off : offsets)
    {
        if (Field<uint32_t>(ctx, 4) != 0)
            continue;
        const uint32_t a = RelocLocate(ctx, u(obj + off));
        if (Field<uint32_t>(ctx, 8) != 0)
            Endian_Swap32(reinterpret_cast<uint32_t*>(a));
    }
    const uint32_t list = Field<uint32_t>(obj, 0x14);
    if (list == 0)
        return 0;
    return orig::fn_004eaaa0(u(ctx), list);
}

static uint32_t fn_004eae90_Abi(uint32_t eax, uint32_t a0, uint32_t /*a1*/)
{
    return fn_004eae90(B(eax), B(a0));
}
ABI_ENTRY_004eae90(fn_004eae90_Entry, fn_004eae90_Abi)
HOOK(0x004eae90, "fn_004eae90", fn_004eae90_Entry, nullptr);

// ================================================================================================================
// 0x004ee010: name dictionary / hierarchy numbering over loaded objects
// ================================================================================================================

namespace
{
// The binary search written inline in 0x004ee010 over a chunk table's searcher s ({+0 count, +4 compare callback
// (cdecl, s), +8 index, +0xc result, +0x10 entries, +0x18 key}): returns the entry found (null when the compare never
// hit 0 or the entry is null).
uint8_t* InlineSearch(uint8_t* s, uint32_t count, uint32_t key)
{
    Field<uint32_t>(s, 0) = count;
    uint32_t hi = count;
    uint32_t mid = hi >> 1;
    uint32_t lo = 0;
    Field<uint32_t>(s, 0x18) = key;
    Field<uint32_t>(s, 8) = hi;
    if (hi != mid)
    {
        for (;;)
        {
            Field<uint32_t>(s, 8) = mid;
            const int32_t r = reinterpret_cast<int32_t(__cdecl*)(uint8_t*)>(Field<uint32_t>(s, 4))(s);
            Field<int32_t>(s, 0xc) = r;
            if (r == 0)
                break;
            if (r < 0)
                hi = Field<uint32_t>(s, 8);
            else
                lo = Field<uint32_t>(s, 8);
            mid = ((hi - lo) >> 1) + lo;
            if (Field<uint32_t>(s, 8) == mid)
                break;
        }
    }
    if (Field<uint32_t>(s, 0xc) != 0)
        return nullptr;
    return Field<uint8_t**>(s, 0x10)[Field<uint32_t>(s, 8)];
}
// InlineSearch on the primary searcher of chunk table t (needs count +0x1c and parts +0x20 nonzero); null otherwise.
uint8_t* TableFind(uint8_t* t, uint32_t key)
{
    const uint32_t count = Field<uint32_t>(t, 0x1c);
    if (count == 0 || Field<uint32_t>(t, 0x20) == 0)
        return nullptr;
    return InlineSearch(Field<uint8_t*>(t, 0x24), count, key);
}
// Value slot of a table entry: entry + word[entry+2].
inline uint32_t* EntrySlot(uint8_t* e)
{
    return reinterpret_cast<uint32_t*>(e + Field<uint16_t>(e, 2));
}
// A new chunk table record on heap h (as built inline in 0x004ee010 and 0x00447bb0): 0x14 bytes from 0x004e1e90,
// {+0 heap, +8 type 3, word +0x10 = 1, +4 = 0x004e0ff0 table (0x100 entries, row hook 0x004f0c40)}.
uint8_t* NewTable(uint32_t h)
{
    uint8_t* t = B(orig::fn_004e1e90(h, 0x14, 1, 2));
    Field<uint32_t>(t, 0) = h;
    Field<uint32_t>(t, 8) = 3;
    Field<uint16_t>(t, 0x10) = 1;
    GameGlobal<uint32_t>(0x005caa5c) = 0x004f0c40;
    Field<uint32_t>(t, 4) = orig::fn_004e0ff0(h, 0x100, 0, 1, 0x400, 2, 0x005caa58, h, 0x14, 1, 2);
    return t;
}
}  // namespace

// 0x004ee010 (stack: include dictionary or null, exclude dictionary or null, null-terminated prefix list or null,
// unused, object array, object count, unused; caller pops): numbers the named parts of a set of loaded objects
// (each object's +0x28 chunk table): builds a temporary heap (0x30-byte header from the allocator hook 0x005cacc0) and
// a dictionary (NameDictionary_Add 0x004f0cd0) of every item name of every object; gives each name a node
// {+0 base, +4 next number, +8 sub-dictionary, +0xc child count, +0x10 children} (0x004e1e90); links children through
// each item's parent index (word +0xc, found in the object's secondary index); numbers every item's part names in its
// node's sub-dictionary (a part name counts unless the exclude dictionary has it or the include dictionary, when
// given, lacks it; names starting with one of the prefixes share the prefix's number); lays the nodes out
// (0x004edf80: base = max over children of base + count); writes base + number into word +0xa of every part record;
// frees the heap's blocks (0x004e1f70). Returns the highest number + 1. Caller 0x0045bf70. The original's frame slots
// are kept as named locals.
uint32_t fn_004ee010(uint8_t* include, uint8_t* exclude, const char* const* prefixes, uint8_t** objs, int32_t count)
{
    uint8_t* h = B(u(GameGlobal<void*(__cdecl*)(uint32_t, uint32_t)>(0x005cacc0)(0x30, 2)));
    std::memset(h, 0, 0x30);
    Field<uint32_t>(h, 0) = u(h + 0x10);
    Field<uint32_t>(h, 0x10) = u(h);
    Field<uint32_t>(h, 0xc) = 0;
    Field<uint32_t>(h, 4) = 0x30;
    Field<uint16_t>(h, 8) = 1;
    Field<uint16_t>(h, 0xa) = 2;
    Field<uint32_t>(h, 0x14) = 0x30;
    Field<uint32_t>(h, 0x18) = 1;
    Field<uint32_t>(h, 0x1c) = 2;
    Field<uint32_t>(h, 0x20) = u(h);
    uint8_t* const dict = NewTable(u(h));
    uint32_t names = 0;

    // Every item name of every object goes into the dictionary (value -1 marks it as seen).
    for (int32_t i = 0; i < count; ++i)
    {
        uint8_t* obj = objs[i];
        if (obj == nullptr)
            continue;
        for (uint32_t k = 0;; ++k)
        {
            uint8_t* e = TableEntry(Field<uint8_t*>(Field<uint8_t*>(obj, 0x28), 4), k, 0x10, 0);
            if (e == nullptr)
                break;
            uint8_t* r = B(orig::fn_004f0cd0(u(dict), u(e) + Field<uint16_t>(e, 2) + 4));
            if (r == nullptr)
                continue;
            uint32_t* slot = EntrySlot(r);
            if (*slot == 0)
            {
                *slot = 0xffffffffu;
                ++names;
            }
        }
    }

    // One node per name.
    for (uint32_t k = 0;; ++k)
    {
        uint8_t* e = TableEntry(Field<uint8_t*>(dict, 4), k, 0x10, 0);
        if (e == nullptr)
            break;
        uint8_t* node = B(orig::fn_004e1e90(u(h), names * 4 + 0x10, 1, 2));
        if (node == nullptr)
            continue;
        Field<uint32_t>(node, 8) = u(NewTable(u(h)));
        *EntrySlot(e) = u(node);
    }

    // Children and part numbers.
    for (uint32_t k = 0;; ++k)
    {
        uint8_t* e = TableEntry(Field<uint8_t*>(dict, 4), k, 0x10, 0);
        if (e == nullptr)
            break;
        uint8_t* p = e + Field<uint16_t>(e, 2);
        uint8_t* node = Field<uint8_t*>(p, 0);
        const uint32_t name = u(p + 4);
        if (node == nullptr || count <= 0)
            continue;
        for (int32_t j = 0; j < count; ++j)
        {
            uint8_t* obj = objs[j];
            uint8_t* rec = TableFind(Field<uint8_t*>(Field<uint8_t*>(obj, 0x28), 4), name);
            if (rec == nullptr)
                continue;
            uint8_t* item = Field<uint8_t*>(rec, Field<uint16_t>(rec, 2));
            if (Field<uint32_t>(item, 0xc) != 0)
            {
                uint32_t parentKey = Field<uint16_t>(item, 0xc);
                uint8_t* t = Field<uint8_t*>(Field<uint8_t*>(obj, 0x28), 4);
                const uint32_t n = Field<uint32_t>(t, 0x1c);
                if (n != 0 && Field<uint32_t>(t, 0x20) > 1)
                {
                    uint8_t* s = Field<uint8_t*>(t, 0x24) + 0x1c;
                    Field<uint32_t>(s, 0) = n;
                    Field<uint32_t*>(s, 0x18) = &parentKey;
                    orig::fn_004e0f40(u(s));
                    uint8_t* r2 = Field<uint32_t>(s, 0xc) == 0 ? Field<uint8_t**>(s, 0x10)[Field<uint32_t>(s, 8)]
                                                               : nullptr;
                    if (r2 != nullptr)
                    {
                        const uint32_t pname = u(r2) + Field<uint16_t>(r2, 2) + 4;
                        uint8_t* td = Field<uint8_t*>(dict, 4);
                        const uint32_t dn = Field<uint32_t>(td, 0x1c);
                        if (dn != 0 && Field<uint32_t>(td, 0x20) != 0)
                        {
                            uint8_t* sd = Field<uint8_t*>(td, 0x24);
                            Field<uint32_t>(sd, 0) = dn;
                            Field<uint32_t>(sd, 0x18) = pname;
                            orig::fn_004e0f40(u(sd));
                            uint8_t* r3 = Field<uint32_t>(sd, 0xc) == 0
                                              ? Field<uint8_t**>(sd, 0x10)[Field<uint32_t>(sd, 8)]
                                              : nullptr;
                            uint8_t* parent = r3 != nullptr ? Field<uint8_t*>(r3, Field<uint16_t>(r3, 2)) : nullptr;
                            if (parent != nullptr)
                            {
                                // The parent node becomes a child of this name's node (once).
                                const int32_t nc = Field<int32_t>(node, 0xc);
                                int32_t c = 0;
                                if (nc > 0)
                                {
                                    while (Field<uint8_t*>(node, 0x10 + c * 4) != parent)
                                    {
                                        ++c;
                                        if (!(c < Field<int32_t>(node, 0xc)))
                                            break;
                                    }
                                }
                                if (c == nc)
                                {
                                    Field<uint8_t*>(node, 0x10 + nc * 4) = parent;
                                    ++Field<int32_t>(node, 0xc);
                                }
                            }
                        }
                    }
                }
            }
            for (uint32_t m = 0;; ++m)
            {
                uint8_t* pe = TableEntry(Field<uint8_t*>(item, 4), m, 0x10, 0);
                if (pe == nullptr)
                    break;
                const uint32_t part = u(pe) + Field<uint16_t>(pe, 2) + 4;
                uint8_t* r = B(orig::fn_004f0cd0(Field<uint32_t>(node, 8), part));
                if (r == nullptr)
                    continue;
                uint32_t* slot = EntrySlot(r);
                if (*slot != 0)
                    continue;
                bool fresh = true;
                if (exclude != nullptr && TableFind(Field<uint8_t*>(exclude, 4), part) != nullptr)
                    fresh = false;
                if (include != nullptr && TableFind(Field<uint8_t*>(include, 4), part) == nullptr)
                    fresh = false;
                if (prefixes != nullptr && *prefixes != nullptr)
                {
                    for (const char* const* pp = prefixes; *pp != nullptr; ++pp)
                    {
                        const char* str = *pp;
                        const char* nm = reinterpret_cast<const char*>(part);
                        uint32_t a = 0;
                        char c = str[0];
                        while (c != 0 && nm[a] == c)
                            c = str[++a];
                        if (str[a] != 0)
                            continue;
                        uint8_t* pr = B(orig::fn_004f0cd0(Field<uint32_t>(node, 8), u(str)));
                        if (pr != nullptr)
                        {
                            uint32_t* ps = EntrySlot(pr);
                            if (*ps == 0)
                            {
                                const uint32_t v = Field<uint32_t>(node, 4) + 1;
                                Field<uint32_t>(node, 4) = v;
                                *ps = v;
                            }
                            *slot = *ps;
                        }
                        fresh = false;
                    }
                }
                if (fresh)
                {
                    const uint32_t v = Field<uint32_t>(node, 4) + 1;
                    Field<uint32_t>(node, 4) = v;
                    *slot = v;
                }
            }
        }
    }

    // Layout.
    for (uint32_t k = 0;; ++k)
    {
        uint8_t* e = TableEntry(Field<uint8_t*>(dict, 4), k, 0x10, 0);
        if (e == nullptr)
            break;
        fn_004edf80(reinterpret_cast<int32_t*>(*EntrySlot(e)));
    }

    // Final numbers into the part records.
    int32_t best = 0;
    for (uint32_t k = 0;; ++k)
    {
        uint8_t* e = TableEntry(Field<uint8_t*>(dict, 4), k, 0x10, 0);
        if (e == nullptr)
            break;
        uint8_t* p = e + Field<uint16_t>(e, 2);
        uint8_t* node = Field<uint8_t*>(p, 0);
        const uint32_t name = u(p + 4);
        if (node == nullptr)
            continue;
        for (uint32_t m = 0;; ++m)
        {
            uint8_t* pe = TableEntry(Field<uint8_t*>(Field<uint8_t*>(node, 8), 4), m, 0x10, 0);
            if (pe == nullptr)
                break;
            uint8_t* q = pe + Field<uint16_t>(pe, 2);
            int32_t v = Field<int32_t>(q, 0);
            if (v != 0)
                v += Field<int32_t>(node, 0);
            if (best < v)
                best = v;
            const uint32_t part = u(q + 4);
            for (int32_t j = 0; j < count; ++j)
            {
                uint8_t* rec = TableFind(Field<uint8_t*>(Field<uint8_t*>(objs[j], 0x28), 4), name);
                if (rec == nullptr)
                    continue;
                uint8_t* item = Field<uint8_t*>(rec, Field<uint16_t>(rec, 2));
                uint8_t* pr = TableFind(Field<uint8_t*>(item, 4), part);
                if (pr == nullptr)
                    continue;
                uint8_t* partRec = Field<uint8_t*>(pr, Field<uint16_t>(pr, 2));
                if (partRec != nullptr)
                    Field<uint16_t>(partRec, 0xa) = static_cast<uint16_t>(v);
            }
        }
    }

    // Free every block of the temporary heap.
    uint8_t* const head = h + 0x10;
    uint8_t* first = Field<uint8_t*>(h, 0x10);
    uint8_t* next = Field<uint8_t*>(first, 0);
    uint8_t* node = first == head ? nullptr : first;
    while (node != nullptr)
    {
        orig::fn_004e1f70(u(h), u(node + 0xc));
        if (next == head)
            break;
        node = next;
        next = Field<uint8_t*>(next, 0);
    }
    return static_cast<uint32_t>(best + 1);
}

static uint32_t fn_004ee010_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t /*a3*/, uint32_t a4, uint32_t a5,
                                uint32_t /*a6*/)
{
    return fn_004ee010(B(a0), B(a1), reinterpret_cast<const char* const*>(a2), reinterpret_cast<uint8_t**>(a4),
                       static_cast<int32_t>(a5));
}
ABI_ENTRY_004ee010(fn_004ee010_Entry, fn_004ee010_Abi)
HOOK(0x004ee010, "fn_004ee010", fn_004ee010_Entry, nullptr);

// ================================================================================================================
// 0x004e8620: relocation and byte-swapping of a loaded mesh record
// ================================================================================================================

namespace
{
// The address-range lookup with the binary search written inline (instead of 0x004e0f40); the key pointer is the
// address of a local holding addr. Returns the translated address (reads through a null record like the original).
uint32_t RelocLocateInline(uint8_t* ctx, uint32_t addr)
{
    uint32_t key = addr;
    uint8_t* t = Field<uint8_t*>(Field<uint8_t*>(ctx, 0), 8);
    uint8_t* rec = TableFind(t, u(&key));
    volatile const uint16_t* w = reinterpret_cast<volatile const uint16_t*>(u(rec) + 2);
    const uint8_t* q = rec + *w;
    return Field<uint32_t>(q, 4) - Field<uint32_t>(q, 0) + addr;
}
inline void BSwapAt(uint32_t a)
{
    uint32_t* d = reinterpret_cast<uint32_t*>(a);
    const uint32_t v = *d;
    *d = (v << 24) | ((v & 0xff00u) << 8) | ((v >> 8) & 0xff00u) | (v >> 24);
}
// Unless relocate-only: locate addr (0x004e0f40 search) and, when the context swaps, Endian_Swap32 it (the call).
void LocSwapCall(uint8_t* ctx, uint32_t addr)
{
    if (Field<uint32_t>(ctx, 4) != 0)
        return;
    const uint32_t a = RelocLocate(ctx, addr);
    if (Field<uint32_t>(ctx, 8) != 0)
        Endian_Swap32(reinterpret_cast<uint32_t*>(a));
}
// As LocSwapCall with the byte swap written inline.
void LocSwap32F(uint8_t* ctx, uint32_t addr)
{
    if (Field<uint32_t>(ctx, 4) != 0)
        return;
    const uint32_t a = RelocLocate(ctx, addr);
    if (Field<uint32_t>(ctx, 8) != 0)
        BSwapAt(a);
}
// Inline-search variants: 32-bit swap, 16-bit swap, and search only (byte fields: the result is not used).
void LocSwap32I(uint8_t* ctx, uint32_t addr)
{
    if (Field<uint32_t>(ctx, 4) != 0)
        return;
    const uint32_t a = RelocLocateInline(ctx, addr);
    if (Field<uint32_t>(ctx, 8) != 0)
        BSwapAt(a);
}
void LocSwap16I(uint8_t* ctx, uint32_t addr)
{
    if (Field<uint32_t>(ctx, 4) != 0)
        return;
    const uint32_t a = RelocLocateInline(ctx, addr);
    if (Field<uint32_t>(ctx, 8) != 0)
        Swap16(a);
}
void LocSearchI(uint8_t* ctx, uint32_t addr)
{
    if (Field<uint32_t>(ctx, 4) != 0)
        return;
    uint32_t key = addr;
    TableFind(Field<uint8_t*>(Field<uint8_t*>(ctx, 0), 8), u(&key));
}
inline void FixPtr(uint8_t* ctx, uint32_t field)
{
    orig::fn_004e2040(field, u(ctx));
}
inline void SwapField(uint8_t* ctx, uint32_t field, uint32_t size)
{
    orig::fn_004e2160(u(ctx), field, size, 0, 0);
}
}  // namespace

// 0x004e8620 (eax = relocation context; stack: mesh record; caller pops): fixes up and byte-swaps one loaded mesh
// record (the records of the item tables handled by 0x004eae90). In order: Reloc_FixupPointer on +4, +0x70, +0x74;
// 0x004f0e60 on the tables +0x70 (callbacks 0x004f0dd0 / 0x004f0e50) and +0x74 (0x004f0dd0 / 0x004f0dd0);
// Reloc_FixupPointer on +0x28..+0x34, +0x3c, +0x48..+0x6c, +0x7c, +0x84, +0x8c, +0x94; the counts +0x10..+0x24,
// +0x38, +0x40, +0x78, +0x80, +0x88, +0x90 (located, Endian_Swap32); the [+0x10] vertices of the array +0x28 (stride
// [0x005fb8ac + 4 * format +0x24], formats 0-5, jump table 0x004eaa84; Reloc_SwapField on their dwords; a null vertex
// address prints 0x0058affc "***Warning, degenerate positions array in '%s'" (0x00539d33) with the name [+4]+0x34
// and ends the loop); the [+0x14] triangles (3 dwords each at +0x2c) and the per-triangle lists at +0x3c (count
// then items); the arrays +0x30 ([+0x18] dwords), +0x34 ([+0x1c] pairs) and +0x48..+0x6c ([+0x40] dwords each);
// 0x004fbae0 (eax = record, edi = ctx); then the [+0x78] 100-byte records of +0x7c, the [+0x80] 0x2c-byte records of
// +0x84, the [+0x88] 0x58-byte records of +0x8c (with a 4x4 matrix at +0xc) and the [+0x90] 0x18-byte records of
// +0x94 (with a word list at +0x14). "Located" means: unless the context is relocate-only (+4), the field's loaded
// address is looked up (0x004e0f40 or the same binary search inline, as in the original) and the value there is
// byte-swapped when the context swaps (+8). Returns 0 (the original's EAX is not meaningful). Caller 0x004eae90.
uint32_t fn_004e8620(uint8_t* ctx, uint8_t* m)
{
    FixPtr(ctx, u(m + 4));
    FixPtr(ctx, u(m + 0x70));
    FixPtr(ctx, u(m + 0x74));
    fn_004f0e60(ctx, Field<uint8_t*>(m, 0x70), 0x004f0dd0, 0x004f0e50);
    fn_004f0e60(ctx, Field<uint8_t*>(m, 0x74), 0x004f0dd0, 0x004f0dd0);
    static const uint32_t ptrs[] = {0x28, 0x2c, 0x30, 0x34, 0x3c, 0x48, 0x4c, 0x50, 0x54, 0x58,
                                    0x5c, 0x60, 0x64, 0x68, 0x6c, 0x7c, 0x84, 0x8c, 0x94};
    for (const uint32_t off : ptrs)
        FixPtr(ctx, u(m + off));
    static const uint32_t counts[] = {0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x38, 0x40, 0x78, 0x80, 0x88, 0x90};
    for (const uint32_t off : counts)
        LocSwapCall(ctx, u(m + off));

    // Vertices: per format, the dwords Reloc_SwapField swaps (in the original's order).
    if (Field<uint32_t>(m, 0x28) != 0 && Field<int32_t>(m, 0x10) > 0)
    {
        static const uint8_t f0[] = {0, 4, 8};
        static const uint8_t f1[] = {0xc, 0, 4, 8};
        static const uint8_t f2[] = {0xc, 0x10, 0x14, 0, 4, 8};
        static const uint8_t f3[] = {0xc, 0x10, 0x14, 0x18, 0x1c, 0, 4, 8};
        static const uint8_t f4[] = {0xc, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0, 4, 8};
        static const uint8_t f5[] = {0xc, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0, 4, 8};
        static const uint8_t* const lists[] = {f0, f1, f2, f3, f4, f5};
        static const uint32_t sizes[] = {sizeof(f0), sizeof(f1), sizeof(f2), sizeof(f3), sizeof(f4), sizeof(f5)};
        int32_t i = 0;
        do
        {
            const uint32_t format = Field<uint32_t>(m, 0x24);
            if (format <= 5)
            {
                const uint32_t v = GameGlobal<uint32_t>(0x005fb8ac + format * 4) * static_cast<uint32_t>(i) +
                                   Field<uint32_t>(m, 0x28);
                if (v == 0)
                {
                    GameFunction<int(__cdecl*)(uint32_t, uint32_t)>(0x00539d33)(0x0058affc,
                                                                                Field<uint32_t>(m, 4) + 0x34);
                    i = Field<int32_t>(m, 0x10);
                }
                else
                {
                    for (uint32_t k = 0; k < sizes[format]; ++k)
                        SwapField(ctx, v + lists[format][k], 4);
                }
            }
            ++i;
        } while (i < Field<int32_t>(m, 0x10));
    }

    // Triangles and per-triangle lists.
    uint32_t* list = Field<uint32_t*>(m, 0x3c);
    for (int32_t i = 0; i < Field<int32_t>(m, 0x14); ++i)
    {
        const uint32_t tri = Field<uint32_t>(m, 0x2c);
        if (tri != 0)
        {
            LocSwap32F(ctx, i * 0xc + Field<uint32_t>(m, 0x2c));
            LocSwap32F(ctx, i * 0xc + 4 + Field<uint32_t>(m, 0x2c));
            LocSwap32F(ctx, i * 0xc + 8 + Field<uint32_t>(m, 0x2c));
        }
        if (list != nullptr)
        {
            LocSwap32F(ctx, u(list));
            if (*list != 0)
            {
                uint32_t* item = list + 1;
                for (uint32_t k = 0; k < *list; ++k, ++item)
                    LocSwap32I(ctx, u(item));
            }
            list = list + *list + 1;
        }
    }
    if (Field<uint32_t>(m, 0x30) != 0)
    {
        for (int32_t i = 0; i < Field<int32_t>(m, 0x18); ++i)
            LocSwap32I(ctx, Field<uint32_t>(m, 0x30) + i * 4);
    }
    if (Field<uint32_t>(m, 0x34) != 0)
    {
        for (int32_t i = 0; i < Field<int32_t>(m, 0x1c); ++i)
        {
            LocSwap32I(ctx, Field<uint32_t>(m, 0x34) + i * 8);
            LocSwap32I(ctx, i * 8 + 4 + Field<uint32_t>(m, 0x34));
        }
    }
    for (int32_t i = 0; i < Field<int32_t>(m, 0x40); ++i)
    {
        for (uint32_t off = 0x48; off <= 0x6c; off += 4)
        {
            if (Field<uint32_t>(m, off) != 0)
                LocSwap32I(ctx, Field<uint32_t>(m, off) + i * 4);
        }
    }
    orig::fn_004fbae0(u(m), u(ctx));

    // 100-byte records at +0x7c.
    for (int32_t i = 0; i < Field<int32_t>(m, 0x78); ++i)
    {
        const uint32_t o = i * 100;
        FixPtr(ctx, o + 4 + Field<uint32_t>(m, 0x7c));
        LocSwap16I(ctx, o + 0x14 + Field<uint32_t>(m, 0x7c));
        LocSwap16I(ctx, o + 0x16 + Field<uint32_t>(m, 0x7c));
        LocSwap16I(ctx, o + 0x18 + Field<uint32_t>(m, 0x7c));
        LocSwap16I(ctx, o + 0x1a + Field<uint32_t>(m, 0x7c));
        LocSwap16I(ctx, o + 0x1c + Field<uint32_t>(m, 0x7c));
        LocSearchI(ctx, o + 0x32 + Field<uint32_t>(m, 0x7c));
        LocSwap16I(ctx, o + 0x30 + Field<uint32_t>(m, 0x7c));
        LocSwap16I(ctx, o + 0x34 + Field<uint32_t>(m, 0x7c));
        LocSearchI(ctx, o + 0x36 + Field<uint32_t>(m, 0x7c));
        LocSearchI(ctx, o + 0x37 + Field<uint32_t>(m, 0x7c));
        LocSwap32I(ctx, o + 0x38 + Field<uint32_t>(m, 0x7c));
        LocSwap32I(ctx, o + 0x3c + Field<uint32_t>(m, 0x7c));
        LocSwap32I(ctx, o + 0x40 + Field<uint32_t>(m, 0x7c));
        for (uint32_t off = 0x44; off <= 0x60; off += 4)
            SwapField(ctx, o + off + Field<uint32_t>(m, 0x7c), 4);
    }

    // 0x2c-byte records at +0x84.
    for (int32_t i = 0; i < Field<int32_t>(m, 0x80); ++i)
    {
        const uint32_t o = i * 0x2c;
        FixPtr(ctx, Field<uint32_t>(m, 0x84) + 4 + o);
        FixPtr(ctx, Field<uint32_t>(m, 0x84) + 0x28 + o);
        for (uint32_t off = 0x10; off <= 0x20; off += 4)
            SwapField(ctx, Field<uint32_t>(m, 0x84) + off + o, 4);
        uint8_t* r = B(Field<uint32_t>(m, 0x84) + o);
        if (Field<uint32_t>(r, 0x28) != 0 && Field<int32_t>(r, 0x20) > 0)
        {
            int32_t k = 0;
            do
            {
                SwapField(ctx, Field<uint32_t>(r, 0x28) + k * 4, 4);
                r = B(Field<uint32_t>(m, 0x84) + o);
                ++k;
            } while (k < Field<int32_t>(r, 0x20));
        }
        const uint32_t slot = Field<uint32_t>(m, 0x84) + o + 0x24;
        FixPtr(ctx, slot);
        const uint32_t p = *reinterpret_cast<uint32_t*>(slot);
        if (p != 0)
        {
            FixPtr(ctx, p + 4);
            FixPtr(ctx, p + 0x3c);
        }
    }

    // 0x58-byte records at +0x8c.
    for (int32_t i = 0; i < Field<int32_t>(m, 0x88); ++i)
    {
        const uint32_t o = i * 0x58;
        FixPtr(ctx, Field<uint32_t>(m, 0x8c) + 4 + o);
        SwapField(ctx, Field<uint32_t>(m, 0x8c) + 8 + o, 2);
        SwapField(ctx, Field<uint32_t>(m, 0x8c) + 0x4c + o, 4);
        for (uint32_t k = 0; k < 16; ++k)
            SwapField(ctx, Field<uint32_t>(m, 0x8c) + o + 0xc + k * 4, 4);
    }

    // 0x18-byte records at +0x94.
    for (int32_t i = 0; i < Field<int32_t>(m, 0x90); ++i)
    {
        const uint32_t o = i * 0x18;
        FixPtr(ctx, Field<uint32_t>(m, 0x94) + 4 + o);
        FixPtr(ctx, o + 0x14 + Field<uint32_t>(m, 0x94));
        SwapField(ctx, o + 0x10 + Field<uint32_t>(m, 0x94), 4);
        for (int32_t k = 0; k < Field<int32_t>(B(Field<uint32_t>(m, 0x94)), 0x10 + o); ++k)
            SwapField(ctx, Field<uint32_t>(B(Field<uint32_t>(m, 0x94)), o + 0x14) + k * 2, 2);
    }
    return 0;
}

static uint32_t fn_004e8620_Abi(uint32_t eax, uint32_t a0)
{
    return fn_004e8620(B(eax), B(a0));
}
ABI_ENTRY_004e8620(fn_004e8620_Entry, fn_004e8620_Abi)
HOOK(0x004e8620, "fn_004e8620", fn_004e8620_Entry, nullptr);
