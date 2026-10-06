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
uint32_t Mat_TransformPoints(uint32_t src, uint32_t mat, uint32_t count, uint32_t dst);
uint32_t Matrix_LookAtNeg(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi);
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
// (0, -1, 0) rotated by the z rotation {cos, -sin; sin, cos} (Mat_TransformPoints 0x004e36a0, edx = 1) and normalised
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
    Mat_TransformPoints(at(0x18), at(0x30), 1, at(0xc));
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
        return Mat_TransformPoints(D(obj + 0x414) + 0x14, u(m), 1, outVec);
    }
    Fl(outVec) = s[0] * Fl(D(obj + 0x414) + 0x14);
    Fl(outVec + 4) = s[1] * Fl(D(obj + 0x414) + 0x18);
    Fl(outVec + 8) = s[2] * Fl(D(obj + 0x414) + 0x1c);
    return outVec;
}
}  // namespace

// 0x004541a0 (ecx = object; stack: out vec3, out scalar, byte world; ret 0xc). Shape descriptor (+0x414) data scaled by
// the object's scale (+0x37c): scalar = scale.x * desc+0x20; vec = desc+0x14 componentwise times the scale, or, when
// world is set, desc+0x14 transformed by the object's matrix (virtual +0x64; Mat_TransformPoints, edx = 1). Returns the
// out vector (or Mat_TransformPoints's eax). No direct callers (virtual); not executed in recorded runs.
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
    const uint32_t r = Matrix_LookAtNeg(u(w), u(m), 0, u(pos));
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
float Rand_FloatRange(uint32_t loBits, uint32_t hiBits)
{
    const float lo = u2f(loBits);
    const float hi = u2f(hiBits);
    const int r = GameRand();
    return static_cast<float>(r) * (hi - lo) * Fl(0x005bcbc0) + lo;
}
static float Rand_FloatRange_Abi(uint32_t /*ecx*/, uint32_t a0, uint32_t a1)
{
    return Rand_FloatRange(a0, a1);
}
ABI_ENTRY_0046e2c0(Rand_FloatRange_Entry, Rand_FloatRange_Abi)
HOOK(0x0046e2c0, "Rand_FloatRange", Rand_FloatRange_Entry, nullptr);

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

uint32_t AppBase_Cleanup(uint32_t app);

// 0x004e0070 (eax = app). The application base destructor body (caller Game_Destruct 0x00401710): vtable 0x0058b6f0,
// DestroyWindow(+0x64) through the import slot 0x0056b244, then 0x004e3f00 (esi = app). Returns its eax. Not executed
// in recorded runs.
uint32_t AppBase_Destruct(uint32_t app)
{
    D(app) = 0x0058b6f0;
    reinterpret_cast<int(__stdcall*)(uint32_t)>(GameGlobal<uint32_t>(0x0056b244))(D(app + 0x64));
    return AppBase_Cleanup(app);
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

// 0x004e0950 (stack: memory object; ret 4). The PMS memory constructor (no direct callers: reached through a pointer;
// executed in recorded runs). Clears +4..+0xc, vtable 0x0058b5d4, clears 0x1d dwords at +0x10018 and +0x10014,
// +0x10010 = the embedded allocation pool +0x10, initialised by 0x004f60e0 (eax = +0x10, edx = 5; object, 0x10,
// 0x10000, 3, "PMS_Memory::m_pAllocPool"), then creates the "Default memory pool" (0x0058b630) pools of 16, 32, 64,
// 128 and 256-byte elements (PmsPool_Create, count 0; object, tag, 1 << n, n, 4 for n = 4..8). Returns the object.
// (The original's SEH frame is not reproduced.)
uint32_t PmsMemory_Construct(uint32_t mem)
{
    D(mem + 4) = 0;
    D(mem + 8) = 0;
    D(mem + 0xc) = 0;
    D(mem) = 0x0058b5d4;
    std::memset(P(mem + 0x10018), 0, 0x1d * 4);
    D(mem + 0x10014) = 0;
    D(mem + 0x10010) = mem + 0x10;
    orig::fn_004f60e0(mem + 0x10, 5, mem, 0x10, 0x10000, 3, 0x0058b644);
    for (uint32_t n = 4; n <= 8; ++n)
        PmsPool_Create(0, mem, 0x0058b630, 1u << n, n, 4);
    return mem;
}
static uint32_t PmsMemory_Construct_Abi(uint32_t a0)
{
    return PmsMemory_Construct(a0);
}
ABI_ENTRY_004e0950(PmsMemory_Construct_Entry, PmsMemory_Construct_Abi)
HOOK(0x004e0950, "PmsMemory_Construct", PmsMemory_Construct_Entry, nullptr);

// 0x004e0a10 (stack: memory object; ret 4). The PMS memory destructor: PmsHeap_DestroyPools, vtable 0x00589bbc, then
// PmsHeap_DestroyPools again. Returns its eax. No direct callers; not executed in recorded runs.
uint32_t PmsMemory_Destruct(uint32_t mem)
{
    PmsHeap_DestroyPools(mem);
    D(mem) = 0x00589bbc;
    return PmsHeap_DestroyPools(mem);
}
static uint32_t PmsMemory_Destruct_Abi(uint32_t a0)
{
    return PmsMemory_Destruct(a0);
}
ABI_ENTRY_004e0a10(PmsMemory_Destruct_Entry, PmsMemory_Destruct_Abi)
HOOK(0x004e0a10, "PmsMemory_Destruct", PmsMemory_Destruct_Entry, nullptr);

// 0x004e0a60 (eax = memory object). Caller PMS_heapAlloc 0x004e07f0. Takes a node from the allocation pool +0x10010:
// when its free list (+8) has no second entry and the re-entry guard 0x0064a70b is clear, the pool is grown first
// (0x004f6150, guarded); when the list is empty it is grown unguarded. Returns the node unlinked from the list.
uint32_t PmsMemory_AllocNode(uint32_t mem)
{
    const uint32_t pool = D(mem + 0x10010);
    if (D(D(pool + 8)) == 0 && !GameGlobal<uint8_t>(0x0064a70b))
    {
        GameGlobal<uint8_t>(0x0064a70b) = 1;
        orig::fn_004f6150(pool);
        GameGlobal<uint8_t>(0x0064a70b) = 0;
    }
    const uint32_t p = D(mem + 0x10010);
    if (!D(p + 8))
        orig::fn_004f6150(p);
    const uint32_t node = D(p + 8);
    D(p + 8) = D(node);
    return node;
}
static uint32_t PmsMemory_AllocNode_Abi(uint32_t eax)
{
    return PmsMemory_AllocNode(eax);
}
ABI_ENTRY_004e0a60(PmsMemory_AllocNode_Entry, PmsMemory_AllocNode_Abi)
HOOK(0x004e0a60, "PmsMemory_AllocNode", PmsMemory_AllocNode_Entry, nullptr);

// 0x004e0ab0 (esi = memory object). Caller 0x00403940. 0x004f6290 (ebx = pool) for every pool of the lists +4 and +8,
// then for the allocation pool +0x10010. Returns its eax.
uint32_t PmsMemory_ForEachPool(uint32_t mem)
{
    for (uint32_t p = D(mem + 4); p; p = D(p))
        orig::fn_004f6290(p);
    for (uint32_t p = D(mem + 8); p; p = D(p))
        orig::fn_004f6290(p);
    return orig::fn_004f6290(D(mem + 0x10010));
}
static uint32_t PmsMemory_ForEachPool_Abi(uint32_t esi)
{
    return PmsMemory_ForEachPool(esi);
}
ABI_ENTRY_004e0ab0(PmsMemory_ForEachPool_Entry, PmsMemory_ForEachPool_Abi)
HOOK(0x004e0ab0, "PmsMemory_ForEachPool", PmsMemory_ForEachPool_Entry, nullptr);

// 0x004e0af0 (stack: size; caller pops): allocates from the game heap (PMS_heapAlloc, ecx = 0x0063a210; 0x10, size,
// 2, "new" 0x0058b5e8). Callers: compiler-generated code at 0x0054607e.. (operator new, guess).
uint32_t GameHeap_Alloc(uint32_t size)
{
    return orig::fn_004e07f0(kHeap, 0x10, size, 2, 0x0058b5e8);
}
static uint32_t GameHeap_Alloc_Abi(uint32_t a0)
{
    return GameHeap_Alloc(a0);
}
ABI_ENTRY_004e0af0(GameHeap_Alloc_Entry, GameHeap_Alloc_Abi)
HOOK(0x004e0af0, "GameHeap_Alloc", GameHeap_Alloc_Entry, nullptr);

// 0x004e0b20: returns the string "virtual" (0x0058b5cc); slot 0 of the PMS memory vtable 0x0058b5d4 (whose slots 1
// and 2 are AlignedBlock_Alloc / AlignedBlock_Free). Not executed in recorded runs.
uint32_t PmsMemory_GetKind()
{
    return 0x0058b5cc;
}
static uint32_t PmsMemory_GetKind_Abi()
{
    return PmsMemory_GetKind();
}
ABI_ENTRY_004e0b20(PmsMemory_GetKind_Entry, PmsMemory_GetKind_Abi)
HOOK(0x004e0b20, "PmsMemory_GetKind", PmsMemory_GetKind_Entry, nullptr);

// 0x004e0bd0 Log_Flush. Unless logging is suppressed (0x0063a104 > 0) or the buffer 0x00600210 is empty (write pointer
// 0x005cacd0 at its start): OutputDebugStringA(buffer), the callback 0x0063a10c (buffer) unless 0x0063a108 is set,
// fprintf(0x0063a110, "%s" 0x005bbb70, buffer) when that file is open and fprintf(0x0063a114, ...) when 0x0063a11c;
// then the write pointer is reset. Returns the last call's eax (the count when suppressed).
uint32_t Log_Flush()
{
    uint32_t r = GameGlobal<uint32_t>(0x0063a104);
    if (static_cast<int32_t>(r) > 0)
        return r;
    if (GameGlobal<uint32_t>(0x005cacd0) == 0x00600210)
        return r;
    r = Import<StdFn1>(0x0056b06c)(0x00600210);  // OutputDebugStringA
    const uint32_t cb = GameGlobal<uint32_t>(0x0063a10c);
    if (cb && !GameGlobal<uint32_t>(0x0063a108))
        r = reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(cb)(0x00600210);
    using Fprintf = uint32_t(__cdecl*)(uint32_t, uint32_t, ...);
    const uint32_t f = GameGlobal<uint32_t>(0x0063a110);
    if (f)
        r = GameFunction<Fprintf>(0x0053ae47)(f, 0x005bbb70, 0x00600210u);
    if (GameGlobal<uint32_t>(0x0063a11c))
        r = GameFunction<Fprintf>(0x0053ae47)(GameGlobal<uint32_t>(0x0063a114), 0x005bbb70, 0x00600210u);
    GameGlobal<uint32_t>(0x005cacd0) = 0x00600210;
    return r;
}
static uint32_t Log_Flush_Abi()
{
    return Log_Flush();
}
ABI_ENTRY_004e0bd0(Log_Flush_Entry, Log_Flush_Abi)
HOOK(0x004e0bd0, "Log_Flush", Log_Flush_Entry, nullptr);

// 0x004e0b30 Log_Printf (cdecl format, ...). Unless suppressed (0x0063a104 > 0; returns the count), appends to the log
// buffer at the write pointer 0x005cacd0 (flushed first when fewer than 0x200 bytes remain before 0x00604210): the
// indent 0x0063a0f8 in spaces, then vsprintf (0x00538a0f) of the format; the pointer advances by the new text's
// length. Unless 0x0063a0fc is set the buffer is flushed (a tail jump to Log_Flush); else returns the length with al
// = that flag. Variadic: a hand-written stub passes the address of the caller's arguments as the va_list.
uint32_t Log_PrintfV(const char* fmt, void* args)
{
    const uint32_t n = GameGlobal<uint32_t>(0x0063a104);
    if (static_cast<int32_t>(n) > 0)
        return n;
    uint32_t w = GameGlobal<uint32_t>(0x005cacd0);
    if (static_cast<int32_t>(0x00604210 - w) < 0x200)
    {
        Log_Flush();
        w = GameGlobal<uint32_t>(0x005cacd0);
    }
    const uint32_t indent = GameGlobal<uint32_t>(0x0063a0f8);
    std::memset(P(w), 0x20, indent);
    GameFunction<int(__cdecl*)(char*, const char*, void*)>(0x00538a0f)(reinterpret_cast<char*>(indent + w), fmt, args);
    const uint32_t start = GameGlobal<uint32_t>(0x005cacd0);
    const uint32_t len = static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(start)));
    GameGlobal<uint32_t>(0x005cacd0) = start + len;
    const uint8_t keep = GameGlobal<uint8_t>(0x0063a0fc);
    if (!keep)
        return Log_Flush();
    return (len & 0xffffff00u) | keep;
}
static uint32_t __cdecl Log_Printf_Impl(const char* fmt, void* args)
{
    return Log_PrintfV(fmt, args);
}
static __declspec(naked) void Log_Printf_Entry()
{
    __asm {
        lea eax, [esp + 8]
        push eax
        push dword ptr [esp + 8]
        call Log_Printf_Impl
        add esp, 8
        ret
    }
}
HOOK(0x004e0b30, "Log_Printf", Log_Printf_Entry, nullptr);

// 0x004e0c60 (cdecl: file name, format, ...; callers 0x004fca00, 0x0050ca70). Formats into a 0x200-byte buffer
// (vsprintf), opens the file for appending (0x004f1da0, esi = a local {handle, path}; name, 0), and when that succeeded
// writes the text (WriteFile) and closes it (CloseHandle). Then Log_Printf (text). Returns its eax. (The original's
// security cookie is not reproduced; the ABI lists six stack arguments: the variadic tail is passed as a va_list by
// a hand-written stub.)
uint32_t Log_FileV(uint32_t name, const char* fmt, void* args)
{
    uint32_t written = 0;
    uint32_t file[0x42] = {};  // {handle, path[0x104]}
    char buf[0x200];
    GameFunction<int(__cdecl*)(char*, const char*, void*)>(0x00538a0f)(buf, fmt, args);
    const uint32_t ok = orig::fn_004f1da0(u(file), 0, name, 0, 0, 0, 0);
    if (static_cast<uint8_t>(ok))
    {
        const uint32_t len = static_cast<uint32_t>(std::strlen(buf));
        reinterpret_cast<uint32_t(__stdcall*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
            GameGlobal<uint32_t>(0x0056b0cc))(file[0], u(buf), len, u(&written), 0);  // WriteFile
        Import<StdFn1>(0x0056b084)(file[0]);                                        // CloseHandle
    }
    // The text itself is the format (as in the original).
    return GameFunction<uint32_t(__cdecl*)(const char*, ...)>(0x004e0b30)(buf);
}
static uint32_t __cdecl Log_File_Impl(uint32_t name, const char* fmt, void* args)
{
    return Log_FileV(name, fmt, args);
}
static __declspec(naked) void Log_File_Entry()
{
    __asm {
        lea eax, [esp + 0xc]
        push eax
        push dword ptr [esp + 0xc]
        push dword ptr [esp + 0xc]
        call Log_File_Impl
        add esp, 0xc
        ret
    }
}
HOOK(0x004e0c60, "Log_File", Log_File_Entry, nullptr);

// 0x004e0d20 DxError_ToString (eax = HRESULT): the name string of a Direct3D / COM error code (the game's string
// table 0x0058b358..0x0058b5c4), "Unknown DirectX error" (0x0058b358) for anything else. Reproduces the original's
// compare tree and jump tables (0x004e0e64, 0x004e0e78 / 0x004e0e94) as one switch. Not executed in recorded runs.
uint32_t DxError_ToString(uint32_t hr)
{
    switch (hr)
    {
    case 0x80070057u: return 0x0058b380;  // E_INVALIDARG
    case 0x8007000eu: return 0x0058b370;  // E_OUTOFMEMORY
    case 0x80004005u: return 0x0058b390;  // E_FAIL
    case 0x8876017cu: return 0x0058b490;  // D3DERR_OUTOFVIDEOMEMORY
    case 0x88760818u: return 0x0058b398;  // D3DERR_WRONGTEXTUREFORMAT
    case 0x88760819u: return 0x0058b3f4;  // D3DERR_UNSUPPORTEDCOLOROPERATION
    case 0x8876081au: return 0x0058b418;  // D3DERR_UNSUPPORTEDCOLORARG
    case 0x8876081bu: return 0x0058b434;  // D3DERR_UNSUPPORTEDALPHAOPERATION
    case 0x8876081cu: return 0x0058b458;  // D3DERR_UNSUPPORTEDALPHAARG
    case 0x8876081du: return 0x0058b474;  // D3DERR_TOOMANYOPERATIONS
    case 0x8876081eu: return 0x0058b574;  // D3DERR_CONFLICTINGTEXTUREFILTER
    case 0x8876081fu: return 0x0058b3d4;  // D3DERR_UNSUPPORTEDFACTORVALUE
    case 0x88760821u: return 0x0058b594;  // D3DERR_CONFLICTINGRENDERSTATE
    case 0x88760822u: return 0x0058b3b4;  // D3DERR_UNSUPPORTEDTEXTUREFILTER
    case 0x88760826u: return 0x0058b550;  // D3DERR_CONFLICTINGTEXTUREPALETTE
    case 0x88760827u: return 0x0058b508;  // D3DERR_DRIVERINTERNALERROR
    case 0x88760866u: return 0x0058b4a8;  // D3DERR_NOTFOUND
    case 0x88760867u: return 0x0058b4cc;  // D3DERR_MOREDATA
    case 0x88760868u: return 0x0058b53c;  // D3DERR_DEVICELOST
    case 0x88760869u: return 0x0058b524;  // D3DERR_DEVICENOTRESET
    case 0x8876086au: return 0x0058b4b8;  // D3DERR_NOTAVAILABLE
    case 0x8876086bu: return 0x0058b4dc;  // D3DERR_INVALIDDEVICE
    case 0x8876086cu: return 0x0058b4f4;  // D3DERR_INVALIDCALL
    case 0x0876086fu: return 0x0058b5b4;  // D3DOK_NOAUTOGEN
    case 0u: return 0x0058b5c4;           // D3D_OK
    default: return 0x0058b358;           // Unknown DirectX error
    }
}
static uint32_t DxError_ToString_Abi(uint32_t eax)
{
    return DxError_ToString(eax);
}
ABI_ENTRY_004e0d20(DxError_ToString_Entry, DxError_ToString_Abi)
HOOK(0x004e0d20, "DxError_ToString", DxError_ToString_Entry, nullptr);

// 0x004e0ee0 DxError_Log (eax = function name; stack: HRESULT; caller pops). Caller 0x004fdf30. Log_Printf("Error %s in
// DX function %s\n" 0x0058b33c, DxError_ToString(hr), name). Returns its eax. Not executed in recorded runs.
uint32_t DxError_Log(uint32_t name, uint32_t hr)
{
    return GameFunction<uint32_t(__cdecl*)(const char*, ...)>(0x004e0b30)(reinterpret_cast<const char*>(0x0058b33c),
                                                                       DxError_ToString(hr), name);
}
static uint32_t DxError_Log_Abi(uint32_t eax, uint32_t a0)
{
    return DxError_Log(eax, a0);
}
ABI_ENTRY_004e0ee0(DxError_Log_Entry, DxError_Log_Abi)
HOOK(0x004e0ee0, "DxError_Log", DxError_Log_Entry, nullptr);

// 0x004e0f00 (stack: size; caller pops): 32-byte aligned allocation through the allocator hook 0x0063a0f0 (cdecl):
// size + 0x20 bytes, the result rounded up past the next 32-byte boundary, with the raw pointer's low 5 bits stored
// just below it. Returns the aligned pointer.
uint32_t Mem_AllocAligned32(uint32_t size)
{
    const uint32_t p = reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(GameGlobal<uint32_t>(0x0063a0f0))(size + 0x20);
    const uint32_t a = (p + 0x20) & 0xffffffe0u;
    D(a - 4) = p & 0x1f;
    return a;
}
static uint32_t Mem_AllocAligned32_Abi(uint32_t a0)
{
    return Mem_AllocAligned32(a0);
}
ABI_ENTRY_004e0f00(Mem_AllocAligned32_Entry, Mem_AllocAligned32_Abi)
HOOK(0x004e0f00, "Mem_AllocAligned32", Mem_AllocAligned32_Entry, nullptr);

// 0x004e0f20 (stack: aligned pointer; cdecl): recovers the raw pointer (p + [p-4] - 0x20) and tail-jumps to the free
// hook 0x0063a0f4 with it in place of the argument. Recovered as call-then-return through a hand-written stub (no
// generated stub: "no ret found"; the target is cdecl, so nothing is popped). Deviation: the caller's argument slot
// keeps the aligned pointer (the original overwrote it with the raw one).
uint32_t Mem_FreeAligned32(uint32_t p)
{
    const uint32_t raw = D(p - 4) + p - 0x20;
    return reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(GameGlobal<uint32_t>(0x0063a0f4))(raw);
}
static uint32_t __cdecl Mem_FreeAligned32_Impl(uint32_t p)
{
    return Mem_FreeAligned32(p);
}
static __declspec(naked) void Mem_FreeAligned32_Entry()
{
    __asm {
        push dword ptr [esp + 4]
        call Mem_FreeAligned32_Impl
        add esp, 4
        ret
    }
}
HOOK(0x004e0f20, "Mem_FreeAligned32", Mem_FreeAligned32_Entry, nullptr);

// 0x004e0f40 (esi = search {count, compare callback, index, result}): binary search over [0, count). The callback
// (cdecl, the search block) reads +8 and returns the comparison (stored in +0xc): 0 = found, < 0 = below, > 0 = above.
// Returns the last midpoint, or 0 when found.
uint32_t BinarySearch(uint32_t s)
{
    uint32_t hi = D(s);
    uint32_t mid = hi >> 1;
    uint32_t lo = 0;
    D(s + 8) = hi;
    if (hi == mid)
        return mid;
    do
    {
        D(s + 8) = mid;
        const int32_t r = static_cast<int32_t>(reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(D(s + 4))(s));
        D(s + 0xc) = static_cast<uint32_t>(r);
        if (r == 0)
            return 0;
        if (r < 0)
            hi = D(s + 8);
        else
            lo = D(s + 8);
        mid = ((hi - lo) >> 1) + lo;
    } while (D(s + 8) != mid);
    return mid;
}
static uint32_t BinarySearch_Abi(uint32_t esi)
{
    return BinarySearch(esi);
}
ABI_ENTRY_004e0f40(BinarySearch_Entry, BinarySearch_Abi)
HOOK(0x004e0f40, "BinarySearch", BinarySearch_Entry, nullptr);

// 0x004e0f80 (eax = object, edx = pairs or 0). Callers 0x004e0ff0, 0x004e1320. Initialises the object's +0x20 entries
// of 0x1c bytes at +0x24: +4 = pair[1] and +0x14 = pair[0] (or 0 without pairs), +8, +0xc and +0x18 = 0. Returns the
// address after the last entry + 0x14 (the object when there are none).
uint32_t Entries_Init(uint32_t obj, const uint32_t* pairs)
{
    uint32_t n = D(obj + 0x20);
    if (!n)
        return obj;
    uint32_t e = D(obj + 0x24) + 0x14;
    do
    {
        if (pairs)
        {
            D(e - 0x10) = pairs[1];
            D(e) = pairs[0];
            pairs += 2;
        }
        else
        {
            D(e - 0x10) = 0;
            D(e) = 0;
        }
        D(e - 0xc) = 0;
        D(e - 8) = 0;
        D(e + 4) = 0;
        e += 0x1c;
    } while (--n);
    return e;
}
static uint32_t Entries_Init_Abi(uint32_t eax, uint32_t edx)
{
    return Entries_Init(eax, reinterpret_cast<const uint32_t*>(edx));
}
ABI_ENTRY_004e0f80(Entries_Init_Entry, Entries_Init_Abi)
HOOK(0x004e0f80, "Entries_Init", Entries_Init_Entry, nullptr);

uint32_t Arena_Alloc(uint32_t arena, uint32_t size, uint32_t a2, uint32_t a3);

// 0x004e0ff0 (eax = arena; stack: rows, extra bytes, row count, name bytes, columns, pairs, and four slots that are
// not read; caller pops). Callers 0x00447bb0, 0x0045bf70, 0x004e1100, 0x004ee010. Builds a table in a private
// sub-arena: a 0x30-byte arena header (Arena_Alloc 0x004e1e90 (arena, 0x30, 1, 2); two linked 0x10-byte headers,
// sizes 0x30, words 1/2, parent arena), the table object (columns * 0x1c + 0x28 bytes: +0 = header, +0x18 = rows,
// +0x1c = 0, +0x20 = columns, +0x24 = the column entries at +0x28, +8 = 1), the cell storage (rows * columns * 4;
// each column entry's +0x10 points at its rows * 4 slice) and, when extra bytes are requested, an extra block
// (+0x14; +4 = +0xc = +0x10 = that size, +0x10 cleared when the block is missing). +4 ends as row count * (aligned
// name bytes + (2 * columns + 5 & ~3)). The column entries are initialised by Entries_Init (pairs). Returns the
// table.
uint32_t Table_Create(uint32_t arena, uint32_t rows, uint32_t extra, uint32_t rowCount, uint32_t nameBytes,
                      uint32_t columns, uint32_t pairs)
{
    const uint32_t hdr = Arena_Alloc(arena, 0x30, 1, 2);
    D(hdr) = hdr + 0x10;
    D(hdr + 0x10) = hdr;
    Field<uint16_t>(P(hdr), 0xa) = 2;
    D(hdr + 0x1c) = 2;
    D(hdr + 0xc) = arena;
    D(hdr + 4) = 0x30;
    Field<uint16_t>(P(hdr), 8) = 1;
    D(hdr + 0x14) = 0x30;
    D(hdr + 0x18) = 1;
    D(hdr + 0x20) = hdr;
    const uint32_t t = Arena_Alloc(hdr, columns * 0x1c + 0x28, 1, 0);
    D(t + 0x18) = rows;
    const uint32_t bytes = ((columns + columns + 5) & 0xfffffffcu) + ((nameBytes + 3) & 0xfffffffcu);
    const uint32_t total = bytes * rowCount;
    D(t) = hdr;
    D(t + 0x24) = t + 0x28;
    D(t + 0x1c) = 0;
    D(t + 0x20) = columns;
    D(t + 4) = total;
    D(t + 8) = 1;
    uint32_t cells = Arena_Alloc(hdr, (rows * columns) << 2, 1, 0);
    D(t + 4) = extra;
    D(t + 0xc) = extra;
    D(t + 0x10) = extra;
    if (extra)
        D(t + 0x14) = Arena_Alloc(D(t), extra, 1, 0);
    if (!D(t + 0x14))
        D(t + 0x10) = 0;
    uint32_t e = D(t + 0x24);
    D(t + 4) = total;
    for (uint32_t n = columns; n != 0; --n)
    {
        D(e + 0x10) = cells;
        cells += rows << 2;
        e += 0x1c;
    }
    Entries_Init(t, reinterpret_cast<const uint32_t*>(pairs));
    return t;
}
static uint32_t Table_Create_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                 uint32_t a5, uint32_t /*a6*/, uint32_t /*a7*/, uint32_t /*a8*/, uint32_t /*a9*/)
{
    return Table_Create(eax, a0, a1, a2, a3, a4, a5);
}
ABI_ENTRY_004e0ff0(Table_Create_Entry, Table_Create_Abi)
HOOK(0x004e0ff0, "Table_Create", Table_Create_Entry, nullptr);

uint32_t Arena_FreeBlock(uint32_t arena, uint32_t block);

// 0x004e1100 (eax = extra rows, edx = sort column; stack: &table, arena, row count, name bytes; caller pops). Caller
// 0x004f0cd0. Rebuilds a table (Table_Create) with room for old rows + extra rows and the old rows' total byte size
// (the size word at each row's start, summed over the sort column's rows). The column entries are search blocks
// ({count, compare, index, result, rows, base, key}); each old entry gets count = old row count, the new ones copy
// +4 and +0x14. Each old row (in sort-column order) is copied dword-wise into the new storage (+0x14 advances, +0x10
// shrinks), stored at its index in the new sort column, and for every other column the old entry is binary searched
// (inlined BinarySearch, key = old +0x14 + the row's field offset word + row) and the new row stored at the found
// index of the new column; the new row count +0x1c is incremented. Then every block of the old table's arena
// (circular list at header +0x10) is freed (0x004e1f70 (header, block + 0xc)) and *table = the new table. Returns 1
// (0 when Table_Create failed; 1 without touching anything when there are no rows at all). Not executed in recorded
// runs.
uint32_t Table_Grow(uint32_t extra, uint32_t col, uint32_t* tablePtr, uint32_t arena, uint32_t rowCount,
                    uint32_t nameBytes)
{
    const uint32_t old = *tablePtr;
    const uint32_t n = D(old + 0x1c);
    const uint32_t rows = extra + n;
    if (!rows)
        return 1;
    const uint32_t colOff = col * 0x1c;
    uint32_t sum = 0;
    {
        uint32_t p = D(D(old + 0x24) + colOff + 0x10);
        for (uint32_t k = n; k != 0; --k)
        {
            sum += Field<uint16_t>(P(D(p)), 0);
            p += 4;
        }
    }
    const uint32_t nt = Table_Create(arena, rows, sum, rowCount, nameBytes, D(old + 0x20), 0);
    if (!nt)
        return 0;
    {
        uint32_t oe = D(old + 0x24);
        uint32_t ne = D(nt + 0x24) + 4;
        for (uint32_t k = D(old + 0x20); k != 0; --k)
        {
            D(oe) = D(old + 0x1c);
            D(ne + 0x10) = D(oe + 0x14);
            D(ne) = D(oe + 4);
            oe += 0x1c;
            ne += 0x1c;
        }
    }
    const uint32_t newSort = D(nt + 0x24) + colOff;
    const uint32_t oldSort = D(old + 0x24) + colOff;
    for (uint32_t i = 0; i < D(old + 0x1c); ++i)
    {
        const uint32_t row = D(D(oldSort + 0x10) + i * 4);
        const uint32_t size = Field<uint16_t>(P(row), 0);
        const uint32_t dest = D(nt + 0x14);
        D(nt + 0x14) = dest + size;
        D(nt + 0x10) = D(nt + 0x10) - size;
        for (uint32_t k = 0; k < (size >> 2); ++k)
            D(dest + k * 4) = D(row + k * 4);
        D(D(newSort + 0x10) + i * 4) = dest;
        uint32_t oe = D(old + 0x24);
        uint32_t neRows = D(nt + 0x24) + 0x10;
        uint32_t field = row + 2;
        for (uint32_t c = D(old + 0x20); c != 0; --c)
        {
            if (oe != oldSort)
            {
                D(oe + 0x18) = D(oe + 0x14) + Field<uint16_t>(P(field), 0) + row;
                BinarySearch(oe);
                D(D(neRows) + D(oe + 8) * 4) = dest;
            }
            field += 2;
            neRows += 0x1c;
            oe += 0x1c;
        }
        D(nt + 0x1c) = D(nt + 0x1c) + 1;
    }
    const uint32_t hdr = D(old);
    const uint32_t sentinel = hdr + 0x10;
    uint32_t node = D(hdr + 0x10);
    uint32_t next = D(node);
    node = node == sentinel ? 0 : node;
    if (node)
    {
        for (;;)
        {
            Arena_FreeBlock(hdr, node + 0xc);
            if (next == sentinel)
                break;
            node = next;
            next = D(next);
        }
    }
    *tablePtr = nt;
    return 1;
}
static uint32_t Table_Grow_Abi(uint32_t eax, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Table_Grow(eax, edx, reinterpret_cast<uint32_t*>(a0), a1, a2, a3);
}
ABI_ENTRY_004e1100(Table_Grow_Entry, Table_Grow_Abi)
HOOK(0x004e1100, "Table_Grow", Table_Grow_Entry, nullptr);

// Arenas ("MAP_Mem"): a 0x30-byte header {+0xc parent, +0x10 circular block list, +0x14 bytes, +0x18 blocks,
// +0x1c minimum alignment shift}; each block carries a 0xc-byte header {+0 next, +4 size, word +8 = 1, word +0xa =
// alignment shift} in front of the returned pointer. Without a parent (or when the parent fails) memory comes from
// the allocator hooks 0x005cacc0 (size, shift) / 0x005cacc4 (pointer).

// 0x004e1e90 (cdecl: arena or 0, size, zero, alignment shift). Returns 0 for size 0. The shift is raised to the arena's
// minimum; size grows by the aligned header padding (rounded to 4, + 0xf) and the memory comes from the parent arena
// (recursively, same zero flag and shift) or the allocator hook; it is cleared when zero is set. With an arena the
// block header is filled and pushed on its list, +0x18 += 1, +0x14 += size; returns header + 0xc. Without an arena
// returns the raw memory. Deviation: the original also overwrites its size argument slot with the padded size.
uint32_t Arena_Alloc(uint32_t arena, uint32_t size, uint32_t zero, uint32_t shift)
{
    if (!size)
        return 0;
    uint32_t pad;
    uint32_t total;
    uint32_t mem = 0;
    bool have = false;
    if (arena)
    {
        if (shift < D(arena + 0x1c))
            shift = Field<uint16_t>(P(arena), 0x1c);
        const uint32_t mask = (1u << (shift & 0x1f)) - 1;
        pad = ((mask + 0xc) & ~mask) - 0xc;
        total = (pad + size + 0xf) & 0xfffffffcu;
        const uint32_t parent = D(arena + 0xc);
        if (parent)
        {
            mem = Arena_Alloc(parent, total, zero, shift);
            have = mem != 0;
        }
    }
    else
    {
        pad = size;
        total = size;
    }
    if (!have)
        mem = reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t)>(GameGlobal<uint32_t>(0x005cacc0))(total, shift);
    if (zero)
        std::memset(P(mem), 0, total);
    if (!arena)
        return mem;
    const uint32_t h = mem + pad;
    D(h) = h;
    D(h) = D(arena + 0x10);
    D(arena + 0x10) = h;
    D(h + 4) = total;
    Field<uint16_t>(P(h), 0xa) = static_cast<uint16_t>(shift);
    Field<uint16_t>(P(h), 8) = 1;
    D(arena + 0x18) = D(arena + 0x18) + 1;
    D(arena + 0x14) = D(arena + 0x14) + total;
    return h + 0xc;
}
static uint32_t Arena_Alloc_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Arena_Alloc(a0, a1, a2, a3);
}
ABI_ENTRY_004e1e90(Arena_Alloc_Entry, Arena_Alloc_Abi)
HOOK(0x004e1e90, "Arena_Alloc", Arena_Alloc_Entry, nullptr);

// 0x004e1f70 (cdecl: arena or 0, block). Without a block returns 0; without an arena frees through the hook 0x005cacc4.
// Otherwise unlinks the block header from the arena list (not found: logs "Error MAP_MemFree: couldn't free block"
// 0x0058b314 and returns), self-links it, takes its size off +0x14 and one off +0x18, and when header word +8 bit 0 is
// set frees the raw memory (header minus the aligned padding) to the parent arena (recursively) or the hook. Returns
// the last call's eax (the block size when nothing more is freed).
uint32_t Arena_FreeBlock(uint32_t arena, uint32_t block)
{
    if (!block)
        return 0;
    using FreeFn = uint32_t(__cdecl*)(uint32_t);
    if (!arena)
        return reinterpret_cast<FreeFn>(GameGlobal<uint32_t>(0x005cacc4))(block);
    const uint32_t mask = (1u << (Field<uint8_t>(P(block), static_cast<uint32_t>(-2)) & 0x1f)) - 1;
    const uint32_t h = block - 0xc;
    const uint32_t sentinel = arena + 0x10;
    uint32_t prev = sentinel;
    uint32_t n = D(arena + 0x10);
    bool found = false;
    while (n != sentinel)
    {
        if (n == h)
        {
            found = true;
            break;
        }
        prev = n;
        n = D(n);
    }
    if (!found)
        return GameFunction<uint32_t(__cdecl*)(const char*, ...)>(0x004e0b30)(reinterpret_cast<const char*>(0x0058b314));
    D(prev) = D(n);
    D(n) = n;
    const uint32_t size = D(h + 4);
    D(arena + 0x14) = D(arena + 0x14) - size;
    D(arena + 0x18) = D(arena + 0x18) - 1;
    if (!(Field<uint8_t>(P(h), 8) & 1))
        return size;
    const uint32_t parent = D(arena + 0xc);
    const uint32_t raw = h - ((mask + 0xc) & ~mask) + 0xc;
    if (parent)
        return Arena_FreeBlock(parent, raw);
    return reinterpret_cast<FreeFn>(GameGlobal<uint32_t>(0x005cacc4))(raw);
}
static uint32_t Arena_FreeBlock_Abi(uint32_t a0, uint32_t a1)
{
    return Arena_FreeBlock(a0, a1);
}
ABI_ENTRY_004e1f70(Arena_FreeBlock_Entry, Arena_FreeBlock_Abi)
HOOK(0x004e1f70, "Arena_FreeBlock", Arena_FreeBlock_Entry, nullptr);

// 0x004e1e50 (ebx = arena). Callers 0x00447c70, 0x00448c70, 0x00493fe0. Frees every block on the arena's list
// (Arena_FreeBlock (arena, header + 0xc), the next link read first). Returns the last eax (0 for an empty list).
uint32_t Arena_FreeAll(uint32_t arena)
{
    const uint32_t sentinel = arena + 0x10;
    uint32_t node = D(arena + 0x10);
    uint32_t next = D(node);
    node = node == sentinel ? 0 : node;
    uint32_t r = node;
    if (!node)
        return 0;
    for (;;)
    {
        r = Arena_FreeBlock(arena, node + 0xc);
        if (next == sentinel)
            break;
        node = next;
        next = D(next);
    }
    return r;
}
static uint32_t Arena_FreeAll_Abi(uint32_t ebx)
{
    return Arena_FreeAll(ebx);
}
ABI_ENTRY_004e1e50(Arena_FreeAll_Entry, Arena_FreeAll_Abi)
HOOK(0x004e1e50, "Arena_FreeAll", Arena_FreeAll_Entry, nullptr);

// 0x004e2010 (cdecl: search block): range compare callback for BinarySearch. row = rows (+0x10)[index (+8)]; the
// field at row + the row's word +2 holds {start, ?, length}; with the key *(+0x18): -1 when key - start < 0, else 1
// when key - start - length >= 0, else 0 (sign of the 32-bit differences). No direct callers; not executed in
// recorded runs.
uint32_t Search_CompareRange(uint32_t s)
{
    const uint32_t row = D(D(s + 0x10) + D(s + 8) * 4);
    const uint32_t f = Field<uint16_t>(P(row), 2) + row;
    const uint32_t d = D(D(s + 0x18)) - D(f);
    if (static_cast<int32_t>(d) < 0)
        return 0xffffffffu;
    return static_cast<int32_t>(d - D(f + 8)) >= 0 ? 1u : 0u;
}
static uint32_t Search_CompareRange_Abi(uint32_t a0)
{
    return Search_CompareRange(a0);
}
ABI_ENTRY_004e2010(Search_CompareRange_Entry, Search_CompareRange_Abi)
HOOK(0x004e2010, "Search_CompareRange", Search_CompareRange_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Matrices: 0x44 bytes, float m[16] row-major plus a flags word at +0x40 (bit 4 / bit 5 swap on transpose).

// 0x004e2210 Matrix_Transpose (eax = destination, ebx = source): transposes (through a local copy when they are the
// same matrix), the flags word with bits 4 and 5 exchanged. Returns the source flags without those bits (eax as
// left).
uint32_t Matrix_Transpose(uint32_t dst, uint32_t src)
{
    uint32_t tmp[17];
    uint32_t* out = dst == src ? tmp : reinterpret_cast<uint32_t*>(dst);
    const uint32_t* s = reinterpret_cast<const uint32_t*>(src);
    for (int i = 0; i < 4; ++i)
    {
        out[i] = s[i * 4];
        out[4 + i] = s[i * 4 + 1];
        out[8 + i] = s[i * 4 + 2];
        out[12 + i] = s[i * 4 + 3];
    }
    const uint32_t f = s[16];
    const uint32_t low = f & 0xffffffcfu;
    out[16] = ((f >> 1) & 0x10) | ((f & 0x10) << 1) | low;
    if (out != reinterpret_cast<uint32_t*>(dst))
        std::memcpy(P(dst), out, 0x44);
    return low;
}
static uint32_t Matrix_Transpose_Abi(uint32_t eax, uint32_t ebx)
{
    return Matrix_Transpose(eax, ebx);
}
ABI_ENTRY_004e2210(Matrix_Transpose_Entry, Matrix_Transpose_Abi)
HOOK(0x004e2210, "Matrix_Transpose", Matrix_Transpose_Entry, nullptr);

// 0x004e2280 Matrix_InverseGeneral (cdecl: destination, source). Caller 0x004e2b40. Gauss-Jordan elimination with
// partial pivoting on the 4x8 matrix [source | identity (0x005fb8d8)] through a row-pointer array: per column the row
// with the largest |value| (first strict maximum) is swapped in; a zero pivot copies the source to the destination
// and returns 0; otherwise the pivot row is scaled by 1/pivot and subtracted (factor * pivot row) from the other three
// rows, all eight entries each. The destination gets the right halves of the final rows and the source's flags word;
// returns 1.
uint32_t Matrix_InverseGeneral(uint32_t dst, uint32_t src)
{
    float a[4][8];
    float* rows[4];
    for (int r = 0; r < 4; ++r)
    {
        rows[r] = a[r];
        for (int k = 0; k < 4; ++k)
        {
            a[r][k] = Fl(src + (r * 4 + k) * 4);
            a[r][4 + k] = Fl(0x005fb8d8 + (r * 4 + k) * 4);
        }
    }
    auto absx = [](float x) { return x < Fl(0x0056c05c) ? -x : x; };
    for (int col = 0; col < 4; ++col)
    {
        int best = col;
        for (int j = col + 1; j < 4; ++j)
        {
            if (absx(rows[j][col]) > absx(rows[best][col]))
                best = j;
        }
        float* piv = rows[best];
        rows[best] = rows[col];
        rows[col] = piv;
        if (piv[col] == Fl(0x0056c05c))
        {
            std::memcpy(P(dst), P(src), 0x44);
            return 0;
        }
        const float inv = Fl(0x005bcb2c) / piv[col];
        for (int k = 0; k < 8; ++k)
            piv[k] = inv * piv[k];
        for (int r = 0; r < 4; ++r)
        {
            if (r == col)
                continue;
            float* row = rows[r];
            const float f = row[col];
            for (int k = 0; k < 8; ++k)
                row[k] = row[k] - f * piv[k];
        }
    }
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k)
            Fl(dst + (r * 4 + k) * 4) = rows[r][4 + k];
    D(dst + 0x40) = D(src + 0x40);
    return 1;
}
static uint32_t Matrix_InverseGeneral_Abi(uint32_t a0, uint32_t a1)
{
    return Matrix_InverseGeneral(a0, a1);
}
ABI_ENTRY_004e2280(Matrix_InverseGeneral_Entry, Matrix_InverseGeneral_Abi)
HOOK(0x004e2280, "Matrix_InverseGeneral", Matrix_InverseGeneral_Entry, nullptr);

// 0x004e28f0 Matrix_Identity (edx = matrix): zeroes the 0x11 dwords and sets the diagonal to 1.0. Returns 1.0's bits
// (eax as left).
uint32_t Matrix_Identity(uint32_t m)
{
    std::memset(P(m), 0, 0x11 * 4);
    D(m) = 0x3f800000;
    D(m + 0x14) = 0x3f800000;
    D(m + 0x28) = 0x3f800000;
    D(m + 0x3c) = 0x3f800000;
    return 0x3f800000;
}
static uint32_t Matrix_Identity_Abi(uint32_t edx)
{
    return Matrix_Identity(edx);
}
ABI_ENTRY_004e28f0(Matrix_Identity_Entry, Matrix_Identity_Abi)
HOOK(0x004e28f0, "Matrix_Identity", Matrix_Identity_Entry, nullptr);

// 0x004e2910 Matrix_Multiply (eax = B; stack: A, destination; caller pops): destination = A * B (through a local when
// the destination is A or B). Either flags word with bit 0x20: full 4x4 product (each element summed from k = 3 down
// to 0). Otherwise affine: the 3x3 part is the product when both have rotation bits (0xf), else a copy of the 3x4 rows
// of whichever has them (B when A has none); the translation column is A's rotation times B's translation plus A's
// translation when B has bit 0x10, else A's; the last row is (0, 0, 0, 1). Flags = (A | B) with bit 3 kept only when
// one of them has it, plus bit 2 when A has bit 1 and B bit 0. Each sum is reproduced in the original's operand order.
// No result (eax keeps B).
void Matrix_Multiply(uint32_t bm, uint32_t am, uint32_t dst)
{
    float tmp[17];
    float* out = (dst == am || dst == bm) ? tmp : reinterpret_cast<float*>(dst);
    const float* A = reinterpret_cast<const float*>(am);
    const float* B = reinterpret_cast<const float*>(bm);
    const uint32_t af = D(am + 0x40);
    if (!(af & 0x20) && !(D(bm + 0x40) & 0x20))
    {
        if ((af & 0xf) && (D(bm + 0x40) & 0xf))
        {
            for (int r = 0; r < 3; ++r)
            {
                const float* a = A + r * 4;
                float* o = out + r * 4;
                o[0] = a[0] * B[0] + a[1] * B[4] + B[8] * a[2];
                o[1] = B[9] * a[2] + B[5] * a[1] + B[1] * a[0];
                o[2] = a[2] * B[10] + a[0] * B[2] + a[1] * B[6];
            }
        }
        else
        {
            const uint32_t from = (af & 0xf) ? am : bm;
            std::memcpy(out, P(from), 12 * 4);
        }
        if (Field<uint8_t>(P(bm), 0x40) & 0x10)
        {
            out[3] = A[2] * B[11] + A[1] * B[7] + A[0] * B[3] + A[3];
            out[7] = A[6] * B[11] + A[4] * B[3] + A[5] * B[7] + A[7];
            out[11] = A[10] * B[11] + A[8] * B[3] + A[9] * B[7] + A[11];
        }
        else
        {
            reinterpret_cast<uint32_t*>(out)[3] = D(am + 0xc);
            reinterpret_cast<uint32_t*>(out)[7] = D(am + 0x1c);
            reinterpret_cast<uint32_t*>(out)[11] = D(am + 0x2c);
        }
        reinterpret_cast<uint32_t*>(out)[14] = 0;
        reinterpret_cast<uint32_t*>(out)[13] = 0;
        reinterpret_cast<uint32_t*>(out)[12] = 0;
        reinterpret_cast<uint32_t*>(out)[15] = 0x3f800000;
    }
    else
    {
        for (int r = 0; r < 4; ++r)
        {
            const float* a = A + r * 4;
            float* o = out + r * 4;
            for (int c = 0; c < 4; ++c)
                o[c] = B[12 + c] * a[3] + B[8 + c] * a[2] + B[4 + c] * a[1] + B[c] * a[0];
        }
    }
    const uint32_t d = D(bm + 0x40);
    const uint32_t e = D(am + 0x40);
    uint32_t f = ((d & e) & 8) ^ (d | e);
    reinterpret_cast<uint32_t*>(out)[16] = f;
    if ((Field<uint8_t>(P(am), 0x40) & 2) && (Field<uint8_t>(P(bm), 0x40) & 1))
    {
        f |= 4;
        reinterpret_cast<uint32_t*>(out)[16] = f;
    }
    if (out != reinterpret_cast<float*>(dst))
        std::memcpy(P(dst), out, 0x44);
}
static void Matrix_Multiply_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    Matrix_Multiply(eax, a0, a1);
}
ABI_ENTRY_004e2910(Matrix_Multiply_Entry, Matrix_Multiply_Abi)
HOOK(0x004e2910, "Matrix_Multiply", Matrix_Multiply_Entry, nullptr);

// 0x004e2b40 Matrix_Inverse (eax = source, ecx = destination). Projective (flags bit 0x20): Matrix_InverseGeneral
// (destination, source), returning its eax. Otherwise (through a local when in place) the 3x3 part is the identity
// (no rotation bits 0xf), the transpose (no scale/shear bits 6), or the cofactor inverse divided by the determinant
// (no zero check); the translation is -(R^-1 * t) when flags bit 0x10, else 0; last row (0, 0, 0, 1); flags copied.
// Returns the source flags.
uint32_t Matrix_Inverse(uint32_t src, uint32_t dst)
{
    const uint32_t f = D(src + 0x40);
    if (!(~f & 0x20))
        return Matrix_InverseGeneral(dst, src);
    float tmp[17];
    float* o = dst == src ? tmp : reinterpret_cast<float*>(dst);
    uint32_t* ou = reinterpret_cast<uint32_t*>(o);
    const float* s = reinterpret_cast<const float*>(src);
    if (!(f & 0xf))
    {
        ou[10] = 0x3f800000;
        ou[5] = 0x3f800000;
        ou[0] = 0x3f800000;
        ou[9] = 0;
        ou[8] = 0;
        ou[6] = 0;
        ou[4] = 0;
        ou[2] = 0;
        ou[1] = 0;
    }
    else if (!(f & 6))
    {
        const uint32_t* su = reinterpret_cast<const uint32_t*>(src);
        ou[0] = su[0];
        ou[1] = su[4];
        ou[2] = su[8];
        ou[4] = su[1];
        ou[5] = su[5];
        ou[6] = su[9];
        ou[8] = su[2];
        ou[9] = su[6];
        ou[10] = su[10];
    }
    else
    {
        const float c0 = s[10] * s[5] - s[9] * s[6];
        const float det = c0 * s[0] - (s[10] * s[4] - s[8] * s[6]) * s[1] + (s[9] * s[4] - s[8] * s[5]) * s[2];
        const float inv = Fl(0x005bcb2c) / det;
        o[0] = c0 * inv;
        o[1] = (s[9] * s[2] - s[10] * s[1]) * inv;
        o[2] = (s[6] * s[1] - s[5] * s[2]) * inv;
        o[4] = (s[8] * s[6] - s[10] * s[4]) * inv;
        o[5] = (s[10] * s[0] - s[8] * s[2]) * inv;
        o[6] = (s[4] * s[2] - s[6] * s[0]) * inv;
        o[8] = (s[9] * s[4] - s[8] * s[5]) * inv;
        o[9] = (s[8] * s[1] - s[9] * s[0]) * inv;
        o[10] = (s[5] * s[0] - s[4] * s[1]) * inv;
    }
    if (Field<uint8_t>(P(src), 0x40) & 0x10)
    {
        o[3] = -(o[2] * s[11] + s[3] * o[0] + o[1] * s[7]);
        o[7] = -(o[4] * s[3] + s[7] * o[5] + o[6] * s[11]);
        o[11] = -(o[10] * s[11] + s[3] * o[8] + o[9] * s[7]);
    }
    else
    {
        ou[11] = 0;
        ou[7] = 0;
        ou[3] = 0;
    }
    ou[14] = 0;
    ou[13] = 0;
    ou[12] = 0;
    ou[15] = 0x3f800000;
    const uint32_t r = D(src + 0x40);
    ou[16] = r;
    if (o != reinterpret_cast<float*>(dst))
        std::memcpy(P(dst), o, 0x44);
    return r;
}
static uint32_t Matrix_Inverse_Abi(uint32_t eax, uint32_t ecx)
{
    return Matrix_Inverse(eax, ecx);
}
ABI_ENTRY_004e2b40(Matrix_Inverse_Entry, Matrix_Inverse_Abi)
HOOK(0x004e2b40, "Matrix_Inverse", Matrix_Inverse_Entry, nullptr);

// 0x004e2d50 Matrix_Translation (edx = matrix, esi = vec3): identity with the translation column = vec, flags 0x10.
// Returns vec.z's bits (eax as left). Caller 0x0040e350; not executed in recorded runs.
uint32_t Matrix_Translation(uint32_t m, uint32_t v)
{
    Matrix_Identity(m);
    D(m + 0xc) = D(v);
    D(m + 0x1c) = D(v + 4);
    D(m + 0x2c) = D(v + 8);
    D(m + 0x40) = 0x10;
    return D(v + 8);
}
static uint32_t Matrix_Translation_Abi(uint32_t edx, uint32_t esi)
{
    return Matrix_Translation(edx, esi);
}
ABI_ENTRY_004e2d50(Matrix_Translation_Entry, Matrix_Translation_Abi)
HOOK(0x004e2d50, "Matrix_Translation", Matrix_Translation_Entry, nullptr);

// 0x004e2d90 Matrix_Scale (edx = matrix, esi = scale vec3; the ABI's three stack slots are not read): diagonal =
// scale, flags = 2 | 8 when an odd number of the components are negative. Returns the flags.
uint32_t Matrix_Scale(uint32_t m, const float* s)
{
    Matrix_Identity(m);
    D(m) = reinterpret_cast<const uint32_t*>(s)[0];
    D(m + 0x14) = reinterpret_cast<const uint32_t*>(s)[1];
    D(m + 0x28) = reinterpret_cast<const uint32_t*>(s)[2];
    const uint32_t nx = s[0] < Fl(0x0056c05c) ? 1u : 0u;
    const uint32_t ny = s[1] < Fl(0x0056c05c) ? 1u : 0u;
    const uint32_t nz = s[2] < Fl(0x0056c05c) ? 1u : 0u;
    const uint32_t f = ((nz ^ ny ^ nx) ? 8u : 0u) | 2u;
    D(m + 0x40) = f;
    return f;
}
static uint32_t Matrix_Scale_Abi(uint32_t edx, uint32_t esi, uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/)
{
    return Matrix_Scale(edx, reinterpret_cast<const float*>(esi));
}
ABI_ENTRY_004e2d90(Matrix_Scale_Entry, Matrix_Scale_Abi)
HOOK(0x004e2d90, "Matrix_Scale", Matrix_Scale_Entry, nullptr);

// 0x004e2e30 Matrix_RotationX (edx = matrix; stack: angle; caller pops; the ABI lists 7 slots, 1 is read): rotation
// about x (fsin/fcos of the float angle): m[5] = cos, m[6] = -sin, m[9] = sin, m[10] = cos, flags 1. Returns 0.
uint32_t Matrix_RotationX(uint32_t m, uint32_t angleBits)
{
    const float a = u2f(angleBits);
    const float s = X87Sin(a);
    std::memset(P(m), 0, 0x11 * 4);
    D(m) = 0x3f800000;
    D(m + 0x3c) = 0x3f800000;
    D(m + 0x40) = 1;
    const float c = X87Cos(a);
    Fl(m + 0x14) = c;
    Fl(m + 0x18) = -s;
    Fl(m + 0x24) = s;
    Fl(m + 0x28) = c;
    return 0;
}
static uint32_t Matrix_RotationX_Abi(uint32_t edx, uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/,
                                     uint32_t /*a4*/, uint32_t /*a5*/, uint32_t /*a6*/)
{
    return Matrix_RotationX(edx, a0);
}
ABI_ENTRY_004e2e30(Matrix_RotationX_Entry, Matrix_RotationX_Abi)
HOOK(0x004e2e30, "Matrix_RotationX", Matrix_RotationX_Entry, nullptr);

// 0x004e2e70 Matrix_RotationZ (edx = matrix; stack: angle; caller pops): rotation about z: m[0] = cos, m[1] = -sin,
// m[4] = sin, m[5] = cos, flags 1. Returns 1.0's bits (eax as left).
uint32_t Matrix_RotationZ(uint32_t m, uint32_t angleBits)
{
    const float a = u2f(angleBits);
    const float s = X87Sin(a);
    std::memset(P(m), 0, 0x11 * 4);
    D(m + 0x28) = 0x3f800000;
    D(m + 0x3c) = 0x3f800000;
    D(m + 0x40) = 1;
    const float c = X87Cos(a);
    Fl(m) = c;
    Fl(m + 4) = -s;
    Fl(m + 0x10) = s;
    Fl(m + 0x14) = c;
    return 0x3f800000;
}
static uint32_t Matrix_RotationZ_Abi(uint32_t edx, uint32_t a0)
{
    return Matrix_RotationZ(edx, a0);
}
ABI_ENTRY_004e2e70(Matrix_RotationZ_Entry, Matrix_RotationZ_Abi)
HOOK(0x004e2e70, "Matrix_RotationZ", Matrix_RotationZ_Entry, nullptr);

// 0x004e2eb0 Matrix_LookAt (eax = up or 0 -> (0, 0, 1), ecx = out matrix, edx = forward or 0 -> (0, -1, 0), edi =
// position). F = forward normalised; R = forward x up (from the unnormalised inputs), normalised; U = F x R. Columns:
// m0/m4/m8 = R, m1/m5/m9 = U, m2/m6/m10 = F, translation = position, last row (0, 0, 0, 1), flags 0x11. A zero length
// leaves the vector unnormalised. Returns position.z's bits. Deviation: a zero-length forward makes the original use
// uninitialised locals for F (zeros here).
uint32_t Matrix_LookAt(const float* up, uint32_t out, const float* dir, uint32_t pos)
{
    const float upDefault[3] = {0.0f, 0.0f, 1.0f};
    const float dirDefault[3] = {0.0f, -1.0f, 0.0f};
    if (!up)
        up = upDefault;
    if (!dir)
        dir = dirDefault;
    float F[3] = {0.0f, 0.0f, 0.0f};
    const float len = X87Sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(len == Fl(0x0056c05c)))
    {
        const float inv = Fl(0x005bcb2c) / len;
        F[0] = inv * dir[0];
        F[1] = inv * dir[1];
        F[2] = inv * dir[2];
    }
    float R[3];
    R[0] = dir[2] * up[1] - dir[1] * up[2];
    R[1] = up[2] * dir[0] - dir[2] * up[0];
    R[2] = dir[1] * up[0] - up[1] * dir[0];
    const float len2 = X87Sqrt(R[2] * R[2] + R[1] * R[1] + R[0] * R[0]);
    if (!(len2 == Fl(0x0056c05c)))
    {
        const float inv = Fl(0x005bcb2c) / len2;
        R[0] = R[0] * inv;
        R[1] = R[1] * inv;
        R[2] = inv * R[2];
    }
    float* m = reinterpret_cast<float*>(out);
    const float ux = F[1] * R[2] - F[2] * R[1];
    m[0] = R[0];
    m[4] = R[1];
    m[2] = F[0];
    const float uy = F[2] * R[0] - F[0] * R[2];
    const float uz = F[0] * R[1] - F[1] * R[0];
    m[9] = uz;
    m[8] = R[2];
    m[6] = F[1];
    m[1] = ux;
    m[5] = uy;
    m[10] = F[2];
    D(out + 0xc) = D(pos);
    D(out + 0x1c) = D(pos + 4);
    D(out + 0x2c) = D(pos + 8);
    D(out + 0x38) = 0;
    D(out + 0x34) = 0;
    D(out + 0x30) = 0;
    D(out + 0x3c) = 0x3f800000;
    D(out + 0x40) = 0x11;
    return D(pos + 8);
}
static uint32_t Matrix_LookAt_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi)
{
    return Matrix_LookAt(reinterpret_cast<const float*>(eax), ecx, reinterpret_cast<const float*>(edx), edi);
}
ABI_ENTRY_004e2eb0(Matrix_LookAt_Entry, Matrix_LookAt_Abi)
HOOK(0x004e2eb0, "Matrix_LookAt", Matrix_LookAt_Entry, nullptr);

// 0x004e3070 (eax = forward or 0 -> (0, -1, 0), ecx = out matrix, edx = up or 0 -> (0, 0, 1), edi = position). The
// variant of Matrix_LookAt with G = -normalised forward: R = G x up, normalised; U = G x R. Columns: R, G, U;
// translation = position, last row (0, 0, 0, 1), flags 0x11. Returns position.z's bits. Deviation as Matrix_LookAt
// for a zero-length forward. Callers: object orientation (0x00455e70 and others).
uint32_t Matrix_LookAtNeg(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi)
{
    const float upDefault[3] = {0.0f, 0.0f, 1.0f};
    const float dirDefault[3] = {0.0f, -1.0f, 0.0f};
    const float* up = edx ? reinterpret_cast<const float*>(edx) : upDefault;
    const float* dir = eax ? reinterpret_cast<const float*>(eax) : dirDefault;
    float F[3] = {0.0f, 0.0f, 0.0f};
    const float len = X87Sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(len == Fl(0x0056c05c)))
    {
        const float inv = Fl(0x005bcb2c) / len;
        F[0] = inv * dir[0];
        F[1] = inv * dir[1];
        F[2] = inv * dir[2];
    }
    const float gx = -F[0];
    const float gy = -F[1];
    const float gz = -F[2];
    float R[3];
    R[0] = gy * up[2] - gz * up[1];
    R[1] = gz * up[0] - gx * up[2];
    R[2] = gx * up[1] - gy * up[0];
    const float len2 = X87Sqrt(R[2] * R[2] + R[1] * R[1] + R[0] * R[0]);
    if (!(len2 == Fl(0x0056c05c)))
    {
        const float inv = Fl(0x005bcb2c) / len2;
        R[0] = R[0] * inv;
        R[1] = R[1] * inv;
        R[2] = inv * R[2];
    }
    float* m = reinterpret_cast<float*>(ecx);
    const float ux = gz * R[1] - gy * R[2];
    m[0] = R[0];
    m[4] = R[1];
    m[8] = R[2];
    const float uy = gx * R[2] - gz * R[0];
    m[5] = gy;
    m[9] = gz;
    const float uz = gy * R[0] - gx * R[1];
    m[10] = uz;
    m[1] = gx;
    m[2] = ux;
    m[6] = uy;
    D(ecx + 0xc) = D(edi);
    D(ecx + 0x1c) = D(edi + 4);
    D(ecx + 0x2c) = D(edi + 8);
    D(ecx + 0x38) = 0;
    D(ecx + 0x34) = 0;
    D(ecx + 0x30) = 0;
    D(ecx + 0x3c) = 0x3f800000;
    D(ecx + 0x40) = 0x11;
    return D(edi + 8);
}
static uint32_t Matrix_LookAtNeg_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi)
{
    return Matrix_LookAtNeg(eax, ecx, edx, edi);
}
ABI_ENTRY_004e3070(Matrix_LookAtNeg_Entry, Matrix_LookAtNeg_Abi)
HOOK(0x004e3070, "Matrix_LookAtNeg", Matrix_LookAtNeg_Entry, nullptr);

// 0x004e3260 (eax = out matrix, ecx = quaternion (x, y, z, w)): rotation matrix of a unit quaternion with the doubled
// products (2x = x + x ...): m0 = 1 - (2yy + 2zz), m1 = 2xy - 2zw, m2 = 2xz + 2yw, m4 = 2xy + 2zw, m5 = 1 - (2zz +
// 2xx), m6 = 2yz - 2xw, m8 = 2xz - 2yw, m9 = 2yz + 2xw, m10 = 1 - (2yy + 2xx); translation 0, last row (0, 0, 0, 1),
// flags 1. No result (eax preserved).
void Quat_ToMatrix(uint32_t out, const float* q)
{
    const float x = q[0];
    const float y = q[1];
    const float w = q[3];
    float* m = reinterpret_cast<float*>(out);
    const float z = q[2];
    D(out + 0x2c) = 0;
    const float x2 = x + x;
    D(out + 0x1c) = 0;
    D(out + 0xc) = 0;
    const float y2 = y + y;
    D(out + 0x38) = 0;
    D(out + 0x34) = 0;
    D(out + 0x30) = 0;
    D(out + 0x3c) = 0x3f800000;
    D(out + 0x40) = 1;
    const float z2 = z + z;
    const float xw = x2 * w;
    const float yw = y2 * w;
    const float zw = z2 * w;
    const float xx = x2 * x;
    const float xy = y2 * x;
    const float xz = z2 * x;
    const float yy = y2 * y;
    const float yz = z2 * y;
    const float zz = z2 * z;
    m[0] = Fl(0x005bcb2c) - (yy + zz);
    m[1] = xy - zw;
    m[2] = xz + yw;
    m[4] = xy + zw;
    m[5] = Fl(0x005bcb2c) - (zz + xx);
    m[6] = yz - xw;
    m[8] = xz - yw;
    m[9] = yz + xw;
    m[10] = Fl(0x005bcb2c) - (yy + xx);
}
static void Quat_ToMatrix_Abi(uint32_t eax, uint32_t ecx)
{
    Quat_ToMatrix(eax, reinterpret_cast<const float*>(ecx));
}
ABI_ENTRY_004e3260(Quat_ToMatrix_Entry, Quat_ToMatrix_Abi)
HOOK(0x004e3260, "Quat_ToMatrix", Quat_ToMatrix_Entry, nullptr);

// 0x004e3370 (esi = out matrix; stack: transform {+0 position, +0xc quaternion, +0x1c scale, +0x28 flags}; caller
// pops). Rotation from the quaternion (Quat_ToMatrix) when flags bit 0, else identity; translation = position when
// bit 0x10; when bits 0xa the columns are scaled by the scale vector (and negated when bit 8); flags copied. Returns
// the flags.
uint32_t Transform_ToMatrix(uint32_t out, uint32_t t)
{
    if (Field<uint8_t>(P(t), 0x28) & 1)
        Quat_ToMatrix(out, reinterpret_cast<const float*>(t + 0xc));
    else
        Matrix_Identity(out);
    if (Field<uint8_t>(P(t), 0x28) & 0x10)
    {
        D(out + 0xc) = D(t);
        D(out + 0x1c) = D(t + 4);
        D(out + 0x2c) = D(t + 8);
    }
    const uint32_t f = D(t + 0x28);
    if (f & 0xa)
    {
        float* m = reinterpret_cast<float*>(out);
        const float* s = reinterpret_cast<const float*>(t + 0x1c);
        if (f & 8)
        {
            m[0] = -(m[0] * s[0]);
            m[4] = -(m[4] * s[0]);
            m[8] = -(m[8] * s[0]);
            m[1] = -(s[1] * m[1]);
            m[5] = -(s[1] * m[5]);
            m[9] = -(s[1] * m[9]);
            m[2] = -(m[2] * s[2]);
            m[6] = -(s[2] * m[6]);
            m[10] = -(s[2] * m[10]);
        }
        else
        {
            m[0] = m[0] * s[0];
            m[4] = m[4] * s[0];
            m[8] = m[8] * s[0];
            m[1] = s[1] * m[1];
            m[5] = s[1] * m[5];
            m[9] = s[1] * m[9];
            m[2] = m[2] * s[2];
            m[6] = s[2] * m[6];
            m[10] = s[2] * m[10];
        }
    }
    const uint32_t r = D(t + 0x28);
    D(out + 0x40) = r;
    return r;
}
static uint32_t Transform_ToMatrix_Abi(uint32_t esi, uint32_t a0)
{
    return Transform_ToMatrix(esi, a0);
}
ABI_ENTRY_004e3370(Transform_ToMatrix_Entry, Transform_ToMatrix_Abi)
HOOK(0x004e3370, "Transform_ToMatrix", Transform_ToMatrix_Entry, nullptr);

namespace
{
// cos(h) / sin(h) of h = fov * 0.5 exactly as 0x004e3490 computes it (both kept on the x87 stack, divided once).
float X87CotHalf(float fov)
{
    const float half = Fl(0x005bcb4c);
    float r;
    __asm {
        fld fov
        fmul half
        fld st(0)
        fcos
        fxch st(1)
        fsin
        fdivp st(1), st
        fstp r
    }
    return r;
}
}  // namespace

// 0x004e3490 Matrix_Perspective (eax = matrix; stack: fov, near, far, aspect; caller pops): m0 = cot(fov/2) / aspect,
// m5 = cot(fov/2), q = far / (far - near), m10 = -q, m11 = -(q * near), m14 = -1, every other element 0, flags 0x36.
// No result (eax preserved).
void Matrix_Perspective(uint32_t m, uint32_t fovBits, uint32_t nearBits, uint32_t farBits, uint32_t aspectBits)
{
    D(m + 0x38) = 0xbf800000;
    D(m + 0x3c) = 0;
    D(m + 0x34) = 0;
    D(m + 0x30) = 0;
    D(m + 0x24) = 0;
    D(m + 0x20) = 0;
    D(m + 0x1c) = 0;
    D(m + 0x18) = 0;
    D(m + 0x10) = 0;
    D(m + 0xc) = 0;
    D(m + 8) = 0;
    D(m + 4) = 0;
    D(m + 0x40) = 0x36;
    const float cot = X87CotHalf(u2f(fovBits));
    const float q = u2f(farBits) / (u2f(farBits) - u2f(nearBits));
    Fl(m) = cot / u2f(aspectBits);
    Fl(m + 0x14) = cot;
    Fl(m + 0x28) = -q;
    Fl(m + 0x2c) = -(q * u2f(nearBits));
}
static void Matrix_Perspective_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Matrix_Perspective(eax, a0, a1, a2, a3);
}
ABI_ENTRY_004e3490(Matrix_Perspective_Entry, Matrix_Perspective_Abi)
HOOK(0x004e3490, "Matrix_Perspective", Matrix_Perspective_Entry, nullptr);

// 0x004e3510 (eax = transform B, ecx only reserves a stack slot; stack: out transform, transform A, t; caller pops).
// Blends two transforms: position and scale = (1 - t) * A + t * B (z, then y, then x, each position before the scale
// component), the quaternion by 0x004f1400 (ecx = B +0xc, edx = A +0xc, esi = out +0xc; t), flags = A | B. Returns B's
// flags.
uint32_t Transform_Blend(uint32_t b, uint32_t out, uint32_t a, uint32_t tBits)
{
    const float t = u2f(tBits);
    const float s = Fl(0x005bcb2c) - t;
    for (int k = 2; k >= 0; --k)
    {
        Fl(out + k * 4) = s * Fl(a + k * 4) + t * Fl(b + k * 4);
        Fl(out + 0x1c + k * 4) = s * Fl(a + 0x1c + k * 4) + t * Fl(b + 0x1c + k * 4);
    }
    orig::fn_004f1400(b + 0xc, a + 0xc, out + 0xc, tBits);
    const uint32_t bf = D(b + 0x28);
    D(out + 0x28) = D(a + 0x28) | bf;
    return bf;
}
static uint32_t Transform_Blend_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Transform_Blend(eax, a0, a1, a2);
}
ABI_ENTRY_004e3510(Transform_Blend_Entry, Transform_Blend_Abi)
HOOK(0x004e3510, "Transform_Blend", Transform_Blend_Entry, nullptr);

// 0x004e35a0 (ecx = matrix; stack: out quaternion (x, y, z, w); caller pops). Matrix to quaternion: trace = m5 + m0 +
// m10; when positive s = sqrt(trace + 1), w = 0.5 * s and x/y/z = (m9 - m6, m2 - m8, m4 - m1) * 0.5 / s. Otherwise the
// largest diagonal element i (strictly greater comparisons) with j, k from the cycle table 0x005fb8cc ({1, 2, 0}):
// s = sqrt(m_ii - (m_kk + m_jj) + 1), q[i] = 0.5 * s, r = 0.5 / s, w = (m_kj - m_jk) * r, q[j] = (m_ji + m_ij) * r,
// q[k] = (m_ki + m_ik) * r (m_rc = row r, column c). Returns i + 4k in the second case (0 in the first; the original
// leaves the FPU status word there).
uint32_t Matrix_ToQuat(const float* m, float* q)
{
    const float trace = m[5] + m[0] + m[10];
    if (trace > Fl(0x0056c05c))
    {
        const float s = X87Sqrt(trace + Fl(0x005bcb2c));
        q[3] = Fl(0x005bcb4c) * s;
        const float r = Fl(0x005bcb4c) / s;
        q[0] = (m[9] - m[6]) * r;
        q[1] = (m[2] - m[8]) * r;
        q[2] = (m[4] - m[1]) * r;
        return 0;
    }
    uint32_t i = 0;
    if (m[5] > m[0])
        i = 1;
    if (m[10] > m[i * 5])
        i = 2;
    const uint32_t j = D(0x005fb8cc + i * 4);
    const uint32_t k = D(0x005fb8cc + j * 4);
    const float s = X87Sqrt(m[i * 5] - (m[k * 5] + m[j * 5]) + Fl(0x005bcb2c));
    q[i] = Fl(0x005bcb4c) * s;
    const float r = Fl(0x005bcb4c) / s;
    q[3] = (m[j + k * 4] - m[k + j * 4]) * r;
    q[j] = (m[i + j * 4] + m[j + i * 4]) * r;
    q[k] = (m[i + k * 4] + m[k + i * 4]) * r;
    return i + k * 4;
}
static uint32_t Matrix_ToQuat_Abi(uint32_t ecx, uint32_t a0)
{
    return Matrix_ToQuat(reinterpret_cast<const float*>(ecx), reinterpret_cast<float*>(a0));
}
ABI_ENTRY_004e35a0(Matrix_ToQuat_Entry, Matrix_ToQuat_Abi)
HOOK(0x004e35a0, "Matrix_ToQuat", Matrix_ToQuat_Entry, nullptr);

// 0x004e36a0 (eax = source vec3 array, ecx = matrix, edx = count; stack: destination array; caller pops). Transforms
// count points by the matrix: with rotation/scale bits (0xb) the 3x3 product (plus the translation when bit 0x10),
// else only the translation (bit 0x10) or a copy. Sums in the original's order: x = (m1*y + x*m0) + z*m2 (+ m3),
// y = (x*m4 + z*m6) + y*m5 (+ m7), z = (z*m10 + m9*y) + m8*x (+ m11). Returns the source pointer past the last point.
uint32_t Mat_TransformPoints(uint32_t src, uint32_t mat, uint32_t count, uint32_t dst)
{
    const float* m = reinterpret_cast<const float*>(mat);
    const uint8_t f = Field<uint8_t>(P(mat), 0x40);
    if (!count)
        return src;
    for (uint32_t n = count; n != 0; --n)
    {
        const float* v = reinterpret_cast<const float*>(src);
        float* o = reinterpret_cast<float*>(dst);
        if (f & 0xb)
        {
            if (f & 0x10)
            {
                o[0] = m[1] * v[1] + v[0] * m[0] + v[2] * m[2] + m[3];
                o[1] = v[0] * m[4] + v[2] * m[6] + v[1] * m[5] + m[7];
                o[2] = v[2] * m[10] + m[9] * v[1] + m[8] * v[0] + m[11];
            }
            else
            {
                o[0] = m[1] * v[1] + v[0] * m[0] + v[2] * m[2];
                o[1] = v[0] * m[4] + v[2] * m[6] + v[1] * m[5];
                o[2] = v[2] * m[10] + m[9] * v[1] + m[8] * v[0];
            }
        }
        else if (f & 0x10)
        {
            o[0] = m[3] + v[0];
            o[1] = v[1] + m[7];
            o[2] = v[2] + m[11];
        }
        else
        {
            const uint32_t* vs = reinterpret_cast<const uint32_t*>(src);
            uint32_t* od = reinterpret_cast<uint32_t*>(dst);
            od[0] = vs[0];
            od[1] = vs[1];
            od[2] = vs[2];
        }
        src += 0xc;
        dst += 0xc;
    }
    return src;
}
static uint32_t Mat_TransformPoints_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return Mat_TransformPoints(eax, ecx, edx, a0);
}
ABI_ENTRY_004e36a0(Mat_TransformPoints_Entry, Mat_TransformPoints_Abi)
HOOK(0x004e36a0, "Mat_TransformPoints", Mat_TransformPoints_Entry, nullptr);

// 0x004e37f0 (eax = source vec3 array, ecx = matrix, edx = count; stack: destination array, flag mask; caller pops).
// As Mat_TransformPoints with the matrix flags ANDed with the mask first and its own summation order: x = (y*m1 +
// z*m2) + x*m0 (+ m3), y = (y*m5 + m4*x) + z*m6 (+ m7), z = (m8*x + m10*z) + m9*y (+ m11). Returns the source pointer
// past the last point.
uint32_t Mat_TransformPointsMasked(uint32_t src, uint32_t mat, uint32_t count, uint32_t dst, uint32_t mask)
{
    const float* m = reinterpret_cast<const float*>(mat);
    const uint8_t f = static_cast<uint8_t>(D(mat + 0x40) & mask);
    if (!count)
        return src;
    for (uint32_t n = count; n != 0; --n)
    {
        const float* v = reinterpret_cast<const float*>(src);
        float* o = reinterpret_cast<float*>(dst);
        if (f & 0xb)
        {
            if (f & 0x10)
            {
                o[0] = v[1] * m[1] + v[2] * m[2] + v[0] * m[0] + m[3];
                o[1] = v[1] * m[5] + m[4] * v[0] + v[2] * m[6] + m[7];
                o[2] = m[8] * v[0] + m[10] * v[2] + m[9] * v[1] + m[11];
            }
            else
            {
                o[0] = v[1] * m[1] + v[2] * m[2] + v[0] * m[0];
                o[1] = v[1] * m[5] + m[4] * v[0] + v[2] * m[6];
                o[2] = m[8] * v[0] + m[10] * v[2] + m[9] * v[1];
            }
        }
        else if (f & 0x10)
        {
            o[0] = m[3] + v[0];
            o[1] = v[1] + m[7];
            o[2] = v[2] + m[11];
        }
        else
        {
            const uint32_t* vs = reinterpret_cast<const uint32_t*>(src);
            uint32_t* od = reinterpret_cast<uint32_t*>(dst);
            od[0] = vs[0];
            od[1] = vs[1];
            od[2] = vs[2];
        }
        src += 0xc;
        dst += 0xc;
    }
    return src;
}
static uint32_t Mat_TransformPointsMasked_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1)
{
    return Mat_TransformPointsMasked(eax, ecx, edx, a0, a1);
}
ABI_ENTRY_004e37f0(Mat_TransformPointsMasked_Entry, Mat_TransformPointsMasked_Abi)
HOOK(0x004e37f0, "Mat_TransformPointsMasked", Mat_TransformPointsMasked_Entry, nullptr);

// 0x004e3950 (eax = matrix, ecx = destination vec4 array, edx = count; stack: source vec4 array; caller pops).
// Transforms count vec4s (each copied to locals first, so in place works). Affine matrices (no flags bit 0x20) keep w;
// projective ones compute w = row 3 . v. Rotation bits 0xb select the 3x3 product, bit 0x10 adds w * the translation
// column; with neither the vectors are copied (unless source == destination). Every sum is reproduced in the
// original's operand order (see the code). Returns the matrix (the copy loop leaves the last w there).
uint32_t Mat_TransformVec4(uint32_t mat, uint32_t dst, uint32_t count, uint32_t src)
{
    const float* m = reinterpret_cast<const float*>(mat);
    const uint32_t f = D(mat + 0x40);
    const bool proj = !(~f & 0x20);
    const bool rot = (f & 0xb) != 0;
    const bool tr = (f & 0x10) != 0;
    if (!rot && !tr)
    {
        if (src == dst)
            return mat;
        uint32_t r = mat;
        for (uint32_t n = count; n != 0; --n)
        {
            D(dst) = D(src);
            D(dst + 4) = D(src + 4);
            D(dst + 8) = D(src + 8);
            r = D(src + 0xc);
            D(dst + 0xc) = r;
            src += 0x10;
            dst += 0x10;
        }
        return r;
    }
    for (uint32_t n = count; n != 0; --n)
    {
        const float x = Fl(src);
        const float y = Fl(src + 4);
        const float z = Fl(src + 8);
        const uint32_t wb = D(src + 0xc);
        const float w = u2f(wb);
        float* o = reinterpret_cast<float*>(dst);
        src += 0x10;
        dst += 0x10;
        if (!proj)
        {
            if (rot && tr)
            {
                o[0] = y * m[1] + w * m[3] + z * m[2] + x * m[0];
                o[1] = y * m[5] + w * m[7] + z * m[6] + x * m[4];
                reinterpret_cast<uint32_t*>(o)[3] = wb;
                o[2] = x * m[8] + y * m[9] + w * m[11] + z * m[10];
            }
            else if (rot)
            {
                o[0] = y * m[1] + z * m[2] + x * m[0];
                o[1] = y * m[5] + z * m[6] + x * m[4];
                reinterpret_cast<uint32_t*>(o)[3] = wb;
                o[2] = x * m[8] + y * m[9] + z * m[10];
            }
            else
            {
                o[0] = w * m[3] + x;
                o[1] = w * m[7] + y;
                reinterpret_cast<uint32_t*>(o)[3] = wb;
                o[2] = w * m[11] + z;
            }
        }
        else
        {
            if (rot && tr)
            {
                o[0] = y * m[1] + w * m[3] + z * m[2] + x * m[0];
                o[1] = y * m[5] + w * m[7] + z * m[6] + x * m[4];
                o[2] = x * m[8] + y * m[9] + w * m[11] + z * m[10];
                o[3] = w * m[15] + z * m[14] + x * m[12] + y * m[13];
            }
            else if (rot)
            {
                o[0] = y * m[1] + z * m[2] + x * m[0];
                o[1] = y * m[5] + z * m[6] + x * m[4];
                o[2] = x * m[8] + y * m[9] + z * m[10];
                o[3] = w * m[15] + z * m[14] + x * m[12] + y * m[13];
            }
            else
            {
                o[0] = w * m[3] + x;
                o[1] = w * m[7] + y;
                o[2] = w * m[11] + z;
                o[3] = x * m[12] + y * m[13] + z * m[14] + w * m[15];
            }
        }
    }
    return mat;
}
static uint32_t Mat_TransformVec4_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return Mat_TransformVec4(eax, ecx, edx, a0);
}
ABI_ENTRY_004e3950(Mat_TransformVec4_Entry, Mat_TransformVec4_Abi)
HOOK(0x004e3950, "Mat_TransformVec4", Mat_TransformVec4_Entry, nullptr);

// 0x004e3db0 (eax = matrix, ecx only reserves a stack slot; stack: three angles in degrees; caller pops). Rotation
// matrix from Euler angles (each times 0x005bcba0 = pi/180): with sA/cA, sB/cB, sC/cC:
// m0 = cC*cB, m1 = -(sA*(cC*sB)) - sC*cA, m2 = sC*sA - (cC*sB)*cA, m4 = cB*sC, m5 = cC*cA - (sC*sB)*sA,
// m6 = -((sC*sB)*cA) - cC*sA, m8 = sB, m9 = cB*sA, m10 = cB*cA; translation 0, row 3 all 0 (m15 = 0 as in the
// original), flags 1. Reproduced instruction for instruction in x87 asm because several sines/cosines stay on the FPU
// stack at extended precision while others are stored as floats (the original also stores sA and cC over its
// argument slots). No result (eax preserved). Callers 0x0041a350, 0x0041f850; not executed in recorded runs.
void Matrix_FromEulerDeg(uint32_t m, uint32_t a0, uint32_t a1, uint32_t a2)
{
    float A = u2f(a0);
    float B = u2f(a1);
    float C = u2f(a2);
    float L = 0.0f;
    float k = Fl(0x005bcba0);
    D(m + 0x3c) = 0;
    D(m + 0x38) = 0;
    D(m + 0x34) = 0;
    D(m + 0x30) = 0;
    D(m + 0x2c) = 0;
    D(m + 0x1c) = 0;
    D(m + 0xc) = 0;
    D(m + 0x40) = 1;
    __asm {
        mov eax, m
        fld A
        fmul k
        fld st(0)
        fsin
        fstp A
        fcos
        fld B
        fmul k
        fst B
        fsin
        fld B
        fcos
        fstp L
        fld C
        fmul k
        fld st(0)
        fsin
        fxch st(1)
        fcos
        fst B
        fmul L
        fstp dword ptr [eax]
        fld B
        fmul st, st(2)
        fld A
        fmul st, st(1)
        fchs
        fld st(2)
        fmul st, st(5)
        fsubp st(1), st
        fstp dword ptr [eax + 4]
        fld st(1)
        fmul A
        fxch st(1)
        fmul st, st(4)
        fsubp st(1), st
        fstp dword ptr [eax + 8]
        fld L
        fmul st, st(1)
        fstp dword ptr [eax + 0x10]
        fmul st, st(1)
        fld B
        fmul st, st(3)
        fld st(1)
        fmul A
        fsubp st(1), st
        fstp dword ptr [eax + 0x14]
        fmul st, st(2)
        fchs
        fld B
        fmul A
        fsubp st(1), st
        fstp dword ptr [eax + 0x18]
        fstp dword ptr [eax + 0x20]
        fld L
        fmul A
        fstp dword ptr [eax + 0x24]
        fld L
        fmul st, st(1)
        fstp dword ptr [eax + 0x28]
        fstp st(0)
    }
}
static void Matrix_FromEulerDeg_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t a0, uint32_t a1, uint32_t a2)
{
    Matrix_FromEulerDeg(eax, a0, a1, a2);
}
ABI_ENTRY_004e3db0(Matrix_FromEulerDeg_Entry, Matrix_FromEulerDeg_Abi)
HOOK(0x004e3db0, "Matrix_FromEulerDeg", Matrix_FromEulerDeg_Entry, nullptr);

// 0x004e3e90 (eax = matrix, ecx = out vec3): the lengths of the first three rows' xyz (sqrt((a*a + b*b) + c*c)), i.e.
// the scale factors. Caller 0x004517b0. No result (eax preserved).
void Matrix_GetRowLengths(const float* m, float* out)
{
    for (int r = 0; r < 3; ++r)
    {
        const float* v = m + r * 4;
        out[r] = X87Sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    }
}
static void Matrix_GetRowLengths_Abi(uint32_t eax, uint32_t ecx)
{
    Matrix_GetRowLengths(reinterpret_cast<const float*>(eax), reinterpret_cast<float*>(ecx));
}
ABI_ENTRY_004e3e90(Matrix_GetRowLengths_Entry, Matrix_GetRowLengths_Abi)
HOOK(0x004e3e90, "Matrix_GetRowLengths", Matrix_GetRowLengths_Entry, nullptr);

// 0x004e3f00 (esi = app). The application base cleanup (caller AppBase_Destruct 0x004e0070): vtable 0x0058b2b0; the
// sound system +0x2c virtual +0x70, the singleton 0x0063a0b8 deleted (virtual 0 (1)) when +0x28 is set, input +0x30
// virtual 0, renderer +0x38 virtual +0x14; then the shared counters 0x0063a0b4 (count at +0) and 0x0063a0e8 (count
// at +4) drop and are freed to the game heap at 0. Returns eax as left. Not executed in recorded runs.
uint32_t AppBase_Cleanup(uint32_t app)
{
    uint32_t r = 0;
    const uint32_t snd = D(app + 0x2c);
    D(app) = 0x0058b2b0;
    if (snd)
        r = VCall<uint32_t>(P(snd), 0x70 / 4);
    if (D(app + 0x28))
    {
        const uint32_t s = GameGlobal<uint32_t>(0x0063a0b8);
        if (s)
            r = VCall<uint32_t>(P(s), 0, 1u);
    }
    if (D(app + 0x30))
        r = VCall<uint32_t>(P(D(app + 0x30)), 0);
    if (D(app + 0x38))
        r = VCall<uint32_t>(P(D(app + 0x38)), 0x14 / 4);
    uint32_t c = GameGlobal<uint32_t>(0x0063a0b4);
    if (--D(c) == 0)
    {
        r = orig::fn_004e08d0(kHeap, c);
        GameGlobal<uint32_t>(0x0063a0b4) = 0;
    }
    c = GameGlobal<uint32_t>(0x0063a0e8);
    if (--D(c + 4) == 0)
    {
        r = orig::fn_004e08d0(kHeap, c);
        GameGlobal<uint32_t>(0x0063a0e8) = 0;
    }
    return r;
}
static uint32_t AppBase_Cleanup_Abi(uint32_t esi)
{
    return AppBase_Cleanup(esi);
}
ABI_ENTRY_004e3f00(AppBase_Cleanup_Entry, AppBase_Cleanup_Abi)
HOOK(0x004e3f00, "AppBase_Cleanup", AppBase_Cleanup_Entry, nullptr);

// 0x004e3f80 (ecx = app): returns the byte +0x25 (al; upper eax bytes are the caller's, returned as 0). A virtual; not
// executed in recorded runs.
uint32_t AppBase_GetFlag25(uint32_t app)
{
    return Field<uint8_t>(P(app), 0x25);
}
static uint32_t AppBase_GetFlag25_Abi(uint32_t ecx)
{
    return AppBase_GetFlag25(ecx);
}
ABI_ENTRY_004e3f80(AppBase_GetFlag25_Entry, AppBase_GetFlag25_Abi)
HOOK(0x004e3f80, "AppBase_GetFlag25", AppBase_GetFlag25_Entry, nullptr);

// 0x004e3f90 (ecx = app; stack: byte; ret 4): when the byte differs from +0x25, 0x004e4790 (eax = byte); then +0x25 =
// byte. Returns that call's eax (the byte otherwise). A virtual; not executed in recorded runs.
uint32_t AppBase_SetFlag25(uint32_t app, uint32_t value)
{
    const uint8_t b = static_cast<uint8_t>(value);
    uint32_t r = b;
    if (b != Field<uint8_t>(P(app), 0x25))
        r = orig::fn_004e4790(b);
    Field<uint8_t>(P(app), 0x25) = b;
    return r;
}
static uint32_t AppBase_SetFlag25_Abi(uint32_t ecx, uint32_t a0)
{
    return AppBase_SetFlag25(ecx, a0);
}
ABI_ENTRY_004e3f90(AppBase_SetFlag25_Entry, AppBase_SetFlag25_Abi)
HOOK(0x004e3f90, "AppBase_SetFlag25", AppBase_SetFlag25_Entry, nullptr);

// 0x004e3fb0 (ecx = app): requests quit (byte +0x26 = 1; App_MessageLoop posts WM_QUIT). A virtual; not executed in
// recorded runs. No result.
void AppBase_RequestQuit(uint32_t app)
{
    Field<uint8_t>(P(app), 0x26) = 1;
}
static void AppBase_RequestQuit_Abi(uint32_t ecx)
{
    AppBase_RequestQuit(ecx);
}
ABI_ENTRY_004e3fb0(AppBase_RequestQuit_Entry, AppBase_RequestQuit_Abi)
HOOK(0x004e3fb0, "AppBase_RequestQuit", AppBase_RequestQuit_Entry, nullptr);

// 0x004e41c0 / 0x004e41d0 (ecx = app): the renderer's (+0x38) virtual +0xe8 / +0xec, its al zero-extended. App vtable
// 0x0058b6f0 slots 5 and 6; not executed in recorded runs.
uint32_t AppBase_RendererQueryE8(uint32_t app)
{
    return static_cast<uint8_t>(VCall<uint32_t>(P(D(app + 0x38)), 0xe8 / 4));
}
static uint32_t AppBase_RendererQueryE8_Abi(uint32_t ecx)
{
    return AppBase_RendererQueryE8(ecx);
}
ABI_ENTRY_004e41c0(AppBase_RendererQueryE8_Entry, AppBase_RendererQueryE8_Abi)
HOOK(0x004e41c0, "AppBase_RendererQueryE8", AppBase_RendererQueryE8_Entry, nullptr);

uint32_t AppBase_RendererQueryEC(uint32_t app)
{
    return static_cast<uint8_t>(VCall<uint32_t>(P(D(app + 0x38)), 0xec / 4));
}
static uint32_t AppBase_RendererQueryEC_Abi(uint32_t ecx)
{
    return AppBase_RendererQueryEC(ecx);
}
ABI_ENTRY_004e41d0(AppBase_RendererQueryEC_Entry, AppBase_RendererQueryEC_Abi)
HOOK(0x004e41d0, "AppBase_RendererQueryEC", AppBase_RendererQueryEC_Entry, nullptr);

// 0x004e4520 Timer_Seconds: (QueryPerformanceCounter - start 0x00604ad8) / frequency 0x00604ac8, returned in st0
// (the ABI table lists eax; callers use st0). The division is done in x87 asm (fild/fild/fdivp) like the original.
static float Timer_Seconds()
{
    int64_t t;
    GameQpc(&t);
    int64_t d = t - GameGlobal<int64_t>(0x00604ad8);
    float r;
    __asm {
        fild qword ptr [d]
        mov eax, 0x00604ac8
        fild qword ptr [eax]
        fdivp st(1), st
        fstp r
    }
    return r;
}
static float Timer_Seconds_Abi()
{
    return Timer_Seconds();
}
ABI_ENTRY_004e4520(Timer_Seconds_Entry, Timer_Seconds_Abi)
HOOK(0x004e4520, "Timer_Seconds", Timer_Seconds_Entry, nullptr);

// 0x004e41e0 (ecx = app; stack: one argument, not read; caller pops). App vtable slot 12: the per-frame step. The
// renderer (+0x38) virtual +0xc4 (device state 0x005fb8c8) gives the new state (3 -> app virtual +0xc); state 0 ends
// here. Then Timer_Tick 0x004e47e0; the frame time dt (0x00604a90) accumulates into +0x20 when +0x1c (a time limit)
// is non-zero and not (byte +0x24 and +0x18); passing the limit without +0x18 tail-jumps to app virtual +0x40
// (AppBase_RequestQuit, no arguments). Otherwise input +0x30 virtual +0x1c (dt), app virtual +8 (dt), sound +0x2c
// virtual +0x130 (+8, dt, 1), 0x004e5ec0 (bl = 0), and a pending pause adjustment (0x005fb8c4: when the counter
// 0x00604aac drops to 0, 0x00604aa4 += Timer_Seconds - 0x00604aa8; flag cleared). Returns eax as left (0 here). Not
// executed in recorded runs (Game overrides the frame).
uint32_t App_Frame(uint32_t app, uint32_t /*a0*/)
{
    uint32_t st = VCall<uint32_t>(P(D(app + 0x38)), 0xc4 / 4, GameGlobal<uint32_t>(0x005fb8c8));
    GameGlobal<uint32_t>(0x005fb8c8) = st;
    if (st == 3)
    {
        VCall<uint32_t>(P(app), 0xc / 4);
        st = GameGlobal<uint32_t>(0x005fb8c8);
    }
    if (!st)
        return 0;
    orig::fn_004e47e0();
    const uint32_t dtBits = GameGlobal<uint32_t>(0x00604a90);
    if (!(Field<uint8_t>(P(app), 0x24) && D(app + 0x18)))
    {
        if (!(Fl(app + 0x1c) == Fl(0x0056c05c)))
        {
            const float t = u2f(dtBits) + Fl(app + 0x20);
            Fl(app + 0x20) = t;
            if (!D(app + 0x18) && t > Fl(app + 0x1c))
                return VCall<uint32_t>(P(app), 0x40 / 4);
        }
    }
    const uint32_t in = D(app + 0x30);
    if (in)
        VCall<uint32_t>(P(in), 0x1c / 4, dtBits);
    VCall<uint32_t>(P(app), 8 / 4, dtBits);
    const uint32_t snd = D(app + 0x2c);
    if (snd)
        VCall<uint32_t>(P(snd), 0x130 / 4, D(app + 8), dtBits, 1u);
    orig::fn_004e5ec0(0);
    if (GameGlobal<uint8_t>(0x005fb8c4))
    {
        const uint32_t n = GameGlobal<uint32_t>(0x00604aac);
        if (n > 0)
        {
            GameGlobal<uint32_t>(0x00604aac) = n - 1;
            if (n - 1 == 0)
                Fl(0x00604aa4) = Timer_Seconds() - Fl(0x00604aa8) + Fl(0x00604aa4);
        }
        GameGlobal<uint8_t>(0x005fb8c4) = 0;
    }
    return 0;
}
static uint32_t App_Frame_Abi(uint32_t ecx, uint32_t a0)
{
    return App_Frame(ecx, a0);
}
ABI_ENTRY_004e41e0(App_Frame_Entry, App_Frame_Abi)
HOOK(0x004e41e0, "App_Frame", App_Frame_Entry, nullptr);

// 0x004e42e0 (ecx = app): runs frames (virtual +0x30) until the quit byte +0x26 is set, Sleep(0), then deletes every
// object of the list 0x0063a0d8 (next at +0x544; virtual +0x38 (1)). Returns eax as left. A virtual; not executed in
// recorded runs.
uint32_t App_RunUntilQuit(uint32_t app)
{
    while (!Field<uint8_t>(P(app), 0x26))
        VCall<uint32_t>(P(app), 0x30 / 4);
    uint32_t r = Import<StdFn1>(0x0056b0b8)(0);  // Sleep
    for (uint32_t o = GameGlobal<uint32_t>(0x0063a0d8); o; o = GameGlobal<uint32_t>(0x0063a0d8))
    {
        GameGlobal<uint32_t>(0x0063a0d8) = D(o + 0x544);
        r = VCall<uint32_t>(P(o), 0x38 / 4, 1u);
    }
    return r;
}
static uint32_t App_RunUntilQuit_Abi(uint32_t ecx)
{
    return App_RunUntilQuit(ecx);
}
ABI_ENTRY_004e42e0(App_RunUntilQuit_Entry, App_RunUntilQuit_Abi)
HOOK(0x004e42e0, "App_RunUntilQuit", App_RunUntilQuit_Entry, nullptr);

// 0x004e4350 App_WinMain (stdcall hInstance, hPrevInstance, command line, nCmdShow; ret 0x10). Caller the CRT entry
// 0x00539238. Installs the aligned-allocation hooks (0x0063a0f0 = 0x004e4330, 0x0063a0f4 = GameHeap_Free 0x004011d0),
// builds the sine tables (0x004e8310), and takes the single-instance mutex "LucasArtsAD" (CreateMutexA into
// 0x0063a0ec): no mutex, or ERROR_ALREADY_EXISTS (0xb7), closes it and returns 0. Otherwise Game_CreateAndRun
// (&{hInstance, nCmdShow}, command line), CloseHandle(mutex), and returns the run's result.
uint32_t App_WinMain(uint32_t hInstance, uint32_t /*hPrev*/, uint32_t cmdLine, uint32_t nCmdShow)
{
    uint32_t params[2] = {hInstance, nCmdShow};
    GameGlobal<uint32_t>(0x0063a0f0) = 0x004e4330;
    GameGlobal<uint32_t>(0x0063a0f4) = 0x004011d0;
    orig::fn_004e8310(0);
    const uint32_t h = Import<StdFn3>(0x0056b07c)(0, 0, 0x0058b2f4);  // CreateMutexA
    GameGlobal<uint32_t>(0x0063a0ec) = h;
    if (!h)
    {
        Import<StdFn1>(0x0056b084)(h);  // CloseHandle(0), as the original
        return 0;
    }
    if (reinterpret_cast<uint32_t(__stdcall*)()>(GameGlobal<uint32_t>(0x0056b080))() == 0xb7)  // GetLastError
    {
        Import<StdFn1>(0x0056b084)(GameGlobal<uint32_t>(0x0063a0ec));
        return 0;
    }
    const uint32_t r = Game_CreateAndRun(u(params), cmdLine);
    Import<StdFn1>(0x0056b084)(GameGlobal<uint32_t>(0x0063a0ec));
    return r;
}
static uint32_t App_WinMain_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return App_WinMain(a0, a1, a2, a3);
}
ABI_ENTRY_004e4350(App_WinMain_Entry, App_WinMain_Abi)
HOOK(0x004e4350, "App_WinMain", App_WinMain_Entry, nullptr);

// 0x004e43e0 (ecx = app): returns +4. App vtable slot 14; not executed in recorded runs.
uint32_t AppBase_GetField4(uint32_t app)
{
    return D(app + 4);
}
static uint32_t AppBase_GetField4_Abi(uint32_t ecx)
{
    return AppBase_GetField4(ecx);
}
ABI_ENTRY_004e43e0(AppBase_GetField4_Entry, AppBase_GetField4_Abi)
HOOK(0x004e43e0, "AppBase_GetField4", AppBase_GetField4_Entry, nullptr);

// 0x004e4440 (edx = object): clears +0, +4, bytes +0x208/+0x209, +0x20c, five dwords at +0x210 and 0x11 dwords at
// +0x224. Returns the object. Callers 0x004e43f0, 0x004f20d0.
uint32_t Shared268_Init(uint32_t p)
{
    D(p + 4) = 0;
    Field<uint8_t>(P(p), 0x208) = 0;
    Field<uint8_t>(P(p), 0x209) = 0;
    D(p) = 0;
    D(p + 0x20c) = 0;
    for (int i = 0; i < 5; ++i)
        D(p + 0x210 + i * 4) = 0;
    std::memset(P(p + 0x224), 0, 0x11 * 4);
    return p;
}
static uint32_t Shared268_Init_Abi(uint32_t edx)
{
    return Shared268_Init(edx);
}
ABI_ENTRY_004e4440(Shared268_Init_Entry, Shared268_Init_Abi)
HOOK(0x004e4440, "Shared268_Init", Shared268_Init_Entry, nullptr);

// 0x004e43f0: acquires the shared 0x268-byte object 0x0063a0e8 (created on first use from the game heap: 0x10, 0x268,
// 2, "new", initialised by Shared268_Init) and adds a reference (+4). Returns it. Original bug: a failed allocation
// stores 0 and then increments 0->+4. Callers App_Init 0x004e3fc0, 0x004e49e0.
uint32_t Shared268_Acquire()
{
    uint32_t p = GameGlobal<uint32_t>(0x0063a0e8);
    if (!p)
    {
        p = orig::fn_004e07f0(kHeap, 0x10, 0x268, 2, 0x0058b5e8);
        if (p)
        {
            Shared268_Init(p);
            const uint32_t c = D(p + 4) + 1;
            GameGlobal<uint32_t>(0x0063a0e8) = p;
            D(p + 4) = c;
            return p;
        }
        GameGlobal<uint32_t>(0x0063a0e8) = 0;
    }
    ++D(p + 4);
    return p;
}
static uint32_t Shared268_Acquire_Abi()
{
    return Shared268_Acquire();
}
ABI_ENTRY_004e43f0(Shared268_Acquire_Entry, Shared268_Acquire_Abi)
HOOK(0x004e43f0, "Shared268_Acquire", Shared268_Acquire_Entry, nullptr);

// 0x004e4480 (cdecl: context (not read), format, ...; callers 0x004f9ef0, 0x004fb560): vsprintf into a 0x200-byte
// buffer, then Log_Printf(buffer) (the text as the format). Returns its eax. The ABI table's register inputs come from
// the security-cookie epilogue; a hand-written stub passes the variadic tail as a va_list. (The cookie is not
// reproduced.)
uint32_t Log_PrintfCtxV(const char* fmt, void* args)
{
    char buf[0x200];
    GameFunction<int(__cdecl*)(char*, const char*, void*)>(0x00538a0f)(buf, fmt, args);
    return GameFunction<uint32_t(__cdecl*)(const char*, ...)>(0x004e0b30)(buf);
}
static uint32_t __cdecl Log_PrintfCtx_Impl(const char* fmt, void* args)
{
    return Log_PrintfCtxV(fmt, args);
}
static __declspec(naked) void Log_PrintfCtx_Entry()
{
    __asm {
        lea eax, [esp + 0xc]
        push eax
        push dword ptr [esp + 0xc]
        call Log_PrintfCtx_Impl
        add esp, 8
        ret
    }
}
HOOK(0x004e4480, "Log_PrintfCtx", Log_PrintfCtx_Entry, nullptr);

// 0x004e44e0 (stack: one argument, not read; ret 4). Caller 0x00402e80. Divides 1.0 by 0.0 ten times (a deliberate
// x87 divide-by-zero, guess: a crash/exception test hook). Returns 0.
uint32_t Fpu_DivideByZeroTest()
{
    volatile float x = 1.0f;
    volatile float y = 0.0f;
    for (int i = 10; i != 0; --i)
        x = x / y;
    return 0;
}
static uint32_t Fpu_DivideByZeroTest_Abi(uint32_t /*a0*/)
{
    return Fpu_DivideByZeroTest();
}
ABI_ENTRY_004e44e0(Fpu_DivideByZeroTest_Entry, Fpu_DivideByZeroTest_Abi)
HOOK(0x004e44e0, "Fpu_DivideByZeroTest", Fpu_DivideByZeroTest_Entry, nullptr);
