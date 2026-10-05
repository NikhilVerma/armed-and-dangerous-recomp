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

// ================================================================================================================
// 0x0048c620 - 0x0048c9b0: save / load a slot
// ================================================================================================================
// Savegame header (0x124 bytes, written first): +0 level name (0x0044a660 of game+0xa64), +0x104 [game+0xa64]+8,
// +0x108 game+0x550, +0x10c save format hash (manager+0x1028), +0x110 0x0044a9d0(header, game+0xa64), +0x114
// FILETIME of the save (GetSystemTime + SystemTimeToFileTime), +0x11c thumbnail width 0x80, +0x120 height 0x4c.
// The thumbnail (width * height * 4 bytes) follows, then the object data (0x0048b780).

uint32_t fn_004a3930(uint32_t eax);

// 0x0048c620 (eax = wide slot name, esi = manager): saves the game into the slot: narrows the name (unused copy,
// at most 0xff chars), opens savegame.dat for writing (0x0048b6d0 mode 1), writes the header and a 0x80 x 0x4c
// thumbnail (SaveGame_CreateThumbnail into a "thumbnail temp" buffer), 0x00406690 (eax = game+0xa6c, ecx = game),
// audio (game+0x2c) vt+0x114(0x00487d80), lists the subsystems game+0xa84, +0xa78, +0xa94, +0xa90, +0xa68, +0xa7c,
// +0xa9c (0x0048ba60), writes the objects (0x0048b780), audio vt+0x114(0x00487d90), closes (0x0048b720 BL = 1;
// ecx scratch 0) and on success counts game+0x56c. Caller 0x004a3a80. Not executed in recorded runs.
uint32_t SaveGame_Write(uint32_t wname, uint8_t* m)
{
    uint8_t f[0x240] = {};  // original frame: +8 SYSTEMTIME, +0x18 header, +0x140 narrowed name
    const wchar_t* w = reinterpret_cast<const wchar_t*>(wname);
    const uint32_t len = GameFunction<uint32_t(__cdecl*)(const wchar_t*)>(0x00538ea0)(w);
    uint32_t left = 0x100;
    uint32_t i = 0;
    if (len != 0)
    {
        for (;;)
        {
            if (--left == 0)
                break;
            f[0x140 + i] = static_cast<uint8_t>(w[i]);
            ++i;
            if (i >= len)
                break;
        }
    }
    f[0x140 + i] = 0;
    const uint32_t r = fn_0048b6d0(m, 1, wname);
    if (static_cast<uint8_t>(r) == 0)
        return r;
    uint8_t* h = f + 0x18;
    std::memset(h, 0, 0x49 * 4);
    const char* name = reinterpret_cast<const char*>(orig::fn_0044a660(Field<uint32_t>(Gm(), 0xa64), 0));
    std::strcpy(reinterpret_cast<char*>(h), name);
    Field<uint32_t>(h, 0x104) = Field<uint32_t>(Field<uint8_t*>(Gm(), 0xa64), 8);
    Field<uint32_t>(h, 0x108) = Field<uint32_t>(Gm(), 0x550);
    Field<uint32_t>(h, 0x10c) = Field<uint32_t>(m, 0x1028);
    Field<uint32_t>(h, 0x110) = orig::fn_0044a9d0(u(h), Field<uint32_t>(Gm(), 0xa64));
    Imp<decltype(&GetSystemTime)>(0x0056b1dc)(reinterpret_cast<LPSYSTEMTIME>(f + 8));
    Imp<decltype(&SystemTimeToFileTime)>(0x0056b110)(reinterpret_cast<const SYSTEMTIME*>(f + 8),
                                                     reinterpret_cast<LPFILETIME>(h + 0x114));
    Field<uint32_t>(h, 0x11c) = 0x80;
    Field<uint32_t>(h, 0x120) = 0x4c;
    fn_0048c380(0x124, m, u(h));
    const uint32_t buf = HeapAlloc_(0x1a, Field<uint32_t>(h, 0x120) * Field<uint32_t>(h, 0x11c) * 4, 2, 0x0059d948);
    SaveGame_CreateThumbnail(Field<uint32_t>(h, 0x120), Field<uint32_t>(h, 0x11c), buf);
    fn_0048c380(Field<uint32_t>(h, 0x11c) * Field<uint32_t>(h, 0x120) * 4, m, buf);
    Free(buf);
    orig::fn_00406690(Field<uint32_t>(Gm(), 0xa6c), u(Gm()));
    VCall<uint32_t>(Field<void*>(Gm(), 0x2c), 0x114 / 4, 0x00487d80u);
    fn_0048ba60(Field<uint32_t>(Gm(), 0xa84), m);
    fn_0048ba60(Field<uint32_t>(Gm(), 0xa78), m);
    fn_0048ba60(Field<uint32_t>(Gm(), 0xa94), m);
    fn_0048ba60(Field<uint32_t>(Gm(), 0xa90), m);
    fn_0048ba60(Field<uint32_t>(Gm(), 0xa68), m);
    fn_0048ba60(Field<uint32_t>(Gm(), 0xa7c), m);
    fn_0048ba60(Field<uint32_t>(Gm(), 0xa9c), m);
    fn_0048b780(m);
    VCall<uint32_t>(Field<void*>(Gm(), 0x2c), 0x114 / 4, 0x00487d90u);
    const uint32_t c = fn_0048b720(m, 0, 1);
    if (static_cast<uint8_t>(c) != 0)
        Field<uint32_t>(Gm(), 0x56c) += 1;
    return c;
}
static uint32_t SaveGame_Write_Abi(uint32_t eax, uint32_t esi)
{
    return SaveGame_Write(eax, B(esi));
}
ABI_ENTRY_0048c620(SaveGame_Write_Entry, SaveGame_Write_Abi)
HOOK(0x0048c620, "SaveGame_Write", SaveGame_Write_Entry, nullptr);

// 0x0048c870 (eax = header buffer (0x124); stack: manager, wide slot name, texture record or 0, flag; ret 0x10):
// reads a slot's header: releases the texture first (renderer vt+0x128(texture, 1)), opens savegame.dat for reading
// (0x0048b6d0 mode 0), reads the header; when its format hash matches manager+0x1028 and 0x00418790 (ebx =
// header+0x110, esi = header, edi = game+0xa64; flag) accepts it, loads the thumbnail into the texture
// (Texture_LoadRgbPixels 0x0044f1f0: eax = pixels, ebx = texture; width, height, 1) through a "thumbnail temp"
// buffer. Closes (0x0048b720, BL = result; ecx scratch 0). Callers 0x0048c990, 0x004a3020.
uint32_t SaveGame_ReadHeader(uint8_t* h, uint8_t* m, uint32_t wname, uint32_t texture, uint32_t flag)
{
    if (texture != 0)
        VCall<uint32_t>(Field<void*>(Gm(), 0x38), 0x128 / 4, texture, 1u);
    const uint32_t r = fn_0048b6d0(m, 0, wname);
    if (static_cast<uint8_t>(r) == 0)
        return r;
    uint8_t ok = static_cast<uint8_t>(fn_0048c3a0(0x124, m, u(h)));
    if (ok != 0)
    {
        if (Field<uint32_t>(h, 0x10c) != Field<uint32_t>(m, 0x1028) ||
            static_cast<uint8_t>(orig::fn_00418790(u(h + 0x110), u(h), Field<uint32_t>(Gm(), 0xa64), flag)) == 0)
        {
            ok = 0;
        }
        else if (texture != 0)
        {
            const uint32_t buf =
                HeapAlloc_(0x1a, Field<uint32_t>(h, 0x11c) * Field<uint32_t>(h, 0x120) * 4, 2, 0x0059d948);
            fn_0048c3a0(Field<uint32_t>(h, 0x11c) * Field<uint32_t>(h, 0x120) * 4, m, buf);
            orig::fn_0044f1f0(buf, texture, Field<uint32_t>(h, 0x11c), Field<uint32_t>(h, 0x120), 1);
            Free(buf);
        }
    }
    return fn_0048b720(m, 0, ok);
}
static uint32_t SaveGame_ReadHeader_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return SaveGame_ReadHeader(B(eax), B(a0), a1, a2, a3);
}
ABI_ENTRY_0048c870(SaveGame_ReadHeader_Entry, SaveGame_ReadHeader_Abi)
HOOK(0x0048c870, "SaveGame_ReadHeader", SaveGame_ReadHeader_Entry, nullptr);

// 0x0048c990 (eax = wide slot name, ecx = manager): validates a slot: 0x0048c870 into a local header (no texture,
// flag 1). Callers 0x00492fd0, 0x004a2e90.
uint32_t SaveGame_CheckSlot(uint32_t wname, uint8_t* m)
{
    uint8_t h[0x124] = {};
    return SaveGame_ReadHeader(h, m, wname, 0, 1);
}
static uint32_t SaveGame_CheckSlot_Abi(uint32_t eax, uint32_t ecx)
{
    return SaveGame_CheckSlot(eax, B(ecx));
}
ABI_ENTRY_0048c990(SaveGame_CheckSlot_Entry, SaveGame_CheckSlot_Abi)
HOOK(0x0048c990, "SaveGame_CheckSlot", SaveGame_CheckSlot_Entry, nullptr);

// 0x0048c9b0 (ecx = manager; stack: wide slot name; ret 4): loads the slot: opens it (0x0048b6d0 mode 0), reads
// and checks the header (format hash, 0x00418790 with flag 1), 0x00401f60 (eax = header+0x108; game), skips the
// thumbnail (0x0048c3a0 into null), sets byte +0x60 of the physics manager (game+0xa8c), loads the level
// (Game_LoadLevelByName 0x00403640: ecx = game; header, header+0x104, slot name), reads the objects (0x0048b780);
// on success 0x0048c5c0, 0x0048c530, PhysicsManager_Step (ecx = 1, edi = physics; 0), 0x004a3930 (eax = 0) and
// 0x00401f40(game, 0). Closes (0x0048b720, BL = result) and on success counts game+0x56c. Caller 0x00403690.
uint32_t SaveGame_Load(uint8_t* m, uint32_t wname)
{
    uint8_t f[0x230] = {};  // original frame: +0xc header, +0x130 narrowed name
    const wchar_t* w = reinterpret_cast<const wchar_t*>(wname);
    const uint32_t len = GameFunction<uint32_t(__cdecl*)(const wchar_t*)>(0x00538ea0)(w);
    uint32_t left = 0x100;
    uint32_t i = 0;
    if (len != 0)
    {
        for (;;)
        {
            if (--left == 0)
                break;
            f[0x130 + i] = static_cast<uint8_t>(w[i]);
            ++i;
            if (i >= len)
                break;
        }
    }
    f[0x130 + i] = 0;
    const uint32_t r = fn_0048b6d0(m, 0, wname);
    if (static_cast<uint8_t>(r) == 0)
        return r;
    uint8_t* h = f + 0xc;
    uint8_t ok = static_cast<uint8_t>(fn_0048c3a0(0x124, m, u(h)));
    if (ok != 0)
    {
        if (Field<uint32_t>(h, 0x10c) != Field<uint32_t>(m, 0x1028) ||
            static_cast<uint8_t>(orig::fn_00418790(u(h + 0x110), u(h), Field<uint32_t>(Gm(), 0xa64), 1)) == 0)
        {
            ok = 0;
        }
        else
        {
            orig::fn_00401f60(Field<uint32_t>(h, 0x108), u(Gm()));
            fn_0048c3a0(Field<uint32_t>(h, 0x120) * Field<uint32_t>(h, 0x11c) * 4, m, 0);
            uint8_t* phys = Field<uint8_t*>(Gm(), 0xa8c);
            if (phys != nullptr)
                phys[0x60] = 1;
            orig::fn_00403640(u(Gm()), u(h), Field<uint32_t>(h, 0x104), wname);
            ok = static_cast<uint8_t>(fn_0048b780(m));
            if (ok != 0)
            {
                fn_0048c5c0(u(m));
                fn_0048c530(u(m));
                const uint32_t pm = Field<uint32_t>(Gm(), 0xa8c);
                if (pm != 0)
                    orig::fn_00411d40(1, pm, 0);
                fn_004a3930(0);
                orig::fn_00401f40(u(Gm()), 0);
            }
        }
    }
    const uint32_t c = fn_0048b720(m, 0, ok);
    if (static_cast<uint8_t>(c) != 0)
        Field<uint32_t>(Gm(), 0x56c) += 1;
    return c;
}
static uint32_t SaveGame_Load_Abi(uint32_t ecx, uint32_t a0)
{
    return SaveGame_Load(B(ecx), a0);
}
ABI_ENTRY_0048c9b0(SaveGame_Load_Entry, SaveGame_Load_Abi)
HOOK(0x0048c9b0, "SaveGame_Load", SaveGame_Load_Entry, nullptr);

// ================================================================================================================
// 0x0048cb30 - : recreating objects on load, per-class serialise methods
// ================================================================================================================

// 0x0048cb30 (eax = saved type id; stack: manager; ret 4): recreates one saved object and lists it (0x0048ba60).
// id >= 0: template = 0x0045ac10(id & 0x1ff, object system game+0xa84); when (id >> 18) & 0x3ff is set the level
// table entry is spawned (0x0045d7a0(system, that, 0)); otherwise the template is created at a random point of the
// level bounds (0x00449450, eax = box, ecx = game+0xa60; x, y in [10%, 90%], z in [90%, 95%] of the box via _rand)
// with an identity matrix (flags 0x10) by ObjectSystem_CreateObject (eax = ecx = 0; system, matrix, 0x3ff,
// template, 0, 0, 0, (id >> 9) & 0x1ff, 1). id < 0: singletons by id - 0x80000001: 0 game+0xa68, 1 +0xa90,
// 2 +0xa94, 3 Streamers_Spawn (edi = +0xa94; 0), 4 +0xa84, 5 0x0041eb30(+0xa90), 6 FxInstance_Alloc (eax = +0xa90),
// 7 +0xa7c, 8 Decals_TakeFree (eax = +0xa7c), 9 +0xa9c, 10 +0xa78; others 0. Returns the object (0 = failure).
uint32_t fn_0048cb30(uint32_t id, uint8_t* m)
{
    uint8_t f[0x80] = {};  // original frame from its lowest local: +0x10 rand, +0x14 x, +0x18 y, +0x20 box,
                           // +0x38 matrix (17 dwords)
    uint32_t obj = 0;
    if (static_cast<int32_t>(id) >= 0)
    {
        uint8_t* g = Gm();
        const uint32_t sys = Field<uint32_t>(g, 0xa84);
        const uint32_t entry = (id >> 18) & 0x3ff;
        const uint32_t tmpl = orig::fn_0045ac10(id & 0x1ff, sys);
        if (entry != 0)
        {
            obj = orig::fn_0045d7a0(0, sys, entry, 0);
        }
        else
        {
            orig::fn_00449450(u(f + 0x20), Field<uint32_t>(g, 0xa60));
            const float* box = reinterpret_cast<const float*>(f + 0x20);
            const float k1 = GameGlobal<float>(0x005bcecc);
            const float k2 = GameGlobal<float>(0x005bcec8);
            Field<int32_t>(f, 0x10) = static_cast<int32_t>(orig::fn_005391cd());
            float t = static_cast<float>(Field<int32_t>(f, 0x10)) * k1;
            t = t + GameGlobal<float>(0x005bcbfc);
            float d = box[3] - box[0];
            Field<float>(f, 0x14) = t * d + box[0];
            Field<int32_t>(f, 0x10) = static_cast<int32_t>(orig::fn_005391cd());
            t = static_cast<float>(Field<int32_t>(f, 0x10)) * k1;
            t = t + GameGlobal<float>(0x005bcbfc);
            d = box[4] - box[1];
            Field<float>(f, 0x18) = t * d + box[1];
            Field<int32_t>(f, 0x10) = static_cast<int32_t>(orig::fn_005391cd());
            const uint32_t variant = (id >> 9) & 0x1ff;
            t = static_cast<float>(Field<int32_t>(f, 0x10)) * k2;
            t = t + GameGlobal<float>(0x005bcc38);
            d = box[5] - box[2];
            uint32_t* mtx = reinterpret_cast<uint32_t*>(f + 0x38);
            std::memset(mtx, 0, 0x11 * 4);
            Field<float>(f, 0x64) = t * d + box[2];
            mtx[3] = Field<uint32_t>(f, 0x14);
            mtx[7] = Field<uint32_t>(f, 0x18);
            mtx[0] = 0x3f800000;
            mtx[5] = 0x3f800000;
            mtx[10] = 0x3f800000;
            mtx[15] = 0x3f800000;
            mtx[16] = 0x10;
            obj = orig::fn_0045f020(0, 0, Field<uint32_t>(Gm(), 0xa84), u(mtx), 0x3ff, tmpl, 0, 0, 0, variant, 1);
        }
    }
    else
    {
        uint8_t* g = Gm();
        switch (id + 0x7fffffffu)
        {
        case 0:
            obj = Field<uint32_t>(g, 0xa68);
            break;
        case 1:
            obj = Field<uint32_t>(g, 0xa90);
            break;
        case 2:
            obj = Field<uint32_t>(g, 0xa94);
            break;
        case 3:
            obj = orig::fn_00494ee0(Field<uint32_t>(g, 0xa94), 0);
            break;
        case 4:
            obj = Field<uint32_t>(g, 0xa84);
            break;
        case 5:
            obj = orig::fn_0041eb30(Field<uint32_t>(g, 0xa90));
            break;
        case 6:
            obj = orig::fn_0041cc20(Field<uint32_t>(g, 0xa90));
            break;
        case 7:
            obj = Field<uint32_t>(g, 0xa7c);
            break;
        case 8:
            obj = orig::fn_004165a0(Field<uint32_t>(g, 0xa7c));
            break;
        case 9:
            obj = Field<uint32_t>(g, 0xa9c);
            break;
        case 10:
            obj = Field<uint32_t>(g, 0xa78);
            break;
        default:
            return 0;
        }
    }
    if (obj != 0)
        fn_0048ba60(obj, m);
    return obj;
}
static uint32_t fn_0048cb30_Abi(uint32_t eax, uint32_t a0)
{
    return fn_0048cb30(eax, B(a0));
}
ABI_ENTRY_0048cb30(fn_0048cb30_Entry, fn_0048cb30_Abi)
HOOK(0x0048cb30, "fn_0048cb30", fn_0048cb30_Entry, nullptr);

// 0x0048cdb0 (eax = body or 0, esi = 0x90-byte state): captures a physics body's state, the inverse of
// Body_RestoreState 0x0048cec0: +0 flags (1 = body +0xc bit 0, 2 = entity (+8) vt+0x18 == 4, 4 = +0x44 set), +4
// float +0x3c, +8 float +0x38, +0xc +0x20, then entity +0x20 (4 dwords), +0xb0 (4), +0x30 (4); unless flag 1 also
// entity vt+0x24(state+0x40), entity +0x40 (4) at +0x70 and +0x50 (4) at +0x80. Returns the size (0x40 / 0x90; 0
// for no body). Caller 0x0048cf70. Not executed in recorded runs.
uint32_t Body_CaptureState(uint8_t* body, uint8_t* s)
{
    if (body == nullptr)
        return 0;
    Field<uint32_t>(s, 0) = 0;
    if ((Field<uint8_t>(body, 0xc) & 1) != 0)
        Field<uint32_t>(s, 0) = 1;
    if (VCall<uint32_t>(Field<void*>(body, 8), 0x18 / 4) == 4)
        Field<uint32_t>(s, 0) |= 2;
    if (Field<uint32_t>(body, 0x44) != 0)
        Field<uint32_t>(s, 0) |= 4;
    Field<float>(s, 4) = Field<float>(body, 0x3c);
    Field<float>(s, 8) = Field<float>(body, 0x38);
    Field<uint32_t>(s, 0xc) = Field<uint32_t>(body, 0x20);
    uint8_t* e = Field<uint8_t*>(body, 8);
    for (uint32_t i = 0; i < 4; ++i)
        Field<uint32_t>(s, 0x10 + i * 4) = Field<uint32_t>(e, 0x20 + i * 4);
    e = Field<uint8_t*>(body, 8);
    for (uint32_t i = 0; i < 4; ++i)
        Field<uint32_t>(s, 0x20 + i * 4) = Field<uint32_t>(e, 0xb0 + i * 4);
    e = Field<uint8_t*>(body, 8);
    for (uint32_t i = 0; i < 4; ++i)
        Field<uint32_t>(s, 0x30 + i * 4) = Field<uint32_t>(e, 0x30 + i * 4);
    if ((Field<uint8_t>(s, 0) & 1) != 0)
        return 0x40;
    VCall<uint32_t>(Field<void*>(body, 8), 0x24 / 4, u(s + 0x40));
    e = Field<uint8_t*>(body, 8);
    for (uint32_t i = 0; i < 4; ++i)
        Field<uint32_t>(s, 0x70 + i * 4) = Field<uint32_t>(e, 0x40 + i * 4);
    e = Field<uint8_t*>(body, 8);
    for (uint32_t i = 0; i < 4; ++i)
        Field<uint32_t>(s, 0x80 + i * 4) = Field<uint32_t>(e, 0x50 + i * 4);
    return 0x90;
}
static uint32_t Body_CaptureState_Abi(uint32_t eax, uint32_t esi)
{
    return Body_CaptureState(B(eax), B(esi));
}
ABI_ENTRY_0048cdb0(Body_CaptureState_Entry, Body_CaptureState_Abi)
HOOK(0x0048cdb0, "Body_CaptureState", Body_CaptureState_Entry, nullptr);

// 0x0048cf70 (eax = body or 0, edi = manager): writes a body's state: size (Body_CaptureState into a 16-aligned
// 0x90-byte local), then the state when the size is nonzero. Callers 0x0048d010, 0x0048d1c0. Not executed in
// recorded runs.
uint32_t fn_0048cf70(uint8_t* body, uint8_t* m)
{
    alignas(16) uint8_t state[0x90] = {};
    uint32_t size = Body_CaptureState(body, state);
    uint32_t r = fn_0048c380(4, m, u(&size));
    if (size != 0)
        r = fn_0048c380(size, m, u(state));
    return r;
}
static uint32_t fn_0048cf70_Abi(uint32_t eax, uint32_t edi)
{
    return fn_0048cf70(B(eax), B(edi));
}
ABI_ENTRY_0048cf70(fn_0048cf70_Entry, fn_0048cf70_Abi)
HOOK(0x0048cf70, "fn_0048cf70", fn_0048cf70_Entry, nullptr);

// 0x0048cfc0 (eax = manager; stack: body; caller pops): reads a body's state: size, then the state and
// Body_RestoreState (eax = size, ecx = body, esi = state) when nonzero. Callers 0x0048d090, 0x0048d280.
uint32_t fn_0048cfc0(uint8_t* m, uint32_t body)
{
    alignas(16) uint8_t state[0x90] = {};
    uint32_t size = 0;
    uint32_t r = fn_0048c3a0(4, m, u(&size));
    if (size != 0)
    {
        fn_0048c3a0(size, m, u(state));
        r = orig::fn_0048cec0(size, body, u(state));
    }
    return r;
}
static uint32_t fn_0048cfc0_Abi(uint32_t eax, uint32_t a0)
{
    return fn_0048cfc0(B(eax), a0);
}
ABI_ENTRY_0048cfc0(fn_0048cfc0_Entry, fn_0048cfc0_Abi)
HOOK(0x0048cfc0, "fn_0048cfc0", fn_0048cfc0_Entry, nullptr);

// 0x0048d010 (eax = manager, ecx not read (push ecx is a local), esi = array of 0x4c-byte records {+0 body}
// ended by a null body, or null): writes the record count, then each body's state (0x0048cf70). Caller 0x0048d1c0.
// Not executed in recorded runs.
uint32_t fn_0048d010(uint8_t* m, uint8_t* arr)
{
    uint32_t n = 0;
    if (arr == nullptr)
        return fn_0048c380(4, m, u(&n));
    while (Field<uint32_t>(arr, n * 0x4c) != 0)
        ++n;
    fn_0048c380(4, m, u(&n));
    uint32_t i = 0;
    uint32_t body = Field<uint32_t>(arr, 0);
    while (body != 0)
    {
        fn_0048cf70(B(body), m);
        ++i;
        body = Field<uint32_t>(arr, i * 0x4c);
    }
    return body;
}
static uint32_t fn_0048d010_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t esi)
{
    return fn_0048d010(B(eax), B(esi));
}
ABI_ENTRY_0048d010(fn_0048d010_Entry, fn_0048d010_Abi)
HOOK(0x0048d010, "fn_0048d010", fn_0048d010_Entry, nullptr);

// 0x0048d090 (eax = array of 0x4c-byte records or null, ecx not read, edi = manager): reads the record count and
// restores each record's body (0x0048cfc0); once the array is null or a body is null, the remaining states are read
// with a null body (the pointer then stops advancing, original). Caller 0x0048d280. Returns 0.
uint32_t fn_0048d090(uint8_t* arr, uint8_t* m)
{
    uint32_t n = 0;
    fn_0048c3a0(4, m, u(&n));
    while (n != 0)
    {
        --n;
        if (arr != nullptr && Field<uint32_t>(arr, 0) != 0)
        {
            fn_0048cfc0(m, Field<uint32_t>(arr, 0));
            arr += 0x4c;
        }
        else
        {
            fn_0048cfc0(m, 0);
        }
    }
    return 0;
}
static uint32_t fn_0048d090_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t edi)
{
    return fn_0048d090(B(eax), B(edi));
}
ABI_ENTRY_0048d090(fn_0048d090_Entry, fn_0048d090_Abi)
HOOK(0x0048d090, "fn_0048d090", fn_0048d090_Entry, nullptr);

// 0x0048d0f0 (ecx = object): the object's saved type id (decoded by 0x0048cb30): 0 when the object is in one of
// the object system's lists +0xec0, +0xeb8, +0xebc, +0xe6c (ObjectList_Contains, esi = list, edi = object) or has
// flag 0x80000 in +0x424; else (template index 0x0045ac30(ecx = system, esi = +0x414)) & 0x1ff | (+0x418 & 0x1ff) << 9
// | (word +0x396 & 0x3ff) << 18. Virtual method (object save vt+0, no direct callers). Not executed in recorded runs.
uint32_t Object_SaveTypeId(uint8_t* obj)
{
    static const uint32_t lists[4] = {0xec0, 0xeb8, 0xebc, 0xe6c};
    for (uint32_t i = 0; i < 4; ++i)
    {
        const uint32_t list = Field<uint32_t>(Field<uint8_t*>(Gm(), 0xa84), lists[i]);
        if (static_cast<uint8_t>(orig::fn_00454690(list, u(obj))) != 0)
            return 0;
    }
    if ((Field<uint32_t>(obj, 0x424) & 0x80000) != 0)
        return 0;
    const uint32_t t = orig::fn_0045ac30(Field<uint32_t>(Gm(), 0xa84), Field<uint32_t>(obj, 0x414));
    const uint32_t hi = (((Field<uint16_t>(obj, 0x396) & 0x3ffu) << 9) | (Field<uint32_t>(obj, 0x418) & 0x1ffu)) << 9;
    return (t & 0x1ff) | hi;
}
static uint32_t Object_SaveTypeId_Abi(uint32_t ecx)
{
    return Object_SaveTypeId(B(ecx));
}
ABI_ENTRY_0048d0f0(Object_SaveTypeId_Entry, Object_SaveTypeId_Abi)
HOOK(0x0048d0f0, "Object_SaveTypeId", Object_SaveTypeId_Entry, nullptr);

// 0x0048d1c0 (ecx = object; stack: manager; ret 4): object save write: described members (0x0048bda0 with the
// class descriptor of vt slot 1), the Havok body state (+0x1c4, 0x0048cf70), the sub-body records (+0x1bc,
// 0x0048d010), then the linked list +0x428 (next at +0): count and each 0x20-byte node. Callers 0x0048d3a0,
// 0x0048d610, 0x0048df20.
uint32_t fn_0048d1c0(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048bda0(m, desc, u(obj));
    fn_0048cf70(Field<uint8_t*>(obj, 0x1c4), m);
    fn_0048d010(m, Field<uint8_t*>(obj, 0x1bc));
    uint32_t n = 0;
    for (const uint32_t* p = Field<uint32_t*>(obj, 0x428); p != nullptr; p = reinterpret_cast<const uint32_t*>(*p))
        ++n;
    uint32_t r = fn_0048c380(4, m, u(&n));
    for (uint32_t* p = Field<uint32_t*>(obj, 0x428); p != nullptr; p = reinterpret_cast<uint32_t*>(*p))
        r = fn_0048c380(0x20, m, u(p));
    return r;
}
static uint32_t fn_0048d1c0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d1c0(B(ecx), B(a0));
}
ABI_ENTRY_0048d1c0(fn_0048d1c0_Entry, fn_0048d1c0_Abi)
HOOK(0x0048d1c0, "fn_0048d1c0", fn_0048d1c0_Entry, nullptr);

// 0x0048d250 (ecx = object; stack: manager; ret 4): object save fixup: 0x0048c010 with its class descriptor, then
// Object_UnlinkAll (ebx = object) and 0x00487df0 (esi = object). Callers 0x0048ddb0, 0x0048e0a0.
uint32_t fn_0048d250(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048c010(m, desc, u(obj));
    orig::fn_004546f0(u(obj));
    return orig::fn_00487df0(u(obj));
}
static uint32_t fn_0048d250_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d250(B(ecx), B(a0));
}
ABI_ENTRY_0048d250(fn_0048d250_Entry, fn_0048d250_Abi)
HOOK(0x0048d250, "fn_0048d250", fn_0048d250_Entry, nullptr);

// 0x0048d280 (ecx = object; stack: manager; ret 4): object save read, inverse of 0x0048d1c0: described members
// (0x0048c0f0), body state (0x0048cfc0); a pending ragdoll flag (byte +0x421) is cleared and replayed:
// Object_SetRagdoll (AL = 1, esi = object; 1, 1, 0), Body_addConstraints(object), Object_SetRagdoll (AL = 1; 1, 0,
// 0); sub-bodies (0x0048d090), then each 0x20-byte node re-added with 0x0046f830 (eax = game+0xaa4; object, node)
// and a final 0x0046f830(object, 0); +0x2d4 = 0, +0x370 = -1.0f, 0x00409ec0 (eax = object; 0, 1), 0x00453670
// (eax = object). Callers 0x0048d380, 0x0048d450, 0x0048d670, 0x0048e0b0.
uint32_t fn_0048d280(uint8_t* obj, uint8_t* m)
{
    uint8_t f[0x24] = {};  // original frame: +0 count, +4 node buffer (0x20)
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048c0f0(m, desc, u(obj));
    uint32_t r = fn_0048cfc0(m, Field<uint32_t>(obj, 0x1c4));
    if (Field<uint8_t>(obj, 0x421) != 0)
    {
        Field<uint8_t>(obj, 0x421) = 0;
        orig::fn_00459000(Lo8(r, 1), u(obj), 1, 1, 0);
        r = orig::fn_00456570(u(obj));
        orig::fn_00459000(Lo8(r, 1), u(obj), 1, 0, 0);
    }
    fn_0048d090(Field<uint8_t*>(obj, 0x1bc), m);
    fn_0048c3a0(4, m, u(f));
    while (Field<uint32_t>(f, 0) != 0)
    {
        Field<uint32_t>(f, 0) -= 1;
        fn_0048c3a0(0x20, m, u(f + 4));
        orig::fn_0046f830(Field<uint32_t>(Gm(), 0xaa4), u(obj), u(f + 4));
    }
    Field<uint32_t>(f, 0) -= 1;
    orig::fn_0046f830(Field<uint32_t>(Gm(), 0xaa4), u(obj), 0);
    Field<uint32_t>(obj, 0x2d4) = 0;
    Field<uint32_t>(obj, 0x370) = 0xbf800000;
    orig::fn_00409ec0(u(obj), 0, 1);
    return orig::fn_00453670(u(obj));
}
static uint32_t fn_0048d280_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d280(B(ecx), B(a0));
}
ABI_ENTRY_0048d280(fn_0048d280_Entry, fn_0048d280_Abi)
HOOK(0x0048d280, "fn_0048d280", fn_0048d280_Entry, nullptr);

// 0x0048d380 (ecx = object; stack: manager; ret 4): 0x0048d280 then 0x0049abf0 (eax = object). Virtual method (no
// direct callers). Not executed in recorded runs.
uint32_t fn_0048d380(uint8_t* obj, uint8_t* m)
{
    fn_0048d280(obj, m);
    return orig::fn_0049abf0(u(obj));
}
static uint32_t fn_0048d380_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d380(B(ecx), B(a0));
}
ABI_ENTRY_0048d380(fn_0048d380_Entry, fn_0048d380_Abi)
HOOK(0x0048d380, "fn_0048d380", fn_0048d380_Entry, nullptr);

// 0x0048d3a0 (ecx = object; stack: manager; ret 4): write: 0x0048d1c0, then the container +0x958: its count
// (vt+0x34) and each 12-byte item (iterator 0x004e7340 / 0x004e73c0); count 0 when there is no container. Returns
// the iterator's released reference (or the write's EAX). Caller 0x0048d4d0. Not executed in recorded runs.
uint32_t fn_0048d3a0(uint8_t* obj, uint8_t* m)
{
    uint32_t iter[2] = {0, 0};
    fn_0048d1c0(obj, m);
    uint32_t n = 0;
    void* list = Field<void*>(obj, 0x958);
    if (list == nullptr)
        return fn_0048c380(4, m, u(&n));
    n = VCall<uint32_t>(list, 0x34 / 4);
    fn_0048c380(4, m, u(&n));
    uint32_t item = orig::fn_004e7340(Field<uint32_t>(obj, 0x958), u(iter));
    while (item != 0)
    {
        fn_0048c380(0xc, m, item);
        item = orig::fn_004e73c0(u(iter));
    }
    const uint32_t held = iter[1];
    if (held != 0)
        Field<uint32_t>(B(held), 0x18) -= 1;
    return held;
}
static uint32_t fn_0048d3a0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d3a0(B(ecx), B(a0));
}
ABI_ENTRY_0048d3a0(fn_0048d3a0_Entry, fn_0048d3a0_Abi)
HOOK(0x0048d3a0, "fn_0048d3a0", fn_0048d3a0_Entry, nullptr);

// 0x0048d450 (ecx = object; stack: manager; ret 4): read, inverse of 0x0048d3a0: 0x0048d280, the item count; when
// nonzero 0x004246c0 (AL = 1 on the count's upper bytes; object) and 0x004246a0 (esi = object), then each 12-byte
// item re-added by 0x00424630 (eax = object, edi = item). Caller 0x0048d540.
uint32_t fn_0048d450(uint8_t* obj, uint8_t* m)
{
    uint8_t item[0xc] = {};
    fn_0048d280(obj, m);
    uint32_t n = 0;
    uint32_t r = fn_0048c3a0(4, m, u(&n));
    if (n == 0)
        return r;
    orig::fn_004246c0(Lo8(n, 1), u(obj));
    r = orig::fn_004246a0(u(obj));
    while (n != 0)
    {
        --n;
        fn_0048c3a0(0xc, m, u(item));
        r = orig::fn_00424630(u(obj), u(item));
    }
    return r;
}
static uint32_t fn_0048d450_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d450(B(ecx), B(a0));
}
ABI_ENTRY_0048d450(fn_0048d450_Entry, fn_0048d450_Abi)
HOOK(0x0048d450, "fn_0048d450", fn_0048d450_Entry, nullptr);

// 0x0048d4d0 (ecx = object; stack: manager; ret 4): write: 0x0048d3a0, then for each of the +0xb68 entries at
// +0xb6c: 0x0046d720 (edx = 0x0041f4f0(eax = game+0xa90, ecx = 2), esi = entry) written as 4 bytes. Virtual
// method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048d4d0(uint8_t* obj, uint8_t* m)
{
    uint32_t r = fn_0048d3a0(obj, m);
    for (uint32_t i = 0; i < Field<uint32_t>(obj, 0xb68); ++i)
    {
        const uint32_t t = orig::fn_0041f4f0(Field<uint32_t>(Gm(), 0xa90), 2);
        uint32_t v = orig::fn_0046d720(t, Field<uint32_t>(obj, 0xb6c + i * 4));
        r = fn_0048c380(4, m, u(&v));
    }
    return r;
}
static uint32_t fn_0048d4d0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d4d0(B(ecx), B(a0));
}
ABI_ENTRY_0048d4d0(fn_0048d4d0_Entry, fn_0048d4d0_Abi)
HOOK(0x0048d4d0, "fn_0048d4d0", fn_0048d4d0_Entry, nullptr);

// 0x0048d540 (ecx = object; stack: manager; ret 4): read, inverse of 0x0048d4d0: 0x0048d450, then each +0xb6c
// entry = 0x0046d740 (esi = 0x0041f4f0(game+0xa90, 2), edi = value read; 1). The `push 1` precedes the 0x0041f4f0
// call (plain ret) and is 0x0046d740's argument. Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048d540(uint8_t* obj, uint8_t* m)
{
    uint32_t r = fn_0048d450(obj, m);
    for (uint32_t i = 0; i < Field<uint32_t>(obj, 0xb68); ++i)
    {
        uint32_t v = 0;
        fn_0048c3a0(4, m, u(&v));
        const uint32_t t = orig::fn_0041f4f0(Field<uint32_t>(Gm(), 0xa90), 2);
        r = orig::fn_0046d740(t, v, 1);
        Field<uint32_t>(obj, 0xb6c + i * 4) = r;
    }
    return r;
}
static uint32_t fn_0048d540_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d540(B(ecx), B(a0));
}
ABI_ENTRY_0048d540(fn_0048d540_Entry, fn_0048d540_Abi)
HOOK(0x0048d540, "fn_0048d540", fn_0048d540_Entry, nullptr);

// 0x0048d5d0 (ecx = object; stack: manager; ret 4): collect: 0x00461b60, then lists each of the +0x4b8 objects at
// +0x4b4 (0x0048ba60). Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048d5d0(uint8_t* obj, uint8_t* m)
{
    uint32_t r = fn_00461b60(obj, u(m));
    for (uint32_t i = 0; i < Field<uint32_t>(obj, 0x4b8); ++i)
    {
        fn_0048ba60(Field<uint32_t*>(obj, 0x4b4)[i], m);
        r = Field<uint32_t>(obj, 0x4b8);
    }
    return r;
}
static uint32_t fn_0048d5d0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d5d0(B(ecx), B(a0));
}
ABI_ENTRY_0048d5d0(fn_0048d5d0_Entry, fn_0048d5d0_Abi)
HOOK(0x0048d5d0, "fn_0048d5d0", fn_0048d5d0_Entry, nullptr);

// 0x0048d610 (ecx = object; stack: manager; ret 4): write: 0x0048d1c0, then each of the +0x4b8 objects at +0x4b4
// as its object index (0x0048bb90). Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048d610(uint8_t* obj, uint8_t* m)
{
    uint32_t r = fn_0048d1c0(obj, m);
    if (Field<uint32_t>(obj, 0x4b8) == 0)
        return 0;
    for (uint32_t i = 0; i < Field<uint32_t>(obj, 0x4b8); ++i)
    {
        uint32_t v = fn_0048bb90(m, Field<uint32_t*>(obj, 0x4b4)[i]);
        fn_0048c380(4, m, u(&v));
        r = Field<uint32_t>(obj, 0x4b8);
    }
    return r;
}
static uint32_t fn_0048d610_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d610(B(ecx), B(a0));
}
ABI_ENTRY_0048d610(fn_0048d610_Entry, fn_0048d610_Abi)
HOOK(0x0048d610, "fn_0048d610", fn_0048d610_Entry, nullptr);

// 0x0048d670 (ecx = object; stack: manager; ret 4): read, inverse of 0x0048d610: 0x0048d280, then (when +0x4b8 is
// nonzero) allocates the +0x4b4 array (tag 3, "spline keys") and fills it with objects by index (0x0048bbc0).
// Virtual method (no direct callers).
uint32_t fn_0048d670(uint8_t* obj, uint8_t* m)
{
    uint32_t r = fn_0048d280(obj, m);
    const uint32_t n = Field<uint32_t>(obj, 0x4b8);
    if (n == 0)
        return n;
    Field<uint32_t>(obj, 0x4b4) = HeapAlloc_(3, n * 4, 2, 0x005b2330);
    r = Field<uint32_t>(obj, 0x4b8);
    for (uint32_t i = 0; i < Field<uint32_t>(obj, 0x4b8); ++i)
    {
        uint32_t v = 0;
        fn_0048c3a0(4, m, u(&v));
        Field<uint32_t*>(obj, 0x4b4)[i] = fn_0048bbc0(v, m);
        r = Field<uint32_t>(obj, 0x4b8);
    }
    return r;
}
static uint32_t fn_0048d670_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d670(B(ecx), B(a0));
}
ABI_ENTRY_0048d670(fn_0048d670_Entry, fn_0048d670_Abi)
HOOK(0x0048d670, "fn_0048d670", fn_0048d670_Entry, nullptr);

// 0x0048d6f0 (ecx = object; stack: manager; ret 4): collect: 0x0048bc80 with its class descriptor, then lists every
// node of the chain +4 (next at +4) (0x0048ba60). Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048d6f0(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    uint32_t r = fn_0048bc80(m, desc, u(obj));
    for (uint8_t* n = Field<uint8_t*>(obj, 4); n != nullptr; n = Field<uint8_t*>(n, 4))
        r = fn_0048ba60(u(n), m);
    return r;
}
static uint32_t fn_0048d6f0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d6f0(B(ecx), B(a0));
}
ABI_ENTRY_0048d6f0(fn_0048d6f0_Entry, fn_0048d6f0_Abi)
HOOK(0x0048d6f0, "fn_0048d6f0", fn_0048d6f0_Entry, nullptr);

// 0x0048d730 (ecx = object; stack: manager; ret 4): write: described members (0x0048bda0), then (word +0x6c + 1) *
// 0x20 bytes from +0x80. Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048d730(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048bda0(m, desc, u(obj));
    return fn_0048c380((Field<uint16_t>(obj, 0x6c) + 1u) << 5, m, Field<uint32_t>(obj, 0x80));
}
static uint32_t fn_0048d730_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d730(B(ecx), B(a0));
}
ABI_ENTRY_0048d730(fn_0048d730_Entry, fn_0048d730_Abi)
HOOK(0x0048d730, "fn_0048d730", fn_0048d730_Entry, nullptr);

// 0x0048d770 (ecx = object; stack: manager; ret 4): read, inverse of 0x0048d730. Virtual method (no direct callers).
// Not executed in recorded runs.
uint32_t fn_0048d770(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048c0f0(m, desc, u(obj));
    return fn_0048c3a0((Field<uint16_t>(obj, 0x6c) + 1u) << 5, m, Field<uint32_t>(obj, 0x80));
}
static uint32_t fn_0048d770_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d770(B(ecx), B(a0));
}
ABI_ENTRY_0048d770(fn_0048d770_Entry, fn_0048d770_Abi)
HOOK(0x0048d770, "fn_0048d770", fn_0048d770_Entry, nullptr);

// 0x0048d7b0 (ecx = object; stack: manager; ret 4): collect: 0x0048bc80 with the object's class descriptor, then
// the global tables 0x005802d8 on g_game, 0x00584400 on 0x0063a1e8 and 0x005852b8 on 0x0063a140. Virtual method
// (no direct callers). Not executed in recorded runs.
uint32_t fn_0048d7b0(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048bc80(m, desc, u(obj));
    fn_0048bc80(m, 0x005802d8, u(Gm()));
    fn_0048bc80(m, 0x00584400, 0x0063a1e8);
    return fn_0048bc80(m, 0x005852b8, 0x0063a140);
}
static uint32_t fn_0048d7b0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d7b0(B(ecx), B(a0));
}
ABI_ENTRY_0048d7b0(fn_0048d7b0_Entry, fn_0048d7b0_Abi)
HOOK(0x0048d7b0, "fn_0048d7b0", fn_0048d7b0_Entry, nullptr);

// 0x0048d800 (ecx = object; stack: manager; ret 4): write: the same four tables as 0x0048d7b0 (0x0048bda0), then a
// block from 0x0046f250 (eax = game+0xaa4, edx = &size; &data; AL = ok, size 0 otherwise): size and data. Virtual
// method (no direct callers). Not executed in recorded runs. (When 0x0046f250 fails the data pointer slot keeps
// the manager pointer, written with size 0.)
uint32_t fn_0048d800(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048bda0(m, desc, u(obj));
    fn_0048bda0(m, 0x005802d8, u(Gm()));
    fn_0048bda0(m, 0x00584400, 0x0063a1e8);
    fn_0048bda0(m, 0x005852b8, 0x0063a140);
    uint32_t size = 0;
    uint32_t data = u(m);
    if (static_cast<uint8_t>(orig::fn_0046f250(Field<uint32_t>(Gm(), 0xaa4), u(&size), u(&data))) == 0)
        size = 0;
    fn_0048c380(4, m, u(&size));
    return fn_0048c380(size, m, data);
}
static uint32_t fn_0048d800_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d800(B(ecx), B(a0));
}
ABI_ENTRY_0048d800(fn_0048d800_Entry, fn_0048d800_Abi)
HOOK(0x0048d800, "fn_0048d800", fn_0048d800_Entry, nullptr);

// 0x0048d8a0 (ecx = object; stack: manager; ret 4): fixup: 0x0048c010 with the same four tables as 0x0048d7b0.
// Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048d8a0(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048c010(m, desc, u(obj));
    fn_0048c010(m, 0x005802d8, u(Gm()));
    fn_0048c010(m, 0x00584400, 0x0063a1e8);
    return fn_0048c010(m, 0x005852b8, 0x0063a140);
}
static uint32_t fn_0048d8a0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d8a0(B(ecx), B(a0));
}
ABI_ENTRY_0048d8a0(fn_0048d8a0_Entry, fn_0048d8a0_Abi)
HOOK(0x0048d8a0, "fn_0048d8a0", fn_0048d8a0_Entry, nullptr);

// 0x0048d8f0 (ecx = object; stack: manager; ret 4): read, inverse of 0x0048d800: the four tables (0x0048c0f0), the
// current block (0x0046f250, eax = game+0xaa4, edx = &size; &data; result not checked, the size slot starts as the
// manager pointer as in the original's reused argument slot), the saved size, then reads `size` bytes into the block
// when the saved size matches, else into null; 0x0040f8c0 (edx = object; game+0x4b0). Virtual method (no direct
// callers). Not executed in recorded runs.
uint32_t fn_0048d8f0(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048c0f0(m, desc, u(obj));
    fn_0048c0f0(m, 0x005802d8, u(Gm()));
    fn_0048c0f0(m, 0x00584400, 0x0063a1e8);
    fn_0048c0f0(m, 0x005852b8, 0x0063a140);
    uint32_t size = u(m);
    uint32_t data = 0;
    uint32_t saved = 0;
    orig::fn_0046f250(Field<uint32_t>(Gm(), 0xaa4), u(&size), u(&data));
    fn_0048c3a0(4, m, u(&saved));
    fn_0048c3a0(size, m, saved == size ? data : 0);
    return orig::fn_0040f8c0(u(obj), Field<uint32_t>(Gm(), 0x4b0));
}
static uint32_t fn_0048d8f0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d8f0(B(ecx), B(a0));
}
ABI_ENTRY_0048d8f0(fn_0048d8f0_Entry, fn_0048d8f0_Abi)
HOOK(0x0048d8f0, "fn_0048d8f0", fn_0048d8f0_Entry, nullptr);

// 0x0048d9a0 (ecx = object; stack: manager; ret 4): collect: 0x0048bc80, then every node of the chain +0xc (next at
// +0x50) (0x0048ba60). Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048d9a0(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    uint32_t r = fn_0048bc80(m, desc, u(obj));
    for (uint8_t* n = Field<uint8_t*>(obj, 0xc); n != nullptr; n = Field<uint8_t*>(n, 0x50))
        r = fn_0048ba60(u(n), m);
    return r;
}
static uint32_t fn_0048d9a0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d9a0(B(ecx), B(a0));
}
ABI_ENTRY_0048d9a0(fn_0048d9a0_Entry, fn_0048d9a0_Abi)
HOOK(0x0048d9a0, "fn_0048d9a0", fn_0048d9a0_Entry, nullptr);

// 0x0048d9e0 (ecx = object; stack: manager; ret 4): write: described members, then +0x1c * 16 bytes at +0x44,
// +0x1c * 8 bytes at +0x4c (when set) and +0x20 * 2 bytes at +0x48. Virtual method (no direct callers). Not
// executed in recorded runs.
uint32_t fn_0048d9e0(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048bda0(m, desc, u(obj));
    fn_0048c380(Field<uint32_t>(obj, 0x1c) << 4, m, Field<uint32_t>(obj, 0x44));
    if (Field<uint32_t>(obj, 0x4c) != 0)
        fn_0048c380(Field<uint32_t>(obj, 0x1c) << 3, m, Field<uint32_t>(obj, 0x4c));
    return fn_0048c380(Field<uint32_t>(obj, 0x20) << 1, m, Field<uint32_t>(obj, 0x48));
}
static uint32_t fn_0048d9e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048d9e0(B(ecx), B(a0));
}
ABI_ENTRY_0048d9e0(fn_0048d9e0_Entry, fn_0048d9e0_Abi)
HOOK(0x0048d9e0, "fn_0048d9e0", fn_0048d9e0_Entry, nullptr);

// 0x0048da40 (ecx = object; stack: manager; ret 4): read, inverse of 0x0048d9e0. Virtual method (no direct
// callers). Not executed in recorded runs.
uint32_t fn_0048da40(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048c0f0(m, desc, u(obj));
    fn_0048c3a0(Field<uint32_t>(obj, 0x1c) << 4, m, Field<uint32_t>(obj, 0x44));
    if (Field<uint32_t>(obj, 0x4c) != 0)
        fn_0048c3a0(Field<uint32_t>(obj, 0x1c) << 3, m, Field<uint32_t>(obj, 0x4c));
    return fn_0048c3a0(Field<uint32_t>(obj, 0x20) << 1, m, Field<uint32_t>(obj, 0x48));
}
static uint32_t fn_0048da40_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048da40(B(ecx), B(a0));
}
ABI_ENTRY_0048da40(fn_0048da40_Entry, fn_0048da40_Abi)
HOOK(0x0048da40, "fn_0048da40", fn_0048da40_Entry, nullptr);

// 0x0048daa0 (ecx = object; stack: manager; ret 4): write: the count of nodes in the list +4 (next at +0) whose +0xc
// is not 1, then each such node with the descriptor table 0x00588ca8 (0x0048bda0). Virtual method (no direct
// callers). Not executed in recorded runs.
uint32_t fn_0048daa0(uint8_t* obj, uint8_t* m)
{
    uint32_t n = 0;
    for (uint8_t* p = Field<uint8_t*>(obj, 4); p != nullptr; p = Field<uint8_t*>(p, 0))
        if (Field<uint32_t>(p, 0xc) != 1)
            ++n;
    uint32_t r = fn_0048c380(4, m, u(&n));
    for (uint8_t* p = Field<uint8_t*>(obj, 4); p != nullptr; p = Field<uint8_t*>(p, 0))
        if (Field<uint32_t>(p, 0xc) != 1)
            r = fn_0048bda0(m, 0x00588ca8, u(p));
    return r;
}
static uint32_t fn_0048daa0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048daa0(B(ecx), B(a0));
}
ABI_ENTRY_0048daa0(fn_0048daa0_Entry, fn_0048daa0_Abi)
HOOK(0x0048daa0, "fn_0048daa0", fn_0048daa0_Entry, nullptr);

// 0x0048db10 (ecx = object; stack: manager; ret 4): read, inverse of 0x0048daa0: moves every node of the list +4
// whose +0xc is not 1 to the list +8 (PostEffect_MoveToList 0x0047c030: eax = node, edx = destination link; object),
// reads the count, then for each takes the head of +8, moves it after the current tail link of +4 and reads it with
// the table 0x00588ca8 (0x0048c0f0). Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048db10(uint8_t* obj, uint8_t* m)
{
    uint32_t link = u(obj + 4);
    uint32_t node = *D(link);
    while (node != 0)
    {
        if (Field<uint32_t>(B(node), 0xc) == 1)
            link = node;
        else
            orig::fn_0047c030(node, u(obj + 8), u(obj));
        node = *D(link);
    }
    uint32_t n = 0;
    uint32_t r = fn_0048c3a0(4, m, u(&n));
    while (n != 0)
    {
        --n;
        const uint32_t head = Field<uint32_t>(obj, 8);
        orig::fn_0047c030(head, link, u(obj));
        r = fn_0048c0f0(m, 0x00588ca8, head);
        link = head;
    }
    return r;
}
static uint32_t fn_0048db10_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048db10(B(ecx), B(a0));
}
ABI_ENTRY_0048db10(fn_0048db10_Entry, fn_0048db10_Abi)
HOOK(0x0048db10, "fn_0048db10", fn_0048db10_Entry, nullptr);

// 0x0048dba0 (ecx = object; stack: manager; ret 4): collect: 0x0048bc80 with its descriptor, then every node of the
// three lists at +0x1c, +0x20, +0x24 (0x0046d700 first / 0x0046d710 next, ecx = list) with the table 0x00588888.
// Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048dba0(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    uint32_t r = fn_0048bc80(m, desc, u(obj));
    for (uint32_t k = 0; k < 3; ++k)
    {
        const uint32_t list = Field<uint32_t>(obj, 0x1c + k * 4);
        for (uint32_t n = orig::fn_0046d700(list); n != 0; n = orig::fn_0046d710(n, Field<uint32_t>(obj, 0x1c + k * 4)))
            r = fn_0048bc80(m, 0x00588888, n);
    }
    return r;
}
static uint32_t fn_0048dba0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048dba0(B(ecx), B(a0));
}
ABI_ENTRY_0048dba0(fn_0048dba0_Entry, fn_0048dba0_Abi)
HOOK(0x0048dba0, "fn_0048dba0", fn_0048dba0_Entry, nullptr);

// 0x0048dc00 (ecx = object; stack: manager; ret 4): write: described members, then for each of the three lists at
// +0x1c..+0x24 its node count and each node with the table 0x00588888 (0x0048bda0). Virtual method (no direct
// callers). Not executed in recorded runs.
uint32_t fn_0048dc00(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    uint32_t r = fn_0048bda0(m, desc, u(obj));
    for (uint32_t k = 0; k < 3; ++k)
    {
        const uint32_t list = Field<uint32_t>(obj, 0x1c + k * 4);
        uint32_t n = 0;
        for (uint32_t p = orig::fn_0046d700(list); p != 0; p = orig::fn_0046d710(p, list))
            ++n;
        fn_0048c380(4, m, u(&n));
        for (uint32_t p = orig::fn_0046d700(Field<uint32_t>(obj, 0x1c + k * 4)); p != 0;
             p = orig::fn_0046d710(p, Field<uint32_t>(obj, 0x1c + k * 4)))
            r = fn_0048bda0(m, 0x00588888, p);
    }
    return r;
}
static uint32_t fn_0048dc00_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048dc00(B(ecx), B(a0));
}
ABI_ENTRY_0048dc00(fn_0048dc00_Entry, fn_0048dc00_Abi)
HOOK(0x0048dc00, "fn_0048dc00", fn_0048dc00_Entry, nullptr);

// 0x0048dca0 (ecx = object; stack: manager; ret 4): read, inverse of 0x0048dc00: for each of the three lists the
// count, then each node allocated with Particle_Alloc (edx = list) and read with the table 0x00588888. Virtual
// method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048dca0(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    uint32_t r = fn_0048c0f0(m, desc, u(obj));
    for (uint32_t k = 0; k < 3; ++k)
    {
        uint32_t n = 0;
        fn_0048c3a0(4, m, u(&n));
        while (n != 0)
        {
            --n;
            const uint32_t node = orig::fn_0046d910(Field<uint32_t>(obj, 0x1c + k * 4));
            r = fn_0048c0f0(m, 0x00588888, node);
        }
    }
    return r;
}
static uint32_t fn_0048dca0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048dca0(B(ecx), B(a0));
}
ABI_ENTRY_0048dca0(fn_0048dca0_Entry, fn_0048dca0_Abi)
HOOK(0x0048dca0, "fn_0048dca0", fn_0048dca0_Entry, nullptr);

namespace
{
// The eight movement speed globals and their names (0x0048dd20 / 0x0048ddb0).
const uint32_t kSpeedInfo[8][2] = {
    {0x005feb54, 0x0059044c}, {0x005feb2c, 0x00590438}, {0x005feb04, 0x00590424}, {0x005feadc, 0x0059040c},
    {0x005feab4, 0x005903f8}, {0x005fea8c, 0x005903e4}, {0x005fea64, 0x005903d0}, {0x005fea3c, 0x005903bc},
};
}  // namespace

// 0x0048dd20 (ecx = object; stack: manager; ret 4): collect: registers the eight g_*SpeedInfo globals
// (0x005fea3c..0x005feb54, names "g_walkFwdSpeedInfo" etc. at 0x005903bc..0x0059044c) with 0x0048bab0, then
// 0x00461b60. Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048dd20(void* obj, uint8_t* m)
{
    for (uint32_t i = 0; i < 8; ++i)
        fn_0048bab0(kSpeedInfo[i][0], m, kSpeedInfo[i][1]);
    return fn_00461b60(obj, u(m));
}
static uint32_t fn_0048dd20_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048dd20(reinterpret_cast<void*>(ecx), B(a0));
}
ABI_ENTRY_0048dd20(fn_0048dd20_Entry, fn_0048dd20_Abi)
HOOK(0x0048dd20, "fn_0048dd20", fn_0048dd20_Entry, nullptr);

// 0x0048ddb0 (ecx = object; stack: manager; ret 4): fixup: the same eight registrations, then 0x0048d250. Virtual
// method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048ddb0(uint8_t* obj, uint8_t* m)
{
    for (uint32_t i = 0; i < 8; ++i)
        fn_0048bab0(kSpeedInfo[i][0], m, kSpeedInfo[i][1]);
    return fn_0048d250(obj, m);
}
static uint32_t fn_0048ddb0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048ddb0(B(ecx), B(a0));
}
ABI_ENTRY_0048ddb0(fn_0048ddb0_Entry, fn_0048ddb0_Abi)
HOOK(0x0048ddb0, "fn_0048ddb0", fn_0048ddb0_Entry, nullptr);

// 0x0048de40 (ecx = fx object; stack: manager; ret 4): collect: 0x00461b60, then the emitters at +0x4ec (0x98-byte
// entries while byte flag 8 is set, at most 10; table 0x005883d8), the particle emitters at +0x4f0 (0x194 bytes,
// at most 10; table 0x00588048), the 4 entries at +0x514 (0x174 bytes; table 0x005885b8) and +0x518 (table
// 0x00588510). Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048de40(uint8_t* obj, uint8_t* m)
{
    uint32_t r = fn_00461b60(obj, u(m));
    if (Field<uint32_t>(obj, 0x4ec) != 0)
    {
        for (uint32_t off = 0; off < 0x5f0; off += 0x98)
        {
            const uint32_t e = Field<uint32_t>(obj, 0x4ec) + off;
            if ((*B(e) & 8) == 0)
                break;
            r = fn_0048bc80(m, 0x005883d8, e);
        }
    }
    if (Field<uint32_t>(obj, 0x4f0) != 0)
    {
        for (uint32_t off = 0; off < 0xfc8; off += 0x194)
        {
            const uint32_t e = Field<uint32_t>(obj, 0x4f0) + off;
            if ((*B(e) & 8) == 0)
                break;
            r = fn_0048bc80(m, 0x00588048, e);
        }
    }
    if (Field<uint32_t>(obj, 0x514) != 0)
    {
        for (uint32_t off = 0; off < 0x5d0; off += 0x174)
            r = fn_0048bc80(m, 0x005885b8, Field<uint32_t>(obj, 0x514) + off);
    }
    const uint32_t last = Field<uint32_t>(obj, 0x518);
    if (last != 0)
        r = fn_0048bc80(m, 0x00588510, last);
    return r;
}
static uint32_t fn_0048de40_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048de40(B(ecx), B(a0));
}
ABI_ENTRY_0048de40(fn_0048de40_Entry, fn_0048de40_Abi)
HOOK(0x0048de40, "fn_0048de40", fn_0048de40_Entry, nullptr);

// 0x0048df20 (ecx = fx object; stack: manager; ret 4): write: 0x0048d1c0, then the count of flagged (bit 3) leading
// emitters at +0x4ec (at most 10, 0 when absent) and each (table 0x005883d8), the same for +0x4f0 (table
// 0x00588048), then the pointer +0x514 (as a presence flag) and its block (table 0x005885b8), and +0x518 likewise
// (table 0x00588510). Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048df20(uint8_t* obj, uint8_t* m)
{
    fn_0048d1c0(obj, m);
    uint32_t n = 0;
    const uint32_t tables[2][4] = {{0x4ec, 0x98, 0x005883d8, 0}, {0x4f0, 0x194, 0x00588048, 0}};
    for (uint32_t k = 0; k < 2; ++k)
    {
        const uint32_t arr = Field<uint32_t>(obj, tables[k][0]);
        const uint32_t stride = tables[k][1];
        if (arr == 0)
        {
            n = 0;
            fn_0048c380(4, m, u(&n));
            continue;
        }
        n = 0;
        while ((*B(arr + n * stride) & 8) != 0)
        {
            ++n;
            if (n >= 0xa)
                break;
        }
        fn_0048c380(4, m, u(&n));
        for (uint32_t i = 0; i < n; ++i)
            fn_0048bda0(m, tables[k][2], Field<uint32_t>(obj, tables[k][0]) + i * stride);
    }
    n = Field<uint32_t>(obj, 0x514);
    fn_0048c380(4, m, u(&n));
    if (Field<uint32_t>(obj, 0x514) != 0)
        fn_0048bda0(m, 0x005885b8, Field<uint32_t>(obj, 0x514));
    n = Field<uint32_t>(obj, 0x518);
    uint32_t r = fn_0048c380(4, m, u(&n));
    if (Field<uint32_t>(obj, 0x518) != 0)
        r = fn_0048bda0(m, 0x00588510, Field<uint32_t>(obj, 0x518));
    return r;
}
static uint32_t fn_0048df20_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048df20(B(ecx), B(a0));
}
ABI_ENTRY_0048df20(fn_0048df20_Entry, fn_0048df20_Abi)
HOOK(0x0048df20, "fn_0048df20", fn_0048df20_Entry, nullptr);

// 0x0048e0b0 (ecx = fx object; stack: manager; ret 4): read, inverse of 0x0048df20: 0x0048d280; with a local
// context {0, object}: count emitters, each from FxObject_AllocEmitter (eax = &context; game+0xa90) read with table
// 0x005883d8; count particle emitters (FxObject_AllocParticleEmitter, table 0x00588048); when the +0x514 flag is
// set FxObject_EnsureUvScroll (esi = &context; game+0xa90) and +0x514 read with 0x005885b8; when the +0x518 flag is
// set FxObject_EnsureTexAnim and +0x518 read with 0x00588510. Virtual method (no direct callers).
uint32_t fn_0048e0b0(uint8_t* obj, uint8_t* m)
{
    uint32_t r = fn_0048d280(obj, m);
    uint32_t n = 0;
    uint32_t ctx[2] = {0, u(obj)};
    fn_0048c3a0(4, m, u(&n));
    for (uint32_t i = 0; i < n; ++i)
    {
        const uint32_t e = orig::fn_0041e420(u(ctx), Field<uint32_t>(Gm(), 0xa90));
        r = fn_0048c0f0(m, 0x005883d8, e);
    }
    fn_0048c3a0(4, m, u(&n));
    for (uint32_t i = 0; i < n; ++i)
    {
        const uint32_t e = orig::fn_0041e6b0(u(ctx), Field<uint32_t>(Gm(), 0xa90));
        r = fn_0048c0f0(m, 0x00588048, e);
    }
    r = fn_0048c3a0(4, m, u(&n));
    if (n != 0)
    {
        orig::fn_00420520(u(ctx), Field<uint32_t>(Gm(), 0xa90));
        r = fn_0048c0f0(m, 0x005885b8, Field<uint32_t>(obj, 0x514));
    }
    r = fn_0048c3a0(4, m, u(&n));
    if (n != 0)
    {
        orig::fn_0041e2c0(u(ctx), Field<uint32_t>(Gm(), 0xa90));
        r = fn_0048c0f0(m, 0x00588510, Field<uint32_t>(obj, 0x518));
    }
    return r;
}
static uint32_t fn_0048e0b0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048e0b0(B(ecx), B(a0));
}
ABI_ENTRY_0048e0b0(fn_0048e0b0_Entry, fn_0048e0b0_Abi)
HOOK(0x0048e0b0, "fn_0048e0b0", fn_0048e0b0_Entry, nullptr);

// 0x0048e200 (ecx = object with texture records; stack: manager; ret 4): write: described members, the count of
// the +0x20 records (0x24 bytes at +0x18) with bit 2 of byte +0x23, then for each such record its index, and the
// renderer's raw texture data (vt+0x14c(record, &data, &size), AL = ok): size and data, then the data is freed;
// size 0 when the renderer fails. Virtual method (no direct callers). Not executed in recorded runs.
uint32_t fn_0048e200(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    fn_0048bda0(m, desc, u(obj));
    uint32_t n = 0;
    {
        const uint8_t* rec = Field<uint8_t*>(obj, 0x18);
        for (uint32_t i = 0; i < Field<uint32_t>(obj, 0x20); ++i)
            if ((rec[i * 0x24 + 0x23] & 4) != 0)
                ++n;
    }
    uint32_t r = fn_0048c380(4, m, u(&n));
    uint8_t* rec = Field<uint8_t*>(obj, 0x18);
    for (uint32_t i = 0; i < Field<uint32_t>(obj, 0x20); ++i, rec += 0x24)
    {
        if ((rec[0x23] & 4) == 0)
            continue;
        uint32_t index = i;
        fn_0048c380(4, m, u(&index));
        uint32_t size = 0;
        uint32_t data = 0;
        const uint32_t ok =
            VCall<uint32_t>(Field<void*>(Gm(), 0x38), 0x14c / 4, u(rec), u(&data), u(&size));
        if (static_cast<uint8_t>(ok) != 0)
        {
            fn_0048c380(4, m, u(&size));
            fn_0048c380(size, m, data);
            r = Free(data);
        }
        else
        {
            size = 0;
            r = fn_0048c380(4, m, u(&size));
        }
    }
    return r;
}
static uint32_t fn_0048e200_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048e200(B(ecx), B(a0));
}
ABI_ENTRY_0048e200(fn_0048e200_Entry, fn_0048e200_Abi)
HOOK(0x0048e200, "fn_0048e200", fn_0048e200_Entry, nullptr);

// 0x0048e310 (ecx = object with texture records; stack: manager; ret 4): read, inverse of 0x0048e200 (texture data
// restore): for each saved record index and size, reads the data into a "Temp raw texture data" buffer (tag 0x1a)
// and hands it to the renderer (vt+0x150(record, data, size)), then frees it. Virtual method (no direct callers).
// Not executed in recorded runs.
uint32_t fn_0048e310(uint8_t* obj, uint8_t* m)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    uint32_t r = fn_0048c0f0(m, desc, u(obj));
    uint32_t n = 0;
    r = fn_0048c3a0(4, m, u(&n));
    while (n != 0)
    {
        --n;
        uint32_t index = 0;
        uint32_t size = 0;
        fn_0048c3a0(4, m, u(&index));
        const uint32_t rec = Field<uint32_t>(obj, 0x18) + index * 0x24;
        r = fn_0048c3a0(4, m, u(&size));
        if (size != 0)
        {
            const uint32_t buf = HeapAlloc_(0x1a, size, 2, 0x005903a4);
            fn_0048c3a0(size, m, buf);
            VCall<uint32_t>(Field<void*>(Gm(), 0x38), 0x150 / 4, rec, buf, size);
            r = Free(buf);
        }
    }
    return r;
}
static uint32_t fn_0048e310_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048e310(B(ecx), B(a0));
}
ABI_ENTRY_0048e310(fn_0048e310_Entry, fn_0048e310_Abi)
HOOK(0x0048e310, "fn_0048e310", fn_0048e310_Entry, nullptr);

// 0x0048e3f0 (eax = container field, ecx = name, ebx = manager; stack: no-items flag; caller pops): collect for a
// container member (save type 8): registers it in the +0x10 list (0x0048bab0, edi = manager; name): the container
// [field], or the field itself when the container is null (original); unless the flag is set, lists every item of
// the container (iterator 0x004e7340 / 0x004e73c0, 0x0048ba60) and releases the iterator's reference. Returns the
// flag, that reference or 0x0048bab0's EAX. Caller 0x0048bc80.
uint32_t fn_0048e3f0(uint32_t field, uint32_t name, uint8_t* m, uint32_t noItems)
{
    uint32_t iter[2] = {0, 0};
    const uint32_t c = *D(field);
    if (c == 0)
        return fn_0048bab0(field, m, name);
    fn_0048bab0(c, m, name);
    if (noItems != 0)
        return noItems;
    uint32_t item = orig::fn_004e7340(c, u(iter));
    while (item != 0)
    {
        fn_0048ba60(item, m);
        item = orig::fn_004e73c0(u(iter));
    }
    const uint32_t held = iter[1];
    if (held != 0)
        Field<uint32_t>(B(held), 0x18) -= 1;
    return held;
}
static uint32_t fn_0048e3f0_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t a0)
{
    return fn_0048e3f0(eax, ecx, B(ebx), a0);
}
ABI_ENTRY_0048e3f0(fn_0048e3f0_Entry, fn_0048e3f0_Abi)
HOOK(0x0048e3f0, "fn_0048e3f0", fn_0048e3f0_Entry, nullptr);

// 0x0048e460 (eax = container field; stack: manager, flag (unused), name; caller pops): fixup for a container
// member: creates a missing container (0x18 bytes, tag 0x10, "new"; 0x004e7010 eax = game+0xac4, edx = 0, esi =
// block; 0 when the allocation fails) and registers it (0x0048bab0, edi = manager; name). Caller 0x0048c010.
uint32_t fn_0048e460(uint32_t field, uint32_t mgr, uint32_t /*flag*/, uint32_t name)
{
    if (*D(field) == 0)
    {
        const uint32_t blk = HeapAlloc_(0x10, 0x18, 2, 0x0058b5e8);
        *D(field) = blk != 0 ? orig::fn_004e7010(Field<uint32_t>(Gm(), 0xac4), 0, blk) : 0;
    }
    return fn_0048bab0(*D(field), B(mgr), name);
}
static uint32_t fn_0048e460_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0048e460(eax, a0, a1, a2);
}
ABI_ENTRY_0048e460(fn_0048e460_Entry, fn_0048e460_Abi)
HOOK(0x0048e460, "fn_0048e460", fn_0048e460_Entry, nullptr);

// 0x0048e4b0 (ebx = manager; stack: container or null, mode; caller pops): write for a container member (save type
// 8): null -> -1; mode 0: the count of items found in the object list (0x0048bb90), then each nonzero index; mode 1:
// the same with the +0x10 list (0x0048bbd0); other modes write nothing. Two iterator passes (0x004e7340 /
// 0x004e73c0); only the second pass's reference is released (original). Caller 0x0048bda0. Not executed in recorded
// runs.
uint32_t fn_0048e4b0(uint8_t* m, uint32_t list, uint32_t mode)
{
    uint32_t iter[2] = {0, 0};
    uint32_t count = 0;
    uint32_t idx = 0;
    if (list == 0)
    {
        count = 0xffffffffu;
        return fn_0048c380(4, m, u(&count));
    }
    if (mode != 0 && mode != 1)
        return mode - 1;
    auto lookup = [&](uint32_t item) { return mode == 0 ? fn_0048bb90(m, item) : fn_0048bbd0(m, item); };
    for (uint32_t item = orig::fn_004e7340(list, u(iter)); item != 0; item = orig::fn_004e73c0(u(iter)))
    {
        idx = lookup(item);
        if (idx != 0)
            ++count;
    }
    fn_0048c380(4, m, u(&count));
    for (uint32_t item = orig::fn_004e7340(list, u(iter)); item != 0; item = orig::fn_004e73c0(u(iter)))
    {
        idx = lookup(item);
        if (idx != 0)
            fn_0048c380(4, m, u(&idx));
    }
    const uint32_t held = iter[1];
    if (held != 0)
        Field<uint32_t>(B(held), 0x18) -= 1;
    return held;
}
static uint32_t fn_0048e4b0_Abi(uint32_t ebx, uint32_t a0, uint32_t a1)
{
    return fn_0048e4b0(B(ebx), a0, a1);
}
ABI_ENTRY_0048e4b0(fn_0048e4b0_Entry, fn_0048e4b0_Abi)
HOOK(0x0048e4b0, "fn_0048e4b0", fn_0048e4b0_Entry, nullptr);

// 0x0048e630 (eax = container field, edi = manager; stack: mode, unused; caller pops): read for a container member:
// count -1 destroys the container (vt+0x10(1)) and clears the field; otherwise mode 0 adds each object read by index
// (0x0048bbc0) with vt+0x18(item), mode 1 the same from the +0x10 list (0x0048bc00). Returns the count (or the
// destroy call's EAX / -1). Caller 0x0048c0f0.
uint32_t fn_0048e630(uint32_t field, uint8_t* m, uint32_t mode, uint32_t /*a1*/)
{
    void* c = reinterpret_cast<void*>(*D(field));
    uint32_t n = 0;
    fn_0048c3a0(4, m, u(&n));
    if (n == 0xffffffffu)
    {
        uint32_t r = n;
        if (c != nullptr)
            r = VCall<uint32_t>(c, 0x10 / 4, 1u);
        *D(field) = 0;
        return r;
    }
    if (n == 0 || (mode != 0 && mode != 1))
        return n;
    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t idx = 0;
        fn_0048c3a0(4, m, u(&idx));
        const uint32_t item = mode == 0 ? fn_0048bbc0(idx, m) : fn_0048bc00(idx, m);
        VCall<uint32_t>(c, 0x18 / 4, item);
    }
    return n;
}
static uint32_t fn_0048e630_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_0048e630(eax, B(edi), a0, a1);
}
ABI_ENTRY_0048e630(fn_0048e630_Entry, fn_0048e630_Abi)
HOOK(0x0048e630, "fn_0048e630", fn_0048e630_Entry, nullptr);

// ================================================================================================================
// 0x004a07a0 - 0x004a3930: unlocks, save slot state, quick load, progress.dat
// ================================================================================================================
// Game save-slot state (from use): +0x4cc slot number (1..6), +0x4d0 wide slot name, +0x510 wide quick-save slot
// name, byte +0x568 flags (4, 8 quick save available, 0x10 progress saving on, 0x20), +0x56c saves counter,
// progress (progress.dat): +0x1fc, +0x200 counts and the 0x80-byte tables +0x28c, +0x30c.

uint32_t fn_004a2cb0();
uint32_t fn_004a3840(uint32_t name);

// 0x004a07a0 (eax = path, ecx = game, edi = unlock category; stack: byte apply; ret 4): unlocks an item: unless
// 0x004a0720 (eax = path, ecx = category, ebx = game) reports it known or 0x004a0690 (eax = ecx = category, esi =
// path; game) rejects it, and while the category has fewer than 0x20 entries (game+0x1f8[category]), appends the
// hash (Str_HashName 0x004620b0) of the path's file name (_splitpath) to game+0x20c[category * 32 + n]. With apply:
// categories 1 and 2 save progress (0x004a3840, eax = 0; its ecx = the new count, not read), others run
// Options_Apply (AL = 1, esi = game). Category 2 always ends with 0x004a0850 (ecx = game). Callers include
// 0x00415940, 0x0044daf0, 0x004a0850.
uint32_t Unlock_Add(uint32_t path, uint8_t* g, uint32_t cat, uint32_t apply)
{
    char name[0x100] = {};
    uint32_t r = orig::fn_004a0720(path, cat, u(g));
    if (static_cast<uint8_t>(r) == 0)
    {
        r = orig::fn_004a0690(cat, cat, path, u(g));
        if (static_cast<uint8_t>(r) != 0 && Field<uint32_t>(g, 0x1f8 + cat * 4) < 0x20)
        {
            SplitPath(path, 0, 0, u(name), 0);
            const uint32_t h = orig::fn_004620b0(u(name));
            const uint32_t n = Field<uint32_t>(g, 0x1f8 + cat * 4);
            Field<uint32_t>(g, 0x20c + ((cat << 5) + n) * 4) = h;
            Field<uint32_t>(g, 0x1f8 + cat * 4) = n + 1;
            r = Lo8(h, apply);
            if (static_cast<uint8_t>(apply) != 0)
            {
                if (cat == 1 || cat == 2)
                    r = fn_004a3840(0);
                else
                    r = orig::fn_00401ce0(Lo8(h, 1), u(g));
            }
        }
    }
    if (cat == 2)
        r = orig::fn_004a0850(u(g));
    return r;
}
static uint32_t Unlock_Add_Abi(uint32_t eax, uint32_t ecx, uint32_t edi, uint32_t a0)
{
    return Unlock_Add(eax, B(ecx), edi, a0);
}
ABI_ENTRY_004a07a0(Unlock_Add_Entry, Unlock_Add_Abi)
HOOK(0x004a07a0, "Unlock_Add", Unlock_Add_Entry, nullptr);

// 0x004a2cb0: resets the save slot: deletes the L"SCRATCH" slot files (0x0048b500), clears the slot number, slot
// name and quick-save name, flag 0x10 and the saves counter; with flag 4: flags = flags & ~0x20 | 8 and the quick-save
// name = "SCRATCH" (0x00403be0, eax = name, edx = destination, edi = 0); else flags = flags & ~8 | 0x20. Ends with
// 0x004a3930 (eax = 0, tail jump in the original). Callers 0x004921a0, 0x004a2d40, 0x004a2de0.
uint32_t fn_004a2cb0()
{
    fn_0048b500(0x0059d9c8);
    uint8_t* g = Gm();
    Field<uint32_t>(g, 0x4cc) = 0;
    Field<uint16_t>(g, 0x4d0) = 0;
    Field<uint16_t>(g, 0x510) = 0;
    Field<uint8_t>(g, 0x568) &= 0xef;
    Field<uint32_t>(g, 0x56c) = 0;
    const uint8_t f = Field<uint8_t>(g, 0x568);
    if ((f & 4) != 0)
    {
        Field<uint8_t>(g, 0x568) = static_cast<uint8_t>((f & 0xdf) | 8);
        orig::fn_00403be0(0x0059d9c8, u(g + 0x510), 0);
        return fn_004a3930(0);
    }
    Field<uint8_t>(g, 0x568) = static_cast<uint8_t>((f & 0xf7) | 0x20);
    return fn_004a3930(0);
}
static uint32_t fn_004a2cb0_Abi()
{
    return fn_004a2cb0();
}
ABI_ENTRY_004a2cb0(fn_004a2cb0_Entry, fn_004a2cb0_Abi)
HOOK(0x004a2cb0, "fn_004a2cb0", fn_004a2cb0_Entry, nullptr);

// 0x004a2d40 (eax = slot number, ebx = wide slot name or 0): selects a save slot: resets (0x004a2cb0); for slots
// 1..6 stores the number and the name (given, or built by 0x004a2c10(eax = slot; destination)); with flag 4 sets
// flag 0x10, else clears the saves counter, sets flags 0x18 and copies the name to the quick-save name. Ends with
// 0x004a3930 (eax = 0, tail jump). Caller 0x004a2de0. Returns 0x004a2cb0's EAX for an invalid slot.
uint32_t fn_004a2d40(uint32_t slot, uint32_t wname)
{
    const uint32_t r = fn_004a2cb0();
    if (slot == 0 || slot >= 7)
        return r;
    uint8_t* g = Gm();
    Field<uint32_t>(g, 0x4cc) = slot;
    if (wname != 0)
    {
        orig::fn_00403be0(wname, u(g + 0x4d0), 0);
    }
    else
    {
        orig::fn_004a2c10(slot, u(g + 0x4d0));
        g = Gm();
    }
    const uint8_t f = Field<uint8_t>(g, 0x568);
    if ((f & 4) != 0)
    {
        Field<uint8_t>(g, 0x568) = static_cast<uint8_t>(f | 0x10);
        return fn_004a3930(0);
    }
    Field<uint32_t>(g, 0x56c) = 0;
    Field<uint8_t>(g, 0x568) |= 0x18;
    orig::fn_00403be0(u(g + 0x4d0), u(g + 0x510), 0);
    return fn_004a3930(0);
}
static uint32_t fn_004a2d40_Abi(uint32_t eax, uint32_t ebx)
{
    return fn_004a2d40(eax, ebx);
}
ABI_ENTRY_004a2d40(fn_004a2d40_Entry, fn_004a2d40_Abi)
HOOK(0x004a2d40, "fn_004a2d40", fn_004a2d40_Entry, nullptr);

// 0x004a2de0 (esi = profile/slot object or 0): when the object's vt+0x150 result has bit 11 set, selects its slot
// (0x004a2d40: eax = +0x4fc, ebx = object+0x500); otherwise resets (0x004a2cb0, tail jump). Caller 0x004a3bf0.
uint32_t fn_004a2de0(uint8_t* obj)
{
    if (obj != nullptr && (VCall<uint32_t>(obj, 0x150 / 4) & 0x800) != 0)
        return fn_004a2d40(Field<uint32_t>(obj, 0x4fc), u(obj + 0x500));
    return fn_004a2cb0();
}
static uint32_t fn_004a2de0_Abi(uint32_t esi)
{
    return fn_004a2de0(B(esi));
}
ABI_ENTRY_004a2de0(fn_004a2de0_Entry, fn_004a2de0_Abi)
HOOK(0x004a2de0, "fn_004a2de0", fn_004a2de0_Entry, nullptr);

// 0x004a2e40 (esi = save slot object): deletes the slot's files (0x0048b500, ecx = object+0x500), 0x004a3020(object,
// byte +0x4fa); when +0x4fa is set, marks +0x4fb and calls vt+0x20(object) on the UI's top screen
// (Ui_TopScreenField4 0x004907b0, eax = game+0xab4). Caller 0x004a2e29, inside an untracked function at 0x004a2e10
// (closes the top UI screen 0x00490b60, then this with esi = its 2nd stack argument when set; AL = 1). Not executed
// in recorded runs.
uint32_t fn_004a2e40(uint8_t* obj)
{
    fn_0048b500(u(obj + 0x500));
    const uint32_t r = orig::fn_004a3020(u(obj), Field<uint8_t>(obj, 0x4fa));
    if (Field<uint8_t>(obj, 0x4fa) == 0)
        return Lo8(r, 0);
    Field<uint8_t>(obj, 0x4fb) = 1;
    void* top = reinterpret_cast<void*>(orig::fn_004907b0(Field<uint32_t>(Gm(), 0xab4)));
    return VCall<uint32_t>(top, 0x20 / 4, u(obj));
}
static uint32_t fn_004a2e40_Abi(uint32_t esi)
{
    return fn_004a2e40(B(esi));
}
ABI_ENTRY_004a2e40(fn_004a2e40_Entry, fn_004a2e40_Abi)
HOOK(0x004a2e40, "fn_004a2e40", fn_004a2e40_Entry, nullptr);

// 0x004a2e90 (esi = wide output or 0; stack: byte validate; caller pops): AL = 1 when a quick save exists (flag 8
// and a nonzero saves counter) and, with validate, its slot passes SaveGame_CheckSlot (eax = quick-save name, ecx =
// game+0xab8); copies the quick-save name to the output (0x00403be0). Callers 0x00491b70, 0x004a2ef0.
uint32_t QuickSave_Available(uint32_t out, uint32_t validate)
{
    uint8_t* g = Gm();
    uint32_t r = Field<uint8_t>(g, 0x568);
    if ((Field<uint8_t>(g, 0x568) & 8) == 0)
        return Lo8(r, 0);
    r = Field<uint32_t>(g, 0x56c);
    if (r == 0)
        return 0;
    if (static_cast<uint8_t>(validate) != 0)
    {
        r = SaveGame_CheckSlot(u(g + 0x510), Field<uint8_t*>(g, 0xab8));
        if (static_cast<uint8_t>(r) == 0)
            return Lo8(r, 0);
        g = Gm();
    }
    if (out != 0)
        r = orig::fn_00403be0(u(g + 0x510), out, 0);
    return Lo8(r, 1);
}
static uint32_t QuickSave_Available_Abi(uint32_t esi, uint32_t a0)
{
    return QuickSave_Available(esi, a0);
}
ABI_ENTRY_004a2e90(QuickSave_Available_Entry, QuickSave_Available_Abi)
HOOK(0x004a2e90, "QuickSave_Available", QuickSave_Available_Entry, nullptr);

// 0x004a2ef0: quick load: when a valid quick save exists (0x004a2e90 with validation, name copied after a '-'),
// passes L"-<name>" to 0x00490810 (eax = UI, edx = string) and pushes the "Loading" screen (UI_PushScreen eax = 0);
// otherwise shows the "ui_notify_badQuickload" notification (UI_PushNotify: ecx = UI, edx = 0; 0, 0, 0). Caller
// 0x004921a0. Not executed in recorded runs.
uint32_t QuickLoad_Start()
{
    uint16_t buf[0x100] = {};
    if (static_cast<uint8_t>(QuickSave_Available(u(buf + 1), 1)) != 0)
    {
        uint8_t* g = Gm();
        buf[0] = 0x2d;
        orig::fn_00490810(Field<uint32_t>(g, 0xab4), u(buf));
        return orig::fn_00490b00(0, Field<uint32_t>(g, 0xab4), 0x005b252c);
    }
    return orig::fn_00490970(0x0058e8d4, Field<uint32_t>(Gm(), 0xab4), 0, 0, 0, 0);
}
static uint32_t QuickLoad_Start_Abi()
{
    return QuickLoad_Start();
}
ABI_ENTRY_004a2ef0(QuickLoad_Start_Entry, QuickLoad_Start_Abi)
HOOK(0x004a2ef0, "QuickLoad_Start", QuickLoad_Start_Entry, nullptr);

// 0x004a3840 (eax = wide slot name or 0 (then the current slot game+0x4d0, nothing without a slot); ecx only a
// `push ecx` local): when progress saving is on (flag 0x10), writes the slot's "progress.dat" (0x0048b0a0 mode 1):
// magic 0xfecd0001 (0x0057e870), game+0x550, +0x1fc, the 0x80 bytes +0x28c, +0x200, the 0x80 bytes +0x30c.
// Callers 0x004a07a0, 0x004a3a80. Returns the slot name argument when nothing is written.
uint32_t fn_004a3840(uint32_t name)
{
    uint8_t* g = Gm();
    if ((Field<uint8_t>(g, 0x568) & 0x10) == 0)
        return name;
    if (name == 0)
    {
        if (Field<uint32_t>(g, 0x4cc) == 0)
            return 0;
        name = u(g + 0x4d0);
    }
    const uint32_t s = orig::fn_0048b0a0(name, 0x0058e7b8, 1);
    if (s == 0)
        return 0;
    if (static_cast<uint8_t>(orig::fn_0048aac0(s)) != 0)
    {
        orig::fn_0048ac90(4, s, 0x0057e870);
        uint32_t v = Field<uint32_t>(Gm(), 0x550);
        orig::fn_0048ac90(4, s, u(&v));
        orig::fn_0048ac90(4, s, u(Gm() + 0x1fc));
        orig::fn_0048ac90(0x80, s, u(Gm() + 0x28c));
        orig::fn_0048ac90(4, s, u(Gm() + 0x200));
        orig::fn_0048ac90(0x80, s, u(Gm() + 0x30c));
        orig::fn_0048aff0(s, 0);
    }
    return orig::fn_0048b130(s);
}
static uint32_t fn_004a3840_Abi(uint32_t eax, uint32_t /*ecx*/)
{
    return fn_004a3840(eax);
}
ABI_ENTRY_004a3840(fn_004a3840_Entry, fn_004a3840_Abi)
HOOK(0x004a3840, "fn_004a3840", fn_004a3840_Entry, nullptr);

// 0x004a3930 (eax = wide slot name or 0 = current slot): clears progress (+0x1fc, +0x200), 0x00401f60 (eax = 1;
// game), then reads the slot's "progress.dat": magic 0xfecd0001, a value passed to 0x00401f60 (eax = value; game),
// +0x1fc, +0x28c (0x80), +0x200, +0x30c (0x80). AL = 1 when read. Callers 0x0048c9b0, 0x004a2cb0, 0x004a2d40,
// 0x004a3020.
uint32_t fn_004a3930(uint32_t name)
{
    uint8_t ok = 0;
    uint8_t* g = Gm();
    Field<uint32_t>(g, 0x1fc) = 0;
    Field<uint32_t>(g, 0x200) = 0;
    const uint32_t r = orig::fn_00401f60(1, u(g));
    if (name == 0)
    {
        if (Field<uint32_t>(Gm(), 0x4cc) == 0)
            return Lo8(r, 0);
        name = u(Gm() + 0x4d0);
    }
    const uint32_t s = orig::fn_0048b0a0(name, 0x0058e7b8, 0);
    if (s == 0)
        return 0;
    if (static_cast<uint8_t>(orig::fn_0048aac0(s)) != 0)
    {
        uint32_t magic = 0;
        uint32_t v = 0;
        orig::fn_0048af10(s, u(&magic), 4);
        if (magic == 0xfecd0001)
        {
            orig::fn_0048af10(s, u(&v), 4);
            orig::fn_00401f60(v, u(Gm()));
            orig::fn_0048af10(s, u(Gm() + 0x1fc), 4);
            orig::fn_0048af10(s, u(Gm() + 0x28c), 0x80);
            orig::fn_0048af10(s, u(Gm() + 0x200), 4);
            orig::fn_0048af10(s, u(Gm() + 0x30c), 0x80);
            ok = 1;
        }
        orig::fn_0048aff0(s, 0);
    }
    return Lo8(orig::fn_0048b130(s), ok);
}
static uint32_t fn_004a3930_Abi(uint32_t eax)
{
    return fn_004a3930(eax);
}
ABI_ENTRY_004a3930(fn_004a3930_Entry, fn_004a3930_Abi)
HOOK(0x004a3930, "fn_004a3930", fn_004a3930_Entry, nullptr);
