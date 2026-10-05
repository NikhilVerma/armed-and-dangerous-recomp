// Recovered: save batch save_01 (options registry 0x00462110-0x00462660, options file, savegame files and slots
// 0x0048b150-0x0048b780, save-manager object serialisation 0x0048b780-0x0048e630, savegame UI helpers). Static
// recovery 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified from the disassembly. "AL = x" returns keep the upper 24 bits EAX had in the
// original (usually the previous call's result) where that is cheap.
//
// Options registry (0x00462110): a table {u32 count; 32 entries of 7 dwords} at 0x0064a330. Entry: +0 hash of the
// name (0x004620b0), +4 name ("&m_options.m_xxx"), +8 address of the value, +0xc default value, +0x10 type (0 int,
// 1 float, 2 bool byte, 3 string, 4 blob), +0x14 size, +0x18 cdecl callback(op, name, type, address, size) with op
// 0 = reset, 1 = loaded, 2 = about to save. File image: magic 0x6f707430, then per entry {u32 hash, u16 type, u16
// length, data}. Written to / read from "options.opt".
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
inline uint32_t* D(uint32_t v)
{
    return reinterpret_cast<uint32_t*>(v);
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
// 0x004e07f0 PMS_heapAlloc (ecx = heap 0x0063a210; stack: tag, size, align, name).
inline uint32_t HeapAlloc_(uint32_t tag, uint32_t size, uint32_t align, uint32_t name)
{
    return orig::fn_004e07f0(0x0063a210u, tag, size, align, name);
}
// 0x004e08d0 PMS_heapFree (ecx = heap 0x0063a210; stack: block).
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
// _makepath (0x0053859e, cdecl, 5 arguments). Returned as EAX for callers that keep it.
inline uint32_t MakePath(uint32_t path, uint32_t drive, uint32_t dir, uint32_t fname, uint32_t ext)
{
    return GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0053859e)(
        path, drive, dir, fname, ext);
}
// _splitpath (0x00538456, cdecl, 5 arguments).
inline uint32_t SplitPath(uint32_t path, uint32_t drive, uint32_t dir, uint32_t fname, uint32_t ext)
{
    return GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00538456)(
        path, drive, dir, fname, ext);
}
// The game's 0x0044ad70 (eax = string): 32-bit name hash.
inline uint32_t Hash(uint32_t s)
{
    return orig::fn_0044ad70(s);
}
// x86 "shl by (32 - d), shr by d, or" with d in 0..31: a rotate right that is the identity for d = 0.
inline uint32_t RotR(uint32_t v, uint32_t d)
{
    d &= 0x1f;
    return (v << ((32 - d) & 0x1f)) | (v >> d);
}

// Calls an original function that takes EBX and EDI and one caller-popped stack argument (0x004f2030: ebx = find
// state, edi = out name or null; stack: pattern; see src/loader/loader_01.cpp).
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
// Calls an original function that takes ECX and ESI and one caller-popped stack argument (0x004f1f40: ecx = find
// state, esi = pattern; stack: out name or 0). symbols/abi.csv lists 0x004f1f40 with 6 stack arguments; it reads one
// ([esp+0x314] after its 0x308 frame and two pushes), the other three slots are the caller's leftover pushes.
__declspec(noinline) uint32_t CallEcxEsi1(uint32_t target, uint32_t ecx_, uint32_t esi_, uint32_t a0)
{
    uint32_t r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a0
        mov eax, target
        mov ecx, ecx_
        mov esi, esi_
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
// cdecl callback types.
typedef uint32_t(__cdecl* Cb5)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(__cdecl* Cb3)(uint32_t, uint32_t, uint32_t);
typedef uint32_t(__cdecl* Cb2)(uint32_t, uint32_t);
}  // namespace

// Entry stubs for functions whose symbols/abi.csv inputs are incomplete (same shape as src/gen/entry_stubs.h).
// eax, ecx, edi + 1 caller-popped stack argument, plain ret.
#define SV_ENTRY_EAX_ECX_EDI_S1(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push dword ptr [esp + 28] __asm push edi __asm push ecx __asm push eax __asm call impl __asm add esp, 16 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret }

// Prototypes (functions of this file call each other directly).
uint32_t fn_00462110(uint32_t name, uint32_t* table, uint32_t type, uint32_t addr, uint32_t size, uint32_t cb);
uint32_t fn_00462160(uint32_t cb, uint32_t addr, uint32_t* table, uint32_t name, uint32_t value);
uint32_t fn_00462190(uint32_t cb, uint32_t addr, uint32_t* table, uint32_t name, uint32_t value);
uint32_t fn_004621c0(uint32_t cb, uint32_t addr, uint32_t* table, uint32_t name, uint32_t value);
uint32_t fn_004621f0(uint32_t cb, uint32_t size, uint32_t addr, uint32_t* table, uint32_t name);
uint32_t fn_00462220(uint32_t* e, uint32_t a0, uint32_t a1);
uint32_t fn_00462250(uint32_t* e, uint32_t table);
uint32_t fn_00462310(uint8_t* buf, uint32_t* table, uint32_t size);
uint32_t fn_00462450(uint32_t* table, uint8_t* buf, uint32_t cap);
uint32_t fn_004625d0(uint32_t* table, uint32_t* outBuf, uint32_t* outSize);
uint32_t fn_00462610(uint32_t* table);
uint32_t fn_00462660(uint32_t table);
uint32_t fn_0048b150(uint32_t eax, uint32_t ecx, uint32_t buf, uint32_t size);
uint32_t fn_0048b1a0(uint32_t eax, uint32_t ecx, uint32_t* outSize, uint32_t* outBuf);
uint32_t fn_0048b210(uint32_t fname, uint32_t wname);
uint32_t fn_0048b260(uint32_t eax, uint32_t wname, uint32_t fname, uint32_t out, uint32_t outSize);
uint32_t fn_0048c010(uint8_t* mgr, uint32_t desc, uint32_t base);
uint32_t fn_0048bc80(uint8_t* mgr, uint32_t desc, uint32_t base);

// ================================================================================================================
// 0x00401a30 - 0x00403560: options registration, quick save
// ================================================================================================================

// 0x00401a30 (ebx = game): zeroes 0xd5 dwords at +0x174 and the options table 0x0064a330, registers only
// "&m_options.m_resolution" (+0x4a8, int, default DisplayMode_Pack(480, 640, 32)) and loads options.opt into it
// (0x00462660). Caller 0x004011e0 (Game constructor). Returns 0x00462660's EAX.
uint32_t fn_00401a30(uint8_t* g)
{
    std::memset(g + 0x174, 0, 0xd5 * 4);
    uint32_t* table = &GameGlobal<uint32_t>(0x0064a330);
    table[0] = 0;
    const uint32_t mode = orig::fn_00470c90(0x1e0, 0x280, 0x20);
    fn_00462190(0, u(g + 0x4a8), table, 0x005bcaf0, mode);
    return fn_00462660(u(table));
}
static uint32_t fn_00401a30_Abi(uint32_t ebx)
{
    return fn_00401a30(B(ebx));
}
ABI_ENTRY_00401a30(fn_00401a30_Entry, fn_00401a30_Abi)
HOOK(0x00401a30, "fn_00401a30", fn_00401a30_Entry, nullptr);

// 0x00401a80 Options_Register (eax = game): zeroes 0xd5 dwords at +0x174 and the options table 0x0064a330,
// registers every option (names "&m_options.m_*" at 0x005bc880..0x005bcaf0 give the members: Subtitles +0x176,
// SubtitlesGame +0x177, numUnlocked +0x1f8, unlocked +0x20c (0x280-byte blob), cheatsKnown +0x48c, autoAim +0x4c2,
// "&m_control->m_inputActions" ([+0xa80]+0x1c, 0x118-byte blob with callback 0x00401a10), drawDistance +0x4b0,
// brightness +0x4b4, the enable flags +0x4bc..+0x4c0, LODs +0x4a4/+0x4a0, textureQuality +0x49c, mouseSensitivity
// +0x4b8, resolution +0x4a8, skinHQ +0x4c1, maxTextures +0x4ac, volumes +0x490/+0x498/+0x494), loads options.opt
// (0x00462660; +0x4c4 = 1 only when the file existed but failed to load), then 0x00401010 (ebx = game; its ecx is
// not read: 0) and Options_Apply 0x00401ce0 (al = 0, upper bytes of 0x00401010's result; esi = game).
// Caller 0x004011e0. Returns Options_Apply's EAX.
uint32_t Options_Register(uint8_t* g)
{
    std::memset(g + 0x174, 0, 0xd5 * 4);
    uint32_t* t = &GameGlobal<uint32_t>(0x0064a330);
    t[0] = 0;
    fn_00462160(0, u(g + 0x176), t, 0x005bcad8, orig::fn_00447630());
    fn_00462160(0, u(g + 0x177), t, 0x005bcabc, orig::fn_00447630());
    fn_004621f0(0, 0x14, u(g + 0x1f8), t, 0x005bcaa0);
    fn_004621f0(0, 0x280, u(g + 0x20c), t, 0x005bca88);
    fn_00462190(0, u(g + 0x48c), t, 0x005bca6c, 0);
    fn_00462160(0, u(g + 0x4c2), t, 0x005bca54, 0);
    fn_004621f0(0x00401a10, 0x118, Field<uint32_t>(g, 0xa80) + 0x1c, t, 0x005bca38);
    fn_004621c0(0, u(g + 0x4b0), t, 0x005bca1c, 0x3f800000);
    fn_004621c0(0, u(g + 0x4b4), t, 0x005bca04, 0x3fc00000);
    fn_00462160(0, u(g + 0x4bc), t, 0x005bc9e8, 0);
    fn_00462160(0, u(g + 0x4bd), t, 0x005bc9c8, 1);
    fn_00462160(0, u(g + 0x4be), t, 0x005bc9ac, 1);
    fn_00462160(0, u(g + 0x4bf), t, 0x005bc990, 1);
    fn_00462160(0, u(g + 0x4c0), t, 0x005bc970, 1);
    fn_00462190(0, u(g + 0x4a4), t, 0x005bc958, 4);
    fn_00462190(0, u(g + 0x4a0), t, 0x005bc940, 4);
    fn_00462190(0, u(g + 0x49c), t, 0x005bc924, 2);
    fn_004621c0(0, u(g + 0x4b8), t, 0x005bc904, 0x3f000000);
    const uint32_t mode = orig::fn_00470c90(0x1e0, 0x280, 0x20);
    fn_00462190(0, u(g + 0x4a8), t, 0x005bcaf0, mode);
    fn_00462160(0, u(g + 0x4c1), t, 0x005bc8f0, 1);
    fn_00462190(0, u(g + 0x4ac), t, 0x005bc8d4, 4);
    fn_004621c0(0, u(g + 0x490), t, 0x005bc8b8, 0x3f4ccccd);
    fn_004621c0(0, u(g + 0x498), t, 0x005bc89c, 0x3f4ccccd);
    fn_004621c0(0, u(g + 0x494), t, 0x005bc880, 0x3f4ccccd);
    const uint32_t loaded = fn_00462660(u(t));
    Field<uint8_t>(g, 0x4c4) = static_cast<uint8_t>(loaded) == 0 ? 1 : 0;
    const uint32_t r = orig::fn_00401010(0, u(g));
    return orig::fn_00401ce0(r & 0xffffff00u, u(g));
}
static uint32_t Options_Register_Abi(uint32_t eax)
{
    return Options_Register(B(eax));
}
ABI_ENTRY_00401a80(Options_Register_Entry, Options_Register_Abi)
HOOK(0x00401a80, "Options_Register", Options_Register_Entry, nullptr);

// 0x00401e60 (esi = game): when byte +0x4c3 (options dirty, guess) is set, saves the options table 0x0064a330 to
// options.opt (0x00462610) and clears it. Callers 0x00490d80, 0x004a0850. AL = 0 when nothing was saved.
uint32_t fn_00401e60(uint8_t* g)
{
    const uint8_t dirty = Field<uint8_t>(g, 0x4c3);
    if (dirty == 0)
        return 0;
    const uint32_t r = fn_00462610(&GameGlobal<uint32_t>(0x0064a330));
    Field<uint8_t>(g, 0x4c3) = 0;
    return r;
}
static uint32_t fn_00401e60_Abi(uint32_t esi)
{
    return fn_00401e60(B(esi));
}
ABI_ENTRY_00401e60(fn_00401e60_Entry, fn_00401e60_Abi)
HOOK(0x00401e60, "fn_00401e60", fn_00401e60_Entry, nullptr);

// 0x00403520 (cdecl; stack: screen name or null): deferred game callback (stored by 0x00403560 through 0x00403370):
// unless the UI (game+0xab4) already shows the screen (0x00490ab0), pushes it (UI_PushScreen 0x00490b00, eax =
// game+0x510). Default name "QuickSave" (0x005bc150). Not executed in recorded runs.
uint32_t QuickSave_PushScreen(uint32_t name)
{
    if (name == 0)
        name = 0x005bc150;
    uint8_t* g = Gm();
    const uint32_t r = orig::fn_00490ab0(name, Field<uint32_t>(g, 0xab4));
    if (static_cast<uint8_t>(r) != 0)
        return r;
    return orig::fn_00490b00(u(g + 0x510), Field<uint32_t>(g, 0xab4), name);
}
static uint32_t QuickSave_PushScreen_Abi(uint32_t a0)
{
    return QuickSave_PushScreen(a0);
}
ABI_ENTRY_00403520(QuickSave_PushScreen_Entry, QuickSave_PushScreen_Abi)
HOOK(0x00403520, "QuickSave_PushScreen", QuickSave_PushScreen_Entry, nullptr);

// 0x00403560 (esi = game): when bit 3 of +0x568 is set and the UI is not already on "QuickSave" (0x00490ab0),
// defers 0x00403520("QuickSave") with 0x00403370 (eax = game, ecx = callback, edx = argument; the original tail-jumps
// there). Callers 0x0044daf0, 0x004690c0. Returns 0 when the bit is clear (original: the caller's EAX).
uint32_t QuickSave_Request(uint8_t* g)
{
    if ((Field<uint8_t>(g, 0x568) & 8) == 0)
        return 0;
    const uint32_t r = orig::fn_00490ab0(0x005bc150, Field<uint32_t>(g, 0xab4));
    if (static_cast<uint8_t>(r) != 0)
        return r;
    return orig::fn_00403370(u(g), 0x00403520, 0x005bc150);
}
static uint32_t QuickSave_Request_Abi(uint32_t esi)
{
    return QuickSave_Request(B(esi));
}
ABI_ENTRY_00403560(QuickSave_Request_Entry, QuickSave_Request_Abi)
HOOK(0x00403560, "QuickSave_Request", QuickSave_Request_Entry, nullptr);

// ================================================================================================================
// 0x0044a610 - 0x00461b60: profile name, thumbnail, object serialise thunks
// ================================================================================================================

// 0x0044a610 (eax = key or 0, esi = object, edi = wide output buffer): when eax is set, 0x004187e0 (eax = ecx = edx =
// 0; stack: esi, eax, edi) may fill the output (AL != 0); otherwise copies the wide string at esi+0x360 to edi.
// Returns edi. Caller 0x004a3020.
uint32_t fn_0044a610(uint32_t eax, uint8_t* obj, uint16_t* out)
{
    if (eax != 0 && static_cast<uint8_t>(orig::fn_004187e0(0, 0, 0, u(obj), eax, u(out))) != 0)
        return u(out);
    const uint16_t* src = reinterpret_cast<const uint16_t*>(obj + 0x360);
    uint16_t* d = out;
    while (*src != 0)
        *d++ = *src++;
    *d = 0;
    return u(out);
}
static uint32_t fn_0044a610_Abi(uint32_t eax, uint32_t esi, uint32_t edi)
{
    return fn_0044a610(eax, B(esi), reinterpret_cast<uint16_t*>(edi));
}
ABI_ENTRY_0044a610(fn_0044a610_Entry, fn_0044a610_Abi)
HOOK(0x0044a610, "fn_0044a610", fn_0044a610_Entry, nullptr);

// 0x0044f3a0 SaveGame_CreateThumbnail (eax = height, ecx = width; stack: output buffer or 0; caller pops): renders
// the scene into a 9-dword texture record (SceneThumbnail_Render 0x0044f2d0(record, width, height)), locks it
// (renderer vt+0x140(record, &bits, &pitch)), allocates width*height*4 bytes when no buffer was given (tag 0x1a,
// "Thumbnail"), copies the rows, unlocks (vt+0x144) and releases (vt+0x128(record, 1)). Returns the buffer; when
// the render or the lock fails it returns the stack argument (original). Caller 0x0048c620. Not executed in
// recorded runs.
uint32_t SaveGame_CreateThumbnail(uint32_t height, uint32_t width, uint32_t out)
{
    uint32_t bits = 0;
    uint32_t pitch = 0;
    uint32_t record[9] = {};
    if (orig::fn_0044f2d0(u(record), width, height) == 0)
        return out;
    void* r = Field<void*>(Gm(), 0x38);
    if (static_cast<uint8_t>(VCall<uint32_t>(r, 0x140 / 4, u(record), u(&bits), u(&pitch))) != 0)
    {
        if (out == 0)
            out = HeapAlloc_(0x1a, width * height * 4, 2, 0x005b1140);
        if (out != 0)
        {
            uint8_t* dst = B(out);
            const uint32_t row = width * 4;
            for (uint32_t y = height; y != 0; --y)
            {
                std::memcpy(dst, B(bits), row);
                bits += pitch;
                dst += row;
            }
        }
        VCall<uint32_t>(Field<void*>(Gm(), 0x38), 0x144 / 4, u(record));
    }
    VCall<uint32_t>(Field<void*>(Gm(), 0x38), 0x128 / 4, u(record), 1u);
    return out;
}
static uint32_t SaveGame_CreateThumbnail_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return SaveGame_CreateThumbnail(eax, ecx, a0);
}
ABI_ENTRY_0044f3a0(SaveGame_CreateThumbnail_Entry, SaveGame_CreateThumbnail_Abi)
HOOK(0x0044f3a0, "SaveGame_CreateThumbnail", SaveGame_CreateThumbnail_Entry, nullptr);

// 0x00461b40 (ecx = object; stack: save manager; ret 4): virtual method: 0x0048c010(ecx = manager; stack: the
// object's class descriptor (vt slot 1), object). The `push ecx` before the slot-1 getter (plain ret) stays on the
// stack as 0x0048c010's second argument. Not executed in recorded runs.
uint32_t fn_00461b40(void* obj, uint32_t mgr)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    return fn_0048c010(B(mgr), desc, u(obj));
}
static uint32_t fn_00461b40_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00461b40(reinterpret_cast<void*>(ecx), a0);
}
ABI_ENTRY_00461b40(fn_00461b40_Entry, fn_00461b40_Abi)
HOOK(0x00461b40, "fn_00461b40", fn_00461b40_Entry, nullptr);

// 0x00461b60 (ecx = object; stack: save manager; ret 4): as 0x00461b40 with 0x0048bc80 (collects the object's
// references). Callers 0x0048d5d0, 0x0048dd20, 0x0048de40.
uint32_t fn_00461b60(void* obj, uint32_t mgr)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    return fn_0048bc80(B(mgr), desc, u(obj));
}
static uint32_t fn_00461b60_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00461b60(reinterpret_cast<void*>(ecx), a0);
}
ABI_ENTRY_00461b60(fn_00461b60_Entry, fn_00461b60_Abi)
HOOK(0x00461b60, "fn_00461b60", fn_00461b60_Entry, nullptr);

// ================================================================================================================
// 0x00462110 - 0x00462660: options registry and options.opt
// ================================================================================================================

// 0x00462110 (eax = name, edx = table; stack: type, address, size, callback; ret 0x10): appends an entry (at most
// 0x20): +4 name, +0 hash (0x004620b0), +0x10 type, +0x18 callback, +8 address, +0x14 size. Returns the entry or 0.
uint32_t fn_00462110(uint32_t name, uint32_t* t, uint32_t type, uint32_t addr, uint32_t size, uint32_t cb)
{
    const uint32_t n = t[0];
    if (n >= 0x20)
        return 0;
    uint32_t* e = t + 1 + n * 7;
    t[0] = n + 1;
    e[1] = name;
    e[0] = orig::fn_004620b0(name);
    e[4] = type;
    e[6] = cb;
    e[2] = addr;
    e[5] = size;
    return u(e);
}
static uint32_t fn_00462110_Abi(uint32_t eax, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00462110(eax, D(edx), a0, a1, a2, a3);
}
ABI_ENTRY_00462110(fn_00462110_Entry, fn_00462110_Abi)
HOOK(0x00462110, "fn_00462110", fn_00462110_Entry, nullptr);

// 0x00462160 (eax = callback, ecx = address, edi = table; stack: name, default; ret 8): registers a bool option
// (type 2, size 1, byte default) and resets it (0x00462250). AL = registered.
uint32_t fn_00462160(uint32_t cb, uint32_t addr, uint32_t* t, uint32_t name, uint32_t value)
{
    uint32_t* e = D(fn_00462110(name, t, 2, addr, 1, cb));
    if (e != nullptr)
    {
        reinterpret_cast<uint8_t*>(e)[0xc] = static_cast<uint8_t>(value);
        fn_00462250(e, u(t));
    }
    return e != nullptr ? 1u : 0u;
}
static uint32_t fn_00462160_Abi(uint32_t eax, uint32_t ecx, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_00462160(eax, ecx, D(edi), a0, a1);
}
ABI_ENTRY_00462160(fn_00462160_Entry, fn_00462160_Abi)
HOOK(0x00462160, "fn_00462160", fn_00462160_Entry, nullptr);

// 0x00462190 (eax = callback, ecx = address, edi = table; stack: name, default; ret 8): registers an int option
// (type 0, size 4) and resets it. AL = registered.
uint32_t fn_00462190(uint32_t cb, uint32_t addr, uint32_t* t, uint32_t name, uint32_t value)
{
    uint32_t* e = D(fn_00462110(name, t, 0, addr, 4, cb));
    if (e != nullptr)
    {
        e[3] = value;
        fn_00462250(e, u(t));
    }
    return e != nullptr ? 1u : 0u;
}
static uint32_t fn_00462190_Abi(uint32_t eax, uint32_t ecx, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_00462190(eax, ecx, D(edi), a0, a1);
}
ABI_ENTRY_00462190(fn_00462190_Entry, fn_00462190_Abi)
HOOK(0x00462190, "fn_00462190", fn_00462190_Entry, nullptr);

// 0x004621c0 (eax = callback, ecx = address, edi = table; stack: name, default bits; ret 8): registers a float
// option (type 1, size 4) and resets it. AL = registered.
uint32_t fn_004621c0(uint32_t cb, uint32_t addr, uint32_t* t, uint32_t name, uint32_t value)
{
    uint32_t* e = D(fn_00462110(name, t, 1, addr, 4, cb));
    if (e != nullptr)
    {
        e[3] = value;
        fn_00462250(e, u(t));
    }
    return e != nullptr ? 1u : 0u;
}
static uint32_t fn_004621c0_Abi(uint32_t eax, uint32_t ecx, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_004621c0(eax, ecx, D(edi), a0, a1);
}
ABI_ENTRY_004621c0(fn_004621c0_Entry, fn_004621c0_Abi)
HOOK(0x004621c0, "fn_004621c0", fn_004621c0_Entry, nullptr);

// 0x004621f0 (eax = callback, ecx = size, edx = address, edi = table; stack: name; ret 4): registers a blob option
// (type 4, default 0) and resets it (zero fill). AL = registered.
uint32_t fn_004621f0(uint32_t cb, uint32_t size, uint32_t addr, uint32_t* t, uint32_t name)
{
    uint32_t* e = D(fn_00462110(name, t, 4, addr, size, cb));
    if (e != nullptr)
    {
        e[3] = 0;
        fn_00462250(e, u(t));
    }
    return e != nullptr ? 1u : 0u;
}
static uint32_t fn_004621f0_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi, uint32_t a0)
{
    return fn_004621f0(eax, ecx, edx, D(edi), a0);
}
ABI_ENTRY_004621f0(fn_004621f0_Entry, fn_004621f0_Abi)
HOOK(0x004621f0, "fn_004621f0", fn_004621f0_Entry, nullptr);

// 0x00462220 (eax = entry; stack: unused, op; ret 8): calls the entry's callback +0x18 (cdecl: op, name, type,
// address, size) when set. Returns its EAX, else the entry.
uint32_t fn_00462220(uint32_t* e, uint32_t /*a0*/, uint32_t op)
{
    const uint32_t cb = e[6];
    if (cb == 0)
        return u(e);
    return reinterpret_cast<Cb5>(cb)(op, e[1], e[4], e[2], e[5]);
}
static uint32_t fn_00462220_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_00462220(D(eax), a0, a1);
}
ABI_ENTRY_00462220(fn_00462220_Entry, fn_00462220_Abi)
HOOK(0x00462220, "fn_00462220", fn_00462220_Entry, nullptr);

// 0x00462250 (esi = entry; stack: table; ret 4): resets the option to its default (int/float dword, bool byte,
// string copy from the default pointer, blob zero-filled), then the callback with op 0 (0x00462220).
uint32_t fn_00462250(uint32_t* e, uint32_t table)
{
    switch (e[4])
    {
    case 0:
    case 1:
        *D(e[2]) = e[3];
        break;
    case 2:
        *B(e[2]) = reinterpret_cast<uint8_t*>(e)[0xc];
        break;
    case 3:
    {
        const char* s = reinterpret_cast<const char*>(e[3]);
        char* d = reinterpret_cast<char*>(e[2]);
        char c;
        do
        {
            c = *s++;
            *d++ = c;
        } while (c != 0);
        break;
    }
    case 4:
        std::memset(B(e[2]), 0, e[5]);
        break;
    default:
        break;
    }
    return fn_00462220(e, table, 0);
}
static uint32_t fn_00462250_Abi(uint32_t esi, uint32_t a0)
{
    return fn_00462250(D(esi), a0);
}
ABI_ENTRY_00462250(fn_00462250_Entry, fn_00462250_Abi)
HOOK(0x00462250, "fn_00462250", fn_00462250_Entry, nullptr);

// 0x00462310 (eax = file image; stack: table, size; ret 8): parses an options image: magic 0x6f707430 ("0tpo"),
// then records {hash, u16 type, u16 length, data}. A record whose hash matches an entry: type mismatch logs
// "Option is incorrect type\n" (0x005a0ecc); otherwise the value is stored when the length fits (int/float 4,
// bool 1 -> 0/1, string copied unbounded (length 0 = empty), blob only when the length equals the entry size)
// and the callback runs with op 1. AL = 1 when the records end exactly at the end; bad magic: AL = 0 (upper
// bytes of the image pointer).
uint32_t fn_00462310(uint8_t* buf, uint32_t* t, uint32_t size)
{
    uint8_t* end = buf + size;
    if (*reinterpret_cast<uint32_t*>(buf) != 0x6f707430)
        return Lo8(u(buf), 0);
    uint8_t* p = buf + 4;
    if (p < end)
    {
        do
        {
            const uint32_t hash = *reinterpret_cast<uint32_t*>(p);
            const uint16_t type = *reinterpret_cast<uint16_t*>(p + 4);
            const uint16_t len = *reinterpret_cast<uint16_t*>(p + 6);
            p += 8;
            const uint32_t n = t[0];
            uint32_t i = 0;
            for (; i < n; ++i)
                if (t[1 + i * 7] == hash)
                    break;
            if (i != n)
            {
                uint32_t* e = t + 1 + i * 7;
                if (static_cast<int32_t>(static_cast<int8_t>(e[4])) != static_cast<int32_t>(type))
                {
                    GameLog(reinterpret_cast<const char*>(0x005a0ecc));
                }
                else
                {
                    switch (e[4])
                    {
                    case 0:
                    case 1:
                        if (len == 4)
                            *D(e[2]) = *reinterpret_cast<uint32_t*>(p);
                        break;
                    case 2:
                        if (len == 1)
                            *B(e[2]) = *p != 0 ? 1 : 0;
                        break;
                    case 3:
                        if (len == 0)
                        {
                            *B(e[2]) = 0;
                        }
                        else
                        {
                            const uint8_t* s = p;
                            uint8_t* d = B(e[2]);
                            uint8_t c;
                            do
                            {
                                c = *s++;
                                *d++ = c;
                            } while (c != 0);
                        }
                        break;
                    case 4:
                        if (len == e[5])
                            std::memcpy(B(e[2]), p, len);
                        break;
                    default:
                        break;
                    }
                    fn_00462220(e, u(t), 1);
                }
            }
            p += len;
        } while (p < end);
    }
    return p == end ? 1u : 0u;
}
static uint32_t fn_00462310_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_00462310(B(eax), D(a0), a1);
}
ABI_ENTRY_00462310(fn_00462310_Entry, fn_00462310_Abi)
HOOK(0x00462310, "fn_00462310", fn_00462310_Entry, nullptr);

// 0x00462450 (ecx not read (push ecx is a local); stack: table, buffer, capacity; ret 0xc): writes the options
// image (format of 0x00462310) into the buffer: 0 when the buffer is null or capacity < 0x80; stops before an entry
// once the bytes written reach capacity - 0x20. Each entry first gets its callback with op 2. Returns the bytes
// written (0 for an empty table).
uint32_t fn_00462450(uint32_t* t, uint8_t* buf, uint32_t cap)
{
    if (buf == nullptr || cap < 0x80)
        return 0;
    const uint32_t limit = cap - 0x20;
    *reinterpret_cast<uint32_t*>(buf) = 0x6f707430;
    uint8_t* p = buf + 4;
    uint32_t written = 0;
    for (uint32_t i = 0; i < t[0]; ++i)
    {
        if (written >= limit)
            break;
        uint32_t* e = t + 1 + i * 7;
        fn_00462220(e, u(t), 2);
        *reinterpret_cast<uint32_t*>(p) = e[0];
        p += 4;
        *reinterpret_cast<int16_t*>(p) = static_cast<int16_t>(static_cast<int8_t>(e[4]));
        p += 2;
        switch (e[4])
        {
        case 2:
            *reinterpret_cast<uint16_t*>(p) = 1;
            p += 2;
            *p = *B(e[2]) != 0 ? 1 : 0;
            p += 1;
            break;
        case 0:
        case 1:
            *reinterpret_cast<uint16_t*>(p) = 4;
            p += 2;
            *reinterpret_cast<uint32_t*>(p) = *D(e[2]);
            p += 4;
            break;
        case 3:
        {
            const char* s = reinterpret_cast<const char*>(e[2]);
            *reinterpret_cast<uint16_t*>(p) = static_cast<uint16_t>(std::strlen(s) + 1);
            p += 2;
            uint8_t* d = p;
            char c;
            do
            {
                c = *s++;
                *d++ = static_cast<uint8_t>(c);
            } while (c != 0);
            p += *reinterpret_cast<uint16_t*>(p - 2);
            break;
        }
        case 4:
            *reinterpret_cast<uint16_t*>(p) = static_cast<uint16_t>(e[5]);
            p += 2;
            std::memcpy(p, B(e[2]), e[5]);
            p += e[5];
            break;
        default:
            *reinterpret_cast<uint16_t*>(p) = 0;
            p += 2;
            break;
        }
        written = static_cast<uint32_t>(p - buf);
    }
    return written;
}
static uint32_t fn_00462450_Abi(uint32_t /*ecx*/, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00462450(D(a0), B(a1), a2);
}
ABI_ENTRY_00462450(fn_00462450_Entry, fn_00462450_Abi)
HOOK(0x00462450, "fn_00462450", fn_00462450_Entry, nullptr);

// 0x004625d0 (stack: table, &buffer, &size; ret 0xc): allocates 0x10000 bytes (tag 5, "Save options temp space";
// not null-checked here, 0x00462450 returns 0 for null) and writes the options image into it. AL = size != 0
// (upper bytes of the size).
uint32_t fn_004625d0(uint32_t* t, uint32_t* outBuf, uint32_t* outSize)
{
    const uint32_t buf = HeapAlloc_(5, 0x10000, 2, 0x005a0eb4);
    *outBuf = buf;
    const uint32_t size = fn_00462450(t, B(buf), 0x10000);
    *outSize = size;
    return Lo8(size, size != 0 ? 1 : 0);
}
static uint32_t fn_004625d0_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004625d0(D(a0), D(a1), D(a2));
}
ABI_ENTRY_004625d0(fn_004625d0_Entry, fn_004625d0_Abi)
HOOK(0x004625d0, "fn_004625d0", fn_004625d0_Entry, nullptr);

// 0x00462610 (edx = table): builds the options image (0x004625d0) and writes it to "options.opt" (0x0048b150,
// eax = 0, ecx = name; buffer, size), then frees the buffer. Caller 0x00401e60. Returns the free's EAX, or
// 0x004625d0's when the image is empty (the buffer then leaks, original).
uint32_t fn_00462610(uint32_t* t)
{
    uint32_t size = 0;
    uint32_t buf = 0;
    const uint32_t r = fn_004625d0(t, &buf, &size);
    if (static_cast<uint8_t>(r) == 0)
        return r;
    fn_0048b150(0, 0x005a0ea8, buf, size);
    return Free(buf);
}
static uint32_t fn_00462610_Abi(uint32_t edx)
{
    return fn_00462610(D(edx));
}
ABI_ENTRY_00462610(fn_00462610_Entry, fn_00462610_Abi)
HOOK(0x00462610, "fn_00462610", fn_00462610_Entry, nullptr);

// 0x00462660 (stack: table; ret 4): loads "options.opt": AL = 1 when the file does not exist (0x0048b210); reads
// it (0x0048b1a0: eax = 0, ecx = name, edi = &size; &buffer), AL = 0 when that fails; otherwise parses it
// (0x00462310, result ignored), frees the buffer and returns AL = 1. Callers 0x00401a30, Options_Register.
uint32_t fn_00462660(uint32_t table)
{
    uint32_t r = fn_0048b210(0x005a0ea8, 0);
    if (static_cast<uint8_t>(r) == 0)
        return Lo8(r, 1);
    uint32_t size = 0;
    uint32_t buf = 0;
    r = fn_0048b1a0(0, 0x005a0ea8, &size, &buf);
    if (static_cast<uint8_t>(r) == 0)
        return Lo8(r, 0);
    fn_00462310(B(buf), D(table), size);
    r = Free(buf);
    return Lo8(r, 1);
}
static uint32_t fn_00462660_Abi(uint32_t a0)
{
    return fn_00462660(a0);
}
ABI_ENTRY_00462660(fn_00462660_Entry, fn_00462660_Abi)
HOOK(0x00462660, "fn_00462660", fn_00462660_Entry, nullptr);

// ================================================================================================================
// 0x004881f0 - 0x00488220: scene pass index lookups (save manager types 10)
// ================================================================================================================

// 0x004881f0 (eax not read, esi = pointer, edi = table {+0x18 entries of 0x24 bytes, +0x1c pointer array, +0x20
// count}): index i where esi == &entries[i] or esi == array[i], else -1 (also for null). Used by the save writer
// 0x0048bda0 (type 10, table = game+0xa78). Not executed in recorded runs.
uint32_t fn_004881f0(uint32_t key, uint8_t* t)
{
    if (key == 0)
        return 0xffffffffu;
    const uint32_t n = Field<uint32_t>(t, 0x20);
    uint32_t e = Field<uint32_t>(t, 0x18);
    const uint32_t* arr = Field<uint32_t*>(t, 0x1c);
    for (uint32_t i = 0; i < n; ++i, e += 0x24)
        if (key == e || key == arr[i])
            return i;
    return 0xffffffffu;
}
static uint32_t fn_004881f0_Abi(uint32_t /*eax*/, uint32_t esi, uint32_t edi)
{
    return fn_004881f0(esi, B(edi));
}
ABI_ENTRY_004881f0(fn_004881f0_Entry, fn_004881f0_Abi)
HOOK(0x004881f0, "fn_004881f0", fn_004881f0_Entry, nullptr);

// 0x00488220 (eax = index, ecx = table): -1 -> 0, else [table+0x1c][index]. Inverse of 0x004881f0 for the save
// reader 0x0048c0f0. Not executed in recorded runs.
uint32_t fn_00488220(uint32_t index, uint8_t* t)
{
    if (index == 0xffffffffu)
        return 0;
    return Field<uint32_t*>(t, 0x1c)[index];
}
static uint32_t fn_00488220_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00488220(eax, B(ecx));
}
ABI_ENTRY_00488220(fn_00488220_Entry, fn_00488220_Abi)
HOOK(0x00488220, "fn_00488220", fn_00488220_Entry, nullptr);

// ================================================================================================================
// 0x0048b150 - 0x0048b500: files under SavedGames
// ================================================================================================================

// 0x0048b150 (eax = wide slot name or 0, ecx = file name; stack: buffer, size; caller pops): writes a whole file:
// opens a compressed file in write mode (0x0048b0a0 mode 1), FileStream_Open, writes (0x0048ac90: ecx = size;
// file, buffer), CompressedFile_Close (its ecx scratch: 0) and destroys it (0x0048b130). AL = write result.
// Caller 0x00462610.
uint32_t fn_0048b150(uint32_t eax, uint32_t ecx, uint32_t buf, uint32_t size)
{
    uint8_t ok = 0;
    const uint32_t s = orig::fn_0048b0a0(eax, ecx, 1);
    if (s == 0)
        return 0;
    if (static_cast<uint8_t>(orig::fn_0048aac0(s)) != 0)
    {
        ok = static_cast<uint8_t>(orig::fn_0048ac90(size, s, buf));
        orig::fn_0048aff0(s, 0);
    }
    return Lo8(orig::fn_0048b130(s), ok);
}
static uint32_t fn_0048b150_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0048b150(eax, ecx, a0, a1);
}
ABI_ENTRY_0048b150(fn_0048b150_Entry, fn_0048b150_Abi)
HOOK(0x0048b150, "fn_0048b150", fn_0048b150_Entry, nullptr);

// 0x0048b1a0 (eax = wide slot name or 0, ecx = file name, edi = &size; stack: &buffer; caller pops): reads a whole
// file: opens it in read mode (0x0048b0a0 mode 0), FileStream_Open, *edi = its length (0x0048aab0); when nonzero
// allocates it (tag 5, "Save game file"), stores the buffer and reads (0x0048af10: ecx = file; buffer, size);
// closes and destroys it. AL = read result. symbols/abi.csv omits the EDI input (0x0048b1c6 mov [edi], eax; caller
// 0x00462660 sets edi = &size): hand-written entry stub. Caller 0x00462660.
uint32_t fn_0048b1a0(uint32_t eax, uint32_t ecx, uint32_t* outSize, uint32_t* outBuf)
{
    uint8_t ok = 0;
    const uint32_t s = orig::fn_0048b0a0(eax, ecx, 0);
    if (s == 0)
        return 0;
    if (static_cast<uint8_t>(orig::fn_0048aac0(s)) != 0)
    {
        const uint32_t size = orig::fn_0048aab0(s);
        *outSize = size;
        if (size != 0)
        {
            const uint32_t buf = HeapAlloc_(5, size, 2, 0x0059d9b8);
            *outBuf = buf;
            ok = static_cast<uint8_t>(orig::fn_0048af10(s, buf, *outSize));
        }
        orig::fn_0048aff0(s, 0);
    }
    return Lo8(orig::fn_0048b130(s), ok);
}
static uint32_t fn_0048b1a0_Abi(uint32_t eax, uint32_t ecx, uint32_t edi, uint32_t a0)
{
    return fn_0048b1a0(eax, ecx, D(edi), D(a0));
}
SV_ENTRY_EAX_ECX_EDI_S1(fn_0048b1a0_Entry, fn_0048b1a0_Abi)
HOOK(0x0048b1a0, "fn_0048b1a0", fn_0048b1a0_Entry, nullptr);

// 0x0048b210 (ecx = file name; stack: wide slot name; caller pops): resolves the path (0x0048b260, AL = 0) and
// checks that it exists (0x004f1e60, esi = path: FindFirstFile + FindClose; its other listed inputs ebx/edi/ebp are
// only saved by the CRT security epilogue: 0). AL = exists. Caller 0x00462660.
uint32_t fn_0048b210(uint32_t fname, uint32_t wname)
{
    char path[0x100];
    const uint32_t r = fn_0048b260(Lo8(u(path), 0), wname, fname, u(path), 0x100);
    if (static_cast<uint8_t>(r) == 0)
        return Lo8(r, 0);
    return orig::fn_004f1e60(0, u(path), 0, 0);
}
static uint32_t fn_0048b210_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048b210(ecx, a0);
}
ABI_ENTRY_0048b210(fn_0048b210_Entry, fn_0048b210_Abi)
HOOK(0x0048b210, "fn_0048b210", fn_0048b210_Entry, nullptr);

// 0x0048b260 (eax: AL = create the directory, ecx = wide slot name or 0; stack: file name, output path, output
// size (never read); caller pops): builds the save file path. AL set: 0x004f1e00("SavedGames\\") creates the
// directory (its listed esi/ebp inputs are not read; passed as the original's values). No slot: "SavedGames\\" +
// name; slot: the slot name narrowed (at most 0x103 chars) + "__" + name (sprintf "__%s" 0x0059d9a4) inside
// "SavedGames\\". Final path = _makepath(out, 0, root 0x0063a208, that, 0) (unbounded, original). AL = 1. Callers
// 0x0048b0a0, 0x0048b210, 0x0048b500.
uint32_t fn_0048b260(uint32_t eax, uint32_t wname, uint32_t fname, uint32_t out, uint32_t /*outSize*/)
{
    char frame[0x208];  // +0 the relative path, +0x104 the narrowed slot name
    char* a = frame;
    char* n = frame + 0x104;
    if (static_cast<uint8_t>(eax) != 0)
        orig::fn_004f1e00(wname, fname, 0x0059d9ac);
    if (wname == 0)
    {
        MakePath(u(a), 0, 0x0059d9ac, fname, 0);
    }
    else
    {
        const wchar_t* w = reinterpret_cast<const wchar_t*>(wname);
        const uint32_t len = GameFunction<uint32_t(__cdecl*)(const wchar_t*)>(0x00538ea0)(w);
        uint32_t left = 0x104;
        uint32_t i = 0;
        if (len != 0)
        {
            for (;;)
            {
                if (--left == 0)
                    break;
                n[i] = static_cast<char>(w[i]);
                ++i;
                if (i >= len)
                    break;
            }
        }
        n[i] = 0;
        GameFunction<int(__cdecl*)(char*, const char*, uint32_t)>(0x0053814f)(
            a, reinterpret_cast<const char*>(0x0059d9a4), fname);
        std::strcat(n, a);
        MakePath(u(a), 0, 0x0059d9ac, u(n), 0);
    }
    const uint32_t r = MakePath(out, 0, GameGlobal<uint32_t>(0x0063a208), u(a), 0);
    return Lo8(r, 1);
}
static uint32_t fn_0048b260_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0048b260(eax, ecx, a0, a1, a2);
}
ABI_ENTRY_0048b260(fn_0048b260_Entry, fn_0048b260_Abi)
HOOK(0x0048b260, "fn_0048b260", fn_0048b260_Entry, nullptr);

// 0x0048b370 (esi = cdecl callback(wide slot name, path, context) or 0; stack: context, wide output or 0, path
// output or 0; caller pops): enumerates "SavedGames\\*__savegame.dat" (0x004f2030: ebx = find state, edi = 0); for
// each non-directory builds the full path; with a callback, splits the file name, cuts it at "__savegame", widens
// it (0x004e59d0: eax = 0x100, ecx = wide buffer, edi = name) and calls the callback; the first one returning
// AL != 0 ends the search: path copied to the path output, slot name to the wide output (0x00403be0: eax = name,
// edx = output, edi = 0), AL = 1. Else AL = 0. FindNextFileA / FindClose through the IAT. Caller 0x004a2c10.
uint32_t fn_0048b370(uint32_t callback, uint32_t context, uint32_t wout, uint32_t pout)
{
    uint8_t f[0x65c] = {};  // the original frame from its lowest local: +0xf found byte, +0x10 path,
                            // +0x114 find state (handle, WIN32_FIND_DATAA +0x118, directory +0x258),
                            // +0x35c file name, +0x45c wide slot name
    auto at = [&](uint32_t o) { return u(f + o); };
    f[0xf] = 0;
    MakePath(at(0x10), 0, 0x0059d9ac, 0x0059d994, 0);
    const uint32_t r = CallEbxEdi1(0x004f2030, at(0x114), 0, at(0x10));
    if (static_cast<uint8_t>(r) == 0)
        return Lo8(r, f[0xf]);
    auto findNext = Imp<decltype(&FindNextFileA)>(0x0056b0d8);
    for (;;)
    {
        if (((Field<uint32_t>(f, 0x118) >> 4) & 1) == 0)
        {
            MakePath(at(0x10), 0, at(0x258), at(0x144), 0);
            if (callback != 0)
            {
                SplitPath(at(0x10), 0, 0, at(0x35c), 0);
                char* cut = GameFunction<char*(__cdecl*)(uint32_t, uint32_t)>(0x005383d0)(at(0x35c), 0x0059d988);
                if (cut != nullptr)
                {
                    *cut = 0;
                    orig::fn_004e59d0(0x100, at(0x45c), at(0x35c));
                    const uint32_t hit = reinterpret_cast<Cb3>(callback)(at(0x45c), at(0x10), context);
                    if (static_cast<uint8_t>(hit) != 0)
                    {
                        if (pout != 0)
                            std::strcpy(reinterpret_cast<char*>(pout), reinterpret_cast<const char*>(f + 0x10));
                        if (wout != 0)
                            orig::fn_00403be0(at(0x45c), wout, 0);
                        const uint32_t c = Imp<decltype(&FindClose)>(0x0056b0dc)(Field<HANDLE>(f, 0x114));
                        return Lo8(c, 1);
                    }
                }
            }
        }
        if (findNext(Field<HANDLE>(f, 0x114), reinterpret_cast<LPWIN32_FIND_DATAA>(f + 0x118)) == 0)
            break;
    }
    const uint8_t found = f[0xf];
    const uint32_t c = Imp<decltype(&FindClose)>(0x0056b0dc)(Field<HANDLE>(f, 0x114));
    return Lo8(c, found);
}
static uint32_t fn_0048b370_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0048b370(esi, a0, a1, a2);
}
ABI_ENTRY_0048b370(fn_0048b370_Entry, fn_0048b370_Abi)
HOOK(0x0048b370, "fn_0048b370", fn_0048b370_Entry, nullptr);

// 0x0048b500 (ecx = wide slot name): deletes every file of the slot: path of "*.*" (0x005b2550) via 0x0048b260
// (AL = 0), enumerates it (0x004f1f40: ecx = find state, esi = pattern; stack 0), and for each non-directory
// builds the full path, turns '/' into '\\' and DeleteFileA's it. Returns AL = 1 always. Callers 0x004a2cb0,
// 0x004a2e40.
uint32_t fn_0048b500(uint32_t wname)
{
    uint8_t f[0x454] = {};  // original frame: +4 length, +8 path copy, +0x108 path, +0x20c find state
                            // (handle, WIN32_FIND_DATAA +0x210, directory +0x350)
    auto at = [&](uint32_t o) { return u(f + o); };
    fn_0048b260(Lo8(at(0x108), 0), wname, 0x005b2550, at(0x108), 0x104);
    const uint32_t r = CallEcxEsi1(0x004f1f40, at(0x20c), at(0x108), 0);
    if (static_cast<uint8_t>(r) == 0)
        return Lo8(r, 1);
    auto findNext = Imp<decltype(&FindNextFileA)>(0x0056b0d8);
    auto del = Imp<decltype(&DeleteFileA)>(0x0056b0c4);
    do
    {
        if (((Field<uint32_t>(f, 0x210) >> 4) & 1) == 0)
        {
            MakePath(at(0x108), 0, at(0x350), at(0x23c), 0);
            char* q = reinterpret_cast<char*>(f + 8);
            std::strcpy(q, reinterpret_cast<const char*>(f + 0x108));
            const uint32_t len = static_cast<uint32_t>(std::strlen(q));
            Field<uint32_t>(f, 4) = len;
            if (len != 0)
            {
                for (uint32_t i = 0; i < static_cast<uint32_t>(std::strlen(q)); ++i)
                    if (q[i] == '/')
                        q[i] = '\\';
            }
            del(q);
        }
    } while (findNext(Field<HANDLE>(f, 0x20c), reinterpret_cast<LPWIN32_FIND_DATAA>(f + 0x210)) != 0);
    const uint32_t c = Imp<decltype(&FindClose)>(0x0056b0dc)(Field<HANDLE>(f, 0x20c));
    return Lo8(c, 1);
}
static uint32_t fn_0048b500_Abi(uint32_t ecx)
{
    return fn_0048b500(ecx);
}
ABI_ENTRY_0048b500(fn_0048b500_Entry, fn_0048b500_Abi)
HOOK(0x0048b500, "fn_0048b500", fn_0048b500_Entry, nullptr);

// ================================================================================================================
// 0x0048b620 - 0x0048c5c0: the save manager
// ================================================================================================================
// Save manager (guess from use; 0x102c bytes): +0 object count, +4 object array (0x0048ba60), +8 count, +0xc
// checksum, +0x10 array (0x0048bab0), +0x14 compressed file, +0x18 string bytes, +0x1c {free list of 16-byte nodes,
// +0x20 root of the string tree (node: +0 child 0, +4 child 1, +8 hash, +0xc string)}, +0x24 256-node pool,
// +0x1024 string memory, +0x1028 version hash of the class descriptor tables (0x0048c3c0). Class descriptor table
// entries are 6 dwords: type, offset, stride, count, sub-table / flag, name.

uint32_t fn_0048b680(uint8_t* m, uint32_t size);
uint32_t fn_0048b720(uint8_t* m, uint32_t ecx, uint32_t ebx);
uint32_t fn_0048b750(uint32_t rec, uint32_t mgr);
uint32_t fn_0048b9d0(uint8_t* m, uint32_t keep);
uint32_t fn_0048ba60(uint32_t obj, uint8_t* m);
uint32_t fn_0048bab0(uint32_t item, uint8_t* m, uint32_t name);
uint32_t fn_0048bb20(uint32_t str, uint8_t* m);
uint32_t fn_0048bb90(uint8_t* m, uint32_t obj);
uint32_t fn_0048bbd0(uint8_t* m, uint32_t obj);
uint32_t fn_0048c380(uint32_t size, uint8_t* m, uint32_t src);
uint32_t fn_0048c3a0(uint32_t size, uint8_t* m, uint32_t dst);
uint32_t fn_0048c3c0(uint8_t* m);
uint32_t fn_0048c440(uint32_t old, uint32_t newCount, uint32_t oldCount);
uint32_t fn_0048c4f0(uint32_t node, uint32_t cb, uint32_t ctx);
uint32_t fn_0048cb30(uint32_t id, uint8_t* m);
uint32_t fn_0048e3f0(uint32_t p, uint32_t name, uint8_t* m, uint32_t noItems);
uint32_t fn_0048e460(uint32_t p, uint32_t mgr, uint32_t a1, uint32_t name);
uint32_t fn_0048e4b0(uint8_t* m, uint32_t list, uint32_t mode);
uint32_t fn_0048e630(uint32_t p, uint8_t* m, uint32_t mode, uint32_t a1);

// 0x0048b620 (eax = manager): constructor: clears +0..+0x1028, resets (0x0048b9d0 keep 0) and computes the
// version hash (0x0048c3c0, ebx = manager; its ecx is not read). Returns the manager. Caller 0x004011e0.
uint32_t fn_0048b620(uint8_t* m)
{
    Field<uint32_t>(m, 0x1c) = 0;
    Field<uint32_t>(m, 0x20) = 0;
    std::memset(m, 0, 0x18);
    std::memset(m + 0x18, 0, 0x404 * 4);
    fn_0048b9d0(m, 0);
    fn_0048c3c0(m);
    return u(m);
}
static uint32_t fn_0048b620_Abi(uint32_t eax)
{
    return fn_0048b620(B(eax));
}
ABI_ENTRY_0048b620(fn_0048b620_Entry, fn_0048b620_Abi)
HOOK(0x0048b620, "fn_0048b620", fn_0048b620_Entry, nullptr);

// 0x0048b660 (eax = manager): destructor: closes an open file (0x0048b720: ecx passed on as scratch, BL = 0; both
// passed as 0) and frees the string memory (0x0048b680, edi = 0). Returns 0x0048b680's EAX. Caller 0x00401710.
// Not executed in recorded runs.
uint32_t fn_0048b660(uint8_t* m)
{
    if (Field<uint32_t>(m, 0x14) != 0)
        fn_0048b720(m, 0, 0);
    return fn_0048b680(m, 0);
}
static uint32_t fn_0048b660_Abi(uint32_t eax)
{
    return fn_0048b660(B(eax));
}
ABI_ENTRY_0048b660(fn_0048b660_Entry, fn_0048b660_Abi)
HOOK(0x0048b660, "fn_0048b660", fn_0048b660_Entry, nullptr);

// 0x0048b680 (esi = manager, edi = size): frees the string memory +0x1024 and, when size != 0, allocates a new one
// (tag 6, "Save game string memory"). Returns +0x1024.
uint32_t fn_0048b680(uint8_t* m, uint32_t size)
{
    const uint32_t old = Field<uint32_t>(m, 0x1024);
    if (old != 0)
    {
        Free(old);
        Field<uint32_t>(m, 0x1024) = 0;
    }
    if (size != 0)
        Field<uint32_t>(m, 0x1024) = HeapAlloc_(6, size, 2, 0x0059d970);
    return Field<uint32_t>(m, 0x1024);
}
static uint32_t fn_0048b680_Abi(uint32_t esi, uint32_t edi)
{
    return fn_0048b680(B(esi), edi);
}
ABI_ENTRY_0048b680(fn_0048b680_Entry, fn_0048b680_Abi)
HOOK(0x0048b680, "fn_0048b680", fn_0048b680_Entry, nullptr);

// 0x0048b6d0 (eax = manager; stack: mode (1 write, 0 read), wide slot name; ret 8): opens "savegame.dat"
// (0x0059d960) of the slot (0x0048b0a0) into +0x14 and FileStream_Open's it; on failure resets (0x0048b9d0 keep 0).
// AL = opened; AL = 0 when a file is already open. Callers 0x0048c620, 0x0048c870, 0x0048c9b0.
uint32_t fn_0048b6d0(uint8_t* m, uint32_t mode, uint32_t wname)
{
    if (Field<uint32_t>(m, 0x14) != 0)
        return Lo8(Field<uint32_t>(m, 0x14), 0);
    const uint32_t s = orig::fn_0048b0a0(wname, 0x0059d960, mode);
    Field<uint32_t>(m, 0x14) = s;
    if (s != 0)
    {
        const uint32_t r = orig::fn_0048aac0(s);
        if (static_cast<uint8_t>(r) != 0)
            return r;
    }
    return Lo8(fn_0048b9d0(m, 0), 0);
}
static uint32_t fn_0048b6d0_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_0048b6d0(B(eax), a0, a1);
}
ABI_ENTRY_0048b6d0(fn_0048b6d0_Entry, fn_0048b6d0_Abi)
HOOK(0x0048b6d0, "fn_0048b6d0", fn_0048b6d0_Entry, nullptr);

// 0x0048b720 (eax = manager, ecx = scratch passed to CompressedFile_Close, ebx: BL = result): when a file is open,
// closes it (CompressedFile_Close) and resets (0x0048b9d0 keep 0). Returns AL = BL. Callers 0x0048b660,
// 0x0048c620, 0x0048c870, 0x0048c9b0.
uint32_t fn_0048b720(uint8_t* m, uint32_t ecx, uint32_t ebx)
{
    uint32_t r = Field<uint32_t>(m, 0x14);
    if (r != 0)
    {
        orig::fn_0048aff0(r, ecx);
        r = fn_0048b9d0(m, 0);
    }
    return Lo8(r, ebx);
}
static uint32_t fn_0048b720_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx)
{
    return fn_0048b720(B(eax), ecx, ebx);
}
ABI_ENTRY_0048b720(fn_0048b720_Entry, fn_0048b720_Abi)
HOOK(0x0048b720, "fn_0048b720", fn_0048b720_Entry, nullptr);

// 0x0048b740 (esi = manager): frees the string memory (0x0048b680, edi = 0). Caller 0x0044a670.
uint32_t fn_0048b740(uint8_t* m)
{
    return fn_0048b680(m, 0);
}
static uint32_t fn_0048b740_Abi(uint32_t esi)
{
    return fn_0048b740(B(esi));
}
ABI_ENTRY_0048b740(fn_0048b740_Entry, fn_0048b740_Abi)
HOOK(0x0048b740, "fn_0048b740", fn_0048b740_Entry, nullptr);

// 0x0048b750 (cdecl; stack: string tree node, manager): writes the node's string (+0xc) with its terminator
// (0x0048c380). Callback of the string-tree walk 0x0048c4f0 (from 0x0048b780). Not executed in recorded runs.
uint32_t fn_0048b750(uint32_t rec, uint32_t mgr)
{
    const uint32_t s = Field<uint32_t>(B(rec), 0xc);
    const uint32_t len = static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(s)));
    return fn_0048c380(len + 1, B(mgr), s);
}
static uint32_t fn_0048b750_Abi(uint32_t a0, uint32_t a1)
{
    return fn_0048b750(a0, a1);
}
ABI_ENTRY_0048b750(fn_0048b750_Entry, fn_0048b750_Abi)
HOOK(0x0048b750, "fn_0048b750", fn_0048b750_Entry, nullptr);

// 0x0048b780 (eax = manager): runs the open save file. Read mode (flags bit 1 set, bit 0 clear): object count,
// each object's id recreated by 0x0048cb30 (0 aborts), the string block into the string memory (each string
// re-entered by 0x0048bb20), every object's vt+0x10(manager), the checksum (mismatch with +0xc aborts), then
// vt+0x14(manager). Write mode (bits 1 and 0): vt+8(manager) each, the count, each vt+0 (type id), +0x18, the
// string tree (0x0048b750 / 0x0048c4f0), the checksum +0xc, then vt+0xc(manager) each. Always ends with
// 0x0048b9d0 (keep 1). AL = 1 on success. Callers 0x0048c620, 0x0048c9b0.
uint32_t fn_0048b780(uint8_t* m)
{
    uint32_t l0c = 0;
    uint32_t l10 = 0;
    uint32_t l14 = 0;
    uint8_t ok = 0;
    uint8_t* s = Field<uint8_t*>(m, 0x14);
    auto obj = [&](uint32_t i) { return reinterpret_cast<void*>(Field<uint32_t*>(m, 4)[i]); };
    if (s == nullptr)
        return Lo8(fn_0048b9d0(m, 1), 0);
    if ((s[0] & 2) != 0 && (s[0] & 1) == 0)
    {
        ok = 1;
        fn_0048c3a0(4, m, u(&l10));
        for (uint32_t i = 0; i < l10; ++i)
        {
            fn_0048c3a0(4, m, u(&l0c));
            if (fn_0048cb30(l0c, m) == 0)
                return Lo8(fn_0048b9d0(m, 1), 0);
        }
        fn_0048c3a0(4, m, u(&l0c));
        uint32_t p = fn_0048b680(m, l0c);
        fn_0048c3a0(l0c, m, Field<uint32_t>(m, 0x1024));
        while (p < Field<uint32_t>(m, 0x1024) + l0c)
            p = fn_0048bb20(p, m);
        for (uint32_t i = 0; i < l10; ++i)
            VCall<uint32_t>(obj(i), 0x10 / 4, u(m));
        fn_0048c3a0(4, m, u(&l14));
        if (l14 != Field<uint32_t>(m, 0xc))
            return Lo8(fn_0048b9d0(m, 1), 0);
        for (uint32_t i = 0; i < l10; ++i)
            VCall<uint32_t>(obj(i), 0x14 / 4, u(m));
        return Lo8(fn_0048b9d0(m, 1), ok);
    }
    if ((s[0] & 2) != 0 && (s[0] & 1) != 0)
    {
        ok = 1;
        for (uint32_t i = 0; i < Field<uint32_t>(m, 0); ++i)
            VCall<uint32_t>(obj(i), 8 / 4, u(m));
        fn_0048c380(4, m, u(m));
        for (uint32_t i = 0; i < Field<uint32_t>(m, 0); ++i)
        {
            l0c = VCall<uint32_t>(obj(i), 0);
            fn_0048c380(4, m, u(&l0c));
        }
        fn_0048c380(4, m, u(m + 0x18));
        const uint32_t* root = Field<uint32_t*>(m, 0x20);
        if (root != nullptr)
        {
            fn_0048b750(u(root), u(m));
            if (root[0] != 0)
                fn_0048c4f0(root[0], 0x0048b750, u(m));
            if (root[1] != 0)
                fn_0048c4f0(root[1], 0x0048b750, u(m));
        }
        fn_0048c380(4, m, u(m + 0xc));
        for (uint32_t i = 0; i < Field<uint32_t>(m, 0); ++i)
            VCall<uint32_t>(obj(i), 0xc / 4, u(m));
    }
    return Lo8(fn_0048b9d0(m, 1), ok);
}
static uint32_t fn_0048b780_Abi(uint32_t eax)
{
    return fn_0048b780(B(eax));
}
ABI_ENTRY_0048b780(fn_0048b780_Entry, fn_0048b780_Abi)
HOOK(0x0048b780, "fn_0048b780", fn_0048b780_Entry, nullptr);

// 0x0048b9d0 (esi = manager; stack: keep file (byte); ret 4): frees the object array (+0 = 0 before the free) and
// the +0x10 array (+0xc = +8 = 0), rebuilds the 256-node free list at +0x24 (+0x1c = pool, +0x20 = +0x18 = 0);
// unless keep, destroys the file (0x0048b130) and clears +0..+0x14. Returns 0.
uint32_t fn_0048b9d0(uint8_t* m, uint32_t keep)
{
    if (Field<uint32_t>(m, 4) != 0)
    {
        const uint32_t a = Field<uint32_t>(m, 4);
        Field<uint32_t>(m, 0) = 0;
        Free(a);
        Field<uint32_t>(m, 4) = 0;
    }
    if (Field<uint32_t>(m, 0x10) != 0)
    {
        const uint32_t a = Field<uint32_t>(m, 0x10);
        Field<uint32_t>(m, 0xc) = 0;
        Field<uint32_t>(m, 8) = 0;
        Free(a);
        Field<uint32_t>(m, 0x10) = 0;
    }
    uint8_t* pool = m + 0x24;
    Field<uint32_t>(m, 0x20) = 0;
    Field<uint32_t>(m, 0x1c) = 0;
    std::memset(pool, 0, 0x400 * 4);
    for (uint32_t i = 0; i < 0xff; ++i)
        Field<uint32_t>(pool, i * 0x10) = u(pool + (i + 1) * 0x10);
    Field<uint32_t>(pool, 0xff0) = Field<uint32_t>(m, 0x1c);
    Field<uint32_t>(m, 0x1c) = u(pool);
    Field<uint32_t>(m, 0x18) = 0;
    if (static_cast<uint8_t>(keep) == 0)
    {
        if (Field<uint32_t>(m, 0x14) != 0)
        {
            orig::fn_0048b130(Field<uint32_t>(m, 0x14));
            Field<uint32_t>(m, 0x14) = 0;
        }
        std::memset(m, 0, 0x18);
    }
    return 0;
}
static uint32_t fn_0048b9d0_Abi(uint32_t esi, uint32_t a0)
{
    return fn_0048b9d0(B(esi), a0);
}
ABI_ENTRY_0048b9d0(fn_0048b9d0_Entry, fn_0048b9d0_Abi)
HOOK(0x0048b9d0, "fn_0048b9d0", fn_0048b9d0_Entry, nullptr);

// 0x0048ba60 (eax = object, edi = manager): adds the object to the manager's object list unless null (AL = 1),
// its vt+0 (type id) is 0 (AL = 0) or it is already listed (0x0048bb90). The array grows by 0x40 (0x0048c440).
uint32_t fn_0048ba60(uint32_t obj, uint8_t* m)
{
    if (obj == 0)
        return 1;
    const uint32_t id = VCall<uint32_t>(reinterpret_cast<void*>(obj), 0);
    if (id == 0)
        return 0;
    const uint32_t found = fn_0048bb90(m, obj);
    if (found != 0)
        return Lo8(found, 1);
    const uint32_t n = Field<uint32_t>(m, 0);
    if ((n & 0x3f) == 0)
        Field<uint32_t>(m, 4) = fn_0048c440(Field<uint32_t>(m, 4), n + 0x40, n);
    uint32_t* arr = Field<uint32_t*>(m, 4);
    arr[Field<uint32_t>(m, 0)] = obj;
    Field<uint32_t>(m, 0) += 1;
    return Lo8(u(arr), 1);
}
static uint32_t fn_0048ba60_Abi(uint32_t eax, uint32_t edi)
{
    return fn_0048ba60(eax, B(edi));
}
ABI_ENTRY_0048ba60(fn_0048ba60_Entry, fn_0048ba60_Abi)
HOOK(0x0048ba60, "fn_0048ba60", fn_0048ba60_Entry, nullptr);

// 0x0048bab0 (eax = item, edi = manager; stack: name; ret 4): adds the item to the +0x10 list unless listed
// (returns 0x0048bbd0's index + 1); the checksum +0xc ^= rotr(hash(name) (0x0044ad70), (count * 3) & 31). Returns
// the new count.
uint32_t fn_0048bab0(uint32_t item, uint8_t* m, uint32_t name)
{
    const uint32_t found = fn_0048bbd0(m, item);
    if (found != 0)
        return found;
    const uint32_t n = Field<uint32_t>(m, 8);
    if ((n & 0x3f) == 0)
        Field<uint32_t>(m, 0x10) = fn_0048c440(Field<uint32_t>(m, 0x10), n + 0x40, n);
    const uint32_t h = Hash(name);
    const uint32_t c = Field<uint32_t>(m, 8);
    Field<uint32_t>(m, 0xc) ^= RotR(h, c * 3);
    Field<uint32_t*>(m, 0x10)[c] = item;
    Field<uint32_t>(m, 8) = c + 1;
    return c + 1;
}
static uint32_t fn_0048bab0_Abi(uint32_t eax, uint32_t edi, uint32_t a0)
{
    return fn_0048bab0(eax, B(edi), a0);
}
ABI_ENTRY_0048bab0(fn_0048bab0_Entry, fn_0048bab0_Abi)
HOOK(0x0048bab0, "fn_0048bab0", fn_0048bab0_Entry, nullptr);

// 0x0048bb20 (eax = string; stack: manager; ret 4): enters the string in the manager's string tree (keyed by its
// hash 0x0044ad70, child chosen by hash bit i at depth i) unless present: 0x00461ff0 (edx = hash, ebx = string,
// edi = manager+0x1c), +0x18 += length + 1. Returns the pointer past the string's terminator (0 for null).
uint32_t fn_0048bb20(uint32_t str, uint8_t* m)
{
    if (str == 0)
        return 0;
    const uint32_t h = Hash(str);
    const uint32_t len = static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(str))) + 1;
    const uint32_t* node = Field<uint32_t*>(m, 0x20);
    for (uint32_t i = 0; node != nullptr; ++i)
    {
        if (node[2] == h)
            return str + len;
        node = reinterpret_cast<const uint32_t*>(node[(h >> (i & 0x1f)) & 1]);
    }
    orig::fn_00461ff0(h, str, u(m + 0x1c));
    Field<uint32_t>(m, 0x18) += len;
    return str + len;
}
static uint32_t fn_0048bb20_Abi(uint32_t eax, uint32_t a0)
{
    return fn_0048bb20(eax, B(a0));
}
ABI_ENTRY_0048bb20(fn_0048bb20_Entry, fn_0048bb20_Abi)
HOOK(0x0048bb20, "fn_0048bb20", fn_0048bb20_Entry, nullptr);

// 0x0048bb90 (ecx = manager, esi = object): index + 1 of the object in the object list, 0 when absent or null.
uint32_t fn_0048bb90(uint8_t* m, uint32_t obj)
{
    if (obj == 0)
        return 0;
    const uint32_t n = Field<uint32_t>(m, 0);
    const uint32_t* a = Field<uint32_t*>(m, 4);
    for (uint32_t i = 0; i < n; ++i)
        if (a[i] == obj)
            return i + 1;
    return 0;
}
static uint32_t fn_0048bb90_Abi(uint32_t ecx, uint32_t esi)
{
    return fn_0048bb90(B(ecx), esi);
}
ABI_ENTRY_0048bb90(fn_0048bb90_Entry, fn_0048bb90_Abi)
HOOK(0x0048bb90, "fn_0048bb90", fn_0048bb90_Entry, nullptr);

// 0x0048bbc0 (eax = index + 1, ecx = manager): object list entry index - 1, 0 for 0.
uint32_t fn_0048bbc0(uint32_t idx, uint8_t* m)
{
    if (idx == 0)
        return 0;
    return Field<uint32_t*>(m, 4)[idx - 1];
}
static uint32_t fn_0048bbc0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0048bbc0(eax, B(ecx));
}
ABI_ENTRY_0048bbc0(fn_0048bbc0_Entry, fn_0048bbc0_Abi)
HOOK(0x0048bbc0, "fn_0048bbc0", fn_0048bbc0_Entry, nullptr);

// 0x0048bbd0 (ecx = manager, esi = item): index + 1 of the item in the +0x10 list (count +8), 0 when absent/null.
uint32_t fn_0048bbd0(uint8_t* m, uint32_t obj)
{
    if (obj == 0)
        return 0;
    const uint32_t n = Field<uint32_t>(m, 8);
    const uint32_t* a = Field<uint32_t*>(m, 0x10);
    for (uint32_t i = 0; i < n; ++i)
        if (a[i] == obj)
            return i + 1;
    return 0;
}
static uint32_t fn_0048bbd0_Abi(uint32_t ecx, uint32_t esi)
{
    return fn_0048bbd0(B(ecx), esi);
}
ABI_ENTRY_0048bbd0(fn_0048bbd0_Entry, fn_0048bbd0_Abi)
HOOK(0x0048bbd0, "fn_0048bbd0", fn_0048bbd0_Entry, nullptr);

// 0x0048bc00 (eax = index + 1, ecx = manager): +0x10 list entry index - 1, 0 for 0.
uint32_t fn_0048bc00(uint32_t idx, uint8_t* m)
{
    if (idx == 0)
        return 0;
    return Field<uint32_t*>(m, 0x10)[idx - 1];
}
static uint32_t fn_0048bc00_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0048bc00(eax, B(ecx));
}
ABI_ENTRY_0048bc00(fn_0048bc00_Entry, fn_0048bc00_Abi)
HOOK(0x0048bc00, "fn_0048bc00", fn_0048bc00_Entry, nullptr);

// 0x0048bc10 (eax = field, ecx = base pointer field; stack: manager, unused; ret 4): [field] - [base], or -1 when
// [field] is null (save type 11). Not executed in recorded runs.
uint32_t fn_0048bc10(uint32_t field, uint32_t base, uint32_t /*mgr*/)
{
    const uint32_t v = *D(field);
    if (v == 0)
        return 0xffffffffu;
    return v - *D(base);
}
static uint32_t fn_0048bc10_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_0048bc10(eax, ecx, a0);
}
ABI_ENTRY_0048bc10(fn_0048bc10_Entry, fn_0048bc10_Abi)
HOOK(0x0048bc10, "fn_0048bc10", fn_0048bc10_Entry, nullptr);

// 0x0048bc30 (ecx = offset; stack: manager (unused), base pointer field; ret 8): -1 -> 0, else [base] + offset
// (inverse of 0x0048bc10). Not executed in recorded runs.
uint32_t fn_0048bc30(uint32_t off, uint32_t /*mgr*/, uint32_t base)
{
    if (off == 0xffffffffu)
        return 0;
    return *D(base) + off;
}
static uint32_t fn_0048bc30_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0048bc30(ecx, a0, a1);
}
ABI_ENTRY_0048bc30(fn_0048bc30_Entry, fn_0048bc30_Abi)
HOOK(0x0048bc30, "fn_0048bc30", fn_0048bc30_Entry, nullptr);

// 0x0048bc50 (eax = string; stack: manager (unused); ret 4): hash of the string (0x0044ad70), 0 for null (save type
// 14). Not executed in recorded runs.
uint32_t fn_0048bc50(uint32_t str, uint32_t /*mgr*/)
{
    if (str == 0)
        return 0;
    return Hash(str);
}
static uint32_t fn_0048bc50_Abi(uint32_t eax, uint32_t a0)
{
    return fn_0048bc50(eax, a0);
}
ABI_ENTRY_0048bc50(fn_0048bc50_Entry, fn_0048bc50_Abi)
HOOK(0x0048bc50, "fn_0048bc50", fn_0048bc50_Entry, nullptr);

// 0x0048bc60 (edx = hash; stack: manager; ret 4): looks the hash up in the manager's string tree (0x00461fc0, eax =
// manager+0x1c, edx = hash); 0 for 0 (inverse of 0x0048bc50).
uint32_t fn_0048bc60(uint32_t hash, uint8_t* m)
{
    if (hash == 0)
        return 0;
    return orig::fn_00461fc0(u(m + 0x1c), hash);
}
static uint32_t fn_0048bc60_Abi(uint32_t edx, uint32_t a0)
{
    return fn_0048bc60(edx, B(a0));
}
ABI_ENTRY_0048bc60(fn_0048bc60_Entry, fn_0048bc60_Abi)
HOOK(0x0048bc60, "fn_0048bc60", fn_0048bc60_Entry, nullptr);

// 0x0048bc80 (ecx = manager; stack: class descriptor table, base; ret 8): collect pass over the described members
// (each entry, count elements at offset + i * stride): type 3 object pointer -> 0x0048ba60, 4 sub-table on the same
// base, 5 embedded struct (sub-table on the member), 8 container 0x0048e3f0 (ecx = entry+0x14, ebx = manager;
// entry+0x10), 12 0x0048bab0 (member; entry+0x14), 14 string 0x0048bb20, 17 member = 0x00455a00(edx = member).
// Returns 0 (original: EAX of the last entry check; the caller's EAX for an empty table).
uint32_t fn_0048bc80(uint8_t* m, uint32_t desc, uint32_t base)
{
    const uint32_t* e = reinterpret_cast<const uint32_t*>(desc);
    if (e == nullptr || e[0] == 0)
        return 0;
    do
    {
        uint32_t p = e[1] + base;
        for (uint32_t i = 0; i < e[3]; ++i, p += e[2])
        {
            switch (e[0])
            {
            case 3:
                fn_0048ba60(*D(p), m);
                break;
            case 4:
                fn_0048bc80(m, e[4], base);
                break;
            case 5:
                fn_0048bc80(m, e[4], p);
                break;
            case 8:
                fn_0048e3f0(p, e[5], m, e[4]);
                break;
            case 12:
                fn_0048bab0(p, m, e[5]);
                break;
            case 14:
                fn_0048bb20(*D(p), m);
                break;
            case 17:
                *D(p) = orig::fn_00455a00(*D(p));
                break;
            default:
                break;
            }
        }
        e += 6;
    } while (e[0] != 0);
    return 0;
}
static uint32_t fn_0048bc80_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0048bc80(B(ecx), a0, a1);
}
ABI_ENTRY_0048bc80(fn_0048bc80_Entry, fn_0048bc80_Abi)
HOOK(0x0048bc80, "fn_0048bc80", fn_0048bc80_Entry, nullptr);

// 0x0048bda0 (ecx = manager; stack: class descriptor table, base; ret 8): write pass (a leading type-1 entry is
// skipped). Per element: 2 raw bytes (stride), 3 object index (0x0048bb90), 4/5 sub-tables, 6 0x0045ac60 (edx =
// game+0xa84, esi = value), 7 self-relative pointer (0 stays 0), 8 container 0x0048e4b0, 9 +0x10 list index
// (0x0048bbd0), 10 scene pass index (0x004881f0, table game+0xa78), 11 0x0048bc10 (base pointer at base+entry+0x10),
// 13 0x0045acd0 (ecx = game+0xa84), 14 string hash (0x0048bc50), 15 FxBank_PointerToOffset (ecx = game+0xa90),
// 16 float [0x00604a98] - value. Each value is written as 4 bytes (0x0048c380). Callers include 0x00410380.
// Returns 0 (original: as 0x0048bc80). Not executed in recorded runs.
uint32_t fn_0048bda0(uint8_t* m, uint32_t desc, uint32_t base)
{
    const uint32_t* e = reinterpret_cast<const uint32_t*>(desc);
    if (e == nullptr)
        return 0;
    if (e[0] == 1)
        e += 6;
    if (e[0] == 0)
        return 0;
    uint32_t tmp = 0;
    float ftmp = 0.0f;
    do
    {
        uint32_t p = e[1] + base;
        for (uint32_t i = 0; i < e[3]; ++i, p += e[2])
        {
            bool write = true;
            switch (e[0])
            {
            case 2:
                fn_0048c380(e[2], m, p);
                write = false;
                break;
            case 3:
                tmp = fn_0048bb90(m, *D(p));
                break;
            case 4:
                fn_0048bda0(m, e[4], base);
                write = false;
                break;
            case 5:
                fn_0048bda0(m, e[4], p);
                write = false;
                break;
            case 6:
                tmp = orig::fn_0045ac60(Field<uint32_t>(Gm(), 0xa84), *D(p));
                break;
            case 7:
                tmp = *D(p) == 0 ? 0 : *D(p) - p;
                break;
            case 8:
                fn_0048e4b0(m, *D(p), e[4]);
                write = false;
                break;
            case 9:
                tmp = fn_0048bbd0(m, *D(p));
                break;
            case 10:
                tmp = fn_004881f0(*D(p), Field<uint8_t*>(Gm(), 0xa78));
                break;
            case 11:
                tmp = fn_0048bc10(p, e[4] + base, u(m));
                break;
            case 13:
                tmp = orig::fn_0045acd0(*D(p), Field<uint32_t>(Gm(), 0xa84));
                break;
            case 14:
                tmp = fn_0048bc50(*D(p), u(m));
                break;
            case 15:
                tmp = orig::fn_0041c780(*D(p), Field<uint32_t>(Gm(), 0xa90));
                break;
            case 16:
                ftmp = GameGlobal<float>(0x00604a98) - *reinterpret_cast<float*>(p);
                fn_0048c380(4, m, u(&ftmp));
                write = false;
                break;
            default:
                write = false;
                break;
            }
            if (write)
                fn_0048c380(4, m, u(&tmp));
        }
        e += 6;
    } while (e[0] != 0);
    return 0;
}
static uint32_t fn_0048bda0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0048bda0(B(ecx), a0, a1);
}
ABI_ENTRY_0048bda0(fn_0048bda0_Entry, fn_0048bda0_Abi)
HOOK(0x0048bda0, "fn_0048bda0", fn_0048bda0_Entry, nullptr);

// 0x0048c010 (ecx = manager; stack: class descriptor table, base; ret 8): post-load pass: 4/5 sub-tables, 8
// container 0x0048e460 (member; manager, entry+0x10, entry+0x14), 12 0x0048bab0 (member; entry+0x14), 17 member =
// 0x00455a00(edx = member). Returns 0 (original: as 0x0048bc80).
uint32_t fn_0048c010(uint8_t* m, uint32_t desc, uint32_t base)
{
    const uint32_t* e = reinterpret_cast<const uint32_t*>(desc);
    if (e == nullptr || e[0] == 0)
        return 0;
    do
    {
        uint32_t p = e[1] + base;
        for (uint32_t i = 0; i < e[3]; ++i, p += e[2])
        {
            switch (e[0])
            {
            case 4:
                fn_0048c010(m, e[4], base);
                break;
            case 5:
                fn_0048c010(m, e[4], p);
                break;
            case 8:
                fn_0048e460(p, u(m), e[4], e[5]);
                break;
            case 12:
                fn_0048bab0(p, m, e[5]);
                break;
            case 17:
                *D(p) = orig::fn_00455a00(*D(p));
                break;
            default:
                break;
            }
        }
        e += 6;
    } while (e[0] != 0);
    return 0;
}
static uint32_t fn_0048c010_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0048c010(B(ecx), a0, a1);
}
ABI_ENTRY_0048c010(fn_0048c010_Entry, fn_0048c010_Abi)
HOOK(0x0048c010, "fn_0048c010", fn_0048c010_Entry, nullptr);

// 0x0048c0f0 (ecx = manager; stack: class descriptor table, base; ret 8): read pass, inverse of 0x0048bda0 (a
// leading type-1 entry skipped): 2 raw bytes, 3 object (0x0048bbc0), 4/5 sub-tables, 6 0x0045ac90 (ecx =
// game+0xa84), 7 self-relative pointer, 8 container 0x0048e630, 9 +0x10 list (0x0048bc00), 10 scene pass entry
// (0x00488220, ecx = game+0xa78), 11 0x0048bc30, 13 0x0045acb0 (stack game+0xa84), 14 string (0x0048bc60), 15
// FxBank_OffsetToPointer (stack game+0xa90), 16 float [0x00604a98] - value, 17 member = -1. Callers include
// 0x00410360. Returns 0 (original: as 0x0048bc80).
uint32_t fn_0048c0f0(uint8_t* m, uint32_t desc, uint32_t base)
{
    const uint32_t* e = reinterpret_cast<const uint32_t*>(desc);
    if (e == nullptr)
        return 0;
    if (e[0] == 1)
        e += 6;
    if (e[0] == 0)
        return 0;
    uint32_t tmp = 0;
    float ftmp = 0.0f;
    do
    {
        uint32_t p = e[1] + base;
        for (uint32_t i = 0; i < e[3]; ++i, p += e[2])
        {
            switch (e[0])
            {
            case 2:
                fn_0048c3a0(e[2], m, p);
                break;
            case 3:
                fn_0048c3a0(4, m, u(&tmp));
                *D(p) = fn_0048bbc0(tmp, m);
                break;
            case 4:
                fn_0048c0f0(m, e[4], base);
                break;
            case 5:
                fn_0048c0f0(m, e[4], p);
                break;
            case 6:
                fn_0048c3a0(4, m, u(&tmp));
                *D(p) = orig::fn_0045ac90(tmp, Field<uint32_t>(Gm(), 0xa84));
                break;
            case 7:
                fn_0048c3a0(4, m, p);
                if (*D(p) != 0)
                    *D(p) += p;
                break;
            case 8:
                fn_0048e630(p, m, e[4], e[5]);
                break;
            case 9:
                fn_0048c3a0(4, m, u(&tmp));
                *D(p) = fn_0048bc00(tmp, m);
                break;
            case 10:
                fn_0048c3a0(4, m, u(&tmp));
                *D(p) = fn_00488220(tmp, Field<uint8_t*>(Gm(), 0xa78));
                break;
            case 11:
                fn_0048c3a0(4, m, u(&tmp));
                *D(p) = fn_0048bc30(tmp, u(m), base + e[4]);
                break;
            case 13:
                fn_0048c3a0(4, m, u(&tmp));
                *D(p) = orig::fn_0045acb0(tmp, Field<uint32_t>(Gm(), 0xa84));
                break;
            case 14:
                fn_0048c3a0(4, m, u(&tmp));
                *D(p) = fn_0048bc60(tmp, m);
                break;
            case 15:
                fn_0048c3a0(4, m, u(&tmp));
                *D(p) = orig::fn_0041c790(tmp, Field<uint32_t>(Gm(), 0xa90));
                break;
            case 16:
                fn_0048c3a0(4, m, u(&ftmp));
                *reinterpret_cast<float*>(p) = GameGlobal<float>(0x00604a98) - ftmp;
                break;
            case 17:
                *D(p) = 0xffffffffu;
                break;
            default:
                break;
            }
        }
        e += 6;
    } while (e[0] != 0);
    return 0;
}
static uint32_t fn_0048c0f0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0048c0f0(B(ecx), a0, a1);
}
ABI_ENTRY_0048c0f0(fn_0048c0f0_Entry, fn_0048c0f0_Abi)
HOOK(0x0048c0f0, "fn_0048c0f0", fn_0048c0f0_Entry, nullptr);

// 0x0048c380 (ecx = size, edx = manager; stack: source; ret 4): writes size bytes to the manager's file
// (0x0048ac90: ecx = size; file, source). AL = 1 for size 0. Not executed in recorded runs.
uint32_t fn_0048c380(uint32_t size, uint8_t* m, uint32_t src)
{
    if (size == 0)
        return 1;
    return orig::fn_0048ac90(size, Field<uint32_t>(m, 0x14), src);
}
static uint32_t fn_0048c380_Abi(uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return fn_0048c380(ecx, B(edx), a0);
}
ABI_ENTRY_0048c380(fn_0048c380_Entry, fn_0048c380_Abi)
HOOK(0x0048c380, "fn_0048c380", fn_0048c380_Entry, nullptr);

// 0x0048c3a0 (eax = size, ecx = manager; stack: destination; ret 4): reads size bytes from the manager's file
// (0x0048af10: ecx = file; destination, size). AL = 1 for size 0.
uint32_t fn_0048c3a0(uint32_t size, uint8_t* m, uint32_t dst)
{
    if (size == 0)
        return 1;
    return orig::fn_0048af10(Field<uint32_t>(m, 0x14), dst, size);
}
static uint32_t fn_0048c3a0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_0048c3a0(eax, B(ecx), a0);
}
ABI_ENTRY_0048c3a0(fn_0048c3a0_Entry, fn_0048c3a0_Abi)
HOOK(0x0048c3a0, "fn_0048c3a0", fn_0048c3a0_Entry, nullptr);

// 0x0048c3c0 (ebx = manager; ecx only a `push ecx` local): version hash of the save format: +0x1028 = 0x124, then
// for every class descriptor table in the null-terminated list 0x005cb1b0 and every entry: shift += 3,
// +0x1028 ^= rotr(hash(entry name +0x14), shift & 31). Returns the address of the list's terminating slot.
uint32_t fn_0048c3c0(uint8_t* m)
{
    Field<uint32_t>(m, 0x1028) = 0x124;
    uint32_t* lp = &GameGlobal<uint32_t>(0x005cb1b0);
    const uint32_t* e = reinterpret_cast<const uint32_t*>(*lp);
    uint32_t d = 0;
    while (e != nullptr)
    {
        if (e[0] != 0)
        {
            do
            {
                d += 3;
                const uint32_t h = Hash(e[5]);
                Field<uint32_t>(m, 0x1028) ^= RotR(h, d);
                e += 6;
            } while (e[0] != 0);
        }
        ++lp;
        e = reinterpret_cast<const uint32_t*>(*lp);
    }
    return u(lp);
}
static uint32_t fn_0048c3c0_Abi(uint32_t /*ecx*/, uint32_t ebx)
{
    return fn_0048c3c0(B(ebx));
}
ABI_ENTRY_0048c3c0(fn_0048c3c0_Entry, fn_0048c3c0_Abi)
HOOK(0x0048c3c0, "fn_0048c3c0", fn_0048c3c0_Entry, nullptr);

// 0x0048c440 (cdecl; stack: old array, new count, old count): pointer-array realloc: allocates new count dwords
// (tag 0x1a, "Realloc"; zeroed without a null check, original), copies min(old, new) dwords and frees the old
// array. Returns the new array (0 for a new count of 0).
uint32_t fn_0048c440(uint32_t old, uint32_t newCount, uint32_t oldCount)
{
    uint32_t buf = 0;
    if (newCount != 0)
    {
        buf = HeapAlloc_(0x1a, newCount * 4, 2, 0x0059d958);
        std::memset(B(buf), 0, newCount * 4);
    }
    if (old != 0)
    {
        if (buf != 0)
        {
            const uint32_t n = oldCount < newCount ? oldCount : newCount;
            std::memcpy(B(buf), B(old), n * 4);
        }
        Free(old);
    }
    return buf;
}
static uint32_t fn_0048c440_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0048c440(a0, a1, a2);
}
ABI_ENTRY_0048c440(fn_0048c440_Entry, fn_0048c440_Abi)
HOOK(0x0048c440, "fn_0048c440", fn_0048c440_Entry, nullptr);

// 0x0048c4c0 (eax = string tree {+0 free list, +4 root}, edx = hash): the node with that hash (child by hash bit i
// at depth i) or 0. Caller 0x0050ca70. Not executed in recorded runs.
uint32_t fn_0048c4c0(uint8_t* tree, uint32_t hash)
{
    const uint32_t* node = Field<uint32_t*>(tree, 4);
    for (uint32_t i = 0; node != nullptr; ++i)
    {
        if (node[2] == hash)
            break;
        node = reinterpret_cast<const uint32_t*>(node[(hash >> (i & 0x1f)) & 1]);
    }
    return u(node);
}
static uint32_t fn_0048c4c0_Abi(uint32_t eax, uint32_t edx)
{
    return fn_0048c4c0(B(eax), edx);
}
ABI_ENTRY_0048c4c0(fn_0048c4c0_Entry, fn_0048c4c0_Abi)
HOOK(0x0048c4c0, "fn_0048c4c0", fn_0048c4c0_Entry, nullptr);

// 0x0048c4f0 (ecx = tree node; stack: cdecl callback(node, context), context; ret 8): pre-order walk: callback on
// the node, recurse into child 0, continue with child 1. Returns the last call's EAX. Not executed in recorded runs.
uint32_t fn_0048c4f0(uint32_t node, uint32_t cb, uint32_t ctx)
{
    uint32_t r = 0;
    const uint32_t* n = reinterpret_cast<const uint32_t*>(node);
    do
    {
        r = reinterpret_cast<Cb2>(cb)(u(n), ctx);
        if (n[0] != 0)
            r = fn_0048c4f0(n[0], cb, ctx);
        n = reinterpret_cast<const uint32_t*>(n[1]);
    } while (n != nullptr);
    return r;
}
static uint32_t fn_0048c4f0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0048c4f0(ecx, a0, a1);
}
ABI_ENTRY_0048c4f0(fn_0048c4f0_Entry, fn_0048c4f0_Abi)
HOOK(0x0048c4f0, "fn_0048c4f0", fn_0048c4f0_Entry, nullptr);

// 0x0048c530 (stack: 1 dword, not read; ret 4): after loading, for every entry of the list game+0xa84 -> +0xef0
// (iterator 0x004e7340 / 0x004e73c0) creates "fx_siren" (Fx_Create 0x0041cc60: AL = 1 on the item's upper bytes,
// ecx = game+0xa90; name, 0x80-byte description {+0 = 4, +0x18 = 0x005bba94, +0x1c = item}); releases the iterator's
// reference (+0x18 -= 1). Returns that reference. Caller 0x0048c9b0. Deviation: the rest of the description is
// uninitialised stack in the original, zero here.
uint32_t fn_0048c530(uint32_t /*a0*/)
{
    uint32_t iter[2] = {0, 0};
    uint32_t desc[0x20] = {};
    uint32_t item = orig::fn_004e7340(Field<uint32_t>(Field<uint8_t*>(Gm(), 0xa84), 0xef0), u(iter));
    while (item != 0)
    {
        desc[7] = item;
        desc[6] = 0x005bba94;
        desc[0] = 4;
        orig::fn_0041cc60(Lo8(item, 1), Field<uint32_t>(Gm(), 0xa90), 0x005b2310, u(desc));
        item = orig::fn_004e73c0(u(iter));
    }
    const uint32_t held = iter[1];
    if (held != 0)
        Field<uint32_t>(B(held), 0x18) -= 1;
    return held;
}
static uint32_t fn_0048c530_Abi(uint32_t a0)
{
    return fn_0048c530(a0);
}
ABI_ENTRY_0048c530(fn_0048c530_Entry, fn_0048c530_Abi)
HOOK(0x0048c530, "fn_0048c530", fn_0048c530_Entry, nullptr);

// 0x0048c5c0 (stack: 1 dword, not read; ret 4): calls vt+0x24 on every entry of the list game+0xa84 -> +0xeb8
// (iterator 0x004e7340 / 0x004e73c0) and releases the iterator's reference. Returns it. Caller 0x0048c9b0.
uint32_t fn_0048c5c0(uint32_t /*a0*/)
{
    uint32_t iter[2] = {0, 0};
    uint32_t item = orig::fn_004e7340(Field<uint32_t>(Field<uint8_t*>(Gm(), 0xa84), 0xeb8), u(iter));
    while (item != 0)
    {
        VCall<uint32_t>(reinterpret_cast<void*>(item), 0x24 / 4);
        item = orig::fn_004e73c0(u(iter));
    }
    const uint32_t held = iter[1];
    if (held != 0)
        Field<uint32_t>(B(held), 0x18) -= 1;
    return held;
}
static uint32_t fn_0048c5c0_Abi(uint32_t a0)
{
    return fn_0048c5c0(a0);
}
ABI_ENTRY_0048c5c0(fn_0048c5c0_Entry, fn_0048c5c0_Abi)
HOOK(0x0048c5c0, "fn_0048c5c0", fn_0048c5c0_Entry, nullptr);

// PART3
