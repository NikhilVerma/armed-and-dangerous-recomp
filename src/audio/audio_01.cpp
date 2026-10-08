// Recovered: audio-classified game functions, batch audio_01 (0x004058f0-0x0047bb30). Static recovery 2026-10-05,
// compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified from the disassembly.
//
// Game+0x2c is the audio object (PC audio, vtable 0x0058a230; Ghidra calls the member renderer_2c, see
// docs/subsystems/qol.md). Audio virtuals used here (callee pops, checked from their `ret N`):
//   +0xc4 0x004f3670 (name, ..., 5 more) ret 0x18: play a named sound (guess: "PE150042_PeasantPickUp" style cues)
//   +0xd0 0x004f4030 (6 args) ret 0x18; +0xdc 0x004f4380 (1 arg) ret 4, returns al.
// Class names in comments come from the vtable that holds the function (slot 1's descriptor +0x14 string); where a
// function sits in several vtables, the class names are listed as found (vtable starts are found by scanning
// backwards over code pointers, so they are a guess when vtables are adjacent).
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
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
inline void* Audio()
{
    return Field<void*>(g_game, 0x2c);
}
inline const char* S(uint32_t address)
{
    return reinterpret_cast<const char*>(address);
}
// 0x005448ee _stricmp (cdecl). Its "ecx input" is `push ecx` reserving a local, not an argument: pass 0.
inline int32_t Stricmp(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(orig::fn_005448ee(0, a, b));
}
// 0x005391cd rand (cdecl): returns eax.
inline int32_t Rand()
{
    return static_cast<int32_t>(static_cast<uint32_t>(orig::fn_005391cd()));
}
// Global game clock (seconds) read by many functions here.
inline float GameTime()
{
    return GameGlobal<float>(0x00604a98);
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 0x004058f0 (edi = object picked up; caller 0x00405d10; not executed in recorded runs). Plays "ui_peasant_pickup"
// (audio +0xc4 (0x005bbe84, edi, 0, 0, -1, 0)); then, at most every K(0x005bcb68) seconds (Game+0xbcc keeps the time
// of the last line: fsub/fcomp, returns when below or unordered), picks a random voice line: range base
// 0x834 when the object's name (0x00453270) is not "peasant_man" (0x005bbe78), span 0x95f+1 (+0xc9 when
// 0x00475d40(Game+0x748) is non-null); r = (rand() * span) >> 15 (signed) + base; each 100 of r is one of the
// "PE150042_PeasantPickUp".. strings (audio +0xc4 (name, edi, 0, 0, -1, 0)), 0x834..0x95f the "ROM5019x" lines
// (audio +0xd0 (name, Game+0x748, 0, 0, -1, 0)), then "Q1150178"/"Q1150180" (audio +0xc4 (name, that object, 0, 0,
// -1, 0)). The four trailing arguments are pushed once before the branches from 0x320 on (0x00405acf).
// Returns the audio call's eax (early exit: the fnstsw word, 0 here).
uint32_t fn_004058f0(uint32_t object)
{
    VCall<uint32_t>(Audio(), 0xc4 / 4, 0x005bbe84u, object, 0u, 0u, 0xffffffffu, 0u);
    const float now = GameTime();
    const float elapsed = now - Field<float>(g_game, 0xbcc);
    if (!(elapsed >= K(0x005bcb68)))
        return 0;
    Field<float>(g_game, 0xbcc) = now;
    const uint32_t name = orig::fn_00453270(object);
    const uint32_t base = Stricmp(name, 0x005bbe78u) != 0 ? 0x834u : 0u;
    const uint32_t other = orig::fn_00475d40(Field<uint32_t>(g_game, 0x748));
    const uint32_t span = (other != 0 ? 0xc9u : 0u) + 0x95fu - base + 1u;
    const uint32_t r = static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(Rand()) * span) >> 15) + base;
    static const uint32_t kPeasant[] = {0x005bbe60, 0x005bbe48, 0x005bbe30, 0x005bbe18, 0x005bbe00, 0x005bbde8,
                                        0x005bbdd0, 0x005bbdb8, 0x005bbda0, 0x005bbd88, 0x005bbd70, 0x005bbd58,
                                        0x005bbd40, 0x005bbd28, 0x005bbd10, 0x005bbcf8, 0x005bbce0, 0x005bbcc8,
                                        0x005bbcb0, 0x005bbc98, 0x005bbc80};
    if (r < 0x834)
        return VCall<uint32_t>(Audio(), 0xc4 / 4, kPeasant[r / 100], object, 0u, 0u, 0xffffffffu, 0u);
    if (r < 0x898)
        return VCall<uint32_t>(Audio(), 0xd0 / 4, 0x005bbc68u, Field<uint32_t>(g_game, 0x748), 0u, 0u, 0xffffffffu, 0u);
    if (r < 0x8fc)
        return VCall<uint32_t>(Audio(), 0xd0 / 4, 0x005bbc50u, Field<uint32_t>(g_game, 0x748), 0u, 0u, 0xffffffffu, 0u);
    if (r < 0x960)
        return VCall<uint32_t>(Audio(), 0xd0 / 4, 0x005bbc38u, Field<uint32_t>(g_game, 0x748), 0u, 0u, 0xffffffffu, 0u);
    if (r < 0x9c4)
        return VCall<uint32_t>(Audio(), 0xc4 / 4, 0x005bbc20u, other, 0u, 0u, 0xffffffffu, 0u);
    return VCall<uint32_t>(Audio(), 0xc4 / 4, 0x005bbc08u, other, 0u, 0u, 0xffffffffu, 0u);
}

static uint32_t fn_004058f0_Abi(uint32_t edi)
{
    return fn_004058f0(edi);
}
ABI_ENTRY_004058f0(fn_004058f0_Entry, fn_004058f0_Abi)
HOOK(0x004058f0, "fn_004058f0", fn_004058f0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0040d250 (cdecl; stack: name, outName, outObject, outFlag; caller pops; executed in recorded runs). Dialogue
// speaker check: a name starting with 'R' returns true. A name starting with 'J' takes the character
// 0x00475d30(Game+0x748) ("JON" lines), any other 0x00475d40(Game+0x748) ("Q11" lines). The character must exist,
// 0x00454780 (esi = it) must be false and audio +0xdc (character+0xb50) false; then when the level object
// (Game+0xa84 +0xf04) virtual +0x34 returns non-zero, or the name is not "JON50172_Dismay" (0x005bb29c) /
// "Q1150162_Dismay" (0x005bb1fc), it stores outName = character+0xb50, outObject = character, outFlag = 0 and
// returns true; otherwise false. Returns al only (upper bits of eax are not reproduced).
uint32_t fn_0040d250(const char* name, uint32_t* outName, uint32_t* outObject, uint8_t* outFlag)
{
    if (name[0] == 'R')
        return 1;
    const bool jon = name[0] == 'J';
    const uint32_t ch = jon ? orig::fn_00475d30(Field<uint32_t>(g_game, 0x748))
                            : orig::fn_00475d40(Field<uint32_t>(g_game, 0x748));
    if (!ch)
        return 0;
    if (static_cast<uint8_t>(orig::fn_00454780(ch)))
        return 0;
    if (static_cast<uint8_t>(VCall<uint32_t>(Audio(), 0xdc / 4, Field<uint32_t>(P(ch), 0xb50))))
        return 0;
    void* level = Field<void*>(Field<void*>(g_game, 0xa84), 0xf04);
    if (VCall<uint32_t>(level, 0x34 / 4) == 0)
    {
        if (Stricmp(jon ? 0x005bb29cu : 0x005bb1fcu, u(name)) == 0)
            return 0;
    }
    *outName = ch + 0xb50;
    *outFlag = 0;
    *outObject = ch;
    return 1;
}

static uint32_t fn_0040d250_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0040d250(S(a0), reinterpret_cast<uint32_t*>(a1), reinterpret_cast<uint32_t*>(a2),
                       reinterpret_cast<uint8_t*>(a3));
}
ABI_ENTRY_0040d250(fn_0040d250_Entry, fn_0040d250_Abi)
HOOK(0x0040d250, "fn_0040d250", fn_0040d250_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0041bd70 (ecx = object; stack: outPosition, arg1, arg2 (unused); callee pops 12; virtual, vtable data
// 0x005a5328 (FxObject +0x50); not executed in recorded runs). When Game+0x2684 is 0: Body_getLinearVelocity
// (eax = object, ecx = arg1), then 0x00453c50(object, object+0x558, &matrix, 1, 0, 0, 1, 0, 1) fills a local 0x44-byte
// matrix and its translation (m[3], m[7], m[11]) is copied to outPosition (first one through fld/fstp).
uint32_t fn_0041bd70(uint8_t* object, float* out, uint32_t arg1)
{
    if (Field<uint32_t>(g_game, 0x2684) != 0)
        return u(g_game);  // eax still holds Game (mov eax, [0x5fe490])
    orig::fn_00458000(u(object), arg1);
    float m[17];
    orig::fn_00453c50(u(object), Field<uint32_t>(object, 0x558), u(m), 1, 0, 0, 1, 0, 1);
    out[0] = m[3];
    uint32_t* o = reinterpret_cast<uint32_t*>(out);
    std::memcpy(&o[1], &m[7], 4);
    std::memcpy(&o[2], &m[11], 4);
    return u(out);
}

static uint32_t fn_0041bd70_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t /*a2*/)
{
    return fn_0041bd70(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<float*>(a0), a1);
}
ABI_ENTRY_0041bd70(fn_0041bd70_Entry, fn_0041bd70_Abi)
HOOK(0x0041bd70, "fn_0041bd70", fn_0041bd70_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0041bdd0 (ecx = object; stack: a0, a1, props, a3; callee pops 16; virtual, vtable data 0x005a8f88 =
// FxEarObject +0x18 (slot 1 0x00461c80 -> "CLASS(FxEarObject)"); not executed in recorded runs). FxEarObject property
// load (strings): base load 0x004517b0(a0, a1, props, a3); flags +0x49c = 0; floats "delay_repeat" -> +0x464,
// "delay_initial_max" -> +0x468, "delay_initial_min" -> +0x46c (0x004534a0, esi = a3, edi = destination);
// booleans "looping_fx" (bit 0), "one_shot" (bit 1), "lock_fx" (bit 3) through 0x00453400 (eax = 0, esi = a3, edi =
// the a3 stack slot, reused as the output); string "fx_name" -> +0x470 (0x004533a0, eax = default 0x005bb9e0);
// "trigger_id" -> +0x460 (0x00453450, ebx = name). Clears +0x490/+0x494/+0x498. With a trigger and a name other
// than 0x005bb9e0 it calls virtual +0xf4(0); otherwise logs "FxEarObject: Bad properties..." (0x00407ff0) and calls
// virtual +0xdc().
uint32_t FxEarObject_LoadProperties(uint8_t* self, uint32_t a0, uint32_t a1, uint32_t props, uint32_t a3)
{
    uint32_t slot = a3;  // the original reuses its a3 argument slot as the boolean output
    orig::fn_004517b0(u(self), a0, a1, props, a3);
    Field<uint32_t>(self, 0x49c) = 0;
    orig::fn_004534a0(a3, u(self + 0x464), u(self), 0x005b9540u, props, 0, 0);
    orig::fn_004534a0(a3, u(self + 0x468), u(self), 0x005b952cu, props, 0, 0);
    orig::fn_004534a0(a3, u(self + 0x46c), u(self), 0x005b9518u, props, 0, 0);
    orig::fn_00453400(0, a3, u(&slot), u(self), 0x005b950cu, props, 0);
    if (slot)
        Field<uint32_t>(self, 0x49c) |= 1;
    orig::fn_00453400(0, a3, u(&slot), u(self), 0x005b9500u, props, 0);
    if (slot)
        Field<uint32_t>(self, 0x49c) |= 2;
    orig::fn_004533a0(0x005bb9e0u, u(self + 0x470), u(self), 0x005bad70u, props, a3, 0);
    orig::fn_00453450(0, 0x005b94f4u, a3, u(self + 0x460), u(self), props, 0);
    orig::fn_00453400(0, a3, u(&slot), u(self), 0x005b94ecu, props, 0);
    if (slot)
        Field<uint32_t>(self, 0x49c) |= 8;
    Field<uint32_t>(self, 0x498) = 0;
    Field<uint32_t>(self, 0x494) = 0;
    Field<uint32_t>(self, 0x490) = 0;
    if (Field<uint32_t>(self, 0x460) != 0 && Stricmp(u(self + 0x470), 0x005bb9e0u) != 0)
        return VCall<uint32_t>(self, 0xf4 / 4, 0u);
    orig::fn_00407ff0(Field<uint32_t>(g_game, 0x34), 0x005b94b8u, Field<uint32_t>(self, 0x460), u(self + 0x470));
    return VCall<uint32_t>(self, 0xdc / 4);
}

static uint32_t FxEarObject_LoadProperties_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return FxEarObject_LoadProperties(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0041bdd0(FxEarObject_LoadProperties_Entry, FxEarObject_LoadProperties_Abi)
HOOK(0x0041bdd0, "FxEarObject_LoadProperties", FxEarObject_LoadProperties_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00420080 (eax = holder (+4 = object), edi = sound name; stack: fx, a1, positional, looping-object, looping;
// callee pops 20; caller 0x00420770; executed in recorded runs). Plays an Fx sound (strings "Fx : Failed to play
// sound (no push)" / "Fx : Failed to play looping sound"). The original first overwrites the low byte of its a1 stack
// slot with (looping != 0) and passes that whole dword (upper bytes = a1's) as the third play argument.
//   positional == 0: handle = audio +0xd0 (name, 0, slot, 0, -1, 0).
//   else, without holder / holder+4: log 0x005b8004 (Game+0x34, 10, fmt, [fx+8]+0x24); handle stays the positional
//     argument's dword.
//   else if the 4th argument's low byte is set and 0x0045a480(object) returns true: audio +0xd0 (name, 0, slot, 0,
//     -1, 0); otherwise object+0x558 = a1 and audio +0xc4 (name, object, slot, 0, -1, 0).
// The result is kept in the positional argument's stack slot. When looping: stored at object+0x55c if that is free
// (returns the handle); otherwise logs 0x005b7fdc and calls audio +0xd8 (&handle, 0) (returns its eax). Without
// looping, returns 0.
uint32_t Fx_PlaySound(uint32_t holder, uint32_t name, uint32_t fx, uint32_t a1, uint32_t positional,
                      uint32_t objectFlag, uint32_t looping)
{
    const uint32_t slot = (a1 & 0xffffff00u) | (looping != 0 ? 1u : 0u);
    uint32_t handle = positional;
    if (static_cast<uint8_t>(positional) == 0)
    {
        handle = VCall<uint32_t>(Audio(), 0xd0 / 4, name, 0u, slot, 0u, 0xffffffffu, 0u);
    }
    else if (holder == 0 || Field<uint32_t>(P(holder), 4) == 0)
    {
        orig::fn_00407ff0(Field<uint32_t>(g_game, 0x34), 10, 0x005b8004u, Field<uint32_t>(P(fx), 8) + 0x24);
    }
    else
    {
        const uint32_t object = Field<uint32_t>(P(holder), 4);
        if (static_cast<uint8_t>(objectFlag) != 0 && static_cast<uint8_t>(orig::fn_0045a480(object)) != 0)
        {
            handle = VCall<uint32_t>(Audio(), 0xd0 / 4, name, 0u, slot, 0u, 0xffffffffu, 0u);
        }
        else
        {
            Field<uint32_t>(P(object), 0x558) = a1;
            handle = VCall<uint32_t>(Audio(), 0xc4 / 4, name, Field<uint32_t>(P(holder), 4), slot, 0u, 0xffffffffu, 0u);
        }
    }
    if (looping == 0)
        return 0;
    if (holder != 0)
    {
        const uint32_t object = Field<uint32_t>(P(holder), 4);
        if (object != 0 && Field<uint32_t>(P(object), 0x55c) == 0)
        {
            Field<uint32_t>(P(object), 0x55c) = handle;
            return handle;
        }
    }
    orig::fn_00407ff0(Field<uint32_t>(g_game, 0x34), 10, 0x005b7fdcu, Field<uint32_t>(P(fx), 8) + 0x24);
    return VCall<uint32_t>(Audio(), 0xd8 / 4, u(&handle), 0u);
}

static uint32_t Fx_PlaySound_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                 uint32_t a4)
{
    return Fx_PlaySound(eax, edi, a0, a1, a2, a3, a4);
}
ABI_ENTRY_00420080(Fx_PlaySound_Entry, Fx_PlaySound_Abi)
HOOK(0x00420080, "Fx_PlaySound", Fx_PlaySound_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00434180 (ecx = object; stack: damage; callee pops 4; virtual, vtable data 0x005af368 (Twiglet +0x348, guess);
// not executed in recorded runs). Goliath hit reaction (strings "GOL50005_Hurt", "goliath_hit_med"). When
// 0x0044e600 (player and Game+0x26cc set) is false: unless +0x9bc is set, the base handler 0x00430340(damage).
// Otherwise 0x004555e0(damage) (apply damage), and when 0x00454780 (esi = object) is false, +0x460 == 100,
// Game+0xcbe == 0, (+0xa18 & 2) == 0 and damage+0x14 > +0x9ec (fcomp, ah & 0x41): +0xb50 = audio +0xc4
// ("GOL50005_Hurt", object, 0, 0, -1, 0), Object_setPhysicsEnabled(object, 1), 0x00408e30(eax 0x80, ecx object,
// edx -1; "goliath_hit_med", 0, 0.25f, 0, 0), Body_setLinearVelocity(zero vector), virtual +0x1b8(0x66).
uint32_t fn_00434180(uint8_t* self, uint8_t* damage)
{
    if (!static_cast<uint8_t>(orig::fn_0044e600()))
    {
        if (Field<uint32_t>(self, 0x9bc) != 0)
            return Field<uint32_t>(self, 0x9bc);
        return orig::fn_00430340(u(self), u(damage));
    }
    orig::fn_004555e0(u(self), u(damage));
    uint32_t r = orig::fn_00454780(u(self));
    if (static_cast<uint8_t>(r) != 0 || Field<uint32_t>(self, 0x460) != 0x64)
        return r;
    if (Field<uint8_t>(g_game, 0xcbe) != 0 || (Field<uint8_t>(self, 0xa18) & 2) != 0)
        return r;
    if (!(Field<float>(damage, 0x14) > Field<float>(self, 0x9ec)))
        return r;
    Field<uint32_t>(self, 0xb50) = VCall<uint32_t>(Audio(), 0xc4 / 4, 0x005b63a0u, u(self), 0u, 0u, 0xffffffffu, 0u);
    orig::fn_004577b0(u(self), 1);
    orig::fn_00408e30(0x80, u(self), 0xffffffffu, 0x005b6390u, 0, 0x3e800000u, 0, 0);
    uint32_t zero[3] = {0, 0, 0};
    orig::fn_00458510(u(zero), u(self));
    return VCall<uint32_t>(self, 0x1b8 / 4, 0x66u);
}

static uint32_t fn_00434180_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00434180(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(a0));
}
ABI_ENTRY_00434180(fn_00434180_Entry, fn_00434180_Abi)
HOOK(0x00434180, "fn_00434180", fn_00434180_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00435170 (ecx = object; stack: damage; callee pops 4; virtual, vtable data 0x005a2510 (Q112Object +0xd0); not
// executed in recorded runs). Damage scaling then the base handler: when damage && (damage->flags & 0x400),
// damage+0x14 = +0xb58 * damage+0x14; 0x00430340(damage). Then when no eye loop plays (+0xb5c == 0), health
// (0x004555b0, +0x454) < max (0x004555c0, +0x458) * 0.25 (0x005bcbe4) and 0x00454780 (esi = object) is false:
// +0xb5c = audio +0xc4 (r < 50 ? "fol_q112_eye_1_lp" 0x005b611c : "fol_q112_eye_2_lp" 0x005b6108, object, 1, 0, -1, 0)
// with r = (rand() * 100) >> 15.
uint32_t fn_00435170(uint8_t* self, uint8_t* damage)
{
    if (damage && (Field<uint32_t>(damage, 0) & 0x400))
        Field<float>(damage, 0x14) = Field<float>(self, 0xb58) * Field<float>(damage, 0x14);
    uint32_t r = orig::fn_00430340(u(self), u(damage));
    if (Field<uint32_t>(self, 0xb5c) != 0)
        return Field<uint32_t>(self, 0xb5c);
    const float health = orig::fn_004555b0(u(self));
    const float limit = orig::fn_004555c0(u(self)) * K(0x005bcbe4);
    if (!(limit > health))
        return r;
    r = orig::fn_00454780(u(self));
    if (static_cast<uint8_t>(r))
        return r;
    const int32_t roll = Rand() * 100;
    const uint32_t name = (roll & static_cast<int32_t>(0xffff8000u)) < 0x190000 ? 0x005b611cu : 0x005b6108u;
    return Field<uint32_t>(self, 0xb5c) = VCall<uint32_t>(Audio(), 0xc4 / 4, name, u(self), 1u, 0u, 0xffffffffu, 0u);
}

static uint32_t fn_00435170_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00435170(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(a0));
}
ABI_ENTRY_00435170(fn_00435170_Entry, fn_00435170_Abi)
HOOK(0x00435170, "fn_00435170", fn_00435170_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00436260 (ecx = object; stack: arg; callee pops 4; virtual, vtable data 0x005af5d4 (Twiglet +0x5b4, guess); not
// executed in recorded runs). Zeppelin remark (strings "ROM5020x_Zeppelin"): when GameTime - Game+0xb44 >= Game+0xb50
// (else straight to 0x00436010): Game+0xb44 = now, Game+0xb50 = (max+0xb4c - min+0xb48) * rand() * K(0x005bcbc0) +
// min (a random delay), r = (rand() * 300) >> 15 picks "ROM50206/50208/50209_Zeppelin" for audio +0xd0 (name,
// Game+0x748, 0, 0, -1, 0). Always ends with 0x00436010(ecx = object; arg) and returns its eax.
uint32_t AIVoice_PlayZeppelin(uint32_t self, uint32_t arg)
{
    const float now = GameTime();
    if (now - Field<float>(g_game, 0xb44) >= Field<float>(g_game, 0xb50))  // fcomp, ah & 1: unordered skips too
    {
        Field<float>(g_game, 0xb44) = now;
        const float hi = Field<float>(g_game, 0xb4c);
        const float lo = Field<float>(g_game, 0xb48);
        const float r = static_cast<float>(Rand());
        Field<float>(g_game, 0xb50) = (hi - lo) * r * K(0x005bcbc0) + lo;
        const uint32_t pick = static_cast<uint32_t>((Rand() * 300) >> 15);
        const uint32_t name = pick < 100 ? 0x005b5fa0u : pick < 200 ? 0x005b5f8cu : 0x005b5f78u;
        VCall<uint32_t>(Audio(), 0xd0 / 4, name, Field<uint32_t>(g_game, 0x748), 0u, 0u, 0xffffffffu, 0u);
    }
    return orig::fn_00436010(self, arg);
}

static uint32_t AIVoice_PlayZeppelin_Abi(uint32_t ecx, uint32_t a0)
{
    return AIVoice_PlayZeppelin(ecx, a0);
}
ABI_ENTRY_00436260(AIVoice_PlayZeppelin_Entry, AIVoice_PlayZeppelin_Abi)
HOOK(0x00436260, "AIVoice_PlayZeppelin", AIVoice_PlayZeppelin_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// AI voice lines (0x00437510-0x004383a0). Virtual methods of the character classes (vtable data at 0x005ac900..
// (PickupObject, guess) and 0x005ae868.. (SniperCaptain, guess)); none executed in recorded runs. Each picks one
// of the strings it names with r = (rand() * range) >> 15 (imul, sar: signed) and r / 100 as the index (the
// original compares r against 100, 200, ... with jae), then plays it with audio +0xc4 (name, object, 0, 0, -1, 0).
// Flag +0x484 & 0x200000 selects the second voice set (the "CA2..." strings, guess: second actor).
namespace
{
uint32_t Say(uint32_t name, uint32_t object)
{
    return VCall<uint32_t>(Audio(), 0xc4 / 4, name, object, 0u, 0u, 0xffffffffu, 0u);
}
uint32_t Roll(int32_t range)
{
    return static_cast<uint32_t>((Rand() * range) >> 15);
}
template <size_t N>
uint32_t Pick(int32_t range, const uint32_t (&names)[N])
{
    const uint32_t r = Roll(range) / 100;
    return names[r < N ? r : N - 1];
}
bool SecondVoice(uint32_t object)
{
    return (Field<uint32_t>(P(object), 0x484) & 0x200000) != 0;
}
// Shared voice delay, Game +timeOffset (last time) and +timeOffset+0xc (delay), +4/+8 (min/max of the next delay):
// passes when GameTime - last >= delay (fcomp, ah & 1).
bool SharedDelayPassed(uint32_t timeOffset, float now)
{
    return now - Field<float>(g_game, timeOffset) >= Field<float>(g_game, timeOffset + 0xc);
}
// Stores the time and draws the next delay: (max - min) * rand() * K(0x005bcbc0) + min.
void SharedDelayRestart(uint32_t timeOffset, float now)
{
    Field<float>(g_game, timeOffset) = now;
    const float hi = Field<float>(g_game, timeOffset + 8);
    const float lo = Field<float>(g_game, timeOffset + 4);
    const float r = static_cast<float>(Rand());
    Field<float>(g_game, timeOffset + 0xc) = (hi - lo) * r * K(0x005bcbc0) + lo;
}
}  // namespace

// 0x00437510 (ecx = object; vtable data 0x005acdd8, 0x005aeac8). "CA150050/51/52_CapnBase" (range 300) or, second
// voice, "CA250036/39_CapnBase" (range 200); the handle goes to +0xb50.
uint32_t AIVoice_PlayCapnbase(uint32_t self)
{
    static const uint32_t a[] = {0x005b5e90, 0x005b5e7c, 0x005b5e68};
    static const uint32_t b[] = {0x005b5e54, 0x005b5e40};
    const uint32_t name = SecondVoice(self) ? Pick(300, a) : Pick(200, b);
    return Field<uint32_t>(P(self), 0xb50) = Say(name, self);
}
static uint32_t AIVoice_PlayCapnbase_Abi(uint32_t ecx)
{
    return AIVoice_PlayCapnbase(ecx);
}
ABI_ENTRY_00437510(AIVoice_PlayCapnbase_Entry, AIVoice_PlayCapnbase_Abi)
HOOK(0x00437510, "AIVoice_PlayCapnbase", AIVoice_PlayCapnbase_Entry, nullptr);

// 0x004375e0 (ecx = object; vtable data 0x005acddc, 0x005aeacc). "CA150018/19_Help" or "CA250018/19_Help" (range
// 200 each) -> +0xb50.
uint32_t AIVoice_PlayHelp(uint32_t self)
{
    static const uint32_t a[] = {0x005b5e30, 0x005b5e20};
    static const uint32_t b[] = {0x005b5e10, 0x005b5e00};
    const uint32_t name = SecondVoice(self) ? Pick(200, a) : Pick(200, b);
    return Field<uint32_t>(P(self), 0xb50) = Say(name, self);
}
static uint32_t AIVoice_PlayHelp_Abi(uint32_t ecx)
{
    return AIVoice_PlayHelp(ecx);
}
ABI_ENTRY_004375e0(AIVoice_PlayHelp_Entry, AIVoice_PlayHelp_Abi)
HOOK(0x004375e0, "AIVoice_PlayHelp", AIVoice_PlayHelp_Entry, nullptr);

// 0x00437760 (ecx = object; vtable data 0x005accec, 0x005ae9dc). Shared delay Game+0xb44 (returns when it has not
// passed; eax = fnstsw, 0 here); then "CA150053_OpenGate" (second voice, no roll) or "CA250037/38_OpenGate" (range
// 200) -> +0xb50.
uint32_t AIVoice_PlayOpengate(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t b[] = {0x005b5d60, 0x005b5d4c};
    const uint32_t name = SecondVoice(self) ? 0x005b5d74u : Pick(200, b);
    return Field<uint32_t>(P(self), 0xb50) = Say(name, self);
}
static uint32_t AIVoice_PlayOpengate_Abi(uint32_t ecx)
{
    return AIVoice_PlayOpengate(ecx);
}
ABI_ENTRY_00437760(AIVoice_PlayOpengate_Entry, AIVoice_PlayOpengate_Abi)
HOOK(0x00437760, "AIVoice_PlayOpengate", AIVoice_PlayOpengate_Entry, nullptr);

// 0x00437940 (ecx = object; vtable data 0x005accb8, 0x005ae9a8). "CA150001/2/3_Fight" or "CA250001/2/3_Fight"
// (range 300); the handle is not kept.
uint32_t AIVoice_PlayFight(uint32_t self)
{
    static const uint32_t a[] = {0x005b5cac, 0x005b5c9c, 0x005b5c8c};
    static const uint32_t b[] = {0x005b5c7c, 0x005b5c6c, 0x005b5c5c};
    const uint32_t name = SecondVoice(self) ? Pick(300, a) : Pick(300, b);
    return Say(name, self);
}
static uint32_t AIVoice_PlayFight_Abi(uint32_t ecx)
{
    return AIVoice_PlayFight(ecx);
}
ABI_ENTRY_00437940(AIVoice_PlayFight_Entry, AIVoice_PlayFight_Abi)
HOOK(0x00437940, "AIVoice_PlayFight", AIVoice_PlayFight_Entry, nullptr);

// 0x00437a20 (ecx = object; vtable data 0x005accd8, 0x005ae9c8 (+0x160)). Unless the scream +0xb4c still plays
// (audio +0xdc; returns its eax), "ca150078/79/80_general_scream" (range 300) -> +0xb4c.
uint32_t AIVoice_PlayScream(uint32_t self)
{
    const uint32_t playing = VCall<uint32_t>(Audio(), 0xdc / 4, Field<uint32_t>(P(self), 0xb4c));
    if (static_cast<uint8_t>(playing))
        return playing;
    static const uint32_t a[] = {0x005b5c44, 0x005b5c2c, 0x005b5c14};
    return Field<uint32_t>(P(self), 0xb4c) = Say(Pick(300, a), self);
}
static uint32_t AIVoice_PlayScream_Abi(uint32_t ecx)
{
    return AIVoice_PlayScream(ecx);
}
ABI_ENTRY_00437a20(AIVoice_PlayScream_Entry, AIVoice_PlayScream_Abi)
HOOK(0x00437a20, "AIVoice_PlayScream", AIVoice_PlayScream_Entry, nullptr);

// 0x00437ac0 (ecx = object; vtable data 0x005acce0, 0x005ae9d0). As 0x00437a20 with "ca150081..85_topsy_scream"
// (range 500) -> +0xb4c.
uint32_t AIVoice_PlayTopsyScream(uint32_t self)
{
    const uint32_t playing = VCall<uint32_t>(Audio(), 0xdc / 4, Field<uint32_t>(P(self), 0xb4c));
    if (static_cast<uint8_t>(playing))
        return playing;
    static const uint32_t a[] = {0x005b5bfc, 0x005b5be4, 0x005b5bcc, 0x005b5bb4, 0x005b5b9c};
    return Field<uint32_t>(P(self), 0xb4c) = Say(Pick(500, a), self);
}
static uint32_t AIVoice_PlayTopsyScream_Abi(uint32_t ecx)
{
    return AIVoice_PlayTopsyScream(ecx);
}
ABI_ENTRY_00437ac0(AIVoice_PlayTopsyScream_Entry, AIVoice_PlayTopsyScream_Abi)
HOOK(0x00437ac0, "AIVoice_PlayTopsyScream", AIVoice_PlayTopsyScream_Entry, nullptr);

// 0x00437b60 (ecx = object; vtable data 0x005accdc, 0x005ae9cc (+0x164)). Shared delay Game+0xb64: when it has not
// passed, tail-jumps to virtual +0x160 (the general scream, no stack arguments: reproduced as call-and-return).
// Otherwise unless +0xb4c still plays (audio +0xdc; returns its eax, delay untouched): restarts the delay and plays
// "ca150046..49_sticky_scream" (second voice) or "ca250032..35_sticky_scream" (range 400 each) -> +0xb4c.
uint32_t AIVoice_PlayStickyScream(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb64, now))
        return VCall<uint32_t>(P(self), 0x160 / 4);
    const uint32_t playing = VCall<uint32_t>(Audio(), 0xdc / 4, Field<uint32_t>(P(self), 0xb4c));
    if (static_cast<uint8_t>(playing))
        return playing;
    SharedDelayRestart(0xb64, now);
    static const uint32_t a[] = {0x005b5b84, 0x005b5b6c, 0x005b5b54, 0x005b5b3c};
    static const uint32_t b[] = {0x005b5b24, 0x005b5b0c, 0x005b5af4, 0x005b5adc};
    const uint32_t name = SecondVoice(self) ? Pick(400, a) : Pick(400, b);
    return Field<uint32_t>(P(self), 0xb4c) = Say(name, self);
}
static uint32_t AIVoice_PlayStickyScream_Abi(uint32_t ecx)
{
    return AIVoice_PlayStickyScream(ecx);
}
ABI_ENTRY_00437b60(AIVoice_PlayStickyScream_Entry, AIVoice_PlayStickyScream_Abi)
HOOK(0x00437b60, "AIVoice_PlayStickyScream", AIVoice_PlayStickyScream_Entry, nullptr);

// 0x00437d60 (ecx = object; stack: one unused argument, callee pops 4; vtable data 0x005accc4, 0x005ae9b4).
// Shared delay Game+0xb64; returns false when it has not passed. Otherwise restarts it and plays one of ten
// "CA150045.._Hurt" (second voice, range 1000) or seven "CA2500xx_Hurt" (range 700) -> +0xb50; returns true (al).
uint32_t AIVoice_PlayHurt(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb64, now))
        return 0;
    SharedDelayRestart(0xb64, now);
    static const uint32_t a[] = {0x005b5acc, 0x005b5abc, 0x005b5aac, 0x005b5a9c, 0x005b5a8c,
                                 0x005b5a7c, 0x005b5a6c, 0x005b5a5c, 0x005b5a4c, 0x005b5a3c};
    static const uint32_t b[] = {0x005b5a2c, 0x005b5a1c, 0x005b5a0c, 0x005b59fc, 0x005b59ec, 0x005b59dc, 0x005b59cc};
    const uint32_t name = SecondVoice(self) ? Pick(1000, a) : Pick(700, b);
    Field<uint32_t>(P(self), 0xb50) = Say(name, self);
    return 1;
}
static uint32_t AIVoice_PlayHurt_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return AIVoice_PlayHurt(ecx);
}
ABI_ENTRY_00437d60(AIVoice_PlayHurt_Entry, AIVoice_PlayHurt_Abi)
HOOK(0x00437d60, "AIVoice_PlayHurt", AIVoice_PlayHurt_Entry, nullptr);

// 0x00437f60 (ecx = object; vtable data 0x005accc0, 0x005ae9b0). Per-object delay: returns unless GameTime - +0xb48
// >= 1.0 (0x005bcb2c); then +0xb48 = now and "ca150027..31_Hurt" (second voice) or "ca250027..29.._Hurt" (range 500
// each); the handle is not kept.
uint32_t AIVoice_PlayHurtShort(uint32_t self)
{
    const float now = GameTime();
    if (!(now - Field<float>(P(self), 0xb48) >= K(0x005bcb2c)))
        return 0;
    Field<float>(P(self), 0xb48) = now;
    static const uint32_t a[] = {0x005b59bc, 0x005b59ac, 0x005b599c, 0x005b598c, 0x005b597c};
    static const uint32_t b[] = {0x005b596c, 0x005b595c, 0x005b594c, 0x005b593c, 0x005b592c};
    const uint32_t name = SecondVoice(self) ? Pick(500, a) : Pick(500, b);
    return Say(name, self);
}
static uint32_t AIVoice_PlayHurtShort_Abi(uint32_t ecx)
{
    return AIVoice_PlayHurtShort(ecx);
}
ABI_ENTRY_00437f60(AIVoice_PlayHurtShort_Entry, AIVoice_PlayHurtShort_Abi)
HOOK(0x00437f60, "AIVoice_PlayHurtShort", AIVoice_PlayHurtShort_Entry, nullptr);

// 0x004380e0 (ecx = object; vtable data 0x005acccc, 0x005ae9bc). Shared delay Game+0xb64 and the scream +0xb4c not
// playing (audio +0xdc), else returns false. Restarts the delay, stops the previous line (audio +0xd8 (&+0xb50, 0)),
// plays "ca150021..26/36_Dead" (second voice, range 700) or "ca2500xx_Dead" (range 600) -> +0xb50; returns true.
uint32_t AIVoice_PlayDead(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb64, now))
        return 0;
    if (static_cast<uint8_t>(VCall<uint32_t>(Audio(), 0xdc / 4, Field<uint32_t>(P(self), 0xb4c))))
        return 0;
    SharedDelayRestart(0xb64, now);
    uint32_t* handle = &Field<uint32_t>(P(self), 0xb50);
    VCall<uint32_t>(Audio(), 0xd8 / 4, u(handle), 0u);
    static const uint32_t a[] = {0x005b591c, 0x005b590c, 0x005b58fc, 0x005b58ec, 0x005b58dc, 0x005b58cc, 0x005b58bc};
    static const uint32_t b[] = {0x005b58ac, 0x005b589c, 0x005b588c, 0x005b587c, 0x005b586c, 0x005b585c};
    const uint32_t name = SecondVoice(self) ? Pick(700, a) : Pick(600, b);
    *handle = Say(name, self);
    return 1;
}
static uint32_t AIVoice_PlayDead_Abi(uint32_t ecx)
{
    return AIVoice_PlayDead(ecx);
}
ABI_ENTRY_004380e0(AIVoice_PlayDead_Entry, AIVoice_PlayDead_Abi)
HOOK(0x004380e0, "AIVoice_PlayDead", AIVoice_PlayDead_Entry, nullptr);

// 0x004382e0 (ecx = object; vtable data 0x005accd4, 0x005ae9c4). "ca150010/11_Taunt" (second voice, range 200) or
// "ca250007..10_Taunt" (range 400) -> +0xb50.
uint32_t AIVoice_PlayTaunt(uint32_t self)
{
    static const uint32_t a[] = {0x005b584c, 0x005b583c};
    static const uint32_t b[] = {0x005b582c, 0x005b581c, 0x005b580c, 0x005b57fc};
    const uint32_t name = SecondVoice(self) ? Pick(200, a) : Pick(400, b);
    return Field<uint32_t>(P(self), 0xb50) = Say(name, self);
}
static uint32_t AIVoice_PlayTaunt_Abi(uint32_t ecx)
{
    return AIVoice_PlayTaunt(ecx);
}
ABI_ENTRY_004382e0(AIVoice_PlayTaunt_Entry, AIVoice_PlayTaunt_Abi)
HOOK(0x004382e0, "AIVoice_PlayTaunt", AIVoice_PlayTaunt_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Grunt voice lines (0x004383a0-0x00438ea2): implementations take (esi = &handle, edi = object; stack: flags
// object+0x484, flags object+0x488; caller pops) and pick one of three voice sets: +0x484 & 0x200000 ("GR3..."
// strings), else +0x488 & 0x20000000 ("GR2..." / "gr2..."), else the first ("GR1..."/"gr1..."). Small virtual
// wrappers (ecx = object) load the flags and the handle member. None executed in recorded runs except 0x00438cb0.
namespace
{
template <size_t A, size_t B, size_t C>
uint32_t GruntSay(uint32_t* handle, uint32_t object, uint32_t f484, uint32_t f488, int32_t ra, const uint32_t (&a)[A],
                  int32_t rb, const uint32_t (&b)[B], int32_t rc, const uint32_t (&c)[C])
{
    uint32_t name;
    if (f484 & 0x200000)
        name = Pick(ra, a);
    else if (f488 & 0x20000000)
        name = Pick(rb, b);
    else
        name = Pick(rc, c);
    return *handle = Say(name, object);
}
}  // namespace

// 0x004383a0 (esi = &handle, edi = object; stack: f484, f488; callers 0x00438590, 0x00438810). Unless the handle
// still plays (audio +0xdc; returns its eax): "GR350049..52_General_Scream" (range 400), "gr2..." (range 400) or
// "gr150050..53_general_scream" set (range 600: r < 100, < 200, < 300, < 500 (no 400 step: two slots for the fourth
// string), else the fifth).
uint32_t AIVoice_GruntScream(uint32_t* handle, uint32_t object, uint32_t f484, uint32_t f488)
{
    const uint32_t playing = VCall<uint32_t>(Audio(), 0xdc / 4, *handle);
    if (static_cast<uint8_t>(playing))
        return playing;
    static const uint32_t a[] = {0x005b57e4, 0x005b57cc, 0x005b57b4, 0x005b579c};
    static const uint32_t b[] = {0x005b5784, 0x005b576c, 0x005b5754, 0x005b573c};
    uint32_t name;
    if (f484 & 0x200000)
        name = Pick(400, a);
    else if (f488 & 0x20000000)
        name = Pick(400, b);
    else
    {
        const uint32_t r = Roll(600);
        name = r < 100 ? 0x005b5724u : r < 200 ? 0x005b570cu : r < 300 ? 0x005b56f4u : r < 500 ? 0x005b56dcu : 0x005b56c4u;
    }
    return *handle = Say(name, object);
}
static uint32_t AIVoice_GruntScream_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return AIVoice_GruntScream(reinterpret_cast<uint32_t*>(esi), edi, a0, a1);
}
ABI_ENTRY_004383a0(AIVoice_GruntScream_Entry, AIVoice_GruntScream_Abi)
HOOK(0x004383a0, "AIVoice_GruntScream", AIVoice_GruntScream_Entry, nullptr);

// 0x00438590 (ecx = object; vtable data 0x005a9e50, 0x005abce8 (BowGruntObject +0x160, guess)): 0x004383a0 with
// handle +0xb4c.
uint32_t fn_00438590(uint32_t self)
{
    return AIVoice_GruntScream(&Field<uint32_t>(P(self), 0xb4c), self, Field<uint32_t>(P(self), 0x484),
                               Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00438590_Abi(uint32_t ecx)
{
    return fn_00438590(ecx);
}
ABI_ENTRY_00438590(fn_00438590_Entry, fn_00438590_Abi)
HOOK(0x00438590, "fn_00438590", fn_00438590_Entry, nullptr);

// 0x004385c0 (esi = &handle, edi = object; stack: f484, f488; caller 0x004387e0). Unless the handle still plays
// (audio +0xdc): "GR350078..82_Topsy_Scream", "gr2..topsy" or "gr150079.._topsy_scream" (range 500 each).
uint32_t AIVoice_GruntTopsyScream(uint32_t* handle, uint32_t object, uint32_t f484, uint32_t f488)
{
    const uint32_t playing = VCall<uint32_t>(Audio(), 0xdc / 4, *handle);
    if (static_cast<uint8_t>(playing))
        return playing;
    static const uint32_t a[] = {0x005b56ac, 0x005b5694, 0x005b567c, 0x005b5664, 0x005b564c};
    static const uint32_t b[] = {0x005b5634, 0x005b561c, 0x005b5604, 0x005b55ec, 0x005b55d4};
    static const uint32_t c[] = {0x005b55bc, 0x005b55a4, 0x005b558c, 0x005b5574, 0x005b555c};
    return GruntSay(handle, object, f484, f488, 500, a, 500, b, 500, c);
}
static uint32_t AIVoice_GruntTopsyScream_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return AIVoice_GruntTopsyScream(reinterpret_cast<uint32_t*>(esi), edi, a0, a1);
}
ABI_ENTRY_004385c0(AIVoice_GruntTopsyScream_Entry, AIVoice_GruntTopsyScream_Abi)
HOOK(0x004385c0, "AIVoice_GruntTopsyScream", AIVoice_GruntTopsyScream_Entry, nullptr);

// 0x004387e0 (ecx = object; vtable data 0x005a9e58, 0x005abcf0): 0x004385c0 with handle +0xb4c.
uint32_t fn_004387e0(uint32_t self)
{
    return AIVoice_GruntTopsyScream(&Field<uint32_t>(P(self), 0xb4c), self, Field<uint32_t>(P(self), 0x484),
                                    Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_004387e0_Abi(uint32_t ecx)
{
    return fn_004387e0(ecx);
}
ABI_ENTRY_004387e0(fn_004387e0_Entry, fn_004387e0_Abi)
HOOK(0x004387e0, "fn_004387e0", fn_004387e0_Entry, nullptr);

// 0x00438810 (eax = &handle, ecx = object, ebx = f484; stack: f488; caller pops; caller 0x00439de0). Shared delay
// Game+0xb64: when it has not passed, falls back to 0x004383a0 (esi = handle, edi = object; f484, f488). Otherwise
// unless the handle still plays (audio +0xdc): restarts the delay and plays "GR350034..36_Sticky_Scream" (range 300),
// "gr2.._sticky_scream" or "gr150034.._sticky_scream" (range 400). rand() is drawn before the set is chosen.
uint32_t AIVoice_GruntStickyScream(uint32_t* handle, uint32_t object, uint32_t f484, uint32_t f488)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb64, now))
        return AIVoice_GruntScream(handle, object, f484, f488);
    const uint32_t playing = VCall<uint32_t>(Audio(), 0xdc / 4, *handle);
    if (static_cast<uint8_t>(playing))
        return playing;
    SharedDelayRestart(0xb64, now);
    const int32_t roll = Rand();
    uint32_t name;
    if (f484 & 0x200000)
    {
        const uint32_t r = static_cast<uint32_t>((roll * 300) >> 15);
        name = r < 100 ? 0x005b5544u : r < 200 ? 0x005b552cu : 0x005b5514u;
    }
    else
    {
        const uint32_t r = static_cast<uint32_t>((roll * 400) >> 15);
        if (f488 & 0x20000000)
            name = r < 100 ? 0x005b54fcu : r < 200 ? 0x005b54e4u : r < 300 ? 0x005b54ccu : 0x005b54b4u;
        else
            name = r < 100 ? 0x005b549cu : r < 200 ? 0x005b5484u : r < 300 ? 0x005b546cu : 0x005b5454u;
    }
    return *handle = Say(name, object);
}
static uint32_t AIVoice_GruntStickyScream_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t a0)
{
    return AIVoice_GruntStickyScream(reinterpret_cast<uint32_t*>(eax), ecx, ebx, a0);
}
ABI_ENTRY_00438810(AIVoice_GruntStickyScream_Entry, AIVoice_GruntStickyScream_Abi)
HOOK(0x00438810, "AIVoice_GruntStickyScream", AIVoice_GruntStickyScream_Entry, nullptr);

// 0x00438a60 (esi = &handle, edi = object; stack: f484, f488; caller 0x00438c10). Shared delay Game+0xb44 (returns
// 0 when it has not passed), restarts it, then "GR150039/40_OpenGate"-style sets: range 200, 200, 400.
uint32_t AIVoice_GruntOpengate(uint32_t* handle, uint32_t object, uint32_t f484, uint32_t f488)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x005b5440, 0x005b542c};
    static const uint32_t b[] = {0x005b5418, 0x005b5404};
    static const uint32_t c[] = {0x005b53f0, 0x005b53dc, 0x005b53c8, 0x005b53b4};
    return GruntSay(handle, object, f484, f488, 200, a, 200, b, 400, c);
}
static uint32_t AIVoice_GruntOpengate_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return AIVoice_GruntOpengate(reinterpret_cast<uint32_t*>(esi), edi, a0, a1);
}
ABI_ENTRY_00438a60(AIVoice_GruntOpengate_Entry, AIVoice_GruntOpengate_Abi)
HOOK(0x00438a60, "AIVoice_GruntOpengate", AIVoice_GruntOpengate_Entry, nullptr);

// 0x00438c10 (ecx = object; vtable data 0x005a9e64, 0x005abcfc): 0x00438a60 with handle +0xb50.
uint32_t fn_00438c10(uint32_t self)
{
    return AIVoice_GruntOpengate(&Field<uint32_t>(P(self), 0xb50), self, Field<uint32_t>(P(self), 0x484),
                                 Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00438c10_Abi(uint32_t ecx)
{
    return fn_00438c10(ecx);
}
ABI_ENTRY_00438c10(fn_00438c10_Entry, fn_00438c10_Abi)
HOOK(0x00438c10, "fn_00438c10", fn_00438c10_Entry, nullptr);

// 0x00438c40 (eax = object, ecx = f484, esi = &handle; stack: f488; caller pops; caller 0x00439dc0).
// "GR350044_LadderPlaced", "GR250042_LadderPlaced" or "GR150043_LadderPlaced" (no roll) -> handle.
uint32_t AIVoice_PlayLadderplaced(uint32_t object, uint32_t f484, uint32_t* handle, uint32_t f488)
{
    const uint32_t name = (f484 & 0x200000) ? 0x005b539cu : (f488 & 0x20000000) ? 0x005b5384u : 0x005b536cu;
    return *handle = Say(name, object);
}
static uint32_t AIVoice_PlayLadderplaced_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return AIVoice_PlayLadderplaced(eax, ecx, reinterpret_cast<uint32_t*>(esi), a0);
}
ABI_ENTRY_00438c40(AIVoice_PlayLadderplaced_Entry, AIVoice_PlayLadderplaced_Abi)
HOOK(0x00438c40, "AIVoice_PlayLadderplaced", AIVoice_PlayLadderplaced_Entry, nullptr);

// 0x00438cb0 (esi = &handle, edi = object; stack: f484, f488; caller 0x00438e80; executed in recorded runs).
// Shared delay Game+0xb44 (returns 0 when it has not passed), restarts it, then "GR1/2/3..._ClimbTaunt" (range 300
// each).
uint32_t AIVoice_PlayClimbtaunt(uint32_t* handle, uint32_t object, uint32_t f484, uint32_t f488)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x005b5358, 0x005b5344, 0x005b5330};
    static const uint32_t b[] = {0x005b531c, 0x005b5308, 0x005b52f4};
    static const uint32_t c[] = {0x005b52e0, 0x005b52cc, 0x005b52b8};
    return GruntSay(handle, object, f484, f488, 300, a, 300, b, 300, c);
}
static uint32_t AIVoice_PlayClimbtaunt_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return AIVoice_PlayClimbtaunt(reinterpret_cast<uint32_t*>(esi), edi, a0, a1);
}
ABI_ENTRY_00438cb0(AIVoice_PlayClimbtaunt_Entry, AIVoice_PlayClimbtaunt_Abi)
HOOK(0x00438cb0, "AIVoice_PlayClimbtaunt", AIVoice_PlayClimbtaunt_Entry, nullptr);

// 0x00438e80 (ecx = object; vtable data 0x005a9e5c, 0x005abcf4): 0x00438cb0 with handle +0xb50.
uint32_t fn_00438e80(uint32_t self)
{
    return AIVoice_PlayClimbtaunt(&Field<uint32_t>(P(self), 0xb50), self, Field<uint32_t>(P(self), 0x484),
                                  Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00438e80_Abi(uint32_t ecx)
{
    return fn_00438e80(ecx);
}
ABI_ENTRY_00438e80(fn_00438e80_Entry, fn_00438e80_Abi)
HOOK(0x00438e80, "fn_00438e80", fn_00438e80_Entry, nullptr);

// 0x00438eb0 (eax = f484, esi = &handle, edi = object; stack: f488; caller 0x00438f90). "GR1/2/3..._Help" (range
// 200 each) -> handle.
uint32_t AIVoice_GruntHelp(uint32_t f484, uint32_t* handle, uint32_t object, uint32_t f488)
{
    static const uint32_t a[] = {0x005b52a8, 0x005b5298};
    static const uint32_t b[] = {0x005b5288, 0x005b5278};
    static const uint32_t c[] = {0x005b5268, 0x005b5258};
    return GruntSay(handle, object, f484, f488, 200, a, 200, b, 200, c);
}
static uint32_t AIVoice_GruntHelp_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return AIVoice_GruntHelp(eax, reinterpret_cast<uint32_t*>(esi), edi, a0);
}
ABI_ENTRY_00438eb0(AIVoice_GruntHelp_Entry, AIVoice_GruntHelp_Abi)
HOOK(0x00438eb0, "AIVoice_GruntHelp", AIVoice_GruntHelp_Entry, nullptr);

// 0x00438f90 (ecx = object; vtable data 0x005a9f54, 0x005abdec): 0x00438eb0 with handle +0xb50.
uint32_t fn_00438f90(uint32_t self)
{
    return AIVoice_GruntHelp(Field<uint32_t>(P(self), 0x484), &Field<uint32_t>(P(self), 0xb50), self,
                             Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00438f90_Abi(uint32_t ecx)
{
    return fn_00438f90(ecx);
}
ABI_ENTRY_00438f90(fn_00438f90_Entry, fn_00438f90_Abi)
HOOK(0x00438f90, "fn_00438f90", fn_00438f90_Entry, nullptr);

// 0x004390e0 (eax = f484, esi = object; stack: f488; caller 0x004391d0). "GR350001..3_Fight" (range 300),
// "GR2..._Fight" (range 300) or "GR150002_Fight" 0x005b5198 (no roll); the handle is not kept.
uint32_t AIVoice_GruntFight(uint32_t f484, uint32_t object, uint32_t f488)
{
    static const uint32_t a[] = {0x005b51f8, 0x005b51e8, 0x005b51d8};
    static const uint32_t b[] = {0x005b51c8, 0x005b51b8, 0x005b51a8};
    if (f484 & 0x200000)
        return Say(Pick(300, a), object);
    if (f488 & 0x20000000)
        return Say(Pick(300, b), object);
    return Say(0x005b5198u, object);
}
static uint32_t AIVoice_GruntFight_Abi(uint32_t eax, uint32_t esi, uint32_t a0)
{
    return AIVoice_GruntFight(eax, esi, a0);
}
ABI_ENTRY_004390e0(AIVoice_GruntFight_Entry, AIVoice_GruntFight_Abi)
HOOK(0x004390e0, "AIVoice_GruntFight", AIVoice_GruntFight_Entry, nullptr);

// 0x004391d0 (ecx = object; vtable data 0x005a9e30, 0x005abcc8): 0x004390e0 with the object's flags.
uint32_t fn_004391d0(uint32_t self)
{
    return AIVoice_GruntFight(Field<uint32_t>(P(self), 0x484), self, Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_004391d0_Abi(uint32_t ecx)
{
    return fn_004391d0(ecx);
}
ABI_ENTRY_004391d0(fn_004391d0_Entry, fn_004391d0_Abi)
HOOK(0x004391d0, "fn_004391d0", fn_004391d0_Entry, nullptr);

// 0x004392f0 (esi = &handle, edi = object; stack: f484, f488; caller 0x00439e00). Shared delay Game+0xb64
// (returns false when it has not passed), restarts it, then "GR1/2/3..._Hurt" (range 400 each) -> handle; returns
// true (al).
uint32_t AIVoice_GruntHurt(uint32_t* handle, uint32_t object, uint32_t f484, uint32_t f488)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb64, now))
        return 0;
    SharedDelayRestart(0xb64, now);
    static const uint32_t a[] = {0x005b50f8, 0x005b50e8, 0x005b50d8, 0x005b50c8};
    static const uint32_t b[] = {0x005b50b8, 0x005b50a8, 0x005b5098, 0x005b5088};
    static const uint32_t c[] = {0x005b5078, 0x005b5068, 0x005b5058, 0x005b5048};
    GruntSay(handle, object, f484, f488, 400, a, 400, b, 400, c);
    return 1;
}
static uint32_t AIVoice_GruntHurt_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return AIVoice_GruntHurt(reinterpret_cast<uint32_t*>(esi), edi, a0, a1);
}
ABI_ENTRY_004392f0(AIVoice_GruntHurt_Entry, AIVoice_GruntHurt_Abi)
HOOK(0x004392f0, "AIVoice_GruntHurt", AIVoice_GruntHurt_Entry, nullptr);

// 0x004394c0 (ecx = &lastTime, esi = object; stack: f484, f488; caller 0x00439e30). Returns unless GameTime -
// *lastTime >= 4.0 (0x005bcbe0); then *lastTime = now and "GR3..._Hurt" (range 500), "GR2..." (range 500) or
// "gr150029.._Hurt" (range 400); the handle is not kept.
uint32_t AIVoice_GruntHurtShort(float* lastTime, uint32_t object, uint32_t f484, uint32_t f488)
{
    const float now = GameTime();
    if (!(now - *lastTime >= K(0x005bcbe0)))
        return 0;
    *lastTime = now;
    static const uint32_t a[] = {0x005b5038, 0x005b5028, 0x005b5018, 0x005b5008, 0x005b4ff8};
    static const uint32_t b[] = {0x005b4fe8, 0x005b4fd8, 0x005b4fc8, 0x005b4fb8, 0x005b4fa8};
    static const uint32_t c[] = {0x005b4f98, 0x005b4f88, 0x005b4f78, 0x005b4f68};
    if (f484 & 0x200000)
        return Say(Pick(500, a), object);
    if (f488 & 0x20000000)
        return Say(Pick(500, b), object);
    return Say(Pick(400, c), object);
}
static uint32_t AIVoice_GruntHurtShort_Abi(uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return AIVoice_GruntHurtShort(reinterpret_cast<float*>(ecx), esi, a0, a1);
}
ABI_ENTRY_004394c0(AIVoice_GruntHurtShort_Entry, AIVoice_GruntHurtShort_Abi)
HOOK(0x004394c0, "AIVoice_GruntHurtShort", AIVoice_GruntHurtShort_Entry, nullptr);

// 0x004396a0 (ebx = &stateFlags, esi = &handle, edi = object; stack: f484, f488, screamHandle; caller 0x00439e60;
// executed in recorded runs). Returns true at once when *stateFlags & 0x400 (death line already said). Returns
// false when the shared delay Game+0xb64 has not passed or the scream still plays (audio +0xdc). Otherwise restarts
// the delay, stops the handle (audio +0xd8 (handle, 0)), sets *stateFlags |= 0x400 and plays "GR3/gr1/gr2.._Dead"
// (range 300 each) -> handle; returns true.
uint32_t AIVoice_GruntDead(uint32_t* stateFlags, uint32_t* handle, uint32_t object, uint32_t f484, uint32_t f488,
                           uint32_t scream)
{
    if (*stateFlags & 0x400)
        return 1;
    const float now = GameTime();
    if (!SharedDelayPassed(0xb64, now))
        return 0;
    if (static_cast<uint8_t>(VCall<uint32_t>(Audio(), 0xdc / 4, scream)))
        return 0;
    SharedDelayRestart(0xb64, now);
    VCall<uint32_t>(Audio(), 0xd8 / 4, u(handle), 0u);
    *stateFlags |= 0x400;
    static const uint32_t a[] = {0x005b4f58, 0x005b4f48, 0x005b4f38};
    static const uint32_t b[] = {0x005b4f28, 0x005b4f18, 0x005b4f08};
    static const uint32_t c[] = {0x005b4ef8, 0x005b4ee8, 0x005b4ed8};
    GruntSay(handle, object, f484, f488, 300, a, 300, b, 300, c);
    return 1;
}
static uint32_t AIVoice_GruntDead_Abi(uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return AIVoice_GruntDead(reinterpret_cast<uint32_t*>(ebx), reinterpret_cast<uint32_t*>(esi), edi, a0, a1, a2);
}
ABI_ENTRY_004396a0(AIVoice_GruntDead_Entry, AIVoice_GruntDead_Abi)
HOOK(0x004396a0, "AIVoice_GruntDead", AIVoice_GruntDead_Entry, nullptr);

// 0x00439890 (ebx = &stateFlags, esi = &handle, edi = object; stack: f484, f488; caller 0x00439ea0; executed in
// recorded runs). Unless *stateFlags & 0x400 (returns it): stops the handle (audio +0xd8 (handle, 0)), sets the
// flag, takes a 12-byte position from object virtual +0x54(&pos), then plays at that position with audio +0xcc
// (name, &pos, 0, 0, 0, -1, 0) for the "GR3500xx_Dead" (range 500) and "gr1500xx_Dead" (range 500) sets, or on the
// object (audio +0xc4) for the "gr2.._Dead" set (range 500) -> handle. Returns the play call's eax.
uint32_t AIVoice_GruntDeadAtPosition(uint32_t* stateFlags, uint32_t* handle, uint32_t object, uint32_t f484,
                                     uint32_t f488)
{
    if (*stateFlags & 0x400)
        return *stateFlags;
    VCall<uint32_t>(Audio(), 0xd8 / 4, u(handle), 0u);
    *stateFlags |= 0x400;
    uint32_t pos[3];
    VCall<uint32_t>(P(object), 0x54 / 4, u(pos));
    static const uint32_t a[] = {0x005b4ec8, 0x005b4eb8, 0x005b4ea8, 0x005b4e98, 0x005b4e88};
    static const uint32_t b[] = {0x005b4e78, 0x005b4e68, 0x005b4e58, 0x005b4e48, 0x005b4e38};
    static const uint32_t c[] = {0x005b4e28, 0x005b4e18, 0x005b4e08, 0x005b4df8, 0x005b4de8};
    if (f484 & 0x200000)
        return *handle = VCall<uint32_t>(Audio(), 0xcc / 4, Pick(500, a), u(pos), 0u, 0u, 0u, 0xffffffffu, 0u);
    if (f488 & 0x20000000)
        return *handle = Say(Pick(500, b), object);
    return *handle = VCall<uint32_t>(Audio(), 0xcc / 4, Pick(500, c), u(pos), 0u, 0u, 0u, 0xffffffffu, 0u);
}
static uint32_t AIVoice_GruntDeadAtPosition_Abi(uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return AIVoice_GruntDeadAtPosition(reinterpret_cast<uint32_t*>(ebx), reinterpret_cast<uint32_t*>(esi), edi, a0,
                                       a1);
}
ABI_ENTRY_00439890(AIVoice_GruntDeadAtPosition_Entry, AIVoice_GruntDeadAtPosition_Abi)
HOOK(0x00439890, "AIVoice_GruntDeadAtPosition", AIVoice_GruntDeadAtPosition_Entry, nullptr);

// 0x00439ae0 (eax = f484, esi = &handle, edi = object; stack: f488; caller 0x00439ed0). "GR3.._Taunt" (range 400),
// "GR2.._Taunt" (range 400) or "gr150017..20_Taunt" set (range 500) -> handle.
uint32_t AIVoice_GruntTaunt(uint32_t f484, uint32_t* handle, uint32_t object, uint32_t f488)
{
    static const uint32_t a[] = {0x005b4dd8, 0x005b4dc8, 0x005b4db8, 0x005b4da8};
    static const uint32_t b[] = {0x005b4d98, 0x005b4d88, 0x005b4d78, 0x005b4d68};
    static const uint32_t c[] = {0x005b4d58, 0x005b4d48, 0x005b4d38, 0x005b4d28, 0x005b4d18};
    return GruntSay(handle, object, f484, f488, 400, a, 400, b, 500, c);
}
static uint32_t AIVoice_GruntTaunt_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return AIVoice_GruntTaunt(eax, reinterpret_cast<uint32_t*>(esi), edi, a0);
}
ABI_ENTRY_00439ae0(AIVoice_GruntTaunt_Entry, AIVoice_GruntTaunt_Abi)
HOOK(0x00439ae0, "AIVoice_GruntTaunt", AIVoice_GruntTaunt_Entry, nullptr);

// 0x00439cb0 (eax = f484, esi = &handle, edi = object; stack: f488; caller 0x00439f00). "GR350015_Ambient",
// "GR250012_Ambient" (no roll) or "GR150016/..._Ambient" (range 200) -> handle.
uint32_t AIVoice_PlayAmbient(uint32_t f484, uint32_t* handle, uint32_t object, uint32_t f488)
{
    static const uint32_t c[] = {0x005b4cdc, 0x005b4cc8};
    uint32_t name;
    if (f484 & 0x200000)
        name = 0x005b4d04u;
    else if (f488 & 0x20000000)
        name = 0x005b4cf0u;
    else
        name = Pick(200, c);
    return *handle = Say(name, object);
}
static uint32_t AIVoice_PlayAmbient_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return AIVoice_PlayAmbient(eax, reinterpret_cast<uint32_t*>(esi), edi, a0);
}
ABI_ENTRY_00439cb0(AIVoice_PlayAmbient_Entry, AIVoice_PlayAmbient_Abi)
HOOK(0x00439cb0, "AIVoice_PlayAmbient", AIVoice_PlayAmbient_Entry, nullptr);

// 0x00439dc0 (ecx = object; vtable data 0x005a9e60, 0x005abcf8): 0x00438c40 with handle +0xb50.
uint32_t fn_00439dc0(uint32_t self)
{
    return AIVoice_PlayLadderplaced(self, Field<uint32_t>(P(self), 0x484), &Field<uint32_t>(P(self), 0xb50),
                                    Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00439dc0_Abi(uint32_t ecx)
{
    return fn_00439dc0(ecx);
}
ABI_ENTRY_00439dc0(fn_00439dc0_Entry, fn_00439dc0_Abi)
HOOK(0x00439dc0, "fn_00439dc0", fn_00439dc0_Entry, nullptr);

// 0x00439de0 (ecx = object; vtable data 0x005a9e54, 0x005abcec): 0x00438810 with handle +0xb4c.
uint32_t fn_00439de0(uint32_t self)
{
    return AIVoice_GruntStickyScream(&Field<uint32_t>(P(self), 0xb4c), self, Field<uint32_t>(P(self), 0x484),
                                     Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00439de0_Abi(uint32_t ecx)
{
    return fn_00439de0(ecx);
}
ABI_ENTRY_00439de0(fn_00439de0_Entry, fn_00439de0_Abi)
HOOK(0x00439de0, "fn_00439de0", fn_00439de0_Entry, nullptr);

// 0x00439e00 (ecx = object; stack: one unused argument, callee pops 4; vtable data 0x005a9e3c, 0x005abcd4):
// 0x004392f0 with handle +0xb50.
uint32_t fn_00439e00(uint32_t self)
{
    return AIVoice_GruntHurt(&Field<uint32_t>(P(self), 0xb50), self, Field<uint32_t>(P(self), 0x484),
                             Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00439e00_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return fn_00439e00(ecx);
}
ABI_ENTRY_00439e00(fn_00439e00_Entry, fn_00439e00_Abi)
HOOK(0x00439e00, "fn_00439e00", fn_00439e00_Entry, nullptr);

// 0x00439e30 (ecx = object; vtable data 0x005a9e38, 0x005abcd0): 0x004394c0 with the time +0xb48.
uint32_t fn_00439e30(uint32_t self)
{
    return AIVoice_GruntHurtShort(&Field<float>(P(self), 0xb48), self, Field<uint32_t>(P(self), 0x484),
                                  Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00439e30_Abi(uint32_t ecx)
{
    return fn_00439e30(ecx);
}
ABI_ENTRY_00439e30(fn_00439e30_Entry, fn_00439e30_Abi)
HOOK(0x00439e30, "fn_00439e30", fn_00439e30_Entry, nullptr);

// 0x00439e60 (ecx = object; vtable data 0x005a9e44, 0x005abcdc): 0x004396a0 with flags +0x48c, handle +0xb50 and
// the scream handle +0xb4c.
uint32_t fn_00439e60(uint32_t self)
{
    return AIVoice_GruntDead(&Field<uint32_t>(P(self), 0x48c), &Field<uint32_t>(P(self), 0xb50), self,
                             Field<uint32_t>(P(self), 0x484), Field<uint32_t>(P(self), 0x488),
                             Field<uint32_t>(P(self), 0xb4c));
}
static uint32_t fn_00439e60_Abi(uint32_t ecx)
{
    return fn_00439e60(ecx);
}
ABI_ENTRY_00439e60(fn_00439e60_Entry, fn_00439e60_Abi)
HOOK(0x00439e60, "fn_00439e60", fn_00439e60_Entry, nullptr);

// 0x00439ea0 (ecx = object; vtable data 0x005a9e48, 0x005abce0): 0x00439890 with flags +0x48c and handle +0xb50.
uint32_t fn_00439ea0(uint32_t self)
{
    return AIVoice_GruntDeadAtPosition(&Field<uint32_t>(P(self), 0x48c), &Field<uint32_t>(P(self), 0xb50), self,
                                       Field<uint32_t>(P(self), 0x484), Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00439ea0_Abi(uint32_t ecx)
{
    return fn_00439ea0(ecx);
}
ABI_ENTRY_00439ea0(fn_00439ea0_Entry, fn_00439ea0_Abi)
HOOK(0x00439ea0, "fn_00439ea0", fn_00439ea0_Entry, nullptr);

// 0x00439ed0 (ecx = object; vtable data 0x005a9e4c, 0x005abce4): 0x00439ae0 with handle +0xb50.
uint32_t fn_00439ed0(uint32_t self)
{
    return AIVoice_GruntTaunt(Field<uint32_t>(P(self), 0x484), &Field<uint32_t>(P(self), 0xb50), self,
                              Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00439ed0_Abi(uint32_t ecx)
{
    return fn_00439ed0(ecx);
}
ABI_ENTRY_00439ed0(fn_00439ed0_Entry, fn_00439ed0_Abi)
HOOK(0x00439ed0, "fn_00439ed0", fn_00439ed0_Entry, nullptr);

// 0x00439f00 (ecx = object; vtable data 0x005a9f50, 0x005abde8): 0x00439cb0 with handle +0xb50.
uint32_t fn_00439f00(uint32_t self)
{
    return AIVoice_PlayAmbient(Field<uint32_t>(P(self), 0x484), &Field<uint32_t>(P(self), 0xb50), self,
                               Field<uint32_t>(P(self), 0x488));
}
static uint32_t fn_00439f00_Abi(uint32_t ecx)
{
    return fn_00439f00(ecx);
}
ABI_ENTRY_00439f00(fn_00439f00_Entry, fn_00439f00_Abi)
HOOK(0x00439f00, "fn_00439f00", fn_00439f00_Entry, nullptr);

// 0x00439f30 (ecx = object; vtable data 0x005af3ec (Twiglet +0x3cc, guess)). Takes the 12-byte position from
// virtual +0x54(&pos) and plays "..._Dead" 0x005b4cb8 there (audio +0xcc (name, &pos, 0, 0, 0, -1, 0)); returns true.
uint32_t AIVoice_PlayDeadAtPosition(uint32_t self)
{
    uint32_t pos[3];
    VCall<uint32_t>(P(self), 0x54 / 4, u(pos));
    VCall<uint32_t>(Audio(), 0xcc / 4, 0x005b4cb8u, u(pos), 0u, 0u, 0u, 0xffffffffu, 0u);
    return 1;
}
static uint32_t AIVoice_PlayDeadAtPosition_Abi(uint32_t ecx)
{
    return AIVoice_PlayDeadAtPosition(ecx);
}
ABI_ENTRY_00439f30(AIVoice_PlayDeadAtPosition_Entry, AIVoice_PlayDeadAtPosition_Abi)
HOOK(0x00439f30, "AIVoice_PlayDeadAtPosition", AIVoice_PlayDeadAtPosition_Entry, nullptr);

// 0x00439f70 (ecx = object; vtable data 0x005af4f8 (Twiglet +0x4d8, guess)). When +0x9bc is set and
// ((rand() * 100) & 0xffff8000) < 0x258000 (75 %): one of thirteen "PE1500xx_PeasantCaptured" lines (range 1300);
// otherwise "GOL50002..4_Ambient" (range 300). -> +0xb50.
uint32_t AIVoice_PlayCapturedOrAmbient(uint32_t self)
{
    static const uint32_t a[] = {0x005b4c9c, 0x005b4c80, 0x005b4c64, 0x005b4c48, 0x005b4c2c, 0x005b4c10, 0x005b4bf4,
                                 0x005b4bd8, 0x005b4bbc, 0x005b4ba0, 0x005b4b84, 0x005b4b68, 0x005b4b4c};
    static const uint32_t b[] = {0x005b4b38, 0x005b4b24, 0x005b4b10};
    uint32_t name;
    if (Field<uint32_t>(P(self), 0x9bc) != 0 && ((Rand() * 100) & static_cast<int32_t>(0xffff8000u)) < 0x258000)
        name = Pick(1300, a);
    else
        name = Pick(300, b);
    return Field<uint32_t>(P(self), 0xb50) = Say(name, self);
}
static uint32_t AIVoice_PlayCapturedOrAmbient_Abi(uint32_t ecx)
{
    return AIVoice_PlayCapturedOrAmbient(ecx);
}
ABI_ENTRY_00439f70(AIVoice_PlayCapturedOrAmbient_Entry, AIVoice_PlayCapturedOrAmbient_Abi)
HOOK(0x00439f70, "AIVoice_PlayCapturedOrAmbient", AIVoice_PlayCapturedOrAmbient_Entry, nullptr);

// 0x0043a100 (esi = &handle, edi = object; caller 0x0043c7b0). Shared delay Game+0xb44 (false when it has not
// passed), restarts it, "q1250012.._Hurt" (seven, range 700) -> handle; returns true.
uint32_t AIVoice_Q12Hurt(uint32_t* handle, uint32_t object)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x005b4b00, 0x005b4af0, 0x005b4ae0, 0x005b4ad0, 0x005b4ac0, 0x005b4ab0, 0x005b4aa0};
    *handle = Say(Pick(700, a), object);
    return 1;
}
static uint32_t AIVoice_Q12Hurt_Abi(uint32_t esi, uint32_t edi)
{
    return AIVoice_Q12Hurt(reinterpret_cast<uint32_t*>(esi), edi);
}
ABI_ENTRY_0043a100(AIVoice_Q12Hurt_Entry, AIVoice_Q12Hurt_Abi)
HOOK(0x0043a100, "AIVoice_Q12Hurt", AIVoice_Q12Hurt_Entry, nullptr);

// 0x0043a2b0 (esi = object; caller 0x0043a300). "q1250015_Dead" / "q1250016_Dead" (range 200), not kept; returns
// true (al).
uint32_t AIVoice_Q12Dead(uint32_t object)
{
    static const uint32_t a[] = {0x005b4a80, 0x005b4a70};
    Say(Pick(200, a), object);
    return 1;
}
static uint32_t AIVoice_Q12Dead_Abi(uint32_t esi)
{
    return AIVoice_Q12Dead(esi);
}
ABI_ENTRY_0043a2b0(AIVoice_Q12Dead_Entry, AIVoice_Q12Dead_Abi)
HOOK(0x0043a2b0, "AIVoice_Q12Dead", AIVoice_Q12Dead_Entry, nullptr);

// 0x0043a300 (ecx = object; vtable data 0x005a2594 (Q112Object +0x154), 0x005a41ec (Q112Boss +0x154)): 0x0043a2b0.
uint32_t fn_0043a300(uint32_t self)
{
    return AIVoice_Q12Dead(self);
}
static uint32_t fn_0043a300_Abi(uint32_t ecx)
{
    return fn_0043a300(ecx);
}
ABI_ENTRY_0043a300(fn_0043a300_Entry, fn_0043a300_Abi)
HOOK(0x0043a300, "fn_0043a300", fn_0043a300_Entry, nullptr);

// 0x0043a310 (ecx = object; vtable data 0x005a7fd4 (ShrubPatrol +0x13c)). Shared delay Game+0xb44 (false when it
// has not passed), restarts it, "SP1500xx_Taunt" (second voice, range 300) or "SP2500xx_Taunt" (range 600) ->
// +0xb50; returns true.
uint32_t AIVoice_ShrubTaunt(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x005b4a60, 0x005b4a50, 0x005b4a40};
    static const uint32_t b[] = {0x005b4a30, 0x005b4a20, 0x005b4a10, 0x005b4a00, 0x005b49f0, 0x005b49e0};
    const uint32_t name = SecondVoice(self) ? Pick(300, a) : Pick(600, b);
    Field<uint32_t>(P(self), 0xb50) = Say(name, self);
    return 1;
}
static uint32_t AIVoice_ShrubTaunt_Abi(uint32_t ecx)
{
    return AIVoice_ShrubTaunt(ecx);
}
ABI_ENTRY_0043a310(AIVoice_ShrubTaunt_Entry, AIVoice_ShrubTaunt_Abi)
HOOK(0x0043a310, "AIVoice_ShrubTaunt", AIVoice_ShrubTaunt_Entry, nullptr);

// 0x0043a480 (ecx = object; vtable data 0x005a7fe8 (ShrubPatrol +0x150)). As 0x0043a310 with
// "SP1500xx_FriendlyFire" / "SP2500xx_FriendlyFire" (range 400 each).
uint32_t AIVoice_ShrubFriendlyFire(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x005b49c8, 0x005b49b0, 0x005b4998, 0x005b4980};
    static const uint32_t b[] = {0x005b4968, 0x005b4950, 0x005b4938, 0x005b4920};
    const uint32_t name = SecondVoice(self) ? Pick(400, a) : Pick(400, b);
    Field<uint32_t>(P(self), 0xb50) = Say(name, self);
    return 1;
}
static uint32_t AIVoice_ShrubFriendlyFire_Abi(uint32_t ecx)
{
    return AIVoice_ShrubFriendlyFire(ecx);
}
ABI_ENTRY_0043a480(AIVoice_ShrubFriendlyFire_Entry, AIVoice_ShrubFriendlyFire_Abi)
HOOK(0x0043a480, "AIVoice_ShrubFriendlyFire", AIVoice_ShrubFriendlyFire_Entry, nullptr);

// 0x0043a5e0 (ecx = object; vtable data 0x005aad64). Shared delay Game+0xb44 (false when it has not passed),
// restarts it, plays "MO150001_Taunt" -> +0xb50; returns true. (The play arguments are pushed while the delay is
// computed; same values.)
uint32_t AIVoice_MoTaunt(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    Field<uint32_t>(P(self), 0xb50) = Say(0x005b4910u, self);
    return 1;
}
static uint32_t AIVoice_MoTaunt_Abi(uint32_t ecx)
{
    return AIVoice_MoTaunt(ecx);
}
ABI_ENTRY_0043a5e0(AIVoice_MoTaunt_Entry, AIVoice_MoTaunt_Abi)
HOOK(0x0043a5e0, "AIVoice_MoTaunt", AIVoice_MoTaunt_Entry, nullptr);

// 0x0043a680 (ecx = object; vtable data 0x005aae88). "MO150002..4_Ambient" (range 300) -> +0xb50.
uint32_t AIVoice_MoAmbient(uint32_t self)
{
    static const uint32_t a[] = {0x005b48fc, 0x005b48e8, 0x005b48d4};
    return Field<uint32_t>(P(self), 0xb50) = Say(Pick(300, a), self);
}
static uint32_t AIVoice_MoAmbient_Abi(uint32_t ecx)
{
    return AIVoice_MoAmbient(ecx);
}
ABI_ENTRY_0043a680(AIVoice_MoAmbient_Entry, AIVoice_MoAmbient_Abi)
HOOK(0x0043a680, "AIVoice_MoAmbient", AIVoice_MoAmbient_Entry, nullptr);

// 0x0043a6e0 (ecx = object; vtable data 0x005aad70). Per-object delay: returns unless GameTime - +0xb48 >= 4.0
// (0x005bcbe0); then +0xb48 = now and "MO150011..15_Hurt" (range 500), not kept.
uint32_t AIVoice_MoHurt(uint32_t self)
{
    const float now = GameTime();
    if (!(now - Field<float>(P(self), 0xb48) >= K(0x005bcbe0)))
        return 0;
    Field<float>(P(self), 0xb48) = now;
    static const uint32_t a[] = {0x005b48c4, 0x005b48b4, 0x005b48a4, 0x005b4894, 0x005b4884};
    return Say(Pick(500, a), self);
}
static uint32_t AIVoice_MoHurt_Abi(uint32_t ecx)
{
    return AIVoice_MoHurt(ecx);
}
ABI_ENTRY_0043a6e0(AIVoice_MoHurt_Entry, AIVoice_MoHurt_Abi)
HOOK(0x0043a6e0, "AIVoice_MoHurt", AIVoice_MoHurt_Entry, nullptr);

// 0x0043a7b0 (ecx = object; vtable data 0x005aad7c). Shared delay Game+0xb64 (false when it has not passed),
// restarts it, stops +0xb50 (audio +0xd8 (&+0xb50, 0)), plays "MO1500xx_Dead" (range 600 with five strings: r >= 400
// all take the fifth); the new handle is not stored. Returns true.
uint32_t AIVoice_MoDead(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb64, now))
        return 0;
    SharedDelayRestart(0xb64, now);
    VCall<uint32_t>(Audio(), 0xd8 / 4, u(&Field<uint32_t>(P(self), 0xb50)), 0u);
    static const uint32_t a[] = {0x005b4874, 0x005b4864, 0x005b4854, 0x005b4844, 0x005b4834};
    Say(Pick(600, a), self);
    return 1;
}
static uint32_t AIVoice_MoDead_Abi(uint32_t ecx)
{
    return AIVoice_MoDead(ecx);
}
ABI_ENTRY_0043a7b0(AIVoice_MoDead_Entry, AIVoice_MoDead_Abi)
HOOK(0x0043a7b0, "AIVoice_MoDead", AIVoice_MoDead_Entry, nullptr);

// 0x0043c7b0 (ecx = object; stack: one unused argument, callee pops 4; vtable data 0x005a258c (Q112Object +0x14c),
// 0x005a41e4 (Q112Boss +0x14c)): 0x0043a100 with handle +0xb50.
uint32_t fn_0043c7b0(uint32_t self)
{
    return AIVoice_Q12Hurt(&Field<uint32_t>(P(self), 0xb50), self);
}
static uint32_t fn_0043c7b0_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return fn_0043c7b0(ecx);
}
ABI_ENTRY_0043c7b0(fn_0043c7b0_Entry, fn_0043c7b0_Abi)
HOOK(0x0043c7b0, "fn_0043c7b0", fn_0043c7b0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0043e450 (ecx = object; stack: outPosition, arg1, handle; callee pops 12; virtual +0x50 of 25 classes
// (PenguinObject, Q112Object, SquadObject, GenericAIObject, ...); not executed in recorded runs). Audio emitter
// position (string "GetAudioInfo : Can't find ic bone ..."). When Game+0x2684 is 0: Body_getLinearVelocity (eax =
// object, ecx = arg1); if handle is the object's voice +0xb40 and 0x00454780 (esi = object) is false, the bone
// +0xb44's matrix comes from 0x00453c50(object, +0xb44, &m, 1, 0, 1, 1, 0, 0) (on failure logs 0x005b2bc0 with the
// bone and audio +0xe8(handle), 5 printf words) and its translation (m[3], m[7], m[11]) is stored at outPosition;
// otherwise virtual +0x54(outPosition) (the plain object position).
uint32_t fn_0043e450(uint8_t* self, float* out, uint32_t arg1, uint32_t handle)
{
    if (Field<uint32_t>(g_game, 0x2684) != 0)
        return u(g_game);  // eax still holds Game
    orig::fn_00458000(u(self), arg1);
    if (handle == 0 || handle != Field<uint32_t>(self, 0xb40) || static_cast<uint8_t>(orig::fn_00454780(u(self))))
        return VCall<uint32_t>(self, 0x54 / 4, u(out));
    float m[17];
    if (!static_cast<uint8_t>(orig::fn_00453c50(u(self), Field<uint32_t>(self, 0xb44), u(m), 1, 0, 1, 1, 0, 0)))
    {
        const uint32_t name = VCall<uint32_t>(Audio(), 0xe8 / 4, handle);
        GameFunction<void(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00407ff0)(
            Field<uint32_t>(g_game, 0x34), 5, 0x005b2bc0u, Field<uint32_t>(self, 0xb44), name);
    }
    out[0] = m[3];
    uint32_t* o = reinterpret_cast<uint32_t*>(out);
    std::memcpy(&o[1], &m[7], 4);
    std::memcpy(&o[2], &m[11], 4);
    return u(out);
}
static uint32_t fn_0043e450_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0043e450(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<float*>(a0), a1, a2);
}
ABI_ENTRY_0043e450(fn_0043e450_Entry, fn_0043e450_Abi)
HOOK(0x0043e450, "fn_0043e450", fn_0043e450_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0044d1f0 (edi = position (passed through to audio +0xcc); stack: target, peasant; callee pops 8; caller
// 0x0044d600; not executed in recorded runs). Peasant drop-off (strings "ui_peasant_release", "*_PeasantDropOff"):
// plays "ui_peasant_release" on target (audio +0xc4 (0x005b1a88, target, 0, 0, -1, 0)); then at most every
// K(0x005bcb68) seconds (Game+0xba4; returns when below or unordered) a voice line: base 0x578 unless the peasant's
// name (0x00453270) is "peasant_man"; q11 = 0x00475d40(Game+0x748), jon = 0x00475d30(Game+0x748) (stored over the
// peasant argument slot); top = q11 ? (jon ? 2000 : 0x76b) : (jon ? 0x6a3 : 0x63f) and an offset (stored over the
// target argument slot) = q11 ? 0 : (jon ? 300 : target, unchanged; unreachable then since r <= 0x63f);
// r = (rand() * (top - base + 1)) >> 15 + base.
//   r < 0x578: fourteen "PE1/PM1/PE2/PM2..._PeasantDropOff" at the position (audio +0xcc (name, edi, Game+0x748, 0,
//   0, -1, 0)); < 0x5dc / < 0x640: "ROM50197/50199" (audio +0xd0 (name, Game+0x748, 0, 0, -1, 0)); < 0x6a4/0x708/
//   0x76c - offset: "Q1150173/79/81" on q11 (audio +0xc4); else "JON50185_PeasantDropOff" on jon.
uint32_t fn_0044d1f0(uint32_t position, uint32_t target, uint32_t peasant)
{
    VCall<uint32_t>(Audio(), 0xc4 / 4, 0x005b1a88u, target, 0u, 0u, 0xffffffffu, 0u);
    const float now = GameTime();
    if (!(now - Field<float>(g_game, 0xba4) >= K(0x005bcb68)))
        return 0;
    Field<float>(g_game, 0xba4) = now;
    const uint32_t name = orig::fn_00453270(peasant);
    const uint32_t base = Stricmp(name, 0x005bbe78u) != 0 ? 0x578u : 0u;
    const uint32_t player = Field<uint32_t>(g_game, 0x748);
    const uint32_t q11 = orig::fn_00475d40(player);
    const uint32_t jon = orig::fn_00475d30(player);
    uint32_t top;
    uint32_t offset = target;
    if (q11 == 0)
    {
        if (jon == 0)
            top = 0x63f;
        else
        {
            offset = 300;
            top = 0x6a3;
        }
    }
    else
    {
        offset = 0;
        top = jon != 0 ? 2000u : 0x76bu;
    }
    const uint32_t r =
        static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(Rand()) * (top - base + 1)) >> 15) + base;
    static const uint32_t kAt[] = {0x005b1a70, 0x005b1a58, 0x005b1a40, 0x005b1a28, 0x005b1a10, 0x005b19f8, 0x005b19e0,
                                   0x005b19c8, 0x005b19b0, 0x005b1998, 0x005b1980, 0x005b1968, 0x005b1950, 0x005b1938};
    if (r < 0x578)
        return VCall<uint32_t>(Audio(), 0xcc / 4, kAt[r / 100], position, Field<uint32_t>(g_game, 0x748), 0u, 0u,
                               0xffffffffu, 0u);
    if (r < 0x5dc)
        return VCall<uint32_t>(Audio(), 0xd0 / 4, 0x005b1920u, Field<uint32_t>(g_game, 0x748), 0u, 0u, 0xffffffffu, 0u);
    if (r < 0x640)
        return VCall<uint32_t>(Audio(), 0xd0 / 4, 0x005b1908u, Field<uint32_t>(g_game, 0x748), 0u, 0u, 0xffffffffu, 0u);
    if (r < 0x6a4u - offset)
        return VCall<uint32_t>(Audio(), 0xc4 / 4, 0x005b18f0u, q11, 0u, 0u, 0xffffffffu, 0u);
    if (r < 0x708u - offset)
        return VCall<uint32_t>(Audio(), 0xc4 / 4, 0x005b18d8u, q11, 0u, 0u, 0xffffffffu, 0u);
    if (r < 0x76cu - offset)
        return VCall<uint32_t>(Audio(), 0xc4 / 4, 0x005b18c0u, q11, 0u, 0u, 0xffffffffu, 0u);
    return VCall<uint32_t>(Audio(), 0xc4 / 4, 0x005b18a8u, jon, 0u, 0u, 0xffffffffu, 0u);
}
static uint32_t fn_0044d1f0_Abi(uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_0044d1f0(edi, a0, a1);
}
ABI_ENTRY_0044d1f0(fn_0044d1f0_Entry, fn_0044d1f0_Abi)
HOOK(0x0044d1f0, "fn_0044d1f0", fn_0044d1f0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Jon's voice (PlayerWeapon vtable 0x005ae210 slots +0x520..+0x53c; strings "JON5.../jon5..."); none executed in
// recorded runs.

// 0x0044ea40 (no inputs; vtable data 0x005ae730 = PlayerWeapon +0x520). Returns 0x7f + ((rand() * 2) >> 15), i.e.
// 127 or 128 (shl 1, sar 15).
uint32_t fn_0044ea40()
{
    return static_cast<uint32_t>(((Rand() << 1) >> 15) + 0x7f);
}
static uint32_t fn_0044ea40_Abi()
{
    return fn_0044ea40();
}
ABI_ENTRY_0044ea40(fn_0044ea40_Entry, fn_0044ea40_Abi)
HOOK(0x0044ea40, "fn_0044ea40", fn_0044ea40_Entry, nullptr);

// 0x0044ea50 (ecx = object; PlayerWeapon +0x528). "JON50057/58/59_Fight" (range 300) -> +0xb50.
uint32_t AIVoice_JonFight(uint32_t self)
{
    static const uint32_t a[] = {0x005b1554, 0x005b1544, 0x005b1534};
    return Field<uint32_t>(P(self), 0xb50) = Say(Pick(300, a), self);
}
static uint32_t AIVoice_JonFight_Abi(uint32_t ecx)
{
    return AIVoice_JonFight(ecx);
}
ABI_ENTRY_0044ea50(AIVoice_JonFight_Entry, AIVoice_JonFight_Abi)
HOOK(0x0044ea50, "AIVoice_JonFight", AIVoice_JonFight_Entry, nullptr);

// 0x0044eab0 (ecx = object; PlayerWeapon +0x524). Shared delay Game+0xb44 (false when it has not passed),
// restarts it, "JON50026_Fight"/"JON50028/29_Taunt" (range 300) -> +0xb50; returns true.
uint32_t AIVoice_JonTaunt(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x005b1524, 0x005b1514, 0x005b1504};
    Field<uint32_t>(P(self), 0xb50) = Say(Pick(300, a), self);
    return 1;
}
static uint32_t AIVoice_JonTaunt_Abi(uint32_t ecx)
{
    return AIVoice_JonTaunt(ecx);
}
ABI_ENTRY_0044eab0(AIVoice_JonTaunt_Entry, AIVoice_JonTaunt_Abi)
HOOK(0x0044eab0, "AIVoice_JonTaunt", AIVoice_JonTaunt_Entry, nullptr);

// 0x0044eb80 (ecx = object; PlayerWeapon +0x53c). "jon50062..66_Dead" (range 500) -> +0xb50; returns true.
uint32_t AIVoice_JonDead(uint32_t self)
{
    static const uint32_t a[] = {0x005b14f4, 0x005b14e4, 0x005b14d4, 0x005b14c4, 0x005b14b4};
    Field<uint32_t>(P(self), 0xb50) = Say(Pick(500, a), self);
    return 1;
}
static uint32_t AIVoice_JonDead_Abi(uint32_t ecx)
{
    return AIVoice_JonDead(ecx);
}
ABI_ENTRY_0044eb80(AIVoice_JonDead_Entry, AIVoice_JonDead_Abi)
HOOK(0x0044eb80, "AIVoice_JonDead", AIVoice_JonDead_Entry, nullptr);

// 0x0044ebf0 (ecx = object; PlayerWeapon +0x530). Per-object delay: returns unless GameTime - +0xb48 >=
// K(0x005bcb68); then +0xb48 = now and "jon50067/68_Hurt" (range 200), not kept.
uint32_t AIVoice_JonHurtShort(uint32_t self)
{
    const float now = GameTime();
    if (!(now - Field<float>(P(self), 0xb48) >= K(0x005bcb68)))
        return 0;
    Field<float>(P(self), 0xb48) = now;
    static const uint32_t a[] = {0x005b14a4, 0x005b1494};
    return Say(Pick(200, a), self);
}
static uint32_t AIVoice_JonHurtShort_Abi(uint32_t ecx)
{
    return AIVoice_JonHurtShort(ecx);
}
ABI_ENTRY_0044ebf0(AIVoice_JonHurtShort_Entry, AIVoice_JonHurtShort_Abi)
HOOK(0x0044ebf0, "AIVoice_JonHurtShort", AIVoice_JonHurtShort_Entry, nullptr);

// 0x0044ec60 (ecx = object; PlayerWeapon +0x538). Shared delay Game+0xb44 (false when it has not passed),
// restarts it, "jon50076..80_FriendlyFire" (range 500) -> +0xb50; returns true.
uint32_t AIVoice_PlayFriendlyfire(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x005b147c, 0x005b1464, 0x005b144c, 0x005b1434, 0x005b141c};
    Field<uint32_t>(P(self), 0xb50) = Say(Pick(500, a), self);
    return 1;
}
static uint32_t AIVoice_PlayFriendlyfire_Abi(uint32_t ecx)
{
    return AIVoice_PlayFriendlyfire(ecx);
}
ABI_ENTRY_0044ec60(AIVoice_PlayFriendlyfire_Entry, AIVoice_PlayFriendlyfire_Abi)
HOOK(0x0044ec60, "AIVoice_PlayFriendlyfire", AIVoice_PlayFriendlyfire_Entry, nullptr);

// 0x0044ed50 (ecx = object; stack: damage; callee pops 4; PlayerWeapon +0x534). When damage && (damage->flags &
// 0x20000) (guess: sniper hit) and the sniper delay Game+0xb94 (min +0xb98, max +0xb9c, delay +0xba0) has passed:
// restarts it and Rooney comments "ROM50103_Sniper" ((rand() * 200 & 0xffff8000) < 0x320000) or "ROM50104_Sniper"
// (audio +0xd0 (name, object, 0, 0, -1, 0)); returns true. Otherwise the shared delay Game+0xb44 (false when it has
// not passed): restarts it, "jon500xx_help/_Hurt" (range 600) -> +0xb50; returns true.
uint32_t AIVoice_JonHurt(uint32_t self, uint32_t damage)
{
    const float now = GameTime();
    if (damage != 0 && (Field<uint32_t>(P(damage), 0) & 0x20000) && SharedDelayPassed(0xb94, now))
    {
        SharedDelayRestart(0xb94, now);
        const uint32_t name =
            ((Rand() * 200) & static_cast<int32_t>(0xffff8000u)) < 0x320000 ? 0x005b140cu : 0x005b13fcu;
        VCall<uint32_t>(Audio(), 0xd0 / 4, name, self, 0u, 0u, 0xffffffffu, 0u);
        return 1;
    }
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x005b13ec, 0x005b13dc, 0x005b13cc, 0x005b13bc, 0x005b13ac, 0x005b139c};
    Field<uint32_t>(P(self), 0xb50) = Say(Pick(600, a), self);
    return 1;
}
static uint32_t AIVoice_JonHurt_Abi(uint32_t ecx, uint32_t a0)
{
    return AIVoice_JonHurt(ecx, a0);
}
ABI_ENTRY_0044ed50(AIVoice_JonHurt_Entry, AIVoice_JonHurt_Abi)
HOOK(0x0044ed50, "AIVoice_JonHurt", AIVoice_JonHurt_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0044f510 (esi = sound reference: +0xc = sound, +0x20 = use count; caller 0x004928f0; not executed in recorded
// runs). First use starts it: audio +0xe0 (+0xc, 0, 0); always ++count. Returns the old count, or the start result.
uint32_t fn_0044f510(uint8_t* ref)
{
    uint32_t r = Field<uint32_t>(ref, 0x20);
    if (r == 0)
        r = VCall<uint32_t>(Audio(), 0xe0 / 4, Field<uint32_t>(ref, 0xc), 0u, 0u);
    ++Field<uint32_t>(ref, 0x20);
    return r;
}
static uint32_t fn_0044f510_Abi(uint32_t esi)
{
    return fn_0044f510(reinterpret_cast<uint8_t*>(esi));
}
ABI_ENTRY_0044f510(fn_0044f510_Entry, fn_0044f510_Abi)
HOOK(0x0044f510, "fn_0044f510", fn_0044f510_Entry, nullptr);

// 0x0044f540 (eax = sound reference; caller 0x004928f0; not executed in recorded runs). --count; at zero stops it:
// audio +0xe4 (+0xc, 0, 0). Returns the reference, or the stop result.
uint32_t fn_0044f540(uint8_t* ref)
{
    if (--Field<uint32_t>(ref, 0x20) != 0)
        return u(ref);
    return VCall<uint32_t>(Audio(), 0xe4 / 4, Field<uint32_t>(ref, 0xc), 0u, 0u);
}
static uint32_t fn_0044f540_Abi(uint32_t eax)
{
    return fn_0044f540(reinterpret_cast<uint8_t*>(eax));
}
ABI_ENTRY_0044f540(fn_0044f540_Entry, fn_0044f540_Abi)
HOOK(0x0044f540, "fn_0044f540", fn_0044f540_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0044f580 (ebx: not an input (`or ebx, -1`); stack: music state, dt; callee pops 8; callers 0x00402e80,
// 0x004a2ba0; executed in recorded runs). Dynamic music: picks the track for the current situation from the
// per-level tables 0x005ce4e0 + 0x1c * (Game+0x2708 - 1) (states 0 "lNamb", 1 "stealth", 2 "skirm2", 3 "lNbat",
// 4 "skirm1", 5 "lNvic", 6 "lNdef"; -1 = silence) and plays it with audio +0xd4 (name, 0, 1, flag). Music state:
// +0 timer, +4 time the current track has played, +8 hold timer, +0xc track handle, +0x10/+0x11/+0x12 victory/
// no-fade/defeat requests, +0x14/+0x18 enemy counts (near/far), +0x1c state, +0x20 lock count.
// Thresholds in Game: +0xd6c/+0xd70 count limits, +0xd74/+0xd80 radii, +0xd78/+0xd7c last battle/stealth time
// (re-armed after 180 s, 0x005bcbb8), +0xd84 minimum track time, +0xd88/+0xd8c hold clamp.
// The original keeps the "flag" byte in the low byte of its dt argument slot (upper bytes stay dt's) and pushes
// that dword as the fourth play argument; reproduced. The squared distance helper 0x004e8080 (eax = a, ecx = b,
// st0 = |a - b|^2, no side effects) is computed inline in its own operation order.
uint32_t Music_Update(uint8_t* m, uint32_t dtBits)
{
    if (Field<uint32_t>(g_game, 0x2684) != 0 && Field<uint32_t>(g_game, 0x748) != 0)
        return u(g_game);
    if (Field<uint32_t>(m, 0x20) != 0)
        return u(g_game);
    const float dt = bf(dtBits);
    void* audio = Audio();
    uint32_t playing = 0;
    if (static_cast<uint8_t>(VCall<uint32_t>(audio, 0xdc / 4, Field<uint32_t>(audio, 0x98))))
        playing = VCall<uint32_t>(Field<void*>(Field<void*>(audio, 0x90), 0x78), 0x18 / 4);
    int32_t next = -1;
    float& timer = Field<float>(m, 0);
    float& played = Field<float>(m, 4);
    float& hold = Field<float>(m, 8);
    int32_t& state = Field<int32_t>(m, 0x1c);
    if (state == -1 && playing != 0)
        return 0;
    timer = timer - dt;
    if (playing)
        played = dt + played;
    else
        Field<uint32_t>(m, 4) = 0;
    if (hold > 0.0f)
    {
        hold = hold - dt;
        return 0;
    }
    audio = Audio();
    if (static_cast<uint8_t>(VCall<uint32_t>(audio, 0xdc / 4, Field<uint32_t>(audio, 0x98))) &&
        Field<uint32_t>(audio, 0x94) != 0 && Field<uint32_t>(audio, 0x94) + 0xfc != 0)
    {
        Field<uint32_t>(m, 8) = 0x41200000;  // 10.0f
        return 0;
    }
    uint8_t flag = 0;
    if (played >= Field<float>(g_game, 0xd84) && Field<float>(g_game, 0xd84) > 0.0f)
        Field<uint32_t>(m, 0) = 0x3f800000;  // 1.0f; next stays -1
    else
        next = state;
    bool decided = false;
    if (playing != 0 && state != 5 && state != 6 && !static_cast<uint8_t>(orig::fn_0044e600()))
    {
        if (Field<uint8_t>(m, 0x12))
        {
            next = 6;
            Field<uint32_t>(m, 0) = 0x3f800000;
            flag = 1;
            decided = true;
        }
        else if (Field<uint8_t>(m, 0x10))
        {
            next = 5;
            Field<uint32_t>(m, 0) = 0x3f800000;
            flag = Field<uint8_t>(m, 0x11) == 0 ? 1 : 0;
            decided = true;
        }
    }
    if (!decided && timer <= 0.0f)
    {
        if (static_cast<uint8_t>(orig::fn_0044e600()) || Field<uint32_t>(g_game, 0x748) == 0)
        {
            next = 0;
        }
        else
        {
            uint32_t iterator[2] = {0, 0};
            timer = orig::fn_0046e2c0(0, orig::f2u(0.5f), orig::f2u(1.0f));
            Field<uint32_t>(m, 0x14) = 0;
            Field<uint32_t>(m, 0x18) = 0;
            float playerPos[3];
            VCall<uint32_t>(Field<void*>(g_game, 0x748), 0x54 / 4, u(playerPos));
            const uint32_t list = Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xe7c);
            for (uint32_t obj = orig::fn_004e7340(list, u(iterator)); obj; obj = orig::fn_004e73c0(u(iterator)))
            {
                if (static_cast<uint8_t>(VCall<uint32_t>(P(obj), 0xe4 / 4)))
                    continue;
                if (!static_cast<uint8_t>(VCall<uint32_t>(P(obj), 0xe8 / 4)))
                    continue;
                if (static_cast<uint8_t>(orig::fn_00454780(obj)))
                    continue;
                float pos[3];
                VCall<uint32_t>(P(obj), 0x54 / 4, u(pos));
                const float dz = playerPos[2] - pos[2];
                const float dy = playerPos[1] - pos[1];
                const float dx = playerPos[0] - pos[0];
                const float dist = dx * dx + dy * dy + dz * dz;
                bool countNear = true;
                float radius = 0.0f;
                if (static_cast<uint8_t>(VCall<uint32_t>(P(obj), 0x10c / 4)))
                {
                    if (orig::fn_0042c6f0(obj) == 0)
                        countNear = false;
                    else
                        radius = Field<float>(g_game, 0xd74);
                }
                else
                {
                    radius = Field<float>(g_game, 0xd80);
                }
                if (countNear && dist <= radius * radius)
                    ++Field<uint32_t>(m, 0x14);
                const float far = Field<float>(g_game, 0xd80);
                if (dist <= far * far)
                    ++Field<uint32_t>(m, 0x18);
            }
            const float now = GameTime();
            const float rearm = K(0x005bcbb8);
            const uint32_t nearCount = Field<uint32_t>(m, 0x14);
            const uint32_t farCount = Field<uint32_t>(m, 0x18);
            const uint32_t nearLimit = Field<uint32_t>(g_game, 0xd70);
            const uint32_t farLimit = Field<uint32_t>(g_game, 0xd6c);
            float& lastBattle = Field<float>(g_game, 0xd78);
            float& lastStealth = Field<float>(g_game, 0xd7c);
            // 0x0044fa8f / 0x0044f88f: battle (2) when armed and re-armed, else lNbat (3), arming the time if unset.
            auto battle = [&]() {
                if (lastBattle > 0.0f && now - rearm >= lastBattle)
                {
                    Field<uint32_t>(g_game, 0xd78) = GameGlobal<uint32_t>(0x00604a98);
                    next = 2;
                    return;
                }
                if (lastBattle == 0.0f)
                    Field<uint32_t>(g_game, 0xd78) = GameGlobal<uint32_t>(0x00604a98);
                next = 3;
            };
            // 0x0044fa10: stealth (1) when armed and re-armed; otherwise silence when nobody is near.
            auto stealthOrSilence = [&]() {
                if (lastStealth > 0.0f && now - rearm >= lastStealth)
                {
                    Field<uint32_t>(g_game, 0xd7c) = GameGlobal<uint32_t>(0x00604a98);
                    next = 1;
                    return;
                }
                if (farCount == 0 && nearCount == 0)
                    next = -1;
            };
            const bool manyNear = static_cast<double>(nearCount) >= static_cast<double>(nearLimit) * 0.5;
            switch (state)
            {
            case 0:
                if (nearCount >= nearLimit)
                    battle();
                else if (farCount >= farLimit && now - rearm >= lastStealth)
                {
                    Field<uint32_t>(g_game, 0xd7c) = GameGlobal<uint32_t>(0x00604a98);
                    next = 1;
                }
                break;
            case 1:
                if (nearCount >= nearLimit)
                    battle();
                else if (farCount == 0)
                    next = -1;
                break;
            case 2:
            case 4:
                if (manyNear)
                {
                    if (now - rearm >= lastBattle)
                    {
                        Field<uint32_t>(g_game, 0xd78) = GameGlobal<uint32_t>(0x00604a98);
                        next = 3;
                    }
                }
                else
                    stealthOrSilence();
                break;
            case 3:
                if (manyNear)
                {
                    if (now - rearm >= lastBattle)
                    {
                        Field<uint32_t>(g_game, 0xd78) = GameGlobal<uint32_t>(0x00604a98);
                        next = 4;
                    }
                }
                else
                    stealthOrSilence();
                break;
            case 5:
            case 6:
                if (playing == 0)
                {
                    Field<uint8_t>(m, 0x10) = 0;
                    Field<uint8_t>(m, 0x12) = 0;
                    Field<uint32_t>(m, 8) = 0x40a00000;  // 5.0f
                    next = -1;
                }
                break;
            case -1:
                if (nearCount >= nearLimit)
                    battle();
                else if (farCount >= farLimit)
                {
                    if (lastStealth > 0.0f && now - rearm >= lastStealth)
                    {
                        Field<uint32_t>(g_game, 0xd7c) = GameGlobal<uint32_t>(0x00604a98);
                        next = 1;
                    }
                    else
                    {
                        if (lastStealth == 0.0f)
                            Field<uint32_t>(g_game, 0xd7c) = GameGlobal<uint32_t>(0x00604a98);
                        next = 0;
                    }
                }
                else if (hold <= 0.0f)
                    next = 0;
                break;
            default:
                break;
            }
            if (iterator[1])
                --Field<int32_t>(P(iterator[1]), 0x18);
        }
    }
    if (next == state)
        return 0;
    state = next;
    if (next == -1)
    {
        hold = played;
        if (played < Field<float>(g_game, 0xd88))
            Field<uint32_t>(m, 8) = Field<uint32_t>(g_game, 0xd88);
        else if (played > Field<float>(g_game, 0xd8c))
            Field<uint32_t>(m, 8) = Field<uint32_t>(g_game, 0xd8c);
        const uint32_t fade =
            (!static_cast<uint8_t>(orig::fn_0044e600()) && Field<uint32_t>(g_game, 0x748) != 0) ? 1u : 0u;
        return VCall<uint32_t>(Audio(), 0xd8 / 4, u(m + 0xc), fade);
    }
    uint32_t name;
    uint32_t last = 0;
    if (static_cast<uint8_t>(orig::fn_0044e600()))
    {
        Field<uint32_t>(m, 8) = 0x49127c00;  // 600000.0f
        name = 0x005b107c;                  // "basedefend"
    }
    else if (Field<uint32_t>(g_game, 0x748) == 0)
    {
        Field<uint32_t>(m, 8) = 0x49127c00;
        name = 0x005b1100;  // "l2amb"
    }
    else
    {
        Field<uint32_t>(m, 8) = 0x41200000;  // 10.0f
        const uint32_t level = Field<uint32_t>(g_game, 0x2708) - 1;
        if (level > 4)
            return level;
        name = GameGlobal<uint32_t>(0x005ce4e0 + level * 0x1c + static_cast<uint32_t>(next) * 4);
        last = (dtBits & 0xffffff00u) | flag;
    }
    return Field<uint32_t>(m, 0xc) = VCall<uint32_t>(Audio(), 0xd4 / 4, name, 0u, 1u, last);
}
static uint32_t Music_Update_Abi(uint32_t /*ebx*/, uint32_t a0, uint32_t a1)
{
    return Music_Update(reinterpret_cast<uint8_t*>(a0), a1);
}
ABI_ENTRY_0044f580(Music_Update_Entry, Music_Update_Abi)
HOOK(0x0044f580, "Music_Update", Music_Update_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004619c0 (no inputs; vtable data 0x005a2d2c, guess: slot 1 of the SoundEar vtable). Returns the class
// descriptor 0x00580f08 (its +0x14 is "CLASS(SoundEar)").
uint32_t SoundEar_GetClass()
{
    return 0x00580f08;
}
static uint32_t SoundEar_GetClass_Abi()
{
    return SoundEar_GetClass();
}
ABI_ENTRY_004619c0(SoundEar_GetClass_Entry, SoundEar_GetClass_Abi)
HOOK(0x004619c0, "SoundEar_GetClass", SoundEar_GetClass_Entry, nullptr);

// 0x00461c80 (no inputs; FxEarObject vtable 0x005a8f70 slot 1). Returns the class descriptor 0x00580de8 (its +0x14 is
// "CLASS(FxEarObject)").
uint32_t FxEarObject_GetClass()
{
    return 0x00580de8;
}
static uint32_t FxEarObject_GetClass_Abi()
{
    return FxEarObject_GetClass();
}
ABI_ENTRY_00461c80(FxEarObject_GetClass_Entry, FxEarObject_GetClass_Abi)
HOOK(0x00461c80, "FxEarObject_GetClass", FxEarObject_GetClass_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00468dd0 (ecx = pub object; caller 0x00467fe0; not executed in recorded runs). Pub ambience lines (strings
// "PE1500xx_Pub_..."), all through audio +0xd0 (name, 0, 0, 0, -1, 0). One-shot intro lines first: when Game+0x574
// is clear it is set, 0x00401f40(Game, 0) runs and 0x005a093c ("..._Pub_Once_Intro", guess from order) plays;
// otherwise the first set request byte of +0x141/+0x142/+0x143 sets Game+0x575/+0x576/+0x577, runs 0x00401f40(Game,
// 0) and plays 0x005a0920/0x005a0904/0x005a08ec. Otherwise the enabled extras +0x144..+0x148 (ids 0..4) are
// collected in a local list; r = (rand() * (15 + count)) >> 15: r < 15 plays one of fifteen general lines (index
// (rand() * 15) >> 15, the table the original builds in its frame); else extra id list[r - 15] plays 0x005a08cc/
// 0x005a08b0/0x005a0894/0x005a0874/0x005a0854. Returns the play call's eax (0 for an unknown id).
uint32_t Pub_PlayAmbientLine(uint8_t* pub)
{
    uint32_t frame[20];
    static const uint32_t kLines[15] = {0x005a0a94, 0x005a0a7c, 0x005a0a64, 0x005a0a4c, 0x005a0a34,
                                        0x005a0a1c, 0x005a0a04, 0x005a09ec, 0x005a09d4, 0x005a09bc,
                                        0x005a09a4, 0x005a098c, 0x005a0978, 0x005a0964, 0x005a0954};
    for (int i = 0; i < 15; ++i)
        frame[5 + i] = kLines[i];
    uint8_t* game = reinterpret_cast<uint8_t*>(g_game);
    auto play = [](uint32_t name) {
        return VCall<uint32_t>(Audio(), 0xd0 / 4, name, 0u, 0u, 0u, 0xffffffffu, 0u);
    };
    if (Field<uint8_t>(game, 0x574) == 0)
    {
        Field<uint8_t>(game, 0x574) = 1;
        orig::fn_00401f40(u(game), 0);
        return play(0x005a093c);
    }
    static const uint32_t kOnce[3] = {0x005a0920, 0x005a0904, 0x005a08ec};
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (Field<uint8_t>(pub, 0x141 + i))
        {
            Field<uint8_t>(game, 0x575 + i) = 1;
            orig::fn_00401f40(u(game), 0);
            return play(kOnce[i]);
        }
    }
    int32_t count = 0;
    for (uint32_t id = 0; id < 5; ++id)
    {
        if (Field<uint8_t>(pub, 0x144 + id))
            frame[count++] = id;
    }
    const int32_t r = (Rand() * (count + 15)) >> 15;
    if (r < 15)
        return play(frame[5 + ((Rand() * 15) >> 15)]);
    static const uint32_t kExtra[5] = {0x005a08cc, 0x005a08b0, 0x005a0894, 0x005a0874, 0x005a0854};
    const uint32_t id = frame[r - 15];
    if (id < 5)
        return play(kExtra[id]);
    return 0;
}
static uint32_t Pub_PlayAmbientLine_Abi(uint32_t ecx)
{
    return Pub_PlayAmbientLine(reinterpret_cast<uint8_t*>(ecx));
}
ABI_ENTRY_00468dd0(Pub_PlayAmbientLine_Entry, Pub_PlayAmbientLine_Abi)
HOOK(0x00468dd0, "Pub_PlayAmbientLine", Pub_PlayAmbientLine_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0046a790 (ecx = object; vtable data 0x005a1e84 (StigTurret +0x4ac, guess); executed in recorded runs). Starts
// the "amb_pub_exterior_lp" loop once: when +0x4cc is 0, +0x4cc = audio +0xc4 (0x005a05f0, object, 1, 0, -1, 0),
// or -1 when that returns 0. Returns +0x4cc's old value or the play result.
uint32_t Pub_StartExteriorLoop(uint8_t* self)
{
    if (Field<uint32_t>(self, 0x4cc) != 0)
        return Field<uint32_t>(self, 0x4cc);
    const uint32_t h = VCall<uint32_t>(Audio(), 0xc4 / 4, 0x005a05f0u, u(self), 1u, 0u, 0xffffffffu, 0u);
    Field<uint32_t>(self, 0x4cc) = h;
    if (h == 0)
        Field<uint32_t>(self, 0x4cc) = 0xffffffff;
    return h;
}
static uint32_t Pub_StartExteriorLoop_Abi(uint32_t ecx)
{
    return Pub_StartExteriorLoop(reinterpret_cast<uint8_t*>(ecx));
}
ABI_ENTRY_0046a790(Pub_StartExteriorLoop_Entry, Pub_StartExteriorLoop_Abi)
HOOK(0x0046a790, "Pub_StartExteriorLoop", Pub_StartExteriorLoop_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00472400 (no inputs; twelve pickup callers 0x00472470..0x00472770; executed in recorded runs). Ammo pickup
// sound: r = (rand() * 100) >> 15; "ui_pickup_ammo_1" (r < 33), "_2" (r < 66) or "_3" through audio +0xd0 (name,
// 0, 0, 0, -1, 0).
uint32_t Pickup_PlayAmmoSound()
{
    const int32_t r = (Rand() * 100) >> 15;
    const uint32_t name = r < 0x21 ? 0x005a00f4u : r < 0x42 ? 0x005a00e0u : 0x005a00ccu;
    return VCall<uint32_t>(Audio(), 0xd0 / 4, name, 0u, 0u, 0u, 0xffffffffu, 0u);
}
static uint32_t Pickup_PlayAmmoSound_Abi()
{
    return Pickup_PlayAmmoSound();
}
ABI_ENTRY_00472400(Pickup_PlayAmmoSound_Entry, Pickup_PlayAmmoSound_Abi)
HOOK(0x00472400, "Pickup_PlayAmmoSound", Pickup_PlayAmmoSound_Entry, nullptr);

// 0x00472790 (eax = pickup object, ecx: not read by this function (the tracker's ecx input is _stricmp's local
// reservation), edi = collector; callers 0x00472800, 0x00472850; not executed in recorded runs). Health pickup
// sound: without a collector "ui_pickup_health" through audio +0xd0 (name, 0, 0, 0, -1, 0); otherwise audio +0xc4
// (name, collector, 0, 0, -1, 0) with "ROM50193_Ambient" when the pickup is named "pickup_healthytea" (0x00453270),
// else "ui_pickup_health".
uint32_t Pickup_PlayHealthSound(uint32_t pickup, uint32_t collector)
{
    if (collector == 0)
        return VCall<uint32_t>(Audio(), 0xd0 / 4, 0x005a00b8u, 0u, 0u, 0u, 0xffffffffu, 0u);
    uint32_t name = 0x005a00b8;
    if (pickup != 0 && Stricmp(orig::fn_00453270(pickup), 0x005b05a8u) == 0)
        name = 0x005a00a4;
    return VCall<uint32_t>(Audio(), 0xc4 / 4, name, collector, 0u, 0u, 0xffffffffu, 0u);
}
static uint32_t Pickup_PlayHealthSound_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t edi)
{
    return Pickup_PlayHealthSound(eax, edi);
}
ABI_ENTRY_00472790(Pickup_PlayHealthSound_Entry, Pickup_PlayHealthSound_Abi)
HOOK(0x00472790, "Pickup_PlayHealthSound", Pickup_PlayHealthSound_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0047bb30 (eax = enable test, esi = vehicle, edi = &glide handle; stack: open; caller pops; eight callers;
// not executed in recorded runs). Jumper wing sounds (strings "veh_jumper_..."): nothing when eax is 0. Opening:
// audio +0xc8 ("veh_jumper_wings_out", vehicle, 0, 0, -1), then *handle = audio +0xc8 ("veh_jumper_glide_lp",
// vehicle, 1, 0, -1). Closing: audio +0xc8 ("veh_jumper_wings_in", vehicle, 0, 0, -1), then audio +0xd8 (handle, 0)
// stops the glide loop. Returns eax of the last call (edx: the original leaves the callee's edx; 0 here).
uint64_t Jumper_PlayWingSounds(uint32_t enable, uint32_t vehicle, uint32_t* handle, uint32_t open)
{
    if (enable == 0)
        return 0;
    if (static_cast<uint8_t>(open))
    {
        VCall<uint32_t>(Audio(), 0xc8 / 4, 0x0059e5a0u, vehicle, 0u, 0u, 0xffffffffu);
        const uint32_t h = VCall<uint32_t>(Audio(), 0xc8 / 4, 0x0059e578u, vehicle, 1u, 0u, 0xffffffffu);
        *handle = h;
        return h;
    }
    VCall<uint32_t>(Audio(), 0xc8 / 4, 0x0059e58cu, vehicle, 0u, 0u, 0xffffffffu);
    return VCall<uint32_t>(Audio(), 0xd8 / 4, u(handle), 0u);
}
static uint64_t Jumper_PlayWingSounds_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return Jumper_PlayWingSounds(eax, esi, reinterpret_cast<uint32_t*>(edi), a0);
}
ABI_ENTRY_0047bb30(Jumper_PlayWingSounds_Entry, Jumper_PlayWingSounds_Abi)
HOOK(0x0047bb30, "Jumper_PlayWingSounds", Jumper_PlayWingSounds_Entry, nullptr);
