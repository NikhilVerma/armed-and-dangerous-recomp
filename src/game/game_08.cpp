// Recovered (static, batch game_08): ArmyTimerObject / GruntLadderObject / PickupObject / TriggerObject virtuals and the
// turret family (StigTurret, CameraPathObject = forge cannon, zeppelin turret, base turret) around 0x00496da0-0x0049bf20.
// Compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do. Functions the recorded
// runs never execute are marked "not executed in recorded runs". Vtable slots come from a scan of .rdata/.data in
// game.unpacked.exe; class names from the vtable's slot 1 descriptor (+0x14 "CLASS(Name)"). Several derived vtables
// share their base's slot 1, so the class name is the slot-1 descriptor's, not necessarily the most derived class.
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
uint32_t World()  // g_game +0xa84
{
    return D(G(), 0xa84);
}
// Virtual call by byte offset (thiscall).
template <class R, class... A>
R VC(uint32_t obj, uint32_t off, A... args)
{
    return VCall<R>(P(obj), static_cast<int>(off / 4), args...);
}
uint32_t Lo8(uint32_t eax, uint32_t al)
{
    return (eax & 0xffffff00u) | (al & 0xffu);
}
// `fild dword; jge; fadd [0x005bcb28] (2^32)`: an unsigned 32-bit value loaded onto the x87 stack (as game_05.cpp).
double U32ToX87(uint32_t v)
{
    double d = static_cast<double>(static_cast<int32_t>(v));
    if (static_cast<int32_t>(v) < 0)
        d = static_cast<float>(d + static_cast<double>(K(0x005bcb28)));
    return d;
}
// Calls an original function that returns its result in st0 with ecx and edx loaded (0x004e7880: ecx = in,
// edx = out, the vector normalise returning the length; symbols/abi.csv lists an eax return; as src/game/game_01.cpp).
float CallSt0_EcxEdx(uint32_t target, uint32_t ecx_, uint32_t edx_)
{
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
float Vec3_Normalize(uint32_t in, uint32_t out)
{
    return CallSt0_EcxEdx(0x004e7880, in, out);
}
// The refcount release seen inline in several functions: --obj+0x28; when it reaches 0 and obj vt+0xd8() returns
// al = 0, 0x00487da0 (eax = obj) frees it. Returns eax as the original leaves it (the decremented count's owner path
// is not reproduced bit for bit: 0 when nothing else was called).
uint32_t Ref_Release(uint32_t obj)
{
    if (--D(obj, 0x28) == 0)
    {
        const uint32_t r = VC<uint32_t>(obj, 0xd8);
        if (!static_cast<uint8_t>(r))
            return orig::fn_00487da0(obj);
        return r;
    }
    return 0;
}
}  // namespace

// Functions recovered later in this file, called directly by earlier ones.
uint32_t fn_00497110(uint32_t obj);

// ---------------------------------------------------------------------------------------------------------------
// 0x00496da0 (ecx = object; ArmyTimerObject vtable 0x005a2800 slot +0xbc). +0x78c = 0; 0x00496750 (ecx = object); when
// byte +0x4a9 is set: two 0x00496690 passes (edi = +0x788, stack: object, +0x52c, +0x784) with +0x78c = 1 around the
// first and (edi = +0x790, stack: object, +0x794, +0x80c) with +0x78d = 1 around the second. Returns the last callee's
// eax, or al = byte +0x4a9 (0) over 0x00496750's eax.
uint32_t fn_00496da0(uint32_t obj)
{
    B(obj, 0x78c) = 0;
    const uint32_t r = orig::fn_00496750(obj);
    const uint8_t on = B(obj, 0x4a9);
    if (!on)
        return Lo8(r, on);
    B(obj, 0x78c) = 1;
    orig::fn_00496690(obj + 0x788, obj, obj + 0x52c, D(obj, 0x784));
    const uint32_t v = D(obj, 0x80c);
    B(obj, 0x78c) = 0;
    B(obj, 0x78d) = 1;
    const uint32_t r2 = orig::fn_00496690(obj + 0x790, obj, obj + 0x794, v);
    B(obj, 0x78d) = 0;
    return r2;
}
static uint32_t fn_00496da0_Abi(uint32_t ecx)
{
    return fn_00496da0(ecx);
}
ABI_ENTRY_00496da0(fn_00496da0_Entry, fn_00496da0_Abi)
HOOK(0x00496da0, "fn_00496da0", fn_00496da0_Entry, nullptr);

// 0x00496e10 (ecx = object; stack: 4, ret 0x10; GruntLadderObject vtable 0x005a3528 slot +0x280, the load slot):
// Ai_InitGeneric 0x004247f0 (same arguments), +0x604 = World +0xe88, +0x608 = World +0xe7c, then ObjectList_Unlink
// 0x00454750 (esi = World +0xe8c, edi = object). Not executed in recorded runs.
uint32_t fn_00496e10(uint32_t obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004247f0(obj, a0, a1, a2, a3);
    D(obj, 0x604) = D(World(), 0xe88);
    D(obj, 0x608) = D(World(), 0xe7c);
    return orig::fn_00454750(D(World(), 0xe8c), obj);
}
static uint32_t fn_00496e10_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00496e10(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00496e10(fn_00496e10_Entry, fn_00496e10_Abi)
HOOK(0x00496e10, "fn_00496e10", fn_00496e10_Entry, nullptr);

// 0x00496e70 (stack: 6, ret 0x18; GruntLadderObject vtable 0x005a3528 slot +0x404): empty. Not executed in recorded
// runs.
void fn_00496e70()
{
}
static void fn_00496e70_Abi(uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/, uint32_t /*a4*/,
                            uint32_t /*a5*/)
{
    fn_00496e70();
}
ABI_ENTRY_00496e70(fn_00496e70_Entry, fn_00496e70_Abi)
HOOK(0x00496e70, "fn_00496e70", fn_00496e70_Entry, nullptr);

// 0x00496e80 (stack: 8, ret 0x20; GruntLadderObject vtable 0x005a3528 slot +0x408): empty. Not executed in recorded
// runs.
void fn_00496e80()
{
}
static void fn_00496e80_Abi(uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/, uint32_t /*a4*/,
                            uint32_t /*a5*/, uint32_t /*a6*/, uint32_t /*a7*/)
{
    fn_00496e80();
}
ABI_ENTRY_00496e80(fn_00496e80_Entry, fn_00496e80_Abi)
HOOK(0x00496e80, "fn_00496e80", fn_00496e80_Entry, nullptr);

// 0x00496e90 (ecx = object; stack: a0, a1, set, values; ret 0x10; PickupObject vtable 0x005a2ab0 slot +0x3c8, a load
// slot): 0x0040ad30 (same arguments); ObjectList_Link 0x004546d0 into World +0xe90, +0xe94, +0xe9c, +0xea0; +0x468 = 1,
// +0x46c = +0x470 = 0; "radius" (0x005b22a8) -> +0x464 (default 5.0); bool 0x005bb68c into a0's stack slot; then three
// value properties (names 0x0058f7a0, 0x0058f768, 0x0058f730) read into a1's slot, each non-zero one passed to
// 0x00456180 (ecx = 0x0058f778 / 0x0058f740 / 0x0058f708, esi = object; 0x005bbfe8, 0x0058f78c / 0x0058f754 /
// 0x0058f71c, value, 0, the a0 slot dword, 0, 0) (strings: IC_tree_slide_1..3, mover_grunt_tree_1..3: guess, the
// grunt tree-slide sounds/movers). vt+0xfc(1, 1); 0x004535b0 (eax = &v, ecx = object) fills a vector v and +0x464 *=
// |v| (sqrt((v2*v2 + v1*v1) + v0*v0)). Returns &+0x464. Not executed in recorded runs.
uint32_t fn_00496e90(uint32_t obj, uint32_t a0, uint32_t a1, uint32_t set, uint32_t values)
{
    uint32_t loc[4];  // [esp+0x10]: loc[0] = &+0x464, loc[1..3] = vector
    uint32_t slotA0 = a0;
    uint32_t slotA1 = a1;
    orig::fn_0040ad30(obj, a0, a1, set, values);
    orig::fn_004546d0(D(World(), 0xe90), obj);
    orig::fn_004546d0(D(World(), 0xe94), obj);
    orig::fn_004546d0(D(World(), 0xe9c), obj);
    orig::fn_004546d0(D(World(), 0xea0), obj);
    D(obj, 0x468) = 1;
    D(obj, 0x46c) = 0;
    D(obj, 0x470) = 0;
    loc[0] = obj + 0x464;
    orig::fn_004534a0(values, obj + 0x464, obj, 0x005b22a8, set, 0x40a00000u, 0);
    orig::fn_004534f0(set & 0xffffff00u, values, u(&slotA0), obj, 0x005bb68c, set, 0);
    static const uint32_t kNames[3] = {0x0058f7a0, 0x0058f768, 0x0058f730};
    static const uint32_t kEcx[3] = {0x0058f778, 0x0058f740, 0x0058f708};
    static const uint32_t kArg[3] = {0x0058f78c, 0x0058f754, 0x0058f71c};
    for (int i = 0; i < 3; ++i)
    {
        orig::fn_00453450(0, kNames[i], values, u(&slotA1), obj, set, 0);
        if (slotA1)
            orig::fn_00456180(kEcx[i], obj, 0x005bbfe8, kArg[i], slotA1, 0, slotA0, 0, 0);
    }
    VC<uint32_t>(obj, 0xfc, 1u, 1u);
    orig::fn_004535b0(u(&loc[1]), obj);
    const float x = u2f(loc[1]), y = u2f(loc[2]), z = u2f(loc[3]);
    float s = z * z;
    s = s + y * y;
    s = s + x * x;
    s = std::sqrt(s);
    Fl(loc[0], 0) = s * Fl(loc[0], 0);
    return loc[0];
}
static uint32_t fn_00496e90_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00496e90(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00496e90(fn_00496e90_Entry, fn_00496e90_Abi)
HOOK(0x00496e90, "fn_00496e90", fn_00496e90_Entry, nullptr);

// 0x00497080 (ecx = object; stack: collision desc; ret 4; PickupObject vtable 0x005a2ab0 slot +0x3cc):
// Object_BuildRadiusCollision 0x004527d0 (ecx = object, desc), then desc byte +0xd = 1 and dword +0 = 5.
uint32_t fn_00497080(uint32_t obj, uint32_t desc)
{
    const uint32_t r = orig::fn_004527d0(obj, desc);
    B(desc, 0xd) = 1;
    D(desc, 0) = 5;
    return r;
}
static uint32_t fn_00497080_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00497080(ecx, a0);
}
ABI_ENTRY_00497080(fn_00497080_Entry, fn_00497080_Abi)
HOOK(0x00497080, "fn_00497080", fn_00497080_Entry, nullptr);

// 0x004970a0 (ecx = object; PickupObject vtable 0x005a2ab0 slot +0x3d0): when flag 0x4000 of +0x460 is set and
// 0x00497110 (the 4x4 grid-cell test) returns al = 0: +0x470 = object, ++object+0x28 (a self reference), and
// ObjectList_Unlink 0x00454750 (esi = World +0xed4, edi = object). Not executed in recorded runs.
uint32_t fn_004970a0(uint32_t obj)
{
    const uint32_t f = D(obj, 0x460);
    if (!(f & 0x4000))
        return f;
    const uint32_t r = fn_00497110(obj);
    if (static_cast<uint8_t>(r))
        return r;
    D(obj, 0x470) = obj;
    ++D(obj, 0x28);
    return orig::fn_00454750(D(World(), 0xed4), obj);
}
static uint32_t fn_004970a0_Abi(uint32_t ecx)
{
    return fn_004970a0(ecx);
}
ABI_ENTRY_004970a0(fn_004970a0_Entry, fn_004970a0_Abi)
HOOK(0x004970a0, "fn_004970a0", fn_004970a0_Entry, nullptr);

// 0x004970e0 (ecx = object; stack: 3, ret 0xc; PickupObject vtable 0x005a2ab0 slot +0x444): 0x00454240 (same
// arguments), then vt+0x6c(a0). Not executed in recorded runs.
uint32_t fn_004970e0(uint32_t obj, uint32_t a0, uint32_t a1, uint32_t a2)
{
    orig::fn_00454240(obj, a0, a1, a2);
    return VC<uint32_t>(obj, 0x6c, a0);
}
static uint32_t fn_004970e0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004970e0(ecx, a0, a1, a2);
}
ABI_ENTRY_004970e0(fn_004970e0_Entry, fn_004970e0_Abi)
HOOK(0x004970e0, "fn_004970e0", fn_004970e0_Entry, nullptr);

// 0x00497110 (ecx = object): position = vt+0x54; 0x0046f320 (esi = grid g_game +0xaa4; x, y, &cx, &cy) maps it to a
// cell; when that fails al = 0. Otherwise every cell (i, j) with i in [cx-2, cx+1] and j in [cy-2, cy+1] outside the
// 3x3 block around (cx, cy) is tested with 0x0046f2e0 (eax = grid, ecx = i, edx = j); a failure clears the result and
// ends that column (the outer loop continues). Original quirk: the range is cx-2..cx+1 (asymmetric). Returns al = 1
// when all passed, upper eax = cx+2's. Caller 0x004970a0. Not executed in recorded runs.
uint32_t fn_00497110(uint32_t obj)
{
    uint32_t loc[7];  // sub esp, 0x1c: [1] = cx, [2] = cy, [3] = grid, [4..6] = position
    VC<uint32_t>(obj, 0x54, u(&loc[4]));
    const uint32_t grid = D(G(), 0xaa4);
    loc[3] = grid;
    const uint32_t r = orig::fn_0046f320(grid, loc[4], loc[5], u(&loc[1]), u(&loc[2]));
    if (!static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    const int32_t cx = static_cast<int32_t>(loc[1]);
    const int32_t cy = static_cast<int32_t>(loc[2]);
    uint8_t ok = 1;
    for (int32_t i = cx - 2; i < cx + 2; ++i)
    {
        for (int32_t j = cy - 2; j < cy + 2; ++j)
        {
            if (i < cx - 1 || i > cx + 1 || j < cy - 1 || j > cy + 1)
            {
                if (!static_cast<uint8_t>(orig::fn_0046f2e0(loc[3], static_cast<uint32_t>(i), static_cast<uint32_t>(j))))
                {
                    ok = 0;
                    break;
                }
            }
        }
    }
    return Lo8(static_cast<uint32_t>(cx + 2), ok);
}
static uint32_t fn_00497110_Abi(uint32_t ecx)
{
    return fn_00497110(ecx);
}
ABI_ENTRY_00497110(fn_00497110_Entry, fn_00497110_Abi)
HOOK(0x00497110, "fn_00497110", fn_00497110_Entry, nullptr);

// 0x004971d0 (ecx = object; PickupObject vtable 0x005a2ab0 slot +0x4f0): returns +0x468. Not executed in recorded runs.
uint32_t fn_004971d0(uint32_t obj)
{
    return D(obj, 0x468);
}
static uint32_t fn_004971d0_Abi(uint32_t ecx)
{
    return fn_004971d0(ecx);
}
ABI_ENTRY_004971d0(fn_004971d0_Entry, fn_004971d0_Abi)
HOOK(0x004971d0, "fn_004971d0", fn_004971d0_Entry, nullptr);

// 0x004971e0 (ecx = object; PickupObject vtable 0x005a2ab0 slot +0x4ec): returns +0x46c. Not executed in recorded runs.
uint32_t fn_004971e0(uint32_t obj)
{
    return D(obj, 0x46c);
}
static uint32_t fn_004971e0_Abi(uint32_t ecx)
{
    return fn_004971e0(ecx);
}
ABI_ENTRY_004971e0(fn_004971e0_Entry, fn_004971e0_Abi)
HOOK(0x004971e0, "fn_004971e0", fn_004971e0_Entry, nullptr);

// 0x004971f0 (ecx = object; stack: 1 unused, ret 4; PickupObject vtable 0x005a2ab0 slot +0x4f8): al = (+0x470 == 0),
// upper eax from +0x470. Not executed in recorded runs.
uint32_t fn_004971f0(uint32_t obj)
{
    const uint32_t v = D(obj, 0x470);
    return Lo8(v, v == 0 ? 1u : 0u);
}
static uint32_t fn_004971f0_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return fn_004971f0(ecx);
}
ABI_ENTRY_004971f0(fn_004971f0_Entry, fn_004971f0_Abi)
HOOK(0x004971f0, "fn_004971f0", fn_004971f0_Entry, nullptr);

// 0x00497200 (stack: 1 unused, ret 4; PickupObject vtable 0x005a2ab0 slot +0x504, JumpMarker vtable 0x005ac628 slot
// +0x154): returns 1. Not executed in recorded runs.
uint32_t fn_00497200()
{
    return 1;
}
static uint32_t fn_00497200_Abi(uint32_t /*a0*/)
{
    return fn_00497200();
}
ABI_ENTRY_00497200(fn_00497200_Entry, fn_00497200_Abi)
HOOK(0x00497200, "fn_00497200", fn_00497200_Entry, nullptr);

// 0x00497210 (ecx = object; stack: out pos, dir, a2, a3, target, matrix; ret 0x18; PickupObject vtable 0x005a2ab0 slot
// +0x4fc). p = object vt+0x54. With a target: dir.xy = p.xy - target vt+0x54 .xy, normalised by 0x004e7780 (ecx = edx =
// dir; dir.z is not written first), out.xy = dir.xy * K(0x005bcbe8) + p.xy. Without: 0x00453150 (ecx = object, esi =
// dir), dir = -dir, out.xy = dir.xy * +0x464 + p.xy. out.z = p.z. With a matrix, 0x004e3070 (eax = dir, ecx = matrix,
// edx = 0, edi = out). Returns that call's eax (0 otherwise: the original's eax there is not reproduced). Not executed in
// recorded runs.
uint32_t fn_00497210(uint32_t obj, uint32_t out, uint32_t dir, uint32_t /*a2*/, uint32_t /*a3*/, uint32_t target,
                     uint32_t matrix)
{
    float p[3], t[3];
    float x, y;
    if (target)
    {
        VC<uint32_t>(obj, 0x54, u(p));
        VC<uint32_t>(target, 0x54, u(t));
        Fl(dir, 0) = p[0] - t[0];
        Fl(dir, 4) = p[1] - t[1];
        orig::fn_004e7780(dir, dir);
        x = Fl(dir, 0) * K(0x005bcbe8) + p[0];
        Fl(out, 0) = x;
        y = Fl(dir, 4) * K(0x005bcbe8);
    }
    else
    {
        VC<uint32_t>(obj, 0x54, u(p));
        orig::fn_00453150(obj, dir);
        const float nx = -Fl(dir, 0);
        Fl(dir, 0) = nx;
        Fl(dir, 4) = -Fl(dir, 4);
        Fl(dir, 8) = -Fl(dir, 8);
        Fl(out, 0) = nx * Fl(obj, 0x464) + p[0];
        y = Fl(obj, 0x464) * Fl(dir, 4);
    }
    D(out, 8) = f2u(p[2]);
    Fl(out, 4) = y + p[1];
    if (matrix)
        return orig::fn_004e3070(dir, matrix, 0, out);
    return 0;
}
static uint32_t fn_00497210_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                uint32_t a5)
{
    return fn_00497210(ecx, a0, a1, a2, a3, a4, a5);
}
ABI_ENTRY_00497210(fn_00497210_Entry, fn_00497210_Abi)
HOOK(0x00497210, "fn_00497210", fn_00497210_Entry, nullptr);

// 0x004972f0 (ecx = object; stack: out pos, dir, a2, a3, target, matrix; ret 0x18; PickupObject vtable 0x005a2ab0 slot
// +0x500). As 0x00497210, but both branches scale by +0x464 (out.xy = dir.xy * +0x464 + p.xy), and with a matrix
// Matrix_LookAt 0x004e2eb0 (eax = 0, ecx = matrix, edx = dir, edi = out). Not executed in recorded runs.
uint32_t fn_004972f0(uint32_t obj, uint32_t out, uint32_t dir, uint32_t /*a2*/, uint32_t /*a3*/, uint32_t target,
                     uint32_t matrix)
{
    float p[3], t[3];
    float nx;
    VC<uint32_t>(obj, 0x54, u(p));
    if (target)
    {
        VC<uint32_t>(target, 0x54, u(t));
        Fl(dir, 0) = p[0] - t[0];
        Fl(dir, 4) = p[1] - t[1];
        orig::fn_004e7780(dir, dir);
        nx = Fl(dir, 0);
    }
    else
    {
        orig::fn_00453150(obj, dir);
        nx = -Fl(dir, 0);
        Fl(dir, 0) = nx;
        Fl(dir, 4) = -Fl(dir, 4);
        Fl(dir, 8) = -Fl(dir, 8);
    }
    Fl(out, 0) = nx * Fl(obj, 0x464) + p[0];
    const float y = Fl(obj, 0x464) * Fl(dir, 4);
    D(out, 8) = f2u(p[2]);
    Fl(out, 4) = y + p[1];
    if (matrix)
        return orig::fn_004e2eb0(0, matrix, dir, out);
    return 0;
}
static uint32_t fn_004972f0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                uint32_t a5)
{
    return fn_004972f0(ecx, a0, a1, a2, a3, a4, a5);
}
ABI_ENTRY_004972f0(fn_004972f0_Entry, fn_004972f0_Abi)
HOOK(0x004972f0, "fn_004972f0", fn_004972f0_Entry, nullptr);

// 0x004973b0 (ecx = object; stack: other, unused; ret 8; PickupObject vtable 0x005a2ab0 slot +0x508): +0x470 = other,
// ++other+0x28 (reference). Not executed in recorded runs.
uint32_t fn_004973b0(uint32_t obj, uint32_t other)
{
    D(obj, 0x470) = other;
    ++D(other, 0x28);
    return other;
}
static uint32_t fn_004973b0_Abi(uint32_t ecx, uint32_t a0, uint32_t /*a1*/)
{
    return fn_004973b0(ecx, a0);
}
ABI_ENTRY_004973b0(fn_004973b0_Entry, fn_004973b0_Abi)
HOOK(0x004973b0, "fn_004973b0", fn_004973b0_Entry, nullptr);

// 0x004973c0 (ecx = object; stack: other, unused; ret 8; PickupObject vtable 0x005a2ab0 slot +0x50c): when other is
// the object held in +0x470, releases that reference (inline refcount release) and clears +0x470. Not executed in
// recorded runs.
uint32_t fn_004973c0(uint32_t obj, uint32_t other)
{
    if (!other)
        return 0;
    const uint32_t held = D(obj, 0x470);
    if (other != held)
        return other;
    uint32_t r = other;
    if (--D(held, 0x28) == 0)
    {
        r = VC<uint32_t>(held, 0xd8);
        if (!static_cast<uint8_t>(r))
            r = orig::fn_00487da0(held);
    }
    D(obj, 0x470) = 0;
    return r;
}
static uint32_t fn_004973c0_Abi(uint32_t ecx, uint32_t a0, uint32_t /*a1*/)
{
    return fn_004973c0(ecx, a0);
}
ABI_ENTRY_004973c0(fn_004973c0_Entry, fn_004973c0_Abi)
HOOK(0x004973c0, "fn_004973c0", fn_004973c0_Entry, nullptr);

// 0x00497410 (ecx = object; stack: 1 unused, ret 4; PickupObject vtable 0x005a2ab0 slot +0x510): ObjectList_Unlink
// 0x00454750 (esi = World +0xed4, edi = object). Not executed in recorded runs.
uint32_t fn_00497410(uint32_t obj)
{
    return orig::fn_00454750(D(World(), 0xed4), obj);
}
static uint32_t fn_00497410_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return fn_00497410(ecx);
}
ABI_ENTRY_00497410(fn_00497410_Entry, fn_00497410_Abi)
HOOK(0x00497410, "fn_00497410", fn_00497410_Entry, nullptr);

// 0x00497430 (ecx = object; PickupObject vtable 0x005a2ab0 slot +0x48c, remove): Object_Remove 0x004547b0, vt+0x15c(
// +0x470, 0), +0x470 = 0.
uint32_t fn_00497430(uint32_t obj)
{
    orig::fn_004547b0(obj);
    const uint32_t r = VC<uint32_t>(obj, 0x15c, D(obj, 0x470), 0u);
    D(obj, 0x470) = 0;
    return r;
}
static uint32_t fn_00497430_Abi(uint32_t ecx)
{
    return fn_00497430(ecx);
}
ABI_ENTRY_00497430(fn_00497430_Entry, fn_00497430_Abi)
HOOK(0x00497430, "fn_00497430", fn_00497430_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// TriggerObject (vtable 0x005a7c20). +0x460 on_enter trigger, +0x464 on_exit, +0x468 enable message, +0x46c disable
// message, +0x470 sphere_radius, +0x474 flags: 1 one_shot, 2 ai_can_trigger, 4 pending enter, 8 enabled, 0x10 door_mat.

// 0x00497460 (ecx = object; stack: a0, a1, set, values; ret 0x10; TriggerObject slot 6, the load slot). Flags +0x474
// = 0, then the properties (strings in .rdata): one_shot (1), door_mat (0x10), sphere_radius (+0x470, value), on_enter
// (+0x460), on_exit (+0x464), enable (+0x468; none -> flag 8, enabled), disable (+0x46c), ai_can_trigger (2); the base
// load Object_LoadProperties 0x004517b0 (same arguments); when "visible" is not set, vt+0xf4(0); ObjectList_Link
// 0x004546d0 (esi = World +0xec8, edi = object). The bools are read into the values argument's stack slot (the
// original reuses it; values itself stays in esi).
uint32_t TriggerObject_Load(uint32_t obj, uint32_t a0, uint32_t a1, uint32_t set, uint32_t values)
{
    uint32_t slot = values;
    D(obj, 0x474) = 0;
    orig::fn_004534f0(0, values, u(&slot), obj, 0x005b9500, set, 0);  // one_shot
    if (static_cast<uint8_t>(slot))
        D(obj, 0x474) |= 1;
    orig::fn_004534f0(0, values, u(&slot), obj, 0x0058f6fc, set, 0);  // door_mat
    if (static_cast<uint8_t>(slot))
        D(obj, 0x474) |= 0x10;
    orig::fn_00453400(0, values, obj + 0x470, obj, 0x0058f6ec, set, 0);  // sphere_radius
    orig::fn_00453450(0, 0x0058f6e0, values, obj + 0x460, obj, set, 0);  // on_enter
    orig::fn_00453450(0, 0x0058f6d8, values, obj + 0x464, obj, set, 0);  // on_exit
    orig::fn_00453450(0, 0x0058f6d0, values, obj + 0x468, obj, set, 0);  // enable
    if (D(obj, 0x468) == 0)
        D(obj, 0x474) |= 8;
    orig::fn_00453450(0, 0x0058f6c8, values, obj + 0x46c, obj, set, 0);  // disable
    orig::fn_004534f0(0, values, u(&slot), obj, 0x0058f6b8, set, 0);      // ai_can_trigger
    if (static_cast<uint8_t>(slot))
        D(obj, 0x474) |= 2;
    orig::fn_004517b0(obj, a0, a1, set, values);
    orig::fn_004534f0(0, values, u(&slot), obj, 0x005bba64, set, 0);  // visible
    if (!static_cast<uint8_t>(slot))
        VC<uint32_t>(obj, 0xf4, 0u);
    return static_cast<uint32_t>(orig::fn_004546d0(D(World(), 0xec8), obj));
}
static uint32_t TriggerObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return TriggerObject_Load(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00497460(TriggerObject_Load_Entry, TriggerObject_Load_Abi)
HOOK(0x00497460, "TriggerObject_Load", TriggerObject_Load_Entry, nullptr);

// 0x004975d0 (ecx = object; stack: collision desc; ret 4; TriggerObject slot 7): desc byte +0x78 = 1, float +4 =
// (float)(unsigned) sphere_radius +0x470, byte +0xc = 1; Object_BuildRadiusCollision 0x004527d0 (ecx = object, desc);
// dword +0 = ((flags & 2) | 1) << 3 (8, or 0x18 with ai_can_trigger), byte +0xd = 1.
uint32_t TriggerObject_BuildCollision(uint32_t obj, uint32_t desc)
{
    B(desc, 0x78) = 1;
    Fl(desc, 4) = static_cast<float>(U32ToX87(D(obj, 0x470)));
    B(desc, 0xc) = 1;
    const uint32_t r = orig::fn_004527d0(obj, desc);
    D(desc, 0) = ((static_cast<uint32_t>(B(obj, 0x474)) & 2u) | 1u) << 3;
    B(desc, 0xd) = 1;
    return r;
}
static uint32_t TriggerObject_BuildCollision_Abi(uint32_t ecx, uint32_t a0)
{
    return TriggerObject_BuildCollision(ecx, a0);
}
ABI_ENTRY_004975d0(TriggerObject_BuildCollision_Entry, TriggerObject_BuildCollision_Abi)
HOOK(0x004975d0, "TriggerObject_BuildCollision", TriggerObject_BuildCollision_Entry, nullptr);

// 0x00497620 (ecx = object; stack: a0; ret 4; slot +0xd4 of TriggerObject and many PickupObject-family vtables, also
// PickupGenerator +0x20c/+0x34c, PlayerWeapon +0x37c, SniperCaptain +0x33c, Twiglet +0x20c): when bit 1 of +0x474 is
// set (TriggerObject: one_shot), tail jump to Object_Kill 0x00455640 (ecx = object, a0); otherwise returns (eax as the
// caller left it: not reproduced, 0). Not executed in recorded runs.
uint32_t fn_00497620(uint32_t obj, uint32_t a0)
{
    if (B(obj, 0x474) & 1)
        return orig::fn_00455640(obj, a0);
    return 0;
}
static uint32_t fn_00497620_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00497620(ecx, a0);
}
ABI_ENTRY_00497620(fn_00497620_Entry, fn_00497620_Abi)
HOOK(0x00497620, "fn_00497620", fn_00497620_Entry, nullptr);

// 0x00497640 (ecx = object; TriggerObject slot +0xbc, PickupGenerator vtable 0x005a6258 slot +0x334): when flag 4
// (pending enter) is set: with no on_enter trigger +0x460 the flag is cleared; when enabled (8), Object_SendTrigger
// 0x00461870 (eax = World, ebx = +0x460; object), flag 4 cleared, and a one_shot trigger without door_mat (1 set, 0x10
// clear) gets +0x460 = 0. Returns the flags as eax holds them.
uint32_t fn_00497640(uint32_t obj)
{
    const uint32_t f = D(obj, 0x474);
    if (!(f & 4))
        return f;
    const uint32_t trig = D(obj, 0x460);
    if (!trig)
    {
        D(obj, 0x474) &= 0xfffffffbu;
        return f;
    }
    if (!(f & 8))
        return f;
    orig::fn_00461870(World(), trig, obj);
    const uint32_t nf = D(obj, 0x474) & 0xfffffffbu;
    D(obj, 0x474) = nf;
    if ((nf & 1) && !(nf & 0x10))
        D(obj, 0x460) = 0;
    return nf;
}
static uint32_t fn_00497640_Abi(uint32_t ecx)
{
    return fn_00497640(ecx);
}
ABI_ENTRY_00497640(fn_00497640_Entry, fn_00497640_Abi)
HOOK(0x00497640, "fn_00497640", fn_00497640_Entry, nullptr);

// 0x004976a0 (ecx = object; stack: entering (byte), other; ret 8; TriggerObject slot +0xb4): with an other object and an
// on_enter or on_exit trigger: entering sets flag 4 (pending; sent by 0x00497640); leaving clears it and, with an
// on_exit trigger +0x464 and the trigger enabled (8), sends it (Object_SendTrigger 0x00461870, eax = World, ebx =
// on_exit; object), a one_shot trigger clearing +0x464 first. Not executed in recorded runs.
uint32_t fn_004976a0(uint32_t obj, uint32_t entering, uint32_t other)
{
    if (!other)
        return 0;
    uint32_t v = D(obj, 0x460);
    if (!v)
    {
        v = D(obj, 0x464);
        if (!v)
            return 0;
    }
    if (static_cast<uint8_t>(entering))
    {
        D(obj, 0x474) |= 4;
        return Lo8(v, entering);
    }
    const uint32_t f = D(obj, 0x474) & 0xfffffffbu;
    D(obj, 0x474) = f;
    const uint32_t trig = D(obj, 0x464);
    if (!trig)
        return Lo8(v, entering);
    if (!(f & 8))
        return f;
    if (f & 1)
        D(obj, 0x464) = 0;
    return orig::fn_00461870(World(), trig, obj);
}
static uint32_t fn_004976a0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004976a0(ecx, a0, a1);
}
ABI_ENTRY_004976a0(fn_004976a0_Entry, fn_004976a0_Abi)
HOOK(0x004976a0, "fn_004976a0", fn_004976a0_Entry, nullptr);

// 0x00497720 (ecx = object; stack: a0, message; ret 8; slot +0x38 of TriggerObject and the PickupObject family,
// PeasantObject, PlayerWeapon, SniperCaptain, Twiglet): message == +0x468 (enable) sets flag 8 of +0x474, message ==
// +0x46c (disable) clears it. Returns al = 0 (upper eax = message). Not executed in recorded runs.
uint32_t fn_00497720(uint32_t obj, uint32_t msg)
{
    if (msg == D(obj, 0x468))
    {
        D(obj, 0x474) |= 8;
        return msg & 0xffffff00u;
    }
    if (msg == D(obj, 0x46c))
        D(obj, 0x474) &= 0xfffffff7u;
    return msg & 0xffffff00u;
}
static uint32_t fn_00497720_Abi(uint32_t ecx, uint32_t /*a0*/, uint32_t a1)
{
    return fn_00497720(ecx, a1);
}
ABI_ENTRY_00497720(fn_00497720_Entry, fn_00497720_Abi)
HOOK(0x00497720, "fn_00497720", fn_00497720_Entry, nullptr);

// 0x00497900 (ecx = object; stack: a0, message; ret 8; PickupObject vtable 0x005a2ab0 slot +0x2b0). When message ==
// +0x460: picks a speaker among up to three candidates, each enabled by a byte and counted when within radius +0x4e4
// (squared distance to the player g_game +0x748 < r*r): byte +0x4e9 -> 0x00475d40 (eax = player), byte +0x4eb ->
// 0x00475d30 (eax = player), byte +0x4ec -> 0x004592e0 (eax = World +0xe70, ecx = name +0x4f0; object); dead ones
// (Object_IsDead 0x00454780) are dropped; byte +0x4ea adds one more choice (the object itself). With none in range the
// nearest present one is taken; with none at all, vt+0xdc and al = 0. Otherwise (_rand * count) >> 15 selects: 0 with
// +0x4ea -> g_game +0x2c vt+0xd0(&+0x464, 0, 0, 0, -1, 0); otherwise g_game +0x2c vt+0xc4(line, speaker, 0, 0, -1, 0)
// with (+0x484, first) / (+0x4a4, second) / (+0x4c4, third) by the flags (exact choice below). Then vt+0xdc unless byte
// +0x4e8. Returns al = 0. Not executed in recorded runs.
uint32_t fn_00497900(uint32_t obj, uint32_t msg)
{
    if (msg != D(obj, 0x460))
        return msg & 0xffffff00u;
    uint32_t count = 0;
    uint8_t self = 0;  // [esp+0x13]
    if (B(obj, 0x4ea))
    {
        self = 1;
        count = 1;
    }
    float d1 = 1.0e8f;  // [esp+0x14]  (0x4cbebc20)
    float d3 = 1.0e8f;  // [esp+0x20]
    float d2 = 1.0e8f;  // [esp+0x18]
    uint8_t near1 = 0;  // a1's stack slot
    uint32_t t1 = 0;    // [esp+0x1c]
    float a[3], b[3];
    if (B(obj, 0x4e9))
    {
        const uint32_t t = orig::fn_00475d40(D(G(), 0x748));
        t1 = t;
        if (t && !static_cast<uint8_t>(orig::fn_00454780(t)))
        {
            VC<uint32_t>(D(G(), 0x748), 0x54, u(b));  // [esp+0x30]: player
            VC<uint32_t>(t, 0x54, u(a));              // [esp+0x24]: candidate
            const float dy = a[1] - b[1];
            const float dz = a[2] - b[2];
            const float dx = a[0] - b[0];
            float s = dx * dx;
            s = dz * dz + s;
            s = dy * dy + s;
            d1 = s;
            const float r = Fl(obj, 0x4e4);
            if (d1 < r * r)
            {
                ++count;
                near1 = 1;
            }
        }
        else
            t1 = 0;
    }
    uint32_t t2 = 0;    // ebp
    uint8_t near2 = 0;  // [esp+0x12]
    if (B(obj, 0x4eb))
    {
        t2 = orig::fn_00475d30(D(G(), 0x748));
        if (t2 && !static_cast<uint8_t>(orig::fn_00454780(t2)))
        {
            VC<uint32_t>(D(G(), 0x748), 0x54, u(a));  // [esp+0x24]: player
            VC<uint32_t>(t2, 0x54, u(b));             // [esp+0x30]: candidate
            const float dz = b[2] - a[2];
            const float dy = b[1] - a[1];
            const float dx = b[0] - a[0];
            float s = dy * dy;
            s = s + dz * dz;
            s = s + dx * dx;
            d2 = s;
            const float r = Fl(obj, 0x4e4);
            if (d2 < r * r)
            {
                ++count;
                near2 = 1;
            }
        }
        else
            t2 = 0;
    }
    uint32_t t3 = 0;  // esi
    if (B(obj, 0x4ec))
    {
        t3 = orig::fn_004592e0(D(World(), 0xe70), D(obj, 0x4f0), obj);
        if (t3 && !static_cast<uint8_t>(orig::fn_00454780(t3)))
        {
            VC<uint32_t>(D(G(), 0x748), 0x54, u(a));
            VC<uint32_t>(t3, 0x54, u(b));
            const float dz = b[2] - a[2];
            const float dy = b[1] - a[1];
            const float dx = b[0] - a[0];
            float s = dx * dx;
            s = dy * dy + s;
            s = s + dz * dz;
            d3 = s;
            const float r = Fl(obj, 0x4e4);
            if (d3 < r * r)
                ++count;
        }
        else
            t3 = 0;
    }
    if (count == 0)
    {
        if (!t1 && !t2 && !t3)
            return VC<uint32_t>(obj, 0xdc) & 0xffffff00u;
        if (d1 < d2 && d1 < d3)
            near1 = 1;
        else if (d2 < d1 && d2 < d3)
            near2 = 1;
        count = 1;
    }
    const int32_t pick = (static_cast<int32_t>(static_cast<uint32_t>(orig::fn_005391cd())) *
                          static_cast<int32_t>(count)) >> 15;
    const uint32_t speech = D(G(), 0x2c);
    uint32_t r;
    // The original's branches (0x00497bdc..0x00497cc3), reproduced literally.
    enum
    {
        L1,
        L2,
        L3,
        SELF
    } line;
    if (pick == 0)
    {
        if (self)
            line = SELF;
        else
            line = near1 ? L1 : (near2 ? L2 : L3);  // 0x00497c31
    }
    else if (pick == 1)
    {
        if (self)
            line = near1 ? L1 : (near2 ? L2 : L3);  // 0x00497c31
        else
            line = (near1 && near2) ? L2 : L3;  // 0x00497c77
    }
    else if (pick == 2 && self)
        line = (near1 && near2) ? L2 : L3;  // 0x00497c77
    else
        line = L3;
    switch (line)
    {
    case SELF:
        r = VC<uint32_t>(speech, 0xd0, obj + 0x464, 0u, 0u, 0u, 0xffffffffu, 0u);
        break;
    case L1:
        r = VC<uint32_t>(speech, 0xc4, obj + 0x484, t1, 0u, 0u, 0xffffffffu, 0u);
        break;
    case L2:
        r = VC<uint32_t>(speech, 0xc4, obj + 0x4a4, t2, 0u, 0u, 0xffffffffu, 0u);
        break;
    default:
        r = VC<uint32_t>(speech, 0xc4, obj + 0x4c4, t3, 0u, 0u, 0xffffffffu, 0u);
        break;
    }
    if (!B(obj, 0x4e8))
        r = VC<uint32_t>(obj, 0xdc);
    return r & 0xffffff00u;
}
static uint32_t fn_00497900_Abi(uint32_t ecx, uint32_t /*a0*/, uint32_t a1)
{
    return fn_00497900(ecx, a1);
}
ABI_ENTRY_00497900(fn_00497900_Entry, fn_00497900_Abi)
HOOK(0x00497900, "fn_00497900", fn_00497900_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Turrets. The emitter part (0x00497d00) is shared by the mountable turrets (0x004980c0) and RocketBudTurret
// (callers 0x0049c320, 0x0049c9b0). +0x460 projectile_name, +0x480 ai_projectile_name, +0x4a0 emitter_bone_name,
// +0x4c0 muzzle_fx_name, +0x500 muzzle_bone_name, +0x520 recycle_time, +0x528 send_trigger_on_death, +0x530 flags
// (0x20 rapid_fire, 0x40 / 0x80 muzzle fx state), +0x534 muzzle fx handle, +0x539 zoomed.

// 0x00497d00 (ecx = object; stack: a0, a1, set, values; ret 0x10). Object_LoadProperties 0x004517b0 (same
// arguments); +0x530 = +0x534 = 0; 0x00408e30 (eax = 0x81, ecx = object, edx = 1; 0, 0, 0, 0, 0); then recycle_time
// (+0x520, 1.0), projectile_name, ai_projectile_name, emitter_bone_name, muzzle_fx_name, muzzle_bone_name (strings,
// default ""), send_trigger_on_death (+0x528); ObjectList_Link 0x004546d0 into World +0xe90, +0xe94, +0xe9c;
// vt+0xfc(1, 2); +0x524 = 0.
uint32_t fn_00497d00(uint32_t obj, uint32_t a0, uint32_t a1, uint32_t set, uint32_t values)
{
    orig::fn_004517b0(obj, a0, a1, set, values);
    D(obj, 0x530) = 0;
    D(obj, 0x534) = 0;
    orig::fn_00408e30(0x81, obj, 1, 0, 0, 0, 0, 0);
    orig::fn_004534a0(values, obj + 0x520, obj, 0x0058f640, set, 0x3f800000u, 0);  // recycle_time
    orig::fn_004533a0(0x0058a9a4, obj + 0x460, obj, 0x0058f630, set, values, 0);   // projectile_name
    orig::fn_004533a0(0x0058a9a4, obj + 0x480, obj, 0x0058f61c, set, values, 0);   // ai_projectile_name
    orig::fn_004533a0(0x0058a9a4, obj + 0x4a0, obj, 0x0058f608, set, values, 0);   // emitter_bone_name
    orig::fn_004533a0(0x0058a9a4, obj + 0x4c0, obj, 0x0058f5f8, set, values, 0);   // muzzle_fx_name
    orig::fn_004533a0(0x0058a9a4, obj + 0x500, obj, 0x0058f5e4, set, values, 0);   // muzzle_bone_name
    orig::fn_00453450(0, 0x005bb888, values, obj + 0x528, obj, set, 0);            // send_trigger_on_death
    orig::fn_004546d0(D(World(), 0xe90), obj);
    orig::fn_004546d0(D(World(), 0xe94), obj);
    orig::fn_004546d0(D(World(), 0xe9c), obj);
    const uint32_t r = VC<uint32_t>(obj, 0xfc, 1u, 2u);
    D(obj, 0x524) = 0;
    return r;
}
static uint32_t fn_00497d00_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00497d00(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00497d00(fn_00497d00_Entry, fn_00497d00_Abi)
HOOK(0x00497d00, "fn_00497d00", fn_00497d00_Entry, nullptr);

// 0x00497e60 (ecx = object; stack: a0; ret 4; slot +0xd4 of StigTurret 0x005a19d8, RocketBudTurret 0x005adde8,
// CameraPathObject 0x005a0f18 +0x20c, vtable 0x005abf48 +0x214): with send_trigger_on_death +0x528, Object_SendTrigger
// 0x00461870 (eax = World, ebx = it; object); then Object_Kill 0x00455640 (ecx = object, a0). Caller 0x0049c540. Not
// executed in recorded runs.
uint32_t fn_00497e60(uint32_t obj, uint32_t a0)
{
    const uint32_t trig = D(obj, 0x528);
    if (trig)
        orig::fn_00461870(World(), trig, obj);
    return orig::fn_00455640(obj, a0);
}
static uint32_t fn_00497e60_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00497e60(ecx, a0);
}
ABI_ENTRY_00497e60(fn_00497e60_Entry, fn_00497e60_Abi)
HOOK(0x00497e60, "fn_00497e60", fn_00497e60_Entry, nullptr);

// 0x00497e90 (ecx = object; RocketBudTurret vtable 0x005adde8 slot +0xdc; callers 0x004986b0, 0x0049c4c0):
// Object_Remove 0x004547b0; Fx_Release 0x0041d030(g_game +0xa90, &+0x534) when a muzzle fx is held; vt+0xfc(0, 2). Not
// executed in recorded runs.
uint32_t fn_00497e90(uint32_t obj)
{
    orig::fn_004547b0(obj);
    if (D(obj, 0x534))
        orig::fn_0041d030(D(G(), 0xa90), obj + 0x534);
    return VC<uint32_t>(obj, 0xfc, 0u, 2u);
}
static uint32_t fn_00497e90_Abi(uint32_t ecx)
{
    return fn_00497e90(ecx);
}
ABI_ENTRY_00497e90(fn_00497e90_Entry, fn_00497e90_Abi)
HOOK(0x00497e90, "fn_00497e90", fn_00497e90_Entry, nullptr);

// 0x00497ed0 (ecx = object; callers 0x00498710, 0x0049c6a0, 0x0049cbe0): muzzle fx state step on +0x530: 0x40 set ->
// clear it; else 0x80 clear -> set it; else release the muzzle fx (Fx_Release 0x0041d030(g_game +0xa90, &+0x534)) when
// one is held. Returns the old flags, &+0x534 or Fx_Release's eax.
uint32_t fn_00497ed0(uint32_t obj)
{
    const uint32_t f = D(obj, 0x530);
    if (f & 0x40)
    {
        D(obj, 0x530) &= 0xffffffbfu;
        return f;
    }
    if (!(f & 0x80))
    {
        D(obj, 0x530) |= 0x80;
        return f;
    }
    if (!D(obj, 0x534))
        return obj + 0x534;
    return orig::fn_0041d030(D(G(), 0xa90), obj + 0x534);
}
static uint32_t fn_00497ed0_Abi(uint32_t ecx)
{
    return fn_00497ed0(ecx);
}
ABI_ENTRY_00497ed0(fn_00497ed0_Entry, fn_00497ed0_Abi)
HOOK(0x00497ed0, "fn_00497ed0", fn_00497ed0_Entry, nullptr);

// 0x00497f10 (stack: a, unused, b, c; ret 0x10; StigTurret +0x16c, LightObject 0x005a8a50 +0x2a4, RocketBudTurret
// +0x16c, vtables 0x005a9bb0 +0x514 and 0x005abf48 +0x2ac): *a = *b = *c = 0. Returns a. Not executed in recorded runs.
uint32_t fn_00497f10(uint32_t a, uint32_t b, uint32_t c)
{
    D(a, 0) = 0;
    D(b, 0) = 0;
    D(c, 0) = 0;
    return a;
}
static uint32_t fn_00497f10_Abi(uint32_t a0, uint32_t /*a1*/, uint32_t a2, uint32_t a3)
{
    return fn_00497f10(a0, a2, a3);
}
ABI_ENTRY_00497f10(fn_00497f10_Entry, fn_00497f10_Abi)
HOOK(0x00497f10, "fn_00497f10", fn_00497f10_Entry, nullptr);

// 0x00497f40 (ecx = object; stack: collision desc; ret 4; StigTurret slot 7, CameraPathObject +0x154, vtables
// 0x005a9bb0 +0x3c4, 0x005abf48 +0x15c; caller 0x0049ca90): Object_BuildRadiusCollision 0x004527d0, then desc dword +0 =
// 5 and byte +0xd = 1.
uint32_t fn_00497f40(uint32_t obj, uint32_t desc)
{
    const uint32_t r = orig::fn_004527d0(obj, desc);
    D(desc, 0) = 5;
    B(desc, 0xd) = 1;
    return r;
}
static uint32_t fn_00497f40_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00497f40(ecx, a0);
}
ABI_ENTRY_00497f40(fn_00497f40_Entry, fn_00497f40_Abi)
HOOK(0x00497f40, "fn_00497f40", fn_00497f40_Entry, nullptr);

// 0x00497f60 (ecx = object; stack: out position, out direction or 0; ret 8; StigTurret +0x148, CameraPathObject
// +0x280, LightObject +0x280, RocketBudTurret +0x148, vtables 0x005a9bb0 +0x4f0, 0x005abf48 +0x288): the muzzle bone
// (+0x500) matrix via 0x00453c50(object, +0x500, &m, 1, 0, 0, 1, 0, 0); position = (m[3], m[7], m[11]), direction =
// -(m[1], m[5], m[9]). Returns the direction pointer. Not executed in recorded runs.
uint32_t fn_00497f60(uint32_t obj, uint32_t pos, uint32_t dir)
{
    float m[17];
    orig::fn_00453c50(obj, obj + 0x500, u(m), 1, 0, 0, 1, 0, 0);
    Fl(pos, 0) = m[3];
    D(pos, 4) = f2u(m[7]);
    D(pos, 8) = f2u(m[11]);
    if (dir)
    {
        Fl(dir, 0) = -m[1];
        Fl(dir, 4) = -m[5];
        Fl(dir, 8) = -m[9];
    }
    return dir;
}
static uint32_t fn_00497f60_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00497f60(ecx, a0, a1);
}
ABI_ENTRY_00497f60(fn_00497f60_Entry, fn_00497f60_Abi)
HOOK(0x00497f60, "fn_00497f60", fn_00497f60_Entry, nullptr);

// 0x00497fd0 (ecx = object; stack: 1 unused; ret 4; LightObject +0x27c, RocketBudTurret +0x144; also reached through
// the thunk 0x00498a70): muzzle flash. Needs muzzle_fx_name +0x4c0 and muzzle_bone_name +0x500 (else al = 0). Rapid
// fire (+0x530 & 0x20): flags |= 0x40, &= ~0x80, and when no fx is held Fx_Create 0x0041cc60 (al = 0, ecx = g_game
// +0xa90; name, desc {0x24, 8, .., bone at +0x18, object at +0x1c}) is kept in +0x534. Otherwise a one-shot Fx_Create
// with desc {4, .., bone, object}. Returns al = 1. Desc +4 is not written in the one-shot case (stack garbage in the
// original; 0 here).
uint32_t fn_00497fd0(uint32_t obj)
{
    uint32_t desc[32] = {};  // sub esp, 0x80
    if (!B(obj, 0x4c0))
        return 0;
    if (!B(obj, 0x500))
        return 0;
    const uint32_t name = obj + 0x4c0;
    const uint32_t bone = obj + 0x500;
    if (B(obj, 0x530) & 0x20)
    {
        uint32_t f = D(obj, 0x530) | 0x40;
        D(obj, 0x530) = f;
        f &= 0xffffff7fu;
        D(obj, 0x530) = f;
        if (D(obj, 0x534))
            return Lo8(bone, 1);
        desc[6] = bone;
        desc[7] = obj;
        desc[0] = 0x24;
        desc[1] = 8;
        const uint32_t fx = orig::fn_0041cc60(bone & 0xffffff00u, D(G(), 0xa90), name, u(desc));
        D(obj, 0x534) = fx;
        return Lo8(fx, 1);
    }
    desc[6] = bone;
    desc[7] = obj;
    desc[0] = 4;
    return Lo8(orig::fn_0041cc60(bone & 0xffffff00u, D(G(), 0xa90), name, u(desc)), 1);
}
static uint32_t fn_00497fd0_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return fn_00497fd0(ecx);
}
ABI_ENTRY_00497fd0(fn_00497fd0_Entry, fn_00497fd0_Abi)
HOOK(0x00497fd0, "fn_00497fd0", fn_00497fd0_Entry, nullptr);

// 0x004980c0 (ecx = object; stack: a0, a1, set, values; ret 0x10): mountable turret properties, called by the load
// slots 0x00498d60 (StigTurret), 0x004993e0, 0x00499c40 (CameraPathObject), 0x0049a6a0. The emitter part 0x00497d00;
// auto_aim (value, read into the values argument's stack slot) -> byte +0x538 = (!= 0); camera_bone_name +0x53c,
// zoom_camera_bone_name +0x55c, player_bone_name +0x57c, player_exit_bone_name +0x59c; turn_max_speed +0x5bc (2.0),
// pitch_max_speed +0x5c0 (2.0), zoom_turn_max_speed +0x5c4 (0.025), zoom_pitch_max_speed +0x5c8 (0.1); message_radius
// +0x624 (20); rapid_fire -> flag 0x20 of +0x530; ai_kickout_health_pct +0x60c (50, then * 0.01), +0x614 = 0,
// ai_kickout_stayout_secs +0x610 (10), ai_kickout_impact_force +0x618 (6000), ai_kickout_fx_name +0x5cc,
// ai_kickout_fx_bone +0x5ec; ObjectList_Link into World +0xec4, +0xeb0, +0xea8; +0x61c = +0x620 = +0x628 = 0, zoomed
// +0x539 = 0. Returns 0.
uint32_t fn_004980c0(uint32_t obj, uint32_t a0, uint32_t a1, uint32_t set, uint32_t values)
{
    uint32_t slot = values;
    fn_00497d00(obj, a0, a1, set, values);
    orig::fn_00453400(0, values, u(&slot), obj, 0x0058f5d8, set, 0);  // auto_aim
    B(obj, 0x538) = slot != 0 ? 1 : 0;
    orig::fn_004533a0(0x0058a9a4, obj + 0x53c, obj, 0x0058f5c4, set, values, 0);  // camera_bone_name
    orig::fn_004533a0(0x0058a9a4, obj + 0x55c, obj, 0x0058f5ac, set, values, 0);  // zoom_camera_bone_name
    orig::fn_004533a0(0x0058a9a4, obj + 0x57c, obj, 0x0058f598, set, values, 0);  // player_bone_name
    orig::fn_004533a0(0x0058a9a4, obj + 0x59c, obj, 0x005a065c, set, values, 0);  // player_exit_bone_name
    orig::fn_004534a0(values, obj + 0x5bc, obj, 0x0059f6a4, set, 0x40000000u, 0);  // turn_max_speed
    orig::fn_004534a0(values, obj + 0x5c0, obj, 0x0059f694, set, 0x40000000u, 0);  // pitch_max_speed
    orig::fn_004534a0(values, obj + 0x5c4, obj, 0x0058f584, set, 0x3ccccccdu, 0);  // zoom_turn_max_speed
    orig::fn_004534a0(values, obj + 0x5c8, obj, 0x0058f56c, set, 0x3dcccccdu, 0);  // zoom_pitch_max_speed
    orig::fn_00453400(0x14, values, obj + 0x624, obj, 0x005b1698, set, 0);         // message_radius
    orig::fn_004534f0(0, values, u(&slot), obj, 0x0058f560, set, 0);              // rapid_fire
    if (static_cast<uint8_t>(slot))
        D(obj, 0x530) |= 0x20;
    orig::fn_004534a0(values, obj + 0x60c, obj, 0x0058f548, set, 0x42480000u, 0);  // ai_kickout_health_pct
    Fl(obj, 0x60c) = Fl(obj, 0x60c) * K(0x005bcbec);
    orig::fn_004534a0(values, obj + 0x610, obj, 0x0058f530, set, 0x41200000u, 0);  // ai_kickout_stayout_secs
    D(obj, 0x614) = 0;
    orig::fn_004534a0(values, obj + 0x618, obj, 0x0058f518, set, 0x45bb8000u, 0);  // ai_kickout_impact_force
    orig::fn_004533a0(0x0058a9a4, obj + 0x5cc, obj, 0x0058f504, set, values, 0);   // ai_kickout_fx_name
    orig::fn_004533a0(0x0058a9a4, obj + 0x5ec, obj, 0x0058f4f0, set, values, 0);   // ai_kickout_fx_bone
    orig::fn_004546d0(D(World(), 0xec4), obj);
    orig::fn_004546d0(D(World(), 0xeb0), obj);
    orig::fn_004546d0(D(World(), 0xea8), obj);
    D(obj, 0x61c) = 0;
    D(obj, 0x620) = 0;
    D(obj, 0x628) = 0;
    B(obj, 0x539) = 0;
    return 0;
}
static uint32_t fn_004980c0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004980c0(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004980c0(fn_004980c0_Entry, fn_004980c0_Abi)
HOOK(0x004980c0, "fn_004980c0", fn_004980c0_Entry, nullptr);

// 0x00498310 (eax = turret): al = zoomed byte +0x539 (upper eax = the turret's). Callers 0x004990a0, 0x00499710,
// 0x0049a170, 0x0049b030.
uint32_t fn_00498310(uint32_t t)
{
    return Lo8(t, B(t, 0x539));
}
static uint32_t fn_00498310_Abi(uint32_t eax)
{
    return fn_00498310(eax);
}
ABI_ENTRY_00498310(fn_00498310_Entry, fn_00498310_Abi)
HOOK(0x00498310, "fn_00498310", fn_00498310_Entry, nullptr);

uint32_t fn_00498350(uint32_t t);
uint32_t fn_00498390(uint32_t t, uint32_t flag);

// 0x00498320 (eax = turret): toggles the zoom: zoomed -> 0x00498390 (esi = turret; 0), else 0x00498350 (edi = turret).
// Caller 0x0049b030. Not executed in recorded runs.
uint32_t fn_00498320(uint32_t t)
{
    if (B(t, 0x539))
        return fn_00498390(t, 0);
    return fn_00498350(t);
}
static uint32_t fn_00498320_Abi(uint32_t eax)
{
    return fn_00498320(eax);
}
ABI_ENTRY_00498320(fn_00498320_Entry, fn_00498320_Abi)
HOOK(0x00498320, "fn_00498320", fn_00498320_Entry, nullptr);

// 0x00498350 (edi = turret): zoom in: when not zoomed and zoom_camera_bone_name +0x55c is set, 0x0040fae0 (eax = that
// name, esi = camera g_game +0xa68) and zoomed = 1, al = 1; else al = 0. Caller 0x00498320. Not executed in recorded
// runs.
uint32_t fn_00498350(uint32_t t)
{
    if (B(t, 0x539))
        return 0;
    if (!B(t, 0x55c))
        return Lo8(t + 0x55c, 0);
    const uint32_t r = orig::fn_0040fae0(t + 0x55c, D(G(), 0xa68));
    B(t, 0x539) = 1;
    return Lo8(r, 1);
}
static uint32_t fn_00498350_Abi(uint32_t edi)
{
    return fn_00498350(edi);
}
ABI_ENTRY_00498350(fn_00498350_Entry, fn_00498350_Abi)
HOOK(0x00498350, "fn_00498350", fn_00498350_Entry, nullptr);

// 0x00498390 (esi = turret; stack: flag (byte); ret 4): zoom out: when zoomed, 0x0040fb50 (flag) or 0x0040fb00 (eax =
// camera g_game +0xa68), zoomed = 0, al = 1; else al = 0. Callers 0x00498320, 0x00498bb0, 0x0049afe0, 0x0049bd80. Not
// executed in recorded runs.
uint32_t fn_00498390(uint32_t t, uint32_t flag)
{
    if (!B(t, 0x539))
        return 0;
    uint32_t r;
    if (static_cast<uint8_t>(flag))
        r = orig::fn_0040fb50(D(G(), 0xa68));
    else
        r = orig::fn_0040fb00(D(G(), 0xa68));
    B(t, 0x539) = 0;
    return Lo8(r, 1);
}
static uint32_t fn_00498390_Abi(uint32_t esi, uint32_t a0)
{
    return fn_00498390(esi, a0);
}
ABI_ENTRY_00498390(fn_00498390_Entry, fn_00498390_Abi)
HOOK(0x00498390, "fn_00498390", fn_00498390_Entry, nullptr);

// 0x004983e0 (eax = turret): returns message_radius +0x624. Caller 0x004662a0.
uint32_t fn_004983e0(uint32_t t)
{
    return D(t, 0x624);
}
static uint32_t fn_004983e0_Abi(uint32_t eax)
{
    return fn_004983e0(eax);
}
ABI_ENTRY_004983e0(fn_004983e0_Entry, fn_004983e0_Abi)
HOOK(0x004983e0, "fn_004983e0", fn_004983e0_Entry, nullptr);

// 0x004983f0 (ecx = object; StigTurret +0xe4, CameraPathObject +0x21c, vtables 0x005a9bb0 +0x48c, 0x005abf48 +0x224):
// returns (vt+0x150() al == 0) ? 1 : 0 (neg/sbb/inc). Not executed in recorded runs.
uint32_t fn_004983f0(uint32_t obj)
{
    return static_cast<uint8_t>(VC<uint32_t>(obj, 0x150)) ? 0u : 1u;
}
static uint32_t fn_004983f0_Abi(uint32_t ecx)
{
    return fn_004983f0(ecx);
}
ABI_ENTRY_004983f0(fn_004983f0_Entry, fn_004983f0_Abi)
HOOK(0x004983f0, "fn_004983f0", fn_004983f0_Entry, nullptr);

namespace
{
// The inline "release the object held in *slot and clear the slot" (dec +0x28, vt+0xd8, 0x00487da0).
uint32_t ReleaseSlot(uint32_t slot)
{
    const uint32_t o = D(slot, 0);
    if (!o)
        return 0;
    const uint32_t r = Ref_Release(o);
    D(slot, 0) = 0;
    return r;
}
}  // namespace

// 0x00498400 (ecx = turret; stack: damage info; ret 4; StigTurret +0xd0, CameraPathObject +0x208, vtable 0x005abf48
// +0x210; caller 0x0049aeb0): the turret's damage handler. Ignored when its health (Object_GetHealth 0x004555b0) is
// <= 0, or when the attacker (info +0x2c) is on the same side (vt+0xe4 equal) and its vt+0xe0 returns al = 0. When
// the player rides it (vt+0x14c), the player (g_game +0x748) takes it: player vt+0xd0(info). Otherwise, when occupied
// (vt+0x150): health +0x454 -= info +0x14, capped at +0x458; at <= 0, vt+0xd4(0) (kill). When the health fraction
// (Object_GetMaxHealth 0x004555c0) crosses ai_kickout_health_pct +0x60c downwards, the AI occupant +0x628 is kicked
// out: a damage desc (flags 1, turret position, 0x00458720 (eax = occupant, edx = 0), occupant max health, direction
// (occupant - turret, normalised, z then set to 0.7 and normalised again), ai_kickout_impact_force +0x618, attacker =
// the player) goes to occupant vt+0xd0, +0x614 = ai_kickout_stayout_secs +0x610, and with ai_kickout_fx_name +0x5cc a
// one-shot Fx_Create 0x0041cc60 (desc {4, .., bone +0x5ec or 0 at +0x18, turret at +0x1c}).
uint32_t fn_00498400(uint32_t obj, uint32_t info)
{
    uint32_t L[57];  // sub esp, 0xe4 (8-aligned frame); L[i / 4] = [esp + 0xc + i] after the three pushes
    const float h0 = orig::fn_004555b0(obj);
    L[0x20 / 4] = f2u(h0);
    const float h1 = orig::fn_004555b0(obj);
    if (h1 < K(0x0056c05c) || h1 == K(0x0056c05c))
        return 0;
    const uint32_t attacker = D(info, 0x2c);
    if (attacker)
    {
        const uint8_t side = static_cast<uint8_t>(VC<uint32_t>(attacker, 0xe4));
        if (side == static_cast<uint8_t>(VC<uint32_t>(obj, 0xe4)))
        {
            if (!static_cast<uint8_t>(VC<uint32_t>(D(info, 0x2c), 0xe0)))
                return 0;
        }
    }
    if (static_cast<uint8_t>(VC<uint32_t>(obj, 0x14c)))
    {
        const uint32_t player = D(G(), 0x748);
        if (player)
            return VC<uint32_t>(player, 0xd0, info);
    }
    if (!static_cast<uint8_t>(VC<uint32_t>(obj, 0x150)))
        return 0;
    const float f = Fl(obj, 0x454) - Fl(info, 0x14);
    L[4 / 4] = f2u(f);
    D(obj, 0x454) = L[4 / 4];
    if (f > Fl(obj, 0x458))  // capped at the maximum
    {
        D(obj, 0x454) = D(obj, 0x458);
        return 0;
    }
    if (f < K(0x0056c05c) || f == K(0x0056c05c))
        return VC<uint32_t>(obj, 0xd4, 0u);
    const float pct = Fl(obj, 0x60c);
    const float before = h0 / orig::fn_004555c0(obj);
    if (!(before > pct))
        return 0;
    const float after = u2f(L[4 / 4]) / orig::fn_004555c0(obj);
    if (!(after < pct || after == pct))
        return 0;
    const uint32_t occ = D(obj, 0x628);
    if (!occ)
        return 0;
    std::memset(&L[0x30 / 4], 0, 0xd * 4);
    VC<uint32_t>(occ, 0x54, u(&L[0x24 / 4]));  // occupant position
    VC<uint32_t>(obj, 0x54, u(&L[0x14 / 4]));  // turret position
    Fl(u(L), 0x8) = Fl(u(L), 0x24) - Fl(u(L), 0x14);
    Fl(u(L), 0xc) = Fl(u(L), 0x28) - Fl(u(L), 0x18);
    Fl(u(L), 0x10) = Fl(u(L), 0x2c) - Fl(u(L), 0x1c);
    Vec3_Normalize(u(&L[0x8 / 4]), u(&L[0x8 / 4]));
    Fl(u(L), 0x44) = orig::fn_004555c0(occ);
    L[0x38 / 4] = L[0x18 / 4];
    L[0x3c / 4] = L[0x1c / 4];
    L[0x34 / 4] = L[0x14 / 4];
    L[0x40 / 4] = orig::fn_00458720(occ, 0);
    L[0x48 / 4] = L[0x8 / 4];
    L[0x4c / 4] = L[0xc / 4];
    L[0x54 / 4] = D(obj, 0x618);
    L[0x50 / 4] = L[0x10 / 4];
    L[0x50 / 4] = 0x3f333333u;  // 0.7
    Vec3_Normalize(u(&L[0x48 / 4]), u(&L[0x48 / 4]));
    L[0x5c / 4] = D(G(), 0x748);
    L[0x30 / 4] |= 1;
    uint32_t r = VC<uint32_t>(occ, 0xd0, u(&L[0x30 / 4]));
    D(obj, 0x614) = D(obj, 0x610);
    if (B(obj, 0x5cc))
    {
        std::memset(&L[0x64 / 4], 0, 0x20 * 4);
        L[0x80 / 4] = obj;
        L[0x64 / 4] = 4;
        L[0x7c / 4] = B(obj, 0x5ec) ? obj + 0x5ec : 0;
        r = orig::fn_0041cc60(u(&L[0x64 / 4]) & 0xffffff00u, D(G(), 0xa90), obj + 0x5cc, u(&L[0x64 / 4]));
    }
    return r;
}
static uint32_t fn_00498400_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00498400(ecx, a0);
}
ABI_ENTRY_00498400(fn_00498400_Entry, fn_00498400_Abi)
HOOK(0x00498400, "fn_00498400", fn_00498400_Entry, nullptr);

// 0x004986b0 (ecx = turret; StigTurret +0xdc, CameraPathObject +0x214; callers 0x00499630, 0x0049ac60): remove:
// 0x00497e90; when g_game +0xac8 is set and g_game +0xacc is this turret, vt+0x17c(); then releases the occupant
// reference +0x628 and clears it. Not executed in recorded runs.
uint32_t fn_004986b0(uint32_t obj)
{
    uint32_t r = fn_00497e90(obj);
    const uint32_t g = G();
    if (D(g, 0xac8) && D(g, 0xacc) == obj)
        r = VC<uint32_t>(obj, 0x17c);
    if (D(obj, 0x628))
        r = ReleaseSlot(obj + 0x628);
    return r;
}
static uint32_t fn_004986b0_Abi(uint32_t ecx)
{
    return fn_004986b0(ecx);
}
ABI_ENTRY_004986b0(fn_004986b0_Entry, fn_004986b0_Abi)
HOOK(0x004986b0, "fn_004986b0", fn_004986b0_Entry, nullptr);

// 0x00498710 (ecx = turret; callers 0x004990a0, 0x00499710, 0x0049a170, 0x0049b030): 0x00497ed0 (muzzle fx step), then
// the kickout stay-out timer +0x614 -= frame time (0x00604a90) while > 0. Returns 0x00497ed0's eax (the original's eax
// after fnstsw is not reproduced).
uint32_t fn_00498710(uint32_t obj)
{
    const uint32_t r = fn_00497ed0(obj);
    if (Fl(obj, 0x614) > K(0x0056c05c))
        Fl(obj, 0x614) = Fl(obj, 0x614) - K(0x00604a90);
    return r;
}
static uint32_t fn_00498710_Abi(uint32_t ecx)
{
    return fn_00498710(ecx);
}
ABI_ENTRY_00498710(fn_00498710_Entry, fn_00498710_Abi)
HOOK(0x00498710, "fn_00498710", fn_00498710_Entry, nullptr);

// 0x00498740 (eax = turret, cl = on): flag 1 of +0x530 set or cleared. Caller 0x004662a0.
void fn_00498740(uint32_t t, uint32_t on)
{
    const uint32_t f = D(t, 0x530);
    if (static_cast<uint8_t>(on))
        D(t, 0x530) = f | 1;
    else
        D(t, 0x530) = f & 0xfffffffeu;
}
static void fn_00498740_Abi(uint32_t eax, uint32_t ecx)
{
    fn_00498740(eax, ecx);
}
ABI_ENTRY_00498740(fn_00498740_Entry, fn_00498740_Abi)
HOOK(0x00498740, "fn_00498740", fn_00498740_Entry, nullptr);

// 0x00498760 (ecx = turret; StigTurret +0x184, CameraPathObject +0x2bc, vtables 0x005a9bb0 +0x52c, 0x005abf48 +0x2c4):
// al = (+0x530 & 1) && !(+0x530 & 4); upper eax = the flags. Not executed in recorded runs.
uint32_t fn_00498760(uint32_t obj)
{
    const uint32_t f = D(obj, 0x530);
    return Lo8(f, ((f & 1) && !(f & 4)) ? 1u : 0u);
}
static uint32_t fn_00498760_Abi(uint32_t ecx)
{
    return fn_00498760(ecx);
}
ABI_ENTRY_00498760(fn_00498760_Entry, fn_00498760_Abi)
HOOK(0x00498760, "fn_00498760", fn_00498760_Entry, nullptr);

// 0x00498780 (ecx = turret; StigTurret +0x14c, CameraPathObject +0x284, vtables 0x005a9bb0 +0x4f4, 0x005abf48 +0x28c):
// al = bit 3 of +0x530 (upper eax as the caller left it: 0 here). Not executed in recorded runs.
uint32_t fn_00498780(uint32_t obj)
{
    return (static_cast<uint32_t>(B(obj, 0x530)) >> 3) & 1u;
}
static uint32_t fn_00498780_Abi(uint32_t ecx)
{
    return fn_00498780(ecx);
}
ABI_ENTRY_00498780(fn_00498780_Entry, fn_00498780_Abi)
HOOK(0x00498780, "fn_00498780", fn_00498780_Entry, nullptr);

// 0x00498790 (ecx = turret; StigTurret +0x150, CameraPathObject +0x288, vtables 0x005a9bb0 +0x4f8, 0x005abf48 +0x290):
// al = bit 2 of +0x530 (occupied by an AI; upper eax: 0 here). Not executed in recorded runs.
uint32_t fn_00498790(uint32_t obj)
{
    return (static_cast<uint32_t>(B(obj, 0x530)) >> 2) & 1u;
}
static uint32_t fn_00498790_Abi(uint32_t ecx)
{
    return fn_00498790(ecx);
}
ABI_ENTRY_00498790(fn_00498790_Entry, fn_00498790_Abi)
HOOK(0x00498790, "fn_00498790", fn_00498790_Entry, nullptr);

// 0x004987a0 (ecx = turret; stack: a0; ret 4; StigTurret +0x158, vtables 0x005a9bb0 +0x500, 0x005abf48 +0x298): can an
// AI use it: vt+0x174(a0) al != 0, the kickout timer +0x614 <= 0, flag 8 clear and flags 0x14 clear -> al = 1, else
// al = 0. Not executed in recorded runs.
uint32_t fn_004987a0(uint32_t obj, uint32_t a0)
{
    const uint32_t r = VC<uint32_t>(obj, 0x174, a0);
    if (!static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    const float t = Fl(obj, 0x614);
    if (t > K(0x0056c05c))
        return 0;
    const uint32_t f = D(obj, 0x530);
    if (f & 8)
        return f & 0xffffff00u;
    return Lo8(f, (f & 0x14) ? 0u : 1u);
}
static uint32_t fn_004987a0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004987a0(ecx, a0);
}
ABI_ENTRY_004987a0(fn_004987a0_Entry, fn_004987a0_Abi)
HOOK(0x004987a0, "fn_004987a0", fn_004987a0_Entry, nullptr);

// 0x004987f0 (ecx = turret; stack: on (byte); ret 4; StigTurret +0x15c, CameraPathObject +0x294, vtables 0x005a9bb0
// +0x504, 0x005abf48 +0x29c): flag 0x10 of +0x530 set or cleared; al = 1. Not executed in recorded runs.
uint32_t fn_004987f0(uint32_t obj, uint32_t on)
{
    uint32_t f = D(obj, 0x530);
    if (static_cast<uint8_t>(on))
        f |= 0x10;
    else
        f &= 0xffffffefu;
    D(obj, 0x530) = f;
    return Lo8(f, 1);
}
static uint32_t fn_004987f0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004987f0(ecx, a0);
}
ABI_ENTRY_004987f0(fn_004987f0_Entry, fn_004987f0_Abi)
HOOK(0x004987f0, "fn_004987f0", fn_004987f0_Entry, nullptr);

// 0x00498820 (ecx = turret; stack: occupant; ret 4; StigTurret +0x160, CameraPathObject +0x298, vtables 0x005a9bb0
// +0x508, 0x005abf48 +0x2a0): AI enters: flag 4 of +0x530, vt+0x15c(0), the previous occupant reference +0x628 released,
// +0x628 = occupant with ++occupant+0x28; al = 1. Not executed in recorded runs.
uint32_t fn_00498820(uint32_t obj, uint32_t occ)
{
    D(obj, 0x530) |= 4;
    VC<uint32_t>(obj, 0x15c, 0u);
    ReleaseSlot(obj + 0x628);
    D(obj, 0x628) = occ;
    ++D(occ, 0x28);
    return Lo8(occ, 1);
}
static uint32_t fn_00498820_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00498820(ecx, a0);
}
ABI_ENTRY_00498820(fn_00498820_Entry, fn_00498820_Abi)
HOOK(0x00498820, "fn_00498820", fn_00498820_Entry, nullptr);

// 0x00498890 (ecx = turret; StigTurret +0x164, CameraPathObject +0x29c, vtables 0x005a9bb0 +0x50c, 0x005abf48 +0x2a4):
// AI leaves: flag 4 of +0x530 cleared, the occupant reference +0x628 released and cleared; al = 1. Not executed in
// recorded runs.
uint32_t fn_00498890(uint32_t obj)
{
    D(obj, 0x530) &= 0xfffffffbu;
    return Lo8(ReleaseSlot(obj + 0x628), 1);
}
static uint32_t fn_00498890_Abi(uint32_t ecx)
{
    return fn_00498890(ecx);
}
ABI_ENTRY_00498890(fn_00498890_Entry, fn_00498890_Abi)
HOOK(0x00498890, "fn_00498890", fn_00498890_Entry, nullptr);

// 0x004988e0 (ecx = turret; stack: 1 unused; ret 4; StigTurret +0x138; tail-jumped to by 0x004999c0, 0x0049a4c0,
// 0x0049be70): ready to fire: +0x530 |= 0x40, &= ~0x80; an occupant other than the player -> al = 1; else last fire
// time +0x524 == 0 -> al = 1, else eax = (time 0x00604a98 - +0x524 > recycle_time +0x520).
uint32_t fn_004988e0(uint32_t obj)
{
    const uint32_t f = D(obj, 0x530) | 0x40;
    D(obj, 0x530) = f;
    D(obj, 0x530) = f & 0xffffff7fu;
    const uint32_t occ = D(obj, 0x628);
    if (occ && occ != D(G(), 0x748))
        return Lo8(occ, 1);
    const float last = Fl(obj, 0x524);
    if (last == K(0x0056c05c))
        return 1;
    const float dt = K(0x00604a98) - last;
    return dt > Fl(obj, 0x520) ? 1u : 0u;
}
static uint32_t fn_004988e0_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return fn_004988e0(ecx);
}
ABI_ENTRY_004988e0(fn_004988e0_Entry, fn_004988e0_Abi)
HOOK(0x004988e0, "fn_004988e0", fn_004988e0_Entry, nullptr);

// 0x00498960 (ecx = turret; stack: a0, target; ret 8; StigTurret +0x13c; caller 0x0049a530): fires. With a target: the
// emitter bone (+0x4a0) matrix (0x00453c50(turret, +0x4a0, &m, 1, 0, 0, 1, 0, 0)), position (m[3], m[7], m[11]) and
// 0x0047de40 (eax = &position, ebx = target; g_game +0xaa8, occupant +0x628, projectile_name +0x460 when the occupant
// is the player else ai_projectile_name +0x480, 0). Without: 0x0047dbf0 (eax = emitter bone +0x4a0, ecx =
// projectile_name +0x460, edx = turret; g_game +0xaa8, player, auto_aim byte +0x538). A player occupant counts a shot
// (++g_game +0x2718); vt+0x144(0) (muzzle flash); last fire time +0x524 = time 0x00604a98. Returns (projectile != 0).
// Not executed in recorded runs.
uint32_t fn_00498960(uint32_t obj, uint32_t target)
{
    uint32_t L[20];  // sub esp, 0x50: L[0..2] = position, L[3..19] = matrix
    uint32_t proj;
    if (target)
    {
        orig::fn_00453c50(obj, obj + 0x4a0, u(&L[3]), 1, 0, 0, 1, 0, 0);
        L[2] = L[3 + 11];
        L[0] = L[3 + 3];
        L[1] = L[3 + 7];
        const uint32_t g = G();
        const uint32_t occ = D(obj, 0x628);
        const uint32_t name = occ == D(g, 0x748) ? obj + 0x460 : obj + 0x480;
        proj = orig::fn_0047de40(u(L), target, D(g, 0xaa8), occ, name, 0);
    }
    else
    {
        const uint32_t g = G();
        proj = orig::fn_0047dbf0(obj + 0x4a0, obj + 0x460, obj, D(g, 0xaa8), D(g, 0x748),
                                 static_cast<uint32_t>(B(obj, 0x538)));
    }
    const uint32_t occ = D(obj, 0x628);
    if (occ && occ == D(G(), 0x748))
        ++D(G(), 0x2718);
    VC<uint32_t>(obj, 0x144, 0u);
    D(obj, 0x524) = D(0x00604a98, 0);
    return proj != 0 ? 1u : 0u;
}
static uint32_t fn_00498960_Abi(uint32_t ecx, uint32_t /*a0*/, uint32_t a1)
{
    return fn_00498960(ecx, a1);
}
ABI_ENTRY_00498960(fn_00498960_Entry, fn_00498960_Abi)
HOOK(0x00498960, "fn_00498960", fn_00498960_Entry, nullptr);

// 0x00498a60 (ecx = turret; stack: 1 unused; ret 4; StigTurret +0x140, CameraPathObject +0x278; tail-jumped to by
// 0x00499bb0): al = auto_aim byte +0x538 (upper eax: 0 here). Not executed in recorded runs.
uint32_t fn_00498a60(uint32_t obj)
{
    return B(obj, 0x538);
}
static uint32_t fn_00498a60_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return fn_00498a60(ecx);
}
ABI_ENTRY_00498a60(fn_00498a60_Entry, fn_00498a60_Abi)
HOOK(0x00498a60, "fn_00498a60", fn_00498a60_Entry, nullptr);

namespace
{
// x87 fsin / fcos of a single-precision angle, as the original computes them (as src/game/game_05.cpp).
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
// The inline bone-callback registration of the turret load slots (0x00498ec0 in 0x00498d60): the model's node named
// by the string at obj+name (search 0x004e0f40 over the skeleton of 0x00408800 (eax = 4, ecx = obj)'s entry) gets
// {callback, obj} in the node table obj+0x410 +0x14 at its index (word +0xa of the node; a missing node is read
// through null as in the original).
void Turret_SetBoneCallback(uint32_t obj, uint32_t name, uint32_t callback)
{
    const uint32_t idx = D(orig::fn_00408800(4, obj), 0);
    const uint32_t e = D(D(D(D(D(obj, 0x414), 0xc) + idx * 12, 8), 0x2c), 4);
    uint32_t node = 0;
    const uint32_t first = D(e, 0x1c);
    if (first && D(e, 0x20) != 0)
    {
        const uint32_t s = D(e, 0x24);
        D(s, 0) = first;
        D(s, 0x18) = name;
        orig::fn_004e0f40(s);
        if (D(s, 0xc) == 0)
        {
            const uint32_t n = D(D(s, 0x10), D(s, 8) * 4);
            if (n)
                node = D(n + *reinterpret_cast<uint16_t*>(n + 2), 0);
        }
    }
    D(D(D(obj, 0x410), 0x14), static_cast<uint32_t>(*reinterpret_cast<uint16_t*>(node + 0xa)) * 8 + 4) = obj;
    D(D(D(obj, 0x410), 0x14), static_cast<uint32_t>(*reinterpret_cast<uint16_t*>(node + 0xa)) * 8) = callback;
}
}  // namespace

uint32_t fn_00498bb0(uint32_t obj);

// 0x00498a80 (ecx = turret; callers 0x00498f60, 0x00499bd0, 0x00499f30, 0x0049bcc0): the player mounts. Needs
// player_bone_name +0x57c and camera_bone_name +0x53c (else al = 0). With a player: player vt+0xf4(0), 0x004a0e00 (al
// = 0, esi = player +0xc24). ++turret+0x28; +0x530 |= 8; 0x00408e30 (eax = 0x81, ecx = turret, edx = 1; 0 x5); camera
// 0x0040e200 (eax = g_game +0xa68, ebx = camera bone; turret, 0); Object_Attach 0x004539f0 (eax = player bone, ecx =
// turret, edx = player; 0, 1, 0xc); Object_setPhysicsEnabled 0x004577b0 (player, 1); the occupant +0x628 = the player
// (old one released, ++player+0x28); player vt+0x134(0). al = 1. (The player is dereferenced unchecked after the first
// test, as in the original.) Not executed in recorded runs.
uint32_t fn_00498a80(uint32_t obj)
{
    if (!B(obj, 0x57c))
        return 0;
    if (!B(obj, 0x53c))
        return 0;
    const uint32_t player0 = D(G(), 0x748);
    if (player0)
    {
        VC<uint32_t>(player0, 0xf4, 0u);
        orig::fn_004a0e00(G() & 0xffffff00u, D(G(), 0x748) + 0xc24);
    }
    ++D(obj, 0x28);
    D(obj, 0x530) |= 8;
    orig::fn_00408e30(0x81, obj, 1, 0, 0, 0, 0, 0);
    orig::fn_0040e200(D(G(), 0xa68), obj + 0x53c, obj, 0);
    orig::fn_004539f0(obj + 0x57c, obj, D(G(), 0x748), 0, 1, 0xc);
    orig::fn_004577b0(D(G(), 0x748), 1);
    ReleaseSlot(obj + 0x628);
    const uint32_t player = D(G(), 0x748);
    D(obj, 0x628) = player;
    ++D(player, 0x28);
    return Lo8(VC<uint32_t>(D(G(), 0x748), 0x134, 0u), 1);
}
static uint32_t fn_00498a80_Abi(uint32_t ecx)
{
    return fn_00498a80(ecx);
}
ABI_ENTRY_00498a80(fn_00498a80_Entry, fn_00498a80_Abi)
HOOK(0x00498a80, "fn_00498a80", fn_00498a80_Entry, nullptr);

// 0x00498bb0 (ecx = turret; vtable 0x005abf48 slot +0x2bc; callers 0x00498fb0, 0x00499f80): the player dismounts.
// Zoom out (0x00498390, esi = turret; 1); +0x530 &= ~8; with a player: player vt+0xf4(1), 0x004a0e00 (al = 1, esi =
// player +0xc24), 0x004757b0 (esi = player), Object_Detach 0x00453b20 (esi = player), Object_setPhysicsEnabled
// (player, 0). With player_exit_bone_name +0x59c the player is placed at that bone (0x00453c50 matrix, Object_SetPosition
// 0x00457870 (al = 0 over m[7], ecx = &position, edx = player)). Object_ForceGround 0x00455860 (edi = player; 10, 20);
// g_game +0xac8 = +0xacc = 0; the turret's own reference released (--+0x28 ...); +0x61c = +0x620 = 0; the occupant
// +0x628 released and cleared; player vt+0x134(1). al = 1. Not executed in recorded runs.
uint32_t fn_00498bb0(uint32_t obj)
{
    uint32_t L[20];  // sub esp, 0x50: L[0..2] = position, L[3..19] = matrix
    fn_00498390(obj, 1);
    D(obj, 0x530) &= 0xfffffff7u;
    const uint32_t player = D(G(), 0x748);
    if (player)
    {
        VC<uint32_t>(player, 0xf4, 1u);
        orig::fn_004a0e00(Lo8(G(), 1), D(G(), 0x748) + 0xc24);
        orig::fn_004757b0(D(G(), 0x748));
        orig::fn_00453b20(D(G(), 0x748));
        orig::fn_004577b0(D(G(), 0x748), 0);
    }
    if (B(obj, 0x59c))
    {
        orig::fn_00453c50(obj, obj + 0x59c, u(&L[3]), 1, 0, 0, 1, 0, 0);
        L[0] = L[3 + 3];
        L[1] = L[3 + 7];
        L[2] = L[3 + 11];
        orig::fn_00457870(L[1] & 0xffffff00u, u(L), D(G(), 0x748), 0, 0, 0);
    }
    orig::fn_00455860(D(G(), 0x748), 0x41200000u, 0x41a00000u);
    D(G(), 0xac8) = 0;
    D(G(), 0xacc) = 0;
    Ref_Release(obj);
    D(obj, 0x61c) = 0;
    D(obj, 0x620) = 0;
    ReleaseSlot(obj + 0x628);
    return Lo8(VC<uint32_t>(D(G(), 0x748), 0x134, 1u), 1);
}
static uint32_t fn_00498bb0_Abi(uint32_t ecx)
{
    return fn_00498bb0(ecx);
}
ABI_ENTRY_00498bb0(fn_00498bb0_Entry, fn_00498bb0_Abi)
HOOK(0x00498bb0, "fn_00498bb0", fn_00498bb0_Entry, nullptr);

// 0x00498d30 (ecx = turret; StigTurret +0x180, CameraPathObject +0x2b8, vtables 0x005a9bb0 +0x528, 0x005abf48 +0x2c0):
// when the player is mounted on this turret (g_game +0xac8 set and +0xacc == turret) vt+0x17c (dismount), else vt+0x178
// (mount); al = 1. Not executed in recorded runs.
uint32_t fn_00498d30(uint32_t obj)
{
    const uint32_t g = G();
    if (D(g, 0xac8) && D(g, 0xacc) == obj)
        return Lo8(VC<uint32_t>(obj, 0x17c), 1);
    return Lo8(VC<uint32_t>(obj, 0x178), 1);
}
static uint32_t fn_00498d30_Abi(uint32_t ecx)
{
    return fn_00498d30(ecx);
}
ABI_ENTRY_00498d30(fn_00498d30_Entry, fn_00498d30_Abi)
HOOK(0x00498d30, "fn_00498d30", fn_00498d30_Entry, nullptr);

// 0x00498d60 (ecx = turret; stack: a0, a1, set, values; ret 0x10; StigTurret vtable 0x005a19d8 slot 6, the load slot).
// The mountable-turret properties 0x004980c0; pivot_bone_name +0x62c; pitch_range_min/max +0x64c/+0x650, yaw_range_min/
// max +0x654/+0x658 (-45/45 each, then * 0.0174533, degrees to radians, after the bools); ai_can_use (default 1) read
// twice (the same name 0x0058f494 both times) -> flag 0x100; ai_never_leave not set -> flag 0x200 (bools into the values
// argument's stack slot); the pivot bone gets the callback 0x00499670 (Turret_SetBoneCallback); vt+0xf8(8, 0). Not
// executed in recorded runs.
uint32_t fn_00498d60(uint32_t obj, uint32_t a0, uint32_t a1, uint32_t set, uint32_t values)
{
    uint32_t slot = values;
    fn_004980c0(obj, a0, a1, set, values);
    orig::fn_004533a0(0x0058a9a4, obj + 0x62c, obj, 0x0058f4e0, set, values, 0);   // pivot_bone_name
    orig::fn_004534a0(values, obj + 0x64c, obj, 0x0058f4d0, set, 0xc2340000u, 0);  // pitch_range_min
    orig::fn_004534a0(values, obj + 0x650, obj, 0x0058f4c0, set, 0x42340000u, 0);  // pitch_range_max
    orig::fn_004534a0(values, obj + 0x654, obj, 0x0058f4b0, set, 0xc2340000u, 0);  // yaw_range_min
    orig::fn_004534a0(values, obj + 0x658, obj, 0x0058f4a0, set, 0x42340000u, 0);  // yaw_range_max
    orig::fn_004534f0(1, values, u(&slot), obj, 0x0058f494, set, 0);              // ai_can_use
    if (static_cast<uint8_t>(slot))
        D(obj, 0x530) |= 0x100;
    orig::fn_004534f0(1, values, u(&slot), obj, 0x0058f494, set, 0);  // ai_can_use (again)
    if (static_cast<uint8_t>(slot))
        D(obj, 0x530) |= 0x100;
    orig::fn_004534f0(0, values, u(&slot), obj, 0x0058f484, set, 0);  // ai_never_leave
    if (!static_cast<uint8_t>(slot))
        D(obj, 0x530) |= 0x200;
    Fl(obj, 0x64c) = Fl(obj, 0x64c) * K(0x005bcba0);
    Fl(obj, 0x650) = Fl(obj, 0x650) * K(0x005bcba0);
    Fl(obj, 0x654) = Fl(obj, 0x654) * K(0x005bcba0);
    Fl(obj, 0x658) = Fl(obj, 0x658) * K(0x005bcba0);
    Turret_SetBoneCallback(obj, obj + 0x62c, 0x00499670);
    return VC<uint32_t>(obj, 0xf8, 8u, 0u);
}
static uint32_t fn_00498d60_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00498d60(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_00498d60(fn_00498d60_Entry, fn_00498d60_Abi)
HOOK(0x00498d60, "fn_00498d60", fn_00498d60_Entry, nullptr);

// 0x00498f60 (ecx = turret; StigTurret +0x178): mount (0x00498a80); on success the sound "wpn_stig_mount" (g_game +0x2c
// vt+0xd0(0x0058f474, 0, 0, 0, -1, 0)), g_game +0xac8 = 4 and +0xacc = turret; al = 1. Not executed in recorded runs.
uint32_t fn_00498f60(uint32_t obj)
{
    const uint32_t r = fn_00498a80(obj);
    if (!static_cast<uint8_t>(r))
        return r;
    VC<uint32_t>(D(G(), 0x2c), 0xd0, 0x0058f474u, 0u, 0u, 0u, 0xffffffffu, 0u);
    D(G(), 0xac8) = 4;
    D(G(), 0xacc) = obj;
    return Lo8(G(), 1);
}
static uint32_t fn_00498f60_Abi(uint32_t ecx)
{
    return fn_00498f60(ecx);
}
ABI_ENTRY_00498f60(fn_00498f60_Entry, fn_00498f60_Abi)
HOOK(0x00498f60, "fn_00498f60", fn_00498f60_Entry, nullptr);

// 0x00498fb0 (ecx = turret; StigTurret +0x17c): dismount (0x00498bb0); on success g_game +0x2c vt+0xd8(&+0x65c, 0)
// (stops the loop sound) and +0x65c = 0; al = 1. Not executed in recorded runs.
uint32_t fn_00498fb0(uint32_t obj)
{
    const uint32_t r = fn_00498bb0(obj);
    if (!static_cast<uint8_t>(r))
        return r;
    const uint32_t r2 = VC<uint32_t>(D(G(), 0x2c), 0xd8, obj + 0x65c, 0u);
    D(obj, 0x65c) = 0;
    return Lo8(r2, 1);
}
static uint32_t fn_00498fb0_Abi(uint32_t ecx)
{
    return fn_00498fb0(ecx);
}
ABI_ENTRY_00498fb0(fn_00498fb0_Entry, fn_00498fb0_Abi)
HOOK(0x00498fb0, "fn_00498fb0", fn_00498fb0_Entry, nullptr);

// 0x00498ff0 (eax = turret, ebx = source pose; stack: dest pose, 2 unused; ret 0xc): the pivot-bone callback body
// (same shape as the one registered at 0x00499670: guess). Copies 0xb dwords of the pose, then rotates it: qz = (0*s,
// 0*s, s, c) of pitch +0x620 * 0.5 and qx = (s, s*0, s*0, c) of yaw +0x61c * 0.5 (x87 fsin/fcos), 0x004f1710 (eax = &qx,
// ecx = &qz, esi = &q) and 0x004f1710 (eax = &q, ecx = source +0xc, esi = dest +0xc); dest +0x28 = source +0x28 | 1.
uint32_t fn_00498ff0(uint32_t t, uint32_t src, uint32_t dst)
{
    float L[12];  // sub esp, 0x30: L[0..3] = qx, L[4..7] = qz, L[8..11] = product
    const float p = Fl(t, 0x620) * K(0x005bcb4c);
    L[3] = Fl(t, 0x61c);
    std::memcpy(P(dst), P(src), 0xb * 4);
    const float sp = X87Sin(p);
    const float z = K(0x0056c05c) * sp;
    L[4] = z;
    L[5] = z;
    L[6] = sp;
    L[7] = X87Cos(p);
    const float y = L[3] * K(0x005bcb4c);
    const float sy = X87Sin(y);
    L[0] = sy;
    const float zy = sy * K(0x0056c05c);
    L[1] = zy;
    L[2] = zy;
    L[3] = X87Cos(y);
    orig::fn_004f1710(u(&L[0]), u(&L[4]), u(&L[8]));
    orig::fn_004f1710(u(&L[8]), src + 0xc, dst + 0xc);
    const uint32_t r = D(src, 0x28) | 1;
    D(dst, 0x28) = r;
    return r;
}
static uint32_t fn_00498ff0_Abi(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/)
{
    return fn_00498ff0(eax, ebx, a0);
}
ABI_ENTRY_00498ff0(fn_00498ff0_Entry, fn_00498ff0_Abi)
HOOK(0x00498ff0, "fn_00498ff0", fn_00498ff0_Entry, nullptr);
