// Recovered: audio-classified game functions, batch audio_02 (0x004857e0-0x004fa7f0). Static recovery 2026-10-05,
// compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions as in src/audio/audio_01.cpp: every function's comment gives its address, its register/stack ABI (as
// the original reads it), what it does and the evidence. "guess" marks interpretations not proven by strings or
// behaviour. Names stay fn_XXXXXXXX unless the behaviour was verified from the disassembly.
//
// Game+0x2c is the audio object. Its PC implementation has vtable 0x0058a230 (85 slots, tools/vtable.py); the
// constructor 0x004f20d0 installs the abstract base vtable 0x0058aa40 (slot 1 is _purecall), so
// the audio class has no CLASS() descriptor. Slot offsets below are offsets into these vtables (they agree).
// The sound system keeps:
//   +0x28 list of playing voices (iterated with 0x004e7340 (eax = list, esi = &iterator) / 0x004e73c0 (edi =
//         &iterator); the iterator is {list, chunk} and the chunk's +0x18 counts iterators: it is decremented when the
//         iteration ends), +0x30 free-voice pool (singly linked through voice+0), +0x74 ambient sounds (0x00500230),
//   +0x78 definition count, +0x80 definition table, +0x90 current music voice (guess), +0x9c frame time,
//   +0xa0 definition name map (0x004f1160), +0xa8 pause depth, function pointers (driver callbacks, cdecl) at +0x4c..
//   +0x64 (+0x50 "is buffer playing", +0x58 "release buffer", +0x64 "is listener object", guesses from use).
// A voice: +0 flags, +4 handle, +8 buffer, +0xc definition (+0x20 definition flags, +0xbc name), +0x10/+0x14 volume
// offsets, +0x1c, +0x20 position, +0x2c velocity, +0x58 fade level, +0x8c/+0x90/+0x94 fade timers.
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
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
inline void* Audio()
{
    return Field<void*>(g_game, 0x2c);
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
inline float GameTime()
{
    return GameGlobal<float>(0x00604a98);
}
const uint32_t kHeap = 0x0063a210;  // PMS heap used by PMS_heapAlloc 0x004e07f0 / PMS_heapFree 0x004e08d0
inline uint32_t HeapAlloc(uint32_t size, uint32_t align, uint32_t tag)
{
    return orig::fn_004e07f0(kHeap, align, size, 2, tag);
}
inline uint32_t HeapFree(uint32_t p)
{
    return orig::fn_004e08d0(kHeap, p);
}

// List iteration (see the header comment).
struct Iter
{
    uint32_t list;
    uint32_t chunk;
};
inline uint32_t IterFirst(uint32_t list, Iter* it)
{
    return orig::fn_004e7340(list, u(it));
}
inline uint32_t IterNext(Iter* it)
{
    return orig::fn_004e73c0(u(it));
}
// End of an iteration: returns the chunk (eax in the originals).
inline uint32_t IterEnd(Iter* it)
{
    if (it->chunk)
        --Field<int32_t>(P(it->chunk), 0x18);
    return it->chunk;
}
// Leaving an iteration early: virtual +0xc of the list with the chunk, then the chunk's iterator count drops; returns
// the virtual's eax.
inline uint32_t IterBreak(Iter* it)
{
    const uint32_t chunk = it->chunk;
    const uint32_t r = VCall<uint32_t>(P(it->list), 0xc / 4, chunk);
    --Field<int32_t>(P(chunk), 0x18);
    return r;
}

uint32_t Say(uint32_t name, uint32_t object)
{
    return VCall<uint32_t>(Audio(), 0xc4 / 4, name, object, 0u, 0u, 0xffffffffu, 0u);
}
uint32_t Say2D(uint32_t name, uint32_t object)
{
    return VCall<uint32_t>(Audio(), 0xd0 / 4, name, object, 0u, 0u, 0xffffffffu, 0u);
}
// r = (rand() * range) >> 15 (imul, sar); the originals compare r with jae (unsigned) against 100, 200, ...
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

// ---------------------------------------------------------------------------------------------------------------
// 0x004857e0 (esi = object; caller 0x00485910; executed in recorded runs). Q11 ambient remark (strings
// "q1150026_Ambient", "q1150174_Ambient_Spec"): when virtual +0x268 returns true, (+0xbcc & 0x20020) == 0, the player
// (Game+0x748) health (0x004555b0) <= max (0x004555c0) * K(0x005bcbe4) (fcompp, ah & 1) and the squared distance
// between the two positions (virtual +0x54 of each into a local vector; summed z, y, x in the disassembly's order)
// is below K(0x005bccb8): (rand() * 100) & 0xffff8000 < 0x190000 (signed) plays "q1150174_Ambient_Spec" with audio
// +0xd0, else "q1150026_Ambient" with audio +0xc4 (name, object, 0, 0, -1, 0); then virtual +0x19c(0x5a, 0, 0, 0, 0, 0),
// +0xb5c = 9 and returns true (al; upper bytes from the +0x19c call). Otherwise returns false.
uint32_t Q11Voice_PlayAmbient(uint8_t* self)
{
    if (!static_cast<uint8_t>(VCall<uint32_t>(self, 0x268 / 4)))
        return 0;
    if (Field<uint32_t>(self, 0xbcc) & 0x20020)
        return 0;
    const uint32_t player = Field<uint32_t>(g_game, 0x748);
    const float health = orig::fn_004555b0(player);
    const float limit = orig::fn_004555c0(player) * K(0x005bcbe4);  // eax still holds the player
    if (!(limit >= health))
        return 0;
    float a[3];
    float b[3];
    VCall<uint32_t>(self, 0x54 / 4, u(a));
    VCall<uint32_t>(P(player), 0x54 / 4, u(b));
    const float dz = b[2] - a[2];
    const float dy = b[1] - a[1];
    const float dx = b[0] - a[0];
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (!(d2 < K(0x005bccb8)))
        return 0;
    const int32_t roll = Rand() * 100;
    if ((roll & static_cast<int32_t>(0xffff8000u)) < 0x190000)
        Say2D(0x0059df84u, u(self));
    else
        Say(0x0059df70u, u(self));
    const uint32_t r = VCall<uint32_t>(self, 0x19c / 4, 0x5au, 0u, 0u, 0u, 0u, 0u);
    Field<uint32_t>(self, 0xb5c) = 9;
    return (r & 0xffffff00u) | 1u;
}
static uint32_t Q11Voice_PlayAmbient_Abi(uint32_t esi)
{
    return Q11Voice_PlayAmbient(reinterpret_cast<uint8_t*>(esi));
}
ABI_ENTRY_004857e0(Q11Voice_PlayAmbient_Entry, Q11Voice_PlayAmbient_Abi)
HOOK(0x004857e0, "Q11Voice_PlayAmbient", Q11Voice_PlayAmbient_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Q11 voice lines (0x00487050-0x004876a0): virtual methods (ecx = object) of one character class, vtable data
// 0x005a6f34..0x005a6f4c (vtable 0x005a6cc0 +0x274..+0x28c, guess for the vtable start); none executed in recorded
// runs. Each picks one "q11..." string with r = (rand() * range) >> 15 and plays it with audio +0xc4 (name, object, 0,
// 0, -1, 0) unless stated.

// 0x00487050 (vtable data 0x005a6f4c). "q1150051..59_Dead" (7 strings, range 700); returns true (al; upper bytes from
// the audio call).
uint32_t Q11Voice_PlayDead(uint32_t self)
{
    static const uint32_t a[] = {0x0059df0c, 0x0059defc, 0x0059deec, 0x0059dedc, 0x0059decc, 0x0059debc, 0x0059deac};
    return (Say(Pick(700, a), self) & 0xffffff00u) | 1u;
}
static uint32_t Q11Voice_PlayDead_Abi(uint32_t ecx)
{
    return Q11Voice_PlayDead(ecx);
}
ABI_ENTRY_00487050(Q11Voice_PlayDead_Entry, Q11Voice_PlayDead_Abi)
HOOK(0x00487050, "Q11Voice_PlayDead", Q11Voice_PlayDead_Entry, nullptr);

// 0x00487150 (vtable data 0x005a6f40). Per-object delay: returns unless GameTime - +0xb48 >= K(0x005bcb68) (fcomp,
// ah & 1); then +0xb48 = now and "q1150060/61/62_Hurt" (range 300); the handle is not kept.
uint32_t Q11Voice_PlayHurt(uint32_t self)
{
    const float now = GameTime();
    if (!(now - Field<float>(P(self), 0xb48) >= K(0x005bcb68)))
        return 0;
    Field<float>(P(self), 0xb48) = now;
    static const uint32_t a[] = {0x0059de9c, 0x0059de8c, 0x0059de7c};
    return Say(Pick(300, a), self);
}
static uint32_t Q11Voice_PlayHurt_Abi(uint32_t ecx)
{
    return Q11Voice_PlayHurt(ecx);
}
ABI_ENTRY_00487150(Q11Voice_PlayHurt_Entry, Q11Voice_PlayHurt_Abi)
HOOK(0x00487150, "Q11Voice_PlayHurt", Q11Voice_PlayHurt_Entry, nullptr);

// 0x004871e0 (vtable data 0x005a6f48). Shared delay Game+0xb44 (returns false when it has not passed), restarts it,
// then "q1150068..73_FriendlyFire" (range 600) -> +0xb50; returns true.
uint32_t Q11Voice_PlayFriendlyFire(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x0059de64, 0x0059de4c, 0x0059de34, 0x0059de1c, 0x0059de04, 0x0059ddec};
    Field<uint32_t>(P(self), 0xb50) = Say(Pick(600, a), self);
    return 1;
}
static uint32_t Q11Voice_PlayFriendlyFire_Abi(uint32_t ecx)
{
    return Q11Voice_PlayFriendlyFire(ecx);
}
ABI_ENTRY_004871e0(Q11Voice_PlayFriendlyFire_Entry, Q11Voice_PlayFriendlyFire_Abi)
HOOK(0x004871e0, "Q11Voice_PlayFriendlyFire", Q11Voice_PlayFriendlyFire_Entry, nullptr);

// 0x004872e0 (ecx = object; stack: damage; callee pops 4; vtable data 0x005a6f44). With damage && (damage->flags &
// 0x20000) and the shared delay Game+0xb94 passed: restarts it and plays "Q1150092_Sniper" when (rand() * 200) &
// 0xffff8000 < 0x320000 (signed), else "Q1150093_Sniper", both with audio +0xd0 (name, object, 0, 0, -1, 0), handle
// not kept; returns true. Otherwise with the shared delay Game+0xb44: "q1150050/53/54_help", "q1150064..67_Hurt"
// (range 700) -> +0xb50, returns true. Else false. GameTime is read once.
uint32_t Q11Voice_PlayDamaged(uint32_t self, const uint32_t* damage)
{
    const float now = GameTime();
    if (damage && (damage[0] & 0x20000) && SharedDelayPassed(0xb94, now))
    {
        SharedDelayRestart(0xb94, now);
        const int32_t roll = Rand() * 200;
        Say2D((roll & static_cast<int32_t>(0xffff8000u)) < 0x320000 ? 0x0059dddcu : 0x0059ddccu, self);
        return 1;
    }
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x0059ddbc, 0x0059ddac, 0x0059dd9c, 0x0059dd8c, 0x0059dd7c, 0x0059dd6c, 0x0059dd5c};
    Field<uint32_t>(P(self), 0xb50) = Say(Pick(700, a), self);
    return 1;
}
static uint32_t Q11Voice_PlayDamaged_Abi(uint32_t ecx, uint32_t a0)
{
    return Q11Voice_PlayDamaged(ecx, reinterpret_cast<const uint32_t*>(a0));
}
ABI_ENTRY_004872e0(Q11Voice_PlayDamaged_Entry, Q11Voice_PlayDamaged_Abi)
HOOK(0x004872e0, "Q11Voice_PlayDamaged", Q11Voice_PlayDamaged_Entry, nullptr);

// 0x004874c0 (vtable data 0x005a6f38). Range 600 when the "JON" character (0x00475d30(Game+0x748)) exists and
// 0x00454780 (esi = it) is false, else 499: "Q1150045/46/47/148/149_Fight" with audio +0xc4, r >= 500 plays
// "Q1154007_FightSpec" with audio +0xd0; the handle goes to +0xb50.
uint32_t Q11Voice_PlayFight(uint32_t self)
{
    const uint32_t jon = orig::fn_00475d30(Field<uint32_t>(g_game, 0x748));
    int32_t range = 499;
    if (jon != 0 && !static_cast<uint8_t>(orig::fn_00454780(jon)))
        range = 600;
    const uint32_t r = Roll(range);
    static const uint32_t a[] = {0x0059dd4c, 0x0059dd3c, 0x0059dd2c, 0x0059dd1c, 0x0059dd0c};
    const uint32_t h = r < 500 ? Say(a[r / 100], self) : Say2D(0x0059dcf8u, self);
    return Field<uint32_t>(P(self), 0xb50) = h;
}
static uint32_t Q11Voice_PlayFight_Abi(uint32_t ecx)
{
    return Q11Voice_PlayFight(ecx);
}
ABI_ENTRY_004874c0(Q11Voice_PlayFight_Entry, Q11Voice_PlayFight_Abi)
HOOK(0x004874c0, "Q11Voice_PlayFight", Q11Voice_PlayFight_Entry, nullptr);

// 0x004875d0 (vtable data 0x005a6f34). Shared delay Game+0xb44 (returns false when it has not passed), restarts it,
// then "Q1150038_Fight" / "Q1150040_Fight" (range 200) -> +0xb50; returns true.
uint32_t Q11Voice_PlayFightShort(uint32_t self)
{
    const float now = GameTime();
    if (!SharedDelayPassed(0xb44, now))
        return 0;
    SharedDelayRestart(0xb44, now);
    static const uint32_t a[] = {0x0059dce8, 0x0059dcd8};
    Field<uint32_t>(P(self), 0xb50) = Say(Pick(200, a), self);
    return 1;
}
static uint32_t Q11Voice_PlayFightShort_Abi(uint32_t ecx)
{
    return Q11Voice_PlayFightShort(ecx);
}
ABI_ENTRY_004875d0(Q11Voice_PlayFightShort_Entry, Q11Voice_PlayFightShort_Abi)
HOOK(0x004875d0, "Q11Voice_PlayFightShort", Q11Voice_PlayFightShort_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00491760 (cdecl; stack: names, count, volume group; caller pops; executed in recorded runs). Front-end sound:
// counter 0x0063a160 += 1; name = names[rand() % count] (unsigned div); when non-null: handle = audio +0xd0 (name, 0,
// 0, 0, -1, 0); g = group 1: audio +0x98(), 2: +0xa0(), 3: +0x9c() (float getters, st0), else 0.0; then audio +0xf8
// (handle, -audio +0x98() + g) and returns its eax. Returns 0 without a name.
uint32_t UiSound_PlayRandom(const uint32_t* names, uint32_t count, uint32_t group)
{
    const uint32_t r = static_cast<uint32_t>(Rand());
    ++GameGlobal<uint32_t>(0x0063a160);
    const uint32_t name = names[r % count];
    if (name == 0)
        return 0;
    const uint32_t handle = VCall<uint32_t>(Audio(), 0xd0 / 4, name, 0u, 0u, 0u, 0xffffffffu, 0u);
    float g;
    if (group == 1)
        g = VCall<float>(Audio(), 0x98 / 4);
    else if (group == 2)
        g = VCall<float>(Audio(), 0xa0 / 4);
    else if (group == 3)
        g = VCall<float>(Audio(), 0x9c / 4);
    else
        g = 0.0f;
    const float base = VCall<float>(Audio(), 0x98 / 4);
    return VCall<uint32_t>(Audio(), 0xf8 / 4, handle, -base + g);
}
static uint32_t UiSound_PlayRandom_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return UiSound_PlayRandom(reinterpret_cast<const uint32_t*>(a0), a1, a2);
}
ABI_ENTRY_00491760(UiSound_PlayRandom_Entry, UiSound_PlayRandom_Abi)
HOOK(0x00491760, "UiSound_PlayRandom", UiSound_PlayRandom_Entry, nullptr);

// 0x00491830 (eax = volume group; not executed in recorded runs): 0x00491760(0x005cb164 {"ui_select"}, 1, group).
static uint32_t UiSound_PlaySelect_Abi(uint32_t eax)
{
    return UiSound_PlayRandom(reinterpret_cast<const uint32_t*>(0x005cb164), 1, eax);
}
ABI_ENTRY_00491830(UiSound_PlaySelect_Entry, UiSound_PlaySelect_Abi)
HOOK(0x00491830, "UiSound_PlaySelect", UiSound_PlaySelect_Entry, nullptr);

// 0x00491850 (eax = volume group; executed in recorded runs): 0x00491760(0x005cb158 {"ui_select_1", ...}, 3, group).
static uint32_t UiSound_PlaySelectSet_Abi(uint32_t eax)
{
    return UiSound_PlayRandom(reinterpret_cast<const uint32_t*>(0x005cb158), 3, eax);
}
ABI_ENTRY_00491850(UiSound_PlaySelectSet_Entry, UiSound_PlaySelectSet_Abi)
HOOK(0x00491850, "UiSound_PlaySelectSet", UiSound_PlaySelectSet_Entry, nullptr);

// 0x00491870 (eax = volume group; executed in recorded runs): 0x00491760(0x005cb168 {"ui_cancel"}, 1, group).
static uint32_t UiSound_PlayCancel_Abi(uint32_t eax)
{
    return UiSound_PlayRandom(reinterpret_cast<const uint32_t*>(0x005cb168), 1, eax);
}
ABI_ENTRY_00491870(UiSound_PlayCancel_Entry, UiSound_PlayCancel_Abi)
HOOK(0x00491870, "UiSound_PlayCancel", UiSound_PlayCancel_Entry, nullptr);

// 0x00491890 (eax = volume group; executed in recorded runs): 0x00491760(0x005cb154 {"ui_move"}, 1, group).
static uint32_t UiSound_PlayMove_Abi(uint32_t eax)
{
    return UiSound_PlayRandom(reinterpret_cast<const uint32_t*>(0x005cb154), 1, eax);
}
ABI_ENTRY_00491890(UiSound_PlayMove_Entry, UiSound_PlayMove_Abi)
HOOK(0x00491890, "UiSound_PlayMove", UiSound_PlayMove_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00493950 (ecx = object; stack: a0, a1, props, a3; callee pops 16; virtual, vtable data 0x005a86a0 (vtable 0x005a8550 +0x150 if the vtable starts there; its slot 1 gives "CLASS(PeasantObject)": guess); not executed in recorded runs). Ambient sound group
// properties (strings "world", "loop%d", "oneshot%d", "min_distance", "max_distance", "min_delay"): base load
// 0x004517b0(a0, a1, props, a3); virtual +0x54(&position); bool "world" (0x00453400, eax = 0, esi = a3, output = the
// a1 stack slot); group = audio +0x13c(world != 0, &position, 0) (kept in the a3 slot). Then up to 10 "loopN"
// strings (sprintf into a local, 0x004533a0 with default 0x005bb9e0 "") are added with audio +0x144(group, name),
// stopping at a missing or empty one (counter in the props slot); without "world": floats "min_distance" (default
// 100) and "max_distance" (300) -> audio +0x150(group, min, max). Then up to 10 "oneshotN" strings -> audio
// +0x148(group, name); when any: "min_delay" (default 3) and again "min_delay" (default 5; original bug: the maximum
// reads the minimum's key 0x005b93d8) -> audio +0x14c(group, min, max). The original's 0x3c-byte frame is kept as one
// array (position +0x10, sprintf buffer +0x1c, name +0x2c; the name buffer is enlarged here, the original copies
// into its frame without a bound).
uint32_t Object_LoadAmbientSoundProperties(uint8_t* self, uint32_t a0, uint32_t a1, uint32_t props, uint32_t a3)
{
    uint8_t frame[0x2c + 0x100];
    uint8_t* const position = frame + 0x10;
    char* const key = reinterpret_cast<char*>(frame + 0x1c);
    uint8_t* const name = frame + 0x2c;
    uint32_t slotA1 = a1;      // reused: "world", then the oneshot count and the minimum values
    uint32_t slotProps = props;  // reused: loop counter, then maximum values
    orig::fn_004517b0(u(self), a0, a1, props, a3);
    VCall<uint32_t>(self, 0x54 / 4, u(position));
    orig::fn_00453400(0, a3, u(&slotA1), u(self), 0x0058fc24u, props, 0);
    const uint32_t group = VCall<uint32_t>(Audio(), 0x13c / 4, slotA1 != 0 ? 1u : 0u, u(position), 0u);
    uint32_t r = group;
    for (uint32_t i = 1;; ++i)
    {
        slotProps = i;
        orig::fn_0053814f(u(key), 0x0058fc1cu, i);
        if (orig::fn_004533a0(0x005bb9e0u, u(name), u(self), u(key), props, a3, 0) == 0)
            break;
        if (Stricmp(0x005bb9e0u, u(name)) == 0)
            break;
        r = VCall<uint32_t>(Audio(), 0x144 / 4, group, u(name));
        if (!(slotProps < 10))
            break;
    }
    if (slotA1 == 0)
    {
        orig::fn_004534a0(a3, u(&slotA1), u(self), 0x0058fc0cu, props, 0x42c80000u, 0);
        orig::fn_004534a0(a3, u(&slotProps), u(self), 0x005bbf98u, props, 0x43960000u, 0);
        r = VCall<uint32_t>(Audio(), 0x150 / 4, group, slotA1, slotProps);
    }
    slotA1 = 0;
    for (;;)
    {
        slotProps = slotA1 + 1;
        orig::fn_0053814f(u(key), 0x0058fc00u, slotProps);
        if (orig::fn_004533a0(0x005bb9e0u, u(name), u(self), u(key), props, a3, 0) == 0)
            break;
        if (Stricmp(0x005bb9e0u, u(name)) == 0)
            break;
        r = VCall<uint32_t>(Audio(), 0x148 / 4, group, u(name));
        slotA1 = slotProps;
        if (!(slotProps < 10))
            break;
    }
    if (slotA1 != 0)
    {
        orig::fn_004534a0(a3, u(&slotA1), u(self), 0x005b93d8u, props, 0x40400000u, 0);
        orig::fn_004534a0(a3, u(&slotProps), u(self), 0x005b93d8u, props, 0x40a00000u, 0);
        r = VCall<uint32_t>(Audio(), 0x14c / 4, group, slotA1, slotProps);
    }
    return r;
}
static uint32_t Object_LoadAmbientSoundProperties_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Object_LoadAmbientSoundProperties(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00493950(Object_LoadAmbientSoundProperties_Entry, Object_LoadAmbientSoundProperties_Abi)
HOOK(0x00493950, "Object_LoadAmbientSoundProperties", Object_LoadAmbientSoundProperties_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// The sound system (Game+0x2c). Offsets "+0x.." on `self` are members, "slot +0x.." virtual methods.

uint32_t fn_004f22e0(uint8_t* self);
uint32_t fn_004f2590(uint8_t* self);
uint32_t fn_004f26a0(uint8_t* self, uint32_t* voice, uint32_t immediate);
uint32_t fn_004f29d0(uint32_t* voice, uint8_t* self);
uint32_t fn_004f28d0(uint8_t* self, uint32_t* voice);
uint32_t fn_004f2950(uint8_t* self, uint32_t* voice);
uint32_t fn_004f2870(uint32_t* voice, uint8_t* self);
uint32_t fn_004f2f50(uint8_t* self);
uint32_t fn_004f3000(uint8_t* self);

// 0x004f20d0 (edi = object; the ecx "input" is `push ecx` reserving a local; caller 0x004f9ca0; executed in recorded
// runs). Sound system base constructor: vtable 0x0058aa40, +0x14 = 1.0, flags/members cleared in the original's
// order, +0x34 = 1; +0x30 = a 0x10-byte pool header {0x98, 0x14, 0, 0} (PMS_heapAlloc 0x004e07f0 on heap 0x0063a210,
// tag 0x0058b5e8); +0x74 = AmbientSounds_Init (0x00500230, esi = 0x9c bytes; this); +0x28 = a PMS_List (vtable
// 0x0058b098, data 0x004e06f0(edx 0x14; heap, "PMS_List::m_listData", 0x1c, 2, 5), +4 = 1). The shared objects
// 0x0063a0e8 (0x268 bytes, 0x004e4440 (edx = memory); refcount at +4) and 0x0063a0b4 (4-byte refcount) are created
// on first use and referenced at +8 / +4. Returns the object. The SEH frame only covers exceptions in
// AmbientSounds_Init and is not reproduced.
uint32_t fn_004f20d0(uint8_t* self)
{
    Field<uint32_t>(self, 0) = 0x0058aa40;
    Field<uint32_t>(self, 0x10) = 0;
    Field<uint32_t>(self, 0x14) = 0x3f800000;
    Field<uint32_t>(self, 0x18) = 0;
    Field<uint32_t>(self, 0x20) = 0;
    Field<uint32_t>(self, 0x1c) = 0;
    Field<uint32_t>(self, 0x24) = 0;
    Field<uint32_t>(self, 0xa8) = 0;
    Field<uint32_t>(self, 0x28) = 0;
    for (uint32_t o = 0x38; o <= 0x48; o += 4)
        Field<uint32_t>(self, o) = 0;
    Field<uint32_t>(self, 0x4c) = 0;
    Field<uint32_t>(self, 0x50) = 0;
    Field<uint32_t>(self, 0x54) = 0;
    Field<uint32_t>(self, 0x58) = 0;
    Field<uint32_t>(self, 0x64) = 0;
    Field<uint8_t>(self, 0xc) = 0;
    Field<uint8_t>(self, 0x7c) = 0;
    Field<uint32_t>(self, 0x34) = 1;
    Field<uint32_t>(self, 0x90) = 0;
    Field<uint32_t>(self, 0x94) = 0;
    Field<uint32_t>(self, 0x98) = 0;
    uint32_t p = HeapAlloc(0x10, 0x10, 0x0058b5e8);
    if (p)
    {
        Field<uint32_t>(P(p), 0) = 0x98;
        Field<uint32_t>(P(p), 4) = 0x14;
        Field<uint32_t>(P(p), 8) = 0;
        Field<uint32_t>(P(p), 0xc) = 0;
    }
    Field<uint32_t>(self, 0x30) = p;
    p = HeapAlloc(0x9c, 0x10, 0x0058b5e8);
    Field<uint32_t>(self, 0x74) = p ? orig::fn_00500230(p, u(self)) : 0;
    p = HeapAlloc(0x18, 0x10, 0x0058b5e8);
    if (p)
    {
        Field<uint32_t>(P(p), 0) = 0x0058b098;
        Field<uint32_t>(P(p), 8) = orig::fn_004e06f0(0x14, kHeap, 0x0058b0e0, 0x1c, 2, 5);
        Field<uint8_t>(P(p), 4) = 1;
        Field<uint32_t>(P(p), 0xc) = 0;
        Field<uint32_t>(P(p), 0x10) = 0;
        Field<uint32_t>(P(p), 0x14) = 0;
    }
    Field<uint32_t>(self, 0x28) = p;
    uint32_t shared = GameGlobal<uint32_t>(0x0063a0e8);
    if (shared == 0)
    {
        p = HeapAlloc(0x268, 0x10, 0x0058b5e8);
        shared = p ? orig::fn_004e4440(p) : 0;
        GameGlobal<uint32_t>(0x0063a0e8) = shared;
    }
    ++Field<int32_t>(P(shared), 4);
    Field<uint32_t>(self, 8) = shared;
    uint32_t count = GameGlobal<uint32_t>(0x0063a0b4);
    if (count == 0)
    {
        count = HeapAlloc(4, 0x10, 0x0058b5e8);
        GameGlobal<uint32_t>(0x0063a0b4) = count;
    }
    ++Field<int32_t>(P(count), 0);
    Field<uint32_t>(self, 4) = count;
    Field<uint32_t>(self, 0x80) = 0;
    Field<uint32_t>(self, 0x84) = 0;
    Field<uint32_t>(self, 0x78) = 0;
    Field<uint32_t>(self, 0xa0) = 0;
    Field<uint32_t>(self, 0xa4) = 0;
    return u(self);
}
static uint32_t fn_004f20d0_Abi(uint32_t /*ecx*/, uint32_t edi)
{
    return fn_004f20d0(reinterpret_cast<uint8_t*>(edi));
}
ABI_ENTRY_004f20d0(fn_004f20d0_Entry, fn_004f20d0_Abi)
HOOK(0x004f20d0, "fn_004f20d0", fn_004f20d0_Entry, nullptr);

// 0x004f22c0 (ecx = object; stack: flags; callee pops 4; base vtable 0x0058aa40 +0x68 (scalar deleting
// destructor; the PC class overrides it with 0x004f9ce0); not executed in recorded runs): destructor 0x004f22e0, then with flags bit 0
// PMS_heapFree(heap 0x0063a210, object). Returns the object.
uint32_t fn_004f22c0(uint8_t* self, uint32_t flags)
{
    fn_004f22e0(self);
    if (flags & 1)
        HeapFree(u(self));
    return u(self);
}
static uint32_t fn_004f22c0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f22c0(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f22c0(fn_004f22c0_Entry, fn_004f22c0_Abi)
HOOK(0x004f22c0, "fn_004f22c0", fn_004f22c0_Entry, nullptr);

// 0x004f22e0 (ecx = object; callers 0x004f22c0, 0x004f9d10; not executed in recorded runs). Sound system base
// destructor: vtable 0x0058aa40, shutdown 0x004f2590; frees the pool header +0x30 and its block chain (+0xc, linked
// through +0), deletes the voice list +0x28 (virtual +0x10(1)), destroys and frees the ambient sounds +0x74
// (AmbientSounds_Destroy 0x00500280), and releases the shared objects 0x0063a0e8 (count at +4) and 0x0063a0b4,
// freeing them when the count reaches 0. Returns the last value in eax (the 0x0063a0b4 pointer or the free's result).
uint32_t fn_004f22e0(uint8_t* self)
{
    Field<uint32_t>(self, 0) = 0x0058aa40;
    fn_004f2590(self);
    const uint32_t pool = Field<uint32_t>(self, 0x30);
    if (pool)
    {
        for (uint32_t b = Field<uint32_t>(P(pool), 0xc); b != 0;)
        {
            const uint32_t next = Field<uint32_t>(P(b), 0);
            HeapFree(b);
            b = next;
        }
        Field<uint32_t>(P(pool), 0xc) = 0;
        Field<uint32_t>(P(pool), 8) = 0;
        HeapFree(pool);
    }
    const uint32_t list = Field<uint32_t>(self, 0x28);
    Field<uint32_t>(self, 0x30) = 0;
    if (list)
        VCall<uint32_t>(P(list), 0x10 / 4, 1u);
    const uint32_t ambient = Field<uint32_t>(self, 0x74);
    Field<uint32_t>(self, 0x28) = 0;
    if (ambient)
    {
        orig::fn_00500280(ambient);
        HeapFree(ambient);
    }
    const uint32_t shared = GameGlobal<uint32_t>(0x0063a0e8);
    Field<uint32_t>(self, 0x74) = 0;
    if (--Field<int32_t>(P(shared), 4) == 0)
    {
        HeapFree(shared);
        GameGlobal<uint32_t>(0x0063a0e8) = 0;
    }
    uint32_t r = GameGlobal<uint32_t>(0x0063a0b4);
    Field<uint32_t>(self, 8) = 0;
    if (--Field<int32_t>(P(r), 0) == 0)
    {
        r = HeapFree(r);
        GameGlobal<uint32_t>(0x0063a0b4) = 0;
    }
    Field<uint32_t>(self, 4) = 0;
    return r;
}
static uint32_t fn_004f22e0_Abi(uint32_t ecx)
{
    return fn_004f22e0(reinterpret_cast<uint8_t*>(ecx));
}
ABI_ENTRY_004f22e0(fn_004f22e0_Entry, fn_004f22e0_Abi)
HOOK(0x004f22e0, "fn_004f22e0", fn_004f22e0_Entry, nullptr);

// 0x004f23a0 (ecx = object; stack: 11 arguments; callee pops 0x2c; base vtable 0x0058aa40 +0x74 (the PC override is Audio_InitDirectSound); caller 0x004fa8f0
// Audio_InitDirectSound; executed in recorded runs). Initialise once (byte +0xc): +0x4c = a2, +0x60 = a3, +0x50 = a4,
// +0x54 = a5, +0x58 = a6, +0x64 = a8 (driver callbacks), +0xc = 1, +0x14 = 1.0, +0x18/+0x20/+0x1c/+0x24 = 0, +0x68 =
// a9; virtual +0x50() -> byte +0x70, virtual +0x54() -> byte +0x71; with a0
// definitions: +0x78 = a0, +0x80 = zeroed PMS_heapAlloc(align 2, a0 * 4, "audio definition list"); then virtual
// +0x124(0, 0). Always returns true (al; upper bytes not reproduced). a1, a7 and a10 are not read.
uint32_t fn_004f23a0(uint8_t* self, const uint32_t* a)
{
    if (Field<uint8_t>(self, 0xc) == 0)
    {
        Field<uint32_t>(self, 0x4c) = a[2];
        Field<uint32_t>(self, 0x60) = a[3];
        Field<uint32_t>(self, 0x50) = a[4];
        Field<uint32_t>(self, 0x54) = a[5];
        Field<uint32_t>(self, 0x58) = a[6];
        Field<uint32_t>(self, 0x64) = a[8];
        Field<uint8_t>(self, 0xc) = 1;
        Field<uint32_t>(self, 0x14) = 0x3f800000;
        Field<uint32_t>(self, 0x18) = 0;
        Field<uint32_t>(self, 0x20) = 0;
        Field<uint32_t>(self, 0x1c) = 0;
        Field<uint32_t>(self, 0x24) = 0;
        Field<uint32_t>(self, 0x68) = a[9];
        Field<uint8_t>(self, 0x70) = static_cast<uint8_t>(VCall<uint32_t>(self, 0x50 / 4));
        Field<uint8_t>(self, 0x71) = static_cast<uint8_t>(VCall<uint32_t>(self, 0x54 / 4));
        if (a[0] != 0)
        {
            Field<uint32_t>(self, 0x78) = a[0];
            const uint32_t table = orig::fn_004e07f0(kHeap, 2, a[0] * 4, 2, 0x0058ad40);
            Field<uint32_t>(self, 0x80) = table;
            std::memset(P(table), 0, a[0] * 4);
        }
        VCall<uint32_t>(self, 0x124 / 4, 0u, 0u);
    }
    return 1;
}
static uint32_t fn_004f23a0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10)
{
    const uint32_t a[11] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10};
    return fn_004f23a0(reinterpret_cast<uint8_t*>(ecx), a);
}
ABI_ENTRY_004f23a0(fn_004f23a0_Entry, fn_004f23a0_Abi)
HOOK(0x004f23a0, "fn_004f23a0", fn_004f23a0_Entry, nullptr);

// 0x004f2470 (ebx = &index, esi = sound system, edi = name; stack: quiet (byte); callee pops 4; executed in recorded
// runs). Definition lookup (string "Audio : %s not found"): returns 0 without a table (+0x80). With a name: *index =
// the name map +0xa0 entry's first dword (0x004f1160, eax = map; name) or -1 when there is no map or no entry. Then
// returns table[*index] when *index < +0x78 (unsigned); otherwise logs "Audio : %s not found" (Debug_Printf, Game
// +8 of the sound system, level 5) unless quiet, and returns 0 (the log's eax when it logged).
uint32_t AudioPC_FindDefinition(uint32_t* index, uint8_t* self, uint32_t name, uint32_t quiet)
{
    if (Field<uint32_t>(self, 0x80) == 0)
        return 0;
    if (name != 0)
    {
        const uint32_t map = Field<uint32_t>(self, 0xa0);
        if (map == 0)
            *index = 0xffffffffu;
        else
        {
            const uint32_t e = orig::fn_004f1160(map, name);
            *index = e != 0 ? Field<uint32_t>(P(e), 0) : 0xffffffffu;
        }
    }
    if (*index < Field<uint32_t>(self, 0x78))
        return Field<uint32_t*>(self, 0x80)[*index];
    if (static_cast<uint8_t>(quiet) != 0)
        return 0;
    return orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
}
static uint32_t AudioPC_FindDefinition_Abi(uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return AudioPC_FindDefinition(reinterpret_cast<uint32_t*>(ebx), reinterpret_cast<uint8_t*>(esi), edi, a0);
}
ABI_ENTRY_004f2470(AudioPC_FindDefinition_Entry, AudioPC_FindDefinition_Abi)
HOOK(0x004f2470, "AudioPC_FindDefinition", AudioPC_FindDefinition_Entry, nullptr);

// 0x004f24e0 (ecx = sound system; stack: all (byte); callee pops 4; slot +0xa8; executed in recorded runs). Stops
// every voice in +0x28 (0x004f26a0 (voice, 1) and flags |= 0x20000000), except the voice +0x90 unless `all`; then
// virtual +0x130(0, 0, 0). Returns the iterator chunk (eax after the iteration ends). The SEH frame only covers the
// iterator and is not reproduced.
uint32_t fn_004f24e0(uint8_t* self, uint32_t all)
{
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (v != Field<uint32_t>(self, 0x90) || static_cast<uint8_t>(all) != 0)
        {
            fn_004f26a0(self, reinterpret_cast<uint32_t*>(v), 1);
            Field<uint32_t>(P(v), 0) |= 0x20000000;
        }
    }
    VCall<uint32_t>(self, 0x130 / 4, 0u, 0u, 0u);
    return IterEnd(&it);
}
static uint32_t fn_004f24e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f24e0(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f24e0(fn_004f24e0_Entry, fn_004f24e0_Abi)
HOOK(0x004f24e0, "fn_004f24e0", fn_004f24e0_Entry, nullptr);

// 0x004f2590 (ecx = sound system; callers 0x004f22e0, 0x004f9d10, 0x004fafc0; executed in recorded runs). Shutdown:
// virtual +0xa8(1) (stop all); frees the name map +0xa0 (0x004f11c0 (ebx = map) and its +0x1c block first unless
// byte +0xc of the map is set) and +0xa4; virtual +0x7c(i) for every definition i < +0x78 (re-read each pass); frees
// +0x80 and +0x84; byte +0x7c = 0; voice list +0x28 virtual +0x30(); AmbientSounds_Clear (0x005003c0, eax = +0x74);
// frees the pool +0x30's block chain; +0x90 = 0. Returns 0.
uint32_t fn_004f2590(uint8_t* self)
{
    VCall<uint32_t>(self, 0xa8 / 4, 1u);
    const uint32_t map = Field<uint32_t>(self, 0xa0);
    if (map)
    {
        if (Field<uint8_t>(P(map), 0xc) == 0)
        {
            orig::fn_004f11c0(map);
            HeapFree(Field<uint32_t>(P(map), 0x1c));
        }
        HeapFree(map);
        if (Field<uint32_t>(self, 0xa4) != 0)
        {
            HeapFree(Field<uint32_t>(self, 0xa4));
            Field<uint32_t>(self, 0xa4) = 0;
        }
        Field<uint32_t>(self, 0xa0) = 0;
        Field<uint32_t>(self, 0xa4) = 0;
    }
    if (Field<uint32_t>(self, 0x78) != 0)
    {
        uint32_t i = 0;
        do
        {
            VCall<uint32_t>(self, 0x7c / 4, i);
            ++i;
        } while (i < Field<uint32_t>(self, 0x78));
    }
    Field<uint32_t>(self, 0x78) = 0;
    if (Field<uint32_t>(self, 0x80) != 0)
    {
        HeapFree(Field<uint32_t>(self, 0x80));
        Field<uint32_t>(self, 0x80) = 0;
    }
    if (Field<uint32_t>(self, 0x84) != 0)
    {
        HeapFree(Field<uint32_t>(self, 0x84));
        Field<uint32_t>(self, 0x84) = 0;
    }
    Field<uint8_t>(self, 0x7c) = 0;
    VCall<uint32_t>(Field<void*>(self, 0x28), 0x30 / 4);
    orig::fn_005003c0(Field<uint32_t>(self, 0x74));
    const uint32_t pool = Field<uint32_t>(self, 0x30);
    for (uint32_t b = Field<uint32_t>(P(pool), 0xc); b != 0;)
    {
        const uint32_t next = Field<uint32_t>(P(b), 0);
        HeapFree(b);
        b = next;
    }
    Field<uint32_t>(P(pool), 0xc) = 0;
    Field<uint32_t>(P(pool), 8) = 0;
    Field<uint32_t>(self, 0x90) = 0;
    return 0;
}
static uint32_t fn_004f2590_Abi(uint32_t ecx)
{
    return fn_004f2590(reinterpret_cast<uint8_t*>(ecx));
}
ABI_ENTRY_004f2590(fn_004f2590_Entry, fn_004f2590_Abi)
HOOK(0x004f2590, "fn_004f2590", fn_004f2590_Entry, nullptr);

// 0x004f26a0 (ecx = sound system, esi = voice; stack: immediate (byte); callee pops 4; executed in recorded runs).
// Stop a voice: flags = (flags & 0xffff87ff) | 0x40; a pending-start flag 0x4000000 is cleared and forces immediate
// (the low byte of the argument slot becomes 1, the whole dword is passed on). Unless the definition flags have
// 0x2000000: virtual +0x30(voice, immediate). With 0x2000000 (guess: music): marks the current voice +0x90's flags
// +0x64 |= 0x800 and, when immediate, clears +0x94 and calls virtual +0x30(voice, 0). Then definition flags & 0x1000040
// with voice+0x64 bit 2 clear marks the voice finished (|= 0x20000000). Returns the definition pointer (eax), or 0
// for a null voice.
uint32_t fn_004f26a0(uint8_t* self, uint32_t* voice, uint32_t immediate)
{
    if (voice == nullptr)
        return 0;
    uint32_t f = (voice[0] & 0xffff87ffu) | 0x40u;
    voice[0] = f;
    if (f & 0x4000000)
    {
        immediate = (immediate & 0xffffff00u) | 1u;
        voice[0] = f & 0xfbffffffu;
    }
    if ((Field<uint32_t>(P(voice[3]), 0x20) & 0x2000000) == 0)
    {
        VCall<uint32_t>(self, 0x30 / 4, u(voice), immediate);
    }
    else
    {
        const uint32_t current = Field<uint32_t>(self, 0x90);
        if (current != 0)
        {
            Field<uint32_t>(P(current), 0x64) |= 0x800;
            if (static_cast<uint8_t>(immediate) != 0)
            {
                Field<uint32_t>(self, 0x94) = 0;
                VCall<uint32_t>(self, 0x30 / 4, u(voice), 0u);
            }
        }
    }
    const uint32_t def = voice[3];
    if ((Field<uint32_t>(P(def), 0x20) & 0x1000040) != 0 && (Field<uint8_t>(voice, 0x64) & 4) == 0)
        voice[0] |= 0x20000000;
    return def;
}
static uint32_t fn_004f26a0_Abi(uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return fn_004f26a0(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint32_t*>(esi), a0);
}
ABI_ENTRY_004f26a0(fn_004f26a0_Entry, fn_004f26a0_Abi)
HOOK(0x004f26a0, "fn_004f26a0", fn_004f26a0_Entry, nullptr);

// 0x004f2730 (ecx = sound system; stack: voice, dt (float); callee pops 8; slot +0x1c; not executed in recorded
// runs). Fade envelope (guess). With voice+0x8c > 0 (K 0x0056c05c): +0x8c -= dt; while still > 0 the level is
// +0x8c / def+0x48 * def+0x4c; else def+0x20 &= ~0x200000, virtual +0x30(voice, 0) and the level is 0. Otherwise t =
// dt + voice+0x94 (stored): level 0 when t <= def+0x38; 1 - (a - t) / def+0x3c while t < a = def+0x3c + def+0x38; 1.0
// (K 0x005bcb2c) while def+0x40 + def+0x3c + def+0x38 > t; 1 - (1 - (c - t) / def+0x44) * def+0x4c while t < c = that
// + def+0x44; else def+0x4c. Then voice+0x90 = (1 - level) * K(0x005bcb64) and virtual +0x40(voice, virtual
// +0x10(voice)) (+0x10 returns st0).
uint32_t fn_004f2730(uint8_t* self, uint8_t* voice, float dt)
{
    const float zero = K(0x0056c05c);
    const float one = K(0x005bcb2c);
    float level;
    if (Field<float>(voice, 0x8c) > zero)
    {
        const float t = Field<float>(voice, 0x8c) - dt;
        Field<float>(voice, 0x8c) = t;
        const uint8_t* def = Field<uint8_t*>(voice, 0xc);
        if (t > zero)
        {
            level = t / Field<float>(def, 0x48) * Field<float>(def, 0x4c);
        }
        else
        {
            Field<uint32_t>(def, 0x20) &= 0xffdfffffu;
            VCall<uint32_t>(self, 0x30 / 4, u(voice), 0u);
            level = zero;
        }
    }
    else
    {
        const float t = dt + Field<float>(voice, 0x94);
        Field<float>(voice, 0x94) = t;
        const uint8_t* def = Field<uint8_t*>(voice, 0xc);
        if (!(t > Field<float>(def, 0x38)))
        {
            level = zero;
        }
        else
        {
            const float a = Field<float>(def, 0x3c) + Field<float>(def, 0x38);
            if (t < a)
            {
                level = one - (a - t) / Field<float>(def, 0x3c);
            }
            else if (Field<float>(def, 0x40) + Field<float>(def, 0x3c) + Field<float>(def, 0x38) > t)
            {
                level = one;
            }
            else
            {
                const float c = Field<float>(def, 0x40) + Field<float>(def, 0x3c) + Field<float>(def, 0x38) +
                                Field<float>(def, 0x44);
                if (t < c)
                    level = one - (one - (c - t) / Field<float>(def, 0x44)) * Field<float>(def, 0x4c);
                else
                    level = Field<float>(def, 0x4c);
            }
        }
    }
    Field<float>(voice, 0x90) = (one - level) * K(0x005bcb64);
    const float v = VCall<float>(self, 0x10 / 4, u(voice));
    return VCall<uint32_t>(self, 0x40 / 4, u(voice), v);
}
static uint32_t fn_004f2730_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    float dt;
    std::memcpy(&dt, &a1, 4);
    return fn_004f2730(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(a0), dt);
}
ABI_ENTRY_004f2730(fn_004f2730_Entry, fn_004f2730_Abi)
HOOK(0x004f2730, "fn_004f2730", fn_004f2730_Entry, nullptr);

// 0x004f2870 (esi = voice, edi = sound system; callers 0x004f29d0, 0x004f32f0, 0x004f3740; executed in recorded
// runs). Pushes a voice's 3D parameters to the driver: with flags 0x10 and 0x4 and a buffer (+8): when flags & 8
// callback +0x4c(buffer, &+0x20, &+0x2c, +4) (cdecl); then when byte +0x71 and definition flags & 4: callback
// +0x60(buffer, &+0x20, &+0x38, &+0x3c, &+0x40, &+0x4c). Returns the last eax (approximately: 0 / the callback's).
uint32_t fn_004f2870(uint32_t* voice, uint8_t* self)
{
    const uint32_t f = voice[0];
    if (!(f & 0x10) || !(f & 4) || voice[2] == 0)
        return 0;
    uint32_t r = voice[2];
    if (f & 8)
    {
        r = reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t)>(
            Field<uint32_t>(self, 0x4c))(voice[2], u(voice + 8), u(voice + 0xb), voice[1]);
    }
    const uint8_t enabled = Field<uint8_t>(self, 0x71);
    r = (r & 0xffffff00u) | enabled;
    if (enabled == 0 || (Field<uint8_t>(P(voice[3]), 0x20) & 4) == 0)
        return r;
    return reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
        Field<uint32_t>(self, 0x60))(voice[2], u(voice + 8), u(voice + 0xe), u(voice + 0xf), u(voice + 0x10),
                                     u(voice + 0x13));
}
static uint32_t fn_004f2870_Abi(uint32_t esi, uint32_t edi)
{
    return fn_004f2870(reinterpret_cast<uint32_t*>(esi), reinterpret_cast<uint8_t*>(edi));
}
ABI_ENTRY_004f2870(fn_004f2870_Entry, fn_004f2870_Abi)
HOOK(0x004f2870, "fn_004f2870", fn_004f2870_Entry, nullptr);

// 0x004f28d0 (eax = sound system, edx = voice; caller 0x004f29d0; not executed in recorded runs). Fade-in step
// (guess): unless +0x9c (frame time) == 0 or voice+0x58 == def+0x70: step = def+0x70 / def+0x74 * +0x9c; voice+0x58
// += step; when the new value is below def+0x70 the step becomes step + (def+0x70 - value) and voice+0x58 = (def+0x70
// - voice+0x58) + voice+0x58 (as the original computes it); then with step != 0: +0x24 += step and 0x004f3000
// (refresh volumes). The `!= 0` tests are taken for unordered values too (test ah, 0x44; jnp).
uint32_t fn_004f28d0(uint8_t* self, uint32_t* voice)
{
    const float zero = K(0x0056c05c);
    if (!(Field<float>(self, 0x9c) != zero))
        return 0;
    const uint8_t* def = reinterpret_cast<const uint8_t*>(voice[3]);
    if (!(Field<float>(voice, 0x58) != Field<float>(def, 0x70)))
        return 0;
    float step = Field<float>(def, 0x70) / Field<float>(def, 0x74) * Field<float>(self, 0x9c);
    const float value = step + Field<float>(voice, 0x58);
    Field<float>(voice, 0x58) = value;
    if (value < Field<float>(def, 0x70))
    {
        step = step + (Field<float>(def, 0x70) - value);
        Field<float>(voice, 0x58) = (Field<float>(def, 0x70) - Field<float>(voice, 0x58)) + Field<float>(voice, 0x58);
    }
    if (!(step != zero))
        return 0;
    Field<float>(self, 0x24) = step + Field<float>(self, 0x24);
    return fn_004f3000(self);
}
static uint32_t fn_004f28d0_Abi(uint32_t eax, uint32_t edx)
{
    return fn_004f28d0(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<uint32_t*>(edx));
}
ABI_ENTRY_004f28d0(fn_004f28d0_Entry, fn_004f28d0_Abi)
HOOK(0x004f28d0, "fn_004f28d0", fn_004f28d0_Entry, nullptr);

// 0x004f2950 (eax = sound system, esi = voice; caller 0x004f2a80; not executed in recorded runs). Fade-out step
// (guess): unless +0x9c == 0 (returns false): step = def+0x70 / def+0x78 * +0x9c; voice+0x58 -= step; when the result
// is still above 0 (as the original tests it) step += result and voice+0x58 = 0; with step != 0: +0x24 = -step +
// +0x24 and 0x004f3000. Returns true (al) when voice+0x58 == 0.
uint32_t fn_004f2950(uint8_t* self, uint32_t* voice)
{
    const float zero = K(0x0056c05c);
    if (!(Field<float>(self, 0x9c) != zero))
        return 0;
    const uint8_t* def = reinterpret_cast<const uint8_t*>(voice[3]);
    float step = Field<float>(def, 0x70) / Field<float>(def, 0x78) * Field<float>(self, 0x9c);
    const float value = Field<float>(voice, 0x58) - step;
    Field<float>(voice, 0x58) = value;
    if (value > zero)
    {
        step = step + value;
        Field<uint32_t>(voice, 0x58) = 0;
    }
    if (step != zero)
    {
        Field<float>(self, 0x24) = -step + Field<float>(self, 0x24);
        fn_004f3000(self);
    }
    return Field<float>(voice, 0x58) == zero ? 1u : 0u;
}
static uint32_t fn_004f2950_Abi(uint32_t eax, uint32_t esi)
{
    return fn_004f2950(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<uint32_t*>(esi));
}
ABI_ENTRY_004f2950(fn_004f2950_Entry, fn_004f2950_Abi)
HOOK(0x004f2950, "fn_004f2950", fn_004f2950_Entry, nullptr);

// 0x004f29d0 (eax = voice, ecx = sound system; caller 0x004f2a80; executed in recorded runs). Per-voice update:
// definition flags & 0x1000040 with voice flags bit 0x1000 clear: fn_00500d20 (ebx = voice, esi = sound system)
// false marks the voice finished (|= 0x20000000) and returns. Then def flags 0x800000: fade-in 0x004f28d0; 0x200000:
// virtual +0x1c(voice, +0x9c); voice flags 0x10: 0x004f2870, virtual +0x18(voice, &+0x20), +0x20(voice, &+0x2c) and,
// with byte +0x71 and def flags & 4, +0x44(voice). Returns the last eax (approximately).
uint32_t fn_004f29d0(uint32_t* voice, uint8_t* self)
{
    uint32_t r = voice[3];
    if ((Field<uint32_t>(P(voice[3]), 0x20) & 0x1000040) != 0 && (voice[0] & 0x1000) == 0)
    {
        r = orig::fn_00500d20(u(voice), u(self));
        if (static_cast<uint8_t>(r) == 0)
        {
            voice[0] |= 0x20000000;
            return voice[0];
        }
    }
    if (Field<uint32_t>(P(voice[3]), 0x20) & 0x800000)
        r = fn_004f28d0(self, voice);
    if (Field<uint32_t>(P(voice[3]), 0x20) & 0x200000)
        r = VCall<uint32_t>(self, 0x1c / 4, u(voice), Field<uint32_t>(self, 0x9c));
    if (voice[0] & 0x10)
    {
        fn_004f2870(voice, self);
        VCall<uint32_t>(self, 0x18 / 4, u(voice), u(voice + 8));
        r = VCall<uint32_t>(self, 0x20 / 4, u(voice), u(voice + 0xb));
        const uint8_t enabled = Field<uint8_t>(self, 0x71);
        r = (r & 0xffffff00u) | enabled;
        if (enabled != 0 && (Field<uint8_t>(P(voice[3]), 0x20) & 4) != 0)
            r = VCall<uint32_t>(self, 0x44 / 4, u(voice));
    }
    return r;
}
static uint32_t fn_004f29d0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004f29d0(reinterpret_cast<uint32_t*>(eax), reinterpret_cast<uint8_t*>(ecx));
}
ABI_ENTRY_004f29d0(fn_004f29d0_Entry, fn_004f29d0_Abi)
HOOK(0x004f29d0, "fn_004f29d0", fn_004f29d0_Entry, nullptr);

// 0x004f2a80 (ecx = sound system; stack: a0 (not read), frame time, update (byte); callee pops 12; base vtable
// 0x0058aa40 +0x130 = PC +0x130; caller 0x004f9ef0; executed in recorded runs). Per-frame voice update. With update:
// +0x9c = frame time and, unless paused (+0xa8), AmbientSounds_Update (0x00500520, edi = +0x74; frame time); else
// +0x9c = 0. For every voice in +0x28:
//   flags & 0x60000000 clear (live): a started voice (flags 4) whose buffer callback +0x50(voice+8) reports false is
//     released (flags &= ~4, callback +0x58(voice+8), flags &= ~8, voice+8 = 0); then 0x004f29d0 runs when flags have
//     0x1000, or 0x4000000 (cleared first), or virtual +0x28(voice) returns true, or def flags & 0x1000040; otherwise
//     the voice is marked finished (|= 0x20000000).
//   otherwise (finished/stopping): def flags 0x800000 runs the fade-out 0x004f2950 and keeps the voice until it
//     returns true. A def with flags 0x10000000 (looping/restart, guess) and voice flags not (0x40001040 without 0x20)
//     is played again: flags 0x200 -> audio +0xd0, 0x80 -> +0xc4 (def+0xbc, voice+8, 0, 0, -1, voice+4), 0x100 ->
//     +0xcc (def+0xbc, &voice+0x20, voice+8, 0, 0, -1, voice+4); a non-zero result is looked up in +0x28 and the
//     voice holding it takes this voice's handle +4 (the iteration is left early, see IterBreak). Finally virtual +0xc(voice)
//     (release).
// Returns the iterator chunk. The tracker size (637) is short: the function ends at 0x004f2d04 (a block after
// 0x004f2cfd belongs to it). The SEH frame only covers the iterators and is not reproduced.
uint32_t fn_004f2a80(uint8_t* self, uint32_t frameTime, uint32_t update)
{
    Iter it = {0, 0};
    if (static_cast<uint8_t>(update) != 0)
    {
        Field<uint32_t>(self, 0x9c) = frameTime;
        if (Field<uint32_t>(self, 0xa8) == 0)
            orig::fn_00500520(Field<uint32_t>(self, 0x74), frameTime);
    }
    else
    {
        Field<uint32_t>(self, 0x9c) = 0;
    }
    typedef uint32_t(__cdecl * Callback1)(uint32_t);
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        uint32_t* voice = reinterpret_cast<uint32_t*>(v);
        if ((voice[0] & 0x60000000) == 0)
        {
            if (voice[0] & 4)
            {
                if (static_cast<uint8_t>(reinterpret_cast<Callback1>(Field<uint32_t>(self, 0x50))(voice[2])) == 0)
                {
                    voice[0] &= 0xfffffffbu;
                    reinterpret_cast<Callback1>(Field<uint32_t>(self, 0x58))(voice[2]);
                    const uint32_t f = voice[0] & 0xfffffff7u;
                    voice[2] = 0;
                    voice[0] = f;
                }
            }
            const uint32_t f = voice[0];
            bool run;
            if (f & 0x1000)
            {
                run = true;
            }
            else if (f & 0x4000000)
            {
                voice[0] = f & 0xfbffffffu;
                run = true;
            }
            else
            {
                run = static_cast<uint8_t>(VCall<uint32_t>(self, 0x28 / 4, v)) != 0 ||
                      (Field<uint32_t>(P(voice[3]), 0x20) & 0x1000040) != 0;
            }
            if (run)
                fn_004f29d0(voice, self);
            else
                voice[0] |= 0x20000000;
            continue;
        }
        if (Field<uint32_t>(P(voice[3]), 0x20) & 0x800000)
        {
            if (static_cast<uint8_t>(fn_004f2950(self, voice)) == 0)
                continue;
        }
        const uint32_t def = voice[3];
        if (Field<uint32_t>(P(def), 0x20) & 0x10000000)
        {
            const uint32_t f = voice[0];
            if ((f & 0x40001040) == 0 || (f & 0x20) != 0)
            {
                uint32_t handle;
                bool played = true;
                if (f & 0x200)
                    handle = VCall<uint32_t>(self, 0xd0 / 4, def + 0xbc, voice[2], 0u, 0u, 0xffffffffu, voice[1]);
                else if (f & 0x80)
                    handle = VCall<uint32_t>(self, 0xc4 / 4, def + 0xbc, voice[2], 0u, 0u, 0xffffffffu, voice[1]);
                else if (f & 0x100)
                    handle = VCall<uint32_t>(self, 0xcc / 4, def + 0xbc, u(voice + 8), voice[2], 0u, 0u, 0xffffffffu,
                                             voice[1]);
                else
                    played = false;
                if (played && handle != 0)
                {
                    Iter inner = {0, 0};
                    uint32_t n = IterFirst(Field<uint32_t>(self, 0x28), &inner);
                    for (; n != 0; n = IterNext(&inner))
                    {
                        if (Field<uint32_t>(P(n), 4) == handle)
                            break;
                    }
                    if (n != 0)
                    {
                        IterBreak(&inner);
                        Field<uint32_t>(P(n), 4) = voice[1];
                    }
                    else
                    {
                        IterEnd(&inner);
                    }
                }
            }
        }
        VCall<uint32_t>(self, 0xc / 4, v);
    }
    return IterEnd(&it);
}
static uint32_t fn_004f2a80_Abi(uint32_t ecx, uint32_t /*a0*/, uint32_t a1, uint32_t a2)
{
    return fn_004f2a80(reinterpret_cast<uint8_t*>(ecx), a1, a2);
}
ABI_ENTRY_004f2a80(fn_004f2a80_Entry, fn_004f2a80_Abi)
HOOK(0x004f2a80, "fn_004f2a80", fn_004f2a80_Entry, nullptr);

// 0x004f2d10 (ecx = sound system; stack: voice; callee pops 4; base vtable 0x0058aa40 +0xc; caller 0x004fb4b0;
// executed in recorded runs). Release a voice: clears +0x90/+0x98 when it is the current voice +0x90; voice+4 = 0;
// releases voice+8 through callback +0x58 (cdecl) and clears it; removes it from the list +0x28 (list virtual
// +0x28(voice), whose eax is returned) and pushes it on the free list of the pool +0x30 (voice+0 = pool+8, pool+8 =
// voice).
uint32_t fn_004f2d10(uint8_t* self, uint32_t* voice)
{
    if (Field<uint32_t*>(self, 0x90) == voice)
    {
        Field<uint32_t>(self, 0x90) = 0;
        Field<uint32_t>(self, 0x98) = 0;
    }
    const uint32_t buffer = voice[2];
    voice[1] = 0;
    if (buffer != 0)
    {
        reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(Field<uint32_t>(self, 0x58))(buffer);
        voice[2] = 0;
    }
    const uint32_t r = VCall<uint32_t>(Field<void*>(self, 0x28), 0x28 / 4, u(voice));
    const uint32_t pool = Field<uint32_t>(self, 0x30);
    voice[0] = Field<uint32_t>(P(pool), 8);
    Field<uint32_t>(P(pool), 8) = u(voice);
    return r;
}
static uint32_t fn_004f2d10_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f2d10(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_004f2d10(fn_004f2d10_Entry, fn_004f2d10_Abi)
HOOK(0x004f2d10, "fn_004f2d10", fn_004f2d10_Entry, nullptr);

// 0x004f2d60 (ecx = sound system; stack: handle, all (byte), flag (byte); callee pops 12; slot +0xe0; executed in
// recorded runs). Pause (guess from the flags): for each voice (all) or the one whose +4 == handle: unless flags have
// 0x40 or 0x8001000, flags |= 0x1000 and virtual +0x3c(voice). With all: flags |= 0x2000 for every voice, then
// +0xa8 += 1. With a handle: flags |= flag ? 0x800 : 0x4000, the search stops (IterBreak: list virtual +0xc, whose eax
// is returned). Otherwise returns the iterator chunk.
uint32_t fn_004f2d60(uint8_t* self, uint32_t handle, uint32_t all, uint32_t flag)
{
    Iter it = {0, 0};
    const uint8_t every = static_cast<uint8_t>(all);
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        uint32_t* voice = reinterpret_cast<uint32_t*>(v);
        if (every == 0 && handle != voice[1])
            continue;
        const uint32_t f = voice[0];
        if ((f & 0x40) == 0 && (f & 0x8001000) == 0)
        {
            voice[0] = f | 0x1000;
            VCall<uint32_t>(self, 0x3c / 4, v);
        }
        if (every == 0)
        {
            voice[0] |= static_cast<uint8_t>(flag) != 0 ? 0x800u : 0x4000u;
            return IterBreak(&it);
        }
        voice[0] |= 0x2000;
    }
    if (every != 0)
        ++Field<int32_t>(self, 0xa8);
    return IterEnd(&it);
}
static uint32_t fn_004f2d60_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004f2d60(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2);
}
ABI_ENTRY_004f2d60(fn_004f2d60_Entry, fn_004f2d60_Abi)
HOOK(0x004f2d60, "fn_004f2d60", fn_004f2d60_Entry, nullptr);

// 0x004f2e40 (ecx = sound system; stack: handle, all (byte), flag (byte); callee pops 12; slot +0xe4; executed in
// recorded runs). Resume, the counterpart of 0x004f2d60: all clears 0x2000 on every voice, a handle clears flag ?
// 0x800 : 0x4000 on its voice; then unless flags have 0x40 or 0x8006800, flags &= ~0x1000 and virtual +0x38(voice).
// With a handle the search stops (IterBreak, its eax returned);
// with all: 0x004f3000 (ebx = this) and +0xa8 -= 1. Returns the iterator chunk otherwise.
uint32_t fn_004f2e40(uint8_t* self, uint32_t handle, uint32_t all, uint32_t flag)
{
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        uint32_t* voice = reinterpret_cast<uint32_t*>(v);
        uint32_t f;
        if (static_cast<uint8_t>(all) != 0)
        {
            f = voice[0] & 0xffffdfffu;
        }
        else
        {
            if (handle != voice[1])
                continue;
            f = voice[0] & (static_cast<uint8_t>(flag) != 0 ? 0xfffff7ffu : 0xffffbfffu);
        }
        voice[0] = f;
        if ((f & 0x40) == 0 && (f & 0x8006800) == 0)
        {
            voice[0] = f & 0xffffefffu;
            VCall<uint32_t>(self, 0x38 / 4, v);
        }
        if (static_cast<uint8_t>(all) == 0)
            return IterBreak(&it);
    }
    if (static_cast<uint8_t>(all) != 0)
    {
        fn_004f3000(self);
        --Field<int32_t>(self, 0xa8);
    }
    return IterEnd(&it);
}
static uint32_t fn_004f2e40_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004f2e40(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2);
}
ABI_ENTRY_004f2e40(fn_004f2e40_Entry, fn_004f2e40_Abi)
HOOK(0x004f2e40, "fn_004f2e40", fn_004f2e40_Entry, nullptr);

// 0x004f2f30 (ecx = sound system; stack: value; callee pops 4; slot +0x94; not executed in recorded runs): +0x14 =
// value (a float scale, guess: pitch/frequency), then 0x004f2f50 (ebx = this); returns its eax.
uint32_t fn_004f2f30(uint8_t* self, uint32_t value)
{
    Field<uint32_t>(self, 0x14) = value;
    return fn_004f2f50(self);
}
static uint32_t fn_004f2f30_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f2f30(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f2f30(fn_004f2f30_Entry, fn_004f2f30_Abi)
HOOK(0x004f2f30, "fn_004f2f30", fn_004f2f30_Entry, nullptr);

// 0x004f2f50 (ebx = sound system; caller 0x004f2f30; not executed in recorded runs). For every voice without flags
// 0x1000: virtual +0x24(voice, v) with v = voice+0x1c when def flags have 0x2000000, else _ftol((float)(int)voice+0x1c
// * +0x14 + K(0x005bcb4c)) (fild, fmul, fadd, __ftol2: truncation). Returns the iterator chunk.
uint32_t fn_004f2f50(uint8_t* self)
{
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        uint32_t* voice = reinterpret_cast<uint32_t*>(v);
        if (voice[0] & 0x1000)
            continue;
        uint32_t value;
        if (Field<uint32_t>(P(voice[3]), 0x20) & 0x2000000)
        {
            value = voice[7];
        }
        else
        {
            const float f = static_cast<float>(static_cast<int32_t>(voice[7])) * Field<float>(self, 0x14) + K(0x005bcb4c);
            value = static_cast<uint32_t>(static_cast<int64_t>(f));
        }
        VCall<uint32_t>(self, 0x24 / 4, v, value);
    }
    return IterEnd(&it);
}
static uint32_t fn_004f2f50_Abi(uint32_t ebx)
{
    return fn_004f2f50(reinterpret_cast<uint8_t*>(ebx));
}
ABI_ENTRY_004f2f50(fn_004f2f50_Entry, fn_004f2f50_Abi)
HOOK(0x004f2f50, "fn_004f2f50", fn_004f2f50_Entry, nullptr);

// 0x004f3000 (ebx = sound system; callers 0x004f28d0, 0x004f2950, 0x004f2e40, 0x004f4260/42a0/42e0; executed in
// recorded runs). Volume refresh: for every voice without flags 0x1000, virtual +0x40(voice, virtual +0x10(voice))
// (+0x10 returns st0). Returns the iterator chunk.
uint32_t fn_004f3000(uint8_t* self)
{
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 0) & 0x1000)
            continue;
        const float volume = VCall<float>(self, 0x10 / 4, v);
        VCall<uint32_t>(self, 0x40 / 4, v, volume);
    }
    return IterEnd(&it);
}
static uint32_t fn_004f3000_Abi(uint32_t ebx)
{
    return fn_004f3000(reinterpret_cast<uint8_t*>(ebx));
}
ABI_ENTRY_004f3000(fn_004f3000_Entry, fn_004f3000_Abi)
HOOK(0x004f3000, "fn_004f3000", fn_004f3000_Entry, nullptr);

// 0x004f3090 (ecx = sound system; stack: voice; callee pops 4; returns st0; base vtable 0x0058aa40 +0x10 (the PC
// class overrides it with 0x004fb170); not executed in recorded runs). A voice's volume: the group level by def flags
// (0x2000000: +0x1c, 0x600: +0x20, 0x40000000: 0.0, else +0x18), plus +0x24 unless def flags have 0x800000, plus
// voice+0x10, +0x14 and +0x90 (added in that order).
float fn_004f3090(uint8_t* self, uint8_t* voice)
{
    const uint32_t f = Field<uint32_t>(Field<void*>(voice, 0xc), 0x20);
    float v;
    if (f & 0x2000000)
        v = Field<float>(self, 0x1c);
    else if (f & 0x600)
        v = Field<float>(self, 0x20);
    else if (f & 0x40000000)
        v = K(0x0056c05c);
    else
        v = Field<float>(self, 0x18);
    if ((f & 0x800000) == 0)
        v = v + Field<float>(self, 0x24);
    v = v + Field<float>(voice, 0x10);
    v = v + Field<float>(voice, 0x14);
    return v + Field<float>(voice, 0x90);
}
static float fn_004f3090_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f3090(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(a0));
}
ABI_ENTRY_004f3090(fn_004f3090_Entry, fn_004f3090_Abi)
HOOK(0x004f3090, "fn_004f3090", fn_004f3090_Entry, nullptr);

// 0x004f30e0 (ecx = sound system; stack: &handle, immediate; callee pops 8; slot +0xd8; executed in recorded runs).
// Stop a sound by handle: when *handle != 0, the first voice whose +4 == *handle is stopped (0x004f26a0(voice,
// immediate)) and *handle = 0. Returns the iterator chunk (0 when *handle was 0).
uint32_t fn_004f30e0(uint8_t* self, uint32_t* handle, uint32_t immediate)
{
    if (*handle == 0)
        return 0;
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 4) == *handle)
        {
            fn_004f26a0(self, reinterpret_cast<uint32_t*>(v), immediate);
            *handle = 0;
            break;
        }
    }
    return IterEnd(&it);
}
static uint32_t fn_004f30e0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f30e0(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint32_t*>(a0), a1);
}
ABI_ENTRY_004f30e0(fn_004f30e0_Entry, fn_004f30e0_Abi)
HOOK(0x004f30e0, "fn_004f30e0", fn_004f30e0_Entry, nullptr);

// 0x004f3170 (ecx = sound system; stack: object; callee pops 4; slot +0x120; not executed in recorded runs). When
// object != 0, stops (0x004f26a0(voice, 0)) every voice whose +8 == object. Returns the iterator chunk (0 otherwise).
uint32_t fn_004f3170(uint8_t* self, uint32_t object)
{
    if (object == 0)
        return 0;
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 8) == object)
            fn_004f26a0(self, reinterpret_cast<uint32_t*>(v), 0);
    }
    return IterEnd(&it);
}
static uint32_t fn_004f3170_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f3170(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f3170(fn_004f3170_Entry, fn_004f3170_Abi)
HOOK(0x004f3170, "fn_004f3170", fn_004f3170_Entry, nullptr);

// 0x004f3200 (ecx = sound system; stack: definition index, object; callee pops 8; slot +0x11c; not executed in
// recorded runs). When object != 0, stops (0x004f26a0(voice, 0)) every voice with definition +0x80[index] and +8 ==
// object (the table is re-read per voice). Returns the iterator chunk (0 otherwise). The tracker size (151) is short:
// the epilogue runs to 0x004f329f.
uint32_t fn_004f3200(uint8_t* self, uint32_t index, uint32_t object)
{
    if (object == 0)
        return 0;
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 0xc) == Field<uint32_t*>(self, 0x80)[index] && Field<uint32_t>(P(v), 8) == object)
            fn_004f26a0(self, reinterpret_cast<uint32_t*>(v), 0);
    }
    return IterEnd(&it);
}
static uint32_t fn_004f3200_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f3200(reinterpret_cast<uint8_t*>(ecx), a0, a1);
}
ABI_ENTRY_004f3200(fn_004f3200_Entry, fn_004f3200_Abi)
HOOK(0x004f3200, "fn_004f3200", fn_004f3200_Entry, nullptr);

// 0x004f32a0 (ecx = sound system; stack: name, object, a2, a3, a4; callee pops 20; slot +0xc8; not executed in
// recorded runs). Plays 2D (audio +0xd0) when callback +0x64(object) (cdecl, guess: "object is the listener")
// returns true, else positional (+0xc4), with (name, object, a2, a3, a4, 0); returns the call's eax.
uint32_t fn_004f32a0(uint8_t* self, uint32_t name, uint32_t object, uint32_t a2, uint32_t a3, uint32_t a4)
{
    const uint8_t local = static_cast<uint8_t>(
        reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(Field<uint32_t>(self, 0x64))(object));
    if (local != 0)
        return VCall<uint32_t>(self, 0xd0 / 4, name, object, a2, a3, a4, 0u);
    return VCall<uint32_t>(self, 0xc4 / 4, name, object, a2, a3, a4, 0u);
}
static uint32_t fn_004f32a0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004f32a0(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a3, a4);
}
ABI_ENTRY_004f32a0(fn_004f32a0_Entry, fn_004f32a0_Abi)
HOOK(0x004f32a0, "fn_004f32a0", fn_004f32a0_Entry, nullptr);

uint32_t fn_004f3a80(uint8_t* def, uint8_t* self);

namespace
{
// Definition index by name (the name map +0xa0, 0x004f1160), as inlined in the play functions: -1 when the map or the
// entry is missing.
uint32_t LookupIndex(uint8_t* self, uint32_t name)
{
    const uint32_t map = Field<uint32_t>(self, 0xa0);
    if (map == 0)
        return 0xffffffffu;
    const uint32_t e = orig::fn_004f1160(map, name);
    return e != 0 ? Field<uint32_t>(P(e), 0) : 0xffffffffu;
}
// The common tail of the play functions 0x004f32f0 / 0x004f3740 after the voice is filled: frequency, a new handle
// (+0x34, skipping 0), driver start virtual +0x34(def, index, volume, frequency, extra, voice, &voice+0x20, 1); on
// failure the voice is released (virtual +0xc) and 0 returned; on success flags |= 0x8000000 when def flags have
// 0x20000, flags |= startFlag, and the handle is returned. The iterator is ended in all cases.
uint32_t StartVoice(uint8_t* self, uint8_t* def, uint32_t* voice, uint32_t index, uint32_t extra, uint32_t startFlag,
                    Iter* it, uint32_t lastArg = 1)
{
    const float volume = VCall<float>(self, 0x10 / 4, u(voice));
    uint32_t frequency;
    if (Field<uint32_t>(P(voice[3]), 0x20) & 0x2000000)
    {
        frequency = voice[7];
    }
    else
    {
        const float f = static_cast<float>(static_cast<int32_t>(voice[7])) * Field<float>(self, 0x14) + K(0x005bcb4c);
        frequency = static_cast<uint32_t>(static_cast<int64_t>(f));
    }
    const uint32_t handle = Field<uint32_t>(self, 0x34);
    Field<uint32_t>(self, 0x34) = handle + 1;
    if (handle + 1 == 0)
        Field<uint32_t>(self, 0x34) = 1;
    voice[1] = handle;
    const uint32_t ok = VCall<uint32_t>(self, 0x34 / 4, u(def), index, fb(volume), frequency, extra, u(voice),
                                        u(voice + 8), lastArg);
    if (static_cast<uint8_t>(ok) == 0)
    {
        VCall<uint32_t>(self, 0xc / 4, u(voice));
        IterEnd(it);
        return 0;
    }
    if (Field<uint32_t>(def, 0x20) & 0x20000)
        voice[0] |= 0x8000000;
    voice[0] |= startFlag;
    const uint32_t h = voice[1];
    IterEnd(it);
    return h;
}
// Unique definitions (def flags & 1): a live voice of the same definition and object is either stopped (def flags &
// 2: 0x004f26a0(voice, 1)) or its handle returned (the iteration ends). Returns true with *handle set in that case.
bool UniqueVoice(uint8_t* self, uint8_t* def, uint32_t object, Iter* it, uint32_t* handle)
{
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), it); v != 0; v = IterNext(it))
    {
        uint32_t* voice = reinterpret_cast<uint32_t*>(v);
        if (voice[3] != u(def) || voice[2] != object || (voice[0] & 0x20000000) != 0)
            continue;
        if ((Field<uint8_t>(def, 0x20) & 2) == 0)
        {
            *handle = voice[1];
            IterEnd(it);
            return true;
        }
        fn_004f26a0(self, voice, 1);
    }
    return false;
}
// A new voice from the pool +0x30 (0x004f6020, esi = pool), zeroed (0x98 bytes) and added to the list (+0x28 virtual
// +0x14) before the null test, as in the original.
uint32_t* NewVoice(uint8_t* self)
{
    uint32_t* voice = reinterpret_cast<uint32_t*>(orig::fn_004f6020(Field<uint32_t>(self, 0x30)));
    std::memset(voice, 0, 0x26 * 4);
    VCall<uint32_t>(Field<void*>(self, 0x28), 0x14 / 4, u(voice));
    return voice;
}
}  // namespace

// 0x004f32f0 (ecx = sound system; stack: name, object, extra, index; callee pops 16; slot +0x58; executed in recorded
// runs). Play a non-positional sound: the definition by name (map +0xa0; the index argument slot receives it) or by
// index; out of range logs "Audio : %s not found" (level 5) and returns 0; no table or no definition returns 0.
// Unique definitions reuse/stop the object's voice (see UniqueVoice). Needs byte +0xc (initialised). New voice: def,
// flags 0x1c, +0x14 = 0, +0x10 = def+0x28, +0x18 = +0x1c = 0x004f3a80(def) (frequency), +0x90 = 0, +8 = object,
// callback +0x54(object) (cdecl), 0x004f2870, then StartVoice (start flag 0x80). Returns the handle or 0.
uint32_t fn_004f32f0(uint8_t* self, uint32_t name, uint32_t object, uint32_t extra, uint32_t index)
{
    Iter it = {0, 0};
    if (Field<uint32_t>(self, 0x80) == 0)
        return 0;
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
        return 0;
    }
    uint8_t* def = Field<uint8_t**>(self, 0x80)[index];
    if (def == nullptr)
        return 0;
    if (Field<uint8_t>(def, 0x20) & 1)
    {
        uint32_t h;
        if (UniqueVoice(self, def, object, &it, &h))
            return h;
    }
    if (Field<uint8_t>(self, 0xc) == 0)
    {
        IterEnd(&it);
        return 0;
    }
    uint32_t* voice = NewVoice(self);
    if (voice == nullptr)
    {
        IterEnd(&it);
        return 0;
    }
    voice[3] = u(def);
    voice[0] = 0x1c;
    voice[5] = 0;
    voice[4] = Field<uint32_t>(def, 0x28);
    const uint32_t frequency = fn_004f3a80(def, self);
    voice[6] = frequency;
    voice[7] = frequency;
    voice[0x24] = 0;
    voice[2] = object;
    reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(Field<uint32_t>(self, 0x54))(object);
    fn_004f2870(voice, self);
    return StartVoice(self, def, voice, index, extra, 0x80, &it);
}
static uint32_t fn_004f32f0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004f32f0(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004f32f0(fn_004f32f0_Entry, fn_004f32f0_Abi)
HOOK(0x004f32f0, "fn_004f32f0", fn_004f32f0_Entry, nullptr);

// 0x004f3530 (ebx = handle; stack: sound system; callee pops 4; caller 0x004f35e0; executed in recorded runs). The
// voice in +0x28 whose +4 == handle (leaving the iteration early, IterBreak), or 0; 0 for handle 0.
uint32_t fn_004f3530(uint32_t handle, uint8_t* self)
{
    if (handle == 0)
        return 0;
    Iter it = {0, 0};
    uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it);
    for (; v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 4) == handle)
        {
            IterBreak(&it);
            return v;
        }
    }
    IterEnd(&it);
    return v;
}
static uint32_t fn_004f3530_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_004f3530(ebx, reinterpret_cast<uint8_t*>(a0));
}
ABI_ENTRY_004f3530(fn_004f3530_Entry, fn_004f3530_Abi)
HOOK(0x004f3530, "fn_004f3530", fn_004f3530_Entry, nullptr);

// 0x004f35e0 (edi = sound system; the eax "input" is overwritten (or eax, -1); stack: definition, &busy (byte);
// callee pops 8; callers 0x004f3670, 0x004f39a0, 0x004f4030; executed in recorded runs). Exclusive channel of a
// definition: *busy = 0; without def flags bits 11..15 returns -1. Otherwise channel i = the lowest set bit of
// (flags >> 11) & 0x1f, with the handle slot +0x38 + 4i. When that handle still plays (audio +0xdc): if the playing
// voice's definition +0x24 (priority, guess) >= this definition's (unsigned) *busy = 1; otherwise it is stopped
// (+0xd8(&slot, 0)). A finished non-zero handle is stopped as well. Returns i. The playing voice is looked up with
// 0x004f3530 and not null-checked (as in the original).
uint32_t fn_004f35e0(uint8_t* self, uint8_t* def, uint8_t* busy)
{
    *busy = 0;
    uint32_t flags = Field<uint32_t>(def, 0x20);
    if ((flags & 0xf800) == 0)
        return 0xffffffffu;
    flags = (flags >> 11) & 0x1f;
    uint32_t i = 0;
    while ((flags & 1) == 0)
    {
        flags >>= 1;
        ++i;
    }
    uint32_t* slot = &Field<uint32_t>(self, 0x38 + i * 4);
    if (static_cast<uint8_t>(VCall<uint32_t>(self, 0xdc / 4, *slot)) != 0)
    {
        const uint32_t v = fn_004f3530(*slot, self);
        if (Field<uint32_t>(Field<void*>(P(v), 0xc), 0x24) >= Field<uint32_t>(def, 0x24))
        {
            *busy = 1;
            return i;
        }
        VCall<uint32_t>(self, 0xd8 / 4, u(slot), 0u);
        return i;
    }
    if (*slot != 0)
        VCall<uint32_t>(self, 0xd8 / 4, u(slot), 0u);
    return i;
}
static uint32_t fn_004f35e0_Abi(uint32_t /*eax*/, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_004f35e0(reinterpret_cast<uint8_t*>(edi), reinterpret_cast<uint8_t*>(a0), reinterpret_cast<uint8_t*>(a1));
}
ABI_ENTRY_004f35e0(fn_004f35e0_Entry, fn_004f35e0_Abi)
HOOK(0x004f35e0, "fn_004f35e0", fn_004f35e0_Entry, nullptr);

// 0x004f3670 (ecx = sound system; the ebx "input" is only saved; stack: name, object, a2, a3 (not read), index,
// channel handle; callee pops 24; slot +0xc4; not executed in recorded runs, but called by voice code through the
// audio vtable). Play with an exclusive channel: definition by name or index (as 0x004f32f0, the log when out of
// range); 0x004f35e0(def, &busy) (busy byte written over the name slot's low byte) gives the channel i; when busy
// returns the channel's handle +0x38 + 4i. Otherwise h = virtual +0x58(name, object, a2, index) and, when i >= 0,
// the channel slot = the last argument if non-zero, else h. Returns h.
uint32_t fn_004f3670(uint8_t* self, uint32_t name, uint32_t object, uint32_t a2, uint32_t index, uint32_t last)
{
    if (Field<uint32_t>(self, 0x80) == 0)
        return 0;
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
        return 0;
    }
    uint8_t* def = Field<uint8_t**>(self, 0x80)[index];
    if (def == nullptr)
        return 0;
    uint8_t busy;
    const int32_t i = static_cast<int32_t>(fn_004f35e0(self, def, &busy));
    if (busy != 0)
        return Field<uint32_t>(self, 0x38 + i * 4);
    const uint32_t h = VCall<uint32_t>(self, 0x58 / 4, name, object, a2, index);
    if (i >= 0)
        Field<uint32_t>(self, 0x38 + i * 4) = last != 0 ? last : h;
    return h;
}
static uint32_t fn_004f3670_Abi(uint32_t ecx, uint32_t /*ebx*/, uint32_t a0, uint32_t a1, uint32_t a2,
                                uint32_t /*a3*/, uint32_t a4, uint32_t a5)
{
    return fn_004f3670(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a4, a5);
}
ABI_ENTRY_004f3670(fn_004f3670_Entry, fn_004f3670_Abi)
HOOK(0x004f3670, "fn_004f3670", fn_004f3670_Entry, nullptr);

// 0x004f3740 (ecx = sound system; stack: name, &position, object, extra, index; callee pops 20; slot +0x5c; executed
// in recorded runs). Play a positional sound: as 0x004f32f0, but the unique-voice check needs object != 0, the new
// voice has flags 0x10 and position +0x20 = *position (3 dwords), and only with an object: flags |= 6, +8 = object and
// callback +0x54(object). Start flag 0x100.
uint32_t fn_004f3740(uint8_t* self, uint32_t name, const uint32_t* position, uint32_t object, uint32_t extra,
                     uint32_t index)
{
    Iter it = {0, 0};
    if (Field<uint32_t>(self, 0x80) == 0)
        return 0;
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
        return 0;
    }
    uint8_t* def = Field<uint8_t**>(self, 0x80)[index];
    if (def == nullptr)
        return 0;
    if ((Field<uint8_t>(def, 0x20) & 1) && object != 0)
    {
        uint32_t h;
        if (UniqueVoice(self, def, object, &it, &h))
            return h;
    }
    if (Field<uint8_t>(self, 0xc) == 0)
    {
        IterEnd(&it);
        return 0;
    }
    uint32_t* voice = NewVoice(self);
    if (voice == nullptr)
    {
        IterEnd(&it);
        return 0;
    }
    voice[3] = u(def);
    voice[0] = 0x10;
    voice[5] = 0;
    voice[4] = Field<uint32_t>(def, 0x28);
    const uint32_t frequency = fn_004f3a80(def, self);
    voice[6] = frequency;
    voice[7] = frequency;
    voice[8] = position[0];
    voice[9] = position[1];
    voice[10] = position[2];
    voice[0x24] = 0;
    if (object != 0)
    {
        voice[0] |= 6;
        voice[2] = object;
        reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(Field<uint32_t>(self, 0x54))(object);
    }
    fn_004f2870(voice, self);
    return StartVoice(self, def, voice, index, extra, 0x100, &it);
}
static uint32_t fn_004f3740_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004f3740(reinterpret_cast<uint8_t*>(ecx), a0, reinterpret_cast<const uint32_t*>(a1), a2, a3, a4);
}
ABI_ENTRY_004f3740(fn_004f3740_Entry, fn_004f3740_Abi)
HOOK(0x004f3740, "fn_004f3740", fn_004f3740_Entry, nullptr);

// 0x004f39a0 (ecx = sound system; the ebx "input" is only saved; stack: name, &position, object, a3, a4 (not read),
// index, channel handle; callee pops 28; slot +0xcc; not executed in recorded runs). The positional counterpart of
// 0x004f3670: h = virtual +0x5c(name, &position, object, a3, index).
uint32_t fn_004f39a0(uint8_t* self, uint32_t name, uint32_t position, uint32_t object, uint32_t a3, uint32_t index,
                     uint32_t last)
{
    if (Field<uint32_t>(self, 0x80) == 0)
        return 0;
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
        return 0;
    }
    uint8_t* def = Field<uint8_t**>(self, 0x80)[index];
    if (def == nullptr)
        return 0;
    uint8_t busy;
    const int32_t i = static_cast<int32_t>(fn_004f35e0(self, def, &busy));
    if (busy != 0)
        return Field<uint32_t>(self, 0x38 + i * 4);
    const uint32_t h = VCall<uint32_t>(self, 0x5c / 4, name, position, object, a3, index);
    if (i >= 0)
        Field<uint32_t>(self, 0x38 + i * 4) = last != 0 ? last : h;
    return h;
}
static uint32_t fn_004f39a0_Abi(uint32_t ecx, uint32_t /*ebx*/, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                uint32_t /*a4*/, uint32_t a5, uint32_t a6)
{
    return fn_004f39a0(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a3, a5, a6);
}
ABI_ENTRY_004f39a0(fn_004f39a0_Entry, fn_004f39a0_Abi)
HOOK(0x004f39a0, "fn_004f39a0", fn_004f39a0_Entry, nullptr);

// 0x004f3a80 (ecx = definition; stack: sound system (not read); callee pops 4; callers 0x004f32f0, 0x004f3740,
// 0x004f3c60, 0x004f3eb0; executed in recorded runs). Playback frequency: base = def+0xe0 as unsigned (fild, +2^32
// when negative); with a variation def+0x2c > 0: hi = (v * K(0x005bcbe4) + 1) * base, lo = (1 - v * K(0x005bccdc)) *
// base, result = (hi - lo) * rand() * K(0x005bcbc0) + lo; truncated (__ftol2).
uint32_t fn_004f3a80(uint8_t* def, uint8_t* /*self*/)
{
    const int32_t raw = Field<int32_t>(def, 0xe0);
    const float one = K(0x005bcb2c);
    float base = static_cast<float>(raw);
    if (raw < 0)
        base = base + K(0x005bcb28);
    const float v = Field<float>(def, 0x2c);
    if (!(v > K(0x0056c05c)))
    {
        if (raw >= 0)
            return static_cast<uint32_t>(raw);  // fild/ftol of an int is exact
        return static_cast<uint32_t>(static_cast<int64_t>(base));
    }
    const float hi = (v * K(0x005bcbe4) + one) * base;
    const float lo = (one - v * K(0x005bccdc)) * base;
    const float r = static_cast<float>(Rand());
    const float f = (hi - lo) * r * K(0x005bcbc0) + lo;
    return static_cast<uint32_t>(static_cast<int64_t>(f));
}
static uint32_t fn_004f3a80_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f3a80(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(a0));
}
ABI_ENTRY_004f3a80(fn_004f3a80_Entry, fn_004f3a80_Abi)
HOOK(0x004f3a80, "fn_004f3a80", fn_004f3a80_Entry, nullptr);

// 0x004f3b10 (ecx = sound system; the esi "input" is overwritten on every path; stack: name, a1, a2, index/flag;
// callee pops 16; slot +0x64; not executed in recorded runs). Music (guess: one current voice +0x90 with handle +0x98,
// queued definition +0x94): definition index by name or the last argument; out of range returns 0. When the current
// handle still plays (audio +0xdc): +0x94 = the definition. Otherwise +0x90 = +0x98 = 0, h = virtual +0x60(name, a1,
// a2, index) (0 returns 0), +0x90 = the voice in +0x28 whose +4 == h (stays 0 when none) and Audio_ResolveSound
// (0x00500a00, edi = 0; this, voice, &voice+0x64, 1). Then the low byte of the last argument sets (or clears) flag
// 0x8000000 of voice +0x90 (not null-checked), +0x98 = its handle, which is returned.
uint32_t fn_004f3b10(uint8_t* self, uint32_t name, uint32_t a1, uint32_t a2, uint32_t index)
{
    const uint32_t last = index;
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
        return 0;
    if (static_cast<uint8_t>(VCall<uint32_t>(self, 0xdc / 4, Field<uint32_t>(self, 0x98))) != 0)
    {
        Field<uint32_t>(self, 0x94) = Field<uint32_t*>(self, 0x80)[index];
    }
    else
    {
        Iter it = {0, 0};
        Field<uint32_t>(self, 0x90) = 0;
        Field<uint32_t>(self, 0x98) = 0;
        const uint32_t h = VCall<uint32_t>(self, 0x60 / 4, name, a1, a2, index);
        if (h == 0)
            return 0;
        for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
        {
            if (Field<uint32_t>(P(v), 4) == h)
            {
                Field<uint32_t>(self, 0x90) = v;
                break;
            }
        }
        const uint32_t voice = Field<uint32_t>(self, 0x90);
        orig::fn_00500a00(0, u(self), voice, voice + 0x64, 1);
        IterEnd(&it);
    }
    uint32_t* voice = Field<uint32_t*>(self, 0x90);
    if (static_cast<uint8_t>(last) != 0)
        voice[0] |= 0x8000000;
    else
        voice[0] &= 0xf7ffffffu;
    Field<uint32_t>(self, 0x98) = Field<uint32_t*>(self, 0x90)[1];
    return Field<uint32_t*>(self, 0x90)[1];
}
static uint32_t fn_004f3b10_Abi(uint32_t ecx, uint32_t /*esi*/, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004f3b10(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004f3b10(fn_004f3b10_Entry, fn_004f3b10_Abi)
HOOK(0x004f3b10, "fn_004f3b10", fn_004f3b10_Entry, nullptr);

// 0x004f3c60 (ecx = sound system; stack: name, object, extra, index; callee pops 16; slot +0x60; executed in recorded
// runs). Play as 0x004f32f0 with these differences: the unique-voice check needs object != 0; the new voice has
// flags 1 and, only with an object, flags |= 6, +8 = object and callback +0x54(object); no 0x004f2870; the start call
// passes 0 as its last argument and the start flag is 0x200.
uint32_t fn_004f3c60(uint8_t* self, uint32_t name, uint32_t object, uint32_t extra, uint32_t index)
{
    Iter it = {0, 0};
    if (Field<uint32_t>(self, 0x80) == 0)
        return 0;
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
        return 0;
    }
    uint8_t* def = Field<uint8_t**>(self, 0x80)[index];
    if (def == nullptr)
        return 0;
    if ((Field<uint8_t>(def, 0x20) & 1) && object != 0)
    {
        uint32_t h;
        if (UniqueVoice(self, def, object, &it, &h))
            return h;
    }
    if (Field<uint8_t>(self, 0xc) == 0)
    {
        IterEnd(&it);
        return 0;
    }
    uint32_t* voice = NewVoice(self);
    if (voice == nullptr)
    {
        IterEnd(&it);
        return 0;
    }
    voice[3] = u(def);
    voice[0] = 1;
    voice[5] = 0;
    voice[4] = Field<uint32_t>(def, 0x28);
    const uint32_t frequency = fn_004f3a80(def, self);
    voice[6] = frequency;
    voice[7] = frequency;
    voice[0x24] = 0;
    if (object != 0)
    {
        voice[0] |= 6;
        voice[2] = object;
        reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(Field<uint32_t>(self, 0x54))(object);
    }
    return StartVoice(self, def, voice, index, extra, 0x200, &it, 0);
}
static uint32_t fn_004f3c60_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004f3c60(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004f3c60(fn_004f3c60_Entry, fn_004f3c60_Abi)
HOOK(0x004f3c60, "fn_004f3c60", fn_004f3c60_Entry, nullptr);

// 0x004f3eb0 (ecx = sound system; stack: a0, channels (word), rate, bits (word); callee pops 16; slot +0xc0; not
// executed in recorded runs). Stream voice (guess: the static definition is a raw PCM format): needs byte +0xc. New
// voice (as NewVoice); then the static definition 0x00638e28 (0xfc bytes) is zeroed, its flags +0x20 |= 0x40000040 and
// a WAVEFORMATEX-like block filled: +0xde = channels, +0xea = bits, +0xe8 = channels * bits / 8 (signed, truncated),
// +0xe0 = rate, +0xe4 = (+0xe8) * rate. Voice: def, flags 0x10001, +0x14 = 0, +0x10 = def+0x28, frequency 0x004f3a80,
// +0x90 = 0, +0x78 = a0; volume virtual +0x10; frequency as in StartVoice; new handle; virtual +0x34(def, -1, volume,
// frequency, 0, voice, &voice+0x20, 0): failure releases the voice (+0xc) and returns 0, success sets flag 0x200 and
// returns the handle.
uint32_t fn_004f3eb0(uint8_t* self, uint32_t a0, uint32_t channels, uint32_t rate, uint32_t bits)
{
    if (Field<uint8_t>(self, 0xc) == 0)
        return 0;
    uint32_t* voice = NewVoice(self);
    if (voice == nullptr)
        return 0;
    uint8_t* def = reinterpret_cast<uint8_t*>(0x00638e28);
    std::memset(def, 0, 0x3f * 4);
    Field<uint32_t>(def, 0x20) |= 0x40000040;
    const uint16_t ch = static_cast<uint16_t>(channels);
    const uint16_t bt = static_cast<uint16_t>(bits);
    Field<uint16_t>(def, 0xde) = ch;
    Field<uint16_t>(def, 0xea) = bt;
    const int32_t product = static_cast<int32_t>(ch) * static_cast<int32_t>(bt);
    const uint16_t align = static_cast<uint16_t>((product + ((product >> 31) & 7)) >> 3);
    Field<uint16_t>(def, 0xe8) = align;
    Field<uint32_t>(def, 0xe0) = rate;
    Field<uint32_t>(def, 0xe4) = static_cast<uint32_t>(align) * rate;
    voice[3] = u(def);
    voice[0] = 0x10001;
    voice[5] = 0;
    voice[4] = Field<uint32_t>(def, 0x28);
    const uint32_t frequency = fn_004f3a80(def, self);
    voice[6] = frequency;
    voice[7] = frequency;
    voice[0x24] = 0;
    voice[0x1e] = a0;
    const float volume = VCall<float>(self, 0x10 / 4, u(voice));
    uint32_t f;
    if (Field<uint32_t>(P(voice[3]), 0x20) & 0x2000000)
    {
        f = voice[7];
    }
    else
    {
        const float x = static_cast<float>(static_cast<int32_t>(voice[7])) * Field<float>(self, 0x14) + K(0x005bcb4c);
        f = static_cast<uint32_t>(static_cast<int64_t>(x));
    }
    const uint32_t handle = Field<uint32_t>(self, 0x34);
    Field<uint32_t>(self, 0x34) = handle + 1;
    if (handle + 1 == 0)
        Field<uint32_t>(self, 0x34) = 1;
    voice[1] = handle;
    const uint32_t ok = VCall<uint32_t>(self, 0x34 / 4, u(def), 0xffffffffu, fb(volume), f, 0u, u(voice), u(voice + 8),
                                        0u);
    if (static_cast<uint8_t>(ok) == 0)
    {
        VCall<uint32_t>(self, 0xc / 4, u(voice));
        return 0;
    }
    voice[0] |= 0x200;
    return voice[1];
}
static uint32_t fn_004f3eb0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004f3eb0(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004f3eb0(fn_004f3eb0_Entry, fn_004f3eb0_Abi)
HOOK(0x004f3eb0, "fn_004f3eb0", fn_004f3eb0_Entry, nullptr);

// 0x004f4030 (ecx = sound system; the ebx "input" is only saved; stack: name, object, a2, a3 (not read), index,
// channel handle; callee pops 24; slot +0xd0; not executed in recorded runs, but called by voice code). As 0x004f3670
// with h = virtual +0x60(name, object, a2, index).
uint32_t fn_004f4030(uint8_t* self, uint32_t name, uint32_t object, uint32_t a2, uint32_t index, uint32_t last)
{
    if (Field<uint32_t>(self, 0x80) == 0)
        return 0;
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
        return 0;
    }
    uint8_t* def = Field<uint8_t**>(self, 0x80)[index];
    if (def == nullptr)
        return 0;
    uint8_t busy;
    const int32_t i = static_cast<int32_t>(fn_004f35e0(self, def, &busy));
    if (busy != 0)
        return Field<uint32_t>(self, 0x38 + i * 4);
    const uint32_t h = VCall<uint32_t>(self, 0x60 / 4, name, object, a2, index);
    if (i >= 0)
        Field<uint32_t>(self, 0x38 + i * 4) = last != 0 ? last : h;
    return h;
}
static uint32_t fn_004f4030_Abi(uint32_t ecx, uint32_t /*ebx*/, uint32_t a0, uint32_t a1, uint32_t a2,
                                uint32_t /*a3*/, uint32_t a4, uint32_t a5)
{
    return fn_004f4030(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a4, a5);
}
ABI_ENTRY_004f4030(fn_004f4030_Entry, fn_004f4030_Abi)
HOOK(0x004f4030, "fn_004f4030", fn_004f4030_Entry, nullptr);

// 0x004f4100 (ecx = sound system; the eax "input" is overwritten (or eax, -1); stack: name, a1, a2, a3; callee pops
// 16; slot +0xd4; executed in recorded runs). Checks the definition by name only (without a name the index stays -1
// and the "Audio : %s not found" log runs with a null name), then returns virtual +0x64(name, a1, a2, a3) (0x004f3b10).
uint32_t fn_004f4100(uint8_t* self, uint32_t name, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (Field<uint32_t>(self, 0x80) == 0)
        return 0;
    uint32_t index = 0xffffffffu;
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
        return 0;
    }
    if (Field<uint32_t*>(self, 0x80)[index] == 0)
        return 0;
    return VCall<uint32_t>(self, 0x64 / 4, name, a1, a2, a3);
}
static uint32_t fn_004f4100_Abi(uint32_t /*eax*/, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004f4100(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004f4100(fn_004f4100_Entry, fn_004f4100_Abi)
HOOK(0x004f4100, "fn_004f4100", fn_004f4100_Entry, nullptr);

// 0x004f4190 (ecx = sound system; stack: handle; callee pops 4; slot +0xe8; not executed in recorded runs). The
// definition (+0xc) of the voice with this handle (leaving the iteration early); handle 0 returns the string
// "0 handle, error" (0x0058ad18), an unknown handle "handle not found" (0x0058ad04).
uint32_t fn_004f4190(uint8_t* self, uint32_t handle)
{
    if (handle == 0)
        return 0x0058ad18;
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 4) == handle)
        {
            IterBreak(&it);
            return Field<uint32_t>(P(v), 0xc);
        }
    }
    IterEnd(&it);
    return 0x0058ad04;
}
static uint32_t fn_004f4190_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f4190(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f4190(fn_004f4190_Entry, fn_004f4190_Abi)
HOOK(0x004f4190, "fn_004f4190", fn_004f4190_Entry, nullptr);

// Entry stubs for the three vtable thunks below, which tools/abi.py marks "no ret found" (tail jumps through the
// vtable) and so has no generated stub. Same shape as src/gen/entry_stubs.h.
#define AUDIO02_ENTRY_ECX(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push ecx __asm call impl __asm add esp, 4 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret }
#define AUDIO02_ENTRY_ECX_S1(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push dword ptr [esp + 28] __asm push ecx __asm call impl __asm add esp, 8 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret 4 }

namespace
{
// 0x004f4260 / 0x004f42a0 / 0x004f42e0: group level setters. level = virtual +0xac(1 - value, 0, 1.0f) (st0); when it
// differs from the member (fcom, test ah 0x44, jnp: unordered counts as different) it is stored and 0x004f3000
// refreshes the voices.
uint32_t SetGroupLevel(uint8_t* self, uint32_t offset, float value)
{
    const float level = VCall<float>(self, 0xac / 4, K(0x005bcb2c) - value, 0u, 0x3f800000u);
    if (level == Field<float>(self, offset))
        return 0;
    Field<float>(self, offset) = level;
    return fn_004f3000(self);
}
inline float ArgFloat(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
}  // namespace

// 0x004f4260 (ecx = sound system; stack: value (float); callee pops 4; slot +0x90; not executed in recorded runs):
// SetGroupLevel on +0x20 (the level of definitions with flags 0x600, read by 0x004f3090; getter 0x004f4320).
uint32_t fn_004f4260(uint8_t* self, float value)
{
    return SetGroupLevel(self, 0x20, value);
}
static uint32_t fn_004f4260_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f4260(reinterpret_cast<uint8_t*>(ecx), ArgFloat(a0));
}
ABI_ENTRY_004f4260(fn_004f4260_Entry, fn_004f4260_Abi)
HOOK(0x004f4260, "fn_004f4260", fn_004f4260_Entry, nullptr);

// 0x004f42a0 (ecx = sound system; stack: value (float); callee pops 4; slot +0x8c; not executed in recorded runs):
// SetGroupLevel on +0x1c (definitions with flags 0x2000000; getter 0x004f4330).
uint32_t fn_004f42a0(uint8_t* self, float value)
{
    return SetGroupLevel(self, 0x1c, value);
}
static uint32_t fn_004f42a0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f42a0(reinterpret_cast<uint8_t*>(ecx), ArgFloat(a0));
}
ABI_ENTRY_004f42a0(fn_004f42a0_Entry, fn_004f42a0_Abi)
HOOK(0x004f42a0, "fn_004f42a0", fn_004f42a0_Entry, nullptr);

// 0x004f42e0 (ecx = sound system; stack: value (float); callee pops 4; slot +0x88; not executed in recorded runs):
// SetGroupLevel on +0x18 (the default group; getter 0x004f4340).
uint32_t fn_004f42e0(uint8_t* self, float value)
{
    return SetGroupLevel(self, 0x18, value);
}
static uint32_t fn_004f42e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f42e0(reinterpret_cast<uint8_t*>(ecx), ArgFloat(a0));
}
ABI_ENTRY_004f42e0(fn_004f42e0_Entry, fn_004f42e0_Abi)
HOOK(0x004f42e0, "fn_004f42e0", fn_004f42e0_Entry, nullptr);

// 0x004f4320 (ecx = sound system; returns st0; slot +0xa0; not executed in recorded runs): returns +0x20.
float fn_004f4320(uint8_t* self)
{
    return Field<float>(self, 0x20);
}
static float fn_004f4320_Abi(uint32_t ecx)
{
    return fn_004f4320(reinterpret_cast<uint8_t*>(ecx));
}
ABI_ENTRY_004f4320(fn_004f4320_Entry, fn_004f4320_Abi)
HOOK(0x004f4320, "fn_004f4320", fn_004f4320_Entry, nullptr);

// 0x004f4330 (ecx = sound system; returns st0; slot +0x9c; not executed in recorded runs): returns +0x1c.
float fn_004f4330(uint8_t* self)
{
    return Field<float>(self, 0x1c);
}
static float fn_004f4330_Abi(uint32_t ecx)
{
    return fn_004f4330(reinterpret_cast<uint8_t*>(ecx));
}
ABI_ENTRY_004f4330(fn_004f4330_Entry, fn_004f4330_Abi)
HOOK(0x004f4330, "fn_004f4330", fn_004f4330_Entry, nullptr);

// 0x004f4340 (ecx = sound system; returns st0; slot +0x98; not executed in recorded runs): returns +0x18.
float fn_004f4340(uint8_t* self)
{
    return Field<float>(self, 0x18);
}
static float fn_004f4340_Abi(uint32_t ecx)
{
    return fn_004f4340(reinterpret_cast<uint8_t*>(ecx));
}
ABI_ENTRY_004f4340(fn_004f4340_Entry, fn_004f4340_Abi)
HOOK(0x004f4340, "fn_004f4340", fn_004f4340_Entry, nullptr);

// 0x004f4350 (ecx = sound system; stack: a0, a1; callee pops 8; slot +0x124; not executed in recorded runs): +0x6c =
// a0; when byte +0x70 is set, virtual +0x48(a0, a1) (its eax returned; otherwise a0).
uint32_t fn_004f4350(uint8_t* self, uint32_t a0, uint32_t a1)
{
    const uint8_t enabled = Field<uint8_t>(self, 0x70);
    Field<uint32_t>(self, 0x6c) = a0;
    if (enabled == 0)
        return a0;
    return VCall<uint32_t>(self, 0x48 / 4, a0, a1);
}
static uint32_t fn_004f4350_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f4350(reinterpret_cast<uint8_t*>(ecx), a0, a1);
}
ABI_ENTRY_004f4350(fn_004f4350_Entry, fn_004f4350_Abi)
HOOK(0x004f4350, "fn_004f4350", fn_004f4350_Entry, nullptr);

// 0x004f4370 (ecx = sound system; slot +0x128; not executed in recorded runs): tail jump to virtual +0x4c with the
// caller's stack argument intact. symbols/abi.csv has "no ret found, stack 0"; the target (0x004f9dc0 in the PC
// vtable) ends in `ret 4`, so this thunk takes one stack argument (a float) and the stub pops it.
uint32_t fn_004f4370(uint8_t* self, uint32_t a0)
{
    return VCall<uint32_t>(self, 0x4c / 4, a0);
}
static uint32_t fn_004f4370_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f4370(reinterpret_cast<uint8_t*>(ecx), a0);
}
AUDIO02_ENTRY_ECX_S1(fn_004f4370_Entry, fn_004f4370_Abi)
HOOK(0x004f4370, "fn_004f4370", fn_004f4370_Entry, nullptr);

// 0x004f4380 (ecx = sound system; stack: handle; callee pops 4; slot +0xdc; executed in recorded runs). Is a sound
// playing: false for handle 0 or an unknown handle; otherwise returns virtual +0x28(voice) (al) for the voice with this
// handle (the iteration ends without the list call). Upper bytes of eax are not reproduced on the false paths.
uint32_t fn_004f4380(uint8_t* self, uint32_t handle)
{
    if (handle == 0)
        return 0;
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 4) == handle)
        {
            const uint32_t r = VCall<uint32_t>(self, 0x28 / 4, v);
            IterEnd(&it);
            return r;
        }
    }
    IterEnd(&it);
    return 0;
}
static uint32_t fn_004f4380_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f4380(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f4380(fn_004f4380_Entry, fn_004f4380_Abi)
HOOK(0x004f4380, "fn_004f4380", fn_004f4380_Entry, nullptr);

// 0x004f4430 (ecx = sound system; stack: enable (byte); callee pops 4; slot +0xb8; not executed in recorded runs).
// Enable: byte +0x70 = 1 and virtual +0x48(+0x6c, 0). Disable: virtual +0x48(0, 0) with +0x6c preserved around the
// call, then byte +0x70 = 0. Returns the virtual's eax.
uint32_t fn_004f4430(uint8_t* self, uint32_t enable)
{
    if (static_cast<uint8_t>(enable) != 0)
    {
        const uint32_t value = Field<uint32_t>(self, 0x6c);
        Field<uint8_t>(self, 0x70) = 1;
        return VCall<uint32_t>(self, 0x48 / 4, value, 0u);
    }
    const uint32_t saved = Field<uint32_t>(self, 0x6c);
    const uint32_t r = VCall<uint32_t>(self, 0x48 / 4, 0u, 0u);
    Field<uint32_t>(self, 0x6c) = saved;
    Field<uint8_t>(self, 0x70) = 0;
    return r;
}
static uint32_t fn_004f4430_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f4430(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f4430(fn_004f4430_Entry, fn_004f4430_Abi)
HOOK(0x004f4430, "fn_004f4430", fn_004f4430_Entry, nullptr);

// 0x004f4470 (ecx = sound system; stack: enable (byte); callee pops 4; slot +0xbc; not executed in recorded runs).
// Enable: byte +0x71 = 1. Disable: for every voice whose definition flags have 4, +0x38 = +0x3c = 0 and virtual
// +0x44(voice); byte +0x71 = 0. Returns the iterator chunk (eax is not meaningful on the enable path: 0 here).
uint32_t fn_004f4470(uint8_t* self, uint32_t enable)
{
    if (static_cast<uint8_t>(enable) != 0)
    {
        Field<uint8_t>(self, 0x71) = 1;
        return 0;
    }
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint8_t>(Field<void*>(P(v), 0xc), 0x20) & 4)
        {
            Field<uint32_t>(P(v), 0x38) = 0;
            Field<uint32_t>(P(v), 0x3c) = 0;
            VCall<uint32_t>(self, 0x44 / 4, v);
        }
    }
    Field<uint8_t>(self, 0x71) = 0;
    return IterEnd(&it);
}
static uint32_t fn_004f4470_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f4470(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f4470(fn_004f4470_Entry, fn_004f4470_Abi)
HOOK(0x004f4470, "fn_004f4470", fn_004f4470_Entry, nullptr);

// 0x004f4520 (ecx = sound system; slot +0xb4; not executed in recorded runs): tail jump to virtual
// +0x54 (0x00435250 in the PC vtable: `xor al, al; ret`, no stack arguments): reproduced as call-and-return.
uint32_t fn_004f4520(uint8_t* self)
{
    return VCall<uint32_t>(self, 0x54 / 4);
}
static uint32_t fn_004f4520_Abi(uint32_t ecx)
{
    return fn_004f4520(reinterpret_cast<uint8_t*>(ecx));
}
AUDIO02_ENTRY_ECX(fn_004f4520_Entry, fn_004f4520_Abi)
HOOK(0x004f4520, "fn_004f4520", fn_004f4520_Entry, nullptr);

// 0x004f4530 (ecx = sound system; slot +0xb0; not executed in recorded runs): tail jump to virtual +0x50
// (0x00433fa0 in the PC vtable: `mov al, 1; ret`): reproduced as call-and-return.
uint32_t fn_004f4530(uint8_t* self)
{
    return VCall<uint32_t>(self, 0x50 / 4);
}
static uint32_t fn_004f4530_Abi(uint32_t ecx)
{
    return fn_004f4530(reinterpret_cast<uint8_t*>(ecx));
}
AUDIO02_ENTRY_ECX(fn_004f4530_Entry, fn_004f4530_Abi)
HOOK(0x004f4530, "fn_004f4530", fn_004f4530_Entry, nullptr);

// 0x004f4540 (ecx = sound system; stack: handle, offset; callee pops 8; slot +0xf8; executed in recorded runs). Sets a
// voice's volume offset: the voice with this handle gets +0x14 = offset and virtual +0x40(voice, virtual
// +0x10(voice)) (the iteration then ends normally). Returns the iterator chunk (0 for handle 0).
uint32_t fn_004f4540(uint8_t* self, uint32_t handle, uint32_t offset)
{
    if (handle == 0)
        return 0;
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 4) == handle)
        {
            Field<uint32_t>(P(v), 0x14) = offset;
            const float volume = VCall<float>(self, 0x10 / 4, v);
            VCall<uint32_t>(self, 0x40 / 4, v, volume);
            break;
        }
    }
    return IterEnd(&it);
}
static uint32_t fn_004f4540_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f4540(reinterpret_cast<uint8_t*>(ecx), a0, a1);
}
ABI_ENTRY_004f4540(fn_004f4540_Entry, fn_004f4540_Abi)
HOOK(0x004f4540, "fn_004f4540", fn_004f4540_Entry, nullptr);

// 0x004f45e0 (ecx = sound system; stack: handle; callee pops 4; slot +0xec; not executed in recorded runs). For the
// voice with this handle whose definition flags have 0x40: returns its +0x88; otherwise (or no such voice, or handle
// 0) returns 0. The search stops at the first voice with the handle.
uint32_t fn_004f45e0(uint8_t* self, uint32_t handle)
{
    if (handle == 0)
        return 0;
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 4) == handle)
        {
            if ((Field<uint8_t>(Field<void*>(P(v), 0xc), 0x20) & 0x40) == 0)
                break;
            const uint32_t r = Field<uint32_t>(P(v), 0x88);
            IterEnd(&it);
            return r;
        }
    }
    IterEnd(&it);
    return 0;
}
static uint32_t fn_004f45e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f45e0(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f45e0(fn_004f45e0_Entry, fn_004f45e0_Abi)
HOOK(0x004f45e0, "fn_004f45e0", fn_004f45e0_Entry, nullptr);

// 0x004f46b0 (ecx = sound system; stack: handle, frequency; callee pops 8; slot +0xf0; not executed in recorded
// runs). The voice with this handle gets +0x1c = frequency and virtual +0x24(voice, f) with f = frequency for
// definitions with flags 0x2000000, else _ftol((float)frequency * +0x14 + K(0x005bcb4c)). Returns the iterator chunk.
uint32_t fn_004f46b0(uint8_t* self, uint32_t handle, uint32_t frequency)
{
    if (handle == 0)
        return 0;
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 4) == handle)
        {
            Field<uint32_t>(P(v), 0x1c) = frequency;
            uint32_t f = frequency;
            if ((Field<uint32_t>(Field<void*>(P(v), 0xc), 0x20) & 0x2000000) == 0)
            {
                const float x =
                    static_cast<float>(static_cast<int32_t>(frequency)) * Field<float>(self, 0x14) + K(0x005bcb4c);
                f = static_cast<uint32_t>(static_cast<int64_t>(x));
            }
            VCall<uint32_t>(self, 0x24 / 4, v, f);
            break;
        }
    }
    return IterEnd(&it);
}
static uint32_t fn_004f46b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f46b0(reinterpret_cast<uint8_t*>(ecx), a0, a1);
}
ABI_ENTRY_004f46b0(fn_004f46b0_Entry, fn_004f46b0_Abi)
HOOK(0x004f46b0, "fn_004f46b0", fn_004f46b0_Entry, nullptr);

// 0x004f4760 (ecx = sound system; stack: handle, delta; callee pops 8; slot +0xf4; not executed in recorded runs).
// Pitch shift (guess): the voice with this handle gets +0x1c = +0x18 + delta and virtual +0x24(voice, f) with f as in
// 0x004f46b0 (computed from the new +0x1c). Returns the iterator chunk (0 for handle 0).
uint32_t fn_004f4760(uint8_t* self, uint32_t handle, uint32_t delta)
{
    if (handle == 0)
        return 0;
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 4) == handle)
        {
            const uint32_t frequency = Field<uint32_t>(P(v), 0x18) + delta;
            Field<uint32_t>(P(v), 0x1c) = frequency;
            uint32_t f = frequency;
            if ((Field<uint32_t>(Field<void*>(P(v), 0xc), 0x20) & 0x2000000) == 0)
            {
                const float x =
                    static_cast<float>(static_cast<int32_t>(frequency)) * Field<float>(self, 0x14) + K(0x005bcb4c);
                f = static_cast<uint32_t>(static_cast<int64_t>(x));
            }
            VCall<uint32_t>(self, 0x24 / 4, v, f);
            break;
        }
    }
    return IterEnd(&it);
}
static uint32_t fn_004f4760_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f4760(reinterpret_cast<uint8_t*>(ecx), a0, a1);
}
ABI_ENTRY_004f4760(fn_004f4760_Entry, fn_004f4760_Abi)
HOOK(0x004f4760, "fn_004f4760", fn_004f4760_Entry, nullptr);

namespace
{
// fild of a dword read as unsigned (the originals add 2^32 (K 0x005bcb28) after fild when the sign bit is set; that
// fadd rounds to single precision, the plain fild is exact).
double UnsignedFild(uint32_t x)
{
    if (static_cast<int32_t>(x) >= 0)
        return static_cast<double>(static_cast<int32_t>(x));
    return static_cast<double>(static_cast<float>(static_cast<int32_t>(x)) + K(0x005bcb28));
}
}  // namespace

// 0x004f4820 (ecx = sound system; stack: name, index; callee pops 8; returns st0; slot +0x100; not executed in recorded
// runs). Duration of a definition (guess: samples / rate): def+0xf4 / def+0xe0, both as unsigned; 0.0 (K 0x0056c05c)
// without a table or definition, and after the "Audio : %s not found" log when the index is out of range.
float fn_004f4820(uint8_t* self, uint32_t name, uint32_t index)
{
    if (Field<uint32_t>(self, 0x80) == 0)
        return K(0x0056c05c);
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
        return K(0x0056c05c);
    }
    const uint8_t* def = Field<uint8_t**>(self, 0x80)[index];
    if (def == nullptr)
        return K(0x0056c05c);
    return static_cast<float>(UnsignedFild(Field<uint32_t>(def, 0xf4)) / UnsignedFild(Field<uint32_t>(def, 0xe0)));
}
static float fn_004f4820_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f4820(reinterpret_cast<uint8_t*>(ecx), a0, a1);
}
ABI_ENTRY_004f4820(fn_004f4820_Entry, fn_004f4820_Abi)
HOOK(0x004f4820, "fn_004f4820", fn_004f4820_Entry, nullptr);

// 0x004f48d0 (stack: voice; callee pops 4; returns st0 (symbols/abi.csv says eax: the stub leaves st0 alone); slot +0x08 (ecx is not read); not executed in recorded runs):
// voice duration: def+0xf4 (unsigned) / voice+0x1c (fidiv, signed).
float fn_004f48d0(const uint8_t* voice)
{
    const uint8_t* def = Field<uint8_t*>(voice, 0xc);
    return static_cast<float>(UnsignedFild(Field<uint32_t>(def, 0xf4)) /
                              static_cast<double>(Field<int32_t>(voice, 0x1c)));
}
static float fn_004f48d0_Abi(uint32_t a0)
{
    return fn_004f48d0(reinterpret_cast<const uint8_t*>(a0));
}
ABI_ENTRY_004f48d0(fn_004f48d0_Entry, fn_004f48d0_Abi)
HOOK(0x004f48d0, "fn_004f48d0", fn_004f48d0_Entry, nullptr);

// 0x004f4900 (ecx = sound system; stack: handle; callee pops 4; returns st0; slot +0xfc; not executed in recorded
// runs): virtual +0x08(voice) (0x004f48d0) for the voice with this handle, else 0.0.
float fn_004f4900(uint8_t* self, uint32_t handle)
{
    if (handle == 0)
        return K(0x0056c05c);
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 4) == handle)
        {
            const float r = VCall<float>(self, 0x08 / 4, v);
            IterEnd(&it);
            return r;
        }
    }
    IterEnd(&it);
    return K(0x0056c05c);
}
static float fn_004f4900_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f4900(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f4900(fn_004f4900_Entry, fn_004f4900_Abi)
HOOK(0x004f4900, "fn_004f4900", fn_004f4900_Entry, nullptr);

// 0x004f49b0 (ecx = sound system; stack: path, definition, index; callee pops 12; slot +0x78; not executed in
// recorded runs). Load a .wav definition (strings below). A local wave file object (0x1bc bytes, WaveFile_Init
// 0x00500f10) is set up; every exit closes its file handle (+0xa8) with CloseHandle through the import slot
// 0x0056b084. Fails (false, log level 4 with the path) when there is no table ("Loading audio definition before
// system is initialized"), table[index] is set ("...that is already loaded"), WaveFile_Open (0x00500ff0) fails ("Can
// not open audio resource"), or def+0xdc is null ("Invalid audio resource format"; never in practice). Reads min(fmt
// size, 0x14) bytes of the 'fmt ' chunk to def+0xdc (RiffChunk_Read 0x00500ed0) and zero-fills the rest of the 0x14
// bytes; def+0xf0 = data size. Stream definitions (def flags 0x40) keep a copy of the path in def+0xf8 ("stream name";
// original bug: the buffer is strlen(path) bytes and the copy writes the terminator one byte past it); others load the
// data into def+0xf8 ("wav data", WaveFile_ReadData 0x00501100). def+0xf4 = sample count: size * 8 / bits / channels,
// or size * 64 / (channels * 36) when def+0xec (cbSize) is non-zero (guess: IMA ADPCM). table[index] = def; true.
// The stack cookie and SEH frame are not reproduced.
uint32_t fn_004f49b0(uint8_t* self, const char* path, uint8_t* def, uint32_t index)
{
    alignas(4) uint8_t wave[0x1bc];
    orig::fn_00500f10(u(wave));
    const auto close = [&wave]() {
        reinterpret_cast<uint32_t(__stdcall*)(uint32_t)>(GameGlobal<uint32_t>(0x0056b084))(Field<uint32_t>(wave, 0xa8));
    };
    if (Field<uint32_t>(self, 0x80) == 0)
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 4, 0x0058acc8u, u(path));
        close();
        return 0;
    }
    if (Field<uint32_t*>(self, 0x80)[index] != 0)
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 4, 0x0058ac94u, u(path));
        close();
        return 0;
    }
    if (static_cast<uint8_t>(orig::fn_00500ff0(u(wave), u(path))) == 0)
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 4, 0x0058ac74u, u(path));
        close();
        return 0;
    }
    const uint32_t fmtSize = Field<uint32_t>(wave, 0x24);
    uint8_t* format = def + 0xdc;
    if (format == nullptr)
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 4, 0x0058ac50u, u(path));
        close();
        return 0;
    }
    orig::fn_00500ed0(u(wave + 0x18), 0, fmtSize > 0x14 ? 0x14u : fmtSize, u(format));
    if (fmtSize < 0x14)
        std::memset(format + fmtSize, 0, 0x14 - fmtSize);
    const uint32_t dataSize = Field<uint32_t>(wave, 0x3c);
    Field<uint32_t>(def, 0xf0) = dataSize;
    if (Field<uint8_t>(def, 0x20) & 0x40)
    {
        const uint32_t length = static_cast<uint32_t>(std::strlen(path));
        char* copy = reinterpret_cast<char*>(orig::fn_004e07f0(kHeap, 2, length, 2, 0x0058ac44));
        Field<uint32_t>(def, 0xf8) = u(copy);
        std::memcpy(copy, path, length + 1);
    }
    else
    {
        const uint32_t data = orig::fn_004e07f0(kHeap, 2, dataSize, 2, 0x0058ac38);
        Field<uint32_t>(def, 0xf8) = data;
        orig::fn_00501100(Field<uint32_t>(def, 0xf0), 0, u(wave), 0, data);
    }
    uint32_t samples;
    if (Field<uint16_t>(def, 0xec) == 0)
        samples = Field<uint32_t>(def, 0xf0) * 8 / Field<uint16_t>(def, 0xea) / Field<uint16_t>(def, 0xde);
    else
        samples = (Field<uint32_t>(def, 0xf0) << 6) / (static_cast<uint32_t>(Field<uint16_t>(def, 0xde)) * 9 * 4);
    Field<uint32_t>(def, 0xf4) = samples;
    Field<uint32_t*>(self, 0x80)[index] = u(def);
    close();
    return 1;
}
static uint32_t fn_004f49b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004f49b0(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<const char*>(a0), reinterpret_cast<uint8_t*>(a1),
                       a2);
}
ABI_ENTRY_004f49b0(fn_004f49b0_Entry, fn_004f49b0_Abi)
HOOK(0x004f49b0, "fn_004f49b0", fn_004f49b0_Entry, nullptr);

// 0x004f4c50 (ecx = sound system; stack: index; callee pops 4; base vtable 0x0058aa40 +0x7c (the PC override
// 0x004fab40 calls it); executed in recorded runs). Unload definition `index`: with a table and an entry, unless byte
// +0x7c is set its data +0xf8 is freed (PMS_heapFree) and cleared; the table entry is cleared. Returns the last eax
// (approximately).
uint32_t fn_004f4c50(uint8_t* self, uint32_t index)
{
    uint32_t* table = Field<uint32_t*>(self, 0x80);
    if (table == nullptr)
        return 0;
    const uint32_t def = table[index];
    if (def == 0)
        return 0;
    if (Field<uint8_t>(self, 0x7c) != 0)
    {
        table[index] = 0;
        return u(table);
    }
    const uint32_t data = Field<uint32_t>(P(def), 0xf8);
    if (data != 0)
    {
        HeapFree(data);
        Field<uint32_t>(P(Field<uint32_t*>(self, 0x80)[index]), 0xf8) = 0;
    }
    Field<uint32_t*>(self, 0x80)[index] = 0;
    return data != 0 ? u(Field<uint32_t*>(self, 0x80)) : 0;
}
static uint32_t fn_004f4c50_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f4c50(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f4c50(fn_004f4c50_Entry, fn_004f4c50_Abi)
HOOK(0x004f4c50, "fn_004f4c50", fn_004f4c50_Entry, nullptr);

// 0x004f4cf0 (ecx = sound system; stack: name, index; callee pops 8; slot +0x104; not executed in recorded runs). The
// definition by name or index (0 without a table; out of range logs "Audio : %s not found" and returns the log's eax,
// eax having been cleared before the call).
uint32_t fn_004f4cf0(uint8_t* self, uint32_t name, uint32_t index)
{
    if (Field<uint32_t>(self, 0x80) == 0)
        return 0;
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
        return orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
    return Field<uint32_t*>(self, 0x80)[index];
}
static uint32_t fn_004f4cf0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f4cf0(reinterpret_cast<uint8_t*>(ecx), a0, a1);
}
ABI_ENTRY_004f4cf0(fn_004f4cf0_Entry, fn_004f4cf0_Abi)
HOOK(0x004f4cf0, "fn_004f4cf0", fn_004f4cf0_Entry, nullptr);

// 0x004f4d60 (ecx = sound system; slot +0x108; not executed in recorded runs): returns +0x78 - 1 (last definition
// index).
uint32_t fn_004f4d60(uint8_t* self)
{
    return Field<uint32_t>(self, 0x78) - 1;
}
static uint32_t fn_004f4d60_Abi(uint32_t ecx)
{
    return fn_004f4d60(reinterpret_cast<uint8_t*>(ecx));
}
ABI_ENTRY_004f4d60(fn_004f4d60_Entry, fn_004f4d60_Abi)
HOOK(0x004f4d60, "fn_004f4d60", fn_004f4d60_Entry, nullptr);

// 0x004f4d70 (ecx = sound system; stack: handle; callee pops 4; returns st0; slot +0x110; not executed in recorded
// runs): the float def+0x28 of the voice with this handle (guess: base volume), else 0.0.
float fn_004f4d70(uint8_t* self, uint32_t handle)
{
    if (handle == 0)
        return K(0x0056c05c);
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        if (Field<uint32_t>(P(v), 4) == handle)
        {
            const float r = Field<float>(Field<void*>(P(v), 0xc), 0x28);
            IterEnd(&it);
            return r;
        }
    }
    IterEnd(&it);
    return K(0x0056c05c);
}
static float fn_004f4d70_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f4d70(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f4d70(fn_004f4d70_Entry, fn_004f4d70_Abi)
HOOK(0x004f4d70, "fn_004f4d70", fn_004f4d70_Entry, nullptr);

// 0x004f4e40 (ecx = sound system; stack: name, index; callee pops 8; returns st0; slot +0x10c; not executed in
// recorded runs): def+0x28 of the definition by name or index, else 0.0 (after the "not found" log when out of range).
float fn_004f4e40(uint8_t* self, uint32_t name, uint32_t index)
{
    if (Field<uint32_t>(self, 0x80) == 0)
        return K(0x0056c05c);
    if (name != 0)
        index = LookupIndex(self, name);
    if (index >= Field<uint32_t>(self, 0x78))
    {
        orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058ad28u, name);
        return K(0x0056c05c);
    }
    const uint8_t* def = Field<uint8_t**>(self, 0x80)[index];
    if (def == nullptr)
        return K(0x0056c05c);
    return Field<float>(def, 0x28);
}
static float fn_004f4e40_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f4e40(reinterpret_cast<uint8_t*>(ecx), a0, a1);
}
ABI_ENTRY_004f4e40(fn_004f4e40_Entry, fn_004f4e40_Abi)
HOOK(0x004f4e40, "fn_004f4e40", fn_004f4e40_Entry, nullptr);

// 0x004f4ec0 (stack: x, lo, hi (floats); callee pops 12; returns st0; slot +0xac, ecx not read; caller 0x004f4260
// and the other level setters; not executed in recorded runs). Level curve: 0.0 when x <= lo (unordered goes on),
// K(0x005bcb64) when x >= hi; otherwise a cubic: 0x0046d630 (eax = local float[4]; 0, -5, -10, -40) fills the
// coefficients c and with t = (x - lo) / (hi - lo) the result is ((c0 * t + c1) * t + c2) * t + c3.
float fn_004f4ec0(float x, float lo, float hi)
{
    if (x < lo || x == lo)
        return K(0x0056c05c);
    if (!(x < hi) && !(x != x) && !(hi != hi))
        return K(0x005bcb64);
    float c[4];
    orig::fn_0046d630(u(c), 0, 0xc0a00000u, 0xc1200000u, 0xc2200000u);
    const float t = (x - lo) / (hi - lo);
    float r = c[0] * t + c[1];
    r = r * t + c[2];
    r = r * t;
    return r + c[3];
}
static float fn_004f4ec0_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004f4ec0(ArgFloat(a0), ArgFloat(a1), ArgFloat(a2));
}
ABI_ENTRY_004f4ec0(fn_004f4ec0_Entry, fn_004f4ec0_Abi)
HOOK(0x004f4ec0, "fn_004f4ec0", fn_004f4ec0_Entry, nullptr);

// 0x004f4f50 (ecx = sound system; stack: callback; callee pops 4; slot +0x114; not executed in recorded runs). Calls
// callback(voice+8) (cdecl) for every voice with a non-zero +8; a null callback falls back to +0x5c, and nothing
// happens when that is null too. Returns the iterator chunk.
uint32_t fn_004f4f50(uint8_t* self, uint32_t callback)
{
    if (callback == 0)
    {
        callback = Field<uint32_t>(self, 0x5c);
        if (callback == 0)
            return 0;
    }
    Iter it = {0, 0};
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
        const uint32_t object = Field<uint32_t>(P(v), 8);
        if (object != 0)
            reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(callback)(object);
    }
    return IterEnd(&it);
}
static uint32_t fn_004f4f50_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f4f50(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f4f50(fn_004f4f50_Entry, fn_004f4f50_Abi)
HOOK(0x004f4f50, "fn_004f4f50", fn_004f4f50_Entry, nullptr);

// 0x004f4fe0 (stack: definition; callee pops 4; slot +0x138, ecx not read; called by AudioDef_Load; not executed in
// recorded runs). Definition defaults: zeroes 0x1a0 bytes, then +0x28 = 0, +0x30 = 20.0, +0x34 = 500.0, +0x24 = 15.
// Returns 0.
uint32_t fn_004f4fe0(uint8_t* def)
{
    std::memset(def, 0, 0x68 * 4);
    Field<uint32_t>(def, 0x28) = 0;
    Field<uint32_t>(def, 0x30) = 0x41a00000;
    Field<uint32_t>(def, 0x34) = 0x43fa0000;
    Field<uint32_t>(def, 0x24) = 0xf;
    return 0;
}
static uint32_t fn_004f4fe0_Abi(uint32_t a0)
{
    return fn_004f4fe0(reinterpret_cast<uint8_t*>(a0));
}
ABI_ENTRY_004f4fe0(fn_004f4fe0_Entry, fn_004f4fe0_Abi)
HOOK(0x004f4fe0, "fn_004f4fe0", fn_004f4fe0_Entry, nullptr);

// 0x004f5010 AudioDef_Load (ecx = sound system; stack: path, out; callee pops 8; slot +0x134; not executed in
// recorded runs). Loads a compiled sound definition file (strings "audio def file", "group list", "def list", "aud
// name list", "Wrong sound def file verison (%d), looking for %d", "Audio def file '%s' read error",
// "Unsupported Opcode (%d)"). out (0x57e64 bytes) is zeroed; the file is opened with 0x004f1c90 (esi = a local
// 0x108-byte file object, handle at +0) and read with ReadFile/CloseHandle through the import slots 0x0056b0c8 /
// 0x0056b084. Header: version (must be 1, else fprintf 0x0053ae47 to 0x005bf1a8 and false), out+0x57e58 (definition
// count), out+0x57e5c (group count), out+4 (byte-code size); out+0 = byte code (PMS_heapAlloc align 2). Read errors
// print the read-error message and return false (the byte code is freed when its read was short). Then out+8 = group
// records (0x1a0 each), out+0xc = definition records (0x1a0 each), out+0x10 = names (0x240 each), all zeroed. The byte
// code is a list of {op, size} words followed by operands; a local 0x1a0-byte record is filled:
//   1 nop; 2 end (returns true); 3 new definition (defaults via virtual +0x138, 32-byte name, group = -1);
//   4 store definition (adds the group's +0x28, +0x2c, +0x24 (int), +0x74, +0x78, +0x70, +0x30, +0x34 and ORs its
//     flags +0x20, then copies the record to the next definition slot); 5 +0x30/+0x34; 6 +0x28; 7 +0x2c; 8 skips 0x18;
//   9 flags |= 0x200000 and +0x38..+0x4c; 10 flags |= 0x400000 and +0x50..+0x6c; 11 flags |= operand; 14 +0x24;
//   15 flags |= 0x800000 and +0x70..+0x78; 16 select group by name (_stricmp over the groups stored so far);
//   17 new group (zeroed record, 32-byte name); 18 store group; 19 flags |= 0x1000000, two 32-byte strings at +0x7c,
//   +0x9c; 20 a list of 32-byte entries appended to out+0x14 (at most 10000, count out+0x57e54; the definition's first
//   entry index goes to out+0x4e214 + 4i; the list count is kept in the record at +0x17c, as the original's frame
//   does); 21 the file name (size bytes) to names+0, its base name without extension to +0x200 and its directory to
//   +0x100 ("." when none); 22 flags |= 0x8000000; 23 flags |= 0x10000000 and a 32-byte string at +0xbc;
//   12, 13 and others: Debug_Printf(Game+8, "Unsupported Opcode (%d)", op) (original bug: called with 3 arguments, so
//   the format is passed as the level) and the operands are skipped (original bug: by ecx after the call, which the
//   call clobbers; reproduced with the operand size).
// The stack cookie is not reproduced.
uint32_t AudioDef_Load(uint8_t* self, const char* path, uint8_t* out)
{
    typedef int(__stdcall * ReadFileFn)(uint32_t, void*, uint32_t, uint32_t*, void*);
    typedef int(__stdcall * CloseHandleFn)(uint32_t);
    typedef int(__cdecl * FprintfFn)(uint32_t, uint32_t, ...);
    const ReadFileFn readFile = reinterpret_cast<ReadFileFn>(GameGlobal<uint32_t>(0x0056b0c8));
    const CloseHandleFn closeHandle = reinterpret_cast<CloseHandleFn>(GameGlobal<uint32_t>(0x0056b084));
    const FprintfFn fprintf = reinterpret_cast<FprintfFn>(0x0053ae47);
    alignas(8) uint8_t file[0x108];
    alignas(4) uint8_t rec[0x1a0];
    char groupName[32];
    uint32_t read = 0;
    uint32_t version = 0;
    std::memset(out, 0, 0x15f99 * 4);
    if (static_cast<uint8_t>(orig::fn_004f1c90(u(file), u(path))) == 0)
        return 0;
    const uint32_t h = Field<uint32_t>(file, 0);
    readFile(h, &version, 4, &read, nullptr);
    if (read != 4)
    {
        fprintf(0x005bf1a8, 0x0058ac18, path);
        closeHandle(Field<uint32_t>(file, 0));
        return 0;
    }
    if (version != 1)
    {
        fprintf(0x005bf1a8, 0x0058abe4, version, 1);
        closeHandle(Field<uint32_t>(file, 0));
        return 0;
    }
    const uint32_t offsets[3] = {0x57e58, 0x57e5c, 4};
    for (uint32_t k = 0; k < 3; ++k)
    {
        readFile(Field<uint32_t>(file, 0), out + offsets[k], 4, &read, nullptr);
        if (read != 4)
        {
            fprintf(0x005bf1a8, 0x0058ac18, path);
            closeHandle(Field<uint32_t>(file, 0));
            return 0;
        }
    }
    const uint32_t code = orig::fn_004e07f0(kHeap, 2, Field<uint32_t>(out, 4), 2, 0x0058abd4);
    Field<uint32_t>(out, 0) = code;
    readFile(Field<uint32_t>(file, 0), P(code), Field<uint32_t>(out, 4), &read, nullptr);
    if (read != Field<uint32_t>(out, 4))
    {
        if (Field<uint32_t>(out, 0) != 0)
        {
            HeapFree(Field<uint32_t>(out, 0));
            Field<uint32_t>(out, 0) = 0;
        }
        closeHandle(Field<uint32_t>(file, 0));
        fprintf(0x005bf1a8, 0x0058ac18, path);
        return 0;
    }
    closeHandle(Field<uint32_t>(file, 0));
    uint32_t size = Field<uint32_t>(out, 0x57e5c) * 0x1a0;
    Field<uint32_t>(out, 8) = orig::fn_004e07f0(kHeap, 2, size, 2, 0x0058abc8);
    std::memset(P(Field<uint32_t>(out, 8)), 0, Field<uint32_t>(out, 0x57e5c) * 0x1a0);
    size = Field<uint32_t>(out, 0x57e58) * 0x1a0;
    Field<uint32_t>(out, 0xc) = orig::fn_004e07f0(kHeap, 2, size, 2, 0x0058abbc);
    std::memset(P(Field<uint32_t>(out, 0xc)), 0, Field<uint32_t>(out, 0x57e58) * 0x1a0);
    size = Field<uint32_t>(out, 0x57e58) * 0x240;
    Field<uint32_t>(out, 0x10) = orig::fn_004e07f0(kHeap, 2, size, 2, 0x0058abac);
    std::memset(P(Field<uint32_t>(out, 0x10)), 0, Field<uint32_t>(out, 0x57e58) * 0x240);

    const uint8_t* pc = reinterpret_cast<const uint8_t*>(Field<uint32_t>(out, 0));
    uint32_t nameOff = 0;
    uint32_t groupCount = 0;
    uint32_t defOff = 0;
    uint32_t groupOff = 0;
    uint32_t* firstEntry = reinterpret_cast<uint32_t*>(out + 0x4e214);
    uint32_t group = read;  // the original's group index shares the ReadFile count slot
    for (;;)
    {
        const uint32_t op = Field<uint32_t>(pc, 0);
        const uint32_t opSize = Field<uint32_t>(pc, 4);
        pc += 8;
        switch (op)
        {
        case 1:
            break;
        case 2:
            return 1;
        case 3:
            VCall<uint32_t>(self, 0x138 / 4, u(rec));
            std::memcpy(rec, pc, 0x20);
            pc += 0x20;
            group = 0xffffffffu;
            break;
        case 4:
        {
            if (group != 0xffffffffu)
            {
                const uint8_t* g = reinterpret_cast<const uint8_t*>(Field<uint32_t>(out, 8)) + group * 0x1a0;
                Field<float>(rec, 0x28) = Field<float>(rec, 0x28) + Field<float>(g, 0x28);
                Field<float>(rec, 0x2c) = Field<float>(rec, 0x2c) + Field<float>(g, 0x2c);
                Field<uint32_t>(rec, 0x24) = Field<uint32_t>(rec, 0x24) + Field<uint32_t>(g, 0x24);
                Field<float>(rec, 0x74) = Field<float>(rec, 0x74) + Field<float>(g, 0x74);
                Field<float>(rec, 0x78) = Field<float>(rec, 0x78) + Field<float>(g, 0x78);
                Field<float>(rec, 0x70) = Field<float>(rec, 0x70) + Field<float>(g, 0x70);
                Field<float>(rec, 0x30) = Field<float>(rec, 0x30) + Field<float>(g, 0x30);
                Field<float>(rec, 0x34) = Field<float>(rec, 0x34) + Field<float>(g, 0x34);
                Field<uint32_t>(rec, 0x20) |= Field<uint32_t>(g, 0x20);
            }
            std::memcpy(reinterpret_cast<uint8_t*>(Field<uint32_t>(out, 0xc)) + defOff, rec, 0x1a0);
            ++firstEntry;
            defOff += 0x1a0;
            nameOff += 0x240;
            break;
        }
        case 5:
            std::memcpy(rec + 0x30, pc, 8);
            pc += 8;
            break;
        case 6:
            std::memcpy(rec + 0x28, pc, 4);
            pc += 4;
            break;
        case 7:
            std::memcpy(rec + 0x2c, pc, 4);
            pc += 4;
            break;
        case 8:
            pc += 0x18;
            break;
        case 9:
            Field<uint32_t>(rec, 0x20) |= 0x200000;
            std::memcpy(rec + 0x38, pc, 0x18);
            pc += 0x18;
            break;
        case 10:
            Field<uint32_t>(rec, 0x20) |= 0x400000;
            std::memcpy(rec + 0x50, pc, 0x20);
            pc += 0x20;
            break;
        case 11:
            Field<uint32_t>(rec, 0x20) |= Field<uint32_t>(pc, 0);
            pc += 4;
            break;
        case 14:
            std::memcpy(rec + 0x24, pc, 4);
            pc += 4;
            break;
        case 15:
            Field<uint32_t>(rec, 0x20) |= 0x800000;
            std::memcpy(rec + 0x70, pc, 0xc);
            pc += 0xc;
            break;
        case 16:
        {
            std::memcpy(groupName, pc, 0x20);
            pc += 0x20;
            if (groupOff == 0)
                break;
            for (uint32_t i = 0; i < groupCount; ++i)
            {
                if (Stricmp(Field<uint32_t>(out, 8) + i * 0x1a0, u(groupName)) == 0)
                {
                    group = i;
                    break;
                }
            }
            break;
        }
        case 17:
            std::memset(rec, 0, 0x1a0);
            std::memcpy(rec, pc, 0x20);
            pc += 0x20;
            break;
        case 18:
            std::memcpy(reinterpret_cast<uint8_t*>(Field<uint32_t>(out, 8)) + groupOff, rec, 0x1a0);
            ++groupCount;
            groupOff += 0x1a0;
            break;
        case 19:
            Field<uint32_t>(rec, 0x20) |= 0x1000000;
            std::memcpy(rec + 0x7c, pc, 0x20);
            std::memcpy(rec + 0x9c, pc + 0x20, 0x20);
            pc += 0x40;
            break;
        case 20:
        {
            const uint32_t n = Field<uint32_t>(pc, 0);
            *firstEntry = Field<uint32_t>(out, 0x57e54);
            pc += 4;
            Field<uint32_t>(rec, 0x17c) = n;
            for (uint32_t i = 0; i < Field<uint32_t>(rec, 0x17c); ++i)
            {
                const uint32_t count = Field<uint32_t>(out, 0x57e54);
                if (count < 10000)
                {
                    std::memcpy(out + 0x14 + count * 0x20, pc, 0x20);
                    pc += 0x20;
                    Field<uint32_t>(out, 0x57e54) = count + 1;
                }
            }
            break;
        }
        case 21:
        {
            char* name = reinterpret_cast<char*>(Field<uint32_t>(out, 0x10)) + nameOff;
            std::memcpy(name, pc, opSize);
            pc += opSize;
            const char* slash = reinterpret_cast<const char*>(GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t)>(
                0x00539ee0)(u(name), 0x5c));
            std::strcpy(name + 0x200, slash ? slash + 1 : name);
            char* dot = reinterpret_cast<char*>(GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t)>(0x00539ee0)(
                u(name + 0x200), 0x2e));
            if (dot)
                *dot = 0;
            std::strcpy(name + 0x100, name);
            char* dir = reinterpret_cast<char*>(GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t)>(0x00539ee0)(
                u(name + 0x100), 0x5c));
            if (dir)
                *dir = 0;
            else
                Field<uint16_t>(name, 0x100) = 0x2e;
            break;
        }
        case 22:
            Field<uint32_t>(rec, 0x20) |= 0x8000000;
            break;
        case 23:
            Field<uint32_t>(rec, 0x20) |= 0x10000000;
            std::memcpy(rec + 0xbc, pc, 0x20);
            pc += 0x20;
            break;
        default:
            GameFunction<void(__cdecl*)(uint32_t, uint32_t, uint32_t)>(0x00407ff0)(Field<uint32_t>(self, 8), 0x0058ab94,
                                                                                op);
            pc += opSize;
            break;
        }
    }
}
static uint32_t AudioDef_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return AudioDef_Load(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<const char*>(a0), reinterpret_cast<uint8_t*>(a1));
}
ABI_ENTRY_004f5010(AudioDef_Load_Entry, AudioDef_Load_Abi)
HOOK(0x004f5010, "AudioDef_Load", AudioDef_Load_Entry, nullptr);

// 0x004f5820 (ecx = sound system; stack: no position (byte), &position, a2; callee pops 12; slot +0x13c; not executed
// in recorded runs): AmbientSounds_Add (0x005002e0, bl = no position; +0x74, &position, a2). Only bl is set by the
// original (the upper ebx bytes are its caller's); they are passed as 0 here.
uint32_t fn_004f5820(uint8_t* self, uint32_t noPosition, uint32_t position, uint32_t a2)
{
    return orig::fn_005002e0(noPosition & 0xff, Field<uint32_t>(self, 0x74), position, a2);
}
static uint32_t fn_004f5820_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004f5820(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2);
}
ABI_ENTRY_004f5820(fn_004f5820_Entry, fn_004f5820_Abi)
HOOK(0x004f5820, "fn_004f5820", fn_004f5820_Entry, nullptr);

// 0x004f5840 (ecx = sound system; stack: &id; callee pops 4; slot +0x140; not executed in recorded runs):
// AmbientSounds_Remove (0x00500370, ecx = +0x74, esi = &id).
uint32_t fn_004f5840(uint8_t* self, uint32_t id)
{
    return orig::fn_00500370(Field<uint32_t>(self, 0x74), id);
}
static uint32_t fn_004f5840_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f5840(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f5840(fn_004f5840_Entry, fn_004f5840_Abi)
HOOK(0x004f5840, "fn_004f5840", fn_004f5840_Entry, nullptr);

// 0x004f5860 (ecx = sound system; stack: id, name; callee pops 8; slot +0x144; not executed in recorded runs):
// AmbientSounds_AddLoop (0x005004a0, eax = +0x74, ecx = id; name).
uint32_t fn_004f5860(uint8_t* self, uint32_t id, uint32_t name)
{
    return orig::fn_005004a0(Field<uint32_t>(self, 0x74), id, name);
}
static uint32_t fn_004f5860_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f5860(reinterpret_cast<uint8_t*>(ecx), a0, a1);
}
ABI_ENTRY_004f5860(fn_004f5860_Entry, fn_004f5860_Abi)
HOOK(0x004f5860, "fn_004f5860", fn_004f5860_Entry, nullptr);

// 0x004f5880 (ecx = sound system; stack: id, name; callee pops 8; slot +0x148; not executed in recorded runs):
// AmbientSounds_AddOneShot (0x00500420, eax = +0x74, ecx = id; name).
uint32_t fn_004f5880(uint8_t* self, uint32_t id, uint32_t name)
{
    return orig::fn_00500420(Field<uint32_t>(self, 0x74), id, name);
}
static uint32_t fn_004f5880_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004f5880(reinterpret_cast<uint8_t*>(ecx), a0, a1);
}
ABI_ENTRY_004f5880(fn_004f5880_Entry, fn_004f5880_Abi)
HOOK(0x004f5880, "fn_004f5880", fn_004f5880_Entry, nullptr);

namespace
{
// The ambient element with this id (+8) in the circular list +0x74 (head = the list object, linked through +0);
// returns the element, or the head when absent (eax at those exits).
uint32_t AmbientFind(uint8_t* self, uint32_t id, bool* found)
{
    const uint32_t head = Field<uint32_t>(self, 0x74);
    *found = false;
    for (uint32_t e = Field<uint32_t>(P(head), 0); e != head; e = Field<uint32_t>(P(e), 0))
    {
        if (Field<uint32_t>(P(e), 8) == id)
        {
            *found = true;
            return e;
        }
    }
    return head;
}
}  // namespace

// 0x004f58a0 (ecx = sound system; stack: id, min, max; callee pops 12; slot +0x14c; not executed in recorded runs):
// the ambient element with this id gets the one-shot delay range +0x18 = min, +0x1c = max.
uint32_t fn_004f58a0(uint8_t* self, uint32_t id, uint32_t lo, uint32_t hi)
{
    bool found;
    const uint32_t e = AmbientFind(self, id, &found);
    if (found)
    {
        Field<uint32_t>(P(e), 0x18) = lo;
        Field<uint32_t>(P(e), 0x1c) = hi;
    }
    return e;
}
static uint32_t fn_004f58a0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004f58a0(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2);
}
ABI_ENTRY_004f58a0(fn_004f58a0_Entry, fn_004f58a0_Abi)
HOOK(0x004f58a0, "fn_004f58a0", fn_004f58a0_Entry, nullptr);

// 0x004f58d0 (ecx = sound system; stack: id, min, max; callee pops 12; slot +0x150; not executed in recorded runs):
// the ambient element with this id gets the distance range +0x8c = min, +0x90 = max.
uint32_t fn_004f58d0(uint8_t* self, uint32_t id, uint32_t lo, uint32_t hi)
{
    bool found;
    const uint32_t e = AmbientFind(self, id, &found);
    if (found)
    {
        Field<uint32_t>(P(e), 0x8c) = lo;
        Field<uint32_t>(P(e), 0x90) = hi;
    }
    return e;
}
static uint32_t fn_004f58d0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004f58d0(reinterpret_cast<uint8_t*>(ecx), a0, a1, a2);
}
ABI_ENTRY_004f58d0(fn_004f58d0_Entry, fn_004f58d0_Abi)
HOOK(0x004f58d0, "fn_004f58d0", fn_004f58d0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// The DirectSound implementation (vtable 0x0058a230, 0x128 bytes): +0x108 buffer table (one IDirectSoundBuffer per
// definition), +0x10c IDirectSound8, +0x110 primary buffer, +0x114 IDirectSound3DListener, +0x118, +0x11c, +0x120
// count of 3D voices, +0x124 reference count. COM calls go through the interfaces' own vtables (stdcall); method
// names follow the DirectSound 8 vtable layout.

uint32_t fn_004f9ca0(uint8_t* mem);
uint32_t fn_004f9d10(uint8_t* self);
uint32_t fn_004f9f70(uint8_t* self, const float* matrix, float dt);
uint32_t fn_004fa340(uint8_t* self, uint8_t* def, uint32_t index, uint32_t* voice, uint32_t extra);
uint32_t fn_004fa6d0(uint8_t* self, const uint8_t* def, uint32_t size);

namespace
{
template <class R, class... A>
R Com(uint32_t object, int slot, A... args)
{
    void** vt = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__stdcall*)(uint32_t, A...)>(vt[slot])(object, args...);
}
}  // namespace

// 0x004f9c10 (the ecx "input" is `push ecx` reserving a local; callers 0x004e03e0, 0x0050de70; executed in recorded
// runs). Acquire the sound system singleton 0x0063a0bc: created on first use (0x128 bytes, PMS_heapAlloc align 0x10,
// tag 0x0058b5e8; constructor 0x004f9ca0), then its reference count +0x124 += 1. Returns it.
uint32_t AudioPC_Acquire()
{
    uint32_t obj = GameGlobal<uint32_t>(0x0063a0bc);
    if (obj == 0)
    {
        const uint32_t p = HeapAlloc(0x128, 0x10, 0x0058b5e8);
        obj = p ? fn_004f9ca0(reinterpret_cast<uint8_t*>(p)) : 0;
        GameGlobal<uint32_t>(0x0063a0bc) = obj;
    }
    ++Field<int32_t>(P(obj), 0x124);
    return obj;
}
static uint32_t AudioPC_Acquire_Abi(uint32_t /*ecx*/)
{
    return AudioPC_Acquire();
}
ABI_ENTRY_004f9c10(AudioPC_Acquire_Entry, AudioPC_Acquire_Abi)
HOOK(0x004f9c10, "AudioPC_Acquire", AudioPC_Acquire_Entry, nullptr);

// 0x004f9c80 (no inputs; slot +0x70 of the PC vtable; not executed in recorded runs). Release the singleton: when
// --+0x124 reaches 0, virtual +0x68(1) (scalar deleting destructor 0x004f9ce0) and 0x0063a0bc = 0.
uint32_t AudioPC_Release()
{
    const uint32_t obj = GameGlobal<uint32_t>(0x0063a0bc);
    if (--Field<int32_t>(P(obj), 0x124) != 0)
        return obj;
    const uint32_t r = VCall<uint32_t>(P(obj), 0x68 / 4, 1u);
    GameGlobal<uint32_t>(0x0063a0bc) = 0;
    return r;
}
static uint32_t AudioPC_Release_Abi()
{
    return AudioPC_Release();
}
ABI_ENTRY_004f9c80(AudioPC_Release_Entry, AudioPC_Release_Abi)
HOOK(0x004f9c80, "AudioPC_Release", AudioPC_Release_Entry, nullptr);

// 0x004f9ca0 (eax = memory; the ecx input is not read (the base constructor's ecx "input" is a reserved local);
// caller 0x004f9c10; executed in recorded runs). PC sound system constructor: base constructor 0x004f20d0 (edi =
// memory), +0x124, +0x10c, +0x110, +0x114, +0x108 = 0, byte +0x11c = 0, vtable 0x0058a230. Returns the object.
uint32_t fn_004f9ca0(uint8_t* mem)
{
    fn_004f20d0(mem);
    Field<uint32_t>(mem, 0x124) = 0;
    Field<uint32_t>(mem, 0x10c) = 0;
    Field<uint32_t>(mem, 0x110) = 0;
    Field<uint32_t>(mem, 0x114) = 0;
    Field<uint32_t>(mem, 0x108) = 0;
    Field<uint8_t>(mem, 0x11c) = 0;
    Field<uint32_t>(mem, 0) = 0x0058a230;
    return u(mem);
}
static uint32_t fn_004f9ca0_Abi(uint32_t eax, uint32_t /*ecx*/)
{
    return fn_004f9ca0(reinterpret_cast<uint8_t*>(eax));
}
ABI_ENTRY_004f9ca0(fn_004f9ca0_Entry, fn_004f9ca0_Abi)
HOOK(0x004f9ca0, "fn_004f9ca0", fn_004f9ca0_Entry, nullptr);

// 0x004f9ce0 (ecx = object; stack: flags; callee pops 4; slot +0x68; not executed in recorded runs): scalar deleting
// destructor: 0x004f9d10(object), then with flags bit 0 PMS_heapFree. Returns the object.
uint32_t fn_004f9ce0(uint8_t* self, uint32_t flags)
{
    fn_004f9d10(self);
    if (flags & 1)
        HeapFree(u(self));
    return u(self);
}
static uint32_t fn_004f9ce0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f9ce0(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f9ce0(fn_004f9ce0_Entry, fn_004f9ce0_Abi)
HOOK(0x004f9ce0, "fn_004f9ce0", fn_004f9ce0_Entry, nullptr);

// 0x004f9d10 (stack: object; callee pops 4; caller 0x004f9ce0; not executed in recorded runs). PC destructor: vtable
// 0x0058a230, shutdown 0x004f2590, frees the buffer table +0x108, releases (IUnknown::Release, vt +8) the listener
// +0x114, the primary buffer +0x110 and IDirectSound8 +0x10c (each cleared), then the base destructor 0x004f22e0.
uint32_t fn_004f9d10(uint8_t* self)
{
    Field<uint32_t>(self, 0) = 0x0058a230;
    fn_004f2590(self);
    if (Field<uint32_t>(self, 0x108) != 0)
    {
        HeapFree(Field<uint32_t>(self, 0x108));
        Field<uint32_t>(self, 0x108) = 0;
    }
    if (Field<uint32_t>(self, 0x114) != 0)
        Com<uint32_t>(Field<uint32_t>(self, 0x114), 2);
    Field<uint32_t>(self, 0x114) = 0;
    if (Field<uint32_t>(self, 0x110) != 0)
        Com<uint32_t>(Field<uint32_t>(self, 0x110), 2);
    Field<uint32_t>(self, 0x110) = 0;
    if (Field<uint32_t>(self, 0x10c) != 0)
        Com<uint32_t>(Field<uint32_t>(self, 0x10c), 2);
    Field<uint32_t>(self, 0x10c) = 0;
    return fn_004f22e0(self);
}
static uint32_t fn_004f9d10_Abi(uint32_t a0)
{
    return fn_004f9d10(reinterpret_cast<uint8_t*>(a0));
}
ABI_ENTRY_004f9d10(fn_004f9d10_Entry, fn_004f9d10_Abi)
HOOK(0x004f9d10, "fn_004f9d10", fn_004f9d10_Entry, nullptr);

// 0x004f9dc0 (ecx = object; stack: factor (float); callee pops 4; slot +0x4c; not executed in recorded runs):
// listener +0x114 SetRolloffFactor(factor * K(0x005bcb68), DS3D_DEFERRED); returns the HRESULT. No null check.
uint32_t fn_004f9dc0(uint8_t* self, float factor)
{
    return Com<uint32_t>(Field<uint32_t>(self, 0x114), 0x3c / 4, factor * K(0x005bcb68), 1u);
}
static uint32_t fn_004f9dc0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f9dc0(reinterpret_cast<uint8_t*>(ecx), ArgFloat(a0));
}
ABI_ENTRY_004f9dc0(fn_004f9dc0_Entry, fn_004f9dc0_Abi)
HOOK(0x004f9dc0, "fn_004f9dc0", fn_004f9dc0_Entry, nullptr);

// 0x004f9de0 (ecx = object; stack: value, a1 (not read); callee pops 8; slot +0x48; executed in recorded runs): +0x118
// = value, then an iteration over the voices with an empty body. Returns the iterator chunk.
uint32_t fn_004f9de0(uint8_t* self, uint32_t value)
{
    Iter it = {0, 0};
    Field<uint32_t>(self, 0x118) = value;
    for (uint32_t v = IterFirst(Field<uint32_t>(self, 0x28), &it); v != 0; v = IterNext(&it))
    {
    }
    return IterEnd(&it);
}
static uint32_t fn_004f9de0_Abi(uint32_t ecx, uint32_t a0, uint32_t /*a1*/)
{
    return fn_004f9de0(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004f9de0(fn_004f9de0_Entry, fn_004f9de0_Abi)
HOOK(0x004f9de0, "fn_004f9de0", fn_004f9de0_Entry, nullptr);

// 0x004f9e60 (ecx = object; stack: factor (float); callee pops 4; slot +0x12c; not executed in recorded runs): when
// the listener +0x114 exists, SetDopplerFactor(factor * K(0x005bcb68), DS3D_DEFERRED); returns the HRESULT (0
// without a listener).
uint32_t fn_004f9e60(uint8_t* self, float factor)
{
    const uint32_t listener = Field<uint32_t>(self, 0x114);
    if (listener == 0)
        return 0;
    return Com<uint32_t>(listener, 0x30 / 4, factor * K(0x005bcb68), 1u);
}
static uint32_t fn_004f9e60_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004f9e60(reinterpret_cast<uint8_t*>(ecx), ArgFloat(a0));
}
ABI_ENTRY_004f9e60(fn_004f9e60_Entry, fn_004f9e60_Abi)
HOOK(0x004f9e60, "fn_004f9e60", fn_004f9e60_Entry, nullptr);

// 0x004f9e90 (esi = object; stack: distance factor; callee pops 4; caller 0x004fa8f0 Audio_InitDirectSound; executed
// in recorded runs). Listener setup: false without IDirectSound8 (+0x10c) or when the primary buffer's
// QueryInterface(IID 0x00579148, &+0x114) fails; otherwise virtual +0x12c(0.0), listener SetDistanceFactor(factor,
// DS3D_IMMEDIATE), virtual +0x48(0, 0) and true (al).
uint32_t fn_004f9e90(uint8_t* self, uint32_t factor)
{
    if (Field<uint32_t>(self, 0x10c) == 0)
        return 0;
    if (Com<uint32_t>(Field<uint32_t>(self, 0x110), 0, 0x00579148u, u(&Field<uint32_t>(self, 0x114))) != 0)
        return 0;
    VCall<uint32_t>(self, 0x12c / 4, 0u);
    Com<uint32_t>(Field<uint32_t>(self, 0x114), 0x2c / 4, factor, 0u);
    VCall<uint32_t>(self, 0x48 / 4, 0u, 0u);
    return 1;
}
static uint32_t fn_004f9e90_Abi(uint32_t esi, uint32_t a0)
{
    return fn_004f9e90(reinterpret_cast<uint8_t*>(esi), a0);
}
ABI_ENTRY_004f9e90(fn_004f9e90_Entry, fn_004f9e90_Abi)
HOOK(0x004f9e90, "fn_004f9e90", fn_004f9e90_Entry, nullptr);

// 0x004f9ef0 (ecx = object; stack: camera matrix, frame time, commit (byte); callee pops 12; slot +0x130; not executed
// in recorded runs). With IDirectSound8: the base update 0x004f2a80(matrix, frame time, commit); with a matrix the
// listener update 0x004f9f70 (eax = this; matrix, frame time) and the matrix (0x44 bytes) is kept at +0xc4; with
// commit, listener CommitDeferredSettings; when K(0x005caa48) == K(0x005bcbdc) (ordered) the debug print 0x004e4480
// (Game+8 of this, "3D = %d\n", +0x120) (cdecl).
uint32_t fn_004f9ef0(uint8_t* self, const uint32_t* matrix, uint32_t frameTime, uint32_t commit)
{
    if (Field<uint32_t>(self, 0x10c) == 0)
        return 0;
    uint32_t r = fn_004f2a80(self, frameTime, commit);
    if (matrix != nullptr)
    {
        fn_004f9f70(self, reinterpret_cast<const float*>(matrix), ArgFloat(frameTime));
        std::memcpy(self + 0xc4, matrix, 0x11 * 4);
    }
    if (static_cast<uint8_t>(commit) != 0)
        r = Com<uint32_t>(Field<uint32_t>(self, 0x114), 0x44 / 4);
    if (K(0x005caa48) == K(0x005bcbdc))
        r = GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t, ...)>(0x004e4480)(Field<uint32_t>(self, 8), 0x0058a5d4u,
                                                                                Field<uint32_t>(self, 0x120));
    return r;
}
static uint32_t fn_004f9ef0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004f9ef0(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<const uint32_t*>(a0), a1, a2);
}
ABI_ENTRY_004f9ef0(fn_004f9ef0_Entry, fn_004f9ef0_Abi)
HOOK(0x004f9ef0, "fn_004f9ef0", fn_004f9ef0_Entry, nullptr);

// 0x004f9f70 (eax = object; stack: camera matrix, frame time (float); callee pops 8; caller 0x004f9ef0; executed in
// recorded runs). Listener from the camera: without IDirectSound8 returns. inv = Matrix_Inverse(matrix) (0x004e2b40,
// eax = matrix, ecx = local 0x44-byte matrix). When dt > 0: k = 1 / dt, velocity +0xb8/+0xbc/+0xc0 = (inv m[3], m[7],
// m[11] - previous position +0xac/+0xb0/+0xb4) * k and listener SetVelocity(v, DS3D_DEFERRED). Then the position
// +0xac.. = (m[3], m[7], m[11]), listener SetPosition(position, deferred) and SetOrientation(-m[2], -m[6], -m[10],
// -m[1], -m[5], -m[9], deferred).
uint32_t fn_004f9f70(uint8_t* self, const float* matrix, float dt)
{
    if (Field<uint32_t>(self, 0x10c) == 0)
        return 0;
    float inv[17];
    orig::fn_004e2b40(u(matrix), u(inv));
    if (dt > K(0x0056c05c))
    {
        const float k = K(0x005bcb2c) / dt;
        const uint32_t listener = Field<uint32_t>(self, 0x114);
        const float vx = (inv[3] - Field<float>(self, 0xac)) * k;
        Field<float>(self, 0xb8) = vx;
        const float vy = (inv[7] - Field<float>(self, 0xb0)) * k;
        Field<float>(self, 0xbc) = vy;
        const float vz = (inv[11] - Field<float>(self, 0xb4)) * k;
        Field<float>(self, 0xc0) = vz;
        Com<uint32_t>(listener, 0x40 / 4, vx, vy, vz, 1u);
    }
    Field<float>(self, 0xb4) = inv[11];
    Field<float>(self, 0xac) = inv[3];
    Field<float>(self, 0xb0) = inv[7];
    Com<uint32_t>(Field<uint32_t>(self, 0x114), 0x38 / 4, inv[3], inv[7], inv[11], 1u);
    return Com<uint32_t>(Field<uint32_t>(self, 0x114), 0x34 / 4, -inv[2], -inv[6], -inv[10], -inv[1], -inv[5], -inv[9],
                         1u);
}
static uint32_t fn_004f9f70_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_004f9f70(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<const float*>(a0), ArgFloat(a1));
}
ABI_ENTRY_004f9f70(fn_004f9f70_Entry, fn_004f9f70_Abi)
HOOK(0x004f9f70, "fn_004f9f70", fn_004f9f70_Entry, nullptr);

// Entry stubs for the two tail-jump thunks 0x004fa1c0 / 0x004fa1d0 ("no ret found" in symbols/abi.csv): they replace
// their first stack argument with the voice's DirectSound buffer and jump to a stdcall COM method, which pops both
// stack slots (ret 8). No register inputs.
#define AUDIO02_ENTRY_S2_RET8(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push dword ptr [esp + 32] __asm push dword ptr [esp + 32] __asm call impl __asm add esp, 8 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret 8 }

// 0x004fa0c0 (stack: voice; callee pops 4; slot +0x28, ecx not read; called from 0x004f2a80; not executed in recorded
// runs). Is the voice's buffer (+0x5c) playing: false without a buffer; true when voice flags have 0x4000800
// (paused/pending); else GetStatus(&status) (written over the argument slot) & 5 (playing | looping).
uint32_t fn_004fa0c0(uint32_t* voice)
{
    const uint32_t buffer = voice[0x17];
    if (buffer == 0)
        return 0;
    if (voice[0] & 0x4000800)
        return 1;
    uint32_t status = u(voice);
    Com<uint32_t>(buffer, 0x24 / 4, u(&status));
    return (status & 5) != 0 ? 1u : 0u;
}
static uint32_t fn_004fa0c0_Abi(uint32_t a0)
{
    return fn_004fa0c0(reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_004fa0c0(fn_004fa0c0_Entry, fn_004fa0c0_Abi)
HOOK(0x004fa0c0, "fn_004fa0c0", fn_004fa0c0_Entry, nullptr);

// 0x004fa0f0 (ecx = object; stack: voice; callee pops 4; slot +0x3c; not executed in recorded runs): virtual
// +0x30(voice, 0) (stop without release, guess: pause).
uint32_t fn_004fa0f0(uint8_t* self, uint32_t voice)
{
    return VCall<uint32_t>(self, 0x30 / 4, voice, 0u);
}
static uint32_t fn_004fa0f0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fa0f0(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004fa0f0(fn_004fa0f0_Entry, fn_004fa0f0_Abi)
HOOK(0x004fa0f0, "fn_004fa0f0", fn_004fa0f0_Entry, nullptr);

// 0x004fa100 (ecx = object; stack: voice, resume (byte); callee pops 8; slot +0x2c; not executed in recorded runs).
// Start a voice's buffer (+0x5c). Streamed definitions (flags 0x1000040): fn_00500840 (ebx = this, esi = voice) fills
// the stream; nothing more unless voice+0x64 bit 2 is set; otherwise (not resuming) the callback +0x68(def, (def
// flags >> 10) & 0xffffff01) (cdecl) runs when def flags have 0x600, and the buffer plays looped (Play(0, 0,
// DSBPLAY_LOOPING)). Other definitions: the same callback, then Play(0, 0, voice flags & 0x20 ? looping : 0). Both
// set voice flags |= 0x4000000.
uint32_t fn_004fa100(uint8_t* self, uint32_t* voice, uint32_t resume)
{
    typedef uint32_t(__cdecl * Callback2)(uint32_t, uint32_t);
    const uint32_t def = voice[3];
    uint32_t loop;
    if (Field<uint32_t>(P(def), 0x20) & 0x1000040)
    {
        const uint32_t r = orig::fn_00500840(u(self), u(voice));
        if ((Field<uint8_t>(voice, 0x64) & 4) == 0)
            return r;
        if (static_cast<uint8_t>(resume) == 0)
        {
            const uint32_t cb = Field<uint32_t>(self, 0x68);
            const uint32_t d = voice[3];
            const uint32_t f = Field<uint32_t>(P(d), 0x20);
            if (cb != 0 && (f & 0x600) != 0)
                reinterpret_cast<Callback2>(cb)(d, (f >> 10) & 0xffffff01u);
        }
        loop = 1;
    }
    else
    {
        const uint32_t f = Field<uint32_t>(P(def), 0x20);
        if (static_cast<uint8_t>(resume) == 0)
        {
            const uint32_t cb = Field<uint32_t>(self, 0x68);
            if (cb != 0 && (f & 0x600) != 0)
                reinterpret_cast<Callback2>(cb)(def, (f >> 10) & 0xffffff01u);
        }
        loop = (voice[0] & 0x20) ? 1u : 0u;
    }
    const uint32_t r = Com<uint32_t>(voice[0x17], 0x30 / 4, 0u, 0u, loop);
    voice[0] |= 0x4000000;
    return r;
}
static uint32_t fn_004fa100_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004fa100(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint32_t*>(a0), a1);
}
ABI_ENTRY_004fa100(fn_004fa100_Entry, fn_004fa100_Abi)
HOOK(0x004fa100, "fn_004fa100", fn_004fa100_Entry, nullptr);

// 0x004fa1b0 (ecx = object; stack: voice; callee pops 4; slot +0x38; not executed in recorded runs): virtual
// +0x2c(voice, 1) (start as resume).
uint32_t fn_004fa1b0(uint8_t* self, uint32_t voice)
{
    return VCall<uint32_t>(self, 0x2c / 4, voice, 1u);
}
static uint32_t fn_004fa1b0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004fa1b0(reinterpret_cast<uint8_t*>(ecx), a0);
}
ABI_ENTRY_004fa1b0(fn_004fa1b0_Entry, fn_004fa1b0_Abi)
HOOK(0x004fa1b0, "fn_004fa1b0", fn_004fa1b0_Entry, nullptr);

// 0x004fa1c0 (stack: voice, frequency; slot +0x24, ecx not read; not executed in recorded runs): tail jump to the
// voice buffer's (+0x5c) SetFrequency(frequency) with the buffer written over the voice slot; the COM method pops both
// (symbols/abi.csv: "no ret found, stack 1 caller"; really 2 stack slots, callee pops 8). Hand-written stub.
uint32_t fn_004fa1c0(const uint32_t* voice, uint32_t frequency)
{
    return Com<uint32_t>(voice[0x17], 0x44 / 4, frequency);
}
static uint32_t fn_004fa1c0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004fa1c0(reinterpret_cast<const uint32_t*>(a0), a1);
}
AUDIO02_ENTRY_S2_RET8(fn_004fa1c0_Entry, fn_004fa1c0_Abi)
HOOK(0x004fa1c0, "fn_004fa1c0", fn_004fa1c0_Entry, nullptr);

// 0x004fa1d0 (stack: voice, level (float); slot +0x40, ecx not read; not executed in recorded runs): tail jump to the
// voice buffer's SetVolume: -10000 (DSBVOLUME_MIN) when level <= K(0x005bcb64), else _ftol(level * K(0x005bcb60))
// (unordered computes). Same stack shape as 0x004fa1c0 (callee pops 8). Hand-written stub.
uint32_t fn_004fa1d0(const uint32_t* voice, float level)
{
    uint32_t volume;
    if (level < K(0x005bcb64) || level == K(0x005bcb64))
        volume = 0xffffd8f0u;
    else
        volume = static_cast<uint32_t>(static_cast<int64_t>(level * K(0x005bcb60)));
    return Com<uint32_t>(voice[0x17], 0x3c / 4, volume);
}
static uint32_t fn_004fa1d0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004fa1d0(reinterpret_cast<const uint32_t*>(a0), ArgFloat(a1));
}
AUDIO02_ENTRY_S2_RET8(fn_004fa1d0_Entry, fn_004fa1d0_Abi)
HOOK(0x004fa1d0, "fn_004fa1d0", fn_004fa1d0_Entry, nullptr);

// 0x004fa210 (ecx = object; stack: def, index, volume, frequency, extra, voice, &position, 3d (byte); callee pops 32;
// slot +0x34; called by the play functions; not executed in recorded runs). Driver start (strings "Audio : Trying to
// play stereo sound in 3d"): a 3D start fails when +0x120 (3D voices) > 0x3f. Stereo definitions (def+0xde == 2) fail
// in 3D (log level 5 with the definition). Mono 3D non-looping (low byte of extra clear) streamed (voice def flags 0x40)
// voices: virtual +0x18(voice, &position) first and a fail when voice+0x60 >= def+0x34 (too far, guess). Then the
// buffer voice+0x5c = 0x004fa340(this, def, index, voice, extra) (fail when null); 3D: flags |= 0x8000 and +0x120 += 1;
// flags |= 0x80000000; virtual +0x40(voice, volume), +0x24(voice, frequency); flags bit 0x20 = looping (low byte of
// extra); a mono 3D voice whose virtual +0x18(voice, &position) returns false is not started; otherwise virtual
// +0x2c(voice, 0). Returns true (al) from that point on.
uint32_t fn_004fa210(uint8_t* self, uint8_t* def, uint32_t index, uint32_t volume, uint32_t frequency, uint32_t extra,
                     uint32_t* voice, uint32_t position, uint32_t is3d)
{
    const uint8_t three = static_cast<uint8_t>(is3d);
    if (three != 0 && Field<uint32_t>(self, 0x120) > 0x3f)
        return 0;
    const uint8_t looping = static_cast<uint8_t>(extra);
    if (Field<uint16_t>(def, 0xde) == 2)
    {
        if (three != 0)
        {
            orig::fn_00407ff0(Field<uint32_t>(self, 8), 5, 0x0058a5a4u, u(def));
            return 0;
        }
    }
    else if (three != 0 && looping == 0 && (Field<uint8_t>(P(voice[3]), 0x20) & 0x40) != 0)
    {
        VCall<uint32_t>(self, 0x18 / 4, u(voice), position);
        if (Field<float>(voice, 0x60) >= Field<float>(P(voice[3]), 0x34))
            return 0;
    }
    const uint32_t buffer = fn_004fa340(self, def, index, voice, extra);
    voice[0x17] = buffer;
    if (buffer == 0)
        return 0;
    if (three != 0)
    {
        voice[0] |= 0x8000;
        ++Field<int32_t>(self, 0x120);
    }
    voice[0] |= 0x80000000u;
    VCall<uint32_t>(self, 0x40 / 4, u(voice), volume);
    VCall<uint32_t>(self, 0x24 / 4, u(voice), frequency);
    if (looping != 0)
        voice[0] |= 0x20;
    else
        voice[0] &= 0xffffffdfu;
    if (Field<uint16_t>(def, 0xde) == 1 && three != 0)
    {
        if (static_cast<uint8_t>(VCall<uint32_t>(self, 0x18 / 4, u(voice), position)) == 0)
            return 1;
    }
    VCall<uint32_t>(self, 0x2c / 4, u(voice), 0u);
    return 1;
}
static uint32_t fn_004fa210_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                uint32_t a5, uint32_t a6, uint32_t a7)
{
    return fn_004fa210(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(a0), a1, a2, a3, a4,
                       reinterpret_cast<uint32_t*>(a5), a6, a7);
}
ABI_ENTRY_004fa210(fn_004fa210_Entry, fn_004fa210_Abi)
HOOK(0x004fa210, "fn_004fa210", fn_004fa210_Entry, nullptr);

// 0x004fa330 (stack: voice; callee pops 4; slot +0x14, ecx not read; not executed in recorded runs): returns
// voice+0x78 (the stream object).
uint32_t fn_004fa330(const uint8_t* voice)
{
    return Field<uint32_t>(voice, 0x78);
}
static uint32_t fn_004fa330_Abi(uint32_t a0)
{
    return fn_004fa330(reinterpret_cast<const uint8_t*>(a0));
}
ABI_ENTRY_004fa330(fn_004fa330_Entry, fn_004fa330_Abi)
HOOK(0x004fa330, "fn_004fa330", fn_004fa330_Entry, nullptr);

// 0x004fa340 (stdcall-like; stack: object, def, index, voice, extra; callee pops 20; caller 0x004fa210; executed in
// recorded runs). Creates the voice's DirectSound buffer (voice+0x5c is cleared first):
//   loaded definitions (no def flags 0x1000040): IDirectSound8 (+0x10c) DuplicateSoundBuffer(+0x108[index],
//   &voice+0x5c), voice flags &= ~0x400; returns voice+0x5c (0 without a table buffer).
//   streamed: voice+0x64..+0x8b and +0x80 zeroed (+0x78, the stream object, read first); voice+0x74 = chunk size
//   (def+0xe4 * 4 / 2 for file streams (flags 0x40, computed as (x << 2) >> 1), else (def+0xe4 >> 3) * 2); buffer size
//   = (+0x74 * 4 (voice def flags 0x2000000) or * 16) rounded up to 0x2000. File streams: an intro definition named
//   at def+0x7c (flags 0x1000000; AudioPC_FindDefinition quiet; this definition when absent; when its data +0xf8 is
//   null, Debug_Printf(Game+8, "Sequence Intro not setup right for\n%s", def) with three arguments (original bug:
//   the format is passed as the level) and its return value is then used as the intro definition, as in the
//   original). The old stream object is kept; otherwise a 0x55c-byte stream (0x004e5f30, eax = memory, ebx = buffer
//   size; 0, size / 4) opens the path _makepath(0, 0x0063a204 (directory), intro or def +0xfc, 0) through its virtual
//   +0x10(path, 0, 0, 0). Memory streams: a 0xe4-byte reader (vtable 0x0058b13c, byte +0xc = 0x10, rest zeroed) is
//   set up on the intro (def+0x7c) definition's data (0x004e5e80, ecx = reader, edx = data size +0xf0, esi = data
//   +0xf8), with the same intro fallback and log.
//   Then with the low byte of extra: voice+0x64 |= 0x80 and voice+0x6c = def, else voice+0x6c = 0; buffer =
//   0x004fa6d0(object, def, voice+0x74); voice flags &= ~0x400; voice+0x5c = buffer, returned.
// The stack cookie and SEH frame are not reproduced; the path buffer is the original's 0x104-byte frame slot.
uint32_t fn_004fa340(uint8_t* self, uint8_t* def, uint32_t index, uint32_t* voice, uint32_t extra)
{
    typedef uint32_t(__cdecl * MakePathFn)(char*, uint32_t, uint32_t, uint32_t, uint32_t);
    typedef uint32_t(__cdecl * Printf3Fn)(uint32_t, uint32_t, uint32_t);
    const MakePathFn makePath = reinterpret_cast<MakePathFn>(0x0053859e);
    const Printf3Fn debugPrintf3 = reinterpret_cast<Printf3Fn>(0x00407ff0);
    uint8_t* v = reinterpret_cast<uint8_t*>(voice);
    voice[0x17] = 0;
    if ((Field<uint32_t>(def, 0x20) & 0x1000040) == 0)
    {
        const uint32_t source = Field<uint32_t*>(self, 0x108)[index];
        if (source == 0)
            return 0;
        Com<uint32_t>(Field<uint32_t>(self, 0x10c), 0x14 / 4, source, u(&voice[0x17]));
        voice[0] &= 0xfffffbffu;
        return voice[0x17];
    }
    const uint32_t oldStream = Field<uint32_t>(v, 0x78);
    std::memset(v + 0x64, 0, 0xa * 4);
    Field<uint32_t>(v, 0x80) = 0;
    if (Field<uint8_t>(def, 0x20) & 0x40)
        Field<uint32_t>(v, 0x74) = (Field<uint32_t>(def, 0xe4) << 2) >> 1;
    else
        Field<uint32_t>(v, 0x74) = (Field<uint32_t>(def, 0xe4) >> 3) << 1;
    uint32_t bufferSize;
    if (Field<uint32_t>(P(voice[3]), 0x20) & 0x2000000)
        bufferSize = ((Field<uint32_t>(v, 0x74) << 2) + 0x1fff) & 0xffffe000u;
    else
        bufferSize = ((Field<uint32_t>(v, 0x74) << 4) + 0x1fff) & 0xffffe000u;
    char path[0x104];
    uint32_t found = 0;
    const uint32_t flags = Field<uint32_t>(def, 0x20);
    if (flags & 0x40)
    {
        uint32_t source = u(def);
        if (flags & 0x1000000)
        {
            source = AudioPC_FindDefinition(&found, self, u(def + 0x7c), 1);
            if (source == 0)
                source = u(def);
            if (Field<uint32_t>(P(source), 0xf8) == 0)
                source = debugPrintf3(Field<uint32_t>(self, 8), 0x0058a57c, u(def));
        }
        if (oldStream != 0)
        {
            Field<uint32_t>(v, 0x78) = oldStream;
        }
        else
        {
            const uint32_t p = HeapAlloc(0x55c, 0x10, 0x0058b5e8);
            const uint32_t stream = p ? orig::fn_004e5f30(p, bufferSize, 0, bufferSize >> 2) : 0;
            Field<uint32_t>(v, 0x78) = stream;
            makePath(path, 0, GameGlobal<uint32_t>(0x0063a204), source + 0xfc, 0);
            VCall<uint32_t>(P(Field<uint32_t>(v, 0x78)), 0x10 / 4, u(path), 0u, 0u, 0u);
        }
    }
    else
    {
        uint32_t reader = HeapAlloc(0xe4, 0x10, 0x0058b5e8);
        if (reader != 0)
        {
            std::memset(P(reader + 4), 0, 0x37 * 4);
            Field<uint32_t>(P(reader), 0) = 0x0058b13c;
            Field<uint8_t>(P(reader), 0xc) = 0x10;
        }
        Field<uint32_t>(v, 0x78) = reader;
        uint32_t source = AudioPC_FindDefinition(&found, self, u(def + 0x7c), 1);
        if (source == 0)
            source = u(def);
        if (Field<uint32_t>(P(source), 0xf8) == 0)
        {
            debugPrintf3(Field<uint32_t>(self, 8), 0x0058a57c, u(def));
            source = u(def);
        }
        orig::fn_004e5e80(Field<uint32_t>(v, 0x78), Field<uint32_t>(P(source), 0xf0), Field<uint32_t>(P(source), 0xf8));
    }
    if (static_cast<uint8_t>(extra) != 0)
    {
        Field<uint32_t>(v, 0x64) |= 0x80;
        Field<uint32_t>(v, 0x6c) = u(def);
    }
    else
    {
        Field<uint32_t>(v, 0x6c) = 0;
    }
    const uint32_t buffer = fn_004fa6d0(self, def, Field<uint32_t>(v, 0x74));
    voice[0] &= 0xfffffbffu;
    voice[0x17] = buffer;
    return buffer;
}
static uint32_t fn_004fa340_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004fa340(reinterpret_cast<uint8_t*>(a0), reinterpret_cast<uint8_t*>(a1), a2, reinterpret_cast<uint32_t*>(a3),
                       a4);
}
ABI_ENTRY_004fa340(fn_004fa340_Entry, fn_004fa340_Abi)
HOOK(0x004fa340, "fn_004fa340", fn_004fa340_Entry, nullptr);

// 0x004fa6d0 (stack: object, def, size; callee pops 12; callers 0x004fa340, 0x004fa7f0; executed in recorded runs).
// Creates a secondary buffer: WAVEFORMATEX on the stack (PCM (1), channels def+0xde, rate def+0xe0, block align
// channels * bits / 8 (signed, truncated), average bytes align * rate, bits def+0xea, cbSize 0) and a DSBUFFERDESC
// (size 0x24, flags 0x100e8 (mono) / 0x100a8 (stereo), bytes = size, 3D algorithm GUID copied from 0x005795d4);
// IDirectSound8 (+0x10c) CreateSoundBuffer(&desc, &buffer, 0) (the buffer is written over the def argument slot).
// Returns the buffer, or 0 when the HRESULT is negative. The stack cookie is not reproduced.
uint32_t fn_004fa6d0(uint8_t* self, const uint8_t* def, uint32_t size)
{
    alignas(4) uint8_t wfx[0x14];
    alignas(4) uint8_t desc[0x24];
    std::memset(wfx, 0, sizeof(wfx));
    const uint32_t rate = Field<uint32_t>(def, 0xe0);
    const uint16_t channels = Field<uint16_t>(def, 0xde);
    const uint16_t bits = Field<uint16_t>(def, 0xea);
    Field<uint16_t>(wfx, 0xe) = bits;
    const int32_t product = static_cast<int32_t>(bits) * static_cast<int32_t>(channels);
    const uint16_t align = static_cast<uint16_t>((product + ((product >> 31) & 7)) >> 3);
    Field<uint16_t>(wfx, 0xc) = align;
    Field<uint32_t>(wfx, 4) = rate;
    Field<uint32_t>(wfx, 8) = static_cast<uint32_t>(align) * rate;
    std::memset(desc, 0, sizeof(desc));
    Field<uint32_t>(desc, 0x18) = GameGlobal<uint32_t>(0x005795d8);
    Field<uint32_t>(desc, 0x1c) = GameGlobal<uint32_t>(0x005795dc);
    Field<uint32_t>(desc, 8) = size;
    Field<uint32_t>(desc, 0x14) = GameGlobal<uint32_t>(0x005795d4);
    Field<uint32_t>(desc, 4) = channels != 2 ? 0x100e8u : 0x100a8u;
    Field<uint32_t>(desc, 0x20) = GameGlobal<uint32_t>(0x005795e0);
    Field<uint16_t>(wfx, 0) = 1;
    Field<uint16_t>(wfx, 2) = channels;
    Field<uint32_t>(desc, 0) = 0x24;
    Field<uint32_t>(desc, 0x10) = u(wfx);
    uint32_t buffer = u(def);
    const int32_t hr = static_cast<int32_t>(Com<uint32_t>(Field<uint32_t>(self, 0x10c), 0xc / 4, u(desc), u(&buffer), 0u));
    return hr < 0 ? 0 : buffer;
}
static uint32_t fn_004fa6d0_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004fa6d0(reinterpret_cast<uint8_t*>(a0), reinterpret_cast<const uint8_t*>(a1), a2);
}
ABI_ENTRY_004fa6d0(fn_004fa6d0_Entry, fn_004fa6d0_Abi)
HOOK(0x004fa6d0, "fn_004fa6d0", fn_004fa6d0_Entry, nullptr);

// 0x004fa7f0 (stack: object, def, index; callee pops 12; caller 0x004fab80; executed in recorded runs). Uploads a
// loaded definition: unless def flags have 0x1010040 (streamed), buffer = 0x004fa6d0(object, def, def+0xf0); with a
// buffer: Lock(0, def+0xf0, &p1, &n1 (the def argument slot), &p2, &n2, 0) - on failure the buffer is released -
// else copies n1 bytes of def+0xf8 to p1 and the next n2 bytes to p2, Unlock(p1, n1, p2, n2), and the buffer table
// +0x108[index] = buffer. Then the definition's data +0xf8 is freed (PMS_heapFree) and cleared. Returns the last eax
// (approximately).
uint32_t fn_004fa7f0(uint8_t* self, uint8_t* def, uint32_t index)
{
    if (Field<uint32_t>(def, 0x20) & 0x1010040)
        return 0;
    uint32_t r = 0;
    const uint32_t buffer = fn_004fa6d0(self, def, Field<uint32_t>(def, 0xf0));
    if (buffer != 0)
    {
        uint32_t p1 = 0;
        uint32_t n1 = u(def);
        uint32_t p2 = 0;
        uint32_t n2 = 0;
        const int32_t hr = static_cast<int32_t>(Com<uint32_t>(buffer, 0x2c / 4, 0u, Field<uint32_t>(def, 0xf0), u(&p1),
                                                              u(&n1), u(&p2), u(&n2), 0u));
        if (hr < 0)
        {
            Com<uint32_t>(buffer, 2);
        }
        else
        {
            std::memcpy(P(p1), P(Field<uint32_t>(def, 0xf8)), n1);
            std::memcpy(P(p2), P(Field<uint32_t>(def, 0xf8) + n1), n2);
            Com<uint32_t>(buffer, 0x4c / 4, p1, n1, p2, n2);
            Field<uint32_t*>(self, 0x108)[index] = buffer;
        }
    }
    const uint32_t data = Field<uint32_t>(def, 0xf8);
    if (data != 0)
    {
        r = HeapFree(data);
        Field<uint32_t>(def, 0xf8) = 0;
    }
    return r;
}
static uint32_t fn_004fa7f0_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004fa7f0(reinterpret_cast<uint8_t*>(a0), reinterpret_cast<uint8_t*>(a1), a2);
}
ABI_ENTRY_004fa7f0(fn_004fa7f0_Entry, fn_004fa7f0_Abi)
HOOK(0x004fa7f0, "fn_004fa7f0", fn_004fa7f0_Entry, nullptr);
