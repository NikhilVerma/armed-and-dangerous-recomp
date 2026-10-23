// Recovered: AI-side game code, batch ai_01 (0x00404750-0x00425df0): the alarm generator controller and the alarm
// generator, the bow bridge, the barricade / twiglet generator, the jump marker and the generic AI object. Static
// recovery 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified; virtual methods are named Class_SlotN when the class is known from the vtable's
// slot 1 descriptor ("CLASS(Name)" at descriptor +0x14).
//
// Shared globals (evidence: 0x00404a40 allocates them, 0x00404b50 frees them):
//   0x0063a1f0, 0x0063a1ec, 0x0063a1e8  three object lists (0x004e7010 list objects, 16 bytes each) of the alarm
//                                       generator controller; 0x0063a1f8[3] the three most recent spawn points (each
//                                       holds a reference, +0x28); 0x0063a1f4 the controller's spawn timer (float).
//   Game+0xc5c  the "alarm_generator_controller" object (0x004049a0), Game+0x2710 a spawned-object counter.
#include <cmath>
#include <cstring>
#include <initializer_list>

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
// A float constant of the game's .rdata (0x0056c05c = 0.0, 0x005bcb2c = 1.0, 0x005bcbdc = 5.0, 0x005bce58 =
// 10/32768, 0x005bcbc0 = 1/32768, 0x005bc15c = 0.75).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
inline uint8_t* GameP()
{
    return reinterpret_cast<uint8_t*>(g_game);
}
inline uint32_t G(uint32_t offset)
{
    return Field<uint32_t>(g_game, offset);
}
// 0x00604a90: the frame time (float) the timers count down by.
inline float FrameTime()
{
    return GameGlobal<float>(0x00604a90);
}
// 0x005391cd _rand (0..0x7fff).
inline int32_t Rand()
{
    return static_cast<int32_t>(static_cast<uint32_t>(orig::fn_005391cd()));
}
// Releases one reference (+0x28) of an object; at zero, when virtual +0xd8 says no, 0x00487da0 (eax = object)
// destroys it. The inlined idiom of 0x00404b50, 0x00404e50 and others.
inline void ReleaseRef(uint8_t* o)
{
    if (--Field<int32_t>(o, 0x28) == 0 && !static_cast<uint8_t>(VCall<uint32_t>(o, 0xd8 / 4)))
        orig::fn_00487da0(u(o));
}
// Calls 0x004e7880 (ecx = source vector, edx = destination): normalises a 3-vector and leaves its length in st0
// (fsqrt, then the scale by 1/len when non-zero). symbols/abi.csv lists an eax return; callers use st0.
float Call_004e7880_St0(uint32_t ecx_, uint32_t edx_)
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
// fsin / fcos of a double constant in .rdata, as the original computes them (x87, not the CRT).
inline void SinCosOf(uint32_t address, float* s, float* c)
{
    float vs, vc;
    __asm {
        mov eax, address
        fld qword ptr [eax]
        fsin
        fstp vs
        mov eax, address
        fld qword ptr [eax]
        fcos
        fstp vc
    }
    *s = vs;
    *c = vc;
}
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t fn_00404790(uint8_t* interest, uint8_t* owner, uint8_t* target, uint32_t a0, uint32_t a1, const uint32_t* pos);
uint32_t fn_00404940();
uint32_t fn_004049a0();
uint32_t fn_00404a00(uint32_t eax, uint8_t* obj, uint32_t a0);
uint32_t fn_00404d00(uint8_t* obj, uint32_t a0);
uint32_t fn_00425d30(uint8_t* obj, uint32_t a0);

// ---------------------------------------------------------------------------------------------------------------
// 0x00404750 (eax = vec3 out, ecx = ai_interest object; stack: two out pointers; ret 8): *a0 = +0x478 (owner),
// *a1 = +0x464, out = +0x468..+0x470 (position). The getter of the fields 0x00404790 sets. Caller 0x00426920. Not
// executed in recorded runs.
void fn_00404750(uint32_t* out, uint8_t* obj, uint32_t* a0, uint32_t* a1)
{
    *a0 = Field<uint32_t>(obj, 0x478);
    *a1 = Field<uint32_t>(obj, 0x464);
    out[0] = Field<uint32_t>(obj, 0x468);
    out[1] = Field<uint32_t>(obj, 0x46c);
    out[2] = Field<uint32_t>(obj, 0x470);
}

static uint32_t fn_00404750_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1)
{
    fn_00404750(reinterpret_cast<uint32_t*>(eax), B(ecx), reinterpret_cast<uint32_t*>(a0),
                reinterpret_cast<uint32_t*>(a1));
    return 0;
}
ABI_ENTRY_00404750(fn_00404750_Entry, fn_00404750_Abi)
HOOK(0x00404750, "fn_00404750", fn_00404750_Entry, nullptr);

// 0x00404790 (eax = ai_interest object, ecx = owner, edx = target or 0; stack: a0, a1, position or 0; ret 0xc):
// +0x464 = a0, +0x460 = a1, owner gains a reference (+0x28) and goes to +0x478. With a target: it gains a reference,
// goes to +0x474 and its position (virtual +0x54) fills +0x468. Without: +0x474 = 0 and +0x468 = *position when given,
// else the owner's position. Caller 0x00404810. Not executed in recorded runs.
uint32_t fn_00404790(uint8_t* interest, uint8_t* owner, uint8_t* target, uint32_t a0, uint32_t a1, const uint32_t* pos)
{
    Field<uint32_t>(interest, 0x464) = a0;
    Field<uint32_t>(interest, 0x460) = a1;
    ++Field<int32_t>(owner, 0x28);
    Field<uint8_t*>(interest, 0x478) = owner;
    if (target)
    {
        ++Field<int32_t>(target, 0x28);
        Field<uint8_t*>(interest, 0x474) = target;
        return VCall<uint32_t>(target, 0x54 / 4, interest + 0x468);
    }
    Field<uint32_t>(interest, 0x474) = 0;
    uint32_t* dst = &Field<uint32_t>(interest, 0x468);
    if (pos)
    {
        dst[0] = pos[0];
        dst[1] = pos[1];
        dst[2] = pos[2];
        return u(dst);
    }
    return VCall<uint32_t>(owner, 0x54 / 4, dst);
}

static uint32_t fn_00404790_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00404790(B(eax), B(ecx), B(edx), a0, a1, reinterpret_cast<const uint32_t*>(a2));
}
ABI_ENTRY_00404790(fn_00404790_Entry, fn_00404790_Abi)
HOOK(0x00404790, "fn_00404790", fn_00404790_Entry, nullptr);

// 0x00404810 (ebx = AI object; stack: a0, a1 (floats), target, position; caller pops). Posts an "ai_interest"
// (string 0x005bc040) for the player object: only when ebx is set, 0x0044e600 says no, ebx virtual +0xd8 says yes
// and ebx is Game+0x748 (the player). Searches the list World+0xee0 (iterator 0x004e7340 / 0x004e73c0) for an
// interest with the same target (+0x474, non-zero) and owner (+0x478); found: the iteration ends (0x004e7420) and the
// interest is refreshed (0x00404790) only when its +0x464 < a0 or its +0x460 < a1. Not found: a new "ai_interest"
// object (ObjectSystem_CreateObject 0x0045f020) is set up by 0x00404790. Returns eax as left (the iterator node, or
// the value of the failed test).
uint32_t fn_00404810(uint8_t* ai, uint32_t a0, uint32_t a1, uint32_t target, uint32_t pos)
{
    if (!ai)
        return 0;
    uint32_t r = orig::fn_0044e600();
    if (static_cast<uint8_t>(r))
        return r;
    r = VCall<uint32_t>(ai, 0xd8 / 4);
    if (!static_cast<uint8_t>(r))
        return r;
    if (u(ai) != G(0x748))
        return u(g_game);
    uint32_t iterator[2] = {0, 0};
    uint8_t* found = nullptr;
    for (uint8_t* it = B(orig::fn_004e7340(Field<uint32_t>(P(G(0xa84)), 0xee0), u(iterator))); it;
         it = B(orig::fn_004e73c0(u(iterator))))
    {
        const uint32_t t = Field<uint32_t>(it, 0x474);
        if (t && t == target && Field<uint8_t*>(it, 0x478) == ai)
        {
            found = it;
            break;
        }
    }
    uint8_t* interest = nullptr;
    if (found)
    {
        orig::fn_004e7420(u(iterator));
        if (Field<float>(found, 0x464) < bf(a0) || Field<float>(found, 0x460) < bf(a1))
            interest = found;
    }
    else
    {
        interest = B(orig::fn_0045f020(0x005bc040u, 0, G(0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0));
    }
    if (interest)
        fn_00404790(interest, ai, B(target), a0, a1, reinterpret_cast<const uint32_t*>(pos));
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_00404810_Abi(uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00404810(B(ebx), a0, a1, a2, a3);
}
ABI_ENTRY_00404810(fn_00404810_Entry, fn_00404810_Abi)
HOOK(0x00404810, "fn_00404810", fn_00404810_Entry, nullptr);

// 0x00404940 (no inputs): clears the three recent spawn points 0x0063a1f8[3] (without releasing them) and sets the
// controller's spawn timer 0x0063a1f4 = Game+0xb3c + rand * (Game+0xb40 - Game+0xb3c) / 32768. Returns 0.
uint32_t fn_00404940()
{
    const float hi = Field<float>(g_game, 0xb40);
    const float lo = Field<float>(g_game, 0xb3c);
    const int32_t r = Rand();
    GameGlobal<uint32_t>(0x0063a1f8) = 0;
    GameGlobal<uint32_t>(0x0063a1fc) = 0;
    GameGlobal<uint32_t>(0x0063a200) = 0;
    float t = static_cast<float>(r) * (hi - lo);
    t = t * K(0x005bcbc0);
    GameGlobal<float>(0x0063a1f4) = t + lo;
    return 0;
}

static uint32_t fn_00404940_Abi()
{
    return fn_00404940();
}
ABI_ENTRY_00404940(fn_00404940_Entry, fn_00404940_Abi)
HOOK(0x00404940, "fn_00404940", fn_00404940_Entry, nullptr);

// 0x004049a0 (no inputs): returns the alarm generator controller Game+0xc5c, creating it first when missing: clears
// 0x0063a1e8..0x0063a203 (7 dwords: the three lists, the timer, the spawn points), creates
// "alarm_generator_controller" / "grunt" (0x0045f020), stores it in Game+0xc5c, counts Game+0x2710 and tail-calls
// 0x00404940 (whose 0 is then returned). Caller: AlarmGenerator slot 6 (0x00405490).
uint32_t fn_004049a0()
{
    uint8_t* game = GameP();
    if (Field<uint32_t>(game, 0xc5c))
        return Field<uint32_t>(game, 0xc5c);
    for (uint32_t i = 0; i < 7; ++i)
        GameGlobal<uint32_t>(0x0063a1e8 + 4 * i) = 0;
    const uint32_t obj = orig::fn_0045f020(0x005bc024u, 0x005bc01cu, Field<uint32_t>(game, 0xa84), 0, 0x3ff, 0, 0, 0,
                                           0, 0xaa, 0);
    Field<uint32_t>(g_game, 0xc5c) = obj;
    ++Field<int32_t>(g_game, 0x2710);
    return fn_00404940();
}

static uint32_t fn_004049a0_Abi()
{
    return fn_004049a0();
}
ABI_ENTRY_004049a0(fn_004049a0_Entry, fn_004049a0_Abi)
HOOK(0x004049a0, "fn_004049a0", fn_004049a0_Entry, nullptr);

// 0x00404a00 (al = add, ecx = object; stack: one unused dword, ret 4): puts the object into (al set) or takes it out
// of (al clear) the controller list 0x0063a1f0: 0x00454690 (esi = list, edi = object) tests membership, 0x004546d0
// adds, 0x00454750 removes. Returns the last callee's eax. Caller: AlarmGenerator slot 47 (0x00405680).
uint32_t fn_00404a00(uint32_t eax, uint8_t* obj, uint32_t /*a0*/)
{
    const uint32_t list = GameGlobal<uint32_t>(0x0063a1f0);
    if (static_cast<uint8_t>(eax))
    {
        const uint32_t r = orig::fn_00454690(list, u(obj));
        if (static_cast<uint8_t>(r))
            return r;
        return static_cast<uint32_t>(orig::fn_004546d0(GameGlobal<uint32_t>(0x0063a1f0), u(obj)));
    }
    const uint32_t r = orig::fn_00454690(list, u(obj));
    if (!static_cast<uint8_t>(r))
        return r;
    return orig::fn_00454750(GameGlobal<uint32_t>(0x0063a1f0), u(obj));
}

static uint32_t fn_00404a00_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_00404a00(eax, B(ecx), a0);
}
ABI_ENTRY_00404a00(fn_00404a00_Entry, fn_00404a00_Abi)
HOOK(0x00404a00, "fn_00404a00", fn_00404a00_Entry, nullptr);

// 0x00404a40 (ecx = controller; stack: 4, ret 0x10). Slot 6 of vtable 0x005a6cc0, the class created as
// "alarm_generator_controller" (guess: 0x004049a0 creates it and this slot fills the globals it clears; the vtable
// keeps Object's slot 1, so there is no CLASS() name). Allocates the three lists 0x0063a1f0 / 0x0063a1ec / 0x0063a1e8
// (PMS_heapAlloc 0x004e07f0 (ecx = heap 0x0063a210; 0x10, 0x18, 2, "new") then 0x004e7010 (eax = Game+0xac4, edx = 0,
// esi = block); 0 when the allocation fails), sets +0x460 = 5 + rand * 10/32768 (seconds), the base set-up 0x004517b0
// (same four arguments) and virtual +0xf4 (0). Not executed in recorded runs.
uint32_t fn_00404a40(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    static const uint32_t lists[3] = {0x0063a1f0, 0x0063a1ec, 0x0063a1e8};
    for (uint32_t list : lists)
    {
        const uint32_t block = orig::fn_004e07f0(0x0063a210u, 0x10, 0x18, 2, 0x0058b5e8u);
        if (block)
            GameGlobal<uint32_t>(list) = orig::fn_004e7010(G(0xac4), 0, block);
        else
            GameGlobal<uint32_t>(list) = 0;
    }
    const int32_t r = Rand();
    float t = static_cast<float>(r) * K(0x005bce58);
    Field<float>(obj, 0x460) = t + K(0x005bcbdc);
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    return VCall<uint32_t>(obj, 0xf4 / 4, 0u);
}

static uint32_t fn_00404a40_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00404a40(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00404a40(fn_00404a40_Entry, fn_00404a40_Abi)
HOOK(0x00404a40, "fn_00404a40", fn_00404a40_Entry, nullptr);

// 0x00404b50 (ecx = controller). Slot 55 (+0xdc) of vtable 0x005a6cc0 (the alarm generator controller, see
// 0x00404a40): empties each of the three lists 0x0063a1f0 / 0x0063a1ec / 0x0063a1e8 (0x00454750 per member, one
// iterator shared by the three walks), deletes it (virtual +0x10 (1)) and clears the global; Game+0xc5c = 0; releases
// the three recent spawn points 0x0063a1f8[3]; Object_Remove 0x004547b0 (ecx = controller). Returns the iterator node.
// Not executed in recorded runs.
uint32_t fn_00404b50(uint8_t* obj)
{
    uint32_t iterator[2] = {0, 0};
    static const uint32_t lists[3] = {0x0063a1f0, 0x0063a1ec, 0x0063a1e8};
    for (uint32_t list : lists)
    {
        for (uint32_t it = orig::fn_004e7340(GameGlobal<uint32_t>(list), u(iterator)); it;
             it = orig::fn_004e73c0(u(iterator)))
            orig::fn_00454750(GameGlobal<uint32_t>(list), it);
        void* l = GameGlobal<void*>(list);
        if (l)
            VCall<void>(l, 0x10 / 4, 1u);
        GameGlobal<uint32_t>(list) = 0;
    }
    Field<uint32_t>(g_game, 0xc5c) = 0;
    for (uint32_t i = 0; i < 3; ++i)
    {
        uint8_t*& slot = GameGlobal<uint8_t*>(0x0063a1f8 + 4 * i);
        if (slot)
        {
            ReleaseRef(slot);
            slot = nullptr;
        }
    }
    orig::fn_004547b0(u(obj));
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_00404b50_Abi(uint32_t ecx)
{
    return fn_00404b50(B(ecx));
}
ABI_ENTRY_00404b50(fn_00404b50_Entry, fn_00404b50_Abi)
HOOK(0x00404b50, "fn_00404b50", fn_00404b50_Entry, nullptr);

// 0x00404ca0 (no register inputs; ecx = controller unused). Slot 9 (+0x24) of vtable 0x005a6cc0 (the alarm generator
// controller): removes every member of the list 0x0063a1f0 (0x00454750), then 0x00404940 (resets the timer and the
// spawn points). Returns the iterator node. Not executed in recorded runs.
uint32_t fn_00404ca0()
{
    uint32_t iterator[2] = {0, 0};
    for (uint32_t it = orig::fn_004e7340(GameGlobal<uint32_t>(0x0063a1f0), u(iterator)); it;
         it = orig::fn_004e73c0(u(iterator)))
        orig::fn_00454750(GameGlobal<uint32_t>(0x0063a1f0), it);
    fn_00404940();
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_00404ca0_Abi()
{
    return fn_00404ca0();
}
ABI_ENTRY_00404ca0(fn_00404ca0_Entry, fn_00404ca0_Abi)
HOOK(0x00404ca0, "fn_00404ca0", fn_00404ca0_Entry, nullptr);

// 0x00404d00 (eax = object; stack: one unused dword, ret 4). Whether the object is hidden from the player: false
// unless the player (Game+0x748) virtual +0xf0 (object, 180.0) says yes; then the player's "ref_hit_01" matrix
// (0x00453c50) gives the eye point, the object's position (virtual +0x54) with the eye's height is the ray start, and
// the horizontal direction to the eye is normalised (0x004e7880, length in st0). Ray_CastQuery 0x00414110 (eax =
// start, esi = direction; Game+0xa8c, length + 5, ..., World+0xe7c, World+0xe9c, mask 2): no hit -> true; a hit object
// that exists, virtual +0xe4 and 0x004531e0 say yes -> true when its +0x424 bit 11 is clear; else false.
// Callers: 0x00404e50 (twice). Not executed in recorded runs.
uint32_t fn_00404d00(uint8_t* obj, uint32_t /*a0*/)
{
    uint32_t L[0x74 / 4] = {};  // the original frame: [esp + k] = L[k / 4] after its push esi
    void* player = Field<void*>(g_game, 0x748);
    const uint32_t r = VCall<uint32_t>(player, 0xf0 / 4, u(obj), 0x43340000u);
    if (!static_cast<uint8_t>(r))
        return r;
    orig::fn_00453c50(G(0x748), 0x005bc010u, u(&L[0x30 / 4]), 1, 0, 0, 1, 0, 0);
    L[0xc / 4] = L[0x3c / 4];
    L[0x10 / 4] = L[0x4c / 4];
    L[0x14 / 4] = L[0x5c / 4];
    VCall<void>(obj, 0x54 / 4, &L[0x24 / 4]);
    const float dx = bf(L[0xc / 4]) - bf(L[0x24 / 4]);
    L[0x2c / 4] = L[0x14 / 4];
    L[0x18 / 4] = fb(dx);
    const float dy = bf(L[0x10 / 4]) - bf(L[0x28 / 4]);
    L[0x20 / 4] = 0;
    L[0x1c / 4] = fb(dy);
    const float len = Call_004e7880_St0(u(&L[0x18 / 4]), u(&L[0x18 / 4]));
    uint8_t* world = B(G(0xa84));
    L[0x8 / 4] = fb(len + K(0x005bcbdc));
    L[0x4 / 4] = 0;
    const uint32_t hit = orig::fn_00414110(u(&L[0x24 / 4]), u(&L[0x18 / 4]), G(0xa8c), L[0x8 / 4], u(&L[0x8 / 4]), 0,
                                           u(&L[0x4 / 4]), 0, Field<uint32_t>(world, 0xe7c), 0,
                                           Field<uint32_t>(world, 0xe9c), 0, 0, 0, 2);
    if (!static_cast<uint8_t>(hit))
        return (hit & ~0xffu) | 1;
    uint8_t* other = B(L[0x4 / 4]);
    if (!other)
        return 0;
    uint32_t t = VCall<uint32_t>(other, 0xe4 / 4);
    if (!static_cast<uint8_t>(t))
        return t & ~0xffu;
    t = orig::fn_004531e0(u(other));
    if (!static_cast<uint8_t>(t))
        return t & ~0xffu;
    const uint32_t v = Field<uint32_t>(other, 0x424) >> 11;
    return (v & ~0xffu) | (~v & 1);
}

static uint32_t fn_00404d00_Abi(uint32_t eax, uint32_t a0)
{
    return fn_00404d00(B(eax), a0);
}
ABI_ENTRY_00404d00(fn_00404d00_Entry, fn_00404d00_Abi)
HOOK(0x00404d00, "fn_00404d00", fn_00404d00_Entry, nullptr);

// 0x00404e50 (ecx = controller; no stack arguments read, plain ret: symbols/abi.csv's "stack 9" is an overstatement).
// Slot 47 (+0xbc) of vtable 0x005a6cc0, the alarm generator controller's update:
// 1. +0x460 -= frame time; at <= 0: for each member of the list 0x0063a1e8 (while 0x00454780 (esi = controller) says
//    no) farther than Game+0xd64 from the player (horizontal x/y distance) and not hidden (0x00404d00), sends it a
//    0x34-byte message with flags 0x8000 (virtual +0xd4); +0x460 = 5 + rand * 10/32768.
// 2. When World+0xef0 virtual +0x34 is non-zero or Game+0xdcc is set, Game+0xc80 <= 0, Game+0xcbc and Game+0xadc are
//    clear and Game+0xae0 == 0: the spawn timer 0x0063a1f4 counts down; at <= 0 and while the list 0x0063a1ec
//    (spawned troops, virtual +0x34 = count) holds fewer than Game+0x26d4: picks the nearest generator of the list
//    0x0063a1f0 that is not inside its +0x494 minimum distance, within 0.75 * Game+0xd64 when +0x461 is set and not
//    hidden from the player; generators among the last three spawn points 0x0063a1f8 are skipped (the first such is
//    the fallback, whose spawn-point slot is released). At the generator's "ref_spawn_00" matrix (0x00453c50) a troop
//    type is picked by weight (+0x464.. six weights, total +0x47c, rand * total >> 15 + 1) and created:
//    "grunt", "grunt_regular", "captain" (+ 0x00425d30 (1)), "captain", none ("Generator : unkown troop type\n%d",
//    Debug_Printf 0x00407ff0), "monk"; 0x0042dd50 / 0x0042cc60 pass the generator's tags (+0x480.. +0x490), the troop
//    joins 0x0063a1ec (and 0x0063a1e8 when +0x461), and the generator becomes the newest spawn point. Then the timer
//    restarts as in 0x00404940.
// Original bug: weight slot 4 (num_rocket_captain) has no troop type; that path logs the message and then uses the
// stack slot [esp+0x1c] (the fallback's spawn-point index, or iterator data) as the troop object. Reproduced.
// Returns eax as left (not reproduced: 0).
uint32_t fn_00404e50(uint8_t* obj)
{
    uint32_t S[0xc8 / 4] = {};  // the original frame: [esp + k] = S[k / 4] after its four pushes
    S[0x18 / 4] = u(obj);
    const float t = Field<float>(obj, 0x460) - FrameTime();
    Field<float>(obj, 0x460) = t;
    if (t <= K(0x0056c05c))
    {
        void* player = Field<void*>(g_game, 0x748);
        S[0x1c / 4] = 0;
        S[0x20 / 4] = 0;
        VCall<void>(player, 0x54 / 4, &S[0x28 / 4]);
        for (uint8_t* it = B(orig::fn_004e7340(GameGlobal<uint32_t>(0x0063a1e8), u(&S[0x1c / 4]))); it;
             it = B(orig::fn_004e73c0(u(&S[0x1c / 4]))))
        {
            if (static_cast<uint8_t>(orig::fn_00454780(u(obj))))
                continue;
            VCall<void>(it, 0x54 / 4, &S[0x34 / 4]);
            const float dy = bf(S[0x2c / 4]) - bf(S[0x38 / 4]);
            const float dx = bf(S[0x28 / 4]) - bf(S[0x34 / 4]);
            const float r = Field<float>(g_game, 0xd64);
            const float d2 = dx * dx + dy * dy;
            if (!(r * r < d2))
                continue;
            if (static_cast<uint8_t>(fn_00404d00(it, u(obj))))
                continue;
            for (uint32_t k = 0; k < 0xd; ++k)
                S[0x4c / 4 + k] = 0;
            S[0x4c / 4] |= 0x8000;
            VCall<void>(it, 0xd4 / 4, &S[0x4c / 4]);
        }
        const int32_t r = Rand();
        const float v = static_cast<float>(r) * K(0x005bce58);
        Field<float>(obj, 0x460) = v + K(0x005bcbdc);
        if (S[0x20 / 4])
            --Field<int32_t>(P(S[0x20 / 4]), 0x18);
    }

    void* troopsAll = Field<void*>(P(G(0xa84)), 0xef0);
    if (!VCall<uint32_t>(troopsAll, 0x34 / 4) && !Field<uint8_t>(g_game, 0xdcc))
        return 0;
    if (!(Field<float>(g_game, 0xc80) <= K(0x0056c05c)))
        return 0;
    if (Field<uint8_t>(g_game, 0xcbc))
        return 0;
    if (G(0xadc))
        return 0;
    if (!(Field<float>(g_game, 0xae0) == K(0x0056c05c)))
        return 0;
    const float timer = GameGlobal<float>(0x0063a1f4) - FrameTime();
    GameGlobal<float>(0x0063a1f4) = timer;
    if (!(timer <= K(0x0056c05c)))
        return 0;

    const uint32_t count = VCall<uint32_t>(GameGlobal<void*>(0x0063a1ec), 0x34 / 4);
    if (count < G(0x26d4))
    {
        void* player = Field<void*>(g_game, 0x748);
        S[0x28 / 4] = 0;
        S[0x2c / 4] = 0;
        uint8_t* best = nullptr;
        uint8_t* fallback = nullptr;
        VCall<void>(player, 0x54 / 4, &S[0x40 / 4]);
        for (uint8_t* it = B(orig::fn_004e7340(GameGlobal<uint32_t>(0x0063a1f0), u(&S[0x28 / 4]))); it;
             it = B(orig::fn_004e73c0(u(&S[0x28 / 4]))))
        {
            VCall<void>(it, 0x54 / 4, &S[0x34 / 4]);
            const float dy = bf(S[0x38 / 4]) - bf(S[0x44 / 4]);
            const float dx = bf(S[0x34 / 4]) - bf(S[0x40 / 4]);
            const float d2 = dx * dx + dy * dy;
            S[0x14 / 4] = fb(d2);
            if (best && bf(S[0x14 / 4]) >= bf(S[0x24 / 4]))
                continue;
            const float minDist = Field<float>(it, 0x494);
            if (minDist > K(0x0056c05c) && bf(S[0x14 / 4]) < minDist * minDist)
                continue;
            if (Field<uint8_t>(it, 0x461))
            {
                const float r = Field<float>(g_game, 0xd64) * K(0x005bc15c);
                if (bf(S[0x14 / 4]) > r * r)
                    continue;
            }
            if (static_cast<uint8_t>(fn_00404d00(it, S[0x18 / 4])))
                continue;
            int32_t idx = 2;
            while (idx >= 0 && GameGlobal<uint8_t*>(0x0063a1f8 + 4 * idx) != it)
                --idx;
            if (idx >= 0)
            {
                if (!fallback)
                {
                    fallback = it;
                    S[0x1c / 4] = static_cast<uint32_t>(idx);
                }
                continue;
            }
            S[0x24 / 4] = S[0x14 / 4];
            best = it;
        }
        bool spawn = true;
        if (!best)
        {
            if (!fallback)
            {
                spawn = false;
            }
            else
            {
                uint8_t*& slot = GameGlobal<uint8_t*>(0x0063a1f8 + 4 * S[0x1c / 4]);
                ReleaseRef(slot);
                slot = nullptr;
                best = fallback;
            }
        }
        if (spawn)
        {
            orig::fn_0053814f(u(&S[0x4c / 4]), 0x005bc000u, 0);  // sprintf(name, "ref_spawn_00")
            orig::fn_00453c50(u(best), u(&S[0x4c / 4]), u(&S[0x80 / 4]), 1, 0, 0, 1, 0, 0);
            const int32_t total = Field<int32_t>(best, 0x47c);
            const int32_t product = static_cast<int32_t>(static_cast<uint32_t>(Rand()) * static_cast<uint32_t>(total));
            const uint32_t pick = static_cast<uint32_t>((product >> 15) + 1);  // imul, sar 15, inc
            const uint32_t* w = &Field<uint32_t>(best, 0x464);
            uint32_t sum = w[0];
            uint32_t type = 0;
            while (pick > sum)
            {
                ++w;
                sum += *w;
                ++type;
            }
            uint32_t troop = 0;
            const uint32_t names[6] = {0x005bc01c, 0x005bbfe8, 0x005bbff8, 0x005bbff8, 0, 0x005bbfe0};
            if (type <= 5 && type != 4)
            {
                // "grunt", "grunt_regular", "captain", "captain", -, "monk"
                const uint32_t tag = Field<uint16_t>(B(S[0x18 / 4]), 0x394);
                troop = orig::fn_0045f020(0, names[type], G(0xa84), u(&S[0x80 / 4]), tag, 0, 0, 0, 0, 0xaa, 0);
                if (type == 2)
                    fn_00425d30(B(troop), 1);
                ++Field<int32_t>(g_game, 0x2710);
            }
            else
            {
                orig::fn_00407ff0(G(0x34), 0x005bbfbcu, type, 0);
                troop = S[0x1c / 4];
            }
            orig::fn_0042dd50(Field<uint32_t>(best, 0x480), Field<uint32_t>(best, 0x484), Field<uint32_t>(best, 0x490),
                              troop, u(best));
            orig::fn_0042cc60(troop, Field<uint32_t>(best, 0x488), Field<uint32_t>(best, 0x48c));
            orig::fn_004546d0(GameGlobal<uint32_t>(0x0063a1ec), troop);
            if (Field<uint8_t>(best, 0x461))
                orig::fn_004546d0(GameGlobal<uint32_t>(0x0063a1e8), troop);
            uint8_t*& last = GameGlobal<uint8_t*>(0x0063a200);
            if (last)
            {
                ReleaseRef(last);
                GameGlobal<uint32_t>(0x0063a200) = 0;
            }
            GameGlobal<uint32_t>(0x0063a200) = GameGlobal<uint32_t>(0x0063a1fc);
            GameGlobal<uint32_t>(0x0063a1fc) = GameGlobal<uint32_t>(0x0063a1f8);
            GameGlobal<uint8_t*>(0x0063a1f8) = best;
            ++Field<int32_t>(best, 0x28);
        }
        if (S[0x2c / 4])
            --Field<int32_t>(P(S[0x2c / 4]), 0x18);
    }
    const float hi = Field<float>(g_game, 0xb40);
    const float lo = Field<float>(g_game, 0xb3c);
    const int32_t r = Rand();
    float v = static_cast<float>(r) * (hi - lo);
    v = v * K(0x005bcbc0);
    GameGlobal<float>(0x0063a1f4) = v + lo;
    return 0;
}

static uint32_t fn_00404e50_Abi(uint32_t ecx, uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/,
                                uint32_t /*a4*/, uint32_t /*a5*/, uint32_t /*a6*/, uint32_t /*a7*/, uint32_t /*a8*/)
{
    return fn_00404e50(B(ecx));
}
ABI_ENTRY_00404e50(fn_00404e50_Entry, fn_00404e50_Abi)
HOOK(0x00404e50, "fn_00404e50", fn_00404e50_Entry, nullptr);

// 0x00405490 (ecx = generator; stack: 4, ret 0x10). AlarmGenerator (vtable 0x005a2058) slot 6, the set-up: makes
// sure the controller exists (0x004049a0), the base set-up 0x004517b0 (same arguments), then reads its parameters
// from the block a2 (readers 0x004534a0 / 0x004534f0 / 0x00453400 / 0x00453450; esi = a3, edi = destination;
// stack: generator, name, a2, default...): min_spawn_distance +0x494, max_distance +0x461 (byte), num_bow +0x464,
// num_regular +0x468, num_captain +0x470, num_rocket_captain +0x474, num_jumper_captain +0x46c, num_monk +0x478,
// marker_tag_min +0x480, marker_tag_max +0x484, alert_group +0x490, priority_target_tag_min +0x488,
// priority_target_tag_max +0x48c; the weight total +0x47c = the six counts (stored after each addition, as the
// original does); virtual +0xf4 (0). Not executed in recorded runs.
uint32_t AlarmGenerator_Slot6(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    fn_004049a0();
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    const uint32_t o = u(obj);
    const uint32_t r = orig::fn_004534a0(a3, o + 0x494, o, 0x005bbfa8u, a2, 0, 0);
    orig::fn_004534f0(r & ~0xffu, a3, o + 0x461, o, 0x005bbf98u, a2, 0);
    orig::fn_00453400(1, a3, o + 0x464, o, 0x005bbf90u, a2, 0);
    orig::fn_00453400(1, a3, o + 0x468, o, 0x005bbf84u, a2, 0);
    orig::fn_00453400(1, a3, o + 0x470, o, 0x005bbf78u, a2, 0);
    orig::fn_00453400(0, a3, o + 0x474, o, 0x005bbf64u, a2, 0);
    orig::fn_00453400(0, a3, o + 0x46c, o, 0x005bbf50u, a2, 0);
    orig::fn_00453400(0, a3, o + 0x478, o, 0x005bbf44u, a2, 0);
    orig::fn_00453450(0, 0x005bbf34u, a3, o + 0x480, o, a2, 0);
    orig::fn_00453450(0, 0x005bbf24u, a3, o + 0x484, o, a2, 0);
    orig::fn_00453450(0, 0x005bbf18u, a3, o + 0x490, o, a2, 0);
    orig::fn_00453450(0, 0x005bbf00u, a3, o + 0x488, o, a2, 0);
    orig::fn_00453450(0, 0x005bbee8u, a3, o + 0x48c, o, a2, 0);
    uint32_t& total = Field<uint32_t>(obj, 0x47c);
    total = 0;
    uint32_t sum = Field<uint32_t>(obj, 0x464);
    total = sum;
    sum += Field<uint32_t>(obj, 0x468);
    total = sum;
    sum += Field<uint32_t>(obj, 0x46c);
    total = sum;
    sum += Field<uint32_t>(obj, 0x470);
    total = sum;
    sum += Field<uint32_t>(obj, 0x474);
    total = sum;
    sum = Field<uint32_t>(obj, 0x478) + sum;
    total = sum;
    return VCall<uint32_t>(obj, 0xf4 / 4, 0u);
}

static uint32_t AlarmGenerator_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return AlarmGenerator_Slot6(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00405490(AlarmGenerator_Slot6_Entry, AlarmGenerator_Slot6_Abi)
HOOK(0x00405490, "AlarmGenerator_Slot6", AlarmGenerator_Slot6_Entry, nullptr);

// 0x00405660 (ecx = generator). AlarmGenerator slot 9 (+0x24): clears the "registered" byte +0x460 (see slot 47).
// Not executed in recorded runs.
void AlarmGenerator_Slot9(uint8_t* obj)
{
    Field<uint8_t>(obj, 0x460) = 0;
}

static uint32_t AlarmGenerator_Slot9_Abi(uint32_t ecx)
{
    AlarmGenerator_Slot9(B(ecx));
    return 0;
}
ABI_ENTRY_00405660(AlarmGenerator_Slot9_Entry, AlarmGenerator_Slot9_Abi)
HOOK(0x00405660, "AlarmGenerator_Slot9", AlarmGenerator_Slot9_Entry, nullptr);

// 0x00405670 (ecx = generator). AlarmGenerator slot 8 (+0x20): Object_setPhysicsEnabled 0x004577b0 (eax = generator;
// 1). Not executed in recorded runs.
uint32_t AlarmGenerator_Slot8(uint8_t* obj)
{
    return static_cast<uint32_t>(orig::fn_004577b0(u(obj), 1));
}

static uint32_t AlarmGenerator_Slot8_Abi(uint32_t ecx)
{
    return AlarmGenerator_Slot8(B(ecx));
}
ABI_ENTRY_00405670(AlarmGenerator_Slot8_Entry, AlarmGenerator_Slot8_Abi)
HOOK(0x00405670, "AlarmGenerator_Slot8", AlarmGenerator_Slot8_Entry, nullptr);

// 0x00405680 (ecx = generator). AlarmGenerator slot 47 (+0xbc), the update: the first time (+0x460 clear) puts the
// generator into the controller's list 0x0063a1f0 (0x00404a00, al = 1, stack: Game+0xc5c) and sets +0x460.
// Returns eax as left.
uint32_t AlarmGenerator_Slot47(uint8_t* obj)
{
    if (Field<uint8_t>(obj, 0x460))
        return Field<uint8_t>(obj, 0x460);  // original: al = the byte, upper eax as the caller left it
    const uint32_t r = fn_00404a00((u(g_game) & ~0xffu) | 1, obj, G(0xc5c));
    Field<uint8_t>(obj, 0x460) = 1;
    return r;
}

static uint32_t AlarmGenerator_Slot47_Abi(uint32_t ecx)
{
    return AlarmGenerator_Slot47(B(ecx));
}
ABI_ENTRY_00405680(AlarmGenerator_Slot47_Entry, AlarmGenerator_Slot47_Abi)
HOOK(0x00405680, "AlarmGenerator_Slot47", AlarmGenerator_Slot47_Entry, nullptr);

// 0x00409110 (cdecl; stack: object). A callback of the script command table 0x00409140 (passed to 0x00459390 at
// 0x004097c4, and called directly at 0x004097de): when the object's virtual +0x10c says yes, 0x00431720 (object, 0);
// then virtual +0xdc. Returns al = 1. Not executed in recorded runs.
uint32_t fn_00409110(uint8_t* obj)
{
    if (static_cast<uint8_t>(VCall<uint32_t>(obj, 0x10c / 4)))
        orig::fn_00431720(u(obj), 0);
    const uint32_t r = VCall<uint32_t>(obj, 0xdc / 4);
    return (r & ~0xffu) | 1;
}

static uint32_t fn_00409110_Abi(uint32_t a0)
{
    return fn_00409110(B(a0));
}
ABI_ENTRY_00409110(fn_00409110_Entry, fn_00409110_Abi)
HOOK(0x00409110, "fn_00409110", fn_00409110_Entry, nullptr);

// 0x0040aa00 (ecx = object). PickupObject (vtable 0x005abf48) slot 318 (+0x4f8): Object_DrawGroups 0x0048a540
// (edi = object; Game+0xa78). Not executed in recorded runs.
uint32_t PickupObject_Slot318(uint8_t* obj)
{
    return orig::fn_0048a540(u(obj), G(0xa78));
}

static uint32_t PickupObject_Slot318_Abi(uint32_t ecx)
{
    return PickupObject_Slot318(B(ecx));
}
ABI_ENTRY_0040aa00(PickupObject_Slot318_Entry, PickupObject_Slot318_Abi)
HOOK(0x0040aa00, "PickupObject_Slot318", PickupObject_Slot318_Entry, nullptr);

// 0x0040aa20 (ecx = bridge; stack: 4, ret 0x10). BridgeObject (vtable 0x005aafe8) slot 6, the set-up: builds the
// local matrices A = scale 3 (flags 2) and B = rotation about z by the double 0x005bcf70 (2.3562 = 3pi/4; fsin/fcos;
// flags 1), Matrix_Multiply 0x004e2910 (eax = A; &C, a1) then (eax = B; &A, &C); the base set-up 0x004517b0 (a0, &A,
// a2, a3); Anim_Set 0x00408d90 (eax = -1, esi = bridge; "grunt_bow_stand", 0x81, 0, 0, 0, 0); creates a "grunt_bow"
// (0x0045f020) and, when it exists, attaches it with 0x004539f0 (eax = "ref_lhand", ecx = bridge, edx = grunt;
// "ref_hand_00", 1, 0). Not executed in recorded runs.
uint32_t BridgeObject_Slot6(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    float s, c;
    SinCosOf(0x005bcf70, &s, &c);
    uint32_t A[17] = {};
    uint32_t Bm[17] = {};
    uint32_t C[17];
    A[15] = 0x3f800000;
    A[10] = 0x40400000;
    A[5] = 0x40400000;
    A[0] = 0x40400000;
    A[16] = 2;
    Bm[10] = 0x3f800000;
    Bm[15] = 0x3f800000;
    Bm[16] = 1;
    Bm[0] = fb(c);
    Bm[1] = fb(-s);
    Bm[4] = fb(s);
    Bm[5] = fb(c);
    orig::fn_004e2910(u(A), u(C), a1);
    orig::fn_004e2910(u(Bm), u(A), u(C));
    orig::fn_004517b0(u(obj), a0, u(A), a2, a3);
    orig::fn_00408d90(0xffffffffu, u(obj), 0x005bb9b4u, 0x81, 0, 0, 0, 0);
    const uint32_t grunt = orig::fn_0045f020(0, 0x005bc21cu, G(0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
    if (!grunt)
        return 0;
    return static_cast<uint32_t>(orig::fn_004539f0(0x005bb99cu, u(obj), grunt, 0x005bb9a8u, 1, 0));
}

static uint32_t BridgeObject_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return BridgeObject_Slot6(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0040aa20(BridgeObject_Slot6_Entry, BridgeObject_Slot6_Abi)
HOOK(0x0040aa20, "BridgeObject_Slot6", BridgeObject_Slot6_Entry, nullptr);

// 0x0040ab70 (ecx = bridge). BridgeObject slot 8 (+0x20): +0x462 = 0, +0x463 = 1; when +0x1bc is set also +0x460 = 0,
// +0x461 = 1 and 0x00458600 (esi = bridge). Returns al = 1 or 0x00458600's eax. Not executed in recorded runs.
uint32_t BridgeObject_Slot8(uint8_t* obj)
{
    const uint32_t p = Field<uint32_t>(obj, 0x1bc);
    Field<uint8_t>(obj, 0x462) = 0;
    Field<uint8_t>(obj, 0x463) = 1;
    if (!p)
        return 1;
    Field<uint8_t>(obj, 0x460) = 0;
    Field<uint8_t>(obj, 0x461) = 1;
    return orig::fn_00458600(u(obj));
}

static uint32_t BridgeObject_Slot8_Abi(uint32_t ecx)
{
    return BridgeObject_Slot8(B(ecx));
}
ABI_ENTRY_0040ab70(BridgeObject_Slot8_Entry, BridgeObject_Slot8_Abi)
HOOK(0x0040ab70, "BridgeObject_Slot8", BridgeObject_Slot8_Entry, nullptr);

// 0x0040aba0 (ecx = bridge; stack: message; ret 4). BridgeObject slot 7 (+0x1c): 0x00457810 (eax = bridge: +0x424
// |= 0x40), 0x004527d0 (ecx = bridge; message), then message +0 = 0x70001, +0xd = 1. Not executed in recorded runs.
uint32_t BridgeObject_Slot7(uint8_t* obj, uint8_t* msg)
{
    orig::fn_00457810(u(obj));
    const uint32_t r = orig::fn_004527d0(u(obj), u(msg));
    Field<uint32_t>(msg, 0) = 0x70001;
    Field<uint8_t>(msg, 0xd) = 1;
    return r;
}

static uint32_t BridgeObject_Slot7_Abi(uint32_t ecx, uint32_t a0)
{
    return BridgeObject_Slot7(B(ecx), B(a0));
}
ABI_ENTRY_0040aba0(BridgeObject_Slot7_Entry, BridgeObject_Slot7_Abi)
HOOK(0x0040aba0, "BridgeObject_Slot7", BridgeObject_Slot7_Entry, nullptr);

// 0x0040ad30 (ecx = object; stack: 4, ret 0x10): +0x460 = 0, the base set-up 0x004517b0 (same arguments), then
// 0x004546d0 (esi = World+0xed4, edi = object) adds it to that list. Callers: TwigletGenerator slot 84 (0x0040ad80),
// 0x00496e90.
uint32_t fn_0040ad30(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Field<uint32_t>(obj, 0x460) = 0;
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    return static_cast<uint32_t>(orig::fn_004546d0(Field<uint32_t>(P(G(0xa84)), 0xed4), u(obj)));
}

static uint32_t fn_0040ad30_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0040ad30(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0040ad30(fn_0040ad30_Entry, fn_0040ad30_Abi)
HOOK(0x0040ad30, "fn_0040ad30", fn_0040ad30_Entry, nullptr);

uint32_t fn_0040bf40(uint32_t name, uint32_t owner);

// 0x0040ad80 (ecx = generator; stack: a0, a1, parameter block a2, a3; ret 0x10). TwigletGenerator (vtable 0x005aed80)
// slot 84 (+0x150), the set-up from the level's parameters (all names are the game's strings; readers 0x004534f0
// bool, 0x004534a0 float, 0x00453400 int, 0x00453450 tag, 0x004533a0 string; esi = a3, edi = destination; stack:
// generator, name, a2, default...):
//   0x0040ad30 (base set-up, joins World+0xed4); clears the slot counts, limits, occupants (+0x4b0[32]) and +0x59c;
//   flags +0x460: Bullseye 0x40 (and BullsEyeSize +0x468, default 10), takes_damage 0x1000 with player_damage 0x100,
//   teammate_damage 0x200, badguy_damage 0x400, takes_bullet_damage 0x2000; alert_group +0x530; zeppelin_death 1,
//   window_generator 4, base_defend 0x10; death_delay +0x464, send_trigger_on_death +0x5a8, receive_trigger_kill
//   +0x5ac, score_value +0x5b0, score_value_peasant_building +0x5b4, toss_peasants 0x800, toss_grunts 0x20;
//   "invisible" (when clear: virtual +0xfc (1, 1)); priority_target_tag_min/max +0x56c/+0x570; valid 0x4000;
//   "windows": while the model has a "ref_win_0%d" matrix (0x00453c50) a "village_window" / "DefaultGameObject" is
//   created there (0x0045f020), physics on (0x004577b0) and attached (0x00450980);
//   with "valid" and more than one model part: animation 0x00408e30 (eax = 0x81, ecx = generator, edx = 1), then the
//   slot kinds are counted from the model's matrices ("ref_ground_0%d", "ref_window_0%d", "ref_roof_0%d",
//   "ref_enter", "ref_exit_0%d"; 0x00453b90 tests a name) with limits min(n, 2) (windows unlimited for window
//   generators, enter 1), or one ground slot when virtual +0x138 is 3; the slot total +0x480 and the limit total
//   +0x494. Without slots it leaves World+0xed4. Visible, non base_defend generators join World +0xe90, +0xe94,
//   (+0xea4 when damageable), +0xe9c, +0xea0; others get virtual +0xf8 (0x4000, 0), +0xf4 (0) and +0x424 |= 0x20000;
//   with exit slots or as a window generator: receive_trigger_activate/deactivate/reset +0x534/+0x538/+0x5a4, the
//   six troop counts (num_bow ... num_monk) +0x53c.., release_time_min/max +0x578/+0x57c (defaults 1 / 2; logged "%s
//   has bad release times, setting defaults" when either is <= 0), release_max +0x590, release_amount +0x598,
//   release_count +0x594, marker_tag_min/max +0x580/+0x584, marker_sequence_min/max +0x588/+0x58c and the troop list
//   +0x5a0; otherwise those are zeroed. The counts are saved to +0x554.., the release timer +0x574 = min + rand *
//   (max - min) / 32768. Then the interaction movers (IC_building_tag; trigger_los; IC_window_dive_low_1 ...
//   IC_door_2, each tag set registers a mover with 0x00456180) and a "RewardPoster" attachment name (when given:
//   +0x460 |= 0x8000 and 0x0040bf40 creates the poster). Not executed in recorded runs. The al defaults of the bool
//   reader are passed as 0 / 1 (the original leaves the previous call's upper eax bits; the reader uses al).
uint32_t TwigletGenerator_Slot84(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t Fr[0x8c / 4] = {};  // the original frame: [esp + k] = Fr[k / 4] after its four pushes (+0x7c.. = args)
    Fr[0x7c / 4] = a0;           // reused as the "invisible" / "trigger_los" byte (and rand's value)
    Fr[0x80 / 4] = a1;           // reused as the bool / tag result
    Fr[0x84 / 4] = a2;
    Fr[0x88 / 4] = a3;
    uint32_t* tmp = &Fr[0x80 / 4];
    uint32_t* tmp2 = &Fr[0x7c / 4];
    uint32_t* name = &Fr[0x14 / 4];
    uint32_t* matrix = &Fr[0x34 / 4];
    const uint32_t o = u(obj);
    auto readBool = [&](uint32_t def, uint32_t key, uint32_t* dst) {
        orig::fn_004534f0(def, a3, u(dst), o, key, a2, 0);
        return static_cast<uint8_t>(*dst) != 0;
    };
    auto readFloat = [&](uint32_t off, uint32_t key, uint32_t def) {
        orig::fn_004534a0(a3, o + off, o, key, a2, def, 0);
    };
    auto readTag = [&](uint32_t dst, uint32_t key) { orig::fn_00453450(0, key, a3, dst, o, a2, 0); };
    auto readInt = [&](uint32_t def, uint32_t off, uint32_t key) {
        orig::fn_00453400(def, a3, o + off, o, key, a2, 0);
    };
    auto setFlag = [&](uint32_t bit) { Field<uint32_t>(obj, 0x460) |= bit; };

    fn_0040ad30(obj, a0, a1, a2, a3);
    for (uint32_t off : {0x46cu, 0x470u, 0x474u, 0x478u, 0x47cu, 0x49cu, 0x4a0u, 0x4a4u, 0x4a8u, 0x498u, 0x4acu, 0x59cu})
        Field<uint32_t>(obj, off) = 0;
    for (uint32_t i = 0; i < 0x20; ++i)
        Field<uint32_t>(obj, 0x4b0 + 4 * i) = 0;
    readBool(0, 0x005bb940, tmp);  // Bullseye
    readFloat(0x468, 0x005bb930, 0x41200000);
    if (static_cast<uint8_t>(*tmp))
        setFlag(0x40);
    if (readBool(1, 0x005bb920, tmp))  // takes_damage
    {
        setFlag(0x1000);
        if (readBool(1, 0x005bb910, tmp))
            setFlag(0x100);
        if (readBool(1, 0x005bb900, tmp))
            setFlag(0x200);
        if (readBool(0, 0x005bb8f0, tmp))
            setFlag(0x400);
        if (readBool(0, 0x005bb8dc, tmp))
            setFlag(0x2000);
    }
    readTag(o + 0x530, 0x005bbf18);  // alert_group
    if (readBool(0, 0x005bb8cc, tmp))
        setFlag(1);
    if (readBool(0, 0x005bb8b8, tmp))
        setFlag(4);
    if (readBool(0, 0x005bb8ac, tmp))
        setFlag(0x10);
    readFloat(0x464, 0x005bb8a0, 0);
    readTag(o + 0x5a8, 0x005bb888);
    readTag(o + 0x5ac, 0x005bb870);
    readFloat(0x5b0, 0x005bb864, 0);
    readFloat(0x5b4, 0x005bb844, 0);
    if (readBool(0, 0x005bb834, tmp))
        setFlag(0x800);
    if (readBool(1, 0x005bb828, tmp))
        setFlag(0x20);
    if (!readBool(0, 0x005bba58, tmp2))  // invisible
        VCall<void>(obj, 0xfc / 4, 1u, 1u);
    readTag(o + 0x56c, 0x005bbf00);
    readTag(o + 0x570, 0x005bbee8);
    if (readBool(0, 0x005bb820, tmp))  // valid
        setFlag(0x4000);
    if (readBool(0, 0x005bb818, tmp) &&  // windows
        Field<uint32_t>(Field<void*>(Field<void*>(obj, 0x414), 0x10), 0x24) > 1)
    {
        uint32_t index = 0;
        orig::fn_0053814f(u(name), 0x005bb80cu, 0);
        uint32_t found = orig::fn_00453c50(o, u(name), u(matrix), 1, 0, 1, 1, 0, 0);
        while (static_cast<uint8_t>(found))
        {
            const uint32_t window = orig::fn_0045f020(0x005bb7f8u, 0x005bb7e8u, G(0xa84), u(matrix), 0x3ff, 0, 0, 0, 0,
                                                      0xaa, 0);
            if (window)
            {
                orig::fn_004577b0(window, 1);
                orig::fn_00450980(o, window);
            }
            ++index;
            orig::fn_0053814f(u(name), 0x005bb80cu, index);
            found = orig::fn_00453c50(o, u(name), u(matrix), 1, 0, 1, 1, 0, 0);
        }
    }
    if ((Field<uint32_t>(obj, 0x460) & 0x4000) &&
        Field<uint32_t>(Field<void*>(Field<void*>(obj, 0x414), 0x10), 0x24) > 1)
    {
        orig::fn_00408e30(0x81, o, 1, 0, 0, 0, 0, 0);
        if (VCall<uint32_t>(obj, 0x138 / 4) == 3)
        {
            Field<uint32_t>(obj, 0x46c) = 1;
            Field<uint32_t>(obj, 0x484) = 1;
        }
        else
        {
            // Counts the matrices name_0<n> from the stored count upwards.
            auto countKind = [&](uint32_t off, uint32_t fmt) {
                orig::fn_0053814f(u(name), fmt, Field<uint32_t>(obj, off));
                while (static_cast<uint8_t>(orig::fn_00453b90(0, o, u(name))))
                {
                    const uint32_t n = Field<uint32_t>(obj, off) + 1;
                    Field<uint32_t>(obj, off) = n;
                    orig::fn_0053814f(u(name), fmt, n);
                }
            };
            countKind(0x46c, 0x005bb7d8);  // ref_ground_0%d
            const uint32_t ground = Field<uint32_t>(obj, 0x46c);
            Field<uint32_t>(obj, 0x484) = ground > 2 ? 2 : ground;
            countKind(0x470, 0x005bb7c8);  // ref_window_0%d
            const uint32_t windows = Field<uint32_t>(obj, 0x470);
            Field<uint32_t>(obj, 0x488) = (windows > 2 && !(Field<uint8_t>(obj, 0x460) & 4)) ? 2 : windows;
            countKind(0x474, 0x005bb7b8);  // ref_roof_0%d
            const uint32_t roofs = Field<uint32_t>(obj, 0x474);
            Field<uint32_t>(obj, 0x48c) = roofs > 2 ? 2 : roofs;
            orig::fn_0053814f(u(name), 0x005bb7acu, 0);  // ref_enter
            if (static_cast<uint8_t>(orig::fn_00453b90(0, o, u(name))))
                ++Field<uint32_t>(obj, 0x478);
            Field<uint32_t>(obj, 0x490) = 1;
            countKind(0x47c, 0x005bb79c);  // ref_exit_0%d
        }
        Field<uint32_t>(obj, 0x480) = Field<uint32_t>(obj, 0x478) + Field<uint32_t>(obj, 0x470) +
                                      Field<uint32_t>(obj, 0x47c) + Field<uint32_t>(obj, 0x474) +
                                      Field<uint32_t>(obj, 0x46c);
        Field<uint32_t>(obj, 0x494) = Field<uint32_t>(obj, 0x490) + Field<uint32_t>(obj, 0x48c) +
                                      Field<uint32_t>(obj, 0x488) + Field<uint32_t>(obj, 0x484);
    }
    else
    {
        Field<uint32_t>(obj, 0x480) = 0;
    }
    if (!Field<uint32_t>(obj, 0x480))
        orig::fn_00454750(Field<uint32_t>(P(G(0xa84)), 0xed4), o);
    if (!(Field<uint8_t>(obj, 0x460) & 0x10) && !static_cast<uint8_t>(*tmp2))
    {
        orig::fn_004546d0(Field<uint32_t>(P(G(0xa84)), 0xe90), o);
        orig::fn_004546d0(Field<uint32_t>(P(G(0xa84)), 0xe94), o);
        if (Field<uint32_t>(obj, 0x460) & 0x1000)
            orig::fn_004546d0(Field<uint32_t>(P(G(0xa84)), 0xea4), o);
        orig::fn_004546d0(Field<uint32_t>(P(G(0xa84)), 0xe9c), o);
        orig::fn_004546d0(Field<uint32_t>(P(G(0xa84)), 0xea0), o);
    }
    else
    {
        VCall<void>(obj, 0xf8 / 4, 0x4000u, 0u);
        VCall<void>(obj, 0xf4 / 4, 0u);
        Field<uint32_t>(obj, 0x424) |= 0x20000;
    }
    if (!Field<uint32_t>(obj, 0x47c) && !(Field<uint8_t>(obj, 0x460) & 4))
    {
        for (uint32_t off : {0x534u, 0x5a4u, 0x53cu, 0x540u, 0x548u, 0x54cu, 0x544u, 0x550u, 0x578u, 0x57cu, 0x580u,
                             0x584u, 0x5a0u})
            Field<uint32_t>(obj, off) = 0;
    }
    else
    {
        readTag(o + 0x534, 0x005bba00);
        readTag(o + 0x538, 0x005bb780);
        readTag(o + 0x5a4, 0x005bb768);
        readInt(0, 0x53c, 0x005bbf90);  // num_bow
        readInt(0, 0x540, 0x005bbf84);  // num_regular
        readInt(0, 0x548, 0x005bbf78);  // num_captain
        readInt(0, 0x54c, 0x005bbf64);  // num_rocket_captain
        readInt(0, 0x544, 0x005bbf50);  // num_jumper_captain
        readInt(0, 0x550, 0x005bbf44);  // num_monk
        readFloat(0x578, 0x005bb754, 0x3f800000);
        readFloat(0x57c, 0x005bb740, 0x40000000);
        if (Field<float>(obj, 0x578) <= K(0x0056c05c) || Field<float>(obj, 0x57c) <= K(0x0056c05c))
        {
            const uint32_t objName = orig::fn_00453270(o);
            orig::fn_00407ff0(G(0x34), 0x005bb714u, objName, 0);
            Field<uint32_t>(obj, 0x578) = 0x3f800000;
            Field<uint32_t>(obj, 0x57c) = 0x40000000;
        }
        readInt(3, 0x590, 0x005bb708);
        readInt(3, 0x598, 0x005bb6f8);
        readInt(0, 0x594, 0x005bb6e8);
        readInt(0, 0x580, 0x005bbf34);
        readInt(0, 0x584, 0x005bbf24);
        readInt(0, 0x588, 0x005bb6d4);
        readInt(0, 0x58c, 0x005bb6c0);
        const uint32_t block = orig::fn_004e07f0(0x0063a210u, 0x10, 0x18, 2, 0x0058b5e8u);
        Field<uint32_t>(obj, 0x5a0) = block ? orig::fn_004e7010(G(0xac4), 0, block) : 0;
    }
    std::memcpy(obj + 0x554, obj + 0x53c, 6 * 4);
    Fr[0x10 / 4] = Field<uint32_t>(obj, 0x57c);
    *tmp = Field<uint32_t>(obj, 0x578);
    *tmp2 = static_cast<uint32_t>(Rand());
    {
        float t = static_cast<float>(static_cast<int32_t>(*tmp2)) * (bf(Fr[0x10 / 4]) - bf(*tmp));
        t = t * K(0x005bcbc0);
        Field<float>(obj, 0x574) = t + bf(*tmp);
    }
    readTag(u(tmp), 0x005bb6a8);              // IC_window_dive_low_1
    readTag(u(&Fr[0x10 / 4]), 0x005bb698);    // IC_building_tag
    readBool(0, 0x005bb68c, tmp2);            // trigger_los
    // The interaction movers: {tag key, reference name, mover name, uses IC_building_tag}.
    static const uint32_t movers[15][4] = {
        {0x005bb6a8, 0x005bb654, 0x005bb670, 0}, {0x005bb63c, 0x005bb600, 0x005bb61c, 0},
        {0x005bb5f0, 0x005bb5c0, 0x005bb5d8, 0}, {0x005bb5ac, 0x005bb570, 0x005bb590, 1},
        {0x005bb558, 0x005bb570, 0x005bb538, 1}, {0x005bb520, 0x005bb570, 0x005bb500, 1},
        {0x005bb4e8, 0x005bb570, 0x005bb4cc, 1}, {0x005bb4b4, 0x005bb484, 0x005bb49c, 0},
        {0x005bb46c, 0x005bb43c, 0x005bb454, 0}, {0x005bb424, 0x005bb3f4, 0x005bb40c, 0},
        {0x005bb3e0, 0x005bb3c8, 0x005bb49c, 0}, {0x005bb3b4, 0x005bb39c, 0x005bb454, 0},
        {0x005bb388, 0x005bb370, 0x005bb40c, 0}, {0x005bb364, 0x005bb33c, 0x005bb350, 0},
        {0x005bb330, 0x005bb308, 0x005bb31c, 0}};
    for (uint32_t k = 0; k < 15; ++k)
    {
        if (k)
            readTag(u(tmp), movers[k][0]);
        const uint32_t tag = *tmp;
        if (tag)
            orig::fn_00456180(movers[k][1], o, 0x005bbfe8u, movers[k][2], tag, movers[k][3] ? Fr[0x10 / 4] : 0, *tmp2,
                              Field<uint32_t>(obj, 0x56c), Field<uint32_t>(obj, 0x570));
    }
    const uint32_t r = orig::fn_004533a0(0x0058a9a4u, u(name), o, 0x005bb2f8u, a2, a3, 0);  // RewardPoster
    if (r != 1)
        return r;
    Field<uint32_t>(obj, 0x460) |= 0x8000;
    return fn_0040bf40(u(name), o);
}

static uint32_t TwigletGenerator_Slot84_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return TwigletGenerator_Slot84(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0040ad80(TwigletGenerator_Slot84_Entry, TwigletGenerator_Slot84_Abi)
HOOK(0x0040ad80, "TwigletGenerator_Slot84", TwigletGenerator_Slot84_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// The barricade (vtable 0x005a7068, BarricadeObject) and the twiglet generator (vtable 0x005aed80, TwigletGenerator)
// share these methods. Both have numbered "slots" (places a troop can occupy): indices [0, +0x46c) are ground slots,
// then +0x470 window, +0x474 roof, +0x478 enter and +0x47c exit/spawn slots (names from 0x0040c280's
// "ref_ground_0%d" / "ref_window_0%d" / "ref_roof_0%d" / "ref_enter" / "ref_exit_0%d"). +0x49c..+0x4a8 count the
// occupied slots of the first four kinds (limits +0x484..+0x490), +0x4ac is the occupied-slot bit mask, +0x498 the
// occupied count and +0x4b0[] the occupant per slot.

// 0x0040bf40 (ebx = attachment name; stack: owner; ret 4). Caller: TwigletGenerator slot 84 (0x0040ad80). When the
// owner's matrix of that name exists (0x00453c50 returns al != 0), creates a "prop_rewardposter" there (0x0045f020),
// adds it to World+0xed8 (0x004546d0) and attaches it (0x004539f0, eax = name, ecx = owner, edx = poster; 0, 1, 0).
// Returns the last callee's eax. Not executed in recorded runs.
uint32_t fn_0040bf40(uint32_t name, uint32_t owner)
{
    uint32_t m[17];
    const uint32_t r = orig::fn_00453c50(owner, name, u(m), 1, 0, 0, 1, 0, 0);
    if (!static_cast<uint8_t>(r))
        return r;
    const uint32_t poster = orig::fn_0045f020(0, 0x005bb2e4u, G(0xa84), u(m), 0x3ff, 0, 0, 0, 0, 0xaa, 0);
    if (!poster)
        return 0;
    orig::fn_004546d0(Field<uint32_t>(P(G(0xa84)), 0xed8), poster);
    return static_cast<uint32_t>(orig::fn_004539f0(name, owner, poster, 0, 1, 0));
}

static uint32_t fn_0040bf40_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_0040bf40(ebx, a0);
}
ABI_ENTRY_0040bf40(fn_0040bf40_Entry, fn_0040bf40_Abi)
HOOK(0x0040bf40, "fn_0040bf40", fn_0040bf40_Entry, nullptr);

// 0x0040bfd0 (ecx = object; stack: slot; ret 4). BarricadeObject slot 88 (+0x160) / TwigletGenerator slot 166:
// marks a slot unusable: +0x4ac |= 1 << slot. Returns the object. Not executed in recorded runs.
uint32_t BarricadeObject_Slot88(uint8_t* obj, uint32_t slot)
{
    Field<uint32_t>(obj, 0x4ac) |= 1u << (slot & 31);
    return u(obj);
}

static uint32_t BarricadeObject_Slot88_Abi(uint32_t ecx, uint32_t a0)
{
    return BarricadeObject_Slot88(B(ecx), a0);
}
ABI_ENTRY_0040bfd0(BarricadeObject_Slot88_Entry, BarricadeObject_Slot88_Abi)
HOOK(0x0040bfd0, "BarricadeObject_Slot88", BarricadeObject_Slot88_Entry, nullptr);

// 0x0040bff0 (ecx = object). BarricadeObject slot 8 (+0x20) / TwigletGenerator slot 86: for each slot (count =
// virtual +0x144) of kind 1 (virtual +0x154, ground): virtual +0x14c (&a, &b, slot, object, 0, &c) gives a point,
// 0x0046f320 (esi = Game+0xaa4; a, b, &x, &y) maps it and 0x0046f2e0 (eax = Game+0xaa4, ecx = x, edx = y) tests it;
// when either fails the slot is marked unusable (virtual +0x160 = slot 88). Returns the last count (guess: walkable
// ground check). Not executed in recorded runs.
uint32_t BarricadeObject_Slot8(uint8_t* obj)
{
    uint32_t S[0x6c / 4] = {};  // the original frame: [esp + k] = S[k / 4] after push ebx, edi
    uint32_t i = 0;
    uint32_t n = VCall<uint32_t>(obj, 0x144 / 4);
    if (!n)
        return n;
    do
    {
        if (VCall<uint32_t>(obj, 0x154 / 4, i) == 1)
        {
            VCall<void>(obj, 0x14c / 4, &S[0x10 / 4], &S[0x1c / 4], i, u(obj), 0u, &S[0x28 / 4]);
            const uint32_t map = G(0xaa4);
            const uint32_t r = orig::fn_0046f320(map, S[0x10 / 4], S[0x14 / 4], u(&S[0xc / 4]), u(&S[0x8 / 4]));
            if (!static_cast<uint8_t>(r) || !static_cast<uint8_t>(orig::fn_0046f2e0(map, S[0xc / 4], S[0x8 / 4])))
                VCall<void>(obj, 0x160 / 4, i);
        }
        ++i;
        n = VCall<uint32_t>(obj, 0x144 / 4);
    } while (i < n);
    return n;
}

static uint32_t BarricadeObject_Slot8_Abi(uint32_t ecx)
{
    return BarricadeObject_Slot8(B(ecx));
}
ABI_ENTRY_0040bff0(BarricadeObject_Slot8_Entry, BarricadeObject_Slot8_Abi)
HOOK(0x0040bff0, "BarricadeObject_Slot8", BarricadeObject_Slot8_Entry, nullptr);

// 0x0040c0a0 (ecx = object; stack: message; ret 4). BarricadeObject slot 7 (+0x1c) / TwigletGenerator slot 85:
// 0x004527d0 (ecx = object; message), message +0xd = 1, message +0 = 0x100 when 0x0044e600 returns al != 0, else 4.
uint32_t BarricadeObject_Slot7(uint8_t* obj, uint8_t* msg)
{
    orig::fn_004527d0(u(obj), u(msg));
    Field<uint8_t>(msg, 0xd) = 1;
    const uint32_t v = static_cast<uint8_t>(orig::fn_0044e600()) ? 0x100u : 4u;
    Field<uint32_t>(msg, 0) = v;
    return v;
}

static uint32_t BarricadeObject_Slot7_Abi(uint32_t ecx, uint32_t a0)
{
    return BarricadeObject_Slot7(B(ecx), B(a0));
}
ABI_ENTRY_0040c0a0(BarricadeObject_Slot7_Entry, BarricadeObject_Slot7_Abi)
HOOK(0x0040c0a0, "BarricadeObject_Slot7", BarricadeObject_Slot7_Entry, nullptr);

// 0x0040c0d0 (ecx = object). BarricadeObject slot 80 (+0x140) / TwigletGenerator slot 158: returns +0x494. Not
// executed in recorded runs.
uint32_t BarricadeObject_Slot80(uint8_t* obj)
{
    return Field<uint32_t>(obj, 0x494);
}

static uint32_t BarricadeObject_Slot80_Abi(uint32_t ecx)
{
    return BarricadeObject_Slot80(B(ecx));
}
ABI_ENTRY_0040c0d0(BarricadeObject_Slot80_Entry, BarricadeObject_Slot80_Abi)
HOOK(0x0040c0d0, "BarricadeObject_Slot80", BarricadeObject_Slot80_Entry, nullptr);

// 0x0040c0e0 (ecx = object). BarricadeObject slot 79 (+0x13c) / TwigletGenerator slot 157: returns +0x498 (the
// occupied-slot count, see 0x0040c400). Not executed in recorded runs.
uint32_t BarricadeObject_Slot79(uint8_t* obj)
{
    return Field<uint32_t>(obj, 0x498);
}

static uint32_t BarricadeObject_Slot79_Abi(uint32_t ecx)
{
    return BarricadeObject_Slot79(B(ecx));
}
ABI_ENTRY_0040c0e0(BarricadeObject_Slot79_Entry, BarricadeObject_Slot79_Abi)
HOOK(0x0040c0e0, "BarricadeObject_Slot79", BarricadeObject_Slot79_Entry, nullptr);

// 0x0040c0f0 (ecx = object). BarricadeObject slot 81 (+0x144) / TwigletGenerator slot 159: returns +0x480 (used as
// the slot count by 0x0040bff0 / 0x0040c740). Not executed in recorded runs.
uint32_t BarricadeObject_Slot81(uint8_t* obj)
{
    return Field<uint32_t>(obj, 0x480);
}

static uint32_t BarricadeObject_Slot81_Abi(uint32_t ecx)
{
    return BarricadeObject_Slot81(B(ecx));
}
ABI_ENTRY_0040c0f0(BarricadeObject_Slot81_Entry, BarricadeObject_Slot81_Abi)
HOOK(0x0040c0f0, "BarricadeObject_Slot81", BarricadeObject_Slot81_Entry, nullptr);

// 0x0040c100 (ecx = object; stack: slot; ret 4). BarricadeObject slot 82 (+0x148) / TwigletGenerator slot 160:
// whether a slot is free: false when its bit of +0x4ac is set or it has an occupant (+0x4b0[slot]); ground slots:
// count +0x49c < limit +0x484; all others need health ratio 0x004555b0 / 0x004555c0 (+0x454 / +0x458) >= 0.25, then
// window +0x4a0 < +0x488, roof +0x4a4 < +0x48c, enter +0x4a8 < +0x490, exit: true; beyond the last kind false.
// Returns al (upper eax bits as the original leaves them where cheap). Not executed in recorded runs.
uint32_t BarricadeObject_Slot82(uint8_t* obj, uint32_t slot)
{
    const uint32_t mask = 1u << (slot & 31);
    if (Field<uint32_t>(obj, 0x4ac) & mask)
        return mask & ~0xffu;
    const uint32_t occupant = Field<uint32_t>(obj, 0x4b0 + 4 * slot);
    if (occupant)
        return occupant & ~0xffu;
    const uint32_t n0 = Field<uint32_t>(obj, 0x46c);
    if (slot < n0)
        return Field<uint32_t>(obj, 0x49c) < Field<uint32_t>(obj, 0x484) ? 1u : 0u;
    const float health = orig::fn_004555b0(u(obj));
    const float maxHealth = orig::fn_004555c0(u(obj));
    if (health / maxHealth < K(0x005bcbe4))
        return 0;
    const uint32_t n1 = Field<uint32_t>(obj, 0x470);
    if (slot < n1 + n0)
        return Field<uint32_t>(obj, 0x4a0) < Field<uint32_t>(obj, 0x488) ? 1u : 0u;
    const uint32_t n2 = Field<uint32_t>(obj, 0x474);
    if (slot < n2 + n1 + n0)
        return Field<uint32_t>(obj, 0x4a4) < Field<uint32_t>(obj, 0x48c) ? 1u : 0u;
    const uint32_t n3 = Field<uint32_t>(obj, 0x478);
    if (slot < n3 + n2 + n1 + n0)
        return Field<uint32_t>(obj, 0x4a8) < Field<uint32_t>(obj, 0x490) ? 1u : 0u;
    const uint32_t total = Field<uint32_t>(obj, 0x47c) + n3 + n2 + n1 + n0;
    return (n1 & ~0xffu) | (slot < total ? 1u : 0u);
}

static uint32_t BarricadeObject_Slot82_Abi(uint32_t ecx, uint32_t a0)
{
    return BarricadeObject_Slot82(B(ecx), a0);
}
ABI_ENTRY_0040c100(BarricadeObject_Slot82_Entry, BarricadeObject_Slot82_Abi)
HOOK(0x0040c100, "BarricadeObject_Slot82", BarricadeObject_Slot82_Entry, nullptr);

// 0x0040c1f0 (ecx = object; stack: slot; ret 4). BarricadeObject slot 85 (+0x154) / TwigletGenerator slot 163: the
// slot's kind: ground 1, window 3, roof 4, enter 6, exit 5, beyond 0. Not executed in recorded runs.
uint32_t BarricadeObject_Slot85(uint8_t* obj, uint32_t slot)
{
    const uint32_t n0 = Field<uint32_t>(obj, 0x46c);
    if (slot < n0)
        return 1;
    const uint32_t n1 = Field<uint32_t>(obj, 0x470);
    if (slot < n1 + n0)
        return 3;
    const uint32_t n2 = Field<uint32_t>(obj, 0x474);
    if (slot < n2 + n1 + n0)
        return 4;
    const uint32_t n3 = Field<uint32_t>(obj, 0x478);
    if (slot < n3 + n2 + n1 + n0)
        return 6;
    return slot < Field<uint32_t>(obj, 0x47c) + n3 + n2 + n1 + n0 ? 5u : 0u;
}

static uint32_t BarricadeObject_Slot85_Abi(uint32_t ecx, uint32_t a0)
{
    return BarricadeObject_Slot85(B(ecx), a0);
}
ABI_ENTRY_0040c1f0(BarricadeObject_Slot85_Entry, BarricadeObject_Slot85_Abi)
HOOK(0x0040c1f0, "BarricadeObject_Slot85", BarricadeObject_Slot85_Entry, nullptr);

// 0x0040c280 (ecx = object; stack: position out, direction out, slot, a3, a4 (unused), matrix buffer or 0; ret 0x18).
// TwigletGenerator slot 161: the slot's attachment matrix (0x00453c50) named by kind ("ref_ground_0%d",
// "ref_window_0%d", "ref_roof_0%d" with the index inside the kind, "ref_enter", "ref_exit_0%d"); position =
// translation (m[3], m[7], m[11]), direction = -(m[1], m[5], m[9]). Beyond the last kind 0x00453c50 gets name 0.
// Original quirk: that last path reads the result from the local matrix even when a buffer was passed (then the
// local is uninitialised; here it is zero). Returns the direction pointer. Not executed in recorded runs.
uint32_t TwigletGenerator_Slot161(uint8_t* obj, uint32_t* pos, float* dir, uint32_t slot, uint32_t /*a3*/,
                                  uint32_t /*a4*/, uint32_t* buffer)
{
    uint32_t name[0x20 / 4] = {};
    uint32_t local[0x44 / 4] = {};  // a matrix with its flags word: 0x00453c50 writes 0x44 bytes (original frame [esp+0x30], 0x44)
    uint32_t* m = buffer ? buffer : local;
    const uint32_t n0 = Field<uint32_t>(obj, 0x46c);
    const uint32_t n1 = Field<uint32_t>(obj, 0x470);
    const uint32_t n2 = Field<uint32_t>(obj, 0x474);
    const uint32_t n3 = Field<uint32_t>(obj, 0x478);
    if (slot < n0)
        orig::fn_0053814f(u(name), 0x005bb7d8u, slot);
    else if (slot < n1 + n0)
        orig::fn_0053814f(u(name), 0x005bb7c8u, slot - n0);
    else if (slot < n2 + n1 + n0)
        orig::fn_0053814f(u(name), 0x005bb7b8u, slot - n1 - n0);
    else if (slot < n3 + n2 + n1 + n0)
        orig::fn_0053814f(u(name), 0x005bb7acu, 0);
    else if (slot < Field<uint32_t>(obj, 0x47c) + n3 + n2 + n1 + n0)
        orig::fn_0053814f(u(name), 0x005bb79cu, slot - Field<uint32_t>(obj, 0x478) - n2 - n1 - n0);
    else
    {
        orig::fn_00453c50(u(obj), 0, u(m), 1, 0, 0, 1, 0, 0);
        pos[0] = local[3];
        pos[1] = local[7];
        pos[2] = local[11];
        dir[0] = -bf(local[1]);
        dir[1] = -bf(local[5]);
        dir[2] = -bf(local[9]);
        return u(dir);
    }
    orig::fn_00453c50(u(obj), u(name), u(m), 1, 0, 0, 1, 0, 0);
    pos[0] = m[3];
    pos[1] = m[7];
    pos[2] = m[11];
    dir[0] = -bf(m[1]);
    dir[1] = -bf(m[5]);
    dir[2] = -bf(m[9]);
    return u(dir);
}

static uint32_t TwigletGenerator_Slot161_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                             uint32_t a4, uint32_t a5)
{
    return TwigletGenerator_Slot161(B(ecx), reinterpret_cast<uint32_t*>(a0), reinterpret_cast<float*>(a1), a2, a3, a4,
                                    reinterpret_cast<uint32_t*>(a5));
}
ABI_ENTRY_0040c280(TwigletGenerator_Slot161_Entry, TwigletGenerator_Slot161_Abi)
HOOK(0x0040c280, "TwigletGenerator_Slot161", TwigletGenerator_Slot161_Entry, nullptr);

// 0x0040c400 (ecx = object; stack: occupant, slot; ret 8). BarricadeObject slot 86 (+0x158) / TwigletGenerator slot
// 164: occupies a slot: counts it by kind (+0x49c ground, +0x4a0 window, +0x4a4 roof, +0x4a8 enter; exit none),
// sets its bit in +0x4ac, +0x498 += 1, the occupant gains a reference (+0x28) and goes to +0x4b0[slot]. Returns the
// object. Not executed in recorded runs.
uint32_t BarricadeObject_Slot86(uint8_t* obj, uint8_t* occupant, uint32_t slot)
{
    const uint32_t n0 = Field<uint32_t>(obj, 0x46c);
    const uint32_t n1 = Field<uint32_t>(obj, 0x470);
    if (slot < n0)
        ++Field<uint32_t>(obj, 0x49c);
    else if (slot < n1 + n0)
        ++Field<uint32_t>(obj, 0x4a0);
    else
    {
        const uint32_t n2 = Field<uint32_t>(obj, 0x474);
        if (slot < n2 + n1 + n0)
            ++Field<uint32_t>(obj, 0x4a4);
        else if (slot < Field<uint32_t>(obj, 0x478) + n2 + n1 + n0)
            ++Field<uint32_t>(obj, 0x4a8);
    }
    const uint32_t bits = Field<uint32_t>(obj, 0x4ac) | (1u << (slot & 31));
    const uint32_t count = Field<uint32_t>(obj, 0x498) + 1;
    Field<uint32_t>(obj, 0x4ac) = bits;
    Field<uint32_t>(obj, 0x498) = count;
    ++Field<int32_t>(occupant, 0x28);
    Field<uint8_t*>(obj, 0x4b0 + 4 * slot) = occupant;
    return u(obj);
}

static uint32_t BarricadeObject_Slot86_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return BarricadeObject_Slot86(B(ecx), B(a0), a1);
}
ABI_ENTRY_0040c400(BarricadeObject_Slot86_Entry, BarricadeObject_Slot86_Abi)
HOOK(0x0040c400, "BarricadeObject_Slot86", BarricadeObject_Slot86_Entry, nullptr);

// 0x0040c4a0 (ecx = object; stack: occupant, slot; ret 8). BarricadeObject slot 87 (+0x15c) / TwigletGenerator slot
// 165: frees a slot: only when the object's virtual +0xd8 says yes and the occupant is set and is +0x4b0[slot]; the
// reverse of slot 86 (count by kind, bit, +0x498), then the occupant loses its reference and the slot is cleared.
// Returns eax as left where cheap.
uint32_t BarricadeObject_Slot87(uint8_t* obj, uint8_t* occupant, uint32_t slot)
{
    const uint32_t r = VCall<uint32_t>(obj, 0xd8 / 4);
    if (!static_cast<uint8_t>(r))
        return r;
    if (!occupant)
        return 0;
    uint8_t** entry = &Field<uint8_t*>(obj, 0x4b0 + 4 * slot);
    if (occupant != *entry)
        return u(occupant);
    const uint32_t n0 = Field<uint32_t>(obj, 0x46c);
    const uint32_t n1 = Field<uint32_t>(obj, 0x470);
    if (slot < n0)
        --Field<uint32_t>(obj, 0x49c);
    else if (slot < n1 + n0)
        --Field<uint32_t>(obj, 0x4a0);
    else
    {
        const uint32_t n2 = Field<uint32_t>(obj, 0x474);
        if (slot < n2 + n1 + n0)
            --Field<uint32_t>(obj, 0x4a4);
        else if (slot < Field<uint32_t>(obj, 0x478) + n2 + n1 + n0)
            --Field<uint32_t>(obj, 0x4a8);
    }
    const uint32_t bits = Field<uint32_t>(obj, 0x4ac) & ~(1u << (slot & 31));
    Field<uint32_t>(obj, 0x4ac) = bits;
    --Field<uint32_t>(obj, 0x498);
    ReleaseRef(*entry);
    *entry = nullptr;
    return 0;
}

static uint32_t BarricadeObject_Slot87_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return BarricadeObject_Slot87(B(ecx), B(a0), a1);
}
ABI_ENTRY_0040c4a0(BarricadeObject_Slot87_Entry, BarricadeObject_Slot87_Abi)
HOOK(0x0040c4a0, "BarricadeObject_Slot87", BarricadeObject_Slot87_Entry, nullptr);

// 0x0040c590 (eax = matrix or 0, ecx = troop type, esi = generator). Creates a troop of the type at the matrix
// (0x0045f020; tag = generator +0x394 word): 0 "grunt", 1 "grunt_regular", 2 "captain" + 0x00425d30 (ebx = troop;
// 1) (guess: jumper captain), 3 "captain", 4 "captain" + 0x00434ac0 (troop) (guess: rocket captain), 5 "monk",
// counted in Game+0x2710; any other type logs "Generator : unkown troop type\n%d" (Debug_Printf 0x00407ff0) and
// gives 0. Then 0x0042cc60 (eax = troop, ecx = generator +0x56c, edx = +0x570) stores the two values at troop +0x858
// / +0x85c. Returns the troop (eax survives 0x0042cc60). Original bug: with an unknown type 0x0042cc60 writes
// through the null troop. Callers 0x0040c740, 0x0040c850, 0x0040c910.
uint32_t fn_0040c590(uint32_t matrix, uint32_t type, uint8_t* gen)
{
    static const uint32_t names[6] = {0x005bc01c, 0x005bbfe8, 0x005bbff8, 0x005bbff8, 0x005bbff8, 0x005bbfe0};
    uint32_t troop = 0;
    if (type <= 5)
    {
        troop = orig::fn_0045f020(0, names[type], G(0xa84), matrix, Field<uint16_t>(gen, 0x394), 0, 0, 0, 0, 0xaa, 0);
        if (type == 4)
            orig::fn_00434ac0(troop);
        else if (type == 2)
            fn_00425d30(B(troop), 1);
        ++Field<int32_t>(g_game, 0x2710);
    }
    else
    {
        orig::fn_00407ff0(G(0x34), 0x005bbfbcu, type, 0);
    }
    orig::fn_0042cc60(troop, Field<uint32_t>(gen, 0x56c), Field<uint32_t>(gen, 0x570));
    return troop;
}

static uint32_t fn_0040c590_Abi(uint32_t eax, uint32_t ecx, uint32_t esi)
{
    return fn_0040c590(eax, ecx, B(esi));
}
ABI_ENTRY_0040c590(fn_0040c590_Entry, fn_0040c590_Abi)
HOOK(0x0040c590, "fn_0040c590", fn_0040c590_Entry, nullptr);

uint32_t fn_0040c910(uint8_t* obj);

// 0x0040c710 (eax = generator). Caller 0x0040cb10: 0x0040c910 (spawn one troop); unless +0x460 bit 3 is set
// (cleared by 0x0040c910 when no troops are left), copies the six remaining-troop counts +0x554.. back over
// +0x53c.. (refill). Returns al = 1 after the refill, else al = 0.
uint32_t fn_0040c710(uint8_t* obj)
{
    const uint32_t r = fn_0040c910(obj);
    if (Field<uint8_t>(obj, 0x460) & 8)
        return r & ~0xffu;
    std::memcpy(obj + 0x53c, obj + 0x554, 6 * 4);
    return 1;
}

static uint32_t fn_0040c710_Abi(uint32_t eax)
{
    return fn_0040c710(B(eax));
}
ABI_ENTRY_0040c710(fn_0040c710_Entry, fn_0040c710_Abi)
HOOK(0x0040c710, "fn_0040c710", fn_0040c710_Entry, nullptr);

// 0x0040c740 (eax = generator; ecx is only pushed as a stack slot). Caller 0x0040cb10: when the window count +0x4a0
// is below its limit +0x488: picks the k-th free window slot (kind 3 by virtual +0x154 and free by virtual +0x148),
// k = rand * (limit - count) >> 15; picks a troop type by the six weights +0x53c.. (0 when all are 0: returns al = 0),
// creates it (0x0040c590, no matrix), puts it into the slot (0x0042d690, ebx = slot, esi = generator, edi = troop) and
// uses up one of that type (+0x53c[type] -= 1). Returns al = 1 otherwise. Not executed in recorded runs.
uint32_t fn_0040c740(uint8_t* obj, uint32_t /*ecx*/)
{
    const uint32_t count = Field<uint32_t>(obj, 0x4a0);
    const uint32_t limit = Field<uint32_t>(obj, 0x488);
    if (count >= limit)
        return (limit & ~0xffu) | 1;
    const uint32_t range = limit - count - 1 + 1;
    const int32_t k = static_cast<int32_t>(static_cast<uint32_t>(Rand()) * range) >> 15;
    uint32_t seen = 0;
    uint32_t slot = 0;
    if (!Field<uint32_t>(obj, 0x480))
        return 1;
    bool found = false;
    do
    {
        if (VCall<uint32_t>(obj, 0x154 / 4, slot) == 3 && static_cast<uint8_t>(VCall<uint32_t>(obj, 0x148 / 4, slot)))
        {
            if (seen == static_cast<uint32_t>(k))
            {
                found = true;
                break;
            }
            ++seen;
        }
        ++slot;
    } while (slot < Field<uint32_t>(obj, 0x480));
    if (!found)
        return 1;
    const uint32_t* w = &Field<uint32_t>(obj, 0x53c);
    const uint32_t total = w[5] + w[4] + w[3] + w[2] + w[1] + w[0];
    if (!total)
        return 0;
    const int32_t product = static_cast<int32_t>(static_cast<uint32_t>(Rand()) * total);
    const uint32_t pick = static_cast<uint32_t>((product >> 15) + 1);
    uint32_t sum = w[0];
    uint32_t type = 0;
    while (pick > sum)
    {
        ++w;
        sum += *w;
        ++type;
    }
    const uint32_t troop = fn_0040c590(0, type, obj);
    if (troop)
        orig::fn_0042d690(slot, u(obj), troop);
    --Field<uint32_t>(obj, 0x53c + 4 * type);
    return 1;
}

static uint32_t fn_0040c740_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0040c740(B(eax), ecx);
}
ABI_ENTRY_0040c740(fn_0040c740_Entry, fn_0040c740_Abi)
HOOK(0x0040c740, "fn_0040c740", fn_0040c740_Entry, nullptr);

// 0x0040c850 (eax = generator). Caller 0x0040cf40: finds the first free exit slot (kind 5, free) and creates a
// "grunt_regular" (type 1, 0x0040c590) at its "ref_spawn_0%d" matrix (index = slot - window count - ground count;
// guess: an original slip that ignores the roof and enter kinds, harmless when those are 0), then 0x0042dc20
// (eax = generator, ecx = troop, ebx = slot). Returns the last callee's eax. Not executed in recorded runs.
uint32_t fn_0040c850(uint8_t* obj)
{
    uint32_t S[0x6c / 4] = {};  // the original frame: [esp + k] = S[k / 4] after push ebx, esi
    uint32_t slot = 0;
    uint32_t n = VCall<uint32_t>(obj, 0x144 / 4);
    if (!n)
        return n;
    for (;;)
    {
        if (VCall<uint32_t>(obj, 0x154 / 4, slot) == 5 && static_cast<uint8_t>(VCall<uint32_t>(obj, 0x148 / 4, slot)))
            break;
        ++slot;
        n = VCall<uint32_t>(obj, 0x144 / 4);
        if (slot >= n)
            return n;
    }
    orig::fn_0053814f(u(&S[0x8 / 4]), 0x005bb2d4u, slot - Field<uint32_t>(obj, 0x470) - Field<uint32_t>(obj, 0x46c));
    orig::fn_00453c50(u(obj), u(&S[0x8 / 4]), u(&S[0x28 / 4]), 1, 0, 0, 1, 0, 0);
    const uint32_t troop = fn_0040c590(u(&S[0x28 / 4]), 1, obj);
    return orig::fn_0042dc20(u(obj), troop, slot);
}

static uint32_t fn_0040c850_Abi(uint32_t eax)
{
    return fn_0040c850(B(eax));
}
ABI_ENTRY_0040c850(fn_0040c850_Entry, fn_0040c850_Abi)
HOOK(0x0040c850, "fn_0040c850", fn_0040c850_Entry, nullptr);

// 0x0040c910 (stack: generator; ret 4). Callers 0x0040c710, 0x0040cb10: the troop type with the most remaining
// (+0x53c[6], first maximum); none left: +0x460 &= ~8 and return. Otherwise at the first free exit slot (kind 5) a
// troop of that type is created at "ref_spawn_0%d" (as 0x0040c850), +0x59c -= 1, 0x0042db50 (eax = +0x580, ecx =
// +0x584, edx = generator, ebx = slot; troop, +0x530, +0x588, +0x58c, +0x538) sets it up, it joins the list +0x5a0
// (0x004546d0) and its type count drops. In both found / not-found cases the spawn timer +0x574 = +0x578 + rand *
// (+0x57c - +0x578) / 32768. Returns eax as left (rand's value / the flag word).
uint32_t fn_0040c910(uint8_t* obj)
{
    uint32_t S[0x84 / 4] = {};  // the original frame: [esp + k] = S[k / 4] after its four pushes
    const uint32_t* w = &Field<uint32_t>(obj, 0x53c);
    uint32_t best = 0;
    for (uint32_t k = 1; k < 6; ++k)
        if (w[k] > w[best])
            best = k;
    S[0x80 / 4] = best;
    if (!w[best])
    {
        const uint32_t v = Field<uint32_t>(obj, 0x460) & 0xfffffff7u;
        Field<uint32_t>(obj, 0x460) = v;
        return v;
    }
    uint32_t slot = 0;
    uint32_t n = VCall<uint32_t>(obj, 0x144 / 4);
    bool found = false;
    if (n)
    {
        do
        {
            if (VCall<uint32_t>(obj, 0x154 / 4, slot) == 5 &&
                static_cast<uint8_t>(VCall<uint32_t>(obj, 0x148 / 4, slot)))
            {
                found = true;
                break;
            }
            ++slot;
            n = VCall<uint32_t>(obj, 0x144 / 4);
        } while (slot < n);
    }
    if (found)
    {
        orig::fn_0053814f(u(&S[0x18 / 4]), 0x005bb2d4u,
                          slot - Field<uint32_t>(obj, 0x470) - Field<uint32_t>(obj, 0x46c));
        orig::fn_00453c50(u(obj), u(&S[0x18 / 4]), u(&S[0x38 / 4]), 1, 0, 0, 1, 0, 0);
        const uint32_t troop = fn_0040c590(u(&S[0x38 / 4]), best, obj);
        const uint32_t a4 = Field<uint32_t>(obj, 0x538);
        const uint32_t a2 = Field<uint32_t>(obj, 0x588);
        Field<uint32_t>(obj, 0x59c) = Field<uint32_t>(obj, 0x59c) - 1;
        orig::fn_0042db50(Field<uint32_t>(obj, 0x580), Field<uint32_t>(obj, 0x584), u(obj), slot, troop,
                          Field<uint32_t>(obj, 0x530), a2, Field<uint32_t>(obj, 0x58c), a4);
        orig::fn_004546d0(Field<uint32_t>(obj, 0x5a0), troop);
        --Field<uint32_t>(obj, 0x53c + 4 * S[0x80 / 4]);
    }
    const float hi = Field<float>(obj, 0x57c);
    const float lo = Field<float>(obj, 0x578);
    const int32_t r = Rand();
    float t = static_cast<float>(r) * (hi - lo);
    t = t * K(0x005bcbc0);
    Field<float>(obj, 0x574) = t + lo;
    return static_cast<uint32_t>(r);
}

static uint32_t fn_0040c910_Abi(uint32_t a0)
{
    return fn_0040c910(B(a0));
}
ABI_ENTRY_0040c910(fn_0040c910_Entry, fn_0040c910_Abi)
HOOK(0x0040c910, "fn_0040c910", fn_0040c910_Entry, nullptr);

// 0x0040cd40 (ecx = object). BarricadeObject slot 55 (+0xdc) / TwigletGenerator slot 133, the removal: frees every
// occupied slot (virtual +0x15c = slot 87 (occupant, slot), then +0x4b0[slot] = 0); when the troop list +0x5a0 exists,
// removes its members (0x00454750), deletes it (virtual +0x10 (1)), clears it, +0x460 bit 3, +0x534 and +0x5a4; then
// Object_Remove 0x004547b0. Not executed in recorded runs.
uint32_t BarricadeObject_Slot55(uint8_t* obj)
{
    uint32_t slot = 0;
    if (VCall<uint32_t>(obj, 0x144 / 4))
    {
        uint8_t** entry = &Field<uint8_t*>(obj, 0x4b0);
        do
        {
            if (*entry)
            {
                VCall<void>(obj, 0x15c / 4, *entry, slot);
                *entry = nullptr;
            }
            ++slot;
            ++entry;
        } while (slot < VCall<uint32_t>(obj, 0x144 / 4));
    }
    if (Field<uint32_t>(obj, 0x5a0))
    {
        uint32_t iterator[2] = {0, 0};
        for (uint32_t it = orig::fn_004e7340(Field<uint32_t>(obj, 0x5a0), u(iterator)); it;
             it = orig::fn_004e73c0(u(iterator)))
            orig::fn_00454750(Field<uint32_t>(obj, 0x5a0), it);
        void* list = Field<void*>(obj, 0x5a0);
        if (list)
            VCall<void>(list, 0x10 / 4, 1u);
        Field<uint32_t>(obj, 0x5a0) = 0;
        Field<uint32_t>(obj, 0x460) &= 0xfffffff7u;
        Field<uint32_t>(obj, 0x534) = 0;
        Field<uint32_t>(obj, 0x5a4) = 0;
        if (iterator[1])
            --Field<int32_t>(P(iterator[1]), 0x18);
    }
    return orig::fn_004547b0(u(obj));
}

static uint32_t BarricadeObject_Slot55_Abi(uint32_t ecx)
{
    return BarricadeObject_Slot55(B(ecx));
}
ABI_ENTRY_0040cd40(BarricadeObject_Slot55_Entry, BarricadeObject_Slot55_Abi)
HOOK(0x0040cd40, "BarricadeObject_Slot55", BarricadeObject_Slot55_Entry, nullptr);

// 0x0040ce20 (ecx = object). BarricadeObject slot 56 (+0xe0) / TwigletGenerator slot 134: returns 1 when +0x460
// bit 11 is clear, else 0. Not executed in recorded runs.
uint32_t BarricadeObject_Slot56(uint8_t* obj)
{
    return ~(Field<uint32_t>(obj, 0x460) >> 11) & 1;
}

static uint32_t BarricadeObject_Slot56_Abi(uint32_t ecx)
{
    return BarricadeObject_Slot56(B(ecx));
}
ABI_ENTRY_0040ce20(BarricadeObject_Slot56_Entry, BarricadeObject_Slot56_Abi)
HOOK(0x0040ce20, "BarricadeObject_Slot56", BarricadeObject_Slot56_Entry, nullptr);

// 0x0040ce30 (ecx = object). BarricadeObject slot 57 (+0xe4) / TwigletGenerator slot 135: al = +0x460 bit 11 (upper
// eax = the shifted flags, as the original). Not executed in recorded runs.
uint32_t BarricadeObject_Slot57(uint8_t* obj)
{
    const uint32_t v = Field<uint32_t>(obj, 0x460) >> 11;
    return (v & ~0xffu) | (v & 1);
}

static uint32_t BarricadeObject_Slot57_Abi(uint32_t ecx)
{
    return BarricadeObject_Slot57(B(ecx));
}
ABI_ENTRY_0040ce30(BarricadeObject_Slot57_Entry, BarricadeObject_Slot57_Abi)
HOOK(0x0040ce30, "BarricadeObject_Slot57", BarricadeObject_Slot57_Entry, nullptr);

// 0x0040ce40 (ecx = object; stack: damage message; ret 4). BarricadeObject slot 52 (+0xd0) / TwigletGenerator slot
// 130: only when +0x460 bit 12 (damageable) is set and bit 1 (destroyed) clear; messages with flag 0x400 need bit 13;
// a source (message +0x2c) that is not excluded by its virtual +0xe0 must be allowed: the player needs bit 8, others
// for which virtual +0xe4 says yes need bit 9, and those for which +0xe8 says yes need bit 10. Then 0x004555e0 (ecx =
// object; message) applies it and, when +0x534 is set, +0x460 |= 8. Returns eax as left where cheap.
uint32_t BarricadeObject_Slot52(uint8_t* obj, uint8_t* msg)
{
    uint32_t flags = Field<uint32_t>(obj, 0x460);
    if (!(flags & 0x1000) || (flags & 2))
        return flags;
    if ((Field<uint32_t>(msg, 0) & 0x400) && !(flags & 0x2000))
        return flags;
    void* src = Field<void*>(msg, 0x2c);
    if (src && !static_cast<uint8_t>(VCall<uint32_t>(src, 0xe0 / 4)))
    {
        src = Field<void*>(msg, 0x2c);
        if (u(src) == G(0x748))
        {
            if (!(Field<uint32_t>(obj, 0x460) & 0x100))
                return Field<uint32_t>(obj, 0x460);
        }
        else if (static_cast<uint8_t>(VCall<uint32_t>(src, 0xe4 / 4)))
        {
            if (!(Field<uint32_t>(obj, 0x460) & 0x200))
                return Field<uint32_t>(obj, 0x460);
        }
        if (static_cast<uint8_t>(VCall<uint32_t>(Field<void*>(msg, 0x2c), 0xe8 / 4)))
        {
            if (!(Field<uint32_t>(obj, 0x460) & 0x400))
                return Field<uint32_t>(obj, 0x460);
        }
    }
    orig::fn_004555e0(u(obj), u(msg));
    const uint32_t r = Field<uint32_t>(obj, 0x534);
    if (r)
        Field<uint32_t>(obj, 0x460) |= 8;
    return r;
}

static uint32_t BarricadeObject_Slot52_Abi(uint32_t ecx, uint32_t a0)
{
    return BarricadeObject_Slot52(B(ecx), B(a0));
}
ABI_ENTRY_0040ce40(BarricadeObject_Slot52_Entry, BarricadeObject_Slot52_Abi)
HOOK(0x0040ce40, "BarricadeObject_Slot52", BarricadeObject_Slot52_Entry, nullptr);

// 0x0040cef0 (eax = object, cl = flag A; stack: flag B (byte); ret 4): +0x460 bit 5 = A, bit 11 = B. Caller
// 0x0040cf40 (copies the barricade's two bits to the "building" it leaves behind).
void fn_0040cef0(uint8_t* obj, uint32_t ecx, uint32_t a0)
{
    uint32_t f = Field<uint32_t>(obj, 0x460);
    f = static_cast<uint8_t>(ecx) ? (f | 0x20) : (f & 0xffffffdfu);
    Field<uint32_t>(obj, 0x460) = f;
    f = Field<uint32_t>(obj, 0x460);
    Field<uint32_t>(obj, 0x460) = static_cast<uint8_t>(a0) ? (f | 0x800) : (f & 0xfffff7ffu);
}

static uint32_t fn_0040cef0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    fn_0040cef0(B(eax), ecx, a0);
    return 0;
}
ABI_ENTRY_0040cef0(fn_0040cef0_Entry, fn_0040cef0_Abi)
HOOK(0x0040cef0, "fn_0040cef0", fn_0040cef0_Entry, nullptr);

// 0x0040cf40 (ecx = object; stack: one unused dword, ret 4). BarricadeObject slot 53 (+0xd4) / TwigletGenerator slot
// 131, the destruction: unless already destroyed (+0x460 bit 1): with bit 0 a "grunt_regular" is created at its
// matrix (virtual +0x5c) and 0x0043e520 (esi = grunt); +0x464 == 0 -> virtual +0xdc, else bit 1 is set; when the
// effect name +0x3a0 is not "null" an effect is started (Fx_Create 0x0041cc60, ecx = Game+0xa90; name, &{2, ...,
// position (virtual +0x64), ... 0}); when the replacement name +0x3c0 is not "null" a "building" of that type is
// created (0x0045f020), taken out of World+0xed4 (0x00454690 / 0x00454750) unless bit 14 is set, and gets bits 5 and
// 11 (0x0040cef0). With bit 11 the score +0x5b4 goes to Game+0x2724 (>= 0) or Game+0x2728 and 0x0040d390 (object,
// building, 1); when that returns 0: the building spawns a troop (0x0040c850) and, with bit 5, 0x0040d390 (object,
// building, 0) and the score +0x5b0. Finally the trigger +0x5a8, when set, is sent (Object_SendTrigger 0x00461870,
// eax = World, ebx = trigger; object). Not executed in recorded runs. (_stricmp 0x005448ee lists an ecx input; it is
// the CRT's and passed 0.)
uint32_t BarricadeObject_Slot53(uint8_t* obj, uint32_t /*a0*/)
{
    uint32_t S[0x90 / 4] = {};  // the original frame: [esp + k] = S[k / 4] after its four pushes
    uint32_t flags = Field<uint32_t>(obj, 0x460);
    if (flags & 2)
        return flags;
    if (flags & 1)
    {
        VCall<void>(obj, 0x5c / 4, &S[0x10 / 4]);
        const uint32_t grunt =
            orig::fn_0045f020(0, 0x005bbfe8u, G(0xa84), u(&S[0x10 / 4]), Field<uint16_t>(obj, 0x394), 0, 0, 0, 0, 0xaa, 0);
        orig::fn_0043e520(grunt);
    }
    if (Field<float>(obj, 0x464) == K(0x0056c05c))
        VCall<void>(obj, 0xdc / 4);
    else
        Field<uint32_t>(obj, 0x460) |= 2;
    const uint32_t effect = u(obj) + 0x3a0;
    uint32_t r = orig::fn_005448ee(0, effect, 0x005bba94u);
    if (r)
    {
        VCall<void>(obj, 0x64 / 4, &S[0x40 / 4]);
        S[0x84 / 4] = 0;
        S[0x10 / 4] = 2;
        orig::fn_0041cc60(r & ~0xffu, G(0xa90), effect, u(&S[0x10 / 4]));
    }
    const uint32_t replacement = u(obj) + 0x3c0;
    uint32_t building = 0;
    if (orig::fn_005448ee(0, replacement, 0x005bba94u))
    {
        VCall<void>(obj, 0x5c / 4, &S[0x10 / 4]);
        building = orig::fn_0045f020(0x005bb2acu, replacement, G(0xa84), u(&S[0x10 / 4]), Field<uint16_t>(obj, 0x394),
                                     0, 0, 0, 0, 0xaa, 0);
        if (!(Field<uint32_t>(obj, 0x460) & 0x4000))
        {
            if (static_cast<uint8_t>(orig::fn_00454690(Field<uint32_t>(P(G(0xa84)), 0xed4), building)))
                orig::fn_00454750(Field<uint32_t>(P(G(0xa84)), 0xed4), building);
        }
        const uint32_t f = Field<uint32_t>(obj, 0x460);
        const uint32_t b = (f >> 11) & 0xffffff01u;
        const uint32_t a = ((f >> 5) & ~0xffu) | ((f >> 5) & 1);
        fn_0040cef0(B(building), a, b);
    }
    bool skip = false;
    if (Field<uint32_t>(obj, 0x460) & 0x800)
    {
        const float score = Field<float>(obj, 0x5b4);
        if (score >= K(0x0056c05c))
            Field<float>(g_game, 0x2724) = score + Field<float>(g_game, 0x2724);
        else
            Field<float>(g_game, 0x2728) = score + Field<float>(g_game, 0x2728);
        if (orig::fn_0040d390(u(obj), building, 1))
            skip = true;
    }
    if (!skip)
    {
        if (building)
            fn_0040c850(B(building));
        if (Field<uint8_t>(obj, 0x460) & 0x20)
        {
            orig::fn_0040d390(u(obj), building, 0);
            const float score = Field<float>(obj, 0x5b0);
            if (score >= K(0x0056c05c))
                Field<float>(g_game, 0x2724) = score + Field<float>(g_game, 0x2724);
            else
                Field<float>(g_game, 0x2728) = score + Field<float>(g_game, 0x2728);
        }
    }
    const uint32_t trigger = Field<uint32_t>(obj, 0x5a8);
    if (trigger)
        return orig::fn_00461870(G(0xa84), trigger, u(obj));
    return 0;
}

static uint32_t BarricadeObject_Slot53_Abi(uint32_t ecx, uint32_t a0)
{
    return BarricadeObject_Slot53(B(ecx), a0);
}
ABI_ENTRY_0040cf40(BarricadeObject_Slot53_Entry, BarricadeObject_Slot53_Abi)
HOOK(0x0040cf40, "BarricadeObject_Slot53", BarricadeObject_Slot53_Entry, nullptr);

// 0x0040d1b0 (ecx = object; stack: a0 (unused), trigger id; ret 8). BarricadeObject slot 14 (+0x38) /
// TwigletGenerator slot 92, the trigger handler: id +0x534 -> +0x460 |= 8 and, unless bits 2 or 4 are set, +0x534 = 0;
// id +0x538 -> clears bit 3; id +0x5a4 -> sets bit 3 and refills the troop counts (+0x554.. to +0x53c..); id +0x5ac
// -> sets bit 12 (damageable) and virtual +0xd4 (0). Returns al = 0. Not executed in recorded runs.
uint32_t BarricadeObject_Slot14(uint8_t* obj, uint32_t /*a0*/, uint32_t id)
{
    if (id == Field<uint32_t>(obj, 0x534))
    {
        const uint32_t f = Field<uint32_t>(obj, 0x460) | 8;
        Field<uint32_t>(obj, 0x460) = f;
        if (!(f & 0x14))
            Field<uint32_t>(obj, 0x534) = 0;
        return f & ~0xffu;
    }
    if (id == Field<uint32_t>(obj, 0x538))
    {
        Field<uint32_t>(obj, 0x460) &= 0xfffffff7u;
        return id & ~0xffu;
    }
    if (id == Field<uint32_t>(obj, 0x5a4))
    {
        Field<uint32_t>(obj, 0x460) |= 8;
        std::memcpy(obj + 0x53c, obj + 0x554, 6 * 4);
        return 0;
    }
    if (id == Field<uint32_t>(obj, 0x5ac))
    {
        Field<uint32_t>(obj, 0x460) |= 0x1000;
        return VCall<uint32_t>(obj, 0xd4 / 4, 0u) & ~0xffu;
    }
    return id & ~0xffu;
}

static uint32_t BarricadeObject_Slot14_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return BarricadeObject_Slot14(B(ecx), a0, a1);
}
ABI_ENTRY_0040d1b0(BarricadeObject_Slot14_Entry, BarricadeObject_Slot14_Abi)
HOOK(0x0040d1b0, "BarricadeObject_Slot14", BarricadeObject_Slot14_Entry, nullptr);

// The shared tail of 0x0040dcb0 / 0x0040dd10: the named matrix of the object (0x00453c50, into the buffer or a local
// 0x44-byte one); position = translation (m[3], m[7], m[11]), direction = -(m[1], m[5], m[9]). Returns the direction
// pointer.
static uint32_t NamedPoint(uint8_t* obj, uint32_t name, uint32_t* pos, float* dir, uint32_t* buffer)
{
    uint32_t local[0x44 / 4];
    uint32_t* m = buffer ? buffer : local;
    orig::fn_00453c50(u(obj), name, u(m), 1, 0, 0, 1, 0, 0);
    pos[0] = m[3];
    pos[1] = m[7];
    pos[2] = m[11];
    dir[0] = -bf(m[1]);
    dir[1] = -bf(m[5]);
    dir[2] = -bf(m[9]);
    return u(dir);
}

// 0x0040dcb0 (ecx = object; stack: position out, direction out, a2..a4 (unused), matrix buffer or 0; ret 0x18).
// BarricadeObject slot 83 (+0x14c): the "ref_exit" point and direction (see NamedPoint). Not executed in recorded runs.
uint32_t BarricadeObject_Slot83(uint8_t* obj, uint32_t* pos, float* dir, uint32_t* buffer)
{
    return NamedPoint(obj, 0x005badcc, pos, dir, buffer);
}

static uint32_t BarricadeObject_Slot83_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t /*a2*/, uint32_t /*a3*/,
                                           uint32_t /*a4*/, uint32_t a5)
{
    return BarricadeObject_Slot83(B(ecx), reinterpret_cast<uint32_t*>(a0), reinterpret_cast<float*>(a1),
                                  reinterpret_cast<uint32_t*>(a5));
}
ABI_ENTRY_0040dcb0(BarricadeObject_Slot83_Entry, BarricadeObject_Slot83_Abi)
HOOK(0x0040dcb0, "BarricadeObject_Slot83", BarricadeObject_Slot83_Entry, nullptr);

// 0x0040dd10 (as 0x0040dcb0). BarricadeObject slot 84 (+0x150): the "ref_barracade" point and direction. Not executed
// in recorded runs.
uint32_t BarricadeObject_Slot84(uint8_t* obj, uint32_t* pos, float* dir, uint32_t* buffer)
{
    return NamedPoint(obj, 0x005badbc, pos, dir, buffer);
}

static uint32_t BarricadeObject_Slot84_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t /*a2*/, uint32_t /*a3*/,
                                           uint32_t /*a4*/, uint32_t a5)
{
    return BarricadeObject_Slot84(B(ecx), reinterpret_cast<uint32_t*>(a0), reinterpret_cast<float*>(a1),
                                  reinterpret_cast<uint32_t*>(a5));
}
ABI_ENTRY_0040dd10(BarricadeObject_Slot84_Entry, BarricadeObject_Slot84_Abi)
HOOK(0x0040dd10, "BarricadeObject_Slot84", BarricadeObject_Slot84_Entry, nullptr);

// 0x0040dd70 (ecx = marker; stack: 4, ret 0x10). JumpMarker (vtable 0x005ac628) slot 6, the set-up: +0x468 (the
// occupant) = 0, the base set-up 0x004517b0, joins World+0xf10 (0x004546d0), virtual +0xf4 (0), then reads
// "marker_tag" into +0x464 (0x00453450, ebx = name, esi = a3, edi = destination; marker, a2, 0) and "marker_range"
// into +0x46c (0x004534a0; marker, name, a2, 50.0, 0). Not executed in recorded runs.
uint32_t JumpMarker_Slot6(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Field<uint32_t>(obj, 0x468) = 0;
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    orig::fn_004546d0(Field<uint32_t>(P(G(0xa84)), 0xf10), u(obj));
    VCall<void>(obj, 0xf4 / 4, 0u);
    orig::fn_00453450(0, 0x005badb0u, a3, u(obj) + 0x464, u(obj), a2, 0);
    return orig::fn_004534a0(a3, u(obj) + 0x46c, u(obj), 0x005bada0u, a2, 0x42480000u, 0);
}

static uint32_t JumpMarker_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return JumpMarker_Slot6(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0040dd70(JumpMarker_Slot6_Entry, JumpMarker_Slot6_Abi)
HOOK(0x0040dd70, "JumpMarker_Slot6", JumpMarker_Slot6_Entry, nullptr);

// 0x0040de00 (ecx = marker). JumpMarker slot 55 (+0xdc), the removal: when occupied (+0x468), virtual +0x15c
// (occupant, 0) frees it and +0x468 = 0; then tail-jumps to Object_Remove 0x004547b0 (ecx = marker). Not executed in
// recorded runs.
uint32_t JumpMarker_Slot55(uint8_t* obj)
{
    const uint32_t occupant = Field<uint32_t>(obj, 0x468);
    if (occupant)
    {
        VCall<void>(obj, 0x15c / 4, occupant, 0u);
        Field<uint32_t>(obj, 0x468) = 0;
    }
    return orig::fn_004547b0(u(obj));
}

static uint32_t JumpMarker_Slot55_Abi(uint32_t ecx)
{
    return JumpMarker_Slot55(B(ecx));
}
ABI_ENTRY_0040de00(JumpMarker_Slot55_Entry, JumpMarker_Slot55_Abi)
HOOK(0x0040de00, "JumpMarker_Slot55", JumpMarker_Slot55_Entry, nullptr);

// 0x0040de30 (ecx = marker). JumpMarker slot 79 (+0x13c): 1 when occupied (+0x468), else 0. Not executed in recorded
// runs.
uint32_t JumpMarker_Slot79(uint8_t* obj)
{
    return Field<uint32_t>(obj, 0x468) != 0 ? 1u : 0u;
}

static uint32_t JumpMarker_Slot79_Abi(uint32_t ecx)
{
    return JumpMarker_Slot79(B(ecx));
}
ABI_ENTRY_0040de30(JumpMarker_Slot79_Entry, JumpMarker_Slot79_Abi)
HOOK(0x0040de30, "JumpMarker_Slot79", JumpMarker_Slot79_Entry, nullptr);

// 0x0040de40 (ecx = marker; stack: one unused dword, ret 4). JumpMarker slot 82 (+0x148): al = 1 when free (+0x468 is
// 0; upper eax = +0x468 as the original). Not executed in recorded runs.
uint32_t JumpMarker_Slot82(uint8_t* obj, uint32_t /*a0*/)
{
    const uint32_t v = Field<uint32_t>(obj, 0x468);
    return (v & ~0xffu) | (v == 0 ? 1u : 0u);
}

static uint32_t JumpMarker_Slot82_Abi(uint32_t ecx, uint32_t a0)
{
    return JumpMarker_Slot82(B(ecx), a0);
}
ABI_ENTRY_0040de40(JumpMarker_Slot82_Entry, JumpMarker_Slot82_Abi)
HOOK(0x0040de40, "JumpMarker_Slot82", JumpMarker_Slot82_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Generic AI object (GenericAIObject, vtable 0x005b2958) helpers: +0x958 a list (0x004e7010 object) of remembered
// points, +0x95c the pool (12-byte nodes, 20 per chunk; free list +8, chunks +0xc) they come from, +0x960 the last
// point.

// 0x00424610 (esi = node, edi = AI object). Caller GenericAI_Update 0x0043ee90: takes the node out of the list +0x958
// (virtual +0x28) and returns it to the pool's free list (+0x95c +8). Returns the pool. Not executed in recorded runs.
uint32_t fn_00424610(uint32_t* node, uint8_t* ai)
{
    VCall<void>(Field<void*>(ai, 0x958), 0x28 / 4, node);
    uint8_t* pool = Field<uint8_t*>(ai, 0x95c);
    node[0] = Field<uint32_t>(pool, 8);
    Field<uint32_t*>(pool, 8) = node;
    return u(pool);
}

static uint32_t fn_00424610_Abi(uint32_t esi, uint32_t edi)
{
    return fn_00424610(reinterpret_cast<uint32_t*>(esi), B(edi));
}
ABI_ENTRY_00424610(fn_00424610_Entry, fn_00424610_Abi)
HOOK(0x00424610, "fn_00424610", fn_00424610_Entry, nullptr);

uint32_t fn_004246a0(uint8_t* ai);

// 0x00424630 (eax = AI object, edi = point). Callers GenericAI_Update 0x0043ee90, 0x0048d450: when the list +0x958
// holds more than 100 points (virtual +0x34) it is cleared (0x004246a0); a node from the pool (0x004f6020, esi =
// pool) gets the point and joins the list (virtual +0x18); +0x960 = the point. Returns eax as left (the point's x, or
// 0 when the pool is empty). Not executed in recorded runs.
uint32_t fn_00424630(uint8_t* ai, const uint32_t* point)
{
    if (VCall<uint32_t>(Field<void*>(ai, 0x958), 0x34 / 4) > 100)
        fn_004246a0(ai);
    uint32_t* node = reinterpret_cast<uint32_t*>(orig::fn_004f6020(Field<uint32_t>(ai, 0x95c)));
    if (!node)
        return 0;
    node[0] = point[0];
    node[1] = point[1];
    node[2] = point[2];
    VCall<void>(Field<void*>(ai, 0x958), 0x18 / 4, node);
    Field<uint32_t>(ai, 0x960) = point[0];
    Field<uint32_t>(ai, 0x964) = point[1];
    Field<uint32_t>(ai, 0x968) = point[2];
    return point[0];
}

static uint32_t fn_00424630_Abi(uint32_t eax, uint32_t edi)
{
    return fn_00424630(B(eax), reinterpret_cast<const uint32_t*>(edi));
}
ABI_ENTRY_00424630(fn_00424630_Entry, fn_00424630_Abi)
HOOK(0x00424630, "fn_00424630", fn_00424630_Entry, nullptr);

// 0x004246a0 (esi = AI object): when the list +0x958 exists, clears it (virtual +0x30) and resets the pool
// (0x004f5ff0, edi = +0x95c). Returns eax as left (0 when there is no list). Not executed in recorded runs.
uint32_t fn_004246a0(uint8_t* ai)
{
    void* list = Field<void*>(ai, 0x958);
    if (!list)
        return 0;
    VCall<void>(list, 0x30 / 4);
    return orig::fn_004f5ff0(Field<uint32_t>(ai, 0x95c));
}

static uint32_t fn_004246a0_Abi(uint32_t esi)
{
    return fn_004246a0(B(esi));
}
ABI_ENTRY_004246a0(fn_004246a0_Entry, fn_004246a0_Abi)
HOOK(0x004246a0, "fn_004246a0", fn_004246a0_Entry, nullptr);

uint32_t fn_004247a0(uint8_t* pool, uint32_t flags);

// 0x004246c0 (al = create; stack: AI object; ret 4). Creates (al != 0) or destroys the point list and its pool:
// create: the list +0x958 when missing (PMS_heapAlloc 0x004e07f0 (0x10, 0x18, 2, "new") + 0x004e7010 (eax =
// Game+0xac4, edx = 0, esi = block)), the pool +0x95c when missing (0x10 bytes: node size 0xc, 0x14 per chunk, no
// free nodes, no chunks); destroy: the list's virtual +0x10 (1) and the pool's 0x004247a0 (edi = pool; 1), both
// cleared. Returns eax as left where cheap.
uint32_t fn_004246c0(uint32_t eax, uint8_t* ai)
{
    if (static_cast<uint8_t>(eax))
    {
        if (!Field<uint32_t>(ai, 0x958))
        {
            const uint32_t block = orig::fn_004e07f0(0x0063a210u, 0x10, 0x18, 2, 0x0058b5e8u);
            Field<uint32_t>(ai, 0x958) = block ? orig::fn_004e7010(G(0xac4), 0, block) : 0;
        }
        if (Field<uint32_t>(ai, 0x95c))
            return eax;
        uint32_t* pool = reinterpret_cast<uint32_t*>(orig::fn_004e07f0(0x0063a210u, 0x10, 0x10, 2, 0x0058b5e8u));
        if (!pool)
        {
            Field<uint32_t>(ai, 0x95c) = 0;
            return 0;
        }
        pool[2] = 0;
        pool[3] = 0;
        pool[0] = 0xc;
        pool[1] = 0x14;
        Field<uint32_t*>(ai, 0x95c) = pool;
        return u(pool);
    }
    void* list = Field<void*>(ai, 0x958);
    if (list)
    {
        VCall<void>(list, 0x10 / 4, 1u);
        Field<uint32_t>(ai, 0x958) = 0;
    }
    uint8_t* pool = Field<uint8_t*>(ai, 0x95c);
    if (pool)
    {
        fn_004247a0(pool, 1);
        Field<uint32_t>(ai, 0x95c) = 0;
    }
    return eax;
}

static uint32_t fn_004246c0_Abi(uint32_t eax, uint32_t a0)
{
    return fn_004246c0(eax, B(a0));
}
ABI_ENTRY_004246c0(fn_004246c0_Entry, fn_004246c0_Abi)
HOOK(0x004246c0, "fn_004246c0", fn_004246c0_Entry, nullptr);

// 0x004247a0 (edi = pool; stack: flags; ret 4): the pool's destructor: frees every chunk (linked through their first
// dword from +0xc; PMS_heapFree 0x004e08d0, ecx = heap 0x0063a210), clears +0xc and +8 and, with flags bit 0, frees
// the pool itself. Returns the pool. Caller 0x004246c0. Not executed in recorded runs.
uint32_t fn_004247a0(uint8_t* pool, uint32_t flags)
{
    uint32_t* chunk = Field<uint32_t*>(pool, 0xc);
    while (chunk)
    {
        uint32_t* next = reinterpret_cast<uint32_t*>(chunk[0]);
        orig::fn_004e08d0(0x0063a210u, u(chunk));
        chunk = next;
    }
    Field<uint32_t>(pool, 0xc) = 0;
    Field<uint32_t>(pool, 8) = 0;
    if (flags & 1)
        orig::fn_004e08d0(0x0063a210u, u(pool));
    return u(pool);
}

static uint32_t fn_004247a0_Abi(uint32_t edi, uint32_t a0)
{
    return fn_004247a0(B(edi), a0);
}
ABI_ENTRY_004247a0(fn_004247a0_Entry, fn_004247a0_Abi)
HOOK(0x004247a0, "fn_004247a0", fn_004247a0_Entry, nullptr);

// One parameter read of Ai_InitGeneric: kind 'F' float (0x004534a0), 'I' int (0x00453400, default in eax), 'T' tag
// (0x00453450, name in ebx); destination offset, key string, default, last stack argument.
struct ParamStep
{
    char kind;
    uint16_t off;
    uint32_t key;
    uint32_t def;
    uint32_t last;
};

static void RunParamSteps(uint8_t* obj, uint32_t a2, uint32_t a3, const ParamStep* steps, uint32_t count)
{
    const uint32_t o = u(obj);
    for (uint32_t i = 0; i < count; ++i)
    {
        const ParamStep& s = steps[i];
        if (s.kind == 'F')
            orig::fn_004534a0(a3, o + s.off, o, s.key, a2, s.def, s.last);
        else if (s.kind == 'I')
            orig::fn_00453400(s.def, a3, o + s.off, o, s.key, a2, s.last);
        else
            orig::fn_00453450(0, s.key, a3, o + s.off, o, a2, s.last);
    }
}

// 0x004247f0 Ai_InitGeneric (ecx = AI object; stack: a0 (spawn record), a1, parameter block a2, a3; ret 0x10).
// GenericAIObject (vtable 0x005b2958) slot 6, also called by the set-ups of many AI classes. Counts teammates with a
// non-zero +0x396 in Game+0x2710 (virtual +0xe8), the base set-up 0x004517b0, scales health +0x458 (and +0x454) by
// Game+0x558 (teammates) or Game+0x554 (virtual +0xe4), joins the World lists +0xe80 +0xe90 +0xe94 +0xe9c +0xea4
// +0xe7c +0xe8c, 0x00458270 (edx = object; 0, 0, 20000 / model +0x1c4 ->+8 ->+0x2c (0 when that is 0)), then reads
// about 110 parameters (names are the game's strings at the keys; see the step tables), the pickup to drop
// ("pickup_drop_specific": index into the 28 pickup names at Game+0x2890 (stride 0x150) -> +0xb2c, or -2 / -3 for
// "pickup_health_1" / "pickup_health_2", -1 none), a 50% coin for +0x484 bit 21, 0x00428250 (eax = object, cl = 0),
// 0x00427600 (esi = object), 0x004559c0 (eax = object, cl = 1), and the "mode": attention_roaming (default),
// ready_roaming, attention_guard, ready_guard, trigger_charge, attention_window, ready_window, attention_tower,
// ready_tower, trigger_marker, trigger_ic, trigger_scared, trigger_fire, turret, marker_boss, bully, each setting
// flags (+0x484 / +0x488 / +0x48c), reading its own parameters and calling its set-up (virtual +0x19c / +0x1a0 /
// +0x1b8, 0x0042d0f0, 0x0042d000); an unknown mode or an invalid trigger_marker path logs a message (Debug_Printf
// 0x00407ff0) and deactivates the object (virtual +0xdc). Otherwise "score_value" +0x47c and the position +0x4c0
// (virtual +0x54). Original quirk: mode trigger_charge stores rand's value over its own a2 stack slot (unused
// afterwards). 0x0042d0f0 lists an ecx input that it only pushes as a stack slot (passed 0); 0x00428250 /
// 0x004559c0 get cl only (upper ecx bits as left by the original are not reproduced).
uint32_t Ai_InitGeneric(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t Fr[0x70 / 4] = {};  // the original frame: [esp + k] = Fr[k / 4] after its four pushes (+0x60.. = args)
    Fr[0x60 / 4] = a0;
    Fr[0x64 / 4] = a1;
    Fr[0x68 / 4] = a2;
    Fr[0x6c / 4] = a3;
    uint32_t* tmp = &Fr[0x64 / 4];
    char* pickup = reinterpret_cast<char*>(&Fr[0x3c / 4]);
    char* mode = reinterpret_cast<char*>(&Fr[0x1c / 4]);
    const uint32_t o = u(obj);
    auto readBool = [&](uint32_t def, uint32_t key, uint32_t* dst) {
        orig::fn_004534f0(def, a3, u(dst), o, key, a2, 0);
        return static_cast<uint8_t>(*dst) != 0;
    };
    auto readTag = [&](uint32_t off, uint32_t key) { orig::fn_00453450(0, key, a3, o + off, o, a2, 0); };
    auto readInt0 = [&](uint32_t off, uint32_t key) { orig::fn_00453400(0, a3, o + off, o, key, a2, 0); };
    auto log1 = [&](uint32_t fmt) { orig::fn_00407ff0(G(0x34), fmt, orig::fn_00453270(o), 0); };
    auto stricmp = [](uint32_t a, uint32_t b) { return orig::fn_005448ee(0, a, b); };

    if (static_cast<uint8_t>(VCall<uint32_t>(obj, 0xe8 / 4)) && Field<uint16_t>(obj, 0x396))
        ++Field<int32_t>(g_game, 0x2710);
    orig::fn_004517b0(o, a0, a1, a2, a3);
    uint32_t scale = 0;
    if (static_cast<uint8_t>(VCall<uint32_t>(obj, 0xe8 / 4)))
        scale = 0x558;
    else if (static_cast<uint8_t>(VCall<uint32_t>(obj, 0xe4 / 4)))
        scale = 0x554;
    if (scale)
    {
        const float h = Field<float>(g_game, scale) * Field<float>(obj, 0x458);
        Field<float>(obj, 0x458) = h;
        Field<float>(obj, 0x454) = h;
    }
    for (uint32_t list : {0xe80u, 0xe90u, 0xe94u, 0xe9cu, 0xea4u, 0xe7cu, 0xe8cu})
        orig::fn_004546d0(Field<uint32_t>(P(G(0xa84)), list), o);
    {
        const float v = Field<float>(Field<void*>(Field<void*>(obj, 0x1c4), 8), 0x2c);
        const float inv = (v == K(0x0056c05c)) ? K(0x0056c05c) : K(0x005bcb2c) / v;
        orig::fn_00458270(o, 0, 0, fb(inv * K(0x005bcd34)));
    }
    static const ParamStep stepsA[] = {
        {'F', 0x8fc, 0x005b75a4, 0x3f800000, 1}, {'F', 0x900, 0x005b758c, 0x3f800000, 1},
        {'F', 0x888, 0x005b7578, 0x3f000000, 1}};
    RunParamSteps(obj, a2, a3, stepsA, 3);
    if (!readBool(1, 0x005b7568, tmp))  // can_be_sharked
        Field<uint32_t>(obj, 0x48c) |= 0x100;
    fn_00425d30(obj, readBool(0, 0x005b7558, tmp) ? 1u : 0u);  // jumper_captain
    static const ParamStep stepsB[] = {
        {'F', 0xae4, 0x005b7544, 0x40000000, 1}, {'F', 0xae8, 0x005b7538, 0x432f0000, 1},
        {'F', 0xaf0, 0x005b752c, 0x40400000, 1}, {'F', 0xaec, 0x005b7518, 0x3e800000, 1}};
    RunParamSteps(obj, a2, a3, stepsB, 4);
    Field<float>(obj, 0xaec) = Field<float>(obj, 0xaec) * K(0x005bcb4c);
    static const ParamStep stepsC[] = {
        {'F', 0xaf4, 0x005b74fc, 0x40800000, 1}, {'F', 0xafc, 0x005b74e8, 0x41200000, 1},
        {'F', 0xaf8, 0x005b74d4, 0x41f00000, 1}, {'F', 0xb00, 0x005b74c0, 0x41200000, 1}};
    RunParamSteps(obj, a2, a3, stepsC, 4);
    orig::fn_004534a0(a3, o + 0xb04, o, 0x005b74a4u, a2, fb(Field<float>(obj, 0xaec) * K(0x005bcbe4)), 1);
    Field<float>(obj, 0xb04) = Field<float>(obj, 0xb04) * K(0x005bcb4c);
    static const ParamStep stepsD[] = {
        {'T', 0xb14, 0x005b7498, 0, 0},          {'F', 0xb24, 0x005b7480, 0x3f800000, 1},
        {'I', 0x58c, 0x005b746c, 1, 1},          {'I', 0x590, 0x005b7458, 1, 1},
        {'F', 0x594, 0x005b7444, 0, 1},          {'F', 0x598, 0x005b7430, 0, 1},
        {'I', 0x59c, 0x005b741c, 1, 1},          {'I', 0x5a0, 0x005b7408, 1, 1},
        {'I', 0x5a4, 0x005b73f4, 1, 1},          {'I', 0x5a8, 0x005b73e0, 1, 1},
        {'F', 0x5ac, 0x005b73c8, 0, 1},          {'F', 0x5b0, 0x005b73b0, 0, 1},
        {'F', 0x5e4, 0x005b7398, 0x41200000, 1}, {'F', 0x5e8, 0x005b7380, 0x41700000, 1},
        {'F', 0x5ec, 0x005b736c, 0x42480000, 1}, {'F', 0x5f0, 0x005b7358, 0x43160000, 1},
        {'F', 0x5f4, 0x005b733c, 0x43c80000, 1}, {'F', 0x5f8, 0x005b7320, 0x44480000, 1},
        {'T', 0x858, 0x005b7308, 0, 0},          {'T', 0x85c, 0x005b72f0, 0, 0},
        {'I', 0x89c, 0x005b72d8, 3, 1},          {'I', 0x8a0, 0x005b72c0, 5, 1},
        {'F', 0x8a4, 0x005b72a4, 0x3f800000, 1}, {'F', 0x8a8, 0x005b7288, 0x3f800000, 1},
        {'I', 0x8ac, 0x005b726c, 10, 1},         {'I', 0x8b0, 0x005b7250, 10, 1},
        {'I', 0x8b4, 0x005b7234, 5, 1},          {'I', 0x8b8, 0x005b7218, 5, 1},
        {'F', 0x8bc, 0x005b71fc, 0x3e800000, 1}, {'F', 0x8c0, 0x005b71e0, 0x3f000000, 1},
        {'F', 0x8c4, 0x005b71cc, 0x3f000000, 1}, {'F', 0x8c8, 0x005b71b8, 0x3f400000, 1},
        {'I', 0x8cc, 0x005b719c, 1, 1},          {'I', 0x8d0, 0x005b7180, 1, 1},
        {'F', 0x8d4, 0x005b7164, 0, 1},          {'F', 0x8d8, 0x005b7148, 0, 1},
        {'I', 0x8dc, 0x005b712c, 1, 1},          {'I', 0x8e0, 0x005b7110, 1, 1},
        {'I', 0x8e4, 0x005b70f4, 1, 1},          {'I', 0x8e8, 0x005b70d8, 1, 1},
        {'F', 0x8ec, 0x005b70bc, 0, 1},          {'F', 0x8f0, 0x005b70a0, 0, 1},
        {'F', 0x8f4, 0x005b7088, 0x40a00000, 0}, {'F', 0x8f8, 0x005b7070, 0x40e00000, 0},
        {'T', 0x528, 0x005bb888, 0, 0},          {'T', 0x530, 0x005bb870, 0, 0},
        {'I', 0x574, 0x005bbf18, 0, 0},          {'F', 0x5dc, 0x005b7060, 0x43960000, 0},
        {'F', 0xb18, 0x005b7050, 0x43960000, 1}, {'F', 0xb1c, 0x005b7050, 0x42fa0000, 1},
        {'F', 0x5e0, 0x005b7040, 0x41a00000, 1}, {'F', 0x9e0, 0x005b7030, 0x41a00000, 1},
        {'F', 0x9e4, 0x005b7020, 0x42700000, 1}, {'F', 0x9e8, 0x005b7014, 0x41200000, 1},
        {'F', 0x9ec, 0x005b7008, 0x41c80000, 1}, {'F', 0xa0c, 0x005b6ffc, 0x42480000, 1}};
    RunParamSteps(obj, a2, a3, stepsD, sizeof(stepsD) / sizeof(stepsD[0]));
    if (readBool(0, 0x005b6ff0, tmp))  // ledge_death
        Field<uint32_t>(obj, 0x484) |= 0x80000000u;
    static const ParamStep stepsE[] = {{'F', 0x944, 0x005b6fe4, 0x41f00000, 1}, {'F', 0x948, 0x005b6fd8, 0x42380000, 1}};
    RunParamSteps(obj, a2, a3, stepsE, 2);
    orig::fn_004534a0(a3, o + 0x94c, o, 0x005b6fccu, a2, fb(Field<float>(obj, 0x948) * K(0x005bcf6c)), 1);
    orig::fn_00453400(0, a3, o + 0x950, o, 0x005b6fc0u, a2, 1);
    orig::fn_004534a0(a3, o + 0x954, o, 0x005b6fb0u, a2, fb(Field<float>(obj, 0x948) * K(0x005bcbe4)), 1);
    static const ParamStep stepsF[] = {
        {'F', 0x97c, 0x005b6f9c, 0, 1},          {'F', 0x980, 0x005b6f88, 0x42c80000, 1},
        {'F', 0x984, 0x005b6f74, 0, 1},          {'F', 0x988, 0x005b6f60, 0, 1},
        {'F', 0x98c, 0x005b6f48, 0x3f800000, 1}, {'F', 0x990, 0x005b6f34, 0x41880000, 1},
        {'F', 0x9a4, 0x005b6f20, 0, 1},          {'F', 0x9a8, 0x005b6f0c, 0x42c80000, 1},
        {'F', 0x9ac, 0x005b6ef4, 0, 1},          {'F', 0x9b0, 0x005b6ee0, 0, 1},
        {'F', 0x9b4, 0x005b6ec8, 0x3f800000, 1}, {'F', 0x9b8, 0x005b6eb0, 0x41880000, 1}};
    RunParamSteps(obj, a2, a3, stepsF, sizeof(stepsF) / sizeof(stepsF[0]));
    orig::fn_004533a0(0x0058a9a4u, u(pickup), o, 0x005b6e98u, a2, a3, 0);  // pickup_drop_specific
    orig::fn_004534a0(a3, o + 0xb30, o, 0x005b6e84u, a2, 0x42c80000u, 0);
    Field<int32_t>(obj, 0xb2c) = -1;
    Field<float>(obj, 0xb30) = Field<float>(obj, 0xb30) * K(0x005bcbec);
    if (pickup[0])
    {
        uint32_t index = 0;
        bool found = false;
        for (uint32_t off = 0; off < 0x24c0; off += 0x150, ++index)
        {
            if (!stricmp(u(g_game) + off + 0x2890, u(pickup)))
            {
                found = true;
                break;
            }
        }
        if (found)
            Field<uint32_t>(obj, 0xb2c) = index;
        if (!found || index >= 0x1c)
        {
            if (!stricmp(0x005b6e74u, u(pickup)))
                Field<int32_t>(obj, 0xb2c) = -2;
            else if (!stricmp(0x005b6e64u, u(pickup)))
                Field<int32_t>(obj, 0xb2c) = -3;
        }
    }
    orig::fn_00453400(0, a3, o + 0x86c, o, 0x005b6e54u, a2, 1);
    orig::fn_004534a0(a3, o + 0x868, o, 0x005b6e40u, a2, 0x42c80000u, 1);
    if (static_cast<int32_t>((static_cast<uint32_t>(Rand()) * 100u) & 0xffff8000u) < 0x190000)
        Field<uint32_t>(obj, 0x484) |= 0x200000;
    orig::fn_00428250(o, 0);
    orig::fn_00427600(o);
    orig::fn_004559c0(o, 1);
    orig::fn_004533a0(0x005b6e2cu, u(mode), o, 0x005b6e24u, a2, a3, 0);  // mode, default "attention_roaming"
    const uint32_t m = u(mode);
    if (!stricmp(m, 0x005b6e2c))  // attention_roaming
    {
        if (a2)
            VCall<void>(obj, 0x19c / 4, 0xfu, 1u, 0u, 0u, 0u, 0u);
        VCall<void>(obj, 0x1b8 / 4, 1u);
    }
    else if (!stricmp(m, 0x005b6e14))  // ready_roaming
    {
        VCall<void>(obj, 0x1a0 / 4, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
        VCall<void>(obj, 0x1b8 / 4, 1u);
    }
    else if (!stricmp(m, 0x005b6e04))  // attention_guard
    {
        orig::fn_0042d0f0(o, 0, 1, 1);
    }
    else if (!stricmp(m, 0x005b6df8))  // ready_guard
    {
        orig::fn_0042d0f0(o, 0, 0, 1);
    }
    else if (!stricmp(m, 0x005b6de8))  // trigger_charge
    {
        if (readBool(0, 0x005b6dd4, tmp))  // trigger_no_damage
            Field<uint32_t>(obj, 0x48c) |= 0x4000;
        readInt0(0x52c, 0x005bba00);
        if (readBool(1, 0x005bb68c, tmp))  // trigger_los
            Field<uint32_t>(obj, 0x488) |= 0x40;
        Field<uint32_t>(obj, 0x488) |= 0x100;
        const int32_t r = Rand();
        Fr[0x68 / 4] = static_cast<uint32_t>(r);
        const float t = static_cast<float>(r) * K(0x005bcd54);
        Field<float>(obj, 0x494) = t + K(0x005bcd50);
        VCall<void>(obj, 0x1a0 / 4, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
        VCall<void>(obj, 0x1b8 / 4, 0x8cu);
    }
    else
    {
        // attention_window / ready_window / attention_tower / ready_tower: obj_tag, generic_txt, 0x0042d0f0.
        uint32_t window = 0;  // 1 attention_window, 2 ready_window, 3 attention_tower, 4 ready_tower
        if (!stricmp(m, 0x005b6dc0))
            window = 1;
        else if (!stricmp(m, 0x005b6d9c))
            window = 2;
        else if (!stricmp(m, 0x005b6d8c))
            window = 3;
        else if (!stricmp(m, 0x005b6d80))
            window = 4;
        if (window)
        {
            Field<uint32_t>(obj, 0x488) |= (window <= 2) ? 0x80u : 0x200000u;
            readTag(0x534, 0x005b6db8);
            orig::fn_004533a0(0x005bb9e0u, o + 0x4cc, o, 0x005b6dacu, a2, a3, 0);
            orig::fn_0042d0f0(o, 0, (window & 1) ? 1u : 0u, 1);
        }
        else if (!stricmp(m, 0x005b6d70))  // trigger_marker
        {
            orig::fn_00453400(0, a3, o + 0x52c, o, 0x005bba00u, a2, 0);
            readTag(0x558, 0x005bbf34);
            readTag(0x55c, 0x005bbf24);
            const int32_t lo = Field<int32_t>(obj, 0x558);
            const int32_t hi = Field<int32_t>(obj, 0x55c);
            if (!lo || !hi || lo > hi)
            {
                VCall<void>(obj, 0x54 / 4, &Fr[0x10 / 4]);
                const uint32_t name = orig::fn_00453270(o);
                GameFunction<void(__cdecl*)(uint32_t, uint32_t, ...)>(0x00407ff0)(
                    G(0x34), 0x005b6d00u, name, static_cast<double>(bf(Fr[0x10 / 4])),
                    static_cast<double>(bf(Fr[0x14 / 4])), static_cast<double>(bf(Fr[0x18 / 4])),
                    Field<uint32_t>(obj, 0x52c));
                return VCall<uint32_t>(obj, 0xdc / 4);
            }
            if (readBool(0, 0x005b6cf0, tmp))  // marker_looping
                Field<uint32_t>(obj, 0x488) |= 2;
            if (readBool(0, 0x005b6ce0, tmp))  // marker_patrol
                Field<uint32_t>(obj, 0x488) |= 1;
            if (readBool(0, 0x005b6cd0, tmp))  // marker_startloc
                Field<uint32_t>(obj, 0x484) |= 0x10000;
            if (readBool(1, 0x005bb68c, tmp))  // trigger_los
                Field<uint32_t>(obj, 0x488) |= 0x40;
            if (readBool(0, 0x005b6cb8, tmp))  // marker_strict_patrol
            {
                Field<uint32_t>(obj, 0x484) |= 0x2000000;
                const uint32_t f = Field<uint32_t>(obj, 0x488) | 0x8000;
                Field<uint32_t>(obj, 0x488) = f;
                Field<uint32_t>(obj, 0x488) = f | 1;
            }
            VCall<void>(obj, 0x19c / 4, 0xfu, 1u, 0u, 0u, 0u, 0u);
            VCall<void>(obj, 0x1b8 / 4, 0x8cu);
        }
        else if (!stricmp(m, 0x005b6cac))  // trigger_ic
        {
            Field<uint32_t>(obj, 0x488) |= 0x8000000;
            orig::fn_00453450(0, 0x005bba00u, a3, u(&Fr[0x60 / 4]), o, a2, 0);
            if (readBool(1, 0x005bb68c, tmp))  // trigger_los
                Field<uint32_t>(obj, 0x488) |= 0x40;
            orig::fn_004533a0(0x005bb9e0u, u(pickup), o, 0x005b6dacu, a2, a3, 0);  // generic_txt
            orig::fn_0042d000(0, o, Fr[0x60 / 4], Fr[0x64 / 4], u(pickup), 0, 0);
        }
        else if (!stricmp(m, 0x005b6c9c))  // trigger_scared
        {
            orig::fn_00453400(0, a3, o + 0x52c, o, 0x005bba00u, a2, 0);
            readTag(0x558, 0x005bbf34);
            readTag(0x55c, 0x005bbf24);
            Field<uint32_t>(obj, 0x488) |= 0x10;
            orig::fn_0042d0f0(o, 0, 1, 1);
        }
        else if (!stricmp(m, 0x005b6c8c))  // trigger_fire
        {
            orig::fn_00453400(0, a3, o + 0x52c, o, 0x005bba00u, a2, 0);
            readTag(0x534, 0x005b6db8);
            Field<uint32_t>(obj, 0x488) |= 0x20;
            orig::fn_0042d0f0(o, 0, 1, 1);
        }
        else if (!stricmp(m, 0x005b6c84))  // turret
        {
            readTag(0x534, 0x005b6db8);
            if (!Field<uint32_t>(obj, 0x534))
                log1(0x005b6c18);
            Field<uint32_t>(obj, 0x488) |= 0x40000000;
        }
        else if (!stricmp(m, 0x005b6c08))  // marker_boss
        {
            orig::fn_00453400(0, a3, o + 0x52c, o, 0x005bba00u, a2, 0);
            readTag(0x558, 0x005bbf34);
            readTag(0x55c, 0x005bbf24);
            Field<uint32_t>(obj, 0x560) = 0;
            readInt0(0x564, 0x005bb6c0);  // marker_sequence_max
            const uint32_t last = Field<uint32_t>(obj, 0x564);
            if (last - Field<uint32_t>(obj, 0x560) < 2)
                orig::fn_00407ff0(G(0x34), 0x005b6bb8u, orig::fn_00453270(o), last);
            Field<uint32_t>(obj, 0x568) = Field<uint32_t>(obj, 0x564) + 1;
            Field<uint32_t>(obj, 0x56c) = Field<uint32_t>(obj, 0x564) + 1;
            VCall<void>(obj, 0x1b8 / 4, 0x10eu);
        }
        else if (!stricmp(m, 0x005b6bac))  // bully
        {
            readTag(0x534, 0x005b6db8);
            if (!Field<uint32_t>(obj, 0x534))
                log1(0x005b6b40);
            Field<uint32_t>(obj, 0x488) |= 0x10000000;
        }
        else
        {
            orig::fn_00407ff0(G(0x34), 0x005b6b04u, m, Field<uint32_t>(P(a0), 0x10) + 4);
            return VCall<uint32_t>(obj, 0xdc / 4);
        }
    }
    orig::fn_004534a0(a3, o + 0x47c, o, 0x005bb864u, a2, 0, 0);  // score_value
    return VCall<uint32_t>(obj, 0x54 / 4, obj + 0x4c0);
}

static uint32_t Ai_InitGeneric_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Ai_InitGeneric(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004247f0(Ai_InitGeneric_Entry, Ai_InitGeneric_Abi)
HOOK(0x004247f0, "Ai_InitGeneric", Ai_InitGeneric_Entry, nullptr);

// 0x00425be0 (eax = other object, ecx = object; stack: a0; ret 4). Caller GenericAI_Update 0x0043ee90: knocks the
// object away from the other: physics off (Object_setPhysicsEnabled 0x004577b0 (eax = object; 0)), 0x00453b20 (esi =
// object); a 0x34-byte message {0x83, ..., +0x10 = 0x00458720 (eax = object, edx = 0), +0x14 = health 0x004555b0,
// +0x18 = direction (object - other in x/y (0x004e7780 on it, result dropped), z = 0.7 + rand * 6.1e-6, normalised
// unless its length is 0), +0x24 = 21666.67 + rand * 0.661, +0x2c = the player}; 0x004324d0 (eax = 0, ebx = object;
// other, 1), then virtual +0x17c (&message, a0). Not executed in recorded runs.
uint32_t fn_00425be0(uint8_t* other, uint8_t* obj, uint32_t a0)
{
    uint32_t S[0x5c / 4] = {};  // the original frame: [esp + k] = S[k / 4] after its three pushes
    orig::fn_004577b0(u(obj), 0);
    orig::fn_00453b20(u(obj));
    VCall<void>(obj, 0x54 / 4, &S[0x10 / 4]);
    VCall<void>(other, 0x54 / 4, &S[0x1c / 4]);
    S[0x40 / 4] = fb(bf(S[0x10 / 4]) - bf(S[0x1c / 4]));
    S[0x44 / 4] = fb(bf(S[0x14 / 4]) - bf(S[0x20 / 4]));
    orig::fn_004e7780(u(&S[0x40 / 4]), u(&S[0x40 / 4]));
    S[0x38 / 4] = orig::fn_00458720(u(obj), 0);
    S[0x3c / 4] = fb(orig::fn_004555b0(u(obj)));
    float v = static_cast<float>(Rand()) * K(0x005bcdf4);
    S[0x48 / 4] = fb(v + K(0x005bcd48));
    v = static_cast<float>(Rand()) * K(0x005bcdf0);
    S[0x4c / 4] = fb(v + K(0x005bcdec));
    float len = bf(S[0x44 / 4]) * bf(S[0x44 / 4]);
    len = len + bf(S[0x40 / 4]) * bf(S[0x40 / 4]);
    len = len + bf(S[0x48 / 4]) * bf(S[0x48 / 4]);
    len = std::sqrt(len);
    if (!(len == K(0x0056c05c)))
    {
        const float inv = K(0x005bcb2c) / len;
        S[0x40 / 4] = fb(bf(S[0x40 / 4]) * inv);
        S[0x44 / 4] = fb(bf(S[0x44 / 4]) * inv);
        S[0x48 / 4] = fb(inv * bf(S[0x48 / 4]));
    }
    S[0x28 / 4] = 0x83;
    S[0x54 / 4] = G(0x748);
    orig::fn_004324d0(0, u(obj), u(other), 1);
    return VCall<uint32_t>(obj, 0x17c / 4, &S[0x28 / 4], a0);
}

static uint32_t fn_00425be0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_00425be0(B(eax), B(ecx), a0);
}
ABI_ENTRY_00425be0(fn_00425be0_Entry, fn_00425be0_Abi)
HOOK(0x00425be0, "fn_00425be0", fn_00425be0_Entry, nullptr);

// 0x00425d30 (ebx = troop; stack: jumper flag (byte); ret 4). Callers 0x00404e50, 0x0040c590, 0x004247f0: 0x0047b9f0
// (eax = tag word +0x394 with al = flag, ecx = troop +0xb08; troop, tag, 0); then the fire range: with +0xb08 set
// "jumper_fire_range_min" (400) / "jumper_fire_range_max" (450), else "fire_range_min" (200) / "fire_range_max" (350)
// into +0x5d0 / +0x5d4 (0x004534a0, esi = 0, edi = destination; troop, name, 0, default, 0), and +0x5d8 = min +
// rand * (max - min) / 32768. Returns rand's value.
uint32_t fn_00425d30(uint8_t* troop, uint32_t a0)
{
    const uint32_t tag = Field<uint16_t>(troop, 0x394);
    orig::fn_0047b9f0((tag & ~0xffu) | static_cast<uint8_t>(a0), u(troop) + 0xb08, u(troop), tag, 0);
    const uint32_t t = u(troop);
    if (Field<uint32_t>(troop, 0xb08))
    {
        orig::fn_004534a0(0, t + 0x5d0, t, 0x005b6aecu, 0, 0x43c80000u, 0);
        orig::fn_004534a0(0, t + 0x5d4, t, 0x005b6ad4u, 0, 0x43e10000u, 0);
    }
    else
    {
        orig::fn_004534a0(0, t + 0x5d0, t, 0x005b6ac4u, 0, 0x43480000u, 0);
        orig::fn_004534a0(0, t + 0x5d4, t, 0x005b6ab4u, 0, 0x43af0000u, 0);
    }
    const float hi = Field<float>(troop, 0x5d4);
    const float lo = Field<float>(troop, 0x5d0);
    const int32_t r = Rand();
    float v = static_cast<float>(r) * (hi - lo);
    v = v * K(0x005bcbc0);
    Field<float>(troop, 0x5d8) = v + lo;
    return static_cast<uint32_t>(r);
}

static uint32_t fn_00425d30_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_00425d30(B(ebx), a0);
}
ABI_ENTRY_00425d30(fn_00425d30_Entry, fn_00425d30_Abi)
HOOK(0x00425d30, "fn_00425d30", fn_00425d30_Entry, nullptr);

// 0x00425df0 (ecx = object; stack: message; ret 4). Slot 7 (+0x1c) of many AI classes (PenguinObject, Q112Object,
// Q112Boss, SaucerObject, Polewart, ShrubPatrol, SheepObject, ColossusObject, BowGruntObject, SniperCaptain,
// GoliathFireball, GenericAIObject, ...): 0x00457810 (eax = object: +0x424 |= 0x40), 0x004527d0 (ecx = object;
// message), message +0 = 0x170001, and when +0x1bc is set 0x00458600 (esi = object).
uint32_t fn_00425df0(uint8_t* obj, uint8_t* msg)
{
    orig::fn_00457810(u(obj));
    uint32_t r = orig::fn_004527d0(u(obj), u(msg));
    Field<uint32_t>(msg, 0) = 0x170001;
    r = Field<uint32_t>(obj, 0x1bc);
    if (r)
        r = orig::fn_00458600(u(obj));
    return r;
}

static uint32_t fn_00425df0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00425df0(B(ecx), B(a0));
}
ABI_ENTRY_00425df0(fn_00425df0_Entry, fn_00425df0_Abi)
HOOK(0x00425df0, "fn_00425df0", fn_00425df0_Entry, nullptr);
