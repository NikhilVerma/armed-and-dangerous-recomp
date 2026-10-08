// Recovered (static, batch game_01): game object construction/destruction, options helpers, weapon table, game
// state helpers and the camera. Compile-checked only (the game was not run); the oracle (tools/compare.py) is still
// to do. Functions the recorded runs never execute are marked "not executed in recorded runs".
#include <cmath>
#include <cstring>

#include "game.h"
#include "game/camera.h"
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
uint32_t f2u(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
float u2f(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
void* GGame()
{
    return GameGlobal<void*>(0x005fe490);
}

const uint32_t kHeap = 0x0063a210;      // the game heap (PMS)
const uint32_t kStrNew = 0x0058b5e8;    // allocation tag string
uint32_t HeapAlloc(uint32_t size)
{
    return orig::fn_004e07f0(kHeap, 0x10, size, 2, kStrNew);
}
void HeapFree(uint32_t p)
{
    orig::fn_004e08d0(kHeap, p);
}
}  // namespace

// Prototypes of functions recovered in this file (called directly).
uint32_t fn_00401e80(uint32_t eax, void* game, uint32_t flag2);
uint32_t fn_00401ef0(void* game);
uint32_t fn_00401f40(void* game, uint32_t flag);
uint32_t fn_00401f60(uint32_t mode, void* game);
uint32_t Weapon_BuildTable(void* game);
uint32_t fn_00403940(void* game);
uint32_t fn_004039f0(void* game);
uint32_t fn_00403aa0(void* game, uint32_t a0, uint32_t a1);
uint32_t fn_004065d0(void* obj);
uint32_t fn_0040e000(void* obj);
uint32_t fn_0040e020(void* obj);
uint32_t fn_00403ac0(void* game, float dt);
uint32_t fn_00403590(void* game);
uint32_t fn_004035d0(void* game, uint32_t name);
uint32_t fn_004033c0(void* game, uint32_t text);
uint32_t fn_00403390(void* game);
uint32_t fn_00403370(void* game, uint32_t callback, uint32_t arg);
void fn_00402e70(void* game);

namespace
{
// Bounded UTF-16 copy inlined at 0x004033da / 0x0040346d / 0x004034b9: copies up to 0x400 characters of src into
// dst; when src is longer, the last copied character is replaced by the terminator. Returns the pointer the
// original leaves in eax (the end of the copied text).
uint32_t CopyText400(uint16_t* dst, const uint16_t* src)
{
    int32_t left = 0x400;
    bool ended = false;
    for (;;)
    {
        const uint16_t c = *src;
        --left;
        if (c == 0)
        {
            ended = true;
            break;
        }
        *dst = c;
        ++dst;
        ++src;
        if (left == 0)
            break;
    }
    dst[ended ? 0 : -1] = 0;
    return u(dst);
}

// Calls a game callback stored by 0x00403370 the way 0x00403390 does: eax = game, ecx = the callback, edx = arg,
// arg pushed and popped by the caller.
uint32_t CallGameCallback(uint32_t fn, uint32_t game, uint32_t arg)
{
    uint32_t r;
    __asm {
        mov eax, game
        mov ecx, fn
        mov edx, arg
        push edx
        call ecx
        add esp, 4
        mov r, eax
    }
    return r;
}

typedef void*(__stdcall* LoadIconAFn)(void*, uint32_t);
typedef uint32_t(__stdcall* SetClassLongAFn)(void*, int32_t, uint32_t);
typedef void(__stdcall* SleepFn)(uint32_t);
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 0x004011e0 (eax, ecx passed through; stack: game, a1, a2; ret 0xc). The Game constructor (caller 0x00403a30):
// base constructor 0x004dff60(game, a1, a2, ecx, eax), vtable 0x005bc098, g_game (0x005fe490) = game. +0xac4 = the
// first node of the list 0x0063a214 whose +0x14 >= 0x1c and word +0x1c >= 2 (else 0). 0x004e0bd0, the counter
// 0x0063a104 += 1, +0x748 = +0xab4 = +0xa64 = 0, 0x00401e80 (al = 1, edx = game; 1), 0x00401a30 (ebx = game),
// 0x00401fb0 (ecx = game; a1, a2, ecx), the window class icon (LoadIconA(+0x6c, 0x68), SetClassLongA(+0x64, -14)),
// callbacks 0x0063a0f0 = 0x004011b0 and 0x0063a0f4 = 0x004011d0. Then each subsystem is allocated from the heap
// 0x0063a210 (tag 0x10, align 2) and constructed (null when the allocation fails): +0xa8c, +0xa64, +0xa60, +0xa68,
// +0xa80, +0xa6c, +0xa70, +0xa74, +0xa78 ScenePass, +0xa7c DecalManager, +0xa84, +0xa88, +0xa90 FxSystem, +0xa94
// Streamers, +0xaa0, +0xa98 PostTargets, +0xa9c PostEffects, +0xaa4, +0xab4 UI, +0xaa8 (1 byte, not constructed),
// +0xaac, +0xac0 (font), +0xab8. Finally +0xabc = 0, +0x4c30 = 1/60, +0x4c34 = 1, byte +0x74d = 0, +0xa58 = +0xa5c
// = 0, 0x20 dwords at +0x74e and +0x7ce = 0, Options_Register 0x00401a80 (eax = game), Weapon_BuildTable 0x00402110,
// 0x00403aa0 (1.0, 0), 0x004039f0 (esi = game), g_game +0x700 = 0. Returns game.
uint32_t Game_Init(uint32_t eaxIn, uint32_t ecxIn, void* game, uint32_t a1, uint32_t a2)
{
    orig::fn_004dff60(u(game), a1, a2, ecxIn, eaxIn);
    Field<uint32_t>(game, 0) = 0x005bc098;
    GameGlobal<void*>(0x005fe490) = game;
    uint32_t node = GameGlobal<uint32_t>(0x0063a214);
    for (; node; node = Field<uint32_t>(P(node), 0))
        if (Field<uint32_t>(P(node), 0x14) >= 0x1c && Field<uint16_t>(P(node), 0x1c) >= 2)
            break;
    Field<uint32_t>(game, 0xac4) = node;
    const uint32_t r = orig::fn_004e0bd0();
    ++GameGlobal<int32_t>(0x0063a104);
    Field<uint32_t>(game, 0x748) = 0;
    Field<uint32_t>(game, 0xab4) = 0;
    Field<uint32_t>(game, 0xa64) = 0;
    fn_00401e80((r & 0xffffff00u) | 1u, game, 1);
    orig::fn_00401a30(u(game));
    orig::fn_00401fb0(u(game), a1, a2, ecxIn);
    void* icon = (*reinterpret_cast<LoadIconAFn*>(0x0056b24c))(Field<void*>(game, 0x6c), 0x68);
    (*reinterpret_cast<SetClassLongAFn*>(0x0056b248))(Field<void*>(game, 0x64), -0xe, u(icon));
    GameGlobal<uint32_t>(0x0063a0f0) = 0x004011b0;
    GameGlobal<uint32_t>(0x0063a0f4) = 0x004011d0;
    uint32_t p;
    p = HeapAlloc(0x70);
    Field<uint32_t>(game, 0xa8c) = p ? orig::fn_00411910(p) : 0;
    p = HeapAlloc(0x464);
    Field<uint32_t>(game, 0xa64) = p ? orig::fn_004497f0(p) : 0;
    p = HeapAlloc(0xf50);
    Field<uint32_t>(game, 0xa60) = p ? orig::fn_004483e0(p) : 0;
    p = HeapAlloc(0x1a4);
    Field<uint32_t>(game, 0xa68) = p ? fn_0040e000(P(p)) : 0;
    p = HeapAlloc(0x134);
    Field<uint32_t>(game, 0xa80) = p ? orig::fn_00414460(p) : 0;
    p = HeapAlloc(0x3b0);
    Field<uint32_t>(game, 0xa6c) = p ? fn_004065d0(P(p)) : 0;
    p = HeapAlloc(0xa0);
    Field<uint32_t>(game, 0xa70) = p ? orig::fn_004626e0(p) : 0;
    p = HeapAlloc(0x154);
    Field<uint32_t>(game, 0xa74) = p ? orig::fn_00467fd0(p) : 0;
    p = HeapAlloc(0x1cc);
    Field<uint32_t>(game, 0xa78) = p ? orig::fn_00487f00(p) : 0;
    p = HeapAlloc(0x28);
    Field<uint32_t>(game, 0xa7c) = p ? orig::fn_00416050(p) : 0;
    p = HeapAlloc(0xf38);
    // 0x0045a4e0 lists ecx as an input but only pushes it (a stack slot): passed as 0.
    Field<uint32_t>(game, 0xa84) = p ? orig::fn_0045a4e0(0, p) : 0;
    p = HeapAlloc(0x24);
    Field<uint32_t>(game, 0xa88) = p ? orig::fn_0044f4a0(p) : 0;
    p = HeapAlloc(0x28);
    Field<uint32_t>(game, 0xa90) = p ? orig::fn_0041c700(p) : 0;
    p = HeapAlloc(0x10);
    Field<uint32_t>(game, 0xa94) = p ? orig::fn_00494d90(p) : 0;
    p = HeapAlloc(0x40);
    Field<uint32_t>(game, 0xaa0) = p ? orig::fn_0048e850(p) : 0;
    p = HeapAlloc(0x2fc);
    Field<uint32_t>(game, 0xa98) = p ? orig::fn_0047c850(p) : 0;
    p = HeapAlloc(0x6f0);
    Field<uint32_t>(game, 0xa9c) = p ? orig::fn_0047bd10(p) : 0;
    p = HeapAlloc(0x3c);
    Field<uint32_t>(game, 0xaa4) = p ? orig::fn_0046e770(p) : 0;
    p = HeapAlloc(0x360);
    Field<uint32_t>(game, 0xab4) = p ? orig::fn_0048ff90(p) : 0;
    Field<uint32_t>(game, 0xaa8) = HeapAlloc(1);
    p = HeapAlloc(0x63c);
    Field<uint32_t>(game, 0xaac) = p ? orig::fn_0046a7e0(p) : 0;
    p = HeapAlloc(0xb4);
    Field<uint32_t>(game, 0xac0) = p ? orig::fn_00495e50(p) : 0;
    p = HeapAlloc(0x102c);
    Field<uint32_t>(game, 0xab8) = p ? orig::fn_0048b620(p) : 0;
    Field<uint32_t>(game, 0xabc) = 0;
    Field<uint32_t>(game, 0x4c30) = 0x3c888889;  // 1/60
    Field<uint32_t>(game, 0x4c34) = 1;
    Field<uint8_t>(game, 0x74d) = 0;
    Field<uint32_t>(game, 0xa58) = 0;
    Field<uint32_t>(game, 0xa5c) = 0;
    std::memset(&Field<uint8_t>(game, 0x74e), 0, 0x20 * 4);
    std::memset(&Field<uint8_t>(game, 0x7ce), 0, 0x20 * 4);
    orig::fn_00401a80(u(game));
    Weapon_BuildTable(game);
    fn_00403aa0(game, 0x3f800000u, 0);
    fn_004039f0(game);
    Field<uint32_t>(GGame(), 0x700) = 0;
    return u(game);
}
static uint32_t Game_Init_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Game_Init(eax, ecx, P(a0), a1, a2);
}
ABI_ENTRY_004011e0(Game_Init_Entry, Game_Init_Abi)
HOOK(0x004011e0, "Game_Init", Game_Init_Entry, nullptr);

// 0x00401710 (ebx = game; ecx unused). The Game destructor body (caller 0x00403a30): vtable 0x005bc098; when the
// level manager +0xa64 is set, 0x00403940 (edi = game) first. Then each subsystem pointer is destroyed (its
// destructor with the register shown) and freed to the heap 0x0063a210, then cleared: +0xa64 (0x00449830 edi),
// +0xa60 (0x00448c70 edi), +0xa68 (0x0040e020 esi), +0xa6c (0x00406690 eax), +0xa80 (free only), +0xa70 (0x00462770
// eax), +0xa74 (free only), +0xa78 (ScenePass_Destruct esi), +0xa7c (DecalManager_Destruct eax), +0xa88 (free only),
// +0xa90 (FxSystem_Destruct esi), +0xa94 (Streamers_Destruct eax), +0xa98 (0x0047ca60 esi), +0xa9c
// (PostEffects_Destruct eax), +0xab4 (0x00490760 esi), +0xaa8 (free only), +0xaac (0x0046acb0 edx), +0xa84
// (0x0045a6c0 edi), +0xaa0 (0x0048e970 esi), +0xaa4 (0x0046e7c0 esi), +0xa8c (0x00411950 eax), +0xac0 (free only),
// +0xab8 (0x0048b660 eax). Tail jump to the base destructor 0x004e0070 (eax = game). Not executed in recorded runs:
// not verifiable by compare yet. Several destructors are reached through one-jump thunks (0x00406680, 0x004483f0,
// 0x0047c910, 0x0046a810, 0x0048e860, 0x0046e7b0); the targets are called directly. 0x00406690 lists ecx as an input
// but only pushes it (a stack slot): passed as 0.
uint32_t Game_Destruct(uint32_t /*ecx*/, void* game)
{
    Field<uint32_t>(game, 0) = 0x005bc098;
    if (Field<uint32_t>(game, 0xa64))
        fn_00403940(game);
    uint32_t p;
    if ((p = Field<uint32_t>(game, 0xa64)) != 0)
    {
        orig::fn_00449830(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa64) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa60)) != 0)
    {
        orig::fn_00448c70(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa60) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa68)) != 0)
    {
        fn_0040e020(P(p));
        HeapFree(p);
        Field<uint32_t>(game, 0xa68) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa6c)) != 0)
    {
        orig::fn_00406690(p, 0);
        HeapFree(p);
        Field<uint32_t>(game, 0xa6c) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa80)) != 0)
    {
        HeapFree(p);
        Field<uint32_t>(game, 0xa80) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa70)) != 0)
    {
        orig::fn_00462770(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa70) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa74)) != 0)
    {
        HeapFree(p);
        Field<uint32_t>(game, 0xa74) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa78)) != 0)
    {
        orig::fn_00487fc0(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa78) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa7c)) != 0)
    {
        orig::fn_004160c0(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa7c) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa88)) != 0)
    {
        HeapFree(p);
        Field<uint32_t>(game, 0xa88) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa90)) != 0)
    {
        orig::fn_0041c760(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa90) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa94)) != 0)
    {
        orig::fn_00494de0(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa94) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa98)) != 0)
    {
        orig::fn_0047ca60(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa98) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa9c)) != 0)
    {
        orig::fn_0047bd70(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa9c) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xab4)) != 0)
    {
        orig::fn_00490760(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xab4) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xaa8)) != 0)
    {
        HeapFree(p);
        Field<uint32_t>(game, 0xaa8) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xaac)) != 0)
    {
        orig::fn_0046acb0(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xaac) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa84)) != 0)
    {
        orig::fn_0045a6c0(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa84) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xaa0)) != 0)
    {
        orig::fn_0048e970(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xaa0) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xaa4)) != 0)
    {
        orig::fn_0046e7c0(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xaa4) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xa8c)) != 0)
    {
        orig::fn_00411950(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xa8c) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xac0)) != 0)
    {
        HeapFree(p);
        Field<uint32_t>(game, 0xac0) = 0;
    }
    if ((p = Field<uint32_t>(game, 0xab8)) != 0)
    {
        orig::fn_0048b660(p);
        HeapFree(p);
        Field<uint32_t>(game, 0xab8) = 0;
    }
    return orig::fn_004e0070(u(game));
}
static uint32_t Game_Destruct_Abi(uint32_t ecx, uint32_t ebx)
{
    return Game_Destruct(ecx, P(ebx));
}
ABI_ENTRY_00401710(Game_Destruct_Entry, Game_Destruct_Abi)
HOOK(0x00401710, "Game_Destruct", Game_Destruct_Entry, nullptr);

// 0x00401e80 (al = reset flag, edx = game; stack: flag2; ret 4). Callers Game_Init and Game_StartLevel 0x00403690.
// When al: 0x2a dwords at +0x4c8 = 0 and 0x00401f60 (eax = 1; game). When flag2 (byte): the three dwords +0x570 =
// {5000, 0, 0} and copied to +0x57c. Then 0x00401f40(game, 0). Returns 0x00401f40's eax.
uint32_t fn_00401e80(uint32_t eax, void* game, uint32_t flag2)
{
    if (static_cast<uint8_t>(eax))
    {
        std::memset(&Field<uint8_t>(game, 0x4c8), 0, 0x2a * 4);
        fn_00401f60(1, game);
    }
    if (static_cast<uint8_t>(flag2))
    {
        uint32_t* a = &Field<uint32_t>(game, 0x570);
        a[0] = 0;
        a[1] = 0;
        a[2] = 0;
        a[0] = 0x1388;
        uint32_t* b = &Field<uint32_t>(game, 0x57c);
        b[0] = a[0];
        b[1] = a[1];
        b[2] = a[2];
    }
    return fn_00401f40(game, 0);
}
static uint32_t fn_00401e80_Abi(uint32_t eax, uint32_t edx, uint32_t a0)
{
    return fn_00401e80(eax, P(edx), a0);
}
ABI_ENTRY_00401e80(fn_00401e80_Entry, fn_00401e80_Abi)
HOOK(0x00401e80, "fn_00401e80", fn_00401e80_Entry, nullptr);

// 0x00401ef0 (eax = game). Copies the three dwords +0x570 to +0x57c. Returns game + 0x57c. Not executed in recorded
// runs: not verifiable by compare yet.
uint32_t fn_00401ef0(void* game)
{
    const uint32_t* a = &Field<uint32_t>(game, 0x570);
    uint32_t* b = &Field<uint32_t>(game, 0x57c);
    b[0] = a[0];
    b[1] = a[1];
    b[2] = a[2];
    return u(b);
}
static uint32_t fn_00401ef0_Abi(uint32_t eax)
{
    return fn_00401ef0(P(eax));
}
ABI_ENTRY_00401ef0(fn_00401ef0_Entry, fn_00401ef0_Abi)
HOOK(0x00401ef0, "fn_00401ef0", fn_00401ef0_Entry, nullptr);

// 0x00401f10 (eax = game). The reverse copy: the three dwords +0x57c to +0x570, then 0x00401f40(game, 0).
uint32_t fn_00401f10(void* game)
{
    const uint32_t* b = &Field<uint32_t>(game, 0x57c);
    uint32_t* a = &Field<uint32_t>(game, 0x570);
    a[0] = b[0];
    a[1] = b[1];
    a[2] = b[2];
    return fn_00401f40(game, 0);
}
static uint32_t fn_00401f10_Abi(uint32_t eax)
{
    return fn_00401f10(P(eax));
}
ABI_ENTRY_00401f10(fn_00401f10_Entry, fn_00401f10_Abi)
HOOK(0x00401f10, "fn_00401f10", fn_00401f10_Entry, nullptr);

// 0x00401f40 (stack: game, flag; ret 8). 0x00410ca0, 0x00494b70, then when flag (byte) 0x00401ef0 (eax = game).
// Returns 0x00401ef0's eax, else the flag byte with 0x00494b70's upper eax bytes.
uint32_t fn_00401f40(void* game, uint32_t flag)
{
    orig::fn_00410ca0();
    const uint32_t r = orig::fn_00494b70();
    if (static_cast<uint8_t>(flag))
        return fn_00401ef0(game);
    return (r & 0xffffff00u) | (flag & 0xffu);
}
static uint32_t fn_00401f40_Abi(uint32_t a0, uint32_t a1)
{
    return fn_00401f40(P(a0), a1);
}
ABI_ENTRY_00401f40(fn_00401f40_Entry, fn_00401f40_Abi)
HOOK(0x00401f40, "fn_00401f40", fn_00401f40_Entry, nullptr);

// 0x00401f60 (eax = mode; stack: game; ret 4). g_game +0x550 = mode (1 when mode >= 4, unsigned), then the five
// dwords of the table 0x00589188 entry (game +0x550) are copied to g_game +0x554 (game is the stack argument; the
// table index is read from it, the copy goes to g_game). Returns the index.
uint32_t fn_00401f60(uint32_t mode, void* game)
{
    void* g = GGame();
    Field<uint32_t>(g, 0x550) = mode;
    if (mode >= 4)
        Field<uint32_t>(g, 0x550) = 1;
    const uint32_t index = Field<uint32_t>(game, 0x550);
    std::memcpy(&Field<uint8_t>(g, 0x554), reinterpret_cast<const void*>(0x00589188 + index * 20), 20);
    return index;
}
static uint32_t fn_00401f60_Abi(uint32_t eax, uint32_t a0)
{
    return fn_00401f60(eax, P(a0));
}
ABI_ENTRY_00401f60(fn_00401f60_Entry, fn_00401f60_Abi)
HOOK(0x00401f60, "fn_00401f60", fn_00401f60_Entry, nullptr);

// 0x00402110 (stack: game, unused; ret 4; caller Game_Init). Builds the weapon name table in g_game: 0x930 dwords
// from +0x2770 are cleared, then each weapon entry (0x150 bytes from +0x2770; player weapons, then AI weapons) gets
// its name strings copied from the game's string constants (inlined strcpy, the copy length is strlen + 1 of each
// constant). The table below is generated from the disassembly (offset in g_game, source string, bytes) in the
// original order. Evidence: the strings ("player_machinegun", "Camera_machinegun", "pickup_mgun_bullets", ...).
// Returns the last dword the inlined copies loaded (eax = [0x005bc164]).
namespace
{
struct WeaponStringCopy
{
    uint32_t offset;
    uint32_t source;
    uint32_t bytes;
};
const WeaponStringCopy kWeaponStrings[] = {
    {0x2770, 0x005bc86c, 18},  // "player_machinegun"
    {0x2790, 0x005bc85c, 16},  // "weap_machinegun"
    {0x27b0, 0x005bc848, 20},  // "pub_info_machinegun"
    {0x2810, 0x005bc838, 13},  // "ui_player_mg"
    {0x2850, 0x005bc824, 18},  // "trotter_fire_mgun"
    {0x2870, 0x005bc810, 18},  // "Camera_machinegun"
    {0x2890, 0x005bc7fc, 20},  // "pickup_mgun_bullets"
    {0x2830, 0x005bc7ec, 16},  // "ui_mg_crosshair"
    {0x28c0, 0x005bc7d8, 17},  // "player_snipergun"
    {0x28e0, 0x005bc7c8, 15},  // "weap_snipergun"
    {0x2900, 0x005bc7b4, 19},  // "pub_info_snipergun"
    {0x2960, 0x005bc7a0, 17},  // "ui_player_sniper"
    {0x29a0, 0x005bc78c, 20},  // "trotter_fire_single"
    {0x29c0, 0x005bc77c, 14},  // "Camera_sniper"
    {0x29e0, 0x005bc764, 22},  // "pickup_sniper_bullets"
    {0x2980, 0x005bc750, 20},  // "ui_sniper_crosshair"
    {0x2a10, 0x005bc740, 13},  // "player_rifle"
    {0x2a30, 0x005bc734, 11},  // "weap_rifle"
    {0x2a50, 0x005bc724, 15},  // "pub_info_rifle"
    {0x2ab0, 0x005bc714, 16},  // "ui_player_rifle"
    {0x2af0, 0x005bc78c, 20},  // "trotter_fire_single"
    {0x2b10, 0x005bc704, 13},  // "Camera_rifle"
    {0x2b30, 0x005bc6ec, 21},  // "pickup_rifle_bullets"
    {0x2ad0, 0x005bc6dc, 13},  // "ui_crosshair"
    {0x2b60, 0x005bc6c8, 18},  // "player_rlvindaloo"
    {0x2b80, 0x005bc6b8, 16},  // "weap_rlvindaloo"
    {0x2ba0, 0x005bc6a4, 20},  // "pub_info_rlvindaloo"
    {0x2c00, 0x005bc68c, 21},  // "ui_player_rlvindaloo"
    {0x2c40, 0x005bc678, 20},  // "trotter_fire_gurner"
    {0x2c60, 0x005bc668, 16},  // "camera_vindaloo"
    {0x2c80, 0x005bc650, 24},  // "pickup_vindaloo_mortars"
    {0x2c20, 0x005bc638, 22},  // "ui_vindaloo_crosshair"
    {0x2cb0, 0x005bc628, 16},  // "player_rlgurner"
    {0x2cd0, 0x005bc618, 14},  // "weap_rlgurner"
    {0x2cf0, 0x005bc604, 18},  // "pub_info_rlgurner"
    {0x2d50, 0x005bc5f0, 19},  // "ui_player_rlgurner"
    {0x2d90, 0x005bc678, 20},  // "trotter_fire_gurner"
    {0x2db0, 0x005bc5e0, 14},  // "camera_gurner"
    {0x2dd0, 0x005bc5c8, 22},  // "pickup_gurner_rockets"
    {0x2d70, 0x005bc5b4, 20},  // "ui_gurner_crosshair"
    {0x2e00, 0x005bc5a4, 16},  // "player_sharkgun"
    {0x2e20, 0x005bc594, 14},  // "weap_sharkgun"
    {0x2e40, 0x005bc580, 18},  // "pub_info_sharkgun"
    {0x2ea0, 0x005bc56c, 19},  // "ui_player_sharkgun"
    {0x2ee0, 0x005bc678, 20},  // "trotter_fire_gurner"
    {0x2f00, 0x005bc5e0, 14},  // "camera_gurner"
    {0x2f20, 0x005bc554, 24},  // "pickup_sharkgun_rockets"
    {0x2ec0, 0x005bc53c, 22},  // "ui_sharkgun_crosshair"
    {0x2f50, 0x005bc524, 22},  // "player_guyfawkes_bomb"
    {0x2f70, 0x005bc514, 15},  // "weap_guyfawkes"
    {0x2f90, 0x005bc500, 19},  // "pub_info_guyfawkes"
    {0x2ff0, 0x005bc4ec, 20},  // "ui_player_guyfawkes"
    {0x3070, 0x005bc4d8, 17},  // "pickup_guyfawkes"
    {0x2fb0, 0x005bc4c0, 23},  // "ui_pub_award_guyfawkes"
    {0x2fd0, 0x005bc4ac, 20},  // "pub_award_guyfawkes"
    {0x30a0, 0x005bc49c, 16},  // "player_rabbiter"
    {0x30c0, 0x005bc488, 18},  // "weap_bunchoffives"
    {0x30e0, 0x005bc470, 22},  // "pub_info_bunchoffives"
    {0x3140, 0x005bc458, 23},  // "ui_player_bunchoffives"
    {0x3180, 0x005bc678, 20},  // "trotter_fire_gurner"
    {0x31c0, 0x005bc444, 20},  // "pickup_bunchoffives"
    {0x3100, 0x005bc430, 17},  // "ui_pub_award_bof"
    {0x3120, 0x005bc420, 14},  // "pub_award_bof"
    {0x31f0, 0x005bc40c, 18},  // "player_topsyturvy"
    {0x3210, 0x005bc3fc, 16},  // "weap_topsyturvy"
    {0x3230, 0x005bc3e8, 20},  // "pub_info_topsyturvy"
    {0x3290, 0x005bc3d8, 16},  // "ui_player_topsy"
    {0x32d0, 0x005bc78c, 20},  // "trotter_fire_single"
    {0x3310, 0x005bc3c4, 18},  // "pickup_topsyturvy"
    {0x3250, 0x005bc3b0, 19},  // "ui_pub_award_topsy"
    {0x3270, 0x005bc3a0, 16},  // "pub_award_topsy"
    {0x3340, 0x005bc388, 21},  // "player_blackhole_box"
    {0x3360, 0x005bc378, 15},  // "weap_blackhole"
    {0x3380, 0x005bc364, 19},  // "pub_info_blackhole"
    {0x33e0, 0x005bc350, 20},  // "ui_player_blackhole"
    {0x3420, 0x005bc678, 20},  // "trotter_fire_gurner"
    {0x3460, 0x005bc33c, 17},  // "pickup_blackhole"
    {0x33a0, 0x005bc324, 23},  // "ui_pub_award_blackhole"
    {0x33c0, 0x005bc310, 20},  // "pub_award_blackhole"
    {0x3490, 0x005bc2fc, 19},  // "player_sticky_bomb"
    {0x34b0, 0x005bc2ec, 16},  // "weap_stickybomb"
    {0x34d0, 0x005bc2dc, 16},  // "pub_info_sticky"
    {0x3530, 0x005bc2c8, 19},  // "ui_player_grenades"
    {0x35b0, 0x005bc2b4, 20},  // "pickup_sticky_bombs"
    {0x34f0, 0x005bc29c, 24},  // "ui_pub_award_stickybomb"
    {0x3510, 0x005bc284, 21},  // "pub_award_stickybomb"
    {0x3680, 0x005bc274, 14},  // "ui_tickerbomb"
    {0x3640, 0x005bc25c, 24},  // "ui_pub_award_tickerbomb"
    {0x3660, 0x005bc244, 21},  // "pub_award_tickerbomb"
    {0x3730, 0x005bc234, 14},  // "molmer_pistol"
    {0x3880, 0x005bc228, 11},  // "q_chaingun"
    {0x39d0, 0x005bc21c, 10},  // "grunt_bow"
    {0x3b20, 0x005bc210, 12},  // "grunt_rifle"
    {0x3c70, 0x005bc204, 11},  // "captain_mg"
    {0x3dc0, 0x005bc1ec, 23},  // "captain_rocketlauncher"
    {0x3f10, 0x005bc1d8, 18},  // "captain_snipergun"
    {0x4060, 0x005bc1c8, 14},  // "q112_chaingun"
    {0x41b0, 0x005bc1b0, 24},  // "saucer_missile_launcher"
    {0x4300, 0x005bc198, 21},  // "mini_zeppelin_turret"
    {0x4450, 0x005bc188, 16},  // "colossus_turret"
    {0x45a0, 0x005bc178, 15},  // "shrubpatrolgun"
    {0x4840, 0x005bc16c, 9},  // "monk_gun"
    {0x4990, 0x005bc160, 10},  // "monk_gun2"
};
}  // namespace

uint32_t Weapon_BuildTable(void* /*game*/)
{
    uint8_t* g = static_cast<uint8_t*>(GGame());
    std::memset(g + 0x2770, 0, 0x930 * 4);
    for (const WeaponStringCopy& c : kWeaponStrings)
        std::memcpy(g + c.offset, reinterpret_cast<const void*>(c.source), c.bytes);
    return GameGlobal<uint32_t>(0x005bc164);
}
static uint32_t Weapon_BuildTable_Abi(uint32_t a0)
{
    return Weapon_BuildTable(P(a0));
}
ABI_ENTRY_00402110(Weapon_BuildTable_Entry, Weapon_BuildTable_Abi)
HOOK(0x00402110, "Weapon_BuildTable", Weapon_BuildTable_Entry, nullptr);

// 0x00402e70 (eax = game). +0x748 = 0. (Its caller 0x00403940 uses it on level teardown.)
void fn_00402e70(void* game)
{
    Field<uint32_t>(game, 0x748) = 0;
}
static void fn_00402e70_Abi(uint32_t eax)
{
    fn_00402e70(P(eax));
}
ABI_ENTRY_00402e70(fn_00402e70_Entry, fn_00402e70_Abi)
HOOK(0x00402e70, "fn_00402e70", fn_00402e70_Entry, nullptr);

// 0x00402e80 (ecx = game; stack: dt; ret 4). Game vtable 0x005bc098 slot 2: the per-frame update (guess from
// behaviour: every subsystem is updated with dt). A pending level request (byte +0x74d) starts the level named at
// +0x84e (Game_StartLevel 0x00403690, eax = name, ecx = game; 0, 0). Sub-steps +0x4c34 = 1 - (int)(dt * -20.0)
// ([0x005bcd44]) capped at 5, step +0x4c30 = dt / steps capped at 0.05 ([0x005bcc34]). Then 0x0044a390 (esi = +0xa64),
// 0x00403ac0 (dt), (+0x28) vt+4(dt) when set, 0x004145c0 (edi = +0xa80; dt), Fx_UpdateAll(+0xa90),
// Streamers_Update (ebx = +0xa94; dt), 0x0045da90(+0xa84), 0x0048ed90 (ebx = +0xaa0). When 0x00462790(+0xa70) is
// true: Hud_UpdateCrosshair(+0xa6c), 0x004915b0 (eax = +0xab4), 0x00463fa0(+0xa70), 0x0044f580 (ebx = +0xaa0 as
// left; +0xa88, dt), Game_RenderFrame 0x00403050 (ebx = game) unless byte +0x74c; when +0x2684 == 0,
// 0x0044a590(ecx = +0xa64) is false, byte +0xae6 == 0 and dt > 0: the physics step 0x00411d40 (via thunk 0x00411e00;
// ecx = +0x4c34, edi = +0xa8c; +0x4c30); byte +0xae6 = 0; when byte +0x74c: +0x74c = 0 and game vt+0x24(0). When
// byte +0xae5: UI_PushScreen 0x00490b00 (eax = 0, ecx = +0xab4; "PubMenu" 0x005bc08c) and +0xae5 = 0. Tail jump to
// 0x004e44e0(+0x34) (the argument slot is reused). Not executed in recorded runs: not verifiable by compare yet.
// 0x004145c0 lists edx as an input but its callee overwrites it before use (passed as 0); 0x0044f580 only saves ebx.
uint32_t Game_Update(void* game, uint32_t dtBits)
{
    const float dt = u2f(dtBits);
    if (Field<uint8_t>(game, 0x74d))
    {
        Field<uint8_t>(game, 0x74d) = 0;
        orig::fn_00403690(u(game) + 0x84e, u(game), 0, 0);
    }
    const int32_t steps = 1 - static_cast<int32_t>(dt * GameGlobal<float>(0x005bcd44));
    Field<int32_t>(game, 0x4c34) = steps;
    if (steps > 5)
        Field<int32_t>(game, 0x4c34) = 5;
    const float step = dt / static_cast<float>(Field<int32_t>(game, 0x4c34));
    Field<float>(game, 0x4c30) = step;
    if (step > GameGlobal<float>(0x005bcc34))
        Field<uint32_t>(game, 0x4c30) = 0x3d4ccccd;
    orig::fn_0044a390(Field<uint32_t>(game, 0xa64));
    fn_00403ac0(game, dt);
    if (void* o = Field<void*>(game, 0x28))
        VCall<void>(o, 1, dtBits);
    orig::fn_004145c0(0, Field<uint32_t>(game, 0xa80), dtBits);
    orig::fn_004237b0(Field<uint32_t>(game, 0xa90));
    orig::fn_00494f30(Field<uint32_t>(game, 0xa94), dtBits);
    orig::fn_0045da90(Field<uint32_t>(game, 0xa84));
    const uint32_t ebx = Field<uint32_t>(game, 0xaa0);
    orig::fn_0048ed90(ebx);
    if (static_cast<uint8_t>(orig::fn_00462790(Field<uint32_t>(game, 0xa70))))
    {
        orig::fn_00406f50(Field<uint32_t>(game, 0xa6c));
        orig::fn_004915b0(Field<uint32_t>(game, 0xab4));
        orig::fn_00463fa0(Field<uint32_t>(game, 0xa70));
        orig::fn_0044f580(ebx, Field<uint32_t>(game, 0xa88), dtBits);
        if (!Field<uint8_t>(game, 0x74c))
            orig::fn_00403050(u(game));
        if (Field<uint32_t>(game, 0x2684) == 0 &&
            !static_cast<uint8_t>(orig::fn_0044a590(Field<uint32_t>(game, 0xa64))) && Field<uint8_t>(game, 0xae6) == 0 &&
            dt > GameGlobal<float>(0x0056c05c))
            orig::fn_00411d40(Field<uint32_t>(game, 0x4c34), Field<uint32_t>(game, 0xa8c), Field<uint32_t>(game, 0x4c30));
        const bool flag = Field<uint8_t>(game, 0x74c) != 0;
        Field<uint8_t>(game, 0xae6) = 0;
        if (flag)
        {
            Field<uint8_t>(game, 0x74c) = 0;
            VCall<void>(game, 0x24 / 4, 0u);
        }
    }
    if (Field<uint8_t>(game, 0xae5))
    {
        orig::fn_00490b00(0, Field<uint32_t>(game, 0xab4), 0x005bc08c);
        Field<uint8_t>(game, 0xae5) = 0;
    }
    return orig::fn_004e44e0(Field<uint32_t>(game, 0x34));
}
static uint32_t Game_Update_Abi(uint32_t ecx, uint32_t a0)
{
    return Game_Update(P(ecx), a0);
}
ABI_ENTRY_00402e80(Game_Update_Entry, Game_Update_Abi)
HOOK(0x00402e80, "Game_Update", Game_Update_Entry, nullptr);

// 0x00403370 (eax = game, ecx = callback, edx = argument). Stores a deferred callback: +0xa58 = callback, +0xa5c =
// argument; when +0x2684 is 0 it runs at once (tail jump to 0x00403390). Returns game, or 0x00403390's eax.
uint32_t fn_00403370(void* game, uint32_t callback, uint32_t arg)
{
    Field<uint32_t>(game, 0xa58) = callback;
    Field<uint32_t>(game, 0xa5c) = arg;
    if (Field<uint32_t>(game, 0x2684) != 0)
        return u(game);
    return fn_00403390(game);
}
static uint32_t fn_00403370_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return fn_00403370(P(eax), ecx, edx);
}
ABI_ENTRY_00403370(fn_00403370_Entry, fn_00403370_Abi)
HOOK(0x00403370, "fn_00403370", fn_00403370_Entry, nullptr);

// 0x00403390 (eax = game). Runs the deferred callback +0xa58 when set: both +0xa58 and +0xa5c are cleared first, then
// callback(+0xa5c) (cdecl; eax = game, ecx = callback, edx = argument as the original leaves them). Returns the
// callback's eax, else game.
uint32_t fn_00403390(void* game)
{
    const uint32_t callback = Field<uint32_t>(game, 0xa58);
    if (!callback)
        return u(game);
    const uint32_t arg = Field<uint32_t>(game, 0xa5c);
    Field<uint32_t>(game, 0xa58) = 0;
    Field<uint32_t>(game, 0xa5c) = 0;
    return CallGameCallback(callback, u(game), arg);
}
static uint32_t fn_00403390_Abi(uint32_t eax)
{
    return fn_00403390(P(eax));
}
ABI_ENTRY_00403390(fn_00403390_Entry, fn_00403390_Abi)
HOOK(0x00403390, "fn_00403390", fn_00403390_Entry, nullptr);

// 0x004033c0 (eax = game, ecx = text). Copies a UTF-16 text (ecx, else +0x265c) into +0x1de4 (at most 0x400
// characters); when both are null +0x1de4 becomes empty. Returns the end of the copy (eax), or game.
uint32_t fn_004033c0(void* game, uint32_t text)
{
    if (!text)
    {
        text = Field<uint32_t>(game, 0x265c);
        if (!text)
        {
            Field<uint16_t>(game, 0x1de4) = 0;
            return u(game);
        }
    }
    return CopyText400(&Field<uint16_t>(game, 0x1de4), reinterpret_cast<const uint16_t*>(text));
}
static uint32_t fn_004033c0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004033c0(P(eax), ecx);
}
ABI_ENTRY_004033c0(fn_004033c0_Entry, fn_004033c0_Abi)
HOOK(0x004033c0, "fn_004033c0", fn_004033c0_Entry, nullptr);

// 0x00403410 (ecx = game; stack: text, flags; ret 8). Shows a UTF-16 message (text, else +0x1de4). Flag 2 queues
// it: when a message is pending (+0x15d8 != 0.0) the pending one (+0xdd8, flags +0xdd4) is shown first through this
// function; then +0xdd4 = flags without 2, +0x15d8 = 1.0 and the text is copied to +0xdd8. Otherwise: flag 0x10
// also copies it to +0x1de4 (0x004033c0) and is cleared; the text is copied to +0x15e0, +0x1de0 = &+0x15e0, byte
// +0x15dc = 1, word +0x15de = flags; without flag 8, 0x004031e0 (bl = 1 with the flags' upper bytes, edi = game; 1).
// Copies take at most 0x400 characters. Returns the end of the copy or 0x004031e0's eax.
uint32_t fn_00403410(void* game, uint32_t text, uint32_t flags)
{
    if (!text)
        text = u(game) + 0x1de4;
    if (flags & 2)
    {
        if (!(Field<float>(game, 0x15d8) == GameGlobal<float>(0x0056c05c)))
            fn_00403410(game, u(game) + 0xdd8, Field<uint32_t>(game, 0xdd4));
        Field<uint32_t>(game, 0xdd4) = flags & ~2u;
        Field<uint32_t>(game, 0x15d8) = 0x3f800000;
        return CopyText400(&Field<uint16_t>(game, 0xdd8), reinterpret_cast<const uint16_t*>(text));
    }
    if (flags & 0x10)
    {
        fn_004033c0(game, text);
        flags &= ~0x10u;
    }
    const uint32_t r = CopyText400(&Field<uint16_t>(game, 0x15e0), reinterpret_cast<const uint16_t*>(text));
    Field<uint32_t>(game, 0x1de0) = u(game) + 0x15e0;
    Field<uint8_t>(game, 0x15dc) = 1;
    Field<uint16_t>(game, 0x15de) = static_cast<uint16_t>(flags);
    if (flags & 8)
        return r;
    return orig::fn_004031e0((flags & 0xffffff00u) | 1u, u(game), 1);
}
static uint32_t fn_00403410_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00403410(P(ecx), a0, a1);
}
ABI_ENTRY_00403410(fn_00403410_Entry, fn_00403410_Abi)
HOOK(0x00403410, "fn_00403410", fn_00403410_Entry, nullptr);

// 0x00403590 (esi = game). Chooses the start level: when 0x0044a5a0(+0xa64) is true, 0x0044a5c0(+0xa64, &name)
// fills a 0x20-byte local name and 0x004035d0 requests it; otherwise 0x004035d0 with no name (the default
// "lev_shell").
uint32_t fn_00403590(void* game)
{
    uint8_t name[0x20];
    if (static_cast<uint8_t>(orig::fn_0044a5a0(Field<uint32_t>(game, 0xa64))))
    {
        orig::fn_0044a5c0(Field<uint32_t>(game, 0xa64), u(name));
        return fn_004035d0(game, u(name));
    }
    return fn_004035d0(game, 0);
}
static uint32_t fn_00403590_Abi(uint32_t esi)
{
    return fn_00403590(P(esi));
}
ABI_ENTRY_00403590(fn_00403590_Entry, fn_00403590_Abi)
HOOK(0x00403590, "fn_00403590", fn_00403590_Entry, nullptr);

// 0x004035d0 (eax = game, ecx = level name or null; symbols/abi.csv lists 8 caller-popped stack arguments, none is
// read). Requests a level: byte +0x74d = 1, then 0x004e59d0 (eax = 0x104, ecx = +0x84e, edi = name, default
// "lev_shell" 0x005bc144) stores the name at +0x84e (read by Game_Update 0x00402e80).
uint32_t fn_004035d0(void* game, uint32_t name)
{
    Field<uint8_t>(game, 0x74d) = 1;
    if (!name)
        name = 0x005bc144;
    return orig::fn_004e59d0(0x104, u(game) + 0x84e, name);
}
static uint32_t fn_004035d0_Abi(uint32_t eax, uint32_t ecx, uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/,
                                uint32_t /*a3*/, uint32_t /*a4*/, uint32_t /*a5*/, uint32_t /*a6*/, uint32_t /*a7*/)
{
    return fn_004035d0(P(eax), ecx);
}
ABI_ENTRY_004035d0(fn_004035d0_Entry, fn_004035d0_Abi)
HOOK(0x004035d0, "fn_004035d0", fn_004035d0_Entry, nullptr);

// 0x00403600 (eax = game, edx = UTF-16 level name). Requests a level by a wide name: byte +0x74d = 1 and the name
// is copied (unbounded) to +0x84e. Returns the address of the written terminator.
uint32_t fn_00403600(void* game, uint32_t name)
{
    Field<uint8_t>(game, 0x74d) = 1;
    const uint16_t* src = reinterpret_cast<const uint16_t*>(name);
    uint16_t* dst = &Field<uint16_t>(game, 0x84e);
    for (uint16_t c = *src; c != 0; c = *++src)
        *dst++ = c;
    *dst = 0;
    return u(dst);
}
static uint32_t fn_00403600_Abi(uint32_t eax, uint32_t edx)
{
    return fn_00403600(P(eax), edx);
}
ABI_ENTRY_00403600(fn_00403600_Entry, fn_00403600_Abi)
HOOK(0x00403600, "fn_00403600", fn_00403600_Entry, nullptr);

// 0x00403640 (ecx = game; stack: name, a1, a2; ret 0xc). Converts the name into a 0x208-byte local (0x004e59d0
// eax = 0x104, ecx = buffer, edi = name) and starts that level: Game_StartLevel 0x00403690 (eax = buffer, ecx =
// game; a1, a2).
uint32_t Game_LoadLevelByName(void* game, uint32_t name, uint32_t a1, uint32_t a2)
{
    uint8_t buffer[0x208];
    orig::fn_004e59d0(0x104, u(buffer), name);
    return orig::fn_00403690(u(buffer), u(game), a1, a2);
}
static uint32_t Game_LoadLevelByName_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Game_LoadLevelByName(P(ecx), a0, a1, a2);
}
ABI_ENTRY_00403640(Game_LoadLevelByName_Entry, Game_LoadLevelByName_Abi)
HOOK(0x00403640, "Game_LoadLevelByName", Game_LoadLevelByName_Entry, nullptr);

// 0x00403940 (edi = game). Level teardown: clears the deferred callback (0x00403370 eax = game, ecx = edx = 0),
// 0x0044ad50 (al = 1). When the level manager +0xa64 is not 0x0044a600 and is 0x0044a5f0 (eax = +0xa64 both):
// DecalManager_Clear (esi = +0xa7c), 0x0044a670(+0xa64), 0x00402e70 (eax = game), Sleep(0), every object of the
// list 0x0063a0d8 (next +0x544) is unlinked and gets vt+0x38(1), then 0x004e0ab0 (esi = heap 0x0063a210). Finally
// 0x0044ad50 (al = 0) and PhysicsManager_shutdown 0x00411cb0(g_game +0xa8c). Upper eax bytes passed to 0x0044ad50
// are the previous call's (it only tests al further down).
uint32_t fn_00403940(void* game)
{
    uint32_t r = fn_00403370(game, 0, 0);
    orig::fn_0044ad50((r & 0xffffff00u) | 1u);
    const uint32_t lm = Field<uint32_t>(game, 0xa64);
    r = orig::fn_0044a600(lm);
    if (!static_cast<uint8_t>(r))
    {
        r = orig::fn_0044a5f0(lm);
        if (static_cast<uint8_t>(r))
        {
            orig::fn_004160d0(Field<uint32_t>(game, 0xa7c));
            orig::fn_0044a670(Field<uint32_t>(game, 0xa64));
            fn_00402e70(game);
            (*reinterpret_cast<SleepFn*>(0x0056b0b8))(0);
            for (void* o = GameGlobal<void*>(0x0063a0d8); o; o = GameGlobal<void*>(0x0063a0d8))
            {
                GameGlobal<uint32_t>(0x0063a0d8) = Field<uint32_t>(o, 0x544);
                VCall<void>(o, 0x38 / 4, 1u);
            }
            r = orig::fn_004e0ab0(kHeap);
        }
    }
    orig::fn_0044ad50(r & 0xffffff00u);
    return orig::fn_00411cb0(Field<uint32_t>(GGame(), 0xa8c));
}
static uint32_t fn_00403940_Abi(uint32_t edi)
{
    return fn_00403940(P(edi));
}
ABI_ENTRY_00403940(fn_00403940_Entry, fn_00403940_Abi)
HOOK(0x00403940, "fn_00403940", fn_00403940_Entry, nullptr);

// 0x004039f0 (esi = game; caller Game_Init). Renderer (g_game +0x38) vt+8(); when it fails:
// LoadingScreen_ErrorLoop 0x004495b0(-1) (does not return). Then 0x00488880(g_game +0xa78) (ScenePass) and a tail
// jump to 0x00403590 (esi = game).
uint32_t fn_004039f0(void* game)
{
    if (!static_cast<uint8_t>(VCall<uint32_t>(Field<void*>(GGame(), 0x38), 2)))
        orig::fn_004495b0(0xffffffffu);
    orig::fn_00488880(Field<uint32_t>(GGame(), 0xa78));
    return fn_00403590(game);
}
static uint32_t fn_004039f0_Abi(uint32_t esi)
{
    return fn_004039f0(P(esi));
}
ABI_ENTRY_004039f0(fn_004039f0_Entry, fn_004039f0_Abi)
HOOK(0x004039f0, "fn_004039f0", fn_004039f0_Entry, nullptr);

// 0x00403aa0 (ecx = game; stack: a0, a1; ret 8). +0xad0 = a0 (a float scale; 1.0 = off), +0xad4 = a1 (a timer),
// then 0x00403ac0(0).
uint32_t fn_00403aa0(void* game, uint32_t a0, uint32_t a1)
{
    Field<uint32_t>(game, 0xad0) = a0;
    Field<uint32_t>(game, 0xad4) = a1;
    return fn_00403ac0(game, 0.0f);
}
static uint32_t fn_00403aa0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00403aa0(P(ecx), a0, a1);
}
ABI_ENTRY_00403aa0(fn_00403aa0_Entry, fn_00403aa0_Abi)
HOOK(0x00403aa0, "fn_00403aa0", fn_00403aa0_Entry, nullptr);

// 0x00403ac0 (ecx = game; stack: dt; ret 4). Times the scale +0xad0 (guess: a time-scale effect): active when
// +0xad0 != 1.0 (bit compare) and +0x2684 == 0. With a timer (+0xad4 != 0) the timer becomes +0xad4 - dt / +0xad0;
// at or below 0 the effect ends (+0xad0 = 1.0, +0xad4 = 0, inactive). When the active state or the scale differs
// from the applied one (byte 0x00604abc, float 0x00604ac0): byte 0x00604abc = active, value = active ? +0xad0 : 1.0,
// 0x00604ac0 = value (0.2 when value < 0) and a tail jump to (g_game +0x2c) vt+0x94(value).
uint32_t fn_00403ac0(void* game, float dt)
{
    bool active;
    if (Field<uint32_t>(game, 0xad0) == 0x3f800000u || Field<uint32_t>(game, 0x2684) != 0)
        active = false;
    else
    {
        active = true;
        if (!(Field<float>(game, 0xad4) == GameGlobal<float>(0x0056c05c)))
        {
            const float t = Field<float>(game, 0xad4) - dt / Field<float>(game, 0xad0);
            Field<float>(game, 0xad4) = t;
            if (t <= GameGlobal<float>(0x0056c05c))
            {
                Field<uint32_t>(game, 0xad0) = 0x3f800000;
                Field<uint32_t>(game, 0xad4) = 0;
                active = false;
            }
        }
    }
    const uint8_t a = active ? 1 : 0;
    if (a == GameGlobal<uint8_t>(0x00604abc))
    {
        if (!a)
            return 0;  // original: eax as left
        if (GameGlobal<float>(0x00604ac0) == Field<float>(game, 0xad0))
            return 0;  // original: eax as left
    }
    const uint32_t value = a ? Field<uint32_t>(game, 0xad0) : 0x3f800000u;
    GameGlobal<uint8_t>(0x00604abc) = a;
    if (u2f(value) < GameGlobal<float>(0x0056c05c))
        GameGlobal<uint32_t>(0x00604ac0) = 0x3e4ccccd;
    else
        GameGlobal<uint32_t>(0x00604ac0) = value;
    return VCall<uint32_t>(Field<void*>(GGame(), 0x2c), 0x94 / 4, value);
}
static uint32_t fn_00403ac0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00403ac0(P(ecx), u2f(a0));
}
ABI_ENTRY_00403ac0(fn_00403ac0_Entry, fn_00403ac0_Abi)
HOOK(0x00403ac0, "fn_00403ac0", fn_00403ac0_Entry, nullptr);

// 0x00403bb0 (edi = game). True when an object +0x748 is set, 0x00454780 (esi = it) is false and the level manager
// (+0xa64) state +0xc is 1. The al result keeps the level manager pointer's upper bytes (sete); false returns 0
// with the previous eax's upper bytes.
uint32_t fn_00403bb0(void* game)
{
    const uint32_t o = Field<uint32_t>(game, 0x748);
    if (!o)
        return 0;
    const uint32_t r = orig::fn_00454780(o);
    if (static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    const uint32_t lm = Field<uint32_t>(game, 0xa64);
    return (lm & 0xffffff00u) | (Field<uint32_t>(P(lm), 0xc) == 1 ? 1u : 0u);
}
static uint32_t fn_00403bb0_Abi(uint32_t edi)
{
    return fn_00403bb0(P(edi));
}
ABI_ENTRY_00403bb0(fn_00403bb0_Entry, fn_00403bb0_Abi)
HOOK(0x00403bb0, "fn_00403bb0", fn_00403bb0_Entry, nullptr);

namespace
{
// The inlined reference release used by object methods (0x00404600, 0x00404680): --obj+0x28; at 0 and when
// obj vt+0xd8() returns false, 0x00487da0 (eax = obj) frees it.
void ReleaseRef(uint32_t obj)
{
    if (--Field<int32_t>(P(obj), 0x28) != 0)
        return;
    if (!static_cast<uint8_t>(VCall<uint32_t>(P(obj), 0xd8 / 4)))
        orig::fn_00487da0(obj);
}
}  // namespace

// 0x00404550 (ecx = object; stack: 4 arguments, none read; ret 0x10). AIInterestObject (vtable 0x005a7820) slot 6,
// the load slot: +0x400..+0x40c, 0x62 dwords at +0x34, 0x20 dwords at +0x248 and +0x478 = 0; +0x2d8 = a new 0x18-byte
// list (heap 0x0063a210; 0x004e7010 eax = g_game +0xac4, edx = 0, esi = block; 0 when the allocation fails);
// registered in the lists (g_game +0xa84)+0xe70 and +0xee0 (0x004546d0 esi = list, edi = object); then object
// vt+0x1c(0). Not executed in recorded runs: not verifiable by compare yet.
uint32_t AIInterestObject_Load(void* obj, uint32_t, uint32_t, uint32_t, uint32_t)
{
    uint32_t* a = &Field<uint32_t>(obj, 0x400);
    a[0] = 0;
    a[1] = 0;
    a[2] = 0;
    a[3] = 0;
    std::memset(&Field<uint8_t>(obj, 0x34), 0, 0x62 * 4);
    std::memset(&Field<uint8_t>(obj, 0x248), 0, 0x20 * 4);
    Field<uint32_t>(obj, 0x478) = 0;
    const uint32_t block = HeapAlloc(0x18);
    Field<uint32_t>(obj, 0x2d8) = block ? orig::fn_004e7010(Field<uint32_t>(GGame(), 0xac4), 0, block) : 0;
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xe70), u(obj));
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xee0), u(obj));
    return VCall<uint32_t>(obj, 0x1c / 4, 0u);
}
static uint32_t AIInterestObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return AIInterestObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00404550(AIInterestObject_Load_Entry, AIInterestObject_Load_Abi)
HOOK(0x00404550, "AIInterestObject_Load", AIInterestObject_Load_Entry, nullptr);

// 0x00404600 (ecx = object). AIInterestObject (vtable 0x005a7820) slot 0x37 (+0xdc, remove): Object_Remove 0x004547b0
// (ecx = object), then releases +0x478 (not null-checked: original quirk) and +0x474 (when set) and clears both.
// Not executed in recorded runs: not verifiable by compare yet.
uint32_t AIInterestObject_Remove(void* obj)
{
    orig::fn_004547b0(u(obj));
    ReleaseRef(Field<uint32_t>(obj, 0x478));
    Field<uint32_t>(obj, 0x478) = 0;
    if (const uint32_t p = Field<uint32_t>(obj, 0x474))
    {
        ReleaseRef(p);
        Field<uint32_t>(obj, 0x474) = 0;
    }
    return 0;  // original: eax as left
}
static uint32_t AIInterestObject_Remove_Abi(uint32_t ecx)
{
    return AIInterestObject_Remove(P(ecx));
}
ABI_ENTRY_00404600(AIInterestObject_Remove_Entry, AIInterestObject_Remove_Abi)
HOOK(0x00404600, "AIInterestObject_Remove", AIInterestObject_Remove_Entry, nullptr);

// 0x00404680 (ecx = object). AIInterestObject (vtable 0x005a7820) slot 0x2f (+0xbc, update): when the target
// +0x478 vt+0xd8() is false the object removes itself (tail jump to its vt+0xdc). When +0x474 is set: +0x474
// vt+0x54(&+0x468); when its vt+0xd8() is false it is released and cleared, and the object removes itself when the
// timer +0x460 is 0.0. Then while the timer is positive it counts down by the frame time [0x00604a90]; when it goes
// below 0 the object removes itself (tail jump to vt+0xdc). Not executed in recorded runs: not verifiable by compare
// yet.
uint32_t AIInterestObject_Update(void* obj)
{
    const float zero = GameGlobal<float>(0x0056c05c);
    if (!static_cast<uint8_t>(VCall<uint32_t>(Field<void*>(obj, 0x478), 0xd8 / 4)))
        return VCall<uint32_t>(obj, 0xdc / 4);
    if (void* p = Field<void*>(obj, 0x474))
    {
        VCall<void>(p, 0x54 / 4, u(obj) + 0x468);
        if (!static_cast<uint8_t>(VCall<uint32_t>(Field<void*>(obj, 0x474), 0xd8 / 4)))
        {
            ReleaseRef(Field<uint32_t>(obj, 0x474));
            Field<uint32_t>(obj, 0x474) = 0;
            if (Field<float>(obj, 0x460) == zero)
                VCall<void>(obj, 0xdc / 4);
        }
    }
    if (Field<float>(obj, 0x460) > zero)
    {
        const float t = Field<float>(obj, 0x460) - GameGlobal<float>(0x00604a90);
        Field<float>(obj, 0x460) = t;
        if (t < zero)
            return VCall<uint32_t>(obj, 0xdc / 4);
    }
    return 0;  // original: eax as left (fnstsw)
}
static uint32_t AIInterestObject_Update_Abi(uint32_t ecx)
{
    return AIInterestObject_Update(P(ecx));
}
ABI_ENTRY_00404680(AIInterestObject_Update_Entry, AIInterestObject_Update_Abi)
HOOK(0x00404680, "AIInterestObject_Update", AIInterestObject_Update_Entry, nullptr);

// 0x004056b0 (ecx = object; stack: a0..a3; ret 0x10; frame 0x80). PeasantObject (vtable 0x005a8550) slot 6, the
// load slot: 0x00457810 (eax = object), byte +0x48c = 0, +0x498 = 0, the base load 0x00497460 (ecx = object; same
// arguments). +0x480 = 0, +0x478 = -1.0, +0x47c = -1. Properties: "count" (0x005bbee0, default 1) -> +0x484
// (0x00453400 eax = default, esi = a3, edi = target; object, name, a2, 0), "saved_score_value" (0x005bbecc) -> +0x490
// and "killed_score_value" (0x005bbeb8) -> +0x494 (0x004534a0 esi = a3, edi = target; object, name, a2, 0, 0).
// Registered in (g_game +0xa84) lists +0xe9c, +0xef8, +0xefc (0x004546d0). Animation 0x00408e30 (eax = 0x81, ecx =
// object, edx = -1; "peasant_man_stand" 0x005bbea4, 0, 0, 0, 0). x = ((+0x1c4)+8)+0x2c: 0x00458270 (edx = object;
// 0, 0, (x == 0 ? 0 : 1 / x) * 20000.0). Object_setPhysicsEnabled 0x004577b0 (eax = object; 1). +0x498 =
// Fx_Create 0x0041cc60 (al = 0, ecx = g_game +0xa90; "FX_peasant" 0x005bbe98, &desc), desc being a local block at
// frame +0x10 with +0 = 0x24, +4 = 8, +0x18 = 0, +0x1c = object (the rest left as is). Not executed in recorded
// runs: not verifiable by compare yet.
uint32_t PeasantObject_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t frame[0x80 / 4];
    orig::fn_00457810(u(obj));
    Field<uint8_t>(obj, 0x48c) = 0;
    Field<uint32_t>(obj, 0x498) = 0;
    orig::fn_00497460(u(obj), a0, a1, a2, a3);
    Field<uint32_t>(obj, 0x480) = 0;
    Field<uint32_t>(obj, 0x478) = 0xbf800000u;
    Field<uint32_t>(obj, 0x47c) = 0xffffffffu;
    orig::fn_00453400(1, a3, u(obj) + 0x484, u(obj), 0x005bbee0, a2, 0);
    orig::fn_004534a0(a3, u(obj) + 0x490, u(obj), 0x005bbecc, a2, 0, 0);
    orig::fn_004534a0(a3, u(obj) + 0x494, u(obj), 0x005bbeb8, a2, 0, 0);
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xe9c), u(obj));
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xef8), u(obj));
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xefc), u(obj));
    orig::fn_00408e30(0x81, u(obj), 0xffffffffu, 0x005bbea4, 0, 0, 0, 0);
    const float x = Field<float>(Field<void*>(Field<void*>(obj, 0x1c4), 8), 0x2c);
    float v;
    if (x == GameGlobal<float>(0x0056c05c))
        v = GameGlobal<float>(0x0056c05c);
    else
        v = GameGlobal<float>(0x005bcb2c) / x;
    v = v * GameGlobal<float>(0x005bcd34);
    orig::fn_00458270(u(obj), 0, 0, f2u(v));
    const uint32_t r = static_cast<uint32_t>(orig::fn_004577b0(u(obj), 1));
    uint32_t* desc = &frame[0x10 / 4];
    desc[0] = 0x24;
    desc[1] = 8;
    desc[0x18 / 4] = 0;
    desc[0x1c / 4] = u(obj);
    const uint32_t fx = orig::fn_0041cc60(r & 0xffffff00u, Field<uint32_t>(GGame(), 0xa90), 0x005bbe98, u(desc));
    Field<uint32_t>(obj, 0x498) = fx;
    return fx;
}
static uint32_t PeasantObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PeasantObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004056b0(PeasantObject_Load_Entry, PeasantObject_Load_Abi)
HOOK(0x004056b0, "PeasantObject_Load", PeasantObject_Load_Entry, nullptr);

// 0x00405850 (ecx = object). PeasantObject (vtable 0x005a8550) slot 0x37 (+0xdc, remove): Fx_Release 0x0041d030
// (g_game +0xa90, &+0x498) when +0x498 is set, then a tail jump to Object_Remove 0x004547b0 (ecx = object). Not
// executed in recorded runs: not verifiable by compare yet.
uint32_t PeasantObject_Remove(void* obj)
{
    if (Field<uint32_t>(obj, 0x498))
        orig::fn_0041d030(Field<uint32_t>(GGame(), 0xa90), u(obj) + 0x498);
    return orig::fn_004547b0(u(obj));
}
static uint32_t PeasantObject_Remove_Abi(uint32_t ecx)
{
    return PeasantObject_Remove(P(ecx));
}
ABI_ENTRY_00405850(PeasantObject_Remove_Entry, PeasantObject_Remove_Abi)
HOOK(0x00405850, "PeasantObject_Remove", PeasantObject_Remove_Entry, nullptr);

// 0x00405880 (ecx = object; stack: target; ret 4). PeasantObject (vtable 0x005a8550) slot 7: target +0 = 0x60000,
// byte +0x79 = 1, byte +0x12 = 0, 0x004527d0 (ecx = object; target), then 0x004574e0(object, target, (float)(unsigned)
// +0x470, flags) with flags = (((byte +0x474) & 2) | 1) << 3; target +0 = flags | 0x60000; 0x00458600 (esi =
// object) when +0x1bc is set. Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00405880(void* obj, uint32_t target)
{
    Field<uint32_t>(P(target), 0) = 0x60000;
    Field<uint8_t>(P(target), 0x79) = 1;
    Field<uint8_t>(P(target), 0x12) = 0;
    orig::fn_004527d0(u(obj), target);
    const float count = static_cast<float>(Field<uint32_t>(obj, 0x470));
    const uint32_t flags = ((Field<uint8_t>(obj, 0x474) & 2u) | 1u) << 3;
    uint32_t r = orig::fn_004574e0(u(obj), target, f2u(count), flags);
    Field<uint32_t>(P(target), 0) = flags | 0x60000;
    r = Field<uint32_t>(obj, 0x1bc);
    if (r)
        r = orig::fn_00458600(u(obj));
    return r;
}
static uint32_t fn_00405880_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00405880(P(ecx), a0);
}
ABI_ENTRY_00405880(fn_00405880_Entry, fn_00405880_Abi)
HOOK(0x00405880, "fn_00405880", fn_00405880_Entry, nullptr);

// 0x00406210 (ecx = object; stack: a0, a1; ret 8). PeasantObject (vtable 0x005a8550) slot 0x2d: when a1 is set,
// object vt+0xd8() is true, a0 (byte) is set, g_game +0x26f4 < 3 (unsigned) and 0x004539e0 (eax = object) is false:
// byte +0x48c = 1 and, when +0x460 is set, Object_SendTrigger 0x00461870 (eax = g_game +0xa84, ebx = +0x460;
// object) and +0x460 = 0. (guess: the peasant is rescued.) Not executed in recorded runs: not verifiable by compare
// yet.
uint32_t fn_00406210(void* obj, uint32_t a0, uint32_t a1)
{
    if (!a1)
        return 0;
    if (!static_cast<uint8_t>(VCall<uint32_t>(obj, 0xd8 / 4)) || !static_cast<uint8_t>(a0))
        return 0;
    void* g = GGame();
    if (Field<uint32_t>(g, 0x26f4) >= 3)
        return 0;
    if (static_cast<uint8_t>(orig::fn_004539e0(u(obj))))
        return 0;
    Field<uint8_t>(obj, 0x48c) = 1;
    if (const uint32_t trigger = Field<uint32_t>(obj, 0x460))
    {
        orig::fn_00461870(Field<uint32_t>(g, 0xa84), trigger, u(obj));
        Field<uint32_t>(obj, 0x460) = 0;
    }
    return 0;  // original: eax as left
}
static uint32_t fn_00406210_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00406210(P(ecx), a0, a1);
}
ABI_ENTRY_00406210(fn_00406210_Entry, fn_00406210_Abi)
HOOK(0x00406210, "fn_00406210", fn_00406210_Entry, nullptr);

// 0x00406280 (ecx = object; stack: a0; ret 4). PeasantObject (vtable 0x005a8550) slot 0x34: unless 0x004539e0
// (eax = object) is true, a tail jump to the base slot 0x004555e0 (ecx = object; a0). Returns 0x004539e0's eax
// otherwise. Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00406280(void* obj, uint32_t a0)
{
    const uint32_t r = orig::fn_004539e0(u(obj));
    if (static_cast<uint8_t>(r))
        return r;
    return orig::fn_004555e0(u(obj), a0);
}
static uint32_t fn_00406280_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00406280(P(ecx), a0);
}
ABI_ENTRY_00406280(fn_00406280_Entry, fn_00406280_Abi)
HOOK(0x00406280, "fn_00406280", fn_00406280_Entry, nullptr);

// 0x004062a0 (ecx = object; stack: a0; ret 4). PeasantObject (vtable 0x005a8550) slot 0x35 (kill): the
// "killed_score_value" +0x494 is added to g_game +0x2728 when negative, else to +0x2724. When the count +0x484 is
// not 0: g_game +0x26f4 -= count when byte +0x48c is set, g_game +0x26e4 += count, and when +0x26e4 >= +0x26e0
// (unsigned) 0x0044a580 (eax = g_game +0xa64, ecx = 8). Then Object_Kill 0x00455640 (ecx = object; a0). Not
// executed in recorded runs: not verifiable by compare yet.
uint32_t fn_004062a0(void* obj, uint32_t a0)
{
    void* g = GGame();
    const float v = Field<float>(obj, 0x494);
    if (v < GameGlobal<float>(0x0056c05c))
        Field<float>(g, 0x2728) = v + Field<float>(g, 0x2728);
    else
        Field<float>(g, 0x2724) = v + Field<float>(g, 0x2724);
    const uint32_t n = Field<uint32_t>(obj, 0x484);
    if (n != 0)
    {
        if (Field<uint8_t>(obj, 0x48c))
            Field<uint32_t>(g, 0x26f4) -= n;
        const uint32_t total = Field<uint32_t>(g, 0x26e4) + Field<uint32_t>(obj, 0x484);
        Field<uint32_t>(g, 0x26e4) = total;
        if (total >= Field<uint32_t>(g, 0x26e0))
            orig::fn_0044a580(Field<uint32_t>(g, 0xa64), 8);
    }
    return orig::fn_00455640(u(obj), a0);
}
static uint32_t fn_004062a0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004062a0(P(ecx), a0);
}
ABI_ENTRY_004062a0(fn_004062a0_Entry, fn_004062a0_Abi)
HOOK(0x004062a0, "fn_004062a0", fn_004062a0_Entry, nullptr);

// 0x00406330 (no inputs; caller 0x0044d600). Returns the object of the list (g_game +0xa84)+0xef8 with the largest
// +0x480 (signed, first wins on ties, only values above 0), or 0. Iterator 0x004e7340 (eax = list, esi = iterator) /
// 0x004e73c0 (edi = iterator); the iterator node (second dword) loses a reference (+0x18) afterwards. Not executed in
// recorded runs: not verifiable by compare yet.
uint32_t fn_00406330()
{
    uint32_t iterator[2] = {0, 0};
    int32_t best = 0;
    uint32_t found = 0;
    const uint32_t list = Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xef8);
    for (uint32_t o = orig::fn_004e7340(list, u(iterator)); o; o = orig::fn_004e73c0(u(iterator)))
    {
        const int32_t v = Field<int32_t>(P(o), 0x480);
        if (best < v)
        {
            found = o;
            best = v;
        }
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return found;
}
ABI_ENTRY_00406330(fn_00406330_Entry, fn_00406330)
HOOK(0x00406330, "fn_00406330", fn_00406330_Entry, nullptr);

// 0x004063a0 (ecx = peasant). Banks the peasant's count: g_game +0x26e8 += +0x484, +0x26f4 -= +0x484, +0x272c +=
// "saved_score_value" +0x490, then +0x484 = 0. Returns g_game. Not executed in recorded runs: not verifiable by
// compare yet.
uint32_t fn_004063a0(void* obj)
{
    void* g = GGame();
    Field<uint32_t>(g, 0x26e8) += Field<uint32_t>(obj, 0x484);
    Field<uint32_t>(g, 0x26f4) = Field<uint32_t>(g, 0x26f4) - Field<uint32_t>(obj, 0x484);
    Field<float>(g, 0x272c) = Field<float>(obj, 0x490) + Field<float>(g, 0x272c);
    Field<uint32_t>(obj, 0x484) = 0;
    return u(g);
}
static uint32_t fn_004063a0_Abi(uint32_t ecx)
{
    return fn_004063a0(P(ecx));
}
ABI_ENTRY_004063a0(fn_004063a0_Entry, fn_004063a0_Abi)
HOOK(0x004063a0, "fn_004063a0", fn_004063a0_Entry, nullptr);

// 0x004063f0 (eax = peasant). Returns its count +0x484. Not executed in recorded runs: not verifiable by compare
// yet.
uint32_t fn_004063f0(void* obj)
{
    return Field<uint32_t>(obj, 0x484);
}
static uint32_t fn_004063f0_Abi(uint32_t eax)
{
    return fn_004063f0(P(eax));
}
ABI_ENTRY_004063f0(fn_004063f0_Entry, fn_004063f0_Abi)
HOOK(0x004063f0, "fn_004063f0", fn_004063f0_Entry, nullptr);

// 0x00406400 (eax = block). Clears bytes +0xc0, +0xc1, +0xa0 and dwords +0, +4, +8, +0xc, +0x98, +0x9c.
void fn_00406400(void* p)
{
    Field<uint8_t>(p, 0xc0) = 0;
    Field<uint8_t>(p, 0xc1) = 0;
    Field<uint32_t>(p, 0) = 0;
    Field<uint32_t>(p, 0x98) = 0;
    Field<uint32_t>(p, 0x9c) = 0;
    Field<uint32_t>(p, 4) = 0;
    Field<uint32_t>(p, 8) = 0;
    Field<uint32_t>(p, 0xc) = 0;
    Field<uint8_t>(p, 0xa0) = 0;
}
static void fn_00406400_Abi(uint32_t eax)
{
    fn_00406400(P(eax));
}
ABI_ENTRY_00406400(fn_00406400_Entry, fn_00406400_Abi)
HOOK(0x00406400, "fn_00406400", fn_00406400_Entry, nullptr);

// 0x00406430 (eax = player block (layout of 0x00406400), ebx = argument; ecx only pushed as a stack slot). Drives
// a range +4 (start +0x14, end +0x18, scale +0x1c * +0x20) with the position +8 and the second position +0xc
// through 0x004eb480 (esi = range; to, from, callback 0x00409140, ebx; caller pops), to = (int)(scale * pos + start).
// ratio = (end - start) / scale. When ratio > +8 (or unordered): 0x004eb480(scale * +0xc + start, scale * +8 +
// start) and false. Otherwise the same call; then when byte +0xc0: +0xc = 0, +8 -= ratio, 0x004eb480(scale * 0.0 +
// start, scale * +8 + start), and when byte +0xc1 and ebx: 0x00457990 (al = 0; ebx, block + 0x10); false. Without
// +0xc0: +8 = ratio and true (al = 1, upper bytes the range pointer's).
uint32_t fn_00406430(void* p, uint32_t /*ecx*/, uint32_t ebx)
{
    void* s = Field<void*>(p, 4);
    float ratio = static_cast<float>(Field<int32_t>(s, 0x18) - Field<int32_t>(s, 0x14)) /
                  static_cast<float>(Field<int32_t>(s, 0x20) * Field<int32_t>(s, 0x1c));
    const bool above = !(ratio <= Field<float>(p, 8));
    float x = static_cast<float>(Field<int32_t>(s, 0x20)) * static_cast<float>(Field<int32_t>(s, 0x1c)) *
                  Field<float>(p, 8) +
              static_cast<float>(Field<int32_t>(s, 0x14));
    int32_t a = static_cast<int32_t>(x);
    x = static_cast<float>(Field<int32_t>(s, 0x20)) * static_cast<float>(Field<int32_t>(s, 0x1c)) * Field<float>(p, 0xc) +
        static_cast<float>(Field<int32_t>(s, 0x14));
    int32_t b = static_cast<int32_t>(x);
    uint32_t r = orig::fn_004eb480(u(s), static_cast<uint32_t>(b), static_cast<uint32_t>(a), 0x00409140, ebx);
    if (above)
        return r & 0xffffff00u;
    if (!Field<uint8_t>(p, 0xc0))
    {
        s = Field<void*>(p, 4);
        Field<float>(p, 8) = static_cast<float>(Field<int32_t>(s, 0x18) - Field<int32_t>(s, 0x14)) /
                             static_cast<float>(Field<int32_t>(s, 0x20) * Field<int32_t>(s, 0x1c));
        return (u(s) & 0xffffff00u) | 1u;
    }
    s = Field<void*>(p, 4);
    Field<uint32_t>(p, 0xc) = 0;
    ratio = static_cast<float>(Field<int32_t>(s, 0x18) - Field<int32_t>(s, 0x14)) /
            static_cast<float>(Field<int32_t>(s, 0x20) * Field<int32_t>(s, 0x1c));
    const float t = Field<float>(p, 8) - ratio;
    Field<float>(p, 8) = t;
    x = static_cast<float>(Field<int32_t>(s, 0x20)) * static_cast<float>(Field<int32_t>(s, 0x1c)) * t +
        static_cast<float>(Field<int32_t>(s, 0x14));
    a = static_cast<int32_t>(x);
    x = static_cast<float>(Field<int32_t>(s, 0x20)) * static_cast<float>(Field<int32_t>(s, 0x1c)) *
            GameGlobal<float>(0x0056c05c) +
        static_cast<float>(Field<int32_t>(s, 0x14));
    b = static_cast<int32_t>(x);
    r = orig::fn_004eb480(u(s), static_cast<uint32_t>(b), static_cast<uint32_t>(a), 0x00409140, ebx);
    if (Field<uint8_t>(p, 0xc1) && ebx)
        r = orig::fn_00457990(r & 0xffffff00u, ebx, u(p) + 0x10);
    return r & 0xffffff00u;
}
static uint32_t fn_00406430_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx)
{
    return fn_00406430(P(eax), ecx, ebx);
}
ABI_ENTRY_00406430(fn_00406430_Entry, fn_00406430_Abi)
HOOK(0x00406430, "fn_00406430", fn_00406430_Entry, nullptr);

// 0x004065d0 (edx = block; caller Game_Init for g_game +0xa6c, size 0x3b0). Constructor: clears +0, +0x58, +0x84,
// +0x2c, word +0x392, byte +0x394, +0x398, +0x39c, bytes +0x150/+0x153, two dwords in each of 4 entries of 0x2c from
// +0x154 (+0 and +0x1c) and of 6 entries from +0x204, 0x1c dwords at +0x30c and +0x37c..+0x38c; word +0x390 = 2.
// Returns the block.
uint32_t fn_004065d0(void* p)
{
    Field<uint32_t>(p, 0) = 0;
    Field<uint32_t>(p, 0x58) = 0;
    Field<uint32_t>(p, 0x84) = 0;
    Field<uint32_t>(p, 0x2c) = 0;
    Field<uint16_t>(p, 0x392) = 0;
    Field<uint16_t>(p, 0x390) = 2;
    Field<uint8_t>(p, 0x394) = 0;
    Field<uint32_t>(p, 0x398) = 0;
    Field<uint32_t>(p, 0x39c) = 0;
    Field<uint8_t>(p, 0x150) = 0;
    Field<uint8_t>(p, 0x153) = 0;
    for (uint32_t i = 0, o = 0x170; i < 4; ++i, o += 0x2c)
    {
        Field<uint32_t>(p, o - 0x1c) = 0;
        Field<uint32_t>(p, o) = 0;
    }
    for (uint32_t i = 0, o = 0x220; i < 6; ++i, o += 0x2c)
    {
        Field<uint32_t>(p, o - 0x1c) = 0;
        Field<uint32_t>(p, o) = 0;
    }
    std::memset(&Field<uint8_t>(p, 0x30c), 0, 0x1c * 4);
    Field<uint32_t>(p, 0x37c) = 0;
    Field<uint32_t>(p, 0x380) = 0;
    Field<uint32_t>(p, 0x384) = 0;
    Field<uint32_t>(p, 0x388) = 0;
    Field<uint32_t>(p, 0x38c) = 0;
    return u(p);
}
static uint32_t fn_004065d0_Abi(uint32_t edx)
{
    return fn_004065d0(P(edx));
}
ABI_ENTRY_004065d0(fn_004065d0_Entry, fn_004065d0_Abi)
HOOK(0x004065d0, "fn_004065d0", fn_004065d0_Entry, nullptr);

// 0x004087b0 (eax = index pointer or null, ecx = object). Returns 0 for null, else the third dword of entry *index
// (12 bytes each) of the table ((object +0x414)+0xc) (an animation range; guess).
uint32_t fn_004087b0(uint32_t index, void* obj)
{
    if (!index)
        return 0;
    const uint32_t i = Field<uint32_t>(P(index), 0);
    const uint8_t* table = Field<uint8_t*>(Field<void*>(obj, 0x414), 0xc);
    return *reinterpret_cast<const uint32_t*>(table + i * 12 + 8);
}
static uint32_t fn_004087b0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004087b0(eax, P(ecx));
}
ABI_ENTRY_004087b0(fn_004087b0_Entry, fn_004087b0_Abi)
HOOK(0x004087b0, "fn_004087b0", fn_004087b0_Entry, nullptr);

// 0x004087d0 (edx = block; stack: one argument, not read; ret 4). Clears 0x20 dwords and sets the first dword of each
// of the four 0x20-byte entries to -1. Returns block + 0x80. (Callers 0x004517b0, 0x00458970: the animation slots
// read by 0x00408800.)
uint32_t fn_004087d0(void* p, uint32_t /*a0*/)
{
    std::memset(p, 0, 0x20 * 4);
    uint8_t* e = static_cast<uint8_t*>(p);
    for (int i = 0; i < 4; ++i, e += 0x20)
        *reinterpret_cast<uint32_t*>(e) = 0xffffffffu;
    return u(e);
}
static uint32_t fn_004087d0_Abi(uint32_t edx, uint32_t a0)
{
    return fn_004087d0(P(edx), a0);
}
ABI_ENTRY_004087d0(fn_004087d0_Entry, fn_004087d0_Abi)
HOOK(0x004087d0, "fn_004087d0", fn_004087d0_Entry, nullptr);

// 0x00408800 (eax = slot, ecx = object). Returns the address of animation slot entry (0x20 bytes) object + 0x248 +
// slot * 0x20; slot 4 maps to entry 0.
uint32_t fn_00408800(uint32_t slot, void* obj)
{
    if (slot == 4)
        return u(obj) + 0x248;
    return (slot << 5) + u(obj) + 0x248;
}
static uint32_t fn_00408800_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00408800(eax, P(ecx));
}
ABI_ENTRY_00408800(fn_00408800_Entry, fn_00408800_Abi)
HOOK(0x00408800, "fn_00408800", fn_00408800_Entry, nullptr);

// 0x0040a4d0 (edx = name/node, ebx = output matrix; stack: object, aspect, &fov, &a3out, &a4out; ret 0x14; ebp frame
// aligned to 8, 0xf8 bytes). Looks up a camera node of the object (callers Camera_Update 0x0040e350 and 0x0040fbb0):
// a query block at frame +0x70 (0x44 bytes cleared, identity matrix at +0x44, +0x88 = edx, +0x8c = &result frame
// +0x18) goes to 0x00453c50(object, edx, ebx, 1, &query, 1, 1, 0, 0). On success the result node (*(+0x8c)) is kept
// when its word +4 is 1; on failure ebx becomes identity. 0x00457850 (eax = &v, ecx = object) gives a vector; a
// translation matrix (identity, +0xc/+0x1c/+0x2c = v * -0.5, flags +0x40 = 0x10) is applied: Matrix_Multiply
// 0x004e2910 (eax = ebx; ebx, &m). When fov, a3out and a4out are all set and a node was found: frame = (int)(range
// scale * slot 4 position + start) (slot 0x00408800 eax = 4; range = the table entry as in 0x004087b0); the fov is,
// by the node's word +0x9a: 0 -> 2 * atan(tan(0x004eb710(eax = node; frame) * 0.5) / aspect), 1 -> 0x004eb710
// itself, else pi/2 [0x005bce6c]; *a3out = node +0xa0, *a4out = node +0xa4; returns true. Otherwise false.
uint32_t fn_0040a4d0(uint32_t edx, float* out, void* obj, float aspect, float* fov, uint32_t* a3out, uint32_t* a4out)
{
    alignas(8) uint32_t L[0x100 / 4];  // L[X / 4] = the original [esp + X] after the two register pushes
    std::memset(&L[0x70 / 4], 0, 0x24 * 4);
    L[0x18 / 4] = 0;
    L[0x1c / 4] = 0;
    L[0x20 / 4] = 0;
    std::memset(&L[0xb4 / 4], 0, 0x11 * 4);
    L[0x24 / 4] = 0;
    uint32_t node = 0;
    L[0xb4 / 4] = 0x3f800000;
    L[0xc8 / 4] = 0x3f800000;
    L[0xdc / 4] = 0x3f800000;
    L[0xf0 / 4] = 0x3f800000;
    L[0xf8 / 4] = edx;
    L[0xfc / 4] = u(&L[0x18 / 4]);
    const uint8_t all = (fov && a3out && a4out) ? 1 : 0;
    if (static_cast<uint8_t>(orig::fn_00453c50(u(obj), edx, u(out), 1, u(&L[0x70 / 4]), 1, 1, 0, 0)))
    {
        const uint32_t hit = *reinterpret_cast<uint32_t*>(L[0xfc / 4]);
        if (hit)
            node = Field<uint16_t>(P(hit), 4) == 1 ? hit : 0;
    }
    else
    {
        std::memset(out, 0, 0x11 * 4);
        reinterpret_cast<uint32_t*>(out)[0] = 0x3f800000;
        reinterpret_cast<uint32_t*>(out)[5] = 0x3f800000;
        reinterpret_cast<uint32_t*>(out)[10] = 0x3f800000;
        reinterpret_cast<uint32_t*>(out)[15] = 0x3f800000;
    }
    orig::fn_00457850(u(&L[0xc / 4]), u(obj));
    const float half = GameGlobal<float>(0x005bcbc8);  // -0.5
    const float x = u2f(L[0xc / 4]) * half;
    const float y = u2f(L[0x10 / 4]) * half;
    const float z = u2f(L[0x14 / 4]) * half;
    std::memset(&L[0x28 / 4], 0, 0x11 * 4);
    L[0x14 / 4] = f2u(z);
    L[0x34 / 4] = f2u(x);
    L[0x44 / 4] = f2u(y);
    L[0x28 / 4] = 0x3f800000;
    L[0x3c / 4] = 0x3f800000;
    L[0x50 / 4] = 0x3f800000;
    L[0x64 / 4] = 0x3f800000;
    L[0x54 / 4] = f2u(z);
    L[0x68 / 4] = 0x10;
    orig::fn_004e2910(u(out), u(out), u(&L[0x28 / 4]));
    if (!all || !node)
        return 0;
    const uint32_t slot = fn_00408800(4, obj);
    const uint32_t i = Field<uint32_t>(P(slot), 0);
    const uint8_t* table = Field<uint8_t*>(Field<void*>(obj, 0x414), 0xc);
    void* range = P(*reinterpret_cast<const uint32_t*>(table + i * 12 + 8));
    const float pos = static_cast<float>(Field<int32_t>(range, 0x20)) * static_cast<float>(Field<int32_t>(range, 0x1c)) *
                          Field<float>(P(slot), 4) +
                      static_cast<float>(Field<int32_t>(range, 0x14));
    const int32_t frame = static_cast<int32_t>(pos);
    const uint16_t kind = Field<uint16_t>(P(node), 0x9a);
    float result;
    if (kind == 0)
    {
        const float h = orig::fn_004eb710(node, static_cast<uint32_t>(frame)) * GameGlobal<float>(0x005bcb4c);
        const float q = static_cast<float>(std::tan(static_cast<double>(h)) / static_cast<double>(aspect));
        result = static_cast<float>(2.0 * std::atan(static_cast<double>(q)));
    }
    else if (kind == 1)
        result = orig::fn_004eb710(node, static_cast<uint32_t>(frame));
    else
        result = GameGlobal<float>(0x005bce6c);
    *fov = result;
    *a3out = Field<uint32_t>(P(node), 0xa0);
    const uint32_t v = Field<uint32_t>(P(node), 0xa4);
    *a4out = v;
    return (v & 0xffffff00u) | 1u;
}
static uint32_t fn_0040a4d0_Abi(uint32_t edx, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                uint32_t a4)
{
    return fn_0040a4d0(edx, static_cast<float*>(P(ebx)), P(a0), u2f(a1), static_cast<float*>(P(a2)),
                       static_cast<uint32_t*>(P(a3)), static_cast<uint32_t*>(P(a4)));
}
ABI_ENTRY_0040a4d0(fn_0040a4d0_Entry, fn_0040a4d0_Abi)
HOOK(0x0040a4d0, "fn_0040a4d0", fn_0040a4d0_Entry, nullptr);

// 0x0040a700 (ecx = object; stack: a0..a3; ret 0x10). AnimPlayer (vtable 0x005ab788, "CLASS(AnimPlayer)") slot 6,
// the load slot: +0x460 = 0, the base load 0x004517b0 (same arguments); properties through 0x00453450 (eax =
// default 0, ebx = name, esi = a3, edi = target; object, a2, 0): "receive_trigger_activate" (0x005bba00) -> +0x484,
// "receive_trigger_bkwd" (0x005bb9e8) -> +0x488; the string "anim" (0x005bb9d8, default "none" 0x005bb9e0) ->
// +0x464 (0x004533a0 eax = default, edi = target; object, name, a2, a3, 0). Registered in (g_game +0xa84)+0xf1c
// (0x004546d0). Anim_Set 0x00408d90 (eax = -1, esi = object; &+0x464, 0x90, 0, 0, 0, 0), then object vt+0xfc(1, 2).
// Not executed in recorded runs: not verifiable by compare yet.
uint32_t AnimPlayer_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Field<uint32_t>(obj, 0x460) = 0;
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    orig::fn_00453450(0, 0x005bba00, a3, u(obj) + 0x484, u(obj), a2, 0);
    orig::fn_00453450(0, 0x005bb9e8, a3, u(obj) + 0x488, u(obj), a2, 0);
    orig::fn_004533a0(0x005bb9e0, u(obj) + 0x464, u(obj), 0x005bb9d8, a2, a3, 0);
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xf1c), u(obj));
    orig::fn_00408d90(0xffffffffu, u(obj), u(obj) + 0x464, 0x90, 0, 0, 0, 0);
    return VCall<uint32_t>(obj, 0xfc / 4, 1u, 2u);
}
static uint32_t AnimPlayer_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return AnimPlayer_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0040a700(AnimPlayer_Load_Entry, AnimPlayer_Load_Abi)
HOOK(0x0040a700, "AnimPlayer_Load", AnimPlayer_Load_Entry, nullptr);

// 0x0040a7d0 (ecx = object; stack: a0, id; ret 8). AnimPlayer (vtable 0x005ab788) slot 0xe, the trigger message
// slot: id == "receive_trigger_activate" (+0x484) and not playing (+0x460 bit 0): Anim_Set (eax = -1, esi = object;
// &+0x464, bit 1 ? 0x20000 : 0, 0, 0, 0, 0), +0x460 |= 1 then &= ~2. id == "receive_trigger_bkwd" (+0x488) and not
// backwards (bit 1): Anim_Set(&+0x464, 0x20040, ...), +0x460 &= ~1 then |= 2. Returns false (al = 0). Not executed
// in recorded runs: not verifiable by compare yet.
uint32_t AnimPlayer_OnMessage(void* obj, uint32_t /*a0*/, uint32_t id)
{
    if (id == Field<uint32_t>(obj, 0x484))
    {
        const uint32_t flags = Field<uint32_t>(obj, 0x460);
        if (flags & 1)
            return flags & 0xffffff00u;
        orig::fn_00408d90(0xffffffffu, u(obj), u(obj) + 0x464, (flags & 2) ? 0x20000u : 0u, 0, 0, 0, 0);
        const uint32_t v = Field<uint32_t>(obj, 0x460) | 1u;
        Field<uint32_t>(obj, 0x460) = v;
        Field<uint32_t>(obj, 0x460) = v & ~2u;
        return (v & ~2u) & 0xffffff00u;
    }
    if (id == Field<uint32_t>(obj, 0x488) && !(Field<uint8_t>(obj, 0x460) & 2))
    {
        orig::fn_00408d90(0xffffffffu, u(obj), u(obj) + 0x464, 0x20040, 0, 0, 0, 0);
        const uint32_t v = Field<uint32_t>(obj, 0x460) & ~1u;
        Field<uint32_t>(obj, 0x460) = v;
        Field<uint32_t>(obj, 0x460) = v | 2u;
        return (v | 2u) & 0xffffff00u;
    }
    return id & 0xffffff00u;
}
static uint32_t AnimPlayer_OnMessage_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return AnimPlayer_OnMessage(P(ecx), a0, a1);
}
ABI_ENTRY_0040a7d0(AnimPlayer_OnMessage_Entry, AnimPlayer_OnMessage_Abi)
HOOK(0x0040a7d0, "AnimPlayer_OnMessage", AnimPlayer_OnMessage_Entry, nullptr);

// 0x0040a890 (ecx = object; stack: a0..a3; ret 0x10; ecx pushed as a local slot). BackgroundObject (vtable
// 0x005ac380, "CLASS(BackgroundObject)") slot 6, the load slot: the base load 0x004517b0 (same arguments);
// "sorting_order" (0x005bb9c8, default 10) -> +0x460 and "fog" (0x005bb9c4, default 0) -> the a3 argument slot
// (0x00453400 eax = default, esi = a3, edi = target; object, name, a2, 0). Then for each mesh i < (a0+0x10)+0x28 *
// +0x24 (unsigned; the counter lives in the a2 slot) of ((object +0x414)+0x10)+0x38, for each part j < mesh +0x14
// (signed; the counter lives in the a3 slot) of mesh +0x18, for each k < c+0x80 (c = part+4): the material flags
// ((c+0x84)[k] (0x2c bytes): +0x10 * 0x64 + (+4)+0x7c)+8 +0x18 get bit 0x10 set when fog, else cleared. Original
// bug: the fog value is re-read from the a3 slot at each mesh, which by then holds the previous mesh's part counter,
// so from the second mesh on the flag follows "previous mesh had parts" instead of fog (reproduced). Then
// 0x00488120 (eax = object, ecx = g_game +0xa78) and object vt+0xf4(0).
uint32_t BackgroundObject_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    orig::fn_00453400(10, a3, u(obj) + 0x460, u(obj), 0x005bb9c8, a2, 0);
    orig::fn_00453400(0, a3, u(&a3), u(obj), 0x005bb9c4, a2, 0);
    a2 = 0;
    void* info = Field<void*>(P(a0), 0x10);
    if (Field<uint32_t>(info, 0x28) * Field<uint32_t>(info, 0x24) != 0)
    {
        do
        {
            const uint32_t fog = a3;
            void* mesh = Field<void**>(Field<void*>(Field<void*>(obj, 0x414), 0x10), 0x38)[a2];
            a1 = u(mesh);
            a3 = 0;
            if (Field<int32_t>(mesh, 0x14) > 0)
            {
                uint32_t* part = &Field<uint32_t>(mesh, 0x18);
                do
                {
                    void* c = Field<void*>(P(*part), 4);
                    if (Field<int32_t>(c, 0x80) > 0)
                    {
                        uint32_t off = 0;
                        int32_t k = 0;
                        do
                        {
                            uint8_t* entries = Field<uint8_t*>(c, 0x84);
                            const uint32_t sub = *reinterpret_cast<uint32_t*>(entries + off + 4);
                            const uint32_t idx = *reinterpret_cast<uint32_t*>(entries + off + 0x10);
                            const uint32_t m = Field<uint32_t>(P(idx * 0x64 + Field<uint32_t>(P(sub), 0x7c)), 8);
                            uint32_t flags = Field<uint32_t>(P(m), 0x18);
                            if (fog)
                                flags |= 0x10;
                            else
                                flags &= ~0x10u;
                            Field<uint32_t>(P(m), 0x18) = flags;
                            ++k;
                            off += 0x2c;
                        } while (k < Field<int32_t>(c, 0x80));
                    }
                    ++a3;
                    ++part;
                } while (static_cast<int32_t>(a3) < Field<int32_t>(P(a1), 0x14));
            }
            ++a2;
            info = Field<void*>(P(a0), 0x10);
        } while (a2 < Field<uint32_t>(info, 0x28) * Field<uint32_t>(info, 0x24));
    }
    orig::fn_00488120(u(obj), Field<uint32_t>(GGame(), 0xa78));
    return VCall<uint32_t>(obj, 0xf4 / 4, 0u);
}
static uint32_t BackgroundObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return BackgroundObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0040a890(BackgroundObject_Load_Entry, BackgroundObject_Load_Abi)
HOOK(0x0040a890, "BackgroundObject_Load", BackgroundObject_Load_Entry, nullptr);

// 0x0040a9e0 (stack: target; ret 4). BackgroundObject (vtable 0x005ac380) slot 7: target bytes +0xd, +0xe and
// +0x7a = 1. Returns the target. Not executed in recorded runs: not verifiable by compare yet.
uint32_t BackgroundObject_Slot7(void* target)
{
    Field<uint8_t>(target, 0xd) = 1;
    Field<uint8_t>(target, 0xe) = 1;
    Field<uint8_t>(target, 0x7a) = 1;
    return u(target);
}
static uint32_t BackgroundObject_Slot7_Abi(uint32_t a0)
{
    return BackgroundObject_Slot7(P(a0));
}
ABI_ENTRY_0040a9e0(BackgroundObject_Slot7_Entry, BackgroundObject_Slot7_Abi)
HOOK(0x0040a9e0, "BackgroundObject_Slot7", BackgroundObject_Slot7_Entry, nullptr);

// 0x0040cb10 (ecx = object; frame 0x48). BarricadeObject (vtable 0x005a7068) and BuildingObject (vtable 0x005aeeb8)
// slot 0x2f (+0xbc, update). Flags +0x460: bit 1: timer +0x464 counts down by the frame time [0x00604a90]; at or
// below 0 the object removes itself (vt+0xdc). Else bit 3 (spawning): with bits 0x14, the delay +0x574 counts down;
// at or below 0: done = bit 2 ? 0x0040c740 (eax = object, ecx = flags) : 0x0040c710 (eax = object); done clears bit
// 3, otherwise +0x574 = 0x0046e2c0(+0x578, +0x57c) (a random value in the range; ecx is only pushed there). Without
// bits 0x14, and when g_game +0xc80 <= 0, byte +0xcbc == 0, +0xadc == 0 and +0xae0 == 0.0: n = (+0x5a0) vt+0x34();
// when +0x59c == 0 and n >= +0x594 (unsigned) the delay is re-rolled (0x0046e2c0), else it counts down; when +0x59c
// == 0 and n < +0x594: +0x59c = +0x598; at or below 0: 0x0040c910(object). Then, when bit 6 and +0x424 bit 8:
// registered in (g_game +0xa84)+0xf04 (0x004546d0), the node "Ref_bullseye" (0x005bb2c4) is looked up into a local
// matrix (0x00453c50(object, name, &m, 1, 0, 0, 1, 0, 0)) and, when found, the decal "Fx_bullseye" is spawned
// (Decals_Spawn 0x004165c0 eax = g_game +0xa7c; &m, +0x468, "Fx_bullseye" 0x005bb2b8, object, 0): on success bit 7
// is set and the decal's +0xf5c = -1.0; bit 6 is cleared. Not executed in recorded runs: not verifiable by compare
// yet.
uint32_t fn_0040cb10(void* obj)
{
    uint32_t frame[0x48 / 4];
    const float zero = GameGlobal<float>(0x0056c05c);
    const float ft = GameGlobal<float>(0x00604a90);
    const uint32_t flags = Field<uint32_t>(obj, 0x460);
    if (flags & 2)
    {
        const float t = Field<float>(obj, 0x464) - ft;
        Field<float>(obj, 0x464) = t;
        if (!(t > zero) && !(t != t))
            VCall<void>(obj, 0xdc / 4);
    }
    else if (flags & 8)
    {
        if (flags & 0x14)
        {
            const float t = Field<float>(obj, 0x574) - ft;
            Field<float>(obj, 0x574) = t;
            if (t <= zero)
            {
                const uint32_t done = (flags & 4) ? orig::fn_0040c740(u(obj), flags) : orig::fn_0040c710(u(obj));
                if (static_cast<uint8_t>(done))
                    Field<uint32_t>(obj, 0x460) &= ~8u;
                else
                    Field<float>(obj, 0x574) =
                        orig::fn_0046e2c0(0, Field<uint32_t>(obj, 0x578), Field<uint32_t>(obj, 0x57c));
            }
        }
        else
        {
            void* g = GGame();
            if (Field<float>(g, 0xc80) <= zero && !Field<uint8_t>(g, 0xcbc) && Field<uint32_t>(g, 0xadc) == 0 &&
                Field<float>(g, 0xae0) == zero)
            {
                frame[0] = f2u(ft);
                const uint32_t n = VCall<uint32_t>(Field<void*>(obj, 0x5a0), 0x34 / 4);
                float t;
                if (Field<uint32_t>(obj, 0x59c) == 0 && n >= Field<uint32_t>(obj, 0x594))
                    t = orig::fn_0046e2c0(0, Field<uint32_t>(obj, 0x578), Field<uint32_t>(obj, 0x57c));
                else
                    t = Field<float>(obj, 0x574) - u2f(frame[0]);
                Field<float>(obj, 0x574) = t;
                if (Field<uint32_t>(obj, 0x59c) == 0 && n < Field<uint32_t>(obj, 0x594))
                    Field<uint32_t>(obj, 0x59c) = Field<uint32_t>(obj, 0x598);
                if (Field<float>(obj, 0x574) <= zero)
                    orig::fn_0040c910(u(obj));
            }
        }
    }
    if (!(Field<uint8_t>(obj, 0x460) & 0x40) || !(Field<uint32_t>(obj, 0x424) & 0x100))
        return 0;  // original: eax as left
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xf04), u(obj));
    uint32_t* m = &frame[1];
    if (static_cast<uint8_t>(orig::fn_00453c50(u(obj), 0x005bb2c4, u(m), 1, 0, 0, 1, 0, 0)))
    {
        const uint32_t decal = orig::fn_004165c0(Field<uint32_t>(GGame(), 0xa7c), u(m), Field<uint32_t>(obj, 0x468),
                                                 0x005bb2b8, u(obj), 0);
        if (decal)
        {
            Field<uint32_t>(obj, 0x460) |= 0x80;
            Field<uint32_t>(P(decal), 0xf5c) = 0xbf800000u;
        }
    }
    Field<uint32_t>(obj, 0x460) &= ~0x40u;
    return 0;  // original: eax as left
}
static uint32_t fn_0040cb10_Abi(uint32_t ecx)
{
    return fn_0040cb10(P(ecx));
}
ABI_ENTRY_0040cb10(fn_0040cb10_Entry, fn_0040cb10_Abi)
HOOK(0x0040cb10, "fn_0040cb10", fn_0040cb10_Entry, nullptr);

namespace
{
uint32_t Rand()
{
    return static_cast<uint32_t>(orig::fn_005391cd());
}
// Speech cue names chosen by 0x0040d390 (100 per bucket of the scaled random value; the last one takes the rest).
const uint32_t kManCueNew[12] = {0x005bb180, 0x005bb168, 0x005bb150, 0x005bb138, 0x005bb120, 0x005bb108,
                                 0x005bb0f0, 0x005bb0d8, 0x005bb0c0, 0x005bb0a8, 0x005bb090, 0x005bb078};
const uint32_t kManCueSoon[10] = {0x005bb060, 0x005bb048, 0x005bb030, 0x005bb018, 0x005bb000,
                                  0x005bafe8, 0x005bafd0, 0x005bafb8, 0x005bafa0, 0x005baf88};
const uint32_t kWomanCueNew[9] = {0x005baf70, 0x005baf58, 0x005baf40, 0x005baf28, 0x005baf10,
                                  0x005baef8, 0x005baee0, 0x005baec8, 0x005baeb0};
const uint32_t kWomanCueSoon[9] = {0x005bae98, 0x005bae80, 0x005bae68, 0x005bae50, 0x005bae38,
                                   0x005bae20, 0x005bae08, 0x005badf0, 0x005badd8};
uint32_t PickCue(const uint32_t* table, uint32_t count, uint32_t v)
{
    uint32_t i = v / 100;
    return table[i < count - 1 ? i : count - 1];
}
}  // namespace

// 0x0040d390 (cdecl; stack: object, a1, peasants; caller 0x0040cf40; frame 0xa0). Spawns 1 to 3 characters
// ((rand() * 3 >> 15) + 1) at the object: position from object vt+0x54(&pos), a matrix rotated by a random angle
// (rand() * 2pi/32768 - pi; cos/-sin/sin/cos) translated to pos + (0, 0, 5) (flags +0x40 = 1). With peasants (byte):
// "peasant_man" (rand() * 100 >> 15 < 50) or "peasant_woman" via ObjectSystem_CreateObject 0x0045f020 (eax =
// "peasantgenai", ecx = name; g_game +0xa84, &m, 0x3ff, 0, 0, 0, 0, 0xaa, 0), a "prop_peasant_chair" (eax = 0, no
// matrix) attached at "center" (0x004539f0; stored at +0xb58, referenced, chair vt+0xf8(0x20000, 0)), 0x00431020
// (eax = g_game +0x748; peasant, rand() * 2^-16 + 2.5, 75.0, 0), and a scream (sound object (g_game +0x2c)
// vt+0xc4(cue, peasant, 0, 0, -1, 0) -> peasant +0xb4c): men use g_game +0xba4 (last), +0xba8/+0xbac (interval
// range), +0xbb0 (interval); when the interval has passed since the last scream, a new interval is rolled and a cue
// from 12 (rand() * 1200 >> 15), else from 10 others (rand() * 1000 >> 15); both then queue 0x00493b70 (ebx = 15,
// edi = 0x005fb6d0; 0x0063a1e0, (g_game +0x748)+0xe60, g_game +0x748, 1, 0, 0x0040d250). Women use +0xb44..+0xb50
// with 9 + 9 cues (rand() * 900 >> 15). Without peasants: "captain" (when rand() * 100 >> 15 < 50 and 0x0045abe0
// (ecx = "captain", esi = g_game +0xa84) finds one) or "grunt_regular" with the same matrix, 0x0042d0f0 (eax =
// soldier; 0, 1), and a message block (0x34 bytes): +0x10 = 0x00458720 (eax = soldier, edx = 0), +0x18 = 0x00453150
// vector (ecx = soldier) with +0x20 = rand() * 6.1e-6 + 0.7 normalised, +0x24 = rand() * 0.661 + 21666.7, +0 |=
// 0x8003, +0x2c = object, +0x14 = 0x004555b0 (eax = soldier), sent with soldier vt+0xd0(&block). Every spawn ends
// with 0x004324d0 (eax = 1, ebx = spawned; a1, 1) or (0, 0) when a1 is 0. Returns the count. (ecx is only pushed as
// a stack slot by 0x0042d0f0, 0x00493b70; passed as 0.)
uint32_t fn_0040d390(void* obj, uint32_t a1, uint32_t peasants)
{
    uint32_t F[0xb0 / 4];  // F[X / 4] = the original [esp + X] after the four register pushes
    const uint32_t count = ((Rand() * 3) >> 15) + 1;
    F[0x24 / 4] = count;  // the original's [esp + 0x14] before the pushes; returned at the end
    uint32_t left = count;
    void* g;
    do
    {
        std::memset(&F[0x2c / 4], 0, 0xd * 4);
        VCall<void>(obj, 0x54 / 4, u(&F[0x60 / 4]));
        F[0x10 / 4] = Rand();
        std::memset(&F[0x6c / 4], 0, 0x11 * 4);
        const float angle = static_cast<float>(static_cast<int32_t>(F[0x10 / 4])) * GameGlobal<float>(0x005bcdbc) -
                            GameGlobal<float>(0x005bcb70);
        F[0x88 / 4] = F[0x64 / 4];
        F[0x10 / 4] = f2u(angle);
        F[0x94 / 4] = 0x3f800000;
        F[0xa8 / 4] = 0x3f800000;
        const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
        F[0xac / 4] = 1;
        F[0x78 / 4] = F[0x60 / 4];
        const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
        F[0x6c / 4] = f2u(c);
        F[0x70 / 4] = f2u(-s);
        F[0x7c / 4] = f2u(s);
        F[0x80 / 4] = f2u(c);
        F[0x98 / 4] = f2u(u2f(F[0x68 / 4]) + GameGlobal<float>(0x005bcbdc));
        uint32_t spawned;
        if (static_cast<uint8_t>(peasants))
        {
            const uint32_t pick = (Rand() * 100) >> 15;
            g = GGame();
            const uint32_t peasant = orig::fn_0045f020(0x005bb1bc, pick < 0x32 ? 0x005bbe78 : 0x005bb1ac,
                                                       Field<uint32_t>(g, 0xa84), u(&F[0x6c / 4]), 0x3ff, 0, 0, 0, 0,
                                                       0xaa, 0);
            const uint32_t chair = orig::fn_0045f020(0, 0x005bb198, Field<uint32_t>(GGame(), 0xa84), 0, 0x3ff, 0, 0, 0,
                                                     0, 0xaa, 0);
            Field<uint32_t>(P(peasant), 0xb58) = chair;
            orig::fn_004539f0(0x005a009c, peasant, chair, 0x005a009c, 1, 0);
            ++Field<int32_t>(Field<void*>(P(peasant), 0xb58), 0x28);
            VCall<void>(Field<void*>(P(peasant), 0xb58), 0xf8 / 4, 0x20000u, 0u);
            spawned = peasant;
            F[0x10 / 4] = Rand();
            const float delay =
                static_cast<float>(static_cast<int32_t>(F[0x10 / 4])) * GameGlobal<float>(0x005bce3c) +
                GameGlobal<float>(0x005bcda4);
            orig::fn_00431020(Field<uint32_t>(GGame(), 0x748), peasant, f2u(delay), 0x42960000u, 0);
            const float now = GameGlobal<float>(0x00604a98);
            g = GGame();
            uint32_t cue;
            if (pick < 0x32)
            {
                if (now - Field<float>(g, 0xba4) < Field<float>(g, 0xbb0))
                    cue = PickCue(kManCueSoon, 10, (Rand() * 1000) >> 15);
                else
                {
                    Field<float>(g, 0xba4) = now;
                    F[0x20 / 4] = Field<uint32_t>(g, 0xbac);
                    F[0x14 / 4] = Field<uint32_t>(g, 0xba8);
                    F[0x10 / 4] = Rand();
                    Field<float>(GGame(), 0xbb0) = static_cast<float>(static_cast<int32_t>(F[0x10 / 4])) *
                                                       (u2f(F[0x20 / 4]) - u2f(F[0x14 / 4])) *
                                                       GameGlobal<float>(0x005bcbc0) +
                                                   u2f(F[0x14 / 4]);
                    cue = PickCue(kManCueNew, 12, (Rand() * 0x4b0) >> 15);
                }
                Field<uint32_t>(P(peasant), 0xb4c) =
                    VCall<uint32_t>(Field<void*>(GGame(), 0x2c), 0xc4 / 4, cue, peasant, 0u, 0u, 0xffffffffu, 0u);
                const uint32_t talker = Field<uint32_t>(GGame(), 0x748);
                orig::fn_00493b70(0, 0xf, 0x005fb6d0, 0x0063a1e0, talker + 0xe60, talker, 1, 0, 0x0040d250);
            }
            else
            {
                if (now - Field<float>(g, 0xb44) < Field<float>(g, 0xb50))
                    cue = PickCue(kWomanCueSoon, 9, (Rand() * 0x384) >> 15);
                else
                {
                    Field<float>(g, 0xb44) = now;
                    F[0x28 / 4] = Field<uint32_t>(g, 0xb4c);
                    F[0x18 / 4] = Field<uint32_t>(g, 0xb48);
                    F[0x10 / 4] = Rand();
                    Field<float>(GGame(), 0xb50) = static_cast<float>(static_cast<int32_t>(F[0x10 / 4])) *
                                                       (u2f(F[0x28 / 4]) - u2f(F[0x18 / 4])) *
                                                       GameGlobal<float>(0x005bcbc0) +
                                                   u2f(F[0x18 / 4]);
                    cue = PickCue(kWomanCueNew, 9, (Rand() * 0x384) >> 15);
                }
                Field<uint32_t>(P(peasant), 0xb4c) =
                    VCall<uint32_t>(Field<void*>(GGame(), 0x2c), 0xc4 / 4, cue, peasant, 0u, 0u, 0xffffffffu, 0u);
            }
        }
        else
        {
            uint32_t name = 0x005bbfe8;  // "grunt_regular"
            if (((Rand() * 100) >> 15) < 0x32 && orig::fn_0045abe0(0x005bbff8, Field<uint32_t>(GGame(), 0xa84)) != 0)
                name = 0x005bbff8;  // "captain"
            const uint32_t soldier = orig::fn_0045f020(0, name, Field<uint32_t>(GGame(), 0xa84), u(&F[0x6c / 4]),
                                                       0x3ff, 0, 0, 0, 0, 0xaa, 0);
            orig::fn_0042d0f0(soldier, 0, 0, 1);
            std::memset(&F[0x2c / 4], 0, 0xd * 4);
            F[0x3c / 4] = orig::fn_00458720(soldier, 0);
            orig::fn_00453150(soldier, u(&F[0x44 / 4]));
            F[0x10 / 4] = Rand();
            F[0x4c / 4] = f2u(static_cast<float>(static_cast<int32_t>(F[0x10 / 4])) * GameGlobal<float>(0x005bcdf4) +
                              GameGlobal<float>(0x005bcd48));
            F[0x10 / 4] = Rand();
            F[0x50 / 4] = f2u(static_cast<float>(static_cast<int32_t>(F[0x10 / 4])) * GameGlobal<float>(0x005bcdf0) +
                              GameGlobal<float>(0x005bcdec));
            const float x = u2f(F[0x44 / 4]);
            const float y = u2f(F[0x48 / 4]);
            const float z = u2f(F[0x4c / 4]);
            const float len = std::sqrt(y * y + x * x + z * z);
            if (!(len == GameGlobal<float>(0x0056c05c)))
            {
                const float inv = GameGlobal<float>(0x005bcb2c) / len;
                F[0x44 / 4] = f2u(x * inv);
                F[0x48 / 4] = f2u(y * inv);
                F[0x4c / 4] = f2u(inv * z);
            }
            F[0x58 / 4] = u(obj);
            F[0x2c / 4] |= 0x8003;
            F[0x40 / 4] = f2u(orig::fn_004555b0(soldier));
            VCall<void>(P(soldier), 0xd0 / 4, u(&F[0x2c / 4]));
            spawned = soldier;
        }
        if (a1)
            orig::fn_004324d0(1, spawned, a1, 1);
        else
            orig::fn_004324d0(1, spawned, 0, 0);
    } while (--left != 0);
    return F[0x24 / 4];
}
ABI_ENTRY_0040d390(fn_0040d390_Entry, fn_0040d390)
HOOK(0x0040d390, "fn_0040d390", fn_0040d390_Entry, nullptr);

// 0x0040df90 (ecx = object; 6 stack arguments passed through). JumpMarker (vtable 0x005ac628) and BuildingObject
// (vtable 0x005aeeb8) slot 0x54: a tail jump to the object's own slot 0x53 (+0x14c) with the frame intact. Slot 0x53
// is 0x0040de50 / 0x0040c280 in those vtables, both (ecx; 6 arguments; ret 0x18), so the stub below pops 0x18
// (symbols/abi.csv: "no ret found", no generated stub). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_0040df90(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return VCall<uint32_t>(obj, 0x14c / 4, a0, a1, a2, a3, a4, a5);
}
static __declspec(naked) void fn_0040df90_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 48]
        push dword ptr [esp + 48]
        push dword ptr [esp + 48]
        push dword ptr [esp + 48]
        push dword ptr [esp + 48]
        push dword ptr [esp + 48]
        push ecx
        call fn_0040df90
        add esp, 28
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret 24
    }
}
HOOK(0x0040df90, "fn_0040df90", fn_0040df90_Entry, nullptr);

// 0x0040dfa0 (ecx = object; stack: target, a1; ret 8). JumpMarker (vtable 0x005ac628) slot 0x56: references the
// target (+0x28 += 1) and stores it at +0x468. Returns the target. Not executed in recorded runs: not verifiable by
// compare yet.
uint32_t fn_0040dfa0(void* obj, uint32_t target, uint32_t /*a1*/)
{
    ++Field<int32_t>(P(target), 0x28);
    Field<uint32_t>(obj, 0x468) = target;
    return target;
}
static uint32_t fn_0040dfa0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0040dfa0(P(ecx), a0, a1);
}
ABI_ENTRY_0040dfa0(fn_0040dfa0_Entry, fn_0040dfa0_Abi)
HOOK(0x0040dfa0, "fn_0040dfa0", fn_0040dfa0_Entry, nullptr);

// 0x0040dfb0 (ecx = object; stack: target, a1; ret 8). JumpMarker (vtable 0x005ac628) slot 0x57: when object
// vt+0xd8() is true and target is the stored +0x468, the reference is released (+0x28; at 0 and when its vt+0xd8()
// is false, 0x00487da0) and +0x468 = 0. Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_0040dfb0(void* obj, uint32_t target, uint32_t /*a1*/)
{
    const uint32_t r = VCall<uint32_t>(obj, 0xd8 / 4);
    if (!static_cast<uint8_t>(r) || !target)
        return r;
    if (target != Field<uint32_t>(obj, 0x468))
        return target;
    ReleaseRef(target);
    Field<uint32_t>(obj, 0x468) = 0;
    return 0;  // original: eax as left
}
static uint32_t fn_0040dfb0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0040dfb0(P(ecx), a0, a1);
}
ABI_ENTRY_0040dfb0(fn_0040dfb0_Entry, fn_0040dfb0_Abi)
HOOK(0x0040dfb0, "fn_0040dfb0", fn_0040dfb0_Entry, nullptr);

// 0x0040e000 (esi = block; caller Game_Init for g_game +0xa68, size 0x1a4). CameraSystem constructor: vtable
// 0x005bad48 (CLASSV(CameraSystem), see CameraSystem_GetClass 0x004103a0), +4 = 0, +0x158 = 0, then 0x0040e050(block).
// Returns the block.
uint32_t CameraSystem_Construct(void* cs)
{
    Field<uint32_t>(cs, 0) = 0x005bad48;
    Field<uint32_t>(cs, 4) = 0;
    Field<uint32_t>(cs, 0x158) = 0;
    orig::fn_0040e050(u(cs));
    return u(cs);
}
uint32_t fn_0040e000(void* obj)
{
    return CameraSystem_Construct(obj);
}
static uint32_t CameraSystem_Construct_Abi(uint32_t esi)
{
    return CameraSystem_Construct(P(esi));
}
ABI_ENTRY_0040e000(CameraSystem_Construct_Entry, CameraSystem_Construct_Abi)
HOOK(0x0040e000, "CameraSystem_Construct", CameraSystem_Construct_Entry, nullptr);

// 0x0040e020 (esi = camera system; caller the Game destructor 0x00401710). CameraSystem destructor body: vtable
// 0x005bad48; the object +0x158 is deleted (vt+0x10(1)) and cleared. Not executed in recorded runs: not verifiable
// by compare yet.
uint32_t CameraSystem_Destruct(void* cs)
{
    void* p = Field<void*>(cs, 0x158);
    Field<uint32_t>(cs, 0) = 0x005bad48;
    if (!p)
        return 0;  // original: eax as left
    const uint32_t r = VCall<uint32_t>(p, 0x10 / 4, 1u);
    Field<uint32_t>(cs, 0x158) = 0;
    return r;
}
uint32_t fn_0040e020(void* obj)
{
    return CameraSystem_Destruct(obj);
}
static uint32_t CameraSystem_Destruct_Abi(uint32_t esi)
{
    return CameraSystem_Destruct(P(esi));
}
ABI_ENTRY_0040e020(CameraSystem_Destruct_Entry, CameraSystem_Destruct_Abi)
HOOK(0x0040e020, "CameraSystem_Destruct", CameraSystem_Destruct_Entry, nullptr);

uint32_t fn_0040f8c0(void* cam, float v);

// 0x0040e050 (stack: camera; ret 4; callers CameraSystem_Construct 0x0040e000, 0x0044a670, 0x0045d670). Camera
// reset: +0xd8 = +0xf8 = 1.0 (near), +0xf0 = +0xf4 = 1.0472 (fov, 60 degrees), +0x70/+0x74/+0x84/+0xdc/+0xe4/+0xe8/
// +0xec = 0, +0xd4 = +0xe0 = 100000.0 (far), +0xd0 = -1.0; 0x0040f8c0 (edx = camera; g_game +0x4b0) (the far scale
// option); +0x154 = 1.7021, +0x88 = +0x150 = +0x14c = 0; the target +4 is released and cleared; the list +0x158 is
// emptied (each element released, then list vt+0x28(element); iterator 0x004e7340 / 0x004e73c0) or created (0x18
// bytes, 0x004e7010 eax = g_game +0xac4, edx = 0); +0x78 = 0, byte +0x48 = 0, +0x6c = +0x68 = 0, byte +0x15c = 0,
// +0x168 = +0x178 = +0x17c = +0x180 = 0, +0x7c = 2.0, +0x80 = 1.0.
uint32_t Camera_Reset(void* cam)
{
    Field<uint32_t>(cam, 0xd8) = 0x3f800000;
    Field<uint32_t>(cam, 0xf8) = 0x3f800000;
    Field<uint32_t>(cam, 0xf0) = 0x3f860a92;
    Field<uint32_t>(cam, 0xf4) = 0x3f860a92;
    Field<uint32_t>(cam, 0x70) = 0;
    Field<uint32_t>(cam, 0x74) = 0;
    Field<uint32_t>(cam, 0x84) = 0;
    Field<uint32_t>(cam, 0xd4) = 0x47c35000;
    Field<uint32_t>(cam, 0xdc) = 0;
    Field<uint32_t>(cam, 0xe0) = 0x47c35000;
    Field<uint32_t>(cam, 0xec) = 0;
    Field<uint32_t>(cam, 0xe8) = 0;
    Field<uint32_t>(cam, 0xe4) = 0;
    Field<uint32_t>(cam, 0xd0) = 0xbf800000u;
    fn_0040f8c0(cam, Field<float>(GGame(), 0x4b0));
    const uint32_t target = Field<uint32_t>(cam, 4);
    Field<uint32_t>(cam, 0x154) = 0x3fd9df52;
    Field<uint32_t>(cam, 0x88) = 0;
    Field<uint32_t>(cam, 0x150) = 0;
    Field<uint32_t>(cam, 0x14c) = 0;
    if (target)
    {
        ReleaseRef(target);
        Field<uint32_t>(cam, 4) = 0;
    }
    if (const uint32_t list = Field<uint32_t>(cam, 0x158))
    {
        uint32_t iterator[2] = {0, 0};
        for (uint32_t e = orig::fn_004e7340(list, u(iterator)); e; e = orig::fn_004e73c0(u(iterator)))
        {
            ReleaseRef(e);
            VCall<void>(Field<void*>(cam, 0x158), 0x28 / 4, e);
        }
        if (iterator[1])
            --Field<int32_t>(P(iterator[1]), 0x18);
    }
    else
    {
        const uint32_t block = HeapAlloc(0x18);
        Field<uint32_t>(cam, 0x158) = block ? orig::fn_004e7010(Field<uint32_t>(GGame(), 0xac4), 0, block) : 0;
    }
    Field<uint32_t>(cam, 0x78) = 0;
    Field<uint8_t>(cam, 0x48) = 0;
    Field<uint32_t>(cam, 0x6c) = 0;
    Field<uint32_t>(cam, 0x68) = 0;
    Field<uint8_t>(cam, 0x15c) = 0;
    Field<uint32_t>(cam, 0x168) = 0;
    Field<uint32_t>(cam, 0x178) = 0;
    Field<uint32_t>(cam, 0x17c) = 0;
    Field<uint32_t>(cam, 0x180) = 0;
    Field<uint32_t>(cam, 0x7c) = 0x40000000;
    Field<uint32_t>(cam, 0x80) = 0x3f800000;
    return 0;  // original: eax as left
}
static uint32_t Camera_Reset_Abi(uint32_t a0)
{
    return Camera_Reset(P(a0));
}
ABI_ENTRY_0040e050(Camera_Reset_Entry, Camera_Reset_Abi)
HOOK(0x0040e050, "Camera_Reset", Camera_Reset_Entry, nullptr);

// 0x0040e200 (eax = camera, ebx = first name or null; stack: target, second name or null; ret 8). Sets the camera
// target: when it differs from +4 the old one is released (when set) and the new one referenced (+0x28 += 1, not
// null-checked) and stored. The names are copied (unbounded strcpy) to +8 and +0x28 (both emptied first). Returns
// the end of the second name's source (eax), else 0.
uint32_t fn_0040e200(void* cam, uint32_t name1, uint32_t target, uint32_t name2)
{
    const uint32_t old = Field<uint32_t>(cam, 4);
    if (old != target)
    {
        if (old)
            ReleaseRef(old);
        ++Field<int32_t>(P(target), 0x28);
        Field<uint32_t>(cam, 4) = target;
    }
    char* d1 = &Field<char>(cam, 8);
    char* d2 = &Field<char>(cam, 0x28);
    *d1 = 0;
    *d2 = 0;
    if (name1)
    {
        const char* s = reinterpret_cast<const char*>(name1);
        while ((*d1++ = *s++) != 0)
        {
        }
    }
    if (!name2)
        return 0;
    const char* s = reinterpret_cast<const char*>(name2);
    while ((*d2++ = *s++) != 0)
    {
    }
    return u(s);
}
static uint32_t fn_0040e200_Abi(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1)
{
    return fn_0040e200(P(eax), ebx, a0, a1);
}
ABI_ENTRY_0040e200(fn_0040e200_Entry, fn_0040e200_Abi)
HOOK(0x0040e200, "fn_0040e200", fn_0040e200_Entry, nullptr);

// 0x0040e270 (ecx = camera, edx = object). When the camera target +4 is the object and the counter +0x168 is set,
// it counts down; at 0 the game time scale is reset (0x00403aa0 ecx = g_game; 1.0, 0). Not executed in recorded
// runs: not verifiable by compare yet.
uint32_t fn_0040e270(void* cam, uint32_t obj)
{
    const uint32_t t = Field<uint32_t>(cam, 4);
    if (t != obj)
        return t;
    uint32_t n = Field<uint32_t>(cam, 0x168);
    if (!n)
        return 0;
    --n;
    Field<uint32_t>(cam, 0x168) = n;
    if (n)
        return n;
    return fn_00403aa0(GGame(), 0x3f800000u, 0);
}
static uint32_t fn_0040e270_Abi(uint32_t ecx, uint32_t edx)
{
    return fn_0040e270(P(ecx), edx);
}
ABI_ENTRY_0040e270(fn_0040e270_Entry, fn_0040e270_Abi)
HOOK(0x0040e270, "fn_0040e270", fn_0040e270_Entry, nullptr);

// 0x0040e2a0 (ecx = camera; stack: v; ret 4). +0x78 = v.
uint32_t fn_0040e2a0(void* cam, uint32_t v)
{
    Field<uint32_t>(cam, 0x78) = v;
    return v;
}
static uint32_t fn_0040e2a0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0040e2a0(P(ecx), a0);
}
ABI_ENTRY_0040e2a0(fn_0040e2a0_Entry, fn_0040e2a0_Abi)
HOOK(0x0040e2a0, "fn_0040e2a0", fn_0040e2a0_Entry, nullptr);

// 0x0040e2b0 (eax = pair, ecx = camera). +0x7c = pair[0], +0x80 = pair[1].
uint32_t fn_0040e2b0(const uint32_t* pair, void* cam)
{
    Field<uint32_t>(cam, 0x7c) = pair[0];
    const uint32_t v = pair[1];
    Field<uint32_t>(cam, 0x80) = v;
    return v;
}
static uint32_t fn_0040e2b0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0040e2b0(static_cast<const uint32_t*>(P(eax)), P(ecx));
}
ABI_ENTRY_0040e2b0(fn_0040e2b0_Entry, fn_0040e2b0_Abi)
HOOK(0x0040e2b0, "fn_0040e2b0", fn_0040e2b0_Entry, nullptr);

// 0x0040e2c0 (eax = camera; returns st0). Returns +0x74. Not executed in recorded runs: not verifiable by compare
// yet.
float fn_0040e2c0(void* cam)
{
    return Field<float>(cam, 0x74);
}
static float fn_0040e2c0_Abi(uint32_t eax)
{
    return fn_0040e2c0(P(eax));
}
ABI_ENTRY_0040e2c0(fn_0040e2c0_Entry, fn_0040e2c0_Abi)
HOOK(0x0040e2c0, "fn_0040e2c0", fn_0040e2c0_Entry, nullptr);

// 0x0040e2d0 / 0x0040e2e0 / 0x0040e2f0 (ecx = camera; stack: v; ret 4). +0x74 / +0x70 / +0x84 = v.
uint32_t fn_0040e2d0(void* cam, uint32_t v)
{
    Field<uint32_t>(cam, 0x74) = v;
    return v;
}
static uint32_t fn_0040e2d0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0040e2d0(P(ecx), a0);
}
ABI_ENTRY_0040e2d0(fn_0040e2d0_Entry, fn_0040e2d0_Abi)
HOOK(0x0040e2d0, "fn_0040e2d0", fn_0040e2d0_Entry, nullptr);

uint32_t fn_0040e2e0(void* cam, uint32_t v)
{
    Field<uint32_t>(cam, 0x70) = v;
    return v;
}
static uint32_t fn_0040e2e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0040e2e0(P(ecx), a0);
}
ABI_ENTRY_0040e2e0(fn_0040e2e0_Entry, fn_0040e2e0_Abi)
HOOK(0x0040e2e0, "fn_0040e2e0", fn_0040e2e0_Entry, nullptr);

uint32_t fn_0040e2f0(void* cam, uint32_t v)
{
    Field<uint32_t>(cam, 0x84) = v;
    return v;
}
static uint32_t fn_0040e2f0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0040e2f0(P(ecx), a0);
}
ABI_ENTRY_0040e2f0(fn_0040e2f0_Entry, fn_0040e2f0_Abi)
HOOK(0x0040e2f0, "fn_0040e2f0", fn_0040e2f0_Entry, nullptr);

// 0x0040e300 (ecx = camera, edx = target vector). Camera shake (guess): while the timer +0x150 is positive, target
// +0x2c += amplitude +0x14c, the amplitude is multiplied by -0.75 ([0x005bcc20]) and the timer counts down by the
// frame time [0x00604a90].
uint32_t fn_0040e300(void* cam, void* target)
{
    if (!(Field<float>(cam, 0x150) > GameGlobal<float>(0x0056c05c)))
        return 0;  // original: the fnstsw word
    Field<float>(target, 0x2c) = Field<float>(target, 0x2c) + Field<float>(cam, 0x14c);
    Field<float>(cam, 0x14c) = Field<float>(cam, 0x14c) * GameGlobal<float>(0x005bcc20);
    Field<float>(cam, 0x150) = Field<float>(cam, 0x150) - GameGlobal<float>(0x00604a90);
    return 0;
}
static uint32_t fn_0040e300_Abi(uint32_t ecx, uint32_t edx)
{
    return fn_0040e300(P(ecx), P(edx));
}
ABI_ENTRY_0040e300(fn_0040e300_Entry, fn_0040e300_Abi)
HOOK(0x0040e300, "fn_0040e300", fn_0040e300_Entry, nullptr);

// 0x0040f840 (eax = camera; returns st0). The field of view +0xf4.
float Camera_GetFov(void* cam)
{
    return Field<float>(cam, 0xf4);
}
static float Camera_GetFov_Abi(uint32_t eax)
{
    return Camera_GetFov(P(eax));
}
ABI_ENTRY_0040f840(Camera_GetFov_Entry, Camera_GetFov_Abi)
HOOK(0x0040f840, "Camera_GetFov", Camera_GetFov_Entry, nullptr);

// 0x0040f850 (eax = camera; returns st0). The near plane +0xf8.
float Camera_GetNear(void* cam)
{
    return Field<float>(cam, 0xf8);
}
static float Camera_GetNear_Abi(uint32_t eax)
{
    return Camera_GetNear(P(eax));
}
ABI_ENTRY_0040f850(Camera_GetNear_Entry, Camera_GetNear_Abi)
HOOK(0x0040f850, "Camera_GetNear", Camera_GetNear_Entry, nullptr);

// 0x0040f860 (eax = camera, cl = unscaled; returns st0). The far plane +0xd4, times the far scale +0xd0 unless cl.
float Camera_GetFar(void* cam, uint32_t unscaled)
{
    if (static_cast<uint8_t>(unscaled))
        return Field<float>(cam, 0xd4);
    return Field<float>(cam, 0xd4) * Field<float>(cam, 0xd0);
}
static float Camera_GetFar_Abi(uint32_t eax, uint32_t ecx)
{
    return Camera_GetFar(P(eax), ecx);
}
ABI_ENTRY_0040f860(Camera_GetFar_Entry, Camera_GetFar_Abi)
HOOK(0x0040f860, "Camera_GetFar", Camera_GetFar_Entry, nullptr);

// 0x0040f880 (ecx = camera; stack: fov; ret 4). +0xf4 = fov.
uint32_t Camera_SetFov(void* cam, uint32_t v)
{
    Field<uint32_t>(cam, 0xf4) = v;
    return v;
}
static uint32_t Camera_SetFov_Abi(uint32_t ecx, uint32_t a0)
{
    return Camera_SetFov(P(ecx), a0);
}
ABI_ENTRY_0040f880(Camera_SetFov_Entry, Camera_SetFov_Abi)
HOOK(0x0040f880, "Camera_SetFov", Camera_SetFov_Entry, nullptr);

// 0x0040f890 (eax = camera; stack: near; ret 4). +0xd8 = +0xf8 = near.
void Camera_SetNear(void* cam, uint32_t v)
{
    Field<uint32_t>(cam, 0xd8) = v;
    Field<uint32_t>(cam, 0xf8) = v;
}
static void Camera_SetNear_Abi(uint32_t eax, uint32_t a0)
{
    Camera_SetNear(P(eax), a0);
}
ABI_ENTRY_0040f890(Camera_SetNear_Entry, Camera_SetNear_Abi)
HOOK(0x0040f890, "Camera_SetNear", Camera_SetNear_Entry, nullptr);

// 0x0040f8b0 (ecx = camera; stack: far; ret 4). +0xd4 = far (caller LevelProperties_Load 0x00423900).
uint32_t Camera_SetFar(void* cam, uint32_t v)
{
    Field<uint32_t>(cam, 0xd4) = v;
    return v;
}
static uint32_t Camera_SetFar_Abi(uint32_t ecx, uint32_t a0)
{
    return Camera_SetFar(P(ecx), a0);
}
ABI_ENTRY_0040f8b0(Camera_SetFar_Entry, Camera_SetFar_Abi)
HOOK(0x0040f8b0, "Camera_SetFar", Camera_SetFar_Entry, nullptr);

// 0x0040f8c0 (edx = camera; stack: scale; ret 4). Sets the far scale +0xd0: 1.0 when 0x0044e600() is true;
// otherwise the scale kept when 0.25 < scale < 1.0, 1.0 when scale >= 1.0 (or unordered after the first test), 0.25
// when scale <= 0.25 (or unordered). When it changes: +0xd0 = scale and renderer (g_game +0x38) vt+0x114(scale *
// +0xdc, scale * +0xe0, &+0xe4) (guess: fog range). Reads EDX from callers that loaded it for other reasons
// (docs/patterns.md).
float (*g_cameraFarScaleAdjust)(float scale) = nullptr;

uint32_t fn_0040f8c0(void* cam, float v)
{
    const float quarter = GameGlobal<float>(0x005bcbe4);
    if (static_cast<uint8_t>(orig::fn_0044e600()))
        v = 1.0f;
    else if (v > quarter && !(v < GameGlobal<float>(0x005bcb2c)))
        v = 1.0f;
    else if (!(v > quarter))
        v = 0.25f;
    if (g_cameraFarScaleAdjust)  // enhancement point (src/game/camera.h); null: the original
        v = g_cameraFarScaleAdjust(v);
    if (Field<float>(cam, 0xd0) == v)
        return 0;  // original: the fnstsw word
    const float b = v * Field<float>(cam, 0xe0);
    Field<float>(cam, 0xd0) = v;
    const float a = v * Field<float>(cam, 0xdc);
    return VCall<uint32_t>(Field<void*>(GGame(), 0x38), 0x114 / 4, f2u(a), f2u(b), u(cam) + 0xe4);
}
static uint32_t fn_0040f8c0_Abi(uint32_t edx, uint32_t a0)
{
    return fn_0040f8c0(P(edx), u2f(a0));
}
ABI_ENTRY_0040f8c0(fn_0040f8c0_Entry, fn_0040f8c0_Abi)
HOOK(0x0040f8c0, "fn_0040f8c0", fn_0040f8c0_Entry, nullptr);

// 0x0040f9e0 (esi = out; stack: one argument, not read; ret 4). Camera position: the inverse of the view matrix
// g_game +0x704 (Matrix_Inverse 0x004e2b40 eax = view, ecx = local 0x44 bytes), out = its +0xc, +0x1c, +0x2c.
uint32_t Camera_GetPosition(uint32_t* out, uint32_t /*a0*/)
{
    uint32_t m[17];
    orig::fn_004e2b40(u(GGame()) + 0x704, u(m));
    out[0] = m[3];
    out[1] = m[7];
    out[2] = m[11];
    return m[3];
}
static uint32_t Camera_GetPosition_Abi(uint32_t esi, uint32_t a0)
{
    return Camera_GetPosition(static_cast<uint32_t*>(P(esi)), a0);
}
ABI_ENTRY_0040f9e0(Camera_GetPosition_Entry, Camera_GetPosition_Abi)
HOOK(0x0040f9e0, "Camera_GetPosition", Camera_GetPosition_Entry, nullptr);

// 0x0040fa10 (esi = out; stack: one argument, not read; ret 4). Camera direction: out = -(+8, +0x18, +0x28) of the
// inverse view matrix (as 0x0040f9e0).
uint32_t Camera_GetDirection(float* out, uint32_t /*a0*/)
{
    float m[17];
    const uint32_t r = orig::fn_004e2b40(u(GGame()) + 0x704, u(m));
    out[0] = -m[2];
    out[1] = -m[6];
    out[2] = -m[10];
    return r;
}
static uint32_t Camera_GetDirection_Abi(uint32_t esi, uint32_t a0)
{
    return Camera_GetDirection(static_cast<float*>(P(esi)), a0);
}
ABI_ENTRY_0040fa10(Camera_GetDirection_Entry, Camera_GetDirection_Abi)
HOOK(0x0040fa10, "Camera_GetDirection", Camera_GetDirection_Entry, nullptr);

// 0x0040fa50 (eax = out; stack: one argument, not read; ret 4). Copies the view matrix g_game +0x704 (0x11 dwords).
void Camera_GetViewMatrix(void* out, uint32_t /*a0*/)
{
    std::memcpy(out, &Field<uint8_t>(GGame(), 0x704), 0x11 * 4);
}
static void Camera_GetViewMatrix_Abi(uint32_t eax, uint32_t a0)
{
    Camera_GetViewMatrix(P(eax), a0);
}
ABI_ENTRY_0040fa50(Camera_GetViewMatrix_Entry, Camera_GetViewMatrix_Abi)
HOOK(0x0040fa50, "Camera_GetViewMatrix", Camera_GetViewMatrix_Entry, nullptr);

// 0x0040fa70 (eax = camera, edx = out). Copies the camera's matrix +0x8c (0x11 dwords). Not executed in recorded
// runs: not verifiable by compare yet.
void fn_0040fa70(void* cam, void* out)
{
    std::memcpy(out, &Field<uint8_t>(cam, 0x8c), 0x11 * 4);
}
static void fn_0040fa70_Abi(uint32_t eax, uint32_t edx)
{
    fn_0040fa70(P(eax), P(edx));
}
ABI_ENTRY_0040fa70(fn_0040fa70_Entry, fn_0040fa70_Abi)
HOOK(0x0040fa70, "fn_0040fa70", fn_0040fa70_Entry, nullptr);

// 0x0040fa90 (esi = camera; stack: mode; ret 4). 0x0040fb50 (eax = camera), then +0x88 = mode. Not executed in
// recorded runs: not verifiable by compare yet.
uint32_t fn_0040fa90(void* cam, uint32_t mode)
{
    orig::fn_0040fb50(u(cam));
    Field<uint32_t>(cam, 0x88) = mode;
    return mode;
}
static uint32_t fn_0040fa90_Abi(uint32_t esi, uint32_t a0)
{
    return fn_0040fa90(P(esi), a0);
}
ABI_ENTRY_0040fa90(fn_0040fa90_Entry, fn_0040fa90_Abi)
HOOK(0x0040fa90, "fn_0040fa90", fn_0040fa90_Entry, nullptr);

// 0x0040fab0 (eax = camera). Returns +0x88 (the mode set by 0x0040fa90).
uint32_t fn_0040fab0(void* cam)
{
    return Field<uint32_t>(cam, 0x88);
}
static uint32_t fn_0040fab0_Abi(uint32_t eax)
{
    return fn_0040fab0(P(eax));
}
ABI_ENTRY_0040fab0(fn_0040fab0_Entry, fn_0040fab0_Abi)
HOOK(0x0040fab0, "fn_0040fab0", fn_0040fab0_Entry, nullptr);

// 0x0040fac0 (eax = camera; stack: amplitude, time; ret 8). Starts the shake read by 0x0040e300: +0x14c =
// amplitude, +0x150 = time.
void fn_0040fac0(void* cam, uint32_t amplitude, uint32_t time)
{
    Field<uint32_t>(cam, 0x150) = time;
    Field<uint32_t>(cam, 0x14c) = amplitude;
}
static void fn_0040fac0_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    fn_0040fac0(P(eax), a0, a1);
}
ABI_ENTRY_0040fac0(fn_0040fac0_Entry, fn_0040fac0_Abi)
HOOK(0x0040fac0, "fn_0040fac0", fn_0040fac0_Entry, nullptr);

// 0x0040fae0 (eax = name, esi = camera). Copies the name (unbounded strcpy) to +0x48 and sets +0x6c = 1. Returns
// the end of the source. Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_0040fae0(const char* name, void* cam)
{
    char* d = &Field<char>(cam, 0x48);
    while ((*d++ = *name++) != 0)
    {
    }
    Field<uint32_t>(cam, 0x6c) = 1;
    return u(name);
}
static uint32_t fn_0040fae0_Abi(uint32_t eax, uint32_t esi)
{
    return fn_0040fae0(static_cast<const char*>(P(eax)), P(esi));
}
ABI_ENTRY_0040fae0(fn_0040fae0_Entry, fn_0040fae0_Abi)
HOOK(0x0040fae0, "fn_0040fae0", fn_0040fae0_Entry, nullptr);

namespace
{
// Calls an original function that returns its result in st0 with edx (and ecx) loaded (0x004759e0, 0x00475990:
// edx = player; 0x004e7880: ecx = in, edx = out, the vector normalise returning the length). symbols/abi.csv lists an
// eax return for them; their callers in Camera_Update 0x0040e350 use st0.
float CallSt0_EcxEdx(uint32_t target, uint32_t ecx_, uint32_t edx_)
{
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
float PlayerAim_4759e0(uint32_t player)
{
    return CallSt0_EcxEdx(0x004759e0, 0, player);
}
float PlayerAim_475990(uint32_t player)
{
    return CallSt0_EcxEdx(0x00475990, 0, player);
}
float Vec3_Normalize(uint32_t in, uint32_t out)
{
    return CallSt0_EcxEdx(0x004e7880, in, out);
}
// Matrix helpers with the original register conventions.
void MatMul(uint32_t left, uint32_t out, uint32_t right)  // Matrix_Multiply 0x004e2910 (eax = left; out, right)
{
    orig::fn_004e2910(left, out, right);
}
void MatInverse(uint32_t src, uint32_t dst)  // Matrix_Inverse 0x004e2b40 (eax = src, ecx = dst)
{
    orig::fn_004e2b40(src, dst);
}
void MatRotX(uint32_t out, uint32_t angle)  // Matrix_RotationX 0x004e2e30 (edx = out; angle; caller pops)
{
    orig::fn_004e2e30(out, angle, 0, 0, 0, 0, 0, 0);
}
void MatRotZ(uint32_t out, uint32_t angle)  // Matrix_RotationZ 0x004e2e70 (edx = out; angle; caller pops)
{
    orig::fn_004e2e70(out, angle);
}
// Matrix_LookAt 0x004e2eb0 (eax = 0, ecx = out, edx = direction, edi = eye).
void MatLookAt(uint32_t out, uint32_t dir, uint32_t eye)
{
    orig::fn_004e2eb0(0, out, dir, eye);
}
}  // namespace

// 0x0040e350 (stack: camera, dt; ret 8; ebp frame aligned to 8, 0x29c bytes; caller 0x0045da90). Camera_Update.
// Unless g_game byte +0xdcb (free camera): with no target (+4) or g_game +0x2684 set it returns at once; target
// vt+0x3c(); when the target's vt+0xd8() is false or the counter +0x168 is set and 0x00414810 (al = 1, ebx = 9, esi =
// g_game +0xa80; 0.5) is false: 0x004757b0 (esi = g_game +0x748) and 0x0040e270. Then by mode +0x88:
// - 2 (or free camera): the camera matrix +0x8c is turned by the player input (0x00414a30 esi = g_game +0xa80, edi =
//   axis 4/5 and 6/7, scaled by 0x004759e0 * 10 / 0x00475990 * 2, times dt; Z and X rotations, the old translation
//   moved into the first), inverted into the view matrix g_game +0x704, then moved by the axes 3-2, 0x21-0x20, 1-0
//   (0x00414aa0, 90.0) times dt when any is not 0.
// - 3: look from the camera position at the target node (camera name +8 or "center" when target byte +0x421;
//   0x0040a4d0, or target vt+0x5c(&m) without a name), fov +0xf4 = +0xf0, near +0xf8 = +0xd8 * +0xd0, smoothing
//   0x0040fd50 (byte +0x15c first sets +0x160 = +0x164 = 1.0).
// - otherwise with a camera name +8 and a look-at name +0x28: pitch +0x74 += (axis 6 - 7 unless 0x00410150) * +0x78 *
//   dt, clamped to [-1.5608, 1.15]; the camera node (0x0040a4d0 with fov/near outputs) and the look-at node relative
//   to the target (vt+0x6c position, rotations by +0x70 and +0x84, pitch +0x74 and its 0.1/0.2 adjusted twin) give a
//   desired matrix; five rays from the eye (towards the look point and +-5 along the look matrix axes) through
//   0x00413cc0/0x00413d40/0x00413d70/0x00413dc0/0x00413e10 (query block at frame +0xa0) shorten the distance to
//   the nearest hit; hit objects that are not part of the target fade (vt+0x100 down to g_game +0xcac by +0xcb4 *
//   dt) and are collected in +0x158 (vt+0x20 / vt+0x18, flag 0x800 of +0x424); objects left in the list fade back up
//   (by +0xcb0 * dt) and leave it at 1.0. Then 0x004100e0 when +0x17c == 2, 0x0040fd50 or 0x0040fbb0, 0x0040e300,
//   copy to +0x8c and the inverse into the view.
// - with only the camera name: the node matrix directly (0x0040a4d0 into +0x8c, 0x004100e0, 0x0040e300,
//   0x0040fbb0).
// - without a name and with +0x168: look from the hint point +0x16c (or the midpoint between player and hint when
//   the hint is farther from the target than the player) at the target, smoothing with +0x160 = 900, +0x164 = 1000.
// Finally 0x0040f750(camera); when +0x17c and the slot 4 of +0x178 (0x00408800) is active with flag 2 at +0x1c:
// for +0x17c == 1: +0x160 = +0x164 = 1.0, byte +0x15c = 1, 0x004757b0, g_game byte +0xdce = 0; +0x17c = +0x180 = 0.
uint32_t Camera_Update(void* cam, float dt)
{
    alignas(8) uint32_t F[0x2a8 / 4];  // F + X = the original [esp + X] after the three register pushes
#define FA(o) (u(F) + (o))
#define FU(o) (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(F) + (o)))
#define FF(o) (*reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(F) + (o)))
    const float zero = GameGlobal<float>(0x0056c05c);
    const uint32_t c = u(cam);
    void* g = GGame();
    if (!Field<uint8_t>(g, 0xdcb))
    {
        void* t = Field<void*>(cam, 4);
        if (!t)
            return 0;
        VCall<void>(t, 0x3c / 4);
        if (Field<uint32_t>(GGame(), 0x2684))
            return 0;
        if (!static_cast<uint8_t>(VCall<uint32_t>(Field<void*>(cam, 4), 0xd8 / 4)))
        {
            orig::fn_004757b0(Field<uint32_t>(GGame(), 0x748));
            fn_0040e270(cam, Field<uint32_t>(cam, 4));
        }
        const uint32_t n = Field<uint32_t>(cam, 0x168);
        if (n &&
            !static_cast<uint8_t>(orig::fn_00414810((n & 0xffffff00u) | 1u, 9, Field<uint32_t>(GGame(), 0xa80), 0x3f000000u)))
        {
            orig::fn_004757b0(Field<uint32_t>(GGame(), 0x748));
            fn_0040e270(cam, Field<uint32_t>(cam, 4));
        }
        g = GGame();
    }
    const uint32_t mode = Field<uint32_t>(cam, 0x88);
    uint32_t view = 0;  // the matrix inverted into the view at 0x0040f645 (0 = skip)
    if (mode == 2 || Field<uint8_t>(g, 0xdcb))
    {
        // 0x0040f21a: free / input-driven camera.
        FF(0x10) = orig::fn_00414a30(Field<uint32_t>(g, 0xa80), 4,
                                     f2u(PlayerAim_4759e0(Field<uint32_t>(g, 0x748)) * GameGlobal<float>(0x005bcb68)),
                                     0x3e570a3d, 0x3f000000);
        g = GGame();
        float r = orig::fn_00414a30(Field<uint32_t>(g, 0xa80), 5,
                                    f2u(PlayerAim_4759e0(Field<uint32_t>(g, 0x748)) * GameGlobal<float>(0x005bcb68)),
                                    0x3e570a3d, 0x3f000000);
        FF(0x10) = (FF(0x10) - r) * dt;
        if (FF(0x10) == zero)
        {
            std::memset(&FU(0x58), 0, 0x11 * 4);
            FU(0x58) = 0x3f800000;
            FU(0x6c) = 0x3f800000;
        }
        else
        {
            const float s = static_cast<float>(std::sin(static_cast<double>(FF(0x10))));
            std::memset(&FU(0x58), 0, 0x11 * 4);
            FU(0x98) = 1;
            const float co = static_cast<float>(std::cos(static_cast<double>(FF(0x10))));
            FF(0x58) = co;
            FF(0x5c) = -s;
            FF(0x68) = s;
            FF(0x6c) = co;
        }
        g = GGame();
        FU(0x80) = 0x3f800000;
        FU(0x94) = 0x3f800000;
        FF(0x14) = orig::fn_00414a30(Field<uint32_t>(g, 0xa80), 6,
                                     f2u(PlayerAim_475990(Field<uint32_t>(g, 0x748)) * 2.0f), 0x3e570a3d, 0x3f000000);
        g = GGame();
        r = orig::fn_00414a30(Field<uint32_t>(g, 0xa80), 7, f2u(PlayerAim_475990(Field<uint32_t>(g, 0x748)) * 2.0f),
                              0x3e570a3d, 0x3f000000);
        FF(0x14) = (FF(0x14) - r) * dt;
        if (FF(0x14) == zero)
        {
            std::memset(&FU(0xe8), 0, 0x11 * 4);
            FU(0xfc) = 0x3f800000;
            FU(0x110) = 0x3f800000;
        }
        else
        {
            const float s = static_cast<float>(std::sin(static_cast<double>(FF(0x14))));
            std::memset(&FU(0xe8), 0, 0x11 * 4);
            FU(0x128) = 1;
            const float co = static_cast<float>(std::cos(static_cast<double>(FF(0x14))));
            FF(0xfc) = co;
            FF(0x100) = -s;
            FF(0x10c) = s;
            FF(0x110) = co;
        }
        FU(0x64) = Field<uint32_t>(cam, 0x98);
        FU(0x84) = Field<uint32_t>(cam, 0xb8);
        FU(0x74) = Field<uint32_t>(cam, 0xa8);
        Field<uint32_t>(cam, 0x98) = 0;
        Field<uint32_t>(cam, 0xa8) = 0;
        Field<uint32_t>(cam, 0xb8) = 0;
        const uint32_t m = c + 0x8c;
        FU(0xe8) = 0x3f800000;
        FU(0x124) = 0x3f800000;
        MatMul(m, FA(0xa0), FA(0x58));
        MatMul(FA(0xe8), m, FA(0xa0));
        g = GGame();
        MatInverse(m, u(g) + 0x704);
        FF(0x10) = orig::fn_00414aa0(Field<uint32_t>(g, 0xa80), 3, 0x42b40000, 0x3e570a3d, 0x3f000000);
        r = orig::fn_00414aa0(Field<uint32_t>(GGame(), 0xa80), 2, 0x42b40000, 0x3e570a3d, 0x3f000000);
        FF(0x10) = FF(0x10) - r;
        FF(0x14) = orig::fn_00414aa0(Field<uint32_t>(GGame(), 0xa80), 0x21, 0x42b40000, 0x3e570a3d, 0x3f000000);
        r = orig::fn_00414aa0(Field<uint32_t>(GGame(), 0xa80), 0x20, 0x42b40000, 0x3e570a3d, 0x3f000000);
        FF(0x14) = FF(0x14) - r;
        FF(0x1c) = orig::fn_00414aa0(Field<uint32_t>(GGame(), 0xa80), 1, 0x42b40000, 0x3e570a3d, 0x3f000000);
        FF(0x14) = FF(0x14) * dt;
        FF(0x10) = FF(0x10) * dt;
        r = orig::fn_00414aa0(Field<uint32_t>(GGame(), 0xa80), 0, 0x42b40000, 0x3e570a3d, 0x3f000000);
        const float z = (FF(0x1c) - r) * dt;
        if (!(FF(0x14) == zero) || !(FF(0x10) == zero) || !(z == zero))
        {
            const float ny = -FF(0x14);
            std::memcpy(&FU(0xa0), P(m), 0x11 * 4);
            std::memset(&FU(0x130), 0, 0x11 * 4);
            FF(0x14c) = ny;
            FF(0x15c) = z;
            FU(0x130) = 0x3f800000;
            FU(0x144) = 0x3f800000;
            FU(0x158) = 0x3f800000;
            FU(0x16c) = 0x3f800000;
            FU(0x13c) = FU(0x10);
            FU(0x170) = 0x10;
            MatMul(FA(0x130), m, FA(0xa0));
            view = m;
        }
    }
    else if (mode == 3)
    {
        // 0x0040e435: look at the target node.
        FU(0x24) = Field<uint32_t>(cam, 0xa8);
        void* t = Field<void*>(cam, 4);
        FU(0x20) = Field<uint32_t>(cam, 0x98);
        FU(0x28) = Field<uint32_t>(cam, 0xb8);
        uint32_t name = c + 8;
        if (Field<uint8_t>(t, 0x421))
            name = 0x005a009c;  // "center"
        if (Field<uint8_t>(t, 0x421) || *reinterpret_cast<const char*>(name) != 0)
            fn_0040a4d0(name, &FF(0x130), t, Field<float>(cam, 0x154), nullptr, nullptr, nullptr);
        else
            VCall<void>(t, 0x5c / 4, FA(0x130));
        Field<uint32_t>(cam, 0xf4) = Field<uint32_t>(cam, 0xf0);
        Field<float>(cam, 0xf8) = Field<float>(cam, 0xd8) * Field<float>(cam, 0xd0);
        FF(0x30) = FF(0x20) - FF(0x13c);
        FF(0x34) = FF(0x24) - FF(0x14c);
        FF(0x38) = FF(0x28) - FF(0x15c);
        MatLookAt(FA(0xe8), FA(0x30), FA(0x20));
        if (!Field<uint8_t>(cam, 0x15c))
        {
            Field<uint8_t>(cam, 0x15c) = 1;
            Field<uint32_t>(cam, 0x164) = 0x3f800000;
            Field<uint32_t>(cam, 0x160) = 0x3f800000;
        }
        orig::fn_0040fd50(c, FA(0xe8));
        std::memcpy(&Field<uint8_t>(cam, 0x8c), &FU(0xe8), 0x11 * 4);
        view = FA(0xe8);
    }
    else if (Field<uint8_t>(cam, 8) && Field<uint8_t>(cam, 0x28))
    {
        // 0x0040e56c: orbit camera with collision rays.
        FU(0x2c) = c + 8;
        float st;
        if (static_cast<uint8_t>(orig::fn_00410150(0, Field<uint32_t>(g, 0xa68))))
            st = zero;
        else
        {
            FF(0x1c) = orig::fn_00414a30(Field<uint32_t>(g, 0xa80), 6, 0x3f800000, 0x3e570a3d, 0x3f000000);
            const float r7 =
                orig::fn_00414a30(Field<uint32_t>(GGame(), 0xa80), 7, 0x3f800000, 0x3e570a3d, 0x3f000000);
            st = FF(0x1c) - r7;
        }
        const float pitch = st * Field<float>(cam, 0x78) * dt + Field<float>(cam, 0x74);
        Field<float>(cam, 0x74) = pitch;
        if (pitch < GameGlobal<float>(0x005bcebc))
            Field<uint32_t>(cam, 0x74) = 0xbfc7c82du;
        if (Field<float>(cam, 0x74) > GameGlobal<float>(0x005bceb8))
            Field<uint32_t>(cam, 0x74) = 0x3f933333u;
        if (!static_cast<uint8_t>(fn_0040a4d0(c + 0x28, &FF(0x218), Field<void*>(cam, 4), Field<float>(cam, 0x154),
                                              &Field<float>(cam, 0xf4), &Field<uint32_t>(cam, 0xf8), &FU(0x1c))))
            goto tail;
        Field<uint32_t>(cam, 0xf0) = Field<uint32_t>(cam, 0xf4);
        VCall<void>(Field<void*>(cam, 4), 0x6c / 4, FA(0x1c4));
        orig::fn_004e2d50(FA(0xe8), FA(0x1c4));
        MatInverse(FA(0xe8), FA(0x178));
        MatMul(FA(0x218), FA(0x130), FA(0x178));
        MatRotZ(FA(0x58), Field<uint32_t>(cam, 0x70));
        MatMul(FA(0x58), FA(0x1d0), FA(0xe8));
        MatMul(FA(0x130), FA(0x58), FA(0x1d0));
        MatRotZ(FA(0xe8), Field<uint32_t>(cam, 0x84));
        MatMul(FA(0xe8), FA(0xa0), FA(0x58));
        fn_0040a4d0(FU(0x2c), &FF(0x260), Field<void*>(cam, 4), Field<float>(cam, 0x154), nullptr, nullptr, nullptr);
        MatMul(FA(0x260), FA(0x130), FA(0x178));
        MatMul(FA(0x130), FA(0xe8), FA(0x1d0));
        MatInverse(FA(0xe8), FA(0x130));
        MatMul(FA(0xa0), FA(0x1d0), FA(0x130));
        MatRotX(FA(0xa0), Field<uint32_t>(cam, 0x74));
        {
            const float a = Field<float>(cam, 0x74);
            const float x = Field<float>(cam, 0x74);
            float v;
            if (x < zero)
                v = a - x * GameGlobal<float>(0x005bcbfc);
            else
                v = x * GameGlobal<float>(0x005bcd2c) + a;
            FF(0x18) = v;
        }
        MatRotX(FA(0x130), FU(0x18));
        MatMul(FA(0x1d0), FA(0x178), FA(0xa0));
        MatMul(FA(0x1d0), FA(0xa0), FA(0x130));
        MatMul(FA(0xa0), FA(0x130), FA(0xe8));
        MatMul(FA(0x178), FA(0x58), FA(0xe8));
        FF(0x20) = FF(0x64) - FF(0xf4);
        FU(0x30) = FU(0xf4);
        FU(0x38) = FU(0x114);
        FF(0x24) = FF(0x74) - FF(0x104);
        FU(0x34) = FU(0x104);
        FU(0x3c) = FU(0x64);
        FU(0x44) = FU(0x84);
        FF(0x28) = FF(0x84) - FF(0x114);
        FU(0x40) = FU(0x74);
        {
            const float len = Vec3_Normalize(FA(0x20), FA(0x20));
            const float count = static_cast<float>(Field<uint32_t>(GGame(), 0x26f4));
            FF(0x2c) = len + count * GameGlobal<float>(0x005bcb68);
        }
        // Objects collected last frame fade back in; at 1.0 they leave the list.
        FU(0x48) = 0;
        FU(0x4c) = 0;
        for (uint32_t e = orig::fn_004e7340(Field<uint32_t>(cam, 0x158), FA(0x48)); e;
             e = orig::fn_004e73c0(FA(0x48)))
        {
            if (!(Field<uint32_t>(P(e), 0x424) & 0x800))
            {
                const float r = orig::fn_00453630(e);
                FF(0x10) = dt * Field<float>(GGame(), 0xcb0) + r;
                if (FF(0x10) >= GameGlobal<float>(0x005bcb2c))
                {
                    FU(0x10) = 0x3f800000;
                    ReleaseRef(e);
                    VCall<void>(Field<void*>(cam, 0x158), 0x28 / 4, e);
                }
                VCall<void>(P(e), 0x100 / 4, FU(0x10));
            }
            Field<uint32_t>(P(e), 0x424) &= 0xfffff7ffu;
        }
        uint32_t found;
        {
            const uint32_t block = HeapAlloc(0x18);
            found = block ? orig::fn_004e7010(Field<uint32_t>(GGame(), 0xac4), 0, block) : 0;
            FU(0x54) = found;
        }
        orig::fn_00413cc0(FA(0xa0), 1);
        {
            const float k = GameGlobal<float>(0x005bcbdc);  // 5.0
            const float ax = FF(0x58) * k;
            FU(0x178) = FU(0x3c);
            FU(0x17c) = FU(0x40);
            FU(0x180) = FU(0x44);
            FF(0x184) = FF(0x64) + ax;
            const float ay = FF(0x68) * k;
            FF(0x188) = FF(0x74) + ay;
            FF(0x18) = FF(0x78) * k;
            FF(0x18c) = FF(0x84) + FF(0x18);
            FF(0x190) = FF(0x64) - ax;
            FF(0x194) = FF(0x74) - ay;
            FF(0x198) = FF(0x84) - FF(0x18);
            const float bx = FF(0x5c) * k;
            FF(0x19c) = FF(0x64) + bx;
            const float by = FF(0x6c) * k;
            FF(0x1a0) = FF(0x74) + by;
            FF(0x18) = FF(0x7c) * k;
            FF(0x1a4) = FF(0x84) + FF(0x18);
            FF(0x1a8) = FF(0x64) - bx;
            FF(0x1ac) = FF(0x74) - by;
            const float last = FF(0x84) - FF(0x18);
            FU(0x18) = 5;
            FF(0x1b0) = last;
        }
        for (uint32_t o = 0; FU(0x18) != 0; o += 0xc)
        {
            FF(0x58 + o) = FF(0x178 + o) - FF(0xf4);
            FF(0x5c + o) = FF(0x17c + o) - FF(0x104);
            FF(0x60 + o) = FF(0x180 + o) - FF(0x114);
            Vec3_Normalize(FA(0x58 + o), FA(0x58 + o));
            --FU(0x18);
        }
        FU(0x14) = 0x60ad78ec;  // 1e20
        FU(0x18) = FA(0x58);
        FU(0x1c) = 5;
        do
        {
            const uint32_t* d = reinterpret_cast<const uint32_t*>(FU(0x18));
            FU(0xb4) = d[0];
            FU(0xbc) = d[2];
            FU(0xa8) = FU(0x30);
            FU(0xb8) = d[1];
            FU(0xb0) = FU(0x38);
            FU(0xc0) = FU(0x2c);
            FU(0xac) = FU(0x34);
            reinterpret_cast<uint8_t*>(F)[0xdc] = 1;
            FU(0xe0) = 1;
            reinterpret_cast<uint8_t*>(F)[0xdd] = 0;
            orig::fn_00413d40(Field<uint32_t>(cam, 4), FA(0xa0));
            orig::fn_00413d70(found, FA(0xa0));
            orig::fn_00413dc0(Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xea0), FA(0xa0));
            orig::fn_00413e10(FA(0xa0));
            for (uint32_t hit = FU(0xd4); hit; hit = FU(0xd4))
            {
                VCall<void>(P(found), 0x18 / 4, hit);
                uint32_t root = hit;
                if (orig::fn_004539d0(hit))
                {
                    uint32_t r;
                    do
                    {
                        root = orig::fn_004539d0(root);
                        r = orig::fn_004539d0(root);
                    } while (r);
                }
                if (root != Field<uint32_t>(cam, 4))
                {
                    void* gg = GGame();
                    if (orig::fn_00453630(hit) > Field<float>(gg, 0xcac))
                    {
                        const float a = orig::fn_00453630(hit);
                        FF(0x10) = a - dt * Field<float>(gg, 0xcb4);
                        if (FF(0x10) < Field<float>(gg, 0xcac))
                            FU(0x10) = Field<uint32_t>(gg, 0xcac);
                        VCall<void>(P(hit), 0x100 / 4, FU(0x10));
                    }
                    if (static_cast<uint8_t>(VCall<uint32_t>(P(hit), 0x114 / 4)) &&
                        (Field<uint32_t>(P(hit), 0x460) & 0x8000))
                    {
                        FU(0x3c) = 0;
                        FU(0x40) = 0;
                        for (uint32_t o = orig::fn_004e7340(Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xed8), FA(0x3c));
                             o; o = orig::fn_004e73c0(FA(0x3c)))
                        {
                            if (orig::fn_004539d0(o) != hit)
                                continue;
                            void* g2 = GGame();
                            if (!(orig::fn_00453630(o) > Field<float>(g2, 0xcac)))
                                continue;
                            const float a = orig::fn_00453630(o);
                            FF(0x10) = a - dt * Field<float>(g2, 0xcb4);
                            if (FF(0x10) < Field<float>(g2, 0xcac))
                                FU(0x10) = Field<uint32_t>(g2, 0xcac);
                            VCall<void>(P(o), 0x100 / 4, FU(0x10));
                            if (!VCall<uint32_t>(Field<void*>(cam, 0x158), 0x20 / 4, o))
                            {
                                ++Field<int32_t>(P(o), 0x28);
                                VCall<void>(Field<void*>(cam, 0x158), 0x18 / 4, o);
                            }
                            Field<uint32_t>(P(o), 0x424) |= 0x800;
                        }
                        if (FU(0x40))
                            --Field<int32_t>(P(FU(0x40)), 0x18);
                        found = FU(0x54);
                    }
                    if (!VCall<uint32_t>(Field<void*>(cam, 0x158), 0x20 / 4, hit))
                    {
                        ++Field<int32_t>(P(hit), 0x28);
                        VCall<void>(Field<void*>(cam, 0x158), 0x18 / 4, hit);
                    }
                    Field<uint32_t>(P(hit), 0x424) |= 0x800;
                }
                const uint32_t* d2 = reinterpret_cast<const uint32_t*>(FU(0x18));
                FU(0xb8) = d2[1];
                FU(0xb4) = d2[0];
                FU(0xbc) = d2[2];
                FU(0xac) = FU(0x34);
                FU(0xa8) = FU(0x30);
                FU(0xb0) = FU(0x38);
                reinterpret_cast<uint8_t*>(F)[0xdc] = 1;
                FU(0xe0) = 1;
                reinterpret_cast<uint8_t*>(F)[0xdd] = 0;
                FU(0xc0) = FU(0x2c);
                orig::fn_00413d40(Field<uint32_t>(cam, 4), FA(0xa0));
                orig::fn_00413d70(found, FA(0xa0));
                orig::fn_00413dc0(Field<uint32_t>(Field<void*>(GGame(), 0xa84), 0xea0), FA(0xa0));
                orig::fn_00413e10(FA(0xa0));
            }
            if (FF(0x14) > FF(0xc4))
                FU(0x14) = FU(0xc4);
            FU(0x18) += 0xc;
        } while (--FU(0x1c) != 0);
        if (found)
            VCall<void>(P(found), 0x10 / 4, 1u);
        FF(0x13c) = FF(0x20) * FF(0x14) + FF(0xf4);
        FF(0x14c) = FF(0x24) * FF(0x14) + FF(0x104);
        FF(0x15c) = FF(0x28) * FF(0x14) + FF(0x114);
        if (Field<uint32_t>(cam, 0x17c) == 2)
            orig::fn_004100e0(c, FA(0x130));
        if (Field<uint8_t>(cam, 0x15c))
            orig::fn_0040fd50(c, FA(0x130));
        else
            orig::fn_0040fbb0(c, FA(0x130));
        fn_0040e300(cam, &FU(0x130));
        std::memcpy(&Field<uint8_t>(cam, 0x8c), &FU(0x130), 0x11 * 4);
        MatInverse(FA(0x130), u(GGame()) + 0x704);
        if (FU(0x4c))
            --Field<int32_t>(P(FU(0x4c)), 0x18);
        goto tail;
    }
    else if (Field<uint8_t>(cam, 8))
    {
        // 0x0040effe: the camera node directly.
        const uint32_t m = c + 0x8c;
        if (!static_cast<uint8_t>(fn_0040a4d0(c + 8, static_cast<float*>(P(m)), Field<void*>(cam, 4),
                                              Field<float>(cam, 0x154), &Field<float>(cam, 0xf4),
                                              &Field<uint32_t>(cam, 0xf8), &FU(0x1c))))
            goto tail;
        if (Field<uint32_t>(cam, 0x17c) == 2)
            orig::fn_004100e0(c, m);
        fn_0040e300(cam, P(m));
        orig::fn_0040fbb0(c, m);
        view = m;
    }
    else
    {
        // 0x0040f055: look from a hint point.
        if (!Field<uint32_t>(cam, 0x168))
            goto tail;
        VCall<void>(Field<void*>(g, 0x748), 0x54 / 4, FA(0x20));
        VCall<void>(Field<void*>(cam, 4), 0x54 / 4, FA(0x30));
        const float a = FF(0x20) - FF(0x30);
        const float b = FF(0x24) - FF(0x34);
        FF(0x2c) = Field<float>(cam, 0x170) - FF(0x34);
        FF(0x1c) = Field<float>(cam, 0x16c) - FF(0x30);
        const float d1 = std::sqrt(b * b + a * a);
        const float d2 = std::sqrt(FF(0x1c) * FF(0x1c) + FF(0x2c) * FF(0x2c));
        if (d2 > d1)
        {
            const float p = FF(0x20) + Field<float>(cam, 0x16c);
            const float q = FF(0x24) + Field<float>(cam, 0x170);
            FF(0x44) = FF(0x28) + Field<float>(cam, 0x174);
            FF(0x3c) = p * GameGlobal<float>(0x005bcb4c);
            FF(0x40) = q * GameGlobal<float>(0x005bcb4c);
            FF(0x44) = FF(0x44) * GameGlobal<float>(0x005bcb4c);
        }
        else
        {
            FF(0x48) = Field<float>(cam, 0x16c) - FF(0x20);
            FF(0x4c) = Field<float>(cam, 0x170) - FF(0x24);
            FF(0x50) = Field<float>(cam, 0x174) - FF(0x28);
            Vec3_Normalize(FA(0x48), FA(0x48));
            const float x = FF(0x48) * GameGlobal<float>(0x005bceb4);
            const float y = FF(0x4c) * GameGlobal<float>(0x005bceb4);
            FF(0x50) = FF(0x50) * GameGlobal<float>(0x005bceb4) + GameGlobal<float>(0x005bcb80);
            FF(0x3c) = x + FF(0x30);
            FF(0x40) = y + FF(0x34);
            FF(0x44) = FF(0x38) + FF(0x50);
        }
        FF(0x48) = FF(0x3c) - FF(0x30);
        FF(0x4c) = FF(0x40) - FF(0x34);
        FF(0x50) = FF(0x44) - FF(0x38);
        MatLookAt(FA(0x1d0), FA(0x48), FA(0x3c));
        Field<uint32_t>(cam, 0x160) = 0x44610000;
        Field<uint32_t>(cam, 0x164) = 0x447a0000;
        orig::fn_0040fd50(c, FA(0x1d0));
        std::memcpy(&Field<uint8_t>(cam, 0x8c), &FU(0x1d0), 0x11 * 4);
        view = c + 0x8c;
    }
    if (view)
        MatInverse(view, u(GGame()) + 0x704);
tail:
    orig::fn_0040f750(c);
    {
        const uint32_t kind = Field<uint32_t>(cam, 0x17c);
        if (kind && Field<uint32_t>(cam, 0x178))
        {
            const uint32_t slot = fn_00408800(4, Field<void*>(cam, 0x178));
            if (Field<uint32_t>(P(slot), 0) != 0xffffffffu && (Field<uint8_t>(P(slot), 0x1c) & 2))
            {
                if (kind == 1)
                {
                    Field<uint32_t>(cam, 0x164) = 0x3f800000;
                    Field<uint32_t>(cam, 0x160) = 0x3f800000;
                    Field<uint8_t>(cam, 0x15c) = 1;
                    orig::fn_004757b0(Field<uint32_t>(GGame(), 0x748));
                    Field<uint8_t>(GGame(), 0xdce) = 0;
                }
                Field<uint32_t>(cam, 0x17c) = 0;
                Field<uint32_t>(cam, 0x180) = 0;
            }
        }
    }
    return 0;  // original: eax as left
#undef FA
#undef FU
#undef FF
}
static uint32_t Camera_Update_Abi(uint32_t a0, uint32_t a1)
{
    return Camera_Update(P(a0), u2f(a1));
}
ABI_ENTRY_0040e350(Camera_Update_Entry, Camera_Update_Abi)
HOOK(0x0040e350, "Camera_Update", Camera_Update_Entry, nullptr);
