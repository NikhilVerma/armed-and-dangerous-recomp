// Recovered: cleanup batch cleanup_01 (handover-recovery.md section 5, "Cleanup list"). Static recovery 2026-10-05,
// compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// 0x0043e710: skipped by game_02 because symbols/abi.csv lists an EBP input. The input is spurious: 0x0043e710 never
//   touches ebp, and its callee 0x00475a90 only saves it (`push ebp` at 0x00475ae5, before its first write
//   `or ebp, -1` at 0x00475af7; no read of ebp on the path before). The generated ABI_ENTRY_0043e710 still passes ebp
//   to the adapter, which ignores it.
// 0x00447040 (the rest of the tracker range of 0x00447020): verified a real entry (GoliathObject vtable 0x005af298
//   slot +0xbc = 0x00447040 at .rdata 0x005af354, its only reference; slot +0xf0 = 0x00447020, 14 bytes then int3/nop
//   padding). It is already recovered in src/ai/ai_07.cpp (GoliathObject_Update, ABI_ENTRY_00447040 exists in
//   src/gen/entry_stubs.h), so it is not repeated here (a second HOOK would clash). Only the tracker row is missing.
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
inline uint32_t& D(uint32_t a)
{
    return *reinterpret_cast<uint32_t*>(a);
}
inline float& Fl(uint32_t a)
{
    return *reinterpret_cast<float*>(a);
}
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
template <class... A>
inline uint32_t VC(uint32_t object, uint32_t off, A... args)
{
    return VCall<uint32_t>(P(object), static_cast<int>(off / 4), args...);
}
inline uint32_t GameP()
{
    return D(0x005fe490);
}
// The CRT's _CIpow 0x00566790 (x87 register convention: st1 = base, st0 = exponent; both popped, result in st0).
// The original loads base (fld) then exponent (fld) and calls it; this shim does the same and pops the result.
float CIpow(float base, float exponent)
{
    static const uint32_t target = 0x00566790;
    float r;
    __asm {
        push ebx
        push esi
        push edi
        fld base
        fld exponent
        call dword ptr [target]
        fstp r
        pop edi
        pop esi
        pop ebx
    }
    return r;
}
// Calls a vector normaliser (ecx = source, edx = destination) that returns its length in st0 (0x004e7780 2D,
// 0x004e7880 3D; the ABI table lists eax for the latter). The original pops st0 (fstp st(0)); so does this shim.
float NormalizeSt0(uint32_t target, uint32_t ecx_, uint32_t edx_)
{
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, ecx_
        mov edx, edx_
        mov eax, target
        call eax
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp r
    }
    return r;
}
}  // namespace

// 0x0043e710 (eax = character; abi.csv's ebp input is spurious, see the header; caller pops nothing; size 490).
// Callers: Character_Die 0x004319e0, 0x00430340, GenericAI_Update 0x0043ee90, Q112Boss_Update 0x00447400.
// Drops a pickup when a character dies (name guess from the evidence: strings "pickup_health_1"/"pickup_health_2",
// ObjectSystem_CreateObject, launch towards the player): nothing during a cutscene (0x0044e600). Chance test:
// rand() * K(0x005bcbc0) must be below 1 - pow(1 - character+0xb30, Game+0xc60 * Game+0x560) (_CIpow 0x00566790).
// The pickup name comes from character+0xb2c: -1 = 0x00475a90 (picks one for the player Game+0x748; 0 = none),
// 0..0x1b = Game + 0x2890 + i * 0x150, -2 = "pickup_health_1", -3 = "pickup_health_2", anything else = none. The object
// is created (CreateObject(0, name, Game+0xa84, 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0)), placed at the character's position
// (vt+0x54) with [2] += K(0x005bcbe0), launched along the 2D direction to the player with [2] = 0.7 renormalised in
// 3D, times K(0x005bcb30) (Body_setLinearVelocity 0x00458510), and linked (0x00472080, ebx) to character+0x880 or,
// when that is 0, character+0xadc.
// Returns the original's eax where cheap (the callers ignore it).
uint32_t Character_DropPickup(uint32_t edi)
{
    // The original frame (sub esp, 0x30): L+0 rand int, L+4 base, L+8 exponent / pow result, L+0xc direction,
    // L+0x18 own position, L+0x24 player position.
    float L[0x30 / 4] = {};
    const uint32_t cut = orig::fn_0044e600();
    if (static_cast<uint8_t>(cut))
        return cut;
    const uint32_t g = GameP();
    L[8 / 4] = Fl(g + 0xc60) * Fl(g + 0x560);
    L[4 / 4] = K(0x005bcb2c) - Fl(edi + 0xb30);
    const int32_t rnd = static_cast<int32_t>(orig::fn_005391cd());
    const float r = static_cast<float>(rnd) * K(0x005bcbc0);
    const float p = CIpow(L[4 / 4], L[8 / 4]);
    L[8 / 4] = p;
    // fsubr 1.0; fcompp; test ah, 0x41; jnp: returns when (1 - p) < r or == r (unordered goes on).
    if (K(0x005bcb2c) - p <= r)
        return 0;  // eax = fnstsw (not reproduced)
    const int32_t sel = static_cast<int32_t>(D(edi + 0xb2c));
    uint32_t name;
    if (sel == -1)
    {
        name = orig::fn_00475a90(D(GameP() + 0x748), 0 /* ebp: saved only */, edi);
        if (name == 0)
            return 0;
    }
    else if (static_cast<uint32_t>(sel) < 0x1c)
    {
        name = static_cast<uint32_t>(sel) * 0x150 + GameP() + 0x2890;
        if (name == 0)
            return static_cast<uint32_t>(sel) * 0x150;
    }
    else if (sel == -2)
        name = 0x005b6e74;  // "pickup_health_1"
    else if (sel == -3)
        name = 0x005b6e64;  // "pickup_health_2"
    else
        return static_cast<uint32_t>(sel);
    const uint32_t obj = orig::fn_0045f020(0, name, D(GameP() + 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
    if (!obj)
        return 0;
    float* own = &L[0x18 / 4];
    float* pl = &L[0x24 / 4];
    float* dir = &L[0x0c / 4];
    const uint32_t r54 = VC(edi, 0x54, u(own));
    own[2] = own[2] + K(0x005bcbe0);
    // xor al, al: the upper eax bytes are vt+0x54's result; the 3 stack arguments in abi.csv are not pushed.
    orig::fn_00457870(r54 & 0xffffff00u, u(own), obj, 0, 0, 0);
    VC(D(GameP() + 0x748), 0x54, u(pl));
    dir[0] = pl[0] - own[0];
    dir[1] = pl[1] - own[1];
    dir[2] = pl[2] - own[2];
    NormalizeSt0(0x004e7780, u(dir), u(dir));
    D(u(&dir[2])) = 0x3f333333;  // 0.7f
    NormalizeSt0(0x004e7880, u(dir), u(dir));
    dir[0] = dir[0] * K(0x005bcb30);
    dir[1] = dir[1] * K(0x005bcb30);
    dir[2] = dir[2] * K(0x005bcb30);
    const uint32_t rv = static_cast<uint32_t>(orig::fn_00458510(u(dir), obj));
    uint32_t link = D(edi + 0x880);
    if (!link)
    {
        link = D(edi + 0xadc);
        if (!link)
            return rv;
    }
    return orig::fn_00472080(obj, link);
}
static uint32_t Character_DropPickup_Abi(uint32_t eax, uint32_t /*ebp: not read*/)
{
    return Character_DropPickup(eax);
}
ABI_ENTRY_0043e710(Character_DropPickup_Entry, Character_DropPickup_Abi)
HOOK(0x0043e710, "Character_DropPickup", Character_DropPickup_Entry, nullptr);
