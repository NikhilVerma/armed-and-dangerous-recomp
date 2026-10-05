// Recovered (static, batch game_02): camera fades/paths, CameraFXObject, cheats, counter / explosion / mission /
// ticker-bomb / molmer object virtuals, character death and assorted small game helpers. Compile-checked only (the
// game was not run); the oracle (tools/compare.py) is still to do. Functions the recorded runs never execute are
// marked "not executed in recorded runs". Vtable slots come from a scan of .rdata in game.unpacked.exe (vtable
// starts = constructor stores `mov [reg], imm32`); class names from the vtable's slot 1 descriptor (+0x14
// "CLASS(Name)").
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
float& F(void* p, uint32_t off)
{
    return Field<float>(p, off);
}

// Game float constants (read in place, as the original does).
inline const float& kZero = GameGlobal<float>(0x0056c05c);       // 0.0
inline const float& kOne = GameGlobal<float>(0x005bcb2c);        // 1.0
inline const float& kFrameTime = GameGlobal<float>(0x00604a90);  // frame time step

void* G()
{
    return GameGlobal<void*>(0x005fe490);
}
}  // namespace

// Functions recovered later in this file, called directly by earlier ones.
uint32_t fn_0044e600();
uint32_t fn_0044a660(uint32_t obj, uint32_t sel);
uint32_t fn_0044a580(void* obj, uint32_t value);
uint32_t PlayerTickerBomb_IsArmed(void* obj);
static uint32_t fn_0044a580_U(uint32_t obj, uint32_t value)
{
    return fn_0044a580(reinterpret_cast<void*>(obj), value);
}
static uint32_t PlayerTickerBomb_IsArmed_U(uint32_t obj)
{
    return PlayerTickerBomb_IsArmed(reinterpret_cast<void*>(obj));
}


// ---------------------------------------------------------------------------------------------------------------
// The inlined block shared by the camera fades 0x0040fb00 / 0x0040fb50 / 0x0040fbb0: when game+0x748 (the player
// object, guess) is set and 0x0044e600 returns al = 0, player vt+0xf4(flag) then 0x004a0e00 (al = flag, upper eax
// bytes from the vt+0xf4 result; esi = game+0x748 re-read + 0xc24). Returns eax as the original leaves it (or
// `fallback` when the block is skipped because +0x748 is 0).
static uint32_t CameraFade_NotifyPlayer(uint32_t flag, uint32_t fallback)
{
    void* player = Field<void*>(G(), 0x748);
    if (!player)
        return fallback;
    const uint32_t r = fn_0044e600();
    if (static_cast<uint8_t>(r))
        return r;
    const uint32_t v = VCall<uint32_t>(player, 0xf4 / 4, flag);
    return orig::fn_004a0e00((v & 0xffffff00u) | flag, Field<uint32_t>(G(), 0x748) + 0xc24);
}

// 0x0040fb00 (eax = camera; caller pops nothing). Starts a fade: camera+0x6c (fade state) = 2, then
// CameraFade_NotifyPlayer(1). Not executed in recorded runs.
uint32_t fn_0040fb00(void* cam)
{
    Field<uint32_t>(cam, 0x6c) = 2;
    return CameraFade_NotifyPlayer(1, u(cam));
}
static uint32_t fn_0040fb00_Abi(uint32_t eax)
{
    return fn_0040fb00(P(eax));
}
ABI_ENTRY_0040fb00(fn_0040fb00_Entry, fn_0040fb00_Abi)
HOOK(0x0040fb00, "fn_0040fb00", fn_0040fb00_Entry, nullptr);

// 0x0040fb50 (eax = camera). Clears the fade: camera+0x6c = 0, byte +0x48 = 0, then CameraFade_NotifyPlayer(1)
// (when +0x748 is 0 the original returns g_game in eax). Not executed in recorded runs.
uint32_t fn_0040fb50(void* cam)
{
    Field<uint32_t>(cam, 0x6c) = 0;
    Field<uint8_t>(cam, 0x48) = 0;
    return CameraFade_NotifyPlayer(1, u(G()));
}
static uint32_t fn_0040fb50_Abi(uint32_t eax)
{
    return fn_0040fb50(P(eax));
}
ABI_ENTRY_0040fb50(fn_0040fb50_Entry, fn_0040fb50_Abi)
HOOK(0x0040fb50, "fn_0040fb50", fn_0040fb50_Entry, nullptr);

// 0x0040fba0 (ecx = camera). Returns camera+0x6c (fade state) != 0.
uint32_t fn_0040fba0(void* cam)
{
    return Field<uint32_t>(cam, 0x6c) != 0 ? 1u : 0u;
}
static uint32_t fn_0040fba0_Abi(uint32_t ecx)
{
    return fn_0040fba0(P(ecx));
}
ABI_ENTRY_0040fba0(fn_0040fba0_Entry, fn_0040fba0_Abi)
HOOK(0x0040fba0, "fn_0040fba0", fn_0040fba0_Entry, nullptr);

// 0x0040fbb0 (edi = camera; stack: target; ret 4; caller 0x0040e350). Advances the camera fade: t = +0x68 /
// game+0x276c (fade length), clamped to [0, 1]; on reaching 1 in state 1 the state becomes 3 and
// CameraFade_NotifyPlayer(0) runs. +0x68 moves by the frame time (up in states 1 and 3, down otherwise), clamped to
// [0, game+0x276c]. 0x0040a4d0 (edx = camera+0x48, ebx = matrix out; stack: +4, +0x154, &float, &out2, &out1) gives a
// matrix whose translation (m[3], m[7], m[11]) is blended into target +0xc/+0x1c/+0x2c by t, and a float blended into
// camera+0xf4. In state 2 a t of 0 ends the fade (state 0). The return value is the state read last (the original
// returns fnstsw bits in state 2; the caller ignores eax, guess).
uint32_t fn_0040fbb0(void* cam, void* target)
{
    if (!Field<uint32_t>(cam, 0x6c))
        return 0;
    float t = F(cam, 0x68) / F(G(), 0x276c);
    if (t >= kOne)  // fcomp/test ah,1: an unordered t takes the "below 1" branch
    {
        t = 1.0f;
        if (Field<uint32_t>(cam, 0x6c) == 1)
        {
            Field<uint32_t>(cam, 0x6c) = 3;
            CameraFade_NotifyPlayer(0, 0);
        }
    }
    else if (t < kZero)
        t = 0.0f;
    const uint32_t state = Field<uint32_t>(cam, 0x6c);
    if (state == 1 || state == 3)
        F(cam, 0x68) = kFrameTime + F(cam, 0x68);
    else
        F(cam, 0x68) = F(cam, 0x68) - kFrameTime;
    if (F(cam, 0x68) > F(G(), 0x276c))
        Field<uint32_t>(cam, 0x68) = Field<uint32_t>(G(), 0x276c);
    else if (F(cam, 0x68) < kZero)
        Field<uint32_t>(cam, 0x68) = 0;

    // Original frame (sub esp, 0x50): +0 float, +4 out1, +8 out2, +0xc matrix (0x44 bytes).
    uint32_t frame[0x50 / 4] = {};
    orig::fn_0040a4d0(u(cam) + 0x48, u(&frame[3]), Field<uint32_t>(cam, 4), Field<uint32_t>(cam, 0x154), u(&frame[0]),
                      u(&frame[2]), u(&frame[1]));
    const float* m = reinterpret_cast<const float*>(&frame[3]);
    const float omt = kOne - t;
    F(target, 0xc) = omt * F(target, 0xc) + m[3] * t;
    F(target, 0x1c) = omt * F(target, 0x1c) + m[7] * t;
    F(target, 0x2c) = omt * F(target, 0x2c) + m[11] * t;
    const uint32_t s = Field<uint32_t>(cam, 0x6c);
    F(cam, 0xf4) = omt * F(cam, 0xf4) + u2f(frame[0]) * t;
    if (s == 2 && t == kZero)
        Field<uint32_t>(cam, 0x6c) = 0;
    return s;
}
static uint32_t fn_0040fbb0_Abi(uint32_t edi, uint32_t a0)
{
    return fn_0040fbb0(P(edi), P(a0));
}
ABI_ENTRY_0040fbb0(fn_0040fbb0_Entry, fn_0040fbb0_Abi)
HOOK(0x0040fbb0, "fn_0040fbb0", fn_0040fbb0_Entry, nullptr);

// 0x0040fd50 (ecx = camera, ebx = matrix in/out; caller 0x0040e350). Counts the camera blend down: +0x160 -= frame
// time, t = +0x160 / +0x164; below 0 the blend ends (byte +0x15c = 0, t = 0). Builds two copies of the camera matrix
// +0x8c (A, B) and two of ebx (C, D); D becomes identity rotation with its translation blended from ebx toward B by t
// and its flags word kept as (flags & 0x10); A and C lose their translation and flag bit 0x10. Their rotations become
// quaternions (0x004e35a0, ecx = matrix; stack: out), slerped by t (0x004f1400, ecx = from, edx = esi = to; stack:
// t), normalised (0x004f12e0), turned back into C (0x004e3260, eax = C, ecx = quat), and Matrix_Multiply 0x004e2910
// (eax = C; stack: ebx, D). Not executed in recorded runs.
uint32_t fn_0040fd50(void* cam, uint32_t mtx)
{
    float t = F(cam, 0x160) - kFrameTime;
    F(cam, 0x160) = t;
    t = t / F(cam, 0x164);
    if (t < kZero)
    {
        Field<uint8_t>(cam, 0x15c) = 0;
        t = 0.0f;
    }
    const float omt = kOne - t;
    uint32_t A[17], B[17], C[17], D[17];
    uint32_t qA[4], qC[4];
    std::memcpy(A, reinterpret_cast<uint8_t*>(cam) + 0x8c, sizeof A);
    std::memcpy(B, reinterpret_cast<uint8_t*>(cam) + 0x8c, sizeof B);
    std::memcpy(C, P(mtx), sizeof C);
    std::memcpy(D, P(mtx), sizeof D);
    float* fB = reinterpret_cast<float*>(B);
    float* fD = reinterpret_cast<float*>(D);
    fD[3] = fD[3] * omt + fB[3] * t;
    fD[7] = fD[7] * omt + fB[7] * t;
    C[16] &= 0xffffffefu;
    D[16] &= 0x10u;
    fD[11] = omt * fD[11] + fB[11] * t;
    A[11] = 0;
    A[7] = 0;
    A[3] = 0;
    A[16] &= 0xffffffefu;
    C[11] = 0;
    C[7] = 0;
    C[3] = 0;
    D[10] = 0x3f800000u;
    D[5] = 0x3f800000u;
    D[0] = 0x3f800000u;
    D[9] = 0;
    D[8] = 0;
    D[6] = 0;
    D[4] = 0;
    D[2] = 0;
    D[1] = 0;
    orig::fn_004e35a0(u(A), u(qA));
    orig::fn_004e35a0(u(C), u(qC));
    orig::fn_004f1400(u(qA), u(qC), u(qC), f2u(t));
    orig::fn_004f12e0(u(qC), u(qC));
    orig::fn_004e3260(u(C), u(qC));
    return orig::fn_004e2910(u(C), mtx, u(D));
}
static uint32_t fn_0040fd50_Abi(uint32_t ecx, uint32_t ebx)
{
    return fn_0040fd50(P(ecx), ebx);
}
ABI_ENTRY_0040fd50(fn_0040fd50_Entry, fn_0040fd50_Abi)
HOOK(0x0040fd50, "fn_0040fd50", fn_0040fd50_Entry, nullptr);

// 0x0040ff50 (eax = animation name; stack: camera, mode, a2; ret 0xc). Camera_StartPath (classifier name). When
// camera+0x178 is 0 it searches the object list Game+0xa84 +0xe70 (iterator 0x004e7340 / 0x004e73c0) for the object
// named "camera_path" (0x005bad88; Object_GetName 0x00453270, _stricmp 0x005448ee); on a hit +0x178 = object, the
// iterator's owner vt+0xc(node) and the node loses a reference (+0x18). Not found: the node loses its reference and
// the node (or 0) is returned. Then +0x180 = a2, +0x17c = mode, name copied to +0x184. Mode 1: Anim_Set 0x00408d90
// (eax = -1, esi = path object; stack: name, 0x80, 0, 0, 0, 0), 0x0040e200 (eax = camera, ebx = "Camera01"
// 0x005bad94; stack: path object, 0), game byte +0xdce = 1 (cinematic active). Mode 2: Anim_Set, then 0x00453c50
// (path object, "Camera01", &matrix, 1, 0, 0, 0, 0, 0) and Matrix_Inverse 0x004e2b40 (eax = &matrix, ecx = path
// object+0x460). Other modes return the end of the copied name (the strcpy pointer). The _stricmp call passes ecx as
// left by Object_GetName (not reproducible): 0.
uint32_t Camera_StartPath(const char* name, void* cam, uint32_t mode, uint32_t a2)
{
    if (!Field<uint32_t>(cam, 0x178))
    {
        uint32_t iter[2] = {0, 0};
        void* list = Field<void*>(Field<void*>(G(), 0xa84), 0xe70);
        uint32_t node;
        for (uint32_t obj = orig::fn_004e7340(u(list), u(iter)); obj; obj = orig::fn_004e73c0(u(iter)))
        {
            const uint32_t objName = orig::fn_00453270(obj);
            if (orig::fn_005448ee(0, objName, 0x005bad88) == 0)
            {
                Field<uint32_t>(cam, 0x178) = obj;
                node = iter[1];
                VCall<void>(P(iter[0]), 0xc / 4, node);
                --Field<int32_t>(P(node), 0x18);
                iter[1] = 0;  // original: eax = 0 stands for the node below
                break;
            }
        }
        node = iter[1];
        if (!Field<uint32_t>(cam, 0x178))
        {
            if (!node)
                return 0;
            --Field<int32_t>(P(node), 0x18);
            return node;
        }
        if (node)
            --Field<int32_t>(P(node), 0x18);
    }
    Field<uint32_t>(cam, 0x180) = a2;
    Field<uint32_t>(cam, 0x17c) = mode;
    char* dst = reinterpret_cast<char*>(cam) + 0x184;
    const char* src = name;
    char c;
    do
    {
        c = *src;
        dst[src - name] = c;
        ++src;
    } while (c);
    if (mode == 1)
    {
        orig::fn_00408d90(0xffffffffu, Field<uint32_t>(cam, 0x178), u(name), 0x80, 0, 0, 0, 0);
        const uint32_t r = orig::fn_0040e200(u(cam), 0x005bad94, Field<uint32_t>(cam, 0x178), 0);
        Field<uint8_t>(G(), 0xdce) = 1;
        return r;
    }
    if (mode == 2)
    {
        const uint32_t path = Field<uint32_t>(cam, 0x178);
        orig::fn_00408d90(0xffffffffu, path, u(name), 0x80, 0, 0, 0, 0);
        uint32_t m[17];
        orig::fn_00453c50(path, 0x005bad94, u(m), 1, 0, 0, 0, 0, 0);
        return orig::fn_004e2b40(u(m), path + 0x460);
    }
    return u(src);
}
static uint32_t Camera_StartPath_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Camera_StartPath(reinterpret_cast<const char*>(eax), P(a0), a1, a2);
}
ABI_ENTRY_0040ff50(Camera_StartPath_Entry, Camera_StartPath_Abi)
HOOK(0x0040ff50, "Camera_StartPath", Camera_StartPath_Entry, nullptr);

// 0x004100e0 (eax = camera, ecx = matrix in/out; caller 0x0040e350). Takes the "Camera01" (0x005bad94) matrix of the
// path object camera+0x178 (0x00453c50(path, "Camera01", &M1, 1, 0, 0, 0, 0, 0)), Matrix_Multiply 0x004e2910 (eax =
// path+0x460; stack: &M0, &M1), then (eax = &M0; stack: &M2, matrix) and copies M2 (17 dwords) over the matrix.
// Returns the second multiply's eax. Not executed in recorded runs.
uint32_t fn_004100e0(void* cam, uint32_t mtx)
{
    uint32_t M0[17], M1[17], M2[17];
    const uint32_t path = Field<uint32_t>(cam, 0x178);
    orig::fn_00453c50(path, 0x005bad94, u(M1), 1, 0, 0, 0, 0, 0);
    orig::fn_004e2910(path + 0x460, u(M0), u(M1));
    const uint32_t r = orig::fn_004e2910(u(M0), u(M2), mtx);
    std::memcpy(P(mtx), M2, sizeof M2);
    return r;
}
static uint32_t fn_004100e0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004100e0(P(eax), ecx);
}
ABI_ENTRY_004100e0(fn_004100e0_Entry, fn_004100e0_Abi)
HOOK(0x004100e0, "fn_004100e0", fn_004100e0_Entry, nullptr);

// 0x004101a0 (ecx = object; stack: 4; ret 0x10). CameraFXObject (vtable 0x005ad058) slot 6, the load slot:
// 0x00457810 (eax = object), the base load 0x004517b0 (ecx = object; same 4 arguments), property "position_only"
// (0x005bad78) read by 0x00453400 (eax = 0, esi = a3, edi = &a1 slot; stack: object, name, a2, 0) into the a1 stack
// slot; 0x004546d0 (esi = Game+0xa84 +0xed0 when set, else +0xecc; edi = object) (list registration, guess);
// "fx_name" (0x005bad70) by 0x004533a0 (eax = 0x0058a9a4, edi = object+0x460; stack: object, name, a2, a3, 0);
// "trigger_start" (0x005bad60) and "receive_trigger_kill" (0x005bb870) by 0x00453450 (eax = 0, ebx = name, esi =
// a3, edi = object+0x480 / +0x484; stack: object, a2, 0). With no start trigger (+0x480 = 0) fn_004102e0 (edx =
// object) runs at once. Last 0x004546d0 (esi = Game+0xa84 +0xe74, edi = object), whose eax is returned.
uint32_t CameraFXObject_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_00457810(u(obj));
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    orig::fn_00453400(0, a3, u(&a1), u(obj), 0x005bad78, a2, 0);
    void* lists = Field<void*>(G(), 0xa84);
    orig::fn_004546d0(a1 ? Field<uint32_t>(lists, 0xed0) : Field<uint32_t>(lists, 0xecc), u(obj));
    orig::fn_004533a0(0x0058a9a4, u(obj) + 0x460, u(obj), 0x005bad70, a2, a3, 0);
    orig::fn_00453450(0, 0x005bad60, a3, u(obj) + 0x480, u(obj), a2, 0);
    orig::fn_00453450(0, 0x005bb870, a3, u(obj) + 0x484, u(obj), a2, 0);
    if (!Field<uint32_t>(obj, 0x480))
        orig::fn_004102e0(u(obj));
    return static_cast<uint32_t>(orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe74), u(obj)));
}
static uint32_t CameraFXObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return CameraFXObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004101a0(CameraFXObject_Load_Entry, CameraFXObject_Load_Abi)
HOOK(0x004101a0, "CameraFXObject_Load", CameraFXObject_Load_Entry, nullptr);

// 0x004107c0 (ecx = object; stack: player, a1 (unused); ret 8). Cheats_Check (classifier name; strings
// ui_cheaton/ui_cheatoff/ui_cheatunknown). Vtable 0x005a76c8 slot 82 (+0x148), class descriptor not resolved.
// 1) When the text-entry object Game+0xa70 is active (byte +0x50, +0x54 set) and the UI (Game+0xab4) reports a
//    target through 0x00491360 (eax = UI, edx = &out2; stack: &out1), the widget 0x00463930(entry, out2, out1, 0)
//    whose vt+0x150 has bit 0x100 gets +0x474 |= 0x4000; if 0x00414b90 (edi = Game+0xa80; stack: 0x10004, 0.5)
//    returns true: 0x00414680 (eax = Game+0xa80, ecx = 0x23), 0x00403be0 (eax = widget+0x4f8 ->+4, edx =
//    object+0x4fc, edi = 0) (copies the typed code, guess), +0x530 = 0.5, +0x4f8 = 8, result = 1.
// 2) Timer +0x530 <= 0: result = 0x004103d0 (ebx = object); in state 8 the timer restarts at 0.5. Otherwise the
//    timer counts down by the frame time; on reaching 0 (+0x530 = +0x4c0 = 0) in state 8 the typed wide string
//    +0x4fc is compared (_wcsicmp 0x005665f0) with the 12 codes of the table 0x00589090 {text id, code, flag}
//    (SHOOTMOR, AMBULNCE, ...), 0x00410710 (edx = object), timer = 1.0. A match sets the flag in Game+0x48c (first
//    time: Options_Apply 0x00401ce0, al = 1, esi = game), toggles it in Game+0x4c8, shows
//    sprintf(Text_Lookup(on/off), Text_Lookup(cheat name)) with 0x00465970 (esi = object), then 0x00401f40(game,
//    0), 0x004919c0(player, 0), 0x00491850(eax = 0). No match: 0x00465850 (ecx = "ui_cheatunknown", edi = object;
//    stack: 1), 0x00491870(eax = 0).
// 3) With a player, when 0x00414730 (al = 1, ebx = 0x15 then 0x16, esi = Game+0xa80; stack: 0.5) reports either
//    button: 0x00410710, 0x00491870(0), 0x00490b60 (ecx = UI), 0x00414680(Game+0xa80, 0x23) and return true.
// Returns the result byte (upper eax bytes not reproduced). Not executed in recorded runs. The ecx inputs of
// _wcsicmp and 0x00465970 are leftovers (neither reads it): passed as 0.
uint32_t Cheats_Check(void* obj, uint32_t player, uint32_t /*a1*/)
{
    uint32_t out1 = 0, out2 = 0;
    uint8_t result = 0;
    void* entry = Field<void*>(G(), 0xa70);
    if (Field<uint8_t>(entry, 0x50) && Field<uint32_t>(entry, 0x54) &&
        static_cast<uint8_t>(orig::fn_00491360(Field<uint32_t>(G(), 0xab4), u(&out2), u(&out1))))
    {
        void* w = P(orig::fn_00463930(u(entry), out2, out1, 0));
        if (w && (VCall<uint32_t>(w, 0x150 / 4) & 0x100))
        {
            Field<uint32_t>(w, 0x474) |= 0x4000;
            if (static_cast<uint8_t>(orig::fn_00414b90(Field<uint32_t>(G(), 0xa80), 0x10004, 0x3f000000)))
            {
                orig::fn_00414680(Field<uint32_t>(G(), 0xa80), 0x23);
                orig::fn_00403be0(Field<uint32_t>(Field<void*>(w, 0x4f8), 4), u(obj) + 0x4fc, 0);
                Field<uint32_t>(obj, 0x530) = 0x3f000000;
                Field<uint32_t>(obj, 0x4f8) = 8;
                result = 1;
            }
        }
    }
    if (!(F(obj, 0x530) > kZero))  // test ah,0x41 / jne: <=, or unordered
    {
        result = static_cast<uint8_t>(orig::fn_004103d0(u(obj)));
        if (Field<uint32_t>(obj, 0x4f8) == 8)
            Field<uint32_t>(obj, 0x530) = 0x3f000000;
    }
    else
    {
        const float v = F(obj, 0x530) - kFrameTime;
        F(obj, 0x530) = v;
        if (v <= kZero)
        {
            Field<uint32_t>(obj, 0x530) = 0;
            Field<uint32_t>(obj, 0x4c0) = 0;
            if (Field<uint32_t>(obj, 0x4f8) == 8)
            {
                uint32_t* found = nullptr;
                uint32_t i = 0;
                for (uint32_t off = 0; off < 0x90; off += 0xc, ++i)
                {
                    if (orig::fn_005665f0(0, GameGlobal<uint32_t>(0x00589094 + off), u(obj) + 0x4fc) == 0)
                    {
                        found = &GameGlobal<uint32_t>(0x00589090 + i * 12);
                        break;
                    }
                }
                orig::fn_00410710(u(obj));
                Field<uint32_t>(obj, 0x530) = 0x3f800000;
                if (found)
                {
                    void* game = G();
                    const uint32_t flag = GameGlobal<uint32_t>(0x00589098 + i * 12);
                    const uint32_t have = Field<uint32_t>(game, 0x48c);
                    if (!(flag & have))
                    {
                        Field<uint32_t>(game, 0x48c) = have | flag;
                        orig::fn_00401ce0((flag & 0xffffff00u) | 1, u(game));
                        game = G();
                    }
                    Field<uint32_t>(game, 0x4c8) ^= GameGlobal<uint32_t>(0x00589098 + i * 12);
                    const uint32_t onOff =
                        (Field<uint32_t>(game, 0x4c8) & GameGlobal<uint32_t>(0x00589098 + i * 12)) ? 0x005bac4c
                                                                                                   : 0x005bac40;
                    const uint32_t nameText = orig::fn_00495f00(*found, Field<uint32_t>(game, 0xac0), 0);
                    const uint32_t fmtText = orig::fn_00495f00(onOff, Field<uint32_t>(G(), 0xac0), 0);
                    orig::fn_00465970(0, u(obj), fmtText, nameText);
                    orig::fn_00401f40(u(G()), 0);
                    orig::fn_004919c0(player, 0);
                    orig::fn_00491850(0);
                }
                else
                {
                    orig::fn_00465850(0x005bac30, u(obj), 1);
                    orig::fn_00491870(0);
                }
            }
        }
    }
    if (player)
    {
        uint32_t r = orig::fn_00414730((player & 0xffffff00u) | 1, 0x15, Field<uint32_t>(G(), 0xa80), 0x3f000000);
        if (!static_cast<uint8_t>(r))
            r = orig::fn_00414730((r & 0xffffff00u) | 1, 0x16, Field<uint32_t>(G(), 0xa80), 0x3f000000);
        if (static_cast<uint8_t>(r))
        {
            orig::fn_00410710(u(obj));
            orig::fn_00491870(0);
            orig::fn_00490b60(Field<uint32_t>(G(), 0xab4));
            orig::fn_00414680(Field<uint32_t>(G(), 0xa80), 0x23);
            return 1;
        }
    }
    return result;
}
static uint32_t Cheats_Check_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Cheats_Check(P(ecx), a0, a1);
}
ABI_ENTRY_004107c0(Cheats_Check_Entry, Cheats_Check_Abi)
HOOK(0x004107c0, "Cheats_Check", Cheats_Check_Entry, nullptr);

// 0x00410e00 (ecx = leftover, ebx = text id string). Looks a cheat up by its text id ("ui_cheat_fullammo", ...) in
// the 12-entry table 0x00589090 {text id, code, flag} with _stricmp 0x005448ee(name, entry id) and returns its flag
// (0 when none matches). _stricmp does not read ecx; the caller's ecx is passed through.
uint32_t Cheat_FlagByName(uint32_t ecx, uint32_t name)
{
    uint32_t i = 0;
    for (uint32_t off = 0; off < 0x90; off += 0xc, ++i)
    {
        if (orig::fn_005448ee(ecx, name, GameGlobal<uint32_t>(0x00589090 + off)) == 0)
            return GameGlobal<uint32_t>(0x00589098 + i * 12);
    }
    return 0;
}
static uint32_t Cheat_FlagByName_Abi(uint32_t ecx, uint32_t ebx)
{
    return Cheat_FlagByName(ecx, ebx);
}
ABI_ENTRY_00410e00(Cheat_FlagByName_Entry, Cheat_FlagByName_Abi)
HOOK(0x00410e00, "Cheat_FlagByName", Cheat_FlagByName_Entry, nullptr);

// 0x00410e40 (ebx = cheat text id; stack: enable byte; caller pops; caller 0x004a0850). Unlocks the cheat
// (Cheat_FlagByName): when its flag is not yet in Game+0x48c it is added and Options_Apply 0x00401ce0 runs (al = 1,
// upper bytes from the new mask; esi = game); with the enable byte the flag is also set in Game+0x4c8 (active
// cheats) and 0x00401f40(game, 0) (save options, guess). Not executed in recorded runs. The caller's ecx (passed to
// Cheat_FlagByName, unread) is not an ABI input here: 0.
uint32_t Cheat_Unlock(uint32_t name, uint32_t enable)
{
    const uint32_t flag = Cheat_FlagByName(0, name);
    if (!flag)
        return 0;
    void* game = G();
    uint32_t r = Field<uint32_t>(game, 0x48c);
    if (!(flag & r))
    {
        r |= flag;
        Field<uint32_t>(game, 0x48c) = r;
        r = orig::fn_00401ce0((r & 0xffffff00u) | 1, u(game));
        game = G();
    }
    if (!static_cast<uint8_t>(enable))
        return r & 0xffffff00u;
    Field<uint32_t>(game, 0x4c8) |= flag;
    return orig::fn_00401f40(u(game), 0);
}
static uint32_t Cheat_Unlock_Abi(uint32_t ebx, uint32_t a0)
{
    return Cheat_Unlock(ebx, a0);
}
ABI_ENTRY_00410e40(Cheat_Unlock_Entry, Cheat_Unlock_Abi)
HOOK(0x00410e40, "Cheat_Unlock", Cheat_Unlock_Entry, nullptr);

// 0x00410ea0 (ebx = cheat text id; caller 0x004a0850). Returns whether the cheat's flag (Cheat_FlagByName) is in the
// unlocked mask Game+0x48c (setne al; upper eax bytes from the flag).
uint32_t Cheat_IsUnlocked(uint32_t name)
{
    const uint32_t flag = Cheat_FlagByName(0, name);
    return (flag & 0xffffff00u) | ((Field<uint32_t>(G(), 0x48c) & flag) ? 1u : 0u);
}
static uint32_t Cheat_IsUnlocked_Abi(uint32_t ebx)
{
    return Cheat_IsUnlocked(ebx);
}
ABI_ENTRY_00410ea0(Cheat_IsUnlocked_Entry, Cheat_IsUnlocked_Abi)
HOOK(0x00410ea0, "Cheat_IsUnlocked", Cheat_IsUnlocked_Entry, nullptr);

// 0x00415770 (ecx = object; stack: 4; ret 0x10). Counter_Init: CounterObject (vtable 0x005a4b00) slot 6, the load
// slot. +0x475 (deactivated) = 0, base load 0x004517b0 (same arguments), triggers "receive_trigger_activate" ->
// +0x460, "receive_trigger_deactivate" -> +0x464, "send_trigger" -> +0x468 (0x00453450: eax = 0, ebx = name, esi =
// a3, edi = field; stack: object, a2, 0). Without a send trigger: Debug_Printf 0x00407ff0(Game+0x34, "Counter
// Sending 0 Trigger\n%s", Object_GetName(object)). "count" -> +0x470 (0x00453400, eax = 1), "infinite" -> byte +0x474
// (0x004534f0, al = 0), +0x46c (current count) = 0, then vt+0xf4(0). Not executed in recorded runs. Debug_Printf
// (variadic, caller pops) gets one extra 0 argument from the 4-argument wrapper.
uint32_t Counter_Init(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Field<uint8_t>(obj, 0x475) = 0;
    orig::fn_004517b0(u(obj), a0, a1, a2, a3);
    orig::fn_00453450(0, 0x005bba00, a3, u(obj) + 0x460, u(obj), a2, 0);
    orig::fn_00453450(0, 0x005bb780, a3, u(obj) + 0x464, u(obj), a2, 0);
    orig::fn_00453450(0, 0x005b9774, a3, u(obj) + 0x468, u(obj), a2, 0);
    if (!Field<uint32_t>(obj, 0x468))
    {
        const uint32_t name = orig::fn_00453270(u(obj));
        orig::fn_00407ff0(Field<uint32_t>(G(), 0x34), 0x005b9754, name, 0);
    }
    const uint32_t r = orig::fn_00453400(1, a3, u(obj) + 0x470, u(obj), 0x005bbee0, a2, 0);
    orig::fn_004534f0(r & 0xffffff00u, a3, u(obj) + 0x474, u(obj), 0x005b9748, a2, 0);
    Field<uint32_t>(obj, 0x46c) = 0;
    return VCall<uint32_t>(obj, 0xf4 / 4, 0u);
}
static uint32_t Counter_Init_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Counter_Init(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00415770(Counter_Init_Entry, Counter_Init_Abi)
HOOK(0x00415770, "Counter_Init", Counter_Init_Entry, nullptr);

// 0x00415860 (ecx = object; stack: a0 (unused), trigger id; ret 8). CounterObject (vtable 0x005a4b00) slot 14 (+0x38),
// the trigger receiver. Activate trigger (+0x460) while not deactivated (+0x475): ++count (+0x46c); on reaching
// +0x470 Object_SendTrigger 0x00461870 (eax = Game+0xa84, ebx = +0x468; stack: object), then with "infinite" (+0x474)
// the count restarts at 0, otherwise vt+0xdc() (finish, guess). Deactivate trigger (+0x464): +0x475 = 1, vt+0xdc().
// Returns false (al = 0; upper eax bytes not reproduced). Not executed in recorded runs.
uint32_t CounterObject_OnTrigger(void* obj, uint32_t /*a0*/, uint32_t id)
{
    if (id == Field<uint32_t>(obj, 0x460))
    {
        if (Field<uint8_t>(obj, 0x475))
            return 0;
        const uint32_t n = Field<uint32_t>(obj, 0x46c) + 1;
        Field<uint32_t>(obj, 0x46c) = n;
        if (n != Field<uint32_t>(obj, 0x470))
            return 0;
        orig::fn_00461870(Field<uint32_t>(G(), 0xa84), Field<uint32_t>(obj, 0x468), u(obj));
        if (Field<uint8_t>(obj, 0x474))
        {
            Field<uint32_t>(obj, 0x46c) = 0;
            return 0;
        }
        VCall<void>(obj, 0xdc / 4);
        return 0;
    }
    if (id == Field<uint32_t>(obj, 0x464))
    {
        Field<uint8_t>(obj, 0x475) = 1;
        VCall<void>(obj, 0xdc / 4);
    }
    return 0;
}
static uint32_t CounterObject_OnTrigger_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return CounterObject_OnTrigger(P(ecx), a0, a1);
}
ABI_ENTRY_00415860(CounterObject_OnTrigger_Entry, CounterObject_OnTrigger_Abi)
HOOK(0x00415860, "CounterObject_OnTrigger", CounterObject_OnTrigger_Entry, nullptr);

// 0x00415900 (eax = object). Returns the dword at +0x470.
uint32_t fn_00415900(void* obj)
{
    return Field<uint32_t>(obj, 0x470);
}
static uint32_t fn_00415900_Abi(uint32_t eax)
{
    return fn_00415900(P(eax));
}
ABI_ENTRY_00415900(fn_00415900_Entry, fn_00415900_Abi)
HOOK(0x00415900, "fn_00415900", fn_00415900_Entry, nullptr);

// 0x0041c180 (ecx = object; stack: 4; ret 0x10). FxExplosion (vtable 0x005a4300) slot 6, the load slot (classifier:
// PowderKeg_Init; strings IC_powder_*, damage_radius_*). +0x487 = 0; without a2: 0x00457810 (eax = object), +0x424
// |= 0x100000. Base load 0x004517b0. With a2: Object_setPhysicsEnabled 0x004577b0 (eax = object; stack: 1),
// vt+0xf8(1, 0), "block_path" (0x004534f0, al = 1) into the a0 stack slot; when set vt+0xfc(1, 2) and +0x487 = 1.
// Without a2: vt+0xf8(0x20000, 0). Four list registrations 0x004546d0 (esi = Game+0xa84 +0xe90, +0xe94, +0xea0,
// +0xe9c; edi = object). +0x460 = 0, "trigger_los" (al = 0) into the a0 slot; "IC_powder_1..3" (0x00453450) into the
// a1 slot, each found one spawning a grunt through 0x00456180 (ecx = "ref_grunt_powder_N", esi = object; stack:
// "grunt_regular", "Mover_grunt_powder_N", a1 slot, 0, a0 slot, 0, 0). Floats (0x004534a0, esi = a3, edi = field;
// stack: object, name, a2, default, 0): start_delay +0x478 (1.0), acceleration_rate +0x47c (0.75), min_delay +0x480
// (0.1); bytes player_damage +0x484 (default 1), teammate_damage +0x485, badguy_damage +0x486; triggers
// send_trigger_on_death +0x488, receive_trigger_kill +0x48c; impact_force +0x474 (2010.0), area_damage +0x464,
// damage_radius_inner +0x468, damage_radius_outer +0x46c. Finally +0x470 = 1 / (outer - inner) when outer > 0.01
// ([0x005bcbec]) and outer > inner, else 1.0. The original reuses its a0/a1 argument slots as outputs: kept as a
// local copy of the four arguments. Returns the last loader's eax (the original leaves fnstsw bits in ax).
uint32_t FxExplosion_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4] = {a0, a1, a2, a3};
    const uint32_t o = u(obj);
    Field<uint8_t>(obj, 0x487) = 0;
    if (!a2)
    {
        orig::fn_00457810(o);
        Field<uint32_t>(obj, 0x424) |= 0x100000;
    }
    orig::fn_004517b0(o, a0, a1, a2, a3);
    uint32_t r;
    if (a2)
    {
        orig::fn_004577b0(o, 1);
        r = VCall<uint32_t>(obj, 0xf8 / 4, 1u, 0u);
        orig::fn_004534f0((r & 0xffffff00u) | 1, a3, u(&args[0]), o, 0x005b94ac, a2, 0);
        if (static_cast<uint8_t>(args[0]))
        {
            VCall<void>(obj, 0xfc / 4, 1u, 2u);
            Field<uint8_t>(obj, 0x487) = 1;
        }
    }
    else
        VCall<void>(obj, 0xf8 / 4, 0x20000u, 0u);
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe90), o);
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe94), o);
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xea0), o);
    r = static_cast<uint32_t>(orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe9c), o));
    Field<uint32_t>(obj, 0x460) = 0;
    orig::fn_004534f0(r & 0xffffff00u, a3, u(&args[0]), o, 0x005bb68c, a2, 0);
    static const uint32_t kPowder[3][3] = {{0x005b94a0, 0x005b9488, 0x005b9474},
                                           {0x005b9468, 0x005b9450, 0x005b943c},
                                           {0x005b9430, 0x005b9418, 0x005b9404}};
    for (int i = 0; i < 3; ++i)
    {
        orig::fn_00453450(0, kPowder[i][0], a3, u(&args[1]), o, a2, 0);
        if (args[1])
            orig::fn_00456180(kPowder[i][2], o, 0x005bbfe8, kPowder[i][1], args[1], 0, args[0], 0, 0);
    }
    orig::fn_004534a0(a3, o + 0x478, o, 0x005b93f8, a2, 0x3f800000, 0);
    orig::fn_004534a0(a3, o + 0x47c, o, 0x005b93e4, a2, 0x3f400000, 0);
    r = orig::fn_004534a0(a3, o + 0x480, o, 0x005b93d8, a2, 0x3dcccccd, 0);
    r = orig::fn_004534f0((r & 0xffffff00u) | 1, a3, o + 0x484, o, 0x005bb910, a2, 0);
    r = orig::fn_004534f0(r & 0xffffff00u, a3, o + 0x485, o, 0x005bb900, a2, 0);
    orig::fn_004534f0(r & 0xffffff00u, a3, o + 0x486, o, 0x005bb8f0, a2, 0);
    orig::fn_00453450(0, 0x005bb888, a3, o + 0x488, o, a2, 0);
    orig::fn_00453450(0, 0x005bb870, a3, o + 0x48c, o, a2, 0);
    orig::fn_004534a0(a3, o + 0x474, o, 0x005b93c8, a2, 0x44fb4000, 0);
    orig::fn_004534a0(a3, o + 0x464, o, 0x005b93bc, a2, 0, 0);
    orig::fn_004534a0(a3, o + 0x468, o, 0x005b93a8, a2, 0, 0);
    r = orig::fn_004534a0(a3, o + 0x46c, o, 0x005b9394, a2, 0, 0);
    const float outer = F(obj, 0x46c);
    if (outer > GameGlobal<float>(0x005bcbec) && outer > F(obj, 0x468))
        F(obj, 0x470) = kOne / (outer - F(obj, 0x468));
    else
        Field<uint32_t>(obj, 0x470) = 0x3f800000;
    return r;
}
static uint32_t FxExplosion_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return FxExplosion_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0041c180(FxExplosion_Load_Entry, FxExplosion_Load_Abi)
HOOK(0x0041c180, "FxExplosion_Load", FxExplosion_Load_Entry, nullptr);

// 0x0041c4e0 (ecx = explosion; stack: damage record; ret 4). FxExplosion (vtable 0x005a4300) slot 52 (+0xd0), the
// damage handler (guess, from the player_damage / teammate_damage / badguy_damage bytes it checks). The record's
// source (+0x2c) decides: source vt+0xe0() true, or record flags (+0) & 0x180000, pass directly; otherwise the
// player (Game+0x748) needs player_damage (+0x484), a source whose vt+0xe4() is true needs teammate_damage (+0x485),
// and then a source whose vt+0xe8() is true needs badguy_damage (+0x486). Passing records go to the base handler
// 0x004555e0 (ecx = explosion; stack: record). Returns eax as left (al = the last flag tested otherwise).
uint32_t FxExplosion_OnDamage(void* obj, void* rec)
{
    if (!rec || !Field<void*>(rec, 0x2c))
        return 0;  // original: eax as left by the caller
    uint32_t r = VCall<uint32_t>(Field<void*>(rec, 0x2c), 0xe0 / 4);
    bool apply = static_cast<uint8_t>(r) || (Field<uint32_t>(rec, 0) & 0x180000);
    if (!apply)
    {
        void* src = Field<void*>(rec, 0x2c);
        if (src == Field<void*>(G(), 0x748))
        {
            r = (r & 0xffffff00u) | Field<uint8_t>(obj, 0x484);
            if (!static_cast<uint8_t>(r))
                return r;
        }
        else
        {
            r = VCall<uint32_t>(src, 0xe4 / 4);
            if (static_cast<uint8_t>(r))
            {
                r = (r & 0xffffff00u) | Field<uint8_t>(obj, 0x485);
                if (!static_cast<uint8_t>(r))
                    return r;
            }
        }
        r = VCall<uint32_t>(Field<void*>(rec, 0x2c), 0xe8 / 4);
        if (static_cast<uint8_t>(r))
        {
            r = (r & 0xffffff00u) | Field<uint8_t>(obj, 0x486);
            if (!static_cast<uint8_t>(r))
                return r;
        }
    }
    return orig::fn_004555e0(u(obj), u(rec));
}
static uint32_t FxExplosion_OnDamage_Abi(uint32_t ecx, uint32_t a0)
{
    return FxExplosion_OnDamage(P(ecx), P(a0));
}
ABI_ENTRY_0041c4e0(FxExplosion_OnDamage_Entry, FxExplosion_OnDamage_Abi)
HOOK(0x0041c4e0, "FxExplosion_OnDamage", FxExplosion_OnDamage_Entry, nullptr);

// 0x0041c570 (ecx = explosion; stack: damage record or 0; ret 4). FxExplosion (vtable 0x005a4300) slot 53 (+0xd4),
// the die/kill slot (Character_Die 0x004319e0 is slot 53 of the character classes). A record with flag 0x40 (chain
// reaction, guess) only lights the fuse when +0x460 <= 0: +0x460 = +0x478 = max(*record+0x30, min_delay +0x480).
// Otherwise: with block_path (+0x487) vt+0xfc(0, 2); send_trigger_on_death (+0x488) via Object_SendTrigger
// 0x00461870 (eax = Game+0xa84, ebx = trigger; stack: explosion); Object_Kill 0x00455640 (ecx = explosion; stack:
// record); when area_damage (+0x464) is not 0: position = vt+0x54(&pos), delay = +0x478 (with a record) or +0x47c *
// +0x478 (kept in the record argument's stack slot), 0x0047f020 (ebx = &area_damage; stack: explosion, &pos,
// explosion, impact_force +0x474, explosion, 0x40, &delay) (area damage, guess), then area_damage = 0. Not executed in
// recorded runs.
uint32_t FxExplosion_Die(void* obj, uint32_t rec)
{
    if (rec && (Field<uint8_t>(P(rec), 0) & 0x40))
    {
        if (F(obj, 0x460) > kZero || F(obj, 0x460) != F(obj, 0x460))  // jp: greater or unordered
            return 0;
        const float v = *Field<float*>(P(rec), 0x30);
        if (v < F(obj, 0x480))
        {
            const uint32_t m = Field<uint32_t>(obj, 0x480);
            Field<uint32_t>(obj, 0x460) = m;
            Field<uint32_t>(obj, 0x478) = m;
        }
        else
        {
            F(obj, 0x460) = v;
            Field<uint32_t>(obj, 0x478) = Field<uint32_t>(obj, 0x460);
        }
        return 0;
    }
    if (Field<uint8_t>(obj, 0x487))
        VCall<void>(obj, 0xfc / 4, 0u, 2u);
    const uint32_t trig = Field<uint32_t>(obj, 0x488);
    if (trig)
        orig::fn_00461870(Field<uint32_t>(G(), 0xa84), trig, u(obj));
    uint32_t r = orig::fn_00455640(u(obj), rec);
    if (F(obj, 0x464) == kZero)
        return r;
    float pos[3];
    VCall<void>(obj, 0x54 / 4, pos);
    uint32_t slot;  // the original stores the delay over its argument slot
    if (rec)
        slot = Field<uint32_t>(obj, 0x478);
    else
    {
        const float d = F(obj, 0x47c) * F(obj, 0x478);
        slot = f2u(d);
    }
    r = orig::fn_0047f020(u(obj) + 0x464, u(obj), u(pos), u(obj), Field<uint32_t>(obj, 0x474), u(obj), 0x40, u(&slot));
    Field<uint32_t>(obj, 0x464) = 0;
    return r;
}
static uint32_t FxExplosion_Die_Abi(uint32_t ecx, uint32_t a0)
{
    return FxExplosion_Die(P(ecx), a0);
}
ABI_ENTRY_0041c570(FxExplosion_Die_Entry, FxExplosion_Die_Abi)
HOOK(0x0041c570, "FxExplosion_Die", FxExplosion_Die_Entry, nullptr);

// 0x0041c6a0 (ecx = explosion). FxExplosion (vtable 0x005a4300) slot 47 (+0xbc), the per-frame update: a running fuse
// (+0x460 > 0) counts down by the frame time and on reaching 0 calls vt+0xd4(0) (FxExplosion_Die). Returns eax as
// left (fnstsw bits; not reproduced). Not executed in recorded runs.
uint32_t FxExplosion_Update(void* obj)
{
    if (!(F(obj, 0x460) > kZero))
        return 0;
    const float v = F(obj, 0x460) - kFrameTime;
    F(obj, 0x460) = v;
    if (v <= kZero)
        return VCall<uint32_t>(obj, 0xd4 / 4, 0u);
    return 0;
}
static uint32_t FxExplosion_Update_Abi(uint32_t ecx)
{
    return FxExplosion_Update(P(ecx));
}
ABI_ENTRY_0041c6a0(FxExplosion_Update_Entry, FxExplosion_Update_Abi)
HOOK(0x0041c6a0, "FxExplosion_Update", FxExplosion_Update_Entry, nullptr);

// 0x0041c6e0 (ecx = explosion; stack: a0 (unused), trigger id; ret 8). FxExplosion (vtable 0x005a4300) slot 14
// (+0x38), the trigger receiver: receive_trigger_kill (+0x48c) -> vt+0xd4(0). Returns false (al = 0; upper eax bytes
// not reproduced). Not executed in recorded runs.
uint32_t FxExplosion_OnTrigger(void* obj, uint32_t /*a0*/, uint32_t id)
{
    if (id == Field<uint32_t>(obj, 0x48c))
        VCall<void>(obj, 0xd4 / 4, 0u);
    return 0;
}
static uint32_t FxExplosion_OnTrigger_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return FxExplosion_OnTrigger(P(ecx), a0, a1);
}
ABI_ENTRY_0041c6e0(FxExplosion_OnTrigger_Entry, FxExplosion_OnTrigger_Abi)
HOOK(0x0041c6e0, "FxExplosion_OnTrigger", FxExplosion_OnTrigger_Entry, nullptr);

// 0x004201f0 (eax = effect handle or 0; stack: 4; ret 0x10; caller 0x00420770). Screen shake setup: the handle's
// object (+4) gets +0x4d4 = a0, +0x4dc = a1, +0x4d8 = a2 and flag 0x8000 in +0x460; returns &object+0x460. With no
// handle: Debug_Printf 0x00407ff0 (an empty stub) (Game+0x34, 10, "Fx : Failed to set screen shake (no push)\n%s",
// a0 ->+8 +0x24).
uint32_t FxScreenShake_Set(void* h, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t /*a3*/)
{
    if (!h)
    {
        const uint32_t name = Field<uint32_t>(P(a0), 8) + 0x24;
        return orig::fn_00407ff0(Field<uint32_t>(G(), 0x34), 0xa, 0x005b7fac, name);
    }
    Field<uint32_t>(Field<void*>(h, 4), 0x4d8) = a2;
    Field<uint32_t>(Field<void*>(h, 4), 0x4dc) = a1;
    Field<uint32_t>(Field<void*>(h, 4), 0x4d4) = a0;
    void* o = Field<void*>(h, 4);
    Field<uint32_t>(o, 0x460) |= 0x8000;
    return u(o) + 0x460;
}
static uint32_t FxScreenShake_Set_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return FxScreenShake_Set(P(eax), a0, a1, a2, a3);
}
ABI_ENTRY_004201f0(FxScreenShake_Set_Entry, FxScreenShake_Set_Abi)
HOOK(0x004201f0, "FxScreenShake_Set", FxScreenShake_Set_Entry, nullptr);

// 0x00427100 (eax = character; callers 0x00431310, Character_Die 0x004319e0, 0x00436010). Death setup (guess): +0x9f0
// = +0x9f4 = 0.5, Object_setPhysicsEnabled 0x004577b0 (eax = character; stack: 1); animation id 0x5b for types
// (+0x460) 0xe8 / 0xea, else 0x56 with flag 0x20000 in +0x484, 0x57 with flag 0x20, else 0x50 + rand() * 6 >> 15
// (_rand 0x005391cd). Then 0x00454750 (esi = Game+0xa84 +0xe9c, edi = character), vt+0xf8(0x60000, 0),
// vt+0x19c(anim, 0, 0, 0, 0, 0), vt+0x1b8(0x191). Not executed in recorded runs.
uint32_t fn_00427100(void* ch)
{
    Field<uint32_t>(ch, 0x9f0) = 0x3f000000;
    Field<uint32_t>(ch, 0x9f4) = 0x3f000000;
    orig::fn_004577b0(u(ch), 1);
    const uint32_t type = Field<uint32_t>(ch, 0x460);
    int32_t anim;
    if (type == 0xe8 || type == 0xea)
        anim = 0x5b;
    else
    {
        const uint32_t flags = Field<uint32_t>(ch, 0x484);
        if (flags & 0x20000)
            anim = 0x56;
        else if (flags & 0x20)
            anim = 0x57;
        else
        {
            const int32_t r = static_cast<int32_t>(orig::fn_005391cd());
            anim = ((r * 6) >> 15) + 0x50;
        }
    }
    orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe9c), u(ch));
    VCall<void>(ch, 0xf8 / 4, 0x60000u, 0u);
    VCall<void>(ch, 0x19c / 4, static_cast<uint32_t>(anim), 0u, 0u, 0u, 0u, 0u);
    return VCall<uint32_t>(ch, 0x1b8 / 4, 0x191u);
}
static uint32_t fn_00427100_Abi(uint32_t eax)
{
    return fn_00427100(P(eax));
}
ABI_ENTRY_00427100(fn_00427100_Entry, fn_00427100_Abi)
HOOK(0x00427100, "fn_00427100", fn_00427100_Entry, nullptr);

// Calls 0x00481ab0 (eax, esi; stack: 1). symbols/abi.csv has "no ret found": it ends in a tail jump to the object's
// vt+0xd4 (ecx = esi) after zeroing its own stack argument, and that thiscall slot pops the argument (the caller
// 0x0042e1c0 does not pop it). So the shim pushes one argument and lets the callee pop it.
static uint32_t Call_00481ab0(uint32_t eax_, uint32_t esi_, uint32_t a0)
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

// The inlined reference release used by 0x0042e1c0 (and 0x00481ab0): when *slot is set, --(*slot)+0x28; at zero,
// unless its vt+0xd8() returns true, 0x00487da0 (eax = object) frees it; then *slot = 0.
static uint32_t ReleaseRef(uint32_t* slot, uint32_t r)
{
    void* o = P(*slot);
    if (!o)
        return r;
    if (--Field<int32_t>(o, 0x28) == 0)
    {
        r = VCall<uint32_t>(o, 0xd8 / 4);
        if (!static_cast<uint8_t>(r))
            r = orig::fn_00487da0(u(o));
    }
    *slot = 0;
    return r;
}

// The shared "fall over" block of 0x0042e1c0 (types 0xe8/0xea/0x193, and 0x195/0x197/0x19b when the ragdoll test
// fails): a zeroed 13-dword record, FxBodyfall_Spawn 0x0043a8d0 (esi = character), record[6..8] = +0x548..+0x550,
// record[9] = -(rand(2.0, 3.5) (0x0046e2c0) * 0x004581c0(eax = character) (st0) * +0x554), record[4] =
// 0x00458720(eax = character, edx = 0), record[11] = 0, then vt+0x17c(&record, flag).
static uint32_t CharacterDeath_Fall(void* ch, uint32_t flag)
{
    uint32_t rec[13] = {};
    orig::fn_0043a8d0(u(ch));
    rec[6] = Field<uint32_t>(ch, 0x548);
    rec[7] = Field<uint32_t>(ch, 0x54c);
    rec[8] = Field<uint32_t>(ch, 0x550);
    const float m = orig::fn_004581c0(u(ch));
    const float rnd = orig::fn_0046e2c0(0, 0x40000000, 0x40600000);
    const float v = rnd * m * F(ch, 0x554);
    rec[9] = f2u(-v);
    rec[4] = orig::fn_00458720(u(ch), 0);
    rec[11] = 0;
    return VCall<uint32_t>(ch, 0x17c / 4, rec, flag);
}

// The turbo reset shared by cases 0xb8-0xbe and 0xbd/0xc4/0xc5 of 0x0042e1c0: +0xb20 = 0, +0x484 &= 0xcfffffff,
// 0x0047bb30 (eax = +0xb08, esi = character, edi = character+0xb0c; stack: 0; caller pops), 0x00408e30 (eax = 0x81,
// ecx = +0xb08, edx = -1; stack: "turbo_idle", 0, 0.25, 0, 0), 0x00454750 (esi = Game+0xa84 +0xf1c, edi = character).
static void CharacterDeath_StopTurbo(void* ch)
{
    Field<uint32_t>(ch, 0xb20) = 0;
    Field<uint32_t>(ch, 0x484) &= 0xcfffffffu;
    orig::fn_0047bb30(Field<uint32_t>(ch, 0xb08), u(ch), u(ch) + 0xb0c, 0);
    orig::fn_00408e30(0x81, Field<uint32_t>(ch, 0xb08), 0xffffffffu, 0x005b6674, 0, 0x3e800000, 0, 0);
    orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xf1c), u(ch));
}

// 0x0042e1c0 (ebx = character; caller 0x0043ee90). Runs a pending death reaction (flag 0x20 of byte +0x48c, cleared
// here) by character type +0x460 (jump tables 0x0042e750 / 0x0042e810): 0x20, 0x3d, 0x5b, 0x7b, 0xa0, 0xa1 ->
// 0x0046f440 (esi = Game+0xaa4, edi = character); 0x3e -> vt+0x23c(0, 0); 0x46 -> vt+0x240(1, 0, 0); 0xb8-0xba, 0xbe
// -> turbo reset, then when 0x00454c20 (ecx = character) is true: zero velocity (Body_setLinearVelocity 0x00458510,
// eax = &zero, edx = character) and either vt+0x19c(0x6c, 0, 0, 0, 0, 0) + vt+0x1b8(0xc0) or (flag 0x20000000 of
// +0x484) vt+0x1a0(0x2a, 0x2b, 0x28, 0x29, 0, 0, +0x580, +0x584) + vt+0x1b8(0xc1), each followed by 0x00454c90 (eax =
// character, cl = 1; stack: 1); else +0x580 = +0x584 = 0.5, +0x4ac..+0x4bc = 0x32, 0x2a, 0x2b, 0x28, 0x29, +0x890 =
// 10.0, vt+0x19c(0x61, 1, 0, 0, 0, 0), vt+0x1b8(0x82), vt+0x188(); 0xbd, 0xc4, 0xc5 -> turbo reset, vt+0x19c(0x6c,
// ...), vt+0x1b8(0xbf), 0x00454c90, 0x00454cc0 (esi = character, edi = character+0x398); 0xc9 -> renderer (+0x2c)
// vt+0xd8(character+0xb4c, 0), then 0x00481ab0 (eax = 0, esi = +0xb54; stack: 0) when +0xb54 is set, else
// 0x00454690 / 0x00454750 on list Game+0xa84 +0xf1c and 0x0042ce70 (eax = +0x588 or the player Game+0x748, ecx =
// character); 0xe8, 0xea, 0x193 -> the fall block (flag 0); 0x195, 0x197, 0x19b -> when (0x00454bc0 or 0x0044e600)
// and vt+0x210() are true a ragdoll death (FxBodyfall_Spawn, 0x00431720(character, 0), +0x9f0 = +0x9f4 = 0.5,
// Object_setPhysicsEnabled(1), vt+0xf8(0x60000, 0), vt+0x19c(0x5d, ...), vt+0x1b8(0x191)), else the fall block
// (flag 1); 0x19d -> release the reference +0x870. Every handled path ends by releasing the reference +0x538.
// Returns eax as the original leaves it where cheap (the byte +0x48c when nothing is pending). Guess: a deferred
// death/knock-down reaction (animation ids, ragdoll, FxBodyfall_Spawn); not named until verified.
uint32_t fn_0042e1c0(void* ch)
{
    const uint8_t pending = Field<uint8_t>(ch, 0x48c);
    if (!(pending & 0x20))
        return pending;
    Field<uint32_t>(ch, 0x48c) &= 0xffffffdfu;
    const uint32_t type = Field<uint32_t>(ch, 0x460);
    uint32_t r = type;
    switch (type)
    {
    case 0x20:
    case 0x3d:
    case 0x5b:
    case 0x7b:
    case 0xa0:
    case 0xa1:
        r = orig::fn_0046f440(Field<uint32_t>(G(), 0xaa4), u(ch));
        break;
    case 0x3e:
        r = VCall<uint32_t>(ch, 0x23c / 4, 0u, 0u);
        break;
    case 0x46:
        r = VCall<uint32_t>(ch, 0x240 / 4, 1u, 0u, 0u);
        break;
    case 0xb8:
    case 0xb9:
    case 0xba:
    case 0xbe:
        CharacterDeath_StopTurbo(ch);
        if (static_cast<uint8_t>(orig::fn_00454c20(u(ch))))
        {
            uint32_t zero[3] = {0, 0, 0};
            orig::fn_00458510(u(zero), u(ch));
            if (!(Field<uint32_t>(ch, 0x484) & 0x20000000))
            {
                VCall<void>(ch, 0x19c / 4, 0x6cu, 0u, 0u, 0u, 0u, 0u);
                VCall<void>(ch, 0x1b8 / 4, 0xc0u);
            }
            else
            {
                VCall<void>(ch, 0x1a0 / 4, 0x2au, 0x2bu, 0x28u, 0x29u, 0u, 0u, Field<uint32_t>(ch, 0x580),
                            Field<uint32_t>(ch, 0x584));
                VCall<void>(ch, 0x1b8 / 4, 0xc1u);
            }
            r = orig::fn_00454c90(u(ch), 1, 1);
        }
        else
        {
            Field<uint32_t>(ch, 0x580) = 0x3f000000;
            Field<uint32_t>(ch, 0x584) = 0x3f000000;
            Field<uint32_t>(ch, 0x4b0) = 0x2a;
            Field<uint32_t>(ch, 0x4b4) = 0x2b;
            Field<uint32_t>(ch, 0x4b8) = 0x28;
            Field<uint32_t>(ch, 0x4bc) = 0x29;
            Field<uint32_t>(ch, 0x4ac) = 0x32;
            Field<uint32_t>(ch, 0x890) = 0x41200000;
            VCall<void>(ch, 0x19c / 4, 0x61u, 1u, 0u, 0u, 0u, 0u);
            VCall<void>(ch, 0x1b8 / 4, 0x82u);
            r = VCall<uint32_t>(ch, 0x188 / 4);
        }
        break;
    case 0xbd:
    case 0xc4:
    case 0xc5:
        CharacterDeath_StopTurbo(ch);
        VCall<void>(ch, 0x19c / 4, 0x6cu, 0u, 0u, 0u, 0u, 0u);
        VCall<void>(ch, 0x1b8 / 4, 0xbfu);
        orig::fn_00454c90(u(ch), 1, 1);
        r = static_cast<uint32_t>(orig::fn_00454cc0(u(ch), u(ch) + 0x398));
        break;
    case 0xc9:
    {
        VCall<void>(Field<void*>(G(), 0x2c), 0xd8 / 4, u(ch) + 0xb4c, 0u);
        const uint32_t b54 = Field<uint32_t>(ch, 0xb54);
        if (b54)
            r = Call_00481ab0(0, b54, 0);
        else
        {
            if (static_cast<uint8_t>(orig::fn_00454690(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xf1c), u(ch))))
                orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xf1c), u(ch));
            const uint32_t target = Field<uint32_t>(ch, 0x588);
            r = orig::fn_0042ce70(target ? target : Field<uint32_t>(G(), 0x748), u(ch));
        }
        break;
    }
    case 0xe8:
    case 0xea:
    case 0x193:
        r = CharacterDeath_Fall(ch, 0);
        break;
    case 0x195:
    case 0x197:
    case 0x19b:
        if ((static_cast<uint8_t>(orig::fn_00454bc0(u(ch))) || static_cast<uint8_t>(fn_0044e600())) &&
            static_cast<uint8_t>(VCall<uint32_t>(ch, 0x210 / 4)))
        {
            orig::fn_0043a8d0(u(ch));
            orig::fn_00431720(u(ch), 0);
            Field<uint32_t>(ch, 0x9f0) = 0x3f000000;
            Field<uint32_t>(ch, 0x9f4) = 0x3f000000;
            orig::fn_004577b0(u(ch), 1);
            VCall<void>(ch, 0xf8 / 4, 0x60000u, 0u);
            VCall<void>(ch, 0x19c / 4, 0x5du, 0u, 0u, 0u, 0u, 0u);
            r = VCall<uint32_t>(ch, 0x1b8 / 4, 0x191u);
        }
        else
            r = CharacterDeath_Fall(ch, 1);
        break;
    case 0x19d:
        r = ReleaseRef(&Field<uint32_t>(ch, 0x870), r);
        break;
    default:
        break;
    }
    return ReleaseRef(&Field<uint32_t>(ch, 0x538), r);
}
static uint32_t fn_0042e1c0_Abi(uint32_t ebx)
{
    return fn_0042e1c0(P(ebx));
}
ABI_ENTRY_0042e1c0(fn_0042e1c0_Entry, fn_0042e1c0_Abi)
HOOK(0x0042e1c0, "fn_0042e1c0", fn_0042e1c0_Entry, nullptr);

// 0x0042f5c0 (eax = character; stack: a0; ret 4; callers 0x0042c820, 0x0042f870, 0x00430340, 0x0043ee90). Handles
// a trigger on the character (string "Unknown Trigger Mode\n%s\nTrigger %d\nIgnoring Trigger"): +0x48c &= ~0x4000,
// Object_setPhysicsEnabled(0). Type 0x10e: 0x0042a660 (edi = character), vt+0x19c(8, 1, 0, 0, 0, 0),
// vt+0x1b8(0x10f). Otherwise by mode flags +0x488: 0x10 -> +0x484 = (+0x484 & ~0x02000000 | 0x20) & ~0x10000, +0x488
// &= ~1 then &= ~0x8000, 0x0042d1b0 (eax = +0x55c, ecx = +0x558; stack: character, 0, 0, 0, 0); 0x20 -> 0x004592e0
// (eax = Game+0xa84 +0xe70, ecx = +0x534; stack: character) and for a hit +0x5e8 = +0x5e4 = 0, vt+0x23c(hit, 1),
// +0x488 |= 4; 0x100 -> 0x0042ce70 (eax = a0, ecx = character); a path +0x558 -> 0x0042d1b0 as above; 0x8000000 ->
// with a usable +0xadc (0x00454780, esi = object) vt+0x17c(0, 1); else with a usable +0xae0: release +0xae0,
// 0x0042d0f0 (eax = character; stack: 0, 1), and when +0xadc is set: +0x484 |= 0x80000000,
// Object_setPhysicsEnabled(1), +0x488 |= 0x80, +0x880 = +0xadc (reference count +0x28 moved: ++ then release of
// +0xadc); else vt+0x238(character+0x4cc, 0x65); no mode -> with +0x52c set and vt+0xe4() false, Debug_Printf
// 0x00407ff0 (Game+0x34, format 0x005b6628, name, +0x52c). Finally +0x52c = 0 unless vt+0xe4() is true. Not executed
// in recorded runs. 0x0042d0f0's ecx is only push scratch: passed as 0.
uint32_t fn_0042f5c0(void* ch, uint32_t a0)
{
    const uint32_t o = u(ch);
    Field<uint32_t>(ch, 0x48c) &= 0xffffbfffu;
    orig::fn_004577b0(o, 0);
    if (Field<uint32_t>(ch, 0x460) == 0x10e)
    {
        orig::fn_0042a660(o);
        VCall<void>(ch, 0x19c / 4, 8u, 1u, 0u, 0u, 0u, 0u);
        VCall<void>(ch, 0x1b8 / 4, 0x10fu);
    }
    else
    {
        const uint32_t mode = Field<uint32_t>(ch, 0x488);
        if (mode & 0x10)
        {
            const uint32_t f = Field<uint32_t>(ch, 0x484) & 0xfdffffffu;
            Field<uint32_t>(ch, 0x484) = f;
            Field<uint32_t>(ch, 0x484) = f | 0x20;
            const uint32_t m = Field<uint32_t>(ch, 0x488) & 0xfffffffeu;
            Field<uint32_t>(ch, 0x488) = m;
            Field<uint32_t>(ch, 0x488) = m & 0xffff7fffu;
            Field<uint32_t>(ch, 0x484) = Field<uint32_t>(ch, 0x484) & 0xfffeffffu;
            orig::fn_0042d1b0(Field<uint32_t>(ch, 0x55c), Field<uint32_t>(ch, 0x558), o, 0, 0, 0, 0);
        }
        else if (mode & 0x20)
        {
            const uint32_t hit = orig::fn_004592e0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe70),
                                                   Field<uint32_t>(ch, 0x534), o);
            if (hit)
            {
                Field<uint32_t>(ch, 0x5e8) = 0;
                Field<uint32_t>(ch, 0x5e4) = 0;
                VCall<void>(ch, 0x23c / 4, hit, 1u);
                Field<uint32_t>(ch, 0x488) |= 4;
            }
        }
        else if (mode & 0x100)
            orig::fn_0042ce70(a0, o);
        else if (Field<uint32_t>(ch, 0x558))
            orig::fn_0042d1b0(Field<uint32_t>(ch, 0x55c), Field<uint32_t>(ch, 0x558), o, 0, 0, 0, 0);
        else if (mode & 0x8000000)
        {
            const uint32_t adc = Field<uint32_t>(ch, 0xadc);
            if (adc && static_cast<uint8_t>(orig::fn_00454780(adc)))
                VCall<void>(ch, 0x17c / 4, 0u, 1u);
            else
            {
                const uint32_t ae0 = Field<uint32_t>(ch, 0xae0);
                if (ae0 && static_cast<uint8_t>(orig::fn_00454780(ae0)))
                {
                    ReleaseRef(&Field<uint32_t>(ch, 0xae0), 0);
                    orig::fn_0042d0f0(o, 0, 0, 1);
                    if (Field<uint32_t>(ch, 0xadc))
                    {
                        Field<uint32_t>(ch, 0x484) |= 0x80000000u;
                        orig::fn_004577b0(o, 1);
                        Field<uint32_t>(ch, 0x488) |= 0x80;
                        void* ref = Field<void*>(ch, 0xadc);
                        Field<void*>(ch, 0x880) = ref;
                        ++Field<int32_t>(ref, 0x28);
                        ReleaseRef(&Field<uint32_t>(ch, 0xadc), 0);
                    }
                }
                else
                    VCall<void>(ch, 0x238 / 4, o + 0x4cc, 0x65u);
            }
        }
        else if (Field<uint32_t>(ch, 0x52c) && !static_cast<uint8_t>(VCall<uint32_t>(ch, 0xe4 / 4)))
        {
            const uint32_t trig = Field<uint32_t>(ch, 0x52c);
            const uint32_t name = orig::fn_00453270(o);
            orig::fn_00407ff0(Field<uint32_t>(G(), 0x34), 0x005b6628, name, trig);
        }
    }
    const uint32_t r = VCall<uint32_t>(ch, 0xe4 / 4);
    if (!static_cast<uint8_t>(r))
        Field<uint32_t>(ch, 0x52c) = 0;
    return r;
}
static uint32_t fn_0042f5c0_Abi(uint32_t eax, uint32_t a0)
{
    return fn_0042f5c0(P(eax), a0);
}
ABI_ENTRY_0042f5c0(fn_0042f5c0_Entry, fn_0042f5c0_Abi)
HOOK(0x0042f5c0, "fn_0042f5c0", fn_0042f5c0_Entry, nullptr);

// Calls 0x0043e710 (eax, ebp; caller pops nothing). It takes EBP as an input (it forwards it to 0x00475a90), which
// the generated orig:: wrappers cannot load, so this shim sets EBP itself.
static uint32_t Call_0043e710(uint32_t eax_, uint32_t ebp_)
{
    static const uint32_t target = 0x0043e710;
    uint32_t r;
    __asm {
        push ebx
        push esi
        push edi
        mov eax, eax_
        mov ecx, ebp_
        push ebp
        mov ebp, ecx
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov r, eax
    }
    return r;
}

// Character_Die's repeated "drop" step: when the record exists without flag 0x8000 and vt+0x154() is false,
// vt+0x158() (guess: drop the held item).
static void CharacterDie_Drop(void* ch, void* rec)
{
    if (rec && !(Field<uint32_t>(rec, 0) & 0x8000) && !static_cast<uint8_t>(VCall<uint32_t>(ch, 0x154 / 4)))
        VCall<void>(ch, 0x158 / 4);
}

// 0x004319e0 (ecx = character; stack: damage record or 0; ret 4). Character_Die: slot 53 (+0xd4) of the character
// vtables (PenguinObject 0x005a1768, ICObject 0x005a1b60, GenericAIObject, GruntObject 0x005a9cf0, SheepObject, ...).
// vt+0x3c(), 0x00431720(character, record); the rider/attachment +0xab4 gets vt+0xd4(record) and is released;
// vt+0xf8(0x70001, 0); unless the record has flag 0x8000: 0x0043e710 (eax = ebp = character) (health pickup drop:
// strings pickup_health_1/2) and Game+0xb1c (kill count, guess) += 1. Type 0x12c stops here. When 0x00426630 (eax =
// character) is true: flag = 1, vt+0x1ac(1) and, with bit 1 of byte +0xa18, vt+0x19c(0, 1, 0, 0, 0, 0). Then
// 0x0046f440 (esi = Game+0xaa4, edi = character), release +0x880, and one of the death animations:
//  - turbo (+0x484 & 0x10000000) or type 0xf0: turbo reset as in 0x0042e1c0 (or vt+0xf8(0x170000, 0) without
//    turbo), 0x00454750 (Game+0xa84 +0xf1c), +0x490 = 15.0, vt+0x19c(0x5c, 1, 0, 0, 0, 0), vt+0x1b8(0x197);
//  - record flag 2: 0x00453b20 (esi) when 0x004539d0 (eax) is set; velocity = record +0x18..+0x20 * +0x24 *
//    [0x005bcedc], 0x00454c90 (eax = character, cl = 0; stack: 1), 0x00458440 (eax = 0, ecx = &velocity, edx =
//    character), +0x490 = 0.25, vt+0x18c(0), +0x490 = 8.0, vt+0x19c(0x49 + rand()*2 >> 15, 1, ...), +0x48c |= 0x80,
//    vt+0x1b8(0x195);
//  - record flag 0x200: +0x48c |= 0x800, record+0x24 = 20.0, vt+0x17c(record, 1);
//  - +0x1bc set and record flag 0x10000: +0x48c |= 0x10, record+0x24 *= [0x005bcb48], vt+0x17c(record, 1);
//  - type 0x12d: drop, +0x9f0 = +0x9f4 = 0.5, physics on, vt+0x19c(0x37, ...), 0x00454750 (Game+0xa84 +0xe9c),
//    vt+0xf8(0x60000, 0), vt+0x1b8(0x191);
//  - (+0x488 & 0x200080) with record flag 0x80: drop, physics off, vt+0x19c(0x3e, ...), vt+0x1b8(0x194);
//  - +0x1bc set and (flag, or record+0x14 >= +0xa0c): vt+0x17c(record, flag dword);
//  - +0x484 sign bit: drop, physics off, vt+0x19c(0x39 + rand()*4 >> 15, ...), vt+0x1b8(0x194);
//  - (+0x488 & 0x200080): drop, physics off, vt+0x19c(0x3d, ...), vt+0x1b8(0x194);
//  - otherwise drop, then 0x00427100 when vt+0x210() is true, else Object_Kill 0x00455640(character, 0).
// The original pushes its flag as a whole stack dword whose upper three bytes were never written (uninitialised);
// here they are 0. Returns the last call's eax.
uint32_t Character_Die(void* ch, void* rec)
{
    const uint32_t o = u(ch);
    VCall<void>(ch, 0x3c / 4);
    orig::fn_00431720(o, u(rec));
    if (void* rider = Field<void*>(ch, 0xab4))
    {
        VCall<void>(rider, 0xd4 / 4, rec);
        ReleaseRef(&Field<uint32_t>(ch, 0xab4), 0);
    }
    VCall<void>(ch, 0xf8 / 4, 0x70001u, 0u);
    if (!rec || !(Field<uint32_t>(rec, 0) & 0x8000))
    {
        Call_0043e710(o, o);
        ++Field<int32_t>(G(), 0xb1c);
    }
    if (Field<uint32_t>(ch, 0x460) == 0x12c)
        return 0;
    uint8_t flag = 0;
    if (static_cast<uint8_t>(orig::fn_00426630(o)))
    {
        const uint8_t b = (Field<uint8_t>(ch, 0xa18) >> 1) & 1;
        flag = 1;
        VCall<void>(ch, 0x1ac / 4, 1u);
        if (b)
            VCall<void>(ch, 0x19c / 4, 0u, 1u, 0u, 0u, 0u, 0u);
    }
    orig::fn_0046f440(Field<uint32_t>(G(), 0xaa4), o);
    ReleaseRef(&Field<uint32_t>(ch, 0x880), 0);
    const uint32_t f484 = Field<uint32_t>(ch, 0x484);
    const uint32_t type = Field<uint32_t>(ch, 0x460);
    if ((f484 & 0x10000000) || type == 0xf0)
    {
        if (f484 & 0x10000000)
            CharacterDeath_StopTurbo(ch);
        else
        {
            Field<uint32_t>(ch, 0xb20) = 0;
            VCall<void>(ch, 0xf8 / 4, 0x170000u, 0u);
            orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xf1c), o);
        }
        Field<uint32_t>(ch, 0x490) = 0x41700000;
        VCall<void>(ch, 0x19c / 4, 0x5cu, 1u, 0u, 0u, 0u, 0u);
        return VCall<uint32_t>(ch, 0x1b8 / 4, 0x197u);
    }
    if (rec)
    {
        const uint32_t rf = Field<uint32_t>(rec, 0);
        if (rf & 2)
        {
            if (orig::fn_004539d0(o))
                orig::fn_00453b20(o);
            const float k = GameGlobal<float>(0x005bcedc);
            float vel[3];
            vel[0] = F(rec, 0x18) * F(rec, 0x24) * k;
            vel[1] = F(rec, 0x1c) * F(rec, 0x24) * k;
            vel[2] = F(rec, 0x20) * F(rec, 0x24) * k;
            orig::fn_00454c90(o, 0, 1);
            orig::fn_00458440(0, u(vel), o);
            Field<uint32_t>(ch, 0x490) = 0x3e800000;
            VCall<void>(ch, 0x18c / 4, 0u);
            Field<uint32_t>(ch, 0x490) = 0x41000000;
            const int32_t r = static_cast<int32_t>(orig::fn_005391cd());
            VCall<void>(ch, 0x19c / 4, static_cast<uint32_t>(((r * 2) >> 15) + 0x49), 1u, 0u, 0u, 0u, 0u);
            Field<uint32_t>(ch, 0x48c) |= 0x80;
            return VCall<uint32_t>(ch, 0x1b8 / 4, 0x195u);
        }
        if (rf & 0x200)
        {
            Field<uint32_t>(ch, 0x48c) |= 0x800;
            Field<uint32_t>(rec, 0x24) = 0x41a00000;
            return VCall<uint32_t>(ch, 0x17c / 4, rec, 1u);
        }
    }
    const uint32_t a1bc = Field<uint32_t>(ch, 0x1bc);
    if (a1bc && rec && (Field<uint32_t>(rec, 0) & 0x10000))
    {
        Field<uint32_t>(ch, 0x48c) |= 0x10;
        F(rec, 0x24) = F(rec, 0x24) * GameGlobal<float>(0x005bcb48);
        return VCall<uint32_t>(ch, 0x17c / 4, rec, 1u);
    }
    if (type == 0x12d)
    {
        CharacterDie_Drop(ch, rec);
        Field<uint32_t>(ch, 0x9f0) = 0x3f000000;
        Field<uint32_t>(ch, 0x9f4) = 0x3f000000;
        orig::fn_004577b0(o, 1);
        VCall<void>(ch, 0x19c / 4, 0x37u, 0u, 0u, 0u, 0u, 0u);
        orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe9c), o);
        VCall<void>(ch, 0xf8 / 4, 0x60000u, 0u);
        return VCall<uint32_t>(ch, 0x1b8 / 4, 0x191u);
    }
    const uint32_t m488 = Field<uint32_t>(ch, 0x488) & 0x200080;
    uint32_t anim;
    if (m488 && rec && (Field<uint32_t>(rec, 0) & 0x80))
    {
        CharacterDie_Drop(ch, rec);
        orig::fn_004577b0(o, 0);
        anim = 0x3e;
    }
    else
    {
        if (a1bc && (flag || (rec && F(rec, 0x14) >= F(ch, 0xa0c))))
            return VCall<uint32_t>(ch, 0x17c / 4, rec, static_cast<uint32_t>(flag));
        if (f484 & 0x80000000u)
        {
            CharacterDie_Drop(ch, rec);
            orig::fn_004577b0(o, 0);
            const int32_t r = static_cast<int32_t>(orig::fn_005391cd());
            anim = static_cast<uint32_t>(((r * 4) >> 15) + 0x39);
        }
        else if (m488)
        {
            CharacterDie_Drop(ch, rec);
            orig::fn_004577b0(o, 0);
            anim = 0x3d;
        }
        else
        {
            CharacterDie_Drop(ch, rec);
            if (static_cast<uint8_t>(VCall<uint32_t>(ch, 0x210 / 4)))
                return fn_00427100(ch);
            return orig::fn_00455640(o, 0);
        }
    }
    VCall<void>(ch, 0x19c / 4, anim, 0u, 0u, 0u, 0u, 0u);
    return VCall<uint32_t>(ch, 0x1b8 / 4, 0x194u);
}
static uint32_t Character_Die_Abi(uint32_t ecx, uint32_t a0)
{
    return Character_Die(P(ecx), P(a0));
}
ABI_ENTRY_004319e0(Character_Die_Entry, Character_Die_Abi)
HOOK(0x004319e0, "Character_Die", Character_Die_Entry, nullptr);

// 0x00433fa0 (stack: 1; caller pops). `mov al, 1; ret`: returns true (upper eax bytes are the caller's; here 0).
// Shared by many vtables (CaptainRocket slot 87, PenguinObject slots 56/67/70, ...). Not executed in recorded runs.
uint32_t fn_00433fa0(uint32_t /*a0*/)
{
    return 1;
}
static uint32_t fn_00433fa0_Abi(uint32_t a0)
{
    return fn_00433fa0(a0);
}
ABI_ENTRY_00433fa0(fn_00433fa0_Entry, fn_00433fa0_Abi)
HOOK(0x00433fa0, "fn_00433fa0", fn_00433fa0_Entry, nullptr);

// 0x00434330 (ecx = fireball; stack: 4; ret 0x10). GoliathFireball (vtable 0x005af8c0) slot 6, the load slot: +0xb78
// = 0, Ai_InitGeneric 0x004247f0 (same arguments), floats (0x004534a0, esi = a3, edi = field; stack: object, name,
// a2, default, 0) gravity_scale +0xb70 (1.0), speed +0xb74 (80.0); list registrations 0x00454750 (esi = Game+0xa84
// +0xe8c, +0xe90, +0xe94; edi = object); impact_force +0xb6c (2010.0), area_damage +0xb5c, damage_radius_inner
// +0xb60, damage_radius_outer +0xb64; +0xb68 = 1 / (outer - inner) when outer > 0.01 and outer > inner, else 1.0;
// +0xb58 (state) = 1. Same tail as FxExplosion_Load. Not executed in recorded runs. Returns the last loader's eax.
uint32_t GoliathFireball_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t o = u(obj);
    Field<uint32_t>(obj, 0xb78) = 0;
    orig::fn_004247f0(o, a0, a1, a2, a3);
    orig::fn_004534a0(a3, o + 0xb70, o, 0x005b6380, a2, 0x3f800000, 0);
    orig::fn_004534a0(a3, o + 0xb74, o, 0x005b6378, a2, 0x42a00000, 0);
    orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe8c), o);
    orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe90), o);
    orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe94), o);
    orig::fn_004534a0(a3, o + 0xb6c, o, 0x005b93c8, a2, 0x44fb4000, 0);
    orig::fn_004534a0(a3, o + 0xb5c, o, 0x005b93bc, a2, 0, 0);
    orig::fn_004534a0(a3, o + 0xb60, o, 0x005b93a8, a2, 0, 0);
    const uint32_t r = orig::fn_004534a0(a3, o + 0xb64, o, 0x005b9394, a2, 0, 0);
    const float outer = F(obj, 0xb64);
    if (outer > GameGlobal<float>(0x005bcbec) && outer > F(obj, 0xb60))
    {
        Field<uint32_t>(obj, 0xb58) = 1;
        F(obj, 0xb68) = kOne / (outer - F(obj, 0xb60));
    }
    else
    {
        Field<uint32_t>(obj, 0xb68) = 0x3f800000;
        Field<uint32_t>(obj, 0xb58) = 1;
    }
    return r;
}
static uint32_t GoliathFireball_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return GoliathFireball_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00434330(GoliathFireball_Load_Entry, GoliathFireball_Load_Abi)
HOOK(0x00434330, "GoliathFireball_Load", GoliathFireball_Load_Entry, nullptr);

// 0x00434500 (stack: 1; ret 4). `mov al, 1; ret 4`: returns true (upper eax bytes are the caller's; here 0). Slot
// 40 / 72 of many object vtables (CameraPathObject, ForgeTurret, PlayerWeapon, ...).
uint32_t fn_00434500(uint32_t /*a0*/)
{
    return 1;
}
static uint32_t fn_00434500_Abi(uint32_t a0)
{
    return fn_00434500(a0);
}
ABI_ENTRY_00434500(fn_00434500_Entry, fn_00434500_Abi)
HOOK(0x00434500, "fn_00434500", fn_00434500_Entry, nullptr);

// 0x00434510 (ecx = fireball; stack: 5; ret 0x14). GoliathFireball (vtable 0x005af8c0) slot 42 (+0xa8), a collision
// callback (guess): unless 0x004539e0 (eax = fireball) is true, hitting nothing (a0 = 0) or anything not named
// "goliath_cart_spill" (0x005b6364; Object_GetName, _stricmp) sets state +0xb58 = 2. Returns false (al = 0; upper eax
// bytes not reproduced). _stricmp's ecx is a leftover it does not read: 0. Not executed in recorded runs.
uint32_t GoliathFireball_OnHit(void* obj, uint32_t other, uint32_t, uint32_t, uint32_t, uint32_t)
{
    if (static_cast<uint8_t>(orig::fn_004539e0(u(obj))))
        return 0;
    if (other)
    {
        const uint32_t name = orig::fn_00453270(other);
        if (orig::fn_005448ee(0, name, 0x005b6364) == 0)
            return 0;
    }
    Field<uint32_t>(obj, 0xb58) = 2;
    return 0;
}
static uint32_t GoliathFireball_OnHit_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return GoliathFireball_OnHit(P(ecx), a0, a1, a2, a3, a4);
}
ABI_ENTRY_00434510(GoliathFireball_OnHit_Entry, GoliathFireball_OnHit_Abi)
HOOK(0x00434510, "GoliathFireball_OnHit", GoliathFireball_OnHit_Entry, nullptr);

// 0x00434550 (ecx = fireball; stack: out; ret 4). GoliathFireball (vtable 0x005af8c0) slot 35 (+0x8c): *out =
// gravity_scale (+0xb70); returns it. Not executed in recorded runs.
uint32_t GoliathFireball_GetGravityScale(void* obj, uint32_t* out)
{
    const uint32_t v = Field<uint32_t>(obj, 0xb70);
    *out = v;
    return v;
}
static uint32_t GoliathFireball_GetGravityScale_Abi(uint32_t ecx, uint32_t a0)
{
    return GoliathFireball_GetGravityScale(P(ecx), reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_00434550(GoliathFireball_GetGravityScale_Entry, GoliathFireball_GetGravityScale_Abi)
HOOK(0x00434550, "GoliathFireball_GetGravityScale", GoliathFireball_GetGravityScale_Entry, nullptr);

// 0x00435250. `xor al, al; ret`: returns false (upper eax bytes are the caller's; here 0). Shared by many vtables
// (CameraPathObject slots 17/19/56/57/..., ...). Not executed in recorded runs.
uint32_t fn_00435250()
{
    return 0;
}
static uint32_t fn_00435250_Abi()
{
    return fn_00435250();
}
ABI_ENTRY_00435250(fn_00435250_Entry, fn_00435250_Abi)
HOOK(0x00435250, "fn_00435250", fn_00435250_Entry, nullptr);

// 0x00436720 (ecx = object; stack: flags, a1; ret 8). ShrubPatrol (vtable 0x005a7e98) slot 62 (+0xf8), the slot the
// death code calls as vt+0xf8(flags, 0). When 0x0044e600 is true and the type (+0x460) is not 0x190 and flags is
// neither 0x4000 nor 0x100: base 0x00459260 (ecx = object; stack: 0x30000, a1). Otherwise flags gets 0x400000 added
// unless 0x00454780 (esi = object) is true, flags is 0x4000 or 0x100, or the type is 0x19a; then 0x00459260(flags,
// a1). Not executed in recorded runs.
uint32_t fn_00436720(void* obj, uint32_t flags, uint32_t a1)
{
    if (static_cast<uint8_t>(fn_0044e600()) && Field<uint32_t>(obj, 0x460) != 0x190 && flags != 0x4000 &&
        flags != 0x100)
        return orig::fn_00459260(u(obj), 0x30000, a1);
    if (!static_cast<uint8_t>(orig::fn_00454780(u(obj))) && flags != 0x4000 && flags != 0x100 &&
        Field<uint32_t>(obj, 0x460) != 0x19a)
        flags |= 0x400000;
    return orig::fn_00459260(u(obj), flags, a1);
}
static uint32_t fn_00436720_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00436720(P(ecx), a0, a1);
}
ABI_ENTRY_00436720(fn_00436720_Entry, fn_00436720_Abi)
HOOK(0x00436720, "fn_00436720", fn_00436720_Entry, nullptr);

// 0x00436820 (ecx = object; stack: 4; ret 0x10). ICObject (vtable 0x005a1b60) slot 6, the load slot: base load
// 0x004517b0; 0x00458270 (edx = object; stack: 0, 0, (mass == 0 ? 0 : 1 / mass) * 20000.0 [0x005bcd34]) with mass =
// (+0x1c4 ->+8) +0x2c; "receive_trigger_activate" -> +0x52c (0x00453400, eax = 0); "trigger_los" (0x004534f0, al = 0)
// into the a3 stack slot, setting +0x488 |= 0x40; "generic_txt" -> +0x4cc (0x004533a0, eax = "none" 0x005bb9e0;
// stack: object, name, a2, a3, 0); "invisible" into the a3 slot, calling vt+0xf4(0) when set; +0x488 |= 0x8000000,
// vt+0x1b8(0x8c), +0x604 = Game+0xa84 +0xe88, +0x608 = Game+0xa84 +0xe7c, 0x004559c0 (eax = object, cl = 0), list
// registration 0x004546d0 (esi = Game+0xa84 +0xf1c, edi = object). Not executed in recorded runs.
uint32_t ICObject_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t o = u(obj);
    uint32_t slot3 = a3;  // the original reuses its a3 argument slot as a byte output
    orig::fn_004517b0(o, a0, a1, a2, a3);
    const float mass = F(Field<void*>(Field<void*>(obj, 0x1c4), 8), 0x2c);
    float v;
    if (mass == kZero)
        v = kZero;
    else
        v = kOne / mass;
    v = v * GameGlobal<float>(0x005bcd34);
    orig::fn_00458270(o, 0, 0, f2u(v));
    uint32_t r = orig::fn_00453400(0, a3, o + 0x52c, o, 0x005bba00, a2, 0);
    orig::fn_004534f0(r & 0xffffff00u, a3, u(&slot3), o, 0x005bb68c, a2, 0);
    if (static_cast<uint8_t>(slot3))
        Field<uint32_t>(obj, 0x488) |= 0x40;
    r = orig::fn_004533a0(0x005bb9e0, o + 0x4cc, o, 0x005b6dac, a2, a3, 0);
    orig::fn_004534f0(r & 0xffffff00u, a3, u(&slot3), o, 0x005bba58, a2, 0);
    if (static_cast<uint8_t>(slot3))
        VCall<void>(obj, 0xf4 / 4, 0u);
    Field<uint32_t>(obj, 0x488) |= 0x8000000;
    VCall<void>(obj, 0x1b8 / 4, 0x8cu);
    void* game = G();
    Field<uint32_t>(obj, 0x604) = Field<uint32_t>(Field<void*>(game, 0xa84), 0xe88);
    Field<uint32_t>(obj, 0x608) = Field<uint32_t>(Field<void*>(game, 0xa84), 0xe7c);
    orig::fn_004559c0(o, 0);
    return static_cast<uint32_t>(orig::fn_004546d0(Field<uint32_t>(Field<void*>(game, 0xa84), 0xf1c), o));
}
static uint32_t ICObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return ICObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00436820(ICObject_Load_Entry, ICObject_Load_Abi)
HOOK(0x00436820, "ICObject_Load", ICObject_Load_Entry, nullptr);

// 0x00436960 (ecx = object; stack: out; ret 4). ICObject (vtable 0x005a1b60) slot 7: 0x00457810 (eax = object), then
// *out = 0x100. Returns out. Not executed in recorded runs.
uint32_t ICObject_Slot7(void* obj, uint32_t* out)
{
    orig::fn_00457810(u(obj));
    *out = 0x100;
    return u(out);
}
static uint32_t ICObject_Slot7_Abi(uint32_t ecx, uint32_t a0)
{
    return ICObject_Slot7(P(ecx), reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_00436960(ICObject_Slot7_Entry, ICObject_Slot7_Abi)
HOOK(0x00436960, "fn_00436960", ICObject_Slot7_Entry, nullptr);

// 0x00436980 (ecx = object; stack: 2; ret 8). ICObject (vtable 0x005a1b60) slot 142 (+0x238): base 0x0042c410 (ecx =
// object; same arguments), then 0x0042c380 (esi = object), whose eax is returned. Not executed in recorded runs.
uint32_t fn_00436980(void* obj, uint32_t a0, uint32_t a1)
{
    orig::fn_0042c410(u(obj), a0, a1);
    return orig::fn_0042c380(u(obj));
}
static uint32_t fn_00436980_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00436980(P(ecx), a0, a1);
}
ABI_ENTRY_00436980(fn_00436980_Entry, fn_00436980_Abi)
HOOK(0x00436980, "fn_00436980", fn_00436980_Entry, nullptr);

// 0x00437290 (ebx = object; ecx = leftover, only pushed as scratch; callers 0x0045f020 ObjectSystem_CreateObject and
// the factory thunks 0x00461510...0x00461650). GenericAIObject constructor: base constructor 0x004511d0 (edx =
// object), vtable = 0x005b2958 (GenericAIObject), 0x004a0d00 (esi = object+0x61c; edx = object as the base
// constructor leaves it), 0x00406400 (eax = object+0xa18), then member defaults (listed below in the original's
// order; +0xb34 = rand() * [0x005bce30] + [0x005bcb7c]), 0x004274e0 (ecx = object; stack: 0), a 0x10-byte pool
// allocation PMS_heapAlloc 0x004e07f0 (ecx = 0x0063a210; stack: 0x10, 0x18, 2, 0x0058b5e8) initialised by 0x004e7010
// (eax = Game+0xac4, edx = 0, esi = block) into +0x518. Returns the object.
uint32_t GenericAIObject_Construct(uint32_t /*ecx*/, void* obj)
{
    const uint32_t o = u(obj);
    orig::fn_004511d0(o);
    Field<uint32_t>(obj, 0) = 0x005b2958;
    orig::fn_004a0d00(o, o + 0x61c);
    orig::fn_00406400(o + 0xa18);
    Field<uint32_t>(obj, 0x46c) = 0;
    Field<uint32_t>(obj, 0x468) = 0;
    Field<uint32_t>(obj, 0x464) = 0;
    Field<uint32_t>(obj, 0x600) = 0x41200000;
    Field<uint32_t>(obj, 0x5fc) = 0x41a00000;
    Field<uint32_t>(obj, 0x894) = 0;
    const int32_t rnd = static_cast<int32_t>(orig::fn_005391cd());
    Field<uint32_t>(obj, 0x484) = 0;
    Field<uint32_t>(obj, 0x488) = 0;
    const float t = static_cast<float>(rnd) * GameGlobal<float>(0x005bce30);
    Field<uint32_t>(obj, 0x48c) = 0;
    Field<uint32_t>(obj, 0x588) = 0;
    Field<uint32_t>(obj, 0x898) = 0;
    const float v = t + GameGlobal<float>(0x005bcb7c);
    static const uint16_t kZeroed[] = {0x88c, 0xadc, 0xae0, 0xb40, 0xb3c, 0xb38, 0xb50, 0xb4c, 0xb44, 0x854, 0x490,
                                       0x494, 0x4a8};
    for (uint16_t off : kZeroed)
        Field<uint32_t>(obj, off) = 0;
    Field<uint32_t>(obj, 0x4ac) = 0x65;
    F(obj, 0xb34) = v;
    std::memset(reinterpret_cast<uint8_t*>(obj) + 0x96c, 0, 10 * 4);
    std::memset(reinterpret_cast<uint8_t*>(obj) + 0x994, 0, 10 * 4);
    Field<uint32_t>(obj, 0x970) = 0xbf800000;
    Field<uint32_t>(obj, 0x998) = 0xbf800000;
    static const uint16_t kZeroed2[] = {0x574, 0xb54, 0x870, 0x864, 0x57c, 0x578, 0xb20, 0x538, 0x904, 0x918, 0x91c,
                                        0x920};
    for (uint16_t off : kZeroed2)
        Field<uint32_t>(obj, off) = 0;
    orig::fn_004274e0(o, 0);
    Field<uint32_t>(obj, 0x398) = 0xffffffff;
    Field<uint32_t>(obj, 0x890) = 0x3f800000;
    Field<uint32_t>(obj, 0x580) = 0x3f000000;
    Field<uint32_t>(obj, 0x584) = 0x3f000000;
    Field<uint32_t>(obj, 0x95c) = 0;
    Field<uint32_t>(obj, 0x958) = 0;
    Field<uint32_t>(obj, 0x570) = 0;
    Field<uint32_t>(obj, 0xb10) = 0xffffffff;
    Field<uint32_t>(obj, 0xb08) = 0;
    Field<uint32_t>(obj, 0x534) = 0;
    static const uint16_t kZeroed3[] = {0x52c, 0x530, 0x528, 0x558, 0x55c, 0x9bc, 0x878, 0x87c, 0x880, 0x510, 0x50c,
                                        0x514};
    for (uint16_t off : kZeroed3)
        Field<uint32_t>(obj, off) = 0;
    const uint32_t block = orig::fn_004e07f0(0x0063a210, 0x10, 0x18, 2, 0x0058b5e8);
    Field<uint32_t>(obj, 0x518) = block ? orig::fn_004e7010(Field<uint32_t>(G(), 0xac4), 0, block) : 0;
    Field<uint32_t>(obj, 0x498) = 2;
    Field<uint32_t>(obj, 0x4b0) = 2;
    Field<uint32_t>(obj, 0x4a0) = 0;
    Field<uint32_t>(obj, 0x4a4) = 1;
    Field<uint32_t>(obj, 0x4b8) = 0;
    Field<uint32_t>(obj, 0x4bc) = 1;
    Field<uint32_t>(obj, 0x49c) = 3;
    Field<uint32_t>(obj, 0x4b4) = 3;
    return o;
}
static uint32_t GenericAIObject_Construct_Abi(uint32_t ecx, uint32_t ebx)
{
    return GenericAIObject_Construct(ecx, P(ebx));
}
ABI_ENTRY_00437290(GenericAIObject_Construct_Entry, GenericAIObject_Construct_Abi)
HOOK(0x00437290, "GenericAIObject_Construct", GenericAIObject_Construct_Entry, nullptr);

// 0x0043a8c0 (stack: 1; ret 4). `xor al, al; ret 4`: returns false (upper eax bytes are the caller's; here 0). Shared
// by many vtables (CameraPathObject slot 66, ForgeTurret slots 66/93, ...). Not executed in recorded runs.
uint32_t fn_0043a8c0(uint32_t /*a0*/)
{
    return 0;
}
static uint32_t fn_0043a8c0_Abi(uint32_t a0)
{
    return fn_0043a8c0(a0);
}
ABI_ENTRY_0043a8c0(fn_0043a8c0_Entry, fn_0043a8c0_Abi)
HOOK(0x0043a8c0, "fn_0043a8c0", fn_0043a8c0_Entry, nullptr);

// 0x0043aa00 (ecx = object; stack: 1; ret 4). SquadObject (vtable 0x005ab3e8) slot 105 (+0x1a4): returns &object+4.
// Not executed in recorded runs.
uint32_t fn_0043aa00(void* obj, uint32_t /*a0*/)
{
    return u(obj) + 4;
}
static uint32_t fn_0043aa00_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043aa00(P(ecx), a0);
}
ABI_ENTRY_0043aa00(fn_0043aa00_Entry, fn_0043aa00_Abi)
HOOK(0x0043aa00, "fn_0043aa00", fn_0043aa00_Entry, nullptr);

// 0x0043ab30 (stack: 1; ret 4). `xor eax, eax; ret 4`: returns 0. Shared by many vtables (PenguinObject slot 106,
// ICObject slot 106, ...). Not executed in recorded runs.
uint32_t fn_0043ab30(uint32_t /*a0*/)
{
    return 0;
}
static uint32_t fn_0043ab30_Abi(uint32_t a0)
{
    return fn_0043ab30(a0);
}
ABI_ENTRY_0043ab30(fn_0043ab30_Entry, fn_0043ab30_Abi)
HOOK(0x0043ab30, "fn_0043ab30", fn_0043ab30_Entry, nullptr);

// 0x004473a0 (ecx = fireball). GoliathFireball (vtable 0x005af8c0) slot 47 (+0xbc), the update: in state 2 (+0xb58,
// set by GoliathFireball_OnHit) it explodes: position = vt+0x54(&pos), 0x0047f020 (ebx = &area_damage +0xb5c; stack:
// 0, &pos, fireball, impact_force +0xb6c, +0xb78, 0, 0) (area damage, guess), then vt+0xdc(). Returns eax as left
// (state 2: vt+0xdc's; otherwise the state compare: not reproduced). Not executed in recorded runs.
uint32_t GoliathFireball_Update(void* obj)
{
    if (Field<uint32_t>(obj, 0xb58) != 2)
        return 0;
    float pos[3];
    VCall<void>(obj, 0x54 / 4, pos);
    orig::fn_0047f020(u(obj) + 0xb5c, 0, u(pos), u(obj), Field<uint32_t>(obj, 0xb6c), Field<uint32_t>(obj, 0xb78), 0,
                      0);
    return VCall<uint32_t>(obj, 0xdc / 4);
}
static uint32_t GoliathFireball_Update_Abi(uint32_t ecx)
{
    return GoliathFireball_Update(P(ecx));
}
ABI_ENTRY_004473a0(GoliathFireball_Update_Entry, GoliathFireball_Update_Abi)
HOOK(0x004473a0, "GoliathFireball_Update", GoliathFireball_Update_Entry, nullptr);

// 0x0044a4b0 (eax = value, ecx = object; stack: flag byte; ret 4; caller 0x00403690). +8 = value, +0xc = 1, byte +0x10
// = 1; 0x0045d670(Game+0xa84, value); 0x004629b0(Game+0xa70, +8); without the flag 0x0045d930 (ecx = Game+0xa84;
// stack: Game+0xa84, +8); 0x0045d9b0 (eax = Game+0xa84); byte +0x10 = 0; then every object of the list Game+0xa84
// +0xe70 gets vt+0x24() (iterator 0x004e7340 / 0x004e73c0), and the iterator's node loses a reference (+0x18).
// Returns the node (or 0) as the original leaves it in eax.
uint32_t fn_0044a4b0(uint32_t value, void* obj, uint32_t flag)
{
    Field<uint32_t>(obj, 8) = value;
    Field<uint32_t>(obj, 0xc) = 1;
    Field<uint8_t>(obj, 0x10) = 1;
    orig::fn_0045d670(Field<uint32_t>(G(), 0xa84), value);
    orig::fn_004629b0(Field<uint32_t>(G(), 0xa70), Field<uint32_t>(obj, 8));
    if (!static_cast<uint8_t>(flag))
    {
        const uint32_t lists = Field<uint32_t>(G(), 0xa84);
        orig::fn_0045d930(lists, lists, Field<uint32_t>(obj, 8));
    }
    orig::fn_0045d9b0(Field<uint32_t>(G(), 0xa84));
    Field<uint8_t>(obj, 0x10) = 0;
    uint32_t iter[2] = {0, 0};
    void* list = Field<void*>(Field<void*>(G(), 0xa84), 0xe70);
    for (uint32_t o = orig::fn_004e7340(u(list), u(iter)); o; o = orig::fn_004e73c0(u(iter)))
        VCall<void>(P(o), 0x24 / 4);
    if (iter[1])
        --Field<int32_t>(P(iter[1]), 0x18);
    return iter[1];
}
static uint32_t fn_0044a4b0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_0044a4b0(eax, P(ecx), a0);
}
ABI_ENTRY_0044a4b0(fn_0044a4b0_Entry, fn_0044a4b0_Abi)
HOOK(0x0044a4b0, "fn_0044a4b0", fn_0044a4b0_Entry, nullptr);

// 0x0044a580 (eax = object, ecx = value). object+0xc = value (the dword 0x0044a590 tests). Not executed in recorded
// runs.
uint32_t fn_0044a580(void* obj, uint32_t value)
{
    Field<uint32_t>(obj, 0xc) = value;
    return u(obj);
}
static uint32_t fn_0044a580_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0044a580(P(eax), ecx);
}
ABI_ENTRY_0044a580(fn_0044a580_Entry, fn_0044a580_Abi)
HOOK(0x0044a580, "fn_0044a580", fn_0044a580_Entry, nullptr);

// 0x0044a590 (ecx = object). Returns object+0xc > 1 (signed).
uint32_t fn_0044a590(void* obj)
{
    return Field<int32_t>(obj, 0xc) > 1 ? 1u : 0u;
}
static uint32_t fn_0044a590_Abi(uint32_t ecx)
{
    return fn_0044a590(P(ecx));
}
ABI_ENTRY_0044a590(fn_0044a590_Entry, fn_0044a590_Abi)
HOOK(0x0044a590, "fn_0044a590", fn_0044a590_Entry, nullptr);

// 0x0044a5a0 (stack: 1 (unused); ret 4; caller 0x00403590). Returns whether the string from game vt+0x38() is set and
// not empty (al; upper eax bytes from the pointer).
uint32_t fn_0044a5a0(uint32_t /*a0*/)
{
    const uint32_t p = VCall<uint32_t>(G(), 0x38 / 4);
    if (p && *reinterpret_cast<const char*>(p))
        return (p & 0xffffff00u) | 1;
    return p & 0xffffff00u;
}
static uint32_t fn_0044a5a0_Abi(uint32_t a0)
{
    return fn_0044a5a0(a0);
}
ABI_ENTRY_0044a5a0(fn_0044a5a0_Entry, fn_0044a5a0_Abi)
HOOK(0x0044a5a0, "fn_0044a5a0", fn_0044a5a0_Entry, nullptr);

// 0x0044a5c0 (stack: a0 (unused), dst; ret 8; caller 0x00403590). Copies the string from game vt+0x38() to dst when it
// is set and not empty. Returns eax as the original leaves it (the pointer, or one past its terminator after the
// copy). Not executed in recorded runs.
uint32_t fn_0044a5c0(uint32_t /*a0*/, char* dst)
{
    const char* p = reinterpret_cast<const char*>(VCall<uint32_t>(G(), 0x38 / 4));
    if (!p || !*p)
        return u(p);
    const char* s = p;
    char c;
    do
    {
        c = *s;
        dst[s - p] = c;
        ++s;
    } while (c);
    return u(s);
}
static uint32_t fn_0044a5c0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_0044a5c0(a0, reinterpret_cast<char*>(a1));
}
ABI_ENTRY_0044a5c0(fn_0044a5c0_Entry, fn_0044a5c0_Abi)
HOOK(0x0044a5c0, "fn_0044a5c0", fn_0044a5c0_Entry, nullptr);

// 0x0044a5f0 (eax = object). Returns the byte +0x11 in al (upper eax bytes are the object pointer's). Not executed in
// recorded runs.
uint32_t fn_0044a5f0(void* obj)
{
    return (u(obj) & 0xffffff00u) | Field<uint8_t>(obj, 0x11);
}
static uint32_t fn_0044a5f0_Abi(uint32_t eax)
{
    return fn_0044a5f0(P(eax));
}
ABI_ENTRY_0044a5f0(fn_0044a5f0_Entry, fn_0044a5f0_Abi)
HOOK(0x0044a5f0, "fn_0044a5f0", fn_0044a5f0_Entry, nullptr);

// 0x0044a600 (eax = object). Returns true when +4 is set, else the byte +0x10 (al; upper eax bytes are the object
// pointer's).
uint32_t fn_0044a600(void* obj)
{
    if (Field<uint32_t>(obj, 4))
        return (u(obj) & 0xffffff00u) | 1;
    return (u(obj) & 0xffffff00u) | Field<uint8_t>(obj, 0x10);
}
static uint32_t fn_0044a600_Abi(uint32_t eax)
{
    return fn_0044a600(P(eax));
}
ABI_ENTRY_0044a600(fn_0044a600_Entry, fn_0044a600_Abi)
HOOK(0x0044a600, "fn_0044a600", fn_0044a600_Entry, nullptr);

// 0x0044a660 (eax = object, cl = selector). Returns object+0x5c when cl is non-zero, else object+0x15c.
uint32_t fn_0044a660(uint32_t obj, uint32_t sel)
{
    return static_cast<uint8_t>(sel) ? obj + 0x5c : obj + 0x15c;
}
static uint32_t fn_0044a660_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0044a660(eax, ecx);
}
ABI_ENTRY_0044a660(fn_0044a660_Entry, fn_0044a660_Abi)
HOOK(0x0044a660, "fn_0044a660", fn_0044a660_Entry, nullptr);

// 0x0044c170 (ecx = mission object; stack: 4; ret 0x10). MissionObject (vtable 0x005a49c8) slot 6, the load slot
// (classifier: MissionObject_LoadProps): reads the level's mission settings, mostly into the Game object. Base load
// 0x004517b0; Game byte +0xdd2 = 0, object bytes +0x5a5 = +0x5a4 = 0, Game +0xafc (8 dwords) and +0xb1c (0x2d dwords)
// zeroed, Game byte +0xcbe = 0. Then the property loaders, in the original order (0x00453400 int: eax = default;
// 0x00453450 trigger: ebx = name; 0x004534a0 float; 0x004534f0 byte: al = default; 0x004533a0 text: eax = default
// text; all with esi = a3, edi = destination, stack: object, name, a2, [default,] 0): env_type, start_in_turret_tag,
// alarm_generator_max_troops (10), alarm triggers, alarm_min_delay (1.0) / alarm_max_delay (2.0) (when min > max:
// Debug_Printf "alarm_min_delay > alarm_max_delay\n%s" and min = max), object triggers, start_text, timeout_text
// ("ui_timeout"), ...; Game+0x2664 = &object+0x4c4; three failure slots i = 2..4 ("receive_trigger_failure_%d" ->
// object+0x478.., "failure_text_%d" -> object+0x4e4 + 0x20 * (i - 2), pointer stored at Game+0x2668..+0x2670, names
// built with _sprintf 0x0053814f into a 0x20-byte buffer); ai_target_tag; player_has_tickerbomb / pause_at_end (into
// the a3 stack slot, then Game bytes +0xd90/+0xd91 = value != 0); many Game flags and ints; timers. T = [0x00604a98]
// (current time, guess) is copied to Game +0xb0c, +0xafc, +0xb84, +0xb34, +0xb38; each min/max delay pair gets a
// next time min + rand() * (max - min) * [0x005bcbc0] (1/32768) with its start time = T (peasant and following
// pairs: start = T - value). Text_Lookup 0x00495f00 (ebx = Game+0xac0) of object +0x484/+0x4a4/+0x544 into Game
// +0x265c/+0x2660/+0x2674; Game+0x267c/+0x2680 = object+0x564/+0x584 when not empty, else 0; 0x004033c0 (eax =
// game, ecx = 0); next_level_name -> Game+0x268c; shadow_color_red/green/blue/blend (0, 0.125, 0.25, 0.3) read into
// the four argument stack slots and packed as Game+0x2704 = blend:red:green:blue bytes (each _ftol2(x * 255.0)).
// Last a list registration 0x004546d0 (esi = Game+0xa84 +0xe74, edi = object) and vt+0xf4(0), whose eax is
// returned. The original reuses its argument slots as scratch: kept here as locals. Not executed in recorded runs.
uint32_t MissionObject_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t o = u(obj);
    uint32_t r = orig::fn_004517b0(o, a0, a1, a2, a3);
    auto game = []() { return u(G()); };
    auto Int = [&](uint32_t def, uint32_t dst, uint32_t name) { r = orig::fn_00453400(def, a3, dst, o, name, a2, 0); };
    auto Trig = [&](uint32_t name, uint32_t dst) { r = orig::fn_00453450(0, name, a3, dst, o, a2, 0); };
    auto Flt = [&](uint32_t dst, uint32_t name, uint32_t def) { r = orig::fn_004534a0(a3, dst, o, name, a2, def, 0); };
    auto Byte = [&](uint32_t def, uint32_t dst, uint32_t name) {
        r = orig::fn_004534f0((r & 0xffffff00u) | def, a3, dst, o, name, a2, 0);
    };
    auto Text = [&](uint32_t def, uint32_t dst, uint32_t name) { r = orig::fn_004533a0(def, dst, o, name, a2, a3, 0); };
    auto Gf = [&](uint32_t off) -> float& { return F(G(), off); };
    auto Gu = [&](uint32_t off) -> uint32_t& { return Field<uint32_t>(G(), off); };
    const float& kTime = GameGlobal<float>(0x00604a98);
    const float& kRandScale = GameGlobal<float>(0x005bcbc0);
    // min + rand() * (max - min) * (1 / 32768); min and max are read before the rand() call.
    auto RandBetween = [&](uint32_t loOff, uint32_t hiOff) {
        const float hi = Gf(hiOff);
        const float lo = Gf(loOff);
        const int32_t rnd = static_cast<int32_t>(orig::fn_005391cd());
        const float span = hi - lo;
        const float t = static_cast<float>(rnd) * span;
        const float s = t * kRandScale;
        return s + lo;
    };

    Field<uint8_t>(G(), 0xdd2) = 0;
    Field<uint8_t>(obj, 0x5a5) = 0;
    Field<uint8_t>(obj, 0x5a4) = 0;
    std::memset(reinterpret_cast<uint8_t*>(G()) + 0xafc, 0, 8 * 4);
    std::memset(reinterpret_cast<uint8_t*>(G()) + 0xb1c, 0, 0x2d * 4);
    Field<uint8_t>(G(), 0xcbe) = 0;
    Int(0, game() + 0x2708, 0x005b228c);  // env_type
    Trig(0x005b2278, game() + 0x26cc);    // start_in_turret_tag
    Int(0xa, game() + 0x26d4, 0x005b225c);  // alarm_generator_max_troops
    Trig(0x005b2244, game() + 0x26d8);
    Trig(0x005b2228, game() + 0x26dc);
    Flt(game() + 0xb3c, 0x005b2218, 0x3f800000);  // alarm_min_delay
    Flt(game() + 0xb40, 0x005b2208, 0x40000000);  // alarm_max_delay
    if (Gf(0xb3c) > Gf(0xb40))
    {
        const uint32_t name = orig::fn_00453270(o);
        orig::fn_00407ff0(Field<uint32_t>(G(), 0x34), 0x005b21e0, name, 0);
        Gu(0xb3c) = Gu(0xb40);
    }
    Trig(0x005b21cc, o + 0x460);
    Trig(0x005b21b4, o + 0x46c);
    Trig(0x005b2198, o + 0x470);
    Text(0x0058a9a4, o + 0x484, 0x005b218c);  // start_text
    Text(0x0058a9a4, o + 0x4a4, 0x005b217c);
    Text(0x005b2170, o + 0x544, 0x005b2160);  // timeout_text ("ui_timeout")
    Text(0x0058a9a4, o + 0x564, 0x005b2154);
    Text(0x0058a9a4, o + 0x584, 0x005b2144);
    Trig(0x005b212c, o + 0x474);
    Text(0x0058a9a4, o + 0x4c4, 0x005b211c);
    Gu(0x2664) = o + 0x4c4;
    {
        char buf[0x20];
        uint32_t i = 2;
        uint32_t p = o + 0x4e4;
        for (uint32_t off = 0x2668; off <= 0x2670; off += 4, ++i, p += 0x20)
        {
            GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
                buf, reinterpret_cast<const char*>(0x005b2100), i);
            r = orig::fn_00453450(0, u(buf), a3, o + off - 0x21f0, o, a2, 0);
            GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
                buf, reinterpret_cast<const char*>(0x005b20f0), i);
            r = orig::fn_004533a0(0x0058a9a4, p, o, u(buf), a2, a3, 0);
            Gu(off) = p;
        }
    }
    Int(0, game() + 0xae8, 0x005b20e0);  // ai_target_tag
    Int(0, game() + 0x26fc, 0x005b20c8);
    Text(0x0058a9a4, o + 0x5ac, 0x005b20ac);
    uint32_t slot = a3;  // the original's a3 argument slot, reused as an output
    Int(0, u(&slot), 0x005b2094);  // player_has_tickerbomb
    Field<uint8_t>(G(), 0xd90) = slot != 0 ? 1 : 0;
    Int(1, u(&slot), 0x005b2084);  // pause_at_end
    Field<uint8_t>(G(), 0xd91) = slot != 0 ? 1 : 0;
    Trig(0x005b206c, game() + 0x2658);
    Byte(0, game() + 0xd92, 0x005b2058);
    Byte(1, game() + 0xd93, 0x005b2040);
    Byte(0, game() + 0xd94, 0x005b2028);
    Byte(0, game() + 0xd95, 0x005b2014);
    Byte(0, game() + 0xd97, 0x005b2000);
    Byte(0, game() + 0xd98, 0x005b1fec);
    Byte(0, game() + 0xd96, 0x005b1fd8);
    Byte(0, game() + 0xdd0, 0x005b1fc0);
    Byte(0, game() + 0xdaf, 0x005b1fa4);
    Byte(0, game() + 0xdb0, 0x005b1f8c);
    Byte(0, game() + 0xdb1, 0x005b1f78);
    Byte(0, game() + 0xdb3, 0x005b1f60);
    Byte(0, game() + 0xdb4, 0x005b1f48);
    Byte(0, game() + 0xdb2, 0x005b1f30);
    Int(0xfffffffeu, game() + 0x25e8, 0x005b1f14);
    Int(0xfffffffeu, game() + 0x25ec, 0x005b1ef8);
    Int(0xfffffffeu, game() + 0x25f0, 0x005b1ee0);
    Int(0xfffffffeu, game() + 0x25f8, 0x005b1ec8);
    Int(0xfffffffeu, game() + 0x25fc, 0x005b1eac);
    Int(0xfffffffeu, game() + 0x25f4, 0x005b1e90);
    Int(0xffffffffu, game() + 0x2610, 0x005b1e74);
    Int(0xffffffffu, game() + 0x2600, 0x005b1e58);
    Int(0xffffffffu, game() + 0x2604, 0x005b1e3c);
    Int(0xffffffffu, game() + 0x260c, 0x005b1e20);
    Int(0xffffffffu, game() + 0x2608, 0x005b1e04);
    Int(0, game() + 0xc50, 0x005b1df8);
    Int(0, game() + 0xc54, 0x005b1de8);
    Trig(0x005b1dcc, game() + 0xc3c);
    Trig(0x005b1db0, game() + 0xc40);
    Trig(0x005b1d98, game() + 0xc44);
    Int(5, game() + 0xc4c, 0x005b1d84);
    Trig(0x005b1d70, o + 0x5a8);
    Flt(game() + 0xaec, 0x005b1d58, 0x3f800000);
    Flt(game() + 0xaf0, 0x005b1d40, 0x41a00000);
    Flt(game() + 0xbd0, 0x005b1d30, 0);
    Flt(game() + 0xbd8, 0x005b1d20, 0);
    Flt(game() + 0xbd4, 0x005b1d10, 0);
    Flt(game() + 0xbdc, 0x005b1d00, 0);
    Flt(game() + 0xbe0, 0x005b1cf0, 0);
    Flt(game() + 0xbe8, 0x005b1ce0, 0);
    Flt(game() + 0xbe4, 0x005b1cd0, 0);
    Flt(game() + 0xbec, 0x005b1cc0, 0);
    Flt(game() + 0xb30, 0x005b1cac, 0x41200000);
    Int(0, game() + 0x26e0, 0x005b1c94);
    Int(0, game() + 0x26ec, 0x005b1c80);
    Trig(0x005b1c68, game() + 0x26f0);
    Trig(0x005b1c50, game() + 0x26f8);
    Flt(o + 0x468, 0x005bb864, 0);  // score_value
    Gf(0xb0c) = kTime;
    Gu(0xafc) = GameGlobal<uint32_t>(0x00604a98);
    Gu(0xb84) = GameGlobal<uint32_t>(0x00604a98);
    Gu(0xb34) = GameGlobal<uint32_t>(0x00604a98);
    Gu(0xb38) = GameGlobal<uint32_t>(0x00604a98);
    {
        const float v = RandBetween(0xaec, 0xaf0);
        Gf(0xaf4) = v;
        Gu(0xaf8) = GameGlobal<uint32_t>(0x00604a98);
    }
    Flt(game() + 0xb48, 0x005b1c40, 0x41f00000);  // vo_delay_min
    Flt(game() + 0xb4c, 0x005b1c30, 0x42340000);
    {
        const float v = RandBetween(0xb48, 0xb4c);
        Gf(0xb50) = v;
        Gu(0xb44) = GameGlobal<uint32_t>(0x00604a98);
    }
    Flt(game() + 0xb58, 0x005b1c1c, 0x43700000);
    Flt(game() + 0xb5c, 0x005b1c08, 0x43960000);
    {
        const float v = RandBetween(0xb58, 0xb5c);
        Gf(0xb60) = v;
        Gu(0xb54) = GameGlobal<uint32_t>(0x00604a98);
    }
    Flt(game() + 0xb98, 0x005b1bf4, 0x43700000);
    Flt(game() + 0xb9c, 0x005b1be0, 0x43960000);
    {
        const float v = RandBetween(0xb98, 0xb9c);
        Gf(0xba0) = v;
        Gf(0xb94) = kTime - v;
    }
    Flt(game() + 0xba8, 0x005b1bcc, 0x41200000);  // peasant_delay_min
    Flt(game() + 0xbac, 0x005b1bb8, 0x41200000);
    {
        const float v = RandBetween(0xba8, 0xbac);
        Gf(0xbb0) = v;
        Gf(0xba4) = kTime - v;
    }
    Flt(game() + 0xbb4, 0x005b1ba0, 0x3f000000);
    Flt(game() + 0xbbc, 0x005b1b8c, 0x40400000);
    Flt(game() + 0xbc0, 0x005b1b74, 0x41f00000);
    Flt(game() + 0xbc4, 0x005b1b5c, 0x42340000);
    Gu(0xbb8) = 0;
    Gu(0xbc8) = 0;
    Gu(0xba4) = GameGlobal<uint32_t>(0x00604a98);
    Flt(game() + 0xb68, 0x005b1b4c, 0x41f00000);
    Flt(game() + 0xb6c, 0x005b1b3c, 0x42340000);
    {
        const float v = RandBetween(0xb68, 0xb6c);
        Gf(0xb70) = v;
        Gu(0xb64) = GameGlobal<uint32_t>(0x00604a98);
    }
    Flt(game() + 0xb78, 0x005b1b2c, 0x42700000);
    Flt(game() + 0xb7c, 0x005b1b1c, 0x42960000);
    {
        const float v = RandBetween(0xb78, 0xb7c);
        Gf(0xb80) = v;
        Gu(0xb74) = GameGlobal<uint32_t>(0x00604a98);
    }
    Flt(game() + 0xb88, 0x005b1b0c, 0x43960000);
    Flt(game() + 0xb8c, 0x005b1afc, 0x43b40000);
    {
        const float v = RandBetween(0xb88, 0xb8c);
        Gf(0xb90) = v;
        Gu(0xb84) = GameGlobal<uint32_t>(0x00604a98);
    }
    Gu(0x265c) = orig::fn_00495f00(o + 0x484, Gu(0xac0), 0);
    Gu(0x2660) = orig::fn_00495f00(o + 0x4a4, Gu(0xac0), 0);
    Gu(0x2674) = orig::fn_00495f00(o + 0x544, Gu(0xac0), 0);
    Gu(0x267c) = Field<uint8_t>(obj, 0x564) ? o + 0x564 : 0;
    Gu(0x2680) = Field<uint8_t>(obj, 0x584) ? o + 0x584 : 0;
    orig::fn_004033c0(game(), 0);
    Text(0x0058a9a4, game() + 0x268c, 0x005b1aec);  // next_level_name
    float red, green, blue, blend;  // the original's a3, a1, a0 and a2 argument slots
    Flt(u(&red), 0x005b1ad8, 0);
    Flt(u(&green), 0x005b1ac4, 0x3e000000);
    Flt(u(&blue), 0x005b1ab0, 0x3e800000);
    Flt(u(&blend), 0x005b1a9c, 0x3e99999a);
    const float k255 = GameGlobal<float>(0x005bcb34);
    uint32_t color = static_cast<uint32_t>(static_cast<int32_t>(blend * k255));
    color = (color << 8) | static_cast<uint32_t>(static_cast<int32_t>(red * k255));
    color = (color << 8) | static_cast<uint32_t>(static_cast<int32_t>(green * k255));
    color = (color << 8) | static_cast<uint32_t>(static_cast<int32_t>(blue * k255));
    Gu(0x2704) = color;
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe74), o);
    return VCall<uint32_t>(obj, 0xf4 / 4, 0u);
}
static uint32_t MissionObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return MissionObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0044c170(MissionObject_Load_Entry, MissionObject_Load_Abi)
HOOK(0x0044c170, "MissionObject_Load", MissionObject_Load_Entry, nullptr);

// 0x0044d150 (ecx = mission object). MissionObject (vtable 0x005a49c8) slot 8 (+0x20), the level start (guess): with
// start_in_turret_tag (Game+0x26cc) the turret 0x004592e0 (eax = Game+0xa84 +0xea8, ecx = tag; stack: object) gets
// vt+0x178(); with a start camera path name (object+0x5ac, not empty) Camera_StartPath(name; Game+0xa68, 2, 0).
// Returns eax as left (Camera_StartPath's, else &object+0x5ac). Not executed in recorded runs.
uint32_t fn_0044d150(void* obj)
{
    const uint32_t tag = Field<uint32_t>(G(), 0x26cc);
    if (tag)
    {
        const uint32_t t = orig::fn_004592e0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xea8), tag, u(obj));
        if (t)
            VCall<void>(P(t), 0x178 / 4);
    }
    const char* name = reinterpret_cast<const char*>(obj) + 0x5ac;
    if (!*name)
        return u(name);
    return Camera_StartPath(name, Field<void*>(G(), 0xa68), 2, 0);
}
static uint32_t fn_0044d150_Abi(uint32_t ecx)
{
    return fn_0044d150(P(ecx));
}
ABI_ENTRY_0044d150(fn_0044d150_Entry, fn_0044d150_Abi)
HOOK(0x0044d150, "fn_0044d150", fn_0044d150_Entry, nullptr);

// 0x0044d1b0 (ecx = mission object). MissionObject (vtable 0x005a49c8) slot 9 (+0x24): +0x464 = [0x00604a98] (time),
// bytes +0x5a5 = +0x5a4 = 0, Game +0x26e4 = +0x26e8 = +0x26f4 = 0. Returns g_game (eax as left). Not executed in
// recorded runs.
uint32_t fn_0044d1b0(void* obj)
{
    Field<uint32_t>(obj, 0x464) = GameGlobal<uint32_t>(0x00604a98);
    void* game = G();
    Field<uint8_t>(obj, 0x5a5) = 0;
    Field<uint8_t>(obj, 0x5a4) = 0;
    Field<uint32_t>(game, 0x26e4) = 0;
    Field<uint32_t>(game, 0x26e8) = 0;
    Field<uint32_t>(game, 0x26f4) = 0;
    return u(game);
}
static uint32_t fn_0044d1b0_Abi(uint32_t ecx)
{
    return fn_0044d1b0(P(ecx));
}
ABI_ENTRY_0044d1b0(fn_0044d1b0_Entry, fn_0044d1b0_Abi)
HOOK(0x0044d1b0, "fn_0044d1b0", fn_0044d1b0_Entry, nullptr);

// 0x0044d600 (ecx = mission object; stack: sender, trigger id; ret 8). MissionObject (vtable 0x005a49c8) slot 14
// (+0x38), the trigger receiver:
//  - +0x470 (success, guess): when 0x0044e600 is true, the retreat direction (0x00453150, ecx = marker 0x0044c030(ebx
//    = 0; stack: -1), esi = &dir; no marker: Debug_Printf "Pre-Success Marker not found..." and (0, 1, 0)), Game
//    byte +0xdd2 = 1, +0xcbe = 0, +0xbf0 = +0xbf8 = 0, and every object of list Game+0xa84 +0xe7c whose vt+0x10c() is
//    true gets vt+0x234(&dir); otherwise just Game byte +0xdd2 = 1;
//  - +0x46c: on levels "lev_1_m1_b" / "lev_2_m1" (level name 0x0044a660(Game+0xa64, cl = 0)) Game+0x272c += 1000.0;
//    unless (0x0044e600 and (0x0049bcb0(Game+0xacc) or Game byte +0xdcd)), Game+0x2730 += score_value (+0x468) and
//    0x0044a580(Game+0xa64, 2);
//  - failure triggers +0x474/+0x478/+0x47c/+0x480 while Game byte +0xdd2 is 0: 0x0049ad60 (esi = Game+0xacc) when
//    Game+0xac8 is 1, else 0x0044a580(Game+0xa64, 4..7);
//  - Game+0x26f0 with Game+0x26f4 non-zero: the sender (reference +0x28 held) hands out objects while its vt+0xd8()
//    is true: 0x00406330(), 0x004063a0 (ecx = it), it vt+0xd4(0), position = sender vt+0x54(&pos), pos.z += 5.0,
//    0x0044d1f0 (edi = &pos; stack: mission, it), sender vt+0xd4(0); then the reference is dropped (0x00487da0 at 0
//    unless vt+0xd8() is true) and with Game+0x26f8 set and Game+0x26e8 >= +0x26ec (unsigned): Object_SendTrigger
//    0x00461870 (eax = Game+0xa84, ebx = +0x26f8; stack: 0), +0x26f8 = 0;
//  - Game+0xc3c / +0xc40 (while +0xdd2 is 0): Game byte +0xcbe = 1 / 0; Game+0x26d8 / +0x26dc: byte +0xdcc = 1 / 0.
// Returns false (al = 0; upper eax bytes not reproduced). The ecx of _stricmp is a leftover: 0. Not executed in
// recorded runs.
uint32_t MissionObject_OnTrigger(void* obj, void* sender, uint32_t id)
{
    const uint32_t o = u(obj);
    if (id == Field<uint32_t>(obj, 0x470))
    {
        if (!static_cast<uint8_t>(fn_0044e600()))
        {
            Field<uint8_t>(G(), 0xdd2) = 1;
            return 0;
        }
        uint32_t frame[5] = {0, 0, 0, 0, 0};  // +0 iterator (2 dwords), +8 direction (3 floats)
        const uint32_t marker = orig::fn_0044c030(0, 0xffffffffu);
        if (marker)
            orig::fn_00453150(marker, u(&frame[2]));
        else
        {
            orig::fn_00407ff0(Field<uint32_t>(G(), 0x34), 0x005b1850, 0, 0);
            frame[2] = 0;
            frame[3] = 0x3f800000;
            frame[4] = 0;
        }
        void* game = G();
        Field<uint8_t>(game, 0xdd2) = 1;
        Field<uint8_t>(game, 0xcbe) = 0;
        Field<uint32_t>(game, 0xbf0) = 0;
        Field<uint32_t>(game, 0xbf8) = 0;
        void* list = Field<void*>(Field<void*>(game, 0xa84), 0xe7c);
        for (uint32_t e = orig::fn_004e7340(u(list), u(&frame[0])); e; e = orig::fn_004e73c0(u(&frame[0])))
        {
            if (static_cast<uint8_t>(VCall<uint32_t>(P(e), 0x10c / 4)))
                VCall<void>(P(e), 0x234 / 4, &frame[2]);
        }
        if (frame[1])
            --Field<int32_t>(P(frame[1]), 0x18);
        return 0;
    }
    if (id == Field<uint32_t>(obj, 0x46c))
    {
        const uint32_t lvl = fn_0044a660(Field<uint32_t>(G(), 0xa64), 0);
        bool bonus = orig::fn_005448ee(0, lvl, 0x005b1844) == 0;
        if (!bonus)
        {
            const uint32_t lvl2 = fn_0044a660(Field<uint32_t>(G(), 0xa64), 0);
            bonus = orig::fn_005448ee(0, lvl2, 0x005b1838) == 0;
        }
        void* game = G();
        if (bonus)
            F(game, 0x272c) = F(game, 0x272c) + GameGlobal<float>(0x005bcc04);
        if (static_cast<uint8_t>(fn_0044e600()))
        {
            const uint32_t acc = Field<uint32_t>(game, 0xacc);
            if (acc && static_cast<uint8_t>(orig::fn_0049bcb0(acc)))
                return 0;
            if (Field<uint8_t>(game, 0xdcd))
                return 0;
        }
        F(game, 0x2730) = F(obj, 0x468) + F(game, 0x2730);
        fn_0044a580_U(Field<uint32_t>(game, 0xa64), 2);
        return 0;
    }
    const uint32_t f474 = Field<uint32_t>(obj, 0x474);
    if (id == f474 || id == Field<uint32_t>(obj, 0x478) || id == Field<uint32_t>(obj, 0x47c) ||
        id == Field<uint32_t>(obj, 0x480))
    {
        void* game = G();
        if (Field<uint8_t>(game, 0xdd2))
            return 0;
        if (Field<uint32_t>(game, 0xac8) == 1)
        {
            orig::fn_0049ad60(Field<uint32_t>(game, 0xacc));
            return 0;
        }
        uint32_t code;
        if (id == f474)
            code = 4;
        else if (id == Field<uint32_t>(obj, 0x478))
            code = 5;
        else if (id == Field<uint32_t>(obj, 0x47c))
            code = 6;
        else if (id == Field<uint32_t>(obj, 0x480))
            code = 7;
        else
            return 0;
        fn_0044a580_U(Field<uint32_t>(game, 0xa64), code);
        return 0;
    }
    void* game = G();
    if (id == Field<uint32_t>(game, 0x26f0))
    {
        if (!Field<uint32_t>(game, 0x26f4))
            return 0;
        ++Field<int32_t>(sender, 0x28);
        while (static_cast<uint8_t>(VCall<uint32_t>(sender, 0xd8 / 4)))
        {
            const uint32_t e = orig::fn_00406330();
            if (!e)
                break;
            orig::fn_004063a0(e);
            VCall<void>(P(e), 0xd4 / 4, 0u);
            float pos[3];
            VCall<void>(sender, 0x54 / 4, pos);
            pos[2] = pos[2] + GameGlobal<float>(0x005bcbdc);
            orig::fn_0044d1f0(u(pos), o, e);
            VCall<void>(sender, 0xd4 / 4, 0u);
        }
        if (--Field<int32_t>(sender, 0x28) == 0 && !static_cast<uint8_t>(VCall<uint32_t>(sender, 0xd8 / 4)))
            orig::fn_00487da0(u(sender));
        game = G();
        const uint32_t trig = Field<uint32_t>(game, 0x26f8);
        if (trig && Field<uint32_t>(game, 0x26e8) >= Field<uint32_t>(game, 0x26ec))
        {
            orig::fn_00461870(Field<uint32_t>(game, 0xa84), trig, 0);
            Field<uint32_t>(G(), 0x26f8) = 0;
        }
        return 0;
    }
    if (id == Field<uint32_t>(game, 0xc3c))
    {
        if (!Field<uint8_t>(game, 0xdd2))
            Field<uint8_t>(game, 0xcbe) = 1;
        return 0;
    }
    if (id == Field<uint32_t>(game, 0xc40))
    {
        if (!Field<uint8_t>(game, 0xdd2))
            Field<uint8_t>(game, 0xcbe) = 0;
        return 0;
    }
    if (id == Field<uint32_t>(game, 0x26d8))
    {
        Field<uint8_t>(game, 0xdcc) = 1;
        return 0;
    }
    if (id == Field<uint32_t>(game, 0x26dc))
        Field<uint8_t>(game, 0xdcc) = 0;
    return 0;
}
static uint32_t MissionObject_OnTrigger_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return MissionObject_OnTrigger(P(ecx), P(a0), a1);
}
ABI_ENTRY_0044d600(MissionObject_OnTrigger_Entry, MissionObject_OnTrigger_Abi)
HOOK(0x0044d600, "MissionObject_OnTrigger", MissionObject_OnTrigger_Entry, nullptr);

// 0x0044daf0 (cdecl; stack: mission object; caller pops; callers MissionObject_Update 0x0044db80 directly and as the
// callback passed to 0x00403370). Shows the mission start once: unless +0x5a4 is set, fn_00410150 (eax = 0, ecx =
// Game+0xa68) is true or Game byte +0xdce (cinematic) is set: +0x5a4 = 1, with a start text (+0x484) 0x00403410 (ecx =
// game; stack: Game+0x265c, 1); when Game byte +0x568 has 0x20: 0x004a07a0 (eax = level name 0x0044a660(Game+0xa64,
// cl = 0), ecx = game, edi = 1; stack: 0, pushed before the 0x0044a660 call); then 0x00403560 (esi = game). Returns
// eax as left (approximately: the last call's).
uint32_t MissionObject_ShowStart(void* obj)
{
    if (Field<uint8_t>(obj, 0x5a4))
        return Field<uint8_t>(obj, 0x5a4);
    void* game = G();
    uint32_t r = orig::fn_00410150(0, Field<uint32_t>(game, 0xa68));
    if (static_cast<uint8_t>(r))
        return r;
    if (Field<uint8_t>(game, 0xdce))
        return Field<uint8_t>(game, 0xdce);
    Field<uint8_t>(obj, 0x5a4) = 1;
    if (Field<uint8_t>(obj, 0x484))
    {
        orig::fn_00403410(u(game), Field<uint32_t>(game, 0x265c), 1);
        game = G();
    }
    if (Field<uint8_t>(game, 0x568) & 0x20)
    {
        const uint32_t lvl = fn_0044a660(Field<uint32_t>(game, 0xa64), 0);
        orig::fn_004a07a0(lvl, u(game), 1, 0);
        game = G();
    }
    return orig::fn_00403560(u(game));
}
static uint32_t MissionObject_ShowStart_Abi(uint32_t a0)
{
    return MissionObject_ShowStart(P(a0));
}
ABI_ENTRY_0044daf0(MissionObject_ShowStart_Entry, MissionObject_ShowStart_Abi)
HOOK(0x0044daf0, "MissionObject_ShowStart", MissionObject_ShowStart_Entry, nullptr);

// 0x0044db80 (ecx = mission object). MissionObject (vtable 0x005a49c8) slot 47 (+0xbc), the per-frame update:
// a pending trigger +0x460 is sent (Object_SendTrigger 0x00461870, eax = Game+0xa84, ebx = trigger; stack: object)
// and cleared; Game+0xb20 = Game+0xb1c, +0xb1c = 0 (per-frame kill count, guess). Nothing more while Game+0x2684 is
// set. T = [0x00604a98]. Once T - +0x464 > 0: first time (+0x5a5) on levels other than "lev_1_m1" with an intro movie
// (Game+0x267c) UI_PushScreen 0x00490b00 (eax = movie, ecx = UI; stack: "PlayMovie"), then 0x00403370 (eax = game,
// ecx = MissionObject_ShowStart 0x0044daf0 as callback, edx = object); later MissionObject_ShowStart(object) until it
// ran. When T - Game+0xb54 >= Game+0xb60: Game+0xb54 = T, Game+0xb60 = next random delay from Game+0xb58..+0xb5c,
// and a scan of list Game+0xa84 +0xe7c (objects with vt+0x10c() and vt+0x25c() true, position vt+0x54) for one
// within 10000 units of the player (distance squared < [0x005bce64]) whose result is never used. Game+0xbc8 counts
// down by the frame time; below 0 with Game+0xbb8 >= +0xbbc it restarts with a random delay (+0xbc0..+0xbc4), +0xbb8
// = 0, and 0x00493b70 (ebx = 0x10, edi = 0x005ce570; stack: 0x0063a18c, player+0xe60, player, 1, 0, 0x0044da10)
// runs (a random pick, guess). Finally Game+0xbb8 -= frame time * +0xbb4, clamped at 0. Not executed in recorded
// runs. 0x00493b70's ecx is pushed as scratch: 0. Original bug candidate: the nearest-object scan result is dropped.
uint32_t MissionObject_Update(void* obj)
{
    const uint32_t trig = Field<uint32_t>(obj, 0x460);
    if (trig)
    {
        orig::fn_00461870(Field<uint32_t>(G(), 0xa84), trig, u(obj));
        Field<uint32_t>(obj, 0x460) = 0;
    }
    void* game = G();
    Field<uint32_t>(game, 0xb20) = Field<uint32_t>(game, 0xb1c);
    Field<uint32_t>(game, 0xb1c) = 0;
    if (Field<uint32_t>(game, 0x2684))
        return 0;
    const uint32_t tBits = GameGlobal<uint32_t>(0x00604a98);
    const float T = u2f(tBits);
    if (T - F(obj, 0x464) > kZero)
    {
        if (!Field<uint8_t>(obj, 0x5a5))
        {
            Field<uint8_t>(obj, 0x5a5) = 1;
            const uint32_t lvl = fn_0044a660(Field<uint32_t>(game, 0xa64), 1);
            if (orig::fn_005448ee(0, 0x005b1738, lvl) != 0)
            {
                void* g = G();
                const uint32_t movie = Field<uint32_t>(g, 0x267c);
                if (movie)
                    orig::fn_00490b00(movie, Field<uint32_t>(g, 0xab4), 0x005bc0fc);
            }
            orig::fn_00403370(u(G()), 0x0044daf0, u(obj));
        }
        else if (!Field<uint8_t>(obj, 0x5a4))
            MissionObject_ShowStart(obj);
        game = G();
    }
    if (T - F(game, 0xb54) >= F(game, 0xb60))  // test ah,1 / jne: below or unordered skips
    {
        Field<uint32_t>(game, 0xb54) = tBits;
        const float hi = F(game, 0xb5c);
        const float lo = F(game, 0xb58);
        uint32_t iter[2] = {0, 0};
        const int32_t rnd = static_cast<int32_t>(orig::fn_005391cd());
        const float span = hi - lo;
        const float t = static_cast<float>(rnd) * span;
        const float s = t * GameGlobal<float>(0x005bcbc0);
        F(G(), 0xb60) = s + lo;
        float ppos[3];
        VCall<void>(Field<void*>(G(), 0x748), 0x54 / 4, ppos);
        void* list = Field<void*>(Field<void*>(G(), 0xa84), 0xe7c);
        uint32_t nearest = 0;  // computed and discarded, as in the original
        for (uint32_t e = orig::fn_004e7340(u(list), u(iter)); e; e = orig::fn_004e73c0(u(iter)))
        {
            if (!static_cast<uint8_t>(VCall<uint32_t>(P(e), 0x10c / 4)) ||
                !static_cast<uint8_t>(VCall<uint32_t>(P(e), 0x25c / 4)))
                continue;
            float epos[3];
            VCall<void>(P(e), 0x54 / 4, epos);
            if (!nearest)
            {
                nearest = e;
                continue;
            }
            const float dz = epos[2] - ppos[2];
            const float dy = epos[1] - ppos[1];
            const float dx = epos[0] - ppos[0];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < GameGlobal<float>(0x005bce64))
                nearest = e;
        }
        (void)nearest;
        if (iter[1])
            --Field<int32_t>(P(iter[1]), 0x18);
        game = G();
    }
    const float dt = kFrameTime;
    const float c = F(game, 0xbc8) - dt;
    F(game, 0xbc8) = c;
    if (c < kZero && F(game, 0xbb8) >= F(game, 0xbbc))
    {
        const float hi = F(game, 0xbc4);
        const float lo = F(game, 0xbc0);
        const int32_t rnd = static_cast<int32_t>(orig::fn_005391cd());
        void* g = G();
        const float span = hi - lo;
        const float t = static_cast<float>(rnd) * span;
        const float s = t * GameGlobal<float>(0x005bcbc0);
        F(g, 0xbc8) = s + lo;
        Field<uint32_t>(g, 0xbb8) = 0;
        const uint32_t player = Field<uint32_t>(g, 0x748);
        orig::fn_00493b70(0, 0x10, 0x005ce570, 0x0063a18c, player + 0xe60, player, 1, 0, 0x0044da10);
        game = G();
    }
    const float v = F(game, 0xbb8) - dt * F(game, 0xbb4);
    F(game, 0xbb8) = v;
    if (v < kZero)
        Field<uint32_t>(game, 0xbb8) = 0;
    return 0;
}
static uint32_t MissionObject_Update_Abi(uint32_t ecx)
{
    return MissionObject_Update(P(ecx));
}
ABI_ENTRY_0044db80(MissionObject_Update_Entry, MissionObject_Update_Abi)
HOOK(0x0044db80, "MissionObject_Update", MissionObject_Update_Entry, nullptr);

// 0x0044de90 (ecx = bomb; stack: 4; ret 0x10). PlayerTickerBomb (vtable 0x005ae380) slot 6, the load slot: base load
// 0x004517b0; triggers (0x00453450) send_trigger_set +0x46c, send_trigger_explode +0x478, send_trigger_explode2
// +0x47c, send_trigger_explode3 +0x480, send_trigger_non_enabled +0x470, receive_trigger_enable +0x474;
// message_radius +0x498 (int, 10); lock_parent_tag +0x49c; lock_parent_bone +0x4a0 (text); arm_time +0x468 (2.0);
// unarmed_falloff (0.3) read into the a2 stack slot and passed to vt+0x100; impact_force +0x484 (2010.0),
// area_damage +0x488, damage_radius_inner +0x48c, damage_radius_outer +0x490, +0x494 = 1 / (outer - inner) when outer
// > 0.01 and outer > inner, else 1.0; list registrations 0x004546d0 (Game+0xa84 +0xeb0, +0xeb4); byte +0x460 = 0,
// vt+0x100(unarmed_falloff), +0x464 = +0x4c0 = 0. Returns 0. Not executed in recorded runs.
uint32_t PlayerTickerBomb_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t o = u(obj);
    orig::fn_004517b0(o, a0, a1, a2, a3);
    orig::fn_00453450(0, 0x005b1724, a3, o + 0x46c, o, a2, 0);
    orig::fn_00453450(0, 0x005b170c, a3, o + 0x478, o, a2, 0);
    orig::fn_00453450(0, 0x005b16f4, a3, o + 0x47c, o, a2, 0);
    orig::fn_00453450(0, 0x005b16dc, a3, o + 0x480, o, a2, 0);
    orig::fn_00453450(0, 0x005b16c0, a3, o + 0x470, o, a2, 0);
    orig::fn_00453450(0, 0x005b16a8, a3, o + 0x474, o, a2, 0);
    orig::fn_00453400(0xa, a3, o + 0x498, o, 0x005b1698, a2, 0);
    orig::fn_00453450(0, 0x005b1688, a3, o + 0x49c, o, a2, 0);
    orig::fn_004533a0(0x0058a9a4, o + 0x4a0, o, 0x005b1674, a2, a3, 0);
    orig::fn_004534a0(a3, o + 0x468, o, 0x005b1668, a2, 0x40000000, 0);
    uint32_t slot2 = a2;  // the original's a2 argument slot, reused as an output
    orig::fn_004534a0(a3, u(&slot2), o, 0x005b1658, a2, 0x3e99999a, 0);
    orig::fn_004534a0(a3, o + 0x484, o, 0x005b93c8, a2, 0x44fb4000, 0);
    orig::fn_004534a0(a3, o + 0x488, o, 0x005b93bc, a2, 0, 0);
    orig::fn_004534a0(a3, o + 0x48c, o, 0x005b93a8, a2, 0, 0);
    orig::fn_004534a0(a3, o + 0x490, o, 0x005b9394, a2, 0, 0);
    const float outer = F(obj, 0x490);
    if (outer > GameGlobal<float>(0x005bcbec) && outer > F(obj, 0x48c))
        F(obj, 0x494) = kOne / (outer - F(obj, 0x48c));
    else
        Field<uint32_t>(obj, 0x494) = 0x3f800000;
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb0), o);
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb4), o);
    Field<uint8_t>(obj, 0x460) = 0;
    VCall<void>(obj, 0x100 / 4, slot2);
    Field<uint32_t>(obj, 0x464) = 0;
    Field<uint32_t>(obj, 0x4c0) = 0;
    return 0;
}
static uint32_t PlayerTickerBomb_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PlayerTickerBomb_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0044de90(PlayerTickerBomb_Load_Entry, PlayerTickerBomb_Load_Abi)
HOOK(0x0044de90, "PlayerTickerBomb_Load", PlayerTickerBomb_Load_Entry, nullptr);

// 0x0044e0c0 (eax = bomb; caller 0x0044e4a0). Sends send_trigger_set (+0x46c) when set (Object_SendTrigger 0x00461870,
// eax = Game+0xa84, ebx = trigger; stack: bomb). Returns its eax, else the bomb. Not executed in recorded runs.
uint32_t PlayerTickerBomb_SendSet(void* obj)
{
    const uint32_t t = Field<uint32_t>(obj, 0x46c);
    if (!t)
        return u(obj);
    return orig::fn_00461870(Field<uint32_t>(G(), 0xa84), t, u(obj));
}
static uint32_t PlayerTickerBomb_SendSet_Abi(uint32_t eax)
{
    return PlayerTickerBomb_SendSet(P(eax));
}
ABI_ENTRY_0044e0c0(PlayerTickerBomb_SendSet_Entry, PlayerTickerBomb_SendSet_Abi)
HOOK(0x0044e0c0, "PlayerTickerBomb_SendSet", PlayerTickerBomb_SendSet_Entry, nullptr);

// 0x0044e0e0 (eax = bomb; caller 0x00467450). Sends send_trigger_non_enabled (+0x470) when set, as 0x0044e0c0. Not
// executed in recorded runs.
uint32_t PlayerTickerBomb_SendNonEnabled(void* obj)
{
    const uint32_t t = Field<uint32_t>(obj, 0x470);
    if (!t)
        return u(obj);
    return orig::fn_00461870(Field<uint32_t>(G(), 0xa84), t, u(obj));
}
static uint32_t PlayerTickerBomb_SendNonEnabled_Abi(uint32_t eax)
{
    return PlayerTickerBomb_SendNonEnabled(P(eax));
}
ABI_ENTRY_0044e0e0(PlayerTickerBomb_SendNonEnabled_Entry, PlayerTickerBomb_SendNonEnabled_Abi)
HOOK(0x0044e0e0, "PlayerTickerBomb_SendNonEnabled", PlayerTickerBomb_SendNonEnabled_Entry, nullptr);

// 0x0044e100 (ecx = bomb; stack: target; ret 4). PlayerTickerBomb (vtable 0x005ae380) slot 7: the base slot
// 0x004527d0 (ecx = bomb; stack: target), then target byte +0xd = 1. Returns the base's eax.
uint32_t fn_0044e100(void* obj, void* target)
{
    const uint32_t r = orig::fn_004527d0(u(obj), u(target));
    Field<uint8_t>(target, 0xd) = 1;
    return r;
}
static uint32_t fn_0044e100_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0044e100(P(ecx), P(a0));
}
ABI_ENTRY_0044e100(fn_0044e100_Entry, fn_0044e100_Abi)
HOOK(0x0044e100, "fn_0044e100", fn_0044e100_Entry, nullptr);

// 0x0044e120 (ecx = bomb). PlayerTickerBomb (vtable 0x005ae380) slot 8 (+0x20): with lock_parent_tag (+0x49c) the
// parent 0x004592e0 (eax = 0, ecx = tag; stack: bomb), when found, gets the bomb attached at lock_parent_bone (+0x4a0)
// via 0x004539f0 (eax = bone name, ecx = parent, edx = bomb; stack: 0, 1, 0). Returns eax as left.
uint32_t PlayerTickerBomb_AttachToParent(void* obj)
{
    const uint32_t tag = Field<uint32_t>(obj, 0x49c);
    if (!tag)
        return 0;
    const uint32_t parent = orig::fn_004592e0(0, tag, u(obj));
    if (!parent)
        return 0;
    return static_cast<uint32_t>(orig::fn_004539f0(u(obj) + 0x4a0, parent, u(obj), 0, 1, 0));
}
static uint32_t PlayerTickerBomb_AttachToParent_Abi(uint32_t ecx)
{
    return PlayerTickerBomb_AttachToParent(P(ecx));
}
ABI_ENTRY_0044e120(PlayerTickerBomb_AttachToParent_Entry, PlayerTickerBomb_AttachToParent_Abi)
HOOK(0x0044e120, "PlayerTickerBomb_AttachToParent", PlayerTickerBomb_AttachToParent_Entry, nullptr);

// 0x0044e150 (ecx = bomb). PlayerTickerBomb (vtable 0x005ae380) slot 47 (+0xbc), the per-frame update. Armed
// (0x0044e390, byte +0x460): the fuse Game+0x25e4 counts down by the frame time; at 0 (+0x25e4 = 0, Game byte +0xdcf
// = 0): send_trigger_explode (+0x478) via Object_SendTrigger, position vt+0x6c(&pos), 0x0047f020 (ebx =
// &area_damage +0x488; stack: 0, &pos, 0, impact_force +0x484, 0, 0, 0), vt+0xd4(0), and the widget 0x00463890 (ebx =
// "ui_tickerbomb_patience"; stack: Game+0xa70) loses flag 4 in +0x474. Not armed, with a set time (+0x464 != 0): when
// 0x00414810 (al = 1, ebx = 0x14, esi = Game+0xa80; stack: 0.5) (button held, guess) and the player is usable
// (0x00454780 false) and within message_radius (distance sqrt(dx*dx + dy*dy + dz*dz) between the positions (vt+0x54)
// compared with 0x0044b8b0 (eax = bomb) as an unsigned value: fild + 2^32 when negative): the widget 0x00463ad0 (ebx =
// 0; stack: Game+0xa70, "ui_tickerbomb_set") gets flag 4 and progress +0x4a0 = (T - +0x464) / arm_time (+0x468); once
// T - +0x464 >= arm_time 0x0044e4a0 (edi = bomb) (arm, guess) and +0x464 = 0. Otherwise 0x0044e430 (edi = bomb).
// T = [0x00604a98]. Returns eax as left (not reproduced). Not executed in recorded runs.
uint32_t PlayerTickerBomb_Update(void* obj)
{
    const uint32_t o = u(obj);
    if (static_cast<uint8_t>(PlayerTickerBomb_IsArmed_U(o)))
    {
        void* game = G();
        const float v = F(game, 0x25e4) - kFrameTime;
        F(game, 0x25e4) = v;
        if (!(v <= kZero))
            return 0;
        Field<uint32_t>(game, 0x25e4) = 0;
        Field<uint8_t>(game, 0xdcf) = 0;
        const uint32_t trig = Field<uint32_t>(obj, 0x478);
        if (trig)
            orig::fn_00461870(Field<uint32_t>(game, 0xa84), trig, o);
        float pos[3];
        VCall<void>(obj, 0x6c / 4, pos);
        orig::fn_0047f020(o + 0x488, 0, u(pos), 0, Field<uint32_t>(obj, 0x484), 0, 0, 0);
        VCall<void>(obj, 0xd4 / 4, 0u);
        const uint32_t w = orig::fn_00463890(0x005b1640, Field<uint32_t>(G(), 0xa70));
        if (w)
            Field<uint32_t>(P(w), 0x474) &= 0xfffffffbu;
        return w;
    }
    if (F(obj, 0x464) == kZero)
        return 0;
    if (static_cast<uint8_t>(orig::fn_00414810(1, 0x14, Field<uint32_t>(G(), 0xa80), 0x3f000000)) &&
        !static_cast<uint8_t>(orig::fn_00454780(Field<uint32_t>(G(), 0x748))))
    {
        float ppos[3], opos[3];
        VCall<void>(Field<void*>(G(), 0x748), 0x54 / 4, ppos);
        VCall<void>(obj, 0x54 / 4, opos);
        const float dz = opos[2] - ppos[2];
        const float dy = opos[1] - ppos[1];
        const float dx = opos[0] - ppos[0];
        const float sum = dx * dx + dy * dy + dz * dz;
        const float dist = std::sqrt(sum);
        const int32_t radius = static_cast<int32_t>(orig::fn_0044b8b0(o));
        float r = static_cast<float>(radius);
        if (radius < 0)
            r = r + GameGlobal<float>(0x005bcb28);
        if (!(r < dist))
        {
            const uint32_t w = orig::fn_00463ad0(0, Field<uint32_t>(G(), 0xa70), 0x005b162c);
            const float T = GameGlobal<float>(0x00604a98);
            if (w)
            {
                Field<uint32_t>(P(w), 0x474) |= 4;
                F(P(w), 0x4a0) = (T - F(obj, 0x464)) / F(obj, 0x468);
            }
            if (GameGlobal<float>(0x00604a98) - F(obj, 0x464) >= F(obj, 0x468))
            {
                const uint32_t r2 = orig::fn_0044e4a0(o);
                Field<uint32_t>(obj, 0x464) = 0;
                return r2;
            }
            return 0;
        }
    }
    return orig::fn_0044e430(o);
}
static uint32_t PlayerTickerBomb_Update_Abi(uint32_t ecx)
{
    return PlayerTickerBomb_Update(P(ecx));
}
ABI_ENTRY_0044e150(PlayerTickerBomb_Update_Entry, PlayerTickerBomb_Update_Abi)
HOOK(0x0044e150, "PlayerTickerBomb_Update", PlayerTickerBomb_Update_Entry, nullptr);

// 0x0044e380 (ecx = bomb; callers 0x00467450, 0x004788a0). Returns receive_trigger_enable (+0x474) == 0 (enabled).
// Not executed in recorded runs.
uint32_t PlayerTickerBomb_IsEnabled(void* obj)
{
    return Field<uint32_t>(obj, 0x474) == 0 ? 1u : 0u;
}
static uint32_t PlayerTickerBomb_IsEnabled_Abi(uint32_t ecx)
{
    return PlayerTickerBomb_IsEnabled(P(ecx));
}
ABI_ENTRY_0044e380(PlayerTickerBomb_IsEnabled_Entry, PlayerTickerBomb_IsEnabled_Abi)
HOOK(0x0044e380, "PlayerTickerBomb_IsEnabled", PlayerTickerBomb_IsEnabled_Entry, nullptr);

// 0x0044e390 (eax = bomb). Returns the armed byte +0x460 in al (upper eax bytes are the pointer's).
uint32_t PlayerTickerBomb_IsArmed(void* obj)
{
    return (u(obj) & 0xffffff00u) | Field<uint8_t>(obj, 0x460);
}
static uint32_t PlayerTickerBomb_IsArmed_Abi(uint32_t eax)
{
    return PlayerTickerBomb_IsArmed(P(eax));
}
ABI_ENTRY_0044e390(PlayerTickerBomb_IsArmed_Entry, PlayerTickerBomb_IsArmed_Abi)
HOOK(0x0044e390, "PlayerTickerBomb_IsArmed", PlayerTickerBomb_IsArmed_Entry, nullptr);

// 0x0044e5e0 (ecx = bomb; stack: a0 (unused), trigger id; ret 8). PlayerTickerBomb (vtable 0x005ae380) slot 14
// (+0x38), the trigger receiver: receive_trigger_enable (+0x474) clears itself (enables the bomb). Returns false (al =
// 0; upper eax bytes not reproduced). Not executed in recorded runs.
uint32_t PlayerTickerBomb_OnTrigger(void* obj, uint32_t /*a0*/, uint32_t id)
{
    if (id == Field<uint32_t>(obj, 0x474))
        Field<uint32_t>(obj, 0x474) = 0;
    return 0;
}
static uint32_t PlayerTickerBomb_OnTrigger_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return PlayerTickerBomb_OnTrigger(P(ecx), a0, a1);
}
ABI_ENTRY_0044e5e0(PlayerTickerBomb_OnTrigger_Entry, PlayerTickerBomb_OnTrigger_Abi)
HOOK(0x0044e5e0, "PlayerTickerBomb_OnTrigger", PlayerTickerBomb_OnTrigger_Entry, nullptr);

// 0x0044e600. Returns false (al = 0) when Game+0x748 (player) is 0, else Game+0x26cc (start_in_turret_tag) != 0; upper
// eax bytes are g_game's. Many camera/player callers.
uint32_t fn_0044e600()
{
    void* game = G();
    if (!Field<uint32_t>(game, 0x748))
        return u(game) & 0xffffff00u;
    return (u(game) & 0xffffff00u) | (Field<uint32_t>(game, 0x26cc) != 0 ? 1u : 0u);
}
static uint32_t fn_0044e600_Abi()
{
    return fn_0044e600();
}
ABI_ENTRY_0044e600(fn_0044e600_Entry, fn_0044e600_Abi)
HOOK(0x0044e600, "fn_0044e600", fn_0044e600_Entry, nullptr);

// 0x0044e620 (caller 0x0047afa0). Returns Game byte +0xdd2 (mission finished, guess: MissionObject sets it on the
// success trigger) in al; upper eax bytes are g_game's.
uint32_t fn_0044e620()
{
    void* game = G();
    return (u(game) & 0xffffff00u) | Field<uint8_t>(game, 0xdd2);
}
static uint32_t fn_0044e620_Abi()
{
    return fn_0044e620();
}
ABI_ENTRY_0044e620(fn_0044e620_Entry, fn_0044e620_Abi)
HOOK(0x0044e620, "fn_0044e620", fn_0044e620_Entry, nullptr);

// 0x0044e630 (ecx = molmer; stack: 4; ret 0x10). MolmerObject (vtable 0x005ae5f8) slot 6, the load slot: +0xbd0 =
// +0xbd4 = 0 (held bomb / dynamite), base load 0x00483250 (same arguments), then on the hand setup object+0x61c:
// 0x004a0ea0 (eax = "ref_rhand", ecx = molmer, esi = +0x61c, edi = "ref_lhand"; stack: 0), 0x004a0ef0 (eax = 0xc, edi
// = +0x61c), 0x004a10b0 (eax = +0x61c, ecx = 1), 0x004a10e0 (ebx = +0x61c; stack: 0), 0x004a10b0 (ecx = 0),
// 0x004a10e0 (stack: ecx as 0x004a10b0 leaves it, taken as 0), 0x00428250 (eax = molmer, cl = 1). Not executed in
// recorded runs.
uint32_t MolmerObject_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t o = u(obj);
    Field<uint32_t>(obj, 0xbd0) = 0;
    Field<uint32_t>(obj, 0xbd4) = 0;
    orig::fn_00483250(o, a0, a1, a2, a3);
    const uint32_t hands = o + 0x61c;
    orig::fn_004a0ea0(0x005b6160, o, hands, 0x005bb99c, 0);
    orig::fn_004a0ef0(0xc, hands);
    orig::fn_004a10b0(hands, 1);
    orig::fn_004a10e0(hands, 0);
    orig::fn_004a10b0(hands, 0);
    orig::fn_004a10e0(hands, 0);
    return orig::fn_00428250(o, 1);
}
static uint32_t MolmerObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return MolmerObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0044e630(MolmerObject_Load_Entry, MolmerObject_Load_Abi)
HOOK(0x0044e630, "MolmerObject_Load", MolmerObject_Load_Entry, nullptr);

// 0x0044e740 (ecx = molmer; stack: a0, event name, a2 (-> bone name), a3; ret 0x10). MolmerObject (vtable 0x005ae5f8)
// slot 13 (+0x34), the animation event handler (guess):
//  - "make_bomb": when +0xbd0 is empty, ObjectSystem_CreateObject 0x0045f020 (eax = 0, ecx = "player_tickerbomb";
//    stack: Game+0xa84, 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0) -> +0xbd0, list registrations 0x00454750 (Game+0xa84 +0xeb0,
//    +0xeb4), reference +0x28 += 1, scale 0x004535b0 (eax = &v, ecx = bomb) * 0.25 written back by 0x004535d0, and
//    0x004539f0 (eax = *a2, ecx = molmer, edx = bomb; stack: 0, 1, 0x10) (attach to the hand bone, guess);
//  - "plant_bomb": 0x004090a0 (eax = a3, edx = a2; stack: &a2 slot), target 0x004592e0 (eax = Game+0xa84 +0xe70, ecx =
//    +0x534; stack: molmer); if found 0x00453b20 (esi = bomb), 0x004539f0 (eax = a2 slot, ecx = target, edx = bomb;
//    stack: 0, 1, 0x10) and the bomb reference +0xbd0 is released;
//  - "make_tnt": when +0xbd4 is empty, CreateObject (eax = "defaultgameobject", ecx = "molmer_dynamite") -> +0xbd4
//    (reference += 1); it gets vt+0xf8(0x100, 0), 0x004539f0 (eax = *a2, ecx = molmer, edx = it; stack: 0, 1, 0),
//    Object_setPhysicsEnabled(1), then 0x00429a40 (al = 0, ecx = molmer);
//  - "Fire_tnt": +0xbd4 gets vt+0xdc() and is released, 0x00428340 (ecx = *a2, esi = molmer; stack:
//    "molmer_dynamite", 1), 0x00429a40 (al = 1, ecx = molmer);
//  - anything else: the base handler 0x0043d180 (ecx = molmer; same 4 arguments).
// _stricmp's ecx is a leftover: 0. Not executed in recorded runs.
uint32_t MolmerObject_OnAnimEvent(void* obj, uint32_t a0, uint32_t name, uint32_t a2, uint32_t a3)
{
    const uint32_t o = u(obj);
    if (orig::fn_005448ee(0, name, 0x005b15c0) == 0)
    {
        if (Field<uint32_t>(obj, 0xbd0))
            return 0;
        const uint32_t bomb =
            orig::fn_0045f020(0, 0x005b15ac, Field<uint32_t>(G(), 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
        Field<uint32_t>(obj, 0xbd0) = bomb;
        orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb0), bomb);
        orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xeb4), Field<uint32_t>(obj, 0xbd0));
        ++Field<int32_t>(Field<void*>(obj, 0xbd0), 0x28);
        const uint32_t b = Field<uint32_t>(obj, 0xbd0);
        float v[3];
        orig::fn_004535b0(u(v), b);
        const float k = GameGlobal<float>(0x005bcbe4);
        v[0] = v[0] * k;
        v[1] = v[1] * k;
        v[2] = v[2] * k;
        orig::fn_004535d0(u(v), b);
        return static_cast<uint32_t>(
            orig::fn_004539f0(*reinterpret_cast<uint32_t*>(a2), o, Field<uint32_t>(obj, 0xbd0), 0, 1, 0x10));
    }
    if (orig::fn_005448ee(0, name, 0x005b15a0) == 0)
    {
        uint32_t slot2 = a2;  // the original's a2 argument slot, passed by address
        orig::fn_004090a0(a3, a2, u(&slot2));
        const uint32_t target = orig::fn_004592e0(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe70),
                                                  Field<uint32_t>(obj, 0x534), o);
        if (!target)
            return 0;
        orig::fn_00453b20(Field<uint32_t>(obj, 0xbd0));
        orig::fn_004539f0(slot2, target, Field<uint32_t>(obj, 0xbd0), 0, 1, 0x10);
        return ReleaseRef(&Field<uint32_t>(obj, 0xbd0), 0);
    }
    if (orig::fn_005448ee(0, name, 0x005b1594) == 0)
    {
        if (!Field<uint32_t>(obj, 0xbd4))
        {
            const uint32_t t =
                orig::fn_0045f020(0x005b1580, 0x005b1570, Field<uint32_t>(G(), 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
            Field<uint32_t>(obj, 0xbd4) = t;
            ++Field<int32_t>(P(t), 0x28);
        }
        VCall<void>(Field<void*>(obj, 0xbd4), 0xf8 / 4, 0x100u, 0u);
        orig::fn_004539f0(*reinterpret_cast<uint32_t*>(a2), o, Field<uint32_t>(obj, 0xbd4), 0, 1, 0);
        const uint32_t r = static_cast<uint32_t>(orig::fn_004577b0(Field<uint32_t>(obj, 0xbd4), 1));
        return orig::fn_00429a40(r & 0xffffff00u, o);
    }
    if (orig::fn_005448ee(0, name, 0x005b1564) == 0)
    {
        if (void* t = Field<void*>(obj, 0xbd4))
        {
            VCall<void>(t, 0xdc / 4);
            ReleaseRef(&Field<uint32_t>(obj, 0xbd4), 0);
        }
        const uint32_t r = orig::fn_00428340(*reinterpret_cast<uint32_t*>(a2), o, 0x005b1570, 1);
        return orig::fn_00429a40((r & 0xffffff00u) | 1, o);
    }
    return orig::fn_0043d180(o, a0, name, a2, a3);
}
static uint32_t MolmerObject_OnAnimEvent_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return MolmerObject_OnAnimEvent(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0044e740(MolmerObject_OnAnimEvent_Entry, MolmerObject_OnAnimEvent_Abi)
HOOK(0x0044e740, "MolmerObject_OnAnimEvent", MolmerObject_OnAnimEvent_Entry, nullptr);

// 0x0044f4c0 (eax = record; callers 0x0044a110, 0x0044a230, 0x0044a670, 0x0044f4a0). Resets a record: +0x1c = -1, +0
// = +4 = 0, +8 = 1.0, +0x14 = +0x18 = 0, bytes +0x10 = +0x12 = 0, +0x20 = 0; Game +0xd78 = +0xd7c = 0; then the
// renderer (Game+0x2c) vt+0xd8(record+0xc, 1). Returns its eax.
uint32_t fn_0044f4c0(void* rec)
{
    void* game = G();
    Field<uint32_t>(rec, 0x1c) = 0xffffffff;
    Field<uint32_t>(rec, 0) = 0;
    Field<uint32_t>(rec, 4) = 0;
    Field<uint32_t>(rec, 8) = 0x3f800000;
    Field<uint32_t>(rec, 0x14) = 0;
    Field<uint32_t>(rec, 0x18) = 0;
    Field<uint8_t>(rec, 0x10) = 0;
    Field<uint8_t>(rec, 0x12) = 0;
    Field<uint32_t>(rec, 0x20) = 0;
    Field<uint32_t>(game, 0xd78) = 0;
    Field<uint32_t>(game, 0xd7c) = 0;
    return VCall<uint32_t>(Field<void*>(game, 0x2c), 0xd8 / 4, u(rec) + 0xc, 1u);
}
static uint32_t fn_0044f4c0_Abi(uint32_t eax)
{
    return fn_0044f4c0(P(eax));
}
ABI_ENTRY_0044f4c0(fn_0044f4c0_Entry, fn_0044f4c0_Abi)
HOOK(0x0044f4c0, "fn_0044f4c0", fn_0044f4c0_Entry, nullptr);

// 0x0044f4a0 (esi = record; caller 0x004011e0). +0xc = 0, then fn_0044f4c0 (eax = record). Returns the record.
uint32_t fn_0044f4a0(void* rec)
{
    Field<uint32_t>(rec, 0xc) = 0;
    fn_0044f4c0(rec);
    return u(rec);
}
static uint32_t fn_0044f4a0_Abi(uint32_t esi)
{
    return fn_0044f4a0(P(esi));
}
ABI_ENTRY_0044f4a0(fn_0044f4a0_Entry, fn_0044f4a0_Abi)
HOOK(0x0044f4a0, "fn_0044f4a0", fn_0044f4a0_Entry, nullptr);

// 0x0044f560 (eax = record, cl = value). Bytes +0x10 = 1, +0x11 = cl, +8 = 0. Not executed in recorded runs.
uint32_t fn_0044f560(void* rec, uint32_t value)
{
    Field<uint8_t>(rec, 0x10) = 1;
    Field<uint8_t>(rec, 0x11) = static_cast<uint8_t>(value);
    Field<uint32_t>(rec, 8) = 0;
    return u(rec);
}
static uint32_t fn_0044f560_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0044f560(P(eax), ecx);
}
ABI_ENTRY_0044f560(fn_0044f560_Entry, fn_0044f560_Abi)
HOOK(0x0044f560, "fn_0044f560", fn_0044f560_Entry, nullptr);

// 0x0044f570 (eax = record). Byte +0x12 = 1, +8 = 0. Not executed in recorded runs.
uint32_t fn_0044f570(void* rec)
{
    Field<uint8_t>(rec, 0x12) = 1;
    Field<uint32_t>(rec, 8) = 0;
    return u(rec);
}
static uint32_t fn_0044f570_Abi(uint32_t eax)
{
    return fn_0044f570(P(eax));
}
ABI_ENTRY_0044f570(fn_0044f570_Entry, fn_0044f570_Abi)
HOOK(0x0044f570, "fn_0044f570", fn_0044f570_Entry, nullptr);
