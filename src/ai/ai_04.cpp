// Recovered: batch ai_04 (0x00430340-0x004367c0): the generic AI object's damage handler and virtual methods
// (GenericAIObject vtable 0x005b2958), the fake grunt platoon (FakeGruntObject 0x005a4da8), the squad (SquadObject
// 0x005ab3e8), and methods of Polewart, GoliathObject (0x005af298), GoliathFireball, GruntLadderObject, GruntObject,
// BowGruntObject, CaptainObject, SniperCaptain, PeasantGenAI (0x005aa9c0), Q112Object, Q112Boss, SaucerObject,
// ColossusObject, ZeppelinObject (0x005af500) and ShrubPatrol. (Several vtables lie back to back in .rdata; the real
// starts were found from the slot-1 descriptor getter, e.g. 0x005af298 / 0x005af500 inside "Twiglet" 0x005af020.)
// Static recovery 2026-10-05,
// compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions (as in ai_01.cpp / ai_06.cpp): every function's comment gives its address, its register/stack ABI (as
// the original reads it), what it does and the evidence. "guess" marks interpretations not proven by strings or
// behaviour. Virtual methods are named Class_SlotN (N = decimal slot index) when the class is known from the vtable's
// slot 1 descriptor ("CLASS(Name)" at descriptor +0x14); a method shared by several classes is named after the base
// class whose vtable holds it (GenericAIObject for the AI characters). Other names stay fn_XXXXXXXX.
//
// Callee notes (see ai_06.cpp for the evidence): the "ecx" inputs of 0x0042d0f0 and 0x0046e2c0 are only `push ecx`
// stack-slot reservations (passed as 0). Where the original passes a register whose low byte it just cleared
// (xor al, al / xor cl, cl) the upper bytes are passed as the original left them when they are known, otherwise 0
// (the callees test only the low byte).
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
// Memory at an absolute address (object fields: D(ebp + 0x460)).
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
// A float constant of the game's .rdata (0x0056c05c = 0.0, 0x005bcb2c = 1.0, 0x005bcb28 = 4294967296.0,
// 0x005bcbec = 0.01, 0x005bcf00 = 250.0, 0x005bcb60 = 100.0, 0x005bce88 = 75.0, 0x005bcd34 = 20000.0,
// 0x005bcb4c = 0.5).
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
// Game+0xa84: the world object (its object lists at +0xe7c.. +0xf1c).
inline uint32_t World()
{
    return D(GameP() + 0xa84);
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
inline bool IsDead(uint32_t object)
{
    return static_cast<uint8_t>(orig::fn_00454780(object)) != 0;
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
// Same, after calling the object's virtual +0xdc first (the idiom of 0x00431720, 0x00432730).
inline void ClearRefDc(uint32_t slot)
{
    uint32_t object = D(slot);
    if (object)
    {
        VC(object, 0xdc);
        Unref(D(slot));
        D(slot) = 0;
    }
}
// Debug_Printf 0x00407ff0 (cdecl, variadic): the log object Game+0x34, a format, then the arguments.
template <class... A>
inline void DebugPrintf(uint32_t format, A... args)
{
    GameFunction<void(__cdecl*)(uint32_t, uint32_t, ...)>(0x00407ff0)(D(GameP() + 0x34), format, args...);
}
// _sprintf 0x0053814f (the game's CRT).
template <class... A>
inline void Sprintf(uint32_t buffer, uint32_t format, A... args)
{
    GameFunction<int(__cdecl*)(uint32_t, uint32_t, ...)>(0x0053814f)(buffer, format, args...);
}
// x87 compare helpers as the disassembly tests them (fcomp; fnstsw; test ah, ...):
//   test ah,0x41 / jp  -> taken when a > b or unordered       test ah,0x41 / jne -> taken when !(a > b)
//   test ah,5 / jnp    -> taken when a < b                     test ah,5 / jp     -> taken when !(a < b)
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t fn_00431720(uint32_t self, uint32_t msg);
uint32_t fn_004324d0(uint32_t eax, uint32_t self, uint32_t object, uint32_t keep);
uint32_t GenericAIObject_Slot55(uint32_t self);
uint32_t fn_00432ee0(uint32_t ecx, uint32_t edx, uint32_t digit);
uint32_t fn_00432ef0(uint32_t index, uint32_t typeOut, uint32_t self, uint32_t matrix);

// ---------------------------------------------------------------------------------------------------------------
// 0x00430340 GenericAIObject_Slot52 (ecx = AI object; stack: damage message; ret 4). The AI character's damage /
// hit-message handler (vtable 0x005b2958 slot 0xd0, shared by BowGruntObject, SniperCaptain, ShrubPatrol; direct
// callers 0x00434180, 0x00435170, 0x00435fa0, 0x004371e0, 0x0048fa20). The message: +0 flags, +4.. data passed to
// 0x00430140, +0x14 damage (float), +0x28 / +0x2c source objects, +0x30 a holder object. Dead objects (0x00454780)
// ignore it. By flag:
//  - 0x800: virtual +0x254; the holder at +0x30 replaces +0x870 (an old one is released and the handler returns);
//    otherwise the character is carried off: +0x878 timer (0x0046e2c0) or a knock-back toward the holder's position
//    (0x0046f6d0 at 250 units along the 2D direction, 0x004e7780), states 0x19c / 0x19d (virtual +0x1b8).
//  - 0x2000: picked up / frozen (0x00454c90, 0x00454cc0, flags +0x488 |= 0x80000, +0x48c |= 2), 0x0042d0f0 when no
//    +0x898 object.
//  - 0x1000: killed outright: 0x0043e710, the trigger +0x528 (0x00461870), 0x00431720, unlinked from World+0xe9c,
//    anim 0x58, state 0x191.
//  - 0x10: 0x00430140 (eax = self; message+4, *holder).
//  - 0x20: virtual +0x1c0 gate; timers +0x878 / +0x87c, "turbo_idle" (0x005b6674) sound when +0x484 bit 0x10000000.
//  - otherwise damage: scaled by +0x8fc / +0x900 (flags 0x80000 / 0x100000), Object_TakeDamage 0x004555e0, the
//    health ratio against +0x950 * 0.01 (flag 2 of +0x484), reactions (anims 0x35, 0x3f..0x47, 0x77..; states 300,
//    0x12f, 0x130, 0x131, 0x197) and finally virtual +0x1b4 (source, 1, flag) when the source is in list +0x604.
// The argument's stack slot is reused by the original as a byte flag (low byte); the final virtual +0x1b4 receives
// that dword (pointer with the low byte replaced), reproduced here.
uint32_t GenericAIObject_Slot52(uint32_t self, uint32_t msg)
{
    alignas(4) uint8_t fr[0x24];  // the original frame (sub esp, 0x24): L(o) = [esp+o] after the sub
#define L(o) (u(fr) + (o))
    const uint32_t ebp = self;
    uint32_t argSlot = msg;  // [esp+0x38]: the argument, later a byte flag in its low byte
    auto setFlag = [&](uint8_t v) { argSlot = (argSlot & 0xffffff00u) | v; };
    uint32_t r = orig::fn_00454780(ebp);
    if (static_cast<uint8_t>(r))
        return r;
    uint32_t eax = D(msg);
    if (eax & 0x800)
    {
        r = VC(ebp, 0x254);
        if (!static_cast<uint8_t>(r))
            return r;
        const uint32_t ebx = ebp + 0x870;
        const uint32_t edi = D(ebx);
        D(ebp + 0x578) = 0;
        bool had = false;
        if (edi)
        {
            Unref(edi);
            D(ebx) = 0;
            had = true;
        }
        const uint32_t holder = D(msg + 0x30);
        D(ebx) = holder;
        ++I(holder + 0x28);
        if (had)
            return 1;
        D(ebp + 0xb30) = 0;
        if (VB(ebp, 0xe4))
        {
            Fl(ebp + 0x878) = orig::fn_0046e2c0(0, 0x3f000000u /* 0.5 */, 0x3fc00000u /* 1.5 */);
            return 0;
        }
        if (static_cast<uint8_t>(orig::fn_00426630(ebp)))
        {
            VC(ebp, 0x1ac, 0u);
            setFlag(1);
        }
        else
            setFlag(0);
        orig::fn_004266b0(ebp);
        orig::fn_00453b20(ebp);
        const uint32_t h = D(ebx);
        VC(h, 0x54, ebp + 0x934);
        VC(ebp, 0x54, L(0xc));
        Fl(L(0x18)) = Fl(L(0xc)) - Fl(ebp + 0x934);
        Fl(L(0x1c)) = Fl(L(0x10)) - Fl(ebp + 0x938);
        orig::fn_004e7780(L(0x18), L(0x18));  // fstp st(0)
        VC(ebp, 0x54, L(0xc));
        Fl(L(0xc)) = Fl(L(0x18)) * K(0x005bcf00) + Fl(L(0xc));
        Fl(L(0x10)) = Fl(L(0x1c)) * K(0x005bcf00) + Fl(L(0x10));
        orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), L(0xc), 0, 0, 0, 0, 0);
        orig::fn_004577b0(ebp, 0);
        const bool flag = static_cast<uint8_t>(argSlot) != 0;
        if (!(D(ebp + 0x484) & 0x2000000) && !flag)
        {
            VC(ebp, 0x18c, 0u);
            uint32_t v = D(ebp + 0x484) | 0x20;
            D(ebp + 0x484) = v;
            D(ebp + 0x484) = v & 0xfdffffffu;
            D(ebp + 0x488) &= 0xfffffffeu;
            D(ebp + 0x484) &= 0xfffeffffu;
            Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3f800000u /* 1.0 */, 0x3fe00000u /* 1.75 */);
            orig::fn_00427210(ebp);
            return VC(ebp, 0x1b8, 0x19cu);
        }
        if (flag && D(ebp + 0xab0))
            fn_004324d0(0, ebp, D(ebp + 0xab0), 1);
        VC(ebp, 0x18c, 0u);
        uint32_t v = D(ebp + 0x484) | 0x20;
        D(ebp + 0x484) = v;
        D(ebp + 0x484) = v & 0xfdffffffu;
        D(ebp + 0x488) &= 0xfffffffeu;
        D(ebp + 0x484) &= 0xfffeffffu;
        D(ebp + 0x490) = 0;
        return VC(ebp, 0x1b8, 0x19du);
    }
    if (eax & 0x2000)
    {
        if (D(ebp + 0x898))
        {
            if (D(ebp + 0x484) & 0x8000)
                D(ebp + 0x484) |= 0x200;
            else
                orig::fn_004266b0(ebp);
        }
        const uint32_t ecx = D(ebp + 0x9bc);
        if (ecx)
            VC(ecx, 0x23c, D(GameP() + 0x748), 1u);
        orig::fn_00454c90(ebp, 1 /* cl = 1 */, 1);
        orig::fn_00454cc0(ebp, ebp + 0x398);
        D(ebp + 0x488) |= 0x80000;
        D(ebp + 0x48c) |= 2;
        VC(ebp, 0x23c, D(msg + 0x28), 0u);
        if (D(ebp + 0x898))
            return D(ebp + 0x898);
        return orig::fn_0042d0f0(ebp, 0 /* push ecx: slot only */, 0, 1);
    }
    if (eax & 0x1000)
    {
        orig::fn_0043e710(ebp, ebp);
        const uint32_t ebx = D(ebp + 0x528);
        if (ebx)
            orig::fn_00461870(World(), ebx, ebp);
        fn_00431720(ebp, msg);
        D(ebp + 0x9f0) = 0;
        D(ebp + 0x9f4) = 0;
        orig::fn_004577b0(ebp, 1);
        orig::fn_00454750(D(World() + 0xe9c), ebp);
        VC(ebp, 0xf8, 0x60000u, 0u);
        VC(ebp, 0x19c, 0x58u, 0u, 0u, 0u, 0u, 0u);
        return VC(ebp, 0x1b8, 0x191u);
    }
    if (eax & 0x10)
        return orig::fn_00430140(ebp, msg + 4, D(D(msg + 0x30)));
    if (eax & 0x20)
    {
        r = VC(ebp, 0x1c0);
        if (!static_cast<uint8_t>(r))
            return r;
        if (VB(ebp, 0xe4))
        {
            Fl(ebp + 0x878) = orig::fn_0046e2c0(0, 0x3a83126fu /* 0.001 */, 0x3f000000u /* 0.5 */);
        }
        else
        {
            if (D(ebp + 0x880))
            {
                fn_004324d0(0, ebp, D(ebp + 0x880), 1);
                orig::fn_004577b0(ebp, 0);
            }
            Fl(ebp + 0x878) = orig::fn_0046e2c0(0, 0x3a83126fu /* 0.001 */, 0x3f800000u /* 1.0 */);
            if (D(ebp + 0xb08) && (D(ebp + 0x484) & 0x10000000))
            {
                D(ebp + 0x484) &= 0xcfffffffu;
                orig::fn_0047bb30(D(ebp + 0xb08), ebp, ebp + 0xb0c, 0);
                orig::fn_00408e30(0x81, D(ebp + 0xb08), 0xffffffffu, 0x005b6674u /* "turbo_idle" */, 0, 0x3e800000u,
                                  0, 0);
                orig::fn_00454750(D(World() + 0xf1c), ebp);
            }
        }
        D(ebp + 0x87c) = D(GameP() + 0xc7c);
        r = orig::fn_00426630(ebp);
        if (!static_cast<uint8_t>(r) && D(ebp + 0x460) != 0xc)
            return r;
        return VC(ebp, 0x1b0, 0u);
    }
    // 0x004308a0: damage.
    if (D(ebp + 0x484) & 0x100000)
        return eax;
    if (eax & 0x80000)
        Fl(msg + 0x14) = Fl(ebp + 0x8fc) * Fl(msg + 0x14);
    if (eax & 0x100000)
        Fl(msg + 0x14) = Fl(ebp + 0x900) * Fl(msg + 0x14);
    if (!(D(ebp + 0x488) & 0x4000) && D(msg + 0x2c))
    {
        const uint8_t bl = static_cast<uint8_t>(VC(D(msg + 0x2c), 0xe4));
        if (bl == static_cast<uint8_t>(VC(ebp, 0xe4)))
        {
            if (!VB(D(msg + 0x2c), 0xe0) || VB(ebp, 0xe0))
            {
                if (VB(ebp, 0x150))
                    D(ebp + 0xb48) = GameTime();
                r = VC(ebp, 0xe4);
                if (!static_cast<uint8_t>(r))
                    return r;
                Fl(ebp + 0x454) = Fl(msg + 0x14) + Fl(ebp + 0x454);
            }
        }
    }
    // 0x0043095c
    D(ebp + 0x484) |= 0x1000000;
    const float before = orig::fn_004555b0(ebp);   // [esp+0x18]
    const float maxHp = orig::fn_004555c0(ebp);    // [esp+0x10]
    orig::fn_004555e0(ebp, msg);
    const float after = orig::fn_004555b0(ebp);    // [esp+0x14]
    eax = D(msg);
    if (eax & 0x80000)
        Fl(msg + 0x14) = K(0x005bcb2c) / Fl(ebp + 0x8fc) * Fl(msg + 0x14);
    if (eax & 0x100000)
        Fl(msg + 0x14) = K(0x005bcb2c) / Fl(ebp + 0x900) * Fl(msg + 0x14);
    r = orig::fn_00454780(ebp);
    if (static_cast<uint8_t>(r))
        return r;
    const uint32_t ebx = msg;
    if (VB(ebp, 0x14c, ebx))
        D(ebp + 0xb48) = GameTime();
    else
        VC(ebp, 0x148);
    if (D(ebp + 0x484) & 0x10000000)
        Fl(ebp + 0xb20) = Fl(ebx + 0x14) + Fl(ebp + 0xb20);
    {
        float t = static_cast<float>(I(ebp + 0x950));
        if (I(ebp + 0x950) < 0)
            t = t + K(0x005bcb28);
        t = t * maxHp;
        t = t * K(0x005bcbec);
        if (t > after)
            D(ebp + 0x484) |= 2;
    }
    {
        const uint32_t esi = D(ebp + 0x9bc);
        if (esi)
        {
            const uint32_t ecx = D(esi + 0x570);
            if (ecx)
            {
                r = orig::fn_0044b4b0(0 /* al = 0 */, ecx, esi);
                if (r)
                    return r;
            }
            VC(esi, 0x23c, D(ebx + 0x2c), 1u);
        }
    }
    if (VB(ebp, 0x178, D(ebx + 0x2c)))
    {
        const uint32_t state = D(ebp + 0x460);
        bool toB0f = false;
        if (state == 0x97 && D(ebp + 0x570) && orig::fn_0044b4b0(0, D(ebp + 0x570), ebp) &&
            !static_cast<uint8_t>(orig::fn_0044e600()))
            toB0f = true;
        if (!toB0f)
        {
            if (state != 0x8c || !(By(ebp + 0x488) & 0x40) || !D(ebx + 0x2c))
                return 0;
            VC(ebp, 0x23c, D(ebx + 0x2c), 1u);
            return orig::fn_0042f5c0(ebp, D(ebx + 0x2c));
        }
    }
    // 0x00430b0f
    if (D(ebp + 0x460) == 0x97 && D(ebp + 0x570) && orig::fn_0044b4b0(0, D(ebp + 0x570), ebp))
        D(ebp + 0x488) |= 0x400000;
    {
        const uint32_t f484 = D(ebp + 0x484);
        setFlag(1);
        if (f484 & 0x10000000)
        {
            D(ebp + 0xb20) = 0;
            fn_00431720(ebp, ebx);
            D(ebp + 0x484) &= 0xcfffffffu;
            orig::fn_0047bb30(D(ebp + 0xb08), ebp, ebp + 0xb0c, 0);
            orig::fn_00408e30(0x81, D(ebp + 0xb08), 0xffffffffu, 0x005b6674u /* "turbo_idle" */, 0, 0x3e800000u, 0,
                              0);
            orig::fn_00454750(D(World() + 0xf1c), ebp);
            D(ebp + 0x490) = 0x41700000;  // 15.0
            VC(ebp, 0x19c, 0x5cu, 1u, 0u, 0u, 0u, 0u);
            return VC(ebp, 0x1b8, 0x197u);
        }
        uint32_t anim = 0, state = 0;
        auto stopMove = [&]() {
            if (!(D(ebp + 0x484) & 0x20000) && !(D(ebp + 0x488) & 0x200000))
                VC(ebp, 0x18c, 0u);
        };
        if (D(ebx) & 0x100)
        {
            if (D(ebp + 0x460) == 0x131)
                goto L_430fa4;
            stopMove();
            D(ebp + 0x484) &= 0xfeffffffu;
            anim = ((Rand() * 6) >> 15) + 0x77;  // sar: rand * 6 is non-negative
            goto L_430f79;
        }
        if ((f484 & 0x8000) || static_cast<uint8_t>(orig::fn_00426630(ebp)))
            goto L_430fa4;
        {
            const float inv = K(0x005bcb2c) / maxHp;
            const float thr = Fl(ebp + 0x9e0) * K(0x005bcbec);
            if (inv * before > thr && !(inv * after > thr) && VB(ebp, 0x200))
            {
                stopMove();
                VC(ebp, 0x19c, 0x35u, 0u, 0u, 0u, 0u, 0u);
                state = 300;
                goto L_430f94;
            }
        }
        // 0x00430cf8
        if (!VB(ebp, 0x204))
            goto L_430fa4;
        if (D(ebp + 0x898))
        {
            VC(D(ebp + 0x898), 0x15c, 0u);
            Unref(D(ebp + 0x898));
            D(ebp + 0x898) = 0;
            D(ebp + 0x484) &= 0xffff7fffu;
        }
        if (By(ebx) & 4)
        {
            stopMove();
            D(ebp + 0x484) &= 0xfeffffffu;
            anim = ((Rand() << 1) >> 15) + 0x47;
            goto L_430f79;
        }
        if (!(Fl(ebx + 0x14) > Fl(ebp + 0x9e8)))
        {
            const uint32_t s = D(ebp + 0x460);
            if (s == 0x12f || s == 0x132 || !VB(ebp, 0x20c))
                goto L_430fa4;
            stopMove();
            D(ebp + 0x484) &= 0xfeffffffu;
            const uint32_t q = (Rand() * 0x190) >> 15;
            if (q < 100)
                VC(ebp, 0x19c, 0x3fu, 1u, 0u, 0u, 0u, 0u);
            else if (q < 200)
                VC(ebp, 0x19c, 0x40u, 1u, 0u, 0u, 0u, 0u);
            else if (q < 300)
                VC(ebp, 0x19c, 0x41u, 1u, 0u, 0u, 0u, 0u);
            else
                VC(ebp, 0x19c, 0x42u, 1u, 0u, 0u, 0u, 0u);
            VC(ebp, 0x1b8, 0x12fu);
            setFlag(0);
            goto L_430fa4;
        }
        if (!(Fl(ebx + 0x14) > Fl(ebp + 0x9ec)))
        {
            stopMove();
            D(ebp + 0x484) &= 0xfeffffffu;
            const uint32_t prev = D(ebp + 0x498);
            if (prev == 0x43)
                VC(ebp, 0x19c, 0x44u, 0u, 0u, 0u, 0u, 0u);
            else if (prev == 0x44)
                VC(ebp, 0x19c, 0x43u, 0u, 0u, 0u, 0u, 0u);
            else if (((Rand() * 0xc8) >> 15) < 100)
                VC(ebp, 0x19c, 0x43u, 0u, 0u, 0u, 0u, 0u);
            else
                VC(ebp, 0x19c, 0x44u, 0u, 0u, 0u, 0u, 0u);
            VC(ebp, 0x1b8, 0x130u);
            setFlag(0);
            goto L_430fa4;
        }
        stopMove();
        D(ebp + 0x484) &= 0xfeffffffu;
        anim = ((Rand() << 1) >> 15) + 0x45;
    L_430f79:
        VC(ebp, 0x19c, anim, 0u, 0u, 0u, 0u, 0u);
        state = 0x131;
    L_430f94:
        VC(ebp, 0x1b8, state);
        setFlag(0);
    }
L_430fa4:
    {
        const uint32_t f488 = D(ebp + 0x488);
        if ((f488 & 4) || (f488 & 0x400000))
            return f488;
        const uint32_t src = D(ebx + 0x2c);
        if (!src)
            return f488;
        r = orig::fn_00454780(src);
        if (static_cast<uint8_t>(r))
            return r;
        r = orig::fn_00454690(D(ebp + 0x604), D(ebx + 0x2c));
        if (!static_cast<uint8_t>(r))
            return r;
        return VC(ebp, 0x1b4, D(ebx + 0x2c), 1u, argSlot);
    }
#undef L
}

static uint32_t GenericAIObject_Slot52_Abi(uint32_t ecx, uint32_t a0)
{
    return GenericAIObject_Slot52(ecx, a0);
}
ABI_ENTRY_00430340(GenericAIObject_Slot52_Entry, GenericAIObject_Slot52_Abi)
HOOK(0x00430340, "GenericAIObject_Slot52", GenericAIObject_Slot52_Entry, nullptr);

// 0x00431720 (stack: AI object, damage message or 0; ret 8). The AI character's death bookkeeping, done once (flag 4
// of +0x48c): clears +0x454 / +0x52c / +0x530; when the kill counts (message without flag 0x8000, or a holder at
// +0x870, or 0x00426630) the kill counters are raised (virtual +0xe8: Game+0xbb8 += 1; not for types +0x418 0x57,
// 0x58, 0x46, 0x49, 0x44: Game+0x270c++; the score +0x47c goes to Game+0x2728 when negative, else Game+0x2724). The
// squad (+0x9bc) is told (virtual +0x23c (player, 1)) unless 0x0044b4b0 objects. With an alarm value +0x574 and a
// living source (+0x2c) in list +0x604, every other living member of World+0xe7c whose virtual +0x10c says yes gets
// 0x0042c700 (eax = member, ebx = source; +0x574). Then +0x574 = 0, 0x0042cc10, renderer virtual +0xd8 (self+0xb50, 0),
// 0x004266b0, the trigger +0x528 is sent (0x00461870) and cleared, and the object +0x894 is stopped (virtual +0xdc) and
// released. Callers 0x00409110, 0x0042e1c0, 0x00430340, 0x00431310, 0x004319e0, 0x0043ee90.
uint32_t fn_00431720(uint32_t self, uint32_t msg)
{
    const uint32_t ebp = self;
    if (By(ebp + 0x48c) & 4)
    {
        D(ebp + 0x454) = 0;
        return 4;
    }
    D(ebp + 0x454) = 0;
    D(ebp + 0x48c) |= 4;
    D(ebp + 0x530) = 0;
    D(ebp + 0x52c) = 0;
    const uint32_t ebx = msg;
    bool count;
    if (ebx && !(D(ebx) & 0x8000))
        count = true;
    else if (D(ebp + 0x870))
        count = true;
    else
        count = static_cast<uint8_t>(orig::fn_00426630(ebp)) != 0;
    if (count)
    {
        if (VB(ebp, 0xe8))
            Fl(GameP() + 0xbb8) = Fl(GameP() + 0xbb8) + K(0x005bcb2c);
        const uint32_t type = D(ebp + 0x418);
        if (type != 0x57 && type != 0x58 && type != 0x46 && type != 0x49 && type != 0x44 && VB(ebp, 0xe8))
            ++I(GameP() + 0x270c);
        const float score = Fl(ebp + 0x47c);
        if (score < K(0x0056c05c))
            Fl(GameP() + 0x2728) = score + Fl(GameP() + 0x2728);
        else
            Fl(GameP() + 0x2724) = score + Fl(GameP() + 0x2724);
    }
    // 0x0043181b
    const uint32_t squad = D(ebp + 0x9bc);
    if (squad)
    {
        const uint32_t ecx = D(squad + 0x570);
        if ((!ecx || !orig::fn_0044b4b0(0 /* al = 0 */, ecx, squad)) && ebx)
            VC(squad, 0x23c, D(GameP() + 0x748), 1u);
    }
    if (D(ebp + 0x574) && ebx && !(D(ebx) & 0x8000) && D(ebx + 0x2c) && !(D(ebp + 0x488) & 0x4000) &&
        !IsDead(D(ebx + 0x2c)) && VB(D(ebx + 0x2c), 0xe4) &&
        static_cast<uint8_t>(orig::fn_00454690(D(ebp + 0x604), D(ebx + 0x2c))))
    {
        uint32_t iter[2] = {0, 0};
        for (uint32_t m = orig::fn_004e7340(D(World() + 0xe7c), u(iter)); m; m = orig::fn_004e73c0(u(iter)))
        {
            if (m != ebp && !IsDead(m) && VB(m, 0x10c))
                orig::fn_0042c700(m, D(ebx + 0x2c), D(ebp + 0x574));
        }
        if (iter[1])
            --I(iter[1] + 0x18);
    }
    D(ebp + 0x574) = 0;
    orig::fn_0042cc10(ebp);
    VC(D(GameP() + 0x2c), 0xd8, ebp + 0xb50, 0u);
    orig::fn_004266b0(ebp);
    if (D(ebp + 0x528))
    {
        orig::fn_00461870(World(), D(ebp + 0x528), ebp);
        D(ebp + 0x528) = 0;
    }
    ClearRefDc(ebp + 0x894);
    return 0;
}

static uint32_t fn_00431720_Abi(uint32_t a0, uint32_t a1)
{
    return fn_00431720(a0, a1);
}
ABI_ENTRY_00431720(fn_00431720_Entry, fn_00431720_Abi)
HOOK(0x00431720, "fn_00431720", fn_00431720_Entry, nullptr);

// 0x00431f70 GenericAIObject_Slot46 (ecx = AI object; stack: out target, out vec3, out value, (unused); ret 0x10).
// When the object has +0x46c or +0x468 set: *a0 = +0x46c, *a1 = +0x470..+0x478, *a2 = +0x468, returns 1; else
// *a0 = *a2 = 0 and returns 0. Virtual slot 0xb8 of GenericAIObject (0x005b2958) and its subclasses. Not executed in
// recorded runs.
uint32_t GenericAIObject_Slot46(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2)
{
    const uint32_t eax = D(self + 0x46c);
    if (!eax && !D(self + 0x468))
    {
        D(a0) = 0;
        D(a2) = 0;
        return 0;
    }
    D(a0) = eax;
    D(a1) = D(self + 0x470);
    D(a1 + 4) = D(self + 0x474);
    D(a1 + 8) = D(self + 0x478);
    D(a2) = D(self + 0x468);
    return 1;
}

static uint32_t GenericAIObject_Slot46_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t /*a3*/)
{
    return GenericAIObject_Slot46(ecx, a0, a1, a2);
}
ABI_ENTRY_00431f70(GenericAIObject_Slot46_Entry, GenericAIObject_Slot46_Abi)
HOOK(0x00431f70, "GenericAIObject_Slot46", GenericAIObject_Slot46_Entry, nullptr);

// 0x00431fd0 GenericAIObject_Slot42 (ecx = AI object; stack: 5 arguments, a0 = other object, a3 = float; ret 0x14).
// Returns 1 at once when the object is the one the player (Game+0x748) holds (0x00475d40) and a0 is what it holds
// (0x004876b0). Otherwise the base test 0x004548f0 (ecx = self; a0..a4) decides, then with a0: a living a0 seen at
// more than 75 (a3 > 75.0) while flag 0x10 of +0x48c is set joins the list +0x518 (virtual +0x1c finds, +0x14 adds;
// reference +0x28 taken); squad mates (a0 virtual +0x10c, same +0x9bc) and state 0x19b give 0. Virtual slot 0xa8.
// Not executed in recorded runs.
uint32_t GenericAIObject_Slot42(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    const uint32_t edi = self;
    const uint32_t esi = a0;
    const uint32_t player = D(GameP() + 0x748);
    if (player && orig::fn_00475d40(player) == edi)
    {
        const uint32_t held = orig::fn_004876b0(edi);
        if (held && esi == held)
            return 1;
    }
    uint8_t bl = static_cast<uint8_t>(orig::fn_004548f0(edi, esi, a1, a2, a3, a4));
    if (esi)
    {
        if (!IsDead(esi) && (By(edi + 0x48c) & 0x10) && bf(a3) > K(0x005bce88))
        {
            if (!VC(D(edi + 0x518), 0x1c, esi))
            {
                VC(D(edi + 0x518), 0x14, esi);
                ++I(esi + 0x28);
            }
        }
        if (VB(esi, 0x10c) && D(edi + 0x9bc) && D(edi + 0x9bc) == D(esi + 0x9bc))
            bl = 0;
        if (D(edi + 0x460) == 0x19b)
            bl = 0;
    }
    return bl;
}

static uint32_t GenericAIObject_Slot42_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                           uint32_t a4)
{
    return GenericAIObject_Slot42(ecx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_00431fd0(GenericAIObject_Slot42_Entry, GenericAIObject_Slot42_Abi)
HOOK(0x00431fd0, "GenericAIObject_Slot42", GenericAIObject_Slot42_Entry, nullptr);

// 0x004320b0 GenericAIObject_Slot44 (ecx = AI object; stack: other object, vec3 a, vec3 b, (unused); ret 0x10).
// Notes a contact / hit by a0 unless the object is held by the player and a0 is the holder. With a0: +0x484 |= 0x800;
// for a valid a0 (0x004531e0) with +0x428 set: states 0x96 / 0x97 compare the 2D distance to 0x0046f400's point with
// 100 (squared: < 100 -> 0x0046f440, else 0x0046f480), other states except 0x10f take a0 as target (0x004324d0
// (0, self, a0, 1)). Unless flag 0x20 of +0x48c is already set, a per-state test (switch on +0x460, jump tables
// 0x00432320 / 0x004323dc) decides whether to record it: the vertical velocity (Body_getLinearVelocity 0x00458000) goes
// to +0x554, a0 (referenced) to +0x538, *a1 to +0x53c, *a2 to +0x548, and +0x48c |= 0x20. Virtual slot 0xb0. Not
// executed in recorded runs.
uint32_t GenericAIObject_Slot44(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2)
{
    alignas(4) uint8_t fr[0x18];  // sub esp, 0x18: velocity / point at +0, position at +0xc
#define L(o) (u(fr) + (o))
    const uint32_t edi = self;
    const uint32_t ebp = a0;
    const uint32_t player = D(GameP() + 0x748);
    if (player && orig::fn_00475d40(player) == edi)
    {
        const uint32_t held = orig::fn_004876b0(edi);
        if (held && ebp == held)
            return held;
    }
    if (ebp)
    {
        D(edi + 0x484) |= 0x800;
        if (static_cast<uint8_t>(orig::fn_004531e0(ebp)) && D(edi + 0x428))
        {
            const uint32_t s = D(edi + 0x460);
            if (s == 0x97 || s == 0x96)
            {
                VC(edi, 0x54, L(0xc));
                const uint32_t esi = D(GameP() + 0xaa4);
                orig::fn_0046f400(edi, L(0), esi);
                const float dy = Fl(L(4)) - Fl(L(0x10));
                const float dx = Fl(L(0)) - Fl(L(0xc));
                if (dx * dx + dy * dy < K(0x005bcb60))
                    orig::fn_0046f440(esi, edi);
                else
                    orig::fn_0046f480(edi, esi);
            }
            else if (s != 0x10f)
                fn_004324d0(0, edi, ebp, 1);
        }
    }
    // 0x004321a8
    if (By(edi + 0x48c) & 0x20)
        return 0;
    bool record = false;
    switch (D(edi + 0x460))
    {
    case 0x3e:
        record = ebp && static_cast<uint8_t>(orig::fn_004531e0(ebp));
        break;
    case 0x3d:
    case 0x5b:
        record = ebp != 0;
        break;
    case 0x20:
    case 0xa0:
    case 0xa1:
        record = ebp == D(edi + 0x904);
        break;
    case 0x7b:
        record = ebp == D(edi + 0x898);
        break;
    case 0x46:
    case 0xe8:
    case 0xea:
    case 0x193:
        record = true;
        break;
    case 0x19d:
        record = ebp && !static_cast<uint8_t>(orig::fn_004531e0(ebp));
        break;
    case 0xc9:
        orig::fn_00458000(edi, L(0));
        record = !(Fl(L(8)) > K(0x0056c05c)) && static_cast<uint8_t>(orig::fn_00454bc0(edi));
        break;
    case 0x19b:
        if (ebp)
            break;
        // fall through (0x0043229c)
    case 0xb8:
    case 0xb9:
    case 0xba:
    case 0xbd:
    case 0xbe:
    case 0xc4:
    case 0xc5:
    case 0x195:
    case 0x197:
        orig::fn_00458000(edi, L(0));
        record = !(Fl(L(8)) > K(0x0056c05c));
        break;
    default:
        break;
    }
    if (!record)
        return 0;
    orig::fn_00458000(edi, L(0));
    D(edi + 0x554) = D(L(8));
    D(edi + 0x538) = ebp;
    if (ebp)
        ++I(ebp + 0x28);
    D(edi + 0x53c) = D(a1);
    D(edi + 0x540) = D(a1 + 4);
    D(edi + 0x544) = D(a1 + 8);
    D(edi + 0x548) = D(a2);
    D(edi + 0x54c) = D(a2 + 4);
    D(edi + 0x550) = D(a2 + 8);
    D(edi + 0x48c) |= 0x20;
    return 0;
#undef L
}

static uint32_t GenericAIObject_Slot44_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t /*a3*/)
{
    return GenericAIObject_Slot44(ecx, a0, a1, a2);
}
ABI_ENTRY_004320b0(GenericAIObject_Slot44_Entry, GenericAIObject_Slot44_Abi)
HOOK(0x004320b0, "GenericAIObject_Slot44", GenericAIObject_Slot44_Entry, nullptr);

// 0x004324d0 (eax = value, ebx = AI object; stack: object, keep; ret 8). Sets the AI target: +0x510 = eax; keep == 0
// releases +0x50c; otherwise, with an object, the old +0x50c is released and the object stored (reference taken),
// except the player (Game+0x748) when virtual +0xe8 says so. Callers include 0x00430340, 0x004320b0, 0x0043ee90.
uint32_t fn_004324d0(uint32_t eax, uint32_t self, uint32_t object, uint32_t keep)
{
    const uint32_t ebx = self;
    D(ebx + 0x510) = eax;
    if (!static_cast<uint8_t>(keep))
    {
        ClearRef(ebx + 0x50c);
        return 0;
    }
    if (!object)
        return keep;
    ClearRef(ebx + 0x50c);
    if (!VB(ebx, 0xe8) || object != D(GameP() + 0x748))
    {
        D(ebx + 0x50c) = object;
        ++I(object + 0x28);
    }
    return 0;
}

static uint32_t fn_004324d0_Abi(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1)
{
    return fn_004324d0(eax, ebx, a0, a1);
}
ABI_ENTRY_004324d0(fn_004324d0_Entry, fn_004324d0_Abi)
HOOK(0x004324d0, "fn_004324d0", fn_004324d0_Entry, nullptr);

// 0x00432590 GenericAIObject_Slot40 (ecx = AI object; stack: other object; ret 4). Whether a0 is a valid target: the
// player is refused (0) when virtual +0xe8 says no and the object is the player's held object (0x00475d40) or the
// player's second slot (0x00475d30); a0 equal to the current target +0x50c gives 0; then, when the object has a team
// (virtual +0xa4 non-zero), the same team as a0 gives 0. Returns 1 otherwise. Virtual slot 0xa0. Not executed in
// recorded runs.
uint32_t GenericAIObject_Slot40(uint32_t self, uint32_t a0)
{
    const uint32_t esi = self;
    const uint32_t edi = a0;
    if (edi == D(GameP() + 0x748))
    {
        if (VB(esi, 0xe8))
            return 1;
        const uint32_t player = D(GameP() + 0x748);
        if (esi == orig::fn_00475d40(player))
            return 1;
        if (esi == orig::fn_00475d30(player))
            return 1;
    }
    if (edi == D(esi + 0x50c))
        return 0;
    if (!VC(esi, 0xa4))
        return 1;
    const uint32_t team = VC(edi, 0xa4);
    if (team == VC(esi, 0xa4))
        return 0;
    return 1;
}

static uint32_t GenericAIObject_Slot40_Abi(uint32_t ecx, uint32_t a0)
{
    return GenericAIObject_Slot40(ecx, a0);
}
ABI_ENTRY_00432590(GenericAIObject_Slot40_Entry, GenericAIObject_Slot40_Abi)
HOOK(0x00432590, "GenericAIObject_Slot40", GenericAIObject_Slot40_Entry, nullptr);

// 0x00432620 GenericAIObject_Slot41 (ecx = AI object): returns +0x510 (the value 0x004324d0 stores). Virtual slot 0xa4.
// Not executed in recorded runs.
uint32_t GenericAIObject_Slot41(uint32_t self)
{
    return D(self + 0x510);
}

static uint32_t GenericAIObject_Slot41_Abi(uint32_t ecx)
{
    return GenericAIObject_Slot41(ecx);
}
ABI_ENTRY_00432620(GenericAIObject_Slot41_Entry, GenericAIObject_Slot41_Abi)
HOOK(0x00432620, "GenericAIObject_Slot41", GenericAIObject_Slot41_Entry, nullptr);

// 0x00432630 GenericAIObject_Slot62 (ecx = AI object; stack: collision flags, value; ret 8). Sets the body's collision
// filter (Body_SetCollisionFilter 0x00459260): in the state checked by 0x0044e600 (and not state 0x190, flags neither
// 0x4000 nor 0x100) it uses 0x20000; otherwise the flags, plus 0x400000 when the object is alive, the flags are
// neither 0x4000 nor 0x100 and the state is not 0x19a. Virtual slot 0xf8.
uint32_t GenericAIObject_Slot62(uint32_t self, uint32_t flags, uint32_t a1)
{
    const uint32_t esi = self;
    uint32_t edi = flags;
    if (static_cast<uint8_t>(orig::fn_0044e600()) && D(esi + 0x460) != 0x190 && edi != 0x4000 && edi != 0x100)
        return orig::fn_00459260(esi, 0x20000, a1);
    if (!IsDead(esi) && edi != 0x4000 && edi != 0x100 && D(esi + 0x460) != 0x19a)
        edi |= 0x400000;
    return orig::fn_00459260(esi, edi, a1);
}

static uint32_t GenericAIObject_Slot62_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return GenericAIObject_Slot62(ecx, a0, a1);
}
ABI_ENTRY_00432630(GenericAIObject_Slot62_Entry, GenericAIObject_Slot62_Abi)
HOOK(0x00432630, "GenericAIObject_Slot62", GenericAIObject_Slot62_Entry, nullptr);

// 0x004326b0 GenericAIObject_Slot35 (ecx = AI object; stack: float out; ret 4). Writes a float: +0x888 when flag
// 0x1000000 of +0x488, -1.0 in state 0xe7, +0xb04 / +0xaec when +0x484 has 0x10000000 (with / without 0x20000000),
// else 0. Virtual slot 0x8c. Not executed in recorded runs.
uint32_t GenericAIObject_Slot35(uint32_t self, uint32_t out)
{
    if (D(self + 0x488) & 0x1000000)
    {
        D(out) = D(self + 0x888);
        return D(out);
    }
    if (D(self + 0x460) == 0xe7)
    {
        D(out) = 0xbf800000u;
        return 0;
    }
    const uint32_t f = D(self + 0x484);
    if (f & 0x10000000)
    {
        if (f & 0x20000000)
            D(out) = D(self + 0xb04);
        else
            D(out) = D(self + 0xaec);
        return 0;
    }
    D(out) = 0;
    return f;
}

static uint32_t GenericAIObject_Slot35_Abi(uint32_t ecx, uint32_t a0)
{
    return GenericAIObject_Slot35(ecx, a0);
}
ABI_ENTRY_004326b0(GenericAIObject_Slot35_Entry, GenericAIObject_Slot35_Abi)
HOOK(0x004326b0, "GenericAIObject_Slot35", GenericAIObject_Slot35_Entry, nullptr);

// 0x00432730 GenericAIObject_Slot55 (ecx = AI object). The AI character's teardown, done once (flag 0x10000 of
// +0x48c): releases every member of the list +0x518 (iterator 0x004e7340 / 0x004e73c0) and destroys the list
// (virtual +0x10 (1)); releases the objects +0x894, +0x46c, +0x538, +0x88c, +0xadc, +0xae0, +0x50c, +0x570 (after
// 0x0044b7f0 (cl = 0) when +0x484 has 0x80000, which it clears), +0x880, +0x9bc, +0x57c, +0xb54, +0x870, +0x864 (after
// 0x004723f0 (cl = 0)), +0x588, +0xab0 (clearing 0x2000 of +0x484), +0xab4 (stopped by virtual +0xdc first);
// 0x0042bb10 / 0x0042ba30 for flags 0x20000 / 0x40000 of +0x488, flag 0x400 of +0x488 gives back Game+0xc48; then
// virtual +0x18c (1), 0x004266b0, +0x854 (stopped and released), renderer virtual +0xd8 for +0xb40 and +0xb4c,
// virtual +0xfc (0, 2), 0x004a0fb0 (esi = +0x61c), 0x0047b9f0 (sound +0xb08; self, tag word +0x394, 0), 0x00455a00 on
// +0xb10, 0x004246c0 (self) and Object_Remove 0x004547b0. Virtual slot 0xdc (destroy, guess); direct callers
// 0x00432de0, 0x00433a90, 0x00434110, 0x004344b0, 0x00435330, 0x00435860, 0x00486bf0.
uint32_t GenericAIObject_Slot55(uint32_t self)
{
    const uint32_t ebx = self;
    uint32_t iter[2] = {0, 0};
    if (D(ebx + 0x48c) & 0x10000)
        return 0x10000;
    D(ebx + 0x48c) |= 0x10000;
    if (D(ebx + 0x518))
    {
        for (uint32_t m = orig::fn_004e7340(D(ebx + 0x518), u(iter)); m; m = orig::fn_004e73c0(u(iter)))
            Unref(m);
        if (D(ebx + 0x518))
            VC(D(ebx + 0x518), 0x10, 1u);
        D(ebx + 0x518) = 0;
    }
    ClearRef(ebx + 0x894);
    ClearRef(ebx + 0x46c);
    ClearRef(ebx + 0x538);
    ClearRef(ebx + 0x88c);
    ClearRef(ebx + 0xadc);
    ClearRef(ebx + 0xae0);
    ClearRef(ebx + 0x50c);
    if (D(ebx + 0x570))
    {
        if (D(ebx + 0x484) & 0x80000)
        {
            D(ebx + 0x484) &= 0xfff7ffffu;
            orig::fn_0044b7f0(D(ebx + 0x570), 0 /* cl = 0 */);
        }
        Unref(D(ebx + 0x570));
        D(ebx + 0x570) = 0;
    }
    if (D(ebx + 0x488) & 0x20000)
        orig::fn_0042bb10(ebx);
    if (D(ebx + 0x488) & 0x40000)
        orig::fn_0042ba30(ebx);
    if (D(ebx + 0x488) & 0x400)
    {
        --I(GameP() + 0xc48);
        D(ebx + 0x488) &= 0xfffffbffu;
    }
    ClearRef(ebx + 0x880);
    ClearRef(ebx + 0x9bc);
    ClearRef(ebx + 0x57c);
    ClearRef(ebx + 0xb54);
    ClearRef(ebx + 0x870);
    if (D(ebx + 0x864))
    {
        orig::fn_004723f0(D(ebx + 0x864), 0 /* cl = 0 */);
        Unref(D(ebx + 0x864));
        D(ebx + 0x864) = 0;
    }
    ClearRef(ebx + 0x588);
    if (D(ebx + 0xab0))
    {
        Unref(D(ebx + 0xab0));
        D(ebx + 0xab0) = 0;
        D(ebx + 0x484) &= 0xffffdfffu;
    }
    ClearRefDc(ebx + 0xab4);
    VC(ebx, 0x18c, 1u);
    orig::fn_004266b0(ebx);
    ClearRefDc(ebx + 0x854);
    VC(D(GameP() + 0x2c), 0xd8, ebx + 0xb40, 0u);
    VC(D(GameP() + 0x2c), 0xd8, ebx + 0xb4c, 0u);
    VC(ebx, 0xfc, 0u, 2u);
    orig::fn_004a0fb0(0 /* edx: left by the virtual call */, ebx + 0x61c);
    const uint32_t tag = W(ebx + 0x394);
    orig::fn_0047b9f0(tag & 0xff00u /* movzx eax; xor al, al */, ebx + 0xb08, ebx, tag, 0);
    const uint32_t v = orig::fn_00455a00(D(ebx + 0xb10));
    D(ebx + 0xb10) = v;
    orig::fn_004246c0(v & 0xffffff00u /* xor al, al */, ebx);
    orig::fn_004547b0(ebx);
    if (iter[1])
        --I(iter[1] + 0x18);
    return iter[1];
}

static uint32_t GenericAIObject_Slot55_Abi(uint32_t ecx)
{
    return GenericAIObject_Slot55(ecx);
}
ABI_ENTRY_00432730(GenericAIObject_Slot55_Entry, GenericAIObject_Slot55_Abi)
HOOK(0x00432730, "GenericAIObject_Slot55", GenericAIObject_Slot55_Entry, nullptr);

// 0x00432cb0 FakeGruntObject_Slot6 (ecx = fake grunt platoon; stack: 4 load arguments; ret 0x10). Init: clears the
// platoon counts +0xb58..+0xb68 and the 32 particle slots +0xb6c, 0x00457810, Object_LoadProperties 0x004517b0
// (a0..a3), links into World+0xe94 and +0xf1c, 0x00458270 (edx = self; 0, 0, 20000 / mass or 0: mass at
// [[+0x1c4]+8]+0x2c), "kill_range" (Object_ReadValuePropDef, default 60.0) into +0xbf4, +0x604 / +0x608 = World+0xe88 /
// World+0xe7c, virtual +0xf4 (0) and +0xbf8 = 1. FakeGruntObject vtable 0x005a4da8 slot 0x18. Not executed in
// recorded runs.
uint32_t FakeGruntObject_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebx = self;
    D(ebx + 0xb58) = 0;
    D(ebx + 0xb5c) = 0;
    D(ebx + 0xb60) = 0;
    D(ebx + 0xb64) = 0;
    D(ebx + 0xb68) = 0;
    std::memset(P(ebx + 0xb6c), 0, 0x20 * 4);
    orig::fn_00457810(ebx);
    orig::fn_004517b0(ebx, a0, a1, a2, a3);
    orig::fn_004546d0(D(World() + 0xe94), ebx);
    orig::fn_004546d0(D(World() + 0xf1c), ebx);
    const float mass = Fl(D(D(ebx + 0x1c4) + 8) + 0x2c);
    float f;
    if (mass == K(0x0056c05c))
        f = K(0x0056c05c);
    else
        f = K(0x005bcb2c) / mass;
    f = f * K(0x005bcd34);
    orig::fn_00458270(ebx, 0, 0, fb(f));
    orig::fn_004534a0(a3, ebx + 0xbf4, ebx, 0x005b6600u /* "kill_range" */, a2, 0x42700000u /* 60.0 */, 0);
    D(ebx + 0x604) = D(World() + 0xe88);
    D(ebx + 0x608) = D(World() + 0xe7c);
    const uint32_t r = VC(ebx, 0xf4, 0u);
    By(ebx + 0xbf8) = 1;
    return r;
}

static uint32_t FakeGruntObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return FakeGruntObject_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00432cb0(FakeGruntObject_Slot6_Entry, FakeGruntObject_Slot6_Abi)
HOOK(0x00432cb0, "FakeGruntObject_Slot6", FakeGruntObject_Slot6_Entry, nullptr);

// 0x00432de0 FakeGruntObject_Slot55 (ecx = fake grunt platoon). Destroy: 0x00432730, then releases the +0xb68 particle
// effects at +0xb6c (Particle_Release 0x0046d9a0 with the system 0x0041f4f0 (Game+0xa90, 2)) and sets +0xb68 = 0.
// FakeGruntObject vtable slot 0xdc. Not executed in recorded runs.
uint32_t FakeGruntObject_Slot55(uint32_t self)
{
    const uint32_t ebx = self;
    GenericAIObject_Slot55(ebx);
    const uint32_t sys = orig::fn_0041f4f0(D(GameP() + 0xa90), 2);
    for (uint32_t i = 0; i < D(ebx + 0xb68); ++i)
    {
        const uint32_t slot = ebx + 0xb6c + 4 * i;
        if (D(slot))
        {
            orig::fn_0046d9a0(D(slot), sys);
            D(slot) = 0;
        }
    }
    D(ebx + 0xb68) = 0;
    return 0;
}

static uint32_t FakeGruntObject_Slot55_Abi(uint32_t ecx)
{
    return FakeGruntObject_Slot55(ecx);
}
ABI_ENTRY_00432de0(FakeGruntObject_Slot55_Entry, FakeGruntObject_Slot55_Abi)
HOOK(0x00432de0, "FakeGruntObject_Slot55", FakeGruntObject_Slot55_Entry, nullptr);

// 0x00432e60 FakeGruntObject_Slot7 (ecx = fake grunt platoon; stack: description out; ret 4). +0x45c = the
// "collision_radius" property (Object_ReadValuePropDef, esi = 0) or, when absent, (virtual +0x94 + virtual +0x90
// first floats) * 0.5; *a0 = 0x4000 and byte a0+0x7b = 1. FakeGruntObject vtable slot 0x1c. Not executed in recorded
// runs.
uint32_t FakeGruntObject_Slot7(uint32_t self, uint32_t out)
{
    alignas(4) uint8_t fr[0x14];  // sub esp, 0x14
#define L(o) (u(fr) + (o))
    const uint32_t ebx = self;
    std::memset(fr, 0, sizeof fr);
    float v;
    if (!orig::fn_004534a0(0, L(0), ebx, 0x005b65ecu /* "collision_radius" */, 0, 0, 0))
    {
        VC(ebx, 0x90, L(8), L(0), 0u);
        VC(ebx, 0x94, L(8), L(4), 0u);
        v = (Fl(L(4)) + Fl(L(0))) * K(0x005bcb4c);
    }
    else
        v = Fl(L(0));
    Fl(ebx + 0x45c) = v;
    D(out) = 0x4000;
    By(out + 0x7b) = 1;
    return out;
#undef L
}

static uint32_t FakeGruntObject_Slot7_Abi(uint32_t ecx, uint32_t a0)
{
    return FakeGruntObject_Slot7(ecx, a0);
}
ABI_ENTRY_00432e60(FakeGruntObject_Slot7_Entry, FakeGruntObject_Slot7_Abi)
HOOK(0x00432e60, "FakeGruntObject_Slot7", FakeGruntObject_Slot7_Entry, nullptr);

// 0x00432ee0 (ecx = index, edx = string; stack: digit; caller pops): string[index] = '0' + digit. Returns the
// character in AL. Caller 0x00432ef0 (patches the reference names "ref_grunt_0x" etc.).
uint32_t fn_00432ee0(uint32_t ecx, uint32_t edx, uint32_t digit)
{
    const uint8_t c = static_cast<uint8_t>(static_cast<uint8_t>(digit) + 0x30);
    By(edx + ecx) = c;
    return c;
}

static uint32_t fn_00432ee0_Abi(uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return fn_00432ee0(ecx, edx, a0);
}
ABI_ENTRY_00432ee0(fn_00432ee0_Entry, fn_00432ee0_Abi)
HOOK(0x00432ee0, "fn_00432ee0", fn_00432ee0_Entry, nullptr);

// 0x00432ef0 (eax = member index, ecx = type out, esi = fake grunt platoon, edi = matrix out (0x44 bytes)). Maps a
// platoon member index to its type and reference point: index < +0xb5c -> type 0 "ref_grunt_0x" (0x005ce648), then
// +0xb60 regulars (type 1 "ref_regular_0x"), +0xb58 captains (type 2 "ref_captain_0x"), the rest torches (type 3
// "ref_torch_0x"); the index within the group is written as a digit into the game's name string (0x00432ee0) and the
// reference matrix is read by 0x00453c50 (self, name, out, 1, 0, 0, 1, 0, 0). Callers 0x00433000, 0x00446460.
// Original bug (guess): for grunts the digit goes to index 10 of "ref_grunt_0x", the '0', not the 'x' (index 11) as for
// the other three names (index 13, 13, 11).
uint32_t fn_00432ef0(uint32_t index, uint32_t typeOut, uint32_t self, uint32_t matrix)
{
    const uint32_t esi = self;
    uint32_t eax = index;
    uint32_t edx = D(esi + 0xb5c);
    if (eax < edx)
    {
        D(typeOut) = 0;
        fn_00432ee0(0xa, 0x005ce648u, eax);
        return orig::fn_00453c50(esi, 0x005ce648u, matrix, 1, 0, 0, 1, 0, 0);
    }
    edx += D(esi + 0xb60);
    if (eax < edx)
    {
        D(typeOut) = 1;
        eax -= D(esi + 0xb5c);
        fn_00432ee0(0xd, 0x005ce668u, eax);
        return orig::fn_00453c50(esi, 0x005ce668u, matrix, 1, 0, 0, 1, 0, 0);
    }
    if (eax < D(esi + 0xb58) + edx)
    {
        D(typeOut) = 2;
        eax = eax - D(esi + 0xb60) - D(esi + 0xb5c);
        fn_00432ee0(0xd, 0x005ce688u, eax);
        return orig::fn_00453c50(esi, 0x005ce688u, matrix, 1, 0, 0, 1, 0, 0);
    }
    D(typeOut) = 3;
    eax = eax - D(esi + 0xb58) - D(esi + 0xb60) - D(esi + 0xb5c);
    fn_00432ee0(0xb, 0x005ce6a8u, eax);
    return orig::fn_00453c50(esi, 0x005ce6a8u, matrix, 1, 0, 0, 1, 0, 0);
}

static uint32_t fn_00432ef0_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t edi)
{
    return fn_00432ef0(eax, ecx, esi, edi);
}
ABI_ENTRY_00432ef0(fn_00432ef0_Entry, fn_00432ef0_Abi)
HOOK(0x00432ef0, "fn_00432ef0", fn_00432ef0_Entry, nullptr);

// 0x00433000 FakeGruntObject_Slot52 (ecx = fake grunt platoon; stack: damage message; ret 4). For each live member
// particle (+0xb6c[i], i < +0xb68) whose reference point (0x00432ef0) lies within +0xbf4 of the hit position
// (message +4..+0xc; squared 3D distance < radius squared): the particle is released (0x0046d9a0), a death effect is
// oriented from the hit (0x004e3070, 2D direction, message +0x20) and created (Fx_Create 0x0041cc60:
// "FX_platoon_grunt_die" / "_regular_" / "_captain_" / "_torch_" by type) and trigger 0x1b is sent (0x00461870).
// Nothing for a dead platoon (0x00454780). FakeGruntObject vtable slot 0xd0. Not executed in recorded runs.
uint32_t FakeGruntObject_Slot52(uint32_t self, uint32_t msg)
{
    alignas(16) uint8_t fr[0xec];  // sub esp, 0xec: L(o) = [esp+o] after the sub
#define L(o) (u(fr) + (o))
    std::memset(fr, 0, sizeof fr);
    D(L(0)) = self;
    if (IsDead(self))
        return 1;
    D(L(4)) = 0;
    if (!D(self + 0xb68))
        return 0;
    const uint32_t ebp = msg;
    uint32_t ebx = self + 0xb6c;
    for (uint32_t i = 0; i < D(self + 0xb68); ++i, ebx += 4)
    {
        D(L(4)) = i;
        D(L(8)) = ebx;
        if (!D(ebx))
            continue;
        fn_00432ef0(i, L(0xc), self, L(0x28));
        const float dz = Fl(ebp + 0xc) - Fl(L(0x54));
        D(L(0x10)) = D(L(0x34));
        const float dy = Fl(ebp + 8) - Fl(L(0x44));
        const float dx = Fl(ebp + 4) - Fl(L(0x34));
        D(L(0x14)) = D(L(0x44));
        D(L(0x18)) = D(L(0x54));
        const float r = Fl(self + 0xbf4);
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (!(r * r > d2))
            continue;
        const uint32_t sys = orig::fn_0041f4f0(D(GameP() + 0xa90), 2);
        orig::fn_0046d9a0(D(ebx), sys);
        D(ebx) = 0;
        Fl(L(0x1c)) = Fl(L(0x10)) - Fl(ebp + 4);
        D(L(0x24)) = D(ebp + 0x20);
        Fl(L(0x20)) = Fl(L(0x14)) - Fl(ebp + 8);
        orig::fn_004e3070(L(0x1c), L(0x9c), 0, L(0x10));
        D(L(0xe0)) = 0;
        D(L(0x6c)) = 2;
        const uint32_t type = D(L(0xc));
        if (type <= 3)
        {
            static const uint32_t names[4] = {0x005b65d4u, 0x005b65bcu, 0x005b65a4u, 0x005b658cu};
            const uint32_t eax = (type == 1) ? (L(0x6c) & 0xffffff00u) : 0;
            orig::fn_0041cc60(eax, D(GameP() + 0xa90), names[type], L(0x6c));
        }
        orig::fn_00461870(World(), 0x1b, self);
    }
    return 0;
#undef L
}

static uint32_t FakeGruntObject_Slot52_Abi(uint32_t ecx, uint32_t a0)
{
    return FakeGruntObject_Slot52(ecx, a0);
}
ABI_ENTRY_00433000(FakeGruntObject_Slot52_Entry, FakeGruntObject_Slot52_Abi)
HOOK(0x00433000, "FakeGruntObject_Slot52", FakeGruntObject_Slot52_Entry, nullptr);

// 0x004331f0 (eax = object, ecx, edx; stack: two values; ret 8; returns nothing): +0x558 = ecx, +0x55c = edx,
// +0xbec = a0, +0xbf0 = a1. Callers 0x004964d0, 0x00496c60.
void fn_004331f0(uint32_t obj, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1)
{
    D(obj + 0x558) = ecx;
    D(obj + 0x55c) = edx;
    D(obj + 0xbec) = a0;
    D(obj + 0xbf0) = a1;
}

static uint32_t fn_004331f0_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1)
{
    fn_004331f0(eax, ecx, edx, a0, a1);
    return eax;
}
ABI_ENTRY_004331f0(fn_004331f0_Entry, fn_004331f0_Abi)
HOOK(0x004331f0, "fn_004331f0", fn_004331f0_Entry, nullptr);

// 0x00433220 Squad_Init (ecx = squad; stack: 4 load arguments, a2 = properties, a3 = property source; ret 0x10).
// SquadObject vtable 0x005ab3e8 slot 0x18. Ai_InitGeneric 0x004247f0, Object_ForceGround (1000, 10000), properties
// "squad_tag" (+0xb58, +0x574 = -tag), "squad_sequence" (+0xb5c), "resume_delay_min/max" (+0xb60 / +0xb64, defaults
// 10 / 20), "charge_delay_min/max" (+0xb68 / +0xb6c, 5 / 10), "charge_count_min/max" (+0xb70 / +0xb74, 1); invalid
// ranges are logged (Debug_Printf, strings 0x005b6518 / 0x005b64c0 / 0x005b6464) and clamped; "goliath_cage" (bool).
// Unlinks from six World lists, 0x00458270 with 20000 / mass, +0x604 / +0x608 lists, virtual +0xf4 (0), a member list
// (PMS_heapAlloc 0x004e07f0 "new" + 0x004e7010) at +0xb78. Then spawns a "grunt" / "grunt_regular" / "captain" at each
// "ref_grunt_0%d" / "ref_regular_0%d" / "ref_captain_0%d" reference (0x00453b90; 0x0042de60 sets them up, Game+0x2710
// counts them) and a "goliath" at "ref_goliath_00" (with a "goliath_cage" holding a random "peasant_man" /
// "peasant_woman" when the bool is set); no references at all logs "Squad has no valid ref points" and deactivates
// (virtual +0xdc). Finally links into World+0xf08 and clears +0x528 / +0x530 / +0x390. The original reuses the
// argument stack slots as locals: a0's low byte = "friction not set" flag (state != 0x8c, Body_SetFriction), a1 = the
// reference counter, a2's low byte = "found any", a3 = the "goliath_cage" bool; reproduced. Not executed in recorded
// runs.
uint32_t Squad_Init(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    alignas(4) uint8_t fr[0x20];  // sub esp, 0x20: the name buffer
#define L(o) (u(fr) + (o))
    std::memset(fr, 0, sizeof fr);
    const uint32_t ebp = self;
    uint32_t a0slot = a0, a1slot = a1, a2slot = a2, a3slot = a3;
    D(ebp + 0xb7c) = 0;
    orig::fn_004247f0(ebp, a0, a1, a2, a3);
    orig::fn_00455860(ebp, 0x447a0000u /* 1000 */, 0x461c4000u /* 10000 */);
    orig::fn_00453450(0, 0x005b6580u /* "squad_tag" */, a3, ebp + 0xb58, ebp, a2, 0);
    D(ebp + 0x574) = 0u - D(ebp + 0xb58);
    orig::fn_00453450(0, 0x005b6570u /* "squad_sequence" */, a3, ebp + 0xb5c, ebp, a2, 0);
    orig::fn_004534a0(a3, ebp + 0xb60, ebp, 0x005b655cu, a2, 0x41200000u /* 10 */, 0);
    orig::fn_004534a0(a3, ebp + 0xb64, ebp, 0x005b6548u, a2, 0x41a00000u /* 20 */, 0);
    if (Fl(ebp + 0xb60) > Fl(ebp + 0xb64))
    {
        const uint32_t name = orig::fn_00453270(ebp);
        DebugPrintf(0x005b6518u, static_cast<double>(Fl(ebp + 0xb60)), static_cast<double>(Fl(ebp + 0xb64)), name);
        D(ebp + 0xb60) = D(ebp + 0xb64);
    }
    orig::fn_004534a0(a3, ebp + 0xb68, ebp, 0x005b6504u, a2, 0x40a00000u /* 5 */, 0);
    orig::fn_004534a0(a3, ebp + 0xb6c, ebp, 0x005b64f0u, a2, 0x41200000u /* 10 */, 0);
    if (Fl(ebp + 0xb68) > Fl(ebp + 0xb6c))
    {
        const uint32_t name = orig::fn_00453270(ebp);
        DebugPrintf(0x005b64c0u, static_cast<double>(Fl(ebp + 0xb68)), static_cast<double>(Fl(ebp + 0xb6c)), name);
        D(ebp + 0xb68) = D(ebp + 0xb6c);
    }
    orig::fn_00453400(1, a3, ebp + 0xb70, ebp, 0x005b64acu, a2, 0);
    orig::fn_00453400(1, a3, ebp + 0xb74, ebp, 0x005b6498u, a2, 0);
    uint32_t eax = D(ebp + 0xb70);
    if (eax > D(ebp + 0xb74))
    {
        const uint32_t hi = D(ebp + 0xb74);
        const uint32_t name = orig::fn_00453270(ebp);
        DebugPrintf(0x005b6464u, name, eax, hi);
        eax = D(ebp + 0xb74);
        D(ebp + 0xb70) = eax;
    }
    orig::fn_004534f0(eax & 0xffffff00u /* xor al, al */, a3, u(&a3slot), ebp, 0x005b6454u /* "goliath_cage" */, a2,
                      0);
    orig::fn_00454750(D(World() + 0xe90), ebp);
    orig::fn_00454750(D(World() + 0xe94), ebp);
    orig::fn_00454750(D(World() + 0xe9c), ebp);
    orig::fn_00454750(D(World() + 0xea4), ebp);
    orig::fn_00454750(D(World() + 0xe7c), ebp);
    orig::fn_00454750(D(World() + 0xe8c), ebp);
    {
        const float mass = Fl(D(D(ebp + 0x1c4) + 8) + 0x2c);
        float f;
        if (mass == K(0x0056c05c))
            f = K(0x0056c05c);
        else
            f = K(0x005bcb2c) / mass;
        f = f * K(0x005bcd34);
        orig::fn_00458270(ebp, 0, 0, fb(f));
    }
    D(ebp + 0x604) = D(World() + 0xe88);
    D(ebp + 0x608) = D(World() + 0xe7c);
    VC(ebp, 0xf4, 0u);
    D(ebp + 0x488) |= 0x10000;
    const uint32_t mem = orig::fn_004e07f0(0x0063a210u, 0x10, 0x18, 2, 0x0058b5e8u /* "new" */);
    D(ebp + 0xb78) = mem ? orig::fn_004e7010(D(GameP() + 0xac4), 0, mem) : 0;
    D(ebp + 0x5e0) = 0;
    a2slot &= 0xffffff00u;
    if (D(ebp + 0x460) == 0x8c)
        a0slot &= 0xffffff00u;
    else
    {
        orig::fn_00458250(ebp, 0);
        a0slot = (a0slot & 0xffffff00u) | 1;
    }
    auto spawnAll = [&](uint32_t format, uint32_t type) {
        a1slot = 0;
        Sprintf(L(0), format, 0u);
        while (static_cast<uint8_t>(orig::fn_00453b90(0, ebp, L(0))))
        {
            a2slot = (a2slot & 0xffffff00u) | 1;
            const uint32_t o = orig::fn_0045f020(0, type, World(), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
            if (o)
            {
                orig::fn_0042de60(D(ebp + 0x574), o, L(0), ebp, a0slot, 0, 0, 0);
                orig::fn_004546d0(D(ebp + 0xb78), o);
                ++I(GameP() + 0x2710);
            }
            ++a1slot;
            Sprintf(L(0), format, a1slot);
        }
    };
    spawnAll(0x005b6444u /* "ref_grunt_0%d" */, 0x005bc01cu /* "grunt" */);
    spawnAll(0x005b6434u /* "ref_regular_0%d" */, 0x005bbfe8u /* "grunt_regular" */);
    spawnAll(0x005b6424u /* "ref_captain_0%d" */, 0x005bbff8u /* "captain" */);
    Sprintf(L(0), 0x005b6414u /* "ref_goliath_0%d" */, 0u);
    if (static_cast<uint8_t>(orig::fn_00453b90(0, ebp, L(0))))
    {
        const uint32_t ebx = orig::fn_0045f020(0, 0x005b640cu /* "goliath" */, World(), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
        if (ebx)
        {
            D(ebp + 0xb7c) = ebx;
            ++I(ebx + 0x28);
            orig::fn_00454750(D(World() + 0xea4), D(ebp + 0xb7c));
            if (static_cast<uint8_t>(a3slot))
            {
                const uint32_t cage =
                    orig::fn_0045f020(0, 0x005b6454u /* "goliath_cage" */, World(), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
                VC(cage, 0xf8, 0x100u, 0u);
                orig::fn_004539f0(0x0059e210u /* "chest" */, ebx, cage, 0x005b6400u /* "ref_chest" */, 1, 0);
                const int32_t q = static_cast<int32_t>((Rand() * 100) & 0xffff8000u);
                const uint32_t kind = q < 0x190000 ? 0x005bbe78u /* "peasant_man" */ : 0x005bb1acu /* "peasant_woman" */;
                const uint32_t peasant = orig::fn_0045f020(0, kind, World(), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
                orig::fn_004539f0(0x005b63f0u /* "ref_peasant_1" */, cage, peasant, 0x005a009cu /* "center" */, 1,
                                  0x14);
                orig::fn_00408e30(0x81, peasant, 0xffffffffu, 0x005bbea4u /* "peasant_man_stand" */, 0, 0, 0, 0);
            }
            orig::fn_0042de60(D(ebp + 0x574), ebx, L(0), ebp, a0slot, D(ebp + 0x530), D(ebp + 0x528),
                              D(ebp + 0x390));
            ++I(GameP() + 0x2710);
        }
    }
    else if (!static_cast<uint8_t>(a2slot))
    {
        const uint32_t info = orig::fn_004555d0(ebp);
        DebugPrintf(0x005b63c0u, D(info + 0x10) + 4);
        VC(ebp, 0xdc);
    }
    orig::fn_004546d0(D(World() + 0xf08), ebp);
    D(ebp + 0x528) = 0;
    D(ebp + 0x530) = 0;
    D(ebp + 0x390) = 0;
    return 0;
#undef L
}

static uint32_t Squad_Init_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Squad_Init(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00433220(Squad_Init_Entry, Squad_Init_Abi)
HOOK(0x00433220, "Squad_Init", Squad_Init_Entry, nullptr);

// 0x00433a10 SquadObject_Slot14 (ecx = squad; stack: a0, a1; ret 8). When a1 is the squad's +0x52c, every member
// (the goliath +0xb7c and the list +0xb78) gets 0x0042de30 (esi = member); then the base method 0x0042f870 (ecx = self;
// a0, a1). SquadObject vtable 0x005ab3e8 slot 0x38. Not executed in recorded runs.
uint32_t SquadObject_Slot14(uint32_t self, uint32_t a0, uint32_t a1)
{
    const uint32_t ebx = self;
    if (D(ebx + 0x52c) == a1)
    {
        uint32_t iter[2] = {0, 0};
        if (D(ebx + 0xb7c))
            orig::fn_0042de30(D(ebx + 0xb7c));
        for (uint32_t m = orig::fn_004e7340(D(ebx + 0xb78), u(iter)); m; m = orig::fn_004e73c0(u(iter)))
            orig::fn_0042de30(m);
        if (iter[1])
            --I(iter[1] + 0x18);
    }
    return orig::fn_0042f870(ebx, a0, a1);
}

static uint32_t SquadObject_Slot14_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return SquadObject_Slot14(ecx, a0, a1);
}
ABI_ENTRY_00433a10(SquadObject_Slot14_Entry, SquadObject_Slot14_Abi)
HOOK(0x00433a10, "SquadObject_Slot14", SquadObject_Slot14_Entry, nullptr);

// 0x00433a90 SquadObject_Slot55 (ecx = squad). Destroy: 0x00432730, the goliath +0xb7c is stopped (virtual +0xdc) and
// released; every member of the list +0xb78 gets virtual +0xd4 (0), is unlinked from the list and loses its holder
// (0x0042cc10); the list is destroyed (virtual +0x10 (1)) and cleared. SquadObject vtable slot 0xdc. Not executed in
// recorded runs.
uint32_t SquadObject_Slot55(uint32_t self)
{
    const uint32_t ebx = self;
    uint32_t iter[2] = {0, 0};
    GenericAIObject_Slot55(ebx);
    ClearRefDc(ebx + 0xb7c);
    for (uint32_t m = orig::fn_004e7340(D(ebx + 0xb78), u(iter)); m; m = orig::fn_004e73c0(u(iter)))
    {
        VC(m, 0xd4, 0u);
        orig::fn_00454750(D(ebx + 0xb78), m);
        orig::fn_0042cc10(m);
    }
    if (D(ebx + 0xb78))
        VC(D(ebx + 0xb78), 0x10, 1u);
    D(ebx + 0xb78) = 0;
    if (iter[1])
        --I(iter[1] + 0x18);
    return iter[1];
}

static uint32_t SquadObject_Slot55_Abi(uint32_t ecx)
{
    return SquadObject_Slot55(ecx);
}
ABI_ENTRY_00433a90(SquadObject_Slot55_Entry, SquadObject_Slot55_Abi)
HOOK(0x00433a90, "SquadObject_Slot55", SquadObject_Slot55_Entry, nullptr);

// 0x00433b60 SquadObject_Slot7 (ecx = squad; stack: description out; ret 4): 0x00457810 (eax = self), the shared
// slot-7 method 0x00425df0 (ecx = self; a0), then *a0 = 0x20000. SquadObject vtable slot 0x1c. Not executed in recorded
// runs.
uint32_t SquadObject_Slot7(uint32_t self, uint32_t out)
{
    orig::fn_00457810(self);
    const uint32_t r = orig::fn_00425df0(self /* ecx kept (0x00457810 only ors +0x424) */, out);
    D(out) = 0x20000;
    return r;
}

static uint32_t SquadObject_Slot7_Abi(uint32_t ecx, uint32_t a0)
{
    return SquadObject_Slot7(ecx, a0);
}
ABI_ENTRY_00433b60(SquadObject_Slot7_Entry, SquadObject_Slot7_Abi)
HOOK(0x00433b60, "SquadObject_Slot7", SquadObject_Slot7_Entry, nullptr);

// 0x00433b80 (eax = squad): returns +0xb5c ("squad_sequence", see Squad_Init). Callers 0x00433bc0, 0x00446ba0. Not
// executed in recorded runs.
uint32_t fn_00433b80(uint32_t squad)
{
    return D(squad + 0xb5c);
}

static uint32_t fn_00433b80_Abi(uint32_t eax)
{
    return fn_00433b80(eax);
}
ABI_ENTRY_00433b80(fn_00433b80_Entry, fn_00433b80_Abi)
HOOK(0x00433b80, "fn_00433b80", fn_00433b80_Entry, nullptr);

// 0x00433b90 (eax = squad): returns +0xb58 ("squad_tag"). Callers 0x00433bc0, 0x00446ba0. Not executed in recorded
// runs.
uint32_t fn_00433b90(uint32_t squad)
{
    return D(squad + 0xb58);
}

static uint32_t fn_00433b90_Abi(uint32_t eax)
{
    return fn_00433b90(eax);
}
ABI_ENTRY_00433b90(fn_00433b90_Entry, fn_00433b90_Abi)
HOOK(0x00433b90, "fn_00433b90", fn_00433b90_Entry, nullptr);

// 0x00433ba0 (ecx = squad; stack: value; ret 4): +0x490 = a0 (the squad's timer). Callers 0x00433bc0, 0x00446ba0. Not
// executed in recorded runs.
uint32_t fn_00433ba0(uint32_t squad, uint32_t value)
{
    D(squad + 0x490) = value;
    return value;
}

static uint32_t fn_00433ba0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00433ba0(ecx, a0);
}
ABI_ENTRY_00433ba0(fn_00433ba0_Entry, fn_00433ba0_Abi)
HOOK(0x00433ba0, "fn_00433ba0", fn_00433ba0_Entry, nullptr);

// 0x00433bb0 (ecx = object; stack: one ignored argument; ret 4): calls the object's virtual +0xdc and returns its
// result. A virtual method of many classes (CameraPathObject, StigTurret, projectiles, pickups, SquadObject slot
// 0x230, ...); caller 0x004820b0.
uint32_t fn_00433bb0(uint32_t self)
{
    return VC(self, 0xdc);
}

static uint32_t fn_00433bb0_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return fn_00433bb0(ecx);
}
ABI_ENTRY_00433bb0(fn_00433bb0_Entry, fn_00433bb0_Abi)
HOOK(0x00433bb0, "fn_00433bb0", fn_00433bb0_Entry, nullptr);

// 0x00433bc0 (ebx = squad; stack: target; ret 4). Passes a target on to the other squads of World+0xf08 with the same
// "squad_tag" (0x00433b90) and a sequence (0x00433b80) not below this squad's (signed): virtual +0x23c (target, 0),
// 0x0042cc70 (ecx = squad; target, 1) and the timer +0x490 copied (0x00433ba0). Callers 0x00433c70, 0x00446ba0. Not
// executed in recorded runs.
uint32_t fn_00433bc0(uint32_t self, uint32_t target)
{
    const uint32_t ebx = self;
    uint32_t iter[2] = {0, 0};
    for (uint32_t s = orig::fn_004e7340(D(World() + 0xf08), u(iter)); s; s = orig::fn_004e73c0(u(iter)))
    {
        if (s == ebx)
            continue;
        if (fn_00433b90(s) != fn_00433b90(ebx))
            continue;
        if (static_cast<int32_t>(fn_00433b80(s)) < static_cast<int32_t>(fn_00433b80(ebx)))
            continue;
        VC(s, 0x23c, target, 0u);
        orig::fn_0042cc70(s, 0 /* edx: left by the virtual call */, target, 1);
        fn_00433ba0(s, D(ebx + 0x490));
    }
    if (iter[1])
        --I(iter[1] + 0x18);
    return iter[1];
}

static uint32_t fn_00433bc0_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_00433bc0(ebx, a0);
}
ABI_ENTRY_00433bc0(fn_00433bc0_Entry, fn_00433bc0_Abi)
HOOK(0x00433bc0, "fn_00433bc0", fn_00433bc0_Entry, nullptr);

// 0x00433c70 SquadObject_Slot143 (ecx = squad; stack: target, pass-on flag (byte); ret 8). The squad is alerted: a
// random resume delay min + rand * (max - min) / 32768 (+0xb60 / +0xb64) replaces the timer +0x490 when larger; then
// (with the flag) the other squads hear of it (0x00433bc0), the squad enters state 0x98 (0x0042cb80, 0x00454c90,
// 0x00454cc0, +0x494 = 0, +0x4ac = old state, virtual +0x1b8) and, with the flag and the charge timer +0x494 run out,
// a random number of members between +0xb70 and +0xb74 charge (0x0042cc10, 0x0042cff0, 0x0042ce70 (eax = target,
// ecx = member), unlinked from +0xb78; dead members are just unlinked; the iteration is abandoned through the
// iterator's virtual +0xc when enough have gone) and the charge timer restarts (+0xb68..+0xb6c). Ends with the base
// method 0x0042cc70 (ecx = self; target, flag). SquadObject vtable slot 0x23c. Not executed in recorded runs.
uint32_t SquadObject_Slot143(uint32_t self, uint32_t target, uint32_t flag)
{
    alignas(4) uint8_t fr[0x10];  // sub esp, 0x10
#define L(o) (u(fr) + (o))
    const uint32_t ebx = self;
    D(L(8)) = D(ebx + 0xb64);
    D(L(0)) = D(ebx + 0xb60);
    D(L(4)) = Rand();
    const float t = static_cast<float>(I(L(4))) * (Fl(L(8)) - Fl(L(0))) * K(0x005bcbc0) + Fl(L(0));
    if (t > Fl(ebx + 0x490))
    {
        Fl(ebx + 0x490) = t;
        if (static_cast<uint8_t>(flag))
            fn_00433bc0(ebx, target);
        if (D(ebx + 0x460) != 0x98)
        {
            orig::fn_0042cb80(ebx);
            orig::fn_00454c90(ebx, 1 /* cl = 1 */, 1);
            orig::fn_00454cc0(ebx, ebx + 0x398);
            D(ebx + 0x494) = 0;
            D(ebx + 0x4ac) = D(ebx + 0x460);
            VC(ebx, 0x1b8, 0x98u);
        }
        if (static_cast<uint8_t>(flag) && !(Fl(ebx + 0x494) > K(0x0056c05c)))
        {
            const uint32_t maxN = D(ebx + 0xb74);
            const uint32_t minN = D(ebx + 0xb70);
            D(L(8)) = 0;
            D(L(0xc)) = 0;
            D(L(0)) = 0;  // members sent
            const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(Rand() * (maxN - minN + 1)) >> 15) + minN;
            uint32_t* iter = reinterpret_cast<uint32_t*>(L(8));
            for (uint32_t m = orig::fn_004e7340(D(ebx + 0xb78), u(iter)); m; m = orig::fn_004e73c0(u(iter)))
            {
                if (IsDead(m))
                {
                    orig::fn_00454750(D(ebx + 0xb78), m);
                    continue;
                }
                if (D(L(0)) >= count)
                {
                    VC(iter[0], 0xc, iter[1]);
                    --I(iter[1] + 0x18);
                    iter[1] = 0;
                    break;
                }
                orig::fn_0042cc10(m);
                orig::fn_0042cff0(m);
                orig::fn_0042ce70(target, m);
                orig::fn_00454750(D(ebx + 0xb78), m);
                ++D(L(0));
            }
            const uint32_t node = iter[1];
            D(L(4)) = D(ebx + 0xb6c);
            D(L(0)) = D(ebx + 0xb68);
            D(L(8)) = Rand();
            Fl(ebx + 0x494) = static_cast<float>(I(L(8))) * (Fl(L(4)) - Fl(L(0))) * K(0x005bcbc0) + Fl(L(0));
            if (node)
                --I(node + 0x18);
        }
    }
    return orig::fn_0042cc70(ebx, 0 /* edx: not set by the original */, target, flag);
#undef L
}

static uint32_t SquadObject_Slot143_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return SquadObject_Slot143(ecx, a0, a1);
}
ABI_ENTRY_00433c70(SquadObject_Slot143_Entry, SquadObject_Slot143_Abi)
HOOK(0x00433c70, "SquadObject_Slot143", SquadObject_Slot143_Entry, nullptr);

// 0x00433e80 Polewart_Slot6 (ecx = polewart; stack: 4 load arguments; ret 0x10). Init: Ai_InitGeneric 0x004247f0,
// +0x604 / +0x608 = World+0xe88 / World+0xe7c, +0xb58 = 0, the timer +0x490 = rand * (5 / 32768) + 5, unlinked from
// World+0xe8c. Polewart vtable 0x005a7328 slot 0x18. Not executed in recorded runs.
uint32_t Polewart_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t edi = self;
    orig::fn_004247f0(edi, a0, a1, a2, a3);
    D(edi + 0x604) = D(World() + 0xe88);
    D(edi + 0x608) = D(World() + 0xe7c);
    const int32_t r = static_cast<int32_t>(Rand());
    D(edi + 0xb58) = 0;
    Fl(edi + 0x490) = static_cast<float>(r) * K(0x005bcdd8) + K(0x005bcbdc);
    return orig::fn_00454750(D(World() + 0xe8c), edi);
}

static uint32_t Polewart_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Polewart_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00433e80(Polewart_Slot6_Entry, Polewart_Slot6_Abi)
HOOK(0x00433e80, "Polewart_Slot6", Polewart_Slot6_Entry, nullptr);

// 0x00433f30 Polewart_Slot42 (ecx = polewart; stack: 5 arguments as GenericAIObject_Slot42; ret 0x14). Dead: 0. With
// a0 and 0x00426600 saying no: a0 joins the list +0x518 (virtual +0x1c finds, +0x14 adds, reference taken) and 0 is
// returned; otherwise the base test 0x004548f0 decides. Polewart vtable slot 0xa8. Not executed in recorded runs.
uint32_t Polewart_Slot42(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    const uint32_t esi = self;
    if (IsDead(esi))
        return 0;
    const uint32_t edi = a0;
    if (edi && !static_cast<uint8_t>(orig::fn_00426600(esi)))
    {
        if (!VC(D(esi + 0x518), 0x1c, edi))
        {
            VC(D(esi + 0x518), 0x14, edi);
            ++I(edi + 0x28);
        }
        return 0;
    }
    return orig::fn_004548f0(esi, edi, a1, a2, a3, a4);
}

static uint32_t Polewart_Slot42_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Polewart_Slot42(ecx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_00433f30(Polewart_Slot42_Entry, Polewart_Slot42_Abi)
HOOK(0x00433f30, "Polewart_Slot42", Polewart_Slot42_Entry, nullptr);

// 0x00433fb0 Polewart_Slot32 (ecx = polewart; stack: three float outs; ret 0xc): *a2 = 1.0, *a1 = *a0 = +0x948.
// Polewart vtable slot 0x80. Not executed in recorded runs.
uint32_t Polewart_Slot32(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2)
{
    D(a2) = 0x3f800000u;
    const uint32_t v = D(self + 0x948);
    D(a1) = v;
    D(a0) = v;
    return a2;
}

static uint32_t Polewart_Slot32_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Polewart_Slot32(ecx, a0, a1, a2);
}
ABI_ENTRY_00433fb0(Polewart_Slot32_Entry, Polewart_Slot32_Abi)
HOOK(0x00433fb0, "Polewart_Slot32", Polewart_Slot32_Entry, nullptr);

// 0x00433fe0 GoliathObject_Slot6 (ecx = goliath; stack: 4 load arguments; ret 0x10). Init: +0xb68 = 0,
// Ai_InitGeneric, +0x604 / +0x608 lists, unlinked from World+0xe8c; when 0x00426600 says so (guess: caged) it is also
// unlinked from World+0xe90 / +0xe94, physics on (0x004577b0 (1)) and virtual +0xf4 (0); otherwise it plays
// "goliath_stand" (0x00408e30, cue 0x81) and +0x494 = rand / 16384 + 1. With 0x00426610 unlinked from World+0xe7c.
// +0xb58 = 4 and 0x00453150 (ecx = self, esi = +0xb5c). GoliathObject vtable 0x005af298 slot 0x18. Not executed in
// recorded runs.
uint32_t GoliathObject_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t edi = self;
    D(edi + 0xb68) = 0;
    orig::fn_004247f0(edi, a0, a1, a2, a3);
    D(edi + 0x604) = D(World() + 0xe88);
    D(edi + 0x608) = D(World() + 0xe7c);
    orig::fn_00454750(D(World() + 0xe8c), edi);
    if (static_cast<uint8_t>(orig::fn_00426600(edi)))
    {
        orig::fn_00454750(D(World() + 0xe90), edi);
        orig::fn_00454750(D(World() + 0xe94), edi);
        orig::fn_004577b0(edi, 1);
        VC(edi, 0xf4, 0u);
    }
    else
    {
        orig::fn_00408e30(0x81, edi, 0xffffffffu, 0x005b63b0u /* "goliath_stand" */, 0, 0, 0, 0);
        const int32_t r = static_cast<int32_t>(Rand());
        Fl(edi + 0x494) = static_cast<float>(r) * K(0x005bce30) + K(0x005bcb2c);
    }
    if (static_cast<uint8_t>(orig::fn_00426610(edi)))
        orig::fn_00454750(D(World() + 0xe7c), edi);
    D(edi + 0xb58) = 4;
    return orig::fn_00453150(edi, edi + 0xb5c);
}

static uint32_t GoliathObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return GoliathObject_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00433fe0(GoliathObject_Slot6_Entry, GoliathObject_Slot6_Abi)
HOOK(0x00433fe0, "GoliathObject_Slot6", GoliathObject_Slot6_Entry, nullptr);

// 0x00434110 GoliathObject_Slot55 (ecx = goliath). Destroy: the object +0xb68 is stopped (virtual +0xdc) and loses a
// reference (the slot is not cleared), then tail call GenericAIObject_Slot55 0x00432730. GoliathObject vtable slot
// 0xdc. Not executed in recorded runs.
uint32_t GoliathObject_Slot55(uint32_t self)
{
    const uint32_t edi = self;
    if (D(edi + 0xb68))
    {
        VC(D(edi + 0xb68), 0xdc);
        Unref(D(edi + 0xb68));
    }
    return GenericAIObject_Slot55(edi);
}

static uint32_t GoliathObject_Slot55_Abi(uint32_t ecx)
{
    return GoliathObject_Slot55(ecx);
}
ABI_ENTRY_00434110(GoliathObject_Slot55_Entry, GoliathObject_Slot55_Abi)
HOOK(0x00434110, "GoliathObject_Slot55", GoliathObject_Slot55_Entry, nullptr);

// 0x00434150 GoliathObject_Slot7 (ecx = goliath; stack: description out; ret 4): 0x00457810, the shared slot-7 method
// 0x00425df0 (ecx = self; a0), and *a0 = 0x20000 when 0x00426600 says so. GoliathObject vtable slot 0x1c.
uint32_t GoliathObject_Slot7(uint32_t self, uint32_t out)
{
    const uint32_t esi = self;
    orig::fn_00457810(esi);
    orig::fn_00425df0(esi /* ecx kept (0x00457810 only ors +0x424) */, out);
    const uint32_t r = orig::fn_00426600(esi);
    if (static_cast<uint8_t>(r))
        D(out) = 0x20000;
    return r;
}

static uint32_t GoliathObject_Slot7_Abi(uint32_t ecx, uint32_t a0)
{
    return GoliathObject_Slot7(ecx, a0);
}
ABI_ENTRY_00434150(GoliathObject_Slot7_Entry, GoliathObject_Slot7_Abi)
HOOK(0x00434150, "GoliathObject_Slot7", GoliathObject_Slot7_Entry, nullptr);

// 0x00434290 GoliathObject_Slot96 (ecx = goliath; stack: a0, a1; ret 8): 0 (AL) when the goliath belongs to a squad
// (+0x9bc), else tail jump to the base method 0x0043ecf0 with the same frame (reproduced as call-then-return).
// GoliathObject vtable slot 0x180. Not executed in recorded runs.
uint32_t GoliathObject_Slot96(uint32_t self, uint32_t a0, uint32_t a1)
{
    const uint32_t squad = D(self + 0x9bc);
    if (squad)
        return squad & 0xffffff00u;
    return orig::fn_0043ecf0(self, a0, a1);
}

static uint32_t GoliathObject_Slot96_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return GoliathObject_Slot96(ecx, a0, a1);
}
ABI_ENTRY_00434290(GoliathObject_Slot96_Entry, GoliathObject_Slot96_Abi)
HOOK(0x00434290, "GoliathObject_Slot96", GoliathObject_Slot96_Entry, nullptr);

// 0x004342b0 GoliathObject_Slot109 (ecx = goliath; stack: a0, a1, a2; ret 0xc): nothing (returns +0x9bc) when the
// goliath belongs to a squad, else a2 = 0 and tail jump to the base method 0x0042c820 (a0, a1, 0). GoliathObject
// vtable slot 0x1b4. Not executed in recorded runs.
uint32_t GoliathObject_Slot109(uint32_t self, uint32_t a0, uint32_t a1)
{
    const uint32_t squad = D(self + 0x9bc);
    if (squad)
        return squad;
    return orig::fn_0042c820(self, a0, a1, 0);
}

static uint32_t GoliathObject_Slot109_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t /*a2*/)
{
    return GoliathObject_Slot109(ecx, a0, a1);
}
ABI_ENTRY_004342b0(GoliathObject_Slot109_Entry, GoliathObject_Slot109_Abi)
HOOK(0x004342b0, "GoliathObject_Slot109", GoliathObject_Slot109_Entry, nullptr);

// 0x004342d0 GoliathObject_Slot14 (ecx = goliath; stack: a0, a1; ret 8). When a1 is +0x52c (guess: the goliath's
// release trigger) it links into World+0xe90 / +0xe94 and calls virtual +0xf4 (1); then the base method 0x0042f870
// (a0, a1). GoliathObject vtable slot 0x38.
uint32_t GoliathObject_Slot14(uint32_t self, uint32_t a0, uint32_t a1)
{
    const uint32_t edi = self;
    if (a1 == D(edi + 0x52c))
    {
        orig::fn_004546d0(D(World() + 0xe90), edi);
        orig::fn_004546d0(D(World() + 0xe94), edi);
        VC(edi, 0xf4, 1u);
    }
    return orig::fn_0042f870(edi, a0, a1);
}

static uint32_t GoliathObject_Slot14_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return GoliathObject_Slot14(ecx, a0, a1);
}
ABI_ENTRY_004342d0(GoliathObject_Slot14_Entry, GoliathObject_Slot14_Abi)
HOOK(0x004342d0, "GoliathObject_Slot14", GoliathObject_Slot14_Entry, nullptr);

// 0x004344b0 GoliathFireball_Slot55 (ecx = fireball). Destroy: releases +0xb78 (cleared), then tail call
// GenericAIObject_Slot55 0x00432730. GoliathFireball vtable 0x005af8c0 slot 0xdc. Not executed in recorded runs.
uint32_t GoliathFireball_Slot55(uint32_t self)
{
    ClearRef(self + 0xb78);
    return GenericAIObject_Slot55(self);
}

static uint32_t GoliathFireball_Slot55_Abi(uint32_t ecx)
{
    return GoliathFireball_Slot55(ecx);
}
ABI_ENTRY_004344b0(GoliathFireball_Slot55_Entry, GoliathFireball_Slot55_Abi)
HOOK(0x004344b0, "GoliathFireball_Slot55", GoliathFireball_Slot55_Entry, nullptr);

// 0x00434560 (eax = fireball): returns +0xb74 in st0. Caller 0x0043cef0. Not executed in recorded runs.
float fn_00434560(uint32_t obj)
{
    return Fl(obj + 0xb74);
}

static float fn_00434560_Abi(uint32_t eax)
{
    return fn_00434560(eax);
}
ABI_ENTRY_00434560(fn_00434560_Entry, fn_00434560_Abi)
HOOK(0x00434560, "fn_00434560", fn_00434560_Entry, nullptr);

// 0x00434570 (eax = object, ecx = fireball; returns nothing): +0xb78 = object, reference taken (the slot
// GoliathFireball_Slot55 releases). Caller 0x0043cef0. Not executed in recorded runs.
void fn_00434570(uint32_t object, uint32_t owner)
{
    D(owner + 0xb78) = object;
    ++I(object + 0x28);
}

static uint32_t fn_00434570_Abi(uint32_t eax, uint32_t ecx)
{
    fn_00434570(eax, ecx);
    return eax;
}
ABI_ENTRY_00434570(fn_00434570_Entry, fn_00434570_Abi)
HOOK(0x00434570, "fn_00434570", fn_00434570_Entry, nullptr);

// 0x00434580 GruntLadderObject_Slot6 (ecx = ladder grunt; stack: 4 load arguments; ret 0x10). Init: Ai_InitGeneric,
// +0x604 / +0x608 lists, unlinked from World+0xe8c, +0xe7c, +0xe9c, +0xe90, +0xe94, +0xea4, 0x004559c0 (eax = self,
// cl = 0) and virtual +0xf8 (0x4000, 0). GruntLadderObject vtable 0x005a3528 slot 0x18. Not executed in recorded runs.
uint32_t GruntLadderObject_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t edi = self;
    orig::fn_004247f0(edi, a0, a1, a2, a3);
    D(edi + 0x604) = D(World() + 0xe88);
    D(edi + 0x608) = D(World() + 0xe7c);
    orig::fn_00454750(D(World() + 0xe8c), edi);
    orig::fn_00454750(D(World() + 0xe7c), edi);
    orig::fn_00454750(D(World() + 0xe9c), edi);
    orig::fn_00454750(D(World() + 0xe90), edi);
    orig::fn_00454750(D(World() + 0xe94), edi);
    orig::fn_00454750(D(World() + 0xea4), edi);
    orig::fn_004559c0(edi, World() & 0xffffff00u /* ecx = World, xor cl, cl */);
    return VC(edi, 0xf8, 0x4000u, 0u);
}

static uint32_t GruntLadderObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return GruntLadderObject_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00434580(GruntLadderObject_Slot6_Entry, GruntLadderObject_Slot6_Abi)
HOOK(0x00434580, "GruntLadderObject_Slot6", GruntLadderObject_Slot6_Entry, nullptr);

// 0x00434670 GruntLadderObject_Slot7 (ecx = ladder grunt; stack: description out; ret 4): 0x00457810 and *a0 =
// 0x4000. GruntLadderObject vtable slot 0x1c. Not executed in recorded runs.
uint32_t GruntLadderObject_Slot7(uint32_t self, uint32_t out)
{
    orig::fn_00457810(self);
    D(out) = 0x4000;
    return out;
}

static uint32_t GruntLadderObject_Slot7_Abi(uint32_t ecx, uint32_t a0)
{
    return GruntLadderObject_Slot7(ecx, a0);
}
ABI_ENTRY_00434670(GruntLadderObject_Slot7_Entry, GruntLadderObject_Slot7_Abi)
HOOK(0x00434670, "GruntLadderObject_Slot7", GruntLadderObject_Slot7_Entry, nullptr);

// 0x004346a0 BowGruntObject_Slot66 (ecx = bow grunt; stack: object; ret 4). Tries to grab / be carried by a0
// (guess): refused (0) when dead or when +0x484 bit 31, +0x488 & 0x200080, +0x484 & 0x2000 / 0x28000 / 0x20, +0x48c & 2,
// +0xab0 or +0xb54 is set, or 0x00454be0 says no. Otherwise +0x484 |= 0x2000, the two handles +0x39c / +0x398 are
// refreshed (0x00455a00), the velocity is zeroed (Body_setLinearVelocity 0x00458510), 0x00454c90 (cl = 1; 1),
// 0x00454cc0, physics on, a0 is stored at +0xab0 (reference taken), 0x00454cc0 again, virtual +0x1a0 (2, 3, 0, 1, 1, 0,
// 0.5, 0.5), state 0xf (virtual +0x1b8) and 1 is returned. BowGruntObject vtable 0x005abb88 slot 0x108. Not executed
// in recorded runs.
uint32_t BowGruntObject_Slot66(uint32_t self, uint32_t object)
{
    alignas(4) uint8_t fr[0xc];  // sub esp, 0xc: the zero velocity
#define L(o) (u(fr) + (o))
    const uint32_t esi = self;
    if (IsDead(esi))
        return 0;
    const uint32_t f = D(esi + 0x484);
    if ((f & 0x80000000u) || (D(esi + 0x488) & 0x200080) || (f & 0x2000) || (f & 0x28000) || (By(esi + 0x48c) & 2) ||
        D(esi + 0xab0) || (f & 0x20) || D(esi + 0xb54) || !static_cast<uint8_t>(orig::fn_00454be0(esi)))
        return 0;
    D(esi + 0x484) |= 0x2000;
    D(esi + 0x39c) = orig::fn_00455a00(D(esi + 0x39c));
    const uint32_t edi = esi + 0x398;
    D(edi) = orig::fn_00455a00(D(esi + 0x398));
    D(L(0)) = 0;
    D(L(4)) = 0;
    D(L(8)) = 0;
    orig::fn_00458510(L(0), esi);
    orig::fn_00454c90(esi, 1 /* cl = 1 */, 1);
    orig::fn_00454cc0(esi, edi);
    orig::fn_004577b0(esi, 1);
    D(esi + 0xab0) = object;
    ++I(object + 0x28);
    orig::fn_00454cc0(esi, edi);
    VC(esi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(esi, 0x1b8, 0xfu);
    return 1;
#undef L
}

static uint32_t BowGruntObject_Slot66_Abi(uint32_t ecx, uint32_t a0)
{
    return BowGruntObject_Slot66(ecx, a0);
}
ABI_ENTRY_004346a0(BowGruntObject_Slot66_Entry, BowGruntObject_Slot66_Abi)
HOOK(0x004346a0, "BowGruntObject_Slot66", BowGruntObject_Slot66_Entry, nullptr);

// 0x004347b0 BowGruntObject_Slot6 (ecx = bow grunt; stack: 4 load arguments; ret 0x10). Init: Ai_InitGeneric,
// +0x604 / +0x608 lists; when virtual +0xd8 says so the weapon attachment +0x61c is set up at "ref_lhand" (0x004a0ea0,
// 0x004a0ef0 (0xe), 0x004a10e0 (0)). Then a random behaviour: rand * 300 >> 15 < 100 sets 0x200000 of +0x484, < 200
// sets 0x20000000 of +0x488 (both cleared first). BowGruntObject vtable slot 0x18. Not executed in recorded runs.
uint32_t BowGruntObject_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    orig::fn_004247f0(ebp, a0, a1, a2, a3);
    D(ebp + 0x604) = D(World() + 0xe88);
    D(ebp + 0x608) = D(World() + 0xe7c);
    if (VB(ebp, 0xd8))
    {
        const uint32_t att = ebp + 0x61c;
        orig::fn_004a0ea0(0x005bb99cu /* "ref_lhand" */, ebp, att, 0, 0);
        orig::fn_004a0ef0(0xe, att);
        orig::fn_004a10e0(att, 0);
    }
    const uint32_t q = (Rand() * 0x12c) >> 15;
    D(ebp + 0x484) &= 0xffdfffffu;
    D(ebp + 0x488) &= 0xdfffffffu;
    if (q < 100)
        D(ebp + 0x484) |= 0x200000;
    else if (q < 200)
        D(ebp + 0x488) |= 0x20000000;
    return 0;
}

static uint32_t BowGruntObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return BowGruntObject_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004347b0(BowGruntObject_Slot6_Entry, BowGruntObject_Slot6_Abi)
HOOK(0x004347b0, "BowGruntObject_Slot6", BowGruntObject_Slot6_Entry, nullptr);

// 0x004348a0 SniperCaptain_Slot6 (ecx = sniper captain; stack: 4 load arguments; ret 0x10). Init: Ai_InitGeneric,
// +0x604 / +0x608 lists; when virtual +0xd8 says so the weapon attachment +0x61c is set up at "ref_chest" (0x004a0ea0,
// 0x004a0ef0 (0x12), 0x004a10e0 (0)). SniperCaptain vtable 0x005ae868 slot 0x18. Not executed in recorded runs.
uint32_t SniperCaptain_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    orig::fn_004247f0(ebp, a0, a1, a2, a3);
    D(ebp + 0x604) = D(World() + 0xe88);
    D(ebp + 0x608) = D(World() + 0xe7c);
    if (VB(ebp, 0xd8))
    {
        const uint32_t att = ebp + 0x61c;
        orig::fn_004a0ea0(0x005b6400u /* "ref_chest" */, ebp, att, 0, 0);
        orig::fn_004a0ef0(0x12, att);
        return orig::fn_004a10e0(att, 0);
    }
    return 0;
}

static uint32_t SniperCaptain_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return SniperCaptain_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004348a0(SniperCaptain_Slot6_Entry, SniperCaptain_Slot6_Abi)
HOOK(0x004348a0, "SniperCaptain_Slot6", SniperCaptain_Slot6_Entry, nullptr);

// 0x00434a80 (esi = object; stack: min, max (floats); ret 8; the ecx input is only a `push ecx` slot): +0x5d0 = min,
// +0x5d4 = max, +0x5d8 = min + rand * (max - min) / 32768. No callers or vtable references found. Not executed in
// recorded runs.
uint32_t fn_00434a80(uint32_t object, uint32_t lo, uint32_t hi)
{
    D(object + 0x5d0) = lo;
    D(object + 0x5d4) = hi;
    const int32_t r = static_cast<int32_t>(Rand());
    Fl(object + 0x5d8) = static_cast<float>(r) * (bf(hi) - bf(lo)) * K(0x005bcbc0) + bf(lo);
    return static_cast<uint32_t>(r);
}

static uint32_t fn_00434a80_Abi(uint32_t /*ecx*/, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return fn_00434a80(esi, a0, a1);
}
ABI_ENTRY_00434a80(fn_00434a80_Entry, fn_00434a80_Abi)
HOOK(0x00434a80, "fn_00434a80", fn_00434a80_Entry, nullptr);

// 0x00434ac0 RocketCaptain_ReadProperties (stack: captain; ret 4). Reads the rocket captain's firing properties with
// no property source (esi = 0): "rkt_fire_num_clips_min/max" +0x58c / +0x590, "rkt_fire_clip_delay_min/max" +0x594 /
// +0x598, "rkt_fire_num_rounds_min/max" +0x59c / +0x5a0, "rkt_fire_num_bursts_min/max" +0x5a4 / +0x5a8,
// "rkt_fire_burst_delay_min/max" +0x5ac / +0x5b0, "rkt_fire_range_min/max" +0x5d0 / +0x5d4 (200 / 350),
// "rkt_fire_inaccuracy_height/angle" +0x5e4 / +0x5e8 (10 / 15), "rkt_fire_accuracy_min/max" +0x5ec / +0x5f0 (50 / 150),
// "rkt_fire_infinite_accuracy_min/max" +0x5f4 / +0x5f8 (400 / 8000). When virtual +0xd8 says so the attachment +0x61c
// is reset (0x004a0fb0), 0x00428250 (cl = 0), 0x004a0ef0 (0x11), 0x004a10e0 (0); +0x484: bit 4 cleared, 0x8000000 set;
// 0x00427600 (esi = captain). Name from the "rkt_fire_*" strings and the "rocket_captain" gate in 0x00434cd0. Callers
// 0x0040c590, 0x00434cd0. Not executed in recorded runs.
uint32_t RocketCaptain_ReadProperties(uint32_t captain)
{
    const uint32_t ebp = captain;
    orig::fn_00453400(1, 0, ebp + 0x58c, ebp, 0x005b631cu, 0, 0);
    orig::fn_00453400(1, 0, ebp + 0x590, ebp, 0x005b6304u, 0, 0);
    orig::fn_004534a0(0, ebp + 0x594, ebp, 0x005b62ecu, 0, 0, 0);
    orig::fn_004534a0(0, ebp + 0x598, ebp, 0x005b62d4u, 0, 0, 0);
    orig::fn_00453400(1, 0, ebp + 0x59c, ebp, 0x005b62bcu, 0, 0);
    orig::fn_00453400(1, 0, ebp + 0x5a0, ebp, 0x005b62a4u, 0, 0);
    orig::fn_00453400(1, 0, ebp + 0x5a4, ebp, 0x005b628cu, 0, 0);
    orig::fn_00453400(1, 0, ebp + 0x5a8, ebp, 0x005b6274u, 0, 0);
    orig::fn_004534a0(0, ebp + 0x5ac, ebp, 0x005b6258u, 0, 0, 0);
    orig::fn_004534a0(0, ebp + 0x5b0, ebp, 0x005b623cu, 0, 0, 0);
    orig::fn_004534a0(0, ebp + 0x5d0, ebp, 0x005b6228u, 0, 0x43480000u, 0);
    orig::fn_004534a0(0, ebp + 0x5d4, ebp, 0x005b6214u, 0, 0x43af0000u, 0);
    orig::fn_004534a0(0, ebp + 0x5e4, ebp, 0x005b61f8u, 0, 0x41200000u, 0);
    orig::fn_004534a0(0, ebp + 0x5e8, ebp, 0x005b61dcu, 0, 0x41700000u, 0);
    orig::fn_004534a0(0, ebp + 0x5ec, ebp, 0x005b61c4u, 0, 0x42480000u, 0);
    orig::fn_004534a0(0, ebp + 0x5f0, ebp, 0x005b61acu, 0, 0x43160000u, 0);
    orig::fn_004534a0(0, ebp + 0x5f4, ebp, 0x005b618cu, 0, 0x43c80000u, 0);
    orig::fn_004534a0(0, ebp + 0x5f8, ebp, 0x005b616cu, 0, 0x45fa0000u, 0);
    if (!VB(ebp, 0xd8))
        return 0;
    const uint32_t att = ebp + 0x61c;
    orig::fn_004a0fb0(0 /* edx: left by the virtual call */, att);
    orig::fn_00428250(ebp, 0 /* cl = 0 */);
    orig::fn_004a0ef0(0x11, att);
    orig::fn_004a10e0(att, 0);
    const uint32_t c = D(ebp + 0x484) & 0xfffffffbu;
    D(ebp + 0x484) = c;
    D(ebp + 0x484) = c | 0x8000000;
    return orig::fn_00427600(ebp);
}

static uint32_t RocketCaptain_ReadProperties_Abi(uint32_t a0)
{
    return RocketCaptain_ReadProperties(a0);
}
ABI_ENTRY_00434ac0(RocketCaptain_ReadProperties_Entry, RocketCaptain_ReadProperties_Abi)
HOOK(0x00434ac0, "RocketCaptain_ReadProperties", RocketCaptain_ReadProperties_Entry, nullptr);

// 0x00434cd0 CaptainObject_Slot6 (ecx = captain; stack: 4 load arguments, a2 = properties, a3 = source; ret 0x10).
// Init: Ai_InitGeneric, +0x604 / +0x608 lists; when virtual +0xd8 says so the weapon attachment +0x61c is set up at
// "ref_rhand" (0x004a0ea0) and the "rocket_captain" bool is read (Object_ReadBoolProp, into a2's stack slot): a rocket
// captain gets RocketCaptain_ReadProperties, the others 0x004a0ef0 (0x10) and 0x00428250 (cl = 1); both end with
// 0x004a10e0 (0). CaptainObject vtable 0x005acb78 slot 0x18. Not executed in recorded runs.
uint32_t CaptainObject_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    uint32_t a2slot = a2;
    orig::fn_004247f0(ebp, a0, a1, a2, a3);
    D(ebp + 0x604) = D(World() + 0xe88);
    D(ebp + 0x608) = D(World() + 0xe7c);
    if (!VB(ebp, 0xd8))
        return 0;
    const uint32_t ebx = ebp + 0x61c;
    orig::fn_004a0ea0(0x005b6160u /* "ref_rhand" */, ebp, ebx, 0, 0);
    orig::fn_004534f0(a2 & 0xffffff00u /* xor al, al */, a3, u(&a2slot), ebp, 0x005b6150u /* "rocket_captain" */, a2,
                      0);
    if (static_cast<uint8_t>(a2slot))
    {
        RocketCaptain_ReadProperties(ebp);
        return orig::fn_004a10e0(ebx, 0);
    }
    orig::fn_004a0ef0(0x10, ebx);
    orig::fn_00428250(ebp, 1 /* cl = 1 */);
    return orig::fn_004a10e0(ebx, 0);
}

static uint32_t CaptainObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return CaptainObject_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00434cd0(CaptainObject_Slot6_Entry, CaptainObject_Slot6_Abi)
HOOK(0x00434cd0, "CaptainObject_Slot6", CaptainObject_Slot6_Entry, nullptr);

// 0x00434da0 CaptainObject_Slot8 (ecx = captain; also SniperCaptain slot 8). AI_ResolveStartObjects 0x00425e20, then
// the rocket range: in the state checked by 0x0044e600 +0x5c8 = 2.0 and +0x5d8 = +0x5d4 (max range), otherwise
// +0x5d8 = min + rand * (max - min) / 32768 (+0x5d0 / +0x5d4). CaptainObject vtable 0x005acb78 slot 0x20. Not executed
// in recorded runs.
uint32_t CaptainObject_Slot8(uint32_t self)
{
    alignas(4) uint8_t fr[0xc];  // sub esp, 0xc
#define L(o) (u(fr) + (o))
    const uint32_t esi = self;
    orig::fn_00425e20(esi);
    if (static_cast<uint8_t>(orig::fn_0044e600()))
    {
        D(esi + 0x5c8) = 0x40000000u;
        D(esi + 0x5d8) = D(esi + 0x5d4);
        return 0;
    }
    D(L(8)) = D(esi + 0x5d4);
    D(L(0)) = D(esi + 0x5d0);
    D(L(4)) = Rand();
    Fl(esi + 0x5d8) = static_cast<float>(I(L(4))) * (Fl(L(8)) - Fl(L(0))) * K(0x005bcbc0) + Fl(L(0));
    return 0;
#undef L
}

static uint32_t CaptainObject_Slot8_Abi(uint32_t ecx)
{
    return CaptainObject_Slot8(ecx);
}
ABI_ENTRY_00434da0(CaptainObject_Slot8_Entry, CaptainObject_Slot8_Abi)
HOOK(0x00434da0, "CaptainObject_Slot8", CaptainObject_Slot8_Entry, nullptr);

// 0x00434e10 GruntObject_Slot147 (ecx = grunt; also BowGruntObject, CaptainObject, SniperCaptain slot 147). Whether the
// grunt can be grabbed now (guess): 0 when +0x48c & 0x102, in states 0xf, 0xe6..0xeb, 0x196, 0x19a, 0x19b, while the
// timer +0x878 is positive, while holding +0x870, or (without 0x8000 of +0x484) when 0x00454be0 says no; otherwise
// !0x00426630. GruntObject vtable 0x005a9cf0 slot 0x24c. Not executed in recorded runs.
uint32_t GruntObject_Slot147(uint32_t self)
{
    const uint32_t esi = self;
    if (D(esi + 0x48c) & 0x102)
        return 0;
    const uint32_t s = D(esi + 0x460);
    if (s > 0x196)
    {
        if (s >= 0x19a && s <= 0x19b)
            return 0;
    }
    else if (s == 0x196 || s == 0xf || (s > 0xe5 && s <= 0xeb))
        return 0;
    if (Fl(esi + 0x878) > K(0x0056c05c))
        return 0;
    if (D(esi + 0x870))
        return 0;
    if (!(D(esi + 0x484) & 0x8000) && !static_cast<uint8_t>(orig::fn_00454be0(esi)))
        return 0;
    return static_cast<uint8_t>(orig::fn_00426630(esi)) ? 0u : 1u;
}

static uint32_t GruntObject_Slot147_Abi(uint32_t ecx)
{
    return GruntObject_Slot147(ecx);
}
ABI_ENTRY_00434e10(GruntObject_Slot147_Entry, GruntObject_Slot147_Abi)
HOOK(0x00434e10, "GruntObject_Slot147", GruntObject_Slot147_Entry, nullptr);

// 0x00434ea0 GruntObject_Slot66 (ecx = grunt; stack: object; ret 4). The grunt's version of BowGruntObject_Slot66
// (same refusal tests and setup; +0x484 |= 0x2000 is set after the first 0x00454cc0 and physics is not switched on):
// handles +0x39c / +0x398 refreshed, velocity zeroed, 0x00454c90, 0x00454cc0, a0 stored at +0xab0 (referenced),
// 0x00454cc0, virtual +0x1a0 (2, 3, 0, 1, 1, 0, 0.5, 0.5), state 0xf; returns 1. GruntObject vtable slot 0x108. Not
// executed in recorded runs.
uint32_t GruntObject_Slot66(uint32_t self, uint32_t object)
{
    alignas(4) uint8_t fr[0xc];  // sub esp, 0xc: the zero velocity
#define L(o) (u(fr) + (o))
    const uint32_t esi = self;
    if (IsDead(esi))
        return 0;
    const uint32_t f = D(esi + 0x484);
    if ((f & 0x80000000u) || (D(esi + 0x488) & 0x200080) || (f & 0x2000) || (f & 0x28000) || (By(esi + 0x48c) & 2) ||
        D(esi + 0xab0) || (f & 0x20) || D(esi + 0xb54) || !static_cast<uint8_t>(orig::fn_00454be0(esi)))
        return 0;
    D(esi + 0x39c) = orig::fn_00455a00(D(esi + 0x39c));
    const uint32_t edi = esi + 0x398;
    D(edi) = orig::fn_00455a00(D(esi + 0x398));
    D(L(0)) = 0;
    D(L(4)) = 0;
    D(L(8)) = 0;
    orig::fn_00458510(L(0), esi);
    orig::fn_00454c90(esi, 1 /* cl = 1 */, 1);
    orig::fn_00454cc0(esi, edi);
    D(esi + 0x484) |= 0x2000;
    D(esi + 0xab0) = object;
    ++I(object + 0x28);
    orig::fn_00454cc0(esi, edi);
    VC(esi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(esi, 0x1b8, 0xfu);
    return 1;
#undef L
}

static uint32_t GruntObject_Slot66_Abi(uint32_t ecx, uint32_t a0)
{
    return GruntObject_Slot66(ecx, a0);
}
ABI_ENTRY_00434ea0(GruntObject_Slot66_Entry, GruntObject_Slot66_Abi)
HOOK(0x00434ea0, "GruntObject_Slot66", GruntObject_Slot66_Entry, nullptr);

// 0x00434fb0 GruntObject_Slot6 (ecx = grunt; stack: 4 load arguments; ret 0x10). Init: Ai_InitGeneric, +0x604 /
// +0x608 lists; when virtual +0xd8 says so the weapon attachment +0x61c is set up at "ref_rhand" (0x004a0ea0,
// 0x004a0ef0 (0xf), 0x004a10e0 (0)) and a random behaviour is chosen as in BowGruntObject_Slot6 (rand * 300 >> 15:
// < 100 -> +0x484 |= 0x200000, < 200 -> +0x488 |= 0x20000000). GruntObject vtable slot 0x18. Not executed in recorded
// runs.
uint32_t GruntObject_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    orig::fn_004247f0(ebp, a0, a1, a2, a3);
    D(ebp + 0x604) = D(World() + 0xe88);
    D(ebp + 0x608) = D(World() + 0xe7c);
    if (!VB(ebp, 0xd8))
        return 0;
    const uint32_t att = ebp + 0x61c;
    orig::fn_004a0ea0(0x005b6160u /* "ref_rhand" */, ebp, att, 0, 0);
    orig::fn_004a0ef0(0xf, att);
    orig::fn_004a10e0(att, 0);
    const uint32_t q = (Rand() * 0x12c) >> 15;
    D(ebp + 0x484) &= 0xffdfffffu;
    D(ebp + 0x488) &= 0xdfffffffu;
    if (q < 100)
        D(ebp + 0x484) |= 0x200000;
    else if (q < 200)
        D(ebp + 0x488) |= 0x20000000;
    return 0;
}

static uint32_t GruntObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return GruntObject_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00434fb0(GruntObject_Slot6_Entry, GruntObject_Slot6_Abi)
HOOK(0x00434fb0, "GruntObject_Slot6", GruntObject_Slot6_Entry, nullptr);

// 0x00435090 Q112Object_Slot6 (ecx = Q112; stack: 4 load arguments, a2 = properties, a3 = source; ret 0x10). Init:
// +0xb5c = 0, +0x5fc = 25.0, +0x600 = 35.0, Ai_InitGeneric, "bullet_scale" (default 0.5) into +0xb58, +0x604 / +0x608
// lists; when virtual +0xd8 says so the weapon attachment +0x61c is set up at "ref_chaingun" (0x004a0ea0, 0x004a0ef0
// (0x13), 0x004a10e0 (0), 0x004a0e00 (al = 0)), 0x00428250 (cl = 1) and 0x004246c0 (al = 1; self). Q112Object vtable
// 0x005a2440 slot 0x18. Not executed in recorded runs.
uint32_t Q112Object_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    D(ebp + 0xb5c) = 0;
    D(ebp + 0x5fc) = 0x41c80000u;
    D(ebp + 0x600) = 0x420c0000u;
    orig::fn_004247f0(ebp, a0, a1, a2, a3);
    orig::fn_004534a0(a3, ebp + 0xb58, ebp, 0x005b6140u /* "bullet_scale" */, a2, 0x3f000000u, 1);
    D(ebp + 0x604) = D(World() + 0xe88);
    D(ebp + 0x608) = D(World() + 0xe7c);
    if (!VB(ebp, 0xd8))
        return 0;
    const uint32_t att = ebp + 0x61c;
    orig::fn_004a0ea0(0x005b6130u /* "ref_chaingun" */, ebp, att, 0, 0);
    orig::fn_004a0ef0(0x13, att);
    orig::fn_004a10e0(att, 0);
    orig::fn_004a0e00(0 /* al = 0 */, att);
    orig::fn_00428250(ebp, 1 /* cl = 1 */);
    return orig::fn_004246c0(1 /* al = 1 */, ebp);
}

static uint32_t Q112Object_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Q112Object_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00435090(Q112Object_Slot6_Entry, Q112Object_Slot6_Abi)
HOOK(0x00435090, "Q112Object_Slot6", Q112Object_Slot6_Entry, nullptr);

// 0x00435220 Q112Object_Slot53 (ecx = Q112; stack: a0; ret 4): renderer (Game+0x2c) virtual +0xd8 (self+0xb5c, 0),
// then Character_Die 0x004319e0 (a0). Q112Object vtable slot 0xd4. Not executed in recorded runs.
uint32_t Q112Object_Slot53(uint32_t self, uint32_t a0)
{
    VC(D(GameP() + 0x2c), 0xd8, self + 0xb5c, 0u);
    return orig::fn_004319e0(self, a0);
}

static uint32_t Q112Object_Slot53_Abi(uint32_t ecx, uint32_t a0)
{
    return Q112Object_Slot53(ecx, a0);
}
ABI_ENTRY_00435220(Q112Object_Slot53_Entry, Q112Object_Slot53_Abi)
HOOK(0x00435220, "Q112Object_Slot53", Q112Object_Slot53_Entry, nullptr);

// 0x00435260 PeasantGenAI_Slot6 (ecx = peasant; stack: 4 load arguments; ret 0x10). Init: +0xb58 = 0, Ai_InitGeneric,
// +0x948 = 40.0, the body ([+0x1c4]+8) virtual +0x1c (45.1), +0x604 / +0x608 lists, unlinked from World+0xe90, +0xe94,
// +0xe7c, +0xea4. PeasantGenAI vtable 0x005aa9c0 slot 0x18. Not executed in recorded runs.
uint32_t PeasantGenAI_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t edi = self;
    D(edi + 0xb58) = 0;
    orig::fn_004247f0(edi, a0, a1, a2, a3);
    D(edi + 0x948) = 0x42200000u;
    VC(D(D(edi + 0x1c4) + 8), 0x1c, 0x42346666u);
    D(edi + 0x604) = D(World() + 0xe88);
    D(edi + 0x608) = D(World() + 0xe7c);
    orig::fn_00454750(D(World() + 0xe90), edi);
    orig::fn_00454750(D(World() + 0xe94), edi);
    orig::fn_00454750(D(World() + 0xe7c), edi);
    return orig::fn_00454750(D(World() + 0xea4), edi);
}

static uint32_t PeasantGenAI_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PeasantGenAI_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00435260(PeasantGenAI_Slot6_Entry, PeasantGenAI_Slot6_Abi)
HOOK(0x00435260, "PeasantGenAI_Slot6", PeasantGenAI_Slot6_Entry, nullptr);

// 0x00435330 PeasantGenAI_Slot55 (ecx = peasant). Destroy: the carried object +0xb58 is stopped (virtual +0xdc) and
// released, then tail call GenericAIObject_Slot55. PeasantGenAI vtable slot 0xdc. Not executed in recorded runs.
uint32_t PeasantGenAI_Slot55(uint32_t self)
{
    ClearRefDc(self + 0xb58);
    return GenericAIObject_Slot55(self);
}

static uint32_t PeasantGenAI_Slot55_Abi(uint32_t ecx)
{
    return PeasantGenAI_Slot55(ecx);
}
ABI_ENTRY_00435330(PeasantGenAI_Slot55_Entry, PeasantGenAI_Slot55_Abi)
HOOK(0x00435330, "PeasantGenAI_Slot55", PeasantGenAI_Slot55_Entry, nullptr);

// 0x00435380 PeasantGenAI_Slot64 (ecx = peasant; stack: fade value; ret 4): passes a0 to the carried object +0xb58
// (virtual +0x100) and to Character_SetFade 0x0042eb60. PeasantGenAI vtable slot 0x100.
uint32_t PeasantGenAI_Slot64(uint32_t self, uint32_t a0)
{
    if (D(self + 0xb58))
        VC(D(self + 0xb58), 0x100, a0);
    return orig::fn_0042eb60(self, a0);
}

static uint32_t PeasantGenAI_Slot64_Abi(uint32_t ecx, uint32_t a0)
{
    return PeasantGenAI_Slot64(ecx, a0);
}
ABI_ENTRY_00435380(PeasantGenAI_Slot64_Entry, PeasantGenAI_Slot64_Abi)
HOOK(0x00435380, "PeasantGenAI_Slot64", PeasantGenAI_Slot64_Entry, nullptr);

// 0x004353b0 PeasantGenAI_Slot95 (ecx = peasant; stack: a0, a1 (unused); ret 8). Drops the carried object +0xb58
// (Object_Detach, physics off, given the peasant's velocity with z = rand * 3 / 32768 + 5), then Character_GoRagdoll
// 0x00431310 (a0, 1). PeasantGenAI vtable slot 0x17c. Not executed in recorded runs.
uint32_t PeasantGenAI_Slot95(uint32_t self, uint32_t a0)
{
    alignas(4) uint8_t fr[0x10];  // sub esp, 0x10: rand at +0, velocity at +4
#define L(o) (u(fr) + (o))
    const uint32_t edi = self;
    const uint32_t esi = D(edi + 0xb58);
    if (esi)
    {
        orig::fn_00453b20(esi);
        orig::fn_00458000(edi, L(4));
        D(L(0)) = Rand();
        const uint32_t carried = D(edi + 0xb58);
        Fl(L(0xc)) = static_cast<float>(I(L(0))) * K(0x005bcf58) + K(0x005bcbdc);
        orig::fn_004577b0(carried, 0);
        orig::fn_00458510(L(4), D(edi + 0xb58));
    }
    return orig::fn_00431310(edi, a0, 1);
#undef L
}

static uint32_t PeasantGenAI_Slot95_Abi(uint32_t ecx, uint32_t a0, uint32_t /*a1*/)
{
    return PeasantGenAI_Slot95(ecx, a0);
}
ABI_ENTRY_004353b0(PeasantGenAI_Slot95_Entry, PeasantGenAI_Slot95_Abi)
HOOK(0x004353b0, "PeasantGenAI_Slot95", PeasantGenAI_Slot95_Entry, nullptr);

// 0x00435420 (ecx = boss; stack: 4 load arguments, a2 = properties, a3 = source; ret 0x10). The shared init of the
// big vehicles / bosses (callers: SaucerObject, ZeppelinObject, ColossusObject, Q112Boss slot 6). Clears +0xb58,
// +0xb8d, +0xb94, sets +0xb8e = 1; reads "target_tag_1".."target_tag_10" and "target_tag_priority" (+0xb60..+0xb88,
// Object_ReadValuePropEbx), "stop_and_fire" (bool, default 1) into +0xb8c and "stop_volley_count" (default 3, into a2's
// stack slot; its low byte goes to +0xb8f); a target list (PMS_heapAlloc "new" + 0x004e7010) at +0xb90 and +0x604;
// +0x608 = World+0xe7c; Ai_InitGeneric; "fire_range_min/max" (+0x5d0 / +0x5d4, 200 / 350) with +0x5d8 = +0x5dc = a
// random range between them; +0x594 / +0x598 = the old +0x5ac / +0x5b0, then "fire_delay_min/max" (+0x5ac / +0x5b0,
// 0.25 / 0.5); +0x5c8 = random between +0x594 and +0x598; +0x5b8 = random count in +0x5a4..+0x5a8; +0x5c0 = 0; linked
// into World+0xf1c; +0x48c |= 0x200; +0xb5c = 1. The original keeps its float temporaries in the argument slots
// (reproduced in a local copy of the argument block). Not executed in recorded runs.
uint32_t fn_00435420(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    uint32_t args[4] = {a0, a1, a2, a3};  // the argument slots [esp+0x14].. (with four saved registers)
#define A(i) (u(args) + 4 * (i))
    D(ebp + 0xb58) = 0;
    By(ebp + 0xb8d) = 0;
    D(ebp + 0xb94) = 0;
    By(ebp + 0xb8e) = 1;
    static const uint32_t tags[11] = {0x005b60f8u, 0x005b60e8u, 0x005b60d8u, 0x005b60c8u, 0x005b60b8u, 0x005b60a8u,
                                      0x005b6098u, 0x005b6088u, 0x005b6078u, 0x005b6068u, 0x005b6054u};
    uint32_t r = 0;
    for (uint32_t i = 0; i < 11; ++i)
        r = orig::fn_00453450(0, tags[i], a3, ebp + 0xb60 + 4 * i, ebp, a2, 0);
    const uint32_t ebx = a2;
    orig::fn_004534f0(r & 0xffffff00u /* xor al, al */, a3, ebp + 0xb8c, ebp, 0x005b6044u /* "stop_and_fire" */, ebx, 1);
    orig::fn_00453400(3, a3, A(2), ebp, 0x005b6030u /* "stop_volley_count" */, ebx, 1);
    By(ebp + 0xb8f) = By(A(2));
    const uint32_t mem = orig::fn_004e07f0(0x0063a210u, 0x10, 0x18, 2, 0x0058b5e8u /* "new" */);
    const uint32_t list = mem ? orig::fn_004e7010(D(GameP() + 0xac4), 0, mem) : 0;
    D(ebp + 0xb90) = list;
    D(ebp + 0x604) = list;
    D(ebp + 0x608) = D(World() + 0xe7c);
    orig::fn_004247f0(ebp, D(A(0)), D(A(1)), ebx, D(A(3)));
    orig::fn_004534a0(a3, ebp + 0x5d0, ebp, 0x005b6ac4u /* "fire_range_min" */, ebx, 0x43480000u, 0);
    orig::fn_004534a0(a3, ebp + 0x5d4, ebp, 0x005b6ab4u /* "fire_range_max" */, ebx, 0x43af0000u, 0);
    D(A(0)) = D(ebp + 0x5d4);
    D(A(2)) = D(ebp + 0x5d0);
    D(A(1)) = Rand();
    {
        const uint32_t old5ac = D(ebp + 0x5ac);
        const uint32_t old5b0 = D(ebp + 0x5b0);
        const float range = static_cast<float>(I(A(1))) * (Fl(A(0)) - Fl(A(2)));
        D(ebp + 0x594) = old5ac;
        const float range2 = range * K(0x005bcbc0);
        D(ebp + 0x598) = old5b0;
        const float v = range2 + Fl(A(2));
        Fl(ebp + 0x5d8) = v;
        Fl(ebp + 0x5dc) = v;
    }
    orig::fn_004534a0(a3, ebp + 0x5ac, ebp, 0x005b6020u /* "fire_delay_min" */, ebx, 0x3e800000u, 1);
    orig::fn_004534a0(a3, ebp + 0x5b0, ebp, 0x005b6010u /* "fire_delay_max" */, ebx, 0x3f000000u, 1);
    D(A(1)) = D(ebp + 0x598);
    D(A(3)) = D(ebp + 0x594);
    D(A(2)) = Rand();
    {
        const uint32_t hi = D(ebp + 0x5a8);
        const uint32_t lo = D(ebp + 0x5a4);
        Fl(ebp + 0x5c8) = static_cast<float>(I(A(2))) * (Fl(A(1)) - Fl(A(3))) * K(0x005bcbc0) + Fl(A(3));
        const uint32_t q = Rand();
        D(ebp + 0x5b8) = static_cast<uint32_t>(static_cast<int32_t>(q * (hi - lo + 1)) >> 15) + lo;
    }
    D(ebp + 0x5c0) = 0;
    orig::fn_004546d0(D(World() + 0xf1c), ebp);
    D(ebp + 0x48c) |= 0x200;
    D(ebp + 0xb5c) = 1;
    return 0;
#undef A
}

static uint32_t fn_00435420_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00435420(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00435420(fn_00435420_Entry, fn_00435420_Abi)
HOOK(0x00435420, "fn_00435420", fn_00435420_Entry, nullptr);

// 0x00435770 Q112Boss_Slot8 (ecx = boss; also SaucerObject, ColossusObject, ZeppelinObject slot 8). Resolves the
// target tags read by 0x00435420: each non-zero "target_tag_N" (+0xb60..+0xb84) is looked up in World+0xe70
// (0x004592e0 (eax = list, ecx = tag; self)) and linked into the target list +0xb90, or logged ("AI : Can't find
// target tag %d\n%s", Debug_Printf, with the name at [[+0x414]+0x10]+4); the "target_tag_priority" +0xb88 object goes
// to +0xb94 (reference taken) or is logged ("AI : Can't find priority target tag %d\n%s"). Q112Boss vtable 0x005a4098
// slot 0x20. Not executed in recorded runs.
uint32_t Q112Boss_Slot8(uint32_t self)
{
    const uint32_t ebx = self;
    for (uint32_t ebp = ebx + 0xb60, n = 0xa; n; ebp += 4, --n)
    {
        const uint32_t tag = D(ebp);
        if (!tag)
            continue;
        const uint32_t obj = orig::fn_004592e0(D(World() + 0xe70), tag, ebx);
        if (!obj)
            DebugPrintf(0x005b5fecu, D(ebp), D(D(ebx + 0x414) + 0x10) + 4);
        else
            orig::fn_004546d0(D(ebx + 0xb90), obj);
    }
    const uint32_t ptag = D(ebx + 0xb88);
    if (!ptag)
        return 0;
    const uint32_t obj = orig::fn_004592e0(D(World() + 0xe70), ptag, ebx);
    D(ebx + 0xb94) = obj;
    if (!obj)
    {
        DebugPrintf(0x005b5fc0u, D(ebx + 0xb88), D(D(ebx + 0x414) + 0x10) + 4);
        return 0;
    }
    ++I(obj + 0x28);
    return obj;
}

static uint32_t Q112Boss_Slot8_Abi(uint32_t ecx)
{
    return Q112Boss_Slot8(ecx);
}
ABI_ENTRY_00435770(Q112Boss_Slot8_Entry, Q112Boss_Slot8_Abi)
HOOK(0x00435770, "Q112Boss_Slot8", Q112Boss_Slot8_Entry, nullptr);

// 0x00435860 Q112Boss_Slot55 (ecx = boss; also SaucerObject, ColossusObject, ZeppelinObject). Destroy: the effect
// +0xb58 is released (Fx_Release 0x0041d030 (Game+0xa90, &+0xb58)), the target list +0xb90 is emptied (unlink each),
// destroyed (virtual +0x10 (1)) and cleared, the priority target +0xb94 released, then GenericAIObject_Slot55.
// Q112Boss vtable slot 0xdc. Not executed in recorded runs.
uint32_t Q112Boss_Slot55(uint32_t self)
{
    const uint32_t ebx = self;
    uint32_t iter[2] = {0, 0};
    if (D(ebx + 0xb58))
        orig::fn_0041d030(D(GameP() + 0xa90), ebx + 0xb58);
    for (uint32_t m = orig::fn_004e7340(D(ebx + 0xb90), u(iter)); m; m = orig::fn_004e73c0(u(iter)))
        orig::fn_00454750(D(ebx + 0xb90), m);
    if (D(ebx + 0xb90))
        VC(D(ebx + 0xb90), 0x10, 1u);
    D(ebx + 0xb90) = 0;
    ClearRef(ebx + 0xb94);
    GenericAIObject_Slot55(ebx);
    if (iter[1])
        --I(iter[1] + 0x18);
    return iter[1];
}

static uint32_t Q112Boss_Slot55_Abi(uint32_t ecx)
{
    return Q112Boss_Slot55(ecx);
}
ABI_ENTRY_00435860(Q112Boss_Slot55_Entry, Q112Boss_Slot55_Abi)
HOOK(0x00435860, "Q112Boss_Slot55", Q112Boss_Slot55_Entry, nullptr);

// 0x00435940 Q112Boss_Slot102 (ecx = boss; stack: 5 arguments, unused; ret 0x14). The boss's firing cycle, returns
// whether a volley finished (AL). Without a weapon target +0x588: 1. With virtual +0x228 (guess: "is airborne /
// turret mode") 0x0042df70 (45.0). While the clip timer +0x5c8 runs: aim animation (virtual +0x1a0 (2, 3, 0, 1, 1, 0,
// +0x580, +0x584)) and the timer counts down by the frame time, 0. Otherwise the weapon of the attachment +0x61c
// (0x004a15f0) gets virtual +0x138, the fire animation (virtual +0x1a0 (0x23, 0x24, 0x21, 0x22, 1, 0, +0x580, +0x584)),
// and when the burst timer +0x5cc has run out a shot (virtual +0x194 (+0x588 or 0, 0, 0 / 1, 0)); the burst counter
// +0x5b8 counts down: bursts left restart +0x5cc (0x0046e2c0 between +0x5ac and +0x5b0), 0; the last one restarts the
// clip (in the +0x228 mode +0x5c8 = 0, +0xb8e = 1, virtual +0x23c (0, 0), +0x5c0 = 0; else +0x5c8 random between
// +0x594 / +0x598 and +0x5c0++), a new burst count in +0x5a4..+0x5a8, 1. Q112Boss vtable slot 0x198 (also Saucer,
// Colossus, Zeppelin). Not executed in recorded runs.
uint32_t Q112Boss_Slot102(uint32_t self)
{
    const uint32_t esi = self;
    if (!D(esi + 0x588))
        return 1;
    if (VB(esi, 0x228))
        orig::fn_0042df70(esi, 0x42340000u /* 45.0 */);
    if (Fl(esi + 0x5c8) > K(0x0056c05c))
    {
        if (VB(esi, 0x228))
            VC(esi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, D(esi + 0x580), D(esi + 0x584));
        Fl(esi + 0x5c8) = Fl(esi + 0x5c8) - Fl(0x00604a90);
        return 0;
    }
    const uint32_t att = esi + 0x61c;
    if (orig::fn_004a15f0(att))
        VC(orig::fn_004a15f0(att), 0x138);
    if (VB(esi, 0x228))
        VC(esi, 0x1a0, 0x23u, 0x24u, 0x21u, 0x22u, 1u, 0u, D(esi + 0x580), D(esi + 0x584));
    if (Fl(esi + 0x5cc) > K(0x0056c05c))
    {
        Fl(esi + 0x5cc) = Fl(esi + 0x5cc) - Fl(0x00604a90);
        return 0;
    }
    if (VB(esi, 0x228))
        VC(esi, 0x194, D(esi + 0x588), 0u, 0u, 0u);
    else
        VC(esi, 0x194, 0u, 0u, 1u, 0u);
    const int32_t left = I(esi + 0x5b8) - 1;
    I(esi + 0x5b8) = left;
    if (left > 0)
    {
        Fl(esi + 0x5cc) = orig::fn_0046e2c0(0, D(esi + 0x5ac), D(esi + 0x5b0));
        return 0;
    }
    if (VB(esi, 0x228))
    {
        D(esi + 0x5c8) = 0;
        By(esi + 0xb8e) = 1;
        VC(esi, 0x23c, 0u, 0u);
        D(esi + 0x5c0) = 0;
    }
    else
    {
        Fl(esi + 0x5c8) = orig::fn_0046e2c0(0, D(esi + 0x594), D(esi + 0x598));
        ++D(esi + 0x5c0);
    }
    const uint32_t hi = D(esi + 0x5a8);
    const uint32_t lo = D(esi + 0x5a4);
    const uint32_t q = Rand();
    D(esi + 0x5b8) = static_cast<uint32_t>(static_cast<int32_t>(q * (hi - lo + 1)) >> 15) + lo;
    return 1;
}

static uint32_t Q112Boss_Slot102_Abi(uint32_t ecx, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
{
    return Q112Boss_Slot102(ecx);
}
ABI_ENTRY_00435940(Q112Boss_Slot102_Entry, Q112Boss_Slot102_Abi)
HOOK(0x00435940, "Q112Boss_Slot102", Q112Boss_Slot102_Entry, nullptr);

// 0x00435b50 (stack: boss; ret 4). The boss's targeting / landing update (caller 0x00447400). The retarget timer
// +0x490 counts down by the frame time; when it has run out (and no clip is pending with a weapon target +0x588, or
// the clip timer +0x5c8 still runs) and the boss may retarget (+0xb8e with virtual +0x228 off or state 0x110, or state
// 0x10e): a target no longer acceptable (virtual +0x180 (target, range +0x5d8)) is dropped (+0xb8e = 1, +0x5c0 = 0, a
// new burst count); a priority target +0xb94 that is the current one is checked for a line of fire from "ref_hit_00"
// (0x00428280, 0x00426fd0) and dropped when blocked (burst count 0x004e7740); then a new target is chosen: the priority
// target when acceptable, visible within 180 (virtual +0xf0 of the weapon, 0x004a15f0 / virtual +0x13c, or of the boss
// with 0x00428280) and in the line of fire, otherwise AI_FindTarget 0x00426920 (range, 0, 1); a found target is taken
// (+0xb8e = 0, virtual +0x23c (target, 0)); the timer restarts at 1 + rand / 32768 s. Otherwise +0xb8e = 1 and the timer
// restarts the same way. Then with a weapon target: unless in virtual +0x228 mode outside state 0x110, +0x488 |=
// 0x1800, and (not in states 0x66 / 0x110) when "stop_and_fire" (+0xb8c) or the volley count +0xb8f is reached by
// +0x5c0, the boss stops (handles refreshed, linear and angular velocity zeroed) and either lands (0x00426630: physics
// on, state 0x66, +0xb5c = 3) or hovers (virtual +0x1a0 (2, 3, 0, 1, 1, 0, 0.5, 0.5), state 0x110). Without a target:
// +0x488 &= ~0x1000 and a landed boss (+0xb5c == 3) takes off again (physics off, the same animation, state 0x64,
// +0xb5c = 2). The original keeps the rand temporary and a flag in the argument's stack slot. Not executed in recorded
// runs.
uint32_t fn_00435b50(uint32_t boss)
{
    alignas(4) uint8_t fr[0xc];  // sub esp, 0xc: a vec3 at +0
#define L(o) (u(fr) + (o))
    std::memset(fr, 0, sizeof fr);
    const uint32_t ebp = boss;
    uint32_t argSlot = boss;  // [esp+0x14]: the argument slot, reused for rand and a byte flag
    const float t = Fl(ebp + 0x490) - Fl(0x00604a90);
    Fl(ebp + 0x490) = t;
    if (!(t > K(0x0056c05c)))
    {
        if (!(Fl(ebp + 0x5c8) > K(0x0056c05c)) && D(ebp + 0x588))
            goto L_435e0b;
        bool retarget;
        if (By(ebp + 0xb8e) && (!VB(ebp, 0x228) || D(ebp + 0x460) == 0x110))
            retarget = true;
        else
            retarget = D(ebp + 0x460) == 0x10e;
        if (!retarget)
        {
            By(ebp + 0xb8e) = 1;
            argSlot = Rand();
        }
        else
        {
            const uint32_t ebx = ebp + 0x588;
            if (D(ebx) && !VB(ebp, 0x180, D(ebx), D(ebp + 0x5d8)))
            {
                const uint32_t hi = D(ebp + 0x5a8);
                const uint32_t lo = D(ebp + 0x5a4);
                By(ebp + 0xb8e) = 1;
                D(ebp + 0x5c0) = 0;
                const uint32_t q = Rand();
                D(ebp + 0x5b8) = static_cast<uint32_t>(static_cast<int32_t>(q * (hi - lo + 1)) >> 15) + lo;
                Unref(D(ebx));
                D(ebx) = 0;
            }
            argSlot = (argSlot & 0xffffff00u) | 1;
            if (D(ebx) && D(ebp + 0xb94) == D(ebx))
            {
                orig::fn_00428280(L(0), ebp, ebp, 0x005b68fcu /* "ref_hit_00" */);
                if (!static_cast<uint8_t>(orig::fn_00426fd0(L(0), ebp, D(ebx), ebp)))
                {
                    const uint32_t hi = D(ebp + 0x5a8);
                    const uint32_t lo = D(ebp + 0x5a4);
                    By(ebp + 0xb8e) = 1;
                    D(ebp + 0x5c0) = 0;
                    const uint32_t n = orig::fn_004e7740(hi, lo);
                    const uint32_t old = D(ebx);
                    D(ebp + 0x5b8) = n;
                    argSlot &= 0xffffff00u;
                    Unref(old);
                    D(ebx) = 0;
                }
            }
            uint32_t found = 0;
            const uint32_t prio = D(ebp + 0xb94);
            if (!prio || D(ebx) != prio)
            {
                if (static_cast<uint8_t>(argSlot) && VB(ebp, 0x180, prio, D(ebp + 0x5d8)))
                {
                    uint32_t viewer;
                    const uint32_t w = orig::fn_004a15f0(ebp + 0x61c);
                    if (w)
                    {
                        VC(w, 0x13c, L(0), 0u);
                        viewer = w;
                    }
                    else
                    {
                        orig::fn_00428280(L(0), ebp, ebp, 0x005b68fcu /* "ref_hit_00" */);
                        viewer = ebp;
                    }
                    if (VB(viewer, 0xf0, D(ebp + 0xb94), 0x43340000u /* 180.0 */))
                    {
                        if (static_cast<uint8_t>(orig::fn_00426fd0(L(0), ebp, D(ebp + 0xb94), ebp)))
                            found = D(ebp + 0xb94);
                        else
                            found = orig::fn_00426920(ebp, D(ebp + 0x5d8), 0, 1);
                    }
                }
                else
                    found = orig::fn_00426920(ebp, D(ebp + 0x5d8), 0, 1);
                if (found)
                {
                    By(ebp + 0xb8e) = 0;
                    VC(ebp, 0x23c, found, 0u);
                }
            }
            argSlot = Rand();
        }
        Fl(ebp + 0x490) = static_cast<float>(static_cast<int32_t>(argSlot)) * K(0x005bcbc0) + K(0x005bcb2c);
    }
    // 0x00435dfd
    if (!D(ebp + 0x588))
    {
        D(ebp + 0x488) &= 0xffffefffu;
        if (D(ebp + 0xb5c) == 3)
        {
            orig::fn_004577b0(ebp, 0);
            VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
            VC(ebp, 0x1b8, 0x64u);
            D(ebp + 0xb5c) = 2;
        }
        return 0;
    }
L_435e0b:
    if (VB(ebp, 0x228) && D(ebp + 0x460) != 0x110)
        return 0;
    D(ebp + 0x488) |= 0x1800;
    {
        const uint32_t s = D(ebp + 0x460);
        if (s == 0x66 || s == 0x110)
            return 0;
    }
    if (!By(ebp + 0xb8c))
    {
        const uint8_t v = By(ebp + 0xb8f);
        if (!v || static_cast<uint32_t>(v) > D(ebp + 0x5c0))
            return 0;
    }
    D(ebp + 0x39c) = orig::fn_00455a00(D(ebp + 0x39c));
    {
        const uint32_t h = orig::fn_00455a00(D(ebp + 0x398));
        D(L(0)) = 0;
        D(L(4)) = 0;
        D(ebp + 0x398) = h;
        D(L(8)) = 0;
    }
    orig::fn_00458510(L(0), ebp);
    orig::fn_00458580(L(0), ebp);
    if (static_cast<uint8_t>(orig::fn_00426630(ebp)))
    {
        D(L(0)) = 0;
        D(L(4)) = 0;
        D(L(8)) = 0;
        orig::fn_00458510(L(0), ebp);
        orig::fn_004577b0(ebp, 1);
        VC(ebp, 0x1b8, 0x66u);
        D(ebp + 0xb5c) = 3;
        return 0;
    }
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    return VC(ebp, 0x1b8, 0x110u);
#undef L
}

static uint32_t fn_00435b50_Abi(uint32_t a0)
{
    return fn_00435b50(a0);
}
ABI_ENTRY_00435b50(fn_00435b50_Entry, fn_00435b50_Abi)
HOOK(0x00435b50, "fn_00435b50", fn_00435b50_Entry, nullptr);

// 0x00435fa0 SaucerObject_Slot52 (ecx = saucer; also ZeppelinObject; stack: damage message; ret 4). A hit from a
// source (+0x2c) of the other side (virtual +0xe4 differs) makes it the target (virtual +0x23c (source, 0)) unless
// +0x488 has 0x4000 or the current target +0x588 is the priority target +0xb94; then GenericAIObject_Slot52. SaucerObject
// vtable 0x005a5e98 slot 0xd0. Not executed in recorded runs.
uint32_t SaucerObject_Slot52(uint32_t self, uint32_t msg)
{
    const uint32_t esi = self;
    if (!(D(esi + 0x488) & 0x4000))
    {
        const uint32_t src = D(msg + 0x2c);
        if (src)
        {
            const uint8_t bl = static_cast<uint8_t>(VC(esi, 0xe4));
            if (static_cast<uint8_t>(VC(src, 0xe4)) != bl && D(esi + 0x588) != D(esi + 0xb94))
                VC(esi, 0x23c, D(msg + 0x2c), 0u);
        }
    }
    return GenericAIObject_Slot52(esi, msg);
}

static uint32_t SaucerObject_Slot52_Abi(uint32_t ecx, uint32_t a0)
{
    return SaucerObject_Slot52(ecx, a0);
}
ABI_ENTRY_00435fa0(SaucerObject_Slot52_Entry, SaucerObject_Slot52_Abi)
HOOK(0x00435fa0, "SaucerObject_Slot52", SaucerObject_Slot52_Entry, nullptr);

// 0x00436010 Q112Boss_Slot53 (ecx = boss; also SaucerObject, ColossusObject; stack: message or 0; ret 4). Death: the
// trigger +0x528 is sent (0x00461870), Game+0x270c++; when virtual +0x210 says so: with a message lacking flag 0x8000,
// virtual +0x158 unless virtual +0x154; then 0x00427100 (eax = self); otherwise Object_Kill 0x00455640 (a0). Q112Boss
// vtable slot 0xd4; caller 0x00436260. Not executed in recorded runs.
uint32_t Q112Boss_Slot53(uint32_t self, uint32_t msg)
{
    const uint32_t esi = self;
    const uint32_t ebx = D(esi + 0x528);
    if (ebx)
        orig::fn_00461870(World(), ebx, esi);
    ++I(GameP() + 0x270c);
    if (!VB(esi, 0x210))
        return orig::fn_00455640(esi, msg);
    if (msg && !(D(msg) & 0x8000) && !VB(esi, 0x154))
        VC(esi, 0x158);
    return orig::fn_00427100(esi);
}

static uint32_t Q112Boss_Slot53_Abi(uint32_t ecx, uint32_t a0)
{
    return Q112Boss_Slot53(ecx, a0);
}
ABI_ENTRY_00436010(Q112Boss_Slot53_Entry, Q112Boss_Slot53_Abi)
HOOK(0x00436010, "Q112Boss_Slot53", Q112Boss_Slot53_Entry, nullptr);

// Shared body of 0x00436090 / 0x00436180: the boss init 0x00435420, then (virtual +0xd8) the turret attachment +0x61c
// at "ref_turret" (0x004a0ea0 (2), 0x004a0ef0 (type), 0x004a10e0 (0)); the turret object (0x004a15f0) is placed with the
// boss's matrix (virtual +0x5c) moved to the turret's position (virtual +0x54) by Object_SetTransform 0x00457990
// (al = 0; turret, matrix); +0x5c4 = 450.0, +0x860 = 360.0, virtual +0xf8 (0x100, 0).
namespace
{
uint32_t TurretBossInit(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t type)
{
    alignas(4) uint8_t fr[0x50];  // sub esp, 0x50: turret position at +0, matrix (0x44 bytes) at +0xc
#define L(o) (u(fr) + (o))
    std::memset(fr, 0, sizeof fr);
    const uint32_t ebp = self;
    fn_00435420(ebp, a0, a1, a2, a3);
    if (!VB(ebp, 0xd8))
        return 0;
    const uint32_t att = ebp + 0x61c;
    orig::fn_004a0ea0(0x005b5fb4u /* "ref_turret" */, ebp, att, 0, 2);
    orig::fn_004a0ef0(type, att);
    orig::fn_004a10e0(att, 0);
    const uint32_t w = orig::fn_004a15f0(att);
    if (w)
    {
        VC(w, 0x54, L(0));
        VC(ebp, 0x5c, L(0xc));
        D(L(0x18)) = D(L(0));
        D(L(0x38)) = D(L(8));
        D(L(0x28)) = D(L(4));
        orig::fn_00457990(D(L(8)) & 0xffffff00u /* eax = z, xor al, al */, w, L(0xc));
    }
    D(ebp + 0x5c4) = 0x43e10000u;
    D(ebp + 0x860) = 0x43b40000u;
    return VC(ebp, 0xf8, 0x100u, 0u);
#undef L
}
}  // namespace

// 0x00436090 SaucerObject_Slot6 (ecx = saucer; stack: 4 load arguments; ret 0x10). Init: TurretBossInit (attachment
// type 0x14), then +0xb8d = 1. SaucerObject vtable slot 0x18. Not executed in recorded runs.
uint32_t SaucerObject_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t r = TurretBossInit(self, a0, a1, a2, a3, 0x14);
    By(self + 0xb8d) = 1;
    return r;
}

static uint32_t SaucerObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return SaucerObject_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00436090(SaucerObject_Slot6_Entry, SaucerObject_Slot6_Abi)
HOOK(0x00436090, "SaucerObject_Slot6", SaucerObject_Slot6_Entry, nullptr);

// 0x00436180 ZeppelinObject_Slot6 (ecx = zeppelin; stack: 4 load arguments; ret 0x10). Init: TurretBossInit
// (attachment type 0x15). ZeppelinObject vtable 0x005af500 slot 0x18. Not executed in recorded runs.
uint32_t ZeppelinObject_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return TurretBossInit(self, a0, a1, a2, a3, 0x15);
}

static uint32_t ZeppelinObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return ZeppelinObject_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00436180(ZeppelinObject_Slot6_Entry, ZeppelinObject_Slot6_Abi)
HOOK(0x00436180, "ZeppelinObject_Slot6", ZeppelinObject_Slot6_Entry, nullptr);

namespace
{
// The turret placement shared by 0x00436090 / 0x00436180 / 0x00436390 / 0x004364b0: the boss's matrix (virtual +0x5c)
// moved to the turret's position (virtual +0x54) and applied to the turret (Object_SetTransform 0x00457990, al = 0).
void PlaceTurret(uint32_t ebp, uint32_t w)
{
    alignas(4) uint8_t fr[0x50];  // the callers' sub esp, 0x50: position at +0, matrix (0x44 bytes) at +0xc
#define L(o) (u(fr) + (o))
    std::memset(fr, 0, sizeof fr);
    VC(w, 0x54, L(0));
    VC(ebp, 0x5c, L(0xc));
    D(L(0x18)) = D(L(0));
    D(L(0x38)) = D(L(8));
    D(L(0x28)) = D(L(4));
    orig::fn_00457990(D(L(8)) & 0xffffff00u /* eax = z, xor al, al */, w, L(0xc));
#undef L
}
}  // namespace

// 0x00436390 ColossusObject_Slot6 (ecx = colossus; stack: 4 load arguments, a2 = properties, a3 = source; ret 0x10).
// Init: the boss init 0x00435420; when virtual +0xd8 says so +0x5d8 = +0x5dc = 400.0, "bullet_scale" (0.5) into
// +0xb98, the turret attachment at "ref_turret" (type 0x16), unlinked from World+0xf1c, the turret placed
// (PlaceTurret), +0x5c4 = 450.0, +0x860 = 360.0; always virtual +0xf8 (0x20000, 0). ColossusObject vtable 0x005aa238
// slot 0x18. Not executed in recorded runs.
uint32_t ColossusObject_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    fn_00435420(ebp, a0, a1, a2, a3);
    if (VB(ebp, 0xd8))
    {
        D(ebp + 0x5d8) = 0x43c80000u;
        D(ebp + 0x5dc) = 0x43c80000u;
        orig::fn_004534a0(a3, ebp + 0xb98, ebp, 0x005b6140u /* "bullet_scale" */, a2, 0x3f000000u, 1);
        const uint32_t att = ebp + 0x61c;
        orig::fn_004a0ea0(0x005b5fb4u /* "ref_turret" */, ebp, att, 0, 2);
        orig::fn_004a0ef0(0x16, att);
        orig::fn_004a10e0(att, 0);
        orig::fn_00454750(D(World() + 0xf1c), ebp);
        const uint32_t w = orig::fn_004a15f0(att);
        if (w)
            PlaceTurret(ebp, w);
        D(ebp + 0x5c4) = 0x43e10000u;
        D(ebp + 0x860) = 0x43b40000u;
    }
    return VC(ebp, 0xf8, 0x20000u, 0u);
}

static uint32_t ColossusObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return ColossusObject_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00436390(ColossusObject_Slot6_Entry, ColossusObject_Slot6_Abi)
HOOK(0x00436390, "ColossusObject_Slot6", ColossusObject_Slot6_Entry, nullptr);

// 0x004364b0 Q112Boss_Slot6 (ecx = Q112 boss; stack: 4 load arguments; ret 0x10). Init: +0x600 = 25.0, +0x5fc = 35.0,
// the boss init 0x00435420; when virtual +0xd8 says so "bullet_scale" (0.5) into +0xb98, the chaingun attachment at
// "ref_chaingun" (type 0x13, 0x004a0e00 (al = 0)), the weapon placed (PlaceTurret), 0x00428250 (cl = 1), unlinked from
// World+0xf1c, +0x5c4 = 450.0, +0x860 = 45.0 and the animation virtual +0x1a0 (2, 3, 0, 1, 1, 0, 0.5, 0.5). Q112Boss
// vtable 0x005a4098 slot 0x18. Not executed in recorded runs.
uint32_t Q112Boss_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    D(ebp + 0x600) = 0x41c80000u;
    D(ebp + 0x5fc) = 0x420c0000u;
    fn_00435420(ebp, a0, a1, a2, a3);
    if (!VB(ebp, 0xd8))
        return 0;
    orig::fn_004534a0(a3, ebp + 0xb98, ebp, 0x005b6140u /* "bullet_scale" */, a2, 0x3f000000u, 1);
    const uint32_t att = ebp + 0x61c;
    orig::fn_004a0ea0(0x005b6130u /* "ref_chaingun" */, ebp, att, 0, 2);
    orig::fn_004a0ef0(0x13, att);
    orig::fn_004a10e0(att, 0);
    orig::fn_004a0e00(0 /* al = 0 */, att);
    const uint32_t w = orig::fn_004a15f0(att);
    if (w)
        PlaceTurret(ebp, w);
    orig::fn_00428250(ebp, 1 /* cl = 1 */);
    orig::fn_00454750(D(World() + 0xf1c), ebp);
    D(ebp + 0x5c4) = 0x43e10000u;
    D(ebp + 0x860) = 0x42340000u;
    return VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
}

static uint32_t Q112Boss_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Q112Boss_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004364b0(Q112Boss_Slot6_Entry, Q112Boss_Slot6_Abi)
HOOK(0x004364b0, "Q112Boss_Slot6", Q112Boss_Slot6_Entry, nullptr);

// 0x00436600 Q112Boss_Slot52 (ecx = boss; also ColossusObject; stack: damage message or 0; ret 4). Messages with flag
// 0x400 have their damage (+0x14) scaled by "bullet_scale" (+0xb98); then tail jump to SaucerObject_Slot52 0x00435fa0.
// Q112Boss vtable slot 0xd0. Not executed in recorded runs.
uint32_t Q112Boss_Slot52(uint32_t self, uint32_t msg)
{
    if (msg && (D(msg) & 0x400))
        Fl(msg + 0x14) = Fl(self + 0xb98) * Fl(msg + 0x14);
    return SaucerObject_Slot52(self, msg);
}

static uint32_t Q112Boss_Slot52_Abi(uint32_t ecx, uint32_t a0)
{
    return Q112Boss_Slot52(ecx, a0);
}
ABI_ENTRY_00436600(Q112Boss_Slot52_Entry, Q112Boss_Slot52_Abi)
HOOK(0x00436600, "Q112Boss_Slot52", Q112Boss_Slot52_Entry, nullptr);

// 0x00436630 ShrubPatrol_Slot6 (ecx = shrub patrol; stack: 4 load arguments; ret 0x10). Init: Ai_InitGeneric,
// unlinked from World+0xe7c and linked into +0xe88, +0x604 / +0x608 = World+0xe7c / World+0xe88 (the reverse of the
// grunts), unlinked from +0xe8c; when virtual +0xd8 says so 0x00428250 (cl = 1) and the weapon attachment at
// "ref_rhand" (type 0x17); physics on, virtual +0xf4 (0). ShrubPatrol vtable 0x005a7e98 slot 0x18. Not executed in
// recorded runs.
uint32_t ShrubPatrol_Slot6(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t ebp = self;
    orig::fn_004247f0(ebp, a0, a1, a2, a3);
    orig::fn_00454750(D(World() + 0xe7c), ebp);
    orig::fn_004546d0(D(World() + 0xe88), ebp);
    D(ebp + 0x604) = D(World() + 0xe7c);
    D(ebp + 0x608) = D(World() + 0xe88);
    orig::fn_00454750(D(World() + 0xe8c), ebp);
    if (VB(ebp, 0xd8))
    {
        orig::fn_00428250(ebp, 1 /* cl = 1 */);
        const uint32_t att = ebp + 0x61c;
        orig::fn_004a0ea0(0x005b6160u /* "ref_rhand" */, ebp, att, 0, 0);
        orig::fn_004a0ef0(0x17, att);
        orig::fn_004a10e0(att, 0);
    }
    orig::fn_004577b0(ebp, 1);
    return VC(ebp, 0xf4, 0u);
}

static uint32_t ShrubPatrol_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return ShrubPatrol_Slot6(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00436630(ShrubPatrol_Slot6_Entry, ShrubPatrol_Slot6_Abi)
HOOK(0x00436630, "ShrubPatrol_Slot6", ShrubPatrol_Slot6_Entry, nullptr);

// 0x004367a0 ShrubPatrol_Slot18 (stack: out pointer or 0; ret 4; `this` unused): *a0 = 0 when given; returns 1 (AL).
// ShrubPatrol vtable slot 0x48. Not executed in recorded runs.
uint32_t ShrubPatrol_Slot18(uint32_t out)
{
    if (out)
        D(out) = 0;
    return (out & 0xffffff00u) | 1;
}

static uint32_t ShrubPatrol_Slot18_Abi(uint32_t a0)
{
    return ShrubPatrol_Slot18(a0);
}
ABI_ENTRY_004367a0(ShrubPatrol_Slot18_Entry, ShrubPatrol_Slot18_Abi)
HOOK(0x004367a0, "ShrubPatrol_Slot18", ShrubPatrol_Slot18_Entry, nullptr);

// 0x004367c0 ShrubPatrol_Slot14 (ecx = shrub patrol; stack: a0, a1; ret 8). When a1 is the activation trigger +0x52c:
// physics off, virtual +0xf4 (1), +0x488 |= 0x80; then the base method 0x0042f870 (a0, a1), after which +0x52c is
// cleared in that case. ShrubPatrol vtable slot 0x38.
uint32_t ShrubPatrol_Slot14(uint32_t self, uint32_t a0, uint32_t a1)
{
    const uint32_t esi = self;
    bool bl = false;
    if (a1 == D(esi + 0x52c))
    {
        orig::fn_004577b0(esi, 0);
        VC(esi, 0xf4, 1u);
        D(esi + 0x488) |= 0x80;
        bl = true;
    }
    const uint32_t r = orig::fn_0042f870(esi, a0, a1);
    if (bl)
        D(esi + 0x52c) = 0;
    return r;
}

static uint32_t ShrubPatrol_Slot14_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return ShrubPatrol_Slot14(ecx, a0, a1);
}
ABI_ENTRY_004367c0(ShrubPatrol_Slot14_Entry, ShrubPatrol_Slot14_Abi)
HOOK(0x004367c0, "ShrubPatrol_Slot14", ShrubPatrol_Slot14_Entry, nullptr);
