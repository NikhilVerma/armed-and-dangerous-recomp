// Recovered (static, batch game_06): PlayerObject helpers around the turbo pack / jump (0x0047a950..0x0047bc80),
// projectile spawn helpers (0x0047dbf0..0x0047e050), the Projectile class methods (Load 0x0047e0c0, per-frame
// 0x0047e580, hit / damage handling 0x0047eae0..0x0047f020), PickupObject-descriptor projectile variants
// (0x0047f210..0x0047fa70, among them the "armed jaws" weapon) and PlayerObject vt+0xbc 0x004788a0.
// Compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do. Functions the recorded
// runs never execute are marked "not executed in recorded runs". Vtable slots come from a scan of .rdata in
// game.unpacked.exe (pointers to the function; vtable start found by walking back over code pointers, class from the
// slot 1 descriptor +0x14 "CLASS(Name)"). Several vtables whose slot 1 descriptor reads "CLASS(PickupObject)" carry
// projectile methods; they are cited by address.
//
// ABI notes found here:
//   0x00407ff0 (tracker name Debug_Printf) is a bare `ret`; 0x0047f7b0 calls it with eax = game+0xa6c and uses eax
//   afterwards (the call is kept, it does nothing).
//   0x004e7880 returns st0 (the vec3 length), not eax -> Normalize3 (inline asm, pops st0).
//   0x00471970 returns st0, listed as eax -> Call_00471970 (inline asm, pops st0).
//   Virtual +0x13c of Projectile (0x0047f4d0 / 0x0047eba0) takes no argument: the `push 0` before it in
//   0x0047eae0 / 0x0047f2a0 outlives it and becomes the 7th argument of 0x0047f020.
//   ecx inputs of 0x00493b70 are passed as 0 (the callers' ecx is whatever the previous callee left), as in
//   game_01.cpp; 0x004eb670 reads only its stack argument (Ghidra draft), its ecx is passed as 0.
// Where the original passes a register whose low byte it just set (mov al, 1 / xor al, al), the upper bytes are
// passed as the original left them when known, otherwise 0 (the callees test only the low byte).
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
inline void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
// Memory at an absolute address (object fields: D(o + 0x460)).
inline uint32_t& D(uint32_t a)
{
    return *reinterpret_cast<uint32_t*>(a);
}
inline int32_t& I(uint32_t a)
{
    return *reinterpret_cast<int32_t*>(a);
}
inline float& Fl(uint32_t a)
{
    return *reinterpret_cast<float*>(a);
}
inline uint8_t& By(uint32_t a)
{
    return *reinterpret_cast<uint8_t*>(a);
}
inline uint32_t fb(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
// A float constant of the game's .rdata, read in place.
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
const uint32_t kZeroAddr = 0x0056c05c;  // 0.0
const uint32_t kOneAddr = 0x005bcb2c;   // 1.0
// Virtual call through the object's vtable at byte offset `off` (thiscall, callee pops).
template <class... A>
inline uint32_t VC(uint32_t object, uint32_t off, A... args)
{
    return VCall<uint32_t>(P(object), static_cast<int>(off / 4), args...);
}
// Same, the caller tests AL.
template <class... A>
inline bool VB(uint32_t object, uint32_t off, A... args)
{
    return static_cast<uint8_t>(VCall<uint32_t>(P(object), static_cast<int>(off / 4), args...)) != 0;
}
// Same, the result comes back in st0.
template <class... A>
inline float VF(uint32_t object, uint32_t off, A... args)
{
    return VCall<float>(P(object), static_cast<int>(off / 4), args...);
}
inline uint32_t GameP()
{
    return D(0x005fe490);
}
inline uint32_t Rand()  // _rand 0x005391cd
{
    return static_cast<uint32_t>(orig::fn_005391cd());
}
// The reference drop the game inlines everywhere: --object+0x28; at zero, unless virtual +0xd8 says the object is
// kept, 0x00487da0 (eax = object) releases it.
inline void Unref(uint32_t object)
{
    if (--I(object + 0x28) == 0 && !VB(object, 0xd8))
        orig::fn_00487da0(object);
}
// The same with the slot test and clear: if (*slot) { drop it; *slot = 0; }.
inline void ClearRef(uint32_t slot)
{
    const uint32_t object = D(slot);
    if (object)
    {
        Unref(object);
        D(slot) = 0;
    }
}
// Object_IsDead 0x00454780 (esi = object), tested on AL.
inline bool IsDead(uint32_t object)
{
    return static_cast<uint8_t>(orig::fn_00454780(object)) != 0;
}
// ObjectSystem_CreateObject 0x0045f020 with the argument pattern every caller here uses: eax = name (0 or a string),
// ecx = template; stack: game+0xa84 (object system), 0, id, 0, 0, 0, 0, 0xaa, 0.
inline uint32_t CreateObject(uint32_t name, uint32_t templ, uint32_t id)
{
    return orig::fn_0045f020(name, templ, D(GameP() + 0xa84), 0, id, 0, 0, 0, 0, 0xaa, 0);
}
// 0x004e7880 (ecx = source vec3, edx = destination): normalises a 3D vector and returns its length in st0. The ABI
// table lists eax as the result; the callers pop st0 (fstp), so it is called here with its real st0 result.
float Normalize3(uint32_t ecx_, uint32_t edx_)
{
    static const uint32_t target = 0x004e7880;
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
// 0x00471970 (eax, ecx, edx, esi; stack: 4 arguments, caller pops): scales the vector edx - eax into ecx and returns
// the distance in st0 (the ABI table lists eax; the callers pop st0).
float Call_00471970(uint32_t eax_, uint32_t ecx_, uint32_t edx_, uint32_t esi_, uint32_t a0, uint32_t a1, uint32_t a2,
                    uint32_t a3)
{
    static const uint32_t target = 0x00471970;
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a3
        push a2
        push a1
        push a0
        mov eax, eax_
        mov ecx, ecx_
        mov edx, edx_
        mov esi, esi_
        call dword ptr [target]
        add esp, 16
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp r
    }
    return r;
}
// The 0x80-byte effect description passed to Fx_Create 0x0041cc60 (eax = flags low byte 0, ecx = game+0xa90; name,
// description): only the fields the callers set are meaningful; the rest is uninitialised stack in the original.
struct FxDesc
{
    uint32_t w[0x20];
};
}  // namespace

namespace
{
// Calls an original function that returns its result in st0 with edx loaded (0x004759e0, 0x00475990: edx =
// player; symbols/abi.csv lists an eax result, the callers use st0). As in game_01.cpp (ecx passed as 0).
float CallSt0Edx(uint32_t target, uint32_t edx_)
{
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        xor ecx, ecx
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
// x87 fsin / fcos of a float (the transcendental result is rounded once, when stored).
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
// fld x; fld y; fchs; fpatan; fsub sub: atan2(x, -y) - sub with one rounding, as 0x00479e39 computes it.
float YawAngle(float x, float y, float turn)
{
    float r;
    __asm {
        fld x
        fld y
        fchs
        fpatan
        fsub turn
        fstp r
    }
    return r;
}
// fild of a dword the original treats as unsigned (fadd 2^32 0x005bcb28 when negative).
float UFloat(uint32_t v)
{
    float f = static_cast<float>(static_cast<int32_t>(v));
    if (static_cast<int32_t>(v) < 0)
        f = f + K(0x005bcb28);
    return f;
}
}  // namespace

// Functions recovered in this file, called directly by earlier ones.
uint32_t fn_0047a950(uint32_t p);
uint32_t fn_0047a9a0(uint32_t p);
uint32_t fn_0047aaf0(uint32_t p, uint32_t a1);
uint32_t fn_0047b730(uint32_t p);
uint32_t fn_0047bc80(uint32_t index, uint32_t o);
uint32_t fn_0047b8e0(uint32_t p);
uint32_t PlayerObject_SetTurboBoost(uint32_t flag, uint32_t p);
uint32_t PlayerObject_SetTurboPack(uint32_t flag, uint32_t slot, uint32_t owner, uint32_t id, uint32_t scale);
uint32_t PlayerObject_PlayChestFx(uint32_t object, uint32_t effect);
uint32_t fn_0047dfd0(uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2);
float Projectile_GetSpeed(uint32_t o);
uint32_t Projectile_GetOwner(uint32_t o);
uint32_t Projectile_UpdateLifetime(uint32_t o);
uint32_t fn_0047e4d0(uint32_t o);
uint32_t Projectile_GetDamageFlags(uint32_t o);
uint32_t Projectile_Remove(uint32_t o);
uint32_t fn_0047ecb0(uint32_t o, uint32_t all);
uint32_t Projectile_ApplyHitDamage(uint32_t victim, uint32_t o, uint32_t hit);
uint32_t AreaDamage_Apply(uint32_t params, uint32_t a0, uint32_t center, uint32_t a2, uint32_t a3, uint32_t a4,
                          uint32_t a5, uint32_t a6);
uint32_t Projectile_Load(uint32_t o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t fn_0047f920(uint32_t o);

// ---------------------------------------------------------------------------------------------------------------
// PlayerObject (vtable 0x005aa888 "CLASS(PlayerObject)") helpers.

// 0x0047a950 (eax = player; returns eax 0/1). True when the player may act: not dead (Object_IsDead 0x00454780),
// the game mode 0x0040fab0 (eax = game+0xa68) is not 2, none of bits 0x7f800000 of +0xa34 are set and 0x00410150
// (eax = 0, ecx = game+0xa68) returns false.
uint32_t fn_0047a950(uint32_t p)
{
    if (IsDead(p))
        return 0;
    const uint32_t a68 = D(GameP() + 0xa68);
    if (orig::fn_0040fab0(a68) == 2)
        return 0;
    if (D(p + 0xa34) & 0x7f800000)
        return 0;
    if (static_cast<uint8_t>(orig::fn_00410150(0, a68)))
        return 0;
    return 1;
}
static uint32_t fn_0047a950_Abi(uint32_t eax)
{
    return fn_0047a950(eax);
}
ABI_ENTRY_0047a950(fn_0047a950_Entry, fn_0047a950_Abi)
HOOK(0x0047a950, "fn_0047a950", fn_0047a950_Entry, nullptr);

// 0x0047a9a0 (eax = player). Drops the objects at +0xad0 / +0xad4 once they are dead (the inlined reference drop),
// then, under the 0x0047a950 conditions, polls 0x00414730 (al = 1, ebx = 0x12 / 0x13, esi = game+0xa80; 0.25) and on
// a hit passes the code to 0x004852b0 (eax = object, ecx = code) for both objects (the last call is a tail jump).
uint32_t fn_0047a9a0(uint32_t p)
{
    const uint32_t ad0 = p + 0xad0;
    if (D(ad0) && IsDead(D(ad0)))
    {
        Unref(D(ad0));
        D(ad0) = 0;
    }
    const uint32_t ad4 = p + 0xad4;
    if (D(ad4) && IsDead(D(ad4)))
    {
        Unref(D(ad4));
        D(ad4) = 0;
    }
    uint32_t r = orig::fn_00454780(p);
    if (static_cast<uint8_t>(r))
        return r;
    const uint32_t g = GameP();
    const uint32_t a68 = D(g + 0xa68);
    r = orig::fn_0040fab0(a68);
    if (r == 2)
        return r;
    if (D(p + 0xa34) & 0x7f800000)
        return r;
    r = orig::fn_00410150(0, a68);
    if (static_cast<uint8_t>(r))
        return r;
    r = orig::fn_00414730((r & 0xffffff00u) | 1, 0x12, D(g + 0xa80), 0x3e800000);
    if (static_cast<uint8_t>(r))
    {
        if (D(ad0))
            r = orig::fn_004852b0(D(ad0), 0x12);
        if (D(ad4))
            r = orig::fn_004852b0(D(ad4), 0x12);
    }
    r = orig::fn_00414730((r & 0xffffff00u) | 1, 0x13, D(GameP() + 0xa80), 0x3e800000);
    if (static_cast<uint8_t>(r))
    {
        if (D(ad0))
            r = orig::fn_004852b0(D(ad0), 0x13);
        if (D(ad4))
            return orig::fn_004852b0(D(ad4), 0x13);
    }
    return r;
}
static uint32_t fn_0047a9a0_Abi(uint32_t eax)
{
    return fn_0047a9a0(eax);
}
ABI_ENTRY_0047a9a0(fn_0047a9a0_Entry, fn_0047a9a0_Abi)
HOOK(0x0047a9a0, "fn_0047a9a0", fn_0047a9a0_Entry, nullptr);

// 0x0047aaf0 (stack: player, unused (the original stores a byte in that slot); ret 8). Turbo pack / jump state on
// the flags +0xa34 (strings "fx_turbo_launch", "Fx_fs_roman_jump"): input polls 0x004148f0 / 0x00414810 /
// 0x00414730 (al = 1, code 0xd, game+0xa80, 0.5) give "pressed" / "released" / "held"; all are forced off (pressed
// on) when the player cannot act (see 0x0047a950, plus game+0xac8). Bit 4: on release the wing sounds stop
// (Jumper_PlayWingSounds 0x0047bb30, 0) and bit 4 is cleared. Bits 2 / 0x600 with a pack (+0xabc): on press with
// bit 0x10 the velocity handle +0x5b0 is released (0x00455a00) and when +0x4c8 < 0 the wing sounds start and the
// flags become |0x8e, &~0x600. Bit 0x40 with bit 0x80: an upward impulse mass * +0xa48 (0x00458440), flags |0x20
// &~0x40, cue 0x00493b70 and "Fx_fs_roman_jump" (0x00478800). Otherwise on "held" with +0x570 > 0: flags
// |0x20 &0xfffff83c; with a pack |0x92, "fx_turbo_launch" on the chest (0x0047bba0), an impulse mass * +0xa84 (and
// the boost object off, 0x0047b900) or mass * +0xa80, applied through 0x00455a30 into +0x5b0, then the jump effect;
// without a pack |0x40 and |0x80 when +0x4bc > 20.
uint32_t fn_0047aaf0(uint32_t p, uint32_t /*a1*/)
{
    uint32_t r = orig::fn_004148f0(1, D(GameP() + 0xa80), 0xd, 0x3f000000);
    const uint8_t pressed = static_cast<uint8_t>(r);
    r = orig::fn_00414810((r & 0xffffff00u) | 1, 0xd, D(GameP() + 0xa80), 0x3f000000);
    const uint8_t released = static_cast<uint8_t>(r);
    r = orig::fn_00414730((r & 0xffffff00u) | 1, 0xd, D(GameP() + 0xa80), 0x3f000000);
    uint8_t bl = static_cast<uint8_t>(r);
    uint8_t cl, dl;
    bool blocked = IsDead(p);
    if (!blocked)
    {
        const uint32_t g = GameP();
        if (D(g + 0xac8))
            blocked = true;
        else
        {
            const uint32_t a68 = D(g + 0xa68);
            if (orig::fn_0040fab0(a68) == 2 || (D(p + 0xa34) & 0x7f800000) ||
                static_cast<uint8_t>(orig::fn_00410150(0, a68)))
                blocked = true;
        }
    }
    if (blocked)
    {
        cl = 0;
        dl = 1;
        bl = 0;
    }
    else
    {
        dl = pressed;
        cl = released;
    }
    const uint32_t f = D(p + 0xa34);
    if (f & 4)
    {
        if (!cl)
            return f;
        orig::fn_0047bb30(D(p + 0xabc), p, p + 0xab8, 0);
        const uint32_t v = D(p + 0xa34) & 0xfffffffbu;
        D(p + 0xa34) = v;
        return v;
    }
    if (((f & 2) || (D(p + 0xabc) && (f & 0x600))) && !(f & 0x100))
    {
        if (!(f & 0x10) || !dl)
            return f;
        const uint32_t h = orig::fn_00455a00(D(p + 0x5b0));
        const bool below = Fl(p + 0x4c8) < K(kZeroAddr);
        D(p + 0x5b0) = h;
        if (!below)
            return h;
        orig::fn_0047bb30(D(p + 0xabc), p, p + 0xab8, 1);
        uint32_t v = D(p + 0xa34) | 0x8e;
        D(p + 0xa34) = v;
        v &= 0xfffff9ffu;
        D(p + 0xa34) = v;
        return v;
    }
    if (f & 0x40)
    {
        if (!(f & 0x80))
            return f;
        float imp[3];
        D(u(&imp[0])) = 0;
        D(u(&imp[1])) = 0;
        imp[2] = orig::fn_004581c0(p) * Fl(p + 0xa48);
        orig::fn_00458440(0, u(imp), p);
        const uint32_t v = D(p + 0xa34) | 0x20;
        D(p + 0xa34) = v;
        D(p + 0xa34) = v & 0xffffffbfu;
        orig::fn_00493b70(0, 2, 0x005fb99c, 0x0064a720, 0, p, 1, 0, 0);
        return orig::fn_00478800(p, 0x0059ec7c);
    }
    if ((f & 0x80) && !(f & 0x100))
        return f;
    if (!bl)
        return f;
    if (!(Fl(p + 0x570) > K(kZeroAddr)))
        return f;
    const uint32_t e = D(p + 0xa34) | 0x20;
    const uint32_t c = e & 0xfffff83cu;
    D(p + 0xa34) = e;
    D(p + 0xa34) = c;
    if (D(p + 0xabc))
    {
        D(p + 0xa34) = c | 0x92;
        PlayerObject_PlayChestFx(D(p + 0xabc), 0x0059ec6c);
        float imp[3];
        D(u(&imp[0])) = 0;
        D(u(&imp[1])) = 0;
        if (static_cast<uint8_t>(fn_0047b8e0(p)))
        {
            imp[2] = orig::fn_004581c0(p) * Fl(p + 0xa84);
            PlayerObject_SetTurboBoost(p & 0xffffff00u, p);
        }
        else
            imp[2] = orig::fn_004581c0(p) * Fl(p + 0xa80);
        D(p + 0x5b0) = orig::fn_00455a30(u(imp), 0x10, p, D(p + 0x5b0), D(p + 0xa94), 0xffffffffu, 0);
        return orig::fn_00478800(p, 0x0059ec7c);
    }
    D(p + 0xa34) = D(p + 0xa34) | 0x40;
    if (Fl(p + 0x4bc) > K(0x005bcbe8))  // 20.0
        D(p + 0xa34) |= 0x80;
    return D(p + 0xa34);
}
static uint32_t fn_0047aaf0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_0047aaf0(a0, a1);
}
ABI_ENTRY_0047aaf0(fn_0047aaf0_Entry, fn_0047aaf0_Abi)
HOOK(0x0047aaf0, "fn_0047aaf0", fn_0047aaf0_Entry, nullptr);

// 0x0047ae40 (ecx = player; stack: spawn record or 0; ret 4). PlayerObject vt+0xd4 (vtable 0x005aa888 slot 0x35).
// 0x0044f570 (eax = game+0xa88), 0x00475910 (esi = player; 1); with game+0xac8: game+0xacc vt+0xd4(0) when 0x0044e600
// is true, else vt+0x17c() unless game+0x26cc. Then from the record: +0x58c..+0x594 = record+0x18..+0x20 (+0x594
// clamped up to 0), +0x59c = record+0x24, +0x598 = record+0x10; without one: zeros, +0x59c = 1.0, +0x598 = -1.
// Returns the last value moved through eax. Not executed in recorded runs.
uint32_t fn_0047ae40(uint32_t p, uint32_t rec)
{
    orig::fn_0044f570(D(GameP() + 0xa88));
    orig::fn_00475910(p, 1);
    if (D(GameP() + 0xac8))
    {
        if (static_cast<uint8_t>(orig::fn_0044e600()))
            VC(D(GameP() + 0xacc), 0xd4, 0u);
        else if (!D(GameP() + 0x26cc))
            VC(D(GameP() + 0xacc), 0x17c);
    }
    if (rec)
    {
        D(p + 0x58c) = D(rec + 0x18);
        D(p + 0x590) = D(rec + 0x1c);
        D(p + 0x594) = D(rec + 0x20);
        if (Fl(p + 0x594) < K(kZeroAddr))
            D(p + 0x594) = 0;
        D(p + 0x59c) = D(rec + 0x24);
        D(p + 0x598) = D(rec + 0x10);
        return D(rec + 0x24);
    }
    D(p + 0x58c) = 0;
    D(p + 0x590) = 0;
    D(p + 0x59c) = 0x3f800000;
    D(p + 0x598) = 0xffffffffu;
    D(p + 0x594) = 0;
    return 0;
}
static uint32_t fn_0047ae40_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0047ae40(ecx, a0);
}
ABI_ENTRY_0047ae40(fn_0047ae40_Entry, fn_0047ae40_Abi)
HOOK(0x0047ae40, "fn_0047ae40", fn_0047ae40_Entry, nullptr);

// 0x0047af50 (ecx = object; value; ret 4): +0x454 = value. Not executed in recorded runs.
uint32_t fn_0047af50(uint32_t o, uint32_t v)
{
    D(o + 0x454) = v;
    return v;
}
static uint32_t fn_0047af50_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0047af50(ecx, a0);
}
ABI_ENTRY_0047af50(fn_0047af50_Entry, fn_0047af50_Abi)
HOOK(0x0047af50, "fn_0047af50", fn_0047af50_Entry, nullptr);

// 0x0047b730 (stack: player; ret 4). Combat-music style timer (guess): the objects 0x00475d30 / 0x00475d40 (eax =
// player) report a position through vt+0x54; when it changed from game+0xb10 / game+0xb00 (or there is no object)
// game+0xb0c / game+0xafc take the time 0x00604a98. When either has been unchanged for more than 30 s, or game+0x748's
// +0xe5c exceeds 30, the cue 0x00493b70 (ebx = 0x37, edi = 0x005ce350; 0x0063a184, player+0xe60, player, 1, 0,
// callback 0x0047b520) is sent, both stamps are reset and +0xe5c is cleared.
uint32_t fn_0047b730(uint32_t p)
{
    const uint32_t t = D(0x00604a98);
    float vec[3];
    const uint32_t a = orig::fn_00475d30(p);
    const uint32_t b = orig::fn_00475d40(p);
    uint32_t g;
    if (a)
    {
        VC(a, 0x54, u(vec));
        g = GameP();
        if (D(u(&vec[0])) != D(g + 0xb10) || D(u(&vec[1])) != D(g + 0xb14) || D(u(&vec[2])) != D(g + 0xb18))
            D(g + 0xb0c) = t;
        D(g + 0xb10) = D(u(&vec[0]));
        D(g + 0xb14) = D(u(&vec[1]));
        D(g + 0xb18) = D(u(&vec[2]));
    }
    else
    {
        g = GameP();
        D(g + 0xb0c) = t;
    }
    if (b)
    {
        VC(b, 0x54, u(vec));
        g = GameP();
        if (D(u(&vec[0])) != D(g + 0xb00) || D(u(&vec[1])) != D(g + 0xb04) || D(u(&vec[2])) != D(g + 0xb08))
            D(g + 0xafc) = t;
        D(g + 0xb00) = D(u(&vec[0]));
        D(g + 0xb04) = D(u(&vec[1]));
        D(g + 0xb08) = D(u(&vec[2]));
    }
    else
        D(g + 0xafc) = t;
    const float tf = Fl(u(&t));
    const float k30 = K(0x005bccbc);  // 30.0
    if (!(tf - Fl(g + 0xb0c) > k30) && !(tf - Fl(g + 0xafc) > k30) && !(Fl(D(g + 0x748) + 0xe5c) > k30))
        return 0;
    orig::fn_00493b70(0, 0x37, 0x005ce350, 0x0063a184, p + 0xe60, p, 1, 0, 0x0047b520);
    g = GameP();
    D(g + 0xb0c) = t;
    D(g + 0xafc) = t;
    D(p + 0xe5c) = 0;
    return g;
}
static uint32_t fn_0047b730_Abi(uint32_t a0)
{
    return fn_0047b730(a0);
}
ABI_ENTRY_0047b730(fn_0047b730_Entry, fn_0047b730_Abi)
HOOK(0x0047b730, "fn_0047b730", fn_0047b730_Entry, nullptr);

// 0x0047b8b0 (ecx = player): 1 when the turbo pack +0xabc exists. Not executed in recorded runs.
uint32_t PlayerObject_HasTurboPack(uint32_t p)
{
    return D(p + 0xabc) != 0 ? 1u : 0u;
}
static uint32_t PlayerObject_HasTurboPack_Abi(uint32_t ecx)
{
    return PlayerObject_HasTurboPack(ecx);
}
ABI_ENTRY_0047b8b0(PlayerObject_HasTurboPack_Entry, PlayerObject_HasTurboPack_Abi)
HOOK(0x0047b8b0, "PlayerObject_HasTurboPack", PlayerObject_HasTurboPack_Entry, nullptr);

// 0x0047b8c0 (eax = player; stack: on (byte); ret 4): PlayerObject_SetTurboPack 0x0047b9f0 (al = on with eax's upper
// bytes, ecx = player+0xabc; player, id = word +0x394, scale 0).
uint32_t fn_0047b8c0(uint32_t p, uint32_t on)
{
    return PlayerObject_SetTurboPack((p & 0xffffff00u) | (on & 0xff), p + 0xabc, p,
                                     *reinterpret_cast<uint16_t*>(p + 0x394), 0);
}
static uint32_t fn_0047b8c0_Abi(uint32_t eax, uint32_t a0)
{
    return fn_0047b8c0(eax, a0);
}
ABI_ENTRY_0047b8c0(fn_0047b8c0_Entry, fn_0047b8c0_Abi)
HOOK(0x0047b8c0, "fn_0047b8c0", fn_0047b8c0_Entry, nullptr);

// 0x0047b8e0 (eax = player): 1 when both the turbo pack +0xabc and the boost object +0xac0 exist. Not executed in
// recorded runs.
uint32_t fn_0047b8e0(uint32_t p)
{
    return (D(p + 0xabc) && D(p + 0xac0)) ? 1u : 0u;
}
static uint32_t fn_0047b8e0_Abi(uint32_t eax)
{
    return fn_0047b8e0(eax);
}
ABI_ENTRY_0047b8e0(fn_0047b8e0_Entry, fn_0047b8e0_Abi)
HOOK(0x0047b8e0, "fn_0047b8e0", fn_0047b8e0_Entry, nullptr);

// 0x0047b900 (al = on, ecx = player). On: unless +0xac0 exists, creates "pickup_turbo_boost" (name "default", id
// 0x3ff), vt+0xf8(0x100, 0), takes a reference and attaches it to the pack +0xabc at "ref_turbo" (Object_Attach
// 0x004539f0), then ObjectList_Unlink 0x00454750 (esi = game+0xa84 +0xe9c). Off: vt+0xdc() on +0xac0 and the
// reference is dropped. Not executed in recorded runs.
uint32_t PlayerObject_SetTurboBoost(uint32_t flag, uint32_t p)
{
    if (static_cast<uint8_t>(flag))
    {
        if (D(p + 0xac0))
            return D(p + 0xac0);
        const uint32_t o = CreateObject(0x005baaf0, 0x005b05cc, 0x3ff);
        D(p + 0xac0) = o;
        VC(o, 0xf8, 0x100u, 0u);
        I(D(p + 0xac0) + 0x28) += 1;
        orig::fn_004539f0(0x0059e5c0, D(p + 0xabc), D(p + 0xac0), 0x0059e5c0, 1, 0);
        return orig::fn_00454750(D(D(GameP() + 0xa84) + 0xe9c), D(p + 0xac0));
    }
    const uint32_t slot = p + 0xac0;
    if (!D(slot))
        return 0;
    const uint32_t r = VC(D(slot), 0xdc);
    Unref(D(slot));
    D(slot) = 0;
    return r;
}
static uint32_t PlayerObject_SetTurboBoost_Abi(uint32_t eax, uint32_t ecx)
{
    return PlayerObject_SetTurboBoost(eax, ecx);
}
ABI_ENTRY_0047b900(PlayerObject_SetTurboBoost_Entry, PlayerObject_SetTurboBoost_Abi)
HOOK(0x0047b900, "PlayerObject_SetTurboBoost", PlayerObject_SetTurboBoost_Entry, nullptr);

// 0x0047b9f0 (al = on, ecx = slot; stack: owner, id, scale; caller pops). On: unless *slot exists, creates "turbo"
// (template 0x0059e5b8, id) into *slot, flag 0x20000 in +0x424, Object_SetScale 0x004535d0 (scale, scale, scale)
// unless scale == 0, Object_setPhysicsEnabled 0x004577b0 (1), vt+0xf8(0x100, 0), a reference, attached to the owner
// at "chest", animation "turbo_idle" (0x00408e30, eax = 0x80, edx = -1), then ObjectList_Unlink (esi = game+0xa84
// +0xe9c). Off: vt+0xdc() on *slot, the reference dropped, *slot = 0.
uint32_t PlayerObject_SetTurboPack(uint32_t flag, uint32_t slot, uint32_t owner, uint32_t id, uint32_t scale)
{
    if (static_cast<uint8_t>(flag))
    {
        if (D(slot))
            return D(slot);
        const uint32_t o = CreateObject(0, 0x0059e5b8, id);
        D(slot) = o;
        if (!o)
            return 0;
        D(o + 0x424) |= 0x20000;
        if (!(Fl(u(&scale)) == K(kZeroAddr)))
        {
            uint32_t v[3] = {scale, scale, scale};
            orig::fn_004535d0(u(v), D(slot));
        }
        orig::fn_004577b0(D(slot), 1);
        VC(D(slot), 0xf8, 0x100u, 0u);
        I(D(slot) + 0x28) += 1;
        orig::fn_004539f0(0x0059e210, owner, D(slot), 0x0059e210, 1, 0);
        orig::fn_00408e30(0x80, D(slot), 0xffffffffu, 0x005b6674, 0, 0, 0, 0);
        return orig::fn_00454750(D(D(GameP() + 0xa84) + 0xe9c), D(slot));
    }
    if (!D(slot))
        return 0;
    const uint32_t r = VC(D(slot), 0xdc);
    Unref(D(slot));
    D(slot) = 0;
    return r;
}
static uint32_t PlayerObject_SetTurboPack_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return PlayerObject_SetTurboPack(eax, ecx, a0, a1, a2);
}
ABI_ENTRY_0047b9f0(PlayerObject_SetTurboPack_Entry, PlayerObject_SetTurboPack_Abi)
HOOK(0x0047b9f0, "PlayerObject_SetTurboPack", PlayerObject_SetTurboPack_Entry, nullptr);

// 0x0047bba0 (eax = object, edx = effect name). Plays an effect on the object's "chest" bone: Fx_Create 0x0041cc60
// (eax = object with al = 0, ecx = game+0xa90; name, description {flags 4, +0x18 "chest", +0x1c object}). Not
// executed in recorded runs.
uint32_t PlayerObject_PlayChestFx(uint32_t object, uint32_t effect)
{
    FxDesc d;
    std::memset(&d, 0, sizeof d);
    d.w[0x1c / 4] = object;
    d.w[0x18 / 4] = 0x0059e210;
    d.w[0] = 4;
    return orig::fn_0041cc60(object & 0xffffff00u, D(GameP() + 0xa90), effect, u(&d));
}
static uint32_t PlayerObject_PlayChestFx_Abi(uint32_t eax, uint32_t edx)
{
    return PlayerObject_PlayChestFx(eax, edx);
}
ABI_ENTRY_0047bba0(PlayerObject_PlayChestFx_Entry, PlayerObject_PlayChestFx_Abi)
HOOK(0x0047bba0, "PlayerObject_PlayChestFx", PlayerObject_PlayChestFx_Entry, nullptr);

// 0x0047bbe0 (ecx = player; stack: a0, trigger, a2, a3; ret 0x10). PlayerObject vt+0x34 (vtable 0x005aa888 slot 13):
// on the trigger "topsy_create" (_stricmp) with no +0xad8 yet: 0x004a0e00 (al = 0, esi = player+0xc24), creates
// "player_topsyturvy" (id 0x400) into +0xad8 and attaches it (eax = player+0xdec, ecx = player; game+0xc88 when that
// string is not empty, else 0; 1; 0), then takes a reference. Not executed in recorded runs.
uint32_t PlayerObject_OnTopsyCreate(uint32_t p, uint32_t /*a0*/, uint32_t trigger, uint32_t /*a2*/, uint32_t /*a3*/)
{
    uint32_t r = orig::fn_005448ee(0, trigger, 0x0059e568);
    if (r)
        return r;
    if (D(p + 0xad8))
        return D(p + 0xad8);
    orig::fn_004a0e00(0, p + 0xc24);
    const uint32_t o = CreateObject(0, 0x005bc40c, 0x400);
    const uint32_t s = GameP() + 0xc88;
    D(p + 0xad8) = o;
    r = static_cast<uint32_t>(orig::fn_004539f0(p + 0xdec, p, o, By(s) ? s : 0, 1, 0));
    I(D(p + 0xad8) + 0x28) += 1;
    return r;
}
static uint32_t PlayerObject_OnTopsyCreate_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PlayerObject_OnTopsyCreate(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0047bbe0(PlayerObject_OnTopsyCreate_Entry, PlayerObject_OnTopsyCreate_Abi)
HOOK(0x0047bbe0, "PlayerObject_OnTopsyCreate", PlayerObject_OnTopsyCreate_Entry, nullptr);

// 0x0047bc80 (eax = index, ecx = object): 1 when +0x1c[index] or +0xa8[index] is set.
uint32_t fn_0047bc80(uint32_t index, uint32_t o)
{
    if (D(o + index * 4 + 0x1c))
        return 1;
    if (D(o + index * 4 + 0xa8))
        return 1;
    return 0;
}
static uint32_t fn_0047bc80_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_0047bc80(eax, ecx);
}
ABI_ENTRY_0047bc80(fn_0047bc80_Entry, fn_0047bc80_Abi)
HOOK(0x0047bc80, "fn_0047bc80", fn_0047bc80_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Projectile spawn helpers: create an object from a template (id 0x400) and launch it through its vt+0x140
// (Projectile 0x0047e400: matrix, owner); vt+0x150 sets the target point (0x0047e340).

// 0x0047dbf0 (eax = source object, ecx = template, edx = parent; stack: a0, owner, use_target (byte); ret 0xc). In game
// mode 3 (0x0040fab0) it defers to 0x0047dfd0. Otherwise creates the template, takes the bone matrix of the source
// on the parent (0x00453c50) as the start point P; vt+0x164(&Q) gives the default target; with use_target the record
// vt+0x160() (object, flag +0x20, point +4) replaces it: when the flag is set and vt+0x15c() is false the target comes
// from 0x004580a0 (eax = &Q, ecx = object, ebx = &P; speed 0x0047e360, 1) instead, and when vt+0x15c() is true
// vt+0x14c(object, "ref_hit_00") is called first. Distance |P - Q| goes to +0x568 / +0x56c, vt+0x150(&Q), then the
// direction Q - P (normalised when > 0) becomes a matrix at P (0x004e3070) for vt+0x140(matrix, owner). Returns the
// new object or 0.
uint32_t fn_0047dbf0(uint32_t src, uint32_t templ, uint32_t parent, uint32_t a0, uint32_t owner, uint32_t useTarget)
{
    alignas(4) uint8_t fr[0xb0];
    std::memset(fr, 0, sizeof fr);
    const uint32_t S = u(fr);
    if (orig::fn_0040fab0(D(GameP() + 0xa68)) == 3)
        return fn_0047dfd0(src, parent, a0, owner, templ);
    const uint32_t o = CreateObject(0, templ, 0x400);
    if (!o || !parent)
        return 0;
    if (!static_cast<uint8_t>(orig::fn_00453c50(parent, src, S + 0x28, 1, 0, 0, 1, 0, 0)))
        return 0;
    D(S + 0x14) = D(S + 0x54);  // P = bone matrix translation (m[3], m[7], m[11])
    D(S + 0x0c) = D(S + 0x34);
    D(S + 0x10) = D(S + 0x44);
    VC(o, 0x164, S);  // Q (S+0..8)
    if (static_cast<uint8_t>(useTarget))
    {
        const uint32_t rec = VC(o, 0x160);
        if (D(rec) && VB(D(rec), 0xd8))
        {
            bool copy = true;
            if (By(rec + 0x20))
            {
                if (!VB(o, 0x15c))
                {
                    const float speed = Projectile_GetSpeed(o);
                    orig::fn_004580a0(S, D(rec), S + 0xc, fb(speed), 1);
                    copy = false;
                }
                else
                    VC(o, 0x14c, D(rec), 0x005b68fcu);
            }
            if (copy)
            {
                D(S + 0) = D(rec + 4);
                D(S + 4) = D(rec + 8);
                D(S + 8) = D(rec + 0xc);
            }
        }
    }
    const float dz = Fl(S + 0x14) - Fl(S + 8);
    const float dx = Fl(S + 0x0c) - Fl(S + 0);
    const float dy = Fl(S + 0x10) - Fl(S + 4);
    const float len = std::sqrt(dy * dy + dx * dx + dz * dz);
    Fl(o + 0x568) = len;
    Fl(S + 0x24) = len;
    Fl(o + 0x56c) = len;
    VC(o, 0x150, S);
    Fl(S + 0x18) = Fl(S + 0) - Fl(S + 0x0c);
    Fl(S + 0x1c) = Fl(S + 4) - Fl(S + 0x10);
    Fl(S + 0x20) = Fl(S + 8) - Fl(S + 0x14);
    if (Fl(S + 0x24) > K(kZeroAddr))
    {
        const float inv = K(kOneAddr) / Fl(S + 0x24);
        Fl(S + 0x18) = Fl(S + 0x18) * inv;
        Fl(S + 0x1c) = Fl(S + 0x1c) * inv;
        Fl(S + 0x20) = inv * Fl(S + 0x20);
    }
    orig::fn_004e3070(S + 0x18, S + 0x6c, 0, S + 0x0c);
    VC(o, 0x140, S + 0x6c, owner);
    return o;
}
static uint32_t fn_0047dbf0_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0047dbf0(eax, ecx, edx, a0, a1, a2);
}
ABI_ENTRY_0047dbf0(fn_0047dbf0_Entry, fn_0047dbf0_Abi)
HOOK(0x0047dbf0, "fn_0047dbf0", fn_0047dbf0_Entry, nullptr);

// 0x0047de40 (eax = start point, ebx = target point; stack: a0 (unused), owner, template, a3 (unused); ret 0x10).
// Creates the template (id 0x400), sets +0x568 / +0x56c = |start - target| (the original keeps the length in the
// template argument slot), vt+0x150(target), and launches it along target - start (normalised when > 0) from a
// matrix at the start point (0x004e3070): vt+0x140(matrix, owner). Returns the object or 0.
uint32_t fn_0047de40(uint32_t from, uint32_t to, uint32_t /*a0*/, uint32_t owner, uint32_t templ, uint32_t /*a3*/)
{
    alignas(4) uint8_t fr[0x5c];
    std::memset(fr, 0, sizeof fr);
    const uint32_t S = u(fr);
    const uint32_t o = CreateObject(0, templ, 0x400);
    if (!o)
        return 0;
    D(S + 8) = D(to + 8);
    D(S + 4) = D(to + 4);
    D(S + 0) = D(to);
    const float dz = Fl(from + 8) - Fl(S + 8);
    const float dy = Fl(from + 4) - Fl(S + 4);
    const float dx = Fl(from) - Fl(S + 0);
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    Fl(o + 0x568) = len;
    Fl(o + 0x56c) = len;
    VC(o, 0x150, S);
    Fl(S + 0x0c) = Fl(S + 0) - Fl(from);
    Fl(S + 0x10) = Fl(S + 4) - Fl(from + 4);
    Fl(S + 0x14) = Fl(S + 8) - Fl(from + 8);
    if (len > K(kZeroAddr))
    {
        const float inv = K(kOneAddr) / len;
        Fl(S + 0x0c) = Fl(S + 0x0c) * inv;
        Fl(S + 0x10) = Fl(S + 0x10) * inv;
        Fl(S + 0x14) = inv * Fl(S + 0x14);
    }
    orig::fn_004e3070(S + 0x0c, S + 0x18, 0, from);
    VC(o, 0x140, S + 0x18, owner);
    return o;
}
static uint32_t fn_0047de40_Abi(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0047de40(eax, ebx, a0, a1, a2, a3);
}
ABI_ENTRY_0047de40(fn_0047de40_Entry, fn_0047de40_Abi)
HOOK(0x0047de40, "fn_0047de40", fn_0047de40_Entry, nullptr);

// 0x0047df70 (stack: a0 (unused), owner, template, matrix; ret 0x10). Creates the template (id 0x400) and launches it:
// vt+0x140(matrix, owner). Returns the object or 0. Not executed in recorded runs.
uint32_t fn_0047df70(uint32_t /*a0*/, uint32_t owner, uint32_t templ, uint32_t matrix)
{
    const uint32_t o = CreateObject(0, templ, 0x400);
    if (!o)
        return 0;
    VC(o, 0x140, matrix, owner);
    return o;
}
static uint32_t fn_0047df70_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0047df70(a0, a1, a2, a3);
}
ABI_ENTRY_0047df70(fn_0047df70_Entry, fn_0047df70_Abi)
HOOK(0x0047df70, "fn_0047df70", fn_0047df70_Entry, nullptr);

// 0x0047dfd0 (ecx = source object, edx = parent; stack: a0 (unused), owner, template; ret 0xc). Takes the source's
// bone matrix on the parent (0x00453c50); when found, creates the template (id 0x400) and launches it from that
// matrix: vt+0x140(matrix, owner). Returns the object or 0. Not executed in recorded runs.
uint32_t fn_0047dfd0(uint32_t src, uint32_t parent, uint32_t /*a0*/, uint32_t owner, uint32_t templ)
{
    uint32_t m[0x44 / 4];
    std::memset(m, 0, sizeof m);
    if (!static_cast<uint8_t>(orig::fn_00453c50(parent, src, u(m), 1, 0, 0, 1, 0, 0)))
        return 0;
    const uint32_t o = CreateObject(0, templ, 0x400);
    if (!o)
        return 0;
    VC(o, 0x140, u(m), owner);
    return o;
}
static uint32_t fn_0047dfd0_Abi(uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0047dfd0(ecx, edx, a0, a1, a2);
}
ABI_ENTRY_0047dfd0(fn_0047dfd0_Entry, fn_0047dfd0_Abi)
HOOK(0x0047dfd0, "fn_0047dfd0", fn_0047dfd0_Entry, nullptr);

// 0x0047e050 (edi = position; stack: a0 (unused), owner, template, direction, up; ret 0x14). Creates the template
// (id 0x400) and launches it from the matrix 0x004e3070 (eax = direction, ecx = matrix, edx = up, edi = position):
// vt+0x140(matrix, owner). Returns the object or 0. Not executed in recorded runs.
uint32_t fn_0047e050(uint32_t pos, uint32_t /*a0*/, uint32_t owner, uint32_t templ, uint32_t dir, uint32_t up)
{
    uint32_t m[0x44 / 4];
    std::memset(m, 0, sizeof m);
    const uint32_t o = CreateObject(0, templ, 0x400);
    if (!o)
        return 0;
    orig::fn_004e3070(dir, u(m), up, pos);
    VC(o, 0x140, u(m), owner);
    return o;
}
static uint32_t fn_0047e050_Abi(uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_0047e050(edi, a0, a1, a2, a3, a4);
}
ABI_ENTRY_0047e050(fn_0047e050_Entry, fn_0047e050_Abi)
HOOK(0x0047e050, "fn_0047e050", fn_0047e050_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Projectile (vtable 0x005ac790 "CLASS(Projectile)", and the same methods in vtables 0x005adc78, 0x005a9a40 ...).

// 0x0047e0c0 (ecx = projectile; stack: template, a1, a2, a3; ret 0x10). Projectile Load (vtable 0x005ac790 slot 6):
// Object_LoadProperties 0x004517b0, clears +0x504/+0x544/+0x548/+0x520..+0x528/+0x568/+0x570/+0x574, then reads
// "speed" +0x460 (default 10, then times 3), "lifetime" +0x464 (4), "fx_start" +0x484, "fx_start_bone_name" +0x4a4,
// "fx_end" +0x4c4, "fx_end_material" +0x4e4, "damage" +0x46c (10), "impact_force" +0x470 (2010),
// "head_shot_multiplier" +0x53c (1), "torso_shot_multiplier" +0x540 (1), "area_damage" +0x474,
// "damage_radius_inner" +0x478, "damage_radius_outer" +0x47c (0); +0x480 = 1 / (outer - inner) when outer > 0.01 and
// outer > inner, else 1; "allow_cartoon_death" +0x578 (true), "damages_buildings" +0x579 (false); +0x468 = 0x004581c0
// (eax = projectile); +0x538 = 0; linked into the lists game+0xa84 +0xf1c and +0xe98 (ObjectList_Link 0x004546d0).
uint32_t Projectile_Load(uint32_t o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004517b0(o, a0, a1, a2, a3);
    D(o + 0x574) = 0;
    D(o + 0x570) = 0;
    D(o + 0x504) = 0;
    D(o + 0x544) = 0;
    By(o + 0x548) = 0;
    D(o + 0x568) = 0;
    D(o + 0x520) = 0;
    D(o + 0x524) = 0;
    D(o + 0x528) = 0;
    orig::fn_004534a0(a3, o + 0x460, o, 0x005b6378, a2, 0x41200000, 0);
    Fl(o + 0x460) = Fl(o + 0x460) * K(0x005bcb7c);  // 3.0
    orig::fn_004534a0(a3, o + 0x464, o, 0x0059e520, a2, 0x40800000, 0);
    orig::fn_004533a0(0x0058a9a4, o + 0x484, o, 0x0059e514, a2, a3, 0);
    orig::fn_004533a0(0x0058a9a4, o + 0x4a4, o, 0x0059e500, a2, a3, 0);
    orig::fn_004533a0(0x0058a9a4, o + 0x4c4, o, 0x0059e4f8, a2, a3, 0);
    orig::fn_004533a0(0x0058a9a4, o + 0x4e4, o, 0x0059e4e8, a2, a3, 0);
    orig::fn_004534a0(a3, o + 0x46c, o, 0x0059e4e0, a2, 0x41200000, 0);
    orig::fn_004534a0(a3, o + 0x470, o, 0x005b93c8, a2, 0x44fb4000, 0);
    orig::fn_004534a0(a3, o + 0x53c, o, 0x0059e4c8, a2, 0x3f800000, 0);
    orig::fn_004534a0(a3, o + 0x540, o, 0x0059e4b0, a2, 0x3f800000, 0);
    orig::fn_004534a0(a3, o + 0x474, o, 0x005b93bc, a2, 0, 0);
    orig::fn_004534a0(a3, o + 0x478, o, 0x005b93a8, a2, 0, 0);
    orig::fn_004534a0(a3, o + 0x47c, o, 0x005b9394, a2, 0, 0);
    if (Fl(o + 0x47c) > K(0x005bcbec) && Fl(o + 0x47c) > Fl(o + 0x478))  // 0.01
        Fl(o + 0x480) = K(kOneAddr) / (Fl(o + 0x47c) - Fl(o + 0x478));
    else
        D(o + 0x480) = 0x3f800000;
    orig::fn_004534f0(1, a3, o + 0x578, o, 0x0059e49c, a2, 0);
    orig::fn_004534f0(0, a3, o + 0x579, o, 0x0059e488, a2, 0);
    Fl(o + 0x468) = orig::fn_004581c0(o);
    D(o + 0x538) = 0;
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf1c), o);
    return static_cast<uint32_t>(orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe98), o));
}
static uint32_t Projectile_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Projectile_Load(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0047e0c0(Projectile_Load_Entry, Projectile_Load_Abi)
HOOK(0x0047e0c0, "Projectile_Load", Projectile_Load_Entry, nullptr);

// 0x0047e320 (ecx = projectile; stack: record; ret 4). Projectile vt+0x1c (slot 7): 0x00457810 (eax = projectile),
// then record+0xf = 1 and record+0 = 0x4000. Returns the record.
uint32_t fn_0047e320(uint32_t o, uint32_t rec)
{
    orig::fn_00457810(o);
    By(rec + 0xf) = 1;
    D(rec) = 0x4000;
    return rec;
}
static uint32_t fn_0047e320_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0047e320(ecx, a0);
}
ABI_ENTRY_0047e320(fn_0047e320_Entry, fn_0047e320_Abi)
HOOK(0x0047e320, "fn_0047e320", fn_0047e320_Entry, nullptr);

// 0x0047e340 (ecx = projectile; stack: point; ret 4). Projectile vt+0x150 (slot 84): the target point +0x520..+0x528
// (read by 0x0047e4d0 when there is no target object). Not executed in recorded runs.
uint32_t Projectile_SetTargetPoint(uint32_t o, uint32_t pt)
{
    D(o + 0x520) = D(pt);
    D(o + 0x524) = D(pt + 4);
    D(o + 0x528) = D(pt + 8);
    return D(pt + 8);
}
static uint32_t Projectile_SetTargetPoint_Abi(uint32_t ecx, uint32_t a0)
{
    return Projectile_SetTargetPoint(ecx, a0);
}
ABI_ENTRY_0047e340(Projectile_SetTargetPoint_Entry, Projectile_SetTargetPoint_Abi)
HOOK(0x0047e340, "Projectile_SetTargetPoint", Projectile_SetTargetPoint_Entry, nullptr);

// 0x0047e360 (eax = projectile; returns st0): "speed" +0x460 (read by Projectile_Load).
float Projectile_GetSpeed(uint32_t o)
{
    return Fl(o + 0x460);
}
static float Projectile_GetSpeed_Abi(uint32_t eax)
{
    return Projectile_GetSpeed(eax);
}
ABI_ENTRY_0047e360(Projectile_GetSpeed_Entry, Projectile_GetSpeed_Abi)
HOOK(0x0047e360, "Projectile_GetSpeed", Projectile_GetSpeed_Entry, nullptr);

// 0x0047e370 (ecx = projectile; stack: owner; ret 4). Projectile vt+0x148 (slot 82): +0x504 = owner and +0x574 =
// owner, one reference each; +0x570 = 0. Not executed in recorded runs.
uint32_t Projectile_SetOwner(uint32_t o, uint32_t owner)
{
    D(o + 0x504) = owner;
    I(owner + 0x28) += 1;
    const uint32_t v = D(o + 0x504);
    D(o + 0x574) = v;
    I(v + 0x28) += 1;
    D(o + 0x570) = 0;
    return v;
}
static uint32_t Projectile_SetOwner_Abi(uint32_t ecx, uint32_t a0)
{
    return Projectile_SetOwner(ecx, a0);
}
ABI_ENTRY_0047e370(Projectile_SetOwner_Entry, Projectile_SetOwner_Abi)
HOOK(0x0047e370, "Projectile_SetOwner", Projectile_SetOwner_Entry, nullptr);

// 0x0047e3a0 (eax = projectile): the owner +0x504 (set by 0x0047e370).
uint32_t Projectile_GetOwner(uint32_t o)
{
    return D(o + 0x504);
}
static uint32_t Projectile_GetOwner_Abi(uint32_t eax)
{
    return Projectile_GetOwner(eax);
}
ABI_ENTRY_0047e3a0(Projectile_GetOwner_Entry, Projectile_GetOwner_Abi)
HOOK(0x0047e3a0, "Projectile_GetOwner", Projectile_GetOwner_Entry, nullptr);

// 0x0047e3b0 (ecx = projectile; stack: target object, bone name or 0; ret 8). Projectile vt+0x14c (slot 83): the
// target object +0x544 (one reference) and its bone name copied into +0x548 ("" without one). The function ends at
// 0x0047e3f1 (`ret 8`, outside the tracker's size). Not executed in recorded runs.
uint32_t Projectile_SetTarget(uint32_t o, uint32_t target, uint32_t bone)
{
    D(o + 0x544) = target;
    uint32_t r = bone;
    if (bone)
    {
        const char* s = reinterpret_cast<const char*>(bone);
        char* d = reinterpret_cast<char*>(o + 0x548);
        char c;
        do
        {
            c = *s++;
            *d++ = c;
        } while (c);
        r = u(s);
    }
    else
        By(o + 0x548) = 0;
    if (D(o + 0x544))
        I(D(o + 0x544) + 0x28) += 1;
    return r;
}
static uint32_t Projectile_SetTarget_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Projectile_SetTarget(ecx, a0, a1);
}
ABI_ENTRY_0047e3b0(Projectile_SetTarget_Entry, Projectile_SetTarget_Abi)
HOOK(0x0047e3b0, "Projectile_SetTarget", Projectile_SetTarget_Entry, nullptr);

// 0x0047e400 (ecx = projectile; stack: matrix, owner; ret 8). Projectile vt+0x140 (slot 80), the launch: vt+0x148
// (owner), 0x00457990 (al = 0; projectile, matrix) places it, vt+0x54(+0x514) reads the position, 0x00453150 (ecx =
// projectile, esi = &dir) the facing; the velocity speed * +0x468 * dir goes to 0x00458440 (eax = 0, ecx = &v, edx =
// projectile), then vt+0x154() (the start effect). Not executed in recorded runs.
uint32_t Projectile_Launch(uint32_t o, uint32_t matrix, uint32_t owner)
{
    float fr[6];
    std::memset(fr, 0, sizeof fr);
    const uint32_t r = VC(o, 0x148, owner);
    orig::fn_00457990(r & 0xffffff00u, o, matrix);
    VC(o, 0x54, o + 0x514);
    orig::fn_00453150(o, u(&fr[0]));
    fr[3] = Projectile_GetSpeed(o) * Fl(o + 0x468) * fr[0];
    fr[4] = Projectile_GetSpeed(o) * Fl(o + 0x468) * fr[1];
    fr[5] = Projectile_GetSpeed(o) * Fl(o + 0x468) * fr[2];
    orig::fn_00458440(0, u(&fr[3]), o);
    return VC(o, 0x154);
}
static uint32_t Projectile_Launch_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Projectile_Launch(ecx, a0, a1);
}
ABI_ENTRY_0047e400(Projectile_Launch_Entry, Projectile_Launch_Abi)
HOOK(0x0047e400, "Projectile_Launch", Projectile_Launch_Entry, nullptr);

// 0x0047e4a0 (ecx = projectile; returns al). "lifetime" +0x464 -= the frame time 0x00604a90; at or below 0 the
// projectile is removed (vt+0xd4(0)) and 1 is returned.
uint32_t Projectile_UpdateLifetime(uint32_t o)
{
    const float v = Fl(o + 0x464) - Fl(0x00604a90);
    Fl(o + 0x464) = v;
    if (v <= K(kZeroAddr))
    {
        const uint32_t r = VC(o, 0xd4, 0u);
        return (r & 0xffffff00u) | 1;
    }
    return 0;
}
static uint32_t Projectile_UpdateLifetime_Abi(uint32_t ecx)
{
    return Projectile_UpdateLifetime(ecx);
}
ABI_ENTRY_0047e4a0(Projectile_UpdateLifetime_Entry, Projectile_UpdateLifetime_Abi)
HOOK(0x0047e4a0, "Projectile_UpdateLifetime", Projectile_UpdateLifetime_Entry, nullptr);

// 0x0047e4d0 (esi = projectile). Distance to the target: the target object's bone +0x548 (or its origin; 0x00453c50
// matrix translation) or else the target point +0x520; |target - position +0x514| goes to +0x568 and +0x56c. Not
// executed in recorded runs.
uint32_t fn_0047e4d0(uint32_t o)
{
    alignas(4) uint8_t fr[0x50];
    std::memset(fr, 0, sizeof fr);
    const uint32_t S = u(fr);
    uint32_t r;
    float x;
    if (D(o + 0x544))
    {
        r = orig::fn_00453c50(D(o + 0x544), By(o + 0x548) ? o + 0x548 : 0, S + 0xc, 1, 0, 0, 1, 0, 0);
        x = Fl(S + 0x18);
        D(S + 4) = D(S + 0x28);
        D(S + 8) = D(S + 0x38);
    }
    else
    {
        D(S + 0) = D(o + 0x520);
        r = D(o + 0x528);
        x = Fl(S + 0);
        D(S + 4) = D(o + 0x524);
        D(S + 8) = D(o + 0x528);
    }
    const float dx = x - Fl(o + 0x514);
    const float dy = Fl(S + 4) - Fl(o + 0x518);
    const float dz = Fl(S + 8) - Fl(o + 0x51c);
    const float len = std::sqrt(dz * dz + dy * dy + dx * dx);
    Fl(o + 0x568) = len;
    Fl(o + 0x56c) = len;
    return r;
}
static uint32_t fn_0047e4d0_Abi(uint32_t esi)
{
    return fn_0047e4d0(esi);
}
ABI_ENTRY_0047e4d0(fn_0047e4d0_Entry, fn_0047e4d0_Abi)
HOOK(0x0047e4d0, "fn_0047e4d0", fn_0047e4d0_Entry, nullptr);

// 0x0047e580 (ecx = projectile). Projectile vt+0xbc (slot 47; per-frame, executed in recorded runs). Lifetime
// (0x0047e4a0: removed -> return); the hit list +0x570 is flushed (0x0047ecb0, all); a target +0x544 that vt+0xd8
// no longer keeps is dropped; with no distance yet (+0x568 == 0) 0x0047e4d0. When the list game+0xa84 +0xf00 is not
// empty (vt+0x34), its first entry steers the projectile: 0x00471970 toward that object's position (70, 70, 1),
// 0x00458000 velocity into +0x57c, 0x00471750 (mass twice) and 0x00455a30 into the handle +0x39c, 0x00455e70 (0, 0).
// Then the previous position +0x508 = +0x514, vt+0x54 reads the new one and the travelled distance accumulates in
// +0x538; with vt+0x168() and vt+0x144() <= +0x538 the projectile is removed. Otherwise the segment +0x508 ->
// +0x514 is ray-cast (Query_Init 0x00413cc0 etc., excluding the hit list +0x570 and owner chain +0x574, including
// game+0xa84 +0xe90; Ray_Cast 0x00413e10) and every hit is passed to vt+0x138(object, record, part) until that
// returns false. The 0x34-byte hit record: +4 point, +0x10 part, +0x18 direction (x, y of +0x52c, z 0.7,
// normalised), +0x24 "impact_force".
uint32_t Projectile_Update(uint32_t o)
{
    alignas(4) uint8_t fr[0xa0];
    std::memset(fr, 0, sizeof fr);
    const uint32_t S = u(fr);
    uint32_t r = Projectile_UpdateLifetime(o);
    if (static_cast<uint8_t>(r))
        return r;
    if (D(o + 0x570))
        fn_0047ecb0(o, 1);
    const uint32_t tgt = D(o + 0x544);
    if (tgt && !VB(tgt, 0xd8))
    {
        Unref(D(o + 0x544));
        D(o + 0x544) = 0;
    }
    if (Fl(o + 0x568) == K(kZeroAddr))
        fn_0047e4d0(o);
    if (VC(D(D(GameP() + 0xa84) + 0xf00), 0x34) != 0)
    {
        const uint32_t list = D(D(GameP() + 0xa84) + 0xf00);
        D(S + 0) = 0;
        D(S + 4) = 0;
        const uint32_t e = orig::fn_004e7340(list, S);
        if (e)
        {
            const uint32_t it1 = D(S + 4);
            VC(D(S + 0), 0xc, it1);
            I(it1 + 0x18) -= 1;
            VC(e, 0x54, S + 0xc);
            Call_00471970(o + 0x514, S, S + 0xc, 0, 0x428c0000, 0x428c0000, 0x3f800000, 0);
            orig::fn_00458000(o, o + 0x57c);
            const float m1 = orig::fn_004581c0(o);
            const float m2 = orig::fn_004581c0(o);
            Fl(S + 0) = orig::fn_00471750(S, o + 0x57c, S + 0x94, 0, fb(m2), fb(m1));
            D(o + 0x39c) = orig::fn_00455a30(S + 0x94, 0x10, o, D(o + 0x39c), D(S + 0), 0xffffffffu, 0);
            orig::fn_00455e70(o, 0, 0);
        }
        else if (D(S + 4))
            I(D(S + 4) + 0x18) -= 1;
    }
    D(o + 0x508) = D(o + 0x514);
    D(o + 0x50c) = D(o + 0x518);
    D(o + 0x510) = D(o + 0x51c);
    VC(o, 0x54, o + 0x514);
    Fl(S + 0) = Fl(o + 0x514) - Fl(o + 0x508);
    Fl(S + 4) = Fl(o + 0x518) - Fl(o + 0x50c);
    Fl(S + 8) = Fl(o + 0x51c) - Fl(o + 0x510);
    Fl(o + 0x538) = std::sqrt(Fl(S + 8) * Fl(S + 8) + Fl(S + 4) * Fl(S + 4) + Fl(S + 0) * Fl(S + 0)) + Fl(o + 0x538);
    if (VB(o, 0x168))
    {
        const float range = VF(o, 0x144);
        if (range <= Fl(o + 0x538))
            return VC(o, 0xd4, 0u);
    }
    orig::fn_00453150(o, o + 0x52c);
    orig::fn_00458000(o, o + 0x57c);
    const uint32_t dy = D(S + 4);
    const uint32_t q = S + 0x4c;  // the ray query (0x54 bytes)
    const uint32_t h = S + 0x18;  // the 0x34-byte hit record
    for (;;)
    {
        orig::fn_00413cc0(q, 1);
        D(q + 0x14) = D(S + 0);
        D(q + 0x1c) = D(S + 8);
        D(q + 0x10) = D(o + 0x510);
        By(q + 0x3d) = 0;
        D(q + 0x18) = dy;
        D(q + 0x08) = D(o + 0x508);
        D(q + 0x0c) = D(o + 0x50c);
        D(q + 0x20) = 0x3f800000;
        orig::fn_00413d70(D(o + 0x570), q);
        orig::fn_00413d40(D(o + 0x574), q);
        orig::fn_00413dc0(D(D(GameP() + 0xa84) + 0xe90), q);
        By(q + 0x3c) = 1;
        D(q + 0x40) = 2;
        r = orig::fn_00413e10(q);
        if (!static_cast<uint8_t>(r))
            return r;
        const float t = Fl(q + 0x24);
        Fl(S + 0x0c) = t * Fl(o + 0x52c) + Fl(o + 0x508);
        std::memset(P(h), 0, 0x34);
        D(h + 4) = D(S + 0x0c);
        Fl(S + 0x10) = t * Fl(o + 0x530) + Fl(o + 0x50c);
        D(h + 8) = D(S + 0x10);
        D(h + 0x10) = D(q + 0x38);
        Fl(S + 0x14) = t * Fl(o + 0x534) + Fl(o + 0x510);
        D(h + 0x0c) = D(S + 0x14);
        D(h + 0x18) = D(o + 0x52c);
        D(h + 0x1c) = D(o + 0x530);
        D(h + 0x24) = D(o + 0x470);
        D(h + 0x20) = D(o + 0x534);
        D(h + 0x20) = 0x3f333333;
        const float len = std::sqrt(Fl(h + 0x1c) * Fl(h + 0x1c) + Fl(h + 0x18) * Fl(h + 0x18) + K(0x005bced4));
        if (!(len == K(kZeroAddr)))
        {
            const float inv = K(kOneAddr) / len;
            Fl(h + 0x18) = Fl(h + 0x18) * inv;
            Fl(h + 0x1c) = Fl(h + 0x1c) * inv;
            Fl(h + 0x20) = inv * K(0x005bcd48);  // 0.7
        }
        r = VC(o, 0x138, D(q + 0x34), h, D(q + 0x44));
        if (!static_cast<uint8_t>(r))
            return r;
    }
}
static uint32_t Projectile_Update_Abi(uint32_t ecx)
{
    return Projectile_Update(ecx);
}
ABI_ENTRY_0047e580(Projectile_Update_Entry, Projectile_Update_Abi)
HOOK(0x0047e580, "Projectile_Update", Projectile_Update_Entry, nullptr);

// 0x0047e9b0 (esi = object; stack: local matrix; ret 4). With a parent (Object_GetParent 0x004539d0): world = local *
// parent matrix vt+0x60 (Matrix_Multiply 0x004e2910) and 0x00457990 (al = 0; object, world) places the object.
// Without a parent nothing happens. Not executed in recorded runs.
uint32_t fn_0047e9b0(uint32_t o, uint32_t local)
{
    uint32_t m[0x44 / 4];
    std::memset(m, 0, sizeof m);
    if (!orig::fn_004539d0(o))
        return 0;
    const uint32_t pm = VC(o, 0x60);
    const uint32_t r = orig::fn_004e2910(local, u(m), pm);
    return orig::fn_00457990(r & 0xffffff00u, o, u(m));
}
static uint32_t fn_0047e9b0_Abi(uint32_t esi, uint32_t a0)
{
    return fn_0047e9b0(esi, a0);
}
ABI_ENTRY_0047e9b0(fn_0047e9b0_Entry, fn_0047e9b0_Abi)
HOOK(0x0047e9b0, "fn_0047e9b0", fn_0047e9b0_Entry, nullptr);

// 0x0047e9f0 (eax = hit record, ecx = target, ebx = object; stack: out matrix; ret 4). Sticks the object to the
// target: for part -1 (record+0x10) out = object matrix vt+0x60 * inverse(target vt+0x5c matrix) and Object_Attach
// (eax = 0, ecx = target, edx = object; 0, 1, 0); otherwise the part's node (0x00458660, esi = part, edi = target,
// then 0x004eb670) and its matrix (0x00453c50) are used the same way, attaching with eax = node. Not executed in
// recorded runs.
uint32_t fn_0047e9f0(uint32_t rec, uint32_t target, uint32_t o, uint32_t out)
{
    if (!target)
        return rec;
    uint32_t a[0x44 / 4], b[0x44 / 4];
    std::memset(a, 0, sizeof a);
    std::memset(b, 0, sizeof b);
    const uint32_t part = D(rec + 0x10);
    if (part == 0xffffffffu)
    {
        const uint32_t pm = VC(o, 0x60);
        VC(target, 0x5c, u(a));
        orig::fn_004e2b40(u(a), u(b));
        orig::fn_004e2910(pm, out, u(b));
        return static_cast<uint32_t>(orig::fn_004539f0(0, target, o, 0, 1, 0));
    }
    const uint32_t node = orig::fn_00458660(part, target);
    if (!node)
        return 0;
    const uint32_t bone = orig::fn_004eb670(0, node);
    if (!bone)
        return 0;
    const uint32_t r = orig::fn_00453c50(target, bone, u(a), 1, 0, 1, 1, 0, 0);
    if (!static_cast<uint8_t>(r))
        return r;
    const uint32_t pm = VC(o, 0x60);
    orig::fn_004e2b40(u(a), u(b));
    orig::fn_004e2910(pm, out, u(b));
    return static_cast<uint32_t>(orig::fn_004539f0(bone, target, o, 0, 1, 0));
}
static uint32_t fn_0047e9f0_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t a0)
{
    return fn_0047e9f0(eax, ecx, ebx, a0);
}
ABI_ENTRY_0047e9f0(fn_0047e9f0_Entry, fn_0047e9f0_Abi)
HOOK(0x0047e9f0, "fn_0047e9f0", fn_0047e9f0_Entry, nullptr);

// 0x0047eae0 (ecx = projectile; stack: hit object, hit record, part; ret 0xc). Projectile vt+0x138 (slot 78), the hit
// handler called by 0x0047e580: 0x00457870 (al = 0, ecx = &record+4, edx = projectile) moves it to the hit point,
// +0x514 = the point, +0x454 = 0; with "area_damage" +0x474 != 0 AreaDamage_Apply 0x0047f020 (ebx = +0x474; object,
// point, projectile, impact force, owner, damage flags vt+0x13c, 0); a hit object takes the damage (0x0047eec0); then
// vt+0x158(object, part) (the end effect) and vt+0xd4(0). Returns al = 0.
uint32_t fn_0047eae0(uint32_t o, uint32_t obj, uint32_t rec, uint32_t part)
{
    const uint32_t pt = rec + 4;
    orig::fn_00457870(0, pt, o, 0, 0, 0);
    D(o + 0x514) = D(pt);
    D(o + 0x518) = D(pt + 4);
    D(o + 0x51c) = D(pt + 8);
    D(o + 0x454) = 0;
    if (!(Fl(o + 0x474) == K(kZeroAddr)))
    {
        const uint32_t flags = VC(o, 0x13c);  // the `push 0` before it becomes AreaDamage_Apply's last argument
        const uint32_t owner = Projectile_GetOwner(o);
        AreaDamage_Apply(o + 0x474, obj, pt, o, D(o + 0x470), owner, flags, 0);
    }
    if (obj)
        Projectile_ApplyHitDamage(obj, o, rec);
    VC(o, 0x158, obj, part);
    const uint32_t r = VC(o, 0xd4, 0u);
    return r & 0xffffff00u;
}
static uint32_t fn_0047eae0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0047eae0(ecx, a0, a1, a2);
}
ABI_ENTRY_0047eae0(fn_0047eae0_Entry, fn_0047eae0_Abi)
HOOK(0x0047eae0, "fn_0047eae0", fn_0047eae0_Entry, nullptr);

// 0x0047eba0 (ecx = projectile): damage flags from "allow_cartoon_death" +0x578 (0x80) and "damages_buildings"
// +0x579 (0x400 when false). Vt+0x13c of the projectile-like vtables (Projectile itself uses 0x0047f4d0).
uint32_t Projectile_GetDamageFlags(uint32_t o)
{
    uint32_t r = By(o + 0x578) ? 0x80u : 0u;
    if (!By(o + 0x579))
        r |= 0x400;
    return r;
}
static uint32_t Projectile_GetDamageFlags_Abi(uint32_t ecx)
{
    return Projectile_GetDamageFlags(ecx);
}
ABI_ENTRY_0047eba0(Projectile_GetDamageFlags_Entry, Projectile_GetDamageFlags_Abi)
HOOK(0x0047eba0, "Projectile_GetDamageFlags", Projectile_GetDamageFlags_Entry, nullptr);

// 0x0047ebd0 (ecx = projectile). Projectile vt+0xdc (slot 55; the same slot is PlayerObject_Remove): flushes the hit
// list +0x570 (0x0047ecb0, all = 0), drops +0x574, +0x544, +0x504, Object_Remove 0x004547b0, deletes the list
// (vt+0x10(1)) and clears +0x570.
uint32_t Projectile_Remove(uint32_t o)
{
    if (D(o + 0x570))
        fn_0047ecb0(o, 0);
    ClearRef(o + 0x574);
    ClearRef(o + 0x544);
    ClearRef(o + 0x504);
    uint32_t r = orig::fn_004547b0(o);
    if (D(o + 0x570))
        r = VC(D(o + 0x570), 0x10, 1u);
    D(o + 0x570) = 0;
    return r;
}
static uint32_t Projectile_Remove_Abi(uint32_t ecx)
{
    return Projectile_Remove(ecx);
}
ABI_ENTRY_0047ebd0(Projectile_Remove_Entry, Projectile_Remove_Abi)
HOOK(0x0047ebd0, "Projectile_Remove", Projectile_Remove_Entry, nullptr);

// 0x0047ecb0 (stack: projectile, keepLive (byte); ret 8). Empties the hit list +0x570 (iterator 0x004e7340 /
// 0x004e73c0): each entry (unless keepLive and its vt+0xd8 keeps it) loses a reference and is removed (list
// vt+0x28). Returns the iterator's second word (whose +0x18 count is decremented).
uint32_t fn_0047ecb0(uint32_t o, uint32_t keepLive)
{
    uint32_t it[2] = {0, 0};
    for (uint32_t e = orig::fn_004e7340(D(o + 0x570), u(it)); e; e = orig::fn_004e73c0(u(it)))
    {
        if (static_cast<uint8_t>(keepLive) && VB(e, 0xd8))
            continue;
        Unref(e);
        VC(D(o + 0x570), 0x28, e);
    }
    const uint32_t r = it[1];
    if (r)
        I(r + 0x18) -= 1;
    return r;
}
static uint32_t fn_0047ecb0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_0047ecb0(a0, a1);
}
ABI_ENTRY_0047ecb0(fn_0047ecb0_Entry, fn_0047ecb0_Abi)
HOOK(0x0047ecb0, "fn_0047ecb0", fn_0047ecb0_Entry, nullptr);

// 0x0047ed40 (ecx = projectile; stack: hit object, part; ret 8). Projectile vt+0x158 (slot 86), the end effect:
// "fx_end" +0x4c4 at the position (description flags 8, position at +0xc); "fx_end_material" +0x4e4 when there is a
// hit object or part (flags |0x800, +0x78 = part+0x10 or 0, +0x7c = object); then the noise 0x00404810 (ebx = owner;
// 200, 2, 0, &position). Returns al = 1. Not executed in recorded runs.
uint32_t fn_0047ed40(uint32_t o, uint32_t obj, uint32_t part)
{
    FxDesc d;
    std::memset(&d, 0, sizeof d);
    VC(o, 0x54, u(&d.w[3]));
    d.w[0] = 8;
    if (By(o + 0x4c4))
        orig::fn_0041cc60((o + 0x4c4) & 0xffffff00u, D(GameP() + 0xa90), o + 0x4c4, u(&d));
    if (By(o + 0x4e4) && (part || obj))
    {
        d.w[0] |= 0x800;
        uint32_t eax = 0;
        if (part)
            eax = D(part + 0x10);
        d.w[0x78 / 4] = eax;
        d.w[0x7c / 4] = obj;
        orig::fn_0041cc60(eax & 0xffffff00u, D(GameP() + 0xa90), o + 0x4e4, u(&d));
    }
    const uint32_t r = orig::fn_00404810(Projectile_GetOwner(o), 0x43480000, 0x40000000, 0, u(&d.w[3]));
    return (r & 0xffffff00u) | 1;
}
static uint32_t fn_0047ed40_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0047ed40(ecx, a0, a1);
}
ABI_ENTRY_0047ed40(fn_0047ed40_Entry, fn_0047ed40_Abi)
HOOK(0x0047ed40, "fn_0047ed40", fn_0047ed40_Entry, nullptr);

// 0x0047ee30 (ecx = projectile). Projectile vt+0x154 (slot 85), the start effect: "fx_start" +0x484 on the bone
// "fx_start_bone_name" +0x4a4 of the projectile (flags 4, +0x18 bone, +0x1c projectile) and the noise 0x00404810
// (ebx = owner; 200, 0, projectile, 0). Returns the effect or 0. Not executed in recorded runs.
uint32_t fn_0047ee30(uint32_t o)
{
    if (!By(o + 0x484) || !By(o + 0x4a4))
        return 0;
    FxDesc d;
    std::memset(&d, 0, sizeof d);
    d.w[0x18 / 4] = o + 0x4a4;
    d.w[0x1c / 4] = o;
    d.w[0] = 4;
    const uint32_t fx = orig::fn_0041cc60(u(&d) & 0xffffff00u, D(GameP() + 0xa90), o + 0x484, u(&d));
    if (fx)
        orig::fn_00404810(Projectile_GetOwner(o), 0x43480000, 0, o, 0);
    return fx;
}
static uint32_t fn_0047ee30_Abi(uint32_t ecx)
{
    return fn_0047ee30(ecx);
}
ABI_ENTRY_0047ee30(fn_0047ee30_Entry, fn_0047ee30_Abi)
HOOK(0x0047ee30, "fn_0047ee30", fn_0047ee30_Entry, nullptr);

// 0x0047eec0 (eax = victim, ebx = projectile; stack: hit record; ret 4). Damage of a direct hit: a player-owned
// projectile (owner == game+0x748) hitting a victim whose vt+0x10c() is true and whose type +0x418 is not 0x99 /
// 0x9a counts game+0x271c. record+0x14 = "area_damage" + "damage"; on a part (record+0x10 != -1) unless both
// multipliers are exactly 1, the part's bone name (0x00458660, 0x004eb670) selects "head"/"neck" (game+0x2714++,
// head_shot_multiplier * damage, flag 4) or "chest"/"belly" (torso_shot_multiplier * damage, flag 8). Then flags |=
// vt+0x13c(), +0x28 = projectile, +0x2c = owner and victim vt+0xd0(record).
uint32_t Projectile_ApplyHitDamage(uint32_t victim, uint32_t o, uint32_t h)
{
    if (Projectile_GetOwner(o) == D(GameP() + 0x748) && VB(victim, 0x10c))
    {
        const uint32_t type = D(victim + 0x418);
        if (type != 0x9a && type != 0x99)
            I(GameP() + 0x271c) += 1;
    }
    Fl(h + 0x14) = Fl(o + 0x474) + Fl(o + 0x46c);
    const uint32_t part = D(h + 0x10);
    if (part != 0xffffffffu && !(D(o + 0x53c) == 0x3f800000 && D(o + 0x540) == 0x3f800000))
    {
        const uint32_t node = orig::fn_00458660(part, victim);
        if (node)
        {
            const uint32_t name = orig::fn_004eb670(0, node);
            if (name)
            {
                auto stricmp = [](uint32_t a, uint32_t b) { return orig::fn_005448ee(0, a, b); };
                if (!stricmp(name, 0x005bac28) || !stricmp(name, 0x0059e480))
                {
                    I(GameP() + 0x2714) += 1;
                    D(h) |= 4;
                    Fl(h + 0x14) = Fl(o + 0x53c) * Fl(o + 0x46c);
                }
                else if (!stricmp(name, 0x0059e210) || !stricmp(name, 0x0059f908))
                {
                    D(h) |= 8;
                    Fl(h + 0x14) = Fl(o + 0x540) * Fl(o + 0x46c);
                }
            }
        }
    }
    D(h) |= VC(o, 0x13c);
    D(h + 0x28) = o;
    D(h + 0x2c) = Projectile_GetOwner(o);
    return VC(victim, 0xd0, h);
}
static uint32_t Projectile_ApplyHitDamage_Abi(uint32_t eax, uint32_t ebx, uint32_t a0)
{
    return Projectile_ApplyHitDamage(eax, ebx, a0);
}
ABI_ENTRY_0047eec0(Projectile_ApplyHitDamage_Entry, Projectile_ApplyHitDamage_Abi)
HOOK(0x0047eec0, "Projectile_ApplyHitDamage", Projectile_ApplyHitDamage_Entry, nullptr);

// 0x0047f020 (ebx = {damage, inner radius, outer radius, 1 / (outer - inner)} (Projectile +0x474..+0x480); stack:
// excluded object, centre, source, impact force, owner, flags, a6; caller pops). Area damage: unless outer < 0.01,
// outer < inner or damage == 0, every object of the list game+0xa84 +0xe94 other than the excluded object and the
// source, kept by vt+0xd8, gets its bounding sphere (vt+0x94(&centre, &radius, 1)); at a surface distance d <= outer
// it receives vt+0xd0(record): the 0x34-byte record {flags | a5 | 1, +4 centre, +0x10 part 0x00458720, +0x14 damage
// (full within inner, else damage * (1 - (d - inner) * k)), +0x18 direction (z 0.7, normalised), +0x24 impact force,
// +0x2c owner, +0x30 a6}. Returns the iterator's second word.
uint32_t AreaDamage_Apply(uint32_t a, uint32_t excluded, uint32_t c, uint32_t src, uint32_t impact, uint32_t owner,
                          uint32_t flags, uint32_t a6)
{
    alignas(4) uint8_t fr[0x5c];
    std::memset(fr, 0, sizeof fr);
    const uint32_t S = u(fr);
    if (Fl(a + 8) < K(0x005bcbec))
        return 0;
    if (Fl(a + 8) < Fl(a + 4))
        return 0;
    if (Fl(a) == K(kZeroAddr))
        return 0;
    D(S + 8) = 0;
    D(S + 0xc) = 0;
    for (uint32_t e = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xe94), S + 8); e; e = orig::fn_004e73c0(S + 8))
    {
        if (e == excluded || e == src)
            continue;
        if (!VB(e, 0xd8))
            continue;
        VC(e, 0x94, S + 0x1c, S + 4, 1u);
        Fl(S + 0x10) = Fl(S + 0x1c) - Fl(c);
        Fl(S + 0x14) = Fl(S + 0x20) - Fl(c + 4);
        Fl(S + 0x18) = Fl(S + 0x24) - Fl(c + 8);
        const float d = Normalize3(S + 0x10, S + 0x10) - Fl(S + 4);
        Fl(S + 0) = d;
        if (d > Fl(a + 8))
            continue;
        const uint32_t h = S + 0x28;
        std::memset(P(h), 0, 0x34);
        D(h + 0x10) = orig::fn_00458720(e, 0);
        D(h + 0x1c) = D(S + 0x14);
        D(h + 0x20) = D(S + 0x18);
        D(h + 0x18) = D(S + 0x10);
        D(h + 0x24) = impact;
        D(h + 4) = D(c);
        D(h + 0x0c) = D(c + 8);
        D(h + 0x30) = a6;
        D(h + 8) = D(c + 4);
        D(h + 0x20) = 0x3f333333;
        Normalize3(h + 0x18, h + 0x18);
        D(h + 0x2c) = owner;
        D(h) = D(h) | (flags | 1);
        if (d <= Fl(a + 4))
            D(h + 0x14) = D(a);
        else
            Fl(h + 0x14) = (K(kOneAddr) - (d - Fl(a + 4)) * Fl(a + 0xc)) * Fl(a);
        VC(e, 0xd0, h);
    }
    const uint32_t r = D(S + 0xc);
    if (r)
        I(r + 0x18) -= 1;
    return r;
}
static uint32_t AreaDamage_Apply_Abi(uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                     uint32_t a5, uint32_t a6)
{
    return AreaDamage_Apply(ebx, a0, a1, a2, a3, a4, a5, a6);
}
ABI_ENTRY_0047f020(AreaDamage_Apply_Entry, AreaDamage_Apply_Abi)
HOOK(0x0047f020, "AreaDamage_Apply", AreaDamage_Apply_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Projectile variants whose vtables' slot 1 descriptor reads "CLASS(PickupObject)".

// 0x0047f210 (ecx = projectile; stack: owner; ret 4). Vtable 0x005a94e0 slot 162 (+0x288, the place of Projectile's
// vt+0x148 SetOwner in that class): +0x574 = 0, +0x504 = owner (one reference), a new list (PMS_heapAlloc 0x004e07f0
// "new" 0x10 bytes, 0x004e7010 with game+0xac4) into +0x570 holding the owner (another reference) and the projectile
// itself (one more reference), +0x588 = 0. A failed allocation leaves +0x570 = 0 and the original then calls
// through a null pointer (reproduced). Not executed in recorded runs.
uint32_t fn_0047f210(uint32_t o, uint32_t owner)
{
    D(o + 0x574) = 0;
    D(o + 0x504) = owner;
    I(owner + 0x28) += 1;
    const uint32_t mem = orig::fn_004e07f0(0x0063a210, 0x10, 0x18, 2, 0x0058b5e8);
    uint32_t list = 0;
    if (mem)
        list = orig::fn_004e7010(D(GameP() + 0xac4), 0, mem);
    D(o + 0x570) = list;
    VC(list, 0x18, D(o + 0x504));
    I(D(o + 0x504) + 0x28) += 1;
    const uint32_t r = VC(D(o + 0x570), 0x18, o);
    I(o + 0x28) += 1;
    D(o + 0x588) = 0;
    return r;
}
static uint32_t fn_0047f210_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0047f210(ecx, a0);
}
ABI_ENTRY_0047f210(fn_0047f210_Entry, fn_0047f210_Abi)
HOOK(0x0047f210, "fn_0047f210", fn_0047f210_Entry, nullptr);

// 0x0047f2a0 (ecx = projectile; stack: hit object, hit record, part; ret 0xc). Vtable 0x005a94e0 slot 158 (+0x278,
// the hit handler): a hit object in the list game+0xa84 +0xe8c (ObjectList_Contains 0x00454690) is added to the
// projectile's list +0x570 (one reference) and the projectile flies on. Area damage (0x0047f020) as in 0x0047eae0, the
// direct damage 0x0047eec0 and a kill count +0x588 when the victim's vt+0x10c() is true. When the projectile stops, a
// count above 2 spawns "pickup_health_2", above 1 "pickup_health_1" in front of the player (game+0x748: position
// vt+0x54 + facing (0x00453150) * 15, z + 4; placed by 0x00457870); then the projectile moves to the hit point,
// vt+0x158(object, part) and vt+0xd4(0). Returns 1 when it flies on. Not executed in recorded runs.
uint32_t fn_0047f2a0(uint32_t o, uint32_t obj, uint32_t rec, uint32_t part)
{
    float v[3];
    float dir[3];
    std::memset(v, 0, sizeof v);
    std::memset(dir, 0, sizeof dir);
    uint8_t stop = 1;
    if (obj && static_cast<uint8_t>(orig::fn_00454690(D(D(GameP() + 0xa84) + 0xe8c), obj)))
    {
        I(obj + 0x28) += 1;
        VC(D(o + 0x570), 0x18, obj);
        stop = 0;
    }
    if (!(Fl(o + 0x474) == K(kZeroAddr)))
    {
        const uint32_t flags = VC(o, 0x13c);  // the `push 0` before it becomes the last argument
        const uint32_t owner = Projectile_GetOwner(o);
        AreaDamage_Apply(o + 0x474, obj, rec + 4, o, D(o + 0x470), owner, flags, 0);
    }
    if (obj)
    {
        Projectile_ApplyHitDamage(obj, o, rec);
        if (VB(obj, 0x10c))
            I(o + 0x588) += 1;
    }
    if (stop)
    {
        const uint32_t n = D(o + 0x588);
        uint32_t pickup = 0;
        if (n > 2)
            pickup = CreateObject(0, 0x005b6e64, 0x3ff);
        else if (n > 1)
            pickup = CreateObject(0, 0x005b6e74, 0x3ff);
        if (pickup)
        {
            VC(D(GameP() + 0x748), 0x54, u(v));
            orig::fn_00453150(D(GameP() + 0x748), u(dir));
            v[0] = dir[0] * K(0x005bcd50) + v[0];  // 15.0
            v[1] = dir[1] * K(0x005bcd50) + v[1];
            v[2] = v[2] + K(0x005bcbe0);  // 4.0
            orig::fn_00457870(0, u(v), pickup, 0, 0, 0);
        }
        const uint32_t pt = rec + 4;
        orig::fn_00457870(0, pt, o, 0, 0, 0);
        D(o + 0x514) = D(pt);
        D(o + 0x518) = D(pt + 4);
        D(o + 0x51c) = D(pt + 8);
        VC(o, 0x158, obj, part);
        VC(o, 0xd4, 0u);
    }
    return stop == 0 ? 1u : 0u;
}
static uint32_t fn_0047f2a0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0047f2a0(ecx, a0, a1, a2);
}
ABI_ENTRY_0047f2a0(fn_0047f2a0_Entry, fn_0047f2a0_Abi)
HOOK(0x0047f2a0, "fn_0047f2a0", fn_0047f2a0_Entry, nullptr);

// 0x0047f4d0 (ecx = projectile). Projectile vt+0x13c (vtable 0x005ac790 slot 79): Projectile_GetDamageFlags 0x0047eba0
// | 0x20000. Not executed in recorded runs.
uint32_t fn_0047f4d0(uint32_t o)
{
    return Projectile_GetDamageFlags(o) | 0x20000;
}
static uint32_t fn_0047f4d0_Abi(uint32_t ecx)
{
    return fn_0047f4d0(ecx);
}
ABI_ENTRY_0047f4d0(fn_0047f4d0_Entry, fn_0047f4d0_Abi)
HOOK(0x0047f4d0, "fn_0047f4d0", fn_0047f4d0_Entry, nullptr);

// 0x0047f4e0 (ecx = projectile; stack: point; ret 4). Vtable 0x005a3b50 slot 244 (+0x3d0): scatters the point by a
// random offset in [-s, s] per axis (_rand * (2s) / 32768 - s, in that x87 order), s = min(game+0xd68 * game+0x28bc,
// game+0x28bc) * distance +0x568 * 0.01, and copies it to the target point +0x520. Not executed in recorded runs.
uint32_t fn_0047f4e0(uint32_t o, uint32_t pt)
{
    const uint32_t g = GameP();
    float s = Fl(g + 0xd68) * Fl(g + 0x28bc);
    if (s > Fl(g + 0x28bc))
        s = Fl(g + 0x28bc);
    s = s * Fl(o + 0x568) * K(0x005bcbec);
    const float hi = s;
    const float lo = -s;
    const float k = K(0x005bcbc0);  // 1/32768
    int32_t r = static_cast<int32_t>(Rand());
    Fl(pt) = static_cast<float>(r) * (hi - lo) * k + lo + Fl(pt);
    r = static_cast<int32_t>(Rand());
    Fl(pt + 4) = static_cast<float>(r) * (hi - lo) * k + lo + Fl(pt + 4);
    r = static_cast<int32_t>(Rand());
    Fl(pt + 8) = static_cast<float>(r) * (hi - lo) * k + lo + Fl(pt + 8);
    D(o + 0x520) = D(pt);
    D(o + 0x524) = D(pt + 4);
    D(o + 0x528) = D(pt + 8);
    return D(pt + 4);
}
static uint32_t fn_0047f4e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0047f4e0(ecx, a0);
}
ABI_ENTRY_0047f4e0(fn_0047f4e0_Entry, fn_0047f4e0_Abi)
HOOK(0x0047f4e0, "fn_0047f4e0", fn_0047f4e0_Entry, nullptr);

// 0x0047f5d0 (ecx = projectile; stack: template, a1, a2, a3; ret 0x10). Vtable 0x005a9790 (+0x158 by the .rdata
// scan): Projectile_Load 0x0047e0c0, then Object_setPhysicsEnabled 0x004577b0 (eax = projectile; 1). Not executed in
// recorded runs.
uint32_t fn_0047f5d0(uint32_t o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Projectile_Load(o, a0, a1, a2, a3);
    return static_cast<uint32_t>(orig::fn_004577b0(o, 1));
}
static uint32_t fn_0047f5d0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0047f5d0(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0047f5d0(fn_0047f5d0_Entry, fn_0047f5d0_Abi)
HOOK(0x0047f5d0, "fn_0047f5d0", fn_0047f5d0_Entry, nullptr);

// 0x0047f600 (ebx = projectile, esi = victim). Places the projectile at the victim's matrix (vt+0x5c, 0x00457990),
// sends the victim damage record {flags 0x1000, part -1, damage 1.0, owner game+0x748} (vt+0xd0) and plays on the
// projectile (0x00408e30, eax = 0x81, edx = -1) "sharkbite_grunt" when the victim's name contains "grunt"
// (0x004e5a40 on Object_GetName 0x00453270), else "sharkbite_captain". Not executed in recorded runs.
uint32_t fn_0047f600(uint32_t o, uint32_t victim)
{
    uint32_t m[0x44 / 4];
    uint32_t e[0xd];
    std::memset(m, 0, sizeof m);
    const uint32_t r = VC(victim, 0x5c, u(m));
    orig::fn_00457990(r & 0xffffff00u, o, u(m));
    std::memset(e, 0, sizeof e);
    e[0] |= 0x1000;
    e[0x10 / 4] = 0xffffffffu;
    e[0x14 / 4] = 0x3f800000;
    e[0x2c / 4] = D(GameP() + 0x748);
    VC(victim, 0xd0, u(e));
    const uint32_t name = orig::fn_00453270(victim);
    const uint32_t found = static_cast<uint32_t>(orig::fn_004e5a40(name, 0x005bc01c));
    return orig::fn_00408e30(0x81, o, 0xffffffffu, found ? 0x0059e470 : 0x0059e45c, 0, 0, 0, 0);
}
static uint32_t fn_0047f600_Abi(uint32_t ebx, uint32_t esi)
{
    return fn_0047f600(ebx, esi);
}
ABI_ENTRY_0047f600(fn_0047f600_Entry, fn_0047f600_Abi)
HOOK(0x0047f600, "fn_0047f600", fn_0047f600_Entry, nullptr);

// 0x0047f6b0 (ecx = object). Vtable 0x005a9790 slot 127 (+0x1fc): when bit 2 of +0x1c of the component 0x00408800
// (eax = 4, ecx = object) is set, tail-jumps to the object's vt+0xdc (no stack arguments; reproduced as
// call-then-return). Not executed in recorded runs.
uint32_t fn_0047f6b0(uint32_t o)
{
    const uint32_t c = orig::fn_00408800(4, o);
    if (By(c + 0x1c) & 2)
        return VC(o, 0xdc);
    return c;
}
static uint32_t fn_0047f6b0_Abi(uint32_t ecx)
{
    return fn_0047f6b0(ecx);
}
ABI_ENTRY_0047f6b0(fn_0047f6b0_Entry, fn_0047f6b0_Abi)
HOOK(0x0047f6b0, "fn_0047f6b0", fn_0047f6b0_Entry, nullptr);

// 0x0047f6d0 (ecx = projectile; stack: template, a1, a2, a3; ret 0x10). Vtable 0x005a5970 slot 244 (+0x3d0; the
// "wpn_armed_jaws_loop" class, guess: the armed jaws weapon): Projectile_Load, "submerge_attack_distance" +0x594 (40),
// "initial_arc_distance" +0x590 (20), "max_kills" +0x598 (Object_ReadValueProp 0x00453400, type 6), +0x59c = 0,
// ObjectList_Unlink from game+0xa84 +0xf1c, clears +0x588/+0x589/+0x5a0..+0x5a8, +0x58c = -3, +0x5ac = _rand *
// (15/32768) + 15. Not executed in recorded runs.
uint32_t fn_0047f6d0(uint32_t o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Projectile_Load(o, a0, a1, a2, a3);
    orig::fn_004534a0(a3, o + 0x594, o, 0x0059e440, a2, 0x42200000, 0);
    orig::fn_004534a0(a3, o + 0x590, o, 0x0059e428, a2, 0x41a00000, 0);
    orig::fn_00453400(6, a3, o + 0x598, o, 0x0059e41c, a2, 0);
    D(o + 0x59c) = 0;
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xf1c), o);
    By(o + 0x588) = 0;
    By(o + 0x589) = 0;
    D(o + 0x58c) = 0xc0400000;
    D(o + 0x5a0) = 0;
    D(o + 0x5a4) = 0;
    D(o + 0x5a8) = 0;
    const int32_t r = static_cast<int32_t>(Rand());
    Fl(o + 0x5ac) = static_cast<float>(r) * K(0x005bcdd4) + K(0x005bcd50);
    return static_cast<uint32_t>(r);
}
static uint32_t fn_0047f6d0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0047f6d0(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0047f6d0(fn_0047f6d0_Entry, fn_0047f6d0_Abi)
HOOK(0x0047f6d0, "fn_0047f6d0", fn_0047f6d0_Entry, nullptr);

// 0x0047f7b0 (stack: out vec3; ret 4). Vtable 0x005a5970 slot 327 (+0x51c): copies the vec3 at game+0xa6c +4 after
// the no-op 0x00407ff0 (a bare `ret`). Not executed in recorded runs.
uint32_t fn_0047f7b0(uint32_t out)
{
    const uint32_t a = D(GameP() + 0xa6c);
    orig::fn_00407ff0(0, 0, 0, 0);
    D(out) = D(a + 4);
    D(out + 4) = D(a + 8);
    D(out + 8) = D(a + 0xc);
    return D(a + 0xc);
}
static uint32_t fn_0047f7b0_Abi(uint32_t a0)
{
    return fn_0047f7b0(a0);
}
ABI_ENTRY_0047f7b0(fn_0047f7b0_Entry, fn_0047f7b0_Abi)
HOOK(0x0047f7b0, "fn_0047f7b0", fn_0047f7b0_Entry, nullptr);

// 0x0047f7e0 (ecx = projectile; stack: matrix, owner; ret 8). Vtable 0x005a5970 slot 318 (+0x4f8, the launch of that
// class): vt+0x148(owner), placed at the matrix (0x00457990), position vt+0x54(+0x514); the arc target = position -
// (m[1], m[5], m[9]) * "initial_arc_distance" +0x590 with x, y jittered by _rand * (10/32768) - 5; +0x460 = 50; the
// launch velocity 0x00471a00 (ebx = +0x57c, esi = &target, edi = position; 50, -64.348, 1) goes to
// Body_setLinearVelocity 0x00458510; vt+0x154(); 0x0047f920 when a target +0x544 exists; the loop sound
// "wpn_armed_jaws_loop" (game+0x2c vt+0xc4) is kept in +0x5b0. Not executed in recorded runs.
uint32_t fn_0047f7e0(uint32_t o, uint32_t m, uint32_t owner)
{
    float v[3];
    const uint32_t r = VC(o, 0x148, owner);
    orig::fn_00457990(r & 0xffffff00u, o, m);
    const uint32_t pos = o + 0x514;
    VC(o, 0x54, pos);
    const float n1 = -Fl(m + 4);
    const float n5 = -Fl(m + 0x14);
    const float n9 = -Fl(m + 0x24);
    v[0] = n1 * Fl(o + 0x590) + Fl(pos);
    v[1] = n5 * Fl(o + 0x590) + Fl(o + 0x518);
    v[2] = n9 * Fl(o + 0x590) + Fl(o + 0x51c);
    int32_t rr = static_cast<int32_t>(Rand());
    v[0] = static_cast<float>(rr) * K(0x005bce58) - K(0x005bcbdc) + v[0];
    rr = static_cast<int32_t>(Rand());
    D(o + 0x460) = 0x42480000;
    v[1] = static_cast<float>(rr) * K(0x005bce58) - K(0x005bcbdc) + v[1];
    orig::fn_00471a00(o + 0x57c, u(v), pos, 0x42480000, 0xc280b23a, 1);
    orig::fn_00458510(o + 0x57c, o);
    VC(o, 0x154);
    if (D(o + 0x544))
        fn_0047f920(o);
    const uint32_t snd = D(GameP() + 0x2c);
    const uint32_t h = VC(snd, 0xc4, 0x0059e408u, o, 1u, 0u, 0xffffffffu, 0u);
    D(o + 0x5b0) = h;
    return h;
}
static uint32_t fn_0047f7e0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0047f7e0(ecx, a0, a1);
}
ABI_ENTRY_0047f7e0(fn_0047f7e0_Entry, fn_0047f7e0_Abi)
HOOK(0x0047f7e0, "fn_0047f7e0", fn_0047f7e0_Entry, nullptr);

// 0x0047f920 (edx = projectile). Sends the target +0x544 a damage record {flags 0x2000, part -1, damage 1.0,
// +0x28 projectile, +0x2c game+0x748} through its vt+0xd0. Not executed in recorded runs.
uint32_t fn_0047f920(uint32_t o)
{
    uint32_t e[0xd];
    std::memset(e, 0, sizeof e);
    e[0] |= 0x2000;
    e[0x10 / 4] = 0xffffffffu;
    e[0x14 / 4] = 0x3f800000;
    e[0x2c / 4] = D(GameP() + 0x748);
    e[0x28 / 4] = o;
    return VC(D(o + 0x544), 0xd0, u(e));
}
static uint32_t fn_0047f920_Abi(uint32_t edx)
{
    return fn_0047f920(edx);
}
ABI_ENTRY_0047f920(fn_0047f920_Entry, fn_0047f920_Abi)
HOOK(0x0047f920, "fn_0047f920", fn_0047f920_Entry, nullptr);

// 0x0047f980 (ecx = projectile). Vtable 0x005a5970 slot 293 (+0x494, the remove of that class): the held object
// +0x5a0 loses a reference, is detached (Object_Detach 0x00453b20) and removed (vt+0xdc), then +0x5a4 and +0x544 are
// dropped, Projectile_Remove 0x0047ebd0, and the loop sound +0x5b0 is stopped (game+0x2c vt+0xd8(&+0x5b0, 1)). Not
// executed in recorded runs.
uint32_t fn_0047f980(uint32_t o)
{
    if (D(o + 0x5a0))
    {
        Unref(D(o + 0x5a0));
        orig::fn_00453b20(D(o + 0x5a0));
        VC(D(o + 0x5a0), 0xdc);
        D(o + 0x5a0) = 0;
    }
    ClearRef(o + 0x5a4);
    ClearRef(o + 0x544);
    Projectile_Remove(o);
    if (!D(o + 0x5b0))
        return o + 0x5b0;
    return VC(D(GameP() + 0x2c), 0xd8, o + 0x5b0, 1u);
}
static uint32_t fn_0047f980_Abi(uint32_t ecx)
{
    return fn_0047f980(ecx);
}
ABI_ENTRY_0047f980(fn_0047f980_Entry, fn_0047f980_Abi)
HOOK(0x0047f980, "fn_0047f980", fn_0047f980_Entry, nullptr);

// 0x0047fa70 (stack: projectile; ret 4). Target search of the jaws class (guess): from the player's position (game+0x748
// vt+0x54, z + 6) over the list game+0xa84 +0xe7c, every object with vt+0x10c() and vt+0x24c() true whose centre
// (vt+0x54, z + 5) lies within the projectile's range vt+0x144() and nearer than the best so far is ray-checked
// (Ray_CastQuery 0x00414110 along the normalised direction for distance + 5, with the object temporarily removed from
// the ignore list game+0xa84 +0xea0 by vt+0x14 / vt+0x28); a clear line makes it the best. The best becomes the
// target (vt+0x14c(object, 0)), then 0x0047e4d0 and 0x0047f920. Returns the iterator's second word. Not executed in
// recorded runs.
uint32_t fn_0047fa70(uint32_t o)
{
    alignas(4) uint8_t fr[0x64];
    std::memset(fr, 0, sizeof fr);
    const uint32_t S = u(fr);
    const uint32_t pl = D(GameP() + 0x748);
    D(S + 0x0c) = 0;
    D(S + 0x10) = 0;
    uint32_t best = 0;
    D(S + 0x54) = 0x60ad78ec;  // 1e20
    VC(pl, 0x54, S + 0x20);
    Fl(S + 0x28) = Fl(S + 0x28) + K(0x005bccf0);  // 6.0
    for (uint32_t e = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xe7c), S + 0x0c); e; e = orig::fn_004e73c0(S + 0x0c))
    {
        if (!VB(e, 0x10c) || !VB(e, 0x24c))
            continue;
        VC(e, 0x54, S + 0x2c);
        Fl(S + 0x34) = Fl(S + 0x34) + K(0x005bcbdc);  // 5.0
        Fl(S + 0x14) = Fl(S + 0x2c) - Fl(S + 0x20);
        Fl(S + 0x18) = Fl(S + 0x30) - Fl(S + 0x24);
        Fl(S + 0x1c) = Fl(S + 0x34) - Fl(S + 0x28);
        const float len = std::sqrt(Fl(S + 0x1c) * Fl(S + 0x1c) + Fl(S + 0x18) * Fl(S + 0x18) +
                                    Fl(S + 0x14) * Fl(S + 0x14));
        Fl(S + 0) = len;
        if (!(len == K(kZeroAddr)))
        {
            const float inv = K(kOneAddr) / Fl(S + 0);
            Fl(S + 0x14) = Fl(S + 0x14) * inv;
            Fl(S + 0x18) = Fl(S + 0x18) * inv;
            Fl(S + 0x1c) = Fl(S + 0x1c) * inv;
        }
        if (VF(o, 0x144) < Fl(S + 0))
            continue;
        if (Fl(S + 0) > Fl(S + 0x54))
            continue;
        VC(D(D(GameP() + 0xa84) + 0xea0), 0x14, e);
        D(S + 4) = 0;
        const uint32_t g = GameP();
        const uint32_t ignore = D(D(g + 0xa84) + 0xea0);
        const float reach = Fl(S + 0) + K(0x005bcbdc);
        const uint32_t hit = orig::fn_00414110(S + 0x20, S + 0x14, D(g + 0xa8c), fb(reach), S + 8, 0, S + 4, 0, 0, 0,
                                               ignore, 0, 0, 1, 2);
        if (static_cast<uint8_t>(hit) && D(S + 4) == e)
        {
            D(S + 0x54) = D(S + 0);
            best = e;
        }
        VC(D(D(GameP() + 0xa84) + 0xea0), 0x28, e);
    }
    if (best)
    {
        VC(o, 0x14c, best, 0u);
        fn_0047e4d0(o);
        fn_0047f920(o);
    }
    const uint32_t r = D(S + 0x10);
    if (r)
        I(r + 0x18) -= 1;
    return r;
}
static uint32_t fn_0047fa70_Abi(uint32_t a0)
{
    return fn_0047fa70(a0);
}
ABI_ENTRY_0047fa70(fn_0047fa70_Entry, fn_0047fa70_Abi)
HOOK(0x0047fa70, "fn_0047fa70", fn_0047fa70_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004788a0 (ecx = player). PlayerObject vt+0xbc (vtable 0x005aa888 slot 47, the slot of Projectile_Update): the
// player's per-frame update (8298 bytes; executed in recorded runs). The original frame (`and esp, -8; sub esp,
// 0x128`) is kept as one array; SP(x) is [esp+x] at the depth after the four register pushes. In order:
//  - timers game+0xae0, +0xb04 (health ratio +0x454/+0x458 <= 0.2: cue 0x00493b70 every second), 0x00478200 (dt);
//  - 0x0047a9a0; death: +0x588 counts 5 s down then 0x0044a580 (game+0xa64, 3); with +0x1bc the body is ragdolled
//    (0x00459000 / Body_addConstraints 0x00456570, flag 0x10 in four components 0x00408800, "fx_trotter_death",
//    0x00456050) or, with +0x421, only 0x00459000;
//  - action input 0x14 (0x0047a950 + 0x00414730): pub doorways (list +0xeac, 0x0046a5f0 / 0x0046a640 / 0x0046a6c0 /
//    0x0046a6f0 -> game+0xad8, early return) or the nearest usable object of list +0xeb0 ("player_tickerbomb" via
//    0x0044e380 / 0x0044e390 / 0x0044b8b0 and 0x0044e3a0, else ObjectList_Contains +0xea8 and vt+0x184 -> 0x00475910
//    and vt+0x178, early return);
//  - previous position +0xc18 = +0xc0c, vt+0x54(+0xc0c); move input 0x00414a30 (codes 5-4, 6-7) into SP14 / SP18;
//    turn input (0x004759e0 scaled by 1 - game+0xd58 / 4) into SP30, or the 0x2000000 timer game+0xc80 dropping
//    +0xad8; camera pitch 0x0040e2c0 + 0x00475990 * SP18 * dt clamped to +-1.5608 (0x0040e2d0); camera / punching
//    glove timers (game+0xcc4.., +0xafc "player_punchingglove" removal) or analog input 0x00414aa0 (codes 2-3, 0-1),
//    or the l1m1 camera path case; the stick vector is clamped to 1 and shaped by 0x00414c00 (+0x99c / +0x9a4);
//  - 0x0047aaf0 (turbo jump), speeds by state (+0x9ac.. walk, +0xa38.. bit 0x80, +0xa78.. bit 2, +0xa98.. bit 4,
//    0x100 with 0x00475fc0 blending, 0x200 none), the slow-down timer +0x9b4/+0x9b8, the local velocity
//    from the matrix vt+0x5c or 0x004f1830 / 0x004e3260 * vt+0x60, +0x468 = max speed; velocity through 0x00471810 /
//    0x00455a30 into +0x39c (or released, 0x00455a00); "FX_fs_roman_stop"; "FX_roman_slide" (+0x568, vt+0xb8,
//    Fx_Release 0x0041d030); steering toward the list game+0xa84 +0xf00 (0x00455aa0, SP30 turn);
//  - yaw: atan2 of the facing minus SP30 * dt * 10, a rotation matrix at the position for 0x00457990, angular
//    velocity 0 (0x00458580); camera roll +0x98c (0x0040e2f0) unless bit 4;
//  - weapons +0xc24 (0x004a1890 .. 0x004a1aa0, inputs 8..0x11, 0xc, 9; the jump table 0x0047a910: +0xae0 10 s timer,
//    punching glove ("wpn_puncher_glove_on", "wpn_puncher_ding", timers game+0xcc4.., attach at game+0xcfc /
//    game+0xcdc, flag 0x8000000), flag 0x800000, game+0xadc), fire (0x0047a920: recoil 300 * mass for kinds 3..5),
//    dry-fire sounds (0x0047a938);
//  - 0x0047b730, +0x974 = 0, 0x0040e2e0(0), animation 0x00476a10 / 0x00477570 and the stamp code +0x1d0 from the
//    cache +0x410 ("Error: Can't generate stamp code, no cache\n"), and the gravity impulse (state speed +0xa4c /
//    +0xa88 / +0xab4 * -64.348 + 64.348) * mass through 0x00455a30 into +0x5b4, whose result is returned.
// Evidence: disassembly 0x004788a0..0x0047a90c; jump tables read from .rdata. Inputs whose upper eax bytes are
// unknown are passed as described at the top of this file; fild of game+0xd58 is converted to float before the
// multiply (exact below 2^24).
static bool PlayerCanAct(uint32_t p)
{
    if (IsDead(p))
        return false;
    const uint32_t g = GameP();
    if (D(g + 0xac8) || By(g + 0xae4))
        return false;
    const uint32_t a68 = D(g + 0xa68);
    if (orig::fn_0040fab0(a68) == 2)
        return false;
    return !static_cast<uint8_t>(orig::fn_00410150(0, a68));
}

uint32_t PlayerObject_Update(uint32_t p)
{
    alignas(8) uint8_t fr[0x128];
    std::memset(fr, 0, sizeof fr);
#define SP(o) (u(fr) + (o) - 0x10)
    const float k0 = K(kZeroAddr);
    const float k1 = K(kOneAddr);
    uint32_t g = GameP();
    uint32_t r = 0;
    uint32_t f = 0;
    uint32_t w = 0;
    float A = 0.0f;
    float t = 0.0f;

    // 0x004788b0: timers.
    if (Fl(g + 0xae0) > k0)
    {
        const float v = Fl(g + 0xae0) - Fl(0x00604a90);
        Fl(g + 0xae0) = v;
        if (v <= k0)
            D(g + 0xae0) = 0;
    }
    if (!By(0x0064a728))
        By(0x0064a728) = 1;
    D(SP(0x38)) = D(0x00604a90);  // dt
    orig::fn_00478200(p, D(SP(0x38)));
    if (Fl(p + 0x454) / Fl(p + 0x458) <= K(0x005bcd2c))  // 0.2
    {
        const float v = Fl(p + 0xb04) - Fl(SP(0x38));
        Fl(p + 0xb04) = v;
        if (v < k0)
        {
            orig::fn_00493b70(0, 4, 0x005fb9bc, 0x0064a724, 0, p, 1, 0, 0);
            D(p + 0xb04) = 0x3f800000;
        }
    }
    // 0x00478977: death.
    fn_0047a9a0(p);
    if (!IsDead(p) || static_cast<uint8_t>(orig::fn_0044e600()))
        D(p + 0x588) = 0x40a00000;
    else
    {
        const float v = Fl(p + 0x588) - Fl(SP(0x38));
        Fl(p + 0x588) = v;
        if (v < k0)
        {
            D(p + 0x588) = 0;
            const uint32_t lm = D(GameP() + 0xa64);
            if (!static_cast<uint8_t>(orig::fn_0044a590(lm)))
                orig::fn_0044a580(lm, 3);
        }
        if (D(p + 0x1bc) && Fl(p + 0x588) > k0)
        {
            if (!By(p + 0x421))
            {
                const uint32_t s690 = D(p + 0x690);
                const uint32_t s804 = D(p + 0x804);
                D(p + 0x804) = 0;
                D(p + 0x690) = 0;
                orig::fn_00459000(1, p, 1, 1, 0);
                orig::fn_00456570(p);
                D(p + 0x690) = s690;
                D(p + 0x804) = s804;
                uint32_t c = 0;
                for (uint32_t i = 0; i < 4; ++i)
                {
                    c = orig::fn_00408800(i, p);
                    D(c + 0x1c) |= 0x10;
                }
                orig::fn_00459000((c & 0xffffff00u) | 1, p, 1, 0, 0);
                orig::fn_0040fa90(D(GameP() + 0xa68), 3);
                std::memset(P(SP(0xb8)), 0, 0x80);
                const uint32_t m = VC(p, 0x64, SP(0xe8));
                D(SP(0x12c)) = 0;
                D(SP(0xb8)) = 2;
                orig::fn_0041cc60(m & 0xffffff00u, D(GameP() + 0xa90), 0x0059eda0, SP(0xb8));
                orig::fn_00456050(p, p + 0x58c, D(p + 0x598), D(p + 0x59c));
            }
            else
            {
                D(p + 0x804) = 0;
                D(p + 0x690) = 0;
                orig::fn_00459000(1, p, 0, 0, 0);
            }
        }
    }
    // 0x00478b22: the action input.
    r = fn_0047a950(p);
    if (static_cast<uint8_t>(r))
        r = orig::fn_00414730((r & 0xffffff00u) | 1, 0x14, D(GameP() + 0xa80), 0x3f000000);
    if (static_cast<uint8_t>(r))
    {
        g = GameP();
        if (D(g + 0xac8) && !D(g + 0x26cc))
            VC(D(g + 0xacc), 0x17c);
        else
        {
            uint32_t best = 0;
            D(SP(0x24)) = 0;
            D(SP(0x28)) = 0;
            D(SP(0x30)) = 0x60ad78ec;  // 1e20
            VC(p, 0x54, SP(0x14));
            g = GameP();
            if (!By(g + 0xdd3))
            {
                for (uint32_t e = orig::fn_004e7340(D(D(g + 0xa84) + 0xeac), SP(0x24)); e;
                     e = orig::fn_004e73c0(SP(0x24)))
                {
                    orig::fn_0046a5f0(e, SP(0x3c));
                    Fl(SP(0x48)) = Fl(SP(0x14)) - Fl(SP(0x3c));
                    Fl(SP(0x4c)) = Fl(SP(0x18)) - Fl(SP(0x40));
                    Fl(SP(0x50)) = Fl(SP(0x1c)) - Fl(SP(0x44));
                    const float len = Normalize3(SP(0x48), SP(0x48));
                    const float rad = orig::fn_0046a640(e);
                    if (!(rad < len) && static_cast<uint8_t>(orig::fn_0046a6c0(e)))
                    {
                        orig::fn_0046a6f0(e);
                        g = GameP();
                        D(g + 0xad8) = e;
                        By(g + 0xae5) = 1;
                        By(g + 0xae6) = 1;
                        const uint32_t it1 = D(SP(0x28));
                        const uint32_t rr = VC(D(SP(0x24)), 0xc, it1);
                        I(it1 + 0x18) -= 1;
                        return rr;
                    }
                }
            }
            for (uint32_t e = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xeb0), SP(0x24)); e;
                 e = orig::fn_004e73c0(SP(0x24)))
            {
                VC(e, 0x54, SP(0x3c));
                const float dx = Fl(SP(0x14)) - Fl(SP(0x3c));
                const float dy = Fl(SP(0x18)) - Fl(SP(0x40));
                const float dz = Fl(SP(0x1c)) - Fl(SP(0x44));
                Fl(SP(0x10)) = std::sqrt(dz * dz + dy * dy + dx * dx);
                bool generic = true;
                bool pick = false;
                g = GameP();
                if (By(g + 0xd90) && !orig::fn_005448ee(0, orig::fn_00453270(e), 0x005b15ac))
                {
                    generic = false;
                    if (static_cast<uint8_t>(orig::fn_0044e380(e)) && !static_cast<uint8_t>(orig::fn_0044e390(e)))
                    {
                        const uint32_t n = orig::fn_0044b8b0(e);
                        D(SP(0x48)) = n;
                        if (!(UFloat(n) < Fl(SP(0x10))) && Fl(SP(0x30)) > Fl(SP(0x10)))
                        {
                            D(SP(0x30)) = D(SP(0x10));
                            pick = true;
                        }
                    }
                }
                if (generic)
                {
                    g = GameP();
                    if (!By(g + 0xdd3) && static_cast<uint8_t>(orig::fn_00454690(D(D(g + 0xa84) + 0xea8), e)) &&
                        VB(e, 0x184) && Fl(SP(0x30)) > Fl(SP(0x10)))
                    {
                        D(SP(0x30)) = D(SP(0x10));
                        pick = true;
                    }
                }
                if (pick)
                    best = e;
            }
            if (best)
            {
                if (!orig::fn_005448ee(0, orig::fn_00453270(best), 0x005b15ac))
                    orig::fn_0044e3a0(best);
                else if (static_cast<uint8_t>(orig::fn_00454690(D(D(GameP() + 0xa84) + 0xea8), best)))
                {
                    orig::fn_00475910(p, 1);
                    VC(best, 0x178);
                    const uint32_t it1 = D(SP(0x28));
                    if (!it1)
                        return 0;
                    I(it1 + 0x18) -= 1;
                    return it1;
                }
            }
            if (D(SP(0x28)))
                I(D(SP(0x28)) + 0x18) -= 1;
        }
    }
    // 0x00478e00: position history and the move input.
    D(p + 0xc18) = D(p + 0xc0c);
    D(p + 0xc1c) = D(p + 0xc10);
    D(p + 0xc20) = D(p + 0xc14);
    VC(p, 0x54, p + 0xc0c);
    if (PlayerCanAct(p))
    {
        Fl(SP(0x14)) = orig::fn_00414a30(D(GameP() + 0xa80), 5, 0x3f800000, 0x3e570a3d, 0x3f000000);
        const float v4 = orig::fn_00414a30(D(GameP() + 0xa80), 4, 0x3f800000, 0x3e570a3d, 0x3f000000);
        Fl(SP(0x14)) = Fl(SP(0x14)) - v4;
        Fl(SP(0x18)) = orig::fn_00414a30(D(GameP() + 0xa80), 6, 0x3f800000, 0x3e570a3d, 0x3f000000);
        const float v7 = orig::fn_00414a30(D(GameP() + 0xa80), 7, 0x3f800000, 0x3e570a3d, 0x3f000000);
        Fl(SP(0x18)) = Fl(SP(0x18)) - v7;
    }
    else
    {
        D(SP(0x18)) = 0;
        D(SP(0x14)) = 0;
    }
    // 0x00478f7d: the turn input.
    g = GameP();
    if (PlayerCanAct(p))
    {
        g = GameP();
        f = D(p + 0xa34);
        if (f & 0x7800000)
        {
            D(SP(0x30)) = 0;
            if (f & 0x2000000)
            {
                const float v = Fl(g + 0xc80) - Fl(SP(0x38));
                Fl(g + 0xc80) = v;
                if (v <= k0)
                {
                    const uint32_t e = D(p + 0xa34) & 0xfdffffffu;
                    D(p + 0xa34) = e;
                    D(p + 0xa34) = e | 0x4000000;
                    if (D(p + 0xad8))
                    {
                        Unref(D(p + 0xad8));
                        orig::fn_00453b20(D(p + 0xad8));
                        VC(D(p + 0xad8), 0xdc);
                        D(p + 0xad8) = 0;
                    }
                    orig::fn_004a0e00(1, p + 0xc24);
                    g = GameP();
                }
            }
        }
        else
        {
            const float aim = CallSt0Edx(0x004759e0, p);
            const float x = aim * (k1 - UFloat(D(g + 0xd58)) * K(0x005bcbe4)) * Fl(SP(0x14));
            Fl(SP(0x30)) = x;
            if (!(x == k0))
            {
                D(p + 0xc04) = 0;
                D(p + 0xe5c) = 0;
            }
        }
    }
    else
        D(SP(0x30)) = 0;
    // 0x004790e4: camera pitch.
    {
        const uint32_t cam = D(g + 0xa68);
        const float a = CallSt0Edx(0x00475990, p) * Fl(SP(0x18)) * Fl(SP(0x38));
        const float b = orig::fn_0040e2c0(cam);
        Fl(SP(0x10)) = a + b;
        if (Fl(SP(0x10)) < K(0x005bcebc))
            D(SP(0x10)) = 0xbfc7c82d;
        else if (Fl(SP(0x10)) > K(0x005bcfc0))
            D(SP(0x10)) = 0x3fc7c82d;
        orig::fn_0040e2d0(cam, D(SP(0x10)));
    }
    // 0x00479142: analog input / punching glove timers.
    {
        bool skip = IsDead(p);
        g = GameP();
        if (!skip)
            skip = D(g + 0xac8) || By(g + 0xae4);
        if (!skip)
        {
            f = D(p + 0xa34);
            skip = (f & 0x200) || orig::fn_0040fab0(D(g + 0xa68)) == 2 || (f & 0x7800000);
        }
        if (skip)
        {
            D(SP(0x18)) = 0;
            D(SP(0x14)) = 0;
        }
        else if (f & 0x78000000)
        {
            D(SP(0x18)) = 0;
            D(SP(0x14)) = 0;
            D(SP(0x20)) = 0;
            D(SP(0x10)) = 0;
            bool active = false;
            if (D(g + 0xcc0))
            {
                D(SP(0x24)) = 0xcc4;
                do
                {
                    const uint32_t slot = orig::fn_00407fd0(D(SP(0x10)), D(g + 0xa6c));
                    if (D(slot))
                    {
                        const bool dead = IsDead(D(slot));
                        g = GameP();
                        if (!dead)
                        {
                            const uint32_t off = D(SP(0x24));
                            const float v = Fl(g + off) - Fl(0x00604a90);
                            D(SP(0x20)) += 1;
                            Fl(g + off) = v;
                            if (v <= k0)
                            {
                                VC(D(slot), 0x124, 0u);
                                g = GameP();
                                D(g + off) = 0x47c35000;  // 100000
                            }
                        }
                    }
                    D(SP(0x10)) += 1;
                    D(SP(0x24)) += 4;
                } while (D(SP(0x10)) < D(g + 0xcc0));
                active = D(SP(0x20)) != 0;
            }
            if (!active && (D(p + 0xa34) & 0x8000000))
            {
                if (D(p + 0xafc))
                {
                    Unref(D(p + 0xafc));
                    orig::fn_00453b20(D(p + 0xafc));
                    VC(D(p + 0xafc), 0xdc);
                    D(p + 0xafc) = 0;
                }
                orig::fn_004a0e00(1, p + 0xc24);
                D(p + 0xa34) &= 0x87ffffffu;
                g = GameP();
                By(g + 0xcbc) = 0;
                By(g + 0xcbd) = 1;
            }
        }
        else if (static_cast<uint8_t>(orig::fn_00410150(SP(0x10), D(g + 0xa68))))
        {
            D(SP(0x18)) = 0;
            D(SP(0x14)) = 0;
            const uint32_t lev = orig::fn_0044a660(D(g + 0xa64), 0 /* cl = 0 */);
            if (!orig::fn_005448ee(0, lev, 0x005b1738) && !orig::fn_005448ee(0, D(SP(0x10)), 0x0059ed8c))
                D(SP(0x18)) = 0x3f800000;
        }
        else
        {
            Fl(SP(0x10)) = k1 - UFloat(D(g + 0xd58)) * K(0x005bcbe4);
            Fl(SP(0x14)) = orig::fn_00414aa0(D(g + 0xa80), 2, 0x3f800000, 0x3e570a3d, 0x3f000000);
            const float v3 = orig::fn_00414aa0(D(GameP() + 0xa80), 3, 0x3f800000, 0x3e570a3d, 0x3f000000);
            Fl(SP(0x14)) = Fl(SP(0x14)) - v3;
            Fl(SP(0x18)) = orig::fn_00414aa0(D(GameP() + 0xa80), 0, 0x3f800000, 0x3e570a3d, 0x3f000000);
            const float v1 = orig::fn_00414aa0(D(GameP() + 0xa80), 1, 0x3f800000, 0x3e570a3d, 0x3f000000);
            const float d = Fl(SP(0x18)) - v1;
            Fl(SP(0x14)) = Fl(SP(0x10)) * Fl(SP(0x14));
            Fl(SP(0x18)) = Fl(SP(0x10)) * d;
        }
    }
    // 0x0047944e: stick magnitude.
    Fl(SP(0x2c)) = std::sqrt(Fl(SP(0x14)) * Fl(SP(0x14)) + Fl(SP(0x18)) * Fl(SP(0x18)));
    if (Fl(SP(0x2c)) > K(0x005bcc68))
    {
        if (Fl(SP(0x2c)) > k1)
        {
            const float inv = k1 / Fl(SP(0x2c));
            D(SP(0x2c)) = 0x3f800000;
            Fl(SP(0x14)) = Fl(SP(0x14)) * inv;
            Fl(SP(0x18)) = inv * Fl(SP(0x18));
        }
        const uint32_t curve = static_cast<uint8_t>(orig::fn_00475840(p)) ? p + 0x9a4 : p + 0x99c;
        float v = orig::fn_00414c00(0, curve, D(SP(0x2c)));
        if (v > K(0x005bcfc4))
            v = v + K(0x005bcd94);
        if (v > k1)
            v = k1;
        v = v / Fl(SP(0x2c));
        Fl(SP(0x14)) = Fl(SP(0x14)) * v;
        Fl(SP(0x18)) = Fl(SP(0x18)) * v;
        Fl(SP(0x2c)) = v * Fl(SP(0x2c));
    }
    fn_0047aaf0(p, SP(0x14));
    // 0x0047952a: speeds by state.
    f = D(p + 0xa34);
    const uint32_t f100 = f & 0x100;
    if (f100)
    {
        const uint32_t type = D(p + 0x554);
        bool set = false;
        if (type == 8 || type == 0x13 || type == 0x14 || type == 0x16 || type == 0x17)
        {
            const float v = orig::fn_00475fc0(0, p);
            if (v > k0)
            {
                Fl(SP(0x10)) = v;
                set = true;
            }
        }
        if (!set)
            D(SP(0x10)) = 0;
        Fl(SP(0x20)) = k1 - Fl(SP(0x10));
        float ld;
        if (!(f & 6))
        {
            if (f & 0x80)
            {
                D(SP(0x24)) = D(p + 0x9c8);
                ld = Fl(p + 0x9c4);
            }
            else
            {
                D(SP(0x24)) = D(p + 0x9d0);
                ld = Fl(p + 0x9cc);
            }
        }
        else
        {
            D(SP(0x24)) = D(p + 0x9c0);
            ld = Fl(p + 0x9bc);
        }
        const float x = Fl(SP(0x10)) * Fl(p + 0x9d4);
        A = ld * Fl(SP(0x20)) + x;
        const float y = Fl(SP(0x10)) * Fl(p + 0x9d8);
        D(SP(0x10)) = D(p + 0x9b0);
        const float z = Fl(SP(0x24)) * Fl(SP(0x20));
        D(SP(0x24)) = D(p + 0x9ac);
        Fl(SP(0x20)) = y + z;
    }
    else if (f & 0x200)
    {
        A = k0;
        D(SP(0x20)) = 0;
        D(SP(0x24)) = 0;
        D(SP(0x10)) = 0;
    }
    else if (f & 4)
    {
        A = Fl(p + 0xaac);
        D(SP(0x20)) = D(p + 0xab0);
        D(SP(0x10)) = D(p + 0xaa0);
        D(SP(0x24)) = Fl(SP(0x18)) > k0 ? D(p + 0xa98) : D(p + 0xa9c);
    }
    else if (f & 2)
    {
        A = Fl(p + 0xa8c);
        D(SP(0x20)) = D(p + 0xa90);
        D(SP(0x10)) = D(p + 0xa7c);
        D(SP(0x24)) = D(p + 0xa78);
    }
    else if (f & 0x80)
    {
        A = Fl(p + 0xa40);
        D(SP(0x20)) = D(p + 0xa44);
        D(SP(0x10)) = D(p + 0xa3c);
        D(SP(0x24)) = D(p + 0xa38);
    }
    else
    {
        A = Fl(p + 0x9d4);
        D(SP(0x20)) = D(p + 0x9d8);
        D(SP(0x10)) = D(p + 0x9b0);
        D(SP(0x24)) = D(p + 0x9ac);
    }
    // 0x004796de: the slow-down timer +0x9b4 / +0x9b8.
    if (Fl(p + 0x9b4) > k0)
    {
        float wgt = k1 - Fl(p + 0x9b4) / Fl(p + 0x9b8);
        const float v = Fl(p + 0x9b4) - Fl(SP(0x38));
        Fl(p + 0x9b4) = v;
        if (v < k0)
            D(p + 0x9b4) = 0;
        if (!(wgt > k0))
            wgt = k0;
        const float om = k1 - wgt;
        A = A * wgt + om;
        Fl(SP(0x20)) = Fl(SP(0x20)) * K(0x005bcb4c) * om + wgt * Fl(SP(0x20));
    }
    // 0x00479773: forward (SP34) and strafe (t) speeds with the dead-zone ramps +0x998 / +0x994.
    Fl(SP(0x34)) = Fl(SP(0x10)) * Fl(SP(0x14));
    if (Fl(SP(0x14)) > k0)
        Fl(SP(0x34)) = (k1 - Fl(SP(0x14))) * Fl(p + 0x998) + Fl(SP(0x34));
    else if (Fl(SP(0x14)) < k0)
        Fl(SP(0x34)) = Fl(SP(0x34)) - (Fl(SP(0x14)) + k1) * Fl(p + 0x998);
    t = Fl(SP(0x24)) * Fl(SP(0x18));
    if (Fl(SP(0x18)) > k0)
        t = t + (k1 - Fl(SP(0x18))) * Fl(p + 0x994);
    else if (Fl(SP(0x18)) < k0)
        t = t - (Fl(SP(0x18)) + k1) * Fl(p + 0x994);
    {
        const float c2c = Fl(SP(0x2c));
        D(SP(0x3c)) = D(p + 0x4c0);
        D(SP(0x40)) = D(p + 0x4c4);
        D(SP(0x44)) = D(p + 0x4c8);
        Fl(SP(0x48)) = (k1 - c2c) * A + c2c * Fl(SP(0x20));
        Fl(SP(0x20)) = t;
    }
    if ((f & 0x80) && !f100)
    {
        VC(p, 0x5c, SP(0x70));
        D(SP(0x2c)) = 0;
    }
    else
    {
        orig::fn_004f1830(p + 0x574, 0x005fb9b0, SP(0x54));
        orig::fn_004e3260(SP(0xb8), SP(0x54));
        const uint32_t pm = VC(p, 0x60);
        orig::fn_004e2910(pm, SP(0x70), SP(0xb8));
        D(SP(0x2c)) = 0x40a00000;
    }
    // 0x004798c5: world velocity from the matrix.
    {
        const uint32_t f2 = D(p + 0xa34);
        float h = k0;
        if (f2 & 4)
            h = Fl(p + 0xaa4);
        else if (((f2 & 0x80) || (f2 & 0x200)) && !(f2 & 0x100))
        {
            D(SP(0x44)) = 0;
            D(SP(0x40)) = 0;
            D(SP(0x3c)) = 0;
        }
        const float s = h + Fl(SP(0x20));
        const float n1 = -(Fl(SP(0x74)) * s);
        Fl(SP(0x18)) = -(Fl(SP(0x84)) * s);
        Fl(SP(0x1c)) = -(Fl(SP(0x94)) * Fl(SP(0x20)));
        Fl(SP(0x14)) = Fl(SP(0x70)) * Fl(SP(0x34)) + n1;
        Fl(SP(0x18)) = Fl(SP(0x80)) * Fl(SP(0x34)) + Fl(SP(0x18));
        Fl(SP(0x1c)) = Fl(SP(0x90)) * Fl(SP(0x34)) + Fl(SP(0x1c));
        if (f2 & 4)
        {
            D(SP(0x1c)) = D(p + 0x4c8);
            if (Fl(SP(0x1c)) > k0)
                D(SP(0x1c)) = 0;
            else
            {
                const float lim = -Fl(p + 0xaa8);
                if (Fl(SP(0x1c)) < lim)
                    Fl(SP(0x1c)) = lim;
            }
        }
    }
    {
        const float v = Fl(SP(0x10)) > Fl(SP(0x24)) ? Fl(SP(0x10)) : Fl(SP(0x24));
        Fl(p + 0x468) = v;
        Fl(SP(0x10)) = v;
    }
    // 0x004799d5: the velocity handle +0x39c.
    if (Fl(p + 0x570) == k0 && D(p + 0x584))
        r = orig::fn_00455a00(D(p + 0x39c));
    else
    {
        const float m1 = orig::fn_004581c0(p);
        const float m2 = orig::fn_004581c0(p) * Fl(SP(0x48));
        Fl(SP(0x48)) = orig::fn_00471810(SP(0x14), SP(0x3c), 0x005fb9a4, SP(0x54), D(SP(0x10)), fb(m2), fb(m1),
                                         p + 0x574, D(SP(0x2c)));
        r = orig::fn_00455a30(SP(0x54), 0x10, p, D(p + 0x39c), D(SP(0x48)), 0xffffffffu, 0);
    }
    D(p + 0x39c) = r;
    if (Fl(p + 0x570) > k0)
    {
        D(SP(0x10)) = D(p + 0x47c);
        D(SP(0x24)) = D(p + 0x480);
        const float a = std::sqrt(Fl(p + 0x470) * Fl(p + 0x470) + Fl(p + 0x474) * Fl(p + 0x474));
        const float b = std::sqrt(Fl(SP(0x10)) * Fl(SP(0x10)) + Fl(SP(0x24)) * Fl(SP(0x24)));
        if (a - b < K(0x005bcd44))  // -20
            orig::fn_00478800(p, 0x0059ed78);
    }
    // 0x00479ae2: the slide effect.
    if (I(p + 0x584) > 0 && Fl(p + 0x4bc) > K(0x005bcb68) &&
        Fl(SP(0x18)) * Fl(SP(0x18)) + Fl(SP(0x14)) * Fl(SP(0x14)) < K(0x005bcbe0))
    {
        if (!D(p + 0x568))
        {
            D(SP(0xd4)) = p;
            D(SP(0xd0)) = 0;
            D(SP(0xb8)) = 0x24;
            D(SP(0xbc)) = 8;
            if (VB(p, 0xb8, SP(0x10), SP(0x54), SP(0x48), 0x3dcccccdu))
            {
                D(SP(0xb8)) = 0x824;
                D(SP(0x130)) = D(SP(0x48));
                D(SP(0x134)) = D(SP(0x10));
            }
            D(p + 0x568) = orig::fn_0041cc60(SP(0xb8) & 0xffffff00u, D(GameP() + 0xa90), 0x0059ed68, SP(0xb8));
        }
    }
    else if (D(p + 0x568))
        orig::fn_0041d030(D(GameP() + 0xa90), p + 0x568);
    // 0x00479bef: steering toward the list game+0xa84 +0xf00.
    if (VC(D(D(GameP() + 0xa84) + 0xf00), 0x34) != 0)
    {
        uint32_t best = 0;
        D(SP(0x48)) = 0;
        D(SP(0x4c)) = 0;
        D(SP(0x24)) = 0x60ad78ec;
        VC(p, 0x54, SP(0x54));
        uint32_t e = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xf00), SP(0x48));
        for (; e; e = orig::fn_004e73c0(SP(0x48)))
        {
            VC(e, 0x54, SP(0x64));
            Fl(SP(0x14)) = Fl(SP(0x64)) - Fl(SP(0x54));
            Fl(SP(0x18)) = Fl(SP(0x68)) - Fl(SP(0x58));
            Fl(SP(0x1c)) = Fl(SP(0x6c)) - Fl(SP(0x5c));
            Fl(SP(0x10)) = std::sqrt(Fl(SP(0x1c)) * Fl(SP(0x1c)) + Fl(SP(0x18)) * Fl(SP(0x18)) +
                                     Fl(SP(0x14)) * Fl(SP(0x14)));
            if (!(Fl(SP(0x10)) == k0))
            {
                const float inv = k1 / Fl(SP(0x10));
                Fl(SP(0x14)) = Fl(SP(0x14)) * inv;
                Fl(SP(0x18)) = Fl(SP(0x18)) * inv;
                Fl(SP(0x1c)) = Fl(SP(0x1c)) * inv;
            }
            if (VF(e, 0x144) < Fl(SP(0x10)))
                continue;
            if (!(Fl(SP(0x24)) > Fl(SP(0x10))))
                continue;
            D(SP(0x3c)) = D(SP(0x14));
            D(SP(0x40)) = D(SP(0x18));
            D(SP(0x44)) = D(SP(0x1c));
            D(SP(0x24)) = D(SP(0x10));
            best = e;
        }
        if (best)
        {
            Fl(SP(0x10)) = (k1 - Fl(SP(0x24)) / VF(best, 0x144)) * K(0x005bcda8);  // 200
            Fl(SP(0x14)) = orig::fn_004581c0(p) * Fl(SP(0x3c)) * Fl(SP(0x10));
            Fl(SP(0x18)) = orig::fn_004581c0(p) * Fl(SP(0x40)) * Fl(SP(0x10));
            D(SP(0x1c)) = e;  // 0: the loop's last iterator result
            orig::fn_00455aa0(SP(0x14), p, D(SP(0x38)), 0xffffffffu, e, 0x10);
            if (Fl(SP(0x30)) == k0)
            {
                orig::fn_00453150(p, SP(0x14));
                Fl(SP(0x10)) = Fl(SP(0x3c)) * Fl(SP(0x14)) + Fl(SP(0x40)) * Fl(SP(0x18));
                if (Fl(SP(0x10)) < K(0x005bcc38) && Fl(SP(0x24)) > k0)
                {
                    const float c = Fl(SP(0x3c)) * Fl(SP(0x18)) - Fl(SP(0x40)) * Fl(SP(0x14));
                    const float x = Fl(SP(0x10)) - K(0x005bcc38);
                    Fl(SP(0x30)) = c > k0 ? x * K(0x005bcfbc) : x * K(0x005bcfb8);
                }
            }
        }
        if (D(SP(0x4c)))
            I(D(SP(0x4c)) + 0x18) -= 1;
    }
    // 0x00479e2e: yaw and the placement matrix.
    orig::fn_00453150(p, SP(0x64));
    std::memset(P(SP(0x70)), 0, 0x44);
    D(SP(0x98)) = 0x3f800000;
    D(SP(0xac)) = 0x3f800000;
    Fl(SP(0x10)) = YawAngle(Fl(SP(0x64)), Fl(SP(0x68)), Fl(SP(0x30)) * Fl(SP(0x38)) * K(0x005bcb68));
    {
        const float sn = X87Sin(Fl(SP(0x10)));
        const float cs = X87Cos(Fl(SP(0x10)));
        Fl(SP(0x70)) = cs;
        Fl(SP(0x74)) = -sn;
        Fl(SP(0x80)) = sn;
        Fl(SP(0x84)) = cs;
    }
    VC(p, 0x54, SP(0x54));
    D(SP(0x7c)) = D(SP(0x54));
    D(SP(0x9c)) = D(SP(0x5c));
    D(SP(0x8c)) = D(SP(0x58));
    D(SP(0xb0)) = 0x11;
    orig::fn_00457990(D(SP(0x5c)) & 0xffffff00u, p, SP(0x70));
    D(SP(0x64)) = 0;
    D(SP(0x68)) = 0;
    D(SP(0x6c)) = 0;
    orig::fn_00458580(SP(0x64), p);
    if (!(By(p + 0xa34) & 4))
    {
        const float sp = std::sqrt(Fl(SP(0x20)) * Fl(SP(0x20)) + Fl(SP(0x34)) * Fl(SP(0x34)));
        const float v = (sp * Fl(p + 0x990) * Fl(SP(0x30)) + Fl(p + 0x98c) * K(0x005bcb7c)) * K(0x005bcbe4);
        Fl(p + 0x98c) = v;
        orig::fn_0040e2f0(D(GameP() + 0xa68), fb(-v));
    }
    // 0x00479f62: weapons.
    w = p + 0xc24;
    orig::fn_004a1890(w);
    {
        const float v = Fl(p + 0xc00) - Fl(SP(0x38));
        Fl(p + 0xc00) = v;
        if (v < k0)
            D(p + 0xc00) = 0;
    }
    if (IsDead(p))
        goto done;
    g = GameP();
    if (D(g + 0xac8))
        goto done;
    f = D(p + 0xa34);
    if (f & 0x200)
        goto done;
    if (orig::fn_0040fab0(D(g + 0xa68)) == 2 || (f & 0x7f800000))
        goto done;
    if (static_cast<uint8_t>(orig::fn_00410150(0, D(g + 0xa68))))
        goto done;
    if (static_cast<uint8_t>(orig::fn_004a1680(w)))
    {
        if (static_cast<uint8_t>(orig::fn_004a1690(w)))
            orig::fn_004a16b0(w);
        goto done;
    }
    if (static_cast<uint8_t>(orig::fn_004a17a0(w)))
    {
        if (static_cast<uint8_t>(orig::fn_004a17b0(w)))
            orig::fn_004a17d0(w);
        goto done;
    }
    g = GameP();
    if (D(g + 0xac8) || orig::fn_0040fab0(D(g + 0xa68)) == 2)
        goto done;
    {
        const uint32_t a80 = D(g + 0xa80);
        r = fn_0047bc80(10, a80);
        if (static_cast<uint8_t>(r))
        {
            r = orig::fn_00414730((r & 0xffffff00u) | 1, 10, a80, 0x3f000000);
            if (static_cast<uint8_t>(r))
                r = orig::fn_00475850(p);
        }
        else
        {
            r = orig::fn_00414810((r & 0xffffff00u) | 1, 0xb, a80, 0x3f000000);
            const uint8_t cl = static_cast<uint8_t>(r);
            r = orig::fn_00475840(p);
            if (static_cast<uint8_t>(r) != cl)
                r = orig::fn_00475850(p);
        }
    }
    r = orig::fn_00414730((r & 0xffffff00u) | 1, 0xe, D(GameP() + 0xa80), 0x3f000000);
    if (static_cast<uint8_t>(r))
    {
        r = orig::fn_004a0bb0(w, 0x3f000000);
        goto check_select;
    }
    r = orig::fn_00414730((r & 0xffffff00u) | 1, 0xf, D(GameP() + 0xa80), 0x3f000000);
    if (static_cast<uint8_t>(r))
    {
        r = orig::fn_004a0be0(w, 0x3f000000);
        goto check_select;
    }
    r = orig::fn_00414730((r & 0xffffff00u) | 1, 0x10, D(GameP() + 0xa80), 0x3f000000);
    if (static_cast<uint8_t>(r))
    {
        orig::fn_004a0bb0(p + 0xc54, 0);
        goto done;
    }
    r = orig::fn_00414730((r & 0xffffff00u) | 1, 0x11, D(GameP() + 0xa80), 0x3f000000);
    if (static_cast<uint8_t>(r))
    {
        orig::fn_004a0be0(p + 0xc54, 0);
        goto done;
    }
    if (VB(orig::fn_004a15f0(w), 0x164))
        goto done;
    r = orig::fn_00414810(1, 0xc, D(GameP() + 0xa80), 0x3f000000);
    if (static_cast<uint8_t>(r) && VB(orig::fn_004a15f0(w), 0x15c))
    {
        VC(orig::fn_004a15f0(w), 0x160);
        D(p + 0xa34) |= 0x190000;
        D(p + 0xc04) = 0;
        D(p + 0xe5c) = 0;
    }
    if (VB(orig::fn_004a15f0(w), 0x164))
        goto done;
    r = orig::fn_004a1380(orig::fn_004a1670(w), w);
    if (r == 0)
        r = orig::fn_004a0be0(p + 0xc54, 0);
    r = orig::fn_00414730((r & 0xffffff00u) | 1, 9, D(GameP() + 0xa80), 0x3f000000);
    if (!static_cast<uint8_t>(r) || !static_cast<uint8_t>(orig::fn_004a1a50(w)))
        goto fire;
    switch (orig::fn_004a1670(w) - 6)  // jump table 0x0047a910
    {
    case 0:  // 0x0047a4c2
        g = GameP();
        if (!(Fl(g + 0xae0) == k0))
            goto fire;
        D(g + 0xae0) = 0x41200000;
        goto mode_end;
    case 1:  // 0x0047a2bd: the punching glove
    {
        if (By(p + 0xa34) & 0xc0)
            goto fire;
        const uint32_t cnt = orig::fn_00407c20(D(GameP() + 0xa6c));
        g = GameP();
        D(g + 0xcc0) = cnt;
        if (!cnt)
            goto fire;
        VC(D(g + 0x2c), 0xd0, 0x0059ed50u, 0u, 0u, 0u, 0xffffffffu, 0u);
        VC(D(GameP() + 0x2c), 0xd0, 0x0059ed3cu, 0u, 0u, 0u, 0xffffffffu, 0u);
        g = GameP();
        D(SP(0x24)) = D(orig::fn_00407fd0(0, D(g + 0xa6c)) + 0x1c);
        if (D(g + 0xcc0))
        {
            D(SP(0x10)) = 0xcc4;
            uint32_t i = 0;
            do
            {
                const uint32_t slot = orig::fn_00407fd0(i, D(g + 0xa6c));
                if (slot && D(slot))
                {
                    if (!IsDead(D(slot)))
                    {
                        VC(D(slot), 0x120, SP(0x48));
                        const float inv = k1 / Fl(SP(0x48));
                        D(SP(0x2c)) = i;
                        const float a = Fl(SP(0x24)) * inv + UFloat(i) + k1;
                        g = GameP();
                        const uint32_t off = D(SP(0x10));
                        const float v = a - inv * Fl(slot + 0x1c);
                        Fl(g + off) = v;
                        if (v < k0)
                            D(g + off) = 0;
                    }
                    else
                        g = GameP();
                }
                ++i;
                D(SP(0x10)) += 4;
            } while (i < D(g + 0xcc0));
        }
        By(g + 0xcbc) = 1;
        By(g + 0xcbd) = 0;
        orig::fn_004a0e00(D(g + 0xcc0) & 0xffffff00u, p + 0xc24);
        const uint32_t glove = CreateObject(0, 0x005b0590, 0x400);
        g = GameP();
        D(p + 0xafc) = glove;
        orig::fn_004539f0(By(g + 0xcfc) ? g + 0xcfc : 0, p, glove, By(g + 0xcdc) ? g + 0xcdc : 0, 1, 0);
        I(D(p + 0xafc) + 0x28) += 1;
        D(p + 0xa34) |= 0x8000000;
        goto mode_end;
    }
    case 2:  // 0x0047a293
        if ((By(p + 0xa34) & 0xc0) || D(p + 0x580))
            goto fire;
        D(p + 0xa34) |= 0x800000;
        goto count;
    case 3:  // 0x0047a4ac
        if (D(GameP() + 0xadc))
            goto fire;
        goto mode_end;
    default:
        goto mode_end;
    }
mode_end:  // 0x0047a4e9
    w = p + 0xc24;
    orig::fn_004a1aa0(w);
    if ((orig::fn_004a1670(w) == 6 || orig::fn_004a1670(w) == 9 || orig::fn_004a1670(w) == 10) &&
        (D(p + 0x558) == 0 || D(p + 0x558) == 6) && !(D(p + 0xa34) & 0x58000))
        D(p + 0xa34) |= 0x220000;
count:  // 0x0047a541
    I(GameP() + 0x2718) += 1;
fire:  // 0x0047a555
    w = p + 0xc24;
    r = orig::fn_00414810(1, 8, D(GameP() + 0xa80), 0x3f000000);
    if (!static_cast<uint8_t>(r))
    {
        g = GameP();
        D(g + 0xd68) = 0;
        By(g + 0xcbd) = 0;
    }
    else if (!By(GameP() + 0xcbd))
    {
        if (VB(orig::fn_004a15f0(w), 0x138))
        {
            VC(orig::fn_004a15f0(w), 0x140);
            const uint32_t kind = orig::fn_004a1660(w);
            if (kind >= 3 && kind <= 5)  // jump table 0x0047a920: recoil
            {
                Fl(SP(0x10)) = orig::fn_004581c0(p) * K(0x005bcfb4);  // 300
                orig::fn_00453150(p, SP(0x64));
                Fl(SP(0x54)) = -(Fl(SP(0x64)) * Fl(SP(0x10)));
                Fl(SP(0x58)) = -(Fl(SP(0x68)) * Fl(SP(0x10)));
                Fl(SP(0x5c)) = Fl(SP(0x10)) * K(0x005bcd2c);
                orig::fn_00455aa0(SP(0x54), p, 0x3dcccccd, 0xffffffffu, 0, 0x10);
                D(p + 0x9b8) = 0x3f800000;
                D(p + 0x9b4) = 0x3f800000;
            }
            I(GameP() + 0x2718) += 1;
            const uint32_t v = D(p + 0xa34) | 0x8000;
            D(p + 0xa34) = v;
            D(p + 0xa34) = v & 0xffffbfffu;
            D(p + 0xc04) = 0;
            D(p + 0xe5c) = 0;
            D(p + 0xc00) = 0x3f000000;
        }
        if (orig::fn_004a1660(w) == 0)
        {
            g = GameP();
            Fl(g + 0xd68) = Fl(SP(0x38)) + Fl(g + 0xd68);
        }
    }
    // 0x0047a6af
    if (!(Fl(p + 0xc00) == k0))
        goto done;
    if (D(p + 0xa34) & 0x8000)
        goto done;
    if (!VB(orig::fn_004a15f0(w), 0x158))
        goto done;
    if (VB(orig::fn_004a15f0(w), 0x15c))
    {
        VC(orig::fn_004a15f0(w), 0x160);
        D(p + 0xa34) |= 0x190000;
        D(p + 0xc04) = 0;
        D(p + 0xe5c) = 0;
        goto done;
    }
    r = orig::fn_004a0be0(w, 0x3f000000);
    if (r != 0xffffffffu)
        goto selected;
    r = orig::fn_00414730((r & 0xffffff00u) | 1, 8, D(GameP() + 0xa80), 0x3f000000);
    if (!static_cast<uint8_t>(r))
        goto done;
    {
        uint32_t snd = 0;
        switch (orig::fn_004a1660(w))  // jump table 0x0047a938: dry fire
        {
        case 0:
            snd = 0x0059ed24;  // wpn_machgun_dry_fire_1
            break;
        case 1:
            snd = 0x0059ecf4;  // wpn_rifle_dry_fire_2
            break;
        case 2:
            snd = 0x0059ed0c;  // wpn_rifle_dry_fire_1
            break;
        case 3:
            snd = 0x0059ecc8;  // wpn_rocket_dry_fire_2
            break;
        case 4:
            snd = 0x0059ece0;  // wpn_mortar_dry_fire
            break;
        default:
            break;
        }
        if (snd)
            VC(D(GameP() + 0x2c), 0xd0, snd, 0u, 0u, 0u, 0xffffffffu, 0u);
    }
    goto done;
check_select:  // 0x0047a0dd
    if (r == 0xffffffffu)
        goto done;
selected:  // 0x0047a0e6
    orig::fn_00475910(p, 0);
    D(p + 0xa34) |= 0x440000;
    D(p + 0xc04) = 0;
    D(p + 0xe5c) = 0;
done:  // 0x0047a7fe
    fn_0047b730(p);
    D(p + 0x974) = 0;
    orig::fn_0040e2e0(D(GameP() + 0xa68), 0);
    if (!By(p + 0x421))
    {
        const uint32_t dt = D(SP(0x38));
        orig::fn_00476a10(p, D(SP(0x34)), fb(-Fl(SP(0x20))), 0, D(SP(0x30)), dt);
        orig::fn_00477570(p, dt);
        const uint32_t cache = D(p + 0x410);
        uint32_t stamp;
        if (cache)
        {
            if (++I(cache + 8) == 0)
                D(cache + 8) = 1;
            stamp = D(cache + 8);
        }
        else
        {
            GameLog(reinterpret_cast<const char*>(0x0058af8c));
            stamp = 0;
        }
        D(p + 0x1d0) = stamp;
    }
    f = D(p + 0xa34);
    if (f & 4)
        D(SP(0x10)) = D(p + 0xab4);
    else if (f & 2)
        D(SP(0x10)) = D(p + 0xa88);
    else
        D(SP(0x10)) = D(p + 0xa4c);
    D(SP(0x58)) = 0;
    D(SP(0x54)) = 0;
    {
        const float m = orig::fn_004581c0(p);
        Fl(SP(0x5c)) = m * (Fl(SP(0x10)) * K(0x005bcd64) - K(0x005bcd64));
    }
    r = orig::fn_00455a30(SP(0x54), 0x10, p, D(p + 0x5b4), 0x3f800000, 0xffffffffu, 0);
    D(p + 0x5b4) = r;
    return r;
#undef SP
}
static uint32_t PlayerObject_Update_Abi(uint32_t ecx)
{
    return PlayerObject_Update(ecx);
}
ABI_ENTRY_004788a0(PlayerObject_Update_Entry, PlayerObject_Update_Abi)
HOOK(0x004788a0, "PlayerObject_Update", PlayerObject_Update_Entry, nullptr);
