// Recovered (static, batch missing_01): game functions that symbols/functions.csv does not list (Ghidra never made
// them functions). Candidates from work/classify/missing_functions.csv (tools/find_missing_functions.py: call/jmp
// targets and code pointers) and work/classify/missing_reported.csv (reported by recovery agents). Compile-checked
// only (the game was not run); the oracle (tools/compare.py) is still to do. None of them is executed in the recorded
// runs as far as the tracker knows (they had no tracker row): "not verifiable by compare yet".
//
// Each candidate was disassembled (capstone, game.unpacked.exe) from its address to its ret / tail jump, its callers
// checked (E8/E9 at instruction boundaries, pointers in .rdata/.data or push imm32 in code), and its ABI derived by
// hand (first reads before writes, `ret N` / caller `add esp`, st0 results). There are no generated ABI_ENTRY_ stubs
// for these addresses, so every entry stub below is hand-written in the shape of src/gen/entry_stubs.h: save every
// register except the returned ones, push the inputs as cdecl arguments (register inputs in the order eax ecx edx ebx
// esi edi, then stack arguments), call the _Abi adapter, restore, `ret N`.
//
// Rejected candidate: 0x0049ca86 is nop padding before fn_0049ca90; its only "caller" (E9 at 0x0048c9c2) is the
// second byte of `mov ebp, ecx` (8B E9) at 0x0048c9c1 inside SaveGame_Load, not a jump.
//
//   0x00433e70 EmptyMethod_Ret4            0x004eb930 CurveEval_Bezier         0x004a2e10 SaveSlot_OnDeleteConfirmed
//   0x00454530 Object_DrawBase             0x004eba50 CurveParam_Ease          0x00499670 Turret_BoneCallback
//   0x00455d90 Object_IsInViewCone         0x004f15a0 Quat_SlerpRaw            0x0049a0b0 MountedTurret_BoneCallback
//   0x004695d0 PubScreen_HandleInput       0x004f16b0 Quat_Squad               0x0042c080 fn_0042c080
//   0x0046e2f0 fn_0046e2f0                 0x004f1920 Quat_Unpack32            0x004a8810 SpeedTrapAgent_Create
//   0x004e0a00 fn_004e0a00                 0x004fb800 fn_004fb800              0x004a8840 SpeedTrapAgent_CreateSwapped
//   0x004e0b10 PMS_DebugDelete             0x004fba10 fn_004fba10              0x004df500 Type19Agent_Create
//   0x004e7330 PMS_ListIterator_Release    0x00500fe0 WaveFile_CloseHandle     0x004dfa80 Type19Shape_BuildDisplay
//   0x00448db0 ScenePass_QueueWorldCell    0x00489050 ScenePass_DrawWorldObject
#include <cmath>
#include <cstdint>
#include <cstring>

#include "game.h"
#include "gen/orig_calls.h"
#include "hooks.h"

namespace
{
template <class T>
inline uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
inline uint8_t* B(uint32_t v)
{
    return reinterpret_cast<uint8_t*>(v);
}
inline uint32_t& D(const void* p, uint32_t o)
{
    return Field<uint32_t>(p, o);
}
inline float& F(const void* p, uint32_t o)
{
    return Field<float>(p, o);
}
inline uint8_t& Bt(const void* p, uint32_t o)
{
    return Field<uint8_t>(p, o);
}
// A float constant of the game's .rdata (0x0056c05c = 0.0, 0x005bcb2c = 1.0, 0x005bcb5c = -1.0, 0x005bcb4c = 0.5,
// 0x005bcb7c = 3.0, 0x005bcb80 = 2.0, 0x005bcc60 = 1/3, 0x005bcbb0 = 0.0001, 0x005bcd78 = 1/511, 0x005bcc18 = 1/255,
// 0x005bcdb8 = 57.339447, 0x005bcedc = 0.15, 0x005bcf30 = 6.6666665).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
inline uint8_t* G()
{
    return B(GameGlobal<uint32_t>(0x005fe490));
}
inline uint32_t f2u(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

// Calls an original function that takes eax and returns its result in st0 (popped here as a float). Used for
// PubObject_GetHealthPool 0x0046a6a0 (`fld [eax+0x46c]; ret`; symbols/abi.csv lists no result, so the generated
// orig:: wrapper would leave st0 on the x87 stack).
float CallSt0Eax(uint32_t fn, uint32_t eaxValue)
{
    float r;
    __asm {
        mov eax, eaxValue
        call fn
        fstp r
    }
    return r;
}
// fld y; fld x; fpatan; fstp float.
float Atan2F(float y, float x)
{
    float r;
    __asm {
        fld dword ptr [y]
        fld dword ptr [x]
        fpatan
        fstp dword ptr [r]
    }
    return r;
}
// fld x; fsin; fmul k; fstp float (fsin's extended result is rounded once, by the fmul, as in the original).
float SinMulF(float x, float k)
{
    float r;
    __asm {
        fld dword ptr [x]
        fsin
        fmul dword ptr [k]
        fstp dword ptr [r]
    }
    return r;
}
// The audio object (game+0x2c) vt+0xd0(name, 0, 0, 0, -1, 0): plays a UI sound (as in 0x00467fe0).
inline void PlayUiSound(uint32_t name)
{
    VCall<uint32_t>(B(D(G(), 0x2c)), 0xd0 / 4, name, 0u, 0u, 0u, 0xffffffffu, 0u);
}
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t Quat_SlerpRaw(const float* q1, const float* q0, float* out, float t);

// ---------------------------------------------------------------------------------------------------------------
// 0x00433e70 (no register inputs; stack: one argument, ignored; ret 4; eax preserved). The body is a single
// `ret 4`: an empty virtual method with one argument, shared by identical-code folding across 421 .rdata pointers
// (vtable slots; PMS_List vtable slot 3, see src/engine/engine_02.cpp) and the tail jump of ScenePass_RenderMain
// (0x00489627). Size 3, nop padding up to 0x00433e80 (room for the hook).
void EmptyMethod_Ret4()
{
}
static void EmptyMethod_Ret4_Abi()
{
    EmptyMethod_Ret4();
}
static __declspec(naked) void EmptyMethod_Ret4_Entry()
{
    __asm {
        push eax
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        call EmptyMethod_Ret4_Abi
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        pop eax
        ret 4
    }
}
HOOK(0x00433e70, "EmptyMethod_Ret4", EmptyMethod_Ret4_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00454530 (ecx = object; no stack arguments; plain ret). The base object draw (guess name): FxObject_Draw
// 0x00419060 tail-jumps here (render_r6.cpp) and 119 .rdata pointers (vtable slot +0xc0) reference it; the scene
// pass world walk 0x00489050 calls it through vt+0xc0. Steps:
//  - vt+0x30() (visible?): al == 0 -> return (its eax);
//  - 0x004543f0(eax = 0, ecx = object, edx = 0; 0, 1, 0), 0x00408800(eax = 4, ecx = object) -> p,
//    groups = 0x00453120(eax = object, edx = +0x374; [p]), 0x00457c20(ebx = object);
//  - a six-dword draw item source: {vt+0x7c(), group entry, mesh entry, +0x410, 0, object}; translucent byte =
//    0x00453630(object) (st0) < 1.0;
//  - when (+0x414)+0x44 & game+0x4c8: 0x00410fa0(object) (cdecl);
//  - for each group i < groups+0x14 (pointers from groups+0x18): mesh = [group+4]; when mesh+0x78 is set, each of
//    its mesh+0x80 entries (0x2c bytes from mesh+0x84) is queued with ItemList_Add 0x0048a0d0 (eax = game+0xa78;
//    &source, object, translucent).
// Deviation: the translucent argument is pushed as a dword whose upper bytes are uninitialised stack in the
// original (`mov byte ptr [esp+0xc], 1`); here they are 0 (ItemList_Add reads the byte only). The return value of
// the drawing paths (whatever eax held) is not reproduced: 0.
uint32_t Object_DrawBase(uint8_t* obj)
{
    const uint32_t visible = VCall<uint32_t>(obj, 0x30 / 4);
    if (!static_cast<uint8_t>(visible))
        return visible;
    orig::fn_004543f0(0, u(obj), 0, 0, 1, 0);
    const uint32_t p = orig::fn_00408800(4, u(obj));
    uint8_t* groups = B(orig::fn_00453120(u(obj), D(obj, 0x374), D(B(p), 0)));
    orig::fn_00457c20(u(obj));
    uint32_t source[6];  // the original's [esp+0x28..0x3c]
    source[0] = VCall<uint32_t>(obj, 0x7c / 4);
    source[3] = D(obj, 0x410);
    source[4] = 0;
    source[5] = u(obj);
    source[1] = 0;
    source[2] = 0;
    const uint32_t translucent = orig::fn_00453630(u(obj)) < K(0x005bcb2c) ? 1u : 0u;
    if (D(B(D(obj, 0x414)), 0x44) & D(G(), 0x4c8))
        orig::fn_00410fa0(u(obj));
    if (!groups)
        return 0;
    if (static_cast<int32_t>(D(groups, 0x14)) <= 0)
        return 0;
    uint32_t* cursor = &D(groups, 0x18);
    int32_t i = 0;
    do
    {
        uint8_t* mesh = B(D(B(*cursor), 4));
        if (D(mesh, 0x78) != 0 && static_cast<int32_t>(D(mesh, 0x80)) > 0)
        {
            uint32_t offset = 0;
            int32_t k = 0;
            do
            {
                source[2] = D(mesh, 0x84) + offset;
                source[1] = *cursor;
                orig::fn_0048a0d0(D(G(), 0xa78), u(source), u(obj), translucent);
                ++k;
                offset += 0x2c;
            } while (k < static_cast<int32_t>(D(mesh, 0x80)));
        }
        ++i;
        ++cursor;
    } while (i < static_cast<int32_t>(D(groups, 0x14)));
    return 0;
}
static uint32_t Object_DrawBase_Abi(uint32_t ecx)
{
    return Object_DrawBase(B(ecx));
}
static __declspec(naked) void Object_DrawBase_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push ecx
        call Object_DrawBase_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x00454530, "Object_DrawBase", Object_DrawBase_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00455d90 (ecx = object; stack: target object, cone angle (float, degrees); ret 8; returns al). Reached by the
// tail jump of fn_00447020 (0x0044702e) and 162 .rdata pointers (vtable slots). Whether the target lies within the
// object's view cone (guess name): no target -> 0. dir = 0x00453150(ecx = object, esi = &dir) (the facing),
// both positions through vt+0x54(&pos), d = (target - object).xy normalised in place by Vec2_Normalize 0x004e7780
// (its st0 length dropped), dot = dir.y * d.y + d.x * dir.x clamped to [-1, 1], then the CRT _CI function 0x00566ad0
// (acos; it takes and returns st0) and: angle * 0.5 > 57.339447 * acos(dot) -> 1, else 0. The dot is kept in the
// target argument's stack slot in the original. Frame: one 0x30-byte local block (diff at +0, target position +0xc,
// own position +0x18, facing +0x24).
namespace
{
// fld dot; call 0x00566ad0; fld 57.339447; fmul st, st(1); fld angle; fmul 0.5; fcompp; fnstsw; fstp st(0):
// true when neither C0 nor C3 is set (angle * 0.5 > 57.34 * acos(dot)).
bool ConeTest(float dot, float angle)
{
    static const uint32_t target = 0x00566ad0;
    uint16_t sw;
    __asm {
        push ebx
        push esi
        push edi
        fld dword ptr [dot]
        call dword ptr [target]
        fld dword ptr ds:[0x005bcdb8]
        fmul st, st(1)
        fld dword ptr [angle]
        fmul dword ptr ds:[0x005bcb4c]
        fcompp
        fnstsw ax
        fstp st(0)
        mov sw, ax
        pop edi
        pop esi
        pop ebx
    }
    return (sw & 0x4100) == 0;
}
}  // namespace
uint32_t Object_IsInViewCone(uint8_t* self, uint8_t* target, float angle)
{
    if (!target)
        return 0;
    uint32_t frame[12] = {};
    float* f = reinterpret_cast<float*>(frame);
    orig::fn_00453150(u(self), u(&frame[9]));
    VCall<uint32_t>(self, 0x54 / 4, u(&frame[6]));
    VCall<uint32_t>(target, 0x54 / 4, u(&frame[3]));
    f[0] = f[3] - f[6];
    f[1] = f[4] - f[7];
    orig::fn_004e7780(u(&frame[0]), u(&frame[0]));
    float a = f[10] * f[1];
    const float b = f[0] * f[9];
    float dot = a + b;
    if (dot > K(0x005bcb2c))
        dot = K(0x005bcb2c);
    else if (dot < K(0x005bcb5c))
        dot = K(0x005bcb5c);
    return ConeTest(dot, angle) ? 1u : 0u;
}
static uint32_t Object_IsInViewCone_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    float angle;
    std::memcpy(&angle, &a1, 4);
    return Object_IsInViewCone(B(ecx), B(a0), angle);
}
static __declspec(naked) void Object_IsInViewCone_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        push ecx
        call Object_IsInViewCone_Abi
        add esp, 12
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret 8
    }
}
HOOK(0x00455d90, "Object_IsInViewCone", Object_IsInViewCone_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004695d0 (stack: owner, input flags pointer; ret 8; 3078 bytes of code + a 10-entry jump table at 0x0046a1d8).
// Reached only by the tail jump of 0x00493130 (pub screen vtable 0x0058fc98 slot 1, the per-frame input handler
// the UI manager 0x004915b0 calls with (mgr, &mgr+0xec)), which replaces the first argument with game+0xa74 (the pub
// controller: layout in src/game/game_04.cpp, opened by 0x00467fe0). Strings "ui_pub_exit/pickup/putdown/move_1/
// move_2", "pui_pub", "ref_pub_interior": the pub screen's update (guess name). Steps:
//  - dt = the frame time 0x00604a90 (kept in the owner argument's slot); with a pub (+0x8c): the shown pool +0x14c
//    runs down to the pub's health pool (0x0046a6a0, st0) at pub+0x474 * pub+0x470 * dt per frame (pool bar
//    (+0x84)+0x4a0 = +0x14c / pub+0x470 first), and the shown health +0x150 up to +0x98 at max health (0x004555c0,
//    st0) * pub+0x474 * dt (health bar (+0x88)+0x4a0 = +0x150 / max health first);
//  - state +0xa8 (0..9, else skipped): 0/5 place the bomb object (+0xac / +0xb4) at (0.5, 0.5, 0) (0x00457870, al =
//    0), shown, bit 8 cleared, anchored at "ref_pub_interior" (0x00463d20), timer +0xa4 = now (0x00604a98), next
//    state; 1/6 grow the object to scale 3 over 0.15 s (Object_SetScale 0x004535d0, scale 1 + 2 * t * 6.67) then
//    show +0xb0 / +0xb8 (6 keeps scale z = 1); 2/7 wait for input bit 0x10 (hide +0xb0 / +0xb8, timer, next state)
//    while bit 0x20 exits (sound "ui_pub_exit", 0x00490b60 pops the screen); 3/8 shrink back to 1 over 0.15 s, then
//    bit 8 and a move (0x00464c60) to marker +0x6c / the first free +0x44 marker; 4 waits for the move (0x00464c90)
//    to end, then marker kind 11 (0x00469450), hidden, state 5 when a special weapon +0xa0 is pending else -1;
//    9 waits likewise, then 0x00469270(special weapon), hidden, state -1;
//  - state -1 with byte +0xbc (new weapons to see): +0xc0 shown; bit 0x10 hides it, selects the +0x58 marker (+0x70),
//    clears +0xbc, moves the cursor +0x7c there (0x00464b10) and shows +0xc4; else bit 0x20 exits;
//  - state -1 otherwise, with a selection +0x70: direction bits 4/8/1/2 pick the neighbour id +0x47c/+0x480/+0x484/
//    +0x488 (0x00463b90 "pui_pub"); without one, the mouse (0x00491380) picks the object under the cursor
//    (0x00463a00, also stored at +0x78) unless it is the cursor's own anchor or, while carrying (+0x74), not a kind-0
//    slot. A new target plays "ui_pub_move_1" (same kind) or "_2", moves the cursor and becomes +0x70. Then the cursor
//    and the markers' objects advance their moves (0x00464c90) and the 0x1c objects at +0xd0 show only the one whose
//    index is the selection's +0x4a8 (when it holds an object). Bit 0x10 on a kind-2 marker (shop) buys its weapon
//    unless held (0x004a1600 list): into the first free weapon slot +0x38.. ("ui_pub_pickup"; pub byte +0x460+id
//    cleared, 0x004a0f70 / 0x004a10c0 equip), or, all three full, it is carried (+0x74) and the cursor goes to slot
//    3; bit 0x10 on a kind-0 slot while carrying swaps the two ("ui_pub_putdown"). Bit 0x20 exits on kinds 1..2, or
//    drops the carried item on kind 0.
// The return value (eax) is not reproduced (0): its only caller returns it to 0x004915b0, which is called by
// Game_Update.
namespace
{
inline void PubExitScreen()
{
    PlayUiSound(0x005a0848);  // "ui_pub_exit"
    orig::fn_00490b60(D(G(), 0xab4));
}
// States 0 / 5: place the object at owner+off.
void PubPlaceObject(uint8_t* o, uint32_t off, uint32_t* frame)
{
    if (!D(o, off))
        return;
    frame[1] = 0x3f000000;
    frame[2] = 0x3f000000;
    frame[3] = 0;
    orig::fn_00457870(0, u(&frame[1]), D(o, off), 0, 0, 0);
    D(B(D(o, off)), 0x474) |= 4;
    D(B(D(o, off)), 0x474) &= 0xfffffff7u;
    const uint32_t ecxValue = D(B(D(o, off)), 0x474);  // ecx at the call (unused by 0x00463d20)
    D(B(D(o, off)), 0x468) = orig::fn_00463d20(ecxValue, 0x005a0b8c, D(G(), 0xa70));
    D(o, 0xa4) = GameGlobal<uint32_t>(0x00604a98);
}
// The scale ramps of states 1/6 (grow: 1 + 2 * e * 6.67) and 3/8 (shrink: 1 + 2 * (1 - e * 6.67)); z stays frame[3].
void PubSetRampScale(uint32_t obj, float e, bool grow, uint32_t* frame)
{
    float s = e * K(0x005bcf30);
    if (!grow)
        s = K(0x005bcb2c) - s;
    s = s + s;
    s = s + K(0x005bcb2c);
    frame[1] = f2u(s);
    frame[2] = f2u(s);
    if (obj)
        orig::fn_004535d0(u(&frame[1]), obj);
}
// States 2 / 7: wait for the confirm / exit bits.
void PubWaitInput(uint8_t* o, const uint8_t* input, uint32_t off, uint32_t next)
{
    if (input[0] & 0x10)
    {
        const uint32_t obj = D(o, off);
        D(o, 0xa8) = next;
        if (obj)
            D(B(obj), 0x474) &= 0xfffffffbu;
        D(o, 0xa4) = GameGlobal<uint32_t>(0x00604a98);
    }
    if (input[0] & 0x20)
        PubExitScreen();
}
}  // namespace

uint32_t PubScreen_HandleInput(uint8_t* o, const uint8_t* input)
{
    const float dt = GameGlobal<float>(0x00604a90);
    uint32_t frame[4] = {};  // the original's 0x10-byte local block (+0 cursor x, +4..+0xc a vector)
    uint32_t slot14 = 0;     // the owner argument's slot: dt, then the cursor y

    // Health pool and health bars.
    uint32_t pub = D(o, 0x8c);
    if (pub)
    {
        if (!(CallSt0Eax(0x0046a6a0, pub) == F(o, 0x14c)))
        {
            F(B(D(o, 0x84)), 0x4a0) = F(o, 0x14c) / F(B(pub), 0x470);
            pub = D(o, 0x8c);
            float v = F(B(pub), 0x474) * F(B(pub), 0x470);
            v = v * dt;
            v = F(o, 0x14c) - v;
            F(o, 0x14c) = v;
            if (v < CallSt0Eax(0x0046a6a0, pub))
                F(o, 0x14c) = CallSt0Eax(0x0046a6a0, pub);
        }
        uint8_t* g = G();
        if (!(F(o, 0x150) == F(o, 0x98)))
        {
            F(B(D(o, 0x88)), 0x4a0) = F(o, 0x150) / orig::fn_004555c0(D(g, 0x748));
            float v = orig::fn_004555c0(D(g, 0x748)) * F(B(D(o, 0x8c)), 0x474);
            v = v * dt;
            v = v + F(o, 0x150);
            F(o, 0x150) = v;
            if (v > F(o, 0x98))
                D(o, 0x150) = D(o, 0x98);
        }
    }

    // The bomb / special weapon presentation states.
    switch (D(o, 0xa8))
    {
    case 0:
        PubPlaceObject(o, 0xac, frame);
        D(o, 0xa8) = 1;
        break;
    case 1:
    case 6:
    {
        const uint32_t obj = D(o, D(o, 0xa8) == 1 ? 0xac : 0xb4);
        const bool first = D(o, 0xa8) == 1;
        const float e = GameGlobal<float>(0x00604a98) - F(o, 0xa4);
        frame[3] = 0x3f800000;
        if (e <= K(0x005bcedc))
        {
            PubSetRampScale(obj, e, true, frame);
            break;
        }
        D(o, 0xa8) = first ? 2 : 7;
        frame[1] = 0x40400000;
        frame[2] = 0x40400000;
        if (first)
            frame[3] = 0x40400000;
        if (obj)
            orig::fn_004535d0(u(&frame[1]), obj);
        const uint32_t next = D(o, first ? 0xb0 : 0xb8);
        if (next)
            D(B(next), 0x474) |= 4;
        break;
    }
    case 2:
        PubWaitInput(o, input, 0xb0, 3);
        break;
    case 7:
        PubWaitInput(o, input, 0xb8, 8);
        break;
    case 3:
    case 8:
    {
        const bool first = D(o, 0xa8) == 3;
        const uint32_t obj = D(o, first ? 0xac : 0xb4);
        const float e = GameGlobal<float>(0x00604a98) - F(o, 0xa4);
        frame[3] = 0x3f800000;
        if (e <= K(0x005bcedc))
        {
            PubSetRampScale(obj, e, false, frame);
            break;
        }
        D(o, 0xa8) = first ? 4 : 9;
        frame[1] = 0x3f800000;
        frame[2] = 0x3f800000;
        if (!obj)
            break;
        orig::fn_004535d0(u(&frame[1]), obj);
        if (first)
        {
            D(B(D(o, 0xac)), 0x474) |= 8;
            orig::fn_00464c60(D(o, 0xac), D(B(D(o, 0x6c)), 0x468));
        }
        else
        {
            D(B(D(o, 0xb4)), 0x474) |= 8;
            for (uint32_t i = 0; i < 5; ++i)
            {
                if (!D(B(D(o, 0x44 + i * 4)), 0x4a0))
                {
                    orig::fn_00464c60(D(o, 0xb4), D(B(D(o, 0x44 + i * 4)), 0x468));
                    break;
                }
            }
        }
        break;
    }
    case 4:
        if (D(o, 0xac))
        {
            orig::fn_00464c90(D(o, 0xac));
            if (D(B(D(o, 0xac)), 0x460) == 0xffffffffu)
            {
                orig::fn_00469450(0xb, u(o));
                D(B(D(o, 0xac)), 0x474) &= 0xfffffffbu;
                D(o, 0xa8) = D(o, 0xa0) != 0xffffffffu ? 5u : 0xffffffffu;
            }
        }
        else
        {
            orig::fn_00469450(0xb, u(o));
            D(o, 0xa8) = D(o, 0xa0) != 0xffffffffu ? 5u : 0xffffffffu;
        }
        break;
    case 5:
        PubPlaceObject(o, 0xb4, frame);
        D(o, 0xa8) = 6;
        break;
    case 9:
        if (D(o, 0xb4))
        {
            orig::fn_00464c90(D(o, 0xb4));
            if (D(B(D(o, 0xb4)), 0x460) == 0xffffffffu)
            {
                orig::fn_00469270(u(o), D(o, 0xa0));
                D(B(D(o, 0xb4)), 0x474) &= 0xfffffffbu;
                D(o, 0xa8) = 0xffffffffu;
            }
        }
        else
        {
            orig::fn_00469270(u(o), D(o, 0xa0));
            D(o, 0xa8) = 0xffffffffu;
        }
        break;
    default:
        break;
    }

    if (D(o, 0xa8) != 0xffffffffu)
        return 0;

    // New weapons notification.
    if (Bt(o, 0xbc))
    {
        if (D(o, 0xc0))
            D(B(D(o, 0xc0)), 0x474) |= 4;
        const uint32_t in = *reinterpret_cast<const uint32_t*>(input);
        if (in & 0x10)
        {
            if (D(o, 0xc0))
                D(B(D(o, 0xc0)), 0x474) &= 0xfffffffbu;
            const uint32_t m = D(o, 0x58);
            const uint32_t cursor = D(o, 0x7c);
            D(o, 0x70) = m;
            Bt(o, 0xbc) = 0;
            D(B(cursor), 0x46c) = D(B(m), 0x46c);
            D(B(D(o, 0x7c)), 0x468) = D(B(D(o, 0x70)), 0x468);
            orig::fn_00464b10(D(o, 0x7c));
            if (D(o, 0xc4))
                D(B(D(o, 0xc4)), 0x474) |= 4;
            return 0;
        }
        if (in & 0x20)
            PubExitScreen();
        return 0;
    }

    // Moving the cursor.
    uint8_t* sel = B(D(o, 0x70));
    if (!sel)
        return 0;
    const uint32_t in = *reinterpret_cast<const uint32_t*>(input);
    uint32_t target = 0;
    bool haveDirection = true;
    uint32_t id = 0;
    if (in & 4)
        id = D(sel, 0x47c);
    else if (in & 8)
        id = D(sel, 0x480);
    else if (in & 1)
        id = D(sel, 0x484);
    else if (in & 2)
        id = D(sel, 0x488);
    else
        haveDirection = false;
    bool move = false;
    if (haveDirection)
    {
        target = orig::fn_00463b90(0x005a03e8, 0xffffffffu, D(G(), 0xa70), id);  // "pui_pub"
        move = target != 0;
    }
    if (!move)
    {
        if (static_cast<uint8_t>(orig::fn_00491380(D(G(), 0xab4), u(&frame[0]), u(&slot14), 0)))
        {
            target = orig::fn_00463a00(D(G(), 0xa70), frame[0], slot14, 0);
            D(o, 0x78) = target;
            if (target && D(B(target), 0x468) != D(B(D(o, 0x7c)), 0x468) &&
                (D(o, 0x74) == 0 || D(B(target), 0x4a4) == 0))
                move = true;
        }
    }
    if (move)
    {
        const bool sameKind = D(B(target), 0x4a4) == D(B(D(o, 0x70)), 0x4a4);
        PlayUiSound(sameKind ? 0x005a0838 : 0x005a0828);  // "ui_pub_move_1" / "ui_pub_move_2"
        orig::fn_00464c60(D(o, 0x7c), D(B(target), 0x468));
        D(o, 0x70) = target;
    }
    orig::fn_00464c90(D(o, 0x7c));
    for (uint32_t i = 0; i < 0xe; ++i)
    {
        const uint32_t m = D(o, i * 4);
        if (m && D(B(m), 0x4a0))
            orig::fn_00464c90(D(B(m), 0x4a0));
    }
    for (uint32_t i = 0; i < 0x1c; ++i)
    {
        const uint32_t obj = D(o, 0xd0 + i * 4);
        if (!obj)
            continue;
        uint8_t* s = B(D(o, 0x70));
        const bool on = D(s, 0x4a0) != 0 && D(s, 0x4a8) == i;
        D(B(obj), 0x474) = on ? (D(B(obj), 0x474) | 4) : (D(B(obj), 0x474) & 0xfffffffbu);
    }

    // Confirm: buy into a weapon slot, or swap with the carried item.
    if (input[0] & 0x10)
    {
        uint8_t* s = B(D(o, 0x70));
        const uint32_t kind = D(s, 0x4a4);
        if (kind == 2)
        {
            if (D(s, 0x4a0))
            {
                uint8_t* g = G();
                const uint32_t* held = reinterpret_cast<const uint32_t*>(orig::fn_004a1600(D(g, 0x748) + 0xc24));
                uint32_t c = 0;
                for (; c < 3; ++c)
                    if (held[c] != 0xffffffffu && held[c] == D(s, 0x4a8))
                        break;
                if (c >= 3)
                {
                    uint32_t k = 0;
                    for (; k < 3; ++k)
                        if (!D(B(D(o, 0x38 + k * 4)), 0x4a0))
                            break;
                    VCall<uint32_t>(B(D(g, 0x2c)), 0xd0 / 4, 0x005a0808u, 0u, 0u, 0u, 0xffffffffu, 0u);  // pickup
                    if (k < 3)
                    {
                        const uint32_t slotOff = 0x38 + k * 4;
                        D(B(D(o, slotOff)), 0x4a0) = D(B(D(o, 0x70)), 0x4a0);
                        D(B(D(o, slotOff)), 0x4a8) = D(B(D(o, 0x70)), 0x4a8);
                        uint8_t* slot = B(D(o, slotOff));
                        orig::fn_00464c60(D(slot, 0x4a0), D(slot, 0x468));
                        if (D(o, 0x8c))
                            Bt(B(D(o, 0x8c)), 0x460 + D(B(D(o, slotOff)), 0x4a8)) = 0;
                        D(B(D(o, 0x70)), 0x4a0) = 0;
                        D(B(D(o, 0x70)), 0x4a8) = 0xffffffffu;
                        orig::fn_004a0f70(D(B(D(o, slotOff)), 0x4a8), 0xffffffffu, D(G(), 0x748) + 0xc24);
                        orig::fn_004a10c0(D(G(), 0x748) + 0xc24, D(B(D(o, slotOff)), 0x4a8));
                    }
                    else
                    {
                        D(o, 0x74) = D(o, 0x70);
                        orig::fn_00464c60(D(o, 0x7c), D(B(D(o, 0x40)), 0x468));
                        D(o, 0x70) = D(o, 0x40);
                        if (D(o, 0xc4))
                            D(B(D(o, 0xc4)), 0x474) &= 0xfffffffbu;
                        if (D(o, 0xc8))
                            D(B(D(o, 0xc8)), 0x474) |= 4;
                    }
                }
            }
        }
        else if (kind == 0 && D(o, 0x74))
        {
            uint8_t* carried = B(D(o, 0x74));
            const uint32_t carriedUi = D(carried, 0x4a0);
            const uint32_t carriedKind = D(carried, 0x4a8);
            const uint32_t r = orig::fn_00464c60(carriedUi, D(s, 0x468));  // returns its eax (the object)
            D(B(D(o, 0x74)), 0x4a0) = D(B(D(o, 0x70)), 0x4a0);
            D(B(D(o, 0x74)), 0x4a8) = D(B(D(o, 0x70)), 0x4a8);
            D(B(D(o, 0x70)), 0x4a0) = r;
            D(B(D(o, 0x70)), 0x4a8) = carriedKind;
            D(B(D(B(D(o, 0x74)), 0x4a0)), 0x468) = D(B(D(o, 0x74)), 0x468);
            orig::fn_00464b10(D(B(D(o, 0x74)), 0x4a0));
            if (D(o, 0x8c))
            {
                Bt(B(D(o, 0x8c)), 0x460 + carriedKind) = 0;
                Bt(B(D(o, 0x8c)), 0x460 + D(B(D(o, 0x74)), 0x4a8)) = 1;
            }
            orig::fn_004a0f70(carriedKind, D(B(D(o, 0x74)), 0x4a8), D(G(), 0x748) + 0xc24);
            orig::fn_004a10c0(D(G(), 0x748) + 0xc24, carriedKind);
            const uint32_t c8 = D(o, 0xc8);
            D(o, 0x74) = 0;
            if (c8)
                D(B(c8), 0x474) &= 0xfffffffbu;
            if (D(o, 0xc4))
                D(B(D(o, 0xc4)), 0x474) |= 4;
            PlayUiSound(0x005a0818);  // "ui_pub_putdown"
        }
    }

    // Exit / drop.
    if (!(input[0] & 0x20))
        return 0;
    const int32_t kind = static_cast<int32_t>(D(B(D(o, 0x70)), 0x4a4));
    if (kind != 0)
    {
        if (kind <= 0 || kind > 2)
            return 0;
        PubExitScreen();
        return 0;
    }
    if (!D(o, 0x74))
    {
        PubExitScreen();
        return 0;
    }
    const uint32_t c8 = D(o, 0xc8);
    D(o, 0x74) = 0;
    if (c8)
        D(B(c8), 0x474) &= 0xfffffffbu;
    if (D(o, 0xc4))
        D(B(D(o, 0xc4)), 0x474) |= 4;
    return 0;
}
static uint32_t PubScreen_HandleInput_Abi(uint32_t a0, uint32_t a1)
{
    return PubScreen_HandleInput(B(a0), B(a1));
}
static __declspec(naked) void PubScreen_HandleInput_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        call PubScreen_HandleInput_Abi
        add esp, 8
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret 8
    }
}
HOOK(0x004695d0, "PubScreen_HandleInput", PubScreen_HandleInput_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0046e2f0 (ecx = object; plain ret). Reached by the tail jump at 0x004195d9 of an untracked function at 0x004193a0
// (also missing from functions.csv; referenced by code at 0x0041a14a / 0x0041a274). Unless +0x50 == 0.0: v = +0x4c /
// +0x50, and +8 = v when v < +8 (a minimum). The return value (eax from fnstsw) is not reproduced: 0.
uint32_t fn_0046e2f0(uint8_t* obj)
{
    if (!(F(obj, 0x50) == K(0x0056c05c)))
    {
        const float v = F(obj, 0x4c) / F(obj, 0x50);
        if (v < F(obj, 8))
            F(obj, 8) = v;
    }
    return 0;
}
static uint32_t fn_0046e2f0_Abi(uint32_t ecx)
{
    return fn_0046e2f0(B(ecx));
}
static __declspec(naked) void fn_0046e2f0_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push ecx
        call fn_0046e2f0_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x0046e2f0, "fn_0046e2f0", fn_0046e2f0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Destructors reached only from the C++ exception-unwind funclets at 0x0056a8xx (the __CxxFrameHandler state tables
// of their constructors); never on the normal path.

// 0x004e0a00 (eax = object; plain ret). Unwind action of the constructor FUN_004e0950 (funclet 0x0056a8d0): vtable =
// 0x00589bbc (an abstract base: all slots _purecall 0x005381db), then FUN_004e0600(object) (stack, callee pops).
// Returns its eax.
uint32_t fn_004e0a00(uint8_t* obj)
{
    D(obj, 0) = 0x00589bbc;
    return orig::fn_004e0600(u(obj));
}
static uint32_t fn_004e0a00_Abi(uint32_t eax)
{
    return fn_004e0a00(B(eax));
}
static __declspec(naked) void fn_004e0a00_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push eax
        call fn_004e0a00_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004e0a00, "fn_004e0a00", fn_004e0a00_Entry, nullptr);

// 0x004e0b10 (eax = pointer; the callers also push a source file name and line, which it does not read, and pop them
// themselves; plain ret). The debug operator delete (guess name: the placement delete of the file/line operator new):
// PMS_heapFree 0x004e08d0(ecx = the heap 0x0063a210; pointer). Called by 10 unwind funclets (0x0056a8bd ...). Returns
// its eax.
uint32_t PMS_DebugDelete(uint32_t p)
{
    return orig::fn_004e08d0(0x0063a210, p);
}
static uint32_t PMS_DebugDelete_Abi(uint32_t eax)
{
    return PMS_DebugDelete(eax);
}
static __declspec(naked) void PMS_DebugDelete_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push eax
        call PMS_DebugDelete_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004e0b10, "PMS_DebugDelete", PMS_DebugDelete_Entry, nullptr);

// 0x004e7330 (eax = list iterator; plain ret). Unwind action (funclets 0x0056a850 / 0x0056aab0 / 0x0056aab8) of a
// PMS_List iterator (src/engine/engine_02.cpp): releases the current node's lock, node = +4, node+0x18 -= 1 when
// there is one. Returns the node.
uint32_t PMS_ListIterator_Release(uint8_t* it)
{
    const uint32_t node = D(it, 4);
    if (node)
        --D(B(node), 0x18);
    return node;
}
static uint32_t PMS_ListIterator_Release_Abi(uint32_t eax)
{
    return PMS_ListIterator_Release(B(eax));
}
static __declspec(naked) void PMS_ListIterator_Release_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push eax
        call PMS_ListIterator_Release_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004e7330, "PMS_ListIterator_Release", PMS_ListIterator_Release_Entry, nullptr);

// 0x004fb800 (ecx = object; plain ret). Unwind action (funclet 0x0056a8f0): the non-deleting destructor of the class
// whose scalar deleting destructor is fn_004fb7c0 (src/renderer/auto_004fb170.cpp): vtable = 0x0058a204, then unlinks
// the object (+4 = a, +8 = b: a+8 = b when a is set; *b = a). Returns b.
uint32_t fn_004fb800(uint8_t* obj)
{
    const uint32_t a = D(obj, 4);
    D(obj, 0) = 0x0058a204;
    if (a)
        D(B(a), 8) = D(obj, 8);
    const uint32_t b = D(obj, 8);
    D(B(b), 0) = D(obj, 4);
    return b;
}
static uint32_t fn_004fb800_Abi(uint32_t ecx)
{
    return fn_004fb800(B(ecx));
}
static __declspec(naked) void fn_004fb800_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push ecx
        call fn_004fb800_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004fb800, "fn_004fb800", fn_004fb800_Entry, nullptr);

// 0x004fba10 (ecx = object; plain ret; eax preserved). Unwind action (funclet 0x0056a870): destructor of the singleton
// whose instance pointer is 0x0063a0b8 (created by fn_004fba30): clears the pointer when it is this object, vtable =
// 0x0058a1f0.
void fn_004fba10(uint8_t* obj)
{
    const bool current = GameGlobal<uint32_t>(0x0063a0b8) == u(obj);
    D(obj, 0) = 0x0058a1f0;
    if (current)
        GameGlobal<uint32_t>(0x0063a0b8) = 0;
}
static void fn_004fba10_Abi(uint32_t ecx)
{
    fn_004fba10(B(ecx));
}
static __declspec(naked) void fn_004fba10_Entry()
{
    __asm {
        push eax
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push ecx
        call fn_004fba10_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        pop eax
        ret
    }
}
HOOK(0x004fba10, "fn_004fba10", fn_004fba10_Entry, nullptr);

// 0x00500fe0 (eax = wave file; plain ret). Unwind action (funclet 0x0056aa50) of WaveFile_Init's frame object: closes
// the file handle +0xa8 through the CloseHandle import slot 0x0056b084 (as src/audio/audio_02.cpp). Returns its
// result.
uint32_t WaveFile_CloseHandle(uint8_t* wave)
{
    return reinterpret_cast<uint32_t(__stdcall*)(uint32_t)>(GameGlobal<uint32_t>(0x0056b084))(D(wave, 0xa8));
}
static uint32_t WaveFile_CloseHandle_Abi(uint32_t eax)
{
    return WaveFile_CloseHandle(B(eax));
}
static __declspec(naked) void WaveFile_CloseHandle_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push eax
        call WaveFile_CloseHandle_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x00500fe0, "WaveFile_CloseHandle", WaveFile_CloseHandle_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Animation curve helpers (callers in the anim samplers 0x004ebb50..0x004ebf40; src/anim/anim_02.cpp calls them
// through asm shims).

// 0x004eb930 (eax = argument block; plain ret; returns eax = the block). Cubic Bezier evaluation of n components:
// block +0 = header h (+0/+4 time range, +0x10 key A, +0x14 key B, +0x1c s, +0x20 r (1 - s, guess)), +4 P0, +8 T0,
// +0xc T1, +0x10 P1, +0x14 out, +0x18 n. When key A's flags (+4 & 0x1c00) are 0x800 or key B's (+4 & 0x380) are 0x100
// (step keys, guess), out = P1 when s is exactly 1.0 (bit compare) else P0. Otherwise h3 = (B.time - A.time, wrapped
// by the range when negative) * 1/3 and out[i] = ((((h3 * T0 + P0) * (s * 3) + P0 * r) * r + (h3 * T1 + P1) *
// (s * s * 3)) * r) + P1 * s * (s * s), in that operation order.
uint32_t CurveEval_Bezier(uint8_t* blk)
{
    uint8_t* h = B(D(blk, 0));
    const float s2 = F(h, 0x1c) * F(h, 0x1c);
    uint8_t* keyA = B(D(h, 0x10));
    bool copy = (D(keyA, 4) & 0x1c00) == 0x800;
    if (!copy)
    {
        uint8_t* keyB = B(D(h, 0x14));
        copy = (D(keyB, 4) & 0x380) == 0x100;
        if (!copy)
        {
            int32_t dt = static_cast<int32_t>(D(keyB, 0) - D(keyA, 0));
            if (dt < 0)
                dt = static_cast<int32_t>(D(h, 4) - D(h, 0) + static_cast<uint32_t>(dt));
            const float h3 = static_cast<float>(dt) * K(0x005bcc60);
            if (D(blk, 0x18) == 0)
                return u(blk);
            const float s2x3 = s2 * K(0x005bcb7c);
            for (uint32_t i = 0; i < D(blk, 0x18); ++i)
            {
                const float* T0 = reinterpret_cast<const float*>(D(blk, 8));
                const float* P0 = reinterpret_cast<const float*>(D(blk, 4));
                const float* P1 = reinterpret_cast<const float*>(D(blk, 0x10));
                const float* T1 = reinterpret_cast<const float*>(D(blk, 0xc));
                float* out = reinterpret_cast<float*>(D(blk, 0x14));
                float x = h3 * T0[i];
                x = x + P0[i];
                const float s3 = F(h, 0x1c) * K(0x005bcb7c);
                x = x * s3;
                const float pr = P0[i] * F(h, 0x20);
                x = x + pr;
                x = x * F(h, 0x20);
                float w = h3 * T1[i];
                w = w + P1[i];
                w = w * s2x3;
                x = x + w;
                x = x * F(h, 0x20);
                float v = P1[i] * F(h, 0x1c);
                v = v * s2;
                x = x + v;
                out[i] = x;
            }
            return u(blk);
        }
    }
    for (uint32_t i = 0; i < D(blk, 0x18); ++i)
    {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(D(h, 0x1c) == 0x3f800000u ? D(blk, 0x10) : D(blk, 4));
        reinterpret_cast<uint32_t*>(D(blk, 0x14))[i] = src[i];
    }
    return u(blk);
}
static uint32_t CurveEval_Bezier_Abi(uint32_t eax)
{
    return CurveEval_Bezier(B(eax));
}
static __declspec(naked) void CurveEval_Bezier_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push eax
        call CurveEval_Bezier_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004eb930, "CurveEval_Bezier", CurveEval_Bezier_Entry, nullptr);

// 0x004eba50 (cdecl (t, a, b); caller pops; returns st0). Ease in / ease out time warp (guess name): t itself when t
// is 0 or 1 or a + b == 0; a and b are scaled by 1 / (a + b) when a + b > 1; k = 1 / (2 - a - b); t < a: k / a * t * t;
// t < 1 - b: (2t - a) * k; else 1 - k / b * (1 - t)^2.
float CurveParam_Ease(float t, float a, float b)
{
    const float s = a + b;
    if (t == K(0x0056c05c))
        return t;
    if (t == K(0x005bcb2c))
        return t;
    if (static_cast<double>(s) == GameGlobal<double>(0x005bcb38))
        return t;
    if (s > K(0x005bcb2c))
    {
        const float inv = K(0x005bcb2c) / s;
        a = a * inv;
        b = inv * b;
    }
    float d = K(0x005bcb80) - a;
    d = d - b;
    const float k = K(0x005bcb2c) / d;
    if (t < a)
    {
        float r = k / a;
        r = r * t;
        return r * t;
    }
    if (K(0x005bcb2c) - b > t)
    {
        float r = t + t;
        r = r - a;
        return r * k;
    }
    const float v = K(0x005bcb2c) - t;
    float r = k / b;
    r = r * v;
    r = r * v;
    return K(0x005bcb2c) - r;
}
static float CurveParam_Ease_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    float t, a, b;
    std::memcpy(&t, &a0, 4);
    std::memcpy(&a, &a1, 4);
    std::memcpy(&b, &a2, 4);
    return CurveParam_Ease(t, a, b);
}
static __declspec(naked) void CurveParam_Ease_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 36]
        push dword ptr [esp + 36]
        push dword ptr [esp + 36]
        call CurveParam_Ease_Abi
        add esp, 12
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004eba50, "CurveParam_Ease", CurveParam_Ease_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Quaternion helpers (callers: the anim samplers 0x004ebdc0..0x004ebf40).

// 0x004f15a0 (ecx = q1, edx = q0, esi = out; stack: t (float); caller pops; plain ret). Spherical interpolation
// without the shortest-arc sign flip (weights from fpatan / fsin): d = q0.q1 (x, z, y, w order); d + 1 <= 0.0001 ->
// out = q1; 1 - d <= 0.0001 -> linear weights (1 - t, t); else a = atan2(sqrt(1 - d*d), d), w0 = sin((1 - t) a) /
// sin a, w1 = sin(a t) / sin a; out = q0 * w0 + q1 * w1. Returns eax = q1.z bits on the copy path (cheap); the other
// paths leave fnstsw garbage in eax (0 here).
uint32_t Quat_SlerpRaw(const float* q1, const float* q0, float* out, float t)
{
    float d = q0[0] * q1[0];
    d = d + q0[2] * q1[2];
    d = d + q0[1] * q1[1];
    d = d + q1[3] * q0[3];
    if (!(d + K(0x005bcb2c) > K(0x005bcbb0)))
    {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(q1);
        uint32_t* dst = reinterpret_cast<uint32_t*>(out);
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        return src[2];
    }
    float w0, w1;
    if (!(K(0x005bcb2c) - d > K(0x005bcbb0)))
    {
        w0 = K(0x005bcb2c) - t;
        w1 = t;
    }
    else
    {
        float c = d * d;
        c = K(0x005bcb2c) - c;
        const float sn = std::sqrt(c);
        const float angle = Atan2F(sn, d);
        const float inv = K(0x005bcb2c) / sn;
        float x0 = K(0x005bcb2c) - t;
        x0 = x0 * angle;
        w0 = SinMulF(x0, inv);
        const float x1 = angle * t;
        w1 = SinMulF(x1, inv);
    }
    out[0] = w0 * q0[0] + w1 * q1[0];
    out[1] = w0 * q0[1] + w1 * q1[1];
    out[2] = w0 * q0[2] + w1 * q1[2];
    out[3] = w1 * q1[3] + w0 * q0[3];
    return 0;
}
static uint32_t Quat_SlerpRaw_Abi(uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0)
{
    float t;
    std::memcpy(&t, &a0, 4);
    return Quat_SlerpRaw(reinterpret_cast<const float*>(ecx), reinterpret_cast<const float*>(edx),
                         reinterpret_cast<float*>(esi), t);
}
static __declspec(naked) void Quat_SlerpRaw_Entry()
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
        push edx
        push ecx
        call Quat_SlerpRaw_Abi
        add esp, 16
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004f15a0, "Quat_SlerpRaw", Quat_SlerpRaw_Entry, nullptr);

// 0x004f16b0 (ecx = p, edx = q; stack: out, a, b, t; caller pops; plain ret). Squad (guess, from the shape):
// r1 = slerp(ecx p, edx q, t), r2 = slerp(ecx b, edx a, t), out = slerp(ecx r2, edx r1, 2 t (1 - t)), all through
// 0x004f15a0. Frame: 0x20 bytes of locals (r2 at +0, r1 at +0x10). Returns the last slerp's eax.
uint32_t Quat_Squad(const float* p, const float* q, float* out, const float* a, const float* b, float t)
{
    float frame[8] = {};
    Quat_SlerpRaw(p, q, &frame[4], t);
    Quat_SlerpRaw(b, a, &frame[0], t);
    float tt = K(0x005bcb2c) - t;
    tt = tt * t;
    tt = tt + tt;
    return Quat_SlerpRaw(&frame[0], &frame[4], out, tt);
}
static uint32_t Quat_Squad_Abi(uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    float t;
    std::memcpy(&t, &a3, 4);
    return Quat_Squad(reinterpret_cast<const float*>(ecx), reinterpret_cast<const float*>(edx),
                      reinterpret_cast<float*>(a0), reinterpret_cast<const float*>(a1),
                      reinterpret_cast<const float*>(a2), t);
}
static __declspec(naked) void Quat_Squad_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push edx
        push ecx
        call Quat_Squad_Abi
        add esp, 24
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004f16b0, "Quat_Squad", Quat_Squad_Entry, nullptr);

// 0x004f1920 (ecx = out quaternion, edi = pointer to a packed 32-bit quaternion; plain ret; returns eax = out).
// Smallest-three unpacking: bits 0-1 = index of the largest component, set to -1.0 (bit 2) or 1.0; the others, in the
// order of the table 0x0057e7f8 ({1,2,3}, {0,2,3}, {0,1,3}, {0,1,2}), get the signed fields bits 3-12 and 13-22
// (* 1/511, 0x005bcd78) and bits 23-31 (* 1/255, 0x005bcc18); then Quat_Normalize 0x004f12e0 (ecx = edx = out).
uint32_t Quat_Unpack32(float* out, const uint32_t* packed)
{
    const uint32_t v = *packed;
    const uint32_t big = v & 3;
    const uint32_t* order = &GameGlobal<uint32_t>(0x0057e7f8 + big * 12);
    out[big] = (v & 4) ? K(0x005bcb5c) : K(0x005bcb2c);
    const int32_t e0 = static_cast<int32_t>(*packed << 19) >> 22;
    out[order[0]] = static_cast<float>(e0) * K(0x005bcd78);
    const int32_t e1 = static_cast<int32_t>(*packed << 9) >> 22;
    out[order[1]] = static_cast<float>(e1) * K(0x005bcd78);
    const int32_t e2 = static_cast<int32_t>(*packed) >> 23;
    out[order[2]] = static_cast<float>(e2) * K(0x005bcc18);
    orig::fn_004f12e0(u(out), u(out));
    return u(out);
}
static uint32_t Quat_Unpack32_Abi(uint32_t ecx, uint32_t edi)
{
    return Quat_Unpack32(reinterpret_cast<float*>(ecx), reinterpret_cast<const uint32_t*>(edi));
}
static __declspec(naked) void Quat_Unpack32_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push edi
        push ecx
        call Quat_Unpack32_Abi
        add esp, 8
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004f1920, "Quat_Unpack32", Quat_Unpack32_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004a2e10 (cdecl (unused, save slot object); caller pops; returns al = 1). The "yes" callback that fn_004a36f0
// passes to UI_PushYesNo 0x00490910 (push 0x004a2e10 at 0x004a3732, with the slot object as its context): pops the top
// screen (0x00490b60, ecx = game+0xab4), then deletes the slot's files (fn_004a2e40, esi = slot) when there is a slot.
// The upper eax bytes are those of the last call's eax.
uint32_t SaveSlot_OnDeleteConfirmed(uint32_t /*a0*/, uint32_t slot)
{
    uint32_t r = orig::fn_00490b60(D(G(), 0xab4));
    if (slot)
        r = orig::fn_004a2e40(slot);
    return (r & 0xffffff00u) | 1u;
}
static uint32_t SaveSlot_OnDeleteConfirmed_Abi(uint32_t a0, uint32_t a1)
{
    return SaveSlot_OnDeleteConfirmed(a0, a1);
}
static __declspec(naked) void SaveSlot_OnDeleteConfirmed_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        call SaveSlot_OnDeleteConfirmed_Abi
        add esp, 8
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004a2e10, "SaveSlot_OnDeleteConfirmed", SaveSlot_OnDeleteConfirmed_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Bone controller callbacks: cdecl (out transform, in transform, bone, parent bone, skeleton, mode, layer), caller
// pops (see src/anim/anim_02.cpp); the bone's record is skeleton+0x14 -> [bone u16 +0xa * 8], whose +4 is the
// user data given at registration. Each reads only its first five arguments.

// 0x00499670: the turret bone callback registered by fn_00498d60 (pointer at 0x00498f3e; also 0x0049960c):
// fn_00498ff0(eax = user data (the turret), ebx = in; out, bone, parent). Returns its eax.
uint32_t Turret_BoneCallback(uint32_t out, uint32_t in, uint8_t* bone, uint32_t parent, uint8_t* skel)
{
    const uint32_t data = D(B(D(skel, 0x14)), Field<uint16_t>(bone, 0xa) * 8u + 4);
    return orig::fn_00498ff0(data, in, out, u(bone), parent);
}
static uint32_t Turret_BoneCallback_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Turret_BoneCallback(a0, a1, B(a2), a3, B(a4));
}
static __declspec(naked) void Turret_BoneCallback_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        call Turret_BoneCallback_Abi
        add esp, 20
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x00499670, "Turret_BoneCallback", Turret_BoneCallback_Entry, nullptr);

// 0x0049a0b0: the mountable-turret pitch / yaw bone callback registered by fn_00499c40 (pointers at 0x00499ea5 /
// 0x00499f22): the user data is a record {turret, axis byte}; fn_00499fd0(eax = turret, ebx = in; out, bone, parent,
// axis (zero-extended)). Returns its eax.
uint32_t MountedTurret_BoneCallback(uint32_t out, uint32_t in, uint8_t* bone, uint32_t parent, uint8_t* skel)
{
    uint8_t* rec = B(D(B(D(skel, 0x14)), Field<uint16_t>(bone, 0xa) * 8u + 4));
    const uint32_t axis = Bt(rec, 4);
    return orig::fn_00499fd0(D(rec, 0), in, out, u(bone), parent, axis);
}
static uint32_t MountedTurret_BoneCallback_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return MountedTurret_BoneCallback(a0, a1, B(a2), a3, B(a4));
}
static __declspec(naked) void MountedTurret_BoneCallback_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        call MountedTurret_BoneCallback_Abi
        add esp, 20
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x0049a0b0, "MountedTurret_BoneCallback", MountedTurret_BoneCallback_Entry, nullptr);

// 0x0042c080: the bone callback fn_0042c0e0 installs (pointer at 0x0042c340; also 0x00485d23): when the user data is
// the bone itself, out (0x2c bytes) = {0, 0, 0, 0, 0, 0, 1.0, 1.0, 1.0, 1.0, 0} (returns out); otherwise out = in
// (11 dwords; returns the bone).
uint32_t fn_0042c080(uint32_t* out, const uint32_t* in, uint8_t* bone, uint32_t /*parent*/, uint8_t* skel)
{
    if (u(bone) == D(B(D(skel, 0x14)), Field<uint16_t>(bone, 0xa) * 8u + 4))
    {
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
        out[3] = 0;
        out[4] = 0;
        out[5] = 0;
        out[6] = 0x3f800000;
        out[7] = 0x3f800000;
        out[8] = 0x3f800000;
        out[9] = 0x3f800000;
        out[10] = 0;
        return u(out);
    }
    for (uint32_t i = 0; i < 11; ++i)
        out[i] = in[i];
    return u(bone);
}
static uint32_t fn_0042c080_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_0042c080(reinterpret_cast<uint32_t*>(a0), reinterpret_cast<const uint32_t*>(a1), B(a2), a3, B(a4));
}
static __declspec(naked) void fn_0042c080_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        call fn_0042c080_Abi
        add esp, 20
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x0042c080, "fn_0042c080", fn_0042c080_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Havok collision agent create functions of the game's own shapes (cdecl (A, B, input); caller pops). The memory
// comes from the Havok allocator [0x005fed84] vt+0x10(size, 0x15); the u16 at +4 records the size.

// 0x004a8810: registered by registerAgents_speedTrap 0x004a87e0 (pointer at 0x004a87e5). Speed-trap agent:
// SpeedTrapAgent_Construct 0x004a8790(eax = input, ecx = A, ebx = agent; B). Returns its eax (the agent).
uint32_t SpeedTrapAgent_Create(uint32_t a, uint32_t b, uint32_t input)
{
    uint8_t* mem = B(VCall<uint32_t>(B(GameGlobal<uint32_t>(0x005fed84)), 0x10 / 4, 0xcu, 0x15u));
    Field<uint16_t>(mem, 4) = 0xc;
    return orig::fn_004a8790(input, a, u(mem), b);
}
static uint32_t SpeedTrapAgent_Create_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return SpeedTrapAgent_Create(a0, a1, a2);
}
static __declspec(naked) void SpeedTrapAgent_Create_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 36]
        push dword ptr [esp + 36]
        push dword ptr [esp + 36]
        call SpeedTrapAgent_Create_Abi
        add esp, 12
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004a8810, "SpeedTrapAgent_Create", SpeedTrapAgent_Create_Entry, nullptr);

// 0x004a8840: registered by 0x004a87e0 (pointer at 0x004a87f5) for the swapped shape pair: as 0x004a8810 with A and
// B exchanged (ecx = B; A), then vtable = 0x0058e688 (the swapped agent). Returns the agent.
uint32_t SpeedTrapAgent_CreateSwapped(uint32_t a, uint32_t b, uint32_t input)
{
    uint8_t* mem = B(VCall<uint32_t>(B(GameGlobal<uint32_t>(0x005fed84)), 0x10 / 4, 0xcu, 0x15u));
    Field<uint16_t>(mem, 4) = 0xc;
    orig::fn_004a8790(input, b, u(mem), a);
    D(mem, 0) = 0x0058e688;
    return u(mem);
}
static uint32_t SpeedTrapAgent_CreateSwapped_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return SpeedTrapAgent_CreateSwapped(a0, a1, a2);
}
static __declspec(naked) void SpeedTrapAgent_CreateSwapped_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 36]
        push dword ptr [esp + 36]
        push dword ptr [esp + 36]
        call SpeedTrapAgent_CreateSwapped_Abi
        add esp, 12
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004a8840, "SpeedTrapAgent_CreateSwapped", SpeedTrapAgent_CreateSwapped_Entry, nullptr);

// 0x004df500 (reads no arguments; plain ret): the type-0x19 agent create function the physics init registers for
// (1, 0x19), (0x19, 1), (0x19, 0x19) (pointers at 0x00411aef / 0x00411aff / 0x00411b0f; src/physics/physics_01.cpp):
// 0x210 bytes, the Havok agent base constructor FUN_0051a5b0 (ecx = agent), vtable 0x0058b7a8 (Type19Agent: slots
// Type19Agent_DeletingDestructor 0x004df540, Type19Agent_ProcessCollision 0x004df570), +0x200 = 0. Returns the agent.
uint32_t Type19Agent_Create()
{
    uint8_t* mem = B(VCall<uint32_t>(B(GameGlobal<uint32_t>(0x005fed84)), 0x10 / 4, 0x210u, 0x15u));
    Field<uint16_t>(mem, 4) = 0x210;
    orig::fn_0051a5b0(u(mem));
    D(mem, 0) = 0x0058b7a8;
    D(mem, 0x200) = 0;
    return u(mem);
}
static uint32_t Type19Agent_Create_Abi()
{
    return Type19Agent_Create();
}
static __declspec(naked) void Type19Agent_Create_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        call Type19Agent_Create_Abi
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004df500, "Type19Agent_Create", Type19Agent_Create_Entry, nullptr);

// 0x004dfa80 (cdecl (shape, a1, a2, builder); caller pops; plain ret): the display builder for shape type 0x19 that
// Type19Shape_Construct registers (pointer at 0x004dfafe; src/physics/physics_04.cpp): builds the display of the
// wrapped child shape (+0x28) through the Havok display builder FUN_004a7ba0 (ecx = builder; child, a1, a2; callee
// pops). Returns its eax.
uint32_t Type19Shape_BuildDisplay(uint8_t* shape, uint32_t a1, uint32_t a2, uint32_t builder)
{
    return orig::fn_004a7ba0(builder, D(shape, 0x28), a1, a2);
}
static uint32_t Type19Shape_BuildDisplay_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Type19Shape_BuildDisplay(B(a0), a1, a2, a3);
}
static __declspec(naked) void Type19Shape_BuildDisplay_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        call Type19Shape_BuildDisplay_Abi
        add esp, 16
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004dfa80, "Type19Shape_BuildDisplay", Type19Shape_BuildDisplay_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Scene pass world-walk callbacks: 0x0048eb60 calls them cdecl (cell, a1) (`push edx; push ecx; call eax; add esp,
// 8`); both read only the first argument. Registered by the scene pass (src/renderer/scene_pass.cpp) at 0x00489207
// (flag 1) and 0x004891fd (flag 8).

// 0x00448db0 (flag 1; cell = {x, y}). The world grid W = game+0xa60. Outside the grid (x >= W+8 or y >= W+0xc) -> W.
// planes = ScenePass_GetFrustum 0x00488990(eax = game+0xa78, edx = &planes) (count). A draw item source {W+0xf04,
// (W+0xf4c)+0x18, entry, 0, 9999999.0, 0}. For each layer i < (W+0xefc)+0x28 (signed): mesh = [(W+0xf4c)+0x18+i*4]+4;
// for each j < W+0x10: the cell record [(W+0xef4)+i*4] + ((W+0xc * j + y) * W+8 + x) * 8 = {first, count} indexes
// (W+0xef8)[i] -> entry index; entry = mesh+0x84 + index * 0x2c; entries with +0x20 set are queued (ItemList_Add
// 0x0048a0d0, eax = game+0xa78; &source, 0, 0) when their box (+0x24: min +0x24, max +0x30) is absent or passes the
// frustum test: the mask of (1 << count) - 1 planes loses a plane as soon as a corner (8, in x/y/z bit order) has
// z*c + x*a + y*b + d >= 0; visible when the mask becomes 0. Returns W.
uint32_t ScenePass_QueueWorldCell(const uint32_t* cell)
{
    uint8_t* g = G();
    uint8_t* w = B(D(g, 0xa60));
    const uint32_t x = cell[0];
    const uint32_t y = cell[1];
    if (x >= D(w, 8))
        return u(w);
    if (y >= D(w, 0xc))
        return u(w);
    uint32_t planesPtr = 0;
    const uint32_t planeCount = orig::fn_00488990(D(g, 0xa78), u(&planesPtr));
    w = B(D(G(), 0xa60));
    uint32_t source[6];  // the original's [esp+0x30..0x44] (frame offsets)
    source[0] = u(w) + 0xf04;
    source[1] = D(B(D(w, 0xf4c)), 0x18);
    source[2] = 0;
    source[3] = 0;
    source[4] = 0x4b18967f;  // 9999999.0
    source[5] = 0;
    if (static_cast<int32_t>(D(B(D(w, 0xefc)), 0x28)) <= 0)
        return u(w);
    int32_t i = 0;
    do
    {
        uint8_t* mesh = B(D(B(D(B(D(w, 0xf4c)), 0x18 + static_cast<uint32_t>(i) * 4)), 4));
        for (uint32_t j = 0; j < D(w, 0x10); ++j)
        {
            const uint32_t index = (D(w, 0xc) * j + y) * D(w, 8) + x;
            const uint8_t* rec = B(D(B(D(w, 0xef4)), static_cast<uint32_t>(i) * 4) + index * 8);
            const uint32_t first = D(rec, 0);
            const uint32_t end = D(rec, 4) + first;
            for (uint32_t k = first; k < end; ++k)
            {
                w = B(D(G(), 0xa60));
                const uint32_t entryIndex = D(B(D(B(D(w, 0xef8)), static_cast<uint32_t>(i) * 4)), k * 4);
                uint8_t* entry = B(entryIndex * 0x2c + D(mesh, 0x84));
                if (!D(entry, 0x20))
                    continue;
                const uint8_t* box = B(D(entry, 0x24));
                bool visible = true;
                if (box)
                {
                    visible = false;
                    uint32_t mask = (1u << (planeCount & 31)) - 1;
                    for (uint32_t c = 0; c < 8 && !visible; ++c)
                    {
                        const float px = F(box, 0x24 + (c & 1) * 12);
                        const float py = F(box, 0x28 + ((c >> 1) & 1) * 12);
                        const float pz = F(box, 0x2c + ((c >> 2) & 1) * 12);
                        const float* pl = reinterpret_cast<const float*>(planesPtr);
                        uint32_t bit = 1;
                        for (uint32_t p = 0; p < planeCount; ++p, pl += 4, bit <<= 1)
                        {
                            if (!(mask & bit))
                                continue;
                            float d = pz * pl[2];
                            d = d + px * pl[0];
                            d = d + py * pl[1];
                            d = d + pl[3];
                            if (d >= K(0x0056c05c))
                            {
                                mask ^= bit;
                                if (!mask)
                                {
                                    visible = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (visible)
                {
                    source[2] = u(entry);
                    orig::fn_0048a0d0(D(G(), 0xa78), u(source), 0, 0);
                }
            }
            w = B(D(G(), 0xa60));
        }
        w = B(D(G(), 0xa60));
        ++i;
    } while (i < static_cast<int32_t>(D(B(D(w, 0xefc)), 0x28)));
    return u(w);
}
static uint32_t ScenePass_QueueWorldCell_Abi(uint32_t a0)
{
    return ScenePass_QueueWorldCell(reinterpret_cast<const uint32_t*>(a0));
}
static __declspec(naked) void ScenePass_QueueWorldCell_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 28]
        call ScenePass_QueueWorldCell_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x00448db0, "ScenePass_QueueWorldCell", ScenePass_QueueWorldCell_Entry, nullptr);

// 0x00489050 (flag 8; the argument is an object): +0x424 |= 0x80, then a tail jump to the object's vt+0xc0 (the draw,
// e.g. Object_DrawBase 0x00454530 / FxObject_Draw: thiscall without stack arguments, plain ret) with the walker's
// frame intact; recovered as call-then-return. Returns the draw's eax.
uint32_t ScenePass_DrawWorldObject(uint8_t* obj)
{
    D(obj, 0x424) |= 0x80;
    return VCall<uint32_t>(obj, 0xc0 / 4);
}
static uint32_t ScenePass_DrawWorldObject_Abi(uint32_t a0)
{
    return ScenePass_DrawWorldObject(B(a0));
}
static __declspec(naked) void ScenePass_DrawWorldObject_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 28]
        call ScenePass_DrawWorldObject_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x00489050, "ScenePass_DrawWorldObject", ScenePass_DrawWorldObject_Entry, nullptr);
