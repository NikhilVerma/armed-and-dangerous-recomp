// Recovered: fx script opcodes. S2, Approach B.
//   0x00423680 Fx_AttachMesh (opcode 0x13)
#include "fx.h"

#include "game.h"
#include "game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

void (*g_fxMeshAttached)(void* object, const char* name) = nullptr;

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
}  // namespace

// 0x00423680 (ebx = fx instance; stack: params, name, attachPoints; callee pops 0xc; only caller 0x00420770).
// Fx script opcode 0x13: when the fx allows locked objects (fx+0x14 bit 2) and its owner (fx+0xb4) answers its
// virtual +0xd8, spawns object `name` through the factory 0x0045f020 (class 0x005bbb04, world Game+0xa84, flags
// 0x400, 0xaa), optionally inserts it into the world lists +0xebc / +0xe74, attaches it to the owner at a point
// picked from `attachPoints` (0x0041cba0) and returns it with +0x468 = fx start time - frame time. Level 1 uses it
// for the camera_fx snow cylinders "fxo_snow" / "fxo_snow_2". Otherwise logs (0x00407ff0) or returns null.
void* Fx_AttachMesh(void* fx, void* params, const char* name, void* attachPoints)
{
    // 0x00423680: test byte [ebx+0x14], 4 / je 0x42377f
    if (!(Field<uint8_t>(fx, 0x14) & 4))
    {
        // 0x0042377f..0x0042379c: log(Game+0x34, 10, "Fx : Unable to create locked object ...%s",
        // [params+8]+0x24); caller pops 0x10. Then 0x0042379f returns 0.
        orig::fn_00407ff0(Field<uint32_t>(g_game, 0x34), 10u, 0x005b7e28u,
                          Field<uint32_t>(params, 8) + 0x24);
        return nullptr;
    }

    // 0x00423690..0x004236a0: owner (fx+0xb4) virtual +0xd8 (ecx = owner), al == 0 -> return 0.
    void* owner = Field<void*>(fx, 0xb4);
    if (!VCall<bool>(owner, 0xd8 / 4))
        return nullptr;

    // 0x004236a6..0x004236d8: eax = 0x005bbb04, ecx = name;
    // stack (Game+0xa84, 0, 0x400, 0, 0, 0, 0, 0xaa, 0), callee pops. The result is not null-checked.
    void* world = Field<void*>(g_game, 0xa84);
    void* obj = reinterpret_cast<void*>(orig::fn_0045f020(0x005bbb04u, u(name), u(world), 0u, 0x400u, 0u, 0u, 0u,
                                                          0u, 0xaau, 0u));

    // 0x004236da..0x004236fc: byte fx+0x14 negative (bit 7): obj+0x464 |= 0x100, then 0x004546d0 with
    // esi = Game+0xa84 -> +0xebc, edi = obj (list add).
    if (static_cast<int8_t>(Field<uint8_t>(fx, 0x14)) < 0)
    {
        Field<uint32_t>(obj, 0x464) |= 0x100;
        orig::fn_004546d0(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xebc), u(obj));
    }

    // 0x00423701..0x0042371b: dword fx+0x14 bit 10 (test ah, 4): 0x004546d0 with esi = Game+0xa84 -> +0xe74,
    // edi = obj.
    if (Field<uint32_t>(fx, 0x14) & 0x400)
        orig::fn_004546d0(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xe74), u(obj));

    // 0x00423720..0x00423725: 0x0041cba0(eax = attachPoints; stack: params). Its "ecx input" is only the
    // `push ecx` at 0x0041cba0 that reserves a local slot ([esp+0x10]); the slot is written at 0x0041cbc8 before
    // its only read at 0x0041cbf4, so ecx's value is never used: pass 0.
    uint32_t attachPoint = orig::fn_0041cba0(u(attachPoints), 0u, u(params));

    // 0x0042372a..0x0042373d: 0x004539f0(eax = fx+0xd8, ecx = owner (reloaded from fx+0xb4), edx = obj;
    // stack: attachPoint, 1, 0), callee pops. All three registers are loaded explicitly here and read at
    // 0x004539f4..0x004539f8. Its edx output is not used: 0x0042375a overwrites edx.
    orig::fn_004539f0(Field<uint32_t>(fx, 0xd8), Field<uint32_t>(fx, 0xb4), u(obj), attachPoint, 1u, 0u);

    // 0x00423742..0x00423755: obj+0x424 |= 0x20000, then 0x00453750(obj) (stack, callee pops).
    Field<uint32_t>(obj, 0x424) |= 0x20000;
    orig::fn_00453750(u(obj));

    // 0x0042375a..0x00423767: obj virtual +0xd8 (ecx = obj), al == 0 -> return 0.
    if (!VCall<bool>(obj, 0xd8 / 4))
        return nullptr;

    // 0x00423769..0x00423774: fld [fx+0xc]; fsub [0x00604a90] (frame time); fstp dword [obj+0x468] (single).
    Field<float>(obj, 0x468) = Field<float>(fx, 0xc) - GameGlobal<float>(0x00604a90);

    if (g_fxMeshAttached)
        g_fxMeshAttached(obj, name);
    return obj;  // 0x0042376c: eax = edi
}

static uint32_t Fx_AttachMesh_Abi(uint32_t ebx, uint32_t params, uint32_t name, uint32_t attachPoints)
{
    return u(Fx_AttachMesh(reinterpret_cast<void*>(ebx), reinterpret_cast<void*>(params),
                           reinterpret_cast<const char*>(name), reinterpret_cast<void*>(attachPoints)));
}
ABI_ENTRY_00423680(Fx_AttachMesh_Entry, Fx_AttachMesh_Abi)
HOOK(0x00423680, "Fx_AttachMesh", Fx_AttachMesh_Entry, nullptr);
