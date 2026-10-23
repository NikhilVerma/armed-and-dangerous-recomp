// Recovered: UI / HUD batch ui_01 (0x00401010-0x00465350). Static recovery 2026-10-05, compile-checked only (the game
// was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified from the disassembly. g = the Game object (0x005fe490).
#include <cmath>
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
// A float constant of the game's .rdata (0x005bcb4c = 0.5, 0x005bcbe0 = 4.0, 0x0056c05c = 0.0).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
inline uint8_t* G()
{
    return reinterpret_cast<uint8_t*>(g_game);
}
// 0x004e07f0 (ecx = heap 0x0063a210; stack: size, align?, flags, tag): the game's allocator.
inline uint32_t Alloc(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return orig::fn_004e07f0(0x0063a210u, a0, a1, a2, a3);
}
}  // namespace

namespace
{
// 0x004e7880 (ecx, edx = vectors; returns st0): the generated orig wrapper only returns EAX and would leave st0 on the
// x87 stack, so it is called here and st0 stored as a float (the callers store it with fstp dword). guess: length.
float Fn004e7880(uint32_t ecxValue, uint32_t edxValue)
{
    static const uint32_t target = 0x004e7880;
    float result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, ecxValue
        mov edx, edxValue
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp dword ptr [result]
    }
    return result;
}
// The list object g+0xa84 +0xea0 (virtual +0x14 / +0x28 bracket each line-of-sight test with the tested object).
inline void* RayIgnoreList()
{
    return Field<void*>(Field<void*>(G(), 0xa84), 0xea0);
}
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t fn_004067e0(float* pos, uint32_t arg);
uint32_t fn_00408050(uint8_t* object, uint32_t arg0, const float* planes);
uint32_t fn_00408190(uint8_t* hud, uint32_t list);
uint32_t fn_004082a0(uint8_t* obj);
uint32_t fn_00408330(uint32_t index, uint32_t arg0, float distance);
uint32_t ObjectList_FindByName(const char* secondName, uint32_t arg0, const char* name);
uint32_t fn_00464ae0(uint8_t* obj);

// ================================================================================================================
// Options
// ================================================================================================================

// 0x00401010 (ecx unused (frame slot only), ebx = game): asks renderer (g+0x38) virtual +0x160 for 5, 2 and 1, builds a
// value with 0x00470c90 (eax = result(2), ecx = result(1), esi = result(5)); when it differs from g+0x4a8, stores it
// there and sets the options-changed byte g+0x4c3. Returns the value. guess: display mode index.
uint32_t fn_00401010(uint8_t* game)
{
    void* r = Field<void*>(game, 0x38);
    const uint32_t v5 = VCall<uint32_t>(r, 0x160 / 4, 5u);
    const uint32_t v2 = VCall<uint32_t>(r, 0x160 / 4, 2u);
    const uint32_t v1 = VCall<uint32_t>(r, 0x160 / 4, 1u);
    const uint32_t v = orig::fn_00470c90(v2, v1, v5);
    if (v != Field<uint32_t>(game, 0x4a8))
    {
        Field<uint32_t>(game, 0x4a8) = v;
        Field<uint8_t>(game, 0x4c3) = 1;
    }
    return v;
}

static uint32_t fn_00401010_Abi(uint32_t /*ecx*/, uint32_t ebx)
{
    return fn_00401010(B(ebx));
}
ABI_ENTRY_00401010(fn_00401010_Entry, fn_00401010_Abi)
HOOK(0x00401010, "fn_00401010", fn_00401010_Entry, nullptr);

// 0x00401ce0 Options_Apply (eax: al = initial "changed" flag, esi = game): pushes the option values of the game object
// to the subsystems: 0x00403150 (al = g+0x2684 != 0; stack: game, 0), bytes +0x4be/+0x4bf to (g+0xa7c)+4/+5,
// 0x0040f8c0 (edx = g+0xa68; stack: +0x4b0), renderer (g+0x38) virtuals +0x40 (x3 +0x4b4), +0x154 (0, +0x4bd),
// +0x154 (1, +0x4c0), +0x9c (+0x4c1 ? 3 : 1), +0x118 (4 - +0x4a0), +0x11c (+0x49c); clamps +0x4ac to virtual
// +0x98(1) (marks changed), +0x94 (+0x4ac); g+0x30 virtual +0x24 (+0x4b8 + 0.5); g+0x2c virtuals +0x88/+0x90/+0x8c
// (+0x490/+0x494/+0x498). Sets +0x4c3 when changed. Returns the +0x8c result. Name from the tracker.
uint32_t Options_Apply(uint32_t eax, uint8_t* game)
{
    const uint32_t edx = Field<uint32_t>(game, 0x2684);
    uint8_t changed = static_cast<uint8_t>(eax);
    orig::fn_00403150((eax & 0xffffff00u) | (edx != 0 ? 1u : 0u), u(game), 0);
    Field<uint8_t>(Field<void*>(game, 0xa7c), 4) = Field<uint8_t>(game, 0x4be);
    Field<uint8_t>(Field<void*>(game, 0xa7c), 5) = Field<uint8_t>(game, 0x4bf);
    orig::fn_0040f8c0(Field<uint32_t>(game, 0xa68), Field<uint32_t>(game, 0x4b0));
    const uint32_t gamma = Field<uint32_t>(game, 0x4b4);
    VCall<uint32_t>(Field<void*>(game, 0x38), 0x40 / 4, gamma, gamma, gamma);
    VCall<uint32_t>(Field<void*>(game, 0x38), 0x154 / 4, 0u, static_cast<uint32_t>(Field<uint8_t>(game, 0x4bd)));
    VCall<uint32_t>(Field<void*>(game, 0x38), 0x154 / 4, 1u, static_cast<uint32_t>(Field<uint8_t>(game, 0x4c0)));
    VCall<uint32_t>(Field<void*>(game, 0x38), 0x9c / 4, Field<uint8_t>(game, 0x4c1) != 0 ? 3u : 1u);
    VCall<uint32_t>(Field<void*>(game, 0x38), 0x118 / 4, 4u - Field<uint32_t>(game, 0x4a0));
    VCall<uint32_t>(Field<void*>(game, 0x38), 0x11c / 4, Field<uint32_t>(game, 0x49c));
    if (Field<uint32_t>(game, 0x4ac) > VCall<uint32_t>(Field<void*>(game, 0x38), 0x98 / 4, 1u))
    {
        Field<uint32_t>(game, 0x4ac) = VCall<uint32_t>(Field<void*>(game, 0x38), 0x98 / 4, 1u);
        changed = 1;
    }
    VCall<uint32_t>(Field<void*>(game, 0x38), 0x94 / 4, Field<uint32_t>(game, 0x4ac));
    const float bright = Field<float>(game, 0x4b8) + K(0x005bcb4c);
    VCall<uint32_t>(Field<void*>(game, 0x30), 0x24 / 4, bright);
    VCall<uint32_t>(Field<void*>(G(), 0x2c), 0x88 / 4, Field<uint32_t>(game, 0x490));
    VCall<uint32_t>(Field<void*>(G(), 0x2c), 0x90 / 4, Field<uint32_t>(game, 0x494));
    const uint32_t result = VCall<uint32_t>(Field<void*>(G(), 0x2c), 0x8c / 4, Field<uint32_t>(game, 0x498));
    if (changed)
        Field<uint8_t>(game, 0x4c3) = 1;
    return result;
}

static uint32_t Options_Apply_Abi(uint32_t eax, uint32_t esi)
{
    return Options_Apply(eax, B(esi));
}
ABI_ENTRY_00401ce0(Options_Apply_Entry, Options_Apply_Abi)
HOOK(0x00401ce0, "Options_Apply", Options_Apply_Entry, nullptr);

// ================================================================================================================
// HUD object (the object Hud_UpdateCrosshair 0x00406f50 works on)
// ================================================================================================================

// 0x00406690 (eax = hud; ecx unused (frame slot only)): reset. Clears +0, +0x58, +0x84, +0x2c, releases the target list
// (0x004082a0), clears two dwords in each of 4 slots of 0x2c bytes at +0x154/+0x170, releases the object in each of 6
// slots of 0x2c bytes at +0x204 (reference count +0x28; at 0 virtual +0xd8, and 0x00487da0 when it returns 0) and
// clears the slot's +0x1c, clears 0x1c dwords at +0x30c and +0x37c..+0x38c. Returns 0.
uint32_t fn_00406690(uint8_t* hud)
{
    Field<uint32_t>(hud, 0) = 0;
    Field<uint32_t>(hud, 0x58) = 0;
    Field<uint32_t>(hud, 0x84) = 0;
    Field<uint32_t>(hud, 0x2c) = 0;
    fn_004082a0(hud);
    for (uint8_t* p = hud + 0x170; p != hud + 0x170 + 4 * 0x2c; p += 0x2c)
    {
        Field<uint32_t>(p - 0x1c, 0) = 0;
        Field<uint32_t>(p, 0) = 0;
    }
    uint8_t* slot = hud + 0x204;
    for (int i = 6; i != 0; --i, slot += 0x2c)
    {
        uint8_t* obj = Field<uint8_t*>(slot, 0);
        if (obj)
        {
            if (--Field<int32_t>(obj, 0x28) == 0 && !VCall<uint8_t>(obj, 0xd8 / 4))
                orig::fn_00487da0(u(obj));
            Field<uint32_t>(slot, 0) = 0;
        }
        Field<uint32_t>(slot, 0x1c) = 0;
    }
    std::memset(hud + 0x30c, 0, 0x1c * 4);
    Field<uint32_t>(hud, 0x37c) = 0;
    Field<uint32_t>(hud, 0x380) = 0;
    Field<uint32_t>(hud, 0x384) = 0;
    Field<uint32_t>(hud, 0x388) = 0;
    Field<uint32_t>(hud, 0x38c) = 0;
    return 0;
}

static uint32_t fn_00406690_Abi(uint32_t eax, uint32_t /*ecx*/)
{
    return fn_00406690(B(eax));
}
ABI_ENTRY_00406690(fn_00406690_Entry, fn_00406690_Abi)
HOOK(0x00406690, "fn_00406690", fn_00406690_Entry, nullptr);

// 0x00406740 (eax = object): true when its byte +0x152 is set, g+0x2684 and g+0xac8 are 0, g+0x748 is set and
// 0x0040fab0 (eax = g+0xa68) returns 0. Returns al.
uint32_t fn_00406740(uint8_t* obj)
{
    if (Field<uint8_t>(obj, 0x152) && Field<uint32_t>(G(), 0x2684) == 0 && Field<uint32_t>(G(), 0xac8) == 0 &&
        Field<uint32_t>(G(), 0x748) != 0 && orig::fn_0040fab0(Field<uint32_t>(G(), 0xa68)) == 0)
        return 1;
    return 0;
}

static uint32_t fn_00406740_Abi(uint32_t eax)
{
    return fn_00406740(B(eax));
}
ABI_ENTRY_00406740(fn_00406740_Entry, fn_00406740_Abi)
HOOK(0x00406740, "fn_00406740", fn_00406740_Entry, nullptr);

// 0x00406790 (stack: one unused argument, callee pops 4): false when g+0xdce (cinematic active, per game.h) or
// g+0x2684 is set; true when 0x0040fab0 (eax = g+0xa68) returns 0; else g+0xac8 != 0. Returns al.
uint32_t fn_00406790(uint32_t /*arg*/)
{
    if (Field<uint8_t>(G(), 0xdce))
        return 0;
    if (Field<uint32_t>(G(), 0x2684))
        return 0;
    if (orig::fn_0040fab0(Field<uint32_t>(G(), 0xa68)) == 0)
        return 1;
    return Field<uint32_t>(G(), 0xac8) != 0 ? 1u : 0u;
}

static uint32_t fn_00406790_Abi(uint32_t a0)
{
    return fn_00406790(a0);
}
ABI_ENTRY_00406790(fn_00406790_Entry, fn_00406790_Abi)
HOOK(0x00406790, "fn_00406790", fn_00406790_Entry, nullptr);

// 0x004067e0 (esi = position (3 floats); stack: one unused argument, callee pops 4): when g+0xac8 is set, returns
// (g+0xacc) virtual +0x148 (pos, 0). Otherwise, when g+0x748 is set and 0x0040fab0 (eax = g+0xa68) returns 0 (the
// original relies on that call keeping ecx = g+0x748), calls (g+0x748) virtual +0x54 (pos) and adds 4.0 to pos[2].
// Returns eax as left.
uint32_t fn_004067e0(float* pos, uint32_t /*arg*/)
{
    if (Field<uint32_t>(G(), 0xac8))
        return VCall<uint32_t>(Field<void*>(G(), 0xacc), 0x148 / 4, pos, 0u);
    void* obj = Field<void*>(G(), 0x748);
    if (!obj)
        return u(G());
    const uint32_t r = orig::fn_0040fab0(Field<uint32_t>(G(), 0xa68));
    if (r)
        return r;
    const uint32_t v = VCall<uint32_t>(obj, 0x54 / 4, pos);
    pos[2] = pos[2] + K(0x005bcbe0);
    return v;
}

static uint32_t fn_004067e0_Abi(uint32_t esi, uint32_t a0)
{
    return fn_004067e0(reinterpret_cast<float*>(esi), a0);
}
ABI_ENTRY_004067e0(fn_004067e0_Entry, fn_004067e0_Abi)
HOOK(0x004067e0, "fn_004067e0", fn_004067e0_Entry, nullptr);

// ================================================================================================================
// Hud_UpdateCrosshair 0x00406f50 and the target picking around it
// ================================================================================================================

// Frame accessors for the two large functions below: the original stack frame is kept as one array and addressed by
// the body's esp offsets (four callee-saved registers pushed, so esp+0x10 is the frame start).
#define FU(o) Field<uint32_t>(frame, (o) - 0x10)
#define FI(o) Field<int32_t>(frame, (o) - 0x10)
#define FF(o) Field<float>(frame, (o) - 0x10)
#define FB(o) Field<uint8_t>(frame, (o) - 0x10)
#define FA(o) u(frame + (o) - 0x10)

// 0x00406f50 Hud_UpdateCrosshair (stack: hud, callee pops 4; caller 0x00402e80). Per frame:
// - flags from the player's (g+0x748) weapon (+0xc24): +0x152 = type (0x004a1660) 3 or g+0xac8 == 1, +0x150 = type 5,
//   +0x151 = 0x004a1670 == 10; clears the current targets; eye direction (0x0040fa10 -> local 0x4c) and position
//   (0x0040f9e0 -> local 0xa8) of the camera g+0xa68.
// - g+0xac8 0 or 1: frustum (HudFrustum_Build), eye +0x3a0 (0x004067e0), candidate list (0x00408190 of list
//   g+0xa84 +0xea4 or +0xe8c). When the weapon's virtual +0x16c is true, for at most 21 candidates inside the frustum
//   planes +0xb0, in front of the eye and in range (0x00408330): line-of-sight test 0x00414110 bracketed by
//   (g+0xa84 +0xea0) virtuals +0x14/+0x28; a hit stores the target at +0 (or, with +0x152, up to 4 targets in the
//   slots at +0x154). With +0x151 (and g+0xac8 == 0) the same against the planes +0x100 with weapon index 10 stores
//   one target at +0x84. Other g+0xac8 values release the list (0x004082a0).
// - aims a ray (0x00413cc0/0x00413d40/0x00413dc0/0x00413e10, query in local 0x5c, range 5000 or by mode/weapon) and
//   stores the hit at +0x58..+0x80, the crosshair state word +0x392 and flags +0x3ac/+0x394, the aim point +0x88 when
//   no target, updates the 6 marker slots at +0x204 (g+0xcbc), and loads the crosshair objects by name
//   (ObjectList_FindByName) into +0x30c.. and +0x37c..+0x38c ("ui_crosshair_friendly" 0x005bbbf0, ... strings of
//   recover.py). Returns the iterator node (eax as left). Name from the tracker.
uint32_t Hud_UpdateCrosshair(uint8_t* hud)
{
    alignas(4) uint8_t frame[0xa4] = {};
    auto abortIter = [&]() {
        const uint32_t node = FU(0x18);
        VCall<uint32_t>(P(FU(0x14)), 0xc / 4, node);
        --Field<int32_t>(P(node), 0x18);
        FU(0x18) = 0;
    };
    uint8_t* g = G();
    {
        uint8_t* player = Field<uint8_t*>(g, 0x748);
        Field<uint8_t>(hud, 0x152) =
            ((player && orig::fn_004a1660(u(player + 0xc24)) == 3) || Field<uint32_t>(g, 0xac8) == 1) ? 1 : 0;
        player = Field<uint8_t*>(g, 0x748);
        Field<uint8_t>(hud, 0x150) = (player && orig::fn_004a1660(u(player + 0xc24)) == 5) ? 1 : 0;
        player = Field<uint8_t*>(g, 0x748);
        Field<uint8_t>(hud, 0x151) = (player && orig::fn_004a1670(u(player + 0xc24)) == 0xa) ? 1 : 0;
    }
    Field<uint32_t>(hud, 0x58) = 0;
    Field<uint32_t>(hud, 0x2c) = 0;
    Field<uint32_t>(hud, 0x84) = 0;
    Field<uint32_t>(hud, 0xa0) = 0;
    Field<uint32_t>(hud, 0) = 0;
    Field<uint32_t>(hud, 0x1c) = 0;
    orig::fn_0040fa10(FA(0x4c), Field<uint32_t>(g, 0xa68));
    orig::fn_0040f9e0(FA(0xa8), Field<uint32_t>(g, 0xa68));
    if (Field<uint8_t>(hud, 0x152))
    {
        for (uint8_t* p = hud + 0x170; p != hud + 0x170 + 4 * 0x2c; p += 0x2c)
        {
            Field<uint32_t>(p - 0x1c, 0) = 0;
            Field<uint32_t>(p, 0) = 0;
        }
    }
    if (Field<uint32_t>(g, 0x2684))
        return Field<uint8_t>(hud, 0x152);
    const uint32_t mode = Field<uint32_t>(g, 0xac8);
    FU(0x14) = 0;
    FU(0x18) = 0;
    if (mode == 0 || mode == 1)
    {
        orig::fn_00408390(u(hud));
        fn_004067e0(reinterpret_cast<float*>(hud + 0x3a0), u(hud));
        const uint32_t source = Field<uint8_t>(hud, 0x152) ? Field<uint32_t>(Field<void*>(G(), 0xa84), 0xea4)
                                                           : Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe8c);
        const uint32_t list = fn_00408190(hud, source);
        FU(0xa4) = list;
        FU(0x58) = 0;
        FU(0x48) = 0;
        uint8_t* player = Field<uint8_t*>(G(), 0x748);
        if (player && VCall<uint8_t>(P(orig::fn_004a15f0(u(player + 0xc24))), 0x16c / 4))
        {
            uint8_t* t = B(orig::fn_004e7340(list, FA(0x14)));
            if (t)
            {
                FU(0x30) = u(hud + 0x154);
                for (; t; t = B(orig::fn_004e73c0(FA(0x14))))
                {
                    if (Field<uint8_t>(hud, 0x150))
                    {
                        if (!VCall<uint8_t>(t, 0x10c / 4))
                            continue;
                        if (!VCall<uint8_t>(t, 0x24c / 4))
                            continue;
                    }
                    if (Field<uint8_t>(hud, 0x152) && VCall<uint8_t>(t, 0x10c / 4) && !VCall<uint8_t>(t, 0x250 / 4))
                        continue;
                    const int32_t count = FI(0x48);
                    FI(0x48) = count + 1;
                    if (count > 0x14)
                    {
                        abortIter();
                        break;
                    }
                    if (!static_cast<uint8_t>(fn_00408050(t, u(hud), reinterpret_cast<float*>(hud + 0xb0))))
                        continue;
                    FB(0x13) = static_cast<uint8_t>(orig::fn_00458040(u(t), FA(0x38)));
                    FF(0x24) = FF(0x38) - Field<float>(hud, 0x3a0);
                    FF(0x28) = FF(0x3c) - Field<float>(hud, 0x3a4);
                    FF(0x2c) = FF(0x40) - Field<float>(hud, 0x3a8);
                    FF(0x20) = Fn004e7880(FA(0x24), FA(0x24));
                    float dot = FF(0x4c) * FF(0x24) + FF(0x54) * FF(0x2c);
                    dot = dot + FF(0x50) * FF(0x28);
                    if (dot < K(0x0056c05c))
                        continue;
                    uint8_t* g2 = G();
                    const uint32_t weapon = orig::fn_004a1660(Field<uint32_t>(g2, 0x748) + 0xc24);
                    if (!static_cast<uint8_t>(fn_00408330(weapon, u(hud), FF(0x20))))
                    {
                        orig::fn_004e7420(FA(0x14));
                        break;
                    }
                    FU(0x1c) = 0;
                    VCall<uint32_t>(Field<void*>(Field<void*>(g2, 0xa84), 0xea0), 0x14 / 4, t);
                    const float range = FF(0x20) + K(0x005bcb68);
                    const uint8_t hit = static_cast<uint8_t>(orig::fn_00414110(
                        u(hud + 0x3a0), FA(0x24), Field<uint32_t>(G(), 0xa8c), fb(range), FA(0x44), 0, FA(0x1c),
                        FA(0x34), 0, 0, u(RayIgnoreList()), 0, 0, 1, 2));
                    if (Field<uint8_t>(hud, 0x152))
                    {
                        if (hit && FU(0x1c) == u(t))
                        {
                            uint8_t* slot = B(FU(0x30));
                            Field<uint32_t>(slot, 0x1c) = FU(0x44);
                            Field<uint32_t>(slot, 4) = FU(0x38);
                            Field<uint32_t>(slot, 8) = FU(0x3c);
                            Field<uint32_t>(slot, 0xc) = FU(0x40);
                            Field<uint32_t>(slot, 0x24) = FU(0x34);
                            Field<uint8_t>(slot, 0x20) = FB(0x13);
                            Field<uint32_t>(slot, 0) = u(t);
                            const int32_t n = FI(0x58);
                            FI(0x58) = n + 1;
                            FU(0x30) = u(slot + 0x2c);
                            if (n >= 3)
                            {
                                VCall<uint32_t>(RayIgnoreList(), 0x28 / 4, t);
                                orig::fn_004e7420(FA(0x14));
                                break;
                            }
                        }
                        VCall<uint32_t>(RayIgnoreList(), 0x28 / 4, t);
                    }
                    else
                    {
                        if (hit && FU(0x1c) == u(t))
                        {
                            Field<uint32_t>(hud, 0x1c) = FU(0x44);
                            Field<uint32_t>(hud, 4) = FU(0x38);
                            Field<uint32_t>(hud, 8) = FU(0x3c);
                            Field<uint32_t>(hud, 0xc) = FU(0x40);
                            Field<uint32_t>(hud, 0) = u(t);
                            Field<uint8_t>(hud, 0x20) = FB(0x13);
                            Field<uint32_t>(hud, 0x24) = FU(0x34);
                            VCall<uint32_t>(RayIgnoreList(), 0x28 / 4, t);
                            orig::fn_004e7420(FA(0x14));
                            break;
                        }
                        VCall<uint32_t>(RayIgnoreList(), 0x28 / 4, t);
                    }
                }
            }
        }
        // 0x0040743e: the special weapon's (+0x151) single target.
        if (Field<uint8_t>(hud, 0x151) && Field<uint32_t>(G(), 0xac8) == 0)
        {
            FI(0x48) = 0;
            for (uint8_t* t = B(orig::fn_004e7340(FU(0xa4), FA(0x14))); t; t = B(orig::fn_004e73c0(FA(0x14))))
            {
                const int32_t count = FI(0x48);
                FI(0x48) = count + 1;
                if (count > 0x14)
                {
                    abortIter();
                    break;
                }
                if (!static_cast<uint8_t>(fn_00408050(t, u(hud), reinterpret_cast<float*>(hud + 0x100))))
                    continue;
                FB(0x13) = static_cast<uint8_t>(orig::fn_00458040(u(t), FA(0x38)));
                FF(0x24) = FF(0x38) - Field<float>(hud, 0x3a0);
                FF(0x28) = FF(0x3c) - Field<float>(hud, 0x3a4);
                FF(0x2c) = FF(0x40) - Field<float>(hud, 0x3a8);
                FF(0x20) = Fn004e7880(FA(0x24), FA(0x24));
                float dot = FF(0x4c) * FF(0x24) + FF(0x54) * FF(0x2c);
                dot = dot + FF(0x50) * FF(0x28);
                if (dot < K(0x0056c05c))
                    continue;
                if (!static_cast<uint8_t>(fn_00408330(0xa, u(hud), FF(0x20))))
                {
                    abortIter();
                    break;
                }
                FU(0x1c) = 0;
                VCall<uint32_t>(RayIgnoreList(), 0x14 / 4, t);
                const float range = FF(0x20) + K(0x005bcb68);
                const uint8_t hit = static_cast<uint8_t>(
                    orig::fn_00414110(u(hud + 0x3a0), FA(0x24), Field<uint32_t>(G(), 0xa8c), fb(range), FA(0x44), 0,
                                      FA(0x1c), FA(0x34), 0, 0, u(RayIgnoreList()), 0, 0, 1, 2));
                if (hit && FU(0x1c) == u(t))
                {
                    Field<uint32_t>(hud, 0xa0) = FU(0x20);
                    Field<uint32_t>(hud, 0x88) = FU(0x38);
                    Field<uint32_t>(hud, 0x8c) = FU(0x3c);
                    Field<uint32_t>(hud, 0x90) = FU(0x40);
                    Field<uint32_t>(hud, 0x84) = u(t);
                    Field<uint8_t>(hud, 0xa4) = FB(0x13);
                    Field<uint32_t>(hud, 0xa8) = FU(0x34);
                    VCall<uint32_t>(RayIgnoreList(), 0x28 / 4, t);
                    orig::fn_004e7420(FA(0x14));
                    break;
                }
                VCall<uint32_t>(RayIgnoreList(), 0x28 / 4, t);
            }
        }
    }
    else
    {
        fn_004082a0(hud);
    }

    // 0x00407680: the aim ray.
    orig::fn_00413cc0(FA(0x5c), 1);
    FU(0x74) = FU(0x50);
    FU(0x70) = FU(0x4c);
    FU(0x78) = FU(0x54);
    FU(0x68) = FU(0xac);
    FU(0x64) = FU(0xa8);
    FU(0x6c) = FU(0xb0);
    g = G();
    if (!static_cast<uint8_t>(orig::fn_0040fba0(Field<uint32_t>(g, 0xa68))))
    {
        FF(0x64) = FF(0x4c) * K(0x005bcbe8) + FF(0xa8);
        FF(0x68) = FF(0x50) * K(0x005bcbe8) + FF(0x68);
        FF(0x6c) = FF(0x54) * K(0x005bcbe8) + FF(0x6c);
    }
    const uint32_t mode2 = Field<uint32_t>(g, 0xac8);
    FU(0x7c) = 0x459c4000u;  // 5000.0
    if (mode2 == 1)
        FU(0x7c) = 0x44bb8000u;  // 1500.0
    else if (mode2 == 3)
        FU(0x7c) = 0x43fa0000u;  // 500.0
    else if (mode2 == 4)
        FU(0x7c) = 0x44fa0000u;  // 2000.0
    else if (Field<uint32_t>(g, 0x748))
    {
        const uint32_t weapon = Field<uint32_t>(g, 0x748) + 0xc24;
        if (orig::fn_004a1660(weapon) != 0xffffffffu)
            FF(0x7c) = Field<float>(g, orig::fn_004a1660(weapon) * 0x150 + 0x28b8);
    }
    uint32_t rayList = Field<uint32_t>(Field<void*>(g, 0xa84), 0xe9c);
    if (mode2)
        rayList = Field<uint32_t>(Field<void*>(g, 0xa84), 0xea4);
    orig::fn_00413d40(Field<uint32_t>(g, 0x748), FA(0x5c));
    orig::fn_00413dc0(rayList, FA(0x5c));
    FB(0x98) = 1;
    FU(0x9c) = 2;
    const uint8_t aimHit = static_cast<uint8_t>(orig::fn_00413e10(FA(0x5c)));
    Field<uint32_t>(hud, 0x80) = FU(0xa0);
    Field<float>(hud, 0x5c) = FF(0x80) * FF(0x70) + FF(0x64);
    Field<uint8_t>(hud, 0x79) = aimHit != 0 ? 1 : 0;
    Field<uint32_t>(hud, 0x74) = FU(0x80);
    Field<uint32_t>(hud, 0x58) = FU(0x90);
    Field<uint32_t>(hud, 0x7c) = FU(0x94);
    Field<float>(hud, 0x60) = FF(0x80) * FF(0x74) + FF(0x68);
    Field<float>(hud, 0x64) = FF(0x80) * FF(0x78) + FF(0x6c);
    Field<uint8_t>(hud, 0x3ac) = (FU(0x90) && VCall<uint8_t>(P(FU(0x90)), 0x10c / 4)) ? 1 : 0;
    Field<uint16_t>(hud, 0x392) = 0;
    uint8_t* aimed = Field<uint8_t*>(hud, 0x58);
    if (aimed)
    {
        if (static_cast<uint8_t>(
                orig::fn_00454690(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xea4), Field<uint32_t>(hud, 0x58))))
            std::memcpy(hud + 0x2c, hud + 0x58, 0xb * 4);
        if (VCall<uint8_t>(Field<void*>(hud, 0x58), 0x110 / 4))
        {
            aimed = Field<uint8_t*>(hud, 0x58);
            Field<uint16_t>(hud, 0x392) = static_cast<uint16_t>(Field<uint8_t>(aimed, 0x4d0) & 1);
        }
        else
        {
            const uint8_t r = VCall<uint8_t>(Field<void*>(hud, 0x58), 0x114 / 4);
            aimed = Field<uint8_t*>(hud, 0x58);
            if (r)
            {
                const uint32_t flags = Field<uint32_t>(aimed, 0x460);
                if (flags & 0x1000)
                {
                    const uint32_t player = Field<uint32_t>(G(), 0x748);
                    if (player && (orig::fn_004a1660(player + 0xc24) == 2 || orig::fn_004a1660(player + 0xc24) == 0))
                        Field<uint16_t>(hud, 0x392) = static_cast<uint16_t>((flags >> 13) & 1);
                    else
                        Field<uint16_t>(hud, 0x392) = 1;
                }
            }
            else
            {
                const uint32_t type = Field<uint32_t>(aimed, 0x418);
                if (type == 0x91 || type == 0x37 || type == 0x4e || type == 0x35 || type == 0x4f)
                    Field<uint16_t>(hud, 0x392) = 0;
                else
                    Field<uint16_t>(hud, 0x392) = 1;
            }
        }
    }
    uint8_t flag394 = 0;
    if (Field<uint8_t>(hud, 0x3ac) && VCall<uint8_t>(aimed, 0xe4 / 4))
        flag394 = 1;
    else if (Field<uint8_t*>(hud, 0x58) && Field<uint32_t>(Field<uint8_t*>(hud, 0x58), 0x418) == 0x35)
        flag394 = 1;
    Field<uint8_t>(hud, 0x394) = flag394;
    if (Field<uint32_t>(hud, 0) && Field<uint32_t>(G(), 0x748))
    {
        const uint32_t weapon = Field<uint32_t>(G(), 0x748) + 0xc24;
        if (orig::fn_004a1660(weapon) == 2 || orig::fn_004a1660(weapon) == 0)
        {
            if (static_cast<uint8_t>(
                    orig::fn_00454690(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xea4), Field<uint32_t>(hud, 0))))
                std::memcpy(hud + 0x2c, hud, 0xb * 4);
            if (!VCall<uint8_t>(Field<void*>(hud, 0), 0x110 / 4) && !VCall<uint8_t>(Field<void*>(hud, 0), 0x114 / 4))
                Field<uint16_t>(hud, 0x392) = 1;
        }
    }
    if (static_cast<uint8_t>(orig::fn_0044e600()))
    {
        Field<uint16_t>(hud, 0x392) =
            (Field<uint32_t>(hud, 0x58) || Field<uint32_t>(hud, 0x154) || Field<uint32_t>(hud, 0x180) ||
             Field<uint32_t>(hud, 0x1ac) || Field<uint32_t>(hud, 0x1d8))
                ? 1
                : 0;
    }
    g = G();
    if (!Field<uint32_t>(hud, 0x84))
    {
        Field<float>(hud, 0x88) = FF(0x70) * Field<float>(g, 0x35d8) + FF(0x64);
        Field<float>(hud, 0x8c) = FF(0x74) * Field<float>(g, 0x35d8) + FF(0x68);
        Field<float>(hud, 0x90) = FF(0x78) * Field<float>(g, 0x35d8) + FF(0x6c);
    }
    if (Field<uint8_t>(g, 0xcbc))
    {
        uint8_t* slot = hud + 0x204;
        for (int i = 6; i != 0; --i, slot += 0x2c)
        {
            uint8_t* obj = Field<uint8_t*>(slot, 0);
            if (!obj)
                continue;
            const uint8_t gone = static_cast<uint8_t>(orig::fn_00454780(u(obj)));
            obj = Field<uint8_t*>(slot, 0);
            if (gone)
            {
                if (--Field<int32_t>(obj, 0x28) == 0 && !VCall<uint8_t>(obj, 0xd8 / 4))
                    orig::fn_00487da0(u(obj));
                Field<uint32_t>(slot, 0) = 0;
            }
            else
            {
                orig::fn_00458040(u(obj), u(slot + 4));
            }
        }
    }
    if (!Field<uint32_t>(hud, 0x314))
    {
        uint32_t* out = reinterpret_cast<uint32_t*>(hud + 0x30c);
        for (uint32_t i = 0; i < 0x24c0; i += 0x150, ++out)
        {
            const char* name = reinterpret_cast<const char*>(G() + 0x2830 + i);
            if (*name)
                *out = ObjectList_FindByName(nullptr, Field<uint32_t>(G(), 0xa70), name);
        }
    }
    if (!Field<uint32_t>(hud, 0x37c))
        Field<uint32_t>(hud, 0x37c) = ObjectList_FindByName(nullptr, Field<uint32_t>(G(), 0xa70),
                                                            reinterpret_cast<const char*>(0x005bbbf0));
    if (!Field<uint32_t>(hud, 0x380))
        Field<uint32_t>(hud, 0x380) = ObjectList_FindByName(nullptr, Field<uint32_t>(G(), 0xa70),
                                                            reinterpret_cast<const char*>(0x005bbbd8));
    if (!Field<uint32_t>(hud, 0x384))
        Field<uint32_t>(hud, 0x384) = ObjectList_FindByName(nullptr, Field<uint32_t>(G(), 0xa70),
                                                            reinterpret_cast<const char*>(0x005bbbc4));
    if (!Field<uint32_t>(hud, 0x388))
        Field<uint32_t>(hud, 0x388) = ObjectList_FindByName(nullptr, Field<uint32_t>(G(), 0xa70),
                                                            reinterpret_cast<const char*>(0x005bbbb4));
    if (!Field<uint32_t>(hud, 0x38c))
        Field<uint32_t>(hud, 0x38c) = ObjectList_FindByName(nullptr, Field<uint32_t>(G(), 0xa70),
                                                            reinterpret_cast<const char*>(0x005bbb9c));
    if (FU(0x18))
        --Field<int32_t>(P(FU(0x18)), 0x18);
    return FU(0x18);
}

static uint32_t Hud_UpdateCrosshair_Abi(uint32_t a0)
{
    return Hud_UpdateCrosshair(B(a0));
}
ABI_ENTRY_00406f50(Hud_UpdateCrosshair_Entry, Hud_UpdateCrosshair_Abi)
HOOK(0x00406f50, "Hud_UpdateCrosshair", Hud_UpdateCrosshair_Entry, nullptr);

// 0x00407c20 (stack: hud, callee pops 4; caller 0x004788a0; not executed in recorded runs): fills the 6 marker slots
// at +0x204 (0x2c bytes: +0 object (referenced, +0x28), +4 position, +0x10/+0x14 screen position, +0x1c, +0x20, +0x24)
// with up to 6 objects of list g+0xa84 +0xe7c (through 0x00408190) whose virtual +0x118 is true, inside the frustum
// planes +0xb0 (built with +0x153 set), in range for weapon index 7 (0x00408330) and in line of sight (0x00414110);
// objects higher than the player (+20) are skipped when 0x00454be0 says so. Then projects each filled slot
// (World_ToScreen 0x004f02f0). Returns the number of slots filled.
uint32_t fn_00407c20(uint8_t* hud)
{
    alignas(4) uint8_t frame[0x54] = {};
    FU(0x2c) = 0;
    FU(0x30) = 0;
    uint8_t* slot = hud + 0x204;
    for (int i = 6; i != 0; --i, slot += 0x2c)
    {
        uint8_t* obj = Field<uint8_t*>(slot, 0);
        if (obj)
        {
            if (--Field<int32_t>(obj, 0x28) == 0 && !VCall<uint8_t>(obj, 0xd8 / 4))
                orig::fn_00487da0(u(obj));
            Field<uint32_t>(slot, 0) = 0;
        }
        Field<uint32_t>(slot, 0x1c) = 0;
    }
    Field<uint8_t>(hud, 0x153) = 1;
    orig::fn_00408390(u(hud));
    Field<uint8_t>(hud, 0x153) = 0;
    fn_004067e0(reinterpret_cast<float*>(hud + 0x3a0), u(hud));
    const uint32_t list = fn_00408190(hud, Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe7c));
    FU(0x18) = 0;
    FU(0x1c) = 0;
    slot = hud + 0x204;
    for (uint8_t* t = B(orig::fn_004e7340(list, FA(0x2c))); t; t = B(orig::fn_004e73c0(FA(0x2c))))
    {
        if (!VCall<uint8_t>(t, 0x118 / 4))
            continue;
        const int32_t count = FI(0x1c);
        FI(0x1c) = count + 1;
        if (count > 0x14)
        {
            const uint32_t node = FU(0x30);
            VCall<uint32_t>(P(FU(0x2c)), 0xc / 4, node);
            FU(0x30) = 0;
            --Field<int32_t>(P(node), 0x18);
            break;
        }
        if (!static_cast<uint8_t>(fn_00408050(t, u(hud), reinterpret_cast<float*>(hud + 0xb0))))
            continue;
        FB(0x13) = static_cast<uint8_t>(orig::fn_00458040(u(t), FA(0x40)));
        FF(0x34) = FF(0x40) - Field<float>(hud, 0x3a0);
        FF(0x38) = FF(0x44) - Field<float>(hud, 0x3a4);
        FF(0x3c) = FF(0x48) - Field<float>(hud, 0x3a8);
        float sum = FF(0x3c) * FF(0x3c) + FF(0x38) * FF(0x38);
        sum = sum + FF(0x34) * FF(0x34);
        FF(0x14) = std::sqrt(sum);
        if (FF(0x14) != K(0x0056c05c))
        {
            const float inv = K(0x005bcb2c) / FF(0x14);
            FF(0x34) = FF(0x34) * inv;
            FF(0x38) = FF(0x38) * inv;
            FF(0x3c) = FF(0x3c) * inv;
        }
        if (!static_cast<uint8_t>(fn_00408330(7, u(hud), FF(0x14))))
        {
            const uint32_t node = FU(0x30);
            VCall<uint32_t>(P(FU(0x2c)), 0xc / 4, node);
            FU(0x30) = 0;
            --Field<int32_t>(P(node), 0x18);
            break;
        }
        VCall<uint32_t>(t, 0x54 / 4, FA(0x58));
        VCall<uint32_t>(Field<void*>(G(), 0x748), 0x54 / 4, FA(0x4c));
        if (FF(0x54) + K(0x005bcbe8) < FF(0x60) && static_cast<uint8_t>(orig::fn_00454be0(u(t))))
            continue;
        FU(0x20) = 0;
        VCall<uint32_t>(RayIgnoreList(), 0x14 / 4, t);
        const float range = FF(0x14) + K(0x005bcb68);
        const uint8_t hit = static_cast<uint8_t>(orig::fn_00414110(u(hud + 0x3a0), FA(0x34),
                                                                   Field<uint32_t>(G(), 0xa8c), fb(range), FA(0x24),
                                                                   0, FA(0x20), FA(0x28), 0, 0, u(RayIgnoreList()),
                                                                   0, 0, 1, 2));
        if (hit && FU(0x20) == u(t))
        {
            Field<uint32_t>(slot, 0x1c) = FU(0x24);
            Field<uint32_t>(slot, 0) = u(t);
            ++Field<int32_t>(t, 0x28);
            Field<uint32_t>(slot, 4) = FU(0x40);
            Field<uint32_t>(slot, 8) = FU(0x44);
            Field<uint8_t>(slot, 0x20) = FB(0x13);
            Field<uint32_t>(slot, 0xc) = FU(0x48);
            Field<uint32_t>(slot, 0x24) = FU(0x28);
            const int32_t n = FI(0x18);
            slot += 0x2c;
            FI(0x18) = n + 1;
            if (n >= 5)
            {
                VCall<uint32_t>(RayIgnoreList(), 0x28 / 4, t);
                const uint32_t node = FU(0x30);
                VCall<uint32_t>(P(FU(0x2c)), 0xc / 4, node);
                FU(0x30) = 0;
                --Field<int32_t>(P(node), 0x18);
                break;
            }
        }
        VCall<uint32_t>(RayIgnoreList(), 0x28 / 4, t);
    }
    slot = hud + 0x204;
    for (int i = 6; i != 0; --i, slot += 0x2c)
    {
        if (!Field<uint32_t>(slot, 0))
            continue;
        orig::fn_004f02f0(FA(0x34), Field<uint32_t>(G(), 0x38), u(slot + 4));
        Field<uint32_t>(slot, 0x10) = FU(0x34);
        Field<uint32_t>(slot, 0x14) = FU(0x38);
        Field<uint32_t>(slot, 0x18) = 0;
    }
    Field<uint8_t>(hud, 0x153) = 0;
    if (FU(0x30))
        --Field<int32_t>(P(FU(0x30)), 0x18);
    return FU(0x18);
}

static uint32_t fn_00407c20_Abi(uint32_t a0)
{
    return fn_00407c20(B(a0));
}
ABI_ENTRY_00407c20(fn_00407c20_Entry, fn_00407c20_Abi)
HOOK(0x00407c20, "fn_00407c20", fn_00407c20_Entry, nullptr);

#undef FU
#undef FI
#undef FF
#undef FB
#undef FA

// 0x00408050 (eax = object; stack: unused, planes (4 x {x, y, z, d}); callee pops 8): bounding sphere test. Asks the
// object for a centre (virtual +0x90 (&centre, &radius, 1)) and a second sphere (virtual +0x94 (&c2, &r2, 1)); false
// when 0x00454780 (esi = object) is true or when centre lies more than (r + r2) / 2 behind any of the 4 planes.
// Returns al.
uint32_t fn_00408050(uint8_t* object, uint32_t /*arg0*/, const float* planes)
{
    float local[8];  // the original frame (0x20 bytes)
    if (static_cast<uint8_t>(orig::fn_00454780(u(object))))
        return 0;
    VCall<uint32_t>(object, 0x90 / 4, &local[2], &local[1], 1u);
    VCall<uint32_t>(object, 0x94 / 4, &local[5], &local[0], 1u);
    local[1] = -((local[0] + local[1]) * K(0x005bcb4c));
    const float* p = planes;
    for (int i = 0; i < 4; ++i, p += 4)
    {
        float d = local[4] * p[2] + local[2] * p[0];
        d = d + local[3] * p[1];
        d = d + p[3];
        if (d < local[1])
            return 0;
    }
    return 1;
}

static uint32_t fn_00408050_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_00408050(B(eax), a0, reinterpret_cast<const float*>(a1));
}
ABI_ENTRY_00408050(fn_00408050_Entry, fn_00408050_Abi)
HOOK(0x00408050, "fn_00408050", fn_00408050_Entry, nullptr);

// 0x00408190 (eax = hud; stack: source list, callee pops 4): the candidate list at +0x398 is rebuilt every 20 calls
// (+0x39c counts) or when missing: released (0x004082a0), a new list (heap 0x10/0x1c/2, 0x004e6bd0 edx = 100, esi =
// block; callback 0x00408100), 0x0063a1e4 = hud + 0x3a0 (guess: sort origin for the callback), and every object of the
// source list whose virtual +0xe4 is false is added (reference +0x28, 0x004e6e00) except, in mode g+0xac8 == 1, those
// with virtual +0x114 true or type (+0x418) 0x50 / 0x55. Returns the list.
uint32_t fn_00408190(uint8_t* hud, uint32_t source)
{
    const uint32_t calls = ++Field<uint32_t>(hud, 0x39c);
    if (calls < 0x14 && Field<uint32_t>(hud, 0x398))
        return Field<uint32_t>(hud, 0x398);
    fn_004082a0(hud);
    const uint32_t block = Alloc(0x10, 0x1c, 2, 0x0058b5e8u);
    Field<uint32_t>(hud, 0x398) = block ? orig::fn_004e6bd0(0x64, block, 0x00408100u) : 0;
    GameGlobal<uint32_t>(0x0063a1e4) = u(hud + 0x3a0);
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(source, u(iterator))); obj; obj = B(orig::fn_004e73c0(u(iterator))))
    {
        if (VCall<uint8_t>(obj, 0xe4 / 4))
            continue;
        if (Field<uint32_t>(G(), 0xac8) == 1)
        {
            if (VCall<uint8_t>(obj, 0x114 / 4))
                continue;
            const uint32_t type = Field<uint32_t>(obj, 0x418);
            if (type == 0x50 || type == 0x55)
                continue;
        }
        ++Field<int32_t>(obj, 0x28);
        orig::fn_004e6e00(Field<uint32_t>(hud, 0x398), u(obj));
    }
    const uint32_t result = Field<uint32_t>(hud, 0x398);
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return result;
}

static uint32_t fn_00408190_Abi(uint32_t eax, uint32_t a0)
{
    return fn_00408190(B(eax), a0);
}
ABI_ENTRY_00408190(fn_00408190_Entry, fn_00408190_Abi)
HOOK(0x00408190, "fn_00408190", fn_00408190_Entry, nullptr);

// 0x004082a0 (ebx = hud): releases the candidate list +0x398: every object loses a reference (+0x28; at 0 virtual
// +0xd8, and 0x00487da0 when that returns 0), the list is deleted (virtual +0x10 (1)), +0x398 and the counter +0x39c
// are cleared. Returns eax as left (0 when there was no list, else the iterator node).
uint32_t fn_004082a0(uint8_t* hud)
{
    if (!Field<uint32_t>(hud, 0x398))
    {
        Field<uint32_t>(hud, 0x39c) = 0;
        return 0;
    }
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(hud, 0x398), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        if (--Field<int32_t>(obj, 0x28) == 0 && !VCall<uint8_t>(obj, 0xd8 / 4))
            orig::fn_00487da0(u(obj));
    }
    if (Field<void*>(hud, 0x398))
        VCall<uint32_t>(Field<void*>(hud, 0x398), 0x10 / 4, 1u);
    Field<uint32_t>(hud, 0x398) = 0;
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    Field<uint32_t>(hud, 0x39c) = 0;
    return iterator[1];
}

static uint32_t fn_004082a0_Abi(uint32_t ebx)
{
    return fn_004082a0(B(ebx));
}
ABI_ENTRY_004082a0(fn_004082a0_Entry, fn_004082a0_Abi)
HOOK(0x004082a0, "fn_004082a0", fn_004082a0_Entry, nullptr);

// 0x00408330 (eax = weapon index; stack: unused, distance; callee pops 8; not executed in recorded runs): range test.
// True in mode g+0xac8 != 0; false for index >= 28 or when the weapon's range (float g+0x28b8 + index * 0x150) is 0;
// else distance <= range. Returns al (eax 0/1 on the last test).
uint32_t fn_00408330(uint32_t index, uint32_t /*arg0*/, float distance)
{
    if (Field<uint32_t>(G(), 0xac8))
        return 1;
    if (index >= 0x1c)
        return 0;
    const float range = Field<float>(G(), index * 0x150 + 0x28b8);
    if (range == K(0x0056c05c))
        return 0;
    return distance <= range ? 1u : 0u;
}

static uint32_t fn_00408330_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_00408330(eax, a0, bf(a1));
}
ABI_ENTRY_00408330(fn_00408330_Entry, fn_00408330_Abi)
HOOK(0x00408330, "fn_00408330", fn_00408330_Entry, nullptr);

// ================================================================================================================
// Name entry and object helpers (0x004103d0-0x00410d70)
// ================================================================================================================

uint32_t fn_00410710(uint8_t* obj);
uint32_t fn_00410b20(uint32_t entry, uint32_t ecx, uint8_t* obj);
uint32_t fn_00410c20(uint8_t* obj, uint32_t ecx);
uint32_t fn_00410d70();
uint32_t fn_00418690(uint32_t arg0);
uint32_t fn_00464b10(uint8_t* obj);
uint32_t fn_004651a0(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);

// 0x004103d0 (ebx = object; caller 0x004107c0; not executed in recorded runs): one step of a typed name (wide
// characters at +0x4fc, length +0x4f8; +0x4c0 = the buffer). Input device g+0xa80, 0x00414b90 (edi = device; stack:
// key, delay): key 0x0e (Backspace scan code) removes the last character, key 0x1c (Enter scan code) sets the length to
// 8; both play 0x00414680 (eax = device, ecx = 0x23; guess: a UI sound) and return 1. Otherwise the key that
// (g+0x30) virtual +0x38 (0) reports, when pressed (delay 0.5) and its name (0x00415580) is a single letter 'A'..'Z'
// followed only by blanks, is appended (after the sound). Returns 0 then.
uint32_t fn_004103d0(uint8_t* obj)
{
    Field<uint32_t>(obj, 0x4c0) = u(obj + 0x4fc);
    if (static_cast<uint8_t>(orig::fn_00414b90(Field<uint32_t>(G(), 0xa80), 0xe, 0x3e800000u)))
    {
        orig::fn_00414680(Field<uint32_t>(G(), 0xa80), 0x23);
        const uint32_t length = Field<uint32_t>(obj, 0x4f8);
        if (length)
        {
            Field<uint32_t>(obj, 0x4f8) = length - 1;
            Field<uint16_t>(obj, 0x4fc + (length - 1) * 2) = 0;
        }
        return 1;
    }
    if (static_cast<uint8_t>(orig::fn_00414b90(Field<uint32_t>(G(), 0xa80), 0x1c, 0x3e800000u)))
    {
        orig::fn_00414680(Field<uint32_t>(G(), 0xa80), 0x23);
        Field<uint32_t>(obj, 0x4f8) = 8;
        return 1;
    }
    const uint32_t key = VCall<uint32_t>(Field<void*>(G(), 0x30), 0x38 / 4, 0u);
    if (!key)
        return 0;
    if (!static_cast<uint8_t>(orig::fn_00414b90(Field<uint32_t>(G(), 0xa80), key, 0x3f000000u)))
        return 0;
    orig::fn_00414680(Field<uint32_t>(G(), 0xa80), 0x23);
    const uint16_t* name = reinterpret_cast<const uint16_t*>(orig::fn_00415580(key));
    if (!name)
        return 0;
    const uint16_t c = name[0];
    if (c < 0x41 || c > 0x5a)
        return 0;
    uint32_t i = 1;
    for (uint16_t n = name[1]; n != 0; n = name[++i])
    {
        if (n != 0x20 && n != 9)
            break;
    }
    if (name[i] != 0)
        return 0;
    const uint32_t length = Field<uint32_t>(obj, 0x4f8);
    Field<uint16_t>(obj, 0x4fc + length * 2) = c;
    ++Field<uint32_t>(obj, 0x4f8);
    return 0;
}

static uint32_t fn_004103d0_Abi(uint32_t ebx)
{
    return fn_004103d0(B(ebx));
}
ABI_ENTRY_004103d0(fn_004103d0_Entry, fn_004103d0_Abi)
HOOK(0x004103d0, "fn_004103d0", fn_004103d0_Entry, nullptr);

// 0x004106b0 (esi = object; caller 0x00491b70; not executed in recorded runs): when the sign bit of virtual +0x150's
// al is set, tail-jumps to 0x00410710 (edx = object), else returns the virtual's result.
uint32_t fn_004106b0(uint8_t* obj)
{
    const uint32_t r = VCall<uint32_t>(obj, 0x150 / 4);
    if (static_cast<int8_t>(r) < 0)
        return fn_00410710(obj);
    return r;
}

static uint32_t fn_004106b0_Abi(uint32_t esi)
{
    return fn_004106b0(B(esi));
}
ABI_ENTRY_004106b0(fn_004106b0_Entry, fn_004106b0_Abi)
HOOK(0x004106b0, "fn_004106b0", fn_004106b0_Entry, nullptr);

// 0x004106d0 (ecx = object; stack: 4 arguments, callee pops 16; virtual: vtable 0x005a7328 (CLASS(Polewart)) slot
// +0x3b8; not executed in recorded runs): 0x004651a0 with the same four arguments, flags +0x474 |= 0x11400, then the
// name-entry reset 0x00410710. Returns 0.
uint32_t fn_004106d0(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    fn_004651a0(obj, a0, a1, a2, a3);
    Field<uint32_t>(obj, 0x474) = Field<uint32_t>(obj, 0x474) | 0x11400;
    return fn_00410710(obj);
}

static uint32_t fn_004106d0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004106d0(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004106d0(fn_004106d0_Entry, fn_004106d0_Abi)
HOOK(0x004106d0, "fn_004106d0", fn_004106d0_Entry, nullptr);

// 0x00410710 (edx = object): clears +0x4c0, +0x530, the length +0x4f8 and the 0x34 bytes +0x4fc..+0x52c (the
// wide-character name of 0x004103d0). Returns 0.
uint32_t fn_00410710(uint8_t* obj)
{
    Field<uint32_t>(obj, 0x4c0) = 0;
    Field<uint32_t>(obj, 0x530) = 0;
    Field<uint32_t>(obj, 0x4f8) = 0;
    Field<uint32_t>(obj, 0x4fc) = 0;
    Field<uint32_t>(obj, 0x500) = 0;
    Field<uint32_t>(obj, 0x504) = 0;
    Field<uint32_t>(obj, 0x508) = 0;
    Field<uint32_t>(obj, 0x50c) = 0;
    std::memset(obj + 0x510, 0, 8 * 4);
    return 0;
}

static uint32_t fn_00410710_Abi(uint32_t edx)
{
    return fn_00410710(B(edx));
}
ABI_ENTRY_00410710(fn_00410710_Entry, fn_00410710_Abi)
HOOK(0x00410710, "fn_00410710", fn_00410710_Entry, nullptr);

// 0x00410ad0 (ecx = object; stack: 4 arguments, callee pops 16; virtual: vtable 0x005a1348 (CLASS(PlayerWeapon)) slot
// +0x188; not executed in recorded runs): 0x004651a0 with the same arguments, +0x4f8 = 0, flags +0x474 |= 0x1400,
// clears 8 dwords at +0x4fc. Returns 0.
uint32_t fn_00410ad0(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    fn_004651a0(obj, a0, a1, a2, a3);
    Field<uint32_t>(obj, 0x4f8) = 0;
    Field<uint32_t>(obj, 0x474) |= 0x1400;
    std::memset(obj + 0x4fc, 0, 8 * 4);
    return 0;
}

static uint32_t fn_00410ad0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00410ad0(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00410ad0(fn_00410ad0_Entry, fn_00410ad0_Abi)
HOOK(0x00410ad0, "fn_00410ad0", fn_00410ad0_Entry, nullptr);

// 0x00410b20 (eax = table entry (12 bytes) or 0, ecx unused (frame slot only), esi = object; caller 0x00410be0; not
// executed in recorded runs): +0x4f8 = entry, clears 8 dwords at +0x4fc. With an entry whose mask (+8) meets g+0x48c
// or g+0x4c8, or when bit 7 of g+0x4c8 is set: 0x00465850 (ecx = entry[0], edi = object; stack: 1) and flag bit 0 of
// +0x470 set (else cleared). Flag 0x40 of +0x474 is cleared when the mask met g+0x4c8, else set. Returns the new +0x470.
uint32_t fn_00410b20(uint32_t entry, uint32_t /*ecx*/, uint8_t* obj)
{
    Field<uint32_t>(obj, 0x4f8) = entry;
    std::memset(obj + 0x4fc, 0, 8 * 4);
    uint8_t shown = 0;
    uint8_t matched = 0;
    const uint32_t e = Field<uint32_t>(obj, 0x4f8);
    if (e)
    {
        uint8_t* g = G();
        const uint32_t mask = Field<uint32_t>(P(e), 8);
        const uint32_t flags = Field<uint32_t>(g, 0x4c8);
        matched = (mask & flags) != 0 ? 1 : 0;
        if ((mask & Field<uint32_t>(g, 0x48c)) || matched || static_cast<int8_t>(flags) < 0)
        {
            orig::fn_00465850(Field<uint32_t>(P(e), 0), u(obj), 1);
            shown = 1;
        }
    }
    if (!matched)
        Field<uint32_t>(obj, 0x474) = Field<uint32_t>(obj, 0x474) | 0x40;
    else
        Field<uint32_t>(obj, 0x474) = Field<uint32_t>(obj, 0x474) & 0xffffffbfu;
    const uint32_t v = shown ? (Field<uint32_t>(obj, 0x470) | 1) : (Field<uint32_t>(obj, 0x470) & 0xfffffffeu);
    Field<uint32_t>(obj, 0x470) = v;
    return v;
}

static uint32_t fn_00410b20_Abi(uint32_t eax, uint32_t ecx, uint32_t esi)
{
    return fn_00410b20(eax, ecx, B(esi));
}
ABI_ENTRY_00410b20(fn_00410b20_Entry, fn_00410b20_Abi)
HOOK(0x00410b20, "fn_00410b20", fn_00410b20_Entry, nullptr);

// 0x00410bd0 (no inputs; virtual: vtable 0x005a1348 (CLASS(PlayerWeapon)) slot +0x2c0; not executed in recorded
// runs): returns 0x102.
uint32_t fn_00410bd0()
{
    return 0x102;
}

static uint32_t fn_00410bd0_Abi()
{
    return fn_00410bd0();
}
ABI_ENTRY_00410bd0(fn_00410bd0_Entry, fn_00410bd0_Abi)
HOOK(0x00410bd0, "fn_00410bd0", fn_00410bd0_Entry, nullptr);

// 0x00410be0 (eax = object; caller 0x00491b70; not executed in recorded runs): with n = +0x478 % 100, positions the
// object (0x00410c20) and applies entry n of the 12-byte table 0x00589090 (0 when n >= 12) with 0x00410b20.
uint32_t fn_00410be0(uint8_t* obj)
{
    const uint32_t n = Field<uint32_t>(obj, 0x478) % 100;
    const uint32_t entry = n < 0xc ? 0x00589090u + n * 12 : 0;
    fn_00410c20(obj, 100);
    return fn_00410b20(entry, 100, obj);
}

static uint32_t fn_00410be0_Abi(uint32_t eax)
{
    return fn_00410be0(B(eax));
}
ABI_ENTRY_00410be0(fn_00410be0_Entry, fn_00410be0_Abi)
HOOK(0x00410be0, "fn_00410be0", fn_00410be0_Entry, nullptr);

// 0x00410c20 (eax = object, ecx unused (frame slot only); not executed in recorded runs): 0x00464b10, then with
// n = +0x478 % 100: +0x4e8 += (n % 3 - 1) * K(0x005bcbe4) (0.25) and +0x4ec -= (n / 3 * 40 + 24) * K(0x005bcbf4) (a
// 3-column grid layout, guess); flags +0x474 |= 0x2000. Returns the new flags.
uint32_t fn_00410c20(uint8_t* obj, uint32_t /*ecx*/)
{
    fn_00464b10(obj);
    const int32_t n = static_cast<int32_t>(Field<uint32_t>(obj, 0x478) % 100);
    const int32_t column = n % 3 - 1;
    const int32_t y = n / 3 * 40 + 0x18;
    Field<float>(obj, 0x4e8) = static_cast<float>(column) * K(0x005bcbe4) + Field<float>(obj, 0x4e8);
    Field<float>(obj, 0x4ec) = Field<float>(obj, 0x4ec) - static_cast<float>(y) * K(0x005bcbf4);
    const uint32_t v = Field<uint32_t>(obj, 0x474) | 0x2000;
    Field<uint32_t>(obj, 0x474) = v;
    return v;
}

static uint32_t fn_00410c20_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00410c20(B(eax), ecx);
}
ABI_ENTRY_00410c20(fn_00410c20_Entry, fn_00410c20_Abi)
HOOK(0x00410c20, "fn_00410c20", fn_00410c20_Entry, nullptr);

// 0x00410ca0 (no inputs; caller 0x00401f40): applies the pending flags of g+0x4c8: g+0x174 = (flags & 0x21) != 0,
// g+0x175 = (flags & 0x11) != 0; bit 0x2000: 0x00418690 (g); when 0x00403bb0 (edi = g) is true: bit 0x200:
// 0x00475a30 (ecx = player g+0x748; stack: player +0x458, read here instead of through the getter 0x004555c0 which only
// does fld [eax+0x458] and leaves eax = player), bit 0x100: 0x00410d70. Then clears bits 0x2300. Returns the flags.
uint32_t fn_00410ca0()
{
    uint8_t* g = G();
    Field<uint8_t>(g, 0x174) = (Field<uint8_t>(g, 0x4c8) & 0x21) != 0 ? 1 : 0;
    Field<uint8_t>(g, 0x175) = (Field<uint8_t>(g, 0x4c8) & 0x11) != 0 ? 1 : 0;
    if (Field<uint32_t>(g, 0x4c8) & 0x2000)
    {
        fn_00418690(u(g));
        g = G();
    }
    const uint8_t ok = static_cast<uint8_t>(orig::fn_00403bb0(u(g)));
    uint8_t* game = G();
    if (ok)
    {
        if (Field<uint32_t>(game, 0x4c8) & 0x200)
        {
            const uint32_t player = Field<uint32_t>(game, 0x748);
            orig::fn_00475a30(player, Field<uint32_t>(P(player), 0x458));
        }
        if (Field<uint32_t>(game, 0x4c8) & 0x100)
            fn_00410d70();
    }
    const uint32_t v = Field<uint32_t>(game, 0x4c8) & 0xffffdcffu;
    Field<uint32_t>(game, 0x4c8) = v;
    return v;
}

static uint32_t fn_00410ca0_Abi()
{
    return fn_00410ca0();
}
ABI_ENTRY_00410ca0(fn_00410ca0_Entry, fn_00410ca0_Abi)
HOOK(0x00410ca0, "fn_00410ca0", fn_00410ca0_Entry, nullptr);

// 0x00410d70 (no inputs; caller 0x00410ca0; not executed in recorded runs): for the player's (g+0x748) weapon block
// +0xc24: each valid id (not -1) of the 3-entry table 0x004a1600 and the 5-entry table 0x004a1630 gets 0x004a13a0
// (eax = 100000, ecx = id; stack: block) (guess: refill ammunition). Returns eax as left.
uint32_t fn_00410d70()
{
    uint8_t* g = G();
    uint32_t r = Field<uint32_t>(g, 0x748);
    if (!r)
        return 0;
    const uint32_t* ids = reinterpret_cast<const uint32_t*>(orig::fn_004a1600(r + 0xc24));
    for (uint32_t i = 0; i < 3; ++i)
        if (ids[i] != 0xffffffffu)
            r = orig::fn_004a13a0(0x186a0, ids[i], Field<uint32_t>(g, 0x748) + 0xc24);
    ids = reinterpret_cast<const uint32_t*>(orig::fn_004a1630(Field<uint32_t>(g, 0x748) + 0xc24));
    for (uint32_t i = 0; i < 5; ++i)
        if (ids[i] != 0xffffffffu)
            r = orig::fn_004a13a0(0x186a0, ids[i], Field<uint32_t>(g, 0x748) + 0xc24);
    return r;
}

static uint32_t fn_00410d70_Abi()
{
    return fn_00410d70();
}
ABI_ENTRY_00410d70(fn_00410d70_Entry, fn_00410d70_Abi)
HOOK(0x00410d70, "fn_00410d70", fn_00410d70_Entry, nullptr);

// 0x00415910 (edx, esi = screen, and edi = the screen name: symbols/abi.csv lists only edx and esi, but the callee
// 0x00493150 passes edi on to UiScreen_Construct 0x004916e0, which copies the string at edi to +0xc; hence the
// hand-written entry stub below): constructor of a UI screen class: 0x00493150 (eax = 0x0058a9a4, ecx = 0x300,
// edx, edi; stack: screen), clears +0x278/+0x27c/+0x280, vtable 0x005b9710. Returns the screen.
uint32_t fn_00415910(uint32_t edx, uint8_t* screen, uint32_t edi)
{
    orig::fn_00493150(0x0058a9a4u, 0x300, edx, edi, u(screen));
    Field<uint32_t>(screen, 0x278) = 0;
    Field<uint32_t>(screen, 0x27c) = 0;
    Field<uint32_t>(screen, 0x280) = 0;
    Field<uint32_t>(screen, 0) = 0x005b9710;
    return u(screen);
}

static uint32_t fn_00415910_Abi(uint32_t edx, uint32_t esi, uint32_t edi)
{
    return fn_00415910(edx, B(esi), edi);
}
static __declspec(naked) void fn_00415910_Entry()
{
    __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push edi __asm push esi
    __asm push edx __asm call fn_00415910_Abi __asm add esp, 12 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx
    __asm pop edx __asm pop ecx __asm ret
}
HOOK(0x00415910, "fn_00415910", fn_00415910_Entry, nullptr);

// ================================================================================================================
// Credits screen (vtable 0x005b9710; constructor 0x00415910)
// ================================================================================================================

uint32_t fn_00415a00(uint8_t* screen, const char* path);
uint32_t fn_00415b40(const uint16_t* text, uint16_t* out, uint32_t max, uint32_t arg0);

namespace
{
// 0x004f2030 (ebx = 0x14c-byte find record {FindFirstFile handle, WIN32_FIND_DATAA, directory}, edi = passed on to
// 0x004f1f40; stack: path, caller pops 4): locates a game file. symbols/abi.csv lists no register inputs and 6 stack
// arguments, but the code reads ebx and edi (mov ecx, ebx / push edi before each 0x004f1f40) and only [esp+0x10c]
// (one argument), so it is called here with a hand-written shim.
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
template <class Fn>
inline Fn Import(uint32_t iatSlot)
{
    return *reinterpret_cast<Fn*>(iatSlot);
}
}  // namespace

// 0x00415940 (ecx = screen; stack: unused, open flag (byte); callee pops 8; virtual: vtable 0x005b9710 slot 2; not
// executed in recorded runs): base open 0x00493320 (ecx = screen; stack: 0x005b9740, flag), +0x278 = 0. Opening:
// loads the text file 0x005b9734 into +0x280 (0x00415a00), 0x004a07a0 (eax = 0x0058eb30, ecx = g, edi = 4; stack: 1)
// (strings 'credits', 'credits.txt', 'ui_extrasmenu_credits'), counts the text's lines (0x00415b40) and stores
// (lines + K(0x005bceb0)) / line height (the text object +0x68's virtual +0x14, else K(0x005bccbc)) at +0x27c (guess:
// scroll length). Closing: frees the text (0x00415a00 (0)). Returns eax as left.
uint32_t fn_00415940(uint8_t* screen, uint32_t /*arg0*/, uint32_t open)
{
    orig::fn_00493320(u(screen), 0x005b9740u, open);
    Field<uint32_t>(screen, 0x278) = 0;
    if (!static_cast<uint8_t>(open))
        return fn_00415a00(screen, nullptr);
    fn_00415a00(screen, reinterpret_cast<const char*>(0x005b9734));
    orig::fn_004a07a0(0x0058eb30u, u(G()), 4, 1);
    float height = K(0x005bccbc);
    if (Field<void*>(screen, 0x68))
        height = VCall<float>(Field<void*>(screen, 0x68), 0x14 / 4);
    uint32_t lines = 0;
    uint32_t r = fn_00415b40(Field<const uint16_t*>(screen, 0x280), nullptr, 0, u(screen));
    while (r)
    {
        ++lines;
        r = fn_00415b40(reinterpret_cast<const uint16_t*>(r), nullptr, 0, u(screen));
    }
    float n = static_cast<float>(static_cast<int32_t>(lines));
    if (static_cast<int32_t>(lines) < 0)
        n = n + K(0x005bcb28);
    n = n + K(0x005bceb0);
    Field<float>(screen, 0x27c) = n / height;
    return r;
}

static uint32_t fn_00415940_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00415940(B(ecx), a0, a1);
}
ABI_ENTRY_00415940(fn_00415940_Entry, fn_00415940_Abi)
HOOK(0x00415940, "fn_00415940", fn_00415940_Entry, nullptr);

// 0x00415a00 (ecx = screen; stack: path, callee pops 4; not executed in recorded runs): frees the text +0x280 (heap
// 0x0063a210, 0x004e08d0). With a non-empty path: locates the file (0x004f2030, find record in the frame), FindClose,
// builds the full name (_makepath 0x0053859e from the record's directory and file name; 0x004f19d0 copies it turning
// '/' into '\\'), CreateFileA (GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING), allocates size + 2 zeroed bytes
// (0x004e07f0 (0x1a, size + 2, 2, 0)) into +0x280, ReadFile, CloseHandle; on a short read frees it again (recursion
// with 0). Returns eax as left (guess: not used).
uint32_t fn_00415a00(uint8_t* screen, const char* path)
{
    alignas(4) uint8_t frame[0x458] = {};
    if (Field<uint32_t>(screen, 0x280))
    {
        orig::fn_004e08d0(0x0063a210u, Field<uint32_t>(screen, 0x280));
        Field<uint32_t>(screen, 0x280) = 0;
    }
    if (!path || !*path)
        return 0;
    uint8_t* record = frame + 0x10c;
    if (!static_cast<uint8_t>(Fn004f2030(u(record), 0, u(path))))
        return 0;
    const uint32_t size = Field<uint32_t>(record, 0x24);
    GameFunction<void(__cdecl*)(char*, const char*, const char*, const char*, const char*)>(0x0053859e)(
        reinterpret_cast<char*>(frame + 0x354), nullptr, reinterpret_cast<const char*>(record + 0x144),
        reinterpret_cast<const char*>(record + 0x30), nullptr);
    Import<BOOL(WINAPI*)(HANDLE)>(0x0056b0dc)(Field<HANDLE>(record, 0));
    orig::fn_004f19d0(u(frame + 0x354), 0, u(frame + 8));
    const HANDLE file = Import<HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE)>(
        0x0056b0c0)(reinterpret_cast<LPCSTR>(frame + 8), 0x80000000u, 1, nullptr, 3, 0, nullptr);
    Field<HANDLE>(frame, 4) = file;
    if (file == INVALID_HANDLE_VALUE || !file)
        return u(file);
    const uint32_t block = Alloc(0x1a, size + 2, 2, 0);
    Field<uint32_t>(screen, 0x280) = block;
    std::memset(P(block), 0, size + 2);
    Import<BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED)>(0x0056b0c8)(
        file, P(Field<uint32_t>(screen, 0x280)), size, reinterpret_cast<LPDWORD>(frame), nullptr);
    uint32_t r = Import<BOOL(WINAPI*)(HANDLE)>(0x0056b084)(Field<HANDLE>(frame, 4));
    if (Field<uint32_t>(frame, 0) != size)
        r = fn_00415a00(screen, nullptr);
    return r;
}

static uint32_t fn_00415a00_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00415a00(B(ecx), reinterpret_cast<const char*>(a0));
}
ABI_ENTRY_00415a00(fn_00415a00_Entry, fn_00415a00_Abi)
HOOK(0x00415a00, "fn_00415a00", fn_00415a00_Entry, nullptr);

// 0x00415b40 (eax = wide text, edx = output or 0, edi = output capacity; stack: unused, callee pops 4; not executed in
// recorded runs): copies one line ('\n' ends it; characters below 0x20 are dropped; at most edi characters stored) to
// the output and terminates it. Returns the start of the next line, or 0 at the end of the text when nothing was
// stored.
uint32_t fn_00415b40(const uint16_t* text, uint16_t* out, uint32_t max, uint32_t /*arg0*/)
{
    uint32_t stored = 0;
    if (!text)
        return 0;
    uint16_t c = *text;
    if (c != 0)
    {
        for (;;)
        {
            ++text;
            if (c == 0xa)
                break;
            if (c >= 0x20 && out)
            {
                const uint32_t n = stored++;
                if (n < max)
                    *out++ = c;
            }
            c = *text;
            if (c == 0)
                break;
        }
    }
    if (out)
        *out = 0;
    if (c == 0 && stored == 0)
        return 0;
    return u(text);
}

static uint32_t fn_00415b40_Abi(uint32_t eax, uint32_t edx, uint32_t edi, uint32_t a0)
{
    return fn_00415b40(reinterpret_cast<const uint16_t*>(eax), reinterpret_cast<uint16_t*>(edx), edi, a0);
}
ABI_ENTRY_00415b40(fn_00415b40_Entry, fn_00415b40_Abi)
HOOK(0x00415b40, "fn_00415b40", fn_00415b40_Entry, nullptr);

// 0x00415d50 (eax: al = clear first, esi = renderer, edi = camera record; caller 0x00494b70): optional virtual +0xcc
// (0, 0, 0, 0), then virtual +0x154 (4, byte +0xe2), the three matrices +0 / +0x44 / +0x88 (virtuals +0x54 / +0x50 /
// +0x4c) and +0xcc (virtual +0x74); clears +0x228. Returns the last virtual's result.
uint32_t fn_00415d50(uint32_t eax, void* renderer, uint8_t* record)
{
    if (static_cast<uint8_t>(eax))
        VCall<uint32_t>(renderer, 0xcc / 4, 0u, 0u, 0u, 0u);
    VCall<uint32_t>(renderer, 0x154 / 4, 4u, static_cast<uint32_t>(Field<uint8_t>(record, 0xe2)));
    VCall<uint32_t>(renderer, 0x54 / 4, record);
    VCall<uint32_t>(renderer, 0x50 / 4, record + 0x44);
    VCall<uint32_t>(renderer, 0x4c / 4, record + 0x88);
    const uint32_t r = VCall<uint32_t>(renderer, 0x74 / 4, record + 0xcc);
    Field<uint32_t>(record, 0x228) = 0;
    return r;
}

static uint32_t fn_00415d50_Abi(uint32_t eax, uint32_t esi, uint32_t edi)
{
    return fn_00415d50(eax, P(esi), B(edi));
}
ABI_ENTRY_00415d50(fn_00415d50_Entry, fn_00415d50_Abi)
HOOK(0x00415d50, "fn_00415d50", fn_00415d50_Entry, nullptr);

// ================================================================================================================
// 0x00418690-0x004187e0
// ================================================================================================================

uint32_t fn_00418790(uint32_t* expected, const char* name, uint32_t edi, uint32_t arg0);
uint32_t fn_004187e0(char* path, char* copy1, char* copy2, uint32_t arg0, const char* name, uint32_t text);

// 0x00418690 (stack: unused (the caller passes g), callee pops 4; caller 0x00410ca0; not executed in recorded runs):
// for each object of list g+0xa84 +0xe70 whose virtual +0x110 is true and whose byte +0x4d0 has bit 4: virtual +0xf4
// (0x00453740 (eax = object) == 0 in the low byte, the result's upper bytes kept). Returns eax as left.
uint32_t fn_00418690(uint32_t /*arg0*/)
{
    uint32_t iterator[2] = {0, 0};
    void* level = Field<void*>(G(), 0xa84);
    if (!level)
        return 0;
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(level, 0xe70), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        if (!VCall<uint8_t>(obj, 0x110 / 4))
            continue;
        if (!(Field<uint8_t>(obj, 0x4d0) & 4))
            continue;
        const uint32_t r = orig::fn_00453740(u(obj));
        VCall<uint32_t>(obj, 0xf4 / 4, (r & 0xffffff00u) | (static_cast<uint8_t>(r) == 0 ? 1u : 0u));
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_00418690_Abi(uint32_t a0)
{
    return fn_00418690(a0);
}
ABI_ENTRY_00418690(fn_00418690_Entry, fn_00418690_Abi)
HOOK(0x00418690, "fn_00418690", fn_00418690_Entry, nullptr);

// 0x00418730 (eax = wide name; stack: a0, a1, a2, callee pops 12; caller 0x00492fd0; not executed in recorded runs):
// narrows the name into a 0x104-byte buffer (at most 0x103 characters, low bytes only, _wcslen 0x00538ea0) and
// returns 0x00418790 (ebx = a1, esi = buffer, edi = a0; stack: a2).
uint32_t fn_00418730(const uint16_t* wide, uint32_t a0, uint32_t a1, uint32_t a2)
{
    char buffer[0x104];
    const uint32_t length = GameFunction<uint32_t(__cdecl*)(const uint16_t*)>(0x00538ea0)(wide);
    uint32_t i = 0;
    uint32_t room = 0x104;
    while (i < length)
    {
        if (--room == 0)
            break;
        buffer[i] = static_cast<char>(wide[i]);
        ++i;
    }
    buffer[i] = 0;
    return fn_00418790(reinterpret_cast<uint32_t*>(a1), buffer, a0, a2);
}

static uint32_t fn_00418730_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00418730(reinterpret_cast<const uint16_t*>(eax), a0, a1, a2);
}
ABI_ENTRY_00418730(fn_00418730_Entry, fn_00418730_Abi)
HOOK(0x00418730, "fn_00418730", fn_00418730_Entry, nullptr);

// 0x00418790 (ebx = expected value or 0, esi = name, edi = passed on; stack: unused, callee pops 4): resolves the name
// with 0x004187e0 (eax = 0x104-byte local path, ecx = edx = 0; stack: edi, name, 0); with an expected value, true
// only when 0x0044a9d0 (eax = name, ecx = edi) equals it. Returns al.
uint32_t fn_00418790(uint32_t* expected, const char* name, uint32_t edi, uint32_t /*arg0*/)
{
    char path[0x104];
    if (!static_cast<uint8_t>(fn_004187e0(path, nullptr, nullptr, edi, name, 0)))
        return 0;
    if (!expected)
        return 1;
    return orig::fn_0044a9d0(u(name), edi) == *expected ? 1u : 0u;
}

static uint32_t fn_00418790_Abi(uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return fn_00418790(reinterpret_cast<uint32_t*>(ebx), reinterpret_cast<const char*>(esi), edi, a0);
}
ABI_ENTRY_00418790(fn_00418790_Entry, fn_00418790_Abi)
HOOK(0x00418790, "fn_00418790", fn_00418790_Entry, nullptr);

// 0x004187e0 (eax = path out or 0, ecx = copy out or 0, edx = copy out or 0; stack: unused, name, text out or 0; callee
// pops 12): copies the name to ecx and edx, builds the path _makepath(eax, 0, *0x0063a204 (data directory), name, 0)
// and, with a text output: Text_Lookup 0x00495f00 (eax = name, ebx = g+0xac0; stack: 1) found -> 0x00403be0 (eax =
// text, edx = output, edi = 0), else 0x004e59d0 (eax = 0x80, ecx = output, edi = name) (guess: copies the key itself).
// Returns 1.
uint32_t fn_004187e0(char* path, char* copy1, char* copy2, uint32_t /*arg0*/, const char* name, uint32_t text)
{
    if (copy1)
        std::strcpy(copy1, name);
    if (copy2)
        std::strcpy(copy2, name);
    if (path)
        GameFunction<void(__cdecl*)(char*, const char*, const char*, const char*, const char*)>(0x0053859e)(
            path, nullptr, GameGlobal<const char*>(0x0063a204), name, nullptr);
    if (!text)
        return 1;
    const uint32_t found = orig::fn_00495f00(u(name), Field<uint32_t>(G(), 0xac0), 1);
    if (found)
    {
        orig::fn_00403be0(found, text, 0);
        return 1;
    }
    orig::fn_004e59d0(0x80, text, u(name));
    return 1;
}

static uint32_t fn_004187e0_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004187e0(reinterpret_cast<char*>(eax), reinterpret_cast<char*>(ecx), reinterpret_cast<char*>(edx), a0,
                       reinterpret_cast<const char*>(a1), a2);
}
ABI_ENTRY_004187e0(fn_004187e0_Entry, fn_004187e0_Abi)
HOOK(0x004187e0, "fn_004187e0", fn_004187e0_Entry, nullptr);

// ================================================================================================================
// Language tables (16-byte records at 0x00588e78: {language code, byte +8, text +0xc}, 5 entries)
// ================================================================================================================

// 0x00447600 UI_GetLoadingText (no inputs): the record of Language_GetCode 0x00447590 (index 5, past the table, when
// none matches); returns its text +0xc ("Chargement en cours...", "Laden..."). Name from the tracker.
uint32_t UI_GetLoadingText()
{
    const uint32_t code = orig::fn_00447590();
    uint32_t i = 0;
    for (const uint32_t* e = reinterpret_cast<const uint32_t*>(0x00588e78); i < 5 && *e != code; e += 4)
        ++i;
    return GameGlobal<uint32_t>(0x00588e84 + i * 16);
}

static uint32_t UI_GetLoadingText_Abi()
{
    return UI_GetLoadingText();
}
ABI_ENTRY_00447600(UI_GetLoadingText_Entry, UI_GetLoadingText_Abi)
HOOK(0x00447600, "UI_GetLoadingText", UI_GetLoadingText_Entry, nullptr);

// 0x00447630 (no inputs; caller 0x00401a80): same lookup, returns the record's byte +8 in al (upper bytes of eax are
// the language code's).
uint32_t fn_00447630()
{
    const uint32_t code = orig::fn_00447590();
    uint32_t i = 0;
    for (const uint32_t* e = reinterpret_cast<const uint32_t*>(0x00588e78); i < 5 && *e != code; e += 4)
        ++i;
    return (code & 0xffffff00u) | GameGlobal<uint8_t>(0x00588e80 + i * 16);
}

static uint32_t fn_00447630_Abi()
{
    return fn_00447630();
}
ABI_ENTRY_00447630(fn_00447630_Entry, fn_00447630_Abi)
HOOK(0x00447630, "fn_00447630", fn_00447630_Entry, nullptr);

// ================================================================================================================
// Key binding entries (control options; action index +0x588, key names +0x590/+0x594, label +0x578)
// ================================================================================================================

uint32_t fn_00447e20(uint32_t action, uint8_t* entry);
uint32_t fn_00447e70(uint8_t* entry);

// 0x00447d20 (ecx = entry; stack: 4 arguments, callee pops 16; virtual: found at slot +0x280 of vtable 0x005abb88
// (slot 1 names CLASS(BowGruntObject); the vtable start is a guess of the scan); not executed in recorded runs):
// 0x0046d400 with the same arguments, action 0x23 (none, 0x00447e20), 0x0046d580 (eax = entry; stack: 0.8).
// Returns eax as left by 0x0046d580.
uint32_t fn_00447d20(uint8_t* entry, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_0046d400(u(entry), a0, a1, a2, a3);
    fn_00447e20(0x23, entry);
    return orig::fn_0046d580(u(entry), 0x3f4ccccdu);
}

static uint32_t fn_00447d20_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00447d20(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00447d20(fn_00447d20_Entry, fn_00447d20_Abi)
HOOK(0x00447d20, "fn_00447d20", fn_00447d20_Entry, nullptr);

// 0x00447d60 (eax = column, ecx = entry, edx = row; caller 0x004710d0): hides the entry (+0x474 |= 0x40, +0x470 bit 0
// cleared), +0x4f0 = 1.0; for column < 12 tail-jumps to 0x00447e20 with the action of the table 0x00588dd8 (dword
// index column + 12 * row - 0x210). Returns eax as left.
uint32_t fn_00447d60(uint32_t column, uint8_t* entry, uint32_t row)
{
    Field<uint32_t>(entry, 0x474) |= 0x40;
    Field<uint32_t>(entry, 0x470) = Field<uint32_t>(entry, 0x470) & 0xfffffffeu;
    Field<uint32_t>(entry, 0x4f0) = 0x3f800000u;
    if (column >= 0xc)
        return column;
    const uint32_t index = column + (row * 3 - 0x84) * 4;
    return fn_00447e20(GameGlobal<uint32_t>(0x00588dd8 + index * 4), entry);
}

static uint32_t fn_00447d60_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return fn_00447d60(eax, B(ecx), edx);
}
ABI_ENTRY_00447d60(fn_00447d60_Entry, fn_00447d60_Abi)
HOOK(0x00447d60, "fn_00447d60", fn_00447d60_Entry, nullptr);

// 0x00447da0 (ecx = entry; virtual: found at slot +0x3ac of vtable 0x005abb88 (see 0x00447d20); not executed in
// recorded runs): 0x004656f0 (esi = entry; stack: 128.0, 0), 0x0046d5a0 (eax = entry; stack: 128.0), 0x0046d4b0;
// when +0x474 has 0x4000: two screen points from 0x0046b0a0 (eax = entry, ecx/edx = out; stack: (-128, 0) and
// (160, 0)) and 0x00465470 (al = 0 (upper bytes of eax: the first point's x bits), ecx = entry; stack: y1, x1, y2? ,
// x1 in the original's slot order). Returns eax as left.
uint32_t fn_00447da0(uint8_t* entry)
{
    uint32_t local[3] = {0, 0, 0};  // the original frame (0xc bytes)
    orig::fn_004656f0(u(entry), 0x43000000u, 0);
    orig::fn_0046d5a0(u(entry), 0x43000000u);
    uint32_t r = orig::fn_0046d4b0(u(entry));
    if (!(Field<uint32_t>(entry, 0x474) & 0x4000))
        return r;
    orig::fn_0046b0a0(u(entry), u(&local[2]), u(&local[0]), 0xffffff80u, 0);
    orig::fn_0046b0a0(u(entry), u(&local[1]), u(&local[0]), 0xa0, 0);
    return orig::fn_00465470(local[0] & 0xffffff00u, u(entry), local[2], local[0], local[1], local[0]);
}

static uint32_t fn_00447da0_Abi(uint32_t ecx)
{
    return fn_00447da0(B(ecx));
}
ABI_ENTRY_00447da0(fn_00447da0_Entry, fn_00447da0_Abi)
HOOK(0x00447da0, "fn_00447da0", fn_00447da0_Entry, nullptr);

// 0x00447e20 (eax = action index, ecx = entry): +0x588 = action. Actions >= 0x23 (signed) hide the entry (+0x474 |=
// 0x40, +0x470 bit 0 cleared; returns the action). Others show it (0x40 cleared, bit 0 set) and set it up:
// 0x004658a0 (eax = 0x00415720 (eax = action), esi = entry), then the key labels (0x00447e70).
uint32_t fn_00447e20(uint32_t action, uint8_t* entry)
{
    Field<uint32_t>(entry, 0x588) = action;
    const uint32_t flags = Field<uint32_t>(entry, 0x474);
    if (static_cast<int32_t>(action) >= 0x23)
    {
        Field<uint32_t>(entry, 0x474) = flags | 0x40;
        Field<uint32_t>(entry, 0x470) = Field<uint32_t>(entry, 0x470) & 0xfffffffeu;
        return action;
    }
    Field<uint32_t>(entry, 0x474) = flags & 0xffffffbfu;
    Field<uint32_t>(entry, 0x470) |= 1;
    const uint32_t r = orig::fn_00415720(Field<uint32_t>(entry, 0x588));
    orig::fn_004658a0(r, u(entry));
    return fn_00447e70(entry);
}

static uint32_t fn_00447e20_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00447e20(eax, B(ecx));
}
ABI_ENTRY_00447e20(fn_00447e20_Entry, fn_00447e20_Abi)
HOOK(0x00447e20, "fn_00447e20", fn_00447e20_Entry, nullptr);

// 0x00447e70 (esi = entry): reads the two keys bound to action +0x588 from the input device g+0xa80 (+0x1c / +0xa8
// tables) into +0x590/+0x594, names them (0x00415580) and sets the label +0x578: "PMS_INPUT_UNASSIGNED" (0x005b2600,
// Text_Lookup with g+0xac0) when none, the single name, or "PMS_INPUT_AORB" (0x005b25f0) formatted with both names
// into +0x4f8 (_swprintf 0x0053862e). Flags +0x474: 0x2000 set, then 0x4000 cleared. Returns the flags.
uint32_t fn_00447e70(uint8_t* entry)
{
    const uint32_t action = Field<uint32_t>(entry, 0x588);
    Field<uint32_t>(entry, 0x590) = Field<uint32_t>(Field<void*>(G(), 0xa80), 0x1c + action * 4);
    Field<uint32_t>(entry, 0x594) = Field<uint32_t>(Field<void*>(G(), 0xa80), 0xa8 + action * 4);
    Field<uint32_t>(entry, 0x474) |= 0x2000;
    const uint32_t first = orig::fn_00415580(Field<uint32_t>(entry, 0x590));
    const uint32_t second = orig::fn_00415580(Field<uint32_t>(entry, 0x594));
    if (!first)
    {
        if (second)
            Field<uint32_t>(entry, 0x578) = second;
        else
            Field<uint32_t>(entry, 0x578) = orig::fn_00495f00(0x005b2600u, Field<uint32_t>(G(), 0xac0), 0);
    }
    else if (!second)
    {
        Field<uint32_t>(entry, 0x578) = first;
    }
    else
    {
        const uint32_t format = orig::fn_00495f00(0x005b25f0u, Field<uint32_t>(G(), 0xac0), 0);
        Field<uint32_t>(entry, 0x578) = format;
        GameFunction<int(__cdecl*)(uint16_t*, const uint16_t*, ...)>(0x0053862e)(
            reinterpret_cast<uint16_t*>(entry + 0x4f8), reinterpret_cast<const uint16_t*>(format), first, second);
        Field<uint32_t>(entry, 0x578) = u(entry + 0x4f8);
    }
    const uint32_t flags = Field<uint32_t>(entry, 0x474) & 0xffffbfffu;
    Field<uint32_t>(entry, 0x474) = flags;
    return flags;
}

static uint32_t fn_00447e70_Abi(uint32_t esi)
{
    return fn_00447e70(B(esi));
}
ABI_ENTRY_00447e70(fn_00447e70_Entry, fn_00447e70_Abi)
HOOK(0x00447e70, "fn_00447e70", fn_00447e70_Entry, nullptr);

// 0x00447f50 UI_ShowAssignKeyPrompt (esi = entry; caller 0x004714c0; not executed in recorded runs): 0x00490880
// (al = 1, ecx = g+0xab4), 0x004908a0 (eax = g+0xab4, cl = 1), clears byte +0x50 of g+0xa70, label +0x578 =
// Text_Lookup("PMS_INPUT_ASSIGNKEY" 0x005b25dc), flags +0x474 |= 0x6000, byte +0x58c = 0. Returns the flags. Name from
// the tracker.
uint32_t UI_ShowAssignKeyPrompt(uint8_t* entry)
{
    const uint32_t menu = Field<uint32_t>(G(), 0xab4);
    orig::fn_00490880(1, menu);
    orig::fn_004908a0(Field<uint32_t>(G(), 0xab4), (Field<uint32_t>(G(), 0xab4) & 0xffffff00u) | 1);
    Field<uint8_t>(Field<void*>(G(), 0xa70), 0x50) = 0;
    Field<uint32_t>(entry, 0x578) = orig::fn_00495f00(0x005b25dcu, Field<uint32_t>(G(), 0xac0), 0);
    const uint32_t flags = Field<uint32_t>(entry, 0x474) | 0x6000;
    Field<uint32_t>(entry, 0x474) = flags;
    Field<uint8_t>(entry, 0x58c) = 0;
    return flags;
}

static uint32_t UI_ShowAssignKeyPrompt_Abi(uint32_t esi)
{
    return UI_ShowAssignKeyPrompt(B(esi));
}
ABI_ENTRY_00447f50(UI_ShowAssignKeyPrompt_Entry, UI_ShowAssignKeyPrompt_Abi)
HOOK(0x00447f50, "UI_ShowAssignKeyPrompt", UI_ShowAssignKeyPrompt_Entry, nullptr);

// 0x00447fc0 (edi = key; stack: entry, callee pops 4; caller 0x004481b0; not executed in recorded runs): unbinds the
// key from every action of the 6 x 4 table 0x00588dd8 (second binding g+0xa80 +0xa8 cleared; a first binding
// +0x1c is replaced by the second and the second cleared), noting whether the entry's own action (+0x588) lost it;
// then Options_Apply (eax = 0x00588e01: al = 1, upper bytes the table pointer the original leaves in eax; esi = g).
// Returns al = the entry's action lost the key.
uint32_t fn_00447fc0(uint32_t key, uint8_t* entry)
{
    uint8_t* g = G();
    uint8_t lost = 0;
    const uint32_t* actions = reinterpret_cast<const uint32_t*>(0x00588dd8);
    for (int i = 0; i < 6 * 4; ++i)
    {
        const uint32_t a = actions[i];
        uint32_t* second = &Field<uint32_t>(Field<void*>(g, 0xa80), 0xa8 + a * 4);
        if (*second == key)
        {
            if (a == Field<uint32_t>(entry, 0x588))
                lost = 1;
            *second = 0;
        }
        uint8_t* device = Field<uint8_t*>(g, 0xa80);
        if (Field<uint32_t>(device, 0x1c + a * 4) == key)
        {
            if (a == Field<uint32_t>(entry, 0x588))
                lost = 1;
            Field<uint32_t>(device, 0x1c + a * 4) = Field<uint32_t>(device, 0xa8 + a * 4);
            Field<uint32_t>(Field<void*>(g, 0xa80), 0xa8 + a * 4) = 0;
        }
    }
    Options_Apply(0x00588e01u, g);
    return lost;
}

static uint32_t fn_00447fc0_Abi(uint32_t edi, uint32_t a0)
{
    return fn_00447fc0(edi, B(a0));
}
ABI_ENTRY_00447fc0(fn_00447fc0_Entry, fn_00447fc0_Abi)
HOOK(0x00447fc0, "fn_00447fc0", fn_00447fc0_Entry, nullptr);

// 0x004481b0 (eax = key, ecx = entry; caller 0x00448260; not executed in recorded runs): binds the key to the entry's
// action unless 0x00447fc0 reports the action already had it: an empty first slot takes it, else it becomes the second
// key (and the old second moves to the first slot when there was one), written back to g+0xa80 +0x1c / +0xa8. Then
// the labels (0x00447e70) and Options_Apply (al = 1, esi = g). Returns Options_Apply's result.
uint32_t fn_004481b0(uint32_t key, uint8_t* entry)
{
    if (!static_cast<uint8_t>(fn_00447fc0(key, entry)))
    {
        uint8_t* g = G();
        const uint32_t a = Field<uint32_t>(entry, 0x588);
        Field<uint32_t>(entry, 0x590) = Field<uint32_t>(Field<void*>(g, 0xa80), 0x1c + a * 4);
        const uint32_t second = Field<uint32_t>(Field<void*>(g, 0xa80), 0xa8 + a * 4);
        Field<uint32_t>(entry, 0x594) = second;
        if (!Field<uint32_t>(entry, 0x590))
        {
            Field<uint32_t>(entry, 0x590) = key;
        }
        else
        {
            Field<uint32_t>(entry, 0x594) = key;
            if (second)
                Field<uint32_t>(entry, 0x590) = second;
        }
        Field<uint32_t>(Field<void*>(g, 0xa80), 0x1c + a * 4) = Field<uint32_t>(entry, 0x590);
        Field<uint32_t>(Field<void*>(g, 0xa80), 0xa8 + Field<uint32_t>(entry, 0x588) * 4) =
            Field<uint32_t>(entry, 0x594);
    }
    const uint32_t r = fn_00447e70(entry);
    return Options_Apply((r & 0xffffff00u) | 1, G());
}

static uint32_t fn_004481b0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004481b0(eax, B(ecx));
}
ABI_ENTRY_004481b0(fn_004481b0_Entry, fn_004481b0_Abi)
HOOK(0x004481b0, "fn_004481b0", fn_004481b0_Entry, nullptr);

// 0x00448260 (ecx = entry; virtual: found at slot +0x324 of vtable 0x005abb88 (see 0x00447d20); executed in recorded
// runs): per-frame update of a key binding entry. When visible (+0x470 bit 0 or +0x474 bit 2) refreshes the labels if
// the device's bindings changed (0x00447e70). While waiting for a key (+0x474 & 0x4000): the key from (g+0x30)
// virtual +0x38 (0) or the first of the 14 buttons 0x00588e38 that 0x00414b20 (edi = g+0xa80; stack: button, 0.5)
// reports; none (or a released prompt, +0x58c = 0) arms +0x58c. With +0x58c armed: Escape (1) cancels (0x00491870
// (eax = 0)), Pause (0xc5) is ignored; a key not bound to the two reserved actions 0x00588dd0 is bound (0x004481b0,
// then 0x00491850 (eax = 0)); then the prompt closes: labels, 0x4000 cleared, 0x00490880 (al = 0, ecx = g+0xab4),
// 0x004908a0 (eax = g+0xab4, cl = 0), byte +0x50 of g+0xa70 = 1 and a tail jump to 0x00414680 (eax = g+0xa80, ecx =
// 0x23; guess: UI sound). Returns eax as left.
uint32_t fn_00448260(uint8_t* entry)
{
    uint8_t cancelled = 0;
    if (!(Field<uint8_t>(entry, 0x470) & 1) && !(Field<uint8_t>(entry, 0x474) & 2))
        return Field<uint8_t>(entry, 0x470);
    {
        const uint32_t a = Field<uint32_t>(entry, 0x588);
        uint8_t* device = Field<uint8_t*>(G(), 0xa80);
        if (Field<uint32_t>(entry, 0x590) != Field<uint32_t>(device, 0x1c + a * 4) ||
            Field<uint32_t>(entry, 0x594) != Field<uint32_t>(device, 0xa8 + a * 4))
            fn_00447e70(entry);
    }
    if (!(Field<uint32_t>(entry, 0x474) & 0x4000))
        return Field<uint32_t>(entry, 0x474);
    uint32_t key = VCall<uint32_t>(Field<void*>(G(), 0x30), 0x38 / 4, 0u);
    if (!key)
    {
        const uint32_t* buttons = reinterpret_cast<const uint32_t*>(0x00588e38);
        uint32_t i = 0;
        for (; i < 0xe; ++i)
        {
            if (static_cast<uint8_t>(orig::fn_00414b20(Field<uint32_t>(G(), 0xa80), buttons[i], 0x3f000000u)))
                break;
        }
        if (i == 0xe)
        {
            Field<uint8_t>(entry, 0x58c) = 1;
            return 1;
        }
        key = buttons[i];
        if (!key)
        {
            Field<uint8_t>(entry, 0x58c) = 1;
            return 1;
        }
    }
    if (!Field<uint8_t>(entry, 0x58c))
        return 0;
    if (key == 1)
    {
        key = 0;
        cancelled = 1;
        orig::fn_00491870(0);
    }
    else if (key == 0xc5)
    {
        key = 0;
    }
    uint8_t* device = Field<uint8_t*>(G(), 0xa80);
    bool reserved = false;
    for (uint32_t i = 0; i < 2; ++i)
    {
        const uint32_t a = GameGlobal<uint32_t>(0x00588dd0 + i * 4);
        if (key == Field<uint32_t>(device, 0x1c + a * 4) || key == Field<uint32_t>(device, 0xa8 + a * 4))
        {
            reserved = true;
            break;
        }
    }
    if (!reserved && key)
    {
        fn_004481b0(key, entry);
        orig::fn_00491850(0);
    }
    else if (!cancelled)
    {
        return 0;
    }
    const uint32_t r = fn_00447e70(entry);
    Field<uint32_t>(entry, 0x474) &= 0xffffbfffu;
    uint8_t* g = G();
    orig::fn_00490880(r & 0xffffff00u, Field<uint32_t>(g, 0xab4));
    orig::fn_004908a0(Field<uint32_t>(g, 0xab4), Field<uint32_t>(g, 0xab4) & 0xffffff00u);
    Field<uint8_t>(Field<void*>(G(), 0xa70), 0x50) = 1;
    return orig::fn_00414680(Field<uint32_t>(G(), 0xa80), 0x23);
}

static uint32_t fn_00448260_Abi(uint32_t ecx)
{
    return fn_00448260(B(ecx));
}
ABI_ENTRY_00448260(fn_00448260_Entry, fn_00448260_Abi)
HOOK(0x00448260, "fn_00448260", fn_00448260_Entry, nullptr);

// ================================================================================================================
// End-of-level screen sequence (state record +0xc; 0x00403370 (eax = g, ecx = callback, edx = record) re-arms the
// callback 0x0044a230)
// ================================================================================================================

uint32_t fn_00463890(const char* name, uint32_t arg0);

// 0x0044a230 (stack: record, caller pops; registered as a callback through 0x00403370; not executed in recorded runs):
// advances the end-of-level sequence by state +0xc: 10 pushes "Stats" (UI_PushScreen 0x00490b00, eax = 0, ecx =
// g+0xab4) -> 11; 11 with a movie g+0x2680 stops something (0x0044f4c0 (eax = g+0xa88), (g+0x2c) virtual +0xa8 (0))
// and pushes "PlayMovie" (eax = the movie) -> 12; 11 without a movie, 12 and 13 push "Credits" after level "lev_5_m5"
// (0x0044a660 (eax = record, cl = 0), _stricmp) -> 14, else 0x00401ef0 (eax = g), 0x004907c0 (eax = g+0xab4, ecx = 0
// or g+0x268c when g+0x568 bit 2 is clear) and "Loading"; 14 does 0x004907c0 (ecx = 0) and "Loading". The states that
// continue re-arm the callback (tail jump to 0x00403370). Returns eax as left.
uint32_t fn_0044a230(uint8_t* record)
{
    const uint32_t state = Field<uint32_t>(record, 0xc);
    if (state - 0xa > 4)
        return state - 0xa;
    if (state == 0xa)
    {
        orig::fn_00490b00(0, Field<uint32_t>(G(), 0xab4), 0x005b2548u);
        Field<uint32_t>(record, 0xc) = 0xb;
        return orig::fn_00403370(u(G()), 0x0044a230u, u(record));
    }
    uint32_t ecx = 0;
    if (state == 0xb)
    {
        Field<uint32_t>(record, 0xc) = 0xc;
        ecx = Field<uint32_t>(G(), 0x2680);
        if (ecx)
        {
            orig::fn_0044f4c0(Field<uint32_t>(G(), 0xa88));
            VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xa8 / 4, 0u);
            orig::fn_00490b00(Field<uint32_t>(G(), 0x2680), Field<uint32_t>(G(), 0xab4), 0x005bc0fcu);
            return orig::fn_00403370(u(G()), 0x0044a230u, u(record));
        }
    }
    uint8_t* g;
    if (state != 0xe)
    {
        const uint32_t level = orig::fn_0044a660(u(record), ecx & 0xffffff00u);
        if (GameFunction<int(__cdecl*)(const char*, const char*)>(0x005448ee)(
                reinterpret_cast<const char*>(level), reinterpret_cast<const char*>(0x005b253c)) == 0)
        {
            orig::fn_00490b00(0, Field<uint32_t>(G(), 0xab4), 0x005b2534u);
            Field<uint32_t>(record, 0xc) = 0xe;
            return orig::fn_00403370(u(G()), 0x0044a230u, u(record));
        }
        g = G();
        orig::fn_00401ef0(u(g));
        if (Field<uint8_t>(g, 0x568) & 4)
            orig::fn_004907c0(Field<uint32_t>(g, 0xab4), 0);
        else
            orig::fn_004907c0(Field<uint32_t>(g, 0xab4), u(g + 0x268c));
    }
    else
    {
        g = G();
        orig::fn_004907c0(Field<uint32_t>(g, 0xab4), 0);
    }
    return orig::fn_00490b00(0, Field<uint32_t>(g, 0xab4), 0x005b252cu);
}

static uint32_t fn_0044a230_Abi(uint32_t a0)
{
    return fn_0044a230(B(a0));
}
ABI_ENTRY_0044a230(fn_0044a230_Entry, fn_0044a230_Abi)
HOOK(0x0044a230, "fn_0044a230", fn_0044a230_Entry, nullptr);

// 0x0044a390 (esi = record; caller 0x00402e80): when the record is active (+0x15c) and its state +0xc is 2..8: state
// 2 stops (g+0x2c) virtual +0xa8 (0) and 0x0044f560 (eax = g+0xa88, cl = 0), 0x00403410 (ecx = g; stack: g+0x2660, 1)
// when g+0x2660 is set, state = 10 and arms the callback 0x0044a230 (tail jump to 0x00403370); states 3, 4, 8 and 5..7
// stop the same way (0x0044f570) and push "MissionFailed" (UI_PushScreen, eax = g+0x2664 or g+0x2654 + state * 4),
// state = 9. Returns eax as left.
uint32_t fn_0044a390(uint8_t* record)
{
    if (!Field<uint8_t>(record, 0x15c))
        return Field<uint8_t>(record, 0x15c);
    const uint32_t state = Field<uint32_t>(record, 0xc);
    if (state == 0 || state == 1 || state - 2 > 6)
        return state;
    if (state == 2)
    {
        VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xa8 / 4, 0u);
        uint8_t* g = G();
        orig::fn_0044f560(Field<uint32_t>(g, 0xa88), 0);
        if (Field<uint32_t>(g, 0x2660))
        {
            orig::fn_00403410(u(g), Field<uint32_t>(g, 0x2660), 1);
            g = G();
        }
        Field<uint32_t>(record, 0xc) = 0xa;
        return orig::fn_00403370(u(g), 0x0044a230u, u(record));
    }
    VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xa8 / 4, 0u);
    uint8_t* g = G();
    orig::fn_0044f570(Field<uint32_t>(g, 0xa88));
    const uint32_t movie = (state == 3 || state == 4 || state == 8) ? Field<uint32_t>(g, 0x2664)
                                                                    : Field<uint32_t>(g, 0x2654 + Field<uint32_t>(record, 0xc) * 4);
    const uint32_t r = orig::fn_00490b00(movie, Field<uint32_t>(g, 0xab4), 0x005b251cu);
    Field<uint32_t>(record, 0xc) = 9;
    return r;
}

static uint32_t fn_0044a390_Abi(uint32_t esi)
{
    return fn_0044a390(B(esi));
}
ABI_ENTRY_0044a390(fn_0044a390_Entry, fn_0044a390_Abi)
HOOK(0x0044a390, "fn_0044a390", fn_0044a390_Entry, nullptr);

// ================================================================================================================
// Ticker bomb HUD (strings 'ui_tickerbomb_patience', 'ui_tickerbomb_set', 'wpn_ticker_windup_lp', 'ui_clock')
// ================================================================================================================

// 0x0044e3a0 (edi = ticker; caller 0x004788a0; not executed in recorded runs): while g+0xdcf (armed) shows
// "ui_tickerbomb_patience" (0x00463890, +0x474 |= 4); otherwise, when the ticker is not set (+0x460), +0x464 = the time
// 0x00604a98 and, when "ui_tickerbomb_set" exists, starts the sound "wpn_ticker_windup_lp" ((g+0x2c) virtual +0xd0
// (name, 0, 1, 0, -1, 0), handle at +0x4c0) and shows the widget (+0x474 |= 4, +0x4a0 = 0). Returns eax as left.
uint32_t fn_0044e3a0(uint8_t* ticker)
{
    uint8_t* g = G();
    if (Field<uint8_t>(g, 0xdcf))
    {
        const uint32_t w = fn_00463890(reinterpret_cast<const char*>(0x005b1640), Field<uint32_t>(g, 0xa70));
        if (w)
            Field<uint32_t>(P(w), 0x474) |= 4;
        return w;
    }
    if (Field<uint8_t>(ticker, 0x460))
        return u(g);
    Field<uint32_t>(ticker, 0x464) = GameGlobal<uint32_t>(0x00604a98);
    const uint32_t w =
        ObjectList_FindByName(nullptr, Field<uint32_t>(g, 0xa70), reinterpret_cast<const char*>(0x005b162c));
    if (!w)
        return 0;
    Field<uint32_t>(ticker, 0x4c0) =
        VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xd0 / 4, 0x005b1614u, 0u, 1u, 0u, 0xffffffffu, 0u);
    Field<uint32_t>(P(w), 0x474) |= 4;
    Field<uint32_t>(P(w), 0x4a0) = 0;
    return Field<uint32_t>(ticker, 0x4c0);
}

static uint32_t fn_0044e3a0_Abi(uint32_t edi)
{
    return fn_0044e3a0(B(edi));
}
ABI_ENTRY_0044e3a0(fn_0044e3a0_Entry, fn_0044e3a0_Abi)
HOOK(0x0044e3a0, "fn_0044e3a0", fn_0044e3a0_Entry, nullptr);

// 0x0044e430 (edi = ticker; caller 0x0044e150; not executed in recorded runs): when not set (+0x460): hides
// "ui_tickerbomb_set" (+0x474 &= ~4), stops the wind-up sound +0x4c0 ((g+0x2c) virtual +0xd8 (&handle, 0)), clears it,
// g+0xdcf and +0x464. Returns eax as left.
uint32_t fn_0044e430(uint8_t* ticker)
{
    if (Field<uint8_t>(ticker, 0x460))
        return Field<uint8_t>(ticker, 0x460);
    const uint32_t w =
        ObjectList_FindByName(nullptr, Field<uint32_t>(G(), 0xa70), reinterpret_cast<const char*>(0x005b162c));
    if (w)
        Field<uint32_t>(P(w), 0x474) &= 0xfffffffbu;
    uint32_t r = Field<uint32_t>(ticker, 0x4c0);
    if (r)
        r = VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xd8 / 4, ticker + 0x4c0, 0u);
    Field<uint32_t>(ticker, 0x4c0) = 0;
    Field<uint8_t>(G(), 0xdcf) = 0;
    Field<uint32_t>(ticker, 0x464) = 0;
    return r;
}

static uint32_t fn_0044e430_Abi(uint32_t edi)
{
    return fn_0044e430(B(edi));
}
ABI_ENTRY_0044e430(fn_0044e430_Entry, fn_0044e430_Abi)
HOOK(0x0044e430, "fn_0044e430", fn_0044e430_Entry, nullptr);

// 0x0044e4a0 (edi = ticker; caller 0x0044e150; not executed in recorded runs): arms the ticker bomb when not set:
// hides "ui_tickerbomb_set", stops the wind-up sound, plays "wpn_ticker" and one of the voice lines
// "ROM50211_TickerArmed" / "ROM50212_TickerArmed" (_rand 0x005391cd: (rand * 100) & 0xffff8000 < 0x190000, i.e. a 50%
// split) through (g+0x2c) virtual +0xd0 (name, 0, 0, 0, -1, 0); sets +0x460 and g+0xdcf, 0x0044e0c0 (eax = ticker),
// virtual +0x100 (1.0), starts "ui_clock" (Anim_Set 0x00408d90, eax = -1, esi = widget; stack: name, 0x81, 0, 0, 0,
// 0; +0x474 |= 4), g+0x25e4 = 6.0, +0x390 = 0. Returns eax as left.
uint32_t fn_0044e4a0(uint8_t* ticker)
{
    if (Field<uint8_t>(ticker, 0x460))
        return Field<uint8_t>(ticker, 0x460);
    const uint32_t w =
        ObjectList_FindByName(nullptr, Field<uint32_t>(G(), 0xa70), reinterpret_cast<const char*>(0x005b162c));
    if (w)
        Field<uint32_t>(P(w), 0x474) &= 0xfffffffbu;
    if (Field<uint32_t>(ticker, 0x4c0))
        VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xd8 / 4, ticker + 0x4c0, 0u);
    Field<uint32_t>(ticker, 0x4c0) = 0;
    VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xd0 / 4, 0x005b1608u, 0u, 0u, 0u, 0xffffffffu, 0u);
    const int32_t roll = GameFunction<int(__cdecl*)()>(0x005391cd)() * 100;
    const uint32_t line = static_cast<int32_t>(static_cast<uint32_t>(roll) & 0xffff8000u) < 0x190000 ? 0x005b15f0u
                                                                                                    : 0x005b15d8u;
    VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xd0 / 4, line, 0u, 0u, 0u, 0xffffffffu, 0u);
    Field<uint8_t>(ticker, 0x460) = 1;
    Field<uint8_t>(G(), 0xdcf) = 1;
    orig::fn_0044e0c0(u(ticker));
    VCall<uint32_t>(ticker, 0x100 / 4, 0x3f800000u);
    const uint32_t clock =
        ObjectList_FindByName(nullptr, Field<uint32_t>(G(), 0xa70), reinterpret_cast<const char*>(0x005b15cc));
    uint32_t r = clock;
    if (clock)
    {
        r = orig::fn_00408d90(0xffffffffu, clock, 0x005b15ccu, 0x81, 0, 0, 0, 0);
        Field<uint32_t>(P(clock), 0x474) |= 4;
    }
    Field<uint32_t>(G(), 0x25e4) = 0x40c00000u;
    Field<uint32_t>(ticker, 0x390) = 0;
    return r;
}

static uint32_t fn_0044e4a0_Abi(uint32_t edi)
{
    return fn_0044e4a0(B(edi));
}
ABI_ENTRY_0044e4a0(fn_0044e4a0_Entry, fn_0044e4a0_Abi)
HOOK(0x0044e4a0, "fn_0044e4a0", fn_0044e4a0_Entry, nullptr);

// ================================================================================================================
// HUD manager (0x004626e0 constructor-like init; 0x004629b0 draws it)
// ================================================================================================================

uint32_t fn_004637d0(uint8_t* hud);

// 0x004626e0 (edi = HUD; caller 0x004011e0): init: clears +0x48, +0x7c..+0x98 (+0x94 = 1.0), +0x9c = a new list
// (heap 0x10/0x18/2, 0x004e7010 (eax = g+0xac4, edx = 0, esi = block)), reset 0x004637d0, +0x4c = 0.2. Returns the HUD.
uint32_t fn_004626e0(uint8_t* hud)
{
    Field<uint32_t>(hud, 0x48) = 0;
    Field<uint32_t>(hud, 0x7c) = 0;
    Field<uint32_t>(hud, 0x84) = 0;
    Field<uint32_t>(hud, 0x80) = 0;
    Field<uint32_t>(hud, 0x88) = 0;
    Field<uint32_t>(hud, 0x8c) = 0;
    Field<uint32_t>(hud, 0x90) = 0;
    Field<uint32_t>(hud, 0x94) = 0x3f800000u;
    Field<uint32_t>(hud, 0x98) = 0;
    const uint32_t block = Alloc(0x10, 0x18, 2, 0x0058b5e8u);
    Field<uint32_t>(hud, 0x9c) = block ? orig::fn_004e7010(Field<uint32_t>(G(), 0xac4), 0, block) : 0;
    fn_004637d0(hud);
    Field<uint32_t>(hud, 0x4c) = 0x3e4ccccdu;
    return u(hud);
}

static uint32_t fn_004626e0_Abi(uint32_t edi)
{
    return fn_004626e0(B(edi));
}
ABI_ENTRY_004626e0(fn_004626e0_Entry, fn_004626e0_Abi)
HOOK(0x004626e0, "fn_004626e0", fn_004626e0_Entry, nullptr);

// 0x00462770 (eax = HUD; caller 0x00401710; not executed in recorded runs): deletes the list +0x9c (virtual +0x10 (1)).
// Returns eax as left.
uint32_t fn_00462770(uint8_t* hud)
{
    void* list = Field<void*>(hud, 0x9c);
    if (!list)
        return u(hud);
    return VCall<uint32_t>(list, 0x10 / 4, 1u);
}

static uint32_t fn_00462770_Abi(uint32_t eax)
{
    return fn_00462770(B(eax));
}
ABI_ENTRY_00462770(fn_00462770_Entry, fn_00462770_Abi)
HOOK(0x00462770, "fn_00462770", fn_00462770_Entry, nullptr);

// 0x00462790 (stack: unused, callee pops 4; caller 0x00402e80): pause input. When g+0xdcb is set, every object of list
// g+0xa84 +0xe70 with an active animation (+0x414 -> +0x10 -> +0x28) gets virtual +0x9c, 0x00409ec0 (eax = object;
// stack: 0, 1) and, with byte +0x421, 0x00459000 (al = 1, esi = object; stack: 0, 0, 0). Unless a menu is up
// (0x00490a20 (eax = g+0xab4)): outside cut scenes (g+0x2684 == 0) button 0x15 (0x00414730, eax: al = 1, ebx =
// button, esi = g+0xa80; stack: 0.5) pushes "PauseMenu" (UI_PushScreen); in a cut scene buttons 0x1b, 0x1c or 8 are
// consumed (0x00414680 for each) and skip it (0x004031e0 (ebx = 1, edi = g; stack: 0)). Returns 1.
uint32_t fn_00462790(uint32_t /*arg0*/)
{
    uint8_t* g = G();
    if (Field<uint8_t>(g, 0xdcb))
    {
        uint32_t iterator[2] = {0, 0};
        for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(g, 0xa84), 0xe70), u(iterator))); obj;
             obj = B(orig::fn_004e73c0(u(iterator))))
        {
            const uint32_t anim = Field<uint32_t>(obj, 0x414);
            if (!anim || !Field<uint32_t>(Field<void*>(P(anim), 0x10), 0x28))
                continue;
            VCall<uint32_t>(obj, 0x9c / 4);
            const uint32_t r = orig::fn_00409ec0(u(obj), 0, 1);
            if (Field<uint8_t>(obj, 0x421))
                orig::fn_00459000((r & 0xffffff00u) | 1, u(obj), 0, 0, 0);
        }
        if (iterator[1])
            --Field<int32_t>(P(iterator[1]), 0x18);
        g = G();
    }
    if (static_cast<uint8_t>(orig::fn_00490a20(Field<uint32_t>(g, 0xab4))))
        return 1;
    const uint32_t cutscene = Field<uint32_t>(g, 0x2684);
    if (cutscene == 0)
    {
        if (static_cast<uint8_t>(orig::fn_00414730(1, 0x15, Field<uint32_t>(g, 0xa80), 0x3f000000u)))
            orig::fn_00490b00(0, Field<uint32_t>(G(), 0xab4), 0x005bc138u);
        return 1;
    }
    uint32_t r = orig::fn_00414730((cutscene & 0xffffff00u) | 1, 0x1b, Field<uint32_t>(g, 0xa80), 0x3f000000u);
    if (!static_cast<uint8_t>(r))
    {
        r = orig::fn_00414730((r & 0xffffff00u) | 1, 0x1c, Field<uint32_t>(G(), 0xa80), 0x3f000000u);
        if (!static_cast<uint8_t>(r))
        {
            r = orig::fn_00414730((r & 0xffffff00u) | 1, 8, Field<uint32_t>(G(), 0xa80), 0x3f000000u);
            if (!static_cast<uint8_t>(r))
                return 1;
        }
    }
    uint8_t* game = G();
    orig::fn_00414680(Field<uint32_t>(game, 0xa80), 0x1b);
    orig::fn_00414680(Field<uint32_t>(game, 0xa80), 0x1c);
    orig::fn_00414680(Field<uint32_t>(game, 0xa80), 8);
    orig::fn_004031e0(1, u(game), 0);
    return 1;
}

static uint32_t fn_00462790_Abi(uint32_t a0)
{
    return fn_00462790(a0);
}
ABI_ENTRY_00462790(fn_00462790_Entry, fn_00462790_Abi)
HOOK(0x00462790, "fn_00462790", fn_00462790_Entry, nullptr);

uint32_t fn_00463e80(uint8_t* hud, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t fn_00463f30(uint8_t* hud);
uint32_t fn_00464b10(uint8_t* obj);

namespace
{
// Class names (ui object kinds) 0x004629b0 accepts as the created object's name for "objnames" entries; others get "ui".
const uint32_t kUiClassNames[] = {
    0x005b01d8, 0x005b01c0, 0x005b01ac, 0x005b0188, 0x005b017c, 0x005b019c, 0x005b0170, 0x005b0160,
    0x005b0154, 0x005b0144, 0x005b0130, 0x005b0120, 0x005b0114, 0x005bc6dc, 0x005bc750, 0x005bc7ec,
    0x005bbbd8, 0x005bc53c, 0x005bc638, 0x005bc5b4, 0x005bbbc4, 0x005bbbb4, 0x005bbb9c, 0x005bbbf0,
    0x005bc274, 0x005b162c, 0x005b15cc, 0x005b03d8, 0x005b010c, 0x005b033c, 0x005b00f0, 0x005b0100};
// Text names 0x004629b0 passes as the name of a created "ui_text" object; others are created unnamed.
const uint32_t kUiTextNames[] = {
    0x005b0324, 0x005b001c, 0x005afff4, 0x005b0310, 0x005b02c8, 0x005b021c, 0x005b0208, 0x005b01f8,
    0x005b01e4, 0x005b02a8, 0x005b1640, 0x005b0294, 0x005b0288, 0x005b026c, 0x005b0254, 0x005b0240,
    0x005b022c, 0x005b03cc, 0x005b03c0, 0x005b03a8, 0x005b0398, 0x005b03b4, 0x005b0388, 0x005b0378,
    0x005b036c, 0x005b035c, 0x005b034c, 0x005b02bc, 0x005b00d4, 0x005b02ec, 0x005b02dc, 0x005b02fc};
inline int Stricmp(uint32_t a, uint32_t b)
{
    return GameFunction<int(__cdecl*)(const char*, const char*)>(0x005448ee)(reinterpret_cast<const char*>(a),
                                                                            reinterpret_cast<const char*>(b));
}
// ObjectSystem_CreateObject 0x0045f020 (eax = object name or 0, ecx = class name; stack: level, 0, 0x3ff, 0, 0, 0,
// 0, 0xaa, 1) as every call of 0x004629b0 makes it.
inline uint32_t CreateUiObject(uint32_t name, uint32_t className)
{
    return orig::fn_0045f020(name, className, Field<uint32_t>(G(), 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 1);
}
}  // namespace

#define FU(o) Field<uint32_t>(frame, (o) - 0x10)
#define FF(o) Field<float>(frame, (o) - 0x10)
#define FA(o) u(frame + (o) - 0x10)

// 0x004629b0 Hud_CreateLevelUi (stack: HUD, unused; callee pops 8; caller 0x0044a4b0): builds the level's UI from the
// HUD's description block +0x48. Logs "Error: This level is missing the UI..." (Debug_Printf 0x00407ff0, g+0x34)
// when the class "ui_crosshair" is unknown (0x0045abe0). Block: 8 counts {objnames, refnames, txtnames, plnames,
// object refs, objects, texts, behaviour bytes} and a skipped word, then the four 32-byte name lists (pointer arrays
// +0x84/+0x88/+0x8c/+0x90, heap 0x0063a210 with the tags "objnames list" ...), then the records: object refs
// (instances of known classes, 0x004546d0 into g+0xa84 +0xec0); objects (ObjectSystem_CreateObject 0x0045f020 named
// by class or "ui"; a class with a behaviour +0x3c gets its name from 0x0045dec0 (ecx = "behavior"); +0x468/+0x46c from
// the record, then 5 dwords to +0x478..+0x488, written into a dummy in the frame when creation fails); texts
// ("ui_text" objects, 0x004659e0 (0.5 x4), 0x004659b0, 0x00465850, 0x004659d0, 5 dwords as above; when the class
// "ui_text" is unknown the 5 trailing dwords are not skipped, see the report); behaviour data (0x00495ae0, else
// 0x00495ad0). Then every UI object gets 0x00464b10, 0x00463f30, 0x00463e80 ("pui_main", 1, 0, 0), and the widgets
// are looked up by name (ObjectList_FindByName) into +0x18.., +0x30, +0x34.., +0x54..+0x78 (with "ui_ammo_infinity"
// placed by 0x00457870 and "ui_mouse_cursor" hidden), some shown with flag 0x10. Name: guess from the error string.
uint32_t Hud_CreateLevelUi(uint8_t* hud, uint32_t /*arg1*/)
{
    alignas(16) uint8_t frame[0x548] = {};
    if (!orig::fn_0045abe0(0x005bc6dcu, Field<uint32_t>(G(), 0xa84)))
        GameFunction<void(__cdecl*)(uint32_t, const char*, ...)>(0x00407ff0)(
            Field<uint32_t>(G(), 0x34), reinterpret_cast<const char*>(0x005a0e40));
    const uint32_t* p = Field<const uint32_t*>(hud, 0x48);
    const uint32_t objNames = p[0];
    const uint32_t refNames = p[1];
    FU(0x14) = p[2];
    FU(0x18) = p[3];
    FU(0x10) = p[4];
    FU(0x20) = p[5];
    FU(0x1c) = p[6];
    FU(0x24) = p[7];
    const uint8_t* rec = reinterpret_cast<const uint8_t*>(p + 9);
    Field<uint32_t>(hud, 0x7c) = objNames;
    Field<uint32_t>(hud, 0x80) = refNames;
    Field<uint32_t>(hud, 0x84) = Alloc(3, objNames * 4, 2, 0x005a0e2cu);
    Field<uint32_t>(hud, 0x88) = Alloc(3, refNames * 4, 2, 0x005a0e1cu);
    Field<uint32_t>(hud, 0x8c) = Alloc(3, FU(0x14) * 4, 2, 0x005a0e0cu);
    Field<uint32_t>(hud, 0x90) = Alloc(3, FU(0x18) * 4, 2, 0x005a0dfcu);
    for (uint32_t i = 0; i < objNames; ++i, rec += 0x20)
        Field<uint32_t*>(hud, 0x84)[i] = u(rec);
    for (uint32_t i = 0; i < refNames; ++i, rec += 0x20)
        Field<uint32_t*>(hud, 0x88)[i] = u(rec);
    for (uint32_t i = 0; i < FU(0x14); ++i, rec += 0x20)
        Field<uint32_t*>(hud, 0x8c)[i] = u(rec);
    for (uint32_t i = 0; i < FU(0x18); ++i, rec += 0x20)
        Field<uint32_t*>(hud, 0x90)[i] = u(rec);

    // Object references.
    if (FU(0x10))
    {
        do
        {
            const uint32_t index = Field<uint32_t>(rec, 0);
            rec += 4;
            if (orig::fn_0045abe0(Field<uint32_t*>(hud, 0x84)[index], Field<uint32_t>(G(), 0xa84)))
            {
                const uint32_t obj = CreateUiObject(0, Field<uint32_t*>(hud, 0x84)[index]);
                if (obj)
                    orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xec0), obj);
            }
        } while (--FU(0x10));
    }

    // Objects.
    if (FU(0x20))
    {
        FU(0x10) = FU(0x20);
        do
        {
            const uint32_t index = Field<uint32_t>(rec, 0);
            FU(0x18) = Field<uint32_t>(rec, 4);
            FU(0x14) = Field<uint32_t>(rec, 8);
            rec += 0xc;
            const uint32_t cls = orig::fn_0045abe0(Field<uint32_t*>(hud, 0x8c)[index], Field<uint32_t>(G(), 0xa84));
            if (!cls)
            {
                rec += 0x14;
                continue;
            }
            const uint32_t desc = Field<uint32_t>(P(cls), 0x10);
            const uint32_t behaviour = Field<uint32_t>(P(desc), 0x3c);
            if (behaviour)
            {
                orig::fn_0045dec0(Field<uint32_t>(P(behaviour), 0), 0x005aff64u, Field<uint32_t>(P(behaviour), 8),
                                  Field<uint32_t>(G(), 0xa84), FA(0x38));
            }
            else
            {
                bool known = false;
                for (const uint32_t name : kUiClassNames)
                {
                    if (Stricmp(Field<uint32_t>(P(cls), 0x10) + 4, name) == 0)
                    {
                        known = true;
                        break;
                    }
                }
                if (known)
                    std::strcpy(reinterpret_cast<char*>(frame + 0x38 - 0x10),
                                reinterpret_cast<const char*>(Field<uint32_t>(P(cls), 0x10) + 4));
                else
                {
                    Field<uint16_t>(frame, 0x38 - 0x10) = GameGlobal<uint16_t>(0x005b03e4);
                    Field<uint8_t>(frame, 0x3a - 0x10) = GameGlobal<uint8_t>(0x005b03e6);
                }
            }
            uint8_t* obj = B(CreateUiObject(FA(0x38), Field<uint32_t*>(hud, 0x8c)[index]));
            if (!obj)
            {
                obj = frame + 0x60 - 0x10;
            }
            else
            {
                Field<uint32_t>(obj, 0x46c) = FU(0x18);
                Field<uint32_t>(obj, 0x468) = FU(0x14);
            }
            Field<uint32_t>(obj, 0x478) = Field<uint32_t>(rec, 0);
            Field<uint32_t>(obj, 0x47c) = Field<uint32_t>(rec, 4);
            Field<uint32_t>(obj, 0x480) = Field<uint32_t>(rec, 8);
            Field<uint32_t>(obj, 0x484) = Field<uint32_t>(rec, 0xc);
            Field<uint32_t>(obj, 0x488) = Field<uint32_t>(rec, 0x10);
            rec += 0x14;
        } while (--FU(0x10));
    }

    // Texts.
    if (FU(0x1c))
    {
        FU(0x10) = FU(0x1c);
        do
        {
            const uint32_t index = Field<uint32_t>(rec, 0);
            FU(0x14) = Field<uint32_t>(rec, 8);
            FU(0x18) = Field<uint32_t>(rec, 4);
            FU(0x1c) = Field<uint32_t>(rec, 0xc);
            FU(0x20) = Field<uint32_t>(rec, 0x10);
            rec += 0x14;
            if (!orig::fn_0045abe0(0x005b0334u, Field<uint32_t>(G(), 0xa84)))
                continue;
            uint32_t name = 0;
            for (const uint32_t s : kUiTextNames)
            {
                if (Stricmp(Field<uint32_t*>(hud, 0x90)[index], s) == 0)
                {
                    name = Field<uint32_t*>(hud, 0x90)[index];
                    break;
                }
            }
            uint8_t* obj = B(CreateUiObject(name, 0x005b0334u));
            if (!obj)
            {
                obj = frame + 0x60 - 0x10;
            }
            else
            {
                Field<uint32_t>(obj, 0x46c) = FU(0x18);
                Field<uint32_t>(obj, 0x468) = FU(0x14);
                orig::fn_004659e0(u(obj), 0x3f000000u, 0x3f000000u, 0x3f000000u, 0x3f000000u);
                orig::fn_004659b0(u(obj), FU(0x1c));
                orig::fn_00465850(Field<uint32_t*>(hud, 0x90)[index], u(obj), 1);
                orig::fn_004659d0(u(obj), FU(0x20));
            }
            Field<uint32_t>(obj, 0x478) = Field<uint32_t>(rec, 0);
            Field<uint32_t>(obj, 0x47c) = Field<uint32_t>(rec, 4);
            Field<uint32_t>(obj, 0x480) = Field<uint32_t>(rec, 8);
            Field<uint32_t>(obj, 0x484) = Field<uint32_t>(rec, 0xc);
            Field<uint32_t>(obj, 0x488) = Field<uint32_t>(rec, 0x10);
            rec += 0x14;
        } while (--FU(0x10));
    }

    // Behaviour data.
    if (FU(0x24))
        orig::fn_00495ae0(FU(0x24), u(rec), u(rec) + FU(0x24) * 40, Field<uint32_t>(hud, 0x90));
    else
        orig::fn_00495ad0();

    FU(0x24) = 0;
    FU(0x28) = 0;
    for (uint32_t obj = orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), FA(0x24)); obj;
         obj = orig::fn_004e73c0(FA(0x24)))
        fn_00464b10(B(obj));
    fn_00463f30(hud);
    fn_00463e80(hud, 0x005a0df0u, 1, 0, 0);
    const uint32_t hudArg = u(hud);
    uint32_t* out = reinterpret_cast<uint32_t*>(hud + 0x18);
    for (uint32_t i = 0; i < 0x7e0; i += 0x150, ++out)
        *out = ObjectList_FindByName(nullptr, hudArg, reinterpret_cast<const char*>(G() + 0x2810 + i));
    Field<uint32_t>(hud, 0x30) = ObjectList_FindByName(nullptr, hudArg, reinterpret_cast<const char*>(0x005a0de0));
    out = reinterpret_cast<uint32_t*>(hud + 0x34);
    for (uint32_t i = 0; i < 0x690; i += 0x150, ++out)
        *out = ObjectList_FindByName(nullptr, hudArg, reinterpret_cast<const char*>(G() + 0x2ff0 + i));
    const uint32_t infinity = ObjectList_FindByName(nullptr, hudArg, reinterpret_cast<const char*>(0x005a0dcc));
    Field<uint32_t>(hud, 0x58) = infinity;
    if (infinity)
    {
        const uint32_t r = VCall<uint32_t>(P(infinity), 0x6c / 4, FA(0x2c));
        FF(0x2c) = FF(0x2c) + K(0x005bcee0);
        orig::fn_00457870(r & 0xffffff00u, FA(0x2c), Field<uint32_t>(hud, 0x58), 0, 0, 0);
    }
    const uint32_t cursor = ObjectList_FindByName(nullptr, hudArg, reinterpret_cast<const char*>(0x005a0dbc));
    Field<uint32_t>(hud, 0x54) = cursor;
    if (cursor)
    {
        Field<uint32_t>(P(cursor), 0x474) &= 0xfffffff7u;
        Field<uint32_t>(Field<void*>(hud, 0x54), 0x470) = Field<uint32_t>(Field<void*>(hud, 0x54), 0x470) & 0xfffffffeu;
    }
    const uint32_t names[] = {0x005a0dac, 0x005a0d98, 0x005a0d84, 0x005a0d70,
                              0x005a0d5c, 0x005a0d48, 0x005a0d34, 0x005a0d20};
    for (uint32_t i = 0; i < 8; ++i)
        Field<uint32_t>(hud, 0x5c + i * 4) =
            ObjectList_FindByName(nullptr, hudArg, reinterpret_cast<const char*>(names[i]));
    const uint32_t shown[] = {0x005a0de0, 0x005bc2c8, 0x005b162c, 0x005b0100};
    for (const uint32_t s : shown)
    {
        const uint32_t w = ObjectList_FindByName(nullptr, hudArg, reinterpret_cast<const char*>(s));
        if (w)
            Field<uint32_t>(P(w), 0x474) |= 0x10;
    }
    if (FU(0x28))
        --Field<int32_t>(P(FU(0x28)), 0x18);
    return FU(0x28);
}

#undef FU
#undef FF
#undef FA

static uint32_t Hud_CreateLevelUi_Abi(uint32_t a0, uint32_t a1)
{
    return Hud_CreateLevelUi(B(a0), a1);
}
ABI_ENTRY_004629b0(Hud_CreateLevelUi_Entry, Hud_CreateLevelUi_Abi)
HOOK(0x004629b0, "Hud_CreateLevelUi", Hud_CreateLevelUi_Entry, nullptr);

// ================================================================================================================
// UI object list g+0xa84 +0xeb8 queries
// ================================================================================================================

// 0x004637d0 (eax = HUD; callers 0x0044a670, 0x004626e0): reset: clears +0..+0xc, the widgets +0x18..+0x2c, +0x30,
// +0x7c/+0x80, frees the four name arrays +0x84..+0x90 (heap 0x0063a210, 0x004e08d0) and empties the pool list +0x9c
// (its used chain +0xc is put back on the free chain of its block +8; +0xc/+0x10/+0x14 cleared). Returns eax as left.
uint32_t fn_004637d0(uint8_t* hud)
{
    Field<uint32_t>(hud, 0) = 0;
    Field<uint32_t>(hud, 4) = 0;
    Field<uint32_t>(hud, 8) = 0;
    Field<uint32_t>(hud, 0xc) = 0;
    std::memset(hud + 0x18, 0, 6 * 4);
    Field<uint32_t>(hud, 0x30) = 0;
    Field<uint32_t>(hud, 0x7c) = 0;
    if (Field<uint32_t>(hud, 0x84))
        orig::fn_004e08d0(0x0063a210u, Field<uint32_t>(hud, 0x84));
    Field<uint32_t>(hud, 0x84) = 0;
    Field<uint32_t>(hud, 0x80) = 0;
    if (Field<uint32_t>(hud, 0x88))
        orig::fn_004e08d0(0x0063a210u, Field<uint32_t>(hud, 0x88));
    Field<uint32_t>(hud, 0x88) = 0;
    if (Field<uint32_t>(hud, 0x8c))
        orig::fn_004e08d0(0x0063a210u, Field<uint32_t>(hud, 0x8c));
    Field<uint32_t>(hud, 0x8c) = 0;
    if (Field<uint32_t>(hud, 0x90))
        orig::fn_004e08d0(0x0063a210u, Field<uint32_t>(hud, 0x90));
    Field<uint32_t>(hud, 0x90) = 0;
    uint8_t* list = Field<uint8_t*>(hud, 0x9c);
    const uint32_t used = Field<uint32_t>(list, 0xc);
    if (used)
    {
        uint8_t* block = Field<uint8_t*>(list, 8);
        *Field<uint32_t*>(list, 0x10) = Field<uint32_t>(block, 8);
        Field<uint32_t>(block, 8) = used;
    }
    Field<uint32_t>(list, 0xc) = 0;
    Field<uint32_t>(list, 0x10) = 0;
    Field<uint32_t>(list, 0x14) = 0;
    return used;
}

static uint32_t fn_004637d0_Abi(uint32_t eax)
{
    return fn_004637d0(B(eax));
}
ABI_ENTRY_004637d0(fn_004637d0_Entry, fn_004637d0_Abi)
HOOK(0x004637d0, "fn_004637d0", fn_004637d0_Entry, nullptr);

// 0x00463890 (ebx = name; stack: unused, callee pops 4): the first object of the UI list g+0xa84 +0xeb8 whose virtual
// +0x150 has bit 1 and whose text +0x4a0 equals the name (_stricmp); 0 when none.
uint32_t fn_00463890(const char* name, uint32_t /*arg0*/)
{
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        if (!(VCall<uint8_t>(obj, 0x150 / 4) & 2))
            continue;
        if (Stricmp(u(obj + 0x4a0), u(name)) != 0)
            continue;
        VCall<uint32_t>(P(iterator[0]), 0xc / 4, iterator[1]);
        --Field<int32_t>(P(iterator[1]), 0x18);
        return u(obj);
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return 0;
}

static uint32_t fn_00463890_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_00463890(reinterpret_cast<const char*>(ebx), a0);
}
ABI_ENTRY_00463890(fn_00463890_Entry, fn_00463890_Abi)
HOOK(0x00463890, "fn_00463890", fn_00463890_Entry, nullptr);

// 0x00463930 (stack: unused, a1, a2, a3; callee pops 16): the first object of the UI list that is shown (+0x474 bit 2,
// or +0x470 bit 0 with +0x474 bit 4), whose virtual +0x150 has bit 1, whose +0x474 has 0x100 and whose virtual +0x140
// (a1, a2, a3) is true (guess: a hit test of the mouse); 0 when none.
// Enhancement point (hook/enhance/ui_aspect.cpp): the factor the UI text is drawn larger by. When set (> 0), boxes
// (+0x490..+0x49c) grow about their centre by it and the candidate whose centre is nearest wins: the boxes fit the
// unscaled text, so clicks on the outer part of scaled text missed and the click was cancelled (0x004929e0), and
// boxes taller than the item spacing let the first item in list order win over the one under the cursor (the
// player, 2026-10-11). 0 by default (original).
float g_uiHitScale = 0.0f;

static uint32_t NearestHit(float x, float y, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t iterator[2] = {0, 0};
    uint8_t* best = nullptr;
    float bestD = 1e30f;
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        const uint32_t flags = Field<uint32_t>(obj, 0x474);
        if (!(flags & 2) && (!(Field<uint8_t>(obj, 0x470) & 1) || !(flags & 4)))
            continue;
        if (!(VCall<uint8_t>(obj, 0x150 / 4) & 2) || !(Field<uint32_t>(obj, 0x474) & 0x100))
            continue;
        const bool exact = VCall<uint8_t>(obj, 0x140 / 4, a1, a2, a3) != 0;  // also lays the object out
        const float l = Field<float>(obj, 0x490), r = Field<float>(obj, 0x494);
        const float b = Field<float>(obj, 0x498), tp = Field<float>(obj, 0x49c);
        const float cx = 0.5f * (l + r), cy = 0.5f * (b + tp);
        const float hx = 0.5f * (r - l) * g_uiHitScale, hy = 0.5f * (tp - b) * g_uiHitScale;
        if (!exact && !(hx > 0.0f && hy > 0.0f && x >= cx - hx && x < cx + hx && y >= cy - hy && y < cy + hy))
            continue;
        const float dx = hx > 0.0f ? (x - cx) / hx : 0.0f, dy = hy > 0.0f ? (y - cy) / hy : 0.0f;
        const float d = dx * dx + dy * dy * 4.0f;  // rows matter more than columns
        if (d < bestD)
            bestD = d, best = obj;
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return u(best);
}

uint32_t fn_00463930(uint32_t arg0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (g_uiHitScale > 0.0f)
        return NearestHit(bf(a1), bf(a2), a1, a2, a3);
    (void)arg0;
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        const uint32_t flags = Field<uint32_t>(obj, 0x474);
        if (!(flags & 2) && (!(Field<uint8_t>(obj, 0x470) & 1) || !(flags & 4)))
            continue;
        if (!(VCall<uint8_t>(obj, 0x150 / 4) & 2))
            continue;
        if (!(Field<uint32_t>(obj, 0x474) & 0x100))
            continue;
        if (!VCall<uint8_t>(obj, 0x140 / 4, a1, a2, a3))
            continue;
        VCall<uint32_t>(P(iterator[0]), 0xc / 4, iterator[1]);
        --Field<int32_t>(P(iterator[1]), 0x18);
        return u(obj);
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return 0;
}

static uint32_t fn_00463930_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00463930(a0, a1, a2, a3);
}
ABI_ENTRY_00463930(fn_00463930_Entry, fn_00463930_Abi)
HOOK(0x00463930, "fn_00463930", fn_00463930_Entry, nullptr);

// 0x00463a00 (stack: unused, a1, a2, a3; callee pops 16; caller 0x00493130; not executed in recorded runs): as
// 0x00463930 but for objects whose virtual +0x150 lacks bit 1 (no +0x100 test).
uint32_t fn_00463a00(uint32_t /*arg0*/, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        const uint32_t flags = Field<uint32_t>(obj, 0x474);
        if (!(flags & 2) && (!(Field<uint8_t>(obj, 0x470) & 1) || !(flags & 4)))
            continue;
        if (VCall<uint8_t>(obj, 0x150 / 4) & 2)
            continue;
        if (!VCall<uint8_t>(obj, 0x140 / 4, a1, a2, a3))
            continue;
        VCall<uint32_t>(P(iterator[0]), 0xc / 4, iterator[1]);
        --Field<int32_t>(P(iterator[1]), 0x18);
        return u(obj);
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return 0;
}

static uint32_t fn_00463a00_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00463a00(a0, a1, a2, a3);
}
ABI_ENTRY_00463a00(fn_00463a00_Entry, fn_00463a00_Abi)
HOOK(0x00463a00, "fn_00463a00", fn_00463a00_Entry, nullptr);

// 0x00463ac0 (eax = HUD, ecx = position; caller 0x00491380): when "ui_mouse_cursor" (+0x54) exists, tail-jumps to
// 0x00457870 (al = 0, ecx, edx = cursor), which moves it. Returns eax as left.
uint32_t fn_00463ac0(uint8_t* hud, uint32_t ecx)
{
    const uint32_t cursor = Field<uint32_t>(hud, 0x54);
    if (!cursor)
        return u(hud);
    return static_cast<uint32_t>(orig::fn_00457870(u(hud) & 0xffffff00u, ecx, cursor, 0, 0, 0));
}

static uint32_t fn_00463ac0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00463ac0(B(eax), ecx);
}
ABI_ENTRY_00463ac0(fn_00463ac0_Entry, fn_00463ac0_Abi)
HOOK(0x00463ac0, "fn_00463ac0", fn_00463ac0_Entry, nullptr);

// 0x00463ad0 ObjectList_FindByName (ebx = second name or 0; stack: unused, name; callee pops 8): the first object of
// the UI list g+0xa84 +0xeb8 whose virtual +0x150 lacks bit 1 and whose name (Object_GetName 0x00453270) equals the
// name (_stricmp) and, with a second name, whose 0x00464ae0 string equals it too; 0 when none. Name from the behaviour
// (callers pass "ui_crosshair_friendly", "ui_tickerbomb_set", ...).
uint32_t ObjectList_FindByName(const char* secondName, uint32_t /*arg0*/, const char* name)
{
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        if (VCall<uint32_t>(obj, 0x150 / 4) & 2)
            continue;
        if (Stricmp(orig::fn_00453270(u(obj)), u(name)) != 0)
            continue;
        if (secondName && Stricmp(fn_00464ae0(obj), u(secondName)) != 0)
            continue;
        VCall<uint32_t>(P(iterator[0]), 0xc / 4, iterator[1]);
        --Field<int32_t>(P(iterator[1]), 0x18);
        return u(obj);
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return 0;
}

static uint32_t ObjectList_FindByName_Abi(uint32_t ebx, uint32_t a0, uint32_t a1)
{
    return ObjectList_FindByName(reinterpret_cast<const char*>(ebx), a0, reinterpret_cast<const char*>(a1));
}
ABI_ENTRY_00463ad0(ObjectList_FindByName_Entry, ObjectList_FindByName_Abi)
HOOK(0x00463ad0, "ObjectList_FindByName", ObjectList_FindByName_Entry, nullptr);

uint32_t fn_00463c50(uint32_t ecx, const char* name, uint8_t* hud);

// 0x00463b90 (eax = object name or 0, ecx = name index; stack: HUD, record id; callee pops 8): the UI object whose
// +0x478 equals the record id (-1 gives 0) and whose name index +0x46c equals the index (with a name: its index in
// the HUD's objnames list, 0x00463c50); an index out of the list (>= +0x7c) matches any. 0 when none.
uint32_t fn_00463b90(const char* name, uint32_t index, uint8_t* hud, uint32_t id)
{
    if (id == 0xffffffffu)
        return 0;
    uint32_t iterator[2] = {0, 0};
    if (name)
        index = fn_00463c50(0, name, hud);
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        if (Field<uint32_t>(obj, 0x478) != id)
            continue;
        if (index < Field<uint32_t>(hud, 0x7c) && index != Field<uint32_t>(obj, 0x46c))
            continue;
        VCall<uint32_t>(P(iterator[0]), 0xc / 4, iterator[1]);
        --Field<int32_t>(P(iterator[1]), 0x18);
        return u(obj);
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return 0;
}

static uint32_t fn_00463b90_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00463b90(reinterpret_cast<const char*>(eax), ecx, B(a0), a1);
}
ABI_ENTRY_00463b90(fn_00463b90_Entry, fn_00463b90_Abi)
HOOK(0x00463b90, "fn_00463b90", fn_00463b90_Entry, nullptr);

// 0x00463c50 (ecx unused, ebx = name, edi = HUD): index of the name in the HUD's objnames list (+0x84, count +0x7c;
// _stricmp), -1 when absent.
uint32_t fn_00463c50(uint32_t /*ecx*/, const char* name, uint8_t* hud)
{
    for (uint32_t i = 0; i < Field<uint32_t>(hud, 0x7c); ++i)
        if (Stricmp(u(name), Field<uint32_t*>(hud, 0x84)[i]) == 0)
            return i;
    return 0xffffffffu;
}

static uint32_t fn_00463c50_Abi(uint32_t ecx, uint32_t ebx, uint32_t edi)
{
    return fn_00463c50(ecx, reinterpret_cast<const char*>(ebx), B(edi));
}
ABI_ENTRY_00463c50(fn_00463c50_Entry, fn_00463c50_Abi)
HOOK(0x00463c50, "fn_00463c50", fn_00463c50_Entry, nullptr);

// 0x00463c90 (ebx = name; stack: unused, callee pops 4): the object of list g+0xa84 +0xec0 named so (Object_GetName
// 0x00453270, _stricmp); 0 when none.
uint32_t fn_00463c90(const char* name, uint32_t /*arg0*/)
{
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xec0), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        if (Stricmp(orig::fn_00453270(u(obj)), u(name)) != 0)
            continue;
        VCall<uint32_t>(P(iterator[0]), 0xc / 4, iterator[1]);
        --Field<int32_t>(P(iterator[1]), 0x18);
        return u(obj);
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return 0;
}

static uint32_t fn_00463c90_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_00463c90(reinterpret_cast<const char*>(ebx), a0);
}
ABI_ENTRY_00463c90(fn_00463c90_Entry, fn_00463c90_Abi)
HOOK(0x00463c90, "fn_00463c90", fn_00463c90_Entry, nullptr);

// 0x00463d20 (ecx unused, ebx = name, edi = HUD; caller 0x00493130; not executed in recorded runs): index of the name
// in the HUD's refnames list (+0x88, count +0x80), -1 when absent.
uint32_t fn_00463d20(uint32_t /*ecx*/, const char* name, uint8_t* hud)
{
    for (uint32_t i = 0; i < Field<uint32_t>(hud, 0x80); ++i)
        if (Stricmp(u(name), Field<uint32_t*>(hud, 0x88)[i]) == 0)
            return i;
    return 0xffffffffu;
}

static uint32_t fn_00463d20_Abi(uint32_t ecx, uint32_t ebx, uint32_t edi)
{
    return fn_00463d20(ecx, reinterpret_cast<const char*>(ebx), B(edi));
}
ABI_ENTRY_00463d20(fn_00463d20_Entry, fn_00463d20_Abi)
HOOK(0x00463d20, "fn_00463d20", fn_00463d20_Entry, nullptr);

// 0x00463d60 (ebx: bl = keep shown; stack: unused, callee pops 4): pushes every UI object's shown bit (+0x470 bit 0)
// onto its history (+0x470 = (v << 1) | (v & 1)), clearing bit 0 unless bl. Returns the iterator node.
uint32_t fn_00463d60(uint32_t ebx, uint32_t /*arg0*/)
{
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        const uint32_t v = Field<uint32_t>(obj, 0x470);
        const uint32_t n = (v & 1) | (v + v);
        Field<uint32_t>(obj, 0x470) = n;
        if (!static_cast<uint8_t>(ebx))
            Field<uint32_t>(obj, 0x470) = n & 0xfffffffeu;
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_00463d60_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_00463d60(ebx, a0);
}
ABI_ENTRY_00463d60(fn_00463d60_Entry, fn_00463d60_Abi)
HOOK(0x00463d60, "fn_00463d60", fn_00463d60_Entry, nullptr);

// 0x00463df0 (stack: unused, callee pops 4): pops every UI object's shown history: +0x470 = v >> 1 (stored twice: first
// with v's bit 0 kept, then bit 0 = v's bit 1). Returns the iterator node.
uint32_t fn_00463df0(uint32_t /*arg0*/)
{
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        const uint32_t v = Field<uint32_t>(obj, 0x470);
        const uint32_t h = v >> 1;
        uint32_t n = ((v ^ h) & 1) ^ h;
        Field<uint32_t>(obj, 0x470) = n;
        n = (h & 1) ? (n | 1) : (n & 0xfffffffeu);
        Field<uint32_t>(obj, 0x470) = n;
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_00463df0_Abi(uint32_t a0)
{
    return fn_00463df0(a0);
}
ABI_ENTRY_00463df0(fn_00463df0_Entry, fn_00463df0_Abi)
HOOK(0x00463df0, "fn_00463df0", fn_00463df0_Entry, nullptr);

// 0x00463e80 (ebx = HUD; stack: name, show (byte), flag 4 (byte), unused; callee pops 16): every UI object whose
// objnames entry (HUD +0x84 [+0x46c]) equals the name gets shown bit +0x470 bit 0 = show and, with the third
// argument, +0x474 |= 4. Returns the iterator node.
uint32_t fn_00463e80(uint8_t* hud, uint32_t name, uint32_t show, uint32_t flag4, uint32_t /*a3*/)
{
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        if (Stricmp(name, Field<uint32_t*>(hud, 0x84)[Field<uint32_t>(obj, 0x46c)]) != 0)
            continue;
        if (static_cast<uint8_t>(show))
            Field<uint32_t>(obj, 0x470) = Field<uint32_t>(obj, 0x470) | 1;
        else
            Field<uint32_t>(obj, 0x470) = Field<uint32_t>(obj, 0x470) & 0xfffffffeu;
        if (static_cast<uint8_t>(flag4))
            Field<uint32_t>(obj, 0x474) |= 4;
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_00463e80_Abi(uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00463e80(B(ebx), a0, a1, a2, a3);
}
ABI_ENTRY_00463e80(fn_00463e80_Entry, fn_00463e80_Abi)
HOOK(0x00463e80, "fn_00463e80", fn_00463e80_Entry, nullptr);

// 0x00463f30 (stack: HUD (unused), callee pops 4; caller Hud_CreateLevelUi): hides (+0x470 bit 0 cleared) every UI
// object that has a name index (+0x46c != -1). Returns the iterator node.
uint32_t fn_00463f30(uint8_t* /*hud*/)
{
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        if (Field<uint32_t>(obj, 0x46c) != 0xffffffffu)
            Field<uint32_t>(obj, 0x470) &= 0xfffffffeu;
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_00463f30_Abi(uint32_t a0)
{
    return fn_00463f30(B(a0));
}
ABI_ENTRY_00463f30(fn_00463f30_Entry, fn_00463f30_Abi)
HOOK(0x00463f30, "fn_00463f30", fn_00463f30_Entry, nullptr);

namespace
{
// |sin(x) * k| + one, in the x87 order of 0x00463fa0 (fsin keeps its full result until the fmul rounds it).
float PulseScale(const float* x, const float* k, const float* one)
{
    float result;
    __asm {
        mov eax, x
        mov ecx, k
        mov edx, one
        fld dword ptr [eax]
        fsin
        fmul dword ptr [ecx]
        fabs
        fadd dword ptr [edx]
        fstp dword ptr [result]
    }
    return result;
}
inline void SetShown(uint32_t widget, bool on)
{
    const uint32_t v = Field<uint32_t>(P(widget), 0x474);
    Field<uint32_t>(P(widget), 0x474) = on ? (v | 4) : (v & 0xfffffffbu);
}
}  // namespace

// 0x00463fa0 (stack: HUD, callee pops 4; caller 0x00402e80): per-frame HUD widgets. Pulse: +0x94 = |sin(+0x98) *
// 0.15| + 1, +0x98 += dt (0x00604a90) * 5 wrapped to (-pi, pi]. With a player (g+0x748) and no g+0x26cc: the weapon
// icons +0x18.. (6, shown for the current weapon 0x004a1660), +0x30 shown, "ui_ammo_infinity" +0x58 for weapon 2, the
// special icons +0x34.. (5, for 0x004a1670 == 6 + i) and, for special weapon 10, the frame of the animation +0x44
// (0x00408800 (eax = 4), value 1.0 when *0x00408000 (eax = g+0xa6c) is set, Anim_Set (eax = 1; stack: 0, 0x210, 0,
// 0, 0, 0)); otherwise all hidden. The 8 widgets +0x5c.. are shown by 8 flags: buttons 0x10/0x11 with more than one
// weapon (0x004a1640), 0xf/0xe with more than one special (0x004a1610) (0x00414810, al = 1, esi = g+0xa80; stack:
// 0.5), then the four counts > 1 again; all off in cut scenes. Returns eax as left.
uint32_t fn_00463fa0(uint8_t* hud)
{
    uint8_t flags[8];
    Field<float>(hud, 0x94) = PulseScale(&Field<float>(hud, 0x98), reinterpret_cast<const float*>(0x005bcedc),
                                         reinterpret_cast<const float*>(0x005bcb2c));
    float phase = GameGlobal<float>(0x00604a90) * K(0x005bcbdc) + Field<float>(hud, 0x98);
    Field<float>(hud, 0x98) = phase;
    if (phase > K(0x005bcb70))
    {
        phase = Field<float>(hud, 0x98);
        do
            phase = phase - K(0x005bcb74);
        while (phase > K(0x005bcb70));
        Field<float>(hud, 0x98) = phase;
    }
    uint8_t* g = G();
    if (Field<uint32_t>(g, 0x748) && !Field<uint32_t>(g, 0x26cc))
    {
        for (uint32_t i = 0; i < 6; ++i)
        {
            const uint32_t w = Field<uint32_t>(hud, 0x18 + i * 4);
            SetShown(w, orig::fn_004a1660(Field<uint32_t>(g, 0x748) + 0xc24) == i);
        }
        if (Field<uint32_t>(hud, 0x30))
            Field<uint32_t>(Field<void*>(hud, 0x30), 0x474) |= 4;
        const uint32_t infinity = Field<uint32_t>(hud, 0x58);
        if (infinity)
            SetShown(infinity, orig::fn_004a1660(Field<uint32_t>(g, 0x748) + 0xc24) == 2);
        for (uint32_t i = 0; i < 5; ++i)
        {
            const uint32_t w = Field<uint32_t>(hud, 0x34 + i * 4);
            SetShown(w, orig::fn_004a1670(Field<uint32_t>(g, 0x748) + 0xc24) == i + 6);
        }
        if (orig::fn_004a1670(Field<uint32_t>(g, 0x748) + 0xc24) == 0xa)
        {
            const uint32_t frame = orig::fn_00408800(4, Field<uint32_t>(hud, 0x44));
            const uint32_t* state = reinterpret_cast<const uint32_t*>(orig::fn_00408000(Field<uint32_t>(g, 0xa6c)));
            Field<float>(P(frame), 4) = *state != 0 ? K(0x005bcb2c) : K(0x0056c05c);
            orig::fn_00408d90(1, Field<uint32_t>(hud, 0x44), 0, 0x210, 0, 0, 0, 0);
            g = G();
        }
    }
    else
    {
        for (uint32_t i = 0; i < 6; ++i)
            Field<uint32_t>(Field<void*>(hud, 0x18 + i * 4), 0x474) &= 0xfffffffbu;
        for (uint32_t i = 0; i < 5; ++i)
            Field<uint32_t>(Field<void*>(hud, 0x34 + i * 4), 0x474) &= 0xfffffffbu;
        if (Field<uint32_t>(hud, 0x30))
            Field<uint32_t>(Field<void*>(hud, 0x30), 0x474) &= 0xfffffffbu;
        if (Field<uint32_t>(hud, 0x58))
            Field<uint32_t>(Field<void*>(hud, 0x58), 0x474) &= 0xfffffffbu;
    }
    if (Field<uint32_t>(g, 0x2684) == 0 && Field<uint32_t>(g, 0x748) && !Field<uint32_t>(g, 0x26cc))
    {
        const uint32_t buttons[4] = {0x10, 0x11, 0xf, 0xe};
        for (uint32_t i = 0; i < 4; ++i)
        {
            const uint32_t block = Field<uint32_t>(G(), 0x748) + 0xc24;
            const uint32_t count = i < 2 ? orig::fn_004a1640(block) : orig::fn_004a1610(block);
            flags[i] = 0;
            if (count > 1 && static_cast<uint8_t>(orig::fn_00414810((count & 0xffffff00u) | 1, buttons[i],
                                                                     Field<uint32_t>(G(), 0xa80), 0x3f000000u)))
                flags[i] = 1;
        }
        const uint32_t block = Field<uint32_t>(G(), 0x748) + 0xc24;
        flags[4] = orig::fn_004a1640(block) > 1 ? 1 : 0;
        flags[5] = orig::fn_004a1640(block) > 1 ? 1 : 0;
        flags[6] = orig::fn_004a1610(block) > 1 ? 1 : 0;
        flags[7] = orig::fn_004a1610(block) > 1 ? 1 : 0;
    }
    else
    {
        std::memset(flags, 0, sizeof(flags));
    }
    uint32_t w = 0;
    for (uint32_t i = 0; i < 8; ++i)
    {
        w = Field<uint32_t>(hud, 0x5c + i * 4);
        if (w)
            SetShown(w, flags[i] != 0);
    }
    return w;
}

static uint32_t fn_00463fa0_Abi(uint32_t a0)
{
    return fn_00463fa0(B(a0));
}
ABI_ENTRY_00463fa0(fn_00463fa0_Entry, fn_00463fa0_Abi)
HOOK(0x00463fa0, "fn_00463fa0", fn_00463fa0_Entry, nullptr);

// ================================================================================================================
// UI object base (fields +0x460..+0x49c; vtable slots +0x140 hit test, +0x144 layout, +0x150 kind bits)
// ================================================================================================================

// 0x004643b0 (eax = destination, ecx = source; caller 0x00406840): copies 16 bytes. Returns the destination.
uint32_t fn_004643b0(uint32_t* dst, const uint32_t* src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    return u(dst);
}

static uint32_t fn_004643b0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004643b0(reinterpret_cast<uint32_t*>(eax), reinterpret_cast<const uint32_t*>(ecx));
}
ABI_ENTRY_004643b0(fn_004643b0_Entry, fn_004643b0_Abi)
HOOK(0x004643b0, "fn_004643b0", fn_004643b0_Entry, nullptr);

// 0x004643d0 (ecx = 4 slots; stack: value, callee pops 4; caller 0x00466530; not executed in recorded runs): stores
// the value in the first empty slot. Returns the slot index (4 when full).
uint32_t fn_004643d0(uint32_t* slots, uint32_t value)
{
    uint32_t i = 0;
    for (; i < 4; ++i)
    {
        if (slots[i] == 0)
        {
            slots[i] = value;
            return i;
        }
    }
    return i;
}

static uint32_t fn_004643d0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004643d0(reinterpret_cast<uint32_t*>(ecx), a0);
}
ABI_ENTRY_004643d0(fn_004643d0_Entry, fn_004643d0_Abi)
HOOK(0x004643d0, "fn_004643d0", fn_004643d0_Entry, nullptr);

// 0x004643f0 (eax = object, ecx = value; caller 0x00466430; not executed in recorded runs): +0x14 = value. Returns
// the object.
uint32_t fn_004643f0(uint8_t* obj, uint32_t value)
{
    Field<uint32_t>(obj, 0x14) = value;
    return u(obj);
}

static uint32_t fn_004643f0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004643f0(B(eax), ecx);
}
ABI_ENTRY_004643f0(fn_004643f0_Entry, fn_004643f0_Abi)
HOOK(0x004643f0, "fn_004643f0", fn_004643f0_Entry, nullptr);

// 0x00464400 (eax = object; stack: a0, a1, a2, a3 (byte); callee pops 16; caller 0x00466077): forwards to
// 0x00466470 (al = a3, upper bytes of eax a1's; ecx = +0x14; stack: a0, a1, a2) when +0x14 is set. Returns its result,
// else the object.
uint32_t fn_00464400(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t target = Field<uint32_t>(obj, 0x14);
    if (!target)
        return u(obj);
    return orig::fn_00466470((a1 & 0xffffff00u) | (a3 & 0xffu), target, a0, a1, a2);
}

static uint32_t fn_00464400_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00464400(B(eax), a0, a1, a2, a3);
}
ABI_ENTRY_00464400(fn_00464400_Entry, fn_00464400_Abi)
HOOK(0x00464400, "fn_00464400", fn_00464400_Entry, nullptr);

// 0x00464430 (eax = object, ebx = HUD; caller 0x004a33d0): adds the object to the HUD's list +0x9c (0x004546d0,
// esi = list, edi = object) unless 0x00454690 finds it there. Returns eax as left.
uint32_t fn_00464430(uint32_t obj, uint8_t* hud)
{
    if (!obj)
        return 0;
    const uint32_t r = orig::fn_00454690(Field<uint32_t>(hud, 0x9c), obj);
    if (static_cast<uint8_t>(r))
        return r;
    return static_cast<uint32_t>(orig::fn_004546d0(Field<uint32_t>(hud, 0x9c), obj));
}

static uint32_t fn_00464430_Abi(uint32_t eax, uint32_t ebx)
{
    return fn_00464430(eax, B(ebx));
}
ABI_ENTRY_00464430(fn_00464430_Entry, fn_00464430_Abi)
HOOK(0x00464430, "fn_00464430", fn_00464430_Entry, nullptr);

// 0x004648a0 (no inputs; virtual, e.g. slot +0x150 of several UI vtables (0x005a1610, 0x005a22e8, ...) and slot
// +0x168 of CLASS(StigTurret) 0x005a19d8; not executed in recorded runs): returns 1.
uint32_t fn_004648a0()
{
    return 1;
}

static uint32_t fn_004648a0_Abi()
{
    return fn_004648a0();
}
ABI_ENTRY_004648a0(fn_004648a0_Entry, fn_004648a0_Abi)
HOOK(0x004648a0, "fn_004648a0", fn_004648a0_Entry, nullptr);

// 0x004648b0 (ecx = UI object; stack: a0..a3, callee pops 16): base init: +0x424 |= 0x10000, 0x004517b0 (ecx =
// object; stack: a0..a3), the record fields +0x460/+0x468/+0x46c/+0x478..+0x488 = -1, flags +0x474 = 0, +0x464 = 0,
// shown +0x470 = 1, rectangle +0x490..+0x49c = 0, virtual +0xf4 (0), flags |= 8, then adds it to lists g+0xa84 +0xe74
// and +0xeb8 (0x004546d0). Returns the last call's eax.
uint32_t fn_004648b0(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Field<uint32_t>(obj, 0x424) = Field<uint32_t>(obj, 0x424) | 0x10000;
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    Field<uint32_t>(obj, 0x478) = 0xffffffffu;
    Field<uint32_t>(obj, 0x46c) = 0xffffffffu;
    Field<uint32_t>(obj, 0x468) = 0xffffffffu;
    Field<uint32_t>(obj, 0x460) = 0xffffffffu;
    Field<uint32_t>(obj, 0x488) = 0xffffffffu;
    Field<uint32_t>(obj, 0x484) = 0xffffffffu;
    Field<uint32_t>(obj, 0x480) = 0xffffffffu;
    Field<uint32_t>(obj, 0x47c) = 0xffffffffu;
    Field<uint32_t>(obj, 0x474) = 0;
    Field<uint32_t>(obj, 0x464) = 0;
    Field<uint32_t>(obj, 0x470) = 1;
    Field<uint32_t>(obj, 0x490) = 0;
    Field<uint32_t>(obj, 0x494) = 0;
    Field<uint32_t>(obj, 0x498) = 0;
    Field<uint32_t>(obj, 0x49c) = 0;
    VCall<uint32_t>(obj, 0xf4 / 4, 0u);
    Field<uint32_t>(obj, 0x474) |= 8;
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe74), u(obj));
    return static_cast<uint32_t>(orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb8), u(obj)));
}

static uint32_t fn_004648b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004648b0(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004648b0(fn_004648b0_Entry, fn_004648b0_Abi)
HOOK(0x004648b0, "fn_004648b0", fn_004648b0_Entry, nullptr);

// 0x00464990 (ecx = UI object; stack: x, y, unused; callee pops 12; virtual slot +0x140 of the UI vtables, the hit test
// 0x00463930 calls): lays the object out first when +0x474 has 0x2000 (virtual +0x144), then returns 1 when (x, y) lies
// in the rectangle +0x490 (left), +0x494 (right, exclusive), +0x498 (top), +0x49c (bottom, exclusive).
uint32_t fn_00464990(uint8_t* obj, float x, float y, uint32_t /*arg2*/)
{
    if (Field<uint32_t>(obj, 0x474) & 0x2000)
        VCall<uint32_t>(obj, 0x144 / 4);
    if (!(x >= Field<float>(obj, 0x490)))
        return 0;
    if (!(x < Field<float>(obj, 0x494)))
        return 0;
    if (!(y >= Field<float>(obj, 0x498)))
        return 0;
    if (!(y < Field<float>(obj, 0x49c)))
        return 0;
    return 1;
}

static uint32_t fn_00464990_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00464990(B(ecx), bf(a0), bf(a1), a2);
}
ABI_ENTRY_00464990(fn_00464990_Entry, fn_00464990_Abi)
HOOK(0x00464990, "fn_00464990", fn_00464990_Entry, nullptr);

// 0x00464a00 (ecx = UI object; caller 0x00467f50): clears the layout-pending flag 0x2000 of +0x474. Returns the flags.
uint32_t fn_00464a00(uint8_t* obj)
{
    const uint32_t v = Field<uint32_t>(obj, 0x474) & 0xffffdfffu;
    Field<uint32_t>(obj, 0x474) = v;
    return v;
}

static uint32_t fn_00464a00_Abi(uint32_t ecx)
{
    return fn_00464a00(B(ecx));
}
ABI_ENTRY_00464a00(fn_00464a00_Entry, fn_00464a00_Abi)
HOOK(0x00464a00, "fn_00464a00", fn_00464a00_Entry, nullptr);

uint32_t fn_00464ae0(uint8_t* obj);

// 0x00464a10 (ecx = UI object; stack: menu, input (pointer to direction bits); callee pops 8): moves the focus. The
// first set bit of *input (1 -> link 2, 2 -> 3, 4 -> 0, 8 -> 1) picks a link +0x47c[link] (record id, -1 = none); the
// linked object (0x00463b90, eax = 0, ecx = this object's name index +0x46c; stack: g+0xa70, id) is tried with
// 0x00491b20 (esi = object, edi = menu), following that object's same link up to 16 steps. Returns al = focus moved.
uint32_t fn_00464a10(uint8_t* obj, uint32_t menu, const uint32_t* input)
{
    if (!menu)
        return 0;
    const uint32_t bits = *input;
    if (!bits)
        return 0;
    uint32_t link;
    if (bits & 1)
        link = 2;
    else if (bits & 2)
        link = 3;
    else if (bits & 4)
        link = 0;
    else if (bits & 8)
        link = 1;
    else
        return 0;
    const uint32_t* next = &Field<uint32_t>(obj, 0x47c + link * 4);
    if (*next == 0xffffffffu)
        return 0;
    for (uint32_t steps = 0;;)
    {
        const uint32_t found = fn_00463b90(nullptr, Field<uint32_t>(obj, 0x46c), B(Field<uint32_t>(G(), 0xa70)), *next);
        if (!found)
            return 0;
        if (++steps >= 0x10)
            return 0;
        if (static_cast<uint8_t>(orig::fn_00491b20(found, menu)))
            return 1;
        next = &Field<uint32_t>(P(found), 0x47c + link * 4);
        if (*next == 0xffffffffu)
            return 0;
    }
}

static uint32_t fn_00464a10_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00464a10(B(ecx), a0, reinterpret_cast<const uint32_t*>(a1));
}
ABI_ENTRY_00464a10(fn_00464a10_Entry, fn_00464a10_Abi)
HOOK(0x00464a10, "fn_00464a10", fn_00464a10_Entry, nullptr);

// 0x00464ae0 (eax = UI object): its refnames entry: (g+0xa70) +0x88 [+0x468], 0 when +0x468 is -1.
uint32_t fn_00464ae0(uint8_t* obj)
{
    const uint32_t index = Field<uint32_t>(obj, 0x468);
    if (index == 0xffffffffu)
        return 0;
    return Field<uint32_t*>(Field<void*>(G(), 0xa70), 0x88)[index];
}

static uint32_t fn_00464ae0_Abi(uint32_t eax)
{
    return fn_00464ae0(B(eax));
}
ABI_ENTRY_00464ae0(fn_00464ae0_Entry, fn_00464ae0_Abi)
HOOK(0x00464ae0, "fn_00464ae0", fn_00464ae0_Entry, nullptr);

// 0x00464b10 (esi = UI object): places the object on its anchor. The anchor is the object of list g+0xa84 +0xec0 named
// by the HUD's objnames entry +0x46c (0x00463c90, stored at +0x48c); its transform for the refnames entry (0x00453c50
// (anchor, 0x00464ae0 name, &matrix, 1, 0, 0, 1, 0, 0), 0x44-byte local matrix) is applied with 0x00457990 (al = 0;
// stack: object, &matrix). Objects whose virtual +0x150 lacks bit 1 get +0x424 |= 0x400 and +0x474 |= 0x2000; the
// others get their screen position from the matrix (x = m[3] / 640, y = m[7] / 480, with +0x474 & 0x20000 x is
// corrected for the aspect: (x - 0.5) * (renderer virtual +0x88 * 4/3) / (w / h of renderer virtual +0x7c) + 0.5)
// via 0x004659e0 (stack: x, y, 0.5, 0.5). Returns eax as left.
uint32_t fn_00464b10(uint8_t* obj)
{
    alignas(4) uint8_t matrix[0x44];
    if (Field<uint32_t>(obj, 0x46c) == 0xffffffffu)
        return Field<uint32_t>(obj, 0x46c);
    const uint32_t hud = Field<uint32_t>(G(), 0xa70);
    const uint32_t anchor = fn_00463c90(
        reinterpret_cast<const char*>(Field<uint32_t*>(P(hud), 0x84)[Field<uint32_t>(obj, 0x46c)]), hud);
    Field<uint32_t>(obj, 0x48c) = anchor;
    if (!anchor)
        return 0;
    const uint32_t r = orig::fn_00453c50(anchor, fn_00464ae0(obj), u(matrix), 1, 0, 0, 1, 0, 0);
    orig::fn_00457990(r & 0xffffff00u, u(obj), u(matrix));
    if (!(VCall<uint32_t>(obj, 0x150 / 4) & 2))
    {
        Field<uint32_t>(obj, 0x424) |= 0x400;
        Field<uint32_t>(obj, 0x474) |= 0x2000;
        return Field<uint32_t>(obj, 0x474);
    }
    if (Field<uint32_t>(obj, 0x474) & 0x20000)
    {
        const float* view = VCall<const float*>(Field<void*>(G(), 0x38), 0x7c / 4);
        const float scale = VCall<float>(Field<void*>(G(), 0x38), 0x88 / 4) * K(0x005bcd90);
        const float aspect = scale / (view[0] / view[1]);
        const float y = Field<float>(matrix, 0x1c) * K(0x005bcbf4);
        float x = Field<float>(matrix, 0xc) * K(0x005bcbf8) - K(0x005bcb4c);
        x = x * aspect + K(0x005bcb4c);
        return orig::fn_004659e0(u(obj), fb(x), fb(y), 0x3f000000u, 0x3f000000u);
    }
    const float y = Field<float>(matrix, 0x1c) * K(0x005bcbf4);
    const float x = Field<float>(matrix, 0xc) * K(0x005bcbf8);
    return orig::fn_004659e0(u(obj), fb(x), fb(y), 0x3f000000u, 0x3f000000u);
}

static uint32_t fn_00464b10_Abi(uint32_t esi)
{
    return fn_00464b10(B(esi));
}
ABI_ENTRY_00464b10(fn_00464b10_Entry, fn_00464b10_Abi)
HOOK(0x00464b10, "fn_00464b10", fn_00464b10_Entry, nullptr);

// 0x00464c60 (eax = UI object; stack: new refnames index, callee pops 4; caller 0x00493130; not executed in recorded
// runs): starts a move to another anchor point: +0x464 = time (0x00604a98), +0x460 = old +0x468, +0x468 = new. Returns
// the object.
uint32_t fn_00464c60(uint8_t* obj, uint32_t index)
{
    const uint32_t now = GameGlobal<uint32_t>(0x00604a98);
    const uint32_t old = Field<uint32_t>(obj, 0x468);
    Field<uint32_t>(obj, 0x464) = now;
    Field<uint32_t>(obj, 0x460) = old;
    Field<uint32_t>(obj, 0x468) = index;
    return u(obj);
}

static uint32_t fn_00464c60_Abi(uint32_t eax, uint32_t a0)
{
    return fn_00464c60(B(eax), a0);
}
ABI_ENTRY_00464c60(fn_00464c60_Entry, fn_00464c60_Abi)
HOOK(0x00464c60, "fn_00464c60", fn_00464c60_Entry, nullptr);

// 0x00464c90 (esi = UI object; caller 0x00493130; not executed in recorded runs): advances the move started by
// 0x00464c60: t = (time - +0x464) * 10, clamped to 1 (ending the move: +0x460 = -1, +0x464 = 0); the anchor's
// transforms for the new (+0x468) and old (+0x460) refnames points (0x00453c50 into two local matrices) are blended
// in elements 3, 7, 11 as old * (1 - t) + new * t and applied with 0x00457990 (al = 0; stack: object, &new matrix).
uint32_t fn_00464c90(uint8_t* obj)
{
    alignas(4) uint8_t frame[0x88];  // two 0x44-byte matrices, as in the original frame
    uint8_t* target = frame;
    uint8_t* source = frame + 0x44;
    if (Field<uint32_t>(obj, 0x460) == 0xffffffffu || Field<uint32_t>(obj, 0x468) == 0xffffffffu)
        return 0;
    if (Field<float>(obj, 0x464) == K(0x0056c05c))
        return 0;
    if (!Field<uint32_t>(obj, 0x48c))
    {
        const uint32_t hud = Field<uint32_t>(G(), 0xa70);
        Field<uint32_t>(obj, 0x48c) = fn_00463c90(
            reinterpret_cast<const char*>(Field<uint32_t*>(P(hud), 0x84)[Field<uint32_t>(obj, 0x46c)]), hud);
    }
    const uint32_t anchor = Field<uint32_t>(obj, 0x48c);
    if (!anchor)
        return 0;
    orig::fn_00453c50(anchor,
                      Field<uint32_t*>(Field<void*>(G(), 0xa70), 0x88)[Field<uint32_t>(obj, 0x468)], u(target), 1,
                      0, 0, 1, 0, 0);
    orig::fn_00453c50(Field<uint32_t>(obj, 0x48c),
                      Field<uint32_t*>(Field<void*>(G(), 0xa70), 0x88)[Field<uint32_t>(obj, 0x460)], u(source), 1,
                      0, 0, 1, 0, 0);
    float t = (GameGlobal<float>(0x00604a98) - Field<float>(obj, 0x464)) * K(0x005bcb68);
    if (t > K(0x005bcb2c))
    {
        Field<uint32_t>(obj, 0x460) = 0xffffffffu;
        t = K(0x005bcb2c);
        Field<uint32_t>(obj, 0x464) = 0;
    }
    const float rest = K(0x005bcb2c) - t;
    float v = Field<float>(source, 0xc) * rest;
    Field<float>(target, 0xc) = v + Field<float>(target, 0xc) * t;
    v = Field<float>(source, 0x1c) * rest;
    Field<float>(target, 0x1c) = v + Field<float>(target, 0x1c) * t;
    v = rest * Field<float>(source, 0x2c);
    Field<float>(target, 0x2c) = v + Field<float>(target, 0x2c) * t;
    return orig::fn_00457990(0, u(obj), u(target));
}

static uint32_t fn_00464c90_Abi(uint32_t esi)
{
    return fn_00464c90(B(esi));
}
ABI_ENTRY_00464c90(fn_00464c90_Entry, fn_00464c90_Abi)
HOOK(0x00464c90, "fn_00464c90", fn_00464c90_Entry, nullptr);

// 0x00464e10 (ecx = object; virtual: slot +0x9c of many vtables (e.g. 0x005a1610) and +0x20c of CLASS(PlayerWeapon)
// 0x005a1348; not executed in recorded runs): +0x374 = 0. Returns 0.
uint32_t fn_00464e10(uint8_t* obj)
{
    Field<uint32_t>(obj, 0x374) = 0;
    return 0;
}

static uint32_t fn_00464e10_Abi(uint32_t ecx)
{
    return fn_00464e10(B(ecx));
}
ABI_ENTRY_00464e10(fn_00464e10_Entry, fn_00464e10_Abi)
HOOK(0x00464e10, "fn_00464e10", fn_00464e10_Entry, nullptr);

// ================================================================================================================
// Text widgets (+0x4a0 text key, +0x4c0 string, +0x4c4 owned buffer, +0x4d0/+0x4d4 bound keys)
// ================================================================================================================

uint32_t fn_004652b0(uint32_t key2, uint8_t* text, uint32_t key1);

namespace
{
// 0x00465970 (esi = text widget; stack: format, ...; caller pops): formats a wide string (_vswprintf 0x00538a66 into
// a 0x1000-byte frame buffer) and sets it (0x004658a0). symbols/abi.csv lists 2 stack arguments and ecx; it is
// variadic and ecx is only clobbered by _alloca_probe, so 0x004652b0's three arguments are pushed by hand here.
uint32_t Fn00465970(uint32_t esiValue, uint32_t format, uint32_t a1, uint32_t a2)
{
    static const uint32_t target = 0x00465970;
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov eax, a2
        mov ecx, a1
        mov edx, format
        mov esi, esiValue
        push eax
        push ecx
        push edx
        call dword ptr [target]
        add esp, 12
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}
}  // namespace

// 0x00465130 (ecx = UI object; stack: flag (byte), callee pops 4): sets (flag) or clears flag 0x80 of +0x474. Returns
// the flags.
uint32_t fn_00465130(uint8_t* obj, uint32_t flag)
{
    const uint32_t v = Field<uint32_t>(obj, 0x474);
    const uint32_t n = static_cast<uint8_t>(flag) ? (v | 0x80) : (v & 0xffffff7fu);
    Field<uint32_t>(obj, 0x474) = n;
    return n;
}

static uint32_t fn_00465130_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00465130(B(ecx), a0);
}
ABI_ENTRY_00465130(fn_00465130_Entry, fn_00465130_Abi)
HOOK(0x00465130, "fn_00465130", fn_00465130_Entry, nullptr);

// 0x00465160 (ecx = text widget): frees its own string buffer +0x4c4 (heap 0x0063a210) and points +0x4c0 back at the
// empty string 0x005a0d1c, then tail-jumps to Object_Remove 0x004547b0 (ecx = widget).
uint32_t fn_00465160(uint8_t* text)
{
    if (Field<uint32_t>(text, 0x4c4))
    {
        orig::fn_004e08d0(0x0063a210u, Field<uint32_t>(text, 0x4c4));
        Field<uint32_t>(text, 0x4c4) = 0;
        Field<uint32_t>(text, 0x4c0) = 0x005a0d1c;
    }
    return orig::fn_004547b0(u(text));
}

static uint32_t fn_00465160_Abi(uint32_t ecx)
{
    return fn_00465160(B(ecx));
}
ABI_ENTRY_00465160(fn_00465160_Entry, fn_00465160_Abi)
HOOK(0x00465160, "fn_00465160", fn_00465160_Entry, nullptr);

// 0x004651a0 (ecx = text widget; stack: a0..a3, callee pops 16): init: UI base init 0x004648b0 (same arguments),
// scales +0x4c8/+0x4cc = 1.0, flags +0x474 |= 0x200, keys +0x4d0/+0x4d4 = 0, string +0x4c0 = 0x005a0d1c (empty),
// +0x4c4/+0x4d8/+0x4dc/+0x4e0 = 0, text key +0x4a0 (32 bytes) cleared. Returns 0.
uint32_t fn_004651a0(uint8_t* text, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    fn_004648b0(text, a0, a1, a2, a3);
    Field<uint32_t>(text, 0x4cc) = 0x3f800000u;
    Field<uint32_t>(text, 0x4c8) = 0x3f800000u;
    Field<uint32_t>(text, 0x474) = Field<uint32_t>(text, 0x474) | 0x200;
    Field<uint32_t>(text, 0x4d4) = 0;
    Field<uint32_t>(text, 0x4d0) = 0;
    Field<uint32_t>(text, 0x4c0) = 0x005a0d1c;
    Field<uint32_t>(text, 0x4c4) = 0;
    Field<uint32_t>(text, 0x4e0) = 0;
    Field<uint32_t>(text, 0x4d8) = 0;
    Field<uint32_t>(text, 0x4dc) = 0;
    std::memset(text + 0x4a0, 0, 8 * 4);
    return 0;
}

static uint32_t fn_004651a0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004651a0(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004651a0(fn_004651a0_Entry, fn_004651a0_Abi)
HOOK(0x004651a0, "fn_004651a0", fn_004651a0_Entry, nullptr);

// 0x00465230 (ecx = text widget; stack: a0..a3, callee pops 16): text init 0x004651a0, flags |= 0x20, then
// 0x00454750 (esi = list g+0xa84 +0xe74, edi = widget) (guess: removes it from that list). Returns its eax.
uint32_t fn_00465230(uint8_t* text, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    fn_004651a0(text, a0, a1, a2, a3);
    Field<uint32_t>(text, 0x474) = Field<uint32_t>(text, 0x474) | 0x20;
    return orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe74), u(text));
}

static uint32_t fn_00465230_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00465230(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00465230(fn_00465230_Entry, fn_00465230_Abi)
HOOK(0x00465230, "fn_00465230", fn_00465230_Entry, nullptr);

// 0x00465280 (ecx = first action; stack: text widget, second action; callee pops 8; not executed in recorded runs):
// the keys bound to both actions (0x00415750, ecx = action, edx = g+0xa80: first binding, else the second; 0 for
// action 0x23) go to 0x004652b0 (eax = second key, ecx = widget; stack: first key).
// Enhancement point (hook/enhance/gamepad.cpp): the input code a prompt shows for an action instead of its first
// binding (a controller's button while one is in use), or 0. Null by default (original behaviour).
int32_t (*g_promptKey)(uint32_t action) = nullptr;

uint32_t fn_00465280(uint32_t action1, uint8_t* text, uint32_t action2)
{
    const uint32_t device = Field<uint32_t>(G(), 0xa80);
    uint32_t key1 = orig::fn_00415750(action1, device);
    uint32_t key2 = orig::fn_00415750(action2, device);
    if (g_promptKey)
    {
        if (const int32_t k = g_promptKey(action1))
            key1 = static_cast<uint32_t>(k);
        if (const int32_t k = g_promptKey(action2))
            key2 = static_cast<uint32_t>(k);
    }
    return fn_004652b0(key2, text, key1);
}

static uint32_t fn_00465280_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00465280(ecx, B(a0), a1);
}
ABI_ENTRY_00465280(fn_00465280_Entry, fn_00465280_Abi)
HOOK(0x00465280, "fn_00465280", fn_00465280_Entry, nullptr);

// 0x004652b0 (eax = second key, ecx = text widget; stack: first key, callee pops 4; not executed in recorded runs):
// when the keys changed (+0x4d0/+0x4d4), stores them and sets the text to the widget's text key (+0x4a0, Text_Lookup
// 0x00495f00 with g+0xac0) formatted with both key names (0x00415580) through 0x00465970; the text key is saved to a
// 32-byte frame buffer around that and copied back. Returns eax as left.
uint32_t fn_004652b0(uint32_t key2, uint8_t* text, uint32_t key1)
{
    char saved[0x20];
    if (Field<uint32_t>(text, 0x4d0) == key1 && Field<uint32_t>(text, 0x4d4) == key2)
        return key2;
    std::strcpy(saved, reinterpret_cast<const char*>(text + 0x4a0));
    Field<uint32_t>(text, 0x4d0) = key1;
    Field<uint32_t>(text, 0x4d4) = key2;
    const uint32_t name2 = orig::fn_00415580(key2);
    const uint32_t name1 = orig::fn_00415580(Field<uint32_t>(text, 0x4d0));
    const uint32_t format = orig::fn_00495f00(u(text + 0x4a0), Field<uint32_t>(G(), 0xac0), 0);
    Fn00465970(u(text), format, name1, name2);
    std::strcpy(reinterpret_cast<char*>(text + 0x4a0), saved);
    return u(text);
}

static uint32_t fn_004652b0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_004652b0(eax, B(ecx), a0);
}
ABI_ENTRY_004652b0(fn_004652b0_Entry, fn_004652b0_Abi)
HOOK(0x004652b0, "fn_004652b0", fn_004652b0_Entry, nullptr);

// 0x00465350 (no inputs; virtual, e.g. slot +0x2a0 of CLASS(CameraPathObject) 0x005a0f18 and +0x150 of several UI
// vtables; not executed in recorded runs): returns 2.
uint32_t fn_00465350()
{
    return 2;
}

static uint32_t fn_00465350_Abi()
{
    return fn_00465350();
}
ABI_ENTRY_00465350(fn_00465350_Entry, fn_00465350_Abi)
HOOK(0x00465350, "fn_00465350", fn_00465350_Entry, nullptr);
