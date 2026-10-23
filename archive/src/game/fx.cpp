// Recovered: fx script opcodes. S2, Approach B.
//   0x00423680 Fx_AttachMesh (opcode 0x13)
//   0x004234d0 Fx_CreateObject (opcode 0x05)
#include "fx.h"

#include "game.h"
#include "game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

void (*g_fxMeshAttached)(void* object, const char* name) = nullptr;
void (*g_fxObjectCreated)(void* object, const char* name) = nullptr;
bool (*g_fxAttachAllowed)(void* fx, const char* name) = nullptr;

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
    if (g_fxAttachAllowed && !g_fxAttachAllowed(fx, name))
        return nullptr;  // as the owner refusing above: the script carries on without the object

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

// 0x004234d0 (ebx = fx instance; stack: 5 arguments, only the second (object name) is read; callee pops 0x14;
// only caller 0x00420770). Fx script opcode 0x05: spawns object `name` through the factory 0x0045f020 (same
// arguments as Fx_AttachMesh), adds it to the world lists by fx+0x14 bits 7 and 10, then places it by the fx
// flags: bit 0 attach to fx+0x6c (0x004539f0) and set +0x424 bit 0x20000; else bit 1 with bit 6 clear: attach
// (0x004539f0, 0x00453750) and, if the object answers +0xd8, 0x00453b20; bit 1 with bit 6: set the full transform
// (0x00457990) from fx+0x70, combined with the bone/point fx+0x68 of the object when set; neither: position only
// (0x00457870, fx+0x18). Returns the object with +0x468 = fx start time - frame time, or null when an attach
// target does not answer +0xd8. Rain and the level-1 snow puffs use it (docs/subsystems/weather.md).
void* Fx_CreateObject(void* fx, uint32_t a0, const char* name, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0, (void)a2, (void)a3, (void)a4;
    // 0x004234d0..0x0042350b: factory(eax = 0x005bbb04, ecx = name; stack Game+0xa84, 0, 0x400, 0, 0, 0, 0, 0xaa,
    // 0), callee pops. Not null-checked.
    void* world = Field<void*>(g_game, 0xa84);
    void* obj = reinterpret_cast<void*>(orig::fn_0045f020(0x005bbb04u, u(name), u(world), 0u, 0x400u, 0u, 0u, 0u,
                                                          0u, 0xaau, 0u));
    // 0x0042350d..0x00423537: fx+0x14 bit 7: obj+0x464 |= 0x100, list add Game+0xa84 -> +0xebc.
    if (static_cast<int8_t>(Field<uint8_t>(fx, 0x14)) < 0)
    {
        Field<uint32_t>(obj, 0x464) |= 0x100;
        orig::fn_004546d0(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xebc), u(obj));
    }
    // 0x0042353c..0x00423556: fx+0x14 bit 10: list add +0xe74.
    if (Field<uint32_t>(fx, 0x14) & 0x400)
        orig::fn_004546d0(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xe74), u(obj));

    uint32_t flags = Field<uint32_t>(fx, 0x14);  // 0x0042355b
    void* target = Field<void*>(fx, 0x6c);
    if (flags & 1)
    {
        // 0x00423562..0x00423590: target virtual +0xd8, then 0x004539f0(eax = fx+0x64, ecx = target, edx = obj;
        // stack fx+0x68, 1, 0); obj+0x424 |= 0x20000.
        if (!VCall<bool>(target, 0xd8 / 4))
            return nullptr;
        orig::fn_004539f0(Field<uint32_t>(fx, 0x64), u(target), u(obj), Field<uint32_t>(fx, 0x68), 1u, 0u);
        Field<uint32_t>(obj, 0x424) |= 0x20000;
    }
    else if (flags & 2)
    {
        if (!(flags & 0x40))
        {
            // 0x004235a1..0x004235ed: as above, then 0x00453750(obj); obj +0xd8 must answer, then 0x00453b20.
            if (!VCall<bool>(target, 0xd8 / 4))
                return nullptr;
            orig::fn_004539f0(Field<uint32_t>(fx, 0x64), u(target), u(obj), Field<uint32_t>(fx, 0x68), 1u, 0u);
            orig::fn_00453750(u(obj));
            if (!VCall<bool>(obj, 0xd8 / 4))
                return nullptr;
            orig::fn_00453b20(u(obj));
        }
        else if (Field<uint32_t>(fx, 0x68) != 0)
        {
            // 0x004235f6..0x0042363d: point transform of obj at fx+0x68 into `point` (0x00453c50, 9 stack
            // arguments, callee pops), inverse (0x004e2b40), times fx+0x70 (0x004e2910, caller pops), set as the
            // object's transform (0x00457990 with al = 0; eax's upper bytes are 0x004e2910's result).
            alignas(16) uint8_t point[0x44], inverse[0x44];
            orig::fn_00453c50(u(obj), Field<uint32_t>(fx, 0x68), u(point), 1u, 0u, 0u, 0u, 0u, 1u);
            orig::fn_004e2b40(u(point), u(inverse));
            uint32_t r = orig::fn_004e2910(u(inverse), u(point), u(fx) + 0x70);
            orig::fn_00457990(r & 0xffffff00u, u(obj), u(point));
        }
        else
        {
            // 0x0042363f..0x0042364b: 0x00457990(al = 0; stack obj, fx+0x70). eax was fx+0x68 (zero here).
            orig::fn_00457990(0u, u(obj), u(fx) + 0x70);
        }
    }
    else
    {
        // 0x0042364d..0x00423654: 0x00457870(al = 0, upper bytes from fx+0x14; ecx = fx+0x18, edx = obj). It has
        // an ebp frame and an aligned local area, no stack arguments (the ABI tool's 3 are its locals).
        orig::fn_00457870(flags & 0xffffff00u, u(fx) + 0x18, u(obj), 0u, 0u, 0u);
    }
    // 0x00423659..0x00423664: obj+0x468 = fx+0xc - frame time (0x00604a90).
    Field<float>(obj, 0x468) = Field<float>(fx, 0xc) - GameGlobal<float>(0x00604a90);
    if (g_fxObjectCreated)
        g_fxObjectCreated(obj, name);
    return obj;
}

static uint32_t Fx_CreateObject_Abi(uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return u(Fx_CreateObject(reinterpret_cast<void*>(ebx), a0, reinterpret_cast<const char*>(a1), a2, a3, a4));
}
ABI_ENTRY_004234d0(Fx_CreateObject_Entry, Fx_CreateObject_Abi)
HOOK(0x004234d0, "Fx_CreateObject", Fx_CreateObject_Entry, nullptr);
