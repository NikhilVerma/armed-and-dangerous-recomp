// Recovered (static, batch game_05): pickup object "collect" virtuals (PickupObject vt+0x138 and neighbours),
// PlayerObject virtuals (load, matrices, release, weapon/camera helpers) and the player's weapon/camera helpers
// around 0x00475xxx-0x00478xxx. Compile-checked only (the game was not run); the oracle (tools/compare.py) is still
// to do. Functions the recorded runs never execute are marked "not executed in recorded runs". Vtable slots come from
// a scan of .rdata in game.unpacked.exe (vtable starts = constructor stores `mov [reg], imm32`); class names from the
// vtable's slot 1 descriptor (+0x14 "CLASS(Name)").
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
uint32_t& D(uint32_t base, uint32_t off)
{
    return *reinterpret_cast<uint32_t*>(base + off);
}
float& Fl(uint32_t base, uint32_t off)
{
    return *reinterpret_cast<float*>(base + off);
}
uint8_t& B(uint32_t base, uint32_t off)
{
    return *reinterpret_cast<uint8_t*>(base + off);
}
uint32_t G()
{
    return GameGlobal<uint32_t>(0x005fe490);
}
float K(uint32_t addr)
{
    return GameGlobal<float>(addr);
}
// x87 fsin / fcos of a single-precision angle, as the original computes them (as src/physics/physics_02.cpp).
float X87Sin(float angle)
{
    float r;
    __asm {
        fld angle
        fsin
        fstp r
    }
    return r;
}
float X87Cos(float angle)
{
    float r;
    __asm {
        fld angle
        fcos
        fstp r
    }
    return r;
}
// `fild dword; jge; fadd [0x005bcb28] (2^32)`: an unsigned 32-bit value loaded onto the x87 stack. Kept in double so
// the following single-precision add rounds once, as the original's (the game runs x87 at single precision).
double U32ToX87(uint32_t v)
{
    double d = static_cast<double>(static_cast<int32_t>(v));
    if (static_cast<int32_t>(v) < 0)
        d = static_cast<float>(d + static_cast<double>(K(0x005bcb28)));
    return d;
}
// The refcount release seen inline in several functions: --obj+0x28; when it reaches 0 and obj vt+0xd8() returns
// al = 0, 0x00487da0 (eax = obj) frees it.
void Ref_Release(uint32_t obj)
{
    if (--D(obj, 0x28) == 0)
    {
        if (!static_cast<uint8_t>(VCall<uint32_t>(P(obj), 0xd8 / 4)))
            orig::fn_00487da0(obj);
    }
}
}  // namespace

// Functions recovered later in this file, called directly by earlier ones.
uint32_t fn_00475a30(uint32_t obj, float amount);
uint32_t fn_00475840(uint32_t player);
uint32_t fn_004757b0(uint32_t p);
float fn_00475990(uint32_t player);
uint32_t fn_00475880(uint32_t player);
uint32_t fn_00475910(uint32_t player, uint32_t flag);

// ---------------------------------------------------------------------------------------------------------------
// PickupObject vt+0x138 "collect" for ammo pickups (0x004725e0, 0x00472630, 0x00472680, 0x004726d0, 0x00472720;
// ecx = pickup; vtables 0x005a2ab0, 0x005ac900, 0x005ae4b8, 0x005a9790, 0x005a6390, all CLASS(PickupObject)): with
// the player's weapon state p = g_game +0x748 +0xc24, 0x004a13a0(eax = pickup +0x49c (amount), ecx = weapon slot,
// p); when that returns al != 0: 0x004a12a0(eax = slot, edi = p re-read), Pickup_PlayAmmoSound 0x00472400 and al =
// 1, else al = 0. Weapon slots 6, 7, 9, 8, 10. Not executed in recorded runs.
static uint32_t Pickup_CollectAmmo(uint32_t pickup, uint32_t slot)
{
    const uint32_t g = G();
    const uint32_t r = orig::fn_004a13a0(D(pickup, 0x49c), slot, D(g, 0x748) + 0xc24);
    if (!static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    orig::fn_004a12a0(slot, D(g, 0x748) + 0xc24);
    return (orig::fn_00472400() & 0xffffff00u) | 1u;
}

uint32_t fn_004725e0(uint32_t pickup)
{
    return Pickup_CollectAmmo(pickup, 6);
}
static uint32_t fn_004725e0_Abi(uint32_t ecx)
{
    return fn_004725e0(ecx);
}
ABI_ENTRY_004725e0(fn_004725e0_Entry, fn_004725e0_Abi)
HOOK(0x004725e0, "fn_004725e0", fn_004725e0_Entry, nullptr);

uint32_t fn_00472630(uint32_t pickup)
{
    return Pickup_CollectAmmo(pickup, 7);
}
static uint32_t fn_00472630_Abi(uint32_t ecx)
{
    return fn_00472630(ecx);
}
ABI_ENTRY_00472630(fn_00472630_Entry, fn_00472630_Abi)
HOOK(0x00472630, "fn_00472630", fn_00472630_Entry, nullptr);

uint32_t fn_00472680(uint32_t pickup)
{
    return Pickup_CollectAmmo(pickup, 9);
}
static uint32_t fn_00472680_Abi(uint32_t ecx)
{
    return fn_00472680(ecx);
}
ABI_ENTRY_00472680(fn_00472680_Entry, fn_00472680_Abi)
HOOK(0x00472680, "fn_00472680", fn_00472680_Entry, nullptr);

uint32_t fn_004726d0(uint32_t pickup)
{
    return Pickup_CollectAmmo(pickup, 8);
}
static uint32_t fn_004726d0_Abi(uint32_t ecx)
{
    return fn_004726d0(ecx);
}
ABI_ENTRY_004726d0(fn_004726d0_Entry, fn_004726d0_Abi)
HOOK(0x004726d0, "fn_004726d0", fn_004726d0_Entry, nullptr);

uint32_t fn_00472720(uint32_t pickup)
{
    return Pickup_CollectAmmo(pickup, 10);
}
static uint32_t fn_00472720_Abi(uint32_t ecx)
{
    return fn_00472720(ecx);
}
ABI_ENTRY_00472720(fn_00472720_Entry, fn_00472720_Abi)
HOOK(0x00472720, "fn_00472720", fn_00472720_Entry, nullptr);

// 0x00472770 (no inputs; PickupObject vt+0x138 of vtable 0x005a3b50). g_game byte +0xdd0 = 1, Pickup_PlayAmmoSound
// 0x00472400, al = 1. Not executed in recorded runs.
uint32_t fn_00472770()
{
    B(G(), 0xdd0) = 1;
    return (orig::fn_00472400() & 0xffffff00u) | 1u;
}
static uint32_t fn_00472770_Abi()
{
    return fn_00472770();
}
ABI_ENTRY_00472770(fn_00472770_Entry, fn_00472770_Abi)
HOOK(0x00472770, "fn_00472770", fn_00472770_Entry, nullptr);

// 0x00472800 (ecx = pickup; PickupObject vt+0x138 of vtable 0x005af158). Health pickup: amount = (float)(unsigned)
// pickup +0x49c * g_game +0x55c; 0x00475a30(ecx = player g_game +0x748, amount); when it returns al != 0,
// Pickup_PlayHealthSound 0x00472790(eax = pickup, edi = 0) and al = 1, else al = 0. Not executed in recorded runs.
uint32_t fn_00472800(uint32_t pickup)
{
    const uint32_t g = G();
    const float amount = static_cast<float>(U32ToX87(D(pickup, 0x49c)) * Fl(g, 0x55c));
    const uint32_t r = fn_00475a30(D(g, 0x748), amount);
    if (!static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    return (orig::fn_00472790(pickup, 0, 0) & 0xffffff00u) | 1u;
}
static uint32_t fn_00472800_Abi(uint32_t ecx)
{
    return fn_00472800(ecx);
}
ABI_ENTRY_00472800(fn_00472800_Entry, fn_00472800_Abi)
HOOK(0x00472800, "fn_00472800", fn_00472800_Entry, nullptr);

// 0x00472850 (ecx = pickup; stack: target object; ret 4; PickupObject vt+0x13c of vtable 0x005af158). Health pickup
// applied to `target`: unless Object_IsDead 0x00454780(esi = target) or 0x004555c0(target) (max health, guess) <=
// 0x004555b0(target) (health, guess), target +0x454 = min-like: health + (float)(unsigned) pickup +0x49c when that
// is below the maximum, else the maximum (each re-read through its call); then Pickup_PlayHealthSound(eax = pickup,
// edi = target), al = 1. Not executed in recorded runs.
uint32_t fn_00472850(uint32_t pickup, uint32_t target)
{
    const uint32_t dead = orig::fn_00454780(target);
    if (static_cast<uint8_t>(dead))
        return dead & 0xffffff00u;
    {
        const float h = orig::fn_004555b0(target);
        const float m = orig::fn_004555c0(target);
        if (!(m > h))
            return 0;  // original: the fnstsw word
    }
    const double amount = U32ToX87(D(pickup, 0x49c));
    const float sum = static_cast<float>(orig::fn_004555b0(target) + amount);
    const float m = orig::fn_004555c0(target);
    float v;
    if (m > sum)
        v = static_cast<float>(orig::fn_004555b0(target) + amount);
    else
        v = orig::fn_004555c0(target);
    Fl(target, 0x454) = v;
    return (orig::fn_00472790(pickup, 0, target) & 0xffffff00u) | 1u;
}
static uint32_t fn_00472850_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00472850(ecx, a0);
}
ABI_ENTRY_00472850(fn_00472850_Entry, fn_00472850_Abi)
HOOK(0x00472850, "fn_00472850", fn_00472850_Entry, nullptr);

// 0x004728e0 (ecx = object; stack: a0..a3; ret 0x10; slot 0x18 of PickupObject vtables 0x005a9bb0 and 0x005acf18:
// guess Load). 0x00471e40(ecx = object, a0, a1, a2, a3) (the base version), ObjectList_Link 0x004546d0(esi =
// g_game +0xa84 +0xf1c, edi = object), Object_setPhysicsEnabled 0x004577b0(eax = object, 1). Not executed in
// recorded runs.
uint32_t fn_004728e0(uint32_t obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_00471e40(obj, a0, a1, a2, a3);
    orig::fn_004546d0(D(D(G(), 0xa84), 0xf1c), obj);
    return static_cast<uint32_t>(orig::fn_004577b0(obj, 1));
}
static uint32_t fn_004728e0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004728e0(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004728e0(fn_004728e0_Entry, fn_004728e0_Abi)
HOOK(0x004728e0, "fn_004728e0", fn_004728e0_Entry, nullptr);

// 0x00472930 (ecx = pickup; PickupObject vt+0x138 of vtable 0x005acf18). p = g_game +0x748 +0xc24: when
// 0x004a0ef0(eax = 3, edi = p) returns al != 0: 0x004a14e0(eax = pickup +0x49c, ecx = 3, p re-read),
// Pickup_PlayAmmoSound and al = 1; else returns 0x004a0ef0's eax. Not executed in recorded runs.
uint32_t fn_00472930(uint32_t pickup)
{
    const uint32_t g = G();
    const uint32_t r = orig::fn_004a0ef0(3, D(g, 0x748) + 0xc24);
    if (!static_cast<uint8_t>(r))
        return r;
    orig::fn_004a14e0(D(pickup, 0x49c), 3, D(g, 0x748) + 0xc24);
    return (orig::fn_00472400() & 0xffffff00u) | 1u;
}
static uint32_t fn_00472930_Abi(uint32_t ecx)
{
    return fn_00472930(ecx);
}
ABI_ENTRY_00472930(fn_00472930_Entry, fn_00472930_Abi)
HOOK(0x00472930, "fn_00472930", fn_00472930_Entry, nullptr);

// 0x00472980 (no inputs; PickupObject vt+0x138 of vtable 0x005a9bb0). When 0x0047b8b0(ecx = player g_game +0x748)
// returns al != 0, al = 0; else Pickup_PlayAmmoSound, 0x0047b8c0(eax = player re-read, 1), al = 1. Not executed in
// recorded runs.
uint32_t fn_00472980()
{
    const uint32_t r = orig::fn_0047b8b0(D(G(), 0x748));
    if (static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    orig::fn_00472400();
    return (orig::fn_0047b8c0(D(G(), 0x748), 1) & 0xffffff00u) | 1u;
}
static uint32_t fn_00472980_Abi()
{
    return fn_00472980();
}
ABI_ENTRY_00472980(fn_00472980_Entry, fn_00472980_Abi)
HOOK(0x00472980, "fn_00472980", fn_00472980_Entry, nullptr);

// 0x004729c0 (no inputs; PickupObject vt+0x138 of vtable 0x005a6778). player = g_game +0x748: when
// 0x0047b8b0(ecx = player) returns al = 0 or 0x0047b8e0(eax = player) returns al = 0: 0x0047b900(eax = al 1 over
// 0x0047b8e0's upper bytes, ecx = player), Pickup_PlayAmmoSound, al = 1. Otherwise al = 0. Not executed in recorded
// runs.
uint32_t fn_004729c0()
{
    const uint32_t player = D(G(), 0x748);
    uint32_t r = orig::fn_0047b8b0(player);
    if (static_cast<uint8_t>(r))
    {
        r = orig::fn_0047b8e0(player);
        if (static_cast<uint8_t>(r))
            return r & 0xffffff00u;
    }
    orig::fn_0047b900((r & 0xffffff00u) | 1u, player);
    return (orig::fn_00472400() & 0xffffff00u) | 1u;
}
static uint32_t fn_004729c0_Abi()
{
    return fn_004729c0();
}
ABI_ENTRY_004729c0(fn_004729c0_Entry, fn_004729c0_Abi)
HOOK(0x004729c0, "fn_004729c0", fn_004729c0_Entry, nullptr);

// 0x00472a00 (ecx = pickup; slot 0x2f of PickupObject vtable 0x005a3c90; executed in recorded runs). When byte
// +0x4bf (set by 0x00472aa0) is set: 0x004a07a0(eax = 0x0044a660(eax = g_game +0xa64, cl = 0), ecx = g_game, edi = 2,
// 1), g_game byte +0x2720 = 1, Pickup_PlayAmmoSound, +0x4bf = 0. Then 0x00472200(ecx = pickup); when byte +0x4be is
// clear: +0x4be = 1 and, when 0x004a0720(eax = 0x0044a660(g_game +0xa64, cl = 0), ecx = 2, ebx = g_game) returns
// al != 0, tail call pickup vt+0xdc() (removes itself, guess).
uint32_t fn_00472a00(uint32_t pickup)
{
    if (B(pickup, 0x4bf))
    {
        const uint32_t g = G();
        const uint32_t s = orig::fn_0044a660(D(g, 0xa64), pickup & 0xffffff00u);
        orig::fn_004a07a0(s, g, 2, 1);
        B(G(), 0x2720) = 1;
        orig::fn_00472400();
        B(pickup, 0x4bf) = 0;
    }
    const uint32_t r0 = orig::fn_00472200(pickup);
    const uint8_t done = B(pickup, 0x4be);
    if (done)
        return (r0 & 0xffffff00u) | done;
    B(pickup, 0x4be) = 1;
    const uint32_t g = G();
    const uint32_t s = orig::fn_0044a660(D(g, 0xa64), 0);
    const uint32_t r = orig::fn_004a0720(s, 2, g);
    if (!static_cast<uint8_t>(r))
        return r;
    return VCall<uint32_t>(P(pickup), 0xdc / 4);
}
static uint32_t fn_00472a00_Abi(uint32_t ecx)
{
    return fn_00472a00(ecx);
}
ABI_ENTRY_00472a00(fn_00472a00_Entry, fn_00472a00_Abi)
HOOK(0x00472a00, "fn_00472a00", fn_00472a00_Entry, nullptr);

// 0x00472aa0 (ecx = pickup; PickupObject vt+0x138 of vtable 0x005a3c90). byte +0x4bf = 1 (consumed by 0x00472a00),
// al = 1. Not executed in recorded runs.
uint32_t fn_00472aa0(uint32_t pickup)
{
    B(pickup, 0x4bf) = 1;
    return 1;  // original: mov al, 1 over the caller's eax
}
static uint32_t fn_00472aa0_Abi(uint32_t ecx)
{
    return fn_00472aa0(ecx);
}
ABI_ENTRY_00472aa0(fn_00472aa0_Entry, fn_00472aa0_Abi)
HOOK(0x00472aa0, "fn_00472aa0", fn_00472aa0_Entry, nullptr);

// 0x00472ab0 (ecx = player; PlayerObject vt+0xdc of vtable 0x005aa888: remove). +0x5b0 / +0x5b4 =
// 0x00455a00(edx = old value) (releases sound handles, guess); +0xafc: Object_Detach 0x00453b20(esi), refcount
// release, = 0; when +0x568: Fx_Release 0x0041d030(g_game +0xa90, &+0x568); refcount release of +0x580 (= 0);
// audio (g_game +0x2c) vt+0xd8(&+0xab8, 0) (stops a sound, guess); 0x0047b9f0(eax = al 0 over that result, ecx = &+0xabc, player,
// (u16)+0x394, 0); refcount releases of +0xad0 and +0xad4 (= 0); 0x004a0fb0(esi = +0xc24; its edx input is not read:
// 0x004a0b10 overwrites it); +0x464 = +0x460 = 0; tail jump to Object_Remove 0x004547b0(ecx = player). Not executed
// in recorded runs.
uint32_t PlayerObject_Remove(uint32_t p)
{
    D(p, 0x5b0) = orig::fn_00455a00(D(p, 0x5b0));
    D(p, 0x5b4) = orig::fn_00455a00(D(p, 0x5b4));
    if (D(p, 0xafc))
    {
        orig::fn_00453b20(D(p, 0xafc));
        Ref_Release(D(p, 0xafc));
        D(p, 0xafc) = 0;
    }
    if (D(p, 0x568))
        orig::fn_0041d030(D(G(), 0xa90), p + 0x568);
    if (const uint32_t o = D(p, 0x580))
    {
        Ref_Release(o);
        D(p, 0x580) = 0;
    }
    const uint32_t r = VCall<uint32_t>(P(D(G(), 0x2c)), 0xd8 / 4, p + 0xab8, 0u);
    orig::fn_0047b9f0(r & 0xffffff00u, p + 0xabc, p, Field<uint16_t>(P(p), 0x394), 0);
    if (const uint32_t o = D(p, 0xad0))
    {
        Ref_Release(o);
        D(p, 0xad0) = 0;
    }
    if (const uint32_t o = D(p, 0xad4))
    {
        Ref_Release(o);
        D(p, 0xad4) = 0;
    }
    orig::fn_004a0fb0(0, p + 0xc24);
    D(p, 0x464) = 0;
    D(p, 0x460) = 0;
    return orig::fn_004547b0(p);
}
static uint32_t PlayerObject_Remove_Abi(uint32_t ecx)
{
    return PlayerObject_Remove(ecx);
}
ABI_ENTRY_00472ab0(PlayerObject_Remove_Entry, PlayerObject_Remove_Abi)
HOOK(0x00472ab0, "PlayerObject_Remove", PlayerObject_Remove_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// PlayerObject vt+0x70..+0x7c (vtable 0x005aa888): a matrix = rotation about z by the player's angle +0x974 (A[0] =
// A[5] = cos, A[1] = -sin, A[4] = sin, A[10] = A[15] = 1, flags A[16] = 1; fsin / fcos) times a base matrix B from
// 0x00457f80 / 0x00457f40 (ecx = player, &B).
// Matrix_Multiply 0x004e2910(eax = &A, out, &B). Frame: angle +0x8, A +0xc..+0x50, B +0x50..+0x94.
static uint32_t PlayerObject_RotZTimes(uint32_t p, uint32_t out, uint32_t baseFn)
{
    float b[17];
    if (baseFn == 0x00457f80)
        orig::fn_00457f80(p, u(b));
    else
        orig::fn_00457f40(p, u(b));
    const float angle = Fl(p, 0x974);
    const float s = X87Sin(angle);
    uint32_t a[17];
    std::memset(a, 0, sizeof(a));
    a[10] = 0x3f800000u;
    a[15] = 0x3f800000u;
    a[16] = 1;
    const float c = X87Cos(angle);
    a[0] = f2u(c);
    a[1] = f2u(-s);
    a[4] = f2u(s);
    a[5] = f2u(c);
    return orig::fn_004e2910(u(a), out, u(b));
}

// 0x00473b60 (ecx = player; stack: out; ret 4; vt+0x70). out = RotZ(+0x974) * 0x00457f80 matrix. Not executed in
// recorded runs.
uint32_t fn_00473b60(uint32_t p, uint32_t out)
{
    return PlayerObject_RotZTimes(p, out, 0x00457f80);
}
static uint32_t fn_00473b60_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00473b60(ecx, a0);
}
ABI_ENTRY_00473b60(fn_00473b60_Entry, fn_00473b60_Abi)
HOOK(0x00473b60, "fn_00473b60", fn_00473b60_Entry, nullptr);

// 0x00473bf0 (ecx = player; vt+0x74). +0x5b8 = RotZ(+0x974) * 0x00457f80 matrix; returns &+0x5b8. Not executed in
// recorded runs.
uint32_t fn_00473bf0(uint32_t p)
{
    PlayerObject_RotZTimes(p, p + 0x5b8, 0x00457f80);
    return p + 0x5b8;
}
static uint32_t fn_00473bf0_Abi(uint32_t ecx)
{
    return fn_00473bf0(ecx);
}
ABI_ENTRY_00473bf0(fn_00473bf0_Entry, fn_00473bf0_Abi)
HOOK(0x00473bf0, "fn_00473bf0", fn_00473bf0_Entry, nullptr);

// 0x00473c80 (ecx = player; stack: out; ret 4; vt+0x78). out = RotZ(+0x974) * 0x00457f40 matrix. Not executed in
// recorded runs.
uint32_t fn_00473c80(uint32_t p, uint32_t out)
{
    return PlayerObject_RotZTimes(p, out, 0x00457f40);
}
static uint32_t fn_00473c80_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00473c80(ecx, a0);
}
ABI_ENTRY_00473c80(fn_00473c80_Entry, fn_00473c80_Abi)
HOOK(0x00473c80, "fn_00473c80", fn_00473c80_Entry, nullptr);

// 0x00473d10 (ecx = player; vt+0x7c). +0x5fc = RotZ(+0x974) * 0x00457f40 matrix; returns &+0x5fc. Not executed in
// recorded runs.
uint32_t fn_00473d10(uint32_t p)
{
    PlayerObject_RotZTimes(p, p + 0x5fc, 0x00457f40);
    return p + 0x5fc;
}
static uint32_t fn_00473d10_Abi(uint32_t ecx)
{
    return fn_00473d10(ecx);
}
ABI_ENTRY_00473d10(fn_00473d10_Entry, fn_00473d10_Abi)
HOOK(0x00473d10, "fn_00473d10", fn_00473d10_Entry, nullptr);

namespace
{
// |v| of a 3-float vector in the original x87 order ((x*x + y*y) + z*z, fsqrt).
float Len3(uint32_t v)
{
    const float x = Fl(v, 0), y = Fl(v, 4), z = Fl(v, 8);
    float s = x * x + y * y;
    s = s + z * z;
    return std::sqrt(s);
}
// The direction words of a side animation table pair (inlined twice in 0x00473da0): a = (1, -0.5, a[2]) scaled by
// 1 / sqrt(a[2]^2 + 1.25 (0x005bcf60)); b = (-1, b[1], b[2]) scaled by 1 / sqrt((b[2]^2 + b[1]^2) + 1), each only when
// the length is not 0.
void PlayerAnimTable_Normalize(uint32_t a, uint32_t b)
{
    const float la = std::sqrt(Fl(a, 8) * Fl(a, 8) + K(0x005bcf60));
    if (!(la == K(0x0056c05c)))
    {
        const float inv = K(0x005bcb2c) / la;
        Fl(a, 0) = inv;
        Fl(a, 4) = K(0x005bcbc8) * inv;
        Fl(a, 8) = Fl(a, 8) * inv;
    }
    float sb = Fl(b, 8) * Fl(b, 8) + Fl(b, 4) * Fl(b, 4);
    sb = sb + K(0x005bcb2c);
    const float lb = std::sqrt(sb);
    if (!(lb == K(0x0056c05c)))
    {
        const float inv = K(0x005bcb2c) / lb;
        Fl(b, 0) = K(0x005bcb5c) * inv;
        Fl(b, 4) = Fl(b, 4) * inv;
        Fl(b, 8) = Fl(b, 8) * inv;
    }
}
}  // namespace
// 0x00473da0 (ecx = player; stack: instance, matrix, properties (set), values; ret 0x10; PlayerObject vt+0x18 of
// vtable 0x005aa888: Load). Object_LoadProperties 0x004517b0 (same arguments); +0x458 = +0x454 = g_game +0x554 *
// +0x458 (health scale, guess); fn_00408e30(eax = 0x81, ecx = player, edx = -1, "Trotter_ref", 0, 0, 0, 0); then the
// float properties through Object_ReadValuePropDef 0x004534a0 (esi = values, edi = destination; player, name, set,
// default, 0) in the original order (names and defaults below; four exponent / bias pairs are read into the a3 / a1
// argument slots of the original and copied to +0x9dc/+0x9e0, +0x9e4/+0x9e8, +0x99c/+0x9a0, +0x9a4/+0x9a8; locals
// here). Scalings: turbo_anti_gravity, turbo_glide_anti_gravity and jump_anti_gravity * 0.5, the two twist limits *
// pi/180 (0x005bcba0); +0xa90 = turbo_movement_power, +0xa44 = jump_movement_power, turbo_glide_anti_gravity defaults
// to turbo_anti_gravity * 0.25. Four global 10-float animation speed tables (run 0x005feab4 -> copy 0x005fea8c, walk
// 0x005feb54 -> 0x005feb2c, side run 0x005fea64 -> 0x005fea3c, side walk 0x005feb04 -> 0x005feadc; the tables and
// copies are zeroed first, the anim_* properties fill words 4..9, words 0..2 are direction vectors set and normalised
// here). "topsy_camera_bone" (text, default "Camera03") -> +0xadc (Object_ReadStringProp 0x004533a0). Then state
// resets, four list registrations (0x004546d0, g_game +0xa84 +0xe88 / +0xe9c / +0xe90 / +0xe94), 0x00458270(edx =
// player, 0, 0, 1000.0), Object_ForceGround 0x00455860(edi = player, 1000.0, 10000.0), 0x0047b9f0(eax = al 0 over
// ForceGround's result, ecx = &+0xabc, player, (u16)+0x394, 0), fn_004757b0 (camera), vt+0x54 positions into +0x500,
// +0x50c, +0x518, velocities (Body_getLinearVelocity 0x00458000), direction vectors (0x00453150, 0x00453180), the
// +0x68c look-at state (0xba dwords) cleared with mode 0x29 / state 7, and the "Trotter_ref" animation's bones bound to
// the 10 entries of table 0x005fb760 (0x004e0f40 lookups; bone +8 |= 0x20). Returns +0x464. Not executed in recorded
// runs.
uint32_t PlayerObject_Load(uint32_t o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004517b0(o, a0, a1, a2, a3);
    {
        const float h = Fl(G(), 0x554) * Fl(o, 0x458);
        Fl(o, 0x458) = h;
        Fl(o, 0x454) = h;
    }
    orig::fn_00408e30(0x81, o, 0xffffffffu, 0x0059f8dc, 0, 0, 0, 0);
    const uint32_t set = a2;
    const uint32_t values = a3;
    const auto Rd = [o, set, values](uint32_t dst, uint32_t name, uint32_t def) {
        orig::fn_004534a0(values, dst, o, name, set, def, 0);
    };
    uint32_t tA = a3;  // the original's a3 argument slot
    uint32_t tB = a1;  // the original's a1 argument slot
    Rd(o + 0x5a8, 0x0059f8cc, 0x3e99999au);  // "friction_min" 0.3
    Rd(o + 0x5a4, 0x0059f8bc, 0x40000000u);  // "friction_max" 2
    Rd(o + 0x5ac, 0x0059f8a8, 0x40800000u);  // "friction_exponent" 4
    Rd(o + 0x9ac, 0x0059f898, 0x41200000u);  // "fwd_max_speed" 10
    Rd(o + 0x9b0, 0x0059f888, 0x41200000u);  // "side_max_speed" 10
    Rd(o + 0x994, 0x0059f874, 0x40800000u);  // "fwd_max_speed_min" 4
    Rd(o + 0x998, 0x0059f860, 0x40800000u);  // "side_max_speed_min" 4
    Rd(o + 0x9bc, 0x0059f848, 0);            // "landing_turbo_power_min" 0
    Rd(o + 0x9c0, 0x0059f830, 0);            // "landing_turbo_power_max" 0
    Rd(o + 0x9c4, 0x0059f818, 0);            // "landing_jump_power_min" 0
    Rd(o + 0x9c8, 0x0059f800, 0);            // "landing_jump_power_max" 0
    Rd(o + 0x9cc, 0x0059f7ec, 0);            // "landing_power_min" 0
    Rd(o + 0x9d0, 0x0059f7d8, 0);            // "landing_power_max" 0
    Rd(o + 0x9d4, 0x0059f7c4, 0x41200000u);  // "movement_power_min" 10
    Rd(o + 0x9d8, 0x0059f7b0, 0x41200000u);  // "movement_power_max" 10
    Rd(u(&tA), 0x0059f79c, 0x40000000u);     // "targeting_exponent" 2
    Rd(u(&tB), 0x0059f78c, 0);               // "targeting_bias" 0
    D(o, 0x9dc) = tA;
    D(o, 0x9e0) = tB;
    Rd(u(&tA), 0x0059f774, 0x40a00000u);  // "zoom_targeting_exponent" 5
    Rd(u(&tB), 0x0059f760, 0);            // "zoom_targeting_bias" 0
    D(o, 0x9e4) = tA;
    D(o, 0x9e8) = tB;
    Rd(u(&tA), 0x0059f74c, 0x40000000u);  // "control_exponent" 2
    Rd(u(&tB), 0x0059f73c, 0);            // "control_bias" 0
    D(o, 0x99c) = tA;
    D(o, 0x9a0) = tB;
    Rd(u(&tA), 0x0059f724, 0x40a00000u);  // "zoom_control_exponent" 5
    Rd(u(&tB), 0x0059f708, 0);            // "zoom_control_control_bias" 0
    D(o, 0x9a4) = tA;
    D(o, 0x9a8) = tB;
    Rd(o + 0xa2c, 0x0059f6f0, 0x3f000000u);  // "targeting_control_drag" 0.5
    Rd(o + 0xa30, 0x0059f6d4, 0x3e800000u);  // "zoom_targeting_control_drag" 0.25
    Rd(o + 0x9f0, 0x0059f6c4, 0x42200000u);  // "turn_power_min" 40
    Rd(o + 0x9f4, 0x0059f6b4, 0x42200000u);  // "turn_power_max" 40
    Rd(o + 0x9ec, 0x0059f6a4, 0x3dcccccdu);  // "turn_max_speed" 0.1
    Rd(o + 0x9f8, 0x0059f694, 0x3f800000u);  // "pitch_max_speed" 1
    Rd(o + 0x990, 0x0059f688, 0x3ba3d70au);  // "roll_factor" 0.005
    Rd(o + 0x9fc, 0x0059f66c, 0x3ccccccdu);  // "machinegun_turn_max_speed" 0.025
    Rd(o + 0xa14, 0x0059f650, 0x3dcccccdu);  // "machinegun_pitch_max_speed" 0.1
    Rd(o + 0xa00, 0x0059f638, 0x3ccccccdu);  // "sniper_turn_max_speed" 0.025
    Rd(o + 0xa18, 0x0059f620, 0x3dcccccdu);  // "sniper_pitch_max_speed" 0.1
    Rd(o + 0xa04, 0x0059f608, 0x3ccccccdu);  // "rifle_turn_max_speed" 0.025
    Rd(o + 0xa1c, 0x0059f5f0, 0x3dcccccdu);  // "rifle_pitch_max_speed" 0.1
    Rd(o + 0xa10, 0x0059f5d8, 0x3ccccccdu);  // "sharkgun_turn_max_speed" 0.025
    Rd(o + 0xa28, 0x0059f5bc, 0x3dcccccdu);  // "sharkgun_pitch_max_speed" 0.1
    Rd(o + 0xa0c, 0x0059f5a4, 0x3ccccccdu);  // "gurner_turn_max_speed" 0.025
    Rd(o + 0xa24, 0x0059f58c, 0x3dcccccdu);  // "gurner_pitch_max_speed" 0.1
    Rd(o + 0xa08, 0x0059f574, 0x3ccccccdu);  // "vindaloo_turn_max_speed" 0.025
    Rd(o + 0xa20, 0x0059f558, 0x3dcccccdu);  // "vindaloo_pitch_max_speed" 0.1
    Rd(o + 0xa78, 0x005b7544, 0x40000000u);  // "turbo_fwd_max_speed" 2
    Rd(o + 0xa7c, 0x0059f540, 0x40000000u);  // "turbo_side_max_speed" 2
    Rd(o + 0xa80, 0x005b7538, 0x432f0000u);  // "turbo_force" 175
    Rd(o + 0xa84, 0x0059f52c, 0x43960000u);  // "turbo_boost_force" 300
    Rd(o + 0xa94, 0x005b752c, 0x40400000u);  // "turbo_time" 3
    Rd(o + 0xa88, 0x005b7518, 0x3e800000u);  // "turbo_anti_gravity" 0.25
    Fl(o, 0xa88) = Fl(o, 0xa88) * K(0x005bcb4c);
    Rd(o + 0xaa8, 0x005b74e8, 0x41200000u);  // "turbo_glide_zvel" 10
    Rd(o + 0xaa4, 0x005b74d4, 0x41f00000u);  // "turbo_glide_xyvel" 30
    Rd(o + 0xaac, 0x0059f514, 0x41200000u);  // "turbo_glide_power_min" 10
    Rd(o + 0xab0, 0x0059f4fc, 0x41200000u);  // "turbo_glide_power_max" 10
    Rd(o + 0xa98, 0x005b74fc, 0x42480000u);  // "turbo_glide_fwd_max_speed" 50
    Rd(o + 0xa9c, 0x0059f4e0, 0x42200000u);  // "turbo_glide_bkd_max_speed" 40
    Rd(o + 0xaa0, 0x0059f4c4, 0x40800000u);  // "turbo_glide_side_max_speed" 4
    Rd(o + 0xa8c, 0x0059f4ac, 0x41200000u);  // "turbo_movement_power" 10
    {
        const float def = Fl(o, 0xa88) * K(0x005bcbe4);
        D(o, 0xa90) = D(o, 0xa8c);
        Rd(o + 0xab4, 0x005b74a4, f2u(def));  // "turbo_glide_anti_gravity" turbo_anti_gravity * 0.25
    }
    Fl(o, 0xab4) = Fl(o, 0xab4) * K(0x005bcb4c);
    Rd(o + 0xa38, 0x0059f498, 0x40000000u);  // "jump_fwd_max_speed" 2
    Rd(o + 0xa3c, 0x0059f484, 0x40000000u);  // "jump_side_max_speed" 2
    Rd(o + 0xa40, 0x0059f470, 0x41200000u);  // "jump_movement_power" 10
    D(o, 0xa44) = D(o, 0xa40);
    Rd(o + 0xa50, 0x0059f464, 0xc2480000u);  // "fall_speed" -50
    Rd(o + 0xa58, 0x0059f450, 0xc2480000u);  // "fall_speed_turbo" -50
    Rd(o + 0xa54, 0x0059f438, 0xc2480000u);  // "fall_speed_turbo_glided" -50
    Rd(o + 0xa5c, 0x0059f428, 0xc2480000u);  // "fall_speed_jump" -50
    Rd(o + 0xa64, 0x0059f414, 0x3dcccccdu);  // "fall_land_hard_time" 0.1
    Rd(o + 0xa68, 0x0059f408, 0x42200000u);  // "land_speed" 40
    Rd(o + 0xa6c, 0x0059f3f8, 0x42200000u);  // "land_speed_jump" 40
    Rd(o + 0xa70, 0x0059f3e4, 0x420c0000u);  // "land_speed_turbo" 35
    Rd(o + 0xa48, 0x0059f3d4, 0x41200000u);  // "jump_impulse" 10
    Rd(o + 0xa4c, 0x0059f3c0, 0x3e800000u);  // "jump_anti_gravity" 0.25
    Fl(o, 0xa4c) = Fl(o, 0xa4c) * K(0x005bcb4c);
    // The animation speed tables (10 dwords each), zeroed in the original's order.
    std::memset(P(0x005feab4), 0, 0x28);
    std::memset(P(0x005feb54), 0, 0x28);
    std::memset(P(0x005fea8c), 0, 0x28);
    std::memset(P(0x005feb2c), 0, 0x28);
    std::memset(P(0x005fea64), 0, 0x28);
    std::memset(P(0x005feb04), 0, 0x28);
    std::memset(P(0x005fea3c), 0, 0x28);
    std::memset(P(0x005feadc), 0, 0x28);
    Rd(0x005feac4, 0x005b6f9c, 0);            // "anim_run_min_speed" 0
    Rd(0x005feac8, 0x005b6f88, 0x42c80000u);  // "anim_run_max_speed" 100
    Rd(0x005feacc, 0x005b6f74, 0);            // "anim_run_low_factor" 0
    Rd(0x005fead0, 0x005b6f60, 0);            // "anim_run_low_speed" 0
    Rd(0x005fead4, 0x005b6f48, 0x3f800000u);  // "anim_run_high_factor" 1
    Rd(0x005fead8, 0x005b6f34, 0x41880000u);  // "anim_run_high_speed" 17
    D(0x005feab8, 0) = 0xbf800000u;
    std::memcpy(P(0x005fea8c), P(0x005feab4), 0x28);
    D(0x005fea90, 0) = 0x3f800000u;
    Rd(0x005feb64, 0x0059f3ac, 0);            // "anim_walk_min_speed" 0
    Rd(0x005feb68, 0x0059f398, 0x42c80000u);  // "anim_walk_max_speed" 100
    Rd(0x005feb6c, 0x0059f380, 0);            // "anim_walk_low_factor" 0
    Rd(0x005feb70, 0x0059f36c, 0);            // "anim_walk_low_speed" 0
    Rd(0x005feb74, 0x0059f354, 0x3f800000u);  // "anim_walk_high_factor" 1
    Rd(0x005feb78, 0x0059f33c, 0x4089999au);  // "anim_walk_high_speed" 4.3
    D(0x005feb58, 0) = 0xbf800000u;
    std::memcpy(P(0x005feb2c), P(0x005feb54), 0x28);
    D(0x005feb30, 0) = 0x3f800000u;
    Rd(0x005fea74, 0x0059f324, 0);            // "anim_side_run_min_speed" 0
    Rd(0x005fea78, 0x0059f30c, 0x42c80000u);  // "anim_side_run_max_speed" 100
    Rd(0x005fea7c, 0x0059f2f0, 0);            // "anim_side_run_low_factor" 0
    Rd(0x005fea80, 0x0059f2d8, 0);            // "anim_side_run_low_speed" 0
    Rd(0x005fea84, 0x0059f2bc, 0x3f800000u);  // "anim_side_run_high_factor" 1
    Rd(0x005fea88, 0x0059f2a0, 0x41880000u);  // "anim_side_run_high_speed" 17
    D(0x005fea64, 0) = 0x3f800000u;
    D(0x005fea68, 0) = 0xbf000000u;
    std::memcpy(P(0x005fea3c), P(0x005fea64), 0x28);
    D(0x005fea3c, 0) = 0xbf800000u;
    PlayerAnimTable_Normalize(0x005fea64, 0x005fea3c);
    Rd(0x005feb14, 0x0059f284, 0);            // "anim_side_walk_min_speed" 0
    Rd(0x005feb18, 0x0059f268, 0x42c80000u);  // "anim_side_walk_max_speed" 100
    Rd(0x005feb1c, 0x0059f24c, 0);            // "anim_side_walk_low_factor" 0
    Rd(0x005feb20, 0x0059f230, 0);            // "anim_side_walk_low_speed" 0
    Rd(0x005feb24, 0x0059f214, 0x3f800000u);  // "anim_side_walk_high_factor" 1
    Rd(0x005feb28, 0x0059f1f8, 0x41880000u);  // "anim_side_walk_high_speed" 17
    D(0x005feb04, 0) = 0x3f800000u;
    D(0x005feb08, 0) = 0xbf000000u;
    std::memcpy(P(0x005feadc), P(0x005feb04), 0x28);
    D(0x005feadc, 0) = 0xbf800000u;
    PlayerAnimTable_Normalize(0x005feb04, 0x005feadc);
    Rd(o + 0x55c, 0x0059f1e0, 0x3ecccccdu);  // "anim_walk_run_switch" 0.4
    Rd(o + 0x560, 0x0059f1cc, 0x42340000u);  // "normal_twist_limit" 45
    Rd(o + 0x564, 0x0059f1b8, 0x42340000u);  // "running_twist_limit" 45
    Fl(o, 0x560) = Fl(o, 0x560) * K(0x005bcba0);
    Fl(o, 0x564) = Fl(o, 0x564) * K(0x005bcba0);
    Rd(o + 0xbd0, 0x0059f1a0, 0x428c0000u);  // "landing_damage_min_hurt" 70
    Rd(o + 0xbd8, 0x0059f188, 0x43480000u);  // "landing_damage_max_hurt" 200
    Rd(o + 0xbe0, 0x0059f16c, 0x42200000u);  // "landing_damage_min_speed" 40
    Rd(o + 0xbe8, 0x0059f150, 0x42c80000u);  // "landing_damage_max_speed" 100
    Rd(o + 0xbd4, 0x0059f130, 0x428c0000u);  // "landing_damage_min_hurt_turbo" 70
    Rd(o + 0xbdc, 0x0059f110, 0x43480000u);  // "landing_damage_max_hurt_turbo" 200
    Rd(o + 0xbe4, 0x0059f0f0, 0x42200000u);  // "landing_damage_min_speed_turbo" 40
    Rd(o + 0xbec, 0x0059f0d0, 0x42c80000u);  // "landing_damage_max_speed_turbo" 100
    Rd(o + 0xbf0, 0x0059f0bc, 0x40400000u);  // "death_falling_time" 3
    Rd(o + 0xbf4, 0x0059f0a4, 0x43480000u);  // "death_falling_distance" 200
    Rd(o + 0xbf8, 0x0059f088, 0x40400000u);  // "turbo_death_falling_time" 3
    Rd(o + 0xbfc, 0x0059f068, 0x43480000u);  // "turbo_death_falling_distance" 200
    // "topsy_camera_bone" (default "Camera03").
    orig::fn_004533a0(0x0059f05c, o + 0xadc, o, 0x0059f048, set, values, 0);
    D(o, 0xad8) = 0;
    D(o, 0xafc) = 0;
    D(o, 0xa34) = 0;
    D(o, 0xab8) = 0;
    D(o, 0xb00) = 0;
    D(o, 0xb04) = 0;
    orig::fn_004546d0(D(D(G(), 0xa84), 0xe88), o);
    orig::fn_004546d0(D(D(G(), 0xa84), 0xe9c), o);
    orig::fn_004546d0(D(D(G(), 0xa84), 0xe90), o);
    orig::fn_004546d0(D(D(G(), 0xa84), 0xe94), o);
    orig::fn_00458270(o, 0, 0, 0x447a0000u);
    const uint32_t fg = orig::fn_00455860(o, 0x447a0000u, 0x461c4000u);
    D(o, 0xabc) = 0;
    D(o, 0xac0) = 0;
    orig::fn_0047b9f0(fg & 0xffffff00u, o + 0xabc, o, Field<uint16_t>(P(o), 0x394), 0);
    D(o, 0x5b4) = 0xffffffffu;
    D(o, 0x5b0) = 0xffffffffu;
    D(o, 0x98c) = 0;
    D(o, 0xac4) = 0;
    D(o, 0xac8) = 0;
    B(o, 0xacc) = 0;
    D(o, 0xad0) = 0;
    D(o, 0xad4) = 0;
    fn_004757b0(o);
    D(o, 0x468) = 0;
    VCall<uint32_t>(P(o), 0x54 / 4, o + 0x500);
    VCall<uint32_t>(P(o), 0x54 / 4, o + 0x50c);
    VCall<uint32_t>(P(o), 0x54 / 4, o + 0x518);
    D(o, 0x484) = 0;
    D(o, 0x480) = 0;
    D(o, 0x47c) = 0;
    D(o, 0x490) = 0;
    D(o, 0x48c) = 0;
    D(o, 0x488) = 0;
    D(o, 0x478) = 0;
    D(o, 0x474) = 0;
    D(o, 0x470) = 0;
    Fl(o, 0x46c) = Len3(o + 0x47c);
    orig::fn_00453150(o, o + 0x494);
    Fl(o, 0x4a0) = std::fabs(Fl(o, 0x494));
    Fl(o, 0x4a4) = std::fabs(Fl(o, 0x498));
    Fl(o, 0x4a8) = std::fabs(Fl(o, 0x49c));
    orig::fn_00458000(o, o + 0x4c0);
    orig::fn_00458000(o, o + 0x530);
    orig::fn_00458000(o, o + 0x524);
    orig::fn_00453150(o, o + 0x4d8);
    Fl(o, 0x4b8) = Len3(o + 0x4c0);
    orig::fn_00458000(o, o + 0x4e8);
    orig::fn_00453180(o, o + 0x4f4);
    Fl(o, 0x4e4) = Len3(o + 0x4e8);
    D(o, 0x544) = 0;
    D(o, 0x540) = 0;
    D(o, 0x53c) = 0;
    D(o, 0x550) = 0;
    D(o, 0x548) = 0;
    D(o, 0x54c) = 0;
    D(o, 0x554) = 0x29;
    D(o, 0x558) = 7;
    B(o, 0xacd) = 0;
    D(o, 0x588) = 0x40a00000u;
    D(o, 0x56c) = 0;
    D(o, 0x574) = 0;
    D(o, 0x578) = 0;
    D(o, 0x57c) = 0x3f800000u;
    D(o, 0x580) = 0;
    D(o, 0x570) = 0;
    D(o, 0x988) = 0;
    D(o, 0x984) = 0;
    D(o, 0xa60) = 0;
    D(o, 0x654) = 0x3f800000u;
    D(o, 0x660) = 0x3f800000u;
    D(o, 0x650) = 0;
    D(o, 0x64c) = 0;
    D(o, 0x66c) = 0;
    D(o, 0x670) = 0;
    D(o, 0x674) = D(o, 0x654);
    D(o, 0x65c) = 0;
    D(o, 0x658) = 0;
    D(o, 0xc00) = 0;
    D(o, 0xc04) = 0;
    D(o, 0xe5c) = 0;
    D(o, 0xc08) = 0;
    D(o, 0x664) = 0;
    D(o, 0x684) = 0;
    D(o, 0x668) = 0;
    D(o, 0x688) = 0;
    D(o, 0x678) = 0;
    D(o, 0x67c) = 0;
    D(o, 0x680) = D(o, 0x660);
    std::memset(P(o + 0x68c), 0, 0xba * 4);
    D(o, 0x830) = 7;
    D(o, 0x6bc) = 7;
    D(o, 0x82c) = 0x29;
    D(o, 0x6b8) = 0x29;
    D(o, 0x6a0) = 0;
    D(o, 0x6a4) = 0;
    D(o, 0x6a8) = 0;
    D(o, 0x6ac) = 0;
    D(o, 0x814) = 0;
    D(o, 0x818) = 0;
    D(o, 0x81c) = 0;
    D(o, 0x820) = 0;
    const uint32_t anims = D(o, 0x414);
    D(o, 0x974) = 0;
    D(o, 0x978) = 0;
    D(o, 0x97c) = 0;
    D(o, 0x980) = 0;
    D(o, 0x644) = 0x3e800000u;
    D(o, 0x640) = 0;
    D(o, 0x648) = 0;
    D(o, 0x568) = 0;
    // Bind the "Trotter_ref" animation's bones to the 10 entries (0xc bytes: value, bone hash) of 0x005fb760.
    const uint32_t found = orig::fn_004f1160(D(D(anims, 0x10), 0), 0x0059f8dc);
    if (found && static_cast<int32_t>(D(found, 0)) >= 0)
    {
        const uint32_t anim = D(D(D(o, 0x414), 0xc) + D(found, 0) * 12, 8);
        uint32_t entry = 0x005fb760;
        for (int32_t n = 10; n != 0; --n, entry += 0xc)
        {
            const uint32_t skel = D(D(anim, 0x2c), 4);
            const uint32_t key = D(skel, 0x1c);
            const uint32_t hash = D(entry, 4);
            if (key == 0 || D(skel, 0x20) == 0)
                continue;
            const uint32_t q = D(skel, 0x24);
            D(q, 0) = key;
            D(q, 0x18) = hash;
            orig::fn_004e0f40(q);
            if (D(q, 0xc) != 0)
                continue;
            const uint32_t e = D(D(q, 0x10), D(q, 8) * 4);
            if (!e)
                continue;
            const uint32_t bone = D(e + Field<uint16_t>(P(e), 2), 0);
            if (!bone)
                continue;
            const uint32_t tab = D(D(o, 0x410), 0x14);
            D(tab, Field<uint16_t>(P(bone), 0xa) * 8 + 4) = entry;
            const uint32_t tab2 = D(D(o, 0x410), 0x14);
            D(tab2, Field<uint16_t>(P(bone), 0xa) * 8) = D(entry, 0);
            B(bone, 8) |= 0x20;
        }
    }
    D(D(o, 0x1c4), 0x3c) = 0;
    if (const uint32_t x = D(o, 0x460))
        D(x, 0x1c) = D(o, 0x1c4);
    const uint32_t y = D(o, 0x464);
    if (y)
        D(y, 0x1c) = D(o, 0x1c4);
    D(o, 0x9b8) = 0x3f800000u;
    D(o, 0x9b4) = 0;
    D(o, 0xbcc) = 0;
    return y;
}
static uint32_t PlayerObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PlayerObject_Load(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00473da0(PlayerObject_Load_Entry, PlayerObject_Load_Abi)
HOOK(0x00473da0, "PlayerObject_Load", PlayerObject_Load_Entry, nullptr);

// 0x00475630 (stack: out; ret 4; PlayerObject vt+0x48). When out is non-null, *out = 1.0f; al = 1. Not executed in
// recorded runs.
uint32_t fn_00475630(uint32_t out)
{
    if (out)
        D(out, 0) = 0x3f800000u;
    return (out & 0xffffff00u) | 1u;
}
static uint32_t fn_00475630_Abi(uint32_t a0)
{
    return fn_00475630(a0);
}
ABI_ENTRY_00475630(fn_00475630_Entry, fn_00475630_Abi)
HOOK(0x00475630, "fn_00475630", fn_00475630_Entry, nullptr);

// 0x00475650 (ecx = player; PlayerObject vt+0x24: guess reset/spawn). 0x0047b8c0(eax = player, g_game byte +0xd92);
// w = player +0xc24 (weapon state): 0x004a0ea0(eax = "ref_rhand" 0x005b6160, ecx = player, esi = w, edi = 0, 0); for
// i < 0x1c:
// 0x004a0f30(eax = i, ecx = w) when g_game byte +0xdaf+i; 0x004a0ef0(eax = i, edi = w) when g_game byte +0xd93+i;
// 0x004a14e0(eax = g_game +0x25e8[i], ecx = i, w) when that is not -1. Then 0x004a10e0(ebx = w, 0), 0x004a12c0(esi =
// w, 0), 0x004a1a30(eax = "L_hand" 0x0059f040, ecx = w). +0xad0 / +0xad4 = 0x0045d5f0(eax = g_game +0xa84, ebx =
// "molmer" 0x005affb4 / "q111" 0x005affac) (objects found by name, guess): when found and 0x00486b90(eax = it) returns al != 0,
// node +0x28 += 1 and 0x004836b0(eax = node, ecx = player); otherwise the member is reset to 0. Not executed in
// recorded runs.
uint32_t fn_00475650(uint32_t p)
{
    orig::fn_0047b8c0(p, B(G(), 0xd92));
    const uint32_t w = p + 0xc24;
    orig::fn_004a0ea0(0x005b6160, p, w, 0, 0);
    for (uint32_t i = 0; i < 0x1c; ++i)
    {
        if (B(G(), 0xdaf + i))
            orig::fn_004a0f30(i, w);
    }
    const uint32_t g = G();
    for (uint32_t i = 0; i < 0x1c; ++i)
    {
        if (B(g, 0xd93 + i))
            orig::fn_004a0ef0(i, w);
    }
    for (uint32_t i = 0; i < 0x1c; ++i)
    {
        const uint32_t v = D(g, 0x25e8 + 4 * i);
        if (v != 0xffffffffu)
            orig::fn_004a14e0(v, i, w);
    }
    orig::fn_004a10e0(w, 0);
    orig::fn_004a12c0(w, 0);
    orig::fn_004a1a30(0x0059f040, w);
    uint32_t r = orig::fn_0045d5f0(D(G(), 0xa84), 0x005affb4);
    D(p, 0xad0) = r;
    if (r)
    {
        const uint32_t node = r;
        r = orig::fn_00486b90(node);
        if (static_cast<uint8_t>(r))
        {
            ++D(node, 0x28);
            r = orig::fn_004836b0(D(p, 0xad0), p);
        }
        else
            D(p, 0xad0) = 0;
    }
    r = orig::fn_0045d5f0(D(G(), 0xa84), 0x005affac);
    D(p, 0xad4) = r;
    if (r)
    {
        const uint32_t node = r;
        r = orig::fn_00486b90(node);
        if (static_cast<uint8_t>(r))
        {
            ++D(node, 0x28);
            return orig::fn_004836b0(D(p, 0xad4), p);
        }
        D(p, 0xad4) = 0;
    }
    return r;
}
static uint32_t fn_00475650_Abi(uint32_t ecx)
{
    return fn_00475650(ecx);
}
ABI_ENTRY_00475650(fn_00475650_Entry, fn_00475650_Abi)
HOOK(0x00475650, "fn_00475650", fn_00475650_Entry, nullptr);

// 0x004757b0 (esi = player; executed in recorded runs; callers Camera_Update 0x0040e350, 0x00473da0, 0x00476a10,
// 0x00498bb0). When player vt+0xd8() returns al != 0: camera = g_game +0xa68: 0x0040e200(eax = camera, ebx =
// "Camera_start" 0x0059f024, player, "Camera_end" 0x0059f034), 0x0040e2a0(ecx = camera,
// 0x00475990(player)), then tail jump to 0x0040e2b0(eax = player +0x9e4 when 0x00475840 (aiming flag +0xacc) else
// +0x9dc, ecx = camera).
uint32_t fn_004757b0(uint32_t p)
{
    const uint32_t r = VCall<uint32_t>(P(p), 0xd8 / 4);
    if (!static_cast<uint8_t>(r))
        return r;
    orig::fn_0040e200(D(G(), 0xa68), 0x0059f024, p, 0x0059f034);
    const uint32_t g = G();
    orig::fn_0040e2a0(D(g, 0xa68), f2u(fn_00475990(p)));
    const uint32_t aiming = fn_00475840(p);
    const uint32_t cam = D(g, 0xa68);
    return orig::fn_0040e2b0(static_cast<uint8_t>(aiming) ? p + 0x9e4 : p + 0x9dc, cam);
}
static uint32_t fn_004757b0_Abi(uint32_t esi)
{
    return fn_004757b0(esi);
}
ABI_ENTRY_004757b0(fn_004757b0_Entry, fn_004757b0_Abi)
HOOK(0x004757b0, "fn_004757b0", fn_004757b0_Entry, nullptr);

// 0x00475840 (eax = player; executed in recorded runs). Returns al = byte +0xacc (aiming / zoom mode, guess: set by
// 0x00475880, cleared by 0x00475910) over the input's upper bytes.
uint32_t fn_00475840(uint32_t player)
{
    return (player & 0xffffff00u) | B(player, 0xacc);
}
static uint32_t fn_00475840_Abi(uint32_t eax)
{
    return fn_00475840(eax);
}
ABI_ENTRY_00475840(fn_00475840_Entry, fn_00475840_Abi)
HOOK(0x00475840, "fn_00475840", fn_00475840_Entry, nullptr);

// 0x00475850 (eax = player; caller 0x004788a0). Toggles the mode +0xacc: when set, 0x00475910(esi = player, 0);
// else 0x00475880(ebx = player). Not executed in recorded runs.
uint32_t fn_00475850(uint32_t p)
{
    if (B(p, 0xacc))
        return fn_00475910(p, 0);
    return fn_00475880(p);
}
static uint32_t fn_00475850_Abi(uint32_t eax)
{
    return fn_00475850(eax);
}
ABI_ENTRY_00475850(fn_00475850_Entry, fn_00475850_Abi)
HOOK(0x00475850, "fn_00475850", fn_00475850_Entry, nullptr);

// 0x00475880 (ebx = player). Enters the mode +0xacc: when clear and the current weapon's g_game table entry (stride
// 0x150 at +0x2870, index 0x004a1660(eax = player +0xc24)) has a non-empty name: 0x0040fae0(eax = that entry, esi =
// camera g_game +0xa68) (copies it as the camera name), +0xacc = 1, 0x0040e2a0(ecx = camera, 0x00475990(player)),
// 0x0040e2b0(eax = player +0x9e4, ecx = camera), al = 1. Otherwise al = 0. Not executed in recorded runs.
uint32_t fn_00475880(uint32_t p)
{
    if (B(p, 0xacc))
        return 0;  // original: xor al, al over the caller's eax
    const uint32_t w = p + 0xc24;
    const uint32_t off = orig::fn_004a1660(w) * 0x150;
    const uint32_t g = G();
    if (!B(off + g, 0x2870))
        return off & 0xffffff00u;
    const uint32_t off2 = orig::fn_004a1660(w) * 0x150;
    orig::fn_0040fae0(off2 + g + 0x2870, D(g, 0xa68));
    B(p, 0xacc) = 1;
    orig::fn_0040e2a0(D(g, 0xa68), f2u(fn_00475990(p)));
    return (orig::fn_0040e2b0(p + 0x9e4, D(g, 0xa68)) & 0xffffff00u) | 1u;
}
static uint32_t fn_00475880_Abi(uint32_t ebx)
{
    return fn_00475880(ebx);
}
ABI_ENTRY_00475880(fn_00475880_Entry, fn_00475880_Abi)
HOOK(0x00475880, "fn_00475880", fn_00475880_Entry, nullptr);

// 0x00475910 (esi = player; stack: flag; ret 4; callers 0x00475850, 0x004788a0, 0x0047ae40). Leaves the mode
// +0xacc: when set, camera = g_game +0xa68: 0x0040fb50(eax = camera) when the flag byte is set else 0x0040fb00,
// +0xacc = 0, 0x0040e2a0(ecx = camera, 0x00475990(player)), 0x0040e2b0(eax = player +0x9dc, ecx = camera), al = 1.
// Otherwise al = 0. Not executed in recorded runs.
uint32_t fn_00475910(uint32_t p, uint32_t flag)
{
    if (!B(p, 0xacc))
        return 0;  // original: xor al, al over the mode byte
    if (static_cast<uint8_t>(flag))
        orig::fn_0040fb50(D(G(), 0xa68));
    else
        orig::fn_0040fb00(D(G(), 0xa68));
    const uint32_t g = G();
    B(p, 0xacc) = 0;
    orig::fn_0040e2a0(D(g, 0xa68), f2u(fn_00475990(p)));
    return (orig::fn_0040e2b0(p + 0x9dc, D(g, 0xa68)) & 0xffffff00u) | 1u;
}
static uint32_t fn_00475910_Abi(uint32_t esi, uint32_t a0)
{
    return fn_00475910(esi, a0);
}
ABI_ENTRY_00475910(fn_00475910_Entry, fn_00475910_Abi)
HOOK(0x00475910, "fn_00475910", fn_00475910_Entry, nullptr);

// 0x00475990 (edx = player; returns st0, not eax as symbols/abi.csv says: every caller stores st0; executed in
// recorded runs). In the mode +0xacc: a = +0xa14[weapon 0x004a1660(eax = player +0xc24)], b = +0xa30; else a =
// +0x9f8, b = +0xa2c. Returns a * b when g_game +0xa6c byte +0x3ac is set, else a (guess: camera field of view with
// a scale).
float fn_00475990(uint32_t p)
{
    float a, b;
    if (static_cast<uint8_t>(fn_00475840(p)))
    {
        const uint32_t i = orig::fn_004a1660(p + 0xc24);
        a = Fl(p, 0xa14 + i * 4);
        b = Fl(p, 0xa30);
    }
    else
    {
        a = Fl(p, 0x9f8);
        b = Fl(p, 0xa2c);
    }
    if (B(D(G(), 0xa6c), 0x3ac))
        return a * b;
    return a;
}
static float fn_00475990_Abi(uint32_t edx)
{
    return fn_00475990(edx);
}
ABI_ENTRY_00475990(fn_00475990_Entry, fn_00475990_Abi)
HOOK(0x00475990, "fn_00475990", fn_00475990_Entry, nullptr);

// 0x004759e0 (edx = player; returns st0 like 0x00475990; executed in recorded runs; callers Camera_Update 0x0040e350,
// 0x004788a0). As 0x00475990 with a = +0x9fc[weapon] / +0x9ec.
float fn_004759e0(uint32_t p)
{
    float a, b;
    if (static_cast<uint8_t>(fn_00475840(p)))
    {
        const uint32_t i = orig::fn_004a1660(p + 0xc24);
        a = Fl(p, 0x9fc + i * 4);
        b = Fl(p, 0xa30);
    }
    else
    {
        a = Fl(p, 0x9ec);
        b = Fl(p, 0xa2c);
    }
    if (B(D(G(), 0xa6c), 0x3ac))
        return a * b;
    return a;
}
static float fn_004759e0_Abi(uint32_t edx)
{
    return fn_004759e0(edx);
}
ABI_ENTRY_004759e0(fn_004759e0_Entry, fn_004759e0_Abi)
HOOK(0x004759e0, "fn_004759e0", fn_004759e0_Entry, nullptr);

// 0x00475a30 (ecx = object; stack: amount (float); ret 4; callers 0x00410ca0, 0x00450da0, 0x00472800). Heal (guess):
// when 0x004555c0(obj) (maximum) > 0x004555b0(obj) (current): v = current + amount when that is below the maximum,
// else the maximum (each re-read through its call), 0x0047af50(ecx = obj, v), al = 1. Otherwise al = 0. Not executed
// in recorded runs.
uint32_t fn_00475a30(uint32_t obj, float amount)
{
    {
        const float h = orig::fn_004555b0(obj);
        const float m = orig::fn_004555c0(obj);
        if (!(m > h))
            return 0;  // original: the fnstsw word
    }
    const float sum = orig::fn_004555b0(obj) + amount;
    const float m = orig::fn_004555c0(obj);
    float v;
    if (m > sum)
        v = orig::fn_004555b0(obj) + amount;
    else
        v = orig::fn_004555c0(obj);
    return (orig::fn_0047af50(obj, f2u(v)) & 0xffffff00u) | 1u;
}
static uint32_t fn_00475a30_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00475a30(ecx, u2f(a0));
}
ABI_ENTRY_00475a30(fn_00475a30_Entry, fn_00475a30_Abi)
HOOK(0x00475a30, "fn_00475a30", fn_00475a30_Entry, nullptr);

// 0x00475a90 (eax = player; stack: one argument, not read; ret 4; caller 0x0043e710). Picks the name of a pickup to
// drop (guess): returns 0 when player vt+0xd8() is false. Otherwise, with g = g_game: health ratio = 0x004555b0 /
// 0x004555c0; h = g +0xc68 when ratio < g +0xc64 else g +0xc6c; the first weapon in the 3-entry list 0x004a1600(eax =
// w = player +0xc24) that is not -1, not the current weapon 0x004a1660(w) and not 2 gives a = g +0xc70 and sel = its
// index when a != 0. x = rand() * (((g +0xc78 + g +0xc74) + a) + h) * 1/32768 (0x005bcbc0). Returns
// "pickup_health_1" 0x005b6e74 when x < h; the current weapon's table name (g +0x2890 + index * 0x150) when h + g
// +0xc74 > x and the current weapon is not 2; the weapon sel's name when sel != -1 and (a + g +0xc74) + h > x; else g
// +0x35b0. The register list's EBP input is a save: ebp is set (-1) before any read. Not executed in recorded runs.
uint32_t fn_00475a90(uint32_t p)
{
    const uint32_t r = VCall<uint32_t>(P(p), 0xd8 / 4);
    if (!static_cast<uint8_t>(r))
        return 0;
    const float ratio = orig::fn_004555b0(p) / orig::fn_004555c0(p);
    uint32_t g = G();
    const float h = ratio < Fl(g, 0xc64) ? u2f(D(g, 0xc68)) : u2f(D(g, 0xc6c));
    float a = 0.0f;
    uint32_t sel = 0xffffffffu;
    const uint32_t w = p + 0xc24;
    const uint32_t list = orig::fn_004a1600(w);
    for (uint32_t i = 0; i < 3; ++i)
    {
        const uint32_t v = D(list, 4 * i);
        if (v == 0xffffffffu || v == orig::fn_004a1660(w) || v == 2)
            continue;
        a = u2f(D(G(), 0xc70));
        const uint32_t vi = D(list, 4 * i);
        if (!(a == K(0x0056c05c)))
            sel = vi == 0xffffffffu ? (sel | vi) : vi;
        break;
    }
    g = G();
    float total = Fl(g, 0xc78) + Fl(g, 0xc74);
    total = total + a;
    total = total + h;
    const int32_t rnd = static_cast<int32_t>(orig::fn_005391cd());
    const float x = static_cast<float>(rnd) * total * K(0x005bcbc0);
    if (x < h)
        return 0x005b6e74;
    g = G();
    if (h + Fl(g, 0xc74) > x)
    {
        if (orig::fn_004a1660(w) != 2)
            return orig::fn_004a1660(w) * 0x150 + g + 0x2890;
    }
    if (sel != 0xffffffffu && (a + Fl(g, 0xc74)) + h > x)
        return g + sel * 0x150 + 0x2890;
    return g + 0x35b0;
}
static uint32_t fn_00475a90_Abi(uint32_t eax, uint32_t /*ebp*/, uint32_t /*a0*/)
{
    return fn_00475a90(eax);
}
ABI_ENTRY_00475a90(fn_00475a90_Entry, fn_00475a90_Abi)
HOOK(0x00475a90, "fn_00475a90", fn_00475a90_Entry, nullptr);

// 0x00475c30 (edx = player; executed in recorded runs; caller 0x00471cb0). Returns the current weapon's g_game table
// name (+0x2890 + 0x004a1660(eax = player +0xc24) * 0x150) when 0x004a1360(eax = 0x1c, edx = player +0xc24) returns
// 0, else 0.
uint32_t fn_00475c30(uint32_t p)
{
    const uint32_t w = p + 0xc24;
    if (orig::fn_004a1360(0x1c, w) != 0)
        return 0;
    return orig::fn_004a1660(w) * 0x150 + G() + 0x2890;
}
static uint32_t fn_00475c30_Abi(uint32_t edx)
{
    return fn_00475c30(edx);
}
ABI_ENTRY_00475c30(fn_00475c30_Entry, fn_00475c30_Abi)
HOOK(0x00475c30, "fn_00475c30", fn_00475c30_Entry, nullptr);

// 0x00475c70 (eax = player, ecx: not read; stack: name; ret 4; caller 0x00471cb0). Whether the player can take the
// weapon `name` (guess): w = player +0xc24; for the 3 entries of 0x004a1600(eax = w) that are not -1, the first
// whose g_game table name (+0x2890 + index * 0x150) matches _stricmp: al = 1 when 0x004a1360(eax = index, edx = w)
// returns 0, otherwise go on to the 5 entries of 0x004a1630(eax = w), where the first match gives al = 1 when
// 0x004a1380(eax = index, edx = w) returns 0, else al = 0. No match: al = 0. Not executed in recorded runs.
uint32_t fn_00475c70(uint32_t p, uint32_t name)
{
    const uint32_t w = p + 0xc24;
    uint32_t list = orig::fn_004a1600(w);
    for (uint32_t i = 0; i < 3; ++i)
    {
        const uint32_t v = D(list, 4 * i);
        if (v == 0xffffffffu)
            continue;
        const uint32_t g = G();
        if (orig::fn_005448ee(g, v * 0x150 + g + 0x2890, name) == 0)
        {
            const uint32_t r = orig::fn_004a1360(D(list, 4 * i), w);
            if (r == 0)
                return 1;
            break;
        }
    }
    list = orig::fn_004a1630(w);
    for (uint32_t i = 0; i < 5; ++i)
    {
        const uint32_t v = D(list, 4 * i);
        if (v == 0xffffffffu)
            continue;
        const uint32_t g = G();
        if (orig::fn_005448ee(g, v * 0x150 + g + 0x2890, name) == 0)
        {
            const uint32_t r = orig::fn_004a1380(D(list, 4 * i), w);
            if (r != 0)
                return r & 0xffffff00u;
            return 1;
        }
    }
    return 0;  // original: xor al, al over the loop's last eax
}
static uint32_t fn_00475c70_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t a0)
{
    return fn_00475c70(eax, a0);
}
ABI_ENTRY_00475c70(fn_00475c70_Entry, fn_00475c70_Abi)
HOOK(0x00475c70, "fn_00475c70", fn_00475c70_Entry, nullptr);

// 0x00475d30 (eax = player; executed in recorded runs). Returns +0xad0 (set by 0x00475650).
uint32_t fn_00475d30(uint32_t p)
{
    return D(p, 0xad0);
}
static uint32_t fn_00475d30_Abi(uint32_t eax)
{
    return fn_00475d30(eax);
}
ABI_ENTRY_00475d30(fn_00475d30_Entry, fn_00475d30_Abi)
HOOK(0x00475d30, "fn_00475d30", fn_00475d30_Entry, nullptr);

// 0x00475d40 (eax = player; executed in recorded runs). Returns +0xad4 (set by 0x00475650).
uint32_t fn_00475d40(uint32_t p)
{
    return D(p, 0xad4);
}
static uint32_t fn_00475d40_Abi(uint32_t eax)
{
    return fn_00475d40(eax);
}
ABI_ENTRY_00475d40(fn_00475d40_Entry, fn_00475d40_Abi)
HOOK(0x00475d40, "fn_00475d40", fn_00475d40_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Player animation state selection (0x00475ff0, called by 0x00476a10).
namespace
{
// 0x004e7880 (ecx = in, edx = out; returns st0, popped by this caller with fstp st(0); symbols/abi.csv lists eax).
void Vec3_NormalizeTo(uint32_t src, uint32_t dst)
{
    static const uint32_t target = 0x004e7880;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, src
        mov edx, dst
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp st(0)
    }
}
// The animation-event test repeated in 0x00475ff0: t = (k - anim+0x14 / anim+0x1c) / anim+0x20 (idiv, fild, fidiv),
// then with slot 4 (0x00408800) flag 0x40 (reversed, guess) slot time +4 <= t, else slot time >= t.
float AnimEventTime(uint32_t anim, int32_t k)
{
    const int32_t q = static_cast<int32_t>(D(anim, 0x14)) / static_cast<int32_t>(D(anim, 0x1c));
    return static_cast<float>(k - q) / static_cast<float>(static_cast<int32_t>(D(anim, 0x20)));
}
bool AnimEventReached(uint32_t slot, float t)
{
    if (D(slot, 0x1c) & 0x40)
        return Fl(slot, 4) <= t;
    return Fl(slot, 4) >= t;
}
// The player animation table at 0x005cb650 (0x28-byte entries, index = mode * 7 + state; type +0 = 0 means "use
// entry +4 + mode * 7 instead").
uint32_t AnimTable(uint32_t index)
{
    return 0x005cb650 + index * 0x28;
}
}  // namespace

// 0x00475ff0 (stack: player, mode; ret 8; caller 0x00476a10; executed in recorded runs). Chooses and starts the
// player's animation for `mode` (+0x554, the movement mode, guess) and a state (+0x558) derived from the request bits
// of +0x54c (0x40000 -> 4, 0x8000 -> 1 or 2 by the weapon 0x004a1660, 0x10000 -> 3, 0x20000 -> 5, which also clear
// +0xa34 bits and byte +0xacd; else +0x558 kept when it is 1..5, +0x548 bit 1 and +0xacd are clear). The entry of
// table 0x005cb650 (mode * 7 + state, following +4 links; `fallback` when a link was followed) has its animation
// indices (+0x14..+0x20) looked up once by name (prefix +4, suffixes "", "_b"/"_t", "_bl"/"_tl"/"_br"/"_tr";
// 0x00475f50). When the mode, state or animations changed (or 0x8000 forced it), the blend speed comes from table
// 0x005cb3a8 [previous kind * 13 + kind] and AnimSlot_Set 0x00408a70 starts slots 0..3 by the entry type (1: one
// animation, 2: a blend of two weighted by the camera pitch (camera +0x74 * 2 clamped to [-1, 1], (v + 1) * 0.5),
// 3: four corners); then +0x68c (0x174 bytes, the look-at state, guess) is saved to +0x800 and rebuilt. +0x554 =
// mode, +0x558 = state, 0x00409ec0(eax = player, 0, 0). Afterwards the pending animation events of +0x54c bits
// 0x200000 (state 5: reload, 0x004a1950 / 0x004a1970, g_game +0x2718 += 1), 0x80000 (state 3: audio name 0x005fb9dc[w]
// at frame 0x0059eef4[w] and weapon vt+0x164 / +0x168 at frame 0x0059eedc[w]) and 0x400000 (state 4: 0x004a1680 /
// 0x004a16b0 at frame 13) fire once the slot-4 time passes their frame, clearing their +0xa34 bit. Returns al = mode
// != the old +0x554.
uint32_t fn_00475ff0(uint32_t actor, uint32_t mode)
{
    uint8_t fallback = 0;
    uint8_t force = 0;
    uint8_t diffType = 0;
    uint32_t user = 0;
    const uint32_t slot4 = orig::fn_00408800(4, actor);
    uint32_t animEntry = 0;
    if (D(slot4, 0) != 0xffffffffu)
        animEntry = D(D(actor, 0x414), 0xc) + D(slot4, 0) * 12;
    const uint32_t req = D(actor, 0x54c);
    uint32_t state = 0;
    if (req & 0x40000)
    {
        B(actor, 0xacd) = 0;
        D(actor, 0xa34) &= 0xfffbffffu;
        state = 4;
    }
    else if (req & 0x8000)
    {
        const uint32_t w = orig::fn_004a1660(actor + 0xc24);
        state = (w == 0 || w == 1 || w == 2) ? 1u : 2u;
        B(actor, 0xacd) = 0;
        D(actor, 0xa34) &= 0xffff7fffu;
        if (mode == 0 || mode == 0x11 || mode == 0x12 || mode == 0x15 || mode == 0x18 || mode == 0x19 ||
            mode == 0x1a || mode == 0x1b)
        {
            if (!(B(actor, 0xa34) & 0x40))
                force = 1;
        }
    }
    else if (req & 0x10000)
    {
        B(actor, 0xacd) = 0;
        D(actor, 0xa34) &= 0xfffeffffu;
        state = 3;
    }
    else if (req & 0x20000)
    {
        B(actor, 0xacd) = 0;
        D(actor, 0xa34) &= 0xfffdffffu;
        state = 5;
    }
    else
    {
        const uint32_t s = D(actor, 0x558);
        if (s >= 1 && s <= 5 && !(B(actor, 0x548) & 2) && !B(actor, 0xacd))
            state = s;
    }
    // The previous entry (+0x554 / +0x558).
    uint32_t prev = 0;
    {
        const uint32_t m = D(actor, 0x554);
        if (m != 0x29 && D(actor, 0x558) != 7)
        {
            const uint32_t m7 = m * 7;
            prev = AnimTable(D(actor, 0x558) + m7);
            while (D(prev, 0) == 0)
                prev = AnimTable(D(prev, 4) + m7);
        }
    }
    const uint32_t prevKind = prev ? D(prev, 0x10) : 0;
    const uint32_t m7 = mode * 7;
    uint32_t e = AnimTable(m7 + state);
    if (D(e, 0) == 0)
    {
        fallback = 1;
        do
            e = AnimTable(D(e, 4) + m7);
        while (D(e, 0) == 0);
    }
    const uint32_t kind = D(e, 0x10);
    if (!B(e, 0x24))
    {
        const uint32_t type = D(e, 0);
        if (type == 3)
        {
            D(e, 0x14) = orig::fn_00475f50(0x0059efa0, D(e, 4), D(actor, 0x414), 0, 0, 0);
            D(e, 0x18) = orig::fn_00475f50(0x0059ef9c, D(e, 4), D(actor, 0x414), 0, 0, 0);
            D(e, 0x1c) = orig::fn_00475f50(0x0059ef98, D(e, 4), D(actor, 0x414), 0, 0, 0);
            D(e, 0x20) = orig::fn_00475f50(0x0059ef94, D(e, 4), D(actor, 0x414), 0, 0, 0);
        }
        else if (type == 2)
        {
            D(e, 0x14) = orig::fn_00475f50(0x0059efa8, D(e, 4), D(actor, 0x414), 0, 0, 0);
            D(e, 0x18) = orig::fn_00475f50(0x0059efa4, D(e, 4), D(actor, 0x414), 0, 0, 0);
            D(e, 0x1c) = 0xffffffffu;
            D(e, 0x20) = 0xffffffffu;
        }
        else if (type == 1)
        {
            D(e, 0x14) = orig::fn_00475f50(0x0058a9a4, D(e, 4), D(actor, 0x414), 0, 0, 0);
            D(e, 0x18) = 0xffffffffu;
            D(e, 0x1c) = 0xffffffffu;
            D(e, 0x20) = 0xffffffffu;
        }
        B(e, 0x24) = 1;
    }
    const uint8_t changed = D(actor, 0x554) != mode ? 1 : 0;
    uint32_t speed = 0;
    uint32_t flags = D(e, 8);
    uint8_t needSet;
    if (!changed && D(actor, 0x558) == state)
        needSet = 0;
    else
    {
        needSet = 1;
        if (!prev)
            diffType = 1;
        else if (D(prev, 0x14) == D(e, 0x14) && D(prev, 0x18) == D(e, 0x18) && D(prev, 0x1c) == D(e, 0x1c) &&
                 D(prev, 0x20) == D(e, 0x20) && D(prev, 0) == D(e, 0))
            needSet = 0;
    }
    if (!diffType && (!prev || D(prev, 0) != D(e, 0)))
        diffType = 1;
    if (!needSet && !force)
        flags |= 0x20000;
    else
    {
        speed = D(0x005cb3a8, (prevKind * 13 + kind) * 4);
        if (D(actor, 0x554) != mode && D(actor, 0x558) == state && state != 0 && !fallback)
        {
            flags |= 0xa00;
            user = f2u(orig::fn_00475fc0(0, actor));
        }
        else if (!diffType && prevKind == kind && kind == 4 && (state == 0 || state == 6))
        {
        }
        else
        {
            flags |= 0xa00;
            if (flags & 0x40)
                user = 0x3f800000u;
            if (fallback)
                B(actor, 0xacd) = 1;
        }
    }
    float v = orig::fn_0040e2c0(D(G(), 0xa68));
    v = v + v;
    if (v > K(0x005bcb2c))
        v = K(0x005bcb2c);
    else if (v < K(0x005bcb5c))
        v = K(0x005bcb5c);
    const uint32_t weight = f2u((v + K(0x005bcb2c)) * K(0x005bcb4c));
    const uint32_t type = D(e, 0);
    if (type == 3)
    {
        flags |= 0x4000;
        orig::fn_00408a70(3, flags, actor, D(e, 0x20), user, D(e, 0xc), speed, 0);
        orig::fn_00408a70(2, flags, actor, D(e, 0x18), user, D(e, 0xc), 0x3f000000u, 0);
        orig::fn_00408a70(1, flags, actor, D(e, 0x1c), user, D(e, 0xc), weight, 0);
        orig::fn_00408a70(0, flags, actor, D(e, 0x14), user, D(e, 0xc), 0x3f000000u, 0);
    }
    else if (type == 2)
    {
        flags |= 0x8000;
        orig::fn_00408a70(1, flags, actor, D(e, 0x18), user, D(e, 0xc), speed, 0);
        orig::fn_00408a70(0, flags, actor, D(e, 0x14), user, D(e, 0xc), weight, 0);
    }
    else if (type == 1)
    {
        orig::fn_00408a70(1, flags, actor, 0xffffffffu, user, D(e, 0xc), speed, 0);
        orig::fn_00408a70(0, flags, actor, D(e, 0x14), user, D(e, 0xc), speed, 0);
    }
    if (needSet || force)
    {
        std::memcpy(P(actor + 0x800), P(actor + 0x68c), 0x5d * 4);
        D(actor, 0x6a0) = D(e, 0x14);
        D(actor, 0x6a4) = D(e, 0x18);
        D(actor, 0x6a8) = D(e, 0x1c);
        B(actor, 0x68c) = 1;
        D(actor, 0x6b0) = D(actor, 0x560);
        D(actor, 0x6ac) = D(e, 0x20);
        D(actor, 0x690) = 0;
        if (kind == 4)
        {
            if (D(e, 0xc) != 0)
            {
                D(actor, 0x6b0) = D(actor, 0x564);
                const uint32_t src = D(e, 0xc);
                D(actor, 0x694) = D(src, 0);
                D(actor, 0x698) = D(src, 4);
                D(actor, 0x69c) = D(src, 8);
                D(D(e, 0xc), 0xc) |= 1;
            }
            else
            {
                D(actor, 0x694) = 0;
                D(actor, 0x69c) = 0;
                D(actor, 0x6b0) = 0x3f860a92u;
                D(actor, 0x698) = 0xbf800000u;
            }
            D(actor, 0x690) |= 4;
        }
        else if (kind == 2 || kind == 3 || kind == 5 || kind == 8 || kind == 1)
        {
            if (prevKind == 4 || prevKind == 6)
                Vec3_NormalizeTo(actor + 0x4d8, actor + 0x694);
            else if (prevKind == 2 || prevKind == 3 || prevKind == 5 || prevKind == 8 || prevKind == 1)
            {
                D(actor, 0x694) = D(actor, 0x808);
                D(actor, 0x698) = D(actor, 0x80c);
                D(actor, 0x69c) = D(actor, 0x810);
            }
            else
                orig::fn_00453150(actor, actor + 0x694);
            D(actor, 0x690) |= 2;
            if (kind == 3)
                D(actor, 0x690) |= 8;
        }
        D(actor, 0x6b8) = mode;
        D(actor, 0x6bc) = state;
        D(actor, 0x690) |= 0x30;
    }
    D(actor, 0x554) = mode;
    D(actor, 0x558) = state;
    orig::fn_00409ec0(actor, 0, 0);
    if (D(actor, 0x54c) & 0x200000)
    {
        bool clear = true;
        if (state == 5 && !fallback)
        {
            const float t = AnimEventTime(D(animEntry, 8), 9);
            const uint32_t sf = D(slot4, 0x1c) & 0x40;
            if (AnimEventReached(slot4, t))
            {
                const uint32_t w = actor + 0xc24;
                if (static_cast<uint8_t>(orig::fn_004a1950(w)))
                {
                    orig::fn_004a1970(sf, w);
                    ++D(G(), 0x2718);
                }
            }
            else
                clear = false;
        }
        if (clear)
            D(actor, 0xa34) &= 0xffdfffffu;
    }
    if (D(actor, 0x54c) & 0x80000)
    {
        bool clear = true;
        if (state == 3 && !fallback)
        {
            const uint32_t wst = actor + 0xc24;
            uint32_t w = orig::fn_004a1660(wst);
            if (static_cast<int32_t>(w) < 0 || !(w < 6))
                w = 0;
            const float t1 = AnimEventTime(D(animEntry, 8), static_cast<int32_t>(D(0x0059eef4, 4 * w)));
            if ((D(actor, 0xa34) & 0x100000) && AnimEventReached(slot4, t1))
            {
                VCall<uint32_t>(P(D(G(), 0x2c)), 0xd0 / 4, D(0x005fb9dc, 4 * w), actor, 0u, 0u, 0xffffffffu, 0u);
                D(actor, 0xa34) &= 0xffefffffu;
            }
            const float t2 = AnimEventTime(D(animEntry, 8), static_cast<int32_t>(D(0x0059eedc, 4 * w)));
            if (AnimEventReached(slot4, t2))
            {
                const uint32_t wo = orig::fn_004a15f0(wst);
                if (static_cast<uint8_t>(VCall<uint32_t>(P(wo), 0x164 / 4)))
                    VCall<uint32_t>(P(orig::fn_004a15f0(wst)), 0x168 / 4);
            }
            else
                clear = false;
        }
        if (clear)
            D(actor, 0xa34) &= 0xfff7ffffu;
    }
    if (D(actor, 0x54c) & 0x400000)
    {
        bool clear = true;
        if (state == 4 && !fallback)
        {
            const float t = AnimEventTime(D(animEntry, 8), 13);
            if (AnimEventReached(slot4, t))
            {
                const uint32_t w = actor + 0xc24;
                if (static_cast<uint8_t>(orig::fn_004a1680(w)))
                    orig::fn_004a16b0(w);
            }
            else
                clear = false;
        }
        if (clear)
            D(actor, 0xa34) &= 0xffbfffffu;
    }
    return changed;
}
static uint32_t fn_00475ff0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_00475ff0(a0, a1);
}
ABI_ENTRY_00475ff0(fn_00475ff0_Entry, fn_00475ff0_Abi)
HOOK(0x00475ff0, "fn_00475ff0", fn_00475ff0_Entry, nullptr);

// 0x00476a10 (stack: player, vx, vy, vz, moving (float), dt (float); ret 0x18; caller 0x004788a0; executed in
// recorded runs). The player's per-frame movement / animation update (guess from use): +0x548 = slot 4 flags
// (0x00408800), +0x54c = +0xa34 (request bits snapshot), +0xa34 &= ~0x6000; previous velocity +0x47c = +0x470, +0x470
// = (vx, vy, vz), +0x488.. = |previous|, speed +0x46c = its length (and +0x494.. / +0x4a0.. the last non-zero one);
// +0xa74 = the speed limit +0xa68 / +0xa70 / +0xa6c by the stance bits; running flag 0x800 with its 0.2 s timer +0x984
// (body velocity +0x53c length > 0x005bcbec); local velocity +0x4ac = 0x004e37f0(eax = &+0x47c, ecx = vt+0x60()
// (matrix; vt+0x60 ends in a plain ret, the pushed -0x11 is 0x004e37f0's second argument), edx = 1, &+0x4ac, -0x11),
// +0x4cc.. = +0x4cc.. * 0.5 + local + +0x4c0.., its xy direction +0x4d8.. when longer than 0x005bcd3c; +0x4f4.. = (0,
// 0, +0x4f0) when +0x4e4 > 0x005bcd3c; the footstep timer +0x988 (0x00493b70 sound 0x0064a730 when it expires while
// moving, +0xabc set and +0xa34 bit 2); 0x00475d50(esi = player, +0x47c, -+0x480, +0x484, moving). Unless
// Object_IsDead or 0x0044e600(): the +0xa34 state bits pick the animation mode for 0x00475ff0 (and advance the state
// machine when the animation finished, +0x548 bit 1); idle (mode +0xc08, random fidget 1..5 after +0xc04 >
// 0x005bcbdc) accumulates dt into +0xc04 / +0xe5c, cleared when +0x554 is not 0..5. Returns +0x554.
uint32_t fn_00476a10(uint32_t p, uint32_t vx, uint32_t vy, uint32_t vz, uint32_t moving, uint32_t dt)
{
    const uint32_t s4 = orig::fn_00408800(4, p);
    D(p, 0x548) = D(s4, 0x1c);
    const uint32_t a34 = D(p, 0xa34);
    D(p, 0x54c) = a34;
    D(p, 0xa34) = a34 & 0xffff9fffu;
    D(p, 0x47c) = D(p, 0x470);
    D(p, 0x480) = D(p, 0x474);
    D(p, 0x484) = D(p, 0x478);
    Fl(p, 0x488) = std::fabs(Fl(p, 0x47c));
    D(p, 0x470) = vx;
    Fl(p, 0x48c) = std::fabs(Fl(p, 0x480));
    D(p, 0x478) = vz;
    D(p, 0x474) = vy;
    Fl(p, 0x490) = std::fabs(Fl(p, 0x484));
    {
        const float x = Fl(p, 0x47c);
        const float z = Fl(p, 0x484);
        const float y = Fl(p, 0x480);
        float len = z * z + y * y;
        len = len + x * x;
        len = std::sqrt(len);
        Fl(p, 0x46c) = len;
        if (len > K(0x0056c05c))
        {
            D(p, 0x494) = D(p, 0x47c);
            D(p, 0x498) = D(p, 0x480);
            D(p, 0x49c) = D(p, 0x484);
            Fl(p, 0x4a0) = std::fabs(Fl(p, 0x494));
            Fl(p, 0x4a4) = std::fabs(Fl(p, 0x498));
            Fl(p, 0x4a8) = std::fabs(Fl(p, 0x49c));
        }
    }
    {
        const uint32_t s = D(p, 0xa34);
        if (s & 0x200)
            D(p, 0xa74) = D(p, 0xa68);
        else if (s & 2)
            D(p, 0xa74) = D(p, 0xa70);
        else if (s & 0xc0)
            D(p, 0xa74) = D(p, 0xa6c);
        else
            D(p, 0xa74) = D(p, 0xa68);
    }
    {
        const float x = Fl(p, 0x53c);
        const float y = Fl(p, 0x540);
        const float z = Fl(p, 0x544);
        float len = x * x + y * y;
        len = len + z * z;
        len = std::sqrt(len);
        if (len > K(0x005bcbec) && Fl(p, 0x46c) > K(0x0056c05c))
        {
            D(p, 0xa34) |= 0x800;
            D(p, 0x984) = 0x3e4ccccdu;
            if (D(p, 0xa34) & 0x100)
                D(p, 0xa34) &= 0xfffff83cu;
        }
        else
        {
            const float t = Fl(p, 0x984) - K(0x00604a90);
            Fl(p, 0x984) = t;
            if (t < K(0x0056c05c))
            {
                D(p, 0xa34) &= 0xfffff7ffu;
                D(p, 0x984) = 0;
            }
        }
    }
    {
        const uint32_t m = VCall<uint32_t>(P(p), 0x60 / 4);
        orig::fn_004e37f0(p + 0x47c, m, 1, p + 0x4ac, 0xffffffefu);
    }
    Fl(p, 0x4cc) = Fl(p, 0x4cc) * K(0x005bcb4c);
    Fl(p, 0x4d0) = Fl(p, 0x4d0) * K(0x005bcb4c);
    Fl(p, 0x4d4) = Fl(p, 0x4d4) * K(0x005bcb4c);
    Fl(p, 0x4cc) = Fl(p, 0x4cc) + Fl(p, 0x4ac);
    Fl(p, 0x4d0) = Fl(p, 0x4d0) + Fl(p, 0x4b0);
    Fl(p, 0x4d4) = Fl(p, 0x4d4) + Fl(p, 0x4b4);
    Fl(p, 0x4cc) = Fl(p, 0x4cc) + Fl(p, 0x4c0);
    Fl(p, 0x4d0) = Fl(p, 0x4d0) + Fl(p, 0x4c4);
    Fl(p, 0x4d4) = Fl(p, 0x4d4) + Fl(p, 0x4c8);
    {
        const float y = Fl(p, 0x4d0);
        const float x = Fl(p, 0x4cc);
        const float len = std::sqrt(x * x + y * y);
        if (len > K(0x005bcd3c))
        {
            const float inv = K(0x005bcb2c) / len;
            Fl(p, 0x4d8) = inv * Fl(p, 0x4cc);
            Fl(p, 0x4dc) = inv * Fl(p, 0x4d0);
            D(p, 0x4e0) = 0;
        }
    }
    if (Fl(p, 0x4e4) > K(0x005bcd3c))
    {
        D(p, 0x4f4) = D(p, 0x4e8);
        D(p, 0x4f8) = D(p, 0x4ec);
        D(p, 0x4fc) = D(p, 0x4f0);
        D(p, 0x4f8) = 0;
        D(p, 0x4f4) = 0;
    }
    if (!(u2f(moving) == K(0x0056c05c)))
    {
        if (Fl(p, 0x988) == K(0x0056c05c) && D(p, 0xabc) != 0 && (B(p, 0xa34) & 4))
            orig::fn_00493b70(0, 2, 0x005fb9d4, 0x0064a730, 0, p, 1, 0, 0);
        D(p, 0x988) = 0x3e4ccccdu;
    }
    else
    {
        const float t = Fl(p, 0x988) - u2f(dt);
        Fl(p, 0x988) = t;
        if (t < K(0x0056c05c))
            D(p, 0x988) = 0;
    }
    orig::fn_00475d50(p, D(p, 0x47c), f2u(-Fl(p, 0x480)), D(p, 0x484), moving);
    if (static_cast<uint8_t>(orig::fn_00454780(p)) || static_cast<uint8_t>(orig::fn_0044e600()))
        goto done;
    {
        const uint32_t s = D(p, 0xa34);
        const auto finished = [p](uint32_t r) { return !static_cast<uint8_t>(r) && (B(p, 0x548) & 2); };
        if (s & 0x7800000)
        {
            if (s & 0x800000)
            {
                if (finished(fn_00475ff0(p, 0x20)))
                {
                    const uint32_t v = D(p, 0xa34) & 0xff7fffffu;
                    D(p, 0xa34) = v;
                    D(p, 0xa34) = v | 0x1000000;
                    orig::fn_0040e200(D(G(), 0xa68), p + 0xadc, p, 0);
                }
            }
            else if (s & 0x1000000)
            {
                if (finished(fn_00475ff0(p, 0x21)))
                {
                    orig::fn_004a1aa0(p + 0xc24);
                    const uint32_t v = D(p, 0xa34) & 0xfeffffffu;
                    D(p, 0xa34) = v;
                    D(p, 0xa34) = v | 0x2000000;
                    D(G(), 0xc80) = D(G(), 0xc7c);
                }
            }
            else if (s & 0x2000000)
                fn_00475ff0(p, 0x22);
            else if (s & 0x4000000)
            {
                if (finished(fn_00475ff0(p, 0x23)))
                {
                    D(p, 0xa34) &= 0xfbffffffu;
                    fn_004757b0(p);
                }
            }
        }
        else if (s & 0x78000000)
        {
            if (s & 0x70000000)
            {
                // Mode 0x25 / 0x26 / 0x27; the original's fall-back (none of the three bits, unreachable) passes
                // the dt argument slot.
                const uint32_t mode = (s & 0x10000000) ? 0x25u : (s & 0x20000000) ? 0x26u : (s & 0x40000000) ? 0x27u : dt;
                if (finished(fn_00475ff0(p, mode)))
                {
                    const uint32_t v = D(p, 0xa34) & 0x8fffffffu;
                    D(p, 0xa34) = v;
                    D(p, 0xa34) = v | 0x8000000;
                }
            }
            else if (s & 0x8000000)
            {
                const uint32_t r = fn_00475ff0(p, 0x24);
                if (static_cast<uint8_t>(orig::fn_00414810((r & 0xffffff00u) | 1u, 8, D(G(), 0xa80), 0x3f000000u)))
                {
                    D(p, 0xa34) &= 0xf7ffffffu;
                    const int32_t rnd = (static_cast<int32_t>(orig::fn_005391cd()) * 100) >> 15;
                    if (rnd < 0x21)
                        D(p, 0xa34) |= 0x10000000;
                    else if (rnd < 0x42)
                        D(p, 0xa34) |= 0x20000000;
                    else
                        D(p, 0xa34) |= 0x40000000;
                }
            }
        }
        else if (s & 0x200)
        {
            if (s & 0x100)
            {
                const uint32_t mode = Fl(p, 0xa60) > Fl(p, 0xa64) ? 8u : 0x13u;
                if (finished(fn_00475ff0(p, mode)))
                    D(p, 0xa34) &= 0xfffff83cu;
            }
            else if (s & 0x400)
            {
                if (finished(fn_00475ff0(p, 6)))
                    D(p, 0xa34) &= 0xfffffbffu;
            }
            else
                fn_00475ff0(p, 7);
        }
        else if (s & 2)
        {
            if (s & 0x100)
            {
                const uint32_t mode = Fl(p, 0x550) > Fl(p, 0xa74) ? 0x17u : 0x16u;
                if (finished(fn_00475ff0(p, mode)))
                    D(p, 0xa34) &= 0xfffffe7du;
            }
            else if (Fl(p, 0x48c) > Fl(p, 0x488))
                fn_00475ff0(p, Fl(p, 0x480) < K(0x0056c05c) ? 0x18u : 0x19u);
            else if (Fl(p, 0x488) > Fl(p, 0x48c))
                fn_00475ff0(p, Fl(p, 0x47c) < K(0x0056c05c) ? 0x1bu : 0x1au);
            else
                fn_00475ff0(p, 0x15);
        }
        else if (s & 0xc0)
        {
            if (s & 0x100)
            {
                if (finished(fn_00475ff0(p, 0x13)))
                    D(p, 0xa34) &= 0xfffffe7fu;
            }
            else if (s & 0x40)
            {
                if (finished(fn_00475ff0(p, 0x11)))
                    D(p, 0xa34) |= 0x80;
            }
            else
                fn_00475ff0(p, 0x12);
        }
        else if (s & 0x800)
        {
            // Walk / run modes by direction (frame +0x10..+0x2c of the original).
            const uint32_t arr[8] = {0xd, 0xe, 0x10, 0xf, 9, 0xa, 0xc, 0xb};
            if (!(Fl(p, 0x498) >= Fl(p, 0x4a0)) && !(Fl(p, 0x498) * K(0x005bcf94) >= Fl(p, 0x4a0)))
            {
                const uint32_t c = Fl(p, 0x9b0) * Fl(p, 0x55c) < Fl(p, 0x4bc) ? 1u : 0u;
                fn_00475ff0(p, Fl(p, 0x494) < K(0x0056c05c) ? arr[c * 4 + 2] : arr[c * 4 + 3]);
            }
            else
            {
                const uint32_t c = Fl(p, 0x9ac) * Fl(p, 0x55c) < Fl(p, 0x4bc) ? 1u : 0u;
                fn_00475ff0(p, Fl(p, 0x498) < K(0x0056c05c) ? arr[c * 4 + 0] : arr[c * 4 + 1]);
            }
        }
        else if (D(p, 0x54c) & 0x2000)
        {
            D(p, 0xa34) |= 0x2000;
            const uint32_t mode = Fl(p, 0x980) < K(0x0056c05c) ? 0x1du : 0x1cu;
            if (finished(fn_00475ff0(p, mode)))
                D(p, 0xa34) &= 0xffffdfffu;
        }
        else if (D(p, 0x54c) & 0x4000)
        {
            D(p, 0xa34) |= 0x4000;
            const uint32_t mode = Fl(p, 0xbcc) < K(0x005bcf90) ? 0x1eu : 0x1fu;
            if (finished(fn_00475ff0(p, mode)))
                D(p, 0xa34) &= 0xffffbfffu;
        }
        else if (VCall<uint32_t>(P(D(D(G(), 0xa84), 0xf00)), 0x34 / 4) != 0)
            fn_00475ff0(p, 0x28);
        else
        {
            if (Fl(p, 0xc04) > K(0x005bcbdc))
            {
                if (B(p, 0x548) & 2)
                {
                    if ((orig::fn_005391cd() << 1) & 0xffff8000u)
                        D(p, 0xc08) = 0;
                    else
                        D(p, 0xc08) = ((static_cast<int32_t>(orig::fn_005391cd()) * 5) >> 15) + 1;
                }
            }
            else
                D(p, 0xc08) = 0;
            fn_00475ff0(p, D(p, 0xc08));
            Fl(p, 0xc04) = u2f(dt) + Fl(p, 0xc04);
            Fl(p, 0xe5c) = u2f(dt) + Fl(p, 0xe5c);
        }
    }
done:
    const int32_t m = static_cast<int32_t>(D(p, 0x554));
    if (m != 0 && (m < 1 || m > 5))
    {
        D(p, 0xc04) = 0;
        D(p, 0xe5c) = 0;
    }
    return static_cast<uint32_t>(m);
}
static uint32_t fn_00476a10_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return fn_00476a10(a0, a1, a2, a3, a4, a5);
}
ABI_ENTRY_00476a10(fn_00476a10_Entry, fn_00476a10_Abi)
HOOK(0x00476a10, "fn_00476a10", fn_00476a10_Entry, nullptr);

// 0x00477c10 (stack: three arguments; ret 0xc). Empty: `ret 0xc` only. Shared by many vtables (e.g. slot 0x2b of
// CameraPathObject 0x005a0f18, PickupObject, StigTurret and others). Not executed in recorded runs.
void fn_00477c10(uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/)
{
}
static void fn_00477c10_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    fn_00477c10(a0, a1, a2);
}
ABI_ENTRY_00477c10(fn_00477c10_Entry, fn_00477c10_Abi)
HOOK(0x00477c10, "fn_00477c10", fn_00477c10_Entry, nullptr);

// 0x00477c20 (ecx = player; stack: other, a1..a4; ret 0x14; PlayerObject vt+0xa8; executed in recorded runs). When
// `other` is set and its +0x414 descriptor's +0x10 record has the name (+4) "fxo_rocket_turret" 0x0059eea0
// (_stricmp), player +0xa34 |= 0x80000000. Then 0x004548f0(ecx = player, other, a1, a2, a3, a4) (the base version,
// guess: hit / damage).
uint32_t fn_00477c20(uint32_t p, uint32_t other, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    if (other && D(other, 0x414) && D(D(other, 0x414), 0x10))
    {
        const uint32_t name = D(D(other, 0x414), 0x10) + 4;
        if (name && orig::fn_005448ee(p, name, 0x0059eea0) == 0)
            D(p, 0xa34) |= 0x80000000u;
    }
    return orig::fn_004548f0(p, other, a1, a2, a3, a4);
}
static uint32_t fn_00477c20_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_00477c20(ecx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_00477c20(fn_00477c20_Entry, fn_00477c20_Abi)
HOOK(0x00477c20, "fn_00477c20", fn_00477c20_Entry, nullptr);

// 0x00477c90 (eax = contact record c; stack: player; ret 4; executed in recorded runs; reached through a pointer,
// no direct callers). The player's contact callback (guess; ebp frame aligned to 16). The record's bodies c+4 / c+8:
// the side whose +0x1c equals c +0x418 is "self" (n is negated when it is the c+8 side, flip). The other body's entity
// list (+0x78 pairs, count +0x7c) gives `found` = the value of the first pair whose key is 0 (self's list is scanned
// the same way, result unused). For each contact point i < c +0x10: p = c[0] vt+0x18(c[3][i]) (p+0x10 = normal);
// when n.z > 0: player +0x584 += 1, n normalised (when its length is not 0), and when n.z > player +0x57c: player
// +0x574.. = n, the old player +0x580 released (= 0) and +0x580 = found (+0x28 += 1); when n.z <= 0 and player
// +0x5b0 >= 0: +0x5b0 = 0x00455a00(edx = +0x5b0). Then q = c[0] vt+0x14(c[3][i]): q +8 = 0 when n.z <= 0.7
// (0x005bcd48) or found and fn_004531e0(ecx = found) is true, else q +8 = player +0x5a0. Afterwards, when player
// +0xa34 & 0x78000000 (punch states, guess): the first live slot 0x00407fd0(eax = k, ecx = g_game +0xa6c), k < g_game
// +0xcc0, holding `found`: with +0xa34 & 0x70000000: Camera_GetDirection 0x0040fa10(esi = &n, camera), slot object
// vt+0x128(1, &n, 0x004581c0(eax = object) * g_game +0xd24), release, slot = 0, audio "wpn_puncher_hit_1/2/3"
// (0x0059ee8c / 0x0059ee78 / 0x0059ee64 by rand() * 100 >> 15 < 0x21 / < 0x42 / else) and "wpn_puncher_cheer"
// 0x0059ee50 (audio g_game +0x2c vt+0xd0(name, 0, 0, 0, -1, 0)). Otherwise slot object vt+0x128(0, 0, 0), release,
// slot = 0, and every live slot object k < g_game +0xcc0 gets vt+0x11c(), a release and slot = 0.
uint32_t fn_00477c90(uint32_t c, uint32_t player)
{
    uint32_t selfBody, otherBody;
    uint8_t flip;
    if (D(c, 0x418) == D(D(c, 4), 0x1c))
    {
        selfBody = D(c, 4);
        otherBody = D(c, 8);
        flip = 0;
    }
    else
    {
        selfBody = D(c, 8);
        otherBody = D(c, 4);
        flip = 1;
    }
    {
        const uint32_t e = D(selfBody, 0x1c);
        const int32_t n = static_cast<int32_t>(D(e, 0x7c));
        if (n > 0)
        {
            const uint32_t list = D(e, 0x78);
            for (int32_t j = 0; j < n; ++j)
                if (D(list, 8 * j) == 0)
                    break;
        }
    }
    uint32_t found = 0;
    {
        const uint32_t e = D(otherBody, 0x1c);
        const int32_t n = static_cast<int32_t>(D(e, 0x7c));
        if (n > 0)
        {
            const uint32_t list = D(e, 0x78);
            for (int32_t j = 0; j < n; ++j)
            {
                if (D(list, 8 * j) == 0)
                {
                    found = D(list, 8 * j + 4);
                    break;
                }
            }
        }
    }
    float n[3];
    const uint32_t count = D(c, 0x10);
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t pt = VCall<uint32_t>(P(D(c, 0)), 0x18 / 4, D(D(c, 0xc), 4 * i));
        const float x = Fl(pt, 0x10);
        const uint32_t ty = D(pt, 0x14);
        const uint32_t tz = D(pt, 0x18);
        if (flip)
        {
            n[0] = -x;
            n[1] = -u2f(ty);
            n[2] = -u2f(tz);
        }
        else
        {
            n[0] = x;
            std::memcpy(&n[1], &ty, 4);
            std::memcpy(&n[2], &tz, 4);
        }
        if (n[2] > K(0x0056c05c))
        {
            ++D(player, 0x584);
            float len = n[2] * n[2] + n[1] * n[1];
            len = len + n[0] * n[0];
            len = std::sqrt(len);
            if (!(len == K(0x0056c05c)))
            {
                const float inv = K(0x005bcb2c) / len;
                n[0] = n[0] * inv;
                n[1] = n[1] * inv;
                n[2] = inv * n[2];
            }
            if (n[2] > Fl(player, 0x57c))
            {
                std::memcpy(&Fl(player, 0x574), n, 12);
                if (const uint32_t old = D(player, 0x580))
                {
                    Ref_Release(old);
                    D(player, 0x580) = 0;
                }
                if (found)
                {
                    ++D(found, 0x28);
                    D(player, 0x580) = found;
                }
            }
        }
        else if (static_cast<int32_t>(D(player, 0x5b0)) >= 0)
            D(player, 0x5b0) = orig::fn_00455a00(D(player, 0x5b0));
        const uint32_t q = VCall<uint32_t>(P(D(c, 0)), 0x14 / 4, D(D(c, 0xc), 4 * i));
        if (!(n[2] > K(0x005bcd48)) || (found && static_cast<uint8_t>(orig::fn_004531e0(found))))
            D(q, 8) = 0;
        else
            Fl(q, 8) = Fl(player, 0x5a0);
    }
    if (!(D(player, 0xa34) & 0x78000000u))
        return 0;
    uint32_t slot = 0;
    for (uint32_t k = 0;; ++k)
    {
        if (!(k < D(G(), 0xcc0)))
            return 0;
        slot = orig::fn_00407fd0(k, D(G(), 0xa6c));
        const uint32_t o = D(slot, 0);
        if (o && !static_cast<uint8_t>(orig::fn_00454780(o)) && D(slot, 0) == found)
            break;
    }
    if (D(player, 0xa34) & 0x70000000u)
    {
        const int32_t r = (static_cast<int32_t>(orig::fn_005391cd()) * 100) >> 15;
        orig::fn_0040fa10(u(n), D(G(), 0xa68));
        const uint32_t o = D(slot, 0);
        const float speed = orig::fn_004581c0(o) * Fl(G(), 0xd24);
        VCall<uint32_t>(P(o), 0x128 / 4, 1u, u(n), f2u(speed));
        Ref_Release(D(slot, 0));
        D(slot, 0) = 0;
        const uint32_t name = r < 0x21 ? 0x0059ee8cu : r < 0x42 ? 0x0059ee78u : 0x0059ee64u;
        VCall<uint32_t>(P(D(G(), 0x2c)), 0xd0 / 4, name, 0u, 0u, 0u, 0xffffffffu, 0u);
        return VCall<uint32_t>(P(D(G(), 0x2c)), 0xd0 / 4, 0x0059ee50u, 0u, 0u, 0u, 0xffffffffu, 0u);
    }
    VCall<uint32_t>(P(D(slot, 0)), 0x128 / 4, 0u, 0u, 0u);
    Ref_Release(D(slot, 0));
    D(slot, 0) = 0;
    for (uint32_t k = 0; k < D(G(), 0xcc0); ++k)
    {
        const uint32_t s = orig::fn_00407fd0(k, D(G(), 0xa6c));
        if (!s || !D(s, 0) || static_cast<uint8_t>(orig::fn_00454780(D(s, 0))))
            continue;
        VCall<uint32_t>(P(D(s, 0)), 0x11c / 4);
        Ref_Release(D(s, 0));
        D(s, 0) = 0;
    }
    return 0;
}
static uint32_t fn_00477c90_Abi(uint32_t eax, uint32_t a0)
{
    return fn_00477c90(eax, a0);
}
ABI_ENTRY_00477c90(fn_00477c90_Entry, fn_00477c90_Abi)
HOOK(0x00477c90, "fn_00477c90", fn_00477c90_Entry, nullptr);

// 0x00478100 (bl = flag, esi = player; stack: value (float); ret 4; caller 0x00478200). Damage (guess) from a 0x34-byte
// record L (zeroed, L[4] = -1): amount = player +0x454 when the flag is set; otherwise, with k = 1 when byte +0xa34
// bit 1 is set else 0, t = (-value - +0xbe0[k]) / +0xbe8[k]; returns when t <= 0 (or unordered), t is clamped to 1
// and amount = (1 - t) * +0xbd0[k] + t * +0xbd8[k]. L[5] = amount; when amount != 0: L[12] = 0, L[0] = 0x400000
// when the flag is set else 0, L[9] = 1.0, L[10] = L[11] = 0, L[6] = 0, L[8] = 1.0, player vt+0x54(&L[1]) (position,
// guess), player vt+0xd0(&L[0]). Not executed in recorded runs.
uint32_t fn_00478100(uint32_t flag, uint32_t p, float value)
{
    uint32_t L[13];
    std::memset(L, 0, sizeof(L));
    L[4] = 0xffffffffu;
    float amount;
    if (static_cast<uint8_t>(flag))
        amount = Fl(p, 0x454);
    else
    {
        const uint32_t k = (B(p, 0xa34) & 2) ? 1u : 0u;
        float t = (-value - Fl(p, 0xbe0 + 4 * k)) / Fl(p, 0xbe8 + 4 * k);
        if (!(t > K(0x0056c05c)))
            return 0;  // original: the fnstsw word
        if (t > K(0x005bcb2c))
            t = K(0x005bcb2c);
        const float lo = (K(0x005bcb2c) - t) * Fl(p, 0xbd0 + 4 * k);
        amount = lo + t * Fl(p, 0xbd8 + 4 * k);
    }
    L[5] = f2u(amount);
    if (amount == K(0x0056c05c))
        return 0;  // original: the fnstsw word
    L[12] = 0;
    L[0] = 0;
    if (static_cast<uint8_t>(flag))
        L[0] = 0x400000;
    L[9] = 0x3f800000u;
    L[10] = 0;
    L[11] = 0;
    L[6] = 0;
    L[8] = 0x3f800000u;
    VCall<uint32_t>(P(p), 0x54 / 4, u(&L[1]));
    return VCall<uint32_t>(P(p), 0xd0 / 4, u(&L[0]));
}
static uint32_t fn_00478100_Abi(uint32_t ebx, uint32_t esi, uint32_t a0)
{
    return fn_00478100(ebx, esi, u2f(a0));
}
ABI_ENTRY_00478100(fn_00478100_Entry, fn_00478100_Abi)
HOOK(0x00478100, "fn_00478100", fn_00478100_Entry, nullptr);

// 0x00478800 (esi = player; stack: effect; ret 4; callers 0x00478200, 0x004788a0, 0x0047aaf0). Spawns an effect at
// the player (guess): frame F (0x94 bytes, not initialised), effect params at F+0x14: player vt+0x54(&F[0x20])
// (position), F[0x14] = 8, player vt+0xb8(&F[0], &F[8], &F[4], 0); when that returns al != 0, F[0x14] |= 0x800,
// F[0x8c] = F[4], F[0x90] = F[0]. Then Fx_Create 0x0041cc60(eax = al 0 over vt+0xb8's result, ecx = g_game +0xa90,
// effect, &F[0x14]). Not executed in recorded runs.
uint32_t fn_00478800(uint32_t p, uint32_t effect)
{
    uint32_t F[0x94 / 4];
    VCall<uint32_t>(P(p), 0x54 / 4, u(F) + 0x20);
    F[0x14 / 4] = 8;
    const uint32_t r = VCall<uint32_t>(P(p), 0xb8 / 4, u(F), u(F) + 8, u(F) + 4, 0u);
    if (static_cast<uint8_t>(r))
    {
        F[0x14 / 4] |= 0x800;
        F[0x8c / 4] = F[4 / 4];
        F[0x90 / 4] = F[0];
    }
    return orig::fn_0041cc60(r & 0xffffff00u, D(G(), 0xa90), effect, u(F) + 0x14);
}
static uint32_t fn_00478800_Abi(uint32_t esi, uint32_t a0)
{
    return fn_00478800(esi, a0);
}
ABI_ENTRY_00478800(fn_00478800_Entry, fn_00478800_Abi)
HOOK(0x00478800, "fn_00478800", fn_00478800_Entry, nullptr);

// 0x00478890 (stack: out; ret 4; PlayerObject vt+0x8c). *out = 0.5f; returns out. Not executed in recorded runs.
uint32_t fn_00478890(uint32_t out)
{
    D(out, 0) = 0x3f000000u;
    return out;
}
static uint32_t fn_00478890_Abi(uint32_t a0)
{
    return fn_00478890(a0);
}
ABI_ENTRY_00478890(fn_00478890_Entry, fn_00478890_Abi)
HOOK(0x00478890, "fn_00478890", fn_00478890_Entry, nullptr);
