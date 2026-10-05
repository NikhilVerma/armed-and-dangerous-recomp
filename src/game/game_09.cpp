// Recovered (static, batch game_09): turret virtuals (BaseDefendTurret, RocketTowerTurret, RocketBudTurret) around
// 0x0049bf20-0x0049cdc0, level-name and player weapon-state helpers around 0x004a0690-0x004a1bb0, and WeaponObject /
// PlayerWeapon / PlayerVindaloo virtuals around 0x004a1bf0-0x004a2a70. Compile-checked only (the game was not run);
// the oracle (tools/compare.py) is still to do. Functions the recorded runs never execute are marked "not executed in
// recorded runs". Vtable slots come from a scan of .rdata in game.unpacked.exe (vtable starts = constructor stores
// `mov [reg], imm32`); class names from the vtable's slot 1 descriptor (+0x14 "CLASS(Name)").
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
// Game time (0x00604a98) and frame time step (0x00604a90).
uint32_t TimeBits()
{
    return GameGlobal<uint32_t>(0x00604a98);
}
float Time()
{
    return GameGlobal<float>(0x00604a98);
}
float Dt()
{
    return GameGlobal<float>(0x00604a90);
}
int32_t Rand()
{
    return static_cast<int32_t>(static_cast<uint32_t>(orig::fn_005391cd()));
}
// 0x004e7880 (ecx = in, edx = out): vector normalise; returns the length in st0 (symbols/abi.csv says eax; the
// callers here `fstp` it).
float Vec3_Normalize(uint32_t vin, uint32_t vout)
{
    static const uint32_t target = 0x004e7880;
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, vin
        mov edx, vout
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp r
    }
    return r;
}
// The refcount release seen inline in several functions: --obj+0x28; when it reaches 0 and obj vt+0xd8() returns
// al = 0, 0x00487da0 (eax = obj) frees it. Returns the last eax of the sequence.
uint32_t Ref_Release(uint32_t obj)
{
    if (--D(obj, 0x28) != 0)
        return 0;
    const uint32_t r = VCall<uint32_t>(P(obj), 0xd8 / 4);
    if (static_cast<uint8_t>(r))
        return r;
    return orig::fn_00487da0(obj);
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 0x0049bf20 (ecx = turret; stack: reload, a1; ret 8). BaseDefendTurret (vtable 0x005a9f58) slot 0x4f (+0x13c),
// "fire". reload != 0: reload animation. When +0x634 > 0.0 ([0x0056c05c]) 0x0047dbf0(eax = +0x6c4, ecx = +0x6a4,
// edx = turret; Game+0xaa8, Game+0x748, 0) and Anim_Set 0x00408d90 on +0x80c with "base_homing_reload" (0x0058f10c);
// else 0x00453740(eax = +0x80c) picks 0x0047dbf0 with ecx = +0x6a4 (and +0x808 / +0x80c vt+0xf4(1) / (0)) or
// +0x684, and Anim_Set on +0x808 with "base_mortar_reload" (0x0058f134). Then vt+0x144(reload), +0x640 = time,
// Game+0x2718 += 1; al = (0x0047dbf0 result != 0). reload == 0: fires a projectile: muzzle matrix 0x00453c50(turret,
// +0x4a0 or +0x754 alternating on byte +0x62c, &m, 1, 0, 0, 1, 0, 0); a target from the target list (Game+0xa6c
// +0x58 / +0x154; up to four random candidates 0x2c apart, countdown +0x818 re-armed with rand in [+0x820, +0x81c]);
// ObjectSystem_CreateObject 0x0045f020(eax = 0, ecx = +0x460; Game+0xa84, 0, 0x400, 0, 0, 0, 0, 0xaa, 0); aim point
// = the target position, or for a moving target (+0x418 not 0x55 / 0x50) the lead point 0x004580a0 with the
// projectile speed 0x0047e360; +0x568 = +0x56c = distance; projectile vt+0x14c(target, 0), vt+0x150(&aim), matrix
// from the direction 0x004e3070 and vt+0x140(&m, player). Toggles +0x62c, Game+0x2718 += 1, vt+0x144(0), +0x524 =
// time, al = 1. Not executed in recorded runs.
uint32_t fn_0049bf20(uint32_t t, uint32_t reload, uint32_t /*a1*/)
{
    if (reload != 0)
    {
        uint32_t r;
        uint32_t anim;
        uint32_t name;
        if (Fl(t, 0x634) > K(0x0056c05c))
        {
            r = orig::fn_0047dbf0(t + 0x6c4, t + 0x6a4, t, D(G(), 0xaa8), D(G(), 0x748), 0);
            anim = D(t, 0x80c);
            name = 0x0058f10c;
        }
        else
        {
            const uint8_t al = static_cast<uint8_t>(orig::fn_00453740(D(t, 0x80c)));
            if (al)
            {
                r = orig::fn_0047dbf0(t + 0x6c4, t + 0x6a4, t, D(G(), 0xaa8), D(G(), 0x748), 0);
                VCall<uint32_t>(P(D(t, 0x808)), 0xf4 / 4, 1u);
                VCall<uint32_t>(P(D(t, 0x80c)), 0xf4 / 4, 0u);
            }
            else
            {
                r = orig::fn_0047dbf0(t + 0x6c4, t + 0x684, t, D(G(), 0xaa8), D(G(), 0x748), 0);
            }
            anim = D(t, 0x808);
            name = 0x0058f134;
        }
        orig::fn_00408d90(0xffffffffu, anim, name, 0x80, 0, 0, 0, 0);
        VCall<uint32_t>(P(t), 0x144 / 4, reload);
        D(t, 0x640) = TimeBits();
        ++D(G(), 0x2718);
        return r != 0 ? 1u : 0u;
    }

    float aim[3];   // esp+0x10
    float muz[3];   // esp+0x1c
    float dir[3];   // esp+0x28
    float m[17];    // esp+0x34 (0x44 bytes)
    const uint32_t src = B(t, 0x62c) ? t + 0x4a0 : t + 0x754;
    orig::fn_00453c50(t, src, u(m), 1, 0, 0, 1, 0, 0);
    muz[0] = m[3];
    muz[2] = m[11];
    muz[1] = m[7];
    const uint32_t list = D(G(), 0xa6c);
    const uint32_t cands = orig::fn_00407fe0(list);
    const uint32_t cur = orig::fn_00408010(list);
    uint32_t target;
    uint32_t pos;
    if (D(cur, 0) != 0 && (B(D(cur, 0), 0x424) & 0x40))
    {
        pos = cur + 4;
        target = D(cur, 0);
    }
    else
    {
        uint32_t n = 0;
        if (D(cands, 0) != 0)
            n = 1;
        if (D(cands, 0x2c) != 0)
            ++n;
        if (D(cands, 0x58) != 0)
            ++n;
        if (D(cands, 0x84) != 0)
            ++n;
        bool pick = false;
        if (n == 0)
        {
            const int32_t hi = static_cast<int32_t>(D(t, 0x81c));
            const int32_t lo = static_cast<int32_t>(D(t, 0x820));
            D(t, 0x818) = static_cast<uint32_t>(((Rand() * (hi - lo + 1)) >> 15) + lo);
        }
        else if (static_cast<int32_t>(--D(t, 0x818)) < 0)
        {
            pick = true;
        }
        if (pick)
        {
            const uint32_t e = cands + static_cast<uint32_t>((Rand() * static_cast<int32_t>(n)) >> 15) * 0x2c;
            target = D(e, 0);
            pos = e + 4;
        }
        else
        {
            pos = cur + 4;
            target = 0;
        }
    }
    const uint32_t proj = orig::fn_0045f020(0, t + 0x460, D(G(), 0xa84), 0, 0x400, 0, 0, 0, 0, 0xaa, 0);
    if (proj != 0)
    {
        const uint32_t type = target ? D(target, 0x418) : 0;
        if (target == 0 || type == 0x55 || type == 0x50)
        {
            D(u(aim), 0) = D(pos, 0);
            D(u(aim), 4) = D(pos, 4);
            D(u(aim), 8) = D(pos, 8);
        }
        else
        {
            const float speed = orig::fn_0047e360(proj);
            orig::fn_004580a0(u(aim), target, u(muz), f2u(speed), 1);
        }
        const float dy = muz[1] - aim[1];
        const float dz = muz[2] - aim[2];
        const float dx = muz[0] - aim[0];
        const float d = std::sqrt(dx * dx + dz * dz + dy * dy);
        Fl(proj, 0x568) = d;
        Fl(proj, 0x56c) = d;
        VCall<uint32_t>(P(proj), 0x14c / 4, target, 0u);
        VCall<uint32_t>(P(proj), 0x150 / 4, u(aim));
        dir[0] = aim[0] - muz[0];
        dir[1] = aim[1] - muz[1];
        dir[2] = aim[2] - muz[2];
        orig::fn_004e3070(u(dir), u(m), 0, u(muz));
        VCall<uint32_t>(P(proj), 0x140 / 4, u(m), D(G(), 0x748));
    }
    B(t, 0x62c) = B(t, 0x62c) == 0 ? 1 : 0;
    ++D(G(), 0x2718);
    VCall<uint32_t>(P(t), 0x144 / 4, 0u);
    const uint32_t now = TimeBits();
    D(t, 0x524) = now;
    return (now & 0xffffff00u) | 1u;
}
static uint32_t fn_0049bf20_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0049bf20(ecx, a0, a1);
}
ABI_ENTRY_0049bf20(fn_0049bf20_Entry, fn_0049bf20_Abi)
HOOK(0x0049bf20, "fn_0049bf20", fn_0049bf20_Entry, nullptr);

// 0x0049c2a0 (ecx = turret; stack: on; ret 4). BaseDefendTurret (vtable 0x005a9f58) slot 0x51 (+0x144). on == 0:
// the base slot 0x00497fd0 (via the thunk 0x00498a70; ecx = turret, 0) and its eax. Otherwise, when byte +0x704 and
// byte +0x6e4 are both set, Fx_Create 0x0041cc60(eax = al cleared, ecx = Game+0xa90; &+0x704 (effect name), &{4,
// ..., +0x18 = &+0x6e4, +0x1c = turret}) and al = 1, else al = 0. Not executed in recorded runs.
uint32_t fn_0049c2a0(uint32_t t, uint32_t on)
{
    uint32_t loc[0x80 / 4] = {};
    loc[0x1c / 4] = t;
    loc[0] = 4;
    if (on == 0)
        return orig::fn_00497fd0(t, 0);
    if (B(t, 0x704) == 0)
        return on & 0xffffff00u;
    const uint32_t eff = t + 0x6e4;
    if (B(eff, 0) == 0)
        return eff & 0xffffff00u;
    loc[0x18 / 4] = eff;
    const uint32_t r = orig::fn_0041cc60(u(loc) & 0xffffff00u, D(G(), 0xa90), t + 0x704, u(loc));
    return (r & 0xffffff00u) | 1u;
}
static uint32_t fn_0049c2a0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0049c2a0(ecx, a0);
}
ABI_ENTRY_0049c2a0(fn_0049c2a0_Entry, fn_0049c2a0_Abi)
HOOK(0x0049c2a0, "fn_0049c2a0", fn_0049c2a0_Entry, nullptr);

// 0x0049c320 (ecx = turret; stack: a0, matrix, props, a3; ret 0x10). RocketTowerTurret (vtable 0x005a8b88) slot 6
// (load): the base load 0x00497d00(ecx; a0..a3); +0x5bc..+0x5c4 = the matrix translation (m[3], m[7], m[11]) or 0;
// Object_SetPosition 0x00457870(eax = al cleared, ecx = &+0x5bc, edx = turret); the properties
// "emitter_bone_name_1..4" (Object_ReadStringProp 0x004533a0 into +0x538/+0x558/+0x578/+0x598), "active_range"
// (+0x5b8, 500), "spin_max_speed" (+0x5d8, 0.5), "spin_accel" (+0x5dc, 0.5), (+0x5e0, 0.8) and
// "blind_fire_duration" (+0x5d0, 1.0) (Object_ReadValuePropDef 0x004534a0); state reset and ObjectList_Link
// 0x004546d0 (esi = Game+0xa84 +0xea4, edi = turret). Returns its eax. Not executed in recorded runs.
uint32_t fn_0049c320(uint32_t t, uint32_t a0, uint32_t mat, uint32_t props, uint32_t a3)
{
    uint32_t eax = orig::fn_00497d00(t, a0, mat, props, a3);
    if (mat != 0)
    {
        D(t, 0x5bc) = D(mat, 0xc);
        eax = D(mat, 0x1c);
        D(t, 0x5c0) = eax;
        D(t, 0x5c4) = D(mat, 0x2c);
    }
    else
    {
        eax = 0;
        D(t, 0x5bc) = 0;
        D(t, 0x5c0) = 0;
        D(t, 0x5c4) = 0;
    }
    orig::fn_00457870(eax & 0xffffff00u, t + 0x5bc, t, 0, 0, 0);
    orig::fn_004533a0(0x0058a9a4, t + 0x538, t, 0x0058f01c, props, a3, 0);
    orig::fn_004533a0(0x0058a9a4, t + 0x558, t, 0x0058f44c, props, a3, 0);
    orig::fn_004533a0(0x0058a9a4, t + 0x578, t, 0x0058f008, props, a3, 0);
    orig::fn_004533a0(0x0058a9a4, t + 0x598, t, 0x0058eff4, props, a3, 0);
    orig::fn_004534a0(a3, t + 0x5b8, t, 0x0058efe4, props, 0x43fa0000, 0);
    orig::fn_004534a0(a3, t + 0x5d8, t, 0x0058efd4, props, 0x3f000000, 0);
    orig::fn_004534a0(a3, t + 0x5dc, t, 0x0058efc8, props, 0x3f000000, 0);
    orig::fn_004534a0(a3, t + 0x5e0, t, 0x0058efb8, props, 0x3f4ccccd, 0);
    orig::fn_004534a0(a3, t + 0x5d0, t, 0x0058efa4, props, 0x3f800000, 0);
    const uint32_t g = G();
    D(t, 0x5e4) = 0;
    D(t, 0x5e8) = 0;
    D(t, 0x524) = TimeBits();
    B(t, 0x5d5) = 0;
    D(t, 0x5c8) = 0x3f800000;
    D(t, 0x5cc) = 0;
    B(t, 0x5d4) = 0;
    D(t, 0x5f0) = 0;
    return static_cast<uint32_t>(orig::fn_004546d0(D(D(g, 0xa84), 0xea4), t));
}
static uint32_t fn_0049c320_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0049c320(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0049c320(fn_0049c320_Entry, fn_0049c320_Abi)
HOOK(0x0049c320, "fn_0049c320", fn_0049c320_Entry, nullptr);

// 0x0049c4c0 (ecx = turret). RocketTowerTurret (vtable 0x005a8b88) slot 0x37 (+0xdc): stops the turn loop sound
// ((Game+0x2c) vt+0xd8(&+0x5f0, 0)), releases the reference at +0x5ec (refcount +0x28, 0x00487da0) and clears it,
// then a tail jump to the base slot 0x00497e90 (ecx = turret). Not executed in recorded runs.
uint32_t fn_0049c4c0(uint32_t t)
{
    VCall<uint32_t>(P(D(G(), 0x2c)), 0xd8 / 4, t + 0x5f0, 0u);
    const uint32_t ref = D(t, 0x5ec);
    if (ref != 0)
    {
        Ref_Release(ref);
        D(t, 0x5ec) = 0;
    }
    return orig::fn_00497e90(t);
}
static uint32_t fn_0049c4c0_Abi(uint32_t ecx)
{
    return fn_0049c4c0(ecx);
}
ABI_ENTRY_0049c4c0(fn_0049c4c0_Entry, fn_0049c4c0_Abi)
HOOK(0x0049c4c0, "fn_0049c4c0", fn_0049c4c0_Entry, nullptr);

// 0x0049c520 (eax = object, ecx = turret). Stores the object at turret +0x5ec and references it (+0x28 += 1).
// Called by 0x004510b0. Not executed in recorded runs.
void fn_0049c520(uint32_t obj, uint32_t t)
{
    D(t, 0x5ec) = obj;
    ++D(obj, 0x28);
}
static uint32_t fn_0049c520_Abi(uint32_t eax, uint32_t ecx)
{
    fn_0049c520(eax, ecx);
    return eax;
}
ABI_ENTRY_0049c520(fn_0049c520_Entry, fn_0049c520_Abi)
HOOK(0x0049c520, "fn_0049c520", fn_0049c520_Entry, nullptr);

// 0x0049c530 (eax = object, ecx, edx). object +0x528 = ecx, +0x390 = edx. Called by 0x004510b0. Not executed in
// recorded runs.
void fn_0049c530(uint32_t obj, uint32_t a, uint32_t b)
{
    D(obj, 0x528) = a;
    D(obj, 0x390) = b;
}
static uint32_t fn_0049c530_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    fn_0049c530(eax, ecx, edx);
    return eax;
}
ABI_ENTRY_0049c530(fn_0049c530_Entry, fn_0049c530_Abi)
HOOK(0x0049c530, "fn_0049c530", fn_0049c530_Entry, nullptr);

// 0x0049c540 (ecx = turret; stack: a0 unused; ret 4). RocketTowerTurret (vtable 0x005a8b88) slot 0x35 (+0xd4): the
// base slot 0x00497e60(ecx, 0); then, unless turret vt+0xd8() is true, detaches the object at +0x5ec (+0x5ec = 0;
// when its vt+0xd8() is true its vt+0xd4(0)) and releases it. Not executed in recorded runs.
uint32_t fn_0049c540(uint32_t t, uint32_t /*a0*/)
{
    orig::fn_00497e60(t, 0);
    uint32_t r = VCall<uint32_t>(P(t), 0xd8 / 4);
    if (static_cast<uint8_t>(r))
        return r;
    const uint32_t obj = D(t, 0x5ec);
    if (obj == 0)
        return r;
    D(t, 0x5ec) = 0;
    r = VCall<uint32_t>(P(obj), 0xd8 / 4);
    if (static_cast<uint8_t>(r))
        r = VCall<uint32_t>(P(obj), 0xd4 / 4, 0u);
    if (--D(obj, 0x28) != 0)
        return r;
    r = VCall<uint32_t>(P(obj), 0xd8 / 4);
    if (static_cast<uint8_t>(r))
        return r;
    return orig::fn_00487da0(obj);
}
static uint32_t fn_0049c540_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0049c540(ecx, a0);
}
ABI_ENTRY_0049c540(fn_0049c540_Entry, fn_0049c540_Abi)
HOOK(0x0049c540, "fn_0049c540", fn_0049c540_Entry, nullptr);

// 0x0049c5b0 (ecx = turret; stack: a0 unused; ret 4). RocketTowerTurret (0x005a8b88) / RocketBudTurret (0x005adde8)
// slot 0x4e (+0x138), "ready to fire": flags +0x530 |= 0x40 then &= ~0x80 (two stores); al = 1 when the last fire
// time +0x524 is 0.0, else 1 when time - +0x524 > the interval +0x520, else 0. Not executed in recorded runs.
uint32_t fn_0049c5b0(uint32_t t, uint32_t /*a0*/)
{
    uint32_t f = D(t, 0x530) | 0x40;
    D(t, 0x530) = f;
    f &= 0xffffff7fu;
    D(t, 0x530) = f;
    if (Fl(t, 0x524) == K(0x0056c05c))
        return 1;
    if (Time() - Fl(t, 0x524) > Fl(t, 0x520))
        return 1;
    return 0;
}
static uint32_t fn_0049c5b0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0049c5b0(ecx, a0);
}
ABI_ENTRY_0049c5b0(fn_0049c5b0_Entry, fn_0049c5b0_Abi)
HOOK(0x0049c5b0, "fn_0049c5b0", fn_0049c5b0_Entry, nullptr);

// 0x0049c610 (ecx = turret; stack: a0, a1 unused; ret 8). RocketTowerTurret (vtable 0x005a8b88) slot 0x4f (+0x13c),
// "fire": for each of the four emitter bones (+0x538, 0x20 apart) 0x0047dfd0(ecx = bone name, edx = turret;
// Game+0xaa8, turret, +0x460) and, when it returns a projectile, its vt+0x14c(player, "chest" 0x0059e210). Then
// vt+0x144(0), +0x524 = time; al = (last projectile != 0). Not executed in recorded runs.
uint32_t fn_0049c610(uint32_t t, uint32_t /*a0*/, uint32_t /*a1*/)
{
    uint32_t bone = t + 0x538;
    uint32_t proj = 0;
    for (int i = 4; i != 0; --i)
    {
        proj = orig::fn_0047dfd0(bone, t, D(G(), 0xaa8), t, t + 0x460);
        if (proj != 0)
            VCall<uint32_t>(P(proj), 0x14c / 4, D(G(), 0x748), 0x0059e210u);
        bone += 0x20;
    }
    VCall<uint32_t>(P(t), 0x144 / 4, 0u);
    D(t, 0x524) = TimeBits();
    return proj != 0 ? 1u : 0u;
}
static uint32_t fn_0049c610_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0049c610(ecx, a0, a1);
}
ABI_ENTRY_0049c610(fn_0049c610_Entry, fn_0049c610_Abi)
HOOK(0x0049c610, "fn_0049c610", fn_0049c610_Entry, nullptr);

namespace
{
// The line-of-sight test shared by the rocket turrets' updates (0x0049c7aa-0x0049c87d and 0x0049cc95-0x0049cd6d):
// the player's position 0x00458040(esi = player, edi = &R), the eye S = the turret position (pos[0..2]) raised by
// [0x005bcb68], dir = R - S normalised (0x004e7880, length kept) and Ray_CastQuery 0x00414110(eax = &S, esi = &dir;
// Game+0xa8c, len, &len, 0, &hit, 0, 0, turret, Game+0xa84 +0xe9c, 0, 0, 1, 2). Returns the hit object. f is the
// original 0x38-byte frame (from its edi-pushed base): hit +0, len +4, S +8, dir +0x14, R +0x2c.
uint32_t Turret_SightHit(uint32_t t, uint32_t pos)
{
    uint32_t f[0x38 / 4];
    const uint32_t fb = u(f);
    orig::fn_00458040(D(G(), 0x748), fb + 0x2c);
    D(fb, 0x10) = D(pos, 8);
    D(fb, 0x08) = D(pos, 0);
    D(fb, 0x0c) = D(pos, 4);
    Fl(fb, 0x10) = Fl(fb, 0x10) + K(0x005bcb68);
    D(fb, 0) = 0;
    Fl(fb, 0x14) = Fl(fb, 0x2c) - Fl(fb, 0x08);
    Fl(fb, 0x18) = Fl(fb, 0x30) - Fl(fb, 0x0c);
    Fl(fb, 0x1c) = Fl(fb, 0x34) - Fl(fb, 0x10);
    Fl(fb, 4) = Vec3_Normalize(fb + 0x14, fb + 0x14);
    const uint32_t g = G();
    orig::fn_00414110(fb + 8, fb + 0x14, D(g, 0xa8c), D(fb, 4), fb + 4, 0, fb, 0, 0, t, D(D(g, 0xa84), 0xe9c), 0, 0,
                      1, 2);
    return D(fb, 0);
}
}  // namespace

// 0x0049c6a0 (ecx = turret). RocketTowerTurret (vtable 0x005a8b88) slot 0x2f (+0xbc), update: the base update
// 0x00497ed0; the player's position (player vt+0x6c). In range (|d|^2 <= +0x5b8^2 from +0x5bc): first frame in
// range clears +0x524 (byte +0x5d5); spin speed +0x5e8 -= dt * +0x5e0; when it drops below 0: +0x5e8 = 0, the turn
// loop sound is stopped ((Game+0x2c) vt+0xd8(&+0x5f0, 0)) and when vt+0x138(0) ("ready") the sight timer +0x5c8
// counts down by dt; at 0 it re-arms to 1.0 and a line-of-sight ray (Turret_SightHit) sets byte +0x5d4 = (hit ==
// player) (+0x5cc = time when seen); then when seen, or within the blind-fire time +0x5d0 of the last sighting,
// vt+0x13c(0, 0) fires (not ready: +0x5c8 = 0). Out of range: +0x5d5 = 0, spin speed += dt * +0x5dc up to +0x5d8,
// and the turn loop sound "wpn_rocket_tower_turn_lp" (0x0058ef88) is started ((Game+0x2c) vt+0xc4) when +0x5f0 is
// 0. Always: angle +0x5e4 += dt * speed, wrapped by [0x005bcb2c], written to the animation channel 0x00408800(eax =
// 4, ecx = turret) +4, then Anim_Set 0x00408d90(eax = 1, esi = turret; 0, 0x210, 0, 0, 0, 0). Returns its eax.
// Not executed in recorded runs.
uint32_t fn_0049c6a0(uint32_t t)
{
    float q[3];  // esp+0x2c
    orig::fn_00497ed0(t);
    VCall<uint32_t>(P(D(G(), 0x748)), 0x6c / 4, u(q));
    const float dx = q[0] - Fl(t, 0x5bc);
    const float dy = q[1] - Fl(t, 0x5c0);
    const float dz = q[2] - Fl(t, 0x5c4);
    const float r = Fl(t, 0x5b8);
    const float sum = dz * dz + dy * dy + dx * dx;
    if (r * r >= sum)  // `test ah, 1; jne`: out of range when r^2 < sum or unordered
    {
        if (B(t, 0x5d5) == 0)
            D(t, 0x524) = 0;
        B(t, 0x5d5) = 1;
        const float v = Fl(t, 0x5e8) - Dt() * Fl(t, 0x5e0);
        Fl(t, 0x5e8) = v;
        if (v < K(0x0056c05c))
        {
            D(t, 0x5e8) = 0;
            VCall<uint32_t>(P(D(G(), 0x2c)), 0xd8 / 4, t + 0x5f0, 0u);
            if (static_cast<uint8_t>(VCall<uint32_t>(P(t), 0x138 / 4, 0u)))
            {
                const float c = Fl(t, 0x5c8) - Dt();
                Fl(t, 0x5c8) = c;
                if (c <= K(0x0056c05c))
                {
                    D(t, 0x5c8) = 0x3f800000;
                    const uint32_t hit = Turret_SightHit(t, t + 0x5bc);
                    const uint8_t seen = hit == D(G(), 0x748) ? 1 : 0;
                    B(t, 0x5d4) = seen;
                    if (seen)
                        D(t, 0x5cc) = TimeBits();
                }
                if (B(t, 0x5d4) != 0 || Time() - Fl(t, 0x5cc) < Fl(t, 0x5d0))
                    VCall<uint32_t>(P(t), 0x13c / 4, 0u, 0u);
            }
            else
            {
                D(t, 0x5c8) = 0;
            }
        }
    }
    else
    {
        B(t, 0x5d5) = 0;
        const float v = Dt() * Fl(t, 0x5dc) + Fl(t, 0x5e8);
        Fl(t, 0x5e8) = v;
        if (v > Fl(t, 0x5d8))
            D(t, 0x5e8) = D(t, 0x5d8);
        if (D(t, 0x5f0) == 0)
            D(t, 0x5f0) = VCall<uint32_t>(P(D(G(), 0x2c)), 0xc4 / 4, 0x0058ef88u, t, 1u, 0u, 0xffffffffu, 0u);
    }
    float a = Dt() * Fl(t, 0x5e8) + Fl(t, 0x5e4);
    Fl(t, 0x5e4) = a;
    if (a > K(0x005bcb2c))
    {
        do
        {
            a = a - K(0x005bcb2c);
        } while (a > K(0x005bcb2c));
        Fl(t, 0x5e4) = a;
    }
    const uint32_t ch = orig::fn_00408800(4, t);
    D(ch, 4) = D(t, 0x5e4);
    return orig::fn_00408d90(1, t, 0, 0x210, 0, 0, 0, 0);
}
static uint32_t fn_0049c6a0_Abi(uint32_t ecx)
{
    return fn_0049c6a0(ecx);
}
ABI_ENTRY_0049c6a0(fn_0049c6a0_Entry, fn_0049c6a0_Abi)
HOOK(0x0049c6a0, "fn_0049c6a0", fn_0049c6a0_Entry, nullptr);

// 0x0049c9b0 (ecx = turret; stack: a0, matrix, props, a3; ret 0x10). RocketBudTurret (vtable 0x005adde8) slot 6
// (load): the base load 0x00497d00(ecx; a0..a3); "active_range" (+0x538, 500), "inaccuracy_range" (+0x53c, 10) and
// "blind_fire_duration" (+0x548, 10) (Object_ReadValuePropDef 0x004534a0, esi = a3); +0x524 = time; ObjectList_Link
// 0x004546d0 (esi = Game+0xa84 +0xea4, edi = turret); +0x550..+0x558 = the matrix translation (no null check),
// +0x540 = 1.0, +0x544 = 0, byte +0x54c = 0. Returns the matrix pointer (eax). Not executed in recorded runs.
uint32_t fn_0049c9b0(uint32_t t, uint32_t a0, uint32_t mat, uint32_t props, uint32_t a3)
{
    orig::fn_00497d00(t, a0, mat, props, a3);
    orig::fn_004534a0(a3, t + 0x538, t, 0x0058efe4, props, 0x43fa0000, 0);
    orig::fn_004534a0(a3, t + 0x53c, t, 0x0058ef74, props, 0x41200000, 0);
    orig::fn_004534a0(a3, t + 0x548, t, 0x0058efa4, props, 0x41200000, 0);
    D(t, 0x524) = TimeBits();
    orig::fn_004546d0(D(D(G(), 0xa84), 0xea4), t);
    D(t, 0x550) = D(mat, 0xc);
    D(t, 0x554) = D(mat, 0x1c);
    D(t, 0x558) = D(mat, 0x2c);
    D(t, 0x540) = 0x3f800000;
    D(t, 0x544) = 0;
    B(t, 0x54c) = 0;
    return mat;
}
static uint32_t fn_0049c9b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0049c9b0(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0049c9b0(fn_0049c9b0_Entry, fn_0049c9b0_Abi)
HOOK(0x0049c9b0, "fn_0049c9b0", fn_0049c9b0_Entry, nullptr);

// 0x0049ca90 (ecx = object; stack: desc; ret 4). RocketTowerTurret (0x005a8b88) / RocketBudTurret (0x005adde8) slot 7
// (+0x1c): the base slot 0x00497f40(ecx; desc), then desc[0] = 5. symbols/abi.csv lists no register input, but
// 0x00497f40 passes ecx on to 0x004527d0 (ecx input), so the entry stub below forwards ecx. Not executed in recorded
// runs.
uint32_t fn_0049ca90(uint32_t obj, uint32_t desc)
{
    const uint32_t r = orig::fn_00497f40(obj, desc);
    D(desc, 0) = 5;
    return r;
}
static __declspec(naked) void fn_0049ca90_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 28]
        push ecx
        call fn_0049ca90
        add esp, 8
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret 4
    }
}
HOOK(0x0049ca90, "fn_0049ca90", fn_0049ca90_Entry, nullptr);

// 0x0049cab0 (ecx = turret; stack: a0, a1 unused; ret 8). RocketBudTurret (vtable 0x005adde8) slot 0x4f (+0x13c),
// "fire": muzzle matrix 0x00453c50(turret, +0x4a0, &m, 1, 0, 0, 1, 0, 0) (al = 0: returns its eax); aim at the
// player's position (vt+0x6c) with x and z each spread by rand * 2s * [0x005bcbc0] - s (s = +0x53c "inaccuracy");
// 0x0047de40(eax = &muzzle, ebx = &aim; Game+0xaa8, turret, +0x460, 0), vt+0x144(0), +0x524 = time; al = (result
// != 0). Not executed in recorded runs.
uint32_t fn_0049cab0(uint32_t t, uint32_t /*a0*/, uint32_t /*a1*/)
{
    uint32_t f[0x68 / 4];  // original frame, from its esi-pushed base
    const uint32_t fb = u(f);
    const uint32_t ok = orig::fn_00453c50(t, t + 0x4a0, fb + 0x24, 1, 0, 0, 1, 0, 0);
    if (static_cast<uint8_t>(ok) == 0)
        return ok;
    D(fb, 0x18) = D(fb, 0x30);  // muzzle = m[3], m[7], m[11]
    D(fb, 0x20) = D(fb, 0x50);
    D(fb, 0x1c) = D(fb, 0x40);
    VCall<uint32_t>(P(D(G(), 0x748)), 0x6c / 4, fb + 0x0c);
    Fl(fb, 0) = -Fl(t, 0x53c);
    D(fb, 8) = D(t, 0x53c);
    int32_t r = Rand();
    D(fb, 4) = D(t, 0x53c);
    Fl(fb, 0x0c) = static_cast<float>(r) * (Fl(fb, 8) - Fl(fb, 0)) * K(0x005bcbc0) + Fl(fb, 0) + Fl(fb, 0x0c);
    Fl(fb, 0) = -Fl(t, 0x53c);
    r = Rand();
    Fl(fb, 0x10) = static_cast<float>(r) * (Fl(fb, 4) - Fl(fb, 0)) * K(0x005bcbc0) + Fl(fb, 0) + Fl(fb, 0x10);
    const uint32_t res = orig::fn_0047de40(fb + 0x18, fb + 0x0c, D(G(), 0xaa8), t, t + 0x460, 0);
    VCall<uint32_t>(P(t), 0x144 / 4, 0u);
    D(t, 0x524) = TimeBits();
    return res != 0 ? 1u : 0u;
}
static uint32_t fn_0049cab0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0049cab0(ecx, a0, a1);
}
ABI_ENTRY_0049cab0(fn_0049cab0_Entry, fn_0049cab0_Abi)
HOOK(0x0049cab0, "fn_0049cab0", fn_0049cab0_Entry, nullptr);

// 0x0049cbe0 (ecx = turret). RocketBudTurret (vtable 0x005adde8) slot 0x2f (+0xbc), update: the base update
// 0x00497ed0; when the player (vt+0x6c) is within +0x538 of +0x550: unless vt+0x138(0) ("ready"; else +0x540 = 0),
// the sight timer +0x540 counts down by dt; at 0 it re-arms to 1.0 and the line-of-sight ray (Turret_SightHit from
// +0x550) sets byte +0x54c = (hit == player) (+0x544 = time when seen); then when seen, or within +0x548 of the last
// sighting, vt+0x13c(0, 0) fires. Returns 0 (the original leaves the FPU status word in eax; callers do not use it).
// Not executed in recorded runs.
uint32_t fn_0049cbe0(uint32_t t)
{
    float q[3];  // esp+0x24
    orig::fn_00497ed0(t);
    VCall<uint32_t>(P(D(G(), 0x748)), 0x6c / 4, u(q));
    const float dx = q[0] - Fl(t, 0x550);
    const float dy = q[1] - Fl(t, 0x554);
    const float dz = q[2] - Fl(t, 0x558);
    const float r = Fl(t, 0x538);
    const float sum = dz * dz + dy * dy + dx * dx;
    if (r * r < sum)
        return 0;
    if (!static_cast<uint8_t>(VCall<uint32_t>(P(t), 0x138 / 4, 0u)))
    {
        D(t, 0x540) = 0;
        return 0;
    }
    const float c = Fl(t, 0x540) - Dt();
    Fl(t, 0x540) = c;
    if (c <= K(0x0056c05c))
    {
        D(t, 0x540) = 0x3f800000;
        const uint32_t hit = Turret_SightHit(t, t + 0x550);
        const uint8_t seen = hit == D(G(), 0x748) ? 1 : 0;
        B(t, 0x54c) = seen;
        if (seen)
            D(t, 0x544) = TimeBits();
    }
    if (B(t, 0x54c) == 0 && !(Time() - Fl(t, 0x544) < Fl(t, 0x548)))
        return 0;
    VCall<uint32_t>(P(t), 0x13c / 4, 0u, 0u);
    return 0;
}
static uint32_t fn_0049cbe0_Abi(uint32_t ecx)
{
    return fn_0049cbe0(ecx);
}
ABI_ENTRY_0049cbe0(fn_0049cbe0_Entry, fn_0049cbe0_Abi)
HOOK(0x0049cbe0, "fn_0049cbe0", fn_0049cbe0_Entry, nullptr);
