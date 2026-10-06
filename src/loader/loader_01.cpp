// Recovered: loader batch loader_01 (0x004479d0-0x004e8430: level chunk dispatch, level start/stop, *.big
// directory scans, object-system loaders, texture/font/render-mode loaders, chunk-stream helpers). Static recovery
// 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified from the disassembly. "AL = x" returns keep the upper 24 bits EAX had in the
// original (usually the previous call's result) where that is cheap.
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
inline uint8_t* B(uint32_t v)
{
    return reinterpret_cast<uint8_t*>(v);
}
inline float bf(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
inline uint32_t fb(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
// The game object (global 0x005fe490).
inline uint8_t* Gm()
{
    return GameGlobal<uint8_t*>(0x005fe490);
}
// EAX with AL replaced (setcc / mov al, imm on top of an earlier value).
inline uint32_t Lo8(uint32_t eax, uint32_t al)
{
    return (eax & 0xffffff00u) | (al & 0xffu);
}
// The game's allocator hook (function pointer at 0x005cacc0, cdecl (size, tag)).
inline uint32_t Alloc(uint32_t size, uint32_t tag)
{
    return u(GameGlobal<void*(__cdecl*)(uint32_t, uint32_t)>(0x005cacc0)(size, tag));
}
// 0x004e08d0 (ecx = heap 0x0063a210; stack: block): the game's free.
inline uint32_t Free(uint32_t block)
{
    return orig::fn_004e08d0(0x0063a210u, block);
}
// Imports the game calls through its IAT slot (keeps import hooks).
template <class F>
F Imp(uintptr_t slot)
{
    return *reinterpret_cast<F*>(slot);
}
inline BOOL FindNext(HANDLE h, WIN32_FIND_DATAA* d)
{
    return Imp<decltype(&FindNextFileA)>(0x0056b0d8)(h, d);
}
inline BOOL FindClose_(HANDLE h)
{
    return Imp<decltype(&FindClose)>(0x0056b0dc)(h);
}
// _makepath (0x0053859e, cdecl, 5 arguments). Returned as EAX for callers that keep it.
inline uint32_t MakePath(uint32_t path, uint32_t drive, uint32_t dir, uint32_t fname, uint32_t ext)
{
    return GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0053859e)(
        path, drive, dir, fname, ext);
}

// Calls an original function that takes EBX and EDI (and `stackCount` caller-popped stack arguments, 0 or 1).
// symbols/abi.csv lists 0x004f2030 as "inputs [] stack 6" and 0x004f59f0 as "inputs [ecx edi]"; their disassembly
// reads EBX and EDI (0x004f206c mov ecx, ebx; 0x004f5a54 mov esi, ebx) and one / no stack argument.
__declspec(noinline) uint32_t CallEbxEdi(uint32_t target, uint32_t ebx_, uint32_t edi_)
{
    uint32_t r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov eax, target
        mov ebx, ebx_
        mov edi, edi_
        call eax
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov r, eax
    }
    return r;
}
__declspec(noinline) uint32_t CallEbxEdi1(uint32_t target, uint32_t ebx_, uint32_t edi_, uint32_t a0)
{
    uint32_t r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a0
        mov eax, target
        mov ebx, ebx_
        mov edi, edi_
        call eax
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov r, eax
    }
    return r;
}
// 0x004f2030 (ebx = find state {+0 handle, +4 WIN32_FIND_DATAA, +0x144 path}, edi = out: name pointer or null;
// stack: pattern): FindFirstFile over the search roots 0x0063a204 / 0x0063a208 / as given (via 0x004f1f40). AL = found.
inline uint32_t FindFirst(uint32_t state, uint32_t outName, uint32_t pattern)
{
    return CallEbxEdi1(0x004f2030, state, outName, pattern);
}
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t fn_004479e0(uint32_t* p);
uint32_t fn_00447c70(uint8_t* obj);
uint32_t fn_00448400(uint8_t* obj);
uint32_t fn_0044ac10(uint32_t eax, uint8_t* level);
uint32_t fn_0044acc0(uint8_t* level);
uint32_t fn_0044ad20(uint32_t eax, uint8_t* level);
uint32_t fn_0045a860(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t fn_0045aad0(uint32_t eax, uint32_t a0, uint32_t a1);
uint32_t ObjectSystem_LoadTemplates(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t fn_0045d510(uint32_t eax, uint32_t edi, uint32_t a0);
uint32_t fn_00462950(uint32_t eax, uint32_t edi, uint32_t a0);
uint32_t Font_Load(uint32_t eax, uint32_t ecx, uint32_t a0);

// ================================================================================================================
// 0x004479d0 - 0x0044ad20: level object, chunk dispatch, *.big scans
// ================================================================================================================

// 0x004479d0 (ecx = object): AL = ([ecx+4] == 0) via sete, EAX upper bits zero. Caller 0x00412450.
uint32_t fn_004479d0(uint8_t* obj)
{
    return Field<uint32_t>(obj, 4) == 0 ? 1u : 0u;
}

// 0x004479e0 (eax = 5-dword record): {9, 0, 0, 0, byte 0}. Type 9 is the chunk-file object type ChunkFile_Load
// (0x00447a00) accepts. Not executed in recorded runs.
uint32_t fn_004479e0(uint32_t* p)
{
    p[0] = 9;
    p[1] = 0;
    p[2] = 0;
    p[3] = 0;
    reinterpret_cast<uint8_t*>(p)[0x10] = 0;
    return 0;
}

// 0x00447bb0 (stack: 1 dword, never read): creates an empty chunk-file object when the level chunk file is missing
// (caller 0x0044acc0): a 0x30-byte heap header from the allocator hook (tag 2) set up as a one-block heap, a
// 0x14-byte type-9 record (0x004479e0) and a 0x14-byte record of type 3 whose +4 is a 0x100-entry table from
// 0x004e0ff0 (callback 0x004f0c40 stored at 0x005caa5c first). Returns the type-9 record. The second 0x004e1e90
// call's four pushes stay on the stack as 0x004e0ff0's arguments 7-10 (add esp, 0x28 after it). Not executed in
// recorded runs.
uint32_t fn_00447bb0(uint32_t /*a0*/)
{
    uint8_t* h = B(Alloc(0x30, 2));
    std::memset(h, 0, 0x30);
    Field<uint32_t>(h, 0xc) = 0;
    Field<uint32_t>(h, 0) = u(h + 0x10);
    Field<uint32_t>(h, 0x10) = u(h);
    Field<uint32_t>(h, 4) = 0x30;
    Field<uint16_t>(h, 8) = 1;
    Field<uint16_t>(h, 0xa) = 2;
    Field<uint32_t>(h, 0x14) = 0x30;
    Field<uint32_t>(h, 0x18) = 1;
    Field<uint32_t>(h, 0x1c) = 4;
    Field<uint32_t>(h, 0x20) = u(h);
    const uint32_t rec = orig::fn_004e1e90(u(h), 0x14, 1, 2);
    Field<uint32_t>(h, 0x20) = rec;
    fn_004479e0(reinterpret_cast<uint32_t*>(rec));
    Field<uint32_t>(B(rec), 8) = u(h);
    const uint32_t table = orig::fn_004e1e90(u(h), 0x14, 1, 2);
    Field<uint32_t>(B(table), 0) = u(h);
    Field<uint32_t>(B(table), 8) = 3;
    Field<uint16_t>(B(table), 0x10) = 1;
    GameGlobal<uint32_t>(0x005caa5c) = 0x004f0c40;
    Field<uint32_t>(B(table), 4) = orig::fn_004e0ff0(u(h), 0x100, 0, 1, 0x400, 2, 0x005caa58, u(h), 0x14, 1, 2);
    Field<uint32_t>(B(rec), 0xc) = table;
    return rec;
}

// 0x00447c70 (esi = chunk-file object): releases it: 0x004e1e50 (ebx = [[+0xc]+4]+0), 0x004e1f70([[+0xc]], [+0xc]);
// then 0x004e1e50 (ebx = [+8]) and, when byte +0x10 (we allocated) is set, 0x004e1f70([[+8]+0xc], [+4]).
// Callers 0x0044a670, 0x0044ad20. AL = byte +0x10 unless the last free ran.
uint32_t fn_00447c70(uint8_t* obj)
{
    uint8_t* rec = Field<uint8_t*>(obj, 0xc);
    const uint32_t heapA = Field<uint32_t>(rec, 0);
    orig::fn_004e1e50(Field<uint32_t>(Field<uint8_t*>(rec, 4), 0));
    orig::fn_004e1f70(heapA, u(rec));
    uint8_t* blk = Field<uint8_t*>(obj, 8);
    const uint32_t buffer = Field<uint32_t>(obj, 4);
    const uint32_t heapB = Field<uint32_t>(blk, 0xc);
    uint32_t r = orig::fn_004e1e50(u(blk));
    const uint8_t owned = Field<uint8_t>(obj, 0x10);
    r = Lo8(r, owned);
    if (owned != 0)
        r = orig::fn_004e1f70(heapB, buffer);
    return r;
}

// 0x00447cb0 (eax = chunk-file object, edi = 4-dword cursor {3 dwords out, +0xc key}): looks the key up in the
// sorted table of [[eax+0xc]+4] (count +0x1c, +0x20 nonzero, searcher [+0x24] via 0x004e0f40); on a hit whose entry
// (entry + word[entry+2]) holds a non-null pointer, copies its 3 dwords to the cursor, sets cursor+0xc = entry +
// word[entry+2] + 4 and returns AL = 1, else AL = 0. Caller 0x00412450.
uint32_t fn_00447cb0(uint8_t* obj, uint32_t* cursor)
{
    uint8_t* t = Field<uint8_t*>(Field<uint8_t*>(obj, 0xc), 4);
    const uint32_t count = Field<uint32_t>(t, 0x1c);
    const uint32_t key = cursor[3];
    if (count == 0 || Field<uint32_t>(t, 0x20) == 0)
        return Lo8(u(t), 0);
    uint8_t* s = Field<uint8_t*>(t, 0x24);
    Field<uint32_t>(s, 0) = count;
    Field<uint32_t>(s, 0x18) = key;
    orig::fn_004e0f40(u(s));
    const uint32_t cmp = Field<uint32_t>(s, 0xc);
    if (cmp != 0)
        return Lo8(cmp, 0);
    uint8_t** entries = Field<uint8_t**>(s, 0x10);
    uint8_t* entry = entries[Field<uint32_t>(s, 8)];
    if (entry == nullptr)
        return Lo8(u(entries), 0);
    uint32_t* slot = reinterpret_cast<uint32_t*>(entry + Field<uint16_t>(entry, 2));
    if (slot == nullptr)
        return 0;
    const uint32_t* src = reinterpret_cast<const uint32_t*>(*slot);
    if (src == nullptr)
        return 0;
    cursor[0] = src[0];
    cursor[1] = src[1];
    cursor[2] = src[2];
    const uint32_t next = u(entry) + Field<uint16_t>(entry, 2) + 4;
    cursor[3] = next;
    return Lo8(next, 1);
}

// 0x004483e0 (edx = landscape): 0x00448400(edx) then returns EDX (unchanged by it). Caller 0x004011e0.
uint32_t fn_004483e0(uint8_t* obj)
{
    fn_00448400(obj);
    return u(obj);
}

// 0x00448400 (edx = landscape): resets it: 17 dwords at +0xf04 zeroed and set to an identity matrix (1.0 at +0xf04,
// +0xf18, +0xf2c, +0xf40; 0x44-byte game matrix), +0xef4/+0xef8/+0xefc/+0xf4c/+0xf00 and +0..+0x10 zeroed.
// Callers 0x004483e0, Landscape_Load (0x00448470). Returns 0.
uint32_t fn_00448400(uint8_t* obj)
{
    uint32_t* m = reinterpret_cast<uint32_t*>(obj + 0xf04);
    for (int i = 0; i < 0x11; ++i)
        m[i] = 0;
    m[0] = 0x3f800000;
    m[5] = 0x3f800000;
    m[10] = 0x3f800000;
    m[15] = 0x3f800000;
    Field<uint32_t>(obj, 0xef4) = 0;
    Field<uint32_t>(obj, 0xef8) = 0;
    Field<uint32_t>(obj, 0xefc) = 0;
    Field<uint32_t>(obj, 0xf4c) = 0;
    Field<uint32_t>(obj, 0xf00) = 0;
    Field<uint32_t>(obj, 4) = 0;
    Field<uint32_t>(obj, 8) = 0;
    Field<uint32_t>(obj, 0xc) = 0;
    Field<uint32_t>(obj, 0x10) = 0;
    Field<uint32_t>(obj, 0) = 0;
    return 0;
}

// 0x00448460 (eax = pointer): returns [eax]. Not executed in recorded runs.
uint32_t fn_00448460(uint32_t* p)
{
    return *p;
}

// 0x00448c70 (edi = landscape): unloads it: 0x00457000 (esi = [+0]; [game+0xa8c]) and clears +0; frees +0xef4 and
// +0xef8 (0x004e08d0); for +0xefc: 0x0045aad0 (AL = 0; game+0xa84, it), then releases its chunk storage
// (0x004e1e50 ebx = [+0xc], and 0x004e1f70([[+0xc]+0xc], [+8]) when word +6 is set) and clears it; 0x004ed590
// (ebx = +0xf4c) and clears it; frees +4. Caller 0x0044a670.
uint32_t fn_00448c70(uint8_t* obj)
{
    uint32_t r = 0;
    const uint32_t first = Field<uint32_t>(obj, 0);
    if (first != 0)
    {
        r = orig::fn_00457000(first, Field<uint32_t>(Gm(), 0xa8c));
        Field<uint32_t>(obj, 0) = 0;
    }
    if (Field<uint32_t>(obj, 0xef4) != 0)
    {
        r = Free(Field<uint32_t>(obj, 0xef4));
        Field<uint32_t>(obj, 0xef4) = 0;
    }
    if (Field<uint32_t>(obj, 0xef8) != 0)
    {
        r = Free(Field<uint32_t>(obj, 0xef8));
        Field<uint32_t>(obj, 0xef8) = 0;
    }
    const uint32_t res = Field<uint32_t>(obj, 0xefc);
    if (res != 0)
    {
        const uint32_t a84 = Field<uint32_t>(Gm(), 0xa84);
        r = fn_0045aad0(Lo8(a84, 0), a84, res);
        uint8_t* blk = Field<uint8_t*>(obj, 0xefc);
        if (blk != nullptr)
        {
            uint8_t* store = Field<uint8_t*>(blk, 0xc);
            const uint32_t buffer = Field<uint32_t>(blk, 8);
            const uint32_t heap = Field<uint32_t>(store, 0xc);
            r = orig::fn_004e1e50(u(store));
            if (Field<uint16_t>(blk, 6) != 0)
                r = orig::fn_004e1f70(heap, buffer);
        }
        Field<uint32_t>(obj, 0xefc) = 0;
    }
    const uint32_t f4c = Field<uint32_t>(obj, 0xf4c);
    if (f4c != 0)
    {
        r = orig::fn_004ed590(f4c);
        Field<uint32_t>(obj, 0xf4c) = 0;
    }
    if (Field<uint32_t>(obj, 4) != 0)
    {
        r = Free(Field<uint32_t>(obj, 4));
        Field<uint32_t>(obj, 4) = 0;
    }
    return r;
}

// 0x00448d60 (al = flag, esi = landscape; stack: byte flag2; ret 4): when AL, 0x0045aad0 (AL = 1; game+0xa84,
// +0xefc); when flag2, 0x0045a860(game+0xa84, +0xefc, 0, 1). Caller 0x0045b190. Not executed in recorded runs.
uint32_t fn_00448d60(uint32_t eax, uint8_t* obj, uint32_t a0)
{
    if (static_cast<uint8_t>(eax) != 0)
        eax = fn_0045aad0(Lo8(Field<uint32_t>(obj, 0xefc), 1), Field<uint32_t>(Gm(), 0xa84),
                          Field<uint32_t>(obj, 0xefc));
    eax = Lo8(eax, a0);
    if (static_cast<uint8_t>(a0) != 0)
        eax = fn_0045a860(Field<uint32_t>(Gm(), 0xa84), Field<uint32_t>(obj, 0xefc), 0, 1);
    return eax;
}

// 0x00449440 (eax = object): returns float +0x2c in st0. Not executed in recorded runs.
float fn_00449440(uint8_t* obj)
{
    return Field<float>(obj, 0x2c);
}

// 0x00449450 (eax = 6-dword out, ecx = source): out = {[+0x14], [+0x1c], [+0x24], [+0x18], [+0x20], [+0x28]}.
// Caller 0x0048cb30.
void fn_00449450(uint32_t* out, uint8_t* src)
{
    out[0] = Field<uint32_t>(src, 0x14);
    out[1] = Field<uint32_t>(src, 0x1c);
    out[2] = Field<uint32_t>(src, 0x24);
    out[3] = Field<uint32_t>(src, 0x18);
    out[4] = Field<uint32_t>(src, 0x20);
    out[5] = Field<uint32_t>(src, 0x28);
}

// 0x00449510 (edx = object): constructor-style reset: dwords +0..+8 and +0x20..+0x6c zeroed, bytes +0xc..+0x11
// zeroed, byte +0x12 = 1, +0x70 = 1, +0x74 = 0, bytes +0x78..+0x7c zeroed, identity game matrix at +0x80.
// Returns the object. Callers Landscape_Load (0x00448470), 0x004517b0, 0x00456a90.
uint32_t fn_00449510(uint8_t* p)
{
    Field<uint32_t>(p, 0x20) = 0;
    Field<uint32_t>(p, 0x24) = 0;
    Field<uint32_t>(p, 0x3c) = 0;
    Field<uint32_t>(p, 0x38) = 0;
    Field<uint32_t>(p, 0x34) = 0;
    Field<uint32_t>(p, 0x30) = 0;
    Field<uint32_t>(p, 0x4c) = 0;
    Field<uint32_t>(p, 0x48) = 0;
    Field<uint32_t>(p, 0x44) = 0;
    Field<uint32_t>(p, 0x40) = 0;
    Field<uint32_t>(p, 0x5c) = 0;
    Field<uint32_t>(p, 0x58) = 0;
    Field<uint32_t>(p, 0x54) = 0;
    Field<uint32_t>(p, 0x50) = 0;
    Field<uint32_t>(p, 0x6c) = 0;
    Field<uint32_t>(p, 0x68) = 0;
    Field<uint32_t>(p, 0x64) = 0;
    Field<uint32_t>(p, 0x60) = 0;
    Field<uint32_t>(p, 0) = 0;
    Field<uint32_t>(p, 4) = 0;
    Field<uint32_t>(p, 8) = 0;
    for (uint32_t o = 0xc; o <= 0x11; ++o)
        Field<uint8_t>(p, o) = 0;
    Field<uint8_t>(p, 0x12) = 1;
    Field<uint32_t>(p, 0x70) = 1;
    Field<uint32_t>(p, 0x74) = 0;
    for (uint32_t o = 0x78; o <= 0x7c; ++o)
        Field<uint8_t>(p, o) = 0;
    uint32_t* m = reinterpret_cast<uint32_t*>(p + 0x80);
    for (int i = 0; i < 0x11; ++i)
        m[i] = 0;
    m[0] = 0x3f800000;
    m[5] = 0x3f800000;
    m[10] = 0x3f800000;
    m[15] = 0x3f800000;
    return u(p);
}

// 0x004497f0 (edx = object): reset: +4, +0xc = 0, bytes +0x10..+0x12 = 0, +0 = -1, 0x101 dwords at +0x5c, 6 at
// +0x14 and 9 at +0x2c zeroed. Returns the object. Caller 0x004011e0.
uint32_t fn_004497f0(uint8_t* p)
{
    Field<uint32_t>(p, 4) = 0;
    Field<uint32_t>(p, 0xc) = 0;
    Field<uint8_t>(p, 0x10) = 0;
    Field<uint8_t>(p, 0x11) = 0;
    Field<uint8_t>(p, 0x12) = 0;
    Field<uint32_t>(p, 0) = 0xffffffffu;
    std::memset(p + 0x5c, 0, 0x101 * 4);
    std::memset(p + 0x14, 0, 6 * 4);
    std::memset(p + 0x2c, 0, 9 * 4);
    return u(p);
}

// 0x00449830 (edi = level): stops the level stream: 0x0044ac10 (AL = 0, edi), then destroys the object at
// 0x0063a194 (virtual slot 0 with 0) and clears the global. EAX's upper bits on entry are not an input (0 here).
// Caller 0x00401710. Not executed in recorded runs.
uint32_t fn_00449830(uint8_t* level)
{
    uint32_t r = fn_0044ac10(0, level);
    void* stream = GameGlobal<void*>(0x0063a194);
    if (stream != nullptr)
    {
        r = VCall<uint32_t>(stream, 0, 0u);
        GameGlobal<uint32_t>(0x0063a194) = 0;
    }
    return r;
}

// 0x00449e80 (ecx not read; stack: chunk id, chunk data; caller pops): the level file's chunk handler (its address
// is stored as a callback at stream+8 by 0x0044ac10). Dispatches on the magic id to the loaders, each given
// [game+0xa64]+4 and the chunk data:
//   0x0beaded ObjectSystem_LoadProperties  0x0bada55 ObjectSystem_LoadTemplates  0x0b0d1e5 0x00462950
//   0x0c0ffee ObjectSystem_LoadAnims       0x0effec5 Fx_LoadChunk                0x0ffdbac Rumble_Load
//   0xaddbeef Font_Load                    0xbabebabe 0x0045d510                 0xbadf00d ObjectSystem_LoadSkins
//   0xb00b1e5 RenderSystem_LoadTextures    0xdeadc0de TextData_Load              0xfadebacc Landscape_Load
//   0xdeafdeaf virtual +0x80 of [game+0x2c] ([a64]+4, data, [0x0063a194])
// then stores the id at [[game+0xa64]] and returns 1 (the loader's result is ignored); unknown ids store it and
// return 0.
uint32_t Level_Load(uint32_t id, uint32_t data)
{
    uint8_t* g = Gm();
    const uint32_t ctx = Field<uint32_t>(Field<uint8_t*>(g, 0xa64), 4);
    switch (id)
    {
    case 0x0beaded:
        orig::fn_0045b7d0(ctx, Field<uint32_t>(g, 0xa84), data);
        break;
    case 0x0bada55:
        ObjectSystem_LoadTemplates(Field<uint32_t>(g, 0xa84), ctx, data);
        break;
    case 0x0b0d1e5:
        fn_00462950(ctx, data, Field<uint32_t>(g, 0xa70));
        break;
    case 0x0c0ffee:
        orig::fn_0045b430(Field<uint32_t>(g, 0xa64), ctx, Field<uint32_t>(g, 0xa84), data);
        break;
    case 0x0ffdbac:
        orig::fn_004e4d90(ctx, Field<uint32_t>(g, 0x30), data);
        break;
    case 0x0effec5:
        orig::fn_0041c7e0(ctx, u(g), Field<uint32_t>(g, 0xa90), data);
        break;
    case 0xaddbeef:
        Font_Load(ctx, Field<uint32_t>(g, 0xa78), data);
        break;
    case 0xbabebabe:
        fn_0045d510(ctx, data, Field<uint32_t>(g, 0xa84));
        break;
    case 0xbadf00d:
        orig::fn_0045af30(ctx, Field<uint32_t>(g, 0xa84), data);
        break;
    case 0xb00b1e5:
        orig::fn_00488400(ctx, u(g), Field<uint32_t>(g, 0xa78), data);
        break;
    case 0xdeadc0de:
        orig::fn_00496060(ctx, Field<uint32_t>(g, 0xac0), data);
        break;
    case 0xfadebacc:
        orig::fn_00448470(ctx, Field<uint32_t>(g, 0xa60), data);
        break;
    case 0xdeafdeaf:
        VCall<uint32_t>(Field<void*>(g, 0x2c), 0x80 / 4, ctx, data, GameGlobal<uint32_t>(0x0063a194));
        break;
    default:
        Field<uint32_t>(Field<uint8_t*>(Gm(), 0xa64), 0) = id;
        return 0;
    }
    Field<uint32_t>(Field<uint8_t*>(Gm(), 0xa64), 0) = id;
    return 1;
}

// 0x0044a110 (eax = level object, ecx not read; stack: level name; ret 4): starts a level. 0x004187e0 resolves the
// level's paths into +0x25c / +0x15c / +0x5c (stack: level, name, +0x360); on failure returns its AL = 0. Then:
// LoadingScreen_Show(AL = 1), PhysicsManager_init (ebx = [game+0xa8c]), 0x0044ac10 (open the level stream),
// 0x0044acc0 (load the level chunk file, AL = loaded), 0x004f59f0 (ebx = +4, edi = +0x25c), 0x004187e0 again with
// no outputs, 0x0044ad20 (release the chunk file unless it loaded), LoadingScreen_Show(AL = 0), 0x00487ff0
// ([game+0xa78]), PostProcess_CreateTargets ([game+0xa98]), 0x0044ac10 (AL = 0: close the stream), +8 = -1,
// byte +0x11 = 1, 0x0044f4c0 ([game+0xa88]) and virtuals +0x88, +0x90, +0x8c of [game+0x2c] with 0. AL = 1.
// Caller 0x00403690.
uint32_t fn_0044a110(uint8_t* level, uint32_t name)
{
    const uint32_t extra = u(level + 0x360);
    uint32_t r = orig::fn_004187e0(u(level + 0x25c), u(level + 0x15c), u(level + 0x5c), u(level), name, extra);
    if (static_cast<uint8_t>(r) == 0)
        return r;
    r = orig::fn_004499c0(Lo8(r, 1), u(level));
    r = orig::fn_00411960(Field<uint32_t>(Gm(), 0xa8c));
    r = fn_0044ac10(Lo8(r, 1), level);
    const uint32_t loaded = fn_0044acc0(level) & 0xff;
    CallEbxEdi(0x004f59f0, Field<uint32_t>(level, 4), u(level + 0x25c));
    r = orig::fn_004187e0(0, 0, 0, u(level), name, extra);
    r = fn_0044ad20(Lo8(r, loaded), level);
    r = orig::fn_004499c0(Lo8(r, 0), u(level));
    orig::fn_00487ff0(Field<uint32_t>(Gm(), 0xa78));
    r = orig::fn_0047c920(Field<uint32_t>(Gm(), 0xa98));
    fn_0044ac10(Lo8(r, 0), level);
    Field<uint32_t>(level, 8) = 0xffffffffu;
    Field<uint8_t>(level, 0x11) = 1;
    orig::fn_0044f4c0(Field<uint32_t>(Gm(), 0xa88));
    VCall<uint32_t>(Field<void*>(Gm(), 0x2c), 0x88 / 4, 0u);
    VCall<uint32_t>(Field<void*>(Gm(), 0x2c), 0x90 / 4, 0u);
    r = VCall<uint32_t>(Field<void*>(Gm(), 0x2c), 0x8c / 4, 0u);
    return Lo8(r, 1);
}

// 0x0044a9d0 (eax, ecx = level name parts for 0x004187e0's stack): checksum of the level's *.big files: resolves
// the level directory (0x004187e0, output in eax only), FindFirstFile "<dir>*.big" (0x004f1f40, string 0x005b2624
// "*.big") and for every file XORs in its size rotated left by (0x0044ad70 hash of its name) & 31 (written as
// shr (32 - n) / shl n, so n = 0 contributes 0). Returns the checksum, 0 when nothing was found. Callers 0x00418790,
// 0x0048c620. One frame array as in the original (0x450 bytes: find state at +0, file-name path +0x248, dir +0x34c).
uint32_t fn_0044a9d0(uint32_t eax, uint32_t ecx)
{
    uint8_t f[0x450];
    uint32_t sum = 0;
    if (static_cast<uint8_t>(orig::fn_004187e0(u(f + 0x34c), 0, 0, ecx, eax, 0)) == 0)
        return 0;
    MakePath(u(f + 0x248), 0, u(f + 0x34c), 0x005b2624, 0);
    if (static_cast<uint8_t>(orig::fn_004f1f40(u(f), u(f + 0x248), 0, 0, 0, 0, 0, 0)) == 0)
        return 0;
    auto* data = reinterpret_cast<WIN32_FIND_DATAA*>(f + 4);
    do
    {
        const uint32_t n = orig::fn_0044ad70(u(f + 0x30)) & 0x1f;
        const uint32_t size = Field<uint32_t>(f, 0x24);
        const uint32_t hi = size >> ((0x20 - n) & 0x1f);
        const uint32_t lo = size << n;
        sum ^= hi ^ lo;
    } while (FindNext(Field<HANDLE>(f, 0), data) != 0);
    FindClose_(Field<HANDLE>(f, 0));
    return sum;
}

// 0x0044aa80 (eax = pattern or null for 0x005b2478 "lev_*.*"; stack: unused, callback, context, byte needBig;
// ret 0x10): enumerates level directories: with the stream paused (0x0044ad50 AL = 1 / AL = 0 around it), for each
// directory entry matching the pattern (attribute 0x10) that - when needBig - contains a "*.big" file, counts it and
// calls callback(context, name) (cdecl) when given. Returns the count. Caller 0x00492c90. Not executed in recorded
// runs. One frame array as in the original (0x59c bytes: count +0, find state +8, path +0x250, inner state +0x354).
uint32_t fn_0044aa80(uint32_t pattern, uint32_t /*a0*/, uint32_t callback, uint32_t context, uint32_t needBig)
{
    uint8_t f[0x59c];
    Field<uint32_t>(f, 0) = 0;
    uint32_t r = orig::fn_0044ad50(Lo8(pattern, 1));
    if (pattern == 0)
        pattern = 0x005b2478;
    r = FindFirst(u(f + 8), u(f + 4), pattern);
    if (static_cast<uint8_t>(r) != 0)
    {
        uint32_t name = Field<uint32_t>(f, 4);
        for (;;)
        {
            if (((Field<uint32_t>(f, 0xc) >> 4) & 1) != 0)
            {
                bool counted = true;
                if (static_cast<uint8_t>(needBig) != 0)
                {
                    MakePath(u(f + 0x250), 0, name, 0x005b2624, 0);
                    if (static_cast<uint8_t>(FindFirst(u(f + 0x354), 0, u(f + 0x250))) == 0)
                        counted = false;
                    else
                        FindClose_(Field<HANDLE>(f, 0x354));
                }
                if (counted)
                {
                    Field<uint32_t>(f, 0) += 1;
                    if (callback != 0)
                        reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t)>(callback)(context, name);
                }
            }
            r = FindNext(Field<HANDLE>(f, 8), reinterpret_cast<WIN32_FIND_DATAA*>(f + 0xc));
            if (r == 0)
                break;
            name = u(f + 0x38);
        }
        r = FindClose_(Field<HANDLE>(f, 8));
    }
    orig::fn_0044ad50(Lo8(r, 0));
    return Field<uint32_t>(f, 0);
}

// 0x0044ab90 (eax, ecx = level name parts): AL = 1 when the level directory (0x004187e0) contains a "*.big" file
// (0x004f1f40 + FindClose), else AL = 0. Caller 0x00403690. Frame 0x454 bytes as in the original.
uint32_t fn_0044ab90(uint32_t eax, uint32_t ecx)
{
    uint8_t f[0x454];
    uint32_t r = orig::fn_004187e0(u(f + 4), 0, 0, ecx, eax, 0);
    if (static_cast<uint8_t>(r) == 0)
        return Lo8(r, 0);
    MakePath(u(f + 0x108), 0, u(f + 4), 0x005b2624, 0);
    r = orig::fn_004f1f40(u(f + 0x20c), u(f + 0x108), u(f), 0, 0, 0, 0, 0);
    if (static_cast<uint8_t>(r) == 0)
        return Lo8(r, 0);
    r = FindClose_(Field<HANDLE>(f, 0x20c));
    return Lo8(r, 1);
}

// 0x0044ac10 (al = open, edi = level): AL = 1 and no stream at +4: creates the level stream (0x2c bytes from
// 0x004e07f0, heap 0x0063a210, tag string 0x0058b5e8): +8 = chunk handler Level_Load (0x00449e80), +0x20 =
// 0x00449770, +0x14 = 'dflt' then the language code (Language_GetCode), +0xc = 0x004fbab0(), +0x10 = 0x00449e60,
// +0 = 0x004495b0; then 0x0044ad50 (AL = 1). AL = 0: destroys +4 (0x004f5910, free), then 0x0044ad50 (AL = 0).
// Original bug: allocation failure stores null at +4 and still writes [null+0x10]. Callers 0x00449830, 0x0044a110.
uint32_t fn_0044ac10(uint32_t eax, uint8_t* level)
{
    if (static_cast<uint8_t>(eax) != 0)
    {
        if (Field<uint32_t>(level, 4) != 0)
            return eax;
        uint8_t* s = B(orig::fn_004e07f0(0x0063a210u, 0x10, 0x2c, 2, 0x0058b5e8));
        if (s != nullptr)
        {
            Field<uint32_t>(s, 8) = 0x00449e80;
            Field<uint32_t>(s, 0x28) = 0;
            Field<uint32_t>(s, 0x24) = 0;
            Field<uint32_t>(s, 0x10) = 0;
            Field<uint32_t>(s, 0x14) = 0x64666c74;
            Field<uint8_t>(s, 0x1c) = 0;
            Field<uint32_t>(s, 0x20) = 0x00449770;
            Field<uint32_t>(s, 0) = 0;
            Field<uint32_t>(s, 0xc) = orig::fn_004fbab0();
        }
        Field<uint32_t>(level, 4) = u(s);
        *reinterpret_cast<volatile uint32_t*>(u(s) + 0x10) = 0x00449e60;  // null + 0x10 when allocation failed
        Field<uint32_t>(Field<uint8_t*>(level, 4), 0) = 0x004495b0;
        const uint32_t lang = orig::fn_00447590();
        Field<uint32_t>(Field<uint8_t*>(level, 4), 0x14) = lang;
        return orig::fn_0044ad50(Lo8(lang, 1));
    }
    const uint32_t s = Field<uint32_t>(level, 4);
    if (s == 0)
        return eax;
    orig::fn_004f5910(s);
    const uint32_t r = Free(s);
    Field<uint32_t>(level, 4) = 0;
    return orig::fn_0044ad50(Lo8(r, 0));
}

// 0x0044acc0 (esi = level): loads the level chunk file "<+0x25c><+0x5c><0x005b2470 ext>" (_makepath) with
// ChunkFile_Load (no buffer, no heap; ECX is whatever _makepath left, passed as 0 here) into +0x460; when that
// fails, +0x460 = an empty chunk-file object (0x00447bb0) and AL = 0, else AL = 1. Caller 0x0044a110.
uint32_t fn_0044acc0(uint8_t* level)
{
    uint8_t path[0x104];
    MakePath(u(path), 0, u(level + 0x25c), u(level + 0x5c), 0x005b2470);
    uint8_t ok = 1;
    uint32_t r = orig::fn_00447a00(u(path), 0, 0, 0, u(path), 0, u(level + 0x25c), u(level + 0x5c), 0x005b2470);
    Field<uint32_t>(level, 0x460) = r;
    if (r == 0)
    {
        ok = 0;
        r = fn_00447bb0(0);
        Field<uint32_t>(level, 0x460) = r;
    }
    return Lo8(r, ok);
}

// 0x0044ad20 (al = keep, edi = level): unless AL, releases the chunk file +0x460 (0x00447c70) and clears it.
// Caller 0x0044a110.
uint32_t fn_0044ad20(uint32_t eax, uint8_t* level)
{
    if (static_cast<uint8_t>(eax) != 0)
        return eax;
    const uint32_t r = fn_00447c70(Field<uint8_t*>(level, 0x460));
    Field<uint32_t>(level, 0x460) = 0;
    return r;
}

// ================================================================================================================
// 0x0045a6c0 - 0x0045bf10: object system (game+0xa84) teardown and lookups
// ================================================================================================================

namespace
{
// Item i of a chunk table t ({+0x1c count, +0x20 nonzero, [+0x24]+0x10 entry pointers}): *(entry + word[entry+2]).
// False (stop) when i >= count, +0x20 is 0 or the entry is null; `eax` gets what EAX held at that exit in the
// original loops (t, or 0 for a null entry).
bool TableItem(uint8_t* t, uint32_t i, uint8_t** item, uint32_t* eax)
{
    *eax = u(t);
    if (i >= Field<uint32_t>(t, 0x1c) || Field<uint32_t>(t, 0x20) == 0)
        return false;
    uint8_t* e = Field<uint8_t**>(Field<uint8_t*>(t, 0x24), 0x10)[i];
    if (e == nullptr)
    {
        *eax = 0;
        return false;
    }
    *item = Field<uint8_t*>(e, Field<uint16_t>(e, 2));
    return true;
}

// Releases a hash table object (0x004f11c0 frees its chains, then its bucket array +0x1c, unless byte +0xc says
// it does not own them), then the object itself. Inlined in 0x0045add0, 0x0045b250, 0x0045b6f0, 0x0045b9a0.
uint32_t ReleaseHash(uint32_t h)
{
    if (Field<uint8_t>(B(h), 0xc) == 0)
    {
        orig::fn_004f11c0(h);
        Free(Field<uint32_t>(B(h), 0x1c));
    }
    return Free(h);
}

// Releases a chunk store c: every node of the circular list at heap+0x10 (heap = [c+0xc]) gives its payload
// (node+0xc) back to the heap (0x004e1f70), then, when word c+6 is set, the buffer [c+8] goes back to [heap+0xc].
// Inlined in 0x0045add0 and 0x0045b250.
uint32_t ReleaseChunkStore(uint8_t* c)
{
    uint8_t* heap = Field<uint8_t*>(c, 0xc);
    const uint32_t buffer = Field<uint32_t>(c, 8);
    const uint32_t parent = Field<uint32_t>(heap, 0xc);
    uint8_t* head = heap + 0x10;
    uint8_t* first = Field<uint8_t*>(heap, 0x10);
    uint8_t* next = Field<uint8_t*>(first, 0);
    uint32_t r = first == head ? 0 : u(first);
    uint8_t* node = B(r);
    while (node != nullptr)
    {
        r = orig::fn_004e1f70(u(heap), u(node + 0xc));
        if (next == head)
            break;
        node = next;
        next = Field<uint8_t*>(next, 0);
    }
    if (Field<uint16_t>(c, 6) != 0)
        r = orig::fn_004e1f70(parent, buffer);
    return r;
}

// Binary search with the searcher at [t+0x24]+0x1c (count +0, compare callback +4 (cdecl, searcher), index +8,
// result +0xc, entries +0x10, key pointer +0x18), as written inline in 0x0045a860. Needs count != 0 and +0x20 > 1.
// Returns entry + word[entry+2] + 4 for a hit, else 0.
uint32_t SearchSecondary(uint8_t* t, uint32_t* key)
{
    const uint32_t count = Field<uint32_t>(t, 0x1c);
    if (count == 0 || Field<uint32_t>(t, 0x20) <= 1)
        return 0;
    uint8_t* s = Field<uint8_t*>(t, 0x24) + 0x1c;
    Field<uint32_t>(s, 0) = count;
    uint32_t hi = Field<uint32_t>(s, 0);
    uint32_t mid = hi >> 1;
    uint32_t lo = 0;
    Field<uint32_t*>(s, 0x18) = key;
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
        return 0;
    uint8_t* e = Field<uint8_t**>(s, 0x10)[Field<uint32_t>(s, 8)];
    if (e == nullptr)
        return 0;
    return u(e) + Field<uint16_t>(e, 2) + 4;
}
}  // namespace

uint32_t fn_0045add0(uint8_t* sys);
uint32_t fn_0045b250(uint8_t* sys);
uint32_t fn_0045b6f0(uint8_t* sys);
uint32_t fn_0045b9a0(uint8_t* sys);
uint32_t fn_0045bb60(uint32_t a0, uint8_t* rec);
uint32_t fn_0045d4a0(uint8_t* sys);
uint32_t fn_00462930(uint8_t* obj);
uint32_t fn_00489770(uint8_t* mode, uint32_t a0);
uint32_t RenderModes_Load(uint32_t eax, uint32_t ecx, uint32_t a0);
uint32_t fn_0045ad90(uint8_t* sys);

// 0x0045a6c0 (edi = object system): destructor body: vtable = 0x005a826c, 0x0045ad90 (unload everything), destroys
// +0xf30 (0x00403f70, free), then each of the 0x2f objects at +0xe6c and the one at +0xf28 through virtual +0x10(1)
// and clears them. Caller 0x00401710. Not executed in recorded runs.
uint32_t fn_0045a6c0(uint8_t* sys)
{
    Field<uint32_t>(sys, 0) = 0x005a826c;
    uint32_t r = fn_0045ad90(sys);
    const uint32_t f30 = Field<uint32_t>(sys, 0xf30);
    if (f30 != 0)
    {
        orig::fn_00403f70(f30);
        r = Free(f30);
    }
    Field<uint32_t>(sys, 0xf30) = 0;
    void** slot = reinterpret_cast<void**>(sys + 0xe6c);
    for (int i = 0; i < 0x2f; ++i, ++slot)
    {
        if (*slot != nullptr)
        {
            r = VCall<uint32_t>(*slot, 0x10 / 4, 1u);
            *slot = nullptr;
        }
    }
    void* last = Field<void*>(sys, 0xf28);
    if (last != nullptr)
        r = VCall<uint32_t>(last, 0x10 / 4, 1u);
    Field<uint32_t>(sys, 0xf28) = 0;
    return r;
}

// 0x0045a740 (ebx = new value, esi = old value; stack: object system; ret 4): in every object of every loaded
// group (+0xe18[0..+0xe0c), each one's chunk table [[+0x1c]+4], items' tables [+4]), for each 0x64-byte record of
// the object's +0x7c array (count +0x78) replaces any of the four dwords +0x20..+0x2c of [record+8] equal to the
// old value with the new one. guess: texture handle re-binding after a texture reload. Caller 0x00494b70.
uint32_t fn_0045a740(uint32_t newValue, uint32_t oldValue, uint8_t* sys)
{
    uint32_t k = 0;
    if (Field<uint32_t>(sys, 0xe0c) == 0)
        return 0;
    do
    {
        uint8_t* group = Field<uint8_t**>(sys, 0xe18)[k];
        uint32_t eax;
        uint8_t* item;
        for (uint32_t i = 0; TableItem(Field<uint8_t*>(Field<uint8_t*>(group, 0x1c), 4), i, &item, &eax); ++i)
        {
            uint8_t* obj;
            for (uint32_t j = 0; TableItem(Field<uint8_t*>(item, 4), j, &obj, &eax); ++j)
            {
                for (int32_t n = 0; n < Field<int32_t>(obj, 0x78); ++n)
                {
                    for (uint32_t o = 0x20; o <= 0x2c; o += 4)
                    {
                        uint8_t* rec = Field<uint8_t*>(Field<uint8_t*>(obj, 0x7c) + n * 0x64, 8);
                        if (Field<uint32_t>(rec, o) == oldValue)
                            Field<uint32_t>(rec, o) = newValue;
                    }
                }
            }
        }
        ++k;
    } while (k < Field<uint32_t>(sys, 0xe0c));
    return k;
}

// 0x0045a860 (stack: object system (unused), group, a2, byte texturesOnly; ret 0x10): (re)binds a loaded group:
// 0x004eb360 (eax = renderer [game+0x38], ecx = game; group, 1), then for every object of every item of the group's
// table: unless texturesOnly, destroys object+0 (0x004120f0, free) and reloads each render mode of +0x7c
// (RenderModes_Load, count +0x78, stride 0x64); then for each 0x2c-byte entry of +0x84 (count +0x80) with a texture
// (+0x24): unless texturesOnly 0x00489a20 (eax = texture+0x24, edi = [game+0xa78]; pass = [mode+8], [obj+0x44]+0x98,
// a2), looks the mode's word key (+0x14) up in [[mode+4]+0x70]+4 (SearchSecondary) and calls renderer virtual
// +0x184(texture, pass+0x18, pass+0x44, found). Callers Landscape_Load, 0x00448d60, ObjectSystem_LoadSkins,
// 0x0045b190.
uint32_t fn_0045a860(uint32_t /*a0*/, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint8_t* g = Gm();
    orig::fn_004eb360(Field<uint32_t>(g, 0x38), u(g), a1, 1);
    uint8_t* group = B(a1);
    uint32_t eax = 0;
    uint8_t* item;
    for (uint32_t i = 0; TableItem(Field<uint8_t*>(Field<uint8_t*>(group, 0x1c), 4), i, &item, &eax); ++i)
    {
        uint8_t* obj;
        for (uint32_t j = 0; TableItem(Field<uint8_t*>(item, 4), j, &obj, &eax); ++j)
        {
            if (static_cast<uint8_t>(a3) == 0)
            {
                const uint32_t first = Field<uint32_t>(obj, 0);
                if (first != 0)
                {
                    orig::fn_004120f0(first);
                    Free(first);
                    Field<uint32_t>(obj, 0) = 0;
                }
                for (int32_t k = 0; k < Field<int32_t>(obj, 0x78); ++k)
                {
                    const uint32_t a78 = Field<uint32_t>(Gm(), 0xa78);
                    RenderModes_Load(Field<uint32_t>(obj, 0x7c) + k * 0x64, a78, a78);
                }
            }
            for (int32_t n = 0; n < Field<int32_t>(obj, 0x80); ++n)
            {
                uint8_t* entry = Field<uint8_t*>(obj, 0x84) + n * 0x2c;
                const uint32_t texture = Field<uint32_t>(entry, 0x24);
                if (texture == 0)
                    continue;
                uint8_t* mode = Field<uint8_t*>(obj, 0x7c) + Field<uint32_t>(entry, 0x10) * 0x64;
                const uint32_t pass = Field<uint32_t>(mode, 8);
                if (static_cast<uint8_t>(a3) == 0)
                    orig::fn_00489a20(texture + 0x24, Field<uint32_t>(Gm(), 0xa78), pass,
                                      Field<uint32_t>(obj, 0x44) + 0x98, a2);
                uint32_t found = 0;
                uint32_t key = Field<uint16_t>(mode, 0x14);
                if (key != 0)
                {
                    uint8_t* owner = Field<uint8_t*>(mode, 4);
                    if (owner != nullptr)
                        found = SearchSecondary(Field<uint8_t*>(Field<uint8_t*>(owner, 0x70), 4), &key);
                }
                VCall<uint32_t>(Field<void*>(Gm(), 0x38), 0x184 / 4,
                                Field<uint32_t>(Field<uint8_t*>(obj, 0x84) + n * 0x2c, 0x24), pass + 0x18, pass + 0x44,
                                found);
            }
        }
    }
    return eax;
}

// 0x0045aad0 (al = keep; stack: object system (unused), group; ret 8): unless AL, for every object of every item of
// the group's table destroys object+0 (0x004120f0, free) and releases each render mode of +0x7c (0x00489770 with
// [game+0xa78]); then 0x004eb360 (eax = renderer, ecx = group; group, 0). Callers 0x00448c70, 0x00448d60,
// 0x0045add0, 0x0045b190.
uint32_t fn_0045aad0(uint32_t eax, uint32_t /*a0*/, uint32_t a1)
{
    if (static_cast<uint8_t>(eax) == 0)
    {
        uint8_t* group = B(a1);
        uint32_t ex;
        uint8_t* item;
        for (uint32_t i = 0; TableItem(Field<uint8_t*>(Field<uint8_t*>(group, 0x1c), 4), i, &item, &ex); ++i)
        {
            uint8_t* obj;
            for (uint32_t j = 0; TableItem(Field<uint8_t*>(item, 4), j, &obj, &ex); ++j)
            {
                const uint32_t first = Field<uint32_t>(obj, 0);
                if (first != 0)
                {
                    orig::fn_004120f0(first);
                    Free(first);
                    Field<uint32_t>(obj, 0) = 0;
                }
                for (int32_t k = 0; k < Field<int32_t>(obj, 0x78); ++k)
                    fn_00489770(Field<uint8_t*>(obj, 0x7c) + k * 0x64, Field<uint32_t>(Gm(), 0xa78));
            }
        }
    }
    return orig::fn_004eb360(Field<uint32_t>(Gm(), 0x38), a1, a1, 0);
}

// 0x0045abe0 (ecx = name key, esi = object system): looks the key up in hash +0xe38 (0x004f1160); returns
// +0xe40 + index * 0x50 when found with index >= 0, else 0. Callers 0x0040d390, 0x00412140, 0x0045f020, ...
uint32_t fn_0045abe0(uint32_t key, uint8_t* sys)
{
    const int32_t* r = reinterpret_cast<const int32_t*>(orig::fn_004f1160(Field<uint32_t>(sys, 0xe38), key));
    if (r == nullptr || *r < 0)
        return 0;
    return Field<uint32_t>(sys, 0xe40) + static_cast<uint32_t>(*r) * 0x50;
}

// 0x0045ac10 (eax = 1-based index, ecx = object system): the 0x50-byte record +0xe40[index - 1] when
// 1 <= index <= +0xe34 (unsigned), else 0. Caller 0x0048cb30.
uint32_t fn_0045ac10(uint32_t index, uint8_t* sys)
{
    if (index == 0 || index > Field<uint32_t>(sys, 0xe34))
        return 0;
    return Field<uint32_t>(sys, 0xe40) + index * 0x50 - 0x50;
}

// 0x0045ac30 (ecx = object system, esi = record): the 1-based index of the record in +0xe40 (count +0xe34, stride
// 0x50), 0 when null or not found. Caller 0x0048d0f0.
uint32_t fn_0045ac30(uint8_t* sys, uint32_t record)
{
    uint32_t i = 0;
    if (record == 0)
        return 0;
    const uint32_t count = Field<uint32_t>(sys, 0xe34);
    if (count == 0)
        return 0;
    uint32_t p = Field<uint32_t>(sys, 0xe40);
    do
    {
        ++i;
        if (record == p)
            return i;
        p += 0x50;
    } while (i < count);
    return 0;
}

// 0x0045ac60 (edx = object system, esi = value): index of the value in the dword array +0xe2c (count +0xe20), or
// -1. Caller 0x0048bda0. Not executed in recorded runs.
uint32_t fn_0045ac60(uint8_t* sys, uint32_t value)
{
    const uint32_t count = Field<uint32_t>(sys, 0xe20);
    const uint32_t* a = Field<uint32_t*>(sys, 0xe2c);
    for (uint32_t i = 0; i < count; ++i)
        if (a[i] == value)
            return i;
    return 0xffffffffu;
}

// 0x0045ac90 (eax = index, ecx = object system): 0 for -1, else +0xe2c[index]. Caller 0x0048c0f0.
uint32_t fn_0045ac90(uint32_t index, uint8_t* sys)
{
    if (index == 0xffffffffu)
        return 0;
    return Field<uint32_t*>(sys, 0xe2c)[index];
}

// 0x0045acb0 (ecx = offset; stack: object system; ret 4): 0 for 0, else +0xe30 + offset (offset to pointer).
// Caller 0x0048c0f0.
uint32_t fn_0045acb0(uint32_t offset, uint8_t* sys)
{
    if (offset == 0)
        return 0;
    return Field<uint32_t>(sys, 0xe30) + offset;
}

// 0x0045acd0 (eax = pointer, ecx = object system): 0 for null, else pointer - +0xe30 (inverse of 0x0045acb0).
// Caller 0x0048bda0. Not executed in recorded runs.
uint32_t fn_0045acd0(uint32_t p, uint8_t* sys)
{
    if (p == 0)
        return 0;
    return p - Field<uint32_t>(sys, 0xe30);
}

// 0x0045ace0 (ecx = name key, esi = object system): looks the key up in hash +0xe24 (0x004f1160); returns
// +0xe2c[index] when found with index >= 0, else 0. Callers 0x0042c410, 0x00453c50, 0x0045bbe0, ...
uint32_t fn_0045ace0(uint32_t key, uint8_t* sys)
{
    const int32_t* r = reinterpret_cast<const int32_t*>(orig::fn_004f1160(Field<uint32_t>(sys, 0xe24), key));
    if (r == nullptr || *r < 0)
        return 0;
    return Field<uint32_t*>(sys, 0xe2c)[*r];
}

// 0x0045ad90 (eax = object system): unloads it: 0x0045d4a0, 0x00462930 (esi = [game+0xa70]) when set,
// 0x0045b9a0, 0x0045b6f0, 0x0045add0, 0x0045b250. Callers 0x0044a670, 0x0045a6c0.
uint32_t fn_0045ad90(uint8_t* sys)
{
    fn_0045d4a0(sys);
    uint8_t* a70 = Field<uint8_t*>(Gm(), 0xa70);
    if (a70 != nullptr)
        fn_00462930(a70);
    fn_0045b9a0(sys);
    fn_0045b6f0(sys);
    fn_0045add0(sys);
    return fn_0045b250(sys);
}

// 0x0045add0 (stack: object system; ret 4): frees the group data: hash +0xe10 (ReleaseHash), +0xe14, every group
// of +0xe18 (count +0xe0c: 0x0045aad0 with AL = 0, then ReleaseChunkStore), the +0xe18 array (count reset), +0xe1c.
// Caller 0x0045ad90.
uint32_t fn_0045add0(uint8_t* sys)
{
    uint32_t r = 0;
    const uint32_t hash = Field<uint32_t>(sys, 0xe10);
    if (hash != 0)
    {
        r = ReleaseHash(hash);
        Field<uint32_t>(sys, 0xe10) = 0;
    }
    if (Field<uint32_t>(sys, 0xe14) != 0)
    {
        r = Free(Field<uint32_t>(sys, 0xe14));
        Field<uint32_t>(sys, 0xe14) = 0;
    }
    if (Field<uint32_t>(sys, 0xe18) != 0)
    {
        for (uint32_t i = 0; i < Field<uint32_t>(sys, 0xe0c); ++i)
        {
            r = fn_0045aad0(0, u(sys), Field<uint32_t*>(sys, 0xe18)[i]);
            uint8_t* c = Field<uint8_t**>(sys, 0xe18)[i];
            if (c != nullptr)
                r = ReleaseChunkStore(c);
        }
        if (Field<uint32_t>(sys, 0xe18) != 0)
        {
            r = Free(Field<uint32_t>(sys, 0xe18));
            Field<uint32_t>(sys, 0xe18) = 0;
        }
        Field<uint32_t>(sys, 0xe0c) = 0;
    }
    if (Field<uint32_t>(sys, 0xe1c) != 0)
    {
        r = Free(Field<uint32_t>(sys, 0xe1c));
        Field<uint32_t>(sys, 0xe1c) = 0;
    }
    return r;
}

// 0x0045b190 (eax = object system, bl = rebind; stack: byte unbind; ret 4): for every group of +0xe18: when
// unbind, 0x0045aad0 (AL = 1); when rebind, 0x0045a860(system, group, 0, 1); then 0x00448d60 (AL = unbind,
// esi = landscape [game+0xa60]; ebx). Callers 0x00401070, 0x004010c0. Not executed in recorded runs.
uint32_t fn_0045b190(uint8_t* sys, uint32_t ebx, uint32_t a0)
{
    uint32_t eax = Field<uint32_t>(sys, 0xe0c);
    for (uint32_t i = 0; i < Field<uint32_t>(sys, 0xe0c); ++i)
    {
        if (static_cast<uint8_t>(a0) != 0)
            fn_0045aad0(Lo8(Field<uint32_t>(sys, 0xe18), 1), u(sys), Field<uint32_t*>(sys, 0xe18)[i]);
        if (static_cast<uint8_t>(ebx) != 0)
            fn_0045a860(u(sys), Field<uint32_t*>(sys, 0xe18)[i], 0, 1);
        eax = Field<uint32_t>(sys, 0xe0c);
    }
    return fn_00448d60(Lo8(eax, a0), Field<uint8_t*>(Gm(), 0xa60), ebx);
}

// 0x0045b250 (stack: object system; ret 4): frees the template data: for every entry of +0xe2c (count +0xe20) the
// 12-byte slots of each sub-table of [entry+0x24] (dword pointers at +0xc.., counts +8) and the entry's chunk store
// (ReleaseChunkStore); then the +0xe2c array (count reset), +0xe30, hash +0xe24 (ReleaseHash), +0xe28.
// Caller 0x0045ad90.
uint32_t fn_0045b250(uint8_t* sys)
{
    uint32_t r = 0;
    if (Field<uint32_t>(sys, 0xe2c) != 0)
    {
        for (uint32_t i = 0; i < Field<uint32_t>(sys, 0xe20); ++i)
        {
            uint8_t* t = Field<uint8_t*>(Field<uint8_t**>(sys, 0xe2c)[i], 0x24);
            if (t != nullptr && Field<uint32_t>(t, 8) != 0)
            {
                for (uint32_t j = 0; j < Field<uint32_t>(Field<uint8_t*>(Field<uint8_t**>(sys, 0xe2c)[i], 0x24), 8);
                     ++j)
                {
                    uint8_t* sub =
                        Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t**>(sys, 0xe2c)[i], 0x24), 0xc + j * 4);
                    uint32_t* slot = reinterpret_cast<uint32_t*>(sub + 0xc);
                    for (uint32_t k = 0; k < Field<uint32_t>(sub, 8); ++k, slot += 3)
                    {
                        if (*slot != 0)
                        {
                            r = Free(*slot);
                            *slot = 0;
                        }
                    }
                }
            }
            uint8_t* c = Field<uint8_t**>(sys, 0xe2c)[i];
            if (c != nullptr)
                r = ReleaseChunkStore(c);
        }
        if (Field<uint32_t>(sys, 0xe2c) != 0)
        {
            r = Free(Field<uint32_t>(sys, 0xe2c));
            Field<uint32_t>(sys, 0xe2c) = 0;
        }
        Field<uint32_t>(sys, 0xe20) = 0;
    }
    if (Field<uint32_t>(sys, 0xe30) != 0)
    {
        r = Free(Field<uint32_t>(sys, 0xe30));
        Field<uint32_t>(sys, 0xe30) = 0;
    }
    const uint32_t hash = Field<uint32_t>(sys, 0xe24);
    if (hash != 0)
    {
        r = ReleaseHash(hash);
        Field<uint32_t>(sys, 0xe24) = 0;
    }
    if (Field<uint32_t>(sys, 0xe28) != 0)
    {
        r = Free(Field<uint32_t>(sys, 0xe28));
        Field<uint32_t>(sys, 0xe28) = 0;
    }
    return r;
}

// 0x0045b6f0 (esi = object system): frees the hashes of the 12-byte records of +0xe58 (count +0xe48, ReleaseHash,
// slots not cleared), resets the count, then hash +0xe4c, +0xe54 and +0xe58. Caller 0x0045ad90.
uint32_t fn_0045b6f0(uint8_t* sys)
{
    uint32_t r = Field<uint32_t>(sys, 0xe48);
    for (uint32_t i = 0; i < Field<uint32_t>(sys, 0xe48); ++i)
    {
        const uint32_t h = Field<uint32_t>(Field<uint8_t*>(sys, 0xe58) + i * 0xc, 0);
        if (h != 0)
            r = ReleaseHash(h);
    }
    Field<uint32_t>(sys, 0xe48) = 0;
    const uint32_t h = Field<uint32_t>(sys, 0xe4c);
    if (h != 0)
    {
        r = ReleaseHash(h);
        Field<uint32_t>(sys, 0xe4c) = 0;
    }
    if (Field<uint32_t>(sys, 0xe54) != 0)
    {
        r = Free(Field<uint32_t>(sys, 0xe54));
        Field<uint32_t>(sys, 0xe54) = 0;
    }
    if (Field<uint32_t>(sys, 0xe58) != 0)
    {
        r = Free(Field<uint32_t>(sys, 0xe58));
        Field<uint32_t>(sys, 0xe58) = 0;
    }
    return r;
}

// 0x0045b9a0 (ecx not read, edi = object system): frees the 0x50-byte records of +0xe40 (count +0xe34): each one's
// cells (0x0045bb60), +0x48 (cleared), +0xc, and the hash at [+0x10] (ReleaseHash); then +0xe40 (count reset),
// +0xe44, hash +0xe38, +0xe3c, each object of +0xe60 (0x004ed1e0, count +0xe68), +0xe60 and +0xe64, +0xe68 = 0.
// Caller 0x0045ad90.
uint32_t fn_0045b9a0(uint8_t* sys)
{
    uint32_t r = 0;
    if (Field<uint32_t>(sys, 0xe40) != 0)
    {
        for (uint32_t i = 0; i < Field<uint32_t>(sys, 0xe34); ++i)
        {
            const uint32_t off = i * 0x50;
            fn_0045bb60(u(sys), Field<uint8_t*>(sys, 0xe40) + off);
            const uint32_t p48 = Field<uint32_t>(Field<uint8_t*>(sys, 0xe40) + off, 0x48);
            if (p48 != 0)
            {
                Free(p48);
                Field<uint32_t>(Field<uint8_t*>(sys, 0xe40) + off, 0x48) = 0;
            }
            Free(Field<uint32_t>(Field<uint8_t*>(sys, 0xe40) + off, 0xc));
            const uint32_t h = Field<uint32_t>(Field<uint8_t*>(Field<uint8_t*>(sys, 0xe40) + off, 0x10), 0);
            if (h != 0)
                r = ReleaseHash(h);
        }
        if (Field<uint32_t>(sys, 0xe40) != 0)
        {
            r = Free(Field<uint32_t>(sys, 0xe40));
            Field<uint32_t>(sys, 0xe40) = 0;
        }
        Field<uint32_t>(sys, 0xe34) = 0;
    }
    if (Field<uint32_t>(sys, 0xe44) != 0)
    {
        r = Free(Field<uint32_t>(sys, 0xe44));
        Field<uint32_t>(sys, 0xe44) = 0;
    }
    const uint32_t h = Field<uint32_t>(sys, 0xe38);
    if (h != 0)
    {
        r = ReleaseHash(h);
        Field<uint32_t>(sys, 0xe38) = 0;
    }
    if (Field<uint32_t>(sys, 0xe3c) != 0)
    {
        r = Free(Field<uint32_t>(sys, 0xe3c));
        Field<uint32_t>(sys, 0xe3c) = 0;
    }
    for (uint32_t i = 0; i < Field<uint32_t>(sys, 0xe68); ++i)
    {
        const uint32_t o = Field<uint32_t*>(sys, 0xe60)[i];
        if (o != 0)
            r = orig::fn_004ed1e0(o);
    }
    if (Field<uint32_t>(sys, 0xe60) != 0)
    {
        r = Free(Field<uint32_t>(sys, 0xe60));
        Field<uint32_t>(sys, 0xe60) = 0;
    }
    if (Field<uint32_t>(sys, 0xe64) != 0)
    {
        r = Free(Field<uint32_t>(sys, 0xe64));
        Field<uint32_t>(sys, 0xe64) = 0;
    }
    Field<uint32_t>(sys, 0xe68) = 0;
    return r;
}

// 0x0045bb60 (stack: object system (unused), record; ret 8): frees the cells of the record's grid [+0x10]
// (+0x24 * +0x28 cells at [+0x38]): each cell's +0x18 pointers (count +0x14) and the cell, through the free hook
// at 0x005cacc4 (cdecl), and clears the slot. Caller 0x0045b9a0.
uint32_t fn_0045bb60(uint32_t /*a0*/, uint8_t* rec)
{
    auto freeHook = GameGlobal<void(__cdecl*)(uint32_t)>(0x005cacc4);
    uint8_t* grid = Field<uint8_t*>(rec, 0x10);
    uint32_t b = 0;
    if (Field<uint32_t>(grid, 0x28) * Field<uint32_t>(grid, 0x24) == 0)
        return u(grid);
    do
    {
        uint8_t* cell = Field<uint8_t**>(Field<uint8_t*>(rec, 0x10), 0x38)[b];
        if (cell != nullptr)
        {
            const uint32_t n = Field<uint32_t>(cell, 0x14);
            uint32_t* p = reinterpret_cast<uint32_t*>(cell + 0x18);
            for (uint32_t c = 0; c < n; ++c, ++p)
            {
                if (*p != 0)
                    GameGlobal<void(__cdecl*)(uint32_t)>(0x005cacc4)(*p);
            }
            freeHook(u(cell));
            Field<uint8_t**>(Field<uint8_t*>(rec, 0x10), 0x38)[b] = nullptr;
        }
        ++b;
        grid = Field<uint8_t*>(rec, 0x10);
    } while (b < Field<uint32_t>(grid, 0x28) * Field<uint32_t>(grid, 0x24));
    return u(grid);
}

// 0x0045bf10 (eax = object, edx = index; stack: 1 dword, never read; ret 4): for the cell [[eax+0x10]+0x38][index]
// (when [+0x10]+0x28 is nonzero) whose +0xc is set, builds a key from +0xc+0x34 into a 0x40-byte buffer
// (0x004588f0) and looks it up in hash [[eax+0x10]] (0x004f1160); returns the found index, else -1.
// Callers 0x00458950, ObjectSystem_LoadTemplates.
uint32_t fn_0045bf10(uint8_t* obj, uint32_t index, uint32_t /*a0*/)
{
    uint8_t buffer[0x40];
    buffer[0] = 0;
    uint8_t* t = Field<uint8_t*>(obj, 0x10);
    if (Field<uint32_t>(t, 0x28) == 0)
        return 0xffffffffu;
    uint8_t* cell = Field<uint8_t**>(t, 0x38)[index];
    if (cell == nullptr)
        return 0xffffffffu;
    uint8_t* src = Field<uint8_t*>(cell, 0xc);
    if (src == nullptr)
        return 0xffffffffu;
    orig::fn_004588f0(u(src + 0x34), u(buffer));
    const uint32_t* r = reinterpret_cast<const uint32_t*>(orig::fn_004f1160(Field<uint32_t>(t, 0), u(buffer)));
    if (r == nullptr)
        return 0xffffffffu;
    return *r;
}

// ================================================================================================================
// 0x0045d4a0 - 0x004888b0: per-level data blocks, texture bank, font
// ================================================================================================================

uint32_t fn_0045d4e0(uint8_t* sys);
uint32_t TextureBank_Find(uint32_t key, uint8_t* bank);
uint32_t fn_00488880(uint32_t a0);

// 0x0045d4a0 (eax = object system): 0x0045d670(system, -1), 0x0045d4e0, frees +0xe5c (instance data) and clears it.
// Caller 0x0045ad90.
uint32_t fn_0045d4a0(uint8_t* sys)
{
    orig::fn_0045d670(u(sys), 0xffffffffu);
    fn_0045d4e0(sys);
    uint32_t r = Field<uint32_t>(sys, 0xe5c);
    if (r != 0)
        r = Free(r);
    Field<uint32_t>(sys, 0xe5c) = 0;
    return r;
}

// 0x0045d4e0 (ebx = object system): virtual +0x30 (no arguments) of each of the 0x2f objects at +0xe6c that is set,
// then a tail jump to virtual +0x30 of +0xf28 (not null-checked), reproduced as call-then-return (no stack
// arguments: the frame is intact). Caller 0x0045d4a0.
uint32_t fn_0045d4e0(uint8_t* sys)
{
    void** slot = reinterpret_cast<void**>(sys + 0xe6c);
    for (int i = 0; i < 0x2f; ++i, ++slot)
    {
        if (*slot != nullptr)
            VCall<uint32_t>(*slot, 0x30 / 4);
    }
    return VCall<uint32_t>(Field<void*>(sys, 0xf28), 0x30 / 4);
}

namespace
{
// The chunk stream of a level load context (ctx+0x28): virtual +4 reads (size, destination) unless flag bit 6 of
// +0x10 (error) is already set; when the error bit is set afterwards, the context's error callback [ctx] (cdecl)
// gets -1. Written inline in every chunk loader (0x0045d510, 0x00462950, Font_Load, ...). Returns EAX as left.
uint32_t ChunkRead(uint8_t* ctx, uint32_t size, uint32_t dest, uint32_t eax)
{
    void* stream = Field<void*>(ctx, 0x28);
    if (stream == nullptr || ((Field<uint32_t>(stream, 0x10) >> 6) & 1) != 0)
        return eax;
    VCall<uint32_t>(stream, 4 / 4, size, dest);
    eax = Field<uint32_t>(ctx, 0x28);
    if (((Field<uint32_t>(Field<uint8_t*>(ctx, 0x28), 0x10) >> 6) & 1) == 0)
        return eax;
    const uint32_t onError = Field<uint32_t>(ctx, 0);
    if (onError == 0)
        return eax;
    return reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(onError)(0xffffffffu);
}
}  // namespace

// 0x0045d510 (eax = load context, edi = chunk size; stack: object system; ret 4): chunk 0xbabebabe: allocates
// size bytes (0x004e07f0, heap 0x0063a210, args 0xf, size, 2, "instance data" 0x005b0864) into system+0xe5c and
// reads the chunk into it. Caller Level_Load.
uint32_t fn_0045d510(uint32_t eax, uint32_t edi, uint32_t a0)
{
    const uint32_t p = orig::fn_004e07f0(0x0063a210u, 0xf, edi, 2, 0x005b0864);
    Field<uint32_t>(B(a0), 0xe5c) = p;
    return ChunkRead(B(eax), edi, p, p);
}

// 0x00462930 (esi = UI system [game+0xa70]): frees +0x48 (ui data) and clears it. Caller 0x0045ad90.
uint32_t fn_00462930(uint8_t* obj)
{
    uint32_t r = Field<uint32_t>(obj, 0x48);
    if (r != 0)
        r = Free(r);
    Field<uint32_t>(obj, 0x48) = 0;
    return r;
}

// 0x00462950 (eax = load context, edi = chunk size; stack: UI system [game+0xa70]; ret 4): chunk 0xb0d1e5:
// allocates size bytes (0x004e07f0 args 7, size, 2, "ui data" 0x005a0ea0) into +0x48 and reads the chunk into it.
// Caller Level_Load.
uint32_t fn_00462950(uint32_t eax, uint32_t edi, uint32_t a0)
{
    const uint32_t p = orig::fn_004e07f0(0x0063a210u, 7, edi, 2, 0x005a0ea0);
    Field<uint32_t>(B(a0), 0x48) = p;
    return ChunkRead(B(eax), edi, p, p);
}

// 0x00488180 (ecx = name key, esi = texture bank, edi = texture or null): looks the key up in hash +0xc
// (0x004f1160); when found with index >= 0 sets slot +0x1c[index] = edi, or the bank's own record +0x18 + index *
// 0x24 when edi is null. Returns the index (or what EAX held on a miss). Caller 0x00494b70.
uint32_t fn_00488180(uint32_t key, uint8_t* bank, uint32_t texture)
{
    const uint32_t hash = Field<uint32_t>(bank, 0xc);
    if (hash == 0)
        return 0;
    const int32_t* r = reinterpret_cast<const int32_t*>(orig::fn_004f1160(hash, key));
    if (r == nullptr)
        return 0;
    const int32_t index = *r;
    if (index < 0)
        return static_cast<uint32_t>(index);
    if (texture == 0)
        Field<uint32_t*>(bank, 0x1c)[index] = Field<uint32_t>(bank, 0x18) + index * 0x24;
    else
        Field<uint32_t*>(bank, 0x1c)[index] = texture;
    return static_cast<uint32_t>(index);
}

// 0x004881c0 (ecx = name key, esi = texture bank; no stack arguments are read although symbols/abi.csv lists 8):
// the texture +0x1c[index] for the key found in hash +0xc (0x004f1160) with index >= 0, else 0. Many callers.
uint32_t TextureBank_Find(uint32_t key, uint8_t* bank)
{
    const uint32_t hash = Field<uint32_t>(bank, 0xc);
    if (hash == 0)
        return 0;
    const int32_t* r = reinterpret_cast<const int32_t*>(orig::fn_004f1160(hash, key));
    if (r == nullptr || *r < 0)
        return 0;
    return Field<uint32_t*>(bank, 0x1c)[*r];
}

// 0x00488880 (stack: 1 dword, never read; ret 4): renderer [game+0x38] virtual +4(0, 0x0059da34); when it returns
// AL = 0, LoadingScreen_ErrorLoop(-1) (0x004495b0, does not return). Callers 0x004039f0, Font_Load.
uint32_t fn_00488880(uint32_t /*a0*/)
{
    const uint32_t r = VCall<uint32_t>(Field<void*>(Gm(), 0x38), 4 / 4, 0u, 0x0059da34u);
    if (static_cast<uint8_t>(r) == 0)
        return orig::fn_004495b0(0xffffffffu);
    return r;
}

// 0x004888b0 (eax = load context, ecx = render system [game+0xa78]; stack: chunk size; ret 4): chunk 0xaddbeef:
// reads a 0x20-byte header (texture name), allocates size bytes into +4 (0x004e07f0 args 1, size, 2, "Font Data"
// 0x0059da28), finds the texture by name (TextureBank_Find) into +8 and reads the remaining size - 0x20 bytes into
// +4. With a texture, renderer virtual +4(&+4, 0) (guess: builds the font); without, 0x00488880 (error).
uint32_t Font_Load(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    uint8_t header[0x20];
    uint8_t* ctx = B(eax);
    uint8_t* sys = B(ecx);
    ChunkRead(ctx, 0x20, u(header), 0);
    uint32_t* data = reinterpret_cast<uint32_t*>(sys + 4);
    *data = orig::fn_004e07f0(0x0063a210u, 1, a0, 2, 0x0059da28);
    Field<uint32_t>(sys, 8) = TextureBank_Find(u(header), sys);
    ChunkRead(ctx, a0 - 0x20, *data, 0);
    if (Field<uint32_t>(sys, 8) != 0)
        return VCall<uint32_t>(Field<void*>(Gm(), 0x38), 4 / 4, u(data), 0u);
    return fn_00488880(u(sys));
}

// ================================================================================================================
// 0x0046ed30: path grid
// ================================================================================================================

namespace
{
// SearchSecondary, but returning the raw entry (0 when the search fails), for callers that dereference it unchecked.
uint32_t SearchSecondaryEntry(uint8_t* t, uint32_t* key)
{
    const uint32_t count = Field<uint32_t>(t, 0x1c);
    if (count == 0 || Field<uint32_t>(t, 0x20) <= 1)
        return 0;
    uint8_t* s = Field<uint8_t*>(t, 0x24) + 0x1c;
    Field<uint32_t*>(s, 0x18) = key;
    uint32_t hi = count;
    uint32_t mid = hi >> 1;
    uint32_t lo = 0;
    Field<uint32_t>(s, 0) = count;
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
        return 0;
    return Field<uint32_t*>(s, 0x10)[Field<uint32_t>(s, 8)];
}

// __ftol2 (0x0056a7c8): truncates to a 64-bit integer; callers use EAX.
inline int32_t Ftol(float x)
{
    return static_cast<int32_t>(static_cast<int64_t>(x));
}

// x snapped to the grid of 10 as 0x0046ed30 writes it: x - (unsigned)trunc(x) % 10 for x >= 0, else
// x - (10 - (unsigned)trunc(-x) % 10). (The 2^32 correction after fild is dead: the remainder is never negative.)
float SnapTen(float x)
{
    if (!(x < 0.0f))
    {
        const uint32_t rem = static_cast<uint32_t>(Ftol(x)) % 10u;
        return x - static_cast<float>(static_cast<int32_t>(rem));
    }
    const uint32_t rem = static_cast<uint32_t>(Ftol(-x)) % 10u;
    const float t = GameGlobal<float>(0x005bcb68) - static_cast<float>(static_cast<int32_t>(rem));
    return x - t;
}
}  // namespace

// 0x0046ed30 (stack: path grid; ret 4): builds the pathing grid of the landscape [game+0xa60]:
//   +0xc / +0x10 = landscape +0x14 / +0x1c snapped to multiples of 10 (SnapTen), +0x14 / +0x18 = landscape +0x18 /
//   +0x20, +4 / +8 = 1 - ftol((max - min) * -0.1) (cells across / down); +0 = a zeroed bit grid of
//   (w * h >> 2) + 1 bytes (0x004e07f0 args 0x14, n, 2, "pathing grid"); a second one is scratch.
//   For every object of every item of the landscape's group table (+0xefc), for each 0x2c-byte entry of +0x84:
//   triangles count = [[+0x24]+0x10] - 2 (strip) or [+0x20] / 3; the material name (key word +0x14 of the entry's
//   render mode, looked up in [[mode+4]+0x70]+4) containing "nonpath_"/"_nonpath" or "path_"/"_path" sets flags;
//   each triangle (0x004efb50) is rasterized by PathGrid_RasterizeTriangle (0x0046ea00, ebx = grid; tri, scratch grid,
//   path, nonpath) when flagged, else only when its plane normal (Plane_FromPoints) is unit length (0.99 <= len
//   <= 1.001) and its z is below 0.64278 (steeper than 50 degrees), with flags 0.
//   Then +0x24 "nodelist" (0xbb80 bytes), +0x20 "hashtable" (0x3ec), +0x38 "pqheap" (0x1f44); frees the scratch.
// Original bug: a material key that is not found makes the name lookup read [0 + 2] (no null check).
// Caller Landscape_Load (0x00448470). Frame 0x60 bytes kept as one array (offsets as in the original).
uint32_t PathGrid_Build(uint8_t* grid)
{
    uint8_t f[0x60];
    uint8_t* land = Field<uint8_t*>(Gm(), 0xa60);
    Field<float>(grid, 0xc) = SnapTen(Field<float>(land, 0x14));
    land = Field<uint8_t*>(Gm(), 0xa60);
    Field<float>(grid, 0x10) = SnapTen(Field<float>(land, 0x1c));
    Field<uint32_t>(grid, 0x14) = Field<uint32_t>(Field<uint8_t*>(Gm(), 0xa60), 0x18);
    Field<uint32_t>(grid, 0x18) = Field<uint32_t>(Field<uint8_t*>(Gm(), 0xa60), 0x20);
    const float k = GameGlobal<float>(0x005bcd14);
    const int32_t w = 1 - Ftol((Field<float>(grid, 0x14) - Field<float>(grid, 0xc)) * k);
    Field<int32_t>(grid, 4) = w;
    const int32_t h = 1 - Ftol((Field<float>(grid, 0x18) - Field<float>(grid, 0x10)) * k);
    Field<int32_t>(grid, 8) = h;
    uint32_t n = ((static_cast<uint32_t>(h) * static_cast<uint32_t>(w)) >> 2) + 1;
    uint8_t* bits = B(orig::fn_004e07f0(0x0063a210u, 0x14, n, 2, 0x005a03cc));
    Field<uint32_t>(grid, 0) = u(bits);
    std::memset(bits, 0, ((Field<uint32_t>(grid, 8) * Field<uint32_t>(grid, 4)) >> 2) + 1);
    n = ((Field<uint32_t>(grid, 8) * Field<uint32_t>(grid, 4)) >> 2) + 1;
    uint8_t* scratch = B(orig::fn_004e07f0(0x0063a210u, 0x14, n, 2, 0x005a03cc));
    Field<uint32_t>(f, 0x10) = u(scratch);
    std::memset(scratch, 0, ((Field<uint32_t>(grid, 8) * Field<uint32_t>(grid, 4)) >> 2) + 1);

    uint32_t ex;
    uint8_t* item;
    for (uint32_t i = 0;
         TableItem(Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(Gm(), 0xa60), 0xefc), 0x1c), 4), i,
                   &item, &ex);
         ++i)
    {
        uint8_t* obj;
        for (uint32_t j = 0; TableItem(Field<uint8_t*>(item, 4), j, &obj, &ex); ++j)
        {
            for (int32_t e = 0; e < Field<int32_t>(obj, 0x80); ++e)
            {
                uint8_t* entry = Field<uint8_t*>(obj, 0x84) + e * 0x2c;
                uint32_t count;
                if (Field<uint32_t>(entry, 0x24) != 0)
                    count = Field<uint32_t>(Field<uint8_t*>(entry, 0x24), 0x10) - 2;
                else
                    count = static_cast<uint32_t>(Field<int32_t>(entry, 0x20) / 3);
                Field<uint32_t>(f, 8) = count;
                Field<uint8_t>(f, 4) = 0;  // nonpath
                Field<uint8_t>(f, 0) = 0;  // path
                const int32_t m = Field<int32_t>(entry, 0x10);
                if (m >= 0)
                {
                    uint8_t* mode = Field<uint8_t*>(obj, 0x7c) + m * 0x64;
                    const uint32_t key = Field<uint16_t>(mode, 0x14);
                    if (key != 0)
                    {
                        Field<uint32_t>(f, 0x28) = key;
                        const uint32_t hit = SearchSecondaryEntry(
                            Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(mode, 4), 0x70), 4),
                            reinterpret_cast<uint32_t*>(f + 0x28));
                        const uint32_t word = *reinterpret_cast<volatile uint16_t*>(hit + 2);
                        const char* name = reinterpret_cast<const char*>(hit + word + 4);
                        auto strstr_ = GameFunction<const char*(__cdecl*)(const char*, const char*)>(0x005383d0);
                        if (strstr_(name, reinterpret_cast<const char*>(0x005a03c0)) != nullptr)
                            Field<uint8_t>(f, 4) = 1;
                        else if (strstr_(name, reinterpret_cast<const char*>(0x005a03b4)) != nullptr)
                            Field<uint8_t>(f, 4) = 1;
                        else if (strstr_(name, reinterpret_cast<const char*>(0x005a03ac)) != nullptr ||
                                 strstr_(name, reinterpret_cast<const char*>(0x005a03a4)) != nullptr)
                            Field<uint8_t>(f, 0) = 1;
                    }
                }
                const uint32_t path = Field<uint8_t>(f, 0);
                for (uint32_t t = 0; t < Field<uint32_t>(f, 8); ++t)
                {
                    uint8_t* tri = f + 0x3c;
                    if (static_cast<uint8_t>(orig::fn_004efb50(u(Field<uint8_t*>(obj, 0x84) + e * 0x2c), t, u(tri))) ==
                        0)
                        continue;
                    if (Field<uint8_t>(f, 4) != 0 || Field<uint8_t>(f, 0) != 0)
                    {
                        orig::fn_0046ea00(u(grid), u(tri), Field<uint32_t>(f, 0x10), path, Field<uint8_t>(f, 4));
                        continue;
                    }
                    orig::fn_004e80b0(u(f + 0x54), u(f + 0x3c), u(f + 0x48), u(f + 0x2c));
                    const float nx = Field<float>(f, 0x2c);
                    const float ny = Field<float>(f, 0x30);
                    const float nz = Field<float>(f, 0x34);
                    const float len = std::sqrt(nz * nz + ny * ny + nx * nx);
                    if (len < GameGlobal<float>(0x005bccf8))
                        continue;
                    if (!(len <= GameGlobal<float>(0x005bcd10)))
                        continue;
                    if (!(Field<float>(f, 0x34) < GameGlobal<float>(0x005bcd0c)))
                        continue;
                    orig::fn_0046ea00(u(grid), u(tri), Field<uint32_t>(f, 0x10), 0, 0);
                }
            }
        }
    }
    Field<uint32_t>(grid, 0x24) = orig::fn_004e07f0(0x0063a210u, 0x14, 0xbb80, 2, 0x005a0398);
    Field<uint32_t>(grid, 0x20) = orig::fn_004e07f0(0x0063a210u, 0x14, 0x3ec, 2, 0x005a038c);
    Field<uint32_t>(grid, 0x38) = orig::fn_004e07f0(0x0063a210u, 0x14, 0x1f44, 2, 0x005a0384);
    return Free(Field<uint32_t>(f, 0x10));
}

// ================================================================================================================
// 0x00489630 - 0x00489b40: render modes (0x64-byte material records of an object, +8 = 0x58-byte render state)
// ================================================================================================================

// 0x00489630 (eax = key, ecx = owner): looks the key up in the secondary searcher of the owner's table [ecx+4]
// (searcher [+0x24]+0x1c via 0x004e0f40; needs count +0x1c != 0 and +0x20 > 1); returns entry + word[entry+2] + 4
// (the entry's name), else 0. Callers 0x004122f0, 0x00489680.
uint32_t fn_00489630(uint32_t key, uint8_t* owner)
{
    uint32_t k = key;
    uint8_t* t = Field<uint8_t*>(owner, 4);
    const uint32_t count = Field<uint32_t>(t, 0x1c);
    if (count == 0 || Field<uint32_t>(t, 0x20) <= 1)
        return 0;
    uint8_t* s = Field<uint8_t*>(t, 0x24) + 0x1c;
    Field<uint32_t>(s, 0) = count;
    Field<uint32_t*>(s, 0x18) = &k;
    orig::fn_004e0f40(u(s));
    if (Field<uint32_t>(s, 0xc) != 0)
        return 0;
    uint8_t* e = Field<uint8_t**>(s, 0x10)[Field<uint32_t>(s, 8)];
    if (e == nullptr)
        return 0;
    return u(e) + Field<uint16_t>(e, 2) + 4;
}

// 0x00489680 (ebx = render mode; stack: render system [game+0xa78]; ret 4): when the mode's owner [+4] has a
// name table (+0x74), resolves each still-unset texture slot +0x20/+0x24/+0x28/+0x2c whose name key word
// +0x16/+0x18/+0x1a/+0x1c is nonzero: name (0x00489630) -> base name without directory and extension (0x004e5980,
// 0x40-byte buffer; its ECX input is not read) -> TextureBank_Find. Caller RenderModes_Load.
uint32_t fn_00489680(uint8_t* mode, uint32_t a0)
{
    char name[0x40];
    uint8_t* owner = Field<uint8_t*>(mode, 4);
    if (Field<uint32_t>(owner, 0x74) == 0)
        return a0;
    uint32_t r = 0;
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (Field<uint32_t>(mode, 0x20 + i * 4) != 0)
            continue;
        const uint32_t key = Field<uint16_t>(mode, 0x16 + i * 2);
        if (key == 0)
            continue;
        const uint32_t n = fn_00489630(key, Field<uint8_t*>(owner, 0x74));
        orig::fn_004e5980(0, n, u(name));
        r = TextureBank_Find(u(name), B(a0));
        Field<uint32_t>(mode, 0x20 + i * 4) = r;
    }
    return r;
}

// 0x00489770 (esi = render mode; stack: render system, not read; ret 4): frees the render state +8 and clears it
// and the texture slots +0x20..+0x2c. Caller 0x0045aad0.
uint32_t fn_00489770(uint8_t* mode, uint32_t /*a0*/)
{
    uint32_t r = Field<uint32_t>(mode, 8);
    if (r != 0)
        r = Free(r);
    Field<uint32_t>(mode, 0x20) = 0;
    Field<uint32_t>(mode, 0x24) = 0;
    Field<uint32_t>(mode, 0x2c) = 0;
    Field<uint32_t>(mode, 0x28) = 0;
    Field<uint32_t>(mode, 8) = 0;
    return r;
}

namespace
{
// R/B swap of a packed colour (0x00489b40 converts the file's colours to D3D order).
inline uint32_t SwapRB(uint32_t v)
{
    return ((v >> 16) & 0xff) | ((v & 0xff) << 16) | (v & 0xff00ff00u);
}
}  // namespace

// 0x00489b40 (eax = render mode, ecx not read; stack: render system [game+0xa78]; ret 4): builds the mode's 0x58-byte
// render state (+8, 0x004e07f0 args 4, 0x58, 2, "RenderMode structure" 0x0059da10) when the render system has a
// state table (+0x1c8) and the mode has none: zeroed, textures resolved (0x00489680), +0x18 state flags / +0x1c
// colour / +0x20..+0x2c textures / +0x44..+0x50 blend args chosen from mode flags (word +0x34: 0x6000 blend kinds,
// 2, 4, 0x200, 0x1000) and the byte +0x32; then 0x004897d0 (esi = state+0x18, edi = mode; system, 1.0), bit 0x10
// unless (flags & 0xf00) == 0x100, colours +0x50..+0x5c R/B-swapped into +0x30..+0x3c, +0x40 = 8.0 with flag 2 when
// [+0x60] > 0 and [+0x5c] has colour (also writes 8.0 back to +0x60), and +0x54 = the system's shader slot
// (+0x1c4 table, index +0x20, or +1 when Material_IsTranslucent, or the 0x004f1140 lookup of the first texture).
uint32_t RenderModes_Load(uint32_t eax, uint32_t /*ecx*/, uint32_t a0)
{
    uint8_t* mode = B(eax);
    uint8_t* sys = B(a0);
    if (Field<uint32_t>(sys, 0x1c8) == 0 || Field<uint32_t>(mode, 8) != 0)
        return a0;
    uint8_t* rs = B(orig::fn_004e07f0(0x0063a210u, 4, 0x58, 2, 0x0059da10));
    Field<uint32_t>(mode, 8) = u(rs);
    if (rs == nullptr)
        return 0;
    std::memset(rs, 0, 0x58);
    fn_00489680(mode, a0);
    Field<uint32_t>(rs, 0x18) = 0x80081;
    Field<uint32_t>(rs, 0x1c) = 0x3f3f3f;
    const uint32_t flags = Field<uint16_t>(mode, 0x34);
    if ((flags & 0x6000) != 0)
    {
        bool tailA = false;
        if ((flags & 2) != 0)
        {
            Field<uint32_t>(rs, 0x18) = 0x8b081;
            Field<uint32_t>(rs, 0x44) = (Field<uint8_t>(mode, 0x32) >> 2) & 3;
            Field<uint32_t>(rs, 0x48) = Field<uint8_t>(mode, 0x32) & 3;
            Field<uint32_t>(rs, 0x4c) = (Field<uint8_t>(mode, 0x32) >> 4) & 3;
            Field<uint32_t>(rs, 0x50) = Field<uint8_t>(mode, 0x32) >> 6;
            Field<uint32_t>(rs, 0x20) = Field<uint32_t>(mode, 0x24);
            tailA = true;
        }
        else if ((flags & 0x6000) == 0x2000)
        {
            Field<uint32_t>(rs, 0x18) = 0x8d081;
            Field<uint32_t>(rs, 0x44) = Field<uint8_t>(mode, 0x32) & 3;
            Field<uint32_t>(rs, 0x48) = (Field<uint8_t>(mode, 0x32) >> 4) & 3;
            Field<uint32_t>(rs, 0x4c) = Field<uint8_t>(mode, 0x32) >> 6;
            Field<uint32_t>(rs, 0x50) = 0;
            Field<uint32_t>(rs, 0x20) = Field<uint32_t>(mode, 0x20);
            Field<uint32_t>(rs, 0x24) = Field<uint32_t>(mode, 0x28);
            Field<uint32_t>(rs, 0x28) = Field<uint32_t>(mode, 0x2c);
        }
        else if ((flags & 0x6000) == 0x4000)
        {
            Field<uint32_t>(rs, 0x18) = 0x8f081;
            Field<uint32_t>(rs, 0x44) = 6;
            Field<uint32_t>(rs, 0x48) = Field<uint8_t>(mode, 0x32) & 3;
            Field<uint32_t>(rs, 0x4c) = Field<uint8_t>(mode, 0x32) >> 6;
            Field<uint32_t>(rs, 0x50) = 0;
            Field<uint32_t>(rs, 0x20) = 0;
            Field<uint32_t>(rs, 0x24) = Field<uint32_t>(mode, 0x20);
            Field<uint32_t>(rs, 0x28) = Field<uint32_t>(mode, 0x2c);
        }
        else
        {
            Field<uint32_t>(rs, 0x18) = 0x8a081;
            Field<uint32_t>(rs, 0x44) = 6;
            Field<uint32_t>(rs, 0x48) = Field<uint8_t>(mode, 0x32) & 3;
            Field<uint32_t>(rs, 0x4c) = (Field<uint8_t>(mode, 0x32) >> 4) & 3;
            Field<uint32_t>(rs, 0x50) = Field<uint8_t>(mode, 0x32) >> 6;
            Field<uint32_t>(rs, 0x20) = 0;
            tailA = true;
        }
        if (tailA)
        {
            Field<uint32_t>(rs, 0x24) = Field<uint32_t>(mode, 0x20);
            Field<uint32_t>(rs, 0x28) = Field<uint32_t>(mode, 0x28);
            Field<uint32_t>(rs, 0x2c) = Field<uint32_t>(mode, 0x2c);
        }
        if ((Field<uint8_t>(mode, 0x35) & 0x10) != 0)
        {
            Field<uint32_t>(rs, 0x18) |= 8;
            Field<uint8_t>(mode, 0x34) |= 0x10;
        }
        else
        {
            Field<uint32_t>(rs, 0x18) = Field<uint32_t>(rs, 0x18) | 4;
        }
    }
    else
    {
        const uint32_t tex0 = Field<uint32_t>(mode, 0x20);
        const uint32_t arg = Field<uint8_t>(mode, 0x32) & 3;
        if (tex0 != 0)
        {
            if ((flags & 0x200) != 0)
            {
                Field<uint32_t>(rs, 0x18) = 0x80089;
                Field<uint8_t>(mode, 0x34) |= 0x10;
            }
            else
            {
                Field<uint32_t>(rs, 0x18) = 0x80085;
            }
        }
        Field<uint32_t>(rs, 0x50) = 0;
        Field<uint32_t>(rs, 0x4c) = 0;
        Field<uint32_t>(rs, 0x48) = 0;
        Field<uint32_t>(rs, 0x44) = 0;
        const uint32_t flags2 = Field<uint16_t>(mode, 0x34);
        if ((flags2 & 2) != 0)
        {
            Field<uint32_t>(rs, 0x24) = Field<uint32_t>(mode, 0x24);
            Field<uint32_t>(rs, 0x48) = (Field<uint8_t>(mode, 0x32) >> 2) & 3;
            const uint32_t s = Field<uint32_t>(rs, 0x18) | 0xc000;
            Field<uint32_t>(rs, 0x20) = tex0;
            Field<uint32_t>(rs, 0x44) = arg;
            Field<uint32_t>(rs, 0x18) = s;
        }
        else if ((flags2 & 4) != 0)
        {
            Field<uint32_t>(rs, 0x24) = tex0;
            const uint32_t s = Field<uint32_t>(rs, 0x18) | 0x7000;
            Field<uint32_t>(rs, 0x48) = arg;
            Field<uint32_t>(rs, 0x20) = 0;
            Field<uint32_t>(rs, 0x44) = 6;
            Field<uint32_t>(rs, 0x18) = s;
            uint32_t c = static_cast<uint32_t>(Ftol(Field<float>(mode, 0x40) * GameGlobal<float>(0x005bcb34)));
            if (c >= 0xff)
                c = 0xff;
            const uint32_t a = c & 0xff;
            Field<uint32_t>(rs, 0x1c) = ((((a | 0xffffff00u) << 8) | a) << 8) | a;
        }
        else
        {
            const uint32_t s = Field<uint32_t>(rs, 0x18) | 0x3000;
            Field<uint32_t>(rs, 0x20) = tex0;
            Field<uint32_t>(rs, 0x44) = arg;
            Field<uint32_t>(rs, 0x18) = s;
        }
    }
    orig::fn_004897d0(Field<uint32_t>(mode, 8) + 0x18, u(mode), a0, 0x3f800000);
    uint8_t* st = Field<uint8_t*>(mode, 8);
    const uint32_t s18 = Field<uint32_t>(st, 0x18);
    if ((s18 & 0xf00) != 0x100)
        Field<uint32_t>(st, 0x18) = s18 | 0x10;
    Field<uint32_t>(rs, 0x30) = SwapRB(Field<uint32_t>(mode, 0x50));
    Field<uint32_t>(rs, 0x34) = SwapRB(Field<uint32_t>(mode, 0x54));
    Field<uint32_t>(rs, 0x38) = SwapRB(Field<uint32_t>(mode, 0x58));
    Field<uint32_t>(rs, 0x3c) = SwapRB(Field<uint32_t>(mode, 0x5c));
    if (Field<float>(mode, 0x60) > 0.0f && (Field<uint32_t>(mode, 0x5c) & 0xffffff) != 0)
    {
        Field<uint32_t>(mode, 0x60) = 0x41000000;
        Field<uint32_t>(rs, 0x18) |= 2;
        Field<uint32_t>(rs, 0x40) = Field<uint32_t>(mode, 0x60);
    }
    else
    {
        Field<uint32_t>(rs, 0x18) &= 0xfffffffdu;
        Field<uint32_t>(rs, 0x40) = 0;
    }
    const uint32_t* tex = Field<uint32_t*>(mode, 0x20);
    if (tex == nullptr)
    {
        const uint32_t v = Field<uint32_t*>(sys, 0x1c4)[Field<uint32_t>(sys, 0x20)];
        Field<uint32_t>(rs, 0x54) = v;
        return v;
    }
    if (static_cast<uint8_t>(orig::fn_004897a0(u(mode), a0, 0x3f800000)) != 0)
    {
        const uint32_t v = Field<uint32_t*>(sys, 0x1c4)[Field<uint32_t>(sys, 0x20) + 1];
        Field<uint32_t>(rs, 0x54) = v;
        return v;
    }
    int32_t index = static_cast<int32_t>(orig::fn_004f1140(*tex, Field<uint32_t>(sys, 0xc)));
    const uint32_t* table = Field<uint32_t*>(sys, 0x1c4);
    if (index < 0)
        index = Field<int32_t>(sys, 0x20);
    Field<uint32_t>(rs, 0x54) = table[index];
    return static_cast<uint32_t>(index);
}

// ================================================================================================================
// 0x0048a6c0 - 0x0048b130: render system class methods, compressed-file wrapper object
// ================================================================================================================

uint32_t fn_0048a770(uint8_t* f);

// 0x0048a6c0 (no inputs): returns the class descriptor 0x0057eda8 ("CLASSP(RenderSystem)" at its +0x14). Slot 1 of
// the vtable at 0x0059d9f8 (slot 0 is 0x0048a6d0). No direct callers; not executed in recorded runs.
uint32_t RenderSystem_GetClass()
{
    return 0x0057eda8;
}

// 0x0048a6d0 (no inputs): returns 0x8000000b. Slot 0 of the vtable at 0x0059d9f8 (class RenderSystem by slot 1).
// guess: a type id. Not executed in recorded runs.
uint32_t fn_0048a6d0()
{
    return 0x8000000b;
}

// 0x0048a6e0 (eax = checksum state): +0 = -1 (no running checksum). Callers 0x0048a830, 0x0048aac0.
void fn_0048a6e0(uint32_t* state)
{
    state[0] = 0xffffffffu;
}

// 0x0048a6f0 (eax = checksum state): finishes it (0x0048a770) unless +0 is -1. Returns EAX as left (the state
// when nothing was done). Caller 0x0048a8b0.
uint32_t fn_0048a6f0(uint8_t* state)
{
    if (Field<uint32_t>(state, 0) == 0xffffffffu)
        return u(state);
    return fn_0048a770(state);
}

// 0x0048a770 (esi = checksum state {+0 running, +4 value, +8 total}): total ^= rotl(value, value & 31) (written
// as shr (32 - n) | shl n), +0 = -1. Returns value << n (EAX as left). Callers 0x0048a6f0, 0x0048aac0, 0x0048aff0.
uint32_t fn_0048a770(uint8_t* f)
{
    const uint32_t v = Field<uint32_t>(f, 4);
    const uint32_t n = v & 0x1f;
    const uint32_t hi = v >> ((0x20 - n) & 0x1f);
    const uint32_t lo = v << n;
    Field<uint32_t>(f, 0) = 0xffffffffu;
    Field<uint32_t>(f, 8) ^= hi | lo;
    return lo;
}

// 0x0048a830 (al = mode bit 0, cl = mode bit 4, edx = file name, esi = 0x1258-byte file object): initialises it:
// checksum state +0x124c reset (0x0048a6e0), name copied to +1, flags byte +0 = (old & 0xe8) | cl<<4 | al | 8,
// +0x1248 = 0, 14 dwords at +0x210 and +0x238 zeroed, callbacks +0x230 = 0x0048a800, +0x234 = 0x0048a820; then
// 0x0044ad50 (AL = 1). Returns the object. Caller 0x0048b0a0.
uint32_t fn_0048a830(uint32_t eax, uint32_t ecx, const char* name, uint8_t* f)
{
    const uint8_t bl = static_cast<uint8_t>(eax);
    fn_0048a6e0(reinterpret_cast<uint32_t*>(f + 0x124c));
    char* dst = reinterpret_cast<char*>(f + 1);
    const char* src = name;
    char c;
    do
    {
        c = *src++;
        *dst++ = c;
    } while (c != 0);
    const uint8_t cl = static_cast<uint8_t>(((ecx & 1) << 4) | (Field<uint8_t>(f, 0) & 0xe8) | (bl & 1) | 8);
    Field<uint8_t>(f, 0) = cl;
    Field<uint32_t>(f, 0x1248) = 0;
    std::memset(f + 0x210, 0, 0xe * 4);
    Field<uint32_t>(f, 0x238) = 0;
    Field<uint32_t>(f, 0x230) = 0x0048a800;
    Field<uint32_t>(f, 0x234) = 0x0048a820;
    orig::fn_0044ad50(1);
    return u(f);
}

// 0x0048a8b0 (ecx = passed on, esi = file object): closes it when open (flag bit 1: CompressedFile_Close with the
// caller's ECX), 0x0044ad50 (AL = 0), then a tail jump to 0x0048a6f0 (eax = +0x124c), reproduced as
// call-then-return. Caller 0x0048b130.
uint32_t fn_0048a8b0(uint32_t ecx, uint8_t* f)
{
    uint32_t r = 0;
    if ((Field<uint8_t>(f, 0) & 2) != 0)
        r = orig::fn_0048aff0(u(f), ecx);
    orig::fn_0044ad50(Lo8(r, 0));
    return fn_0048a6f0(f + 0x124c);
}

// 0x0048aab0 (eax = file object): returns +0x1248. Caller 0x0048b1a0.
uint32_t fn_0048aab0(uint8_t* f)
{
    return Field<uint32_t>(f, 0x1248);
}

// 0x0048b070 (eax = wide name, ecx not read): 1 when the name is non-empty and not L"SCRATCH" (0x0059d9c8,
// _wcsicmp), else 0. Caller 0x0048b0a0.
uint32_t fn_0048b070(const wchar_t* name, uint32_t /*ecx*/)
{
    if (name == nullptr || name[0] == 0)
        return 0;
    if (GameFunction<int(__cdecl*)(const wchar_t*, const wchar_t*)>(0x005665f0)(
            name, reinterpret_cast<const wchar_t*>(0x0059d9c8)) == 0)
        return 0;
    return 1;
}

// 0x0048b0a0 (eax = wide name, ecx = passed on; stack: byte mode; caller pops): opens a compressed file object:
// 0x0048b260 (AL = mode, ecx = name; ecx_in, 0x100-byte path buffer, 0x100) resolves the path; on success allocates
// 0x1258 bytes (0x004e07f0 args 0x10, 0x1258, 2, "new") and initialises it (0x0048a830: AL = mode, CL = 1 when
// the name is empty or "SCRATCH" (0x0048b070), edx = path). Returns the object or 0. Callers 0x0048b150,
// 0x0048b1a0, 0x0048b6d0, 0x004a3840, 0x004a3930.
uint32_t fn_0048b0a0(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    char path[0x100];
    const uint32_t r = orig::fn_0048b260(Lo8(u(path), a0), eax, ecx, u(path), 0x100);
    if (static_cast<uint8_t>(r) == 0)
        return 0;
    const uint8_t scratch = fn_0048b070(reinterpret_cast<const wchar_t*>(eax), 0) == 0 ? 1 : 0;
    const uint32_t f = orig::fn_004e07f0(0x0063a210u, 0x10, 0x1258, 2, 0x0058b5e8);
    if (f == 0)
        return 0;
    return fn_0048a830(Lo8(f, a0), scratch, path, B(f));
}

// 0x0048b130 (eax = file object or null): closes (0x0048a8b0; its ECX is the caller's, not an input here: 0) and
// frees it. Callers 0x0048b150, 0x0048b1a0, 0x0048b9d0, 0x004a3840, 0x004a3930.
uint32_t fn_0048b130(uint8_t* f)
{
    if (f == nullptr)
        return 0;
    fn_0048a8b0(0, f);
    return Free(u(f));
}

// ================================================================================================================
// 0x00495ad0 - 0x00495ff0: text data
// ================================================================================================================

// 0x00495ad0: clears the cached text table globals 0x0063a150 / 0x0063a154. Callers 0x0044a670, 0x004629b0.
uint32_t fn_00495ad0()
{
    GameGlobal<uint32_t>(0x0063a150) = 0;
    GameGlobal<uint32_t>(0x0063a154) = 0;
    return 0;
}

// 0x00495ae0 (eax = count, ecx = table of 0x28-byte records; stack: entry base, string table; caller pops):
// unless already done for this table/count (globals 0x0063a150 / 0x0063a154), fixes it up: each record's +0x24 =
// base + index * 16; for each 16-byte entry (count +0x20): +8 = index of string table[+8] in the font table at
// 0x005cadd8 (0x18-byte entries, name first, null-terminated; _stricmp; the entry count when absent), +0xc =
// string table[+0xc]. Caller 0x004629b0.
uint32_t fn_00495ae0(uint32_t count, uint8_t* table, uint32_t base, uint32_t strings)
{
    if (GameGlobal<uint8_t*>(0x0063a150) == table && GameGlobal<uint32_t>(0x0063a154) == count)
        return count;
    GameGlobal<uint8_t*>(0x0063a150) = table;
    GameGlobal<uint32_t>(0x0063a154) = count;
    const uint32_t* str = reinterpret_cast<const uint32_t*>(strings);
    auto stricmp_ = GameFunction<int(__cdecl*)(uint32_t, uint32_t)>(0x005448ee);
    uint32_t i = 0;
    if (count == 0)
        return 0;
    uint32_t off = 0;
    do
    {
        uint8_t* rec = GameGlobal<uint8_t*>(0x0063a150) + off;
        const uint32_t entries = (Field<uint32_t>(rec, 0x24) << 4) + base;
        Field<uint32_t>(rec, 0x24) = entries;
        uint32_t* e = reinterpret_cast<uint32_t*>(entries + 8);
        for (uint32_t j = 0; j < Field<uint32_t>(GameGlobal<uint8_t*>(0x0063a150) + off, 0x20); ++j, e += 4)
        {
            const uint32_t name = str[e[0]];
            uint32_t index = 0;
            if (GameGlobal<uint32_t>(0x005cadd8) != 0)
            {
                uint8_t* font = B(0x005cadd8);
                while (stricmp_(name, Field<uint32_t>(font, 0)) != 0)
                {
                    ++index;
                    font += 0x18;
                    if (Field<uint32_t>(font, 0) == 0)
                        break;
                }
            }
            e[0] = index;
            e[1] = str[e[1]];
        }
        ++i;
        off += 0x28;
    } while (i < GameGlobal<uint32_t>(0x0063a154));
    return i;
}

// 0x00495ff0 (esi = text data [game+0xac0]): frees hash +0xa8 (ReleaseHash), +0xa0, and zeroes +0xa4 / +0xac.
// Caller 0x0044a670.
uint32_t fn_00495ff0(uint8_t* t)
{
    uint32_t r = 0;
    const uint32_t h = Field<uint32_t>(t, 0xa8);
    if (h != 0)
    {
        r = ReleaseHash(h);
        Field<uint32_t>(t, 0xa8) = 0;
    }
    if (Field<uint32_t>(t, 0xa0) != 0)
    {
        r = Free(Field<uint32_t>(t, 0xa0));
        Field<uint32_t>(t, 0xa0) = 0;
    }
    Field<uint32_t>(t, 0xa4) = 0;
    Field<uint32_t>(t, 0xac) = 0;
    return r;
}

// ================================================================================================================
// 0x004e1ad0 - 0x004e1db0: chunk heap objects
// ================================================================================================================

namespace
{
// Relocation context of the loader's fix-up passes (as in src/loader/auto_004e6570.cpp): +0 file (whose +8 is the
// relocation table), +4 nonzero = already fixed up, +8 nonzero = byte-swap.
struct RelocCtx01
{
    uint8_t* file;
    uint32_t fixed;
    uint32_t swap;
};

// Where the field at `field` really lives: field + ([entry + w + 4] - [entry + w]) for the relocation entry found
// by key = field (searcher [table+0x24], 0x004e0f40; w = word[entry+2]). The original reads [0 + 2] when the
// search fails (no null check).
uint8_t* RelocLocate(RelocCtx01* ctx, uintptr_t field)
{
    uintptr_t key = field;
    uint8_t* t = Field<uint8_t*>(ctx->file, 8);
    uint32_t e = 0;
    const uint32_t count = Field<uint32_t>(t, 0x1c);
    if (count != 0 && Field<uint32_t>(t, 0x20) != 0)
    {
        uint8_t* s = Field<uint8_t*>(t, 0x24);
        Field<uint32_t>(s, 0) = count;
        Field<uintptr_t*>(s, 0x18) = &key;
        orig::fn_004e0f40(u(s));
        if (Field<uint32_t>(s, 0xc) == 0)
            e = Field<uint32_t*>(s, 0x10)[Field<uint32_t>(s, 8)];
    }
    const uint32_t w = *reinterpret_cast<volatile uint16_t*>(e + 2);
    const uint32_t from = Field<uint32_t>(B(e + w), 0);
    const uint32_t to = Field<uint32_t>(B(e + w), 4);
    return B(to - from + field);
}
}  // namespace

// 0x004e1ad0 (edi = relocation context; stack: chunk heap object; caller pops): fixes up a loaded chunk heap
// object: the circular list at +0x10 (Reloc_FixupPointer 0x004e2040 on each link), then unless already fixed each
// node's +4 (dword) / +8 / +0xa (words) byte-swapped when swapping; then +0x20 and +0xc (Reloc_FixupPointer) and
// +0x14 / +0x18 / +0x1c located and swapped by 0x004f60b0. Callers 0x004e1320, 0x004e1db0.
uint32_t fn_004e1ad0(RelocCtx01* ctx, uint8_t* heap)
{
    uint8_t* head = heap + 0x10;
    uint8_t* p = head;
    do
    {
        orig::fn_004e2040(u(p), u(ctx));
        p = Field<uint8_t*>(p, 0);
    } while (p != head);
    uint32_t r = 0;
    for (uint8_t* node = Field<uint8_t*>(head, 0); node != heap + 0x10; node = Field<uint8_t*>(node, 0))
    {
        if (ctx->fixed == 0)
        {
            uint8_t* f = RelocLocate(ctx, u(node + 4));
            if (ctx->swap != 0)
            {
                const uint32_t v = Field<uint32_t>(f, 0);
                Field<uint32_t>(f, 0) = (v << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
            }
        }
        for (uint32_t o = 8; o <= 0xa; o += 2)
        {
            if (ctx->fixed != 0)
                continue;
            uint8_t* f = RelocLocate(ctx, u(node + o));
            if (ctx->swap != 0)
            {
                const uint16_t v = Field<uint16_t>(f, 0);
                Field<uint16_t>(f, 0) = static_cast<uint16_t>((v << 8) | (v >> 8));
            }
        }
    }
    orig::fn_004e2040(u(heap + 0x20), u(ctx));
    r = orig::fn_004e2040(u(heap + 0xc), u(ctx));
    for (uint32_t o = 0x14; o <= 0x1c; o += 4)
    {
        r = ctx->fixed;
        if (r != 0)
            continue;
        uint8_t* f = RelocLocate(ctx, u(heap + o));
        r = ctx->swap;
        if (r != 0)
            r = orig::fn_004f60b0(u(f), 0, 0, 0);
    }
    return r;
}

// 0x004e1db0 (eax = loaded chunk buffer or null, ecx = block alignment; stack: parent heap; caller pops): with a
// buffer, the heap object at buffer + [buffer+8] is fixed up in place (0x004e1ad0 with context {buffer, 1, 0}) and
// its +0xc = parent; without, a new 0x30-byte one-block heap is made in the parent (0x004e1e90(parent, 0x30, 1, 2)):
// list head, sizes 0x30, words 1 / 2, +0x18 = 1, +0x1c = max(alignment, 2), +0x20 = itself. Returns the heap.
// Callers ChunkFile_Load, ObjectSystem_LoadSkins, 0x004e84e0, 0x004eb140.
uint32_t fn_004e1db0(uint32_t buffer, uint32_t align, uint32_t parent)
{
    if (buffer != 0)
    {
        uint8_t* heap = B(buffer + Field<uint32_t>(B(buffer), 8));
        RelocCtx01 ctx = {B(buffer), 1, 0};
        fn_004e1ad0(&ctx, heap);
        Field<uint32_t>(heap, 0xc) = parent;
        return u(heap);
    }
    uint8_t* h = B(orig::fn_004e1e90(parent, 0x30, 1, 2));
    Field<uint32_t>(h, 0xc) = parent;
    Field<uint32_t>(h, 0) = u(h + 0x10);
    Field<uint32_t>(h, 0x10) = u(h);
    Field<uint32_t>(h, 4) = 0x30;
    Field<uint16_t>(h, 8) = 1;
    Field<uint16_t>(h, 0xa) = 2;
    Field<uint32_t>(h, 0x14) = 0x30;
    Field<uint32_t>(h, 0x18) = 1;
    Field<uint32_t>(h, 0x1c) = align < 2 ? 2 : align;
    Field<uint32_t>(h, 0x20) = u(h);
    return u(h);
}

// ================================================================================================================
// 0x004e5aa0 - 0x004e6b70: streamers. Base (memory) streamer vtable 0x0058b178, a variant with destructor 0x004e5e30
// at 0x0058b13c, file streamer (background thread) vtable 0x0058b0f8. Layout: +4 buffer, +8 block size, byte +0xc
// block count (<= 16), byte +0xd write block, byte +0xe read block, +0x10 flags (1 file, 2 stop, 4 open request,
// 8 write, 0x10 thread, 0x40 error, 0x80 reset, 0x200 owns buffer), +0x14 read total, +0x18 written total, +0x1c
// buffered, +0x20 blocks of {data, read pos, fill} (12 bytes). File streamer: +0xe4 progress callback (cdecl
// (+0xe8, +0x14)), +0xec mode, +0x1f8 name, +0x510 file list cursor, +0x518 thread, +0x520 lock, +0x538 / +0x53c /
// +0x540 events, +0x544 deferred-delete link (list 0x0063a0d8), +0x564 pause count. The class names are not known
// (slot 1 is not a class descriptor here); names below are descriptive guesses from behaviour.
// ================================================================================================================

namespace
{
inline void EnterCS(uint8_t* s)
{
    Imp<decltype(&EnterCriticalSection)>(0x0056b0b0)(reinterpret_cast<CRITICAL_SECTION*>(s + 0x520));
}
inline void LeaveCS(uint8_t* s)
{
    Imp<decltype(&LeaveCriticalSection)>(0x0056b0b4)(reinterpret_cast<CRITICAL_SECTION*>(s + 0x520));
}
inline BOOL SetEvent_(uint32_t h)
{
    return Imp<decltype(&SetEvent)>(0x0056b0bc)(reinterpret_cast<HANDLE>(h));
}
inline DWORD Wait_(uint32_t h)
{
    return Imp<decltype(&WaitForSingleObject)>(0x0056b0a8)(reinterpret_cast<HANDLE>(h), 0xffffffffu);
}
inline void Sleep_(DWORD ms)
{
    Imp<decltype(&Sleep)>(0x0056b0b8)(ms);
}
inline uint8_t* Block(uint8_t* s, uint32_t index)
{
    return s + 0x20 + index * 0xc;
}
}  // namespace

uint32_t fn_004e5b30(uint32_t buffer, uint32_t blockSize, uint32_t total, uint8_t* s);
uint32_t fn_004e5ba0(uint8_t* s, uint32_t size, uint32_t dest);
uint32_t fn_004e5d60(uint8_t* s, uint32_t n);
uint32_t fn_004e5de0(uint8_t* s);
uint32_t fn_004e5e50(uint8_t* s);
uint32_t fn_004e5f90(uint8_t* s);
uint32_t fn_004e6070(uint8_t* s);

// 0x004e5aa0 (ecx = streamer; stack: flags; ret 4): scalar deleting destructor, vtable 0x0058b178 slot 14: vtable
// = 0x0058b178, frees the owned buffer (flag 0x200), frees the object when flags & 1. Not executed in recorded runs.
uint32_t fn_004e5aa0(uint8_t* s, uint32_t flags)
{
    Field<uint32_t>(s, 0) = 0x0058b178;
    if ((Field<uint32_t>(s, 0x10) & 0x200) != 0)
    {
        Free(Field<uint32_t>(s, 4));
        Field<uint32_t>(s, 4) = 0;
    }
    if ((flags & 1) != 0)
        Free(u(s));
    return u(s);
}

// 0x004e5ae0 (eax = object, ebx = total size; stack: buffer or null, block size; ret 8): memory streamer
// constructor: vtable 0x0058b178, 0x37 dwords from +4 zeroed, blocks set up by 0x004e5b30. Returns the object.
// Caller 0x0050de70. Not executed in recorded runs.
uint32_t fn_004e5ae0(uint8_t* s, uint32_t total, uint32_t buffer, uint32_t blockSize)
{
    Field<uint32_t>(s, 0) = 0x0058b178;
    std::memset(s + 4, 0, 0x37 * 4);
    fn_004e5b30(buffer, blockSize, total, s);
    return u(s);
}

// 0x004e5b10 (ecx = streamer or null; stack: 1 dword; ret 4): Release, slot 0 of 0x0058b178 / 0x0058b13c: a tail
// jump to virtual +0x38 (deleting destructor) with the argument slot overwritten by 1, reproduced as
// call-then-return. Not executed in recorded runs.
uint32_t fn_004e5b10(uint8_t* s, uint32_t /*a0*/)
{
    if (s == nullptr)
        return 0;
    return VCall<uint32_t>(s, 0x38 / 4, 1u);
}

// 0x004e5b30 (eax = buffer or null, ecx = block size, ebx = total size, esi = streamer): uses the buffer or
// allocates total bytes (0x004e07f0 args 0x1a, total, 2, "Streamer memory" 0x0058b1c8, flag 0x200); +8 = block
// size, byte +0xc = low byte of total / block size, capped at 16 when above (a quotient of 0x100 gives 0: compared
// as a byte), block i data = buffer + i * block size. Callers 0x004e5ae0, 0x004e5f30.
uint32_t fn_004e5b30(uint32_t buffer, uint32_t blockSize, uint32_t total, uint8_t* s)
{
    if (buffer == 0)
    {
        Field<uint32_t>(s, 4) = orig::fn_004e07f0(0x0063a210u, 0x1a, total, 2, 0x0058b1c8);
        Field<uint32_t>(s, 0x10) |= 0x200;
    }
    else
    {
        Field<uint32_t>(s, 4) = buffer;
    }
    const uint8_t q = static_cast<uint8_t>(total / blockSize);
    Field<uint32_t>(s, 8) = blockSize;
    Field<uint8_t>(s, 0xc) = q;
    if (q > 0x10)
        Field<uint8_t>(s, 0xc) = 0x10;
    uint32_t i = 0;
    if (Field<uint8_t>(s, 0xc) == 0)
        return 0;
    do
    {
        Field<uint32_t>(Block(s, i), 0) = Field<uint32_t>(s, 8) * i + Field<uint32_t>(s, 4);
        ++i;
    } while (i < Field<uint8_t>(s, 0xc));
    return i;
}

// 0x004e5ba0 (ecx = streamer; stack: size, destination or null; ret 8): Read, slot 1 of the base vtables: loops
// over virtual +0x1c (get read pointer; returns bytes available) and +0x20 (consume n), copying up to size bytes.
// Returns the bytes read. Caller 0x004e6810 (file streamer Read).
uint32_t fn_004e5ba0(uint8_t* s, uint32_t size, uint32_t dest)
{
    uint32_t total = 0;
    uint32_t remaining = size;
    while (remaining != 0)
    {
        uint32_t src = 0;
        uint32_t n = VCall<uint32_t>(s, 0x1c / 4, &src);
        if (n == 0)
            break;
        if (n >= remaining)
            n = remaining;
        if (dest != 0)
        {
            std::memcpy(B(dest), B(src), n);
            dest += n;
        }
        VCall<uint32_t>(s, 0x20 / 4, n);
        remaining -= n;
        total += n;
    }
    return total;
}

// 0x004e5c30 (ecx = streamer): 1 when the write block (byte +0xd) is empty (read pos == fill), else 0. Slot 2 of
// the base vtables. Not executed in recorded runs.
uint32_t fn_004e5c30(uint8_t* s)
{
    uint8_t* b = Block(s, Field<uint8_t>(s, 0xd));
    return Field<uint32_t>(b, 4) == Field<uint32_t>(b, 8) ? 1u : 0u;
}

// 0x004e5c50 (ecx = streamer): +0x1c (bytes buffered). Slot 3 of the base vtables. Not executed in recorded runs.
uint32_t fn_004e5c50(uint8_t* s)
{
    return Field<uint32_t>(s, 0x1c);
}

// 0x004e5c60 (ecx = streamer): +0x14 (bytes read). Slot 9 of all streamer vtables. Not executed in recorded runs.
uint32_t fn_004e5c60(uint8_t* s)
{
    return Field<uint32_t>(s, 0x14);
}

// 0x004e5c70 (ecx = streamer): +0x18 (bytes written). Slot 12 of all streamer vtables (also at 0x005898f4). Not
// executed in recorded runs.
uint32_t fn_004e5c70(uint8_t* s)
{
    return Field<uint32_t>(s, 0x18);
}

// 0x004e5c80 (ecx = streamer; stack: n; ret 4): Consume, slot 8 of the base vtables: +0x14 += n, read block's read
// pos += n, +0x1c -= n; when the read block is used up the read index advances (mod the block count). Not executed
// in recorded runs.
uint32_t fn_004e5c80(uint8_t* s, uint32_t n)
{
    Field<uint32_t>(s, 0x14) += n;
    Field<uint32_t>(Block(s, Field<uint8_t>(s, 0xe)), 4) += n;
    Field<uint32_t>(s, 0x1c) -= n;
    const int32_t idx = Field<uint8_t>(s, 0xe);
    uint8_t* b = Block(s, idx);
    if (Field<uint32_t>(b, 4) != Field<uint32_t>(b, 8))
        return static_cast<uint32_t>(idx);
    const int32_t c = Field<uint8_t>(s, 0xc);
    Field<uint8_t>(s, 0xe) = static_cast<uint8_t>((idx + 1) % c);
    return static_cast<uint32_t>((idx + 1) / c);
}

// 0x004e5cc0 (ecx = streamer; stack: out pointer; ret 4): GetReadPointer, slot 7 of the base vtables: 0 when the
// read block is empty, else *out = data + read pos and returns fill - read pos. Not executed in recorded runs.
uint32_t fn_004e5cc0(uint8_t* s, uint32_t* out)
{
    uint8_t* b = Block(s, Field<uint8_t>(s, 0xe));
    const uint32_t pos = Field<uint32_t>(b, 4);
    if (pos == Field<uint32_t>(b, 8))
        return 0;
    *out = Field<uint32_t>(b, 0) + pos;
    return Field<uint32_t>(b, 8) - Field<uint32_t>(b, 4);
}

// 0x004e5d60 (ecx = streamer; stack: n; ret 4): CommitWrite, slot 11 of the base vtables: splits n bytes over the
// write blocks (at most the block size each): +0x18 += m, block read pos = 0, fill = m, +0x1c += m, write index
// advances (mod the block count). Caller 0x004e6870 (file streamer, under its lock).
uint32_t fn_004e5d60(uint8_t* s, uint32_t n)
{
    uint32_t r = 0;
    while (n != 0)
    {
        uint32_t m = Field<uint32_t>(s, 8);
        if (n < m)
            m = n;
        Field<uint32_t>(s, 0x18) += m;
        Field<uint32_t>(Block(s, Field<uint8_t>(s, 0xd)), 4) = 0;
        Field<uint32_t>(Block(s, Field<uint8_t>(s, 0xd)), 8) = m;
        Field<uint32_t>(s, 0x1c) += m;
        const int32_t idx = Field<uint8_t>(s, 0xd) + 1;
        const int32_t c = Field<uint8_t>(s, 0xc);
        r = static_cast<uint32_t>(idx / c);
        Field<uint8_t>(s, 0xd) = static_cast<uint8_t>(idx % c);
        n -= m;
    }
    return r;
}

// 0x004e5dc0 (ecx = streamer): AL = (flags & 0x41) != 0 (error or file). Slot 13 of 0x0058b178. Not executed in
// recorded runs.
uint32_t fn_004e5dc0(uint8_t* s)
{
    return (Field<uint32_t>(s, 0x10) & 0x41) != 0 ? 1u : 0u;
}

// 0x004e5dd0 (ecx = streamer; stack: 4 dwords, not read; ret 0x10): Open on a memory streamer: sets the error flag
// 0x40. Slot 4 of the base vtables. Not executed in recorded runs.
void fn_004e5dd0(uint8_t* s, uint32_t, uint32_t, uint32_t, uint32_t)
{
    Field<uint32_t>(s, 0x10) |= 0x40;
}

// 0x004e5de0 (ecx = streamer): Reset, slot 5 of the base vtables: flags |= 1, every block's fill and read pos = 0,
// +0x14 / +0x18 / +0x1c = 0, both block indexes 0. Caller 0x004e69c0.
uint32_t fn_004e5de0(uint8_t* s)
{
    Field<uint32_t>(s, 0x10) |= 1;
    for (uint32_t i = 0; i < Field<uint8_t>(s, 0xc); ++i)
    {
        Field<uint32_t>(Block(s, i), 8) = 0;
        Field<uint32_t>(Block(s, i), 4) = 0;
    }
    Field<uint32_t>(s, 0x14) = 0;
    Field<uint32_t>(s, 0x18) = 0;
    Field<uint32_t>(s, 0x1c) = 0;
    Field<uint8_t>(s, 0xd) = 0;
    Field<uint8_t>(s, 0xe) = 0;
    return 0;
}

// 0x004e5e30 (ecx = streamer; stack: flags; ret 4): scalar deleting destructor, slot 14 of 0x0058b13c: 0x004e5e50,
// then frees the object when flags & 1.
uint32_t fn_004e5e30(uint8_t* s, uint32_t flags)
{
    fn_004e5e50(s);
    if ((flags & 1) != 0)
        Free(u(s));
    return u(s);
}

// 0x004e5e50 (esi = streamer): base destructor body: vtable = 0x0058b178, frees the owned buffer (flag 0x200).
// Caller 0x004e5e30.
uint32_t fn_004e5e50(uint8_t* s)
{
    uint32_t r = Field<uint32_t>(s, 0x10);
    Field<uint32_t>(s, 0) = 0x0058b178;
    if ((r & 0x200) != 0)
    {
        r = Free(Field<uint32_t>(s, 4));
        Field<uint32_t>(s, 4) = 0;
    }
    return r;
}

// 0x004e5e80 (ecx = streamer, edx = size, esi = data): when data and size are given and the write block is empty,
// points the write block at the caller's data, raises the block size to at least size and commits size bytes
// (virtual +0x2c); AL = 1, else AL = 0. Callers 0x004fa340, 0x004fb360.
uint32_t fn_004e5e80(uint8_t* s, uint32_t size, uint32_t data)
{
    uint8_t* b = Block(s, Field<uint8_t>(s, 0xd));
    if (data == 0 || size == 0 || Field<uint32_t>(b, 4) != Field<uint32_t>(b, 8))
        return Lo8(u(b), 0);
    Field<uint32_t>(b, 0) = data;
    uint32_t bs = Field<uint32_t>(s, 8);
    if (!(bs > size))
        bs = size;
    Field<uint32_t>(s, 8) = bs;
    const uint32_t r = VCall<uint32_t>(s, 0x2c / 4, size);
    return Lo8(r, 1);
}

// 0x004e5ec0 (bl = all): Sleep(0), then walks the deferred-delete list 0x0063a0d8 (link +0x544) and deletes
// (virtual +0x38 with 1) every streamer whose thread has finished (flag 0x10 clear), or all when BL. Caller
// 0x004e41e0.
uint32_t fn_004e5ec0(uint32_t ebx)
{
    Sleep_(0);
    uint32_t r = 0;
    uint32_t* link = &GameGlobal<uint32_t>(0x0063a0d8);
    uint8_t* s = B(*link);
    while (s != nullptr)
    {
        if (static_cast<uint8_t>(ebx) != 0 || ((Field<uint32_t>(s, 0x10) >> 4) & 1) == 0)
        {
            *link = Field<uint32_t>(s, 0x544);
            r = VCall<uint32_t>(s, 0x38 / 4, 1u);
        }
        else
        {
            link = reinterpret_cast<uint32_t*>(s + 0x544);
        }
        s = B(*link);
    }
    return r;
}

// 0x004e5f10 (ecx = file streamer; stack: flags; ret 4): scalar deleting destructor, slot 14 of 0x0058b0f8:
// 0x004e6070, then frees the object when flags & 1.
uint32_t fn_004e5f10(uint8_t* s, uint32_t flags)
{
    fn_004e6070(s);
    if ((flags & 1) != 0)
        Free(u(s));
    return u(s);
}

// 0x004e5f30 (eax = object, ebx = buffer size; stack: buffer or null, block size; ret 8): file streamer
// constructor: base part (vtable 0x0058b178, 0x37 dwords zeroed, 0x004e5b30 with the block size rounded up to
// 0x800), vtable 0x0058b0f8, 0x10b dwords from +0xe4 zeroed, +0x510 / +0x514 = 0, flags |= 1, then 0x004e5f90
// (thread). Returns the object. Callers 0x004f5f10, 0x004fa340, 0x0050c6d0.
uint32_t fn_004e5f30(uint8_t* s, uint32_t total, uint32_t buffer, uint32_t blockSize)
{
    Field<uint32_t>(s, 0) = 0x0058b178;
    std::memset(s + 4, 0, 0x37 * 4);
    fn_004e5b30(buffer, (blockSize + 0x7ff) & 0xfffff800u, total, s);
    Field<uint32_t>(s, 0) = 0x0058b0f8;
    std::memset(s + 0xe4, 0, 0x10b * 4);
    Field<uint32_t>(s, 0x510) = 0;
    Field<uint32_t>(s, 0x514) = 0;
    Field<uint32_t>(s, 0x10) |= 1;
    fn_004e5f90(s);
    return u(s);
}

// 0x004e5f90 (esi = file streamer): the first live file streamer (count 0x0063a0e0) initialises the global lock
// 0x00604ae0; sets +0x548 = 1.0, +0x54c = -1.0, +0x550 = 0, zeroes +0x518..+0x543, flags |= 0x10, +0x544 =
// +0x558 = 0, initialises the lock +0x520, creates three auto-reset events (+0x538, +0x53c, +0x540) and the
// suspended worker thread (0x004e6650, stack 0x1000, id +0x51c) into +0x518, priority 1, resumes it.
// Caller 0x004e5f30.
uint32_t fn_004e5f90(uint8_t* s)
{
    const uint32_t live = GameGlobal<uint32_t>(0x0063a0e0);
    GameGlobal<uint32_t>(0x0063a0e0) = live + 1;
    auto initCS = Imp<decltype(&InitializeCriticalSection)>(0x0056b094);
    if (live == 0)
        initCS(reinterpret_cast<CRITICAL_SECTION*>(0x00604ae0));
    Field<uint32_t>(s, 0x548) = 0x3f800000;
    Field<uint32_t>(s, 0x54c) = 0xbf800000;
    Field<uint32_t>(s, 0x550) = 0;
    std::memset(s + 0x518, 0, 0xb * 4);
    Field<uint32_t>(s, 0x10) |= 0x10;
    Field<uint32_t>(s, 0x544) = 0;
    Field<uint32_t>(s, 0x558) = 0;
    initCS(reinterpret_cast<CRITICAL_SECTION*>(s + 0x520));
    auto createEvent = Imp<decltype(&CreateEventA)>(0x0056b098);
    Field<HANDLE>(s, 0x538) = createEvent(nullptr, 0, 0, nullptr);
    Field<HANDLE>(s, 0x53c) = createEvent(nullptr, 0, 0, nullptr);
    Field<HANDLE>(s, 0x540) = createEvent(nullptr, 0, 0, nullptr);
    HANDLE t = Imp<decltype(&CreateThread)>(0x0056b09c)(
        nullptr, 0x1000, reinterpret_cast<LPTHREAD_START_ROUTINE>(0x004e6650), s, 4,
        reinterpret_cast<DWORD*>(s + 0x51c));
    Field<HANDLE>(s, 0x518) = t;
    Imp<decltype(&SetThreadPriority)>(0x0056b0a0)(t, 1);
    return Imp<decltype(&ResumeThread)>(0x0056b0a4)(Field<HANDLE>(s, 0x518));
}

// 0x004e6070 (esi = file streamer): destructor body: vtable 0x0058b0f8; when the thread runs (flag 0x10) requests
// stop (flag 2 under the lock) and signals +0x538; waits for the thread, closes it and the events +0x53c / +0x538
// (+0x540 is never closed), deletes the lock and, for the last streamer, the global lock 0x00604ae0; then the base
// destructor body (vtable 0x0058b178, owned buffer). Caller 0x004e5f10.
uint32_t fn_004e6070(uint8_t* s)
{
    Field<uint32_t>(s, 0) = 0x0058b0f8;
    if (((Field<uint32_t>(s, 0x10) >> 4) & 1) != 0)
    {
        EnterCS(s);
        Field<uint32_t>(s, 0x10) |= 2;
        LeaveCS(s);
        SetEvent_(Field<uint32_t>(s, 0x538));
    }
    Wait_(Field<uint32_t>(s, 0x518));
    auto closeHandle = Imp<decltype(&CloseHandle)>(0x0056b084);
    closeHandle(Field<HANDLE>(s, 0x518));
    closeHandle(Field<HANDLE>(s, 0x53c));
    closeHandle(Field<HANDLE>(s, 0x538));
    auto deleteCS = Imp<decltype(&DeleteCriticalSection)>(0x0056b0ac);
    deleteCS(reinterpret_cast<CRITICAL_SECTION*>(s + 0x520));
    if (--GameGlobal<uint32_t>(0x0063a0e0) == 0)
        deleteCS(reinterpret_cast<CRITICAL_SECTION*>(0x00604ae0));
    uint32_t r = Field<uint32_t>(s, 0x10);
    Field<uint32_t>(s, 0) = 0x0058b178;
    if ((r & 0x200) != 0)
    {
        r = Free(Field<uint32_t>(s, 4));
        Field<uint32_t>(s, 4) = 0;
    }
    return r;
}

// 0x004e6120 (ecx = file streamer; stack: byte now; ret 4): Release, slot 0 of 0x0058b0f8: requests the thread to
// stop (as 0x004e6070); deletes the object (virtual +0x38 with 1) unless it owns its buffer (flag 0x200) and `now`
// is 0, in which case it is queued on the deferred-delete list 0x0063a0d8 (link +0x544, reaped by 0x004e5ec0).
uint32_t fn_004e6120(uint8_t* s, uint32_t now)
{
    if (((Field<uint32_t>(s, 0x10) >> 4) & 1) != 0)
    {
        EnterCS(s);
        Field<uint32_t>(s, 0x10) |= 2;
        LeaveCS(s);
        SetEvent_(Field<uint32_t>(s, 0x538));
    }
    if (((Field<uint32_t>(s, 0x10) >> 9) & 1) == 0 || static_cast<uint8_t>(now) != 0)
        return VCall<uint32_t>(s, 0x38 / 4, 1u);
    const uint32_t head = GameGlobal<uint32_t>(0x0063a0d8);
    Field<uint32_t>(s, 0x544) = head;
    GameGlobal<uint32_t>(0x0063a0d8) = u(s);
    return head;
}

// 0x004e6650 (thread start routine, stdcall (streamer); ret 4): FileStream_ThreadLoop (0x004e6740, esi = streamer;
// ECX is passed through as found). Started by 0x004e5f90.
uint32_t fn_004e6650(uint32_t ecx, uint32_t param)
{
    return orig::fn_004e6740(ecx, param);
}

// 0x004e6660 (ecx = file streamer): IsFinished, slot 13 of 0x0058b0f8, under the lock: 0 while an open is pending
// (flag 4), 1 on error (0x40), 0 while another file is queued (virtual +0x40 with 0), else flags bit 0.
uint32_t fn_004e6660(uint8_t* s)
{
    EnterCS(s);
    const uint32_t flags = Field<uint32_t>(s, 0x10);
    uint8_t r;
    if ((flags & 4) != 0)
        r = 0;
    else if ((flags & 0x40) != 0)
        r = 1;
    else if (static_cast<uint8_t>(VCall<uint32_t>(s, 0x40 / 4, 0u)) != 0)
        r = 0;
    else
        r = Field<uint8_t>(s, 0x10) & 1;
    LeaveCS(s);
    return r;
}

// 0x004e66c0 (ecx = file streamer): slot 15 of 0x0058b0f8 (guess: Update): with stop (flag 2) AL = 0; when idle or
// in error (flags & 0x41): an open request (flag 4) runs AsyncFile_Open (0x004e6380), else virtual +0x40(1) and,
// when it queued nothing, waits on +0x538; AL = 0. Otherwise AL = virtual +8 (block empty?), waiting on +0x538
// when 0.
uint32_t fn_004e66c0(uint8_t* s)
{
    EnterCS(s);
    const uint32_t flags = Field<uint32_t>(s, 0x10);
    LeaveCS(s);
    if ((flags & 2) != 0)
        return 0;
    if ((flags & 0x41) != 0)
    {
        if ((flags & 4) != 0)
        {
            orig::fn_004e6380(u(s));
            return 0;
        }
        if (static_cast<uint8_t>(VCall<uint32_t>(s, 0x40 / 4, 1u)) == 0)
            Wait_(Field<uint32_t>(s, 0x538));
        return 0;
    }
    if (static_cast<uint8_t>(VCall<uint32_t>(s, 8 / 4)) != 0)
        return 1;
    Wait_(Field<uint32_t>(s, 0x538));
    return 0;
}

// 0x004e6810 (ecx = file streamer; stack: size, destination; ret 8): Read, slot 1 of 0x0058b0f8: 0x004e5ba0 then
// Sleep(0). Returns the bytes read.
uint32_t fn_004e6810(uint8_t* s, uint32_t size, uint32_t dest)
{
    const uint32_t r = fn_004e5ba0(s, size, dest);
    Sleep_(0);
    return r;
}

// 0x004e6830 (ecx = file streamer): slot 2 of 0x0058b0f8: AL = write block empty (read pos == fill), under the
// lock.
uint32_t fn_004e6830(uint8_t* s)
{
    EnterCS(s);
    uint8_t* b = Block(s, Field<uint8_t>(s, 0xd));
    const uint8_t r = Field<uint32_t>(b, 4) == Field<uint32_t>(b, 8) ? 1 : 0;
    LeaveCS(s);
    return r;
}

// 0x004e6870 (ecx = file streamer; stack: n; ret 4): CommitWrite, slot 11 of 0x0058b0f8: under the lock, unless a
// reset is pending (flag 0x80) 0x004e5d60(n); signals +0x53c.
uint32_t fn_004e6870(uint8_t* s, uint32_t n)
{
    EnterCS(s);
    if (((Field<uint32_t>(s, 0x10) >> 7) & 1) == 0)
        fn_004e5d60(s, n);
    SetEvent_(Field<uint32_t>(s, 0x53c));
    LeaveCS(s);
    return 0;
}

// 0x004e68b0 (ecx = file streamer; stack: n; ret 4): Consume, slot 8 of 0x0058b0f8: the base Consume (as
// 0x004e5c80) under the lock, signals +0x538, then the progress callback +0xe4 (cdecl (+0xe8, read total)).
uint32_t fn_004e68b0(uint8_t* s, uint32_t n)
{
    EnterCS(s);
    Field<uint32_t>(s, 0x14) += n;
    Field<uint32_t>(Block(s, Field<uint8_t>(s, 0xe)), 4) += n;
    Field<uint32_t>(s, 0x1c) -= n;
    const int32_t idx = Field<uint8_t>(s, 0xe);
    uint8_t* b = Block(s, idx);
    if (Field<uint32_t>(b, 4) == Field<uint32_t>(b, 8))
        Field<uint8_t>(s, 0xe) = static_cast<uint8_t>((idx + 1) % static_cast<int32_t>(Field<uint8_t>(s, 0xc)));
    SetEvent_(Field<uint32_t>(s, 0x538));
    LeaveCS(s);
    const uint32_t cb = Field<uint32_t>(s, 0xe4);
    if (cb != 0)
        return reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t)>(cb)(Field<uint32_t>(s, 0xe8),
                                                                          Field<uint32_t>(s, 0x14));
    return cb;
}

// 0x004e6940 (ecx = file streamer; stack: out pointer; ret 4): GetReadPointer, slot 7 of 0x0058b0f8: blocking:
// under the lock takes the read block (as 0x004e5cc0) and asks virtual +0x34 (finished); returns the bytes
// available, 0 when none and finished, else waits on +0x53c and retries.
uint32_t fn_004e6940(uint8_t* s, uint32_t* out)
{
    for (;;)
    {
        EnterCS(s);
        uint8_t* b = Block(s, Field<uint8_t>(s, 0xe));
        uint32_t avail = 0;
        const uint32_t pos = Field<uint32_t>(b, 4);
        if (pos != Field<uint32_t>(b, 8))
        {
            *out = Field<uint32_t>(b, 0) + pos;
            avail = Field<uint32_t>(b, 8) - Field<uint32_t>(b, 4);
        }
        const uint8_t finished = static_cast<uint8_t>(VCall<uint32_t>(s, 0x34 / 4));
        LeaveCS(s);
        if (avail != 0)
            return avail;
        if (finished != 0)
            return 0;
        Wait_(Field<uint32_t>(s, 0x53c));
    }
}

// 0x004e69c0 (ecx = file streamer): Reset, slot 5 of 0x0058b0f8: under the lock (taken twice) flags |= 0x80, the
// base Reset (0x004e5de0), signals +0x538. Not executed in recorded runs.
uint32_t fn_004e69c0(uint8_t* s)
{
    EnterCS(s);
    EnterCS(s);
    Field<uint32_t>(s, 0x10) |= 0x80;
    LeaveCS(s);
    fn_004e5de0(s);
    SetEvent_(Field<uint32_t>(s, 0x538));
    LeaveCS(s);
    return 0;
}

// 0x004e6a10 (ecx = file streamer; stack: name, mode, byte write, byte reset; ret 0x10): Open, slot 4 of
// 0x0058b0f8: mode +0xec = (write ? (mode == 1 || mode == 3) : mode); under the lock: optional reset (virtual
// +0x14), name copied to +0x1f8 (strncpy 0x104, terminated at +0x2fb), flag 8 = write, flag 4 (open request);
// signals +0x538. Not executed in recorded runs.
uint32_t fn_004e6a10(uint8_t* s, uint32_t name, uint32_t mode, uint32_t write, uint32_t reset)
{
    uint32_t m;
    if (static_cast<uint8_t>(write) != 0)
        m = (mode == 1 || mode == 3) ? 1 : 0;
    else
        m = mode;
    EnterCS(s);
    if (static_cast<uint8_t>(reset) != 0)
        VCall<uint32_t>(s, 0x14 / 4);
    Field<uint32_t>(s, 0xec) = m;
    if (name != 0)
    {
        GameFunction<char*(__cdecl*)(char*, const char*, size_t)>(0x005382a0)(
            reinterpret_cast<char*>(s + 0x1f8), reinterpret_cast<const char*>(name), 0x104);
        Field<uint8_t>(s, 0x2fb) = 0;
    }
    EnterCS(s);
    uint32_t flags = Field<uint32_t>(s, 0x10);
    if (static_cast<uint8_t>(write) != 0)
        flags |= 8;
    else
        flags &= 0xfffffff7u;
    Field<uint32_t>(s, 0x10) = flags;
    LeaveCS(s);
    EnterCS(s);
    Field<uint32_t>(s, 0x10) |= 4;
    LeaveCS(s);
    LeaveCS(s);
    return SetEvent_(Field<uint32_t>(s, 0x538));
}

// 0x004e6ad0 (ecx = file streamer): the name buffer +0x1f8. Slot 6 of 0x0058b0f8. Not executed in recorded runs.
uint32_t fn_004e6ad0(uint8_t* s)
{
    return u(s + 0x1f8);
}

// 0x004e6ae0 (ecx = file streamer): +0x1c (bytes buffered) read under the lock. Slot 3 of 0x0058b0f8.
uint32_t fn_004e6ae0(uint8_t* s)
{
    EnterCS(s);
    const uint32_t v = Field<uint32_t>(s, 0x1c);
    LeaveCS(s);
    return v;
}

// 0x004e6b00 (ecx = file streamer; stack: byte open; ret 4): NextFile, slot 16 of 0x0058b0f8: under the lock, when
// the file list cursor +0x510 points at another name and there is no error: when `open`, Open (virtual +0x10:
// name, +0x514, 0, 0) and advances the cursor; AL = 1. Else AL = 0.
uint32_t fn_004e6b00(uint8_t* s, uint32_t open)
{
    uint8_t r = 0;
    EnterCS(s);
    const uint32_t* cursor = Field<uint32_t*>(s, 0x510);
    if (cursor != nullptr)
    {
        const uint32_t name = *cursor;
        if (name != 0 && ((Field<uint32_t>(s, 0x10) >> 6) & 1) == 0)
        {
            if (static_cast<uint8_t>(open) != 0)
            {
                VCall<uint32_t>(s, 0x10 / 4, name, Field<uint32_t>(s, 0x514), 0u, 0u);
                Field<uint32_t>(s, 0x510) += 4;
            }
            r = 1;
        }
    }
    LeaveCS(s);
    return r;
}

// 0x004e6b70 (al = pause, esi = file streamer): pause counter +0x564: pausing increments it and, on the first
// pause, Sleep(20); resuming decrements it (when above 0) and signals +0x538 when it reaches 0. Returns count != 0.
// Caller 0x0044ad50. Not executed in recorded runs.
uint32_t fn_004e6b70(uint32_t eax, uint8_t* s)
{
    if (static_cast<uint8_t>(eax) != 0)
    {
        const uint32_t c = Field<uint32_t>(s, 0x564) + 1;
        Field<uint32_t>(s, 0x564) = c;
        if (c == 1)
            Sleep_(0x14);
        return Field<uint32_t>(s, 0x564) != 0 ? 1u : 0u;
    }
    uint32_t c = Field<uint32_t>(s, 0x564);
    if (c != 0)
    {
        --c;
        Field<uint32_t>(s, 0x564) = c;
        if (c == 0)
            SetEvent_(Field<uint32_t>(s, 0x538));
    }
    return Field<uint32_t>(s, 0x564) != 0 ? 1u : 0u;
}

// 0x004e8430 (eax = load context, edi = 0xc-byte header + data destination): reads a 12-byte header into the
// destination (first dword = total size), then the remaining size - 12 bytes after it (ChunkRead), then
// 0x004e84e0 (ecx = destination; 0, 0). Caller ObjectSystem_LoadAnims (0x0045b430).
uint32_t fn_004e8430(uint32_t eax, uint32_t* out)
{
    uint32_t header[3];
    uint8_t* ctx = B(eax);
    ChunkRead(ctx, 0xc, u(header), 0);
    out[0] = header[0];
    out[1] = header[1];
    out[2] = header[2];
    ChunkRead(ctx, header[0] - 0xc, u(out + 3), 0);
    return orig::fn_004e84e0(u(out), 0, 0);
}

// ================================================================================================================
// ABI adapters, entry stubs and hooks
// ================================================================================================================

// 0x0045d4e0 ends in a tail jump, so tools/abi.py generated no stub ("no ret found"): EBX input, no stack arguments.
#define L01_ENTRY_EBX(stub, impl)                                                                                      \
    static __declspec(naked) void stub()                                                                               \
    {                                                                                                                  \
        __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push ebx       \
        __asm call impl __asm add esp, 4 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx         \
        __asm pop ecx __asm ret                                                                                        \
    }

static uint32_t fn_004479d0_Abi(uint32_t ecx) { return fn_004479d0(B(ecx)); }
static uint32_t fn_004479e0_Abi(uint32_t eax) { return fn_004479e0(reinterpret_cast<uint32_t*>(eax)); }
static uint32_t fn_00447bb0_Abi(uint32_t a0) { return fn_00447bb0(a0); }
static uint32_t fn_00447c70_Abi(uint32_t esi) { return fn_00447c70(B(esi)); }
static uint32_t fn_00447cb0_Abi(uint32_t eax, uint32_t edi)
{
    return fn_00447cb0(B(eax), reinterpret_cast<uint32_t*>(edi));
}
static uint32_t fn_004483e0_Abi(uint32_t edx) { return fn_004483e0(B(edx)); }
static uint32_t fn_00448400_Abi(uint32_t edx) { return fn_00448400(B(edx)); }
static uint32_t fn_00448460_Abi(uint32_t eax) { return fn_00448460(reinterpret_cast<uint32_t*>(eax)); }
static uint32_t fn_00448c70_Abi(uint32_t edi) { return fn_00448c70(B(edi)); }
static uint32_t fn_00448d60_Abi(uint32_t eax, uint32_t esi, uint32_t a0) { return fn_00448d60(eax, B(esi), a0); }
static float fn_00449440_Abi(uint32_t eax) { return fn_00449440(B(eax)); }
static uint32_t fn_00449450_Abi(uint32_t eax, uint32_t ecx)
{
    fn_00449450(reinterpret_cast<uint32_t*>(eax), B(ecx));
    return 0;
}
static uint32_t fn_00449510_Abi(uint32_t edx) { return fn_00449510(B(edx)); }
static uint32_t fn_004497f0_Abi(uint32_t edx) { return fn_004497f0(B(edx)); }
static uint32_t fn_00449830_Abi(uint32_t edi) { return fn_00449830(B(edi)); }
static uint32_t Level_Load_Abi(uint32_t /*ecx*/, uint32_t a0, uint32_t a1) { return Level_Load(a0, a1); }
static uint32_t fn_0044a110_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t a0) { return fn_0044a110(B(eax), a0); }
static uint32_t fn_0044a9d0_Abi(uint32_t eax, uint32_t ecx) { return fn_0044a9d0(eax, ecx); }
static uint32_t fn_0044aa80_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0044aa80(eax, a0, a1, a2, a3);
}
static uint32_t fn_0044ab90_Abi(uint32_t eax, uint32_t ecx) { return fn_0044ab90(eax, ecx); }
static uint32_t fn_0044ac10_Abi(uint32_t eax, uint32_t edi) { return fn_0044ac10(eax, B(edi)); }
static uint32_t fn_0044acc0_Abi(uint32_t esi) { return fn_0044acc0(B(esi)); }
static uint32_t fn_0044ad20_Abi(uint32_t eax, uint32_t edi) { return fn_0044ad20(eax, B(edi)); }
static uint32_t fn_0045a6c0_Abi(uint32_t edi) { return fn_0045a6c0(B(edi)); }
static uint32_t fn_0045a740_Abi(uint32_t ebx, uint32_t esi, uint32_t a0) { return fn_0045a740(ebx, esi, B(a0)); }
static uint32_t fn_0045a860_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0045a860(a0, a1, a2, a3);
}
static uint32_t fn_0045aad0_Abi(uint32_t eax, uint32_t a0, uint32_t a1) { return fn_0045aad0(eax, a0, a1); }
static uint32_t fn_0045abe0_Abi(uint32_t ecx, uint32_t esi) { return fn_0045abe0(ecx, B(esi)); }
static uint32_t fn_0045ac10_Abi(uint32_t eax, uint32_t ecx) { return fn_0045ac10(eax, B(ecx)); }
static uint32_t fn_0045ac30_Abi(uint32_t ecx, uint32_t esi) { return fn_0045ac30(B(ecx), esi); }
static uint32_t fn_0045ac60_Abi(uint32_t edx, uint32_t esi) { return fn_0045ac60(B(edx), esi); }
static uint32_t fn_0045ac90_Abi(uint32_t eax, uint32_t ecx) { return fn_0045ac90(eax, B(ecx)); }
static uint32_t fn_0045acb0_Abi(uint32_t ecx, uint32_t a0) { return fn_0045acb0(ecx, B(a0)); }
static uint32_t fn_0045acd0_Abi(uint32_t eax, uint32_t ecx) { return fn_0045acd0(eax, B(ecx)); }
static uint32_t fn_0045ace0_Abi(uint32_t ecx, uint32_t esi) { return fn_0045ace0(ecx, B(esi)); }
static uint32_t fn_0045ad90_Abi(uint32_t eax) { return fn_0045ad90(B(eax)); }
static uint32_t fn_0045add0_Abi(uint32_t a0) { return fn_0045add0(B(a0)); }
static uint32_t fn_0045b190_Abi(uint32_t eax, uint32_t ebx, uint32_t a0) { return fn_0045b190(B(eax), ebx, a0); }
static uint32_t fn_0045b250_Abi(uint32_t a0) { return fn_0045b250(B(a0)); }
static uint32_t fn_0045b6f0_Abi(uint32_t esi) { return fn_0045b6f0(B(esi)); }
static uint32_t fn_0045b9a0_Abi(uint32_t /*ecx*/, uint32_t edi) { return fn_0045b9a0(B(edi)); }
static uint32_t fn_0045bb60_Abi(uint32_t a0, uint32_t a1) { return fn_0045bb60(a0, B(a1)); }
static uint32_t fn_0045bf10_Abi(uint32_t eax, uint32_t edx, uint32_t a0) { return fn_0045bf10(B(eax), edx, a0); }
static uint32_t fn_0045d4a0_Abi(uint32_t eax) { return fn_0045d4a0(B(eax)); }
static uint32_t fn_0045d4e0_Abi(uint32_t ebx) { return fn_0045d4e0(B(ebx)); }
static uint32_t fn_0045d510_Abi(uint32_t eax, uint32_t edi, uint32_t a0) { return fn_0045d510(eax, edi, a0); }
static uint32_t fn_00462930_Abi(uint32_t esi) { return fn_00462930(B(esi)); }
static uint32_t fn_00462950_Abi(uint32_t eax, uint32_t edi, uint32_t a0) { return fn_00462950(eax, edi, a0); }
static uint32_t PathGrid_Build_Abi(uint32_t a0) { return PathGrid_Build(B(a0)); }
static uint32_t fn_00488180_Abi(uint32_t ecx, uint32_t esi, uint32_t edi) { return fn_00488180(ecx, B(esi), edi); }
static uint32_t TextureBank_Find_Abi(uint32_t ecx, uint32_t esi, uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/,
                                     uint32_t /*a3*/, uint32_t /*a4*/, uint32_t /*a5*/, uint32_t /*a6*/,
                                     uint32_t /*a7*/)
{
    return TextureBank_Find(ecx, B(esi));
}
static uint32_t fn_00488880_Abi(uint32_t a0) { return fn_00488880(a0); }
static uint32_t Font_Load_Abi(uint32_t eax, uint32_t ecx, uint32_t a0) { return Font_Load(eax, ecx, a0); }
static uint32_t fn_00489630_Abi(uint32_t eax, uint32_t ecx) { return fn_00489630(eax, B(ecx)); }
static uint32_t fn_00489680_Abi(uint32_t ebx, uint32_t a0) { return fn_00489680(B(ebx), a0); }
static uint32_t fn_00489770_Abi(uint32_t esi, uint32_t a0) { return fn_00489770(B(esi), a0); }
static uint32_t RenderModes_Load_Abi(uint32_t eax, uint32_t ecx, uint32_t a0) { return RenderModes_Load(eax, ecx, a0); }
static uint32_t RenderSystem_GetClass_Abi() { return RenderSystem_GetClass(); }
static uint32_t fn_0048a6d0_Abi() { return fn_0048a6d0(); }
static uint32_t fn_0048a6e0_Abi(uint32_t eax)
{
    fn_0048a6e0(reinterpret_cast<uint32_t*>(eax));
    return 0;
}
static uint32_t fn_0048a6f0_Abi(uint32_t eax) { return fn_0048a6f0(B(eax)); }
static uint32_t fn_0048a770_Abi(uint32_t esi) { return fn_0048a770(B(esi)); }
static uint32_t fn_0048a830_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi)
{
    return fn_0048a830(eax, ecx, reinterpret_cast<const char*>(edx), B(esi));
}
static uint32_t fn_0048a8b0_Abi(uint32_t ecx, uint32_t esi) { return fn_0048a8b0(ecx, B(esi)); }
static uint32_t fn_0048aab0_Abi(uint32_t eax) { return fn_0048aab0(B(eax)); }
static uint32_t fn_0048b070_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0048b070(reinterpret_cast<const wchar_t*>(eax), ecx);
}
static uint32_t fn_0048b0a0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0) { return fn_0048b0a0(eax, ecx, a0); }
static uint32_t fn_0048b130_Abi(uint32_t eax) { return fn_0048b130(B(eax)); }
static uint32_t fn_00495ad0_Abi() { return fn_00495ad0(); }
static uint32_t fn_00495ae0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00495ae0(eax, B(ecx), a0, a1);
}
static uint32_t fn_00495ff0_Abi(uint32_t esi) { return fn_00495ff0(B(esi)); }
static uint32_t fn_004e1ad0_Abi(uint32_t edi, uint32_t a0)
{
    return fn_004e1ad0(reinterpret_cast<RelocCtx01*>(edi), B(a0));
}
static uint32_t fn_004e1db0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0) { return fn_004e1db0(eax, ecx, a0); }
static uint32_t fn_004e5aa0_Abi(uint32_t ecx, uint32_t a0) { return fn_004e5aa0(B(ecx), a0); }
static uint32_t fn_004e5ae0_Abi(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1)
{
    return fn_004e5ae0(B(eax), ebx, a0, a1);
}
static uint32_t fn_004e5b10_Abi(uint32_t ecx, uint32_t a0) { return fn_004e5b10(B(ecx), a0); }
static uint32_t fn_004e5b30_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t esi)
{
    return fn_004e5b30(eax, ecx, ebx, B(esi));
}
static uint32_t fn_004e5ba0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1) { return fn_004e5ba0(B(ecx), a0, a1); }
static uint32_t fn_004e5c30_Abi(uint32_t ecx) { return fn_004e5c30(B(ecx)); }
static uint32_t fn_004e5c50_Abi(uint32_t ecx) { return fn_004e5c50(B(ecx)); }
static uint32_t fn_004e5c60_Abi(uint32_t ecx) { return fn_004e5c60(B(ecx)); }
static uint32_t fn_004e5c70_Abi(uint32_t ecx) { return fn_004e5c70(B(ecx)); }
static uint32_t fn_004e5c80_Abi(uint32_t ecx, uint32_t a0) { return fn_004e5c80(B(ecx), a0); }
static uint32_t fn_004e5cc0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004e5cc0(B(ecx), reinterpret_cast<uint32_t*>(a0));
}
static uint32_t fn_004e5d60_Abi(uint32_t ecx, uint32_t a0) { return fn_004e5d60(B(ecx), a0); }
static uint32_t fn_004e5dc0_Abi(uint32_t ecx) { return fn_004e5dc0(B(ecx)); }
static uint32_t fn_004e5dd0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    fn_004e5dd0(B(ecx), a0, a1, a2, a3);
    return 0;
}
static uint32_t fn_004e5de0_Abi(uint32_t ecx) { return fn_004e5de0(B(ecx)); }
static uint32_t fn_004e5e30_Abi(uint32_t ecx, uint32_t a0) { return fn_004e5e30(B(ecx), a0); }
static uint32_t fn_004e5e50_Abi(uint32_t esi) { return fn_004e5e50(B(esi)); }
static uint32_t fn_004e5e80_Abi(uint32_t ecx, uint32_t edx, uint32_t esi) { return fn_004e5e80(B(ecx), edx, esi); }
static uint32_t fn_004e5ec0_Abi(uint32_t ebx) { return fn_004e5ec0(ebx); }
static uint32_t fn_004e5f10_Abi(uint32_t ecx, uint32_t a0) { return fn_004e5f10(B(ecx), a0); }
static uint32_t fn_004e5f30_Abi(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1)
{
    return fn_004e5f30(B(eax), ebx, a0, a1);
}
static uint32_t fn_004e5f90_Abi(uint32_t esi) { return fn_004e5f90(B(esi)); }
static uint32_t fn_004e6070_Abi(uint32_t esi) { return fn_004e6070(B(esi)); }
static uint32_t fn_004e6120_Abi(uint32_t ecx, uint32_t a0) { return fn_004e6120(B(ecx), a0); }
static uint32_t fn_004e6650_Abi(uint32_t ecx, uint32_t a0) { return fn_004e6650(ecx, a0); }
static uint32_t fn_004e6660_Abi(uint32_t ecx) { return fn_004e6660(B(ecx)); }
static uint32_t fn_004e66c0_Abi(uint32_t ecx) { return fn_004e66c0(B(ecx)); }
static uint32_t fn_004e6810_Abi(uint32_t ecx, uint32_t a0, uint32_t a1) { return fn_004e6810(B(ecx), a0, a1); }
static uint32_t fn_004e6830_Abi(uint32_t ecx) { return fn_004e6830(B(ecx)); }
static uint32_t fn_004e6870_Abi(uint32_t ecx, uint32_t a0) { return fn_004e6870(B(ecx), a0); }
static uint32_t fn_004e68b0_Abi(uint32_t ecx, uint32_t a0) { return fn_004e68b0(B(ecx), a0); }
static uint32_t fn_004e6940_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004e6940(B(ecx), reinterpret_cast<uint32_t*>(a0));
}
static uint32_t fn_004e69c0_Abi(uint32_t ecx) { return fn_004e69c0(B(ecx)); }
static uint32_t fn_004e6a10_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004e6a10(B(ecx), a0, a1, a2, a3);
}
static uint32_t fn_004e6ad0_Abi(uint32_t ecx) { return fn_004e6ad0(B(ecx)); }
static uint32_t fn_004e6ae0_Abi(uint32_t ecx) { return fn_004e6ae0(B(ecx)); }
static uint32_t fn_004e6b00_Abi(uint32_t ecx, uint32_t a0) { return fn_004e6b00(B(ecx), a0); }
static uint32_t fn_004e6b70_Abi(uint32_t eax, uint32_t esi) { return fn_004e6b70(eax, B(esi)); }
static uint32_t fn_004e8430_Abi(uint32_t eax, uint32_t edi) { return fn_004e8430(eax, reinterpret_cast<uint32_t*>(edi)); }

ABI_ENTRY_004479d0(fn_004479d0_Entry, fn_004479d0_Abi)
ABI_ENTRY_004479e0(fn_004479e0_Entry, fn_004479e0_Abi)
ABI_ENTRY_00447bb0(fn_00447bb0_Entry, fn_00447bb0_Abi)
ABI_ENTRY_00447c70(fn_00447c70_Entry, fn_00447c70_Abi)
ABI_ENTRY_00447cb0(fn_00447cb0_Entry, fn_00447cb0_Abi)
ABI_ENTRY_004483e0(fn_004483e0_Entry, fn_004483e0_Abi)
ABI_ENTRY_00448400(fn_00448400_Entry, fn_00448400_Abi)
ABI_ENTRY_00448460(fn_00448460_Entry, fn_00448460_Abi)
ABI_ENTRY_00448c70(fn_00448c70_Entry, fn_00448c70_Abi)
ABI_ENTRY_00448d60(fn_00448d60_Entry, fn_00448d60_Abi)
ABI_ENTRY_00449440(fn_00449440_Entry, fn_00449440_Abi)
ABI_ENTRY_00449450(fn_00449450_Entry, fn_00449450_Abi)
ABI_ENTRY_00449510(fn_00449510_Entry, fn_00449510_Abi)
ABI_ENTRY_004497f0(fn_004497f0_Entry, fn_004497f0_Abi)
ABI_ENTRY_00449830(fn_00449830_Entry, fn_00449830_Abi)
ABI_ENTRY_00449e80(Level_Load_Entry, Level_Load_Abi)
ABI_ENTRY_0044a110(fn_0044a110_Entry, fn_0044a110_Abi)
ABI_ENTRY_0044a9d0(fn_0044a9d0_Entry, fn_0044a9d0_Abi)
ABI_ENTRY_0044aa80(fn_0044aa80_Entry, fn_0044aa80_Abi)
ABI_ENTRY_0044ab90(fn_0044ab90_Entry, fn_0044ab90_Abi)
ABI_ENTRY_0044ac10(fn_0044ac10_Entry, fn_0044ac10_Abi)
ABI_ENTRY_0044acc0(fn_0044acc0_Entry, fn_0044acc0_Abi)
ABI_ENTRY_0044ad20(fn_0044ad20_Entry, fn_0044ad20_Abi)
ABI_ENTRY_0045a6c0(fn_0045a6c0_Entry, fn_0045a6c0_Abi)
ABI_ENTRY_0045a740(fn_0045a740_Entry, fn_0045a740_Abi)
ABI_ENTRY_0045a860(fn_0045a860_Entry, fn_0045a860_Abi)
ABI_ENTRY_0045aad0(fn_0045aad0_Entry, fn_0045aad0_Abi)
ABI_ENTRY_0045abe0(fn_0045abe0_Entry, fn_0045abe0_Abi)
ABI_ENTRY_0045ac10(fn_0045ac10_Entry, fn_0045ac10_Abi)
ABI_ENTRY_0045ac30(fn_0045ac30_Entry, fn_0045ac30_Abi)
ABI_ENTRY_0045ac60(fn_0045ac60_Entry, fn_0045ac60_Abi)
ABI_ENTRY_0045ac90(fn_0045ac90_Entry, fn_0045ac90_Abi)
ABI_ENTRY_0045acb0(fn_0045acb0_Entry, fn_0045acb0_Abi)
ABI_ENTRY_0045acd0(fn_0045acd0_Entry, fn_0045acd0_Abi)
ABI_ENTRY_0045ace0(fn_0045ace0_Entry, fn_0045ace0_Abi)
ABI_ENTRY_0045ad90(fn_0045ad90_Entry, fn_0045ad90_Abi)
ABI_ENTRY_0045add0(fn_0045add0_Entry, fn_0045add0_Abi)
ABI_ENTRY_0045b190(fn_0045b190_Entry, fn_0045b190_Abi)
ABI_ENTRY_0045b250(fn_0045b250_Entry, fn_0045b250_Abi)
ABI_ENTRY_0045b6f0(fn_0045b6f0_Entry, fn_0045b6f0_Abi)
ABI_ENTRY_0045b9a0(fn_0045b9a0_Entry, fn_0045b9a0_Abi)
ABI_ENTRY_0045bb60(fn_0045bb60_Entry, fn_0045bb60_Abi)
ABI_ENTRY_0045bf10(fn_0045bf10_Entry, fn_0045bf10_Abi)
ABI_ENTRY_0045d4a0(fn_0045d4a0_Entry, fn_0045d4a0_Abi)
L01_ENTRY_EBX(fn_0045d4e0_Entry, fn_0045d4e0_Abi)
ABI_ENTRY_0045d510(fn_0045d510_Entry, fn_0045d510_Abi)
ABI_ENTRY_00462930(fn_00462930_Entry, fn_00462930_Abi)
ABI_ENTRY_00462950(fn_00462950_Entry, fn_00462950_Abi)
ABI_ENTRY_0046ed30(PathGrid_Build_Entry, PathGrid_Build_Abi)
ABI_ENTRY_00488180(fn_00488180_Entry, fn_00488180_Abi)
ABI_ENTRY_004881c0(TextureBank_Find_Entry, TextureBank_Find_Abi)
ABI_ENTRY_00488880(fn_00488880_Entry, fn_00488880_Abi)
ABI_ENTRY_004888b0(Font_Load_Entry, Font_Load_Abi)
ABI_ENTRY_00489630(fn_00489630_Entry, fn_00489630_Abi)
ABI_ENTRY_00489680(fn_00489680_Entry, fn_00489680_Abi)
ABI_ENTRY_00489770(fn_00489770_Entry, fn_00489770_Abi)
ABI_ENTRY_00489b40(RenderModes_Load_Entry, RenderModes_Load_Abi)
ABI_ENTRY_0048a6c0(RenderSystem_GetClass_Entry, RenderSystem_GetClass_Abi)
ABI_ENTRY_0048a6d0(fn_0048a6d0_Entry, fn_0048a6d0_Abi)
ABI_ENTRY_0048a6e0(fn_0048a6e0_Entry, fn_0048a6e0_Abi)
ABI_ENTRY_0048a6f0(fn_0048a6f0_Entry, fn_0048a6f0_Abi)
ABI_ENTRY_0048a770(fn_0048a770_Entry, fn_0048a770_Abi)
ABI_ENTRY_0048a830(fn_0048a830_Entry, fn_0048a830_Abi)
ABI_ENTRY_0048a8b0(fn_0048a8b0_Entry, fn_0048a8b0_Abi)
ABI_ENTRY_0048aab0(fn_0048aab0_Entry, fn_0048aab0_Abi)
ABI_ENTRY_0048b070(fn_0048b070_Entry, fn_0048b070_Abi)
ABI_ENTRY_0048b0a0(fn_0048b0a0_Entry, fn_0048b0a0_Abi)
ABI_ENTRY_0048b130(fn_0048b130_Entry, fn_0048b130_Abi)
ABI_ENTRY_00495ad0(fn_00495ad0_Entry, fn_00495ad0_Abi)
ABI_ENTRY_00495ae0(fn_00495ae0_Entry, fn_00495ae0_Abi)
ABI_ENTRY_00495ff0(fn_00495ff0_Entry, fn_00495ff0_Abi)
ABI_ENTRY_004e1ad0(fn_004e1ad0_Entry, fn_004e1ad0_Abi)
ABI_ENTRY_004e1db0(fn_004e1db0_Entry, fn_004e1db0_Abi)
ABI_ENTRY_004e5aa0(fn_004e5aa0_Entry, fn_004e5aa0_Abi)
ABI_ENTRY_004e5ae0(fn_004e5ae0_Entry, fn_004e5ae0_Abi)
ABI_ENTRY_004e5b10(fn_004e5b10_Entry, fn_004e5b10_Abi)
ABI_ENTRY_004e5b30(fn_004e5b30_Entry, fn_004e5b30_Abi)
ABI_ENTRY_004e5ba0(fn_004e5ba0_Entry, fn_004e5ba0_Abi)
ABI_ENTRY_004e5c30(fn_004e5c30_Entry, fn_004e5c30_Abi)
ABI_ENTRY_004e5c50(fn_004e5c50_Entry, fn_004e5c50_Abi)
ABI_ENTRY_004e5c60(fn_004e5c60_Entry, fn_004e5c60_Abi)
ABI_ENTRY_004e5c70(fn_004e5c70_Entry, fn_004e5c70_Abi)
ABI_ENTRY_004e5c80(fn_004e5c80_Entry, fn_004e5c80_Abi)
ABI_ENTRY_004e5cc0(fn_004e5cc0_Entry, fn_004e5cc0_Abi)
ABI_ENTRY_004e5d60(fn_004e5d60_Entry, fn_004e5d60_Abi)
ABI_ENTRY_004e5dc0(fn_004e5dc0_Entry, fn_004e5dc0_Abi)
ABI_ENTRY_004e5dd0(fn_004e5dd0_Entry, fn_004e5dd0_Abi)
ABI_ENTRY_004e5de0(fn_004e5de0_Entry, fn_004e5de0_Abi)
ABI_ENTRY_004e5e30(fn_004e5e30_Entry, fn_004e5e30_Abi)
ABI_ENTRY_004e5e50(fn_004e5e50_Entry, fn_004e5e50_Abi)
ABI_ENTRY_004e5e80(fn_004e5e80_Entry, fn_004e5e80_Abi)
ABI_ENTRY_004e5ec0(fn_004e5ec0_Entry, fn_004e5ec0_Abi)
ABI_ENTRY_004e5f10(fn_004e5f10_Entry, fn_004e5f10_Abi)
ABI_ENTRY_004e5f30(fn_004e5f30_Entry, fn_004e5f30_Abi)
ABI_ENTRY_004e5f90(fn_004e5f90_Entry, fn_004e5f90_Abi)
ABI_ENTRY_004e6070(fn_004e6070_Entry, fn_004e6070_Abi)
ABI_ENTRY_004e6120(fn_004e6120_Entry, fn_004e6120_Abi)
ABI_ENTRY_004e6650(fn_004e6650_Entry, fn_004e6650_Abi)
ABI_ENTRY_004e6660(fn_004e6660_Entry, fn_004e6660_Abi)
ABI_ENTRY_004e66c0(fn_004e66c0_Entry, fn_004e66c0_Abi)
ABI_ENTRY_004e6810(fn_004e6810_Entry, fn_004e6810_Abi)
ABI_ENTRY_004e6830(fn_004e6830_Entry, fn_004e6830_Abi)
ABI_ENTRY_004e6870(fn_004e6870_Entry, fn_004e6870_Abi)
ABI_ENTRY_004e68b0(fn_004e68b0_Entry, fn_004e68b0_Abi)
ABI_ENTRY_004e6940(fn_004e6940_Entry, fn_004e6940_Abi)
ABI_ENTRY_004e69c0(fn_004e69c0_Entry, fn_004e69c0_Abi)
ABI_ENTRY_004e6a10(fn_004e6a10_Entry, fn_004e6a10_Abi)
ABI_ENTRY_004e6ad0(fn_004e6ad0_Entry, fn_004e6ad0_Abi)
ABI_ENTRY_004e6ae0(fn_004e6ae0_Entry, fn_004e6ae0_Abi)
ABI_ENTRY_004e6b00(fn_004e6b00_Entry, fn_004e6b00_Abi)
ABI_ENTRY_004e6b70(fn_004e6b70_Entry, fn_004e6b70_Abi)
ABI_ENTRY_004e8430(fn_004e8430_Entry, fn_004e8430_Abi)

HOOK(0x004479d0, "fn_004479d0", fn_004479d0_Entry, nullptr);
HOOK(0x004479e0, "fn_004479e0", fn_004479e0_Entry, nullptr);
HOOK(0x00447bb0, "fn_00447bb0", fn_00447bb0_Entry, nullptr);
HOOK(0x00447c70, "fn_00447c70", fn_00447c70_Entry, nullptr);
HOOK(0x00447cb0, "fn_00447cb0", fn_00447cb0_Entry, nullptr);
HOOK(0x004483e0, "fn_004483e0", fn_004483e0_Entry, nullptr);
HOOK(0x00448400, "fn_00448400", fn_00448400_Entry, nullptr);
HOOK(0x00448460, "fn_00448460", fn_00448460_Entry, nullptr);
HOOK(0x00448c70, "fn_00448c70", fn_00448c70_Entry, nullptr);
HOOK(0x00448d60, "fn_00448d60", fn_00448d60_Entry, nullptr);
HOOK(0x00449440, "fn_00449440", fn_00449440_Entry, nullptr);
HOOK(0x00449450, "fn_00449450", fn_00449450_Entry, nullptr);
HOOK(0x00449510, "fn_00449510", fn_00449510_Entry, nullptr);
HOOK(0x004497f0, "fn_004497f0", fn_004497f0_Entry, nullptr);
HOOK(0x00449830, "fn_00449830", fn_00449830_Entry, nullptr);
HOOK(0x00449e80, "Level_Load", Level_Load_Entry, nullptr);
HOOK(0x0044a110, "fn_0044a110", fn_0044a110_Entry, nullptr);
HOOK(0x0044a9d0, "fn_0044a9d0", fn_0044a9d0_Entry, nullptr);
HOOK(0x0044aa80, "fn_0044aa80", fn_0044aa80_Entry, nullptr);
HOOK(0x0044ab90, "fn_0044ab90", fn_0044ab90_Entry, nullptr);
HOOK(0x0044ac10, "fn_0044ac10", fn_0044ac10_Entry, nullptr);
HOOK(0x0044acc0, "fn_0044acc0", fn_0044acc0_Entry, nullptr);
HOOK(0x0044ad20, "fn_0044ad20", fn_0044ad20_Entry, nullptr);
HOOK(0x0045a6c0, "fn_0045a6c0", fn_0045a6c0_Entry, nullptr);
HOOK(0x0045a740, "fn_0045a740", fn_0045a740_Entry, nullptr);
HOOK(0x0045a860, "fn_0045a860", fn_0045a860_Entry, nullptr);
HOOK(0x0045aad0, "fn_0045aad0", fn_0045aad0_Entry, nullptr);
HOOK(0x0045abe0, "fn_0045abe0", fn_0045abe0_Entry, nullptr);
HOOK(0x0045ac10, "fn_0045ac10", fn_0045ac10_Entry, nullptr);
HOOK(0x0045ac30, "fn_0045ac30", fn_0045ac30_Entry, nullptr);
HOOK(0x0045ac60, "fn_0045ac60", fn_0045ac60_Entry, nullptr);
HOOK(0x0045ac90, "fn_0045ac90", fn_0045ac90_Entry, nullptr);
HOOK(0x0045acb0, "fn_0045acb0", fn_0045acb0_Entry, nullptr);
HOOK(0x0045acd0, "fn_0045acd0", fn_0045acd0_Entry, nullptr);
HOOK(0x0045ace0, "fn_0045ace0", fn_0045ace0_Entry, nullptr);
HOOK(0x0045ad90, "fn_0045ad90", fn_0045ad90_Entry, nullptr);
HOOK(0x0045add0, "fn_0045add0", fn_0045add0_Entry, nullptr);
HOOK(0x0045b190, "fn_0045b190", fn_0045b190_Entry, nullptr);
HOOK(0x0045b250, "fn_0045b250", fn_0045b250_Entry, nullptr);
HOOK(0x0045b6f0, "fn_0045b6f0", fn_0045b6f0_Entry, nullptr);
HOOK(0x0045b9a0, "fn_0045b9a0", fn_0045b9a0_Entry, nullptr);
HOOK(0x0045bb60, "fn_0045bb60", fn_0045bb60_Entry, nullptr);
HOOK(0x0045bf10, "fn_0045bf10", fn_0045bf10_Entry, nullptr);
HOOK(0x0045d4a0, "fn_0045d4a0", fn_0045d4a0_Entry, nullptr);
HOOK(0x0045d4e0, "fn_0045d4e0", fn_0045d4e0_Entry, nullptr);
HOOK(0x0045d510, "fn_0045d510", fn_0045d510_Entry, nullptr);
HOOK(0x00462930, "fn_00462930", fn_00462930_Entry, nullptr);
HOOK(0x00462950, "fn_00462950", fn_00462950_Entry, nullptr);
HOOK(0x0046ed30, "PathGrid_Build", PathGrid_Build_Entry, nullptr);
HOOK(0x00488180, "fn_00488180", fn_00488180_Entry, nullptr);
HOOK(0x004881c0, "TextureBank_Find", TextureBank_Find_Entry, nullptr);
HOOK(0x00488880, "fn_00488880", fn_00488880_Entry, nullptr);
HOOK(0x004888b0, "Font_Load", Font_Load_Entry, nullptr);
HOOK(0x00489630, "fn_00489630", fn_00489630_Entry, nullptr);
HOOK(0x00489680, "fn_00489680", fn_00489680_Entry, nullptr);
HOOK(0x00489770, "fn_00489770", fn_00489770_Entry, nullptr);
HOOK(0x00489b40, "RenderModes_Load", RenderModes_Load_Entry, nullptr);
HOOK(0x0048a6c0, "RenderSystem_GetClass", RenderSystem_GetClass_Entry, nullptr);
HOOK(0x0048a6d0, "fn_0048a6d0", fn_0048a6d0_Entry, nullptr);
HOOK(0x0048a6e0, "fn_0048a6e0", fn_0048a6e0_Entry, nullptr);
HOOK(0x0048a6f0, "fn_0048a6f0", fn_0048a6f0_Entry, nullptr);
HOOK(0x0048a770, "fn_0048a770", fn_0048a770_Entry, nullptr);
HOOK(0x0048a830, "fn_0048a830", fn_0048a830_Entry, nullptr);
HOOK(0x0048a8b0, "fn_0048a8b0", fn_0048a8b0_Entry, nullptr);
HOOK(0x0048aab0, "fn_0048aab0", fn_0048aab0_Entry, nullptr);
HOOK(0x0048b070, "fn_0048b070", fn_0048b070_Entry, nullptr);
HOOK(0x0048b0a0, "fn_0048b0a0", fn_0048b0a0_Entry, nullptr);
HOOK(0x0048b130, "fn_0048b130", fn_0048b130_Entry, nullptr);
HOOK(0x00495ad0, "fn_00495ad0", fn_00495ad0_Entry, nullptr);
HOOK(0x00495ae0, "fn_00495ae0", fn_00495ae0_Entry, nullptr);
HOOK(0x00495ff0, "fn_00495ff0", fn_00495ff0_Entry, nullptr);
HOOK(0x004e1ad0, "fn_004e1ad0", fn_004e1ad0_Entry, nullptr);
HOOK(0x004e1db0, "fn_004e1db0", fn_004e1db0_Entry, nullptr);
HOOK(0x004e5aa0, "fn_004e5aa0", fn_004e5aa0_Entry, nullptr);
HOOK(0x004e5ae0, "fn_004e5ae0", fn_004e5ae0_Entry, nullptr);
HOOK(0x004e5b10, "fn_004e5b10", fn_004e5b10_Entry, nullptr);
HOOK(0x004e5b30, "fn_004e5b30", fn_004e5b30_Entry, nullptr);
HOOK(0x004e5ba0, "fn_004e5ba0", fn_004e5ba0_Entry, nullptr);
HOOK(0x004e5c30, "fn_004e5c30", fn_004e5c30_Entry, nullptr);
HOOK(0x004e5c50, "fn_004e5c50", fn_004e5c50_Entry, nullptr);
HOOK(0x004e5c60, "fn_004e5c60", fn_004e5c60_Entry, nullptr);
HOOK(0x004e5c70, "fn_004e5c70", fn_004e5c70_Entry, nullptr);
HOOK(0x004e5c80, "fn_004e5c80", fn_004e5c80_Entry, nullptr);
HOOK(0x004e5cc0, "fn_004e5cc0", fn_004e5cc0_Entry, nullptr);
HOOK(0x004e5d60, "fn_004e5d60", fn_004e5d60_Entry, nullptr);
HOOK(0x004e5dc0, "fn_004e5dc0", fn_004e5dc0_Entry, nullptr);
HOOK(0x004e5dd0, "fn_004e5dd0", fn_004e5dd0_Entry, nullptr);
HOOK(0x004e5de0, "fn_004e5de0", fn_004e5de0_Entry, nullptr);
HOOK(0x004e5e30, "fn_004e5e30", fn_004e5e30_Entry, nullptr);
HOOK(0x004e5e50, "fn_004e5e50", fn_004e5e50_Entry, nullptr);
HOOK(0x004e5e80, "fn_004e5e80", fn_004e5e80_Entry, nullptr);
HOOK(0x004e5ec0, "fn_004e5ec0", fn_004e5ec0_Entry, nullptr);
HOOK(0x004e5f10, "fn_004e5f10", fn_004e5f10_Entry, nullptr);
HOOK(0x004e5f30, "fn_004e5f30", fn_004e5f30_Entry, nullptr);
HOOK(0x004e5f90, "fn_004e5f90", fn_004e5f90_Entry, nullptr);
HOOK(0x004e6070, "fn_004e6070", fn_004e6070_Entry, nullptr);
HOOK(0x004e6120, "fn_004e6120", fn_004e6120_Entry, nullptr);
HOOK(0x004e6650, "fn_004e6650", fn_004e6650_Entry, nullptr);
HOOK(0x004e6660, "fn_004e6660", fn_004e6660_Entry, nullptr);
HOOK(0x004e66c0, "fn_004e66c0", fn_004e66c0_Entry, nullptr);
HOOK(0x004e6810, "fn_004e6810", fn_004e6810_Entry, nullptr);
HOOK(0x004e6830, "fn_004e6830", fn_004e6830_Entry, nullptr);
HOOK(0x004e6870, "fn_004e6870", fn_004e6870_Entry, nullptr);
HOOK(0x004e68b0, "fn_004e68b0", fn_004e68b0_Entry, nullptr);
HOOK(0x004e6940, "fn_004e6940", fn_004e6940_Entry, nullptr);
HOOK(0x004e69c0, "fn_004e69c0", fn_004e69c0_Entry, nullptr);
HOOK(0x004e6a10, "fn_004e6a10", fn_004e6a10_Entry, nullptr);
HOOK(0x004e6ad0, "fn_004e6ad0", fn_004e6ad0_Entry, nullptr);
HOOK(0x004e6ae0, "fn_004e6ae0", fn_004e6ae0_Entry, nullptr);
HOOK(0x004e6b00, "fn_004e6b00", fn_004e6b00_Entry, nullptr);
HOOK(0x004e6b70, "fn_004e6b70", fn_004e6b70_Entry, nullptr);
HOOK(0x004e8430, "fn_004e8430", fn_004e8430_Entry, nullptr);

// ================================================================================================================
// 0x0045bf70: object templates (level chunk 0x0bada55)
// ================================================================================================================

namespace
{
// ChunkRead that reports whether the stream read ran and its result (bytes read).
bool ChunkReadR(uint8_t* ctx, uint32_t size, uint32_t dest, uint32_t* result)
{
    void* stream = Field<void*>(ctx, 0x28);
    if (stream == nullptr || ((Field<uint32_t>(stream, 0x10) >> 6) & 1) != 0)
        return false;
    *result = VCall<uint32_t>(stream, 4 / 4, size, dest);
    if (((Field<uint32_t>(Field<uint8_t*>(ctx, 0x28), 0x10) >> 6) & 1) != 0)
    {
        const uint32_t onError = Field<uint32_t>(ctx, 0);
        if (onError != 0)
            reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(onError)(0xffffffffu);
    }
    return true;
}

// Property lookups of a template's property block p ([template+0x3c]): 0x0045dec0 / 0x0045df00 / 0x0045df30
// (eax = [p], ecx = name, esi = [p+8]; [game+0xa84], out). AL = found. guess: string / int-or-float / bool.
inline uint32_t PropString(uint8_t* p, uint32_t name, uint32_t out)
{
    return orig::fn_0045dec0(Field<uint32_t>(p, 0), name, Field<uint32_t>(p, 8), Field<uint32_t>(Gm(), 0xa84), out);
}
inline uint32_t PropValue(uint8_t* p, uint32_t name, uint32_t out)
{
    return orig::fn_0045df00(Field<uint32_t>(p, 0), name, Field<uint32_t>(p, 8), Field<uint32_t>(Gm(), 0xa84), out);
}
inline uint32_t PropBool(uint8_t* p, uint32_t name, uint32_t out)
{
    return orig::fn_0045df30(Field<uint32_t>(p, 0), name, Field<uint32_t>(p, 8), Field<uint32_t>(Gm(), 0xa84), out);
}
inline int Sprintf(uint8_t* buffer, uint32_t format, uint32_t a)
{
    return GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
        reinterpret_cast<char*>(buffer), reinterpret_cast<const char*>(format), a);
}
inline int Sprintf2(uint8_t* buffer, uint32_t format, uint32_t a, uint32_t b)
{
    return GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
        reinterpret_cast<char*>(buffer), reinterpret_cast<const char*>(format), a, b);
}
// PMS_List iterator release (iterator {list, current} at it): list virtual +0xc(current), current+0x18 -= 1.
inline void IterRelease(uint8_t* it)
{
    uint8_t* cur = Field<uint8_t*>(it, 4);
    VCall<uint32_t>(Field<void*>(it, 0), 0xc / 4, cur);
    Field<uint32_t>(cur, 0x18) -= 1;
}
}  // namespace

// 0x0045bf70 (stack: object system [game+0xa84], load context, chunk size; ret 0xc): chunk 0x0bada55, the object
// templates. Reads the name hash (count, "objdef hashtable entries" -> +0xe3c, hash object +0xe38 with compare
// 0x0045b5f0, set up by 0x004f1220), the template count +0xe34 and, into "objdef data" (+0xe44, the rest of the
// chunk), each template (size, then size - 4 bytes; its own hash with compare 0x0045b630). For each 0x50-byte
// record of "obj template list" (+0xe40): +0x10 = template, pointers fixed up (+0x2c/+0x30/+0x34/+0x38), properties
// +0x3c (ObjectSystem_FindProperty), LOD table +0x40 (-1 for the last, the "collision_lod_index" and
// "shadow_lod_index" entries, else 0, then "lodcutoff_%d"), animation data +0xc ("template Animation data",
// ModelSlots_Resolve). Templates whose animations share an anim set are grouped through PMS_Lists (list type of
// 0x004e7010; per-template lists 0x004e6bd0 with compare 0x0045b660 / 0x0045b670) into "anim cache array" entries
// (0x004ee010, with a heap built for the "ref_anim_name" / "%s_ref" reference animation via 0x004ef590), stored in
// +8 (bit 31 and bits 16-30 = group index for a 2-animation template whose second animation is its reference).
// Then "shared cache ptrs" +0xe60 / "shared stamps" +0xe64 (count +0xe68), each record's bounds (+0x14 centre,
// +0x20 radius, +0x24 smallest half extent, +0x28 / +0x34 min / max over its cells' meshes), "scale_total" and
// "template scale data" +0x48 ("scale_value_%d", "scale_x_%d"...), and collision (+0x4c flags 1 "collide_poly",
// 2 "concave", 4 has a collision cell; CollisionSkin_build 0x00412450 per mesh and LOD) inside the 0x004e0bd0
// bracket (busy flag 0x0063a0fc). Caller Level_Load. Frame 0x78 bytes kept as one array (offsets as in the
// original). Its 0x004e0ff0 call passes 8 values; the original's 9th/10th stack words are its caller's saved
// EDI/ESI (not reproducible, passed as 0; 0x004e0ff0 reads 6 arguments as far as seen).
uint32_t ObjectSystem_LoadTemplates(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint8_t f[0x78];
    uint8_t* sys = B(a0);
    uint8_t* ctx = B(a1);
    auto U = [&](uint32_t o) -> uint32_t& { return Field<uint32_t>(f, o); };
    auto Rec = [&](uint32_t off) -> uint8_t* { return Field<uint8_t*>(sys, 0xe40) + off; };
    auto Tmpl = [&](uint32_t off) -> uint8_t* { return Field<uint8_t*>(Rec(off), 0x10); };
    const uint32_t kNew = 0x0058b5e8;
    uint32_t r = 0;

    U(0x34) = 0;
    U(0x38) = 0;
    U(0x1c) = ChunkReadR(ctx, 4, u(f + 0x20), &r) ? r : 0;
    uint8_t* hash = B(orig::fn_004e07f0(0x0063a210u, 0x10, 0x20, 2, kNew));
    if (hash != nullptr)
    {
        Field<uint32_t>(hash, 0) = 0;
        Field<uint32_t>(hash, 4) = 0;
        Field<uint32_t>(hash, 8) = 0;
        Field<uint32_t>(hash, 0x10) = 0x0045b5f0;
        Field<uint32_t>(hash, 0x14) = u(sys);
        Field<uint32_t>(hash, 0x1c) = 0;
    }
    Field<uint32_t>(sys, 0xe38) = u(hash);
    const uint32_t entries = orig::fn_004e07f0(0x0063a210u, 0xc, U(0x20), 2, 0x005b0990);
    Field<uint32_t>(sys, 0xe3c) = entries;
    const uint32_t read2 = ChunkReadR(ctx, U(0x20), entries, &r) ? r : 0;
    orig::fn_004f1220(Field<uint32_t>(sys, 0xe3c), Field<uint32_t>(sys, 0xe38));
    Field<uint32_t>(sys, 0xe44) = orig::fn_004e07f0(0x0063a210u, 0xc, a2 - read2 - U(0x1c), 2, 0x005b0984);
    ChunkReadR(ctx, 4, u(sys + 0xe34), &r);
    const uint32_t list = orig::fn_004e07f0(0x0063a210u, 0xc, Field<uint32_t>(sys, 0xe34) * 0x50, 2, 0x005b0970);
    U(0x1c) = Field<uint32_t>(sys, 0xe44);
    Field<uint32_t>(sys, 0xe40) = list;
    const uint32_t l70 = orig::fn_004e07f0(0x0063a210u, 0x10, 0x18, 2, kNew);
    U(8) = l70 != 0 ? orig::fn_004e7010(Field<uint32_t>(Gm(), 0xac4), 0, l70) : 0;

    // Pass 1: read and set up every template record.
    U(4) = 0;
    for (uint32_t off = 0; U(4) < Field<uint32_t>(sys, 0xe34); off += 0x50)
    {
        const uint32_t cursor = U(0x1c);
        Field<uint32_t>(Rec(off), 8) = 0;
        Field<uint32_t>(Rec(off), 0x10) = cursor;
        if (Field<uint32_t>(ctx, 0x28) != 0)
        {
            ChunkReadR(ctx, 4, u(f + 0x28), &r);
            ChunkReadR(ctx, U(0x28) - 4, cursor + 4, &r);
        }
        uint8_t* th = B(orig::fn_004e07f0(0x0063a210u, 0x10, 0x20, 2, kNew));
        if (th != nullptr)
        {
            const uint32_t recAddr = u(Rec(off));
            Field<uint32_t>(th, 0) = 0;
            Field<uint32_t>(th, 4) = 0;
            Field<uint32_t>(th, 8) = 0;
            Field<uint32_t>(th, 0x10) = 0x0045b630;
            Field<uint32_t>(th, 0x14) = recAddr;
            Field<uint32_t>(th, 0x1c) = 0;
        }
        Field<uint32_t>(Tmpl(off), 0) = u(th);
        Field<uint32_t>(Tmpl(off), 0x2c) += cursor + 4;
        orig::fn_004f1220(Field<uint32_t>(Tmpl(off), 0x2c), Field<uint32_t>(Tmpl(off), 0));
        Field<uint32_t>(Tmpl(off), 0x30) += cursor;
        Field<uint32_t>(Tmpl(off), 0x34) += cursor;
        Field<uint32_t>(Tmpl(off), 0x38) += cursor;
        uint8_t* t = Tmpl(off);
        const uint32_t cells = Field<uint32_t>(t, 0x38) + Field<uint32_t>(t, 0x28) * Field<uint32_t>(t, 0x24) * 4;
        Field<uint32_t>(Tmpl(off), 0x3c) = orig::fn_0045b6c0(cells, u(sys));
        Field<uint32_t>(Tmpl(off), 0x40) = cells + 0x20;
        Field<uint32_t>(Rec(off), 0) = 0xffffffffu;
        Field<uint32_t>(Rec(off), 4) = 0xffffffffu;
        uint8_t* p = Field<uint8_t*>(Tmpl(off), 0x3c);
        if (p != nullptr)
        {
            if (static_cast<uint8_t>(PropValue(p, 0x005b095c, u(Rec(off)))) != 0 &&
                Field<int32_t>(Rec(off), 0) >= Field<int32_t>(Tmpl(off), 0x28))
                Field<uint32_t>(Rec(off), 0) = 0xffffffffu;
            p = Field<uint8_t*>(Tmpl(off), 0x3c);
            if (static_cast<uint8_t>(PropValue(p, 0x005b0948, u(Rec(off)) + 4)) != 0 &&
                Field<int32_t>(Rec(off), 4) >= Field<int32_t>(Tmpl(off), 0x28))
                Field<uint32_t>(Rec(off), 4) = 0xffffffffu;
        }
        for (uint32_t lod = 0; lod < Field<uint32_t>(Tmpl(off), 0x28); ++lod)
        {
            t = Tmpl(off);
            uint8_t* rec = Rec(off);
            uint32_t* cut = Field<uint32_t*>(t, 0x40);
            if (lod == Field<uint32_t>(t, 0x28) - 1)
            {
                cut[lod] = 0xffffffffu;
                continue;
            }
            if (lod == Field<uint32_t>(rec, 0) || lod == Field<uint32_t>(rec, 4))
                cut[lod] = 0xffffffffu;
            else
                cut[lod] = 0;
            Sprintf(f + 0x50, 0x005b0938, lod);
            t = Tmpl(off);
            p = Field<uint8_t*>(t, 0x3c);
            if (p != nullptr)
                PropValue(p, u(f + 0x50), Field<uint32_t>(t, 0x40) + lod * 4);
        }
        uint32_t o = orig::fn_004e07f0(0x0063a210u, 0x10, 0x1c, 2, kNew);
        U(0x10) = o != 0 ? orig::fn_004e6bd0(10, o, 0x0045b660) : 0;
        o = orig::fn_004e07f0(0x0063a210u, 0x10, 0x1c, 2, kNew);
        U(0xc) = o != 0 ? orig::fn_004e6bd0(0x32, o, 0x0045b670) : 0;
        Field<uint32_t>(Rec(off), 0xc) =
            orig::fn_004e07f0(0x0063a210u, 0xc, Field<uint32_t>(Tmpl(off), 0x24) * 0xc, 2, 0x005b0920);
        const uint32_t animBytes = Field<uint32_t>(Tmpl(off), 0x24) * 0xc;
        uint8_t* anims = Field<uint8_t*>(Rec(off), 0xc);
        std::memset(anims, 0, animBytes);
        orig::fn_0045bbe0(u(anims) + animBytes, u(sys), u(Rec(off)), 0, 0);
        U(0x24) = 0;
        if (Field<uint32_t>(Tmpl(off), 0x24) != 0)
        {
            U(0x18) = 0;
            do
            {
                const uint32_t anim = Field<uint32_t>(Field<uint8_t*>(Rec(off), 0xc) + U(0x18), 8);
                U(0x14) = anim;
                if (anim != 0)
                {
                    bool addToB = true;
                    if (VCall<uint32_t>(B(U(8)), 0x34 / 4) != 0)
                    {
                        Field<uint8_t>(f, 1) = 1;
                        uint32_t node = orig::fn_004e7340(U(8), u(f + 0x34));
                        while (node != 0)
                        {
                            if (VCall<uint32_t>(B(node), 0x1c / 4, U(0x14)) != 0)
                            {
                                orig::fn_004e6ed0(U(0x10), node);
                                Field<uint8_t>(f, 1) = 0;
                                IterRelease(f + 0x34);
                                U(0x38) = 0;
                            }
                            node = orig::fn_004e73c0(u(f + 0x34));
                        }
                        addToB = Field<uint8_t>(f, 1) != 0;
                    }
                    if (addToB)
                        orig::fn_004e6ed0(U(0xc), U(0x14));
                }
                U(0x18) += 0xc;
                U(0x24) += 1;
            } while (U(0x24) < Field<uint32_t>(Tmpl(off), 0x24));
        }
        U(0x1c) += U(0x28);
        if (VCall<uint32_t>(B(U(0x10)), 0x34 / 4) == 0)
        {
            VCall<uint32_t>(B(U(8)), 0x18 / 4, U(0xc));
        }
        else
        {
            U(0x44) = 0;
            U(0x48) = 0;
            U(0x3c) = 0;
            U(0x40) = 0;
            Field<uint8_t>(f, 1) = 1;
            uint32_t last = 0;
            uint32_t node = orig::fn_004e7340(U(0x10), u(f + 0x44));
            if (node != 0)
            {
                do
                {
                    if (Field<uint8_t>(f, 1) != 0)
                    {
                        U(0x30) = node;
                        Field<uint8_t>(f, 1) = 0;
                        for (uint32_t e = orig::fn_004e7340(U(0xc), u(f + 0x3c)); e != 0;
                             e = orig::fn_004e73c0(u(f + 0x3c)))
                            orig::fn_004e6ed0(node, e);
                    }
                    else
                    {
                        uint32_t e = orig::fn_004e7340(node, u(f + 0x3c));
                        if (e != 0)
                        {
                            const uint32_t first = U(0x30);
                            do
                            {
                                orig::fn_004e6ed0(first, e);
                                e = orig::fn_004e73c0(u(f + 0x3c));
                            } while (e != 0);
                        }
                        VCall<uint32_t>(B(U(8)), 0x28 / 4, node);
                        VCall<uint32_t>(B(node), 0x10 / 4, 1u);
                    }
                    node = orig::fn_004e73c0(u(f + 0x44));
                } while (node != 0);
                last = U(0x40);
            }
            if (U(0xc) != 0)
                VCall<uint32_t>(B(U(0xc)), 0x10 / 4, 1u);
            if (last != 0)
                Field<uint32_t>(B(last), 0x18) -= 1;
            if (U(0x48) != 0)
                Field<uint32_t>(B(U(0x48)), 0x18) -= 1;
        }
        VCall<uint32_t>(B(U(0x10)), 0x10 / 4, 1u);
        U(4) += 1;
    }

    // Pass 2: one "anim cache array" entry per animation group (node of list U(8)), into list U(0x10).
    uint8_t* lm = B(orig::fn_004e07f0(0x0063a210u, 0x10, 0x18, 2, kNew));
    if (lm != nullptr)
    {
        const uint32_t data = Field<uint32_t>(Gm(), 0xac4);
        Field<uint32_t>(lm, 0) = 0x0058b098;
        uint32_t d = data;
        if (data != 0)
        {
            Field<uint8_t>(lm, 4) = 0;
        }
        else
        {
            d = orig::fn_004e06f0(0, 0x0063a210, 0x0058b0e0, 0x1c, 2, 5);
            Field<uint8_t>(lm, 4) = 1;
        }
        Field<uint32_t>(lm, 8) = d;
        Field<uint32_t>(lm, 0xc) = 0;
        Field<uint32_t>(lm, 0x10) = 0;
        Field<uint32_t>(lm, 0x14) = 0;
    }
    U(0x10) = u(lm);
    U(0x14) = orig::fn_004e7340(U(8), u(f + 0x34));
    while (U(0x14) != 0)
    {
        U(0x44) = 0;
        U(0x48) = 0;
        uint8_t* heap = B(Alloc(0x14, 2));
        Field<uint32_t>(heap, 0) = 0;
        Field<uint32_t>(heap, 4) = 0;
        Field<uint32_t>(heap, 8) = 0;
        Field<uint32_t>(heap, 0xc) = 0;
        Field<uint32_t>(heap, 0x10) = 0;
        Field<uint32_t>(heap, 0) = 0;
        Field<uint32_t>(heap, 8) = 2;
        Field<uint16_t>(heap, 0x10) = 1;
        U(0x18) = u(heap);
        GameGlobal<uint32_t>(0x005caa5c) = 0x004f0c40;
        Field<uint32_t>(heap, 4) = orig::fn_004e0ff0(0, 0x100, 0, 1, 0x400, 1, 0x005caa58, 0x14, 2, 0, 0);
        U(0x1c) = 0;
        U(4) = 0;
        if (Field<uint32_t>(sys, 0xe34) != 0)
        {
            U(0x28) = 0;
            uint32_t off = 0;
            do
            {
                uint8_t* t = Tmpl(off);
                if (Field<uint32_t>(t, 0x24) > 1 &&
                    VCall<uint32_t>(B(U(0x14)), 0x1c / 4, fn_0045ace0(Field<uint32_t>(t, 0x30) + 0x20, sys)) != 0)
                {
                    uint8_t* p = Field<uint8_t*>(Tmpl(off), 0x3c);
                    if (p == nullptr || static_cast<uint8_t>(PropString(p, 0x005b0e6c, u(f + 0x50))) == 0)
                        Sprintf(f + 0x50, 0x005b0e64, u(Tmpl(off)) + 4);
                    uint8_t* h = Field<uint8_t*>(Tmpl(off), 0);
                    if (Field<uint32_t>(h, 0x10) != 0)
                    {
                        uint32_t sum = 0;
                        const uint32_t salt = Field<uint32_t>(h, 4);
                        for (const int8_t* c = reinterpret_cast<const int8_t*>(f + 0x50); *c != 0; ++c)
                        {
                            const int32_t ch = (*c >= 0x41 && *c <= 0x5a) ? *c + 0x20 : *c;
                            sum += salt ^ static_cast<uint32_t>(ch);
                        }
                        uint8_t* e = Field<uint8_t**>(h, 0x1c)[sum % Field<uint32_t>(h, 0)];
                        while (e != nullptr)
                        {
                            if (reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t)>(
                                    Field<uint32_t>(h, 0x10))(Field<uint32_t>(h, 0x14), u(f + 0x50), u(e) + 4) != 0)
                                break;
                            e = Field<uint8_t*>(e, 0);
                        }
                        if (e != nullptr)
                        {
                            const int32_t v = Field<int32_t>(e, 4);
                            if (v >= 0)
                            {
                                uint32_t entry = 0;
                                if (v != -1)
                                    entry = Field<uint32_t>(Rec(off), 0xc) + v * 0xc;
                                orig::fn_004ef590(Field<uint32_t>(B(entry), 8), 0x004edfe0, U(0x18));
                                U(0x1c) += 1;
                            }
                        }
                    }
                }
                U(4) += 1;
                off += 0x50;
                U(0x28) = off;
            } while (U(4) < Field<uint32_t>(sys, 0xe34));
        }
        void* node = B(U(0x14));
        uint32_t n = 0;
        const uint32_t count = VCall<uint32_t>(node, 0x34 / 4);
        uint32_t* arr = reinterpret_cast<uint32_t*>(orig::fn_004e07f0(0x0063a210u, 9, count * 4, 2, 0x005b090c));
        for (uint32_t e = orig::fn_004e7340(u(node), u(f + 0x44)); e != 0; e = orig::fn_004e73c0(u(f + 0x44)))
            arr[n++] = e;
        uint32_t cache;
        if (U(0x1c) != 0)
            cache = orig::fn_004ee010(U(0x18), 0, 0x005fba0c, 0, u(arr), n, 0);
        else
            cache = orig::fn_004ee010(0, 0, 0, 0, u(arr), n, 0);
        VCall<uint32_t>(B(U(0x10)), 0x18 / 4, cache);
        Free(u(arr));
        uint8_t* hp = B(U(0x18));
        uint8_t* sub = Field<uint8_t*>(Field<uint8_t*>(hp, 4), 0);
        U(0x30) = Field<uint32_t>(hp, 0);
        uint8_t* head = sub + 0x10;
        uint8_t* first = Field<uint8_t*>(head, 0);
        uint8_t* next = Field<uint8_t*>(first, 0);
        uint8_t* cur = first == head ? nullptr : first;
        while (cur != nullptr)
        {
            orig::fn_004e1f70(u(sub), u(cur + 0xc));
            if (next == head)
                break;
            cur = next;
            next = Field<uint8_t*>(next, 0);
        }
        orig::fn_004e1f70(U(0x30), u(hp));
        if (U(0x48) != 0)
            Field<uint32_t>(B(U(0x48)), 0x18) -= 1;
        U(0x14) = orig::fn_004e73c0(u(f + 0x34));
    }

    // Pass 3: shared cache tables, and each template's anim group (cache entry) into +8.
    const uint32_t groups = VCall<uint32_t>(B(U(8)), 0x34 / 4);
    Field<uint32_t>(sys, 0xe68) = groups;
    Field<uint32_t>(sys, 0xe60) = orig::fn_004e07f0(0x0063a210u, 0xc, groups * 4, 2, 0x005b08f8);
    Field<uint32_t>(sys, 0xe64) = orig::fn_004e07f0(0x0063a210u, 0xc, Field<uint32_t>(sys, 0xe68) * 4, 2, 0x005b08e8);
    std::memset(Field<uint8_t*>(sys, 0xe60), 0, Field<uint32_t>(sys, 0xe68) * 4);
    std::memset(Field<uint8_t*>(sys, 0xe64), 0, Field<uint32_t>(sys, 0xe68) * 4);
    U(4) = 0;
    for (uint32_t off = 0; U(4) < Field<uint32_t>(sys, 0xe34); off += 0x50, U(4) += 1)
    {
        uint8_t* t = Tmpl(off);
        if (Field<uint32_t>(t, 0x24) <= 1)
            continue;
        U(0x30) = fn_0045ace0(Field<uint32_t>(t, 0x30) + 0x20, sys);
        uint32_t group = 0;
        uint32_t node = orig::fn_004e7340(U(8), u(f + 0x34));
        bool found = false;
        while (node != 0)
        {
            if (VCall<uint32_t>(B(node), 0x1c / 4, U(0x30)) != 0)
            {
                found = true;
                break;
            }
            ++group;
            node = orig::fn_004e73c0(u(f + 0x34));
        }
        if (!found)
            continue;
        U(0x28) = 0;
        U(0x2c) = 0;
        U(0x1c) = 0;
        uint32_t pending = 0;
        for (uint32_t e = orig::fn_004e7340(U(0x10), u(f + 0x28)); e != 0; e = orig::fn_004e73c0(u(f + 0x28)))
        {
            if (group == U(0x1c))
            {
                Field<uint32_t>(Rec(off), 8) = e;
                t = Tmpl(off);
                if (Field<uint32_t>(t, 0x24) == 2)
                {
                    uint8_t* p = Field<uint8_t*>(t, 0x3c);
                    if (p == nullptr || static_cast<uint8_t>(PropString(p, 0x005b0e6c, u(f + 0x50))) == 0)
                        Sprintf(f + 0x50, 0x005b0e64, u(Tmpl(off)) + 4);
                    if (GameFunction<int(__cdecl*)(uint32_t, uint32_t)>(0x005448ee)(
                            Field<uint32_t>(Tmpl(off), 0x30) + 0x20, u(f + 0x50)) == 0)
                    {
                        Field<uint32_t>(Rec(off), 8) |= 0x80000000u;
                        Field<uint32_t>(Rec(off), 8) |= (group & 0x7fff) << 16;
                    }
                }
                IterRelease(f + 0x28);
                pending = 0xffffffffu;
                break;
            }
            U(0x1c) += 1;
        }
        const uint32_t held = pending == 0 ? U(0x2c) : 0;
        IterRelease(f + 0x34);
        U(0x38) = 0;
        if (held != 0)
            Field<uint32_t>(B(held), 0x18) -= 1;
    }
    if (U(0x10) != 0)
        VCall<uint32_t>(B(U(0x10)), 0x10 / 4, 1u);
    for (uint32_t node = orig::fn_004e7340(U(8), u(f + 0x34)); node != 0; node = orig::fn_004e73c0(u(f + 0x34)))
    {
        VCall<uint32_t>(B(U(8)), 0x28 / 4, node);
        VCall<uint32_t>(B(node), 0x10 / 4, 1u);
    }
    VCall<uint32_t>(B(U(8)), 0x10 / 4, 1u);

    // Pass 4: bounds of every template from its cells' meshes.
    U(4) = 0;
    for (U(0x18) = 0; U(4) < Field<uint32_t>(sys, 0xe34); U(0x18) += 0x50, U(4) += 1)
    {
        uint8_t* rec = Rec(U(0x18));
        const uint32_t g0 = GameGlobal<uint32_t>(0x0064a74c);
        const uint32_t g1 = GameGlobal<uint32_t>(0x0064a750);
        const uint32_t g2 = GameGlobal<uint32_t>(0x0064a754);
        float* mx = reinterpret_cast<float*>(rec + 0x34);
        float* mn = reinterpret_cast<float*>(rec + 0x28);
        Field<uint32_t>(rec, 0x34) = g0;
        Field<uint32_t>(rec, 0x38) = g1;
        Field<uint32_t>(rec, 0x3c) = g2;
        Field<uint32_t>(rec, 0x28) = g0;
        Field<uint32_t>(rec, 0x2c) = GameGlobal<uint32_t>(0x0064a750);
        Field<uint32_t>(rec, 0x30) = g2;
        bool firstBox = true;
        uint8_t* t = Field<uint8_t*>(rec, 0x10);
        if (Field<uint32_t>(t, 0x24) != 0 && Field<uint32_t>(t, 0x28) != 0)
        {
            for (U(0x1c) = 0; U(0x1c) < Field<uint32_t>(Field<uint8_t*>(rec, 0x10), 0x28); U(0x1c) += 1)
            {
                uint8_t* cell = Field<uint8_t**>(Field<uint8_t*>(rec, 0x10), 0x38)[U(0x1c)];
                if (cell == nullptr || Field<uint32_t>(cell, 0x14) == 0)
                    continue;
                uint32_t* slot = reinterpret_cast<uint32_t*>(cell + 0x18);
                U(0x14) = Field<uint32_t>(cell, 0x14);
                do
                {
                    uint8_t* m = Field<uint8_t*>(B(*slot), 4);
                    const uint32_t cnt = Field<uint32_t>(m, 0x80);
                    const uint32_t arr = Field<uint32_t>(m, 0x84);
                    ++slot;
                    U(0x30) = u(slot);
                    if (cnt != 0)
                    {
                        U(0x28) = arr + 0x24;
                        U(0x24) = cnt;
                        do
                        {
                            const uint32_t bounds = Field<uint32_t>(B(U(0x28)), 0);
                            if (bounds != 0)
                            {
                                const float* b = reinterpret_cast<const float*>(bounds + 0x24);
                                if (firstBox)
                                {
                                    Field<uint32_t>(rec, 0x28) = Field<uint32_t>(B(u(b)), 0);
                                    Field<uint32_t>(rec, 0x2c) = Field<uint32_t>(B(u(b)), 4);
                                    Field<uint32_t>(rec, 0x30) = Field<uint32_t>(B(u(b)), 8);
                                    Field<uint32_t>(rec, 0x34) = Field<uint32_t>(B(u(b)), 0xc);
                                    Field<uint32_t>(rec, 0x38) = Field<uint32_t>(B(u(b)), 0x10);
                                    Field<uint32_t>(rec, 0x3c) = Field<uint32_t>(B(u(b)), 0x14);
                                    firstBox = false;
                                }
                                if (mn[0] > b[0])
                                    Field<uint32_t>(rec, 0x28) = Field<uint32_t>(B(u(b)), 0);
                                if (mn[1] > b[1])
                                    Field<uint32_t>(rec, 0x2c) = Field<uint32_t>(B(u(b)), 4);
                                if (mn[2] > b[2])
                                    Field<uint32_t>(rec, 0x30) = Field<uint32_t>(B(u(b)), 8);
                                if (mx[0] < b[3])
                                    Field<uint32_t>(rec, 0x34) = Field<uint32_t>(B(u(b)), 0xc);
                                if (mx[1] < b[4])
                                    Field<uint32_t>(rec, 0x38) = Field<uint32_t>(B(u(b)), 0x10);
                                if (mx[2] < b[5])
                                    Field<uint32_t>(rec, 0x3c) = Field<uint32_t>(B(u(b)), 0x14);
                            }
                            U(0x28) += 0x2c;
                            U(0x24) -= 1;
                        } while (U(0x24) != 0);
                        slot = reinterpret_cast<uint32_t*>(U(0x30));
                    }
                    U(0x14) -= 1;
                } while (U(0x14) != 0);
            }
        }
        const float half = GameGlobal<float>(0x005bcb4c);
        const float cx = (mn[0] + mx[0]) * half;
        Field<float>(rec, 0x14) = cx;
        const float cy = (Field<float>(rec, 0x2c) + Field<float>(rec, 0x38)) * half;
        Field<float>(f, 0x30) = cy;
        Field<float>(rec, 0x18) = cy;
        const float cz = (Field<float>(rec, 0x30) + Field<float>(rec, 0x3c)) * half;
        Field<float>(f, 0x28) = cz;
        Field<float>(rec, 0x1c) = cz;
        const float dx = mx[0] - cx;
        Field<float>(f, 0x48) = Field<float>(rec, 0x38) - Field<float>(f, 0x30);
        const float dz = Field<float>(rec, 0x3c) - Field<float>(f, 0x28);
        Field<float>(f, 0x4c) = dz;
        const float dy = Field<float>(f, 0x48);
        Field<float>(rec, 0x20) = std::sqrt(dz * Field<float>(f, 0x4c) + dy * Field<float>(f, 0x48) + dx * dx);
        Field<float>(rec, 0x24) = dx;
        if (dx > Field<float>(f, 0x48))
            Field<uint32_t>(rec, 0x24) = Field<uint32_t>(f, 0x48);
        if (Field<float>(rec, 0x24) > Field<float>(f, 0x4c))
            Field<uint32_t>(rec, 0x24) = Field<uint32_t>(f, 0x4c);
    }

    // Pass 5: scale data.
    U(4) = 0;
    for (U(0x1c) = 0; U(4) < Field<uint32_t>(sys, 0xe34); U(0x1c) += 0x50, U(4) += 1)
    {
        uint8_t* rec = Rec(U(0x1c));
        Field<uint32_t>(rec, 0x40) = 0;
        Field<uint32_t>(rec, 0x48) = 0;
        Field<uint32_t>(rec, 0x44) = orig::fn_00410ec0(u(rec));
        uint8_t* p = Field<uint8_t*>(Field<uint8_t*>(rec, 0x10), 0x3c);
        if (p != nullptr)
            PropValue(p, 0x005b08dc, u(rec) + 0x40);
        if (Field<uint32_t>(rec, 0x40) == 0)
            continue;
        uint8_t* scale = B(orig::fn_004e07f0(0x0063a210u, 0xc, Field<uint32_t>(rec, 0x40) * 0xc, 2, 0x005b08c8));
        Field<uint32_t>(rec, 0x48) = u(scale);
        if (scale == nullptr)
            continue;
        std::memset(scale, 0, Field<uint32_t>(rec, 0x40) * 0xc);
        for (uint32_t k = 0; k < Field<uint32_t>(rec, 0x40); ++k)
        {
            Sprintf2(f + 0x50, 0x005b08b0, 0x005b08b8, k);
            p = Field<uint8_t*>(Field<uint8_t*>(rec, 0x10), 0x3c);
            U(0x10) = 0x3f800000;
            if (p != nullptr)
                PropValue(p, u(f + 0x50), u(f + 0x10));
            const uint32_t v = U(0x10);
            U(0x44) = v;
            U(0x48) = v;
            U(0x4c) = v;
            if (Field<uint32_t>(Field<uint8_t*>(rec, 0x10), 0x3c) != 0)
            {
                Sprintf2(f + 0x50, 0x005b08b0, 0x005b08a4, k);
                PropValue(Field<uint8_t*>(Field<uint8_t*>(rec, 0x10), 0x3c), u(f + 0x50), u(f + 0x44));
                Sprintf2(f + 0x50, 0x005b08b0, 0x005b0898, k);
                PropValue(Field<uint8_t*>(Field<uint8_t*>(rec, 0x10), 0x3c), u(f + 0x50), u(f + 0x48));
                Sprintf2(f + 0x50, 0x005b08b0, 0x005b088c, k);
                PropValue(Field<uint8_t*>(Field<uint8_t*>(rec, 0x10), 0x3c), u(f + 0x50), u(f + 0x4c));
            }
            Field<float>(Field<uint8_t*>(rec, 0x48), k * 0xc) = Field<float>(f, 0x44);
            Field<float>(Field<uint8_t*>(rec, 0x48), k * 0xc + 4) = Field<float>(f, 0x48);
            Field<float>(Field<uint8_t*>(rec, 0x48), k * 0xc + 8) = Field<float>(f, 0x4c);
        }
    }

    // Pass 6: collision, inside the 0x004e0bd0 bracket.
    if (GameGlobal<uint8_t>(0x0063a0fc) == 1)
    {
        Field<uint8_t>(f, 3) = 1;
    }
    else
    {
        orig::fn_004e0bd0();
        GameGlobal<uint8_t>(0x0063a0fc) = 1;
        Field<uint8_t>(f, 3) = 0;
    }
    U(4) = 0;
    for (U(0x1c) = 0; U(4) < Field<uint32_t>(sys, 0xe34); U(0x1c) += 0x50, U(4) += 1)
    {
        uint8_t* rec = Rec(U(0x1c));
        Field<uint32_t>(rec, 0x4c) = 0;
        Field<uint8_t>(f, 2) = 0;
        Field<uint8_t>(f, 1) = 0;
        uint8_t* p = Field<uint8_t*>(Field<uint8_t*>(rec, 0x10), 0x3c);
        if (p == nullptr)
            continue;
        if (static_cast<uint8_t>(PropBool(p, 0x005b0884, u(f + 1))) != 0 && Field<uint8_t>(f, 1) != 0)
            Field<uint32_t>(rec, 0x4c) |= 2;
        p = Field<uint8_t*>(Field<uint8_t*>(rec, 0x10), 0x3c);
        if (static_cast<uint8_t>(PropBool(p, 0x005b0874, u(f + 2))) != 0 && Field<uint8_t>(f, 2) != 0)
            Field<uint32_t>(rec, 0x4c) |= 1;
        int32_t column = Field<int32_t>(rec, 0);
        uint32_t row = 0;
        if (column < 0)
        {
            column = 0;
        }
        else
        {
            const int32_t found = static_cast<int32_t>(fn_0045bf10(rec, static_cast<uint32_t>(column), u(sys)));
            if (found >= 0)
            {
                Field<uint32_t>(rec, 0x4c) |= 4;
                row = static_cast<uint32_t>(found);
            }
        }
        if ((Field<uint32_t>(rec, 0x4c) & 5) == 0)
            continue;
        uint8_t* t = Field<uint8_t*>(rec, 0x10);
        uint8_t* cell =
            Field<uint8_t**>(t, 0x38)[Field<uint32_t>(t, 0x28) * row + static_cast<uint32_t>(column)];
        U(0x18) = u(cell);
        if (cell == nullptr)
            continue;
        U(0x28) = u(cell + 0x18);
        for (int32_t m = 0; m < Field<int32_t>(B(U(0x18)), 0x14); ++m)
        {
            const uint32_t model = Field<uint32_t>(B(Field<uint32_t>(B(U(0x28)), 0)), 4);
            U(0x28) += 4;
            for (int32_t lod = -1; lod < Field<int32_t>(rec, 0x40); ++lod)
            {
                Field<uint8_t>(f, 0x24) = static_cast<uint8_t>(~(Field<uint32_t>(rec, 0x4c) >> 1) & 1);
                orig::fn_00412450(u(rec), Field<uint32_t>(Gm(), 0xa8c), model, 0xffffffffu, static_cast<uint32_t>(m),
                                  u(f + 0x24), U(0x24), 1, 0, static_cast<uint32_t>(lod));
            }
        }
    }
    const uint8_t busy = Field<uint8_t>(f, 3);
    if (busy != GameGlobal<uint8_t>(0x0063a0fc))
    {
        orig::fn_004e0bd0();
        GameGlobal<uint8_t>(0x0063a0fc) = busy;
    }
    const uint32_t held = U(0x38);
    if (held != 0)
        Field<uint32_t>(B(held), 0x18) -= 1;
    return held;
}

static uint32_t ObjectSystem_LoadTemplates_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return ObjectSystem_LoadTemplates(a0, a1, a2);
}
ABI_ENTRY_0045bf70(ObjectSystem_LoadTemplates_Entry, ObjectSystem_LoadTemplates_Abi)
HOOK(0x0045bf70, "ObjectSystem_LoadTemplates", ObjectSystem_LoadTemplates_Entry, nullptr);
