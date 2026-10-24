// Recovered: AI-side game code, batch ai_03 (0x0042a7b0-0x00430140): generic AI cover / hiding-spot search, AI
// helper routines around the generic AI object (GenericAIObject vtable 0x005b2958) and its virtual methods. Static
// recovery 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified; virtual methods give their GenericAIObject slot (class from the vtable's slot 1
// descriptor, "CLASS(GenericAIObject)"); the same function sits at the same slot in the vtables of the other AI
// classes (PenguinObject, Q112Object, GruntLadderObject, SquadObject, ...).
//
// Callees the ABI table describes wrongly (handled here):
//   0x004e8080 returns the squared 3D distance in st0 (listed "none") -> Dist3Sq, recomputed in the x87 order.
//   0x004e7880 returns the vector length in st0 (listed eax) -> Normalize3 (inline asm, pops st0).
//   0x0046e2c0 reads no ecx (push ecx only reserves a stack slot): passed 0.
// Where the original passes a register whose low byte it just cleared (xor al, al), the upper bytes are passed as
// the original left them when known, otherwise 0 (the callees read the low byte only: 0x0046f6d0 `mov cl, byte`).
// Return values: functions that return a bool in al return 0/1 (the upper bytes of eax are not reproduced).
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
inline uint32_t G(uint32_t offset)
{
    return D(GameP() + offset);
}
// 0x00604a90: the frame time (float).
inline float FrameTime()
{
    return GameGlobal<float>(0x00604a90);
}
// 0x005391cd (_rand).
inline int32_t Rand()
{
    return static_cast<int32_t>(static_cast<uint32_t>(orig::fn_005391cd()));
}
// The game's percent roll: (rand() * 100) & 0xffff8000 compared signed against a threshold << 15.
inline int32_t RandPct()
{
    return static_cast<int32_t>(static_cast<uint32_t>(Rand() * 100) & 0xffff8000u);
}
// Debug_Printf 0x00407ff0 (cdecl, variadic: log object, format, ...).
template <class... A>
inline void DebugPrintf(uint32_t log, uint32_t format, A... args)
{
    GameFunction<void(__cdecl*)(uint32_t, uint32_t, ...)>(0x00407ff0)(log, format, args...);
}
// The reference drop the game inlines everywhere: --object+0x28; at zero, unless virtual +0xd8 says the object is
// kept, 0x00487da0 (eax = object) releases it.
inline void Unref(uint32_t object)
{
    if (--I(object + 0x28) == 0 && !VB(object, 0xd8))
        orig::fn_00487da0(object);
}
// 0x004e8080 (eax = a, ecx = b): squared 3D distance, st0 (the ABI table lists no result).
inline float Dist3Sq(uint32_t a, uint32_t b)
{
    float dz = Fl(a + 8) - Fl(b + 8);
    float dy = Fl(a + 4) - Fl(b + 4);
    float dx = Fl(a) - Fl(b);
    return dx * dx + dy * dy + dz * dz;
}
// 0x004e7880 (ecx = source vec3, edx = destination): normalises a 3D vector and returns its length in st0 (the ABI
// table lists eax).
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
// A byte frame laid out like the original's stack frame: Fr::U(o) is [esp+o] at the reference point named in the
// function's comment.
template <int N>
struct Frame
{
    alignas(4) uint8_t b[N];
    Frame() { std::memset(b, 0, N); }
    uint32_t A(int o) { return u(b + o); }
    uint32_t& U(int o) { return *reinterpret_cast<uint32_t*>(b + o); }
    float& F(int o) { return *reinterpret_cast<float*>(b + o); }
    uint8_t& B8(int o) { return b[o]; }
};
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t fn_0042df70(uint32_t self, uint32_t a0);

// ---------------------------------------------------------------------------------------------------------------
// 0x0042a7b0 (ecx = AI object; stack: a0 (bool), a1 (bool), a2 (bool); ret 0xc). GenericAIObject slot 144 (+0x240),
// the "find a hiding spot" decision (strings "Guarding AI looking for a hiding spot (state = %d)" 0x005b6860 when
// +0x484 bit 25, "Barricade AI looking for a hiding spot (state = %d)" 0x005b682c when bit 17). Dead: returns 1. A
// random roll against +0x86c may hand over to 0x0042a500. On the "alternative" path (0x0044e600) it tries 0x00429a50,
// 0x00429ce0, 0x00429ac0. Otherwise it compares two candidate objects (0x0042a100, 0x00429f00) by 2D distance, then
// walks the list World(Game+0xa84)+0xed4 for cover objects (virtuals +0x138 type, +0x13c/+0x140 used/capacity,
// +0x144 slot count, +0x148 slot free, +0x154 slot kind, +0x14c slot position) and keeps the nearest usable slot;
// found: takes it (+0x904 with a reference, +0x908 / +0x914 / +0x934, virtual +0x158) and sets a state through
// virtual +0x1b8 (0xb4/0xb5/0x1f/0x28/0x21/0x1e/0xbc, 0x7b, 0xa0). Nothing found: animation (virtual +0x1a0) and state
// 0x32, returns 0. Not executed in recorded runs.
//
// Frame: fr.U(o) is [esp+o] with the four registers (ebp esi edi ebx) pushed and no call arguments pending (sub esp,
// 0x90: o = 0x10..0x9f). 0x14 best distance, 0x18 distance, 0x1c best cover, 0x20 slot point, 0x2c best point,
// 0x38 target, 0x3c/0x8c 2D temporaries, 0x48 best slot, 0x4c best slot vector, 0x58 candidate A, 0x5c/0x60 list
// iterator, 0x64 own position, 0x70 candidate B, 0x74 other position, 0x80 slot vector, 0x98 0x00453290 result, 0x9c
// squared distance to the target.
uint32_t fn_0042a7b0(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2)
{
    Frame<0xa0> fr;
    uint32_t esi = 0, edi = 0, ebx = 0;
    uint8_t bl = 0;
    fr.U(0x5c) = 0;
    fr.U(0x60) = 0;
    if (static_cast<uint8_t>(orig::fn_00454780(self)))
        return 1;
    if (D(self + 0x858) != 0)
        VC(self, 0x18c, 0u);
    if (!(By(self + 0x484) & 4) && D(self + 0x588) != 0 && !static_cast<uint8_t>(orig::fn_004531e0(D(self + 0x588))) &&
        RandPct() < 0x2d0000)
    {
        fn_0042df70(self, 0x42340000u);
        VC(self, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(self + 0x580), D(self + 0x584));
        VC(self, 0x1b8, 0x32u);
        D(self + 0x484) &= 0xfffffdffu;
        return 0;
    }
    if (static_cast<uint32_t>((Rand() * 100) >> 15) < D(self + 0x86c) &&
        static_cast<uint8_t>(orig::fn_0042a500(self)))
        return 1;
    if (D(self + 0x904) != 0)
        VC(self, 0x188);
    if (D(self + 0x484) & 0x2000000)
        DebugPrintf(G(0x34), 0x005b6860u, D(self + 0x460));
    if (D(self + 0x484) & 0x20000)
        DebugPrintf(G(0x34), 0x005b682cu, D(self + 0x460));
    if (static_cast<uint8_t>(orig::fn_0044e600()))
    {
        if (static_cast<uint8_t>(orig::fn_00429a50(self)) || static_cast<uint8_t>(orig::fn_00429ce0(self)) ||
            static_cast<uint8_t>(orig::fn_00429ac0(self)))
            return 1;
        goto L_b3ec;
    }
    if (static_cast<uint8_t>(a0) && D(self + 0x588) == 0)
        goto L_b3ec;
    VC(self, 0x54, fr.A(0x64));
    fr.U(0x38) = D(self + 0x588) ? D(self + 0x588) : G(0x748);
    esi = orig::fn_0042a100(self, a0);
    fr.U(0x58) = esi;
    edi = orig::fn_00429f00(self, a0);
    fr.U(0x70) = edi;
    {
        float r2 = Fl(self + 0x5dc) + Fl(self + 0x5dc);
        fr.U(0x1c) = 0;
        fr.F(0x14) = r2 * r2;
    }
    if (esi)
    {
        VC(esi, 0x54, fr.A(0x74));
        float dy = fr.F(0x78) - fr.F(0x68);
        float dx = fr.F(0x74) - fr.F(0x64);
        float d = dx * dx + dy * dy;
        if (d < fr.F(0x14))
            fr.F(0x14) = d;
        else
        {
            esi = 0;
            fr.U(0x58) = 0;
        }
    }
    if (edi)
    {
        VC(edi, 0x54, fr.A(0x74));
        float dy = fr.F(0x78) - fr.F(0x68);
        float dx = fr.F(0x74) - fr.F(0x64);
        float d = dx * dx + dy * dy;
        if (d < fr.F(0x14))
        {
            esi = 0;
            fr.U(0x58) = 0;
            fr.F(0x14) = d * K(0x005bcbe4);
        }
        else
        {
            edi = 0;
            fr.U(0x70) = 0;
            if (esi)
                fr.F(0x14) = fr.F(0x14) * K(0x005bcbe4);
        }
    }
    else if (esi)
        fr.F(0x14) = fr.F(0x14) * K(0x005bcbe4);
    VC(fr.U(0x38), 0x54, fr.A(0x74));
    {
        float dy = fr.F(0x78) - fr.F(0x68);
        float dx = fr.F(0x74) - fr.F(0x64);
        fr.F(0x9c) = dx * dx + dy * dy;
    }
    if (!VB(self, 0x1cc))
        goto L_b291;
    if (D(self + 0x488) & 0x4000)
        goto L_b291;
    if (D(self + 0x588) && !static_cast<uint8_t>(orig::fn_004531e0(D(self + 0x588))))
        goto L_b291;
    if (static_cast<uint8_t>(a1))
        fr.B8(0x13) = 1;
    else
    {
        int32_t r = RandPct();
        fr.B8(0x13) = 0;
        if (r < 0xc8000)
            fr.B8(0x13) = 1;
    }
    bl = static_cast<uint8_t>((By(self + 0x484) >> 4) & 1);
    fr.B8(0x12) = bl;
    esi = orig::fn_004e7340(D(G(0xa84) + 0xed4), fr.A(0x5c));
    while (esi)
    {
        if (static_cast<uint8_t>(orig::fn_00453290(esi, fr.A(0x98))) && fr.U(0x98) == D(self + 0x858))
            goto L_aebd;
        if (VC(esi, 0x138) == 3)
            goto L_aebd;
        if (esi == orig::fn_00454c60(self))
            goto L_aebd;
        if (esi == orig::fn_00454c60(fr.U(0x38)) && RandPct() < 0x218000)
            goto L_aebd;
        if (bl)
        {
            if (esi == D(self + 0x918) || esi == D(self + 0x91c))
                goto L_aebd;
        }
        else if (fr.B8(0x13) && esi == D(self + 0x918))
            goto L_aebd;
        {
            uint32_t cap = VC(esi, 0x140);
            if (VC(esi, 0x13c) >= cap)
                goto L_aebd;
        }
        if (!VB(self, 0x244, esi))
            goto L_aebd;
        ebx = 0;
        if (VC(esi, 0x144) != 0)
        {
            do
            {
                if (!VB(esi, 0x148, ebx))
                    goto L_aea6;
                {
                    uint32_t kind = VC(esi, 0x154, ebx);
                    if (kind == 4)
                        goto L_aea6;
                    if (D(self + 0xb08) != 0)
                        goto L_aea6;
                    if (kind == 6)
                    {
                        if (!VB(self, 0x1f8))
                            goto L_aea6;
                    }
                    else if (kind != 1)
                        goto L_aea6;
                }
                edi = fr.U(0x38);
                VC(esi, 0x14c, fr.A(0x20), fr.A(0x80), ebx, self, edi, 0u);
                if (esi != fr.U(0x1c))
                {
                    fr.F(0x3c) = fr.F(0x64) - fr.F(0x74);
                    fr.F(0x40) = fr.F(0x68) - fr.F(0x78);
                    fr.F(0x8c) = fr.F(0x20) - fr.F(0x64);
                    fr.F(0x90) = fr.F(0x24) - fr.F(0x68);
                    orig::fn_004e7780(fr.A(0x3c), fr.A(0x3c));
                    orig::fn_004e7780(fr.A(0x8c), fr.A(0x8c));
                    float dot = fr.F(0x90) * fr.F(0x40) + fr.F(0x8c) * fr.F(0x3c);
                    if (dot < K(0x005bcf64) && Dist3Sq(fr.A(0x64), fr.A(0x20)) > fr.F(0x9c))
                        goto L_aea6;
                }
                fr.F(0x18) = Dist3Sq(fr.A(0x64), fr.A(0x20));
                if (!(fr.F(0x14) > fr.F(0x18)))
                    goto L_aea6;
                if (VC(esi, 0x154, ebx) == 4)
                {
                    fr.F(0x28) = fr.F(0x28) + K(0x005bcbdc);
                    if (!static_cast<uint8_t>(orig::fn_00426fd0(fr.A(0x20), self, edi, 0)))
                        goto L_aea6;
                    fr.U(0x4c) = fr.U(0x80);
                    fr.U(0x50) = fr.U(0x84);
                    fr.U(0x54) = fr.U(0x88);
                    fr.U(0x2c) = fr.U(0x20);
                    fr.U(0x30) = fr.U(0x24);
                    fr.F(0x34) = fr.F(0x28) - K(0x005bcbdc);
                    fr.U(0x14) = fr.U(0x18);
                }
                else if (VC(esi, 0x154, ebx) == 6)
                {
                    fr.U(0x4c) = fr.U(0x80);
                    fr.U(0x50) = fr.U(0x84);
                    fr.U(0x54) = fr.U(0x88);
                    fr.F(0x14) = fr.F(0x18) * K(0x005bc15c);
                    fr.U(0x2c) = fr.U(0x20);
                    fr.U(0x30) = fr.U(0x24);
                    fr.U(0x34) = fr.U(0x28);
                }
                else
                {
                    if (VC(esi, 0x138) != 2)
                    {
                        edi = self;
                        if (!static_cast<uint8_t>(orig::fn_00426ee0(fr.A(0x20), self, fr.U(0x38), esi)))
                            goto L_aea6;
                    }
                    fr.U(0x4c) = fr.U(0x80);
                    fr.U(0x50) = fr.U(0x84);
                    fr.U(0x54) = fr.U(0x88);
                    fr.U(0x2c) = fr.U(0x20);
                    fr.U(0x30) = fr.U(0x24);
                    fr.U(0x34) = fr.U(0x28);
                    fr.U(0x14) = fr.U(0x18);
                }
                fr.U(0x1c) = esi;
                fr.U(0x48) = ebx;
            L_aea6:
                ++ebx;
            } while (ebx < VC(esi, 0x144));
        }
        bl = fr.B8(0x12);
    L_aebd:
        esi = orig::fn_004e73c0(fr.A(0x5c));
    }
    if (fr.U(0x1c) == 0)
        goto L_b289;
    if (static_cast<uint8_t>(a2))
        D(self + 0x484) |= 0x200;
    else
        D(self + 0x484) &= 0xfffffdffu;
    esi = fr.U(0x1c);
    edi = fr.U(0x48);
    ebx = self + 0x934;
    D(ebx) = fr.U(0x2c);
    D(ebx + 4) = fr.U(0x30);
    D(ebx + 8) = fr.U(0x34);
    if (VC(esi, 0x154, edi) == 4)
    {
        D(self + 0x904) = esi;
        ++I(esi + 0x28);
        VC(D(self + 0x904), 0x158, self, edi);
        D(self + 0x908) = fr.U(0x4c);
        D(self + 0x90c) = fr.U(0x50);
        D(self + 0x910) = fr.U(0x54);
        D(self + 0x914) = edi;
        if (static_cast<uint8_t>(orig::fn_00454c20(self)))
            VC(self, 0x1b8, 0xb4u);
        else
        {
            orig::fn_0046f6d0(self, G(0xaa4), fr.A(0x2c), 0, 0, 0, 0, 0);
            if (fr.F(0x14) * K(0x005bcf60) < K(0x005bcf5c))
            {
                orig::fn_0042a2c0(self);
                goto L_aff1;
            }
            Fl(self + 0x490) = orig::fn_0046e2c0(0, 0x3f800000u, 0x40000000u);
            orig::fn_00427210(self);
            VC(self, 0x1b8, 0xb5u);
        }
        goto L_aff1;
    }
    if (!fr.B8(0x12))
    {
        fr.U(0x18) = D(self + 0x5dc);
        float d = Dist3Sq(fr.A(0x2c), fr.A(0x64));
        if (!(fr.F(0x18) * fr.F(0x18) > d))
            goto L_b289;
        edi = fr.U(0x48);
        esi = fr.U(0x1c);
    }
    D(self + 0x484) &= 0xfffffff7u;
    D(self + 0x904) = esi;
    ++I(esi + 0x28);
    VC(D(self + 0x904), 0x158, self, edi);
    D(self + 0x908) = fr.U(0x4c);
    D(self + 0x90c) = fr.U(0x50);
    D(self + 0x910) = fr.U(0x54);
    D(self + 0x914) = edi;
    D(self + 0x494) = 0x40a00000u;  // 5.0f
    if (VC(esi, 0x138) == 2)
    {
        D(self + 0x484) |= 8;
        VC(self, 0x1b8, 0x1fu);
    }
    else if (VC(esi, 0x154, edi) == 6)
    {
        D(self + 0x484) &= 0xffffffefu;
        VC(self, 0x1b8, 0x28u);
    }
    else if (fr.B8(0x12))
    {
        if (By(self + 0x484) & 0x10)
            VC(self, 0x1b8, 0x21u);
        else if (D(self + 0x924) == 5)
        {
            D(self + 0x484) |= 0x10;
            D(self + 0x924) = 3;
            VC(self, 0x1b8, 0x21u);
        }
    }
    else
    {
        D(self + 0x484) &= 0xffffffefu;
        VC(self, 0x1b8, 0x1eu);
    }
    if (static_cast<uint8_t>(orig::fn_00454c20(self)) && D(self + 0xb08) != 0)
    {
        orig::fn_00453150(self, fr.A(0x8c));
        uint32_t r54 = VC(self, 0x54, fr.A(0x3c));
        fr.F(0x3c) = fr.F(0x8c) * K(0x005bcc04) + fr.F(0x3c);
        fr.F(0x40) = fr.F(0x90) * K(0x005bcc04) + fr.F(0x40);
        orig::fn_0046f5b0(r54 & 0xffffff00u, 0, fr.A(0x3c), self, G(0xaa4), 0, 0);
        orig::fn_00470aa0(self, G(0xaa4), 0);
        orig::fn_00427210(self);
        D(ebx) = fr.U(0x2c);
        D(ebx + 4) = fr.U(0x30);
        D(ebx + 8) = fr.U(0x34);
        VC(self, 0x1b8, D(self + 0x4ac));
        VC(self, 0x1b8, 0xbcu);
        goto L_b207;
    }
    {
        uint32_t game = GameP();
        uint32_t r = orig::fn_0046f6b0(game & 0xffffff00u, G(0xaa4) & 0xffffff00u, 0, fr.A(0x2c), G(0xaa4), self, 0);
        if (r & 0xd)
            goto L_b207;
    }
    VC(D(self + 0x904), 0x15c, self, D(self + 0x914));
    {
        uint32_t slot = self + 0x904;
        Unref(D(slot));
        D(slot) = 0;
    }
    orig::fn_0046f440(G(0xaa4), self);
L_b289:
    edi = fr.U(0x70);
    esi = fr.U(0x58);
L_b291:
    if (esi)
    {
        VC(esi, 0xfc, 0u, 2u);
        VC(esi, 0x54, fr.A(0x3c));
        uint32_t r = orig::fn_0046f6b0(G(0xaa4) & 0xffffff00u, 0, GameP() & 0xffffff00u, fr.A(0x3c), G(0xaa4), self, 0);
        VC(esi, 0xfc, 1u, 2u);
        if (r != 1)
            goto L_b3ec;
        D(self + 0x898) = esi;
        ++I(esi + 0x28);
        VC(D(self + 0x898), 0x15c, 1u);
        orig::fn_00427210(self);
        VC(self, 0x1b8, 0x7bu);
        goto L_b318;
    }
    if (!edi)
        goto L_b3ec;
    D(self + 0x914) = 0;
    VC(edi, 0x14c, fr.A(0x3c), self + 0x908, 0u, self, D(self + 0x588), 0u);
    if (static_cast<uint32_t>(orig::fn_0046f6d0(self, G(0xaa4), fr.A(0x3c), 0, 0, 0, 0, 0)) != 1)
        goto L_b3ec;
    D(self + 0x904) = edi;
    D(self + 0x914) = 0;
    ++I(edi + 0x28);
    VC(D(self + 0x904), 0x158, self, D(self + 0x914));
    orig::fn_00427210(self);
    VC(self, 0x1b8, 0xa0u);
L_b318:
    if (static_cast<uint8_t>(a2))
        D(self + 0x484) |= 0x200;
    else
        D(self + 0x484) &= 0xfffffdffu;
    goto L_aff1;
L_b207:
    orig::fn_00427210(self);
L_aff1:
    if (fr.U(0x60))
        --I(fr.U(0x60) + 0x18);
    return 1;
L_b3ec:
    if (D(self + 0xb08) == 0)
    {
        if (By(self + 0x484) & 4)
        {
            VC(self, 0x184);
            goto L_b44a;
        }
        fn_0042df70(self, 0x42340000u);
    }
    VC(self, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(self + 0x580), D(self + 0x584));
    VC(self, 0x1b8, 0x32u);
L_b44a:
    D(self + 0x484) &= 0xfffffdffu;
    if (fr.U(0x60))
        --I(fr.U(0x60) + 0x18);
    return 0;
}

static uint32_t fn_0042a7b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0042a7b0(ecx, a0, a1, a2);
}
ABI_ENTRY_0042a7b0(fn_0042a7b0_Entry, fn_0042a7b0_Abi)
HOOK(0x0042a7b0, "GenericAI_FindHidingSpot", fn_0042a7b0_Entry, nullptr);

// 0x0042b480 (stack: AI object, a1 (bool); ret 8): "turbo launch" to a jump pad (string "FX_turbo_Launch_captain"
// 0x005b6894 passed to 0x0047bba0 with the object +0xb08). No target (+0x588) and !a1: virtual +0x240 (0x0042a7b0,
// the hiding-spot search) with (0, 0, 1), returns 1. Walks the list World(Game+0xa84)+0xf10 (skipping +0x918/+0x91c,
// slot 0 not free (virtual +0x148), or a different owner +0x464 than self+0xb14), rejects candidates closer than
// self+0xb1c, flags those beyond self+0xb18, and keeps the one nearest the target (2D squared). Found: takes it
// (+0x904 with a reference, virtual +0x158, slot position virtual +0x14c into +0x934), launches (0x00471a00 velocity,
// Body_setLinearVelocity 0x00458510), links self into World+0xf1c, animation 0x6b (virtual +0x19c) and state 0xc4/0xc5
// (virtual +0x1b8); returns 1. None: a1 ? 0 : (virtual +0x240 (0, 0, 1), 1). Callers 0x004295d0, 0x0043ee90. Not
// executed in recorded runs.
// Frame: fr.U(o) is [esp+o] with the four registers pushed (sub esp, 0x40: o = 0x10..0x4f); 0x10 best distance, 0x14
// squared distance to the target, 0x18/0x1c list iterator, 0x20 own position, 0x2c target position, 0x38 candidate
// position (later the launch velocity), 0x44 slot vector. The flag at [esp+0x54] reuses the first argument's slot.
uint32_t fn_0042b480(uint32_t self, uint32_t a1)
{
    Frame<0x50> fr;
    uint8_t flag = 0;
    uint32_t ebx = 0, esi;
    uint32_t target = D(self + 0x588);
    if (target == 0)
    {
        if (!static_cast<uint8_t>(a1))
        {
            VC(self, 0x240, 0u, 0u, 1u);
            return 1;
        }
    }
    else
        VC(target, 0x54, fr.A(0x2c));
    VC(self, 0x54, fr.A(0x20));
    {
        float dy = fr.F(0x30) - fr.F(0x24);
        float dx = fr.F(0x2c) - fr.F(0x20);
        fr.F(0x14) = dx * dx + dy * dy;
    }
    flag = 0;
    esi = orig::fn_004e7340(D(G(0xa84) + 0xf10), fr.A(0x18));
    while (esi)
    {
        if (esi == D(self + 0x918) || esi == D(self + 0x91c))
            goto L_next;
        if (!VB(esi, 0x148, 0u))
            goto L_next;
        if (D(esi + 0x464) && D(self + 0xb14) && D(esi + 0x464) != D(self + 0xb14))
            goto L_next;
        VC(esi, 0x54, fr.A(0x38));
        {
            float dy = fr.F(0x3c) - fr.F(0x24);
            float dx = fr.F(0x38) - fr.F(0x20);
            float dc = dx * dx + dy * dy;
            float rmin = Fl(self + 0xb1c);
            if (dc < rmin * rmin)
                goto L_next;
            float rmax = Fl(self + 0xb18);
            bool far_ = dc > rmax * rmax;
            float dt;
            if (D(self + 0x588))
            {
                float ty = fr.F(0x30) - fr.F(0x3c);
                float tx = fr.F(0x2c) - fr.F(0x38);
                dt = tx * tx + ty * ty;
            }
            else
                dt = K(0x0056c05c);
            if (flag)
            {
                if (!(dt < fr.F(0x10)))
                    goto L_next;
                float r = Fl(self + 0x5d8);
                if (!(dt < r * r))
                    goto L_next;
                if (!(fr.F(0x14) > dt))
                    goto L_next;
                if (far_)
                    goto L_next;
                fr.F(0x10) = dt;
                ebx = esi;
            }
            else
            {
                if (ebx && !(dt < fr.F(0x10)) && far_)
                    goto L_next;
                if (fr.F(0x14) > dt && !far_)
                    flag = 1;
                fr.F(0x10) = dt;
                ebx = esi;
            }
        }
    L_next:
        esi = orig::fn_004e73c0(fr.A(0x18));
    }
    if (ebx == 0)
    {
        if (static_cast<uint8_t>(a1))
        {
            if (fr.U(0x1c))
                --I(fr.U(0x1c) + 0x18);
            return 0;
        }
        VC(self, 0x240, 0u, 0u, 1u);
    }
    else
    {
        if (D(self + 0x904))
            VC(self, 0x188);
        D(self + 0x904) = ebx;
        D(self + 0x914) = 0;
        ++I(ebx + 0x28);
        VC(D(self + 0x904), 0x158, self, D(self + 0x914));
        VC(D(self + 0x904), 0x14c, self + 0x934, fr.A(0x44), 0u, self, D(self + 0x588), 0u);
        {
            float speed = Fl(self + 0xaec) * K(0x005bcd64);
            float h2 = Fl(self + 0x948) + Fl(self + 0x948);
            orig::fn_00471a00(fr.A(0x38), self + 0x934, fr.A(0x20), fb(h2), fb(speed), 0);
        }
        D(self + 0x484) &= 0xdfffffffu;
        if (fr.F(0x40) <= K(0x0056c05c))
            fr.U(0x40) = 0x40a00000u;  // 5.0f
        orig::fn_00454c90(self, 0, 1);
        D(self + 0x39c) = orig::fn_00455a00(D(self + 0x39c));
        D(self + 0x398) = orig::fn_00455a00(D(self + 0x398));
        D(self + 0xb10) = orig::fn_00455a00(D(self + 0xb10));
        orig::fn_00458510(fr.A(0x38), self);
        D(self + 0x484) |= 0x10000010u;
        orig::fn_0047bba0(D(self + 0xb08), 0x005b6894u);
        VC(self, 0x19c, 0x6bu, 1u, 0u, 0u, 0u, 0u);
        orig::fn_004546d0(D(G(0xa84) + 0xf1c), self);
        VC(self, 0x1b8, flag ? 0xc4u : 0xc5u);
    }
    if (fr.U(0x1c))
        --I(fr.U(0x1c) + 0x18);
    return 1;
}

static uint32_t fn_0042b480_Abi(uint32_t a0, uint32_t a1)
{
    return fn_0042b480(a0, a1);
}
ABI_ENTRY_0042b480(fn_0042b480_Entry, fn_0042b480_Abi)
HOOK(0x0042b480, "fn_0042b480", fn_0042b480_Entry, nullptr);

namespace
{
// 0x00566ad0: the CRT's _CIacos (argument and result in st0).
float CIacos(float x)
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

// 0x0042b860 (ecx = object or 0, esi = facing vec2 (normalised in place), edi = position; stack: a0 (unused; callers
// pass the AI object), max angle in degrees; ret 8): true when the object lies within the angle of the facing
// direction: acos(dot(normalise(object position - position), normalise(facing))) * 57.29578 (0x005bcdb8) < angle.
// 0x004e7780 is the in-place 2D normalise. Not executed in recorded runs.
uint32_t fn_0042b860(uint32_t object, uint32_t facing, uint32_t pos, uint32_t /*a0*/, uint32_t angle)
{
    Frame<0x18> fr;
    if (!object)
        return 0;
    VC(object, 0x54, fr.A(0xc));
    fr.F(0) = fr.F(0xc) - Fl(pos);
    fr.F(4) = fr.F(0x10) - Fl(pos + 4);
    orig::fn_004e7780(fr.A(0), fr.A(0));
    orig::fn_004e7780(facing, facing);
    float dot = fr.F(4) * Fl(facing + 4) + fr.F(0) * Fl(facing);
    float a = CIacos(dot);
    return K(0x005bcdb8) * a < bf(angle) ? 1u : 0u;
}

static uint32_t fn_0042b860_Abi(uint32_t ecx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_0042b860(ecx, esi, edi, a0, a1);
}
ABI_ENTRY_0042b860(fn_0042b860_Entry, fn_0042b860_Abi)
HOOK(0x0042b860, "fn_0042b860", fn_0042b860_Entry, nullptr);

uint32_t fn_0042bc60(uint32_t eax, uint32_t self, uint32_t a1, uint32_t a2);

// 0x0042b8e0 (ebx = AI object; the listed ecx input is only a `push ecx` stack-slot reservation). With +0x484 bit 17
// (barricade AI): when the target (+0x588) is within 80 degrees of the slot direction +0x908 seen from +0x934
// (0x0042b860), animation 0x66 (virtual +0x19c) and state 0xa2; otherwise leaves its cover (virtual +0x188) and
// searches again (virtual +0x240 (1, 1, 0)). Without: leaves its cover when it has one, then virtual +0x180 (target,
// 2 * +0x5dc); true: virtual +0x240 (1, 0, 0); false: drops the target (+0x588, reference) and calls 0x0042bc60 with
// eax = the dropped target pointer (as the original). Returns the last callee's eax.
uint32_t fn_0042b8e0(uint32_t self)
{
    if (D(self + 0x484) & 0x20000)
    {
        if (fn_0042b860(D(self + 0x588), self + 0x908, self + 0x934, self, 0x42a00000u))
        {
            VC(self, 0x19c, 0x66u, 0u, 0u, 0u, 0u, 0u);
            return VC(self, 0x1b8, 0xa2u);
        }
        VC(self, 0x188);
        return VC(self, 0x240, 1u, 1u, 0u);
    }
    if (D(self + 0x904))
        VC(self, 0x188);
    float r2 = Fl(self + 0x5dc) + Fl(self + 0x5dc);
    if (VB(self, 0x180, D(self + 0x588), fb(r2)))
        return VC(self, 0x240, 1u, 0u, 0u);
    uint32_t slot = self + 0x588;
    uint32_t old = D(slot);
    if (old)
    {
        Unref(old);
        D(slot) = 0;
    }
    return fn_0042bc60(old, self, 1, 1);
}

static uint32_t fn_0042b8e0_Abi(uint32_t /*ecx*/, uint32_t ebx)
{
    return fn_0042b8e0(ebx);
}
ABI_ENTRY_0042b8e0(fn_0042b8e0_Entry, fn_0042b8e0_Abi)
HOOK(0x0042b8e0, "fn_0042b8e0", fn_0042b8e0_Entry, nullptr);

// 0x0042b9e0 (eax = AI object): true when it is alive, +0x48c bit 13 is clear and either its state (+0x460) is 0x11
// or (+0x488 bit 18 clear, bit 20 set and +0x484 bit 2 set). Callers 0x0042ba30, 0x0042bb10. Not executed in
// recorded runs.
uint32_t fn_0042b9e0(uint32_t obj)
{
    if (static_cast<uint8_t>(orig::fn_00454780(obj)))
        return 0;
    if (D(obj + 0x48c) & 0x2000)
        return 0;
    if (D(obj + 0x460) == 0x11)
        return 1;
    uint32_t f = D(obj + 0x488);
    if (f & 0x40000)
        return 0;
    if (!(f & 0x100000))
        return 0;
    if (!(By(obj + 0x484) & 4))
        return 0;
    return 1;
}

static uint32_t fn_0042b9e0_Abi(uint32_t eax)
{
    return fn_0042b9e0(eax);
}
ABI_ENTRY_0042b9e0(fn_0042b9e0_Entry, fn_0042b9e0_Abi)
HOOK(0x0042b9e0, "fn_0042b9e0", fn_0042b9e0_Entry, nullptr);

// 0x0042ba30 (ebx = AI object): clears +0x488 bit 18, unlinks the object from the list World(Game+0xa84)+0xf14
// (ObjectList_Unlink 0x00454750), then walks World+0xe7c for another object whose virtual +0x10c is true and that
// passes 0x0042b9e0, and stops at the first one for which 0x00429ce0, 0x00429ac0 or 0x00429a50 succeeds (ending the
// iteration through the list's virtual +0xc). Returns eax as left: the node's decremented lock count when found, else
// the iterator's node word. Caller 0x00432730. Not executed in recorded runs.
uint32_t fn_0042ba30(uint32_t self)
{
    uint32_t it[2] = {0, 0};
    D(self + 0x488) &= 0xfffbffffu;
    orig::fn_00454750(D(G(0xa84) + 0xf14), self);
    uint32_t esi = orig::fn_004e7340(D(G(0xa84) + 0xe7c), u(it));
    while (esi)
    {
        if (VB(esi, 0x10c) && fn_0042b9e0(esi) && esi != self)
        {
            if (static_cast<uint8_t>(orig::fn_00429ce0(esi)) || static_cast<uint8_t>(orig::fn_00429ac0(esi)) ||
                static_cast<uint8_t>(orig::fn_00429a50(esi)))
            {
                uint32_t node = it[1];
                VC(it[0], 0xc, node);
                return static_cast<uint32_t>(--I(node + 0x18));
            }
        }
        esi = orig::fn_004e73c0(u(it));
    }
    if (it[1])
        --I(it[1] + 0x18);
    return it[1];
}

static uint32_t fn_0042ba30_Abi(uint32_t ebx)
{
    return fn_0042ba30(ebx);
}
ABI_ENTRY_0042ba30(fn_0042ba30_Entry, fn_0042ba30_Abi)
HOOK(0x0042ba30, "fn_0042ba30", fn_0042ba30_Entry, nullptr);

uint32_t fn_0042c6f0(uint32_t obj);

// 0x0042bb10 (eax = AI object): unlinks it from the list World(Game+0xa84)+0xf18, clears +0x488 bit 17, counts the
// objects of World+0xe7c whose virtual +0x10c is true, that pass 0x0042b9e0 and for which 0x0042c6f0 is non-zero,
// rolls n = (rand() * count) >> 15 and calls virtual +0x230 (0) on the n-th of them (counting down: `dec ebx; je`),
// ending the iteration through the list's virtual +0xc. Original bug: a roll of 0 selects nothing (the countdown starts
// at 0 and wraps). Returns eax as left (see 0x0042ba30). Callers 0x00428980, 0x00432730, 0x0043ee90. Not executed in
// recorded runs.
uint32_t fn_0042bb10(uint32_t self)
{
    uint32_t it[2] = {0, 0};
    int32_t count = 0;
    orig::fn_00454750(D(G(0xa84) + 0xf18), self);
    D(self + 0x488) &= 0xfffdffffu;
    uint32_t esi = orig::fn_004e7340(D(G(0xa84) + 0xe7c), u(it));
    while (esi)
    {
        if (VB(esi, 0x10c) && fn_0042b9e0(esi) && fn_0042c6f0(esi) != 0)
            ++count;
        esi = orig::fn_004e73c0(u(it));
    }
    if (count != 0)
    {
        int32_t n = (Rand() * count) >> 15;
        esi = orig::fn_004e7340(D(G(0xa84) + 0xe7c), u(it));
        while (esi)
        {
            if (VB(esi, 0x10c) && fn_0042b9e0(esi) && fn_0042c6f0(esi) != 0 && --n == 0)
            {
                VC(esi, 0x230, 0u);
                uint32_t node = it[1];
                VC(it[0], 0xc, node);
                return static_cast<uint32_t>(--I(node + 0x18));
            }
            esi = orig::fn_004e73c0(u(it));
        }
    }
    if (it[1])
        --I(it[1] + 0x18);
    return it[1];
}

static uint32_t fn_0042bb10_Abi(uint32_t eax)
{
    return fn_0042bb10(eax);
}
ABI_ENTRY_0042bb10(fn_0042bb10_Entry, fn_0042bb10_Abi)
HOOK(0x0042bb10, "fn_0042bb10", fn_0042bb10_Entry, nullptr);

uint32_t fn_0042be80(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2);

// 0x0042bc60 (eax = object (the dropped target, possibly already released: only compared and passed on); stack: AI
// object, a1 (bool), a2 (bool); ret 0xc): the AI's reaction to losing its target. Guarding AI (+0x484 bit 25):
// animation (virtual +0x1a0: 2, 3, 0, 1, 1, 0, 0.5, 0.5) and state 0x7a. Barricade AI (bit 17): animation 0x66
// (virtual +0x19c) and state 0xa2. Otherwise +0x490 = 0 and virtual +0x22c (0x0042be80: frame time, 0, a1) may find
// a new target (then 0x004246a0). If not and a2: without +0x484 bit 2 it idles (animation 0x2a.., state 0x32, or 0xc1
// when it holds cover +0x904); with bit 2, when the object is still referenced (virtual +0xd8), virtual +0x1e8 holds
// and the object is the AI's +0x858 (0x00453290) or in its list +0x604, it leaves cover and calls virtual +0x184;
// else it leaves cover, animation 0x0a, 0x0046f6d0 towards +0x4c0 and state 0x50. Returns the last callee's eax.
// Callers 0x00427e80, 0x0042b8e0, 0x0043ee90, 0x00485910. Not executed in recorded runs.
uint32_t fn_0042bc60(uint32_t obj, uint32_t self, uint32_t a1, uint32_t a2)
{
    uint32_t flags = D(self + 0x484);
    if (flags & 0x2000000)
    {
        VC(self, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
        return VC(self, 0x1b8, 0x7au);
    }
    if (flags & 0x20000)
    {
        VC(self, 0x19c, 0x66u, 0u, 0u, 0u, 0u, 0u);
        return VC(self, 0x1b8, 0xa2u);
    }
    D(self + 0x490) = 0;
    uint32_t r = fn_0042be80(self, D(0x00604a90), 0, a1);
    if (static_cast<uint8_t>(r))
        return orig::fn_004246a0(self);
    if (!static_cast<uint8_t>(a2))
        return r;
    if (!(By(self + 0x484) & 4))
    {
        VC(self, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(self + 0x580), D(self + 0x584));
        return VC(self, 0x1b8, D(self + 0x904) == 0 ? 0x32u : 0xc1u);
    }
    if (obj)
    {
        uint32_t out = a2;
        uint32_t esi = static_cast<uint8_t>(orig::fn_00453290(obj, u(&out))) ? out : 0;
        if (VB(obj, 0xd8) && VB(self, 0x1e8))
        {
            if ((esi && esi == D(self + 0x858)) || static_cast<uint8_t>(orig::fn_00454690(D(self + 0x604), obj)))
            {
                VC(self, 0x188);
                return VC(self, 0x184);
            }
        }
    }
    VC(self, 0x188);
    VC(self, 0x19c, 0xau, 1u, 0u, 0u, 0u, 0u);
    orig::fn_0046f6d0(self, G(0xaa4), self + 0x4c0, 0, 0, 0, 0, 0);
    return VC(self, 0x1b8, 0x50u);
}

static uint32_t fn_0042bc60_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0042bc60(eax, a0, a1, a2);
}
ABI_ENTRY_0042bc60(fn_0042bc60_Entry, fn_0042bc60_Abi)
HOOK(0x0042bc60, "fn_0042bc60", fn_0042bc60_Entry, nullptr);

// 0x0042be80 (ecx = AI object; stack: dt (float), a1 (bool), a2 (unused); ret 0xc). GenericAIObject slot 139
// (+0x22c), the target update: without a target (+0x588) it looks for one (0x00426920 with the range +0x5dc) and
// adopts it (virtual +0x23c (target, 1)). With a target: copies its position to +0x60c; unless +0x488 bit 16, starts
// pursuing (virtual +0x230 (1)) or, holding cover (+0x904), 0x00427c80 (1); returns 1. Without: unless +0x488 bit 28,
// counts the timer +0x494 down by dt; when a1 and (+0x484 bit 6 or the timer ran out): 0x00427330 and a new timer
// rand() * K(0x005bcd54) + K(0x005bcd50). Returns 0. Caller 0x00485490, 0x0042bc60.
uint32_t fn_0042be80(uint32_t self, uint32_t dt, uint32_t a1, uint32_t /*a2*/)
{
    if (D(self + 0x588) == 0)
    {
        uint32_t t = orig::fn_00426920(self, D(self + 0x5dc), 0, 0);
        if (t)
            VC(self, 0x23c, t, 1u);
    }
    if (D(self + 0x588))
    {
        VC(D(self + 0x588), 0x54, self + 0x60c);
        if (!(D(self + 0x488) & 0x10000))
        {
            if (D(self + 0x904) == 0)
                VC(self, 0x230, 1u);
            else
                orig::fn_00427c80(self, 1);
        }
        return 1;
    }
    if (D(self + 0x488) & 0x10000000)
        return 0;
    float t = Fl(self + 0x494) - bf(dt);
    Fl(self + 0x494) = t;
    if (!static_cast<uint8_t>(a1))
        return 0;
    if (!(By(self + 0x484) & 0x40) && !(t <= K(0x0056c05c)))
        return 0;
    orig::fn_00427330(self);
    Fl(self + 0x494) = static_cast<float>(Rand()) * K(0x005bcd54) + K(0x005bcd50);
    return 0;
}

static uint32_t fn_0042be80_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0042be80(ecx, a0, a1, a2);
}
ABI_ENTRY_0042be80(fn_0042be80_Entry, fn_0042be80_Abi)
HOOK(0x0042be80, "fn_0042be80", fn_0042be80_Entry, nullptr);

uint32_t fn_0042c410(uint32_t self, uint32_t a0, uint32_t a1);

// 0x0042bf70 (edi = AI object; stack: a0 (bool); ret 4): stops the body (0x00455a00 on +0x39c / +0x398, zero angular
// and linear velocity 0x00458580 / 0x00458510), releases the held object +0xab4 (0x0044b7c0 (eax = +0x570, cl = 1)
// first), resets +0x61c (0x004a0e00, al = 0), applies virtual +0x248 (1, &out) through virtual +0x238 (0x0042c410)
// and plays animation 0x80 on +0xab8 (fn_00408e30) with blend 0.01 (a0) or 0.25; sets +0xa18 bit 1. Callers
// 0x0042c0e0 and others.
uint32_t fn_0042bf70(uint32_t self, uint32_t a0)
{
    uint32_t loc[4] = {0, 0, 0, 0};  // [esp+0] out word, [esp+4..0xc] zero vector
    D(self + 0x39c) = orig::fn_00455a00(D(self + 0x39c));
    D(self + 0x398) = orig::fn_00455a00(D(self + 0x398));
    orig::fn_00458580(u(&loc[1]), self);
    orig::fn_00458510(u(&loc[1]), self);
    uint32_t slot = self + 0xab4;
    if (D(slot))
    {
        orig::fn_0044b7c0(D(self + 0x570), 1);
        Unref(D(slot));
        D(slot) = 0;
    }
    orig::fn_004a0e00(0, self + 0x61c);
    uint32_t r = VC(self, 0x248, 1u, u(&loc[0]));
    fn_0042c410(self, r, loc[0]);
    uint32_t res = orig::fn_00408e30(0x80, self, 0xffffffffu, self + 0xab8, 0,
                                     static_cast<uint8_t>(a0) ? 0x3c23d70au : 0x3e800000u, 0, 0);
    D(self + 0xa18) |= 2;
    return res;
}

static uint32_t fn_0042bf70_Abi(uint32_t edi, uint32_t a0)
{
    return fn_0042bf70(edi, a0);
}
ABI_ENTRY_0042bf70(fn_0042bf70_Entry, fn_0042bf70_Abi)
HOOK(0x0042bf70, "fn_0042bf70", fn_0042bf70_Entry, nullptr);

// 0x0042c0e0 (ecx = AI object; stack: a0 (bool); ret 4). GenericAIObject slot 107 (+0x1ac): wakes the AI back up
// (only when virtual +0xd8 and 0x00426630 hold): drops the object +0xab0 (calling its virtual +0x1ac (1) when it is
// still kept), clears +0xad8/+0xad9, +0xa18 bits 0-1, +0x488 bit 25 and +0x484 bit 13, re-applies its transform
// (virtual +0x5c into a 0x44-byte matrix, Object_SetTransform 0x00457990), virtual +0xf8 (0x170001, 0); with a0 it
// also stops the body. Bumps the animation-set serial +0x410+8 into +0x1d0 (log 0x0058af8c when missing), hooks the
// callback 0x0042c080 into the current animation's channel table, then state +0x4ac (0xab: 0x0042bf70 (1)). The
// second and third releases test +0xab0 after it was cleared (dead code, reproduced).
uint32_t fn_0042c0e0(uint32_t self, uint32_t a0)
{
    uint32_t mat[17];
    uint32_t zero[3];
    if (!VB(self, 0xd8))
        return 0;
    if (!static_cast<uint8_t>(orig::fn_00426630(self)))
        return 0;
    uint32_t ebp = self + 0xab0;
    uint32_t esi = D(ebp);
    if (esi)
    {
        Unref(esi);
        D(ebp) = 0;
        D(self + 0x484) &= 0xffffdfffu;
        if (VB(esi, 0xd8))
            VC(esi, 0x1ac, 1u);
    }
    By(self + 0xad8) = 0;
    By(self + 0xad9) = 0;
    VC(self, 0x5c, u(mat));
    D(self + 0xa18) &= 0xfffffffeu;
    D(self + 0xa18) &= 0xfffffffdu;
    D(self + 0x488) &= 0xfdffffffu;
    if (By(self + 0xa18) & 4)
    {
        esi = D(self + 0x588);
        if (esi && esi == D(ebp))
        {
            Unref(esi);
            D(self + 0x588) = 0;
        }
        D(self + 0xa18) &= 0xfffffffbu;
    }
    esi = D(ebp);
    if (esi)
    {
        Unref(esi);
        D(ebp) = 0;
        D(self + 0x484) &= 0xffffdfffu;
    }
    orig::fn_00457990(0, self, u(mat));
    VC(self, 0xf8, 0x170001u, 0u);
    if (static_cast<uint8_t>(a0))
    {
        D(self + 0x39c) = orig::fn_00455a00(D(self + 0x39c));
        D(self + 0x398) = orig::fn_00455a00(D(self + 0x398));
        zero[0] = zero[1] = zero[2] = 0;
        orig::fn_00458510(u(zero), self);
    }
    D(self + 0x484) &= 0xffffdfffu;
    {
        uint32_t s = D(self + 0x410);
        uint32_t v;
        if (s)
        {
            if (++D(s + 8) == 0)
                D(s + 8) = 1;
            v = D(s + 8);
        }
        else
        {
            GameLog(reinterpret_cast<const char*>(0x0058af8c));
            v = 0;
        }
        D(self + 0x1d0) = v;
    }
    {
        uint32_t idx = D(orig::fn_00408800(4, self));
        uint32_t e = D(D(self + 0x414) + 0xc) + idx * 12;
        uint32_t a = D(D(D(e + 8) + 0x2c) + 4);
        uint32_t c = D(a + 0x1c);
        if (c && D(a + 0x20) != 0)
        {
            uint32_t s = D(a + 0x24);
            D(s) = c;
            D(s + 0x18) = self + 0xab8;
            orig::fn_004e0f40(s);
            if (D(s + 0xc) == 0)
            {
                uint32_t p = D(D(s + 0x10) + D(s + 8) * 4);
                if (p)
                {
                    p = D(p + *reinterpret_cast<uint16_t*>(p + 2));
                    if (p)
                    {
                        uint32_t t = D(D(self + 0x410) + 0x14);
                        if (t)
                        {
                            D(t + *reinterpret_cast<uint16_t*>(p + 0xa) * 8 + 4) = p;
                            D(D(D(self + 0x410) + 0x14) + *reinterpret_cast<uint16_t*>(p + 0xa) * 8) = 0x0042c080u;
                        }
                    }
                }
            }
        }
    }
    uint32_t st = D(self + 0x4ac);
    if (st == 0xab)
        return fn_0042bf70(self, 1);
    return VC(self, 0x1b8, st);
}

static uint32_t fn_0042c0e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0042c0e0(ecx, a0);
}
ABI_ENTRY_0042c0e0(fn_0042c0e0_Entry, fn_0042c0e0_Abi)
HOOK(0x0042c0e0, "fn_0042c0e0", fn_0042c0e0_Entry, nullptr);

// 0x0042c380 (esi = AI object): when 0x00426630 holds and +0xa18 bit 1 is clear: sets the current animation's speed
// (0x00408800 (4) record +4) to +0xa20 divided by the length of the animation named +0xab8 (0x004f1160 lookup in
// [[+0x414]+0x10], table [+0x414]+0xc, 12-byte entries; -1 when not found), plays animation 0x200 on +0xab8 (blend
// 0.25, last argument 1) and sets +0xa18 bit 1. Returns eax as left (the new +0xa18, or the failed test). Callers
// 0x00436980, 0x0043d180, 0x0043ee90, 0x00485910. Not executed in recorded runs.
uint32_t fn_0042c380(uint32_t self)
{
    uint32_t r = orig::fn_00426630(self);
    if (!static_cast<uint8_t>(r))
        return r;
    if (By(self + 0xa18) & 2)
        return r;
    uint32_t rec = orig::fn_00408800(4, self);
    uint32_t name = self + 0xab8;
    uint32_t p = orig::fn_004f1160(D(D(D(self + 0x414) + 0x10)), name);
    int32_t idx = p ? I(p) : -1;
    Fl(rec + 4) = Fl(self + 0xa20) / Fl(D(D(self + 0x414) + 0xc) + static_cast<uint32_t>(idx) * 12);
    orig::fn_00408e30(0x200, self, 0xffffffffu, name, 0, 0x3e800000u, 0, 1);
    D(self + 0xa18) |= 2;
    return D(self + 0xa18);
}

static uint32_t fn_0042c380_Abi(uint32_t esi)
{
    return fn_0042c380(esi);
}
ABI_ENTRY_0042c380(fn_0042c380_Entry, fn_0042c380_Abi)
HOOK(0x0042c380, "fn_0042c380", fn_0042c380_Entry, nullptr);

// 0x0042c410 (ecx = AI object; stack: animation name, state; ret 8). GenericAIObject slot 142 (+0x238): starts an
// "IC" (in-character, guess) animation: copies the name to +0xab8, saves the transforms (virtual +0x70 into +0xa28,
// virtual +0x5c into +0xa6c), clears +0x488 bit 23, +0xa20/+0xa24, looks the animation up (0x0045ace0 in
// World(Game+0xa84)) into +0xa1c (missing: "AI : Can't find IC Anim\n%s\n%s" 0x005b680c), unhooks the callback of
// its channel table, sets +0xa18 bit 0, disables physics (0x004577b0 (0)), +0x4ac = state and state 0x64 (virtual
// +0x1b8). With +0x484 bit 19 and a held object +0xab4: 0x00429870 (0), the object gets self's transform moved by
// its axes (constants 0x005bcf40 / 0x005bcf3c / 0x005bcf38) and plays "mover_ladder_plant" (0x005b67f8, also its
// virtual +0x238); then clears +0x488 bit 15. A missing animation (+0xa1c = 0) is dereferenced as in the original.
// Caller 0x00436980.
// Frame: fr.U(o) is [esp+o] with the three registers pushed (sub esp, 0x68: o = 0x0c..0x73); 0x0c position, 0x18 /
// 0x24 axes, 0x30 matrix (0x44 bytes).
uint32_t fn_0042c410(uint32_t self, uint32_t name, uint32_t state)
{
    Frame<0x74> fr;
    uint32_t ebp = self + 0xab8;
    {
        uint32_t s = name;
        uint8_t c;
        do
        {
            c = By(s);
            By(ebp + (s - name)) = c;
            ++s;
        } while (c);
    }
    VC(self, 0x70, self + 0xa28);
    VC(self, 0x5c, self + 0xa6c);
    D(self + 0x488) &= 0xff7fffffu;
    D(self + 0xa24) = 0;
    D(self + 0xa20) = 0;
    D(self + 0xa1c) = orig::fn_0045ace0(ebp, G(0xa84));
    if (D(self + 0xa1c) == 0)
        DebugPrintf(G(0x34), 0x005b680cu, ebp, D(orig::fn_004555d0(self) + 0x10) + 4);
    {
        uint32_t a = D(D(D(self + 0xa1c) + 0x2c) + 4);
        uint32_t c = D(a + 0x1c);
        if (c && D(a + 0x20) != 0)
        {
            uint32_t s = D(a + 0x24);
            D(s) = c;
            D(s + 0x18) = ebp;
            orig::fn_004e0f40(s);
            if (D(s + 0xc) == 0)
            {
                uint32_t p = D(D(s + 0x10) + D(s + 8) * 4);
                if (p)
                {
                    p = D(p + *reinterpret_cast<uint16_t*>(p + 2));
                    if (p)
                    {
                        uint32_t t = D(D(self + 0x410) + 0x14);
                        if (t)
                        {
                            D(t + *reinterpret_cast<uint16_t*>(p + 0xa) * 8 + 4) = 0;
                            D(D(D(self + 0x410) + 0x14) + *reinterpret_cast<uint16_t*>(p + 0xa) * 8) = 0;
                        }
                    }
                }
            }
        }
    }
    D(self + 0xa18) |= 1;
    orig::fn_004577b0(self, 0);
    D(self + 0x4ac) = state;
    uint32_t r = VC(self, 0x1b8, 0x64u);
    if (!(D(self + 0x484) & 0x80000))
        return r;
    if (D(self + 0xab4))
    {
        orig::fn_00429870(self, 0);
        VC(self, 0x70, fr.A(0x30));
        uint32_t held = D(self + 0xab4);
        orig::fn_00457990(held & 0xffffff00u, held, fr.A(0x30));
        orig::fn_00453150(D(self + 0xab4), fr.A(0x24));
        orig::fn_004531b0(D(self + 0xab4), fr.A(0x18));
        uint32_t r54 = VC(D(self + 0xab4), 0x54, fr.A(0xc));
        fr.F(0xc) = fr.F(0x18) * K(0x005bcf40) + fr.F(0x24) * K(0x005bcf3c) + fr.F(0xc);
        fr.F(0x10) = fr.F(0x1c) * K(0x005bcf40) + fr.F(0x28) * K(0x005bcf3c) + fr.F(0x10);
        fr.F(0x14) = fr.F(0x14) + K(0x005bcf38);
        orig::fn_00457870(r54 & 0xffffff00u, fr.A(0xc), D(self + 0xab4), 0, 0, 0);
        orig::fn_00408e30(0x80, D(self + 0xab4), 0xffffffffu, 0x005b67f8u, 0, 0, 0, 0);
        VC(D(self + 0xab4), 0x238, 0x005b67f8u, 0u);
    }
    D(self + 0x488) &= 0xffff7fffu;
    return D(self + 0x488);
}

static uint32_t fn_0042c410_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0042c410(ecx, a0, a1);
}
ABI_ENTRY_0042c410(fn_0042c410_Entry, fn_0042c410_Abi)
HOOK(0x0042c410, "GenericAI_PlayICAnim", fn_0042c410_Entry, nullptr);

// 0x0042c670 (ecx = target, esi = AI object; stack: a0 (state), a1 (animation selector); ret 8): adopts the target
// (virtual +0x23c (target, 1)), starts the IC animation named by virtual +0x1a4 (a1) with state a0 (virtual +0x238,
// 0x0042c410), plays animation 0x80 on +0xab8 (blend 0.25), sets +0xa18 bits 1 and 2 and disables physics
// (0x004577b0 (0)). Caller 0x0043ee90. Not executed in recorded runs.
uint32_t fn_0042c670(uint32_t target, uint32_t self, uint32_t a0, uint32_t a1)
{
    VC(self, 0x23c, target, 1u);
    uint32_t name = VC(self, 0x1a4, a1);
    VC(self, 0x238, name, a0);
    orig::fn_00408e30(0x80, self, 0xffffffffu, self + 0xab8, 0, 0x3e800000u, 0, 0);
    uint32_t f = D(self + 0xa18) | 2;
    D(self + 0xa18) = f;
    D(self + 0xa18) = f | 4;
    return static_cast<uint32_t>(orig::fn_004577b0(self, 0));
}

static uint32_t fn_0042c670_Abi(uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return fn_0042c670(ecx, esi, a0, a1);
}
ABI_ENTRY_0042c670(fn_0042c670_Entry, fn_0042c670_Abi)
HOOK(0x0042c670, "fn_0042c670", fn_0042c670_Entry, nullptr);

// 0x0042c6f0 (eax = AI object): returns its target +0x588.
uint32_t fn_0042c6f0(uint32_t obj)
{
    return D(obj + 0x588);
}

static uint32_t fn_0042c6f0_Abi(uint32_t eax)
{
    return fn_0042c6f0(eax);
}
ABI_ENTRY_0042c6f0(fn_0042c6f0_Entry, fn_0042c6f0_Abi)
HOOK(0x0042c6f0, "GenericAI_GetTarget", fn_0042c6f0_Entry, nullptr);

// 0x0042c700 (eax = AI object, ebx = other object; stack: group id; ret 4): when the object is alive, its +0x574
// equals the id, the other object is not itself, +0x488 bit 14 is clear and its timer +0x578 ran out (<= 0): new
// timer random 0.25..1.0 (0x0046e2c0) and +0x57c = other (reference moved: the previous one is released without
// clearing). Callers 0x0042cc70, 0x00431720. Not executed in recorded runs.
uint32_t fn_0042c700(uint32_t obj, uint32_t other, uint32_t id)
{
    if (static_cast<uint8_t>(orig::fn_00454780(obj)))
        return 0;
    if (id != D(obj + 0x574) || other == obj || (D(obj + 0x488) & 0x4000))
        return 0;
    if (!(Fl(obj + 0x578) <= K(0x0056c05c)))
        return 0;
    Fl(obj + 0x578) = orig::fn_0046e2c0(0, 0x3e800000u, 0x3f800000u);
    uint32_t old = D(obj + 0x57c);
    if (old)
        Unref(old);
    D(obj + 0x57c) = other;
    ++I(other + 0x28);
    return 0;
}

static uint32_t fn_0042c700_Abi(uint32_t eax, uint32_t ebx, uint32_t a0)
{
    return fn_0042c700(eax, ebx, a0);
}
ABI_ENTRY_0042c700(fn_0042c700_Entry, fn_0042c700_Abi)
HOOK(0x0042c700, "fn_0042c700", fn_0042c700_Entry, nullptr);

// 0x0042c790 (esi = AI object): guarding AI (+0x484 bit 25) for which virtual +0xe8 holds: when the target (+0x588)
// is farther (2D) than +0x5d8, sets +0x484 bit 22 and both ranges +0x5d8/+0x5dc to 99999.0 (0x47c34f80). Caller
// 0x0042c820. Not executed in recorded runs.
uint32_t fn_0042c790(uint32_t self)
{
    float own[3], tgt[3];
    if (!(D(self + 0x484) & 0x2000000))
        return 0;
    if (!VB(self, 0xe8))
        return 0;
    VC(self, 0x54, u(own));
    VC(D(self + 0x588), 0x54, u(tgt));
    float dy = tgt[1] - own[1];
    float dx = tgt[0] - own[0];
    float r = Fl(self + 0x5d8);
    float d = dx * dx + dy * dy;
    if (r * r < d)
    {
        D(self + 0x484) |= 0x400000;
        D(self + 0x5d8) = 0x47c34f80u;
        D(self + 0x5dc) = 0x47c34f80u;
        return 0x47c34f80u;
    }
    return 0;
}

static uint32_t fn_0042c790_Abi(uint32_t esi)
{
    return fn_0042c790(esi);
}
ABI_ENTRY_0042c790(fn_0042c790_Entry, fn_0042c790_Abi)
HOOK(0x0042c790, "fn_0042c790", fn_0042c790_Entry, nullptr);

namespace
{
// The release of the object +0x570 shared by both paths of 0x0042c820: unless +0x488 bit 15, clears +0x484 bit 19
// first (0x0044b7f0 (eax = object, cl = 0) when it was set), drops the reference and clears the slot.
void Release570(uint32_t self)
{
    uint32_t edi = self + 0x570;
    if (D(edi) == 0 || (D(self + 0x488) & 0x8000))
        return;
    if (D(self + 0x484) & 0x80000)
    {
        D(self + 0x484) &= 0xfff7ffffu;
        orig::fn_0044b7f0(D(edi), 0);
    }
    Unref(D(edi));
    D(edi) = 0;
}
}  // namespace

// 0x0042c820 (ecx = AI object; stack: candidate target, a1, a2 (bool); ret 0xc). GenericAIObject slot 109 (+0x1b4):
// offers a new target. Ignored while the current target was set less than K(0x005bcb68) ago (+0x514 against the game
// time 0x00604a98), or while holding cover (+0x904) unless barricade AI (+0x484 bit 17) or +0x488 bit 21. Without a
// target, or when the candidate is nearer (3D squared) than the current one: adopts it (virtual +0x23c (candidate,
// a1)), releases +0x570, then either 0x0042f5c0 (0) (when +0x52c is set and +0x484 bit 5 is clear) or, when a2 and
// (no target before) or the state (+0x460) is not one of 0x28, 0x29, 0x7b, 0xa0, 0xa1 (jump table 0x0042caf0) and
// 0x00426630 fails: 0x00454c90 (cl = 1, 1) and virtual +0x230 (1); stamps +0x514. Always ends with 0x0042c790 when
// not ignored. Callers 0x004342b0, 0x00485360. Not executed in recorded runs.
uint32_t fn_0042c820(uint32_t self, uint32_t cand, uint32_t a1, uint32_t a2)
{
    uint32_t now = D(0x00604a98);
    uint32_t target = D(self + 0x588);
    if (target && bf(now) - Fl(self + 0x514) < K(0x005bcb68))
        return 0;
    if (D(self + 0x904) && !(D(self + 0x484) & 0x20000) && !(D(self + 0x488) & 0x200000))
        return 0;
    if (!target)
    {
        VC(self, 0x23c, cand, a1);
        Release570(self);
        if (D(self + 0x52c) && !(By(self + 0x484) & 0x20))
            orig::fn_0042f5c0(self, 0);
        else if (static_cast<uint8_t>(a2))
        {
            orig::fn_00454c90(self, 1, 1);
            VC(self, 0x230, 1u);
        }
        uint32_t r = fn_0042c790(self);
        D(self + 0x514) = now;
        return r;
    }
    float np[3], own[3], tp[3];
    VC(cand, 0x54, u(np));
    VC(self, 0x54, u(own));
    VC(D(self + 0x588), 0x54, u(tp));
    float A = np[2] - own[2];
    float B = np[0] - own[0];
    float C = np[1] - own[1];
    float Dz = tp[2] - own[2];
    float E = tp[1] - own[1];
    float F = tp[0] - own[0];
    float dNew = C * C + B * B + A * A;
    float dTgt = F * F + E * E + Dz * Dz;
    if (!(dTgt < dNew))
    {
        VC(self, 0x23c, cand, a1);
        Release570(self);
        if (D(self + 0x52c) && !(By(self + 0x484) & 0x20))
            orig::fn_0042f5c0(self, 0);
        else
        {
            uint32_t st = D(self + 0x460) - 0x28;
            uint8_t cl;
            if (st <= 0x79 && (st == 0 || st == 1 || st == 0x53 || st == 0x78 || st == 0x79))
                cl = 0;
            else
                cl = static_cast<uint8_t>(a2);
            if (!static_cast<uint8_t>(orig::fn_00426630(self)) && cl)
            {
                orig::fn_00454c90(self, 1, 1);
                VC(self, 0x230, 1u);
            }
        }
        D(self + 0x514) = now;
    }
    return fn_0042c790(self);
}

static uint32_t fn_0042c820_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0042c820(ecx, a0, a1, a2);
}
ABI_ENTRY_0042c820(fn_0042c820_Entry, fn_0042c820_Abi)
HOOK(0x0042c820, "fn_0042c820", fn_0042c820_Entry, nullptr);

// 0x0042cc60 (eax = AI object, ecx, edx): +0x858 = ecx, +0x85c = edx (callers 0x00404e50 / 0x0040c590 pass a
// generator's tags).
void fn_0042cc60(uint32_t obj, uint32_t a, uint32_t b)
{
    D(obj + 0x858) = a;
    D(obj + 0x85c) = b;
}

static uint32_t fn_0042cc60_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    fn_0042cc60(eax, ecx, edx);
    return eax;
}
ABI_ENTRY_0042cc60(fn_0042cc60_Entry, fn_0042cc60_Abi)
HOOK(0x0042cc60, "fn_0042cc60", fn_0042cc60_Entry, nullptr);

// 0x0042cc70 (ecx = AI object; the listed edx input is not read; stack: new target, a1 (bool); ret 8).
// GenericAIObject slot 143 (+0x23c), set target: drops +0xab0 unless it is the new target, drops the old target
// +0x588 (none and no new one: return; none: virtual +0x144 unless 0x0044e600). With a new target: releases +0x88c
// (unless dead it plays "peasant_man_stand" 0x005bbea4, animation 0x81; clears +0x488 bit 28); a target that is not
// kept (virtual +0xd8 false) is replaced by the player (Game+0x748) when virtual +0xe8 holds, else nothing is set;
// +0x588 = target (reference), its position into +0x60c; with a1 and a group id +0x574, alerts every other object of
// World+0xe7c whose virtual +0x10c holds (0x0042c700).
uint32_t fn_0042cc70(uint32_t self, uint32_t target, uint32_t a1)
{
    uint32_t edi = self + 0xab0;
    uint32_t esi = D(edi);
    if (esi && esi != target)
    {
        Unref(esi);
        D(edi) = 0;
    }
    uint32_t ebx = self + 0x588;
    esi = D(ebx);
    if (esi)
    {
        Unref(esi);
        D(ebx) = 0;
    }
    else
    {
        if (!target)
            return 0;
        if (!static_cast<uint8_t>(orig::fn_0044e600()))
            VC(self, 0x144);
    }
    if (!target)
        return 0;
    edi = self + 0x88c;
    if (D(edi))
    {
        D(self + 0x488) &= 0xefffffffu;
        esi = D(edi);
        if (By(esi + 0x421) == 0 || !static_cast<uint8_t>(orig::fn_00454780(esi)))
            orig::fn_00408e30(0x81, D(edi), 0xffffffffu, 0x005bbea4u, 0, 0, 0, 0);
        Unref(D(edi));
        D(edi) = 0;
    }
    if (!VB(target, 0xd8))
    {
        if (!VB(self, 0xe8))
            return 0;
        target = G(0x748);
    }
    D(ebx) = target;
    ++I(target + 0x28);
    VC(D(ebx), 0x54, self + 0x60c);
    if (!static_cast<uint8_t>(a1) || D(self + 0x574) == 0)
        return 0;
    uint32_t it[2] = {0, 0};
    esi = orig::fn_004e7340(D(G(0xa84) + 0xe7c), u(it));
    while (esi)
    {
        if (esi != self && VB(esi, 0x10c))
            fn_0042c700(esi, target, D(self + 0x574));
        esi = orig::fn_004e73c0(u(it));
    }
    if (it[1])
        --I(it[1] + 0x18);
    return 0;
}

static uint32_t fn_0042cc70_Abi(uint32_t ecx, uint32_t /*edx*/, uint32_t a0, uint32_t a1)
{
    return fn_0042cc70(ecx, a0, a1);
}
ABI_ENTRY_0042cc70(fn_0042cc70_Entry, fn_0042cc70_Abi)
HOOK(0x0042cc70, "GenericAI_SetTarget", fn_0042cc70_Entry, nullptr);

// 0x0042ce70 (eax = new target or 0, ecx = AI object): with a current target only a given new one is set; without
// one, the new target or else the player (Game+0x748) is set (virtual +0x23c (t, 1)). Then 0x004246c0 (al = 1),
// refreshes +0x60c from the target, virtual +0x184, +0x578 = 0, a random timer +0x490 = rand() * K(0x005bce3c) +
// K(0x005bce20) and drops +0x57c. Not executed in recorded runs.
uint32_t fn_0042ce70(uint32_t t, uint32_t self)
{
    if (D(self + 0x588))
    {
        if (t)
            VC(self, 0x23c, t, 1u);
    }
    else
        VC(self, 0x23c, t ? t : G(0x748), 1u);
    orig::fn_004246c0(1, self);
    VC(D(self + 0x588), 0x54, self + 0x60c);
    VC(self, 0x184);
    int32_t r = Rand();
    D(self + 0x578) = 0;
    Fl(self + 0x490) = static_cast<float>(r) * K(0x005bce3c) + K(0x005bce20);
    uint32_t slot = self + 0x57c;
    if (D(slot))
    {
        Unref(D(slot));
        D(slot) = 0;
    }
    return 0;
}

static uint32_t fn_0042ce70_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0042ce70(eax, ecx);
}
ABI_ENTRY_0042ce70(fn_0042ce70_Entry, fn_0042ce70_Abi)
HOOK(0x0042ce70, "fn_0042ce70", fn_0042ce70_Entry, nullptr);

// 0x0042cf40 (eax = new target or 0, esi = AI object): sets the target like 0x0042ce70; with one: +0x484 bit 5,
// range +0x5dc = 5000.0, target position into +0x60c, 0x0046f6d0 towards it and state 0x3e. Caller 0x0048f690. Not
// executed in recorded runs.
uint32_t fn_0042cf40(uint32_t t, uint32_t self)
{
    if (D(self + 0x588))
    {
        if (t)
            VC(self, 0x23c, t, 1u);
    }
    else
        VC(self, 0x23c, t ? t : G(0x748), 1u);
    if (D(self + 0x588) == 0)
        return 0;
    D(self + 0x484) |= 0x20;
    D(self + 0x5dc) = 0x459c4000u;  // 5000.0f
    VC(D(self + 0x588), 0x54, self + 0x60c);
    orig::fn_0046f6d0(self, G(0xaa4), self + 0x60c, 0, 0, 0, 0, 0);
    return VC(self, 0x1b8, 0x3eu);
}

static uint32_t fn_0042cf40_Abi(uint32_t eax, uint32_t esi)
{
    return fn_0042cf40(eax, esi);
}
ABI_ENTRY_0042cf40(fn_0042cf40_Entry, fn_0042cf40_Abi)
HOOK(0x0042cf40, "fn_0042cf40", fn_0042cf40_Entry, nullptr);

// 0x0042cff0 (eax = AI object): clears +0x484 bit 25 (the guarding flag of 0x0042a7b0).
void fn_0042cff0(uint32_t obj)
{
    D(obj + 0x484) &= 0xfdffffffu;
}

static uint32_t fn_0042cff0_Abi(uint32_t eax)
{
    fn_0042cff0(eax);
    return eax;
}
ABI_ENTRY_0042cff0(fn_0042cff0_Entry, fn_0042cff0_Abi)
HOOK(0x0042cff0, "fn_0042cff0", fn_0042cff0_Entry, nullptr);

// 0x0042d000 (eax = object or 0, ecx = AI object; stack: a0 (+0x52c), a1 (bool), name, a3 (object name or 0),
// animation; ret 0x14): sets up a scripted mode (guess): +0xadc = eax (reference), +0xae0 = the object of
// World(Game+0xa84)+0xe70 named a3 (0x004592e0, reference; the old one is released), +0x488 bit 27 (and bit 6 with
// a1), +0x52c = a0, copies the name to +0x4cc, plays it (Anim_Set 0x00408d90 with a4 | 0x80) when a4 is non-zero and
// sets state 0x8c. Callers 0x004247f0, 0x00456180.
uint32_t fn_0042d000(uint32_t obj, uint32_t self, uint32_t a0, uint32_t a1, uint32_t name, uint32_t a3, uint32_t a4)
{
    D(self + 0xadc) = obj;
    if (obj)
        ++I(obj + 0x28);
    if (a3)
    {
        uint32_t edi = self + 0xae0;
        if (D(edi))
        {
            Unref(D(edi));
            D(edi) = 0;
        }
        uint32_t r = orig::fn_004592e0(D(G(0xa84) + 0xe70), a3, self);
        D(edi) = r;
        if (r)
            ++I(r + 0x28);
    }
    D(self + 0x488) |= 0x8000000;
    D(self + 0x52c) = a0;
    if (static_cast<uint8_t>(a1))
        D(self + 0x488) |= 0x40;
    uint32_t dst = self + 0x4cc;
    {
        uint32_t s = name;
        uint8_t c;
        do
        {
            c = By(s);
            By(dst + (s - name)) = c;
            ++s;
        } while (c);
    }
    if (a4)
        orig::fn_00408d90(0xffffffffu, self, dst, a4 | 0x80, 0, 0, 0, 0);
    return VC(self, 0x1b8, 0x8cu);
}

static uint32_t fn_0042d000_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                uint32_t a4)
{
    return fn_0042d000(eax, ecx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_0042d000(fn_0042d000_Entry, fn_0042d000_Abi)
HOOK(0x0042d000, "fn_0042d000", fn_0042d000_Entry, nullptr);

// 0x0042d0f0 (eax = AI object; the listed ecx input is only a `push ecx` slot, its upper bytes reach 0x00454c90 with
// cl = 1; stack: a0 (bool), a1 (bool); ret 8): becomes a guard: 0x00454c90 (cl = 1, 1), 0x00454cc0 (+0x398), its
// position into the guard point +0x4c0, +0x484 bit 25, random timer +0x494 = rand() * K(0x005bcd54) +
// K(0x005bcd50); with a1 an animation (a0: 0x0f via virtual +0x19c, else virtual +0x1a0 (2, 3, 0, 1, 1, 0, 0.5,
// 0.5)); state 0x79.
uint32_t fn_0042d0f0(uint32_t self, uint32_t ecx, uint32_t a0, uint32_t a1)
{
    orig::fn_00454c90(self, (ecx & 0xffffff00u) | 1, 1);
    orig::fn_00454cc0(self, self + 0x398);
    VC(self, 0x54, self + 0x4c0);
    D(self + 0x484) |= 0x2000000;
    Fl(self + 0x494) = static_cast<float>(Rand()) * K(0x005bcd54) + K(0x005bcd50);
    if (static_cast<uint8_t>(a1))
    {
        if (static_cast<uint8_t>(a0))
            VC(self, 0x19c, 0xfu, 1u, 0u, 0u, 0u, 0u);
        else
            VC(self, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    }
    return VC(self, 0x1b8, 0x79u);
}

static uint32_t fn_0042d0f0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0042d0f0(eax, ecx, a0, a1);
}
ABI_ENTRY_0042d0f0(fn_0042d0f0_Entry, fn_0042d0f0_Abi)
HOOK(0x0042d0f0, "fn_0042d0f0", fn_0042d0f0_Entry, nullptr);

// 0x0042d1b0 (eax = last marker tag, ecx = first marker tag; stack: AI object, a1 (bool), a2, a3, a4 (bool); ret
// 0x14): follow a marker spline (string "AI : Can't find marker spline group\nmarker tag %d, sequence %d\nkilling %s"
// 0x005b67a0). Drops the reference of the old spline +0x570 (the slot is overwritten, not cleared), stores the tag
// range +0x558/+0x55c and a2/a3 (+0x560/+0x564), picks a random tag in the range and looks the spline group up
// (0x0044c030, ebx = 0); missing: logs and removes the object (virtual +0xdc). Found (reference): with +0x484 bit 16
// it rides it (0x0044b980, timer +0x494, animation 0x0e or 8 + rand()*2 >> 15, state 0x97); otherwise it walks to the
// spline start (a1: 0x0046f5b0; a4: 0x0046f6b0; else 0x0046f6d0), animation 0x0e or 0x00427210, state 0x96. With
// +0x488 bit 16, the friction of self, of every object of the list +0xb78 and of +0xb7c is set to 0
// (Body_SetFriction 0x00458250).
// Frame: fr.U(o) is [esp+o] with the four registers pushed (sub esp, 0x14: o = 0x10..0x23); 0x10/0x14 iterator,
// 0x18 spline start position.
uint32_t fn_0042d1b0(uint32_t last, uint32_t first, uint32_t self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    Frame<0x24> fr;
    uint32_t old = D(self + 0x570);
    if (old)
    {
        if (--I(old + 0x28) == 0 && !VB(old, 0xd8))
            orig::fn_00487da0(old);
    }
    D(self + 0x558) = first;
    D(self + 0x55c) = last;
    D(self + 0x560) = a2;
    D(self + 0x564) = a3;
    int32_t tag = ((Rand() * static_cast<int32_t>(last - first + 1)) >> 15) + static_cast<int32_t>(first);
    uint32_t sp = orig::fn_0044c030(0, static_cast<uint32_t>(tag));
    D(self + 0x570) = sp;
    if (sp == 0)
    {
        DebugPrintf(G(0x34), 0x005b67a0u, static_cast<uint32_t>(tag), 0u, D(D(self + 0x414) + 0x10) + 4);
        return VC(self, 0xdc);
    }
    ++I(sp + 0x28);
    if (D(self + 0x484) & 0x10000)
    {
        orig::fn_0044b980((By(self + 0x488) & 2) ? 1u : 0u, self, D(self + 0x570), 0);
        Fl(self + 0x494) = static_cast<float>(Rand()) * K(0x005bce94) + K(0x005bcb7c);
        if (By(self + 0x488) & 1)
            VC(self, 0x19c, 0xeu, 1u, 0u, 0u, 0u, 0u);
        else
        {
            int32_t r = Rand();
            VC(self, 0x19c, static_cast<uint32_t>(((r << 1) >> 15) + 8), 1u, 0u, 0u, 0u, 0u);
        }
        VC(self, 0x1b8, 0x97u);
    }
    else
    {
        D(self + 0x450) = 0;
        uint32_t r54 = VC(D(self + 0x570), 0x54, fr.A(0x18));
        if (static_cast<uint8_t>(a1))
            orig::fn_0046f5b0(r54 & 0xffffff00u, 0, fr.A(0x18), self, G(0xaa4), 0, 0);
        else if (static_cast<uint8_t>(a4))
            orig::fn_0046f6b0(r54 & 0xffffff00u, GameP() & 0xffffff00u, G(0xaa4) & 0xffffff00u, fr.A(0x18), G(0xaa4),
                              self, 0);
        else
            orig::fn_0046f6d0(self, G(0xaa4), fr.A(0x18), 0, 0, 0, 0, 0);
        if (By(self + 0x488) & 1)
            VC(self, 0x19c, 0xeu, 1u, 0u, 0u, 0u, 0u);
        else
            orig::fn_00427210(self);
        VC(self, 0x1b8, 0x96u);
    }
    if (D(self + 0x488) & 0x10000)
    {
        fr.U(0x10) = 0;
        fr.U(0x14) = 0;
        orig::fn_00458250(self, 0);
        uint32_t o = orig::fn_004e7340(D(self + 0xb78), fr.A(0x10));
        while (o)
        {
            orig::fn_00458250(o, 0);
            o = orig::fn_004e73c0(fr.A(0x10));
        }
        if (D(self + 0xb7c))
            orig::fn_00458250(D(self + 0xb7c), 0);
        if (fr.U(0x14))
            --I(fr.U(0x14) + 0x18);
    }
    return 0;
}

static uint32_t fn_0042d1b0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                uint32_t a4)
{
    return fn_0042d1b0(eax, ecx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_0042d1b0(fn_0042d1b0_Entry, fn_0042d1b0_Abi)
HOOK(0x0042d1b0, "AI_FollowMarkerSpline", fn_0042d1b0_Entry, nullptr);

// 0x0042d430 (stack: AI object, min distance (float), max distance (float); ret 0xc): wanders off: turns its facing
// axis (0x00453150) by a random angle rand() * K(0x005bce90) - K(0x005bce8c) about z (rotation matrix through
// 0x004e36a0), normalises it and walks (0x0046f6d0) to its position plus a random distance in [min, max) along it
// per axis (three rand() rolls, scale 1/32768); drops the spline +0x570, clears +0x488 bit 15, 0x00427210 and state
// 0x3d. Callers 0x0043ee90, 0x0044ba30.
// Frame: fr.U(o) is [esp+o] with the three registers (ebp esi edi) pushed (sub esp, 0x68: o = 0x0c..0x73); 0x0c
// direction, 0x18 destination, 0x24 facing axis, 0x30 rotation matrix (0x44 bytes). The argument slots +0x78 (the
// object) and later serve as temporaries (angle, max - min), as in the original.
uint32_t fn_0042d430(uint32_t self, uint32_t minD, uint32_t maxD)
{
    Frame<0x74> fr;
    orig::fn_00453150(self, fr.A(0x24));
    float ang = static_cast<float>(Rand()) * K(0x005bce90) - K(0x005bce8c);
    std::memset(fr.b + 0x30, 0, 0x44);
    float s, c;
    __asm {
        fld ang
        fsin
        fstp s
        fld ang
        fcos
        fstp c
    }
    fr.U(0x58) = 0x3f800000u;
    fr.U(0x6c) = 0x3f800000u;
    fr.U(0x70) = 1;
    fr.F(0x30) = c;
    fr.F(0x34) = -s;
    fr.F(0x40) = s;
    fr.F(0x44) = c;
    orig::fn_004e36a0(fr.A(0x24), fr.A(0x30), 1, fr.A(0xc));
    float len = std::sqrt(fr.F(0x14) * fr.F(0x14) + fr.F(0x10) * fr.F(0x10) + fr.F(0xc) * fr.F(0xc));
    if (!(len == K(0x0056c05c)))
    {
        float inv = K(0x005bcb2c) / len;
        fr.F(0xc) = fr.F(0xc) * inv;
        fr.F(0x10) = fr.F(0x10) * inv;
        fr.F(0x14) = inv * fr.F(0x14);
    }
    VC(self, 0x54, fr.A(0x18));
    float lo = bf(minD);
    float range = bf(maxD) - lo;
    fr.F(0x18) = (static_cast<float>(Rand()) * range * K(0x005bcbc0) + lo) * fr.F(0xc) + fr.F(0x18);
    fr.F(0x1c) = (static_cast<float>(Rand()) * range * K(0x005bcbc0) + lo) * fr.F(0x10) + fr.F(0x1c);
    fr.F(0x20) = (static_cast<float>(Rand()) * range * K(0x005bcbc0) + lo) * fr.F(0x14) + fr.F(0x20);
    orig::fn_0046f6d0(self, G(0xaa4), fr.A(0x18), 0, 0, 0, 0, 0);
    uint32_t slot = self + 0x570;
    if (D(slot))
    {
        Unref(D(slot));
        D(slot) = 0;
    }
    D(self + 0x488) &= 0xffff7fffu;
    orig::fn_00427210(self);
    return VC(self, 0x1b8, 0x3du);
}

static uint32_t fn_0042d430_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0042d430(a0, a1, a2);
}
ABI_ENTRY_0042d430(fn_0042d430_Entry, fn_0042d430_Abi)
HOOK(0x0042d430, "fn_0042d430", fn_0042d430_Entry, nullptr);

// 0x0042d650 (eax = a, ecx = parent, esi = object; stack: a0; no ret of its own: it ends with a tail jump to the
// object's virtual +0x1b8 after overwriting its argument with 0x66, and that thiscall method pops the argument, so
// the function pops 4 bytes; caller 0x004298a0 does not add esp). Attaches the object (Object_Attach 0x004539f0:
// eax = a0, ecx = parent, edx = object; a, 1, 0), virtual +0xf8 (0x4000, 0) and state 0x66. Hand-written entry stub
// (no generated one: "no ret found"). Not executed in recorded runs.
uint32_t fn_0042d650(uint32_t a, uint32_t parent, uint32_t obj, uint32_t a0)
{
    orig::fn_004539f0(a0, parent, obj, a, 1, 0);
    VC(obj, 0xf8, 0x4000u, 0u);
    return VC(obj, 0x1b8, 0x66u);
}

static uint32_t fn_0042d650_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return fn_0042d650(eax, ecx, esi, a0);
}
static __declspec(naked) void fn_0042d650_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 28]
        push esi
        push ecx
        push eax
        call fn_0042d650_Abi
        add esp, 16
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret 4
    }
}
HOOK(0x0042d650, "fn_0042d650", fn_0042d650_Entry, nullptr);

// 0x0042d690 (ebx = slot, esi = cover object, edi = AI object): takes the cover (+0x904 with a reference, +0x914 =
// slot, virtual +0x158 (self, slot)), also keeps it in +0x880 (second reference), sets +0x488 bit 7, becomes a guard
// (0x0042d0f0 (0, 1), ecx = the new +0x488 as the original leaves it), places itself at the slot (cover virtual +0x150
// fills a 0x44-byte matrix; Object_SetTransform 0x00457990) and enables physics. Caller 0x0040c740. Not executed in
// recorded runs.
uint32_t fn_0042d690(uint32_t slot, uint32_t cover, uint32_t self)
{
    uint32_t v0[3], v1[3];
    uint32_t mat[17];
    D(self + 0x904) = cover;
    ++I(cover + 0x28);
    D(self + 0x914) = slot;
    VC(cover, 0x158, self, slot);
    D(self + 0x880) = cover;
    ++I(cover + 0x28);
    uint32_t f = D(self + 0x488) | 0x80;
    D(self + 0x488) = f;
    fn_0042d0f0(self, f, 0, 1);
    uint32_t r = VC(cover, 0x150, u(v1), u(v0), slot, self, 0u, u(mat));
    orig::fn_00457990(r & 0xffffff00u, self, u(mat));
    return static_cast<uint32_t>(orig::fn_004577b0(self, 1));
}

static uint32_t fn_0042d690_Abi(uint32_t ebx, uint32_t esi, uint32_t edi)
{
    return fn_0042d690(ebx, esi, edi);
}
ABI_ENTRY_0042d690(fn_0042d690_Entry, fn_0042d690_Abi)
HOOK(0x0042d690, "fn_0042d690", fn_0042d690_Entry, nullptr);

// 0x0042d710 (eax = marker tag, ecx = AI object): ladder climb: follows the marker spline of that single tag
// (0x0042d1b0 with a4 = 1), 0x00429a40 (al = 0), animation 0x31, grounds itself (Object_ForceGround 0x00455860:
// 1000, 10000), and plays "ladder_run" (0x005b6794, animation 0x81) on the "grunt_ladder" object (0x005b68d0) that
// 0x004298a0 attaches. Caller 0x00446460. Not executed in recorded runs.
uint32_t fn_0042d710(uint32_t tag, uint32_t self)
{
    uint32_t r = fn_0042d1b0(tag, tag, self, 0, 0, 0, 1);
    orig::fn_00429a40(r & 0xffffff00u, self);
    VC(self, 0x19c, 0x31u, 1u, 0u, 0u, 0u, 0u);
    orig::fn_00455860(self, 0x447a0000u, 0x461c4000u);
    uint32_t obj = static_cast<uint32_t>(orig::fn_004298a0(self, 0x005b68d0u, 0, 0));
    return orig::fn_00408e30(0x81, obj, 0xffffffffu, 0x005b6794u, 0, 0, 0, 0);
}

static uint32_t fn_0042d710_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0042d710(eax, ecx);
}
ABI_ENTRY_0042d710(fn_0042d710_Entry, fn_0042d710_Abi)
HOOK(0x0042d710, "AI_ClimbLadder", fn_0042d710_Entry, nullptr);

namespace
{
// The common tail of 0x0042d780 / 0x0042d8f0: unlinks the grunt from the World lists +0xe94, +0xe90, +0xe9c, +0xea4,
// +0xe7c and +0xe8c (ObjectList_Unlink 0x00454750), resets +0x61c (0x004a0e00, al = 0) and sets state 0x67.
uint32_t TurretGruntTail(uint32_t self)
{
    static const uint32_t lists[] = {0xe94, 0xe90, 0xe9c, 0xea4, 0xe7c, 0xe8c};
    for (uint32_t off : lists)
        orig::fn_00454750(D(G(0xa84) + off), self);
    orig::fn_004a0e00(0, self + 0x61c);
    return VC(self, 0x1b8, 0x67u);
}
}  // namespace

// 0x0042d780 (eax = grunt, ecx = turret): mounts the grunt on the turret (Object_Attach 0x004539f0: eax = 0,
// edx = grunt; 0, 1, 0), plays "grunt_turret_attack" / "_1".."_4" by the global counter 0x0064a75c (0..4, anything
// else "_5"; jump table 0x0042d8d0; strings 0x005b6780..0x005b6708; animation 0x80), increments the counter, virtual
// +0xf8 (0x4000, 0) and the common tail (lists, state 0x67). Caller 0x0049ad60. Not executed in recorded runs.
uint32_t fn_0042d780(uint32_t self, uint32_t turret)
{
    static const uint32_t names[5] = {0x005b6780u, 0x005b6768u, 0x005b6750u, 0x005b6738u, 0x005b6720u};
    orig::fn_004539f0(0, turret, self, 0, 1, 0);
    uint32_t n = D(0x0064a75c);
    orig::fn_00408e30(0x80, self, 0xffffffffu, n <= 4 ? names[n] : 0x005b6708u, 0, 0, 0, 0);
    D(0x0064a75c) = D(0x0064a75c) + 1;
    VC(self, 0xf8, 0x4000u, 0u);
    return TurretGruntTail(self);
}

static uint32_t fn_0042d780_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0042d780(eax, ecx);
}
ABI_ENTRY_0042d780(fn_0042d780_Entry, fn_0042d780_Abi)
HOOK(0x0042d780, "fn_0042d780", fn_0042d780_Entry, nullptr);

// 0x0042d8f0 (eax = grunt, ecx = turret): like 0x0042d780 with "grunt_turret_jump_on" (0x005b66f0) or
// "grunt_turret_jump_on_1" (0x005b66d8) alternating on the global byte 0x0064a758. Caller 0x0049b030. Not executed in
// recorded runs.
uint32_t fn_0042d8f0(uint32_t self, uint32_t turret)
{
    orig::fn_004539f0(0, turret, self, 0, 1, 0);
    orig::fn_00408e30(0x80, self, 0xffffffffu, By(0x0064a758) ? 0x005b66f0u : 0x005b66d8u, 0, 0, 0, 0);
    By(0x0064a758) = By(0x0064a758) == 0 ? 1 : 0;
    VC(self, 0xf8, 0x4000u, 0u);
    return TurretGruntTail(self);
}

static uint32_t fn_0042d8f0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0042d8f0(eax, ecx);
}
ABI_ENTRY_0042d8f0(fn_0042d8f0_Entry, fn_0042d8f0_Abi)
HOOK(0x0042d8f0, "fn_0042d8f0", fn_0042d8f0_Entry, nullptr);

// 0x0042da00 (eax = n, ecx = tag, esi = AI object): +0x564 = n, +0x558 = +0x55c = tag, +0x568 = +0x56c = n + 1,
// +0x560 = 0, targets the player (virtual +0x23c (Game+0x748, 0)), +0x4ac = 0x11a, +0x890 = 5000.0, animation 0x61,
// +0x4b0..+0x4bc = 0x63 and state 0x82. Caller 0x0043d180. Not executed in recorded runs.
uint32_t fn_0042da00(uint32_t n, uint32_t tag, uint32_t self)
{
    D(self + 0x564) = n;
    D(self + 0x558) = tag;
    D(self + 0x55c) = tag;
    D(self + 0x568) = n + 1;
    D(self + 0x56c) = n + 1;
    D(self + 0x560) = 0;
    VC(self, 0x23c, G(0x748), 0u);
    D(self + 0x4ac) = 0x11a;
    D(self + 0x890) = 0x459c4000u;
    VC(self, 0x19c, 0x61u, 1u, 0u, 0u, 0u, 0u);
    D(self + 0x4b0) = 0x63;
    D(self + 0x4b4) = 0x63;
    D(self + 0x4b8) = 0x63;
    D(self + 0x4bc) = 0x63;
    return VC(self, 0x1b8, 0x82u);
}

static uint32_t fn_0042da00_Abi(uint32_t eax, uint32_t ecx, uint32_t esi)
{
    return fn_0042da00(eax, ecx, esi);
}
ABI_ENTRY_0042da00(fn_0042da00_Entry, fn_0042da00_Abi)
HOOK(0x0042da00, "fn_0042da00", fn_0042da00_Entry, nullptr);

// 0x0042daa0 (esi = AI object): +0x4ac = 0x119, +0x890 = 5000.0, animation 0x61, +0x4b0..+0x4bc = 0x63 and state
// 0x82 (the second half of 0x0042da00). Caller 0x0043d180. Not executed in recorded runs.
uint32_t fn_0042daa0(uint32_t self)
{
    D(self + 0x4ac) = 0x119;
    D(self + 0x890) = 0x459c4000u;
    VC(self, 0x19c, 0x61u, 1u, 0u, 0u, 0u, 0u);
    D(self + 0x4b0) = 0x63;
    D(self + 0x4b4) = 0x63;
    D(self + 0x4b8) = 0x63;
    D(self + 0x4bc) = 0x63;
    return VC(self, 0x1b8, 0x82u);
}

static uint32_t fn_0042daa0_Abi(uint32_t esi)
{
    return fn_0042daa0(esi);
}
ABI_ENTRY_0042daa0(fn_0042daa0_Entry, fn_0042daa0_Abi)
HOOK(0x0042daa0, "fn_0042daa0", fn_0042daa0_Entry, nullptr);

// 0x0042db00 (ecx = AI object; stack: vec3 pointer; ret 4). GenericAIObject slot 141 (+0x234): a random timer
// +0x4a8 = rand() * K(0x005bccd0) + K(0x005bcbec), +0x908 = *vec, sets +0x48c bit 13. Virtual only (no direct
// callers); not executed in recorded runs.
uint32_t fn_0042db00(uint32_t self, uint32_t v)
{
    Fl(self + 0x4a8) = static_cast<float>(Rand()) * K(0x005bccd0) + K(0x005bcbec);
    D(self + 0x908) = D(v);
    D(self + 0x90c) = D(v + 4);
    D(self + 0x910) = D(v + 8);
    D(self + 0x48c) |= 0x2000;
    return self + 0x908;
}

static uint32_t fn_0042db00_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0042db00(ecx, a0);
}
ABI_ENTRY_0042db00(fn_0042db00_Entry, fn_0042db00_Abi)
HOOK(0x0042db00, "fn_0042db00", fn_0042db00_Entry, nullptr);

// 0x0042db50 (eax = first tag, ecx = last tag, edx = cover object, ebx = slot; stack: AI object, group id, a2, a3,
// a4; ret 0x14): takes the cover slot (+0x904 with a reference, +0x914, virtual +0x158), stores +0x558/+0x55c/+0x560/
// +0x564/+0x574/+0x530, walks to the slot (cover virtual +0x14c, 0x0046f5b0), virtual +0xf8 (0x120000, 0), grounds
// itself (Object_ForceGround 1000, 10000), 0x00427210 and state 0x5a. Caller 0x0040c910.
uint32_t fn_0042db50(uint32_t first, uint32_t last, uint32_t cover, uint32_t slot, uint32_t self, uint32_t a1,
                     uint32_t a2, uint32_t a3, uint32_t a4)
{
    Frame<0x20> fr;  // [esp+o] with esi, edi pushed: 0x08 slot point, 0x14 slot vector
    D(self + 0x904) = cover;
    ++I(cover + 0x28);
    D(self + 0x55c) = last;
    D(self + 0x558) = first;
    D(self + 0x560) = a2;
    D(self + 0x530) = a4;
    D(self + 0x914) = slot;
    D(self + 0x564) = a3;
    D(self + 0x574) = a1;
    VC(cover, 0x158, self, slot);
    uint32_t r = VC(cover, 0x14c, fr.A(0x8), fr.A(0x14), slot, self, 0u, 0u);
    orig::fn_0046f5b0(r & 0xffffff00u, 0, fr.A(0x8), self, G(0xaa4), 0, 0);
    VC(self, 0xf8, 0x120000u, 0u);
    orig::fn_00455860(self, 0x447a0000u, 0x461c4000u);
    orig::fn_00427210(self);
    return VC(self, 0x1b8, 0x5au);
}

static uint32_t fn_0042db50_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t ebx, uint32_t a0, uint32_t a1,
                                uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_0042db50(eax, ecx, edx, ebx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_0042db50(fn_0042db50_Entry, fn_0042db50_Abi)
HOOK(0x0042db50, "fn_0042db50", fn_0042db50_Entry, nullptr);

// 0x0042dc20 (eax = cover object, ecx = AI object, ebx = slot): takes the cover slot and walks to it like 0x0042db50,
// then becomes a "smoking" (wounded, guess) grunt: +0x454 = health (Object_GetHealth 0x004555b0) * K(0x005bcbe4),
// +0x488 bits 8 and 31, +0x484 bit 1 when virtual +0x21c holds, 0x00453540 (cl = 1), the effect "fx_smoking_grunt"
// (0x005b66c4, Fx_Create 0x0041cc60 on Game+0xa90) attached to its "chest" (0x0059e210), +0xb30 = 0, virtual +0xf8
// (0x120000, 0), grounds itself, 0x00427210 and state 0x5a. Caller 0x0040c850. Not executed in recorded runs.
// Frame: fr.U(o) is [esp+o] with esi, edi pushed (sub esp, 0x98: o = 0x08..0x9f); 0x08 slot point, 0x14 slot
// vector, 0x20 effect descriptor (+0 = 4, +0x18 = bone name, +0x1c = owner; the rest is left as found: zero here).
uint32_t fn_0042dc20(uint32_t cover, uint32_t self, uint32_t slot)
{
    Frame<0xa0> fr;
    D(self + 0x904) = cover;
    ++I(cover + 0x28);
    D(self + 0x914) = slot;
    VC(cover, 0x158, self, slot);
    uint32_t r = VC(cover, 0x14c, fr.A(0x8), fr.A(0x14), slot, self, 0u, 0u);
    orig::fn_0046f5b0(r & 0xffffff00u, 0, fr.A(0x8), self, G(0xaa4), 0, 0);
    Fl(self + 0x454) = orig::fn_004555b0(self) * K(0x005bcbe4);
    uint32_t f = D(self + 0x488) | 0x100;
    D(self + 0x488) = f;
    D(self + 0x488) = f | 0x80000000u;
    if (VB(self, 0x21c))
        D(self + 0x484) |= 2;
    orig::fn_00453540(self, 1);
    fr.U(0x3c) = self;
    fr.U(0x38) = 0x0059e210u;
    fr.U(0x20) = 4;
    orig::fn_0041cc60(fr.A(0x20) & 0xffffff00u, G(0xa90), 0x005b66c4u, fr.A(0x20));
    D(self + 0xb30) = 0;
    VC(self, 0xf8, 0x120000u, 0u);
    orig::fn_00455860(self, 0x447a0000u, 0x461c4000u);
    orig::fn_00427210(self);
    return VC(self, 0x1b8, 0x5au);
}

static uint32_t fn_0042dc20_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx)
{
    return fn_0042dc20(eax, ecx, ebx);
}
ABI_ENTRY_0042dc20(fn_0042dc20_Entry, fn_0042dc20_Abi)
HOOK(0x0042dc20, "fn_0042dc20", fn_0042dc20_Entry, nullptr);

// 0x0042dd50 (eax = first tag, ecx = last tag, edx = group id; stack: AI object, generator; ret 8): a troop leaves
// its generator: stores the tags (+0x558/+0x55c, +0x560 = +0x564 = 0, +0x574 = group), walks to the generator's
// "ref_exit_00" point (0x005b66b8; fn_00453c50 matrix, translation m[3], m[7], m[11]; 0x0046f5b0), +0x488 bit 9,
// virtual +0xf8 (0x120000, 0), grounds itself, 0x00427210 and state 0x5a. Caller 0x00404e50. Not executed in
// recorded runs.
// Frame: fr.U(o) is [esp+o] with esi, edi pushed (sub esp, 0x50: o = 0x08..0x57); 0x08 exit point, 0x14 matrix.
uint32_t fn_0042dd50(uint32_t first, uint32_t last, uint32_t group, uint32_t self, uint32_t gen)
{
    Frame<0x58> fr;
    D(self + 0x558) = first;
    D(self + 0x55c) = last;
    D(self + 0x560) = 0;
    D(self + 0x564) = 0;
    D(self + 0x574) = group;
    orig::fn_00453c50(gen, 0x005b66b8u, fr.A(0x14), 1, 0, 0, 1, 0, 0);
    fr.U(0x8) = fr.U(0x20);
    fr.U(0xc) = fr.U(0x30);
    fr.U(0x10) = fr.U(0x40);
    orig::fn_0046f5b0(fr.U(0x30) & 0xffffff00u, 0, fr.A(0x8), self, G(0xaa4), 0, 0);
    D(self + 0x488) |= 0x200;
    VC(self, 0xf8, 0x120000u, 0u);
    orig::fn_00455860(self, 0x447a0000u, 0x461c4000u);
    orig::fn_00427210(self);
    return VC(self, 0x1b8, 0x5au);
}

static uint32_t fn_0042dd50_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1)
{
    return fn_0042dd50(eax, ecx, edx, a0, a1);
}
ABI_ENTRY_0042dd50(fn_0042dd50_Entry, fn_0042dd50_Abi)
HOOK(0x0042dd50, "fn_0042dd50", fn_0042dd50_Entry, nullptr);

// 0x0042de30 (esi = AI object): friction 0 (Body_SetFriction 0x00458250), animation 0x0e (virtual +0x19c: 0x0e, 1,
// 0, 0, 1, 0) and state 0xdc. Caller 0x00433a10. Not executed in recorded runs.
uint32_t fn_0042de30(uint32_t self)
{
    orig::fn_00458250(self, 0);
    VC(self, 0x19c, 0xeu, 1u, 0u, 0u, 1u, 0u);
    return VC(self, 0x1b8, 0xdcu);
}

static uint32_t fn_0042de30_Abi(uint32_t esi)
{
    return fn_0042de30(esi);
}
ABI_ENTRY_0042de30(fn_0042de30_Entry, fn_0042de30_Abi)
HOOK(0x0042de30, "fn_0042de30", fn_0042de30_Entry, nullptr);

// 0x0042de60 (eax = squad tag, ecx = AI object, edx = attachment name, esi = parent object; stack: a0 (bool), a1
// (+0x530), a2 (+0x528), a3 (+0x390); ret 0x10): joins a squad. A zero tag logs "Squad MUST have a 'squad_tag'
// assigned, forcing one now" (0x005b6680) and uses 0xfffe7e53. Stores the tag (+0x574) and arguments, keeps the parent
// (+0x9bc, reference) and the name (+0x9c0), takes the parent's named matrix (fn_00453c50) as its transform
// (Object_SetTransform), registers with the squad (0x004324d0 (0, 1)), then a0: friction 0, animation 0x0e, state
// 0xdc; else animation 0x0f (0x0f, 0, 0, 0, 0, 1), state 0xde. Caller 0x00433220. Not executed in recorded runs.
uint32_t fn_0042de60(uint32_t tag, uint32_t self, uint32_t name, uint32_t parent, uint32_t a0, uint32_t a1,
                     uint32_t a2, uint32_t a3)
{
    Frame<0x4c> fr;  // [esp+o] with ebx, edi pushed; 0x08 matrix (0x44 bytes)
    if (tag == 0)
    {
        DebugPrintf(G(0x34), 0x005b6680u);
        tag = 0xfffe7e53u;
    }
    D(self + 0x574) = tag;
    D(self + 0x530) = a1;
    D(self + 0x390) = a3;
    D(self + 0x528) = a2;
    D(self + 0x9bc) = parent;
    ++I(parent + 0x28);
    {
        uint32_t dst = self + 0x9c0;
        uint32_t s = name;
        uint8_t c;
        do
        {
            c = By(s);
            By(dst + (s - name)) = c;
            ++s;
        } while (c);
    }
    uint32_t r = orig::fn_00453c50(parent, name, fr.A(0x8), 1, 0, 0, 1, 0, 0);
    orig::fn_00457990(r & 0xffffff00u, self, fr.A(0x8));
    orig::fn_004324d0(D(self + 0x574), self, 0, 1);
    if (static_cast<uint8_t>(a0))
    {
        orig::fn_00458250(self, 0);
        VC(self, 0x19c, 0xeu, 1u, 0u, 0u, 1u, 0u);
        return VC(self, 0x1b8, 0xdcu);
    }
    VC(self, 0x19c, 0xfu, 0u, 0u, 0u, 0u, 1u);
    return VC(self, 0x1b8, 0xdeu);
}

static uint32_t fn_0042de60_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0, uint32_t a1,
                                uint32_t a2, uint32_t a3)
{
    return fn_0042de60(eax, ecx, edx, esi, a0, a1, a2, a3);
}
ABI_ENTRY_0042de60(fn_0042de60_Entry, fn_0042de60_Abi)
HOOK(0x0042de60, "fn_0042de60", fn_0042de60_Entry, nullptr);

// 0x0042df70 (eax = AI object; stack: a0 (float, e.g. 45.0); ret 4): face-the-target turn. Virtual +0x190 (a0) false
// with a target (+0x588) sets +0x484 bit 14; no target clears it. With the bit: the 2D direction to the target goes
// to +0x51c (normalised by 0x004e7780), +0x524 = 0, and 0x00454e60 (ebx = +0x51c, 16.0) turns towards it; done:
// clears the bit. Without: 0x00454cc0 (+0x398). Callers 0x00428980, 0x0042a7b0, 0x0042fe50, 0x00435940, 0x0043ee90.
uint32_t fn_0042df70(uint32_t self, uint32_t a0)
{
    float own[3], tgt[3];
    bool r = VB(self, 0x190, a0);
    if (!r && D(self + 0x588))
        D(self + 0x484) |= 0x4000;
    if (D(self + 0x588) == 0)
        D(self + 0x484) &= 0xffffbfffu;
    if (!(D(self + 0x484) & 0x4000))
        return static_cast<uint32_t>(orig::fn_00454cc0(self, self + 0x398));
    VC(self, 0x54, u(own));
    VC(D(self + 0x588), 0x54, u(tgt));
    uint32_t dir = self + 0x51c;
    Fl(dir) = tgt[0] - own[0];
    Fl(dir + 4) = tgt[1] - own[1];
    orig::fn_004e7780(dir, dir);
    D(self + 0x524) = 0;
    uint32_t done = orig::fn_00454e60(dir, self, 0x41800000u);
    if (static_cast<uint8_t>(done))
        D(self + 0x484) &= 0xffffbfffu;
    return done;
}

static uint32_t fn_0042df70_Abi(uint32_t eax, uint32_t a0)
{
    return fn_0042df70(eax, a0);
}
ABI_ENTRY_0042df70(fn_0042df70_Entry, fn_0042df70_Abi)
HOOK(0x0042df70, "GenericAI_TurnToTarget", fn_0042df70_Entry, nullptr);

// 0x0042e050 (edi = AI object): turn animation while +0x484 bit 14 (turning, see 0x0042df70) is set, virtual +0x218
// holds and the body is not keyframed (0x00457790). By the turn state +0x498 (jump table 0x0042e190, index bytes
// 0x0042e19c): states 5-7 wait for the current animation's bit 1 (0x00408800 (4) record +0x1c), then state 0x0f and
// the idle animation (virtual +0x1a0: 2, 3, 0, 1, 1, 0, 0.5, 0.5); states 2, 0x0f, 0x12..0x20 compare the facing axis
// (0x00453150) with the wanted direction +0x51c: dot < K(0x005bcd4c): animation 5; dot < K(0x005bcd48): animation 6
// or 7 by the sign of the cross product (virtual +0x19c). Caller 0x0043ee90.
uint32_t fn_0042e050(uint32_t self)
{
    float ax[3];
    if (!(D(self + 0x484) & 0x4000))
        return 0;
    if (!VB(self, 0x218))
        return 0;
    if (static_cast<uint8_t>(orig::fn_00457790(self)))
        return 0;
    uint32_t st = D(self + 0x498) - 2;
    if (st > 0x1e)
        return 0;
    uint32_t c = st + 2;
    if (c == 5 || c == 6 || c == 7)
    {
        uint32_t rec = orig::fn_00408800(4, self);
        if (!((D(rec + 0x1c) >> 1) & 1))
            return 0;
        D(self + 0x498) = 0xf;
        return VC(self, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    }
    if (!(c == 2 || c == 0xf || (c >= 0x12 && c <= 0x20)))
        return 0;
    orig::fn_00453150(self, u(ax));
    float d = ax[1] * Fl(self + 0x520) + ax[0] * Fl(self + 0x51c);
    if (d < K(0x005bcd4c))
        return VC(self, 0x19c, 5u, 0u, 0u, 0u, 0u, 0u);
    if (!(d < K(0x005bcd48)))
        return 0;
    float cr = ax[0] * Fl(self + 0x520) - ax[1] * Fl(self + 0x51c);
    if (!(cr <= K(0x0056c05c)) && !(cr != cr))
        return VC(self, 0x19c, 6u, 0u, 0u, 0u, 0u, 0u);
    return VC(self, 0x19c, 7u, 0u, 0u, 0u, 0u, 0u);
}

static uint32_t fn_0042e050_Abi(uint32_t edi)
{
    return fn_0042e050(edi);
}
ABI_ENTRY_0042e050(fn_0042e050_Entry, fn_0042e050_Abi)
HOOK(0x0042e050, "fn_0042e050", fn_0042e050_Entry, nullptr);

// 0x0042e910 (eax = AI object): stuck / falling watchdog (guess). Skipped in state 0x82, with +0x488 bits 7/21, when
// attached (Object_GetParent: then +0x890 = 0), when 0x00426630 holds, virtual +0x1bc fails or it is dead. When the
// body's vertical velocity (Body_getLinearVelocity 0x00458000) is not below K(0x005bcd44) the timer +0x890 resets to
// 0. Otherwise: an unarmed timer is armed to 0.2 unless 0x00454bc0 holds; an armed one resets when 0x00454bc0 holds,
// is left alone in the states 0xb5, 0xb8-0xba, 0xbd-0xbf, 0xc4, 0xc5, 0xe7, 0xe8, 0xea, 0xf0, 0x192, 0x193, 0x195,
// 0x197, 0x19b-0x19d, and else counts down by the frame time; at zero the current state and +0x498..+0x4a4 are
// saved (+0x4ac, +0x4b0..+0x4bc), +0x890 = 5.0, animation 0x61 and state 0x82. Caller 0x0043ee90.
uint32_t fn_0042e910(uint32_t self)
{
    float vel[3];
    if (D(self + 0x460) == 0x82)
        return 0;
    if (D(self + 0x488) & 0x200080)
        return 0;
    if (orig::fn_004539d0(self) != 0)
    {
        D(self + 0x890) = 0;
        return 0;
    }
    if (static_cast<uint8_t>(orig::fn_00426630(self)))
        return 0;
    if (!VB(self, 0x1bc))
        return 0;
    if (static_cast<uint8_t>(orig::fn_00454780(self)))
        return 0;
    orig::fn_00458000(self, u(vel));
    if (!(vel[2] < K(0x005bcd44)))
    {
        D(self + 0x890) = 0;
        return 0;
    }
    if (Fl(self + 0x890) <= K(0x0056c05c))
    {
        if (static_cast<uint8_t>(orig::fn_00454bc0(self)))
            return 0;
        D(self + 0x890) = 0x3e4ccccdu;  // 0.2f
        return 0;
    }
    if (static_cast<uint8_t>(orig::fn_00454bc0(self)))
    {
        D(self + 0x890) = 0;
        return 0;
    }
    uint32_t st = D(self + 0x460);
    static const uint32_t skip[] = {0xb8, 0xb9, 0xc5, 0xbd, 0xba, 0xbe, 0xc4, 0xb5, 0xbf, 0x192, 0x193,
                                    0x197, 0xe7, 0xea, 0xe8, 0x195, 0xf0, 0x19b, 0x19d, 0x19c};
    for (uint32_t s : skip)
        if (st == s)
            return 0;
    float t = Fl(self + 0x890) - FrameTime();
    Fl(self + 0x890) = t;
    if (!(t <= K(0x0056c05c)))
        return 0;
    D(self + 0x4b0) = D(self + 0x498);
    D(self + 0x4b4) = D(self + 0x49c);
    D(self + 0x4b8) = D(self + 0x4a0);
    D(self + 0x4ac) = st;
    D(self + 0x4bc) = D(self + 0x4a4);
    D(self + 0x890) = 0x40a00000u;  // 5.0f
    VC(self, 0x19c, 0x61u, 1u, 0u, 0u, 0u, 0u);
    return VC(self, 0x1b8, 0x82u);
}

static uint32_t fn_0042e910_Abi(uint32_t eax)
{
    return fn_0042e910(eax);
}
ABI_ENTRY_0042e910(fn_0042e910_Entry, fn_0042e910_Abi)
HOOK(0x0042e910, "fn_0042e910", fn_0042e910_Entry, nullptr);

// 0x0042ec20 (eax = AI object; the listed ecx input is only a `push ecx` slot; stack: range, a1; ret 8): target
// upkeep, skipped with +0x488 bit 2 or 19. A target that fails virtual +0x180 (target, 2 * +0x5dc) is dropped
// (virtual +0x23c (0, 0), +0x490 = 0); when +0x490 - frame time <= 0 the target reference is released; then
// 0x00426920 (range, a1, 0) may pick a new one (virtual +0x23c (t, 1)). Callers 0x0042ecf0, 0x00430140, 0x0043ee90,
// 0x00447040.
uint32_t fn_0042ec20(uint32_t self, uint32_t a0, uint32_t a1)
{
    uint32_t f = D(self + 0x488);
    if ((f & 4) || (f & 0x80000))
        return f;
    uint32_t edi = self + 0x588;
    if (D(edi))
    {
        float r2 = Fl(self + 0x5dc) + Fl(self + 0x5dc);
        if (!VB(self, 0x180, D(edi), fb(r2)))
        {
            VC(self, 0x23c, 0u, 0u);
            D(self + 0x490) = 0;
        }
    }
    if (Fl(self + 0x490) - FrameTime() <= K(0x0056c05c))
    {
        if (D(edi))
        {
            Unref(D(edi));
            D(edi) = 0;
        }
    }
    uint32_t t = orig::fn_00426920(self, a0, a1, 0);
    if (t)
        return VC(self, 0x23c, t, 1u);
    return 0;
}

static uint32_t fn_0042ec20_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t a0, uint32_t a1)
{
    return fn_0042ec20(eax, a0, a1);
}
ABI_ENTRY_0042ec20(fn_0042ec20_Entry, fn_0042ec20_Abi)
HOOK(0x0042ec20, "fn_0042ec20", fn_0042ec20_Entry, nullptr);

namespace
{
// 0x004e78d0 (ecx = source quaternion, edx = destination): normalises a 4-vector and leaves its length in st0 (the
// ABI table lists eax); the caller pops it.
void NormalizeQuat(uint32_t ecx_, uint32_t edx_)
{
    static const uint32_t target = 0x004e78d0;
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
    (void)r;
}
}  // namespace

// 0x0042ecf0 (stack: AI object; ret 4): the turret-gunner update (strings "ref_hit_00" 0x005b68fc,
// "wpn_colossus_turret_lp" 0x005b665c). Skipped when dead or in states 0x12c..0x132. With +0x488 bit 14, once the game
// time is past Game+0xb2c + Game+0xb30: leaves list +0x608, swaps +0x604/+0x608, drops the target, clears the bit and
// targets the player. With +0x488 bit 11 or +0x48c bit 9 it aims the turret (0x004a15f0 on +0x61c): target upkeep
// (0x0042ec20 with +0x5dc, +0x904) when +0x488 bit 13; the aim direction is towards the target's hit point
// (0x00428280 "ref_hit_00", or 0x004580a0 when 0x004531e0 holds and +0x5b8 - +0x5c0 <= 0) or its own facing; the
// turret rotates towards it by quaternion slerp (0x004e3070, 0x004e35a0, 0x004f1400 with min(frame time *
// K(0x005bcbe0), 1), 0x004e78d0, 0x004e3260; Object_SetTransform keeps the turret's position) and its loop sound runs
// while turning (dot of the old facing with the new forward below K(0x005bcdcc): Game+0x2c virtual +0xc4 into +0xb3c;
// else virtual +0xd8 stops it). Then the fire timer +0x494: expired (and a target when bits 12/13): virtual +0x198
// (0, 0, 0, 0, 0) fires unless in state 0x10f/0x110, idle animation, new timer random(+0x594, +0x598); running: counts
// down, at zero clears +0x484 bit 2 and calls 0x00427600. Caller 0x0043ee90.
// Frame: fr.U(o) is [esp+o] with the four registers (ebp esi edi ebx) pushed (sub esp, 0x90: o = 0x10..0x9f); 0x10
// turret / blend factor, 0x14 turret position, 0x20 aim point / blended quaternion, 0x30 direction / target
// quaternion, 0x40 old facing axis, 0x4c matrix (0x44 bytes), 0x90 current quaternion.
uint32_t fn_0042ecf0(uint32_t self)
{
    Frame<0xa0> fr;
    if (static_cast<uint8_t>(orig::fn_00454780(self)))
        return 0;
    {
        uint32_t st = D(self + 0x460);
        if (st >= 0x12c && st <= 0x132)
            return 0;
    }
    if (D(self + 0x488) & 0x4000)
    {
        uint32_t g = GameP();
        if (Fl(0x00604a98) - Fl(g + 0xb2c) > Fl(g + 0xb30))
        {
            orig::fn_00454750(D(self + 0x608), self);
            uint32_t a = D(self + 0x608);
            D(self + 0x608) = D(self + 0x604);
            D(self + 0x604) = a;
            uint32_t slot = self + 0x588;
            if (D(slot))
            {
                Unref(D(slot));
                D(slot) = 0;
            }
            D(self + 0x488) &= 0xffffbfffu;
            D(self + 0x490) = 0;
            VC(self, 0x23c, G(0x748), 1u);
        }
    }
    if (!(D(self + 0x488) & 0x800) && !(D(self + 0x48c) & 0x200))
        return 0;
    {
        uint32_t ebx = orig::fn_004a15f0(self + 0x61c);
        fr.U(0x10) = ebx;
        if (ebx)
        {
            VC(ebx, 0x54, fr.A(0x14));
            orig::fn_00453150(ebx, fr.A(0x40));
            if (D(self + 0x488) & 0x2000)
                fn_0042ec20(self, D(self + 0x5dc), D(self + 0x904));
            if ((D(self + 0x488) & 0x3000) && D(self + 0x588))
            {
                if (static_cast<uint8_t>(orig::fn_004531e0(D(self + 0x588))) &&
                    !(static_cast<int32_t>(D(self + 0x5b8) - D(self + 0x5c0)) > 0))
                {
                    orig::fn_004580a0(fr.A(0x20), D(self + 0x588), fr.A(0x14), D(self + 0x5c4), 0);
                    ebx = fr.U(0x10);
                }
                else
                    orig::fn_00428280(fr.A(0x20), self, D(self + 0x588), 0x005b68fcu);
                fr.F(0x30) = fr.F(0x20) - fr.F(0x14);
                fr.F(0x34) = fr.F(0x24) - fr.F(0x18);
                fr.F(0x38) = fr.F(0x28) - fr.F(0x1c);
            }
            else
                orig::fn_00453150(self, fr.A(0x30));
            orig::fn_004e3070(fr.A(0x30), fr.A(0x4c), 0, fr.A(0x14));
            orig::fn_004e35a0(fr.A(0x4c), fr.A(0x90));
            if (D(self + 0x588))
            {
                orig::fn_00453150(ebx, fr.A(0x20));
                Normalize3(fr.A(0x30), fr.A(0x30));
                fr.U(0x28) = fr.U(0x38);
                Normalize3(fr.A(0x20), fr.A(0x20));
                orig::fn_004e3070(fr.A(0x20), fr.A(0x4c), 0, fr.A(0x14));
            }
            else
                VC(ebx, 0x5c, fr.A(0x4c));
            orig::fn_004e35a0(fr.A(0x4c), fr.A(0x30));
            float t = FrameTime() * K(0x005bcbe0);
            fr.F(0x10) = t;
            if (t > K(0x005bcb2c))
                fr.U(0x10) = 0x3f800000u;
            orig::fn_004f1400(fr.A(0x90), fr.A(0x30), fr.A(0x20), fr.U(0x10));
            NormalizeQuat(fr.A(0x20), fr.A(0x20));
            orig::fn_004e3260(fr.A(0x4c), fr.A(0x20));
            fr.U(0x58) = fr.U(0x14);
            fr.U(0x68) = fr.U(0x18);
            fr.U(0x78) = fr.U(0x1c);
            orig::fn_00457990(fr.A(0x4c) & 0xffffff00u, ebx, fr.A(0x4c));
            float m9 = -fr.F(0x70);
            float m5 = -fr.F(0x60);
            float m1 = -fr.F(0x50);
            float dot = fr.F(0x48) * m9 + fr.F(0x44) * m5 + fr.F(0x40) * m1;
            if (!(dot < K(0x005bcdcc)))
                VC(G(0x2c), 0xd8, self + 0xb3c, 0u);
            else if (D(self + 0xb3c) == 0)
                D(self + 0xb3c) = VC(G(0x2c), 0xc4, 0x005b665cu, ebx, 1u, 0u, 0xffffffffu, 0u);
        }
    }
    if (Fl(self + 0x494) <= K(0x0056c05c) && !((D(self + 0x488) & 0x3000) && D(self + 0x588) == 0))
    {
        if (!VB(self, 0x198, 0u, 0u, 0u, 0u, 0u))
            return 0;
        uint32_t st = D(self + 0x460);
        if (st == 0x10f || st == 0x110)
            return 0;
        VC(self, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
        Fl(self + 0x494) = orig::fn_0046e2c0(0, D(self + 0x594), D(self + 0x598));
        return 0;
    }
    float t = Fl(self + 0x494) - FrameTime();
    Fl(self + 0x494) = t;
    if (t <= K(0x0056c05c))
    {
        D(self + 0x484) &= 0xfffffffbu;
        orig::fn_00427600(self);
    }
    return 0;
}

static uint32_t fn_0042ecf0_Abi(uint32_t a0)
{
    return fn_0042ecf0(a0);
}
ABI_ENTRY_0042ecf0(fn_0042ecf0_Entry, fn_0042ecf0_Abi)
HOOK(0x0042ecf0, "GenericAI_TurretUpdate", fn_0042ecf0_Entry, nullptr);

// 0x0042f190 (ecx = AI object). GenericAIObject slot 71 (+0x11c): in state 0x19a (knocked down, guess: set by
// 0x0042f1d0) it disables physics (0x004577b0 (0)), targets the player (virtual +0x23c) and resumes the
// pursuit (virtual +0x230 (1)). Virtual only; not executed in recorded runs.
uint32_t fn_0042f190(uint32_t self)
{
    if (D(self + 0x460) != 0x19a)
        return D(self + 0x460);
    orig::fn_004577b0(self, 0);
    VC(self, 0x23c, G(0x748), 1u);
    return VC(self, 0x230, 1u);
}

static uint32_t fn_0042f190_Abi(uint32_t ecx)
{
    return fn_0042f190(ecx);
}
ABI_ENTRY_0042f190(fn_0042f190_Entry, fn_0042f190_Abi)
HOOK(0x0042f190, "fn_0042f190", fn_0042f190_Entry, nullptr);

// 0x0042f1d0 (ecx = AI object; stack: float out; ret 4). GenericAIObject slot 72 (+0x120): *out = Game+0xd1c *
// +0x948. When it rides a jump pad (+0xb08 with +0x484 bit 28) or 0x00426630 holds: virtual +0x1ac (1); a turbo ride
// ends (+0x484 bits 28-29 cleared, Jumper_PlayWingSounds 0x0047bb30 (0), "turbo_idle" 0x005b6674 on +0xb08, unlinked
// from World+0xf1c); virtual +0x124 (1); returns 0. Otherwise idle animation, state 0x19a, physics on; returns 1.
// Virtual only; not executed in recorded runs.
uint32_t fn_0042f1d0(uint32_t self, uint32_t out)
{
    Fl(out) = Fl(GameP() + 0xd1c) * Fl(self + 0x948);
    if ((D(self + 0xb08) && (D(self + 0x484) & 0x10000000)) || static_cast<uint8_t>(orig::fn_00426630(self)))
    {
        VC(self, 0x1ac, 1u);
        if (D(self + 0xb08) && (D(self + 0x484) & 0x10000000))
        {
            D(self + 0x484) &= 0xcfffffffu;
            orig::fn_0047bb30(D(self + 0xb08), self, self + 0xb0c, 0);
            orig::fn_00408e30(0x81, D(self + 0xb08), 0xffffffffu, 0x005b6674u, 0, 0x3e800000u, 0, 0);
            orig::fn_00454750(D(G(0xa84) + 0xf1c), self);
        }
        VC(self, 0x124, 1u);
        return 0;
    }
    VC(self, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(self, 0x1b8, 0x19au);
    orig::fn_004577b0(self, 1);
    return 1;
}

static uint32_t fn_0042f1d0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0042f1d0(ecx, a0);
}
ABI_ENTRY_0042f1d0(fn_0042f1d0_Entry, fn_0042f1d0_Abi)
HOOK(0x0042f1d0, "fn_0042f1d0", fn_0042f1d0_Entry, nullptr);

// 0x0042f2f0 (ecx = AI object; stack: a0 (bool); ret 4). GenericAIObject slot 73 (+0x124): launch towards the
// player. Unless dead, and only in state 0x19a or with a0: aims at the player's "ref_hit_00" point (0x005b68fc,
// fn_00453c50), computes a ballistic velocity (0x00471a00 with Game+0xd1c * +0x948 and Game+0xd20 *
// K(0x005bcd64)), virtual +0x160, 0x004266b0, detaches (Object_Detach), 0x00454c90 (cl = 0, 1), physics off,
// leaves its squad (0x004324d0 (+0x880, 1), eax = 0), stops the body parts (0x00455a00), sets the velocity, animation
// 0x49 + (rand() * 2 >> 15), virtual +0xf8 (0x170001, 0) and state 0x19b. Virtual only; not executed in recorded runs.
// Frame: fr.U(o) is [esp+o] with the four registers pushed (sub esp, 0x68: o = 0x10..0x77); 0x10 aim point, 0x1c
// own position, 0x28 velocity, 0x34 matrix (0x44 bytes).
uint32_t fn_0042f2f0(uint32_t self, uint32_t a0)
{
    Frame<0x78> fr;
    if (static_cast<uint8_t>(orig::fn_00454780(self)))
        return 1;
    if (D(self + 0x460) != 0x19a && !static_cast<uint8_t>(a0))
        return 0;
    VC(self, 0x54, fr.A(0x1c));
    orig::fn_00453c50(G(0x748), 0x005b68fcu, fr.A(0x34), 1, 0, 0, 1, 0, 0);
    fr.U(0x14) = fr.U(0x50);
    float vz = Fl(GameP() + 0xd20) * K(0x005bcd64);
    fr.U(0x10) = fr.U(0x40);
    float sp = Fl(GameP() + 0xd1c) * Fl(self + 0x948);
    fr.U(0x18) = fr.U(0x60);
    orig::fn_00471a00(fr.A(0x28), fr.A(0x10), fr.A(0x1c), fb(sp), fb(vz), 0);
    VC(self, 0x160);
    orig::fn_004266b0(self);
    orig::fn_00453b20(self);
    orig::fn_00454c90(self, 0, 1);
    orig::fn_004577b0(self, 0);
    orig::fn_004324d0(0, self, D(self + 0x880), 1);
    D(self + 0x39c) = orig::fn_00455a00(D(self + 0x39c));
    D(self + 0x398) = orig::fn_00455a00(D(self + 0x398));
    orig::fn_00458510(fr.A(0x28), self);
    int32_t r = Rand();
    VC(self, 0x19c, static_cast<uint32_t>(((r << 1) >> 15) + 0x49), 1u, 0u, 0u, 0u, 0u);
    VC(self, 0xf8, 0x170001u, 0u);
    return VC(self, 0x1b8, 0x19bu);
}

static uint32_t fn_0042f2f0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0042f2f0(ecx, a0);
}
ABI_ENTRY_0042f2f0(fn_0042f2f0_Entry, fn_0042f2f0_Abi)
HOOK(0x0042f2f0, "fn_0042f2f0", fn_0042f2f0_Entry, nullptr);

// 0x0042f470 (ecx = AI object; stack: a0 (bool), position pointer, a2 (unused); ret 0xc). GenericAIObject slot 74
// (+0x128): without a0 just virtual +0x17c (0, 1). With a0: joins the player's squad list (0x004324d0 (player, 1),
// eax = 0), stops the body (0x00455a00, zero velocity), sets +0x488 bit 24, links itself into World+0xf1c unless
// already there (ObjectList_Contains / ObjectList_Link), sets +0x48c bits 4 and 11 and sends itself a damage-like
// event (virtual +0x17c (&event, 1)): kind 0x83, +0x10 = 0x00458720 (edx = 0), +0x14 = health, +0x18 = *position,
// +0x24 = 100.0, +0x2c = the player. Virtual only; not executed in recorded runs.
// Frame: fr.U(o) is [esp+o] with ebx, esi, edi pushed (sub esp, 0x40: o = 0x0c..0x4b); 0x0c zero velocity, 0x18
// event (0x34 bytes).
uint32_t fn_0042f470(uint32_t self, uint32_t a0, uint32_t pos, uint32_t /*a2*/)
{
    Frame<0x4c> fr;
    if (!static_cast<uint8_t>(a0))
        return VC(self, 0x17c, 0u, 1u);
    std::memset(fr.b + 0x18, 0, 0x34);
    orig::fn_004324d0(0, self, G(0x748), 1);
    D(self + 0x39c) = orig::fn_00455a00(D(self + 0x39c));
    D(self + 0x398) = orig::fn_00455a00(D(self + 0x398));
    fr.U(0xc) = 0;
    fr.U(0x10) = 0;
    fr.U(0x14) = 0;
    orig::fn_00458510(fr.A(0xc), self);
    D(self + 0x488) |= 0x1000000;
    if (!static_cast<uint8_t>(orig::fn_00454690(D(G(0xa84) + 0xf1c), self)))
        orig::fn_004546d0(D(G(0xa84) + 0xf1c), self);
    D(self + 0x48c) |= 0x10;
    fr.U(0x28) = orig::fn_00458720(self, 0);
    fr.F(0x2c) = orig::fn_004555b0(self);
    fr.U(0x30) = D(pos);
    fr.U(0x34) = D(pos + 4);
    fr.U(0x38) = D(pos + 8);
    fr.U(0x3c) = 0x42c80000u;  // 100.0f
    fr.U(0x18) = 0x83;
    fr.U(0x44) = G(0x748);
    D(self + 0x48c) |= 0x800;
    return VC(self, 0x17c, fr.A(0x18), 1u);
}

static uint32_t fn_0042f470_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0042f470(ecx, a0, a1, a2);
}
ABI_ENTRY_0042f470(fn_0042f470_Entry, fn_0042f470_Abi)
HOOK(0x0042f470, "fn_0042f470", fn_0042f470_Entry, nullptr);

// 0x0042f870 (ecx = AI object; stack: a0, trigger id; ret 8). GenericAIObject slot 14 (+0x38), the trigger handler:
// the id +0x52c starts 0x0042f5c0 (0); the id +0x530 kills it (virtual +0xd4, Character_Die, with a zeroed 0x34-byte
// event of kind 0x8000). Returns 0. Callers 0x00433a10, 0x004342d0, 0x004367c0, 0x00483680.
uint32_t fn_0042f870(uint32_t self, uint32_t /*a0*/, uint32_t id)
{
    if (id == D(self + 0x52c))
    {
        orig::fn_0042f5c0(self, 0);
        return 0;
    }
    if (id == D(self + 0x530))
    {
        uint32_t ev[13];
        std::memset(ev, 0, sizeof(ev));
        ev[0] = 0x8000;
        VC(self, 0xd4, u(ev));
    }
    return 0;
}

static uint32_t fn_0042f870_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0042f870(ecx, a0, a1);
}
ABI_ENTRY_0042f870(fn_0042f870_Entry, fn_0042f870_Abi)
HOOK(0x0042f870, "fn_0042f870", fn_0042f870_Entry, nullptr);

// 0x0042f8d0 (ecx = AI object; stack: other object; ret 4). GenericAIObject slot 94 (+0x178): true when the AI cannot
// be engaged by the other object (guess: "busy"): 0x00426630, dead, virtual +0x1ec false, +0x898 set, other == self,
// states 0x0c-0x10, 0x5a, 0x7c, 0x8c, 0x96, 0xaa, 0xac, 0xdc, 0xde, 0xe7-0xeb, 0xf0, 0x197, 0x19a-0x19d (jump tables
// 0x0042f9ec / 0x0042fa94), +0x894 set, turn state +0x498 = 0x31, a parent +0x9bc that is not in state 0x98 (unless
// the AI has a group and the other is the parent's target), +0x484 bit 19, riding a spline (state 0x97, 0x0044b4b0
// non-zero), or (+0x484 bit 28) +0xb20 < +0xb24. Caller 0x00486da0. Not executed in recorded runs.
uint32_t fn_0042f8d0(uint32_t self, uint32_t other)
{
    if (static_cast<uint8_t>(orig::fn_00426630(self)) || static_cast<uint8_t>(orig::fn_00454780(self)) ||
        !VB(self, 0x1ec) || D(self + 0x898) != 0)
        return 1;
    if (other == self)
        return 1;
    uint32_t st = D(self + 0x460);
    static const uint32_t busy[] = {0xc,  0xd,  0xe,  0xf,  0x10, 0x5a, 0x7c,  0x8c,  0x96,  0xaa,  0xac,  0xdc,
                                    0xde, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xf0, 0x197, 0x19a, 0x19b, 0x19c, 0x19d};
    for (uint32_t s : busy)
        if (st == s)
            return 1;
    if (D(self + 0x894) != 0)
        return 1;
    if (D(self + 0x498) == 0x31)
        return 1;
    uint32_t parent = D(self + 0x9bc);
    if (parent)
    {
        bool skip = D(self + 0x574) != 0 && other == fn_0042c6f0(parent);
        if (!skip && D(parent + 0x460) != 0x98)
            return 1;
    }
    uint32_t f = D(self + 0x484);
    if (f & 0x80000)
        return 1;
    if (st == 0x97 && D(self + 0x570) && orig::fn_0044b4b0(0, D(self + 0x570), self) != 0)
        return 1;
    if ((f & 0x10000000) && Fl(self + 0xb20) < Fl(self + 0xb24))
        return 1;
    return 0;
}

static uint32_t fn_0042f8d0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0042f8d0(ecx, a0);
}
ABI_ENTRY_0042f8d0(fn_0042f8d0_Entry, fn_0042f8d0_Abi)
HOOK(0x0042f8d0, "fn_0042f8d0", fn_0042f8d0_Entry, nullptr);

// 0x0042fb60 (ecx = AI object; stack: attacker or 0, a1 (object to flee from when no attacker), a2 (bool); ret 0xc).
// GenericAIObject slot 51 (+0xcc): scare / flee (guess). Unless dead or virtual +0x208 fails: a member of a group
// (+0x9bc) leaves it (with a2 it first passes the scare to the group's other members, list +0xb78, virtual +0xcc
// (0, attacker ? self : 0, 0)), ObjectList_Unlink, Object_ReleaseHolder 0x0042cc10, 0x0042cff0. Otherwise a guard on
// cover (+0x898, +0x484 bit 15) attacked by someone becomes guarding (+0x484 bit 25); else it stops when busy
// (virtual +0x178, 0x0042f8d0) or already in state 0xc8. Guarding: with an attacker, cower animation 0x10 + (rand() * 2
// >> 15), virtual +0x164, state 0xca. Otherwise remembers the attacker (+0xb54, reference) and virtual +0x164 (or
// virtual +0x160 without one), runs away from the attacker's side (the player when there is an attacker or no a1;
// 2D direction normalised by 0x004e7780, distance K(0x005bcf00) / K(0x005bcb60), 0x0046f6d0), virtual +0x18c (0),
// adjusts +0x484 (bit 5 set, bits 25 and 16 cleared) and +0x488 bit 0, 0x00427210, timer +0x490 = rand() *
// K(0x005bce30) + K(0x005bcb7c) and state 0xc8. Caller 0x0048f690. Not executed in recorded runs.
// Frame: fr.U(o) is [esp+o] with the four registers pushed (sub esp, 0x24: o = 0x10..0x33); 0x10 iterator / 2D
// direction, 0x1c own position / destination, 0x28 the other's position.
uint32_t fn_0042fb60(uint32_t self, uint32_t attacker, uint32_t a1, uint32_t a2)
{
    Frame<0x34> fr;
    if (static_cast<uint8_t>(orig::fn_00454780(self)))
        return 1;
    if (!VB(self, 0x208))
        return 0;
    uint32_t parent = D(self + 0x9bc);
    if (parent)
    {
        if (static_cast<uint8_t>(a2))
        {
            fr.U(0x10) = 0;
            fr.U(0x14) = 0;
            uint32_t o = orig::fn_004e7340(D(parent + 0xb78), fr.A(0x10));
            while (o)
            {
                if (o != self)
                    VC(o, 0xcc, 0u, attacker ? self : 0u, 0u);
                o = orig::fn_004e73c0(fr.A(0x10));
            }
            if (fr.U(0x14))
                --I(fr.U(0x14) + 0x18);
        }
        orig::fn_00454750(D(D(self + 0x9bc) + 0xb78), self);
        orig::fn_0042cc10(self);
        fn_0042cff0(self);
    }
    else if (attacker && D(self + 0x898) && (D(self + 0x484) & 0x8000))
        D(self + 0x484) |= 0x2000000;
    else
    {
        if (VB(self, 0x178, attacker))
            return 1;
        if (D(self + 0x460) == 0xc8)
            return 0;
    }
    if (D(self + 0x484) & 0x2000000)
    {
        if (!attacker)
            return 0;
        int32_t r = Rand();
        VC(self, 0x19c, static_cast<uint32_t>(((r << 1) >> 15) + 0x10), 1u, 0u, 0u, 0u, 0u);
        VC(self, 0x164);
        return VC(self, 0x1b8, 0xcau);
    }
    if (attacker)
    {
        uint32_t old = D(self + 0xb54);
        if (old)
            Unref(old);
        D(self + 0xb54) = attacker;
        ++I(attacker + 0x28);
        VC(self, 0x164);
    }
    else
        VC(self, 0x160);
    VC(self, 0x54, fr.A(0x1c));
    uint32_t from = (attacker || !a1) ? G(0x748) : a1;
    VC(from, 0x54, fr.A(0x28));
    fr.F(0x10) = fr.F(0x1c) - fr.F(0x28);
    fr.F(0x14) = fr.F(0x20) - fr.F(0x2c);
    orig::fn_004e7780(fr.A(0x10), fr.A(0x10));
    VC(self, 0x54, fr.A(0x1c));
    float k = attacker ? K(0x005bcf00) : K(0x005bcb60);
    fr.F(0x1c) = fr.F(0x10) * k + fr.F(0x1c);
    fr.F(0x20) = fr.F(0x14) * k + fr.F(0x20);
    orig::fn_0046f6d0(self, G(0xaa4), fr.A(0x1c), 0, 0, 0, 0, 0);
    VC(self, 0x18c, 0u);
    uint32_t f = D(self + 0x484) | 0x20;
    D(self + 0x484) = f;
    D(self + 0x484) = f & 0xfdffffffu;
    D(self + 0x488) &= 0xfffffffeu;
    D(self + 0x484) &= 0xfffeffffu;
    orig::fn_00427210(self);
    Fl(self + 0x490) = static_cast<float>(Rand()) * K(0x005bce30) + K(0x005bcb7c);
    return VC(self, 0x1b8, 0xc8u);
}

static uint32_t fn_0042fb60_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0042fb60(ecx, a0, a1, a2);
}
ABI_ENTRY_0042fb60(fn_0042fb60_Entry, fn_0042fb60_Abi)
HOOK(0x0042fb60, "fn_0042fb60", fn_0042fb60_Entry, nullptr);

// 0x0042fe50 (ecx = AI object; stack: a0 (bool); ret 4). GenericAIObject slot 108 (+0x1b0): knocked over / thrown
// (guess). When 0x00426630 holds: virtual +0x1ac (0) and a0 = false. Virtual +0xe4 true: animation 0x71, state 0xeb.
// In state 0x0f with a held object +0xab0: it drops it (0x00454c90 on it, cl = 1; 0x0042df70 (45.0) on it, its idle
// animation with self's +0x580/+0x584, state 0x32; its own +0xab0 and the reference are released) and clears +0x484
// bit 13. With a0, virtual +0x1c4, +0x87c > K(0x005bcb7c), the roll (rand() * 100 >> 15) >= Game+0xca8 and 0x00454bc0:
// +0x578 = 0, +0x878 = random(0.25, +0x87c - 1), animation 0x71, state 0xeb. Otherwise: +0x578 = 0, virtual +0x168,
// stops and detaches the body, physics off, joins World+0xf1c, +0x874 = its z, animation 0x73 (states 0xe9/0xeb) or
// 0x49 + (rand() * 2 >> 15), state 0xe7. Virtual only; not executed in recorded runs.
uint32_t fn_0042fe50(uint32_t self, uint32_t a0)
{
    uint32_t zero[3];
    float pos[3];
    if (static_cast<uint8_t>(orig::fn_00426630(self)))
    {
        VC(self, 0x1ac, 0u);
        a0 &= 0xffffff00u;
    }
    if (VB(self, 0xe4))
    {
        VC(self, 0x19c, 0x71u, 0u, 0u, 0u, 0u, 0u);
        return VC(self, 0x1b8, 0xebu);
    }
    if (D(self + 0x460) == 0xf)
    {
        uint32_t esi = self + 0xab0;
        if (D(esi))
        {
            orig::fn_00454c90(D(esi), 1, 1);
            fn_0042df70(D(esi), 0x42340000u);
            VC(D(esi), 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(self + 0x580), D(self + 0x584));
            VC(D(esi), 0x1b8, 0x32u);
            uint32_t ebx = D(esi) + 0xab0;
            if (D(ebx))
            {
                Unref(D(ebx));
                D(ebx) = 0;
            }
            Unref(D(esi));
            D(esi) = 0;
        }
        D(self + 0x484) &= 0xffffdfffu;
    }
    if (static_cast<uint8_t>(a0) && VB(self, 0x1c4) && Fl(self + 0x87c) > K(0x005bcb7c) &&
        !(static_cast<uint32_t>((Rand() * 100) >> 15) < G(0xca8)) && static_cast<uint8_t>(orig::fn_00454bc0(self)))
    {
        float hi = Fl(self + 0x87c) - K(0x005bcb2c);
        D(self + 0x578) = 0;
        Fl(self + 0x878) = orig::fn_0046e2c0(0, 0x3e800000u, fb(hi));
        VC(self, 0x19c, 0x71u, 0u, 0u, 0u, 0u, 0u);
        return VC(self, 0x1b8, 0xebu);
    }
    D(self + 0x578) = 0;
    VC(self, 0x168);
    D(self + 0x39c) = orig::fn_00455a00(D(self + 0x39c));
    D(self + 0x398) = orig::fn_00455a00(D(self + 0x398));
    zero[0] = zero[1] = zero[2] = 0;
    orig::fn_00458510(u(zero), self);
    orig::fn_00453b20(self);
    orig::fn_004577b0(self, 0);
    if (!static_cast<uint8_t>(orig::fn_00454690(D(G(0xa84) + 0xf1c), self)))
        orig::fn_004546d0(D(G(0xa84) + 0xf1c), self);
    VC(self, 0x54, u(pos));
    uint32_t st = D(self + 0x460);
    Fl(self + 0x874) = pos[2];
    if (st == 0xe9 || st == 0xeb)
        VC(self, 0x19c, 0x73u, 0u, 0u, 0u, 0u, 0u);
    else
    {
        int32_t r = Rand();
        VC(self, 0x19c, static_cast<uint32_t>(((r << 1) >> 15) + 0x49), 1u, 0u, 0u, 0u, 0u);
    }
    return VC(self, 0x1b8, 0xe7u);
}

static uint32_t fn_0042fe50_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0042fe50(ecx, a0);
}
ABI_ENTRY_0042fe50(fn_0042fe50_Entry, fn_0042fe50_Abi)
HOOK(0x0042fe50, "fn_0042fe50", fn_0042fe50_Entry, nullptr);

// 0x00430140 (eax = AI object; stack: position pointer, radius (float); ret 8): set alight / "Guy Fawkes" reaction
// (strings "fx_guy_fawkes" 0x005b660c, "ref_fawkes" 0x005b661c). Only when virtual +0xe8 holds and it is alive:
// stamps Game+0xb2c with the game time; unless +0x488 bit 14 is already set: attaches the effect (Fx_Create on
// Game+0xa90, al = 0), swaps +0x604/+0x608 and links itself into the new +0x608 list, sets the bit and drops its
// target. Then, unless busy (virtual +0x178 (0)): guarding AI resets +0x490 and runs 0x0042ec20 (+0x5dc, 0); others
// outside the radius of the position run to the nearest point of that circle (2D, 0x004e7780, 0x0046f6d0), state
// 0xfa, 0x00427210. Caller 0x00430340. Not executed in recorded runs.
// Frame: fr.U(o) is [esp+o] with the four registers pushed (sub esp, 0x98: o = 0x10..0xa7); 0x10 own position /
// destination, 0x1c 2D direction, 0x28 effect descriptor (+0 = 4, +0x18 = bone name, +0x1c = owner).
uint32_t fn_00430140(uint32_t self, uint32_t pos, uint32_t radius)
{
    Frame<0xa8> fr;
    if (!VB(self, 0xe8))
        return 0;
    if (static_cast<uint8_t>(orig::fn_00454780(self)))
        return 1;
    D(GameP() + 0xb2c) = D(0x00604a98);
    uint32_t f = D(self + 0x488);
    if (f & 0x4000)
        return 0;
    fr.U(0x44) = self;
    fr.U(0x28) = 4;
    fr.U(0x40) = 0x005b661cu;
    orig::fn_0041cc60(f & 0xffffff00u, G(0xa90), 0x005b660cu, fr.A(0x28));
    uint32_t a604 = D(self + 0x604);
    uint32_t a608 = D(self + 0x608);
    D(self + 0x608) = a604;
    D(self + 0x604) = a608;
    orig::fn_004546d0(a604, self);
    D(self + 0x488) |= 0x4000;
    uint32_t slot = self + 0x588;
    if (D(slot))
    {
        Unref(D(slot));
        D(slot) = 0;
    }
    if (VB(self, 0x178, 0u))
        return 1;
    if (D(self + 0x484) & 0x2000000)
    {
        D(self + 0x490) = 0;
        return fn_0042ec20(self, D(self + 0x5dc), 0);
    }
    VC(self, 0x54, fr.A(0x10));
    float r = bf(radius);
    {
        float dy = Fl(pos + 4) - fr.F(0x14);
        float dx = Fl(pos) - fr.F(0x10);
        float d = dx * dx + dy * dy;
        if (!(r * r < d))
            return 0;
    }
    fr.F(0x1c) = fr.F(0x10) - Fl(pos);
    fr.F(0x20) = fr.F(0x14) - Fl(pos + 4);
    orig::fn_004e7780(fr.A(0x1c), fr.A(0x1c));
    fr.F(0x10) = fr.F(0x1c) * r + Fl(pos);
    fr.F(0x14) = fr.F(0x20) * r + Fl(pos + 4);
    orig::fn_0046f6d0(self, G(0xaa4), fr.A(0x10), 0, 0, 0, 0, 0);
    VC(self, 0x1b8, 0xfau);
    return orig::fn_00427210(self);
}

static uint32_t fn_00430140_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_00430140(eax, a0, a1);
}
ABI_ENTRY_00430140(fn_00430140_Entry, fn_00430140_Abi)
HOOK(0x00430140, "fn_00430140", fn_00430140_Entry, nullptr);
