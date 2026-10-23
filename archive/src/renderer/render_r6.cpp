// Recovered (static, batch r6): renderer callbacks, decal/env-map/fx object virtual methods, UI widget draws, level
// render properties. Compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
// Most functions here are virtual methods or callbacks that the recorded runs never execute: they are marked "not
// executed in recorded runs: not verifiable by compare yet". Vtable slots come from a scan of .rdata in
// game.unpacked.exe (vtable starts = constructor stores `mov [reg], imm32`); class names from the vtable's slot 1
// descriptor (+0x14 "CLASS(Name)" / "CLASSV(Name)") where it has one.
//   0x00401070 Game_OnDeviceLost
//   0x004010c0 Game_OnDeviceRestored
//   0x00401130 fn_00401130
//   0x00410170 CameraPathObject_Load
//   0x004102a0 fn_004102a0
//   0x004102b0 fn_004102b0
//   0x00410340 fn_00410340
//   0x00410360 fn_00410360
//   0x00410380 fn_00410380
//   0x004103a0 CameraSystem_GetClass
//   0x004103b0 CameraSystem_GetTypeId
//   0x004103c0 fn_004103c0
//   0x00410520 fn_00410520
//   0x004105b0 fn_004105b0
//   0x00410750 fn_00410750
//   0x00415bb0 fn_00415bb0
//   0x00416700 DecalBase_DeletingDestructor
//   0x004184e0 fn_004184e0
//   0x00418560 DecalSystem_GetClass
//   0x00418570 DecalSystem_GetTypeId
//   0x00418580 fn_00418580
//   0x004185d0 Decal_GetClass
//   0x004185e0 Decal_GetTypeId
//   0x004185f0 fn_004185f0
//   0x00418600 ReturnZero
//   0x00418a70 EnvMap_Load
//   0x00418c00 EnvMap_Remove
//   0x00418d40 fn_00418d40
//   0x00418db0 FxObject_Remove
//   0x00418f60 FxObject_Load
//   0x00419060 FxObject_Draw
//   0x00419250 fn_00419250
//   0x00419260 FxObject_InFrustum
//   0x00419280 fn_00419280
//   0x00419330 fn_00419330
//   0x00419390 fn_00419390
//   0x00419920 fn_00419920
//   0x00423900 LevelProperties_Load
//   0x00466ce0 fn_00466ce0
//   0x004672d0 fn_004672d0
//   0x00467a20 fn_00467a20
//   0x00467dd0 fn_00467dd0
//   0x0046d450 fn_0046d450
//   0x00488110 fn_00488110
//   0x00488140 fn_00488140
//   0x0048ea30 fn_0048ea30
//   0x004935e0 fn_004935e0
//   0x00494640 fn_00494640
//   0x004948d0 fn_004948d0
//   0x00495970 Streamer_GetClass
//   0x00495980 Streamer_GetTypeId
//   0x00495990 StreamerSystem_GetClass
//   0x004959a0 StreamerSystem_GetTypeId
//   0x004a3260 fn_004a3260
//   0x004a3300 fn_004a3300
//   0x004a33d0 fn_004a33d0
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

// Game float constants (read in place, as the original does).
inline const float& kZero = GameGlobal<float>(0x0056c05c);   // 0.0
inline const float& kOne = GameGlobal<float>(0x005bcb2c);    // 1.0
inline const float& kHalf = GameGlobal<float>(0x005bcb4c);   // 0.5
inline const float& kTenth = GameGlobal<float>(0x005bcbfc);  // 0.1
inline const float& kFrameTime = GameGlobal<float>(0x00604a90);  // frame time step (read by many updates)

void* Renderer()
{
    return Field<void*>(g_game, 0x38);
}
void* PostProcess()
{
    return Field<void*>(g_game, 0xa98);
}
uint32_t Font()
{
    return Field<uint32_t>(g_game, 0xac0);
}

// The inlined block after full-screen 2D passes (same as render_r5.cpp): renderer vt+0x154(4, post-process byte
// +0xe2), world (+0x54), view (+0x50), projection (+0x4c) from post-process +0, +0x44, +0x88, viewport (+0x74) from
// +0xcc, and post-process +0x228 = 0.
void RestorePostProcessView()
{
    void* r = Renderer();
    void* pp = PostProcess();
    VCall<void>(r, 0x154 / 4, 4u, static_cast<uint32_t>(Field<uint8_t>(pp, 0xe2)));
    VCall<void>(r, 0x54 / 4, pp);
    VCall<void>(r, 0x50 / 4, u(pp) + 0x44);
    VCall<void>(r, 0x4c / 4, u(pp) + 0x88);
    VCall<void>(r, 0x74 / 4, u(pp) + 0xcc);
    Field<uint32_t>(pp, 0x228) = 0;
}

// Calls 0x004544b0 (ecx = object; stack: 3, callee pops) and returns what it leaves in st0. symbols/abi.csv lists an
// eax return, but its caller 0x00410340 adds a constant to st0 right after the call; this shim keeps that.
float Call_004544b0_St0(uint32_t ecx_, uint32_t a0, uint32_t a1, uint32_t a2)
{
    static const uint32_t target = 0x004544b0;
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a2
        push a1
        push a0
        mov ecx, ecx_
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp r
    }
    return r;
}

// The inlined lookup in the UI widget draws 0x00466ce0 / 0x004672d0 / 0x00467dd0: p = a0+4 ->+4 ->+0x20 ->+4; when
// p+0x1c and p+0x20 (unsigned) are non-zero, the finder p+0x24 gets +0 = p+0x1c, +0x18 = name, 0x004e0f40 (esi =
// finder); when finder+0xc is 0 and entry = (finder+0x10)[finder+8] exists, the result is the dword at entry +
// word entry+2. Otherwise 0.
uint32_t FindNamedValue(void* a0, uint32_t name)
{
    uint8_t* p = Field<uint8_t*>(Field<void*>(Field<void*>(Field<void*>(a0, 4), 4), 0x20), 4);
    const uint32_t first = Field<uint32_t>(p, 0x1c);
    if (!first || !Field<uint32_t>(p, 0x20))
        return 0;
    uint8_t* finder = Field<uint8_t*>(p, 0x24);
    Field<uint32_t>(finder, 0) = first;
    Field<uint32_t>(finder, 0x18) = name;
    orig::fn_004e0f40(u(finder));
    if (Field<uint32_t>(finder, 0xc))
        return 0;
    uint8_t* entry = Field<uint8_t**>(finder, 0x10)[Field<uint32_t>(finder, 8)];
    if (!entry)
        return 0;
    return Field<uint32_t>(entry, Field<uint16_t>(entry, 2));
}

// Entry stubs for functions that symbols/abi.csv marks "no ret found" (tail jumps), so tools/abi.py generated none.
// Same shape as src/gen/entry_stubs.h: save registers, push the inputs, call the cdecl impl, plain ret.
#define R6_ENTRY_EAX(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push eax __asm call impl __asm add esp, 4 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret }
#define R6_ENTRY_EAX_ECX(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push ecx __asm push eax __asm call impl __asm add esp, 8 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret }
#define R6_ENTRY_ECX_EDI(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push edi __asm push ecx __asm call impl __asm add esp, 8 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret }
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 0x00401070 (cdecl; stack: game; caller pops). Renderer device-lost callback: registered with the renderer at
// 0x004020fd (push 0x4010c0, push 0x401070, renderer vt+0x10(lost, restored, game)). When game is set:
// PostProcess_ReleaseTargets 0x0047ca60 (esi = +0xa98), 0x0045b190 (eax = +0xa84, bl = 0; stack 1), EnvMaps_ReleaseAll
// 0x00418a10, Shadows_InvalidateTargets 0x00416290 (eax = +0xa7c), then (+0x30) vt+0x28(0, 1). Not executed in
// recorded runs: not verifiable by compare yet. (0x0045b190 reads only bl; the original leaves the caller's upper
// ebx bytes there, passed here as 0.)
uint32_t Game_OnDeviceLost(void* game)
{
    if (!game)
        return 0;  // original: eax as left by the caller
    orig::fn_0047ca60(Field<uint32_t>(game, 0xa98));
    orig::fn_0045b190(Field<uint32_t>(game, 0xa84), 0, 1);
    orig::fn_00418a10();
    orig::fn_00416290(Field<uint32_t>(game, 0xa7c));
    return VCall<uint32_t>(Field<void*>(game, 0x30), 0x28 / 4, 0u, 1u);
}
static uint32_t Game_OnDeviceLost_Abi(uint32_t a0)
{
    return Game_OnDeviceLost(P(a0));
}
ABI_ENTRY_00401070(Game_OnDeviceLost_Entry, Game_OnDeviceLost_Abi)
HOOK(0x00401070, "Game_OnDeviceLost", Game_OnDeviceLost_Entry, nullptr);

// 0x004010c0 (cdecl; stack: game; caller pops). Renderer device-restored callback (registered with 0x00401070 at
// 0x004020f8). When game is set: game vt+0xc(), 0x0045b190 (eax = +0xa84, bl = 1; stack 0), PostProcess_CreateTargets
// 0x0047c920 (eax = +0xa98), FxSystem_ResetPools 0x0041c9a0 (eax = +0xa90), EnvMaps_ReleaseAll 0x00418a10,
// Shadows_InvalidateTargets 0x00416290 (eax = +0xa7c), 0x00494b70, (+0x30) vt+0x28(1, 1), Options_Apply 0x00401ce0
// (al = 0, upper eax bytes from the vt+0x28 result; esi = game), 0x00401010 (ebx = game; ecx as left by 0x00401ce0,
// only pushed as a stack slot there: passed as 0). Returns 0x00401010's eax. Not executed in recorded runs: not
// verifiable by compare yet.
uint32_t Game_OnDeviceRestored(void* game)
{
    if (!game)
        return 0;  // original: eax as left by the caller
    VCall<void>(game, 0xc / 4);
    orig::fn_0045b190(Field<uint32_t>(game, 0xa84), 1, 0);
    orig::fn_0047c920(Field<uint32_t>(game, 0xa98));
    orig::fn_0041c9a0(Field<uint32_t>(game, 0xa90));
    orig::fn_00418a10();
    orig::fn_00416290(Field<uint32_t>(game, 0xa7c));
    orig::fn_00494b70();
    const uint32_t r = VCall<uint32_t>(Field<void*>(game, 0x30), 0x28 / 4, 1u, 1u);
    orig::fn_00401ce0(r & 0xffffff00u, u(game));
    return orig::fn_00401010(0, u(game));
}
static uint32_t Game_OnDeviceRestored_Abi(uint32_t a0)
{
    return Game_OnDeviceRestored(P(a0));
}
ABI_ENTRY_004010c0(Game_OnDeviceRestored_Entry, Game_OnDeviceRestored_Abi)
HOOK(0x004010c0, "Game_OnDeviceRestored", Game_OnDeviceRestored_Entry, nullptr);

// 0x00401130 (ecx = game; stack: flag; ret 4). Game vtable 0x005bc098 slot 8 (+0x20). value = flag (low byte) ?
// game+0x4b4 : 1.0. When the flag byte differs from the global byte 0x005cad48: game vt+0x24(flag == 0) (slot 9,
// 0x004e3f90, reads a byte), the global = flag, (+0x30) vt+0x28(flag, 1) when +0x30 is set. Then 0x004908f0 (eax =
// UI +0xab4; stack: flag) when set, and renderer (+0x38) vt+0x40(value, value, value) when set (three equal floats;
// guess: gamma). Not executed in recorded runs: not verifiable by compare yet. The original pushes eax for vt+0x24
// with only al set (upper bytes: +0x4b4's when the flag is set, the caller's eax otherwise); passed as 0/1 since
// slot 9 reads only the byte.
uint32_t fn_00401130(void* game, uint32_t flag)
{
    const uint8_t b = static_cast<uint8_t>(flag);
    const uint32_t value = b ? Field<uint32_t>(game, 0x4b4) : 0x3f800000u;
    if (b != GameGlobal<uint8_t>(0x005cad48))
    {
        VCall<void>(game, 0x24 / 4, b == 0 ? 1u : 0u);
        GameGlobal<uint8_t>(0x005cad48) = b;
        if (void* o = Field<void*>(game, 0x30))
            VCall<void>(o, 0x28 / 4, flag, 1u);
    }
    uint32_t r = Field<uint32_t>(game, 0xab4);
    if (r)
        r = orig::fn_004908f0(r, flag);
    if (void* rd = Field<void*>(game, 0x38))
        r = VCall<uint32_t>(rd, 0x40 / 4, value, value, value);
    return r;
}
static uint32_t fn_00401130_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00401130(P(ecx), a0);
}
ABI_ENTRY_00401130(fn_00401130_Entry, fn_00401130_Abi)
HOOK(0x00401130, "fn_00401130", fn_00401130_Entry, nullptr);

// 0x00410170 (ecx = object; stack: 4; ret 0x10). CameraPathObject (vtable 0x005a0f18) slot 6, the load slot
// (LightObject_Load 0x0044add0 is slot 6 too): the base load 0x004517b0 (ecx = object; same 4 arguments), then
// +0x424 |= 0x40000. Not executed in recorded runs: not verifiable by compare yet.
uint32_t CameraPathObject_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t r = orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    Field<uint32_t>(obj, 0x424) |= 0x40000;
    return r;
}
static uint32_t CameraPathObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return CameraPathObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00410170(CameraPathObject_Load_Entry, CameraPathObject_Load_Abi)
HOOK(0x00410170, "CameraPathObject_Load", CameraPathObject_Load_Entry, nullptr);

// 0x004102a0 (stack: target; ret 4; ecx unused). CameraFXObject (vtable 0x005ad058) slot 7: target bytes +0xd and
// +0x7a = 1. Returns the target. Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_004102a0(void* target)
{
    Field<uint8_t>(target, 0xd) = 1;
    Field<uint8_t>(target, 0x7a) = 1;
    return u(target);
}
static uint32_t fn_004102a0_Abi(uint32_t a0)
{
    return fn_004102a0(P(a0));
}
ABI_ENTRY_004102a0(fn_004102a0_Entry, fn_004102a0_Abi)
HOOK(0x004102a0, "fn_004102a0", fn_004102a0_Entry, nullptr);

// 0x004102b0 (ecx = object; stack: a0, id; ret 8). CameraFXObject (vtable 0x005ad058) slot 0xe: id == +0x480 ->
// fn_004102e0 (edx = object); id == +0x484 -> object vt+0xd4(0). Returns false (al = 0; upper eax bytes: id's or the
// callee's). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_004102b0(void* obj, uint32_t /*a0*/, uint32_t id)
{
    if (id == Field<uint32_t>(obj, 0x480))
        return orig::fn_004102e0(u(obj)) & 0xffffff00u;
    if (id == Field<uint32_t>(obj, 0x484))
        id = VCall<uint32_t>(obj, 0xd4 / 4, 0u);
    return id & 0xffffff00u;
}
static uint32_t fn_004102b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004102b0(P(ecx), a0, a1);
}
ABI_ENTRY_004102b0(fn_004102b0_Entry, fn_004102b0_Abi)
HOOK(0x004102b0, "fn_004102b0", fn_004102b0_Entry, nullptr);

// 0x00410340 (ecx = object; stack: 3; ret 0xc; returns st0). CameraFXObject (vtable 0x005ad058) slot 0x32 (+0xc8):
// the base slot 0x32 0x004544b0 (same arguments), then st0 + [0x005bcc1c] (10000.0). Not executed in recorded runs:
// not verifiable by compare yet. Open question: 0x004544b0 -> 0x0048a030 -> 0x004e36a0 does not visibly load st0, so
// the original adds 10000 to whatever is on the x87 stack.
float fn_00410340(void* obj, uint32_t a0, uint32_t a1, uint32_t a2)
{
    const float v = Call_004544b0_St0(u(obj), a0, a1, a2);
    return v + GameGlobal<float>(0x005bcc1c);
}
static float fn_00410340_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00410340(P(ecx), a0, a1, a2);
}
ABI_ENTRY_00410340(fn_00410340_Entry, fn_00410340_Abi)
HOOK(0x00410340, "fn_00410340", fn_00410340_Entry, nullptr);

// 0x00410360 / 0x00410380 (ecx = object; stack: target; ret 4). Shared slots: Streamer/StreamerSystem vtables
// 0x0058faf0, 0x005b7ddc, 0x005b7df4, DecalSystem 0x005b9678, 0x005a826c and AIInterestObject 0x005a7820 slot 5 /
// slot 3. 0x0048c0f0 / 0x0048bda0 (ecx = target; stack: descriptor = object vt+4 (slot 1, the class descriptor),
// object). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00410360(void* obj, uint32_t target)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    return orig::fn_0048c0f0(target, desc, u(obj));
}
static uint32_t fn_00410360_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00410360(P(ecx), a0);
}
ABI_ENTRY_00410360(fn_00410360_Entry, fn_00410360_Abi)
HOOK(0x00410360, "fn_00410360", fn_00410360_Entry, nullptr);

uint32_t fn_00410380(void* obj, uint32_t target)
{
    const uint32_t desc = VCall<uint32_t>(obj, 1);
    return orig::fn_0048bda0(target, desc, u(obj));
}
static uint32_t fn_00410380_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00410380(P(ecx), a0);
}
ABI_ENTRY_00410380(fn_00410380_Entry, fn_00410380_Abi)
HOOK(0x00410380, "fn_00410380", fn_00410380_Entry, nullptr);

// 0x004103a0 / 0x004103b0. CameraSystem (vtable 0x005bad48) slots 1 / 0: the class descriptor 0x00580518
// ("CLASSV(CameraSystem)" at +0x14) and the type id 0x80000001. Not executed in recorded runs: not verifiable by
// compare yet.
uint32_t CameraSystem_GetClass()
{
    return 0x00580518;
}
ABI_ENTRY_004103a0(CameraSystem_GetClass_Entry, CameraSystem_GetClass)
HOOK(0x004103a0, "CameraSystem_GetClass", CameraSystem_GetClass_Entry, nullptr);

uint32_t CameraSystem_GetTypeId()
{
    return 0x80000001u;
}
ABI_ENTRY_004103b0(CameraSystem_GetTypeId_Entry, CameraSystem_GetTypeId)
HOOK(0x004103b0, "CameraSystem_GetTypeId", CameraSystem_GetTypeId_Entry, nullptr);

// 0x004103c0 (ecx = widget, edi passed through). UI widget vtable 0x005a76c8 slot 0x4f (+0x13c, the draw slot): the
// global 0x0063a1dc = widget, then a tail jump to UiText_Render 0x00465360 (ecx, edi unchanged, no stack). Not
// executed in recorded runs: not verifiable by compare yet.
uint32_t fn_004103c0(uint32_t ecx, uint32_t edi)
{
    GameGlobal<uint32_t>(0x0063a1dc) = ecx;
    return orig::fn_00465360(ecx, edi);
}
ABI_ENTRY_004103c0(fn_004103c0_Entry, fn_004103c0)
HOOK(0x004103c0, "fn_004103c0", fn_004103c0_Entry, nullptr);

// 0x00410520 (ecx = widget). UI widget vtable 0x005a14b8 slot 0x51 (+0x144): 0x004656f0 (esi = widget; 160.0, 0),
// 0x00465680 (ecx = widget), measure the text (+0x4f8)+4 with the font Game+0xac0 and size +0x4c8 into w/h
// (0x00495fa0 esi = &l0, edx = &l1), 0x0046b0a0 (eax = widget, ecx = &l3, edx = &l2; 0, 16) (widget-relative point),
// then 0x004655b0(widget, l3, l2, +0x4f0, +0x4f4, l1, l0, 0). Not executed in recorded runs: not verifiable by
// compare yet.
uint32_t fn_00410520(void* w)
{
    uint32_t l[4];  // esp+8 .. esp+0x14 after the two pushes (frame 0x10)
    orig::fn_004656f0(u(w), 0x43200000u, 0);
    orig::fn_00465680(u(w));
    orig::fn_00495fa0(u(&l[1]), u(&l[0]), Font(), Field<uint32_t>(Field<void*>(w, 0x4f8), 4),
                      Field<uint32_t>(w, 0x4c8));
    orig::fn_0046b0a0(u(w), u(&l[3]), u(&l[2]), 0, 0x10);
    return orig::fn_004655b0(u(w), l[3], l[2], Field<uint32_t>(w, 0x4f0), Field<uint32_t>(w, 0x4f4), l[1], l[0], 0);
}
static uint32_t fn_00410520_Abi(uint32_t ecx)
{
    return fn_00410520(P(ecx));
}
ABI_ENTRY_00410520(fn_00410520_Entry, fn_00410520_Abi)
HOOK(0x00410520, "fn_00410520", fn_00410520_Entry, nullptr);

// 0x004105b0 (ecx = widget). UI widget vtable 0x005a14b8 slot 0x4f (+0x13c, draw): colours (flags +0x474 bit 0x40:
// 0xff808080 / 0xff008080, else +0x4e4 / 0xff80ffff). Flag 0x4000: UiText_DrawBackground 0x00465430 (eax = widget)
// and clear it. Renderer vt+0x190(1); Text_Draw 0x00495e80 (eax = colour 1, ecx = font; font, +0x4c0, +0x4c8, +0x4e8,
// +0x4ec, +0x4f0, +0x4f4, 2); 0x0046b0a0 (eax = widget, ecx = &l1, edx = &l0; 0, 16); Text_Draw (eax = colour 2,
// ecx = font; font, (+0x4f8)+4, +0x4c8, l1, l0, +0x4f0, +0x4f4, 2); renderer vt+0x194(). Not executed in recorded
// runs: not verifiable by compare yet.
uint32_t fn_004105b0(void* w)
{
    uint32_t l[2];
    const uint32_t flags = Field<uint32_t>(w, 0x474);
    uint32_t c1, c2;
    if (flags & 0x40)
        c1 = 0xff808080u, c2 = 0xff008080u;
    else
        c1 = Field<uint32_t>(w, 0x4e4), c2 = 0xff80ffffu;
    if (flags & 0x4000)
    {
        orig::fn_00465430(u(w));
        Field<uint32_t>(w, 0x474) &= ~0x4000u;
    }
    VCall<void>(Renderer(), 0x190 / 4, 1u);
    uint32_t font = Font();
    orig::fn_00495e80(c1, font, font, Field<uint32_t>(w, 0x4c0), Field<uint32_t>(w, 0x4c8), Field<uint32_t>(w, 0x4e8),
                      Field<uint32_t>(w, 0x4ec), Field<uint32_t>(w, 0x4f0), Field<uint32_t>(w, 0x4f4), 2);
    orig::fn_0046b0a0(u(w), u(&l[1]), u(&l[0]), 0, 0x10);
    font = Font();
    orig::fn_00495e80(c2, font, font, Field<uint32_t>(Field<void*>(w, 0x4f8), 4), Field<uint32_t>(w, 0x4c8), l[1],
                      l[0], Field<uint32_t>(w, 0x4f0), Field<uint32_t>(w, 0x4f4), 2);
    return VCall<uint32_t>(Renderer(), 0x194 / 4);
}
static uint32_t fn_004105b0_Abi(uint32_t ecx)
{
    return fn_004105b0(P(ecx));
}
ABI_ENTRY_004105b0(fn_004105b0_Entry, fn_004105b0_Abi)
HOOK(0x004105b0, "fn_004105b0", fn_004105b0_Entry, nullptr);

// 0x00410750 (ecx = widget). UI widget vtable 0x005a76c8 slot 0x51 (+0x144): flags +0x474 |= 0x4000, the widget
// points (-200, -24) and (200, 24) through 0x0046b0a0 (eax = widget, ecx = &x, edx = &y), then 0x00465470 (al = 1,
// upper eax bytes = the widget's (eax still held it); ecx = widget; x0, y0, x1, y1). Not executed in recorded runs:
// not verifiable by compare yet.
uint32_t fn_00410750(void* w)
{
    uint32_t l[4];  // l0 = y0, l1 = y1, l2 = x0, l3 = x1
    Field<uint32_t>(w, 0x474) |= 0x4000;
    orig::fn_0046b0a0(u(w), u(&l[2]), u(&l[0]), 0xffffff38u, 0xffffffe8u);
    orig::fn_0046b0a0(u(w), u(&l[3]), u(&l[1]), 0xc8, 0x18);
    return orig::fn_00465470((u(w) & 0xffffff00u) | 1u, u(w), l[2], l[0], l[3], l[1]);
}
static uint32_t fn_00410750_Abi(uint32_t ecx)
{
    return fn_00410750(P(ecx));
}
ABI_ENTRY_00410750(fn_00410750_Entry, fn_00410750_Abi)
HOOK(0x00410750, "fn_00410750", fn_00410750_Entry, nullptr);

// 0x00415bb0 (ecx = screen). Vtable 0x005b9710 slot 7 (no class descriptor). A scrolling text list (guess: credits):
// 0x00493440 (ecx = screen); scroll +0x278 += frame time [0x00604a90] * speed +0x27c; RenderPass_Begin 0x00415dc0
// (esi = renderer, edi = post-process; 0, 1). Lines come from 0x00415b40 (eax = list +0x280 / previous node, edx =
// text buffer, edi = 0x100; stack: screen). Per line index i: y = ((float)(unsigned)i - scroll) * [0x005bce04]
// (-0.0583); y > [0x005bce00] (1.05): not drawn; y < [0x005bcc2c] (-0.05): stop; otherwise renderer vt+0x190(1),
// Text_Draw 0x00495e80 (eax = 0xffc0c0c0, ecx = buffer; font, buffer, 1.4, 0.5, y, 0.5, 0.5, 2), vt+0x194().
// Finally the post-process view restore block. Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00415bb0(void* screen)
{
    // Frame 0x208: +0x10 y, +0x14 line index, +0x18 text buffer (0x200 bytes).
    float y;
    uint32_t index;
    uint8_t buffer[0x200];
    orig::fn_00493440(u(screen));
    const uint32_t list = Field<uint32_t>(screen, 0x280);
    Field<float>(screen, 0x278) = kFrameTime * Field<float>(screen, 0x27c) + Field<float>(screen, 0x278);
    orig::fn_00415dc0(u(Renderer()), u(PostProcess()), 0, 1);
    index = 0;
    for (uint32_t node = orig::fn_00415b40(list, u(buffer), 0x100, u(screen)); node;
         node = orig::fn_00415b40(node, u(buffer), 0x100, u(screen)))
    {
        y = (static_cast<float>(index) - Field<float>(screen, 0x278)) * GameGlobal<float>(0x005bce04);
        if (!(y > GameGlobal<float>(0x005bce00)))
        {
            if (y < GameGlobal<float>(0x005bcc2c))
                break;
            VCall<void>(Renderer(), 0x190 / 4, 1u);
            const uint32_t font = Font();
            orig::fn_00495e80(0xffc0c0c0u, u(buffer), font, u(buffer), 0x3fb33333u, 0x3f000000u, f2u(y), 0x3f000000u,
                              0x3f000000u, 2);
            VCall<void>(Renderer(), 0x194 / 4);
        }
        ++index;
    }
    RestorePostProcessView();
    return 0;  // original: eax as left by the last renderer call
}
static uint32_t fn_00415bb0_Abi(uint32_t ecx)
{
    return fn_00415bb0(P(ecx));
}
ABI_ENTRY_00415bb0(fn_00415bb0_Entry, fn_00415bb0_Abi)
HOOK(0x00415bb0, "fn_00415bb0", fn_00415bb0_Entry, nullptr);

// 0x00416700 (ecx = decal; stack: flags; ret 4). Decal base vtable 0x005b964c slot 6, the scalar deleting
// destructor: DecalBase_Destruct 0x00416720 (ecx), then when flags bit 0: PMS_heapFree 0x004e08d0 (ecx = heap
// 0x0063a210; decal). Returns the decal. Not executed in recorded runs: not verifiable by compare yet.
uint32_t DecalBase_DeletingDestructor(void* decal, uint32_t flags)
{
    orig::fn_00416720(u(decal));
    if (flags & 1)
        orig::fn_004e08d0(0x0063a210, u(decal));
    return u(decal);
}
static uint32_t DecalBase_DeletingDestructor_Abi(uint32_t ecx, uint32_t a0)
{
    return DecalBase_DeletingDestructor(P(ecx), a0);
}
ABI_ENTRY_00416700(DecalBase_DeletingDestructor_Entry, DecalBase_DeletingDestructor_Abi)
HOOK(0x00416700, "DecalBase_DeletingDestructor", DecalBase_DeletingDestructor_Entry, nullptr);

// 0x004184e0 (ecx = decal; stack: a0; ret 4). Vtable 0x005b96bc slot 9: 0x00416730 (a tail jump to (Game+0x40)
// vt+0x14 with a0 as its stack argument, inlined: same frame), then renderer vt+0x58(decal + 0x80, 0) (world
// matrix). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_004184e0(void* decal, uint32_t a0)
{
    VCall<uint32_t>(Field<void*>(g_game, 0x40), 0x14 / 4, a0);
    return VCall<uint32_t>(Renderer(), 0x58 / 4, u(decal) + 0x80, 0u);
}
static uint32_t fn_004184e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004184e0(P(ecx), a0);
}
ABI_ENTRY_004184e0(fn_004184e0_Entry, fn_004184e0_Abi)
HOOK(0x004184e0, "fn_004184e0", fn_004184e0_Entry, nullptr);

// 0x00418560 / 0x00418570. DecalSystem (vtable 0x005b9678) slots 1 / 0: the class descriptor 0x00588a80
// ("CLASSV(DecalSystem)") and the type id 0x80000008. Not executed in recorded runs: not verifiable by compare yet.
uint32_t DecalSystem_GetClass()
{
    return 0x00588a80;
}
ABI_ENTRY_00418560(DecalSystem_GetClass_Entry, DecalSystem_GetClass)
HOOK(0x00418560, "DecalSystem_GetClass", DecalSystem_GetClass_Entry, nullptr);

uint32_t DecalSystem_GetTypeId()
{
    return 0x80000008u;
}
ABI_ENTRY_00418570(DecalSystem_GetTypeId_Entry, DecalSystem_GetTypeId)
HOOK(0x00418570, "DecalSystem_GetTypeId", DecalSystem_GetTypeId_Entry, nullptr);

// 0x00418580 (ecx = decal). Decal (vtable 0x005b9690, CLASS(Decal)) slot 7: when +0xf68 == 0.0 or [0x005bcbfc]
// (0.1) < +0xf64: +0xf64 = +0xf68 = 0.1 and the shadow/decal manager (Game+0xa7c) counter +0x20 += 1. Returns the
// manager in that case (eax), else the fnstsw word (returned as 0 here). Not executed in recorded runs: not
// verifiable by compare yet.
uint32_t fn_00418580(void* decal)
{
    if (Field<float>(decal, 0xf68) == kZero || kTenth < Field<float>(decal, 0xf64))
    {
        Field<uint32_t>(decal, 0xf64) = 0x3dcccccd;
        Field<uint32_t>(decal, 0xf68) = 0x3dcccccd;
        void* m = Field<void*>(g_game, 0xa7c);
        ++Field<int32_t>(m, 0x20);
        return u(m);
    }
    return 0;
}
static uint32_t fn_00418580_Abi(uint32_t ecx)
{
    return fn_00418580(P(ecx));
}
ABI_ENTRY_00418580(fn_00418580_Entry, fn_00418580_Abi)
HOOK(0x00418580, "fn_00418580", fn_00418580_Entry, nullptr);

// 0x004185d0 / 0x004185e0. Decal (vtable 0x005b9690) slots 1 / 0: the class descriptor 0x00588bd0 ("CLASS(Decal)",
// size 0xf6c) and the type id 0x80000009. Not executed in recorded runs: not verifiable by compare yet.
uint32_t Decal_GetClass()
{
    return 0x00588bd0;
}
ABI_ENTRY_004185d0(Decal_GetClass_Entry, Decal_GetClass)
HOOK(0x004185d0, "Decal_GetClass", Decal_GetClass_Entry, nullptr);

uint32_t Decal_GetTypeId()
{
    return 0x80000009u;
}
ABI_ENTRY_004185e0(Decal_GetTypeId_Entry, Decal_GetTypeId)
HOOK(0x004185e0, "Decal_GetTypeId", Decal_GetTypeId_Entry, nullptr);

// 0x004185f0 (ecx = object). Vtable 0x005b96bc slot 7: byte +0x118 = 1. Not executed in recorded runs: not
// verifiable by compare yet.
void fn_004185f0(void* obj)
{
    Field<uint8_t>(obj, 0x118) = 1;
}
static void fn_004185f0_Abi(uint32_t ecx)
{
    fn_004185f0(P(ecx));
}
ABI_ENTRY_004185f0(fn_004185f0_Entry, fn_004185f0_Abi)
HOOK(0x004185f0, "fn_004185f0", fn_004185f0_Entry, nullptr);

// 0x00418600 (xor eax, eax; ret). The shared "return 0" virtual method: 390 vtable slots (decal base 0x005b964c
// slots 0/1/0x1c/0x1d, object vtables slot 0x29/0x31, ...). Not executed in recorded runs: not verifiable by compare
// yet.
uint32_t ReturnZero()
{
    return 0;
}
ABI_ENTRY_00418600(ReturnZero_Entry, ReturnZero)
HOOK(0x00418600, "ReturnZero", ReturnZero_Entry, nullptr);

// 0x00418a70 (ecx = env map object; stack: a0..a3; ret 0x10). Vtable 0x005a5ab0 slot 6 (load; slot 1 gives the
// inherited CLASSV(Object) descriptor): bytes +0x460/+0x461 = 0, nine dwords at +0x470 = 0, the base load
// 0x004517b0 (same arguments), 0x004546d0 (esi = (Game+0xa84)+0xf1c, edi = object). +0x470 = "cubemap_texture"
// (0x005b963c), +0x490 = 0x7f (byte), +0x492 = 0xa0 (word), +0x494 = 0x19. Properties through 0x00453450 (eax =
// default, ebx = name, esi = a3, edi = &a1 (the caller's argument slot reused as the output); object, a2, 0):
// "EnvMapSize" (default 128) -> +0x480 = +0x484 = floor(log2(|size|)) (0 when 0), "EnvMapBpp" (default 32) ->
// +0x488 = +0x48c = (bpp == 32 ? 0 : 5). Floats through 0x004534a0 (esi = a3, edi = target; object, name, a2,
// default, 0): "EnvMapFreq" -> +0x468 (60.0), "EnvMapNearClip" -> +0x464 (2.5). Then 0x004546d0 (esi = (Game+0xa84)
// +0xf0c, edi = object); when byte +0x461 is 0: +0x461 = 1, +0x498 = 1; object vt+0xf4(0). Not executed in recorded
// runs: not verifiable by compare yet. Original quirk: size = INT_MIN loops forever (sar keeps it negative).
uint32_t EnvMap_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Field<uint8_t>(obj, 0x460) = 0;
    Field<uint8_t>(obj, 0x461) = 0;
    std::memset(&Field<uint32_t>(obj, 0x470), 0, 9 * 4);
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xf1c), u(obj));
    Field<uint32_t>(obj, 0x470) = 0x005b963c;
    Field<uint8_t>(obj, 0x490) = 0x7f;
    Field<uint16_t>(obj, 0x492) = 0xa0;
    Field<uint32_t>(obj, 0x494) = 0x19;
    orig::fn_00453450(0x80, 0x005b9630, a3, u(&a1), u(obj), a2, 0);
    int32_t v = static_cast<int32_t>(a1);
    if (v != 0)
    {
        if (v < 0)
            v = -v;
        v >>= 1;
        int32_t n = 0;
        while (v != 0)
        {
            v >>= 1;
            ++n;
        }
        v = n;
    }
    Field<int32_t>(obj, 0x484) = v;
    Field<int32_t>(obj, 0x480) = v;
    orig::fn_00453450(0x20, 0x005b9624, a3, u(&a1), u(obj), a2, 0);
    const uint32_t format = a1 == 0x20 ? 0u : 5u;
    Field<uint32_t>(obj, 0x48c) = format;
    Field<uint32_t>(obj, 0x488) = format;
    orig::fn_004534a0(a3, u(obj) + 0x468, u(obj), 0x005b9618, a2, 0x42700000u, 0);
    orig::fn_004534a0(a3, u(obj) + 0x464, u(obj), 0x005b9608, a2, 0x40200000u, 0);
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xf0c), u(obj));
    if (!Field<uint8_t>(obj, 0x461))
    {
        Field<uint8_t>(obj, 0x461) = 1;
        Field<uint32_t>(obj, 0x498) = 1;
    }
    return VCall<uint32_t>(obj, 0xf4 / 4, 0u);
}
static uint32_t EnvMap_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return EnvMap_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00418a70(EnvMap_Load_Entry, EnvMap_Load_Abi)
HOOK(0x00418a70, "EnvMap_Load", EnvMap_Load_Entry, nullptr);

// 0x00418c00 (ecx = env map object). Vtable 0x005a5ab0 slot 0x37 (+0xdc, the remove slot; FxObject's is 0x00418db0):
// EnvMap_Release 0x00418c30 (esi = object), 0x00454750 (esi = (Game+0xa84)+0xf0c, edi = object), then a tail jump to
// Object_Remove 0x004547b0 (ecx = object; frame intact). Not executed in recorded runs: not verifiable by compare
// yet.
uint32_t EnvMap_Remove(void* obj)
{
    orig::fn_00418c30(u(obj));
    orig::fn_00454750(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xf0c), u(obj));
    return orig::fn_004547b0(u(obj));
}
static uint32_t EnvMap_Remove_Abi(uint32_t ecx)
{
    return EnvMap_Remove(P(ecx));
}
ABI_ENTRY_00418c00(EnvMap_Remove_Entry, EnvMap_Remove_Abi)
HOOK(0x00418c00, "EnvMap_Remove", EnvMap_Remove_Entry, nullptr);

// 0x00418d40 (ecx = env map object). Vtable 0x005a5ab0 slot 0x2f (+0xbc): timer +0x46c = frame time [0x00604a90] *
// EnvMapFreq +0x468 + +0x46c. When it is not below 1.0 and byte +0x461 is 0: +0x461 = 1, +0x498 = 1. When +0x461 is
// set: the global 0x0063a1cc (the env map to refresh next; guess) = object when it is null or its timer is not
// greater than this one's (not +0x46c < its +0x46c). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00418d40(void* obj)
{
    const float t = kFrameTime * Field<float>(obj, 0x468) + Field<float>(obj, 0x46c);
    Field<float>(obj, 0x46c) = t;
    if (!(t < kOne) && !Field<uint8_t>(obj, 0x461))
    {
        Field<uint8_t>(obj, 0x461) = 1;
        Field<uint32_t>(obj, 0x498) = 1;
    }
    if (!Field<uint8_t>(obj, 0x461))
        return 0;
    void* cur = GameGlobal<void*>(0x0063a1cc);
    if (cur && Field<float>(obj, 0x46c) < Field<float>(cur, 0x46c))
        return u(cur);
    GameGlobal<void*>(0x0063a1cc) = obj;
    return u(cur);
}
static uint32_t fn_00418d40_Abi(uint32_t ecx)
{
    return fn_00418d40(P(ecx));
}
ABI_ENTRY_00418d40(fn_00418d40_Entry, fn_00418d40_Abi)
HOOK(0x00418d40, "fn_00418d40", fn_00418d40_Entry, nullptr);

// 0x00418db0 (ecx = fx object). FxObject (vtable 0x005a52d8) slot 0x37 (+0xdc, remove): frees +0x518, +0x514,
// +0x4ec (PMS_heapFree 0x004e08d0, heap 0x0063a210) and clears them; stops the 8 streams +0x4f4.. (Streamer_Stop
// 0x00494f10 eax = stream; stack: Game+0xa94); the particle array +0x4f0 (0x194-byte entries, up to 10 while byte
// +0 has bit 3): entry+0x190 refcount (+4) released (freed at 0), entry+0xec texture refcount (+0x28) released
// (at 0: vt+0xd8(), when it returns false 0x00487da0 eax = texture); frees +0x4f0; +0x634 effect: PostEffects_Stop
// 0x0047bfe0 (edi = Game+0xa9c; effect, 0) when Game+0xa9c is set, +0x634 = 0; flag 0x1000000 of +0x460:
// LightSlot_Release 0x0048a520 (eax = +0x5cc; stack: Game+0xa78); +0x460 = +0x464 = 0; +0x55c set: (Game+0x2c)
// vt+0xd8(&+0x55c, 0); +0x4e0 set: 0x004e5130(Game+0x30, &+0x4e0); then a tail jump to Object_Remove 0x004547b0
// (ecx = object; frame intact). Not executed in recorded runs: not verifiable by compare yet.
uint32_t FxObject_Remove(void* obj)
{
    static const uint32_t kFreed[3] = {0x518u, 0x514u, 0x4ecu};
    for (uint32_t off : kFreed)
    {
        if (const uint32_t p = Field<uint32_t>(obj, off))
        {
            orig::fn_004e08d0(0x0063a210, p);
            Field<uint32_t>(obj, off) = 0;
        }
    }
    for (uint32_t i = 0; i < 8; ++i)
    {
        uint32_t& s = Field<uint32_t>(obj, 0x4f4 + i * 4);
        if (s)
        {
            orig::fn_00494f10(s, Field<uint32_t>(g_game, 0xa94));
            s = 0;
        }
    }
    if (uint8_t* e = Field<uint8_t*>(obj, 0x4f0))
    {
        for (uint32_t i = 0; i < 10 && (e[0] & 8); ++i, e += 0x194)
        {
            if (uint8_t* rc = Field<uint8_t*>(e, 0x190))
            {
                --Field<int32_t>(rc, 4);
                rc = Field<uint8_t*>(e, 0x190);
                if (Field<int32_t>(rc, 4) == 0)
                    orig::fn_004e08d0(0x0063a210, u(rc));
            }
            if (void* tex = Field<void*>(e, 0xec))
            {
                if (--Field<int32_t>(tex, 0x28) == 0)
                {
                    if (!static_cast<uint8_t>(VCall<uint32_t>(tex, 0xd8 / 4)))
                        orig::fn_00487da0(u(tex));
                }
            }
        }
        if (const uint32_t p = Field<uint32_t>(obj, 0x4f0))
        {
            orig::fn_004e08d0(0x0063a210, p);
            Field<uint32_t>(obj, 0x4f0) = 0;
        }
    }
    if (const uint32_t effect = Field<uint32_t>(obj, 0x634))
    {
        if (const uint32_t pe = Field<uint32_t>(g_game, 0xa9c))
            orig::fn_0047bfe0(pe, effect, 0);
        Field<uint32_t>(obj, 0x634) = 0;
    }
    if (Field<uint32_t>(obj, 0x460) & 0x1000000)
        orig::fn_0048a520(Field<uint32_t>(obj, 0x5cc), Field<uint32_t>(g_game, 0xa78));
    Field<uint32_t>(obj, 0x460) = 0;
    Field<uint32_t>(obj, 0x464) = 0;
    if (Field<uint32_t>(obj, 0x55c))
        VCall<void>(Field<void*>(g_game, 0x2c), 0xd8 / 4, u(obj) + 0x55c, 0u);
    if (Field<uint32_t>(obj, 0x4e0))
        orig::fn_004e5130(Field<uint32_t>(g_game, 0x30), u(obj) + 0x4e0);
    return orig::fn_004547b0(u(obj));
}
static uint32_t FxObject_Remove_Abi(uint32_t ecx)
{
    return FxObject_Remove(P(ecx));
}
ABI_ENTRY_00418db0(FxObject_Remove_Entry, FxObject_Remove_Abi)
HOOK(0x00418db0, "FxObject_Remove", FxObject_Remove_Entry, nullptr);

// 0x00418f60 (ecx = fx object; stack: a0..a3; ret 0x10). FxObject (vtable 0x005a52d8) slot 6 (load): the base load
// 0x004517b0 (same arguments), clears +0x634, +0x460, +0x464, +0x468, +0x48c, +0x558, +0x5c8, +0x51c, +0x520,
// +0x518, +0x514, +0x4ec, +0x4f0, the 8 streams +0x4f4, +0x55c, +0x4e0, +0x554; +0x5d0 = -1; then 0x004546d0 (esi =
// (Game+0xa84)+0xedc, edi = object). Returns its eax. Not executed in recorded runs: not verifiable by compare yet.
uint32_t FxObject_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    Field<uint32_t>(obj, 0x634) = 0;
    Field<uint32_t>(obj, 0x460) = 0;
    Field<uint32_t>(obj, 0x464) = 0;
    Field<uint32_t>(obj, 0x468) = 0;
    Field<uint32_t>(obj, 0x48c) = 0;
    Field<uint32_t>(obj, 0x5d0) = 0xffffffffu;
    Field<uint32_t>(obj, 0x558) = 0;
    Field<uint32_t>(obj, 0x5c8) = 0;
    Field<uint32_t>(obj, 0x51c) = 0;
    Field<uint32_t>(obj, 0x520) = 0;
    Field<uint32_t>(obj, 0x518) = 0;
    Field<uint32_t>(obj, 0x514) = 0;
    Field<uint32_t>(obj, 0x4ec) = 0;
    Field<uint32_t>(obj, 0x4f0) = 0;
    std::memset(&Field<uint32_t>(obj, 0x4f4), 0, 8 * 4);
    Field<uint32_t>(obj, 0x55c) = 0;
    Field<uint32_t>(obj, 0x4e0) = 0;
    Field<uint32_t>(obj, 0x554) = 0;
    return static_cast<uint32_t>(orig::fn_004546d0(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xedc), u(obj)));
}
static uint32_t FxObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return FxObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00418f60(FxObject_Load_Entry, FxObject_Load_Abi)
HOOK(0x00418f60, "FxObject_Load", FxObject_Load_Entry, nullptr);

// 0x00419060 (ecx = fx object). FxObject (vtable 0x005a52d8) slot 0x30 (+0xc0, draw): nothing when byte +0x464 has
// bit 7 (hidden); flag 0x10000000 of +0x460: Object_DrawGroups 0x0048a540 (edi = object; stack: Game+0xa78);
// otherwise, unless +0x464 bit 8, a tail jump to the base draw 0x00454530 (ecx = object, no stack arguments, frame
// intact; reginputs: only ecx). Returns +0x464 in the early-out cases (eax). recover.py's size (396) runs into the
// next function at 0x004190a0, not part of this one. Not executed in recorded runs: not verifiable by compare yet.
uint32_t FxObject_Draw(void* obj)
{
    const uint32_t f464 = Field<uint32_t>(obj, 0x464);
    if (f464 & 0x80)
        return f464;
    if (Field<uint32_t>(obj, 0x460) & 0x10000000)
        return orig::fn_0048a540(u(obj), Field<uint32_t>(g_game, 0xa78));
    if (f464 & 0x100)
        return f464;
    return GameFunction<uint32_t(__fastcall*)(void*, void*)>(0x00454530)(obj, nullptr);
}
static uint32_t FxObject_Draw_Abi(uint32_t ecx)
{
    return FxObject_Draw(P(ecx));
}
ABI_ENTRY_00419060(FxObject_Draw_Entry, FxObject_Draw_Abi)
HOOK(0x00419060, "FxObject_Draw", FxObject_Draw_Entry, nullptr);

// 0x00419250 (eax = fx object): returns +0x638 (the value 0x004190xx/0x00488130's caller stores there; r2a). No
// vtable or code reference found (no callers). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00419250(uint32_t eax)
{
    return Field<uint32_t>(P(eax), 0x638);
}
ABI_ENTRY_00419250(fn_00419250_Entry, fn_00419250)
HOOK(0x00419250, "fn_00419250", fn_00419250_Entry, nullptr);

// 0x00419260 (ecx = fx object). FxObject (vtable 0x005a52d8) slot 0xc (the in-frustum slot; base Object_InFrustum
// 0x00454360): always visible (al = 1; upper eax bytes as left by the caller) when +0x460 has 0x10000000, otherwise a
// tail jump to Object_InFrustum (ecx, frame intact). Not executed in recorded runs: not verifiable by compare yet.
uint32_t FxObject_InFrustum(void* obj)
{
    if (Field<uint32_t>(obj, 0x460) & 0x10000000)
        return 1;
    return orig::fn_00454360(u(obj));
}
static uint32_t FxObject_InFrustum_Abi(uint32_t ecx)
{
    return FxObject_InFrustum(P(ecx));
}
ABI_ENTRY_00419260(FxObject_InFrustum_Entry, FxObject_InFrustum_Abi)
HOOK(0x00419260, "FxObject_InFrustum", FxObject_InFrustum_Entry, nullptr);

// 0x00419280 (ecx = object). Slot 0x3a (+0xe8) of most object vtables (162 slots): 1 when vt+0xe4() returns al == 0,
// else 0 (neg al; sbb; inc). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00419280(void* obj)
{
    return static_cast<uint8_t>(VCall<uint32_t>(obj, 0xe4 / 4)) == 0 ? 1u : 0u;
}
static uint32_t fn_00419280_Abi(uint32_t ecx)
{
    return fn_00419280(P(ecx));
}
ABI_ENTRY_00419280(fn_00419280_Entry, fn_00419280_Abi)
HOOK(0x00419280, "fn_00419280", fn_00419280_Entry, nullptr);

// 0x00419330 (ecx = fx object; stack: a0..a4; ret 0x14). FxObject (vtable 0x005a52d8) slot 0x2a: +0x464 |= 0x2000,
// +0x544..+0x54c = the vector *a1, +0x554 = a4, +0x550 = a0, then 0x004548f0 (ecx = object; a0..a4). Not executed in
// recorded runs: not verifiable by compare yet.
uint32_t fn_00419330(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    Field<uint32_t>(obj, 0x464) |= 0x2000;
    const uint32_t* v = static_cast<const uint32_t*>(P(a1));
    Field<uint32_t>(obj, 0x544) = v[0];
    Field<uint32_t>(obj, 0x548) = v[1];
    Field<uint32_t>(obj, 0x54c) = v[2];
    Field<uint32_t>(obj, 0x554) = a4;
    Field<uint32_t>(obj, 0x550) = a0;
    return orig::fn_004548f0(u(obj), a0, a1, a2, a3, a4);
}
static uint32_t fn_00419330_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_00419330(P(ecx), a0, a1, a2, a3, a4);
}
ABI_ENTRY_00419330(fn_00419330_Entry, fn_00419330_Abi)
HOOK(0x00419330, "fn_00419330", fn_00419330_Entry, nullptr);

// 0x00419390 (ecx = fx object; stack: out; ret 4). FxObject (vtable 0x005a52d8) slot 0x23: *out = +0x48c; returns it.
// Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00419390(void* obj, uint32_t out)
{
    const uint32_t v = Field<uint32_t>(obj, 0x48c);
    *static_cast<uint32_t*>(P(out)) = v;
    return v;
}
static uint32_t fn_00419390_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00419390(P(ecx), a0);
}
ABI_ENTRY_00419390(fn_00419390_Entry, fn_00419390_Abi)
HOOK(0x00419390, "fn_00419390", fn_00419390_Entry, nullptr);

// 0x00419920 (ecx = fx object). FxObject (vtable 0x005a52d8) slot 0x31: (+0x464 & 0x200 ? 0x40 : 0) | (+0x464 &
// 0x400 ? 0x20 : 0). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00419920(void* obj)
{
    const uint32_t f = Field<uint32_t>(obj, 0x464);
    uint32_t r = 0;
    if (f & 0x200)
        r = 0x40;
    if (f & 0x400)
        r |= 0x20;
    return r;
}
static uint32_t fn_00419920_Abi(uint32_t ecx)
{
    return fn_00419920(P(ecx));
}
ABI_ENTRY_00419920(fn_00419920_Entry, fn_00419920_Abi)
HOOK(0x00419920, "fn_00419920", fn_00419920_Entry, nullptr);

// 0x00423900 (ecx = level properties object; stack: a0..a3; ret 0x10). Vtable 0x005acde0 slot 6 (load; slot 1 gives
// the inherited CLASSV(Object) descriptor). Level-wide settings read with the property readers 0x004534a0 (float:
// esi = a3, edi = target; object, name, a2, default, 0), 0x00453400 (int: eax = default, esi = a3, edi = target;
// object, name, a2, 0), 0x00453450 (eax = 0, ebx = name, esi = a3, edi = target; object, a2, 0) and 0x004533a0
// (string: eax = default 0x0058a9a4 "", edi = target; object, name, a2, a3, 0). Render part first: the base load
// 0x004517b0; fog_near (10) -> l0, fog_far (3000) -> the a0 slot, fog_red/green/blue (255) -> l1..l3, clip_far (0) ->
// the a3 slot, clip_near (0) -> the a1 slot (the callee-popped argument slots are reused as outputs; locals here,
// started with the incoming values). clip_far == 0: sqrt(s*s + s*s) + fog_far with s = (Game+0xa60)+0x2c
// (0x00449440 twice). clip_near == 0: Camera_GetNear 0x0040f850 (eax = camera Game+0xa68). Colour *= [0x005bcc18]
// (1/255). Then 0x0040f8b0 (ecx = camera; clip_far) (far clip), Camera_SetNear 0x0040f890 (eax = camera;
// clip_near), fog fn_0040f970 (eax = camera, ecx = &colour; fog_near, fog_far). The rest stores gameplay tuning in
// the Game object (army_*, autoaim_*, pickup_* (percent values * [0x005bcbec] 0.01; pickup_drop_factor < 0.01 ->
// 0), weapon ammo/clip/range, topsy_turvy_*, bof_*, camera_*, music_*), then object vt+0xf4(0). Not executed in
// recorded runs: not verifiable by compare yet. Subsystem: level properties (game), with the fog/clip setup.
uint32_t LevelProperties_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const float& kPercent = GameGlobal<float>(0x005bcbec);  // 0.01
    auto G = [] { return u(g_game); };
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    float fogNear, colour[3];  // frame l0, l1..l3 (uninitialised in the original)
    float fogFar = u2f(a0), clipFar = u2f(a3), clipNear = u2f(a1);
    orig::fn_004534a0(a3, u(&fogNear), u(obj), 0x005b7dd0 /* "fog_near" */, a2, 0x41200000u, 0);
    orig::fn_004534a0(a3, u(&fogFar), u(obj), 0x005b7dc8 /* "fog_far" */, a2, 0x453b8000u, 0);
    orig::fn_004534a0(a3, u(&colour[0]), u(obj), 0x005b7dc0 /* "fog_red" */, a2, 0x437f0000u, 0);
    orig::fn_004534a0(a3, u(&colour[1]), u(obj), 0x005b7db4 /* "fog_green" */, a2, 0x437f0000u, 0);
    orig::fn_004534a0(a3, u(&colour[2]), u(obj), 0x005b7da8 /* "fog_blue" */, a2, 0x437f0000u, 0);
    orig::fn_004534a0(a3, u(&clipFar), u(obj), 0x005b7d9c /* "clip_far" */, a2, 0, 0);
    orig::fn_004534a0(a3, u(&clipNear), u(obj), 0x005b7d90 /* "clip_near" */, a2, 0, 0);
    void* game = g_game;
    if (clipFar == kZero)
    {
        const uint32_t level = Field<uint32_t>(game, 0xa60);
        const float s0 = orig::fn_00449440(level);
        const float s1 = orig::fn_00449440(level);
        const float p = s0 * s1;
        clipFar = std::sqrt(p + p) + fogFar;
    }
    if (clipNear == kZero)
        clipNear = orig::fn_0040f850(Field<uint32_t>(game, 0xa68));
    const float k = GameGlobal<float>(0x005bcc18);  // 1/255
    colour[0] = colour[0] * k;
    colour[1] = colour[1] * k;
    colour[2] = colour[2] * k;
    orig::fn_0040f8b0(Field<uint32_t>(game, 0xa68), f2u(clipFar));
    orig::fn_0040f890(Field<uint32_t>(game, 0xa68), f2u(clipNear));
    orig::fn_0040f970(Field<uint32_t>(game, 0xa68), u(colour), f2u(fogNear), f2u(fogFar));
    orig::fn_00453400(0x10, a3, G() + 0xbf4, u(obj), 0x5b7d80 /* "army_max_grunts" */, a2, 0);  // 0x423a91
    Field<uint32_t>(g_game, 0xbf0) = Field<uint32_t>(g_game, 0xbf4);
    orig::fn_00453400(0xe, a3, G() + 0xbf8, u(obj), 0x5b7d6c /* "army_unpause_count" */, a2, 0);  // 0x423abb
    Field<uint32_t>(g_game, 0xbfc) = 0;
    Field<uint32_t>(g_game, 0xc0c) = 0;
    orig::fn_00453450(0, 0x5b7d54 /* "army_marker1_tag_min" */, a3, G() + 0xc00, u(obj), a2, 0);  // 0x423ae3
    orig::fn_00453450(0, 0x5b7d3c /* "army_marker1_tag_max" */, a3, G() + 0xc10, u(obj), a2, 0);  // 0x423b03
    orig::fn_00453450(0, 0x5b7d24 /* "army_marker2_tag_min" */, a3, G() + 0xc04, u(obj), a2, 0);  // 0x423b23
    orig::fn_00453450(0, 0x5b7d0c /* "army_marker2_tag_max" */, a3, G() + 0xc14, u(obj), a2, 0);  // 0x423b43
    orig::fn_00453450(0, 0x5b7cf4 /* "army_marker3_tag_min" */, a3, G() + 0xc08, u(obj), a2, 0);  // 0x423b63
    orig::fn_00453450(0, 0x5b7cdc /* "army_marker3_tag_max" */, a3, G() + 0xc18, u(obj), a2, 0);  // 0x423b83
    Field<uint32_t>(g_game, 0xc1c) = 0;
    Field<uint32_t>(g_game, 0xc2c) = 0;
    orig::fn_00453450(0, 0x5b7cc4 /* "army_ladder1_tag_min" */, a3, G() + 0xc20, u(obj), a2, 0);  // 0x423baf
    orig::fn_00453450(0, 0x5b7cac /* "army_ladder1_tag_max" */, a3, G() + 0xc30, u(obj), a2, 0);  // 0x423bcf
    orig::fn_00453450(0, 0x5b7c94 /* "army_ladder2_tag_min" */, a3, G() + 0xc24, u(obj), a2, 0);  // 0x423bef
    orig::fn_00453450(0, 0x5b7c7c /* "army_ladder2_tag_max" */, a3, G() + 0xc34, u(obj), a2, 0);  // 0x423c0f
    orig::fn_00453450(0, 0x5b7c64 /* "army_ladder3_tag_min" */, a3, G() + 0xc28, u(obj), a2, 0);  // 0x423c2f
    orig::fn_00453450(0, 0x5b7c4c /* "army_ladder3_tag_max" */, a3, G() + 0xc38, u(obj), a2, 0);  // 0x423c4f
    orig::fn_004534a0(a3, G() + 0x2734, u(obj), 0x5b7c38 /* "autoaim_factor_horz" */, a2, 0x3e3851ecu /* 0.18 */, 0);  // 0x423c72
    orig::fn_004534a0(a3, G() + 0x2738, u(obj), 0x5b7c24 /* "autoaim_factor_vert" */, a2, 0x3ecccccdu /* 0.4 */, 0);  // 0x423c91
    orig::fn_004534a0(a3, G() + 0x273c, u(obj), 0x5b7c04 /* "autoaim_multitarget_factor_horz" */, a2, 0x3ecccccdu /* 0.4 */, 0);  // 0x423cb0
    orig::fn_004534a0(a3, G() + 0x2740, u(obj), 0x5b7be4 /* "autoaim_multitarget_factor_vert" */, a2, 0x3f19999au /* 0.6 */, 0);  // 0x423ccf
    orig::fn_004534a0(a3, G() + 0x2744, u(obj), 0x5b7bc8 /* "autoaim_base_factor_horz" */, a2, 0x3e4ccccdu /* 0.2 */, 0);  // 0x423cee
    orig::fn_004534a0(a3, G() + 0x2748, u(obj), 0x5b7bac /* "autoaim_base_factor_vert" */, a2, 0x3e99999au /* 0.3 */, 0);  // 0x423d0d
    orig::fn_004534a0(a3, G() + 0x2754, u(obj), 0x5b7b90 /* "autoaim_sticky_factor_horz" */, a2, 0x3ecccccdu /* 0.4 */, 0);  // 0x423d2c
    orig::fn_004534a0(a3, G() + 0x2758, u(obj), 0x5b7b74 /* "autoaim_sticky_factor_vert" */, a2, 0x3f19999au /* 0.6 */, 0);  // 0x423d4b
    orig::fn_004534a0(a3, G() + 0x2764, u(obj), 0x5b7b54 /* "autoaim_sharkgun_factor_horz" */, a2, 0x3ecccccdu /* 0.4 */, 0);  // 0x423d6a
    orig::fn_004534a0(a3, G() + 0x2768, u(obj), 0x5b7b34 /* "autoaim_sharkgun_factor_vert" */, a2, 0x3f19999au /* 0.6 */, 0);  // 0x423d89
    orig::fn_004534a0(a3, G() + 0x274c, u(obj), 0x5b7b1c /* "autoaim_bof_factor_horz" */, a2, 0x3e4ccccdu /* 0.2 */, 0);  // 0x423da8
    orig::fn_004534a0(a3, G() + 0x2750, u(obj), 0x5b7b04 /* "autoaim_bof_factor_vert" */, a2, 0x3e99999au /* 0.3 */, 0);  // 0x423dc7
    orig::fn_004534a0(a3, G() + 0x276c, u(obj), 0x5b7af0 /* "camera_zoom_secs" */, a2, 0x3f000000u /* 0.5 */, 0);  // 0x423de6
    orig::fn_004534a0(a3, G() + 0xc60, u(obj), 0x5b7adc /* "pickup_drop_factor" */, a2, 0x3f800000u /* 1 */, 0);  // 0x423e05
    if (Field<float>(g_game, 0xc60) < kPercent)  // pickup_drop_factor below 0.01 -> 0
        Field<uint32_t>(g_game, 0xc60) = 0;
    orig::fn_004534a0(a3, G() + 0xc64, u(obj), 0x5b7abc /* "pickup_low_health_thresh_pct" */, a2, 0x41c80000u /* 25 */, 0);  // 0x423e41
    Field<float>(g_game, 0xc64) = Field<float>(g_game, 0xc64) * kPercent;
    orig::fn_004534a0(a3, G() + 0xc68, u(obj), 0x5b7aa0 /* "pickup_health_weight_low" */, a2, 0x42480000u /* 50 */, 0);  // 0x423e71
    Field<float>(g_game, 0xc68) = Field<float>(g_game, 0xc68) * kPercent;
    orig::fn_004534a0(a3, G() + 0xc6c, u(obj), 0x5b7a84 /* "pickup_health_weight_normal" */, a2, 0x41c80000u /* 25 */, 0);  // 0x423ea1
    Field<float>(g_game, 0xc6c) = Field<float>(g_game, 0xc6c) * kPercent;
    orig::fn_004534a0(a3, G() + 0xc74, u(obj), 0x5b7a68 /* "pickup_cur_weapon_weight" */, a2, 0x42480000u /* 50 */, 0);  // 0x423ed1
    Field<float>(g_game, 0xc74) = Field<float>(g_game, 0xc74) * kPercent;
    orig::fn_004534a0(a3, G() + 0xc70, u(obj), 0x5b7a4c /* "pickup_noncur_weapon_weight" */, a2, 0x41c80000u /* 25 */, 0);  // 0x423f01
    Field<float>(g_game, 0xc70) = Field<float>(g_game, 0xc70) * kPercent;
    orig::fn_004534a0(a3, G() + 0xc78, u(obj), 0x5b7a30 /* "pickup_sticky_bomb_weight" */, a2, 0x41c80000u /* 25 */, 0);  // 0x423f31
    Field<float>(g_game, 0xc78) = Field<float>(g_game, 0xc78) * kPercent;
    orig::fn_00453400(0, a3, G() + 0x28b4, u(obj), 0x5b7a1c /* "machinegun_max_ammo" */, a2, 0);  // 0x423f5e
    orig::fn_00453400(0, a3, G() + 0x28b0, u(obj), 0x5b7a08 /* "machinegun_max_clip" */, a2, 0);  // 0x423f7a
    orig::fn_00453400(0, a3, G() + 0x2a04, u(obj), 0x5b79f4 /* "snipergun_max_ammo" */, a2, 0);  // 0x423f96
    orig::fn_00453400(0, a3, G() + 0x2a00, u(obj), 0x5b79e0 /* "snipergun_max_clip" */, a2, 0);  // 0x423fb2
    orig::fn_00453400(0, a3, G() + 0x2b54, u(obj), 0x5b79d0 /* "rifle_max_ammo" */, a2, 0);  // 0x423fce
    orig::fn_00453400(0, a3, G() + 0x2b50, u(obj), 0x5b79c0 /* "rifle_max_clip" */, a2, 0);  // 0x423fea
    orig::fn_00453400(0, a3, G() + 0x2df4, u(obj), 0x5b79b0 /* "gurner_max_ammo" */, a2, 0);  // 0x424006
    orig::fn_00453400(0, a3, G() + 0x2df0, u(obj), 0x5b79a0 /* "gurner_max_clip" */, a2, 0);  // 0x424022
    orig::fn_00453400(0, a3, G() + 0x2f44, u(obj), 0x5b798c /* "sharkgun_max_ammo" */, a2, 0);  // 0x42403e
    orig::fn_00453400(0, a3, G() + 0x2f40, u(obj), 0x5b7978 /* "sharkgun_max_clip" */, a2, 0);  // 0x42405a
    orig::fn_00453400(0, a3, G() + 0x2ca4, u(obj), 0x5b7964 /* "vindaloo_max_ammo" */, a2, 0);  // 0x424076
    orig::fn_00453400(0, a3, G() + 0x2ca0, u(obj), 0x5b7950 /* "vindaloo_max_clip" */, a2, 0);  // 0x424092
    orig::fn_00453400(5, a3, G() + 0x35d4, u(obj), 0x5b793c /* "stickybomb_max_ammo" */, a2, 0);  // 0x4240b1
    orig::fn_00453400(5, a3, G() + 0x3094, u(obj), 0x5b7928 /* "guyfawkes_max_ammo" */, a2, 0);  // 0x4240d0
    orig::fn_00453400(5, a3, G() + 0x31e4, u(obj), 0x5b7910 /* "bunchoffives_max_ammo" */, a2, 0);  // 0x4240ef
    orig::fn_00453400(5, a3, G() + 0x3484, u(obj), 0x5b78fc /* "blackhole_max_ammo" */, a2, 0);  // 0x42410e
    orig::fn_00453400(5, a3, G() + 0x3334, u(obj), 0x5b78e8 /* "topsyturvy_max_ammo" */, a2, 0);  // 0x42412d
    orig::fn_004534a0(a3, G() + 0x28b8, u(obj), 0x5b78d4 /* "machinegun_range" */, a2, 0x41200000u /* 10 */, 0);  // 0x42414c
    orig::fn_004534a0(a3, G() + 0x28bc, u(obj), 0x5b78bc /* "machinegun_spread_pct" */, a2, 0x3f800000u /* 1 */, 0);  // 0x42416b
    orig::fn_004534a0(a3, G() + 0x2a08, u(obj), 0x5b78ac /* "snipergun_range" */, a2, 0x41200000u /* 10 */, 0);  // 0x42418a
    orig::fn_004534a0(a3, G() + 0x2b58, u(obj), 0x5b78a0 /* "rifle_range" */, a2, 0x41200000u /* 10 */, 0);  // 0x4241a9
    orig::fn_004534a0(a3, G() + 0x2df8, u(obj), 0x5b7890 /* "gurner_range" */, a2, 0x41200000u /* 10 */, 0);  // 0x4241c8
    orig::fn_004534a0(a3, G() + 0x2f48, u(obj), 0x5b7880 /* "sharkgun_range" */, a2, 0x41200000u /* 10 */, 0);  // 0x4241e7
    orig::fn_004534a0(a3, G() + 0x2ca8, u(obj), 0x5b7870 /* "vindaloo_range" */, a2, 0x41200000u /* 10 */, 0);  // 0x424206
    orig::fn_004534a0(a3, G() + 0x35d8, u(obj), 0x5b785c /* "sticky_bomb_range" */, a2, 0x41200000u /* 10 */, 0);  // 0x424225
    orig::fn_004534a0(a3, G() + 0x3098, u(obj), 0x5b7848 /* "guy_fawkes_range" */, a2, 0x41200000u /* 10 */, 0);  // 0x424244
    orig::fn_004534a0(a3, G() + 0x3488, u(obj), 0x5b7834 /* "black_hole_range" */, a2, 0x42480000u /* 50 */, 0);  // 0x424263
    orig::fn_004534a0(a3, G() + 0x31e8, u(obj), 0x5b7828 /* "bof_range" */, a2, 0x43fa0000u /* 500 */, 0);  // 0x424282
    orig::fn_004534a0(a3, G() + 0x3338, u(obj), 0x5b7814 /* "topsy_turvy_range" */, a2, 0x41200000u /* 10 */, 0);  // 0x4242a1
    orig::fn_004534a0(a3, G() + 0x483c, u(obj), 0x5b77fc /* "base_defend_spread_pct" */, a2, 0x40000000u /* 2 */, 0);  // 0x4242c0
    orig::fn_004534a0(a3, G() + 0xc7c, u(obj), 0x5b77e4 /* "topsy_turvy_duration" */, a2, 0x40a00000u /* 5 */, 0);  // 0x4242df
    orig::fn_004534a0(a3, G() + 0xc84, u(obj), 0x5b77d0 /* "topsy_turvy_radius" */, a2, 0x42c80000u /* 100 */, 0);  // 0x4242fe
    orig::fn_004533a0(0x58a9a4, G() + 0xc88, u(obj), 0x5b77b8 /* "topsy_turvy_child_bone" */, a2, a3, 0);  // 0x42431e
    orig::fn_00453400(0x32, a3, G() + 0xca8, u(obj), 0x5b77a0 /* "topsy_turvy_fall_pct" */, a2, 0);  // 0x42433d
    orig::fn_004533a0(0x58a9a4, G() + 0xcdc, u(obj), 0x5b7788 /* "bof_glove_child_bone" */, a2, a3, 0);  // 0x42435d
    orig::fn_004533a0(0x58a9a4, G() + 0xcfc, u(obj), 0x5b7770 /* "bof_glove_player_bone" */, a2, a3, 0);  // 0x42437d
    orig::fn_004534a0(a3, G() + 0xd1c, u(obj), 0x5b775c /* "bof_speed_factor" */, a2, 0x40400000u /* 3 */, 0);  // 0x42439c
    orig::fn_004534a0(a3, G() + 0xd20, u(obj), 0x5b7748 /* "bof_gravity_factor" */, a2, 0x3f800000u /* 1 */, 0);  // 0x4243bb
    orig::fn_004534a0(a3, G() + 0xd24, u(obj), 0x5b7730 /* "bof_hit_force_factor" */, a2, 0x469c4000u /* 20000 */, 0);  // 0x4243da
    orig::fn_004534a0(a3, G() + 0xd28, u(obj), 0x5b7718 /* "bof_up_force_punch_1" */, a2, 0x3dcccccdu /* 0.1 */, 0);  // 0x4243f9
    orig::fn_004534a0(a3, G() + 0xd2c, u(obj), 0x5b7700 /* "bof_up_force_punch_2" */, a2, 0x3e4ccccdu /* 0.2 */, 0);  // 0x424418
    orig::fn_004534a0(a3, G() + 0xd30, u(obj), 0x5b76e8 /* "bof_up_force_punch_3" */, a2, 0x3e99999au /* 0.3 */, 0);  // 0x424437
    orig::fn_004534a0(a3, G() + 0xcac, u(obj), 0x5b76d8 /* "camera_falloff" */, a2, 0x3e99999au /* 0.3 */, 0);  // 0x424456
    orig::fn_004534a0(a3, G() + 0xcb0, u(obj), 0x5b76c4 /* "camera_fade_in_rate" */, a2, 0x40400000u /* 3 */, 0);  // 0x424475
    orig::fn_004534a0(a3, G() + 0xcb4, u(obj), 0x5b76ac /* "camera_fade_out_rate" */, a2, 0x40400000u /* 3 */, 0);  // 0x424494
    orig::fn_004534a0(a3, G() + 0xcb8, u(obj), 0x5b7694 /* "damage_flash_duration" */, a2, 0x3dcccccdu /* 0.1 */, 0);  // 0x4244b3
    orig::fn_004533a0(0x58a9a4, G() + 0xd34, u(obj), 0x5b7680 /* "teammate_command_fx" */, a2, a3, 0);  // 0x4244d3
    orig::fn_004534a0(a3, G() + 0xd60, u(obj), 0x5b7668 /* "twiglet_max_distance" */, a2, 0x447a0000u /* 1000 */, 0);  // 0x4244f2
    orig::fn_004534a0(a3, G() + 0xd64, u(obj), 0x5b7650 /* "alarm_ai_max_distance" */, a2, 0x447a0000u /* 1000 */, 0);  // 0x424511
    orig::fn_00453400(2, a3, G() + 0xd6c, u(obj), 0x5b7638 /* "music_nearby_suspense" */, a2, 0);  // 0x424530
    orig::fn_00453400(4, a3, G() + 0xd70, u(obj), 0x5b7624 /* "music_agro_battle" */, a2, 0);  // 0x42454f
    orig::fn_004534a0(a3, G() + 0xd80, u(obj), 0x5b7610 /* "music_nearby_range" */, a2, 0x42c80000u /* 100 */, 0);  // 0x42456e
    orig::fn_004534a0(a3, G() + 0xd74, u(obj), 0x5b75fc /* "music_agro_range" */, a2, 0x48f42400u /* 500000 */, 0);  // 0x42458d
    orig::fn_004534a0(a3, G() + 0xd84, u(obj), 0x5b75ec /* "music_play_time" */, a2, 0x44160000u /* 600 */, 0);  // 0x4245ac
    orig::fn_004534a0(a3, G() + 0xd88, u(obj), 0x5b75d4 /* "music_play_delay_min" */, a2, 0x41f00000u /* 30 */, 0);  // 0x4245cb
    orig::fn_004534a0(a3, G() + 0xd8c, u(obj), 0x5b75bc /* "music_play_delay_max" */, a2, 0x42700000u /* 60 */, 0);  // 0x4245ea
    return VCall<uint32_t>(obj, 0xf4 / 4, 0u);
}
static uint32_t LevelProperties_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return LevelProperties_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00423900(LevelProperties_Load_Entry, LevelProperties_Load_Abi)
HOOK(0x00423900, "LevelProperties_Load", LevelProperties_Load_Entry, nullptr);

namespace
{
// The draw block shared by 0x00466ce0 / 0x004672d0 / 0x00467dd0: t = (1 - +0x4a0) * 0.5 clamped to [0, 0.5]; a
// 0x44-byte matrix: identity, element 3 = x (bits), 7 = t, 11 = 0, flags +0x40 = 0x10; renderer vt+0x58(&m, 0)
// (world), then vt+0xa4(1, 1, (a0+8)+0x44, 0, 0) (blend state). Returns the last call's eax.
uint32_t DrawSlidePanel(void* obj, void* a0, uint32_t x)
{
    float t = (kOne - Field<float>(obj, 0x4a0)) * kHalf;
    if (t < kZero)
        t = kZero;
    else if (t > kHalf)
        t = kHalf;
    uint32_t m[17] = {};
    m[0] = 0x3f800000u;
    m[5] = 0x3f800000u;
    m[10] = 0x3f800000u;
    m[15] = 0x3f800000u;
    m[3] = x;
    m[7] = f2u(t);
    m[11] = 0;
    m[16] = 0x10;
    VCall<void>(Renderer(), 0x58 / 4, u(m), 0u);
    return VCall<uint32_t>(Renderer(), 0xa4 / 4, 1u, 1u, Field<uint32_t>(Field<void*>(a0, 8), 0x44), 0u, 0u);
}
}  // namespace

// 0x00466ce0 (ecx = object; stack: a0; ret 4). Vtable 0x005a3260 slot 0xa (+0x28; the object base slot 0xa is
// fn_004544a0). Looks up "b_health_level1" (0x005a0c94; "blah" 0x005a0c8c when +0x4a8 is set) under a0 (the inlined
// finder, see FindNamedValue); when a0+4 equals the result, the slide-panel draw with x = +0x4a4, otherwise the base
// fn_004544a0(a0). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00466ce0(void* obj, uint32_t a0)
{
    const uint32_t name = Field<uint32_t>(obj, 0x4a8) ? 0x005a0c8cu : 0x005a0c94u;
    const uint32_t found = FindNamedValue(P(a0), name);
    if (Field<uint32_t>(P(a0), 4) != found)
        return orig::fn_004544a0(a0);
    return DrawSlidePanel(obj, P(a0), Field<uint32_t>(obj, 0x4a4));
}
static uint32_t fn_00466ce0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00466ce0(P(ecx), a0);
}
ABI_ENTRY_00466ce0(fn_00466ce0_Entry, fn_00466ce0_Abi)
HOOK(0x00466ce0, "fn_00466ce0", fn_00466ce0_Entry, nullptr);

// 0x004672d0 (ecx = object; stack: a0; ret 4). Vtable 0x005ab8c0 slot 0xa (+0x28): when +0x474 has bit 1, or +0x470
// bit 0 and +0x474 bit 2, and a0+4 equals the "ui_tickerbomb_set" (0x005b162c) lookup: the slide-panel draw with x =
// 0; otherwise the base fn_004544a0(a0). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_004672d0(void* obj, uint32_t a0)
{
    const uint32_t f = Field<uint32_t>(obj, 0x474);
    if ((f & 2) || ((Field<uint8_t>(obj, 0x470) & 1) && (f & 4)))
    {
        if (Field<uint32_t>(P(a0), 4) == FindNamedValue(P(a0), 0x005b162c))
            return DrawSlidePanel(obj, P(a0), 0);
    }
    return orig::fn_004544a0(a0);
}
static uint32_t fn_004672d0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004672d0(P(ecx), a0);
}
ABI_ENTRY_004672d0(fn_004672d0_Entry, fn_004672d0_Abi)
HOOK(0x004672d0, "fn_004672d0", fn_004672d0_Entry, nullptr);

// 0x00467a20 (ecx = object). Vtable 0x005a5818 slot 0x2f (+0xbc, the update slot). Only when +0x470 bit 0 or +0x474
// bit 1, and Game+0x748 is set: +0x474 bit 2 = 0x00475840 (eax = Game+0x748) && 0x004a1660 (eax = Game+0x748 +
// 0xc24) == 1. on = bit 1 || (+0x470 bit 0 && bit 2); when on differs from byte +0x4a0: on -> PostEffects_AddBlurRings
// 0x0047bf20 (ecx = Game+0xa9c; 2, &{0.5, 0.5, 0.1, 0, 0.5, 0.5, 0.5, 1.0}) and PostEffects_SetTimes 0x0047c1a0
// (eax = Game+0xa9c, ecx = 1, edx = 2; 0.25, -1.0, 0.5); off -> PostEffects_Stop 0x0047bfe0 (edi = Game+0xa9c; 2,
// 0); byte +0x4a0 = on. Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00467a20(void* obj)
{
    if (!(Field<uint8_t>(obj, 0x470) & 1) && !(Field<uint8_t>(obj, 0x474) & 2))
        return 0;  // original: eax as left by the caller
    void* game = g_game;
    const uint32_t a = Field<uint32_t>(game, 0x748);
    if (!a)
        return 0;
    uint32_t f;
    if (static_cast<uint8_t>(orig::fn_00475840(a)) && orig::fn_004a1660(a + 0xc24) == 1)
        f = Field<uint32_t>(obj, 0x474) | 4;
    else
        f = Field<uint32_t>(obj, 0x474) & ~4u;
    Field<uint32_t>(obj, 0x474) = f;
    const uint8_t on = ((f & 2) || ((Field<uint8_t>(obj, 0x470) & 1) && (f & 4))) ? 1 : 0;
    if (on == Field<uint8_t>(obj, 0x4a0))
        return f;
    if (on)
    {
        uint32_t rings[8] = {0x3f000000u, 0x3f000000u, 0x3dcccccdu, 0, 0x3f000000u, 0x3f000000u, 0x3f000000u,
                             0x3f800000u};
        orig::fn_0047bf20(Field<uint32_t>(game, 0xa9c), 2, u(rings));
        const uint32_t r = orig::fn_0047c1a0(Field<uint32_t>(game, 0xa9c), 1, 2, 0x3e800000u, 0xbf800000u,
                                             0x3f000000u);
        Field<uint8_t>(obj, 0x4a0) = on;
        return r;
    }
    const uint32_t r = orig::fn_0047bfe0(Field<uint32_t>(game, 0xa9c), 2, 0);
    Field<uint8_t>(obj, 0x4a0) = on;
    return r;
}
static uint32_t fn_00467a20_Abi(uint32_t ecx)
{
    return fn_00467a20(P(ecx));
}
ABI_ENTRY_00467a20(fn_00467a20_Entry, fn_00467a20_Abi)
HOOK(0x00467a20, "fn_00467a20", fn_00467a20_Entry, nullptr);

// 0x00467dd0 (ecx = object; stack: a0; ret 4). Vtable 0x005adb20 slot 0xa (+0x28): when a0+4 equals the "Blah01"
// (0x005a0c84) lookup: the slide-panel draw with x = 0; otherwise the base fn_004544a0(a0). Not executed in recorded
// runs: not verifiable by compare yet.
uint32_t fn_00467dd0(void* obj, uint32_t a0)
{
    if (Field<uint32_t>(P(a0), 4) != FindNamedValue(P(a0), 0x005a0c84))
        return orig::fn_004544a0(a0);
    return DrawSlidePanel(obj, P(a0), 0);
}
static uint32_t fn_00467dd0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00467dd0(P(ecx), a0);
}
ABI_ENTRY_00467dd0(fn_00467dd0_Entry, fn_00467dd0_Abi)
HOOK(0x00467dd0, "fn_00467dd0", fn_00467dd0_Entry, nullptr);

// 0x0046d450 (ecx = widget, edi passed through; no stack; symbols/abi.csv: no ret found). UI widget vtables
// 0x005abdf0 / 0x005af768 slot 0x4f (+0x13c, draw): UiText_Render 0x00465360 (ecx = widget, edi), renderer
// vt+0x190(1), UiText_Draw 0x00464750 (ecx = widget, ebx = +0x578, edi; Game+0xa70, 0.025, 0, +0x580), then a tail
// jump to renderer vt+0x194 (ecx = renderer): ebx/esi are restored and nothing else is on the stack, so the jump
// sees exactly the caller's frame (return address, no arguments); vt+0x194 takes none (its other call sites push
// nothing). Reproduced as a call returning its eax. Executed in recorded runs (recover.py; no direct callers).
uint32_t fn_0046d450(uint32_t ecx, uint32_t edi)
{
    void* w = P(ecx);
    orig::fn_00465360(ecx, edi);
    VCall<void>(Renderer(), 0x190 / 4, 1u);
    orig::fn_00464750(ecx, Field<uint32_t>(w, 0x578), edi, Field<uint32_t>(g_game, 0xa70), 0x3ccccccdu, 0,
                      Field<uint32_t>(w, 0x580));
    return VCall<uint32_t>(Renderer(), 0x194 / 4);
}
R6_ENTRY_ECX_EDI(fn_0046d450_Entry, fn_0046d450)
HOOK(0x0046d450, "fn_0046d450", fn_0046d450_Entry, nullptr);

// 0x00488110 / 0x00488140 (eax = scene pass; symbols/abi.csv: no ret found). ecx = the sky list +0x1bc / +0x1c0,
// then a tail jump to its vt+0x30 with the caller's frame untouched (no pushes; the only caller 0x00488060 pushes no
// arguments, and its stack stays balanced, so vt+0x30 takes none). Reproduced as a call returning its eax. Executed in
// recorded runs (caller ScenePass_Release 0x00488060 in render_r4.cpp inlines them).
uint32_t fn_00488110(uint32_t pass)
{
    return VCall<uint32_t>(Field<void*>(P(pass), 0x1bc), 0x30 / 4);
}
R6_ENTRY_EAX(fn_00488110_Entry, fn_00488110)
HOOK(0x00488110, "fn_00488110", fn_00488110_Entry, nullptr);

uint32_t fn_00488140(uint32_t pass)
{
    return VCall<uint32_t>(Field<void*>(P(pass), 0x1c0), 0x30 / 4);
}
R6_ENTRY_EAX(fn_00488140_Entry, fn_00488140)
HOOK(0x00488140, "fn_00488140", fn_00488140_Entry, nullptr);

// 0x0048ea30 (cdecl; stack: a; caller pops). In a function-pointer table in .data at 0x005cb190 (8 entries; no
// vtable or call reference). c = *a; c+4 = a+0x20; n = *c; when n < 0x80 (unsigned): v = a[4 + a[8]] (dwords),
// d = a+0xc (signed): d >= 0 -> c[3n+2] = v, end = v + d - 1; d < 0 -> c[3n+2] = v + d + 1, end = v; c[3n+3] = end,
// c[3n+4] = a[4 + (a[8] ^ 1)], *c += 1. Returns that last value (or a when full). Not executed in recorded runs: not
// verifiable by compare yet.
uint32_t fn_0048ea30(uint32_t* a)
{
    uint32_t* c = reinterpret_cast<uint32_t*>(a[0]);
    c[1] = a[8];
    const uint32_t n = c[0];
    if (n >= 0x80)
        return u(a);
    uint32_t end;
    if (static_cast<int32_t>(a[3]) >= 0)
    {
        c[n * 3 + 2] = a[4 + a[8]];
        end = a[4 + a[8]] + a[3] - 1;
    }
    else
    {
        c[n * 3 + 2] = a[4 + a[8]] + a[3] + 1;
        end = a[4 + a[8]];
    }
    c[(c[0] + 1) * 3] = end;
    const uint32_t v = a[4 + (a[8] ^ 1)];
    c[c[0] * 3 + 4] = v;
    ++c[0];
    return v;
}
static uint32_t fn_0048ea30_Abi(uint32_t a0)
{
    return fn_0048ea30(static_cast<uint32_t*>(P(a0)));
}
ABI_ENTRY_0048ea30(fn_0048ea30_Entry, fn_0048ea30_Abi)
HOOK(0x0048ea30, "fn_0048ea30", fn_0048ea30_Entry, nullptr);

// 0x004935e0 (ecx = menu image). Vtable 0x0058fd28 slot 7 (draw). Frame (0x40): w, h, quad descriptor (6 dwords:
// 0x130e4, -1, texture, 0...), screen quad (8 dwords). texture = TextureBank_Find 0x004881c0 (ecx = name +0x8c,
// esi = Game+0xa78); none: nothing drawn. Size: 640 x 480 when +0x4c has 0x10000, else renderer vt+0x138(texture,
// &w, &h). 0x004938e0 (esi = &quad; post-process, w, -h, 0.5, 0.5); w = w < 640 ? 0 : w, h = h < 480 ? 0 : h
// (signed); 0x004937a0 (esi = edi = &quad; post-process, w, h); RenderPass_Begin 0x00415dc0 (esi = renderer, edi =
// post-process; 0, 1); Post_DrawScreenQuad 0x0047cdd0 (eax = ecx = 0; post-process, &descriptor, &quad, 0); then the
// post-process view restore block. Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_004935e0(void* img)
{
    uint32_t f[16];  // [0] w, [1] h, [2..7] descriptor, [8..15] quad
    std::memset(&f[2], 0, 6 * 4);
    f[2] = 0x130e4;
    f[3] = 0xffffffffu;
    const uint32_t tex = orig::fn_004881c0(u(img) + 0x8c, Field<uint32_t>(g_game, 0xa78), 0, 0, 0, 0, 0, 0, 0, 0);
    f[4] = tex;
    if (!tex)
        return 0;
    uint32_t h;
    if (Field<uint32_t>(img, 0x4c) & 0x10000)
    {
        h = 0x1e0;
        f[0] = 0x280;
        f[1] = h;
    }
    else
    {
        VCall<void>(Renderer(), 0x138 / 4, tex, u(&f[0]), u(&f[1]));
        h = f[1];
    }
    orig::fn_004938e0(u(&f[8]), u(PostProcess()), f[0], 0u - h, 0x3f000000u, 0x3f000000u);
    const int32_t w = static_cast<int32_t>(f[0]);
    const int32_t hh = static_cast<int32_t>(f[1]);
    f[0] = w < 0x280 ? 0u : f[0];
    f[1] = hh < 0x1e0 ? 0u : f[1];
    orig::fn_004937a0(u(&f[8]), u(&f[8]), u(PostProcess()), f[0], f[1]);
    orig::fn_00415dc0(u(Renderer()), u(PostProcess()), 0, 1);
    orig::fn_0047cdd0(0, 0, u(PostProcess()), u(&f[2]), u(&f[8]), 0);
    RestorePostProcessView();
    return 0;
}
static uint32_t fn_004935e0_Abi(uint32_t ecx)
{
    return fn_004935e0(P(ecx));
}
ABI_ENTRY_004935e0(fn_004935e0_Entry, fn_004935e0_Abi)
HOOK(0x004935e0, "fn_004935e0", fn_004935e0_Entry, nullptr);

// 0x00494640 (ecx = screen). Vtable 0x0058fb14 slot 7 (draw): 0x004927f0 (ecx = screen); when byte +0x88 is set,
// counter +0x7c += 1 and (counter & 0x1f) < 0x18 (blink): count = 0x00415740 (ecx = 0x1b, edx = Game+0xa80), text =
// _swprintf(buffer, Text_Lookup 0x00495f00 (eax = "ui_stats_ok" 0x005a0ccc, ebx = font; 0), count),
// RenderPass_Begin (renderer, post-process; 0, 1), renderer vt+0x190(1), Text_Draw 0x00495e80 (eax = 0xffc0c0c0,
// ecx = Game; font, buffer, 1.2, 0.5, 0.15, 0.5, 0.5, 2), vt+0x194(), then the post-process view restore block. Not
// executed in recorded runs: not verifiable by compare yet.
uint32_t fn_00494640(void* screen)
{
    uint8_t buffer[0x200];
    orig::fn_004927f0(u(screen));
    if (!Field<uint8_t>(screen, 0x88))
        return 0;
    const uint32_t c = ++Field<uint32_t>(screen, 0x7c);
    if (static_cast<uint8_t>(c & 0x1f) >= 0x18)
        return c;
    const uint32_t count = orig::fn_00415740(0x1b, Field<uint32_t>(g_game, 0xa80));
    const uint32_t fmt = orig::fn_00495f00(0x005a0ccc, Font(), 0);
    orig::fn_0053862e(u(buffer), fmt, count, 0);
    orig::fn_00415dc0(u(Renderer()), u(PostProcess()), 0, 1);
    VCall<void>(Renderer(), 0x190 / 4, 1u);
    orig::fn_00495e80(0xffc0c0c0u, u(g_game), Font(), u(buffer), 0x3f99999au, 0x3f000000u, 0x3e19999au, 0x3f000000u,
                      0x3f000000u, 2);
    VCall<void>(Renderer(), 0x194 / 4);
    RestorePostProcessView();
    return 0;
}
static uint32_t fn_00494640_Abi(uint32_t ecx)
{
    return fn_00494640(P(ecx));
}
ABI_ENTRY_00494640(fn_00494640_Entry, fn_00494640_Abi)
HOOK(0x00494640, "fn_00494640", fn_00494640_Entry, nullptr);

// 0x004948d0 (ecx = object). Vtable 0x005a45a8 slot 0x4f (draw). A rotating, shrinking full-screen image (guess: a
// screen flash) driven by v = +0x4f8; nothing when v == 0. angle = v * [0x005bce28] (6.807); alpha = v < 0.1 ?
// _ftol(v * 2550) : 0xff; s = 1.5 - v*v; c = cos(angle) * s, sn = sin(angle) * s. renderer vt+0x138(texture
// [0x0063a15c], &w, &h); h = h*5/4 (signed); R = renderer vt+0x88() (float in st0, kept on the x87 stack through the
// corners). Corners (-w,-h), (w,-h), (-w,h), (w,h): x' = c*x + sn*y, y' = sn*x - c*y, in the original's operation
// order; then x' = x' * [0x005bcbf8] (1/640) + +0x4e8, y' = (1 / (R * 480)) * y' + +0x4ec. Descriptor {0x133ec,
// alpha << 24 | 0xffffff, 0x005fec80, 0, 0, 0}; RenderPass_Begin (renderer, post-process; 0, 1);
// Post_DrawScreenQuad (eax = ecx = 0; post-process, &descriptor, &corners, 0); the post-process view restore block.
// Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_004948d0(void* obj)
{
    if (Field<float>(obj, 0x4f8) == kZero)
        return 0;
    // Frame from the original (0x58 bytes): f0 angle / h, f1 w, f2 f3 temporaries, f4 c, f5 sn, f7 -c, f8..f13
    // descriptor, f14..f21 corners.
    uint32_t f[22];
    float* ff = reinterpret_cast<float*>(f);
    const float v0 = Field<float>(obj, 0x4f8);
    ff[0] = v0 * GameGlobal<float>(0x005bce28);
    uint32_t alpha;
    if (Field<float>(obj, 0x4f8) < kTenth)
    {
        const float a = Field<float>(obj, 0x4f8) * GameGlobal<float>(0x005bce24);
        alpha = static_cast<uint32_t>(static_cast<int32_t>(a));
    }
    else
    {
        alpha = 0xff;
    }
    const float v = Field<float>(obj, 0x4f8);
    const float s = GameGlobal<float>(0x005bce20) - v * v;
    ff[4] = static_cast<float>(std::cos(static_cast<double>(ff[0])) * s);
    ff[5] = static_cast<float>(std::sin(static_cast<double>(ff[0])) * s);
    ff[7] = -ff[4];
    VCall<void>(Renderer(), 0x138 / 4, GameGlobal<uint32_t>(0x0063a15c), u(&f[1]), u(&f[0]));
    const int32_t h = static_cast<int32_t>(f[0]) * 5 / 4;
    f[0] = static_cast<uint32_t>(h);
    const float R = VCall<float>(Renderer(), 0x88 / 4);
    const int32_t w = static_cast<int32_t>(f[1]);
    const float c = ff[4], sn = ff[5], nc = ff[7];
    const float nh = static_cast<float>(-h), nw = static_cast<float>(-w);
    const float pw = static_cast<float>(w), ph = static_cast<float>(h);
    // corner 0 (-w, -h)
    ff[14] = c * nw + nh * sn;
    ff[15] = nh * nc + nw * sn;
    // corner 1 (w, -h)
    ff[2] = c * pw;
    ff[16] = nh * sn + ff[2];
    const float wsn = pw * sn;
    ff[17] = nh * nc + wsn;
    // corner 2 (-w, h)
    const float snh = sn * ph;
    ff[18] = c * nw + snh;
    ff[3] = ph * nc;
    ff[19] = nw * sn + ff[3];
    // corner 3 (w, h)
    ff[20] = snh + ff[2];
    ff[21] = ff[3] + wsn;
    const float scale = kOne / (R * GameGlobal<float>(0x005bce1c));
    for (int i = 0; i < 4; ++i)
    {
        ff[14 + i * 2] = ff[14 + i * 2] * GameGlobal<float>(0x005bcbf8) + Field<float>(obj, 0x4e8);
        ff[15 + i * 2] = scale * ff[15 + i * 2] + Field<float>(obj, 0x4ec);
    }
    std::memset(&f[8], 0, 6 * 4);
    f[9] = ((alpha & 0xff) << 24) | 0xffffffu;
    f[8] = 0x133ec;
    f[10] = 0x005fec80;
    orig::fn_00415dc0(u(Renderer()), u(PostProcess()), 0, 1);
    orig::fn_0047cdd0(0, 0, u(PostProcess()), u(&f[8]), u(&f[14]), 0);
    RestorePostProcessView();
    return 0;
}
static uint32_t fn_004948d0_Abi(uint32_t ecx)
{
    return fn_004948d0(P(ecx));
}
ABI_ENTRY_004948d0(fn_004948d0_Entry, fn_004948d0_Abi)
HOOK(0x004948d0, "fn_004948d0", fn_004948d0_Entry, nullptr);

// 0x00495970 / 0x00495980 / 0x00495990 / 0x004959a0. Streamer (vtable 0x0058fad8) slots 1 / 0 and StreamerSystem
// (vtable 0x0058faf0) slots 1 / 0: class descriptors 0x0057ec40 ("CLASSV(Streamer)") / 0x0057eb68
// ("CLASSV(StreamerSystem)") and type ids 0x80000004 / 0x80000003. Not executed in recorded runs: not verifiable by
// compare yet.
uint32_t Streamer_GetClass()
{
    return 0x0057ec40;
}
ABI_ENTRY_00495970(Streamer_GetClass_Entry, Streamer_GetClass)
HOOK(0x00495970, "Streamer_GetClass", Streamer_GetClass_Entry, nullptr);

uint32_t Streamer_GetTypeId()
{
    return 0x80000004u;
}
ABI_ENTRY_00495980(Streamer_GetTypeId_Entry, Streamer_GetTypeId)
HOOK(0x00495980, "Streamer_GetTypeId", Streamer_GetTypeId_Entry, nullptr);

uint32_t StreamerSystem_GetClass()
{
    return 0x0057eb68;
}
ABI_ENTRY_00495990(StreamerSystem_GetClass_Entry, StreamerSystem_GetClass)
HOOK(0x00495990, "StreamerSystem_GetClass", StreamerSystem_GetClass_Entry, nullptr);

uint32_t StreamerSystem_GetTypeId()
{
    return 0x80000003u;
}
ABI_ENTRY_004959a0(StreamerSystem_GetTypeId_Entry, StreamerSystem_GetTypeId)
HOOK(0x004959a0, "StreamerSystem_GetTypeId", StreamerSystem_GetTypeId_Entry, nullptr);

// 0x004a3260 (eax = widget; ecx is only pushed as a local slot and overwritten before use; symbols/abi.csv: no ret
// found). 0x00464b10 (esi = widget); n = +0x478 % 100 (unsigned); q = n / 3, r = n % 3 (signed); +0x4e8 =
// (float)((r - 1) * 180) * [0x005bcbf8] (1/640) + +0x4e8; +0x4fc = n + 1; +0x4ec = ((q odd) ? 0 : 1) *
// [0x005bcec0] (0.3667) + +0x4ec; +0x474 |= 0x2000; then a tail jump to widget vt+0x144 (ecx = widget) after esi/edi
// and the local slot are popped: the jump sees exactly the caller's frame. The only caller 0x004a3020 pushes no
// arguments (call at 0x004a3039), and slot 0x51 (+0x144) implementations such as 0x00410750 take none, so it is
// reproduced as a call returning its eax. Executed in recorded runs.
uint32_t fn_004a3260(uint32_t eax, uint32_t /*ecx*/)
{
    void* w = P(eax);
    orig::fn_00464b10(eax);
    const uint32_t n = Field<uint32_t>(w, 0x478) % 100u;
    const int32_t q = static_cast<int32_t>(n) / 3;
    const int32_t r = static_cast<int32_t>(n) % 3;
    const int32_t d = r * 0xb4 - 0xb4;
    Field<float>(w, 0x4e8) = static_cast<float>(d) * GameGlobal<float>(0x005bcbf8) + Field<float>(w, 0x4e8);
    const float k = (q & 1) ? kZero : kOne;
    Field<uint32_t>(w, 0x4fc) = n + 1;
    Field<float>(w, 0x4ec) = k * GameGlobal<float>(0x005bcec0) + Field<float>(w, 0x4ec);
    Field<uint32_t>(w, 0x474) |= 0x2000;
    return VCall<uint32_t>(w, 0x144 / 4);
}
R6_ENTRY_EAX_ECX(fn_004a3260_Entry, fn_004a3260)
HOOK(0x004a3260, "fn_004a3260", fn_004a3260_Entry, nullptr);

// 0x004a3300 (ecx = widget, edi passed through). Vtable 0x005aa5f8 slot 0x4e (+0x138): when +0x580 (a text id) is
// set: count = 0x00415740 (ecx = 0x1b, edx = Game+0xa80); _swprintf(buffer, Text_Lookup (eax = +0x580, ebx = font;
// 0), count); width = 0x00465720 (ebx = buffer; +0x4c8, 192.0, 0) (st0); renderer vt+0x190(1); UiText_Draw
// 0x00464750 (ecx = widget, ebx = buffer, edi; Game+0xa70, 0, -0.1, width / +0x4c8); vt+0x194(). Not executed in
// recorded runs: not verifiable by compare yet.
uint32_t fn_004a3300(uint32_t ecx, uint32_t edi)
{
    void* w = P(ecx);
    uint8_t buffer[0x200];
    if (!Field<uint32_t>(w, 0x580))
        return 0;
    const uint32_t count = orig::fn_00415740(0x1b, Field<uint32_t>(g_game, 0xa80));
    const uint32_t fmt = orig::fn_00495f00(Field<uint32_t>(w, 0x580), Font(), 0);
    orig::fn_0053862e(u(buffer), fmt, count, 0);
    const float width = orig::fn_00465720(u(buffer), Field<uint32_t>(w, 0x4c8), 0x43400000u, 0);
    VCall<void>(Renderer(), 0x190 / 4, 1u);
    const float q = width / Field<float>(w, 0x4c8);
    orig::fn_00464750(ecx, u(buffer), edi, Field<uint32_t>(g_game, 0xa70), 0, 0xbdcccccdu, f2u(q));
    return VCall<uint32_t>(Renderer(), 0x194 / 4);
}
ABI_ENTRY_004a3300(fn_004a3300_Entry, fn_004a3300)
HOOK(0x004a3300, "fn_004a3300", fn_004a3300_Entry, nullptr);

// 0x004a33d0 (ecx = widget). Vtable 0x005aa5f8 slot 0x4f (+0x13c, draw): a save-game slot. One-time statics (guard
// bits 0x0064aa18): colours 0x0064a710 = 0x800a2532, 0x0064a714 = 0xe00a2532, 0x0064aa14 = 0xffffffff, 0x0064aa10 =
// 0xffffff. sel = bit 7 of +0x474. Panel 0x0046b0d0 (eax = widget, ecx = -0x60, edx = 0; 0x60, -0xb8, 16, 8, 0.25,
// colour[sel], 0, 0, 0, 0). Thumbnail +0x5a8 when set: renderer vt+0x138(it, &w, &h); q = (h << 7) / (2w);
// 0x0046b0d0 (eax = widget, ecx = -0x40, edx = q - 0x46; 0x40, -0x46 - q, 8, 8, 0.75, [0x0064aa14], [0x0064aa10],
// thumbnail, 0, 0). Renderer vt+0x190(1); UiText_Draw 0x00464750 (ecx = widget, ebx = +0x500, edi = sel; Game+0xa70,
// 0, -0.01667, 1.0); empty slot (byte +0x4f8): "ui_savegame_empty" (0x0058e800) at -0.2167, state = byte +0x4fb ==
// 0; damaged (byte +0x4f9): "ui_savegame_damaged" (0x0058e7ec), state 2; else four lines +0x5b0, (+0x5ac), +0x6b0,
// +0x730 at -0.2167, -0.25, -0.2833, -0.3167 scale 0.75, state 3. When sel: blink counter 0x0064a718 += 1, (& 0x1f) <
// 0x18: +0x580 = table 0x005fb920[(byte +0x4fa == 0) + state * 2], 0x00464430 (eax = widget, ebx = Game+0xa70).
// Renderer vt+0x194(). Not executed in recorded runs: not verifiable by compare yet.
uint32_t fn_004a33d0(void* w)
{
    uint32_t& guard = GameGlobal<uint32_t>(0x0064aa18);
    uint32_t g = guard;
    if (!(g & 1))
    {
        g |= 1;
        guard = g;
        GameGlobal<uint32_t>(0x0064a710) = 0x800a2532u;
        GameGlobal<uint32_t>(0x0064a714) = 0xe00a2532u;
    }
    if (!(g & 2))
    {
        g |= 2;
        guard = g;
        GameGlobal<uint32_t>(0x0064aa14) = 0xffffffffu;
    }
    if (!(g & 4))
    {
        g |= 4;
        guard = g;
        GameGlobal<uint32_t>(0x0064aa10) = 0xffffffu;
    }
    const uint32_t sel = (Field<uint32_t>(w, 0x474) >> 7) & 1;
    orig::fn_0046b0d0(u(w), 0xffffffa0u, 0, 0x60, 0xffffff48u, 0x10, 8, 0x3e800000u,
                      GameGlobal<uint32_t>(0x0064a710 + sel * 4), 0, 0, 0, 0);
    if (const uint32_t thumb = Field<uint32_t>(w, 0x5a8))
    {
        int32_t l[2];  // l[0] = h, l[1] = w (frame +0xc / +0x10)
        VCall<void>(Renderer(), 0x138 / 4, thumb, u(&l[1]), u(&l[0]));
        const int32_t q = (l[0] << 7) / (l[1] + l[1]);
        l[1] = 0x40;
        l[0] = q;
        orig::fn_0046b0d0(u(w), 0xffffffc0u, static_cast<uint32_t>(q - 0x46), 0x40, static_cast<uint32_t>(-0x46 - q), 8,
                          8, 0x3f400000u, GameGlobal<uint32_t>(0x0064aa14), GameGlobal<uint32_t>(0x0064aa10),
                          Field<uint32_t>(w, 0x5a8), 0, 0);
    }
    VCall<void>(Renderer(), 0x190 / 4, 1u);
    orig::fn_00464750(u(w), u(w) + 0x500, sel, Field<uint32_t>(g_game, 0xa70), 0, 0xbc888889u, 0x3f800000u);
    uint32_t state;
    if (Field<uint8_t>(w, 0x4f8))
    {
        const uint32_t text = orig::fn_00495f00(0x0058e800, Font(), 0);
        orig::fn_00464750(u(w), text, sel, Field<uint32_t>(g_game, 0xa70), 0, 0xbe5ddddeu, 0x3f800000u);
        state = Field<uint8_t>(w, 0x4fb) == 0 ? 1u : 0u;
    }
    else if (Field<uint8_t>(w, 0x4f9))
    {
        const uint32_t text = orig::fn_00495f00(0x0058e7ec, Font(), 0);
        orig::fn_00464750(u(w), text, sel, Field<uint32_t>(g_game, 0xa70), 0, 0xbe5ddddeu, 0x3f800000u);
        state = 2;
    }
    else
    {
        orig::fn_00464750(u(w), u(w) + 0x5b0, sel, Field<uint32_t>(g_game, 0xa70), 0, 0xbe5ddddeu, 0x3f400000u);
        orig::fn_00464750(u(w), Field<uint32_t>(w, 0x5ac), sel, Field<uint32_t>(g_game, 0xa70), 0, 0xbe800000u,
                          0x3f400000u);
        orig::fn_00464750(u(w), u(w) + 0x6b0, sel, Field<uint32_t>(g_game, 0xa70), 0, 0xbe911111u, 0x3f400000u);
        orig::fn_00464750(u(w), u(w) + 0x730, sel, Field<uint32_t>(g_game, 0xa70), 0, 0xbea22222u, 0x3f400000u);
        state = 3;
    }
    if (sel)
    {
        const uint32_t c = ++GameGlobal<uint32_t>(0x0064a718);
        if (static_cast<uint8_t>(c & 0x1f) < 0x18)
        {
            const uint32_t idx = (Field<uint8_t>(w, 0x4fa) == 0 ? 1u : 0u) + state * 2;
            Field<uint32_t>(w, 0x580) = GameGlobal<uint32_t>(0x005fb920 + idx * 4);
            orig::fn_00464430(u(w), Field<uint32_t>(g_game, 0xa70));
        }
    }
    return VCall<uint32_t>(Renderer(), 0x194 / 4);
}
static uint32_t fn_004a33d0_Abi(uint32_t ecx)
{
    return fn_004a33d0(P(ecx));
}
ABI_ENTRY_004a33d0(fn_004a33d0_Entry, fn_004a33d0_Abi)
HOOK(0x004a33d0, "fn_004a33d0", fn_004a33d0_Entry, nullptr);
