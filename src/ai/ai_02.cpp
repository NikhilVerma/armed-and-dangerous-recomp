// Recovered: batch ai_02, generic AI object methods 0x00425e20..0x0042a660 (vtable CLASS(GenericAIObject) 0x005b2958
// and the AI classes that share its slots). Static recovery 2026-10-05, compile-checked only (the game was not run);
// the oracle (tools/compare.py) is still to do.
//
// Conventions follow src/ai/ai_06.cpp: D/I/Fl/By are memory at an absolute address, VC/VB are virtual calls through the
// object's vtable at a byte offset (callee pops), floats follow the disassembly's operation order in single precision.
// Virtual slots of the GenericAIObject vtable used here (callee pop counts read from the image):
//   +0x19c 0x004262d0 (6 args, recovered here), +0x1a0 0x00426480 (8 args, here), +0x1a4 0x0043a9e0 (1 arg, returns
//   the animation name), +0x1a8 0x0043ab30 (1 arg), +0x1b8 0x004262c0 (1 arg, here), +0xdc 0x00432730 (no args),
//   +0xd8 0x00454770 (no args, "kept" test).
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
// Memory at an absolute address (object fields: D(self + 0x460)).
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
// The "AI : ..." log lines: Debug_Printf 0x00407ff0 (game+0x34, format, tag, object name = [[self+0x414]+0x10]+4).
inline void AiLog(uint32_t self, uint32_t format, uint32_t tag)
{
    uint32_t name = D(D(self + 0x414) + 0x10) + 4;
    orig::fn_00407ff0(D(GameP() + 0x34), format, tag, name);
}
}  // namespace

uint32_t fn_00429a40(uint32_t eax, uint32_t self);

// ---------------------------------------------------------------------------------------------------------------------
// 0x00425e20 (ecx = AI object; returns eax). Vtable slot +0x20 of GenericAIObject and 16 other AI classes; direct caller
// 0x00434da0. Resolves the start objects named by tags: a random marker spline group tag in [+0x558, +0x55c] when
// +0x484 has 0x10000 (0x0044c030, then its virtual +0x5c fills a 0x44-byte matrix that 0x00457990 applies); with
// +0x488 & 0x200080 the start window/tower object (game+0xa84 list +0xe70, tag +0x534) into +0x880; with 0x10000000 the
// bully target (list +0xef8) into +0x88c ("peasant_man_stand" via 0x00408e30, state 0x118); with 0x40000000 the start
// turret (list +0xea8) into +0x898 / +0xae0 (state 0x7d). Missing objects log "AI : Can't find ..." and kill the AI
// (virtual +0xdc). Name from these strings (guess at the role, the strings are evidence of what it resolves).
uint32_t AI_ResolveStartObjects(uint32_t esi)
{
    uint32_t m[17];  // [esp+0x14], 0x44 bytes: the game matrix
    uint32_t ebp = D(esi + 0x558);
    if (ebp != 0)
    {
        uint32_t ebx = D(esi + 0x55c);
        uint32_t r = Rand();
        ebx = ebx - ebp + 1;
        uint32_t edi = static_cast<uint32_t>(static_cast<int32_t>(r * ebx) >> 15) + ebp;
        if (D(esi + 0x484) & 0x10000)
        {
            uint32_t group = orig::fn_0044c030(0, edi);
            if (group == 0)
            {
                AiLog(esi, 0x005b6a74u, edi);  // "AI : Can't find marker spline group..."
                return VC(esi, 0xdc);
            }
            D(esi + 0x558) = edi;
            D(esi + 0x55c) = edi;
            uint32_t t = VC(group, 0x5c, u(m));
            orig::fn_00457990(t & 0xffffff00u, esi, u(m));  // xor al, al
        }
    }
    uint32_t eax = D(esi + 0x488);
    if (eax & 0x200080)
    {
        uint32_t list = D(D(GameP() + 0xa84) + 0xe70);
        uint32_t obj = orig::fn_004592e0(list, D(esi + 0x534), esi);
        D(esi + 0x880) = obj;
        if (obj == 0)
        {
            AiLog(esi, 0x005b6a30u, D(esi + 0x534));  // "AI : Can't find start window/tower object..."
            uint32_t r = VC(esi, 0xdc);
            D(esi + 0x534) = 0;
            return r;
        }
        I(obj + 0x28) += 1;
        orig::fn_00453c50(D(esi + 0x880), esi + 0x4cc, u(m), 1, 0, 0, 1, 0, 0);
        orig::fn_00457990(u(m) & 0xffffff00u, esi, u(m));  // eax = &m with al cleared
        if (D(esi + 0x488) & 0x200000)
        {
            uint32_t o = D(esi + 0x880);
            D(esi + 0x904) = o;
            I(o + 0x28) += 1;
            uint32_t r = VC(D(esi + 0x904), 0x158, esi, 0u);
            D(esi + 0x914) = 0;
            D(esi + 0x534) = 0;
            return r;
        }
        uint32_t r = static_cast<uint32_t>(orig::fn_004577b0(esi, 1));
        D(esi + 0x534) = 0;
        return r;
    }
    if (eax & 0x10000000)
    {
        uint32_t list = D(D(GameP() + 0xa84) + 0xef8);
        uint32_t obj = orig::fn_004592e0(list, D(esi + 0x534), esi);
        D(esi + 0x88c) = obj;
        if (obj != 0)
        {
            int32_t r = static_cast<int32_t>(Rand());
            Fl(esi + 0x494) = static_cast<float>(r) * K(0x005bce34) + K(0x005bcb68);
            I(D(esi + 0x88c) + 0x28) += 1;
            VC(esi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
            orig::fn_00408e30(0x81, D(esi + 0x88c), 0xffffffffu, 0x005bbea4u /* "peasant_man_stand" */, 0, 0, 0, 0);
            uint32_t rr = VC(esi, 0x1b8, 0x118u);
            D(esi + 0x534) = 0;
            return rr;
        }
        AiLog(esi, 0x005b69e8u, D(esi + 0x534));  // "AI : Can't find bully target..."
        VC(esi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
        uint32_t rr = VC(esi, 0x1b8, 1u);
        D(esi + 0x534) = 0;
        return rr;
    }
    if (!(eax & 0x40000000) || (eax & 0x30) || D(esi + 0x534) == 0)
        return eax;
    uint32_t list = D(D(GameP() + 0xa84) + 0xea8);
    uint32_t turret = orig::fn_004592e0(list, D(esi + 0x534), esi);
    D(esi + 0x898) = turret;
    if (turret == 0)
    {
        AiLog(esi, 0x005b69b0u, D(esi + 0x534));  // "AI : Can't find start turret..."
        uint32_t r = VC(esi, 0xdc);
        D(esi + 0x534) = 0;
        return r;
    }
    if (VB(turret, 0x158, 1u))
    {
        I(D(esi + 0x898) + 0x28) += 1;
        VC(D(esi + 0x898), 0x160, esi);
        uint32_t t = orig::fn_004324d0(0, esi, D(esi + 0x898), 1);
        fn_00429a40(t & 0xffffff00u, esi);  // xor al, al
        uint32_t f = D(esi + 0x484) | 0x8000;
        D(esi + 0x484) = f;
        D(esi + 0x484) = f | 0x200;
        orig::fn_004539f0(0, D(esi + 0x898), esi, 0, 1, 4);
        VC(esi, 0x19c, 0x69u, 1u, 0u, 0u, 0u, 0u);
        uint32_t slot = esi + 0xae0;
        uint32_t old = D(slot);
        if (old)
        {
            Unref(old);
            D(slot) = 0;
        }
        uint32_t tt = D(esi + 0x898);
        D(slot) = tt;
        I(tt + 0x28) += 1;
        uint32_t r = VC(esi, 0x1b8, 0x7du);
        D(esi + 0x534) = 0;
        return r;
    }
    AiLog(esi, 0x005b6968u, D(esi + 0x534));  // "AI : Can't start in turret (turret occupied)..."
    D(esi + 0x898) = 0;
    uint32_t r = VC(esi, 0xdc);
    D(esi + 0x534) = 0;
    return r;
}

static uint32_t AI_ResolveStartObjects_Abi(uint32_t ecx)
{
    return AI_ResolveStartObjects(ecx);
}
ABI_ENTRY_00425e20(AI_ResolveStartObjects_Entry, AI_ResolveStartObjects_Abi)
HOOK(0x00425e20, "AI_ResolveStartObjects", AI_ResolveStartObjects_Entry, nullptr);

// 0x004262c0 (ecx = AI object; stack: state; ret 4). Vtable slot +0x1b8: stores the AI state at +0x460 (the state
// GenericAI_Update 0x0043ee90 switches on). Returns the state (eax).
uint32_t GenericAI_SetState(uint32_t self, uint32_t state)
{
    D(self + 0x460) = state;
    return state;
}

static uint32_t GenericAI_SetState_Abi(uint32_t ecx, uint32_t a0)
{
    return GenericAI_SetState(ecx, a0);
}
ABI_ENTRY_004262c0(GenericAI_SetState_Entry, GenericAI_SetState_Abi)
HOOK(0x004262c0, "GenericAI_SetState", GenericAI_SetState_Entry, nullptr);

// 0x004262d0 (ecx = AI object; stack: anim id, loop, scale, use Anim_Set, random start, flag 0x10; ret 0x18). Vtable
// slot +0x19c (not executed in recorded runs). Plays one animation by id: the name comes from virtual +0x1a4(id), a
// vec3 from virtual +0x1a8(id) (scaled by 0x005bcb5c when `scale`); flags 0x80/0x81 (+0x40, +0x10); blend 0.25 unless
// this is the first animation (flag 0x4000000 of +0x484). Anim_Set 0x00408d90 or 0x00408e30 (random start time with
// flags 0xa00). Stores the id in the four slots +0x498..+0x4a4 and, unless id is 0x23, releases +0x854 after its
// virtual +0xdc. Name: guess from the Anim_Set call.
uint32_t GenericAI_PlayAnim(uint32_t esi, uint32_t id, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t blend;  // [esp+0x10]
    uint32_t ecx = D(esi + 0x484);
    if (!(ecx & 0x4000000))
    {
        D(esi + 0x484) = ecx | 0x4000000;
        blend = 0;
    }
    else
    {
        blend = 0x3e800000;  // 0.25
    }
    if (D(esi + 0x484) & 0x800000)
    {
        D(esi + 0x580) = 0x3f000000;
        D(esi + 0x584) = 0x3f000000;
        D(esi + 0x484) &= 0xff7fffffu;
    }
    uint32_t name = VC(esi, 0x1a4, id);  // stored over the id's stack slot
    uint32_t edi = VC(esi, 0x1a8, id);
    uint32_t ebx = static_cast<uint8_t>(a1) ? 0x81u : 0x80u;
    if (static_cast<uint8_t>(a2))
    {
        ebx |= 0x40;
        if (edi)
        {
            Fl(edi) = Fl(edi) * K(0x005bcb5c);
            Fl(edi + 4) = Fl(edi + 4) * K(0x005bcb5c);
            Fl(edi + 8) = Fl(edi + 8) * K(0x005bcb5c);
        }
    }
    if (static_cast<uint8_t>(a5))
        ebx |= 0x10;
    uint32_t eax;
    if (static_cast<uint8_t>(a3))
    {
        eax = orig::fn_00408d90(0xffffffffu, esi, name, ebx, edi, blend, 0, 0);
    }
    else if (static_cast<uint8_t>(a4))
    {
        int32_t r = static_cast<int32_t>(Rand());
        float start = static_cast<float>(r) * K(0x005bcbc0);
        eax = orig::fn_00408e30((ebx & 0xffffff7fu) | 0xa00, esi, 0xffffffffu, name, edi, blend, fb(start), 0);
    }
    else
    {
        eax = orig::fn_00408e30(ebx, esi, 0xffffffffu, name, edi, blend, 0, 0);
    }
    uint32_t obj = D(esi + 0x854);
    D(esi + 0x498) = id;
    D(esi + 0x49c) = id;
    D(esi + 0x4a0) = id;
    D(esi + 0x4a4) = id;
    if (obj != 0 && id != 0x23)
    {
        VC(obj, 0xdc);
        Unref(D(esi + 0x854));  // no null test in the original
        D(esi + 0x854) = 0;
    }
    return eax;
}

static uint32_t GenericAI_PlayAnim_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                       uint32_t a5)
{
    return GenericAI_PlayAnim(ecx, a0, a1, a2, a3, a4, a5);
}
ABI_ENTRY_004262d0(GenericAI_PlayAnim_Entry, GenericAI_PlayAnim_Abi)
HOOK(0x004262d0, "GenericAI_PlayAnim", GenericAI_PlayAnim_Entry, nullptr);

// 0x00426480 (ecx = AI object; stack: 4 anim ids, two flag bytes, two floats; ret 0x20). Vtable slot +0x1a0 (not
// executed in recorded runs). The four-animation variant of +0x19c: unless the current id +0x498 is 5..7 and id0 is
// 2, sets flag 0x800000 of +0x484, resolves the four names through virtual +0x1a4 (order id2, id3, id0, id1) and calls
// 0x00408c70 (eax = flags 0x80 / 0x20000 when id0 is already playing, |1, |0x40; edx = name0; ebx = 0; ebp = id3;
// stack: name1, name2, name3, f0, f1, blend 0.25 / 0). Stores the ids at +0x498..+0x4a4. Returns id0 (or the old id).
// Name: guess (same family as GenericAI_PlayAnim).
uint32_t GenericAI_PlayAnim4(uint32_t esi, uint32_t id0, uint32_t id1, uint32_t id2, uint32_t id3, uint32_t a4,
                             uint32_t a5, uint32_t f0, uint32_t f1)
{
    uint32_t cur = D(esi + 0x498);
    if (cur >= 5 && cur <= 7 && id0 == 2)
        return cur;
    D(esi + 0x484) |= 0x800000;
    uint32_t flags;  // [esp+4]
    uint32_t blend;  // [esp+8]
    if (D(esi + 0x498) != id0)
    {
        flags = 0x80;
        blend = 0x3e800000;
    }
    else
    {
        flags = 0x20000;
        blend = 0;
    }
    uint32_t ecx = D(esi + 0x484);
    if (!(ecx & 0x4000000))
    {
        D(esi + 0x484) = ecx | 0x4000000;
        blend = 0;
    }
    if (static_cast<uint8_t>(a4))
        flags |= 1;
    if (static_cast<uint8_t>(a5))
        flags |= 0x40;
    uint32_t name2 = VC(esi, 0x1a4, id2);
    uint32_t name3 = VC(esi, 0x1a4, id3);
    uint32_t name0 = VC(esi, 0x1a4, id0);
    uint32_t name1 = VC(esi, 0x1a4, id1);
    orig::fn_00408c70(flags, name0, 0, esi, id3, name1, name2, name3, f0, f1, blend);
    D(esi + 0x4a0) = id2;
    D(esi + 0x4a4) = id3;
    D(esi + 0x498) = id0;
    D(esi + 0x49c) = id1;
    return id0;
}

static uint32_t GenericAI_PlayAnim4_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                        uint32_t a5, uint32_t a6, uint32_t a7)
{
    return GenericAI_PlayAnim4(ecx, a0, a1, a2, a3, a4, a5, a6, a7);
}
ABI_ENTRY_00426480(GenericAI_PlayAnim4_Entry, GenericAI_PlayAnim4_Abi)
HOOK(0x00426480, "GenericAI_PlayAnim4", GenericAI_PlayAnim4_Entry, nullptr);

// 0x004265c0 (ecx = AI object; returns eax 0/1). Caller 0x00485910 (not executed in recorded runs). True when the state
// +0x460 is 1, or 0x79 / 0x7a with +0x588 == 0.
uint32_t fn_004265c0(uint32_t self)
{
    uint32_t s = D(self + 0x460);
    if (s == 1)
        return 1;
    if ((s == 0x79 || s == 0x7a) && D(self + 0x588) == 0)
        return 1;
    return 0;
}

static uint32_t fn_004265c0_Abi(uint32_t ecx)
{
    return fn_004265c0(ecx);
}
ABI_ENTRY_004265c0(fn_004265c0_Entry, fn_004265c0_Abi)
HOOK(0x004265c0, "fn_004265c0", fn_004265c0_Entry, nullptr);

// 0x004265f0 (ecx = AI object; returns eax 0/1). Caller 0x00485910 (not executed in recorded runs). State == 0x50.
uint32_t fn_004265f0(uint32_t self)
{
    return D(self + 0x460) == 0x50 ? 1u : 0u;
}

static uint32_t fn_004265f0_Abi(uint32_t ecx)
{
    return fn_004265f0(ecx);
}
ABI_ENTRY_004265f0(fn_004265f0_Entry, fn_004265f0_Abi)
HOOK(0x004265f0, "fn_004265f0", fn_004265f0_Entry, nullptr);

// 0x00426600 (eax = AI object; returns al). State == 0x8c (sete al: the upper bytes of eax keep the object pointer).
uint32_t fn_00426600(uint32_t self)
{
    return (self & 0xffffff00u) | (D(self + 0x460) == 0x8c ? 1u : 0u);
}

static uint32_t fn_00426600_Abi(uint32_t eax)
{
    return fn_00426600(eax);
}
ABI_ENTRY_00426600(fn_00426600_Entry, fn_00426600_Abi)
HOOK(0x00426600, "fn_00426600", fn_00426600_Entry, nullptr);

// 0x00426610 (eax = AI object; returns al). State == 0x8c and flag 0x8000000 of +0x488 (upper eax bytes kept).
uint32_t fn_00426610(uint32_t self)
{
    bool r = D(self + 0x460) == 0x8c && (D(self + 0x488) & 0x8000000) != 0;
    return (self & 0xffffff00u) | (r ? 1u : 0u);
}

static uint32_t fn_00426610_Abi(uint32_t eax)
{
    return fn_00426610(eax);
}
ABI_ENTRY_00426610(fn_00426610_Entry, fn_00426610_Abi)
HOOK(0x00426610, "fn_00426610", fn_00426610_Entry, nullptr);

// 0x00426630 (eax = AI object; returns eax). Bit 0 of +0xa18.
uint32_t fn_00426630(uint32_t self)
{
    return D(self + 0xa18) & 1;
}

static uint32_t fn_00426630_Abi(uint32_t eax)
{
    return fn_00426630(eax);
}
ABI_ENTRY_00426630(fn_00426630_Entry, fn_00426630_Abi)
HOOK(0x00426630, "fn_00426630", fn_00426630_Entry, nullptr);

// 0x00426640 (ebx = new object, esi = AI object). Replaces the reference at +0x570: when flag 0x80000 of +0x484 is set
// the old object is first passed to 0x0044b7f0 (eax = object, cl = 0) and the new one afterwards (cl = 1, a tail jump
// in the original). The old reference is dropped without clearing (the slot is overwritten).
uint32_t fn_00426640(uint32_t ebx, uint32_t esi)
{
    uint32_t eax = D(esi + 0x570);
    if (eax != 0)
    {
        if (D(esi + 0x484) & 0x80000)
            orig::fn_0044b7f0(eax, 0);  // xor cl, cl (upper ecx bytes: the caller's, unknown)
        Unref(D(esi + 0x570));
    }
    D(esi + 0x570) = ebx;
    I(ebx + 0x28) += 1;
    if (D(esi + 0x484) & 0x80000)
        return orig::fn_0044b7f0(D(esi + 0x570), 1);
    return eax;
}

static uint32_t fn_00426640_Abi(uint32_t ebx, uint32_t esi)
{
    return fn_00426640(ebx, esi);
}
ABI_ENTRY_00426640(fn_00426640_Entry, fn_00426640_Abi)
HOOK(0x00426640, "fn_00426640", fn_00426640_Entry, nullptr);

// 0x004267a0 (eax = AI object; returns al). Bit 8 of +0x484 (shr eax, 8; and al, 1: the upper bytes keep the shifted
// flags).
uint32_t fn_004267a0(uint32_t self)
{
    uint32_t v = D(self + 0x484) >> 8;
    return (v & 0xffffff00u) | (v & 1);
}

static uint32_t fn_004267a0_Abi(uint32_t eax)
{
    return fn_004267a0(eax);
}
ABI_ENTRY_004267a0(fn_004267a0_Entry, fn_004267a0_Abi)
HOOK(0x004267a0, "fn_004267a0", fn_004267a0_Entry, nullptr);

// 0x004267b0 (ecx = AI object; stack: byte enable, a1; ret 8). Vtable slot +0xfc (not executed in recorded runs).
// Unless 0x0044e600 is true: when bit 8 of +0x484 is set (0x004267a0) and `enable`, first calls virtual +0xfc(0, a1)
// on itself; reads the position (virtual +0x54 into a 12-byte local). Enable: sets bit 8, 0x0046f320 (esi = game+0xaa4;
// x, y, &+0xa10, &+0xa14) finds a cell; if 0x0046f2e0 accepts it, sets bit 7 and calls 0x0046ece0(cell, a1, 1).
// Disable: clears bit 8, and if bit 7 was set clears it and calls 0x0046ece0(cell, a1, 0). Guess: registers the AI in
// a grid of game+0xaa4.
uint32_t fn_004267b0(uint32_t edi, uint32_t a0, uint32_t a1)
{
    uint32_t pos[3];  // [esp+0x10]
    uint32_t r = orig::fn_0044e600();
    if (static_cast<uint8_t>(r))
        return r;
    uint8_t bl = static_cast<uint8_t>(a0);
    if (static_cast<uint8_t>(fn_004267a0(edi)) && bl)
        VC(edi, 0xfc, 0u, a1);
    VC(edi, 0x54, u(pos));
    uint32_t game = GameP();
    uint32_t eax, ecx, flag;
    if (bl)
    {
        uint32_t esi = D(edi + 0x484) | 0x100;
        D(edi + 0x484) = esi;
        r = orig::fn_0046f320(D(game + 0xaa4), pos[0], pos[1], edi + 0xa10, edi + 0xa14);
        if (!static_cast<uint8_t>(r))
            return r;
        game = GameP();
        r = orig::fn_0046f2e0(D(game + 0xaa4), D(edi + 0xa10), D(edi + 0xa14));
        if (!static_cast<uint8_t>(r))
        {
            uint32_t f = D(edi + 0x484) & 0xffffff7fu;
            D(edi + 0x484) = f;
            return f;
        }
        D(edi + 0x484) |= 0x80;
        eax = D(edi + 0xa14);
        ecx = D(edi + 0xa10);
        flag = 1;
    }
    else
    {
        ecx = D(edi + 0x484) & 0xfffffeffu;
        D(edi + 0x484) = ecx;
        if (!(ecx & 0x80))
            return ecx & 0xff;
        game = GameP();
        D(edi + 0x484) = ecx & 0xffffff7fu;
        eax = D(edi + 0xa14);
        ecx = D(edi + 0xa10);
        flag = 0;
    }
    return orig::fn_0046ece0(eax, ecx, a1, D(game + 0xaa4), flag);
}

static uint32_t fn_004267b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004267b0(ecx, a0, a1);
}
ABI_ENTRY_004267b0(fn_004267b0_Entry, fn_004267b0_Abi)
HOOK(0x004267b0, "fn_004267b0", fn_004267b0_Entry, nullptr);

// 0x004268e0 (esi = object; stack: 1 argument, not read; ret 4; returns al). Caller GenericAI_Update 0x0043ee90 (not
// executed in recorded runs). True when the object exists, virtual +0xd8 is true and +0x454 / +0x458 (0x004555b0 /
// 0x004555c0, both a single fld) is not below 0x005bcbe4 (fcomp; test ah, 5; jp).
uint32_t fn_004268e0(uint32_t esi, uint32_t /*a0*/)
{
    if (esi == 0 || !VB(esi, 0xd8))
        return 0;
    float ratio = Fl(esi + 0x454) / Fl(esi + 0x458);
    if (!(ratio < K(0x005bcbe4)))
        return 1;
    return 0;
}

static uint32_t fn_004268e0_Abi(uint32_t esi, uint32_t a0)
{
    return fn_004268e0(esi, a0);
}
ABI_ENTRY_004268e0(fn_004268e0_Entry, fn_004268e0_Abi)
HOOK(0x004268e0, "fn_004268e0", fn_004268e0_Entry, nullptr);

// 0x00426920 (ebx = AI object; stack: float range, a1 = object passed to 0x00454750 / 0x004546d0, byte a2 = skip the
// first list; ret 0xc; returns eax = the chosen target or 0). Callers 0x0042be80, 0x0042ec20, 0x00435b50 and
// GenericAI_Update. Counts down the timer +0x490 by the frame time 0x00604a90 and searches only when it is <= 0:
//  1. unless flag 0x4000 of +0x488, walks the window/tower tags [+0x858, +0x85c] (game+0xa84 list +0xe70, 0x004592e0):
//     the first one that is not 0x00454780 is returned at once with its 2D distance in +0x5dc; a missing tag
//     advances +0x858 and past the end resets both and sets +0x5dc = +0x5d8 * 0x005bce20;
//  2. unless a2, walks game+0xa84 list +0xee0 (0x004e7340 / 0x004e73c0 iterator in a local): the entries 0x00404750
//     yields whose virtual +0xe4 (side, guess) differs from the AI's (equal with flag 0x4000) and lie within the
//     entry's radius in 3D, are kept when virtual +0xf180(object, 2 * +0x5dc) accepts them and they are the closest
//     in 2D (squared, against (range + 0x005bcb2c)^2);
//  3. walks the AI's own list +0x604 the same way with the tag filters game+0xae8 / +0x534, virtual +0x180, range,
//     the player (game+0x748) inside +0x5e0 returning at once, the facing tests virtual +0xf0 (180 / 190 degrees,
//     guess), the turret check 0x0042b860 (flag 0x8000) and the line of sight 0x00426fd0.
// Then re-arms +0x490 with a random delay (two ranges depending on 0x0044e600). The candidate positions come from
// 0x00428280 ("ref_hit_00"). Name: guess from this behaviour.
uint32_t fn_00428280(uint32_t esi, uint32_t edi, uint32_t object, uint32_t name);
uint32_t fn_00426fd0(uint32_t eax, uint32_t ecx, uint32_t target, uint32_t exclude);

uint32_t AI_FindTarget(uint32_t ebx, uint32_t range, uint32_t a1, uint32_t a2)
{
    uint32_t fr[0x12];  // sub esp, 0x48: fr[0] is [esp+0xc] with no pushes pending
    auto SP = [&](uint32_t o) { return u(fr) + o - 0xc; };
    uint32_t game;
    float t = Fl(ebx + 0x490) - Fl(0x00604a90);
    Fl(ebx + 0x490) = t;
    D(SP(0x1c)) = 0;  // iterator: list
    D(SP(0x20)) = 0;  // iterator: node
    D(SP(0x10)) = 0;  // best target
    if (t > K(0x0056c05c) || t != t)
        return 0;
    if (!(D(ebx + 0x488) & 0x4000) && D(ebx + 0x858) != 0)
    {
        VC(ebx, 0x54, SP(0x24));
        int32_t edi = I(ebx + 0x858);
        if (edi <= I(ebx + 0x85c))
        {
            do
            {
                game = GameP();
                uint32_t esi = orig::fn_004592e0(D(D(game + 0xa84) + 0xe70), static_cast<uint32_t>(edi), ebx);
                if (esi != 0)
                {
                    if (!static_cast<uint8_t>(orig::fn_00454780(esi)))
                    {
                        VC(esi, 0x54, SP(0x30));
                        float dy = Fl(SP(0x34)) - Fl(SP(0x28));
                        float dx = Fl(SP(0x30)) - Fl(SP(0x24));
                        Fl(ebx + 0x5dc) = std::sqrt(dx * dx + dy * dy);
                        return esi;
                    }
                }
                else
                {
                    int32_t n = I(ebx + 0x858) + 1;
                    I(ebx + 0x858) = n;
                    if (n > I(ebx + 0x85c))
                    {
                        float v = Fl(ebx + 0x5d8);
                        D(ebx + 0x858) = 0;
                        D(ebx + 0x85c) = 0;
                        Fl(ebx + 0x5dc) = v * K(0x005bce20);
                    }
                }
                ++edi;
            } while (edi <= I(ebx + 0x85c));
        }
    }
    game = GameP();
    if ((D(game + 0xae8) != 0 || (By(ebx + 0x488) & 4)) && VB(ebx, 0xe8))
        range = 0x4cbebc20;  // 1e8
    {
        float r = bf(range) + K(0x005bcb2c);
        Fl(SP(0x0c)) = r * r;  // best distance (squared)
    }
    fn_00428280(SP(0x24), ebx, ebx, 0x005b68fcu);  // own position, "ref_hit_00"
    if (!static_cast<uint8_t>(a2))
    {
        game = GameP();
        uint32_t ecx = orig::fn_004e7340(D(D(game + 0xa84) + 0xee0), SP(0x1c));
        while (ecx != 0)
        {
            orig::fn_00404750(SP(0x30), ecx, SP(0x18), SP(0x14));
            uint32_t esi = D(SP(0x18));
            if (esi != ebx)
            {
                uint8_t mine = static_cast<uint8_t>(VC(esi, 0xe4));
                uint8_t its = static_cast<uint8_t>(VC(ebx, 0xe4));
                bool skip = (D(ebx + 0x488) & 0x4000) ? (mine != its) : (mine == its);
                if (!skip)
                {
                    float dz = Fl(SP(0x2c)) - Fl(SP(0x38));
                    float dy = Fl(SP(0x28)) - Fl(SP(0x34));
                    float dx = Fl(SP(0x24)) - Fl(SP(0x30));
                    float d3 = dx * dx + dy * dy + dz * dz;
                    float rad = Fl(SP(0x14)) * Fl(SP(0x14));
                    if (!(rad < d3 || rad != rad || d3 != d3))
                    {
                        VC(esi, 0x54, SP(0x30));
                        float ey = Fl(SP(0x28)) - Fl(SP(0x34));
                        float ex = Fl(SP(0x24)) - Fl(SP(0x30));
                        float d2 = ex * ex + ey * ey;
                        float lim = Fl(ebx + 0x5dc) + Fl(ebx + 0x5dc);
                        if (VB(ebx, 0x180, esi, fb(lim)))
                        {
                            if (d2 < Fl(SP(0x0c)) || D(SP(0x10)) == 0)
                            {
                                Fl(SP(0x0c)) = d2;
                                D(SP(0x10)) = esi;
                            }
                        }
                    }
                }
            }
            ecx = orig::fn_004e73c0(SP(0x1c));
        }
    }
    if (a1 != 0)
    {
        game = GameP();
        orig::fn_00454750(D(D(game + 0xa84) + 0xe9c), a1);
    }
    uint32_t ebp = orig::fn_004e7340(D(ebx + 0x604), SP(0x1c));
    while (ebp != 0)
    {
        if (ebp == ebx)
            goto next;
        {
            uint32_t tag = 0;  // [esp+0x60] (the a2 slot)
            orig::fn_00453290(ebp, u(&tag));
            game = GameP();
            uint32_t g = D(game + 0xae8);
            bool test = true;
            if (g == 0 || tag == g)
            {
                uint32_t own = D(ebx + 0x534);
                if (own == 0 || tag == own)
                    test = false;
            }
            if (test && VB(ebx, 0xe8))
                goto next;
            float lim = Fl(ebx + 0x5dc) + Fl(ebx + 0x5dc);
            if (!VB(ebx, 0x180, ebp, fb(lim)))
                goto next;
            fn_00428280(SP(0x30), ebx, ebp, 0x005b68fcu);
            float dy = Fl(SP(0x34)) - Fl(SP(0x28));
            float dx = Fl(SP(0x30)) - Fl(SP(0x24));
            float d = dx * dx + dy * dy;  // [esp+0x60]
            float rr = bf(range) * bf(range);
            if (rr < d)
                goto next;
            if (d > Fl(SP(0x0c)))
                goto next;
            game = GameP();
            if (ebp == D(game + 0x748))
            {
                float q = Fl(ebx + 0x5e0);
                if (d < q * q)
                {
                    // the player is inside the close range: take it at once
                    if (a1 != 0)
                        orig::fn_004546d0(D(D(game + 0xa84) + 0xe9c), a1);
                    uint32_t node = D(SP(0x20));
                    VC(D(SP(0x1c)), 0xc, node);
                    D(node + 0x18) -= 1;
                    return ebp;
                }
            }
            if (D(game + 0xae8) == 0 && !(By(ebx + 0x488) & 4))
            {
                float q = Fl(ebx + 0x5e0);
                bool ok;
                if (q == K(0x0056c05c))
                {
                    ok = VB(ebx, 0xf0, ebp, 0x43340000u);  // 180.0
                    if (!ok)
                        ok = !(d > K(0x005bcc1c));
                }
                else if (D(ebx + 0x484) & 0x20000)
                {
                    ok = true;
                }
                else
                {
                    ok = VB(ebx, 0xf0, ebp, 0x433e0000u);  // 190.0
                    if (!ok)
                    {
                        float q2 = Fl(ebx + 0x5e0);
                        ok = !(d > q2 * q2);
                    }
                }
                if (!ok)
                    goto next;
                if (D(ebx + 0x484) & 0x8000)
                {
                    orig::fn_00453150(D(ebx + 0x898), SP(0x48));
                    VC(D(ebx + 0x898), 0x54, SP(0x3c));
                    if (!static_cast<uint8_t>(orig::fn_0042b860(ebp, SP(0x48), SP(0x3c), ebx, 0x42820000u)))
                        goto next;
                }
                uint32_t e = orig::fn_00454c60(ebp);
                if (!static_cast<uint8_t>(fn_00426fd0(SP(0x24), ebx, ebp, e)))
                    goto next;
            }
            Fl(SP(0x0c)) = d;
            D(SP(0x10)) = ebp;
        }
    next:
        ebp = orig::fn_004e73c0(SP(0x1c));
    }
    {
        float f;
        if (static_cast<uint8_t>(orig::fn_0044e600()))
            f = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bce30) + K(0x005bcb80);
        else
            f = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bce3c) + K(0x005bc15c);
        Fl(ebx + 0x490) = f;
    }
    if (a1 != 0)
    {
        game = GameP();
        orig::fn_004546d0(D(D(game + 0xa84) + 0xe9c), a1);
    }
    uint32_t node = D(SP(0x20));
    if (node != 0)
        D(node + 0x18) -= 1;
    return D(SP(0x10));
}

static uint32_t AI_FindTarget_Abi(uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return AI_FindTarget(ebx, a0, a1, a2);
}
ABI_ENTRY_00426920(AI_FindTarget_Entry, AI_FindTarget_Abi)
HOOK(0x00426920, "AI_FindTarget", AI_FindTarget_Entry, nullptr);

// 0x00426ee0 (eax = source vec3, edi = AI object; stack: target object, a1; ret 8; returns al). Caller 0x0042a7b0 (not
// executed in recorded runs). Line-of-sight test: the target's "ref_hit_00" point (0x00428280), the direction from
// the source normalised (0x005bcb2c / length, skipped for length 0) and Ray_CastQuery 0x00414110 against game+0xa8c
// with the length; the length is stored over the target argument's stack slot, whose address is passed too (as is
// the address of a1's slot). Returns true when the ray hits (guess at the meaning).
uint32_t fn_00426ee0(uint32_t eax, uint32_t edi, uint32_t target, uint32_t a1)
{
    uint32_t fr[9];  // sub esp, 0x24: fr[0] is [esp+4] with esi pushed
    auto F = [&](uint32_t o) { return u(fr) + o; };
    uint32_t slot0 = target;  // [esp+0x2c]
    uint32_t slot1 = a1;      // [esp+0x30]
    D(F(0xc)) = D(eax);
    D(F(0x10)) = D(eax + 4);
    D(F(0x14)) = D(eax + 8);
    fn_00428280(F(0x18), edi, slot0, 0x005b68fcu);
    Fl(F(0x0)) = Fl(F(0x18)) - Fl(F(0xc));
    Fl(F(0x4)) = Fl(F(0x1c)) - Fl(F(0x10));
    float dz = Fl(F(0x20)) - Fl(F(0x14));
    Fl(F(0x8)) = dz;
    float len = std::sqrt(dz * Fl(F(0x8)) + Fl(F(0x4)) * Fl(F(0x4)) + Fl(F(0x0)) * Fl(F(0x0)));
    Fl(u(&slot0)) = len;
    if (len != K(0x0056c05c))
    {
        float inv = K(0x005bcb2c) / Fl(u(&slot0));
        Fl(F(0x0)) = Fl(F(0x0)) * inv;
        Fl(F(0x4)) = Fl(F(0x4)) * inv;
        Fl(F(0x8)) = Fl(F(0x8)) * inv;
    }
    uint32_t r = orig::fn_00414110(F(0xc), F(0x0), D(GameP() + 0xa8c), slot0, u(&slot1), 0, u(&slot0), 0, 0, 0, 0,
                                   slot1, 0, 0, 2);
    return static_cast<uint8_t>(r) != 0 ? 1u : 0u;
}

static uint32_t fn_00426ee0_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_00426ee0(eax, edi, a0, a1);
}
ABI_ENTRY_00426ee0(fn_00426ee0_Entry, fn_00426ee0_Abi)
HOOK(0x00426ee0, "fn_00426ee0", fn_00426ee0_Entry, nullptr);

// 0x00426fd0 (eax = source vec3, ecx = AI object; stack: target object, exclude; ret 8; returns al). Callers
// AI_FindTarget and others. Line-of-sight test: false without a target; otherwise a query (Query_Init 0x00413cc0, dl =
// 1) from the source raised by 0x005bcbdc towards the target's "ref_hit_00" point (0x00428280), including the list
// game+0xa84+0xe9c, excluding the AI's list +0x608 and the chains of `exclude`, +0x880 and the target; Ray_Cast
// 0x00413e10. True when nothing is hit or the hit object is the target's (0x004539d0).
uint32_t fn_00426fd0(uint32_t eax, uint32_t ecx, uint32_t target, uint32_t exclude)
{
    uint32_t fr[0x15];  // sub esp, 0x54: fr[0] is [esp+0x10] with ebx, ebp, edi, esi pushed
    auto F = [&](uint32_t o) { return u(fr) + o; };
    if (target == 0)
        return 0;
    uint32_t ebx = eax;
    uint32_t edi = ecx;
    uint32_t q = F(0xc);
    orig::fn_00413cc0(q, 1);  // mov dl, 1
    fn_00428280(F(0x0), edi, target, 0x005b68fcu);
    float z = Fl(ebx + 8) + K(0x005bcbdc);
    uint32_t x = D(ebx);
    uint32_t y = D(ebx + 4);
    Fl(F(0x1c)) = z;
    D(F(0x14)) = x;
    float dx = Fl(F(0x0)) - Fl(F(0x14));
    D(F(0x18)) = y;
    Fl(F(0x20)) = dx;
    D(F(0x2c)) = 0x3f800000;
    Fl(F(0x24)) = Fl(F(0x4)) - Fl(F(0x18));
    Fl(F(0x28)) = Fl(F(0x8)) - Fl(F(0x1c));
    orig::fn_00413dc0(D(D(GameP() + 0xa84) + 0xe9c), q);
    orig::fn_00413d70(D(edi + 0x608), q);
    orig::fn_00413d40(exclude, q);
    orig::fn_00413d40(D(edi + 0x880), q);
    orig::fn_00413d40(target, q);
    By(F(0x48)) = 1;
    D(F(0x4c)) = 2;
    By(F(0x4a)) = 1;
    if (!static_cast<uint8_t>(orig::fn_00413e10(q)))
        return 1;
    uint32_t hit = D(F(0x40));
    if (hit == 0)
        return 0;
    return hit == orig::fn_004539d0(target) ? 1u : 0u;
}

static uint32_t fn_00426fd0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00426fd0(eax, ecx, a0, a1);
}
ABI_ENTRY_00426fd0(fn_00426fd0_Entry, fn_00426fd0_Abi)
HOOK(0x00426fd0, "fn_00426fd0", fn_00426fd0_Entry, nullptr);

// 0x004270f0 (eax: not read; returns eax = -1). Shared by many AI vtables (GenericAIObject slot +0x138) and four slots
// of vtable 0x0058e540 (not executed in recorded runs).
uint32_t fn_004270f0(uint32_t /*eax*/)
{
    return 0xffffffffu;
}

static uint32_t fn_004270f0_Abi(uint32_t eax)
{
    return fn_004270f0(eax);
}
ABI_ENTRY_004270f0(fn_004270f0_Entry, fn_004270f0_Abi)
HOOK(0x004270f0, "fn_004270f0", fn_004270f0_Entry, nullptr);

// 0x004271c0 (esi = AI object; returns eax). Callers 0x00427910, 0x00427c80, GenericAI_Update (not executed in recorded
// runs). Plays animation id 0x74..0x76 (virtual +0x19c, no flags): the next one in turn when one of them is playing
// (+0x498), otherwise a random one.
uint32_t fn_004271c0(uint32_t esi)
{
    uint32_t id = D(esi + 0x498);
    if (id >= 0x74 && id <= 0x76)
    {
        ++id;
        if (id > 0x76)
            id = 0x74;
    }
    else
    {
        id = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 3) >> 15) + 0x74;
    }
    return VC(esi, 0x19c, id, 0u, 0u, 0u, 0u, 0u);
}

static uint32_t fn_004271c0_Abi(uint32_t esi)
{
    return fn_004271c0(esi);
}
ABI_ENTRY_004271c0(fn_004271c0_Entry, fn_004271c0_Abi)
HOOK(0x004271c0, "fn_004271c0", fn_004271c0_Entry, nullptr);

// 0x00427330 (esi = AI object; returns eax). Callers 0x0042be80, GenericAI_Update. With flag 0x40 of +0x484: clears it
// and plays animation 0xf looped (virtual +0x19c). Otherwise, when animation 0xf is playing (+0x498), sets the flag
// and plays a random animation 0x12..0x1a.
uint32_t fn_00427330(uint32_t esi)
{
    if (By(esi + 0x484) & 0x40)
    {
        D(esi + 0x484) &= 0xffffffbfu;
        return VC(esi, 0x19c, 0xfu, 1u, 0u, 0u, 0u, 0u);
    }
    if (D(esi + 0x498) != 0xf)
        return 0x40;
    D(esi + 0x484) |= 0x40;
    uint32_t id = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 9) >> 15) + 0x12;
    return VC(esi, 0x19c, id, 0u, 0u, 0u, 0u, 0u);
}

static uint32_t fn_00427330_Abi(uint32_t esi)
{
    return fn_00427330(esi);
}
ABI_ENTRY_00427330(fn_00427330_Entry, fn_00427330_Abi)
HOOK(0x00427330, "fn_00427330", fn_00427330_Entry, nullptr);

// 0x004273a0 (ecx = AI object; returns eax). Vtable slot +0x188; caller 0x00436b50 (not executed in recorded runs).
// Pushes the object at +0x904 into the three-entry history +0x918 / +0x91c / +0x920: with flag 0x10 of +0x484 the
// history shifts (the oldest reference dropped, the slot not cleared); otherwise all three are cleared first. The
// reference moves without a count change. When there was an object, clears flag 0x20000 of +0x484 and calls its
// virtual +0x15c(self, +0x914), then +0x904 = 0.
uint32_t fn_004273a0(uint32_t esi)
{
    uint32_t eax;
    if (By(esi + 0x484) & 0x10)
    {
        uint32_t edi = D(esi + 0x920);
        if (edi)
            Unref(edi);
        D(esi + 0x920) = D(esi + 0x91c);
        D(esi + 0x91c) = D(esi + 0x918);
        eax = D(esi + 0x904);
        D(esi + 0x918) = eax;
    }
    else
    {
        ClearRef(esi + 0x918);
        ClearRef(esi + 0x91c);
        ClearRef(esi + 0x920);
        eax = D(esi + 0x904);
        D(esi + 0x918) = eax;
    }
    if (eax)
    {
        D(esi + 0x484) &= 0xfffdffffu;
        eax = VC(D(esi + 0x904), 0x15c, esi, D(esi + 0x914));
        D(esi + 0x904) = 0;
    }
    return eax;
}

static uint32_t fn_004273a0_Abi(uint32_t ecx)
{
    return fn_004273a0(ecx);
}
ABI_ENTRY_004273a0(fn_004273a0_Entry, fn_004273a0_Abi)
HOOK(0x004273a0, "fn_004273a0", fn_004273a0_Entry, nullptr);

// 0x004274e0 (ecx = AI object; stack: 1 argument, not read; ret 4; returns eax). Vtable slot +0x18c; callers
// 0x00436b60, 0x00437290. Leaves the object at +0x904 (its virtual +0x15c(self, +0x914), then the reference is
// dropped), clears the history +0x918..+0x920, sets +0x924 = 5 and clears flags 0x20000 of +0x484 and 0x200000 of
// +0x488.
uint32_t fn_004274e0(uint32_t esi, uint32_t /*a0*/)
{
    uint32_t obj = D(esi + 0x904);
    if (obj)
    {
        VC(obj, 0x15c, esi, D(esi + 0x914));
        Unref(D(esi + 0x904));
        D(esi + 0x904) = 0;
    }
    ClearRef(esi + 0x918);
    ClearRef(esi + 0x91c);
    ClearRef(esi + 0x920);
    D(esi + 0x924) = 5;
    D(esi + 0x484) &= 0xfffdffffu;
    uint32_t f = D(esi + 0x488) & 0xffdfffffu;
    D(esi + 0x488) = f;
    return f;
}

static uint32_t fn_004274e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004274e0(ecx, a0);
}
ABI_ENTRY_004274e0(fn_004274e0_Entry, fn_004274e0_Abi)
HOOK(0x004274e0, "fn_004274e0", fn_004274e0_Entry, nullptr);

namespace
{
// The inlined random integer in [lo, hi]: ((rand() * (hi - lo + 1)) >> 15) + lo, fields read before the call.
inline uint32_t RandRange(uint32_t self, uint32_t hiOff, uint32_t loOff)
{
    uint32_t hi = D(self + hiOff);
    uint32_t lo = D(self + loOff);
    uint32_t r = Rand();
    return static_cast<uint32_t>(static_cast<int32_t>(r * (hi - lo + 1)) >> 15) + lo;
}
// The inlined random float: (float)rand() * (hi - lo) * 0x005bcbc0 + lo.
inline float RandRangeF(uint32_t self, uint32_t hiOff, uint32_t loOff)
{
    float lo = Fl(self + loOff);
    float hi = Fl(self + hiOff);
    float r = static_cast<float>(static_cast<int32_t>(Rand()));
    return r * (hi - lo) * K(0x005bcbc0) + lo;
}
}  // namespace

// 0x00427600 (esi = AI object; returns eax = +0x5bc). Callers 0x004247f0, 0x0042ecf0, 0x00434ac0, 0x00436bb0,
// 0x0043d180, GenericAI_Update, 0x00485910. Rolls the random counters +0x5b4 / +0x5bc / +0x5b8 (integers) and +0x5c8
// (float) from [lo, hi] pairs: from the turret's tables (+0x89c..+0x8b8 when the turret's virtual +0x168 returns 1,
// otherwise +0x8cc..+0x8e8) when a turret is at +0x898, else from +0x58c..+0x5a8. The first time (flag 4 of +0x484 not
// yet set: sets 4 and 1) also rolls +0x5b4 and zeroes +0x5c8; later calls roll the float instead. Clears +0x5cc and
// clamps +0x5b8 to at most +0x5bc (signed).
uint32_t fn_00427600(uint32_t esi)
{
    uint32_t turret = D(esi + 0x898);
    if (turret != 0)
    {
        uint32_t kind = VC(turret, 0x168);
        if (!(By(esi + 0x484) & 4))
        {
            uint32_t f = D(esi + 0x484) | 4;
            D(esi + 0x484) = f;
            D(esi + 0x484) = f | 1;
            if (kind == 1)
            {
                D(esi + 0x5b4) = RandRange(esi, 0x8a0, 0x89c);
                D(esi + 0x5bc) = RandRange(esi, 0x8b0, 0x8ac);
                D(esi + 0x5b8) = RandRange(esi, 0x8b8, 0x8b4);
            }
            else
            {
                D(esi + 0x5b4) = RandRange(esi, 0x8d0, 0x8cc);
                D(esi + 0x5bc) = RandRange(esi, 0x8e0, 0x8dc);
                D(esi + 0x5b8) = RandRange(esi, 0x8e8, 0x8e4);
            }
            D(esi + 0x5c8) = 0;
        }
        else if (kind == 1)
        {
            D(esi + 0x5bc) = RandRange(esi, 0x8b0, 0x8ac);
            D(esi + 0x5b8) = RandRange(esi, 0x8b8, 0x8b4);
            Fl(esi + 0x5c8) = RandRangeF(esi, 0x8a8, 0x8a4);
        }
        else
        {
            D(esi + 0x5bc) = RandRange(esi, 0x8e0, 0x8dc);
            D(esi + 0x5b8) = RandRange(esi, 0x8e8, 0x8e4);
            Fl(esi + 0x5c8) = RandRangeF(esi, 0x8d8, 0x8d4);
        }
    }
    else if (!(By(esi + 0x484) & 4))
    {
        uint32_t f = D(esi + 0x484) | 4;
        D(esi + 0x484) = f;
        D(esi + 0x484) = f | 1;
        D(esi + 0x5b4) = RandRange(esi, 0x590, 0x58c);
        D(esi + 0x5bc) = RandRange(esi, 0x5a0, 0x59c);
        D(esi + 0x5b8) = RandRange(esi, 0x5a8, 0x5a4);
        D(esi + 0x5c8) = 0;
    }
    else
    {
        D(esi + 0x5bc) = RandRange(esi, 0x5a0, 0x59c);
        D(esi + 0x5b8) = RandRange(esi, 0x5a8, 0x5a4);
        Fl(esi + 0x5c8) = RandRangeF(esi, 0x598, 0x594);
    }
    uint32_t eax = D(esi + 0x5bc);
    D(esi + 0x5cc) = 0;
    if (I(esi + 0x5b8) > static_cast<int32_t>(eax))
        D(esi + 0x5b8) = eax;
    return eax;
}

static uint32_t fn_00427600_Abi(uint32_t esi)
{
    return fn_00427600(esi);
}
ABI_ENTRY_00427600(fn_00427600_Entry, fn_00427600_Abi)
HOOK(0x00427600, "fn_00427600", fn_00427600_Entry, nullptr);

uint32_t fn_00427e80(uint32_t eax);

// 0x00427910 (ecx = AI object; stack: byte allow_close_attack; ret 4). Vtable slot +0x230 (not executed in recorded
// runs). Per-frame decision with a target at +0x588: nothing when 0x00454780 is true, there is no target, or
// 0x00427e80 is true. Otherwise 0x00454c90(cl = 1, 1), reads the AI's position (virtual +0x54) and the target's into
// +0x60c, and compares the squared 2D distance with the squared range +0x5d8:
//  - out of range or flag 4 of +0x484 clear: state 0xa2 with animation 0x66 (flag 0x20000), state 0x79 with the
//    +0x1a0 animation set (flag 0x2000000), else virtual +0x18c(0) and 0x0042b8e0;
//  - in range: drops flag 0x100 of +0x488 (0x004246c0), rolls a random 0..89 (0..199 for the other side, 1000 when the
//    target's virtual +0x10c holds and 0x00426630 or its state 0xf), and when virtual +0x1e0, `allow_close_attack`,
//    the roll below 30 and flag 1 of +0x484 agree plays animation 0x27 into state 0x10; else clears flag 0x10 of +0x484,
//    calls virtual +0x188 for an object at +0x904, and picks state 0xa (with game+0xc50 limiting a counted group list
//    game+0xa84+0xf18, flag 0x100000 at 16 %) or 0x11 (animation 0x74..0x76 via 0x004271c0), or 0xa after
//    0x004a10e0 unless virtual +0x220. Names of the states are not known.
uint32_t fn_00427910(uint32_t edi, uint32_t a0)
{
    uint32_t fr[4];  // sub esp, 0x10: fr[0] is [esp+0xc] with esi, edi, ebx pushed
    auto F = [&](uint32_t o) { return u(fr) + o; };
    if (static_cast<uint8_t>(orig::fn_00454780(edi)))
        return 0;
    if (D(edi + 0x588) == 0)
        return 0;
    if (static_cast<uint8_t>(fn_00427e80(edi)))
        return 0;
    orig::fn_00454c90(edi, (edi & 0xffffff00u) | 1, 1);
    VC(edi, 0x54, F(4));
    VC(D(edi + 0x588), 0x54, edi + 0x60c);
    float dy = Fl(edi + 0x610) - Fl(F(8));
    float dx = Fl(edi + 0x60c) - Fl(F(4));
    float d = dx * dx + dy * dy;
    Fl(F(0)) = d;
    float r = Fl(edi + 0x5d8);
    if (!(d > r * r) && (By(edi + 0x484) & 4))
    {
        if (D(edi + 0x488) & 0x100)
        {
            orig::fn_004246c0(D(edi + 0x488) & 0xffffff00u, edi);  // xor al, al
            D(edi + 0x488) &= 0xfffffeffu;
        }
        uint32_t esi;
        if (VB(edi, 0xe4))
        {
            esi = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 0x5a) >> 15);
            if (VB(D(edi + 0x588), 0x10c))
            {
                uint32_t ecx = D(edi + 0x588);
                if (static_cast<uint8_t>(fn_00426630(ecx)) || D(ecx + 0x460) == 0xf)
                    esi = 0x3e8;
            }
        }
        else
        {
            esi = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 0xc8) >> 15);
        }
        if (VB(edi, 0x1e0) && static_cast<uint8_t>(a0) && esi < 0x1e)
        {
            float rr = Fl(edi + 0x5d8);
            if (Fl(F(0)) < rr * rr && (By(edi + 0x484) & 1))
            {
                VC(edi, 0x19c, 0x27u, 0u, 0u, 0u, 0u, 0u);
                return VC(edi, 0x1b8, 0x10u);
            }
        }
        uint32_t ecx = D(edi + 0x484) & 0xffffffefu;
        D(edi + 0x484) = ecx;
        if (D(edi + 0x904) != 0 && !(ecx & 0x20000) && !(D(edi + 0x488) & 0x200000))
            VC(edi, 0x188);
        uint32_t game = GameP();
        if (D(game + 0xc50) != 0 && !(D(edi + 0x488) & 0x20000) && !VB(edi, 0xe4))
        {
            game = GameP();
            uint32_t n = VC(D(D(game + 0xa84) + 0xf18), 0x34);
            game = GameP();
            if (n < D(game + 0xc50))
            {
                orig::fn_004546d0(D(D(game + 0xa84) + 0xf18), edi);
                uint32_t f = D(edi + 0x488) | 0x20000;
                D(edi + 0x488) = f;
                D(edi + 0x488) = f & 0xffefffffu;
                return VC(edi, 0x1b8, 0xau);
            }
            if (static_cast<int32_t>((Rand() * 100) & 0xffff8000u) < 0x190000)
            {
                D(edi + 0x488) |= 0x100000;
                return VC(edi, 0x1b8, 0xau);
            }
            fn_004271c0(edi);
            return VC(edi, 0x1b8, 0x11u);
        }
        if (VB(edi, 0x220))
            return 1;
        orig::fn_004a10e0(edi + 0x61c, 0);
        return VC(edi, 0x1b8, 0xau);
    }
    uint32_t eax = D(edi + 0x484);
    if (eax & 0x20000)
    {
        VC(edi, 0x19c, 0x66u, 0u, 0u, 0u, 0u, 0u);
        return VC(edi, 0x1b8, 0xa2u);
    }
    if (eax & 0x2000000)
    {
        VC(edi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
        return VC(edi, 0x1b8, 0x79u);
    }
    VC(edi, 0x18c, 0u);
    return orig::fn_0042b8e0(0, edi);  // "ecx" is only a push ecx slot (ai_06)
}

static uint32_t fn_00427910_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00427910(ecx, a0);
}
ABI_ENTRY_00427910(fn_00427910_Entry, fn_00427910_Abi)
HOOK(0x00427910, "fn_00427910", fn_00427910_Entry, nullptr);

// 0x00427c80 (eax = AI object; stack: byte allow_close_attack; ret 4). Callers 0x0042be80, GenericAI_Update (not
// executed in recorded runs). A variant of 0x00427910 without the early checks: 0x00454c90(cl = 1, 1), the squared 2D
// distance to the target +0x588 (its position stored at +0x60c) against the squared range +0x5d8; out of range (or
// flag 4 of +0x484 clear) -> virtual +0x18c(0) and 0x0042b8e0; else animation 0x27 / state 0x10 at a 10 % roll (with
// virtual +0x1e0 and `allow_close_attack`), otherwise clears flag 0x10 and picks state 0xa or 0x11 as 0x00427910 does.
uint32_t fn_00427c80(uint32_t edi, uint32_t a0)
{
    uint32_t fr[4];  // sub esp, 0x10: fr[0] is [esp+0xc] with ebx, esi, edi pushed
    auto F = [&](uint32_t o) { return u(fr) + o; };
    orig::fn_00454c90(edi, 1, 1);  // mov cl, 1 (upper ecx bytes: the caller's, unknown)
    VC(edi, 0x54, F(4));
    VC(D(edi + 0x588), 0x54, edi + 0x60c);
    float dy = Fl(edi + 0x610) - Fl(F(8));
    float dx = Fl(edi + 0x60c) - Fl(F(4));
    float d = dx * dx + dy * dy;
    Fl(F(0)) = d;
    float r = Fl(edi + 0x5d8);
    if (d > r * r || !(By(edi + 0x484) & 4))
    {
        VC(edi, 0x18c, 0u);
        return orig::fn_0042b8e0(0, edi);  // "ecx" is only a push ecx slot (ai_06)
    }
    if (VB(edi, 0x1e0) && static_cast<uint8_t>(a0) &&
        static_cast<int32_t>((Rand() * 100) & 0xffff8000u) < 0xa0000)
    {
        float rr = Fl(edi + 0x5d8);
        if (Fl(F(0)) < rr * rr)
        {
            VC(edi, 0x19c, 0x27u, 0u, 0u, 0u, 0u, 0u);
            return VC(edi, 0x1b8, 0x10u);
        }
    }
    D(edi + 0x484) &= 0xffffffefu;
    uint32_t game = GameP();
    if (D(game + 0xc50) != 0 && !(D(edi + 0x488) & 0x20000) && !VB(edi, 0xe4))
    {
        game = GameP();
        uint32_t n = VC(D(D(game + 0xa84) + 0xf18), 0x34);
        game = GameP();
        if (n < D(game + 0xc50))
        {
            orig::fn_004546d0(D(D(game + 0xa84) + 0xf18), edi);
            uint32_t f = D(edi + 0x488) | 0x20000;
            D(edi + 0x488) = f;
            D(edi + 0x488) = f & 0xffefffffu;
            return VC(edi, 0x1b8, 0xau);
        }
        if (static_cast<int32_t>((Rand() * 100) & 0xffff8000u) >= 0x190000)
        {
            fn_004271c0(edi);
            return VC(edi, 0x1b8, 0x11u);
        }
        D(edi + 0x488) |= 0x100000;
    }
    return VC(edi, 0x1b8, 0xau);
}

static uint32_t fn_00427c80_Abi(uint32_t eax, uint32_t a0)
{
    return fn_00427c80(eax, a0);
}
ABI_ENTRY_00427c80(fn_00427c80_Entry, fn_00427c80_Abi)
HOOK(0x00427c80, "fn_00427c80", fn_00427c80_Entry, nullptr);

// 0x00427e80 (eax = AI object; returns al). Callers 0x00427910, 0x00428980, 0x00484100. Tries to start a close fight
// with the target +0x588 ("ref_fight_00"): false unless there is a target, flag 0x2000000 of +0x484 is clear, the
// target's virtual +0x104 and the AI's virtual +0x1e4 hold, the state is not 0xc..0xe, the target is not 0x00454780,
// a 16 % roll fails (>= 0x190000), the squared 3D distance is below 0x005bccb8 and the angle difference 0x004e81f0
// of both headings (0x00453150 + 0x004e83b0) is outside (0x005bcea8, 0x005bceac). Then the target's "ref_fight_00"
// matrix gives a point and direction (negated column 1) whose height must be within 0x005bcb4c of the target's;
// 0x0046f6b0 (grid game+0xaa4) must accept it (mask 0xd), else 0x0046f440 and false. Drops a held object +0xab0
// (physics on via 0x004577b0, flag 0x2000 off, 0x0042bc60), asks the target (virtual +0x108(self)) and on success
// sends it virtual +0xf8(0x120000, 0), 0x00454c90(cl = 0, 1), holds it at +0xab0, plays the fight animation
// (0x00427210) and enters state 0xc (true). Name: guess from the "ref_fight_00" string.
uint32_t fn_00427e80(uint32_t edi)
{
    uint32_t fr[0x20];  // sub esp, 0x80: fr[0] is [esp+0xc] with ebx, esi, edi pushed
    auto S = [&](uint32_t o) { return u(fr) + o - 0xc; };
    uint32_t target = D(edi + 0x588);
    if (target == 0 || (D(edi + 0x484) & 0x2000000))
        return 0;
    if (!VB(target, 0x104) || !VB(edi, 0x1e4))
        return 0;
    uint32_t state = D(edi + 0x460);
    if (state == 0xc || state == 0xd || state == 0xe)
        return 0;
    if (static_cast<uint8_t>(orig::fn_00454780(D(edi + 0x588))))
        return 0;
    if (static_cast<int32_t>((Rand() * 100) & 0xffff8000u) < 0x190000)
        return 0;
    VC(edi, 0x54, S(0x24));
    VC(D(edi + 0x588), 0x54, S(0x30));
    {
        // 0x004e8080 (eax = a, ecx = b): squared 3D distance, st0
        uint32_t a = S(0x24), b = S(0x30);
        float dz = Fl(a + 8) - Fl(b + 8);
        float dy = Fl(a + 4) - Fl(b + 4);
        float dx = Fl(a) - Fl(b);
        float d = dx * dx + dy * dy + dz * dz;
        if (!(d < K(0x005bccb8)))
            return 0;
    }
    orig::fn_00453150(D(edi + 0x588), S(0xc));
    orig::fn_00453150(edi, S(0x18));
    float h1 = orig::fn_004e83b0(S(0x18));
    float h0 = orig::fn_004e83b0(S(0xc));
    float x = orig::fn_004e81f0(fb(h0), fb(h1));
    if (!(x >= K(0x005bceac)) && !(x <= K(0x005bcea8)))
        return 0;  // (x < hi or unordered) and (x > lo or unordered)
    VC(D(edi + 0x588), 0x54, S(0x3c));
    orig::fn_00453c50(D(edi + 0x588), 0x005b68ecu /* "ref_fight_00" */, S(0x48), 1, 0, 0, 1, 0, 0);
    Fl(S(0xc)) = -Fl(S(0x4c));
    D(S(0x18)) = D(S(0x54));
    Fl(S(0x10)) = -Fl(S(0x5c));
    D(S(0x1c)) = D(S(0x64));
    D(S(0x20)) = D(S(0x74));
    Fl(S(0x14)) = -Fl(S(0x6c));
    if (std::fabs(Fl(S(0x74)) - Fl(S(0x44))) > K(0x005bcb4c))
        return 0;
    uint32_t game = GameP();
    uint32_t grid = D(game + 0xaa4);
    uint32_t r = orig::fn_0046f6b0(S(0xc) & 0xffffff00u, game & 0xffffff00u, grid & 0xffffff00u, S(0x18), grid, edi,
                                   S(0xc));  // al = cl = dl = 0
    if (!(r & 0xd))
    {
        orig::fn_0046f440(D(GameP() + 0xaa4), edi);
        return 0;
    }
    uint32_t slot = edi + 0xab0;
    if (D(slot) != 0)
    {
        orig::fn_004577b0(D(slot), 0);
        D(D(slot) + 0x484) &= 0xffffdfffu;
        orig::fn_0042bc60(D(slot), edi, 0, 0);
        Unref(D(slot));
        D(slot) = 0;
    }
    uint32_t t = D(edi + 0x588);
    if (t == 0)
        return 0;
    if (VB(t, 0x108, edi))
    {
        VC(D(edi + 0x588), 0xf8, 0x120000u, 0u);
        orig::fn_00454c90(edi, 0, 1);  // xor cl, cl (upper ecx bytes: left by the virtual call, unknown)
        uint32_t o = D(edi + 0x588);
        D(slot) = o;
        I(o + 0x28) += 1;
        orig::fn_00427210(edi);
        VC(edi, 0x1b8, 0xcu);
        return 1;
    }
    orig::fn_0046f440(D(GameP() + 0xaa4), edi);
    return 0;
}

static uint32_t fn_00427e80_Abi(uint32_t eax)
{
    return fn_00427e80(eax);
}
ABI_ENTRY_00427e80(fn_00427e80_Entry, fn_00427e80_Abi)
HOOK(0x00427e80, "fn_00427e80", fn_00427e80_Entry, nullptr);

// 0x00428160 (esi = AI object; returns al). Callers 0x00428980, GenericAI_Update (not executed in recorded runs).
// When there is a target +0x588, virtual +0x1dc holds and the target is strictly inside the 3D range +0x5fc, plays
// a random animation 0x5f / 0x60 looped and enters state 0xb (true).
uint32_t fn_00428160(uint32_t esi)
{
    uint32_t fr[6];  // sub esp, 0x18: own position [esp], target position [esp+0xc]
    if (D(esi + 0x588) == 0 || !VB(esi, 0x1dc))
        return 0;
    VC(esi, 0x54, u(&fr[0]));
    VC(D(esi + 0x588), 0x54, u(&fr[3]));
    float dz = bf(fr[2]) - bf(fr[5]);
    float dy = bf(fr[1]) - bf(fr[4]);
    float dx = bf(fr[0]) - bf(fr[3]);
    float r = Fl(esi + 0x5fc);
    float d = dx * dx + dy * dy + dz * dz;
    if (!(r * r > d))
        return 0;
    uint32_t id = static_cast<uint32_t>(static_cast<int32_t>(Rand() << 1) >> 15) + 0x5f;
    VC(esi, 0x19c, id, 1u, 0u, 0u, 0u, 0u);
    VC(esi, 0x1b8, 0xbu);
    return 1;
}

static uint32_t fn_00428160_Abi(uint32_t esi)
{
    return fn_00428160(esi);
}
ABI_ENTRY_00428160(fn_00428160_Entry, fn_00428160_Abi)
HOOK(0x00428160, "fn_00428160", fn_00428160_Entry, nullptr);

// 0x00428220 (eax = AI object, cl = on). Caller 0x0043d180 (not executed in recorded runs). Sets or clears flag
// 0x100000 of +0x484.
uint32_t fn_00428220(uint32_t eax, uint32_t ecx)
{
    uint32_t f = D(eax + 0x484);
    f = static_cast<uint8_t>(ecx) ? (f | 0x100000) : (f & 0xffefffffu);
    D(eax + 0x484) = f;
    return eax;
}

static uint32_t fn_00428220_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00428220(eax, ecx);
}
ABI_ENTRY_00428220(fn_00428220_Entry, fn_00428220_Abi)
HOOK(0x00428220, "fn_00428220", fn_00428220_Entry, nullptr);

// 0x00428250 (eax = AI object, cl = on). Sets or clears flag 0x1000 of +0x484.
uint32_t fn_00428250(uint32_t eax, uint32_t ecx)
{
    uint32_t f = D(eax + 0x484);
    f = static_cast<uint8_t>(ecx) ? (f | 0x1000) : (f & 0xffffefffu);
    D(eax + 0x484) = f;
    return eax;
}

static uint32_t fn_00428250_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00428250(eax, ecx);
}
ABI_ENTRY_00428250(fn_00428250_Entry, fn_00428250_Abi)
HOOK(0x00428250, "fn_00428250", fn_00428250_Entry, nullptr);

// 0x00428280 (esi = output vec3, edi = AI object; stack: object, bone name; ret 8; returns eax). Writes the position
// of a hit reference point: with flag 4 of +0x488 a random "ref_hit_0%d" (0..6, sprintf 0x0053814f) of the AI's
// target +0x588 (the object argument is then ignored), otherwise the named point of the object; via the 0x44-byte
// matrix of 0x00453c50 (translation m[3], m[7], m[11]).
uint32_t fn_00428280(uint32_t esi, uint32_t edi, uint32_t object, uint32_t name)
{
    uint32_t fr[0x26];  // sub esp, 0x98: buffer [esp], matrices [esp+0x10] / [esp+0x54]
    auto F = [&](uint32_t o) { return u(fr) + o; };
    uint32_t m;
    if (By(edi + 0x488) & 4)
    {
        uint32_t idx = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 7) >> 15);
        orig::fn_0053814f(F(0), 0x005b68e0u /* "ref_hit_0%d" */, idx);
        m = F(0x10);
        orig::fn_00453c50(D(edi + 0x588), F(0), m, 1, 0, 0, 1, 0, 0);
    }
    else
    {
        m = F(0x54);
        orig::fn_00453c50(object, name, m, 1, 0, 0, 1, 0, 0);
    }
    D(esi) = D(m + 0xc);
    D(esi + 4) = D(m + 0x1c);
    uint32_t z = D(m + 0x2c);
    D(esi + 8) = z;
    return z;
}

static uint32_t fn_00428280_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_00428280(esi, edi, a0, a1);
}
ABI_ENTRY_00428280(fn_00428280_Entry, fn_00428280_Abi)
HOOK(0x00428280, "fn_00428280", fn_00428280_Entry, nullptr);

// 0x00428340 (ecx = bone name, esi = AI object; stack: a0, byte attach; ret 8). Callers 0x0043d180, 0x0044e740 (not
// executed in recorded runs). With a target +0x588: takes the AI's own point `name` (0x00453c50 into a 0x44-byte
// matrix) and the target's hit point (random "ref_hit_0%d" with flag 4 of +0x488, else "ref_hit_00"), creates an
// object with 0x0047de40 (eax = own point, ebx = target point; game+0xaa8, self, a0, 0); with `attach` it calls the new
// object's virtual +0x14c(target, "ref_hit_00") and, when the target's virtual +0x10c holds, stores it in the target's
// +0xb54 (replacing the old reference). Clears flag 1 of +0x484. Guess: fires a projectile at the target.
uint32_t fn_00428340(uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t a1)
{
    uint32_t fr[0x29];  // sub esp, 0xa4
    auto F = [&](uint32_t o) { return u(fr) + o; };
    if (D(esi + 0x588) == 0)
        return 0;
    orig::fn_00453c50(esi, ecx, F(0x60), 1, 0, 0, 1, 0, 0);
    uint32_t y = D(F(0x7c)), x = D(F(0x6c)), z = D(F(0x8c));
    D(F(0x14)) = y;
    D(F(0x10)) = x;
    D(F(0x18)) = z;
    if (By(esi + 0x488) & 4)
    {
        uint32_t idx = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 7) >> 15);
        orig::fn_0053814f(F(0), 0x005b68e0u /* "ref_hit_0%d" */, idx);
        orig::fn_00453c50(D(esi + 0x588), F(0), F(0x1c), 1, 0, 0, 1, 0, 0);
    }
    else
    {
        orig::fn_00453c50(D(esi + 0x588), 0x005b68fcu /* "ref_hit_00" */, F(0x1c), 1, 0, 0, 1, 0, 0);
    }
    uint32_t px = D(F(0x28)), py = D(F(0x38)), pz = D(F(0x48));
    D(F(0)) = px;
    D(F(4)) = py;
    D(F(8)) = pz;
    uint32_t edi = orig::fn_0047de40(F(0x10), F(0), D(GameP() + 0xaa8), esi, a0, 0);
    if (static_cast<uint8_t>(a1))
    {
        VC(edi, 0x14c, D(esi + 0x588), 0x005b68fcu);
        uint32_t t = D(esi + 0x588);
        if (t != 0 && VB(t, 0x10c))
        {
            uint32_t ebp = D(esi + 0x588);
            uint32_t old = D(ebp + 0xb54);
            if (old)
                Unref(old);
            D(ebp + 0xb54) = edi;
            I(edi + 0x28) += 1;
        }
    }
    uint32_t f = D(esi + 0x484) & 0xfffffffeu;
    D(esi + 0x484) = f;
    return f;
}

static uint32_t fn_00428340_Abi(uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return fn_00428340(ecx, esi, a0, a1);
}
ABI_ENTRY_00428340(fn_00428340_Entry, fn_00428340_Abi)
HOOK(0x00428340, "fn_00428340", fn_00428340_Entry, nullptr);

namespace
{
// 0x004e7880 (ecx = source vec3, edx = destination): normalises a 3D vector and returns its length in st0. The ABI
// table lists eax as the result; the callers pop st0 (fstp), so it is called here with its real st0 result (as in
// src/ai/ai_06.cpp).
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
}  // namespace

// 0x004284e0 (ecx = AI object; stack: target, weapon index, byte aimed, a3; ret 0x10). Vtable slot +0x194; callers
// 0x0044f160, 0x004878f0. Aims and fires: with a target, at its "ref_hit_00" point (0x00428280) scattered sideways and
// vertically by 0x0046e2c0 (random in [-+0x5e8, +0x5e8] and [0, +0x5e4]), the scatter scaled by 0x005bcbfc inside the
// near range (+0x5ec / +0x5f4 with flag 0x400000) and by an interpolation towards the far range (+0x5f0 / +0x5f8, at
// least 0.1); a zero distance or flag 1 of +0x48c aims along the turret's (+0x898, virtual +0x148) or the weapon's
// (+0x61c entry, virtual +0x13c) direction instead, extended by 0x005bcf68 (with flag 1 of +0x48c towards the target's
// 0x004580a0 point). A turret fires through its virtual +0x13c(0, point) when flag 0x8000 of +0x484 is set; otherwise
// the weapon entry (0x004a15f0) fires with 0x004a2010 (target and "ref_hit_00", or none). Without a target: selects
// weapon index a1 (0x004a10b0) and fires along its direction (0x005bcc04) when `aimed`, else 0x004a2100. Name: guess.
uint32_t fn_004284e0(uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t st[0x18];  // sub esp, 0x4c plus the return address and the four arguments: st[0] is [esp+0x10]
    auto S = [&](uint32_t o) { return u(st) + o - 0x10; };
    D(S(0x60)) = a0;
    D(S(0x64)) = a1;
    D(S(0x68)) = a2;
    D(S(0x6c)) = a3;
    uint32_t ebp = a0;
    if (ebp == 0)
    {
        if (D(edi + 0x898) != 0)
            return 0;
        uint32_t esi = edi + 0x61c;
        if (static_cast<uint8_t>(a2))
        {
            orig::fn_004a10b0(esi, a1);
            if (orig::fn_004a15f0(esi) != 0)
            {
                uint32_t w = orig::fn_004a15f0(esi);
                VC(w, 0x13c, S(0x44), S(0x50));
            }
            Fl(S(0x38)) = Fl(S(0x50)) * K(0x005bcc04) + Fl(S(0x44));
            Fl(S(0x3c)) = Fl(S(0x54)) * K(0x005bcc04) + Fl(S(0x48));
            Fl(S(0x40)) = Fl(S(0x58)) * K(0x005bcc04) + Fl(S(0x4c));
            if (orig::fn_004a15f0(esi) == 0)
                return 0;
            uint32_t w = orig::fn_004a15f0(esi);
            return orig::fn_004a2010(S(0x38), w, 0, 0, a3);
        }
        orig::fn_004a10b0(esi, a1);
        if (orig::fn_004a15f0(esi) == 0)
            return 0;
        return orig::fn_004a2100(orig::fn_004a15f0(esi));
    }
    if (!VB(D(edi + 0x588), 0xd8))
        return 0;
    fn_00428280(S(0x20), edi, ebp, 0x005b68fcu);
    VC(edi, 0x54, S(0x38));
    Fl(S(0x14)) = Fl(S(0x20)) - Fl(S(0x38));
    float dy = Fl(S(0x24)) - Fl(S(0x3c));
    Fl(S(0x18)) = dy;
    float len = std::sqrt(dy * Fl(S(0x18)) + Fl(S(0x14)) * Fl(S(0x14)));
    Fl(S(0x68)) = len;
    if (len != K(0x0056c05c) && !(By(edi + 0x48c) & 1))
    {
        float inv = K(0x005bcb2c) / Fl(S(0x68));
        uint32_t h = D(edi + 0x5e8);
        Fl(S(0x68)) = inv;
        float nx = inv * Fl(S(0x14));
        float ny = Fl(S(0x68)) * Fl(S(0x18));
        Fl(S(0x18)) = ny;
        Fl(S(0x14)) = -ny;
        Fl(S(0x18)) = nx;
        float r1 = orig::fn_0046e2c0(h, fb(-bf(h)), h);  // ecx = +0x5e8 as in the original (a push ecx slot)
        Fl(S(0x10)) = r1;
        float r2 = orig::fn_0046e2c0(0, 0, D(edi + 0x5e4));
        float ey = Fl(S(0x3c)) - Fl(S(0x24));
        uint32_t edx = D(edi + 0x484);
        float ex = Fl(S(0x38)) - Fl(S(0x20));
        Fl(S(0x64)) = ex * ex + ey * ey;
        float lo;
        if (edx & 0x400000)
        {
            D(S(0x68)) = D(edi + 0x5f8);
            lo = Fl(edi + 0x5f4);
        }
        else
        {
            D(S(0x68)) = D(edi + 0x5f0);
            lo = Fl(edi + 0x5ec);
        }
        Fl(S(0x60)) = lo * lo;
        float A, B;
        if (!(Fl(S(0x64)) > Fl(S(0x60)) || Fl(S(0x64)) != Fl(S(0x64))))
        {
            A = Fl(S(0x10)) * K(0x005bcbfc);
            B = r2 * K(0x005bcbfc);
        }
        else
        {
            float hi2 = Fl(S(0x68)) * Fl(S(0x68));
            if (Fl(S(0x64)) < hi2)
            {
                float num = Fl(S(0x64)) - Fl(S(0x60));
                float t = num / (hi2 - Fl(S(0x60)));
                Fl(S(0x68)) = t;
                if (t < K(0x005bcbfc))
                    D(S(0x68)) = 0x3dcccccd;  // 0.1
                A = Fl(S(0x68)) * Fl(S(0x10));
                B = r2 * Fl(S(0x68));
            }
            else
            {
                A = Fl(S(0x10));
                B = r2;
            }
        }
        uint32_t turret = D(edi + 0x898);
        Fl(S(0x20)) = Fl(S(0x14)) * A + Fl(S(0x20));
        Fl(S(0x24)) = A * Fl(S(0x18)) + Fl(S(0x24));
        Fl(S(0x28)) = Fl(S(0x28)) + B;
        if (turret != 0)
        {
            if (!(edx & 0x8000))
                return 0;
            return VC(turret, 0x13c, 0u, S(0x20));
        }
        uint32_t wl = edi + 0x61c;
        if (orig::fn_004a15f0(wl) == 0)
            return 0;
        uint32_t w = orig::fn_004a15f0(wl);
        return orig::fn_004a2010(S(0x20), w, ebp, 0x005b68fcu, D(S(0x6c)));
    }
    uint32_t turret = D(edi + 0x898);
    if (turret != 0)
    {
        if ((By(edi + 0x48c) & 1) && D(edi + 0x588) != 0)
        {
            VC(turret, 0x148, S(0x2c), 0u);
            orig::fn_004580a0(S(0x50), D(edi + 0x588), S(0x2c), 0x43b40000u, 1);  // 360.0, ebx = 1
            Fl(S(0x14)) = Fl(S(0x50)) - Fl(S(0x2c));
            Fl(S(0x18)) = Fl(S(0x54)) - Fl(S(0x30));
            Fl(S(0x1c)) = Fl(S(0x58)) - Fl(S(0x34));
            Normalize3(S(0x14), S(0x14));
        }
        else
        {
            VC(turret, 0x148, S(0x2c), S(0x14));
        }
    }
    else
    {
        uint32_t wl = edi + 0x61c;
        orig::fn_004a10b0(wl, D(S(0x64)));
        if (orig::fn_004a15f0(wl) != 0)
        {
            uint32_t w = orig::fn_004a15f0(wl);
            VC(w, 0x13c, S(0x2c), S(0x14));
        }
    }
    Fl(S(0x44)) = Fl(S(0x14)) * K(0x005bcf68) + Fl(S(0x2c));
    Fl(S(0x48)) = Fl(S(0x18)) * K(0x005bcf68) + Fl(S(0x30));
    Fl(S(0x4c)) = Fl(S(0x1c)) * K(0x005bcf68) + Fl(S(0x34));
    turret = D(edi + 0x898);
    if (turret != 0)
    {
        if (!(D(edi + 0x484) & 0x8000))
            return 0;
        return VC(turret, 0x13c, 0u, S(0x44));
    }
    uint32_t wl = edi + 0x61c;
    if (orig::fn_004a15f0(wl) == 0)
        return 0;
    uint32_t w = orig::fn_004a15f0(wl);
    return orig::fn_004a2010(S(0x44), w, 0, 0, D(S(0x6c)));
}

static uint32_t fn_004284e0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004284e0(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004284e0(fn_004284e0_Entry, fn_004284e0_Abi)
HOOK(0x004284e0, "fn_004284e0", fn_004284e0_Entry, nullptr);

// 0x00428980 (ecx = AI object; stack: byte aim, byte check_los, byte may_move, a3 (not read), byte face_check; ret 0x14;
// returns al). Vtable slot +0x198 (not executed in recorded runs). The firing cycle: bit 1 of 0x00408800(4, self)+0x1c
// (guess: animation finished) gates the bookkeeping; with a target and `aim` turns towards it (0x0042df70, 45 degrees)
// with the aim animation set (ids 0x23/0x24/0x21/0x22 while 0x23 plays, else 2/3/0/1), otherwise resets the blend
// +0x580/+0x584 to 0.5. Counts down +0x5c8 (pause) and +0x5cc (fire delay); fires through virtual +0x13c when ready
// (facing test virtual +0xf0 180 degrees and line of sight 0x00426fd0 when `face_check`), or triggers the turret /
// weapon (virtual +0x138) while animation 0x23 plays. At the end of a burst re-rolls the delays (+0x5ac..+0x5b0 float,
// +0x5a4..+0x5a8 count) and counts the bursts +0x5b4; after the last one may throw (virtual +0x1d8, game timer
// +0xb84/+0xb88..+0xb90, angle 0x004e81a0 below 0x005bcbb8, animation 0x32..0x34, state 0x6e), fight (0x00427e80),
// close attack (0x00428160), or leave (state 0x7a / 0x0042b8e0, or state 0xa2 / 0x7a / virtual +0x184 when the line of
// sight is lost). Name: guess.
uint32_t fn_00428980(uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t /*a3*/, uint32_t a4)
{
    uint32_t fr[6];  // sub esp, 0x18: fr[0] is [esp+0x10] with ebx, ebp, esi, edi pushed
    auto S = [&](uint32_t o) { return u(fr) + o - 0x10; };
    uint32_t ebp = (D(orig::fn_00408800(4, edi) + 0x1c) >> 1) & 1;
    fn_00428280(S(0x10), edi, edi, 0x005b68fcu);
    const uint32_t esi = 0x23;
    if (D(edi + 0x588) != 0 && static_cast<uint8_t>(a0))
    {
        orig::fn_0042df70(edi, 0x42340000u);  // 45.0
        uint32_t b1 = D(edi + 0x584), b0 = D(edi + 0x580);
        if (D(edi + 0x498) == esi)
        {
            uint32_t loop = (D(edi + 0x484) & 0x1000) ? 1u : 0u;
            VC(edi, 0x1a0, esi, 0x24u, 0x21u, 0x22u, loop, 0u, b0, b1);
        }
        else
        {
            VC(edi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, b0, b1);
        }
    }
    else
    {
        D(edi + 0x580) = 0x3f000000;
        D(edi + 0x584) = 0x3f000000;
    }
    {
        float p = Fl(edi + 0x5c8);
        if (p > K(0x0056c05c) || p != p)
        {
            Fl(edi + 0x5c8) = Fl(edi + 0x5c8) - Fl(0x00604a90);
            return 0;
        }
    }
    bool los_lost = false;
    if (D(edi + 0x498) != esi)
    {
        float w = Fl(edi + 0x5cc);
        bool wait = w > K(0x0056c05c) || w != w;
        if (!wait)
        {
            uint8_t bl = static_cast<uint8_t>(a4);
            uint32_t t = D(edi + 0x588);
            bool fire = true;
            if (t != 0 && bl && !VB(edi, 0xf0, t, 0x43340000u))
            {
                wait = true;
                fire = false;
            }
            if (fire && bl)
            {
                uint32_t tt = D(edi + 0x588);
                if (tt == 0 || !static_cast<uint8_t>(fn_00426fd0(S(0x10), edi, tt, D(edi + 0x904))))
                {
                    los_lost = true;
                    fire = false;
                }
            }
            if (fire)
            {
                uint32_t b1 = D(edi + 0x584), b0 = D(edi + 0x580);
                uint32_t loop = (D(edi + 0x484) & 0x1000) ? 1u : 0u;
                VC(edi, 0x1a0, esi, 0x24u, 0x21u, 0x22u, loop, 0u, b0, b1);
                if (D(edi + 0x588) == 0)
                    return 0;
                VC(edi, 0x13c);
                return 0;
            }
        }
        if (wait)
            Fl(edi + 0x5cc) = Fl(edi + 0x5cc) - Fl(0x00604a90);
    }
    else
    {
        uint32_t turret = D(edi + 0x898);
        if (turret != 0)
        {
            VC(turret, 0x138, 0u);
        }
        else
        {
            uint32_t wl = edi + 0x61c;
            if (orig::fn_004a15f0(wl) != 0)
                VC(orig::fn_004a15f0(wl), 0x138);
        }
    }
    if (!los_lost)
    {
        if (ebp == 0)
            return 0;
        if (static_cast<uint8_t>(a1) && (I(edi + 0x5bc) % 2) == 0)
        {
            uint32_t tt = D(edi + 0x588);
            if (tt == 0 || !static_cast<uint8_t>(fn_00426fd0(S(0x10), edi, tt, D(edi + 0x904))))
                los_lost = true;
        }
    }
    if (los_lost)
    {
        if (D(edi + 0x460) == 0xbd)
            return 0;
        if (D(edi + 0x488) & 0x20000)
            orig::fn_0042bb10(edi);
        uint32_t f = D(edi + 0x484);
        D(edi + 0x5cc) = 0;
        if (f & 0x20000)
        {
            VC(edi, 0x19c, 0x66u, 0u, 0u, 0u, 0u, 0u);
            VC(edi, 0x1b8, 0xa2u);
            return 0;
        }
        if (f & 0x2000000)
        {
            VC(edi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
            VC(edi, 0x1b8, 0x7au);
            return 0;
        }
        VC(edi, 0x184);
        return 0;
    }
    if (I(edi + 0x5bc) > 0)
    {
        if (I(edi + 0x5b8) > 0)
            return 0;
        if (static_cast<uint8_t>(a2) && static_cast<uint8_t>(fn_00428160(edi)))
            return 0;
        uint32_t hi = D(edi + 0x5b0), lo = D(edi + 0x5ac);
        Fl(edi + 0x5cc) = orig::fn_0046e2c0(lo, lo, hi);  // ecx = +0x5ac (a push ecx slot)
        uint32_t n = RandRange(edi, 0x5a8, 0x5a4);
        uint32_t c = D(edi + 0x5bc);
        D(edi + 0x5b8) = n;
        if (static_cast<int32_t>(n) > static_cast<int32_t>(c))
            D(edi + 0x5b8) = c;
        VC(edi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, D(edi + 0x580), D(edi + 0x584));
        return 0;
    }
    int32_t bursts = I(edi + 0x5b4) - 1;
    I(edi + 0x5b4) = bursts;
    if (bursts > 0)
    {
        if (!static_cast<uint8_t>(a2))
            return 0;
        if (D(edi + 0x488) & 0x800)
        {
            VC(edi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
            return 0;
        }
        if (static_cast<uint8_t>(fn_00427e80(edi)))
            return 0;
        VC(edi, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(edi + 0x580), D(edi + 0x584));
        VC(edi, 0x1b8, 0x32u);
        return 0;
    }
    D(edi + 0x484) &= 0xfffffffbu;
    if (D(edi + 0x488) & 0x20000)
        orig::fn_0042bb10(edi);
    if (!static_cast<uint8_t>(a2))
        return 1;
    uint32_t now = D(0x00604a98);  // stored over the may_move slot
    bool tryFallback = true;
    if (D(edi + 0x588) != 0 && VB(edi, 0x1d8))
    {
        uint32_t game = GameP();
        float since = bf(now) - Fl(game + 0xb84);
        if (since > Fl(game + 0xb90) && !(D(edi + 0x484) & 0x20000))
        {
            tryFallback = false;
            orig::fn_00453150(D(edi + 0x588), S(0x1c));
            orig::fn_00453150(edi, S(0x10));
            float h1 = orig::fn_004e83b0(S(0x10));
            float h0 = orig::fn_004e83b0(S(0x1c));
            float x = orig::fn_004e81a0(fb(h0), fb(h1));
            if (x < K(0x005bcbb8))
            {
                game = GameP();
                if (!VB(D(game + 0x2c), 0xdc, D(edi + 0xb50)))
                {
                    game = GameP();
                    D(game + 0xb84) = now;
                    uint32_t ghi = D(game + 0xb8c), glo = D(game + 0xb88);
                    float r = orig::fn_0046e2c0(ghi, glo, ghi);
                    Fl(GameP() + 0xb90) = r;
                    VC(edi, 0x15c);
                    uint32_t id = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 3) >> 15) + 0x32;
                    VC(edi, 0x19c, id, 0u, 0u, 0u, 0u, 0u);
                    VC(edi, 0x1b8, 0x6eu);
                    return 1;
                }
            }
        }
    }
    if (tryFallback)
    {
        if (static_cast<uint8_t>(fn_00427e80(edi)))
            return 1;
        if (static_cast<uint8_t>(fn_00428160(edi)))
            return 1;
    }
    if (D(edi + 0x484) & 0x2000000)
    {
        VC(edi, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
        VC(edi, 0x1b8, 0x7au);
        return 1;
    }
    orig::fn_0042b8e0(0, edi);  // "ecx" is only a push ecx slot (ai_06)
    return 1;
}

static uint32_t fn_00428980_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_00428980(ecx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_00428980(fn_00428980_Entry, fn_00428980_Abi)
HOOK(0x00428980, "fn_00428980", fn_00428980_Entry, nullptr);

namespace
{
// 0x00566ad0 (CRT, st0 in -> st0 out; it stores st0 as a double and calls the CRT acos path 0x00568268 / 0x00566aed):
// called with the original x87 argument convention.
float CI_00566ad0(float x)
{
    static const uint32_t target = 0x00566ad0;
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        fld x
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp r
    }
    return r;
}
}  // namespace

// 0x00428fc0 (ecx = AI object; stack: float aim_window; ret 4; returns al = on target). Vtable slot +0x190 (no direct
// callers). Moves the aim blend weights +0x580 (horizontal) and +0x584 (vertical) towards the target +0x588:
// horizontal from the angle 0x004e81f0 between the AI's heading (0x00453150, 0x004e83b0) and the 2D direction to the
// target (normalised by 0x004e7780): inside [-window, window] it is 1 - (angle + window) / (2 window) and "on target",
// else 1 or 0; vertical from the elevation acos(dz / len) (0x00566ad0) * 0x005bcb8c - 0x005bcb90, clamped by
// 0x005bcf10 / 0x005bcf0c. With the turret aim (flag 1 of +0x48c and a turret +0x898) the turret's virtual +0x148 and
// 0x004580a0 give the points and the weights are set at once when on target; otherwise they step by
// frame time / (2 window * 0x005bcf04) without overshooting. Without a target: clears flag 1 of +0x48c and aims at
// (0.5, 0x005bcb4c). Name: guess.
uint32_t fn_00428fc0(uint32_t edi, uint32_t a0)
{
    uint32_t fr[0xf];  // sub esp, 0x3c: fr[0] is [esp+0x8] with ebx, edi pushed
    auto S = [&](uint32_t o) { return u(fr) + o - 0x8; };
    float window = bf(a0);
    uint8_t ok;  // [esp+0xb] / bl
    float Y;     // st0
    By(S(0xb)) = 0;
    if (D(edi + 0x588) == 0)
    {
        D(edi + 0x48c) &= 0xfffffffeu;
        Y = K(0x005bcb4c);
        Fl(S(0xc)) = 0.5f;
        ok = 1;
    }
    else
    {
        uint32_t turret = D(edi + 0x898);
        if ((By(edi + 0x48c) & 1) && turret != 0)
        {
            VC(turret, 0x148, S(0x2c), 0u);
            orig::fn_004580a0(S(0x20), D(edi + 0x588), S(0x2c), 0x43b40000u, 1);  // 360.0
            VC(D(edi + 0x588), 0x54, S(0x14));
            Fl(S(0x28)) = (Fl(S(0x1c)) * K(0x005bcbe8) + Fl(S(0x28))) * K(0x005bcf14);
        }
        else
        {
            VC(edi, 0x54, S(0x2c));
            VC(D(edi + 0x588), 0x54, S(0x20));
        }
        Fl(S(0x14)) = Fl(S(0x20)) - Fl(S(0x2c));
        Fl(S(0x18)) = Fl(S(0x24)) - Fl(S(0x30));
        orig::fn_004e7780(S(0x14), S(0x14));  // length popped
        D(S(0x1c)) = 0;
        orig::fn_00453150(edi, S(0x38));
        float hs = orig::fn_004e83b0(S(0x38));
        float hd = orig::fn_004e83b0(S(0x14));
        float ang = orig::fn_004e81f0(fb(hd), fb(hs));
        float neg = -window;
        ok = 0;
        if (!(ang < neg || ang != ang) && !(ang > window || ang != ang))
        {
            By(S(0xb)) = 1;
            ok = 1;
            Fl(S(0xc)) = K(0x005bcb2c) - (ang + window) / (window + window);
        }
        else
        {
            Fl(S(0xc)) = 1.0f;
            if (!(ang < neg))
                Fl(S(0xc)) = 0.0f;
        }
        Fl(S(0x14)) = Fl(S(0x20)) - Fl(S(0x2c));
        Fl(S(0x18)) = Fl(S(0x24)) - Fl(S(0x30));
        float dz = Fl(S(0x28)) - Fl(S(0x34));
        Fl(S(0x10)) = dz;
        Fl(S(0x1c)) = dz;
        float len = Normalize3(S(0x14), S(0x14));
        float e = CI_00566ad0(Fl(S(0x10)) / len) * K(0x005bcb8c) - K(0x005bcb90);
        Fl(S(0x10)) = e;
        if (e > K(0x005bcf10))
        {
            Y = K(0x0056c05c);
            By(S(0xb)) = 0;
            ok = 0;
        }
        else if (Fl(S(0x10)) < K(0x005bcf0c))
        {
            Y = K(0x005bcb2c);
            By(S(0xb)) = 0;
            ok = 0;
        }
        else
        {
            Y = K(0x005bcb2c) - (Fl(S(0x10)) + K(0x005bcf10)) * K(0x005bcf08);  // bl keeps its value
        }
    }
    if ((By(edi + 0x48c) & 1) && ok)
    {
        Fl(edi + 0x584) = Y;
        D(edi + 0x580) = D(S(0xc));
        return ok;
    }
    float step = (window + window) * K(0x005bcf04);
    float r = Fl(0x00604a90) / step;
    if (Fl(S(0xc)) < Fl(edi + 0x580))
    {
        float v = Fl(edi + 0x580) - r;
        Fl(edi + 0x580) = v;
        if (v < Fl(S(0xc)))
            D(edi + 0x580) = D(S(0xc));
    }
    else
    {
        float v = r + Fl(edi + 0x580);
        Fl(edi + 0x580) = v;
        if (v > Fl(S(0xc)))
            D(edi + 0x580) = D(S(0xc));
    }
    if (Y < Fl(edi + 0x584))
    {
        float v = Fl(edi + 0x584) - r;
        Fl(edi + 0x584) = v;
        if (v < Y)
            Fl(edi + 0x584) = Y;
    }
    else
    {
        float v = r + Fl(edi + 0x584);
        Fl(edi + 0x584) = v;
        if (v > Y)
            Fl(edi + 0x584) = Y;
    }
    return ok;
}

static uint32_t fn_00428fc0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00428fc0(ecx, a0);
}
ABI_ENTRY_00428fc0(fn_00428fc0_Entry, fn_00428fc0_Abi)
HOOK(0x00428fc0, "fn_00428fc0", fn_00428fc0_Entry, nullptr);

// 0x004292f0 (eax = fire animation id, ebx = idle animation id, esi = AI object; stack: a0..a7, the fire / idle
// animation sets (a0..a2 / a3..a5) and two blend floats; ret 0x20; returns al = firing). Caller GenericAI_Update (not
// executed in recorded runs). The turret (+0x898) firing cycle, the counterpart of 0x00428980: false without a turret.
// The turret's virtual +0x168 gives its kind (1 uses the four-animation virtual +0x1a0, others +0x19c); kind 2 waits
// for bit 1 of 0x00408800(4, self)+0x1c. Counts down the pause +0x5c8 (idle animation) and the fire delay +0x5cc;
// fires (true) while the burst counters +0x5bc / +0x5b8 allow; re-rolls them from the turret tables +0x8b4..+0x8c0 or
// +0x8e4..+0x8f0 (0x004e7740, 0x0046e2c0) and counts the bursts +0x5b4 (pause +0x8a4..+0x8a8 / +0x8d4..+0x8d8, or
// clears flag 4 of +0x484 after the last one).
uint32_t fn_004292f0(uint32_t edi, uint32_t ebx, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                     uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t turret = D(esi + 0x898);
    if (turret == 0)
        return 0;
    uint32_t kind = VC(turret, 0x168);
    auto Idle = [&]() {
        if (kind == 1)
            VC(esi, 0x1a0, ebx, a3, a4, a5, kind, 0u, a6, a7);
        else
            VC(esi, 0x19c, ebx, 1u, 0u, 0u, 0u, 0u);
    };
    auto Fire = [&]() -> uint32_t {
        if (kind == 1)
            VC(esi, 0x1a0, edi, a0, a1, a2, kind, 0u, a6, a7);
        else
            VC(esi, 0x19c, edi, 0u, 0u, 0u, 0u, 0u);
        return 1;
    };
    {
        float p = Fl(esi + 0x5c8);
        if (p > K(0x0056c05c) || p != p)
        {
            Idle();
            Fl(esi + 0x5c8) = Fl(esi + 0x5c8) - Fl(0x00604a90);
            return 0;
        }
    }
    uint32_t ready = (kind == 2) ? ((D(orig::fn_00408800(4, esi) + 0x1c) >> 1) & 1) : 1u;
    if (D(esi + 0x498) != edi)
    {
        float w = Fl(esi + 0x5cc);
        if (w > K(0x0056c05c) || w != w)
        {
            Fl(esi + 0x5cc) = Fl(esi + 0x5cc) - Fl(0x00604a90);
            return 0;
        }
        if (I(esi + 0x5bc) > 0 && I(esi + 0x5b8) > 0)
            return Fire();
    }
    int32_t left = I(esi + 0x5bc);
    if ((left > 0 && I(esi + 0x5b8) > 0) || ready == 0)
        return Fire();
    if (left > 0)
    {
        uint32_t n, lo, hi;
        if (kind == 1)
        {
            n = orig::fn_004e7740(D(esi + 0x8b8), D(esi + 0x8b4));
            D(esi + 0x5b8) = n;
            hi = D(esi + 0x8c0);
            lo = D(esi + 0x8bc);
            Fl(esi + 0x5cc) = orig::fn_0046e2c0(lo, lo, hi);
        }
        else
        {
            n = orig::fn_004e7740(D(esi + 0x8e8), D(esi + 0x8e4));
            D(esi + 0x5b8) = n;
            hi = D(esi + 0x8f0);
            lo = D(esi + 0x8ec);
            Fl(esi + 0x5cc) = orig::fn_0046e2c0(ready, lo, hi);
        }
        uint32_t c = D(esi + 0x5bc);
        if (I(esi + 0x5b8) > static_cast<int32_t>(c))
            D(esi + 0x5b8) = c;
    }
    else
    {
        int32_t b = I(esi + 0x5b4) - 1;
        I(esi + 0x5b4) = b;
        if (b > 0)
        {
            if (kind == 1)
            {
                uint32_t lo = D(esi + 0x8a4);
                Fl(esi + 0x5c8) = orig::fn_0046e2c0(lo, lo, D(esi + 0x8a8));
            }
            else
            {
                Fl(esi + 0x5c8) = orig::fn_0046e2c0(ready, D(esi + 0x8d4), D(esi + 0x8d8));
            }
        }
        else
        {
            D(esi + 0x484) &= 0xfffffffbu;
        }
    }
    if (kind == 1)
        VC(esi, 0x1a0, ebx, a3, a4, a5, 1u, 0u, a6, a7);
    else
        VC(esi, 0x19c, ebx, 1u, 0u, 0u, 0u, 0u);
    return 0;
}

static uint32_t fn_004292f0_Abi(uint32_t eax, uint32_t ebx, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2,
                                uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    return fn_004292f0(eax, ebx, esi, a0, a1, a2, a3, a4, a5, a6, a7);
}
ABI_ENTRY_004292f0(fn_004292f0_Entry, fn_004292f0_Abi)
HOOK(0x004292f0, "fn_004292f0", fn_004292f0_Entry, nullptr);

// 0x004295d0 (ecx = AI object). Vtable slot +0x184 (no direct callers). Unless dead (Object_IsDead 0x00454780): clears
// the pause +0x5c8 and picks a random point around the target position +0x60c (x and y +-0x005bcbe8 scaled by
// 0x005bcea4, z copied) into +0x934. With a group at +0xb08: when 0x00454c20 holds, asks the grid game+0xaa4
// (0x0046f5b0) for the point ahead of the AI (heading 0x00453150 * 0x005bcc04 + position) and enters state 0xbc with
// +0x4ac = 0x3c; otherwise hands over to 0x0042b480(self, 0) and returns. Without a group: 0x0046f6d0 (grid, point
// +0x934) and state 0x3c. Then 0x00427210 (animation), a random re-search delay +0x490 (two ranges by flag 2 of
// +0x484), 0x004246a0, and copies +0x934 to +0x960. Name: guess (moves towards the target).
uint32_t fn_004295d0(uint32_t ebp)
{
    uint32_t fr[7];  // sub esp, 0x1c: fr[0] is [esp+0x10] with ebp, esi, ebx, edi pushed
    auto F = [&](uint32_t o) { return u(fr) + o; };
    if (static_cast<uint8_t>(orig::fn_00454780(ebp)))
        return 1;
    D(ebp + 0x5c8) = 0;
    {
        int32_t r = static_cast<int32_t>(Rand());
        Fl(ebp + 0x934) = static_cast<float>(r) * K(0x005bcea4) - K(0x005bcbe8) + Fl(ebp + 0x60c);
    }
    {
        int32_t r = static_cast<int32_t>(Rand());
        D(ebp + 0x93c) = D(ebp + 0x614);
        Fl(ebp + 0x938) = static_cast<float>(r) * K(0x005bcea4) - K(0x005bcbe8) + Fl(ebp + 0x610);
    }
    if (D(ebp + 0xb08) != 0)
    {
        if (!static_cast<uint8_t>(orig::fn_00454c20(ebp)))
            return orig::fn_0042b480(ebp, 0);
        orig::fn_00453150(ebp, F(0x10));
        uint32_t t = VC(ebp, 0x54, F(0x4));
        uint32_t game = GameP();
        Fl(F(0x4)) = Fl(F(0x10)) * K(0x005bcc04) + Fl(F(0x4));
        Fl(F(0x8)) = Fl(F(0x14)) * K(0x005bcc04) + Fl(F(0x8));
        orig::fn_0046f5b0(t & 0xffffff00u, 0, F(0x4), ebp, D(game + 0xaa4), 0, 0);  // xor al, al; xor ecx, ecx
        D(ebp + 0x4ac) = 0x3c;
        VC(ebp, 0x1b8, 0xbcu);
    }
    else
    {
        orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), ebp + 0x934, 0, 0, 0, 0, 0);
        VC(ebp, 0x1b8, 0x3cu);
    }
    orig::fn_00427210(ebp);
    {
        int32_t r = static_cast<int32_t>(Rand());
        float f;
        if (By(ebp + 0x484) & 2)
            f = static_cast<float>(r) * K(0x005bcea0) + K(0x005bce9c);
        else
            f = static_cast<float>(r) * K(0x005bce98) + K(0x005bcd2c);
        Fl(ebp + 0x490) = f;
    }
    orig::fn_004246a0(ebp);
    uint32_t x = D(ebp + 0x934), y = D(ebp + 0x938), z = D(ebp + 0x93c);
    D(ebp + 0x960) = x;
    D(ebp + 0x964) = y;
    D(ebp + 0x968) = z;
    return x;
}

static uint32_t fn_004295d0_Abi(uint32_t ecx)
{
    return fn_004295d0(ecx);
}
ABI_ENTRY_004295d0(fn_004295d0_Entry, fn_004295d0_Abi)
HOOK(0x004295d0, "fn_004295d0", fn_004295d0_Entry, nullptr);

// 0x00429790 (ecx = AI object; returns st0). Vtable slot +0x84 (no direct callers): the float at +0x944.
float fn_00429790(uint32_t self)
{
    return Fl(self + 0x944);
}

static float fn_00429790_Abi(uint32_t ecx)
{
    return fn_00429790(ecx);
}
ABI_ENTRY_00429790(fn_00429790_Entry, fn_00429790_Abi)
HOOK(0x00429790, "fn_00429790", fn_00429790_Entry, nullptr);

// 0x004297a0 (ecx = AI object; stack: float* out0, float* out1, float* out2; ret 0xc). Vtable slot +0x80; caller
// 0x00486cb0. *out2 = 5.0; *out1 and *out0 get +0x948 (or +0x94c with flag 2 of +0x484); while holding +0x570 in
// state 0x96 / 0x97 they get 0x0044b4f0 instead, or +0x954 (flag 1 of +0x488) / the above when that is 0.
uint32_t fn_004297a0(uint32_t esi, uint32_t out0, uint32_t out1, uint32_t out2)
{
    D(out2) = 0x40a00000;  // 5.0
    uint32_t held = D(esi + 0x570);
    uint32_t state = D(esi + 0x460);
    if (held != 0 && (state == 0x97 || state == 0x96))
    {
        float f = orig::fn_0044b4f0(esi, held);
        Fl(out1) = f;
        if (f == K(0x0056c05c))
        {
            if (By(esi + 0x488) & 1)
            {
                uint32_t v = D(esi + 0x954);
                D(out1) = v;
                D(out0) = v;
                return v;
            }
            if (By(esi + 0x484) & 2)
            {
                uint32_t v = D(esi + 0x94c);
                D(out1) = v;
                D(out0) = v;
                return v;
            }
            D(out1) = D(esi + 0x948);
        }
        uint32_t v = D(out1);
        D(out0) = v;
        return v;
    }
    uint32_t v = (By(esi + 0x484) & 2) ? D(esi + 0x94c) : D(esi + 0x948);
    D(out1) = v;
    D(out0) = v;
    return v;
}

static uint32_t fn_004297a0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004297a0(ecx, a0, a1, a2);
}
ABI_ENTRY_004297a0(fn_004297a0_Entry, fn_004297a0_Abi)
HOOK(0x004297a0, "fn_004297a0", fn_004297a0_Entry, nullptr);

// 0x00429870 (edi = AI object; stack: byte physics; ret 4). Callers 0x0042c410, 0x0043d180 (not executed in recorded
// runs). Detaches the object at +0xab4 (Object_Detach 0x00453b20) and, when `physics`, re-enables its physics
// (Object_setPhysicsEnabled 0x004577b0(object, 1): a tail jump in the original that reuses the argument slot).
uint32_t fn_00429870(uint32_t edi, uint32_t a0)
{
    uint32_t r = orig::fn_00453b20(D(edi + 0xab4));
    if (!static_cast<uint8_t>(a0))
        return r & 0xffffff00u;
    return static_cast<uint32_t>(orig::fn_004577b0(D(edi + 0xab4), 1));
}

static uint32_t fn_00429870_Abi(uint32_t edi, uint32_t a0)
{
    return fn_00429870(edi, a0);
}
ABI_ENTRY_00429870(fn_00429870_Entry, fn_00429870_Abi)
HOOK(0x00429870, "fn_00429870", fn_00429870_Entry, nullptr);

namespace
{
// 0x0042d650 (eax, ecx, esi; stack: 1 argument). It has no ret of its own: it ends with a tail jump to virtual +0x1b8
// of esi after overwriting its argument with 0x66, and that method pops the argument (ret 4). The generated wrapper
// assumes the caller pops, so it is called by hand. Returns eax and edx as left by the tail-called method.
uint64_t Call_0042d650(uint32_t eax_, uint32_t ecx_, uint32_t esi_, uint32_t a0)
{
    static const uint32_t target = 0x0042d650;
    uint32_t outA, outD;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a0
        mov eax, eax_
        mov ecx, ecx_
        mov esi, esi_
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov outA, eax
        mov outD, edx
    }
    return (static_cast<uint64_t>(outD) << 32) | outA;
}
}  // namespace

// 0x004298a0 (edi = AI object; stack: a0 (not read), a1, a2; ret 0xc; returns eax = the new object, edx as left by
// 0x0042d650). Caller 0x0042d710 (not executed in recorded runs). Creates a "grunt_ladder" object
// (ObjectSystem_CreateObject 0x0045f020, game+0xa84, type 0x3ff, 0xaa), stores it at +0xab4, sets it up with 0x0042d650
// (eax = a2, ecx = self, esi = object; a1) and takes a reference.
uint64_t fn_004298a0(uint32_t edi, uint32_t /*a0*/, uint32_t a1, uint32_t a2)
{
    uint32_t game = GameP();
    uint32_t obj = orig::fn_0045f020(0, 0x005b68d0u /* "grunt_ladder" */, D(game + 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa,
                                     0);
    D(edi + 0xab4) = obj;
    uint64_t r = Call_0042d650(a2, edi, obj, a1);
    I(D(edi + 0xab4) + 0x28) += 1;
    uint32_t eax = D(edi + 0xab4);
    return (r & 0xffffffff00000000ull) | eax;
}

static uint64_t fn_004298a0_Abi(uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004298a0(edi, a0, a1, a2);
}
ABI_ENTRY_004298a0(fn_004298a0_Entry, fn_004298a0_Abi)
HOOK(0x004298a0, "fn_004298a0", fn_004298a0_Entry, nullptr);

// 0x00429900 (eax = first tag; stack: AI object, last tag; ret 8; returns eax = the chosen path's object or 0). Caller
// 0x00446460 (not executed in recorded runs). Rate-limited by the game timer pair +0xaf8 / +0xaf4 (now 0x00604a98):
// collects the marker spline groups of the tags [first, last] (0x0044c030) that are neither 0x0044b7b0 nor 0x0044b7e0
// into a "ladder paths" heap block (PMS_heapAlloc 0x004e07f0, heap 0x0063a210), picks one at random, marks it
// (0x0044b7f0, cl = 1) and returns 0x0044ad40 of it; on success restarts the timer with a random delay in
// [+0xaec, +0xaf0] and sets flag 0x80000 of the AI's +0x484. Name: guess from the string.
uint32_t fn_00429900(uint32_t esi, uint32_t ai, uint32_t last)
{
    uint32_t now = D(0x00604a98);  // [esp+4]
    uint32_t game = GameP();
    if (bf(now) - Fl(game + 0xaf8) < Fl(game + 0xaf4))
        return 0;
    uint32_t buf = orig::fn_004e07f0(0x0063a210u, 0xf, (last - esi) * 4 + 4, 2, 0x005b68c0u /* "ladder paths" */);
    uint32_t n = 0;
    uint32_t res;
    if (esi <= last)
    {
        do
        {
            uint32_t g = orig::fn_0044c030(0, esi);
            if (g != 0 && !static_cast<uint8_t>(orig::fn_0044b7b0(g)) && !static_cast<uint8_t>(orig::fn_0044b7e0(g)))
            {
                D(buf + n * 4) = g;
                ++n;
            }
            ++esi;
        } while (esi <= last);
    }
    if (n != 0)
    {
        uint32_t idx = static_cast<uint32_t>(static_cast<int32_t>(Rand() * n) >> 15);
        orig::fn_0044b7f0(D(buf + idx * 4), 1);  // mov cl, 1
        res = orig::fn_0044ad40(D(buf + idx * 4));
    }
    else
    {
        res = 0;
    }
    orig::fn_004e08d0(0x0063a210u, buf);
    if (res == 0)
        return 0;
    game = GameP();
    D(game + 0xaf8) = now;
    float hi = Fl(game + 0xaf0);
    float lo = Fl(game + 0xaec);
    float r = static_cast<float>(static_cast<int32_t>(Rand()));
    Fl(GameP() + 0xaf4) = r * (hi - lo) * K(0x005bcbc0) + lo;
    D(ai + 0x484) |= 0x80000;
    return res;
}

static uint32_t fn_00429900_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_00429900(eax, a0, a1);
}
ABI_ENTRY_00429900(fn_00429900_Entry, fn_00429900_Abi)
HOOK(0x00429900, "fn_00429900", fn_00429900_Entry, nullptr);

// 0x00429a40 (eax: passed through, ecx = AI object; returns eax). Calls 0x004a0e00 (eax, esi = self + 0x61c, the
// weapon list).
uint32_t fn_00429a40(uint32_t eax, uint32_t self)
{
    return orig::fn_004a0e00(eax, self + 0x61c);
}

static uint32_t fn_00429a40_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00429a40(eax, ecx);
}
ABI_ENTRY_00429a40(fn_00429a40_Entry, fn_00429a40_Abi)
HOOK(0x00429a40, "fn_00429a40", fn_00429a40_Entry, nullptr);

// 0x00429a50 (esi = AI object; returns al). Callers 0x0042a7b0, 0x0042ba30, GenericAI_Update. When game+0xcbe is set and
// the counter game+0xc48 is below game+0xc4c: counts up, sets flag 0x400 of +0x488, clears 0x10000 of +0x484, calls
// 0x0042d1b0 (eax = ecx = game+0xc44; self, 0, 0, 0, 0) and virtual +0x174 (true).
uint32_t fn_00429a50(uint32_t esi)
{
    uint32_t game = GameP();
    if (By(game + 0xcbe) == 0)
        return 0;
    uint32_t c = D(game + 0xc48);
    if (c >= D(game + 0xc4c))
        return 0;
    D(game + 0xc48) = c + 1;
    D(esi + 0x488) |= 0x400;
    D(esi + 0x484) &= 0xfffeffffu;
    uint32_t g = D(game + 0xc44);
    orig::fn_0042d1b0(g, g, esi, 0, 0, 0, 0);
    VC(esi, 0x174);
    return 1;
}

static uint32_t fn_00429a50_Abi(uint32_t esi)
{
    return fn_00429a50(esi);
}
ABI_ENTRY_00429a50(fn_00429a50_Entry, fn_00429a50_Abi)
HOOK(0x00429a50, "fn_00429a50", fn_00429a50_Entry, nullptr);

namespace
{
// 0x0044b710 (eax = optional matrix out, esi = heading out, edi = ladder object; stack: position out). It has no ret
// of its own: it ends with a tail jump to virtual +0x54 of the ladder's last element, which pops the argument (ret 4).
// The generated wrapper does not model the argument, so it is called by hand.
uint32_t Call_0044b710(uint32_t eax_, uint32_t esi_, uint32_t edi_, uint32_t a0)
{
    static const uint32_t target = 0x0044b710;
    uint32_t r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a0
        mov eax, eax_
        mov esi, esi_
        mov edi, edi_
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov r, eax
    }
    return r;
}
}  // namespace

// 0x00429ac0 (stack: AI object; ret 4; returns al). Callers 0x0042a7b0, 0x0042ba30. Sends the AI to the nearest ladder:
// needs virtual +0x1c8 and fewer users in game+0xa84 list +0xf14 (virtual +0x34) than game+0xc54; walks the ladders
// of list +0xee4 that are not 0x0044b7e0 and are 0x0044b760, keeping the closest bottom point (0x0044b710) within the
// range +0x5dc in 2D. The winner is booked in the grid (0x0046f6b0, dl = 1), its point / heading / matrix go to +0x934 /
// +0x908 / +0xa28, it becomes the held object +0x570 (0x00426640, marked by 0x0044b7f0), then the climb animation
// (0x00427210), state 0xac, the user list (ObjectList_Link 0x004546d0) and flag 0x40000 of +0x488 (true). Name: guess.
uint32_t fn_00429ac0(uint32_t a0)
{
    uint32_t st[0x14];  // sub esp, 0x48 plus the return address and the argument: st[0] is [esp+0x10]
    auto S = [&](uint32_t o) { return u(st) + o - 0x10; };
    uint32_t ebp = a0;
    D(S(0x14)) = 0;
    D(S(0x18)) = 0;
    if (!VB(ebp, 0x1c8))
        return 0;
    uint32_t game = GameP();
    uint32_t users = VC(D(D(game + 0xa84) + 0xf14), 0x34);
    if (users >= D(GameP() + 0xc54))
        return 0;
    {
        float r = Fl(ebp + 0x5dc);
        Fl(S(0x10)) = r * r;
    }
    D(S(0x5c)) = 0;  // best ladder, stored over the argument slot
    VC(ebp, 0x54, S(0x4c));
    game = GameP();
    uint32_t edi = orig::fn_004e7340(D(D(game + 0xa84) + 0xee4), S(0x14));
    if (edi == 0)
    {
        uint32_t node = D(S(0x18));
        if (node != 0)
            D(node + 0x18) -= 1;
        return 0;
    }
    do
    {
        if (!static_cast<uint8_t>(orig::fn_0044b7e0(edi)) && static_cast<uint8_t>(orig::fn_0044b760(edi)))
        {
            Call_0044b710(0, S(0x28), edi, S(0x1c));
            float dy = Fl(S(0x20)) - Fl(S(0x50));
            float dx = Fl(S(0x1c)) - Fl(S(0x4c));
            float d = dx * dx + dy * dy;
            if (!(d > Fl(S(0x10)) || d != d))
            {
                Fl(S(0x10)) = d;
                D(S(0x40)) = D(S(0x1c));
                D(S(0x44)) = D(S(0x20));
                D(S(0x48)) = D(S(0x24));
                D(S(0x5c)) = edi;
                D(S(0x34)) = D(S(0x28));
                D(S(0x38)) = D(S(0x2c));
                D(S(0x3c)) = D(S(0x30));
            }
        }
        edi = orig::fn_004e73c0(S(0x14));
    } while (edi != 0);
    if (D(S(0x5c)) == 0)
    {
        uint32_t node = D(S(0x18));
        if (node != 0)
            D(node + 0x18) -= 1;
        return 0;
    }
    game = GameP();
    uint32_t grid = D(game + 0xaa4);
    orig::fn_0046f6b0(grid & 0xffffff00u, S(0x34) & 0xffffff00u, (game & 0xffffff00u) | 1, S(0x40), grid, ebp,
                      S(0x34));  // al = 0, cl = 0, dl = 1
    edi = D(S(0x5c));
    Call_0044b710(ebp + 0xa28, ebp + 0x908, edi, ebp + 0x934);
    fn_00426640(edi, ebp);
    D(ebp + 0x484) |= 0x80000;
    orig::fn_0044b7f0(D(ebp + 0x570), 1);  // mov cl, 1
    orig::fn_00427210(ebp);
    VC(ebp, 0x1b8, 0xacu);
    game = GameP();
    orig::fn_004546d0(D(D(game + 0xa84) + 0xf14), ebp);
    D(ebp + 0x488) |= 0x40000;
    uint32_t node = D(S(0x18));
    if (node != 0)
        D(node + 0x18) -= 1;
    return 1;
}

static uint32_t fn_00429ac0_Abi(uint32_t a0)
{
    return fn_00429ac0(a0);
}
ABI_ENTRY_00429ac0(fn_00429ac0_Entry, fn_00429ac0_Abi)
HOOK(0x00429ac0, "fn_00429ac0", fn_00429ac0_Entry, nullptr);

// 0x00429ce0 (stack: AI object; ret 4; returns al). Callers 0x0042a7b0, 0x0042ba30. The twin of 0x00429ac0 for the
// other end of the ladders: the candidates must be 0x0044b7b0 (instead of 0x0044b760); the winner is held
// (0x00426640, 0x0044b7f0) before its point is read (0x0044b710), and the state is 0xaa. Name: guess.
uint32_t fn_00429ce0(uint32_t a0)
{
    uint32_t st[0x14];  // sub esp, 0x48 plus the return address and the argument: st[0] is [esp+0x10]
    auto S = [&](uint32_t o) { return u(st) + o - 0x10; };
    uint32_t ebp = a0;
    D(S(0x14)) = 0;
    D(S(0x18)) = 0;
    if (!VB(ebp, 0x1c8))
        return 0;
    uint32_t game = GameP();
    uint32_t users = VC(D(D(game + 0xa84) + 0xf14), 0x34);
    if (users >= D(GameP() + 0xc54))
        return 0;
    {
        float r = Fl(ebp + 0x5dc);
        Fl(S(0x10)) = r * r;
    }
    D(S(0x5c)) = 0;  // best ladder, stored over the argument slot
    VC(ebp, 0x54, S(0x4c));
    game = GameP();
    uint32_t edi = orig::fn_004e7340(D(D(game + 0xa84) + 0xee4), S(0x14));
    if (edi == 0)
    {
        uint32_t node = D(S(0x18));
        if (node != 0)
            D(node + 0x18) -= 1;
        return 0;
    }
    do
    {
        if (!static_cast<uint8_t>(orig::fn_0044b7e0(edi)) && static_cast<uint8_t>(orig::fn_0044b7b0(edi)))
        {
            Call_0044b710(0, S(0x28), edi, S(0x1c));
            float dy = Fl(S(0x20)) - Fl(S(0x50));
            float dx = Fl(S(0x1c)) - Fl(S(0x4c));
            float d = dx * dx + dy * dy;
            if (!(d > Fl(S(0x10)) || d != d))
            {
                Fl(S(0x10)) = d;
                D(S(0x40)) = D(S(0x1c));
                D(S(0x44)) = D(S(0x20));
                D(S(0x48)) = D(S(0x24));
                D(S(0x5c)) = edi;
                D(S(0x34)) = D(S(0x28));
                D(S(0x38)) = D(S(0x2c));
                D(S(0x3c)) = D(S(0x30));
            }
        }
        edi = orig::fn_004e73c0(S(0x14));
    } while (edi != 0);
    if (D(S(0x5c)) == 0)
    {
        uint32_t node = D(S(0x18));
        if (node != 0)
            D(node + 0x18) -= 1;
        return 0;
    }
    game = GameP();
    uint32_t grid = D(game + 0xaa4);
    orig::fn_0046f6b0(grid & 0xffffff00u, S(0x34) & 0xffffff00u, (game & 0xffffff00u) | 1, S(0x40), grid, ebp,
                      S(0x34));  // al = 0, cl = 0, dl = 1
    edi = D(S(0x5c));
    fn_00426640(edi, ebp);
    D(ebp + 0x484) |= 0x80000;
    orig::fn_0044b7f0(D(ebp + 0x570), 1);  // mov cl, 1
    Call_0044b710(ebp + 0xa28, ebp + 0x908, edi, ebp + 0x934);
    orig::fn_00427210(ebp);
    VC(ebp, 0x1b8, 0xaau);
    game = GameP();
    orig::fn_004546d0(D(D(game + 0xa84) + 0xf14), ebp);
    D(ebp + 0x488) |= 0x40000;
    uint32_t node = D(S(0x18));
    if (node != 0)
        D(node + 0x18) -= 1;
    return 1;
}

static uint32_t fn_00429ce0_Abi(uint32_t a0)
{
    return fn_00429ce0(a0);
}
ABI_ENTRY_00429ce0(fn_00429ce0_Entry, fn_00429ce0_Abi)
HOOK(0x00429ce0, "fn_00429ce0", fn_00429ce0_Entry, nullptr);

// 0x00429f00 (ebx = AI object; stack: byte need_target; ret 4; returns eax = the chosen object or 0). Caller 0x0042a7b0
// (not executed in recorded runs). Picks the closest object of game+0xa84 list +0xed4 whose virtual +0x138 is 3 and
// virtual +0x148(0) holds, that is not in the history +0x918 / +0x91c and lies within the range +0x5dc (2D); with a
// target +0x588 it must also be within +0x5d8 of the target, be set up by its virtual +0x150 (point, out, 0, self,
// target, 0), pass 0x0042b860 (80.0) and have a line of sight to the target from 0x005bcbdc above it (0x00426fd0).
// Needs virtual +0x1f4; without a target and with `need_target` returns 0. Guess: chooses cover / a turret to use.
uint32_t fn_00429f00(uint32_t ebx, uint32_t a0)
{
    uint32_t st[0x12];  // sub esp, 0x40 plus the return address and the argument: st[0] is [esp+0xc] (3 pushes)
    auto S = [&](uint32_t o) { return u(st) + o - 0xc; };
    D(S(0x50)) = a0;
    D(S(0x14)) = 0;
    D(S(0x18)) = 0;
    if (D(ebx + 0x588) == 0 && static_cast<uint8_t>(a0))
        return 0;
    if (!VB(ebx, 0x1f4))
        return 0;
    VC(ebx, 0x54, S(0x28));
    uint32_t game = GameP();
    D(S(0xc)) = 0;  // best object
    uint32_t ebp = orig::fn_004e7340(D(D(game + 0xa84) + 0xed4), S(0x14));
    while (ebp != 0)
    {
        if (VC(ebp, 0x138) == 3 && VB(ebp, 0x148, 0u) && ebp != D(ebx + 0x918) && ebp != D(ebx + 0x91c))
        {
            VC(ebp, 0x54, S(0x1c));
            float dy = Fl(S(0x20)) - Fl(S(0x2c));
            float dx = Fl(S(0x1c)) - Fl(S(0x28));
            float d = dx * dx + dy * dy;
            Fl(S(0x50)) = d;
            float r = Fl(ebx + 0x5dc);
            bool ok = !(Fl(S(0x50)) > r * r);
            if (ok && D(S(0xc)) != 0)
                ok = Fl(S(0x10)) > Fl(S(0x50)) || Fl(S(0x10)) != Fl(S(0x10)) || Fl(S(0x50)) != Fl(S(0x50));
            if (ok)
            {
                uint32_t t = D(ebx + 0x588);
                if (t != 0)
                {
                    VC(t, 0x54, S(0x34));
                    float ey = Fl(S(0x20)) - Fl(S(0x38));
                    float ex = Fl(S(0x1c)) - Fl(S(0x34));
                    float q = Fl(ebx + 0x5d8);
                    float e = ex * ex + ey * ey;
                    ok = !(q * q < e);
                    if (ok)
                    {
                        VC(ebp, 0x150, S(0x1c), S(0x40), 0u, ebx, D(ebx + 0x588), 0u);
                        ok = static_cast<uint8_t>(
                                 orig::fn_0042b860(D(ebx + 0x588), S(0x40), S(0x1c), ebx, 0x42a00000u)) != 0;  // 80.0
                    }
                    if (ok)
                    {
                        Fl(S(0x24)) = Fl(S(0x24)) + K(0x005bcbdc);
                        ok = static_cast<uint8_t>(fn_00426fd0(S(0x1c), ebx, D(ebx + 0x588), ebp)) != 0;
                    }
                }
                if (ok)
                {
                    D(S(0xc)) = ebp;
                    D(S(0x10)) = D(S(0x50));
                }
            }
        }
        ebp = orig::fn_004e73c0(S(0x14));
    }
    uint32_t node = D(S(0x18));
    if (node != 0)
        D(node + 0x18) -= 1;
    return D(S(0xc));
}

static uint32_t fn_00429f00_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_00429f00(ebx, a0);
}
ABI_ENTRY_00429f00(fn_00429f00_Entry, fn_00429f00_Abi)
HOOK(0x00429f00, "fn_00429f00", fn_00429f00_Entry, nullptr);

// 0x0042a100 (ebx = AI object; stack: byte need_target; ret 4; returns eax = the chosen turret or 0). Caller 0x0042a7b0
// (not executed in recorded runs). The turret variant of 0x00429f00: needs virtual +0x1f0, walks the turret list
// game+0xa84 +0xea8 for free turrets (virtual +0x158(0)) within the range +0x5dc, closest first; with a target +0x588
// the turret must be within +0x5d8 of it, pass 0x0042b860 with the turret's heading (0x00453150, 65.0) and have a line
// of sight from 0x005bcbdc above it (0x00426fd0).
uint32_t fn_0042a100(uint32_t ebx, uint32_t a0)
{
    uint32_t st[0x12];  // sub esp, 0x40 plus the return address and the argument: st[0] is [esp+0xc] (3 pushes)
    auto S = [&](uint32_t o) { return u(st) + o - 0xc; };
    D(S(0x50)) = a0;
    D(S(0x14)) = 0;
    D(S(0x18)) = 0;
    if (D(ebx + 0x588) == 0 && static_cast<uint8_t>(a0))
        return 0;
    if (!VB(ebx, 0x1f0))
        return 0;
    VC(ebx, 0x54, S(0x28));
    uint32_t game = GameP();
    D(S(0xc)) = 0;  // best turret
    uint32_t ebp = orig::fn_004e7340(D(D(game + 0xa84) + 0xea8), S(0x14));
    while (ebp != 0)
    {
        if (VB(ebp, 0x158, 0u))
        {
            VC(ebp, 0x54, S(0x1c));
            float dy = Fl(S(0x20)) - Fl(S(0x2c));
            float dx = Fl(S(0x1c)) - Fl(S(0x28));
            float d = dx * dx + dy * dy;
            Fl(S(0x50)) = d;
            float r = Fl(ebx + 0x5dc);
            bool ok = !(Fl(S(0x50)) > r * r);
            if (ok && D(S(0xc)) != 0)
                ok = Fl(S(0x10)) > Fl(S(0x50)) || Fl(S(0x10)) != Fl(S(0x10)) || Fl(S(0x50)) != Fl(S(0x50));
            if (ok)
            {
                uint32_t t = D(ebx + 0x588);
                if (t != 0)
                {
                    VC(t, 0x54, S(0x34));
                    float ey = Fl(S(0x20)) - Fl(S(0x38));
                    float ex = Fl(S(0x1c)) - Fl(S(0x34));
                    float q = Fl(ebx + 0x5d8);
                    float e = ex * ex + ey * ey;
                    ok = !(q * q < e);
                    if (ok)
                    {
                        orig::fn_00453150(ebp, S(0x40));
                        ok = static_cast<uint8_t>(
                                 orig::fn_0042b860(D(ebx + 0x588), S(0x40), S(0x1c), ebx, 0x42820000u)) != 0;  // 65.0
                    }
                    if (ok)
                    {
                        Fl(S(0x24)) = Fl(S(0x24)) + K(0x005bcbdc);
                        ok = static_cast<uint8_t>(fn_00426fd0(S(0x1c), ebx, D(ebx + 0x588), ebp)) != 0;
                    }
                }
                if (ok)
                {
                    D(S(0xc)) = ebp;
                    D(S(0x10)) = D(S(0x50));
                }
            }
        }
        ebp = orig::fn_004e73c0(S(0x14));
    }
    uint32_t node = D(S(0x18));
    if (node != 0)
        D(node + 0x18) -= 1;
    return D(S(0xc));
}

static uint32_t fn_0042a100_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_0042a100(ebx, a0);
}
ABI_ENTRY_0042a100(fn_0042a100_Entry, fn_0042a100_Abi)
HOOK(0x0042a100, "fn_0042a100", fn_0042a100_Entry, nullptr);

// 0x0042a2c0 (stack: AI object; ret 4). Callers 0x0042a7b0, GenericAI_Update (not executed in recorded runs). Starts a
// turbo (jet pack) launch towards the point +0x934 (strings "FX_turbo_Launch_captain", "turbo_forward_cycle"):
// 0x00454c90(cl = 0, 1), refreshes +0x39c / +0x398 through 0x00455a00, reads the position (virtual +0x54). Close to the
// point (2D squared distance below 0x005bccb8): zero linear velocity (Body_setLinearVelocity 0x00458510) and an upward
// kick 0x004581c0 * +0xae8 * 0x005bcbe4 via 0x00458440. Otherwise a launch velocity from 0x00471a00 (in state 0xbc:
// +0xaf8 and +0xb04 * 0x005bcd64, sets flag 0x20000000, the wing sounds 0x0047bb30 of +0xb08, the four-animation set
// 0x6f/0x70/0x6d/0x6e and animation 0x005b68ac on +0xb08; else +0x948 and +0xaec * 0x005bcd64, clears the flag)
// applied with 0x00458510. Then the effect 0x005b6894 on +0xb08 (0x0047bba0), the user list game+0xa84 +0xf1c
// (ObjectList_Link), flag 0x10000000, animation 0x6b looped and state 0xbd (from 0xbc) or 0xb8.
uint32_t fn_0042a2c0(uint32_t ebp)
{
    uint32_t fr[6];  // sub esp, 0x18: fr[0] is [esp+0xc] with ebp, esi, edi pushed
    auto S = [&](uint32_t o) { return u(fr) + o - 0xc; };
    orig::fn_00454c90(ebp, 0, 1);  // xor cl, cl (upper ecx bytes: the caller's, unknown)
    D(ebp + 0x39c) = orig::fn_00455a00(D(ebp + 0x39c));
    D(ebp + 0x398) = orig::fn_00455a00(D(ebp + 0x398));
    VC(ebp, 0x54, S(0x18));
    float dy = Fl(ebp + 0x938) - Fl(S(0x1c));
    float dx = Fl(ebp + 0x934) - Fl(S(0x18));
    if (dx * dx + dy * dy < K(0x005bccb8))
    {
        D(S(0xc)) = 0;
        D(S(0x10)) = 0;
        D(S(0x14)) = 0;
        orig::fn_00458510(S(0xc), ebp);
        D(S(0xc)) = 0;
        D(S(0x10)) = 0;
        Fl(S(0x14)) = orig::fn_004581c0(ebp) * Fl(ebp + 0xae8) * K(0x005bcbe4);
        orig::fn_00458440(0, S(0xc), ebp);
    }
    else
    {
        uint32_t esi = ebp + 0x934;
        if (D(ebp + 0x460) == 0xbc)
        {
            float f = Fl(ebp + 0xb04) * K(0x005bcd64);
            orig::fn_00471a00(S(0xc), esi, S(0x18), D(ebp + 0xaf8), fb(f), 0);
            D(ebp + 0x484) |= 0x20000000;
            orig::fn_0047bb30(D(ebp + 0xb08), ebp, ebp + 0xb0c, 1);
            VC(ebp, 0x1a0, 0x6fu, 0x70u, 0x6du, 0x6eu, 1u, 0u, 0x3f000000u, 0x3f000000u);
            orig::fn_00408e30(0x81, D(ebp + 0xb08), 0xffffffffu, 0x005b68acu, 0, 0x3e800000u, 0, 0);
        }
        else
        {
            D(ebp + 0x484) &= 0xdfffffffu;
            float f = Fl(ebp + 0xaec) * K(0x005bcd64);
            orig::fn_00471a00(S(0xc), esi, S(0x18), D(ebp + 0x948), fb(f), 0);
        }
        orig::fn_00458510(S(0xc), ebp);
    }
    orig::fn_0047bba0(D(ebp + 0xb08), 0x005b6894u);
    uint32_t game = GameP();
    orig::fn_004546d0(D(D(game + 0xa84) + 0xf1c), ebp);
    D(ebp + 0x484) |= 0x10000000;
    VC(ebp, 0x19c, 0x6bu, 1u, 0u, 0u, 0u, 0u);
    if (D(ebp + 0x460) == 0xbc)
        return VC(ebp, 0x1b8, 0xbdu);
    return VC(ebp, 0x1b8, 0xb8u);
}

static uint32_t fn_0042a2c0_Abi(uint32_t a0)
{
    return fn_0042a2c0(a0);
}
ABI_ENTRY_0042a2c0(fn_0042a2c0_Entry, fn_0042a2c0_Abi)
HOOK(0x0042a2c0, "fn_0042a2c0", fn_0042a2c0_Entry, nullptr);

// 0x0042a500 (stack: AI object; ret 4; returns al). Caller 0x0042a7b0 (not executed in recorded runs). Finds the
// closest object of game+0xa84 list +0xef4 that 0x004723c0 accepts within the range +0x868 (2D), books its position in
// the grid game+0xaa4 (0x0046f6d0 must return 1), holds it at +0x864 (reference, 0x004723f0 with cl = 1), plays the
// move animation (0x00427210) and enters state 0xd2 (true).
uint32_t fn_0042a500(uint32_t a0)
{
    uint32_t st[0xb];  // sub esp, 0x24 plus the return address and the argument: st[0] is [esp+0x10] (4 pushes)
    auto S = [&](uint32_t o) { return u(st) + o - 0x10; };
    uint32_t ebp = a0;
    D(S(0x14)) = 0;
    D(S(0x18)) = 0;
    D(S(0x38)) = 0;  // best object, stored over the argument slot
    VC(ebp, 0x54, S(0x1c));
    uint32_t game = GameP();
    uint32_t best = 0;  // ebx
    uint32_t esi = orig::fn_004e7340(D(D(game + 0xa84) + 0xef4), S(0x14));
    while (esi != 0)
    {
        if (static_cast<uint8_t>(orig::fn_004723c0(esi)))
        {
            VC(esi, 0x54, S(0x28));
            float dy = Fl(S(0x2c)) - Fl(S(0x20));
            float dx = Fl(S(0x28)) - Fl(S(0x1c));
            float d = dx * dx + dy * dy;
            float r = Fl(ebp + 0x868);
            if (!(d > r * r) && (best == 0 || d < Fl(S(0x10))))
            {
                Fl(S(0x10)) = d;
                D(S(0x38)) = esi;
                best = esi;
            }
        }
        esi = orig::fn_004e73c0(S(0x14));
    }
    bool ok = false;
    if (best != 0)
    {
        VC(best, 0x54, S(0x1c));
        game = GameP();
        uint32_t r = static_cast<uint32_t>(orig::fn_0046f6d0(ebp, D(game + 0xaa4), S(0x1c), 0, 0, 0, 0, 0));
        if (r == 1)
        {
            uint32_t o = D(S(0x38));
            D(ebp + 0x864) = o;
            I(o + 0x28) += 1;
            orig::fn_004723f0(D(ebp + 0x864), 1);  // mov cl, 1
            orig::fn_00427210(ebp);
            VC(ebp, 0x1b8, 0xd2u);
            ok = true;
        }
    }
    uint32_t node = D(S(0x18));
    if (node != 0)
        D(node + 0x18) -= 1;
    return ok ? 1u : 0u;
}

static uint32_t fn_0042a500_Abi(uint32_t a0)
{
    return fn_0042a500(a0);
}
ABI_ENTRY_0042a500(fn_0042a500_Entry, fn_0042a500_Abi)
HOOK(0x0042a500, "fn_0042a500", fn_0042a500_Entry, nullptr);

// 0x0042a660 (edi = AI object). Callers 0x0042f5c0, GenericAI_Update (not executed in recorded runs). Picks a random
// spline group tag in [+0x558, +0x55c] and a random marker index in [+0x560, +0x564] that avoids the last two
// (+0x568, +0x56c; unsigned compares, shifting past them), records the index history, resolves the group (0x0044c030,
// ebx = index) and books its point in the grid game+0xaa4 (0x0046f6d0): with 0x0044b470 the point and its heading
// (0x00453150, flags 1), otherwise the point only (flags 1, 1).
uint32_t fn_0042a660(uint32_t edi)
{
    uint32_t fr[7];  // sub esp, 0x1c: fr[0] is [esp+0xc] with ebx, ebp, esi pushed
    auto S = [&](uint32_t o) { return u(fr) + o - 0xc; };
    uint32_t tag = RandRange(edi, 0x55c, 0x558);
    uint32_t last = D(edi + 0x568);
    uint32_t hi = D(edi + 0x564);
    uint32_t ebx;
    if (last <= hi)
    {
        if (D(edi + 0x56c) < hi)
        {
            uint32_t lo = D(edi + 0x560);
            uint32_t h = hi - 2;
            uint32_t r = Rand();
            ebx = static_cast<uint32_t>(static_cast<int32_t>(r * (h - lo + 1)) >> 15) + lo;
            if (ebx >= D(edi + 0x568))
                ++ebx;
            if (ebx >= D(edi + 0x56c))
                ++ebx;
        }
        else
        {
            uint32_t lo = D(edi + 0x560);
            uint32_t h = hi - 1;
            uint32_t r = Rand();
            ebx = static_cast<uint32_t>(static_cast<int32_t>(r * (h - lo + 1)) >> 15) + lo;
            if (ebx >= D(edi + 0x568))
                ++ebx;
        }
    }
    else
    {
        uint32_t lo = D(edi + 0x560);
        uint32_t r = Rand();
        ebx = static_cast<uint32_t>(static_cast<int32_t>(r * (hi - lo + 1)) >> 15) + lo;
    }
    uint32_t g = orig::fn_0044c030(ebx, tag);
    D(edi + 0x56c) = D(edi + 0x568);
    D(edi + 0x568) = ebx;
    if (g == 0)
        return 0;
    uint32_t grid;
    if (static_cast<uint8_t>(orig::fn_0044b470(g)))
    {
        orig::fn_00453150(g, S(0x1c));
        VC(g, 0x54, S(0x10));
        grid = D(GameP() + 0xaa4);
        return static_cast<uint32_t>(orig::fn_0046f6d0(edi, grid, S(0x10), S(0x1c), 0, 1, 0, 0));
    }
    VC(g, 0x54, S(0x10));
    grid = D(GameP() + 0xaa4);
    return static_cast<uint32_t>(orig::fn_0046f6d0(edi, grid, S(0x10), 0, 0, 1, 1, 0));
}

static uint32_t fn_0042a660_Abi(uint32_t edi)
{
    return fn_0042a660(edi);
}
ABI_ENTRY_0042a660(fn_0042a660_Entry, fn_0042a660_Abi)
HOOK(0x0042a660, "fn_0042a660", fn_0042a660_Entry, nullptr);
