// Recovered (static, batch engine_01): shared engine utilities and small helpers (heap wrappers, string and
// token helpers, vector/matrix math, class-descriptor getters, game start-up glue). Compile-checked only (the game
// was not run); the oracle (tools/compare.py) is still to do. Functions the recorded runs never execute are marked
// "not executed in recorded runs".
//
// Conventions: each function has an implementation, an _Abi adapter taking exactly the ABI's inputs (register
// inputs in the order eax ecx edx ebx esi edi, then stack arguments) and the generated ABI_ENTRY stub, unless a
// hand-written stub is noted. x87 code is reproduced in float in the disassembly's order (the game runs the FPU at
// single precision); fsin/fcos/fsqrt go through inline asm where the exact instruction matters.
#include <cstring>

#include "game.h"
#include "game/game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

#pragma warning(disable : 4731)  // ebp modified by inline asm (hand-written stubs save and restore it)

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
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
float& Fl(uint32_t address)
{
    return *reinterpret_cast<float*>(address);
}
uint32_t& D(uint32_t address)
{
    return *reinterpret_cast<uint32_t*>(address);
}
uint32_t GameP()
{
    return GameGlobal<uint32_t>(0x005fe490);
}

// x87 transcendental / root instructions, exactly as the original executes them.
float X87Sin(float a)
{
    float r;
    __asm {
        fld a
        fsin
        fstp r
    }
    return r;
}
float X87Cos(float a)
{
    float r;
    __asm {
        fld a
        fcos
        fstp r
    }
    return r;
}
float X87Sqrt(float a)
{
    float r;
    __asm {
        fld a
        fsqrt
        fstp r
    }
    return r;
}

// Thiscall through a function pointer with no stack arguments (the original: mov ecx, obj; call reg).
void CallThis(uint32_t fn, uint32_t self)
{
    reinterpret_cast<void(__fastcall*)(void*, void*)>(fn)(P(self), nullptr);
}

// The game's sscanf (CRT 0x005381a7, cdecl).
int GameSscanf(const char* s, const char* fmt, void* out)
{
    return GameFunction<int(__cdecl*)(const char*, const char*, ...)>(0x005381a7)(s, fmt, out);
}
// The game's rand (CRT 0x005391cd).
int GameRand()
{
    return GameFunction<int(__cdecl*)()>(0x005391cd)();
}

constexpr uint32_t kHeap = 0x0063a210;  // the game heap object (ecx of PMS_heapAlloc / PMS_heapFree)
}  // namespace

// Prototypes of functions recovered in this file and called before their definition.
uint32_t Mat_TransformVec3(uint32_t vec, uint32_t mat, uint32_t asDirection, uint32_t out);
uint32_t fn_004e3070(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi);
uint32_t fn_0048e970(uint32_t esi);

// ---------------------------------------------------------------------------------------------------------------
// 0x004011d0 (stack: block; caller pops; the ABI lists 4 stack slots, 1 is read). Frees a block to the game heap:
// PMS_heapFree 0x004e08d0 (ecx = heap 0x0063a210; block). Returns its eax. Callers: compiler-generated code at
// 0x005475da.. (operator delete, guess).
uint32_t GameHeap_Free(uint32_t block)
{
    return orig::fn_004e08d0(kHeap, block);
}
static uint32_t GameHeap_Free_Abi(uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/)
{
    return GameHeap_Free(a0);
}
ABI_ENTRY_004011d0(GameHeap_Free_Entry, GameHeap_Free_Abi)
HOOK(0x004011d0, "GameHeap_Free", GameHeap_Free_Entry, nullptr);

// 0x00401fb0 (ecx = game; stack: a0 (not read), a1, a2; ret 0xc). Caller Game_Init 0x004011e0. Resets the game's
// camera matrix +0x704 to identity (flags word 0), clears +0x588 (0x42 dwords), +0x690 (0x1c), +0xd90 (0x669) and
// +0xac8 (8), unpacks the display mode Game+0x4a8 (DisplayMode_Unpack 0x00470cc0) into a 0x58-byte app-init block
// (flags 7, mode, callbacks 0x0045a480/0x0045a420/0x0045a4c0/0x0045a440/0x0045a450/Debug_Printf/0x00495e00, the
// matrix pointer, 0x3e9c0ec1 = 0.305) and passes it to App_Init 0x004e3fc0 (ecx = game; block, a1, a2). Then
// 0x0064a224 = 0x00401000 and the renderer (+0x38) virtual +0x10 (Game_OnDeviceLost 0x00401070,
// Game_OnDeviceRestored 0x004010c0, game). Returns that call's eax.
uint32_t Game_InitApp(uint32_t game, uint32_t a1, uint32_t a2)
{
    // Original frame: three DisplayMode_Unpack outputs at esp+0x10 followed by the 0x16-dword block at esp+0x1c.
    uint32_t frame[3 + 0x16] = {};
    uint32_t* out = frame;
    uint32_t* s = frame + 3;
    const uint32_t m = game + 0x704;
    for (int i = 0; i < 0x11; ++i)
        D(m + i * 4) = 0;
    D(m) = 0x3f800000;
    D(m + 0x14) = 0x3f800000;
    D(m + 0x28) = 0x3f800000;
    D(m + 0x3c) = 0x3f800000;
    s[1] |= 1;
    orig::fn_00470cc0(D(GameP() + 0x4a8), u(&out[0]), u(&out[1]), u(&out[2]));
    s[0] |= 1;
    s[0] |= 2;
    s[0] |= 4;
    s[2] = out[0];
    s[4] = out[2];
    std::memset(P(game + 0x588), 0, 0x42 * 4);
    std::memset(P(game + 0x690), 0, 0x1c * 4);
    std::memset(P(game + 0xd90), 0, 0x669 * 4);
    std::memset(P(game + 0xac8), 0, 8 * 4);
    s[3] = out[1];
    s[20] = 0;
    s[8] = 0;
    reinterpret_cast<uint8_t*>(&s[19])[0] = 1;
    reinterpret_cast<uint8_t*>(&s[21])[0] = 1;
    reinterpret_cast<uint8_t*>(&s[7])[0] = 1;
    s[18] = m;
    s[9] = 0x3e9c0ec1;
    s[16] = 0x0045a480;
    s[12] = 0x0045a420;
    s[10] = 0x0045a4c0;
    s[13] = 0x0045a440;
    s[14] = 0x0045a450;
    s[15] = 0x00407ff0;
    s[17] = 0x00495e00;
    orig::fn_004e3fc0(game, u(s), a1, a2);
    GameGlobal<uint32_t>(0x0064a224) = 0x00401000;
    return VCall<uint32_t>(P(D(game + 0x38)), 0x10 / 4, 0x00401070u, 0x004010c0u, game);
}
static uint32_t Game_InitApp_Abi(uint32_t ecx, uint32_t /*a0*/, uint32_t a1, uint32_t a2)
{
    return Game_InitApp(ecx, a1, a2);
}
ABI_ENTRY_00401fb0(Game_InitApp_Entry, Game_InitApp_Abi)
HOOK(0x00401fb0, "Game_InitApp", Game_InitApp_Entry, nullptr);

// 0x00403150 (al = on; stack: game, byte flag; ret 8). Callers 0x00401ce0 and Game_SetPaused 0x004031e0. Off:
// PostEffects_Stop 0x0047bfe0 (edi = Game+0xa9c; 1, flag). On, when game+0x2684 (pause count) is set: Stop (1, 1),
// PostEffects_AddBlur 0x0047be60 (ecx = Game+0xa9c; 1, 0x005bc15c), PostEffects_SetTimes 0x0047c1a0 (eax = Game+0xa9c,
// ecx = 1, edx = 1; 0.1, -1.0, 0.5) and PostEffects_SetIgnorePause 0x0047c1d0 (eax = Game+0xa9c, ecx = edx = esi = 1:
// the registers SetTimes leaves). Returns the last callee's eax (0 when the count is 0).
uint32_t Game_PauseEffect(uint32_t on, uint32_t game, uint32_t flag)
{
    if (!static_cast<uint8_t>(on))
        return orig::fn_0047bfe0(D(GameP() + 0xa9c), 1, static_cast<uint8_t>(flag));
    const uint32_t c = D(game + 0x2684);
    if (!c)
        return c;
    const uint32_t g = GameP();
    orig::fn_0047bfe0(D(g + 0xa9c), 1, 1);
    orig::fn_0047be60(D(g + 0xa9c), 1, 0x005bc15c);
    orig::fn_0047c1a0(D(g + 0xa9c), 1, 1, 0x3dcccccd, 0xbf800000, 0x3f000000);
    return orig::fn_0047c1d0(D(g + 0xa9c), 1, 1, 1);
}
static uint32_t Game_PauseEffect_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Game_PauseEffect(eax, a0, a1);
}
ABI_ENTRY_00403150(Game_PauseEffect_Entry, Game_PauseEffect_Abi)
HOOK(0x00403150, "Game_PauseEffect", Game_PauseEffect_Entry, nullptr);

// 0x004031e0 (bl = with effect, edi = game; stack: byte pause; ret 4). Callers 0x00403410, 0x00462790, 0x00490a40.
// Game+0xa80: 0x00414580 (eax) and 0x00414680 (eax, ecx = 0x23). Pause: Game+0x2684 += 1; on the first pause the
// audio object (+0x2c) virtual +0xe0 (0, 1, 0), 0x004e56c0 (Game+0x30, 1), audio virtuals +0x88/+0x90/+0x8c with
// game +0x490/+0x494/+0x498, Game_PauseEffect (al = 1; game, 0) when bl, and Game+0x2688 = the clock 0x00604a98.
// Unpause: when the count is 1, audio virtual +0xe4 (0, 1, 0), 0x004e56c0 (Game+0x30, 0), Game_PauseEffect (al = 0;
// game, 0) when bl, bytes +0x15dc = 0 / +0xdcb = 0, +0x1de0 = 0, and the paused time dt = clock - Game+0x2688 is
// added to 0x00604aa4 and taken off the clock 0x00604a98 and 0x00604ac4; then the count drops (if non-zero) and
// 0x00403390 (eax = game) runs when the game's count reached 0. Returns eax as left.
uint32_t Game_SetPaused(uint32_t withEffect, uint32_t game, uint32_t pause)
{
    uint32_t esi = GameP();
    orig::fn_00414580(D(esi + 0xa80));
    uint32_t r = orig::fn_00414680(D(esi + 0xa80), 0x23);
    if (static_cast<uint8_t>(pause))
    {
        const uint32_t n = D(esi + 0x2684) + 1;
        D(esi + 0x2684) = n;
        if (n != 1)
            return n;
        VCall<uint32_t>(P(D(esi + 0x2c)), 0xe0 / 4, 0u, n, 0u);
        orig::fn_004e56c0(D(GameP() + 0x30), 1);
        VCall<uint32_t>(P(D(GameP() + 0x2c)), 0x88 / 4, D(game + 0x490));
        VCall<uint32_t>(P(D(GameP() + 0x2c)), 0x90 / 4, D(game + 0x494));
        r = VCall<uint32_t>(P(D(GameP() + 0x2c)), 0x8c / 4, D(game + 0x498));
        if (static_cast<uint8_t>(withEffect))
            r = Game_PauseEffect(1, game, 0);
        Fl(GameP() + 0x2688) = Fl(0x00604a98);
        return r;
    }
    if (D(esi + 0x2684) == 1)
    {
        VCall<uint32_t>(P(D(esi + 0x2c)), 0xe4 / 4, 0u, 1u, 0u);
        orig::fn_004e56c0(D(GameP() + 0x30), 0);
        if (static_cast<uint8_t>(withEffect))
            Game_PauseEffect(0, game, 0);
        esi = GameP();
        Field<uint8_t>(P(esi), 0x15dc) = 0;
        D(esi + 0x1de0) = 0;
        const float dt = Fl(0x00604a98) - Fl(esi + 0x2688);
        Fl(0x00604aa4) = Fl(0x00604aa4) + dt;
        Fl(0x00604a98) = Fl(0x00604a98) - dt;
        Fl(0x00604ac4) = Fl(0x00604ac4) - dt;
        Field<uint8_t>(P(esi), 0xdcb) = 0;
    }
    r = D(esi + 0x2684);
    if (r)
        D(esi + 0x2684) = --r;
    r = D(game + 0x2684);
    if (!r)
        r = orig::fn_00403390(game);
    return r;
}
static uint32_t Game_SetPaused_Abi(uint32_t ebx, uint32_t edi, uint32_t a0)
{
    return Game_SetPaused(ebx, edi, a0);
}
ABI_ENTRY_004031e0(Game_SetPaused_Entry, Game_SetPaused_Abi)
HOOK(0x004031e0, "Game_SetPaused", Game_SetPaused_Entry, nullptr);

// 0x00403a30 (stack: a0, a1; caller pops). Caller 0x004e4350. Allocates the 0x4c38-byte game object from the heap
// (PMS_heapAlloc 0x004e07f0, ecx = 0x0063a210; 0x10, 0x4c38, 2, "new"), constructs it with Game_Init 0x004011e0
// (eax = a1, ecx = a0; object, 0, "Armed & Dangerous"), stores it in g_game, runs it (virtual +0x1c), then
// destroys it (Game_Destruct 0x00401710, ebx = game) and frees it. Returns 0. Original bug: a failed allocation
// dereferences a null object (eax = 0, then mov edx, [eax]).
uint32_t Game_CreateAndRun(uint32_t a0, uint32_t a1)
{
    uint32_t game = orig::fn_004e07f0(kHeap, 0x10, 0x4c38, 2, 0x0058b5e8);
    if (game)
        game = orig::fn_004011e0(a1, a0, game, 0, 0x005bc0dc);
    GameGlobal<uint32_t>(0x005fe490) = game;
    VCall<uint32_t>(P(game), 0x1c / 4);
    const uint32_t g = GameP();
    if (g)
    {
        orig::fn_00401710(0, g);
        orig::fn_004e08d0(kHeap, g);
    }
    return 0;
}
static uint32_t Game_CreateAndRun_Abi(uint32_t a0, uint32_t a1)
{
    return Game_CreateAndRun(a0, a1);
}
ABI_ENTRY_00403a30(Game_CreateAndRun_Entry, Game_CreateAndRun_Abi)
HOOK(0x00403a30, "Game_CreateAndRun", Game_CreateAndRun_Entry, nullptr);

// 0x00403be0 (eax = source, edx = destination, di = stop character). Copies wide characters until a 0 or the stop
// character, terminates the destination and returns the number copied.
uint32_t WStr_CopyUntil(const uint16_t* src, uint16_t* dst, uint16_t stop)
{
    uint32_t n = 0;
    uint16_t c = *src;
    while (c && c != stop)
    {
        *dst++ = c;
        c = *++src;
        ++n;
    }
    *dst = 0;
    return n;
}
static uint32_t WStr_CopyUntil_Abi(uint32_t eax, uint32_t edx, uint32_t edi)
{
    return WStr_CopyUntil(reinterpret_cast<const uint16_t*>(eax), reinterpret_cast<uint16_t*>(edx),
                          static_cast<uint16_t>(edi));
}
ABI_ENTRY_00403be0(WStr_CopyUntil_Entry, WStr_CopyUntil_Abi)
HOOK(0x00403be0, "WStr_CopyUntil", WStr_CopyUntil_Entry, nullptr);

// 0x00403c10 (eax = source, ecx = capacity, edx = destination). Bounded wide copy: copies up to capacity characters;
// when the source ended, the destination is terminated after the copy and 0 is returned; when it was truncated the
// last copied character is overwritten with 0 and -1 is returned. Original bug: capacity 0 writes 0 to
// destination[-1]. Not executed in recorded runs.
uint32_t WStr_CopyN(const uint16_t* src, uint32_t n, uint16_t* dst)
{
    int32_t ecx = static_cast<int32_t>(n);
    bool ended = false;
    if (ecx != 0)
    {
        for (;;)
        {
            const uint16_t c = *src;
            --ecx;
            if (!c)
            {
                ended = true;
                break;
            }
            *dst++ = c;
            ++src;
            if (!ecx)
                break;
        }
    }
    if (!ended)
        --ecx;
    const uint32_t k = ecx != -1 ? 1u : 0u;
    dst[static_cast<int32_t>(k) - 1] = 0;
    return k - 1;
}
static uint32_t WStr_CopyN_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return WStr_CopyN(reinterpret_cast<const uint16_t*>(eax), ecx, reinterpret_cast<uint16_t*>(edx));
}
ABI_ENTRY_00403c10(WStr_CopyN_Entry, WStr_CopyN_Abi)
HOOK(0x00403c10, "WStr_CopyN", WStr_CopyN_Entry, nullptr);

// 0x00403c40 (stack: array, element size, count, thiscall function; ret 0x10). The compiler's vector constructor
// iterator: calls the function (ecx = element) for each element in order. Returns eax as left: count - 1 when
// there are no elements, else the last call's eax (returned here as count - 1; callers ignore it).
uint32_t VectorCtorIterator(uint32_t array, uint32_t size, uint32_t count, uint32_t fn)
{
    const int32_t n = static_cast<int32_t>(count) - 1;
    if (n < 0)
        return static_cast<uint32_t>(n);
    uint32_t p = array;
    for (int32_t i = n + 1; i != 0; --i)
    {
        CallThis(fn, p);
        p += size;
    }
    return static_cast<uint32_t>(n);
}
static uint32_t VectorCtorIterator_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return VectorCtorIterator(a0, a1, a2, a3);
}
ABI_ENTRY_00403c40(VectorCtorIterator_Entry, VectorCtorIterator_Abi)
HOOK(0x00403c40, "VectorCtorIterator", VectorCtorIterator_Entry, nullptr);

// 0x00407fd0 (eax = index, ecx = object): returns object + 0x204 + index * 0x2c (an element of an array of 0x2c-byte
// records). Not executed in recorded runs.
uint32_t fn_00407fd0(uint32_t index, uint32_t obj)
{
    return index * 0x2c + obj + 0x204;
}
static uint32_t fn_00407fd0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00407fd0(eax, ecx);
}
ABI_ENTRY_00407fd0(fn_00407fd0_Entry, fn_00407fd0_Abi)
HOOK(0x00407fd0, "fn_00407fd0", fn_00407fd0_Entry, nullptr);

// 0x00407fe0 (eax = object): returns object + 0x154.
uint32_t fn_00407fe0(uint32_t obj)
{
    return obj + 0x154;
}
static uint32_t fn_00407fe0_Abi(uint32_t eax)
{
    return fn_00407fe0(eax);
}
ABI_ENTRY_00407fe0(fn_00407fe0_Entry, fn_00407fe0_Abi)
HOOK(0x00407fe0, "fn_00407fe0", fn_00407fe0_Entry, nullptr);

// 0x00407ff0: a bare `ret` (compiled-out printf), called variadically with any argument layout; every register,
// EAX included, passes through unchanged (the generated stub preserves them). Not executed in recorded runs (it is
// called, but compiled out).
void Debug_Printf()
{
}
static void Debug_Printf_Abi(uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/)
{
    Debug_Printf();
}
ABI_ENTRY_00407ff0(Debug_Printf_Entry, Debug_Printf_Abi)
HOOK(0x00407ff0, "Debug_Printf", Debug_Printf_Entry, nullptr);

// 0x00408000 (eax = object): returns object + 0x84.
uint32_t fn_00408000(uint32_t obj)
{
    return obj + 0x84;
}
static uint32_t fn_00408000_Abi(uint32_t eax)
{
    return fn_00408000(eax);
}
ABI_ENTRY_00408000(fn_00408000_Entry, fn_00408000_Abi)
HOOK(0x00408000, "fn_00408000", fn_00408000_Entry, nullptr);

// 0x00408010 (eax = object): returns object + 0x58. Not executed in recorded runs.
uint32_t fn_00408010(uint32_t obj)
{
    return obj + 0x58;
}
static uint32_t fn_00408010_Abi(uint32_t eax)
{
    return fn_00408010(eax);
}
ABI_ENTRY_00408010(fn_00408010_Entry, fn_00408010_Abi)
HOOK(0x00408010, "fn_00408010", fn_00408010_Entry, nullptr);

// 0x00408020 (eax = object): returns object + 0x2c. Not executed in recorded runs.
uint32_t fn_00408020(uint32_t obj)
{
    return obj + 0x2c;
}
static uint32_t fn_00408020_Abi(uint32_t eax)
{
    return fn_00408020(eax);
}
ABI_ENTRY_00408020(fn_00408020_Entry, fn_00408020_Abi)
HOOK(0x00408020, "fn_00408020", fn_00408020_Entry, nullptr);

// 0x00408030 (eax = destination, ecx = object): copies the vec3 at object + 0x5c (no result; eax preserved).
void fn_00408030(uint32_t dst, uint32_t obj)
{
    const uint32_t s = obj + 0x5c;
    D(dst) = D(s);
    D(dst + 4) = D(s + 4);
    D(dst + 8) = D(s + 8);
}
static void fn_00408030_Abi(uint32_t eax, uint32_t ecx)
{
    fn_00408030(eax, ecx);
}
ABI_ENTRY_00408030(fn_00408030_Entry, fn_00408030_Abi)
HOOK(0x00408030, "fn_00408030", fn_00408030_Entry, nullptr);

// 0x004090a0 (eax = &index, edx = token table; stack: out; caller pops). Token reader: *out = table[index + 2]
// (pointer to the token text), index += 1. Returns edx = out (eax keeps &index; the stub preserves it).
uint64_t Tokens_NextString(uint32_t* index, uint32_t table, uint32_t* out)
{
    *out = D(table + *index * 4 + 8);
    ++*index;
    return static_cast<uint64_t>(u(out)) << 32;
}
static uint64_t Tokens_NextString_Abi(uint32_t eax, uint32_t edx, uint32_t a0)
{
    return Tokens_NextString(reinterpret_cast<uint32_t*>(eax), edx, reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_004090a0(Tokens_NextString_Entry, Tokens_NextString_Abi)
HOOK(0x004090a0, "Tokens_NextString", Tokens_NextString_Entry, nullptr);

// 0x004090b0 (eax = out int, edx = token table, esi = &index): sscanf(table[index + 2], "%d" (0x005bbb78), out),
// index += 1; returns the new index.
uint32_t Tokens_NextInt(uint32_t out, uint32_t table, uint32_t* index)
{
    GameSscanf(reinterpret_cast<const char*>(D(table + *index * 4 + 8)), reinterpret_cast<const char*>(0x005bbb78),
               P(out));
    return ++*index;
}
static uint32_t Tokens_NextInt_Abi(uint32_t eax, uint32_t edx, uint32_t esi)
{
    return Tokens_NextInt(eax, edx, reinterpret_cast<uint32_t*>(esi));
}
ABI_ENTRY_004090b0(Tokens_NextInt_Entry, Tokens_NextInt_Abi)
HOOK(0x004090b0, "Tokens_NextInt", Tokens_NextInt_Entry, nullptr);

// 0x004090d0 (eax = out float, edx = token table, esi = &index): as 0x004090b0 with "%f" (0x005bbb74). No direct
// callers; not executed in recorded runs.
uint32_t Tokens_NextFloat(uint32_t out, uint32_t table, uint32_t* index)
{
    GameSscanf(reinterpret_cast<const char*>(D(table + *index * 4 + 8)), reinterpret_cast<const char*>(0x005bbb74),
               P(out));
    return ++*index;
}
static uint32_t Tokens_NextFloat_Abi(uint32_t eax, uint32_t edx, uint32_t esi)
{
    return Tokens_NextFloat(eax, edx, reinterpret_cast<uint32_t*>(esi));
}
ABI_ENTRY_004090d0(Tokens_NextFloat_Entry, Tokens_NextFloat_Abi)
HOOK(0x004090d0, "Tokens_NextFloat", Tokens_NextFloat_Entry, nullptr);

// 0x004090f0 (stack: object; caller pops). Calls the object's virtual +0xd4 (0) and returns true (al = 1, upper eax
// bytes from that call). No direct callers (a callback); not executed in recorded runs.
uint32_t fn_004090f0(uint32_t obj)
{
    const uint32_t r = VCall<uint32_t>(P(obj), 0xd4 / 4, 0u);
    return (r & 0xffffff00u) | 1u;
}
static uint32_t fn_004090f0_Abi(uint32_t a0)
{
    return fn_004090f0(a0);
}
ABI_ENTRY_004090f0(fn_004090f0_Entry, fn_004090f0_Abi)
HOOK(0x004090f0, "fn_004090f0", fn_004090f0_Entry, nullptr);

// 0x0040de50 (ecx = object; stack: out vec3 and five arguments that are not read; ret 0x18). The object's position
// (virtual +0x54 into a local), a random angle a = rand() * 2pi/32768 (0x005bcdbc) - pi (0x005bcb70), the direction
// (0, -1, 0) rotated by the z rotation {cos, -sin; sin, cos} (Mat_TransformVec3 0x004e36a0, edx = 1) and normalised
// (0x004e7780, st0 result dropped); object +0x46c = 0; out = position + rand() * k * direction per axis, where k is
// 0.0 (0x0056c05c) for x and (+0x46c as float = 0) / 32768 for y and z, so out = position. Returns the last rand().
// No direct callers (virtual); not executed in recorded runs.
uint32_t fn_0040de50(uint32_t obj, uint32_t outp)
{
    uint32_t frame[0x1b] = {};  // esp+8 .. esp+0x74 of the original frame
    auto at = [&](uint32_t off) -> uint32_t { return u(frame) + off - 8; };
    VCall<uint32_t>(P(obj), 0x54 / 4, P(at(0x24)));
    D(at(0x18)) = 0;
    D(at(0x1c)) = 0xbf800000;
    D(at(0x20)) = 0;
    int r = GameRand();
    float a = static_cast<float>(r) * Fl(0x005bcdbc);
    for (int i = 0; i < 0x11; ++i)
        D(at(0x30) + i * 4) = 0;
    a = a - Fl(0x005bcb70);
    Fl(at(8)) = a;
    const float s = X87Sin(Fl(at(8)));
    D(at(0x58)) = 0x3f800000;
    D(at(0x6c)) = 0x3f800000;
    D(at(0x70)) = 1;
    const float c = X87Cos(Fl(at(8)));
    Fl(at(0x30)) = c;
    Fl(at(0x34)) = -s;
    Fl(at(0x40)) = s;
    Fl(at(0x44)) = c;
    Mat_TransformVec3(at(0x18), at(0x30), 1, at(0xc));
    orig::fn_004e7780(at(0xc), at(0xc));
    D(obj + 0x46c) = 0;
    r = GameRand();
    float* out = reinterpret_cast<float*>(outp);
    out[0] = static_cast<float>(r) * Fl(0x0056c05c) * Fl(at(0xc)) + Fl(at(0x24));
    D(at(8)) = D(obj + 0x46c);
    r = GameRand();
    out[1] = static_cast<float>(r) * Fl(at(8)) * Fl(0x005bcbc0) * Fl(at(0x10)) + Fl(at(0x28));
    D(at(8)) = D(obj + 0x46c);
    r = GameRand();
    out[2] = static_cast<float>(r) * Fl(at(8)) * Fl(0x005bcbc0) * Fl(at(0x14)) + Fl(at(0x2c));
    return static_cast<uint32_t>(r);
}
static uint32_t fn_0040de50_Abi(uint32_t ecx, uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/,
                                uint32_t /*a4*/, uint32_t /*a5*/)
{
    return fn_0040de50(ecx, a0);
}
ABI_ENTRY_0040de50(fn_0040de50_Entry, fn_0040de50_Abi)
HOOK(0x0040de50, "fn_0040de50", fn_0040de50_Entry, nullptr);

// 0x00411f30 (stack: array, element size, count, thiscall function; ret 0x10). The compiler's vector destructor
// iterator: calls the function (ecx = element) for each element, last first. Returns count - 1 (eax as left).
uint32_t VectorDtorIterator(uint32_t array, uint32_t size, uint32_t count, uint32_t fn)
{
    uint32_t p = array + size * count;
    const int32_t n = static_cast<int32_t>(count) - 1;
    if (n < 0)
        return static_cast<uint32_t>(n);
    for (int32_t i = n + 1; i != 0; --i)
    {
        p -= size;
        CallThis(fn, p);
    }
    return static_cast<uint32_t>(n);
}
static uint32_t VectorDtorIterator_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return VectorDtorIterator(a0, a1, a2, a3);
}
ABI_ENTRY_00411f30(VectorDtorIterator_Entry, VectorDtorIterator_Abi)
HOOK(0x00411f30, "VectorDtorIterator", VectorDtorIterator_Entry, nullptr);

// 0x0041f4f0 (eax = object, ecx = index): returns the dword object + 0x1c + index * 4.
uint32_t fn_0041f4f0(uint32_t obj, uint32_t index)
{
    return D(obj + index * 4 + 0x1c);
}
static uint32_t fn_0041f4f0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0041f4f0(eax, ecx);
}
ABI_ENTRY_0041f4f0(fn_0041f4f0_Entry, fn_0041f4f0_Abi)
HOOK(0x0041f4f0, "fn_0041f4f0", fn_0041f4f0_Entry, nullptr);

// 0x004238a0 / 0x004238b0: class descriptor 0x00588648 ("CLASSV(FxParticleColorTable)" at +0x14) and type id
// 0x80000006 (vtable entries 0x005b7de0 and 0x005b7ddc). Not executed in recorded runs.
uint32_t FxParticleColorTable_GetClass()
{
    return 0x00588648;
}
static uint32_t FxParticleColorTable_GetClass_Abi()
{
    return FxParticleColorTable_GetClass();
}
ABI_ENTRY_004238a0(FxParticleColorTable_GetClass_Entry, FxParticleColorTable_GetClass_Abi)
HOOK(0x004238a0, "FxParticleColorTable_GetClass", FxParticleColorTable_GetClass_Entry, nullptr);

uint32_t FxParticleColorTable_GetTypeId()
{
    return 0x80000006;
}
static uint32_t FxParticleColorTable_GetTypeId_Abi()
{
    return FxParticleColorTable_GetTypeId();
}
ABI_ENTRY_004238b0(FxParticleColorTable_GetTypeId_Entry, FxParticleColorTable_GetTypeId_Abi)
HOOK(0x004238b0, "FxParticleColorTable_GetTypeId", FxParticleColorTable_GetTypeId_Entry, nullptr);

// 0x004238c0 / 0x004238d0: class descriptor 0x005886a8 ("CLASSV(FxData)") and type id 0x80000007 (slot-1 pointer at
// 0x005b7df8). Not executed in recorded runs.
uint32_t FxData_GetClass()
{
    return 0x005886a8;
}
static uint32_t FxData_GetClass_Abi()
{
    return FxData_GetClass();
}
ABI_ENTRY_004238c0(FxData_GetClass_Entry, FxData_GetClass_Abi)
HOOK(0x004238c0, "FxData_GetClass", FxData_GetClass_Entry, nullptr);

uint32_t FxData_GetTypeId()
{
    return 0x80000007;
}
static uint32_t FxData_GetTypeId_Abi()
{
    return FxData_GetTypeId();
}
ABI_ENTRY_004238d0(FxData_GetTypeId_Entry, FxData_GetTypeId_Abi)
HOOK(0x004238d0, "FxData_GetTypeId", FxData_GetTypeId_Entry, nullptr);

// 0x004238e0 / 0x004238f0: class descriptor 0x005808d8 ("CLASSV(FxSystem)") and type id 0x80000002 (slot-1 pointer at
// 0x005b7e10). Not executed in recorded runs.
uint32_t FxSystem_GetClass()
{
    return 0x005808d8;
}
static uint32_t FxSystem_GetClass_Abi()
{
    return FxSystem_GetClass();
}
ABI_ENTRY_004238e0(FxSystem_GetClass_Entry, FxSystem_GetClass_Abi)
HOOK(0x004238e0, "FxSystem_GetClass", FxSystem_GetClass_Entry, nullptr);

uint32_t FxSystem_GetTypeId()
{
    return 0x80000002;
}
static uint32_t FxSystem_GetTypeId_Abi()
{
    return FxSystem_GetTypeId();
}
ABI_ENTRY_004238f0(FxSystem_GetTypeId_Entry, FxSystem_GetTypeId_Abi)
HOOK(0x004238f0, "FxSystem_GetTypeId", FxSystem_GetTypeId_Entry, nullptr);

// 0x00427210 (esi = character). Picks an animation/state request: virtual +0x19c (id, 1, 0, 0, 0, 0) with id 0x31
// when +0xab4 is set; else virtual +0x1a0 (2, 3, 0, 1, 1, 0, 0.5, 0.5) when +0x428's +0x1c has bits 0x10 and 8;
// else 0x59 when +0x484 bit 2; 0xc when +0x460 == 0x3e; 0xe when +0x570 and +0x488 bit 1; 0xb when +0x484 bit 0x20;
// else, unless +0x498 is 8..10, 8 + (rand() * 2 >> 15). Returns the virtual call's eax (or +0x498).
uint32_t fn_00427210(uint32_t obj)
{
    void* o = P(obj);
    if (D(obj + 0xab4))
        return VCall<uint32_t>(o, 0x19c / 4, 0x31u, 1u, 0u, 0u, 0u, 0u);
    const uint32_t a = D(obj + 0x428);
    if (a)
    {
        const uint32_t f = D(a + 0x1c);
        if ((f & 0x10) && (f & 8))
            return VCall<uint32_t>(o, 0x1a0 / 4, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    }
    const uint32_t flags = D(obj + 0x484);
    if (flags & 2)
        return VCall<uint32_t>(o, 0x19c / 4, 0x59u, 1u, 0u, 0u, 0u, 0u);
    if (D(obj + 0x460) == 0x3e)
        return VCall<uint32_t>(o, 0x19c / 4, 0xcu, 1u, 0u, 0u, 0u, 0u);
    if (D(obj + 0x570) && (Field<uint8_t>(o, 0x488) & 1))
        return VCall<uint32_t>(o, 0x19c / 4, 0xeu, 1u, 0u, 0u, 0u, 0u);
    if (flags & 0x20)
        return VCall<uint32_t>(o, 0x19c / 4, 0xbu, 1u, 0u, 0u, 0u, 0u);
    const uint32_t cur = D(obj + 0x498);
    if (cur >= 8 && cur <= 0xa)
        return cur;
    const int32_t r = GameRand();
    const uint32_t id = static_cast<uint32_t>(((r << 1) >> 0xf) + 8);
    return VCall<uint32_t>(o, 0x19c / 4, id, 1u, 0u, 0u, 0u, 0u);
}
static uint32_t fn_00427210_Abi(uint32_t esi)
{
    return fn_00427210(esi);
}
ABI_ENTRY_00427210(fn_00427210_Entry, fn_00427210_Abi)
HOOK(0x00427210, "fn_00427210", fn_00427210_Entry, nullptr);

// 0x00433f10 (stack: two arguments, not read; ret 8): returns false (al = 0; the upper eax bytes are as the caller
// left them: returned as 0 here). No direct callers (virtual); not executed in recorded runs.
uint32_t ReturnFalse2()
{
    return 0;
}
static uint32_t ReturnFalse2_Abi(uint32_t /*a0*/, uint32_t /*a1*/)
{
    return ReturnFalse2();
}
ABI_ENTRY_00433f10(ReturnFalse2_Entry, ReturnFalse2_Abi)
HOOK(0x00433f10, "ReturnFalse2", ReturnFalse2_Entry, nullptr);

// 0x00449120 (edx = 2D direction, esi = polygon; stack: one argument, not read; ret 4). Classifies the direction
// into one of eight octants and stores it at polygon +4: bit 2 = y < 0, bit 1 = x < 0, bit 0 from comparing the
// absolute components (x > y / -x > y / -y < x / -y < -x for the four quadrants). Returns the code.
uint32_t Dir2_Octant(const float* d, uint32_t poly)
{
    uint32_t q = (d[1] < Fl(0x0056c05c)) ? 1u : 0u;
    const uint32_t hi = q * 4;
    D(poly + 4) = hi;
    q = (d[0] < Fl(0x0056c05c)) ? 1u : 0u;
    const uint32_t code = q * 2 | hi;
    D(poly + 4) = code;
    uint32_t r;
    switch (code)
    {
    case 0:
        r = (d[0] > d[1]) ? 1u : 0u;
        break;
    case 2:
        r = ((-d[0] > d[1]) ? 1u : 0u) | 2u;
        break;
    case 4:
        r = ((-d[1] < d[0]) ? 1u : 0u) | 4u;
        break;
    case 6:
        r = ((-d[1] < -d[0]) ? 1u : 0u) | 6u;
        break;
    default:
        return code;
    }
    D(poly + 4) = r;
    return r;
}
static uint32_t Dir2_Octant_Abi(uint32_t edx, uint32_t esi, uint32_t /*a0*/)
{
    return Dir2_Octant(reinterpret_cast<const float*>(edx), esi);
}
ABI_ENTRY_00449120(Dir2_Octant_Entry, Dir2_Octant_Abi)
HOOK(0x00449120, "Dir2_Octant", Dir2_Octant_Entry, nullptr);

// 0x00449230 (eax = polygon buffer (two 0xc8-byte polygons); stack: view, planes (vec4 each), plane count; ret 0xc).
// Caller 0x0048eab0. Builds the screen rectangle (0,0) (W,0) (W,H) (0,H) from the view's unsigned sizes +8/+0xc,
// then clips it against each plane in turn (0x004e7f30, ping-ponging between the two polygons) until it is empty.
// Each plane is transformed into screen space first: (a, b, c) scaled by view +0x2c, d' = b*v1c + a*v14 + c*v24 + d,
// all four divided by the length of the scaled normal; the first four planes get d' += 0.5. Before each clip all
// vertex z are set to (v28 - v24) / v2c when the plane's c > 0, else 0. Finally the camera direction
// (Camera_GetDirection 0x0040fa10) classifies the result (Dir2_Octant). Returns the final polygon.
uint32_t Poly_ClipScreenRect(uint32_t out, uint32_t view, uint32_t planes, uint32_t count)
{
    // Original frame (esp+0x18 after the four pushes): [0] = loop index, [2..5] = the plane (later the direction).
    uint32_t frame[6] = {};
    D(out) = 4;
    D(out + 8) = 0;
    D(out + 0xc) = 0;
    auto ufl = [](uint32_t v) {
        float f = static_cast<float>(static_cast<int32_t>(v));
        if (static_cast<int32_t>(v) < 0)
            f = f + Fl(0x005bcb28);
        return f;
    };
    uint32_t ebx = out;
    Fl(out + 0x14) = ufl(D(view + 8));
    D(out + 0x18) = 0;
    Fl(out + 0x20) = ufl(D(view + 8));
    Fl(out + 0x24) = ufl(D(view + 0xc));
    D(out + 0x2c) = 0;
    Fl(out + 0x30) = ufl(D(view + 0xc));
    const float* v = reinterpret_cast<const float*>(view);
    uint32_t ptr = planes;
    for (int32_t i = 0; i < static_cast<int32_t>(count); ++i)
    {
        frame[0] = static_cast<uint32_t>(i);
        if (!D(ebx))
            break;
        float* L = reinterpret_cast<float*>(&frame[2]);
        L[0] = Fl(ptr);
        L[1] = Fl(ptr + 4);
        L[2] = Fl(ptr + 8);
        L[3] = Fl(ptr + 0xc);
        const float dist = L[1] * v[0x1c / 4] + L[0] * v[0x14 / 4] + L[2] * v[0x24 / 4] + L[3];
        const float A = L[0] * v[0x2c / 4];
        const float B = L[1] * v[0x2c / 4];
        const float C = L[2] * v[0x2c / 4];
        L[2] = C;
        const float inv = Fl(0x005bcb2c) / X87Sqrt(C * C + B * B + A * A);
        L[0] = inv * A;
        L[1] = inv * B;
        L[2] = inv * L[2];
        L[3] = inv * dist;
        if (i < 4)
            L[3] = L[3] + Fl(0x005bcb4c);
        const float z = (L[2] > Fl(0x0056c05c)) ? (v[0x28 / 4] - v[0x24 / 4]) / v[0x2c / 4] : Fl(0x0056c05c);
        for (int32_t k = 0; k < static_cast<int32_t>(D(ebx)); ++k)
            Fl(ebx + 0x10 + k * 0xc) = z;
        const uint32_t src = ebx;
        ebx = ((~static_cast<uint32_t>(i)) & 1) * 0xc8 + out;
        D(ebx) = orig::fn_004e7f30(D(src), src + 8, ebx + 8, u(L));
        ptr += 0x10;
    }
    orig::fn_0040fa10(u(&frame[2]), D(GameP() + 0xa68));
    frame[0] = frame[2];
    frame[1] = frame[3];
    Dir2_Octant(reinterpret_cast<const float*>(&frame[0]), ebx);
    return ebx;
}
static uint32_t Poly_ClipScreenRect_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Poly_ClipScreenRect(eax, a0, a1, a2);
}
ABI_ENTRY_00449230(Poly_ClipScreenRect_Entry, Poly_ClipScreenRect_Abi)
HOOK(0x00449230, "Poly_ClipScreenRect", Poly_ClipScreenRect_Entry, nullptr);

namespace
{
// QueryPerformanceCounter through the game's import slot 0x0056b08c (call ebx in the original).
void GameQpc(int64_t* t)
{
    reinterpret_cast<int(__stdcall*)(int64_t*)>(GameGlobal<uint32_t>(0x0056b08c))(t);
}
// The timing log lines of the level reset: (QPC now - start 0x00604ad8) / frequency 0x00604ac8 as a double.
void LogElapsed(uint32_t format)
{
    int64_t t;
    GameQpc(&t);
    const int64_t d = t - GameGlobal<int64_t>(0x00604ad8);
    const double s = static_cast<double>(d) / static_cast<double>(GameGlobal<int64_t>(0x00604ac8));
    GameLog(reinterpret_cast<const char*>(format), s);
}
}  // namespace

// 0x0044a670 (stack: level; ret 4). Caller 0x00403940 (level unload). Resets every game subsystem between levels,
// logging "START/END Audio reset", "START Misc reset", "END Shell reset", "START/END Havok step" with the elapsed
// time: renderer (+0x38) virtual +0x2c, Camera_Reset 0x0040e050 (+0xa68), 0x0044f4c0 (eax = +0xa88), audio (+0x2c)
// virtual +0x84, 0x00491020 (esi = +0xab4), +0x30 virtual +0x20, DecalManager_Clear (esi = +0xa7c), 0x004637d0
// (eax = +0xa70), 0x00406690 (eax = +0xa6c), 0x0045ad90 (eax = +0xa84), FxSystem_Unload 0x0041c9d0 (+0xa90),
// Streamers_Clear (esi = +0xa94), PostEffects_Clear (esi = +0xa9c), PostProcess_ReleaseTargets (esi = +0xa98),
// ScenePass_Release (eax = +0xa78), 0x0046e7c0 (esi = +0xaa4), 0x00448c70 (edi = +0xa60), 0x00495ad0, 0x00495ff0
// (esi = +0xac0), 0x0048e970 (esi = +0xaa0), 0x0048b740 (esi = +0xab8), PhysicsManager_Step (ecx = 1, edi = +0xa8c;
// 0), and the level manager's +0x460 (0x00447c70, then cleared). Finally level byte +0x11 = 0 and 0x101 dwords at
// level +0x5c = 0. Returns 0.
uint32_t Level_ResetSubsystems(uint32_t level)
{
    VCall<uint32_t>(P(D(GameP() + 0x38)), 0x2c / 4);
    orig::fn_0040e050(D(GameP() + 0xa68));
    LogElapsed(0x005b2500);
    orig::fn_0044f4c0(D(GameP() + 0xa88));
    VCall<uint32_t>(P(D(GameP() + 0x2c)), 0x84 / 4);
    LogElapsed(0x005b24e4);
    LogElapsed(0x005b24cc);
    orig::fn_00491020(D(GameP() + 0xab4));
    VCall<uint32_t>(P(D(GameP() + 0x30)), 0x20 / 4);
    orig::fn_004160d0(D(GameP() + 0xa7c));
    orig::fn_004637d0(D(GameP() + 0xa70));
    orig::fn_00406690(D(GameP() + 0xa6c), 0);
    orig::fn_0045ad90(D(GameP() + 0xa84));
    orig::fn_0041c9d0(D(GameP() + 0xa90));
    orig::fn_00494df0(D(GameP() + 0xa94));
    const uint32_t g = GameP();
    orig::fn_0047bd80(D(g + 0xa9c));
    orig::fn_0047ca60(D(g + 0xa98));
    orig::fn_00488060(D(GameP() + 0xa78));
    orig::fn_0046e7c0(D(GameP() + 0xaa4));
    orig::fn_00448c70(D(GameP() + 0xa60));
    orig::fn_00495ad0();
    orig::fn_00495ff0(D(GameP() + 0xac0));
    fn_0048e970(D(GameP() + 0xaa0));
    orig::fn_0048b740(D(GameP() + 0xab8));
    LogElapsed(0x005b24b0);
    LogElapsed(0x005b2498);
    orig::fn_00411d40(1, D(GameP() + 0xa8c), 0);
    const uint32_t obj = D(D(GameP() + 0xa64) + 0x460);
    if (obj)
    {
        orig::fn_00447c70(obj);
        D(D(GameP() + 0xa64) + 0x460) = 0;
    }
    LogElapsed(0x005b2480);
    Field<uint8_t>(P(level), 0x11) = 0;
    std::memset(P(level + 0x5c), 0, 0x101 * 4);
    return 0;
}
static uint32_t Level_ResetSubsystems_Abi(uint32_t a0)
{
    return Level_ResetSubsystems(a0);
}
ABI_ENTRY_0044a670(Level_ResetSubsystems_Entry, Level_ResetSubsystems_Abi)
HOOK(0x0044a670, "Level_ResetSubsystems", Level_ResetSubsystems_Entry, nullptr);

// 0x0044ad40 (eax = level manager): returns its +0x460.
uint32_t fn_0044ad40(uint32_t obj)
{
    return D(obj + 0x460);
}
static uint32_t fn_0044ad40_Abi(uint32_t eax)
{
    return fn_0044ad40(eax);
}
ABI_ENTRY_0044ad40(fn_0044ad40_Entry, fn_0044ad40_Abi)
HOOK(0x0044ad40, "fn_0044ad40", fn_0044ad40_Entry, nullptr);

// 0x0044ad50 (eax passed through): when the stream 0x0063a194 exists, 0x004e6b70 (eax, esi = stream). Returns its
// eax, else the input eax.
uint32_t fn_0044ad50(uint32_t eax)
{
    const uint32_t s = GameGlobal<uint32_t>(0x0063a194);
    if (!s)
        return eax;
    return orig::fn_004e6b70(eax, s);
}
static uint32_t fn_0044ad50_Abi(uint32_t eax)
{
    return fn_0044ad50(eax);
}
ABI_ENTRY_0044ad50(fn_0044ad50_Entry, fn_0044ad50_Abi)
HOOK(0x0044ad50, "fn_0044ad50", fn_0044ad50_Entry, nullptr);

// 0x0044ad70 (eax = string): case-insensitive path hash. For each character (A-Z folded to lower case, '/' to '\\'):
// h = rotl(h, s) + (signed char)c, with the rotation s starting at 0 and advancing by 7 (mod 32). Returns h.
uint32_t Str_HashPath(const char* s)
{
    uint32_t h = 0;
    uint32_t r = 0;
    char c = *s;
    while (c)
    {
        ++s;
        if (c >= 0x41 && c <= 0x5a)
            c = static_cast<char>(c ^ 0x20);
        else if (c == 0x2f)
            c = 0x5c;
        const uint32_t sh = r & 0x1f;
        const uint32_t rot = sh ? ((h << sh) | (h >> (32 - sh))) : h;
        r = (r + 7) & 0x1f;
        h = static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(c))) + rot;
        c = *s;
    }
    return h;
}
static uint32_t Str_HashPath_Abi(uint32_t eax)
{
    return Str_HashPath(reinterpret_cast<const char*>(eax));
}
ABI_ENTRY_0044ad70(Str_HashPath_Entry, Str_HashPath_Abi)
HOOK(0x0044ad70, "Str_HashPath", Str_HashPath_Entry, nullptr);

// 0x00453150 (ecx = object, esi = out vec3): the object's matrix (virtual +0x5c into a 0x44-byte local); out =
// -(m[1], m[5], m[9]) (the negated second column: guess, forward direction). Returns the virtual call's eax.
uint32_t Object_GetNegColumn1(uint32_t obj, float* out)
{
    float m[17];
    const uint32_t r = VCall<uint32_t>(P(obj), 0x5c / 4, m);
    out[0] = -m[1];
    out[1] = -m[5];
    out[2] = -m[9];
    return r;
}
static uint32_t Object_GetNegColumn1_Abi(uint32_t ecx, uint32_t esi)
{
    return Object_GetNegColumn1(ecx, reinterpret_cast<float*>(esi));
}
ABI_ENTRY_00453150(Object_GetNegColumn1_Entry, Object_GetNegColumn1_Abi)
HOOK(0x00453150, "Object_GetNegColumn1", Object_GetNegColumn1_Entry, nullptr);

// 0x004531b0 (ecx = object, esi = out vec3): as 0x00453150 with out = (m[0], m[4], m[8]) (the first column). Not
// executed in recorded runs.
uint32_t Object_GetColumn0(uint32_t obj, uint32_t* out)
{
    uint32_t m[17];
    const uint32_t r = VCall<uint32_t>(P(obj), 0x5c / 4, m);
    out[0] = m[0];
    out[1] = m[4];
    out[2] = m[8];
    return r;
}
static uint32_t Object_GetColumn0_Abi(uint32_t ecx, uint32_t esi)
{
    return Object_GetColumn0(ecx, reinterpret_cast<uint32_t*>(esi));
}
ABI_ENTRY_004531b0(Object_GetColumn0_Entry, Object_GetColumn0_Abi)
HOOK(0x004531b0, "Object_GetColumn0", Object_GetColumn0_Entry, nullptr);

// 0x004535b0 (eax = out vec3, ecx = object): copies the object's scale vector +0x37c (no result; eax preserved).
void Object_GetScale(uint32_t out, uint32_t obj)
{
    const uint32_t s = obj + 0x37c;
    D(out) = D(s);
    D(out + 4) = D(s + 4);
    D(out + 8) = D(s + 8);
}
static void Object_GetScale_Abi(uint32_t eax, uint32_t ecx)
{
    Object_GetScale(eax, ecx);
}
ABI_ENTRY_004535b0(Object_GetScale_Entry, Object_GetScale_Abi)
HOOK(0x004535b0, "Object_GetScale", Object_GetScale_Entry, nullptr);

// 0x00453b90 (eax = shape block or 0, edx = object, ebx = shape). No shape: true (al = 1). Without a block one is
// built on the stack: 0x44 bytes (not initialised by the original), an identity matrix, the shape and a pointer to a
// zeroed 0x150-byte area. Then 0x004ecd40 (object + 0x1d8, +0x1d4, block, 1, +0x410, +0x1d0, +0x2c8, 0) and returns
// whether it was non-zero (al; upper bytes from its eax). Deviation: the uninitialised 0x44 bytes are zero here.
uint32_t fn_00453b90(uint32_t block, uint32_t obj, uint32_t shape)
{
    if (!shape)
        return (block & 0xffffff00u) | 1u;
    uint32_t frame[0x78] = {};  // esp+8 .. esp+0x1e8 of the original frame
    if (!block)
    {
        uint32_t* m = frame + (0x4c - 8) / 4;
        m[0] = 0x3f800000;
        m[5] = 0x3f800000;
        m[10] = 0x3f800000;
        m[15] = 0x3f800000;
        frame[(0x90 - 8) / 4] = shape;
        frame[(0x94 - 8) / 4] = u(frame + (0x98 - 8) / 4);
        block = u(frame);
    }
    const uint32_t r = orig::fn_004ecd40(obj + 0x1d8, D(obj + 0x1d4), block, 1, D(obj + 0x410), D(obj + 0x1d0),
                                         D(obj + 0x2c8), 0);
    return (r & 0xffffff00u) | (r != 0 ? 1u : 0u);
}
static uint32_t fn_00453b90_Abi(uint32_t eax, uint32_t edx, uint32_t ebx)
{
    return fn_00453b90(eax, edx, ebx);
}
ABI_ENTRY_00453b90(fn_00453b90_Entry, fn_00453b90_Abi)
HOOK(0x00453b90, "fn_00453b90", fn_00453b90_Entry, nullptr);

namespace
{
// 0x004541a0 / 0x00454240 share one body; `k` is the descriptor offset of the scalar (+0x20 or +0x24).
uint32_t ObjectDescVec(uint32_t obj, uint32_t outVec, uint32_t outScalar, uint32_t transform, uint32_t k)
{
    float s[3];
    Object_GetScale(u(s), obj);
    Fl(outScalar) = s[0] * Fl(D(obj + 0x414) + k);
    if (static_cast<uint8_t>(transform))
    {
        float m[17];
        VCall<uint32_t>(P(obj), 0x64 / 4, m);
        return Mat_TransformVec3(D(obj + 0x414) + 0x14, u(m), 1, outVec);
    }
    Fl(outVec) = s[0] * Fl(D(obj + 0x414) + 0x14);
    Fl(outVec + 4) = s[1] * Fl(D(obj + 0x414) + 0x18);
    Fl(outVec + 8) = s[2] * Fl(D(obj + 0x414) + 0x1c);
    return outVec;
}
}  // namespace

// 0x004541a0 (ecx = object; stack: out vec3, out scalar, byte world; ret 0xc). Shape descriptor (+0x414) data scaled by
// the object's scale (+0x37c): scalar = scale.x * desc+0x20; vec = desc+0x14 componentwise times the scale, or, when
// world is set, desc+0x14 transformed by the object's matrix (virtual +0x64; Mat_TransformVec3, edx = 1). Returns the
// out vector (or Mat_TransformVec3's eax). No direct callers (virtual); not executed in recorded runs.
uint32_t ObjectDesc_GetVecA(uint32_t obj, uint32_t outVec, uint32_t outScalar, uint32_t world)
{
    return ObjectDescVec(obj, outVec, outScalar, world, 0x20);
}
static uint32_t ObjectDesc_GetVecA_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return ObjectDesc_GetVecA(ecx, a0, a1, a2);
}
ABI_ENTRY_004541a0(ObjectDesc_GetVecA_Entry, ObjectDesc_GetVecA_Abi)
HOOK(0x004541a0, "ObjectDesc_GetVecA", ObjectDesc_GetVecA_Entry, nullptr);

// 0x00454240 (ecx = object; stack: out vec3, out scalar, byte world; ret 0xc): as 0x004541a0 with the scalar from
// desc+0x24. Caller 0x004970e0.
uint32_t ObjectDesc_GetVecB(uint32_t obj, uint32_t outVec, uint32_t outScalar, uint32_t world)
{
    return ObjectDescVec(obj, outVec, outScalar, world, 0x24);
}
static uint32_t ObjectDesc_GetVecB_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return ObjectDesc_GetVecB(ecx, a0, a1, a2);
}
ABI_ENTRY_00454240(ObjectDesc_GetVecB_Entry, ObjectDesc_GetVecB_Abi)
HOOK(0x00454240, "ObjectDesc_GetVecB", ObjectDesc_GetVecB_Entry, nullptr);

// 0x004542e0 (ecx = object; stack: out a, out b, third argument not read; ret 0xc): a = scale * desc+0x28..+0x30,
// b = scale * desc+0x34..+0x3c (componentwise; desc = +0x414, scale = +0x37c). Returns b. No direct callers
// (virtual); not executed in recorded runs.
uint32_t ObjectDesc_GetBox(uint32_t obj, uint32_t a, uint32_t b)
{
    float s[3];
    Object_GetScale(u(s), obj);
    Fl(a) = s[0] * Fl(D(obj + 0x414) + 0x28);
    Fl(a + 4) = s[1] * Fl(D(obj + 0x414) + 0x2c);
    Fl(a + 8) = s[2] * Fl(D(obj + 0x414) + 0x30);
    Fl(b) = s[0] * Fl(D(obj + 0x414) + 0x34);
    Fl(b + 4) = s[1] * Fl(D(obj + 0x414) + 0x38);
    Fl(b + 8) = s[2] * Fl(D(obj + 0x414) + 0x3c);
    return b;
}
static uint32_t ObjectDesc_GetBox_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t /*a2*/)
{
    return ObjectDesc_GetBox(ecx, a0, a1);
}
ABI_ENTRY_004542e0(ObjectDesc_GetBox_Entry, ObjectDesc_GetBox_Abi)
HOOK(0x004542e0, "ObjectDesc_GetBox", ObjectDesc_GetBox_Entry, nullptr);

// 0x00455e70 (esi = object; stack: byte flatten, float limit; ret 8). Callers: many object updates. Reads the body's
// linear velocity (Body_getLinearVelocity 0x00458000, eax = object, ecx = local); flatten clears its z. When the 2D
// speed sqrt(y*y + x*x) exceeds the limit, the velocity divided by that speed (unit in xy; the limit itself is not
// applied) and the object's position (virtual +0x54) build a matrix (0x004e3070, edx = 0), which is applied with
// Object_SetTransform 0x00457990 (al = 0; object, matrix). Returns its eax (0 when nothing is done; the original
// leaves the FPU status there).
uint32_t Object_ClampSpeed(uint32_t obj, uint32_t flatten, uint32_t limitBits)
{
    uint32_t frame[0x1a] = {};  // esp .. esp+0x68: velocity, scaled velocity, position, matrix
    float* v = reinterpret_cast<float*>(&frame[0]);
    float* w = reinterpret_cast<float*>(&frame[3]);
    uint32_t* pos = &frame[6];
    uint32_t* m = &frame[9];
    orig::fn_00458000(obj, u(v));
    if (static_cast<uint8_t>(flatten))
        frame[2] = 0;
    const float len = X87Sqrt(v[1] * v[1] + v[0] * v[0]);
    if (!(len > u2f(limitBits)))
        return 0;
    const float inv = Fl(0x005bcb2c) / len;
    w[0] = v[0] * inv;
    w[1] = v[1] * inv;
    w[2] = inv * v[2];
    VCall<uint32_t>(P(obj), 0x54 / 4, pos);
    const uint32_t r = fn_004e3070(u(w), u(m), 0, u(pos));
    return orig::fn_00457990(r & 0xffffff00u, obj, u(m));
}
static uint32_t Object_ClampSpeed_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return Object_ClampSpeed(esi, a0, a1);
}
ABI_ENTRY_00455e70(Object_ClampSpeed_Entry, Object_ClampSpeed_Abi)
HOOK(0x00455e70, "Object_ClampSpeed", Object_ClampSpeed_Entry, nullptr);

// 0x00456110 (ecx = 2D vector, edi = out vec2; stack: object; ret 4). Returns the vector's length in st0. When it is
// above 0.1 (0x005bcbfc) out = the vector divided by it; otherwise out = the object's facing xy
// (Object_GetNegColumn1 0x00453150).
float Vec2_NormalizeOrFacing(const float* v, float* out, uint32_t obj)
{
    const float len = X87Sqrt(v[0] * v[0] + v[1] * v[1]);
    if (len > Fl(0x005bcbfc))
    {
        const float inv = Fl(0x005bcb2c) / len;
        out[0] = inv * v[0];
        out[1] = inv * v[1];
        return len;
    }
    float f[3];
    Object_GetNegColumn1(obj, f);
    out[0] = f[0];
    out[1] = f[1];
    return len;
}
static float Vec2_NormalizeOrFacing_Abi(uint32_t ecx, uint32_t edi, uint32_t a0)
{
    return Vec2_NormalizeOrFacing(reinterpret_cast<const float*>(ecx), reinterpret_cast<float*>(edi), a0);
}
ABI_ENTRY_00456110(Vec2_NormalizeOrFacing_Entry, Vec2_NormalizeOrFacing_Abi)
HOOK(0x00456110, "Vec2_NormalizeOrFacing", Vec2_NormalizeOrFacing_Entry, nullptr);

// 0x00461f00 / 0x00461f10 / 0x00461f20 / 0x00461f30 / 0x00461f50 / 0x00461fa0: class descriptor getters (vtable slot 1)
// returning 0x005853a8 "CLASS(Twiglet)", 0x005871f0 "CLASS(PickupObject)", 0x00584e50 "CLASS(GoliathObject)",
// 0x00585048 "CLASS(ZeppelinObject)", 0x00584ee0 "CLASS(GoliathFireball)", 0x00587a60 "CLASS(VindalooProjectile)"
// (the descriptor's +0x14). No direct callers (virtual); not executed in recorded runs.
uint32_t Twiglet_GetClass()
{
    return 0x005853a8;
}
static uint32_t Twiglet_GetClass_Abi()
{
    return Twiglet_GetClass();
}
ABI_ENTRY_00461f00(Twiglet_GetClass_Entry, Twiglet_GetClass_Abi)
HOOK(0x00461f00, "Twiglet_GetClass", Twiglet_GetClass_Entry, nullptr);

uint32_t PickupObject_GetClass()
{
    return 0x005871f0;
}
static uint32_t PickupObject_GetClass_Abi()
{
    return PickupObject_GetClass();
}
ABI_ENTRY_00461f10(PickupObject_GetClass_Entry, PickupObject_GetClass_Abi)
HOOK(0x00461f10, "PickupObject_GetClass", PickupObject_GetClass_Entry, nullptr);

uint32_t GoliathObject_GetClass()
{
    return 0x00584e50;
}
static uint32_t GoliathObject_GetClass_Abi()
{
    return GoliathObject_GetClass();
}
ABI_ENTRY_00461f20(GoliathObject_GetClass_Entry, GoliathObject_GetClass_Abi)
HOOK(0x00461f20, "GoliathObject_GetClass", GoliathObject_GetClass_Entry, nullptr);

uint32_t ZeppelinObject_GetClass()
{
    return 0x00585048;
}
static uint32_t ZeppelinObject_GetClass_Abi()
{
    return ZeppelinObject_GetClass();
}
ABI_ENTRY_00461f30(ZeppelinObject_GetClass_Entry, ZeppelinObject_GetClass_Abi)
HOOK(0x00461f30, "ZeppelinObject_GetClass", ZeppelinObject_GetClass_Entry, nullptr);

// 0x00461f40: returns 0x402 (a type id; vtable entry at 0x005af8b8). Not executed in recorded runs.
uint32_t fn_00461f40()
{
    return 0x402;
}
static uint32_t fn_00461f40_Abi()
{
    return fn_00461f40();
}
ABI_ENTRY_00461f40(fn_00461f40_Entry, fn_00461f40_Abi)
HOOK(0x00461f40, "fn_00461f40", fn_00461f40_Entry, nullptr);

uint32_t GoliathFireball_GetClass()
{
    return 0x00584ee0;
}
static uint32_t GoliathFireball_GetClass_Abi()
{
    return GoliathFireball_GetClass();
}
ABI_ENTRY_00461f50(GoliathFireball_GetClass_Entry, GoliathFireball_GetClass_Abi)
HOOK(0x00461f50, "GoliathFireball_GetClass", GoliathFireball_GetClass_Entry, nullptr);

// 0x00461f60 (stack: out vec3; ret 4): copies Game+0xa6c's vec3 +0x5c (0x00408030). Returns out (eax kept). A
// virtual in many vtables; not executed in recorded runs.
uint32_t fn_00461f60(uint32_t out)
{
    fn_00408030(out, D(GameP() + 0xa6c));
    return out;
}
static uint32_t fn_00461f60_Abi(uint32_t a0)
{
    return fn_00461f60(a0);
}
ABI_ENTRY_00461f60(fn_00461f60_Entry, fn_00461f60_Abi)
HOOK(0x00461f60, "fn_00461f60", fn_00461f60_Entry, nullptr);

// 0x00461f80: eax = Game+0xa6c, then a tail jump to Debug_Printf (a bare ret), so it returns Game+0xa6c. A virtual
// in many vtables; not executed in recorded runs.
uint32_t fn_00461f80()
{
    const uint32_t r = D(GameP() + 0xa6c);
    Debug_Printf();
    return r;
}
static uint32_t fn_00461f80_Abi()
{
    return fn_00461f80();
}
ABI_ENTRY_00461f80(fn_00461f80_Entry, fn_00461f80_Abi)
HOOK(0x00461f80, "fn_00461f80", fn_00461f80_Entry, nullptr);

// 0x00461f90: returns Game+0x2ca8 in st0. A virtual (vtable entry 0x005afdc4); not executed in recorded runs.
float fn_00461f90()
{
    return Fl(GameP() + 0x2ca8);
}
static float fn_00461f90_Abi()
{
    return fn_00461f90();
}
ABI_ENTRY_00461f90(fn_00461f90_Entry, fn_00461f90_Abi)
HOOK(0x00461f90, "fn_00461f90", fn_00461f90_Entry, nullptr);

uint32_t VindalooProjectile_GetClass()
{
    return 0x00587a60;
}
static uint32_t VindalooProjectile_GetClass_Abi()
{
    return VindalooProjectile_GetClass();
}
ABI_ENTRY_00461fa0(VindalooProjectile_GetClass_Entry, VindalooProjectile_GetClass_Abi)
HOOK(0x00461fa0, "VindalooProjectile_GetClass", VindalooProjectile_GetClass_Entry, nullptr);

// 0x00461fb0 (ecx = object): tail jump to the object's virtual +0x13c with the caller's arguments intact (the target's
// own `ret N` pops them). The argument count differs by class, so the forward is reproduced instruction for
// instruction by a hand-written naked stub (no generated stub exists: "no ret found"). A virtual in ~40 vtables; not
// executed in recorded runs.
static __declspec(naked) void Object_ForwardVirtual13c_Entry()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x13c]
    }
}
HOOK(0x00461fb0, "Object_ForwardVirtual13c", Object_ForwardVirtual13c_Entry, nullptr);

// 0x00461fc0 (eax = hash tree, edx = key): binary trie lookup ("Hash collision" / "Ran out of hash tree items" in
// the insert 0x00461ff0). From the root tree+4, each node's child is chosen by the next key bit (bit 0 first);
// returns the value (+0xc) of the node whose key (+8) matches, else 0.
uint32_t HashTree_Find(uint32_t tree, uint32_t key)
{
    uint32_t node = D(tree + 4);
    uint32_t bit = 0;
    while (node)
    {
        if (D(node + 8) == key)
            return D(node + 0xc);
        node = D(node + ((key >> (bit & 0x1f)) & 1) * 4);
        ++bit;
    }
    return 0;
}
static uint32_t HashTree_Find_Abi(uint32_t eax, uint32_t edx)
{
    return HashTree_Find(eax, edx);
}
ABI_ENTRY_00461fc0(HashTree_Find_Entry, HashTree_Find_Abi)
HOOK(0x00461fc0, "HashTree_Find", HashTree_Find_Entry, nullptr);

// 0x00461ff0 (edx = key, ebx = value, edi = hash tree): insert. Walks as HashTree_Find; a new node is taken from the
// free list tree+0 (none: logs "Ran out of hash tree items" 0x005a0ef8 and returns 0), linked in, children cleared,
// key and value stored. When the found node's value differs from ebx, logs "Hash collision" (0x005a0ee8). Returns the
// node.
uint32_t HashTree_Insert(uint32_t key, uint32_t value, uint32_t tree)
{
    uint32_t slot = tree + 4;
    uint32_t node = D(slot);
    uint32_t bit = 0;
    while (node)
    {
        if (D(node + 8) == key)
            break;
        slot = node + ((key >> (bit & 0x1f)) & 1) * 4;
        node = D(slot);
        ++bit;
    }
    if (!node)
    {
        node = D(tree);
        if (!node)
        {
            GameLog(reinterpret_cast<const char*>(0x005a0ef8));
            return 0;
        }
        D(slot) = node;
        D(tree) = D(D(tree));
        D(node + 4) = 0;
        D(node) = 0;
        D(node + 8) = key;
        D(node + 0xc) = value;
    }
    if (D(node + 0xc) != value)
        GameLog(reinterpret_cast<const char*>(0x005a0ee8));
    return node;
}
static uint32_t HashTree_Insert_Abi(uint32_t edx, uint32_t ebx, uint32_t edi)
{
    return HashTree_Insert(edx, ebx, edi);
}
ABI_ENTRY_00461ff0(HashTree_Insert_Entry, HashTree_Insert_Abi)
HOOK(0x00461ff0, "HashTree_Insert", HashTree_Insert_Entry, nullptr);

// 0x00462060 (esi = count; stack: nodes, &free list; ret 8). Caller 0x0050c6d0. Clears count 16-byte nodes, chains
// them (node+0 = next) and puts the chain in front of the free list. Returns &free list. Original quirk: count 0
// writes the old list head to nodes - 0x10.
uint32_t HashTree_AddNodes(uint32_t count, uint32_t nodes, uint32_t freeList)
{
    const uint32_t bytes = count << 4;
    std::memset(P(nodes), 0, bytes);
    if (count > 1)
    {
        uint32_t p = nodes + 0x10;
        for (uint32_t i = count - 1; i != 0; --i)
        {
            D(p - 0x10) = p;
            p += 0x10;
        }
    }
    D(bytes + nodes - 0x10) = D(freeList);
    D(freeList) = nodes;
    return freeList;
}
static uint32_t HashTree_AddNodes_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return HashTree_AddNodes(esi, a0, a1);
}
ABI_ENTRY_00462060(HashTree_AddNodes_Entry, HashTree_AddNodes_Abi)
HOOK(0x00462060, "HashTree_AddNodes", HashTree_AddNodes_Entry, nullptr);

// 0x004620b0 (eax = string): case-insensitive name hash (A-Z folded, '/' -> '\\'): h ^= (signed char)c << s, s += 5,
// minus 0x18 when above 0x18. Returns h.
uint32_t Str_HashName(const char* s)
{
    uint32_t h = 0;
    uint32_t sh = 0;
    char c = *s;
    while (c)
    {
        ++s;
        if (c >= 0x41 && c <= 0x5a)
            c = static_cast<char>(c ^ 0x20);
        else if (c == 0x2f)
            c = 0x5c;
        h ^= static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(c))) << (sh & 0x1f);
        sh += 5;
        if (sh > 0x18)
            sh -= 0x18;
        c = *s;
    }
    return h;
}
static uint32_t Str_HashName_Abi(uint32_t eax)
{
    return Str_HashName(reinterpret_cast<const char*>(eax));
}
ABI_ENTRY_004620b0(Str_HashName_Entry, Str_HashName_Abi)
HOOK(0x004620b0, "Str_HashName", Str_HashName_Entry, nullptr);

// 0x00462100 (eax = dword pointer): stores 0 there (no result; eax preserved). No direct callers (a callback).
void fn_00462100(uint32_t p)
{
    D(p) = 0;
}
static void fn_00462100_Abi(uint32_t eax)
{
    fn_00462100(eax);
}
ABI_ENTRY_00462100(fn_00462100_Entry, fn_00462100_Abi)
HOOK(0x00462100, "fn_00462100", fn_00462100_Entry, nullptr);

// 0x004626d0: returns 0x60. A virtual in ~16 vtables; not executed in recorded runs.
uint32_t Return0x60()
{
    return 0x60;
}
static uint32_t Return0x60_Abi()
{
    return Return0x60();
}
ABI_ENTRY_004626d0(Return0x60_Entry, Return0x60_Abi)
HOOK(0x004626d0, "Return0x60", Return0x60_Entry, nullptr);

// 0x0046d710 (eax = &list head, ecx = object): the first node of the circular list whose sentinel is object + 0x14,
// or 0 when the list is empty.
uint32_t List14_First(uint32_t head, uint32_t obj)
{
    const uint32_t first = D(head);
    return first == obj + 0x14 ? 0u : first;
}
static uint32_t List14_First_Abi(uint32_t eax, uint32_t ecx)
{
    return List14_First(eax, ecx);
}
ABI_ENTRY_0046d710(List14_First_Entry, List14_First_Abi)
HOOK(0x0046d710, "List14_First", List14_First_Entry, nullptr);

// 0x0046d700 (ecx = object or 0): 0 for no object, else List14_First (eax = object + 0x14, ecx = object; a tail
// jump). Not executed in recorded runs.
uint32_t List14_FirstOf(uint32_t obj)
{
    if (!obj)
        return 0;
    return List14_First(obj + 0x14, obj);
}
static uint32_t List14_FirstOf_Abi(uint32_t ecx)
{
    return List14_FirstOf(ecx);
}
ABI_ENTRY_0046d700(List14_FirstOf_Entry, List14_FirstOf_Abi)
HOOK(0x0046d700, "List14_FirstOf", List14_FirstOf_Entry, nullptr);

// 0x0046d720 (edx = object, esi = node): the node's index in the circular list at object + 0x14, or -1. Not executed
// in recorded runs.
uint32_t List14_IndexOf(uint32_t obj, uint32_t node)
{
    const uint32_t sentinel = obj + 0x14;
    uint32_t n = D(obj + 0x14);
    uint32_t i = 0;
    if (n != sentinel)
    {
        do
        {
            if (n == node)
                return i;
            n = D(n);
            ++i;
        } while (n != sentinel);
    }
    return 0xffffffffu;
}
static uint32_t List14_IndexOf_Abi(uint32_t edx, uint32_t esi)
{
    return List14_IndexOf(edx, esi);
}
ABI_ENTRY_0046d720(List14_IndexOf_Entry, List14_IndexOf_Abi)
HOOK(0x0046d720, "List14_IndexOf", List14_IndexOf_Entry, nullptr);

// 0x0046d740 (esi = object, edi = index; stack: byte also second list; caller pops). Caller 0x0048d540. The node at
// that index counting along the list +0x14, then (when the flag is set) on along the list +0xc; 0 when the index is
// -1 or out of range.
uint32_t List14_NodeAt(uint32_t obj, uint32_t index, uint32_t alsoSecond)
{
    if (index == 0xffffffffu)
        return 0;
    uint32_t i = 0;
    uint32_t sentinel = obj + 0x14;
    uint32_t n = D(obj + 0x14);
    if (n != sentinel)
    {
        do
        {
            if (i == index)
                return n;
            n = D(n);
            ++i;
        } while (n != sentinel);
    }
    if (!static_cast<uint8_t>(alsoSecond))
        return 0;
    sentinel = obj + 0xc;
    n = D(obj + 0xc);
    if (n == sentinel)
        return 0;
    do
    {
        if (i == index)
            return n;
        n = D(n);
        ++i;
    } while (n != sentinel);
    return 0;
}
static uint32_t List14_NodeAt_Abi(uint32_t esi, uint32_t edi, uint32_t a0)
{
    return List14_NodeAt(esi, edi, a0);
}
ABI_ENTRY_0046d740(List14_NodeAt_Entry, List14_NodeAt_Abi)
HOOK(0x0046d740, "List14_NodeAt", List14_NodeAt_Entry, nullptr);

// 0x0046e2c0 (ecx only reserves a stack slot; stack: lo, hi; caller pops): random float lo + rand() * (hi - lo) /
// 32768, returned in st0 (order: (rand * (hi - lo)) * 1/32768 (0x005bcbc0) + lo).
float RandRange(uint32_t loBits, uint32_t hiBits)
{
    const float lo = u2f(loBits);
    const float hi = u2f(hiBits);
    const int r = GameRand();
    return static_cast<float>(r) * (hi - lo) * Fl(0x005bcbc0) + lo;
}
static float RandRange_Abi(uint32_t /*ecx*/, uint32_t a0, uint32_t a1)
{
    return RandRange(a0, a1);
}
ABI_ENTRY_0046e2c0(RandRange_Entry, RandRange_Abi)
HOOK(0x0046e2c0, "RandRange", RandRange_Entry, nullptr);

// 0x00471750 (eax = a, ecx = b, edx = out vec3; stack: dist, speed, num; caller pops). d = a - b; f = speed, or, when
// dist != 0 and |d| > dist, dist / |d| * speed. f == 0: out = 0 and returns 0.0; else out = d * f and returns
// num / f (st0).
float Vec3_ScaledDelta(const float* a, const float* b, float* out, uint32_t distBits, uint32_t speedBits,
                       uint32_t numBits)
{
    float d[3];
    d[0] = a[0] - b[0];
    d[1] = a[1] - b[1];
    d[2] = a[2] - b[2];
    const float dist = u2f(distBits);
    float f = u2f(speedBits);
    if (!(dist == Fl(0x0056c05c)))
    {
        const float len = X87Sqrt(d[2] * d[2] + d[1] * d[1] + d[0] * d[0]);
        if (len > dist)
            f = dist / len * u2f(speedBits);
    }
    if (f == Fl(0x0056c05c))
    {
        out[2] = 0.0f;
        out[1] = 0.0f;
        out[0] = 0.0f;
        return Fl(0x0056c05c);
    }
    out[0] = d[0] * f;
    out[1] = d[1] * f;
    out[2] = d[2] * f;
    return u2f(numBits) / f;
}
static float Vec3_ScaledDelta_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Vec3_ScaledDelta(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx),
                            reinterpret_cast<float*>(edx), a0, a1, a2);
}
ABI_ENTRY_00471750(Vec3_ScaledDelta_Entry, Vec3_ScaledDelta_Abi)
HOOK(0x00471750, "Vec3_ScaledDelta", Vec3_ScaledDelta_Entry, nullptr);

// 0x00471970 (eax = from, ecx = out vec3, edx = to, esi = out value or 0; stack: min, max, factor, threshold; caller
// pops). out = to - from; len = |out|. When len > threshold: v = clamp(factor * len, min, max), out *= v / len and
// *esi = v (when given). Returns len in st0 (the ABI table lists eax; callers pop st0).
float Vec3_DeltaClamped(const float* from, float* out, const float* to, float* value, uint32_t minBits,
                        uint32_t maxBits, uint32_t factorBits, uint32_t thresholdBits)
{
    out[0] = to[0] - from[0];
    out[1] = to[1] - from[1];
    out[2] = to[2] - from[2];
    const float z = out[2];
    const float len = X87Sqrt(out[0] * out[0] + out[1] * out[1] + z * z);
    if (!(len > u2f(thresholdBits)))
        return len;
    float v = u2f(factorBits) * len;
    if (v < u2f(minBits))
        v = u2f(minBits);
    if (v > u2f(maxBits))
        v = u2f(maxBits);
    const float k = v / len;
    out[0] = k * out[0];
    out[1] = k * out[1];
    out[2] = k * out[2];
    if (value)
        *value = v;
    return len;
}
static float Vec3_DeltaClamped_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0, uint32_t a1,
                                   uint32_t a2, uint32_t a3)
{
    return Vec3_DeltaClamped(reinterpret_cast<const float*>(eax), reinterpret_cast<float*>(ecx),
                             reinterpret_cast<const float*>(edx), reinterpret_cast<float*>(esi), a0, a1, a2, a3);
}
ABI_ENTRY_00471970(Vec3_DeltaClamped_Entry, Vec3_DeltaClamped_Abi)
HOOK(0x00471970, "Vec3_DeltaClamped", Vec3_DeltaClamped_Entry, nullptr);

// 0x00471a00 (ebx = out velocity, esi = target, edi = origin; stack: speed, gravity, byte keep; caller pops).
// Ballistic launch velocity. Horizontal: the normalised xy delta (0x004e7780) times speed; t = |delta xy| / speed;
// vz = (dz - t*t*g*0.5) / t. Unless keep: with disc = sqrt(vz*vz - dz*g*-2.0) (0 when 2g - dz >= vz*vz), the
// roots t1 = (disc - vz) / g and t2 = (-vz - disc) / g; the larger positive one, when it exceeds t, replaces the
// horizontal speed (xy = dir * |delta xy| / root; vz is left as computed with t). Returns the keep byte (eax upper
// bytes as left by the original are not reproduced).
uint32_t Ballistic_LaunchVelocity(float* out, const float* target, const float* origin, uint32_t speedBits,
                                  uint32_t gravityBits, uint32_t keep)
{
    float frame[5] = {};  // esp .. esp+0x14: dz, horizontal distance, normalised dx, dy, (unused)
    float a0 = u2f(speedBits);
    float a1 = u2f(gravityBits);
    float a2 = 0.0f;
    frame[2] = target[0] - origin[0];
    frame[3] = target[1] - origin[1];
    orig::fn_004e7780(u(&frame[2]), u(&frame[2]));
    out[2] = 0.0f;
    out[0] = frame[2] * a0;
    out[1] = frame[3] * a0;
    const float dy = target[1] - origin[1];
    const float dx = target[0] - origin[0];
    const float h = X87Sqrt(dy * dy + dx * dx);
    frame[1] = h;
    a0 = h / a0;  // t, stored over the speed argument
    frame[0] = target[2] - origin[2];
    const float vz = (frame[0] - a0 * a0 * a1 * Fl(0x005bcb4c)) / a0;
    out[2] = vz;
    if (static_cast<uint8_t>(keep))
        return static_cast<uint8_t>(keep);
    const float lim = a1 + a1 - frame[0];
    float disc;
    if (lim < vz * vz)
        disc = X87Sqrt(vz * vz - frame[0] * a1 * Fl(0x005bcc40));
    else
        disc = Fl(0x0056c05c);
    const float ig = Fl(0x005bcb2c) / a1;
    a2 = (disc - vz) * ig;
    a1 = (-vz - disc) * ig;
    float tt = a0;
    if (a2 > Fl(0x0056c05c) && a2 > a1)
        tt = a2;
    else if (a1 > Fl(0x0056c05c) && a1 > a2)
        tt = a1;
    if (!(tt > a0))
        return 0;
    const float k = frame[1] / tt;
    out[0] = frame[2] * k;
    out[1] = k * frame[3];
    return 0;
}
static uint32_t Ballistic_LaunchVelocity_Abi(uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1,
                                             uint32_t a2)
{
    return Ballistic_LaunchVelocity(reinterpret_cast<float*>(ebx), reinterpret_cast<const float*>(esi),
                                    reinterpret_cast<const float*>(edi), a0, a1, a2);
}
ABI_ENTRY_00471a00(Ballistic_LaunchVelocity_Entry, Ballistic_LaunchVelocity_Abi)
HOOK(0x00471a00, "Ballistic_LaunchVelocity", Ballistic_LaunchVelocity_Entry, nullptr);

// 0x0047dbe0: returns 0x8000000a, slot 0 of the PostEffects vtable 0x0059e52c (guess: its type id, as the
// 0x8000000x ids of 0x004238b0 / 0x004238d0). Not executed in recorded runs.
uint32_t PostEffects_GetTypeId()
{
    return 0x8000000a;
}
static uint32_t PostEffects_GetTypeId_Abi()
{
    return PostEffects_GetTypeId();
}
ABI_ENTRY_0047dbe0(PostEffects_GetTypeId_Entry, PostEffects_GetTypeId_Abi)
HOOK(0x0047dbe0, "PostEffects_GetTypeId", PostEffects_GetTypeId_Entry, nullptr);

// Template node pools (0x0048e770 "Template allocate"): 0x1c-byte nodes on intrusive lists whose +0 points at the
// previous link field and +4 is the next node. 0x0063a170 = allocated blocks, 0x0063a16c = free nodes, 0x0063a174 =
// user count.

// 0x0048e800: frees every allocated block (unlinking each from 0x0063a170, PMS_heapFree) and empties the free list
// 0x0063a16c. Returns 0 (eax = the empty list head).
uint32_t TemplatePool_FreeAll()
{
    uint32_t n = GameGlobal<uint32_t>(0x0063a170);
    while (n)
    {
        D(D(n)) = D(n + 4);
        if (D(n + 4))
            D(D(n + 4)) = D(n);
        orig::fn_004e08d0(kHeap, n);
        n = GameGlobal<uint32_t>(0x0063a170);
    }
    GameGlobal<uint32_t>(0x0063a16c) = 0;
    return 0;
}
static uint32_t TemplatePool_FreeAll_Abi()
{
    return TemplatePool_FreeAll();
}
ABI_ENTRY_0048e800(TemplatePool_FreeAll_Entry, TemplatePool_FreeAll_Abi)
HOOK(0x0048e800, "TemplatePool_FreeAll", TemplatePool_FreeAll_Entry, nullptr);

// 0x0048e760: drops the pool user count 0x0063a174; at 0 tail-jumps to TemplatePool_FreeAll. Otherwise eax is the
// caller's (returned as 0 here; the only caller, 0x0048e970, ignores it).
uint32_t TemplatePool_Release()
{
    if (--GameGlobal<int32_t>(0x0063a174) == 0)
        return TemplatePool_FreeAll();
    return 0;
}
static uint32_t TemplatePool_Release_Abi()
{
    return TemplatePool_Release();
}
ABI_ENTRY_0048e760(TemplatePool_Release_Entry, TemplatePool_Release_Abi)
HOOK(0x0048e760, "TemplatePool_Release", TemplatePool_Release_Entry, nullptr);

// 0x0048e770 (ebx = node count). Caller 0x0048f0d0. Allocates count 0x1c-byte nodes in one zeroed block (PMS_heapAlloc
// 0x004e07f0: 3, count * 0x1c, 2, "Template allocate" 0x00590390; no null check), puts the first node on the block
// list 0x0063a170 and the others on the free list 0x0063a16c. Returns the free list head.
uint32_t TemplatePool_Allocate(uint32_t count)
{
    const uint32_t size = count * 0x1c;
    uint32_t node = orig::fn_004e07f0(kHeap, 3, size, 2, 0x00590390);
    std::memset(P(node), 0, size);
    auto push = [](uint32_t head, uint32_t n) {
        D(n) = head;
        D(n + 4) = D(head);
        D(head) = n;
        if (D(n + 4))
            D(D(n + 4)) = n + 4;
    };
    push(0x0063a170, node);
    node += 0x1c;
    if (count > 1)
    {
        for (uint32_t i = count - 1; i != 0; --i)
        {
            push(0x0063a16c, node);
            node += 0x1c;
        }
    }
    return GameGlobal<uint32_t>(0x0063a16c);
}
static uint32_t TemplatePool_Allocate_Abi(uint32_t ebx)
{
    return TemplatePool_Allocate(ebx);
}
ABI_ENTRY_0048e770(TemplatePool_Allocate_Entry, TemplatePool_Allocate_Abi)
HOOK(0x0048e770, "TemplatePool_Allocate", TemplatePool_Allocate_Entry, nullptr);

// 0x0048e850 (edx = block): clears 16 dwords; returns the block. Caller Game_Init 0x004011e0.
uint32_t Clear16Dwords(uint32_t p)
{
    std::memset(P(p), 0, 0x10 * 4);
    return p;
}
static uint32_t Clear16Dwords_Abi(uint32_t edx)
{
    return Clear16Dwords(edx);
}
ABI_ENTRY_0048e850(Clear16Dwords_Entry, Clear16Dwords_Abi)
HOOK(0x0048e850, "Clear16Dwords", Clear16Dwords_Entry, nullptr);

// 0x0048e970 (esi = template table). Callers Level_ResetSubsystems 0x0044a670 and 0x0048e870. When the bucket array
// +0x38 exists: for each of the +0x2c buckets (8 bytes, list head at +4) every node is unlinked from the bucket list
// (+0/+4), pushed on the free list 0x0063a16c, and unlinked from its second list (+8 = previous link, +0xc = next;
// the next node's +8 is updated); the array is freed and cleared and the pool released (TemplatePool_Release). Then
// the object +0x3c, if any, is deleted (virtual +0x10 (1)) and cleared. Returns eax as left.
uint32_t fn_0048e970(uint32_t tbl)
{
    uint32_t r = 0;
    if (D(tbl + 0x38))
    {
        for (uint32_t i = 0; i < D(tbl + 0x2c); ++i)
        {
            uint32_t n = D(D(tbl + 0x38) + i * 8 + 4);
            while (n)
            {
                D(D(n)) = D(n + 4);
                if (D(n + 4))
                    D(D(n + 4)) = D(n);
                D(n) = 0x0063a16c;
                D(n + 4) = GameGlobal<uint32_t>(0x0063a16c);
                GameGlobal<uint32_t>(0x0063a16c) = n;
                if (D(n + 4))
                    D(D(n + 4)) = n + 4;
                D(D(n + 8)) = D(n + 0xc);
                if (D(n + 0xc))
                    D(D(n + 0xc) + 8) = D(n + 8);
                n = D(D(tbl + 0x38) + i * 8 + 4);
            }
        }
        const uint32_t a = D(tbl + 0x38);
        if (a)
        {
            orig::fn_004e08d0(kHeap, a);
            D(tbl + 0x38) = 0;
        }
        r = TemplatePool_Release();
    }
    const uint32_t o = D(tbl + 0x3c);
    if (o)
    {
        r = VCall<uint32_t>(P(o), 0x10 / 4, 1u);
        D(tbl + 0x3c) = 0;
    }
    return r;
}
static uint32_t fn_0048e970_Abi(uint32_t esi)
{
    return fn_0048e970(esi);
}
ABI_ENTRY_0048e970(fn_0048e970_Entry, fn_0048e970_Abi)
HOOK(0x0048e970, "fn_0048e970", fn_0048e970_Entry, nullptr);

// 0x0048f9a0 (ecx = object; stack: one argument; ret 4): object +0xb58 = 7, then a tail jump to 0x0042f2f0 (ecx =
// object, same argument; it pops it). A virtual; not executed in recorded runs.
uint32_t fn_0048f9a0(uint32_t obj, uint32_t a0)
{
    D(obj + 0xb58) = 7;
    return orig::fn_0042f2f0(obj, a0);
}
static uint32_t fn_0048f9a0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048f9a0(ecx, a0);
}
ABI_ENTRY_0048f9a0(fn_0048f9a0_Entry, fn_0048f9a0_Abi)
HOOK(0x0048f9a0, "fn_0048f9a0", fn_0048f9a0_Entry, nullptr);

// 0x0048f9b0 (ecx = object; stack: one argument; ret 4): object +0xb58 = 6, then a tail jump to 0x0042f1d0 (ecx =
// object, same argument). A virtual; not executed in recorded runs.
uint32_t fn_0048f9b0(uint32_t obj, uint32_t a0)
{
    D(obj + 0xb58) = 6;
    return orig::fn_0042f1d0(obj, a0);
}
static uint32_t fn_0048f9b0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0048f9b0(ecx, a0);
}
ABI_ENTRY_0048f9b0(fn_0048f9b0_Entry, fn_0048f9b0_Abi)
HOOK(0x0048f9b0, "fn_0048f9b0", fn_0048f9b0_Entry, nullptr);

// 0x00493b70 (ecx only reserves a stack slot, ebx = count, edi = sound id array; stack: &used, out handle, a2, byte
// a3, a4, filter callback; caller pops). Picks a random sound and plays it on the audio object (Game+0x2c). No sounds:
// returns eax as left (0 here). With more than one: when used >= count it restarts at 0, else entry count-used is
// swapped to the front; then min(3, (count-used+1)/2) times the front is swapped with entry (rand() * (count - used))
// >> 15 (when non-zero). used += 1. The callback (id, &out, &a2, &a3, &a4), when given, may reject the pick (al = 0)
// and the selection repeats. Plays: a3 set -> virtual +0xd0 (id, a2, 0, 0, -1, 0); else a4 -> +0xcc (id, a4, a2, 0, 0,
// -1, 0); else +0xc4 (id, a2, 0, 0, -1, 0). *out = the result when out is set. Returns the result.
uint32_t Audio_PlayRandomSound(uint32_t count, uint32_t* ids, uint32_t* used, uint32_t out, uint32_t a2, uint32_t a3,
                               uint32_t a4, uint32_t filter)
{
    for (;;)
    {
        if (!count)
            return 0;
        if (count > 1)
        {
            const uint32_t e = *used;
            if (e >= count)
                *used = 0;
            else if (e > 0)
            {
                const uint32_t t = ids[count - e];
                const uint32_t f = ids[0];
                ids[0] = t;
                ids[count - e] = f;
            }
            uint32_t n = count - *used;
            if (n > 1)
            {
                n = (n + 1) >> 1;
                if (n > 3)
                    n = 3;
                for (uint32_t s = n; s != 0; --s)
                {
                    const uint32_t range = count - *used - 1 + 1;
                    const int32_t r = GameRand();
                    const uint32_t k = static_cast<uint32_t>(static_cast<int32_t>(r * static_cast<int32_t>(range)) >> 0xf);
                    if (k)
                    {
                        const uint32_t t = ids[k];
                        const uint32_t f = ids[0];
                        ids[0] = t;
                        ids[k] = f;
                    }
                }
            }
        }
        ++*used;
        if (!filter)
            break;
        const uint32_t ok = reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*)>(
            filter)(ids[0], &out, &a2, &a3, &a4);
        if (static_cast<uint8_t>(ok))
            break;
    }
    void* audio = P(D(GameP() + 0x2c));
    uint32_t r;
    if (static_cast<uint8_t>(a3))
        r = VCall<uint32_t>(audio, 0xd0 / 4, ids[0], a2, 0u, 0u, 0xffffffffu, 0u);
    else if (a4)
        r = VCall<uint32_t>(audio, 0xcc / 4, ids[0], a4, a2, 0u, 0u, 0xffffffffu, 0u);
    else
        r = VCall<uint32_t>(audio, 0xc4 / 4, ids[0], a2, 0u, 0u, 0xffffffffu, 0u);
    if (out)
        D(out) = r;
    return r;
}
static uint32_t Audio_PlayRandomSound_Abi(uint32_t /*ecx*/, uint32_t ebx, uint32_t edi, uint32_t a0, uint32_t a1,
                                          uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return Audio_PlayRandomSound(ebx, reinterpret_cast<uint32_t*>(edi), reinterpret_cast<uint32_t*>(a0), a1, a2, a3,
                                 a4, a5);
}
ABI_ENTRY_00493b70(Audio_PlayRandomSound_Entry, Audio_PlayRandomSound_Abi)
HOOK(0x00493b70, "Audio_PlayRandomSound", Audio_PlayRandomSound_Entry, nullptr);

// 0x00495a20 (eax = vec4, ecx = out vec4): multiplies by the constant 4x4 matrix at 0x0057e7b0 (rows of four floats),
// each row summed in the order m0*x + m3*w + m1*y + m2*z. Caller 0x00495380. No result (eax preserved).
void Vec4_MulConstMatrix(const float* v, float* out)
{
    const float* m = reinterpret_cast<const float*>(0x0057e7b0);
    for (int i = 0; i < 4; ++i)
    {
        const float* r = m + i * 4;
        out[i] = r[0] * v[0] + r[3] * v[3] + r[1] * v[1] + r[2] * v[2];
    }
}
static void Vec4_MulConstMatrix_Abi(uint32_t eax, uint32_t ecx)
{
    Vec4_MulConstMatrix(reinterpret_cast<const float*>(eax), reinterpret_cast<float*>(ecx));
}
ABI_ENTRY_00495a20(Vec4_MulConstMatrix_Entry, Vec4_MulConstMatrix_Abi)
HOOK(0x00495a20, "Vec4_MulConstMatrix", Vec4_MulConstMatrix_Entry, nullptr);

// 0x004dff60 (stack: app, a1, a2, a3 (not read), a4; ret 0x14). The application base constructor (caller Game_Init
// 0x004011e0, which passes its eax as a4). Clears bytes +0x25/+0x26 and dwords +8, +0x18..+0x20, +0x28..+0x38,
// +0x60, byte +0x24 = 1, vtable 0x0058b6f0, +4 = a4. The working directory (_getcwd 0x0053a9b9, 0x104 bytes) becomes
// the path +0x70 (_makepath 0x0053859e (path, 0, dir, 0, 0)), copied to 0x00638d28 and 0x00638c28, whose addresses
// go to 0x0063a204 / 0x0063a208. Returns app. (The original's SEH frame and security cookie are not reproduced.)
uint32_t AppBase_Construct(uint32_t app, uint32_t a4)
{
    char buf[0x108] = {};
    Field<uint8_t>(P(app), 0x25) = 0;
    Field<uint8_t>(P(app), 0x26) = 0;
    D(app + 8) = 0;
    D(app + 0x28) = 0;
    D(app + 0x2c) = 0;
    D(app + 0x30) = 0;
    D(app + 0x34) = 0;
    D(app + 0x38) = 0;
    D(app + 0x60) = 0;
    D(app + 0x1c) = 0;
    D(app + 0x20) = 0;
    D(app + 0x18) = 0;
    Field<uint8_t>(P(app), 0x24) = 1;
    D(app) = 0x0058b6f0;
    D(app + 4) = a4;
    GameFunction<char*(__cdecl*)(char*, int)>(0x0053a9b9)(buf, 0x104);
    char* path = reinterpret_cast<char*>(app + 0x70);
    GameFunction<void(__cdecl*)(char*, const char*, const char*, const char*, const char*)>(0x0053859e)(
        path, nullptr, buf, nullptr, nullptr);
    std::strcpy(reinterpret_cast<char*>(0x00638d28), path);
    GameGlobal<uint32_t>(0x0063a204) = 0x00638d28;
    std::strcpy(reinterpret_cast<char*>(0x00638c28), path);
    GameGlobal<uint32_t>(0x0063a208) = 0x00638c28;
    return app;
}
static uint32_t AppBase_Construct_Abi(uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/, uint32_t a4)
{
    return AppBase_Construct(a0, a4);
}
ABI_ENTRY_004dff60(AppBase_Construct_Entry, AppBase_Construct_Abi)
HOOK(0x004dff60, "AppBase_Construct", AppBase_Construct_Entry, nullptr);

uint32_t fn_004e3f00(uint32_t app);

// 0x004e0070 (eax = app). The application base destructor body (caller Game_Destruct 0x00401710): vtable 0x0058b6f0,
// DestroyWindow(+0x64) through the import slot 0x0056b244, then 0x004e3f00 (esi = app). Returns its eax. Not executed
// in recorded runs.
uint32_t AppBase_Destruct(uint32_t app)
{
    D(app) = 0x0058b6f0;
    reinterpret_cast<int(__stdcall*)(uint32_t)>(GameGlobal<uint32_t>(0x0056b244))(D(app + 0x64));
    return fn_004e3f00(app);
}
static uint32_t AppBase_Destruct_Abi(uint32_t eax)
{
    return AppBase_Destruct(eax);
}
ABI_ENTRY_004e0070(AppBase_Destruct_Entry, AppBase_Destruct_Abi)
HOOK(0x004e0070, "AppBase_Destruct", AppBase_Destruct_Entry, nullptr);

// 0x004e0090 (ecx = app; stack: byte active; ret 4). When active differs from the global 0x005cad48: virtual +0x24
// (active == 0; the pushed ecx keeps the app pointer's upper bytes), 0x005cad48 = active, and the object +0x30 (if
// any) gets virtual +0x28 (active, 1). Returns eax as left (0 when nothing changes). A virtual; not executed in
// recorded runs.
uint32_t AppBase_SetActive(uint32_t app, uint32_t active)
{
    const uint8_t a = static_cast<uint8_t>(active);
    if (a == GameGlobal<uint8_t>(0x005cad48))
        return GameGlobal<uint8_t>(0x005cad48);
    uint32_t r = VCall<uint32_t>(P(app), 0x24 / 4, (app & 0xffffff00u) | (a == 0 ? 1u : 0u));
    const uint32_t o = D(app + 0x30);
    GameGlobal<uint8_t>(0x005cad48) = a;
    if (o)
        r = VCall<uint32_t>(P(o), 0x28 / 4, active, 1u);
    return r;
}
static uint32_t AppBase_SetActive_Abi(uint32_t ecx, uint32_t a0)
{
    return AppBase_SetActive(ecx, a0);
}
ABI_ENTRY_004e0090(AppBase_SetActive_Entry, AppBase_SetActive_Abi)
HOOK(0x004e0090, "AppBase_SetActive", AppBase_SetActive_Entry, nullptr);

// 0x004e00d0 (al = force inactive, ecx = app; stack: byte; ret 4). Caller 0x004e0110. v = 0 when forced, else 1 when
// the argument is set or app +0x14 has bit 8. When v differs from 0x005cad48 the argument's low byte becomes v and
// the call tail-jumps to virtual +0x20 (it pops the argument). Returns that eax, else al = v.
uint32_t AppBase_UpdateActive(uint32_t eax, uint32_t app, uint32_t arg)
{
    uint8_t v;
    if (static_cast<uint8_t>(eax))
        v = 0;
    else if (static_cast<uint8_t>(arg) || (Field<uint8_t>(P(app), 0x14) & 8))
        v = 1;
    else
        v = 0;
    if (v == GameGlobal<uint8_t>(0x005cad48))
        return (eax & 0xffffff00u) | v;
    return VCall<uint32_t>(P(app), 0x20 / 4, (arg & 0xffffff00u) | v);
}
static uint32_t AppBase_UpdateActive_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return AppBase_UpdateActive(eax, ecx, a0);
}
ABI_ENTRY_004e00d0(AppBase_UpdateActive_Entry, AppBase_UpdateActive_Abi)
HOOK(0x004e00d0, "AppBase_UpdateActive", AppBase_UpdateActive_Entry, nullptr);

namespace
{
// user32 imports through the game's import slots (so the hook DLL's import hooks still apply).
template <class Fn>
Fn Import(uint32_t slot)
{
    return reinterpret_cast<Fn>(GameGlobal<uint32_t>(slot));
}
using StdFn1 = uint32_t(__stdcall*)(uint32_t);
using StdFn2 = uint32_t(__stdcall*)(uint32_t, uint32_t);
using StdFn3 = uint32_t(__stdcall*)(uint32_t, uint32_t, uint32_t);
using StdFn4 = uint32_t(__stdcall*)(uint32_t, uint32_t, uint32_t, uint32_t);
using StdFn12 = uint32_t(__stdcall*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                     uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t DefWindowProcGame(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp)
{
    return Import<StdFn4>(0x0056b1e8)(hwnd, msg, wp, lp);  // DefWindowProcA
}
}  // namespace

// 0x004e0110 App_WndProc (stdcall hwnd, msg, wparam, lparam; ret 0x10): the window procedure registered by
// 0x004e0300. app = GetWindowLongA(hwnd, GWL_USERDATA). WM_MOVE: app+0x30 virtual +0x28 (1, 1). WM_SIZE: 0x0064a70c =
// minimised (wparam 1 or 4), then AppBase_UpdateActive (al = that; 0x005fb918). WM_ACTIVATE: 0x005fb918 = (LOWORD
// wparam != 0), AppBase_UpdateActive (al = 0x0064a70c; that). WM_CLOSE: PostQuitMessage(0) unless app+0x34's byte
// +0x208 is set; returns 0. WM_ACTIVATEAPP: as AppBase_UpdateActive inlined (virtual +0x20 with the new state). WM_SETCURSOR:
// renderer app+0x38 virtual +0x160 (0) non-zero -> returns 1. WM_GETMINMAXINFO: min track size = app +0xc/+0x10 when
// app +0x14 bit 0x10, else 300 x 300. WM_SYSCOMMAND: SC_MOVE / 0xf090 / 0xf130 -> as WM_SETCURSOR; SC_CLOSE -> this
// procedure with WM_CLOSE first; 0xf150 and anything below 0xf090 other than those -> DefWindowProc, others return 1.
// Everything else (msg != 0) goes to DefWindowProcA; returns its result.
uint32_t App_WndProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp)
{
    const uint32_t app = Import<StdFn2>(0x0056b240)(hwnd, 0xffffffebu);  // GetWindowLongA(hwnd, -21)
    bool force = false;  // reach DefWindowProc without the msg != 0 test
    auto renderQuery = [&]() -> bool {
        const uint32_t r = D(app + 0x38);
        if (!r)
            return false;
        return VCall<uint32_t>(P(r), 0x160 / 4, 0u) != 0;
    };
    if (msg > 0x1c)
    {
        if (msg == 0x20)
        {
            if (renderQuery())
                return 1;
            force = true;
        }
        else if (msg == 0x24)
        {
            if (app && (Field<uint8_t>(P(app), 0x14) & 0x10))
            {
                D(lp + 0x18) = D(app + 0xc);
                D(lp + 0x1c) = D(app + 0x10);
            }
            else
            {
                D(lp + 0x18) = 0x12c;
                D(lp + 0x1c) = 0x12c;
            }
            force = true;
        }
        else if (msg == 0x112)
        {
            if (wp > 0xf090)
            {
                if (wp == 0xf130)
                {
                    if (renderQuery())
                        return 1;
                }
                else if (wp != 0xf150)
                    return 1;
            }
            else if (wp == 0xf090 || wp == 0xf010)
            {
                if (renderQuery())
                    return 1;
            }
            else if (wp == 0xf060)
                App_WndProc(hwnd, 0x10, 0xf060, lp);
            else
                return 1;
            force = true;
        }
    }
    else if (msg == 0x1c)
    {
        const uint8_t minimised = GameGlobal<uint8_t>(0x0064a70c);
        const uint8_t on = static_cast<uint16_t>(wp) != 0 ? 1 : 0;
        GameGlobal<uint8_t>(0x005fb918) = on;
        uint8_t v;
        if (minimised)
            v = 0;
        else if (on || (Field<uint8_t>(P(app), 0x14) & 8))
            v = 1;
        else
            v = 0;
        if (v != GameGlobal<uint8_t>(0x005cad48))
            VCall<uint32_t>(P(app), 0x20 / 4, (msg & 0xffffff00u) | v);
        force = true;
    }
    else if (msg >= 3 && msg - 3 <= 0xd)
    {
        switch (msg)
        {
        case 3:  // WM_MOVE
        {
            const uint32_t o = D(app + 0x30);
            if (o)
                VCall<uint32_t>(P(o), 0x28 / 4, 1u, 1u);
            break;
        }
        case 5:  // WM_SIZE
        {
            const uint8_t m = (static_cast<uint16_t>(wp) == 4 || static_cast<uint16_t>(wp) == 1) ? 1 : 0;
            GameGlobal<uint8_t>(0x0064a70c) = m;
            AppBase_UpdateActive(m, app, GameGlobal<uint8_t>(0x005fb918));
            break;
        }
        case 6:  // WM_ACTIVATE
        {
            const uint8_t on = static_cast<uint16_t>(wp) != 0 ? 1 : 0;
            GameGlobal<uint8_t>(0x005fb918) = on;
            AppBase_UpdateActive(GameGlobal<uint8_t>(0x0064a70c), app, on);
            break;
        }
        case 0x10:  // WM_CLOSE
            if (!Field<uint8_t>(P(D(app + 0x34)), 0x208))
                Import<StdFn1>(0x0056b25c)(0);  // PostQuitMessage
            return 0;
        default:
            break;
        }
    }
    if (!force && msg == 0)
        return 0;
    return DefWindowProcGame(hwnd, msg, wp, lp);
}
static uint32_t App_WndProc_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return App_WndProc(a0, a1, a2, a3);
}
ABI_ENTRY_004e0110(App_WndProc_Entry, App_WndProc_Abi)
HOOK(0x004e0110, "App_WndProc", App_WndProc_Entry, nullptr);

// 0x004e0300 (esi = app, edi = start-up parameters {hInstance, nCmdShow}; stack: window title; ret 4). Caller
// 0x004e03e0. Registers the window class "AppWinClass" (0x0058b740; procedure App_WndProc 0x004e0110, arrow cursor,
// black brush, menu "PCAppMenu" 0x0058b74c), creates the window (CreateWindowExA(WS_EX_APPWINDOW, class, title,
// 0x06cf0000, CW_USEDEFAULT x4, 0, 0, hInstance, 0)) into app+0x64, stores app as its GWL_USERDATA, shows and updates
// it, hides the cursor, loads the accelerators "AppAccel" (0x0058b734) into +0x68 and keeps hInstance in +0x6c.
// Returns the accelerator handle (eax as left); RegisterClassA failing returns its 0.
uint32_t App_CreateWindow(uint32_t app, uint32_t params, uint32_t title)
{
    uint32_t wc[10] = {};  // WNDCLASSA
    wc[0] = 0;
    wc[1] = 0x004e0110;
    wc[2] = 0;
    wc[3] = 0;
    wc[4] = D(params);
    wc[5] = 0;
    wc[6] = Import<StdFn2>(0x0056b1ec)(0, 0x7f00);  // LoadCursorA(0, IDC_ARROW)
    wc[7] = Import<StdFn1>(0x0056b05c)(4);          // GetStockObject(BLACK_BRUSH)
    wc[8] = 0x0058b74c;
    wc[9] = 0x0058b740;
    const uint32_t atom = Import<StdFn1>(0x0056b1f0)(u(wc));  // RegisterClassA
    if (!static_cast<uint16_t>(atom))
        return atom;
    const uint32_t hwnd = Import<StdFn12>(0x0056b1f4)(0x40000, 0x0058b740, title, 0x06cf0000, 0x80000000u,
                                                      0x80000000u, 0x80000000u, 0x80000000u, 0, 0, D(params), 0);
    D(app + 0x64) = hwnd;
    Import<StdFn3>(0x0056b1f8)(hwnd, 0xffffffebu, app);         // SetWindowLongA
    Import<StdFn2>(0x0056b1fc)(D(app + 0x64), D(params + 4));   // ShowWindow
    Import<StdFn1>(0x0056b200)(D(app + 0x64));                  // UpdateWindow
    Import<StdFn1>(0x0056b204)(0);                              // ShowCursor(FALSE)
    D(app + 0x68) = Import<StdFn2>(0x0056b208)(D(params), 0x0058b734);  // LoadAcceleratorsA
    D(app + 0x6c) = D(params);
    return D(app + 0x68);
}
static uint32_t App_CreateWindow_Abi(uint32_t esi, uint32_t edi, uint32_t a0)
{
    return App_CreateWindow(esi, edi, a0);
}
ABI_ENTRY_004e0300(App_CreateWindow_Entry, App_CreateWindow_Abi)
HOOK(0x004e0300, "App_CreateWindow", App_CreateWindow_Entry, nullptr);

// 0x004e03e0 (ecx = app; stack: init block, window title, start-up parameters; ret 0xc). A virtual (no direct
// callers). When block+4 bit 0: App_CreateWindow (esi = app, edi = parameters; title), the shared renderer 0x0063a0c4
// (created on first use by Renderer_Create 0x004fffe0 (0, hwnd, block +8, +0xc, +0x10, bit 1 of block+4, 1)) with its
// count 0x0063a0c8 += 1 (unless creation failed), app+0x38 = renderer, virtuals +0xc and +0x10. When byte block+0x4c:
// the input object 0x004f7b30 into app+0x30, its virtual +4 (block+0x50, 4, 8, 0, 1, byte block+0x54, &app+0x64) and
// +0x28 (1, 1). byte app+0x27 = 1; when byte block+0x1c: the sound system (AudioPC_Acquire 0x004f9c10) into app+0x2c
// and app+0x27 = its virtual +0x74 (block +0x20..+0x44, hwnd). Then 0x004fba30 into app+0x28 with its +8 = renderer
// and +0xc = sound system. Returns that object.
uint32_t App_InitSystems(uint32_t app, uint32_t block, uint32_t title, uint32_t params)
{
    if (Field<uint8_t>(P(block), 4) & 1)
    {
        App_CreateWindow(app, params, title);
        const uint32_t windowed = (D(block + 4) >> 1) & 1;
        uint32_t r = GameGlobal<uint32_t>(0x0063a0c4);
        if (!r)
        {
            r = orig::fn_004fffe0(0, D(app + 0x64), D(block + 8), D(block + 0xc), D(block + 0x10), windowed, 1);
            GameGlobal<uint32_t>(0x0063a0c4) = r;
            if (r)
                ++GameGlobal<uint32_t>(0x0063a0c8);
        }
        else
            ++GameGlobal<uint32_t>(0x0063a0c8);
        D(app + 0x38) = r;
        VCall<uint32_t>(P(app), 0xc / 4);
        VCall<uint32_t>(P(app), 0x10 / 4);
    }
    if (Field<uint8_t>(P(block), 0x4c))
    {
        const uint32_t in = orig::fn_004f7b30();
        D(app + 0x30) = in;
        VCall<uint32_t>(P(in), 4 / 4, D(block + 0x50), 4u, 8u, 0u, 1u,
                        static_cast<uint32_t>(Field<uint8_t>(P(block), 0x54)), app + 0x64);
        VCall<uint32_t>(P(D(app + 0x30)), 0x28 / 4, 1u, 1u);
    }
    Field<uint8_t>(P(app), 0x27) = 1;
    if (Field<uint8_t>(P(block), 0x1c))
    {
        const uint32_t snd = orig::fn_004f9c10(0);
        D(app + 0x2c) = snd;
        const uint32_t ok = VCall<uint32_t>(P(snd), 0x74 / 4, D(block + 0x20), D(block + 0x24), D(block + 0x28),
                                            D(block + 0x2c), D(block + 0x30), D(block + 0x34), D(block + 0x38),
                                            D(block + 0x3c), D(block + 0x40), D(block + 0x44), D(app + 0x64));
        Field<uint8_t>(P(app), 0x27) = static_cast<uint8_t>(ok);
    }
    const uint32_t t = orig::fn_004fba30();
    D(app + 0x28) = t;
    D(t + 8) = D(app + 0x38);
    D(t + 0xc) = D(app + 0x2c);
    return t;
}
static uint32_t App_InitSystems_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return App_InitSystems(ecx, a0, a1, a2);
}
ABI_ENTRY_004e03e0(App_InitSystems_Entry, App_InitSystems_Abi)
HOOK(0x004e03e0, "App_InitSystems", App_InitSystems_Entry, nullptr);

// 0x004e04f0 App_MessageLoop (ecx = app). Until WM_QUIT (0x12): a pending quit request (byte app+0x26) posts
// PostQuitMessage(0); when the application is active (0x005cad48) PeekMessageA(PM_REMOVE) else GetMessageA; a
// message goes through TranslateAcceleratorA(app+0x64, app+0x68) or TranslateMessage + DispatchMessageA; no
// message runs a frame (virtual +0x30). Returns the last call's eax. A virtual; not executed in recorded runs (the
// recordings start the game through another path).
uint32_t App_MessageLoop(uint32_t app)
{
    uint32_t msg[7] = {};  // MSG
    msg[1] = 0xa;
    uint32_t r = 0;
    do
    {
        if (Field<uint8_t>(P(app), 0x26))
        {
            Field<uint8_t>(P(app), 0x26) = 0;
            Import<StdFn1>(0x0056b25c)(0);
        }
        if (GameGlobal<uint8_t>(0x005cad48))
            r = reinterpret_cast<uint32_t(__stdcall*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
                GameGlobal<uint32_t>(0x0056b20c))(u(msg), 0, 0, 0, 1);
        else
            r = Import<StdFn4>(0x0056b210)(u(msg), 0, 0, 0);
        if (r)
        {
            r = Import<StdFn3>(0x0056b214)(D(app + 0x64), D(app + 0x68), u(msg));
            if (!r)
            {
                Import<StdFn1>(0x0056b218)(u(msg));
                r = Import<StdFn1>(0x0056b21c)(u(msg));
            }
        }
        else
            r = VCall<uint32_t>(P(app), 0x30 / 4);
    } while (msg[1] != 0x12);
    return r;
}
static uint32_t App_MessageLoop_Abi(uint32_t ecx)
{
    return App_MessageLoop(ecx);
}
ABI_ENTRY_004e04f0(App_MessageLoop_Entry, App_MessageLoop_Abi)
HOOK(0x004e04f0, "App_MessageLoop", App_MessageLoop_Entry, nullptr);

// 0x004e05a0 (stack: block, size, alignment shift; ret 0xc). Allocates size + (1 << shift) - 1 bytes with malloc
// (0x0053aaa6): block+0xc = that size, +4 = the raw pointer, +8 = it rounded up to the alignment. Returns the aligned
// pointer. No direct callers; not executed in recorded runs.
uint32_t AlignedBlock_Alloc(uint32_t block, uint32_t size, uint32_t shift)
{
    const uint32_t align = 1u << (shift & 0x1f);
    const uint32_t total = align + size - 1;
    D(block + 0xc) = total;
    const uint32_t p = GameFunction<uint32_t(__cdecl*)(uint32_t)>(0x0053aaa6)(total);
    D(block + 4) = p;
    const uint32_t a = (p + (align - 1)) & ~(align - 1);
    D(block + 8) = a;
    return a;
}
static uint32_t AlignedBlock_Alloc_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return AlignedBlock_Alloc(a0, a1, a2);
}
ABI_ENTRY_004e05a0(AlignedBlock_Alloc_Entry, AlignedBlock_Alloc_Abi)
HOOK(0x004e05a0, "AlignedBlock_Alloc", AlignedBlock_Alloc_Entry, nullptr);

// 0x004e05e0 (stack: block; ret 4): free (0x0053aab8) of the raw pointer block+4. Returns its eax. No direct callers;
// not executed in recorded runs.
uint32_t AlignedBlock_Free(uint32_t block)
{
    return GameFunction<uint32_t(__cdecl*)(uint32_t)>(0x0053aab8)(D(block + 4));
}
static uint32_t AlignedBlock_Free_Abi(uint32_t a0)
{
    return AlignedBlock_Free(a0);
}
ABI_ENTRY_004e05e0(AlignedBlock_Free_Entry, AlignedBlock_Free_Abi)
HOOK(0x004e05e0, "AlignedBlock_Free", AlignedBlock_Free_Entry, nullptr);

// PMS heap pools: a heap keeps two singly linked pool lists, +4 (normal) and +8 (pool flag byte +0x1e bit 1), each
// pool sorted by its element size +0x14; a pool's extra chunks hang off +4 (chained by +4) and are freed to the heap
// +0xc; flag bit 2 means the pool memory itself is not owned.

// 0x004e0790 (esi = pool, edi = heap). Callers 0x004e6c40, 0x004e7060. Destroys a pool: unlinks it from its heap list,
// frees its chunks (PMS_heapFree 0x004e08d0, ecx = pool +0xc) and, unless flag bit 2, the pool itself (ecx = heap).
// Returns eax as left by the last free (0 when nothing was freed).
uint32_t PmsPool_Destroy(uint32_t pool, uint32_t heap)
{
    if (!pool)
        return 0;
    uint32_t link = (Field<uint8_t>(P(pool), 0x1e) & 1) ? heap + 8 : heap + 4;
    if (D(link))
    {
        do
        {
            const uint32_t n = D(link);
            if (n == pool)
                break;
            link = n;
        } while (D(link));
        if (D(link))
            D(link) = D(pool);
    }
    uint32_t r = 0;
    for (uint32_t c = D(pool + 4); c; c = D(pool + 4))
    {
        D(pool + 4) = D(c + 4);
        r = orig::fn_004e08d0(D(pool + 0xc), c);
    }
    if (!(Field<uint8_t>(P(pool), 0x1e) & 2))
        r = orig::fn_004e08d0(heap, pool);
    return r;
}
static uint32_t PmsPool_Destroy_Abi(uint32_t esi, uint32_t edi)
{
    return PmsPool_Destroy(esi, edi);
}
ABI_ENTRY_004e0790(PmsPool_Destroy_Entry, PmsPool_Destroy_Abi)
HOOK(0x004e0790, "PmsPool_Destroy", PmsPool_Destroy_Entry, nullptr);

// 0x004e0600 (stack: heap; ret 4). Caller 0x004e0a10. Destroys every pool of the heap (list +4, then list +8; the
// inlined PmsPool_Destroy), then frees the blocks of the large-block list +0xc (each block's +8) until it is empty.
// Not executed in recorded runs.
uint32_t PmsHeap_DestroyPools(uint32_t heap)
{
    uint32_t r = 0;
    for (uint32_t p = D(heap + 4); p; p = D(heap + 4))
        r = PmsPool_Destroy(p, heap);
    for (uint32_t p = D(heap + 8); p; p = D(heap + 8))
        r = PmsPool_Destroy(p, heap);
    for (uint32_t b = D(heap + 0xc); b; b = D(heap + 0xc))
        r = orig::fn_004e08d0(heap, D(b + 8));
    return r;
}
static uint32_t PmsHeap_DestroyPools_Abi(uint32_t a0)
{
    return PmsHeap_DestroyPools(a0);
}
ABI_ENTRY_004e0600(PmsHeap_DestroyPools_Entry, PmsHeap_DestroyPools_Abi)
HOOK(0x004e0600, "PmsHeap_DestroyPools", PmsHeap_DestroyPools_Entry, nullptr);

// 0x004e06f0 (edx = element count; stack: heap, tag, element size, alignment shift, a4; ret 0x14). Creates a pool:
// element size rounded up to the alignment; the block is 0x10000 bytes for count 0, else the aligned 0x24-byte header
// plus count elements rounded up to 4 KB. PMS_heapAlloc (ecx = heap; 0x19, bytes, 0xc, tag), initialised by
// 0x004f60e0 (eax = block, edx = shift; heap, element size, bytes, a4, tag) and inserted in the heap list (+8 when its
// flag bit 1, else +4) before the first pool with an element size >= this one. Returns the pool.
uint32_t PmsPool_Create(uint32_t count, uint32_t heap, uint32_t tag, uint32_t elemSize, uint32_t shift, uint32_t a4)
{
    const uint32_t mask = (1u << (shift & 0x1f)) - 1;
    const uint32_t size = (mask + elemSize) & ~mask;
    uint32_t bytes;
    if (!count)
        bytes = 0x10000;
    else
        bytes = (((mask + 0x24) & ~mask) + size * count + 0xfff) & 0xfffff000u;
    const uint32_t pool = orig::fn_004e07f0(heap, 0x19, bytes, 0xc, tag);
    orig::fn_004f60e0(pool, shift, heap, size, bytes, a4, tag);
    uint32_t link = (Field<uint8_t>(P(pool), 0x1e) & 1) ? heap + 8 : heap + 4;
    while (D(link) && D(D(link) + 0x14) < size)
        link = D(link);
    D(pool) = D(link);
    D(link) = pool;
    return pool;
}
static uint32_t PmsPool_Create_Abi(uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return PmsPool_Create(edx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_004e06f0(PmsPool_Create_Entry, PmsPool_Create_Abi)
HOOK(0x004e06f0, "PmsPool_Create", PmsPool_Create_Entry, nullptr);
