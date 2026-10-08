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
// (+0x5b8, 500), "spin_max_speed" (+0x5d8, 0.5), "spin_accel" (+0x5dc, 0.5), "spin_stop_accel" (+0x5e0, 0.8) and
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

// ---------------------------------------------------------------------------------------------------------------
// Level names and the player's weapon state. The weapon state (owner of the lists below; Game+0x748 +0xc24 for the
// player, see src/game/game_05.cpp) holds two slot lists, primary at +0x00 and special at +0x30, each {+0 owner,
// +4.. slot weapon ids (-1 = empty), +0x20 slot count, +0x24 current index, +0x28 pending index (-1 = none), +0x2c
// switch timer}; per weapon id w an ammo record at +0x60 + w*0xc {+0 clip, +4 spare, +8 id}; the weapon objects
// +0x1b0..+0x1b8 (current index +0x1bc), the parent object +0x1c0, attach bone names +0x1c8 / +0x1e8, a scale
// +0x228 and flags +0x22c. Weapon descriptors are Game+0x2770 + w*0x150 (+0x20 name, +0x140 max clip, +0x144
// max spare). Ids 6, 7, 8, 9 and 10 are the special weapons. Names below stay fn_XXXXXXXX (evidence is structural).

namespace
{
uint32_t Strnicmp(uint32_t ecx_, uint32_t a, uint32_t b, uint32_t n)
{
    return orig::fn_00544957(ecx_, a, b, n);
}
bool IsSpecialWeapon(uint32_t w)
{
    return w == 6 || w == 7 || w == 9 || w == 10 || w == 8;
}
uint32_t WeaponDesc(uint32_t w)
{
    return G() + w * 0x150;
}
}  // namespace

// 0x004a0690 (eax = kind, ecx (passed to _strnicmp), esi = name; stack: a0 unused; ret 4). Level name test by kind:
// 1: "lev_" followed by a digit (after "lev_demo_" for demo levels); 0: "pre_" or "post_"; 3: "lev_bonus"; any other
// kind: true. Returns al (upper eax bits as the original leaves them). _strnicmp 0x00544957 with the strings
// 0x0058ea78, 0x0058ea6c, 0x0058fd68, 0x0058fd70, 0x0058ea60.
uint32_t fn_004a0690(uint32_t kind, uint32_t ecx_, uint32_t name, uint32_t /*a0*/)
{
    if (kind == 1)
    {
        uint32_t n = 4;
        uint32_t r = Strnicmp(ecx_, name, 0x0058ea78, 4);
        if (r != 0)
            return r & 0xffffff00u;
        r = Strnicmp(ecx_, name, 0x0058ea6c, 9);
        if (r == 0)
            n = 9;
        const int8_t c = static_cast<int8_t>(B(name, n));
        if (c < 0x30)
            return r & 0xffffff00u;
        if (c <= 0x39)
            return (r & 0xffffff00u) | 1u;
        return r & 0xffffff00u;
    }
    uint32_t r;
    if (kind == 0)
    {
        r = Strnicmp(ecx_, name, 0x0058fd68, 4);
        if (r == 0)
            return 1;
        r = Strnicmp(ecx_, name, 0x0058fd70, 5);
    }
    else if (kind == 3)
    {
        r = Strnicmp(ecx_, name, 0x0058ea60, 9);
    }
    else
    {
        return (kind & 0xffffff00u) | 1u;
    }
    if (r != 0)
        return r & 0xffffff00u;
    return 1;
}
static uint32_t fn_004a0690_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return fn_004a0690(eax, ecx, esi, a0);
}
ABI_ENTRY_004a0690(fn_004a0690_Entry, fn_004a0690_Abi)
HOOK(0x004a0690, "fn_004a0690", fn_004a0690_Entry, nullptr);

// 0x004a0720 (eax = path, ecx = kind, ebx = table). When 0x004a0690(kind, path) accepts the name, its file name
// (_splitpath 0x00538456 into a 0x100 buffer) is hashed (0x004620b0) and looked up in table +0x20c + kind*0x80
// (count at table +0x1f8 + kind*4). al = found.
uint32_t fn_004a0720(uint32_t path, uint32_t kind, uint32_t table)
{
    char buf[0x100];
    const uint32_t ok = fn_004a0690(kind, kind, path, table);
    if (static_cast<uint8_t>(ok) == 0)
        return ok & 0xffffff00u;
    orig::fn_00538456(kind, path, 0, 0, u(buf), 0);
    const uint32_t h = orig::fn_004620b0(u(buf));
    const uint32_t n = D(table + kind * 4, 0x1f8);
    uint32_t p = table + (kind << 7) + 0x20c;
    for (uint32_t i = 0; i < n; ++i, p += 4)
    {
        if (D(p, 0) == h)
            return (h & 0xffffff00u) | 1u;
    }
    return h & 0xffffff00u;
}
static uint32_t fn_004a0720_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx)
{
    return fn_004a0720(eax, ecx, ebx);
}
ABI_ENTRY_004a0720(fn_004a0720_Entry, fn_004a0720_Abi)
HOOK(0x004a0720, "fn_004a0720", fn_004a0720_Entry, nullptr);

// 0x004a0b10 (eax = list, ecx = owner, edx (overwritten); stack: count; ret 4). Slot list init: +0 = owner, five
// slots = -1, +0x18 = +0x1c = 0, +0x20 = count, +0x24 = 0, +0x28 = -1, +0x2c = 0.
void fn_004a0b10(uint32_t list, uint32_t owner, uint32_t count)
{
    D(list, 0) = owner;
    D(list, 4) = 0xffffffffu;
    D(list, 8) = 0xffffffffu;
    D(list, 0xc) = 0xffffffffu;
    D(list, 0x10) = 0xffffffffu;
    D(list, 0x14) = 0xffffffffu;
    D(list, 0x18) = 0;
    D(list, 0x1c) = 0;
    D(list, 0x20) = count;
    D(list, 0x24) = 0;
    D(list, 0x28) = 0xffffffffu;
    D(list, 0x2c) = 0;
}
static uint32_t fn_004a0b10_Abi(uint32_t eax, uint32_t ecx, uint32_t /*edx*/, uint32_t a0)
{
    fn_004a0b10(eax, ecx, a0);
    return eax;
}
ABI_ENTRY_004a0b10(fn_004a0b10_Entry, fn_004a0b10_Abi)
HOOK(0x004a0b10, "fn_004a0b10", fn_004a0b10_Entry, nullptr);

// 0x004a0b60 (edx = list, esi = weapon id). Index of the slot holding the id, or -1.
uint32_t fn_004a0b60(uint32_t list, uint32_t id)
{
    const uint32_t n = D(list, 0x20);
    for (uint32_t i = 0; i < n; ++i)
    {
        if (D(list + 4 + i * 4, 0) == id)
            return i;
    }
    return 0xffffffffu;
}
static uint32_t fn_004a0b60_Abi(uint32_t edx, uint32_t esi)
{
    return fn_004a0b60(edx, esi);
}
ABI_ENTRY_004a0b60(fn_004a0b60_Entry, fn_004a0b60_Abi)
HOOK(0x004a0b60, "fn_004a0b60", fn_004a0b60_Entry, nullptr);

// 0x004a0b50 (edx = list, esi = weapon id). al = the list holds the id (0x004a0b60 != -1); upper eax bits are the
// index's.
uint32_t fn_004a0b50(uint32_t list, uint32_t id)
{
    const uint32_t i = fn_004a0b60(list, id);
    return (i & 0xffffff00u) | (i != 0xffffffffu ? 1u : 0u);
}
static uint32_t fn_004a0b50_Abi(uint32_t edx, uint32_t esi)
{
    return fn_004a0b50(edx, esi);
}
ABI_ENTRY_004a0b50(fn_004a0b50_Entry, fn_004a0b50_Abi)
HOOK(0x004a0b50, "fn_004a0b50", fn_004a0b50_Entry, nullptr);

// 0x004a0b80 (esi = list; stack: id; ret 4). Puts the id in the first empty slot: al = 1, or al = 0 when full.
uint32_t fn_004a0b80(uint32_t list, uint32_t id)
{
    const uint32_t n = D(list, 0x20);
    uint32_t i = 0;
    for (; i < n; ++i)
    {
        if (D(list + 4 + i * 4, 0) == 0xffffffffu)
        {
            D(list + i * 4, 4) = id;
            return (i & 0xffffff00u) | 1u;
        }
    }
    return i & 0xffffff00u;
}
static uint32_t fn_004a0b80_Abi(uint32_t esi, uint32_t a0)
{
    return fn_004a0b80(esi, a0);
}
ABI_ENTRY_004a0b80(fn_004a0b80_Entry, fn_004a0b80_Abi)
HOOK(0x004a0b80, "fn_004a0b80", fn_004a0b80_Entry, nullptr);

// 0x004a0c10 / 0x004a0c70 (edx = list). The next / previous slot (wrapping, at most five steps from the current
// index +0x24) that holds a weapon with ammo (clip + spare != 0 in the owner's ammo record), or -1.
static uint32_t Slot_AmmoSum(uint32_t list, uint32_t id)
{
    const uint32_t e = D(list, 0) + (id * 3 + 0x18) * 4;
    return D(e, 4) + D(e, 0);
}
uint32_t fn_004a0c10(uint32_t list)
{
    uint32_t i = D(list, 0x24);
    uint32_t step = 0;
    for (;;)
    {
        const uint32_t k = step++;
        if (k > 4)
            break;
        const uint32_t last = D(list, 0x20) - 1;
        ++i;
        if (i > last)
            i = 0;
        const uint32_t id = D(list + i * 4, 4);
        if (id == 0xffffffffu)
            continue;
        if (Slot_AmmoSum(list, id) == 0)
            continue;
        break;
    }
    const uint32_t id = D(list + i * 4, 4);
    if (id == 0xffffffffu || Slot_AmmoSum(list, id) == 0)
        return 0xffffffffu;
    return i;
}
static uint32_t fn_004a0c10_Abi(uint32_t edx)
{
    return fn_004a0c10(edx);
}
ABI_ENTRY_004a0c10(fn_004a0c10_Entry, fn_004a0c10_Abi)
HOOK(0x004a0c10, "fn_004a0c10", fn_004a0c10_Entry, nullptr);

uint32_t fn_004a0c70(uint32_t list)
{
    uint32_t i = D(list, 0x24);
    uint32_t step = 0;
    for (;;)
    {
        const uint32_t k = step++;
        if (k > 4)
            break;
        const uint32_t last = D(list, 0x20) - 1;
        --i;
        if (i > last)
            i = last;
        const uint32_t id = D(list + i * 4, 4);
        if (id == 0xffffffffu)
            continue;
        if (Slot_AmmoSum(list, id) == 0)
            continue;
        break;
    }
    const uint32_t id = D(list + i * 4, 4);
    if (id == 0xffffffffu || Slot_AmmoSum(list, id) == 0)
        return 0xffffffffu;
    return i;
}
static uint32_t fn_004a0c70_Abi(uint32_t edx)
{
    return fn_004a0c70(edx);
}
ABI_ENTRY_004a0c70(fn_004a0c70_Entry, fn_004a0c70_Abi)
HOOK(0x004a0c70, "fn_004a0c70", fn_004a0c70_Entry, nullptr);

// 0x004a0bb0 / 0x004a0be0 (esi = list; stack: time; ret 4). Select the next / previous usable slot (0x004a0c10 /
// 0x004a0c70) as pending (+0x28) with the switch time +0x2c, unless there is none or it is the current one (then
// +0x28 = -1 and -1). Called by 0x004788a0. Not executed in recorded runs.
uint32_t fn_004a0bb0(uint32_t list, uint32_t time)
{
    const uint32_t i = fn_004a0c10(list);
    D(list, 0x28) = i;
    if (i != 0xffffffffu && i != D(list, 0x24))
    {
        D(list, 0x2c) = time;
        return i;
    }
    D(list, 0x28) = 0xffffffffu;
    return 0xffffffffu;
}
static uint32_t fn_004a0bb0_Abi(uint32_t esi, uint32_t a0)
{
    return fn_004a0bb0(esi, a0);
}
ABI_ENTRY_004a0bb0(fn_004a0bb0_Entry, fn_004a0bb0_Abi)
HOOK(0x004a0bb0, "fn_004a0bb0", fn_004a0bb0_Entry, nullptr);

uint32_t fn_004a0be0(uint32_t list, uint32_t time)
{
    const uint32_t i = fn_004a0c70(list);
    D(list, 0x28) = i;
    if (i != 0xffffffffu && i != D(list, 0x24))
    {
        D(list, 0x2c) = time;
        return i;
    }
    D(list, 0x28) = 0xffffffffu;
    return 0xffffffffu;
}
static uint32_t fn_004a0be0_Abi(uint32_t esi, uint32_t a0)
{
    return fn_004a0be0(esi, a0);
}
ABI_ENTRY_004a0be0(fn_004a0be0_Entry, fn_004a0be0_Abi)
HOOK(0x004a0be0, "fn_004a0be0", fn_004a0be0_Entry, nullptr);

// 0x004a0cd0 (esi = list, edi = weapon id). Removes the id from its slot (-1): al = (that slot is the current
// one); al = 0 when not found. Not executed in recorded runs.
uint32_t fn_004a0cd0(uint32_t list, uint32_t id)
{
    const uint32_t n = D(list, 0x20);
    uint32_t i = 0;
    for (; i < n; ++i)
    {
        if (D(list + 4 + i * 4, 0) == id)
        {
            D(list + i * 4, 4) = 0xffffffffu;
            return (i & 0xffffff00u) | (D(list, 0x24) == i ? 1u : 0u);
        }
    }
    return i & 0xffffff00u;
}
static uint32_t fn_004a0cd0_Abi(uint32_t esi, uint32_t edi)
{
    return fn_004a0cd0(esi, edi);
}
ABI_ENTRY_004a0cd0(fn_004a0cd0_Entry, fn_004a0cd0_Abi)
HOOK(0x004a0cd0, "fn_004a0cd0", fn_004a0cd0_Entry, nullptr);

// 0x004a0d00 (edx (passed on), esi = state). Weapon state init: primary list (3 slots), special list (+0x30, 5
// slots), weapon objects +0x1b0..+0x1b8 = 0, ammo records +0x60..+0x1b0 zeroed with ids 0..0x1b, then the remaining
// fields (+0x1c4 = -1, +0x228 = 1.0, +0x234 = 1.5, the rest 0). Returns the state. Called by 0x00437290 and
// 0x00461840.
uint32_t fn_004a0d00(uint32_t edx, uint32_t s)
{
    fn_004a0b10(s, s, 3);
    (void)edx;
    fn_004a0b10(s + 0x30, s, 5);
    D(s, 0x1b4) = 0;
    D(s, 0x1b0) = 0;
    D(s, 0x1b8) = 0;
    std::memset(P(s + 0x60), 0, 0x54 * 4);
    for (uint32_t i = 0; i < 0x1c; ++i)
        D(s + 0x68, i * 0xc) = i;
    D(s, 0x1c0) = 0;
    B(s, 0x1e8) = 0;
    B(s, 0x1c8) = 0;
    B(s, 0x208) = 0;
    D(s, 0x1c4) = 0xffffffffu;
    D(s, 0x22c) = 0;
    D(s, 0x228) = 0x3f800000;
    D(s, 0x1bc) = 0;
    D(s, 0x230) = 0;
    D(s, 0x234) = 0x3fc00000;
    return s;
}
static uint32_t fn_004a0d00_Abi(uint32_t edx, uint32_t esi)
{
    return fn_004a0d00(edx, esi);
}
ABI_ENTRY_004a0d00(fn_004a0d00_Entry, fn_004a0d00_Abi)
HOOK(0x004a0d00, "fn_004a0d00", fn_004a0d00_Entry, nullptr);

// 0x004a0da0 (esi = state; stack: scale; ret 4). Each weapon object +0x1b0/+0x1b4/+0x1b8 that exists gets
// vt+0x100(scale); +0x228 = scale. Called by 0x0042eb60.
uint32_t fn_004a0da0(uint32_t s, uint32_t scale)
{
    uint32_t r = 0;
    if (D(s, 0x1b0) != 0)
        r = VCall<uint32_t>(P(D(s, 0x1b0)), 0x100 / 4, scale);
    if (D(s, 0x1b4) != 0)
        r = VCall<uint32_t>(P(D(s, 0x1b4)), 0x100 / 4, scale);
    if (D(s, 0x1b8) != 0)
        r = VCall<uint32_t>(P(D(s, 0x1b8)), 0x100 / 4, scale);
    D(s, 0x228) = scale;
    return r;
}
static uint32_t fn_004a0da0_Abi(uint32_t esi, uint32_t a0)
{
    return fn_004a0da0(esi, a0);
}
ABI_ENTRY_004a0da0(fn_004a0da0_Entry, fn_004a0da0_Abi)
HOOK(0x004a0da0, "fn_004a0da0", fn_004a0da0_Entry, nullptr);

// 0x004a0e00 (al = show, esi = state). Shows (show != 0 and flag +0x22c bit 0 set: each weapon object vt+0xf4(1),
// bit cleared) or hides (show == 0 and bit clear: vt+0xf4(0), bit set) the weapon objects.
uint32_t fn_004a0e00(uint32_t eax, uint32_t s)
{
    const uint8_t f = B(s, 0x22c);
    uint32_t r = (eax & 0xffffff00u) | f;
    if (static_cast<uint8_t>(eax))
    {
        if (!(f & 1))
            return r;
        for (uint32_t off = 0x1b0; off <= 0x1b8; off += 4)
        {
            if (D(s, off) != 0)
                r = VCall<uint32_t>(P(D(s, off)), 0xf4 / 4, 1u);
        }
        D(s, 0x22c) &= 0xfffffffeu;
        return r;
    }
    if (f & 1)
        return r;
    for (uint32_t off = 0x1b0; off <= 0x1b8; off += 4)
    {
        if (D(s, off) != 0)
            r = VCall<uint32_t>(P(D(s, off)), 0xf4 / 4, 0u);
    }
    D(s, 0x22c) |= 1;
    return r;
}
static uint32_t fn_004a0e00_Abi(uint32_t eax, uint32_t esi)
{
    return fn_004a0e00(eax, esi);
}
ABI_ENTRY_004a0e00(fn_004a0e00_Entry, fn_004a0e00_Abi)
HOOK(0x004a0e00, "fn_004a0e00", fn_004a0e00_Entry, nullptr);

// 0x004a0ea0 (eax = bone name 1, ecx = parent, esi = state, edi = bone name 2; stack: flags; ret 4). +0x1c0 =
// parent; the names are copied (_strncpy 0x005382a0, 0x20) to +0x1c8 / +0x1e8 when given; +0x22c |= flags.
// Returns the new flags.
uint32_t fn_004a0ea0(uint32_t name1, uint32_t parent, uint32_t s, uint32_t name2, uint32_t flags)
{
    D(s, 0x1c0) = parent;
    if (name1 != 0)
        orig::fn_005382a0(s + 0x1c8, name1, 0x20);
    if (name2 != 0)
        orig::fn_005382a0(s + 0x1e8, name2, 0x20);
    const uint32_t f = D(s, 0x22c) | flags;
    D(s, 0x22c) = f;
    return f;
}
static uint32_t fn_004a0ea0_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return fn_004a0ea0(eax, ecx, esi, edi, a0);
}
ABI_ENTRY_004a0ea0(fn_004a0ea0_Entry, fn_004a0ea0_Abi)
HOOK(0x004a0ea0, "fn_004a0ea0", fn_004a0ea0_Entry, nullptr);

// 0x004a0ef0 (eax = weapon id, edi = primary list). Adds a non-special weapon id to the list (0x004a0b80) unless
// it is already there (0x004a0b50); al = 0 for special ids or when already present.
uint32_t fn_004a0ef0(uint32_t id, uint32_t list)
{
    if (IsSpecialWeapon(id))
        return id & 0xffffff00u;
    const uint32_t r = fn_004a0b50(list, id);
    if (static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    return fn_004a0b80(list, id);
}
static uint32_t fn_004a0ef0_Abi(uint32_t eax, uint32_t edi)
{
    return fn_004a0ef0(eax, edi);
}
ABI_ENTRY_004a0ef0(fn_004a0ef0_Entry, fn_004a0ef0_Abi)
HOOK(0x004a0ef0, "fn_004a0ef0", fn_004a0ef0_Entry, nullptr);

uint32_t fn_004a10e0(uint32_t s, uint32_t index);
uint32_t fn_004a12e0(uint32_t s);

// 0x004a0f30 (eax = weapon id, ecx = state). Removes a non-special weapon id from the primary list (0x004a0cd0);
// when it was the current one, a tail jump to 0x004a12e0 (eax = state) selects the previous usable one. Called by
// 0x00475650. Not executed in recorded runs.
uint32_t fn_004a0f30(uint32_t id, uint32_t s)
{
    if (IsSpecialWeapon(id))
        return id;
    const uint32_t r = fn_004a0cd0(s, id);
    if (!static_cast<uint8_t>(r))
        return r;
    return fn_004a12e0(s);
}
static uint32_t fn_004a0f30_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004a0f30(eax, ecx);
}
ABI_ENTRY_004a0f30(fn_004a0f30_Entry, fn_004a0f30_Abi)
HOOK(0x004a0f30, "fn_004a0f30", fn_004a0f30_Entry, nullptr);

// 0x004a0f70 (eax = new id, ecx = old id, edx = primary list). Replaces the old id's slot with the new id and
// re-equips the current slot (0x004a10e0(ebx = list; +0x24)); when the old id is absent, adds the new one
// (0x004a0ef0). Called by 0x00493130. Not executed in recorded runs.
uint32_t fn_004a0f70(uint32_t id, uint32_t old, uint32_t list)
{
    const uint32_t n = D(list, 0x20);
    for (uint32_t i = 0; i < n; ++i)
    {
        if (D(list + 4 + i * 4, 0) == old)
        {
            D(list + i * 4, 4) = id;
            return fn_004a10e0(list, D(list, 0x24));
        }
    }
    return fn_004a0ef0(id, list);
}
static uint32_t fn_004a0f70_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return fn_004a0f70(eax, ecx, edx);
}
ABI_ENTRY_004a0f70(fn_004a0f70_Entry, fn_004a0f70_Abi)
HOOK(0x004a0f70, "fn_004a0f70", fn_004a0f70_Entry, nullptr);

// 0x004a10b0 (eax = state, ecx = index). +0x1bc = index (the current weapon object).
void fn_004a10b0(uint32_t s, uint32_t index)
{
    D(s, 0x1bc) = index;
}
static uint32_t fn_004a10b0_Abi(uint32_t eax, uint32_t ecx)
{
    fn_004a10b0(eax, ecx);
    return eax;
}
ABI_ENTRY_004a10b0(fn_004a10b0_Entry, fn_004a10b0_Abi)
HOOK(0x004a10b0, "fn_004a10b0", fn_004a10b0_Entry, nullptr);

// 0x004a0ff0 (eax = state). Destroys the current weapon object (+0x1b0 + +0x1bc*4): Object_Detach 0x00453b20 (esi
// = object), vt+0xdc(), reference release (+0x28; 0x00487da0) and the slot cleared.
uint32_t fn_004a0ff0(uint32_t s)
{
    const uint32_t obj = D(s + D(s, 0x1bc) * 4, 0x1b0);
    if (obj == 0)
        return D(s, 0x1bc);
    orig::fn_00453b20(obj);
    VCall<uint32_t>(P(D(s + D(s, 0x1bc) * 4, 0x1b0)), 0xdc / 4);
    const uint32_t slot = s + D(s, 0x1bc) * 4 + 0x1b0;
    const uint32_t r = Ref_Release(D(slot, 0));
    D(slot, 0) = 0;
    return r;
}
static uint32_t fn_004a0ff0_Abi(uint32_t eax)
{
    return fn_004a0ff0(eax);
}
ABI_ENTRY_004a0ff0(fn_004a0ff0_Entry, fn_004a0ff0_Abi)
HOOK(0x004a0ff0, "fn_004a0ff0", fn_004a0ff0_Entry, nullptr);

// 0x004a1060 (eax = state). The same for the special weapon object +0x1b8.
uint32_t fn_004a1060(uint32_t s)
{
    const uint32_t slot = s + 0x1b8;
    const uint32_t obj = D(slot, 0);
    if (obj == 0)
        return s;
    orig::fn_00453b20(obj);
    VCall<uint32_t>(P(D(slot, 0)), 0xdc / 4);
    const uint32_t r = Ref_Release(D(slot, 0));
    D(slot, 0) = 0;
    return r;
}
static uint32_t fn_004a1060_Abi(uint32_t eax)
{
    return fn_004a1060(eax);
}
ABI_ENTRY_004a1060(fn_004a1060_Entry, fn_004a1060_Abi)
HOOK(0x004a1060, "fn_004a1060", fn_004a1060_Entry, nullptr);

// 0x004a0fb0 (edx (passed on), esi = state). Resets both slot lists (3 and 2 slots; 0x004a0b10) and destroys the
// weapon objects at index 1 and 0 (0x004a10b0 + 0x004a0ff0; the last as a tail jump). Called by 0x00432730,
// 0x00434ac0, 0x00472ab0.
uint32_t fn_004a0fb0(uint32_t edx, uint32_t s)
{
    (void)edx;
    fn_004a0b10(s, s, 3);
    fn_004a0b10(s + 0x30, s, 2);
    fn_004a10b0(s, 1);
    fn_004a0ff0(s);
    fn_004a10b0(s, 0);
    return fn_004a0ff0(s);
}
static uint32_t fn_004a0fb0_Abi(uint32_t edx, uint32_t esi)
{
    return fn_004a0fb0(edx, esi);
}
ABI_ENTRY_004a0fb0(fn_004a0fb0_Entry, fn_004a0fb0_Abi)
HOOK(0x004a0fb0, "fn_004a0fb0", fn_004a0fb0_Entry, nullptr);

// 0x004a10c0 (eax = primary list, esi = weapon id). Equips the slot holding the id: 0x004a10e0(ebx = list;
// 0x004a0b60(list, id)). Called by 0x00493130. Not executed in recorded runs.
uint32_t fn_004a10c0(uint32_t list, uint32_t id)
{
    return fn_004a10e0(list, fn_004a0b60(list, id));
}
static uint32_t fn_004a10c0_Abi(uint32_t eax, uint32_t esi)
{
    return fn_004a10c0(eax, esi);
}
ABI_ENTRY_004a10c0(fn_004a10c0_Entry, fn_004a10c0_Abi)
HOOK(0x004a10c0, "fn_004a10c0", fn_004a10c0_Entry, nullptr);

// 0x004a10e0 (ebx = state (its primary list); stack: index; ret 4). Equips slot `index`: destroys the current
// weapon object (0x004a0ff0), +0x24 = index; a matrix scaled by the parent's scale (0x004535b0 + Matrix_Scale
// 0x004e2d90) or the identity; ObjectSystem_CreateObject 0x0045f020(eax = 0, ecx = the weapon's descriptor
// (Game+0x2770 + id*0x150, id = 0x004a1660); Game+0xa84, &m, 0x3ff, 0, 0, 0, 0, 0xaa, 0) into the current object
// slot; then (object referenced) vt+0x100(+0x228), vt+0xf4(0) when hidden (+0x22c bit 0), and with a parent:
// vt+0x150(parent), bone = vt+0x14c(); with a bone ObjectList_Link 0x004546d0 (esi = Game+0xa84 +0xf1c) and
// Object_Attach 0x004539f0(eax = bone name +0x1c8 (object 0) / +0x1e8, ecx = parent, edx = object; bone, 0, 8 when
// +0x22c bit 1 else bone, 1, 0).
uint32_t fn_004a10e0(uint32_t s, uint32_t index)
{
    float sc[3];   // esp+8
    float m[17];   // esp+0x14
    uint32_t r = fn_004a0ff0(s);
    D(s, 0x24) = index;
    if (D(s, 0x1c0) != 0)
    {
        orig::fn_004535b0(u(sc), D(s, 0x1c0));
        orig::fn_004e2d90(u(m), u(sc), 0, 0, 0);
    }
    else
    {
        std::memset(m, 0, sizeof(m));
        D(u(m), 0) = 0x3f800000;
        D(u(m), 0x14) = 0x3f800000;
        D(u(m), 0x28) = 0x3f800000;
        D(u(m), 0x3c) = 0x3f800000;
    }
    const uint32_t w = orig::fn_004a1660(s);
    const uint32_t g = G();
    const uint32_t created =
        orig::fn_0045f020(0, w * 0x150 + g + 0x2770, D(g, 0xa84), u(m), 0x3ff, 0, 0, 0, 0, 0xaa, 0);
    D(s + D(s, 0x1bc) * 4, 0x1b0) = created;
    const uint32_t obj = D(s + D(s, 0x1bc) * 4, 0x1b0);
    if (obj == 0)
        return obj;
    ++D(obj, 0x28);
    r = VCall<uint32_t>(P(D(s + D(s, 0x1bc) * 4, 0x1b0)), 0x100 / 4, D(s, 0x228));
    if (B(s, 0x22c) & 1)
        r = VCall<uint32_t>(P(D(s + D(s, 0x1bc) * 4, 0x1b0)), 0xf4 / 4, 0u);
    const uint32_t parent = D(s, 0x1c0);
    if (parent == 0)
        return parent;
    VCall<uint32_t>(P(D(s + D(s, 0x1bc) * 4, 0x1b0)), 0x150 / 4, parent);
    const uint32_t bone = VCall<uint32_t>(P(D(s + D(s, 0x1bc) * 4, 0x1b0)), 0x14c / 4);
    if (bone == 0)
        return bone;
    orig::fn_004546d0(D(D(G(), 0xa84), 0xf1c), D(s + D(s, 0x1bc) * 4, 0x1b0));
    const uint8_t f = B(s, 0x22c);
    const uint32_t cur = D(s, 0x1bc);
    const uint32_t name = cur != 0 ? s + 0x1e8 : s + 0x1c8;
    if (f & 2)
        r = static_cast<uint32_t>(orig::fn_004539f0(name, D(s, 0x1c0), D(s + cur * 4, 0x1b0), bone, 0, 8));
    else
        r = static_cast<uint32_t>(orig::fn_004539f0(name, D(s, 0x1c0), D(s + cur * 4, 0x1b0), bone, 1, 0));
    return r;
}
static uint32_t fn_004a10e0_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_004a10e0(ebx, a0);
}
ABI_ENTRY_004a10e0(fn_004a10e0_Entry, fn_004a10e0_Abi)
HOOK(0x004a10e0, "fn_004a10e0", fn_004a10e0_Entry, nullptr);

// 0x004a12c0 (esi = state; stack: index; ret 4). Destroys the special weapon object (0x004a1060) and sets the
// special list's current index (+0x54). Returns index.
uint32_t fn_004a12c0(uint32_t s, uint32_t index)
{
    fn_004a1060(s);
    D(s, 0x54) = index;
    return index;
}
static uint32_t fn_004a12c0_Abi(uint32_t esi, uint32_t a0)
{
    return fn_004a12c0(esi, a0);
}
ABI_ENTRY_004a12c0(fn_004a12c0_Entry, fn_004a12c0_Abi)
HOOK(0x004a12c0, "fn_004a12c0", fn_004a12c0_Entry, nullptr);

// 0x004a12a0 (eax = weapon id, edi = state). Selects the special list slot holding the id: 0x004a12c0(esi =
// state; 0x004a0b60(state+0x30, id)). Called by the ammo pickups (0x004725e0 ...). Not executed in recorded runs.
uint32_t fn_004a12a0(uint32_t id, uint32_t s)
{
    return fn_004a12c0(s, fn_004a0b60(s + 0x30, id));
}
static uint32_t fn_004a12a0_Abi(uint32_t eax, uint32_t edi)
{
    return fn_004a12a0(eax, edi);
}
ABI_ENTRY_004a12a0(fn_004a12a0_Entry, fn_004a12a0_Abi)
HOOK(0x004a12a0, "fn_004a12a0", fn_004a12a0_Entry, nullptr);

// 0x004a12e0 (eax = state). Equips the previous usable primary slot (0x004a0c70, 0x004a10e0) and returns the new
// current index, or 0 when there is none. Not executed in recorded runs.
uint32_t fn_004a12e0(uint32_t s)
{
    const uint32_t i = fn_004a0c70(s);
    if (i == 0xffffffffu)
        return 0;
    fn_004a10e0(s, i);
    return D(s, 0x24);
}
static uint32_t fn_004a12e0_Abi(uint32_t eax)
{
    return fn_004a12e0(eax);
}
ABI_ENTRY_004a12e0(fn_004a12e0_Entry, fn_004a12e0_Abi)
HOOK(0x004a12e0, "fn_004a12e0", fn_004a12e0_Entry, nullptr);

// 0x004a1300 (eax = weapon id, edx = state). The weapon's clip ammo (record +0); id 0x1c means the current
// primary weapon (0x004a1660). Called by 0x00465a20.
uint32_t fn_004a1300(uint32_t id, uint32_t s)
{
    if (id == 0x1c)
    {
        const uint32_t w = orig::fn_004a1660(s);
        return D(s, (w + 8) * 3 * 4);
    }
    return D(s, (id * 3 + 0x18) * 4);
}
static uint32_t fn_004a1300_Abi(uint32_t eax, uint32_t edx)
{
    return fn_004a1300(eax, edx);
}
ABI_ENTRY_004a1300(fn_004a1300_Entry, fn_004a1300_Abi)
HOOK(0x004a1300, "fn_004a1300", fn_004a1300_Entry, nullptr);

// 0x004a1320 (eax = weapon id; stack: state; ret 4). The weapon's maximum clip (descriptor Game+0x28b0 +
// id*0x150); id 0x1c = the current primary weapon. Called by 0x00465a20.
uint32_t fn_004a1320(uint32_t id, uint32_t s)
{
    if (id == 0x1c)
    {
        const uint32_t w = orig::fn_004a1660(s);
        return D(G(), (w + 0x1f) * 0x150);
    }
    return D(G(), (id + 0x1f) * 0x150);
}
static uint32_t fn_004a1320_Abi(uint32_t eax, uint32_t a0)
{
    return fn_004a1320(eax, a0);
}
ABI_ENTRY_004a1320(fn_004a1320_Entry, fn_004a1320_Abi)
HOOK(0x004a1320, "fn_004a1320", fn_004a1320_Entry, nullptr);

// 0x004a1360 / 0x004a1380 (eax = weapon id, edx = state). The weapon's spare ammo (record +4); id 0x1c = the
// current primary (0x004a1660) / special (0x004a1670) weapon.
uint32_t fn_004a1360(uint32_t id, uint32_t s)
{
    if (id == 0x1c)
    {
        const uint32_t w = orig::fn_004a1660(s);
        return D(s + w * 0xc, 0x64);
    }
    return D(s + id * 0xc, 0x64);
}
static uint32_t fn_004a1360_Abi(uint32_t eax, uint32_t edx)
{
    return fn_004a1360(eax, edx);
}
ABI_ENTRY_004a1360(fn_004a1360_Entry, fn_004a1360_Abi)
HOOK(0x004a1360, "fn_004a1360", fn_004a1360_Entry, nullptr);

uint32_t fn_004a1380(uint32_t id, uint32_t s)
{
    if (id == 0x1c)
    {
        const uint32_t w = orig::fn_004a1670(s);
        return D(s + w * 0xc, 0x64);
    }
    return D(s + id * 0xc, 0x64);
}
static uint32_t fn_004a1380_Abi(uint32_t eax, uint32_t edx)
{
    return fn_004a1380(eax, edx);
}
ABI_ENTRY_004a1380(fn_004a1380_Entry, fn_004a1380_Abi)
HOOK(0x004a1380, "fn_004a1380", fn_004a1380_Entry, nullptr);

// 0x004a13a0 (eax = amount, ecx = weapon id; stack: state; ret 4). Adds ammo: special weapons (6..10) join the
// special list (0x004a0b50 / 0x004a0b80) and their spare count grows up to the descriptor maximum (+0x144) unless
// already full; other weapons fill the spare count up to +0x144 and the rest goes to the clip, capped at +0x140
// (refused when both are full). al = 1 when added, else 0 (also for amount 0 / -1 or id >= 0x1c). Called by the
// pickups (0x004725e0 ...).
uint32_t fn_004a13a0(uint32_t amount, uint32_t id, uint32_t s)
{
    if (amount == 0xffffffffu || amount == 0 || id >= 0x1c)
        return amount & 0xffffff00u;
    if (IsSpecialWeapon(id))
    {
        const uint32_t r = fn_004a0b50(s + 0x30, id);
        if (!static_cast<uint8_t>(r))
            fn_004a0b80(s + 0x30, id);
        const uint32_t maxp = WeaponDesc(id) + 0x28b4;
        const uint32_t p = s + id * 0xc + 0x64;
        uint32_t cur = D(p, 0);
        if (cur >= D(maxp, 0))
            return p & 0xffffff00u;
        cur += amount;
        D(p, 0) = cur;
        if (cur > D(maxp, 0))
            D(p, 0) = D(maxp, 0);
        return (p & 0xffffff00u) | 1u;
    }
    const uint32_t g = G();
    const uint32_t maxLoaded = D(g + id * 0x150, 0x28b4);
    const uint32_t p = s + id * 0xc + 0x64;
    const uint32_t cur = D(p, 0);
    if (cur >= maxLoaded)
    {
        if (D(s, (id * 3 + 0x18) * 4) >= D(g, (id + 0x1f) * 0x150))
            return cur & 0xffffff00u;
    }
    uint32_t take = maxLoaded - cur;
    if (!(take < amount))
        take = amount;
    D(p, 0) = cur + take;
    const uint32_t rp = s + (id * 3 + 0x18) * 4;
    const uint32_t res = D(rp, 0) + (amount - take);
    D(rp, 0) = res;
    const uint32_t maxRes = D(g, (id + 0x1f) * 0x150);
    if (res > maxRes)
        D(rp, 0) = maxRes;
    return (rp & 0xffffff00u) | 1u;
}
static uint32_t fn_004a13a0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_004a13a0(eax, ecx, a0);
}
ABI_ENTRY_004a13a0(fn_004a13a0_Entry, fn_004a13a0_Abi)
HOOK(0x004a13a0, "fn_004a13a0", fn_004a13a0_Entry, nullptr);
// 0x004a14e0 (eax = amount, ecx = weapon id; stack: state; ret 4). Sets ammo. Special weapons (6..10): amount != 0
// joins the special list (0x004a0b50 / 0x004a0b80) and sets the spare count, capped at the descriptor maximum
// (+0x144); amount 0 removes the id from the special list (0x004a0cd0) and zeroes the count. Others: amount -2 fills
// both clip (+0x140) and spare (+0x144) to the maximum; otherwise clip = min(amount, max clip) and spare
// = amount - clip. id >= 0x1c: nothing. Returns eax as the original leaves it. Called by 0x00467fe0, 0x00472930,
// 0x00475650.
uint32_t fn_004a14e0(uint32_t amount, uint32_t id, uint32_t s)
{
    if (id >= 0x1c)
        return amount;
    if (IsSpecialWeapon(id))
    {
        if (amount != 0)
        {
            const uint32_t r = fn_004a0b50(s + 0x30, id);
            if (!static_cast<uint8_t>(r))
                fn_004a0b80(s + 0x30, id);
            const uint32_t g = G();
            const uint32_t p = s + id * 0xc + 0x64;
            D(p, 0) = amount;
            const uint32_t mx = D(g + id * 0x150, 0x28b4);
            if (amount > mx)
                D(p, 0) = mx;
            return p;
        }
        const uint32_t r = fn_004a0b50(s + 0x30, id);
        if (static_cast<uint8_t>(r))
            fn_004a0cd0(s + 0x30, id);
        D(s + id * 0xc, 0x64) = 0;
        return id * 3;
    }
    if (amount == 0xfffffffeu)
    {
        const uint32_t g = G();
        D(s, (id * 3 + 0x18) * 4) = D(g, (id + 0x1f) * 0x150);
        D(s + id * 0xc, 0x64) = D(g + id * 0x150, 0x28b4);
        return g;
    }
    uint32_t res = D(G(), (id + 0x1f) * 0x150);
    if (amount < res)
        res = amount;
    D(s, (id * 3 + 0x18) * 4) = res;
    D(s + id * 0xc, 0x64) = amount - res;
    return id * 3;
}
static uint32_t fn_004a14e0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_004a14e0(eax, ecx, a0);
}
ABI_ENTRY_004a14e0(fn_004a14e0_Entry, fn_004a14e0_Abi)
HOOK(0x004a14e0, "fn_004a14e0", fn_004a14e0_Entry, nullptr);

// 0x004a15f0 (eax = state). The current weapon object (+0x1b0 + +0x1bc*4).
uint32_t fn_004a15f0(uint32_t s)
{
    return D(s + D(s, 0x1bc) * 4, 0x1b0);
}
static uint32_t fn_004a15f0_Abi(uint32_t eax)
{
    return fn_004a15f0(eax);
}
ABI_ENTRY_004a15f0(fn_004a15f0_Entry, fn_004a15f0_Abi)
HOOK(0x004a15f0, "fn_004a15f0", fn_004a15f0_Entry, nullptr);

// 0x004a1600 / 0x004a1630 (eax = state). The primary (+4) / special (+0x34) slot array. Not executed in recorded
// runs.
uint32_t fn_004a1600(uint32_t s)
{
    return s + 4;
}
static uint32_t fn_004a1600_Abi(uint32_t eax)
{
    return fn_004a1600(eax);
}
ABI_ENTRY_004a1600(fn_004a1600_Entry, fn_004a1600_Abi)
HOOK(0x004a1600, "fn_004a1600", fn_004a1600_Entry, nullptr);

uint32_t fn_004a1630(uint32_t s)
{
    return s + 0x34;
}
static uint32_t fn_004a1630_Abi(uint32_t eax)
{
    return fn_004a1630(eax);
}
ABI_ENTRY_004a1630(fn_004a1630_Entry, fn_004a1630_Abi)
HOOK(0x004a1630, "fn_004a1630", fn_004a1630_Entry, nullptr);

// 0x004a1610 / 0x004a1640 (ecx = state). Number of occupied slots of the primary / special list. Called by
// 0x00463fa0.
static uint32_t CountSlots(uint32_t list)
{
    uint32_t n = D(list, 0x20);
    uint32_t c = 0;
    uint32_t p = list + 4;
    for (; n != 0; --n, p += 4)
    {
        if (D(p, 0) != 0xffffffffu)
            ++c;
    }
    return c;
}
uint32_t fn_004a1610(uint32_t s)
{
    return CountSlots(s);
}
static uint32_t fn_004a1610_Abi(uint32_t ecx)
{
    return fn_004a1610(ecx);
}
ABI_ENTRY_004a1610(fn_004a1610_Entry, fn_004a1610_Abi)
HOOK(0x004a1610, "fn_004a1610", fn_004a1610_Entry, nullptr);

uint32_t fn_004a1640(uint32_t s)
{
    return CountSlots(s + 0x30);
}
static uint32_t fn_004a1640_Abi(uint32_t ecx)
{
    return fn_004a1640(ecx);
}
ABI_ENTRY_004a1640(fn_004a1640_Entry, fn_004a1640_Abi)
HOOK(0x004a1640, "fn_004a1640", fn_004a1640_Entry, nullptr);

// 0x004a1660 / 0x004a1670 (eax = state). The current primary (slot +0x24) / special (slot +0x54) weapon id.
uint32_t fn_004a1660(uint32_t s)
{
    return D(s + D(s, 0x24) * 4, 4);
}
static uint32_t fn_004a1660_Abi(uint32_t eax)
{
    return fn_004a1660(eax);
}
ABI_ENTRY_004a1660(fn_004a1660_Entry, fn_004a1660_Abi)
HOOK(0x004a1660, "fn_004a1660", fn_004a1660_Entry, nullptr);

uint32_t fn_004a1670(uint32_t s)
{
    return D(s + D(s, 0x54) * 4, 0x34);
}
static uint32_t fn_004a1670_Abi(uint32_t eax)
{
    return fn_004a1670(eax);
}
ABI_ENTRY_004a1670(fn_004a1670_Entry, fn_004a1670_Abi)
HOOK(0x004a1670, "fn_004a1670", fn_004a1670_Entry, nullptr);

// 0x004a1680 / 0x004a17a0 (ecx = state). A primary (+0x28) / special (+0x58) switch is pending (!= -1).
uint32_t fn_004a1680(uint32_t s)
{
    return D(s, 0x28) != 0xffffffffu ? 1u : 0u;
}
static uint32_t fn_004a1680_Abi(uint32_t ecx)
{
    return fn_004a1680(ecx);
}
ABI_ENTRY_004a1680(fn_004a1680_Entry, fn_004a1680_Abi)
HOOK(0x004a1680, "fn_004a1680", fn_004a1680_Entry, nullptr);

uint32_t fn_004a17a0(uint32_t s)
{
    return D(s, 0x58) != 0xffffffffu ? 1u : 0u;
}
static uint32_t fn_004a17a0_Abi(uint32_t ecx)
{
    return fn_004a17a0(ecx);
}
ABI_ENTRY_004a17a0(fn_004a17a0_Entry, fn_004a17a0_Abi)
HOOK(0x004a17a0, "fn_004a17a0", fn_004a17a0_Entry, nullptr);

// 0x004a1690 / 0x004a17b0 (eax = state). The pending primary / special switch is due: pending (!= -1) and its timer
// (+0x2c / +0x5c) <= 0.0. Called by 0x004788a0. Not executed in recorded runs.
uint32_t fn_004a1690(uint32_t s)
{
    if (D(s, 0x28) != 0xffffffffu && Fl(s, 0x2c) <= K(0x0056c05c))
        return 1;
    return 0;
}
static uint32_t fn_004a1690_Abi(uint32_t eax)
{
    return fn_004a1690(eax);
}
ABI_ENTRY_004a1690(fn_004a1690_Entry, fn_004a1690_Abi)
HOOK(0x004a1690, "fn_004a1690", fn_004a1690_Entry, nullptr);

uint32_t fn_004a17b0(uint32_t s)
{
    if (D(s, 0x58) != 0xffffffffu && Fl(s, 0x5c) <= K(0x0056c05c))
        return 1;
    return 0;
}
static uint32_t fn_004a17b0_Abi(uint32_t eax)
{
    return fn_004a17b0(eax);
}
ABI_ENTRY_004a17b0(fn_004a17b0_Entry, fn_004a17b0_Abi)
HOOK(0x004a17b0, "fn_004a17b0", fn_004a17b0_Entry, nullptr);

namespace
{
// The weapon-change chat line and sounds shared by 0x004a16b0 / 0x004a17d0: Text_Lookup 0x00495f00 (ebx = Game+0xac0)
// of the weapon name (descriptor +0x20) and of the format (fmt key), _swprintf 0x0053862e into a 0x200-byte buffer,
// Chat_AddMessage 0x0046aa60(eax = buffer; Game+0xaac, 2).
void WeaponChangeMessage(uint32_t w, uint32_t fmtKey)
{
    wchar_t buf[0x100];
    const uint32_t name = orig::fn_00495f00(w * 0x150 + G() + 0x2790, D(G(), 0xac0), 0);
    const uint32_t fmt = orig::fn_00495f00(fmtKey, D(G(), 0xac0), 0);
    orig::fn_0053862e(u(buf), fmt, name, 0);
    orig::fn_0046aa60(u(buf), D(G(), 0xaac), 2);
}
void PlaySound2d(uint32_t name)  // (Game+0x2c) vt+0xd0(name, 0, 0, 0, -1, 0)
{
    VCall<uint32_t>(P(D(G(), 0x2c)), 0xd0 / 4, name, 0u, 0u, 0u, 0xffffffffu, 0u);
}
}  // namespace

// 0x004a16b0 (esi = state). Completes a pending primary weapon switch: 0x004a10e0(ebx = state; +0x28), +0x28 = -1,
// +0x2c = 0; the chat line "ui_change_weapon" (0x0058e9f8) with the weapon's name; the sound
// "wpn_armed_jaws_weapons" (0x0058e9e0) for weapon 5 and "ui_weapon_put_away" (0x0058e9cc); word (Game+0xa6c)
// +0x390 = 2. Returns the new current index +0x24. Called by 0x00475ff0, 0x004788a0. Not executed in recorded runs.
uint32_t fn_004a16b0(uint32_t s)
{
    fn_004a10e0(s, D(s, 0x28));
    D(s, 0x28) = 0xffffffffu;
    D(s, 0x2c) = 0;
    WeaponChangeMessage(fn_004a1660(s), 0x0058e9f8);
    if (fn_004a1660(s) == 5)
        PlaySound2d(0x0058e9e0);
    PlaySound2d(0x0058e9cc);
    *reinterpret_cast<uint16_t*>(D(G(), 0xa6c) + 0x390) = 2;
    return D(s, 0x24);
}
static uint32_t fn_004a16b0_Abi(uint32_t esi)
{
    return fn_004a16b0(esi);
}
ABI_ENTRY_004a16b0(fn_004a16b0_Entry, fn_004a16b0_Abi)
HOOK(0x004a16b0, "fn_004a16b0", fn_004a16b0_Entry, nullptr);

// 0x004a17d0 (eax = state). Completes a pending special weapon switch: 0x004a12c0(esi = state; +0x58), +0x58 = -1,
// +0x5c = 0; the chat line "ui_change_special_weapon" (0x0058e9b0) and the sound "ui_weapon_put_away". Returns the
// new special index +0x54. Called by 0x004788a0. Not executed in recorded runs.
uint32_t fn_004a17d0(uint32_t s)
{
    fn_004a12c0(s, D(s, 0x58));
    D(s, 0x58) = 0xffffffffu;
    D(s, 0x5c) = 0;
    WeaponChangeMessage(fn_004a1670(s), 0x0058e9b0);
    PlaySound2d(0x0058e9cc);
    return D(s, 0x54);
}
static uint32_t fn_004a17d0_Abi(uint32_t eax)
{
    return fn_004a17d0(eax);
}
ABI_ENTRY_004a17d0(fn_004a17d0_Entry, fn_004a17d0_Abi)
HOOK(0x004a17d0, "fn_004a17d0", fn_004a17d0_Entry, nullptr);

// 0x004a1950 (eax = state). al = the special weapon cooldown +0x230 > 0.0.
uint32_t fn_004a1950(uint32_t s)
{
    return Fl(s, 0x230) > K(0x0056c05c) ? 1u : 0u;
}
static uint32_t fn_004a1950_Abi(uint32_t eax)
{
    return fn_004a1950(eax);
}
ABI_ENTRY_004a1950(fn_004a1950_Entry, fn_004a1950_Abi)
HOOK(0x004a1950, "fn_004a1950", fn_004a1950_Entry, nullptr);

// 0x004a1970 (ecx (frame slot, low byte replaced), esi = state). Fires the queued special weapon +0x1c4 from the
// parent +0x1c0 (none: al = 0): +0x230 = 0; 0x0047dbf0(eax = &+0x208 (emitter bone), ecx = the weapon descriptor
// Game+0x2770 + id*0x150, edx = parent; Game+0xaa8, parent, (id == 10)); on success the spare count drops by one
// unless Game+0x175 is set, +0x1c4 = -1 and for special weapon 9 the sound "wpn_black_hole_fire" (0x0058e99c).
// al = success. Called by 0x00475ff0, 0x004a1890. Not executed in recorded runs.
uint32_t fn_004a1970(uint32_t ecx_, uint32_t s)
{
    const uint32_t parent = D(s, 0x1c0);
    if (parent == 0)
        return 0;
    const uint32_t w = D(s, 0x1c4);
    const uint32_t flag = (ecx_ & 0xffffff00u) | (w == 10 ? 1u : 0u);
    const uint32_t g = G();
    D(s, 0x230) = 0;
    const uint32_t r = orig::fn_0047dbf0(s + 0x208, w * 0x150 + g + 0x2770, parent, D(g, 0xaa8), parent, flag);
    if (r != 0)
    {
        const uint32_t g2 = G();
        if (B(g2, 0x175) == 0)
            --D(s + D(s, 0x1c4) * 0xc, 0x64);
        D(s, 0x1c4) = 0xffffffffu;
        if (fn_004a1670(s) == 9)
            VCall<uint32_t>(P(D(g2, 0x2c)), 0xd0 / 4, 0x0058e99cu, 0u, 0u, 0u, 0xffffffffu, 0u);
    }
    return r != 0 ? 1u : 0u;
}
static uint32_t fn_004a1970_Abi(uint32_t ecx, uint32_t esi)
{
    return fn_004a1970(ecx, esi);
}
ABI_ENTRY_004a1970(fn_004a1970_Entry, fn_004a1970_Abi)
HOOK(0x004a1970, "fn_004a1970", fn_004a1970_Entry, nullptr);

// 0x004a1890 (ecx = state). Per-frame weapon timers: pending switch timers +0x2c / +0x5c -= dt (when pending,
// 0x004a1680 / 0x004a17a0); special cooldown +0x230 -= dt (when running, 0x004a1950) and at <= 0 it is cleared and
// the queued special weapon fires (0x004a1970). Called by 0x004788a0.
uint32_t fn_004a1890(uint32_t s)
{
    if (fn_004a1680(s))
        Fl(s, 0x2c) = Fl(s, 0x2c) - Dt();
    if (fn_004a17a0(s))
        Fl(s, 0x5c) = Fl(s, 0x5c) - Dt();
    if (!fn_004a1950(s))
        return 0;
    const float c = Fl(s, 0x230) - Dt();
    Fl(s, 0x230) = c;
    if (!(c <= K(0x0056c05c)))
        return 0;
    D(s, 0x230) = 0;
    return fn_004a1970(s, s);
}
static uint32_t fn_004a1890_Abi(uint32_t ecx)
{
    return fn_004a1890(ecx);
}
ABI_ENTRY_004a1890(fn_004a1890_Entry, fn_004a1890_Abi)
HOOK(0x004a1890, "fn_004a1890", fn_004a1890_Entry, nullptr);

// 0x004a1900 (edx = state; stack: weapon id; ret 4). The special weapon can fire: no cooldown (0x004a1950) and
// spare ammo (0x004a1380) > 0. Called by 0x004a1a50. Not executed in recorded runs.
uint32_t fn_004a1900(uint32_t s, uint32_t id)
{
    if (fn_004a1950(s))
        return 0;
    if (fn_004a1380(id, s) != 0)
        return 1;
    return 0;
}
static uint32_t fn_004a1900_Abi(uint32_t edx, uint32_t a0)
{
    return fn_004a1900(edx, a0);
}
ABI_ENTRY_004a1900(fn_004a1900_Entry, fn_004a1900_Abi)
HOOK(0x004a1900, "fn_004a1900", fn_004a1900_Entry, nullptr);

// 0x004a1930 (eax = state, ecx = weapon id; stack: cooldown; ret 4). Queues a special weapon: +0x1c4 = id, +0x234 =
// +0x230 = cooldown; al = 1. Called by 0x004a1aa0. Not executed in recorded runs.
uint32_t fn_004a1930(uint32_t s, uint32_t id, uint32_t cooldown)
{
    D(s, 0x1c4) = id;
    D(s, 0x234) = cooldown;
    D(s, 0x230) = cooldown;
    return (s & 0xffffff00u) | 1u;
}
static uint32_t fn_004a1930_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_004a1930(eax, ecx, a0);
}
ABI_ENTRY_004a1930(fn_004a1930_Entry, fn_004a1930_Abi)
HOOK(0x004a1930, "fn_004a1930", fn_004a1930_Entry, nullptr);

// 0x004a1a30 (eax = name, ecx = state). Copies the special weapon emitter bone name to +0x208 (_strncpy 0x005382a0,
// 0x20). Called by 0x00475650.
uint32_t fn_004a1a30(uint32_t name, uint32_t s)
{
    return orig::fn_005382a0(s + 0x208, name, 0x20);
}
static uint32_t fn_004a1a30_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004a1a30(eax, ecx);
}
ABI_ENTRY_004a1a30(fn_004a1a30_Entry, fn_004a1a30_Abi)
HOOK(0x004a1a30, "fn_004a1a30", fn_004a1a30_Entry, nullptr);

// 0x004a1a50 (edx = state). The current special weapon (0x004a1670) can be used: 6, 9, 10: 0x004a1900(edx; id);
// 7, 8: spare ammo of the current special (0x004a1380(eax = 0x1c)) > 0; others: al = 0. Called by 0x004788a0. Not
// executed in recorded runs.
uint32_t fn_004a1a50(uint32_t s)
{
    const uint32_t k = fn_004a1670(s) - 6;
    if (k > 4)
        return k & 0xffffff00u;
    if (k == 1 || k == 2)
        return fn_004a1380(0x1c, s) != 0 ? 1u : 0u;
    return fn_004a1900(s, fn_004a1670(s));
}
static uint32_t fn_004a1a50_Abi(uint32_t edx)
{
    return fn_004a1a50(edx);
}
ABI_ENTRY_004a1a50(fn_004a1a50_Entry, fn_004a1a50_Abi)
HOOK(0x004a1a50, "fn_004a1a50", fn_004a1a50_Entry, nullptr);

// 0x004a1aa0 (esi = state). Uses the current special weapon (0x004a1670; jump table 0x004a1b90): 6, 9, 10 queue it
// with a 0.4 s cooldown (0x004a1930); 7 spends one (spare +0xb8) unless Game+0x175 is set; 8 applies area damage
// around the player (player vt+0x54 position; AreaDamage_Apply 0x0047f020(ebx = &{1.0, r, r, 1.0}, r = Game+0xc84;
// 0, &pos, 0, 1.0, player, 0x20, 0)) and spends one (+0xc4) unless Game+0x175. Called by 0x00476a10, 0x004788a0. Not
// executed in recorded runs.
uint32_t fn_004a1aa0(uint32_t s)
{
    const uint32_t k = fn_004a1670(s) - 6;
    if (k > 4)
        return k;
    switch (k)
    {
    case 0:
        return fn_004a1930(s, 6, 0x3ecccccd);
    case 3:
        return fn_004a1930(s, 9, 0x3ecccccd);
    case 4:
        return fn_004a1930(s, 10, 0x3ecccccd);
    case 1:
    {
        const uint32_t g = G();
        if (B(g, 0x175) == 0)
            --D(s, 0xb8);
        return g;
    }
    default:
    {
        float f[0x1c / 4];  // original frame: pos +0, {1.0, r, r, 1.0} at +0xc
        const uint32_t fb = u(f);
        VCall<uint32_t>(P(D(G(), 0x748)), 0x54 / 4, fb);
        const uint32_t g = G();
        const float r = Fl(g, 0xc84);
        Fl(fb, 0x14) = r;
        Fl(fb, 0x10) = r;
        D(fb, 0xc) = 0x3f800000;
        D(fb, 0x18) = 0x3f800000;
        orig::fn_0047f020(fb + 0xc, 0, fb, 0, 0x3f800000, D(g, 0x748), 0x20, 0);
        const uint32_t g2 = G();
        if (B(g2, 0x175) == 0)
            --D(s, 0xc4);
        return g2;
    }
    }
}
static uint32_t fn_004a1aa0_Abi(uint32_t esi)
{
    return fn_004a1aa0(esi);
}
ABI_ENTRY_004a1aa0(fn_004a1aa0_Entry, fn_004a1aa0_Abi)
HOOK(0x004a1aa0, "fn_004a1aa0", fn_004a1aa0_Entry, nullptr);

// 0x004a1bb0 (eax = state). On the current weapon object: releases its effect +0x550 (Fx_Release 0x0041d030(Game
// +0xa90, &+0x550)) when set and clears flag bit 2 of +0x548. Called by 0x004690c0. Not executed in recorded runs.
uint32_t fn_004a1bb0(uint32_t s)
{
    const uint32_t obj = D(s + D(s, 0x1bc) * 4, 0x1b0);
    if (obj == 0)
        return s;
    uint32_t r = obj + 0x550;
    if (D(obj, 0x550) != 0)
        r = orig::fn_0041d030(D(G(), 0xa90), obj + 0x550);
    D(obj, 0x548) &= 0xfffffffbu;
    return r;
}
static uint32_t fn_004a1bb0_Abi(uint32_t eax)
{
    return fn_004a1bb0(eax);
}
ABI_ENTRY_004a1bb0(fn_004a1bb0_Entry, fn_004a1bb0_Abi)
HOOK(0x004a1bb0, "fn_004a1bb0", fn_004a1bb0_Entry, nullptr);
// ---------------------------------------------------------------------------------------------------------------
// WeaponObject (vtable 0x005adf60) and its PlayerWeapon / PlayerVindaloo subclasses (vtables 0x005a1348, 0x005a4858,
// 0x005a90a8, 0x005ac210, 0x005ad458, 0x005ae210). Fields (from the property names read in the load slots):
// +0x460 "projectile_name", +0x480 "hand_bone_name", +0x4a0 "emitter_bone_name", +0x4c0 "emitter_bone_name_2",
// +0x4e0 "muzzle_fx_name", +0x500 "muzzle_bone_name", +0x520 "muzzle_bone_name_2", +0x540 "recycle_time", +0x544
// last fire time, +0x548 flags (1 "two_handed", 2 "rapid_fire", 0x10 second emitter, 0x20 emitter toggle, 4 / 8
// muzzle effect state), +0x54c the owner (set by slot 0x54), +0x550 the muzzle effect; PlayerWeapon: +0x554 weapon
// id, +0x558 "clip_reload_time", +0x55c reload timer, +0x560 the owner's ammo record for the weapon (player
// +0xc84 + id*0xc; +0 clip, +4 spare, +8 id); PlayerVindaloo: +0x564 "start_angle_pitch", +0x568 "start_angle_yaw".

namespace
{
// x87 fsin / fcos of a single-precision angle (as src/game/game_05.cpp).
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
// `test al, 0x10; je; test al, 0x20; jne`: the second emitter is used when flag 0x10 is set and 0x20 clear.
bool UseSecondEmitter(uint32_t f)
{
    return (f & 0x10) && !(f & 0x20);
}
}  // namespace

// 0x004a1bf0 (ecx = weapon; stack: a0, matrix, props, a3; ret 0x10). WeaponObject (vtable 0x005adf60) slot 6 (load):
// Object_LoadProperties 0x004517b0(ecx; a0..a3); +0x548/+0x550/+0x54c/+0x544 = 0; 0x00408e30(eax = 0x81, ecx =
// weapon, edx = 1; 0, 0, 0, 0, 0); "recycle_time" (+0x540, 1.0), the string properties listed above
// (Object_ReadStringProp 0x004533a0, Object_ReadValuePropDef 0x004534a0, esi = a3) and the booleans "two_handed"
// (flag 1) and "rapid_fire" (flag 2) (Object_ReadBoolProp 0x004534f0, read into the a3 argument slot); a second
// emitter bone name sets flags 0x10 and 0x20 (two stores). Called by 0x004a2150.
uint32_t fn_004a1bf0(uint32_t w, uint32_t a0, uint32_t mat, uint32_t props, uint32_t a3)
{
    orig::fn_004517b0(w, a0, mat, props, a3);
    D(w, 0x548) = 0;
    D(w, 0x550) = 0;
    D(w, 0x54c) = 0;
    D(w, 0x544) = 0;
    orig::fn_00408e30(0x81, w, 1, 0, 0, 0, 0, 0);
    orig::fn_004534a0(a3, w + 0x540, w, 0x0058f640, props, 0x3f800000, 0);
    orig::fn_004533a0(0x0058a9a4, w + 0x460, w, 0x0058f630, props, a3, 0);
    orig::fn_004533a0(0x0058a9a4, w + 0x480, w, 0x0058e98c, props, a3, 0);
    uint32_t r = orig::fn_004533a0(0x0058a9a4, w + 0x4a0, w, 0x0058f608, props, a3, 0);
    uint32_t slot = a3;  // the a3 argument slot, reused as the bool output
    r = orig::fn_004534f0(r & 0xffffff00u, a3, u(&slot), w, 0x0058e980, props, 0);
    if (static_cast<uint8_t>(slot))
        D(w, 0x548) |= 1;
    orig::fn_004533a0(0x0058a9a4, w + 0x4c0, w, 0x0058f44c, props, a3, 0);
    orig::fn_004533a0(0x0058a9a4, w + 0x520, w, 0x0058f278, props, a3, 0);
    if (B(w, 0x4c0) != 0)
    {
        const uint32_t f = D(w, 0x548) | 0x10;
        D(w, 0x548) = f;
        D(w, 0x548) = f | 0x20;
    }
    orig::fn_004533a0(0x0058a9a4, w + 0x4e0, w, 0x0058f5f8, props, a3, 0);
    r = orig::fn_004533a0(0x0058a9a4, w + 0x500, w, 0x0058f5e4, props, a3, 0);
    r = orig::fn_004534f0(r & 0xffffff00u, a3, u(&slot), w, 0x0058f560, props, 0);
    const uint8_t b = static_cast<uint8_t>(slot);
    if (b)
        D(w, 0x548) |= 2;
    return (r & 0xffffff00u) | b;
}
static uint32_t fn_004a1bf0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004a1bf0(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004a1bf0(fn_004a1bf0_Entry, fn_004a1bf0_Abi)
HOOK(0x004a1bf0, "fn_004a1bf0", fn_004a1bf0_Entry, nullptr);

// 0x004a1d80 (ecx = weapon; stack: desc; ret 4). WeaponObject / PlayerWeapon / PlayerVindaloo slot 7 (+0x1c):
// 0x00457810(eax = weapon) (flag +0x424 |= 0x40), then desc[0] = 0x4000, byte desc+0xd = byte desc+0x7a = 1. Not
// executed in recorded runs.
uint32_t fn_004a1d80(uint32_t w, uint32_t desc)
{
    orig::fn_00457810(w);
    D(desc, 0) = 0x4000;
    B(desc, 0xd) = 1;
    B(desc, 0x7a) = 1;
    return desc;
}
static uint32_t fn_004a1d80_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004a1d80(ecx, a0);
}
ABI_ENTRY_004a1d80(fn_004a1d80_Entry, fn_004a1d80_Abi)
HOOK(0x004a1d80, "fn_004a1d80", fn_004a1d80_Entry, nullptr);

// 0x004a1da0 (ecx = weapon). WeaponObject / PlayerWeapon / PlayerVindaloo slot 0x37 (+0xdc): releases the muzzle
// effect +0x550 (Fx_Release 0x0041d030(Game+0xa90, &+0x550)) when set, then a tail jump to Object_Remove 0x004547b0
// (ecx = weapon).
uint32_t fn_004a1da0(uint32_t w)
{
    if (D(w, 0x550) != 0)
        orig::fn_0041d030(D(G(), 0xa90), w + 0x550);
    return orig::fn_004547b0(w);
}
static uint32_t fn_004a1da0_Abi(uint32_t ecx)
{
    return fn_004a1da0(ecx);
}
ABI_ENTRY_004a1da0(fn_004a1da0_Entry, fn_004a1da0_Abi)
HOOK(0x004a1da0, "fn_004a1da0", fn_004a1da0_Entry, nullptr);

// 0x004a1dd0 (ecx = weapon). WeaponObject slot 0x2f (+0xbc), update of the muzzle effect state: flag 4 (just fired)
// is cleared; else flag 8 is set, and on the frame after that the effect +0x550 is released (Fx_Release
// 0x0041d030). Called by 0x004a2220.
uint32_t fn_004a1dd0(uint32_t w)
{
    const uint32_t f = D(w, 0x548);
    if (f & 4)
    {
        D(w, 0x548) &= 0xfffffffbu;
        return f;
    }
    if (!(f & 8))
    {
        D(w, 0x548) |= 8;
        return f;
    }
    if (D(w, 0x550) == 0)
        return w + 0x550;
    return orig::fn_0041d030(D(G(), 0xa90), w + 0x550);
}
static uint32_t fn_004a1dd0_Abi(uint32_t ecx)
{
    return fn_004a1dd0(ecx);
}
ABI_ENTRY_004a1dd0(fn_004a1dd0_Entry, fn_004a1dd0_Abi)
HOOK(0x004a1dd0, "fn_004a1dd0", fn_004a1dd0_Entry, nullptr);

// 0x004a1e10 (ecx = weapon). Slot 0x53 (+0x14c): the hand bone name (+0x480). Not executed in recorded runs.
uint32_t fn_004a1e10(uint32_t w)
{
    return w + 0x480;
}
static uint32_t fn_004a1e10_Abi(uint32_t ecx)
{
    return fn_004a1e10(ecx);
}
ABI_ENTRY_004a1e10(fn_004a1e10_Entry, fn_004a1e10_Abi)
HOOK(0x004a1e10, "fn_004a1e10", fn_004a1e10_Entry, nullptr);

// 0x004a1e20 (ecx = weapon). Slot 0x52 (+0x148): al = "two_handed" (flag +0x548 bit 0). Not executed in recorded
// runs.
uint32_t fn_004a1e20(uint32_t w)
{
    return B(w, 0x548) & 1u;
}
static uint32_t fn_004a1e20_Abi(uint32_t ecx)
{
    return fn_004a1e20(ecx);
}
ABI_ENTRY_004a1e20(fn_004a1e20_Entry, fn_004a1e20_Abi)
HOOK(0x004a1e20, "fn_004a1e20", fn_004a1e20_Entry, nullptr);

// 0x004a1e30 (ecx = weapon; stack: owner; ret 4). Slot 0x54 (+0x150): +0x54c = owner. Not executed in recorded runs.
uint32_t fn_004a1e30(uint32_t w, uint32_t owner)
{
    D(w, 0x54c) = owner;
    return owner;
}
static uint32_t fn_004a1e30_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004a1e30(ecx, a0);
}
ABI_ENTRY_004a1e30(fn_004a1e30_Entry, fn_004a1e30_Abi)
HOOK(0x004a1e30, "fn_004a1e30", fn_004a1e30_Entry, nullptr);

// 0x004a1e40 (ecx = weapon). Ready to fire: flags +0x548 |= 4 then &= ~8 (two stores); al = 1 when never fired
// (+0x544 == 0.0) or when time - +0x544 > "recycle_time" +0x540. Called by 0x004a2140, 0x004a23b0.
uint32_t fn_004a1e40(uint32_t w)
{
    uint32_t f = D(w, 0x548) | 4;
    D(w, 0x548) = f;
    f &= 0xfffffff7u;
    D(w, 0x548) = f;
    if (Fl(w, 0x544) == K(0x0056c05c))
        return 1;
    if (Time() - Fl(w, 0x544) > Fl(w, 0x540))
        return 1;
    return 0;
}
static uint32_t fn_004a1e40_Abi(uint32_t ecx)
{
    return fn_004a1e40(ecx);
}
ABI_ENTRY_004a1e40(fn_004a1e40_Entry, fn_004a1e40_Abi)
HOOK(0x004a1e40, "fn_004a1e40", fn_004a1e40_Entry, nullptr);

// 0x004a1e90 (ecx = weapon; stack: outPos, outDir; ret 8). Slot 0x4f (+0x13c): the emitter matrix (0x00453c50 with
// +0x4a0, or +0x4c0 for the second emitter); outPos = its translation (m[3], m[7], m[11]); outDir (when given) =
// -(m[1], m[5], m[9]). Not executed in recorded runs.
uint32_t fn_004a1e90(uint32_t w, uint32_t outPos, uint32_t outDir)
{
    float m[17];
    const uint32_t bone = UseSecondEmitter(D(w, 0x548)) ? w + 0x4c0 : w + 0x4a0;
    orig::fn_00453c50(w, bone, u(m), 1, 0, 0, 1, 0, 0);
    Fl(outPos, 0) = m[3];
    D(outPos, 4) = D(u(m), 0x1c);
    D(outPos, 8) = D(u(m), 0x2c);
    if (outDir != 0)
    {
        Fl(outDir, 0) = -m[1];
        Fl(outDir, 4) = -m[5];
        Fl(outDir, 8) = -m[9];
    }
    return outDir;
}
static uint32_t fn_004a1e90_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004a1e90(ecx, a0, a1);
}
ABI_ENTRY_004a1e90(fn_004a1e90_Entry, fn_004a1e90_Abi)
HOOK(0x004a1e90, "fn_004a1e90", fn_004a1e90_Entry, nullptr);

// 0x004a1f10 (ecx = weapon). WeaponObject slot 0x51 (+0x144): +0x544 = time (last fire). Called by 0x004a2450.
uint32_t fn_004a1f10(uint32_t w)
{
    const uint32_t t = TimeBits();
    D(w, 0x544) = t;
    return t;
}
static uint32_t fn_004a1f10_Abi(uint32_t ecx)
{
    return fn_004a1f10(ecx);
}
ABI_ENTRY_004a1f10(fn_004a1f10_Entry, fn_004a1f10_Abi)
HOOK(0x004a1f10, "fn_004a1f10", fn_004a1f10_Entry, nullptr);

// 0x004a1f20 (ecx = weapon). Slot 0x55 (+0x154), muzzle effect: needs "muzzle_fx_name" and "muzzle_bone_name"
// (else al = 0). Rapid fire (flag 2): flags |= 4, &= ~8 and, when no effect is running, Fx_Create 0x0041cc60(eax =
// al cleared, ecx = Game+0xa90; &+0x4e0, &{0x24, 8, ..., +0x18 = &+0x500, +0x1c = weapon}) kept in +0x550.
// Otherwise a one-shot Fx_Create with {4, ..., +0x18 = muzzle bone (+0x520 for the second emitter), +0x1c =
// weapon}. al = 1. Not executed in recorded runs.
uint32_t fn_004a1f20(uint32_t w)
{
    uint32_t loc[0x80 / 4] = {};
    const uint32_t fx = w + 0x4e0;
    if (B(fx, 0) == 0)
        return 0;
    uint32_t bone = w + 0x500;
    if (B(bone, 0) == 0)
        return 0;
    uint32_t f = D(w, 0x548);
    if (f & 2)
    {
        f |= 4;
        D(w, 0x548) = f;
        f &= 0xfffffff7u;
        D(w, 0x548) = f;
        if (D(w, 0x550) != 0)
            return (bone & 0xffffff00u) | 1u;
        const uint32_t ecx_ = D(G(), 0xa90);
        loc[0x18 / 4] = bone;
        loc[0x1c / 4] = w;
        loc[0] = 0x24;
        loc[1] = 8;
        const uint32_t r = orig::fn_0041cc60(u(loc) & 0xffffff00u, ecx_, fx, u(loc));
        D(w, 0x550) = r;
        return (r & 0xffffff00u) | 1u;
    }
    if (UseSecondEmitter(f))
        bone = w + 0x520;
    loc[0x18 / 4] = bone;
    loc[0x1c / 4] = w;
    loc[0] = 4;
    const uint32_t r = orig::fn_0041cc60(bone & 0xffffff00u, D(G(), 0xa90), fx, u(loc));
    return (r & 0xffffff00u) | 1u;
}
static uint32_t fn_004a1f20_Abi(uint32_t ecx)
{
    return fn_004a1f20(ecx);
}
ABI_ENTRY_004a1f20(fn_004a1f20_Entry, fn_004a1f20_Abi)
HOOK(0x004a1f20, "fn_004a1f20", fn_004a1f20_Entry, nullptr);

// 0x004a2010 (eax = aim point, esi = weapon, edi = target; stack: bone, noShot; ret 8). Fires: no aim point: al = 0.
// Else the muzzle effect (vt+0x154); unless noShot, the emitter matrix (0x00453c50, emitter as in 0x004a1e90),
// flag 0x20 toggled (alternating emitters), the projectile 0x0047de40(eax = &muzzle, ebx = aim; Game+0xaa8, owner
// +0x54c, &+0x460, 0) and with a target its vt+0x14c(target, bone). Then vt+0x144() (last fire time) and al = 1.
// Called by 0x004284e0, 0x004a2100.
uint32_t fn_004a2010(uint32_t aim, uint32_t w, uint32_t target, uint32_t bone, uint32_t noShot)
{
    if (aim == 0)
        return 0;
    VCall<uint32_t>(P(w), 0x154 / 4);
    if (static_cast<uint8_t>(noShot) == 0)
    {
        float pos[3];  // esp+4
        float m[17];   // esp+0x10
        const uint32_t em = UseSecondEmitter(D(w, 0x548)) ? w + 0x4c0 : w + 0x4a0;
        orig::fn_00453c50(w, em, u(m), 1, 0, 0, 1, 0, 0);
        D(w, 0x548) ^= 0x20;
        D(u(pos), 0) = D(u(m), 0xc);
        D(u(pos), 4) = D(u(m), 0x1c);
        D(u(pos), 8) = D(u(m), 0x2c);
        const uint32_t proj = orig::fn_0047de40(u(pos), aim, D(G(), 0xaa8), D(w, 0x54c), w + 0x460, 0);
        if (proj != 0 && target != 0)
            VCall<uint32_t>(P(proj), 0x14c / 4, target, bone);
    }
    const uint32_t r = VCall<uint32_t>(P(w), 0x144 / 4);
    return (r & 0xffffff00u) | 1u;
}
static uint32_t fn_004a2010_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_004a2010(eax, esi, edi, a0, a1);
}
ABI_ENTRY_004a2010(fn_004a2010_Entry, fn_004a2010_Abi)
HOOK(0x004a2010, "fn_004a2010", fn_004a2010_Entry, nullptr);

// 0x004a2100 (esi = weapon). Fires at the target list's aim point: list = Game+0xa6c (0x00407ff0 is a bare `ret`
// that leaves eax = list; not called here), the point list+4, or list+0x58 +4 when list[0] is 0 (0x00408010);
// 0x004a2010(eax = point, esi, edi = 0; 0, 0). al = 1. Called by 0x004284e0. Not executed in recorded runs.
uint32_t fn_004a2100(uint32_t w)
{
    const uint32_t list = D(G(), 0xa6c);
    uint32_t p = list;
    if (D(p, 0) == 0)
        p = orig::fn_00408010(list);
    const uint32_t r = fn_004a2010(p + 4, w, 0, 0, 0);
    return (r & 0xffffff00u) | 1u;
}
static uint32_t fn_004a2100_Abi(uint32_t esi)
{
    return fn_004a2100(esi);
}
ABI_ENTRY_004a2100(fn_004a2100_Entry, fn_004a2100_Abi)
HOOK(0x004a2100, "fn_004a2100", fn_004a2100_Entry, nullptr);

// 0x004a2140 (ecx = weapon). WeaponObject slot 0x4e (+0x138): 0x004a1e40 then al = 1. Not executed in recorded runs.
uint32_t fn_004a2140(uint32_t w)
{
    return (fn_004a1e40(w) & 0xffffff00u) | 1u;
}
static uint32_t fn_004a2140_Abi(uint32_t ecx)
{
    return fn_004a2140(ecx);
}
ABI_ENTRY_004a2140(fn_004a2140_Entry, fn_004a2140_Abi)
HOOK(0x004a2140, "fn_004a2140", fn_004a2140_Entry, nullptr);

// 0x004a2150 (ecx = weapon; stack: a0, matrix, props, a3; ret 0x10). PlayerWeapon slot 6 (load): the WeaponObject
// load 0x004a1bf0; +0x554 = the weapon id whose descriptor name (Game+0x2770 + id*0x150) matches the object name
// (Object_GetName 0x00453270, _stricmp 0x005448ee; 0x1c descriptors) or -1, +0x560 = &player ammo record (Game+0x748
// +0xc84 + id*0xc) or 0; "clip_reload_time" (+0x558, 1.5); +0x55c = 0. Called by 0x004a24d0.
uint32_t fn_004a2150(uint32_t w, uint32_t a0, uint32_t mat, uint32_t props, uint32_t a3)
{
    fn_004a1bf0(w, a0, mat, props, a3);
    D(w, 0x554) = 0xffffffffu;
    D(w, 0x560) = 0;
    uint32_t off = 0;
    uint32_t i = 0;
    do
    {
        const uint32_t desc = off + G() + 0x2770;
        const uint32_t name = orig::fn_00453270(w);
        if (orig::fn_005448ee(desc, name, desc) == 0)
        {
            D(w, 0x554) = i;
            break;
        }
        off += 0x150;
        ++i;
    } while (off < 0x24c0);
    const uint32_t id = D(w, 0x554);
    if (id != 0xffffffffu)
        D(w, 0x560) = D(G(), 0x748) + (id * 3 + 0x321) * 4;
    const uint32_t r = orig::fn_004534a0(a3, w + 0x558, w, 0x0058e96c, props, 0x3fc00000, 0);
    D(w, 0x55c) = 0;
    return r;
}
static uint32_t fn_004a2150_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004a2150(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004a2150(fn_004a2150_Entry, fn_004a2150_Abi)
HOOK(0x004a2150, "fn_004a2150", fn_004a2150_Entry, nullptr);

// 0x004a2220 (ecx = weapon). PlayerWeapon / PlayerVindaloo slot 0x2f (+0xbc), update: 0x004a1dd0; while reloading
// (vt+0x164) the timer +0x55c counts down by dt and at <= 0 it is cleared and the reload completes (tail jump to
// vt+0x168). Returns 0 where the original leaves the FPU status word in eax.
uint32_t fn_004a2220(uint32_t w)
{
    fn_004a1dd0(w);
    const uint32_t r = VCall<uint32_t>(P(w), 0x164 / 4);
    if (!static_cast<uint8_t>(r))
        return r;
    const float c = Fl(w, 0x55c) - Dt();
    Fl(w, 0x55c) = c;
    if (!(c <= K(0x0056c05c)))
        return 0;
    D(w, 0x55c) = 0;
    return VCall<uint32_t>(P(w), 0x168 / 4);
}
static uint32_t fn_004a2220_Abi(uint32_t ecx)
{
    return fn_004a2220(ecx);
}
ABI_ENTRY_004a2220(fn_004a2220_Entry, fn_004a2220_Abi)
HOOK(0x004a2220, "fn_004a2220", fn_004a2220_Entry, nullptr);

// 0x004a2270 (ecx = weapon). PlayerWeapon slot 0x56 (+0x158), clip empty: al = (ammo record clip == 0); al = 0
// without a record or when Game+0x175 (infinite ammo, guess) is set. Not executed in recorded runs.
uint32_t fn_004a2270(uint32_t w)
{
    const uint32_t p = D(w, 0x560);
    if (p == 0)
        return 0;
    if (B(G(), 0x175) != 0)
        return p & 0xffffff00u;
    return (p & 0xffffff00u) | (D(p, 0) == 0 ? 1u : 0u);
}
static uint32_t fn_004a2270_Abi(uint32_t ecx)
{
    return fn_004a2270(ecx);
}
ABI_ENTRY_004a2270(fn_004a2270_Entry, fn_004a2270_Abi)
HOOK(0x004a2270, "fn_004a2270", fn_004a2270_Entry, nullptr);

// 0x004a22a0 (ecx = weapon). PlayerWeapon slot 0x57 (+0x15c), can reload: an ammo record with spare ammo (+4) and
// a clip below the descriptor's clip size (Game+0x28b0 + id*0x150). Not executed in recorded runs.
uint32_t fn_004a22a0(uint32_t w)
{
    const uint32_t p = D(w, 0x560);
    if (p == 0)
        return 0;
    if (D(p, 4) != 0 && D(p, 0) < D(G(), (D(p, 8) + 0x1f) * 0x150))
        return 1;
    return 0;
}
static uint32_t fn_004a22a0_Abi(uint32_t ecx)
{
    return fn_004a22a0(ecx);
}
ABI_ENTRY_004a22a0(fn_004a22a0_Entry, fn_004a22a0_Abi)
HOOK(0x004a22a0, "fn_004a22a0", fn_004a22a0_Entry, nullptr);

// 0x004a22e0 / 0x004a29d0 (ecx = weapon). PlayerWeapon slot 0x5a (+0x168), reload complete: when vt+0x15c() (can
// reload), moves min(clip size, clip size - clip, spare) into the clip (0x004a22e0 takes it from the spare,
// 0x004a29d0 (vtable 0x005a1348) does not), +0x55c = 0, al = 1. Not executed in recorded runs.
static uint32_t ReloadAmount(uint32_t size, uint32_t clip, uint32_t spare)
{
    const uint32_t room = size - clip;
    const uint32_t lim = size < room ? size : room;
    if (lim >= spare)
        return spare;
    return size < room ? size : room;
}
uint32_t fn_004a22e0(uint32_t w)
{
    const uint32_t r = VCall<uint32_t>(P(w), 0x15c / 4);
    if (!static_cast<uint8_t>(r))
        return r;
    const uint32_t g = G();
    const uint32_t p = D(w, 0x560);
    const uint32_t clip = D(p, 0);
    const uint32_t size = D(g, (D(p, 8) + 0x1f) * 0x150);
    D(p, 0) = ReloadAmount(size, clip, D(p, 4)) + clip;
    const uint32_t q = D(w, 0x560);
    const uint32_t diff = clip - D(q, 0);
    D(q, 4) = D(q, 4) + diff;
    D(w, 0x55c) = 0;
    return (diff & 0xffffff00u) | 1u;
}
static uint32_t fn_004a22e0_Abi(uint32_t ecx)
{
    return fn_004a22e0(ecx);
}
ABI_ENTRY_004a22e0(fn_004a22e0_Entry, fn_004a22e0_Abi)
HOOK(0x004a22e0, "fn_004a22e0", fn_004a22e0_Entry, nullptr);

uint32_t fn_004a29d0(uint32_t w)
{
    const uint32_t r = VCall<uint32_t>(P(w), 0x15c / 4);
    if (!static_cast<uint8_t>(r))
        return r;
    const uint32_t p = D(w, 0x560);
    const uint32_t size = D(G(), (D(p, 8) + 0x1f) * 0x150);
    const uint32_t clip = D(p, 0);
    const uint32_t take = ReloadAmount(size, clip, D(p, 4));
    D(p, 0) = clip + take;
    D(w, 0x55c) = 0;
    return (take & 0xffffff00u) | 1u;
}
static uint32_t fn_004a29d0_Abi(uint32_t ecx)
{
    return fn_004a29d0(ecx);
}
ABI_ENTRY_004a29d0(fn_004a29d0_Entry, fn_004a29d0_Abi)
HOOK(0x004a29d0, "fn_004a29d0", fn_004a29d0_Entry, nullptr);

// 0x004a2360 (ecx = weapon). PlayerWeapon slot 0x58 (+0x160), start reload: when vt+0x15c() (can reload), +0x55c =
// "clip_reload_time" +0x558 and al = 1. Not executed in recorded runs.
uint32_t fn_004a2360(uint32_t w)
{
    const uint32_t r = VCall<uint32_t>(P(w), 0x15c / 4);
    if (!static_cast<uint8_t>(r))
        return r;
    const uint32_t t = D(w, 0x558);
    D(w, 0x55c) = t;
    return (t & 0xffffff00u) | 1u;
}
static uint32_t fn_004a2360_Abi(uint32_t ecx)
{
    return fn_004a2360(ecx);
}
ABI_ENTRY_004a2360(fn_004a2360_Entry, fn_004a2360_Abi)
HOOK(0x004a2360, "fn_004a2360", fn_004a2360_Entry, nullptr);

// 0x004a2390 (ecx = weapon). PlayerWeapon slot 0x59 (+0x164), reloading: +0x55c > 0.0. Not executed in recorded runs.
uint32_t fn_004a2390(uint32_t w)
{
    return Fl(w, 0x55c) > K(0x0056c05c) ? 1u : 0u;
}
static uint32_t fn_004a2390_Abi(uint32_t ecx)
{
    return fn_004a2390(ecx);
}
ABI_ENTRY_004a2390(fn_004a2390_Entry, fn_004a2390_Abi)
HOOK(0x004a2390, "fn_004a2390", fn_004a2390_Entry, nullptr);

// 0x004a23b0 (ecx = weapon). PlayerWeapon / PlayerVindaloo slot 0x4e (+0x138), ready to fire: not reloading
// (vt+0x164), clip not empty (vt+0x158) and 0x004a1e40. Called by 0x004a2910. Not executed in recorded runs.
uint32_t fn_004a23b0(uint32_t w)
{
    uint32_t r = VCall<uint32_t>(P(w), 0x164 / 4);
    if (static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    r = VCall<uint32_t>(P(w), 0x158 / 4);
    if (static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    r = fn_004a1e40(w);
    return (r & 0xffffff00u) | (static_cast<uint8_t>(r) != 0 ? 1u : 0u);
}
static uint32_t fn_004a23b0_Abi(uint32_t ecx)
{
    return fn_004a23b0(ecx);
}
ABI_ENTRY_004a23b0(fn_004a23b0_Entry, fn_004a23b0_Abi)
HOOK(0x004a23b0, "fn_004a23b0", fn_004a23b0_Entry, nullptr);

// 0x004a23e0 (ecx = weapon). PlayerWeapon slot 0x50 (+0x140) (vtables 0x005a1348, 0x005ad458, 0x005ae210), fire:
// with an owner (+0x54c), 0x0047dbf0(eax = &+0x4a0, ecx = &+0x460, edx = weapon; Game+0xaa8, owner, vt+0x16c()),
// the muzzle effect vt+0x154() and on success vt+0x144(); al = success. Not executed in recorded runs.
uint32_t fn_004a23e0(uint32_t w)
{
    if (D(w, 0x54c) == 0)
        return 0;
    const uint32_t v = VCall<uint32_t>(P(w), 0x16c / 4);
    const uint32_t r = orig::fn_0047dbf0(w + 0x4a0, w + 0x460, w, D(G(), 0xaa8), D(w, 0x54c), v);
    VCall<uint32_t>(P(w), 0x154 / 4);
    if (r != 0)
        VCall<uint32_t>(P(w), 0x144 / 4);
    return r != 0 ? 1u : 0u;
}
static uint32_t fn_004a23e0_Abi(uint32_t ecx)
{
    return fn_004a23e0(ecx);
}
ABI_ENTRY_004a23e0(fn_004a23e0_Entry, fn_004a23e0_Abi)
HOOK(0x004a23e0, "fn_004a23e0", fn_004a23e0_Entry, nullptr);

// 0x004a2450 (ecx = weapon). PlayerWeapon / PlayerVindaloo slot 0x51 (+0x144): the last fire time (0x004a1f10) and,
// unless Game+0x175 is set, one round less in the clip (when not empty). Not executed in recorded runs.
uint32_t fn_004a2450(uint32_t w)
{
    fn_004a1f10(w);
    const uint32_t g = G();
    if (B(g, 0x175) != 0)
        return g;
    const uint32_t p = D(w, 0x560);
    const uint32_t c = D(p, 0);
    if (c != 0)
        D(p, 0) = c - 1;
    return p;
}
static uint32_t fn_004a2450_Abi(uint32_t ecx)
{
    return fn_004a2450(ecx);
}
ABI_ENTRY_004a2450(fn_004a2450_Entry, fn_004a2450_Abi)
HOOK(0x004a2450, "fn_004a2450", fn_004a2450_Entry, nullptr);

uint32_t fn_004a2a70(uint32_t w, uint32_t bone, uint32_t owner, uint32_t proj);

// 0x004a2480 (ecx = weapon). PlayerWeapon slot 0x50 (+0x140) (vtable 0x005ac210), fire along the camera ray: with
// an owner, 0x004a2a70(weapon, &+0x4a0, owner, &+0x460) and on success vt+0x154(), vt+0x144() and al = 1. Not
// executed in recorded runs.
uint32_t fn_004a2480(uint32_t w)
{
    const uint32_t owner = D(w, 0x54c);
    if (owner == 0)
        return 0;
    const uint32_t r = fn_004a2a70(w, w + 0x4a0, owner, w + 0x460);
    if (!static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    VCall<uint32_t>(P(w), 0x154 / 4);
    const uint32_t r2 = VCall<uint32_t>(P(w), 0x144 / 4);
    return (r2 & 0xffffff00u) | 1u;
}
static uint32_t fn_004a2480_Abi(uint32_t ecx)
{
    return fn_004a2480(ecx);
}
ABI_ENTRY_004a2480(fn_004a2480_Entry, fn_004a2480_Abi)
HOOK(0x004a2480, "fn_004a2480", fn_004a2480_Entry, nullptr);

// 0x004a24d0 (ecx = weapon; stack: a0, matrix, props, a3; ret 0x10). PlayerVindaloo (vtable 0x005a90a8) slot 6
// (load): the PlayerWeapon load 0x004a2150; "start_angle_yaw" (+0x568, 6) and "start_angle_pitch" (+0x564, 6)
// (Object_ReadValuePropDef 0x004534a0, esi = a3), both scaled by [0x005bcba0] (degrees to radians, guess). Not
// executed in recorded runs.
uint32_t fn_004a24d0(uint32_t w, uint32_t a0, uint32_t mat, uint32_t props, uint32_t a3)
{
    fn_004a2150(w, a0, mat, props, a3);
    orig::fn_004534a0(a3, w + 0x568, w, 0x0058e95c, props, 0x40c00000, 0);
    const uint32_t r = orig::fn_004534a0(a3, w + 0x564, w, 0x0058e948, props, 0x40c00000, 0);
    Fl(w, 0x568) = Fl(w, 0x568) * K(0x005bcba0);
    Fl(w, 0x564) = Fl(w, 0x564) * K(0x005bcba0);
    return r;
}
static uint32_t fn_004a24d0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004a24d0(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004a24d0(fn_004a24d0_Entry, fn_004a24d0_Abi)
HOOK(0x004a24d0, "fn_004a24d0", fn_004a24d0_Entry, nullptr);
// 0x004a2550 (ecx = weapon). PlayerVindaloo (vtable 0x005a90a8) slot 0x50 (+0x140), fire a fan of rockets. With an
// owner (+0x54c) and the emitter matrix (0x00453c50 with +0x4a0; else al = 0): the muzzle position, the default aim
// point (0x00408030: target list +0x5c) and the camera matrix (0x0040fa70(eax = Game+0xa68)); empty entries of the
// target list's four candidates (Game+0xa6c +0x154, 0x2c bytes each, 0x00407fe0) are filled from earlier ones. Yaw
// offsets {a, -a, a, -a} (a = +0x568) and pitch offsets {s + b, s + b, s - b*[0x005bcb4c]} (b = +0x564, s = the
// camera's downward tilt -m[10] clamped at 0). Game+0xdd0 set: 4 rockets, Game+0x2718 += 3 and
// "wpn_rome_vindaloo_fire_4" (0x0058e92c); else 2 rockets, += 1 and "wpn_rome_vindaloo_fire_2" (0x0058e910). Each
// rocket: direction = -(row 2 of RotX(pitch) * RotY(yaw) * camera) (Matrix_Multiply 0x004e2910 twice), 0x0047e050(edi
// = &muzzle; Game+0xaa8, owner, &+0x460, &dir, 0), homing on the candidate's object (vt+0x14c(object, "ref_hit_00"
// 0x005b68fc) when its byte +0x20 is set, else vt+0x150(&candidate +4)) or the default point. Then vt+0x154() and,
// when the last rocket exists, vt+0x144(); al = last rocket != 0. symbols/abi.csv says 16 caller-cleaned stack
// arguments: the function reads none (an ebp frame aligned to 8; plain ret), so the entry stub below forwards ecx
// only. The 0x200-byte frame F is kept as one array. Not executed in recorded runs.
uint32_t fn_004a2550(uint32_t w)
{
    alignas(8) uint8_t F[0x200];
    const uint32_t fb = u(F);
    D(fb, 4) = w;
    if (D(w, 0x54c) == 0)
        return 0;
    uint32_t cand = orig::fn_00407fe0(D(G(), 0xa6c));
    const uint32_t ok = orig::fn_00453c50(w, w + 0x4a0, fb + 0xe0, 1, 0, 0, 1, 0, 0);
    if (static_cast<uint8_t>(ok) == 0)
        return ok & 0xffffff00u;
    const uint32_t g = G();
    D(fb, 0x1c) = D(fb, 0xfc);  // muzzle = m[3], m[7], m[11]
    D(fb, 0x18) = D(fb, 0xec);
    D(fb, 0x20) = D(fb, 0x10c);
    orig::fn_00408030(fb + 0x44, D(g, 0xa6c));
    orig::fn_0040fa70(D(g, 0xa68), fb + 0x170);
    Fl(fb, 0x14) = -Fl(fb, 0x198);
    if (D(cand, 0x84) == 0)
        std::memcpy(P(cand + 0x84), P(cand), 0x2c);
    if (D(cand, 0x58) == 0)
        std::memcpy(P(cand + 0x58), P(D(cand, 0x2c) != 0 ? cand + 0x2c : cand), 0x2c);
    if (D(cand, 0x2c) == 0)
        std::memcpy(P(cand + 0x2c), P(cand), 0x2c);
    const float s = Fl(fb, 0x14) < K(0x0056c05c) ? -Fl(fb, 0x14) : K(0x0056c05c);
    const uint32_t a = D(w, 0x568);
    const float na = -u2f(a);
    Fl(fb, 0x28) = na;
    Fl(fb, 0x30) = na;
    D(fb, 0x24) = a;
    const float sb = s + Fl(w, 0x564);
    Fl(fb, 0x34) = sb;
    Fl(fb, 0x38) = sb;
    D(fb, 0x2c) = a;
    const float sm = s - Fl(w, 0x564) * K(0x005bcb4c);
    Fl(fb, 0x3c) = sm;
    Fl(fb, 0x40) = sm;
    const uint32_t snd = D(g, 0x2c);
    if (B(g, 0xdd0) != 0)
    {
        D(g, 0x2718) += 3;
        D(fb, 8) = 4;
        VCall<uint32_t>(P(snd), 0xd0 / 4, 0x0058e92cu, 0u, 0u, 0u, 0xffffffffu, 0u);
    }
    else
    {
        D(g, 0x2718) += 1;
        D(fb, 8) = 2;
        VCall<uint32_t>(P(snd), 0xd0 / 4, 0x0058e910u, 0u, 0u, 0u, 0xffffffffu, 0u);
    }
    const uint32_t n = D(fb, 8);
    uint32_t proj = D(fb, 0);  // the original's uninitialised slot when no rocket is fired (n is 2 or 4)
    for (uint32_t i = 0; i < n; ++i, cand += 0x2c)
    {
        const float yaw = Fl(fb, 0x24 + i * 4);
        const float sy = X87Sin(yaw);
        std::memset(P(fb + 0x50), 0, 0x44);  // RotY at F+0x50
        D(fb, 0x90) = 1;
        D(fb, 0x64) = 0x3f800000;
        D(fb, 0x8c) = 0x3f800000;
        Fl(fb, 0) = sy;
        const float cy = X87Cos(yaw);
        Fl(fb, 0x58) = sy;
        std::memset(P(fb + 0x98), 0, 0x44);  // RotX at F+0x98
        D(fb, 0xd8) = 1;
        D(fb, 0x98) = 0x3f800000;
        D(fb, 0xd4) = 0x3f800000;
        Fl(fb, 0x50) = cy;
        Fl(fb, 0x70) = -Fl(fb, 0);
        Fl(fb, 0x78) = cy;
        Fl(fb, 0) = Fl(fb, 0x34 + i * 4);
        const float sp = X87Sin(Fl(fb, 0));
        const float cp = X87Cos(Fl(fb, 0));
        Fl(fb, 0xac) = cp;
        Fl(fb, 0xb0) = -sp;
        Fl(fb, 0xbc) = sp;
        Fl(fb, 0xc0) = cp;
        orig::fn_004e2910(fb + 0x98, fb + 0x1b8, fb + 0x50);
        orig::fn_004e2910(fb + 0x1b8, fb + 0x128, fb + 0x170);
        const uint32_t owner = D(D(fb, 4), 0x54c);
        Fl(fb, 0xc) = -Fl(fb, 0x130);
        Fl(fb, 0x10) = -Fl(fb, 0x140);
        Fl(fb, 0x14) = -Fl(fb, 0x150);
        proj = orig::fn_0047e050(fb + 0x18, D(G(), 0xaa8), owner, D(fb, 4) + 0x460, fb + 0xc, 0);
        if (proj != 0)
        {
            const uint32_t t = D(cand, 0);
            if (t != 0)
            {
                if (B(cand, 0x20) != 0)
                    VCall<uint32_t>(P(proj), 0x14c / 4, t, 0x005b68fcu);
                else
                    VCall<uint32_t>(P(proj), 0x150 / 4, cand + 4);
            }
            else
            {
                VCall<uint32_t>(P(proj), 0x150 / 4, fb + 0x44);
            }
        }
    }
    VCall<uint32_t>(P(w), 0x154 / 4);
    if (proj != 0)
        VCall<uint32_t>(P(w), 0x144 / 4);
    return proj != 0 ? 1u : 0u;
}
static __declspec(naked) void fn_004a2550_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push ecx
        call fn_004a2550
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
HOOK(0x004a2550, "fn_004a2550", fn_004a2550_Entry, nullptr);

// 0x004a2910 (ecx = weapon). PlayerWeapon (vtable 0x005a4858) slot 0x4e (+0x138): ready only with a current target
// (Game+0xa6c [0], via the bare-`ret` 0x00407ff0) whose vt+0xd8() is true; then a tail jump to 0x004a23b0. Not
// executed in recorded runs.
uint32_t fn_004a2910(uint32_t w)
{
    const uint32_t list = D(G(), 0xa6c);
    const uint32_t c = D(list, 0);
    if (c == 0)
        return list & 0xffffff00u;
    const uint32_t r = VCall<uint32_t>(P(c), 0xd8 / 4);
    if (!static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    return fn_004a23b0(w);
}
static uint32_t fn_004a2910_Abi(uint32_t ecx)
{
    return fn_004a2910(ecx);
}
ABI_ENTRY_004a2910(fn_004a2910_Entry, fn_004a2910_Abi)
HOOK(0x004a2910, "fn_004a2910", fn_004a2910_Entry, nullptr);

// 0x004a2950 (ecx = weapon). PlayerWeapon (vtable 0x005a4858) slot 0x50 (+0x140), fire: with an owner,
// 0x0047dbf0(eax = &+0x4a0, ecx = &+0x460, edx = weapon; Game+0xaa8, owner, 1), the muzzle effect vt+0x154(), the
// sound "wpn_shark_fire" (0x0058e900) and on success vt+0x144(); al = success. Not executed in recorded runs.
uint32_t fn_004a2950(uint32_t w)
{
    const uint32_t owner = D(w, 0x54c);
    if (owner == 0)
        return 0;
    const uint32_t r = orig::fn_0047dbf0(w + 0x4a0, w + 0x460, w, D(G(), 0xaa8), owner, 1);
    VCall<uint32_t>(P(w), 0x154 / 4);
    PlaySound2d(0x0058e900);
    if (r != 0)
        VCall<uint32_t>(P(w), 0x144 / 4);
    return r != 0 ? 1u : 0u;
}
static uint32_t fn_004a2950_Abi(uint32_t ecx)
{
    return fn_004a2950(ecx);
}
ABI_ENTRY_004a2950(fn_004a2950_Entry, fn_004a2950_Abi)
HOOK(0x004a2950, "fn_004a2950", fn_004a2950_Entry, nullptr);

// 0x004a2a40 (no register input). PlayerWeapon slot 0x5b (+0x16c) (vtables 0x005a1348, 0x005ad458): al = 0 when
// 0x0040fba0(ecx = Game+0xa68) is true or Game+0x550 > 1, else byte Game+0x4c2 (both read from the Game object, not
// the weapon). Not executed in recorded runs.
uint32_t fn_004a2a40()
{
    const uint32_t g = G();
    const uint32_t r = orig::fn_0040fba0(D(g, 0xa68));
    if (static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    if (D(g, 0x550) > 1)
        return r & 0xffffff00u;
    return (r & 0xffffff00u) | B(g, 0x4c2);
}
static uint32_t fn_004a2a40_Abi()
{
    return fn_004a2a40();
}
ABI_ENTRY_004a2a40(fn_004a2a40_Entry, fn_004a2a40_Abi)
HOOK(0x004a2a40, "fn_004a2a40", fn_004a2a40_Entry, nullptr);

// 0x004a2a70 (cdecl: weapon, bone, owner, projectile name). Fires along the camera ray: camera position and
// direction (Camera_GetPosition 0x0040f9e0 / Camera_GetDirection 0x0040fa10 on Game+0xa68); with the emitter matrix
// (0x00453c50) the start is moved along the ray to the muzzle's depth (t = dir.muzzle - dir.pos), else by
// [0x005bcbdc]; target = start + dir * Game+0x2a08; 0x0047de40(eax = &start, ebx = &target; Game+0xaa8, owner, name,
// 0). Returns 1 when a projectile was made. Called by 0x004a2480. Not executed in recorded runs.
uint32_t fn_004a2a70(uint32_t w, uint32_t bone, uint32_t owner, uint32_t proj)
{
    float F[0x68 / 4];  // pos +0, dir +0xc, target +0x18, matrix +0x24
    const uint32_t fb = u(F);
    const uint32_t g = G();
    orig::fn_0040fa10(fb + 0xc, D(g, 0xa68));
    orig::fn_0040f9e0(fb, D(g, 0xa68));
    const uint32_t ok = orig::fn_00453c50(w, bone, fb + 0x24, 1, 0, 0, 1, 0, 0);
    float t;
    if (static_cast<uint8_t>(ok))
    {
        const float m = Fl(fb, 0x14) * Fl(fb, 0x50) + Fl(fb, 0x10) * Fl(fb, 0x40) + Fl(fb, 0xc) * Fl(fb, 0x30);
        const float p = Fl(fb, 8) * Fl(fb, 0x14) + Fl(fb, 4) * Fl(fb, 0x10) + Fl(fb, 0) * Fl(fb, 0xc);
        t = m - p;
    }
    else
    {
        t = K(0x005bcbdc);
    }
    const uint32_t g2 = G();
    Fl(fb, 0) = Fl(fb, 0xc) * t + Fl(fb, 0);
    Fl(fb, 4) = Fl(fb, 0x10) * t + Fl(fb, 4);
    Fl(fb, 8) = Fl(fb, 0x14) * t + Fl(fb, 8);
    Fl(fb, 0x18) = Fl(fb, 0xc) * Fl(g2, 0x2a08) + Fl(fb, 0);
    Fl(fb, 0x1c) = Fl(fb, 0x10) * Fl(g2, 0x2a08) + Fl(fb, 4);
    Fl(fb, 0x20) = Fl(fb, 0x14) * Fl(g2, 0x2a08) + Fl(fb, 8);
    const uint32_t r = orig::fn_0047de40(fb, fb + 0x18, D(g2, 0xaa8), owner, proj, 0);
    return r != 0 ? 1u : 0u;
}
static uint32_t fn_004a2a70_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004a2a70(a0, a1, a2, a3);
}
ABI_ENTRY_004a2a70(fn_004a2a70_Entry, fn_004a2a70_Abi)
HOOK(0x004a2a70, "fn_004a2a70", fn_004a2a70_Entry, nullptr);
