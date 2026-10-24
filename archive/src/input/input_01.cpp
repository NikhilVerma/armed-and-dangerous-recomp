// Recovered: input batch input_01 (controls 0x00414460-0x00415750, force feedback 0x0041ff60, the platform-independent
// input base class 0x004e48a0-0x004e5750 and the PC DirectInput 8 input class 0x004f7b30-0x004f98b0). Static recovery
// 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified from the disassembly.
//
// Objects (evidence: the disassembly of the functions below):
// - Input (Game+0x30): the PC class has vtable 0x0058a600 (35 slots; constructor 0x004f7bb0, singleton 0x0063a0c0
//   with a reference count at +0x224), its base class vtable 0x0058b1e0 (pure slots = 0x005381db _purecall). Base
//   members: +4 the active rumble list (vtable 0x0058b098), +8 the rumble samples (0x840 bytes each, count +0x1c),
//   +0xc the rumble node pool, +0x10 (0x004e43f0 result), +0x14 the next rumble id, +0x18 initialised, +0x19 rumble
//   enabled, +0x20 the rumble name hash table. PC members: +0x28 the mouse IDirectInputDevice8A, +0x58 the keyboard
//   IDirectInputDevice8A, +0x15c the IDirectInput8A, +0x160 the game controller, +0x218 the window, +0x21c the mouse
//   sensitivity (1.0), +0x220 "exclusive" byte (cursor clipping). Details above the PC functions below.
//   Virtual slots used by the controls (thiscall): +8 (device) -> connected (bool), +0x10 device count, +0x2c (device)
//   -> any input, +0x3c (device) -> any key, +0x68 (device, code, threshold) -> pressed, +0x6c (device, code,
//   threshold) -> held, +0x74 -> not held (0x004e4d40), +0x78 (device, code, a, b, c) -> axis value (float).
// - Controls (Game+0xa80; the actions' names are the "ControlAction*" strings of 0x004155a0): +0 device count, +4 the active device (-1: any), +0xc/+0x10 a bit mask of
//   35 "consumed" actions (cleared once the action is released), +0x14 a lock counter (non-zero: listen to every
//   device), +0x18 cleared by 0x00414580, +0x1c 35 primary bindings, +0xa8 35 secondary bindings (input codes).
// Input codes (0x00414c90 strings "PMS_INPUT_*"): 1..0xed DirectInput keyboard scan codes (PMS_INPUT_VK_*),
// 0x10000.. mouse (PMS_INPUT_MOUSE_UP = 0x10000, PMS_INPUT_MOUSE_DOWN = 0x10001, ...), -0x2e..-1 joystick axes and
// buttons (PMS_INPUT_LAXIS_*, PMS_INPUT_RAXIS_*, PMS_INPUT_DPAD_*, PMS_INPUT_A_BUTTON, ...).
#include <cstring>

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

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
// A float constant of the game's .rdata.
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
// Game+0x30: the input object.
inline void* InputObj()
{
    return Field<void*>(g_game, 0x30);
}
constexpr uint32_t kHeap = 0x0063a210;  // the game heap (PMS_heapAlloc 0x004e07f0 / PMS_heapFree 0x004e08d0)

// fmod(x, y) through the game's _CIfmod (0x00566790: st1 = x, st0 = y, result in st0).
float CIfmod(float x, float y)
{
    float r;
    __asm {
        fld x
        fld y
        mov eax, 0x00566790
        call eax
        fstp r
    }
    return r;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Controls (Game+0xa80)
// ---------------------------------------------------------------------------------------------------------------------

uint32_t Controls_LoadDefaultBindings(uint32_t set, void* controls);

// Enhancement points (hook/enhance/gamepad.cpp), null by default (original behaviour):
// - g_padBinding(action, slot): a third binding column for a game controller (controller codes -0x2e..-1), slots 0
//   and 1; 0 = none. Queried with the two original bindings in 0x00414730 / 0x00414810 / 0x004148f0 / 0x00414a30 /
//   0x00414aa0.
// - g_padPoll(pc): after the controller poll (0x004f8600) in the per-frame update: may fill the controller state.
int32_t (*g_padBinding)(int32_t action, int slot) = nullptr;
void (*g_padPoll)(void* pc) = nullptr;

// 0x00414460 (esi = controls; returns eax = controls): +0 = Input virtual +0x10 (device count), +4 = -1 (any device),
// +0x14, +0x18, +8, +0xc, +0x10 = 0, then the default bindings (0x004144a0 with eax = 0).
void* Controls_Init(void* controls)
{
    Field<uint32_t>(controls, 0) = VCall<uint32_t>(InputObj(), 0x10 / 4);
    Field<int32_t>(controls, 4) = -1;
    Field<uint32_t>(controls, 0x14) = 0;
    Field<uint32_t>(controls, 0x18) = 0;
    Field<uint32_t>(controls, 8) = 0;
    Field<uint32_t>(controls, 0xc) = 0;
    Field<uint32_t>(controls, 0x10) = 0;
    Controls_LoadDefaultBindings(0, controls);
    return controls;
}

// 0x004144a0 (eax = binding set; stack: controls; callee pops 4): clears the 70 binding words (+0x1c..+0x133) and
// copies the default table 0x00588ed8 ({action, primary, secondary} triples, terminated by action 0x23; sets 0x1b0
// bytes apart). A set >= 1 is replaced by 0, so only set 0 is ever used. Returns eax = the last table offset.
uint32_t Controls_LoadDefaultBindings(uint32_t set, void* controls)
{
    if (set >= 1)
        set = 0;
    std::memset(static_cast<uint8_t*>(controls) + 0x1c, 0, 0x46 * 4);
    uint32_t offset = set * 0x1b0;
    for (;;)
    {
        const int32_t action = GameGlobal<int32_t>(0x00588ed8 + offset);
        if (action == 0x23)
            break;
        Field<uint32_t>(controls, 0x1c + action * 4) = GameGlobal<uint32_t>(0x00588edc + offset);
        Field<uint32_t>(controls, 0xa8 + action * 4) = GameGlobal<uint32_t>(0x00588ee0 + offset);
        offset += 0xc;
    }
    return offset;
}

// 0x00414520 (eax = controls): swaps the bindings PMS_INPUT_MOUSE_UP (0x10000) and PMS_INPUT_MOUSE_DOWN (0x10001) in
// both binding tables (invert mouse Y; the codes' names from 0x00414c90). Not executed in recorded runs.
void Controls_InvertMouseY(void* controls)
{
    for (uint32_t action = 0; action < 0x23; action++)
        for (uint32_t table = 0; table < 2; table++)
        {
            uint32_t& code = Field<uint32_t>(controls, 0x1c + action * 4 + table * 0x8c);
            if (code == 0x10000)
                code = 0x10001;
            else if (code == 0x10001)
                code = 0x10000;
        }
}

// 0x00414580 (eax = controls): +0x18 = 0.
void fn_00414580(void* controls)
{
    Field<uint32_t>(controls, 0x18) = 0;
}

// 0x00414590 (eax = controls, cl = increment): the lock counter +0x14 goes up (cl != 0) or down; returns al = counter
// != 0 (the upper bytes of eax keep the controls pointer's).
uint32_t Controls_Lock(void* controls, uint32_t increment)
{
    if (static_cast<uint8_t>(increment))
        Field<int32_t>(controls, 0x14)++;
    else
        Field<int32_t>(controls, 0x14)--;
    return (u(controls) & 0xffffff00u) | (Field<int32_t>(controls, 0x14) != 0 ? 1u : 0u);
}

void Controls_ReleaseConsumed(void* controls);

// 0x004145c0 (edx: passed to 0x004146c0, which does not read it; edi = controls; stack: one unread argument, callee
// pops 4): releases consumed actions (0x004146c0), then finds the active device. Locked (+0x14) or no device (+4 ==
// -1): the first device that is connected (Input +8) with any input (Input +0x2c) becomes +4, Game+0x20 = 0 and +0x18
// = 0 (returns g_game); none: returns the device count. Otherwise the active device: not connected -> 0x004907a0
// (Ui_SetFlag10, eax = Game+0xab4); any input -> Game+0x20 = 0, +0x18 = 0, returns the input value.
uint32_t Controls_Poll(uint32_t edx, void* controls)
{
    (void)edx;
    Controls_ReleaseConsumed(controls);
    void* input;
    if (Field<uint32_t>(controls, 0x14) == 0 && Field<int32_t>(controls, 4) != -1)
    {
        input = InputObj();
        if (!VCall<uint8_t>(input, 8 / 4, Field<uint32_t>(controls, 4)))
            return orig::fn_004907a0(Field<uint32_t>(g_game, 0xab4));
        const uint32_t any = VCall<uint32_t>(InputObj(), 0x2c / 4, Field<uint32_t>(controls, 4));
        if (!any)
            return 0;
        Field<uint32_t>(g_game, 0x20) = 0;
        Field<uint32_t>(controls, 0x18) = 0;
        return any;
    }
    if (Field<uint32_t>(controls, 0) == 0)
        return 0;
    for (uint32_t device = 0;;)
    {
        if (VCall<uint8_t>(InputObj(), 8 / 4, device) && VCall<uint32_t>(InputObj(), 0x2c / 4, device) != 0)
        {
            Field<uint32_t>(g_game, 0x20) = 0;
            Field<uint32_t>(controls, 4) = device;
            Field<uint32_t>(controls, 0x18) = 0;
            return u(g_game);
        }
        device++;
        if (device >= Field<uint32_t>(controls, 0))
            return Field<uint32_t>(controls, 0);
    }
}

// 0x00414680 (eax = controls, ecx = action): marks the action consumed (bit in +0xc..); action 0x23 marks every action
// (both mask words = -1; returns -1). Returns the mask word's address otherwise.
uint32_t Controls_Consume(void* controls, int32_t action)
{
    if (action == 0x23)
    {
        Field<int32_t>(controls, 0xc) = -1;
        Field<int32_t>(controls, 0x10) = -1;
        return 0xffffffffu;
    }
    uint32_t* word = &Field<uint32_t>(controls, 0xc + (action / 32) * 4);
    *word |= 1u << (action & 0x1f);
    return u(word);
}

uint32_t Controls_IsReleased(uint32_t eax, void* controls, int32_t action, uint32_t threshold);

// 0x004146c0 (eax = controls; edx listed by abi.csv but written before any read): for each consumed action (0..0x22)
// that is released on every binding (0x004148f0, al = 0, threshold 0.25), clears its bit. When no bit at or above the
// current one is set in the current word, jumps to the next word plus one (original bug: the next word's first action,
// 0x20, is then skipped).
void Controls_ReleaseConsumed(void* controls)
{
    uint32_t action = 0;
    do
    {
        const uint32_t word = action >> 5;
        const uint32_t bit = 1u << (action & 0x1f);
        const uint32_t mask = Field<uint32_t>(controls, 0xc + word * 4);
        if (bit & mask)
        {
            if (static_cast<uint8_t>(Controls_IsReleased(0, controls, static_cast<int32_t>(action), 0x3e800000u)))
                Field<uint32_t>(controls, 0xc + word * 4) &= ~bit;
        }
        else if (!(mask & (0xffffffffu << (action & 0x1f))))
            action = (action + 0x20) & 0xffffffe0u;
        action++;
    } while (action < 0x23);
}

namespace
{
// The bit test shared by 0x00414730 / 0x00414810 / 0x004148f0: the action's consumed bit.
bool ConsumedBit(void* controls, int32_t action)
{
    return (Field<uint32_t>(controls, 0xc + (action / 32) * 4) & (1u << (action & 0x1f))) != 0;
}

// The controller bindings (g_padBinding) through Input virtual `slot`: any of them true (all = false), or all of
// them true (all = true, for the release test). No controller binding: false (any) / true (all).
bool PadQuery(uint32_t device, int32_t action, uint32_t threshold, int slot, bool all)
{
    if (!g_padBinding)
        return all;
    for (int s = 0; s < 2; s++)
    {
        const int32_t code = g_padBinding(action, s);
        if (!code)
            continue;
        const bool r = VCall<uint8_t>(InputObj(), slot, device, static_cast<uint32_t>(code), threshold) != 0;
        if (r != all)
            return r;
    }
    return all;
}

// The device search shared by 0x00414730 (slot 0x68) and 0x00414810 (slot 0x6c): any device, either binding.
uint32_t QueryAction(void* controls, int32_t action, uint32_t threshold, int slot)
{
    if (Field<uint32_t>(controls, 0x14) != 0 || Field<int32_t>(controls, 4) == -1)
    {
        const uint32_t count = Field<uint32_t>(controls, 0);
        if (count == 0)
            return 0;
        for (uint32_t device = 0; device < Field<uint32_t>(controls, 0); device++)
        {
            if (VCall<uint8_t>(InputObj(), slot, device, Field<uint32_t>(controls, 0x1c + action * 4), threshold) ||
                VCall<uint8_t>(InputObj(), slot, device, Field<uint32_t>(controls, 0xa8 + action * 4), threshold) ||
                PadQuery(device, action, threshold, slot, false))
            {
                Field<uint32_t>(controls, 4) = device;
                return 1;
            }
        }
        return Field<uint32_t>(controls, 0) & 0xffffff00u;  // xor al, al after eax = count
    }
    if (VCall<uint8_t>(InputObj(), slot, Field<uint32_t>(controls, 4), Field<uint32_t>(controls, 0x1c + action * 4),
                       threshold))
        return 1;
    if (VCall<uint8_t>(InputObj(), slot, Field<uint32_t>(controls, 4), Field<uint32_t>(controls, 0xa8 + action * 4),
                       threshold))
        return 1;
    if (PadQuery(Field<uint32_t>(controls, 4), action, threshold, slot, false))
        return 1;
    return 0;
}
}  // namespace

// 0x00414730 (al = respect consumed, ebx = action, esi = controls; stack: threshold (float); callee pops 4): action
// pressed. al != 0 and the action consumed -> 0. Locked or any device: the first device where either binding is
// pressed (Input +0x68) becomes the active device (+4), returns 1. Otherwise only the active device. Returns al.
uint32_t Controls_IsPressed(uint32_t eax, int32_t action, void* controls, uint32_t threshold)
{
    if (static_cast<uint8_t>(eax) && ConsumedBit(controls, action))
        return 0;
    return QueryAction(controls, action, threshold, 0x68 / 4);
}

// 0x00414810 (al = respect consumed, ebx = action, esi = controls; stack: threshold (float); callee pops 4): as
// 0x00414730 with Input +0x6c (held).
uint32_t Controls_IsHeld(uint32_t eax, int32_t action, void* controls, uint32_t threshold)
{
    if (static_cast<uint8_t>(eax) && ConsumedBit(controls, action))
        return 0;
    return QueryAction(controls, action, threshold, 0x6c / 4);
}

// 0x004148f0 (al = respect consumed, esi = controls, edi = action; stack: threshold (float); callee pops 4): action
// released. al != 0 and consumed -> 1. Locked or any device: 1 when on some device both bindings are not held (Input
// +0x74; the active device is not changed). Otherwise the active device's bindings must both be not held.
uint32_t Controls_IsReleased(uint32_t eax, void* controls, int32_t action, uint32_t threshold)
{
    if (static_cast<uint8_t>(eax) && ConsumedBit(controls, action))
        return 1;
    if (Field<uint32_t>(controls, 0x14) != 0 || Field<int32_t>(controls, 4) == -1)
    {
        if (Field<uint32_t>(controls, 0) == 0)
            return 0;
        for (uint32_t device = 0; device < Field<uint32_t>(controls, 0); device++)
        {
            if (VCall<uint8_t>(InputObj(), 0x74 / 4, device, Field<uint32_t>(controls, 0x1c + action * 4), threshold) &&
                VCall<uint8_t>(InputObj(), 0x74 / 4, device, Field<uint32_t>(controls, 0xa8 + action * 4), threshold) &&
                PadQuery(device, action, threshold, 0x74 / 4, true))
                return 1;
        }
        return Field<uint32_t>(controls, 0) & 0xffffff00u;
    }
    if (!VCall<uint8_t>(InputObj(), 0x74 / 4, Field<uint32_t>(controls, 4),
                        Field<uint32_t>(controls, 0x1c + action * 4), threshold))
        return 0;
    if (!VCall<uint8_t>(InputObj(), 0x74 / 4, Field<uint32_t>(controls, 4),
                        Field<uint32_t>(controls, 0xa8 + action * 4), threshold))
        return 0;
    if (!PadQuery(Field<uint32_t>(controls, 4), action, threshold, 0x74 / 4, true))
        return 0;
    return 1;
}

// 0x004149d0 (edi = controls): any key. Locked or any device: the first device with Input +0x3c != 0 becomes the
// active device (returns that value; none: 0). Otherwise Input +0x3c of the active device. Not executed in recorded
// runs.
uint32_t Controls_AnyKey(void* controls)
{
    if (Field<uint32_t>(controls, 0x14) != 0 || Field<int32_t>(controls, 4) == -1)
    {
        if (Field<uint32_t>(controls, 0) == 0)
            return 0;
        for (uint32_t device = 0; device < Field<uint32_t>(controls, 0); device++)
        {
            const uint32_t r = VCall<uint32_t>(InputObj(), 0x3c / 4, device);
            if (r)
            {
                Field<uint32_t>(controls, 4) = device;
                return r;
            }
        }
        return 0;
    }
    return VCall<uint32_t>(InputObj(), 0x3c / 4, Field<uint32_t>(controls, 4));
}

// The larger of `r` and Input +0x78 for the controller bindings (g_padBinding); `r` unchanged without them.
static float PadAxis(void* controls, int32_t action, uint32_t a0, uint32_t a1, uint32_t a2, float r)
{
    if (!g_padBinding)
        return r;
    for (int s = 0; s < 2; s++)
        if (const int32_t code = g_padBinding(action, s))
        {
            const float v = VCall<float>(InputObj(), 0x78 / 4, Field<uint32_t>(controls, 4), static_cast<uint32_t>(code),
                                         a1, a2, a0);
            if (v > r)
                r = v;
        }
    return r;
}

// 0x00414a30 (esi = controls, edi = action; stack: a0, a1, a2; callee pops 12; returns st0): the larger of Input +0x78
// (active device, binding, a1, a2, a0) over the two bindings (device +4 as is, also -1). The original stores the first
// value into a0's stack slot.
float Controls_AxisMax(void* controls, int32_t action, uint32_t a0, uint32_t a1, uint32_t a2)
{
    const float v1 = VCall<float>(InputObj(), 0x78 / 4, Field<uint32_t>(controls, 4),
                                  Field<uint32_t>(controls, 0x1c + action * 4), a1, a2, a0);
    const float v2 = VCall<float>(InputObj(), 0x78 / 4, Field<uint32_t>(controls, 4),
                                  Field<uint32_t>(controls, 0xa8 + action * 4), a1, a2, a0);
    return PadAxis(controls, action, a0, a1, a2, v1 > v2 ? v1 : v2);
}

// 0x00414aa0 (esi = controls, edi = action; stack: a0, a1, a2; callee pops 12; returns st0): as 0x00414a30, then the
// result is capped at a0 (as a float): r < a0 ? r : a0.
float Controls_AxisMaxCapped(void* controls, int32_t action, uint32_t a0, uint32_t a1, uint32_t a2)
{
    const float v1 = VCall<float>(InputObj(), 0x78 / 4, Field<uint32_t>(controls, 4),
                                  Field<uint32_t>(controls, 0x1c + action * 4), a1, a2, a0);
    const float v2 = VCall<float>(InputObj(), 0x78 / 4, Field<uint32_t>(controls, 4),
                                  Field<uint32_t>(controls, 0xa8 + action * 4), a1, a2, a0);
    const float r = PadAxis(controls, action, a0, a1, a2, v1 > v2 ? v1 : v2);
    if (r < bf(a0))
        return r;
    return bf(a0);
}

namespace
{
// 0x00414b20 / 0x00414b90: one raw input code on any device (locked or no active device; the matching device becomes
// active) or on the active device.
uint32_t QueryCode(void* controls, uint32_t code, uint32_t threshold, int slot)
{
    if (Field<uint32_t>(controls, 0x14) != 0 || Field<int32_t>(controls, 4) == -1)
    {
        if (Field<uint32_t>(controls, 0) == 0)
            return 0;
        for (uint32_t device = 0; device < Field<uint32_t>(controls, 0); device++)
            if (VCall<uint8_t>(InputObj(), slot, device, code, threshold))
            {
                Field<uint32_t>(controls, 4) = device;
                return 1;
            }
        return Field<uint32_t>(controls, 0) & 0xffffff00u;
    }
    return VCall<uint32_t>(InputObj(), slot, Field<uint32_t>(controls, 4), code, threshold);
}
}  // namespace

// 0x00414b20 (edi = controls; stack: code, threshold; callee pops 8): raw input code held (Input +0x6c).
uint32_t Controls_IsCodeHeld(void* controls, uint32_t code, uint32_t threshold)
{
    return QueryCode(controls, code, threshold, 0x6c / 4);
}

// 0x00414b90 (edi = controls; stack: code, threshold; callee pops 8): raw input code pressed (Input +0x68). Not
// executed in recorded runs.
uint32_t Controls_IsCodePressed(void* controls, uint32_t code, uint32_t threshold)
{
    return QueryCode(controls, code, threshold, 0x68 / 4);
}

// 0x00414c00 (ecx: only a `push ecx` slot; esi = {range, value}; stack: delta (float), caller pops; returns st0): the
// wrapped difference fmod(value + delta, range) - fmod(value, range) capped at 1.0 for delta >= 0, else fmod(value,
// range) - fmod(value - delta, range) floored at -1.0 (0x0056c05c = 0.0, 0x005bcb2c = 1.0, 0x005bcb5c = -1.0; fmod is
// the CRT's _CIfmod 0x00566790). The original also stores fmod(value, range) into the delta's stack slot.
float fn_00414c00(uint32_t ecx, const float* range, float delta)
{
    (void)ecx;
    if (!(delta < K(0x0056c05c)))
    {
        const float a = CIfmod(delta + range[1], range[0]);
        const float b = CIfmod(range[1], range[0]);
        const float d = a - b;
        if (d > K(0x005bcb2c))
            return K(0x005bcb2c);
        return d;
    }
    const float a = CIfmod(range[1], range[0]);
    const float b = CIfmod(range[1] - delta, range[0]);
    const float d = a - b;
    if (d < K(0x005bcb5c))
        return K(0x005bcb5c);
    return d;
}

namespace
{
// 0x00414c90's switch, read from its jump tables (0x004153dc/0x00415194, 0x00415550/0x00415500, 0x004154cc,
// 0x004154f0): pointers to "PMS_INPUT_*" string ids. 0x005b9aec = "PMS_INPUT_VK_UNKNOWN".
const uint32_t kKeyNameLow[238] = {
    0x00000000, 0x005ba3a8, 0x005ba644, 0x005ba634, 0x005ba624, 0x005ba614, 0x005ba604, 0x005ba5f4,
    0x005ba5e4, 0x005ba5d4, 0x005ba5c4, 0x005ba654, 0x005b9f50, 0x005b9f3c, 0x005ba410, 0x005ba3fc,
    0x005ba4b4, 0x005ba454, 0x005ba574, 0x005ba4a4, 0x005ba484, 0x005ba434, 0x005ba474, 0x005ba534,
    0x005ba4d4, 0x005ba4c4, 0x005b9fa4, 0x005b9f8c, 0x005ba3e8, 0x005b9ffc, 0x005ba5b4, 0x005ba494,
    0x005ba584, 0x005ba564, 0x005ba554, 0x005ba544, 0x005ba524, 0x005ba514, 0x005ba504, 0x005b9f24,
    0x005b9f0c, 0x005b9ef8, 0x005ba028, 0x005b9e60, 0x005ba424, 0x005ba444, 0x005ba594, 0x005ba464,
    0x005ba5a4, 0x005ba4e4, 0x005ba4f4, 0x005b9f78, 0x005b9f64, 0x005b9ee4, 0x005ba014, 0x005ba1c8,
    0x005b9fd0, 0x005ba394, 0x005ba3bc, 0x005ba160, 0x005ba150, 0x005ba140, 0x005ba130, 0x005ba120,
    0x005ba110, 0x005ba100, 0x005ba0f0, 0x005ba0e0, 0x005ba0cc, 0x005ba050, 0x005ba03c, 0x005ba210,
    0x005ba1f8, 0x005ba1e0, 0x005ba19c, 0x005ba258, 0x005ba240, 0x005ba228, 0x005ba1b4, 0x005ba2a0,
    0x005ba288, 0x005ba270, 0x005ba2b8, 0x005ba184, 0x005b9aec, 0x005b9aec, 0x005b9b20, 0x005ba0b8,
    0x005ba0a4, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005ba090, 0x005ba07c, 0x005ba068, 0x005b9aec,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec,
    0x005b9bec, 0x005b9aec, 0x005b9aec, 0x005b9b54, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec,
    0x005b9aec, 0x005b9bd0, 0x005b9aec, 0x005b9bb4, 0x005b9aec, 0x005b9b9c, 0x005b9b38, 0x005b9aec,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9ecc, 0x005b9aec, 0x005b9aec,
    0x005b9e48, 0x005b9c60, 0x005b9c4c, 0x005b9c34, 0x005b9b84, 0x005b9c20, 0x005b9b70, 0x005b9b04,
    0x005b9aec, 0x005b9e30, 0x005b9aec, 0x005b9aec, 0x005b9eb4, 0x005b9fe4, 0x005b9aec, 0x005b9aec,
    0x005b9e1c, 0x005b9e04, 0x005b9dec, 0x005b9aec, 0x005b9dd4, 0x005b9aec, 0x005b9aec, 0x005b9aec,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9dbc, 0x005b9aec,
    0x005b9da8, 0x005b9aec, 0x005b9d90, 0x005b9c04, 0x005b9aec, 0x005ba170, 0x005b9aec, 0x005b9ea0,
    0x005b9fbc, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005ba3d4, 0x005b9aec, 0x005ba344,
    0x005ba320, 0x005ba380, 0x005b9aec, 0x005ba330, 0x005b9aec, 0x005ba30c, 0x005b9aec, 0x005ba358,
    0x005ba2f8, 0x005ba36c, 0x005ba2e4, 0x005ba2d0, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9e8c, 0x005b9e78, 0x005b9d7c, 0x005b9d68, 0x005b9d54,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9d40, 0x005b9aec, 0x005b9d28, 0x005b9d10, 0x005b9cf8,
    0x005b9ce0, 0x005b9ccc, 0x005b9cb4, 0x005b9c9c, 0x005b9c88, 0x005b9c70,
};
const uint32_t kKeyNameNeg[39] = {  // codes -0x27..-1
    0x005ba98c, 0x005ba9a4, 0x005ba9b8, 0x005ba89c, 0x005ba8b4, 0x005ba8c8, 0x005ba8dc, 0x005b9aec,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005b9aec,
    0x005b9aec, 0x005b9aec, 0x005b9aec, 0x005ba798, 0x005ba7a8, 0x005ba86c, 0x005ba884, 0x005ba83c,
    0x005ba854, 0x005ba7d4, 0x005ba814, 0x005ba828, 0x005ba7bc, 0x005ba7ec, 0x005ba800,
};
const uint32_t kKeyNameMouse[9] = {  // codes 0x10001..0x10009
    0x005ba76c, 0x005ba754, 0x005ba73c, 0x005ba728, 0x005ba714,
    0x005ba700, 0x005ba6ec, 0x005ba6d8, 0x005ba6c4,
};
const uint32_t kKeyNameNeg2[4] = {0x005ba920, 0x005ba938, 0x005ba94c, 0x005ba95c};  // codes -0x2c..-0x29
}  // namespace

// 0x00414c90 (eax = input code): the code's string id "PMS_INPUT_*" (ASCII, e.g. 0x10000 -> "PMS_INPUT_MOUSE_UP",
// 1 -> "PMS_INPUT_VK_ESCAPE"); unknown codes -> 0x005b9aec "PMS_INPUT_VK_UNKNOWN", code 0 -> 0. The switch's
// comparisons are unsigned (ja/je), reproduced in the same order.
uint32_t Input_CodeNameId(uint32_t code)
{
    if (code <= 0x10000)
    {
        if (code == 0x10000)
            return 0x005ba784;
        if (code > 0xed)
            return 0x005b9aec;
        return kKeyNameLow[code];
    }
    if (code > 0xffffffd8u)  // -0x27..-1
        return kKeyNameNeg[code + 0x27];
    if (code == 0xffffffd8u)
        return 0x005ba974;
    if (code <= 0x1000a)
    {
        if (code == 0x1000a)
            return 0x005ba6b0;
        return kKeyNameMouse[code - 0x10001];
    }
    if (code > 0xffffffd3u)  // -0x2c..-0x29
        return kKeyNameNeg2[code - 0xffffffd4u];
    if (code == 0xffffffd3u)
        return 0x005ba908;
    if (code > 0x1000d)
        return code == 0xffffffd2u ? 0x005ba8f0 : 0x005b9aec;
    if (code == 0x1000d)
        return 0x005ba664;
    if (code == 0x1000b)
        return 0x005ba69c;
    if (code == 0x1000c)
        return 0x005ba680;
    return 0x005b9aec;
}

// 0x00415580 (eax = input code): the code's display text: Text_Lookup 0x00495f00 (eax = 0x00414c90's string id,
// ebx = Game+0xac0, stack 1; the `push 1` is made before 0x00414c90, which ends in a plain ret).
uint32_t Input_CodeText(uint32_t code)
{
    const uint32_t id = Input_CodeNameId(code);
    return orig::fn_00495f00(id, Field<uint32_t>(g_game, 0xac0), 1);
}

namespace
{
// 0x004155a0's switch (jump table 0x00415688): 0 "ControlActionMoveForward", 8 "ControlActionFire", 0x1b
// "ControlActionMenuSelect", 0x1c "ControlActionMenuCancel", 0x20 "ControlActionMoveLiftUp", 0x22
// "ControlActionSpecialEvent", ...
const uint32_t kActionNameId[0x23] = {
    0x005b989c, 0x005b9880, 0x005b9868, 0x005b9850, 0x005b9838, 0x005b9820, 0x005b980c, 0x005b97f4, 0x005b9ad8,
    0x005b9ab8, 0x005b9aa0, 0x005b9a8c, 0x005b9a78, 0x005b9a60, 0x005b97dc, 0x005b97c4, 0x005b97a4, 0x005b9784,
    0x005b9a44, 0x005b9a24, 0x005b9a10, 0x005b998c, 0x005b9974, 0x005b9930, 0x005b9918, 0x005b9900, 0x005b98e8,
    0x005b98d0, 0x005b98b8, 0x005b995c, 0x005b9944, 0x005b99f4, 0x005b99dc, 0x005b99c0, 0x005b99a4,
};
}  // namespace

// 0x004155a0 (eax = action): the action's string id "ControlAction*" (unsigned switch 0..0x22); other values -> 0.
uint32_t Controls_ActionNameId(uint32_t action)
{
    if (action > 0x22)
        return 0;
    return kActionNameId[action];
}

// 0x00415720 (eax = action): the action's display text (Text_Lookup 0x00495f00 with eax = 0x004155a0's id, ebx =
// Game+0xac0, stack 1).
uint32_t Controls_ActionText(uint32_t action)
{
    const uint32_t id = Controls_ActionNameId(action);
    return orig::fn_00495f00(id, Field<uint32_t>(g_game, 0xac0), 1);
}

uint32_t Controls_Binding(int32_t action, void* controls);

// 0x00415740 (ecx = action, edx = controls): the display text of the action's binding (0x00415750), 0 when unbound.
// 0x00415750 leaves ecx/edx as given; the tail jump to 0x00415580 passes eax = the code.
uint32_t Controls_BindingText(int32_t action, void* controls)
{
    const uint32_t code = Controls_Binding(action, controls);
    if (!code)
        return 0;
    return Input_CodeText(code);
}

// 0x00415750 (ecx = action, edx = controls): the action's primary binding, or the secondary when the primary is 0;
// action 0x23 -> 0.
uint32_t Controls_Binding(int32_t action, void* controls)
{
    if (action == 0x23)
        return 0;
    const uint32_t code = Field<uint32_t>(controls, 0x1c + action * 4);
    if (code)
        return code;
    return Field<uint32_t>(controls, 0xa8 + action * 4);
}

// ---------------------------------------------------------------------------------------------------------------------
// Force feedback (rumble) and the input base class (vtable 0x0058b1e0)
// ---------------------------------------------------------------------------------------------------------------------

uint32_t Rumble_Start(uint32_t index, uint32_t name, void* input, uint32_t device, uint32_t flagA, uint32_t flagB);
uint32_t Rumble_SetGain(void* input, uint32_t id, uint32_t gain);

// 0x0041ff60 (ebx = rumble sample name, esi = effect or 0, edi = looping flag; stack: a0 (effect description: +8 ->
// name at +0x24), a1, a2 (floats), a3 (byte); callee pops 16). Both a1 and a2 <= 0 and edi == 0: tail jump to
// Rumble_Start 0x004e5020 (eax = -1, ecx = name; the frame rewritten to Game+0x30, the Controls' active device, 0, 0).
// Otherwise without an effect: Debug_Printf "Fx : Failed to play force (min/max distance) (no push)" (0x005b802c, a
// compiled-out printf) and returns eax = Game+0x34 as it passes through. With a3: 0x0045a480(effect+4) must return
// al != 0. Then the effect's object (effect+4): +0x460 |= 0x80000, +0x4e4 = a1, +0x4e8 = a2, +0x4e0 = Rumble_Start(-1,
// name, Game+0x30, active device, flag, flag) where flag = a3's slot with its low byte = (edi != 0); then
// Rumble_SetGain 0x004e51f0(Game+0x30, id, 0).
uint32_t Fx_PlayForce(uint32_t name, void* effect, uint32_t looping, void* a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (!(bf(a1) > K(0x0056c05c)) && !(bf(a2) > K(0x0056c05c)) && looping == 0)
    {
        void* controls = Field<void*>(g_game, 0xa80);
        return Rumble_Start(0xffffffffu, name, Field<void*>(g_game, 0x30), Field<uint32_t>(controls, 4), 0, 0);
    }
    if (!effect)
    {
        const uint32_t audio = Field<uint32_t>(g_game, 0x34);
        orig::fn_00407ff0(audio, 0xa, 0x005b802c, Field<uint32_t>(a0, 8) + 0x24);
        return audio;
    }
    if (a3)
    {
        const uint32_t r = orig::fn_0045a480(Field<uint32_t>(effect, 4));
        if (!static_cast<uint8_t>(r))
            return r;
    }
    Field<uint32_t>(Field<void*>(effect, 4), 0x460) |= 0x80000;
    Field<uint32_t>(Field<void*>(effect, 4), 0x4e4) = a1;
    const uint32_t flag = (a3 & 0xffffff00u) | (looping != 0 ? 1u : 0u);
    Field<uint32_t>(Field<void*>(effect, 4), 0x4e8) = a2;
    const uint32_t id = Rumble_Start(0xffffffffu, name, Field<void*>(g_game, 0x30),
                                     Field<uint32_t>(Field<void*>(g_game, 0xa80), 4), flag, flag);
    Field<uint32_t>(Field<void*>(effect, 4), 0x4e0) = id;
    return Rumble_SetGain(Field<void*>(g_game, 0x30), Field<uint32_t>(Field<void*>(effect, 4), 0x4e0), 0);
}

// 0x004e48a0 (edi = input): the base class destructor body (guess: called by the PC destructor 0x004f7c20 last):
// vtable = 0x0058b1e0; frees the samples (+8), the pool's node chain (pool +0xc, linked by +0) and the pool (+0xc),
// deletes the active list (+4, virtual +0x10 with 1), drops a reference on the global 0x0063a0e8 (+4; freed at 0, no
// null test), +0x10 = 0. Not executed in recorded runs.
uint32_t Input_BaseDestroy(void* input)
{
    uint32_t r = 0;
    Field<uint32_t>(input, 0) = 0x0058b1e0;
    if (Field<uint32_t>(input, 8))
    {
        orig::fn_004e08d0(kHeap, Field<uint32_t>(input, 8));
        Field<uint32_t>(input, 8) = 0;
    }
    void* pool = Field<void*>(input, 0xc);
    if (pool)
    {
        for (uint32_t node = Field<uint32_t>(pool, 0xc); node;)
        {
            const uint32_t next = Field<uint32_t>(P(node), 0);
            orig::fn_004e08d0(kHeap, node);
            node = next;
        }
        Field<uint32_t>(pool, 0xc) = 0;
        Field<uint32_t>(pool, 8) = 0;
        orig::fn_004e08d0(kHeap, u(pool));
    }
    Field<uint32_t>(input, 0xc) = 0;
    if (void* list = Field<void*>(input, 4))
        VCall<uint32_t>(list, 0x10 / 4, 1u);
    void* shared = GameGlobal<void*>(0x0063a0e8);
    Field<uint32_t>(input, 4) = 0;
    r = u(shared);
    if (--Field<int32_t>(shared, 4) == 0)
    {
        r = orig::fn_004e08d0(kHeap, u(shared));
        GameGlobal<uint32_t>(0x0063a0e8) = 0;
    }
    Field<uint32_t>(input, 0x10) = 0;
    return r;
}

// 0x004e4930 (ecx = input): virtual +0x20 (shutdown; PC slot 8, called by the PC destructor): frees the pool's node
// chain and the pool (+0xc), deletes the active list (+4, virtual +0x10 with 1), the name table (+0x20: when its byte
// +0xc is 0, 0x004f11c0 (ebx = table) and its +0x1c buffer are released first), +0x24 and the samples (+8).
uint32_t Input_Shutdown(void* input)
{
    if (void* pool = Field<void*>(input, 0xc))
    {
        for (uint32_t node = Field<uint32_t>(pool, 0xc); node;)
        {
            const uint32_t next = Field<uint32_t>(P(node), 0);
            orig::fn_004e08d0(kHeap, node);
            node = next;
        }
        Field<uint32_t>(pool, 0xc) = 0;
        Field<uint32_t>(pool, 8) = 0;
        orig::fn_004e08d0(kHeap, u(pool));
        Field<uint32_t>(input, 0xc) = 0;
    }
    if (void* list = Field<void*>(input, 4))
    {
        VCall<uint32_t>(list, 0x10 / 4, 1u);
        Field<uint32_t>(input, 4) = 0;
    }
    if (void* table = Field<void*>(input, 0x20))
    {
        if (Field<uint8_t>(table, 0xc) == 0)
        {
            orig::fn_004f11c0(u(table));
            orig::fn_004e08d0(kHeap, Field<uint32_t>(table, 0x1c));
        }
        orig::fn_004e08d0(kHeap, u(table));
        Field<uint32_t>(input, 0x20) = 0;
    }
    if (Field<uint32_t>(input, 0x24))
    {
        orig::fn_004e08d0(kHeap, Field<uint32_t>(input, 0x24));
        Field<uint32_t>(input, 0x24) = 0;
    }
    uint32_t r = Field<uint32_t>(input, 8);
    if (r)
    {
        r = orig::fn_004e08d0(kHeap, r);
        Field<uint32_t>(input, 8) = 0;
    }
    return r;
}

// 0x004e49e0 (ecx = input; stack: sample count + 6 unread arguments; callee pops 28): the base Init (vtable slot 1;
// called by the PC Init 0x004f81d0). Already initialised (+0x18): returns 1. With samples: +8 = 0x840 bytes per sample
// ("Rumble Samples" 0x0058b29c), +0xc = a node pool (0x10 bytes: node size 0x24, count/10 + 1 per block), +4 = the
// active list (0x18 bytes, vtable 0x0058b098, +8 = 0x004e06f0 "PMS_List::m_listData" (edx = count/10 + 1), +4 = 1),
// +0x1c = count. Always: +0x10 = 0x004e43f0(). Returns al = 1.
uint32_t Input_BaseInit(void* input, uint32_t count)
{
    if (Field<uint8_t>(input, 0x18))
        return 1;
    if (count)
    {
        Field<uint32_t>(input, 8) = orig::fn_004e07f0(kHeap, 0, count * 0x840, 2, 0x0058b29c);
        void* pool = P(orig::fn_004e07f0(kHeap, 0x10, 0x10, 2, 0x0058b5e8));
        if (pool)
        {
            Field<uint32_t>(pool, 0) = 0x24;
            Field<uint32_t>(pool, 4) = count / 10 + 1;
            Field<uint32_t>(pool, 8) = 0;
            Field<uint32_t>(pool, 0xc) = 0;
        }
        Field<void*>(input, 0xc) = pool;
        void* list = P(orig::fn_004e07f0(kHeap, 0x10, 0x18, 2, 0x0058b5e8));
        if (list)
        {
            Field<uint32_t>(list, 0) = 0x0058b098;
            Field<uint32_t>(list, 8) = orig::fn_004e06f0(count / 10 + 1, kHeap, 0x0058b0e0, 0x1c, 2, 5);
            Field<uint8_t>(list, 4) = 1;
            Field<uint32_t>(list, 0xc) = 0;
            Field<uint32_t>(list, 0x10) = 0;
            Field<uint32_t>(list, 0x14) = 0;
        }
        Field<void*>(input, 4) = list;
        Field<uint32_t>(input, 0x1c) = count;
    }
    const uint32_t r = orig::fn_004e43f0();
    Field<uint32_t>(input, 0x10) = r;
    return (r & 0xffffff00u) | 1;
}

// 0x004e4b00 (ecx = input; stack: device; callee pops 4): virtual +0x30: connected (+8) and (+0x40 or +0x54) non-zero;
// returns that value (0 otherwise). Not executed in recorded runs.
uint32_t fn_004e4b00(void* input, uint32_t device)
{
    if (!VCall<uint8_t>(input, 8 / 4, device))
        return 0;
    uint32_t r = VCall<uint32_t>(input, 0x40 / 4, device);
    if (r)
        return r;
    r = VCall<uint32_t>(input, 0x54 / 4, device);
    if (r)
        return r;
    return 0;
}

// 0x004e4b40 (ecx = input; stack: device; callee pops 4): virtual +0x40: +0x4c, else +0x60. Not executed in recorded
// runs.
uint32_t fn_004e4b40(void* input, uint32_t device)
{
    const uint32_t r = VCall<uint32_t>(input, 0x4c / 4, device);
    if (r)
        return r;
    return VCall<uint32_t>(input, 0x60 / 4, device);
}

// 0x004e4b60 (ecx = input; stack: device; callee pops 4): virtual +0x2c (any input, per the Controls' use): connected
// (+8) and the first non-zero of +0x30, +0x34, +0x38; 0 otherwise. Not executed in recorded runs.
uint32_t Input_AnyInput(void* input, uint32_t device)
{
    if (!VCall<uint8_t>(input, 8 / 4, device))
        return 0;
    uint32_t r = VCall<uint32_t>(input, 0x30 / 4, device);
    if (r)
        return r;
    r = VCall<uint32_t>(input, 0x34 / 4, device);
    if (r)
        return r;
    r = VCall<uint32_t>(input, 0x38 / 4, device);
    if (r)
        return r;
    return 0;
}

// 0x004e4ba0 (ecx = input; stack: device; callee pops 4): virtual +0x50: +0x54, else +0x58. Not executed in recorded
// runs.
uint32_t fn_004e4ba0(void* input, uint32_t device)
{
    const uint32_t r = VCall<uint32_t>(input, 0x54 / 4, device);
    if (r)
        return r;
    return VCall<uint32_t>(input, 0x58 / 4, device);
}

// 0x004e4bc0 (ecx = input; stack: device; callee pops 4): virtual +0x3c: +0x40, else +0x38, else +0x44. Not executed
// in recorded runs.
uint32_t fn_004e4bc0(void* input, uint32_t device)
{
    uint32_t r = VCall<uint32_t>(input, 0x40 / 4, device);
    if (r)
        return r;
    r = VCall<uint32_t>(input, 0x38 / 4, device);
    if (r)
        return r;
    return VCall<uint32_t>(input, 0x44 / 4, device);
}

// 0x004e4bf0 (ecx = input; stack: device; callee pops 4): virtual +0x5c: +0x60, else +0x54, else +0x58. Not executed
// in recorded runs.
uint32_t fn_004e4bf0(void* input, uint32_t device)
{
    uint32_t r = VCall<uint32_t>(input, 0x60 / 4, device);
    if (r)
        return r;
    r = VCall<uint32_t>(input, 0x54 / 4, device);
    if (r)
        return r;
    return VCall<uint32_t>(input, 0x58 / 4, device);
}

// 0x004e4c20 (ecx = input; stack: device; callee pops 4): virtual +0x48: +0x38, else +0x4c, else +0x44. Not executed
// in recorded runs.
uint32_t fn_004e4c20(void* input, uint32_t device)
{
    uint32_t r = VCall<uint32_t>(input, 0x38 / 4, device);
    if (r)
        return r;
    r = VCall<uint32_t>(input, 0x4c / 4, device);
    if (r)
        return r;
    return VCall<uint32_t>(input, 0x44 / 4, device);
}

// 0x004e4c50 (ecx = input; stack: device; callee pops 4): base virtual +0x54 (the PC class overrides it with
// 0x004f89b0): the first of the codes -0x28, -0x29, -0x26, -0x27, -0x2d, -0x2e, -0x2b, -0x2c held (+0x6c, threshold
// 0.75) is returned; none: 0. Not executed in recorded runs.
uint32_t fn_004e4c50(void* input, uint32_t device)
{
    static const int32_t codes[7] = {-0x28, -0x29, -0x26, -0x27, -0x2d, -0x2e, -0x2b};
    for (int32_t code : codes)
        if (VCall<uint8_t>(input, 0x6c / 4, device, code, 0x3f400000u))
            return static_cast<uint32_t>(code);
    return VCall<uint8_t>(input, 0x6c / 4, device, -0x2c, 0x3f400000u) ? 0xffffffd4u : 0u;
}

// 0x004e4d40 (ecx = input; stack: device, code, threshold; callee pops 12): virtual +0x74: not held (!+0x6c). Not
// executed in recorded runs (called virtually by 0x004148f0).
uint32_t Input_IsUp(void* input, uint32_t device, uint32_t code, uint32_t threshold)
{
    return VCall<uint8_t>(input, 0x6c / 4, device, code, threshold) ? 0u : 1u;
}

// 0x004e5020 (eax = sample index, ecx = sample name or 0; stack: input, device, flagA (byte), flagB (byte); callee pops
// 16): starts a rumble. With a name: no samples (+8) -> 0; index = the name table's (+0x20, 0x004f1160 eax = table)
// entry's first dword, -1 when missing or without a table. Index >= count (+0x1c, unsigned) -> 0. A node from the pool
// (0x004f6020, esi = pool), zeroed (0x24 bytes), added to the active list (virtual +0x14); id = +0x14 (then +0x14 + 1,
// 1 after a wrap to 0). Node: +0 sample, +4 time 0, +8/+0xc 0, +0x10 gain 1.0, +0x14 device, +0x18 id, +0x1c pause
// count 0, +0x20 flags: sample+0x38 <= 0 -> 1 (first motor done) else flagA -> 2 (loop); sample+0x448 <= 0 -> 0x10000
// else flagB -> 0x20000. Returns the id.
uint32_t Rumble_Start(uint32_t index, uint32_t name, void* input, uint32_t device, uint32_t flagA, uint32_t flagB)
{
    if (name)
    {
        if (!Field<uint32_t>(input, 8))
            return 0;
        if (Field<uint32_t>(input, 0x20))
        {
            const uint32_t entry = orig::fn_004f1160(Field<uint32_t>(input, 0x20), name);
            index = entry ? Field<uint32_t>(P(entry), 0) : 0xffffffffu;
        }
        else
            index = 0xffffffffu;
    }
    if (index >= Field<uint32_t>(input, 0x1c))
        return 0;
    const uint32_t sample = index * 0x840 + Field<uint32_t>(input, 8);
    void* node = P(orig::fn_004f6020(Field<uint32_t>(input, 0xc)));
    std::memset(node, 0, 0x24);
    VCall<uint32_t>(Field<void*>(input, 4), 0x14 / 4, node);
    const uint32_t id = Field<uint32_t>(input, 0x14);
    Field<uint32_t>(input, 0x14) = id + 1;
    if (id + 1 == 0)
        Field<uint32_t>(input, 0x14) = 1;
    Field<uint32_t>(node, 0x18) = id;
    Field<uint32_t>(node, 0) = sample;
    Field<uint32_t>(node, 0x1c) = 0;
    Field<uint32_t>(node, 0x14) = device;
    Field<uint32_t>(node, 4) = 0;
    Field<uint32_t>(node, 8) = 0;
    Field<uint32_t>(node, 0xc) = 0;
    Field<uint32_t>(node, 0x10) = 0x3f800000;
    if (Field<float>(P(sample), 0x38) <= K(0x0056c05c))
        Field<uint32_t>(node, 0x20) |= 1;
    else if (static_cast<uint8_t>(flagA))
        Field<uint32_t>(node, 0x20) |= 2;
    if (Field<float>(P(sample), 0x448) <= K(0x0056c05c))
        Field<uint32_t>(node, 0x20) |= 0x10000;
    else if (static_cast<uint8_t>(flagB))
        Field<uint32_t>(node, 0x20) |= 0x20000;
    return Field<uint32_t>(node, 0x18);
}

// 0x004e5130 (stack: input, &id; callee pops 8): stops the rumble with that id: unless already stopping (flag
// 0x80000000), +8 = sample+0x34 and +0xc = sample+0x444 (fade-out times, guess) and the flag is set; the list iterator
// (0x004e7340 / 0x004e73c0) is released through its owner's virtual +0xc. *id = 0 in every case. Returns 0, or the
// iterator node when nothing matched.
uint32_t Rumble_Stop(void* input, uint32_t* id)
{
    uint32_t iter[2] = {0, 0};
    if (!Field<uint32_t>(input, 4))
        return 0;
    for (uint32_t node = orig::fn_004e7340(Field<uint32_t>(input, 4), u(iter)); node;
         node = orig::fn_004e73c0(u(iter)))
    {
        if (Field<uint32_t>(P(node), 0x18) != *id)
            continue;
        if (!(Field<uint32_t>(P(node), 0x20) & 0x80000000u))
        {
            void* sample = Field<void*>(P(node), 0);
            Field<uint32_t>(P(node), 8) = Field<uint32_t>(sample, 0x34);
            Field<uint32_t>(P(node), 0xc) = Field<uint32_t>(sample, 0x444);
            Field<uint32_t>(P(node), 0x20) |= 0x80000000u;
        }
        VCall<uint32_t>(P(iter[0]), 0xc / 4, iter[1]);
        Field<int32_t>(P(iter[1]), 0x18)--;
        *id = 0;
        return 0;
    }
    *id = 0;
    if (iter[1])
        Field<int32_t>(P(iter[1]), 0x18)--;
    return iter[1];
}

// 0x004e51f0 (stack: input, id, gain (float); callee pops 12): sets the gain (+0x10) of the rumble with that id and
// releases the iterator (owner virtual +0xc, node reference -1). Returns the reference count left, or the iterator node
// when nothing matched.
uint32_t Rumble_SetGain(void* input, uint32_t id, uint32_t gain)
{
    uint32_t iter[2] = {0, 0};
    for (uint32_t node = orig::fn_004e7340(Field<uint32_t>(input, 4), u(iter)); node;
         node = orig::fn_004e73c0(u(iter)))
    {
        if (Field<uint32_t>(P(node), 0x18) != id)
            continue;
        Field<uint32_t>(P(node), 0x10) = gain;
        VCall<uint32_t>(P(iter[0]), 0xc / 4, iter[1]);
        return static_cast<uint32_t>(--Field<int32_t>(P(iter[1]), 0x18));
    }
    if (iter[1])
        Field<int32_t>(P(iter[1]), 0x18)--;
    return iter[1];
}

namespace
{
// 0x004e5460's motor value: random (sample+kind == 3: rand() * amplitude * 2^-15, 0x005bcbc0) or the wave table:
// t = node time minus whole periods (subtracted while t >= period), entry 0x12 (or 0x116) - (int)(t / period * -250.0
// (0x005bcbc4)) of the sample's dwords.
float MotorValue(void* node, void* sample, uint32_t kindOff, uint32_t ampOff, uint32_t periodOff, int32_t tableBase)
{
    if (Field<int32_t>(sample, kindOff) == 3)
    {
        const float amp = Field<float>(sample, ampOff);
        const int32_t r = static_cast<int32_t>(orig::fn_005391cd());
        return static_cast<float>(r) * amp * K(0x005bcbc0);
    }
    float t = Field<float>(node, 4);
    const float period = Field<float>(sample, periodOff);
    while (t >= period)
        t = t - period;
    const int32_t i = static_cast<int32_t>(t / Field<float>(sample, periodOff) * K(0x005bcbc4));
    return Field<float>(sample, (tableBase - i) * 4);
}
}  // namespace

// 0x004e5460 (ecx = input, edx = node or 0; stack: id, &first, &second (floats); callee pops 12): the two motor values
// of a rumble. *first = *second = 0. Without a node: the active list is searched for the id (none: return); the
// iterator is released (owner virtual +0xc, node reference -1). First motor: done (flag 1) -> 0, else the sample's wave
// (kind +0x44, amplitude +0x40, period +0x38, table from dword 0x12 down); second motor: done (0x10000) -> 0, else kind
// +0x454, amplitude +0x450, period +0x448, table from dword 0x116 down.
uint32_t Rumble_MotorValues(void* input, void* node, uint32_t id, float* first, float* second)
{
    uint32_t iter[2] = {0, 0};
    *first = 0;
    *second = 0;
    if (!node)
    {
        for (uint32_t n = orig::fn_004e7340(Field<uint32_t>(input, 4), u(iter)); n; n = orig::fn_004e73c0(u(iter)))
            if (Field<uint32_t>(P(n), 0x18) == id)
            {
                node = P(n);
                break;
            }
        if (!node)
        {
            if (iter[1])
                Field<int32_t>(P(iter[1]), 0x18)--;
            return iter[1];
        }
        VCall<uint32_t>(P(iter[0]), 0xc / 4, iter[1]);
        Field<int32_t>(P(iter[1]), 0x18)--;
        iter[1] = 0;
    }
    if (Field<uint32_t>(node, 0x20) & 1)
        *first = 0;
    else
        *first = MotorValue(node, Field<void*>(node, 0), 0x44, 0x40, 0x38, 0x12);
    if (Field<uint32_t>(node, 0x20) & 0x10000)
        *second = 0;
    else
        *second = MotorValue(node, Field<void*>(node, 0), 0x454, 0x450, 0x448, 0x116);
    return 0;
}

// 0x004e5610 (ecx = envelope {delay, attack, hold, release, sustain level, fade time} (guess for the names), edx =
// node; stack: unread, fade (float); callee pops 8; returns st0): the envelope gain at the node's time (+4). fade > 0:
// fade / env[5] * env[4]. Otherwise t <= env[0]: 0; t < env[0] + env[1]: 1 - (env[0] + env[1] - t) / env[1]; t <
// env[0] + env[2] + env[1]: 1; t < env[3] + env[0] + env[2] + env[1]: 1 - (1 - (s - t) / env[3]) * env[4]; else env[4].
float Rumble_Envelope(const float* env, void* node, uint32_t unused, float fade)
{
    (void)unused;
    if (fade > K(0x0056c05c))
        return fade / env[5] * env[4];
    const float t = Field<float>(node, 4);
    if (!(t > env[0]))
        return K(0x0056c05c);
    const float a = env[0] + env[1];
    if (a > t)
        return K(0x005bcb2c) - (a - t) / env[1];
    if (!(env[0] + env[2] + env[1] > t))
    {
        const float s = env[3] + env[0] + env[2] + env[1];
        if (s > t)
            return K(0x005bcb2c) - (K(0x005bcb2c) - (s - t) / env[3]) * env[4];
        return env[4];
    }
    return K(0x005bcb2c);
}

// 0x004e56c0 (stack: input, pause (byte); callee pops 8): every active rumble's pause count (+0x1c) goes up (pause) or
// down (not below 0). Returns the iterator node.
uint32_t Rumble_PauseAll(void* input, uint32_t pause)
{
    uint32_t iter[2] = {0, 0};
    for (uint32_t node = orig::fn_004e7340(Field<uint32_t>(input, 4), u(iter)); node;
         node = orig::fn_004e73c0(u(iter)))
    {
        if (static_cast<uint8_t>(pause))
            Field<uint32_t>(P(node), 0x1c)++;
        else if (Field<uint32_t>(P(node), 0x1c))
            Field<uint32_t>(P(node), 0x1c)--;
    }
    if (iter[1])
        Field<int32_t>(P(iter[1]), 0x18)--;
    return iter[1];
}

// 0x004e5750 (ebx: only saved; stack: input, dt (float); callee pops 8): advances every active rumble. A rumble whose
// device is gone (virtual +8) gets flags 0x10001. Both motors done (1 and 0x10000): id = 0, removed from the list
// (virtual +0x28) and pushed onto the pool's free list (pool +8). Stopping (0x80000000): the fade times (+8, +0xc) run
// down by dt; <= 0 sets the motor's done flag. Paused (+0x1c): skipped. Otherwise time += dt; past both periods without
// a loop flag (0x20002): done. With rumble enabled (+0x19): the motor values (0x004e5460) times the envelopes (sample
// +0x20 and +0x430, 0x004e5610 with the fade times) times the gain (+0x10). Each motor not finished this frame adds its
// value to its sum. Finally, with a device seen and rumble enabled: virtual +0x88 (device, sum1, sum2). The running
// values persist from one rumble to the next when rumble is disabled (as in the original, where they live in the
// frame: the first motor's in the input argument's slot).
uint32_t Rumble_Update(uint32_t ebx, void* input, float dt)
{
    (void)ebx;
    uint32_t iter[2] = {0, 0};
    if (!Field<uint32_t>(input, 4))
        return 0;
    float first = 0, second = 0, sum1 = 0, sum2 = 0;
    int32_t device = -1;
    for (uint32_t n = orig::fn_004e7340(Field<uint32_t>(input, 4), u(iter)); n; n = orig::fn_004e73c0(u(iter)))
    {
        void* node = P(n);
        if (!VCall<uint8_t>(input, 8 / 4, Field<uint32_t>(node, 0x14)))
            Field<uint32_t>(node, 0x20) |= 0x10001;
        uint32_t flags = Field<uint32_t>(node, 0x20);
        if ((flags & 1) && (flags & 0x10000))
        {
            Field<uint32_t>(node, 0x18) = 0;
            VCall<uint32_t>(Field<void*>(input, 4), 0x28 / 4, node);
            void* pool = Field<void*>(input, 0xc);
            Field<uint32_t>(node, 0) = Field<uint32_t>(pool, 8);
            Field<void*>(pool, 8) = node;
            continue;
        }
        if (flags & 0x80000000u)
        {
            const float a = Field<float>(node, 8) - dt;
            Field<float>(node, 8) = a;
            if (a <= K(0x0056c05c))
                Field<uint32_t>(node, 0x20) |= 1;
            const float b = Field<float>(node, 0xc) - dt;
            Field<float>(node, 0xc) = b;
            if (b <= K(0x0056c05c))
                Field<uint32_t>(node, 0x20) |= 0x10000;
        }
        device = Field<int32_t>(node, 0x14);
        if (Field<uint32_t>(node, 0x1c))
            continue;
        void* sample = Field<void*>(node, 0);
        const float t = dt + Field<float>(node, 4);
        Field<float>(node, 4) = t;
        if (!(t < Field<float>(sample, 0x38)) && !(t < Field<float>(sample, 0x448)))
        {
            flags = Field<uint32_t>(node, 0x20);
            if (!(flags & 0x20002))
            {
                Field<uint32_t>(node, 0x20) = flags | 0x10001;
                continue;
            }
        }
        if (Field<uint8_t>(input, 0x19))
        {
            Rumble_MotorValues(input, node, 0, &first, &second);
            sample = Field<void*>(node, 0);
            first = Rumble_Envelope(reinterpret_cast<const float*>(static_cast<uint8_t*>(sample) + 0x20), node, u(input),
                                    Field<float>(node, 8)) *
                    Field<float>(node, 0x10) * first;
            second = Rumble_Envelope(reinterpret_cast<const float*>(static_cast<uint8_t*>(sample) + 0x430), node,
                                     u(input), Field<float>(node, 0xc)) *
                     Field<float>(node, 0x10) * second;
        }
        if (!(Field<float>(node, 4) < Field<float>(sample, 0x38)) && !(Field<uint32_t>(node, 0x20) & 2))
            Field<uint32_t>(node, 0x20) |= 1;
        else
            sum1 = first + sum1;
        if (!(Field<float>(node, 4) < Field<float>(Field<void*>(node, 0), 0x448)) &&
            !(Field<uint32_t>(node, 0x20) & 0x20000))
            Field<uint32_t>(node, 0x20) |= 0x10000;
        else
            sum2 = second + sum2;
    }
    if (device != -1 && Field<uint8_t>(input, 0x19))
        VCall<uint32_t>(input, 0x88 / 4, device, sum1, sum2);
    if (iter[1])
        Field<int32_t>(P(iter[1]), 0x18)--;
    return iter[1];
}

// ---------------------------------------------------------------------------------------------------------------------
// The PC input class (vtable 0x0058a600): DirectInput 8 keyboard, mouse and one game controller
// ---------------------------------------------------------------------------------------------------------------------
// Members (from the functions below): +0x28 mouse IDirectInputDevice8A (DIMOUSESTATE2, 0x14 bytes), +0x2c/+0x30/+0x34
// mouse x/y/wheel this frame (floats), +0x38/+0x3c/+0x40 the previous frame's, +0x44 button mask, +0x48 previous mask,
// +0x4c held, +0x50 released, +0x54 pressed; +0x58 keyboard IDirectInputDevice8A, +0x5c 256 key states (0 up, 1
// pressed, 2 held, 3 released); +0x15c IDirectInput8A, +0x160 controller IDirectInputDevice8A, +0x164 its DIDEVCAPS,
// +0x190 its state (0x34 bytes: X, Y, Z, Rz, POV, 12 button bytes; axes recentred by -32255), +0x1c4 the previous
// state, +0x1f8..+0x204 the four axes scaled (floats), +0x208..+0x214 the previous ones.

namespace
{
template <class Fn>
inline Fn Import(uint32_t iatSlot)
{
    return *reinterpret_cast<Fn*>(iatSlot);
}
using SetCapture_t = HWND(WINAPI*)(HWND);
using ReleaseCapture_t = BOOL(WINAPI*)();
using ClientToScreen_t = BOOL(WINAPI*)(HWND, LPPOINT);
using ClipCursor_t = BOOL(WINAPI*)(const RECT*);
using GetClientRect_t = BOOL(WINAPI*)(HWND, LPRECT);
using GetModuleHandleA_t = HMODULE(WINAPI*)(LPCSTR);
using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

inline IDirectInputDevice8A* Device(void* pc, uint32_t offset)
{
    return Field<IDirectInputDevice8A*>(pc, offset);
}
constexpr HRESULT kInputLost = static_cast<HRESULT>(0x8007001e);    // DIERR_INPUTLOST
constexpr HRESULT kNotAcquired = static_cast<HRESULT>(0x8007000c);  // DIERR_NOTACQUIRED
}  // namespace

uint32_t InputPC_Construct(void* mem);
uint32_t InputPC_Destroy(void* pc);

// 0x004f7b30 (no inputs; returns eax): the input singleton 0x0063a0c0: created on first use (PMS_heapAlloc 0x228 bytes,
// "new", constructor 0x004f7bb0), its reference count (+0x224) incremented. An allocation failure stores 0 and then
// increments 0->+0x224 (original bug: a write to address 0x224).
uint32_t InputPC_Get()
{
    uint32_t pc = GameGlobal<uint32_t>(0x0063a0c0);
    if (!pc)
    {
        const uint32_t mem = orig::fn_004e07f0(kHeap, 0x10, 0x228, 2, 0x0058b5e8);
        if (mem)
        {
            pc = InputPC_Construct(P(mem));
            const uint32_t refs = Field<uint32_t>(P(pc), 0x224) + 1;
            GameGlobal<uint32_t>(0x0063a0c0) = pc;
            Field<uint32_t>(P(pc), 0x224) = refs;
            return pc;
        }
        GameGlobal<uint32_t>(0x0063a0c0) = 0;
        pc = 0;
    }
    ++*reinterpret_cast<volatile uint32_t*>(pc + 0x224);
    return pc;
}

// 0x004f7b80 (no inputs; vtable slot 0, thiscall but `this` unread): drops a reference on the singleton 0x0063a0c0; at
// 0 it is destroyed (0x004f7c20), freed and the global cleared. Not executed in recorded runs.
uint32_t InputPC_Release()
{
    const uint32_t pc = GameGlobal<uint32_t>(0x0063a0c0);
    if (--Field<int32_t>(P(pc), 0x224) != 0)
        return pc;
    InputPC_Destroy(P(pc));
    orig::fn_004e08d0(kHeap, pc);
    GameGlobal<uint32_t>(0x0063a0c0) = 0;
    return 0;
}

// 0x004f7bb0 (edx = memory; returns eax = it): the constructor: vtable 0x0058a600, sensitivity +0x21c = 1.0, +0x14 =
// 1, +0x19 = 1 (rumble on), everything else used cleared (+4..+0x10, +0x18, +0x1c..+0x24, +0x28..+0x57,
// +0x58..+0x15b, +0x15c..+0x217, +0x220, +0x224).
uint32_t InputPC_Construct(void* mem)
{
    Field<uint32_t>(mem, 0x14) = 1;
    Field<uint8_t>(mem, 0x19) = 1;
    Field<uint32_t>(mem, 8) = 0;
    Field<uint32_t>(mem, 0x1c) = 0;
    Field<uint32_t>(mem, 4) = 0;
    Field<uint32_t>(mem, 0xc) = 0;
    Field<uint32_t>(mem, 0x24) = 0;
    Field<uint32_t>(mem, 0x20) = 0;
    Field<uint8_t>(mem, 0x18) = 0;
    Field<uint32_t>(mem, 0x10) = 0;
    Field<uint32_t>(mem, 0x224) = 0;
    Field<uint8_t>(mem, 0x220) = 0;
    Field<uint32_t>(mem, 0) = 0x0058a600;
    Field<uint32_t>(mem, 0x21c) = 0x3f800000;
    std::memset(static_cast<uint8_t*>(mem) + 0x58, 0, 0x41 * 4);
    std::memset(static_cast<uint8_t*>(mem) + 0x15c, 0, 0x2f * 4);
    std::memset(static_cast<uint8_t*>(mem) + 0x28, 0, 0xc * 4);
    return u(mem);
}

// 0x004f7c20 (stack: this; callee pops 4): the destructor: vtable 0x0058a600, Input_Shutdown 0x004e4930; keyboard
// (+0x58) Unacquire + Release; mouse (+0x28): SetCapture(window) (sic, 0x0056b224), ClipCursor(NULL) when exclusive
// (+0x220), Unacquire + Release; controller (+0x160) Unacquire + Release; IDirectInput8 (+0x15c) Release (the pointer is
// not cleared); then the base 0x004e48a0. Not executed in recorded runs.
uint32_t InputPC_Destroy(void* pc)
{
    Field<uint32_t>(pc, 0) = 0x0058a600;
    Input_Shutdown(pc);
    if (IDirectInputDevice8A* keyboard = Device(pc, 0x58))
    {
        keyboard->Unacquire();
        Device(pc, 0x58)->Release();
        Field<uint32_t>(pc, 0x58) = 0;
    }
    if (Device(pc, 0x28))
    {
        Import<SetCapture_t>(0x0056b224)(Field<HWND>(pc, 0x218));
        if (Field<uint8_t>(pc, 0x220))
            Import<ClipCursor_t>(0x0056b230)(nullptr);
        Device(pc, 0x28)->Unacquire();
        Device(pc, 0x28)->Release();
        Field<uint32_t>(pc, 0x28) = 0;
    }
    if (Device(pc, 0x160))
    {
        Device(pc, 0x160)->Unacquire();
        Device(pc, 0x160)->Release();
        Field<uint32_t>(pc, 0x160) = 0;
    }
    if (IDirectInput8A* di = Field<IDirectInput8A*>(pc, 0x15c))
        di->Release();
    return Input_BaseDestroy(pc);
}

// 0x004f7cf0 (ecx = pc; stack: value; callee pops 4): virtual +0x24: the mouse sensitivity +0x21c. Not executed in
// recorded runs.
uint32_t InputPC_SetMouseSensitivity(void* pc, uint32_t value)
{
    Field<uint32_t>(pc, 0x21c) = value;
    return value;
}

// 0x004f7e10 (ebx = pc): the game controller: IDirectInput8::EnumDevices(DI8DEVCLASS_GAMECTRL, callback 0x004f7d00,
// &+0x15c, DIEDFL_ATTACHEDONLY) (the callback creates +0x160); then SetCooperativeLevel(window, 9), GetCapabilities
// (+0x164, dwSize 0x2c), EnumObjects(callback 0x004f7d30, a zeroed 0x5cc-byte object table, DIDFT_ALL) and
// SetDataFormat with a 17-object format built on the stack (dwDataSize 0x34, DIDF_ABSAXIS; the GUID pointers and
// types come from the object table). After EnumObjects (whatever its result) the POV words +0x1a0 and +0x1d4 = -1.
// Returns the last HRESULT.
uint32_t InputPC_InitController(void* pc)
{
    IDirectInput8A* di = Field<IDirectInput8A*>(pc, 0x15c);
    HRESULT hr = di->EnumDevices(4, reinterpret_cast<LPDIENUMDEVICESCALLBACKA>(0x004f7d00),
                                 &Field<uint32_t>(pc, 0x15c), 1);
    if (hr < 0)
        return static_cast<uint32_t>(hr);
    IDirectInputDevice8A* joy = Device(pc, 0x160);
    if (!joy)
        return static_cast<uint32_t>(hr);
    hr = joy->SetCooperativeLevel(Field<HWND>(pc, 0x218), 9);
    if (hr < 0)
        return static_cast<uint32_t>(hr);
    Field<uint32_t>(pc, 0x164) = 0x2c;
    hr = Device(pc, 0x160)->GetCapabilities(reinterpret_cast<LPDIDEVCAPS>(&Field<uint32_t>(pc, 0x164)));
    if (hr < 0)
        return static_cast<uint32_t>(hr);
    // The original frame: DIDATAFORMAT at +8, 17 DIOBJECTDATAFORMATs at +0x20, the object table at +0x130.
    alignas(8) uint8_t objects[0x5cc];
    std::memset(objects, 0, sizeof(objects));
    hr = Device(pc, 0x160)->EnumObjects(reinterpret_cast<LPDIENUMDEVICEOBJECTSCALLBACKA>(0x004f7d30), objects, 0);
    if (hr >= 0)
    {
        // object table offset (minus the frame's 0x130) of each GUID, data offset, object table offset of its type
        static const uint16_t layout[17][3] = {
            {0x15c, 4, 0x584},     {0x16c, 0, 0x588},     {0x13c, 0xc, 0x57c},   {0x14c, 8, 0x580},
            {0x33c, 0x10, 0x5fc},  {0x37c, 0x14, 0x67c},  {0x38c, 0x15, 0x680},  {0x39c, 0x16, 0x684},
            {0x3ac, 0x17, 0x688},  {0x3bc, 0x18, 0x68c},  {0x3cc, 0x19, 0x690},  {0x3dc, 0x1a, 0x694},
            {0x3ec, 0x1b, 0x698},  {0x3fc, 0x1c, 0x69c},  {0x40c, 0x1d, 0x6a0},  {0x41c, 0x1e, 0x6a4},
            {0x42c, 0x1f, 0x6a8},
        };
        DIOBJECTDATAFORMAT odf[17];
        for (int i = 0; i < 17; i++)
        {
            odf[i].pguid = reinterpret_cast<const GUID*>(objects + layout[i][0] - 0x130);
            odf[i].dwOfs = layout[i][1];
            odf[i].dwType = *reinterpret_cast<const DWORD*>(objects + layout[i][2] - 0x130);
            odf[i].dwFlags = 0;
        }
        DIDATAFORMAT format;
        format.dwSize = 0x18;
        format.dwObjSize = 0x10;
        format.dwFlags = 1;
        format.dwDataSize = 0x34;
        format.dwNumObjs = 0x11;
        format.rgodf = odf;
        hr = Device(pc, 0x160)->SetDataFormat(&format);
    }
    Field<int32_t>(pc, 0x1a0) = -1;
    Field<int32_t>(pc, 0x1d4) = -1;
    return static_cast<uint32_t>(hr);
}

// 0x004f81d0 (ecx = pc; stack: 7 arguments; callee pops 28): virtual +4, Init. Already initialised (+0x18): 1.
// Window +0x218 = *a6, exclusive +0x220 = a5; DirectInput8Create(GetModuleHandleA(NULL), 0x800, IID 0x005791b8,
// &+0x15c, NULL) through the thunk 0x0056551e; the mouse (+0x28: CreateDevice GUID 0x00579168, SetDataFormat 0x005793b4,
// SetCooperativeLevel 6, Acquire) and the keyboard (+0x58: GUID 0x00579158, format 0x005795bc, level 0x16, Acquire)
// (HRESULTs ignored); the controller (0x004f7e10); virtual +0x28 (1, 1) (capture); then the base Init 0x004e49e0 with
// all seven arguments. Not executed in recorded runs.
uint32_t InputPC_Init(void* pc, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                      uint32_t a6)
{
    if (Field<uint8_t>(pc, 0x18))
        return 1;
    Field<uint32_t>(pc, 0x218) = Field<uint32_t>(P(a6), 0);
    Field<uint8_t>(pc, 0x220) = static_cast<uint8_t>(a5);
    const HMODULE module = Import<GetModuleHandleA_t>(0x0056b0e0)(nullptr);
    GameFunction<DirectInput8Create_t>(0x0056551e)(module, 0x800, *reinterpret_cast<const IID*>(0x005791b8),
                                                   reinterpret_cast<LPVOID*>(&Field<uint32_t>(pc, 0x15c)), nullptr);
    IDirectInput8A* di = Field<IDirectInput8A*>(pc, 0x15c);
    di->CreateDevice(*reinterpret_cast<const GUID*>(0x00579168), &Field<IDirectInputDevice8A*>(pc, 0x28), nullptr);
    Device(pc, 0x28)->SetDataFormat(reinterpret_cast<LPCDIDATAFORMAT>(0x005793b4));
    Device(pc, 0x28)->SetCooperativeLevel(Field<HWND>(pc, 0x218), 6);
    Device(pc, 0x28)->Acquire();
    di = Field<IDirectInput8A*>(pc, 0x15c);
    di->CreateDevice(*reinterpret_cast<const GUID*>(0x00579158), &Field<IDirectInputDevice8A*>(pc, 0x58), nullptr);
    Device(pc, 0x58)->SetDataFormat(reinterpret_cast<LPCDIDATAFORMAT>(0x005795bc));
    Device(pc, 0x58)->SetCooperativeLevel(Field<HWND>(pc, 0x218), 0x16);
    Device(pc, 0x58)->Acquire();
    InputPC_InitController(pc);
    VCall<uint32_t>(pc, 0x28 / 4, 1u, 1u);
    (void)a1, (void)a2, (void)a3, (void)a4;
    return Input_BaseInit(pc, a0);  // 0x004e49e0 reads only its first argument
}

// 0x004f82d0 (ecx = pc; stack: clip (byte), capture (byte); callee pops 8): virtual +0x28. Without a mouse (+0x28):
// nothing. capture: SetCapture(window) else ReleaseCapture(). When exclusive (+0x220): clip -> the window's client
// rectangle in screen coordinates (GetClientRect + ClientToScreen of (0, 0)) goes to ClipCursor and Log_Printf
// "Cursor clipped from (%d,%d) to (%d,%d)\n" (left, top, right, bottom; 0x0058a8fc); otherwise ClipCursor(NULL). Not
// executed in recorded runs.
uint32_t InputPC_SetMouseCapture(void* pc, uint32_t clip, uint32_t capture)
{
    if (!Field<uint32_t>(pc, 0x28))
        return 0;
    if (static_cast<uint8_t>(capture))
        Import<SetCapture_t>(0x0056b224)(Field<HWND>(pc, 0x218));
    else
        Import<ReleaseCapture_t>(0x0056b228)();
    if (!Field<uint8_t>(pc, 0x220))
        return 0;
    if (!static_cast<uint8_t>(clip))
        return static_cast<uint32_t>(Import<ClipCursor_t>(0x0056b230)(nullptr));
    RECT rc;
    Import<GetClientRect_t>(0x0056b250)(Field<HWND>(pc, 0x218), &rc);
    POINT pt = {0, 0};
    Import<ClientToScreen_t>(0x0056b22c)(Field<HWND>(pc, 0x218), &pt);
    rc.left += pt.x;
    rc.right += pt.x;
    rc.top += pt.y;
    rc.bottom += pt.y;
    Import<ClipCursor_t>(0x0056b230)(&rc);
    GameLog(reinterpret_cast<const char*>(0x0058a8fc), rc.left, rc.top, rc.right, rc.bottom);
    return 0;
}

// 0x004f83c0 (esi = pc; ebx/edi/ebp listed by abi.csv only through the /GS epilogue's tail jump to
// __security_check_cookie 0x0053c96a): polls the mouse: GetDeviceState(0x14) into a zeroed DIMOUSESTATE2; on
// DIERR_INPUTLOST / DIERR_NOTACQUIRED Acquire and read again; failure: return. Scale = 1.0 when the global 0x00604a90
// is 0, else 0x005bcc50 / it. Previous x/y/wheel and buttons saved; x/y = count * scale * 0.1 (0x005bcbfc), wheel =
// count; the 8 button bytes' top bits become a mask (bit i = button i): +0x44 = +0x4c = mask, +0x54 = new presses
// (~previous & mask), +0x50 = releases (~mask & previous).
uint32_t InputPC_PollMouse(void* pc)
{
    DIMOUSESTATE2 state;
    std::memset(&state, 0, sizeof(state));
    HRESULT hr = Device(pc, 0x28)->GetDeviceState(0x14, &state);
    if (hr == kInputLost || hr == kNotAcquired)
    {
        Device(pc, 0x28)->Acquire();
        hr = Device(pc, 0x28)->GetDeviceState(0x14, &state);
    }
    if (hr < 0)
        return static_cast<uint32_t>(hr);
    const float setting = GameGlobal<float>(0x00604a90);
    float scale;
    if (setting == K(0x0056c05c))
        scale = K(0x005bcb2c);
    else
        scale = K(0x005bcc50) / setting;
    Field<uint32_t>(pc, 0x40) = Field<uint32_t>(pc, 0x34);
    Field<uint32_t>(pc, 0x3c) = Field<uint32_t>(pc, 0x30);
    Field<uint32_t>(pc, 0x38) = Field<uint32_t>(pc, 0x2c);
    Field<uint32_t>(pc, 0x48) = Field<uint32_t>(pc, 0x44);
    Field<float>(pc, 0x2c) = static_cast<float>(state.lX) * scale * K(0x005bcbfc);
    Field<float>(pc, 0x30) = static_cast<float>(state.lY) * scale * K(0x005bcbfc);
    Field<float>(pc, 0x34) = static_cast<float>(state.lZ);
    const uint8_t* b = state.rgbButtons;
    uint32_t c = (b[1] & 0x80u) | (static_cast<uint32_t>(b[0]) >> 1);
    c >>= 1;
    c |= b[2] & 0x80u;
    c >>= 1;
    c |= b[3] & 0x80u;
    c >>= 1;
    c |= b[4] & 0x80u;
    c >>= 1;
    c |= b[5] & 0x80u;
    c >>= 1;
    c |= b[6] & 0x80u;
    c >>= 1;
    const uint32_t mask = c | (b[7] & 0x80u);
    const uint32_t previous = Field<uint32_t>(pc, 0x48);
    Field<uint32_t>(pc, 0x44) = mask;
    Field<uint32_t>(pc, 0x4c) = mask;
    Field<uint32_t>(pc, 0x54) = ~previous & mask;
    Field<uint32_t>(pc, 0x50) = ~mask & previous;
    return ~mask & previous;
}

// 0x004f8530 (ecx = pc; the other listed inputs and the stack slot come from the /GS epilogue): polls the keyboard:
// GetDeviceState(0x100). DIERR_INPUTLOST / DIERR_NOTACQUIRED: Acquire and return (no retry). Otherwise (also on other
// failures) each key state +0x5c+i steps: down: 0 -> 1, 1 -> 2, 3 -> 1; up: 1 or 2 -> 3, 3 -> 0. Deviation: the
// original's key buffer is uninitialised stack when GetDeviceState fails another way; here it is zeroed.
uint32_t InputPC_PollKeyboard(void* pc)
{
    uint8_t keys[0x100];
    std::memset(keys, 0, sizeof(keys));
    const HRESULT hr = Device(pc, 0x58)->GetDeviceState(0x100, keys);
    if (hr == kInputLost || hr == kNotAcquired)
        return static_cast<uint32_t>(Device(pc, 0x58)->Acquire());
    for (int i = 0; i < 0x100; i++)
    {
        uint8_t& s = Field<uint8_t>(pc, 0x5c + i);
        if (keys[i] & 0x80)
        {
            if (s == 0)
                s = 1;
            else if (s == 1)
                s = 2;
            else if (s == 3)
                s = 1;
        }
        else if (s > 0)
        {
            if (s <= 2)
                s = 3;
            else if (s == 3)
                s = 0;
        }
    }
    return 0x100;
}

// 0x004f8600 (ebx = pc; esi/edi/ebp listed only through the /GS epilogue): polls the controller (+0x160, if any):
// Poll; GetDeviceState(0x34) (DIERR_NOTACQUIRED / DIERR_INPUTLOST: Acquire and read again); failure: return. The
// previous state (+0x1c4) = the current (+0x190); the current = the new one; the four axes recentred by -32255
// (0xffff8201) and scaled by 0x005bcb6c into +0x1f8..+0x204, the previous scaled values moved to +0x208..+0x214.
uint32_t InputPC_PollController(void* pc)
{
    if (!Device(pc, 0x160))
        return 0;
    Device(pc, 0x160)->Poll();
    uint8_t state[0x34];
    HRESULT hr = Device(pc, 0x160)->GetDeviceState(0x34, state);
    if (hr == kNotAcquired || hr == kInputLost)
    {
        Device(pc, 0x160)->Acquire();
        hr = Device(pc, 0x160)->GetDeviceState(0x34, state);
    }
    if (hr < 0)
        return static_cast<uint32_t>(hr);
    uint8_t* base = static_cast<uint8_t*>(pc);
    std::memcpy(base + 0x1c4, base + 0x190, 0x34);
    std::memcpy(base + 0x190, state, 0x34);
    const int32_t x = Field<int32_t>(pc, 0x190) + static_cast<int32_t>(0xffff8201u);
    Field<int32_t>(pc, 0x190) = x;
    const int32_t y = Field<int32_t>(pc, 0x194) + static_cast<int32_t>(0xffff8201u);
    const int32_t z = Field<int32_t>(pc, 0x198) + static_cast<int32_t>(0xffff8201u);
    Field<int32_t>(pc, 0x198) = z;
    const uint32_t oldX = Field<uint32_t>(pc, 0x1f8);
    Field<float>(pc, 0x1f8) = static_cast<float>(x) * K(0x005bcb6c);
    const int32_t r = Field<int32_t>(pc, 0x19c) + static_cast<int32_t>(0xffff8201u);
    Field<int32_t>(pc, 0x19c) = r;
    const uint32_t oldY = Field<uint32_t>(pc, 0x1fc);
    const uint32_t oldZ = Field<uint32_t>(pc, 0x200);
    Field<float>(pc, 0x1fc) = static_cast<float>(y) * K(0x005bcb6c);
    Field<uint32_t>(pc, 0x208) = oldX;
    const uint32_t oldR = Field<uint32_t>(pc, 0x204);
    Field<int32_t>(pc, 0x194) = y;
    Field<uint32_t>(pc, 0x20c) = oldY;
    Field<uint32_t>(pc, 0x210) = oldZ;
    Field<uint32_t>(pc, 0x214) = oldR;
    Field<float>(pc, 0x200) = static_cast<float>(z) * K(0x005bcb6c);
    Field<float>(pc, 0x204) = static_cast<float>(r) * K(0x005bcb6c);
    return 0;
}

// 0x004f8760 (ecx = pc; stack: dt; callee pops 4): virtual +0x1c, the per-frame update: keyboard (0x004f8530), mouse
// (0x004f83c0), controller (0x004f8600), then the rumbles (0x004e5750 with this and dt); returns the latter's eax.
uint32_t InputPC_Update(void* pc, uint32_t dt)
{
    InputPC_PollKeyboard(pc);
    InputPC_PollMouse(pc);
    InputPC_PollController(pc);
    if (g_padPoll)
        g_padPoll(pc);
    return Rumble_Update(u(pc), pc, bf(dt));
}

// 0x004f8790 (stack: 3 unread arguments; callee pops 12): virtual +0xc: returns al = 0. Not executed in recorded runs.
uint32_t InputPC_ReturnFalse3()
{
    return 0;
}

// 0x004f87a0 (ecx = pc; stack: unread device; callee pops 4): virtual +0x38: the first key 1..255 pressed or held
// (state 1 or 2), else 0. Not executed in recorded runs.
uint32_t InputPC_AnyKeyDown(void* pc)
{
    for (uint32_t i = 1; i < 0x100; i++)
    {
        const uint8_t s = Field<uint8_t>(pc, 0x5c + i);
        if (s == 1 || s == 2)
            return i;
    }
    return 0;
}

// 0x004f87c0 (ecx = pc; stack: device; callee pops 4): virtual +0x34: +0x44 (any mouse button), else +0x58 (any
// mouse movement). Not executed in recorded runs.
uint32_t fn_004f87c0(void* pc, uint32_t device)
{
    const uint32_t r = VCall<uint32_t>(pc, 0x44 / 4, device);
    if (r)
        return r;
    return VCall<uint32_t>(pc, 0x58 / 4, device);
}

// 0x004f87e0 (ecx = pc; stack: device; callee pops 4): virtual +0x44: the first mouse button code held (+0x6c,
// threshold 0.5) of 0x10004, 0x10006, 0x10006 (sic: 0x10005 is never tested; original bug), 0x10007..0x1000d; else 0.
// Not executed in recorded runs.
uint32_t InputPC_MouseButtonDown(void* pc, uint32_t device)
{
    static const uint32_t codes[9] = {0x10004, 0x10006, 0x10006, 0x10007, 0x10008, 0x10009, 0x1000a, 0x1000b, 0x1000c};
    for (uint32_t code : codes)
        if (VCall<uint8_t>(pc, 0x6c / 4, device, code, 0x3f000000u))
            return code;
    return VCall<uint8_t>(pc, 0x6c / 4, device, 0x1000du, 0x3f000000u) ? 0x1000du : 0u;
}

// 0x004f8920 (ecx = pc; stack: device; callee pops 4): virtual +0x58: the first mouse movement code 0x10000..0x10003
// held (+0x6c, threshold 0.5), else 0. Not executed in recorded runs.
uint32_t InputPC_MouseMoved(void* pc, uint32_t device)
{
    for (uint32_t code = 0x10000; code < 0x10003; code++)
        if (VCall<uint8_t>(pc, 0x6c / 4, device, code, 0x3f000000u))
            return code;
    return VCall<uint8_t>(pc, 0x6c / 4, device, 0x10003u, 0x3f000000u) ? 0x10003u : 0u;
}

// 0x004f89b0 (ecx = pc; stack: device; callee pops 4): virtual +0x54 (overrides 0x004e4c50): the first controller
// axis code held (+0x6c, threshold 0.75) of -0x28, -0x29, -0x26, -0x27, -0x2d, -0x2e, -0x2b, -0x2c; returns the code,
// except +0x28 for -0x28 (original bug: the base version returns -0x28); else 0. Not executed in recorded runs.
uint32_t InputPC_AxisMoved(void* pc, uint32_t device)
{
    static const int32_t codes[7] = {-0x28, -0x29, -0x26, -0x27, -0x2d, -0x2e, -0x2b};
    for (int32_t code : codes)
        if (VCall<uint8_t>(pc, 0x6c / 4, device, code, 0x3f400000u))
            return code == -0x28 ? 0x28u : static_cast<uint32_t>(code);
    return VCall<uint8_t>(pc, 0x6c / 4, device, -0x2c, 0x3f400000u) ? 0xffffffd4u : 0u;
}

// 0x004f8aa0 (ecx = pc; stack: unread device; callee pops 4): virtual +0x4c: the first controller button -0xc..-1
// held on device 0 (+0x6c, threshold 0.5), then the D-pad codes -0x24..-0x21 (threshold 0); returns the code, else 0.
uint32_t InputPC_ControllerButtonDown(void* pc)
{
    for (int32_t code = -0xc; code <= -1; code++)
        if (VCall<uint8_t>(pc, 0x6c / 4, 0u, code, 0x3f000000u))
            return static_cast<uint32_t>(code);
    for (int32_t code = -0x24; code <= -0x21; code++)
        if (VCall<uint8_t>(pc, 0x6c / 4, 0u, code, 0u))
            return static_cast<uint32_t>(code);
    return 0;
}

// 0x004f8b00 (stack: code; callee pops 4): virtual +0x7c: al = code in 0x10000..0x1000d (signed compares; a mouse
// code). Not executed in recorded runs.
uint32_t InputPC_IsMouseCode(int32_t code)
{
    return code >= 0x10000 && code <= 0x1000d ? 1u : 0u;
}

namespace
{
// D-pad (POV hundredths of a degree) ranges for the codes -0x24..-0x21 (right, left, down, up; up wraps around 0):
// {lo1, hi1, lo2, hi2}; other codes in -0x20..-0xd use the threshold argument's bits for all four (as the original).
void PovRanges(int32_t code, uint32_t threshold, uint32_t r[4])
{
    switch (code + 0x24)
    {
    case 0: r[0] = 0x1194, r[1] = 0x34bc, r[2] = 0x1194, r[3] = 0x34bc; break;
    case 1: r[0] = 0x57e4, r[1] = 0x7b0c, r[2] = 0x57e4, r[3] = 0x7b0c; break;
    case 2: r[0] = 0x34bc, r[1] = 0x57e4, r[2] = 0x34bc, r[3] = 0x57e4; break;
    case 3: r[0] = 0, r[1] = 0x1194, r[2] = 0x7b0c, r[3] = 0x9088; break;
    default: r[0] = r[1] = r[2] = r[3] = threshold; break;
    }
}
bool InPov(const uint32_t r[4], uint32_t v)
{
    return (r[0] <= v && v <= r[1]) || (r[2] <= v && v <= r[3]);
}
// The controller axis behind the codes -0x2e..-0x26 (index code + 0x2e): float offset of the current value (the
// previous one is +0x10 further) and the direction (+1 / -1); index 4 (-0x2a) has none.
const int16_t kAxisOffset[9] = {0x200, 0x200, 0x204, 0x204, 0, 0x1f8, 0x1f8, 0x1fc, 0x1fc};
const int8_t kAxisSign[9] = {1, -1, -1, 1, 0, 1, -1, -1, 1};
// The mouse axis behind the codes 0x10000..0x1000d (index code - 0x10000): float offset (previous +0xc further),
// direction; 0 = a button (0x10004..0x1000b).
const int8_t kMouseOffset[14] = {0x30, 0x30, 0x2c, 0x2c, 0, 0, 0, 0, 0, 0, 0, 0, 0x34, 0x34};
const int8_t kMouseSign[14] = {-1, 1, -1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1};
}  // namespace

// 0x004f8b20 (ecx = pc; stack: device, code, threshold (float); callee pops 12): virtual +0x68, pressed this frame.
// Keys 1..0xff: state == 1. Mouse 0x10000..: axes cross the threshold this frame (x/y/wheel > t now and <= t before, or
// < -t now and >= -t before); other codes: bit (code - 0x10004) of the new presses (+0x54). Controller buttons
// -0xc..-1: down now and not before. D-pad -0x24..-0xd: no current POV (-1) -> 0; no previous POV -> virtual +0x6c
// (held); otherwise previous inside the range and current outside (original bug: that is the release edge; 0x004f91a0
// tests the same). Axes -0x2e..-0x26: >= t now and < t before (or <= -t now and > -t before). Not executed in recorded
// runs.
uint32_t InputPC_IsPressed(void* pc, uint32_t device, int32_t code, uint32_t threshold)
{
    const float t = bf(threshold);
    if (code < 0)
    {
        if (code >= -0xc)
        {
            const int32_t i = -1 - code;
            return (Field<uint8_t>(pc, 0x1a4 + i) & 0x80) && !(Field<uint8_t>(pc, 0x1d8 + i) & 0x80) ? 1u : 0u;
        }
        if (code >= -0x24)
        {
            const uint32_t cur = Field<uint32_t>(pc, 0x1a0);
            if (cur == 0xffffffffu)
                return 0;
            const uint32_t prev = Field<uint32_t>(pc, 0x1d4);
            if (prev == 0xffffffffu)
                return VCall<uint32_t>(pc, 0x6c / 4, device, code, threshold);
            uint32_t r[4];
            PovRanges(code, threshold, r);
            if (!InPov(r, prev))
                return 0;
            return InPov(r, cur) ? 0u : 1u;
        }
        const uint32_t index = static_cast<uint32_t>(code + 0x2e);
        if (index > 8 || kAxisSign[index] == 0)
            return 0;
        const float cur = Field<float>(pc, kAxisOffset[index]);
        const float prev = Field<float>(pc, kAxisOffset[index] + 0x10);
        if (kAxisSign[index] > 0)
        {
            if (!(cur >= t))
                return 0;
            return prev < t ? 1u : 0u;
        }
        const float v = -t;
        if (!(v >= cur))
            return 0;
        return v < prev ? 1u : 0u;
    }
    if (code > 0xffff)
    {
        const uint32_t index = static_cast<uint32_t>(code - 0x10000);
        if (index > 0xd || kMouseSign[index] == 0)
        {
            const uint32_t bit = 1u << ((code - 0x10004) & 0x1f);
            return (Field<uint32_t>(pc, 0x54) & bit) ? 1u : 0u;
        }
        const float cur = Field<float>(pc, kMouseOffset[index]);
        const float prev = Field<float>(pc, kMouseOffset[index] + 0xc);
        if (kMouseSign[index] > 0)
        {
            if (!(cur > t))
                return 0;
            return prev <= t ? 1u : 0u;
        }
        const float v = -t;
        if (!(v > cur))
            return 0;
        return v <= prev ? 1u : 0u;
    }
    if (code == 0 || code >= 0x100)
        return 0;
    return Field<uint8_t>(pc, 0x5c + code) == 1 ? 1u : 0u;
}

// 0x004f8f00 (ecx = pc; stack: device (unread), code, threshold (float); callee pops 12): virtual +0x6c, held. Keys:
// state 1 or 2 (returns eax = 1 / 0). Mouse axes: > t (or < -t); mouse buttons: bit (code - 0x10004) of +0x4c.
// Controller buttons: down now. D-pad: the current POV inside the range (no -1 test). Axes: >= t (or <= -t). Not
// executed in recorded runs (the comparison scenarios do not record virtual calls of this class).
uint32_t InputPC_IsHeld(void* pc, uint32_t device, int32_t code, uint32_t threshold)
{
    (void)device;
    const float t = bf(threshold);
    if (code < 0)
    {
        if (code >= -0xc)
            return (Field<uint8_t>(pc, 0x1a3 - code) & 0x80) ? 1u : 0u;
        if (code >= -0x24)
        {
            uint32_t r[4];
            PovRanges(code, threshold, r);
            return InPov(r, Field<uint32_t>(pc, 0x1a0)) ? 1u : 0u;
        }
        const uint32_t index = static_cast<uint32_t>(code + 0x2e);
        if (index > 8 || kAxisSign[index] == 0)
            return 0;
        const float cur = Field<float>(pc, kAxisOffset[index]);
        if (kAxisSign[index] > 0)
            return cur >= t ? 1u : 0u;
        return -t >= cur ? 1u : 0u;
    }
    if (code > 0xffff)
    {
        const uint32_t index = static_cast<uint32_t>(code - 0x10000);
        if (index > 0xd || kMouseSign[index] == 0)
        {
            const uint32_t bit = 1u << ((code - 0x10004) & 0x1f);
            return (Field<uint32_t>(pc, 0x4c) & bit) ? 1u : 0u;
        }
        const float cur = Field<float>(pc, kMouseOffset[index]);
        if (kMouseSign[index] > 0)
            return cur > t ? 1u : 0u;
        return -t > cur ? 1u : 0u;
    }
    if (code == 0 || code >= 0x100)
        return 0;
    const uint8_t s = Field<uint8_t>(pc, 0x5c + code);
    return s == 1 || s == 2 ? 1u : 0u;
}

// 0x004f91a0 (ecx = pc; stack: device (unread), code, threshold (float); callee pops 12): virtual +0x70, released this
// frame. Keys: state == 3. Mouse axes: <= t now and > t before (or >= -t now and < -t before); buttons: bit of the
// releases (+0x50). Controller buttons: up now, down before. D-pad: no previous POV -> 0; current outside and
// previous inside the range. Axes: < t now and >= t before (or > -t now and <= -t before). Not executed in recorded
// runs.
uint32_t InputPC_IsReleased(void* pc, uint32_t device, int32_t code, uint32_t threshold)
{
    (void)device;
    const float t = bf(threshold);
    if (code < 0)
    {
        if (code >= -0xc)
        {
            const int32_t i = -1 - code;
            return !(Field<uint8_t>(pc, 0x1a4 + i) & 0x80) && (Field<uint8_t>(pc, 0x1d8 + i) & 0x80) ? 1u : 0u;
        }
        if (code >= -0x24)
        {
            const uint32_t prev = Field<uint32_t>(pc, 0x1d4);
            if (prev == 0xffffffffu)
                return 0;
            uint32_t r[4];
            PovRanges(code, threshold, r);
            if (InPov(r, Field<uint32_t>(pc, 0x1a0)))
                return 0;
            return InPov(r, prev) ? 1u : 0u;
        }
        const uint32_t index = static_cast<uint32_t>(code + 0x2e);
        if (index > 8 || kAxisSign[index] == 0)
            return 0;
        const float cur = Field<float>(pc, kAxisOffset[index]);
        const float prev = Field<float>(pc, kAxisOffset[index] + 0x10);
        if (kAxisSign[index] > 0)
        {
            if (!(cur < t))
                return 0;
            return prev >= t ? 1u : 0u;
        }
        const float v = -t;
        if (!(v < cur))
            return 0;
        return v >= prev ? 1u : 0u;
    }
    if (code > 0xffff)
    {
        const uint32_t index = static_cast<uint32_t>(code - 0x10000);
        if (index > 0xd || kMouseSign[index] == 0)
        {
            const uint32_t bit = 1u << ((code - 0x10004) & 0x1f);
            return (Field<uint32_t>(pc, 0x50) & bit) ? 1u : 0u;
        }
        const float cur = Field<float>(pc, kMouseOffset[index]);
        const float prev = Field<float>(pc, kMouseOffset[index] + 0xc);
        if (kMouseSign[index] > 0)
        {
            if (!(cur <= t))
                return 0;
            return prev > t ? 1u : 0u;
        }
        const float v = -t;
        if (!(v <= cur))
            return 0;
        return v > prev ? 1u : 0u;
    }
    if (code == 0 || code >= 0x100)
        return 0;
    return Field<uint8_t>(pc, 0x5c + code) == 3 ? 1u : 0u;
}

// 0x004f9560 (ecx = pc; stack: device, code, deadzone, threshold, scale (floats); callee pops 20; returns st0):
// virtual +0x78, the analogue value. Controller axes -0x2e..-0x26 beyond the dead zone d: (scale / (1 - d)) * (v - d),
// or -((scale / (1 - d)) * (d + v)) on the negative side; else 0. Mouse axes: sensitivity (+0x21c) * value * scale
// when on the code's side of 0 (negated for the negative side), else 0. Other codes (keys, controller buttons, D-pad,
// mouse buttons): scale when held (virtual +0x6c with the threshold), else 0. Not executed in recorded runs.
float InputPC_AxisValue(void* pc, uint32_t device, int32_t code, uint32_t deadzone, uint32_t threshold, uint32_t scale)
{
    const float d = bf(deadzone);
    const float s = bf(scale);
    if (code < 0)
    {
        if (code < -0x24)
        {
            const uint32_t index = static_cast<uint32_t>(code + 0x2e);
            if (index > 8 || kAxisSign[index] == 0)
                return K(0x0056c05c);
            const float v = Field<float>(pc, kAxisOffset[index]);
            if (kAxisSign[index] > 0)
            {
                if (!(v > d))
                    return K(0x0056c05c);
                return s / (K(0x005bcb2c) - d) * (v - d);
            }
            if (!(-d > v))
                return K(0x0056c05c);
            return -(s / (K(0x005bcb2c) - d) * (d + v));
        }
    }
    else if (code > 0xffff)
    {
        const uint32_t index = static_cast<uint32_t>(code - 0x10000);
        if (index <= 0xd && kMouseSign[index] != 0)
        {
            const float v = Field<float>(pc, kMouseOffset[index]);
            if (kMouseSign[index] > 0)
            {
                if (!(v > K(0x0056c05c)))
                    return K(0x0056c05c);
                return Field<float>(pc, 0x21c) * v * s;
            }
            if (!(v < K(0x0056c05c)))
                return K(0x0056c05c);
            return -(Field<float>(pc, 0x21c) * v * s);
        }
    }
    // keys, controller buttons, the D-pad and mouse buttons: digital
    if (VCall<uint8_t>(pc, 0x6c / 4, device, code, threshold))
        return s;
    return K(0x0056c05c);
}

namespace
{
// 0x004f98b0's switch (byte table 0x004f9ba0, jump table 0x004f9ab0) for codes -0x2e..0x32: debug names ("Right Axis
// Right", "DPad Up", "1 Button", "KBD Z", ...); 0x0058a68c = "Unknown Button".
const uint32_t kDebugName[0x61] = {
    0x0058a888, 0x0058a89c, 0x0058a8ac, 0x0058a8ec, 0x0058a68c, 0x0058a8bc, 0x0058a8cc, 0x0058a8dc,
    0x0058a8ec, 0x0058a68c, 0x0058a85c, 0x0058a868, 0x0058a874, 0x0058a880, 0x0058a68c, 0x0058a68c,
    0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c,
    0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c,
    0x0058a68c, 0x0058a68c, 0x0058a7bc, 0x0058a7cc, 0x0058a83c, 0x0058a84c, 0x0058a824, 0x0058a830,
    0x0058a7e8, 0x0058a80c, 0x0058a818, 0x0058a7dc, 0x0058a7f4, 0x0058a800, 0x0058a68c, 0x0058a68c,
    0x0058a7ac, 0x0058a7a4, 0x0058a79c, 0x0058a794, 0x0058a78c, 0x0058a784, 0x0058a77c, 0x0058a774,
    0x0058a76c, 0x0058a7b4, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a6e4, 0x0058a6b4,
    0x0058a744, 0x0058a6dc, 0x0058a6cc, 0x0058a6a4, 0x0058a6c4, 0x0058a724, 0x0058a6f4, 0x0058a6ec,
    0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a68c, 0x0058a764, 0x0058a6d4, 0x0058a74c, 0x0058a73c,
    0x0058a734, 0x0058a72c, 0x0058a71c, 0x0058a714, 0x0058a70c, 0x0058a68c, 0x0058a68c, 0x0058a68c,
    0x0058a68c, 0x0058a68c, 0x0058a69c, 0x0058a6ac, 0x0058a754, 0x0058a6bc, 0x0058a75c, 0x0058a6fc,
    0x0058a704,
};
}  // namespace

// 0x004f98b0 (stack: code; callee pops 4): virtual +0x64: the code's debug name (ASCII); -0x2b reads "Left Axis Up"
// like -0x26 (original data). Codes outside -0x2e..0x32 (unsigned test) -> "Unknown Button". Not executed in recorded
// runs.
uint32_t InputPC_CodeDebugName(uint32_t code)
{
    const uint32_t index = code + 0x2e;
    if (index > 0x60)
        return 0x0058a68c;
    return kDebugName[index];
}

// ---------------------------------------------------------------------------------------------------------------------
// Entry adapters (register inputs in the order eax ecx edx ebx esi edi, then stack arguments)
// ---------------------------------------------------------------------------------------------------------------------

static uint32_t Controls_Init_Abi(uint32_t esi)
{
    return u(Controls_Init(P(esi)));
}
ABI_ENTRY_00414460(Controls_Init_Entry, Controls_Init_Abi)
HOOK(0x00414460, "Controls_Init", Controls_Init_Entry, nullptr);

static uint32_t Controls_LoadDefaultBindings_Abi(uint32_t eax, uint32_t a0)
{
    return Controls_LoadDefaultBindings(eax, P(a0));
}
ABI_ENTRY_004144a0(Controls_LoadDefaultBindings_Entry, Controls_LoadDefaultBindings_Abi)
HOOK(0x004144a0, "Controls_LoadDefaultBindings", Controls_LoadDefaultBindings_Entry, nullptr);

static uint32_t Controls_InvertMouseY_Abi(uint32_t eax)
{
    Controls_InvertMouseY(P(eax));
    return eax;
}
ABI_ENTRY_00414520(Controls_InvertMouseY_Entry, Controls_InvertMouseY_Abi)
HOOK(0x00414520, "Controls_InvertMouseY", Controls_InvertMouseY_Entry, nullptr);

static void fn_00414580_Abi(uint32_t eax)
{
    fn_00414580(P(eax));
}
ABI_ENTRY_00414580(fn_00414580_Entry, fn_00414580_Abi)
HOOK(0x00414580, "fn_00414580", fn_00414580_Entry, nullptr);

static uint32_t Controls_Lock_Abi(uint32_t eax, uint32_t ecx)
{
    return Controls_Lock(P(eax), ecx);
}
ABI_ENTRY_00414590(Controls_Lock_Entry, Controls_Lock_Abi)
HOOK(0x00414590, "Controls_Lock", Controls_Lock_Entry, nullptr);

static uint32_t Controls_Poll_Abi(uint32_t edx, uint32_t edi, uint32_t /*a0*/)
{
    return Controls_Poll(edx, P(edi));
}
ABI_ENTRY_004145c0(Controls_Poll_Entry, Controls_Poll_Abi)
HOOK(0x004145c0, "Controls_Poll", Controls_Poll_Entry, nullptr);

static uint32_t Controls_Consume_Abi(uint32_t eax, uint32_t ecx)
{
    return Controls_Consume(P(eax), static_cast<int32_t>(ecx));
}
ABI_ENTRY_00414680(Controls_Consume_Entry, Controls_Consume_Abi)
HOOK(0x00414680, "Controls_Consume", Controls_Consume_Entry, nullptr);

static uint32_t Controls_ReleaseConsumed_Abi(uint32_t eax, uint32_t /*edx*/)
{
    Controls_ReleaseConsumed(P(eax));
    return 0;  // the original leaves a mask word in eax; its only caller (0x004145c0) ignores it
}
ABI_ENTRY_004146c0(Controls_ReleaseConsumed_Entry, Controls_ReleaseConsumed_Abi)
HOOK(0x004146c0, "Controls_ReleaseConsumed", Controls_ReleaseConsumed_Entry, nullptr);

static uint32_t Controls_IsPressed_Abi(uint32_t eax, uint32_t ebx, uint32_t esi, uint32_t a0)
{
    return Controls_IsPressed(eax, static_cast<int32_t>(ebx), P(esi), a0);
}
ABI_ENTRY_00414730(Controls_IsPressed_Entry, Controls_IsPressed_Abi)
HOOK(0x00414730, "Controls_IsPressed", Controls_IsPressed_Entry, nullptr);

static uint32_t Controls_IsHeld_Abi(uint32_t eax, uint32_t ebx, uint32_t esi, uint32_t a0)
{
    return Controls_IsHeld(eax, static_cast<int32_t>(ebx), P(esi), a0);
}
ABI_ENTRY_00414810(Controls_IsHeld_Entry, Controls_IsHeld_Abi)
HOOK(0x00414810, "Controls_IsHeld", Controls_IsHeld_Entry, nullptr);

static uint32_t Controls_IsReleased_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return Controls_IsReleased(eax, P(esi), static_cast<int32_t>(edi), a0);
}
ABI_ENTRY_004148f0(Controls_IsReleased_Entry, Controls_IsReleased_Abi)
HOOK(0x004148f0, "Controls_IsReleased", Controls_IsReleased_Entry, nullptr);

static uint32_t Controls_AnyKey_Abi(uint32_t edi)
{
    return Controls_AnyKey(P(edi));
}
ABI_ENTRY_004149d0(Controls_AnyKey_Entry, Controls_AnyKey_Abi)
HOOK(0x004149d0, "Controls_AnyKey", Controls_AnyKey_Entry, nullptr);

static float Controls_AxisMax_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Controls_AxisMax(P(esi), static_cast<int32_t>(edi), a0, a1, a2);
}
ABI_ENTRY_00414a30(Controls_AxisMax_Entry, Controls_AxisMax_Abi)
HOOK(0x00414a30, "Controls_AxisMax", Controls_AxisMax_Entry, nullptr);

static float Controls_AxisMaxCapped_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Controls_AxisMaxCapped(P(esi), static_cast<int32_t>(edi), a0, a1, a2);
}
ABI_ENTRY_00414aa0(Controls_AxisMaxCapped_Entry, Controls_AxisMaxCapped_Abi)
HOOK(0x00414aa0, "Controls_AxisMaxCapped", Controls_AxisMaxCapped_Entry, nullptr);

static uint32_t Controls_IsCodeHeld_Abi(uint32_t edi, uint32_t a0, uint32_t a1)
{
    return Controls_IsCodeHeld(P(edi), a0, a1);
}
ABI_ENTRY_00414b20(Controls_IsCodeHeld_Entry, Controls_IsCodeHeld_Abi)
HOOK(0x00414b20, "Controls_IsCodeHeld", Controls_IsCodeHeld_Entry, nullptr);

static uint32_t Controls_IsCodePressed_Abi(uint32_t edi, uint32_t a0, uint32_t a1)
{
    return Controls_IsCodePressed(P(edi), a0, a1);
}
ABI_ENTRY_00414b90(Controls_IsCodePressed_Entry, Controls_IsCodePressed_Abi)
HOOK(0x00414b90, "Controls_IsCodePressed", Controls_IsCodePressed_Entry, nullptr);

static float fn_00414c00_Abi(uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return fn_00414c00(ecx, static_cast<const float*>(P(esi)), bf(a0));
}
ABI_ENTRY_00414c00(fn_00414c00_Entry, fn_00414c00_Abi)
HOOK(0x00414c00, "fn_00414c00", fn_00414c00_Entry, nullptr);

static uint32_t Input_CodeNameId_Abi(uint32_t eax)
{
    return Input_CodeNameId(eax);
}
ABI_ENTRY_00414c90(Input_CodeNameId_Entry, Input_CodeNameId_Abi)
HOOK(0x00414c90, "Input_CodeNameId", Input_CodeNameId_Entry, nullptr);

static uint32_t Input_CodeText_Abi(uint32_t eax)
{
    return Input_CodeText(eax);
}
ABI_ENTRY_00415580(Input_CodeText_Entry, Input_CodeText_Abi)
HOOK(0x00415580, "Input_CodeText", Input_CodeText_Entry, nullptr);

static uint32_t Controls_ActionNameId_Abi(uint32_t eax)
{
    return Controls_ActionNameId(eax);
}
ABI_ENTRY_004155a0(Controls_ActionNameId_Entry, Controls_ActionNameId_Abi)
HOOK(0x004155a0, "Controls_ActionNameId", Controls_ActionNameId_Entry, nullptr);

static uint32_t Controls_ActionText_Abi(uint32_t eax)
{
    return Controls_ActionText(eax);
}
ABI_ENTRY_00415720(Controls_ActionText_Entry, Controls_ActionText_Abi)
HOOK(0x00415720, "Controls_ActionText", Controls_ActionText_Entry, nullptr);

static uint32_t Controls_BindingText_Abi(uint32_t ecx, uint32_t edx)
{
    return Controls_BindingText(static_cast<int32_t>(ecx), P(edx));
}
ABI_ENTRY_00415740(Controls_BindingText_Entry, Controls_BindingText_Abi)
HOOK(0x00415740, "Controls_BindingText", Controls_BindingText_Entry, nullptr);

static uint32_t Controls_Binding_Abi(uint32_t ecx, uint32_t edx)
{
    return Controls_Binding(static_cast<int32_t>(ecx), P(edx));
}
ABI_ENTRY_00415750(Controls_Binding_Entry, Controls_Binding_Abi)
HOOK(0x00415750, "Controls_Binding", Controls_Binding_Entry, nullptr);

static uint32_t Fx_PlayForce_Abi(uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2,
                                 uint32_t a3)
{
    return Fx_PlayForce(ebx, P(esi), edi, P(a0), a1, a2, a3);
}
ABI_ENTRY_0041ff60(Fx_PlayForce_Entry, Fx_PlayForce_Abi)
HOOK(0x0041ff60, "Fx_PlayForce", Fx_PlayForce_Entry, nullptr);

static uint32_t Input_BaseDestroy_Abi(uint32_t edi)
{
    return Input_BaseDestroy(P(edi));
}
ABI_ENTRY_004e48a0(Input_BaseDestroy_Entry, Input_BaseDestroy_Abi)
HOOK(0x004e48a0, "Input_BaseDestroy", Input_BaseDestroy_Entry, nullptr);

static uint32_t Input_Shutdown_Abi(uint32_t ecx)
{
    return Input_Shutdown(P(ecx));
}
ABI_ENTRY_004e4930(Input_Shutdown_Entry, Input_Shutdown_Abi)
HOOK(0x004e4930, "Input_Shutdown", Input_Shutdown_Entry, nullptr);

static uint32_t Input_BaseInit_Abi(uint32_t ecx, uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/,
                                   uint32_t /*a4*/, uint32_t /*a5*/, uint32_t /*a6*/)
{
    return Input_BaseInit(P(ecx), a0);
}
ABI_ENTRY_004e49e0(Input_BaseInit_Entry, Input_BaseInit_Abi)
HOOK(0x004e49e0, "Input_BaseInit", Input_BaseInit_Entry, nullptr);

static uint32_t fn_004e4b00_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004e4b00(P(ecx), a0);
}
ABI_ENTRY_004e4b00(fn_004e4b00_Entry, fn_004e4b00_Abi)
HOOK(0x004e4b00, "fn_004e4b00", fn_004e4b00_Entry, nullptr);

static uint32_t fn_004e4b40_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004e4b40(P(ecx), a0);
}
ABI_ENTRY_004e4b40(fn_004e4b40_Entry, fn_004e4b40_Abi)
HOOK(0x004e4b40, "fn_004e4b40", fn_004e4b40_Entry, nullptr);

static uint32_t Input_AnyInput_Abi(uint32_t ecx, uint32_t a0)
{
    return Input_AnyInput(P(ecx), a0);
}
ABI_ENTRY_004e4b60(Input_AnyInput_Entry, Input_AnyInput_Abi)
HOOK(0x004e4b60, "Input_AnyInput", Input_AnyInput_Entry, nullptr);

static uint32_t fn_004e4ba0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004e4ba0(P(ecx), a0);
}
ABI_ENTRY_004e4ba0(fn_004e4ba0_Entry, fn_004e4ba0_Abi)
HOOK(0x004e4ba0, "fn_004e4ba0", fn_004e4ba0_Entry, nullptr);

static uint32_t fn_004e4bc0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004e4bc0(P(ecx), a0);
}
ABI_ENTRY_004e4bc0(fn_004e4bc0_Entry, fn_004e4bc0_Abi)
HOOK(0x004e4bc0, "fn_004e4bc0", fn_004e4bc0_Entry, nullptr);

static uint32_t fn_004e4bf0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004e4bf0(P(ecx), a0);
}
ABI_ENTRY_004e4bf0(fn_004e4bf0_Entry, fn_004e4bf0_Abi)
HOOK(0x004e4bf0, "fn_004e4bf0", fn_004e4bf0_Entry, nullptr);

static uint32_t fn_004e4c20_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004e4c20(P(ecx), a0);
}
ABI_ENTRY_004e4c20(fn_004e4c20_Entry, fn_004e4c20_Abi)
HOOK(0x004e4c20, "fn_004e4c20", fn_004e4c20_Entry, nullptr);

static uint32_t fn_004e4c50_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004e4c50(P(ecx), a0);
}
ABI_ENTRY_004e4c50(fn_004e4c50_Entry, fn_004e4c50_Abi)
HOOK(0x004e4c50, "fn_004e4c50", fn_004e4c50_Entry, nullptr);

static uint32_t Input_IsUp_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Input_IsUp(P(ecx), a0, a1, a2);
}
ABI_ENTRY_004e4d40(Input_IsUp_Entry, Input_IsUp_Abi)
HOOK(0x004e4d40, "Input_IsUp", Input_IsUp_Entry, nullptr);

static uint32_t Rumble_Start_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Rumble_Start(eax, ecx, P(a0), a1, a2, a3);
}
ABI_ENTRY_004e5020(Rumble_Start_Entry, Rumble_Start_Abi)
HOOK(0x004e5020, "Rumble_Start", Rumble_Start_Entry, nullptr);

static uint32_t Rumble_Stop_Abi(uint32_t a0, uint32_t a1)
{
    return Rumble_Stop(P(a0), static_cast<uint32_t*>(P(a1)));
}
ABI_ENTRY_004e5130(Rumble_Stop_Entry, Rumble_Stop_Abi)
HOOK(0x004e5130, "Rumble_Stop", Rumble_Stop_Entry, nullptr);

static uint32_t Rumble_SetGain_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Rumble_SetGain(P(a0), a1, a2);
}
ABI_ENTRY_004e51f0(Rumble_SetGain_Entry, Rumble_SetGain_Abi)
HOOK(0x004e51f0, "Rumble_SetGain", Rumble_SetGain_Entry, nullptr);

static uint32_t Rumble_MotorValues_Abi(uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Rumble_MotorValues(P(ecx), P(edx), a0, static_cast<float*>(P(a1)), static_cast<float*>(P(a2)));
}
ABI_ENTRY_004e5460(Rumble_MotorValues_Entry, Rumble_MotorValues_Abi)
HOOK(0x004e5460, "Rumble_MotorValues", Rumble_MotorValues_Entry, nullptr);

static float Rumble_Envelope_Abi(uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1)
{
    return Rumble_Envelope(static_cast<const float*>(P(ecx)), P(edx), a0, bf(a1));
}
ABI_ENTRY_004e5610(Rumble_Envelope_Entry, Rumble_Envelope_Abi)
HOOK(0x004e5610, "Rumble_Envelope", Rumble_Envelope_Entry, nullptr);

static uint32_t Rumble_PauseAll_Abi(uint32_t a0, uint32_t a1)
{
    return Rumble_PauseAll(P(a0), a1);
}
ABI_ENTRY_004e56c0(Rumble_PauseAll_Entry, Rumble_PauseAll_Abi)
HOOK(0x004e56c0, "Rumble_PauseAll", Rumble_PauseAll_Entry, nullptr);

static uint32_t Rumble_Update_Abi(uint32_t ebx, uint32_t a0, uint32_t a1)
{
    return Rumble_Update(ebx, P(a0), bf(a1));
}
ABI_ENTRY_004e5750(Rumble_Update_Entry, Rumble_Update_Abi)
HOOK(0x004e5750, "Rumble_Update", Rumble_Update_Entry, nullptr);

static uint32_t InputPC_Get_Abi()
{
    return InputPC_Get();
}
ABI_ENTRY_004f7b30(InputPC_Get_Entry, InputPC_Get_Abi)
HOOK(0x004f7b30, "InputPC_Get", InputPC_Get_Entry, nullptr);

static uint32_t InputPC_Release_Abi()
{
    return InputPC_Release();
}
ABI_ENTRY_004f7b80(InputPC_Release_Entry, InputPC_Release_Abi)
HOOK(0x004f7b80, "InputPC_Release", InputPC_Release_Entry, nullptr);

static uint32_t InputPC_Construct_Abi(uint32_t edx)
{
    return InputPC_Construct(P(edx));
}
ABI_ENTRY_004f7bb0(InputPC_Construct_Entry, InputPC_Construct_Abi)
HOOK(0x004f7bb0, "InputPC_Construct", InputPC_Construct_Entry, nullptr);

static uint32_t InputPC_Destroy_Abi(uint32_t a0)
{
    return InputPC_Destroy(P(a0));
}
ABI_ENTRY_004f7c20(InputPC_Destroy_Entry, InputPC_Destroy_Abi)
HOOK(0x004f7c20, "InputPC_Destroy", InputPC_Destroy_Entry, nullptr);

static uint32_t InputPC_SetMouseSensitivity_Abi(uint32_t ecx, uint32_t a0)
{
    return InputPC_SetMouseSensitivity(P(ecx), a0);
}
ABI_ENTRY_004f7cf0(InputPC_SetMouseSensitivity_Entry, InputPC_SetMouseSensitivity_Abi)
HOOK(0x004f7cf0, "InputPC_SetMouseSensitivity", InputPC_SetMouseSensitivity_Entry, nullptr);

static uint32_t InputPC_InitController_Abi(uint32_t ebx)
{
    return InputPC_InitController(P(ebx));
}
ABI_ENTRY_004f7e10(InputPC_InitController_Entry, InputPC_InitController_Abi)
HOOK(0x004f7e10, "InputPC_InitController", InputPC_InitController_Entry, nullptr);

static uint32_t InputPC_Init_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                 uint32_t a5, uint32_t a6)
{
    return InputPC_Init(P(ecx), a0, a1, a2, a3, a4, a5, a6);
}
ABI_ENTRY_004f81d0(InputPC_Init_Entry, InputPC_Init_Abi)
HOOK(0x004f81d0, "InputPC_Init", InputPC_Init_Entry, nullptr);

static uint32_t InputPC_SetMouseCapture_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return InputPC_SetMouseCapture(P(ecx), a0, a1);
}
ABI_ENTRY_004f82d0(InputPC_SetMouseCapture_Entry, InputPC_SetMouseCapture_Abi)
HOOK(0x004f82d0, "InputPC_SetMouseCapture", InputPC_SetMouseCapture_Entry, nullptr);

static uint32_t InputPC_PollMouse_Abi(uint32_t /*ebx*/, uint32_t esi, uint32_t /*edi*/, uint32_t /*ebp*/)
{
    return InputPC_PollMouse(P(esi));
}
ABI_ENTRY_004f83c0(InputPC_PollMouse_Entry, InputPC_PollMouse_Abi)
HOOK(0x004f83c0, "InputPC_PollMouse", InputPC_PollMouse_Entry, nullptr);

static uint32_t InputPC_PollKeyboard_Abi(uint32_t ecx, uint32_t /*ebx*/, uint32_t /*edi*/, uint32_t /*ebp*/,
                                         uint32_t /*a0*/)
{
    return InputPC_PollKeyboard(P(ecx));
}
ABI_ENTRY_004f8530(InputPC_PollKeyboard_Entry, InputPC_PollKeyboard_Abi)
HOOK(0x004f8530, "InputPC_PollKeyboard", InputPC_PollKeyboard_Entry, nullptr);

static uint32_t InputPC_PollController_Abi(uint32_t ebx, uint32_t /*esi*/, uint32_t /*edi*/, uint32_t /*ebp*/)
{
    return InputPC_PollController(P(ebx));
}
ABI_ENTRY_004f8600(InputPC_PollController_Entry, InputPC_PollController_Abi)
HOOK(0x004f8600, "InputPC_PollController", InputPC_PollController_Entry, nullptr);

static uint32_t InputPC_Update_Abi(uint32_t ecx, uint32_t a0)
{
    return InputPC_Update(P(ecx), a0);
}
ABI_ENTRY_004f8760(InputPC_Update_Entry, InputPC_Update_Abi)
HOOK(0x004f8760, "InputPC_Update", InputPC_Update_Entry, nullptr);

static uint32_t InputPC_ReturnFalse3_Abi(uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/)
{
    return InputPC_ReturnFalse3();
}
ABI_ENTRY_004f8790(InputPC_ReturnFalse3_Entry, InputPC_ReturnFalse3_Abi)
HOOK(0x004f8790, "InputPC_ReturnFalse3", InputPC_ReturnFalse3_Entry, nullptr);

static uint32_t InputPC_AnyKeyDown_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return InputPC_AnyKeyDown(P(ecx));
}
ABI_ENTRY_004f87a0(InputPC_AnyKeyDown_Entry, InputPC_AnyKeyDown_Abi)
HOOK(0x004f87a0, "InputPC_AnyKeyDown", InputPC_AnyKeyDown_Entry, nullptr);

static uint32_t fn_004f87c0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f87c0(P(ecx), a0);
}
ABI_ENTRY_004f87c0(fn_004f87c0_Entry, fn_004f87c0_Abi)
HOOK(0x004f87c0, "fn_004f87c0", fn_004f87c0_Entry, nullptr);

static uint32_t InputPC_MouseButtonDown_Abi(uint32_t ecx, uint32_t a0)
{
    return InputPC_MouseButtonDown(P(ecx), a0);
}
ABI_ENTRY_004f87e0(InputPC_MouseButtonDown_Entry, InputPC_MouseButtonDown_Abi)
HOOK(0x004f87e0, "InputPC_MouseButtonDown", InputPC_MouseButtonDown_Entry, nullptr);

static uint32_t InputPC_MouseMoved_Abi(uint32_t ecx, uint32_t a0)
{
    return InputPC_MouseMoved(P(ecx), a0);
}
ABI_ENTRY_004f8920(InputPC_MouseMoved_Entry, InputPC_MouseMoved_Abi)
HOOK(0x004f8920, "InputPC_MouseMoved", InputPC_MouseMoved_Entry, nullptr);

static uint32_t InputPC_AxisMoved_Abi(uint32_t ecx, uint32_t a0)
{
    return InputPC_AxisMoved(P(ecx), a0);
}
ABI_ENTRY_004f89b0(InputPC_AxisMoved_Entry, InputPC_AxisMoved_Abi)
HOOK(0x004f89b0, "InputPC_AxisMoved", InputPC_AxisMoved_Entry, nullptr);

static uint32_t InputPC_ControllerButtonDown_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return InputPC_ControllerButtonDown(P(ecx));
}
ABI_ENTRY_004f8aa0(InputPC_ControllerButtonDown_Entry, InputPC_ControllerButtonDown_Abi)
HOOK(0x004f8aa0, "InputPC_ControllerButtonDown", InputPC_ControllerButtonDown_Entry, nullptr);

static uint32_t InputPC_IsMouseCode_Abi(uint32_t a0)
{
    return InputPC_IsMouseCode(static_cast<int32_t>(a0));
}
ABI_ENTRY_004f8b00(InputPC_IsMouseCode_Entry, InputPC_IsMouseCode_Abi)
HOOK(0x004f8b00, "InputPC_IsMouseCode", InputPC_IsMouseCode_Entry, nullptr);

static uint32_t InputPC_IsPressed_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return InputPC_IsPressed(P(ecx), a0, static_cast<int32_t>(a1), a2);
}
ABI_ENTRY_004f8b20(InputPC_IsPressed_Entry, InputPC_IsPressed_Abi)
HOOK(0x004f8b20, "InputPC_IsPressed", InputPC_IsPressed_Entry, nullptr);

static uint32_t InputPC_IsHeld_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return InputPC_IsHeld(P(ecx), a0, static_cast<int32_t>(a1), a2);
}
ABI_ENTRY_004f8f00(InputPC_IsHeld_Entry, InputPC_IsHeld_Abi)
HOOK(0x004f8f00, "InputPC_IsHeld", InputPC_IsHeld_Entry, nullptr);

static uint32_t InputPC_IsReleased_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return InputPC_IsReleased(P(ecx), a0, static_cast<int32_t>(a1), a2);
}
ABI_ENTRY_004f91a0(InputPC_IsReleased_Entry, InputPC_IsReleased_Abi)
HOOK(0x004f91a0, "InputPC_IsReleased", InputPC_IsReleased_Entry, nullptr);

static float InputPC_AxisValue_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return InputPC_AxisValue(P(ecx), a0, static_cast<int32_t>(a1), a2, a3, a4);
}
ABI_ENTRY_004f9560(InputPC_AxisValue_Entry, InputPC_AxisValue_Abi)
HOOK(0x004f9560, "InputPC_AxisValue", InputPC_AxisValue_Entry, nullptr);

static uint32_t InputPC_CodeDebugName_Abi(uint32_t a0)
{
    return InputPC_CodeDebugName(a0);
}
ABI_ENTRY_004f98b0(InputPC_CodeDebugName_Entry, InputPC_CodeDebugName_Abi)
HOOK(0x004f98b0, "InputPC_CodeDebugName", InputPC_CodeDebugName_Entry, nullptr);
