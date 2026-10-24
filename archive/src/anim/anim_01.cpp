// Recovered: animation layer (batch anim_01, 0x00408820-0x00493ca0). Static recovery 2026-10-05, compile-checked only
// (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified from the disassembly.
//
// Shared structure seen across this file (from the disassembly, names are guesses unless stated):
// - animated object ("actor"): +0x248 four 0x20-byte animation slots (slot = +0x248 + index * 0x20; 0x00408800
//   maps index 4 to slot 0), +0x374 default time, +0x410 skeleton/pose object, +0x414 animation set (+0xc: array of
//   0xc-byte entries {float length, u32 flags, anim*}, +0x10: name table looked up by 0x004f1160).
// - animation slot (0x20 bytes): +0 entry index (-1 none), +4 speed, +0x10 user value, +0x14 blend target,
//   +0x18 blend weight, +0x1c flags.
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
// A float constant of the game's .rdata (0x0056c05c = 0.0, 0x005bcb2c = 1.0, 0x005bcb4c = 0.5).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
// 0x0056a7c8 (_ftol): truncates st0; callers use EAX.
inline int32_t Ftol(float x)
{
    return static_cast<int32_t>(static_cast<int64_t>(x));
}
inline uint8_t* GameA84()
{
    return Field<uint8_t*>(g_game, 0xa84);
}
// 0x004f1160 (eax = name table, stack: key; callee pops 4): looks a name up in the animation set's name table
// (actor +0x414 -> +0x10 -> [0]) and returns a pointer to the entry index, or 0.
inline int32_t* AnimLookup(uint8_t* actor, uint32_t key)
{
    const uint32_t table = Field<uint32_t>(Field<uint8_t*>(Field<uint8_t*>(actor, 0x414), 0x10), 0);
    return reinterpret_cast<int32_t*>(orig::fn_004f1160(table, key));
}
// The common "element i of a counted list" accessor: list = (+4) -> +4; i < +0x1c and +0x20 != 0 gives
// [[list+0x24]+0x10][i], else 0.
inline uint32_t ListAt(uint8_t* list, uint32_t i)
{
    if (i < Field<uint32_t>(list, 0x1c) && Field<uint32_t>(list, 0x20) != 0)
        return Field<uint32_t*>(Field<uint8_t*>(list, 0x24), 0x10)[i];
    return 0;
}
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t fn_00408820(uint8_t* actor, float weight);
uint32_t AnimSlot_Set(uint32_t index, uint32_t flags, uint8_t* actor, uint32_t entry, float speed, uint32_t user,
                      float weight, uint8_t* slots);
uint32_t fn_00408c70(uint32_t flags, uint32_t name0, uint32_t user, uint8_t* actor, uint32_t name1, uint32_t name2,
                     uint32_t name3, float w02, float w1, float w3);
uint32_t Anim_Set(uint32_t index, uint8_t* actor, uint32_t name, uint32_t flags, uint32_t a2, uint32_t a3, uint32_t a4,
                  uint32_t a5);
uint32_t fn_00408e30(uint32_t flags, uint8_t* actor, int32_t index, uint32_t name, uint32_t a1, uint32_t a2,
                     uint32_t a3, uint8_t force);
uint32_t fn_00408ee0(uint8_t* out, uint8_t* actor, uint8_t* slots, int32_t time);
float fn_00409d90(uint8_t* body, uint8_t* slot);
uint32_t fn_00409ec0(uint8_t* actor, float dt, uint8_t force);

// ================================================================================================================
// Animation slots
// ================================================================================================================

// 0x00408820 (ecx unused; stack: actor, weight (float); ret 8): when Game+0xa84 -> +0xf34 is set, the actor has a
// pose object (+0x410, whose +0x20 is nonzero), +0x421 is clear and weight > 0: if +0x2cc < 1.0, bumps the pose's
// stamp (+8, wrapping 0 to 1) into +0x1d0, walks the clip list of +0x1dc and for each clip refreshes the 0x1c-byte
// track records at +0x1d8 (track lookups through 0x004e0f40) and applies them with 0x004eccc0 (eax = +0x1d4 count,
// ecx = pose, edi = +0x1d8; stack: stamp, +0x2c8, 5); then applies every clip of the current animation-set entry
// (0x004f1160 of actor+4) with 0x004ec410(clip, entry+0x14, pose, stamp, 0, 5); sets +0x2cc = +0x2c8 = 1.0. Always
// +0x2d0 = weight. Otherwise clears +0x2c8/+0x2cc/+0x2d0 and returns 0. guess: snap the pose to the bind/base
// animation before blending. Return value: callers (0x00408a70, 0x0043ee90) ignore it; the "already >= 1.0" path
// returns 0 here (the original leaves FPU status bits in EAX).
uint32_t fn_00408820(uint8_t* actor, float weight)
{
    uint8_t* pose = Field<uint8_t*>(actor, 0x410);
    if (!Field<uint8_t>(GameA84(), 0xf34) || !pose || !Field<uint32_t>(pose, 0x20) || Field<uint8_t>(actor, 0x421) ||
        !(weight > K(0x0056c05c)))
    {
        Field<uint32_t>(actor, 0x2cc) = 0;
        Field<uint32_t>(actor, 0x2c8) = 0;
        Field<uint32_t>(actor, 0x2d0) = 0;
        return 0;
    }
    if (!(Field<float>(actor, 0x2cc) < K(0x005bcb2c)))
    {
        Field<float>(actor, 0x2d0) = weight;
        return 0;
    }
    if (++Field<uint32_t>(pose, 8) == 0)
        Field<uint32_t>(pose, 8) = 1;
    Field<uint32_t>(actor, 0x1d0) = Field<uint32_t>(pose, 8);
    if (Field<uint32_t>(actor, 0x1dc))
    {
        uint32_t i = 0;
        for (;;)
        {
            uint8_t* list = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(actor, 0x1dc), 4), 4);
            const uint32_t clip = ListAt(list, i);
            ++i;
            if (!clip)
                break;
            const uint32_t header = clip + Field<uint16_t>(P(clip), 2);
            const uint32_t data = header + 4;
            if (!Field<uint32_t>(actor, 0x1d4))
                continue;
            Field<uint32_t>(actor, 0x1e4) = Field<uint32_t>(P(header), 0);
            int32_t n = static_cast<int32_t>(Field<uint32_t>(actor, 0x1d4));
            bool apply = true;
            if (--n != 0)
            {
                uint8_t* rec = actor + 0x200;
                do
                {
                    uint8_t* tl = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(rec, static_cast<uint32_t>(-8)), 4), 4);
                    const uint32_t count = Field<uint32_t>(tl, 0x1c);
                    if (!count || !Field<uint32_t>(tl, 0x20))
                    {
                        apply = n == 0;
                        break;
                    }
                    uint8_t* it = Field<uint8_t*>(tl, 0x24);
                    Field<uint32_t>(it, 0) = count;
                    Field<uint32_t>(it, 0x18) = data;
                    orig::fn_004e0f40(u(it));
                    if (Field<uint32_t>(it, 0xc))
                    {
                        apply = n == 0;
                        break;
                    }
                    const uint32_t found = Field<uint32_t*>(it, 0x10)[Field<uint32_t>(it, 8)];
                    if (!found)
                    {
                        apply = n == 0;
                        break;
                    }
                    Field<uint32_t>(rec, 0) = Field<uint32_t>(P(found + Field<uint16_t>(P(found), 2)), 0);
                    rec += 0x1c;
                } while (--n != 0);
            }
            if (apply)
                orig::fn_004eccc0(Field<uint32_t>(actor, 0x1d4), Field<uint32_t>(actor, 0x410), u(actor + 0x1d8),
                                  Field<uint32_t>(actor, 0x1d0), Field<uint32_t>(actor, 0x2c8), 5);
        }
    }
    const int32_t* found = AnimLookup(actor, u(actor + 4));
    if (found && *found >= 0)
    {
        uint8_t* entry = Field<uint8_t*>(Field<uint8_t*>(actor, 0x414), 0xc) + *found * 0xc;
        uint8_t* anim = Field<uint8_t*>(entry, 8);
        if (anim)
        {
            uint32_t i = 0;
            for (;;)
            {
                uint8_t* list = Field<uint8_t*>(Field<uint8_t*>(anim, 0x2c), 4);
                uint32_t clip = ListAt(list, i);
                if (clip)
                    clip = Field<uint32_t>(P(clip + Field<uint16_t>(P(clip), 2)), 0);
                ++i;
                if (!clip)
                    break;
                orig::fn_004ec410(clip, Field<uint32_t>(anim, 0x14), Field<uint32_t>(actor, 0x410),
                                  Field<uint32_t>(actor, 0x1d0), 0, 5);
            }
        }
    }
    Field<uint32_t>(actor, 0x2cc) = 0x3f800000;
    Field<uint32_t>(actor, 0x2c8) = 0x3f800000;
    Field<float>(actor, 0x2d0) = weight;
    return 0x3f800000;
}

// 0x00408a70 (eax = slot index, ecx = flags; stack: actor, entry index, speed (float), user value, weight (float),
// slots (0 = actor+0x248); ret 0x18): sets one animation slot (slot = slots + index*0x20). Without Game+0xa84 ->
// +0xf34 the flags get 0x20000 (and a weight of 0 under the 0x4000/0x8000 conditions). The blend weight/target
// (+0x18/+0x14) logic depends on flags 0x4000/0x8000/0x20000 and on index 1 and 3 (index 1 may push the previous
// slot into slots 2/3, the "cross-fade" copy at 0x00408b41); 0x00408820 is called to snap the base pose. Flag 0x400
// derives the speed from the entry length (+4 / entry.length, or 0.5 without an entry) and swaps 0x400 for 0x200;
// 0x800 stores the speed, 0x40000 zeroes it. Then +0x1c = flags | 0x1000, +0 = entry, +0x10 = user value.
// Callers (0x00408c70, Anim_Set, 0x00475ff0) ignore the return value. guess: AnimSlot_Set (name from behaviour).
uint32_t AnimSlot_Set(uint32_t index, uint32_t flags, uint8_t* actor, uint32_t entry, float speed, uint32_t user,
                      float weight, uint8_t* slots)
{
    uint32_t f = flags;
    uint8_t* slot = (slots ? slots : actor + 0x248) + (index << 5);
    if (!Field<uint8_t>(GameA84(), 0xf34))
        f |= 0x20000;
    const uint32_t noBase = f & 0x20000;
    if (noBase && (!(f & 0x4000) || index == 3) && (!(f & 0x8000) || index == 1))
        weight = 0.0f;
    if (f & 0x4000)
    {
        if (index != 3)
        {
            Field<float>(slot, 0x18) = weight;
            Field<uint32_t>(slot, 0x14) = 0;
        }
        else if (noBase)
        {
            Field<uint32_t>(slot, 0x14) = 0;
        }
        else
        {
            fn_00408820(actor, weight);
            Field<uint32_t>(slot, 0x18) = 0;
            Field<uint32_t>(slot, 0x14) = 0;
        }
    }
    else if (index == 1)
    {
        if (!(weight > K(0x0056c05c)))
        {
            if (!noBase)
            {
                fn_00408820(actor, 0.0f);
                Field<uint32_t>(slot, 0x18) = 0;
                Field<uint32_t>(slot, 0x14) = 0;
            }
        }
        else if (!(Field<float>(slot, 0x18) < K(0x005bcb2c)) && !(Field<uint32_t>(slot, 0x1c) & 0x4000))
        {
            Field<float>(slot, 0x14) = weight;
            Field<uint32_t>(slot, 0x18) = 0x3f800000;
        }
        else if (Field<float>(slot, 0x18) > K(0x0056c05c) || (Field<uint32_t>(slot, 0x1c) & 0x4000))
        {
            fn_00408820(actor, weight);
            Field<uint32_t>(slot, 0x18) = 0;
            Field<uint32_t>(slot, 0x14) = 0;
        }
        else
        {
            std::memcpy(slot + 0x20, slot - 0x20, 0x20);
            std::memcpy(slot + 0x40, slot, 0x20);
            uint32_t v = Field<uint32_t>(slot, 0x3c) | 0x11000;
            Field<uint32_t>(slot, 0x3c) = v;
            Field<uint32_t>(slot, 0x3c) = v & 0xffffbfff;
            v = Field<uint32_t>(slot, 0x5c) | 0x11000;
            Field<uint32_t>(slot, 0x5c) = v;
            Field<uint32_t>(slot, 0x5c) = v & 0xffffbfff;
            Field<float>(slot, 0x14) = weight;
            Field<uint32_t>(slot, 0x18) = 0x3f800000;
        }
    }
    else if (f & 0x8000)
    {
        Field<float>(slot, 0x18) = weight;
        Field<uint32_t>(slot, 0x14) = 0;
    }
    else if (!noBase)
    {
        if (Field<uint32_t>(slot, 0x1c) & 0x4000)
        {
            Field<uint32_t>(slot, 0x18) = 0;
            Field<uint32_t>(slot, 0x14) = 0;
        }
        else
        {
            Field<float>(slot, 0x14) = weight;
            Field<uint32_t>(slot, 0x18) = 0x3f800000;
        }
    }
    float v;
    if (f & 0x400)
    {
        const int32_t e = Field<int32_t>(slot, 0);
        const float* length =
            e == -1 ? nullptr : reinterpret_cast<const float*>(Field<uint8_t*>(Field<uint8_t*>(actor, 0x414), 0xc) + e * 0xc);
        if (length)  // the original also tests the computed address against 0
            v = Field<float>(slot, 4) / *length;
        else
            v = K(0x005bcb4c);
        f = (f & 0xfffffbff) | 0x200;
    }
    else
    {
        v = speed;
    }
    if (f & 0x800)
        Field<float>(slot, 4) = v;
    if (f & 0x40000)
        Field<uint32_t>(slot, 4) = 0;
    Field<uint32_t>(slot, 0x1c) = f | 0x1000;
    Field<uint32_t>(slot, 0) = entry;
    Field<uint32_t>(slot, 0x10) = user;
    return 0;
}

// 0x00408c70 (eax = flags, edx = name 0, ebx = user value, esi = actor; stack: name 1, name 2, name 3, weight for
// slots 0/2, weight slot 1, weight slot 3; ret 0x18): looks the four names up (0x004f1160, -1 when missing), then
// AnimSlot_Set slots 3, 2, 1, 0 (flags | 0x4000, speed 0, slots = actor's own) and 0x00409ec0(actor, 0, 0).
// symbols/abi.csv lists EBP as an input: it is only saved (push ebp) and then set to -1, not read.
uint32_t fn_00408c70(uint32_t flags, uint32_t name0, uint32_t user, uint8_t* actor, uint32_t name1, uint32_t name2,
                     uint32_t name3, float w02, float w1, float w3)
{
    const int32_t* p = AnimLookup(actor, name0);
    const uint32_t e0 = p ? static_cast<uint32_t>(*p) : 0xffffffffu;
    p = AnimLookup(actor, name1);
    const uint32_t e1 = p ? static_cast<uint32_t>(*p) : 0xffffffffu;
    p = AnimLookup(actor, name2);
    const uint32_t e2 = p ? static_cast<uint32_t>(*p) : 0xffffffffu;
    p = AnimLookup(actor, name3);
    const uint32_t e3 = p ? static_cast<uint32_t>(*p) : 0xffffffffu;
    const uint32_t f = flags | 0x4000;
    AnimSlot_Set(3, f, actor, e3, 0.0f, user, w3, nullptr);
    AnimSlot_Set(2, f, actor, e2, 0.0f, user, w02, nullptr);
    AnimSlot_Set(1, f, actor, e1, 0.0f, user, w1, nullptr);
    AnimSlot_Set(0, f, actor, e0, 0.0f, user, w02, nullptr);
    return fn_00409ec0(actor, 0.0f, 0);
}

// 0x00408d90 (eax = entry index or < 0 to look up the name, esi = actor; stack: name, flags, a2 (user), a3 (weight),
// a4 (slots), a5 (speed); ret 0x18): Anim_Set (tracker name). Resolves the name (0x004f1160); when there is no entry
// it logs Debug_Printf(Game+0x34, 7, 0x005bbb7c, name) and returns. Otherwise AnimSlot_Set slot 1 with entry -1 and
// slot 0 with the entry (both with flags, speed a5, user a2, weight a3, slots a4), then 0x00409ec0(actor, 0, 0).
uint32_t Anim_Set(uint32_t index, uint8_t* actor, uint32_t name, uint32_t flags, uint32_t a2, uint32_t a3, uint32_t a4,
                  uint32_t a5)
{
    int32_t e = static_cast<int32_t>(index);
    if (e < 0)
    {
        const int32_t* p = AnimLookup(actor, name);
        if (!p)
            e = -1;
        else
            e = *p;
    }
    if (e == -1)
    {
        orig::fn_00407ff0(Field<uint32_t>(g_game, 0x34), 7, 0x005bbb7cu, name);
        return 0;
    }
    AnimSlot_Set(1, flags, actor, 0xffffffffu, bf(a5), a2, bf(a3), B(a4));
    AnimSlot_Set(0, flags, actor, static_cast<uint32_t>(e), bf(a5), a2, bf(a3), B(a4));
    return fn_00409ec0(actor, 0.0f, 0);
}

// 0x00408e30 (eax = flags, ecx = actor, edx = entry index or < 0 to look up the name; stack: name, a1 (user),
// a2 (weight), a3 (speed), force (byte); ret 0x14; returns AL): plays an animation unless it is already in slot 0
// (0x00408800 index 4): same entry and no force -> when flag 0x40 differs from the slot's, Anim_Set with flags
// & ~0x380 and returns 1, else returns 0; otherwise Anim_Set(flags, name, a1, a2, slots 0, a3) and returns 1.
uint32_t fn_00408e30(uint32_t flags, uint8_t* actor, int32_t index, uint32_t name, uint32_t a1, uint32_t a2,
                     uint32_t a3, uint8_t force)
{
    if (index < 0)
    {
        const int32_t* p = AnimLookup(actor, name);
        index = p ? *p : -1;
    }
    uint8_t* slot = B(orig::fn_00408800(4, u(actor)));
    if (index == Field<int32_t>(slot, 0) && !force)
    {
        if (!((Field<uint32_t>(slot, 0x1c) ^ flags) & 0x40))
            return 0;
        Anim_Set(static_cast<uint32_t>(index), actor, name, flags & 0xfffffc7f, a1, a2, 0, a3);
        return 1;
    }
    Anim_Set(static_cast<uint32_t>(index), actor, name, flags, a1, a2, 0, a3);
    return 1;
}

namespace
{
// (1 - cos(w * 0x005bcb70)) * 0x005bcb4c exactly as 0x00408fa3 / 0x00408fef / 0x0040a158 compute it: fld, fmul,
// fcos, fsubr, fmul on the x87 stack, rounded once on the store. std::cos rounded cos to float before the subtraction
// (and is not fcos), which loses the low bits of 1 - cos for small weights.
inline float CosBlend(float w)
{
    const float k = K(0x005bcb70), one = K(0x005bcb2c), half = K(0x005bcb4c);
    float r;
    __asm {
        fld w
        fmul k
        fcos
        fsubr one
        fmul half
        fstp r
    }
    return r;
}
}  // namespace

// 0x00408ee0 (edx = output records; stack: actor, slots (0 = actor's), time (< 0 = actor+0x374); ret 0xc): builds
// one 0x1c-byte blend record per active slot (2 slots, extended to 4 when a weight is nonzero or flag 0x4000):
// +0 entry, +4 0x00453120(eax = actor, edx = time; stack: entry index) unless entry flag 1, +0x10 frame =
// ftol(anim+0x20 * anim+0x1c * slot speed) + anim+0x14, +0x14 mode (0/1, 0x10/0x11, 0x20/0x21, 0x40/0x41, 0x80/0x81
// by slot flags 0x8000/0x4000 and the slot number), +0x18 weight ((1 - cos(w * 0x005bcb70)) * 0.5 for blended
// slots, the raw weight otherwise). Returns the record count.
uint32_t fn_00408ee0(uint8_t* out, uint8_t* actor, uint8_t* slots, int32_t time)
{
    if (!slots)
        slots = B(orig::fn_00408800(4, u(actor)));
    if (time < 0)
        time = Field<int32_t>(actor, 0x374);
    uint32_t count = 0;
    int32_t limit = 2;
    uint8_t* rec = out;
    for (int32_t i = 0; i < limit; ++i)
    {
        uint8_t* slot = slots + i * 0x20;
        const int32_t e = Field<int32_t>(slot, 0);
        if (e == -1)
            continue;
        uint8_t* entry = Field<uint8_t*>(Field<uint8_t*>(actor, 0x414), 0xc) + e * 0xc;
        if (!Field<uint32_t>(entry, 8))
            continue;
        Field<uint8_t*>(rec, 0) = entry;
        if (!(Field<uint8_t>(entry, 4) & 1))
            Field<uint32_t>(rec, 4) = orig::fn_00453120(u(actor), static_cast<uint32_t>(time), Field<uint32_t>(slot, 0));
        else
            Field<uint32_t>(rec, 4) = 0;
        uint8_t* anim = Field<uint8_t*>(entry, 8);
        const float frames = static_cast<float>(Field<int32_t>(anim, 0x20)) * static_cast<float>(Field<int32_t>(anim, 0x1c));
        Field<int32_t>(rec, 0x10) = Ftol(frames * Field<float>(slot, 4)) + Field<int32_t>(anim, 0x14);
        const uint32_t sf = Field<uint32_t>(slot, 0x1c);
        if (!(sf & 0xc000))
        {
            if (i < 2)
            {
                Field<uint32_t>(rec, 0x14) = 0;
                const float w = CosBlend(Field<float>(slot, 0x18));
                Field<float>(rec, 0x18) = w;
                if (!(w == K(0x0056c05c)))
                    limit = 4;
            }
            else
            {
                Field<uint32_t>(rec, 0x14) = 1;
            }
        }
        else if (sf & 0x8000)
        {
            if (i == 1)
            {
                const float w = CosBlend(Field<float>(slot, 0x18));
                Field<float>(rec, 0x18) = w;
                if (!(w == K(0x0056c05c)))
                    limit = 4;
                Field<uint32_t>(rec, 0x14) = 0x11;
            }
            else
            {
                Field<uint32_t>(rec, 0x18) = Field<uint32_t>(slot, 0x18);
                Field<uint32_t>(rec, 0x14) = (static_cast<uint32_t>(i) & 1) | (i < 2 ? 0x10u : 0x20u);
            }
        }
        else
        {
            Field<uint32_t>(rec, 0x18) = Field<uint32_t>(slot, 0x18);
            limit = 4;
            Field<uint32_t>(rec, 0x14) = (static_cast<uint32_t>(i) & 1) | (i < 2 ? 0x40u : 0x80u);
        }
        ++count;
        rec += 0x1c;
    }
    return count;
}

// 0x00409d90 (edi = body object; stack: slot; ret 4; returns st0): the playback rate of a speed-driven slot.
// Without a rate block (slot+0x10) the rate is 1.0. Block flag 2: its +0x18. Otherwise the body's velocity
// (Body_getLinearVelocity 0x00458000) is reduced to a speed - flag 1: |v| * |block xyz|; else v dot (block xyz
// transformed by body virtual +0x60's matrix, 0x004e37f0 count 1 mask -0x11) - clamped to [+0x10, +0x14], mapped by
// (speed - +0x1c) / (+0x24 - +0x1c) (1.0 when that range is <= 0x005bcd3c) to lerp(+0x18, +0x20). Negated when slot
// flag 0x40. Callers (0x00409ec0, 0x0040a200, 0x0040a3f0) consume st0.
float fn_00409d90(uint8_t* body, uint8_t* slot)
{
    float r = K(0x005bcb2c);
    uint8_t* blk = Field<uint8_t*>(slot, 0x10);
    if (blk)
    {
        const uint32_t bflags = Field<uint32_t>(blk, 0xc);
        if (bflags & 2)
        {
            r = Field<float>(blk, 0x18);
        }
        else
        {
            float v[3];
            orig::fn_00458000(u(body), u(v));
            float s;
            if (bflags & 1)
            {
                const float b0 = Field<float>(blk, 0), b4 = Field<float>(blk, 4), b8 = Field<float>(blk, 8);
                const float speed = std::sqrt(v[2] * v[2] + v[1] * v[1] + v[0] * v[0]);
                const float len = std::sqrt(b0 * b0 + b4 * b4 + b8 * b8);
                s = len * speed;
            }
            else
            {
                const uint32_t m = VCall<uint32_t>(body, 0x60 / 4);
                float d[3];
                orig::fn_004e37f0(u(blk), m, 1, u(d), 0xffffffefu);
                s = d[2] * v[2] + d[1] * v[1] + d[0] * v[0];
            }
            if (s < Field<float>(blk, 0x10))
                s = Field<float>(blk, 0x10);
            if (s > Field<float>(blk, 0x14))
                s = Field<float>(blk, 0x14);
            float t = K(0x005bcb2c);
            const float range = Field<float>(blk, 0x24) - Field<float>(blk, 0x1c);
            if (range > K(0x005bcd3c))
                t = (s - Field<float>(blk, 0x1c)) / range;
            r = (K(0x005bcb2c) - t) * Field<float>(blk, 0x18) + t * Field<float>(blk, 0x20);
        }
    }
    if (Field<uint8_t>(slot, 0x1c) & 0x40)
        r = -r;
    return r;
}

// 0x00409ec0 (eax = actor; stack: dt (float), force (byte); ret 8): advances the actor's animation slots 3..0
// (0x00408800). A slot is processed when dt != 0 or its flag 0x1000 (just set) is on; it clears 0x1000. With an
// entry (actor+0x414 -> +0xc, 0xc bytes, anim at +8) the slot time (+4) advances by rate (0x00409d90) * dt (dt is 0
// under flags 0x30, flag 0x20 cleared), after the pending requests: 0x200 time *= entry length, 0x80 reset (to +0xc
// when flags 4 and 2, then flag 8), 0x100 jump to the end (length, or length - +0xc when 4 and 2, then flag 8). Running
// past either end sets flag 2, wraps the time by the length and stores the overshoot in +0xc; without loop flag 1
// the time sticks to 0/length. +8 keeps the previous time. The blend weight +0x18 then drops by dt / +0x14 (floored
// at 0). Actor level: +0x2cc drops by dt / +0x2d0 (floored at 0) and +0x2c8 = (1 - cos(+0x2cc * 0x005bcb70)) * 0.5.
// When a slot changed or force: bumps the pose stamp (pose +8 -> actor+0x1d0) unless set flag 0x80000000, then
// unless (set flag 0x80000000 and actor+0x424 bit 0x4000) rebuilds the blend records: actor+0x1d4 =
// 0x00408ee0(actor+0x1d8, actor, 0, -1), actor+0x424 |= 0x4000. guess: Anim_Update.
uint32_t fn_00409ec0(uint8_t* actor, float dt, uint8_t force)
{
    uint32_t changed = 0;
    for (uint32_t i = 4; i-- != 0;)
    {
        uint8_t* slot = B(orig::fn_00408800(i, u(actor)));
        if (dt == K(0x0056c05c) && !(Field<uint32_t>(slot, 0x1c) & 0x1000))
            continue;
        Field<uint32_t>(slot, 0x1c) &= 0xffffefff;
        changed = 1;
        const int32_t e = Field<int32_t>(slot, 0);
        uint8_t* entry = nullptr;
        if (e != -1)
            entry = Field<uint8_t*>(Field<uint8_t*>(actor, 0x414), 0xc) + e * 0xc;
        if (entry && Field<uint32_t>(entry, 8))
        {
            const float rate = fn_00409d90(actor, slot);
            float d = dt;
            if (Field<uint8_t>(slot, 0x1c) & 0x30)
            {
                d = K(0x0056c05c);
                Field<uint32_t>(slot, 0x1c) &= 0xffffffdf;
            }
            if (Field<uint32_t>(slot, 0x1c) & 0x200)
            {
                Field<float>(slot, 4) = Field<float>(entry, 0) * Field<float>(slot, 4);
                Field<uint32_t>(slot, 0x1c) &= 0xfffffdff;
            }
            uint32_t f = Field<uint32_t>(slot, 0x1c) & 0xfffffff7;
            Field<uint32_t>(slot, 0x1c) = f;
            if (f & 0x80)
            {
                Field<uint32_t>(slot, 4) = 0;
                if ((f & 4) && (f & 2))
                {
                    Field<uint32_t>(slot, 4) = Field<uint32_t>(slot, 0xc);
                    Field<uint32_t>(slot, 0x1c) |= 8;
                }
                Field<uint32_t>(slot, 0x1c) &= 0xffffff7f;
            }
            f = Field<uint32_t>(slot, 0x1c);
            if (f & 0x100)
            {
                const float length = Field<float>(entry, 0);
                Field<float>(slot, 4) = length;
                if ((f & 4) && (f & 2))
                {
                    Field<float>(slot, 4) = length - Field<float>(slot, 0xc);
                    Field<uint32_t>(slot, 0x1c) = f | 8;
                }
                Field<uint32_t>(slot, 0x1c) &= 0xfffffeff;
            }
            if (!(d == K(0x0056c05c)))
            {
                Field<uint32_t>(slot, 0x1c) &= 0xfffffffd;
                Field<uint32_t>(slot, 8) = Field<uint32_t>(slot, 4);
                const float t = rate * d + Field<float>(slot, 4);
                Field<float>(slot, 4) = t;
                if (rate < K(0x0056c05c))
                {
                    if (t < K(0x0056c05c))
                    {
                        Field<uint32_t>(slot, 0x1c) |= 2;
                        while (Field<float>(slot, 4) < K(0x0056c05c))
                            Field<float>(slot, 4) = Field<float>(entry, 0) + Field<float>(slot, 4);
                        Field<float>(slot, 0xc) = Field<float>(entry, 0) - Field<float>(slot, 4);
                        if (!(Field<uint8_t>(slot, 0x1c) & 1))
                            Field<uint32_t>(slot, 4) = 0;
                    }
                }
                else if (t >= Field<float>(entry, 0))  // unordered sets C0: no wrap
                {
                    Field<uint32_t>(slot, 0x1c) |= 2;
                    while (Field<float>(slot, 4) >= Field<float>(entry, 0))
                        Field<float>(slot, 4) = Field<float>(slot, 4) - Field<float>(entry, 0);
                    Field<uint32_t>(slot, 0xc) = Field<uint32_t>(slot, 4);
                    if (!(Field<uint8_t>(slot, 0x1c) & 1))
                        Field<uint32_t>(slot, 4) = Field<uint32_t>(entry, 0);
                }
            }
        }
        if (Field<float>(slot, 0x18) > K(0x0056c05c) && !(Field<float>(slot, 0x14) == K(0x0056c05c)))
        {
            const float w = Field<float>(slot, 0x18) - dt / Field<float>(slot, 0x14);
            Field<float>(slot, 0x18) = w;
            if (w < K(0x0056c05c))
                Field<uint32_t>(slot, 0x18) = 0;
        }
    }
    if (Field<float>(actor, 0x2cc) > K(0x0056c05c) && !(Field<float>(actor, 0x2d0) == K(0x0056c05c)))
    {
        const float w = Field<float>(actor, 0x2cc) - dt / Field<float>(actor, 0x2d0);
        Field<float>(actor, 0x2cc) = w;
        if (w < K(0x0056c05c))
            Field<uint32_t>(actor, 0x2cc) = 0;
        Field<float>(actor, 0x2c8) = CosBlend(Field<float>(actor, 0x2cc));
    }
    if (!changed && !force)
        return 0;
    if (!(Field<uint32_t>(Field<uint8_t*>(actor, 0x414), 8) & 0x80000000))
    {
        uint8_t* pose = Field<uint8_t*>(actor, 0x410);
        if (pose)
        {
            if (++Field<uint32_t>(pose, 8) == 0)
                Field<uint32_t>(pose, 8) = 1;
            Field<uint32_t>(actor, 0x1d0) = Field<uint32_t>(pose, 8);
        }
    }
    uint8_t* set = Field<uint8_t*>(actor, 0x414);
    if ((Field<uint32_t>(set, 8) & 0x80000000) && (Field<uint32_t>(actor, 0x424) & 0x4000))
        return u(set);
    const uint32_t n = fn_00408ee0(actor + 0x1d8, actor, nullptr, -1);
    Field<uint32_t>(actor, 0x1d4) = n;
    Field<uint32_t>(actor, 0x424) |= 0x4000;
    return n;
}

namespace
{
// Frame number of time t in an animation: ftol(anim+0x20 * anim+0x1c * t + anim+0x14) (fild/fimul/fmul/fiadd/_ftol).
inline uint32_t AnimFrame(uint8_t* anim, float t)
{
    const float frames = static_cast<float>(Field<int32_t>(anim, 0x20)) * static_cast<float>(Field<int32_t>(anim, 0x1c));
    return static_cast<uint32_t>(Ftol(frames * t + static_cast<float>(Field<int32_t>(anim, 0x14))));
}
// 0x004eb480 (esi = anim; stack: from frame, to frame, callback 0x00409140, actor; caller pops): fires the
// animation's frame events between two frames through the callback. guess (from the arguments).
inline void AnimEvents(uint8_t* anim, uint32_t from, uint32_t to, uint8_t* actor)
{
    orig::fn_004eb480(u(anim), from, to, 0x00409140u, u(actor));
}
}  // namespace

// 0x0040a200 (stack: actor; ret 4): unless actor+0x421, copies the four slots (actor+0x248, 0x80 bytes) to a local
// and, for slots 3..0 without flag 0x10000 and with an entry, fires the frame events (0x004eb480 with callback
// 0x00409140) over the interval the slot just played: previous time (+8) to time (+4); when the slot wrapped (flag 2)
// split at the end - forward (rate 0x00409d90 >= 0): prev..length, then 0..time when looping (flag 1); backward:
// prev..0, then length..time when looping. guess: Anim_FireEvents.
uint32_t fn_0040a200(uint8_t* actor)
{
    if (Field<uint8_t>(actor, 0x421))
        return 0;
    uint8_t copy[0x80];
    std::memcpy(copy, actor + 0x248, sizeof copy);
    for (int32_t i = 3; i >= 0; --i)
    {
        uint8_t* slot = copy + i * 0x20;
        const uint32_t flags = Field<uint32_t>(slot, 0x1c);
        if (flags & 0x10000)
            continue;
        const int32_t e = Field<int32_t>(slot, 0);
        if (e == -1)
            continue;
        uint8_t* entry = Field<uint8_t*>(Field<uint8_t*>(actor, 0x414), 0xc) + e * 0xc;
        if (!entry || !Field<uint32_t>(entry, 8))
            continue;
        const float rate = fn_00409d90(actor, slot);
        uint8_t* anim = Field<uint8_t*>(entry, 8);
        if (Field<uint8_t>(slot, 0x1c) & 2)
        {
            if (rate < K(0x0056c05c))
            {
                const uint32_t to = AnimFrame(anim, K(0x0056c05c));
                const uint32_t from = AnimFrame(anim, Field<float>(slot, 8));
                AnimEvents(anim, from, to, actor);
                if (Field<uint8_t>(slot, 0x1c) & 1)
                {
                    anim = Field<uint8_t*>(entry, 8);
                    const uint32_t to2 = AnimFrame(anim, Field<float>(slot, 4));
                    const uint32_t from2 = AnimFrame(anim, Field<float>(entry, 0));
                    AnimEvents(anim, from2, to2, actor);
                }
            }
            else
            {
                const uint32_t to = AnimFrame(anim, Field<float>(entry, 0));
                const uint32_t from = AnimFrame(anim, Field<float>(slot, 8));
                AnimEvents(anim, from, to, actor);
                if (Field<uint8_t>(slot, 0x1c) & 1)
                {
                    anim = Field<uint8_t*>(entry, 8);
                    const uint32_t to2 = AnimFrame(anim, Field<float>(slot, 4));
                    const uint32_t from2 = AnimFrame(anim, K(0x0056c05c));
                    AnimEvents(anim, from2, to2, actor);
                }
            }
        }
        else
        {
            const uint32_t to = AnimFrame(anim, Field<float>(slot, 4));
            const uint32_t from = AnimFrame(anim, Field<float>(slot, 8));
            AnimEvents(anim, from, to, actor);
        }
    }
    return 0;
}

// 0x0040a3f0 (eax = actor, ecx unused; no stack): unless actor+0x421, for slots 3..0 (0x00408800) without flag
// 0x10000, with an entry and with flag 8 (time reset by a request), fires the frame events from the start of play
// to the slot time: 0..time when the rate (0x00409d90) >= 0, length..time when negative. guess.
uint32_t fn_0040a3f0(uint8_t* actor)
{
    if (Field<uint8_t>(actor, 0x421))
        return 0;
    for (uint32_t i = 4; i-- != 0;)
    {
        uint8_t* slot = B(orig::fn_00408800(i, u(actor)));
        if (Field<uint32_t>(slot, 0x1c) & 0x10000)
            continue;
        const int32_t e = Field<int32_t>(slot, 0);
        if (e == -1)
            continue;
        uint8_t* entry = Field<uint8_t*>(Field<uint8_t*>(actor, 0x414), 0xc) + e * 0xc;
        if (!entry || !Field<uint32_t>(entry, 8))
            continue;
        const float rate = fn_00409d90(actor, slot);
        if (!(Field<uint8_t>(slot, 0x1c) & 8))
            continue;
        uint8_t* anim = Field<uint8_t*>(entry, 8);
        const uint32_t to = AnimFrame(anim, Field<float>(slot, 4));
        const uint32_t from = AnimFrame(anim, rate < K(0x0056c05c) ? Field<float>(entry, 0) : K(0x0056c05c));
        AnimEvents(anim, from, to, actor);
    }
    return 0;
}

// 0x0040abc0 (ecx = object; no stack; plain ret): BridgeObject vtable 0x005aafe8 slot 47 (+0xbc), class from slot 1
// ("CLASS(BridgeObject)"). Not executed in recorded runs. symbols/abi.csv lists EBP as an input only because
// 0x00408c70 does (wrongly); it is not read. Animation: when +0x462 is set, advances the shared phase 0x0064a764 by
// 0x00604a90 * 0x005bcfb0 (wrapped by 0x005bcb74) and blends the four "grunt_bow_idle_bl/br/tl/tr" poses through
// 0x00408c70 (names 0x005bb94c/0x005bb960/0x005bb974/0x005bb988, flags 0x81 when +0x463 else 0x20001, weights
// (sin(phase * 0x005bcde8) + 1) / 2, (cos(phase * 0x005bcb7c) + 1) / 2 and 0.2); otherwise, when +0x463, plays
// "grunt_bow_stand" (0x005bb9b4, Anim_Set flags 0x81, weight 0.2). Then with a body (+0x1bc): when +0x460 and +0x461,
// 0x00459000(al = 1; 1, 1, 0), Body_addConstraints(this), 0x00459000(al = 1; 1, 0, 0) and 0x00458760(ebx = this);
// else 0x00459000(al = +0x460; 0, 0, 0). Clears +0x461 and +0x463.
uint32_t fn_0040abc0(uint8_t* self)
{
    if (!Field<uint8_t>(self, 0x462))
    {
        if (Field<uint8_t>(self, 0x463))
            Anim_Set(0xffffffffu, self, 0x005bb9b4u, 0x81, 0, 0x3e4ccccdu, 0, 0);
    }
    else
    {
        float g = GameGlobal<float>(0x00604a90) * K(0x005bcfb0) + GameGlobal<float>(0x0064a764);
        GameGlobal<float>(0x0064a764) = g;
        if (g > K(0x005bcb74))
        {
            do
                g = g - K(0x005bcb74);
            while (g > K(0x005bcb74));
            GameGlobal<float>(0x0064a764) = g;
        }
        const float a = (static_cast<float>(std::sin(static_cast<double>(K(0x005bcde8) * g))) + K(0x005bcb2c)) * K(0x005bcb4c);
        const float b = (static_cast<float>(std::cos(static_cast<double>(g * K(0x005bcb7c)))) + K(0x005bcb2c)) * K(0x005bcb4c);
        const uint32_t flags = Field<uint8_t>(self, 0x463) ? 0x81u : 0x20001u;
        fn_00408c70(flags, 0x005bb94cu, 0, self, 0x005bb960u, 0x005bb974u, 0x005bb988u, a, b, bf(0x3e4ccccdu));
    }
    if (Field<uint32_t>(self, 0x1bc))
    {
        const uint8_t a460 = Field<uint8_t>(self, 0x460);
        if (a460 && Field<uint8_t>(self, 0x461))
        {
            orig::fn_00459000(1, u(self), 1, 1, 0);
            orig::fn_00456570(u(self));
            orig::fn_00459000(1, u(self), 1, 0, 0);
            orig::fn_00458760(u(self));
            Field<uint8_t>(self, 0x461) = 0;
            Field<uint8_t>(self, 0x463) = 0;
            return 0;
        }
        orig::fn_00459000(a460, u(self), 0, 0, 0);  // only AL is read
    }
    Field<uint8_t>(self, 0x461) = 0;
    Field<uint8_t>(self, 0x463) = 0;
    return 0;
}

// 0x00410ec0 (stack: animation set; caller pops; returns eax): for every set entry from 1 on (+0x10 -> +0x24 count,
// +0xc entries of 0xc bytes, anim at +8) looks up each of the 7 track names of the table at 0x00589120 (0xc-byte
// rows: +0 key, +8 mask; first key "l_hand") in the animation's track list (+0x2c -> +4, via 0x004e0f40) and ORs the
// row's mask when the track exists with nonzero data. A nonzero result gets 0x400 when the set's +0 is >= 0.
// guess: which attachment bones the animations drive.
uint32_t fn_00410ec0(uint8_t* set)
{
    uint32_t mask = 0;
    if (Field<uint32_t>(Field<uint8_t*>(set, 0x10), 0x24) <= 1)
        return 0;
    uint32_t i = 1;
    uint32_t offset = 0xc;
    do
    {
        uint8_t* anim = Field<uint8_t*>(Field<uint8_t*>(set, 0xc) + offset, 8);
        if (anim)
        {
            for (uint32_t row = 0; row < 0x54; row += 0xc)
            {
                uint8_t* tl = Field<uint8_t*>(Field<uint8_t*>(anim, 0x2c), 4);
                const uint32_t count = Field<uint32_t>(tl, 0x1c);
                const uint32_t key = GameGlobal<uint32_t>(0x00589120 + row);
                if (!count || !Field<uint32_t>(tl, 0x20))
                    continue;
                uint8_t* it = Field<uint8_t*>(tl, 0x24);
                Field<uint32_t>(it, 0) = count;
                Field<uint32_t>(it, 0x18) = key;
                orig::fn_004e0f40(u(it));
                if (Field<uint32_t>(it, 0xc))
                    continue;
                const uint32_t found = Field<uint32_t*>(it, 0x10)[Field<uint32_t>(it, 8)];
                if (!found)
                    continue;
                if (Field<uint32_t>(P(found + Field<uint16_t>(P(found), 2)), 0) != 0)
                    mask |= GameGlobal<uint32_t>(0x00589128 + row);
            }
        }
        ++i;
        offset += 0xc;
    } while (i < Field<uint32_t>(Field<uint8_t*>(set, 0x10), 0x24));
    if (mask && Field<int32_t>(set, 0) >= 0)
        mask |= 0x400;
    return mask;
}

// 0x00426290 (ecx = object; stack: out; ret 4): virtual slot 11 (+0x2c) of PenguinObject, Q112Object, GruntLadderObject,
// Q112Boss, FakeGruntObject, SaucerObject, Polewart, ShrubPatrol, SheepObject, ColossusObject, SquadObject,
// BowGruntObject, SniperCaptain, GoliathFireball, GenericAIObject (vtables 0x005a1768...) and other slots of
// StigTurret/PickupObject/PlayerWeapon/Twiglet (scan of .rdata). Not executed in recorded runs. When +0x488 bit 31 is
// set, out+8 = the "b_grunt_burnt" texture (TextureBank_Find 0x004881c0, ecx = 0x005b6958, esi = Game+0xa78) if
// found. guess: burnt skin override. TextureBank_Find takes no stack arguments (symbols/abi.csv says 8; the original
// pushes none), the wrapper's extra pushes are popped by the caller and never read.
uint32_t fn_00426290(uint8_t* self, uint8_t* out)
{
    const uint32_t flags = Field<uint32_t>(self, 0x488);
    if (!(flags & 0x80000000))
        return flags;
    const uint32_t tex = orig::fn_004881c0(0x005b6958u, Field<uint32_t>(g_game, 0xa78), 0, 0, 0, 0, 0, 0, 0, 0);
    if (tex)
        Field<uint32_t>(out, 8) = tex;
    return tex;
}

// 0x004266b0 (edi = actor; no stack; plain ret): drops the mounted object at +0x898 (Stig's machine gun, guess):
// when actor+0x484 bit 0x8000 is set, object virtual +0x168; if it returns 1 the object blends the four
// "stig_mgun_idle_bl/br/tl/tr" poses (0x00408c70: flags 0x81, names 0x005b6908/0x005b691c/0x005b6930/0x005b6944,
// weights 0.5, 0.5, 0.25); then virtual +0x164; without the bit virtual +0x15c(0). Clears actor +0x484 bit 0x8000,
// +0x488 bit 0x800, +0x48c bit 1, releases the object (refcount +0x28; at zero, unless virtual +0xd8 returns true,
// 0x00487da0), clears +0x898, then 0x0042cff0(actor), 0x00429a40(al = 1, ecx = actor), 0x00454c90(cl = 0; 1),
// 0x00453b20(esi = actor), Object_setPhysicsEnabled(actor, 0).
uint32_t fn_004266b0(uint8_t* actor)
{
    uint8_t** held = reinterpret_cast<uint8_t**>(actor + 0x898);
    uint8_t* obj = *held;
    if (!obj)
        return 0;
    if (Field<uint32_t>(actor, 0x484) & 0x8000)
    {
        if (VCall<uint32_t>(obj, 0x168 / 4) == 1)
            fn_00408c70(0x81, 0x005b6908u, 0, *held, 0x005b691cu, 0x005b6930u, 0x005b6944u, 0.5f, 0.5f, 0.25f);
        VCall<uint32_t>(*held, 0x164 / 4);
    }
    else
    {
        VCall<uint32_t>(obj, 0x15c / 4, 0u);
    }
    Field<uint32_t>(actor, 0x484) &= 0xffff7fff;
    Field<uint32_t>(actor, 0x488) &= 0xfffff7ff;
    Field<uint32_t>(actor, 0x48c) &= 0xfffffffe;
    uint8_t* o = *held;
    if (--Field<int32_t>(o, 0x28) == 0 && !static_cast<uint8_t>(VCall<uint32_t>(o, 0xd8 / 4)))
        orig::fn_00487da0(u(o));
    *held = nullptr;
    orig::fn_0042cff0(u(actor));
    orig::fn_00429a40(1, u(actor));
    orig::fn_00454c90(u(actor), 0, 1);
    orig::fn_00453b20(u(actor));
    return static_cast<uint32_t>(orig::fn_004577b0(u(actor), 0));
}

// 0x00434930 (ecx = SniperCaptain, edx = passed on to 0x0042cc70; stack: object, a1; ret 8): SniperCaptain vtable
// 0x005ae868 slot 143 (+0x23c) (class from slot 1). Not executed in recorded runs. With an object and +0x488 bit
// 0x4000000 clear: unless virtual +0x13c, virtual +0x140; releases the previous +0x588 (refcount +0x28, virtual +0xd8,
// 0x00487da0), stores and references the object, sets the bit and plays "Captain_sniper_attach" (0x005b6334). Without
// an object and the bit set: releases +0x588, clears the bit, plays "Captain_sniper_detach" (0x005b634c). Both play
// through 0x00408e30(flags 0x80, index -1, weight 0.25) and end with virtual +0x1b8(0x104). Otherwise the base method
// 0x0042cc70(ecx, edx; object, a1) and, unless +0x460 is in [0x12c, 0x132], 0x0042d0f0(eax = this; 0, 0).
uint32_t fn_00434930(uint8_t* self, uint32_t edx, uint8_t* object, uint32_t a1)
{
    const uint32_t flags = Field<uint32_t>(self, 0x488);
    uint8_t** held = reinterpret_cast<uint8_t**>(self + 0x588);
    uint32_t name;
    if (!object)
    {
        if (!(flags & 0x4000000))
            goto base;
        uint8_t* o = *held;
        if (o)
        {
            if (--Field<int32_t>(o, 0x28) == 0 && !static_cast<uint8_t>(VCall<uint32_t>(o, 0xd8 / 4)))
                orig::fn_00487da0(u(o));
            *held = nullptr;
        }
        Field<uint32_t>(self, 0x488) = Field<uint32_t>(self, 0x488) & 0xfbffffff;
        name = 0x005b634c;
    }
    else
    {
        if (flags & 0x4000000)
            goto base;
        if (!static_cast<uint8_t>(VCall<uint32_t>(self, 0x13c / 4)))
            VCall<uint32_t>(self, 0x140 / 4);
        uint8_t* o = *held;
        if (o)
        {
            if (--Field<int32_t>(o, 0x28) == 0 && !static_cast<uint8_t>(VCall<uint32_t>(o, 0xd8 / 4)))
                orig::fn_00487da0(u(o));
            *held = nullptr;
        }
        *held = object;
        ++Field<int32_t>(object, 0x28);
        Field<uint32_t>(self, 0x488) = Field<uint32_t>(self, 0x488) | 0x4000000;
        name = 0x005b6334;
    }
    fn_00408e30(0x80, self, -1, name, 0, 0x3e800000u, 0, 0);
    return VCall<uint32_t>(self, 0x1b8 / 4, 0x104u);
base:
    {
        const uint32_t r = orig::fn_0042cc70(u(self), edx, u(object), a1);
        const uint32_t state = Field<uint32_t>(self, 0x460);
        if (state < 0x12c || state > 0x132)
            return orig::fn_0042d0f0(u(self), 0, 0, 0);  // ECX is not read (only CL, which it sets)
        return r;
    }
}

// ================================================================================================================
// Interaction-type animation names (virtuals; this is not read)
// ================================================================================================================

// 0x0043a9d0 (stack: 2 arguments, unused; ret 8): returns "genericAI_unknown_ic_type" (0x005b480c). Virtual slot 146
// (+0x248) of GenericAIObject, PenguinObject, Polewart, SniperCaptain and other AI classes (vtables 0x005b2958,
// 0x005a1768, ...). Not executed in recorded runs.
uint32_t fn_0043a9d0(uint32_t /*a0*/, uint32_t /*a1*/)
{
    return 0x005b480c;
}

// 0x0043a9e0 (stack: 1 argument, unused; ret 4): returns "genericAI_error_anim" (0x005b47f4). Virtual slot 105
// (+0x1a4) of GenericAIObject (0x005b2958) and GruntLadderObject, slot 203 of StigTurret. Not executed in recorded runs.
uint32_t fn_0043a9e0(uint32_t /*a0*/)
{
    return 0x005b47f4;
}

// 0x0043aa10 (stack: interaction type; ret 4): Polewart vtable 0x005a7328 slot 105 (+0x1a4). Types 8, 9, 14 and 89
// (jump table 0x0043aa3c / byte table 0x0043aa44) give "polewart_charge" (0x005b47d4), anything else "polewart_stand"
// (0x005b47c4). Not executed in recorded runs.
uint32_t fn_0043aa10(uint32_t type)
{
    if (type == 8 || type == 9 || type == 14 || type == 89)
        return 0x005b47d4;
    return 0x005b47c4;
}

// 0x0043aaa0 (stack: interaction type; ret 4): same shape for "goliath_walk" (0x005b47b4, types 8, 9, 14, 89, jump
// table 0x0043aacc / bytes 0x0043aad4) and "goliath_stand" (0x005b63b0). GoliathObject vtable 0x005af298 slot 105
// (+0x1a4). Not executed in recorded runs.
uint32_t fn_0043aaa0(uint32_t type)
{
    if (type == 8 || type == 9 || type == 14 || type == 89)
        return 0x005b47b4;
    return 0x005b63b0;
}

// 0x0043ab40 (stack: interaction type, out flags; ret 8): BowGruntObject vtable 0x005abb88 slot 146 (+0x248). Type 0:
// *out = 0xab, "mover_grunt_ladder_plant"; 1: *out = 0, "mover_grunt_ladder_climb"; 2: *out = 0 and one of
// "mover_grunt_scale", "_2", "_3", "_4" by (rand() << 2) >> 15 (_rand 0x005391cd); else "bow_grunt_ic_type_unkown"
// (sic). Not executed in recorded runs.
uint32_t fn_0043ab40(uint32_t type, uint32_t* out)
{
    if (type == 0)
    {
        *out = 0xab;
        return 0x005b4798;
    }
    if (type == 1)
    {
        *out = 0;
        return 0x005b477c;
    }
    if (type != 2)
        return 0x005b4710;
    *out = 0;
    const int32_t r = static_cast<int32_t>(static_cast<uint32_t>(orig::fn_005391cd()) << 2) >> 15;
    if (r == 0)
        return 0x005b4768;
    if (r == 1)
        return 0x005b4754;
    if (r == 2)
        return 0x005b4740;
    return 0x005b472c;
}

namespace
{
// _rand 0x005391cd (C runtime rand, the game's own copy).
inline uint32_t Rand()
{
    return static_cast<uint32_t>(orig::fn_005391cd());
}
}  // namespace

// 0x0043abc0 (ecx = object; stack: interaction type; ret 4): BowGruntObject vtable 0x005abb88 slot 105 (+0x1a4): the
// animation name for an interaction type (switch over 0..0x8b, byte table 0x0043b078, jump table 0x0043af30; anything
// else "grunt_bow_anim_error" 0x005b40d8). Type 10 picks "grunt_bow_run"/"grunt_bow_run_2" by rand; 18 is
// "grunt_idle_shiver" when Game+0x2708 == 1, else this virtual again with a random idle type 19..22; 80..85 pick
// "grunt_bow_die_1/2/3" by rand. Not executed in recorded runs.
uint32_t fn_0043abc0(uint8_t* self, uint32_t type)
{    switch (type)
    {
        case 0:
            return 0x005bb974;  // grunt_bow_idle_tl
        case 1:
            return 0x005bb988;  // grunt_bow_idle_tr
        case 2:
            return 0x005bb94c;  // grunt_bow_idle_bl
        case 3:
            return 0x005bb960;  // grunt_bow_idle_br
        case 4:
            return 0x005b46e0;  // grunt_bow_turn
        case 5:
            return 0x005b46d0;  // grunt_turn_180
        case 6:
            return 0x005b46bc;  // grunt_turn_left_90
        case 7:
            return 0x005b46a8;  // grunt_turn_right_90
        case 8:
            return 0x005b4698;  // grunt_bow_run
        case 9:
            return 0x005b4688;  // grunt_bow_run_2
        case 10:
            if (static_cast<int32_t>((Rand() * 100) & 0xffff8000) >= 0x190000)
                return 0x005b4688;  // grunt_bow_run_2
            return 0x005b4698;  // grunt_bow_run
        case 11: case 12:
            return 0x005b4668;  // grunt_run_scared
        case 13: case 27: case 28: case 29: case 30: case 31: case 32: case 37: case 38: case 39: case 44: case 45: case 46: case 48: case 53: case 54: case 55: case 56: case 90: case 92: case 94: case 99: case 100: case 101: case 102: case 103: case 104: case 107: case 108: case 109: case 110: case 111: case 112: case 119: case 120: case 121: case 122: case 123: case 124: case 126:
            return 0x005b40d8;  // grunt_bow_anim_error
        case 14:
            return 0x005b46f0;  // grunt_march
        case 15:
            return 0x005b46fc;  // grunt_bow_attention
        case 16:
            return 0x005b4650;  // grunt_grenade_stuck_1
        case 17:
            return 0x005b4638;  // grunt_grenade_stuck_2
        case 18:
            if (GameGlobal<uint32_t>(reinterpret_cast<uintptr_t>(g_game) + 0x2708) == 1)
                return 0x005b4600;  // grunt_idle_shiver;
            return VCall<uint32_t>(self, 0x1a4 / 4, static_cast<uint32_t>((static_cast<int32_t>(Rand() << 3) >> 15) + 0x13));
        case 19:
            return 0x005b45ec;  // grunt_idle_cough
        case 20:
            return 0x005b45d8;  // grunt_idle_sneeze
        case 21:
            return 0x005b45c4;  // grunt_idle_fly_buzz
        case 22:
            return 0x005b45ac;  // grunt_idle_blow_nose
        case 23: case 25:
            return 0x005b4590;  // grunt_bow_attention_scan
        case 24: case 26:
            return 0x005b4574;  // grunt_bow_attention_scan_2
        case 33:
            return 0x005b4560;  // grunt_bow_fire_tl
        case 34:
            return 0x005b454c;  // grunt_bow_fire_tr
        case 35:
            return 0x005b4538;  // grunt_bow_fire_bl
        case 36:
            return 0x005b4524;  // grunt_bow_fire_br
        case 40:
            return 0x005b44fc;  // grunt_bow_reload_tl
        case 41:
            return 0x005b44e8;  // grunt_bow_reload_tr
        case 42:
            return 0x005b44d4;  // grunt_bow_reload_bl
        case 43:
            return 0x005b44c0;  // grunt_bow_reload_br
        case 47:
            return 0x005b4510;  // grunt_bow_run_fire
        case 49:
            return 0x005b4624;  // grunt_ladder_run
        case 50:
            return 0x005b44ac;  // grunt_bow_taunt1
        case 51: case 52:
            return 0x005b4498;  // grunt_bow_taunt2
        case 57: case 59:
            return 0x005b422c;  // grunt_bow_hit_fall_1
        case 58: case 60:
            return 0x005b4214;  // grunt_bow_hit_fall_2
        case 61:
            return 0x005b4200;  // grunt_hit_window
        case 62:
            return 0x005b41e8;  // grunt_window_blown_out
        case 63:
            return 0x005b431c;  // grunt_bow_hit_1
        case 64:
            return 0x005b4308;  // grunt_hit_mgun_2
        case 65:
            return 0x005b42f4;  // grunt_hit_mgun_3
        case 66:
            return 0x005b42e8;  // grunt_hit_4
        case 67:
            return 0x005b42d4;  // grunt_bow_hit_small
        case 68:
            return 0x005b42bc;  // grunt_bow_hit_small_2
        case 69: case 70: case 71: case 72:
            return 0x005b42a8;  // grunt_bow_hit_med
        case 73:
            return 0x005b4298;  // grunt_blown_up
        case 74:
            return 0x005b4284;  // grunt_blown_up_2
        case 75: case 76: case 77: case 78: case 79:
            return 0x005b4274;  // grunt_hit_long
        case 80: case 81: case 82: case 83: case 84: case 85:
            {
                const uint32_t r = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 300) >> 15);
                if (r < 100)
                    return 0x005b4264;
                if (r < 200)
                    return 0x005b4254;
                return 0x005b4244;
            }
        case 86:
            return 0x005b41d0;  // grunt_barricade_death
        case 87:
            return 0x005b41ac;  // grunt_hit_in_back
        case 88:
            return 0x005b41c0;  // grunt_sharkbite
        case 89:
            return 0x005b467c;  // grunt_limp
        case 91:
            return 0x005b4160;  // grunt_crash_land
        case 93:
            return 0x005b4174;  // grunt_blown_up_die
        case 95:
            return 0x005b4480;  // grunt_bow_hand_to_hand
        case 96:
            return 0x005b4464;  // grunt_bow_hand_to_hand_2
        case 97: case 98:
            return 0x005b4614;  // grunt_bow_fall
        case 105:
            return 0x005b4450;  // grunt_fcannon_sit
        case 106:
            return 0x005b443c;  // grunt_fcannon_fire
        case 113:
            return 0x005b4150;  // grunt_hang_into
        case 114:
            return 0x005b4140;  // grunt_hanging
        case 115:
            return 0x005b412c;  // grunt_blown_up_into
        case 116:
            return 0x005b4118;  // grunt_idle_base_1
        case 117:
            return 0x005b4104;  // grunt_idle_base_2
        case 118:
            return 0x005b40f0;  // grunt_idle_base_3
        case 125:
            return 0x005b41a0;  // grunt_scare
        case 127:
            return 0x005b4428;  // mover_grunt_mdie_1
        case 128:
            return 0x005b4414;  // mover_grunt_mdie_2
        case 129:
            return 0x005b4400;  // mover_grunt_qdie_1
        case 130:
            return 0x005b43ec;  // mover_grunt_qdie_2
        case 131:
            return 0x005b43d4;  // grunt_stig_mgun_fire_tl
        case 132:
            return 0x005b43bc;  // grunt_stig_mgun_fire_tr
        case 133:
            return 0x005b43a4;  // grunt_stig_mgun_fire_bl
        case 134:
            return 0x005b438c;  // grunt_stig_mgun_fire_br
        case 135:
            return 0x005b4374;  // grunt_stig_mgun_idle_tl
        case 136:
            return 0x005b435c;  // grunt_stig_mgun_idle_tr
        case 137:
            return 0x005b4344;  // grunt_stig_mgun_idle_bl
        case 138:
            return 0x005b432c;  // grunt_stig_mgun_idle_br
        case 139:
            return 0x005b4188;  // mover_grunt_tower_climb
        default:
            return 0x005b40d8;
    }
}

// 0x0043b110 (ecx = object; stack: interaction type; ret 4): SniperCaptain vtable 0x005ae868 slot 105 (+0x1a4): the
// animation name for an interaction type (switch over 0..0x7c, bytes 0x0043b590, jumps 0x0043b45c; else
// "sniper_captain_error_anim" 0x005b3a14). Types 0..3 give "Captain_sniper_aim_tl/tr/bl/br" with a held object (+0x588)
// else "captain_sniper_stand"; 18 as in 0x0043abc0 ("captain_idle_shiver"); 80..85 "captain_death_1/2" by rand.
// Not executed in recorded runs.
uint32_t fn_0043b110(uint8_t* self, uint32_t type)
{    switch (type)
    {
        case 0:
            return Field<uint32_t>(self, 0x588) ? 0x005b40c0 : 0x005b40a8;
        case 1:
            return Field<uint32_t>(self, 0x588) ? 0x005b4090 : 0x005b40a8;
        case 2:
            return Field<uint32_t>(self, 0x588) ? 0x005b4078 : 0x005b40a8;
        case 3:
            return Field<uint32_t>(self, 0x588) ? 0x005b4060 : 0x005b40a8;
        case 4:
            return 0x005b4050;  // captain_turn
        case 5: case 6: case 7: case 13: case 27: case 28: case 29: case 30: case 31: case 32: case 37: case 38: case 44: case 45: case 46: case 47: case 48: case 49: case 90: case 95: case 96: case 99: case 100: case 105: case 106: case 107: case 109: case 110: case 111: case 112:
            return 0x005b3a14;  // sniper_captain_error_anim
        case 8: case 9:
            return 0x005b401c;  // captain_run
        case 10:
            return 0x005b3f90;  // captain_run_scan
        case 11: case 12:
            return 0x005b3ff8;  // captain_run_scared
        case 14:
            return 0x005b4028;  // captain_march
        case 15:
            return 0x005b4038;  // Captain_sniper_stand
        case 16:
            return 0x005b3fe0;  // captain_grenade_stuck_1
        case 17:
            return 0x005b3fc8;  // captain_grenade_stuck_2
        case 18:
            if (GameGlobal<uint32_t>(reinterpret_cast<uintptr_t>(g_game) + 0x2708) == 1)
                return 0x005b3f7c;  // captain_idle_shiver;
            return VCall<uint32_t>(self, 0x1a4 / 4, static_cast<uint32_t>((static_cast<int32_t>(Rand() << 3) >> 15) + 0x13));
        case 19:
            return 0x005b3f68;  // captain_idle_cough
        case 20:
            return 0x005b3f54;  // captain_idle_sneeze
        case 21:
            return 0x005b3f38;  // captain_idle_ball_adjust
        case 22:
            return 0x005b3f20;  // captain_idle_check_time
        case 23:
            return 0x005b3f08;  // captain_sniper_scan_1
        case 24:
            return 0x005b3ef0;  // captain_sniper_scan_2
        case 25:
            return 0x005b3ed8;  // captain_stand_scan_3
        case 26:
            return 0x005b3ec0;  // captain_stand_scan_4
        case 33:
            return 0x005b3e90;  // Captain_sniper_fire_tl
        case 34:
            return 0x005b3e78;  // Captain_sniper_fire_tr
        case 35:
            return 0x005b3e60;  // Captain_sniper_fire_bl
        case 36:
            return 0x005b3e48;  // Captain_sniper_fire_br
        case 39:
            return 0x005b3ea8;  // captain_grenade_throw
        case 40:
            return 0x005b3e2c;  // captain_sniper_reload_tl
        case 41:
            return 0x005b3e10;  // captain_sniper_reload_tr
        case 42:
            return 0x005b3df4;  // captain_sniper_reload_bl
        case 43:
            return 0x005b3dd8;  // captain_sniper_reload_br
        case 50:
            return 0x005b3d5c;  // captain_taunt1
        case 51:
            return 0x005b3d4c;  // captain_taunt2
        case 52:
            return 0x005b3d3c;  // captain_taunt3
        case 53:
            return 0x005b3d24;  // captain_allfours_hit
        case 54:
            return 0x005b3d08;  // captain_allfours_breathe
        case 55: case 93:
            return 0x005b3cf0;  // captain_allfours_death
        case 56:
            return 0x005b3cd4;  // captain_allfours_stand_up
        case 57:
            return 0x005b3bbc;  // captain_hit_fall_1
        case 58:
            return 0x005b3ba8;  // captain_hit_fall_2
        case 59:
            return 0x005b3b94;  // captain_hit_fall_3
        case 60:
            return 0x005b3b80;  // captain_hit_fall_4
        case 61:
            return 0x005b3b6c;  // captain_hit_window
        case 62:
            return 0x005b3b50;  // captain_window_blown_out
        case 63:
            return 0x005b3cc4;  // captain_hit_1
        case 64:
            return 0x005b3cb4;  // captain_hit_2
        case 65:
            return 0x005b3ca0;  // captain_hit_mgun_3
        case 66:
            return 0x005b3c8c;  // captain_hit_mgun_4
        case 67: case 123:
            return 0x005b3c78;  // captain_hit_small
        case 68: case 124:
            return 0x005b3c64;  // captain_hit_small_2
        case 69: case 71: case 121:
            return 0x005b3c54;  // captain_hit_med
        case 70: case 72: case 122:
            return 0x005b3c40;  // captain_hit_med_2
        case 73:
            return 0x005b3c2c;  // captain_blown_up
        case 74:
            return 0x005b3c18;  // captain_blown_up_2
        case 75: case 76: case 119:
            return 0x005b3c04;  // captain_hit_long
        case 77: case 78: case 79: case 120:
            return 0x005b3bf0;  // captain_hit_long_2
        case 80: case 81: case 82: case 83: case 84: case 85:
            {
                const uint32_t r = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 200) >> 15);
                if (r < 100)
                    return 0x005b3be0;
                return 0x005b3bd0;
            }
        case 86:
            return 0x005b3b38;  // captain_barricade_death
        case 87:
            return 0x005b3b10;  // captain_hit_in_back
        case 88:
            return 0x005b3b24;  // captain_sharkbite
        case 89:
            return 0x005b400c;  // captain_limp
        case 91:
            return 0x005b3ae0;  // captain_jumper_hit_ground
        case 92:
            return 0x005b3afc;  // captain_jumper_fall
        case 94:
            return 0x005b3abc;  // captain_jumper_hit_ground_get_up
        case 97:
            return 0x005b3fb8;  // captain_fall
        case 98:
            return 0x005b3fa4;  // captain_parachute
        case 101:
            return 0x005b3da4;  // captain_barricade_hide
        case 102:
            return 0x005b3dbc;  // captain_barricade_reload
        case 103:
            return 0x005b3d8c;  // captain_barricade_peek
        case 104:
            return 0x005b3d6c;  // captain_barricade_fire_spray
        case 108:
            return 0x005b3aa8;  // captain_jumper_land
        case 113:
            return 0x005b3a94;  // captain_hang_into
        case 114:
            return 0x005b3a84;  // captain_hanging
        case 115:
            return 0x005b3a6c;  // captain_blown_up_into
        case 116:
            return 0x005b3a58;  // captain_idle_base_1
        case 117:
            return 0x005b3a44;  // captain_idle_base_2
        case 118:
            return 0x005b3a30;  // captain_idle_base_3
        default:
            return 0x005b3a14;
    }
}

// 0x0043b610 (ecx = object; stack: interaction type; ret 4): CaptainObject vtable 0x005acb78 slot 105 (+0x1a4) (class
// from slot 1): the animation name for an interaction type (switch over 0..0x7c, bytes 0x0043bbcc, jumps 0x0043ba84;
// else "captain_error_anim" 0x005b3744). Aim types 0..3 choose rocket (+0x484 bit 0x8000000) / jumper glide
// (+0x460 == 0xbd) / idle variants, fire types 33..36 jumper glide / rocket / plain; 18 as in 0x0043abc0; 80..85
// "captain_death_1/2" by rand. Not executed in recorded runs.
uint32_t fn_0043b610(uint8_t* self, uint32_t type)
{    switch (type)
    {
        case 0:
            if (Field<uint32_t>(self, 0x484) & 0x8000000)
                return 0x005b3a00;
            if (Field<uint32_t>(self, 0x460) == 0xbd)
                return 0x005b39e8;
            return 0x005b39d8;
        case 1:
            if (Field<uint32_t>(self, 0x484) & 0x8000000)
                return 0x005b39c4;
            if (Field<uint32_t>(self, 0x460) == 0xbd)
                return 0x005b39ac;
            return 0x005b399c;
        case 2:
            if (Field<uint32_t>(self, 0x484) & 0x8000000)
                return 0x005b3988;
            if (Field<uint32_t>(self, 0x460) == 0xbd)
                return 0x005b3970;
            return 0x005b3960;
        case 3:
            if (Field<uint32_t>(self, 0x484) & 0x8000000)
                return 0x005b394c;
            if (Field<uint32_t>(self, 0x460) == 0xbd)
                return 0x005b3934;
            return 0x005b3924;
        case 4:
            return 0x005b4050;  // captain_turn
        case 5: case 6: case 7: case 13: case 27: case 28: case 29: case 30: case 31: case 32: case 37: case 38: case 44: case 45: case 46: case 47: case 48: case 49: case 90: case 95: case 96: case 99: case 100: case 105: case 106:
            return 0x005b3744;  // captain_error_anim
        case 8: case 9:
            return 0x005b401c;  // captain_run
        case 10:
            return 0x005b3f90;  // captain_run_scan
        case 11: case 12:
            return 0x005b3ff8;  // captain_run_scared
        case 14:
            return 0x005b4028;  // captain_march
        case 15:
            return 0x005b3910;  // captain_attention
        case 16:
            return 0x005b3fe0;  // captain_grenade_stuck_1
        case 17:
            return 0x005b3fc8;  // captain_grenade_stuck_2
        case 18:
            if (GameGlobal<uint32_t>(reinterpret_cast<uintptr_t>(g_game) + 0x2708) == 1)
                return 0x005b3f7c;  // captain_idle_shiver;
            return VCall<uint32_t>(self, 0x1a4 / 4, static_cast<uint32_t>((static_cast<int32_t>(Rand() << 3) >> 15) + 0x13));
        case 19:
            return 0x005b3f68;  // captain_idle_cough
        case 20:
            return 0x005b3f54;  // captain_idle_sneeze
        case 21:
            return 0x005b3f38;  // captain_idle_ball_adjust
        case 22:
            return 0x005b3f20;  // captain_idle_check_time
        case 23:
            return 0x005b38f8;  // captain_stand_scan_1
        case 24:
            return 0x005b38e0;  // captain_stand_scan_2
        case 25:
            return 0x005b3ed8;  // captain_stand_scan_3
        case 26:
            return 0x005b3ec0;  // captain_stand_scan_4
        case 33:
            if (Field<uint32_t>(self, 0x460) == 0xbd)
                return 0x005b38c0;
            if (Field<uint32_t>(self, 0x484) & 0x8000000)
                return 0x005b38a8;
            return 0x005b3898;
        case 34:
            if (Field<uint32_t>(self, 0x460) == 0xbd)
                return 0x005b3878;
            if (Field<uint32_t>(self, 0x484) & 0x8000000)
                return 0x005b3860;
            return 0x005b3850;
        case 35:
            if (Field<uint32_t>(self, 0x460) == 0xbd)
                return 0x005b3830;
            if (Field<uint32_t>(self, 0x484) & 0x8000000)
                return 0x005b3818;
            return 0x005b3808;
        case 36:
            if (Field<uint32_t>(self, 0x460) == 0xbd)
                return 0x005b37e8;
            if (Field<uint32_t>(self, 0x484) & 0x8000000)
                return 0x005b37d0;
            return 0x005b37c0;
        case 39:
            return 0x005b3ea8;  // captain_grenade_throw
        case 40:
            return 0x005b37ac;  // captain_reload_tl
        case 41:
            return 0x005b3798;  // captain_reload_tr
        case 42:
            return 0x005b3784;  // captain_reload_bl
        case 43:
            return 0x005b3770;  // captain_reload_br
        case 50:
            return 0x005b3d5c;  // captain_taunt1
        case 51:
            return 0x005b3d4c;  // captain_taunt2
        case 52:
            return 0x005b3d3c;  // captain_taunt3
        case 53:
            return 0x005b3d24;  // captain_allfours_hit
        case 54:
            return 0x005b3d08;  // captain_allfours_breathe
        case 55: case 93:
            return 0x005b3cf0;  // captain_allfours_death
        case 56:
            return 0x005b3cd4;  // captain_allfours_stand_up
        case 57:
            return 0x005b3bbc;  // captain_hit_fall_1
        case 58:
            return 0x005b3ba8;  // captain_hit_fall_2
        case 59:
            return 0x005b3b94;  // captain_hit_fall_3
        case 60:
            return 0x005b3b80;  // captain_hit_fall_4
        case 61:
            return 0x005b3b6c;  // captain_hit_window
        case 62:
            return 0x005b3b50;  // captain_window_blown_out
        case 63:
            return 0x005b3cc4;  // captain_hit_1
        case 64:
            return 0x005b3cb4;  // captain_hit_2
        case 65:
            return 0x005b3ca0;  // captain_hit_mgun_3
        case 66:
            return 0x005b3c8c;  // captain_hit_mgun_4
        case 67: case 123:
            return 0x005b3c78;  // captain_hit_small
        case 68: case 124:
            return 0x005b3c64;  // captain_hit_small_2
        case 69: case 71: case 121:
            return 0x005b3c54;  // captain_hit_med
        case 70: case 72: case 122:
            return 0x005b3c40;  // captain_hit_med_2
        case 73:
            return 0x005b3c2c;  // captain_blown_up
        case 74:
            return 0x005b3c18;  // captain_blown_up_2
        case 75: case 76: case 119:
            return 0x005b3c04;  // captain_hit_long
        case 77: case 78: case 79: case 120:
            return 0x005b3bf0;  // captain_hit_long_2
        case 80: case 81: case 82: case 83: case 84: case 85:
            {
                const uint32_t r = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 200) >> 15);
                if (r < 100)
                    return 0x005b3be0;
                return 0x005b3bd0;
            }
        case 86:
            return 0x005b3b38;  // captain_barricade_death
        case 87:
            return 0x005b3b10;  // captain_hit_in_back
        case 88:
            return 0x005b3b24;  // captain_sharkbite
        case 89:
            return 0x005b400c;  // captain_limp
        case 91:
            return 0x005b3ae0;  // captain_jumper_hit_ground
        case 92:
            return 0x005b3afc;  // captain_jumper_fall
        case 94:
            return 0x005b3abc;  // captain_jumper_hit_ground_get_up
        case 97:
            return 0x005b3fb8;  // captain_fall
        case 98:
            return 0x005b3fa4;  // captain_parachute
        case 101:
            return 0x005b3da4;  // captain_barricade_hide
        case 102:
            return 0x005b3dbc;  // captain_barricade_reload
        case 103:
            return 0x005b3d8c;  // captain_barricade_peek
        case 104:
            return 0x005b3d6c;  // captain_barricade_fire_spray
        case 107:
            return 0x005b3758;  // captain_jumper_forward
        case 108:
            return 0x005b3aa8;  // captain_jumper_land
        case 109:
            return 0x005b39e8;  // captain_jumper_glide_tl
        case 110:
            return 0x005b39ac;  // captain_jumper_glide_tr
        case 111:
            return 0x005b3970;  // captain_jumper_glide_bl
        case 112:
            return 0x005b3934;  // captain_jumper_glide_br
        case 113:
            return 0x005b3a94;  // captain_hang_into
        case 114:
            return 0x005b3a84;  // captain_hanging
        case 115:
            return 0x005b3a6c;  // captain_blown_up_into
        case 116:
            return 0x005b3a58;  // captain_idle_base_1
        case 117:
            return 0x005b3a44;  // captain_idle_base_2
        case 118:
            return 0x005b3a30;  // captain_idle_base_3
        default:
            return 0x005b3744;
    }
}

// 0x0043bd70 (ecx = object; stack: interaction type; ret 4): GruntObject vtable 0x005a9cf0 slot 105 (+0x1a4) (class
// from slot 1): the animation name for an interaction type (switch over 0..0x8b, bytes 0x0043c2a4, jumps 0x0043c128;
// else "grunt_anim_error" 0x005b3470). Types 8/9: "grunt_run_no_sound" when 0x0044e600 (Game+0x748 and Game+0x26cc set)
// else "grunt_run"; 18 as in 0x0043abc0 ("grunt_idle_shiver"). Not executed in recorded runs.
uint32_t fn_0043bd70(uint8_t* self, uint32_t type)
{    switch (type)
    {
        case 0:
            return 0x005b371c;  // grunt_stand_tl
        case 1:
            return 0x005b370c;  // grunt_stand_tr
        case 2:
            return 0x005b36fc;  // grunt_stand_bl
        case 3:
            return 0x005b36ec;  // grunt_stand_br
        case 4:
            return 0x005b36d0;  // grunt_turn
        case 5:
            return 0x005b46d0;  // grunt_turn_180
        case 6:
            return 0x005b46bc;  // grunt_turn_left_90
        case 7:
            return 0x005b46a8;  // grunt_turn_right_90
        case 8: case 9:
            return static_cast<uint8_t>(orig::fn_0044e600()) ? 0x005b36bc : 0x005b36b0;  // grunt_run_no_sound / grunt_run
        case 10:
            return 0x005b36a0;  // grunt_run_scan
        case 11: case 12:
            return 0x005b4668;  // grunt_run_scared
        case 13: case 27: case 28: case 29: case 30: case 31: case 32: case 37: case 38: case 39: case 47: case 48: case 53: case 54: case 55: case 56: case 90: case 92: case 94: case 99: case 100: case 104: case 107: case 108: case 109: case 110: case 111: case 112: case 126:
            return 0x005b3470;  // grunt_anim_error
        case 14:
            return 0x005b46f0;  // grunt_march
        case 15:
            return 0x005b36dc;  // grunt_attention
        case 16:
            return 0x005b4650;  // grunt_grenade_stuck_1
        case 17:
            return 0x005b4638;  // grunt_grenade_stuck_2
        case 18:
            if (GameGlobal<uint32_t>(reinterpret_cast<uintptr_t>(g_game) + 0x2708) == 1)
                return 0x005b4600;  // grunt_idle_shiver;
            return VCall<uint32_t>(self, 0x1a4 / 4, static_cast<uint32_t>((static_cast<int32_t>(Rand() << 3) >> 15) + 0x13));
        case 19:
            return 0x005b45ec;  // grunt_idle_cough
        case 20:
            return 0x005b45d8;  // grunt_idle_sneeze
        case 21:
            return 0x005b45c4;  // grunt_idle_fly_buzz
        case 22:
            return 0x005b45ac;  // grunt_idle_blow_nose
        case 23: case 25:
            return 0x005b368c;  // grunt_stand_scan_1
        case 24: case 26:
            return 0x005b3678;  // grunt_stand_scan_2
        case 33:
            return 0x005b3668;  // grunt_fire_tl
        case 34:
            return 0x005b3658;  // grunt_fire_tr
        case 35:
            return 0x005b3648;  // grunt_fire_bl
        case 36:
            return 0x005b3638;  // grunt_fire_br
        case 40:
            return 0x005b3628;  // grunt_reload_tl
        case 41:
            return 0x005b3618;  // grunt_reload_tr
        case 42:
            return 0x005b3608;  // grunt_reload_bl
        case 43:
            return 0x005b35f8;  // grunt_reload_br
        case 44:
            return 0x005b35e4;  // grunt_reload_hide
        case 45:
            return 0x005b35d4;  // grunt_peek_1
        case 46:
            return 0x005b35c4;  // grunt_peek_2
        case 49:
            return 0x005b4624;  // grunt_ladder_run
        case 50:
            return 0x005b356c;  // grunt_taunt1
        case 51: case 52:
            return 0x005b355c;  // grunt_taunt2
        case 57: case 59:
            return 0x005b3498;  // grunt_hit_fall_1
        case 58: case 60:
            return 0x005b3484;  // grunt_hit_fall_2
        case 61:
            return 0x005b4200;  // grunt_hit_window
        case 62:
            return 0x005b41e8;  // grunt_window_blown_out
        case 63:
            return 0x005b431c;  // grunt_bow_hit_1
        case 64:
            return 0x005b4308;  // grunt_hit_mgun_2
        case 65:
            return 0x005b42f4;  // grunt_hit_mgun_3
        case 66:
            return 0x005b42e8;  // grunt_hit_4
        case 67: case 119:
            return 0x005b3550;  // grunt_hit_1
        case 68: case 120:
            return 0x005b3544;  // grunt_hit_2
        case 69: case 70: case 71: case 72: case 121: case 122:
            return 0x005b3538;  // grunt_hit_3
        case 73:
            return 0x005b4298;  // grunt_blown_up
        case 74:
            return 0x005b4284;  // grunt_blown_up_2
        case 75: case 123:
            return 0x005b4274;  // grunt_hit_long
        case 76: case 124:
            return 0x005b3524;  // grunt_hit_long_2
        case 77:
            return 0x005b3514;  // grunt_dazed_1
        case 78:
            return 0x005b3504;  // grunt_dazed_2
        case 79:
            return 0x005b34f4;  // grunt_dazed_3
        case 80:
            return 0x005b34e8;  // grunt_die_1
        case 81:
            return 0x005b34dc;  // grunt_die_2
        case 82:
            return 0x005b34d0;  // grunt_die_3
        case 83:
            return 0x005b34c4;  // grunt_die_4
        case 84:
            return 0x005b34b8;  // grunt_die_5
        case 85:
            return 0x005b34ac;  // grunt_die_6
        case 86:
            return 0x005b41d0;  // grunt_barricade_death
        case 87:
            return 0x005b41ac;  // grunt_hit_in_back
        case 88:
            return 0x005b41c0;  // grunt_sharkbite
        case 89:
            return 0x005b467c;  // grunt_limp
        case 91:
            return 0x005b4160;  // grunt_crash_land
        case 93:
            return 0x005b4174;  // grunt_blown_up_die
        case 95:
            return 0x005b4480;  // grunt_bow_hand_to_hand
        case 96:
            return 0x005b4464;  // grunt_bow_hand_to_hand_2
        case 97: case 98:
            return 0x005b4614;  // grunt_bow_fall
        case 101:
            return 0x005b3594;  // grunt_barricade_breathe
        case 102:
            return 0x005b35ac;  // grunt_barricade_reload
        case 103:
            return 0x005b357c;  // grunt_barricade_peek
        case 105:
            return 0x005b4450;  // grunt_fcannon_sit
        case 106:
            return 0x005b443c;  // grunt_fcannon_fire
        case 113:
            return 0x005b4150;  // grunt_hang_into
        case 114:
            return 0x005b4140;  // grunt_hanging
        case 115:
            return 0x005b412c;  // grunt_blown_up_into
        case 116:
            return 0x005b4118;  // grunt_idle_base_1
        case 117:
            return 0x005b4104;  // grunt_idle_base_2
        case 118:
            return 0x005b40f0;  // grunt_idle_base_3
        case 125:
            return 0x005b41a0;  // grunt_scare
        case 127:
            return 0x005b4428;  // mover_grunt_mdie_1
        case 128:
            return 0x005b4414;  // mover_grunt_mdie_2
        case 129:
            return 0x005b4400;  // mover_grunt_qdie_1
        case 130:
            return 0x005b43ec;  // mover_grunt_qdie_2
        case 131:
            return 0x005b43d4;  // grunt_stig_mgun_fire_tl
        case 132:
            return 0x005b43bc;  // grunt_stig_mgun_fire_tr
        case 133:
            return 0x005b43a4;  // grunt_stig_mgun_fire_bl
        case 134:
            return 0x005b438c;  // grunt_stig_mgun_fire_br
        case 135:
            return 0x005b4374;  // grunt_stig_mgun_idle_tl
        case 136:
            return 0x005b435c;  // grunt_stig_mgun_idle_tr
        case 137:
            return 0x005b4344;  // grunt_stig_mgun_idle_bl
        case 138:
            return 0x005b432c;  // grunt_stig_mgun_idle_br
        case 139:
            return 0x005b4188;  // mover_grunt_tower_climb
        default:
            return 0x005b3470;
    }
}


// 0x0043bcf0 (stack: interaction type, out flags; ret 8): GruntObject vtable 0x005a9cf0 slot 146 (+0x248) (class from
// slot 1). The same code as 0x0043ab40 with "grunt_ic_type_unkown" (sic, 0x005b372c) for unknown types. Not executed
// in recorded runs.
uint32_t fn_0043bcf0(uint32_t type, uint32_t* out)
{
    if (type == 0)
    {
        *out = 0xab;
        return 0x005b4798;
    }
    if (type == 1)
    {
        *out = 0;
        return 0x005b477c;
    }
    if (type != 2)
        return 0x005b372c;
    *out = 0;
    const int32_t r = static_cast<int32_t>(Rand() << 2) >> 15;
    if (r == 0)
        return 0x005b4768;
    if (r == 1)
        return 0x005b4754;
    if (r == 2)
        return 0x005b4740;
    return 0x005b472c;
}

// 0x0043c330 (stack: interaction type; ret 4): Q112Object vtable 0x005a2440 slot 105 (+0x1a4): animation name for an
// interaction type (switch over 0..0x63, bytes 0x0043c4fc, jumps 0x0043c46c; else "q112_anim_error" 0x005b3238).
// Not executed in recorded runs.
uint32_t fn_0043c330(uint32_t type)
{    switch (type)
    {
        case 0:
            return 0x005b3464;  // q112_aim_tl
        case 1:
            return 0x005b3458;  // q112_aim_tr
        case 2:
            return 0x005b344c;  // q112_aim_bl
        case 3:
            return 0x005b3440;  // q112_aim_br
        case 4:
            return 0x005b341c;  // q112_turn
        case 5: case 6: case 7: case 11: case 12: case 13: case 16: case 17: case 27: case 28: case 29: case 30: case 31: case 32: case 37: case 38: case 39: case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 57: case 58: case 59: case 60: case 61: case 62: case 63: case 64: case 65: case 66: case 86: case 87: case 88: case 90: case 91: case 92: case 93: case 94:
            return 0x005b3238;  // q112_anim_error
        case 8: case 9:
            return 0x005b3410;  // q112_run
        case 10:
            return 0x005b33d0;  // q112_walk_scan
        case 14:
            return 0x005b33f8;  // q112_march
        case 15:
            return 0x005b3428;  // q112_stand_attention
        case 18: case 21: case 22:
            return 0x005b3394;  // q112_stand_scan_3
        case 19: case 23: case 25:
            return 0x005b33bc;  // q112_stand_scan_1
        case 20: case 24: case 26:
            return 0x005b33a8;  // q112_stand_scan_2
        case 33:
            return 0x005b3384;  // q112_fire_tl
        case 34:
            return 0x005b3374;  // q112_fire_tr
        case 35:
            return 0x005b3364;  // q112_fire_bl
        case 36:
            return 0x005b3354;  // q112_fire_br
        case 40:
            return 0x005b3344;  // q112_reload_tl
        case 41:
            return 0x005b3334;  // q112_reload_tr
        case 42:
            return 0x005b3324;  // q112_reload_bl
        case 43:
            return 0x005b3314;  // q112_reload_br
        case 53:
            return 0x005b3298;  // q112_hit_to_knees
        case 54: case 75: case 76: case 77: case 78: case 79:
            return 0x005b32ac;  // q112_hit_knees_breathe
        case 55:
            return 0x005b3284;  // q112_hit_knees_die
        case 56:
            return 0x005b326c;  // q112_hit_knees_standup
        case 67: case 68:
            return 0x005b32f4;  // q112_hit_med
        case 69: case 70:
            return 0x005b32e4;  // q112_hit_big
        case 71:
            return 0x005b32d4;  // q112_hit_head
        case 72:
            return 0x005b32c4;  // q112_hit_head_2
        case 73: case 74: case 97: case 98:
            return 0x005b33ec;  // q112_fall
        case 80: case 83:
            return 0x005b3260;  // q112_die
        case 81: case 84:
            return 0x005b3254;  // q112_die_2
        case 82: case 85:
            return 0x005b3248;  // q112_die_3
        case 89:
            return 0x005b3404;  // q112_walk
        case 95: case 96:
            return 0x005b3304;  // q112_gun_hit
        case 99:
            return 0x005b33e0;  // q112_land
        default:
            return 0x005b3238;
    }
}

// 0x0043c600 (stack: 1 argument, unused; ret 4): PeasantGenAI vtable 0x005aa9c0 slot 105 (+0x1a4) (class from slot 1):
// "Peasant_blown_up_chair_2" (0x005b3204) when rand() * 100 & 0xffff8000 >= 0x190000, else "Peasant_blown_up_chair"
// (0x005b3220). Not executed in recorded runs.
uint32_t fn_0043c600(uint32_t /*type*/)
{
    if (static_cast<int32_t>((Rand() * 100) & 0xffff8000) < 0x190000)
        return 0x005b3220;
    return 0x005b3204;
}

// 0x0043c630 (stack: 1 argument, unused; ret 4): SaucerObject vtable 0x005a5e98 slot 105 (+0x1a4): returns
// "saucer_hover_low" (0x005b31f0). Not executed in recorded runs.
uint32_t fn_0043c630(uint32_t /*type*/)
{
    return 0x005b31f0;
}

// 0x0043c6e0 (stack: interaction type; ret 4): ColossusObject vtable 0x005aa238 slot 105 (+0x1a4): animation name
// (switch over 0..0x5b, bytes 0x0043c750, jumps 0x0043c730; else "colossus_stand" 0x005b31cc). Not executed in
// recorded runs.
uint32_t fn_0043c6e0(uint32_t type)
{    switch (type)
    {
        case 0: case 1: case 2: case 3: case 5: case 6: case 7: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39: case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62: case 63: case 64: case 65: case 66: case 73: case 74: case 75: case 76: case 77: case 78: case 79: case 88: case 89: case 90:
            return 0x005b31cc;  // colossus_stand
        case 4: case 8: case 9: case 10:
            return 0x005b31bc;  // colossus_walk
        case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26:
            return 0x005b31ac;  // colussus_scan
        case 47:
            return 0x005b3170;  // colussus_run_fire
        case 67: case 68:
            return 0x005b3198;  // colossus_hit_med
        case 69: case 70: case 71: case 72:
            return 0x005b3184;  // colossus_hit_large
        case 80: case 81: case 82: case 83: case 84: case 85: case 86: case 87: case 91:
            return 0x005b315c;  // colossus_blow_up
        default:
            return 0x005b31cc;
    }
}

// 0x0043c8a0 (stack: interaction type; ret 4): Q112Boss vtable 0x005a4098 slot 105 (+0x1a4): animation name (switch
// over 0..0x62, bytes 0x0043c994, jumps 0x0043c94c; else "q112_stand" 0x005b3148). Not executed in recorded runs.
uint32_t fn_0043c8a0(uint32_t type)
{    switch (type)
    {
        case 0:
            return 0x005b3464;  // q112_aim_tl
        case 1:
            return 0x005b3458;  // q112_aim_tr
        case 2:
            return 0x005b344c;  // q112_aim_bl
        case 3:
            return 0x005b3440;  // q112_aim_br
        case 4:
            return 0x005b341c;  // q112_turn
        case 5: case 6: case 7: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 27: case 28: case 29: case 30: case 31: case 32: case 37: case 38: case 39: case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62: case 63: case 64: case 65: case 66: case 67: case 68: case 69: case 70: case 71: case 72: case 73: case 74: case 75: case 76: case 77: case 78: case 79: case 86: case 87: case 88: case 89: case 90: case 91: case 92: case 93: case 94: case 95: case 96:
            return 0x005b3148;  // q112_stand
        case 8: case 9: case 10:
            return 0x005b3404;  // q112_walk
        case 18: case 21: case 22:
            return 0x005b3394;  // q112_stand_scan_3
        case 19: case 23: case 25:
            return 0x005b33bc;  // q112_stand_scan_1
        case 20: case 24: case 26:
            return 0x005b33a8;  // q112_stand_scan_2
        case 33:
            return 0x005b3384;  // q112_fire_tl
        case 34:
            return 0x005b3374;  // q112_fire_tr
        case 35:
            return 0x005b3364;  // q112_fire_bl
        case 36:
            return 0x005b3354;  // q112_fire_br
        case 80: case 83:
            return 0x005b3260;  // q112_die
        case 81: case 84:
            return 0x005b3254;  // q112_die_2
        case 82: case 85:
            return 0x005b3248;  // q112_die_3
        case 97: case 98:
            return 0x005b33ec;  // q112_fall
        default:
            return 0x005b3148;
    }
}

// 0x0043ca00 (stack: interaction type; ret 4): ShrubPatrol vtable 0x005a7e98 slot 105 (+0x1a4): animation name
// (switch over 0..0x62, bytes 0x0043cadc, jumps 0x0043ca9c; else "shrubpatrol_animation_error" 0x005b2ff0). Not
// executed in recorded runs.
uint32_t fn_0043ca00(uint32_t type)
{    switch (type)
    {
        case 0:
            return 0x005b3120;  // shrubpatrol_aim_tl
        case 1:
            return 0x005b310c;  // shrubpatrol_aim_tr
        case 2:
            return 0x005b30f8;  // shrubpatrol_aim_bl
        case 3:
            return 0x005b30e4;  // shrubpatrol_aim_br
        case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 16: case 17: case 27: case 28: case 29: case 30: case 31: case 32: case 37: case 38: case 39: case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62: case 63: case 64: case 65: case 66: case 67: case 68: case 69: case 70: case 71: case 72: case 73: case 74: case 75: case 76: case 77: case 78: case 79: case 80: case 81: case 82: case 83: case 84: case 85: case 86: case 87: case 88: case 89: case 90: case 91: case 92: case 93: case 94: case 95: case 96:
            return 0x005b2ff0;  // shrubpatrol_animation_error
        case 15: case 97: case 98:
            return 0x005b3134;  // shrubpatrol_stand
        case 18: case 19: case 21: case 23: case 25:
            return 0x005b3020;  // shrubpatrol_scan_1
        case 20: case 22: case 24: case 26:
            return 0x005b300c;  // shrubpatrol_scan_2
        case 33:
            return 0x005b30d0;  // shrubpatrol_fire_tl
        case 34:
            return 0x005b30bc;  // shrubpatrol_fire_tr
        case 35:
            return 0x005b30a8;  // shrubpatrol_fire_bl
        case 36:
            return 0x005b3094;  // shrubpatrol_fire_br
        case 40:
            return 0x005b307c;  // shrubpatrol_reload_tl
        case 41:
            return 0x005b3064;  // shrubpatrol_reload_tr
        case 42:
            return 0x005b304c;  // shrubpatrol_reload_bl
        case 43:
            return 0x005b3034;  // shrubpatrol_reload_br
        default:
            return 0x005b2ff0;
    }
}

// 0x0043cb40 (stack: interaction type; ret 4): MonkObject vtable 0x005aac28 slot 105 (+0x1a4) (class from slot 1):
// animation name (switch over 0..0x73, bytes 0x0043cc34, jumps 0x0043cbec; else "monk_anim_error" 0x005b2eec). Not
// executed in recorded runs.
uint32_t fn_0043cb40(uint32_t type)
{    switch (type)
    {
        case 0: case 1: case 2: case 3: case 15: case 40: case 41: case 42: case 43:
            return 0x005b2fe4;  // monk_stand
        case 4: case 8: case 9: case 10: case 14: case 89:
            return 0x005b2fd8;  // monk_walk
        case 5: case 6: case 7: case 11: case 12: case 13: case 16: case 17: case 27: case 28: case 29: case 30: case 31: case 32: case 39: case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62: case 63: case 64: case 65: case 66: case 67: case 68: case 69: case 70: case 73: case 74: case 75: case 76: case 77: case 78: case 79: case 86: case 87: case 88: case 90: case 91: case 94: case 95: case 96: case 99: case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 108: case 109: case 110: case 111: case 112:
            return 0x005b2eec;  // monk_anim_error
        case 18: case 21: case 22:
            return 0x005b2fa8;  // monk_scan_3
        case 19: case 23: case 25:
            return 0x005b2fc0;  // monk_scan_1
        case 20: case 24: case 26:
            return 0x005b2fb4;  // monk_scan_2
        case 33: case 34: case 35: case 36:
            return 0x005b2f9c;  // monk_fire
        case 37:
            return 0x005b2f88;  // monk_cast_spell_2
        case 38:
            return 0x005b2f74;  // monk_cast_spell_1
        case 71: case 72:
            return 0x005b2f60;  // monk_hit_head_shot
        case 80: case 83:
            return 0x005b2f48;  // monk_die_1
        case 81: case 84:
            return 0x005b2f3c;  // monk_die_2
        case 82: case 85: case 93:
            return 0x005b2f30;  // monk_die_3
        case 92: case 97: case 98:
            return 0x005b2fcc;  // monk_fall
        case 107:
            return 0x005b2f54;  // monk_turbo
        case 113:
            return 0x005b2f20;  // monk_hang_into
        case 114:
            return 0x005b2efc;  // monk_hanging
        case 115:
            return 0x005b2f0c;  // monk_blown_up_into
        default:
            return 0x005b2eec;
    }
}


// 0x0044ef20 (stack: interaction type; ret 4): MolmerObject vtable 0x005ae5f8 slot 105 (+0x1a4) (class from slot 1):
// animation name (switch over 0..0x80, bytes 0x0044f0dc, jumps 0x0044f05c; else "molmer_error_anim" 0x005b114c);
// types 27..29 pick "molmer_idle_long_1/2" by rand. Not executed in recorded runs.
uint32_t fn_0044ef20(uint32_t type)
{
    switch (type)
    {        case 0: case 15:
            return 0x005b1344;  // molmer_idle_tl
        case 1:
            return 0x005b1334;  // molmer_idle_tr
        case 2:
            return 0x005b1324;  // molmer_idle_bl
        case 3:
            return 0x005b1314;  // molmer_idle_br
        case 4: case 8: case 9: case 14: case 47:
            return 0x005b129c;  // molmer_run
        case 5: case 6: case 7: case 11: case 12: case 13: case 16: case 17: case 37: case 38: case 44: case 45: case 46: case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62: case 75: case 76: case 77: case 78: case 79: case 86: case 87: case 88: case 89: case 90: case 91: case 92: case 94: case 95: case 96: case 98: case 99: case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107: case 108: case 109: case 110: case 111: case 112: case 115: case 116: case 117: case 118: case 119: case 120: case 121: case 122: case 123: case 124: case 125: case 126:
            return 0x005b114c;  // molmer_error_anim
        case 10:
            return 0x005b128c;  // molmer_scan_run
        case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26:
            return 0x005b1300;  // molmer_scan_stand
        case 27: case 28: case 29:
            if (static_cast<int32_t>((Rand() * 100) & 0xffff8000) >= 0x190000)
                return 0x005b12a8;  // molmer_idle_long_2
            return 0x005b12bc;  // molmer_idle_long_1
        case 30:
            return 0x005b12f0;  // molmer_idle_1
        case 31:
            return 0x005b12e0;  // molmer_idle_2
        case 32:
            return 0x005b12d0;  // molmer_idle_3
        case 33:
            return 0x005b1270;  // molmer_fire_tl
        case 34:
            return 0x005b1260;  // molmer_fire_tr
        case 35:
            return 0x005b1250;  // molmer_fire_bl
        case 36:
            return 0x005b1240;  // molmer_fire_br
        case 39:
            return 0x005b1160;  // molmer_throw_tnt
        case 40:
            return 0x005b122c;  // molmer_reload_tl
        case 41:
            return 0x005b1218;  // molmer_reload_tr
        case 42:
            return 0x005b1204;  // molmer_reload_bl
        case 43:
            return 0x005b11f0;  // molmer_reload_br
        case 48:
            return 0x005b11dc;  // molmer_run_reload
        case 63: case 64: case 65: case 66:
            return 0x005b138c;  // molmer_hit_mgun
        case 67: case 68:
            return 0x005b1378;  // molmer_hit_small
        case 69: case 70:
            return 0x005b1368;  // molmer_hit_med
        case 71: case 72: case 73: case 74:
            return 0x005b1354;  // molmer_hit_large
        case 80: case 81: case 82: case 83: case 84: case 85: case 93:
            return 0x005b1174;  // molmer_die
        case 97:
            return 0x005b1280;  // molmer_fall
        case 113:
            return 0x005b1190;  // molmer_hang_into
        case 114:
            return 0x005b1180;  // molmer_hanging
        case 127:
            return 0x005b11c0;  // mover_molmer_kill_grunt_1
        case 128:
            return 0x005b11a4;  // mover_molmer_kill_grunt_2
        default:
            return 0x005b114c;
    }
}

// ================================================================================================================
// Animation-set helpers
// ================================================================================================================

// 0x00453100 (eax = actor; returns eax): the animation set's +0 (actor+0x414 -> +0) when >= 0, else 0.
uint32_t fn_00453100(uint8_t* actor)
{
    const int32_t v = Field<int32_t>(Field<uint8_t*>(actor, 0x414), 0);
    return v < 0 ? 0u : static_cast<uint32_t>(v);
}

// 0x00453120 (eax = actor, edx = column; stack: row; ret 4): the set's lookup table (actor+0x414 -> +0x10: width +0x28,
// table +0x38): table[row * width + column], or 0 when the width is 0. Called by 0x00408ee0 with (time, entry index).
uint32_t fn_00453120(uint8_t* actor, uint32_t column, uint32_t row)
{
    uint8_t* t = Field<uint8_t*>(Field<uint8_t*>(actor, 0x414), 0x10);
    const uint32_t width = Field<uint32_t>(t, 0x28);
    if (!width)
        return 0;
    return Field<uint32_t*>(t, 0x38)[width * row + column];
}

// 0x00453c50 (stack: actor, bone key, out matrix, a3 (byte), bone record (0 = a local one), a5 (unused), a6 (byte),
// a7, a8 (byte); ret 0x24; returns AL): the world (or model) matrix of one animated bone. Calls the actor's virtual
// +0x3c first. Without a key (or without blend records, actor+0x1d4 <= 0, or when the bone cannot be evaluated) the
// result is the actor matrix: virtual +0x78(out) when a3 and not a8, else +0x70(out); identity when a6 is clear;
// returns 1 without a key, 0 on failure. Otherwise the bone record (0x90+ bytes: +0 matrix, +0x44 matrix, +0x88 key,
// +0x8c per-record index array; a local one with an identity +0x44 when a4 is 0) is evaluated by 0x004ecd40 (actor
// +0x1d8 records, count, record, 1, pose +0x410, stamp +0x1d0, blend +0x2c8, a7). When that fails the key is looked up in
// Game+0xa84's bone table (0x0045ace0 with actor+4, then 0x004eb5d0) and evaluated through a second record (key from
// 0x004eb670) and combined with Matrix_Multiply; then the a6/a3/a8 combination of the record matrix with the actor
// matrix (a3 && a8: translation row scaled by actor+0x37c x, y, x - see original-bugs note in the final report).
uint32_t fn_00453c50(uint8_t* actor, uint32_t key, uint32_t out, uint8_t a3, uint8_t* rec, uint32_t /*a5*/, uint8_t a6,
                     uint32_t a7, uint8_t a8)
{
    // The original frame: esp+0x10 .. esp+0x1d4 (sub esp, 0x1c4). F(x) is the local at esp+x.
    alignas(4) uint8_t frame[0x1c4];
    auto F = [&](uint32_t x) { return frame + (x - 0x10); };
    VCall<uint32_t>(actor, 0x3c / 4);
    if (!key)
    {
        if (a6)
        {
            if (a3 && !a8)
                VCall<uint32_t>(actor, 0x78 / 4, out);
            else
                VCall<uint32_t>(actor, 0x70 / 4, out);
            return 1;
        }
        std::memset(P(out), 0, 0x44);
        Field<uint32_t>(P(out), 0) = 0x3f800000;
        Field<uint32_t>(P(out), 0x14) = 0x3f800000;
        Field<uint32_t>(P(out), 0x28) = 0x3f800000;
        Field<uint32_t>(P(out), 0x3c) = 0x3f800000;
        return 1;
    }
    bool ok = false;
    const int32_t count = Field<int32_t>(actor, 0x1d4);
    if (count > 0)
    {
        if (!rec)
        {
            std::memset(F(0x10), 0, 0x10);
            std::memset(F(0xf8), 0, 0x44);
            rec = F(0xb4);
            Field<uint32_t>(F(0xf8), 0) = 0x3f800000;
            Field<uint32_t>(F(0x10c), 0) = 0x3f800000;
            Field<uint32_t>(F(0x120), 0) = 0x3f800000;
            Field<uint32_t>(F(0x134), 0) = 0x3f800000;
            Field<uint32_t>(F(0x13c), 0) = key;
            Field<uint32_t>(F(0x140), 0) = u(F(0x10));
        }
        uint8_t* records = actor + 0x1d8;
        if (orig::fn_004ecd40(u(records), static_cast<uint32_t>(count), u(rec), 1, Field<uint32_t>(actor, 0x410),
                              Field<uint32_t>(actor, 0x1d0), Field<uint32_t>(actor, 0x2c8), a7))
        {
            ok = true;
        }
        else
        {
            const uint32_t table = orig::fn_0045ace0(u(actor + 4), Field<uint32_t>(g_game, 0xa84));
            const uint32_t bone = table ? orig::fn_004eb5d0(table, key, 0, key) : 0;
            // ECX = key (0x00453ddc mov ecx, [esp+0x1dc], pushed as the key); edx = 0 (0x00453de4), so the
            // callee's miss path that reads the ECX slot is not reached.
            if (bone)
            {
                bool evaluated;
                if (Field<uint32_t>(P(bone), 0x10))
                {
                    std::memset(F(0x10), 0, 0x10);
                    Field<uint32_t>(F(0x1d0), 0) = u(F(0x10));
                    orig::fn_004e28f0(u(F(0x188)));
                    const uint32_t src = Field<uint32_t>(P(bone), 0x10);
                    Field<uint32_t>(F(0x1cc), 0) = orig::fn_004eb670(src, src);
                    evaluated = orig::fn_004ecd40(u(records), Field<uint32_t>(actor, 0x1d4), u(F(0x144)), 1,
                                                  Field<uint32_t>(actor, 0x410), Field<uint32_t>(actor, 0x1d0),
                                                  Field<uint32_t>(actor, 0x2c8), a7) != 0;
                }
                else
                {
                    orig::fn_004e28f0(u(F(0x144)));
                    evaluated = true;
                }
                if (evaluated)
                {
                    for (int32_t i = 0; i < Field<int32_t>(actor, 0x1d4); ++i)
                        Field<uint32_t*>(rec, 0x8c)[i] = bone;
                    orig::fn_004e3370(u(F(0x70)), bone + 0x1c);
                    orig::fn_004e2910(u(F(0x70)), u(F(0x2c)), u(F(0x144)));
                    orig::fn_004e2910(u(rec + 0x44), u(rec), u(F(0x2c)));
                    ok = true;
                }
            }
        }
    }
    if (!ok)
    {
        if (!a6)
        {
            std::memset(P(out), 0, 0x44);
            Field<uint32_t>(P(out), 0) = 0x3f800000;
            Field<uint32_t>(P(out), 0x14) = 0x3f800000;
            Field<uint32_t>(P(out), 0x28) = 0x3f800000;
            Field<uint32_t>(P(out), 0x3c) = 0x3f800000;
            return 0;
        }
        if (a3 && !a8)
            VCall<uint32_t>(actor, 0x78 / 4, out);
        else
            VCall<uint32_t>(actor, 0x70 / 4, out);
        return 0;
    }
    if (!a6)
    {
        std::memcpy(P(out), rec, 0x44);
        return 1;
    }
    if (!a3)
    {
        VCall<uint32_t>(actor, 0x70 / 4, u(F(0x70)));
        orig::fn_004e2910(u(rec), out, u(F(0x70)));
        return 1;
    }
    if (a8)
    {
        orig::fn_004535b0(u(F(0x20)), u(actor));
        std::memcpy(F(0x2c), rec, 0x44);
        Field<float>(F(0x38), 0) = Field<float>(F(0x38), 0) * Field<float>(F(0x20), 0);
        Field<float>(F(0x48), 0) = Field<float>(F(0x48), 0) * Field<float>(F(0x24), 0);
        Field<float>(F(0x58), 0) = Field<float>(F(0x58), 0) * Field<float>(F(0x20), 0);
        VCall<uint32_t>(actor, 0x70 / 4, u(F(0x70)));
        orig::fn_004e2910(u(F(0x2c)), out, u(F(0x70)));
        return 1;
    }
    VCall<uint32_t>(actor, 0x78 / 4, u(F(0x70)));
    orig::fn_004e2910(u(rec), out, u(F(0x70)));
    return 1;
}

// 0x004543f0 (eax = blend records (0 = actor+0x1d8 with count actor+0x1d4), ecx = actor, edx = stamp (0 = actor
// +0x1d0); stack: count, use blend (byte), a2; ret 0xc): poses the actor's skeleton (actor+0x410) from blend records
// with 0x004ed760 (eax = records; count, pose, stamp, blend = use blend ? actor+0x2c8 : 0, a2). Several records go
// straight through; a single one (also any count < 1 other than 0) is copied to a local record keeping only +4 and
// +0x10 with mode/weight (+0x14/+0x18) 0. Count 0 does nothing. The local record's +0, +8 and +0xc are uninitialised
// stack in the original (zero here).
uint32_t fn_004543f0(uint32_t records, uint8_t* actor, uint32_t stamp, int32_t count, uint8_t useBlend, uint32_t a2)
{
    if (!records)
    {
        count = Field<int32_t>(actor, 0x1d4);
        records = u(actor + 0x1d8);
    }
    if (!stamp)
        stamp = Field<uint32_t>(actor, 0x1d0);
    const uint32_t blend = useBlend ? Field<uint32_t>(actor, 0x2c8) : 0u;
    if (!count)
        return records;
    const uint32_t pose = Field<uint32_t>(actor, 0x410);
    if (count > 1)
        return orig::fn_004ed760(records, static_cast<uint32_t>(count), pose, stamp, blend, a2);
    uint32_t local[7] = {};
    local[1] = Field<uint32_t>(P(records), 4);
    local[4] = Field<uint32_t>(P(records), 0x10);
    local[6] = 0;
    local[5] = 0;
    return orig::fn_004ed760(u(local), 1, pose, stamp, blend, a2);
}

// 0x00458040 (esi = actor, edi = out vec3; no stack; returns AL): the position of the actor's "ref_hit_00" bone
// (0x00453c50(actor, 0x005b68fc, &m, 1, 0, 1, 1, 0, 0)): out = (m[3], m[7], m[11]), returns 1; without it the
// actor's virtual +0x94(out, &tmp, 1) and returns 0. guess: aim/hit reference point.
uint32_t fn_00458040(uint8_t* actor, float* out)
{
    alignas(4) uint8_t frame[0x48];  // +0 tmp, +4 matrix (0x44 bytes)
    uint8_t* m = frame + 4;
    if (fn_00453c50(actor, 0x005b68fcu, u(m), 1, nullptr, 1, 1, 0, 0) & 0xff)
    {
        Field<uint32_t>(out, 8) = Field<uint32_t>(m, 0x2c);
        Field<uint32_t>(out, 0) = Field<uint32_t>(m, 0xc);
        Field<uint32_t>(out, 4) = Field<uint32_t>(m, 0x1c);
        return 1;
    }
    VCall<uint32_t>(actor, 0x94 / 4, u(out), u(frame), 1u);
    return 0;
}

// 0x004580a0 (eax = out vec3, ecx = actor, ebx = shooter position; stack: projectile speed (float), use ref bone
// (byte); ret 8): aim lead. out = the actor's position (0x00458040 when the byte is set, else virtual +0x54); when the
// actor moves (horizontal speed > 0x005bcbec), the speed is > 0x005bcbec and the target is not on top of the shooter
// (|d|^2 >= 0x005bcef0, double), solves the 2D intercept time t = 1 / ((sqrt(disc) - b) / (2 |d|^2)) with
// b = (d . v) * 0x005bcc40, disc = b^2 - (|v|^2 - speed^2) |d|^2 * 4 (1.0 when negative) and adds v * t to out x/y.
uint32_t fn_004580a0(float* out, uint8_t* actor, const float* from, float speed, uint8_t useBone)
{
    float v[3];
    orig::fn_00458000(u(actor), u(v));
    if (useBone)
        fn_00458040(actor, out);
    else
        VCall<uint32_t>(actor, 0x54 / 4, u(out));
    const float sq = v[1] * v[1] + v[0] * v[0];
    if (!(std::sqrt(sq) > K(0x005bcbec)))
        return 0;
    if (!(speed > K(0x005bcbec)))
        return 0;
    const float dy = from[1] - out[1];
    const float dx = from[0] - out[0];
    const float d2 = dx * dx + dy * dy;
    if (static_cast<double>(std::fabs(d2)) < GameGlobal<double>(0x005bcef0))
        return 0;
    const float b = (dx * v[0] + dy * v[1]) * K(0x005bcc40);
    float disc = b * b - (sq - speed * speed) * d2 * K(0x005bcbe0);
    if (disc < K(0x0056c05c))
        disc = K(0x005bcb2c);
    const float r = std::sqrt(disc) - b;
    const float t = K(0x005bcb2c) / (r / (d2 + d2));
    out[0] = v[0] * t + out[0];
    out[1] = t * v[1] + out[1];
    return 0;
}

namespace
{
// The track lookup used by 0x004586a0 and 0x00458760: in the actor's first clip (actor+0x1dc -> +8 -> +0x2c -> +4)
// find `key` (0x004e0f40) and return the track's data (found + word[found+2] -> +0), or 0.
inline uint32_t ClipTrack(uint8_t* actor, uint32_t key)
{
    uint8_t* tl = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(actor, 0x1dc), 8), 0x2c), 4);
    const uint32_t count = Field<uint32_t>(tl, 0x1c);
    if (!count || !Field<uint32_t>(tl, 0x20))
        return 0;
    uint8_t* it = Field<uint8_t*>(tl, 0x24);
    Field<uint32_t>(it, 0) = count;
    Field<uint32_t>(it, 0x18) = key;
    orig::fn_004e0f40(u(it));
    if (Field<uint32_t>(it, 0xc))
        return 0;
    const uint32_t found = Field<uint32_t*>(it, 0x10)[Field<uint32_t>(it, 8)];
    if (!found)
        return 0;
    return Field<uint32_t>(P(found + Field<uint16_t>(P(found), 2)), 0);
}
}  // namespace

// 0x00458600 (esi = actor; no stack): binds the actor's body parts to skeleton bones: row = virtual +0x40, column =
// 0x00453100(actor), table = 0x00453120 -> +0x18 (one entry per part); for each part of the 0x4c-byte list at
// actor+0x1bc (until a 0 first dword) whose entry has +8 and a bone record +0xc, pose(+0x410)->+0x10[word(rec+0xa)] =
// the part's first dword. Returns the table cursor.
uint32_t fn_00458600(uint8_t* actor)
{
    const uint32_t row = VCall<uint32_t>(actor, 0x40 / 4);
    uint8_t* parts = Field<uint8_t*>(actor, 0x1bc);
    const uint32_t column = fn_00453100(actor);
    uint32_t* p = reinterpret_cast<uint32_t*>(fn_00453120(actor, column, row) + 0x18);
    uint32_t part = Field<uint32_t>(parts, 0);
    while (part)
    {
        const uint32_t e = *p++;
        const uint32_t rec = Field<uint32_t>(P(e), 0xc);
        if (Field<uint32_t>(P(e), 8) && rec)
            Field<uint32_t*>(Field<uint8_t*>(actor, 0x410), 0x10)[Field<uint16_t>(P(rec), 0xa)] = part;
        part = Field<uint32_t>(parts, 0x4c);
        parts += 0x4c;
    }
    return u(p);
}

// 0x00458660 (esi = part index, edi = actor; no stack): the bone record (+0xc) of body part `index` for the animation in
// slot 0 (0x00408800 index 4; table 0x00453120(actor, 0x00453100(actor), slot entry) -> +0x18), or 0 when the index is
// negative or the entry has no +8. Not executed in recorded runs.
uint32_t fn_00458660(int32_t index, uint8_t* actor)
{
    if (index < 0)
        return 0;
    const uint32_t column = fn_00453100(actor);
    uint8_t* slot = B(orig::fn_00408800(4, u(actor)));
    const uint32_t t = fn_00453120(actor, column, Field<uint32_t>(slot, 0));
    if (t + 0x18 == 0)
        return 0;
    // The entries are inline at t+0x18 (0x0045868a: mov eax, [eax + esi*4 + 0x18]), not behind a pointer: the extra
    // dereference read garbage and crashed when an arrow or sticky bomb hit a body part (2026-10-11).
    const uint32_t e = Field<uint32_t>(P(t), 0x18 + 4 * index);
    if (!e || !Field<uint32_t>(P(e), 8))
        return 0;
    return Field<uint32_t>(P(e), 0xc);
}

// 0x004586a0 (edx = track name (0 = "center" 0x005a009c), edi = actor; no stack): the skeleton bone driven by a named
// track of the actor's first clip: pose(+0x410)->+0x10[word(track+0xa)], or 0 without blend records (+0x1d4), track or
// bone array.
uint32_t fn_004586a0(uint32_t key, uint8_t* actor)
{
    if (!Field<uint32_t>(actor, 0x1d4))
        return 0;
    if (!key)
        key = 0x005a009c;
    const uint32_t track = ClipTrack(actor, key);
    if (!track)
        return 0;
    const uint32_t* bones = Field<uint32_t*>(Field<uint8_t*>(actor, 0x410), 0x10);
    if (!bones)
        return 0;
    return bones[Field<uint16_t>(P(track), 0xa)];
}

// 0x00458720 (eax = actor, edx = track name; no stack): the parent/index word (signed +0xa) of the record at +0x24 of
// the bone 0x004586a0 finds, or -1. (The original copies the bone's 0x20 bytes from +0x14 to the stack first.)
uint32_t fn_00458720(uint8_t* actor, uint32_t key)
{
    const uint32_t bone = fn_004586a0(key, actor);
    if (!bone)
        return 0xffffffffu;
    const uint32_t r = Field<uint32_t>(P(bone), 0x24);
    if (!r)
        return 0xffffffffu;
    return static_cast<uint32_t>(static_cast<int32_t>(Field<int16_t>(P(r), 0xa)));
}

// 0x00458760 (ebx = actor; no stack; 16-byte aligned frame): pushes the actor's "chest" bone (0x0059e210 track of the
// first clip) away from the Game+0x748 object: d = actor position (virtual +0x6c) - that object's position (+0x6c)
// with 0x005bcd50 added to z, normalised (0 when zero), scaled by 0x004581c0(actor) * 0x005bcda8, and passed with the
// bone's 0x20 bytes from +0x14 (copied behind the vector) to 0x00455aa0(eax = &vector; actor, 0.2, bone index
// (signed word +0xa of bone+0x24), 0, 0). Only caller: BridgeObject 0x0040abc0. Not executed in recorded runs.
// guess: knock-back impulse on the bridge grunts.
uint32_t fn_00458760(uint8_t* actor)
{
    alignas(16) uint8_t s[0x50];  // the original frame from esp after the two register pushes
    if (!Field<uint32_t>(actor, 0x1d4))
        return 0;
    const uint32_t track = ClipTrack(actor, 0x0059e210u);
    if (!track)
        return 0;
    uint8_t* pose = Field<uint8_t*>(actor, 0x410);
    if (!Field<uint32_t>(pose, 0x10))
        return 0;
    const uint32_t bone = Field<uint32_t*>(pose, 0x10)[Field<uint16_t>(P(track), 0xa)];
    VCall<uint32_t>(Field<void*>(g_game, 0x748), 0x6c / 4, u(s + 0x18));
    VCall<uint32_t>(actor, 0x6c / 4, u(s + 0xc));
    float* a = reinterpret_cast<float*>(s + 0xc);
    float* b = reinterpret_cast<float*>(s + 0x18);
    const float dx = a[0] - b[0];
    Field<float>(s, 0x34) = a[1] - b[1];
    const float dz = a[2] - b[2] + K(0x005bcd50);
    const float dy = Field<float>(s, 0x34);
    const float len2 = dz * dz + dy * dy + dx * dx;
    float inv;
    if (len2 == K(0x0056c05c))
        inv = K(0x0056c05c);
    else
        inv = K(0x005bcb2c) / std::sqrt(len2);
    Field<float>(s, 0x30) = inv * dx;
    Field<float>(s, 0x34) = Field<float>(s, 0x34) * inv;
    Field<float>(s, 0x38) = inv * dz;
    const float k = orig::fn_004581c0(u(actor)) * K(0x005bcda8);
    const float nx = Field<float>(s, 0x30) * k;
    const float ny = Field<float>(s, 0x34) * k;
    const float nz = Field<float>(s, 0x38) * k;
    Field<float>(s, 0x38) = nz;
    Field<float>(s, 0x2c) = nz;
    Field<float>(s, 0x24) = nx;
    std::memcpy(s + 0x30, P(bone + 0x14), 0x20);
    Field<float>(s, 0x28) = ny;
    const int32_t index = Field<int16_t>(Field<uint8_t*>(s, 0x40), 0xa);
    return orig::fn_00455aa0(u(s + 0x24), u(actor), 0x3e4ccccdu, static_cast<uint32_t>(index), 0, 0);
}

// 0x004588f0 (ecx = path or 0, esi = output buffer; no stack): copies the file name of a path without directory
// ('\\' or '/') and without extension (stops at the first '.') to the buffer; does nothing for a null path. The
// original returns leftover register contents (0 here).
uint32_t fn_004588f0(const char* path, char* out)
{
    if (!path)
        return 0;
    out[0] = 0;
    int32_t i = 0;
    if (path[0])
    {
        while (path[i + 1])
            ++i;
        ++i;
        while (i > 0)
        {
            const char c = path[i - 1];
            if (c == '\\' || c == '/')
                break;
            --i;
        }
    }
    const char* p = path + i;
    int32_t j = 0;
    if (*p != '.')
    {
        for (;;)
        {
            const char c = *p;
            out[j] = c;
            if (!c)
                break;
            const char next = p[1];
            ++p;
            ++j;
            if (next == '.')
                break;
        }
    }
    out[j] = 0;
    return 0;
}

// 0x00458950 (edx = passed on; stack: actor; ret 4): 0x0045bf10(eax = actor+0x414 (animation set), edx; stack:
// Game+0xa84).
uint32_t fn_00458950(uint32_t edx, uint8_t* actor)
{
    return orig::fn_0045bf10(Field<uint32_t>(actor, 0x414), edx, u(GameA84()));
}

// 0x00461d80 (no inputs): returns the class descriptor 0x00585618 whose +0x14 is "CLASS(AnimPlayer)": slot 1 of the
// AnimPlayer vtable (referenced at 0x005ab78c). Not executed in recorded runs.
uint32_t AnimPlayer_GetClass()
{
    return 0x00585618;
}

// 0x00472c30 (al = flags, ecx = int* slot out, ebx = animation, esi = actor, edi = int* row out; stack: row; caller
// pops; returns AL): *row = (flags & 0xc0) ? 0 : row; searches the 4 entries actor+0x6a0[*row * 0x5d + *slot] for one
// whose set entry (actor+0x414 -> +0xc, 0xc bytes) has anim (+8) == animation+0xc; returns 1 with *slot on it, else 0
// (*slot = 4). EAX's upper bytes follow the original (anim pointer / 0).
uint32_t fn_00472c30(uint32_t flags, int32_t* slot, uint8_t* anim, uint8_t* actor, int32_t* row, uint32_t rowValue)
{
    *row = (flags & 0xc0) ? 0 : static_cast<int32_t>(rowValue);
    *slot = 0;
    int32_t j = 0;
    for (;;)
    {
        const int32_t entry = reinterpret_cast<int32_t*>(actor + 0x6a0)[*row * 0x5d + *slot];
        if (entry >= 0)
        {
            const uint32_t a = Field<uint32_t>(Field<uint8_t*>(Field<uint8_t*>(actor, 0x414), 0xc) + entry * 0xc, 8);
            if (a == Field<uint32_t>(anim, 0xc))
                return (a & 0xffffff00u) | 1;
        }
        ++j;
        *slot = j;
        if (j >= 4)
            return 0;
    }
}

// 0x00472c90 (eax = index, ecx = out vec3, edx = actor; stack: out value; ret 4): out = actor+0x66c + index*0xc
// (3 dwords), *value = actor+0x684[index]; returns that value.
uint32_t fn_00472c90(uint32_t index, uint32_t* out, uint8_t* actor, uint32_t* value)
{
    const uint32_t* src = reinterpret_cast<const uint32_t*>(actor + (index * 3 + 0x19b) * 4);
    out[0] = src[0];
    out[1] = src[1];
    out[2] = src[2];
    const uint32_t v = reinterpret_cast<const uint32_t*>(actor + 0x684)[index];
    *value = v;
    return v;
}

// ================================================================================================================
// Player animation helpers
// ================================================================================================================

// 0x00475d50 (esi = player; stack: x, y, z, w (floats); ret 0x10): picks the turbo (jet-pack, guess) animation of the
// object at +0xabc from a direction: with +0xa34 bit 0x80 and w != 0 the direction becomes (-w, 0, 0). A zero
// direction or a dominant |z| plays "turbo_up_cycle"; dominant |y| "turbo_forward_cycle" (y > 0) or
// "turbo_backward_cycle"; dominant |x| "turbo_right_cycle" (x <= 0) or "turbo_left_cycle". These need +0xa34 bit 4 and
// +0x570 == 0, else "turbo_idle" (flags 0x80). All through 0x00408e30(flags 0x81, ecx = +0xabc, index -1, weight 0.25).
// Note: in the |x| > |y| branch the original compares |y| (not |x|) with |z| (reproduced).
uint32_t fn_00475d50(uint8_t* player, float x, float y, float z, float w)
{
    uint8_t* obj = Field<uint8_t*>(player, 0xabc);
    if (!obj)
        return 0;
    const uint32_t flags = Field<uint32_t>(player, 0xa34);
    if ((flags & 0x80) && !(w == K(0x0056c05c)))
    {
        y = 0.0f;
        z = 0.0f;
        x = -w;
    }
    uint32_t name = 0;
    bool up = false;
    if (x == K(0x0056c05c) && y == K(0x0056c05c) && z == K(0x0056c05c))
    {
        up = true;
    }
    else
    {
        const float ax = std::fabs(x);
        const float ay = std::fabs(y);
        const float az = std::fabs(z);
        if (ay >= ax)  // unordered goes to the |x| branch (C0 set)
        {
            if (!(ax >= az))
                up = true;
            else if ((flags & 4) && Field<float>(player, 0x570) == K(0x0056c05c))
                name = y > K(0x0056c05c) ? 0x005b68acu : 0x0059f00cu;
        }
        else
        {
            if (!(ay >= az))
                up = true;
            else if ((flags & 4) && Field<float>(player, 0x570) == K(0x0056c05c))
                name = x > K(0x0056c05c) ? 0x0059efe8u : 0x0059efd4u;
        }
    }
    if (up && (flags & 4) && Field<float>(player, 0x570) == K(0x0056c05c))
        name = 0x0059effc;
    if (name)
        return fn_00408e30(0x81, obj, -1, name, 0, 0x3e800000u, 0, 0);
    return fn_00408e30(0x80, obj, -1, 0x005b6674u, 0, 0x3e800000u, 0, 0);
}

// 0x00475f50 (eax = suffix, ecx = prefix; stack: animation set (callers push four sets before popping them together:
// only the first is read); caller pops): _sprintf(buf, "%s%s", prefix, suffix) (0x0053814f, 0x80-byte buffer), looks
// the name up in the set (+0x10 -> [0], 0x004f1160) and returns its index; a missing or negative one logs "Missing
// player animation : %s" (Debug_Printf, an empty stub) and returns the index or -1.
uint32_t fn_00475f50(uint32_t suffix, uint32_t prefix, uint8_t* set)
{
    char buf[0x80];
    GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(buf, reinterpret_cast<const char*>(0x0059efccu),
                                                                       prefix, suffix);
    const int32_t* p = reinterpret_cast<int32_t*>(
        orig::fn_004f1160(Field<uint32_t>(Field<uint8_t*>(set, 0x10), 0), u(buf)));
    const int32_t index = p ? *p : -1;
    if (index < 0)
        orig::fn_00407ff0(Field<uint32_t>(g_game, 0x34), 7, 0x0059efacu, u(buf));
    return static_cast<uint32_t>(index);
}

// 0x00475fc0 (eax = slot index, ecx = actor; returns st0): the slot's playback position as a fraction: slot time (+4)
// / entry length, or 0.0 without an entry. Not executed in recorded runs.
float fn_00475fc0(uint32_t index, uint8_t* actor)
{
    uint8_t* slot = B(orig::fn_00408800(index, u(actor)));
    if (!slot)
        return K(0x0056c05c);
    const int32_t e = Field<int32_t>(slot, 0);
    if (e == -1)
        return K(0x0056c05c);
    const float* length = reinterpret_cast<const float*>(Field<uint8_t*>(Field<uint8_t*>(actor, 0x414), 0xc) + e * 0xc);
    if (!length)
        return K(0x0056c05c);
    return Field<float>(slot, 4) / *length;
}

// 0x004876c0 (stack: interaction type; ret 4): Q111Object vtable 0x005a6df8 slot 105 (+0x1a4) (class from slot 1):
// animation name (switch over 0..0x82, bytes 0x0048786c, jumps 0x004877e8; else "q111_error_anim" 0x0059dac8). Not
// executed in recorded runs.
uint32_t fn_004876c0(uint32_t type)
{
    switch (type)
    {        case 0: case 15:
            return 0x0059dc88;  // q111_idle_tl
        case 1:
            return 0x0059dc78;  // q111_idle_tr
        case 2:
            return 0x0059dc68;  // q111_idle_bl
        case 3:
            return 0x0059dc58;  // q111_idle_br
        case 4: case 8: case 9: case 14: case 47:
            return 0x0059dbe0;  // q111_run
        case 5: case 6: case 7: case 11: case 12: case 13: case 16: case 17: case 37: case 38: case 39: case 44: case 45: case 46: case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62: case 75: case 76: case 77: case 78: case 79: case 86: case 87: case 88: case 89: case 91: case 92: case 94: case 95: case 96: case 98: case 99: case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107: case 108: case 109: case 110: case 111: case 112: case 115: case 116: case 117: case 118: case 119: case 120: case 121: case 122: case 123: case 124: case 125: case 126: case 127: case 128:
            return 0x0059dac8;  // q111_error_anim
        case 10:
            return 0x0059dbd0;  // q111_scan_run
        case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26:
            return 0x0059dc48;  // q111_scan_stand
        case 27:
            return 0x0059dc14;  // q111_kneel_scan
        case 28:
            return 0x0059dc00;  // q111_idle_long_1
        case 29:
            return 0x0059dbec;  // q111_idle_long_2
        case 30:
            return 0x0059dc3c;  // q111_idle_1
        case 31:
            return 0x0059dc30;  // q111_idle_2
        case 32:
            return 0x0059dc24;  // q111_idle_3
        case 33:
            return 0x0059dbb4;  // q111_fire_tl
        case 34:
            return 0x0059dba4;  // q111_fire_tr
        case 35:
            return 0x0059db94;  // q111_fire_bl
        case 36:
            return 0x0059db84;  // q111_fire_br
        case 40:
            return 0x0059db74;  // q111_reload_tl
        case 41:
            return 0x0059db64;  // q111_reload_tr
        case 42:
            return 0x0059db54;  // q111_reload_bl
        case 43:
            return 0x0059db44;  // q111_reload_br
        case 48:
            return 0x0059db34;  // q111_run_reload
        case 63: case 64: case 65: case 66:
            return 0x0059dcc8;  // q111_hit_mgun
        case 67: case 68:
            return 0x0059dcb8;  // q111_hit_small
        case 69: case 70:
            return 0x0059dca8;  // q111_hit_med
        case 71: case 72: case 73: case 74:
            return 0x0059dc98;  // q111_hit_large
        case 80: case 81: case 82: case 83: case 84: case 85: case 93:
            return 0x0059dad8;  // q111_die
        case 90:
            return 0x0059dae4;  // q111_drop_tea
        case 97:
            return 0x0059dbc4;  // q111_fall
        case 113: case 114:
            return 0x0059daf4;  // q111_hanging
        case 129:
            return 0x0059db1c;  // mover_q_kill_grunt_1
        case 130:
            return 0x0059db04;  // mover_q_kill_grunt_2
        default:
            return 0x0059dac8;
    }
}

namespace
{
// Calls the animation's event/sample callback (anim+4) the way 0x00493ca0 does: cdecl (a1, record) with
// eax = 0x004eb780's result, ecx = record, edx = a1, esi = anim (the callback's own convention is unknown).
uint32_t CallAnimCallback(uint32_t anim, uint32_t a1, uint32_t record, uint32_t eaxValue)
{
    uint32_t result;
    __asm {
        push esi
        push ebx
        push edi
        push ebp
        mov esi, anim
        mov ecx, record
        mov edx, a1
        mov eax, eaxValue
        push ecx
        push edx
        call dword ptr [esi + 4]
        add esp, 8
        pop ebp
        pop edi
        pop ebx
        pop esi
        mov result, eax
    }
    return result;
}
}  // namespace

// 0x00493ca0 (eax = clip player; stack: time (float), a1; caller pops 8): frame = min(ftol(time * 0x005bcc04 + 0.5),
// player+8) (unsigned); with a callback-bearing animation (player+4, its +8 nonzero) fills a 0x24-byte local record
// {0, player+8, ..., +0x18 = frame} through 0x004eb780(eax = anim; &record) and calls anim+4(a1, &record). The record's
// other words are uninitialised in the original (zero here). Returns the callback's result (0 without one).
uint32_t fn_00493ca0(uint8_t* player, float time, uint32_t a1)
{
    uint32_t frame = static_cast<uint32_t>(Ftol(time * K(0x005bcc04) + K(0x005bcb4c)));
    const uint32_t count = Field<uint32_t>(player, 8);
    if (frame > count)
        frame = count;
    uint8_t* anim = Field<uint8_t*>(player, 4);
    uint32_t rec[9] = {};
    rec[0] = 0;
    rec[1] = count;
    rec[6] = frame;
    if (!Field<uint32_t>(anim, 8))
        return 0;
    const uint32_t r = orig::fn_004eb780(u(anim), u(rec));
    return CallAnimCallback(u(anim), a1, u(rec), r);
}

// 0x00477570 (esi = player; stack: dt (float); ret 4): foot placement / ground follow for the player (guess, from the
// behaviour: bone keys at 0x005ce328 are two foot bones, the first "l_ankle"). With the pose stamp bumped (or
// Log_Printf "Error: Can't generate stamp code, no cache" without a pose) and flags 0x30 of +0x690/+0x804 masked, for
// each foot: the bone's world position (0x00453c50) minus the player's offset (0x00457850) is cast down (0x00413cc0 /
// 0x00413dc0 / 0x00413d40 / 0x00413e10, the ray length 12.0 doubling after each hit) and the foot height (+0x664[i])
// and ground normal (+0x64c[i]) blend towards the hit by min(dt * 0x005bcb68, 1), the normal renormalised; min and max
// heights are tracked. Restores +0x1d0/+0x690/+0x804; a low foot with +0x4c8 <= 0 sets +0xa34 |= 0x600 (unless 0x80 or
// 0x1200); +0x648 = sin(+0x640 / +0x644 * 0x005bcb70 + 0x005bcb70) bob while +0x640 > 0, +0x550 > +0xa74 and +0xa34
// bit 0x800 (else both 0), +0x640 -= dt (floored at 0); per foot +0x66c copies of the normals and +0x684 heights above
// the lowest foot. The body offset (0x00457820) becomes lowest * 0x005bc15c - 0x005bcbfc + bob; a second ray from
// virtual +0x90's point down to the body pulls the player down onto the ground (virtual +0x6c, 0x00457870) and keeps
// the vertical velocity >= 1.0 (Body_getLinearVelocity / Body_setLinearVelocity). The original frame is reproduced as
// one 0xf4-byte array (offsets from its esp before the three register pushes).
uint32_t fn_00477570(uint8_t* actor, float dt)
{
    alignas(4) uint8_t f[0xf4];
    auto X = [&](uint32_t o) -> float& { return *reinterpret_cast<float*>(f + o); };
    auto XU = [&](uint32_t o) -> uint32_t& { return *reinterpret_cast<uint32_t*>(f + o); };
    XU(0x8) = 0x41400000;
    orig::fn_00457850(u(f + 0x10), u(actor));
    VCall<uint32_t>(actor, 0x54 / 4, u(f + 0x58));
    X(0x58) = X(0x58) - X(0x10);
    X(0x5c) = X(0x5c) - X(0x14);
    X(0x60) = X(0x60) - X(0x18);
    uint32_t stamp;
    uint8_t* pose = Field<uint8_t*>(actor, 0x410);
    if (pose)
    {
        if (++Field<uint32_t>(pose, 8) == 0)
            Field<uint32_t>(pose, 8) = 1;
        stamp = Field<uint32_t>(pose, 8);
    }
    else
    {
        GameLog(reinterpret_cast<const char*>(0x0058af8cu));
        stamp = 0;
    }
    const uint32_t f690 = Field<uint32_t>(actor, 0x690);
    const uint32_t f804 = Field<uint32_t>(actor, 0x804);
    X(0x54) = dt * K(0x005bcb68);
    XU(0x64) = f690;
    XU(0x1c) = f804;
    XU(0x20) = Field<uint32_t>(actor, 0x1d0);
    Field<uint32_t>(actor, 0x1d0) = stamp;
    Field<uint32_t>(actor, 0x690) = f690 & 0xffffffcf;
    Field<uint32_t>(actor, 0x804) = f804 & 0xffffffcf;
    XU(0x44) = 0;
    XU(0x48) = 0;
    XU(0x4c) = 0xbf800000;
    float* c = reinterpret_cast<float*>(actor + 0x664);
    float* n = reinterpret_cast<float*>(actor + 0x654);  // EBP: the normal's z; x = n[-2]
    for (uint32_t keyp = 0x005ce328; keyp < 0x005ce330; keyp += 4, ++c, n += 3)
    {
        X(0x34) = X(0x54);
        X(0xc) = X(0x54);
        if (X(0x34) > K(0x005bcb2c))
            XU(0x34) = 0x3f800000;
        if (X(0x54) > K(0x005bcb2c))
            XU(0xc) = 0x3f800000;
        fn_00453c50(actor, GameGlobal<uint32_t>(keyp), u(f + 0xb0), 0, nullptr, 0, 1, 0, 0);
        X(0x38) = X(0xbc) - X(0x10);
        X(0x3c) = X(0xcc) - X(0x14);
        X(0x40) = X(0xdc) - X(0x18) + K(0x005bccf0);
        orig::fn_00413cc0(u(f + 0x68), 1);
        orig::fn_00413dc0(Field<uint32_t>(GameA84(), 0xea0), u(f + 0x68));
        orig::fn_00413d40(u(actor), u(f + 0x68));
        f[0xa6] = 1;
        bool hit = false;
        float h = 0.0f;
        float nx = 0.0f;
        if (X(0x8) > K(0x0056c05c))
        {
            std::memcpy(f + 0x70, f + 0x38, 0x18);
            XU(0x88) = XU(0x8);
            f[0xa4] = 1;
            XU(0xa8) = 2;
            const uint8_t r = static_cast<uint8_t>(orig::fn_00413e10(u(f + 0x68)));
            XU(0x28) = XU(0x90);
            XU(0x2c) = XU(0x94);
            XU(0x30) = XU(0x98);
            if (r)
            {
                h = X(0x40) - (X(0x8c) - K(0x005bcbfc)) * K(0x005bcb2c);
                h = h - X(0x60);
                X(0x8) = X(0x8) + X(0x8);
                nx = X(0x28);
                hit = true;
            }
        }
        if (!hit)
        {
            h = K(0x0056c05c);
            XU(0x2c) = 0;
            nx = K(0x0056c05c);
            XU(0x30) = 0x3f800000;
            XU(0x8) = 0;
        }
        *c = (h - *c) * X(0x34) + *c;
        n[-2] = (nx - n[-2]) * X(0xc) + n[-2];
        n[-1] = (X(0x2c) - n[-1]) * X(0xc) + n[-1];
        n[0] = (X(0x30) - n[0]) * X(0xc) + n[0];
        const float len = std::sqrt(n[-2] * n[-2] + n[-1] * n[-1] + n[0] * n[0]);
        if (!(len == K(0x0056c05c)))
        {
            const float inv = K(0x005bcb2c) / len;
            n[-2] = inv * n[-2];
            n[-1] = inv * n[-1];
            n[0] = inv * n[0];
        }
        if (keyp == 0x005ce328 || X(0x0) > *c)
            X(0x0) = *c;
        if (keyp == 0x005ce328 || X(0x4) < *c)
            X(0x4) = *c;
    }
    if (X(0x0) < K(0x005bcdc0) && Field<float>(actor, 0x4c8) <= K(0x0056c05c))
    {
        const uint32_t a = Field<uint32_t>(actor, 0xa34);
        if (!(a & 0x80) && !(a & 0x1200))
            Field<uint32_t>(actor, 0xa34) = a | 0x600;
    }
    const bool bob = Field<float>(actor, 0x640) > K(0x0056c05c);
    Field<uint32_t>(actor, 0x1d0) = XU(0x20);
    Field<uint32_t>(actor, 0x690) = XU(0x64);
    Field<uint32_t>(actor, 0x804) = XU(0x1c);
    if (bob && Field<float>(actor, 0x550) > Field<float>(actor, 0xa74) && (Field<uint32_t>(actor, 0xa34) & 0x800))
    {
        const float a = Field<float>(actor, 0x640) / Field<float>(actor, 0x644) * K(0x005bcb70) + K(0x005bcb70);
        Field<float>(actor, 0x648) = static_cast<float>(std::sin(static_cast<double>(a)));
    }
    else
    {
        Field<uint32_t>(actor, 0x648) = 0;
        Field<uint32_t>(actor, 0x640) = 0;
    }
    const float left = Field<float>(actor, 0x640) - dt;
    Field<float>(actor, 0x640) = left;
    if (left < K(0x0056c05c))
        Field<uint32_t>(actor, 0x640) = 0;
    if (X(0x0) < K(0x005bce78))
        XU(0x0) = 0xc0200000;
    for (uint32_t i = 0; i < 2; ++i)
    {
        const float height = Field<float>(actor, 0x664 + i * 4);
        std::memcpy(actor + 0x66c + i * 0xc, actor + 0x64c + i * 0xc, 0xc);
        const float v = height - X(0x0) - Field<float>(actor, 0x648);
        Field<float>(actor, 0x684 + i * 4) = v;
        if (v < K(0x0056c05c))
            Field<uint32_t>(actor, 0x684 + i * 4) = 0;
    }
    const float m = X(0x0) - X(0x4);
    if (X(0x0) < m)
        X(0x0) = m;
    if (X(0x0) > K(0x0056c05c))
        XU(0x0) = 0;
    XU(0x14) = 0;
    XU(0x10) = 0;
    X(0x18) = X(0x0) * K(0x005bc15c) - K(0x005bcbfc) + Field<float>(actor, 0x648);
    orig::fn_00457820(u(f + 0x10), u(actor));
    orig::fn_00413cc0(u(f + 0x68), 1);
    VCall<uint32_t>(actor, 0x90 / 4, u(f + 0x70), u(f + 0x20), 1u);
    X(0x78) = X(0x20) - X(0x18) - Field<float>(actor, 0x45c) + X(0x78);
    VCall<uint32_t>(actor, 0x54 / 4, u(f + 0x44));
    X(0x4c) = X(0x4c) - X(0x18);
    XU(0x84) = 0xbf800000;
    const float rayLength = X(0x78) - X(0x4c);
    X(0x1c) = rayLength;
    X(0x88) = rayLength;
    orig::fn_00413dc0(Field<uint32_t>(GameA84(), 0xea0), u(f + 0x68));
    orig::fn_00413d40(u(actor), u(f + 0x68));
    f[0xa6] = 1;
    f[0xa4] = 1;
    XU(0xa8) = 2;
    if (!(orig::fn_00413e10(u(f + 0x68)) & 0xff))
        return 0;
    const float v = X(0x8c) - X(0x1c) + Field<float>(actor, 0x45c) * K(0x005bcb4c);
    X(0x4) = v;
    if (!(v < K(0x0056c05c)))
        return 0;
    VCall<uint32_t>(actor, 0x6c / 4, u(f + 0x38));
    X(0x40) = X(0x40) - X(0x4);
    orig::fn_00457870(0, u(f + 0x38), u(actor), 0, 0, 0);  // no stack arguments in reality (symbols/abi.csv says 3)
    orig::fn_00458000(u(actor), u(f + 0x28));
    if (X(0x30) < K(0x005bcb2c))
        XU(0x30) = 0x3f800000;
    orig::fn_00458510(u(f + 0x28), u(actor));
    return 0;
}

// ================================================================================================================
// Leg IK bone controllers (callbacks, cdecl, 7 arguments: out transform, in transform, bone, parent bone, skeleton,
// flags, row). A transform is 0x2c bytes: position (3), quaternion (4), scale (3), flags (+0x28).
// ================================================================================================================

namespace
{
// 0x004e7880 (ecx = vector, edx = out): normalises (out = v / |v|, unchanged when |v| == 0) and returns |v| in st0.
// The generated wrapper would leave st0 on the x87 stack, so this pops it.
inline float Normalize3(uint32_t src, uint32_t dst)
{
    float r;
    __asm {
        mov ecx, src
        mov edx, dst
        mov eax, 0x004e7880
        call eax
        fstp r
    }
    return r;
}
// 0x004e7800 (eax = vector; returns st0): |v|.
inline float Length3(uint32_t v)
{
    float r;
    __asm {
        mov eax, v
        mov edx, 0x004e7800
        call edx
        fstp r
    }
    return r;
}
// 0x00566ad0 / 0x00566a00: the C runtime's _CIacos / _CIasin (argument and result in st0). The original keeps the
// result on the x87 stack (fpatan: not rounded by the single precision control) and rounds only after the next
// fsub / fcom, so it is returned as double: rounding it to float first changes the last bits of a - b.
inline double CrtAcos(float x)
{
    double r;
    __asm {
        fld x
        mov eax, 0x00566ad0
        call eax
        fstp r
    }
    return r;
}
inline double CrtAsin(float x)
{
    double r;
    __asm {
        fld x
        mov eax, 0x00566a00
        call eax
        fstp r
    }
    return r;
}
// The clamp to [-1, 1] the IK code applies before acos/asin: below 0x005bcb5c (-1.0) gives -1.0, above 1.0 gives 1.0.
inline void ClampUnit(float& v)
{
    if (v < K(0x005bcb5c))
        v = bf(0xbf800000u);
    else if (v > K(0x005bcb2c))
        v = bf(0x3f800000u);
}
inline void CopyTail(uint8_t* out, const uint8_t* in)  // scale (+0x1c, 3 dwords) and flags | 1
{
    Field<uint32_t>(out, 0x1c) = Field<uint32_t>(in, 0x1c);
    Field<uint32_t>(out, 0x20) = Field<uint32_t>(in, 0x20);
    Field<uint32_t>(out, 0x24) = Field<uint32_t>(in, 0x24);
    Field<uint32_t>(out, 0x28) = Field<uint32_t>(in, 0x28) | 1;
}
inline void CopyPos(uint8_t* out, const uint8_t* in)
{
    Field<uint32_t>(out, 0) = Field<uint32_t>(in, 0);
    Field<uint32_t>(out, 4) = Field<uint32_t>(in, 4);
    Field<uint32_t>(out, 8) = Field<uint32_t>(in, 8);
}
}  // namespace

// 0x00472cc0 (cdecl; stack: out, in, bone, parent bone or 0, skeleton, flags, row; plain ret): leg IK bone callback
// (no direct callers; executed in recorded runs). The skeleton's actor (+0) keeps per (row, slot, leg) a 0x28-byte IK
// record at actor+0x6c0 + row*0x174 + (slot + leg*4)*0x28 (slot from 0x00472c30, leg = bit 0 of the bone kind
// skeleton+0x14[bone+0xa].+4 -> +8): +0 knee angle, +4 the ankle track ("l_ankle"/"r_ankle", 0x005ce328, looked up in
// the bone's animation), +8 hip rotation quaternion, +0x18 axis, +0x24 angle. Without a matching slot the transform is
// copied. For the ankle itself the record's rotation is undone (actor+0x690 + row*0x174 bit 0x20); for the knee the
// knee angle is applied about the axis; for the hip (bit 0x20) a two-bone solve with the law of cosines (CRT acos) puts
// the foot on the ground height from 0x00472c90; otherwise (bit 0x10) the bone is turned onto the ground normal
// (0x004f1830). The original 16-byte-aligned frame is one 0x220-byte array (offsets from esp after the pushes).
uint32_t fn_00472cc0(uint8_t* out, uint8_t* in, uint8_t* bone, uint8_t* parent, uint8_t* skel, uint32_t flags,
                     uint32_t rowValue)
{
    alignas(16) uint8_t S[0x220];
    auto SF = [&](uint32_t o) -> float& { return *reinterpret_cast<float*>(S + o); };
    auto SU = [&](uint32_t o) -> uint32_t& { return *reinterpret_cast<uint32_t*>(S + o); };
    auto SA = [&](uint32_t o) { return u(S + o); };
    uint8_t* actor = Field<uint8_t*>(skel, 0);
    const uint32_t kind =
        Field<uint32_t>(Field<uint8_t*>(Field<uint8_t*>(skel, 0x14) + Field<uint16_t>(bone, 0xa) * 8, 4), 8);
    SU(0x2c) = kind;
    SU(0x28) = kind & 1;
    SU(0x10) = u(actor);
    if (!(fn_00472c30(flags, reinterpret_cast<int32_t*>(S + 0x34), bone, actor, reinterpret_cast<int32_t*>(S + 0x14),
                      rowValue) &
          0xff))
    {
        std::memcpy(out, in, 0x2c);
        return 0;
    }
    const uint32_t rowOff = SU(0x14) * 0x174;
    const uint32_t off = rowOff + (SU(0x34) + SU(0x28) * 4) * 0x28;  // leg = kind & 1 (0x00472d21)
    uint8_t* R = actor + off + 0x6c0;
    const uint32_t track0 = Field<uint32_t>(R, 4);
    if (!track0 || Field<uint32_t>(P(track0), 0xc) != Field<uint32_t>(bone, 0xc))
    {
        uint8_t* tl = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(bone, 0xc), 0x2c), 4);
        const uint32_t key = GameGlobal<uint32_t>(0x005ce328 + SU(0x2c) * 4);
        uint32_t found = 0;
        const uint32_t count = Field<uint32_t>(tl, 0x1c);
        if (count && Field<uint32_t>(tl, 0x20))
        {
            uint8_t* it = Field<uint8_t*>(tl, 0x24);
            Field<uint32_t>(it, 0) = count;
            Field<uint32_t>(it, 0x18) = key;
            orig::fn_004e0f40(u(it));
            if (!Field<uint32_t>(it, 0xc))
            {
                const uint32_t t = Field<uint32_t*>(it, 0x10)[Field<uint32_t>(it, 8)];
                if (t)
                    found = Field<uint32_t>(P(t + Field<uint16_t>(P(t), 2)), 0);
            }
        }
        Field<uint32_t>(R, 4) = found;
    }
    VCall<uint32_t>(actor, 0x5c / 4, SA(0x1d8));
    const uint16_t index = Field<uint16_t>(bone, 0xa);
    uint8_t* track = Field<uint8_t*>(R, 4);
    const uint32_t legFlags = Field<uint32_t>(actor, rowOff + 0x690);
    if (index == Field<uint16_t>(track, 0xa))
    {
        // the ankle
        CopyPos(out, in);
        if (legFlags & 0x20)
        {
            orig::fn_004f1350(u(R + 8), SA(0x18));
            orig::fn_004f1710(u(in + 0xc), SA(0x18), u(out + 0xc));
            orig::fn_004f1350(u(out + 0xc), SA(0x18));
            orig::fn_004f17c0(u(R + 0x18), SA(0x18), u(R + 0x18));
        }
        else
        {
            std::memcpy(out + 0xc, in + 0xc, 0x10);
        }
        CopyTail(out, in);
        return 0;
    }
    uint8_t* up = Field<uint8_t*>(track, 0x10);
    if (index == Field<uint16_t>(up, 0xa) && (legFlags & 0x20))
    {
        // the knee
        Field<uint32_t>(R, 0x24) = Field<uint32_t>(R, 0);
        const float half = Field<float>(R, 0x24) * K(0x005bcb4c);
        const double s = std::sin(static_cast<double>(half));
        SF(0x18) = static_cast<float>(s * Field<float>(R, 0x18));
        SF(0x1c) = static_cast<float>(s * Field<float>(R, 0x1c));
        SF(0x20) = static_cast<float>(s * Field<float>(R, 0x20));
        SF(0x24) = static_cast<float>(std::cos(static_cast<double>(half)));
        orig::fn_004f1710(u(in + 0xc), SA(0x18), u(out + 0xc));
        orig::fn_004f1350(u(in + 0xc), SA(0x18));
        orig::fn_004f1710(u(R + 8), SA(0x18), SA(0x3c));
        orig::fn_004f1710(u(out + 0xc), SA(0x3c), u(R + 8));
        CopyPos(out, in);
        CopyTail(out, in);
        return 0;
    }
    up = Field<uint8_t*>(up, 0x10);
    if (index == Field<uint16_t>(up, 0xa) && (legFlags & 0x20))
    {
        // the hip: two-bone solve
        orig::fn_004e3370(SA(0x70), u(in));
        if (parent)
        {
            const uint32_t pm = Field<uint32_t>(skel, 0x18) + Field<uint16_t>(parent, 0xa) * 0x48;
            orig::fn_004e2910(SA(0x70), SA(0xb8), pm);
        }
        else
        {
            std::memcpy(S + 0xb8, S + 0x70, 0x44);
        }
        orig::fn_004e2910(SA(0xb8), SA(0x148), SA(0x1d8));
        uint32_t t = orig::fn_004ebf90(u(skel), Field<uint32_t>(R, 4), Field<uint32_t>(bone, 0x18));
        orig::fn_004e3370(SA(0x70), t);
        t = orig::fn_004ebf90(u(skel), Field<uint32_t>(Field<uint8_t*>(R, 4), 0x10), Field<uint32_t>(bone, 0x18));
        orig::fn_004e3370(SA(0x100), t);
        orig::fn_004e2910(SA(0x70), SA(0xb8), SA(0x100));
        SU(0x60) = SU(0xc4);
        SU(0x64) = SU(0xd4);
        SU(0x68) = SU(0xe4);
        SU(0x18) = SU(0x10c);
        SU(0x1c) = SU(0x11c);
        SU(0x20) = SU(0x12c);
        orig::fn_004e36a0(SA(0x60), SA(0x148), 1, SA(0x54));
        const uint32_t leg = SU(0x28);
        const uint32_t act = SU(0x10);
        fn_00472c90(leg, reinterpret_cast<uint32_t*>(S + 0x3c), B(act), reinterpret_cast<uint32_t*>(S + 0x14));
        SF(0x44) = SF(0x5c) + SF(0x14);
        SU(0x40) = SU(0x58);
        SU(0x3c) = SU(0x54);
        orig::fn_004e2b40(SA(0x148), SA(0x190));
        orig::fn_004e36a0(SA(0x3c), SA(0x190), 1, SA(0x54));
        SF(0x10) = std::sqrt(SF(0x12c) * SF(0x12c) + SF(0x11c) * SF(0x11c) + SF(0x10c) * SF(0x10c));
        const float l2 = std::sqrt(SF(0x9c) * SF(0x9c) + SF(0x8c) * SF(0x8c) + SF(0x7c) * SF(0x7c));
        SF(0x4c) = std::sqrt(SF(0xe4) * SF(0xe4) + SF(0xd4) * SF(0xd4) + SF(0xc4) * SF(0xc4));
        SF(0x50) = std::sqrt(SF(0x5c) * SF(0x5c) + SF(0x58) * SF(0x58) + SF(0x54) * SF(0x54));
        SF(0x34) = SF(0x4c) * SF(0x4c);
        SF(0x30) = l2 * l2;
        SF(0x38) = SF(0x10) * SF(0x10);
        SF(0x2c) = l2 * SF(0x10) * K(0x005bcc40);
        SF(0xc) = (SF(0x34) - SF(0x30) - SF(0x38)) / SF(0x2c);
        ClampUnit(SF(0xc));
        SF(0x28) = static_cast<float>(CrtAcos(SF(0xc)));
        const float d2 = SF(0x50) * SF(0x50);
        SF(0x14) = d2;
        SF(0xc) = (d2 - SF(0x30) - SF(0x38)) / SF(0x2c);
        ClampUnit(SF(0xc));
        Field<float>(R, 0) = static_cast<float>(CrtAcos(SF(0xc)) - SF(0x28));
        SF(0xc) = (SF(0x30) - SF(0x34) - SF(0x38)) / (SF(0x4c) * SF(0x10) * K(0x005bcc40));
        ClampUnit(SF(0xc));
        SF(0x28) = static_cast<float>(CrtAcos(SF(0xc)));
        SF(0xc) = (SF(0x30) - SF(0x14) - SF(0x38)) / (SF(0x50) * SF(0x10) * K(0x005bcc40));
        ClampUnit(SF(0xc));
        orig::fn_004e7830(SA(0x18), SA(0x60), u(R + 0x18));
        Normalize3(u(R + 0x18), u(R + 0x18));
        Field<float>(R, 0x24) = static_cast<float>(CrtAcos(SF(0xc)) - SF(0x28));
        orig::fn_004f13d0(u(R + 0x18), u(R + 8));
        CopyPos(out, in);
        orig::fn_004f1710(u(R + 8), u(in + 0xc), u(out + 0xc));
        CopyTail(out, in);
        return 0;
    }
    if (!(legFlags & 0x10))
    {
        std::memcpy(out, in, 0x2c);
        return 0;
    }
    // other leg bones: turn onto the ground normal
    if (parent)
        std::memcpy(S + 0x70, P(Field<uint32_t>(skel, 0x18) + Field<uint16_t>(parent, 0xa) * 0x48), 0x44);
    else
    {
        std::memset(S + 0x70, 0, 0x44);
        SU(0x70) = 0x3f800000;
        SU(0x84) = 0x3f800000;
        SU(0x98) = 0x3f800000;
        SU(0xac) = 0x3f800000;
    }
    orig::fn_004e2910(SA(0x70), SA(0x190), SA(0x1d8));
    orig::fn_004e2b40(SA(0x190), SA(0x148));
    fn_00472c90(SU(0x28), reinterpret_cast<uint32_t*>(S + 0x18), B(SU(0x10)), reinterpret_cast<uint32_t*>(S + 0x14));
    SF(0x3c) = -SF(0x18);
    SF(0x40) = -SF(0x1c);
    SU(0x48) = 0;
    SF(0x44) = -SF(0x20);
    orig::fn_004e3950(SA(0x148), SA(0x18), 1, SA(0x3c));
    Normalize3(SA(0x18), SA(0x18));
    const float rx = Field<float>(R, 0x18), ry = Field<float>(R, 0x1c), rz = Field<float>(R, 0x20);
    const float len2 = rx * rx + ry * ry + rz * rz;
    float s;
    if (len2 > K(0x005bcd7c))
        s = (SF(0x20) * rz + SF(0x1c) * ry + SF(0x18) * rx) / len2;
    else
        s = K(0x0056c05c);
    SF(0x18) = SF(0x18) - s * rx;
    SU(0x3c) = 0;
    SU(0x40) = 0;
    SU(0x44) = 0xbf800000;
    SU(0x48) = 0;
    SF(0x1c) = SF(0x1c) - s * ry;
    SF(0x20) = SF(0x20) - s * rz;
    orig::fn_004e3950(SA(0x148), SA(0x60), 1, SA(0x3c));
    Normalize3(SA(0x60), SA(0x60));
    orig::fn_004f1830(SA(0x18), SA(0x60), SA(0x3c));
    CopyPos(out, in);
    orig::fn_004f1710(u(in + 0xc), SA(0x3c), u(out + 0xc));
    CopyTail(out, in);
    return 0;
}

// 0x004735f0 (cdecl; stack: out, in, bone, parent bone or 0, skeleton, flags, row; plain ret): spine/aim bone callback
// (no direct callers; not executed in recorded runs). Without an actor (skeleton+0) it returns without writing. The
// bone's local "up" axis (the bone matrix 0x004e3370 times the parent's and the actor's virtual +0x60 matrix, inverted,
// applied to (0, 0, 1) and normalised) is the rotation axis. With a row record (actor+0x68c + row*0x174, from
// 0x00472c30) and bone kind 0: record flag 2 bends towards the actor's facing (0x00453150) limited by +0x24 (scaled by
// the animation's frame progress when flag 8), clamping the stored direction (+8) and raising actor+0xa34 bit 0x2000 /
// +0x980 when the limit hits; flag 4 aims at actor+0x4d8 with CRT asin limited to +-[+0x24] and eased by Game time
// 0x00604a90 * 0x005bcda4; otherwise 0. The angle (+0x28) plus actor+0x978 (kind 0), or actor+0x97c - +0x978 - the
// record angle (other kinds), rotates the input quaternion about the axis. Frame: one 0xe0-byte array (offsets from
// esp after the four pushes).
uint32_t fn_004735f0(uint8_t* out, uint8_t* in, uint8_t* bone, uint8_t* parent, uint8_t* skel, uint32_t flags,
                     uint32_t rowValue)
{
    alignas(8) uint8_t S[0xe0];
    auto SF = [&](uint32_t o) -> float& { return *reinterpret_cast<float*>(S + o); };
    auto SU = [&](uint32_t o) -> uint32_t& { return *reinterpret_cast<uint32_t*>(S + o); };
    auto SA = [&](uint32_t o) { return u(S + o); };
    uint8_t* actor = Field<uint8_t*>(skel, 0);
    SU(0x28) = u(actor);
    if (!actor)
        return 0;
    const uint32_t kind =
        Field<uint32_t>(Field<uint8_t*>(Field<uint8_t*>(skel, 0x14) + Field<uint16_t>(bone, 0xa) * 8, 4), 8);
    SU(0x10) = kind;
    uint8_t* rec = nullptr;
    if (fn_00472c30(flags, reinterpret_cast<int32_t*>(S + 0x48), bone, actor, reinterpret_cast<int32_t*>(S + 0x14),
                    rowValue) &
        0xff)
        rec = actor + SU(0x14) * 0x174 + 0x68c;
    orig::fn_004e3370(SA(0x58), u(in));
    if (parent)
        orig::fn_004e2910(SA(0x58), SA(0x58), Field<uint32_t>(skel, 0x18) + Field<uint16_t>(parent, 0xa) * 0x48);
    const uint32_t am = VCall<uint32_t>(actor, 0x60 / 4);
    orig::fn_004e2910(SA(0x58), SA(0x9c), am);
    orig::fn_004e2b40(SA(0x9c), SA(0x58));
    SU(0x38) = 0;
    SU(0x3c) = 0;
    SU(0x40) = 0x3f800000;
    SU(0x44) = 0;
    orig::fn_004e3950(SA(0x58), SA(0x18), 1, SA(0x38));
    Normalize3(SA(0x18), SA(0x38));
    float total;
    if (SU(0x10) != 0)
    {
        total = Field<float>(actor, 0x97c) - Field<float>(actor, 0x978);
        if (rec)
            total = total - Field<float>(rec, 0x28);
    }
    else
    {
        SF(0x44) = Field<float>(actor, 0x978);
        if (!rec)
        {
            total = SF(0x44);
        }
        else if (Field<uint32_t>(rec, 4) & 2)
        {
            orig::fn_00453150(u(actor), SA(0x2c));
            uint8_t* e = rec + 8;
            orig::fn_004e7830(SA(0x2c), u(e), SA(0x18));
            const float d = SF(0x34) * Field<float>(e, 8) + SF(0x30) * Field<float>(e, 4) + SF(0x2c) * Field<float>(e, 0);
            SF(0x10) = d;
            bool negate = false;
            if (d > K(0x005bcb2c))
                SU(0x10) = 0x3f800000;
            else if (d < K(0x005bcb5c))
            {
                SU(0x10) = 0xbf800000;
                negate = true;
            }
            else if (d < K(0x005bcd14))
                negate = true;
            if (negate)
            {
                SF(0x10) = -SF(0x10);
                SF(0x18) = -SF(0x18);
                SF(0x1c) = -SF(0x1c);
                SF(0x20) = -SF(0x20);
            }
            const double a = CrtAcos(SF(0x10));  // stays unrounded on the x87 stack (0x004737ea..0x00473877)
            float lim = Field<float>(rec, 0x24);
            if (Field<uint8_t>(rec, 4) & 8)
            {
                const int32_t idx = Field<int32_t>(rec, 0x2c) * 7 + Field<int32_t>(rec, 0x30);
                uint8_t* anim = Field<uint8_t*>(bone, 0xc);
                float frac;
                if (GameGlobal<uint8_t>(0x005cb658 + idx * 0x28) & 0x40)
                {
                    const int32_t num = Field<int32_t>(anim, 0x14) - Field<int32_t>(bone, 0x18);
                    const int32_t den = Field<int32_t>(anim, 0x14) - Field<int32_t>(anim, 0x18);
                    frac = static_cast<float>(num) / static_cast<float>(den);
                }
                else
                {
                    const int32_t num = Field<int32_t>(anim, 0x18) - Field<int32_t>(bone, 0x18);
                    const int32_t den = Field<int32_t>(anim, 0x18) - Field<int32_t>(anim, 0x14);
                    frac = static_cast<float>(num) / static_cast<float>(den);
                }
                lim = lim * frac;
            }
            float angle = static_cast<float>(a);
            if (a > lim)
            {
                SF(0x14) = static_cast<float>(a - lim) + K(0x005bcd3c);
                angle = static_cast<float>(a - SF(0x14));
                SF(0x48) = angle;
                const float sA = std::sqrt(K(0x005bcb2c) - SF(0x10) * SF(0x10));
                SF(0x10) = sA;
                if (!(sA == K(0x0056c05c)))
                {
                    const float h = SF(0x14) * K(0x005bcb4c);
                    const float k = static_cast<float>(std::sin(static_cast<double>(h)) / SF(0x10));
                    SF(0x18) = SF(0x18) * k;
                    SF(0x1c) = SF(0x1c) * k;
                    SF(0x20) = k * SF(0x20);
                    SF(0x24) = static_cast<float>(std::cos(static_cast<double>(h)));
                    orig::fn_004f17c0(u(e), SA(0x18), u(e));
                    Field<uint32_t>(rec, 0x10) = 0;
                    Normalize3(u(e), u(e));
                    angle = SF(0x48);
                }
                if (!Field<uint8_t>(rec, 0) && !(Field<uint8_t>(rec, 4) & 8))
                {
                    Field<uint32_t>(actor, 0xa34) |= 0x2000;
                    Field<uint32_t>(actor, 0x980) = SU(0x20);
                }
            }
            if (SF(0x20) > K(0x0056c05c))
                angle = -angle;
            Field<float>(rec, 0x28) = angle;
            Field<uint8_t>(rec, 0) = 0;
            total = SF(0x44) + Field<float>(rec, 0x28);
        }
        else if (Field<uint32_t>(rec, 4) & 4)
        {
            const uint32_t m = VCall<uint32_t>(actor, 0x60 / 4);
            orig::fn_004e37f0(u(rec + 8), m, 1, SA(0x4c), 0xffffffefu);
            Normalize3(u(actor + 0x4d8), SA(0x2c));
            orig::fn_004e7830(SA(0x2c), SA(0x4c), SA(0x18));
            SF(0x10) = Length3(SA(0x18));
            if (SF(0x10) < K(0x005bcb5c))
                SU(0x10) = 0xbf800000;
            else if (SF(0x10) > K(0x005bcb2c))
                SU(0x10) = 0x3f800000;
            double a = CrtAsin(SF(0x10));  // unrounded on the x87 stack until stored (0x004739e2..0x00473aa7)
            if (a > Field<float>(rec, 0x24))
                a = Field<float>(rec, 0x24);
            SF(0x14) = -Field<float>(rec, 0x24);
            if (a < SF(0x14))
                a = SF(0x14);
            const float d = SF(0x34) * SF(0x54) + SF(0x30) * SF(0x50) + SF(0x2c) * SF(0x4c);
            const uint32_t c1 = d < K(0x0056c05c) ? 1u : 0u;
            const uint32_t c2 = SF(0x20) < K(0x0056c05c) ? 1u : 0u;
            if (c1 ^ c2)
                a = -a;
            if (Field<uint8_t>(rec, 0))
            {
                Field<float>(rec, 0x28) = static_cast<float>(a);
                Field<uint8_t>(rec, 0) = 0;
            }
            else
            {
                float r = GameGlobal<float>(0x00604a90) * K(0x005bcda4);
                if (r > K(0x005bcb2c))
                    r = K(0x005bcb2c);
                Field<uint8_t>(rec, 0) = 0;
                Field<float>(rec, 0x28) = static_cast<float>(a - Field<float>(rec, 0x28)) * r + Field<float>(rec, 0x28);
            }
            total = SF(0x44) + Field<float>(rec, 0x28);
        }
        else
        {
            Field<uint32_t>(rec, 0x28) = 0;
            total = SF(0x44) + Field<float>(rec, 0x28);
            Field<uint8_t>(rec, 0) = 0;
        }
    }
    const float half = total * K(0x005bcb4c);
    const double s = std::sin(static_cast<double>(half));
    SF(0x18) = static_cast<float>(SF(0x38) * s);
    SF(0x1c) = static_cast<float>(SF(0x3c) * s);
    SF(0x20) = static_cast<float>(SF(0x40) * s);
    SF(0x24) = static_cast<float>(std::cos(static_cast<double>(half)));
    orig::fn_004f1710(SA(0x18), u(in + 0xc), u(out + 0xc));
    CopyPos(out, in);
    CopyTail(out, in);
    return 0;
}

// ================================================================================================================
// ABI adapters, entry stubs and hook registrations (parameters: register inputs eax ecx edx ebx esi edi, then stack)
// ================================================================================================================

static uint32_t fn_00408820_Abi(uint32_t /*ecx*/, uint32_t a0, uint32_t a1)
{
    return fn_00408820(B(a0), bf(a1));
}
static uint32_t AnimSlot_Set_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                 uint32_t a4, uint32_t a5)
{
    return AnimSlot_Set(eax, ecx, B(a0), a1, bf(a2), a3, bf(a4), B(a5));
}
static uint32_t fn_00408c70_Abi(uint32_t eax, uint32_t edx, uint32_t ebx, uint32_t esi, uint32_t /*ebp*/, uint32_t a0,
                                uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return fn_00408c70(eax, edx, ebx, B(esi), a0, a1, a2, bf(a3), bf(a4), bf(a5));
}
static uint32_t Anim_Set_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                             uint32_t a4, uint32_t a5)
{
    return Anim_Set(eax, B(esi), a0, a1, a2, a3, a4, a5);
}
static uint32_t fn_00408e30_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2,
                                uint32_t a3, uint32_t a4)
{
    return fn_00408e30(eax, B(ecx), static_cast<int32_t>(edx), a0, a1, a2, a3, static_cast<uint8_t>(a4));
}
static uint32_t fn_00408ee0_Abi(uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00408ee0(B(edx), B(a0), B(a1), static_cast<int32_t>(a2));
}
static float fn_00409d90_Abi(uint32_t edi, uint32_t a0)
{
    return fn_00409d90(B(edi), B(a0));
}
static uint32_t fn_00409ec0_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_00409ec0(B(eax), bf(a0), static_cast<uint8_t>(a1));
}
static uint32_t fn_0040a200_Abi(uint32_t a0)
{
    return fn_0040a200(B(a0));
}
static uint32_t fn_0040a3f0_Abi(uint32_t eax, uint32_t /*ecx*/)
{
    return fn_0040a3f0(B(eax));
}
static uint32_t fn_0040abc0_Abi(uint32_t ecx, uint32_t /*ebp*/)
{
    return fn_0040abc0(B(ecx));
}
static uint32_t fn_00410ec0_Abi(uint32_t a0)
{
    return fn_00410ec0(B(a0));
}
static uint32_t fn_00426290_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00426290(B(ecx), B(a0));
}
static uint32_t fn_004266b0_Abi(uint32_t edi)
{
    return fn_004266b0(B(edi));
}
static uint32_t fn_00434930_Abi(uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1)
{
    return fn_00434930(B(ecx), edx, B(a0), a1);
}
static uint32_t fn_0043a9d0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_0043a9d0(a0, a1);
}
static uint32_t fn_0043a9e0_Abi(uint32_t a0)
{
    return fn_0043a9e0(a0);
}
static uint32_t fn_0043aa10_Abi(uint32_t a0)
{
    return fn_0043aa10(a0);
}
static uint32_t fn_0043aaa0_Abi(uint32_t a0)
{
    return fn_0043aaa0(a0);
}
static uint32_t fn_0043ab40_Abi(uint32_t a0, uint32_t a1)
{
    return fn_0043ab40(a0, reinterpret_cast<uint32_t*>(a1));
}
static uint32_t fn_0043abc0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043abc0(B(ecx), a0);
}
static uint32_t fn_0043b110_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043b110(B(ecx), a0);
}
static uint32_t fn_0043b610_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043b610(B(ecx), a0);
}
static uint32_t fn_0043bcf0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_0043bcf0(a0, reinterpret_cast<uint32_t*>(a1));
}
static uint32_t fn_0043bd70_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043bd70(B(ecx), a0);
}
static uint32_t fn_0043c330_Abi(uint32_t a0)
{
    return fn_0043c330(a0);
}
static uint32_t fn_0043c600_Abi(uint32_t a0)
{
    return fn_0043c600(a0);
}
static uint32_t fn_0043c630_Abi(uint32_t a0)
{
    return fn_0043c630(a0);
}
static uint32_t fn_0043c6e0_Abi(uint32_t a0)
{
    return fn_0043c6e0(a0);
}
static uint32_t fn_0043c8a0_Abi(uint32_t a0)
{
    return fn_0043c8a0(a0);
}
static uint32_t fn_0043ca00_Abi(uint32_t a0)
{
    return fn_0043ca00(a0);
}
static uint32_t fn_0043cb40_Abi(uint32_t a0)
{
    return fn_0043cb40(a0);
}
static uint32_t fn_0044ef20_Abi(uint32_t a0)
{
    return fn_0044ef20(a0);
}
static uint32_t fn_00453100_Abi(uint32_t eax)
{
    return fn_00453100(B(eax));
}
static uint32_t fn_00453120_Abi(uint32_t eax, uint32_t edx, uint32_t a0)
{
    return fn_00453120(B(eax), edx, a0);
}
static uint32_t fn_00453c50_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                uint32_t a6, uint32_t a7, uint32_t a8)
{
    return fn_00453c50(B(a0), a1, a2, static_cast<uint8_t>(a3), B(a4), a5, static_cast<uint8_t>(a6), a7,
                       static_cast<uint8_t>(a8));
}
static uint32_t fn_004543f0_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004543f0(eax, B(ecx), edx, static_cast<int32_t>(a0), static_cast<uint8_t>(a1), a2);
}
static uint32_t fn_00458040_Abi(uint32_t esi, uint32_t edi)
{
    return fn_00458040(B(esi), reinterpret_cast<float*>(edi));
}
static uint32_t fn_004580a0_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t a0, uint32_t a1)
{
    return fn_004580a0(reinterpret_cast<float*>(eax), B(ecx), reinterpret_cast<const float*>(ebx), bf(a0),
                       static_cast<uint8_t>(a1));
}
static uint32_t fn_00458600_Abi(uint32_t esi)
{
    return fn_00458600(B(esi));
}
static uint32_t fn_00458660_Abi(uint32_t esi, uint32_t edi)
{
    return fn_00458660(static_cast<int32_t>(esi), B(edi));
}
static uint32_t fn_004586a0_Abi(uint32_t edx, uint32_t edi)
{
    return fn_004586a0(edx, B(edi));
}
static uint32_t fn_00458720_Abi(uint32_t eax, uint32_t edx)
{
    return fn_00458720(B(eax), edx);
}
static uint32_t fn_00458760_Abi(uint32_t ebx)
{
    return fn_00458760(B(ebx));
}
static uint32_t fn_004588f0_Abi(uint32_t ecx, uint32_t esi)
{
    return fn_004588f0(reinterpret_cast<const char*>(ecx), reinterpret_cast<char*>(esi));
}
static uint32_t fn_00458950_Abi(uint32_t edx, uint32_t a0)
{
    return fn_00458950(edx, B(a0));
}
static uint32_t AnimPlayer_GetClass_Abi()
{
    return AnimPlayer_GetClass();
}
static uint32_t fn_00472c30_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return fn_00472c30(eax, reinterpret_cast<int32_t*>(ecx), B(ebx), B(esi), reinterpret_cast<int32_t*>(edi), a0);
}
static uint32_t fn_00472c90_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return fn_00472c90(eax, reinterpret_cast<uint32_t*>(ecx), B(edx), reinterpret_cast<uint32_t*>(a0));
}
static uint32_t fn_00472cc0_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                uint32_t a6)
{
    return fn_00472cc0(B(a0), B(a1), B(a2), B(a3), B(a4), a5, a6);
}
static uint32_t fn_004735f0_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                uint32_t a6)
{
    return fn_004735f0(B(a0), B(a1), B(a2), B(a3), B(a4), a5, a6);
}
static uint32_t fn_00475d50_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00475d50(B(esi), bf(a0), bf(a1), bf(a2), bf(a3));
}
static uint32_t fn_00475f50_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/,
                                uint32_t /*a3*/)
{
    return fn_00475f50(eax, ecx, B(a0));
}
static float fn_00475fc0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00475fc0(eax, B(ecx));
}
static uint32_t fn_00477570_Abi(uint32_t esi, uint32_t a0)
{
    return fn_00477570(B(esi), bf(a0));
}
static uint32_t fn_004876c0_Abi(uint32_t a0)
{
    return fn_004876c0(a0);
}
static uint32_t fn_00493ca0_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_00493ca0(B(eax), bf(a0), a1);
}

ABI_ENTRY_00408820(fn_00408820_Entry, fn_00408820_Abi)
ABI_ENTRY_00408a70(AnimSlot_Set_Entry, AnimSlot_Set_Abi)
ABI_ENTRY_00408c70(fn_00408c70_Entry, fn_00408c70_Abi)
ABI_ENTRY_00408d90(Anim_Set_Entry, Anim_Set_Abi)
ABI_ENTRY_00408e30(fn_00408e30_Entry, fn_00408e30_Abi)
ABI_ENTRY_00408ee0(fn_00408ee0_Entry, fn_00408ee0_Abi)
ABI_ENTRY_00409d90(fn_00409d90_Entry, fn_00409d90_Abi)
ABI_ENTRY_00409ec0(fn_00409ec0_Entry, fn_00409ec0_Abi)
ABI_ENTRY_0040a200(fn_0040a200_Entry, fn_0040a200_Abi)
ABI_ENTRY_0040a3f0(fn_0040a3f0_Entry, fn_0040a3f0_Abi)
ABI_ENTRY_0040abc0(fn_0040abc0_Entry, fn_0040abc0_Abi)
ABI_ENTRY_00410ec0(fn_00410ec0_Entry, fn_00410ec0_Abi)
ABI_ENTRY_00426290(fn_00426290_Entry, fn_00426290_Abi)
ABI_ENTRY_004266b0(fn_004266b0_Entry, fn_004266b0_Abi)
ABI_ENTRY_00434930(fn_00434930_Entry, fn_00434930_Abi)
ABI_ENTRY_0043a9d0(fn_0043a9d0_Entry, fn_0043a9d0_Abi)
ABI_ENTRY_0043a9e0(fn_0043a9e0_Entry, fn_0043a9e0_Abi)
ABI_ENTRY_0043aa10(fn_0043aa10_Entry, fn_0043aa10_Abi)
ABI_ENTRY_0043aaa0(fn_0043aaa0_Entry, fn_0043aaa0_Abi)
ABI_ENTRY_0043ab40(fn_0043ab40_Entry, fn_0043ab40_Abi)
ABI_ENTRY_0043abc0(fn_0043abc0_Entry, fn_0043abc0_Abi)
ABI_ENTRY_0043b110(fn_0043b110_Entry, fn_0043b110_Abi)
ABI_ENTRY_0043b610(fn_0043b610_Entry, fn_0043b610_Abi)
ABI_ENTRY_0043bcf0(fn_0043bcf0_Entry, fn_0043bcf0_Abi)
ABI_ENTRY_0043bd70(fn_0043bd70_Entry, fn_0043bd70_Abi)
ABI_ENTRY_0043c330(fn_0043c330_Entry, fn_0043c330_Abi)
ABI_ENTRY_0043c600(fn_0043c600_Entry, fn_0043c600_Abi)
ABI_ENTRY_0043c630(fn_0043c630_Entry, fn_0043c630_Abi)
ABI_ENTRY_0043c6e0(fn_0043c6e0_Entry, fn_0043c6e0_Abi)
ABI_ENTRY_0043c8a0(fn_0043c8a0_Entry, fn_0043c8a0_Abi)
ABI_ENTRY_0043ca00(fn_0043ca00_Entry, fn_0043ca00_Abi)
ABI_ENTRY_0043cb40(fn_0043cb40_Entry, fn_0043cb40_Abi)
ABI_ENTRY_0044ef20(fn_0044ef20_Entry, fn_0044ef20_Abi)
ABI_ENTRY_00453100(fn_00453100_Entry, fn_00453100_Abi)
ABI_ENTRY_00453120(fn_00453120_Entry, fn_00453120_Abi)
ABI_ENTRY_00453c50(fn_00453c50_Entry, fn_00453c50_Abi)
ABI_ENTRY_004543f0(fn_004543f0_Entry, fn_004543f0_Abi)
ABI_ENTRY_00458040(fn_00458040_Entry, fn_00458040_Abi)
ABI_ENTRY_004580a0(fn_004580a0_Entry, fn_004580a0_Abi)
ABI_ENTRY_00458600(fn_00458600_Entry, fn_00458600_Abi)
ABI_ENTRY_00458660(fn_00458660_Entry, fn_00458660_Abi)
ABI_ENTRY_004586a0(fn_004586a0_Entry, fn_004586a0_Abi)
ABI_ENTRY_00458720(fn_00458720_Entry, fn_00458720_Abi)
ABI_ENTRY_00458760(fn_00458760_Entry, fn_00458760_Abi)
ABI_ENTRY_004588f0(fn_004588f0_Entry, fn_004588f0_Abi)
ABI_ENTRY_00458950(fn_00458950_Entry, fn_00458950_Abi)
ABI_ENTRY_00461d80(AnimPlayer_GetClass_Entry, AnimPlayer_GetClass_Abi)
ABI_ENTRY_00472c30(fn_00472c30_Entry, fn_00472c30_Abi)
ABI_ENTRY_00472c90(fn_00472c90_Entry, fn_00472c90_Abi)
ABI_ENTRY_00472cc0(fn_00472cc0_Entry, fn_00472cc0_Abi)
ABI_ENTRY_004735f0(fn_004735f0_Entry, fn_004735f0_Abi)
ABI_ENTRY_00475d50(fn_00475d50_Entry, fn_00475d50_Abi)
ABI_ENTRY_00475f50(fn_00475f50_Entry, fn_00475f50_Abi)
ABI_ENTRY_00475fc0(fn_00475fc0_Entry, fn_00475fc0_Abi)
ABI_ENTRY_00477570(fn_00477570_Entry, fn_00477570_Abi)
ABI_ENTRY_004876c0(fn_004876c0_Entry, fn_004876c0_Abi)
ABI_ENTRY_00493ca0(fn_00493ca0_Entry, fn_00493ca0_Abi)

HOOK(0x00408820, "fn_00408820", fn_00408820_Entry, nullptr);
HOOK(0x00408a70, "AnimSlot_Set", AnimSlot_Set_Entry, nullptr);
HOOK(0x00408c70, "fn_00408c70", fn_00408c70_Entry, nullptr);
HOOK(0x00408d90, "Anim_Set", Anim_Set_Entry, nullptr);
HOOK(0x00408e30, "fn_00408e30", fn_00408e30_Entry, nullptr);
HOOK(0x00408ee0, "fn_00408ee0", fn_00408ee0_Entry, nullptr);
HOOK(0x00409d90, "fn_00409d90", fn_00409d90_Entry, nullptr);
HOOK(0x00409ec0, "fn_00409ec0", fn_00409ec0_Entry, nullptr);
HOOK(0x0040a200, "fn_0040a200", fn_0040a200_Entry, nullptr);
HOOK(0x0040a3f0, "fn_0040a3f0", fn_0040a3f0_Entry, nullptr);
HOOK(0x0040abc0, "fn_0040abc0", fn_0040abc0_Entry, nullptr);
HOOK(0x00410ec0, "fn_00410ec0", fn_00410ec0_Entry, nullptr);
HOOK(0x00426290, "fn_00426290", fn_00426290_Entry, nullptr);
HOOK(0x004266b0, "fn_004266b0", fn_004266b0_Entry, nullptr);
HOOK(0x00434930, "fn_00434930", fn_00434930_Entry, nullptr);
HOOK(0x0043a9d0, "fn_0043a9d0", fn_0043a9d0_Entry, nullptr);
HOOK(0x0043a9e0, "fn_0043a9e0", fn_0043a9e0_Entry, nullptr);
HOOK(0x0043aa10, "fn_0043aa10", fn_0043aa10_Entry, nullptr);
HOOK(0x0043aaa0, "fn_0043aaa0", fn_0043aaa0_Entry, nullptr);
HOOK(0x0043ab40, "fn_0043ab40", fn_0043ab40_Entry, nullptr);
HOOK(0x0043abc0, "fn_0043abc0", fn_0043abc0_Entry, nullptr);
HOOK(0x0043b110, "fn_0043b110", fn_0043b110_Entry, nullptr);
HOOK(0x0043b610, "fn_0043b610", fn_0043b610_Entry, nullptr);
HOOK(0x0043bcf0, "fn_0043bcf0", fn_0043bcf0_Entry, nullptr);
HOOK(0x0043bd70, "fn_0043bd70", fn_0043bd70_Entry, nullptr);
HOOK(0x0043c330, "fn_0043c330", fn_0043c330_Entry, nullptr);
HOOK(0x0043c600, "fn_0043c600", fn_0043c600_Entry, nullptr);
HOOK(0x0043c630, "fn_0043c630", fn_0043c630_Entry, nullptr);
HOOK(0x0043c6e0, "fn_0043c6e0", fn_0043c6e0_Entry, nullptr);
HOOK(0x0043c8a0, "fn_0043c8a0", fn_0043c8a0_Entry, nullptr);
HOOK(0x0043ca00, "fn_0043ca00", fn_0043ca00_Entry, nullptr);
HOOK(0x0043cb40, "fn_0043cb40", fn_0043cb40_Entry, nullptr);
HOOK(0x0044ef20, "fn_0044ef20", fn_0044ef20_Entry, nullptr);
HOOK(0x00453100, "fn_00453100", fn_00453100_Entry, nullptr);
HOOK(0x00453120, "fn_00453120", fn_00453120_Entry, nullptr);
HOOK(0x00453c50, "fn_00453c50", fn_00453c50_Entry, nullptr);
HOOK(0x004543f0, "fn_004543f0", fn_004543f0_Entry, nullptr);
HOOK(0x00458040, "fn_00458040", fn_00458040_Entry, nullptr);
HOOK(0x004580a0, "fn_004580a0", fn_004580a0_Entry, nullptr);
HOOK(0x00458600, "fn_00458600", fn_00458600_Entry, nullptr);
HOOK(0x00458660, "fn_00458660", fn_00458660_Entry, nullptr);
HOOK(0x004586a0, "fn_004586a0", fn_004586a0_Entry, nullptr);
HOOK(0x00458720, "fn_00458720", fn_00458720_Entry, nullptr);
HOOK(0x00458760, "fn_00458760", fn_00458760_Entry, nullptr);
HOOK(0x004588f0, "fn_004588f0", fn_004588f0_Entry, nullptr);
HOOK(0x00458950, "fn_00458950", fn_00458950_Entry, nullptr);
HOOK(0x00461d80, "AnimPlayer_GetClass", AnimPlayer_GetClass_Entry, nullptr);
HOOK(0x00472c30, "fn_00472c30", fn_00472c30_Entry, nullptr);
HOOK(0x00472c90, "fn_00472c90", fn_00472c90_Entry, nullptr);
HOOK(0x00472cc0, "fn_00472cc0", fn_00472cc0_Entry, nullptr);
HOOK(0x004735f0, "fn_004735f0", fn_004735f0_Entry, nullptr);
HOOK(0x00475d50, "fn_00475d50", fn_00475d50_Entry, nullptr);
HOOK(0x00475f50, "fn_00475f50", fn_00475f50_Entry, nullptr);
HOOK(0x00475fc0, "fn_00475fc0", fn_00475fc0_Entry, nullptr);
HOOK(0x00477570, "fn_00477570", fn_00477570_Entry, nullptr);
HOOK(0x004876c0, "fn_004876c0", fn_004876c0_Entry, nullptr);
HOOK(0x00493ca0, "fn_00493ca0", fn_00493ca0_Entry, nullptr);
