// Recovered: batch ai_09, the Twiglet AI class (vtable 0x005af020; slot 1 0x00461f00 returns the descriptor 0x005853a8
// whose +0x14 is "CLASS(Twiglet)") and its helpers, 0x0049daf0..0x004a05a0.
// Static recovery 2026-10-05, compile-checked only (the game was not run); none of these functions is executed in the
// recorded runs, so the oracle (tools/compare.py) cannot verify them yet.
//
// Virtual slots (byte offset in the vtable 0x005af020): +0x34 0x0049f930, +0x38 0x0049dc20, +0x54 0x0049fd20,
// +0x5c 0x0049fcf0, +0x60 0x0049fd70, +0x64 0x0049fcc0, +0x68 0x0049fd50, +0x6c 0x0049fe30, +0x70 0x0049fdd0,
// +0x74 0x0049fd90, +0x78 0x0049fe00, +0x7c 0x0049fdb0, +0x80 0x0049f400, +0x84 0x0049f3f0, +0x8c 0x004a0380,
// +0xa0 0x004a01b0, +0xa8 0x0049fe60, +0xbc 0x0049dcf0, +0xd0 0x0049f530, +0xd4 0x0049f430, +0xdc 0x0049daf0,
// +0xfc 0x0049f850, +0x11c 0x004a03c0, +0x120 0x004a0420, +0x124 0x004a0480, +0x128 0x004a05a0.
//
// Twiglet fields used here (guesses from use): +0x460 flags (bit 0, bit 1, bit 2 "registered in the map" (0x0049f850),
// bit 3 cell taken, bit 4, bit 5, bit 8), +0x464 state, +0x468 timer, +0x470/+0x474 stun timer / time, +0x478 target
// (reference, set to Game+0x748 by 0x0049f260), +0x480 speed, +0x488/+0x48c map cell, +0x498 flags (bit 0 "IC anim"
// playing, bit 1 IC transform override active), +0x4a8 / +0x4ec two 0x44-byte matrices used while bit 1 is set,
// +0x538 IC anim name, +0x55c carried bomb (reference), +0x560 / +0x568 references, +0x564 attach slot (bit index in
// Game+0xd5c, counted in Game+0xd58), +0x584 an object the twiglet waits for.
//
// Callee notes: 0x004e7780 (2D normalise, st0 result) and 0x0046e2c0 (st0 result) go through their generated float
// wrappers; 0x0046e2c0 reads no ecx (the caller's `push ecx` only reserves the argument slot), passed 0. Virtual
// +0x54 (position getter) pops its one argument (0x0049f2b0 only balances its stack that way).
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
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
template <class... A>
inline uint32_t VC(uint32_t object, uint32_t off, A... args)
{
    return VCall<uint32_t>(P(object), static_cast<int>(off / 4), args...);
}
template <class... A>
inline bool VB(uint32_t object, uint32_t off, A... args)
{
    return static_cast<uint8_t>(VCall<uint32_t>(P(object), static_cast<int>(off / 4), args...)) != 0;
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
// rand() * n >> 15 as the original computes it (imul, sar), compared unsigned by the callers.
inline uint32_t RandN(int32_t n)
{
    return static_cast<uint32_t>(static_cast<int32_t>(Rand()) * n >> 15);
}
// The inlined reference drop: --object+0x28; at zero, unless virtual +0xd8 says the object is kept, 0x00487da0
// (eax = object) releases it.
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
// 0x00408e30 (eax = channel, ecx = object, edx = -1; name, 0, 0.25, 0, 0): plays an animation (the twiglet calls it
// only this way).
inline uint32_t PlayAnim(uint32_t channel, uint32_t self, uint32_t name)
{
    return orig::fn_00408e30(channel, self, 0xffffffffu, name, 0, 0x3e800000u, 0, 0);
}
// The attach-slot release the twiglet inlines (0x0049daf0, 0x0049f430, 0x004a01c0): --Game+0xd58 and clear bit
// +0x564 of Game+0xd5c.
inline void FreeAttachSlot(uint32_t game, uint32_t self)
{
    --I(game + 0xd58);
    D(game + 0xd5c) &= ~(1u << (D(self + 0x564) & 31));
}
}  // namespace

uint32_t fn_0049f840(uint32_t self);
uint64_t fn_0049f260(uint32_t self);
uint32_t Twiglet_RunToTarget(uint32_t self);
uint32_t fn_0049fcb0(uint32_t self);

// 0x0049daf0 (ecx = twiglet): Twiglet virtual +0xdc. If the twiglet's parent (Object_GetParent 0x004539d0) is
// Game+0x748 it frees its attach slot; drops the references +0x568 and +0x478; with flag bit 2 (0x0049f840) calls
// virtual +0xfc(0, 2) (leave the map cell); drops +0x55c and +0x560; ends with a tail jump to Object_Remove
// 0x004547b0 (reproduced as call-then-return). Name: guess from the tail call. Not executed in recorded runs.
uint32_t Twiglet_Remove(uint32_t self)
{
    uint32_t parent = orig::fn_004539d0(self);
    uint32_t game = GameP();
    if (parent == D(game + 0x748))
        FreeAttachSlot(game, self);
    ClearRef(self + 0x568);
    ClearRef(self + 0x478);
    if (static_cast<uint8_t>(fn_0049f840(self)))
        VC(self, 0xfc, 0u, 2u);
    ClearRef(self + 0x55c);
    ClearRef(self + 0x560);
    return orig::fn_004547b0(self);
}
static uint32_t Twiglet_Remove_Abi(uint32_t ecx)
{
    return Twiglet_Remove(ecx);
}
ABI_ENTRY_0049daf0(Twiglet_Remove_Entry, Twiglet_Remove_Abi)
HOOK(0x0049daf0, "Twiglet_Remove", Twiglet_Remove_Entry, nullptr);

// 0x0049dc20 (ecx = twiglet; stack: a0 (unused), object; ret 8): Twiglet virtual +0x38. When the object is the one
// the twiglet waits for (+0x584): retargets (0x0049f260), runs to it (0x0049f2b0) and clears +0x584. Returns false
// (al = 0, the upper bytes as left: the run state or the argument). Not executed in recorded runs.
uint32_t Twiglet_Vt38(uint32_t self, uint32_t /*a0*/, uint32_t object)
{
    uint32_t eax = object;
    if (object == D(self + 0x584))
    {
        fn_0049f260(self);
        eax = Twiglet_RunToTarget(self);
        D(self + 0x584) = 0;
    }
    return eax & 0xffffff00u;
}
static uint32_t Twiglet_Vt38_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Twiglet_Vt38(ecx, a0, a1);
}
ABI_ENTRY_0049dc20(Twiglet_Vt38_Entry, Twiglet_Vt38_Abi)
HOOK(0x0049dc20, "Twiglet_Vt38", Twiglet_Vt38_Entry, nullptr);

// 0x0049dc50 (esi = twiglet): plays "twiglet_swipe" (0x0058eec8) or "twiglet_swipe_2" (0x0058eeb8) on channel 0x81,
// 50/50 by rand() * 200 >> 15 < 100. Caller 0x0049dcf0. Not executed in recorded runs.
uint32_t Twiglet_PlaySwipe(uint32_t self)
{
    uint32_t r = RandN(200);
    if (r < 100)
        return PlayAnim(0x81, self, 0x0058eec8u);
    return PlayAnim(0x81, self, 0x0058eeb8u);
}
static uint32_t Twiglet_PlaySwipe_Abi(uint32_t esi)
{
    return Twiglet_PlaySwipe(esi);
}
ABI_ENTRY_0049dc50(Twiglet_PlaySwipe_Entry, Twiglet_PlaySwipe_Abi)
HOOK(0x0049dc50, "Twiglet_PlaySwipe", Twiglet_PlaySwipe_Entry, nullptr);

// 0x0049dc90 (esi = twiglet): 50/50 (rand() * 200 >> 15 < 100) state 0x15 with "twiglet_toss" (0x0058eea8) or state
// 0x16 with "twiglet_jump_toss" (0x0058ee94), channel 0x80. Caller 0x0049dcf0. Not executed in recorded runs.
uint32_t Twiglet_PlayToss(uint32_t self)
{
    uint32_t r = RandN(200);
    if (r < 100)
    {
        D(self + 0x464) = 0x15;
        return PlayAnim(0x80, self, 0x0058eea8u);
    }
    D(self + 0x464) = 0x16;
    return PlayAnim(0x80, self, 0x0058ee94u);
}
static uint32_t Twiglet_PlayToss_Abi(uint32_t esi)
{
    return Twiglet_PlayToss(esi);
}
ABI_ENTRY_0049dc90(Twiglet_PlayToss_Entry, Twiglet_PlayToss_Abi)
HOOK(0x0049dc90, "Twiglet_PlayToss", Twiglet_PlayToss_Entry, nullptr);

uint32_t fn_004a01c0(uint32_t self, uint32_t flag);
uint32_t fn_004a0130(uint32_t flag, uint32_t self, uint32_t object);
uint32_t fn_0049fab0(uint32_t self, uint32_t flag);

namespace
{
// 0x004e7880 (ecx = source vec3, edx = destination): normalises a 3D vector and returns its length in st0. The ABI
// table lists eax as the result; the callers pop st0, so it is called here with its real st0 result.
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

// 0x0049dcf0 (ecx = twiglet): Twiglet virtual +0xbc, the per-frame update (5318 bytes; a switch on the state +0x464
// through the jump table 0x0049f1b8 / byte index table 0x0049f204, index = state - 1, states 1..0x5b; every state
// not listed below goes to the common exit 0x0049f1ab).
// Frame: the original's stack frame (sub esp, 0x88) is kept as one byte array; S(o) is [esp + o] with the four saved
// registers pushed and no call argument pending (locals S(0x10)..S(0x97)). S(0x2c) = frame time (0x00604a90), S(0x28)
// = bit 1 of anim channel 4's +0x1c (0x00408800; "animation finished", guess), also kept in ebx.
// Prologue: the first time records its position (+0x56c, virtual +0x54) and a 10 s timer +0x578; later, while alive,
// every expiry of +0x578 kills the twiglet (virtual +0xd4(0)) when it is at least Game+0xd60 from that point in x/y
// (else 0x0046e2c0(3.0, 7.0), result dropped). A stun timer +0x470 (positive) counts down and at expiry blows it away
// (0x004a01c0(1)), else +0x474 counts down. A carried bomb whose virtual +0xd8 is false is dropped and the twiglet
// killed. Then 0x00454c90(cl = 0; 1) and the state:
//   0x01 idle: when +0x460 bit 8, within +0x588 of Game+0x748 in x/y: retarget, run (0x0049f260, 0x0049f2b0),
//        +0x584 = 0.
//   0x0a run with bomb ("ctr_twiglet_call_1..3" sounds through Game+0x2c virtual +0xc4 every 4..10 s, repaths every
//        1..3 s around a random rotation (0x00453150, 0x004e2e70, 0x004e36a0) when within 400 (squared distance) of
//        the target; within 100 drops the bomb or gets one out ("ctr_twiglet_attack_2", "twiglet_get_bomb_out",
//        state 0x46); stands ("twiglet_stand") while Game+0xc80 > 0, Game+0xcbc or Game+0xa84 -> +0xf00 virtual +0x34).
//   0x0b, 0x14, 0x15/0x16, 0x18 approach / toss / swipe (0x0049dc50, 0x0049dc90, 0x00454e60 turning towards it).
//   0x1e, 0x1f hit reactions; 0x28 dying fade (virtual +0x100(1 - (+0x494 - t) / +0x494), Object_Kill at 0);
//   0x32 IC anim (as the generic AI's 0x00442670 block: 0x00406430, 0x004ed260, 0x004eb5d0, 0x004ec410,
//        Matrix_Multiply 0x004e2910, Object_SetTransform, velocity from the moved translation; ends in 0x0049fab0(1));
//   0x3d attached: at expiry detaches from Game+0x748 and is launched (0x00471a00, "twiglet_fly_no_bomb", 0x3e);
//   0x3f wait then run; 0x46 bomb out -> run (0x0a); 0x50 hanging; 0x51/0x52 blown up (0x00471750, 0x00455a30);
//   0x5a pulled towards +0x568; 0x5b knocked away from +0x568.
// Strings: "twiglet_hanging" 0x0058ee70, "twiglet_blown_up" 0x0058ee80, "twiglet_stand" 0x0058eee8. Constants: 1.5
// 0x005bce20, 2.0 0x005bcb80, 5.0 0x005bcbdc, +-120 0x005bcf84 / 0x005bcf80, 400 0x005bcf7c, 100 0x005bcb60, 2025
// 0x005bcf78, 20 0x005bcbe8. Name from the slot's role (guess). Not executed in recorded runs.
uint32_t Twiglet_Update(uint32_t self)
{
    alignas(4) uint8_t fr[0x98];
    auto S = [&](uint32_t o) { return u(fr) + o; };
    const uint32_t ebp = self;
    const float k0 = K(0x0056c05c);
    D(S(0x2c)) = D(0x00604a90);
    uint32_t anim = orig::fn_00408800(4, ebp);
    const uint32_t ebx = (D(anim + 0x1c) >> 1) & 1;
    D(S(0x28)) = ebx;
    if (!(By(ebp + 0x460) & 0x40))
    {
        D(ebp + 0x460) |= 0x40;
        VC(ebp, 0x54, ebp + 0x56c);
        D(ebp + 0x578) = 0x41200000u;
    }
    else if (!static_cast<uint8_t>(orig::fn_00454780(ebp)))
    {
        float t = Fl(ebp + 0x578) - Fl(S(0x2c));
        Fl(ebp + 0x578) = t;
        if (t <= k0)
        {
            VC(ebp, 0x54, S(0x30));
            float dy = Fl(ebp + 0x570) - Fl(S(0x34));
            uint32_t game = GameP();
            float dx = Fl(ebp + 0x56c) - Fl(S(0x30));
            float r = Fl(game + 0xd60);
            float d2 = dx * dx + dy * dy;
            if (r * r <= d2)
                VC(ebp, 0xd4, 0u);
            else
                orig::fn_0046e2c0(game, 0x40400000u, 0x40e00000u);  // result dropped (fstp st0)
        }
    }
    if (Fl(ebp + 0x470) > k0 && !static_cast<uint8_t>(orig::fn_00454780(ebp)))
    {
        float t = Fl(ebp + 0x470) - Fl(S(0x2c));
        Fl(ebp + 0x470) = t;
        if (t <= k0)
            fn_004a01c0(ebp, 1);
        else
            Fl(ebp + 0x474) = Fl(ebp + 0x474) - Fl(S(0x2c));
    }
    const uint32_t edi = ebp + 0x55c;
    if (D(edi) && !VB(D(edi), 0xd8))
    {
        Unref(D(edi));
        D(edi) = 0;
        VC(ebp, 0xd4, 0u);
        return 0;
    }
    orig::fn_00454c90(ebp, 0, 1);
    switch (D(ebp + 0x464))
    {
    case 0x01:
    {
        orig::fn_00454c90(ebp, 1, 1);
        orig::fn_00454cc0(ebp, ebp + 0x398);
        if (!(D(ebp + 0x460) & 0x100))
            goto L_end;
        VC(ebp, 0x54, S(0x1c));
        VC(D(GameP() + 0x748), 0x54, S(0x30));
        float dx = Fl(S(0x30)) - Fl(S(0x1c));
        float dy = Fl(S(0x34)) - Fl(S(0x20));
        float r = Fl(ebp + 0x588);
        if (!(r * r > dy * dy + dx * dx))
            goto L_end;
        fn_0049f260(ebp);
        Twiglet_RunToTarget(ebp);
        D(ebp + 0x584) = 0;
        return 0;
    }
    case 0x5b:
    {
        uint32_t ic = D(ebp + 0x428);
        if (!(ic && (D(ic + 0x1c) & 0x10) && (D(ic + 0x1c) & 8)))
            Fl(ebp + 0x468) = Fl(ebp + 0x468) - K(0x00604a90);
        if (Fl(ebp + 0x468) <= k0)
            orig::fn_0046f440(D(GameP() + 0xaa4), ebp);
        if (!VB(D(ebp + 0x568), 0xd8))
        {
            VC(ebp, 0xd4, 0u);
            return 0;
        }
        orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);  // the push 0 at 0x0049dfd2 is its argument
        if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
            goto L_end;
        Fl(ebp + 0x468) = orig::fn_0046e2c0(0, 0x3f800000u, 0x40000000u);
        D(ebp + 0x464) = 0x5a;
        return 0;
    }
    case 0x5a:
    {
        orig::fn_00458000(ebp, S(0x3c));
        VC(ebp, 0x54, S(0x30));
        VC(D(ebp + 0x568), 0x54, S(0x1c));
        Fl(S(0x10)) = Fl(S(0x1c)) - Fl(S(0x30));
        Fl(S(0x14)) = Fl(S(0x20)) - Fl(S(0x34));
        Fl(S(0x18)) = Fl(S(0x24)) - Fl(S(0x38));
        Fl(S(0x28)) = Normalize3(S(0x10), S(0x10));
        Fl(S(0x10)) = Fl(S(0x10)) * Fl(ebp + 0x480) * K(0x005bce20);
        Fl(S(0x14)) = Fl(S(0x14)) * Fl(ebp + 0x480) * K(0x005bce20);
        if (Fl(ebp + 0x468) <= k0)
        {
            Fl(S(0x18)) = Fl(S(0x18)) * Fl(ebp + 0x480) * K(0x005bce20);
        }
        else
        {
            D(S(0x18)) = 0;
            float t = Fl(ebp + 0x468) - Fl(S(0x2c));
            Fl(ebp + 0x468) = t;
            if (t <= k0)
                PlayAnim(0x81, ebp, 0x0058ee80u);
        }
        if (Fl(S(0x28)) <= K(0x005bcb80))
            goto L_kill;
        if (!VB(D(ebp + 0x568), 0xd8))
            goto L_kill;
        float m1 = orig::fn_004581c0(ebp);
        float m2 = orig::fn_004581c0(ebp) * K(0x005bcbdc);
        float res = orig::fn_00471750(S(0x10), S(0x3c), S(0x48), 0, fb(m2), fb(m1));
        Fl(S(0x28)) = res;
        D(ebp + 0x39c) = orig::fn_00455a30(S(0x48), 0x10, ebp, D(ebp + 0x39c), fb(res), 0xffffffffu, 0);
        return 0;
    }
    case 0x50:
    {
        orig::fn_00454c90(ebp, 1, 1);
        orig::fn_00454cc0(ebp, ebp + 0x398);
        if (ebx)
            PlayAnim(0x81, ebp, 0x0058ee70u);
        float t = Fl(ebp + 0x470) - Fl(S(0x2c));
        Fl(ebp + 0x470) = t;
        if (!(t <= k0))
            goto L_end;
        fn_004a01c0(ebp, 0);
        return 0;
    }
    case 0x51:
    {
        if (ebx)
            PlayAnim(0x81, ebp, 0x0058ee80u);
        float t = Fl(ebp + 0x474) - Fl(S(0x2c));
        Fl(ebp + 0x474) = t;
        if (t <= k0)
        {
            orig::fn_00454750(D(D(GameP() + 0xa84) + 0xf1c), ebp);
            D(ebp + 0x464) = 0x52;
        }
        orig::fn_00458000(ebp, S(0x10));
        D(S(0x20)) = D(S(0x14));
        D(S(0x1c)) = D(S(0x10));
        D(S(0x24)) = D(S(0x18));
        float z = Fl(S(0x18));
        if (z <= k0)
            D(S(0x24)) = 0;
        else if (z >= K(0x005bcf84))
            D(S(0x24)) = 0x42f00000u;
        float m1 = orig::fn_004581c0(ebp);
        float m2 = orig::fn_004581c0(ebp) * Fl(ebp + 0x484);
        float res = orig::fn_00471750(S(0x1c), S(0x10), S(0x48), 0, fb(m2), fb(m1));
        Fl(S(0x28)) = res;
        D(ebp + 0x39c) = orig::fn_00455a30(S(0x48), 0x10, ebp, D(ebp + 0x39c), fb(res), 0xffffffffu, 0);
        return 0;
    }
    case 0x52:
    {
        if (ebx)
            PlayAnim(0x81, ebp, 0x0058ee80u);
        orig::fn_00458000(ebp, S(0x10));
        D(S(0x24)) = D(S(0x18));
        D(S(0x1c)) = D(S(0x10));
        D(S(0x20)) = D(S(0x14));
        float z = Fl(S(0x18));
        if (z > k0)
            D(S(0x24)) = 0;
        else if (z < K(0x005bcf80))
            D(S(0x24)) = 0xc2f00000u;
        float m1 = orig::fn_004581c0(ebp);
        float m2 = orig::fn_004581c0(ebp) * Fl(ebp + 0x484);
        float res = orig::fn_00471750(S(0x1c), S(0x10), S(0x48), 0, fb(m2), fb(m1));
        Fl(S(0x28)) = res;
        D(ebp + 0x39c) = orig::fn_00455a30(S(0x48), 0x10, ebp, D(ebp + 0x39c), fb(res), 0xffffffffu, 0);
        return 0;
    }
    case 0x0a:
    {
        {
            uint32_t game = GameP();
            if (!(Fl(game + 0xc80) <= k0))
                goto L_stand;
            if (By(game + 0xcbc))
                goto L_stand;
            if (VC(D(D(game + 0xa84) + 0xf00), 0x34))
                goto L_stand;
        }
        if (!D(ebp + 0x478))
            fn_0049f260(ebp);
        PlayAnim(0x81, ebp, D(edi) ? 0x0058ee58u : 0x0058ee4cu);
        VC(ebp, 0x54, S(0x30));
        VC(D(ebp + 0x478), 0x54, S(0x10));
        {
            float dy = Fl(S(0x34)) - Fl(S(0x14));
            float dx = Fl(S(0x30)) - Fl(S(0x10));
            Fl(S(0x28)) = dx * dx + dy * dy;
        }
        float t = Fl(ebp + 0x46c) - Fl(S(0x2c));
        Fl(ebp + 0x46c) = t;
        if (t < k0)
        {
            Fl(ebp + 0x46c) = orig::fn_0046e2c0(0, 0x40800000u, 0x41200000u);
            uint32_t r = RandN(300);
            uint32_t name = r < 100 ? 0x0058ee38u : (r < 200 ? 0x0058ee24u : 0x0058ee10u);
            VC(D(GameP() + 0x2c), 0xc4, name, ebp, 0u, 0u, 0xffffffffu, 0u);
        }
        t = Fl(ebp + 0x468) - Fl(S(0x2c));
        Fl(ebp + 0x468) = t;
        if (t < k0)
        {
            if (Fl(S(0x28)) < K(0x005bcf7c))
            {
                orig::fn_00453150(ebp, S(0x48));
                float ang = orig::fn_0046e2c0(0, 0xc0490fdbu, 0x40490fdbu);
                orig::fn_004e2e70(S(0x54), fb(ang));
                orig::fn_004e36a0(S(0x48), S(0x54), 1, S(0x1c));
                Normalize3(S(0x1c), S(0x1c));
                uint32_t vr = VC(ebp, 0x54, S(0x3c));
                uint32_t game = GameP();
                Fl(S(0x10)) = Fl(S(0x1c)) * K(0x005bcbdc) + Fl(S(0x10));
                Fl(S(0x14)) = Fl(S(0x20)) * K(0x005bcbdc) + Fl(S(0x14));
                Fl(S(0x18)) = Fl(S(0x24)) * K(0x005bcbdc) + Fl(S(0x18));
                orig::fn_0046f5b0(vr & 0xffffff00u /* xor al, al */, 0, S(0x10), ebp, D(game + 0xaa4), 0, 0);
            }
            else
            {
                orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), S(0x10), 0, 0, 0, 0, 0);
            }
            Fl(ebp + 0x468) = orig::fn_0046e2c0(0, 0x3f800000u, 0x40400000u);
        }
        const uint32_t next = 0x46;  // mov ebx, 0x46
        if (Fl(S(0x28)) < K(0x005bcb60))
        {
            uint32_t bomb = D(ebp + 0x55c);
            if (bomb)
            {
                Unref(bomb);
                D(ebp + 0x55c) = 0;
                if (VB(bomb, 0xd8))
                    VC(bomb, 0xd4, 0u);
            }
            else
            {
                uint32_t r = Rand();
                if (static_cast<int32_t>((r * 100u) & 0xffff8000u) < 0xa0000)
                    VC(D(GameP() + 0x2c), 0xc4, 0x0058edf8u, ebp, 0u, 0u, 0xffffffffu, 0u);
                orig::fn_00454c90(ebp, 1, 1);
                orig::fn_00454cc0(ebp, ebp + 0x398);
                PlayAnim(0x80, ebp, 0x0058ede0u);
                D(ebp + 0x464) = next;
            }
        }
        if (!D(ebp + 0x55c))
        {
            D(GameP() + 0xd54) = D(0x00604a98);
            orig::fn_00454c90(ebp, 1, 1);
            orig::fn_00454cc0(ebp, ebp + 0x398);
            PlayAnim(0x80, ebp, 0x0058ede0u);
            D(ebp + 0x464) = next;
            return 0;
        }
        orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);
        if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
            goto L_end;
        orig::fn_00453150(ebp, S(0x48));
        {
            float ang = orig::fn_0046e2c0(0, 0xc0490fdbu, 0x40490fdbu);
            orig::fn_004e2e70(S(0x54), fb(ang));
        }
        orig::fn_004e36a0(S(0x48), S(0x54), 1, S(0x1c));
        Normalize3(S(0x1c), S(0x1c));
        VC(ebp, 0x54, S(0x3c));
        Fl(S(0x10)) = Fl(S(0x1c)) * K(0x005bcbdc) + Fl(S(0x10));
        Fl(S(0x14)) = Fl(S(0x20)) * K(0x005bcbdc) + Fl(S(0x14));
        Fl(S(0x18)) = Fl(S(0x24)) * K(0x005bcbdc) + Fl(S(0x18));
        {
            float dy = Fl(S(0x14)) - Fl(S(0x40));
            float dx = Fl(S(0x10)) - Fl(S(0x3c));
            if (dx * dx + dy * dy < K(0x005bcf78))
            {
                // The push 0 at 0x0049e835 is the third argument of whichever call follows.
                uint32_t game = GameP();
                uint32_t map = D(game + 0xaa4);
                orig::fn_0046f6b0(map & 0xffffff00u, 1, game & 0xffffff00u, S(0x10), map, ebp, 0);
            }
            else
            {
                orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), S(0x10), 0, 0, 1, 0, 0);
            }
        }
        Fl(ebp + 0x468) = orig::fn_0046e2c0(0, 0x3f800000u, 0x40400000u);
        if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
            goto L_end;
        goto L_kill;
    }
    case 0x46:
        if (!ebx)
            goto L_end;
        PlayAnim(0x81, ebp, 0x0058ee58u);
        D(ebp + 0x464) = 0x0a;
        return 0;
    case 0x3f:
    {
        float t = Fl(ebp + 0x468) - Fl(S(0x2c));
        Fl(ebp + 0x468) = t;
        if (!(t <= k0))
            goto L_end;
        Twiglet_RunToTarget(ebp);
        return 0;
    }
    case 0x3d:
    {
        float t = Fl(ebp + 0x468) - Fl(S(0x2c));
        Fl(ebp + 0x468) = t;
        if (!(t <= k0))
            goto L_end;
        uint32_t parent = orig::fn_004539d0(ebp);
        uint32_t game = GameP();
        if (parent == D(game + 0x748))
            FreeAttachSlot(game, ebp);
        fn_004a0130((game & 0xffffff00u) | 1u, ebp, parent);
        VC(ebp, 0xf8, 0x30000u, 0u);
        orig::fn_00453b20(ebp);
        VC(ebp, 0x54, S(0x10));
        VC(parent, 0x70, S(0x54));
        D(S(0x60)) = D(S(0x10));
        D(S(0x80)) = D(S(0x18));
        D(S(0x70)) = D(S(0x14));
        orig::fn_00457990(D(S(0x18)) & 0xffffff00u /* xor al, al */, ebp, S(0x54));
        orig::fn_00453150(parent, S(0x3c));
        D(S(0x38)) = D(S(0x18));
        Fl(S(0x30)) = Fl(S(0x3c)) * K(0x005bcbe8) + Fl(S(0x10));
        Fl(S(0x34)) = Fl(S(0x40)) * K(0x005bcbe8) + Fl(S(0x14));
        orig::fn_00471a00(S(0x48), S(0x30), S(0x10), D(ebp + 0x480), 0xc280b23au, 0);
        D(ebp + 0x39c) = orig::fn_00455a00(D(ebp + 0x39c));
        D(ebp + 0x398) = orig::fn_00455a00(D(ebp + 0x398));
        orig::fn_00458510(S(0x48), ebp);
        PlayAnim(0x81, ebp, 0x0058edccu);
        D(ebp + 0x464) = 0x3e;
        return 0;
    }
    case 0x0b:
    {
        orig::fn_00454c90(ebp, 1, 1);
        if (!D(ebp + 0x478))
            fn_0049f260(ebp);
        VC(ebp, 0x54, S(0x30));
        VC(D(ebp + 0x478), 0x54, S(0x3c));
        Fl(S(0x1c)) = Fl(S(0x3c)) - Fl(S(0x30));
        Fl(S(0x20)) = Fl(S(0x40)) - Fl(S(0x34));
        orig::fn_004e7780(S(0x1c), S(0x1c));
        D(S(0x24)) = 0;
        orig::fn_00454e60(S(0x1c), ebp, 0x41800000u);
        if (!D(S(0x28)))
            goto L_end;
        VC(ebp, 0x54, S(0x1c));
        VC(D(ebp + 0x478), 0x54, S(0x10));
        float dx = Fl(S(0x1c)) - Fl(S(0x10));
        float dy = Fl(S(0x20)) - Fl(S(0x14));
        if (dy * dy + dx * dx >= K(0x005bcb60))
            goto L_run;
        orig::fn_00454c90(ebp, 1, 1);
        Twiglet_PlaySwipe(ebp);
        return 0;
    }
    case 0x14:
    {
        if (!D(ebp + 0x478))
            fn_0049f260(ebp);
        VC(ebp, 0x54, S(0x3c));
        VC(D(ebp + 0x478), 0x54, S(0x1c));
        float t = Fl(ebp + 0x468) - Fl(S(0x2c));
        Fl(ebp + 0x468) = t;
        if (t < k0)
        {
            orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), S(0x1c), 0, 0, 0, 0, 0);
            Fl(ebp + 0x468) = orig::fn_0046e2c0(0, 0x3f800000u, 0x40400000u);
        }
        {
            float dx = Fl(S(0x3c)) - Fl(S(0x1c));
            float dy = Fl(S(0x40)) - Fl(S(0x20));
            float r = Fl(ebp + 0x47c);
            if (r * r > dy * dy + dx * dx)
            {
                VC(ebp, 0xfc, 1u, 2u);
                Twiglet_PlayToss(ebp);
                return 0;
            }
        }
        orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);
        if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
            goto L_end;
        orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), S(0x1c), 0, 0, 0, 0, 0);
        Fl(ebp + 0x468) = orig::fn_0046e2c0(0, 0x3f800000u, 0x40400000u);
        return 0;
    }
    case 0x15:
    case 0x16:
    {
        if (!D(ebp + 0x478))
            fn_0049f260(ebp);
        VC(ebp, 0x54, S(0x30));
        VC(D(ebp + 0x478), 0x54, S(0x3c));
        Fl(S(0x1c)) = Fl(S(0x3c)) - Fl(S(0x30));
        Fl(S(0x20)) = Fl(S(0x40)) - Fl(S(0x34));
        orig::fn_004e7780(S(0x1c), S(0x1c));
        D(S(0x24)) = 0;
        orig::fn_00454e60(S(0x1c), ebp, 0x41800000u);
        if (!D(S(0x28)))
            goto L_end;
        VC(ebp, 0x54, S(0x1c));
        VC(D(ebp + 0x478), 0x54, S(0x10));
        if (!(By(ebp + 0x460) & 1))
            goto L_run;
        {
            float dx = Fl(S(0x1c)) - Fl(S(0x10));
            float dy = Fl(S(0x20)) - Fl(S(0x14));
            float r = Fl(ebp + 0x47c);
            if (r * r <= dy * dy + dx * dx)
                goto L_run;
        }
        Fl(ebp + 0x468) = orig::fn_0046e2c0(0, 0x3f000000u, 0x40400000u);
        D(ebp + 0x464) = 0x18;
        goto L_stand;
    }
    case 0x18:
    {
        {
            uint32_t game = GameP();
            if (!(Fl(game + 0xc80) <= k0))
                goto L_end;
            if (By(game + 0xcbc))
                goto L_end;
            if (VC(D(D(game + 0xa84) + 0xf00), 0x34))
                goto L_end;
        }
        if (!D(ebp + 0x478))
            fn_0049f260(ebp);
        VC(ebp, 0x54, S(0x30));
        VC(D(ebp + 0x478), 0x54, S(0x3c));
        Fl(S(0x1c)) = Fl(S(0x3c)) - Fl(S(0x30));
        Fl(S(0x20)) = Fl(S(0x40)) - Fl(S(0x34));
        orig::fn_004e7780(S(0x1c), S(0x1c));
        D(S(0x24)) = 0;
        orig::fn_00454e60(S(0x1c), ebp, 0x41800000u);
        float t = Fl(ebp + 0x468) - Fl(S(0x2c));
        Fl(ebp + 0x468) = t;
        if (!(t < k0))
            goto L_end;
        Twiglet_PlayToss(ebp);
        return 0;
    }
    case 0x32:
    {
        uint8_t cl = By(ebp + 0x498);
        Fl(ebp + 0x4a0) = Fl(S(0x2c)) + Fl(ebp + 0x4a0);
        if (!(cl & 2))
        {
            if (static_cast<uint8_t>(orig::fn_00406430(ebp + 0x498, cl, ebp)))
            {
                fn_0049fab0(ebp, 1);
                return 0;
            }
        }
        else if (ebx)
        {
            fn_0049fab0(ebp, 1);
            return 0;
        }
        if (!VB(ebp, 0xd8))
            goto L_end;
        uint32_t esi = orig::fn_004ed260(D(ebp + 0x410));
        // ECX at 0x0049efab is unknowable: last written by the virtual +0xd8 callee (0x0049ef81), 0x004ed260 leaves
        // it alone. edx = 0 (0x0049efa9), so 0x004eb5d0 never reads its ECX slot (only on a miss with edx != 0).
        uint32_t m = orig::fn_004eb5d0(D(ebp + 0x49c), 0 /* ECX unknowable, unread with edx = 0 */, 0, ebp + 0x538);
        uint32_t a = D(ebp + 0x49c);
        float v = static_cast<float>(I(a + 0x20)) * static_cast<float>(I(a + 0x1c));
        v = v * Fl(ebp + 0x4a0);
        v = v + static_cast<float>(I(a + 0x14));
        uint32_t r = orig::fn_004ec410(m, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int64_t>(v))) /* _ftol */,
                                       D(ebp + 0x410), esi, 0, 0);
        orig::fn_004e2910(r, ebp + 0x4ec, ebp + 0x4a8);
        VC(ebp, 0x54, S(0x10));
        D(S(0x20)) = D(ebp + 0x508);
        D(S(0x1c)) = D(ebp + 0x4f8);
        D(S(0x24)) = D(ebp + 0x518);
        D(ebp + 0x508) = D(S(0x14));
        D(ebp + 0x4f8) = D(S(0x10));
        D(ebp + 0x518) = D(S(0x18));
        orig::fn_00457990(D(S(0x14)) & 0xffffff00u /* xor al, al */, ebp, ebp + 0x4ec);
        Fl(S(0x30)) = Fl(S(0x1c)) - Fl(S(0x10));
        Fl(S(0x34)) = Fl(S(0x20)) - Fl(S(0x14));
        Fl(S(0x38)) = Fl(S(0x24)) - Fl(S(0x18));
        orig::fn_00458510(S(0x30), ebp);
        D(ebp + 0x4a4) = D(ebp + 0x4a0);
        return 0;
    }
    case 0x1e:
        orig::fn_00454c90(ebp, 1, 1);
        orig::fn_00454cc0(ebp, ebp + 0x398);
        if (!ebx)
            goto L_end;
        if (!(By(ebp + 0x460) & 2))
            Twiglet_RunToTarget(ebp);
        D(ebp + 0x460) &= 0xfffffffdu;
        return 0;
    case 0x1f:
        orig::fn_00454c90(ebp, 1, 1);
        orig::fn_00454cc0(ebp, ebp + 0x398);
        if (!ebx)
            goto L_end;
        goto L_run;
    case 0x28:
    {
        orig::fn_00454cc0(ebp, ebp + 0x398);
        if (static_cast<uint8_t>(orig::fn_00454bc0(ebp)))
            orig::fn_00454c90(ebp, 1, 1);
        if (!ebx)
            goto L_end;
        float t = Fl(ebp + 0x468) - K(0x00604a90);
        Fl(ebp + 0x468) = t;
        if (t <= k0)
            D(ebp + 0x468) = 0;
        float v = Fl(ebp + 0x494) - Fl(ebp + 0x468);
        if (v >= k0)
            VC(ebp, 0x100, fb(K(0x005bcb2c) - v / Fl(ebp + 0x494)));
        if (Fl(ebp + 0x468) == k0)
            orig::fn_00455640(ebp, 0);
        goto L_end;
    }
    default:
        goto L_end;
    }
L_end:
    return 0;
L_run:  // 0x0049f0f5
    Twiglet_RunToTarget(ebp);
    return 0;
L_stand:  // 0x0049ee16
    PlayAnim(0x81, ebp, 0x0058eee8u);
    return 0;
L_kill:  // 0x0049e8eb
    VC(ebp, 0xd4, 0u);
    return 0;
}
static uint32_t Twiglet_Update_Abi(uint32_t ecx)
{
    return Twiglet_Update(ecx);
}
ABI_ENTRY_0049dcf0(Twiglet_Update_Entry, Twiglet_Update_Abi)
HOOK(0x0049dcf0, "Twiglet_Update", Twiglet_Update_Entry, nullptr);

// 0x0049f260 (eax = twiglet): drops the reference +0x478 and stores Game+0x748 there with a new reference; returns it
// (the ABI's edx is whatever the release path left; returned as 0). Not executed in recorded runs.
uint64_t fn_0049f260(uint32_t self)
{
    uint32_t slot = self + 0x478;
    ClearRef(slot);
    uint32_t target = D(GameP() + 0x748);
    D(slot) = target;
    ++I(target + 0x28);
    return target;
}
static uint64_t fn_0049f260_Abi(uint32_t eax)
{
    return fn_0049f260(eax);
}
ABI_ENTRY_0049f260(fn_0049f260_Entry, fn_0049f260_Abi)
HOOK(0x0049f260, "fn_0049f260", fn_0049f260_Entry, nullptr);

// 0x0049f2b0 (esi = twiglet): retargets Game+0x748 (0x0049f260), timer +0x468 = rand() * 6.1e-5 (0x005bce30) + 1.0
// (0x005bcb2c), reads the target's position (virtual +0x54) and paths to it (0x0046f6d0, eax = twiglet, ecx =
// Game+0xaa4, ebx = position), plays "twiglet_run" (0x0058ee4c) or with a bomb (+0x55c) "twiglet_run_with_bomb"
// (0x0058ee58), leaves its map cell when registered (0x0049f840 -> virtual +0xfc(0, 2)) and sets state 0x14 when
// flag bit 0 is set, else 0x0a (returned). Callers 0x0049dc20, 0x0049dcf0, 0x0049f380, 0x0049fab0, 0x0049fe60. Not
// executed in recorded runs.
uint32_t Twiglet_RunToTarget(uint32_t self)
{
    float pos[3];
    fn_0049f260(self);
    int32_t r = static_cast<int32_t>(Rand());
    Fl(self + 0x468) = static_cast<float>(r) * K(0x005bce30) + K(0x005bcb2c);
    VC(D(self + 0x478), 0x54, u(pos));
    orig::fn_0046f6d0(self, D(GameP() + 0xaa4), u(pos), 0, 0, 0, 0, 0);
    PlayAnim(0x81, self, D(self + 0x55c) ? 0x0058ee58u : 0x0058ee4cu);
    if (static_cast<uint8_t>(fn_0049f840(self)))
        VC(self, 0xfc, 0u, 2u);
    uint32_t state = (By(self + 0x460) & 1) ? 0x14u : 0x0au;
    D(self + 0x464) = state;
    return state;
}
static uint32_t Twiglet_RunToTarget_Abi(uint32_t esi)
{
    return Twiglet_RunToTarget(esi);
}
ABI_ENTRY_0049f2b0(Twiglet_RunToTarget_Entry, Twiglet_RunToTarget_Abi)
HOOK(0x0049f2b0, "Twiglet_RunToTarget", Twiglet_RunToTarget_Entry, nullptr);

// 0x0049f380 (eax = twiglet, edx unused; stack: a0 (unused), time; ret 8): clears flag bit 0; in state 0, with a
// positive time stands ("twiglet_stand" 0x0058eee8, timer +0x468 = time, state 0x3f), else runs to the target
// (0x0049f2b0). Callers 0x0043ee90, 0x00482e80, 0x0049d080. Not executed in recorded runs.
uint32_t fn_0049f380(uint32_t self, uint32_t /*edx*/, uint32_t /*a0*/, uint32_t time)
{
    D(self + 0x460) &= 0xfffffffeu;
    uint32_t state = D(self + 0x464);
    if (state != 0)
        return state;
    if (bf(time) > K(0x0056c05c))
    {
        D(self + 0x468) = time;
        uint32_t r = PlayAnim(0x81, self, 0x0058eee8u);
        D(self + 0x464) = 0x3f;
        return r;
    }
    return Twiglet_RunToTarget(self);
}
static uint32_t fn_0049f380_Abi(uint32_t eax, uint32_t edx, uint32_t a0, uint32_t a1)
{
    return fn_0049f380(eax, edx, a0, a1);
}
ABI_ENTRY_0049f380(fn_0049f380_Entry, fn_0049f380_Abi)
HOOK(0x0049f380, "fn_0049f380", fn_0049f380_Entry, nullptr);

// 0x0049f3f0 (ecx = twiglet): Twiglet virtual +0x84, returns +0x484 in st0. Not executed in recorded runs.
float Twiglet_Vt84(uint32_t self)
{
    return Fl(self + 0x484);
}
static float Twiglet_Vt84_Abi(uint32_t ecx)
{
    return Twiglet_Vt84(ecx);
}
ABI_ENTRY_0049f3f0(Twiglet_Vt84_Entry, Twiglet_Vt84_Abi)
HOOK(0x0049f3f0, "Twiglet_Vt84", Twiglet_Vt84_Entry, nullptr);

// 0x0049f400 (ecx = twiglet; stack: three float outs; ret 0xc): Twiglet virtual +0x80: *a0 = *a1 = +0x480 (speed,
// guess), *a2 = 1.0. Not executed in recorded runs.
uint32_t Twiglet_Vt80(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2)
{
    D(a0) = D(self + 0x480);
    uint32_t v = D(self + 0x480);
    D(a1) = v;
    D(a2) = 0x3f800000u;
    return v;
}
static uint32_t Twiglet_Vt80_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Twiglet_Vt80(ecx, a0, a1, a2);
}
ABI_ENTRY_0049f400(Twiglet_Vt80_Entry, Twiglet_Vt80_Abi)
HOOK(0x0049f400, "Twiglet_Vt80", Twiglet_Vt80_Entry, nullptr);

// 0x0049f430 (ecx = twiglet; stack: a0; ret 4): Twiglet virtual +0xd4. When attached (Object_GetParent 0x004539d0)
// frees its attach slot and detaches (Object_Detach 0x00453b20); clears +0x454; counts Game+0x270c; adds +0x57c to
// Game+0x2728 when negative (or NaN), else to Game+0x2724; drops the carried bomb +0x55c and, when virtual +0xd8 of the
// bomb is true, kills it (virtual +0xd4(0)); leaves the map (0x0046f440, esi = Game+0xaa4, edi = twiglet) and ends in
// Object_Kill 0x00455640(a0). Name from the tail call. Not executed in recorded runs.
uint32_t Twiglet_Kill(uint32_t self, uint32_t a0)
{
    if (orig::fn_004539d0(self))
    {
        FreeAttachSlot(GameP(), self);
        orig::fn_00453b20(self);
    }
    uint32_t game = GameP();
    D(self + 0x454) = 0;
    ++I(game + 0x270c);
    float v = Fl(self + 0x57c);
    if (!(v >= K(0x0056c05c)))
        Fl(game + 0x2728) = v + Fl(game + 0x2728);
    else
        Fl(game + 0x2724) = v + Fl(game + 0x2724);
    uint32_t bomb = D(self + 0x55c);
    if (bomb)
    {
        Unref(bomb);
        D(self + 0x55c) = 0;
        if (VB(bomb, 0xd8))  // the dropped reference is used again (see the batch report)
            VC(bomb, 0xd4, 0u);
    }
    orig::fn_0046f440(D(GameP() + 0xaa4), self);
    return orig::fn_00455640(self, a0);
}
static uint32_t Twiglet_Kill_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_Kill(ecx, a0);
}
ABI_ENTRY_0049f430(Twiglet_Kill_Entry, Twiglet_Kill_Abi)
HOOK(0x0049f430, "Twiglet_Kill", Twiglet_Kill_Entry, nullptr);

// 0x0049f530 (ecx = twiglet; stack: damage info; ret 4): Twiglet virtual +0xd0, the damage handler. Ignored when the
// info has bit 6 and the twiglet carries a bomb, or when the source (+0x2c) has the same virtual +0xe4 (team, guess)
// and its virtual +0xe0 is false, or when dead (Object_IsDead 0x00454780). Bit 5: stun, +0x470 = rand() * 3.05e-5
// (0x005bcbc0), +0x474 = Game+0xc7c. Bit 11: takes the reference +0x30 into +0x568; when there was none before, runs
// 250 (0x005bcf00) units away from it (0x004e7780 2D normalise, 0x0046f6d0), timer = 0x0046e2c0(1.0, 1.75),
// "twiglet_run", state 0x5b. Otherwise Object_TakeDamage 0x004555e0; unless dead or in an IC anim (0x0049fcb0):
// forgets +0x584 under flag bit 8 (0x0049f260), then amount +0x14 <= threshold +0x490 plays one of
// "twiglet_hit_mgun[_2|_3]" (state 0x1e) and a larger one "twiglet_hit_med[_2|_3]" (state 0x1f). A null info is
// dereferenced (+0x2c) after the null test (original). Not executed in recorded runs.
uint32_t Twiglet_TakeDamage(uint32_t self, uint32_t info)
{
    if (info && (By(info) & 0x40) && D(self + 0x55c))
        return 0;
    uint32_t src = D(info + 0x2c);
    if (src)
    {
        uint8_t a = static_cast<uint8_t>(VC(src, 0xe4));
        uint8_t b = static_cast<uint8_t>(VC(self, 0xe4));
        if (a == b && !VB(D(info + 0x2c), 0xe0))
            return 0;
    }
    if (static_cast<uint8_t>(orig::fn_00454780(self)))
        return 0;
    uint32_t flags = D(info);
    if (flags & 0x20)
    {
        int32_t r = static_cast<int32_t>(Rand());
        Fl(self + 0x470) = static_cast<float>(r) * K(0x005bcbc0);
        D(self + 0x474) = D(GameP() + 0xc7c);
        return 0;
    }
    if (flags & 0x800)
    {
        uint32_t old = D(self + 0x568);
        bool had = false;
        if (old)
        {
            Unref(old);
            had = true;
        }
        uint32_t n = D(info + 0x30);
        D(self + 0x568) = n;
        ++I(n + 0x28);
        if (had)
            return 0;
        float from[3], pos[3], dir[2];
        VC(D(self + 0x568), 0x54, u(from));
        VC(self, 0x54, u(pos));
        dir[0] = pos[0] - from[0];
        dir[1] = pos[1] - from[1];
        orig::fn_004e7780(u(dir), u(dir));
        VC(self, 0x54, u(pos));
        pos[0] = dir[0] * K(0x005bcf00) + pos[0];
        pos[1] = dir[1] * K(0x005bcf00) + pos[1];
        orig::fn_0046f6d0(self, D(GameP() + 0xaa4), u(pos), 0, 0, 0, 0, 0);
        Fl(self + 0x468) = orig::fn_0046e2c0(0, 0x3f800000u, 0x3fe00000u);
        uint32_t r = PlayAnim(0x81, self, 0x0058ee4cu);
        D(self + 0x464) = 0x5b;
        return r;
    }
    orig::fn_004555e0(self, info);
    if (static_cast<uint8_t>(orig::fn_00454780(self)))
        return 0;
    if (static_cast<uint8_t>(fn_0049fcb0(self)))
        return 0;
    if (D(self + 0x584) && (D(self + 0x460) & 0x100))
    {
        D(self + 0x584) = 0;
        fn_0049f260(self);
    }
    if (Fl(info + 0x14) <= Fl(self + 0x490))
    {
        uint32_t state = D(self + 0x464);
        if (state == 0x1e || state == 0x1f)
            return state;
        D(self + 0x460) &= 0xfffffffdu;
        uint32_t r = RandN(300);
        uint32_t name = r < 100 ? 0x0058edb8u : (r < 200 ? 0x0058eda4u : 0x0058ed90u);
        uint32_t ret = PlayAnim(0x81, self, name);
        D(self + 0x464) = 0x1e;
        return ret;
    }
    if (D(self + 0x464) == 0x1f)
        return 0x1f;
    D(self + 0x460) &= 0xfffffffdu;
    uint32_t r = RandN(300);
    uint32_t name = r < 100 ? 0x0058ed80u : (r < 200 ? 0x0058ed6cu : 0x0058ed58u);
    uint32_t ret = PlayAnim(0x80, self, name);
    D(self + 0x464) = 0x1f;
    return ret;
}
static uint32_t Twiglet_TakeDamage_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_TakeDamage(ecx, a0);
}
ABI_ENTRY_0049f530(Twiglet_TakeDamage_Entry, Twiglet_TakeDamage_Abi)
HOOK(0x0049f530, "Twiglet_TakeDamage", Twiglet_TakeDamage_Entry, nullptr);

// 0x0049f840 (eax = twiglet): al = flag bit 2 (+0x460), the upper bytes of eax kept. Callers 0x0049daf0, 0x0049f2b0.
// Not executed in recorded runs.
uint32_t fn_0049f840(uint32_t self)
{
    return (self & 0xffffff00u) | ((By(self + 0x460) >> 2) & 1u);
}
static uint32_t fn_0049f840_Abi(uint32_t eax)
{
    return fn_0049f840(eax);
}
ABI_ENTRY_0049f840(fn_0049f840_Entry, fn_0049f840_Abi)
HOOK(0x0049f840, "fn_0049f840", fn_0049f840_Entry, nullptr);

// 0x0049f850 (ecx = twiglet; stack: enable, a1; ret 8): Twiglet virtual +0xfc. Enable: sets flag bit 2, maps its
// position (virtual +0x54) to a cell (0x0046f320, esi = Game+0xaa4; x, y, &+0x488, &+0x48c); if 0x0046f2e0 accepts
// the cell, sets bit 3 and calls 0x0046ece0(eax = +0x48c, ecx = +0x488, edx = a1, esi = Game+0xaa4; 1). Disable:
// clears bit 2 and, if bit 3 was set, clears it and calls 0x0046ece0(..., edx = 2; 0). Same shape as the AI's version
// in src/ai/ai_02.cpp (+0xa10/+0xa14). Not executed in recorded runs.
uint32_t Twiglet_VtFC(uint32_t self, uint32_t enable, uint32_t a1)
{
    float pos[3];
    uint32_t r = VC(self, 0x54, u(pos));
    if (static_cast<uint8_t>(enable))
    {
        D(self + 0x460) |= 4;
        r = orig::fn_0046f320(D(GameP() + 0xaa4), fb(pos[0]), fb(pos[1]), self + 0x488, self + 0x48c);
        if (!static_cast<uint8_t>(r))
            return r;
        uint32_t game = GameP();
        r = orig::fn_0046f2e0(D(game + 0xaa4), D(self + 0x488), D(self + 0x48c));
        if (!static_cast<uint8_t>(r))
            return r;
        D(self + 0x460) |= 8;
        return orig::fn_0046ece0(D(self + 0x48c), D(self + 0x488), a1, D(game + 0xaa4), 1);
    }
    uint32_t f = D(self + 0x460) & 0xfffffffbu;
    D(self + 0x460) = f;
    if (!(f & 8))
        return (r & 0xffffff00u) | (f & 0xffu);
    uint32_t game = GameP();
    D(self + 0x460) = f & 0xfffffff7u;
    return orig::fn_0046ece0(D(self + 0x48c), D(self + 0x488), 2, D(game + 0xaa4), 0);
}
static uint32_t Twiglet_VtFC_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Twiglet_VtFC(ecx, a0, a1);
}
ABI_ENTRY_0049f850(Twiglet_VtFC_Entry, Twiglet_VtFC_Abi)
HOOK(0x0049f850, "Twiglet_VtFC", Twiglet_VtFC_Entry, nullptr);

// 0x0049f930 (ecx = twiglet; stack: a0 (unused), event name, event data, a3 (unused); ret 0x10): Twiglet virtual +0x34,
// the animation event handler (guess from the names). "get_bomb" (0x0058ed4c): creates a "twiglet_bomb" object
// (ObjectSystem_CreateObject 0x0045f020, Game+0xa84, type 0x3ff, 0xaa) into +0x55c with a reference, virtual
// +0xf8(0x100, 0) on it, and attaches it (Object_Attach 0x004539f0, "ref_hand_00" -> "ref_bomb", 1, 4). "hit"
// (0x005b2e5c): 0x0043eae0(ebx = twiglet; target +0x478, 5.0, 10.0). "fire" (0x005b3154): the translations of the
// twiglet's node *data (0x00453c50) and of the target's "ref_hit_00", then 0x0047de40(eax = from, ebx = to;
// Game+0xaa8, twiglet, "twiglet_spear", 0) (throws a spear, guess). _stricmp is 0x005448ee (its ecx input is not
// read). Not executed in recorded runs.
uint32_t Twiglet_HandleEvent(uint32_t self, uint32_t /*a0*/, uint32_t name, uint32_t data, uint32_t /*a3*/)
{
    uint32_t r = orig::fn_005448ee(0, name, 0x0058ed4cu);
    if (r == 0)
    {
        uint32_t bomb = orig::fn_0045f020(0, 0x0058ed3cu, D(GameP() + 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
        D(self + 0x55c) = bomb;
        ++I(bomb + 0x28);
        VC(D(self + 0x55c), 0xf8, 0x100u, 0u);
        return static_cast<uint32_t>(orig::fn_004539f0(0x005bb9a8u, self, D(self + 0x55c), 0x0058ed30u, 1, 4));
    }
    r = orig::fn_005448ee(0, name, 0x005b2e5cu);
    if (r == 0)
        return orig::fn_0043eae0(self, D(self + 0x478), 0x40a00000u, 0x41200000u);
    r = orig::fn_005448ee(0, name, 0x005b3154u);
    if (r != 0)
        return r;
    uint32_t a[17], b[17], from[3], to[3];
    orig::fn_00453c50(self, D(data), u(a), 1, 0, 0, 1, 0, 0);
    from[0] = a[3];
    from[1] = a[7];
    from[2] = a[11];
    orig::fn_00453c50(D(self + 0x478), 0x005b68fcu, u(b), 1, 0, 0, 1, 0, 0);
    to[0] = b[3];
    to[1] = b[7];
    to[2] = b[11];
    return orig::fn_0047de40(u(from), u(to), D(GameP() + 0xaa8), self, 0x0058ed20u, 0);
}
static uint32_t Twiglet_HandleEvent_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Twiglet_HandleEvent(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0049f930(Twiglet_HandleEvent_Entry, Twiglet_HandleEvent_Abi)
HOOK(0x0049f930, "Twiglet_HandleEvent", Twiglet_HandleEvent_Entry, nullptr);

// 0x0049fab0 (eax = twiglet; stack: flag; ret 4): ends an IC anim: when virtual +0xd8 is true and +0x498 bit 0 is set,
// reads the matrix (virtual +0x5c), clears +0x498 bits 0 and 1 (two stores), sets it (Object_SetTransform 0x00457990,
// eax = the cleared flags with al = 0), virtual +0xf8(0x20000, 0); with the flag 0x00454c90(cl = 1; 1) and
// 0x00454cc0(esi = twiglet, edi = +0x398); then runs to the target (0x0049f2b0) when +0x460 bit 5 is set, else
// virtual +0xdc. Caller 0x0049dcf0. Not executed in recorded runs.
uint32_t fn_0049fab0(uint32_t self, uint32_t flag)
{
    uint32_t r = VC(self, 0xd8);
    if (!static_cast<uint8_t>(r))
        return r;
    if (!(By(self + 0x498) & 1))
        return r;
    uint32_t m[17];
    VC(self, 0x5c, u(m));
    uint32_t v = D(self + 0x498) & 0xfffffffeu;
    D(self + 0x498) = v;
    D(self + 0x498) = v & 0xfffffffdu;
    orig::fn_00457990(v & 0xffffff00u, self, u(m));
    VC(self, 0xf8, 0x20000u, 0u);
    if (static_cast<uint8_t>(flag))
    {
        orig::fn_00454c90(self, 1, 1);
        orig::fn_00454cc0(self, self + 0x398);
    }
    if (By(self + 0x460) & 0x20)
        return Twiglet_RunToTarget(self);
    return VC(self, 0xdc);
}
static uint32_t fn_0049fab0_Abi(uint32_t eax, uint32_t a0)
{
    return fn_0049fab0(eax, a0);
}
ABI_ENTRY_0049fab0(fn_0049fab0_Entry, fn_0049fab0_Abi)
HOOK(0x0049fab0, "fn_0049fab0", fn_0049fab0_Entry, nullptr);

// 0x0049fb60 (esi = twiglet): starts the IC anim once (+0x498 bit 1): anim channel 4 (0x00408800) gets the speed
// +0x4a0 / the anim's length (index from 0x004f1160 on the name +0x538 in the table of +0x414, -1 when not found);
// plays the anim +0x538 on channel 0x200 and sets bit 1 (returned). Caller 0x0049d590. Not executed in recorded runs.
uint32_t fn_0049fb60(uint32_t self)
{
    if (By(self + 0x498) & 2)
        return 0;
    uint32_t anim = orig::fn_00408800(4, self);
    uint32_t p = orig::fn_004f1160(D(D(D(self + 0x414) + 0x10)), self + 0x538);
    int32_t idx = p ? I(p) : -1;
    Fl(anim + 4) = Fl(self + 0x4a0) / Fl(D(D(self + 0x414) + 0xc) + static_cast<uint32_t>(idx * 3) * 4);
    orig::fn_00408e30(0x200, self, 0xffffffffu, self + 0x538, 0, 0x3e800000u, 0, 0);
    uint32_t f = D(self + 0x498) | 2;
    D(self + 0x498) = f;
    return f;
}
static uint32_t fn_0049fb60_Abi(uint32_t esi)
{
    return fn_0049fb60(esi);
}
ABI_ENTRY_0049fb60(fn_0049fb60_Entry, fn_0049fb60_Abi)
HOOK(0x0049fb60, "fn_0049fb60", fn_0049fb60_Entry, nullptr);

// 0x0049fbe0 (eax = anim name, edi = twiglet; stack: flag; ret 4): prepares an IC anim: copies the name to +0x538,
// stores the matrices (virtual +0x70 -> +0x4a8, virtual +0x5c -> +0x4ec), clears +0x4a4/+0x4a0, looks the anim up
// (0x0045ace0, ecx = name, esi = Game+0xa84) into +0x49c ("AI : Can't find IC Anim\n%s" 0x0058ed04 via Debug_Printf
// 0x00407ff0 when missing), sets +0x498 bit 0, +0x460 bit 5 = flag, virtual +0xf8(0x100, 0), state 0x32. Caller
// 0x0049d590. Not executed in recorded runs.
uint32_t fn_0049fbe0(uint32_t name, uint32_t self, uint32_t flag)
{
    uint32_t dst = self + 0x538;
    uint8_t c;
    do
    {
        c = By(name);
        By(dst) = c;
        ++name;
        ++dst;
    } while (c);
    VC(self, 0x70, self + 0x4a8);
    VC(self, 0x5c, self + 0x4ec);
    uint32_t game = GameP();
    D(self + 0x4a4) = 0;
    D(self + 0x4a0) = 0;
    uint32_t a = orig::fn_0045ace0(self + 0x538, D(game + 0xa84));
    D(self + 0x49c) = a;
    if (!a)
        orig::fn_00407ff0(D(GameP() + 0x34), 0x0058ed04u, self + 0x538, 0);
    D(self + 0x498) |= 1;
    if (static_cast<uint8_t>(flag))
        D(self + 0x460) |= 0x20;
    else
        D(self + 0x460) &= 0xffffffdfu;
    uint32_t r = VC(self, 0xf8, 0x100u, 0u);
    D(self + 0x464) = 0x32;
    return r;
}
static uint32_t fn_0049fbe0_Abi(uint32_t eax, uint32_t edi, uint32_t a0)
{
    return fn_0049fbe0(eax, edi, a0);
}
ABI_ENTRY_0049fbe0(fn_0049fbe0_Entry, fn_0049fbe0_Abi)
HOOK(0x0049fbe0, "fn_0049fbe0", fn_0049fbe0_Entry, nullptr);

// 0x0049fcb0 (eax = twiglet): +0x498 bit 0 (an IC anim is playing). Callers 0x0049f530, 0x004a01c0. Not executed in
// recorded runs.
uint32_t fn_0049fcb0(uint32_t self)
{
    return D(self + 0x498) & 1;
}
static uint32_t fn_0049fcb0_Abi(uint32_t eax)
{
    return fn_0049fcb0(eax);
}
ABI_ENTRY_0049fcb0(fn_0049fcb0_Entry, fn_0049fcb0_Abi)
HOOK(0x0049fcb0, "fn_0049fcb0", fn_0049fcb0_Entry, nullptr);

// Twiglet transform overrides: while +0x498 bit 1 is set (IC anim) the getters answer from the stored matrices
// +0x4ec / +0x4a8 (0x11 dwords), otherwise they tail-jump to the base methods (reproduced as call-then-return).
// None is executed in recorded runs.

// 0x0049fcc0 (ecx; stack: matrix out; ret 4): virtual +0x64, copies +0x4ec, else 0x00457d60.
uint32_t Twiglet_Vt64(uint32_t self, uint32_t out)
{
    if (By(self + 0x498) & 2)
    {
        std::memcpy(P(out), P(self + 0x4ec), 0x44);
        return 0;
    }
    return orig::fn_00457d60(self, out);
}
static uint32_t Twiglet_Vt64_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_Vt64(ecx, a0);
}
ABI_ENTRY_0049fcc0(Twiglet_Vt64_Entry, Twiglet_Vt64_Abi)
HOOK(0x0049fcc0, "Twiglet_Vt64", Twiglet_Vt64_Entry, nullptr);

// 0x0049fcf0 (ecx; stack: matrix out; ret 4): virtual +0x5c, copies +0x4ec, else 0x00457d90.
uint32_t Twiglet_Vt5C(uint32_t self, uint32_t out)
{
    if (By(self + 0x498) & 2)
    {
        std::memcpy(P(out), P(self + 0x4ec), 0x44);
        return 0;
    }
    return orig::fn_00457d90(self, out);
}
static uint32_t Twiglet_Vt5C_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_Vt5C(ecx, a0);
}
ABI_ENTRY_0049fcf0(Twiglet_Vt5C_Entry, Twiglet_Vt5C_Abi)
HOOK(0x0049fcf0, "Twiglet_Vt5C", Twiglet_Vt5C_Entry, nullptr);

// 0x0049fd20 (ecx; stack: vec3 out; ret 4): virtual +0x54, the position: the translation of +0x4ec (+0x4f8, +0x508,
// +0x518), else Body_getPosition 0x00457930.
uint32_t Twiglet_GetPosition(uint32_t self, uint32_t out)
{
    if (By(self + 0x498) & 2)
    {
        D(out) = D(self + 0x4f8);
        D(out + 4) = D(self + 0x508);
        D(out + 8) = D(self + 0x518);
        return out;
    }
    return orig::fn_00457930(self, out);
}
static uint32_t Twiglet_GetPosition_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_GetPosition(ecx, a0);
}
ABI_ENTRY_0049fd20(Twiglet_GetPosition_Entry, Twiglet_GetPosition_Abi)
HOOK(0x0049fd20, "Twiglet_GetPosition", Twiglet_GetPosition_Entry, nullptr);

// 0x0049fd50 (ecx): virtual +0x68, &+0x4ec, else 0x00457ea0.
uint32_t Twiglet_Vt68(uint32_t self)
{
    if (By(self + 0x498) & 2)
        return self + 0x4ec;
    return orig::fn_00457ea0(self);
}
static uint32_t Twiglet_Vt68_Abi(uint32_t ecx)
{
    return Twiglet_Vt68(ecx);
}
ABI_ENTRY_0049fd50(Twiglet_Vt68_Entry, Twiglet_Vt68_Abi)
HOOK(0x0049fd50, "Twiglet_Vt68", Twiglet_Vt68_Entry, nullptr);

// 0x0049fd70 (ecx): virtual +0x60, &+0x4ec, else 0x00457eb0.
uint32_t Twiglet_Vt60(uint32_t self)
{
    if (By(self + 0x498) & 2)
        return self + 0x4ec;
    return orig::fn_00457eb0(self);
}
static uint32_t Twiglet_Vt60_Abi(uint32_t ecx)
{
    return Twiglet_Vt60(ecx);
}
ABI_ENTRY_0049fd70(Twiglet_Vt60_Entry, Twiglet_Vt60_Abi)
HOOK(0x0049fd70, "Twiglet_Vt60", Twiglet_Vt60_Entry, nullptr);

// 0x0049fd90 (ecx): virtual +0x74, &+0x4a8, else 0x00457f00.
uint32_t Twiglet_Vt74(uint32_t self)
{
    if (By(self + 0x498) & 2)
        return self + 0x4a8;
    return orig::fn_00457f00(self);
}
static uint32_t Twiglet_Vt74_Abi(uint32_t ecx)
{
    return Twiglet_Vt74(ecx);
}
ABI_ENTRY_0049fd90(Twiglet_Vt74_Entry, Twiglet_Vt74_Abi)
HOOK(0x0049fd90, "Twiglet_Vt74", Twiglet_Vt74_Entry, nullptr);

// 0x0049fdb0 (ecx): virtual +0x7c, &+0x4a8, else 0x00457ec0.
uint32_t Twiglet_Vt7C(uint32_t self)
{
    if (By(self + 0x498) & 2)
        return self + 0x4a8;
    return orig::fn_00457ec0(self);
}
static uint32_t Twiglet_Vt7C_Abi(uint32_t ecx)
{
    return Twiglet_Vt7C(ecx);
}
ABI_ENTRY_0049fdb0(Twiglet_Vt7C_Entry, Twiglet_Vt7C_Abi)
HOOK(0x0049fdb0, "Twiglet_Vt7C", Twiglet_Vt7C_Entry, nullptr);

// 0x0049fdd0 (ecx; stack: matrix out; ret 4): virtual +0x70, copies +0x4a8, else 0x00457f80.
uint32_t Twiglet_Vt70(uint32_t self, uint32_t out)
{
    if (By(self + 0x498) & 2)
    {
        std::memcpy(P(out), P(self + 0x4a8), 0x44);
        return 0;
    }
    return orig::fn_00457f80(self, out);
}
static uint32_t Twiglet_Vt70_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_Vt70(ecx, a0);
}
ABI_ENTRY_0049fdd0(Twiglet_Vt70_Entry, Twiglet_Vt70_Abi)
HOOK(0x0049fdd0, "Twiglet_Vt70", Twiglet_Vt70_Entry, nullptr);

// 0x0049fe00 (ecx; stack: matrix out; ret 4): virtual +0x78, copies +0x4a8, else 0x00457f40.
uint32_t Twiglet_Vt78(uint32_t self, uint32_t out)
{
    if (By(self + 0x498) & 2)
    {
        std::memcpy(P(out), P(self + 0x4a8), 0x44);
        return 0;
    }
    return orig::fn_00457f40(self, out);
}
static uint32_t Twiglet_Vt78_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_Vt78(ecx, a0);
}
ABI_ENTRY_0049fe00(Twiglet_Vt78_Entry, Twiglet_Vt78_Abi)
HOOK(0x0049fe00, "Twiglet_Vt78", Twiglet_Vt78_Entry, nullptr);

// 0x0049fe30 (ecx; stack: vec3 out; ret 4): virtual +0x6c, the translation of +0x4a8 (+0x4b4, +0x4c4, +0x4d4), else
// Body_getPosition 0x00457930.
uint32_t Twiglet_Vt6C(uint32_t self, uint32_t out)
{
    if (By(self + 0x498) & 2)
    {
        D(out) = D(self + 0x4b4);
        D(out + 4) = D(self + 0x4c4);
        D(out + 8) = D(self + 0x4d4);
        return out;
    }
    return orig::fn_00457930(self, out);
}
static uint32_t Twiglet_Vt6C_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_Vt6C(ecx, a0);
}
ABI_ENTRY_0049fe30(Twiglet_Vt6C_Entry, Twiglet_Vt6C_Abi)
HOOK(0x0049fe30, "Twiglet_Vt6C", Twiglet_Vt6C_Entry, nullptr);

uint32_t fn_004a0130(uint32_t flag, uint32_t self, uint32_t object);

// 0x0049fe60 (ecx = twiglet; stack: other object, a1..a4; ret 0x14): Twiglet virtual +0xa8, the contact handler
// (guess: it ends in the base method 0x004548f0 with the same arguments and returns other == 0 in al).
// State 0x3c (leaping, guess): touching something other than the target: virtual +0xf8(0x20000, 0) and run to the
// target (0x0049f2b0). Touching the target with a bomb: drops the bomb reference and kills the bomb (virtual +0xd4(0))
// when its virtual +0xd8 is true. Without a bomb: takes the first free attach slot 0..2 of Game+0xd5c, plays
// "twiglet_claw_attack1..3" and attaches itself (Object_Attach 0x004539f0, "ref_twigletattach[2|3]" ->
// "ref_trotterattach", 1, 0x10), counts Game+0xd58; then virtual +0xf8(0x20000, 0), state 0x3d, timer = rand() * 3.05e-4
// (0x005bce58) + 20.0 (0x005bcbe8). State 0x3e: when falling (velocity z < 0, Body_getLinearVelocity 0x00458000):
// releases +0x560 (0x004a0130 with al = 0), virtual +0xf8(0x20000, 0), timer = rand() * 6.1e-5 + 1.0,
// "twiglet_stand", state 0x3f. State 0x52: Body_GetMass 0x004581c0 (result dropped), 0x00458720(edx = 0), virtual
// +0xd4(0). State 0x65: unless the other is Game+0x748, virtual +0xd4(0) when velocity z <= 0. State 0x66: the other
// being Game+0x748 returns false at once (al = 0, eax = Game with al cleared), anything else: virtual +0xd4(0). Not
// executed in recorded runs.
uint32_t Twiglet_VtA8(uint32_t self, uint32_t other, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t state = D(self + 0x464);
    if (state == 0x3c)
    {
        if (!other || other != D(self + 0x478))
        {
            VC(self, 0xf8, 0x20000u, 0u);
            Twiglet_RunToTarget(self);
        }
        else
        {
            uint32_t slot = self + 0x55c;
            uint32_t bomb = D(slot);
            if (bomb)
            {
                Unref(bomb);
                D(slot) = 0;
                if (VB(bomb, 0xd8))
                    VC(bomb, 0xd4, 0u);
            }
            else
            {
                uint32_t mask = D(GameP() + 0xd5c);
                uint32_t i = 0;
                for (; i < 3; ++i)
                    if (!(mask & (1u << i)))
                        break;
                if (i < 3)
                {
                    uint32_t ref;
                    if (i == 0)
                    {
                        PlayAnim(0x81, self, 0x0058ececu);
                        ref = 0x0058ecc4u;
                    }
                    else if (i == 1)
                    {
                        PlayAnim(0x81, self, 0x0058ecacu);
                        ref = 0x0058ec98u;
                    }
                    else
                    {
                        PlayAnim(0x81, self, 0x0058ec80u);
                        ref = 0x0058ec6cu;
                    }
                    orig::fn_004539f0(ref, other, self, 0x0058ecd8u, 1, 0x10);
                    uint32_t game = GameP();
                    ++I(game + 0xd58);
                    D(game + 0xd5c) |= 1u << i;
                    D(self + 0x564) = i;
                }
                VC(self, 0xf8, 0x20000u, 0u);
                int32_t r = static_cast<int32_t>(Rand());
                D(self + 0x464) = 0x3d;
                Fl(self + 0x468) = static_cast<float>(r) * K(0x005bce58) + K(0x005bcbe8);
            }
        }
    }
    else if (state == 0x3e)
    {
        float vel[3];
        orig::fn_00458000(self, u(vel));
        if (vel[2] < K(0x0056c05c))
        {
            fn_004a0130(0, self, 0);
            VC(self, 0xf8, 0x20000u, 0u);
            int32_t r = static_cast<int32_t>(Rand());
            Fl(self + 0x468) = static_cast<float>(r) * K(0x005bce30) + K(0x005bcb2c);
            PlayAnim(0x81, self, 0x0058eee8u);
            D(self + 0x464) = 0x3f;
        }
    }
    else if (state == 0x52)
    {
        orig::fn_004581c0(self);
        orig::fn_00458720(self, 0);
        VC(self, 0xd4, 0u);
    }
    else if (state == 0x65)
    {
        if (other != D(GameP() + 0x748))
        {
            float vel[3];
            orig::fn_00458000(self, u(vel));
            if (vel[2] <= K(0x0056c05c))
                VC(self, 0xd4, 0u);
        }
    }
    else if (state == 0x66)
    {
        uint32_t game = GameP();
        if (other == D(game + 0x748))
            return game & 0xffffff00u;
        VC(self, 0xd4, 0u);
    }
    uint32_t r = orig::fn_004548f0(self, other, a1, a2, a3, a4);
    return (r & 0xffffff00u) | (other == 0 ? 1u : 0u);
}
static uint32_t Twiglet_VtA8_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Twiglet_VtA8(ecx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_0049fe60(Twiglet_VtA8_Entry, Twiglet_VtA8_Abi)
HOOK(0x0049fe60, "Twiglet_VtA8", Twiglet_VtA8_Entry, nullptr);

// 0x004a0130 (al = flag, ecx = twiglet, ebx = object): flag clear: drops the reference +0x560 and clears it; flag set
// with an object: drops the old +0x560 (without clearing) and stores the object with a new reference. Callers
// 0x0049dcf0, 0x0049fe60. Not executed in recorded runs.
uint32_t fn_004a0130(uint32_t flag, uint32_t self, uint32_t object)
{
    if (!static_cast<uint8_t>(flag))
    {
        ClearRef(self + 0x560);
        return 0;
    }
    if (!object)
        return flag;
    uint32_t old = D(self + 0x560);
    if (old)
        Unref(old);
    D(self + 0x560) = object;
    ++I(object + 0x28);
    return 0;
}
static uint32_t fn_004a0130_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx)
{
    return fn_004a0130(eax, ecx, ebx);
}
ABI_ENTRY_004a0130(fn_004a0130_Entry, fn_004a0130_Abi)
HOOK(0x004a0130, "fn_004a0130", fn_004a0130_Entry, nullptr);

// 0x004a01b0 (ecx = twiglet; stack: object; ret 4): Twiglet virtual +0xa0: al = object != +0x560 (upper bytes of the
// argument kept). Not executed in recorded runs.
uint32_t Twiglet_VtA0(uint32_t self, uint32_t object)
{
    return (object & 0xffffff00u) | (object != D(self + 0x560) ? 1u : 0u);
}
static uint32_t Twiglet_VtA0_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_VtA0(ecx, a0);
}
ABI_ENTRY_004a01b0(Twiglet_VtA0_Entry, Twiglet_VtA0_Abi)
HOOK(0x004a01b0, "Twiglet_VtA0", Twiglet_VtA0_Entry, nullptr);

// 0x004a01c0 (eax = twiglet; stack: flag; ret 4): blown away. During an IC anim (0x0049fcb0) only clears +0x470. With
// the flag, +0x474 > 3.0 (0x005bcb7c), rand() * 100 >> 15 >= Game+0xca8 and no parent link (0x004539e0): hangs on
// ("twiglet_hang_into" 0x0058ec58, channel 0x80, +0x470 = 0x0046e2c0(0.25, +0x474 - 1.0), state 0x50). Otherwise:
// frees its attach slot when attached to Game+0x748, refreshes +0x39c/+0x398 (0x00455a00), zero velocity
// (Body_setLinearVelocity 0x00458510), Object_Detach 0x00453b20, Object_setPhysicsEnabled 0x004577b0(0), links into
// list Game+0xa84 -> +0xf1c (0x00454690 / 0x004546d0), plays "twiglet_blown_up_into" (state 0x50, channel 0x80) or
// "twiglet_blown_up" (0x81) and sets state 0x51. Caller 0x0049dcf0. Not executed in recorded runs.
uint32_t fn_004a01c0(uint32_t self, uint32_t flag)
{
    uint32_t ic = fn_0049fcb0(self);
    if (static_cast<uint8_t>(ic))
    {
        D(self + 0x470) = 0;
        return ic;
    }
    if (static_cast<uint8_t>(flag) && Fl(self + 0x474) > K(0x005bcb7c))
    {
        uint32_t r = RandN(100);
        if (!(r < D(GameP() + 0xca8)) && !static_cast<uint8_t>(orig::fn_004539e0(self)))
        {
            float x = Fl(self + 0x474) - K(0x005bcb2c);
            Fl(self + 0x470) = orig::fn_0046e2c0(0, 0x3e800000u, fb(x));
            uint32_t ret = PlayAnim(0x80, self, 0x0058ec58u);
            D(self + 0x464) = 0x50;
            return ret;
        }
    }
    uint32_t game = GameP();
    if (orig::fn_004539d0(self) == D(game + 0x748))
        FreeAttachSlot(game, self);
    D(self + 0x39c) = orig::fn_00455a00(D(self + 0x39c));
    D(self + 0x398) = orig::fn_00455a00(D(self + 0x398));
    uint32_t v[3] = {0, 0, 0};
    orig::fn_00458510(u(v), self);
    orig::fn_00453b20(self);
    orig::fn_004577b0(self, 0);
    if (!static_cast<uint8_t>(orig::fn_00454690(D(D(GameP() + 0xa84) + 0xf1c), self)))
        orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf1c), self);
    uint32_t ret;
    if (D(self + 0x464) == 0x50)
        ret = PlayAnim(0x80, self, 0x0058ec40u);
    else
        ret = PlayAnim(0x81, self, 0x0058ee80u);
    D(self + 0x464) = 0x51;
    return ret;
}
static uint32_t fn_004a01c0_Abi(uint32_t eax, uint32_t a0)
{
    return fn_004a01c0(eax, a0);
}
ABI_ENTRY_004a01c0(fn_004a01c0_Entry, fn_004a01c0_Abi)
HOOK(0x004a01c0, "fn_004a01c0", fn_004a01c0_Entry, nullptr);

// 0x004a0380 (ecx = twiglet; stack: float out; ret 4): Twiglet virtual +0x8c: +0x580 when flag bit 4 is set, else
// -1.0 in state 0x51, else 1.0. Not executed in recorded runs.
uint32_t Twiglet_Vt8C(uint32_t self, uint32_t out)
{
    if (By(self + 0x460) & 0x10)
    {
        uint32_t v = D(self + 0x580);
        D(out) = v;
        return v;
    }
    if (D(self + 0x464) == 0x51)
    {
        D(out) = 0xbf800000u;
        return 0;
    }
    D(out) = 0x3f800000u;
    return out;
}
static uint32_t Twiglet_Vt8C_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_Vt8C(ecx, a0);
}
ABI_ENTRY_004a0380(Twiglet_Vt8C_Entry, Twiglet_Vt8C_Abi)
HOOK(0x004a0380, "Twiglet_Vt8C", Twiglet_Vt8C_Entry, nullptr);

// 0x004a03c0 (ecx = twiglet): Twiglet virtual +0x11c: in state 0x64 re-enables physics (Object_setPhysicsEnabled
// 0x004577b0(0)), plays "twiglet_run" / "twiglet_run_with_bomb" (+0x55c) and sets state 0x0a. Not executed in
// recorded runs.
uint32_t Twiglet_Vt11C(uint32_t self)
{
    if (D(self + 0x464) != 0x64)
        return 0;
    orig::fn_004577b0(self, 0);
    uint32_t r = PlayAnim(0x81, self, D(self + 0x55c) ? 0x0058ee58u : 0x0058ee4cu);
    D(self + 0x464) = 0x0a;
    return r;
}
static uint32_t Twiglet_Vt11C_Abi(uint32_t ecx)
{
    return Twiglet_Vt11C(ecx);
}
ABI_ENTRY_004a03c0(Twiglet_Vt11C_Entry, Twiglet_Vt11C_Abi)
HOOK(0x004a03c0, "Twiglet_Vt11C", Twiglet_Vt11C_Entry, nullptr);

// 0x004a0420 (ecx = twiglet, edx unused; stack: float out; ret 4): Twiglet virtual +0x120: *out = Game+0xd1c * +0x480,
// "twiglet_stand", state 0x64, Object_setPhysicsEnabled 0x004577b0(1); returns true (al = 1). Not executed in recorded
// runs.
uint32_t Twiglet_Vt120(uint32_t self, uint32_t /*edx*/, uint32_t out)
{
    Fl(out) = Fl(GameP() + 0xd1c) * Fl(self + 0x480);
    PlayAnim(0x81, self, 0x0058eee8u);
    D(self + 0x464) = 0x64;
    uint32_t r = static_cast<uint32_t>(orig::fn_004577b0(self, 1));
    return (r & 0xffffff00u) | 1u;
}
static uint32_t Twiglet_Vt120_Abi(uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return Twiglet_Vt120(ecx, edx, a0);
}
ABI_ENTRY_004a0420(Twiglet_Vt120_Entry, Twiglet_Vt120_Abi)
HOOK(0x004a0420, "Twiglet_Vt120", Twiglet_Vt120_Entry, nullptr);

// 0x004a0480 (ecx = twiglet; stack: a0 (unused); ret 4): Twiglet virtual +0x124, thrown at Game+0x748: from its
// position (virtual +0x54) to the translation of Game+0x748's "ref_hit_00" (0x00453c50) computes a launch velocity
// (0x00471a00, ebx = out, esi = target, edi = position; Game+0xd1c * +0x480, Game+0xd20 * -64.348 (0x005bcd64), 0),
// 0x00454c90(cl = 0; 1), Object_setPhysicsEnabled(0), refreshes +0x39c/+0x398 (0x00455a00), sets the velocity
// (0x00458510), "twiglet_blown_up", virtual +0xf8(0x170001, 0), state 0x65. Not executed in recorded runs.
uint32_t Twiglet_Vt124(uint32_t self, uint32_t /*a0*/)
{
    float pos[3];
    uint32_t m[17], hit[3], out[3];
    VC(self, 0x54, u(pos));
    orig::fn_00453c50(D(GameP() + 0x748), 0x005b68fcu, u(m), 1, 0, 0, 1, 0, 0);
    hit[1] = m[7];
    uint32_t game = GameP();
    float g = Fl(game + 0xd20) * K(0x005bcd64);
    hit[0] = m[3];
    float speed = Fl(game + 0xd1c) * Fl(self + 0x480);
    hit[2] = m[11];
    orig::fn_00471a00(u(out), u(hit), u(pos), fb(speed), fb(g), 0);
    orig::fn_00454c90(self, m[11] & 0xffffff00u, 1);  // ecx = the last loaded word with cl cleared
    orig::fn_004577b0(self, 0);
    D(self + 0x39c) = orig::fn_00455a00(D(self + 0x39c));
    D(self + 0x398) = orig::fn_00455a00(D(self + 0x398));
    orig::fn_00458510(u(out), self);
    PlayAnim(0x81, self, 0x0058ee80u);
    uint32_t r = VC(self, 0xf8, 0x170001u, 0u);
    D(self + 0x464) = 0x65;
    return r;
}
static uint32_t Twiglet_Vt124_Abi(uint32_t ecx, uint32_t a0)
{
    return Twiglet_Vt124(ecx, a0);
}
ABI_ENTRY_004a0480(Twiglet_Vt124_Entry, Twiglet_Vt124_Abi)
HOOK(0x004a0480, "Twiglet_Vt124", Twiglet_Vt124_Entry, nullptr);

// 0x004a05a0 (ecx = twiglet; stack: flag, a1, a2 (unused); ret 0xc): Twiglet virtual +0x128. Flag clear: virtual
// +0xd4(0). Flag set: refreshes +0x39c/+0x398 (0x00455a00), sets flag bit 4, links into list Game+0xa84 -> +0xf1c
// (0x00454690 / 0x004546d0), reverses its velocity (0x00458000 / 0x00458510) and sets state 0x66. Not executed in
// recorded runs.
uint32_t Twiglet_Vt128(uint32_t self, uint32_t flag, uint32_t /*a1*/, uint32_t /*a2*/)
{
    if (!static_cast<uint8_t>(flag))
        return VC(self, 0xd4, 0u);
    D(self + 0x39c) = orig::fn_00455a00(D(self + 0x39c));
    D(self + 0x398) = orig::fn_00455a00(D(self + 0x398));
    D(self + 0x460) |= 0x10;
    if (!static_cast<uint8_t>(orig::fn_00454690(D(D(GameP() + 0xa84) + 0xf1c), self)))
        orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf1c), self);
    float v[3];
    orig::fn_00458000(self, u(v));
    v[0] = -v[0];
    v[1] = -v[1];
    v[2] = -v[2];
    uint32_t r = static_cast<uint32_t>(orig::fn_00458510(u(v), self));
    D(self + 0x464) = 0x66;
    return r;
}
static uint32_t Twiglet_Vt128_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Twiglet_Vt128(ecx, a0, a1, a2);
}
ABI_ENTRY_004a05a0(Twiglet_Vt128_Entry, Twiglet_Vt128_Abi)
HOOK(0x004a05a0, "Twiglet_Vt128", Twiglet_Vt128_Entry, nullptr);
