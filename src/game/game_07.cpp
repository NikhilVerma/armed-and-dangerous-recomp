// Recovered (static, batch game_07): projectile and object virtual methods (SharkGun, Vindaloo, RocketTower,
// ForgeCannon, RocketBud, StickyBomb, BlackHole, Arrow, Grenade, CaptainRocket, MonkBasket, Penguin/Sheep,
// Timer/ArmyTimer ...) and a few object-system helpers. Compile-checked only (the game was not run); the oracle
// (tools/compare.py) is still to do. Functions the recorded runs never execute are marked "not executed in recorded
// runs". Class names come from slot 1 of the vtable that holds the function ("CLASS(Name)" at descriptor +0x14).
//
// Conventions shared with game_01..04 / ai_06:
//   - VC / VB / VF: virtual call through the object's vtable at a byte offset (thiscall, callee pops).
//   - 0x00457870 (Object_SetPosition) reads only al, ecx and edx (symbols/abi.csv lists three stack arguments that
//     no caller pushes); the upper eax bytes the original leaves are passed when known, otherwise 0.
//   - Callees that return st0 while the ABI table says eax or none are called through inline-asm shims that pop
//     st0 (Call_00471970).
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
// Memory at an absolute address (object fields: D(self + 0x460)).
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
inline uint16_t& W(uint32_t a)
{
    return *reinterpret_cast<uint16_t*>(a);
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
// A float constant of the game's .rdata (0x0056c05c = 0.0).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
const uint32_t kZero = 0x0056c05c;       // 0.0f
const uint32_t kFrameTime = 0x00604a90;  // the frame time (seconds)
inline float Dt()
{
    return GameGlobal<float>(kFrameTime);
}
inline uint32_t GameP()
{
    return D(0x005fe490);
}
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
// The sound system (game+0x2c) virtual +0xc4 (name, object, a2, a3, a4, a5): starts a sound, returns its handle.
inline uint32_t PlaySound(uint32_t name, uint32_t object, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return VC(D(GameP() + 0x2c), 0xc4, name, object, a2, a3, a4, a5);
}
// The reference drop the game inlines everywhere: --object+0x28; at zero, unless virtual +0xd8 says the object is
// kept, 0x00487da0 (eax = object) releases it.
inline void Unref(uint32_t object)
{
    if (--I(object + 0x28) == 0 && !VB(object, 0xd8))
        orig::fn_00487da0(object);
}
// 0x005391cd (_rand).
inline uint32_t Rand()
{
    return static_cast<uint32_t>(orig::fn_005391cd());
}
// x87 transcendental instructions at the FPU's current (single) precision.
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
// fpatan: atan2(y, x).
float X87Atan2(float y, float x)
{
    float r;
    __asm {
        fld y
        fld x
        fpatan
        fstp r
    }
    return r;
}
float X87Sqrt(float a)
{
    float r;
    __asm {
        fld a
        fsqrt
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
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 0x0047fc90 (ecx = self). SharkGunProjectile (vtable 0x005a5d28) slot 0x2f, the update (the "land shark": a fin
// that swims under the ground towards a target and bites). Strings "wpn_armed_jaws_loop", "landshark_fin",
// "landshark", "wpn_shark_dive". Not executed in recorded runs.
//   - When the bite object +0x5a4 exists and its vt+0xd8() is true: nothing. Otherwise, when it exists: it is
//     released, +0x58c = -10.0, 0x004577b0(self; 0), the fin +0x5a0 vt+0xf4(1), velocity +0x57c = 0 (0x00458510);
//     while bites +0x59c < +0x598 the loop sound starts (+0x5b0), else +0x464 = 0.
//   - The target +0x544 is dropped when it is dead (Object_IsDead 0x00454780). With +0x568 == 0, 0x0047e4d0.
//   - 0x00453150 (ecx = self, esi = +0x52c), 0x00458000 (velocity into +0x57c); old position +0x508 = +0x514, then
//     vt+0x54(+0x514); +0x538 += the distance moved.
//   - Not yet diving (+0x588 == 0): below +0x51c < 0x005bcf50, or when the ray old -> new hits (0x004137e0), the
//     shark dives: +0x589 = below, +0x588 = 1, position = old + d * t, vt+0xf4(0), z -= 0x005bcb68, moved there,
//     the fin "landshark_fin" is created (ObjectSystem_CreateObject), referenced, placed and attached, +0x58c =
//     -10.0, linked into list (game+0xa84)+0xf1c, sound "wpn_shark_dive". Then when vt+0xd8(): 0x00455e70(0, 0).
//   - Diving: with no target and +0x464 > 0, 0x0047fa70(self). Target position +0x520 from the target's (or the
//     player's) vt+0x54. With a target: within 0x005bcb7c (2D) it bites ("landshark" created at the target and
//     released into +0x5a4, 0x0047f600, the loop sound stopped, self moved, physics back on, the target dropped);
//     else the speed f = (K2c - min(dist, +0x568) / +0x568) * K60 + K4c. Without a target the shark circles
//     (angle +0x5a8, radius +0x5ac) around the player, or picks a new heading when farther than 0x005bcf4c.
//     Steering 0x00471970 / 0x00471750 / 0x00455a30, then height handling through +0x58c and 0x00457870 or
//     Object_ForceGround 0x00455860; finally when vt+0xd8(): 0x00455e70(1, 0).
// Returns eax as the original leaves it on the early exits, 0 otherwise (the caller is the update loop).
uint32_t SharkGunProjectile_Update(uint32_t self)
{
    // Frame slots of the original (sub esp, 0x38): f10 scalar, f18/f1c speeds, f20 scalar, f24 vec3, f30 vec3,
    // f3c vec3.
    float f10 = 0.0f;
    uint32_t f18 = 0, f1c = 0;
    float f20 = 0.0f;
    float f24[3] = {0.0f, 0.0f, 0.0f};
    float f30[3] = {0.0f, 0.0f, 0.0f};
    float f3c[3] = {0.0f, 0.0f, 0.0f};
    const uint32_t biteSlot = self + 0x5a4;
    if (D(biteSlot))
    {
        const uint32_t r = VC(D(biteSlot), 0xd8);
        if (static_cast<uint8_t>(r))
            return r;
    }
    if (const uint32_t bite = D(biteSlot))
    {
        Unref(bite);
        D(biteSlot) = 0;
        D(self + 0x58c) = 0xc1200000;  // -10.0
        orig::fn_004577b0(self, 0);
        VC(D(self + 0x5a0), 0xf4, 1u);
        D(self + 0x584) = 0;
        D(self + 0x580) = 0;
        D(self + 0x57c) = 0;
        orig::fn_00458510(self + 0x57c, self);
        if (D(self + 0x59c) < D(self + 0x598))
            D(self + 0x5b0) = PlaySound(0x0059e408, self, 1, 0, 0xffffffff, 0);  // "wpn_armed_jaws_loop"
        else
            D(self + 0x464) = 0;
    }
    const uint32_t slot = self + 0x544;  // the target
    if (D(slot) && static_cast<uint8_t>(orig::fn_00454780(D(slot))))
    {
        Unref(D(slot));
        D(slot) = 0;
    }
    if (Fl(self + 0x568) == K(kZero))
        orig::fn_0047e4d0(self);
    orig::fn_00453150(self, self + 0x52c);
    orig::fn_00458000(self, self + 0x57c);
    const uint32_t pos = self + 0x514;
    const uint32_t old = self + 0x508;
    D(old) = D(pos);
    D(old + 4) = D(pos + 4);
    D(old + 8) = D(pos + 8);
    VC(self, 0x54, pos);
    f30[0] = Fl(pos) - Fl(old);
    f30[1] = Fl(pos + 4) - Fl(old + 4);
    f30[2] = Fl(pos + 8) - Fl(old + 8);
    f10 = 0.0f;
    Fl(self + 0x538) = X87Sqrt(f30[2] * f30[2] + f30[1] * f30[1] + f30[0] * f30[0]) + Fl(self + 0x538);
    if (!By(self + 0x588))
    {
        bool dive = Fl(self + 0x51c) < K(0x005bcf50);
        if (!dive)
            dive = static_cast<uint8_t>(orig::fn_004137e0(u(&f10), u(f30), D(GameP() + 0xa8c), old, 0x3f800000)) != 0;
        if (dive)
        {
            By(self + 0x589) = Fl(self + 0x51c) < K(0x005bcf50) ? 1 : 0;
            By(self + 0x588) = 1;
            Fl(pos) = f30[0] * f10 + Fl(old);
            Fl(self + 0x518) = f30[1] * f10 + Fl(self + 0x50c);
            Fl(self + 0x51c) = f30[2] * f10 + Fl(self + 0x510);
            const uint32_t r = VC(self, 0xf4, 0u);
            Fl(self + 0x51c) = Fl(self + 0x51c) - K(0x005bcb68);
            orig::fn_00457870(r & 0xffffff00u, pos, self, 0, 0, 0);
            D(self + 0x584) = 0;
            orig::fn_00458510(self + 0x57c, self);
            const uint32_t fin = orig::fn_0045f020(0, 0x005b0580, D(GameP() + 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa,
                                                   0);  // "landshark_fin"
            D(self + 0x5a0) = fin;
            ++I(fin + 0x28);
            orig::fn_00457870(fin & 0xffffff00u, pos, D(self + 0x5a0), 0, 0, 0);
            orig::fn_004539f0(0, self, D(self + 0x5a0), 0, 1, 0);
            D(self + 0x58c) = 0xc1200000;  // -10.0
            orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf1c), self);
            PlaySound(0x0059e3f8, self, 0, 0, 0xffffffff, 0);  // "wpn_shark_dive"
        }
        if (VB(self, 0xd8))
            orig::fn_00455e70(self, 0, 0);
        return 0;
    }
    if (D(slot) == 0 && Fl(self + 0x464) > K(kZero))
        orig::fn_0047fa70(self);
    const uint32_t tgt = self + 0x520;  // target position
    if (D(slot))
    {
        VC(D(slot), 0x54, tgt);
        const float dy = Fl(pos + 4) - Fl(tgt + 4);
        const float dx = Fl(pos) - Fl(tgt);
        f10 = X87Sqrt(dx * dx + dy * dy);
        if (f10 < K(0x005bcb7c))
        {
            // Bite: "landshark" at the target position.
            ++D(self + 0x59c);
            const uint32_t bite = orig::fn_0045f020(0, 0x005b0574, D(GameP() + 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa,
                                                    0);  // "landshark"
            D(self + 0x5a4) = bite;
            orig::fn_00457870(bite & 0xffffff00u, tgt, bite, 0, 0, 0);
            orig::fn_0047f600(D(self + 0x5a4), D(slot));
            ++I(D(self + 0x5a4) + 0x28);
            uint32_t eax = self + 0x5b0;
            if (D(self + 0x5b0))
                eax = VC(D(GameP() + 0x2c), 0xd8, self + 0x5b0, 0u);
            orig::fn_00457870(eax & 0xffffff00u, tgt, self, 0, 0, 0);
            VC(self, 0xf4, 0u);
            D(self + 0x584) = 0;
            D(self + 0x580) = 0;
            D(self + 0x57c) = 0;
            orig::fn_00458510(self + 0x57c, self);
            orig::fn_004577b0(self, 1);
            VC(D(self + 0x5a0), 0xf4, 0u);
            Unref(D(slot));  // no null test in the original
            D(slot) = 0;
            return 0;
        }
        if (f10 > Fl(self + 0x568))
            f10 = Fl(self + 0x568);
        f20 = (K(0x005bcb2c) - f10 / Fl(self + 0x568)) * K(0x005bcb60) + K(0x005bcb4c);
        f18 = D(self + 0x460);
        f1c = f18;
    }
    else
    {
        VC(D(GameP() + 0x748), 0x54, tgt);
        const float dy = Fl(pos + 4) - Fl(tgt + 4);
        const float dx = Fl(pos) - Fl(tgt);
        const float d2 = dx * dx + dy * dy;
        if (d2 > K(0x005bcf4c))
        {
            // Far from the player: head towards it.
            const float a = X87Atan2(Fl(self + 0x518) - Fl(self + 0x524), Fl(pos) - Fl(tgt)) + K(0x005bcb74);
            Fl(self + 0x5a8) = a;
            Fl(tgt) = X87Cos(a) * Fl(self + 0x5ac) + Fl(tgt);
            Fl(self + 0x524) = X87Sin(Fl(self + 0x5a8)) * Fl(self + 0x5ac) + Fl(self + 0x524);
            Fl(self + 0x590) = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bce30);
        }
        else
        {
            // Circle around the player, direction by +0x590.
            const float step = Dt() * K(0x005bce6c);
            if (Fl(self + 0x590) > K(0x005bcb2c))
                Fl(self + 0x5a8) = step + Fl(self + 0x5a8);
            else
                Fl(self + 0x5a8) = Fl(self + 0x5a8) - step;
            Fl(tgt) = X87Cos(Fl(self + 0x5a8)) * Fl(self + 0x5ac) + Fl(tgt);
            Fl(self + 0x524) = X87Sin(Fl(self + 0x5a8)) * Fl(self + 0x5ac) + Fl(self + 0x524);
        }
        f10 = 100000.0f;  // 0x47c35000
        f20 = 10.0f;
        f1c = 0x40a00000;  // 5.0
        f18 = D(self + 0x460);
    }
    f24[0] = Fl(pos);
    f24[1] = Fl(pos + 4);
    D(u(&f24[2])) = D(self + 0x528);
    Call_00471970(u(f24), u(f30), tgt, u(&f1c), f1c, f18, 0x3f800000, 0);
    f30[2] = 0.0f;
    D(self + 0x584) = 0;
    const float m1 = orig::fn_004581c0(self);
    const float m2 = orig::fn_004581c0(self) * f20;
    f20 = orig::fn_00471750(u(f30), self + 0x57c, u(f3c), 0, fb(m2), fb(m1));
    D(self + 0x39c) = orig::fn_00455a30(u(f3c), 0x10, self, D(self + 0x39c), fb(f20), 0xffffffff, 0);
    if (By(self + 0x589))
    {
        const uint32_t r = VC(self, 0x6c, u(f24));
        D(u(&f24[2])) = 0xc4480000;  // -800.0
        orig::fn_00457870(r & 0xffffff00u, u(f24), self, 0, 0, 0);
    }
    else
    {
        orig::fn_00455860(self, fb(K(0x005bcb68) - Fl(self + 0x58c)), 0x41a00000);  // 20.0
    }
    VC(self, 0x6c, u(f24));
    const float t = Fl(self + 0x464) - Dt();
    Fl(self + 0x464) = t;
    if (D(slot) == 0 && !(t > K(kZero)) && t == t)
    {
        // Out of time: sink and finish below 0x005bcf48.
        const float h = Fl(self + 0x58c) - Dt() * K(0x005bcde8);
        Fl(self + 0x58c) = h;
        f24[2] = h + f24[2];
        orig::fn_00457870(0, u(f24), self, 0, 0, 0);
        if (Fl(self + 0x58c) < K(0x005bcf48))
            VC(self, 0xd4, 0u);
    }
    else if (f10 > Fl(self + 0x594) || f10 != f10 || Fl(self + 0x594) != Fl(self + 0x594))
    {
        // Far: rise, capped to -3.0 above 0x005bcf44.
        const float h = Dt() * K(0x005bce84) + Fl(self + 0x58c);
        Fl(self + 0x58c) = h;
        if (h > K(0x005bcf44))
            D(self + 0x58c) = 0xc0400000;  // -3.0
        f24[2] = f24[2] + Fl(self + 0x58c);
        orig::fn_00457870(0, u(f24), self, 0, 0, 0);
    }
    else
    {
        // Close: sink, reset to -10.0 below 0x005bcf48.
        const float h = Fl(self + 0x58c) - Dt() * K(0x005bcbe8);
        Fl(self + 0x58c) = h;
        f24[2] = h + f24[2];
        orig::fn_00457870(0, u(f24), self, 0, 0, 0);
        if (Fl(self + 0x58c) < K(0x005bcf48))
            D(self + 0x58c) = 0xc1200000;  // -10.0
    }
    if (VB(self, 0xd8))
        orig::fn_00455e70(self, 1, 0);
    return 0;
}
static uint32_t SharkGunProjectile_Update_Abi(uint32_t ecx)
{
    return SharkGunProjectile_Update(ecx);
}
ABI_ENTRY_0047fc90(SharkGunProjectile_Update_Entry, SharkGunProjectile_Update_Abi)
HOOK(0x0047fc90, "SharkGunProjectile_Update", SharkGunProjectile_Update_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00480510 (ecx = self; stack: a0..a3; ret 0x10). VindalooProjectile (vtable 0x005afc80) slot 6, the property
// read: the base 0x0047e0c0(self; a0..a3), then Object_ReadValuePropDef 0x004534a0 (esi = a3, edi = field; self,
// name, a2, default, 0) for "start_speed" +0x58c (7.0), "max_speed" +0x588 (70.0), "homing_delay_duration" +0x590
// (0.7), "full_speed_distance" +0x594 (50.0); +0x594 below 1.0 becomes 1.0; the speeds are multiplied by 3.0
// (0x005bcb7c) and +0x460 = the start speed. Not executed in recorded runs. Returns 0 (the original leaves the
// FPU status word in ax; property readers' results are not used).
uint32_t VindalooProjectile_ReadProps(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_0047e0c0(self, a0, a1, a2, a3);
    orig::fn_004534a0(a3, self + 0x58c, self, 0x0059e3ec, a2, 0x40e00000, 0);  // "start_speed", 7.0
    orig::fn_004534a0(a3, self + 0x588, self, 0x0059e3e0, a2, 0x428c0000, 0);  // "max_speed", 70.0
    orig::fn_004534a0(a3, self + 0x590, self, 0x0059e3c8, a2, 0x3f333333, 0);  // "homing_delay_duration", 0.7
    orig::fn_004534a0(a3, self + 0x594, self, 0x0059e3b4, a2, 0x42480000, 0);  // "full_speed_distance", 50.0
    if (Fl(self + 0x594) < K(0x005bcb2c))
        D(self + 0x594) = 0x3f800000;
    const float start = Fl(self + 0x58c) * K(0x005bcb7c);
    Fl(self + 0x58c) = start;
    Fl(self + 0x588) = Fl(self + 0x588) * K(0x005bcb7c);
    Fl(self + 0x460) = start;
    return 0;
}
static uint32_t VindalooProjectile_ReadProps_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return VindalooProjectile_ReadProps(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00480510(VindalooProjectile_ReadProps_Entry, VindalooProjectile_ReadProps_Abi)
HOOK(0x00480510, "VindalooProjectile_ReadProps", VindalooProjectile_ReadProps_Entry, nullptr);

// 0x004805e0 (ecx = self). VindalooProjectile slot 0x4f (+0x13c): the base 0x0047eba0(self) | 0x200000 (guess: damage
// type flags). Plain ret: the caller's pushed argument (0x004805f0 pushes 0) is not popped. Not executed in
// recorded runs.
uint32_t VindalooProjectile_Flags(uint32_t self)
{
    return orig::fn_0047eba0(self) | 0x200000u;
}
static uint32_t VindalooProjectile_Flags_Abi(uint32_t ecx)
{
    return VindalooProjectile_Flags(ecx);
}
ABI_ENTRY_004805e0(VindalooProjectile_Flags_Entry, VindalooProjectile_Flags_Abi)
HOOK(0x004805e0, "VindalooProjectile_Flags", VindalooProjectile_Flags_Entry, nullptr);

// 0x004805f0 (ecx = self). VindalooProjectile slot 0x2f, the update (a homing missile). 0x0047e580(self); unless
// vt+0xd8() (alive), or (game+0xa84)+0xf00 vt+0x34() is non-zero (guess: paused), nothing. +0x590 (homing delay)
// -= frame time; while still positive only 0x00455e70(0, 0). Otherwise the target point T is the translation of
// the target +0x544's matrix (0x00453c50 with the bone +0x548 when its byte is set) or +0x520. dist = |T - pos|
// (+0x514). Past it (dist > +0x56c + 1.0) it explodes: with a target, a hit record (0x34 bytes: position, -1,
// direction = normalised (+0x52c.x, +0x52c.y, 0.7), damage +0x470) goes to 0x0047eec0 (eax = target, ebx = self);
// when +0x474 != 0, 0x0047f020 (ebx = +0x474; 0, pos, 0, +0x470, 0x0047e3a0(self), vt+0x13c(), 0) - vt+0x13c's
// pushed 0 outlives it as the seventh argument; then vt+0x158(0, 0), vt+0xd4(0), 0x00455e70(0, 0). Otherwise
// +0x56c = dist, the speed factor (1 - min(dist, +0x568) / +0x568) * 100 + 0.5, and the speed
// +0x58c + (+0x588 - +0x58c) * min(+0x568 - min(dist, +0x568), +0x594) / +0x594 steer the projectile (0x00471970,
// 0x00471750 with the mass 0x004581c0, 0x00455a30 into +0x39c), then 0x00455e70(0, 0). Not executed in recorded
// runs.
uint32_t VindalooProjectile_Update(uint32_t self)
{
    uint32_t fr[0x98 / 4];  // the original frame (sub esp, 0x98), addressed by its offsets
    const uint32_t F = u(fr);
    orig::fn_0047e580(self);
    uint32_t r = VC(self, 0xd8);
    if (!static_cast<uint8_t>(r))
        return r;
    Fl(self + 0x590) = Fl(self + 0x590) - Dt();
    r = VC(D(D(GameP() + 0xa84) + 0xf00), 0x34);
    if (r)
        return r;
    if (Fl(self + 0x590) <= K(kZero))
    {
        if (const uint32_t target = D(self + 0x544))
        {
            const uint32_t bone = By(self + 0x548) ? self + 0x548 : 0;
            orig::fn_00453c50(target, bone, F + 0x54, 1, 0, 0, 1, 0, 0);
            D(F + 0) = D(F + 0x60);
            D(F + 4) = D(F + 0x70);
            D(F + 8) = D(F + 0x80);
        }
        else
        {
            D(F + 0) = D(self + 0x520);
            D(F + 4) = D(self + 0x524);
            D(F + 8) = D(self + 0x528);
        }
        const uint32_t pos = self + 0x514;
        const float dx = Fl(F + 0) - Fl(pos);
        const float dy = Fl(F + 4) - Fl(pos + 4);
        const float dz = Fl(F + 8) - Fl(pos + 8);
        float dist = X87Sqrt(dz * dz + dy * dy + dx * dx);
        if (dist > Fl(self + 0x56c) + K(0x005bcb2c))
        {
            if (const uint32_t target = D(self + 0x544))
            {
                std::memset(&fr[0x14 / 4], 0, 0x34);
                D(F + 0x18) = D(pos);
                D(F + 0x1c) = D(pos + 4);
                D(F + 0x20) = D(pos + 8);
                D(F + 0x2c) = D(self + 0x52c);
                D(F + 0x34) = D(self + 0x534);
                D(F + 0x30) = D(self + 0x530);
                D(F + 0x24) = 0xffffffff;
                D(F + 0x38) = D(self + 0x470);
                D(F + 0x34) = 0x3f333333;  // 0.7 replaces the copied z
                Normalize3(F + 0x2c, F + 0x2c);
                orig::fn_0047eec0(target, self, F + 0x14);
            }
            if (!(Fl(self + 0x474) == K(kZero)))
            {
                const uint32_t flags = VC(self, 0x13c);  // plain ret: its pushed 0 stays as the 7th argument below
                const uint32_t kind = orig::fn_0047e3a0(self);
                orig::fn_0047f020(self + 0x474, 0, pos, 0, D(self + 0x470), kind, flags, 0);
            }
            VC(self, 0x158, 0u, 0u);
            VC(self, 0xd4, 0u);
            return orig::fn_00455e70(self, 0, 0);
        }
        Fl(self + 0x56c) = dist;
        if (dist > Fl(self + 0x568))
            dist = Fl(self + 0x568);
        Fl(F + 0x0c) = (K(0x005bcb2c) - dist / Fl(self + 0x568)) * K(0x005bcb60) + K(0x005bcb4c);
        float e = Fl(self + 0x568) - dist;
        if (e > Fl(self + 0x594))
            e = Fl(self + 0x594);
        Fl(F + 0x10) = (Fl(self + 0x588) - Fl(self + 0x58c)) * (e / Fl(self + 0x594)) + Fl(self + 0x58c);
        Call_00471970(pos, F + 0x48, F + 0, F + 0x10, D(F + 0x10), D(F + 0x10), 0x3f800000, 0);
        const float m1 = orig::fn_004581c0(self);
        const float m2 = orig::fn_004581c0(self) * Fl(F + 0x0c);
        Fl(F + 0x0c) = orig::fn_00471750(F + 0x48, self + 0x57c, F + 0, 0, fb(m2), fb(m1));
        D(self + 0x39c) = orig::fn_00455a30(F + 0, 0x10, self, D(self + 0x39c), D(F + 0x0c), 0xffffffff, 0);
    }
    return orig::fn_00455e70(self, 0, 0);
}
static uint32_t VindalooProjectile_Update_Abi(uint32_t ecx)
{
    return VindalooProjectile_Update(ecx);
}
ABI_ENTRY_004805f0(VindalooProjectile_Update_Entry, VindalooProjectile_Update_Abi)
HOOK(0x004805f0, "VindalooProjectile_Update", VindalooProjectile_Update_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00480920 (ecx = self; stack: a0..a3; ret 0x10). RocketTowerProjectile (vtable 0x005aba18) slot 6, the property
// read: as VindalooProjectile_ReadProps plus "stop_homing_proximity" +0x598 (200.0); +0x59c = 1.0, +0x5a0 = 0. Not
// executed in recorded runs. Returns 0 (the original leaves the FPU status word in ax).
uint32_t RocketTowerProjectile_ReadProps(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_0047e0c0(self, a0, a1, a2, a3);
    orig::fn_004534a0(a3, self + 0x58c, self, 0x0059e3ec, a2, 0x40e00000, 0);  // "start_speed", 7.0
    orig::fn_004534a0(a3, self + 0x588, self, 0x0059e3e0, a2, 0x428c0000, 0);  // "max_speed", 70.0
    orig::fn_004534a0(a3, self + 0x590, self, 0x0059e3c8, a2, 0x3f333333, 0);  // "homing_delay_duration", 0.7
    orig::fn_004534a0(a3, self + 0x594, self, 0x0059e3b4, a2, 0x42480000, 0);  // "full_speed_distance", 50.0
    orig::fn_004534a0(a3, self + 0x598, self, 0x0059e39c, a2, 0x43480000, 0);  // "stop_homing_proximity", 200.0
    if (Fl(self + 0x594) < K(0x005bcb2c))
        D(self + 0x594) = 0x3f800000;
    const float start = Fl(self + 0x58c) * K(0x005bcb7c);
    D(self + 0x59c) = 0x3f800000;
    Fl(self + 0x58c) = start;
    D(self + 0x5a0) = 0;
    Fl(self + 0x588) = Fl(self + 0x588) * K(0x005bcb7c);
    Fl(self + 0x460) = start;
    return 0;
}
static uint32_t RocketTowerProjectile_ReadProps_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return RocketTowerProjectile_ReadProps(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00480920(RocketTowerProjectile_ReadProps_Entry, RocketTowerProjectile_ReadProps_Abi)
HOOK(0x00480920, "RocketTowerProjectile_ReadProps", RocketTowerProjectile_ReadProps_Entry, nullptr);

// 0x00480a20 (ecx = self). RocketTowerProjectile slot 0x2f, the update (a homing rocket). 0x0047e580(self); unless
// vt+0xd8() or (game+0xa84)+0xf00 vt+0x34() is non-zero, nothing. +0x590 -= frame time. Once (+0x5a0 == 0), within
// sqrt(2500) of the player (+0xc0c) a pass-by sound plays (sound vt+0xd0, "wpn_rocket_passby_med_1" when
// (rand() * 100) & ~0x7fff < 0x190000, else "_2"; handle into +0x5a0). After the homing delay, with a target +0x544:
// direction d = normalised(translation of its matrix (0x00453c50) - pos), heading h (0x00453150); when h.d > 0.7
// and dist < +0x598 the target is dropped ("stop homing") and 0x00455e70(0, 0) returns. Otherwise +0x56c = dist,
// +0x59c += frame time * 4.0 (the mass factor), speed +0x58c + (+0x588 - +0x58c) * min(+0x568 - min(dist, +0x568),
// +0x594) / +0x594 steers it (0x00471970, 0x00471750, 0x00455a30 into +0x39c). Always 0x00455e70(0, 0) at the end.
// Not executed in recorded runs.
uint32_t RocketTowerProjectile_Update(uint32_t self)
{
    uint32_t fr[0x6c / 4];  // the original frame (sub esp, 0x6c)
    const uint32_t F = u(fr);
    orig::fn_0047e580(self);
    uint32_t r = VC(self, 0xd8);
    if (!static_cast<uint8_t>(r))
        return r;
    Fl(self + 0x590) = Fl(self + 0x590) - Dt();
    r = VC(D(D(GameP() + 0xa84) + 0xf00), 0x34);
    if (r)
        return r;
    if (D(self + 0x5a0) == 0)
    {
        const uint32_t pl = D(GameP() + 0x748);
        const float dz = Fl(self + 0x51c) - Fl(pl + 0xc14);
        const float dy = Fl(self + 0x518) - Fl(pl + 0xc10);
        const float dx = Fl(self + 0x514) - Fl(pl + 0xc0c);
        if (dx * dx + dy * dy + dz * dz < K(0x005bccb8))
        {
            const int32_t v = static_cast<int32_t>((Rand() * 100u) & 0xffff8000u);
            const uint32_t name = v < 0x190000 ? 0x0059e384u : 0x0059e36cu;  // "wpn_rocket_passby_med_1" / "_2"
            D(self + 0x5a0) = VC(D(GameP() + 0x2c), 0xd0, name, self, 0u, 0u, 0xffffffffu, 0u);
        }
    }
    if (Fl(self + 0x590) <= K(kZero))
    {
        const uint32_t slot = self + 0x544;
        if (const uint32_t target = D(slot))
        {
            const uint32_t bone = By(self + 0x548) ? self + 0x548 : 0;
            orig::fn_00453c50(target, bone, F + 0x28, 1, 0, 0, 1, 0, 0);
            const uint32_t pos = self + 0x514;
            Fl(F + 0x04) = Fl(F + 0x34) - Fl(pos);
            D(F + 0x20) = D(F + 0x44);
            D(F + 0x24) = D(F + 0x54);
            Fl(F + 0x08) = Fl(F + 0x44) - Fl(pos + 4);
            D(F + 0x1c) = D(F + 0x34);
            Fl(F + 0x0c) = Fl(F + 0x54) - Fl(pos + 8);
            Fl(F + 0x00) = Normalize3(F + 0x04, F + 0x04);
            orig::fn_00453150(self, F + 0x10);
            const float dot = Fl(F + 0x18) * Fl(F + 0x0c) + Fl(F + 0x14) * Fl(F + 0x08) + Fl(F + 0x10) * Fl(F + 0x04);
            if (dot > K(0x005bcd48) && Fl(F + 0x00) < Fl(self + 0x598))
            {
                Unref(D(slot));
                D(slot) = 0;
                return orig::fn_00455e70(self, 0, 0);
            }
            D(self + 0x56c) = D(F + 0x00);
            if (Fl(F + 0x00) > Fl(self + 0x568))
                D(F + 0x00) = D(self + 0x568);
            Fl(self + 0x59c) = Dt() * K(0x005bcbe0) + Fl(self + 0x59c);
            float e = Fl(self + 0x568) - Fl(F + 0x00);
            if (e > Fl(self + 0x594))
                e = Fl(self + 0x594);
            Fl(F + 0x00) = (Fl(self + 0x588) - Fl(self + 0x58c)) * (e / Fl(self + 0x594)) + Fl(self + 0x58c);
            Call_00471970(pos, F + 0x10, F + 0x1c, F + 0x00, D(F + 0x00), D(F + 0x00), 0x3f800000, 0);
            const float m1 = orig::fn_004581c0(self);
            const float m2 = orig::fn_004581c0(self) * Fl(self + 0x59c);
            Fl(F + 0x00) = orig::fn_00471750(F + 0x10, self + 0x57c, F + 0x1c, 0, fb(m2), fb(m1));
            D(self + 0x39c) = orig::fn_00455a30(F + 0x1c, 0x10, self, D(self + 0x39c), D(F + 0x00), 0xffffffff, 0);
        }
    }
    return orig::fn_00455e70(self, 0, 0);
}
static uint32_t RocketTowerProjectile_Update_Abi(uint32_t ecx)
{
    return RocketTowerProjectile_Update(ecx);
}
ABI_ENTRY_00480a20(RocketTowerProjectile_Update_Entry, RocketTowerProjectile_Update_Abi)
HOOK(0x00480a20, "RocketTowerProjectile_Update", RocketTowerProjectile_Update_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00480d60 (ecx = self; stack: a0..a3; ret 0x10). Slot 6 (property read) of ForgeCannonProjectile (vtable
// 0x005a33b8) and RocketBudProjectile (vtable 0x005a7958): the base 0x0047e0c0(self; a0..a3), then ObjectList_Unlink
// 0x00454750 (esi = list (game+0xa84)+0xf1c, edi = self) and +0x588 = 0 (the pass-by sound handle). Returns the
// unlink's eax. Not executed in recorded runs.
uint32_t RocketBudProjectile_ReadProps(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_0047e0c0(self, a0, a1, a2, a3);
    const uint32_t r = orig::fn_00454750(D(D(GameP() + 0xa84) + 0xf1c), self);
    D(self + 0x588) = 0;
    return r;
}
static uint32_t RocketBudProjectile_ReadProps_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return RocketBudProjectile_ReadProps(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00480d60(RocketBudProjectile_ReadProps_Entry, RocketBudProjectile_ReadProps_Abi)
HOOK(0x00480d60, "RocketBudProjectile_ReadProps", RocketBudProjectile_ReadProps_Entry, nullptr);

// 0x00480db0 (ecx = self; stack: matrix, a1; ret 8). RocketBudProjectile slot 0x50 (+0x140), the launch: vt+0x148(a1),
// Object_SetTransform 0x00457990 (eax = that result with al = 0; self, matrix), vt+0x54(+0x514) (position), then
// 0x00471a00 (ebx = velocity +0x57c, esi = target +0x520, edi = +0x514; speed +0x460, -64.348 (gravity, guess), 1)
// computes the launch velocity, Body_setLinearVelocity 0x00458510, and vt+0x154(). Returns vt+0x154's eax. Not
// executed in recorded runs.
uint32_t RocketBudProjectile_Launch(uint32_t self, uint32_t matrix, uint32_t a1)
{
    const uint32_t r = VC(self, 0x148, a1);
    orig::fn_00457990(r & 0xffffff00u, self, matrix);
    VC(self, 0x54, self + 0x514);
    orig::fn_00471a00(self + 0x57c, self + 0x520, self + 0x514, D(self + 0x460), 0xc280b23a, 1);
    orig::fn_00458510(self + 0x57c, self);
    return VC(self, 0x154);
}
static uint32_t RocketBudProjectile_Launch_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return RocketBudProjectile_Launch(ecx, a0, a1);
}
ABI_ENTRY_00480db0(RocketBudProjectile_Launch_Entry, RocketBudProjectile_Launch_Abi)
HOOK(0x00480db0, "RocketBudProjectile_Launch", RocketBudProjectile_Launch_Entry, nullptr);

// 0x00480e20 (ecx = self). RocketBudProjectile slot 0x2f, the update: 0x0047e580(self); when vt+0xd8(),
// 0x00455e70(0, 0). Then once (+0x588 == 0), within sqrt(4900) of the player (+0xc0c), the pass-by sound
// "wpn_rocket_passby_med_3" (sound vt+0xd0) into +0x588. Not executed in recorded runs.
uint32_t RocketBudProjectile_Update(uint32_t self)
{
    orig::fn_0047e580(self);
    if (VB(self, 0xd8))
        orig::fn_00455e70(self, 0, 0);
    uint32_t r = D(self + 0x588);
    if (r)
        return r;
    const uint32_t pl = D(GameP() + 0x748);
    const float dz = Fl(self + 0x51c) - Fl(pl + 0xc14);
    const float dy = Fl(self + 0x518) - Fl(pl + 0xc10);
    const float dx = Fl(self + 0x514) - Fl(pl + 0xc0c);
    if (dx * dx + dy * dy + dz * dz < K(0x005bcee8))
    {
        r = VC(D(GameP() + 0x2c), 0xd0, 0x0059e354u, self, 0u, 0u, 0xffffffffu, 0u);  // "wpn_rocket_passby_med_3"
        D(self + 0x588) = r;
    }
    return r;
}
static uint32_t RocketBudProjectile_Update_Abi(uint32_t ecx)
{
    return RocketBudProjectile_Update(ecx);
}
ABI_ENTRY_00480e20(RocketBudProjectile_Update_Entry, RocketBudProjectile_Update_Abi)
HOOK(0x00480e20, "RocketBudProjectile_Update", RocketBudProjectile_Update_Entry, nullptr);

// 0x00480ec0 (ecx = self; stack: matrix, a1; ret 8). Slot 0x50 (+0x140, the launch) of ForgeCannonProjectile
// (0x005a33b8), GurnerProjectile (0x005a4c38) and the Projectile vtable 0x005adc78: as RocketBudProjectile_Launch, but
// the speed is the 2D distance to the target +0x520 / +0x568 * +0x568 + 5.0. Executed in recorded runs.
uint32_t Projectile_Launch(uint32_t self, uint32_t matrix, uint32_t a1)
{
    const uint32_t r = VC(self, 0x148, a1);
    orig::fn_00457990(r & 0xffffff00u, self, matrix);
    const uint32_t pos = self + 0x514;
    VC(self, 0x54, pos);
    const float dy = Fl(self + 0x524) - Fl(pos + 4);
    const float dx = Fl(self + 0x520) - Fl(pos);
    const float speed = X87Sqrt(dx * dx + dy * dy) / Fl(self + 0x568) * Fl(self + 0x568) + K(0x005bcbdc);
    orig::fn_00471a00(self + 0x57c, self + 0x520, pos, fb(speed), 0xc280b23a, 1);
    orig::fn_00458510(self + 0x57c, self);
    return VC(self, 0x154);
}
static uint32_t Projectile_Launch_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Projectile_Launch(ecx, a0, a1);
}
ABI_ENTRY_00480ec0(Projectile_Launch_Entry, Projectile_Launch_Abi)
HOOK(0x00480ec0, "Projectile_Launch", Projectile_Launch_Entry, nullptr);

// 0x00480f60 (ecx = self). ForgeCannonProjectile slot 0x2f, the update: 0x0047e580(self); only when vt+0xd8():
// 0x00455e70(0, 0) and once (+0x588 == 0), within sqrt(4900) of the player, the pass-by sound
// "wpn_rocket_passby_med_2" into +0x588. Not executed in recorded runs.
uint32_t ForgeCannonProjectile_Update(uint32_t self)
{
    orig::fn_0047e580(self);
    uint32_t r = VC(self, 0xd8);
    if (!static_cast<uint8_t>(r))
        return r;
    orig::fn_00455e70(self, 0, 0);
    r = D(self + 0x588);
    if (r)
        return r;
    const uint32_t pl = D(GameP() + 0x748);
    const float dz = Fl(self + 0x51c) - Fl(pl + 0xc14);
    const float dy = Fl(self + 0x518) - Fl(pl + 0xc10);
    const float dx = Fl(self + 0x514) - Fl(pl + 0xc0c);
    if (dx * dx + dy * dy + dz * dz < K(0x005bcee8))
    {
        r = VC(D(GameP() + 0x2c), 0xd0, 0x0059e36cu, self, 0u, 0u, 0xffffffffu, 0u);  // "wpn_rocket_passby_med_2"
        D(self + 0x588) = r;
    }
    return r;
}
static uint32_t ForgeCannonProjectile_Update_Abi(uint32_t ecx)
{
    return ForgeCannonProjectile_Update(ecx);
}
ABI_ENTRY_00480f60(ForgeCannonProjectile_Update_Entry, ForgeCannonProjectile_Update_Abi)
HOOK(0x00480f60, "ForgeCannonProjectile_Update", ForgeCannonProjectile_Update_Entry, nullptr);

// Slot 0x4f (+0x13c) of projectile vtables: the base 0x0047eba0(self) with one flag bit added (guess: damage type).
// Plain ret. Not executed in recorded runs.
// 0x00481000: | 0x100000. ForgeCannonProjectile (vtable 0x005a33b8).
uint32_t ForgeCannonProjectile_Flags(uint32_t self)
{
    return orig::fn_0047eba0(self) | 0x100000u;
}
static uint32_t ForgeCannonProjectile_Flags_Abi(uint32_t ecx)
{
    return ForgeCannonProjectile_Flags(ecx);
}
ABI_ENTRY_00481000(ForgeCannonProjectile_Flags_Entry, ForgeCannonProjectile_Flags_Abi)
HOOK(0x00481000, "ForgeCannonProjectile_Flags", ForgeCannonProjectile_Flags_Entry, nullptr);
// 0x00481010: | 0x80000. Projectile vtable 0x005aec10 (slot 1 gives the base "Projectile" descriptor).
uint32_t fn_00481010(uint32_t self)
{
    return orig::fn_0047eba0(self) | 0x80000u;
}
static uint32_t fn_00481010_Abi(uint32_t ecx)
{
    return fn_00481010(ecx);
}
ABI_ENTRY_00481010(fn_00481010_Entry, fn_00481010_Abi)
HOOK(0x00481010, "fn_00481010", fn_00481010_Entry, nullptr);
// 0x00481020: | 0x100. Projectile vtable 0x005ab120 (slot 1 gives the base "Projectile" descriptor).
uint32_t fn_00481020(uint32_t self)
{
    return orig::fn_0047eba0(self) | 0x100u;
}
static uint32_t fn_00481020_Abi(uint32_t ecx)
{
    return fn_00481020(ecx);
}
ABI_ENTRY_00481020(fn_00481020_Entry, fn_00481020_Abi)
HOOK(0x00481020, "fn_00481020", fn_00481020_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00481030 (ecx = self; stack: vec3* target; ret 4). Slot 0x54 (+0x150) of the Projectile vtable 0x005ab120: sets
// the target point +0x520 = *target. Without a target object (+0x544 == 0) each component of *target first gets a
// random offset in [-v, v): v = game+0x483c * +0x568 * 0.01, offset = rand() * (v - -v) * (1/32768) + -v (written
// back into *target). Returns the x bits (eax as left). Not executed in recorded runs.
uint32_t fn_00481030(uint32_t self, uint32_t target)
{
    if (D(self + 0x544) == 0)
    {
        const float hi = Fl(GameP() + 0x483c) * Fl(self + 0x568) * K(0x005bcbec);
        const float lo = -hi;
        for (uint32_t i = 0; i < 3; ++i)
        {
            const float r = static_cast<float>(static_cast<int32_t>(Rand()));
            Fl(target + 4 * i) = r * (hi - lo) * K(0x005bcbc0) + lo + Fl(target + 4 * i);
        }
    }
    const uint32_t x = D(target);
    D(self + 0x520) = x;
    D(self + 0x524) = D(target + 4);
    D(self + 0x528) = D(target + 8);
    return x;
}
static uint32_t fn_00481030_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00481030(ecx, a0);
}
ABI_ENTRY_00481030(fn_00481030_Entry, fn_00481030_Abi)
HOOK(0x00481030, "fn_00481030", fn_00481030_Entry, nullptr);

// 0x00481110 (ecx = self; stack: a0..a3; ret 0x10). StickyBombProjectile (vtable 0x005a83e0) slot 6, the property
// read: the base 0x0047e0c0(self; a0..a3); "linger_lifetime" +0x588 (5.0); the strings "fx_stuck_object" +0x60c and
// "fx_stuck_ground" +0x62c (Object_ReadStringProp 0x004533a0, default ""); "launch_angle_degrees" (30.0) read into
// the original's own argument slot a3, then +0x658 = cos(angle * -pi/180), +0x65c = sin(...); +0x5d4 = +0x64c =
// +0x668 = 0, bytes +0x664 = +0x665 = 0. Returns 0. Not executed in recorded runs.
uint32_t StickyBombProjectile_ReadProps(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_0047e0c0(self, a0, a1, a2, a3);
    orig::fn_004534a0(a3, self + 0x588, self, 0x0059e344, a2, 0x40a00000, 0);  // "linger_lifetime", 5.0
    orig::fn_004533a0(0x0058a9a4, self + 0x60c, self, 0x0059e334, a2, a3, 0);  // "fx_stuck_object"
    orig::fn_004533a0(0x0058a9a4, self + 0x62c, self, 0x0059e324, a2, a3, 0);  // "fx_stuck_ground"
    uint32_t angleSlot = a3;
    orig::fn_004534a0(a3, u(&angleSlot), self, 0x0059e30c, a2, 0x41f00000, 0);  // "launch_angle_degrees", 30.0
    const float a = bf(angleSlot) * K(0x005bcd98);
    By(self + 0x665) = 0;
    const float c = X87Cos(a);
    D(self + 0x5d4) = 0;
    D(self + 0x64c) = 0;
    By(self + 0x664) = 0;
    D(self + 0x668) = 0;
    Fl(self + 0x658) = c;
    Fl(self + 0x65c) = X87Sin(a);
    return 0;
}
static uint32_t StickyBombProjectile_ReadProps_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return StickyBombProjectile_ReadProps(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00481110(StickyBombProjectile_ReadProps_Entry, StickyBombProjectile_ReadProps_Abi)
HOOK(0x00481110, "StickyBombProjectile_ReadProps", StickyBombProjectile_ReadProps_Entry, nullptr);

// 0x004811e0 (ecx = self; stack: object, a1 (unused); ret 8). StickyBombProjectile slot 0x53 (+0x14c): +0x544 =
// object, the name +0x548 = sprintf("ref_hit_0%d", (rand() * 7) >> 15) (a random hit reference 0..6, guess), and the
// object gains a reference (+0x28). Returns sprintf's count. Not executed in recorded runs.
uint32_t StickyBombProjectile_SetHitObject(uint32_t self, uint32_t object, uint32_t /*a1*/)
{
    D(self + 0x544) = object;
    const int32_t n = static_cast<int32_t>(Rand() * 7u) >> 15;
    const uint32_t r = orig::fn_0053814f(self + 0x548, 0x005b68e0, static_cast<uint32_t>(n));  // "ref_hit_0%d"
    if (const uint32_t o = D(self + 0x544))
        ++I(o + 0x28);
    return r;
}
static uint32_t StickyBombProjectile_SetHitObject_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return StickyBombProjectile_SetHitObject(ecx, a0, a1);
}
ABI_ENTRY_004811e0(StickyBombProjectile_SetHitObject_Entry, StickyBombProjectile_SetHitObject_Abi)
HOOK(0x004811e0, "StickyBombProjectile_SetHitObject", StickyBombProjectile_SetHitObject_Entry, nullptr);

// 0x00481220 (ecx = self; stack: owner; ret 4). StickyBombProjectile slot 0x52 (+0x148): +0x574 = 0, +0x504 = owner
// (referenced). A 0x18-byte object list is allocated from the heap 0x0063a210 and built by 0x004e7010 (eax =
// game+0xac4, edx = 0, esi = memory; no null test on failure, as the original) into +0x570; the owner, self, the
// player's two companions (0x00475d30 / 0x00475d40 on game+0x748, when set) and the player are added (list
// vt+0x18) and each gains a reference. Guess: the objects the bomb must not stick to. Returns the player. Not
// executed in recorded runs.
uint32_t StickyBombProjectile_SetOwner(uint32_t self, uint32_t owner)
{
    D(self + 0x574) = 0;
    D(self + 0x504) = owner;
    ++I(owner + 0x28);
    const uint32_t mem = orig::fn_004e07f0(0x0063a210, 0x10, 0x18, 2, 0x0058b5e8);
    const uint32_t list = mem ? orig::fn_004e7010(D(GameP() + 0xac4), 0, mem) : 0;
    D(self + 0x570) = list;
    VC(list, 0x18, D(self + 0x504));
    ++I(D(self + 0x504) + 0x28);
    VC(D(self + 0x570), 0x18, self);
    ++I(self + 0x28);
    uint32_t o = orig::fn_00475d30(D(GameP() + 0x748));
    if (o)
    {
        VC(D(self + 0x570), 0x18, o);
        ++I(o + 0x28);
    }
    o = orig::fn_00475d40(D(GameP() + 0x748));
    if (o)
    {
        VC(D(self + 0x570), 0x18, o);
        ++I(o + 0x28);
    }
    VC(D(self + 0x570), 0x18, D(GameP() + 0x748));
    const uint32_t pl = D(GameP() + 0x748);
    ++I(pl + 0x28);
    return pl;
}
static uint32_t StickyBombProjectile_SetOwner_Abi(uint32_t ecx, uint32_t a0)
{
    return StickyBombProjectile_SetOwner(ecx, a0);
}
ABI_ENTRY_00481220(StickyBombProjectile_SetOwner_Entry, StickyBombProjectile_SetOwner_Abi)
HOOK(0x00481220, "StickyBombProjectile_SetOwner", StickyBombProjectile_SetOwner_Entry, nullptr);

// 0x00481320 (ecx = self; stack: desc; ret 4). StickyBombProjectile slot 7 (+0x1c): Object_BuildRadiusCollision
// 0x004527d0(self; desc), 0x00457810 (eax = self), desc byte +0xf = 1, desc +0 = 0x100, then ObjectList_Link into
// the lists (game+0xa84)+0xe90 and +0xe94. Returns the last link's eax. Not executed in recorded runs.
uint32_t StickyBombProjectile_InitCollision(uint32_t self, uint32_t desc)
{
    orig::fn_004527d0(self, desc);
    orig::fn_00457810(self);
    By(desc + 0xf) = 1;
    D(desc) = 0x100;
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe90), self);
    return static_cast<uint32_t>(orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe94), self));
}
static uint32_t StickyBombProjectile_InitCollision_Abi(uint32_t ecx, uint32_t a0)
{
    return StickyBombProjectile_InitCollision(ecx, a0);
}
ABI_ENTRY_00481320(StickyBombProjectile_InitCollision_Entry, StickyBombProjectile_InitCollision_Abi)
HOOK(0x00481320, "StickyBombProjectile_InitCollision", StickyBombProjectile_InitCollision_Entry, nullptr);

// 0x00481380 (no register input; stack: out; ret 4). StickyBombProjectile slot 0x59 (+0x164): copies the vec3 at +4
// of 0x00408000(eax = game+0xa6c) into *out. Returns its z bits. Not executed in recorded runs.
uint32_t fn_00481380(uint32_t out)
{
    const uint32_t p = orig::fn_00408000(D(GameP() + 0xa6c)) + 4;
    D(out) = D(p);
    D(out + 4) = D(p + 4);
    const uint32_t z = D(p + 8);
    D(out + 8) = z;
    return z;
}
static uint32_t fn_00481380_Abi(uint32_t a0)
{
    return fn_00481380(a0);
}
ABI_ENTRY_00481380(fn_00481380_Entry, fn_00481380_Abi)
HOOK(0x00481380, "fn_00481380", fn_00481380_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
uint32_t StickyBombProjectile_Explode(uint32_t hit, uint32_t self, uint32_t point);

// 0x004813b0 (ecx = self; stack: matrix, a1; ret 8). StickyBombProjectile slot 0x50 (+0x140), the launch: vt+0x148(a1),
// Object_SetTransform 0x00457990 (self, matrix), vt+0x54(+0x514); +0x460 += 2D distance to the target +0x520 / +0x568
// * +0x568; 0x00471a00 (ebx = +0x57c, esi = +0x520, edi = +0x514; +0x460, -64.348, 1) with the target z lowered by
// 4.0 for the call; the velocity is then rotated in the x/y plane by the launch angle (+0x658 cos, +0x65c sin) and
// set (0x00458510). +0x668 = 0; unless the object's name (Object_GetName 0x00453270) contains "dynamite", the loop
// sound "wpn_sticky_grenade_lp" starts into +0x668. vt+0x154(); +0x660 = the clock 0x00604a98, also returned. Not
// executed in recorded runs.
uint32_t StickyBombProjectile_Launch(uint32_t self, uint32_t matrix, uint32_t a1)
{
    const uint32_t r = VC(self, 0x148, a1);
    orig::fn_00457990(r & 0xffffff00u, self, matrix);
    const uint32_t pos = self + 0x514;
    VC(self, 0x54, pos);
    const float dy = Fl(self + 0x524) - Fl(pos + 4);
    const float dx = Fl(self + 0x520) - Fl(pos);
    Fl(self + 0x460) = X87Sqrt(dx * dx + dy * dy) / Fl(self + 0x568) * Fl(self + 0x568) + Fl(self + 0x460);
    const uint32_t speed = D(self + 0x460);
    Fl(self + 0x528) = Fl(self + 0x528) - K(0x005bcbe0);
    orig::fn_00471a00(self + 0x57c, self + 0x520, pos, speed, 0xc280b23a, 1);
    Fl(self + 0x528) = Fl(self + 0x528) + K(0x005bcbe0);
    const uint32_t vz = D(self + 0x584);
    const uint32_t v = self + 0x57c;
    const float c = Fl(self + 0x658);
    const float s = Fl(self + 0x65c);
    const float nx = s * Fl(self + 0x580) + c * Fl(v);
    const float ny = c * Fl(self + 0x580) - s * Fl(v);
    Fl(v) = nx;
    Fl(v + 4) = ny;
    D(v + 8) = vz;
    orig::fn_00458510(v, self);
    D(self + 0x668) = 0;
    if (!orig::fn_005383d0(orig::fn_00453270(self), 0x0059e300))  // strstr(name, "dynamite")
        D(self + 0x668) = PlaySound(0x0059e2e8, self, 1, 0, 0xffffffff, 0);  // "wpn_sticky_grenade_lp"
    VC(self, 0x154);
    const uint32_t t = D(0x00604a98);
    D(self + 0x660) = t;
    return t;
}
static uint32_t StickyBombProjectile_Launch_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return StickyBombProjectile_Launch(ecx, a0, a1);
}
ABI_ENTRY_004813b0(StickyBombProjectile_Launch_Entry, StickyBombProjectile_Launch_Abi)
HOOK(0x004813b0, "StickyBombProjectile_Launch", StickyBombProjectile_Launch_Entry, nullptr);

// 0x00481520 (ecx = self). StickyBombProjectile slot 0x2f, the update. A fuse +0x64c > 0 counts down by the frame
// time and explodes the bomb (StickyBombProjectile_Explode 0x00481ab0 with no hit object) at or below 0. Stuck
// (byte +0x665): 0x0047e9b0 (esi = self; +0x590), the linger time +0x58c counts down and explodes it at or below 0.
// In flight: 0x0047e580(self); unless vt+0xd8() or (game+0xa84)+0xf00 vt+0x34() is non-zero, nothing. Target T =
// translation of the target's matrix (0x00453c50) or +0x520; dist = |T - pos|. When the distance flown +0x538 is
// not below the range +0x568 and there is no target: +0x568 = 0 and once (byte +0x664) the bomb leaves list
// (game+0xa84)+0xf1c. Otherwise it homes: speed factor (1 - min(dist, +0x568) / +0x568) * 5.0 + 2.0, 0x00471970 with
// +0x460, 0x00471750 with the mass, 0x00455a30 into +0x39c. Returns eax as left where cheap, else 0. Not executed in
// recorded runs.
uint32_t StickyBombProjectile_Update(uint32_t self)
{
    uint32_t fr[0x64 / 4];  // the original frame (sub esp, 0x64)
    const uint32_t F = u(fr);
    if (Fl(self + 0x64c) > K(kZero))
    {
        const float t = Fl(self + 0x64c) - Dt();
        Fl(self + 0x64c) = t;
        if (t <= K(kZero))
            return StickyBombProjectile_Explode(0, self, 0);
    }
    if (By(self + 0x665))
    {
        orig::fn_0047e9b0(self, self + 0x590);
        const float t = Fl(self + 0x58c) - Dt();
        Fl(self + 0x58c) = t;
        if (t <= K(kZero))
            return StickyBombProjectile_Explode(0, self, 0);
        return 0;
    }
    orig::fn_0047e580(self);
    uint32_t r = VC(self, 0xd8);
    if (!static_cast<uint8_t>(r))
        return r;
    r = VC(D(D(GameP() + 0xa84) + 0xf00), 0x34);
    if (r)
        return r;
    if (const uint32_t target = D(self + 0x544))
    {
        const uint32_t bone = By(self + 0x548) ? self + 0x548 : 0;
        orig::fn_00453c50(target, bone, F + 0x20, 1, 0, 0, 1, 0, 0);
        D(F + 0x08) = D(F + 0x2c);
        D(F + 0x0c) = D(F + 0x3c);
        D(F + 0x10) = D(F + 0x4c);
    }
    else
    {
        D(F + 0x08) = D(self + 0x520);
        D(F + 0x0c) = D(self + 0x524);
        D(F + 0x10) = D(self + 0x528);
    }
    const uint32_t pos = self + 0x514;
    const float dx = Fl(F + 0x08) - Fl(pos);
    const float dy = Fl(F + 0x0c) - Fl(pos + 4);
    const float dz = Fl(F + 0x10) - Fl(pos + 8);
    float dist = X87Sqrt(dz * dz + dy * dy + dx * dx);
    if (!(Fl(self + 0x538) < Fl(self + 0x568)) && D(self + 0x544) == 0)
    {
        D(self + 0x568) = 0;
        if (!By(self + 0x664))
            orig::fn_00454750(D(D(GameP() + 0xa84) + 0xf1c), self);
        By(self + 0x664) = 1;
        return 0;
    }
    if (dist > Fl(self + 0x568))
        dist = Fl(self + 0x568);
    Fl(F + 0x00) = (K(0x005bcb2c) - dist / Fl(self + 0x568)) * K(0x005bcbdc) + K(0x005bcb80);
    Call_00471970(pos, F + 0x14, F + 0x08, F + 0x04, D(self + 0x460), D(self + 0x460), 0x3f800000, 0);
    const float m1 = orig::fn_004581c0(self);
    const float m2 = orig::fn_004581c0(self) * Fl(F + 0x00);
    Fl(F + 0x00) = orig::fn_00471750(F + 0x14, self + 0x57c, F + 0x08, 0, fb(m2), fb(m1));
    D(self + 0x39c) = orig::fn_00455a30(F + 0x08, 0x10, self, D(self + 0x39c), D(F + 0x00), 0xffffffff, 0);
    return D(self + 0x39c);
}
static uint32_t StickyBombProjectile_Update_Abi(uint32_t ecx)
{
    return StickyBombProjectile_Update(ecx);
}
ABI_ENTRY_00481520(StickyBombProjectile_Update_Entry, StickyBombProjectile_Update_Abi)
HOOK(0x00481520, "StickyBombProjectile_Update", StickyBombProjectile_Update_Entry, nullptr);

// 0x00481790 (ecx = object; stack: value; ret 4; callers 0x0048f690, 0x0048f9c0): +0x58c = value. Returns value.
// Not executed in recorded runs.
uint32_t fn_00481790(uint32_t object, uint32_t value)
{
    D(object + 0x58c) = value;
    return value;
}
static uint32_t fn_00481790_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00481790(ecx, a0);
}
ABI_ENTRY_00481790(fn_00481790_Entry, fn_00481790_Abi)
HOOK(0x00481790, "fn_00481790", fn_00481790_Entry, nullptr);

// 0x004817a0 (eax = object; returns st0; caller 0x0043ee90): the float +0x58c. Not executed in recorded runs.
float fn_004817a0(uint32_t object)
{
    return Fl(object + 0x58c);
}
static float fn_004817a0_Abi(uint32_t eax)
{
    return fn_004817a0(eax);
}
ABI_ENTRY_004817a0(fn_004817a0_Entry, fn_004817a0_Abi)
HOOK(0x004817a0, "fn_004817a0", fn_004817a0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004817b0 (ecx = self; stack: hit object, hit record, a2 (unused); ret 0xc). StickyBombProjectile slot 0x4e
// (+0x138), the stick: the flight direction n = normalised(+0x514 - +0x508) (left as is when its length is 0). On the
// ground (no object) the bomb is placed at record+4 - n * 5.0, on an object at record+4 (0x00457870). The loop sound
// +0x668 is stopped (sound vt+0xd8(&+0x668, 1)). The first time (byte +0x665 clear): physics on (0x004577b0(1)),
// relinked into (game+0xa84)+0xf1c when byte +0x664, and 0x0040e270 (ecx = game+0xa68, edx = self). Then 0x0047e9f0
// (eax = record, ecx = object, ebx = self; +0x590), +0x58c = +0x588 (linger time), +0x665 = 1, +0x5d4 = object
// (referenced), the 0x34-byte record copied to +0x5d8. The effect "fx_stuck_object" +0x60c (on an object) or
// "fx_stuck_ground" +0x62c is created when named (Fx_Create 0x0041cc60, FxSystem game+0xa90, a descriptor {4, ...,
// +0x18 = 0, +0x1c = self}); the object's vt+0xcc(self, 0, 1); every object of list (game+0xa84)+0xe7c other than
// the hit one within sqrt(625) of the bomb gets vt+0xcc(0, object, 1). Returns al = 0 (upper bytes: the iterator's
// node). Not executed in recorded runs.
uint32_t StickyBombProjectile_Stick(uint32_t self, uint32_t object, uint32_t record, uint32_t /*a2*/)
{
    uint32_t fr[0xa4 / 4];  // the original frame (sub esp, 0xa4); the effect descriptor is partly uninitialised
    std::memset(fr, 0, sizeof(fr));
    const uint32_t F = u(fr);
    const uint32_t pos = self + 0x514;
    float nx = Fl(pos) - Fl(self + 0x508);
    Fl(F + 0x04) = Fl(pos + 4) - Fl(self + 0x50c);
    Fl(F + 0x08) = Fl(pos + 8) - Fl(self + 0x510);
    const float len = X87Sqrt(Fl(F + 0x08) * Fl(F + 0x08) + Fl(F + 0x04) * Fl(F + 0x04) + nx * nx);
    if (!(len == K(kZero)))
    {
        const float inv = K(0x005bcb2c) / len;
        nx = nx * inv;
        Fl(F + 0x04) = Fl(F + 0x04) * inv;
        Fl(F + 0x08) = inv * Fl(F + 0x08);
    }
    if (object == 0)
    {
        Fl(pos) = Fl(record + 4) - nx * K(0x005bcbdc);
        Fl(self + 0x518) = Fl(record + 8) - Fl(F + 0x04) * K(0x005bcbdc);
        Fl(self + 0x51c) = Fl(record + 0xc) - Fl(F + 0x08) * K(0x005bcbdc);
        orig::fn_00457870(0, pos, self, 0, 0, 0);
    }
    else
    {
        D(pos) = D(record + 4);
        D(pos + 4) = D(record + 8);
        D(pos + 8) = D(record + 0xc);
        orig::fn_00457870(D(record + 0xc) & 0xffffff00u, pos, self, 0, 0, 0);
    }
    if (D(self + 0x668))
        VC(D(GameP() + 0x2c), 0xd8, self + 0x668, 1u);
    if (!By(self + 0x665))
    {
        orig::fn_004577b0(self, 1);
        if (By(self + 0x664))
            orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf1c), self);
        orig::fn_0040e270(D(GameP() + 0xa68), self);
    }
    orig::fn_0047e9f0(record, object, self, self + 0x590);
    D(self + 0x58c) = D(self + 0x588);
    By(self + 0x665) = 1;
    D(self + 0x5d4) = object;
    if (object)
        ++I(object + 0x28);
    std::memcpy(P(self + 0x5d8), P(record), 0xd * 4);
    const uint32_t fxName = object ? self + 0x60c : self + 0x62c;  // "fx_stuck_object" / "fx_stuck_ground"
    if (By(fxName))
    {
        D(F + 0x3c) = 0;
        D(F + 0x40) = self;
        D(F + 0x24) = 4;
        orig::fn_0041cc60(fxName & 0xffffff00u, D(GameP() + 0xa90), fxName, F + 0x24);
    }
    if (object)
        VC(object, 0xcc, self, 0u, 1u);
    D(F + 0x00) = 0;
    D(F + 0x04) = 0;  // the list iterator {0, 0}
    VC(self, 0x54, F + 0x0c);
    for (uint32_t o = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xe7c), F + 0x00); o; o = orig::fn_004e73c0(F + 0x00))
    {
        if (o == object)
            continue;
        VC(o, 0x54, F + 0x18);
        const float dz = Fl(F + 0x14) - Fl(F + 0x20);
        const float dy = Fl(F + 0x10) - Fl(F + 0x1c);
        const float dx = Fl(F + 0x0c) - Fl(F + 0x18);
        if (dx * dx + dy * dy + dz * dz < K(0x005bced0))
            VC(o, 0xcc, 0u, object, 1u);
    }
    const uint32_t node = D(F + 0x04);
    if (node)
        --I(node + 0x18);
    return node & 0xffffff00u;
}
static uint32_t StickyBombProjectile_Stick_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return StickyBombProjectile_Stick(ecx, a0, a1, a2);
}
ABI_ENTRY_004817b0(StickyBombProjectile_Stick_Entry, StickyBombProjectile_Stick_Abi)
HOOK(0x004817b0, "StickyBombProjectile_Stick", StickyBombProjectile_Stick_Entry, nullptr);

// 0x00481ab0 (eax = hit object or 0, esi = self; stack: point record or 0). StickyBombProjectile's explosion (callers
// 0x0042e1c0, 0x0043ee90, 0x00481520, 0x00481b90). +0x454 (health) = 0; the object defaults to the stuck one +0x5d4,
// the record to +0x5d8; vt+0x54(record+4) (position). When +0x474 != 0, 0x0047f020 (ebx = +0x474; object, record+4,
// self, +0x470, 0x0047e3a0(self), vt+0x13c(), 0) - vt+0x13c (0x0047eba0, plain ret) leaves its pushed 0 as the
// seventh argument. With an object: 0x0047eec0 (eax = object, ebx = self; +0x5d8). vt+0x158(object, 0); the stuck
// object reference is dropped when it is the one used. It has no ret of its own: it ends with a tail jump to
// vt+0xd4 (0x00433bb0, ret 4) with its argument slot overwritten by 0, so the callee pops the one argument; recovered
// as a call of vt+0xd4(0) returning its eax, with a hand-written entry stub (ret 4). Not executed in recorded runs.
uint32_t StickyBombProjectile_Explode(uint32_t hit, uint32_t self, uint32_t point)
{
    D(self + 0x454) = 0;
    if (!hit)
        hit = D(self + 0x5d4);
    if (!point)
        point = self + 0x5d8;
    point += 4;
    VC(self, 0x54, point);
    if (!(Fl(self + 0x474) == K(kZero)))
    {
        const uint32_t flags = VC(self, 0x13c);  // plain ret: its pushed 0 stays as the 7th argument below
        const uint32_t kind = orig::fn_0047e3a0(self);
        orig::fn_0047f020(self + 0x474, hit, point, self, D(self + 0x470), kind, flags, 0);
    }
    if (hit)
        orig::fn_0047eec0(hit, self, self + 0x5d8);
    VC(self, 0x158, hit, 0u);
    if (hit && hit == D(self + 0x5d4))
    {
        Unref(hit);
        D(self + 0x5d4) = 0;
    }
    return VC(self, 0xd4, 0u);
}
static uint32_t StickyBombProjectile_Explode_Abi(uint32_t eax, uint32_t esi, uint32_t a0)
{
    return StickyBombProjectile_Explode(eax, esi, a0);
}
// symbols/abi.csv marks 0x00481ab0 "no ret found", so no generated stub: eax, esi, one stack argument, ret 4.
static __declspec(naked) void StickyBombProjectile_Explode_Entry()
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
        push eax
        call StickyBombProjectile_Explode_Abi
        add esp, 12
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret 4
    }
}
HOOK(0x00481ab0, "StickyBombProjectile_Explode", StickyBombProjectile_Explode_Entry, nullptr);

// 0x00481b90 (ecx = self; stack: damage record; ret 4). StickyBombProjectile slot 0x34 (damage). While health +0x454
// is not at or below 0: health -= record+0x14; still above 0 (or unordered): capped to +0x458. At or below 0: with
// record byte 0 bit 0 set a random fuse +0x64c = rand() * 2.136e-5 (returns rand()), otherwise the bomb explodes
// (StickyBombProjectile_Explode(0, self, 0)). Not executed in recorded runs.
uint32_t StickyBombProjectile_Damage(uint32_t self, uint32_t record)
{
    if (Fl(self + 0x454) <= K(kZero))
        return 0;
    const float h = Fl(self + 0x454) - Fl(record + 0x14);
    Fl(self + 0x454) = h;
    if (h <= K(kZero))
    {
        if (By(record) & 1)
        {
            const uint32_t r = Rand();
            Fl(self + 0x64c) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bce60);
            return r;
        }
        return StickyBombProjectile_Explode(0, self, 0);
    }
    if (h > Fl(self + 0x458))
        D(self + 0x454) = D(self + 0x458);
    return 0;
}
static uint32_t StickyBombProjectile_Damage_Abi(uint32_t ecx, uint32_t a0)
{
    return StickyBombProjectile_Damage(ecx, a0);
}
ABI_ENTRY_00481b90(StickyBombProjectile_Damage_Entry, StickyBombProjectile_Damage_Abi)
HOOK(0x00481b90, "StickyBombProjectile_Damage", StickyBombProjectile_Damage_Entry, nullptr);

// 0x00481c20 (ecx = self). StickyBombProjectile slot 0x37 (remove): drops the stuck object +0x5d4, stops the loop
// sound +0x668 (sound vt+0xd8(&+0x668, 1)), then a tail jump to the base 0x0047ebd0 (ecx = self). Not executed in
// recorded runs.
uint32_t StickyBombProjectile_Remove(uint32_t self)
{
    if (const uint32_t o = D(self + 0x5d4))
    {
        Unref(o);
        D(self + 0x5d4) = 0;
    }
    if (D(self + 0x668))
        VC(D(GameP() + 0x2c), 0xd8, self + 0x668, 1u);
    return orig::fn_0047ebd0(self);
}
static uint32_t StickyBombProjectile_Remove_Abi(uint32_t ecx)
{
    return StickyBombProjectile_Remove(ecx);
}
ABI_ENTRY_00481c20(StickyBombProjectile_Remove_Entry, StickyBombProjectile_Remove_Abi)
HOOK(0x00481c20, "StickyBombProjectile_Remove", StickyBombProjectile_Remove_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00481c90 (ecx = self; stack: a0..a3; ret 0x10). Slot 6 (property read) of GurnerProjectile (0x005a4c38),
// GrenadeProjectile (0x005a5010) and the Projectile vtables 0x005a6608 / 0x005adc78: the base 0x0047e0c0(self;
// a0..a3), then ObjectList_Unlink 0x00454750 from (game+0xa84)+0xf1c. Returns the unlink's eax. Not executed in
// recorded runs.
uint32_t fn_00481c90(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_0047e0c0(self, a0, a1, a2, a3);
    return orig::fn_00454750(D(D(GameP() + 0xa84) + 0xf1c), self);
}
static uint32_t fn_00481c90_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00481c90(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00481c90(fn_00481c90_Entry, fn_00481c90_Abi)
HOOK(0x00481c90, "fn_00481c90", fn_00481c90_Entry, nullptr);

// 0x00481ce0 (ecx = self). Slot 0x2f (update) of the Projectile vtable 0x005a6608: 0x00454c90 (eax = self, cl = 0;
// 1) clears +0x424 bit 0x20, then a tail jump to 0x0047e580 (ecx = the eax 0x00454c90 leaves, self). Not executed
// in recorded runs.
uint32_t fn_00481ce0(uint32_t self)
{
    orig::fn_00454c90(self, self & 0xffffff00u, 1);
    return orig::fn_0047e580(self);
}
static uint32_t fn_00481ce0_Abi(uint32_t ecx)
{
    return fn_00481ce0(ecx);
}
ABI_ENTRY_00481ce0(fn_00481ce0_Entry, fn_00481ce0_Abi)
HOOK(0x00481ce0, "fn_00481ce0", fn_00481ce0_Entry, nullptr);

// 0x00481d00 (ecx = self). Slot 0x4f of the Projectile vtable 0x005a6608: 0x0047eba0(self) | 0x10 (guess: damage
// type flags). Plain ret. Not executed in recorded runs.
uint32_t fn_00481d00(uint32_t self)
{
    return orig::fn_0047eba0(self) | 0x10u;
}
static uint32_t fn_00481d00_Abi(uint32_t ecx)
{
    return fn_00481d00(ecx);
}
ABI_ENTRY_00481d00(fn_00481d00_Entry, fn_00481d00_Abi)
HOOK(0x00481d00, "fn_00481d00", fn_00481d00_Entry, nullptr);

// 0x00481d10 (ecx = self; stack: hit object, record, a2; ret 0xc). Slot 0x4e (+0x138, the hit) of the Projectile
// vtable 0x005a6608: moves to record+4 (0x00457870, al = 0) and copies it to +0x514; +0x454 = 0; 0x0047f020 (ebx =
// +0x474; 0, record+4, 0, +0x470, 0x0047e3a0(self), vt+0x13c(), &+0x478) - vt+0x13c (0x00481d00, plain ret) leaves
// its pushed &+0x478 as the seventh argument; vt+0x158(object, a2); vt+0xd4(0). Returns al = 0 (upper bytes from
// vt+0xd4). Not executed in recorded runs.
uint32_t fn_00481d10(uint32_t self, uint32_t object, uint32_t record, uint32_t a2)
{
    const uint32_t p = record + 4;
    orig::fn_00457870(0, p, self, 0, 0, 0);
    D(self + 0x514) = D(p);
    D(self + 0x518) = D(p + 4);
    D(self + 0x51c) = D(p + 8);
    D(self + 0x454) = 0;
    const uint32_t flags = VC(self, 0x13c);  // plain ret: its pushed &+0x478 stays as the 7th argument below
    const uint32_t kind = orig::fn_0047e3a0(self);
    orig::fn_0047f020(self + 0x474, 0, p, 0, D(self + 0x470), kind, flags, self + 0x478);
    VC(self, 0x158, object, a2);
    return VC(self, 0xd4, 0u) & 0xffffff00u;
}
static uint32_t fn_00481d10_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00481d10(ecx, a0, a1, a2);
}
ABI_ENTRY_00481d10(fn_00481d10_Entry, fn_00481d10_Abi)
HOOK(0x00481d10, "fn_00481d10", fn_00481d10_Entry, nullptr);

// 0x00481db0 (ecx = self; stack: a0..a3; ret 0x10). BlackHoleProjectile (vtable 0x005ad858) slot 6, the property
// read: the base 0x0047e0c0(self; a0..a3), unlinked from (game+0xa84)+0xf1c; "final_height" +0x588 (40.0),
// "suction_lifetime" +0x58c (10.0), "start_delay" +0x594 (2.0); +0x590 = +0x58c; game+0xadc += 1 (guess: live black
// holes); vt+0x148(player), vt+0x154(), vt+0x6c(&+0x598) (position); +0x5a0 += +0x588 (z of +0x598); linked into
// the list again; velocity 0 (0x00458510), physics on (0x004577b0(1)). Returns its eax. Not executed in recorded
// runs.
uint32_t BlackHoleProjectile_ReadProps(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_0047e0c0(self, a0, a1, a2, a3);
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xf1c), self);
    orig::fn_004534a0(a3, self + 0x588, self, 0x0059e2d8, a2, 0x42200000, 0);  // "final_height", 40.0
    orig::fn_004534a0(a3, self + 0x58c, self, 0x0059e2c4, a2, 0x41200000, 0);  // "suction_lifetime", 10.0
    orig::fn_004534a0(a3, self + 0x594, self, 0x005b93f8, a2, 0x40000000, 0);  // "start_delay", 2.0
    D(self + 0x590) = D(self + 0x58c);
    ++I(GameP() + 0xadc);
    VC(self, 0x148, D(GameP() + 0x748));
    VC(self, 0x154);
    VC(self, 0x6c, self + 0x598);
    Fl(self + 0x5a0) = Fl(self + 0x588) + Fl(self + 0x5a0);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf1c), self);
    uint32_t v[3] = {0, 0, 0};
    orig::fn_00458510(u(v), self);
    return static_cast<uint32_t>(orig::fn_004577b0(self, 1));
}
static uint32_t BlackHoleProjectile_ReadProps_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return BlackHoleProjectile_ReadProps(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00481db0(BlackHoleProjectile_ReadProps_Entry, BlackHoleProjectile_ReadProps_Abi)
HOOK(0x00481db0, "BlackHoleProjectile_ReadProps", BlackHoleProjectile_ReadProps_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00481ef0 (ecx = self). BlackHoleProjectile slot 0x2f, the update. Unless 0x0047e4a0(self) (al), the position
// (vt+0x6c into +0x514) eases towards the target point +0x598: pos = pos * 0.95 + target * 0.05, moved there
// (0x00457870). When it is now within 2.0 of the target but the previous position +0x508 was not, vt+0xf4(0). The
// start delay +0x594 counts down; when it runs out: 0x0047f020 (ebx = +0x474; 0, &+0x598, player, 0, player, 0x800,
// self) and the hole joins list (game+0xa84)+0xf00 (guess: the suction list). Afterwards the suction time +0x590
// counts down; at 0 it leaves that list and +0x590 = 1e20. Not executed in recorded runs.
uint32_t BlackHoleProjectile_Update(uint32_t self)
{
    uint32_t r = orig::fn_0047e4a0(self);
    if (static_cast<uint8_t>(r))
        return r;
    const uint32_t pos = self + 0x514;
    const uint32_t t = self + 0x598;
    r = VC(self, 0x6c, pos);
    Fl(pos) = Fl(pos) * K(0x005bce54) + Fl(t) * K(0x005bcc34);
    Fl(self + 0x518) = Fl(self + 0x59c) * K(0x005bcc34) + Fl(self + 0x518) * K(0x005bce54);
    Fl(self + 0x51c) = Fl(self + 0x5a0) * K(0x005bcc34) + Fl(self + 0x51c) * K(0x005bce54);
    orig::fn_00457870(r & 0xffffff00u, pos, self, 0, 0, 0);
    {
        const float dz = Fl(t + 8) - Fl(pos + 8);
        const float dy = Fl(t + 4) - Fl(pos + 4);
        const float dx = Fl(t) - Fl(pos);
        if (dy * dy + dz * dz + dx * dx < K(0x005bcbe0))
        {
            const float oz = Fl(t + 8) - Fl(self + 0x510);
            const float oy = Fl(t + 4) - Fl(self + 0x50c);
            const float ox = Fl(t) - Fl(self + 0x508);
            const float o2 = ox * ox + oy * oy + oz * oz;
            if (!(o2 < K(0x005bcbe0)) && o2 == o2)
                VC(self, 0xf4, 0u);
        }
    }
    if (Fl(self + 0x594) > K(kZero))
    {
        const float d = Fl(self + 0x594) - Dt();
        Fl(self + 0x594) = d;
        if (d > K(kZero) || d != d)
            return 0;
        const uint32_t pl = D(GameP() + 0x748);
        orig::fn_0047f020(self + 0x474, 0, t, pl, 0, pl, 0x800, self);
        return static_cast<uint32_t>(orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf00), self));
    }
    const float s = Fl(self + 0x590) - Dt();
    Fl(self + 0x590) = s;
    if (s > K(kZero) || s != s)
        return 0;
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xf00), self);
    D(self + 0x590) = 0x60ad78ec;  // 1e20
    return 0;
}
static uint32_t BlackHoleProjectile_Update_Abi(uint32_t ecx)
{
    return BlackHoleProjectile_Update(ecx);
}
ABI_ENTRY_00481ef0(BlackHoleProjectile_Update_Entry, BlackHoleProjectile_Update_Abi)
HOOK(0x00481ef0, "BlackHoleProjectile_Update", BlackHoleProjectile_Update_Entry, nullptr);

// 0x004820b0 (ecx = self; stack: a0 (unused); ret 4). Slot 0x35 (+0xd4) of GrenadeProjectile (0x005a5010) and
// BlackHoleProjectile (0x005ad858): +0x454 = 0; when +0x474 != 0, 0x0047f020 (ebx = +0x474; 0, &+0x514, self,
// +0x470, 0x0047e3a0(self), vt+0x13c(), 0) - vt+0x13c (0x0047eba0, plain ret) leaves its pushed 0 as the seventh
// argument; vt+0x158(0, 0); then the base slot 0x00433bb0(self; 0). Returns its eax. Not executed in recorded runs.
uint32_t fn_004820b0(uint32_t self, uint32_t /*a0*/)
{
    D(self + 0x454) = 0;
    if (!(Fl(self + 0x474) == K(kZero)))
    {
        const uint32_t flags = VC(self, 0x13c);  // plain ret: its pushed 0 stays as the 7th argument below
        const uint32_t kind = orig::fn_0047e3a0(self);
        orig::fn_0047f020(self + 0x474, 0, self + 0x514, self, D(self + 0x470), kind, flags, 0);
    }
    VC(self, 0x158, 0u, 0u);
    return orig::fn_00433bb0(self, 0);
}
static uint32_t fn_004820b0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004820b0(ecx, a0);
}
ABI_ENTRY_004820b0(fn_004820b0_Entry, fn_004820b0_Abi)
HOOK(0x004820b0, "fn_004820b0", fn_004820b0_Entry, nullptr);

// 0x00482120 (ecx = self; stack: a0..a3; ret 0x10). BlackHoleBox (vtable 0x005a2940) slot 6, the property read: the
// base 0x0047e0c0(self; a0..a3), unlinked from (game+0xa84)+0xf1c, "initial_arc_distance" +0x588 (20.0), game+0xadc
// += 1, byte +0x58c = 0. Returns the game pointer (eax as left). Not executed in recorded runs.
uint32_t BlackHoleBox_ReadProps(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_0047e0c0(self, a0, a1, a2, a3);
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xf1c), self);
    orig::fn_004534a0(a3, self + 0x588, self, 0x0059e428, a2, 0x41a00000, 0);  // "initial_arc_distance", 20.0
    const uint32_t g = GameP();
    ++I(g + 0xadc);
    By(self + 0x58c) = 0;
    return g;
}
static uint32_t BlackHoleBox_ReadProps_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return BlackHoleBox_ReadProps(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00482120(BlackHoleBox_ReadProps_Entry, BlackHoleBox_ReadProps_Abi)
HOOK(0x00482120, "BlackHoleBox_ReadProps", BlackHoleBox_ReadProps_Entry, nullptr);

// 0x004821a0 (ecx = self). Slot 0x37 (remove) of BlackHoleBox (0x005a2940) and BlackHoleProjectile (0x005ad858):
// game+0xadc -= 1, then a tail jump to the base 0x0047ebd0 (ecx = self). Not executed in recorded runs.
uint32_t BlackHole_Remove(uint32_t self)
{
    --I(GameP() + 0xadc);
    return orig::fn_0047ebd0(self);
}
static uint32_t BlackHole_Remove_Abi(uint32_t ecx)
{
    return BlackHole_Remove(ecx);
}
ABI_ENTRY_004821a0(BlackHole_Remove_Entry, BlackHole_Remove_Abi)
HOOK(0x004821a0, "BlackHole_Remove", BlackHole_Remove_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004821b0 (ecx = self; stack: matrix, a1; ret 8). BlackHoleBox slot 0x50 (+0x140), the throw: vt+0x148(a1),
// Object_SetTransform 0x00457990 (self, matrix), vt+0x54(+0x514), +0x508 = +0x514; the camera direction
// (Camera_GetDirection 0x0040fa10, esi = local; game+0xa68) gives the target pos + dir * +0x588 ("initial_arc_distance");
// +0x460 = 50.0; 0x00471a00 (ebx = +0x57c, esi = target, edi = +0x514; 50.0, -64.348, 1), Body_setLinearVelocity,
// vt+0x154(), Anim_Set 0x00408d90 (eax = -1, esi = self; "player_blackhole_box_open", 0x90, 0, 0, 0, 0). Returns
// Anim_Set's eax. Not executed in recorded runs.
uint32_t BlackHoleBox_Launch(uint32_t self, uint32_t matrix, uint32_t a1)
{
    float dir[3];
    float target[3];
    const uint32_t r = VC(self, 0x148, a1);
    orig::fn_00457990(r & 0xffffff00u, self, matrix);
    const uint32_t pos = self + 0x514;
    VC(self, 0x54, pos);
    D(self + 0x508) = D(pos);
    D(self + 0x50c) = D(pos + 4);
    D(self + 0x510) = D(pos + 8);
    orig::fn_0040fa10(u(dir), D(GameP() + 0xa68));
    D(self + 0x460) = 0x42480000;  // 50.0
    target[0] = dir[0] * Fl(self + 0x588) + Fl(pos);
    target[1] = dir[1] * Fl(self + 0x588) + Fl(self + 0x518);
    target[2] = dir[2] * Fl(self + 0x588) + Fl(self + 0x51c);
    orig::fn_00471a00(self + 0x57c, u(target), pos, 0x42480000, 0xc280b23a, 1);
    orig::fn_00458510(self + 0x57c, self);
    VC(self, 0x154);
    return orig::fn_00408d90(0xffffffff, self, 0x0059e2a8, 0x90, 0, 0, 0, 0);  // "player_blackhole_box_open"
}
static uint32_t BlackHoleBox_Launch_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return BlackHoleBox_Launch(ecx, a0, a1);
}
ABI_ENTRY_004821b0(BlackHoleBox_Launch_Entry, BlackHoleBox_Launch_Abi)
HOOK(0x004821b0, "BlackHoleBox_Launch", BlackHoleBox_Launch_Entry, nullptr);

// 0x004822b0 (ecx = self). BlackHoleBox slot 0x2f, the update. Unless 0x0047e4a0(self) (al):
//   - closed (byte +0x58c clear): position vt+0x54(+0x514); when it moved since +0x508 (any component non-zero),
//     +0x538 = the distance and a ray old -> new (0x004137e0) that hits lands the box: Object_ForceGround(5.0, 10.0),
//     +0x58c = 1, velocity 0, physics on, Anim_Set("player_blackhole_box_open", 0x80). Then +0x508 = +0x514.
//   - open: when the animation slot 4 (0x00408800) has flag +0x1c bit 1 (guess: finished): Anim_Set(..., 0x10),
//     the box matrix (vt+0x70) and the black hole "player_blackhole" is created there (ObjectSystem_CreateObject).
// Not executed in recorded runs.
uint32_t BlackHoleBox_Update(uint32_t self)
{
    uint32_t fr[0x54 / 4];  // the original frame (sub esp, 0x54)
    const uint32_t F = u(fr);
    uint32_t r = orig::fn_0047e4a0(self);
    if (static_cast<uint8_t>(r))
        return r;
    if (By(self + 0x58c))
    {
        r = orig::fn_00408800(4, self);
        if (!(By(r + 0x1c) & 2))
            return r;
        orig::fn_00408d90(0xffffffff, self, 0x0059e2a8, 0x10, 0, 0, 0, 0);  // "player_blackhole_box_open"
        VC(self, 0x70, F + 0x10);
        return orig::fn_0045f020(0, 0x0059e294, D(GameP() + 0xa84), F + 0x10, 0x3ff, 0, 0, 0, 0, 0xaa,
                                 0);  // "player_blackhole"
    }
    const uint32_t pos = self + 0x514;
    const uint32_t old = self + 0x508;
    VC(self, 0x54, pos);
    Fl(F + 0x04) = Fl(pos) - Fl(old);
    Fl(F + 0x08) = Fl(pos + 4) - Fl(old + 4);
    Fl(F + 0x0c) = Fl(pos + 8) - Fl(old + 8);
    if (!(Fl(F + 0x04) == K(kZero)) || !(Fl(F + 0x08) == K(kZero)) || !(Fl(F + 0x0c) == K(kZero)))
    {
        const float len = Normalize3(F + 0x04, F + 0x04);
        Fl(F + 0x00) = len;
        Fl(self + 0x538) = len;
        if (static_cast<uint8_t>(orig::fn_004137e0(F + 0x00, F + 0x04, D(GameP() + 0xa8c), old, D(F + 0x00))))
        {
            orig::fn_00455860(self, 0x40a00000, 0x41200000);  // 5.0, 10.0
            By(self + 0x58c) = 1;
            D(F + 0x04) = 0;
            D(F + 0x08) = 0;
            D(F + 0x0c) = 0;
            orig::fn_00458510(F + 0x04, self);
            orig::fn_004577b0(self, 1);
            orig::fn_00408d90(0xffffffff, self, 0x0059e2a8, 0x80, 0, 0, 0, 0);  // "player_blackhole_box_open"
        }
    }
    D(old) = D(pos);
    D(old + 4) = D(pos + 4);
    const uint32_t z = D(pos + 8);
    D(old + 8) = z;
    return z;
}
static uint32_t BlackHoleBox_Update_Abi(uint32_t ecx)
{
    return BlackHoleBox_Update(ecx);
}
ABI_ENTRY_004822b0(BlackHoleBox_Update_Entry, BlackHoleBox_Update_Abi)
HOOK(0x004822b0, "BlackHoleBox_Update", BlackHoleBox_Update_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00482460 (ecx = self; stack: a0..a3; ret 0x10). ArrowProjectile (vtable 0x005a9a40) slot 6, the property read:
// +0x5d8 = 0 (the trail effect), the base 0x0047e0c0(self; a0..a3), "linger_lifetime" +0x588 (10.0), byte +0x58c = 0
// (stuck), +0x5d4 = 0 (fly-by sound), unlinked from (game+0xa84)+0xf1c. Returns the unlink's eax. Not executed in
// recorded runs.
uint32_t ArrowProjectile_ReadProps(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    D(self + 0x5d8) = 0;
    orig::fn_0047e0c0(self, a0, a1, a2, a3);
    orig::fn_004534a0(a3, self + 0x588, self, 0x0059e344, a2, 0x41200000, 0);  // "linger_lifetime", 10.0
    By(self + 0x58c) = 0;
    D(self + 0x5d4) = 0;
    return orig::fn_00454750(D(D(GameP() + 0xa84) + 0xf1c), self);
}
static uint32_t ArrowProjectile_ReadProps_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return ArrowProjectile_ReadProps(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00482460(ArrowProjectile_ReadProps_Entry, ArrowProjectile_ReadProps_Abi)
HOOK(0x00482460, "ArrowProjectile_ReadProps", ArrowProjectile_ReadProps_Entry, nullptr);

// 0x004824e0 (ecx = self). ArrowProjectile slot 0x37 (remove): Fx_Release 0x0041d030(FxSystem game+0xa90, &+0x5d8),
// then a tail jump to the base 0x0047ebd0 (ecx = self). Not executed in recorded runs.
uint32_t ArrowProjectile_Remove(uint32_t self)
{
    orig::fn_0041d030(D(GameP() + 0xa90), self + 0x5d8);
    return orig::fn_0047ebd0(self);
}
static uint32_t ArrowProjectile_Remove_Abi(uint32_t ecx)
{
    return ArrowProjectile_Remove(ecx);
}
ABI_ENTRY_004824e0(ArrowProjectile_Remove_Entry, ArrowProjectile_Remove_Abi)
HOOK(0x004824e0, "ArrowProjectile_Remove", ArrowProjectile_Remove_Entry, nullptr);

// 0x00482510 (ecx = self; stack: matrix, a1; ret 8). ArrowProjectile slot 0x50 (+0x140), the launch: vt+0x148(a1),
// Object_SetTransform (self, matrix), vt+0x54(+0x514). With d = target +0x520 - pos, the flight time T =
// 3.0 * sqrt(sqrt(|d|^2)) / 0x0047e360(self) (st0; guess: a speed) and the impulse (Body_ApplyImpulse 0x00458440,
// eax = 0, ecx = local, edx = self) = mass +0x468 * (d.x / T, d.y / T, d.z / T - T * -64.348 * 0.5) (the original
// keeps T and 1/T in its own argument slots). Then 0x00458000 (velocity into +0x57c), 0x00455e70(0, 0), +0x5d8 =
// vt+0x154() (the trail effect), returned. Not executed in recorded runs.
uint32_t ArrowProjectile_Launch(uint32_t self, uint32_t matrix, uint32_t a1)
{
    float imp[3];
    const uint32_t r = VC(self, 0x148, a1);
    orig::fn_00457990(r & 0xffffff00u, self, matrix);
    const uint32_t pos = self + 0x514;
    VC(self, 0x54, pos);
    const float dx = Fl(self + 0x520) - Fl(pos);
    const float dy = Fl(self + 0x524) - Fl(pos + 4);
    const float dz = Fl(self + 0x528) - Fl(pos + 8);
    const float t0 = X87Sqrt(X87Sqrt(dz * dz + dy * dy + dx * dx)) * K(0x005bcb7c);
    const float T = t0 / orig::fn_0047e360(self);
    const float inv = K(0x005bcb2c) / T;
    imp[0] = inv * Fl(self + 0x468) * dx;
    imp[1] = inv * Fl(self + 0x468) * dy;
    imp[2] = (inv * dz - T * K(0x005bcd64) * K(0x005bcb4c)) * Fl(self + 0x468);
    orig::fn_00458440(0, u(imp), self);
    orig::fn_00458000(self, self + 0x57c);
    orig::fn_00455e70(self, 0, 0);
    const uint32_t fx = VC(self, 0x154);
    D(self + 0x5d8) = fx;
    return fx;
}
static uint32_t ArrowProjectile_Launch_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return ArrowProjectile_Launch(ecx, a0, a1);
}
ABI_ENTRY_00482510(ArrowProjectile_Launch_Entry, ArrowProjectile_Launch_Abi)
HOOK(0x00482510, "ArrowProjectile_Launch", ArrowProjectile_Launch_Entry, nullptr);

// 0x00482620 (ecx = self). ArrowProjectile slot 0x2f, the update. Attached to a parent (Object_GetParent 0x004539d0)
// and not stuck: nothing. Otherwise 0x00454c90 (eax = self, cl = 0; 1) clears +0x424 bit 0x20. Stuck (byte
// +0x58c): 0x0047e9b0 (esi = self; +0x590), the linger time +0x588 counts down, at or below 0 vt+0xd4(0). In
// flight: 0x0047e580(self); when vt+0xd8(): once (+0x5d4 == 0) within sqrt(4900) of the player a fly-by sound
// ("wpn_arrow_norm_by_1" when (rand() * 100) & ~0x7fff < 0x190000, else "_2") into +0x5d4; then 0x00455e70(0, 0).
// Not executed in recorded runs.
uint32_t ArrowProjectile_Update(uint32_t self)
{
    const uint32_t parent = orig::fn_004539d0(self);
    if (parent && !By(self + 0x58c))
        return parent & 0xffffff00u;
    orig::fn_00454c90(self, self & 0xffffff00u, 1);
    if (By(self + 0x58c))
    {
        orig::fn_0047e9b0(self, self + 0x590);
        const float t = Fl(self + 0x588) - Dt();
        Fl(self + 0x588) = t;
        if (t <= K(kZero))
            return VC(self, 0xd4, 0u);
        return 0;
    }
    orig::fn_0047e580(self);
    const uint32_t r = VC(self, 0xd8);
    if (!static_cast<uint8_t>(r))
        return r;
    if (D(self + 0x5d4) == 0)
    {
        const uint32_t pl = D(GameP() + 0x748);
        const float dz = Fl(self + 0x51c) - Fl(pl + 0xc14);
        const float dy = Fl(self + 0x518) - Fl(pl + 0xc10);
        const float dx = Fl(self + 0x514) - Fl(pl + 0xc0c);
        if (dx * dx + dy * dy + dz * dz < K(0x005bcee8))
        {
            const int32_t v = static_cast<int32_t>((Rand() * 100u) & 0xffff8000u);
            const uint32_t name = v < 0x190000 ? 0x0059e280u : 0x0059e26cu;  // "wpn_arrow_norm_by_1" / "_2"
            D(self + 0x5d4) = VC(D(GameP() + 0x2c), 0xd0, name, self, 0u, 0u, 0xffffffffu, 0u);
        }
    }
    return orig::fn_00455e70(self, 0, 0);
}
static uint32_t ArrowProjectile_Update_Abi(uint32_t ecx)
{
    return ArrowProjectile_Update(ecx);
}
ABI_ENTRY_00482620(ArrowProjectile_Update_Entry, ArrowProjectile_Update_Abi)
HOOK(0x00482620, "ArrowProjectile_Update", ArrowProjectile_Update_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00482780 (ecx = self; stack: hit object, record, a2; ret 0xc). ArrowProjectile slot 0x4e (+0x138), the hit:
// moved to record+4 (0x00457870, al = 0) and +0x514 = it; physics on (0x004577b0(1)); linked into
// (game+0xa84)+0xf1c; 0x0047e9f0 (eax = record, ecx = object, ebx = self; +0x590); when +0x474 != 0, 0x0047f020
// (ebx = +0x474; object, record+4, self, +0x470, 0x0047e3a0(self), vt+0x13c(), 0) - vt+0x13c (0x0047eba0, plain
// ret) leaves its pushed 0 as the seventh argument; with an object 0x0047eec0 (eax = object, ebx = self; record);
// the trail Fx_Release(FxSystem, &+0x5d8); byte +0x58c = 1 (stuck); vt+0x158(object, a2). Returns al = 0 (upper
// bytes from vt+0x158). Not executed in recorded runs.
uint32_t ArrowProjectile_Hit(uint32_t self, uint32_t object, uint32_t record, uint32_t a2)
{
    const uint32_t p = record + 4;
    orig::fn_00457870(0, p, self, 0, 0, 0);
    D(self + 0x514) = D(p);
    D(self + 0x518) = D(p + 4);
    D(self + 0x51c) = D(p + 8);
    orig::fn_004577b0(self, 1);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf1c), self);
    orig::fn_0047e9f0(record, object, self, self + 0x590);
    if (!(Fl(self + 0x474) == K(kZero)))
    {
        const uint32_t flags = VC(self, 0x13c);  // plain ret: its pushed 0 stays as the 7th argument below
        const uint32_t kind = orig::fn_0047e3a0(self);
        orig::fn_0047f020(self + 0x474, object, p, self, D(self + 0x470), kind, flags, 0);
    }
    if (object)
        orig::fn_0047eec0(object, self, record);
    orig::fn_0041d030(D(GameP() + 0xa90), self + 0x5d8);
    By(self + 0x58c) = 1;
    return VC(self, 0x158, object, a2) & 0xffffff00u;
}
static uint32_t ArrowProjectile_Hit_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return ArrowProjectile_Hit(ecx, a0, a1, a2);
}
ABI_ENTRY_00482780(ArrowProjectile_Hit_Entry, ArrowProjectile_Hit_Abi)
HOOK(0x00482780, "ArrowProjectile_Hit", ArrowProjectile_Hit_Entry, nullptr);

// 0x00482870 (ecx = self; stack: matrix, a1; ret 8). GrenadeProjectile (vtable 0x005a5010) slot 0x50 (+0x140), the
// throw: vt+0x148(a1), Object_SetTransform (self, matrix), vt+0x54(+0x514). The target +0x520 is pulled back towards
// the thrower: d = normalised(target - pos), L = |target - pos| - 20 when above 20 (else unchanged), target = pos +
// d * L. 0x00471a00 (ebx = +0x57c, esi = +0x520, edi = +0x514; 0x0047e360(self) (st0), -64.348, 0),
// Body_setLinearVelocity, vt+0x154(), and a random spin: Body_ApplyAngularImpulse 0x004583d0 with three rand() *
// 1.526e-6. Not executed in recorded runs.
uint32_t GrenadeProjectile_Launch(uint32_t self, uint32_t matrix, uint32_t a1)
{
    float spin[3];
    const uint32_t r = VC(self, 0x148, a1);
    orig::fn_00457990(r & 0xffffff00u, self, matrix);
    const uint32_t pos = self + 0x514;
    VC(self, 0x54, pos);
    float dx = Fl(self + 0x520) - Fl(pos);
    float dy = Fl(self + 0x524) - Fl(pos + 4);
    float dz = Fl(self + 0x528) - Fl(pos + 8);
    float len = X87Sqrt(dz * dz + dy * dy + dx * dx);
    if (!(len == K(kZero)))
    {
        const float inv = K(0x005bcb2c) / len;
        dx = dx * inv;
        dy = dy * inv;
        dz = inv * dz;
    }
    if (len > K(0x005bcbe8))
        len = len - K(0x005bcbe8);
    Fl(self + 0x520) = dx * len + Fl(pos);
    Fl(self + 0x524) = dy * len + Fl(self + 0x518);
    Fl(self + 0x528) = dz * len + Fl(self + 0x51c);
    const float q = orig::fn_0047e360(self);
    orig::fn_00471a00(self + 0x57c, self + 0x520, pos, fb(q), 0xc280b23a, 0);
    orig::fn_00458510(self + 0x57c, self);
    VC(self, 0x154);
    spin[0] = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bce50);
    spin[1] = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bce50);
    spin[2] = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bce50);
    return orig::fn_004583d0(u(spin), self);
}
static uint32_t GrenadeProjectile_Launch_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return GrenadeProjectile_Launch(ecx, a0, a1);
}
ABI_ENTRY_00482870(GrenadeProjectile_Launch_Entry, GrenadeProjectile_Launch_Abi)
HOOK(0x00482870, "GrenadeProjectile_Launch", GrenadeProjectile_Launch_Entry, nullptr);

// 0x004829e0 (ecx = self; stack: a0..a3; ret 0x10). CaptainRocket (vtable 0x005a11d8) slot 6, the property read: the
// base 0x0047e0c0(self; a0..a3); "min_speed" +0x588 (10.0), "max_speed" +0x58c (30.0), "turn_power" +0x590 (30.0),
// "dumb_fire" +0x594 (Object_ReadBoolProp 0x004534f0, al = 0); both speeds * 3.0, +0x544 = 0, byte +0x548 = 0,
// +0x598 = 0, +0x460 = the max speed. Returns 0. Not executed in recorded runs.
uint32_t CaptainRocket_ReadProps(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_0047e0c0(self, a0, a1, a2, a3);
    orig::fn_004534a0(a3, self + 0x588, self, 0x0059e260, a2, 0x41200000, 0);  // "min_speed", 10.0
    orig::fn_004534a0(a3, self + 0x58c, self, 0x0059e3e0, a2, 0x41f00000, 0);  // "max_speed", 30.0
    const uint32_t r = orig::fn_004534a0(a3, self + 0x590, self, 0x0059e254, a2, 0x41f00000, 0);  // "turn_power"
    orig::fn_004534f0(r & 0xffffff00u, a3, self + 0x594, self, 0x0059e248, a2, 0);  // "dumb_fire"
    Fl(self + 0x588) = Fl(self + 0x588) * K(0x005bcb7c);
    D(self + 0x544) = 0;
    By(self + 0x548) = 0;
    const float mx = Fl(self + 0x58c) * K(0x005bcb7c);
    D(self + 0x598) = 0;
    Fl(self + 0x58c) = mx;
    Fl(self + 0x460) = mx;
    return 0;
}
static uint32_t CaptainRocket_ReadProps_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return CaptainRocket_ReadProps(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004829e0(CaptainRocket_ReadProps_Entry, CaptainRocket_ReadProps_Abi)
HOOK(0x004829e0, "CaptainRocket_ReadProps", CaptainRocket_ReadProps_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00482ab0 (ecx = self; stack: owner; ret 4). CaptainRocket slot 0x52 (+0x148): +0x574 = 0, +0x504 = owner
// (referenced); a 0x18-byte object list from the heap 0x0063a210 built by 0x004e7010 (eax = game+0xac4, edx = 0, esi
// = memory; no null test on failure, as the original) into +0x570; the owner and self are added (list vt+0x18) and
// referenced. Guess: the objects the rocket ignores. Returns the second add's eax. Not executed in recorded runs.
uint32_t CaptainRocket_SetOwner(uint32_t self, uint32_t owner)
{
    D(self + 0x574) = 0;
    D(self + 0x504) = owner;
    ++I(owner + 0x28);
    const uint32_t mem = orig::fn_004e07f0(0x0063a210, 0x10, 0x18, 2, 0x0058b5e8);
    const uint32_t list = mem ? orig::fn_004e7010(D(GameP() + 0xac4), 0, mem) : 0;
    D(self + 0x570) = list;
    VC(list, 0x18, D(self + 0x504));
    ++I(D(self + 0x504) + 0x28);
    const uint32_t r = VC(D(self + 0x570), 0x18, self);
    ++I(self + 0x28);
    return r;
}
static uint32_t CaptainRocket_SetOwner_Abi(uint32_t ecx, uint32_t a0)
{
    return CaptainRocket_SetOwner(ecx, a0);
}
ABI_ENTRY_00482ab0(CaptainRocket_SetOwner_Entry, CaptainRocket_SetOwner_Abi)
HOOK(0x00482ab0, "CaptainRocket_SetOwner", CaptainRocket_SetOwner_Entry, nullptr);

// 0x00482b40 (ecx = self). CaptainRocket slot 0x2f, the update. 0x0047e580(self); nothing unless vt+0xd8(), not
// "dumb_fire" (byte +0x594) and (game+0xa84)+0xf00 vt+0x34() is 0. Once (+0x598 == 0), within sqrt(4900) of the
// player (squared distance 0x004e8080, recomputed here because it returns st0 while the ABI table lists none) the
// sound "wpn_rocket_passby_med_1" into +0x598. A target +0x544 whose vt+0xd8() is false is dropped. With a target:
// T = the translation of its matrix (0x00453c50), 0x00471970 (pos, T; min speed +0x588, max speed +0x58c, 1.0, 0),
// 0x00471750 with mass * turn power +0x590, 0x00455a30 into +0x39c. Then 0x00455e70(0, 0). Not executed in recorded
// runs.
uint32_t CaptainRocket_Update(uint32_t self)
{
    uint32_t fr[0x60 / 4];  // the original frame (sub esp, 0x60)
    const uint32_t F = u(fr);
    orig::fn_0047e580(self);
    uint32_t r = VC(self, 0xd8);
    if (!static_cast<uint8_t>(r))
        return r;
    if (By(self + 0x594))
        return r;
    r = VC(D(D(GameP() + 0xa84) + 0xf00), 0x34);
    if (r)
        return r;
    if (D(self + 0x598) == 0)
    {
        const uint32_t a = self + 0x514;
        const uint32_t b = D(GameP() + 0x748) + 0xc0c;
        const float dz = Fl(a + 8) - Fl(b + 8);
        const float dy = Fl(a + 4) - Fl(b + 4);
        const float dx = Fl(a) - Fl(b);
        if (dx * dx + dy * dy + dz * dz < K(0x005bcee8))
            D(self + 0x598) = VC(D(GameP() + 0x2c), 0xd0, 0x0059e384u, self, 0u, 0u, 0xffffffffu,
                                 0u);  // "wpn_rocket_passby_med_1"
    }
    const uint32_t slot = self + 0x544;
    if (D(slot) && !VB(D(slot), 0xd8))
    {
        Unref(D(slot));
        D(slot) = 0;
    }
    if (const uint32_t target = D(slot))
    {
        const uint32_t bone = By(self + 0x548) ? self + 0x548 : 0;
        orig::fn_00453c50(target, bone, F + 0x1c, 1, 0, 0, 1, 0, 0);
        D(F + 0x04) = D(F + 0x28);
        D(F + 0x08) = D(F + 0x38);
        D(F + 0x0c) = D(F + 0x48);
        Call_00471970(self + 0x514, F + 0x10, F + 0x04, F + 0x00, D(self + 0x588), D(self + 0x58c), 0x3f800000, 0);
        const float m1 = orig::fn_004581c0(self);
        const float m2 = orig::fn_004581c0(self) * Fl(self + 0x590);
        Fl(F + 0x00) = orig::fn_00471750(F + 0x10, self + 0x57c, F + 0x04, 0, fb(m2), fb(m1));
        D(self + 0x39c) = orig::fn_00455a30(F + 0x04, 0x10, self, D(self + 0x39c), D(F + 0x00), 0xffffffff, 0);
    }
    return orig::fn_00455e70(self, 0, 0);
}
static uint32_t CaptainRocket_Update_Abi(uint32_t ecx)
{
    return CaptainRocket_Update(ecx);
}
ABI_ENTRY_00482b40(CaptainRocket_Update_Entry, CaptainRocket_Update_Abi)
HOOK(0x00482b40, "CaptainRocket_Update", CaptainRocket_Update_Entry, nullptr);

// 0x00482d10 (ecx = self; stack: desc; ret 4). CaptainRocket slot 7 (+0x1c): the base 0x0047e320(self; desc) (which
// sets desc +0 = 0x4000), then desc +0 = 0x100 (through the edx the base leaves, = desc) and ObjectList_Link into
// (game+0xa84)+0xe90 and +0xe94. Returns the last link's eax. Not executed in recorded runs.
uint32_t CaptainRocket_InitCollision(uint32_t self, uint32_t desc)
{
    orig::fn_0047e320(self, desc);
    D(desc) = 0x100;
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe90), self);
    return static_cast<uint32_t>(orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe94), self));
}
static uint32_t CaptainRocket_InitCollision_Abi(uint32_t ecx, uint32_t a0)
{
    return CaptainRocket_InitCollision(ecx, a0);
}
ABI_ENTRY_00482d10(CaptainRocket_InitCollision_Entry, CaptainRocket_InitCollision_Abi)
HOOK(0x00482d10, "CaptainRocket_InitCollision", CaptainRocket_InitCollision_Entry, nullptr);

// 0x00482d60 (ecx = self; stack: damage record; ret 4). CaptainRocket slot 0x34 (damage). Only while health +0x454 >
// 0. An attacker (record+0x2c) other than the owner +0x504 with the same vt+0xe4() (guess: team) as the rocket is
// ignored unless its vt+0xe0(). Health -= record+0x14; still above 0: capped to +0x458; otherwise a hit record
// (0x34 bytes: position vt+0x54, -1, direction = normalised (velocity.x, velocity.y, 0.7) from 0x00458000, damage
// +0x470) goes to the rocket's own vt+0x138(0, record, 0) (the hit: it explodes). Not executed in recorded runs.
uint32_t CaptainRocket_Damage(uint32_t self, uint32_t record)
{
    uint32_t fr[0x34 / 4];  // the original frame (sub esp, 0x34): the hit record
    const uint32_t F = u(fr);
    if (!(Fl(self + 0x454) > K(kZero)) && Fl(self + 0x454) == Fl(self + 0x454))
        return 0;
    const uint32_t attacker = D(record + 0x2c);
    if (attacker != D(self + 0x504) && attacker)
    {
        const uint8_t team = static_cast<uint8_t>(VC(attacker, 0xe4));
        if (team == static_cast<uint8_t>(VC(self, 0xe4)) && !VB(D(record + 0x2c), 0xe0))
            return 0;
    }
    const float h = Fl(self + 0x454) - Fl(record + 0x14);
    Fl(self + 0x454) = h;
    if (h > K(kZero) || h != h)
    {
        if (h > Fl(self + 0x458))
            D(self + 0x454) = D(self + 0x458);
        return 0;
    }
    std::memset(fr, 0, sizeof(fr));
    VC(self, 0x54, F + 0x04);
    D(F + 0x24) = D(self + 0x470);
    D(F + 0x10) = 0xffffffff;
    orig::fn_00458000(self, F + 0x18);
    D(F + 0x20) = 0x3f333333;  // 0.7
    Normalize3(F + 0x18, F + 0x18);
    return VC(self, 0x138, 0u, F, 0u);
}
static uint32_t CaptainRocket_Damage_Abi(uint32_t ecx, uint32_t a0)
{
    return CaptainRocket_Damage(ecx, a0);
}
ABI_ENTRY_00482d60(CaptainRocket_Damage_Entry, CaptainRocket_Damage_Abi)
HOOK(0x00482d60, "CaptainRocket_Damage", CaptainRocket_Damage_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00482e80 (ecx = self; stack: object, record, a2 (unused); ret 0xc). Slot 0x4e (+0x138, the hit) of the
// Projectile vtable 0x005ac4b8: the base 0x0047eae0(self; object, record, 0), then five "twiglet" objects around the
// impact point: radii a, b = rand() * 4.578e-4 + 5.0; for i = 0..4 the direction (0, 1, 0) rotated by i * 0.2 * 2pi
// about z (a 0x44-byte matrix, 0x004e36a0 eax = vector, ecx = matrix, edx = 1; out), normalised; the point
// (record+4 + dir.x * a, record+8 + dir.y * b, record+0xc) is tested by 0x0046f280 (eax = game+0xaa4; x, y) and when
// valid a "twiglet" is created there (ObjectSystem_CreateObject), game+0x2710 += 1, moved (0x00457870), grounded
// (Object_ForceGround 1000, 10000), vt+0xf8(0x20000, 0) and 0x0049f380 (eax = twiglet, edx = rand()'s edx; one in
// ~5.3 (rand() * 100 & ~0x7fff < 0x78000) 1 else 0, count) with count = 0, 1, 2... the number created so far. The
// original realigns its stack (and esp, -8); self is not used after the base call. Returns al = 0. Not executed in
// recorded runs.
uint32_t fn_00482e80(uint32_t self, uint32_t object, uint32_t record, uint32_t /*a2*/)
{
    alignas(8) uint32_t fr[0x7c / 4];  // the original (aligned) frame
    std::memset(fr, 0, sizeof(fr));
    const uint32_t F = u(fr);
    orig::fn_0047eae0(self, object, record, 0);
    D(F + 0x28) = 0;
    D(F + 0x2c) = 0x3f800000;
    D(F + 0x30) = 0;
    Fl(F + 0x08) = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bcdd4) + K(0x005bcbdc);
    Fl(F + 0x0c) = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bcdd4) + K(0x005bcbdc);
    Fl(F + 0x00) = 0.0f;  // twiglets created (float)
    uint32_t eax = 0;
    for (uint32_t i = 0; i < 5; ++i)
    {
        Fl(F + 0x04) = static_cast<float>(static_cast<int32_t>(i)) * K(0x005bcd2c) * K(0x005bcb74);
        std::memset(&fr[0x34 / 4], 0, 0x11 * 4);
        const float s = X87Sin(Fl(F + 0x04));
        D(F + 0x5c) = 0x3f800000;
        D(F + 0x70) = 0x3f800000;
        D(F + 0x74) = 1;
        const float c = X87Cos(Fl(F + 0x04));
        Fl(F + 0x34) = c;
        Fl(F + 0x38) = -s;
        Fl(F + 0x44) = s;
        Fl(F + 0x48) = c;
        orig::fn_004e36a0(F + 0x28, F + 0x34, 1, F + 0x10);
        const float len = X87Sqrt(Fl(F + 0x18) * Fl(F + 0x18) + Fl(F + 0x14) * Fl(F + 0x14) + Fl(F + 0x10) * Fl(F + 0x10));
        if (!(len == K(kZero)))
        {
            const float inv = K(0x005bcb2c) / len;
            Fl(F + 0x10) = Fl(F + 0x10) * inv;
            Fl(F + 0x14) = Fl(F + 0x14) * inv;
            Fl(F + 0x18) = Fl(F + 0x18) * inv;
        }
        D(F + 0x24) = D(record + 0xc);
        Fl(F + 0x1c) = Fl(F + 0x10) * Fl(F + 0x08) + Fl(record + 4);
        Fl(F + 0x20) = Fl(F + 0x14) * Fl(F + 0x0c) + Fl(record + 8);
        eax = orig::fn_0046f280(D(GameP() + 0xaa4), D(F + 0x1c), D(F + 0x20));
        if (!static_cast<uint8_t>(eax))
            continue;
        const uint32_t tw = orig::fn_0045f020(0, 0x005b27b0, D(GameP() + 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa,
                                              0);  // "twiglet"
        ++I(GameP() + 0x2710);
        orig::fn_00457870(GameP() & 0xffffff00u, F + 0x1c, tw, 0, 0, 0);
        orig::fn_00455860(tw, 0x447a0000, 0x461c4000);  // 1000.0, 10000.0
        VC(tw, 0xf8, 0x20000u, 0u);
        const uint64_t r = orig::fn_005391cd();
        const int32_t v = static_cast<int32_t>((static_cast<uint32_t>(r) * 100u) & 0xffff8000u);
        eax = orig::fn_0049f380(tw, static_cast<uint32_t>(r >> 32), v < 0x78000 ? 1u : 0u, D(F + 0x00));
        Fl(F + 0x00) = Fl(F + 0x00) + K(0x005bcb2c);
    }
    return eax & 0xffffff00u;
}
static uint32_t fn_00482e80_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00482e80(ecx, a0, a1, a2);
}
ABI_ENTRY_00482e80(fn_00482e80_Entry, fn_00482e80_Abi)
HOOK(0x00482e80, "fn_00482e80", fn_00482e80_Entry, nullptr);

// 0x004830e0 (ecx = self; stack: a0..a3; ret 0x10). MonkBasketProjectile (vtable 0x005a9218) slot 6, the property
// read: the base 0x0047e0c0(self; a0..a3); "velocity_min" (50.0) and "velocity_max" (75.0) read into the original's
// own argument slots a3 / a1, "start_height" +0x588 (400.0); +0x460 = rand() * (max - min) / 32768 + min. Returns 0.
// Not executed in recorded runs.
uint32_t MonkBasketProjectile_ReadProps(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_0047e0c0(self, a0, a1, a2, a3);
    uint32_t vmin = a3, vmax = a1;
    orig::fn_004534a0(a3, u(&vmin), self, 0x0059e238, a2, 0x42480000, 0);      // "velocity_min", 50.0
    orig::fn_004534a0(a3, u(&vmax), self, 0x0059e228, a2, 0x42960000, 0);      // "velocity_max", 75.0
    orig::fn_004534a0(a3, self + 0x588, self, 0x0059e218, a2, 0x43c80000, 0);  // "start_height", 400.0
    const float r = static_cast<float>(static_cast<int32_t>(Rand()));
    Fl(self + 0x460) = r * (bf(vmax) - bf(vmin)) * K(0x005bcbc0) + bf(vmin);
    return 0;
}
static uint32_t MonkBasketProjectile_ReadProps_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return MonkBasketProjectile_ReadProps(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004830e0(MonkBasketProjectile_ReadProps_Entry, MonkBasketProjectile_ReadProps_Abi)
HOOK(0x004830e0, "MonkBasketProjectile_ReadProps", MonkBasketProjectile_ReadProps_Entry, nullptr);

// 0x00483180 (ecx = self; stack: matrix, a1; ret 8). MonkBasketProjectile slot 0x50 (+0x140), the launch (a basket
// dropped from above): vt+0x148(a1); the caller's matrix z translation (+0x2c) is raised by "start_height" +0x588 for
// Object_SetTransform (self, matrix) and lowered again; vt+0x54(+0x514); velocity (0, 0, -0x0047e360(self)) set
// (0x00458510); vt+0x154(), returned. Not executed in recorded runs.
uint32_t MonkBasketProjectile_Launch(uint32_t self, uint32_t matrix, uint32_t a1)
{
    float v[3];
    const uint32_t r = VC(self, 0x148, a1);
    Fl(matrix + 0x2c) = Fl(matrix + 0x2c) + Fl(self + 0x588);
    orig::fn_00457990(r & 0xffffff00u, self, matrix);
    Fl(matrix + 0x2c) = Fl(matrix + 0x2c) - Fl(self + 0x588);
    VC(self, 0x54, self + 0x514);
    v[0] = 0.0f;
    v[1] = 0.0f;
    v[2] = -orig::fn_0047e360(self);
    orig::fn_00458510(u(v), self);
    return VC(self, 0x154);
}
static uint32_t MonkBasketProjectile_Launch_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return MonkBasketProjectile_Launch(ecx, a0, a1);
}
ABI_ENTRY_00483180(MonkBasketProjectile_Launch_Entry, MonkBasketProjectile_Launch_Abi)
HOOK(0x00483180, "MonkBasketProjectile_Launch", MonkBasketProjectile_Launch_Entry, nullptr);

// 0x00483210 (ecx = self). Slot 0x2f (update) of GurnerProjectile (0x005a4c38) and the Projectile vtable 0x005adc78:
// 0x0047e580(self); when vt+0xd8(), 0x00455e70(0, 0). Executed in recorded runs.
uint32_t Projectile_Update(uint32_t self)
{
    orig::fn_0047e580(self);
    const uint32_t r = VC(self, 0xd8);
    if (!static_cast<uint8_t>(r))
        return r;
    return orig::fn_00455e70(self, 0, 0);
}
static uint32_t Projectile_Update_Abi(uint32_t ecx)
{
    return Projectile_Update(ecx);
}
ABI_ENTRY_00483210(Projectile_Update_Entry, Projectile_Update_Abi)
HOOK(0x00483210, "Projectile_Update", Projectile_Update_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00487da0 (eax = object; callers: the inlined reference drop everywhere). Releases an object whose reference
// count reached 0: ObjectList_Link into (game+0xa84)+0xe6c (guess: the pending-delete list), and when the registry
// (game+0xa84)+0xf28 vt+0x1c(object) knows it, vt+0x28(object) removes it. Returns eax as left (the last call's
// result, or vt+0x1c's 0). Executed in recorded runs.
uint32_t Object_Release(uint32_t object)
{
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe6c), object);
    const uint32_t r = VC(D(D(GameP() + 0xa84) + 0xf28), 0x1c, object);
    if (!r)
        return r;
    return VC(D(D(GameP() + 0xa84) + 0xf28), 0x28, object);
}
static uint32_t Object_Release_Abi(uint32_t eax)
{
    return Object_Release(eax);
}
ABI_ENTRY_00487da0(Object_Release_Entry, Object_Release_Abi)
HOOK(0x00487da0, "Object_Release", Object_Release_Entry, nullptr);

// 0x00487df0 (esi = object; callers 0x004512b0, 0x0048d250): when the registry (game+0xa84)+0xf28 vt+0x1c(object)
// knows the object, vt+0x28(object); then the reference count +0x28 = 0. Returns eax as left. Executed in recorded
// runs.
uint32_t fn_00487df0(uint32_t object)
{
    uint32_t r = VC(D(D(GameP() + 0xa84) + 0xf28), 0x1c, object);
    if (r)
        r = VC(D(D(GameP() + 0xa84) + 0xf28), 0x28, object);
    D(object + 0x28) = 0;
    return r;
}
static uint32_t fn_00487df0_Abi(uint32_t esi)
{
    return fn_00487df0(esi);
}
ABI_ENTRY_00487df0(fn_00487df0_Entry, fn_00487df0_Abi)
HOOK(0x00487df0, "fn_00487df0", fn_00487df0_Entry, nullptr);

// Spatial grid nodes (0x1c bytes, inlined in 0x0048ece0 / 0x0048ed90 / 0x0048f040, allocated by 0x0048f0d0): +0 =
// address of the previous cell-list link, +4 = next in the cell list, +8 = address of the previous object-chain link,
// +0xc = next in the object's chain (head at object+0x40c), +0x10 = object, +0x14 = cell index, +0x18 = flag mask.
// Free nodes are kept in the cell-list links of the free list 0x0063a16c.
namespace
{
void GridNodeFree(uint32_t node)
{
    // Out of its cell list, onto the free list.
    D(D(node)) = D(node + 4);
    if (D(node + 4))
        D(D(node + 4)) = D(node);
    D(node) = 0x0063a16c;
    D(node + 4) = D(0x0063a16c);
    D(0x0063a16c) = node;
    if (D(node + 4))
        D(D(node + 4)) = node + 4;
    // Out of the object's chain.
    D(D(node + 8)) = D(node + 0xc);
    if (D(node + 0xc))
        D(D(node + 0xc) + 8) = D(node + 8);
}
}  // namespace

uint32_t GridNode_Alloc(uint32_t cellHead, uint32_t objLink, uint32_t cell, uint32_t mask, uint32_t object);

// 0x0048ece0 (eax = flags, ecx = link (an address in the object's node chain); stack: grid, object, cell; ret 0xc;
// caller 0x0048ed90). Walks the object's chain from link: nodes of earlier cells, and nodes of this cell whose mask
// shares no bit with flags, are freed; a node of this cell sharing bits keeps them out of flags and the walk moves
// past it; it stops at a later cell. When flags bit 0 is still set, a new node (GridNode_Alloc 0x0048f0d0: cell list
// grid+0x38 + cell * 8 + 4, mask 1) is inserted at the walk position and its chain link (+0xc) returned; otherwise
// the walk position. Executed in recorded runs.
uint32_t Grid_UpdateCell(uint32_t flags, uint32_t link, uint32_t grid, uint32_t object, uint32_t cell)
{
    for (uint32_t node = D(link); node; node = D(link))
    {
        const uint32_t c = D(node + 0x14);
        if (c > cell)
            break;
        if (c == cell && (flags & D(node + 0x18)))
        {
            flags &= ~D(node + 0x18);
            link = node + 0xc;
            continue;
        }
        GridNodeFree(node);
    }
    if (!(flags & 1))
        return link;
    return GridNode_Alloc(D(grid + 0x38) + cell * 8 + 4, link, cell, 1, object) + 0xc;
}
static uint32_t Grid_UpdateCell_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Grid_UpdateCell(eax, ecx, a0, a1, a2);
}
ABI_ENTRY_0048ece0(Grid_UpdateCell_Entry, Grid_UpdateCell_Abi)
HOOK(0x0048ece0, "Grid_UpdateCell", Grid_UpdateCell_Entry, nullptr);

// 0x0048ed90 (ebx = grid; caller 0x00402e80). Re-files every object of the grid's list +0x3c (when the grid has
// cells, +0x38): its bounds (vt+0x90(&centre, &radius, 1)) relative to the origin grid+0/+4, scaled by +0x1c and
// truncated (_ftol2) give the cell rectangle x0..x1, y0..y1, clamped to 0..+0x20-1 / 0..+0x24-1 ("clipped" when
// clamped, or when object+0x424 bit 15 is set and nothing was clamped). The key ((((flags +0x400 << 10) ^ end) << 10)
// ^ clipped << 31) ^ start is compared with +0x408; when it changed, the cells of the rectangle (row by row) are
// updated through Grid_UpdateCell 0x0048ece0 (when flags is non-zero), clipped objects also get the overflow cell
// +0x30, the rest of the chain is freed and +0x408 = key. An object with flags 0, or none of whose parents (from
// itself, Object_GetParent 0x004539d0) passes 0x004531e0, leaves the list (ObjectList_Unlink). Executed in recorded
// runs.
uint32_t Grid_Update(uint32_t grid)
{
    uint32_t iter[2] = {0, 0};
    if (D(grid + 0x38) == 0)
        return 0;
    for (uint32_t obj = orig::fn_004e7340(D(grid + 0x3c), u(iter)); obj; obj = orig::fn_004e73c0(u(iter)))
    {
        float centre[4];  // the original's frame has room for four floats
        float radius;
        VC(obj, 0x90, u(centre), u(&radius), 1u);
        centre[0] = centre[0] - Fl(grid);
        centre[1] = centre[1] - Fl(grid + 4);
        int32_t x0 = static_cast<int32_t>((centre[0] - radius) * Fl(grid + 0x1c));
        int32_t y0 = static_cast<int32_t>((centre[1] - radius) * Fl(grid + 0x1c));
        int32_t x1 = static_cast<int32_t>((centre[0] + radius) * Fl(grid + 0x1c));
        int32_t y1 = static_cast<int32_t>((centre[1] + radius) * Fl(grid + 0x1c));
        const int32_t w = I(grid + 0x20);
        const int32_t h = I(grid + 0x24);
        bool clipped = false;
        if (x0 < 0)
        {
            x0 = 0;
            if (x1 < 0)
                x1 = 0;
            clipped = true;
        }
        if (y0 < 0)
        {
            y0 = 0;
            if (y1 < 0)
                y1 = 0;
            clipped = true;
        }
        if (x1 >= w)
        {
            x1 = w - 1;
            if (x0 >= w)
                x0 = x1;
            clipped = true;
        }
        if (y1 >= h)
        {
            y1 = h - 1;
            if (y0 >= h)
                y0 = y1;
            clipped = true;
        }
        else if (!clipped)
        {
            clipped = (D(obj + 0x424) & 0x8000) != 0;
        }
        const uint32_t end = static_cast<uint32_t>(w * y1 + x1);
        uint32_t rowStart = static_cast<uint32_t>(x0 + w * y0);
        uint32_t rowEnd = static_cast<uint32_t>(w * y0 + x1);
        const uint32_t flags = D(obj + 0x400);
        const uint32_t key = ((((flags << 10) ^ end) << 10) ^ (clipped ? 0x80000000u : 0u)) ^ rowStart;
        if (key != D(obj + 0x408))
        {
            uint32_t link = obj + 0x40c;
            if (flags)
            {
                for (; rowStart <= end; rowStart += static_cast<uint32_t>(I(grid + 0x20)),
                                        rowEnd += static_cast<uint32_t>(I(grid + 0x20)))
                    for (uint32_t c = rowStart; c <= rowEnd; ++c)
                        link = Grid_UpdateCell(D(obj + 0x400), link, grid, obj, c);
                if (clipped)
                    link = Grid_UpdateCell(D(obj + 0x400), link, grid, obj, D(grid + 0x30));
            }
            for (uint32_t node = D(link); node; node = D(link))
                GridNodeFree(node);
            D(obj + 0x408) = key;
        }
        bool keep = false;
        if (D(obj + 0x400))
            for (uint32_t o = obj; o; o = orig::fn_004539d0(o))
                if (static_cast<uint8_t>(orig::fn_004531e0(o)))
                {
                    keep = true;
                    break;
                }
        if (!keep)
            orig::fn_00454750(D(grid + 0x3c), obj);
    }
    if (iter[1])
        --I(iter[1] + 0x18);
    return iter[1];
}
static uint32_t Grid_Update_Abi(uint32_t ebx)
{
    return Grid_Update(ebx);
}
ABI_ENTRY_0048ed90(Grid_Update_Entry, Grid_Update_Abi)
HOOK(0x0048ed90, "Grid_Update", Grid_Update_Entry, nullptr);

// 0x0048f040 (eax = object, ebx = grid; caller Object_Remove 0x004547b0): takes the object out of the grid: out of
// list grid+0x3c when ObjectList_Contains 0x00454690 finds it, every node of its chain (+0x40c) freed, and
// +0x400..+0x40c = 0. Returns 0. Executed in recorded runs.
uint32_t Grid_RemoveObject(uint32_t object, uint32_t grid)
{
    if (static_cast<uint8_t>(orig::fn_00454690(D(grid + 0x3c), object)))
        orig::fn_00454750(D(grid + 0x3c), object);
    for (uint32_t node = D(object + 0x40c); node; node = D(object + 0x40c))
        GridNodeFree(node);
    D(object + 0x400) = 0;
    D(object + 0x404) = 0;
    D(object + 0x408) = 0;
    D(object + 0x40c) = 0;
    return 0;
}
static uint32_t Grid_RemoveObject_Abi(uint32_t eax, uint32_t ebx)
{
    return Grid_RemoveObject(eax, ebx);
}
ABI_ENTRY_0048f040(Grid_RemoveObject_Entry, Grid_RemoveObject_Abi)
HOOK(0x0048f040, "Grid_RemoveObject", Grid_RemoveObject_Entry, nullptr);

// 0x0048f0d0 (esi = cell list head, edi = object chain link; stack: cell, mask, object; caller pops; caller
// 0x0048ece0): takes a node from the free list 0x0063a16c (refilled by 0x0048e770, ebx = 0x80, when empty), sets
// +0x14 = cell, +0x10 = object, +0x18 = mask, unlinks it from the free list and pushes it at the front of the cell
// list and at link in the object's chain. Returns the node. Executed in recorded runs.
uint32_t GridNode_Alloc(uint32_t cellHead, uint32_t objLink, uint32_t cell, uint32_t mask, uint32_t object)
{
    uint32_t node = D(0x0063a16c);
    if (!node)
        node = orig::fn_0048e770(0x80);
    D(node + 0x14) = cell;
    D(node + 0x10) = object;
    D(node + 0x18) = mask;
    D(D(node)) = D(node + 4);
    if (D(node + 4))
        D(D(node + 4)) = D(node);
    D(node) = cellHead;
    D(node + 4) = D(cellHead);
    D(cellHead) = node;
    if (D(node + 4))
        D(D(node + 4)) = node + 4;
    D(node + 8) = objLink;
    D(node + 0xc) = D(objLink);
    D(objLink) = node;
    if (D(node + 0xc))
        D(D(node + 0xc) + 8) = node + 0xc;
    return node;
}
static uint32_t GridNode_Alloc_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return GridNode_Alloc(esi, edi, a0, a1, a2);
}
ABI_ENTRY_0048f0d0(GridNode_Alloc_Entry, GridNode_Alloc_Abi)
HOOK(0x0048f0d0, "GridNode_Alloc", GridNode_Alloc_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Penguin / sheep (PenguinObject vtable 0x005a1768 and SheepObject vtable 0x005a8d00 share these methods). State
// +0xb58: 1 idle (physics off, timer +0xb5c), 2 wandering (physics on), 3 hit, 4/5 picked up / thrown (guess), 8/9
// special hits; the rest run GenericAI_Update.

// 0x0048f140 (ecx = self; stack: a0..a3; ret 0x10). Slot 6, the property read: Object_LoadProperties 0x004517b0(self;
// a0..a3); 0x00458270 (edx = self; 0, 0, 20000 / body mass (0 for a massless body)); "generic_txt" +0x4cc (default
// "none"); when found it is copied to +0xab8 and byte +0xb60 = 0x0044e600() (guess: stamps enabled), then
// vt+0x238(+0x4cc, 0); otherwise +0xb60 = 0. "move_speed" +0x948 (35), "move_power" +0x944 (30), "hit_ragdoll" +0xa0c
// (50), "bof_anti_gravity" +0x888 (0.5), "score_value" +0x47c (0). Linked into (game+0xa84) lists +0xe90, +0xe94,
// +0xe9c, +0xe8c; +0x604 = list +0xe7c, +0x608 = list +0xe88; 0x004559c0 (eax = self, cl = 0). Starts idle (one in
// ~5.3 when +0xb60: physics off, timer rand() * 1.526e-4 + 5, vt+0x19c(8, 1, 0, 0, 0, 0), state 1) or wandering
// (physics on, timer rand() * 4.578e-4 + 20, vt+0x19c(0x17, 1, ...), state 2). With +0xb60 a "stamp" is generated:
// the texture cache +0x410's counter +8 is incremented (0 becomes 1; no cache: the log "Error: Can't generate stamp
// code, no cache"), the named value +0xab8 is looked up in +0xa1c's table, 0x004ec410(value, ftol((+0x20 * +0x1c) *
// +0xa20 + +0x14), cache, code, 0, 0), Matrix_Multiply 0x004e2910 (eax = its result; +0xa6c, +0xa28), the transform set
// from +0xa6c and the object grounded (Object_ForceGround 1000, 10000). Not executed in recorded runs.
uint32_t PenguinObject_ReadProps(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004517b0(self, a0, a1, a2, a3);
    const float mass = Fl(D(D(self + 0x1c4) + 8) + 0x2c);
    const float inv = mass == K(kZero) ? K(kZero) : K(0x005bcb2c) / mass;
    orig::fn_00458270(self, 0, 0, fb(inv * K(0x005bcd34)));
    const uint32_t found = orig::fn_004533a0(0x005bb9e0, self + 0x4cc, self, 0x005b6dac, a2, a3, 0);  // "generic_txt"
    if (found)
    {
        uint32_t i = 0;
        char c;
        do
        {
            c = static_cast<char>(By(self + 0x4cc + i));
            By(self + 0xab8 + i) = static_cast<uint8_t>(c);
            ++i;
        } while (c);
        By(self + 0xb60) = static_cast<uint8_t>(orig::fn_0044e600()) ? 1 : 0;
        VC(self, 0x238, self + 0x4cc, 0u);
    }
    else
    {
        By(self + 0xb60) = 0;
    }
    orig::fn_004534a0(a3, self + 0x948, self, 0x005b6fd8, a2, 0x420c0000, 0);  // "move_speed", 35.0
    orig::fn_004534a0(a3, self + 0x944, self, 0x005b6fe4, a2, 0x41f00000, 0);  // "move_power", 30.0
    orig::fn_004534a0(a3, self + 0xa0c, self, 0x005b6ffc, a2, 0x42480000, 0);  // "hit_ragdoll", 50.0
    orig::fn_004534a0(a3, self + 0x888, self, 0x005b7578, a2, 0x3f000000, 0);  // "bof_anti_gravity", 0.5
    orig::fn_004534a0(a3, self + 0x47c, self, 0x005bb864, a2, 0, 0);           // "score_value", 0
    const uint32_t lists = D(GameP() + 0xa84);
    orig::fn_004546d0(D(lists + 0xe90), self);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe94), self);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe9c), self);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe8c), self);
    const uint32_t e7c = D(D(GameP() + 0xa84) + 0xe7c);
    D(self + 0x604) = e7c;
    D(self + 0x608) = D(D(GameP() + 0xa84) + 0xe88);
    orig::fn_004559c0(self, e7c & 0xffffff00u);
    if (By(self + 0xb60) && static_cast<int32_t>((Rand() * 100u) & 0xffff8000u) < 0x190000)
    {
        orig::fn_004577b0(self, 0);
        Fl(self + 0xb5c) = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bcdd8) + K(0x005bcbdc);
        VC(self, 0x19c, 8u, 1u, 0u, 0u, 0u, 0u);
        D(self + 0xb58) = 1;
    }
    else
    {
        orig::fn_004577b0(self, 1);
        Fl(self + 0xb5c) = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bcdd4) + K(0x005bcbe8);
        VC(self, 0x19c, 0x17u, 1u, 0u, 0u, 0u, 0u);
        D(self + 0xb58) = 2;
    }
    if (!By(self + 0xb60))
        return 0;
    uint32_t code;
    const uint32_t cache = D(self + 0x410);
    if (cache)
    {
        if (++D(cache + 8) == 0)
            D(cache + 8) = 1;
        code = D(cache + 8);
    }
    else
    {
        GameLog(reinterpret_cast<const char*>(0x0058af8c));  // "Error: Can't generate stamp code, no cache\n"
        code = 0;
    }
    uint32_t value = 0;
    {
        const uint32_t p = D(D(D(self + 0xa1c) + 0x2c) + 4);
        const uint32_t first = D(p + 0x1c);
        if (first && D(p + 0x20))
        {
            const uint32_t finder = D(p + 0x24);
            D(finder) = first;
            D(finder + 0x18) = self + 0xab8;
            orig::fn_004e0f40(finder);
            if (D(finder + 0xc) == 0)
            {
                const uint32_t entry = D(D(finder + 0x10) + D(finder + 8) * 4);
                if (entry)
                    value = D(entry + W(entry + 2));
            }
        }
    }
    const uint32_t a = D(self + 0xa1c);
    const float prod = static_cast<float>(I(a + 0x20)) * static_cast<float>(I(a + 0x1c));
    const int32_t n = static_cast<int32_t>(prod * Fl(self + 0xa20) + static_cast<float>(I(a + 0x14)));
    const uint32_t m = orig::fn_004ec410(value, static_cast<uint32_t>(n), D(self + 0x410), code, 0, 0);
    orig::fn_004e2910(m, self + 0xa6c, self + 0xa28);
    orig::fn_00457990(m & 0xffffff00u, self, self + 0xa6c);
    return orig::fn_00455860(self, 0x447a0000, 0x461c4000);  // 1000.0, 10000.0
}
static uint32_t PenguinObject_ReadProps_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PenguinObject_ReadProps(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0048f140(PenguinObject_ReadProps_Entry, PenguinObject_ReadProps_Abi)
HOOK(0x0048f140, "PenguinObject_ReadProps", PenguinObject_ReadProps_Entry, nullptr);

// 0x0048f4c0 (ebx = self; caller 0x0048f690). Picks the nearest object of list (game+0xa84)+0xe7c (not self) whose
// vt+0x10c() and vt+0x258() are true, 0x00454c20() false and 0x00454be0() true, within sqrt(250000) of self, and
// visible: a ray (Ray_CastQuery 0x00414110, world game+0xa8c) from self + (0, 0, 3) towards it + (0, 0, 3) that hits
// nothing, or hits that object (fraction and hit object in locals). The winner goes to vt+0x23c(object, 0) (guess:
// target). Returns eax as left (the iterator node). Not executed in recorded runs.
uint32_t fn_0048f4c0(uint32_t self)
{
    uint32_t fr[0x40 / 4];  // the original frame (sub esp, 0x40)
    std::memset(fr, 0, sizeof(fr));
    const uint32_t F = u(fr);
    D(F + 0x14) = 0;  // iterator {0, 0}
    D(F + 0x18) = 0;
    D(F + 0x0c) = 0;           // best object
    D(F + 0x08) = 0x48742400;  // best squared distance, 250000.0
    VC(self, 0x54, F + 0x1c);
    Fl(F + 0x24) = Fl(F + 0x24) + K(0x005bcb7c);
    for (uint32_t obj = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xe7c), F + 0x14); obj;
         obj = orig::fn_004e73c0(F + 0x14))
    {
        if (obj == self || !VB(obj, 0x10c) || !VB(obj, 0x258) || static_cast<uint8_t>(orig::fn_00454c20(obj)) ||
            !static_cast<uint8_t>(orig::fn_00454be0(obj)))
            continue;
        VC(obj, 0x54, F + 0x28);
        const float dy = Fl(F + 0x2c) - Fl(F + 0x20);
        const float dx = Fl(F + 0x28) - Fl(F + 0x1c);
        Fl(F + 0x00) = dx;
        const float dz = Fl(F + 0x30) - Fl(F + 0x24);
        Fl(F + 0x04) = dz * dz + dx * dx + dy * dy;
        if (Fl(F + 0x04) > Fl(F + 0x08))
            continue;
        Fl(F + 0x30) = Fl(F + 0x30) + K(0x005bcb7c);
        D(F + 0x34) = D(F + 0x00);
        Fl(F + 0x38) = dy;
        Fl(F + 0x3c) = Fl(F + 0x30) - Fl(F + 0x24);
        D(F + 0x00) = 0;
        const uint32_t lists = D(GameP() + 0xa84);
        const uint8_t hit = static_cast<uint8_t>(orig::fn_00414110(F + 0x1c, F + 0x34, D(GameP() + 0xa8c), 0x3f800000,
                                                                   F + 0x10, 0, F + 0x00, 0, D(lists + 0xe7c), 0,
                                                                   D(lists + 0xe9c), 0, 0, 0, 2));
        if (!hit || D(F + 0x00) == 0 || D(F + 0x00) == obj)
        {
            D(F + 0x08) = D(F + 0x04);
            D(F + 0x0c) = obj;
        }
    }
    if (D(F + 0x0c))
        VC(self, 0x23c, D(F + 0x0c), 0u);
    const uint32_t node = D(F + 0x18);
    if (node)
        --I(node + 0x18);
    return node;
}
static uint32_t fn_0048f4c0_Abi(uint32_t ebx)
{
    return fn_0048f4c0(ebx);
}
ABI_ENTRY_0048f4c0(fn_0048f4c0_Entry, fn_0048f4c0_Abi)
HOOK(0x0048f4c0, "fn_0048f4c0", fn_0048f4c0_Entry, nullptr);

uint32_t fn_00481790(uint32_t object, uint32_t value);

// 0x0048f690 (ecx = self). Slot 0x2f, the update: a switch on the state +0xb58 (jump table 0x0048f8bc).
//   1: the timer +0xb5c counts down; while above 0 a tail jump to GenericAI_Update 0x0043ee90; then physics on,
//      vt+0x19c(0x17, 1, 0, 0, 0, 0), timer = 0x0046e2c0(20, 35) (a random float, st0), state 2.
//   2: the timer counts down; only when the animation slot 4 (0x00408800) has flag +0x1c bit 1: below 0 with
//      +0xb60 set the critter goes idle (physics off, timer = random(5, 10), vt+0x19c(8, 1, ...), state 1); below 0
//      otherwise, and at or above 0, it picks an animation by r = (rand() * 100) >> 15: 0x18 below 33, 0x17 below
//      66, else 0x19 (vt+0x19c(n, 1, 0, 0, 0, 0)), and below 0 the timer is reset to random(20, 35).
//   5: when the animation flag is set: physics off, fn_0048f4c0; with +0x588 0x0042cf40 (eax = +0x588, esi = self),
//      else fn_00481790(+0xb54, random(3, 6)) and 0x0042fb60(self; +0xb54, 0, 1); state 4.
//   3, 4, 6..9: a tail jump to GenericAI_Update. Other values: nothing.
// Executed in recorded runs.
uint32_t PenguinObject_Update(uint32_t self)
{
    switch (D(self + 0xb58))
    {
    case 1:
    {
        const float t = Fl(self + 0xb5c) - Dt();
        Fl(self + 0xb5c) = t;
        if (t > K(kZero))
            return orig::fn_0043ee90(self);
        orig::fn_004577b0(self, 1);
        VC(self, 0x19c, 0x17u, 1u, 0u, 0u, 0u, 0u);
        Fl(self + 0xb5c) = orig::fn_0046e2c0(0, 0x41a00000, 0x420c0000);  // random(20, 35)
        D(self + 0xb58) = 2;
        return 2;
    }
    case 2:
    {
        const float t = Fl(self + 0xb5c) - Dt();
        Fl(self + 0xb5c) = t;
        const uint32_t a = orig::fn_00408800(4, self);
        if (!(By(a + 0x1c) & 2))
            return a;
        if (t < K(kZero))
        {
            if (By(self + 0xb60))
            {
                orig::fn_004577b0(self, 0);
                Fl(self + 0xb5c) = orig::fn_0046e2c0(0, 0x40a00000, 0x41200000);  // random(5, 10)
                VC(self, 0x19c, 8u, 1u, 0u, 0u, 0u, 0u);
                D(self + 0xb58) = 1;
                return 1;
            }
            const uint32_t r = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 100u) >> 15);
            const uint32_t anim = r < 0x21 ? 0x18u : (r < 0x42 ? 0x17u : 0x19u);
            VC(self, 0x19c, anim, 1u, 0u, 0u, 0u, 0u);
            Fl(self + 0xb5c) = orig::fn_0046e2c0(0, 0x41a00000, 0x420c0000);  // random(20, 35)
            return 0;
        }
        const uint32_t r = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 100u) >> 15);
        const uint32_t anim = r < 0x21 ? 0x18u : (r < 0x42 ? 0x17u : 0x19u);
        return VC(self, 0x19c, anim, 1u, 0u, 0u, 0u, 0u);
    }
    case 5:
    {
        const uint32_t a = orig::fn_00408800(4, self);
        if (!(By(a + 0x1c) & 2))
            return a;
        orig::fn_004577b0(self, 0);
        fn_0048f4c0(self);
        if (D(self + 0x588))
        {
            orig::fn_0042cf40(D(self + 0x588), self);
            D(self + 0xb58) = 4;
            return 4;
        }
        const float v = orig::fn_0046e2c0(0, 0x40400000, 0x40c00000);  // random(3, 6)
        fn_00481790(D(self + 0xb54), fb(v));
        orig::fn_0042fb60(self, D(self + 0xb54), 0, 1);
        D(self + 0xb58) = 4;
        return 4;
    }
    case 3:
    case 4:
    case 6:
    case 7:
    case 8:
    case 9:
        return orig::fn_0043ee90(self);
    default:
        return D(self + 0xb58) - 1;
    }
}
static uint32_t PenguinObject_Update_Abi(uint32_t ecx)
{
    return PenguinObject_Update(ecx);
}
ABI_ENTRY_0048f690(PenguinObject_Update_Entry, PenguinObject_Update_Abi)
HOOK(0x0048f690, "PenguinObject_Update", PenguinObject_Update_Entry, nullptr);

// 0x0048f8e0 (ecx = self). Slot 0x47 (+0x11c): in state 6 the critter restarts like at load: one in ~5.3 idle
// (physics off, timer rand() * 1.526e-4 + 5, vt+0x19c(8, 1, 0, 0, 0, 0), state 1), else wandering (physics on, timer
// rand() * 4.578e-4 + 20, vt+0x19c(0x17, 1, ...), state 2). Not executed in recorded runs.
uint32_t PenguinObject_Restart(uint32_t self)
{
    if (D(self + 0xb58) != 6)
        return 0;
    if (static_cast<int32_t>((Rand() * 100u) & 0xffff8000u) < 0x190000)
    {
        orig::fn_004577b0(self, 0);
        Fl(self + 0xb5c) = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bcdd8) + K(0x005bcbdc);
        VC(self, 0x19c, 8u, 1u, 0u, 0u, 0u, 0u);
        D(self + 0xb58) = 1;
        return 1;
    }
    orig::fn_004577b0(self, 1);
    Fl(self + 0xb5c) = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bcdd4) + K(0x005bcbe8);
    VC(self, 0x19c, 0x17u, 1u, 0u, 0u, 0u, 0u);
    D(self + 0xb58) = 2;
    return 2;
}
static uint32_t PenguinObject_Restart_Abi(uint32_t ecx)
{
    return PenguinObject_Restart(ecx);
}
ABI_ENTRY_0048f8e0(PenguinObject_Restart_Entry, PenguinObject_Restart_Abi)
HOOK(0x0048f8e0, "PenguinObject_Restart", PenguinObject_Restart_Entry, nullptr);

// 0x0048f9c0 (ecx = self; stack: object, a1, a2 (unused); ret 0xc). Slot 0x33 (+0xcc): when +0xb54 is empty and
// object is set: +0xb54 = object (referenced), fn_00481790(object, 300.0), vt+0x19c(0xd, 0, 0, 0, 0, 0), state 5.
// Not executed in recorded runs.
uint32_t PenguinObject_Grab(uint32_t self, uint32_t object, uint32_t /*a1*/, uint32_t /*a2*/)
{
    if (D(self + 0xb54) || !object)
        return D(self + 0xb54) ? D(self + 0xb54) : object;
    D(self + 0xb54) = object;
    ++I(object + 0x28);
    fn_00481790(D(self + 0xb54), 0x43960000);  // 300.0
    VC(self, 0x19c, 0xdu, 0u, 0u, 0u, 0u, 0u);
    D(self + 0xb58) = 5;
    return 5;
}
static uint32_t PenguinObject_Grab_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return PenguinObject_Grab(ecx, a0, a1, a2);
}
ABI_ENTRY_0048f9c0(PenguinObject_Grab_Entry, PenguinObject_Grab_Abi)
HOOK(0x0048f9c0, "PenguinObject_Grab", PenguinObject_Grab_Entry, nullptr);

// 0x0048fa20 (ecx = self; stack: damage record; ret 4). Slot 0x34 (damage). Nothing when dead (Object_IsDead). With a
// record: flag bit 0x20 and vt+0x1c0() -> state 9, flag bit 0x800 and vt+0x254() -> state 8; both: physics off and
// 0x00430340(self; record). Otherwise (or without a record): health +0x454 = 0, physics off, state 3,
// vt+0x17c(record, 0). Executed in recorded runs.
uint32_t PenguinObject_Damage(uint32_t self, uint32_t record)
{
    const uint32_t dead = orig::fn_00454780(self);
    if (static_cast<uint8_t>(dead))
        return dead;
    if (record)
    {
        uint32_t state = 0;
        if ((By(record) & 0x20) && VB(self, 0x1c0))
            state = 9;
        else if ((D(record) & 0x800) && VB(self, 0x254))
            state = 8;
        if (state)
        {
            D(self + 0xb58) = state;
            orig::fn_004577b0(self, 0);
            return orig::fn_00430340(self, record);
        }
    }
    D(self + 0x454) = 0;
    orig::fn_004577b0(self, 0);
    D(self + 0xb58) = 3;
    return VC(self, 0x17c, record, 0u);
}
static uint32_t PenguinObject_Damage_Abi(uint32_t ecx, uint32_t a0)
{
    return PenguinObject_Damage(ecx, a0);
}
ABI_ENTRY_0048fa20(PenguinObject_Damage_Entry, PenguinObject_Damage_Abi)
HOOK(0x0048fa20, "PenguinObject_Damage", PenguinObject_Damage_Entry, nullptr);
