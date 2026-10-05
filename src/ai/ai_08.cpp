// Recovered: batch ai_08 (0x00483250-0x0049d7a0): the Q111 / Molmer tower pair (Q111Object vtable 0x005a6df8,
// MolmerObject 0x005ae5f8: two characters on the nav mesh that keep formation around each other, "ref_molmer_00" /
// "ref_q_00" bones, "Molmer_Tower_Failure"), ClothObject (0x005aca40), SheepObject (0x005a8d00), PenguinObject
// (0x005a1768), SoundEar (0x005a2d28), TwigletController (0x005a7590), TwigletGenerator (0x005aed80) and Twiglet
// (0x005af020). Vtables lie back to back in .rdata; each start was found from the slot-1 descriptor getter
// (`mov eax, desc; ret`, desc+0x14 = "CLASS(Name)"), as in ai_04.cpp.
// Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still to
// do.
//
// Conventions (as in ai_01.cpp / ai_04.cpp / ai_06.cpp): every function's comment gives its address, its register /
// stack ABI (as the original reads it), what it does and the evidence. "guess" marks interpretations not proven by
// strings or behaviour. Virtual methods are named Class_SlotN (N = decimal slot index) when the class is known from
// the vtable; a method shared by Q111Object and MolmerObject is named after Q111Object (the lower vtable). Other names
// stay fn_XXXXXXXX. Floats follow the disassembly's x87 operation order in single precision.
//
// Callee notes: 0x004e7880 returns st0 (listed eax) -> Normalize3 (inline asm, pops st0). The "ecx" input of
// 0x0046e2c0 and 0x0042d0f0 is only a `push ecx` stack-slot reservation (passed as 0). 0x00457870 reads no stack
// arguments (ABI: 3; passed as 0). Where the original passes a register whose low byte it just set (mov al, 1 /
// xor al, al) the upper bytes are passed as the original left them when known.
#include <cmath>
#include <cstring>

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
inline void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
// Memory at an absolute address (object fields: D(self + 0xbcc)).
inline uint32_t& D(uint32_t a)
{
    return *reinterpret_cast<uint32_t*>(a);
}
inline int32_t& I(uint32_t a)
{
    return *reinterpret_cast<int32_t*>(a);
}
inline float& Fl(uint32_t a)
{
    return *reinterpret_cast<float*>(a);
}
inline uint8_t& By(uint32_t a)
{
    return *reinterpret_cast<uint8_t*>(a);
}
inline uint16_t& W(uint32_t a)
{
    return *reinterpret_cast<uint16_t*>(a);
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
// A float constant of the game's .rdata (0x0056c05c = 0.0, 0x005bcb2c = 1.0).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
// Virtual call through the object's vtable at byte offset `off` (thiscall, callee pops).
template <class... A>
inline uint32_t VC(uint32_t object, uint32_t off, A... args)
{
    return VCall<uint32_t>(P(object), static_cast<int>(off / 4), args...);
}
// Same, the caller tests AL.
template <class... A>
inline bool VB(uint32_t object, uint32_t off, A... args)
{
    return static_cast<uint8_t>(VCall<uint32_t>(P(object), static_cast<int>(off / 4), args...)) != 0;
}
// Same, the result comes back in st0.
template <class... A>
inline float VF(uint32_t object, uint32_t off, A... args)
{
    return VCall<float>(P(object), static_cast<int>(off / 4), args...);
}
inline uint32_t GameP()
{
    return D(0x005fe490);
}
// Game+0x748: the player object.
inline uint32_t Player()
{
    return D(GameP() + 0x748);
}
// Game+0xaa4: the navigation mesh object (0x0046f320 / 0x0046f6d0 / 0x0046ece0 operate on it).
inline uint32_t Nav()
{
    return D(GameP() + 0xaa4);
}
// 0x00604a98: the game time (copied into timestamps).
inline uint32_t GameTime()
{
    return D(0x00604a98);
}
// 0x005391cd (_rand).
inline uint32_t Rand()
{
    return static_cast<uint32_t>(orig::fn_005391cd());
}
// The reference drop the game inlines everywhere: --object+0x28; at zero, unless virtual +0xd8 says the object is
// kept, 0x00487da0 (eax = object) releases it.
inline void Unref(uint32_t object)
{
    if (--I(object + 0x28) == 0 && !VB(object, 0xd8))
        orig::fn_00487da0(object);
}
// The same with the slot test and clear: if (*slot) { drop it; *slot = 0; }.
inline void ClearRef(uint32_t slot)
{
    uint32_t object = D(slot);
    if (object)
    {
        Unref(object);
        D(slot) = 0;
    }
}
// Debug_Printf 0x00407ff0 (cdecl, variadic): the log object Game+0x34, then the rest.
template <class... A>
inline void DebugPrintf(A... args)
{
    GameFunction<void(__cdecl*)(uint32_t, ...)>(0x00407ff0)(D(GameP() + 0x34), args...);
}
// __stricmp 0x005448ee (cdecl).
inline int Stricmp(uint32_t a, uint32_t b)
{
    return GameFunction<int(__cdecl*)(uint32_t, uint32_t)>(0x005448ee)(a, b);
}
// 0x004e7880 (ecx = source vec3, edx = destination): normalises a 3D vector and returns its length in st0. The ABI
// table lists eax as the result; the callers pop st0 (fstp), so it is called here with its real st0 result.
float Normalize3(uint32_t ecx_, uint32_t edx_)
{
    static const uint32_t target = 0x004e7880;
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, ecx_
        mov edx, edx_
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp r
    }
    return r;
}
inline void Copy3(uint32_t dst, uint32_t src)
{
    D(dst) = D(src);
    D(dst + 4) = D(src + 4);
    D(dst + 8) = D(src + 8);
}
// x87 compare helpers as the disassembly tests them (fcomp; fnstsw; test ah, ...):
//   test ah,0x41 / jne -> taken when !(a > b)      test ah,5 / jp -> taken when !(a < b)
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t fn_00483810(uint32_t edi, uint32_t flag);
uint32_t fn_00483c30(uint32_t eax, uint32_t out, uint32_t self);
uint32_t fn_00483d60(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t fn_00484290(uint32_t self);
uint32_t fn_004844c0(uint32_t self, uint32_t pos, uint32_t dir);
uint32_t fn_00486ba0(uint32_t other, uint32_t self);
float fn_00483800(uint32_t self);
uint32_t fn_004837d0(uint32_t self);
void fn_004837e0(uint32_t self, uint32_t ecx);

// ---------------------------------------------------------------------------------------------------------------
// 0x00483250 (ecx = Q111 / Molmer object; stack: a0..a3, ret 0x10; a2 = property block, a3 = property reader).
// Setup of the tower character: Ai_InitGeneric 0x004247f0, then reads the properties "receive_trigger_activate"
// (+0x52c), "delay_distance" (+0xbb8, default 50), "defend_min_distance" (+0xbb4, 100), "formation_distance" (+0xbbc,
// 30), "formation_angle" (+0xbc0, 20), clears +0xb64 (partner) / +0xbb0, copies World lists +0xe7c / +0xe88 into
// +0x604 / +0x608, moves itself from World lists +0xe8c / +0xe7c to +0xe88, random +0xbac. Unless 0x00426630 /
// 0x00426610: reads "tower_marker_tag" (+0xbc4) and "obj_tag" (+0x534) when a trigger is set (logs and drops the
// trigger without a marker tag), 0x0042d0f0, mode +0xb5c = 1 and a random +0x490; otherwise 0x0042cff0, flag 0x400,
// modes +0xb5c = 4 / +0xb60 = 3. Callers 0x0044e630, 0x00486eb0 (Q111Object slot 6). Returns eax as left.
uint32_t fn_00483250(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    orig::fn_004247f0(ebp, a0, a1, a2, a3);
    orig::fn_00453400(0, a3, ebp + 0x52c, ebp, 0x005bba00u /* "receive_trigger_activate" */, a2, 0);
    orig::fn_004534a0(a3, ebp + 0xbb8, ebp, 0x0059e200u /* "delay_distance" */, a2, 0x42480000u, 0);
    orig::fn_004534a0(a3, ebp + 0xbb4, ebp, 0x0059e1ecu /* "defend_min_distance" */, a2, 0x42c80000u, 0);
    orig::fn_004534a0(a3, ebp + 0xbbc, ebp, 0x0059e1d8u /* "formation_distance" */, a2, 0x41f00000u, 0);
    orig::fn_004534a0(a3, ebp + 0xbc0, ebp, 0x0059e1c8u /* "formation_angle" */, a2, 0x41a00000u, 0);
    D(ebp + 0xb64) = 0;
    D(ebp + 0xbb0) = 0;
    uint32_t game = GameP();
    D(ebp + 0x604) = D(D(game + 0xa84) + 0xe7c);
    D(ebp + 0x608) = D(D(game + 0xa84) + 0xe88);
    orig::fn_00454750(D(D(game + 0xa84) + 0xe8c), ebp);
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xe7c), ebp);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe88), ebp);
    D(ebp + 0xbcc) = 0;
    D(ebp + 0xb94) = 0;
    D(ebp + 0xb90) = 0;
    D(ebp + 0xba8) = 0;
    D(ebp + 0xbcc) = 0;
    uint32_t r = Rand();
    Fl(ebp + 0xbac) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bce94) + K(0x005bcb7c);
    if (!static_cast<uint8_t>(orig::fn_00426630(ebp)) && !static_cast<uint8_t>(orig::fn_00426610(ebp)))
    {
        if (D(ebp + 0x52c) != 0)
        {
            orig::fn_00453450(0, 0x0059e1b4u /* "tower_marker_tag" */, a3, ebp + 0xbc4, ebp, a2, 0);
            orig::fn_00453450(0, 0x005b6db8u /* "obj_tag" */, a3, ebp + 0x534, ebp, a2, 0);
            if (D(ebp + 0xbc4) == 0)
            {
                uint32_t trig = D(ebp + 0x52c);
                uint32_t name = orig::fn_00453270(ebp);
                DebugPrintf(0x0059e170u /* "%s setup to receive trigger, with no marker tag set\nTrigger = %d" */, name,
                            trig);
                D(ebp + 0x52c) = 0;
            }
        }
        orig::fn_0042d0f0(ebp, 0, 0, 1);
        r = Rand();
        D(ebp + 0xb5c) = 1;
        Fl(ebp + 0x490) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bcf58) + K(0x005bcb80);
        return r;
    }
    D(ebp + 0xbcc) &= 0xffffefffu;
    r = Rand();
    Fl(ebp + 0xbac) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bce94) + K(0x005bcb7c);
    orig::fn_0042cff0(ebp);
    uint32_t f = D(ebp + 0xbcc) | 0x400;
    D(ebp + 0xbcc) = f;
    D(ebp + 0xb5c) = 4;
    D(ebp + 0xb60) = 3;
    return f;
}

static uint32_t fn_00483250_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00483250(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00483250(fn_00483250_Entry, fn_00483250_Abi)
HOOK(0x00483250, "fn_00483250", fn_00483250_Entry, nullptr);

// 0x004834f0 (stack: Q111 / Molmer object; ret 4; returns eax). Callers 0x00483680, 0x00485910. Not executed in
// recorded runs. The trigger reaction: looks up the marker path +0xbc4 (0x0044c030); without one logs "%s can not
// find trigger marker path %d" (level 0x10). With one: takes its position (virtual +0x54 into +0x934) and direction
// (0x00453150 into +0x908) and asks the nav mesh for a route (0x0046f6d0); on success drops +0xbb0, clears flag
// 0x10000, +0x490 = 5, +0xbc8 = 15, re-pairs itself and its partner (0x00486ba0), flags 0x8000 | 0x40000, 0x0042cff0,
// 0x00427210, mode +0xb5c = 6. On failure (no route or no path) shows the "Molmer_Tower_Failure" text (Text_Lookup
// 0x00495f00, 0x00403410), 0x00484290, flag 0x10000, +0xbc8 = 15.
uint32_t fn_004834f0(uint32_t self)
{
    const uint32_t ebp = self;
    uint32_t path = orig::fn_0044c030(0, D(ebp + 0xbc4));
    if (path == 0)
    {
        uint32_t tag = D(ebp + 0xbc4);
        uint32_t name = orig::fn_00453270(ebp);
        DebugPrintf(0x10u, 0x0059e148u /* "%s can not find trigger marker path %d" */, name, tag);
    }
    else
    {
        VC(path, 0x54, ebp + 0x934);
        orig::fn_00453150(path, ebp + 0x908);
        uint32_t ok = static_cast<uint32_t>(orig::fn_0046f6d0(ebp, Nav(), ebp + 0x934, ebp + 0x908, 0, 0, 0, 0));
        if (ok != 0)
        {
            ClearRef(ebp + 0xbb0);
            D(ebp + 0xbcc) &= 0xfffeffffu;
            uint32_t partner = D(ebp + 0xb64);
            D(ebp + 0x490) = 0x40a00000u;  // 5.0
            D(ebp + 0xbc8) = 0x41700000u;  // 15.0
            if (partner)
                fn_00486ba0(0, partner);
            fn_00486ba0(0, ebp);
            uint32_t f = D(ebp + 0xbcc) | 0x8000;
            D(ebp + 0xbcc) = f;
            D(ebp + 0xbcc) = f | 0x40000;
            orig::fn_0042cff0(ebp);
            uint32_t r = orig::fn_00427210(ebp);
            D(ebp + 0xb5c) = 6;
            return r;
        }
        D(ebp + 0xbcc) |= 0x10000;
    }
    uint32_t text = orig::fn_00495f00(0x0059e130u /* "Molmer_Tower_Failure" */, D(GameP() + 0xac0), 0);
    orig::fn_00403410(GameP(), text, 1);
    fn_00484290(ebp);
    uint32_t f = D(ebp + 0xbcc) | 0x10000;
    D(ebp + 0xbcc) = f;
    D(ebp + 0xbc8) = 0x41700000u;  // 15.0
    return f;
}

static uint32_t fn_004834f0_Abi(uint32_t a0)
{
    return fn_004834f0(a0);
}
ABI_ENTRY_004834f0(fn_004834f0_Entry, fn_004834f0_Abi)
HOOK(0x004834f0, "fn_004834f0", fn_004834f0_Entry, nullptr);

// 0x00483680 Q111Object_Slot14 (ecx = object; stack: a0, trigger id; ret 8; returns al). Vtable slot +0x38 of
// Q111Object 0x005a6df8 and MolmerObject 0x005ae5f8 (no direct callers; not executed in recorded runs). The trigger
// handler: without a marker tag (+0xbc4) it tail-jumps to GenericAI_OnTrigger 0x0042f870 with the same arguments
// (reproduced as call-then-return); otherwise, when the trigger is the one in +0x52c, clears it and runs 0x004834f0.
// Returns 0 then.
uint32_t Q111Object_Slot14(uint32_t self, uint32_t a0, uint32_t trigger)
{
    if (D(self + 0xbc4) == 0)
        return orig::fn_0042f870(self, a0, trigger);
    uint32_t r = trigger;
    if (trigger == D(self + 0x52c))
    {
        D(self + 0x52c) = 0;
        r = fn_004834f0(self);
    }
    return r & 0xffffff00u;
}

static uint32_t Q111Object_Slot14_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Q111Object_Slot14(ecx, a0, a1);
}
ABI_ENTRY_00483680(Q111Object_Slot14_Entry, Q111Object_Slot14_Abi)
HOOK(0x00483680, "Q111Object_Slot14", Q111Object_Slot14_Entry, nullptr);

// 0x004836b0 (eax = Q111 / Molmer object, ecx = passed on to 0x00453150; returns eax). Caller 0x00475650. When neither
// 0x00426630 nor a trigger (+0x52c) holds it: resets the nav cells +0xb88 / +0xb8c to -1, stores its direction
// (0x00453150 into +0xb7c), places itself at its formation point (0x00483d60(1, 0, 0) -> +0xb70, Object_SetPosition
// 0x00457870, Object_ForceGround 0x00455860(1000, 10000)) and plays animation 0 (virtual +0xfc(0, 2); 1 in mode 1).
// Then stores its position in +0x4c0 and pairs "q111" with "molmer" (ObjectSystem_FindByName 0x0045d5f0, 0x00486ba0)
// by its own name (Object_GetName, __stricmp); flags +0xbcc |= 9.
uint32_t fn_004836b0(uint32_t self, uint32_t ecx)
{
    const uint32_t edi = self;
    if (!static_cast<uint8_t>(orig::fn_00426630(edi)) && D(edi + 0x52c) == 0)
    {
        D(edi + 0xb88) = 0xffffffffu;
        D(edi + 0xb8c) = 0xffffffffu;
        orig::fn_00453150(ecx, edi + 0xb7c);
        uint32_t r = fn_00483d60(edi, 0, 0, 0);
        orig::fn_00457870(r & 0xffffff00u, edi + 0xb70, edi, 0, 0, 0);
        orig::fn_00455860(edi, 0x447a0000u /* 1000 */, 0x461c4000u /* 10000 */);
        VC(edi, 0xfc, 0u, 2u);
        if (D(edi + 0xb5c) == 1)
            VC(edi, 0xfc, 1u, 2u);
    }
    VC(edi, 0x54, edi + 0x4c0);
    uint32_t other = 0;
    uint32_t r = static_cast<uint32_t>(Stricmp(orig::fn_00453270(edi), 0x005affacu /* "q111" */));
    if (r == 0)
        other = 0x005affb4u /* "molmer" */;
    else
    {
        r = static_cast<uint32_t>(Stricmp(orig::fn_00453270(edi), 0x005affb4u /* "molmer" */));
        if (r == 0)
            other = 0x005affacu /* "q111" */;
    }
    if (other)
    {
        r = orig::fn_0045d5f0(D(GameP() + 0xa84), other);
        if (r)
            r = fn_00486ba0(r, edi);
    }
    D(edi + 0xbcc) |= 9;
    return r;
}

static uint32_t fn_004836b0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004836b0(eax, ecx);
}
ABI_ENTRY_004836b0(fn_004836b0_Entry, fn_004836b0_Abi)
HOOK(0x004836b0, "fn_004836b0", fn_004836b0_Entry, nullptr);

// 0x004837b0 (ecx = Q111 / Molmer object; returns eax = the flags +0xbcc as read). Caller 0x00485910. Sets flag 0x80
// unless flag 1 is set without flag 8.
uint32_t fn_004837b0(uint32_t self)
{
    uint32_t f = D(self + 0xbcc);
    if (!(f & 1) || (f & 8))
        D(self + 0xbcc) |= 0x80;
    return f;
}

static uint32_t fn_004837b0_Abi(uint32_t ecx)
{
    return fn_004837b0(ecx);
}
ABI_ENTRY_004837b0(fn_004837b0_Entry, fn_004837b0_Abi)
HOOK(0x004837b0, "fn_004837b0", fn_004837b0_Entry, nullptr);

// 0x004837d0 (eax = Q111 / Molmer object; returns al = mode +0xb5c == 1, upper bytes of eax kept). Callers 0x00484290,
// 0x00485910.
uint32_t fn_004837d0(uint32_t self)
{
    return (self & 0xffffff00u) | (D(self + 0xb5c) == 1 ? 1u : 0u);
}

static uint32_t fn_004837d0_Abi(uint32_t eax)
{
    return fn_004837d0(eax);
}
ABI_ENTRY_004837d0(fn_004837d0_Entry, fn_004837d0_Abi)
HOOK(0x004837d0, "fn_004837d0", fn_004837d0_Entry, nullptr);

// 0x004837e0 (eax = Q111 / Molmer object, cl = flag). Caller 0x00483810. Sets (cl != 0) or clears flag 0x10 of +0xbcc.
void fn_004837e0(uint32_t self, uint32_t ecx)
{
    if (static_cast<uint8_t>(ecx))
        D(self + 0xbcc) |= 0x10;
    else
        D(self + 0xbcc) &= 0xffffffefu;
}

static void fn_004837e0_Abi(uint32_t eax, uint32_t ecx)
{
    fn_004837e0(eax, ecx);
}
ABI_ENTRY_004837e0(fn_004837e0_Entry, fn_004837e0_Abi)
HOOK(0x004837e0, "fn_004837e0", fn_004837e0_Entry, nullptr);

// 0x00483800 (eax = Q111 / Molmer object; returns st0 = the float +0xb90, a timestamp). Caller 0x00483810.
float fn_00483800(uint32_t self)
{
    return Fl(self + 0xb90);
}

static float fn_00483800_Abi(uint32_t eax)
{
    return fn_00483800(eax);
}
ABI_ENTRY_00483800(fn_00483800_Entry, fn_00483800_Abi)
HOOK(0x00483800, "fn_00483800", fn_00483800_Entry, nullptr);

// 0x00483810 (edi = Q111 / Molmer object; stack: byte flag "force"; ret 4; returns eax of the final 0x00453150).
// Callers 0x00484290, 0x00484ca0, 0x00485910. Decides which of the pair leads (flag 0x10 of +0xbcc, mirrored inverted
// into the partner +0xb64 by 0x004837e0): when the partner's timestamp +0xb90 is newer it is copied and nothing else
// happens. Otherwise, with the player's position (virtual +0x54, or +0xb70 under flag 1) the closer of the pair
// (ebp) is taken; unless forced, nothing changes while the player is within +0xbb8 of it. The two candidate points
// come from 0x00483c30 (flag 1 without 8) or the player's "ref_molmer_00" / "ref_q_00" bone matrices (0x00475820);
// in mode 2 the points are compared by the absolute cosine to the player's direction (0x00453150, normalised with
// 0x004e7880), else by squared distance; flag 0x10 is set when ebp is (or is not) self accordingly. Timestamp +0xb90 =
// now. Finally +0xb7c = the player's direction (0x00453150).
uint32_t fn_00483810(uint32_t edi, uint32_t flag)
{
    alignas(4) uint8_t fr[0x90];  // the original frame (sub esp, 0x90): F(o) = [esp+o] right after the sub
#define F(o) (u(fr) + (o))
    uint32_t partner = D(edi + 0xb64);
    uint32_t ebp;
    if (partner)
    {
        float a = fn_00483800(partner);
        float b = fn_00483800(edi);
        if (b < a)
        {
            Fl(edi + 0xb90) = fn_00483800(partner);
            goto L_c0b;
        }
    }
    orig::fn_00453150(Player(), F(0x24));
    if (By(edi + 0xbcc) & 1)
        Copy3(F(0x18), edi + 0xb70);
    else
        VC(Player(), 0x54, F(0x18));
    partner = D(edi + 0xb64);
    ebp = edi;
    if (partner)
    {
        VC(partner, 0x54, F(0x24));
        VC(edi, 0x54, F(0x30));
        float a = Fl(F(0x20)) - Fl(F(0x2c));
        float b = Fl(F(0x1c)) - Fl(F(0x28));
        float c = Fl(F(0x18)) - Fl(F(0x24));
        float d = Fl(F(0x20)) - Fl(F(0x38));
        float e = Fl(F(0x1c)) - Fl(F(0x34));
        Fl(F(0x48)) = Fl(F(0x18)) - Fl(F(0x30));
        float pd = c * c + b * b + a * a;
        float sd = Fl(F(0x48)) * Fl(F(0x48)) + e * e + d * d;
        if (sd > pd)
            ebp = D(edi + 0xb64);
    }
    VC(ebp, 0x54, F(0));
    if (!static_cast<uint8_t>(flag))
    {
        float dz = Fl(F(0x20)) - Fl(F(0x8));
        float dy = Fl(F(0x1c)) - Fl(F(0x4));
        float dx = Fl(F(0x18)) - Fl(F(0x0));
        float r = Fl(edi + 0xbb8);
        float d2 = dx * dx + dy * dy + dz * dz;
        if (!(r * r < d2))
            goto L_be0;
    }
    {
        uint32_t f = D(edi + 0xbcc);
        bool less;
        if ((f & 1) && !(f & 8))
        {
            uint32_t r1 = fn_00483c30((f & 0xffffff00u) | 1, F(0xc), edi);
            fn_00483c30(r1 & 0xffffff00u, F(0x3c), edi);
            float x = Fl(F(0xc)) - Fl(F(0x0));
            float z = Fl(F(0x14)) - Fl(F(0x8));
            float y = Fl(F(0x10)) - Fl(F(0x4));
            float A = y * y + z * z + x * x;
            float z2 = Fl(F(0x44)) - Fl(F(0x8));
            float y2 = Fl(F(0x40)) - Fl(F(0x4));
            float x2 = Fl(F(0x3c)) - Fl(F(0x0));
            float B = y2 * y2 + z2 * z2 + x2 * x2;
            less = A < B;
        }
        else
        {
            orig::fn_00475820(F(0x4c), 0x0059e124u /* "ref_q_00" */, Player());
            D(F(0x14)) = D(F(0x78));
            D(F(0xc)) = D(F(0x58));
            D(F(0x10)) = D(F(0x68));
            orig::fn_00475820(F(0x4c), 0x0059e114u /* "ref_molmer_00" */, Player());
            D(F(0x3c)) = D(F(0x58));
            D(F(0x40)) = D(F(0x68));
            D(F(0x44)) = D(F(0x78));
            if (D(edi + 0xb5c) == 2)
            {
                orig::fn_00453150(Player(), F(0x24));
                Fl(F(0x30)) = Fl(F(0xc)) - Fl(F(0x0));
                Fl(F(0x34)) = Fl(F(0x10)) - Fl(F(0x4));
                Fl(F(0x38)) = Fl(F(0x14)) - Fl(F(0x8));
                Normalize3(F(0x30), F(0x30));
                Fl(F(0xc)) = Fl(F(0x3c)) - Fl(F(0x0));
                Fl(F(0x10)) = Fl(F(0x40)) - Fl(F(0x4));
                Fl(F(0x14)) = Fl(F(0x44)) - Fl(F(0x8));
                Normalize3(F(0xc), F(0xc));
                float d1 = std::fabs(Fl(F(0x2c)) * Fl(F(0x38)) + Fl(F(0x28)) * Fl(F(0x34)) + Fl(F(0x30)) * Fl(F(0x24)));
                float d2 = std::fabs(Fl(F(0x14)) * Fl(F(0x2c)) + Fl(F(0x10)) * Fl(F(0x28)) + Fl(F(0xc)) * Fl(F(0x24)));
                less = d2 < d1;
            }
            else
            {
                float x = Fl(F(0xc)) - Fl(F(0x0));
                float z = Fl(F(0x14)) - Fl(F(0x8));
                float y = Fl(F(0x10)) - Fl(F(0x4));
                float A = y * y + z * z + x * x;
                float z2 = Fl(F(0x78)) - Fl(F(0x8));
                float y2 = Fl(F(0x68)) - Fl(F(0x4));
                float x2 = Fl(F(0x58)) - Fl(F(0x0));
                float B = x2 * x2 + y2 * y2 + z2 * z2;
                less = A < B;
            }
        }
        // 0x00483bc8 when less: flag 0x10 = (ebp == self); 0x00483b49 otherwise: flag 0x10 = (ebp != self).
        bool set = less ? (ebp == edi) : (ebp != edi);
        uint32_t g = D(edi + 0xbcc);
        D(edi + 0xbcc) = set ? (g | 0x10) : (g & 0xffffffefu);
    }
L_be0:
    {
        uint32_t t = GameTime();
        D(edi + 0xb90) = t;
        if (D(edi + 0xb64))
            fn_004837e0(D(edi + 0xb64), (t & 0xffffff00u) | ((D(edi + 0xbcc) & 0x10) ? 0u : 1u));
    }
L_c0b:
    return orig::fn_00453150(Player(), edi + 0xb7c);
#undef F
}

static uint32_t fn_00483810_Abi(uint32_t edi, uint32_t a0)
{
    return fn_00483810(edi, a0);
}
ABI_ENTRY_00483810(fn_00483810_Entry, fn_00483810_Abi)
HOOK(0x00483810, "fn_00483810", fn_00483810_Entry, nullptr);

// 0x00483c30 (al = side, ebx = point out, esi = Q111 / Molmer object; returns eax: the original leaves 0x004e36a0's
// result with the x87 status word in its low half; here 0x004e36a0's result). Callers 0x00483810, 0x00483d60. Not
// executed in recorded runs. The formation point: the reversed vector -(+0xb98) is rotated about z by
// +0xbc0 * (side ? 0x005bcba0 : 0x005bcd98) (a 0x44-byte rotation matrix: cos, -sin / sin, cos, m[10] = m[15] = 1,
// flags 1; 0x004e36a0), normalised, scaled by +0xbbc and added to the anchor +0xb70.
uint32_t fn_00483c30(uint32_t eax, uint32_t out, uint32_t self)
{
    alignas(8) uint8_t fr[0x68];  // the original frame (aligned, sub esp, 0x64; push edi): G(o) = [esp+o] after push
#define G(o) (u(fr) + (o))
    const uint32_t esi = self;
    Fl(G(0x14)) = -Fl(esi + 0xb98);
    Fl(G(0x18)) = -Fl(esi + 0xb9c);
    Fl(G(0x1c)) = -Fl(esi + 0xba0);
    Fl(G(0x4)) = Fl(esi + 0xbc0) * (static_cast<uint8_t>(eax) ? K(0x005bcba0) : K(0x005bcd98));
    std::memset(P(G(0x20)), 0, 0x44);
    const float angle = Fl(G(0x4));
    const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
    D(G(0x48)) = 0x3f800000u;
    D(G(0x5c)) = 0x3f800000u;
    D(G(0x60)) = 1;
    const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
    Fl(G(0x20)) = c;
    Fl(G(0x24)) = -s;
    Fl(G(0x30)) = s;
    Fl(G(0x34)) = c;
    uint32_t r = orig::fn_004e36a0(G(0x14), G(0x20), 1, G(0x8));
    float x = Fl(G(0x8));
    float y = Fl(G(0xc));
    float len = std::sqrt(Fl(G(0x10)) * Fl(G(0x10)) + y * y + x * x);
    if (len != K(0x0056c05c))
    {
        float inv = K(0x005bcb2c) / len;
        x = x * inv;
        y = y * inv;
        Fl(G(0x10)) = inv * Fl(G(0x10));
    }
    Fl(out) = x * Fl(esi + 0xbbc) + Fl(esi + 0xb70);
    Fl(out + 4) = y * Fl(esi + 0xbbc) + Fl(esi + 0xb74);
    Fl(out + 8) = Fl(G(0x10)) * Fl(esi + 0xbbc) + Fl(esi + 0xb78);
    return r;
#undef G
}

static uint32_t fn_00483c30_Abi(uint32_t eax, uint32_t ebx, uint32_t esi)
{
    return fn_00483c30(eax, ebx, esi);
}
ABI_ENTRY_00483c30(fn_00483c30_Entry, fn_00483c30_Abi)
HOOK(0x00483c30, "fn_00483c30", fn_00483c30_Entry, nullptr);

// 0x00483d60 (eax = Q111 / Molmer object; stack: byte "move", byte "strict", a2 passed to 0x0046f6d0; ret 0xc; returns
// al). Callers 0x004836b0, 0x00484290, 0x00485910. Finds the formation point on the nav mesh: the point is
// 0x00483c30 of the anchor +0xb70 (flag 1 without 8; side = flag 0x10) or the player's "ref_q_00" / "ref_molmer_00"
// bone (flag 0x10 picks which); its nav cell (0x0046f320) goes to +0xb88 / +0xb8c, failure returns 0. Without "move"
// the anchor takes the point (unless flag 1). With it, and the point more than 1 unit (2D) from the anchor: the
// player's and partner's cells are blocked (0x0046f2e0, 0x0046ece0(.., 2, 1)), a route is asked (0x0046f6d0); if it
// is usable (0x00470c30 of +0x428; "strict" also needs result bits 0xd clear) the anchor takes the point and, for
// result 0, 0x0046f400 runs, +0xb6c = 2 and +0xba4 = the 2D distance + +0xbb8 (flag 0x100); else 0x0046f440 and the
// result is 0. The blocked cells are released (0x0046ece0(.., 2, 0)).
uint32_t fn_00483d60(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2)
{
    alignas(4) uint8_t fr[0x74];  // the original frame (sub esp, 0x74): S(o) = [esp+o] with the 4 saved registers
#define S(o) (u(fr) + ((o) - 0x10))
    const uint32_t edi = self;
    uint32_t ebx = D(edi + 0xbcc);
    uint32_t r;
    uint32_t nav;
    if ((ebx & 1) && !(ebx & 8))
    {
        nav = Nav();
        uint32_t x = D(edi + 0xb70);
        uint32_t y = D(edi + 0xb74);
        D(S(0x3c)) = D(edi + 0xb78);
        r = orig::fn_0046f320(nav, x, y, S(0x20), S(0x24));
        if (!static_cast<uint8_t>(r))
            return r & 0xffffff00u;
        r = fn_00483c30((r & 0xffffff00u) | ((ebx & 0x10) ? 1u : 0u), S(0x28), edi);
    }
    else
    {
        orig::fn_00475820(S(0x40), (ebx & 0x10) ? 0x0059e124u /* "ref_q_00" */ : 0x0059e114u /* "ref_molmer_00" */,
                          Player());
        nav = Nav();
        D(S(0x28)) = D(S(0x4c));
        D(S(0x2c)) = D(S(0x5c));
        D(S(0x30)) = D(S(0x6c));
        r = orig::fn_0046f320(nav, D(S(0x4c)), D(S(0x5c)), S(0x20), S(0x24));
        if (!static_cast<uint8_t>(r))
            return r & 0xffffff00u;
    }
    // 0x00483dd0
    uint8_t bl = static_cast<uint8_t>(a1) == 0 ? 1 : 0;
    if (!static_cast<uint8_t>(a0))
    {
        if (!(D(edi + 0xbcc) & 1))
            Copy3(edi + 0xb70, S(0x28));
    }
    else
    {
        float dy = Fl(edi + 0xb74) - Fl(S(0x2c));
        float dx = Fl(edi + 0xb70) - Fl(S(0x28));
        if (dx * dx + dy * dy > K(0x005bcb2c))
        {
            VC(Player(), 0x54, S(0x34));
            nav = Nav();
            uint32_t ok = orig::fn_0046f320(nav, D(S(0x34)), D(S(0x38)), S(0x14), S(0x18));
            if (static_cast<uint8_t>(ok) && static_cast<uint8_t>(orig::fn_0046f2e0(nav, D(S(0x14)), D(S(0x18)))))
                orig::fn_0046ece0(D(S(0x18)), D(S(0x14)), 2, nav, 1);
            else
                D(S(0x14)) = 0xffffffffu;
            // 0x00483f15
            uint32_t partner = D(edi + 0xb64);
            bool blocked = false;
            if (partner)
            {
                VC(partner, 0x54, S(0x34));
                nav = Nav();
                ok = orig::fn_0046f320(nav, D(S(0x34)), D(S(0x38)), S(0x10), S(0x1c));
                if (static_cast<uint8_t>(ok) && static_cast<uint8_t>(orig::fn_0046f2e0(nav, D(S(0x10)), D(S(0x1c)))))
                {
                    orig::fn_0046ece0(D(S(0x1c)), D(S(0x10)), 2, nav, 1);
                    blocked = true;
                }
            }
            if (!blocked)
                D(S(0x10)) = 0xffffffffu;
            // 0x00483f89
            uint32_t res = static_cast<uint32_t>(orig::fn_0046f6d0(edi, D(GameP() + 0xaa4), S(0x28), 0, 0, a2, 0, 0));
            const uint32_t game = GameP();
            if ((!static_cast<uint8_t>(a1) || (res & 0xd)) && static_cast<uint8_t>(orig::fn_00470c30(edi + 0x428)))
            {
                bl = 1;
                if (!(By(edi + 0xbcc) & 1))
                    Copy3(edi + 0xb70, S(0x28));
                if (res != 0)
                    D(edi + 0xbcc) &= 0xfffffeffu;
                else
                {
                    D(edi + 0xb6c) = 0x40000000u;  // 2.0
                    orig::fn_0046f400(edi, S(0x28), D(game + 0xaa4));
                    float ddy = Fl(edi + 0xb74) - Fl(S(0x2c));
                    float ddx = Fl(edi + 0xb70) - Fl(S(0x28));
                    Fl(edi + 0xba4) = std::sqrt(ddx * ddx + ddy * ddy) + Fl(edi + 0xbb8);
                    D(edi + 0xbcc) |= 0x100;
                }
            }
            else
            {
                orig::fn_0046f440(D(game + 0xaa4), edi);
                bl = 0;
            }
            // 0x00484078
            if (I(S(0x14)) >= 0)
                orig::fn_0046ece0(D(S(0x18)), D(S(0x14)), 2, D(game + 0xaa4), 0);
            if (I(S(0x10)) >= 0)
                orig::fn_0046ece0(D(S(0x1c)), D(S(0x10)), 2, D(game + 0xaa4), 0);
        }
    }
    // 0x004840d9
    D(edi + 0xb88) = D(S(0x20));
    D(edi + 0xb8c) = D(S(0x24));
    return (D(S(0x20)) & 0xffffff00u) | bl;
#undef S
}

static uint32_t fn_00483d60_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00483d60(eax, a0, a1, a2);
}
ABI_ENTRY_00483d60(fn_00483d60_Entry, fn_00483d60_Abi)
HOOK(0x00483d60, "fn_00483d60", fn_00483d60_Entry, nullptr);

// 0x00484100 (ebx = Q111 / Molmer object; stack: target object; ret 4; returns eax = the last _rand). Caller 0x00484830.
// Not executed in recorded runs. Engages a target: 0x00454c90(cl = 1; 1), 0x00454cc0(+0x398), takes the target's
// position (virtual +0x54) and the player's direction (0x00453150) into 0x004844c0, flags +0xbcc &= ~2 then |= 4,
// holds the target in +0xbb0 (reference counted), 0x0042cff0. If the target is not the current one (+0x588): virtual
// +0x23c(target, 0); unless 0x00427e80, a random +0xb68 and virtual +0x180(+0x588, 2 * +0x5dc) (success: virtual
// +0x230(1), mode +0xb5c = 3; failure: virtual +0xfc(0, 2), +0x184); then mode +0xb60 = 3 (mode 4 or +0xab0 set) or
// +0xb5c = 3. Always: flag 0x1000 cleared and a random +0xbac.
uint32_t fn_00484100(uint32_t self, uint32_t target)
{
    alignas(4) uint8_t fr[0x18];  // the original frame (sub esp, 0x18): F(o) = [esp+o] right after the sub
#define F(o) (u(fr) + (o))
    const uint32_t ebx = self;
    const uint32_t ebp = target;
    orig::fn_00454c90(ebx, 1, 1);
    orig::fn_00454cc0(ebx, ebx + 0x398);
    VC(ebp, 0x54, F(0xc));
    orig::fn_00453150(Player(), F(0x0));
    fn_004844c0(ebx, F(0xc), F(0x0));
    uint32_t f = D(ebx + 0xbcc) & 0xfffffffdu;
    D(ebx + 0xbcc) = f;
    D(ebx + 0xbcc) = f | 4;
    ClearRef(ebx + 0xbb0);
    D(ebx + 0xbb0) = ebp;
    ++I(ebp + 0x28);
    orig::fn_0042cff0(ebx);
    if (ebp != D(ebx + 0x588))
    {
        VC(ebx, 0x23c, ebp, 0u);
        bool mode3 = false;
        if (!static_cast<uint8_t>(orig::fn_00427e80(ebx)))
        {
            uint32_t r = Rand();
            uint32_t cur = D(ebx + 0x588);
            Fl(ebx + 0xb68) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bce94) + K(0x005bcb7c);
            float range = Fl(ebx + 0x5dc) + Fl(ebx + 0x5dc);
            if (VB(ebx, 0x180, cur, fb(range)))
            {
                VC(ebx, 0x230, 1u);
                mode3 = true;
            }
            else
            {
                VC(ebx, 0xfc, 0u, 2u);
                VC(ebx, 0x184);
            }
        }
        if (!mode3)
        {
            if (D(ebx + 0xb5c) == 4 || D(ebx + 0xab0) != 0)
                D(ebx + 0xb60) = 3;
            else
                mode3 = true;
        }
        if (mode3)
            D(ebx + 0xb5c) = 3;
    }
    D(ebx + 0xbcc) &= 0xffffefffu;
    uint32_t r = Rand();
    Fl(ebx + 0xbac) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bce94) + K(0x005bcb7c);
    return r;
#undef F
}

static uint32_t fn_00484100_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_00484100(ebx, a0);
}
ABI_ENTRY_00484100(fn_00484100_Entry, fn_00484100_Abi)
HOOK(0x00484100, "fn_00484100", fn_00484100_Entry, nullptr);

// 0x00484290 (eax = Q111 / Molmer object; returns al = 1 when it took up its formation, 0 when 0x00483d60 failed).
// Callers 0x004834f0, 0x004845b0. Not executed in recorded runs. Disengages: drops the target +0xbb0, 0x00454c90(cl =
// 1; 1), 0x00454cc0(+0x398), then rewrites the formation flags of +0xbcc (bits 1, 2, 4, 8; the "stay" result when mode
// +0xb5c is 1 and the anchor flag allows it), virtual +0x180(+0x588, 2 * +0x5dc) / +0x23c(0, 0). Unless staying:
// re-picks the leader (0x00483810 unless the partner is in mode 1, 0x004837d0) and takes the formation point
// (0x00483d60(1, 0, 0)); on success drops +0x588, animation 0 (0x004267a0 -> virtual +0xfc(0, 2)), 0x0042cff0 and
// either a random +0xbac in [3, 7] (0x0046e2c0), +0xb68 = 0.25 and mode +0xb5c = 2 (0x00427210), or (mode 4 / +0xab0)
// +0xb68 = 0 and +0xb60 = 2 or 3.
uint32_t fn_00484290(uint32_t self)
{
    const uint32_t esi = self;
    ClearRef(esi + 0xbb0);
    orig::fn_00454c90(esi, 1, 1);
    orig::fn_00454cc0(esi, esi + 0x398);
    uint32_t eax = D(esi + 0xbcc);
    uint8_t bl = 0;
    uint32_t edx;
    if (eax & 5)
    {
        if (!(eax & 8))
        {
            D(esi + 0xbcc) = eax | 2;
            uint32_t e = D(esi + 0xbcc) | 1;
            D(esi + 0xbcc) = e;
            edx = e | 8;
        }
        else
        {
            if (D(esi + 0xb5c) == 1)
                bl = 1;
            else
                D(esi + 0xbcc) |= 2;
            uint32_t e = D(esi + 0xbcc) & 0xfffffffeu;
            D(esi + 0xbcc) = e;
            edx = e & 0xfffffff7u;
        }
    }
    else
    {
        if (D(esi + 0xb5c) == 1 && !(eax & 1))
            bl = 1;
        else
            D(esi + 0xbcc) |= 2;
        uint32_t e = D(esi + 0xbcc) | 1;
        D(esi + 0xbcc) = e;
        edx = e | 8;
    }
    D(esi + 0xbcc) = edx;
    eax = edx & 0xfffffffbu;
    D(esi + 0xbcc) = eax;
    bool done = false;
    if (eax & 8)
    {
        uint32_t cur = D(esi + 0x588);
        float range = Fl(esi + 0x5dc) + Fl(esi + 0x5dc);
        eax = VC(esi, 0x180, cur, fb(range));
        done = static_cast<uint8_t>(eax) != 0;
    }
    if (!done)
        eax = VC(esi, 0x23c, 0u, 0u);
    if (bl)
        return (eax & 0xffffff00u) | 1;
    uint32_t partner = D(esi + 0xb64);
    if (!partner || !static_cast<uint8_t>(fn_004837d0(partner)))
        fn_00483810(esi, 0);
    uint32_t r = fn_00483d60(esi, 1, 0, 0);
    if (!static_cast<uint8_t>(r))
        return r;
    ClearRef(esi + 0x588);
    if (static_cast<uint8_t>(orig::fn_004267a0(esi)))
        VC(esi, 0xfc, 0u, 2u);
    orig::fn_0042cff0(esi);
    if (D(esi + 0xb5c) != 4 && D(esi + 0xab0) == 0)
    {
        D(esi + 0xbcc) &= 0xffffefffu;
        Fl(esi + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u /* 3.0 */, 0x40e00000u /* 7.0 */);
        uint32_t cur = D(esi + 0x588);
        D(esi + 0xb68) = 0x3e800000u;  // 0.25
        if (cur != 0)
            return (cur & 0xffffff00u) | 1;
        r = orig::fn_00427210(esi);
        D(esi + 0xb5c) = 2;
        return (r & 0xffffff00u) | 1;
    }
    D(esi + 0xbcc) &= 0xffffefffu;
    D(esi + 0xb68) = 0;
    D(esi + 0xb60) = (D(esi + 0x588) != 0 ? 1u : 0u) + 2;
    return 1;
}

static uint32_t fn_00484290_Abi(uint32_t eax)
{
    return fn_00484290(eax);
}
ABI_ENTRY_00484290(fn_00484290_Entry, fn_00484290_Abi)
HOOK(0x00484290, "fn_00484290", fn_00484290_Entry, nullptr);

// 0x004844c0 (eax = Q111 / Molmer object; stack: position, direction; ret 8; returns eax as left: the position's y
// bits in mode 3, else 0x0046f440's result). Callers 0x00484100, 0x00484ca0. Not executed in recorded runs. Re-anchors
// the formation: drops the target +0xbb0, 0x00454c90(cl = 1; 1), 0x00454cc0(+0x398), flags +0xbcc &= ~2, |= 1,
// &= ~8, &= ~4 (four stores), anchor +0xb70 = position, +0xb98 = direction, +0x4c0 = position; unless mode +0xb5c is 3
// the nav route is dropped (0x0046f440).
uint32_t fn_004844c0(uint32_t self, uint32_t pos, uint32_t dir)
{
    const uint32_t esi = self;
    ClearRef(esi + 0xbb0);
    orig::fn_00454c90(esi, 1, 1);
    orig::fn_00454cc0(esi, esi + 0x398);
    uint32_t f = D(esi + 0xbcc) & 0xfffffffdu;
    D(esi + 0xbcc) = f;
    f |= 1;
    D(esi + 0xbcc) = f;
    f &= 0xfffffff7u;
    D(esi + 0xbcc) = f;
    f &= 0xfffffffbu;
    D(esi + 0xbcc) = f;
    Copy3(esi + 0xb70, pos);
    Copy3(esi + 0xb98, dir);
    Copy3(esi + 0x4c0, pos);
    if (D(esi + 0xb5c) == 3)
        return D(pos + 4);
    return orig::fn_0046f440(Nav(), esi);
}

static uint32_t fn_004844c0_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_004844c0(eax, a0, a1);
}
ABI_ENTRY_004844c0(fn_004844c0_Entry, fn_004844c0_Abi)
HOOK(0x004844c0, "fn_004844c0", fn_004844c0_Entry, nullptr);

namespace
{
// Game+0x2c: the dialogue / speech system. Virtual +0xc4 (line, speaker, 0, 0, -1, 0) returns a handle; +0xd0 (line,
// 0, 0, 0, -1, 0) plays a line without a speaker (guess from the arguments).
inline uint32_t SpeakAs(uint32_t line, uint32_t speaker)
{
    return VC(D(GameP() + 0x2c), 0xc4, line, speaker, 0u, 0u, 0xffffffffu, 0u);
}
inline uint32_t Speak(uint32_t line)
{
    return VC(D(GameP() + 0x2c), 0xd0, line, 0u, 0u, 0u, 0xffffffffu, 0u);
}
}  // namespace

// 0x004845b0 (esi = Q111 / Molmer object; stack: byte "announce"; ret 4; returns eax as left). Callers 0x00485120,
// 0x00485490, 0x00485910. Not executed in recorded runs. Sends the pair back to formation (0x00484290 on self and
// partner); when both succeed and "announce" is set, rate-limited by Game+0xb44 / +0xb50 (a new random delay in
// [+0xb48, +0xb4c]) a "return" line is spoken: with a partner "jon25002_B_Return" / "q1111007_B_Return" by one of
// the pair (flag 0x200 picks) or one of "rom50030/31/57/58_Return"; alone the same by percentages. The speaker's
// handle goes to +0xb50. Then the HUD icon "ui_teammate_defend_player" (flag 8) or "ui_teammate_return" (0x00463890
// over Game+0xa70) gets flag 4 in +0x474.
uint32_t fn_004845b0(uint32_t self, uint32_t announce)
{
    const uint32_t esi = self;
    uint32_t edi = 0;
    uint32_t r = fn_00484290(esi);
    if (static_cast<uint8_t>(r))
        edi = 1;
    uint32_t partner = D(esi + 0xb64);
    if (!partner)
        ++edi;
    else
    {
        r = fn_00484290(partner);
        if (static_cast<uint8_t>(r))
            ++edi;
    }
    if (edi != 2 || !static_cast<uint8_t>(announce))
        return r;
    {
        float now = Fl(0x00604a98);
        uint32_t game = GameP();
        if (!(now - Fl(game + 0xb44) < Fl(game + 0xb50)))
        {
            Fl(game + 0xb44) = now;
            float hi = Fl(game + 0xb4c);
            float lo = Fl(game + 0xb48);
            uint32_t rr = Rand();
            Fl(GameP() + 0xb50) = static_cast<float>(static_cast<int32_t>(rr)) * (hi - lo) * K(0x005bcbc0) + lo;
            bool generic = false;
            if (D(esi + 0xb64))
            {
                uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 1000) >> 15);
                if (v < 800)
                    generic = true;
                else if (v < 900)
                {
                    uint32_t who = (D(esi + 0xbcc) & 0x200) ? D(esi + 0xb64) : esi;
                    D(who + 0xb50) = SpeakAs(0x0059e0c0u /* "jon25002_B_Return" */, who);
                }
                else
                {
                    uint32_t who = (D(esi + 0xbcc) & 0x200) ? esi : D(esi + 0xb64);
                    D(who + 0xb50) = SpeakAs(0x0059e0acu /* "q1111007_B_Return" */, who);
                }
            }
            else
            {
                uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 500) >> 15);
                if (v < 400)
                    generic = true;
                else if (D(esi + 0xbcc) & 0x200)
                    D(esi + 0xb50) = SpeakAs(0x0059e0acu /* "q1111007_B_Return" */, esi);
                else
                    D(esi + 0xb50) = SpeakAs(0x0059e0c0u /* "jon25002_B_Return" */, esi);
            }
            if (generic)
            {
                uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 400) >> 15);
                if (v < 100)
                    Speak(0x0059e104u /* "rom50030_Return" */);
                else if (v < 200)
                    Speak(0x0059e0f4u /* "rom50031_Return" */);
                else if (v < 300)
                    Speak(0x0059e0e4u /* "rom50057_Return" */);
                else
                    Speak(0x0059e0d4u /* "rom50058_Return" */);
            }
        }
    }
    uint32_t icon = (By(esi + 0xbcc) & 8) ? 0x005b026cu /* "ui_teammate_defend_player" */
                                          : 0x005b022cu /* "ui_teammate_return" */;
    r = orig::fn_00463890(icon, D(GameP() + 0xa70));
    if (r)
        D(r + 0x474) |= 4;
    return r;
}

static uint32_t fn_004845b0_Abi(uint32_t esi, uint32_t a0)
{
    return fn_004845b0(esi, a0);
}
ABI_ENTRY_004845b0(fn_004845b0_Entry, fn_004845b0_Abi)
HOOK(0x004845b0, "fn_004845b0", fn_004845b0_Entry, nullptr);

namespace
{
// The "attack" lines of 0x00484830: one of six by a random number below 600 (299 when the player's vehicle / mount,
// 0x00475d40, is dead).
uint32_t SpeakAttack()
{
    uint32_t pl = orig::fn_00475d40(Player());
    uint32_t m = static_cast<uint8_t>(orig::fn_00454780(pl)) ? 0x12bu : 0x258u;
    uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * m) >> 15);
    if (v < 100)
        return Speak(0x0059e09cu /* "rom50027_Attack" */);
    if (v < 200)
        return Speak(0x0059e08cu /* "rom50032_Attack" */);
    if (v < 300)
        return Speak(0x0059e07cu /* "rom50034_Attack" */);
    if (v < 400)
        return Speak(0x0059e06cu /* "ROM50117_Attack" */);
    if (v < 500)
        return Speak(0x0059e05cu /* "ROM50118_Attack" */);
    return Speak(0x0059e04cu /* "ROM50119_Attack" */);
}
// Jon's lines (0x004849b5 / 0x00484c5a): "jon50025_Send", "jon50131_Attack", "JON50033_Defend", "JON50130_Defend".
uint32_t SpeakJon(uint32_t s)
{
    uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 400) >> 15);
    uint32_t line = v < 100   ? 0x0059e03cu
                    : v < 200 ? 0x0059e02cu
                    : v < 300 ? 0x0059e01cu
                              : 0x0059e00cu;
    return D(s + 0xb50) = SpeakAs(line, s);
}
// Q's lines (0x00484a5a / 0x00484be8): "q1150039_Send", "Q1150116_Defend", "Q1150118_Defend".
uint32_t SpeakQ(uint32_t s)
{
    uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 400) >> 15);
    uint32_t line = v < 100 ? 0x0059dffcu : v < 200 ? 0x0059dfecu : 0x0059dfdcu;
    return D(s + 0xb50) = SpeakAs(line, s);
}
}  // namespace

// 0x00484830 (eax = Q111 / Molmer object, edi = target; returns eax as left: the spoken line's handle; on the
// rate-limited path the original returns the x87 status word in ax, here 0x00463890's result). Caller 0x00485120.
// Not executed in recorded runs. Sends the pair to attack: 0x00484100(target) for self and partner (the original's
// saved-register push of edi is the first call's argument), the HUD icon "ui_teammate_attack" gets flag 4 (+0x474),
// and, rate-limited like 0x004845b0 (Game+0xb44 / +0xb50 / +0xb48..+0xb4c), a line is spoken: with a partner
// (random 400) half the time a generic "attack" line, else Jon's (flag 0x200 picks the speaker) or Q's lines; alone
// (random 1000) 70% generic, else Q's (flag 0x200) or Jon's.
uint32_t fn_00484830(uint32_t self, uint32_t target)
{
    const uint32_t esi = self;
    fn_00484100(esi, target);
    uint32_t partner = D(esi + 0xb64);
    if (partner)
        fn_00484100(partner, target);
    uint32_t r = orig::fn_00463890(0x005b0240u /* "ui_teammate_attack" */, D(GameP() + 0xa70));
    if (r)
        D(r + 0x474) |= 4;
    float now = Fl(0x00604a98);
    uint32_t game = GameP();
    if (now - Fl(game + 0xb44) < Fl(game + 0xb50))
        return r;
    Fl(game + 0xb44) = now;
    float hi = Fl(game + 0xb4c);
    float lo = Fl(game + 0xb48);
    uint32_t rr = Rand();
    Fl(GameP() + 0xb50) = static_cast<float>(static_cast<int32_t>(rr)) * (hi - lo) * K(0x005bcbc0) + lo;
    if (D(esi + 0xb64))
    {
        uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 400) >> 15);
        if (v < 200)
            return SpeakAttack();
        if (v < 300)
            return SpeakJon((D(esi + 0xbcc) & 0x200) ? D(esi + 0xb64) : esi);
        return SpeakQ((D(esi + 0xbcc) & 0x200) ? esi : D(esi + 0xb64));
    }
    uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 1000) >> 15);
    if (v < 700)
        return SpeakAttack();
    if (D(esi + 0xbcc) & 0x200)
        return SpeakQ(esi);
    return SpeakJon(esi);
}

static uint32_t fn_00484830_Abi(uint32_t eax, uint32_t edi)
{
    return fn_00484830(eax, edi);
}
ABI_ENTRY_00484830(fn_00484830_Entry, fn_00484830_Abi)
HOOK(0x00484830, "fn_00484830", fn_00484830_Entry, nullptr);

namespace
{
// The generic "defend" lines of 0x00484ca0: "rom50028_Defend", "rom50029_Defend", "rom50114_Defend",
// "rom50116_Defend" (random 400).
uint32_t SpeakDefend()
{
    uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 400) >> 15);
    uint32_t line = v < 100   ? 0x0059dfccu
                    : v < 200 ? 0x0059dfbcu
                    : v < 300 ? 0x0059dfacu
                              : 0x0059df9cu;
    return Speak(line);
}
}  // namespace

// 0x00484ca0 (eax = position, ecx = Q111 / Molmer object; returns eax as left: the spoken line's handle; on the
// rate-limited path the original returns the x87 status word in ax, here 0x00463890's result). Caller 0x00485120. Not
// executed in recorded runs. Sends the pair to defend a location: re-anchors self and partner there with the player's
// direction (0x00453150, 0x004844c0), re-picks the leader (0x00483810 forced), shows a marker effect when Game+0xd34
// is set (Fx_Create 0x0041cc60 with a parameter block {8, .., position at +0xc} on Game+0xa90), flags the HUD icon
// "ui_teammate_defend_loc", then speaks rate-limited like 0x00484830 with the "defend" lines.
uint32_t fn_00484ca0(uint32_t pos, uint32_t self)
{
    alignas(4) uint8_t fr[0x98];  // the original frame (sub esp, 0x98): F(o) = [esp+o] right after the sub
#define F(o) (u(fr) + (o))
    const uint32_t ebx = pos;
    const uint32_t edi = self;
    orig::fn_00453150(Player(), F(0xc));
    fn_004844c0(edi, ebx, F(0xc));
    fn_00483810(edi, 1);
    if (D(edi + 0xb64))
        fn_004844c0(D(edi + 0xb64), ebx, F(0xc));
    uint32_t game = GameP();
    if (By(game + 0xd34))
    {
        Copy3(F(0x24), ebx);
        D(F(0x18)) = 8;
        orig::fn_0041cc60((game + 0xd34) & 0xffffff00u, D(game + 0xa90), game + 0xd34, F(0x18));
        game = GameP();
    }
    uint32_t r = orig::fn_00463890(0x005b0254u /* "ui_teammate_defend_loc" */, D(game + 0xa70));
    if (r)
        D(r + 0x474) |= 4;
    float now = Fl(0x00604a98);
    game = GameP();
    if (now - Fl(game + 0xb44) < Fl(game + 0xb50))
        return r;
    Fl(game + 0xb44) = now;
    float hi = Fl(game + 0xb4c);
    float lo = Fl(game + 0xb48);
    uint32_t rr = Rand();
    Fl(GameP() + 0xb50) = static_cast<float>(static_cast<int32_t>(rr)) * (hi - lo) * K(0x005bcbc0) + lo;
    if (D(edi + 0xb64))
    {
        uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 400) >> 15);
        if (v < 200)
            return SpeakDefend();
        if (v < 300)
            return SpeakJon((D(edi + 0xbcc) & 0x200) ? D(edi + 0xb64) : edi);
        return SpeakQ((D(edi + 0xbcc) & 0x200) ? edi : D(edi + 0xb64));
    }
    uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 1000) >> 15);
    if (v < 700)
        return SpeakDefend();
    if (D(edi + 0xbcc) & 0x200)
        return SpeakQ(edi);
    return SpeakJon(edi);
#undef F
}

static uint32_t fn_00484ca0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00484ca0(eax, ecx);
}
ABI_ENTRY_00484ca0(fn_00484ca0_Entry, fn_00484ca0_Abi)
HOOK(0x00484ca0, "fn_00484ca0", fn_00484ca0_Entry, nullptr);

// 0x004852d0 (eax = Q111 / Molmer object). Caller 0x00485120. Not executed in recorded runs. Clears the order flags
// 0x2000 / 0x4000 of +0xbcc.
void fn_004852d0(uint32_t self)
{
    D(self + 0xbcc) &= 0xffff9fffu;
}

static void fn_004852d0_Abi(uint32_t eax)
{
    fn_004852d0(eax);
}
ABI_ENTRY_004852d0(fn_004852d0_Entry, fn_004852d0_Abi)
HOOK(0x004852d0, "fn_004852d0", fn_004852d0_Entry, nullptr);

// 0x00485120 (ebx = Q111 / Molmer object; returns eax as left where cheap: the flags, a mode, the object). Caller
// 0x00485910. The order handler of the pair: nothing while flags 0x400 / 0x8000 are set or it is dead; a dead partner
// is dropped; nothing in modes 5 / 10 (self or partner). Pending orders (flags 0x2000 / 0x4000, set by 0x004852b0)
// are cleared (also on the partner), +0xb94 += 1, then 0x4000 = "return" (0x004845b0(1)); 0x2000 = "attack" the
// object under the cursor (0x00408020 of Game+0xa6c) when it is in the enemy list +0x604 (ObjectList_Contains) or of
// type 0x51 / 0x53 (+0x418), else "defend" the cursor point (0x00408010 + 4, 0x00484ca0). Without orders the counter
// +0xb94 runs down by the frame time (0x00604a90); below zero flag 0x40 is cleared and it is reset to 0. On the
// counter path the original returns the x87 status word; here 0.
uint32_t fn_00485120(uint32_t self)
{
    const uint32_t ebx = self;
    uint32_t eax = D(ebx + 0xbcc);
    if (eax & 0x8400)
        return eax;
    eax = orig::fn_00454780(ebx);
    if (static_cast<uint8_t>(eax))
        return eax;
    const uint32_t edi = ebx + 0xb64;
    uint32_t partner = D(edi);
    if (partner && static_cast<uint8_t>(orig::fn_00454780(partner)))
        ClearRef(edi);
    eax = D(ebx + 0xb5c);
    if (eax == 5 || eax == 0xa)
        return eax;
    eax = D(edi);
    if (eax)
    {
        uint32_t m = D(eax + 0xb5c);
        if (m == 5 || m == 0xa)
            return eax;
    }
    uint32_t f = D(ebx + 0xbcc);
    if (f & 0x6000)
    {
        if (eax)
            fn_004852d0(eax);
        Fl(ebx + 0xb94) = Fl(ebx + 0xb94) + K(0x005bcb2c);
        f = D(ebx + 0xbcc);
        if (f & 0x4000)
            fn_004845b0(ebx, 1);
        else if (f & 0x2000)
        {
            uint32_t game = GameP();
            uint32_t target = D(orig::fn_00408020(D(game + 0xa6c)));
            bool attack = false;
            if (target)
            {
                if (static_cast<uint8_t>(orig::fn_00454690(D(ebx + 0x604), target)))
                    attack = true;
                else
                {
                    uint32_t type = D(target + 0x418);
                    attack = type == 0x51 || type == 0x53;
                }
            }
            if (attack)
                fn_00484830(ebx, target);
            else
                fn_00484ca0(orig::fn_00408010(D(GameP() + 0xa6c)) + 4, ebx);
        }
        fn_004852d0(ebx);
        return ebx;
    }
    float t = Fl(ebx + 0xb94) - Fl(0x00604a90);
    Fl(ebx + 0xb94) = t;
    if (t < K(0x0056c05c))
    {
        if (f & 0x40)
            D(ebx + 0xbcc) &= 0xffffffbfu;
        D(ebx + 0xb94) = 0;
    }
    return 0;
}

static uint32_t fn_00485120_Abi(uint32_t ebx)
{
    return fn_00485120(ebx);
}
ABI_ENTRY_00485120(fn_00485120_Entry, fn_00485120_Abi)
HOOK(0x00485120, "fn_00485120", fn_00485120_Entry, nullptr);

// 0x004852b0 (eax = Q111 / Molmer object, ecx = order code). Caller 0x0047a9a0. Not executed in recorded runs. Order
// 0x12 sets flag 0x2000 ("attack / defend here"), 0x13 flag 0x4000 ("return"); see 0x00485120.
void fn_004852b0(uint32_t self, uint32_t code)
{
    if (code == 0x12)
        D(self + 0xbcc) |= 0x2000;
    else if (code == 0x13)
        D(self + 0xbcc) |= 0x4000;
}

static void fn_004852b0_Abi(uint32_t eax, uint32_t ecx)
{
    fn_004852b0(eax, ecx);
}
ABI_ENTRY_004852b0(fn_004852b0_Entry, fn_004852b0_Abi)
HOOK(0x004852b0, "fn_004852b0", fn_004852b0_Entry, nullptr);

// 0x004852e0 Q111Object_Slot145 (ecx = object; stack: target; ret 4; returns al). Vtable slot +0x244 of Q111Object
// 0x005a6df8 and MolmerObject 0x005ae5f8 (no direct callers; not executed in recorded runs). While in formation (flag 1
// or 4 without 8) a target farther (2D) than +0x5dc from the anchor +0xb70 is refused (0); otherwise the generic test
// 0x00434500(target) decides. (The refusal returns the x87 status word with al = 0 in the original; here 0.)
uint32_t Q111Object_Slot145(uint32_t self, uint32_t target)
{
    alignas(4) uint8_t fr[0xc];
    const uint32_t esi = self;
    uint32_t f = D(esi + 0xbcc);
    if ((f & 5) && !(f & 8))
    {
        VC(target, 0x54, u(fr));
        float dy = Fl(esi + 0xb74) - Fl(u(fr) + 4);
        float dx = Fl(esi + 0xb70) - Fl(u(fr));
        float r = Fl(esi + 0x5dc);
        if (r * r < dx * dx + dy * dy)
            return 0;
    }
    return orig::fn_00434500(target);
}

static uint32_t Q111Object_Slot145_Abi(uint32_t ecx, uint32_t a0)
{
    return Q111Object_Slot145(ecx, a0);
}
ABI_ENTRY_004852e0(Q111Object_Slot145_Entry, Q111Object_Slot145_Abi)
HOOK(0x004852e0, "Q111Object_Slot145", Q111Object_Slot145_Entry, nullptr);

// 0x00485360 Q111Object_Slot109 (ecx = object; stack: a0, a1, a2; ret 0xc; returns eax). Vtable slot +0x1b4 of
// Q111Object and MolmerObject (no direct callers; not executed in recorded runs). Ignores offered targets while it
// holds a live one (+0xbb0); otherwise GenericAI_OfferTarget 0x0042c820 with the same arguments.
uint32_t Q111Object_Slot109(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t cur = D(self + 0xbb0);
    if (cur)
    {
        uint32_t r = orig::fn_00454780(cur);
        if (!static_cast<uint8_t>(r))
            return r;
    }
    return orig::fn_0042c820(self, a0, a1, a2);
}

static uint32_t Q111Object_Slot109_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Q111Object_Slot109(ecx, a0, a1, a2);
}
ABI_ENTRY_00485360(Q111Object_Slot109_Entry, Q111Object_Slot109_Abi)
HOOK(0x00485360, "Q111Object_Slot109", Q111Object_Slot109_Entry, nullptr);

// 0x004853a0 Q111Object_Slot96 (ecx = object; stack: target, a1 (float range); ret 8; returns al). Vtable slot +0x180
// of Q111Object and MolmerObject (no direct callers; not executed in recorded runs). Whether a target may be engaged:
// none for no target; a target with virtual +0x10c set is refused when its +0xb54 says so (virtual +0xd8) or when
// another object claimed this one (+0xab0); the current target (+0xbb0 == +0x588) answers with its virtual +0xd8;
// in formation (flag 1) a target farther (2D) than +0x5dc from the anchor is refused; otherwise 0x0043ecf0(target, a1).
// (Refusals return 0; the original leaves other upper bytes in eax.)
uint32_t Q111Object_Slot96(uint32_t self, uint32_t target, uint32_t a1)
{
    alignas(4) uint8_t fr[0xc];
    const uint32_t esi = self;
    const uint32_t edi = target;
    if (!edi)
        return 0;
    if (VB(edi, 0x10c))
    {
        uint32_t o = D(edi + 0xb54);
        if (o && VB(o, 0xd8))
            return 0;
        uint32_t c = D(esi + 0xab0);
        if (c && c != esi)
            return 0;
    }
    uint32_t cur = D(esi + 0xbb0);
    if (edi == cur && D(esi + 0x588) == cur)
        return VC(edi, 0xd8);
    if (By(esi + 0xbcc) & 1)
    {
        VC(edi, 0x54, u(fr));
        float dy = Fl(esi + 0xb74) - Fl(u(fr) + 4);
        float dx = Fl(esi + 0xb70) - Fl(u(fr));
        float r = Fl(esi + 0x5dc);
        if (!(r * r > dx * dx + dy * dy))
            return 0;
    }
    return orig::fn_0043ecf0(esi, edi, a1);
}

static uint32_t Q111Object_Slot96_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Q111Object_Slot96(ecx, a0, a1);
}
ABI_ENTRY_004853a0(Q111Object_Slot96_Entry, Q111Object_Slot96_Abi)
HOOK(0x004853a0, "Q111Object_Slot96", Q111Object_Slot96_Entry, nullptr);

// 0x00485490 Q111Object_Slot139 (ecx = object; stack: a0, a1, a2; ret 0xc; returns al). Vtable slot +0x22c of
// Q111Object and MolmerObject (no direct callers). Target update: an engaged target (+0xbb0) is dropped, flags |= 4
// then &= ~8, and the pair returns to formation silently (0x004845b0(0)), result 0. Otherwise GenericAI_UpdateTarget
// 0x0042be80(a0, a1, a2); if it finds nothing and the partner's target (+0x588) is acceptable (virtual +0x180(it,
// 2 * +0x5dc)) and not self, that target is taken (virtual +0x23c(it, 0)) and 0x0042be80 runs again.
uint32_t Q111Object_Slot139(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2)
{
    const uint32_t esi = self;
    if (D(esi + 0xbb0))
    {
        ClearRef(esi + 0xbb0);
        uint32_t f = D(esi + 0xbcc) | 4;
        D(esi + 0xbcc) = f;
        D(esi + 0xbcc) = f & 0xfffffff7u;
        return fn_004845b0(esi, 0) & 0xffffff00u;
    }
    uint32_t r = orig::fn_0042be80(esi, a0, a1, a2);
    if (static_cast<uint8_t>(r))
        return (r & 0xffffff00u) | 1;
    uint32_t partner = D(esi + 0xb64);
    if (partner)
    {
        uint32_t its = D(partner + 0x588);
        float range = Fl(esi + 0x5dc) + Fl(esi + 0x5dc);
        r = VC(esi, 0x180, its, fb(range));
        if (static_cast<uint8_t>(r))
        {
            its = D(D(esi + 0xb64) + 0x588);
            if (its != esi)
            {
                VC(esi, 0x23c, its, 0u);
                return orig::fn_0042be80(esi, a0, a1, a2);
            }
            r = its;
        }
        return r & 0xffffff00u;
    }
    return 0;
}

static uint32_t Q111Object_Slot139_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Q111Object_Slot139(ecx, a0, a1, a2);
}
ABI_ENTRY_00485490(Q111Object_Slot139_Entry, Q111Object_Slot139_Abi)
HOOK(0x00485490, "Q111Object_Slot139", Q111Object_Slot139_Entry, nullptr);

// 0x00485580 (edi = Q111 / Molmer object; stack: byte "allow idle"; ret 4; returns al = 1 when an animation was chosen
// (flag 0x20)). Caller 0x00485910. Picks a reaction animation when the camera allows it (0x00410150 of Game+0xa68 is 0)
// and it is not in formation (flags 0x27, or flag 8 without a +0x588 target): if 0x00414810(al = 1, ebx = 8, esi =
// Game+0xa80; 0.5) passes, the heading difference (0x004e81f0 of 0x004e83b0 headings) between its own direction and the
// player's (or Game+0xacc when Game+0xac8 is set) selects: inside [0x005bcd04, 0x005bcd00] a turn (mode 1: random
// +0xbac in [3, 7], virtual +0x198 then +0x1a0(0x2a, 0x2b, 0x28, 0x29, 0, 0, +0x580, +0x584); else virtual +0x198(.., 1)
// then +0x19c(0x30, ..)), flag 0x20; outside an idle (+0x1a0(2, 3, 0, 1, 1, 0, ..) in mode 1, else +0x19c(+0x498, 1,
// ..)). Without the 0x00414810 pass, mode 1 with "allow idle" and no flag 0x1000 plays the idle. Flag 0x1000 is
// cleared when an animation was chosen.
uint32_t fn_00485580(uint32_t self, uint32_t allowIdle)
{
    alignas(4) uint8_t fr[0x18];  // the original frame (sub esp, 0x18): F(o) = [esp+o] right after the sub
#define F(o) (u(fr) + (o))
    const uint32_t edi = self;
    uint32_t game = GameP();
    uint32_t r = orig::fn_00410150(0, D(game + 0xa68));
    bool skip = static_cast<uint8_t>(r) != 0;
    if (!skip)
    {
        uint32_t f = D(edi + 0xbcc);
        if ((f & 0x27) && (!(f & 8) || D(edi + 0x588) != 0))
            skip = true;
    }
    if (!skip)
    {
        r = orig::fn_00414810((r & 0xffffff00u) | 1, 8, D(game + 0xa80), 0x3f000000u /* 0.5 */);
        if (static_cast<uint8_t>(r))
        {
            orig::fn_00453150(edi, F(0xc));
            uint32_t g = GameP();
            uint32_t who = D(g + 0x748);
            if (D(g + 0xac8) != 0 && D(g + 0xacc) != 0)
                who = D(g + 0xacc);
            orig::fn_00453150(who, F(0x0));
            float h0 = orig::fn_004e83b0(F(0x0));
            float h1 = orig::fn_004e83b0(F(0xc));
            float diff = orig::fn_004e81f0(fb(h1), fb(h0));
            bool turn = !(diff < K(0x005bcd04)) && !(diff > K(0x005bcd00) || std::isnan(diff));
            if (turn)
            {
                if (D(edi + 0xb5c) == 1)
                {
                    D(edi + 0xbcc) &= 0xffffefffu;
                    Fl(edi + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u /* 3.0 */, 0x40e00000u /* 7.0 */);
                    if (VB(edi, 0x198, 0u, 0u, 0u, 0u, 0u))
                    {
                        VC(edi, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(edi + 0x580), D(edi + 0x584));
                        D(edi + 0xbcc) |= 0x20;
                    }
                }
                else if (VB(edi, 0x198, 0u, 0u, 0u, 0u, 1u))
                {
                    VC(edi, 0x19c, 0x30u, 0u, 0u, 0u, 0u, 0u);
                    D(edi + 0xbcc) |= 0x20;
                }
            }
            else if (D(edi + 0xb5c) == 1)
                VC(edi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, D(edi + 0x580), D(edi + 0x584));
            else
                VC(edi, 0x19c, D(edi + 0x498), 1u, 0u, 0u, 0u, 0u);
            uint32_t f = D(edi + 0xbcc) & 0xffffefffu;
            D(edi + 0xbcc) = f;
            return (f & 0xffffff00u) | 1;
        }
        if (D(edi + 0xb5c) == 1 && static_cast<uint8_t>(allowIdle) && !(D(edi + 0xbcc) & 0x1000))
            VC(edi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, D(edi + 0x580), D(edi + 0x584));
    }
    if (By(edi + 0xbcc) & 0x20)
    {
        uint32_t f = D(edi + 0xbcc) & 0xffffefffu;
        D(edi + 0xbcc) = f;
        return (f & 0xffffff00u) | 1;
    }
    return 0;
#undef F
}

static uint32_t fn_00485580_Abi(uint32_t edi, uint32_t a0)
{
    return fn_00485580(edi, a0);
}
ABI_ENTRY_00485580(fn_00485580_Entry, fn_00485580_Abi)
HOOK(0x00485580, "fn_00485580", fn_00485580_Entry, nullptr);
