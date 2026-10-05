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

// ---------------------------------------------------------------------------------------------------------------
// 0x00485910 MolmerObject_Slot47 (ecx = object; returns eax, not reproduced: 0). Vtable slot +0xbc of MolmerObject
// 0x005ae5f8; Q111Object 0x005a6df8 reaches it through the thunk 0x00487030 (its slot +0xbc). The per-frame update of
// the tower pair (largest function of the batch, recovered block by block as in ai_06.cpp: the original frame
// sub esp, 0x78 is one array, S(o) = [esp+o] with the four saved registers pushed; labels L_<address>).
// Prologue: with flag 8 the anchor / direction follow the player; 0x00454c90(cl = 0; 1); [esp+0x14] = frame time
// 0x00604a90; flag 0x10000 runs the countdown +0xbc8 into the trigger reaction 0x004834f0; [esp+0x18] = bit 1 of
// 0x00408800(4)+0x1c ("animation finished", guess as in ai_06.cpp); a carry timer +0x878 > 0 switches to mode 5
// (saving the mode in +0xb58); a finished flag-0x20 animation is closed (idle, 0x00427600); a partner that is no
// longer kept (virtual +0xd8) is dropped; orders 0x00485120; dead -> GenericAI_Update 0x0043ee90.
// Modes (+0xb5c, jump table 0x00486b5c): 1 idle in formation, 2 moving to the formation point, 3 engaging, 4 waiting
// on 0x00426630, 5 carried (timer +0x87c, holder +0x870), 6 walking to the trigger marker (+0x934; on arrival:
// placed on the marker 0x004592e0, unlinked from World lists +0xe9c / +0xe90 / +0xe94 / +0xea0, physics stopped,
// matrix of path +0xbc4 (0x00457990, 0x0044ba30), animation "mover_molmer_plant_bomb", mode 7), 7 bomb loop
// ("mover_molmer_bomb_loop" with the animation-end callback 0x0042c080 via 0x004eb5d0), 8 generic, 9 throws a
// "pickup_healthytea" toward the player (ObjectSystem_CreateObject, velocity * 0x005bcb30), 10 waits for an animation.
// Names of the modes are guesses from the strings and calls.
uint32_t MolmerObject_Slot47(uint32_t self)
{
    alignas(4) uint8_t fr[0x78];  // the original frame (sub esp, 0x78)
#define S(o) (u(fr) + ((o) - 0x10))
    const uint32_t ebp = self;
    uint32_t edi;
    uint8_t bl;
    if (By(ebp + 0xbcc) & 8)
    {
        VC(Player(), 0x54, ebp + 0xb70);
        orig::fn_00453150(Player(), ebp + 0xb98);
    }
    orig::fn_00454c90(ebp, 0, 1);
    D(S(0x14)) = D(0x00604a90);
    if (D(ebp + 0xbcc) & 0x10000)
    {
        uint32_t m = D(ebp + 0xb5c);
        if (m != 7 && m != 6)
        {
            float t = Fl(ebp + 0xbc8) - Fl(S(0x14));
            Fl(ebp + 0xbc8) = t;
            if (!(t > K(0x0056c05c) || std::isnan(t)))
                fn_004834f0(ebp);
        }
    }
    edi = orig::fn_00408800(4, ebp);
    bl = static_cast<uint8_t>((By(edi + 0x1c) >> 1) & 1);
    By(S(0x18)) = bl;
    if (D(ebp + 0xb5c) != 5 && Fl(ebp + 0x878) > K(0x0056c05c))
    {
        D(ebp + 0xb58) = D(ebp + 0xb5c);
        uint32_t f = D(ebp + 0xbcc) & 0xffffffdfu;
        D(ebp + 0xbcc) = f;
        D(ebp + 0xbcc) = f & 0xffffefffu;
        uint32_t r = Rand();
        D(ebp + 0xb5c) = 5;
        Fl(ebp + 0xbac) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bce94) + K(0x005bcb7c);
    }
    if ((By(ebp + 0xbcc) & 0x20) && bl)
    {
        if (D(ebp + 0xb5c) != 1)
            VC(ebp, 0x19c, D(ebp + 0x498), 1u, 0u, 0u, 0u, 0u);
        else
            VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, D(ebp + 0x580), D(ebp + 0x584));
        D(ebp + 0xbcc) &= 0xffffffdfu;
        orig::fn_00427600(ebp);
    }
    {
        uint32_t partner = D(ebp + 0xb64);
        if (partner && !VB(partner, 0xd8))
            ClearRef(ebp + 0xb64);
    }
    fn_00485120(ebp);
    if (static_cast<uint8_t>(orig::fn_00454780(ebp)))
        goto L_5af5;
    switch (D(ebp + 0xb5c))
    {
    case 1:
        goto L_60e2;
    case 2:
        goto L_65f7;
    case 3:
        goto L_6908;
    case 4:
        goto L_6af0;
    case 5:
        goto L_5fa1;
    case 6:
        goto L_5d48;
    case 7:
        goto L_5cc6;
    case 8:
        goto L_5af5;
    case 9:
        goto L_5b1b;
    case 10:
        goto L_60d1;
    default:
        return 0;
    }

L_5af5:  // mode 8 and the dead case
    orig::fn_0043ee90(ebp);
    return 0;

L_5b1b:  // mode 9: throw a "pickup_healthytea"
{
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (!By(S(0x18)))
        return 0;
    uint32_t obj = orig::fn_0045f020(0, 0x005b05a8u /* "pickup_healthytea" */, D(GameP() + 0xa84), 0, 0x3ff, 0, 0, 0, 0,
                                     0xaa, 0);
    if (!obj)
        return 0;
    uint32_t r = VC(ebp, 0x54, S(0x2c));
    Fl(S(0x34)) = Fl(S(0x34)) + K(0x005bcbe0);
    orig::fn_00457870(r & 0xffffff00u, S(0x2c), obj, 0, 0, 0);
    VC(Player(), 0x54, S(0x38));
    Fl(S(0x20)) = Fl(S(0x38)) - Fl(S(0x2c));
    Fl(S(0x24)) = Fl(S(0x3c)) - Fl(S(0x30));
    Fl(S(0x28)) = Fl(S(0x40)) - Fl(S(0x34));
    orig::fn_004e7780(S(0x20), S(0x20));  // fstp st(0)
    D(S(0x28)) = 0x3f333333u;              // 0.7
    Normalize3(S(0x20), S(0x20));
    Fl(S(0x20)) = Fl(S(0x20)) * K(0x005bcb30);
    Fl(S(0x24)) = Fl(S(0x24)) * K(0x005bcb30);
    Fl(S(0x28)) = Fl(S(0x28)) * K(0x005bcb30);
    orig::fn_00458510(S(0x20), obj);
    orig::fn_00472080(obj, ebp);
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, D(ebp + 0x580), D(ebp + 0x584));
    D(ebp + 0xbcc) |= 0x20000;
    Fl(ebp + 0xb68) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
    D(ebp + 0xbcc) &= 0xffffefffu;
    Fl(ebp + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
    D(ebp + 0xb5c) = 1;
    return 0;
}

L_5cc6:  // mode 7: the bomb loop
{
    orig::fn_0043ee90(ebp);
    if (D(ebp + 0x460) != 0)
        return 0;
    uint32_t idx = D(edi);
    uint32_t anim = D(D(D(ebp + 0x414) + 0xc) + idx * 12 + 8);
    uint32_t h = orig::fn_004eb5d0(anim, ebp + 0xab8, 0, ebp + 0xab8);
    if (h)
    {
        D(D(D(ebp + 0x410) + 0x14) + W(h + 0xa) * 8 + 4) = h;
        D(D(D(ebp + 0x410) + 0x14) + W(h + 0xa) * 8) = 0x0042c080u;
    }
    VC(ebp, 0x238, 0x0059df58u /* "mover_molmer_bomb_loop" */, 0u);
    orig::fn_0042c380(ebp);
    return 0;
}

L_5d48:  // mode 6: walking to the trigger marker
{
    orig::fn_00470aa0(ebp, Nav(), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        return 0;
    VC(ebp, 0x54, S(0x38));
    const uint32_t ebx = ebp + 0x934;
    float dy = Fl(ebp + 0x938) - Fl(S(0x3c));
    float dx = Fl(ebx) - Fl(S(0x38));
    By(S(0x13)) = 1;
    Fl(S(0x18)) = dx * dx + dy * dy;
    if (Fl(S(0x18)) > K(0x005bcb60))
    {
        D(ebp + 0x490) = 0x40a00000u;  // 5.0
        uint32_t r = static_cast<uint32_t>(orig::fn_0046f6d0(ebp, Nav(), ebx, ebp + 0x908, 0, 0, 0, 0));
        if (r == 0)
            By(S(0x13)) = 0;
    }
    if (!(Fl(S(0x18)) < K(0x005bcb60)) && By(S(0x13)))
        return 0;
    if (Fl(S(0x18)) > K(0x005bcb2c))
    {
        orig::fn_0046f5b0(0, ebp + 0x908, ebx, ebp, Nav(), 0, 1);
        VC(ebp, 0xf8, 0x20000u, 0u);
        return 0;
    }
    uint32_t world = D(GameP() + 0xa84);
    uint32_t placed = orig::fn_004592e0(D(world + 0xe70), D(ebp + 0x534), ebp);
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xe9c), placed);
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xe90), placed);
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xe94), placed);
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xea0), placed);
    D(S(0x2c)) = 0;
    D(S(0x30)) = 0;
    D(S(0x34)) = 0;
    D(ebp + 0x39c) = orig::fn_00455a00(D(ebp + 0x39c));
    D(ebp + 0x398) = orig::fn_00455a00(D(ebp + 0x398));
    orig::fn_00458580(S(0x2c), ebp);
    orig::fn_00458510(S(0x2c), ebp);
    uint32_t path = orig::fn_0044c030(0, D(ebp + 0xbc4));
    uint32_t r = path;
    if (path)
    {
        r = VC(path, 0x5c, S(0x44));
        orig::fn_00457990(r & 0xffffff00u, ebp, S(0x44));
        D(ebp + 0x450) = 0;
        r = orig::fn_0044ba30(path, ebp, 0);
    }
    D(ebp + 0x454) = D(ebp + 0x458);
    orig::fn_004a0e00(r & 0xffffff00u, ebp + 0x61c);
    VC(ebp, 0x238, 0x0059df40u /* "mover_molmer_plant_bomb" */, 0u);
    orig::fn_0042c380(ebp);
    D(ebp + 0xb5c) = 7;
    return 0;
}

L_5fa1:  // mode 5: carried
{
    float t = Fl(ebp + 0x87c) - Fl(S(0x14));
    Fl(ebp + 0x87c) = t;
    if (t > K(0x005bcb5c) || std::isnan(t))
        goto L_5af5;
    uint32_t holder = D(ebp + 0x870);
    if (holder && VB(holder, 0xd8))
        goto L_5af5;
    ClearRef(ebp + 0x870);
    if (orig::fn_00475d40(Player()) != ebp)
    {
        VC(ebp, 0x19c, 0x71u, 0u, 1u, 0u, 0u, 0u);
        D(ebp + 0xb5c) = 0xa;
        return 0;
    }
    goto L_6025;
}

L_60d1:  // mode 10
    if (!By(S(0x18)))
        return 0;
L_6025:
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, D(ebp + 0x580), D(ebp + 0x584));
    if (D(ebp + 0xb58) == 1)
        orig::fn_0042d0f0(ebp, 0, 0, 1);
    orig::fn_0042bc60(0, ebp, 0, 1);
    D(ebp + 0xbcc) &= 0xffffefffu;
    Fl(ebp + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
    D(ebp + 0xb5c) = D(ebp + 0xb58);
    return 0;

L_60e2:  // mode 1: idle in formation
{
    if (static_cast<uint8_t>(orig::fn_004857e0(ebp)))
        return 0;
    uint32_t a = D(ebp + 0x498);
    if (a >= 0x3f && a <= 0x4a)
    {
        orig::fn_0043ee90(ebp);
        Fl(ebp + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
        D(ebp + 0xbcc) &= 0xffffefffu;
        return 0;
    }
    const uint32_t esi = 0x1000;
    float t = Fl(ebp + 0xbac) - Fl(S(0x14));
    Fl(ebp + 0xbac) = t;
    if (By(S(0x18)))
    {
        uint32_t ecx = D(ebp + 0xbcc) & esi;
        if (ecx != 0 || !(t > K(0x0056c05c) || std::isnan(t)))
        {
            if (ecx)
            {
                uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 3) >> 15) + 0x1b;
                VC(ebp, 0x19c, v, 1u, 0u, 0u, 0u, 0u);
            }
            else
            {
                D(ebp + 0xbcc) |= esi;
                uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 3) >> 15) + 0x1e;
                VC(ebp, 0x19c, v, 0u, 0u, 0u, 0u, 0u);
            }
            D(ebp + 0xbac) = 0x47c35000u;  // 100000.0
        }
    }
    bl = 0;
    orig::fn_0042cff0(ebp);
    bool run5580 = true;
    if (!(D(ebp + 0xbcc) & 5))
    {
        orig::fn_0043ee90(ebp);
        if (D(ebp + 0x588) != 0)
            run5580 = false;
    }
    if (run5580)
    {
        bl = static_cast<uint8_t>(fn_00485580(ebp, 1));
        orig::fn_00454c90(ebp, 1, 1);
    }
    // 0x00486200
    uint32_t f = D(ebp + 0xbcc);
    if (!(f & 0x20) && (f & 5))
    {
        if (D(ebp + 0x588) != 0 || VB(ebp, 0x22c, D(S(0x14)), D(S(0x18)), 0u))
        {
            D(ebp + 0xbcc) |= esi;
            Fl(ebp + 0xb68) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
            D(ebp + 0xb5c) = 3;
            return 0;
        }
    }
    // 0x0048626b
    if (D(ebp + 0x498) == 0x2a)
        D(ebp + 0xbcc) |= 0x20;
    f = D(ebp + 0xbcc);
    if (!bl && !(f & 0x20) && D(ebp + 0x498) != 0x23)
    {
        if (!(f & esi))
            VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, D(ebp + 0x580), D(ebp + 0x584));
        orig::fn_0042d0f0(ebp, 0, 0, 0);
    }
    else
        D(ebp + 0xbcc) &= 0xffffefffu;
    // 0x004862d2
    if (By(ebp + 0xbcc) & 4)
    {
        uint32_t partner = D(ebp + 0xb64);
        if (!partner || orig::fn_0042c6f0(partner) == 0)
        {
            float b = Fl(ebp + 0xb68) - Fl(S(0x14));
            Fl(ebp + 0xb68) = b;
            if (!(b > K(0x0056c05c) || std::isnan(b)))
            {
                fn_004845b0(ebp, 0);
                return 0;
            }
        }
    }
    // 0x0048631c
    VC(ebp, 0x54, S(0x2c));
    {
        uint32_t ecx = D(ebp + 0xbcc);
        float R;
        bool haveD = false;
        if ((ecx & 0x100) && Fl(ebp + 0xb6c) > K(0x0056c05c))
        {
            Fl(ebp + 0xb6c) = Fl(ebp + 0xb6c) - Fl(S(0x14));
            if (ecx & 1)
            {
                VC(ebp, 0x54, S(0x20));
                R = K(0x0056c05c);
                D(S(0x18)) = 0xbf800000u;  // -1.0
                haveD = true;
            }
            else
            {
                VC(Player(), 0x54, S(0x20));
                R = Fl(ebp + 0xba4);
            }
        }
        else
        {
            D(ebp + 0xb6c) = 0x40400000u;  // 3.0
            if (ecx & 1)
            {
                R = Fl(ebp + 0xbbc) + K(0x005bcda4);
                Copy3(S(0x20), ebp + 0xb70);
            }
            else
            {
                VC(Player(), 0x54, S(0x20));
                R = Fl(ebp + 0xbb8);
            }
        }
        if (!haveD)
        {
            // 0x004863e8
            float dy = Fl(S(0x24)) - Fl(S(0x30));
            float dx = Fl(S(0x20)) - Fl(S(0x2c));
            Fl(S(0x18)) = dx * dx + dy * dy;
        }
        // 0x0048640a: regroup when the distance exceeds R (or flag 0x80 asks for it)
        if (Fl(S(0x18)) > R * R || (By(ebp + 0xbcc) & 0x80))
        {
            uint32_t partner = D(ebp + 0xb64);
            if (partner && !(By(ebp + 0xbcc) & 0x80))
                fn_004837b0(partner);
            uint32_t g = D(ebp + 0xbcc) & 0xffffff7fu;
            D(ebp + 0xbcc) = g;
            if (!(g & 1) || (g & 8))
            {
                orig::fn_00453150(Player(), S(0x38));
                float dot = Fl(S(0x40)) * Fl(ebp + 0xb84) + Fl(S(0x3c)) * Fl(ebp + 0xb80) +
                            Fl(S(0x38)) * Fl(ebp + 0xb7c);
                fn_00483810(ebp, dot < K(0x0056c05c) ? 1u : 0u);
            }
            if (static_cast<uint8_t>(fn_00483d60(ebp, 1, 0, 0)))
            {
                D(ebp + 0xbcc) &= 0xffffefffu;
                Fl(ebp + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
                orig::fn_0042cff0(ebp);
                VC(ebp, 0xfc, 0u, 2u);
                D(ebp + 0xb68) = 0x3e800000u;  // 0.25
                orig::fn_00427210(ebp);
                orig::fn_00454c90(ebp, 0, 1);
                D(ebp + 0xb5c) = 2;
            }
        }
    }
    // 0x00486525
    if (D(ebp + 0x588) != 0)
        return 0;
    {
        uint32_t game = GameP();
        uint32_t r = orig::fn_00410150(0, D(game + 0xa68));
        bool pass = false;
        if (!static_cast<uint8_t>(r))
        {
            r = orig::fn_00414810((r & 0xffffff00u) | 1, 8, D(game + 0xa80), 0x3f000000u /* 0.5 */);
            pass = static_cast<uint8_t>(r) != 0;
        }
        game = GameP();
        if (pass || (D(ebp + 0xbcc) & 0x800))
        {
            D(ebp + 0xbcc) |= 0x800;
            uint32_t who = D(game + 0x748);
            if (D(game + 0xac8) != 0 && D(game + 0xacc) != 0)
                who = D(game + 0xacc);
            orig::fn_00453150(who, S(0x38));
            if (static_cast<uint8_t>(orig::fn_00454e60(S(0x38), ebp, 0x41000000u /* 8.0 */)))
                D(ebp + 0xbcc) &= 0xfffff7ffu;
            return 0;
        }
        if (D(ebp + 0x588) != 0)
            return 0;
        orig::fn_00454cc0(ebp, ebp + 0x398);
        return 0;
    }
}

L_65f7:  // mode 2: moving to the formation point
{
    VC(ebp, 0x54, S(0x38));
    uint32_t ecx = D(ebp + 0xbcc);
    const uint32_t ebx = 1;
    bool engage = true;
    if (ecx & 1)
    {
        float dy = Fl(ebp + 0xb74) - Fl(S(0x3c));
        float dx = Fl(ebp + 0xb70) - Fl(S(0x38));
        float r = Fl(ebp + 0xbb4);
        if (!(r * r > dx * dx + dy * dy))
            engage = false;
    }
    if (engage && (ecx & 5))
    {
        if (D(ebp + 0x588) != 0 || D(ebp + 0xab0) != 0 || VB(ebp, 0x22c, D(S(0x14)), 0u, ebx))
        {
            orig::fn_0042cff0(ebp);
            Fl(ebp + 0xb68) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
            D(ebp + 0xbcc) &= 0xffffefffu;
            Fl(ebp + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
            D(ebp + 0xb5c) = 3;
            return 0;
        }
    }
    // 0x004866da
    uint32_t f = D(ebp + 0xbcc);
    if (!(f & 1) || (f & 8))
    {
        float b = Fl(ebp + 0xb68) - Fl(S(0x14));
        Fl(ebp + 0xb68) = b;
        if (b < K(0x0056c05c))
        {
            uint32_t a = D(ebp + 0x428);
            bool skip = a != 0 && (D(a + 0x1c) & 0x10) && (D(a + 0x1c) & 8);
            if (!skip)
            {
                uint32_t partner = D(ebp + 0xb64);
                if (!partner || !static_cast<uint8_t>(fn_004837d0(partner)))
                {
                    fn_00483810(ebp, 0);
                    fn_00483d60(ebp, ebx, 0, ebx);
                }
            }
            D(ebp + 0xb68) = 0x3e800000u;  // 0.25
        }
    }
    // 0x0048674b
    orig::fn_00470aa0(ebp, Nav(), 0);
    const uint32_t edi2 = 0x1000;
    f = D(ebp + 0xbcc);
    if (!(f & 0x20) && !(f & edi2))
        orig::fn_00427210(ebp);
    if (!static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
    {
        uint32_t partner = D(ebp + 0xb64);
        if (!partner || !static_cast<uint8_t>(fn_004837d0(partner)))
            fn_00483810(ebp, 0);
        Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x40000000u /* 2.0 */, 0x40a00000u /* 5.0 */);
        VC(ebp, 0xfc, ebx, 2u);
        if (!(D(ebp + 0xbcc) & ebx))
        {
            D(ebp + 0x490) = 0;
            orig::fn_0042d0f0(ebp, 0, 0, ebx);
        }
        else
            VC(ebp, 0x1a0, 2u, 3u, 0u, ebx, ebx, 0u, D(ebp + 0x580), D(ebp + 0x584));
        orig::fn_00454c90(ebp, ebx, ebx);
        uint32_t g = D(ebp + 0xbcc) & 0xfffffffdu;
        D(ebp + 0xbcc) = g;
        D(ebp + 0xbcc) = g | 0x800;
        Fl(ebp + 0xb68) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
        D(ebp + 0xbcc) &= 0xffffefffu;
        Fl(ebp + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
        D(ebp + 0xb5c) = ebx;
        return 0;
    }
    // 0x00486884
    float t = Fl(ebp + 0xbac) - Fl(S(0x14));
    Fl(ebp + 0xbac) = t;
    if (!By(S(0x18)))
        return 0;
    f = D(ebp + 0xbcc);
    if (f & edi2)
    {
        D(ebp + 0xbcc) = f & 0xffffefffu;
        D(ebp + 0x498) = 0;
        orig::fn_00427210(ebp);
    }
    else if (!(t > K(0x0056c05c) || std::isnan(t)))
    {
        D(ebp + 0xbcc) = f | edi2;
        VC(ebp, 0x19c, 0xau, 0u, 0u, 0u, 0u, 0u);
    }
    goto L_6b2f;
}

L_6908:  // mode 3: engaging
{
    orig::fn_0043ee90(ebp);
    if (orig::fn_0042c6f0(ebp) == 0 && D(ebp + 0x498) == 0x23)
        VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    if (static_cast<uint8_t>(orig::fn_00426630(ebp)))
    {
        D(ebp + 0xbcc) &= 0xffffefffu;
        Fl(ebp + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
        D(ebp + 0xb60) = 3;
        D(ebp + 0xb5c) = 4;
        return 0;
    }
    if (static_cast<uint8_t>(orig::fn_004265c0(ebp)))
    {
        uint32_t partner = D(ebp + 0xb64);
        if (partner)
        {
            uint32_t its = D(partner + 0x588);
            if (its && its != ebp && !static_cast<uint8_t>(orig::fn_00454780(its)))
            {
                VC(ebp, 0x23c, D(D(ebp + 0xb64) + 0x588), 0u);
                VC(D(ebp + 0x588), 0x54, ebp + 0x60c);
                return 0;
            }
        }
        float b = Fl(ebp + 0xb68) - Fl(S(0x14));
        Fl(ebp + 0xb68) = b;
        if (b > K(0x0056c05c) || std::isnan(b))
            return 0;
        D(ebp + 0xbcc) &= 0xffffefffu;
        Fl(ebp + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
        orig::fn_0042cff0(ebp);
        D(ebp + 0x490) = 0x40a00000u;  // 5.0
        fn_00483d60(ebp, 1, 0, 0);
        VC(ebp, 0xfc, 0u, 2u);
        orig::fn_00427210(ebp);
        D(ebp + 0xb5c) = 2;
        return 0;
    }
    if (!static_cast<uint8_t>(orig::fn_004265f0(ebp)))
        return 0;
    orig::fn_0042cff0(ebp);
    D(ebp + 0xbcc) &= 0xffffefffu;
    Fl(ebp + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
    D(ebp + 0x490) = 0x40a00000u;  // 5.0
    fn_00483d60(ebp, 1, 0, 0);
    D(ebp + 0xb5c) = 2;
    return 0;
}

L_6af0:  // mode 4
    orig::fn_0043ee90(ebp);
    if (static_cast<uint8_t>(orig::fn_00426630(ebp)))
        return 0;
    orig::fn_0042cff0(ebp);
    if (D(ebp + 0xb60) != 3)
        orig::fn_00427210(ebp);
    D(ebp + 0xb5c) = D(ebp + 0xb60);
    D(ebp + 0xbcc) &= 0xffffefffu;
L_6b2f:
    Fl(ebp + 0xbac) = orig::fn_0046e2c0(0, 0x40400000u, 0x40e00000u);
    return 0;
#undef S
}

static uint32_t MolmerObject_Slot47_Abi(uint32_t ecx)
{
    return MolmerObject_Slot47(ecx);
}
ABI_ENTRY_00485910(MolmerObject_Slot47_Entry, MolmerObject_Slot47_Abi)
HOOK(0x00485910, "MolmerObject_Slot47", MolmerObject_Slot47_Entry, nullptr);

// 0x00486b90 (eax = Q111 / Molmer object; returns al = !(flag 0x400), the upper bytes of +0xbcc >> 10). Callers
// 0x00475650, 0x00486ba0. "Can be paired" (guess).
uint32_t fn_00486b90(uint32_t self)
{
    uint32_t v = D(self + 0xbcc) >> 10;
    return (v & 0xffffff00u) | (~v & 1u);
}

static uint32_t fn_00486b90_Abi(uint32_t eax)
{
    return fn_00486b90(eax);
}
ABI_ENTRY_00486b90(fn_00486b90_Entry, fn_00486b90_Abi)
HOOK(0x00486b90, "fn_00486b90", fn_00486b90_Entry, nullptr);

// 0x00486ba0 (ebx = new partner or 0, edi = Q111 / Molmer object; returns eax as left where cheap). Callers 0x004834f0,
// 0x004836b0. Pairs the object with ebx: drops the old partner slot +0xb64 and, when ebx accepts (0x00486b90), stores
// it there with a reference. Original bug (reproduced): when an old partner exists, the reference dropped is the
// object's own (edi+0x28, virtual +0xd8 / Object_Release on edi), not the old partner's.
uint32_t fn_00486ba0(uint32_t other, uint32_t self)
{
    const uint32_t edi = self;
    const uint32_t esi = edi + 0xb64;
    uint32_t r = D(esi);
    if (r)
    {
        Unref(edi);  // sic: the object itself, see above
        D(esi) = 0;
    }
    if (other)
    {
        r = fn_00486b90(other);
        if (static_cast<uint8_t>(r))
        {
            D(esi) = other;
            ++I(other + 0x28);
        }
    }
    return r;
}

static uint32_t fn_00486ba0_Abi(uint32_t ebx, uint32_t edi)
{
    return fn_00486ba0(ebx, edi);
}
ABI_ENTRY_00486ba0(fn_00486ba0_Entry, fn_00486ba0_Abi)
HOOK(0x00486ba0, "fn_00486ba0", fn_00486ba0_Entry, nullptr);

// 0x00486bf0 (ecx = Q111 / Molmer object; returns eax, not reproduced: 0). Callers 0x0044e6c0, 0x00486fd0 (tail).
// Not executed in recorded runs. Removal: GenericAIObject_Slot55 0x00432730, then drops the partner +0xb64 and the
// target +0xbb0.
uint32_t fn_00486bf0(uint32_t self)
{
    orig::fn_00432730(self);
    ClearRef(self + 0xb64);
    ClearRef(self + 0xbb0);
    return 0;
}

static uint32_t fn_00486bf0_Abi(uint32_t ecx)
{
    return fn_00486bf0(ecx);
}
ABI_ENTRY_00486bf0(fn_00486bf0_Entry, fn_00486bf0_Abi)
HOOK(0x00486bf0, "fn_00486bf0", fn_00486bf0_Entry, nullptr);

// 0x00486c70 Q111Object_Slot112 (ecx = object; returns al = !(flag 0x40000), upper bytes of +0xbcc >> 18). Vtable slot
// +0x1c0 of Q111Object and MolmerObject (no direct callers; not executed in recorded runs).
uint32_t Q111Object_Slot112(uint32_t self)
{
    uint32_t v = D(self + 0xbcc) >> 0x12;
    return (v & 0xffffff00u) | (~v & 1u);
}

static uint32_t Q111Object_Slot112_Abi(uint32_t ecx)
{
    return Q111Object_Slot112(ecx);
}
ABI_ENTRY_00486c70(Q111Object_Slot112_Entry, Q111Object_Slot112_Abi)
HOOK(0x00486c70, "Q111Object_Slot112", Q111Object_Slot112_Entry, nullptr);

// 0x00486c80 Q111Object_Slot149 (ecx = object; no stack arguments; returns eax of the tail call). Vtable slot +0x254 of
// Q111Object and MolmerObject (no direct callers; not executed in recorded runs). It has no ret of its own: it
// tail-jumps to the object's own virtual +0x1c0 (Q111Object_Slot112 for these classes) with the frame untouched;
// reproduced as call-then-return with a hand-written entry stub (no generated one exists).
uint32_t Q111Object_Slot149(uint32_t self)
{
    return VC(self, 0x1c0);
}

static uint32_t Q111Object_Slot149_Abi(uint32_t ecx)
{
    return Q111Object_Slot149(ecx);
}
static __declspec(naked) void Q111Object_Slot149_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push ecx
        call Q111Object_Slot149_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x00486c80, "Q111Object_Slot149", Q111Object_Slot149_Entry, nullptr);

// 0x00486c90 Q111Object_Slot122 (ecx = object; returns al = 0 while in formation (flag 1 without 8), else 1; upper
// bytes of +0xbcc). Vtable slot +0x1e8 of Q111Object and MolmerObject (no direct callers; not executed in recorded
// runs).
uint32_t Q111Object_Slot122(uint32_t self)
{
    uint32_t f = D(self + 0xbcc);
    return (f & 0xffffff00u) | (((f & 1) && !(f & 8)) ? 0u : 1u);
}

static uint32_t Q111Object_Slot122_Abi(uint32_t ecx)
{
    return Q111Object_Slot122(ecx);
}
ABI_ENTRY_00486c90(Q111Object_Slot122_Entry, Q111Object_Slot122_Abi)
HOOK(0x00486c90, "Q111Object_Slot122", Q111Object_Slot122_Entry, nullptr);

// 0x00486cb0 Q111Object_Slot32 (ecx = object; stack: three float out pointers; ret 0xc; returns eax as left). Vtable
// slot +0x80 of Q111Object and MolmerObject (no direct callers; not executed in recorded runs). Outside mode 2 the
// generic 0x004297a0. In mode 2 (moving to formation) the movement speeds: with the 2D distance d to the player
// below 2 * +0xbb8: (d / +0xbb8) * +0x948, +0x948, 5.0; else +0x948, +0x948, 1.0 (guess: speed / max speed / blend).
uint32_t Q111Object_Slot32(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2)
{
    alignas(4) uint8_t fr[0x18];  // the original frame (sub esp, 0x18): F(o) = [esp+o] right after the sub
#define F(o) (u(fr) + (o))
    const uint32_t esi = self;
    if (D(esi + 0xb5c) != 2)
        return orig::fn_004297a0(esi, a0, a1, a2);
    VC(esi, 0x54, F(0xc));
    VC(Player(), 0x54, F(0x0));
    float dy = Fl(F(0x4)) - Fl(F(0x10));
    float dx = Fl(F(0x0)) - Fl(F(0xc));
    float d = std::sqrt(dx * dx + dy * dy);
    float twoR = Fl(esi + 0xbb8) + Fl(esi + 0xbb8);
    if (d < twoR)
    {
        Fl(a0) = d / Fl(esi + 0xbb8) * Fl(esi + 0x948);
        D(a1) = D(esi + 0x948);
        D(a2) = 0x40a00000u;  // 5.0
        return a2;
    }
    D(a0) = D(esi + 0x948);
    uint32_t v = D(esi + 0x948);
    D(a1) = v;
    D(a2) = 0x3f800000u;  // 1.0
    return v;
#undef F
}

static uint32_t Q111Object_Slot32_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Q111Object_Slot32(ecx, a0, a1, a2);
}
ABI_ENTRY_00486cb0(Q111Object_Slot32_Entry, Q111Object_Slot32_Abi)
HOOK(0x00486cb0, "Q111Object_Slot32", Q111Object_Slot32_Entry, nullptr);

// 0x00486d90 Q111Object_Slot34 (ecx = object, not read; returns st0 = 0.5, the constant 0x005bcb4c). Vtable slot +0x88
// of Q111Object and MolmerObject (no direct callers; not executed in recorded runs).
float Q111Object_Slot34()
{
    return K(0x005bcb4c);
}

static float Q111Object_Slot34_Abi()
{
    return Q111Object_Slot34();
}
ABI_ENTRY_00486d90(Q111Object_Slot34_Entry, Q111Object_Slot34_Abi)
HOOK(0x00486d90, "Q111Object_Slot34", Q111Object_Slot34_Entry, nullptr);

// 0x00486da0 Q111Object_Slot94 (ecx = object; stack: target; ret 4; returns al). Vtable slot +0x178 of Q111Object and
// MolmerObject (no direct callers; not executed in recorded runs). A target that passes virtual +0x180(target,
// 2 * +0x5dc) outside modes 6 / 7 goes to the generic 0x0042f8d0(target); otherwise the answer is 1.
uint32_t Q111Object_Slot94(uint32_t self, uint32_t target)
{
    const uint32_t esi = self;
    float range = Fl(esi + 0x5dc) + Fl(esi + 0x5dc);
    uint32_t r = VC(esi, 0x180, target, fb(range));
    if (static_cast<uint8_t>(r))
    {
        r = D(esi + 0xb5c);
        if (r != 6 && r != 7)
            return orig::fn_0042f8d0(esi, target);
    }
    return (r & 0xffffff00u) | 1;
}

static uint32_t Q111Object_Slot94_Abi(uint32_t ecx, uint32_t a0)
{
    return Q111Object_Slot94(ecx, a0);
}
ABI_ENTRY_00486da0(Q111Object_Slot94_Entry, Q111Object_Slot94_Abi)
HOOK(0x00486da0, "Q111Object_Slot94", Q111Object_Slot94_Entry, nullptr);

// 0x00486df0 (eax = attacker, ebx = Q111 / Molmer object; returns eax as left where cheap). Caller 0x0047afa0. Not
// executed in recorded runs. Retaliation: unless engaged (flag 4), at most once per 0x005bcb68 seconds since +0x514
// and when virtual +0x178(attacker) declines, an attacker found in the enemy list +0x604 (ObjectList_Contains)
// replaces the target +0x588 (dropped) via virtual +0x1b4(attacker, 0, 1).
uint32_t fn_00486df0(uint32_t attacker, uint32_t self)
{
    const uint32_t edi = attacker;
    const uint32_t ebx = self;
    if (By(ebx + 0xbcc) & 4)
        return edi;
    if (!(Fl(0x00604a98) - Fl(ebx + 0x514) < K(0x005bcb68)))
        return 0;
    uint32_t r = VC(ebx, 0x178, edi);
    if (static_cast<uint8_t>(r) || !edi)
        return r;
    r = orig::fn_00454690(D(ebx + 0x604), edi);
    if (!static_cast<uint8_t>(r))
        return r;
    ClearRef(ebx + 0x588);
    return VC(ebx, 0x1b4, edi, 0u, 1u);
}

static uint32_t fn_00486df0_Abi(uint32_t eax, uint32_t ebx)
{
    return fn_00486df0(eax, ebx);
}
ABI_ENTRY_00486df0(fn_00486df0_Entry, fn_00486df0_Abi)
HOOK(0x00486df0, "fn_00486df0", fn_00486df0_Entry, nullptr);

// 0x00486e90 Q111Object_Slot129 (ecx = object; returns al = mode +0xb5c is 1 or 3; upper bytes of the mode). Vtable slot
// +0x204 of Q111Object and MolmerObject (no direct callers; not executed in recorded runs).
uint32_t Q111Object_Slot129(uint32_t self)
{
    uint32_t m = D(self + 0xb5c);
    return (m & 0xffffff00u) | ((m == 1 || m == 3) ? 1u : 0u);
}

static uint32_t Q111Object_Slot129_Abi(uint32_t ecx)
{
    return Q111Object_Slot129(ecx);
}
ABI_ENTRY_00486e90(Q111Object_Slot129_Entry, Q111Object_Slot129_Abi)
HOOK(0x00486e90, "Q111Object_Slot129", Q111Object_Slot129_Entry, nullptr);

// Prototypes of the cloth helpers below.
uint32_t fn_004879c0(uint32_t cloth, uint32_t table);
uint32_t fn_00487a40(uint32_t cloth, uint32_t attach);
uint32_t fn_00486f40(uint32_t self, uint32_t a0);

// 0x00486eb0 Q111Object_Slot6 (ecx = object; stack: a0..a3; ret 0x10; returns eax, not reproduced: 0). Vtable slot
// +0x18 of Q111Object only (no direct callers; not executed in recorded runs). Setup: 0x00483250, flags 0x10 then
// 0x200 (two stores), the weapon "ref_rhand" bone (0x004a0ea0 / 0x004a0ef0(0xd) / 0x004a10e0 on the weapon list
// +0x61c), the cape (0x00486f40(0)), 0x00428250(cl = 1).
uint32_t Q111Object_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    fn_00483250(ebp, a0, a1, a2, a3);
    uint32_t f = D(ebp + 0xbcc) | 0x10;
    D(ebp + 0xbcc) = f;
    D(ebp + 0xbcc) = f | 0x200;
    const uint32_t esi = ebp + 0x61c;
    orig::fn_004a0ea0(0x005b6160u /* "ref_rhand" */, ebp, esi, 0, 0);
    orig::fn_004a0ef0(0xd, esi);
    orig::fn_004a10e0(esi, 0);
    fn_00486f40(ebp, 0);
    orig::fn_00428250(ebp, 1);
    return 0;
}

static uint32_t Q111Object_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Q111Object_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00486eb0(Q111Object_Slot6_Entry, Q111Object_Slot6_Abi)
HOOK(0x00486eb0, "Q111Object_Slot6", Q111Object_Slot6_Entry, nullptr);

// 0x00486f40 (eax = Q111 object; stack: a0 passed to the created object; ret 4; returns eax as left where cheap).
// Caller 0x00486eb0. Creates the cape: when the type "q111_cape" exists (0x0045abe0 in the world), creates it
// (ObjectSystem_CreateObject, type 0x400, 0xaa, a0), keeps it in +0xbd0 with a reference, sets its anchor table
// 0x005fb94c (0x004879c0) and attaches it to the object (0x00487a40).
uint32_t fn_00486f40(uint32_t self, uint32_t a0)
{
    const uint32_t ebx = self;
    D(ebx + 0xbd0) = 0;
    uint32_t r = orig::fn_0045abe0(0x0059df34u /* "q111_cape" */, D(GameP() + 0xa84));
    if (!r)
        return r;
    uint32_t cape = orig::fn_0045f020(0, 0x0059df34u /* "q111_cape" */, D(GameP() + 0xa84), 0, 0x400, 0, 0, 0, 0, 0xaa,
                                      a0);
    if (!cape)
        return cape;
    ++I(cape + 0x28);
    D(ebx + 0xbd0) = cape;
    fn_004879c0(cape, 0x005fb94cu);
    return fn_00487a40(D(ebx + 0xbd0), ebx);
}

static uint32_t fn_00486f40_Abi(uint32_t eax, uint32_t a0)
{
    return fn_00486f40(eax, a0);
}
ABI_ENTRY_00486f40(fn_00486f40_Entry, fn_00486f40_Abi)
HOOK(0x00486f40, "fn_00486f40", fn_00486f40_Entry, nullptr);

// 0x00486fd0 Q111Object_Slot55 (ecx = object; returns eax of 0x00486bf0). Vtable slot +0xdc of Q111Object only (no
// direct callers; not executed in recorded runs). Removal: the cape +0xbd0 gets its virtual +0xdc and is released,
// then tail-jumps to 0x00486bf0 (reproduced as a call).
uint32_t Q111Object_Slot55(uint32_t self)
{
    const uint32_t esi = self + 0xbd0;
    if (D(esi))
    {
        VC(D(esi), 0xdc);
        Unref(D(esi));
        D(esi) = 0;
    }
    return fn_00486bf0(self);
}

static uint32_t Q111Object_Slot55_Abi(uint32_t ecx)
{
    return Q111Object_Slot55(ecx);
}
ABI_ENTRY_00486fd0(Q111Object_Slot55_Entry, Q111Object_Slot55_Abi)
HOOK(0x00486fd0, "Q111Object_Slot55", Q111Object_Slot55_Entry, nullptr);

// 0x00487040 Q111Object_Slot78 (ecx = object, not read; returns eax = 0x81 + (rand * 2 >> 15), i.e. 0x81 or 0x82).
// Vtable slot +0x138 of Q111Object only (no direct callers; not executed in recorded runs). A random animation id
// (guess).
uint32_t Q111Object_Slot78()
{
    return static_cast<uint32_t>((static_cast<int32_t>(Rand() << 1) >> 15) + 0x81);
}

static uint32_t Q111Object_Slot78_Abi()
{
    return Q111Object_Slot78();
}
ABI_ENTRY_00487040(Q111Object_Slot78_Entry, Q111Object_Slot78_Abi)
HOOK(0x00487040, "Q111Object_Slot78", Q111Object_Slot78_Entry, nullptr);

// 0x004876b0 (eax = Q111 object; returns eax = the cape +0xbd0). Callers 0x00431fd0, 0x004320b0.
uint32_t fn_004876b0(uint32_t self)
{
    return D(self + 0xbd0);
}

static uint32_t fn_004876b0_Abi(uint32_t eax)
{
    return fn_004876b0(eax);
}
ABI_ENTRY_004876b0(fn_004876b0_Entry, fn_004876b0_Abi)
HOOK(0x004876b0, "fn_004876b0", fn_004876b0_Entry, nullptr);

// 0x004878f0 Q111Object_Slot101 (ecx = object; stack: a0..a3; ret 0x10 by the tail-called method). Vtable slot +0x194
// of Q111Object only (no direct callers; not executed in recorded runs). Stamps Game+0xafc with the game time, then
// tail-jumps to 0x004284e0 with the same frame (reproduced as call-then-return).
uint32_t Q111Object_Slot101(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    D(GameP() + 0xafc) = GameTime();
    return orig::fn_004284e0(self, a0, a1, a2, a3);
}

static uint32_t Q111Object_Slot101_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Q111Object_Slot101(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004878f0(Q111Object_Slot101_Entry, Q111Object_Slot101_Abi)
HOOK(0x004878f0, "Q111Object_Slot101", Q111Object_Slot101_Entry, nullptr);

// 0x00487910 ClothObject_Slot6 (ecx = cloth; stack: a0..a3; ret 0x10; returns eax = 0). Vtable slot +0x18 of
// ClothObject 0x005aca40 (no direct callers; not executed in recorded runs). Setup: Object_LoadProperties 0x004517b0,
// clears the anchor table +0x460 (0x4c dwords), the anchor count +0x590 and the attachment +0x594, +0x598 = 40.0.
uint32_t ClothObject_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004517b0(self, a0, a1, a2, a3);
    std::memset(P(self + 0x460), 0, 0x4c * 4);
    D(self + 0x590) = 0;
    D(self + 0x594) = 0;
    D(self + 0x598) = 0x42200000u;  // 40.0
    return 0;
}

static uint32_t ClothObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return ClothObject_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00487910(ClothObject_Slot6_Entry, ClothObject_Slot6_Abi)
HOOK(0x00487910, "ClothObject_Slot6", ClothObject_Slot6_Entry, nullptr);

// 0x00487960 ClothObject_Slot55 (ecx = cloth; returns eax of Object_Remove, or 0 when kept). Vtable slot +0xdc of
// ClothObject (no direct callers; not executed in recorded runs). Unless flag 8 of +0x424: detaches (0x00487a40(0)),
// clears the anchors (0x004879c0(0)) and tail-jumps to Object_Remove 0x004547b0 (reproduced as a call).
uint32_t ClothObject_Slot55(uint32_t self)
{
    if (By(self + 0x424) & 8)
        return 0;
    fn_00487a40(self, 0);
    fn_004879c0(self, 0);
    return orig::fn_004547b0(self);
}

static uint32_t ClothObject_Slot55_Abi(uint32_t ecx)
{
    return ClothObject_Slot55(ecx);
}
ABI_ENTRY_00487960(ClothObject_Slot55_Entry, ClothObject_Slot55_Abi)
HOOK(0x00487960, "ClothObject_Slot55", ClothObject_Slot55_Entry, nullptr);

// 0x00487990 ClothObject_Slot7 (ecx = cloth; stack: build parameters; ret 4; returns eax as left). Vtable slot +0x1c
// of ClothObject (no direct callers). Builds the physics: 0x00457810, parameters +0x12 = 0 and +0x7a = 1,
// Object_BuildRadiusCollision 0x004527d0, then 0x00458600 when a body (+0x1bc) exists.
uint32_t ClothObject_Slot7(uint32_t self, uint32_t params)
{
    orig::fn_00457810(self);
    By(params + 0x12) = 0;
    By(params + 0x7a) = 1;
    orig::fn_004527d0(self, params);
    uint32_t body = D(self + 0x1bc);
    if (body)
        return orig::fn_00458600(self);
    return body;
}

static uint32_t ClothObject_Slot7_Abi(uint32_t ecx, uint32_t a0)
{
    return ClothObject_Slot7(ecx, a0);
}
ABI_ENTRY_00487990(ClothObject_Slot7_Entry, ClothObject_Slot7_Abi)
HOOK(0x00487990, "ClothObject_Slot7", ClothObject_Slot7_Entry, nullptr);

// 0x004879c0 (stack: cloth, anchor table; ret 8; returns eax = the cloth when anchors were set, else 0). Callers
// 0x00486f40, 0x00487960. The table is pairs of dwords ended by a zero first dword (none for a null table); each
// becomes an anchor of 0x4c bytes at +0x460: the pair, then an identity 0x44-byte matrix. +0x590 = the count.
uint32_t fn_004879c0(uint32_t cloth, uint32_t table)
{
    uint32_t n = 0;
    if (table && D(table) != 0)
    {
        uint32_t c;
        do
        {
            c = D(table + n * 8 + 8);
            ++n;
        } while (c);
    }
    D(cloth + 0x590) = n;
    if (n == 0)
        return 0;
    uint32_t edx = cloth + 0x490;
    for (uint32_t i = 0; i < D(cloth + 0x590); ++i)
    {
        D(edx - 0x30) = D(table + i * 8);
        D(edx - 0x2c) = D(table + i * 8 + 4);
        const uint32_t m = edx - 0x28;
        std::memset(P(m), 0, 0x44);
        D(edx - 0x14) = 0x3f800000u;
        D(edx) = 0x3f800000u;
        D(edx + 0x14) = 0x3f800000u;
        D(m) = 0x3f800000u;
        edx += 0x4c;
    }
    return cloth;
}

static uint32_t fn_004879c0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004879c0(a0, a1);
}
ABI_ENTRY_004879c0(fn_004879c0_Entry, fn_004879c0_Abi)
HOOK(0x004879c0, "fn_004879c0", fn_004879c0_Entry, nullptr);

// 0x00487a40 (eax = cloth, ebx = object to attach or 0; returns eax as left where cheap). Callers 0x00486f40,
// 0x00487960. Replaces the attachment +0x594 (reference counted).
uint32_t fn_00487a40(uint32_t cloth, uint32_t attach)
{
    const uint32_t esi = cloth + 0x594;
    ClearRef(esi);
    if (attach)
    {
        ++I(attach + 0x28);
        D(esi) = attach;
    }
    else
        D(esi) = 0;
    return D(esi);
}

static uint32_t fn_00487a40_Abi(uint32_t eax, uint32_t ebx)
{
    return fn_00487a40(eax, ebx);
}
ABI_ENTRY_00487a40(fn_00487a40_Entry, fn_00487a40_Abi)
HOOK(0x00487a40, "fn_00487a40", fn_00487a40_Entry, nullptr);

// 0x0048fac0 SheepObject_Slot105 (ecx = sheep, not read; stack: animation id; ret 4; returns eax = the animation
// name). Vtable slot +0x1a4 of SheepObject 0x005a8d00 (no direct callers; not executed in recorded runs). The jump
// table 0x0048fb28 / byte table 0x0048fb50 (ids 0..0x61, unsigned) decoded: 0-3 "sheep_turn", 8-0xa "sheep_walk",
// 0xb-0xc "sheep_run_human", 0xd "sheep_salute", 0x17 "sheep_graze", 0x18 "sheep_scan", 0x19 "sheep_bleat",
// 0x49 / 0x4a / 0x61 "sheep_blown_up", 0x5d "sheep_land_dead", others "sheep_error_anim".
uint32_t SheepObject_Slot105(uint32_t id)
{
    switch (id)
    {
    case 0x0: case 0x1: case 0x2: case 0x3:
        return 0x00590348u;  // "sheep_turn"
    case 0x8: case 0x9: case 0xa:
        return 0x00590364u;  // "sheep_walk"
    case 0xb: case 0xc:
        return 0x00590338u;  // "sheep_run_human"
    case 0xd:
        return 0x00590354u;  // "sheep_salute"
    case 0x17:
        return 0x0059032cu;  // "sheep_graze"
    case 0x18:
        return 0x00590320u;  // "sheep_scan"
    case 0x19:
        return 0x00590314u;  // "sheep_bleat"
    case 0x49: case 0x4a: case 0x61:
        return 0x00590380u;  // "sheep_blown_up"
    case 0x5d:
        return 0x00590370u;  // "sheep_land_dead"
    default:
        return 0x00590300u;  // "sheep_error_anim"
    }
}

static uint32_t SheepObject_Slot105_Abi(uint32_t a0)
{
    return SheepObject_Slot105(a0);
}
ABI_ENTRY_0048fac0(SheepObject_Slot105_Entry, SheepObject_Slot105_Abi)
HOOK(0x0048fac0, "SheepObject_Slot105", SheepObject_Slot105_Entry, nullptr);

// 0x0048fbc0 SheepObject_Slot88 (ecx = sheep; returns eax as left where cheap). Vtable slots +0x160, +0x164, +0x168
// and +0x268 of SheepObject (no direct callers; not executed in recorded runs). Screams: unless the speech system
// (Game+0x2c, virtual +0xdc) says its line +0xb4c is still playing, says one of "ctr_sheep_scream_1/2/3" (random 300)
// and keeps the handle in +0xb4c.
uint32_t SheepObject_Slot88(uint32_t self)
{
    uint32_t r = VC(D(GameP() + 0x2c), 0xdc, D(self + 0xb4c));
    if (static_cast<uint8_t>(r))
        return r;
    uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 300) >> 15);
    uint32_t line = v < 100 ? 0x005902ecu /* "ctr_sheep_scream_1" */
                    : v < 200 ? 0x005902d8u /* "ctr_sheep_scream_2" */
                              : 0x005902c4u /* "ctr_sheep_scream_3" */;
    return D(self + 0xb4c) = SpeakAs(line, self);
}

static uint32_t SheepObject_Slot88_Abi(uint32_t ecx)
{
    return SheepObject_Slot88(ecx);
}
ABI_ENTRY_0048fbc0(SheepObject_Slot88_Entry, SheepObject_Slot88_Abi)
HOOK(0x0048fbc0, "SheepObject_Slot88", SheepObject_Slot88_Entry, nullptr);

// 0x0048fc60 SheepObject_Slot86 (ecx = sheep; returns eax of 0x00493b70). Vtable slot +0x158 of SheepObject (no direct
// callers; not executed in recorded runs). Calls 0x00493b70 (ecx = sheep, ebx = 3, edi = 0x005cb16c; stack:
// 0x0063a164, 0, sheep, 0, 0, 0; caller pops).
uint32_t SheepObject_Slot86(uint32_t self)
{
    return orig::fn_00493b70(self, 3, 0x005cb16cu, 0x0063a164u, 0, self, 0, 0, 0);
}

static uint32_t SheepObject_Slot86_Abi(uint32_t ecx)
{
    return SheepObject_Slot86(ecx);
}
ABI_ENTRY_0048fc60(SheepObject_Slot86_Entry, SheepObject_Slot86_Abi)
HOOK(0x0048fc60, "SheepObject_Slot86", SheepObject_Slot86_Entry, nullptr);

// 0x0048fc90 PenguinObject_Slot105 (ecx = penguin, not read; stack: animation id; ret 4; returns eax = the animation
// name). Vtable slot +0x1a4 of PenguinObject 0x005a1768 (no direct callers; not executed in recorded runs). Tables
// 0x0048fcf0 / 0x0048fd14 decoded: 0-3 "pngn_stand", 8-0xa "pngn_walk", 0xb-0xc "pngn_run", 0xd "pngn_salute",
// 0x17 / 0x19 "pngn_scan_chat", 0x18 "pngn_scan_chat1", 0x49 / 0x4a / 0x61 "pngn_blown_up", 0x5d "pngn_land_dead",
// others "penguin_error_anim".
uint32_t PenguinObject_Slot105(uint32_t id)
{
    switch (id)
    {
    case 0x0: case 0x1: case 0x2: case 0x3:
        return 0x00590280u;  // "pngn_stand"
    case 0x8: case 0x9: case 0xa:
        return 0x00590298u;  // "pngn_walk"
    case 0xb: case 0xc:
        return 0x00590274u;  // "pngn_run"
    case 0xd:
        return 0x0059028cu;  // "pngn_salute"
    case 0x17: case 0x19:
        return 0x00590264u;  // "pngn_scan_chat"
    case 0x18:
        return 0x00590254u;  // "pngn_scan_chat1"
    case 0x49: case 0x4a: case 0x61:
        return 0x005902b4u;  // "pngn_blown_up"
    case 0x5d:
        return 0x005902a4u;  // "pngn_land_dead"
    default:
        return 0x00590240u;  // "penguin_error_anim"
    }
}

static uint32_t PenguinObject_Slot105_Abi(uint32_t a0)
{
    return PenguinObject_Slot105(a0);
}
ABI_ENTRY_0048fc90(PenguinObject_Slot105_Entry, PenguinObject_Slot105_Abi)
HOOK(0x0048fc90, "PenguinObject_Slot105", PenguinObject_Slot105_Entry, nullptr);

// 0x0048fd80 PenguinObject_Slot88 (ecx = penguin; returns eax as left where cheap). Vtable slots +0x160, +0x164,
// +0x168 and +0x268 of PenguinObject (no direct callers; not executed in recorded runs). As SheepObject_Slot88 with
// "ctr_penguin_scream_1..4" (random 400).
uint32_t PenguinObject_Slot88(uint32_t self)
{
    uint32_t r = VC(D(GameP() + 0x2c), 0xdc, D(self + 0xb4c));
    if (static_cast<uint8_t>(r))
        return r;
    uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 400) >> 15);
    uint32_t line = v < 100   ? 0x00590228u /* "ctr_penguin_scream_1" */
                    : v < 200 ? 0x00590210u /* "ctr_penguin_scream_2" */
                    : v < 300 ? 0x005901f8u /* "ctr_penguin_scream_3" */
                              : 0x005901e0u /* "ctr_penguin_scream_4" */;
    return D(self + 0xb4c) = SpeakAs(line, self);
}

static uint32_t PenguinObject_Slot88_Abi(uint32_t ecx)
{
    return PenguinObject_Slot88(ecx);
}
ABI_ENTRY_0048fd80(PenguinObject_Slot88_Entry, PenguinObject_Slot88_Abi)
HOOK(0x0048fd80, "PenguinObject_Slot88", PenguinObject_Slot88_Entry, nullptr);

// 0x0048fe20 PenguinObject_Slot86 (ecx = penguin; returns eax of 0x00493b70). Vtable slot +0x158 of PenguinObject (no
// direct callers; not executed in recorded runs). 0x00493b70 (ecx = penguin, ebx = 6, edi = 0x005cb178; stack:
// 0x0063a168, 0, penguin, 0, 0, 0).
uint32_t PenguinObject_Slot86(uint32_t self)
{
    return orig::fn_00493b70(self, 6, 0x005cb178u, 0x0063a168u, 0, self, 0, 0, 0);
}

static uint32_t PenguinObject_Slot86_Abi(uint32_t ecx)
{
    return PenguinObject_Slot86(ecx);
}
ABI_ENTRY_0048fe20(PenguinObject_Slot86_Entry, PenguinObject_Slot86_Abi)
HOOK(0x0048fe20, "PenguinObject_Slot86", PenguinObject_Slot86_Entry, nullptr);

// 0x00497750 SoundEar_Slot6 (ecx = sound ear; stack: a0..a3 (a2 = property block, a3 = reader); ret 0x10; returns eax,
// not reproduced: 0). Vtable slot +0x18 of SoundEar 0x005a2d28 (no direct callers; not executed in recorded runs).
// Setup: Object_LoadProperties, "trigger_id" (+0x460), "reapeat" (sic, bool +0x4e8), the sound names "q_sound"
// (+0x484), "jonsey_sound" (+0x4a4), "roman_sound" (+0x464), "ai_sound" (+0x4c4) with default "none"
// (Object_ReadStringProp 0x004533a0; a result of 1 sets the flags +0x4e9 / +0x4eb / +0x4ea; "ai_sound" also needs
// "ai_tag" +0x4f0 for +0x4ec), "max_distance" (+0x4e4, default 300), virtual +0xf4(0). Without any sound or without
// a trigger id it logs "Sound Ear Invalid Setup\nDeactivating" and removes itself (virtual +0xdc).
uint32_t SoundEar_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    orig::fn_004517b0(ebp, a0, a1, a2, a3);
    uint32_t r = orig::fn_00453450(0, 0x005b94f4u /* "trigger_id" */, a3, ebp + 0x460, ebp, a2, 0);
    orig::fn_004534f0(r & 0xffffff00u, a3, ebp + 0x4e8, ebp, 0x0058f6b0u /* "reapeat" */, a2, 0);
    r = orig::fn_004533a0(0x005bb9e0u /* "none" */, ebp + 0x484, ebp, 0x0058f6a8u /* "q_sound" */, a2, a3, 0);
    By(ebp + 0x4e9) = r == 1 ? 1 : 0;
    r = orig::fn_004533a0(0x005bb9e0u, ebp + 0x4a4, ebp, 0x0058f698u /* "jonsey_sound" */, a2, a3, 0);
    By(ebp + 0x4eb) = r == 1 ? 1 : 0;
    r = orig::fn_004533a0(0x005bb9e0u, ebp + 0x464, ebp, 0x0058f68cu /* "roman_sound" */, a2, a3, 0);
    By(ebp + 0x4ea) = r == 1 ? 1 : 0;
    r = orig::fn_004533a0(0x005bb9e0u, ebp + 0x4c4, ebp, 0x0058f680u /* "ai_sound" */, a2, a3, 0);
    By(ebp + 0x4ec) = 0;
    if (r == 1)
    {
        orig::fn_00453450(0, 0x0058f678u /* "ai_tag" */, a3, ebp + 0x4f0, ebp, a2, 0);
        if (D(ebp + 0x4f0) != 0)
            By(ebp + 0x4ec) = 1;
    }
    orig::fn_004534a0(a3, ebp + 0x4e4, ebp, 0x005bbf98u /* "max_distance" */, a2, 0x43960000u /* 300 */, 0);
    VC(ebp, 0xf4, 0u);
    if ((By(ebp + 0x4e9) || By(ebp + 0x4eb) || By(ebp + 0x4ea) || By(ebp + 0x4ec)) && D(ebp + 0x460) != 0)
        return 0;
    DebugPrintf(0x0058f650u /* "Sound Ear Invalid Setup\nDeactivating" */);
    VC(ebp, 0xdc);
    return 0;
}

static uint32_t SoundEar_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return SoundEar_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00497750(SoundEar_Slot6_Entry, SoundEar_Slot6_Abi)
HOOK(0x00497750, "SoundEar_Slot6", SoundEar_Slot6_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Twiglets. Globals: 0x0063a140 the list of active twiglet generators (created by TwigletController_Slot6),
// 0x0063a144 a percentage threshold (60), 0x0063a148 the maximum twiglet count (100), 0x0063a14c the spawn timer;
// Game+0xc58 the twiglet controller object, Game+0x2710 a twiglet counter.

// 0x0049cdc0 (ecx: only a `push ecx` stack-slot reservation; returns eax = the _rand value). Callers 0x0049ce00,
// 0x0049d010. Not executed in recorded runs. Resets the swarm parameters: 0x0063a144 = 60.0, 0x0063a148 = 100, spawn
// timer 0x0063a14c = rand * 0x005bccb4 + 0x005bcbfc.
uint32_t fn_0049cdc0()
{
    D(0x0063a144) = 0x42700000u;  // 60.0
    D(0x0063a148) = 0x64;
    uint32_t r = Rand();
    Fl(0x0063a14c) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bccb4) + K(0x005bcbfc);
    return r;
}

static uint32_t fn_0049cdc0_Abi(uint32_t /*ecx*/)
{
    return fn_0049cdc0();
}
ABI_ENTRY_0049cdc0(fn_0049cdc0_Entry, fn_0049cdc0_Abi)
HOOK(0x0049cdc0, "fn_0049cdc0", fn_0049cdc0_Entry, nullptr);

// 0x0049ce00 (no inputs; returns eax as left where cheap). Caller 0x0049d3c0. Not executed in recorded runs. Creates the
// controller once: when Game+0xc58 is empty and the type "twiglet" exists (0x0045abe0), ObjectSystem_CreateObject
// (eax = "twiglet_controller", ecx = "twiglet", type 0x3ff, 0xaa) into Game+0xc58, then tail-jumps to 0x0049cdc0
// (reproduced as a call).
uint32_t fn_0049ce00()
{
    uint32_t game = GameP();
    if (D(game + 0xc58) != 0)
        return D(game + 0xc58);
    uint32_t r = orig::fn_0045abe0(0x005b27b0u /* "twiglet" */, D(game + 0xa84));
    if (!r)
        return r;
    uint32_t obj = orig::fn_0045f020(0x005b0048u /* "twiglet_controller" */, 0x005b27b0u /* "twiglet" */,
                                     D(GameP() + 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
    D(GameP() + 0xc58) = obj;
    return fn_0049cdc0();
}

static uint32_t fn_0049ce00_Abi()
{
    return fn_0049ce00();
}
ABI_ENTRY_0049ce00(fn_0049ce00_Entry, fn_0049ce00_Abi)
HOOK(0x0049ce00, "fn_0049ce00", fn_0049ce00_Entry, nullptr);

// 0x0049ce70 (al = activate, ecx = generator; stack: one argument, not read; ret 4; returns eax of the list call).
// Caller 0x0049d530. Not executed in recorded runs. Adds the generator to (al != 0) or removes it from the active list
// 0x0063a140 (ObjectList_Contains / Link / Unlink).
uint32_t fn_0049ce70(uint32_t eax, uint32_t gen, uint32_t /*a0*/)
{
    uint32_t r = orig::fn_00454690(D(0x0063a140), gen);
    if (static_cast<uint8_t>(eax))
    {
        if (static_cast<uint8_t>(r))
            return r;
        return static_cast<uint32_t>(orig::fn_004546d0(D(0x0063a140), gen));
    }
    if (!static_cast<uint8_t>(r))
        return r;
    return orig::fn_00454750(D(0x0063a140), gen);
}

static uint32_t fn_0049ce70_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_0049ce70(eax, ecx, a0);
}
ABI_ENTRY_0049ce70(fn_0049ce70_Entry, fn_0049ce70_Abi)
HOOK(0x0049ce70, "fn_0049ce70", fn_0049ce70_Entry, nullptr);

// 0x0049ceb0 TwigletController_Slot6 (ecx = controller; stack: a0..a3; ret 0x10; returns eax, not reproduced: 0).
// Vtable slot +0x18 of TwigletController 0x005a7590 (no direct callers; not executed in recorded runs). Allocates the
// active-generator list (PMS_heapAlloc 0x004e07f0 on heap 0x0063a210: 0x10, 0x18, 2, "new"; constructed by
// 0x004e7010 with Game+0xac4) into 0x0063a140 (0 on failure), clears +0x464, Object_LoadProperties, virtual +0xf4(0).
uint32_t TwigletController_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t edi = self;
    uint32_t buf = orig::fn_004e07f0(0x0063a210u, 0x10, 0x18, 2, 0x0058b5e8u /* "new" */);
    if (buf)
        D(0x0063a140) = orig::fn_004e7010(D(GameP() + 0xac4), 0, buf);
    else
        D(0x0063a140) = 0;
    D(edi + 0x464) = 0;
    orig::fn_004517b0(edi, a0, a1, a2, a3);
    VC(edi, 0xf4, 0u);
    return 0;
}

static uint32_t TwigletController_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return TwigletController_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0049ceb0(TwigletController_Slot6_Entry, TwigletController_Slot6_Abi)
HOOK(0x0049ceb0, "TwigletController_Slot6", TwigletController_Slot6_Entry, nullptr);

// 0x0049cf30 TwigletController_Slot55 (ecx = controller; returns eax, not reproduced: 0). Vtable slot +0xdc of
// TwigletController (no direct callers; not executed in recorded runs). Removal: unlinks every generator from the
// active list (iterator 0x004e7340 / 0x004e73c0 over an 8-byte iterator on the stack), deletes the list (virtual
// +0x10(1)), clears 0x0063a140 and Game+0xc58, drops +0x460, Object_Remove 0x004547b0, and releases the iterator's
// node (--node+0x18). The tracker size (170) stops 9 bytes short of the real end 0x0049cfe3.
uint32_t TwigletController_Slot55(uint32_t self)
{
    alignas(4) uint8_t it[8] = {};
    const uint32_t ebp = self;
    uint32_t e = orig::fn_004e7340(D(0x0063a140), u(it));
    while (e)
    {
        orig::fn_00454750(D(0x0063a140), e);
        e = orig::fn_004e73c0(u(it));
    }
    uint32_t list = D(0x0063a140);
    if (list)
        VC(list, 0x10, 1u);
    D(0x0063a140) = 0;
    D(GameP() + 0xc58) = 0;
    ClearRef(ebp + 0x460);
    orig::fn_004547b0(ebp);
    uint32_t node = D(u(it) + 4);
    if (node)
        --I(node + 0x18);
    return 0;
}

static uint32_t TwigletController_Slot55_Abi(uint32_t ecx)
{
    return TwigletController_Slot55(ecx);
}
ABI_ENTRY_0049cf30(TwigletController_Slot55_Entry, TwigletController_Slot55_Abi)
HOOK(0x0049cf30, "TwigletController_Slot55", TwigletController_Slot55_Entry, nullptr);

// 0x0049cff0 (ecx = object, not read; stack: physics build parameters; ret 4; returns eax = the parameters). Shared
// slot +0x1c (or +0x154 / +0x15c / +0x18c / +0x1a4 / +0x284) of ~30 classes (CameraPathObject, AlarmGenerator,
// SoundEar, TwigletController, MarkerObject, PickupObject, GenericAIObject, ...; no direct callers; not executed in
// recorded runs): a non-physical object. Sets parameters +0 = 0x4000, +0xd = 1, +0x7a = 1.
uint32_t fn_0049cff0(uint32_t params)
{
    D(params) = 0x4000;
    By(params + 0xd) = 1;
    By(params + 0x7a) = 1;
    return params;
}

static uint32_t fn_0049cff0_Abi(uint32_t a0)
{
    return fn_0049cff0(a0);
}
ABI_ENTRY_0049cff0(fn_0049cff0_Entry, fn_0049cff0_Abi)
HOOK(0x0049cff0, "fn_0049cff0", fn_0049cff0_Entry, nullptr);

// 0x0049d010 TwigletController_Slot9 (ecx = controller; returns eax, not reproduced: 0). Vtable slot +0x24 of
// TwigletController (no direct callers; not executed in recorded runs). Reset: unlinks every active generator,
// resets the swarm parameters (0x0049cdc0), clears +0x460 (without releasing it) and releases the iterator node.
uint32_t TwigletController_Slot9(uint32_t self)
{
    alignas(4) uint8_t it[8] = {};
    uint32_t e = orig::fn_004e7340(D(0x0063a140), u(it));
    while (e)
    {
        orig::fn_00454750(D(0x0063a140), e);
        e = orig::fn_004e73c0(u(it));
    }
    fn_0049cdc0();
    uint32_t node = D(u(it) + 4);
    D(self + 0x460) = 0;
    if (node)
        --I(node + 0x18);
    return 0;
}

static uint32_t TwigletController_Slot9_Abi(uint32_t ecx)
{
    return TwigletController_Slot9(ecx);
}
ABI_ENTRY_0049d010(TwigletController_Slot9_Entry, TwigletController_Slot9_Abi)
HOOK(0x0049d010, "TwigletController_Slot9", TwigletController_Slot9_Entry, nullptr);

uint32_t fn_0049d580(uint32_t gen);
uint32_t fn_0049d590(uint32_t gen);

// 0x0049d080 TwigletController_Slot47 (ecx = controller; returns eax, not reproduced: 0). Vtable slot +0xbc of
// TwigletController (no direct callers; not executed in recorded runs). The swarm update (aligned frame, G(o) =
// [esp+o] with the four saved registers): when the spawn timer 0x0063a14c runs out and fewer than 0x0063a148 twiglets
// exist (World+0xeec, virtual +0x34) it picks the active generator with the most pending spawns (0x00415900; the
// current one +0x460 only when it has no initial list, 0x0049d580 == 1), playing a swarm loop
// "ctr_twiglet_swarm_1_lp" / "_2_lp" (speech virtual +0xd0, handle +0x464) while any generator is pending; spawns a
// twiglet from it (0x0049d590), counts the twiglets of World+0xeec (0x0044ad40 bit 0 = idle) and finds the one
// closest (2D) to the player; 0x0049f380 (eax = twiglet; flag, 0) wakes the new one (flag 1 when the idle share
// is above 0x0063a144 percent or all are idle) and, when the new one is farther than the closest, wakes the closest
// too; restarts the timer and holds the chosen generator in +0x460. Without pending generators the loop is stopped
// (speech virtual +0xd8(+0x464, 0)).
uint32_t TwigletController_Slot47(uint32_t self)
{
    alignas(8) uint8_t fr[0x48];  // the original frame (aligned, sub esp, 0x38 + 4 saved registers)
#define G(o) (u(fr) + (o))
    uint32_t edi = self;
    float t = Fl(0x0063a14c) - Fl(0x00604a90);
    Fl(0x0063a14c) = t;
    if (t > K(0x0056c05c) || std::isnan(t))
        return 0;
    D(G(0x18)) = edi;
    uint32_t count = VC(D(D(GameP() + 0xa84) + 0xeec), 0x34);
    if (count >= D(0x0063a148))
    {
        VC(D(GameP() + 0x2c), 0xd8, edi + 0x464, 0u);
        return 0;
    }
    uint32_t ebx = 0;
    D(G(0x28)) = 0;
    D(G(0x2c)) = 0;
    uint32_t ebp = 0;
    D(G(0x14)) = 0;
    By(G(0x13)) = 0;
    uint32_t esi = orig::fn_004e7340(D(0x0063a140), G(0x28));
    bool found = false;
    if (esi)
    {
        do
        {
            uint32_t ecx = orig::fn_00415900(esi);
            if (ecx > ebp)
            {
                if (esi == D(edi + 0x460) && fn_0049d580(esi) == 1)
                    ebx = esi;
                else
                {
                    D(G(0x14)) = esi;
                    ebp = ecx;
                }
            }
            if (ecx > 0)
            {
                By(G(0x13)) = 1;
                if (D(edi + 0x464) == 0)
                {
                    int32_t v = static_cast<int32_t>((Rand() * 100) & 0xffff8000u);
                    uint32_t line = v < 0x190000 ? 0x0058ef5cu /* "ctr_twiglet_swarm_1_lp" */
                                                 : 0x0058ef44u /* "ctr_twiglet_swarm_2_lp" */;
                    D(edi + 0x464) = VC(D(GameP() + 0x2c), 0xd0, line, esi, 1u, 0u, 0xffffffffu, 0u);
                }
            }
            esi = orig::fn_004e73c0(G(0x28));
            edi = D(G(0x18));
        } while (esi);
        found = D(G(0x14)) != 0;
    }
    if (!found)
    {
        D(G(0x14)) = ebx;
        if (!ebx)
            goto L_360;
    }
    // 0x0049d1ad
    VC(Player(), 0x54, G(0x30));
    D(G(0x20)) = 0;
    D(G(0x1c)) = 0;
    ebp = 0;
    esi = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xeec), G(0x28));
    if (esi)
    {
        do
        {
            uint32_t r = orig::fn_0044ad40(esi);
            if (!(r & 1))
                Fl(G(0x1c)) = Fl(G(0x1c)) + K(0x005bcb2c);
            else
            {
                VC(esi, 0x54, G(0x3c));
                float dy = Fl(G(0x34)) - Fl(G(0x40));
                float dx = Fl(G(0x30)) - Fl(G(0x3c));
                float d2 = dx * dx + dy * dy;
                if (ebp == 0 || d2 < Fl(G(0x24)))
                {
                    Fl(G(0x24)) = d2;
                    ebp = esi;
                }
            }
            Fl(G(0x20)) = Fl(G(0x20)) + K(0x005bcb2c);
            esi = orig::fn_004e73c0(G(0x28));
        } while (esi);
        edi = D(G(0x18));
    }
    // 0x0049d27f
    {
        ebx = D(G(0x14));
        esi = fn_0049d590(ebx);
        bool one;
        if (Fl(G(0x20)) == Fl(G(0x1c)))
            one = true;
        else if (Fl(G(0x1c)) / Fl(G(0x20)) * K(0x005bcb60) > Fl(0x0063a144))
            one = true;
        else
        {
            By(G(0x20)) = 0;
            one = false;
            if (ebp)
            {
                VC(esi, 0x54, G(0x3c));
                float dy = Fl(G(0x34)) - Fl(G(0x40));
                float dx = Fl(G(0x30)) - Fl(G(0x3c));
                if (dx * dx + dy * dy > Fl(G(0x24)))
                {
                    orig::fn_0049f380(ebp, 0, 0, 0);
                    one = true;
                }
            }
        }
        if (one)
            By(G(0x20)) = 1;
        // edx is listed as an input of 0x0049f380 only because 0x00408e30's caller does `or edx, -1`; passed as 0.
        orig::fn_0049f380(esi, 0, D(G(0x20)), 0);
        uint32_t r = Rand();
        Fl(0x0063a14c) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bccb4) + K(0x005bcbfc);
        uint32_t old = D(edi + 0x460);
        if (old)
            Unref(old);
        uint32_t chosen = D(G(0x14));
        D(edi + 0x460) = chosen;
        ++I(chosen + 0x28);
    }
L_360:
    if (!By(G(0x13)))
        VC(D(GameP() + 0x2c), 0xd8, edi + 0x464, 0u);
    {
        uint32_t node = D(G(0x2c));
        if (node)
            --I(node + 0x18);
    }
    return 0;
#undef G
}

static uint32_t TwigletController_Slot47_Abi(uint32_t ecx)
{
    return TwigletController_Slot47(ecx);
}
ABI_ENTRY_0049d080(TwigletController_Slot47_Entry, TwigletController_Slot47_Abi)
HOOK(0x0049d080, "TwigletController_Slot47", TwigletController_Slot47_Entry, nullptr);

// 0x0049d3c0 TwigletGenerator_Slot6 (ecx = generator; stack: a0..a3 (a2 = property block, a3 = reader); ret 0x10;
// returns eax, not reproduced: 0). Vtable slot +0x18 of TwigletGenerator 0x005aed80 (no direct callers; not executed
// in recorded runs). Setup: Object_LoadProperties, the controller (0x0049ce00), virtual +0x6c into a local,
// "release_max" (+0x470, default 10), "trigger_activate" (+0x460), "trigger_deactivate" (+0x464), then up to three
// initial bone names "initial_ic_0..2" (_sprintf into a 32-byte local, Object_ReadStringProp default "none") stored
// at +0x474 + n * 0x20 and counted in +0x468 (those not "none", __stricmp); +0x46c = 0; linked into World lists
// +0xe90 / +0xe9c / +0xea0; virtual +0xf8(5, 0), Object_setPhysicsEnabled 0x004577b0(1), virtual +0xfc(1, 2).
uint32_t TwigletGenerator_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    alignas(4) uint8_t fr[0x2c];  // the original frame (sub esp, 0x2c): F(o) = [esp+0x10+o]
#define F(o) (u(fr) + (o))
    const uint32_t ebp = self;
    orig::fn_004517b0(ebp, a0, a1, a2, a3);
    fn_0049ce00();
    VC(ebp, 0x6c, F(0x0));
    orig::fn_00453400(0xa, a3, ebp + 0x470, ebp, 0x005bb708u /* "release_max" */, a2, 0);
    orig::fn_00453450(0, 0x0058ef30u /* "trigger_activate" */, a3, ebp + 0x460, ebp, a2, 0);
    orig::fn_00453450(0, 0x0058ef1cu /* "trigger_deactivate" */, a3, ebp + 0x464, ebp, a2, 0);
    D(ebp + 0x468) = 0;
    D(ebp + 0x46c) = 0;
    for (uint32_t i = 0; i < 3; ++i)
    {
        GameFunction<int(__cdecl*)(uint32_t, uint32_t, ...)>(0x0053814f)(F(0xc), 0x0058ef0cu /* "initial_ic_%d" */, i);
        const uint32_t dest = (D(ebp + 0x468) << 5) + ebp + 0x474;
        orig::fn_004533a0(0x005bb9e0u /* "none" */, dest, ebp, F(0xc), a2, a3, 0);
        if (Stricmp((D(ebp + 0x468) << 5) + ebp + 0x474, 0x005bb9e0u) != 0)
            ++D(ebp + 0x468);
    }
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe90), ebp);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe9c), ebp);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xea0), ebp);
    VC(ebp, 0xf8, 5u, 0u);
    orig::fn_004577b0(ebp, 1);
    VC(ebp, 0xfc, 1u, 2u);
    return 0;
#undef F
}

static uint32_t TwigletGenerator_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return TwigletGenerator_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0049d3c0(TwigletGenerator_Slot6_Entry, TwigletGenerator_Slot6_Abi)
HOOK(0x0049d3c0, "TwigletGenerator_Slot6", TwigletGenerator_Slot6_Entry, nullptr);

// 0x0049d530 TwigletGenerator_Slot14 (ecx = generator; stack: a0, trigger id; ret 8; returns al = 0). Vtable slot +0x38
// of TwigletGenerator (no direct callers; not executed in recorded runs). Trigger +0x460 activates the generator,
// +0x464 deactivates it (0x0049ce70, al = 1 / 0, with the controller Game+0xc58 as its unread argument).
uint32_t TwigletGenerator_Slot14(uint32_t self, uint32_t /*a0*/, uint32_t trigger)
{
    uint32_t game = GameP();
    if (trigger == D(self + 0x460))
        return fn_0049ce70((game & 0xffffff00u) | 1, self, D(game + 0xc58)) & 0xffffff00u;
    if (trigger == D(self + 0x464))
        return fn_0049ce70(game & 0xffffff00u, self, D(game + 0xc58)) & 0xffffff00u;
    return trigger & 0xffffff00u;
}

static uint32_t TwigletGenerator_Slot14_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return TwigletGenerator_Slot14(ecx, a0, a1);
}
ABI_ENTRY_0049d530(TwigletGenerator_Slot14_Entry, TwigletGenerator_Slot14_Abi)
HOOK(0x0049d530, "TwigletGenerator_Slot14", TwigletGenerator_Slot14_Entry, nullptr);

// 0x0049d580 (eax = generator; returns eax = the initial bone count +0x468). Caller 0x0049d080.
uint32_t fn_0049d580(uint32_t gen)
{
    return D(gen + 0x468);
}

static uint32_t fn_0049d580_Abi(uint32_t eax)
{
    return fn_0049d580(eax);
}
ABI_ENTRY_0049d580(fn_0049d580_Entry, fn_0049d580_Abi)
HOOK(0x0049d580, "fn_0049d580", fn_0049d580_Entry, nullptr);

// 0x0049d590 (ebx = generator; returns eax = the new twiglet). Caller 0x0049d080. Not executed in recorded runs. Spawns
// a twiglet: with initial bones, cycles +0x46c through them, takes the bone matrix (0x00453c50(generator, name, m, 1,
// 0, 0, 1, 0, 0)) and creates a "twiglet" with it (ObjectSystem_CreateObject, a1 = the matrix, type 0x3ff, 0xaa),
// 0x0049fbe0(name; 1) and 0x0049fb60; without, rotates the generator's direction (0x00453150) about z by a random angle
// rand * 0x005bcdbc - 0x005bcb70 (0x44-byte matrix, 0x004e36a0), normalises it (0x004e7880) and places the new
// twiglet at the generator's position plus that direction times rand * 0x005bcf2c + 0x005bccbc (x and y separately;
// Object_SetPosition). Game+0x2710 counts twiglets; +0x470 (release budget) is decremented when positive. The new
// object is used without a null check.
uint32_t fn_0049d590(uint32_t gen)
{
    alignas(4) uint8_t fr[0x6c];  // the original frame (sub esp, 0x6c): F(o) = [esp+o] after the two pushes
#define F(o) (u(fr) + (o))
    const uint32_t ebx = gen;
    uint32_t esi;
    if (D(ebx + 0x468) != 0)
    {
        uint32_t n = D(ebx + 0x46c) + 1;
        D(ebx + 0x46c) = n;
        if (n >= D(ebx + 0x468))
            D(ebx + 0x46c) = 0;
        const uint32_t name = (D(ebx + 0x46c) << 5) + ebx + 0x474;
        orig::fn_00453c50(ebx, name, F(0x28), 1, 0, 0, 1, 0, 0);
        esi = orig::fn_0045f020(0, 0x005b27b0u /* "twiglet" */, D(GameP() + 0xa84), F(0x28), 0x3ff, 0, 0, 0, 0, 0xaa, 0);
        ++I(GameP() + 0x2710);
        orig::fn_0049fbe0((D(ebx + 0x46c) << 5) + ebx + 0x474, esi, 1);
        orig::fn_0049fb60(esi);
    }
    else
    {
        orig::fn_00453150(ebx, F(0x1c));
        uint32_t r = Rand();
        std::memset(P(F(0x28)), 0, 0x44);
        Fl(F(0x0)) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bcdbc) - K(0x005bcb70);
        const float angle = Fl(F(0x0));
        const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
        D(F(0x50)) = 0x3f800000u;
        D(F(0x64)) = 0x3f800000u;
        D(F(0x68)) = 1;
        const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
        Fl(F(0x28)) = c;
        Fl(F(0x2c)) = -s;
        Fl(F(0x38)) = s;
        Fl(F(0x3c)) = c;
        orig::fn_004e36a0(F(0x1c), F(0x28), 1, F(0x10));
        Normalize3(F(0x10), F(0x10));
        VC(ebx, 0x54, F(0x4));
        r = Rand();
        Fl(F(0x4)) = (static_cast<float>(static_cast<int32_t>(r)) * K(0x005bcf2c) + K(0x005bccbc)) * Fl(F(0x10)) +
                     Fl(F(0x4));
        r = Rand();
        Fl(F(0x8)) = (static_cast<float>(static_cast<int32_t>(r)) * K(0x005bcf2c) + K(0x005bccbc)) * Fl(F(0x14)) +
                     Fl(F(0x8));
        esi = orig::fn_0045f020(0, 0x005b27b0u /* "twiglet" */, D(GameP() + 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
        orig::fn_00457870(esi & 0xffffff00u, F(0x4), esi, 0, 0, 0);
        ++I(GameP() + 0x2710);
    }
    if (D(ebx + 0x470) > 0)
        --D(ebx + 0x470);
    return esi;
#undef F
}

static uint32_t fn_0049d590_Abi(uint32_t ebx)
{
    return fn_0049d590(ebx);
}
ABI_ENTRY_0049d590(fn_0049d590_Entry, fn_0049d590_Abi)
HOOK(0x0049d590, "fn_0049d590", fn_0049d590_Entry, nullptr);

// 0x0049d7a0 Twiglet_Slot6 (ecx = twiglet; stack: a0..a3 (a2 = property block or 0, a3 = reader); ret 0x10; returns
// eax, not reproduced: 0). Vtable slot +0x18 of Twiglet 0x005af020 (no direct callers; not executed in recorded runs).
// Setup: counts itself (Game+0x2710) when +0x396 is set, clears its state, random +0x46c, 0x00457810,
// Object_LoadProperties, health scaling by Game+0x558 / +0x554 (virtual +0xe8 / +0xe4) into +0x458 / +0x454, links into
// World lists +0xe90 / +0xe94 / +0xe9c / +0xea4 / +0xe7c / +0xe8c, 0x00458270(0, 0, (1 / mass or 0) * 20000) on its
// body (+0x1c4 -> +8 -> +0x2c). With a property block: "receive_trigger_activate" (+0x584), "trigger_los" (a bool
// read into its own argument slot a1; then flag 0x100 and "activation_range" +0x588, default 400), flag 0x40,
// position into +0x56c, +0x578 = 10, virtual +0xf8(0x20000, 0), animation "twiglet_stand" (0x00408e30), +0x464 = 1.
// Always: "move_power" (30), "move_speed" (46), "long_range_dist" (100), "hit_level_1" (10), "score_value" (0),
// "bof_anti_gravity" (0.5), virtual +0x70 into a local, linked into World list +0xeec.
uint32_t Twiglet_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    alignas(4) uint8_t fr[0x48];  // the original frame (sub esp, 0x48): F(o) = [esp+0x10+o]
#define F(o) (u(fr) + (o))
    const uint32_t ebp = self;
    uint32_t a1Slot = a1;  // the original reuses its a1 stack slot for the "trigger_los" bool
    if (W(ebp + 0x396) != 0)
        ++I(GameP() + 0x2710);
    D(ebp + 0x460) = 0;
    D(ebp + 0x464) = 0;
    D(ebp + 0x478) = 0;
    D(ebp + 0x55c) = 0;
    D(ebp + 0x560) = 0;
    D(ebp + 0x568) = 0;
    D(ebp + 0x470) = 0;
    D(ebp + 0x474) = 0;
    D(ebp + 0x564) = 0;
    uint32_t r = Rand();
    D(F(0x0)) = r;
    Fl(ebp + 0x46c) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bcf88) + K(0x005bcbe0);
    orig::fn_00457810(ebp);
    orig::fn_004517b0(ebp, a0, a1, a2, a3);
    bool scale = false;
    float k = 0.0f;
    if (VB(ebp, 0xe8))
    {
        k = Fl(GameP() + 0x558);
        scale = true;
    }
    else if (VB(ebp, 0xe4))
    {
        k = Fl(GameP() + 0x554);
        scale = true;
    }
    if (scale)
    {
        float v = k * Fl(ebp + 0x458);
        Fl(ebp + 0x458) = v;
        Fl(ebp + 0x454) = v;
    }
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe90), ebp);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe94), ebp);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe9c), ebp);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xea4), ebp);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe7c), ebp);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe8c), ebp);
    {
        float m = Fl(D(D(ebp + 0x1c4) + 8) + 0x2c);
        float inv = m == K(0x0056c05c) ? K(0x0056c05c) : K(0x005bcb2c) / m;
        orig::fn_00458270(ebp, 0, 0, fb(inv * K(0x005bcd34)));
    }
    if (a2 != 0)
    {
        r = orig::fn_00453450(0, 0x005bba00u /* "receive_trigger_activate" */, a3, ebp + 0x584, ebp, a2, 0);
        orig::fn_004534f0((r & 0xffffff00u) | 1, a3, u(&a1Slot), ebp, 0x005bb68cu /* "trigger_los" */, a2, 0);
        if (static_cast<uint8_t>(a1Slot))
        {
            D(ebp + 0x460) |= 0x100;
            orig::fn_004534a0(a3, ebp + 0x588, ebp, 0x0058eef8u /* "activation_range" */, a2, 0x43c80000u /* 400 */, 0);
        }
        D(ebp + 0x460) |= 0x40;
        VC(ebp, 0x54, ebp + 0x56c);
        D(ebp + 0x578) = 0x41200000u;  // 10.0
        VC(ebp, 0xf8, 0x20000u, 0u);
        orig::fn_00408e30(0x81, ebp, 0xffffffffu, 0x0058eee8u /* "twiglet_stand" */, 0, 0x3e800000u /* 0.25 */, 0, 0);
        D(ebp + 0x464) = 1;
    }
    orig::fn_004534a0(a3, ebp + 0x484, ebp, 0x005b6fe4u /* "move_power" */, a2, 0x41f00000u /* 30 */, 1);
    orig::fn_004534a0(a3, ebp + 0x480, ebp, 0x005b6fd8u /* "move_speed" */, a2, 0x42380000u /* 46 */, 1);
    orig::fn_004534a0(a3, ebp + 0x47c, ebp, 0x0058eed8u /* "long_range_dist" */, a2, 0x42c80000u /* 100 */, 1);
    orig::fn_004534a0(a3, ebp + 0x490, ebp, 0x005b7014u /* "hit_level_1" */, a2, 0x41200000u /* 10 */, 1);
    orig::fn_004534a0(a3, ebp + 0x57c, ebp, 0x005bb864u /* "score_value" */, a2, 0, 1);
    orig::fn_004534a0(a3, ebp + 0x580, ebp, 0x005b7578u /* "bof_anti_gravity" */, a2, 0x3f000000u /* 0.5 */, 1);
    VC(ebp, 0x70, F(0x4));
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xeec), ebp);
    return 0;
#undef F
}

static uint32_t Twiglet_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Twiglet_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0049d7a0(Twiglet_Slot6_Entry, Twiglet_Slot6_Abi)
HOOK(0x0049d7a0, "Twiglet_Slot6", Twiglet_Slot6_Entry, nullptr);
