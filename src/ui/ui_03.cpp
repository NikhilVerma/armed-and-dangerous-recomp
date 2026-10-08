// Recovered: UI / HUD batch ui_03 (0x004907c0-0x004a3bf0). Static recovery 2026-10-05, compile-checked only (the game
// was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: as src/ui/ui_01.cpp. Every function's comment gives its address, its register/stack ABI (as the original
// reads it), what it does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay
// fn_XXXXXXXX unless the behaviour was verified from the disassembly or the tracker already had one. g = the Game object
// (0x005fe490).
//
// The screen manager (called "mgr" below; g+0xa6c per its callers, guess) keeps: +0 transition time (float), +4 the
// pending-request list (nodes of 0x1c bytes: +0 next, +4 prev, +8 kind 1..5, +0xc/+0x10 fade-in/out durations, +0x14
// the found screen, +0x18 argument), +8 the free request list, +0xec input bits, +0xf0 flags, +0xf4 the active screen
// stack (screens linked by +4, name at +0xc), +0xf8 the registered screens, +0xfc a UTF-16 level name, +0x2fc a
// 0x24-byte renderer resource, +0x320/+0x324 the cursor position, +0x328/+0x32c the cursor in 0..1, +0x330 six key
// repeat timers, +0x348.. the YesNo/Notify parameters.
#include <cstring>

#include <windows.h>

#include "game.h"
#include "game/game_object.h"
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
inline uint32_t fb(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
inline float bf(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
// A float constant of the game's .rdata.
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
inline uint8_t* G()
{
    return reinterpret_cast<uint8_t*>(g_game);
}
template <class Fn>
inline Fn Import(uint32_t iatSlot)
{
    return *reinterpret_cast<Fn*>(iatSlot);
}
// 0x005448ee _stricmp of the game's CRT (cdecl).
inline int GameStricmp(const char* a, const char* b)
{
    return GameFunction<int(__cdecl*)(const char*, const char*)>(0x005448ee)(a, b);
}
// The frame time g_frameDt (0x00604a90).
inline uint32_t FrameDtBits()
{
    return GameGlobal<uint32_t>(0x00604a90);
}

// Moves a request node (+0 next, +4 prev) of the screen manager to the head of its free list (mgr+8), in the original's
// order of memory accesses (inlined at 0x00490ec5, 0x00490fa6, 0x0049103c).
void Request_Free(uint8_t* mgr, uint8_t* node)
{
    uint8_t* next = Field<uint8_t*>(node, 0);
    if (next)
        Field<uint8_t*>(next, 4) = Field<uint8_t*>(node, 4);
    uint8_t* prev = Field<uint8_t*>(node, 4);
    if (prev)
        Field<uint8_t*>(prev, 0) = Field<uint8_t*>(node, 0);
    uint8_t* head = mgr + 8;
    Field<uint8_t*>(node, 4) = head;
    Field<uint32_t>(node, 0) = 0;
    Field<uint8_t*>(node, 0) = Field<uint8_t*>(head, 0);
    Field<uint8_t*>(head, 0) = node;
    if (Field<uint8_t*>(node, 0))
        Field<uint8_t*>(Field<uint8_t*>(node, 0), 4) = node;
}
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t fn_00490a40(uint32_t eax, uint8_t* mgr);
uint32_t fn_00490a80(uint8_t* screen, const char* name);
uint32_t fn_00490cb0(const char* name, uint8_t* mgr, uint32_t kind, uint32_t arg, uint32_t fadeIn, uint32_t fadeOut);
uint32_t fn_00490d80(uint8_t* mgr);
uint32_t fn_00491110(uint8_t* mgr);
uint32_t fn_00491b20(uint8_t* object, uint8_t* screen);
uint32_t fn_00491b60(uint8_t* object, uint32_t edx);
uint32_t fn_004919c0(uint8_t* screen, uint32_t select);
uint32_t fn_004929a0(uint8_t* screen, uint32_t edx);

// ================================================================================================================
// Screen manager
// ================================================================================================================

// 0x004907c0 (eax = mgr, ecx = 8-bit name): sets the UTF-16 level name at mgr+0xfc: with a non-empty name through
// 0x004e59d0 (eax = 0x100 characters, ecx = destination, edi = source; guess: multibyte to wide), else copies L"lev_shell"
// (0x0058ff70). Returns 0x004e59d0's result or the end of the copied string. Not executed in recorded runs.
uint32_t fn_004907c0(uint8_t* mgr, const char* name)
{
    if (name && *name)
        return orig::fn_004e59d0(0x100, u(mgr + 0xfc), u(name));
    uint16_t* d = reinterpret_cast<uint16_t*>(mgr + 0xfc);
    const uint16_t* s = reinterpret_cast<const uint16_t*>(0x0058ff70);
    while (*s)
        *d++ = *s++;
    *d = 0;
    return u(d);
}

static uint32_t fn_004907c0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004907c0(B(eax), reinterpret_cast<const char*>(ecx));
}
ABI_ENTRY_004907c0(fn_004907c0_Entry, fn_004907c0_Abi)
HOOK(0x004907c0, "fn_004907c0", fn_004907c0_Entry, nullptr);

// 0x00490810 (eax = mgr, edx = UTF-16 name): copies the name (L"lev_shell" 0x0058ff70 when null or empty) to mgr+0xfc.
// Returns the end of the copied string. Not executed in recorded runs.
uint32_t fn_00490810(uint8_t* mgr, const uint16_t* name)
{
    if (!name || !*name)
        name = reinterpret_cast<const uint16_t*>(0x0058ff70);
    uint16_t* d = reinterpret_cast<uint16_t*>(mgr + 0xfc);
    while (*name)
        *d++ = *name++;
    *d = 0;
    return u(d);
}

static uint32_t fn_00490810_Abi(uint32_t eax, uint32_t edx)
{
    return fn_00490810(B(eax), reinterpret_cast<const uint16_t*>(edx));
}
ABI_ENTRY_00490810(fn_00490810_Entry, fn_00490810_Abi)
HOOK(0x00490810, "fn_00490810", fn_00490810_Entry, nullptr);

// 0x00490850 (eax = UTF-16 text, ecx = mgr): with a non-empty text, mgr+0xfc = L'-' and the text goes to mgr+0xfe through
// 0x00403be0 (eax = text, edx = destination, edi = 0). Returns 0x00403be0's result, else eax.
uint32_t fn_00490850(const uint16_t* text, uint8_t* mgr)
{
    if (text && *text)
    {
        Field<uint16_t>(mgr, 0xfc) = 0x2d;
        return orig::fn_00403be0(u(text), u(mgr + 0xfe), 0);
    }
    return u(text);
}

static uint32_t fn_00490850_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00490850(reinterpret_cast<const uint16_t*>(eax), B(ecx));
}
ABI_ENTRY_00490850(fn_00490850_Entry, fn_00490850_Abi)
HOOK(0x00490850, "fn_00490850", fn_00490850_Entry, nullptr);

// 0x00490880 (al = value, ecx = mgr): sets flag bit 1 of mgr+0xf0 to al's bit 0. Returns eax with al = the changed bit.
// Not executed in recorded runs.
uint32_t fn_00490880(uint32_t eax, uint8_t* mgr)
{
    const uint8_t dl = Field<uint8_t>(mgr, 0xf0);
    const uint8_t al = static_cast<uint8_t>((static_cast<uint8_t>(eax << 1) ^ dl) & 2);
    Field<uint8_t>(mgr, 0xf0) = static_cast<uint8_t>(dl ^ al);
    return (eax & 0xffffff00u) | al;
}

static uint32_t fn_00490880_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00490880(eax, B(ecx));
}
ABI_ENTRY_00490880(fn_00490880_Entry, fn_00490880_Abi)
HOOK(0x00490880, "fn_00490880", fn_00490880_Entry, nullptr);

// 0x004908a0 (eax = mgr, cl = on): sets (cl != 0) or clears flag bit 0 of mgr+0xf0, then tells the top screen (+0xf4)
// through virtual +0xc: (0, 1) when set, (1, 1) when cleared. Returns that result, 0 without a screen. guess: pause.
// Not executed in recorded runs.
uint32_t fn_004908a0(uint8_t* mgr, uint32_t ecx)
{
    if (static_cast<uint8_t>(ecx))
    {
        Field<uint8_t>(mgr, 0xf0) |= 1;
        void* top = Field<void*>(mgr, 0xf4);
        if (!top)
            return 0;
        return VCall<uint32_t>(top, 0xc / 4, 0u, 1u);
    }
    Field<uint8_t>(mgr, 0xf0) &= 0xfe;
    void* top = Field<void*>(mgr, 0xf4);
    if (!top)
        return 0;
    return VCall<uint32_t>(top, 0xc / 4, 1u, 1u);
}

static uint32_t fn_004908a0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004908a0(B(eax), ecx);
}
ABI_ENTRY_004908a0(fn_004908a0_Entry, fn_004908a0_Abi)
HOOK(0x004908a0, "fn_004908a0", fn_004908a0_Entry, nullptr);

// 0x004908f0 (eax = mgr; stack: one argument, callee pops 4): tail-jumps to the top screen's (+0xf4) virtual +0x10 with
// the argument (here a call then return); returns eax (mgr) without a screen. Not executed in recorded runs.
uint32_t fn_004908f0(uint8_t* mgr, uint32_t arg0)
{
    void* top = Field<void*>(mgr, 0xf4);
    if (top)
        return VCall<uint32_t>(top, 0x10 / 4, arg0);
    return u(mgr);
}

static uint32_t fn_004908f0_Abi(uint32_t eax, uint32_t a0)
{
    return fn_004908f0(B(eax), a0);
}
ABI_ENTRY_004908f0(fn_004908f0_Entry, fn_004908f0_Abi)
HOOK(0x004908f0, "fn_004908f0", fn_004908f0_Entry, nullptr);

uint32_t UI_PushScreen(uint32_t eax, uint8_t* mgr, uint32_t name);
uint32_t fn_00490b20(uint32_t eax, uint8_t* mgr, uint32_t name);

// 0x00490910 UI_PushYesNo (eax = argument, ecx = mgr, edx = text?; stack: push (byte), 5 parameters; callee pops 0x18):
// stores edx and stack arguments 2..6 at mgr+0x348..+0x35c, then opens "YesNo" (0x0058ffbc) with UI_PushScreen when the
// first stack argument is set, else 0x00490b20. Name from the tracker; string evidence.
uint32_t UI_PushYesNo(uint32_t eax, uint8_t* mgr, uint32_t edx, uint32_t push, uint32_t a1, uint32_t a2, uint32_t a3,
                      uint32_t a4, uint32_t a5)
{
    Field<uint32_t>(mgr, 0x348) = edx;
    Field<uint32_t>(mgr, 0x34c) = a1;
    Field<uint32_t>(mgr, 0x350) = a2;
    Field<uint32_t>(mgr, 0x354) = a3;
    Field<uint32_t>(mgr, 0x358) = a4;
    Field<uint32_t>(mgr, 0x35c) = a5;
    if (static_cast<uint8_t>(push))
        return UI_PushScreen(eax, mgr, 0x0058ffbc);
    return fn_00490b20(eax, mgr, 0x0058ffbc);
}

static uint32_t UI_PushYesNo_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2,
                                 uint32_t a3, uint32_t a4, uint32_t a5)
{
    return UI_PushYesNo(eax, B(ecx), edx, a0, a1, a2, a3, a4, a5);
}
ABI_ENTRY_00490910(UI_PushYesNo_Entry, UI_PushYesNo_Abi)
HOOK(0x00490910, "UI_PushYesNo", UI_PushYesNo_Entry, nullptr);

// 0x00490970 UI_PushNotify (eax = argument, ecx = mgr, edx = text?; stack: push (byte), 2 parameters; callee pops 0xc):
// mgr+0x348 = edx, +0x34c/+0x350 = stack arguments 2/3, +0x354..+0x35c = 0; opens "Notify" (0x0058ffa8) with
// 0x00490b20 when the first argument is 0 and a screen is active (+0xf4), else UI_PushScreen. Name from the tracker.
// Not executed in recorded runs.
uint32_t UI_PushNotify(uint32_t eax, uint8_t* mgr, uint32_t edx, uint32_t push, uint32_t a1, uint32_t a2)
{
    Field<uint32_t>(mgr, 0x348) = edx;
    Field<uint32_t>(mgr, 0x34c) = a1;
    Field<uint32_t>(mgr, 0x350) = a2;
    Field<uint32_t>(mgr, 0x354) = 0;
    Field<uint32_t>(mgr, 0x358) = 0;
    Field<uint32_t>(mgr, 0x35c) = 0;
    if (static_cast<uint8_t>(push) == 0 && Field<uint32_t>(mgr, 0xf4) != 0)
        return fn_00490b20(eax, mgr, 0x0058ffa8);
    return UI_PushScreen(eax, mgr, 0x0058ffa8);
}

static uint32_t UI_PushNotify_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return UI_PushNotify(eax, B(ecx), edx, a0, a1, a2);
}
ABI_ENTRY_00490970(UI_PushNotify_Entry, UI_PushNotify_Abi)
HOOK(0x00490970, "UI_PushNotify", UI_PushNotify_Entry, nullptr);

// 0x004909d0 (eax = mgr, cl = force): &mgr+0x2fc when its word +0x31e is non-zero or cl is set, else 0. Not executed
// in recorded runs.
uint32_t fn_004909d0(uint8_t* mgr, uint32_t ecx)
{
    if (Field<uint16_t>(mgr, 0x31e) == 0 && static_cast<uint8_t>(ecx) == 0)
        return 0;
    return u(mgr + 0x2fc);
}

static uint32_t fn_004909d0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004909d0(B(eax), ecx);
}
ABI_ENTRY_004909d0(fn_004909d0_Entry, fn_004909d0_Abi)
HOOK(0x004909d0, "fn_004909d0", fn_004909d0_Entry, nullptr);

// 0x004909f0 (eax = mgr): when word +0x31e is set, releases the resource at +0x2fc through the renderer (g+0x38)
// virtual +0x128 (resource, 1) and clears its 9 dwords. Returns 0, or eax when nothing was held. Not executed in
// recorded runs.
uint32_t fn_004909f0(uint8_t* mgr)
{
    if (Field<uint16_t>(mgr, 0x31e) == 0)
        return u(mgr);
    VCall<uint32_t>(Field<void*>(G(), 0x38), 0x128 / 4, u(mgr + 0x2fc), 1u);
    std::memset(mgr + 0x2fc, 0, 9 * 4);
    return 0;
}

static uint32_t fn_004909f0_Abi(uint32_t eax)
{
    return fn_004909f0(B(eax));
}
ABI_ENTRY_004909f0(fn_004909f0_Entry, fn_004909f0_Abi)
HOOK(0x004909f0, "fn_004909f0", fn_004909f0_Entry, nullptr);

// 0x00490a20 (eax = mgr): flag bit 3 of mgr+0xf0 (al). Upper bytes of eax keep the input's.
uint32_t fn_00490a20(uint32_t eax)
{
    return (eax & 0xffffff00u) | ((Field<uint8_t>(B(eax), 0xf0) >> 3) & 1u);
}

static uint32_t fn_00490a20_Abi(uint32_t eax)
{
    return fn_00490a20(eax);
}
ABI_ENTRY_00490a20(fn_00490a20_Entry, fn_00490a20_Abi)
HOOK(0x00490a20, "fn_00490a20", fn_00490a20_Entry, nullptr);

// 0x00490a30 (eax = mgr): flag bit 2 of mgr+0xf0 (al). Upper bytes of eax keep the input's.
uint32_t fn_00490a30(uint32_t eax)
{
    return (eax & 0xffffff00u) | ((Field<uint8_t>(B(eax), 0xf0) >> 2) & 1u);
}

static uint32_t fn_00490a30_Abi(uint32_t eax)
{
    return fn_00490a30(eax);
}
ABI_ENTRY_00490a30(fn_00490a30_Entry, fn_00490a30_Abi)
HOOK(0x00490a30, "fn_00490a30", fn_00490a30_Entry, nullptr);

// 0x00490a40 (al = on, esi = mgr): when al differs from flag bit 3 of mgr+0xf0, sets the bit and calls 0x004031e0
// (bl = 1, edi = g; stack: the bit with eax's upper bytes). Returns its result, else eax. (The caller's upper ebx bytes
// reach 0x004031e0 in the original; passed here as 0 with bl = 1.)
uint32_t fn_00490a40(uint32_t eax, uint8_t* mgr)
{
    const uint8_t cl = Field<uint8_t>(mgr, 0xf0);
    if (static_cast<uint8_t>(eax) == ((cl >> 3) & 1))
        return eax;
    uint8_t al = static_cast<uint8_t>(static_cast<uint8_t>(eax << 3) ^ cl);
    al = static_cast<uint8_t>((al & 8) ^ cl);
    Field<uint8_t>(mgr, 0xf0) = al;
    al = static_cast<uint8_t>(al >> 3);
    return orig::fn_004031e0(1, u(G()), (eax & 0xffffff00u) | (al & 1u));
}

static uint32_t fn_00490a40_Abi(uint32_t eax, uint32_t esi)
{
    return fn_00490a40(eax, B(esi));
}
ABI_ENTRY_00490a40(fn_00490a40_Entry, fn_00490a40_Abi)
HOOK(0x00490a40, "fn_00490a40", fn_00490a40_Entry, nullptr);

// 0x00490a80 (eax = first screen, edi = name; ecx unused; stack: one unused argument, callee pops 4): walks the screen
// list linked by +4 and returns the first whose name (+0xc) equals the name (_stricmp), or 0.
uint32_t fn_00490a80(uint8_t* screen, const char* name)
{
    while (screen && GameStricmp(name, reinterpret_cast<const char*>(screen + 0xc)) != 0)
        screen = Field<uint8_t*>(screen, 4);
    return u(screen);
}

static uint32_t fn_00490a80_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t edi, uint32_t /*a0*/)
{
    return fn_00490a80(B(eax), reinterpret_cast<const char*>(edi));
}
ABI_ENTRY_00490a80(fn_00490a80_Entry, fn_00490a80_Abi)
HOOK(0x00490a80, "fn_00490a80", fn_00490a80_Entry, nullptr);

// 0x00490ab0 (eax = name, ecx = mgr): true (al = 1) when the named screen is on the active stack (+0xf4) or is the
// screen (+0x14) of a pending request (+4 list); else al = 0. Not executed in recorded runs.
uint32_t fn_00490ab0(const char* name, uint8_t* mgr)
{
    const uint32_t found = fn_00490a80(Field<uint8_t*>(mgr, 0xf4), name);
    if (found)
        return (found & 0xffffff00u) | 1;
    for (uint8_t* node = Field<uint8_t*>(mgr, 4); node; node = Field<uint8_t*>(node, 0))
    {
        uint8_t* screen = Field<uint8_t*>(node, 0x14);
        if (screen)
        {
            const uint32_t r = static_cast<uint32_t>(GameStricmp(name, reinterpret_cast<const char*>(screen + 0xc)));
            if (r == 0)
                return 1;
        }
    }
    return 0;
}

static uint32_t fn_00490ab0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00490ab0(reinterpret_cast<const char*>(eax), B(ecx));
}
ABI_ENTRY_00490ab0(fn_00490ab0_Entry, fn_00490ab0_Abi)
HOOK(0x00490ab0, "fn_00490ab0", fn_00490ab0_Entry, nullptr);

// 0x00490b00 UI_PushScreen (eax = argument, ecx = mgr; stack: name, callee pops 4): queues request kind 1 (push) of the
// named screen through 0x00490cb0 (fade-in 0.5, fade-out 0.25). Name from the tracker.
uint32_t UI_PushScreen(uint32_t eax, uint8_t* mgr, uint32_t name)
{
    return fn_00490cb0(reinterpret_cast<const char*>(name), mgr, 1, eax, 0x3f000000u, 0x3e800000u);
}

static uint32_t UI_PushScreen_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return UI_PushScreen(eax, B(ecx), a0);
}
ABI_ENTRY_00490b00(UI_PushScreen_Entry, UI_PushScreen_Abi)
HOOK(0x00490b00, "UI_PushScreen", UI_PushScreen_Entry, nullptr);

// 0x00490b20 (eax = argument, ecx = mgr; stack: name, callee pops 4): queues request kind 5 (pop the top, then push the
// named screen) through 0x00490cb0 (0.5, 0.25). Not executed in recorded runs.
uint32_t fn_00490b20(uint32_t eax, uint8_t* mgr, uint32_t name)
{
    return fn_00490cb0(reinterpret_cast<const char*>(name), mgr, 5, eax, 0x3f000000u, 0x3e800000u);
}

static uint32_t fn_00490b20_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_00490b20(eax, B(ecx), a0);
}
ABI_ENTRY_00490b20(fn_00490b20_Entry, fn_00490b20_Abi)
HOOK(0x00490b20, "fn_00490b20", fn_00490b20_Entry, nullptr);

// 0x00490b40 (ecx = mgr): queues request kind 3 (pop all screens) through 0x00490cb0 (no name, 0.5, 0.25).
uint32_t fn_00490b40(uint8_t* mgr)
{
    return fn_00490cb0(nullptr, mgr, 3, 0, 0x3f000000u, 0x3e800000u);
}

static uint32_t fn_00490b40_Abi(uint32_t ecx)
{
    return fn_00490b40(B(ecx));
}
ABI_ENTRY_00490b40(fn_00490b40_Entry, fn_00490b40_Abi)
HOOK(0x00490b40, "fn_00490b40", fn_00490b40_Entry, nullptr);

// 0x00490b60 (ecx = mgr): queues request kind 2 (pop the top screen) through 0x00490cb0 (no name, 0.5, 0.25).
uint32_t fn_00490b60(uint8_t* mgr)
{
    return fn_00490cb0(nullptr, mgr, 2, 0, 0x3f000000u, 0x3e800000u);
}

static uint32_t fn_00490b60_Abi(uint32_t ecx)
{
    return fn_00490b60(B(ecx));
}
ABI_ENTRY_00490b60(fn_00490b60_Entry, fn_00490b60_Abi)
HOOK(0x00490b60, "fn_00490b60", fn_00490b60_Entry, nullptr);

// 0x00490b80 (eax = name, ecx = mgr): queues request kind 4 (pop down to the named screen) through 0x00490cb0 (0.5,
// 0.25). Not executed in recorded runs.
uint32_t fn_00490b80(const char* name, uint8_t* mgr)
{
    return fn_00490cb0(name, mgr, 4, 0, 0x3f000000u, 0x3e800000u);
}

static uint32_t fn_00490b80_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00490b80(reinterpret_cast<const char*>(eax), B(ecx));
}
ABI_ENTRY_00490b80(fn_00490b80_Entry, fn_00490b80_Abi)
HOOK(0x00490b80, "fn_00490b80", fn_00490b80_Entry, nullptr);

// 0x00490ba0 (ebx = mgr; stack: screen, argument; callee pops 8): pushes a screen. When the screen (by name +0xc) is
// already on the stack (+0xf4), returns it. Otherwise the current top gets virtual +0xc (0, 1); 0x004929a0 (eax =
// screen, edx = &mgr+0xf4) links it on top, 0x00491110 resets the key timers, the new top gets virtual +8 (argument, 1)
// and flag bit 2 of mgr+0xf0 = bit 2 of the top's +0x4c. Returns al = the new flags (upper bytes from virtual +8).
uint32_t fn_00490ba0(uint8_t* mgr, uint8_t* screen, uint32_t arg)
{
    uint32_t* top = reinterpret_cast<uint32_t*>(mgr + 0xf4);
    if (*top)
    {
        const uint32_t found = fn_00490a80(B(*top), reinterpret_cast<const char*>(screen + 0xc));
        if (found)
            return found;
        VCall<uint32_t>(reinterpret_cast<void*>(*top), 0xc / 4, 0u, 1u);
    }
    fn_004929a0(screen, u(top));
    fn_00491110(mgr);
    const uint32_t r = VCall<uint32_t>(reinterpret_cast<void*>(*top), 8 / 4, arg, 1u);
    const uint32_t flags = Field<uint32_t>(reinterpret_cast<void*>(*top), 0x4c);
    uint8_t al = Field<uint8_t>(mgr, 0xf0);
    const uint8_t dl = static_cast<uint8_t>((static_cast<uint8_t>((flags >> 2) << 2) ^ al) & 4);
    al = static_cast<uint8_t>(al ^ dl);
    Field<uint8_t>(mgr, 0xf0) = al;
    return (r & 0xffffff00u) | al;
}

static uint32_t fn_00490ba0_Abi(uint32_t ebx, uint32_t a0, uint32_t a1)
{
    return fn_00490ba0(B(ebx), B(a0), a1);
}
ABI_ENTRY_00490ba0(fn_00490ba0_Entry, fn_00490ba0_Abi)
HOOK(0x00490ba0, "fn_00490ba0", fn_00490ba0_Entry, nullptr);

// 0x00490c20 (esi = mgr; stack: the screen that becomes the top, callee pops 4): pops the top screen: virtual +8 (0, 0),
// 0x004929a0 (eax = top, edx = &mgr+0xf8) moves it to the registered list, 0x00491110 resets the key timers; the new
// top gets virtual +0xc (1, top == argument; the pushed dword keeps the argument's upper bytes), or without one
// (g+0xa70)+0x50 = 0. Then flag bit 2 of mgr+0xf0 = (no top or top+0x4c bit 2). Returns eax with al = the changed bit.
uint32_t fn_00490c20(uint8_t* mgr, uint32_t arg0)
{
    void* top = Field<void*>(mgr, 0xf4);
    if (top)
    {
        VCall<uint32_t>(top, 8 / 4, 0u, 0u);
        fn_004929a0(Field<uint8_t*>(mgr, 0xf4), u(mgr + 0xf8));
        fn_00491110(mgr);
        void* next = Field<void*>(mgr, 0xf4);
        if (next)
            VCall<uint32_t>(next, 0xc / 4, 1u, (arg0 & 0xffffff00u) | (u(next) == arg0 ? 1u : 0u));
        else
            Field<uint8_t>(Field<void*>(G(), 0xa70), 0x50) = 0;
    }
    const uint32_t t = Field<uint32_t>(mgr, 0xf4);
    uint8_t al;
    if (t == 0 || (Field<uint8_t>(B(t), 0x4c) & 4) != 0)
        al = 1;
    else
        al = 0;
    const uint8_t cl = Field<uint8_t>(mgr, 0xf0);
    al = static_cast<uint8_t>((static_cast<uint8_t>(al << 2) ^ cl) & 4);
    Field<uint8_t>(mgr, 0xf0) = static_cast<uint8_t>(cl ^ al);
    return (t & 0xffffff00u) | al;
}

static uint32_t fn_00490c20_Abi(uint32_t esi, uint32_t a0)
{
    return fn_00490c20(B(esi), a0);
}
ABI_ENTRY_00490c20(fn_00490c20_Entry, fn_00490c20_Abi)
HOOK(0x00490c20, "fn_00490c20", fn_00490c20_Entry, nullptr);

// 0x00490cb0 (eax = name or 0, ecx = mgr, ebx = kind; stack: argument, fade-in, fade-out (float bits); callee pops 0xc):
// queues a screen request. When a free node exists (+8): the named screen is looked up among the registered (+0xf8)
// then the active (+0xf4) screens, the node moves from the free list to the tail of the pending list (+4) and gets
// +0x14 = screen, +0x18 = argument, +8 = kind, +0x10 = fade-out, +0xc = fade-in; 0x00414580 (eax = g+0xa80; guess:
// flush input); with a kind, 0x00490a40 (al = 1, esi = mgr); returns 0x00491110. Returns 0 without a free node.
// (0x00490a40 gets eax's upper bytes from 0x00414580, which returns nothing: passed here as 0.)
uint32_t fn_00490cb0(const char* name, uint8_t* mgr, uint32_t kind, uint32_t arg, uint32_t fadeIn, uint32_t fadeOut)
{
    if (Field<uint32_t>(mgr, 8) == 0)
        return 0;
    uint32_t screen = 0;
    if (name && *name)
    {
        screen = fn_00490a80(Field<uint8_t*>(mgr, 0xf8), name);
        if (!screen)
            screen = fn_00490a80(Field<uint8_t*>(mgr, 0xf4), name);
    }
    uint8_t* tail = mgr + 4;
    for (uint8_t* p = Field<uint8_t*>(mgr, 4); p; p = Field<uint8_t*>(p, 0))
        tail = p;
    uint8_t* node = Field<uint8_t*>(mgr, 8);
    if (Field<uint8_t*>(node, 0))
        Field<uint8_t*>(Field<uint8_t*>(node, 0), 4) = Field<uint8_t*>(node, 4);
    if (Field<uint8_t*>(node, 4))
        Field<uint8_t*>(Field<uint8_t*>(node, 4), 0) = Field<uint8_t*>(node, 0);
    Field<uint32_t>(node, 0) = 0;
    Field<uint8_t*>(node, 4) = tail;
    Field<uint8_t*>(node, 0) = Field<uint8_t*>(tail, 0);
    Field<uint8_t*>(tail, 0) = node;
    if (Field<uint8_t*>(node, 0))
        Field<uint8_t*>(Field<uint8_t*>(node, 0), 4) = node;
    Field<uint32_t>(node, 0x14) = screen;
    Field<uint32_t>(node, 0x18) = arg;
    Field<uint32_t>(node, 8) = kind;
    Field<uint32_t>(node, 0x10) = fadeOut;
    Field<uint32_t>(node, 0xc) = fadeIn;
    orig::fn_00414580(Field<uint32_t>(G(), 0xa80));
    if (kind)
        fn_00490a40(1, mgr);
    return fn_00491110(mgr);
}

static uint32_t fn_00490cb0_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00490cb0(reinterpret_cast<const char*>(eax), B(ecx), ebx, a0, a1, a2);
}
ABI_ENTRY_00490cb0(fn_00490cb0_Entry, fn_00490cb0_Abi)
HOOK(0x00490cb0, "fn_00490cb0", fn_00490cb0_Entry, nullptr);

// 0x00490d80 (eax = mgr): executes the first pending request (+4) by kind (jump table 0x00490e70): 1 push (0x00490ba0
// with +0x14 / +0x18), 2 pop (0x00490c20 with the second screen), 3 pop all, 4 pop down to +0x14, 5 pop then push;
// then clears the node's kind and screen and sets the transition time mgr+0 = 1.0. Then 0x00401e60 (esi = g) and
// 0x00490a40 (al = a screen is active or a request is still pending, esi = mgr). Returns 0x00490a40's result.
uint32_t fn_00490d80(uint8_t* mgr)
{
    uint8_t* node = Field<uint8_t*>(mgr, 4);
    if (node)
    {
        switch (Field<uint32_t>(node, 8))
        {
        case 5:
            fn_00490c20(mgr, Field<uint32_t>(node, 0x14));
            [[fallthrough]];
        case 1:
            fn_00490ba0(mgr, Field<uint8_t*>(Field<uint8_t*>(mgr, 4), 0x14),
                        Field<uint32_t>(Field<uint8_t*>(mgr, 4), 0x18));
            break;
        case 2:
            fn_00490c20(mgr, Field<uint32_t>(Field<uint8_t*>(mgr, 0xf4), 4));
            break;
        case 3:
            while (Field<uint32_t>(mgr, 0xf4))
                fn_00490c20(mgr, 0);
            break;
        case 4:
            while (Field<uint32_t>(mgr, 0xf4))
            {
                const uint32_t target = Field<uint32_t>(Field<uint8_t*>(mgr, 4), 0x14);
                if (Field<uint32_t>(mgr, 0xf4) == target)
                    break;
                fn_00490c20(mgr, target);
            }
            break;
        default:
            break;
        }
        Field<uint32_t>(Field<uint8_t*>(mgr, 4), 8) = 0;
        Field<uint32_t>(Field<uint8_t*>(mgr, 4), 0x14) = 0;
        Field<uint32_t>(mgr, 0) = 0x3f800000u;
    }
    orig::fn_00401e60(u(G()));
    const uint32_t top = Field<uint32_t>(mgr, 0xf4);
    if (top == 0 && Field<uint32_t>(Field<uint8_t*>(mgr, 4), 0) == 0)
        return fn_00490a40(0, mgr);
    return fn_00490a40((top & 0xffffff00u) | 1, mgr);
}

static uint32_t fn_00490d80_Abi(uint32_t eax)
{
    return fn_00490d80(B(eax));
}
ABI_ENTRY_00490d80(fn_00490d80_Entry, fn_00490d80_Abi)
HOOK(0x00490d80, "fn_00490d80", fn_00490d80_Entry, nullptr);

// 0x00490e90 (esi = mgr; stack: dt (float), callee pops 4): advances the screen transitions. While a request is pending
// (+4): kind 0 (finished) nodes with a successor are freed after 0x00490d80 (time = 0x005bcb80 - time; time below
// 0x005bcb2c first becomes 1.0); at time 0.0 with flag bit 2 and no new screen (or one with +0x4c bit 2) the request
// runs now (time = 2.0, return); below 0x005bcb2c the time advances by dt / fade-in (+0xc) and the request runs when it
// reaches 0x005bcb2c; at or above it the time advances by dt / fade-out (+0x10) and the node is freed (time = 0) when it
// reaches 0x005bcb80. Returns 1 when no request is pending.
uint32_t fn_00490e90(uint8_t* mgr, float dt)
{
    float& t = Field<float>(mgr, 0);
    while (Field<uint32_t>(mgr, 4))
    {
        uint8_t* node = Field<uint8_t*>(mgr, 4);
        if (Field<uint32_t>(node, 8) == 0)
        {
            if (t < K(0x005bcb2c))
                Field<uint32_t>(mgr, 0) = 0x3f800000u;
            if (Field<uint32_t>(node, 0))
            {
                Request_Free(mgr, node);
                fn_00490d80(mgr);
                t = K(0x005bcb80) - t;
                continue;
            }
        }
        if (t == K(0x0056c05c) && (Field<uint8_t>(mgr, 0xf0) & 4) != 0)
        {
            uint8_t* screen = Field<uint8_t*>(node, 0x14);
            if (!screen || (Field<uint8_t>(screen, 0x4c) & 4) != 0)
            {
                fn_00490d80(mgr);
                Field<uint32_t>(mgr, 0) = 0x40000000u;
                return Field<uint32_t>(mgr, 4) == 0 ? 1u : 0u;
            }
        }
        if (t < K(0x005bcb2c))
        {
            if (Field<float>(node, 0xc) != K(0x0056c05c))
            {
                t = dt / Field<float>(node, 0xc) + t;
                if (t < K(0x005bcb2c))
                    return Field<uint32_t>(mgr, 4) == 0 ? 1u : 0u;
            }
            fn_00490d80(mgr);
        }
        else
        {
            if (Field<float>(node, 0x10) != K(0x0056c05c))
            {
                t = dt / Field<float>(node, 0x10) + t;
                if (t < K(0x005bcb80))
                    return Field<uint32_t>(mgr, 4) == 0 ? 1u : 0u;
            }
            Request_Free(mgr, node);
            Field<uint32_t>(mgr, 0) = 0;
        }
    }
    return Field<uint32_t>(mgr, 4) == 0 ? 1u : 0u;
}

static uint32_t fn_00490e90_Abi(uint32_t esi, uint32_t a0)
{
    return fn_00490e90(B(esi), bf(a0));
}
ABI_ENTRY_00490e90(fn_00490e90_Entry, fn_00490e90_Abi)
HOOK(0x00490e90, "fn_00490e90", fn_00490e90_Entry, nullptr);

// 0x00491020 (esi = mgr): queues "pop all" (0x00490b40), then runs and frees every pending request (0x00490d80); time
// mgr+0 = 0, flags +0xf0: bit 3 cleared, bit 2 set. Returns 0. guess: immediate reset of the screen stack.
uint32_t fn_00491020(uint8_t* mgr)
{
    fn_00490b40(mgr);
    while (Field<uint32_t>(mgr, 4))
    {
        fn_00490d80(mgr);
        Request_Free(mgr, Field<uint8_t*>(mgr, 4));
    }
    const uint8_t dl = static_cast<uint8_t>((Field<uint8_t>(mgr, 0xf0) & 0xf7) | 4);
    Field<uint32_t>(mgr, 0) = 0;
    Field<uint8_t>(mgr, 0xf0) = dl;
    return 0;
}

static uint32_t fn_00491020_Abi(uint32_t esi)
{
    return fn_00491020(B(esi));
}
ABI_ENTRY_00491020(fn_00491020_Entry, fn_00491020_Abi)
HOOK(0x00491020, "fn_00491020", fn_00491020_Entry, nullptr);

// 0x00491090 (eax = key; stack: timer*, dt, threshold, first delay, repeat delay (floats); caller pops): key repeat.
// When 0x00414730 (al = 1, ebx = key, esi = g+0xa80; stack: threshold) reports the key (guess: newly pressed), the
// timer = first delay, true. Else when 0x00414810 (same; guess: held) reports it, the timer -= dt and when it reaches
// 0.0 or less it is reset to the repeat delay, true. Otherwise false (al).
uint32_t fn_00491090(uint32_t key, float* timer, float dt, uint32_t threshold, uint32_t first, uint32_t repeat)
{
    const uint32_t r = orig::fn_00414730((key & 0xffffff00u) | 1, key, Field<uint32_t>(G(), 0xa80), threshold);
    if (static_cast<uint8_t>(r))
    {
        *reinterpret_cast<uint32_t*>(timer) = first;
        return (r & 0xffffff00u) | 1;
    }
    const uint32_t h = orig::fn_00414810((r & 0xffffff00u) | 1, key, Field<uint32_t>(G(), 0xa80), threshold);
    if (static_cast<uint8_t>(h))
    {
        const float v = *timer - dt;
        *timer = v;
        if (v <= K(0x0056c05c))
        {
            *reinterpret_cast<uint32_t*>(timer) = repeat;
            return 1;
        }
    }
    return 0;
}

static uint32_t fn_00491090_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_00491090(eax, reinterpret_cast<float*>(a0), bf(a1), a2, a3, a4);
}
ABI_ENTRY_00491090(fn_00491090_Entry, fn_00491090_Abi)
HOOK(0x00491090, "fn_00491090", fn_00491090_Entry, nullptr);

// 0x00491110 (eax = mgr): sets the six key repeat timers mgr+0x330..+0x344 to 0x58635fa9 (a huge float) and, with an
// input object g+0xa80, tail-calls 0x00414680 (eax = it, ecx = 0x23). Returns its result or 0.
uint32_t fn_00491110(uint8_t* mgr)
{
    for (int i = 0; i < 6; ++i)
        Field<uint32_t>(mgr, 0x330 + 4 * i) = 0x58635fa9u;
    const uint32_t input = Field<uint32_t>(G(), 0xa80);
    if (input)
        return orig::fn_00414680(input, 0x23);
    return 0;
}

static uint32_t fn_00491110_Abi(uint32_t eax)
{
    return fn_00491110(B(eax));
}
ABI_ENTRY_00491110(fn_00491110_Entry, fn_00491110_Abi)
HOOK(0x00491110, "fn_00491110", fn_00491110_Entry, nullptr);

// 0x00491140 (edi = mgr; ecx only a frame slot): builds the menu input bits mgr+0xec from key repeats (0x00491090 with
// dt = g_frameDt 0x00604a90): keys 0x17/0x00 -> 1, 0x18/0x01 -> 2, 0x19/0x02 -> 4, 0x1a/0x03 -> 8 (timers +0x330..
// +0x33c; 0.75, 0.25, 0.1), key 0xd through 0x00414730 (threshold 0.5) -> 0x10, 0x1b -> 0x10 (+0x340), 0x1c -> 0x20
// (+0x344; 0.25, 0.25, 0.1). Skipped (bits 0) while flag bit 1 of mgr+0xf0 is set. Returns the last result (or dt bits).
// guess: up/down/left/right, accept, back.
uint32_t fn_00491140(uint8_t* mgr)
{
    const uint32_t dtBits = FrameDtBits();
    Field<uint32_t>(mgr, 0xec) = 0;
    if (Field<uint8_t>(mgr, 0xf0) & 2)
        return dtBits;
    const float dt = bf(dtBits);
    uint32_t r;
    struct Entry
    {
        uint32_t key;
        uint32_t timer;
        uint32_t bit;
    };
    static const Entry first[8] = {{0x17, 0x330, 1}, {0x18, 0x334, 2}, {0x19, 0x338, 4}, {0x1a, 0x33c, 8},
                                   {0x00, 0x330, 1}, {0x01, 0x334, 2}, {0x02, 0x338, 4}, {0x03, 0x33c, 8}};
    for (const Entry& e : first)
    {
        r = fn_00491090(e.key, &Field<float>(mgr, e.timer), dt, 0x3f400000u, 0x3e800000u, 0x3dcccccdu);
        if (static_cast<uint8_t>(r))
            Field<uint32_t>(mgr, 0xec) |= e.bit;
    }
    r = orig::fn_00414730((r & 0xffffff00u) | 1, 0xd, Field<uint32_t>(G(), 0xa80), 0x3f000000u);
    if (static_cast<uint8_t>(r))
        Field<uint32_t>(mgr, 0xec) |= 0x10;
    r = fn_00491090(0x1b, &Field<float>(mgr, 0x340), dt, 0x3e800000u, 0x3e800000u, 0x3dcccccdu);
    if (static_cast<uint8_t>(r))
        Field<uint32_t>(mgr, 0xec) |= 0x10;
    r = fn_00491090(0x1c, &Field<float>(mgr, 0x344), dt, 0x3e800000u, 0x3e800000u, 0x3dcccccdu);
    if (static_cast<uint8_t>(r))
        Field<uint32_t>(mgr, 0xec) |= 0x20;
    return r;
}

static uint32_t fn_00491140_Abi(uint32_t /*ecx*/, uint32_t edi)
{
    return fn_00491140(B(edi));
}
ABI_ENTRY_00491140(fn_00491140_Entry, fn_00491140_Abi)
HOOK(0x00491140, "fn_00491140", fn_00491140_Entry, nullptr);

// 0x00491360 (eax = mgr, edx = float* x; stack: float* y, callee pops 4): returns the normalised cursor position
// mgr+0x328 / +0x32c through the pointers. al = 1 (upper bytes from +0x32c). Not executed in recorded runs.
uint32_t fn_00491360(uint8_t* mgr, uint32_t* x, uint32_t* y)
{
    *x = Field<uint32_t>(mgr, 0x328);
    const uint32_t v = Field<uint32_t>(mgr, 0x32c);
    *y = v;
    return (v & 0xffffff00u) | 1;
}

static uint32_t fn_00491360_Abi(uint32_t eax, uint32_t edx, uint32_t a0)
{
    return fn_00491360(B(eax), reinterpret_cast<uint32_t*>(edx), reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_00491360(fn_00491360_Entry, fn_00491360_Abi)
HOOK(0x00491360, "fn_00491360", fn_00491360_Entry, nullptr);

// 0x00491380 (esi = mgr; stack: float* x, float* y, unused; callee pops 0xc): mouse cursor. GetCursorPos, GetWindowRect
// and GetClientRect (window g+0x64) give the cursor relative to the client centre: mgr+0x320 = cx - (left + bx / 2) -
// w / 2, mgr+0x324 = h - (cy - (top + (by - bx / 2))) - h / 2 (bx/by = window minus client size), clamped to +-w/2,
// +-h/2. 0x00463ac0 (eax = g+0xa70, ecx = {u, v, 0}) gets the position in 0..1, which is also stored at +0x328/+0x32c
// and returned through the pointers. Returns 1 when the position changed, else 0. Evidence: the user32 imports.
uint32_t fn_00491380(uint8_t* mgr, float* outX, float* outY)
{
    uint32_t frame[0x14];  // the original's 0x50-byte frame
    std::memset(frame, 0, sizeof(frame));
    float* f = reinterpret_cast<float*>(frame);
    frame[5] = Field<uint32_t>(mgr, 0x320);  // [0x14] old x
    frame[6] = Field<uint32_t>(mgr, 0x324);  // [0x18] old y
    POINT* cursor = reinterpret_cast<POINT*>(&frame[7]);
    RECT* window = reinterpret_cast<RECT*>(&frame[0xc]);
    RECT* client = reinterpret_cast<RECT*>(&frame[0x10]);
    Import<BOOL(WINAPI*)(LPPOINT)>(0x0056b258)(cursor);
    Import<BOOL(WINAPI*)(HWND, LPRECT)>(0x0056b254)(Field<HWND>(G(), 0x64), window);
    Import<BOOL(WINAPI*)(HWND, LPRECT)>(0x0056b250)(Field<HWND>(G(), 0x64), client);
    const int32_t clientW = client->right - client->left;
    const int32_t clientH = client->bottom - client->top;
    const float halfW = static_cast<float>(clientW) * K(0x005bcb4c);
    const int32_t borderX = window->right - client->right - window->left + client->left;
    const float halfH = static_cast<float>(clientH) * K(0x005bcb4c);
    const int32_t borderY = window->bottom - client->bottom - window->top + client->top;
    const float halfBorderX = static_cast<float>(borderX) * K(0x005bcb4c);
    float x = static_cast<float>(cursor->x) - (static_cast<float>(window->left) + halfBorderX);
    x = x - halfW;
    Field<float>(mgr, 0x320) = x;
    const float h = halfH + halfH;
    float y = static_cast<float>(cursor->y) -
              (static_cast<float>(window->top) + (static_cast<float>(borderY) - halfBorderX));
    y = h - y;
    y = y - halfH;
    Field<float>(mgr, 0x324) = y;
    const float negHalfW = -halfW;
    if (x < negHalfW)
        Field<float>(mgr, 0x320) = negHalfW;
    if (Field<float>(mgr, 0x320) > halfW)
        Field<float>(mgr, 0x320) = halfW;
    const float negHalfH = -halfH;
    if (y < negHalfH)
        Field<float>(mgr, 0x324) = negHalfH;
    if (Field<float>(mgr, 0x324) > halfH)
        Field<float>(mgr, 0x324) = halfH;
    const float w = halfW + halfW;
    f[9] = (halfW + Field<float>(mgr, 0x320)) / w;  // [0x24]
    f[10] = (halfH + Field<float>(mgr, 0x324)) / h;  // [0x28]
    frame[11] = 0;                                    // [0x2c]
    orig::fn_00463ac0(Field<uint32_t>(G(), 0xa70), u(&frame[9]));
    const float nx = (halfW + Field<float>(mgr, 0x320)) / w;
    Field<float>(mgr, 0x328) = nx;
    *outX = nx;
    const float ny = (halfH + Field<float>(mgr, 0x324)) / h;
    Field<float>(mgr, 0x32c) = ny;
    *outY = ny;
    if (Field<float>(mgr, 0x320) == bf(frame[5]) && Field<float>(mgr, 0x324) == bf(frame[6]))
        return 0;
    return 1;
}

static uint32_t fn_00491380_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t /*a2*/)
{
    return fn_00491380(B(esi), reinterpret_cast<float*>(a0), reinterpret_cast<float*>(a1));
}
ABI_ENTRY_00491380(fn_00491380_Entry, fn_00491380_Abi)
HOOK(0x00491380, "fn_00491380", fn_00491380_Entry, nullptr);

// 0x004915b0 (eax = mgr): per-frame update. With flag bit 4 of mgr+0xf0: when the input object g+0xa80 has +0x14 set or
// +4 == -1 the bit is cleared; otherwise, unless the top screen's +0x4c has bit 8, the screen "TCR:C6-8" (0x0058ff64;
// guess: a console certification-requirement screen, e.g. controller removed) is pushed (UI_PushScreen, eax = 0) when
// not already shown (0x00490ab0), clearing the bit. Then the transitions advance (0x00490e90 with g_frameDt); when
// none is pending and a screen is active, the input bits are built (0x00491140) and, without flag bit 0, given to the
// top screen's virtual +4 (mgr, &mgr+0xec), cleared first when its +0x4c has bit 0x4000. Returns the last result.
uint32_t fn_004915b0(uint8_t* mgr)
{
    const uint8_t al = Field<uint8_t>(mgr, 0xf0);
    if (al & 0x10)
    {
        uint8_t* input = Field<uint8_t*>(G(), 0xa80);
        if (Field<uint32_t>(input, 0x14) != 0 || Field<int32_t>(input, 4) == -1)
        {
            Field<uint8_t>(mgr, 0xf0) = static_cast<uint8_t>(al & 0xef);
        }
        else
        {
            uint8_t* top = Field<uint8_t*>(mgr, 0xf4);
            if (!top || (Field<uint32_t>(top, 0x4c) & 0x100) == 0)
            {
                if (!static_cast<uint8_t>(fn_00490ab0(reinterpret_cast<const char*>(0x0058ff64), mgr)))
                {
                    Field<uint8_t>(mgr, 0xf0) &= 0xef;
                    UI_PushScreen(0, mgr, 0x0058ff64);
                }
            }
        }
    }
    uint32_t r = fn_00490e90(mgr, bf(FrameDtBits()));
    if (!static_cast<uint8_t>(r))
        return r;
    if (!Field<uint32_t>(mgr, 0xf4))
        return 0;
    r = fn_00491140(mgr);
    if (Field<uint8_t>(mgr, 0xf0) & 1)
        return r;
    void* top = Field<void*>(mgr, 0xf4);
    if (Field<uint32_t>(top, 0x4c) & 0x4000)
        Field<uint32_t>(mgr, 0xec) = 0;
    return VCall<uint32_t>(top, 4 / 4, u(mgr), u(mgr + 0xec));
}

static uint32_t fn_004915b0_Abi(uint32_t eax)
{
    return fn_004915b0(B(eax));
}
ABI_ENTRY_004915b0(fn_004915b0_Entry, fn_004915b0_Abi)
HOOK(0x004915b0, "fn_004915b0", fn_004915b0_Entry, nullptr);

// ================================================================================================================
// Screens (vtable 0x0058fce0 base)
// ================================================================================================================

// 0x004916e0 UiScreen_Construct (ecx = flags, esi = screen, edi = name; stack: list, layer name, owner; callee pops
// 0xc): vtable 0x0058fce0, +0x58 = owner, +4/+8/+0x5c/+0x64/+0x60 = 0, +0x4c = flags & ~3 (written twice, as the
// original), links into the list with 0x004929a0 (eax = screen, edx = list), copies the name to +0xc and the second
// string to +0x2c. Returns the screen. Name from the tracker.
uint32_t UiScreen_Construct(uint32_t flags, uint8_t* screen, const char* name, uint32_t list, const char* layer,
                            uint32_t owner)
{
    Field<uint32_t>(screen, 0x58) = owner;
    Field<uint32_t>(screen, 0) = 0x0058fce0u;
    Field<uint32_t>(screen, 4) = 0;
    Field<uint32_t>(screen, 8) = 0;
    Field<uint32_t>(screen, 0x5c) = 0;
    Field<uint32_t>(screen, 0x64) = 0;
    Field<uint32_t>(screen, 0x60) = 0;
    Field<uint32_t>(screen, 0x4c) = flags;
    Field<uint32_t>(screen, 0x4c) = flags & 0xfffffffcu;
    fn_004929a0(screen, list);
    std::strcpy(reinterpret_cast<char*>(screen + 0xc), name);
    std::strcpy(reinterpret_cast<char*>(screen + 0x2c), layer);
    return u(screen);
}

static uint32_t UiScreen_Construct_Abi(uint32_t ecx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return UiScreen_Construct(ecx, B(esi), reinterpret_cast<const char*>(edi), a0, reinterpret_cast<const char*>(a1),
                              a2);
}
ABI_ENTRY_004916e0(UiScreen_Construct_Entry, UiScreen_Construct_Abi)
HOOK(0x004916e0, "UiScreen_Construct", UiScreen_Construct_Entry, nullptr);

// 0x00491750 (ecx = screen): base destructor body: vtable 0x0058fce0, then tail-calls 0x004929a0 (eax = screen, edx = 0:
// unlink). Not executed in recorded runs.
uint32_t fn_00491750(uint8_t* screen)
{
    Field<uint32_t>(screen, 0) = 0x0058fce0u;
    return fn_004929a0(screen, 0);
}

static uint32_t fn_00491750_Abi(uint32_t ecx)
{
    return fn_00491750(B(ecx));
}
ABI_ENTRY_00491750(fn_00491750_Entry, fn_00491750_Abi)
HOOK(0x00491750, "fn_00491750", fn_00491750_Entry, nullptr);

// 0x004918b0 (ecx = screen; stack: argument, show (byte); callee pops 8): shows or hides a screen. Show: 0x00463d60
// (ebx: bl = +0x4c bit 3; stack: g+0xa70) saves the HUD shown bits, 0x00463e80 (ebx = g+0xa70; stack: &+0x2c, 1, 1, 0)
// shows the screen's layer, +0x64 = +0x60 = 0, +0x5c = argument, 0x004919c0 (screen, 1) picks the selection, virtual
// +0xc (1, 0), +0x4c |= 1, with +0x4c bit 7 0x00414590 (eax = g+0xa80, cl = 1), with bit 8 g+0x18 += 1, with bit 0x1000
// 0x00403410 (ecx = g; stack: 0, 0xc). Hide: virtual +0xc (0, 1), 0x00491b20 (esi = 0, edi = screen) deselects,
// 0x00463df0 (stack: g+0xa70) restores the shown bits, +0x4c &= ~1, bit 7 -> 0x00414590 (cl = 0), bit 8 -> g+0x18 -= 1.
uint32_t fn_004918b0(uint8_t* screen, uint32_t arg, uint32_t show)
{
    uint8_t* g = G();
    if (static_cast<uint8_t>(show))
    {
        const uint32_t s = Field<uint32_t>(screen, 0x4c) >> 3;
        orig::fn_00463d60((s & 0xffffff00u) | (s & 1u), Field<uint32_t>(g, 0xa70));
        orig::fn_00463e80(Field<uint32_t>(G(), 0xa70), u(screen + 0x2c), 1, 1, 0);
        Field<uint32_t>(screen, 0x64) = 0;
        Field<uint32_t>(screen, 0x60) = 0;
        Field<uint32_t>(screen, 0x5c) = arg;
        fn_004919c0(screen, 1);
        VCall<uint32_t>(screen, 0xc / 4, 1u, 0u);
        const uint32_t flags = Field<uint32_t>(screen, 0x4c) | 1;
        uint8_t* g2 = G();
        Field<uint32_t>(screen, 0x4c) = flags;
        if (static_cast<int8_t>(flags) < 0)
            orig::fn_00414590(Field<uint32_t>(g2, 0xa80), (flags & 0xffffff00u) | 1);
        if (Field<uint32_t>(screen, 0x4c) & 0x100)
            ++Field<int32_t>(g2, 0x18);
        if (Field<uint32_t>(screen, 0x4c) & 0x1000)
            return orig::fn_00403410(u(g2), 0, 0xc);
        return Field<uint32_t>(screen, 0x4c);
    }
    VCall<uint32_t>(screen, 0xc / 4, 0u, 1u);
    fn_00491b20(nullptr, screen);
    orig::fn_00463df0(Field<uint32_t>(G(), 0xa70));
    const uint32_t flags = Field<uint32_t>(screen, 0x4c) & 0xfffffffeu;
    uint8_t* g2 = G();
    Field<uint32_t>(screen, 0x4c) = flags;
    if (static_cast<int8_t>(flags) < 0)
        orig::fn_00414590(Field<uint32_t>(g2, 0xa80), flags & 0xffffff00u);
    const uint32_t f = Field<uint32_t>(screen, 0x4c);
    if (f & 0x100)
        --Field<int32_t>(g2, 0x18);
    return f;
}

static uint32_t fn_004918b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004918b0(B(ecx), a0, a1);
}
ABI_ENTRY_004918b0(fn_004918b0_Entry, fn_004918b0_Abi)
HOOK(0x004918b0, "fn_004918b0", fn_004918b0_Entry, nullptr);

// 0x004919c0 (stack: screen, select (byte); callee pops 8): chooses the screen's selected UI object. With a layer name
// (+0x2c): its index in the HUD's object names (0x00463c50: ebx = name, edi = g+0xa70); when absent the debug print
// 0x00407ff0 (g+0x34, "Cannot find menu %s - you should rebuild this level" 0x0058ff30, name; compiled out). Then
// every UI object (iterator 0x004e7340 eax = (g+0xa84)+0xeb8, esi = &iterator / 0x004e73c0 edi = &iterator) of that
// layer (+0x46c == index) is offered to the screen's virtual +0x14 (object, select): the last accepted is the choice,
// else the last selectable one (+0x474 bit 8 without 0x1040); +0x474 bit 0x10000 sets +0x60. With a choice and select,
// 0x00491b20 (esi = choice, edi = screen); otherwise a remembered +0x64 is re-selected. Finally the iterator's
// reference (+0x18 of iterator[1]) is released. The ecx 0x00463c50 receives is the caller's (unused there: 0).
uint32_t fn_004919c0(uint8_t* screen, uint32_t select)
{
    uint32_t frame[3] = {0, 0, 0};  // [0] choice, [1..2] the iterator
    const char* name = reinterpret_cast<const char*>(screen + 0x2c);
    uint32_t r = 0;
    uint32_t held = 0;
    bool done = false;
    if (*name)
    {
        const int32_t index = static_cast<int32_t>(orig::fn_00463c50(0, u(name), Field<uint32_t>(G(), 0xa70)));
        if (index < 0)
            reinterpret_cast<void(__cdecl*)(...)>(0x00407ff0)(Field<uint32_t>(G(), 0x34), 0x0058ff30u, u(name));
        uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<uint8_t*>(G(), 0xa84), 0xeb8), u(&frame[1])));
        if (obj)
        {
            do
            {
                if (Field<int32_t>(obj, 0x46c) == index)
                {
                    if (static_cast<uint8_t>(VCall<uint32_t>(screen, 0x14 / 4, u(obj), select)))
                        frame[0] = u(obj);
                    const uint32_t bits = Field<uint32_t>(obj, 0x474);
                    if ((bits & 0x100) && !(bits & 0x1040) && frame[0] == 0)
                        frame[0] = u(obj);
                    if (bits & 0x10000)
                        Field<uint32_t>(screen, 0x60) = u(obj);
                }
                obj = B(orig::fn_004e73c0(u(&frame[1])));
            } while (obj);
            if (frame[0] && static_cast<uint8_t>(select))
            {
                r = fn_00491b20(B(frame[0]), screen);
                held = frame[2];
                done = true;
            }
        }
        if (!done)
            held = frame[2];
    }
    if (!done)
    {
        uint8_t* remembered = Field<uint8_t*>(screen, 0x64);
        if (remembered)
            r = fn_00491b20(remembered, screen);
    }
    if (held)
        --Field<int32_t>(B(held), 0x18);
    return r;
}

static uint32_t fn_004919c0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004919c0(B(a0), a1);
}
ABI_ENTRY_004919c0(fn_004919c0_Entry, fn_004919c0_Abi)
HOOK(0x004919c0, "fn_004919c0", fn_004919c0_Entry, nullptr);

// 0x00491ae0 (ecx = value, esi = uint32_t* quotient, edi = uint32_t* remainder; stack: one unused argument, callee pops
// 4): splits the value by 100 (unsigned; 0 and -1 give 0, 0) into the optional outputs. Returns 1 when 0 < quotient <
// 100 (signed). guess: a packed level / sub-index id.
uint32_t fn_00491ae0(uint32_t value, uint32_t* quotient, uint32_t* remainder)
{
    uint32_t q = 0;
    uint32_t r = 0;
    if (value != 0 && value != 0xffffffffu)
    {
        q = value / 100;
        r = value % 100;
    }
    if (quotient)
        *quotient = q;
    if (remainder)
        *remainder = r;
    if (static_cast<int32_t>(q) > 0 && static_cast<int32_t>(q) < 100)
        return 1;
    return 0;
}

static uint32_t fn_00491ae0_Abi(uint32_t ecx, uint32_t esi, uint32_t edi, uint32_t /*a0*/)
{
    return fn_00491ae0(ecx, reinterpret_cast<uint32_t*>(esi), reinterpret_cast<uint32_t*>(edi));
}
ABI_ENTRY_00491ae0(fn_00491ae0_Entry, fn_00491ae0_Abi)
HOOK(0x00491ae0, "fn_00491ae0", fn_00491ae0_Entry, nullptr);

// 0x00491b20 (esi = UI object or 0, edi = screen): selects an object on the screen. A non-null object must be
// selectable (+0x474 bit 8 and not 0x1040), else al = 0. The previous selection (+0x64) gets 0x00491b60 (dl = 0), the
// new one is stored and gets 0x00491b60 (dl = 1). Returns al = 1. (dl's upper edx bytes are the caller's in the
// original; passed here as 0.)
uint32_t fn_00491b20(uint8_t* object, uint8_t* screen)
{
    if (object)
    {
        const uint32_t bits = Field<uint32_t>(object, 0x474);
        if ((bits & 0x1040) != 0 || (bits & 0x100) == 0)
            return bits & 0xffffff00u;
    }
    uint8_t* previous = Field<uint8_t*>(screen, 0x64);
    if (previous)
        fn_00491b60(previous, 0);
    Field<uint8_t*>(screen, 0x64) = object;
    uint32_t r = 0;
    if (object)
        r = fn_00491b60(object, 1);
    return (r & 0xffffff00u) | 1;
}

static uint32_t fn_00491b20_Abi(uint32_t esi, uint32_t edi)
{
    return fn_00491b20(B(esi), B(edi));
}
ABI_ENTRY_00491b20(fn_00491b20_Entry, fn_00491b20_Abi)
HOOK(0x00491b20, "fn_00491b20", fn_00491b20_Entry, nullptr);

// 0x00491b60 (ecx = UI object, edx: dl = selected; stack: one slot, overwritten): replaces its stack argument with edx
// and tail-jumps to the object's virtual +0x14c, which pops it (callers do not; 0x00491b20). Recovered as a call then
// return with a hand-written entry stub (symbols/abi.csv: "no ret found", stack 1 caller: really callee pops 4).
uint32_t fn_00491b60(uint8_t* object, uint32_t edx)
{
    return VCall<uint32_t>(object, 0x14c / 4, edx);
}

namespace
{
uint32_t fn_00491b60_Abi(uint32_t ecx, uint32_t edx)
{
    return fn_00491b60(B(ecx), edx);
}
__declspec(naked) void fn_00491b60_Entry()
{
    __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push edx
    __asm push ecx __asm call fn_00491b60_Abi __asm add esp, 8 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx
    __asm pop edx __asm pop ecx __asm ret 4
}
}  // namespace
HOOK(0x00491b60, "fn_00491b60", fn_00491b60_Entry, nullptr);

uint32_t fn_004927a0(uint8_t* item, uint32_t ecx, uint32_t a0, uint32_t a1);
uint32_t fn_00492c90(uint32_t mode, uint8_t* item, uint8_t* screen);
uint32_t fn_00492d80(uint8_t* screen, uint8_t* item);
uint32_t fn_004a2c10(uint32_t slot, uint16_t* out);
uint32_t fn_004a3020(uint8_t* item, uint32_t flag);

namespace
{
// 0x0046c980 (eax = rows, edi = list box): ends in a tail jump to the list's virtual +0x144 with no stack arguments, so
// tools/abi.py generated no orig wrapper ("no ret found"); called here directly. guess: sets the visible row count.
uint32_t Fn0046c980(uint32_t eaxValue, uint32_t ediValue)
{
    static const uint32_t target = 0x0046c980;
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov eax, eaxValue
        mov edi, ediValue
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}
// 0x004f2030 (ebx = 0x14c-byte find record, edi = output; stack: path pattern; caller pops): FindFirstFile wrapper (same
// shim as src/ui/ui_01.cpp).
uint32_t Fn004f2030(uint32_t ebxValue, uint32_t ediValue, uint32_t path)
{
    static const uint32_t target = 0x004f2030;
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov eax, path
        mov ebx, ebxValue
        mov edi, ediValue
        push eax
        call dword ptr [target]
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}
// CRT functions of the game (cdecl).
inline int GameStrnicmp(const char* a, const char* b, uint32_t n)
{
    return GameFunction<int(__cdecl*)(const char*, const char*, uint32_t)>(0x00544957)(a, b, n);
}
inline void GameMakepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext)
{
    GameFunction<void(__cdecl*)(char*, const char*, const char*, const char*, const char*)>(0x0053859e)(path, drive,
                                                                                                         dir, fname,
                                                                                                         ext);
}
inline void GameSplitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
    GameFunction<void(__cdecl*)(const char*, char*, char*, char*, char*)>(0x00538456)(path, drive, dir, fname, ext);
}
}  // namespace

// 0x00491b70 UiScreen_InitItem (ecx = screen; stack: UI object, flag (byte); callee pops 8; vtable 0x0058fce0 slot 5 and
// the other screen vtables 0x0058fc2c..0x0058fd28, 0x005b9718): prepares one menu item. Its id +0x478 is split by 100
// (0x00491ae0) into quotient q (the action) and remainder r; valid ids (0 < q < 100) other than 98/99 are selectable
// (+0x474 bit 8). Invalid ids get +0x474 |= 0x1000 and return. When the object's virtual +0x150 has bit 1, +0x4ec -=
// r * 24 * 0x005bcbf4. Then by q (jump tables 0x00492134 / 0x004920dc): most known actions enable the item (0x004927a0
// cl = 1); 2 enables it by save slot -1 (0x004a2c10); 5 disables it while nothing is on the (g+0xab4) stack; 6/42 fill
// mission lists (0x00492c90 mode 1 or 3); 11/41 depend on the profile (0x004a0720 and g+0x4c8 bits 0x1000/0x800); 12
// fills the movie list (0x00492d80); 30 sets the text +0x5c; 34 enables by 0x004a2e90; 37/38 subtitle toggles and 79
// auto-aim (0x0046cb60 on g+0x176/+0x177/+0x4c2); 56/57/58 volume sliders (0x0046cce0 on g+0x490/+0x498/+0x494); 64
// 0x00410be0; 65 0x004106b0 when the flag is set; 71 by +0x4a8; 72 save slot (0x004a3020 with "is SaveGame"); 73 the
// "SaveGame"/"ChooseGame" continue text; 99 enabled + 0x1000; others go to 0x004710d0 (edx = q, ebx = screen; stack:
// object, flag, r). Returns al = the result byte (only the case-2 and default paths set it). Strings: 'ui_sfxmenu_*',
// 'ui_controls_autoaim', 'SaveGame', 'ChooseGame'. Name from the tracker. Not executed in recorded runs.
uint32_t UiScreen_InitItem(uint8_t* screen, uint8_t* item, uint32_t flag)
{
    uint8_t result = 0;
    orig::fn_00464b10(u(item));
    uint32_t q = 0;
    uint32_t r = 0;
    const uint32_t valid = fn_00491ae0(Field<uint32_t>(item, 0x478), &q, &r);
    if (static_cast<uint8_t>(valid) && q != 0x62 && q != 0x63)
        Field<uint32_t>(item, 0x474) = Field<uint32_t>(item, 0x474) | 0x100;
    else
        Field<uint32_t>(item, 0x474) = Field<uint32_t>(item, 0x474) & 0xfffffeffu;
    if (!static_cast<uint8_t>(valid))
    {
        Field<uint32_t>(item, 0x474) |= 0x1000;
        return result;
    }
    if (VCall<uint32_t>(item, 0x150 / 4) & 2)
    {
        const int32_t px = static_cast<int32_t>(r * 3 * 8);
        Field<float>(item, 0x4ec) = Field<float>(item, 0x4ec) - static_cast<float>(px) * K(0x005bcbf4);
    }
    uint8_t* g = G();
    switch (q)
    {
    case 1: case 3: case 8: case 9: case 10: case 18: case 28: case 29: case 36: case 43: case 54: case 66: case 69:
    case 70:
        fn_004927a0(item, 1, u(screen), 0);
        return result;
    case 65:
        fn_004927a0(item, 1, u(screen), 0);
        if (!static_cast<uint8_t>(flag))
            return result;
        orig::fn_004106b0(u(item));
        return result;
    case 64:
        orig::fn_00410be0(u(item));
        return result;
    case 2:
    {
        const uint32_t ok = fn_004a2c10(0xffffffffu, nullptr);
        result = static_cast<uint8_t>(ok);
        fn_004927a0(item, ok, u(screen), 0);
        return result;
    }
    case 72:
    {
        fn_004927a0(item, 1, u(screen), 0);
        const int c = GameStricmp(reinterpret_cast<const char*>(screen + 0xc), reinterpret_cast<const char*>(0x00590118));
        const uint32_t neg = 0u - static_cast<uint32_t>(c);
        fn_004a3020(item, (neg & 0xffffff00u) | (c == 0 ? 1u : 0u));
        return result;
    }
    case 73:
    {
        const char* name = reinterpret_cast<const char*>(screen + 0xc);
        uint32_t text;
        if (GameStricmp(name, reinterpret_cast<const char*>(0x00590118)) == 0)
            text = 0x0058ff1c;
        else if (GameStricmp(name, reinterpret_cast<const char*>(0x00590100)) == 0)
            text = 0x0058ff04;
        else
        {
            fn_004927a0(item, 0, u(screen), 1);
            return result;
        }
        fn_004927a0(item, 1, u(screen), 0);
        orig::fn_00465850(text, u(item), 1);
        return result;
    }
    case 99:
        fn_004927a0(item, 1, u(screen), 0);
        Field<uint32_t>(item, 0x474) |= 0x1000;
        return result;
    case 34:
    {
        const uint32_t ok = orig::fn_004a2e90(0, 0);
        fn_004927a0(item, ok, u(screen), 0);
        return result;
    }
    case 5:
        if (Field<uint32_t>(Field<uint8_t*>(g, 0xab4), 0xf4) != 0)
            fn_004927a0(item, 1, u(screen), 0);
        else
            fn_004927a0(item, 0, u(screen), 1);
        return result;
    case 6:
    case 42:
        fn_00492c90(q != 6 ? 3u : 1u, item, screen);
        return result;
    case 12:
        fn_00492d80(screen, item);
        return result;
    case 30:
        orig::fn_00465850(Field<uint32_t>(screen, 0x5c), u(item), 1);
        Field<uint32_t>(item, 0x474) |= 0x1000;
        return result;
    case 56:
        orig::fn_0046cce0(u(item), 0x0058feec, 0x15, 1, 0x0048fef0, u(g + 0x490), 0, 0x3f800000u,
                          Field<uint32_t>(g, 0x490));
        return result;
    case 57:
        orig::fn_0046cce0(u(item), 0x0058fed4, 0x15, 1, 0x0048fef0, u(g + 0x498), 0, 0x3f800000u,
                          Field<uint32_t>(g, 0x498));
        return result;
    case 58:
        orig::fn_0046cce0(u(item), 0x0058febc, 0x15, 1, 0x0048fef0, u(g + 0x494), 0, 0x3f800000u,
                          Field<uint32_t>(g, 0x494));
        return result;
    case 37:
        orig::fn_0046cb60(u(item), u(g + 0x176), 0x0058fea0, 1);
        return result;
    case 38:
        orig::fn_0046cb60(u(item), u(g + 0x177), 0x0058fe84, 1);
        return result;
    case 79:
        orig::fn_0046cb60(u(item), u(g + 0x4c2), 0x0058fe70, 1);
        if (Field<uint32_t>(G(), 0x748) != 0 && Field<uint32_t>(G(), 0x550) > 1)
            fn_004927a0(item, 0, u(screen), 0);
        else
            fn_004927a0(item, 1, u(screen), 0);
        return result;
    case 11:
    case 41:
    {
        const uint32_t ok = orig::fn_004a0720(u(item + 0x4a0), 4, u(G()));
        const uint32_t bit = q == 11 ? 0x1000u : 0x800u;
        if (!static_cast<uint8_t>(ok) && (Field<uint32_t>(G(), 0x4c8) & bit) == 0)
            fn_004927a0(item, 0, u(screen), 0);
        else
            fn_004927a0(item, 1, u(screen), 0);
        return result;
    }
    case 71:
    {
        uint32_t ok = 1;
        if (Field<int8_t>(item, 0x4a8) >= 0x33)
            ok = orig::fn_004a0720(u(item + 0x4a0), 4, u(G()));
        fn_004927a0(item, ok, u(screen), 0);
        return result;
    }
    default:
        return orig::fn_004710d0(q, u(screen), u(item), flag, r);
    }
}

static uint32_t UiScreen_InitItem_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return UiScreen_InitItem(B(ecx), B(a0), a1);
}
ABI_ENTRY_00491b70(UiScreen_InitItem_Entry, UiScreen_InitItem_Abi)
HOOK(0x00491b70, "UiScreen_InitItem", UiScreen_InitItem_Entry, nullptr);

// 0x004921a0 UiScreen_Activate (ecx = screen; stack: mgr, UI object; callee pops 8; vtable 0x0058fce0 slot 6): runs the
// action of a selected menu item, by q = +0x478 / 100 (0x00491ae0; jump tables 0x00492748 / 0x004926dc): screen pushes
// (UI_PushScreen / 0x00490b20 with "Options", "Cheats", "Extras", "LevelSelect", "ShowControls", "MovieSelect",
// "Credits", "GfxOpt", "SfxOpt", "SaveGame"/"LoadGame"/"ChooseGame"/"PlayMovie"/"Loading"), YesNo prompts (quit,
// load, restart, reset), the YesNo answers (28: callback +0x35c (+0x354, +0x358) else open +0x354 / pop; 29: callback
// +0x350 (+0x348, +0x34c) else open +0x348 / pop), level choice (6/42: 0x0046c850 into the 0x104-wchar frame buffer,
// then the level name 0x00490810), difficulty (71: last character of +0x4a0 - '1' to 0x00401f60), movie (12:
// 0x0046c840 into the 0x40-byte buffer, copied to 0x005feb80), 73: 0x004a2cb0 then virtual +0x20 (0), 74: level
// "&lev_*" (0x004035d0), others 0x004714c0 (eax = q, ecx = r, edi = mgr; stack: screen, object, r). Afterwards, when
// the counter 0x0063a160 did not change, plays the select (0x00491850) or cancel (0x00491870) sound, and with the
// object's +0x474 bit 0 applies the options (Options_Apply, al = 1, esi = g). Strings: the screen names above.
// Name from the tracker. Not executed in recorded runs.
uint32_t UiScreen_Activate(uint8_t* screen, uint8_t* mgr, uint8_t* item)
{
    uint8_t frame[0x258];  // the original's frame: [0x10..0x50) movie name, [0x50..0x258) wide level name
    const uint32_t counter = GameGlobal<uint32_t>(0x0063a160);
    uint8_t selectSound = 1;
    uint8_t cancelSound = 0;
    uint32_t q = 0;
    uint32_t r = 0;
    uint32_t result = 0;
    if (static_cast<uint8_t>(fn_00491ae0(Field<uint32_t>(item, 0x478), &q, &r)))
    {
        switch (q)
        {
        case 1:
            Field<uint8_t>(G(), 0x568) &= 0xfb;
            fn_004907c0(mgr, reinterpret_cast<const char*>(0x005b1738));
            result = UI_PushScreen(0x0059004c, mgr, 0x00590118);
            break;
        case 2:
            Field<uint8_t>(G(), 0x568) &= 0xfb;
            result = UI_PushScreen(0x005b252c, mgr, 0x0059010c);
            break;
        case 3:
            result = UI_PushScreen(0, mgr, 0x005900d8);
            break;
        case 5:
            fn_00490b60(mgr);
            cancelSound = 1;
            selectSound = 0;
            break;
        case 6:
        case 42:
        {
            uint16_t* level = reinterpret_cast<uint16_t*>(frame + 0x50);
            if (!static_cast<uint8_t>(orig::fn_0046c850(1, u(item), u(level), 0x104)))
                break;
            fn_00490810(Field<uint8_t*>(G(), 0xab4), level);
            const uint32_t name = (Field<uint8_t>(G(), 0x568) & 4) ? 0x005b252cu : 0x0059004cu;
            result = UI_PushScreen(0, mgr, name);
            if (Field<uint8_t>(G(), 0x568) & 4)
                result = orig::fn_00491830(0);
            break;
        }
        case 8:
            fn_004907c0(Field<uint8_t*>(G(), 0xab4), nullptr);
            result = UI_PushYesNo(0x0058fe34, mgr, 0x005b252c, 1, 0, 0, 0, 0, 0);
            break;
        case 9:
            result = UI_PushScreen(0, mgr, 0x0058fe28);
            break;
        case 10:
            result = UI_PushScreen(0, mgr, 0x0058ffdc);
            break;
        case 11:
            result = UI_PushScreen(0, mgr, 0x00590088);
            break;
        case 12:
        {
            char* movie = reinterpret_cast<char*>(frame + 0x10);
            if (!static_cast<uint8_t>(orig::fn_0046c840(1, u(item), 0x40, u(movie))))
                break;
            char* dst = reinterpret_cast<char*>(0x005feb80);
            int i = 0;
            char c;
            do
            {
                c = movie[i];
                dst[i] = c;
                ++i;
            } while (c);
            result = UI_PushScreen(0x005feb80, mgr, 0x005bc0fc);
            break;
        }
        case 15:
            break;
        case 18:
            result = UI_PushScreen(0, mgr, 0x005b2534);
            break;
        case 28:
        {
            typedef uint32_t(__cdecl * Callback)(uint32_t, uint32_t);
            const Callback fn = Field<Callback>(mgr, 0x35c);
            if (fn && static_cast<uint8_t>(fn(Field<uint32_t>(mgr, 0x354), Field<uint32_t>(mgr, 0x358))))
            {
            }
            else if (Field<uint32_t>(mgr, 0x354))
                fn_00490b20(Field<uint32_t>(mgr, 0x358), mgr, Field<uint32_t>(mgr, 0x354));
            else
                fn_00490b60(mgr);
            cancelSound = 1;
            selectSound = 0;
            break;
        }
        case 29:
        {
            typedef uint32_t(__cdecl * Callback)(uint32_t, uint32_t);
            const Callback fn = Field<Callback>(mgr, 0x350);
            if (fn && static_cast<uint8_t>(fn(Field<uint32_t>(mgr, 0x348), Field<uint32_t>(mgr, 0x34c))))
                break;
            if (Field<uint32_t>(mgr, 0x348))
                result = fn_00490b20(Field<uint32_t>(mgr, 0x34c), mgr, Field<uint32_t>(mgr, 0x348));
            else
                result = fn_00490b60(mgr);
            break;
        }
        case 34:
            if (static_cast<uint8_t>(orig::fn_00403bb0(u(G()))))
                result = UI_PushYesNo(0x0058fe5c, mgr, 0, 1, 0, 0x0048fee0, 0, 0, 0);
            else
                result = orig::fn_004a2ef0();
            break;
        case 36:
            result = UI_PushScreen(0, mgr, 0x005900c0);
            break;
        case 41:
            Field<uint8_t>(G(), 0x568) |= 4;
            result = UI_PushScreen(0x005900a4, mgr, 0x00590100);
            break;
        case 43:
            result = UI_PushYesNo(0x0058fe18, mgr, 0, 1, 0, 0x0048fe50, 0, 0, 0);
            break;
        case 52:
            result = UI_PushScreen(0, mgr, 0x00590038);
            break;
        case 54:
            result = UI_PushScreen(0, mgr, 0x00590010);
            break;
        case 66:
            result = UI_PushScreen(0, mgr, 0x00590024);
            break;
        case 69:
        {
            uint8_t* g = G();
            const uint32_t name = orig::fn_0044a660(Field<uint32_t>(g, 0xa64), 0);
            fn_004907c0(Field<uint8_t*>(g, 0xab4), reinterpret_cast<const char*>(name));
            if (static_cast<uint8_t>(orig::fn_00403bb0(u(g))))
                result = UI_PushYesNo(0x0058fe48, mgr, 0x005b252c, 1, 0, 0, 0, 0, 0);
            else
                result = UI_PushScreen(0, Field<uint8_t*>(G(), 0xab4), 0x005b252c);
            break;
        }
        case 70:
        {
            uint8_t* g = G();
            fn_004907c0(Field<uint8_t*>(g, 0xab4), nullptr);
            result = UI_PushScreen(0, Field<uint8_t*>(g, 0xab4), 0x005b252c);
            break;
        }
        case 71:
        {
            const char* s = reinterpret_cast<const char*>(item + 0x4a0);
            const size_t len = std::strlen(s);
            const int32_t mode = static_cast<int32_t>(static_cast<int8_t>(s[len - 1])) - 0x31;
            orig::fn_00401f60(static_cast<uint32_t>(mode), u(G()));
            orig::fn_00491830(0);
            result = fn_00490b20(0, mgr, 0x005b252c);
            break;
        }
        case 73:
            orig::fn_004a2cb0();
            result = VCall<uint32_t>(screen, 0x20 / 4, 0u);
            break;
        case 74:
            result = orig::fn_004035d0(u(G()), 0x0058fe10, 0, 0, 0, 0, 0, 0, 0, 0);
            break;
        default:
            result = orig::fn_004714c0(q, r, u(mgr), u(screen), u(item), r);
            cancelSound = 0;
            selectSound = 0;
            break;
        }
    }
    if (counter == GameGlobal<uint32_t>(0x0063a160))
    {
        if (selectSound)
            result = orig::fn_00491850(0);
        else if (cancelSound)
            result = orig::fn_00491870(0);
    }
    if (Field<uint32_t>(item, 0x474) & 1)
        result = orig::fn_00401ce0(1, u(G()));
    return result;
}

static uint32_t UiScreen_Activate_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return UiScreen_Activate(B(ecx), B(a0), B(a1));
}
ABI_ENTRY_004921a0(UiScreen_Activate_Entry, UiScreen_Activate_Abi)
HOOK(0x004921a0, "UiScreen_Activate", UiScreen_Activate_Entry, nullptr);

// 0x004927a0 (eax = UI object, cl = enabled; stack: unused, hide (byte); callee pops 8): enabled clears +0x474 bit 6
// and shows the object (+0x470 |= 1); disabled sets bit 6 and shows it unless hide, which clears +0x470 bit 0. Returns
// eax (the object; abi.csv: "returns none").
uint32_t fn_004927a0(uint8_t* item, uint32_t ecx, uint32_t /*a0*/, uint32_t a1)
{
    if (static_cast<uint8_t>(ecx))
    {
        Field<uint32_t>(item, 0x474) = Field<uint32_t>(item, 0x474) & 0xffffffbfu;
        Field<uint32_t>(item, 0x470) |= 1;
        return u(item);
    }
    Field<uint32_t>(item, 0x474) = Field<uint32_t>(item, 0x474) | 0x40;
    if (static_cast<uint8_t>(a1))
        Field<uint32_t>(item, 0x470) &= 0xfffffffeu;
    else
        Field<uint32_t>(item, 0x470) |= 1;
    return u(item);
}

static uint32_t fn_004927a0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004927a0(B(eax), ecx, a0, a1);
}
ABI_ENTRY_004927a0(fn_004927a0_Entry, fn_004927a0_Abi)
HOOK(0x004927a0, "fn_004927a0", fn_004927a0_Entry, nullptr);

// 0x004928f0 (ecx = screen; stack: focus (byte), select (byte); callee pops 8; vtable 0x0058fce0 slot 3): focus
// change. When focus differs from +0x4c bit 1: 0x00414580 (eax = g+0xa80), +0x54 = +0x50 = 0; focus: +0x4c |= 2, (g+
// 0xa70)+0x50 = !(+0x4c bit 9) (guess: mouse cursor shown), key timers reset (0x00491110 eax = g+0xab4), with +0x4c bit
// 0x2000 the sound reference g+0xa88 starts (0x0044f510), and with select 0x004919c0 (screen, 0). Unfocus: +0x4c &= ~2
// and with bit 0x2000 the sound reference stops (0x0044f540).
uint32_t fn_004928f0(uint8_t* screen, uint32_t focus, uint32_t select)
{
    const uint32_t bit = Field<uint32_t>(screen, 0x4c) >> 1;
    const uint8_t cl = static_cast<uint8_t>(focus);
    if (cl == static_cast<uint8_t>(bit & 1))
        return (bit & 0xffffff00u) | (bit & 1);
    uint8_t* g = G();
    orig::fn_00414580(Field<uint32_t>(g, 0xa80));
    Field<uint32_t>(screen, 0x54) = 0;
    Field<uint32_t>(screen, 0x50) = 0;
    if (cl)
    {
        const uint32_t flags = Field<uint32_t>(screen, 0x4c) | 2;
        Field<uint32_t>(screen, 0x4c) = flags;
        Field<uint8_t>(Field<void*>(g, 0xa70), 0x50) = static_cast<uint8_t>(~(flags >> 9) & 1);
        uint32_t r = fn_00491110(Field<uint8_t*>(g, 0xab4));
        if (Field<uint32_t>(screen, 0x4c) & 0x2000)
            r = orig::fn_0044f510(Field<uint32_t>(g, 0xa88));
        if (static_cast<uint8_t>(select))
            return fn_004919c0(screen, 0);
        return (r & 0xffffff00u) | static_cast<uint8_t>(select);
    }
    const uint32_t flags = Field<uint32_t>(screen, 0x4c) & 0xfffffffdu;
    Field<uint32_t>(screen, 0x4c) = flags;
    if (flags & 0x2000)
        return orig::fn_0044f540(Field<uint32_t>(g, 0xa88));
    return flags;
}

static uint32_t fn_004928f0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004928f0(B(ecx), a0, a1);
}
ABI_ENTRY_004928f0(fn_004928f0_Entry, fn_004928f0_Abi)
HOOK(0x004928f0, "fn_004928f0", fn_004928f0_Entry, nullptr);

// 0x004929a0 (eax = screen, edx = list head or 0): intrusive list move. Unlinks the screen (+4 next, +8 the slot that
// points at it) and, with a head, inserts it first (head = screen). Returns the old first (now next) when there was
// one, else the screen (or 0 after inserting into an empty list).
uint32_t fn_004929a0(uint8_t* screen, uint32_t edx)
{
    uint8_t* slot = Field<uint8_t*>(screen, 8);
    if (slot)
        Field<uint32_t>(slot, 0) = Field<uint32_t>(screen, 4);
    uint8_t* next = Field<uint8_t*>(screen, 4);
    uint8_t* link = screen + 4;
    if (next)
        Field<uint32_t>(next, 8) = Field<uint32_t>(screen, 8);
    Field<uint32_t>(screen, 8) = edx;
    if (!edx)
    {
        Field<uint32_t>(link, 0) = 0;
        return u(screen);
    }
    Field<uint32_t>(link, 0) = Field<uint32_t>(B(edx), 0);
    Field<uint8_t*>(B(edx), 0) = screen;
    const uint32_t first = Field<uint32_t>(link, 0);
    if (first)
        Field<uint8_t*>(B(first), 8) = link;
    return first;
}

static uint32_t fn_004929a0_Abi(uint32_t eax, uint32_t edx)
{
    return fn_004929a0(B(eax), edx);
}
ABI_ENTRY_004929a0(fn_004929a0_Entry, fn_004929a0_Abi)
HOOK(0x004929a0, "fn_004929a0", fn_004929a0_Entry, nullptr);

// 0x004929e0 UiScreen_Update (ecx = screen; stack: mgr, uint32_t* input bits; callee pops 8; vtable 0x0058fce0 slot 1):
// +0x50 += g_frameDt; +0x54 = idle time (reset to 0.0 by any input). With the mouse cursor active ((g+0xa70)+0x50 and
// +0x54): the cursor (0x00491380, esi = g+0xab4) and the click 0x00414b20 (edi = g+0xa80; 0x10004, 0.5) pick the object
// under the cursor (0x00463930 (HUD, x, y, moved-or-clicked)); a click on nothing clears input bit 0x10; moving onto
// another object selects it (0x00491b20) with the move sound. With +0x4c bit 4 and 0x004149d0 (edi = g+0xa80; guess:
// a back key) the screen closes. Otherwise the default item +0x60 (when not selected) then the selected item +0x64 get
// virtual +0x148 (screen, input); a handled input plays the move sound when the counter 0x0063a160 is unchanged (the
// selected item then gets 0x00491b60 dl = 1). Unhandled input bit 0x10 activates the selected item (screen virtual
// +0x18 (mgr, item)) or pops the screen (unless +0x4c bit 0x20); input bit 0x20 (unless +0x4c bit 0x20) plays the
// cancel sound and pops (0x00490b60). Name from the tracker.
uint32_t UiScreen_Update(uint8_t* screen, uint8_t* mgr, uint32_t* input)
{
    uint32_t frame[3] = {0, 0, 0};  // [0] byte: moved/clicked, [1] y, [2] x
    const float dt = bf(FrameDtBits());
    Field<float>(screen, 0x50) = dt + Field<float>(screen, 0x50);
    if (*input == 0)
        Field<float>(screen, 0x54) = dt + Field<float>(screen, 0x54);
    else
        Field<float>(screen, 0x54) = K(0x0056c05c);
    uint8_t* hud = Field<uint8_t*>(G(), 0xa70);
    if (Field<uint8_t>(hud, 0x50) && Field<uint32_t>(hud, 0x54))
    {
        uint8_t clicked = 0;
        const uint32_t moved = fn_00491380(Field<uint8_t*>(G(), 0xab4), reinterpret_cast<float*>(&frame[2]),
                                           reinterpret_cast<float*>(&frame[1]));
        reinterpret_cast<uint8_t*>(frame)[0] = static_cast<uint8_t>(moved);
        if (static_cast<uint8_t>(orig::fn_00414b20(Field<uint32_t>(G(), 0xa80), 0x10004, 0x3f000000u)))
        {
            clicked = 1;
            reinterpret_cast<uint8_t*>(frame)[0] = clicked;
        }
        uint8_t* target = B(orig::fn_00463930(Field<uint32_t>(G(), 0xa70), frame[2], frame[1], frame[0]));
        if (!target && clicked)
            *input &= 0xffffffefu;
        if (reinterpret_cast<uint8_t*>(frame)[0] && target && Field<uint8_t*>(screen, 0x64) != target)
        {
            fn_00491b20(target, screen);
            if (Field<uint8_t*>(screen, 0x64) == target)
                orig::fn_00491890(0);
        }
    }
    if ((Field<uint8_t>(screen, 0x4c) & 0x10) && orig::fn_004149d0(Field<uint32_t>(G(), 0xa80)) != 0)
    {
        orig::fn_00491870(0);
        return fn_00490b60(mgr);
    }
    void* def = Field<void*>(screen, 0x60);
    if (def && def != Field<void*>(screen, 0x64))
    {
        const uint32_t counter = GameGlobal<uint32_t>(0x0063a160);
        if (static_cast<uint8_t>(VCall<uint32_t>(def, 0x148 / 4, u(screen), u(input))))
        {
            if (counter != GameGlobal<uint32_t>(0x0063a160))
                return 0;
            return orig::fn_00491890(0);
        }
    }
    void* selected = Field<void*>(screen, 0x64);
    if (selected)
    {
        const uint32_t counter = GameGlobal<uint32_t>(0x0063a160);
        if (static_cast<uint8_t>(VCall<uint32_t>(selected, 0x148 / 4, u(screen), u(input))))
        {
            if (counter == GameGlobal<uint32_t>(0x0063a160))
                orig::fn_00491890(0);
            return fn_00491b60(Field<uint8_t*>(screen, 0x64), 1);
        }
        if (*input & 0x10)
        {
            void* item = Field<void*>(screen, 0x64);
            if (item)
                return VCall<uint32_t>(screen, 0x18 / 4, u(mgr), u(item));
            if (Field<uint8_t>(screen, 0x4c) & 0x20)
                return 0;
            return fn_00490b60(mgr);
        }
    }
    if (Field<uint8_t>(screen, 0x4c) & 0x20)
        return 0;
    if (!(*input & 0x20))
        return 0;
    orig::fn_00491870(0);
    return fn_00490b60(mgr);
}

static uint32_t UiScreen_Update_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return UiScreen_Update(B(ecx), B(a0), reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_004929e0(UiScreen_Update_Entry, UiScreen_Update_Abi)
HOOK(0x004929e0, "UiScreen_Update", UiScreen_Update_Entry, nullptr);

// 0x00492c90 (eax = mode 1 or 3, ecx = list box; stack: screen, callee pops 4): fills a mission list. Clears it
// (ListBox_Clear 0x0046c1d0), names it "ui_listbox_missionlist" (mode 1, 12 rows, pattern "lev_*") or
// "ui_listbox_bonusmissions" (4 rows, "lev_bonus_*") (UiText_SetName 0x00465850, 0x0046c980). Unless g+0x4c8 bit 11
// (guess: all unlocked) and when 0x004a0670 (ecx = g, edx = mode) is false, the list is disabled (0x004927a0 cl = 0) and
// left empty. Otherwise enabled and filled by the level manager g+0xa64 (0x0044aa80 eax = pattern; stack: manager,
// callback 0x00492bd0, &context, flag dword) where context is the frame {list, mode, pattern, flag byte, 0}; then
// ListBox_SortB (0x0046c2c0) and 0x0046c560 (eax = 0, ecx = list). Not executed in recorded runs.
uint32_t fn_00492c90(uint32_t mode, uint8_t* list, uint8_t* screen)
{
    uint32_t frame[5] = {0, 0, 0, 0, 0};  // the original's 0x14-byte frame (context of the callback)
    uint8_t* bytes = reinterpret_cast<uint8_t*>(frame);
    bytes[0xc] = static_cast<uint8_t>((Field<uint32_t>(G(), 0x4c8) >> 0xb) & 1);
    bytes[0xd] = 0;
    frame[1] = mode;
    frame[0] = u(list);
    orig::fn_0046c1d0(u(list));
    if (mode == 1)
    {
        orig::fn_00465850(0x0058fddc, u(list), 1);
        Fn0046c980(0xc, u(list));
        frame[2] = 0x0058fdd4;
    }
    else
    {
        orig::fn_00465850(0x0058fdb8, u(list), 1);
        Fn0046c980(4, u(list));
        frame[2] = 0x0058fdac;
    }
    uint8_t* g = G();
    if (!bytes[0xc] && !static_cast<uint8_t>(orig::fn_004a0670(u(g), mode)))
        return fn_004927a0(list, 0, u(screen), 0);
    fn_004927a0(list, 1, u(screen), 0);
    uint32_t flagDword;
    std::memcpy(&flagDword, bytes + 0xd, 4);
    orig::fn_0044aa80(frame[2], Field<uint32_t>(g, 0xa64), 0x00492bd0, u(frame), flagDword);
    orig::fn_0046c2c0(u(list));
    return orig::fn_0046c560(0, u(list));
}

static uint32_t fn_00492c90_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_00492c90(eax, B(ecx), B(a0));
}
ABI_ENTRY_00492c90(fn_00492c90_Entry, fn_00492c90_Abi)
HOOK(0x00492c90, "fn_00492c90", fn_00492c90_Entry, nullptr);

// 0x00492d80 (stack: screen (unused), list box; callee pops 8): fills the movie list "ui_listbox_movielist" (14 rows):
// clears it, then for every file of "movies\*.pcv" (_makepath, FindFirstFile 0x004f2030, FindNextFileA, FindClose)
// accepted by 0x004a0690 (eax = 0, esi = name; stack: g) and, unless g+0x4c8 bit 12, unlocked (0x004a0720 eax = name,
// ecx = 0, ebx = g): adds "<name>.vid" (_splitpath / _makepath) with ListBox_AddEntryDefault (eax = 0x808080, cl =
// first file, edx = text, ebx = list) and sets the entry id +0x478: "post_"/"pre_" + "D_D" names give 0x1000001 /
// 0x1000000 + (D0 - '0') * 10 + 2 * D2 - 0x60, others the first three characters (s0 + s1 << 8 + s2 << 16, signed).
// Finally ListBox_SortA (0x0046c2b0). Strings: 'movies', '.pcv', '.vid', 'post_', 'pre_'. Not executed in recorded
// runs. (When 0x004f2030 fails the original still calls FindClose on the record's handle.)
uint32_t fn_00492d80(uint8_t* /*screen*/, uint8_t* list)
{
    uint8_t frame[0x558];
    std::memset(frame, 0, sizeof(frame));
    uint8_t* g = G();
    frame[3] = static_cast<uint8_t>((Field<uint32_t>(g, 0x4c8) >> 0xc) & 1);
    orig::fn_00465850(0x0058fd94, u(list), 1);
    Fn0046c980(0xe, u(list));
    orig::fn_0046c1d0(u(list));
    char* path = reinterpret_cast<char*>(frame + 0xc);
    uint32_t& index = *reinterpret_cast<uint32_t*>(frame + 4);
    index = 0;
    GameMakepath(path, nullptr, reinterpret_cast<const char*>(0x0058fd80), reinterpret_cast<const char*>(0x0058fd88),
                 reinterpret_cast<const char*>(0x0058fd8c));
    uint8_t* record = frame + 0x110;
    if (static_cast<uint8_t>(Fn004f2030(u(record), u(frame + 8), u(path))))
    {
        const char* name = *reinterpret_cast<const char**>(frame + 8);
        for (;;)
        {
            if (static_cast<uint8_t>(orig::fn_004a0690(0, 0, u(name), u(G()))) &&
                (frame[3] || static_cast<uint8_t>(orig::fn_004a0720(u(name), 0, u(G())))))
            {
                GameSplitpath(name, nullptr, nullptr, path, nullptr);
                char* text = reinterpret_cast<char*>(frame + 0x358);
                GameMakepath(text, nullptr, nullptr, path, reinterpret_cast<const char*>(0x0058fd78));
                const uint32_t ecx = (index & 0xffffff00u) | (index == 0 ? 1u : 0u);
                uint8_t* entry = B(orig::fn_0046c310(0x808080, ecx, u(text), u(list)));
                if (entry)
                {
                    const char* digits = nullptr;
                    uint32_t base = 0;
                    if (GameStrnicmp(name, reinterpret_cast<const char*>(0x0058fd70), 5) == 0)
                    {
                        base = 0x1000001;
                        digits = name + 5;
                    }
                    else if (GameStrnicmp(name, reinterpret_cast<const char*>(0x0058fd68), 4) == 0)
                    {
                        base = 0x1000000;
                        digits = name + 4;
                    }
                    int32_t id;
                    if (digits && digits[0] >= 0x30 && digits[0] <= 0x39 && digits[1] == 0x5f && digits[2] >= 0x30 &&
                        digits[2] <= 0x39)
                    {
                        const int32_t d = (static_cast<int32_t>(digits[0]) - 0x30) * 5 + static_cast<int32_t>(digits[2]);
                        id = static_cast<int32_t>(base) + d * 2 - 0x60;
                    }
                    else
                    {
                        id = ((static_cast<int32_t>(name[2]) << 8) + static_cast<int32_t>(name[1])) << 8;
                        id += static_cast<int32_t>(name[0]);
                    }
                    Field<int32_t>(entry, 0x478) = id;
                }
            }
            const BOOL more = Import<BOOL(WINAPI*)(HANDLE, LPWIN32_FIND_DATAA)>(0x0056b0d8)(
                Field<HANDLE>(record, 0), reinterpret_cast<LPWIN32_FIND_DATAA>(record + 4));
            if (more)
                name = reinterpret_cast<const char*>(frame + 0x140);
            ++index;
            if (!more)
                break;
        }
    }
    Import<BOOL(WINAPI*)(HANDLE)>(0x0056b0dc)(Field<HANDLE>(record, 0));
    return orig::fn_0046c2b0(u(list));
}

static uint32_t fn_00492d80_Abi(uint32_t a0, uint32_t a1)
{
    return fn_00492d80(B(a0), B(a1));
}
ABI_ENTRY_00492d80(fn_00492d80_Entry, fn_00492d80_Abi)
HOOK(0x00492d80, "fn_00492d80", fn_00492d80_Entry, nullptr);

// ================================================================================================================
// Screen subclasses (vtables 0x0058fb14, 0x0058fc74 (C), 0x0058fc98 (D), 0x0058fcbc (E), 0x0058fd04 (G), 0x0058fd28 (H))
// ================================================================================================================

// 0x00492f80 (ecx = screen; stack: argument, show (byte); callee pops 8; vtable 0x0058fc74 slot 2): base show
// (0x004918b0); when shown, +0x268 = 0 and the level name (g+0xab4)+0xfc is copied to +0x68 (0x00403be0, edi = 0).
// guess: the level-loading screen. Not executed in recorded runs.
uint32_t fn_00492f80(uint8_t* screen, uint32_t arg, uint32_t show)
{
    uint32_t r = fn_004918b0(screen, arg, show);
    if (Field<uint8_t>(screen, 0x4c) & 1)
    {
        Field<uint32_t>(screen, 0x268) = 0;
        r = orig::fn_00403be0(u(Field<uint8_t*>(G(), 0xab4) + 0xfc), u(screen + 0x68), 0);
    }
    return r;
}

static uint32_t fn_00492f80_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00492f80(B(ecx), a0, a1);
}
ABI_ENTRY_00492f80(fn_00492f80_Entry, fn_00492f80_Abi)
HOOK(0x00492f80, "fn_00492f80", fn_00492f80_Entry, nullptr);

// 0x00492fd0 (ecx = screen; stack: mgr, input (unused); callee pops 8; vtable 0x0058fc74 slot 1): counts frames in
// +0x268 (-1 stays -1). At frame 10 the level is started (0x00403600 eax = g, edx = &+0x68 name). At frame 5 the level is
// loaded: a name starting with '-' through 0x0048c990 (eax = name + 1, ecx = g+0xab8), '!' is skipped, others through
// 0x00418730 (eax = name; stack: g+0xa64, 0, 1). On failure: unless the current level (0x0044a660 eax = g+0xa64, cl = 0)
// is "lev_shell" (0x005bc144), the name becomes L"lev_shell"; then UI_PushNotify ("ui_corruptcontent" 0x0058fd54, mgr,
// edx = 0; stack: not-shell flag, 0, 0; the pushed dword keeps the screen pointer's upper bytes from the frame slot) and
// +0x268 = -1. Not executed in recorded runs.
uint32_t fn_00492fd0(uint8_t* screen, uint8_t* mgr)
{
    uint32_t slot = u(screen);  // the original's push ecx frame slot
    const int32_t n = Field<int32_t>(screen, 0x268);
    Field<int32_t>(screen, 0x268) = n + 1;
    if (n == 5)
    {
        uint16_t* name = &Field<uint16_t>(screen, 0x68);
        const uint16_t c = *name;
        uint32_t ok;
        if (c == 0x2d)
            ok = orig::fn_0048c990(u(screen + 0x6a), Field<uint32_t>(G(), 0xab8));
        else if (c == 0x21)
            return c;
        else
            ok = orig::fn_00418730(u(name), Field<uint32_t>(G(), 0xa64), 0, 1);
        if (static_cast<uint8_t>(ok))
            return ok;
        const uint32_t current = orig::fn_0044a660(Field<uint32_t>(G(), 0xa64), 0);
        const uint8_t notShell =
            GameStricmp(reinterpret_cast<const char*>(current), reinterpret_cast<const char*>(0x005bc144)) != 0 ? 1 : 0;
        slot = (slot & 0xffffff00u) | notShell;
        if (notShell)
            orig::fn_00403be0(0x0058ff70, u(name), 0);
        const uint32_t r = UI_PushNotify(0x0058fd54, mgr, 0, slot, 0, 0);
        Field<int32_t>(screen, 0x268) = -1;
        return r;
    }
    if (n == 0xa)
        return orig::fn_00403600(u(G()), u(screen + 0x68));
    if (n == -1)
        Field<int32_t>(screen, 0x268) = n;
    return static_cast<uint32_t>(n);
}

static uint32_t fn_00492fd0_Abi(uint32_t ecx, uint32_t a0, uint32_t /*a1*/)
{
    return fn_00492fd0(B(ecx), B(a0));
}
ABI_ENTRY_00492fd0(fn_00492fd0_Entry, fn_00492fd0_Abi)
HOOK(0x00492fd0, "fn_00492fd0", fn_00492fd0_Entry, nullptr);

uint32_t fn_00493240(uint8_t* screen);
uint32_t fn_00493270(uint8_t* screen);
uint32_t fn_004932e0(const char* list, uint8_t* screen);
uint32_t fn_00493320(uint8_t* screen, uint32_t list, uint32_t show);

// 0x004930c0 (ecx = screen; stack: list (0 = "pub_01" 0x0058fd4c), show (byte); callee pops 8; vtable 0x0058fc98 slot
// 2): opens like 0x00493320; when shown, the object +0x68 gets virtual +0x1c (1), (g+0xa70)+0x50 = 1 and 0x00467fe0
// (stack: g+0xa74); when hidden (g+0xa70)+0x50 = 0 and 0x004690c0 (edi = g+0xa74; ebp unused). guess: the pub screen.
// Not executed in recorded runs.
uint32_t fn_004930c0(uint8_t* screen, uint32_t list, uint32_t show)
{
    if (!list)
        list = 0x0058fd4c;
    fn_00493320(screen, list, show);
    if (Field<uint8_t>(screen, 0x4c) & 1)
    {
        void* obj = Field<void*>(screen, 0x68);
        if (obj)
            VCall<uint32_t>(obj, 0x1c / 4, 1u);
        uint8_t* g = G();
        Field<uint8_t>(Field<void*>(g, 0xa70), 0x50) = 1;
        return orig::fn_00467fe0(Field<uint32_t>(g, 0xa74));
    }
    uint8_t* g = G();
    Field<uint8_t>(Field<void*>(g, 0xa70), 0x50) = 0;
    return orig::fn_004690c0(Field<uint32_t>(g, 0xa74), 0);
}

static uint32_t fn_004930c0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004930c0(B(ecx), a0, a1);
}
ABI_ENTRY_004930c0(fn_004930c0_Entry, fn_004930c0_Abi)
HOOK(0x004930c0, "fn_004930c0", fn_004930c0_Entry, nullptr);

// 0x00493130 (stack: mgr, input; callee pops 8; vtable 0x0058fc98 slot 1): replaces its first stack argument with
// g+0xa74 and tail-jumps to 0x004695d0 (stack-only, ret 8; a function missing from symbols/functions.csv: it starts after
// 0x004694e0's padding). Recovered as a call then return. Not executed in recorded runs.
uint32_t fn_00493130(uint32_t /*mgr*/, uint32_t input)
{
    return GameFunction<uint32_t(__stdcall*)(uint32_t, uint32_t)>(0x004695d0)(Field<uint32_t>(G(), 0xa74), input);
}

static uint32_t fn_00493130_Abi(uint32_t a0, uint32_t a1)
{
    return fn_00493130(a0, a1);
}
ABI_ENTRY_00493130(fn_00493130_Entry, fn_00493130_Abi)
HOOK(0x00493130, "fn_00493130", fn_00493130_Entry, nullptr);

// 0x00493150 (eax = layer name, ecx = flags, edx = list, edi = name; stack: screen; callee pops 4): constructs a screen of
// vtable 0x0058fcbc (UiScreen_Construct with flags | 0x2300), +0x68 = +0x6c = 0, byte +0x70 = 0, empty list (0x004932e0
// eax = 0). Returns the screen. guess: the movie screen (its show plays "movies\<name>.pcv", 0x004931b0).
uint32_t fn_00493150(uint32_t layer, uint32_t flags, uint32_t list, const char* name, uint8_t* screen)
{
    UiScreen_Construct(flags | 0x2300, screen, name, list, reinterpret_cast<const char*>(layer), 0);
    Field<uint32_t>(screen, 0) = 0x0058fcbcu;
    Field<uint32_t>(screen, 0x68) = 0;
    Field<uint32_t>(screen, 0x6c) = 0;
    Field<uint8_t>(screen, 0x70) = 0;
    fn_004932e0(nullptr, screen);
    return u(screen);
}

static uint32_t fn_00493150_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi, uint32_t a0)
{
    return fn_00493150(eax, ecx, edx, reinterpret_cast<const char*>(edi), B(a0));
}
ABI_ENTRY_00493150(fn_00493150_Entry, fn_00493150_Abi)
HOOK(0x00493150, "fn_00493150", fn_00493150_Entry, nullptr);

// 0x00493190 (ecx = screen; stack: keep (byte); callee pops 4; vtables 0x0058fc98 / 0x0058fcbc slot 4): unless keep or
// +0x4c bit 0x400, stops the movie (0x00493240). Returns al = keep, else the last value. Not executed in recorded runs.
uint32_t fn_00493190(uint8_t* screen, uint32_t keep)
{
    if (static_cast<uint8_t>(keep))
        return keep;
    const uint32_t flags = Field<uint32_t>(screen, 0x4c);
    if (flags & 0x400)
        return flags;
    return fn_00493240(screen);
}

static uint32_t fn_00493190_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00493190(B(ecx), a0);
}
ABI_ENTRY_00493190(fn_00493190_Entry, fn_00493190_Abi)
HOOK(0x00493190, "fn_00493190", fn_00493190_Entry, nullptr);

// 0x004931b0 (eax = movie name or 0 (then +0x70), ecx = screen): stops the current movie (0x00493240), builds
// "movies\<name>.pcv" at +0x70 (_splitpath / _makepath; 0x100-byte frame buffer for the name) and opens it through the
// video player (g+0x28) virtual +0xc (path) into +0x68; on success +0x6c = 0x00495bd0 (ecx = name) and 0x004a07a0 (eax =
// name, ecx = g, edi = 0; stack: 1) (guess: marks the movie as seen). Returns 1 when a movie is open. Not executed in
// recorded runs.
uint32_t fn_004931b0(const char* name, uint8_t* screen)
{
    char frame[0x100];
    fn_00493240(screen);
    if (!name)
        name = reinterpret_cast<const char*>(screen + 0x70);
    GameSplitpath(name, nullptr, nullptr, frame, nullptr);
    char* path = reinterpret_cast<char*>(screen + 0x70);
    GameMakepath(path, nullptr, reinterpret_cast<const char*>(0x0058fd80), frame,
                 reinterpret_cast<const char*>(0x0058fd8c));
    const uint32_t movie = VCall<uint32_t>(Field<void*>(G(), 0x28), 0xc / 4, u(path));
    Field<uint32_t>(screen, 0x68) = movie;
    if (movie)
    {
        Field<uint32_t>(screen, 0x6c) = orig::fn_00495bd0(u(frame));
        orig::fn_004a07a0(u(frame), u(G()), 0, 1);
    }
    return Field<uint32_t>(screen, 0x68) != 0 ? 1u : 0u;
}

static uint32_t fn_004931b0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004931b0(reinterpret_cast<const char*>(eax), B(ecx));
}
ABI_ENTRY_004931b0(fn_004931b0_Entry, fn_004931b0_Abi)
HOOK(0x004931b0, "fn_004931b0", fn_004931b0_Entry, nullptr);

// 0x00493240 (esi = screen): closes the open movie +0x68 through the video player (g+0x28) virtual +0x10 (movie) and
// clears +0x68 / +0x6c. Returns eax (the virtual's result, or 0). Not executed in recorded runs.
uint32_t fn_00493240(uint8_t* screen)
{
    const uint32_t movie = Field<uint32_t>(screen, 0x68);
    if (!movie)
        return 0;
    const uint32_t r = VCall<uint32_t>(Field<void*>(G(), 0x28), 0x10 / 4, movie);
    Field<uint32_t>(screen, 0x68) = 0;
    Field<uint32_t>(screen, 0x6c) = 0;
    return r;
}

static uint32_t fn_00493240_Abi(uint32_t esi)
{
    return fn_00493240(B(esi));
}
ABI_ENTRY_00493240(fn_00493240_Entry, fn_00493240_Abi)
HOOK(0x00493240, "fn_00493240", fn_00493240_Entry, nullptr);

// 0x00493270 (esi = screen): takes the next name of the play list +0x274 (separated by ';' or ','; the separator is
// consumed) into a 0x104-byte frame buffer and plays it (0x004931b0). Returns its result, or al = 0 at the end of the
// list. Not executed in recorded runs.
uint32_t fn_00493270(uint8_t* screen)
{
    char frame[0x104];
    char* out = frame;
    if (*Field<char*>(screen, 0x274))
    {
        for (;;)
        {
            char* src = Field<char*>(screen, 0x274);
            *out++ = *src;
            Field<char*>(screen, 0x274) = src + 1;
            const char c = out[-1];
            if (c == 0x3b || c == 0x2c)
            {
                --out;
                break;
            }
            if (*Field<char*>(screen, 0x274) == 0)
                break;
        }
    }
    *out = 0;
    if (!frame[0])
        return 0;
    return fn_004931b0(frame, screen);
}

static uint32_t fn_00493270_Abi(uint32_t esi)
{
    return fn_00493270(B(esi));
}
ABI_ENTRY_00493270(fn_00493270_Entry, fn_00493270_Abi)
HOOK(0x00493270, "fn_00493270", fn_00493270_Entry, nullptr);

// 0x004932e0 (eax = play list or 0, esi = screen): copies the list to +0x174 (game strncpy 0x005382a0, 0x100 bytes,
// +0x273 = 0) or empties it, and points the cursor +0x274 at it. Returns the cursor / strncpy's result.
uint32_t fn_004932e0(const char* list, uint8_t* screen)
{
    char* buffer = reinterpret_cast<char*>(screen + 0x174);
    if (list)
    {
        orig::fn_005382a0(u(buffer), u(list), 0x100);
        Field<char*>(screen, 0x274) = buffer;
        Field<uint8_t>(screen, 0x273) = 0;
        return u(buffer);
    }
    *buffer = 0;
    Field<char*>(screen, 0x274) = buffer;
    return u(buffer);
}

static uint32_t fn_004932e0_Abi(uint32_t eax, uint32_t esi)
{
    return fn_004932e0(reinterpret_cast<const char*>(eax), B(esi));
}
ABI_ENTRY_004932e0(fn_004932e0_Entry, fn_004932e0_Abi)
HOOK(0x004932e0, "fn_004932e0", fn_004932e0_Entry, nullptr);

// 0x00493320 (ecx = screen; stack: play list, show (byte); callee pops 8; vtable 0x0058fcbc slot 2): base show
// (0x004918b0). Shown: 0x0044ad50 (al = 1), PostProcess_ReleaseTargets (esi = g+0xa98), the list is set (0x004932e0) and
// the first movie starts (0x00493270). Hidden: the movie stops (0x00493240), 0x0044ad50 (al = 0) and, when the video
// player g+0x28 exists and its virtual +8 is false, PostProcess_CreateTargets (eax = g+0xa98). (0x0044ad50 gets eax's
// upper bytes from the previous call.)
uint32_t fn_00493320(uint8_t* screen, uint32_t list, uint32_t show)
{
    uint32_t r = fn_004918b0(screen, list, show);
    if (Field<uint8_t>(screen, 0x4c) & 1)
    {
        orig::fn_0044ad50((r & 0xffffff00u) | 1);
        orig::fn_0047ca60(Field<uint32_t>(G(), 0xa98));
        fn_004932e0(reinterpret_cast<const char*>(list), screen);
        return fn_00493270(screen);
    }
    r = fn_00493240(screen);
    r = orig::fn_0044ad50(r & 0xffffff00u);
    void* player = Field<void*>(G(), 0x28);
    if (player)
    {
        r = VCall<uint32_t>(player, 8 / 4);
        if (!static_cast<uint8_t>(r))
            r = orig::fn_0047c920(Field<uint32_t>(G(), 0xa98));
    }
    return r;
}

static uint32_t fn_00493320_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00493320(B(ecx), a0, a1);
}
ABI_ENTRY_00493320(fn_00493320_Entry, fn_00493320_Abi)
HOOK(0x00493320, "fn_00493320", fn_00493320_Entry, nullptr);

// 0x004933b0 (ecx = screen; stack: focus (byte), select; callee pops 8; vtables 0x0058fc98 / 0x0058fcbc slot 3): base
// focus (0x004928f0), then the movie +0x68 gets virtual +0xc (paused = !focus; the dword keeps eax's upper bytes). guess.
// Not executed in recorded runs.
uint32_t fn_004933b0(uint8_t* screen, uint32_t focus, uint32_t select)
{
    const uint32_t r = fn_004928f0(screen, focus, select);
    void* movie = Field<void*>(screen, 0x68);
    if (!movie)
        return r;
    return VCall<uint32_t>(movie, 0xc / 4, (r & 0xffffff00u) | (static_cast<uint8_t>(focus) == 0 ? 1u : 0u));
}

static uint32_t fn_004933b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004933b0(B(ecx), a0, a1);
}
ABI_ENTRY_004933b0(fn_004933b0_Entry, fn_004933b0_Abi)
HOOK(0x004933b0, "fn_004933b0", fn_004933b0_Entry, nullptr);

// 0x004933e0 (ecx = screen; stack: mgr, input bits*; callee pops 8; vtable 0x0058fcbc slot 1): movie screen update. When a
// movie is open and has finished (+0xc bit 0) or input bits 0x30 are set, the next movie of the list starts (0x00493270)
// and input bits 0x30 are cleared; without a movie the next one is tried. When none could start the screen pops
// (0x00490b60); otherwise UiScreen_Update. Not executed in recorded runs.
uint32_t fn_004933e0(uint8_t* screen, uint8_t* mgr, uint32_t* input)
{
    uint8_t* movie = Field<uint8_t*>(screen, 0x68);
    uint8_t ended;
    if (movie)
    {
        if (!(Field<uint8_t>(movie, 0xc) & 1) && !(*reinterpret_cast<uint8_t*>(input) & 0x30))
            return UiScreen_Update(screen, mgr, input);
        ended = static_cast<uint8_t>(fn_00493270(screen)) == 0 ? 1 : 0;
        *input &= 0xffffffcfu;
    }
    else
        ended = static_cast<uint8_t>(fn_00493270(screen)) == 0 ? 1 : 0;
    if (ended)
        return fn_00490b60(mgr);
    return UiScreen_Update(screen, mgr, input);
}

static uint32_t fn_004933e0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004933e0(B(ecx), B(a0), reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_004933e0(fn_004933e0_Entry, fn_004933e0_Abi)
HOOK(0x004933e0, "fn_004933e0", fn_004933e0_Entry, nullptr);

// 0x00493520 (eax = layer, ecx = flags, edx = list, ebx = next screen name or 0; stack: screen, name, duration (float),
// argument string or 0; callee pops 0x10): constructs a timed screen of vtable 0x0058fd28 (UiScreen_Construct, flags |
// 0x23a0): +0x68 = duration, the next screen name at +0x6c and the string at +0x8c (empty when null). Returns it.
// guess: splash screens.
uint32_t fn_00493520(uint32_t layer, uint32_t flags, uint32_t list, const char* next, uint8_t* screen, const char* name,
                     uint32_t duration, const char* text)
{
    UiScreen_Construct(flags | 0x23a0, screen, name, list, reinterpret_cast<const char*>(layer), 0);
    Field<uint32_t>(screen, 0) = 0x0058fd28u;
    Field<uint8_t>(screen, 0x6c) = 0;
    Field<uint8_t>(screen, 0x8c) = 0;
    Field<uint32_t>(screen, 0x68) = duration;
    if (next)
        std::strcpy(reinterpret_cast<char*>(screen + 0x6c), next);
    if (text)
        std::strcpy(reinterpret_cast<char*>(screen + 0x8c), text);
    return u(screen);
}

static uint32_t fn_00493520_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t ebx, uint32_t a0, uint32_t a1,
                                uint32_t a2, uint32_t a3)
{
    return fn_00493520(eax, ecx, edx, reinterpret_cast<const char*>(ebx), B(a0), reinterpret_cast<const char*>(a1), a2,
                       reinterpret_cast<const char*>(a3));
}
ABI_ENTRY_00493520(fn_00493520_Entry, fn_00493520_Abi)
HOOK(0x00493520, "fn_00493520", fn_00493520_Entry, nullptr);

// 0x00493590 (ecx = screen; stack: mgr, input; callee pops 8; vtable 0x0058fd28 slot 1): UiScreen_Update, then once the
// screen's time +0x50 reaches the duration +0x68 it is replaced by the next screen +0x6c (0x00490b20, eax = 0) or popped
// (0x00490b60). Not executed in recorded runs.
uint32_t fn_00493590(uint8_t* screen, uint8_t* mgr, uint32_t* input)
{
    uint32_t r = UiScreen_Update(screen, mgr, input);
    if (Field<float>(screen, 0x50) >= Field<float>(screen, 0x68))
    {
        if (Field<uint8_t>(screen, 0x6c))
            return fn_00490b20(0, mgr, u(screen + 0x6c));
        r = fn_00490b60(mgr);
    }
    return r;
}

static uint32_t fn_00493590_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00493590(B(ecx), B(a0), reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_00493590(fn_00493590_Entry, fn_00493590_Abi)
HOOK(0x00493590, "fn_00493590", fn_00493590_Entry, nullptr);

// 0x00493770 (eax = list, ecx = screen, edi = name): constructs a screen of vtable 0x0058fd04 (UiScreen_Construct with
// flags 0x23a0, layer "" 0x0058a9a4, owner 0). Returns the screen.
uint32_t fn_00493770(uint32_t list, uint8_t* screen, const char* name)
{
    UiScreen_Construct(0x23a0, screen, name, list, reinterpret_cast<const char*>(0x0058a9a4), 0);
    Field<uint32_t>(screen, 0) = 0x0058fd04u;
    return u(screen);
}

static uint32_t fn_00493770_Abi(uint32_t eax, uint32_t ecx, uint32_t edi)
{
    return fn_00493770(eax, B(ecx), reinterpret_cast<const char*>(edi));
}
ABI_ENTRY_00493770(fn_00493770_Entry, fn_00493770_Abi)
HOOK(0x00493770, "fn_00493770", fn_00493770_Entry, nullptr);

// 0x00493790 (stack: mgr, input, both unused; callee pops 8; vtable 0x0058fd04 slot 1): calls the game's virtual +0x40.
// Not executed in recorded runs.
uint32_t fn_00493790(uint32_t /*a0*/, uint32_t /*a1*/)
{
    return VCall<uint32_t>(G(), 0x40 / 4);
}

static uint32_t fn_00493790_Abi(uint32_t a0, uint32_t a1)
{
    return fn_00493790(a0, a1);
}
ABI_ENTRY_00493790(fn_00493790_Entry, fn_00493790_Abi)
HOOK(0x00493790, "fn_00493790", fn_00493790_Entry, nullptr);

// 0x00494000 (eax = layer, ecx = flags, edx = list, edi = name; stack: screen; callee pops 4): constructs a screen of
// vtable 0x0058fb14 (UiScreen_Construct, flags | 0x220), +0x68 = +0x6c = 0. Returns it. guess: the level statistics
// screen (its show 0x00494050 reads the kill / accuracy counters).
uint32_t fn_00494000(uint32_t layer, uint32_t flags, uint32_t list, const char* name, uint8_t* screen)
{
    UiScreen_Construct(flags | 0x220, screen, name, list, reinterpret_cast<const char*>(layer), 0);
    Field<uint32_t>(screen, 0) = 0x0058fb14u;
    Field<uint32_t>(screen, 0x68) = 0;
    Field<uint32_t>(screen, 0x6c) = 0;
    return u(screen);
}

static uint32_t fn_00494000_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi, uint32_t a0)
{
    return fn_00494000(eax, ecx, edx, reinterpret_cast<const char*>(edi), B(a0));
}
ABI_ENTRY_00494000(fn_00494000_Entry, fn_00494000_Abi)
HOOK(0x00494000, "fn_00494000", fn_00494000_Entry, nullptr);

// 0x00494030 (ecx = screen; stack: flags; callee pops 4; slot 0 of every screen vtable): scalar deleting destructor:
// 0x00491750, then with flags bit 0 frees it (PMS_heapFree 0x004e08d0, ecx = heap 0x0063a210). Returns the screen.
// Not executed in recorded runs.
uint32_t fn_00494030(uint8_t* screen, uint32_t flags)
{
    fn_00491750(screen);
    if (static_cast<uint8_t>(flags) & 1)
        orig::fn_004e08d0(0x0063a210u, u(screen));
    return u(screen);
}

static uint32_t fn_00494030_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00494030(B(ecx), a0);
}
ABI_ENTRY_00494030(fn_00494030_Entry, fn_00494030_Abi)
HOOK(0x00494030, "fn_00494030", fn_00494030_Entry, nullptr);

uint32_t fn_00494310(uint8_t* screen);
uint32_t fn_00494870(uint8_t* object, uint32_t arg);

// 0x00494050 (ecx = screen; stack: argument, show (byte); callee pops 8; vtable 0x0058fb14 slot 2): base show; when
// shown: byte +0x88 = 0, +0x7c = +0x68 = +0x6c = +0x80 = 0, +0x84 = 1.0, the four counters g+0x2724/+0x2728/+0x2730/
// +0x272c are scaled by g+0x564, +0x70 = g+0x570 and +0x74 = max(0, g+0x570 + the four truncated counters); with the
// object 0x0063a158, 0x00494870 (ecx = it; stack: 0); then 0x00494310 (esi = screen). Not executed in recorded runs.
uint32_t fn_00494050(uint8_t* screen, uint32_t arg, uint32_t show)
{
    uint32_t r = fn_004918b0(screen, arg, show);
    if (!static_cast<uint8_t>(show))
        return r;
    Field<uint8_t>(screen, 0x88) = 0;
    Field<uint32_t>(screen, 0x7c) = 0;
    Field<uint32_t>(screen, 0x68) = 0;
    Field<uint32_t>(screen, 0x6c) = 0;
    Field<uint32_t>(screen, 0x84) = 0x3f800000u;
    Field<uint32_t>(screen, 0x80) = 0;
    uint8_t* g = G();
    Field<float>(g, 0x2724) = Field<float>(g, 0x564) * Field<float>(g, 0x2724);
    Field<float>(g, 0x2728) = Field<float>(g, 0x564) * Field<float>(g, 0x2728);
    Field<float>(g, 0x2730) = Field<float>(g, 0x564) * Field<float>(g, 0x2730);
    Field<float>(g, 0x272c) = Field<float>(g, 0x564) * Field<float>(g, 0x272c);
    Field<uint32_t>(screen, 0x70) = Field<uint32_t>(g, 0x570);
    int32_t sum = static_cast<int32_t>(Field<float>(g, 0x2724));
    sum += static_cast<int32_t>(Field<float>(g, 0x2730));
    sum += static_cast<int32_t>(Field<float>(g, 0x272c));
    sum += static_cast<int32_t>(Field<float>(g, 0x2728));
    int32_t total = Field<int32_t>(g, 0x570) + sum;
    Field<int32_t>(screen, 0x74) = total;
    if (total <= 0)
        total = 0;
    Field<int32_t>(screen, 0x74) = total;
    const uint32_t object = GameGlobal<uint32_t>(0x0063a158);
    if (object)
        fn_00494870(B(object), 0);
    return fn_00494310(screen);
}

static uint32_t fn_00494050_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00494050(B(ecx), a0, a1);
}
ABI_ENTRY_00494050(fn_00494050_Entry, fn_00494050_Abi)
HOOK(0x00494050, "fn_00494050", fn_00494050_Entry, nullptr);

// 0x00494160 (eax = screen, ecx = record id, ebx = value; stack: text name or 0, maximum, format; callee pops 0xc): sets
// one statistic line. The UI object of the screen's layer (+0x2c) with that record id (0x00463b90 eax = layer, ecx = -1;
// stack: HUD, id), when its virtual +0x150 has bit 5: v = min(maximum * +0x68, value) (unsigned to float), the text
// format(v, ratio * 100) (sprintf into a 0x100-byte frame buffer; ratio = v / maximum, 0 for value 0) goes to the label
// (Stepper_SetLabelA), the text name (UiText_SetName) when given, and the position (Stepper_SetPosition (ratio, or 0
// when the maximum is 0), 1). +0x68 is the reveal fraction. Not executed in recorded runs.
uint32_t fn_00494160(uint8_t* screen, uint32_t id, uint32_t value, uint32_t textName, uint32_t maximum, uint32_t format)
{
    uint8_t frame[0x108];  // [0] ratio, [4] maximum as float, [8..] the text
    uint8_t* obj = B(orig::fn_00463b90(u(screen + 0x2c), 0xffffffffu, Field<uint32_t>(G(), 0xa70), id));
    if (!obj)
        return 0;
    const uint32_t bits = VCall<uint32_t>(obj, 0x150 / 4);
    if (!(bits & 0x20))
        return bits;
    float fmax = static_cast<float>(static_cast<int32_t>(maximum));
    if (static_cast<int32_t>(maximum) < 0)
        fmax = fmax + K(0x005bcb28);
    float& ratio = *reinterpret_cast<float*>(frame);
    *reinterpret_cast<float*>(frame + 4) = fmax;
    float v = fmax * Field<float>(screen, 0x68);
    float fvalue = static_cast<float>(static_cast<int32_t>(value));
    if (static_cast<int32_t>(value) < 0)
        fvalue = fvalue + K(0x005bcb28);
    ratio = fvalue;
    if (!(v < ratio))
        v = ratio;
    if (value == 0)
        *reinterpret_cast<uint32_t*>(frame) = 0;
    else
        ratio = v / *reinterpret_cast<float*>(frame + 4);
    const int32_t percent = static_cast<int32_t>(ratio * K(0x005bcb60));
    const int32_t shown = static_cast<int32_t>(v);
    char* text = reinterpret_cast<char*>(frame + 8);
    GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(text, reinterpret_cast<const char*>(format), shown,
                                                                      percent);
    if (textName)
        orig::fn_00465850(textName, u(obj), 1);
    orig::fn_0046d110(u(obj), u(text));
    const uint32_t position = maximum == 0 ? 0u : *reinterpret_cast<uint32_t*>(frame);
    return orig::fn_0046ce70(u(obj), position, 1);
}

static uint32_t fn_00494160_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00494160(B(eax), ecx, ebx, a0, a1, a2);
}
ABI_ENTRY_00494160(fn_00494160_Entry, fn_00494160_Abi)
HOOK(0x00494160, "fn_00494160", fn_00494160_Entry, nullptr);

// 0x00494280 (eax = screen, ecx = record id, ebx = text name or 0; stack: amount (int); callee pops 4): sets one money
// line of the statistics screen: the UI object of the layer with that id (0x00463b90), when its virtual +0x150 has bit
// 10, gets the text "$%d" / "-$%d" (0x0058fbf4 / 0x0058fbf8) of ftol(amount * +0x68) (sprintf into a 0x40-byte frame
// buffer) through FitText_SetTextA (0x0046d5c0 eax = text, esi = object), after UiText_SetName with a text name. Not
// executed in recorded runs.
uint32_t fn_00494280(uint8_t* screen, uint32_t id, uint32_t textName, int32_t amount)
{
    char frame[0x40];
    uint8_t* obj = B(orig::fn_00463b90(u(screen + 0x2c), 0xffffffffu, Field<uint32_t>(G(), 0xa70), id));
    if (!obj)
        return 0;
    const uint32_t bits = VCall<uint32_t>(obj, 0x150 / 4);
    if (!(bits & 0x400))
        return bits;
    int32_t v = static_cast<int32_t>(static_cast<float>(amount) * Field<float>(screen, 0x68));
    typedef int(__cdecl * Sprintf)(char*, const char*, ...);
    if (v < 0)
        GameFunction<Sprintf>(0x0053814f)(frame, reinterpret_cast<const char*>(0x0058fbf8), -v);
    else
        GameFunction<Sprintf>(0x0053814f)(frame, reinterpret_cast<const char*>(0x0058fbf4), v);
    if (textName)
        orig::fn_00465850(textName, u(obj), 1);
    return orig::fn_0046d5c0(u(frame), u(obj));
}

static uint32_t fn_00494280_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t a0)
{
    return fn_00494280(B(eax), ecx, ebx, static_cast<int32_t>(a0));
}
ABI_ENTRY_00494280(fn_00494280_Entry, fn_00494280_Abi)
HOOK(0x00494280, "fn_00494280", fn_00494280_Entry, nullptr);

// 0x00494310 (esi = screen): fills the statistics screen: shots fired (0x26b0, g+0x2718 of g+0x2718, "%d"), shots hit
// (0x26b1, g+0x271c of g+0x2718, "%d (%d%%)"), enemies killed (0x26b2, g+0x270c of g+0x2710), head shots (0x26b3,
// g+0x2714 of g+0x270c) through 0x00494160, then the money lines kills / accidents / saved / loot (0x26b7..0x26ba,
// ftol of g+0x2724 / +0x2728 / +0x272c / +0x2730) through 0x00494280. Strings: 'ui_stats_*'. Not executed in recorded
// runs.
uint32_t fn_00494310(uint8_t* screen)
{
    uint8_t* g = G();
    fn_00494160(screen, 0x26b0, Field<uint32_t>(g, 0x2718), 0x0058fbe0, Field<uint32_t>(g, 0x2718), 0x005bbb78);
    g = G();
    fn_00494160(screen, 0x26b1, Field<uint32_t>(g, 0x271c), 0x0058fbc0, Field<uint32_t>(g, 0x2718), 0x0058fbd4);
    g = G();
    fn_00494160(screen, 0x26b2, Field<uint32_t>(g, 0x270c), 0x0058fba8, Field<uint32_t>(g, 0x2710), 0x005bbb78);
    g = G();
    fn_00494160(screen, 0x26b3, Field<uint32_t>(g, 0x2714), 0x0058fb94, Field<uint32_t>(g, 0x270c), 0x005bbb78);
    fn_00494280(screen, 0x26b7, 0x0058fb84, static_cast<int32_t>(Field<float>(G(), 0x2724)));
    fn_00494280(screen, 0x26b8, 0x0058fb70, static_cast<int32_t>(Field<float>(G(), 0x2728)));
    fn_00494280(screen, 0x26b9, 0x0058fb60, static_cast<int32_t>(Field<float>(G(), 0x272c)));
    return fn_00494280(screen, 0x26ba, 0x0058fb50, static_cast<int32_t>(Field<float>(G(), 0x2730)));
}

static uint32_t fn_00494310_Abi(uint32_t esi)
{
    return fn_00494310(B(esi));
}
ABI_ENTRY_00494310(fn_00494310_Entry, fn_00494310_Abi)
HOOK(0x00494310, "fn_00494310", fn_00494310_Entry, nullptr);

// 0x00494440 (ecx = screen; stack: mgr, input bits*; callee pops 8; vtable 0x0058fb14 slot 1): statistics screen
// update. +0x84 += dt; byte +0x88 = 0. Key 0x1b (0x00414730, threshold 0.5) or input bit 0x10 skips the current
// reveal (+0x68 = 1.0, 0x00414680 (eax = g+0xa80, ecx = 0x23), input bit 0x10 cleared). By phase +0x6c (jump table
// 0x00494620): 0 reveals the counters (+0x68 += dt * 0.3333, then 0x00494310), 2 the reward bar (+0x68 += 2 dt, select
// sound at 1.0; 0x00494870 on 0x0063a158), 3 the money (+0x68 += dt * 0.3333; g+0x570 = ftol(+0x70 + (+0x74 - +0x70) *
// +0x68), 0x00401f40 (g, 0)), 1/4 wait (byte +0x88 = 1), 5 pops the screen (0x00490b60). A reveal reaching 1.0 advances
// the phase. When the shown value (+0x80) changes and +0x84 >= 0.1, the move sound plays (tick). Then UiScreen_Update.
// Not executed in recorded runs.
uint32_t fn_00494440(uint8_t* screen, uint8_t* mgr, uint32_t* input)
{
    const uint32_t dtBits = FrameDtBits();
    const float dt = bf(dtBits);
    Field<float>(screen, 0x84) = dt + Field<float>(screen, 0x84);
    int32_t shown = Field<int32_t>(screen, 0x80);
    Field<uint8_t>(screen, 0x88) = 0;
    const uint32_t one = 0x3f800000u;
    if (static_cast<uint8_t>(orig::fn_00414730((dtBits & 0xffffff00u) | 1, 0x1b, Field<uint32_t>(G(), 0xa80),
                                               0x3f000000u)) ||
        (*reinterpret_cast<uint8_t*>(input) & 0x10))
    {
        if (Field<uint8_t>(screen, 0x88))
            orig::fn_00491830(0);
        Field<uint32_t>(screen, 0x68) = one;
        orig::fn_00414680(Field<uint32_t>(G(), 0xa80), 0x23);
        *input &= 0xffffffefu;
    }
    uint8_t* g = G();
    switch (Field<uint32_t>(screen, 0x6c))
    {
    case 0:
    {
        const float v = dt * K(0x005bcec4) + Field<float>(screen, 0x68);
        Field<float>(screen, 0x68) = v;
        if (!(v < K(0x005bcb2c)))
            Field<uint32_t>(screen, 0x68) = one;
        ++shown;
        fn_00494310(screen);
        break;
    }
    case 2:
    {
        const float v = (dt + dt) + Field<float>(screen, 0x68);
        Field<float>(screen, 0x68) = v;
        if (!(v < K(0x005bcb2c)))
        {
            orig::fn_00491830(0);
            Field<uint32_t>(screen, 0x68) = one;
        }
        const uint32_t object = GameGlobal<uint32_t>(0x0063a158);
        if (object)
            fn_00494870(B(object), Field<uint32_t>(screen, 0x68));
        break;
    }
    case 3:
    {
        const float v = dt * K(0x005bcec4) + Field<float>(screen, 0x68);
        Field<float>(screen, 0x68) = v;
        if (!(v < K(0x005bcb2c)))
            Field<uint32_t>(screen, 0x68) = one;
        const float from = static_cast<float>(Field<int32_t>(screen, 0x70));
        const float to = static_cast<float>(Field<int32_t>(screen, 0x74));
        const float money = (to - from) * Field<float>(screen, 0x68) + from;
        Field<int32_t>(g, 0x570) = static_cast<int32_t>(money);
        orig::fn_00401f40(u(g), 0);
        shown = Field<int32_t>(G(), 0x570);
        break;
    }
    case 1:
    case 4:
        Field<uint8_t>(screen, 0x88) = 1;
        break;
    case 5:
        return fn_00490b60(mgr);
    default:
        break;
    }
    if (!(Field<float>(screen, 0x68) < K(0x005bcb2c)))
    {
        ++Field<uint32_t>(screen, 0x6c);
        Field<uint32_t>(screen, 0x68) = 0;
    }
    if (shown != Field<int32_t>(screen, 0x80) && !(Field<float>(screen, 0x84) < K(0x005bcbfc)))
    {
        Field<uint32_t>(screen, 0x84) = 0;
        Field<int32_t>(screen, 0x80) = shown;
        orig::fn_00491890(0);
    }
    return UiScreen_Update(screen, mgr, input);
}

static uint32_t fn_00494440_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00494440(B(ecx), B(a0), reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_00494440(fn_00494440_Entry, fn_00494440_Abi)
HOOK(0x00494440, "fn_00494440", fn_00494440_Entry, nullptr);

// 0x00494790 (ecx = widget; stack: 4 arguments passed on; callee pops 0x10): wanted-reward widget init: base init
// 0x004651a0 (same arguments), 0x0063a158 = widget, +0x4f8 = 0, the texture "b_wanted_reward" (0x0058fb40) of bank
// g+0xa78 (TextureBank_Find) is stored at 0x0063a15c and its 9-dword record copied to 0x005fec80 (with +0xc, +0x1c, +0x18
// = 0 and word +0x22 = 0x80), or the record is cleared. Returns the texture. Not executed in recorded runs.
uint32_t fn_00494790(uint8_t* widget, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004651a0(u(widget), a0, a1, a2, a3);
    GameGlobal<uint8_t*>(0x0063a158) = widget;
    Field<uint32_t>(widget, 0x4f8) = 0;
    const uint32_t tex = orig::fn_004881c0(0x0058fb40, Field<uint32_t>(G(), 0xa78), 0, 0, 0, 0, 0, 0, 0, 0);
    GameGlobal<uint32_t>(0x0063a15c) = tex;
    uint32_t* record = reinterpret_cast<uint32_t*>(0x005fec80);
    if (tex)
    {
        std::memcpy(record, reinterpret_cast<const void*>(tex), 9 * 4);
        GameGlobal<uint32_t>(0x005fec8c) = 0;
        GameGlobal<uint32_t>(0x005fec9c) = 0;
        GameGlobal<uint32_t>(0x005fec98) = 0;
        GameGlobal<uint16_t>(0x005feca2) = 0x80;
        return tex;
    }
    std::memset(record, 0, 9 * 4);
    return 0;
}

static uint32_t fn_00494790_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00494790(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00494790(fn_00494790_Entry, fn_00494790_Abi)
HOOK(0x00494790, "fn_00494790", fn_00494790_Entry, nullptr);

// 0x00494820 (ecx = widget): when it is the wanted-reward widget (0x0063a158), releases the record 0x005fec80 through
// the renderer (g+0x38) virtual +0x128 (record, 1), clears it and 0x0063a158 / 0x0063a15c; then tail-calls 0x00465160
// (ecx = widget). Not executed in recorded runs.
uint32_t fn_00494820(uint8_t* widget)
{
    if (GameGlobal<uint8_t*>(0x0063a158) == widget)
    {
        VCall<uint32_t>(Field<void*>(G(), 0x38), 0x128 / 4, 0x005fec80u, 1u);
        std::memset(reinterpret_cast<void*>(0x005fec80), 0, 9 * 4);
        GameGlobal<uint32_t>(0x0063a158) = 0;
        GameGlobal<uint32_t>(0x0063a15c) = 0;
    }
    return orig::fn_00465160(u(widget));
}

static uint32_t fn_00494820_Abi(uint32_t ecx)
{
    return fn_00494820(B(ecx));
}
ABI_ENTRY_00494820(fn_00494820_Entry, fn_00494820_Abi)
HOOK(0x00494820, "fn_00494820", fn_00494820_Entry, nullptr);

// 0x00494870 (ecx = widget; stack: fraction (float); callee pops 4): +0x4f8 = the fraction clamped to 0..1 (0 when <= 0,
// 1.0 when >= 1). Not executed in recorded runs.
uint32_t fn_00494870(uint8_t* object, uint32_t arg)
{
    const float t = bf(arg);
    if (!(t > K(0x0056c05c)))
    {
        Field<uint32_t>(object, 0x4f8) = 0;
        return 0;
    }
    if (!(t < K(0x005bcb2c)))
    {
        Field<uint32_t>(object, 0x4f8) = 0x3f800000u;
        return 0;
    }
    if (!(t > K(0x0056c05c)))
    {
        Field<uint32_t>(object, 0x4f8) = 0;
        return 0;
    }
    Field<uint32_t>(object, 0x4f8) = arg;
    return arg;
}

static uint32_t fn_00494870_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00494870(B(ecx), a0);
}
ABI_ENTRY_00494870(fn_00494870_Entry, fn_00494870_Abi)
HOOK(0x00494870, "fn_00494870", fn_00494870_Entry, nullptr);

// ================================================================================================================
// Movie table, curves, text table
// ================================================================================================================

// 0x00495bd0 (ecx = path): the entry (0x28 bytes) of the table 0x0063a150 (count 0x0063a154) whose name equals the
// path's file name (_splitpath into a 0x100-byte frame buffer, _stricmp), or 0. guess: movie descriptions. Not executed
// in recorded runs.
uint32_t fn_00495bd0(const char* path)
{
    char frame[0x100];
    GameSplitpath(path, nullptr, nullptr, frame, nullptr);
    for (uint32_t i = 0; i < GameGlobal<uint32_t>(0x0063a154); ++i)
    {
        if (GameStricmp(frame, reinterpret_cast<const char*>(GameGlobal<uint32_t>(0x0063a150) + i * 0x28)) == 0)
            return GameGlobal<uint32_t>(0x0063a150) + i * 0x28;
    }
    return 0;
}

static uint32_t fn_00495bd0_Abi(uint32_t ecx)
{
    return fn_00495bd0(reinterpret_cast<const char*>(ecx));
}
ABI_ENTRY_00495bd0(fn_00495bd0_Entry, fn_00495bd0_Abi)
HOOK(0x00495bd0, "fn_00495bd0", fn_00495bd0_Entry, nullptr);

// 0x00495c50 (eax = table {+0x20 count, +0x24 16-byte entries {start, end, ...}}; stack: time (float); callee pops 4):
// binary search for the entry with start <= time <= end. Returns it or 0. guess: subtitle lines. Not executed in
// recorded runs.
uint32_t fn_00495c50(uint8_t* table, float t)
{
    int32_t hi = Field<int32_t>(table, 0x20) - 1;
    if (hi < 0)
        return 0;
    int32_t lo = 0;
    uint8_t* base = Field<uint8_t*>(table, 0x24);
    do
    {
        const int32_t mid = (hi + lo) / 2;
        uint8_t* e = base + mid * 16;
        if (Field<float>(e, 0) > t)
            hi = mid - 1;
        else if (!(Field<float>(e, 4) < t))
            return u(e);
        else
            lo = mid + 1;
    } while (lo <= hi);
    return 0;
}

static uint32_t fn_00495c50_Abi(uint32_t eax, uint32_t a0)
{
    return fn_00495c50(B(eax), bf(a0));
}
ABI_ENTRY_00495c50(fn_00495c50_Entry, fn_00495c50_Abi)
HOOK(0x00495c50, "fn_00495c50", fn_00495c50_Entry, nullptr);

// 0x00495e50 (ecx = text table): sets its fallback text to "Unknown Text" (0x0058f934, widened by 0x004e59d0 eax = 0x20,
// edi = string) and clears +0xa0/+0xa4/+0xa8/+0xac. Returns the table.
uint32_t fn_00495e50(uint8_t* table)
{
    orig::fn_004e59d0(0x20, u(table), 0x0058f934);
    Field<uint32_t>(table, 0xa8) = 0;
    Field<uint32_t>(table, 0xa4) = 0;
    Field<uint32_t>(table, 0xac) = 0;
    Field<uint32_t>(table, 0xa0) = 0;
    return u(table);
}

static uint32_t fn_00495e50_Abi(uint32_t ecx)
{
    return fn_00495e50(B(ecx));
}
ABI_ENTRY_00495e50(fn_00495e50_Entry, fn_00495e50_Abi)
HOOK(0x00495e50, "fn_00495e50", fn_00495e50_Entry, nullptr);

// 0x00495f00 Text_Lookup (eax = key, ebx = text table; stack: quiet (byte); callee pops 4): the UTF-16 text of the key
// (hash +0xa8 lookup 0x004f1160, string array +0xb0 [entry +0x20]). On failure the message "Text lookup failure: %s" /
// "...: null lookup" / "...: no text loaded" (0x0058f91c / 0x0058f8f8 / 0x0058f8d4) is built in a 0x50-byte frame
// buffer; quiet returns 0, otherwise the message is widened into the table's fallback text (0x004e59d0 eax = 0x50) and
// the table (its fallback text at +0) is returned. Name from the tracker; strings.
uint32_t Text_Lookup(const char* key, uint8_t* table, uint32_t quiet)
{
    char frame[0x50];
    const uint32_t hash = Field<uint32_t>(table, 0xa8);
    if (hash)
    {
        if (key && *key)
        {
            const uint32_t entry = orig::fn_004f1160(hash, u(key));
            if (entry)
                return Field<uint32_t*>(table, 0xb0)[Field<uint32_t>(B(entry), 0x20)];
            GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
                frame, reinterpret_cast<const char*>(0x0058f91c), key);
        }
        else
            std::memcpy(frame, reinterpret_cast<const void*>(0x0058f8f8), 8 * 4 + 1);
    }
    else
        std::memcpy(frame, reinterpret_cast<const void*>(0x0058f8d4), 9 * 4);
    if (static_cast<uint8_t>(quiet))
        return 0;
    orig::fn_004e59d0(0x50, u(table), u(frame));
    return u(table);
}

static uint32_t Text_Lookup_Abi(uint32_t eax, uint32_t ebx, uint32_t a0)
{
    return Text_Lookup(reinterpret_cast<const char*>(eax), B(ebx), a0);
}
ABI_ENTRY_00495f00(Text_Lookup_Entry, Text_Lookup_Abi)
HOOK(0x00495f00, "Text_Lookup", Text_Lookup_Entry, nullptr);

// ================================================================================================================
// Profile / unlocks / save slots
// ================================================================================================================

// 0x004a0670 (ecx = game, edx = index): true when the dword game+0x1f8 + index * 4 is non-zero. guess: mission set
// unlocked. Not executed in recorded runs.
uint32_t fn_004a0670(uint8_t* game, uint32_t index)
{
    return Field<uint32_t>(game, 0x1f8 + index * 4) != 0 ? 1u : 0u;
}

static uint32_t fn_004a0670_Abi(uint32_t ecx, uint32_t edx)
{
    return fn_004a0670(B(ecx), edx);
}
ABI_ENTRY_004a0670(fn_004a0670_Entry, fn_004a0670_Abi)
HOOK(0x004a0670, "fn_004a0670", fn_004a0670_Entry, nullptr);

// 0x004a0850 (ecx = game): token reward message. Builds at 0x0064ab00 "token_got" (Text_Lookup, table +0xac0) formatted
// with the token count +0x200 and 0x005cadd0 (game's swprintf 0x0053862e). Walks the 20 unlockables of the table
// 0x0057e878 (0x1c bytes: type 0 level / 1 cheat / 2 other, id, description key, cost per difficulty g+0x550; cost 99 =
// none) that are still locked (0x004a0720 ecx = 3/4, ebx = game; Cheat_IsUnlocked): affordable ones unlock the dearest
// so far (0x004a07a0 eax = id, ecx = game, edi = 3/4; stack: 1, or Cheat_Unlock (ebx = id; stack: 0)), the cheapest
// unaffordable one is remembered. Appends "<br>" and "token_justunlocked" (name, description) for the unlocked one, or
// "token_tounlock_single" / "token_tounlock" (count, name, description) for the next one (names through Text_Lookup,
// else widened into a 0x100-wchar frame buffer). Then g+0x2730 += 2500.0, g+0xae6 = 1, the message is shown
// (0x00403410 ecx = game; stack: text, 1) and 0x00401e60 (esi = game). Strings: 'token_*'.
uint32_t fn_004a0850(uint8_t* game)
{
    uint32_t L[8];  // the original's frame slots 0x00..0x1c
    uint16_t buffer[0x100];
    typedef int(__cdecl * Swprintf)(uint16_t*, const uint16_t*, ...);
    const Swprintf swp = GameFunction<Swprintf>(0x0053862e);
    const uint32_t extra = GameGlobal<uint32_t>(0x005cadd0);
    const uint32_t diff = Field<uint32_t>(G(), 0x550);
    const uint32_t tokens = Field<uint32_t>(game, 0x200);
    uint32_t table = Field<uint32_t>(game, 0xac0);
    L[6] = tokens;
    L[3] = diff;
    L[2] = 0;
    L[4] = 0;
    uint8_t* best = nullptr;  // edi
    const uint32_t fmt = Text_Lookup(reinterpret_cast<const char*>(0x0058ea54), B(table), 0);
    uint16_t* const out = reinterpret_cast<uint16_t*>(0x0064ab00);
    const int n = swp(out, reinterpret_cast<const uint16_t*>(fmt), tokens, extra);
    L[1] = u(out + n);
    uint8_t* entry = reinterpret_cast<uint8_t*>(0x0057e878);
    for (int i = 0; i < 0x14; ++i, entry += 0x1c)
    {
        const uint32_t cost = Field<uint32_t>(entry, 0xc + diff * 4);
        L[5] = cost;
        if (cost == 0x63)
            continue;
        uint32_t unlocked;
        switch (Field<uint32_t>(entry, 0))
        {
        case 0:
            unlocked = orig::fn_004a0720(Field<uint32_t>(entry, 4), 3, u(game));
            break;
        case 1:
            unlocked = orig::fn_00410ea0(Field<uint32_t>(entry, 4));
            break;
        case 2:
            unlocked = orig::fn_004a0720(Field<uint32_t>(entry, 4), 4, u(game));
            break;
        default:
            continue;
        }
        if (static_cast<uint8_t>(unlocked))
            continue;
        if (tokens >= cost)
        {
            if (!best || cost > Field<uint32_t>(best, 0xc + diff * 4))
            {
                L[2] = u(entry);
                best = entry;
            }
            switch (Field<uint32_t>(best, 0))
            {
            case 0:
                orig::fn_004a07a0(Field<uint32_t>(best, 4), u(game), 3, 1);
                best = B(L[2]);
                break;
            case 1:
                orig::fn_00410e40(Field<uint32_t>(best, 4), 0);
                break;
            case 2:
                orig::fn_004a07a0(Field<uint32_t>(best, 4), u(game), 4, 1);
                best = B(L[2]);
                break;
            default:
                break;
            }
        }
        else
        {
            uint8_t* next = B(L[4]);
            if (!next || cost < Field<uint32_t>(next, 0xc + diff * 4))
                L[4] = u(entry);
        }
    }
    uint32_t chosen;
    if (!best)
        chosen = L[4];
    else
        chosen = u(best);
    L[0] = chosen;
    uint32_t name;
    uint32_t desc;
    if (chosen)
    {
        table = Field<uint32_t>(game, 0xac0);
        name = Text_Lookup(Field<const char*>(B(chosen), 4), B(table), 0);
        if (!name)
        {
            orig::fn_004e59d0(0x100, u(buffer), Field<uint32_t>(B(L[0]), 4));
            best = B(L[2]);
            name = u(buffer);
        }
        desc = Text_Lookup(Field<const char*>(B(L[0]), 8), B(Field<uint32_t>(game, 0xac0)), 0);
    }
    else
    {
        name = L[5];
        desc = L[5];
    }
    if (best)
    {
        uint16_t* end = reinterpret_cast<uint16_t*>(L[1]);
        const int len = swp(end, reinterpret_cast<const uint16_t*>(0x0058ea48));
        uint16_t* dst = end + len;
        const uint32_t f = Text_Lookup(reinterpret_cast<const char*>(0x0058ea34), B(Field<uint32_t>(game, 0xac0)), 0);
        swp(dst, reinterpret_cast<const uint16_t*>(f), name, desc);
    }
    else if (L[4])
    {
        const uint32_t count = Field<uint32_t>(B(L[4]), 0xc + L[3] * 4) - L[6];
        const int len = swp(reinterpret_cast<uint16_t*>(L[1]), reinterpret_cast<const uint16_t*>(0x0058ea48));
        L[1] = u(reinterpret_cast<uint16_t*>(L[1]) + len);
        if (count == 1)
        {
            const uint32_t f =
                Text_Lookup(reinterpret_cast<const char*>(0x0058ea1c), B(Field<uint32_t>(game, 0xac0)), 0);
            swp(reinterpret_cast<uint16_t*>(L[1]), reinterpret_cast<const uint16_t*>(f), name, desc);
        }
        else
        {
            const uint32_t f =
                Text_Lookup(reinterpret_cast<const char*>(0x0058ea0c), B(Field<uint32_t>(game, 0xac0)), 0);
            swp(reinterpret_cast<uint16_t*>(L[1]), reinterpret_cast<const uint16_t*>(f), count, name, desc);
        }
    }
    uint8_t* g = G();
    Field<float>(g, 0x2730) = Field<float>(g, 0x2730) + K(0x005bccb8);
    Field<uint8_t>(g, 0xae6) = 1;
    orig::fn_00403410(u(game), 0x0064ab00, 1);
    return orig::fn_00401e60(u(game));
}

static uint32_t fn_004a0850_Abi(uint32_t ecx)
{
    return fn_004a0850(B(ecx));
}
ABI_ENTRY_004a0850(fn_004a0850_Entry, fn_004a0850_Abi)
HOOK(0x004a0850, "fn_004a0850", fn_004a0850_Entry, nullptr);

// 0x004a2ba0 (ebx = passed on to Music_Update): Music_Update 0x0044f580 (ebx; stack: g+0xa88, 0), then (g+0x2c) virtual
// +0x130 (g+0x704, 0, 1). guess: keeps music / sound alive during slow save-game I/O.
uint32_t fn_004a2ba0(uint32_t ebx)
{
    orig::fn_0044f580(ebx, Field<uint32_t>(G(), 0xa88), 0);
    uint8_t* g = G();
    return VCall<uint32_t>(Field<void*>(g, 0x2c), 0x130 / 4, u(g + 0x704), 0u, 1u);
}

static uint32_t fn_004a2ba0_Abi(uint32_t ebx)
{
    return fn_004a2ba0(ebx);
}
ABI_ENTRY_004a2ba0(fn_004a2ba0_Entry, fn_004a2ba0_Abi)
HOOK(0x004a2ba0, "fn_004a2ba0", fn_004a2ba0_Entry, nullptr);

// 0x004a2c10 (eax = save slot 1..6 or -1; stack: UTF-16 name output or 0; caller pops): checks a save slot. Other slots
// clear the output and return al = 0. Otherwise, inside 0x0044ad50 (al = 1) / (al = 0) brackets, 0x0048b370 (esi =
// callback 0x004a2bd0; stack: slot, output, 0) reports whether the slot holds a save; when not, the output gets the
// default name "ui_savegame_slot%d" (sprintf into the 0x40-byte frame buffer, Text_Lookup, 0x00403be0). Returns al =
// 0x0048b370's result. Strings: 'ui_savegame_slot%d'.
uint32_t fn_004a2c10(uint32_t slot, uint16_t* out)
{
    char frame[0x44];
    if (slot != 0xffffffffu && (slot == 0 || slot > 6))
    {
        if (out)
            *out = 0;
        return slot & 0xffffff00u;
    }
    orig::fn_0044ad50((slot & 0xffffff00u) | 1);
    const uint8_t found = static_cast<uint8_t>(orig::fn_0048b370(0x004a2bd0, slot, u(out), 0));
    frame[3] = static_cast<char>(found);
    if (!found && out)
    {
        char* text = frame + 4;
        GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
            text, reinterpret_cast<const char*>(0x0058e8ec), slot);
        const uint32_t s = Text_Lookup(text, Field<uint8_t*>(G(), 0xac0), 0);
        orig::fn_00403be0(s, u(out), 0);
    }
    const uint32_t r = orig::fn_0044ad50(0);
    return (r & 0xffffff00u) | found;
}

static uint32_t fn_004a2c10_Abi(uint32_t eax, uint32_t a0)
{
    return fn_004a2c10(eax, reinterpret_cast<uint16_t*>(a0));
}
ABI_ENTRY_004a2c10(fn_004a2c10_Entry, fn_004a2c10_Abi)
HOOK(0x004a2c10, "fn_004a2c10", fn_004a2c10_Entry, nullptr);

uint32_t fn_004a2fe0(uint8_t* widget);

// 0x004a2f70 (ecx = save slot widget; stack: 4 arguments passed on; callee pops 0x10): base init 0x004651a0, then byte
// +0x4f8 = 1, +0x4f9 = 0, +0x5a8 = +0x5ac = +0x580 = 0, words +0x5b0 / +0x6b0 = 0, and the thumbnail is released
// (0x004a2fe0). Not executed in recorded runs.
uint32_t fn_004a2f70(uint8_t* widget, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004651a0(u(widget), a0, a1, a2, a3);
    Field<uint8_t>(widget, 0x4f8) = 1;
    Field<uint8_t>(widget, 0x4f9) = 0;
    Field<uint32_t>(widget, 0x5a8) = 0;
    Field<uint32_t>(widget, 0x5ac) = 0;
    Field<uint16_t>(widget, 0x5b0) = 0;
    Field<uint16_t>(widget, 0x6b0) = 0;
    Field<uint32_t>(widget, 0x580) = 0;
    return fn_004a2fe0(widget);
}

static uint32_t fn_004a2f70_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004a2f70(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004a2f70(fn_004a2f70_Entry, fn_004a2f70_Abi)
HOOK(0x004a2f70, "fn_004a2f70", fn_004a2f70_Entry, nullptr);

// 0x004a2fd0 (ecx = save slot widget): releases the thumbnail (0x004a2fe0), then tail-calls 0x00465160 (ecx = widget).
uint32_t fn_004a2fd0(uint8_t* widget)
{
    fn_004a2fe0(widget);
    return orig::fn_00465160(u(widget));
}

static uint32_t fn_004a2fd0_Abi(uint32_t ecx)
{
    return fn_004a2fd0(B(ecx));
}
ABI_ENTRY_004a2fd0(fn_004a2fd0_Entry, fn_004a2fd0_Abi)
HOOK(0x004a2fd0, "fn_004a2fd0", fn_004a2fd0_Entry, nullptr);

// 0x004a2fe0 (esi = save slot widget): releases the thumbnail texture +0x5a8 through the renderer (g+0x38) virtual
// +0x128 (texture, 1), clears +0x5a8 and the 9-dword record +0x584. Returns 0.
uint32_t fn_004a2fe0(uint8_t* widget)
{
    const uint32_t tex = Field<uint32_t>(widget, 0x5a8);
    if (tex)
        VCall<uint32_t>(Field<void*>(G(), 0x38), 0x128 / 4, tex, 1u);
    Field<uint32_t>(widget, 0x5a8) = 0;
    std::memset(widget + 0x584, 0, 9 * 4);
    return 0;
}

static uint32_t fn_004a2fe0_Abi(uint32_t esi)
{
    return fn_004a2fe0(B(esi));
}
ABI_ENTRY_004a2fe0(fn_004a2fe0_Entry, fn_004a2fe0_Abi)
HOOK(0x004a2fe0, "fn_004a2fe0", fn_004a2fe0_Entry, nullptr);

namespace
{
// 0x004a3260 (eax = widget): positions a save slot widget and ends in a tail jump to its virtual +0x144 with no stack
// arguments (recovered in src/renderer/render_r6.cpp; no generated orig wrapper: "no ret found"). Called directly.
uint32_t Fn004a3260(uint32_t eaxValue)
{
    static const uint32_t target = 0x004a3260;
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov eax, eaxValue
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}
}  // namespace

// 0x004a3020 (stack: save slot widget, save mode (byte); callee pops 8): fills a save slot widget. Releases the old
// thumbnail (0x004a2fe0), lays it out (0x004a3260), byte +0x4fa = save mode, +0x4f8/+0x4f9/+0x4fb = 0, +0x5ac = +0x580 =
// 0, and asks 0x004a2c10 (eax = slot +0x4fc; stack: name +0x500). An empty slot gets +0x4f8 = 1 and, in save mode, is
// remembered as the default selection 0x0063a13c when none is. An existing one (+0x4f9 = 1 while loading) checked by
// 0x004a3930 (eax = name) gets: +0x730 = "ui_savegame_tokens" formatted with g+0x200 and 0x005cadd0 (swprintf), the save
// header read by 0x0048c870 (eax = header in the frame; stack: g+0xab8, name, &+0x584, 0) with 0x004a2ba0 before and
// after, +0x5a8 = &+0x584 (thumbnail), the level name +0x5b0 (0x0044a610 eax = header, esi = g+0xa64, edi = +0x5b0),
// +0x5ac = Text_Lookup("ui_diff_%d" of header difficulty + 1), the file time +0x7b0/+0x7b4 and +0x6b0 = its local date
// "%d/%02d/%02d %d:%02d:%02d" (month, day, year % 100, hour, minute, second). Loading, the newest save becomes
// 0x0063a13c. Strings: 'ui_savegame_tokens', 'ui_diff_%d'.
uint32_t fn_004a3020(uint8_t* widget, uint32_t flag)
{
    uint8_t frame[0x17c];
    std::memset(frame, 0, sizeof(frame));
    fn_004a2fe0(widget);
    Fn004a3260(u(widget));
    Field<uint8_t>(widget, 0x4fa) = static_cast<uint8_t>(flag);
    uint16_t* name = &Field<uint16_t>(widget, 0x500);
    Field<uint8_t>(widget, 0x4f8) = 0;
    Field<uint8_t>(widget, 0x4f9) = 0;
    Field<uint8_t>(widget, 0x4fb) = 0;
    Field<uint32_t>(widget, 0x5ac) = 0;
    Field<uint32_t>(widget, 0x580) = 0;
    if (!static_cast<uint8_t>(fn_004a2c10(Field<uint32_t>(widget, 0x4fc), name)))
    {
        Field<uint8_t>(widget, 0x4f8) = 1;
        if (Field<uint8_t>(widget, 0x4fa) && GameGlobal<uint32_t>(0x0063a13c) == 0)
            GameGlobal<uint8_t*>(0x0063a13c) = widget;
        return 0;
    }
    Field<uint8_t>(widget, 0x4f9) = 1;
    if (!static_cast<uint8_t>(orig::fn_004a3930(u(name))))
        return 0;
    typedef int(__cdecl * Swprintf)(uint16_t*, const uint16_t*, ...);
    const Swprintf swp = GameFunction<Swprintf>(0x0053862e);
    uint8_t* g = G();
    const uint32_t extra = GameGlobal<uint32_t>(0x005cadd0);
    const uint32_t tokens = Field<uint32_t>(g, 0x200);
    const uint32_t table = Field<uint32_t>(g, 0xac0);
    const uint32_t fmt = Text_Lookup(reinterpret_cast<const char*>(0x0058e8c0), B(table), 0);
    swp(&Field<uint16_t>(widget, 0x730), reinterpret_cast<const uint16_t*>(fmt), tokens, extra);
    fn_004a2ba0(table);
    uint8_t* header = frame + 0x58;
    if (!static_cast<uint8_t>(orig::fn_0048c870(u(header), Field<uint32_t>(G(), 0xab8), u(name), u(widget + 0x584), 0)))
        return 0;
    Field<uint32_t>(widget, 0x5a8) = u(widget + 0x584);
    fn_004a2ba0(table);
    orig::fn_0044a610(u(header), Field<uint32_t>(G(), 0xa64), u(widget + 0x5b0));
    char* diffKey = reinterpret_cast<char*>(frame + 0x18);
    GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
        diffKey, reinterpret_cast<const char*>(0x0058e8b4), *reinterpret_cast<uint32_t*>(frame + 0x160) + 1);
    const uint32_t diffText = Text_Lookup(diffKey, Field<uint8_t*>(G(), 0xac0), 0);
    Field<uint32_t>(widget, 0x7b0) = *reinterpret_cast<uint32_t*>(frame + 0x16c);
    Field<uint32_t>(widget, 0x5ac) = diffText;
    Field<uint32_t>(widget, 0x7b4) = *reinterpret_cast<uint32_t*>(frame + 0x170);
    FILETIME* local = reinterpret_cast<FILETIME*>(frame + 0x10);
    SYSTEMTIME* st = reinterpret_cast<SYSTEMTIME*>(frame);
    Import<BOOL(WINAPI*)(const FILETIME*, LPFILETIME)>(0x0056b074)(reinterpret_cast<const FILETIME*>(frame + 0x16c),
                                                                    local);
    Import<BOOL(WINAPI*)(const FILETIME*, LPSYSTEMTIME)>(0x0056b070)(local, st);
    swp(&Field<uint16_t>(widget, 0x6b0), reinterpret_cast<const uint16_t*>(0x0058e880), static_cast<uint32_t>(st->wMonth),
        static_cast<uint32_t>(st->wDay), static_cast<int32_t>(st->wYear) % 100, static_cast<uint32_t>(st->wHour),
        static_cast<uint32_t>(st->wMinute), static_cast<uint32_t>(st->wSecond));
    Field<uint8_t>(widget, 0x4f9) = 0;
    if (Field<uint8_t>(widget, 0x4fa))
        return 0;
    uint8_t* newest = GameGlobal<uint8_t*>(0x0063a13c);
    if (newest)
    {
        const uint32_t hi = Field<uint32_t>(widget, 0x7b4);
        const uint32_t otherHi = Field<uint32_t>(newest, 0x7b4);
        if (hi < otherHi)
            return 0;
        if (hi == otherHi && Field<uint32_t>(widget, 0x7b0) <= Field<uint32_t>(newest, 0x7b0))
            return 0;
    }
    GameGlobal<uint8_t*>(0x0063a13c) = widget;
    return u(widget);
}

static uint32_t fn_004a3020_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004a3020(B(a0), a1);
}
ABI_ENTRY_004a3020(fn_004a3020_Entry, fn_004a3020_Abi)
HOOK(0x004a3020, "fn_004a3020", fn_004a3020_Entry, nullptr);

// 0x004a36c0 (ecx = save slot widget; stack: one argument passed on; callee pops 4): in save mode (+0x4fa) for an empty
// slot (+0x4f8), +0x4fb = 0x00433fa0 (0x20) (guess: enough free disk space); then tail-calls 0x00465130 (ecx = widget,
// same argument). Not executed in recorded runs.
uint32_t fn_004a36c0(uint8_t* widget, uint32_t arg)
{
    if (Field<uint8_t>(widget, 0x4f8) && Field<uint8_t>(widget, 0x4fa))
        Field<uint8_t>(widget, 0x4fb) = static_cast<uint8_t>(orig::fn_00433fa0(0x20));
    return orig::fn_00465130(u(widget), arg);
}

static uint32_t fn_004a36c0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004a36c0(B(ecx), a0);
}
ABI_ENTRY_004a36c0(fn_004a36c0_Entry, fn_004a36c0_Abi)
HOOK(0x004a36c0, "fn_004a36c0", fn_004a36c0_Entry, nullptr);

// 0x004a36f0 (ecx = save slot widget; stack: screen, input bits*; callee pops 8): input of a save slot. Without input bit
// 0x10, 0x00464a10 (ecx = widget; same arguments). Accept in save mode: an occupied slot asks "ui_yesno_overwrite"
// (0x0058e7d8), an empty one with space (+0x4fb) is saved through the screen's virtual +0x20 (widget), else the cancel
// sound. Accept in load mode: an empty slot gives the cancel sound; one marked +0x4f9 asks "ui_yesno_delete"
// (0x0058e7c8); otherwise the slot name goes to the level name (0x00490850, ecx = g+0xab4) and the screen's virtual
// +0x20 (widget) loads it. Prompts: UI_PushYesNo (ecx = g+0xab4, edx = 0; stack: 1, widget, callback 0x004a2e10, 0, 0,
// 0). Returns al = 1 when handled. Not executed in recorded runs.
uint32_t fn_004a36f0(uint8_t* widget, uint8_t* screen, uint32_t* input)
{
    if (!(*reinterpret_cast<uint8_t*>(input) & 0x10))
        return orig::fn_00464a10(u(widget), u(screen), u(input));
    uint32_t prompt;
    if (Field<uint8_t>(widget, 0x4fa))
    {
        if (Field<uint8_t>(widget, 0x4f8))
        {
            if (Field<uint8_t>(widget, 0x4fb))
            {
                orig::fn_00491850(0);
                VCall<uint32_t>(screen, 0x20 / 4, u(widget));
                return 1;
            }
            orig::fn_00491870(0);
            return 1;
        }
        orig::fn_00491850(0);
        prompt = 0x0058e7d8;
    }
    else
    {
        if (Field<uint8_t>(widget, 0x4f8))
        {
            orig::fn_00491870(0);
            return 1;
        }
        orig::fn_00491850(0);
        if (!Field<uint8_t>(widget, 0x4f9))
        {
            if (!Field<uint8_t>(widget, 0x4f8))
            {
                fn_00490850(&Field<uint16_t>(widget, 0x500), Field<uint8_t*>(G(), 0xab4));
                VCall<uint32_t>(screen, 0x20 / 4, u(widget));
            }
            return 1;
        }
        prompt = 0x0058e7c8;
    }
    UI_PushYesNo(prompt, Field<uint8_t*>(G(), 0xab4), 0, 1, u(widget), 0x004a2e10, 0, 0, 0);
    return 1;
}

static uint32_t fn_004a36f0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004a36f0(B(ecx), B(a0), reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_004a36f0(fn_004a36f0_Entry, fn_004a36f0_Abi)
HOOK(0x004a36f0, "fn_004a36f0", fn_004a36f0_Entry, nullptr);

// 0x004a37e0 (ecx = save slot widget): layout: +0x474 &= ~0x2000, the bounds of (-96, 0) .. (96, -184) pixels around the
// widget (Ui_PixelToScreen 0x0046b0a0 into the 0x10-byte frame) reset its box (UiText_ExtendBounds 0x00465470, al = 1;
// the eax upper bytes are the first point's y). Not executed in recorded runs.
uint32_t fn_004a37e0(uint8_t* widget)
{
    uint32_t frame[4];  // [0]/[1] second point y/x, [2]/[3] first point y/x
    Field<uint32_t>(widget, 0x474) = Field<uint32_t>(widget, 0x474) & 0xffffdfffu;
    orig::fn_0046b0a0(u(widget), u(&frame[3]), u(&frame[2]), static_cast<uint32_t>(-0x60), 0);
    orig::fn_0046b0a0(u(widget), u(&frame[1]), u(&frame[0]), 0x60, static_cast<uint32_t>(-0xb8));
    return orig::fn_00465470((frame[3] & 0xffffff00u) | 1, u(widget), frame[3], frame[2], frame[1], frame[0]);
}

static uint32_t fn_004a37e0_Abi(uint32_t ecx)
{
    return fn_004a37e0(B(ecx));
}
ABI_ENTRY_004a37e0(fn_004a37e0_Entry, fn_004a37e0_Abi)
HOOK(0x004a37e0, "fn_004a37e0", fn_004a37e0_Entry, nullptr);

// 0x004a3a40 (ecx = screen; stack: argument, show (byte); callee pops 8; vtable 0x0058fc50 slot 2): base show; when
// shown, +0xa8 = 0 (phase) and the name g+0x510 is copied to +0x68 (0x00403be0, edi = 0). guess: the save-progress
// screen. Not executed in recorded runs.
uint32_t fn_004a3a40(uint8_t* screen, uint32_t arg, uint32_t show)
{
    const uint32_t r = fn_004918b0(screen, arg, show);
    if (!static_cast<uint8_t>(show))
        return r;
    Field<uint32_t>(screen, 0xa8) = 0;
    return orig::fn_00403be0(u(G() + 0x510), u(screen + 0x68), 0);
}

static uint32_t fn_004a3a40_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004a3a40(B(ecx), a0, a1);
}
ABI_ENTRY_004a3a40(fn_004a3a40_Entry, fn_004a3a40_Abi)
HOOK(0x004a3a40, "fn_004a3a40", fn_004a3a40_Entry, nullptr);

// 0x004a3a80 (ecx = screen; stack: mgr, input; callee pops 8; vtable 0x0058fc50 slot 1): UiScreen_Update, then by phase
// +0xa8 (jump table 0x004a3b44): 0 waits until +0x50 >= 0.5, 1 -> 2, 2 saves (0x0048c620 eax = &+0x68, esi = g+0xab8),
// 3 runs 0x004a3840 (eax = 0; its ecx is only a frame slot: 0) and g+0x56c += 1; each advances the phase. Afterwards
// (phase > 3) the screen returns to "PauseMenu" (0x00490b80) once +0x50 reaches 1.0 (+0x4c bit 2) or 3.0. Not executed
// in recorded runs.
uint32_t fn_004a3a80(uint8_t* screen, uint8_t* mgr, uint32_t* input)
{
    uint32_t r = UiScreen_Update(screen, mgr, input);
    switch (Field<uint32_t>(screen, 0xa8))
    {
    case 0:
        if (!(Field<float>(screen, 0x50) < K(0x005bcb4c)))
            Field<uint32_t>(screen, 0xa8) = 1;
        return r;
    case 1:
        Field<uint32_t>(screen, 0xa8) = 2;
        return r;
    case 2:
        r = orig::fn_0048c620(u(screen + 0x68), Field<uint32_t>(G(), 0xab8));
        ++Field<uint32_t>(screen, 0xa8);
        return r;
    case 3:
        orig::fn_004a3840(0, 0);
        ++Field<uint32_t>(G(), 0x56c);
        ++Field<uint32_t>(screen, 0xa8);
        return r;
    default:
        break;
    }
    const float limit = (Field<uint8_t>(screen, 0x4c) & 4) ? K(0x005bcb2c) : K(0x005bcb7c);
    if (limit <= Field<float>(screen, 0x50))
        r = fn_00490b80(reinterpret_cast<const char*>(0x005bc138), mgr);
    return r;
}

static uint32_t fn_004a3a80_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004a3a80(B(ecx), B(a0), reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_004a3a80(fn_004a3a80_Entry, fn_004a3a80_Abi)
HOOK(0x004a3a80, "fn_004a3a80", fn_004a3a80_Entry, nullptr);

// 0x004a3b60 (ecx = screen; stack: argument, show (byte); callee pops 8; vtable 0x0058fc2c slot 2): save / load screen
// show. 0x0044ad50 (al = show). Shown: the argument string (or "") is copied to +0x68 and the title becomes
// "ui_savemenu_title" for the screen "SaveGame", else "ui_loadmenu_title" (passed to the base show instead of the
// argument). 0x0063a13c (default slot) = 0, base show (0x004918b0; the slots fill it through 0x004a3020), then the
// default slot is selected (0x00491b20). Strings.
uint32_t fn_004a3b60(uint8_t* screen, uint32_t arg, uint32_t show)
{
    orig::fn_0044ad50(static_cast<uint8_t>(show));
    uint32_t title = arg;
    if (static_cast<uint8_t>(show))
    {
        const char* s = arg ? reinterpret_cast<const char*>(arg) : reinterpret_cast<const char*>(0x0058a9a4);
        std::strcpy(reinterpret_cast<char*>(screen + 0x68), s);
        if (GameStricmp(reinterpret_cast<const char*>(screen + 0xc), reinterpret_cast<const char*>(0x00590118)) != 0)
            title = 0x0058e7a4;
        else
            title = 0x0058e790;
    }
    GameGlobal<uint32_t>(0x0063a13c) = 0;
    uint32_t r = fn_004918b0(screen, title, show);
    uint8_t* slot = GameGlobal<uint8_t*>(0x0063a13c);
    if (slot)
        r = fn_00491b20(slot, screen);
    return r;
}

static uint32_t fn_004a3b60_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004a3b60(B(ecx), a0, a1);
}
ABI_ENTRY_004a3b60(fn_004a3b60_Entry, fn_004a3b60_Abi)
HOOK(0x004a3b60, "fn_004a3b60", fn_004a3b60_Entry, nullptr);

// 0x004a3bf0 (ecx = screen; stack: save slot widget, callee pops 4; vtable 0x0058fc2c slot 8, called by
// UiScreen_Activate q 73 with 0): 0x004a2de0 (esi = widget); then with a next screen name +0x68 tail-calls
// UI_PushScreen (eax = 0, ecx = g+0xab4; stack: &+0x68 replacing the argument), else the cancel sound and a pop
// (0x00490b60, ecx = g+0xab4). guess: performs the save / load.
uint32_t fn_004a3bf0(uint8_t* screen, uint32_t widget)
{
    orig::fn_004a2de0(widget);
    if (Field<uint8_t>(screen, 0x68))
        return UI_PushScreen(0, Field<uint8_t*>(G(), 0xab4), u(screen + 0x68));
    orig::fn_00491870(0);
    return fn_00490b60(Field<uint8_t*>(G(), 0xab4));
}

static uint32_t fn_004a3bf0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004a3bf0(B(ecx), a0);
}
ABI_ENTRY_004a3bf0(fn_004a3bf0_Entry, fn_004a3bf0_Abi)
HOOK(0x004a3bf0, "fn_004a3bf0", fn_004a3bf0_Entry, nullptr);
