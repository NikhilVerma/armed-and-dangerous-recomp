// Recovered: batch ai_06, the generic AI per-frame update 0x0043ee90 (29223 bytes, the largest function of the game).
// Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still to
// do.
//
// Method: the function is one big state machine (a switch on the AI state at +0x460 with four jump tables) and was
// recovered block by block, close to the disassembly: the original stack frame (sub esp, 0x4b0) is kept as one byte
// array addressed by the original esp offsets (SP(o) is [esp+o] with the four saved registers pushed and no call
// arguments pending), the registers the original keeps across blocks are C++ variables of the same name, and every
// branch target that is entered from more than one place is a label L_<address>. Floats follow the disassembly's
// operation order in single precision. Names stay fn_XXXXXXXX for callees; "guess" marks interpretations.
//
// Jump tables (read from the image, case values kept):
//   0x004460c0 (byte index table 0x00446190, index = state - 1, states 0x01..0xb6)
//   0x00446248 (dword table, index = state - 0xb9, states 0xb9..0xca)
//   0x00446290 (byte index table 0x004462dc, index = state - 0xdc, states 0xdc..0x118)
//   0x0044631c (byte index table 0x00446364, index = state - 0x11a, states 0x11a..0x1f4)
//   compares: 0xb8 -> 0x00443057, 0xd2 -> 0x00443ba1, 0x119 -> 0x00445204; every other state -> 0x004460a2 (end).
//
// Register state at the dispatch (0x0043f4ee), which several states rely on: esi = game (0x005fe490), edi = 0,
// ebx = self + 0xae0; [esp+0x10] = a byte flag (bit 1 of 0x00408800(4, self)+0x1c, guess: "animation finished"),
// [esp+0x20] = the frame time 0x00604a90.
//
// Callees the ABI table describes wrongly (handled here):
//   0x004e7880 returns st0 (the vec3 length), not eax -> Normalize3 (inline asm, pops st0).
//   0x004e8040 / 0x004e8060 / 0x004e8080 / 0x004e7760 / 0x004e7800 return st0 (2D distance, squared 2D / 3D
//   distance, 2D / 3D length), listed as "none" -> recomputed locally in the original x87 operation order.
//   0x00471970 returns st0, listed as eax -> Call_00471970 (inline asm, pops st0).
//   0x00481ab0 has no ret (tail jump to virtual +0xd4, which pops its one argument); no wrapper exists ->
//   Call_00481ab0.
//   virtual +0x1a4 takes one argument (0x0043ca00, ret 4): the pushes around it outlive it (states 0x29, 0x19c).
//   "ecx" inputs of 0x0042d0f0, 0x00408820, 0x0046e2c0, 0x00406430, 0x0042eba0, 0x0042b8e0 are only
//   `push ecx` stack-slot reservations; 0x00457870 reads no stack arguments (ABI: 3).
// Where the original passes a register whose low byte it just cleared (xor al, al), the upper bytes are passed as
// the original left them when they are known, otherwise 0 (the callees test only the low byte).
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
// A float constant of the game's .rdata (0x0056c05c = 0.0).
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
// Leaf distance helpers, recomputed here in the original's x87 order because they return st0 while the ABI table
// lists no result (calling them through orig:: would leave st0 on the FPU stack):
// 0x004e8060 (eax = a, ecx = b): squared 2D distance (x, y).
inline float Dist2Sq(uint32_t a, uint32_t b)
{
    float dy = Fl(a + 4) - Fl(b + 4);
    float dx = Fl(a) - Fl(b);
    return dx * dx + dy * dy;
}
// 0x004e8080 (eax = a, ecx = b): squared 3D distance.
inline float Dist3Sq(uint32_t a, uint32_t b)
{
    float dz = Fl(a + 8) - Fl(b + 8);
    float dy = Fl(a + 4) - Fl(b + 4);
    float dx = Fl(a) - Fl(b);
    return dx * dx + dy * dy + dz * dz;
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
// 0x00481ab0 (eax, esi = object; stack: 1 argument). It has no ret of its own: it ends with a tail jump to the
// object's virtual +0xd4 after overwriting its argument with 0, and that thiscall method pops the argument (the
// callers here do not add esp). No generated wrapper exists, so it is called by hand.
uint32_t Call_00481ab0(uint32_t eax_, uint32_t esi_, uint32_t a0)
{
    static const uint32_t target = 0x00481ab0;
    uint32_t r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a0
        mov eax, eax_
        mov esi, esi_
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov r, eax
    }
    return r;
}
// 0x004e7760 (eax = vec2): its 2D length in st0 (the ABI table lists no result).
inline float Len2(uint32_t a)
{
    float x = Fl(a);
    float y = Fl(a + 4);
    return std::sqrt(x * x + y * y);
}
// 0x004e8040 (eax = a, ecx = b): 2D distance in st0 (the ABI table lists no result).
inline float Dist2(uint32_t a, uint32_t b)
{
    float dy = Fl(a + 4) - Fl(b + 4);
    float dx = Fl(a) - Fl(b);
    return std::sqrt(dx * dx + dy * dy);
}
// 0x004e7800 (eax = vec3): its length in st0 (the ABI table lists no result).
inline float Len3(uint32_t a)
{
    float x = Fl(a);
    float y = Fl(a + 4);
    float z = Fl(a + 8);
    return std::sqrt(x * x + y * y + z * z);
}
// 0x00471970 (eax, ecx, edx, esi; stack: 4 arguments, caller pops): scales the vector edx - eax into ecx and returns
// the distance in st0 (the ABI table lists eax; the callers pop st0).
float Call_00471970(uint32_t eax_, uint32_t ecx_, uint32_t edx_, uint32_t esi_, uint32_t a0, uint32_t a1, uint32_t a2,
                    uint32_t a3)
{
    static const uint32_t target = 0x00471970;
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a3
        push a2
        push a1
        push a0
        mov eax, eax_
        mov ecx, ecx_
        mov edx, edx_
        mov esi, esi_
        call dword ptr [target]
        add esp, 16
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp r
    }
    return r;
}
// x87 compare helpers as the disassembly tests them (fcomp; fnstsw; test ah, ...):
//   test ah,0x41 / jp  -> taken when a > b or unordered       test ah,0x41 / jne -> taken when !(a > b)
//   test ah,5 / jnp    -> taken when a < b                     test ah,5 / jp     -> taken when !(a < b)
}  // namespace

// 0x0043ee90 GenericAI_Update (ecx = AI object; returns eax, which the callers pass through and ignore).
// The per-frame update of the generic AI character (name from the tracker; vtable callers 0x00446440 and others).
// Prologue: 0x0042e1c0 (ebx = self), virtual +0x224, clears flag 0x20 of +0x424 (0x00454c90), then
//  - a pending object at +0x880 that is not kept (virtual +0xd8) is dropped and an event 0x83 is sent to virtual
//    +0x17c (a 0x34-byte record: position from 0x00453150, two random floats, game +0x748) - early return;
//  - timers +0x578 (drops +0x57c), +0x878/+0x87c, +0x4a8 (random cue 0xae), +0xb34 (re-armed with a random 3..5 s,
//    tests the position against the two rectangles game +0xbd0..+0xbec and calls virtual +0xd4(0) when outside both),
//    objects +0xae0 / +0xadc (virtual +0x1ac, 0x00425be0, early returns);
// then the state machine on +0x460 (see the jump tables at the top of this file); every state ends at 0x004460a2:
// 0x0042e910 (eax = self) and flag 0x800 of +0x484 cleared.
// Evidence: disassembly 0x0043ee90..0x004460b7; state values from the jump tables.
uint32_t GenericAI_Update(uint32_t self)
{
    alignas(16) uint8_t fr[0x4b0];
#define SP(o) (u(fr) + (o) - 0x10)
    const uint32_t ebp = self;
    uint32_t eax = 0, ecx = 0, edx = 0, ebx = 0, esi = 0, edi = 0;
    uint32_t r = 0;
    float t = 0.0f;
    bool cl = false, dl = false;

    std::memset(fr, 0, sizeof fr);  // (not in the original; keeps the compiler quiet about the uninitialised frame)
    D(SP(0x20)) = D(0x00604a90);  // the frame time (float)
    orig::fn_0042e1c0(ebp);
    VC(ebp, 0x224);
    orig::fn_00454c90(ebp, 0 /* cl = 0 */, 1);
    eax = orig::fn_00408800(4, ebp);
    By(SP(0x10)) = static_cast<uint8_t>((By(eax + 0x1c) >> 1) & 1);
    ecx = D(ebp + 0x880);
    esi = ebp + 0x880;
    if (ecx && !VB(ecx, 0xd8))
    {
        std::memset(P(SP(0x240)), 0, 0xd * 4);
        orig::fn_004324d0(0, ebp, D(esi), 1);
        ClearRef(esi);
        D(SP(0x250)) = orig::fn_00458720(ebp, 0);
        Fl(SP(0x254)) = orig::fn_004555b0(ebp);
        orig::fn_00453150(ebp, SP(0x258));
        eax = Rand();
        D(SP(0x3c)) = eax;
        Fl(SP(0x260)) = static_cast<float>(I(SP(0x3c))) * K(0x005bcdf4) + K(0x005bcd48);
        eax = Rand();
        D(SP(0x3c)) = eax;
        Fl(SP(0x264)) = static_cast<float>(I(SP(0x3c))) * K(0x005bcdf0) + K(0x005bcdec);
        Normalize3(SP(0x258), SP(0x258));  // fstp st(0)
        D(SP(0x240)) = 0x83;
        D(SP(0x26c)) = D(GameP() + 0x748);
        return VC(ebp, 0x17c, SP(0x240), 0u);
    }
    // 0x0043efff
    orig::fn_0042e050(ebp);
    if (static_cast<uint8_t>(orig::fn_00454bc0(ebp)))
        VC(ebp, 0xfc, 1u, 2u);
    else
        VC(ebp, 0xfc, 0u, 2u);
    ecx = D(ebp + 0x894);
    edi = ebp + 0x894;
    if (ecx)
    {
        eax = orig::fn_00408800(4, ecx);
        if (D(eax) == 1 && (By(eax + 0x1c) & 2))
        {
            VC(ecx, 0xdc);
            Unref(D(edi));
            D(edi) = 0;
        }
    }
    // 0x0043f080: timer +0x578 drops the object +0x57c.
    if (Fl(ebp + 0x578) > K(0x0056c05c) && !static_cast<uint8_t>(orig::fn_00454780(ebp)))
    {
        t = Fl(ebp + 0x578) - Fl(SP(0x20));
        Fl(ebp + 0x578) = t;
        if (t <= K(0x0056c05c))
        {
            esi = ebp + 0x57c;
            eax = D(esi);
            if (eax)
            {
                if (!VB(ebp, 0x178, eax))
                    VC(ebp, 0x1b4, D(esi), 0u, 1u);
                else if (D(ebp + 0x460) == 0xdc || D(ebp + 0x460) == 0xde)
                {
                    orig::fn_0042d0f0(ebp, 0 /* push ecx: slot only */, 0, 1);
                    VC(ebp, 0x1b8, 0xddu);
                }
                Unref(D(esi));
                D(esi) = 0;
            }
        }
    }
    // 0x0043f150: timers +0x878 / +0x87c.
    if (Fl(ebp + 0x878) > K(0x0056c05c) && !static_cast<uint8_t>(orig::fn_00454780(ebp)))
    {
        t = Fl(ebp + 0x878) - Fl(SP(0x20));
        Fl(ebp + 0x878) = t;
        if (t <= K(0x0056c05c))
            VC(ebp, 0x1b0, 1u);
        else
            Fl(ebp + 0x87c) = Fl(ebp + 0x87c) - Fl(SP(0x20));
    }
    // 0x0043f1aa
    orig::fn_0042ecf0(ebp);
    esi = D(ebp + 0xae0);
    ebx = ebp + 0xae0;
    if (esi && static_cast<uint8_t>(orig::fn_00454780(esi)))
    {
        if (D(ebp + 0x460) == 0x29)
            return 1;  // eax = AL of 0x00454780
        VC(ebp, 0x1ac, 1u);
        r = orig::fn_00425be0(D(ebx), ebp, 0);
        Unref(D(ebx));
        D(ebx) = 0;
        return r;  // (the original returns whatever the reference drop left in eax)
    }
    esi = D(ebp + 0xadc);
    if (esi && static_cast<uint8_t>(orig::fn_00454780(esi)) && static_cast<uint8_t>(orig::fn_00426630(ebp)))
    {
        VC(ebp, 0x1ac, 0u);
        return orig::fn_00425be0(D(ebp + 0xadc), ebp, 1);
    }
    // 0x0043f268: timer +0x4a8.
    if (Fl(ebp + 0x4a8) > K(0x0056c05c))
    {
        t = Fl(ebp + 0x4a8) - Fl(SP(0x20));
        Fl(ebp + 0x4a8) = t;
        if (t <= K(0x0056c05c))
        {
            D(ebp + 0x4a8) = 0;
            if (!static_cast<uint8_t>(orig::fn_00454780(ebp)) && !static_cast<uint8_t>(orig::fn_00426630(ebp)) &&
                D(ebp + 0x460) != 0x82 && !VB(ebp, 0xe4))
            {
                esi = D(ebp + 0x488) | 0x100000;
                D(ebp + 0x488) = esi;
                r = VC(ebp, 0x54, SP(0x70));
                edi = GameP();
                Fl(SP(0x70)) = Fl(ebp + 0x908) * K(0x005bcc08) + Fl(SP(0x70));
                Fl(SP(0x74)) = Fl(ebp + 0x90c) * K(0x005bcc08) + Fl(SP(0x74));
                Fl(SP(0x78)) = Fl(ebp + 0x910) * K(0x005bcc08) + Fl(SP(0x78));
                edi = D(edi + 0xaa4);
                orig::fn_0046f5b0(r & 0xffffff00u /* xor al, al */, 0, SP(0x70), ebp, edi, 0, 0);
                if (static_cast<int32_t>(orig::fn_004e7730(0x64)) < 0x14)
                {
                    D(ebp + 0x484) |= 0x20;
                    VC(ebp, 0x160);
                }
                orig::fn_00427210(ebp);
                VC(ebp, 0x1b8, 0xaeu);
            }
        }
    }
    // 0x0043f3a3: timer +0xb34 and the two rectangles of the game object.
    if (static_cast<uint8_t>(orig::fn_0044e600()) && !static_cast<uint8_t>(orig::fn_00454780(ebp)) &&
        !static_cast<uint8_t>(orig::fn_00426630(ebp)) && !static_cast<uint8_t>(orig::fn_00426610(ebp)) &&
        !static_cast<uint8_t>(orig::fn_004539e0(ebp)))
    {
        t = Fl(ebp + 0xb34) - Fl(SP(0x20));
        Fl(ebp + 0xb34) = t;
        if (t <= K(0x0056c05c))
        {
            Fl(ebp + 0xb34) = orig::fn_0046e2c0(0 /* push ecx: slot only */, 0x40400000u, 0x40a00000u);
            VC(ebp, 0x54, SP(0x5c));
            esi = GameP();
            cl = true;
            dl = true;
            if (Fl(SP(0x5c)) < Fl(esi + 0xbd0) || Fl(SP(0x60)) < Fl(esi + 0xbd4) || Fl(SP(0x5c)) > Fl(esi + 0xbd8) ||
                Fl(SP(0x60)) > Fl(esi + 0xbdc))
                cl = false;
            if (Fl(SP(0x5c)) < Fl(esi + 0xbe0) || Fl(SP(0x60)) < Fl(esi + 0xbe4) || Fl(SP(0x5c)) > Fl(esi + 0xbe8) ||
                Fl(SP(0x60)) > Fl(esi + 0xbec))
                dl = false;
            if (!cl && !dl)
            {
                edi = 0;
                VC(ebp, 0xd4, edi);
                esi = GameP();
                goto L_43f4ee;
            }
        }
    }
    esi = GameP();
    edi = 0;
L_43f4ee:
    switch (D(ebp + 0x460))
    {
    case 0x7f: goto L_43f51f;
    case 0x7e: goto L_43f575;
    case 0x78: goto L_43f5df;
    case 0x7a: goto L_43f5ef;
    case 0x7b: goto L_43f68c;
    case 0x7d: goto L_43f8a0;
    case 0x7c: goto L_43fe5b;
    case 0x2: goto L_43fe90;
    case 0x10: goto L_43ff19;
    case 0x12: goto L_440003;
    case 0x14: goto L_440010;
    case 0x11: goto L_440031;
    case 0xa: goto L_4400da;
    case 0xb: goto L_440175;
    case 0xc: case 0xd: case 0xe: goto L_4402c7;
    case 0xf: goto L_440656;
    case 0x29: goto L_440755;
    case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x28: goto L_4407de;
    case 0x33: goto L_440c9b;
    case 0x32: goto L_440e06;
    case 0x34: goto L_440fa9;
    case 0x3d: goto L_441340;
    case 0x3e: goto L_44136e;
    case 0x3c: goto L_44147b;
    case 0xb4: goto L_4418f7;
    case 0x82: goto L_4419a9;
    case 0x8c: goto L_441c1d;
    case 0x96: goto L_441e33;
    case 0x97: goto L_44205c;
    case 0x98: goto L_4422f7;
    case 0x50: goto L_442371;
    case 0x5a: goto L_442407;
    case 0x5b: goto L_4425a9;
    case 0x67: goto L_4425fe;
    case 0x64: goto L_44261a;
    case 0x65: goto L_44280a;
    case 0x6e: goto L_4428a2;
    case 0xaa: case 0xac: goto L_4429da;
    case 0xae: goto L_442a2f;
    case 0xab: goto L_442a4a;
    case 0xad: goto L_442a63;
    case 0xa0: case 0xa1: goto L_442b0c;
    case 0xa2: goto L_442c3d;
    case 0xa3: goto L_442cf9;
    case 0xa5: case 0xa6: goto L_442e93;
    case 0xa4: goto L_442fc6;
    case 0xb8: goto L_443057;
    case 0xc1: goto L_44319c;
    case 0xb6: case 0xbc: goto L_44330a;
    case 0xb5: goto L_443325;
    case 0xc5: goto L_443409;
    case 0xbd: goto L_443452;
    case 0xbf: goto L_443519;
    case 0xc0: goto L_44356a;
    case 0xbe: goto L_4435ae;
    case 0xc4: goto L_443651;
    case 0xc3: goto L_4437d0;
    case 0xba: goto L_4437f7;
    case 0xb9: goto L_443878;
    case 0xc8: goto L_443a28;
    case 0xd2: goto L_443ba1;
    case 0x118: goto L_443c4a;
    case 0x1: goto L_443f92;
    case 0x79: case 0xdd: goto L_444020;
    case 0x10e: goto L_444246;
    case 0x10f: goto L_444285;
    case 0x110: goto L_444373;
    case 0x104: goto L_4443c1;
    case 0xfa: goto L_44441f;
    case 0xe9: case 0xeb: goto L_4444b3;
    case 0xe7: goto L_444530;
    case 0xea: goto L_4446d3;
    case 0xe8: goto L_44474e;
    case 0xf1: goto L_4448b6;
    case 0xf2: goto L_4449c4;
    case 0xf3: goto L_444ad6;
    case 0xf0: goto L_444d0b;
    case 0xf4: goto L_445045;
    case 0xdc: goto L_44509f;
    case 0x119: goto L_445204;
    case 0x13: goto L_445210;
    case 0x11a: goto L_445240;
    case 0x19c: goto L_445280;
    case 0x19d: goto L_445379;
    case 0x19b: goto L_445538;
    case 0x12c: goto L_44560b;
    case 0x12d: goto L_4456f4;
    case 0x12e: goto L_445759;
    case 0x12f: goto L_4457e3;
    case 0x132: goto L_4459a1;
    case 0x195: case 0x197: goto L_445a6d;
    case 0x130: case 0x131: goto L_445bae;
    case 0x191: goto L_445d09;
    case 0x194: goto L_445d44;
    case 0x192: goto L_445d63;
    case 0x193: goto L_445da9;
    case 0x190: goto L_445e2e;
    case 0x1f4: goto L_445f2d;
    case 0xca: case 0xde: goto L_446093;
    default: goto done;
    }

    // ---- state 0x7f
L_43f51f:
    edi = D(ebp + 0x488) | 0x200000;
    D(ebp + 0x488) = edi;
    VC(ebp, 0x19c, 4u, 1u, 0u, 0u, 0u, 0u);
    orig::fn_0042df70(ebp, 0x41700000u /* 15.0 */);
    if (D(ebp + 0x484) & 0x4000)
        goto done;
    VC(ebp, 0x1b8, 0x7eu);
    goto done;

    // ---- state 0x7e
L_43f575:
    VC(ebp, 0xf8, 0x170001u, 0u);
    D(SP(0x14)) = 0;
    D(ebp + 0x488) |= 0x200000;
    D(SP(0x18)) = 0;
    D(SP(0x1c)) = 0;
    D(ebp + 0x39c) = orig::fn_00455a00(D(ebp + 0x39c));
    D(ebp + 0x398) = orig::fn_00455a00(D(ebp + 0x398));
    orig::fn_00458580(SP(0x14), ebp);
    orig::fn_00458510(SP(0x14), ebp);
    // ---- state 0x78
L_43f5df:
    orig::fn_0042d0f0(ebp, 0 /* push ecx: slot only */, 0, 1);
    goto done;

    // ---- state 0x7a
L_43f5ef:
    if (static_cast<uint8_t>(orig::fn_00454bc0(ebp)))
        goto L_444020;
    eax = D(ebp + 0x488);
    if (static_cast<int8_t>(eax & 0xff) < 0 || (eax & 0x200000) || D(ebp + 0x9bc))
        goto L_444020;
    D(ebp + 0x580) = 0x3f000000;  // 0.5
    D(ebp + 0x584) = 0x3f000000;
    D(ebp + 0x4b0) = 0xf;
    D(ebp + 0x4b4) = 0xf;
    D(ebp + 0x4ac) = D(ebp + 0x460);
    D(ebp + 0x890) = 0x40a00000;  // 5.0
    VC(ebp, 0x19c, 0x61u, 1u, 0u, 0u, 0u, 0u);
    VC(ebp, 0x1b8, 0x82u);
    goto done;

    // ---- state 0x7b
L_43f68c:
    ecx = D(ebp + 0x898);
    if (!ecx || !VB(ecx, 0xd8) || VB(D(ebp + 0x898), 0x14c))
        goto L_43f880;
    orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    orig::fn_004539f0(0x005b2868, D(ebp + 0x898), ebp, 0, 1, 4);
    D(SP(0x14)) = 0;
    D(SP(0x18)) = 0;
    D(SP(0x1c)) = 0;
    D(ebp + 0x39c) = orig::fn_00455a00(D(ebp + 0x39c));
    D(ebp + 0x398) = orig::fn_00455a00(D(ebp + 0x398));
    orig::fn_00458580(SP(0x14), ebp);
    orig::fn_00458510(SP(0x14), ebp);
    orig::fn_0042d0f0(ebp, 0 /* push ecx: slot only */, 0, 1);
    orig::fn_004577b0(ebp, 1);
    r = orig::fn_004324d0(0, ebp, D(ebp + 0x898), 1);
    orig::fn_00429a40(r & 0xffffff00u /* xor al, al */, ebp);
    D(ebp + 0x484) |= 0x8000;
    VC(D(ebp + 0x898), 0x160, ebp);
    ClearRef(ebp + 0xae0);
    eax = D(ebp + 0x898);
    D(ebp + 0xae0) = eax;
    ++D(eax + 0x28);
    D(ebp + 0x484) &= ~4u;
    orig::fn_00427600(ebp);
    VC(ebp, 0x1a0, 0x89u, 0x8au, 0x87u, 0x88u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    esi = D(ebp + 0x898);
    ebx = 0;
    orig::fn_00408c70(0x81, 0x005b6908, ebx, esi, ebp, 0x005b691c, 0x005b6930, 0x005b6944, 0x3f000000, 0x3f000000,
                      0x3e800000);
    Fl(ebp + 0x494) = orig::fn_0046e2c0(D(ebp + 0x8c4), D(ebp + 0x8c4), D(ebp + 0x8c8));
    D(ebp + 0x490) = ebx;
    VC(ebp, 0x1b8, 0x7du);
    goto done;
L_43f880:
    orig::fn_004266b0(ebp);
L_43f887:
    VC(ebp, 0x18c, 0u);
L_43f894:
    orig::fn_0042b8e0(0 /* ecx: not read */, ebp);
    goto done;

    // ---- state 0x7d: the target object +0x898 of type 1 or 2 (its virtual +0x168).
L_43f8a0:
    ecx = D(ebp + 0x898);
    if (!ecx)
        goto done;
    ebx = VC(ecx, 0x168);
    eax = D(ebp + 0x588);
    edi = ebp + 0x588;
    D(SP(0x20)) = ebx;
    D(SP(0x98)) = eax;
    if (eax)
    {
        if (VB(ebp, 0x180, eax, fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))) &&
            static_cast<uint8_t>(orig::fn_0043ec60(ebp, D(edi))))
            goto L_43f934;
        Unref(D(edi));
        D(edi) = 0;
        D(ebp + 0x490) = 0;
        D(ebp + 0x48c) &= ~1u;
    }
L_43f934:
    if (ebx == 1)
        cl = VB(ebp, 0x190, 0x42820000u /* 65.0 */);
    else
        cl = VB(ebp, 0x190, 0x425c0000u /* 55.0 */);
    if (!cl)
        D(ebp + 0x490) = 0;
    D(SP(0x10)) = D(0x00604a90);
    t = Fl(ebp + 0x490) - Fl(SP(0x10));
    Fl(ebp + 0x490) = t;
    if (t <= K(0x0056c05c))
    {
        Fl(SP(0x10)) = Fl(0x00604a90) + Fl(SP(0x10));
        esi = D(edi);
        ebx = esi;
        if (ebx)
        {
            Unref(ebx);
            D(edi) = 0;
        }
        eax = orig::fn_00426920(ebp, D(ebp + 0x5dc), 0, 0);
        if (eax)
            VC(ebp, 0x23c, eax, 1u);
        else if (esi)
        {
            D(edi) = esi;
            ++D(esi + 0x28);
        }
    }
    // 0x0043f9f6
    D(SP(0x3c)) = D(ebp + 0x498);
    D(SP(0x40)) = 1;
    if (D(SP(0x98)) != D(edi))
    {
        D(ebp + 0x48c) &= ~1u;
        orig::fn_00408820(D(SP(0x98)) /* push ecx: slot only */, ebp, 0x3f000000);
        orig::fn_00408820(0, D(ebp + 0x898), 0x3f000000);
    }
    esi = D(SP(0x20));
    if (esi == 1)
        cl = VB(ebp, 0x190, 0x42820000u);
    else
        cl = VB(ebp, 0x190, 0x425c0000u);
    if (By(ebp + 0x48c) & 1)
    {
        if (D(ebp + 0x5bc) || D(ebp + 0x5b8) || D(ebp + 0x5b4))
            goto L_43fa98;
    }
    // 0x0043fb1a
    if (cl || esi != 1 || (D(ebp + 0x484) & 0x200))
        goto L_43fa9a;
    ecx = D(ebp + 0x898);
    if (!ecx || !VB(ecx, 0x154))
        goto done;
    orig::fn_00453c50(D(ebp + 0x898), 0x005b27f4, SP(0x3f4), esi, 0, 0, esi, 0, 0);
    D(SP(0x14)) = D(SP(0x400));
    D(SP(0x18)) = D(SP(0x410));
    D(SP(0x1c)) = D(SP(0x420));
    r = orig::fn_004266b0(ebp);
    edi = D(GameP() + 0xaa4);
    orig::fn_0046f5b0(r & 0xffffff00u /* xor al, al */, 0, SP(0x14), ebp, edi, 0, 0);
    orig::fn_00427210(ebp);
    VC(ebp, 0x1b8, 0x7cu);
    goto done;
L_43fa98:
    cl = true;
L_43fa9a:
    if (!(Fl(ebp + 0x494) <= K(0x0056c05c)) || !D(edi) || !cl)
        goto L_43fc69;
    D(ebp + 0x484) &= ~0x200u;
    if (esi == 1)
        r = orig::fn_004292f0(0x85, 0x89, ebp, 0x86, 0x83, 0x84, 0x8a, 0x87, 0x88, D(ebp + 0x580), D(ebp + 0x584));
    else
        r = orig::fn_004292f0(0x6a, 0x69, ebp, 0x6a, 0x6a, 0x6a, 0x69, 0x69, 0x69, D(ebp + 0x580), D(ebp + 0x584));
    ebx = r & 0xff;  // mov bl, al
    if (!D(ebp + 0x898))
        goto done;
    if (I(ebp + 0x5b4) > 0)
        goto L_43fd07;
    if (D(SP(0x20)) == 1)
    {
        Fl(ebp + 0x494) = orig::fn_0046e2c0(0 /* push ecx: slot only */, D(ebp + 0x8c4), D(ebp + 0x8c8));
        goto L_43fd0e;
    }
    Fl(ebp + 0x494) = orig::fn_0046e2c0(D(ebp + 0x8f8), D(ebp + 0x8f4), D(ebp + 0x8f8));
    goto L_43fd07;
L_43fc69:
    ebx = 0;  // xor bl, bl
    t = Fl(ebp + 0x494) - Fl(SP(0x10));
    Fl(ebp + 0x494) = t;
    if (t <= K(0x0056c05c))
    {
        if (esi == 1 && !D(ebp + 0x5bc) && !D(ebp + 0x5b8) && !D(ebp + 0x5b4))
            D(ebp + 0x48c) |= esi;
        orig::fn_00427600(ebp);
        esi = D(SP(0x20));
    }
    if (esi == 1)
    {
        VC(ebp, 0x1a0, 0x89u, 0x8au, 0x87u, 0x88u, esi, 0u, D(ebp + 0x580), D(ebp + 0x584));
        goto L_43fd0e;
    }
    VC(ebp, 0x19c, 0x69u, 1u, 0u, 0u, 0u, 0u);
L_43fd07:
    if (D(SP(0x20)) != 1)
        goto L_43fd86;
L_43fd0e:
    eax = (D(SP(0x3c)) == D(ebp + 0x498)) ? D(SP(0x40)) : 0x81u;
    esi = D(ebp + 0x898);
    if (ebx & 0xff)
        orig::fn_00408c70(eax, 0x005b2818, 0, esi, ebp, 0x005b282c, 0x005b2840, 0x005b2854, D(ebp + 0x580),
                          D(ebp + 0x584), 0);
    else
        orig::fn_00408c70(eax, 0x005b6908, 0, esi, ebp, 0x005b691c, 0x005b6930, 0x005b6944, D(ebp + 0x580),
                          D(ebp + 0x584), 0x3e800000);
    goto done;
L_43fd86:
    VC(D(ebp + 0x898), 0x16c, SP(0x190), SP(0x68), SP(0x1a0), SP(0x6c));
    {
        float a2 = Fl(SP(0x6c)) * Fl(ebp + 0x584);
        a2 = a2 + a2;
        a2 = -(a2 - Fl(SP(0x6c)));
        float a1 = Fl(SP(0x68)) * Fl(ebp + 0x580);
        a1 = a1 + a1;
        a1 = -(a1 - Fl(SP(0x68)));
        VC(D(ebp + 0x898), 0x170, fb(a1), fb(a2));
    }
    if (D(SP(0x3c)) == D(ebp + 0x498))
        goto done;
    if (ebx & 0xff)
    {
        orig::fn_00408d90(0xffffffffu, D(ebp + 0x898), 0x005b2804, 0x80, 0, 0, 0, 0);
        goto done;
    }
    ecx = D(ebp + 0x898);
    eax = orig::fn_00408800(4, ecx);
    if (!(By(eax + 0x1c) & 2))
        goto done;
    orig::fn_00408e30(0x81, ecx, 1, 0, 0, 0, 0, 0);
    goto done;

    // ---- state 0x7c (esi = game)
L_43fe5b:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    D(ebp + 0x484) &= ~4u;
    goto L_441e5c;

    // ---- state 0x02
L_43fe90:
    if (VB(ebp, 0x22c, D(SP(0x20)), 0u, 0u))
        goto done;
    t = Fl(ebp + 0x618) - Fl(SP(0x20));
    Fl(ebp + 0x618) = t;
    if (!(t < K(0x0056c05c)))
        goto L_443fb1;
    VC(ebp, 0x19c, 0xau, 1u, 0u, 0u, 0u, 0u);
    orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), ebp + 0x4c0, 0, 0, 0, 0, 0);
    VC(ebp, 0x1b8, 0x50u);
    goto done;

    // ---- state 0x10: faces the target +0x588 while it is in range (virtual +0x180).
L_43ff19:
    esi = ebp + 0x588;
    if (!VB(ebp, 0x180, D(ebp + 0x588), fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
        goto L_44019d;
    orig::fn_00454c90(ebp, 1, 1);
    VC(ebp, 0x54, SP(0x178));
    VC(D(esi), 0x54, SP(0x234));
    esi = ebp + 0x51c;
    Fl(esi) = Fl(SP(0x234)) - Fl(SP(0x178));
    Fl(esi + 4) = Fl(SP(0x238)) - Fl(SP(0x17c));
    orig::fn_004e7780(esi, esi);
    D(ebp + 0x524) = 0;
    orig::fn_00454e60(esi, ebp, 0x41800000 /* 16.0 */);
    if (!By(SP(0x10)))
        goto done;
    eax = D(ebp + 0x484);
    if (eax & 0x2000000)
        goto L_4429d0;
    if (!(eax & 4))
        goto L_43f894;
    if (static_cast<int32_t>(orig::fn_004e7730(0x64)) < 0x32)
        goto L_43f894;
    VC(ebp, 0x230, 0u);
    goto done;

    // ---- state 0x12
L_440003:
    orig::fn_0042d1b0(D(ebp + 0x55c), D(ebp + 0x558), ebp, 1, 0, 0, 0);
    goto done;

    // ---- state 0x14
L_440010:
    orig::fn_0042d430(ebp, fb(static_cast<float>(I(ebp + 0x558))), fb(static_cast<float>(I(ebp + 0x55c))));
    goto done;

    // ---- state 0x11
L_440031:
    orig::fn_00454c90(ebp, 1, 1);
    if (static_cast<uint8_t>(orig::fn_00429a50(ebp)))
        goto done;
    if (D(ebp + 0x588))
    {
        VC(ebp, 0x54, SP(0x1e0));
        VC(D(ebp + 0x588), 0x54, SP(0x184));
        esi = ebp + 0x51c;
        Fl(esi) = Fl(SP(0x184)) - Fl(SP(0x1e0));
        Fl(esi + 4) = Fl(SP(0x188)) - Fl(SP(0x1e4));
        orig::fn_004e7780(esi, esi);
        D(ebp + 0x524) = 0;
        orig::fn_00454e60(esi, ebp, 0x41800000);
    }
    if (!By(SP(0x10)))
        goto done;
    orig::fn_004271c0(ebp);
    goto done;

    // ---- state 0x0a
L_4400da:
    edi = ebp + 0x588;
    if (VB(ebp, 0x180, D(ebp + 0x588), fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
    {
        orig::fn_00454c90(ebp, 1, 1);
        VC(ebp, 0x198, 1u, 1u, 1u, 0u, 1u);
        goto done;
    }
    if (D(ebp + 0x488) & 0x20000)
        orig::fn_0042bb10(ebp);
    esi = D(edi);
    if (esi)
    {
        Unref(esi);
        D(edi) = 0;
    }
    orig::fn_0042bc60(esi, ebp, 0, 1);
    goto done;

    // ---- state 0x0b
L_440175:
    esi = ebp + 0x588;
    if (!VB(ebp, 0x180, D(ebp + 0x588), fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
    {
    L_44019d:  // (also state 0x10, esi = self + 0x588)
        edi = D(esi);
        if (edi)
        {
            Unref(edi);
            D(esi) = 0;
        }
        orig::fn_0042bc60(edi, ebp, 0, 1);
        goto done;
    }
    orig::fn_00454c90(ebp, 1, 1);
    VC(ebp, 0x54, SP(0x9c));
    VC(D(esi), 0x54, SP(0xc0));
    esi = ebp + 0x51c;
    Fl(esi) = Fl(SP(0xc0)) - Fl(SP(0x9c));
    Fl(esi + 4) = Fl(SP(0xc4)) - Fl(SP(0xa0));
    orig::fn_004e7780(esi, esi);
    D(ebp + 0x524) = 0;
    orig::fn_00454e60(esi, ebp, 0x41800000);
    if (!By(SP(0x10)))
        goto done;
    Fl(SP(0x3c)) = Fl(ebp + 0x600);
    t = Dist3Sq(SP(0x9c), SP(0xc0));
    if (!(Fl(SP(0x3c)) * Fl(SP(0x3c)) <= t))
        goto done;
    if (!(D(ebp + 0x484) & 0x20000))
        goto L_4429c5;
L_44029b:
    VC(ebp, 0x19c, 0x66u, 0u, 0u, 0u, 0u, 0u);
    VC(ebp, 0x1b8, 0xa2u);
    goto done;

    // ---- states 0x0c, 0x0d, 0x0e: the partner object +0xab0.
L_4402c7:
    esi = D(ebp + 0xab0);
    edi = ebp + 0xab0;
    if (!esi || static_cast<uint8_t>(orig::fn_00454780(esi)))
        goto L_44058d;
    if (D(ebp + 0x460) != 0xe)
        goto L_4403cc;
    orig::fn_00453c50(D(edi), 0x005b68ec, SP(0x274), 1, 0, 0, 1, 0, 0);
    Fl(SP(0x40)) = -Fl(SP(0x278));
    D(SP(0x28)) = D(SP(0x290));
    Fl(SP(0x44)) = -Fl(SP(0x288));
    D(SP(0x24)) = D(SP(0x280));
    Fl(SP(0x48)) = -Fl(SP(0x298));
    D(SP(0x2c)) = D(SP(0x2a0));
    VC(ebp, 0x54, SP(0xb4));
    edi = ebp;
    if (Dist2Sq(SP(0x24), SP(0xb4)) > K(0x005bcb2c))
    {
        orig::fn_00454d80(SP(0x24), ebp, D(ebp + 0x948), D(ebp + 0x948), 0x40a00000, D(ebp + 0x944));
        goto done;
    }
    if (static_cast<uint8_t>(orig::fn_00454e60(SP(0x40), ebp, 0x41800000)))
        goto L_440531;
    goto done;
L_4403cc:
    orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);  // its argument is the push 0 at 0x004402ef
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    D(ebp + 0x39c) = orig::fn_00455a00(D(ebp + 0x39c));
    D(SP(0x14)) = 0;
    D(SP(0x18)) = 0;
    D(SP(0x1c)) = 0;
    orig::fn_00458510(SP(0x14), ebp);
    orig::fn_00453c50(D(edi), 0x005b68ec, SP(0x274), 1, 0, 0, 1, 0, 0);
    Fl(SP(0x40)) = -Fl(SP(0x278));
    D(SP(0x28)) = D(SP(0x290));
    Fl(SP(0x44)) = -Fl(SP(0x288));
    D(SP(0x24)) = D(SP(0x280));
    Fl(SP(0x48)) = -Fl(SP(0x298));
    D(SP(0x2c)) = D(SP(0x2a0));
    VC(ebp, 0x54, SP(0xb4));
    if (!(Dist3Sq(SP(0x24), SP(0xb4)) > K(0x005bcb2c)))
        goto L_440519;
    if (D(ebp + 0x460) == 0xc)
    {
        eax = GameP();
        ecx = D(eax + 0xaa4);
        // al, cl, dl = 0 (the callee pushes the three registers as byte flags)
        orig::fn_0046f6b0(eax & 0xffffff00u, ecx & 0xffffff00u, SP(0x40) & 0xffffff00u, SP(0x24), ecx, ebp, SP(0x40));
        VC(ebp, 0xf8, 0x120000u, 0u);
        VC(ebp, 0x1b8, 0xdu);
        goto done;
    }
    VC(ebp, 0x1b8, 0xeu);
    goto done;
L_440519:
    if (!static_cast<uint8_t>(orig::fn_00454e60(SP(0x40), ebp, 0x41800000)))
        goto done;
L_440531:
    edi = ebp + 0x398;
    D(edi) = orig::fn_00455a00(D(ebp + 0x398));
    orig::fn_00454cc0(ebp, edi);
    VC(ebp, 0xf8, 0x120000u, 0u);
    edi = VC(ebp, 0x138);
    orig::fn_0042c670(D(ebp + 0xab0), ebp, 0x32, edi);
    orig::fn_0042c670(ebp, D(ebp + 0xab0), 0x65, edi);
    goto done;
L_44058d:
    orig::fn_00454c90(ebp, 1, 1);
    esi = D(edi);
    if (esi)
    {
        Unref(esi);
        D(edi) = 0;
        D(ebp + 0x484) &= 0xffffdfffu;
    }
    orig::fn_0042df70(ebp, 0x42340000 /* 45.0 */);
    eax = D(ebp + 0x484);
    if (!(eax & 4))
        goto L_440a22;
    if (eax & 0x20000)
        goto L_44029b;
    if (eax & 0x2000000)
        goto L_4429d0;
    orig::fn_00428280(SP(0x30), ebp, ebp, 0x005b68fc);
    esi = D(ebp + 0x588);
    if (!esi || static_cast<uint8_t>(orig::fn_00454780(esi)))
        goto L_43f894;
    if (!static_cast<uint8_t>(orig::fn_00426fd0(SP(0x30), ebp, D(ebp + 0x588), 0)))
        goto L_43f894;
    VC(ebp, 0x230, 0u);
    goto done;

    // ---- state 0x0f
L_440656:
    orig::fn_00454cc0(ebp, ebp + 0x398);
    esi = D(ebp + 0xab0);
    edi = ebp + 0xab0;
    if (esi && !static_cast<uint8_t>(orig::fn_00454780(esi)))
    {
        ecx = D(edi);
        eax = D(ecx + 0xab0);
        if (eax && eax == ebp)
        {
            ecx = D(ecx + 0x460);
            if (ecx >= 0xc && ecx <= 0xe)
                goto done;
        }
    }
    ClearRef(edi);
    D(ebp + 0x484) &= 0xffffdfffu;
    if (static_cast<uint8_t>(orig::fn_00454780(ebp)))
        goto done;
    esi = D(ebp + 0x588);
    edi = ebp + 0x588;
    if (esi)
        Unref(esi);
    D(edi) = 0;
    orig::fn_004577b0(ebp, 0);
    orig::fn_0042bc60(esi, ebp, 0, 1);
    goto done;

    // ---- state 0x29 (esi = game, ebx = self + 0xae0)
L_440755:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    ClearRef(ebx);
    eax = D(ebp + 0x904);
    if (eax)
    {
        D(ebx) = eax;
        ++D(eax + 0x28);
    }
    // virtual +0x1a4 takes one argument (0x8b, ret 4); the push 0x7f outlives it and is +0x238's second argument
    eax = VC(ebp, 0x1a4, 0x8bu);
    VC(ebp, 0x238, eax, 0x7fu);
    orig::fn_0042c380(ebp);
    goto done;

    // ---- states 0x1e, 0x1f, 0x20, 0x21, 0x28: the vehicle/mount object +0x904 (guess) and the target +0x588.
L_4407de:
    ebx = ebp + 0x588;
    if (D(ebp + 0x904) && VB(ebp, 0x180, D(ebp + 0x588), fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
        goto L_440876;
    esi = D(ebp + 0x588);
    if (esi)
    {
        Unref(esi);
        D(ebx) = 0;
    }
    if (D(ebp + 0x904) && (D(ebp + 0x484) & 0x200))
        orig::fn_0042bc60(esi, ebp, 1, 0);
    else
    {
        VC(ebp, 0x18c, 0u);
        orig::fn_0042bc60(esi, ebp, 1, 1);
    }
L_440876:
    ecx = D(ebp + 0x904);
    if (!ecx)
        goto done;
    if (!D(ebx) && !(D(ebp + 0x484) & 0x200))
        goto done;
    if (!VB(ecx, 0xd8))
        goto L_43f887;
    if (!D(ebx))
        goto L_440a59;
    orig::fn_00428280(SP(0x14), ebp, ebp, 0x005b68fc);
    VC(D(ebx), 0x54, SP(0x160));
    Fl(ebp + 0x490) = Fl(ebp + 0x490) - Fl(SP(0x20));
    t = Fl(ebp + 0x494) - Fl(SP(0x20));
    Fl(SP(0x40)) = t;
    Fl(ebp + 0x494) = t;
    Fl(SP(0x3c)) = Dist2Sq(SP(0x160), SP(0x14));
    if (Fl(ebp + 0x490) <= K(0x0056c05c) && D(ebp + 0x460) != 0x21 && D(ebp + 0x460) != 0x28)
        goto L_440956;
    if (!(Fl(SP(0x40)) < K(0x0056c05c)))
        goto L_440a7d;
    if (!(Fl(SP(0x3c)) < K(0x005bccb8)))
        goto L_440a7d;
L_440956:
    Fl(SP(0x10)) = Fl(ebp + 0x5d8);
    t = Dist3Sq(SP(0x160), SP(0x14));
    if (!(Fl(SP(0x10)) * Fl(SP(0x10)) > t))
        goto L_440a7d;
    Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3f400000 /* 0.75 */, 0x3fa00000 /* 1.25 */);
    if (!static_cast<uint8_t>(orig::fn_00426fd0(SP(0x14), ebp, D(ebp + 0x588), 0)))
        goto L_440a7d;
    if (By(ebp + 0x484) & 4)
    {
        VC(ebp, 0x18c, 0u);
    L_4409cf:
        VC(ebp, 0x230, 1u);
        goto done;
    }
    if (!(Fl(ebp + 0x494) < K(0x0056c05c)))
        goto L_440a7d;
    if (!(Fl(SP(0x3c)) < K(0x005bccb8)))
        goto L_440a7d;
    VC(ebp, 0x18c, 0u);
    orig::fn_0042df70(ebp, 0x42340000 /* 45.0 */);
L_440a22:
    VC(ebp, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(ebp + 0x580), D(ebp + 0x584));
    VC(ebp, 0x1b8, 0x32u);
    goto done;
L_440a59:
    eax = orig::fn_00426920(ebp, D(ebp + 0x5dc), 0, 0);
    if (eax)
        VC(ebp, 0x23c, eax, 1u);
L_440a7d:
    orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    if (D(ebp + 0x460) == 0x1f)
    {
        orig::fn_00454c90(ebp, 1, 1);
        r = VC(D(ebp + 0x904), 0x150, SP(0x30c), ebp + 0x908, D(ebp + 0x914), ebp, D(ebp + 0x588), 0u);
        ecx = GameP();
        edx = D(ecx + 0xaa4);
        orig::fn_0046f6b0(r & 0xffffff00u, ecx & 0xffffff00u, edx & 0xffffff00u, SP(0x30c), edx, ebp, ebp + 0x908);
        VC(ebp, 0x1b8, 0x20u);
    }
    if (D(ebp + 0x460) == 0x28)
    {
        VC(ebp, 0xf8, 0x20000u, 0u);
        VC(D(ebp + 0x904), 0x150, SP(0x2f4), ebp + 0x908, D(ebp + 0x914), ebp, D(ebp + 0x588), 0u);
        eax = GameP();
        ecx = D(eax + 0xaa4);
        // dl = 1: the upper bytes of edx are whatever the virtual call left; the callee passes it on as a flag.
        orig::fn_0046f6b0(eax & 0xffffff00u, ecx & 0xffffff00u, 1, SP(0x2f4), ecx, ebp, ebp + 0x908);
        VC(ebp, 0x1b8, 0x29u);
        goto done;
    }
    orig::fn_00454c90(ebp, 1, 1);
    if (By(ebp + 0x484) & 4)
        goto L_440c3b;
    if (VB(ebp, 0x1d0))
    {
        VC(ebp, 0x19c, 0x2cu, 0u, 0u, 0u, 0u, 0u);
        VC(ebp, 0x1b8, 0x33u);
        goto done;
    }
    orig::fn_00408820(0 /* push ecx: slot only */, ebp, 0x3f000000);
    D(ebp + 0x580) = 0x3f000000;
    D(ebp + 0x584) = 0x3f000000;
    VC(ebp, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(ebp + 0x580), 0x3f000000u);
    VC(ebp, 0x1b8, 0x33u);
    goto done;
L_440c3b:
    Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3f800000 /* 1.0 */, 0x40800000 /* 4.0 */);
    Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x3f400000 /* 0.75 */, 0x3fa00000 /* 1.25 */);
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(ebp, 0x1b8, 0x34u);
    goto done;

    // ---- state 0x33
L_440c9b:
    ecx = D(ebp + 0x904);
    if (ecx && !VB(ecx, 0xd8))
        VC(ebp, 0x18c, 0u);
    ebx = ebp + 0x51c;
    D(ebx) = D(ebp + 0x908);
    D(ebx + 4) = D(ebp + 0x90c);
    D(ebx + 8) = D(ebp + 0x910);
    orig::fn_00454e60(ebx, ebp, 0x41800000);
    orig::fn_00454c90(ebp, 1, 1);
    ebx = 0;
    if (!By(SP(0x10)))
        goto done;
    orig::fn_00427600(ebp);
    if (D(ebp + 0x904) == ebx)
        goto L_440de8;
    Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3f800000, 0x40800000);
    Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x3f400000, 0x3fa00000);
    if (D(ebp + 0x484) & 0x200)
    {
        VC(ebp, 0x1a0, 2u, 3u, ebx, 1u, 1u, ebx, 0x3f000000u, 0x3f000000u);
        VC(ebp, 0x1b8, 0x34u);
        goto done;
    }
    if (VB(ebp, 0x1d0))
        VC(ebp, 0x19c, orig::fn_004e7740(0x2e, 0x2d), ebx, ebx, ebx, ebx, ebx);
    else
        VC(ebp, 0x19c, orig::fn_004e7740(0x1a, 0x17), 1u, ebx, ebx, ebx, ebx);
    VC(ebp, 0x1b8, 0x34u);
    goto done;
L_440de8:
    D(ebp + 0x490) = ebx;
    D(ebp + 0x494) = ebx;
    VC(ebp, 0x1b8, 0x34u);
    goto done;

    // ---- state 0x32
L_440e06:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_0042df70(ebp, 0x42340000 /* 45.0 */);
    VC(ebp, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(ebp + 0x580), D(ebp + 0x584));
    if (!By(SP(0x10)))
        goto done;
    orig::fn_00427600(ebp);
    eax = D(ebp + 0x484);
    if (eax & 0x20000)
        goto L_4409cf;
    if (eax & 0x2000000)
        goto L_4429d0;
    ebx = ebp + 0x588;
    D(ebp + 0x490) = 0;
    if (!D(ebx))
    {
        orig::fn_0042bc60(0, ebp, 1, 1);
        goto done;
    }
    if (D(ebp + 0x858) && static_cast<int32_t>(orig::fn_004e7730(0x64)) < 0x19)
    {
        if (!static_cast<uint8_t>(orig::fn_00453290(D(ebx), SP(0x40))) || D(SP(0x40)) != D(ebp + 0x858))
            goto L_440fa2;
    }
    if (!VB(ebp, 0x180, D(ebx), fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
        goto L_440fa2;
    orig::fn_00428280(SP(0x30), ebp, ebp, 0x005b68fc);
    VC(D(ebx), 0x54, SP(0x2b8));
    if (!D(ebp + 0xb08))
    {
        Fl(SP(0x10)) = Fl(ebp + 0x5d8);
        t = Dist2Sq(SP(0x30), SP(0x2b8));
        if (Fl(SP(0x10)) * Fl(SP(0x10)) > t &&
            static_cast<uint8_t>(orig::fn_00426fd0(SP(0x30), ebp, D(ebx), 0)))
            goto L_445cf7;
    }
    if (D(ebp + 0x904))
        VC(ebp, 0x188);
    if (!D(ebp + 0xb08) && static_cast<int32_t>(orig::fn_004e7730(0x64)) >= 0x32)
        goto L_441e5c;
L_440f92:
    VC(ebp, 0x184);
    goto done;
L_440fa2:
    esi = D(ebx);
    goto L_4414ad;

    // ---- state 0x34
L_440fa9:
    if (!D(ebp + 0x904))
        D(ebp + 0x490) = 0;
    ebx = ebp + 0x51c;
    D(ebx) = D(ebp + 0x908);
    D(ebx + 4) = D(ebp + 0x90c);
    D(ebx + 8) = D(ebp + 0x910);
    orig::fn_00454e60(ebx, ebp, 0x41800000);
    orig::fn_00454c90(ebp, 1, 1);
    ebx = 0;
    t = Fl(ebp + 0x490) - Fl(SP(0x20));
    Fl(ebp + 0x490) = t;
    if (!(t <= K(0x0056c05c)))
    {
        if (!By(SP(0x10)) || !VB(ebp, 0x1d0) || D(ebp + 0x498) == 2)
            goto L_44120a;
    }
    if (D(ebp + 0x484) & 0x200)
        goto L_44120a;
    Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3f800000, 0x40800000);
    edi = ebp + 0x588;
    if (D(ebp + 0x588) == ebx)
        goto L_4411a9;
    if (D(ebp + 0x858) != ebx && static_cast<int32_t>(orig::fn_004e7730(0x64)) < 0x19)
    {
        if (!static_cast<uint8_t>(orig::fn_00453290(D(edi), SP(0x40))) || D(SP(0x40)) != D(ebp + 0x858))
            goto L_441162;
    }
    if (!VB(ebp, 0x180, D(edi), fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
        goto L_441162;
    if (D(ebp + 0x904) == ebx)
        goto L_445a5f;
    VC(ebp, 0x188);
    if ((By(ebp + 0x484) & 0x10) && I(ebp + 0x924) > 0)
    {
        --D(ebp + 0x924);
        VC(ebp, 0x240, 1u, ebx, ebx);
    }
    else
        VC(ebp, 0x184);
    if (By(ebp + 0x484) & 2)
    {
        Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3f81999a /* 1.0125 */, 0x3fd80000 /* 1.6875 */);
        goto done;
    }
    Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3f400000, 0x3fa00000);
    goto done;
L_441162:
    esi = D(edi);
    if (esi)
    {
        Unref(esi);
        D(edi) = 0;
    }
    if (!(D(ebp + 0x484) & 0x200))
        goto L_4412b8;
    orig::fn_0042bc60(esi, ebp, 0, 0);
    if (D(edi))
        goto done;
L_4411a9:
    if (D(ebp + 0x498) == 2 && VB(ebp, 0x1d0))
    {
        VC(ebp, 0x19c, orig::fn_004e7740(0x2e, 0x2d), ebx, ebx, ebx, ebx, ebx);
        goto done;
    }
    VC(ebp, 0x1a0, 2u, 3u, ebx, 1u, 1u, ebx, 0x3f000000u, 0x3f000000u);
    goto done;
L_44120a:
    ecx = D(ebp + 0x904);
    if (!VB(ecx, 0xd8))
    {
        D(ebp + 0x490) = ebx;
        D(ebp + 0x494) = ebx;
        orig::fn_00454c60(ebp);
    }
    t = Fl(ebp + 0x494) - Fl(SP(0x20));
    Fl(ebp + 0x494) = t;
    if (!(t < K(0x0056c05c)))
        goto done;
    edi = ebp + 0x588;
    if (!VB(ebp, 0x180, D(ebp + 0x588), fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
    {
        esi = D(edi);
        if (esi)
        {
            Unref(esi);
            D(edi) = 0;
        }
        if (D(ebp + 0x484) & 0x200)
        {
            orig::fn_0042bc60(esi, ebp, 0, 0);
            goto done;
        }
    L_4412b8:
        orig::fn_0042bc60(esi, ebp, 0, 1);
        goto done;
    }
    D(SP(0x24)) = D(ebp + 0x934);
    D(SP(0x28)) = D(ebp + 0x938);
    Fl(SP(0x2c)) = Fl(ebp + 0x93c) + K(0x005bcbdc);
    if (static_cast<uint8_t>(orig::fn_00426fd0(SP(0x24), ebp, D(edi), ebx)))
    {
        VC(ebp, 0x18c, ebx);
        VC(ebp, 0x184);
        D(ebp + 0x490) = ebx;
        goto done;
    }
    Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x3f400000, 0x3fa00000);
    goto done;

    // ---- state 0x3d (esi = game)
L_441340:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    goto L_442860;

    // ---- state 0x3e
L_44136e:
    if (!VB(ebp, 0x180, D(ebp + 0x588), fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
    {
        esi = D(ebp + 0xb54);
        if (!esi)
            goto done;
        Call_00481ab0(0, esi, 0);
        goto done;
    }
    VC(ebp, 0x54, SP(0x2d0));
    VC(D(ebp + 0x588), 0x54, SP(0x354));
    t = Dist3Sq(SP(0x354), SP(0x2d0));
    Fl(SP(0x40)) = t;
    if (t < K(0x005bcf8c))
        goto L_441435;  // the push 0 at 0x004413ec becomes 0x00481ab0's argument
    orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    if (!(Fl(SP(0x40)) < K(0x005bcf7c)))
        goto L_441447;
L_441435:
    Call_00481ab0(0, D(ebp + 0xb54), 0);
    goto done;
L_441447:
    VC(D(ebp + 0x588), 0x54, ebp + 0x60c);
    orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), ebp + 0x60c, 0, 0, 0, 0, 0);
    goto done;

    // ---- state 0x3c
L_44147b:
    ebx = ebp + 0x588;
    if (!VB(ebp, 0x180, D(ebp + 0x588), fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
    {
        esi = D(ebx);
        if (!esi)
            goto L_44324e;
    L_4414ad:  // (also entered from state 0x32 without the null test)
        Unref(esi);
        if (ebx)
            D(ebx) = 0;
        goto L_44324e;
    }
    t = Fl(ebp + 0x490) - Fl(SP(0x20));
    Fl(ebp + 0x490) = t;
    if (!(t <= K(0x0056c05c)))
        goto L_4416a9;
    orig::fn_00428280(SP(0x14), ebp, ebp, 0x005b68fc);
    orig::fn_00428280(SP(0x40), ebp, D(ebx), 0x005b68fc);
    D(SP(0x3c)) = 0;
    Fl(SP(0x24)) = Fl(SP(0x40)) - Fl(SP(0x14));
    Fl(SP(0x28)) = Fl(SP(0x44)) - Fl(SP(0x18));
    Fl(SP(0x2c)) = Fl(SP(0x48)) - Fl(SP(0x1c));
    Fl(SP(0x10)) = Normalize3(SP(0x24), SP(0x24));
    if (!VB(ebp, 0x1cc))
    {
        if (!(Fl(ebp + 0x5d8) * K(0x005bcc38) >= Fl(SP(0x10))))
            goto L_44166c;
    }
    r = orig::fn_0044e600();
    edi = D(SP(0x10));
    if (!static_cast<uint8_t>(r))
    {
        r = orig::fn_004137e0(SP(0x40), SP(0x24), D(GameP() + 0xa8c), SP(0x14), edi);
        By(SP(0x5b)) = static_cast<uint8_t>(r) ? 0 : 1;
        if (static_cast<uint8_t>(r))
            goto L_44166c;
    }
    if (static_cast<uint8_t>(orig::fn_0044e600()))
        goto L_441657;
    eax = GameP();
    if (!static_cast<uint8_t>(orig::fn_00414110(SP(0x14), SP(0x24), D(eax + 0xa8c), edi, SP(0x40), 0, SP(0x3c), 0,
                                                 D(ebp + 0x608), 0, D(D(eax + 0xa84) + 0xe9c), 0, 0, 0, 2)))
        goto L_441657;
    esi = D(SP(0x3c));
    if (D(ebx) == esi)
        goto L_441657;
    if (!VB(ebp, 0x1e0))
        goto L_44166c;
    if (orig::fn_00454c60(D(ebx)) != esi)
        goto L_44166c;
    if (!(By(ebp + 0x484) & 1))
        goto L_44166c;
    VC(ebp, 0x19c, 0x27u, 0u, 0u, 0u, 0u, 0u);
    VC(ebp, 0x1b8, 0x10u);
    goto done;
L_441657:
    if (Fl(SP(0x10)) < Fl(ebp + 0x5d8))
        goto L_445cf7;
L_44166c:
    if (By(ebp + 0x484) & 2)
        Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3e8a3d71 /* 0.27 */, 0x3f0a3d71 /* 0.54 */);
    else
        Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3e4ccccd /* 0.2 */, 0x3ecccccd /* 0.4 */);
    goto L_4416f2;
L_4416a9:
    VC(ebp, 0x54, SP(0x2e8));
    VC(D(ebx), 0x54, SP(0x33c));
    if (Dist2Sq(SP(0x33c), SP(0x2e8)) < K(0x005bccb8))
        D(ebp + 0x490) = 0;
L_4416f2:
    orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto L_4418a0;
    VC(ebp, 0x54, SP(0x324));
    if (Dist2Sq(ebp + 0x60c, SP(0x324)) > K(0x005bcb60))
        ebx = static_cast<uint32_t>(orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), ebp + 0x60c, 0, 0, 0, 0, 0));
    else
        ebx = 1;
    if (!(ebx & 0xd))
    {
        orig::fn_0046f440(D(GameP() + 0xaa4), ebp);
        ecx = D(ebp + 0x958);
        if (ecx && VC(ecx, 0x34))
        {
            eax = D(ebp + 0x958);
            ebx = 0;
            D(SP(0x40)) = 0;
            D(SP(0x44)) = 0;
            edi = orig::fn_004e7340(eax, SP(0x40));
            if (edi)
                orig::fn_004e7420(SP(0x40));
            ebx = static_cast<uint32_t>(orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), edi, 0, 0, 0, 0, 0));
            orig::fn_00424610(edi, ebp);
            eax = D(SP(0x44));
            if (eax)
                --D(eax + 0x18);
        }
    }
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto L_4418a0;
    if (VB(ebp, 0x240, 0u, 0u, 1u))
        goto L_4418a0;
    if (!ebx)
    {
        if (static_cast<uint8_t>(orig::fn_0044e600()))
            goto L_4418a0;
        if (D(ebp + 0x958))
            goto L_4418ae;
        orig::fn_0042d0f0(ebp, 0 /* push ecx: slot only */, ebx, 1);
        goto L_4418a0;
    }
    orig::fn_00454c90(ebp, 1, 1);
    Fl(ebp + 0x618) = orig::fn_0046e2c0(0, 0x40a00000 /* 5.0 */, 0x41700000 /* 15.0 */);
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(ebp, 0x1b8, 2u);
L_4418a0:
    if (!D(ebp + 0x958))
        goto done;
L_4418ae:
    VC(D(ebp + 0x588), 0x54, SP(0x210));
    if (!(Dist2Sq(ebp + 0x960, SP(0x210)) > K(0x005bcfac)))
        goto done;
    orig::fn_00424630(ebp, SP(0x210));
    goto done;

    // ---- state 0xb4: walks towards the point +0x934.
L_4418f7:
    VC(ebp, 0x54, SP(0x1b0));
    esi = ebp + 0x934;
    edi = ebp + 0x51c;
    Fl(edi) = Fl(esi) - Fl(SP(0x1b0));
    Fl(edi + 4) = Fl(esi + 4) - Fl(SP(0x1b4));
    orig::fn_004e7780(edi, edi);
    D(ebp + 0x524) = 0;
    r = orig::fn_00454e60(edi, ebp, 0x41800000);
    if (!static_cast<uint8_t>(r))
        goto done;
    edi = D(GameP() + 0xaa4);
    orig::fn_0046f5b0(r & 0xffffff00u /* xor al, al */, 0, esi, ebp, edi, 0, 0);
    Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3f800000 /* 1.0 */, 0x40000000 /* 2.0 */);
    orig::fn_00427210(ebp);
    VC(ebp, 0x1b8, 0xb6u);
    goto done;

    // ---- state 0x82
L_4419a9:
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (static_cast<uint8_t>(orig::fn_00454bc0(ebp)))
    {
        orig::fn_0046f480(ebp, D(GameP() + 0xaa4));
        eax = D(ebp + 0x4ac);
        By(SP(0x10)) = (eax == 0x119 || eax == 0x11a) ? 0 : 1;
        D(ebp + 0x890) = 0;
        eax = D(ebp + 0x4b0);
        ecx = D(ebp + 0x4b4);
        if (eax == ecx)
            VC(ebp, 0x19c, eax, D(SP(0x10)), 0u, 0u, 0u, 0u);
        else
            VC(ebp, 0x1a0, eax, ecx, D(ebp + 0x4b8), D(ebp + 0x4bc), D(SP(0x10)), 0u, 0x3f000000u, 0x3f000000u);
        VC(ebp, 0x1b8, D(ebp + 0x4ac));
        D(ebp + 0x48c) &= 0xffff7fffu;
        ecx = D(ebp + 0x894);
        if (!ecx)
            goto done;
        orig::fn_00408e30(0x80, ecx, 0xffffffffu, 0x005b27e4, 0, 0x3e800000, 0, 0);
        goto done;
    }
    if (static_cast<uint8_t>(orig::fn_00426630(ebp)))
        goto done;
    if (!(D(ebp + 0x48c) & 0x8000))
        goto L_441bea;
    orig::fn_00458000(ebp, SP(0x14));
    if (!(Fl(SP(0x1c)) < K(0x005bcfa8)))
        goto L_441bea;
    D(SP(0x28)) = D(SP(0x18));
    D(SP(0x24)) = D(SP(0x14));
    D(SP(0x2c)) = 0xc1c80000;  // -25.0
    {
        float a = orig::fn_004581c0(ebp);
        float b = orig::fn_004581c0(ebp) * K(0x005bcd40);
        Fl(SP(0x40)) = orig::fn_00471750(SP(0x24), SP(0x14), SP(0x30), 0, fb(b), fb(a));
    }
    D(ebp + 0x39c) = orig::fn_00455a30(SP(0x30), 0x10, ebp, D(ebp + 0x39c), D(SP(0x40)), 0xffffffffu, 0);
    if (D(ebp + 0x894))
        goto L_441bea;
    eax = orig::fn_0045f020(0, 0x005b3fa4, D(GameP() + 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
    D(ebp + 0x894) = eax;
    if (eax)
    {
        ++D(eax + 0x28);
        orig::fn_004539f0(0x005b27d4, ebp, D(ebp + 0x894), 0, 1, 0);
        orig::fn_00408e30(0x80, D(ebp + 0x894), 0xffffffffu, 0x005b27c4, 0, 0, 0, 0);
    }
    VC(ebp, 0x19c, 0x62u, 1u, 0u, 0u, 0u, 0u);
L_441bea:
    t = Fl(ebp + 0x890) - Fl(SP(0x20));
    Fl(ebp + 0x890) = t;
    if (!(t < K(0x0056c05c)))
        goto done;
    VC(ebp, 0xd4, 0u);
    goto done;

    // ---- state 0x8c
L_441c1d:
    esi = D(ebp + 0xadc);
    if (!esi || !static_cast<uint8_t>(orig::fn_00454780(esi)))
        goto L_441d86;
    std::memset(P(SP(0x12c)), 0, 0xd * 4);
    orig::fn_004577b0(ebp, 0);
    VC(ebp, 0x54, SP(0x1f8));
    VC(D(ebp + 0xadc), 0x54, SP(0x1c8));
    esi = ebp + 0xadc;
    Fl(SP(0x144)) = Fl(SP(0x1f8)) - Fl(SP(0x1c8));
    Fl(SP(0x148)) = Fl(SP(0x1fc)) - Fl(SP(0x1cc));
    orig::fn_004e7780(SP(0x144), SP(0x144));
    D(SP(0x13c)) = orig::fn_00458720(ebp, 0);
    Fl(SP(0x140)) = orig::fn_004555b0(ebp);
    Fl(SP(0x14c)) = orig::fn_0046e2c0(0, 0x3f333333 /* 0.7 */, 0x3f666666 /* 0.9 */);
    Fl(SP(0x150)) = orig::fn_0046e2c0(0, 0x46a94555, 0x47294555);
    Normalize3(SP(0x144), SP(0x144));  // fstp st(0)
    D(SP(0x12c)) = 0x83;
    D(SP(0x158)) = D(GameP() + 0x748);
    orig::fn_004324d0(0, ebp, D(esi), 1);
    Unref(D(esi));
    D(esi) = 0;
    VC(ebp, 0x17c, SP(0x12c), 0u);
    goto done;
L_441d86:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (By(ebp + 0x488) & 0x40)
    {
        esi = orig::fn_00426920(ebp, D(ebp + 0x5dc), 0, 0);
        if (esi)
        {
            VC(ebp, 0x23c, esi, 1u);
            orig::fn_0042f5c0(ebp, esi);
        }
    }
    t = Fl(ebp + 0x494) - Fl(SP(0x20));
    Fl(ebp + 0x494) = t;
    if (!By(SP(0x10)))
        goto done;  // 0x004460a0 pops st0
    if (!(By(ebp + 0x484) & 0x40) && !(t <= K(0x0056c05c)))
        goto done;
    orig::fn_00427330(ebp);
    Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x41700000 /* 15.0 */, 0x41c00000 /* 24.0 */);
    goto done;

    // ---- state 0x96 (esi = game): the use object +0x570 (guess).
L_441e33:
    eax = D(ebp + 0x488);
    if ((eax & 0x400) && !By(esi + 0xcbe))
    {
        ebx = eax & 0xfffffbffu;
        D(ebp + 0x488) = ebx;
        --D(esi + 0xc48);
    L_441e5c:
        VC(ebp, 0x240, 1u, 0u, 0u);
        goto done;
    }
    ecx = D(ebp + 0x570);
    edi = ebp + 0x570;
    if (!ecx)
    {
        if (D(ebp + 0x484) & 0x2000000)
            goto L_445c37;
        VC(ebp, 0x240, 0u, 0u, 0u);
        goto done;
    }
    ebx = 0;
    if (eax & 0x10000)
    {
        ebx = ebp;
        r = VC(D(ebp + 0xb78), 0x34);
        esi = GameP();
        if (r)
            ebx = 0;
    }
    if (orig::fn_0044b4b0(1 /* al = 1 */, D(edi), ebp) || ebx)
        goto L_441f5e;
    if (VB(ebp, 0x22c, D(SP(0x20)), ebx, 1u))
    {
        if (D(ebp + 0x488) & 0x18000)
            goto done;
        eax = D(ebp + 0x484);
        if (eax & 0x80000)
        {
            D(ebp + 0x484) = eax & 0xfff7ffffu;
            orig::fn_0044b7f0(D(edi), 0 /* cl = 0 */);
        }
        Unref(D(edi));
        D(edi) = 0;
        goto done;
    }
    esi = GameP();
L_441f5e:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x40400000 /* 3.0 */, 0x40e00000 /* 7.0 */);
    VC(ebp, 0x1b8, 0x97u);
    if (orig::fn_0044ba30(D(edi), ebp, (By(ebp + 0x488) >> 1) & 1))
        goto done;
    ecx = D(ebp + 0x484) & 0xffffffdfu;
    D(ebp + 0x484) = ecx;
    if (ecx & 0x80000)
    {
        D(ebp + 0x484) = ecx & 0xfff7ffffu;
        orig::fn_0044b7f0(D(edi), 0);
    }
    D(ebp + 0x488) &= 0xffff7fffu;
    Unref(D(edi));
    D(edi) = 0;
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(ebp, 0x1b8, 1u);
    goto done;

    // ---- state 0x97
L_44205c:
    ebx = ebp + 0x570;
    if (!D(ebp + 0x570))
    {
        if (D(ebp + 0x484) & 0x2000000)
        {
            VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
            edx = 0x7a;
            goto L_4422d8;
        }
        VC(ebp, 0x240, 0u, 0u, 0u);
        VC(ebp, 0x54, ebp + 0x4c0);
        goto done;
    }
    esi = 0;
    if (D(ebp + 0x488) & 0x10000)
    {
        esi = ebp;
        if (VC(D(ebp + 0xb78), 0x34))
            esi = 0;
    }
    if (orig::fn_0044b4b0(0 /* al = 0 */, D(ebx), ebp) || esi)
        goto L_442183;
    if (!VB(ebp, 0x22c, D(SP(0x20)), esi, 1u))
        goto L_442183;
    if (D(ebp + 0x488) & 0x18000)
        goto L_4422e3;
    eax = D(ebp + 0x484);
    if (eax & 0x80000)
    {
        D(ebp + 0x484) = eax & 0xfff7ffffu;
        orig::fn_0044b7f0(D(ebx), 0);
    }
    Unref(D(ebx));
    D(ebx) = 0;
    VC(ebp, 0x54, ebp + 0x4c0);
    goto done;
L_442183:
    eax = D(ebp + 0x498);
    if (eax == 8 || eax == 9)
    {
        if (D(ebp + 0x488) & 0x8000)
            goto L_44222f;
        t = Fl(ebp + 0x494) - Fl(SP(0x20));
        Fl(ebp + 0x494) = t;
        if (t < K(0x0056c05c))
            VC(ebp, 0x19c, 0xau, 1u, 0u, 0u, 0u, 0u);
    }
    else if (eax == 0xa && By(SP(0x10)))
    {
        Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x40400000, 0x40e00000);
        D(ebp + 0x498) = 0;
        VC(ebp, 0x19c, orig::fn_004e7740(9, 8), 1u, 0u, 0u, 0u, 0u);
    }
L_44222f:
    if (orig::fn_0044bff0(ebp, D(ebx), (By(ebp + 0x488) >> 1) & 1))
        goto L_4422e3;
    ecx = D(ebp + 0x484) & 0xffffffdfu;
    D(ebp + 0x484) = ecx;
    if (ecx & 0x80000)
    {
        D(ebp + 0x484) = ecx & 0xfff7ffffu;
        orig::fn_0044b7f0(D(ebx), 0);
    }
    D(ebp + 0x488) &= 0xffff7fffu;
    Unref(D(ebx));
    D(ebx) = 0;
    orig::fn_00454c90(ebp, 1, 1);
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    edx = 1;
L_4422d8:
    VC(ebp, 0x1b8, edx);
L_4422e3:
    VC(ebp, 0x54, ebp + 0x4c0);
    goto done;

    // ---- state 0x98
L_4422f7:
    orig::fn_00454c90(ebp, 1, 1);
    edx = static_cast<uint32_t>(orig::fn_00454cc0(ebp, ebp + 0x398) >> 32);
    Fl(ebp + 0x490) = Fl(ebp + 0x490) - Fl(SP(0x20));
    Fl(ebp + 0x494) = Fl(ebp + 0x494) - Fl(SP(0x20));
    if (!(Fl(ebp + 0x490) <= K(0x0056c05c)))
        goto done;
    orig::fn_0042cc70(ebp, edx /* edx as 0x00454cc0 left it */, 0, 0);
    orig::fn_00458250(ebp, 0);
    VC(ebp, 0x1b8, D(ebp + 0x4ac));
    goto done;

    // ---- state 0x50
L_442371:
    if (VB(ebp, 0x22c, D(SP(0x20)), 0u, 1u))
        goto done;
    orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
L_4423bc:
    orig::fn_00454c90(ebp, 1, 1);
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(ebp, 0x18c, 0u);
    VC(ebp, 0x1b8, 1u);
    goto done;

    // ---- state 0x5a (esi = game)
L_442407:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    VC(ebp, 0xf8, 0x170001u, 0u);
    if (D(ebp + 0x488) & 0x100)
    {
        orig::fn_0042ce70(D(GameP() + 0x748), ebp);
        goto done;
    }
    if (D(ebp + 0x558))
        goto L_442575;
    orig::fn_00453150(ebp, SP(0x30));
    {
        float angle = orig::fn_0046e2c0(0, 0xbf490fdb /* -pi/4 */, 0x3f490fdb /* pi/4 */);
        orig::fn_004e2e70(SP(0x36c), fb(angle));
    }
    orig::fn_004e36a0(SP(0x30), SP(0x36c), 1, SP(0x14));
    Normalize3(SP(0x14), SP(0x14));  // fstp st(0)
    VC(ebp, 0x54, SP(0x7c));
    Fl(SP(0x7c)) = orig::fn_0046e2c0(0, 0x41a00000 /* 20.0 */, 0x42480000 /* 50.0 */) * Fl(SP(0x14)) + Fl(SP(0x7c));
    Fl(SP(0x80)) = orig::fn_0046e2c0(0, 0x41a00000, 0x42480000) * Fl(SP(0x18)) + Fl(SP(0x80));
    Fl(SP(0x84)) = orig::fn_0046e2c0(0, 0x41a00000, 0x42480000) * Fl(SP(0x1c)) + Fl(SP(0x84));
    orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), SP(0x7c), 0, 0, 0, 0, 0);
    VC(ebp, 0x1b8, 0x5bu);
    goto done;

L_442575:
    VC(ebp, 0x18c, 0u);
    orig::fn_0042d1b0(D(ebp + 0x55c), D(ebp + 0x558), ebp, 0, D(ebp + 0x560), D(ebp + 0x564), 0);
    goto done;

    // ---- state 0x5b (esi = game)
L_4425a9:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    VC(ebp, 0x54, ebp + 0x4c0);
    if (!(D(ebp + 0x488) & 0x200))
        goto L_4423bc;
    orig::fn_0042ce70(0, ebp);
    goto done;

    // ---- state 0x67
L_4425fe:
    if (!By(SP(0x10)))
        goto done;
    VC(ebp, 0xdc);
    goto done;

    // ---- state 0x64: the animation/scripted-motion block +0xa18..+0xa98 (guess).
L_44261a:
    ebx = By(SP(0x10));
    if (ebx && (D(ebp + 0x488) & 0x800000))
    {
        D(ebp + 0x488) &= 0xff7fffffu;
        orig::fn_00408e30(0x81, ebp, 0xffffffffu, ebp + 0x4ec, 0, 0x3e800000, 0, 0);
    }
    Fl(ebp + 0xa20) = Fl(SP(0x20)) + Fl(ebp + 0xa20);
    if (By(ebp + 0xa18) & 2)
    {
        if (!ebx)
            goto L_4426b6;
        VC(ebp, 0x1ac, 1u);
        goto L_4427d1;
    }
    if ((D(ebp + 0x48c) & 0x1000) || !static_cast<uint8_t>(orig::fn_00406430(ebp + 0xa18, D(ebp + 0x48c), ebp)))
        goto L_4426b6;
    VC(ebp, 0x1ac, 1u);
    goto L_4427d1;
L_4426b6:
    if (!VB(ebp, 0xd8))
        goto L_4427d1;
    esi = orig::fn_004ed260(D(ebp + 0x410));
    // ECX at 0x004426e5 is unknowable: last written by the virtual +0xd8 callee (0x004426bb), 0x004ed260 leaves it
    // alone. edx = 0 (0x004426e3), so 0x004eb5d0 never reads its ECX slot (only on a first-lookup miss with edx != 0).
    edi = orig::fn_004eb5d0(D(ebp + 0xa1c), 0 /* ECX unknowable, unread with edx = 0 */, 0, ebp + 0xab8);
    eax = D(ebp + 0xa1c);
    {
        float v = static_cast<float>(I(eax + 0x20)) * static_cast<float>(I(eax + 0x1c));
        v = v * Fl(ebp + 0xa20);
        v = v + static_cast<float>(I(eax + 0x14));
        r = orig::fn_004ec410(edi, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int64_t>(v))) /* _ftol */,
                              D(ebp + 0x410), esi, 0, 0);
    }
    esi = ebp + 0xa6c;
    orig::fn_004e2910(r, esi, ebp + 0xa28);
    VC(ebp, 0x54, SP(0x88));
    D(SP(0x14)) = D(ebp + 0xa78);
    D(ebp + 0xa78) = D(SP(0x88));
    D(SP(0x18)) = D(ebp + 0xa88);
    D(ebp + 0xa88) = D(SP(0x8c));
    D(SP(0x1c)) = D(ebp + 0xa98);
    D(ebp + 0xa98) = D(SP(0x90));
    orig::fn_00457990(D(SP(0x8c)) & 0xffffff00u /* xor al, al */, ebp, esi);
    Fl(SP(0x24)) = Fl(SP(0x14)) - Fl(SP(0x88));
    Fl(SP(0x28)) = Fl(SP(0x18)) - Fl(SP(0x8c));
    Fl(SP(0x2c)) = Fl(SP(0x1c)) - Fl(SP(0x90));
    orig::fn_00458510(SP(0x24), ebp);
    D(ebp + 0xa24) = D(ebp + 0xa20);
L_4427d1:
    if (D(ebp + 0x488) & 0x2000000)
    {
        orig::fn_00455e70(ebp, 0, 0);
        goto done;
    }
    if (By(ebp + 0x48c) & 0x40)
        orig::fn_00455e70(ebp, 1, 0);
    goto done;

    // ---- state 0x65
L_44280a:
    orig::fn_00454c90(ebp, 1, 1);
    edi = ebp + 0x39c;
    orig::fn_00454cc0(ebp, edi);
    D(edi) = orig::fn_00455a00(D(edi));
    D(ebp + 0x398) = orig::fn_00455a00(D(ebp + 0x398));
    D(SP(0x14)) = 0;
    D(SP(0x18)) = 0;
    D(SP(0x1c)) = 0;
    orig::fn_00458510(SP(0x14), ebp);
    orig::fn_00458580(SP(0x14), ebp);
L_442860:
    VC(ebp, 0x54, ebp + 0x4c0);
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(ebp, 0x1b8, 1u);
    goto done;

    // ---- state 0x6e
L_4428a2:
    eax = D(ebp + 0x498);
    if (eax < 0x32 || eax > 0x34)
        VC(ebp, 0x19c, orig::fn_004e7740(0x34, 0x32), 0u, 0u, 0u, 0u, 0u);
    orig::fn_00454c90(ebp, 1, 1);
    esi = ebp + 0x588;
    eax = D(esi);
    if (eax && !VB(ebp, 0x180, eax, fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
    {
        Unref(D(esi));
        D(esi) = 0;
    }
    if (D(esi))
    {
        VC(ebp, 0x54, SP(0x16c));
        esi = D(esi);
        VC(esi, 0x54, SP(0x228));
        esi = ebp + 0x51c;
        Fl(esi) = Fl(SP(0x228)) - Fl(SP(0x16c));
        Fl(esi + 4) = Fl(SP(0x22c)) - Fl(SP(0x170));
        orig::fn_004e7780(esi, esi);
        D(ebp + 0x524) = 0;
        orig::fn_00454e60(esi, ebp, 0x41800000);
    }
    if (!By(SP(0x10)))
        goto done;
    if (static_cast<uint8_t>(orig::fn_00428160(ebp)))
        goto done;
    eax = D(ebp + 0x484);
L_4429c5:
    if (!(eax & 0x2000000))
        goto L_43f894;
L_4429d0:
    goto L_445a1a;

    // ---- states 0xaa, 0xac (esi = game)
L_4429da:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    VC(ebp, 0x16c);
    if (D(ebp + 0x460) == 0xaa)
        goto L_442a55;
    VC(ebp, 0x1b8, 0xadu);
    goto done;

    // ---- state 0xae (esi = game)
L_442a2f:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), 0);
    orig::fn_00427210(ebp);
    goto done;

    // ---- state 0xab
L_442a4a:
    VC(ebp, 0x170);
L_442a55:
    orig::fn_0042bf70(ebp, 0);
    goto done;

    // ---- state 0xad
L_442a63:
    D(SP(0x14)) = 0;
    D(SP(0x18)) = 0;
    D(SP(0x1c)) = 0;
    D(ebp + 0x39c) = orig::fn_00455a00(D(ebp + 0x39c));
    D(ebp + 0x398) = orig::fn_00455a00(D(ebp + 0x398));
    orig::fn_00458580(SP(0x14), ebp);
    r = static_cast<uint32_t>(orig::fn_00458510(SP(0x14), ebp));
    orig::fn_004a0e00(r & 0xffffff00u /* xor al, al */, ebp + 0x61c);
    eax = VC(ebp, 0x248, 2u, SP(0x94));
    VC(ebp, 0x238, eax, D(SP(0x94)));
    orig::fn_00408e30(0x80, ebp, 0xffffffffu, ebp + 0xab8, 0, 0x3e800000, 0, 0);
    D(ebp + 0xa18) |= 2;
    goto done;

    // ---- states 0xa0, 0xa1: the vehicle/mount object +0x904 (guess).
L_442b0c:
    esi = D(ebp + 0x904);
    if (!static_cast<uint8_t>(orig::fn_004268e0(esi, ebp)))
    {
        VC(ebp, 0x18c, 0u);  // its argument is the push 0 at 0x00442b1a
        orig::fn_0042b8e0(0, ebp);
        goto done;
    }
    orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);  // same push 0
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    orig::fn_00454c90(ebp, 1, 1);
    if (D(ebp + 0x460) != 0xa0)
        goto L_442bef;
    esi = ebp + 0x934;
    VC(D(ebp + 0x904), 0x150, esi, ebp + 0x908, D(ebp + 0x914), ebp, D(ebp + 0x588), 0u);
    orig::fn_004324d0(0, ebp, D(ebp + 0x904), 1);
    edx = GameP();
    eax = D(edx + 0xaa4);
    orig::fn_0046f6b0(eax & 0xffffff00u, 0 /* cl = 0 */, edx & 0xffffff00u, esi, eax, ebp, 0);
    VC(ebp, 0x1b8, 0xa1u);
    goto done;
L_442bef:  // (state 0xa1, after 0x00454c90 at 0x00442bea)
    if (!D(ebp + 0x588))
    {
        D(ebp + 0x490) = 0;
        eax = orig::fn_00426920(ebp, D(ebp + 0x5dc), D(ebp + 0x904), 0);
        if (eax)
            VC(ebp, 0x23c, eax, 1u);
    }
    edx = D(ebp + 0x484) | 0x20000;
    goto L_442f94;

    // ---- state 0xa2
L_442c3d:
    orig::fn_00454c90(ebp, 1, 1);
    ebx = ebp + 0x51c;
    D(ebx) = D(ebp + 0x908);
    D(ebx + 4) = D(ebp + 0x90c);
    D(ebx + 8) = D(ebp + 0x910);
    orig::fn_00454e60(ebx, ebp, 0x41800000);
    orig::fn_0042ec20(ebp, D(ebp + 0x904), D(ebp + 0x5dc), D(ebp + 0x904));
    if (!By(SP(0x10)))
        goto done;
    D(ebp + 0x484) &= ~4u;
    orig::fn_00427600(ebp);
    esi = D(ebp + 0x904);
    if (!static_cast<uint8_t>(orig::fn_004268e0(esi, ebp)))
        goto L_43f887;
L_442cb5:
    Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x3f800000 /* 1.0 */, 0x40c00000 /* 6.0 */);
    VC(ebp, 0x19c, 0x65u, 1u, 0u, 0u, 0u, 0u);
    VC(ebp, 0x1b8, 0xa3u);
    goto done;

    // ---- state 0xa3
L_442cf9:
    esi = D(ebp + 0x904);
    if (!static_cast<uint8_t>(orig::fn_004268e0(esi, ebp)))
        goto L_442ea3;
    orig::fn_00454c90(ebp, 1, 1);
    esi = ebp + 0x908;
    ebx = ebp + 0x51c;
    D(ebx) = D(esi);
    D(ebx + 4) = D(esi + 4);
    D(ebx + 8) = D(esi + 8);
    orig::fn_00454e60(ebx, ebp, 0x41800000);
    orig::fn_0042ec20(ebp, D(ebp + 0x904), D(ebp + 0x5dc), D(ebp + 0x904));
    if (!D(ebp + 0x588))
        D(ebp + 0x484) |= 0x40000;
    ebx = 0;  // bl
    if (!(D(ebp + 0x484) & 0x200))
    {
        if (!static_cast<uint8_t>(orig::fn_0042b860(D(ebp + 0x588), esi, ebp + 0x934, ebp, 0x42a00000 /* 80.0 */)))
        {
            ebx = 1;
            D(ebp + 0x494) = 0;
        }
    }
    t = Fl(ebp + 0x494) - Fl(SP(0x20));
    Fl(ebp + 0x494) = t;
    if (!(t <= K(0x0056c05c)))
        goto done;
    if (orig::fn_004e7730(0x3e8) >= 0x64)
    {
        if (!(D(ebp + 0x484) & 0x40000) || ebx)
        {
            VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 0u, 0u, 0x3f000000u, 0x3f000000u);
            VC(ebp, 0x1b8, 0xa4u);
            goto L_442e84;
        }
    }
    if (orig::fn_004e7730(0x64) >= 0x32 && D(ebp + 0x588) && VB(ebp, 0x1fc))
    {
        VC(ebp, 0x19c, 0x68u, 0u, 0u, 0u, 0u, 0u);
        edx = 0xa6;
    }
    else
    {
        VC(ebp, 0x19c, 0x67u, 0u, 0u, 0u, 0u, 0u);
        edx = 0xa5;
    }
    VC(ebp, 0x1b8, edx);
L_442e84:
    D(ebp + 0x484) &= 0xfffbffffu;
    goto done;

    // ---- states 0xa5, 0xa6
L_442e93:
    esi = D(ebp + 0x904);
    if (static_cast<uint8_t>(orig::fn_004268e0(esi, ebp)))
        goto L_442ebc;
L_442ea3:
    VC(ebp, 0x18c, 0u);
    orig::fn_0042b8e0(0, ebp);
    goto done;
L_442ebc:
    orig::fn_0042ec20(ebp, D(ebp + 0x5dc), D(ebp + 0x5dc), D(ebp + 0x904));
    orig::fn_00454c90(ebp, 1, 1);
    if (D(ebp + 0x460) == 0xa6)
        orig::fn_00454cc0(ebp, ebp + 0x398);
    else
    {
        ebx = ebp + 0x51c;
        D(ebx) = D(ebp + 0x908);
        D(ebx + 4) = D(ebp + 0x90c);
        D(ebx + 8) = D(ebp + 0x910);
        orig::fn_00454e60(ebx, ebp, 0x41800000);
    }
    if (!By(SP(0x10)))
        goto done;
    if ((By(ebp + 0x484) & 4) && I(ebp + 0x5bc) > 0)
    {
        orig::fn_00427600(ebp);
        goto L_442cb5;
    }
    edx = D(ebp + 0x484) & ~4u;
L_442f94:
    D(ebp + 0x484) = edx;
L_442f9a:
    VC(ebp, 0x19c, 0x66u, 0u, 0u, 0u, 0u, 0u);
    VC(ebp, 0x1b8, 0xa2u);
    goto done;

    // ---- state 0xa4
L_442fc6:
    esi = D(ebp + 0x904);
    if (!static_cast<uint8_t>(orig::fn_004268e0(esi, ebp)))
        goto L_43f887;
    if (By(SP(0x10)))
    {
        D(ebp + 0x490) = 0;
        orig::fn_0042ec20(ebp, D(ebp + 0x904), D(ebp + 0x5dc), D(ebp + 0x904));
        if (!D(ebp + 0x588))
            goto L_4459e4;
        VC(ebp, 0x230, 1u);
        goto done;
    }
    orig::fn_0042ec20(ebp, D(ebp + 0x904), D(ebp + 0x5dc), D(ebp + 0x904));
    orig::fn_0042df70(ebp, 0x42340000 /* 45.0 */);
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 0u, 0u, D(ebp + 0x580), D(ebp + 0x584));  // tail shared at 0x00446062
    goto done;

    // ---- state 0xb8: climbs onto the point +0x934 when slow enough and below it (guess).
L_443057:
    VC(ebp, 0x54, SP(0xa8));
    ebx = ebp + 0x51c;
    Fl(ebx) = Fl(ebp + 0x934) - Fl(SP(0xa8));
    Fl(ebx + 4) = Fl(ebp + 0x938) - Fl(SP(0xac));
    orig::fn_004e7780(ebx, ebx);
    D(ebp + 0x524) = 0;
    orig::fn_00454e60(ebx, ebp, 0x41800000);
    orig::fn_00458000(ebp, SP(0x14));
    if (!(std::sqrt(Fl(SP(0x18)) * Fl(SP(0x18)) + Fl(SP(0x14)) * Fl(SP(0x14))) < K(0x005bcbfc)))
        goto done;
    if (!(Fl(ebp + 0x93c) + K(0x005bcbdc) <= Fl(SP(0xb0))))
        goto done;
    D(ebp + 0x484) |= 0x20000000;
    orig::fn_0047bb30(D(ebp + 0xb08), ebp, ebp + 0xb0c, 1);
    VC(ebp, 0x1a0, 0x6fu, 0x70u, 0x6du, 0x6eu, 1u, 0u, 0x3f000000u, 0x3f000000u);
    orig::fn_00408e30(0x81, D(ebp + 0xb08), 0xffffffffu, 0x005b68ac, 0, 0x3e800000, 0, 0);
    VC(ebp, 0x1b8, 0xb9u);
    goto done;

    // ---- state 0xc1 (edi = 0)
L_44319c:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_0042df70(ebp, 0x42340000 /* 45.0 */);
    VC(ebp, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, edi, edi, D(ebp + 0x580), D(ebp + 0x584));
    if (!By(SP(0x10)))
        goto done;
    orig::fn_00427600(ebp);
    ebx = ebp + 0x588;
    D(ebp + 0x490) = 0;
    eax = D(ebx);
    if (!eax)
    {
        orig::fn_0042bc60(eax, ebp, 1, 1);
        goto done;
    }
    if (!VB(ebp, 0x180, eax, fb(Fl(ebp + 0x5dc) + Fl(ebp + 0x5dc))))
    {
        esi = D(ebx);
        Unref(esi);
        if (ebx)
            D(ebx) = 0;
    L_44324e:
        orig::fn_0042bc60(esi, ebp, 1, 1);
        goto done;
    }
    orig::fn_00428280(SP(0x30), ebp, ebp, 0x005b68fc);
    VC(D(ebx), 0x54, SP(0x300));
    Fl(SP(0x10)) = Fl(ebp + 0x5d8);
    t = Dist2Sq(SP(0x30), SP(0x300));
    if (Fl(SP(0x10)) * Fl(SP(0x10)) > t && static_cast<uint8_t>(orig::fn_00426fd0(SP(0x30), ebp, D(ebx), 0)))
    {
        orig::fn_00427c80(ebp, 1);
        goto done;
    }
    if (D(ebp + 0x904))
        VC(ebp, 0x188);
    if (static_cast<int32_t>(orig::fn_004e7730(0x64)) < 0x32)
        goto L_445a5f;
    VC(ebp, 0x240, 1u, 0u, 0u);
    goto done;

    // ---- states 0xb6, 0xbc (esi = game)
L_44330a:
    if (!static_cast<uint8_t>(orig::fn_00454c20(ebp)))
    {
        esi = GameP();
        D(ebp + 0x490) = 0;
    }
    else
        esi = GameP();
    // ---- state 0xb5 (esi = game)
L_443325:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), 0);
    orig::fn_00427210(ebp);
    Fl(ebp + 0x490) = Fl(ebp + 0x490) - Fl(SP(0x20));
    VC(ebp, 0x54, SP(0x114));
    if (!static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto L_4433fe;
    if (Fl(ebp + 0x490) <= K(0x0056c05c))
        goto L_4433fe;
    edx = ebp + 0x934;
    if (!(Dist2Sq(edx, SP(0x114)) < K(0x005bcfa4)))
        goto done;
    Fl(SP(0x14)) = Fl(edx) - Fl(SP(0x114));
    Fl(SP(0x18)) = Fl(edx + 4) - Fl(SP(0x118));
    orig::fn_004e7780(SP(0x14), SP(0x14));
    orig::fn_00453150(ebp, SP(0x30));
    if (!(Fl(SP(0x34)) * Fl(SP(0x18)) + Fl(SP(0x30)) * Fl(SP(0x14)) > K(0x005bcfa0)))
        goto done;
L_4433fe:
    orig::fn_0042a2c0(ebp);
    goto done;

    // ---- state 0xc5 (edi = 0)
L_443409:
    esi = ebp + 0x51c;
    orig::fn_00458000(ebp, esi);
    t = Len2(esi);
    D(ebp + 0x524) = edi;
    if (!(t > K(0x005bcbec)))
        goto done;
    orig::fn_004e7780(esi, esi);
    orig::fn_00454e60(esi, ebp, 0x41800000);
    goto done;

    // ---- state 0xbd (edi = 0)
L_443452:
    if (D(ebp + 0x588) != edi)
    {
        VC(ebp, 0x54, SP(0x1a4));
        VC(D(ebp + 0x588), 0x54, SP(0x194));
        esi = ebp + 0x51c;
        Fl(esi) = Fl(SP(0x194)) - Fl(SP(0x1a4));
        Fl(esi + 4) = Fl(SP(0x198)) - Fl(SP(0x1a8));
        orig::fn_004e7780(esi, esi);
        D(ebp + 0x524) = 0;
    }
    else
    {
        esi = ebp + 0x51c;
        orig::fn_00458000(ebp, esi);
        t = orig::fn_004e7780(esi, esi);
        D(ebp + 0x524) = edi;
        if (!(t > K(0x005bcbec)))
            goto L_4434f2;
    }
    orig::fn_00454e60(esi, ebp, 0x41800000);
L_4434f2:
    if (!(By(ebp + 0x484) & 4))
        goto done;
    VC(ebp, 0x198, 1u, 1u, 0u, 1u, 1u);
    goto done;

    // ---- state 0xbf (esi = game)
L_443519:
    if (By(SP(0x10)))
    {
        orig::fn_0046f6d0(ebp, D(esi + 0xaa4), ebp + 0x934, 0, 0, 0, 0, 0);
        orig::fn_00427210(ebp);
        VC(ebp, 0x1b8, D(ebp + 0x4ac));
        goto done;
    }
    orig::fn_00454c90(ebp, 1, 1);
    goto done;

    // ---- state 0xc0 (edi = 0)
L_44356a:
    if (!By(SP(0x10)))
        goto L_443fb1;
    VC(ebp, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, edi, edi, D(ebp + 0x580), D(ebp + 0x584));
    VC(ebp, 0x1b8, 0xc1u);
    goto done;

    // ---- state 0xbe
L_4435ae:
    VC(ebp, 0x54, SP(0x1bc));
    ebx = ebp + 0x51c;
    Fl(ebx) = Fl(ebp + 0x934) - Fl(SP(0x1bc));
    Fl(ebx + 4) = Fl(ebp + 0x938) - Fl(SP(0x1c0));
    orig::fn_004e7780(ebx, ebx);
    D(ebp + 0x524) = 0;
    if (!static_cast<uint8_t>(orig::fn_00454e60(ebx, ebp, 0x41800000)))
        goto done;
    orig::fn_00453150(ebp, SP(0x30));
    orig::fn_00458000(ebp, SP(0x14));
    t = Len2(SP(0x14));
    Fl(SP(0x14)) = Fl(SP(0x30)) * t;
    Fl(SP(0x18)) = Fl(SP(0x34)) * t;
    orig::fn_00458510(SP(0x14), ebp);
    goto done;

    // ---- state 0xc4: jump towards the point +0x934 (guess: the flight time is sqrt(|2 dz / (+0xb04 * k)|)).
L_443651:
    orig::fn_00458000(ebp, SP(0x30));
    if (!(Fl(SP(0x38)) < K(0x0056c05c)))
        goto L_44379d;
    D(ebp + 0x484) &= ~4u;
    orig::fn_00427600(ebp);
    VC(ebp, 0x54, SP(0xcc));
    Fl(SP(0x24)) = Fl(ebp + 0x934) - Fl(SP(0xcc));
    Fl(SP(0x28)) = Fl(ebp + 0x938) - Fl(SP(0xd0));
    {
        float len = orig::fn_004e7780(SP(0x24), SP(0x24));
        float h = Fl(SP(0xd4)) - Fl(ebp + 0x93c);
        D(SP(0x2c)) = 0;
        h = h + h;
        float g = Fl(ebp + 0xb04) * K(0x005bcd64);
        float q = h / g;
        q = std::fabs(q);
        q = std::sqrt(q);
        float speed = len / q;
        Fl(SP(0x24)) = Fl(SP(0x24)) * speed;
        Fl(SP(0x28)) = Fl(SP(0x28)) * speed;
    }
    orig::fn_00458510(SP(0x24), ebp);
    orig::fn_0047bb30(D(ebp + 0xb08), ebp, ebp + 0xb0c, 1);
    D(ebp + 0x484) |= 0x30000000;
    VC(ebp, 0x1a0, 0x6fu, 0x70u, 0x6du, 0x6eu, 1u, 0u, 0x3f000000u, 0x3f000000u);
    orig::fn_00408e30(0x81, D(ebp + 0xb08), 0xffffffffu, 0x005b68ac, 0, 0x3e800000, 0, 0);
    D(ebp + 0x4ac) = 0xc3;
    VC(ebp, 0x1b8, 0xbdu);
    goto done;
L_44379d:
    ebx = ebp + 0x51c;
    orig::fn_00458000(ebp, ebx);
    orig::fn_004e7780(ebx, ebx);
    D(ebp + 0x524) = 0;
    orig::fn_00454e60(ebx, ebp, 0x41800000);
    goto done;

    // ---- state 0xc3 (edi = 0)
L_4437d0:
    if ((By(ebp + 0x484) & 4) && D(ebp + 0x588) != edi)
        goto L_445cf7;
    VC(ebp, 0x1b8, 0x32u);
    goto done;

    // ---- state 0xba (edi = 0)
L_4437f7:
    orig::fn_00458000(ebp, SP(0x30));
    D(SP(0x14)) = 0;
    D(SP(0x18)) = 0;
    D(SP(0x1c)) = D(SP(0x38));
    {
        float a = orig::fn_004581c0(ebp);
        float b = orig::fn_004581c0(ebp) * Fl(ebp + 0x944);
        Fl(SP(0x40)) = orig::fn_00471750(SP(0x14), SP(0x30), SP(0x4c), edi, fb(b), fb(a));
    }
    D(ebp + 0xb10) = orig::fn_00455a30(SP(0x4c), 0x10, ebp, D(ebp + 0xb10), D(SP(0x40)), 0xffffffffu, edi);
    goto done;

    // ---- state 0xb9
L_443878:
    orig::fn_00453150(ebp, SP(0x30));
    orig::fn_00458000(ebp, SP(0x24));
    Fl(SP(0x14)) = Fl(SP(0x30)) * Fl(ebp + 0xaf8);
    D(SP(0x1c)) = D(SP(0x2c));
    Fl(SP(0x18)) = Fl(SP(0x34)) * Fl(ebp + 0xaf8);
    if (Fl(SP(0x2c)) > K(0x0056c05c))
        D(SP(0x1c)) = 0;
    else
    {
        t = -Fl(ebp + 0xafc);
        if (Fl(SP(0x2c)) < t)
            Fl(SP(0x1c)) = t;
    }
    {
        float a = orig::fn_004581c0(ebp);
        float b = orig::fn_004581c0(ebp) * Fl(ebp + 0xb00);
        Fl(SP(0x40)) = orig::fn_00471750(SP(0x14), SP(0x24), SP(0x30), 0, fb(b), fb(a));
    }
    D(ebp + 0xb10) = orig::fn_00455a30(SP(0x30), 0x10, ebp, D(ebp + 0xb10), D(SP(0x40)), 0xffffffffu, 0);
    VC(ebp, 0x54, SP(0xfc));
    ebx = ebp + 0x934;
    if (Dist2Sq(ebx, SP(0xfc)) < K(0x005bcbe0))
    {
        D(ebp + 0x484) &= 0xdfffffffu;
        orig::fn_0047bb30(D(ebp + 0xb08), ebp, ebp + 0xb0c, 0);
        VC(ebp, 0x19c, 0x6bu, 1u, 0u, 0u, 0u, 0u);
        orig::fn_00408e30(0x81, D(ebp + 0xb08), 0xffffffffu, 0x005b6674, 0, 0x3e800000, 0, 0);
        VC(ebp, 0x1b8, 0xbau);
    }
    esi = ebp + 0x51c;
    Fl(esi) = Fl(ebx) - Fl(SP(0xfc));
    Fl(esi + 4) = Fl(ebx + 4) - Fl(SP(0x100));
    orig::fn_004e7780(esi, esi);
    D(ebp + 0x524) = 0;
    orig::fn_00454e60(esi, ebp, 0x41800000);
    goto done;

    // ---- state 0xc8 (esi = game, edi = 0)
L_443a28:
    if (D(ebp + 0xb54) == edi)
        Fl(ebp + 0x490) = Fl(ebp + 0x490) - Fl(SP(0x20));
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), edi);
    orig::fn_00427210(ebp);
    if (!static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto L_443b5a;
    if (Fl(ebp + 0x490) <= K(0x0056c05c))
        goto L_443b5a;
    if (D(ebp + 0xb08) == edi || D(ebp + 0x460) != 0xc8)
        goto done;
    eax = D(ebp + 0xb54);
    if (eax == edi)
        goto done;
    if (!(orig::fn_004817a0(eax) < K(0x005bcda4)))
        goto done;
    D(SP(0x14)) = 0;
    D(SP(0x18)) = 0;
    Fl(SP(0x1c)) = orig::fn_004581c0(ebp) * Fl(ebp + 0xae8) * K(0x005bcb4c);
    orig::fn_00458440(0, SP(0x14), ebp);
    orig::fn_0047bba0(D(ebp + 0xb08), 0x005b6894);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf1c), ebp);
    D(ebp + 0x484) |= 0x10000000;
    VC(ebp, 0x19c, 0x6bu, 1u, 0u, 0u, 0u, 0u);
    VC(ebp, 0x1b8, 0xc9u);
    goto done;
L_443b5a:
    esi = D(ebp + 0xb54);
    if (esi != edi)
    {
        Call_00481ab0(0, esi, edi);  // 0x004413a0 with the push edi at 0x00443b62 as its argument
        goto done;
    }
    VC(D(GameP() + 0x2c), 0xd8, ebp + 0xb4c, edi);
    D(ebp + 0x484) &= 0xffffffdfu;
    eax = D(ebp + 0x588);
    if (eax == edi)
        eax = D(GameP() + 0x748);  // 0x0044244d
    orig::fn_0042ce70(eax, ebp);
    goto done;

    // ---- state 0xd2 (esi = game, edi = 0)
L_443ba1:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), edi);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)) && VB(D(ebp + 0x864), 0xd8))
        goto done;
    esi = ebp + 0x864;
    orig::fn_004723f0(D(ebp + 0x864), 0 /* cl = 0 */);
    Unref(D(esi));
    D(esi) = 0;
    goto L_441e5c;

    // ---- state 0x118: the speaker/companion object +0x88c (guess; speech cues by name).
L_443c4a:
    eax = D(ebp + 0x88c);
    edi = ebp + 0x88c;
    if (!eax)
        goto L_443fd4;
    if (static_cast<uint8_t>(orig::fn_00454780(eax)))
    {
        esi = GameP();
        goto L_443fd4;
    }
    ecx = D(edi);
    if (By(ecx + 0x421))
    {
        D(ebp + 0x488) &= 0xefffffffu;
        Unref(D(edi));
        D(edi) = 0;
        orig::fn_0042ce70(D(GameP() + 0x748), ebp);
        goto done;
    }
    t = Fl(ebp + 0x494) - Fl(SP(0x20));
    Fl(ebp + 0x494) = t;
    if (!(t <= K(0x0056c05c)))
        goto L_443f92;
    if (D(ebp + 0x498) != 2)
        goto L_443f35;
    t = Fl(0x00604a98);
    ecx = GameP();
    if (!(t - Fl(ecx + 0xb74) >= Fl(ecx + 0xb80)))
        goto L_443f07;
    Fl(ecx + 0xb74) = t;
    Fl(GameP() + 0xb80) = orig::fn_0046e2c0(ecx, D(ecx + 0xb78), D(ecx + 0xb7c));
    r = orig::fn_004e7730(0x64);
    if (static_cast<int32_t>(r) >= 0x46)
        goto L_443e6d;
    eax = orig::fn_00453270(D(edi));
    if (orig::fn_005448ee(0, eax, 0x005bbe78))
    {
        VC(D(GameP() + 0x2c), 0xc4, 0x005b4b68u, D(edi), 0u, 0u, 0xffffffffu, 0u);
        goto L_443f07;
    }
    r = orig::fn_004e7730(0x2bc);
    if (r < 0x64)
        edx = 0x005b2944;
    else if (r < 0xc8)
        edx = 0x005b2930;
    else if (r < 0x12c)
        edx = 0x005b291c;
    else if (r < 0x190)
        edx = 0x005b2908;
    else if (r < 0x1f4)
        edx = 0x005b4c80;
    else if (r < 0x258)
        edx = 0x005b28f4;
    else
        edx = 0x005b28d8;
    VC(D(GameP() + 0x2c), 0xc4, edx, D(edi), 0u, 0u, 0xffffffffu, 0u);
    goto L_443f07;
L_443e6d:
    if (D(ebp + 0x484) & 0x200000)
        VC(D(GameP() + 0x2c), 0xc4, 0x005b28c4u, ebp, 0u, 0u, 0xffffffffu, 0u);
    else if (D(ebp + 0x488) & 0x20000000)
        VC(D(GameP() + 0x2c), 0xc4, 0x005b28b4u, ebp, 0u, 0u, 0xffffffffu, 0u);
    else
    {
        r = orig::fn_004e7730(0xc8);
        VC(D(GameP() + 0x2c), 0xc4, r < 0x64 ? 0x005b28a0u : 0x005b288cu, ebp, 0u, 0u, 0xffffffffu, 0u);
    }
L_443f07:
    VC(ebp, 0x19c, 0x7du, 0u, 0u, 0u, 0u, 0u);
    orig::fn_00408e30(0x80, D(edi), 0xffffffffu, 0x005b2878, 0, 0x3e800000, 0, 0);
    goto L_443f92;
L_443f35:
    if (!By(SP(0x10)))
        goto L_443f92;
    Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x41200000 /* 10.0 */, 0x41f00000 /* 30.0 */);
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    orig::fn_00408e30(0x81, D(edi), 0xffffffffu, 0x005bbea4, 0, 0, 0, 0);
    // ---- state 0x01
L_443f92:
    if (VB(ebp, 0x22c, D(SP(0x20)), D(SP(0x10)), 0u))
        goto done;
L_443fb1:
    orig::fn_00454c90(ebp, 1, 1);
L_443fbc:
    orig::fn_00454cc0(ebp, ebp + 0x398);
    goto done;
L_443fd4:
    D(ebp + 0x488) &= 0xefffffffu;
    ebx = D(edi);
    if (ebx)
    {
        Unref(ebx);
        D(edi) = 0;
    }
    orig::fn_0042ce70(D(esi + 0x748), ebp);
    goto done;

    // ---- states 0x79, 0xdd (also entered from state 0x7a)
L_444020:
    eax = D(ebp + 0x9bc);
    if (eax && D(eax + 0x460) != 0x98)
    {
        orig::fn_0042cff0(ebp);
        orig::fn_00458250(ebp, 0);  // eax = self, left unchanged by 0x0042cff0
        VC(ebp, 0x19c, 0xeu, 1u, 0u, 0u, 1u, 0u);
        VC(ebp, 0x1b8, 0xdcu);
    }
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_0042ec20(ebp, D(ebp + 0x5dc), D(ebp + 0x5dc), 0);
    if (D(ebp + 0x498) == 0x2a && !(By(ebp + 0x484) & 4))
    {
        orig::fn_0042df70(ebp, 0x42340000 /* 45.0 */);
        VC(ebp, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(ebp + 0x580), D(ebp + 0x584));
        if (By(SP(0x10)))
            orig::fn_00427600(ebp);
        goto L_4441fc;
    }
    if (D(ebp + 0x588))
    {
        if (D(ebp + 0x488) & 0x80000)
            cl = VB(ebp, 0x198, 1u, 0u, 0u, 0u, 0u);
        else
            cl = VB(ebp, 0x198, 1u, 1u, 0u, 1u, 1u);
        if (static_cast<uint8_t>(orig::fn_00428160(ebp)) || !cl)
            goto L_4441fc;
        VC(ebp, 0x1a0, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, D(ebp + 0x580), D(ebp + 0x584));
        goto L_4441fc;
    }
    orig::fn_0042df70(ebp, 0x42340000 /* 45.0 */);
    t = Fl(ebp + 0x494) - Fl(SP(0x20));
    Fl(ebp + 0x494) = t;
    if (!By(SP(0x10)))
        goto L_4441fc;
    if ((By(ebp + 0x484) & 0x40) || t <= K(0x0056c05c))
    {
        orig::fn_00427330(ebp);
        Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x41700000 /* 15.0 */, 0x41c00000 /* 24.0 */);
        goto L_4441fc;
    }
    if (D(ebp + 0x488) & 0x8000)
    {
        orig::fn_00427210(ebp);
        Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x40400000 /* 3.0 */, 0x40e00000 /* 7.0 */);
        VC(ebp, 0x1b8, 0x97u);
    }
L_4441fc:
    if (D(ebp + 0x588) || !By(SP(0x10)) || VB(ebp, 0xe4))
        goto done;
    eax = D(ebp + 0x498);
    if (eax == 0x2a || eax == 0x23)
        goto L_4443f9;
    goto done;

    // ---- state 0x10e (edi = 0)
L_444246:
    if (D(ebp + 0x588) == edi)
        goto L_443fb1;
    orig::fn_0042a660(ebp);
    VC(ebp, 0x19c, 8u, 1u, 0u, 0u, 0u, 0u);
    VC(ebp, 0x1b8, 0x10fu);
    goto done;

    // ---- state 0x10f (edi = 0)
L_444285:
    VC(ebp, 0x19c, 8u, 1u, edi, edi, edi, edi);
    orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), edi);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    if (!VB(ebp, 0x228))
    {
        orig::fn_0042a660(ebp);
        goto done;
    }
    orig::fn_0042a660(ebp);
    D(ebp + 0x39c) = orig::fn_00455a00(D(ebp + 0x39c));
    D(ebp + 0x398) = orig::fn_00455a00(D(ebp + 0x398));
    D(SP(0x14)) = 0;
    D(SP(0x18)) = 0;
    D(SP(0x1c)) = 0;
    orig::fn_00458510(SP(0x14), ebp);
    orig::fn_00458580(SP(0x14), ebp);
    D(ebp + 0x490) = 0;
    D(ebp + 0x5c8) = 0x3c23d70a;  // 0.01
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(ebp, 0x1b8, 0x110u);
    goto done;

    // ---- state 0x110 (edi = 0)
L_444373:
    orig::fn_00454c90(ebp, 1, 1);
    if (D(ebp + 0x588) != edi)
    {
        orig::fn_0042df70(ebp, D(ebp + 0x860));
        goto done;
    }
    VC(ebp, 0x19c, 8u, 1u, edi, edi, edi, edi);
    VC(ebp, 0x1b8, 0x10fu);
    goto done;

    // ---- state 0x104
L_4443c1:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (!By(SP(0x10)))
        goto done;
    VC(ebp, 0x23c, D(ebp + 0x588), 1u);
L_4443f9:
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    goto done;

    // ---- state 0xfa (esi = game, edi = 0)
L_44441f:
    orig::fn_00470aa0(ebp, D(esi + 0xaa4), edi);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
        goto done;
    D(ebp + 0x490) = edi;
    orig::fn_0042ec20(ebp, 0 /* ecx: not read */, D(ebp + 0x5dc), edi);
    ecx = D(ebp + 0x588);
    if (ecx != edi)
    {
        VC(ecx, 0x54, ebp + 0x60c);
        VC(ebp, 0x184);
        goto done;
    }
    VC(ebp, 0x1a0, 2u, 3u, edi, 1u, 1u, edi, 0x3f000000u, 0x3f000000u);
    VC(ebp, 0x1b8, 1u);
    goto done;

    // ---- states 0xe9, 0xeb
L_4444b3:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (By(SP(0x10)))
        VC(ebp, 0x19c, 0x72u, 1u, 0u, 0u, 0u, 0u);
    if (VB(ebp, 0xe4))
        goto done;
    t = Fl(ebp + 0x878) - Fl(SP(0x20));
    Fl(ebp + 0x878) = t;
    if (!(t <= K(0x0056c05c)))
        goto done;
    VC(ebp, 0x1b0, 0u);
    goto done;

    // ---- state 0xe7 (esi = game)
L_444530:
    if (By(SP(0x10)) && D(ebp + 0x498) == 0x73)
    {
        VC(ebp, 0x19c, orig::fn_004e7740(0x4a, 0x49), 1u, 0u, 0u, 0u, 0u);
        esi = GameP();
    }
    t = Fl(ebp + 0x87c) - Fl(SP(0x20));
    Fl(ebp + 0x87c) = t;
    if (!(t <= K(0x0056c05c)))
        goto L_44461c;
    orig::fn_00454750(D(D(esi + 0xa84) + 0xf1c), ebp);
    if (D(ebp + 0xb08) && !(D(ebp + 0x488) & 0x1000000))
    {
        Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3f000000 /* 0.5 */, 0x3fc00000 /* 1.5 */);
        edx = 0xea;
    }
    else
    {
        orig::fn_004324d0(0, ebp, 0, 0);
        VC(D(GameP() + 0x2c), 0xd8, ebp + 0xb50, 0u);
        orig::fn_0042cc10(ebp);
        D(ebp + 0x454) = 0;
        edx = 0xe8;
    }
    VC(ebp, 0x1b8, edx);
L_44461c:
    orig::fn_00458000(ebp, SP(0x24));
    D(SP(0x14)) = D(SP(0x24));
    D(SP(0x18)) = D(SP(0x28));
    D(SP(0x1c)) = D(SP(0x2c));
    if (Fl(SP(0x2c)) <= K(0x0056c05c))
        D(SP(0x1c)) = 0;
    else if (Fl(SP(0x2c)) >= K(0x005bcb60))
        D(SP(0x1c)) = 0x42c80000;  // 100.0
    {
        float a = orig::fn_004581c0(ebp);
        float b = orig::fn_004581c0(ebp) * Fl(ebp + 0x944);
        Fl(SP(0x40)) = orig::fn_00471750(SP(0x14), SP(0x24), SP(0x30), 0, fb(b), fb(a));
    }
    D(ebp + 0xb10) = orig::fn_00455a30(SP(0x30), 0x10, ebp, D(ebp + 0xb10), D(SP(0x40)), 0xffffffffu, 0);
    goto done;

    // ---- state 0xea (edi = 0)
L_4446d3:
    orig::fn_00458000(ebp, SP(0x30));
    if (!(Fl(SP(0x38)) < K(0x0056c05c)))
        goto L_44474e;
    t = Fl(ebp + 0x490) - Fl(SP(0x20));
    Fl(ebp + 0x490) = t;
    if (!(t < K(0x0056c05c)))
        goto L_44474e;
    if (static_cast<uint8_t>(orig::fn_0042b480(ebp, 1)))
        goto L_44474e;
    VC(D(GameP() + 0x2c), 0xd8, ebp + 0xb50, edi);
    orig::fn_0042cc10(ebp);
    D(ebp + 0x454) = edi;
    VC(ebp, 0x1b8, 0xe8u);
    // ---- state 0xe8
L_44474e:
    if (By(SP(0x10)) && D(ebp + 0x498) == 0x73)
        VC(ebp, 0x19c, orig::fn_004e7740(0x4a, 0x49), 1u, 0u, 0u, 0u, 0u);
    orig::fn_00458000(ebp, SP(0x24));
    D(SP(0x1c)) = D(SP(0x2c));
    D(SP(0x14)) = D(SP(0x24));
    D(SP(0x18)) = D(SP(0x28));
    if (Fl(SP(0x2c)) > K(0x0056c05c))
        D(SP(0x1c)) = 0;
    else if (Fl(SP(0x2c)) < K(0x005bcf9c))
        D(SP(0x1c)) = 0xc2c80000;  // -100.0
    {
        float a = orig::fn_004581c0(ebp);
        float b = orig::fn_004581c0(ebp) * Fl(ebp + 0x944);
        Fl(SP(0x40)) = orig::fn_00471750(SP(0x14), SP(0x24), SP(0x30), 0, fb(b), fb(a));
    }
    D(ebp + 0xb10) = orig::fn_00455a30(SP(0x30), 0x10, ebp, D(ebp + 0xb10), D(SP(0x40)), 0xffffffffu, 0);
    VC(ebp, 0x54, SP(0x330));
    if (!(Fl(SP(0x338)) < Fl(ebp + 0x874)))
        goto done;
    if (Fl(ebp + 0x490) > K(0x0056c05c))
    {
        Fl(ebp + 0x490) = Fl(ebp + 0x490) - Fl(SP(0x20));
        goto done;
    }
    std::memset(P(SP(0x36c)), 0, 0xd * 4);
    orig::fn_00431720(ebp, SP(0x36c));
L_4448a4:
    VC(ebp, 0xd4, 0u);
    goto done;

    // ---- state 0xf1: spawns five objects 0x005b27b8 in a circle around +0x60c (guess).
L_4448b6:
    orig::fn_00454cc0(ebp, ebp + 0x398);
    orig::fn_00454c90(ebp, 1, 1);
    if (!By(SP(0x10)))
        goto done;
    VC(ebp, 0x5c, SP(0x3b0));
    orig::fn_00453150(ebp, SP(0x30));
    for (esi = 0; esi < 5; ++esi)
    {
        D(SP(0x40)) = esi;
        t = static_cast<float>(I(SP(0x40)));
        if (I(SP(0x40)) < 0)
            t = t + K(0x005bcb28);
        t = t * K(0x005bcd2c) * K(0x005bcb74);
        orig::fn_004e2e70(SP(0x36c), fb(t));
        orig::fn_004e36a0(SP(0x30), SP(0x36c), 1, SP(0x14));
        Normalize3(SP(0x14), SP(0x14));  // fstp st(0)
        Fl(SP(0x3bc)) = Fl(SP(0x14)) * K(0x005bcb30) + Fl(ebp + 0x60c);
        Fl(SP(0x3cc)) = Fl(SP(0x18)) * K(0x005bcb30) + Fl(ebp + 0x610);
        D(SP(0x3dc)) = D(ebp + 0x614);
        orig::fn_0047df70(D(GameP() + 0xaa8), ebp, 0x005b27b8, SP(0x3b0));
    }
    goto L_444cf5;

    // ---- state 0xf2 (edi = 0): picks a random point 30..40 ahead within +-90 degrees into +0x934.
L_4449c4:
    if (D(ebp + 0x884) == edi)
    {
        VC(ebp, 0x240, 1u, edi, edi);
        goto done;
    }
    orig::fn_00453150(ebp, SP(0x30));
    {
        float angle = orig::fn_0046e2c0(0, 0xbfc90fdb /* -pi/2 */, 0x3fc90fdb /* pi/2 */);
        orig::fn_004e2e70(SP(0x36c), fb(angle));
    }
    orig::fn_004e36a0(SP(0x30), SP(0x36c), 1, SP(0x14));
    Normalize3(SP(0x14), SP(0x14));  // fstp st(0)
    VC(ebp, 0x54, SP(0xe4));
    Fl(ebp + 0x934) = orig::fn_0046e2c0(0, 0x41f00000 /* 30.0 */, 0x42200000 /* 40.0 */) * Fl(SP(0x14)) + Fl(SP(0xe4));
    Fl(ebp + 0x938) = orig::fn_0046e2c0(0, 0x41f00000, 0x42200000) * Fl(SP(0x18)) + Fl(SP(0xe8));
    Fl(ebp + 0x93c) = orig::fn_0046e2c0(0, 0x41f00000, 0x42200000) * Fl(SP(0x1c)) + Fl(SP(0xec));
    VC(ebp, 0x19c, 0x25u, edi, edi, 1u, edi, edi);
    VC(ebp, 0x1b8, 0xf3u);
    goto done;

    // ---- state 0xf3: walks to +0x934, then spawns up to five objects 0x005b27b0 around itself (guess).
L_444ad6:
    orig::fn_00454c90(ebp, 1, 1);
    VC(ebp, 0x54, SP(0x1d4));
    esi = ebp + 0x51c;
    Fl(esi) = Fl(ebp + 0x934) - Fl(SP(0x1d4));
    Fl(esi + 4) = Fl(ebp + 0x938) - Fl(SP(0x1d8));
    orig::fn_004e7780(esi, esi);
    D(ebp + 0x524) = 0;
    orig::fn_00454e60(esi, ebp, 0x41800000);
    if (!By(SP(0x10)))
        goto done;
    D(SP(0x4c)) = 0;
    D(SP(0x50)) = 0x3f800000;  // 1.0
    D(SP(0x54)) = 0;
    Fl(SP(0x30)) = orig::fn_0046e2c0(0, 0x40e00000 /* 7.0 */, 0x41a00000 /* 20.0 */);
    Fl(SP(0x34)) = orig::fn_0046e2c0(0, 0x40e00000, 0x41a00000);
    D(SP(0x10)) = 0;  // a float counter of the spawned objects from here on
    VC(ebp, 0x54, SP(0xd8));
    esi = GameP();
    for (ebx = 0; ebx < 5; ++ebx)
    {
        D(SP(0x40)) = ebx;
        t = static_cast<float>(I(SP(0x40)));
        if (I(SP(0x40)) < 0)
            t = t + K(0x005bcb28);
        t = t * K(0x005bcd2c) * K(0x005bcb74);
        orig::fn_004e2e70(SP(0x36c), fb(t));
        orig::fn_004e36a0(SP(0x4c), SP(0x36c), 1, SP(0x24));
        Normalize3(SP(0x24), SP(0x24));  // fstp st(0)
        D(SP(0x1c)) = D(SP(0xe0));
        Fl(SP(0x14)) = Fl(SP(0x24)) * Fl(SP(0x30)) + Fl(SP(0xd8));
        Fl(SP(0x18)) = Fl(SP(0x28)) * Fl(SP(0x34)) + Fl(SP(0xdc));
        if (!static_cast<uint8_t>(orig::fn_0046f280(D(esi + 0xaa4), D(SP(0x14)), D(SP(0x18)))))
            continue;
        edi = orig::fn_0045f020(0, 0x005b27b0, D(esi + 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
        eax = GameP();
        ++D(eax + 0x2710);
        orig::fn_00457870(eax & 0xffffff00u /* xor al, al */, SP(0x14), edi, 0, 0, 0);
        orig::fn_00455860(edi, 0x447a0000 /* 1000.0 */, 0x461c4000 /* 10000.0 */);
        VC(edi, 0xf8, 0x20000u, 0u);
        if (static_cast<int32_t>(orig::fn_004e7730(0x64)) < 0xf)
            orig::fn_0049f380(edi, 0 /* edx: not read */, 1, D(SP(0x10)));
        else
            orig::fn_0049f380(edi, D(SP(0x10)), 0, D(SP(0x10)));
        Fl(SP(0x10)) = Fl(SP(0x10)) + K(0x005bcb2c);
        esi = GameP();
    }
L_444cf5:
    VC(ebp, 0x240, 1u, 0u, 0u);
    goto done;

    // ---- state 0xf0: approaches +0x934 and slows down on arrival (virtual +0x100 = speed factor, guess).
L_444d0b:
    edi = ebp + 0x51c;
    orig::fn_00458000(ebp, edi);
    orig::fn_00456110(edi, edi, ebp);  // st0 popped
    D(ebp + 0x524) = 0;
    orig::fn_00454e60(edi, ebp, 0x41800000);
    t = Fl(ebp + 0x490) - Fl(SP(0x20));
    Fl(ebp + 0x490) = t;
    if (!(t <= K(0x0056c05c)))
    {
        VC(ebp, 0x100, fb(t - K(0x005bcb2c)));
        goto done;
    }
    VC(ebp, 0x54, SP(0xf0));
    if (!(By(ebp + 0x48c) & 8))
    {
        Fl(ebp + 0x940) = Dist2(ebp + 0x934, SP(0xf0));
        D(ebp + 0x48c) |= 8;
    }
    VC(ebp, 0x100, 0u);
    esi = ebp + 0x934;
    orig::fn_004e77e0(SP(0xf0), esi, SP(0x30));
    t = Len3(SP(0x30));
    Fl(SP(0x10)) = t;
    if (t > Fl(ebp + 0x940))
        D(SP(0x10)) = D(ebp + 0x940);
    Fl(SP(0x40)) = (K(0x005bcb2c) - Fl(SP(0x10)) / Fl(ebp + 0x940)) * K(0x005bcb60) + K(0x005bcb4c);
    if (Fl(SP(0x10)) < K(0x005bcb48))
    {
        r = VC(ebp, 0x100, 0x3f800000u);
        r = VC(ebp, 0xf8, 0x170001u, 0u);
        orig::fn_00457870(r & 0xffffff00u /* xor al, al */, esi, ebp, 0, 0, 0);
        ecx = D(ebp + 0x904);
        if (!ecx || VC(ecx, 0x154, D(ebp + 0x914)) != 4)
            orig::fn_00455860(ebp, 0x40a00000 /* 5.0 */, 0x41200000 /* 10.0 */);
        VC(ebp, 0x19c, 2u, 1u, 0u, 0u, 0u, 0u);
        orig::fn_00454c90(ebp, 1, 1);
        D(ebp + 0x39c) = orig::fn_00455a00(D(ebp + 0x39c));
        D(ebp + 0x398) = orig::fn_00455a00(D(ebp + 0x398));
        D(SP(0x30)) = 0;
        D(SP(0x34)) = 0;
        D(SP(0x38)) = 0;
        orig::fn_00458510(SP(0x30), ebp);
        orig::fn_00454750(D(D(GameP() + 0xa84) + 0xf1c), ebp);
        D(ebp + 0x484) &= ~4u;
        orig::fn_00427600(ebp);
        if (D(ebp + 0x588))
        {
            VC(ebp, 0x230, 0u);
            goto done;
        }
        D(ebp + 0x490) = 0;
        VC(ebp, 0x1b8, 1u);
        goto done;
    }
    if (Fl(SP(0x10)) < K(0x005bcbe8))
        VC(ebp, 0x100, fb((K(0x005bcbe8) - Fl(SP(0x10))) * K(0x005bcc34)));
    if (Fl(SP(0x10)) < Fl(ebp + 0x940))
        Fl(SP(0x10)) = Fl(ebp + 0x940) - Fl(SP(0x10));
    Fl(SP(0x3c)) = Fl(SP(0x10)) * K(0x005bcf98) * K(0x005bcd50) + K(0x005bcdf8);
    Call_00471970(SP(0xf0), SP(0x30), esi, SP(0x3c), D(SP(0x3c)), D(SP(0x3c)), 0x3f800000, 0);  // st0 popped
    orig::fn_00458000(ebp, SP(0x4c));
    {
        float a = orig::fn_004581c0(ebp);
        float b = orig::fn_004581c0(ebp) * Fl(SP(0x40));
        Fl(SP(0x40)) = orig::fn_00471750(SP(0x30), SP(0x4c), SP(0x14), 0, fb(b), fb(a));
    }
    D(ebp + 0x39c) = orig::fn_00455a30(SP(0x14), 0x10, ebp, D(ebp + 0x39c), D(SP(0x40)), 0xffffffffu, 0);
    goto done;

    // ---- state 0xf4 (edi = 0)
L_445045:
    t = Fl(ebp + 0x87c) - Fl(SP(0x20));
    Fl(ebp + 0x87c) = t;
    if (!(t <= K(0x0056c05c)))
        goto done;
    D(ebp + 0x878) = edi;
    VC(ebp, 0x1a0, 2u, 3u, edi, 1u, 1u, edi, 0x3f000000u, 0x3f000000u);
    VC(ebp, 0x240, edi, 1u, edi);
    goto done;

    // ---- state 0xdc (edi = 0): follows the leader +0x9bc (guess) at the offset matrix +0x9c0.
L_44509f:
    VC(ebp, 0x54, SP(0x1ec));
    orig::fn_00453c50(D(ebp + 0x9bc), ebp + 0x9c0, SP(0x47c), 1, edi, edi, 1, edi, edi);
    D(SP(0x38)) = D(SP(0x1f4));
    D(SP(0x30)) = D(SP(0x488));
    D(SP(0x34)) = D(SP(0x498));
    orig::fn_00458000(ebp, SP(0x14));
    Call_00471970(SP(0x1ec), SP(0x4c), SP(0x30), 0, edi, fb(Fl(ebp + 0x948) + Fl(ebp + 0x948)), 0x3f800000,
                  edi);  // st0 popped
    if (Fl(SP(0x1c)) > K(0x0056c05c))
        D(SP(0x54)) = 0;
    {
        float a = orig::fn_004581c0(ebp);
        float b = orig::fn_004581c0(ebp) * K(0x005bcd40);
        Fl(SP(0x40)) = orig::fn_00471750(SP(0x4c), SP(0x14), SP(0x30), 0, fb(b), fb(a));
    }
    D(SP(0x38)) = 0;
    D(ebp + 0x39c) = orig::fn_00455a30(SP(0x30), 0x10, ebp, D(ebp + 0x39c), D(SP(0x40)), 0xffffffffu, 0);
    edi = ebp + 0x51c;
    orig::fn_00458000(ebp, edi);
    if (!(orig::fn_00456110(edi, edi, ebp) > K(0x0056c05c)))
        goto L_443fbc;
    if (Fl(ebp + 0x578) > K(0x0056c05c))
        D(ebp + 0x578) = 0x3a83126f;  // 0.001
    D(ebp + 0x524) = 0;
    orig::fn_00454e60(edi, ebp, 0x41800000);
    goto done;

    // ---- state 0x119
L_445204:
    if (!By(SP(0x10)))
        goto done;
    // ---- state 0x13 (esi = game)
L_445210:
    orig::fn_0042ce70(D(esi + 0x748), ebp);
    goto done;

    // ---- state 0x11a (edi = 0)
L_445240:
    if (!By(SP(0x10)))
        goto done;
    VC(ebp, 0x1a0, 2u, 3u, edi, 1u, 1u, edi, 0x3f000000u, 0x3f000000u);
    VC(ebp, 0x1b8, 0x10eu);
    goto done;

    // ---- state 0x19c (esi = game): the object +0x870 (guess: a carried/held object).
L_445280:
    eax = D(ebp + 0x428);
    ebx = ebp + 0x428;
    if (!eax || !(By(eax + 0x1c) & 0x10) || !(By(eax + 0x1c) & 8))
        Fl(ebp + 0x490) = Fl(ebp + 0x490) - Fl(0x00604a90);
    if (Fl(ebp + 0x490) <= K(0x0056c05c))
        orig::fn_0046f440(D(esi + 0xaa4), ebp);
    ecx = D(ebp + 0x870);
    if (!ecx || !VB(ecx, 0xd8))
        goto L_4448a4;
    orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);
    orig::fn_00427210(ebp);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebx)))
        goto done;
    VC(ebp, 0x168);
    Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x3f800000 /* 1.0 */, 0x40000000 /* 2.0 */);
    // virtual +0x1a4 takes one argument (+0x498); the four pushes before it and 0x20001 outlive it and are
    // Anim_Set's arguments 2..6
    eax = VC(ebp, 0x1a4, D(ebp + 0x498));
    orig::fn_00408d90(0xffffffffu, ebp, eax, 0x20001, 0, 0x3e800000, 0, 0);
    VC(ebp, 0x1b8, 0x19du);
    goto done;

    // ---- state 0x19d (edi = 0)
L_445379:
    ecx = D(ebp + 0x870);
    if (ecx == edi || !VB(ecx, 0xd8))
    {
        VC(ebp, 0xd4, edi);
        goto done;
    }
    orig::fn_00458000(ebp, SP(0x30));
    VC(ebp, 0x54, SP(0x120));
    VC(D(ebp + 0x870), 0x54, SP(0x108));
    Fl(SP(0x24)) = Fl(SP(0x108)) - Fl(SP(0x120));
    Fl(SP(0x28)) = Fl(SP(0x10c)) - Fl(SP(0x124));
    Fl(SP(0x2c)) = Fl(SP(0x110)) - Fl(SP(0x128));
    Fl(SP(0x40)) = Normalize3(SP(0x24), SP(0x24));
    Fl(SP(0x24)) = Fl(SP(0x24)) * Fl(ebp + 0x948) * K(0x005bce20);
    Fl(SP(0x28)) = Fl(SP(0x28)) * Fl(ebp + 0x948) * K(0x005bce20);
    if (Fl(ebp + 0x490) <= K(0x0056c05c))
        Fl(SP(0x2c)) = Fl(SP(0x2c)) * Fl(ebp + 0x948) * K(0x005bce20);
    else
    {
        D(SP(0x2c)) = 0;
        t = Fl(ebp + 0x490) - Fl(SP(0x20));
        Fl(ebp + 0x490) = t;
        if (t <= K(0x0056c05c))
        {
            VC(ebp, 0x160);
            VC(ebp, 0x19c, orig::fn_004e7740(0x4a, 0x49), 1u, 0u, 0u, 0u, 0u);
        }
    }
    if (Fl(SP(0x40)) <= K(0x005bcbdc))
        goto L_4448a4;
    {
        float a = orig::fn_004581c0(ebp);
        float b = orig::fn_004581c0(ebp) * K(0x005bcbdc);
        Fl(SP(0x40)) = orig::fn_00471750(SP(0x24), SP(0x30), SP(0x4c), 0, fb(b), fb(a));
    }
    D(ebp + 0x39c) = orig::fn_00455a30(SP(0x4c), 0x10, ebp, D(ebp + 0x39c), D(SP(0x40)), 0xffffffffu, 0);
    goto done;

    // ---- state 0x19b
L_445538:
    if (static_cast<int8_t>(By(ebp + 0x48c)) < 0)
        goto done;
    VC(ebp, 0x54, SP(0x318));
    orig::fn_00453c50(D(GameP() + 0x748), 0x005b68fc, SP(0x438), 1, 0, 0, 1, 0, 0);
    D(SP(0x38)) = D(SP(0x464));
    eax = GameP();
    D(SP(0x30)) = D(SP(0x444));
    D(SP(0x34)) = D(SP(0x454));
    orig::fn_00471a00(SP(0x4c), SP(0x30), SP(0x318), fb(Fl(eax + 0xd1c) * Fl(ebp + 0x948)),
                      fb(Fl(eax + 0xd20) * K(0x005bcd64)), 0);
    orig::fn_00458510(SP(0x4c), ebp);
    if (static_cast<uint8_t>(orig::fn_00454be0(ebp)))
        goto done;
    D(ebp + 0x48c) |= 0x80;
    goto done;

    // ---- state 0x12c
L_44560b:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (!By(SP(0x10)))
        goto done;
    if (!(orig::fn_004555b0(ebp) <= K(0x0056c05c)))
    {
        Fl(ebp + 0x490) = orig::fn_0046e2c0(0, 0x40400000 /* 3.0 */, 0x40e00000 /* 7.0 */);
        VC(ebp, 0x19c, 0x36u, 1u, 0u, 0u, 0u, 0u);
        VC(ebp, 0x1b8, 0x12du);
        goto done;
    }
    D(ebp + 0x9f0) = 0x3f000000;
    D(ebp + 0x9f4) = 0x3f000000;
    VC(ebp, 0x19c, 0x37u, 0u, 0u, 0u, 0u, 0u);
    orig::fn_004577b0(ebp, 1);
    orig::fn_0043e710(ebp, ebp);
    eax = D(ebp + 0x528);
    if (eax)
        orig::fn_00461870(D(GameP() + 0xa84), eax, ebp);
    VC(ebp, 0x1b8, 0x191u);
    goto done;

    // ---- state 0x12d
L_4456f4:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    t = Fl(ebp + 0x490) - Fl(SP(0x20));
    Fl(ebp + 0x490) = t;
    if (!(t <= K(0x0056c05c)))
        goto done;
    VC(ebp, 0x19c, 0x38u, 0u, 0u, 0u, 0u, 0u);
    VC(ebp, 0x1b8, 0x12eu);
    goto done;

    // ---- state 0x12e
L_445759:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (!By(SP(0x10)))
        goto done;
    if (D(ebp + 0x488) & 0x400000)
    {
    L_445789:
        orig::fn_00427210(ebp);
        Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x40400000, 0x40e00000);
        VC(ebp, 0x1b8, 0x97u);
        goto L_445c0b;
    }
    eax = D(ebp + 0x484);
    if (eax & 0x20000)
        goto L_4459e2;
    if (!(eax & 0x2000000))
        goto L_441e5c;
    goto L_445a1a;

    // ---- state 0x12f
L_4457e3:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (!By(SP(0x10)))
        goto done;
    ecx = D(ebp + 0x484);
    if (ecx & 0x1000000)
        goto L_445992;
    {
        float q = orig::fn_004555b0(ebp);
        q = q / orig::fn_004555c0(ebp);
        if (!(Fl(ebp + 0x9e4) * K(0x005bcbec) >= q))
            goto L_4458b5;
    }
    r = orig::fn_004e7730(0x1f4);
    if (r < 0x64)
        edx = 0x4b;
    else if (r < 0xc8)
        edx = 0x4c;
    else if (r < 0x12c)
        edx = 0x4d;
    else if (r < 0x190)
        edx = 0x4e;
    else
        edx = 0x4f;
    VC(ebp, 0x19c, edx, 0u, 0u, 0u, 0u, 0u);
    VC(ebp, 0x1b8, 0x132u);
    goto L_445992;
L_4458b5:
    if (D(ebp + 0x488) & 0x400000)
    {
        orig::fn_00427210(ebp);
        Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x40400000, 0x40e00000);
        VC(ebp, 0x1b8, 0x97u);
        D(ebp + 0x488) &= 0xffbfffffu;
    }
    else if (ecx & 0x20000)
    {
        VC(ebp, 0x19c, 0x66u, 0u, 0u, 0u, 0u, 0u);
        VC(ebp, 0x1b8, 0xa2u);
    }
    else if (ecx & 0x2000000)
    {
        VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
        VC(ebp, 0x1b8, 0x7au);
    }
    else
    {
        VC(ebp, 0x18c, 0u);
        if (By(ebp + 0x484) & 4)
            VC(ebp, 0x184);
        else
            VC(ebp, 0x240, 1u, 0u, 0u);
    }
L_445992:
    D(ebp + 0x484) &= 0xfeffffffu;
    goto done;

    // ---- state 0x132
L_4459a1:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (!By(SP(0x10)))
        goto done;
    if (D(ebp + 0x488) & 0x400000)
        goto L_445789;
    eax = D(ebp + 0x484);
    if (!(eax & 0x20000))
        goto L_445a0e;
L_4459e2:
L_4459e4:
    VC(ebp, 0x19c, 0x66u, 0u, 0u, 0u, 0u, 0u);
    VC(ebp, 0x1b8, 0xa2u);
    goto done;
L_445a0e:
    if (eax & 0x2000000)
        goto L_445a1a;
    VC(ebp, 0x18c, 0u);
    if (!(By(ebp + 0x484) & 4))
        goto L_441e5c;
L_445a5f:
    VC(ebp, 0x184);
    goto done;
L_445a1a:
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(ebp, 0x1b8, 0x7au);
    goto done;

    // ---- states 0x195, 0x197 (edi = 0): taunt cue by distance to the player game +0x748 (guess).
L_445a6d:
    if (D(ebp + 0xb38) == edi)
    {
        VC(ebp, 0x54, SP(0x348));
        VC(D(GameP() + 0x748), 0x54, SP(0x360));
        if (Dist3Sq(SP(0x348), SP(0x360)) <= K(0x005bccb8))
        {
            r = orig::fn_004e7730(0x12c);
            edx = r < 0x64 ? 0x005b279cu : (r < 0xc8 ? 0x005b2788u : 0x005b2774u);
            D(ebp + 0xb38) = VC(D(GameP() + 0x2c), 0xd0, edx, ebp, edi, edi, 0xffffffffu, edi);
        }
    }
    t = orig::fn_00453630(ebp);
    if (t < K(0x005bcb2c))
    {
        t = t + Fl(SP(0x20));
        Fl(SP(0x3c)) = t;
        if (t > K(0x005bcb2c))
            D(SP(0x3c)) = 0x3f800000;
        VC(ebp, 0x100, D(SP(0x3c)));
    }
    t = Fl(ebp + 0x490) - Fl(SP(0x20));
    Fl(ebp + 0x490) = t;
    if (t <= K(0x0056c05c))
    {
        if (By(ebp + 0x48c) & 4)
            orig::fn_00455640(ebp, edi);
        else
            VC(ebp, 0xd4, edi);
        goto done;
    }
    D(SP(0x30)) = 0;
    D(SP(0x34)) = 0;
    D(SP(0x38)) = 0;
    orig::fn_00458580(SP(0x30), ebp);
    goto done;

    // ---- states 0x130, 0x131
L_445bae:
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (!By(SP(0x10)))
        goto done;
    if (D(ebp + 0x488) & 0x400000)
    {
        orig::fn_00427210(ebp);
        Fl(ebp + 0x494) = orig::fn_0046e2c0(0, 0x40400000, 0x40e00000);
        VC(ebp, 0x1b8, 0x97u);
    L_445c0b:
        D(ebp + 0x488) &= 0xffbfffffu;
        goto done;
    }
    eax = D(ebp + 0x484);
    if (eax & 0x20000)
        goto L_442f9a;
    if (eax & 0x2000000)
    {
    L_445c37:
        VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
        VC(ebp, 0x1b8, 0x7au);
        goto done;
    }
    VC(ebp, 0x18c, 0u);
    if (!D(ebp + 0x588) || !(By(ebp + 0x484) & 4))
        goto L_441e5c;
    orig::fn_00428280(SP(0x30), ebp, ebp, 0x005b68fc);
    VC(D(ebp + 0x588), 0x54, SP(0x2c4));
    Fl(SP(0x10)) = Fl(ebp + 0x5d8);
    t = Dist2Sq(SP(0x30), SP(0x2c4));
    if (!(Fl(SP(0x10)) * Fl(SP(0x10)) > t))
        goto L_440f92;
    if (!static_cast<uint8_t>(orig::fn_00426fd0(SP(0x30), ebp, D(ebp + 0x588), 0)))
        goto L_440f92;
L_445cf7:
    VC(ebp, 0x230, 1u);
    goto done;

    // ---- state 0x191
L_445d09:
    orig::fn_00454cc0(ebp, ebp + 0x398);
    if (static_cast<uint8_t>(orig::fn_00454bc0(ebp)))
        orig::fn_00454c90(ebp, 1, 1);
    if (!By(SP(0x10)))
        goto done;
    orig::fn_0042eba0(0 /* ecx: slot only */, ebp);
    goto done;

    // ---- state 0x194 (edi = 0)
L_445d44:
    if (!By(SP(0x10)))
        goto done;
    VC(ebp, 0x17c, edi, 1u);
    goto done;

    // ---- state 0x192 (edi = 0)
L_445d63:
    orig::fn_00458000(ebp, SP(0x30));
    if (!(Fl(SP(0x38)) < K(0x0056c05c)))
        goto done;
    VC(ebp, 0xf8, 0x70001u, edi);
    VC(ebp, 0x1b8, 0x193u);
    goto done;

    // ---- state 0x193 (edi = 0)
L_445da9:
    orig::fn_00458000(ebp, SP(0x14));
    D(SP(0x38)) = 0;
    if (Fl(SP(0x1c)) < K(0x0056c05c))
        D(SP(0x1c)) = 0;
    D(SP(0x30)) = D(SP(0x14));
    D(SP(0x34)) = D(SP(0x18));
    {
        float a = orig::fn_004581c0(ebp);
        float b = orig::fn_004581c0(ebp) * Fl(ebp + 0x944);
        Fl(SP(0x40)) = orig::fn_00471750(SP(0x30), SP(0x14), SP(0x4c), edi, fb(b), fb(a));
    }
    D(ebp + 0x39c) = orig::fn_00455a30(SP(0x4c), 0x10, ebp, D(ebp + 0x39c), D(SP(0x40)), 0xffffffffu, edi);
    goto done;

    // ---- state 0x190 (edi = 0): the ragdoll / death physics hand-over (guess: Body_addConstraints).
L_445e2e:
    if (D(ebp + 0x1bc) == edi)
        goto done;
    if (!(Fl(ebp + 0x9f0) > K(0x0056c05c)))
        goto done;
    if (By(ebp + 0x421))
    {
        orig::fn_00459000(1 /* al = 1 */, ebp, edi, edi, edi);
        orig::fn_0042eba0(0, ebp);
        goto done;
    }
    ebx = 0x10;
    for (edx = 0; static_cast<int32_t>(edx) < 4; ++edx)
    {
        eax = orig::fn_00408800(edx, ebp);
        D(eax + 0x1c) |= ebx;
    }
    orig::fn_00459000(1, ebp, 1, 1, (By(ebp + 0x48c) & ebx) ? 0x10000u : 0u);
    orig::fn_00456570(ebp);
    orig::fn_00459000(1, ebp, 1, 0, (By(ebp + 0x48c) & ebx) ? 0x10000u : 0u);
    if (D(ebp + 0x48c) & 0x800)
    {
        orig::fn_00455f10(ebp, ebp + 0x9f8, D(ebp + 0xa08), D(ebp + 0xa04));
        orig::fn_0042eba0(0, ebp);
        goto done;
    }
    orig::fn_00456050(ebp, ebp + 0x9f8, D(ebp + 0xa04), D(ebp + 0xa08));
    orig::fn_0042eba0(0, ebp);
    goto done;

    // ---- state 0x1f4 (esi = game, edi = 0): faces the player game +0x748 (guess).
L_445f2d:
    if (D(esi + 0x748) == edi)
        goto L_446093;
    VC(ebp, 0x54, SP(0x21c));
    VC(D(GameP() + 0x748), 0x54, SP(0x204));
    esi = ebp + 0x51c;
    Fl(esi) = Fl(SP(0x204)) - Fl(SP(0x21c));
    Fl(esi + 4) = Fl(SP(0x208)) - Fl(SP(0x220));
    orig::fn_004e7780(esi, esi);
    D(ebp + 0x524) = 0;
    orig::fn_00454e60(esi, ebp, 0x41800000);
    if (static_cast<uint8_t>(orig::fn_00470c30(ebp + 0x428)))
    {
        orig::fn_00470aa0(ebp, D(GameP() + 0xaa4), 0);
        orig::fn_00427210(ebp);
        goto done;
    }
    orig::fn_00454c90(ebp, 1, 1);
    orig::fn_00454cc0(ebp, ebp + 0x398);
    eax = GameP();
    if (VB(D(eax + 0x30), 0x68, D(D(eax + 0xa80) + 4), 0x57u, 0x3f000000u))
    {
        VC(D(GameP() + 0x748), 0x54, SP(0x2dc));
        orig::fn_00453150(D(GameP() + 0x748), SP(0x30));
        orig::fn_0046f6d0(ebp, D(GameP() + 0xaa4), SP(0x2dc), 0, 0, 0, 0, 0);
        orig::fn_00427210(ebp);
        goto done;
    }
    VC(ebp, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    goto done;

    // ---- states 0xca, 0xde
L_446093:
    orig::fn_00454c90(ebp, 1, 1);
    goto done;

done:
    r = orig::fn_0042e910(ebp);
    D(ebp + 0x484) &= 0xfffff7ffu;
    return r;
#undef SP
}

static uint32_t GenericAI_Update_Abi(uint32_t ecx)
{
    return GenericAI_Update(ecx);
}
ABI_ENTRY_0043ee90(GenericAI_Update_Entry, GenericAI_Update_Abi)
HOOK(0x0043ee90, "GenericAI_Update", GenericAI_Update_Entry, nullptr);
